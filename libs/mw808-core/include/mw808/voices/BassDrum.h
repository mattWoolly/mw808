// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Bass drum. A block model of the original circuit (research/01 §1,
// research/05 §2), simulated in volts:
//
//   trigger (1 ms, 4..14 V)
//     -> pulse shaper  C40 15n || R163 100k in series, R162 4.7k shunt, clamp
//        diode D53: a spike of the full pulse height that relaxes (67 us) to
//        4.5 % of it, and a negative kick at the falling edge that the diode
//        holds near -0.7 V whatever the accent.
//     -> bridged-T     C41 = C42 15n, R167 1M bridge; junction to ground through
//        R161 1M || R170 470k || (R165 47k + R166 6.8k): 46.05k -> 49.4 Hz.
//     -> attack state  Q43 shorts R165 for a few ms after the trigger: the
//        junction resistance drops to 6.66k and the resonator runs at 130 Hz
//        for one cycle (the first upward zero crossing lands at 6 ms).
//     -> re-kick       when the attack state ends, C39 33n dumps its charge into
//        the junction through R161 (tau about 40 ms), restarting the ring at
//        the normal pitch. It does not depend on the accent, so it carries
//        relatively more of a soft hit.
//     -> pitch sigh    while the junction swings below about -0.57 V, Q43 leaks
//        (softplus law, Werner eq. 8) and lowers the junction resistance: an
//        amplitude-dependent self-FM, deeper and longer on loud, long notes.
//        Its secant conductance is evaluated every sample at the junction
//        voltage (one fixed-point iteration of the implicit step) and folded
//        into the resonator's coefficient.
//     -> decay         the feedback buffer returns -G Vout through R170, with
//        G = (R169 47k || VR6 500k(B) * knob) / R164 47k. Folded into the
//        damping: d = d_passive (1 - L), L = 1.064 G <= 0.972 with stock parts,
//        so the stock circuit cannot self-oscillate.
//     -> tone          R171 220 + (R172 10k || VR5 10k(C)) into C45 0.1u: a
//        one-pole low-pass, 305 Hz .. 7.2 kHz, that mostly sets the click level.
//
// Derived from the schematic values: pitch (49.4 Hz), attack pitch (130 Hz),
// decay range (T60 0.10 s .. 3.75 s; -20 dB in 330 ms at 12 o'clock), tone
// range, shaper, the form of the sigh law.
// Fitted against reference recordings of a real unit: the sigh's knee, slope
// and softness, the attack length (6.8 ms to the end of Q43's conduction; the
// manual says 4 ms, published measurements 5..6 ms), the re-kick height and
// time constant, and the TONE pot's taper mid-point. Below the normal 4 V the
// envelope generator is faded rather than switched (the hardware would not fire
// it at all under about 2.4 V), which keeps weak hits monotonic.
// Fitted against the service-manual levels (3.5 Vpp normal, 10 Vpp full
// accent): the output-stage gain and the accent transfer exponent.
//
// Non-hardware controls. `shape` (Sweep) scales the Q43 leakage: 0.5 is stock,
// 0 removes the sigh, 1 doubles it. `tune` and Hit::pitchSemis scale every
// timing capacitor (C38..C42) and the pulse width seen by the voice, so the
// whole waveform is transposed while the decay TIME stays put; a new pitch
// glides the still-ringing resonator, nothing is reset.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "mw808/Voices.h"
#include "mw808/dsp/BridgedT.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Math.h"
#include "mw808/dsp/TriggerPulse.h"

namespace mw808::voices {

class BassDrum {
public:
    void prepare(const VoiceSetup& rawSetup) noexcept {
        const VoiceSetup setup = dsp::saneSetup(rawSetup);
        fs_ = setup.sampleRate;
        T_ = 1.0 / fs_;
        auto part = [&setup](std::uint32_t id, double nominal, float tolerance) {
            return nominal * static_cast<double>(dsp::componentScale(
                                 setup.unitSeed, kComponentBase + id, tolerance, setup.variance));
        };
        const double c41 = part(0, 15.0e-9, kCapTol);
        c42_ = part(1, 15.0e-9, kCapTol);
        const double r167 = part(2, 1.0e6, kResTol);
        r165_ = part(3, 47.0e3, kResTol);
        r166_ = part(4, 6.8e3, kResTol);
        r161_ = part(5, 1.0e6, kResTol);
        r170_ = part(6, 470.0e3, kResTol);
        r169_ = part(7, 47.0e3, kResTol);
        r164_ = part(8, 47.0e3, kResTol);
        vr6_ = part(9, 500.0e3, kResTol);
        const double c40 = part(10, 15.0e-9, kCapTol);
        const double r162 = part(11, 4.7e3, kResTol);
        const double r163 = part(12, 100.0e3, kResTol);
        c45_ = part(13, 0.1e-6, kCapTol);
        r171_ = part(14, 220.0, kResTol);
        r172_ = part(15, 10.0e3, kResTol);
        vr5_ = part(16, 10.0e3, kResTol);
        const double c39 = part(17, 33.0e-9, kCapTol);
        const double c38Scale = part(18, 1.0, kCapTol);
        const double r160 = part(19, 22.0e3, kResTol);

        a0_ = 1.0 / (r167 * c41);
        invC42_ = 1.0 / c42_;
        dPassive_ = (c41 + c42_) / (r167 * c41 * c42_);
        gFixed_ = 1.0 / r161_ + 1.0 / r170_;
        gBranchOff_ = 1.0 / (r165_ + r166_);
        shaperTau_ = c40 * (r162 * r163) / (r162 + r163);
        shaperDc_ = r162 / (r162 + r163);
        attackSeconds_ = kAttackSeconds * c38Scale;
        chargeTau_ = r160 * c39;
        leakTau_ = kRekickTauTrim * c39 *
                   (r161_ + 1.0 / (1.0 / r170_ + 1.0 / (r165_ + r166_)));
        // Q43's leakage at a grounded junction: subtracted so the sigh is an
        // exact zero for a silent resonator. The secant conductance
        // (i_C(v) - i_C(0)) / v is tabulated across the knee.
        iC0_ = collectorCurrent(0.0);
        for (int i = 0; i <= kLeakTableSize; ++i) {
            const double v = kLeakTableLow + static_cast<double>(i) / kLeakTableScale;
            leakTable_[static_cast<std::size_t>(i)] =
                std::abs(v) > 1.0e-6 ? (collectorCurrent(v) - iC0_) / v
                                     : kLeakSlope / (1.0 + std::exp(-kAlpha * kKneeVolts));
        }

        pulse_.prepare(fs_);
        bt_.prepare(fs_);
        glide_ = 1.0 - dsp::expDecayCoeff(kGlideSeconds, fs_);
        active_ = false;  // so the parameters below take effect at once
        applyParams();
        reset();
    }

    void reset() noexcept {
        pulse_.reset();
        bt_.reset();
        tone_.reset();
        qC39_ = 0.0;
        attackOn_ = false;
        attackTime_ = 0.0;
        rho_ = rhoTarget_;
        snapTone();
        active_ = false;
    }

    void setParams(const VoiceParams& params) noexcept {
        params_ = dsp::saneParams(params, kVoiceInfo[BD].defaults);
        applyParams();
    }

    void trigger(const Hit& hit) noexcept {
        if (!(hit.volts > 0.0f)) return;
        hitSemis_ = static_cast<double>(hit.pitchSemis);
        if (!(std::abs(hitSemis_) <= 1.0e6)) hitSemis_ = 0.0;
        updatePitchTarget();
        if (!active_) rho_ = rhoTarget_;
        pulse_.setNetwork(shaperTau_ / rhoTarget_, shaperDc_);
        const double volts = dsp::triggerTransfer(static_cast<double>(hit.volts), kAccentGamma);
        pulse_.fire(volts, dsp::kTriggerWidthSeconds / rhoTarget_);
        // The envelope generator needs about 2.4 V of trigger to fire at all
        // (Q41's base divider). Below the normal 4 V it is faded in smoothly
        // instead, so weak hits and low velocities stay monotonic.
        const double ratio = volts / kEnvelopeKneeVolts;
        envelopeDepth_ = 1.0 - std::exp(-ratio * ratio);
        attackOn_ = true;
        attackTime_ = 0.0;
        attackStep_ = T_ * rhoTarget_;
        kCharge_ = 1.0 - std::exp(-attackStep_ / chargeTau_);
        kLeak_ = 1.0 - std::exp(-attackStep_ / leakTau_);
        active_ = true;
    }

    void render(float* out, int n) noexcept {
        for (int i = 0; i < n; ++i) out[i] = active_ ? tick() : 0.0f;
    }

    bool isActive() const noexcept { return active_; }

private:
    static constexpr std::uint32_t kComponentBase = 0x0100;
    static constexpr float kCapTol = 0.20f;
    static constexpr float kResTol = 0.05f;

    // Q43's leakage, in the softplus form of Werner's fit (DAFx-14 eq. 8;
    // research/01 §1.7): i_C = -(slope/alpha) ln(1 + exp(-alpha (V - knee))).
    // Werner's SPICE-fitted constants are alpha 14.315 /V, knee -0.556 V, slope
    // 1.4765e-5 A/V; with those the model sighs about 1 semitone on a normal
    // hit, the shallow end of the published range. The values below keep his
    // knee but are fitted to the zero-crossing trajectories of the reference
    // recordings (63 Hz for the first cycle, 57 Hz at 22 ms, 49.6 Hz settled;
    // research/05 §2.2), which need about 3.7 times the slope.
    static constexpr double kAlpha = 10.65;  // 1/V
    static constexpr double kKneeVolts = -0.570;
    static constexpr double kLeakSlope = 5.40e-5;  // A/V

    static constexpr double kAttackSeconds = 6.76e-3;  // fitted (see header)
    static constexpr double kEnvFallTau = 0.05e-3;     // Q42 turn-on
    static constexpr double kRekickVolts = 7.97;       // fitted; at most the 15 V rail less two drops
    static constexpr double kRekickTauTrim = 1.237;    // fitted; C39 is a 20 % part
    static constexpr double kSwitchOffTau = 0.03e-3;  // Q43 leaving saturation
    static constexpr double kSwitchOnOhms = 50.0;
    static constexpr double kRailVolts = 13.5;
    static constexpr double kMaxLoopGain = 0.985;  // of the oscillation threshold
    static constexpr double kGlideSeconds = 2.0e-3;
    static constexpr double kToneTaperMid = 0.22;  // fitted (see header)
    static constexpr double kOutputGain = 0.497;   // fitted: R177/R176 divider + follower
    static constexpr double kAccentGamma = 0.808;  // fitted
    static constexpr double kEnvelopeKneeVolts = 1.8;
    static constexpr double kSleepVolts = 4.0e-6;
    // Leak table: the softplus is linear below -4 V and zero above +2.5 V.
    static constexpr int kLeakTableSize = 832;
    static constexpr double kLeakTableLow = -4.0, kLeakTableHigh = 2.5;
    static constexpr double kLeakTableScale = kLeakTableSize / (kLeakTableHigh - kLeakTableLow);

    double collectorCurrent(double vJunction) const noexcept {
        return -(kLeakSlope / kAlpha) * dsp::softplus(-kAlpha * (vJunction - kKneeVolts));
    }

    // (i_C(v) - i_C(0)) / v >= 0: what the leak adds to the junction conductance.
    double leakConductance(double v) const noexcept {
        if (v <= kLeakTableLow) return (kLeakSlope * (v - kKneeVolts) - iC0_) / v;  // past the knee: linear
        if (v >= kLeakTableHigh) return -iC0_ / v;                                  // Q43 fully off
        const double p = (v - kLeakTableLow) * kLeakTableScale;
        const auto i = static_cast<std::size_t>(p);
        const double a = leakTable_[i];
        return a + (leakTable_[i + 1] - a) * (p - static_cast<double>(i));
    }

    void updatePitchTarget() noexcept {
        const double semis =
            dsp::clamp(static_cast<double>(params_.tune) + hitSemis_, -48.0, 48.0);
        rhoTarget_ = dsp::semitonesToRatio(semis);
    }

    void applyParams() noexcept {
        updatePitchTarget();
        // DECAY: VR6 is a linear pot in parallel with R169.
        const double k = dsp::clamp(static_cast<double>(params_.decay), 0.0, 1.0);
        const double vr = vr6_ * k;
        gFb_ = (r169_ * vr / (r169_ + vr + 1.0e-3)) / r164_;
        const double regen = gFb_ / (r170_ * c42_);
        d_ = std::max(dPassive_ - regen, dPassive_ * (1.0 - kMaxLoopGain));
        // TONE: VR5 as a variable resistor, bright at the clockwise end.
        const double t = dsp::clamp(static_cast<double>(params_.tone), 0.0, 1.0);
        const double vr5 = vr5_ * dsp::audioTaper(1.0 - t, kToneTaperMid);
        const double rEq = r171_ + r172_ * vr5 / (r172_ + vr5);
        toneTargetHz_ = 1.0 / (dsp::kTwoPi * rEq * c45_);
        // A pot cannot jump; an automated TONE can. Glide while sounding.
        if (!active_) snapTone();
        sweep_ = 2.0 * dsp::clamp(static_cast<double>(params_.shape), 0.0, 1.0);
    }

    float tick() noexcept {
        rho_ += (rhoTarget_ - rho_) * glide_;
        if (toneHz_ != toneTargetHz_) {
            toneHz_ += (toneTargetHz_ - toneHz_) * glide_;
            if (std::abs(toneHz_ - toneTargetHz_) < 1.0e-4 * toneTargetHz_) toneHz_ = toneTargetHz_;
            tone_.setCutoff(toneHz_, fs_);
        }

        const double x = pulse_.tick(
            [](double v) noexcept { return v >= 0.0 ? v : 0.71 * (std::exp(v) - 1.0); });

        // Envelope generator Q41/Q42 and the switch Q43 across R165.
        double rShunt = r165_;  // R165 with Q43 across it
        double vEnv = 0.0;
        if (attackOn_) {
            attackTime_ += attackStep_;
            const double z = attackTime_ - attackSeconds_;
            const double high = kRekickVolts * envelopeDepth_;
            if (z < -12.0 * kEnvFallTau) {
                vEnv = high;
                rShunt = kSwitchOnOhms;
            } else {
                vEnv = z < 0.0 ? high * (1.0 - std::exp(z / kEnvFallTau)) : 0.0;
                const double rCe =
                    z < 0.0 ? kSwitchOnOhms : kSwitchOnOhms * std::exp(z / kSwitchOffTau);
                rShunt = r165_ * rCe / (r165_ + rCe);
                if (z > 25.0 * kSwitchOffTau) attackOn_ = false;
            }
        }

        // C39: charged through D52 while the envelope is high, then discharged
        // into the junction through R161 once it falls (the re-kick).
        const double vAcross = vEnv - qC39_;
        double vRekick = 0.0;
        if (vAcross > 0.0) {
            qC39_ += vAcross * kCharge_;
        } else {
            vRekick = vAcross;
            qC39_ += vAcross * kLeak_;
        }

        // Junction load: R165 + R166 with Q43's leakage (the sigh) as a secant
        // conductance at the junction voltage. The leak is evaluated at the
        // explicit prediction, then once more at the end of a trial step (one
        // fixed-point iteration of the implicit solve). The explicit estimate
        // alone lags the state by a fraction of a sample, enough at high
        // pitches (a +48 semitone hit at 22 kHz) for the leak to pump energy
        // into the ring and hold it in a never-ending oscillation.
        const double a = rho_ * a0_;
        double gBranch = gBranchOff_;
        if (attackOn_) gBranch += envelopeDepth_ * (1.0 / (rShunt + r166_) - gBranchOff_);
        const double gLinear = gFixed_ + gBranch;
        const double scale = rho_ * invC42_;
        const double drivePerG = scale * x;
        const double driveFixed = scale * (x * gFb_ / r170_ - vRekick / r161_);
        const auto step = bt_.step(a * scale * gLinear, d_);
        double gM = gLinear;
        if (sweep_ > 0.0) {
            const double leakGain = sweep_ * (1.0 - r166_ * gBranch);
            const double gGuess = gLinear + leakGain * leakConductance(x - bt_.predictW(a));
            const double wTrial = bt_.trialW(step, a, scale * gGuess, drivePerG * gGuess + driveFixed);
            gM = gLinear + leakGain * leakConductance(x - wTrial);
        }
        const double b = scale * gM;
        bt_.tick(step, a, b, drivePerG * gM + driveFixed);
        bt_.clampOutput(kRailVolts, x);

        const double y = tone_.lowpass(x + bt_.u());

        if (!pulse_.active() && !attackOn_ && std::abs(qC39_) < kSleepVolts &&
            std::abs(y) < kSleepVolts && bt_.quieterThan(a, b, kSleepVolts)) {
            sleep();
        }
        return static_cast<float>(y * (kOutputGain / static_cast<double>(kVoltsPerUnit)));
    }

    void sleep() noexcept {
        bt_.reset();
        tone_.reset();
        qC39_ = 0.0;
        rho_ = rhoTarget_;
        snapTone();
        active_ = false;
    }

    void snapTone() noexcept {
        toneHz_ = toneTargetHz_;
        tone_.setCutoff(toneHz_, fs_);
    }

    VoiceParams params_{kVoiceInfo[BD].defaults};
    double fs_ = 48000.0, T_ = 1.0 / 48000.0;

    // Components (with unit variance applied) and values derived from them.
    double c42_ = 15.0e-9, invC42_ = 1.0 / 15.0e-9;
    double r165_ = 47.0e3, r166_ = 6.8e3, r161_ = 1.0e6, r170_ = 470.0e3;
    double r169_ = 47.0e3, r164_ = 47.0e3, vr6_ = 500.0e3;
    double c45_ = 0.1e-6, r171_ = 220.0, r172_ = 10.0e3, vr5_ = 10.0e3;
    double a0_ = 0.0, dPassive_ = 0.0, gFixed_ = 0.0, gBranchOff_ = 0.0;
    double shaperTau_ = 67.0e-6, shaperDc_ = 0.045;
    double attackSeconds_ = kAttackSeconds;
    double chargeTau_ = 0.73e-3, leakTau_ = 28.0e-3;
    double iC0_ = 0.0;
    std::array<double, kLeakTableSize + 2> leakTable_{};

    // Parameters.
    double d_ = 0.0, gFb_ = 0.0, sweep_ = 1.0;
    double toneHz_ = 0.0, toneTargetHz_ = 0.0;
    double hitSemis_ = 0.0;
    double rhoTarget_ = 1.0, rho_ = 1.0, glide_ = 1.0;

    // State.
    dsp::TriggerPulse pulse_;
    dsp::BridgedT bt_;
    dsp::OnePole tone_;
    double qC39_ = 0.0;
    double attackTime_ = 0.0, attackStep_ = 0.0;
    double kCharge_ = 0.0, kLeak_ = 0.0;
    double envelopeDepth_ = 1.0;
    bool attackOn_ = false;
    bool active_ = false;
};

}  // namespace mw808::voices

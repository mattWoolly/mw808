// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Low/mid/hi tom and low/mid/hi conga: three identical circuits in the
// original, each switched between tom and conga (research/01 §3). One class,
// selected at prepare(). Simulated in volts:
//
//   trigger (1 ms, 4..14 V)
//     -> input network  C_in differentiator, clamp diode for the negative
//        spike, 22k into the op-amp's + input with R_g to ground (the conga
//        position adds a shunt across R_g, so congas are hit more gently).
//     -> bridged-T      C1, C2 (the tom position switches extra capacitance
//        in), bridge R2 820k, and from the junction to ground
//        R1 = R231 1k (+ the lower part of TUNING, 500 ohm B).
//     -> diode clamp    two germanium diodes (1S188FM) from the junction to the
//        top of the TUNING pot bypass R231 while the junction swings far
//        enough: R1 falls, the pitch rises, and it settles as the ring decays.
//        Louder hits bend further.
//     -> regeneration   the second op-amp half inverts the output (33k/33k) and
//        returns it into the junction through 820k. With equal bridge and
//        regeneration resistors the damping is (C1 + C2 - G C1) / (R2 C1 C2)
//        (research/01 §0.2): tau = 2 R2 C1 for G = 1, independent of TUNING.
//     -> toms only      "pink" noise (the bus low-passed at 273 Hz, Q 0.74)
//        through a transistor VCA whose envelope is the trigger rectified onto
//        C_env and discharged through 2.2M (86 / 73 ms), then a 22k band-pass.
//     -> mix amp        inverting, 100k inputs, 470k || LEVEL 100k || 1n
//        (1.93 kHz low-pass), after a coupling capacitor.
//
//          C1        C2 (tom / conga)   C_in   R_g (tom / conga)   pitch at centre
//   LT/LC  56n       59n / 12n          6.8n   4.7k / 1.14k        86.5 / 192 Hz
//   MT/MC  35.2n/27n 39n / 12n          2.7n   6.8k / 1.66k        134 / 276 Hz
//   HT/HC  27n       27.6n / 5.6n       2.7n   5.6k / 1.18k        182 / 404 Hz
//
// Derived from the schematic values: pitches and tuning range (R1 1.0..1.5k),
// excitation sizes (which reproduce the manual's pattern of levels), noise
// envelope and band, mixer filters. The diodes' zero-bias leakage puts the
// settled pitch about 2.5 % above the diode-less schematic figure in the
// table, which is where the reference unit sits too.
// Fitted: ONE inverter gain for all six voices, G = 0.96 (the 33k pair is a
// 5 % part), which turns the ideal decays into the measured ones: -20 dB in
// 204/128/98 ms (toms; manual 200/130/100) and 178/94/86 ms (congas; manual
// 180/100/80). The germanium diode law, the noise amount, the output gain and
// the accent exponent are fitted to reference recordings and to the manual's
// level table.
//
// Controls. `tune` = 0 is the centre of the TUNING pot; the hardware pot spans
// about -1.65 .. +2.05 semitones of the +-12 offered, and outside it R231 or the
// pot is extended (with the kick strength held). `decay` (stock 0.5) scales the
// ring time x0.25..x4 and never depends on tuning. `shape` is the tom noise
// amount: 0 none, 0.5 stock, 1 four times the stock level.

#pragma once

#include <algorithm>
#include <cmath>

#include "mw808/Voices.h"
#include "mw808/dsp/BridgedT.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Math.h"
#include "mw808/dsp/NoiseBusTap.h"
#include "mw808/dsp/TriggerPulse.h"

namespace mw808::voices {

class TomConga {
public:
    // `which`: LT, MT, HT, LC, MC or HC.
    void prepare(const VoiceSetup& rawSetup, VoiceId which) noexcept {
        const VoiceSetup setup = dsp::saneSetup(rawSetup);
        which_ = (which >= LT && which <= HC) ? which : LT;
        const int pair = (which_ - LT) % 3;  // 0 low, 1 mid, 2 hi
        isTom_ = which_ <= HT;
        const Circuit& c = kCircuits[pair];
        fs_ = setup.sampleRate;
        T_ = 1.0 / fs_;
        // The tom and conga of a pair share every part but the switched ones,
        // so they share component ids too.
        const std::uint32_t base = kComponentBase + 0x40U * static_cast<std::uint32_t>(pair);
        auto part = [&setup, base](std::uint32_t id, double nominal, float tolerance) {
            return nominal * static_cast<double>(dsp::componentScale(setup.unitSeed, base + id,
                                                                     tolerance, setup.variance));
        };
        double c1 = part(0, c.c1, kCapTol);
        double c2 = part(1, c.c2, kCapTol);
        if (isTom_) {
            if (c.c1Tom > 0.0) c1 += part(2, c.c1Tom, kCapTol);
            c2 += part(3, c.c2Tom, kCapTol);
        }
        const double r2 = part(4, 820.0e3, kResTol);
        rFb_ = part(5, 820.0e3, kResTol);
        inverterGain_ = kInverterGain * part(6, 1.0, kResTol) / part(7, 1.0, kResTol);
        r231_ = part(8, 1.0e3, kResTol);
        vrTune_ = part(9, 500.0, kResTol);
        a_ = 1.0 / (r2 * c1);
        invC2_ = 1.0 / c2;
        dStock_ = (c1 + c2 - inverterGain_ * c1 * r2 / rFb_) / (r2 * c1 * c2);

        // Input network.
        const double cIn = part(10, c.cIn, kCapTol);
        const double rSeries = part(11, 22.0e3, kResTol);
        double rG = part(12, c.rG, kResTol);
        if (!isTom_) {
            const double shunt = part(13, c.rGConga, kResTol);
            rG = rG * shunt / (rG + shunt);
        }
        pulse_.prepare(fs_);
        pulse_.setNetwork(cIn * (rSeries + rG), 0.0);
        inputGain_ = rG / (rSeries + rG);

        // Output chain: inverter, coupling capacitor, mix amp at full LEVEL.
        const double rIn = part(14, 100.0e3, kResTol);
        const double cCouple = part(15, c.cCouple, kCapTol);
        const double r470 = part(16, 470.0e3, kResTol);
        const double vrLevel = part(17, 100.0e3, kResTol);
        const double cMix = part(18, 1.0e-9, kCapTol);
        const double rF = r470 * vrLevel / (r470 + vrLevel);
        mixGain_ = rF / rIn;
        coupleHp_.setCutoff(1.0 / (dsp::kTwoPi * rIn * cCouple), fs_);
        mixLp_.setCutoff(1.0 / (dsp::kTwoPi * rF * cMix), fs_);

        // Tom noise path.
        const double cEnv = part(19, c.cEnv, kCapTol);
        const double r47 = part(20, 4.7e3, kResTol);
        const double r2M2 = part(21, 2.2e6, kResTol);
        noiseCharge_ = 1.0 - std::exp(-T_ / (r47 * cEnv));
        noiseTau_ = r2M2 * cEnv;
        const double r22 = part(22, 22.0e3, kResTol);
        noiseHp_.setCutoff(1.0 / (dsp::kTwoPi * r22 * part(23, c.cHp, kCapTol)), fs_);
        noiseLp_.setCutoff(1.0 / (dsp::kTwoPi * r22 * part(24, c.cLp, kCapTol)), fs_);
        pink_.setFreqQ(273.0, 0.74, fs_);
        noiseDensity_ = dsp::noiseDensityGain(fs_);

        bt_.prepare(fs_);
        noiseGate_.prepare(fs_);
        diodes_ = dsp::DiodePairKnee{};  // rebuilt exactly: prepare() must not depend on history
        applyParams();
        reset();
    }

    // Contract signature: prepares as the low tom unless prepared with a voice.
    void prepare(const VoiceSetup& setup) noexcept { prepare(setup, which_); }

    void reset() noexcept {
        pulse_.reset();
        noiseGate_.reset();
        bt_.reset();
        pink_.reset();
        noiseHp_.reset();
        noiseLp_.reset();
        coupleHp_.reset();
        mixLp_.reset();
        noiseEnv_ = noiseTarget_ = 0.0;
        clampFraction_ = clampFractionIdle_;
        active_ = false;
    }

    void setParams(const VoiceParams& params) noexcept {
        params_ = dsp::saneParams(params, kVoiceInfo[which_].defaults);
        applyParams();
    }

    void trigger(const Hit& hit) noexcept {
        if (!(hit.volts > 0.0f)) return;
        const double volts = dsp::triggerTransfer(static_cast<double>(hit.volts), kAccentGamma);
        pulse_.fire(volts);
        if (isTom_) {
            noiseTarget_ = kNoiseKneeVolts *
                           dsp::softplus((volts - kNoiseDropVolts) / kNoiseKneeVolts);
            noiseGate_.fire(1.0);
        }
        active_ = true;
    }

    // `noise`: the shared unit-RMS white noise bus at the output rate.
    void render(float* out, const float* noise, int n) noexcept {
        for (int i = 0; i < n; ++i) out[i] = active_ ? tick(static_cast<double>(noise[i])) : 0.0f;
    }

    bool isActive() const noexcept { return active_; }

private:
    static constexpr std::uint32_t kComponentBase = 0x0300;
    static constexpr float kCapTol = 0.20f;
    static constexpr float kResTol = 0.05f;

    struct Circuit {
        double c1, c1Tom;      // inverting-input side; extra in the tom position
        double c2, c2Tom;      // output side; extra in the tom position
        double cIn, rG, rGConga;
        double cCouple;
        double cEnv, cHp, cLp;  // tom noise envelope and band-pass
    };
    static constexpr Circuit kCircuits[3] = {
        {56.0e-9, 0.0, 12.0e-9, 47.0e-9, 6.8e-9, 4.7e3, 1.5e3, 47.0e-9, 39.0e-9, 47.0e-9, 18.0e-9},
        {27.0e-9, 8.2e-9, 12.0e-9, 27.0e-9, 2.7e-9, 6.8e3, 2.2e3, 39.0e-9, 33.0e-9, 33.0e-9, 15.0e-9},
        {27.0e-9, 0.0, 5.6e-9, 22.0e-9, 2.7e-9, 5.6e3, 1.5e3, 33.0e-9, 33.0e-9, 27.0e-9, 10.0e-9},
    };

    static constexpr double kInverterGain = 0.96;     // fitted, one value for all six
    static constexpr double kMinDamping = 1.7;        // 1/s: T60 <= 8 s on any unit, any knob
    static constexpr double kRailVolts = 13.5;
    static constexpr double kInputDiodeVolts = 0.6;
    // Germanium point-contact pair (fitted to the reference pitch drops).
    static constexpr double kDiodeSlopeVolts = 0.030;
    static constexpr double kDiodeSaturationAmps = 1.0e-6;
    static constexpr double kDiodeSeriesOhms = 300.0;
    static constexpr double kDiodeTableTolerance = 0.02;
    static constexpr double kNoiseDropVolts = 1.2;  // rectifier diode + the VCA's base-emitter drop
    static constexpr double kNoiseKneeVolts = 0.08;
    static constexpr double kNoiseGain = 0.5;       // fitted to reference recordings
    static constexpr double kOutputGain = 1.033;     // fitted
    static constexpr double kAccentGamma = 0.830;    // fitted
    static constexpr double kSleepVolts = 4.0e-6;

    void applyParams() noexcept {
        const double semis = dsp::clamp(static_cast<double>(params_.tune), -24.0, 24.0);
        const double rho = dsp::semitonesToRatio(semis);
        // TUNING: find the pot position whose small-signal junction resistance
        // gives this pitch. The junction sees (R231 || diode branch) + the
        // pot's lower part, and the germanium pair leaks even at zero bias, so
        // the branch counts: rd0 = (pot's upper part + series + n Vt / Is) / 2.
        const double leak = kDiodeSeriesOhms + kDiodeSlopeVolts / kDiodeSaturationAmps;
        const auto branch = [leak](double rAbove) { return 0.5 * (rAbove + leak); };
        const auto parallel = [](double a, double b) { return a * b / (a + b); };
        const double r1Centre = parallel(r231_, branch(0.5 * vrTune_)) + 0.5 * vrTune_;
        const double r1Min = parallel(r231_, branch(vrTune_));        // fully clockwise
        const double r1Max = parallel(r231_, branch(0.0)) + vrTune_;  // fully anticlockwise
        const double r1 = r1Centre / (rho * rho);
        double rAbove;
        if (r1 <= r1Min) {
            // Past the clockwise end: shrink R231.
            rAbove = vrTune_;
            rBelow_ = 0.0;
            rBypassed_ = 1.0 / (1.0 / r1 - 1.0 / branch(vrTune_));
        } else if (r1 >= r1Max) {
            // Past the anticlockwise end: extend the pot.
            rAbove = 0.0;
            rBypassed_ = r231_;
            rBelow_ = r1 - parallel(r231_, branch(0.0));
        } else {
            double rb = r1 - r231_;
            for (int i = 0; i < 6; ++i)
                rb = dsp::clamp(r1 - parallel(r231_, branch(vrTune_ - rb)), 0.0, vrTune_);
            rBelow_ = rb;
            rAbove = vrTune_ - rb;
            rBypassed_ = r231_;
        }
        // The table's series resistance only shapes the large-signal bend (the
        // small-signal pitch above is exact), so it may lag a moving TUNING by
        // up to 2 %; rebuilding it on every automated update would triple the
        // voice's cost.
        diodes_.set(kDiodeSlopeVolts, kDiodeSaturationAmps, rAbove + kDiodeSeriesOhms, kDiodeTableTolerance);
        // Outside the hardware travel the kick strength is held at the end value.
        kickComp_ = r1 / dsp::clamp(r1, r1Min, r1Max);
        clampFractionIdle_ = rBypassed_ / (rBypassed_ + rBelow_);
        if (!active_) clampFraction_ = clampFractionIdle_;

        decayMult_ = dsp::decayKnobToMultiplier(static_cast<double>(params_.decay));
        d_ = std::max(dStock_ / decayMult_, kMinDamping);
        noiseDecay_ = dsp::expDecayCoeff(noiseTau_ * decayMult_, fs_);
        const double amount = 2.0 * dsp::clamp(static_cast<double>(params_.shape), 0.0, 1.0);
        noiseAmount_ = isTom_ ? kNoiseGain * amount * amount : 0.0;
    }

    float tick(double white) noexcept {
        const double x = inputGain_ * pulse_.tick([](double v) noexcept {
            return dsp::clampNegative(v, kInputDiodeVolts);
        });

        // Junction network: (R231 || (pot top + diodes)) + pot bottom. The
        // diodes see the part of the junction voltage that falls across R231.
        const double vM = x - bt_.predictW(a_);
        const double gDiodes = diodes_.secantConductance(vM * clampFraction_);
        const double rPar = 1.0 / (1.0 / rBypassed_ + gDiodes);
        const double r1 = rPar + rBelow_;
        clampFraction_ = rPar / r1;
        const double gM = 1.0 / r1 + 1.0 / rFb_;
        const double b = gM * invC2_;
        bt_.tick(a_, b, d_, x * kickComp_ * (gM + inverterGain_ / rFb_) * invC2_);
        bt_.clampOutput(kRailVolts, x);
        double mix = inverterGain_ * (x + bt_.u());

        bool noiseOn = false;
        if (isTom_) {
            if (noiseGate_.active()) {
                const double cover = noiseGate_.tick();
                if (noiseTarget_ > noiseEnv_)
                    noiseEnv_ += (noiseTarget_ - noiseEnv_) * noiseCharge_ * cover;
            }
            noiseOn = noiseEnv_ > 1.0e-6;
            if (noiseOn) {
                noiseEnv_ *= noiseDecay_;
                pink_.tick(white * noiseDensity_);
                const double vca = static_cast<double>(dsp::softClip(
                    static_cast<float>(noiseEnv_ * noiseAmount_ * pink_.lowpass())));
                mix += noiseLp_.lowpass(noiseHp_.highpass(vca));
            } else {
                noiseEnv_ = 0.0;
                mix += noiseLp_.lowpass(noiseHp_.highpass(0.0));
            }
        }

        // The coupling capacitor can swing the mix amp's input to twice a rail
        // when a stuck-at-the-rail ring reverses; the mix amp itself clips.
        const double y = dsp::clamp(mixLp_.lowpass(coupleHp_.highpass(mixGain_ * mix)), -kRailVolts, kRailVolts);

        if (!noiseOn && !pulse_.active() && !noiseGate_.active() && std::abs(y) < kSleepVolts &&
            std::abs(mix) < kSleepVolts && bt_.quieterThan(a_, b, kSleepVolts)) {
            sleep();
        }
        return static_cast<float>(y * (kOutputGain / static_cast<double>(kVoltsPerUnit)));
    }

    void sleep() noexcept {
        bt_.reset();
        pink_.reset();
        noiseHp_.reset();
        noiseLp_.reset();
        coupleHp_.reset();
        mixLp_.reset();
        noiseEnv_ = 0.0;
        clampFraction_ = clampFractionIdle_;
        active_ = false;
    }

    VoiceParams params_{kVoiceInfo[LT].defaults};
    VoiceId which_ = LT;
    bool isTom_ = true;
    double fs_ = 48000.0, T_ = 1.0 / 48000.0;

    // Components and derived values.
    double a_ = 0.0, invC2_ = 0.0, dStock_ = 0.0;
    double rFb_ = 820.0e3, inverterGain_ = kInverterGain;
    double r231_ = 1.0e3, vrTune_ = 500.0;
    double inputGain_ = 0.176, mixGain_ = 0.825;
    double noiseCharge_ = 1.0, noiseTau_ = 86.0e-3, noiseDensity_ = 1.0;

    // Parameters.
    double rBypassed_ = 1.0e3, rBelow_ = 250.0, kickComp_ = 1.0, clampFractionIdle_ = 0.8;
    double d_ = 0.0, decayMult_ = 1.0, noiseDecay_ = 0.0, noiseAmount_ = 0.0;
    dsp::DiodePairKnee diodes_;

    // State.
    dsp::TriggerPulse pulse_;
    dsp::TriggerGate noiseGate_;
    dsp::BridgedT bt_;
    dsp::Svf pink_;
    dsp::OnePole noiseHp_, noiseLp_, coupleHp_, mixLp_;
    double noiseEnv_ = 0.0, noiseTarget_ = 0.0;
    double clampFraction_ = 0.8;
    bool active_ = false;
};

}  // namespace mw808::voices

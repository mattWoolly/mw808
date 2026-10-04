// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Snare drum. A block model of the original circuit (research/01 §2),
// simulated in volts:
//
//   trigger (1 ms, 4..14 V)
//     -> LOW resonator   bridged-T, C58 = C59 27n, R196 680, R197 820k
//                        (schematic 249.6 Hz; trimmed to the manual's 238 Hz).
//        fed by C57 6.8n || R189 100k into R190 680: a differentiator (4.6 us),
//        so it sees an impulse on each edge of the pulse plus 0.7 % of its top.
//     -> HIGH resonator  bridged-T, C60 = C61 6.8n, R195 2.2k, R198 1M
//                        (schematic 499 Hz; trimmed to the manual's 476 Hz).
//        fed by the R191 8.2k / R192 220 divider: the 1 ms rectangle itself,
//        whose two edges are half a period apart and reinforce each other.
//        Full accent drives this op-amp into its supply rails.
//     -> TONE            VR8 100k(B) crossfades the two (the high one through
//                        R200 47k) into R204 100k and the mixer.
//     -> noise           "snappy" envelope: the pulse, tapped by VR9 10k(B) over
//        R199 1k, charges C51 0.47u through follower Q47; it discharges through
//        R184 + R185 (30k) while the swing VCA Q48, switched by the white noise
//        on its base, conducts (half the time: tau 28 ms). The envelope must
//        clear two junction drops, so SNAPPY and accent set the noise AMOUNT,
//        not its decay. The chopped envelope then passes the Sallen-Key
//        high-pass C66 = C67 1.8n, R201 22k, R202 47k (2.75 kHz, Q 0.73) and
//        C69 10n into R205 27k (589 Hz).
//     -> mixer IC13a     inverting; tone through R204 100k, noise through R205
//        27k, feedback R210 470k || LEVEL 100k || C70 220p (8.8 kHz).
//
// Neither resonator has a regeneration stage, so both decays are the passive
// values: 53 ms (low) and 16 ms (high) to -20 dB.
//
// Derived from the schematic values: decays, excitation shapes and sizes, the
// TONE network, the noise filters, the mixer gains, the noise envelope law.
// Trimmed: both resonator capacitors +4.9 % (inside their 20 % tolerance) to
// land on the service-manual pitches. Fitted to reference recordings: the
// noise level relative to the tone (kVcaSwing), a 7 kHz roll-off of the noise
// switch, and the ceiling the noise envelope runs into at high SNAPPY and
// accent. Fitted to the service-manual
// levels (3 Vpp normal, 10 Vpp full accent): the overall output gain and the
// accent transfer exponent.
//
// Non-hardware controls: `tune` scales the resonator capacitors and the pulse
// width they see; `decay` (stock 0.5) scales both resonator decays and the
// noise envelope, x0.25 .. x4.

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

class SnareDrum {
public:
    void prepare(const VoiceSetup& rawSetup) noexcept {
        const VoiceSetup setup = dsp::saneSetup(rawSetup);
        fs_ = setup.sampleRate;
        T_ = 1.0 / fs_;
        auto part = [&setup](std::uint32_t id, double nominal, float tolerance) {
            return nominal * static_cast<double>(dsp::componentScale(
                                 setup.unitSeed, kComponentBase + id, tolerance, setup.variance));
        };
        // Low resonator.
        const double c58 = part(0, 27.0e-9 * kCapTrim, kCapTol);
        const double c59 = part(1, 27.0e-9 * kCapTrim, kCapTol);
        const double r196 = part(2, 680.0, kResTol);
        const double r197 = part(3, 820.0e3, kResTol);
        low_.a = 1.0 / (r197 * c58);
        low_.b = 1.0 / (r196 * c59);
        low_.d = (c58 + c59) / (r197 * c58 * c59);
        // High resonator.
        const double c60 = part(4, 6.8e-9 * kCapTrim, kCapTol);
        const double c61 = part(5, 6.8e-9 * kCapTrim, kCapTol);
        const double r195 = part(6, 2.2e3, kResTol);
        const double r198 = part(7, 1.0e6, kResTol);
        high_.a = 1.0 / (r198 * c60);
        high_.b = 1.0 / (r195 * c61);
        high_.d = (c60 + c61) / (r198 * c60 * c61);
        // Excitation networks.
        const double c57 = part(8, 6.8e-9, kCapTol);
        const double r189 = part(9, 100.0e3, kResTol);
        const double r190 = part(10, 680.0, kResTol);
        lowTau_ = c57 * r189 * r190 / (r189 + r190);
        lowDc_ = r190 / (r189 + r190);
        const double r191 = part(11, 8.2e3, kResTol);
        const double r192 = part(12, 220.0, kResTol);
        highDivider_ = r192 / (r191 + r192);
        // TONE network.
        vr8_ = part(13, 100.0e3, kResTol);
        r200_ = part(14, 47.0e3, kResTol);
        r204_ = part(15, 100.0e3, kResTol);
        // Snappy envelope.
        vr9_ = part(16, 10.0e3, kResTol);
        r199_ = part(17, 1.0e3, kResTol);
        const double c51 = part(18, 0.47e-6, kCapTol);
        const double r184 = part(19, 15.0e3, kResTol);
        const double r185 = part(20, 15.0e3, kResTol);
        const double c52 = part(21, 0.1e-6, kCapTol);
        noiseTau_ = c51 * (r184 + r185) / kVcaDuty;
        envCharge_ = 1.0 - std::exp(-T_ / kEnvChargeSeconds);
        envSmooth_ = 1.0 - std::exp(-T_ / (c52 * r184 * r185 / (r184 + r185)));
        // Noise filters.
        const double c66 = part(22, 1.8e-9, kCapTol);
        const double c67 = part(23, 1.8e-9, kCapTol);
        const double r201 = part(24, 22.0e3, kResTol);
        const double r202 = part(25, 47.0e3, kResTol);
        const double hpFreq = 1.0 / (dsp::kTwoPi * std::sqrt(c66 * c67 * r201 * r202));
        const double hpQ = std::sqrt(c66 * c67 * r202 / r201) / (c66 + c67);
        noiseHp_.setFreqQ(hpFreq, hpQ, fs_);
        const double c69 = part(26, 10.0e-9, kCapTol);
        const double r205 = part(27, 27.0e3, kResTol);
        noiseHp1_.setCutoff(1.0 / (dsp::kTwoPi * r205 * c69), fs_);
        vcaLp_.setCutoff(kVcaLowpassHz, fs_);
        // Mixer, LEVEL at maximum.
        const double r210 = part(28, 470.0e3, kResTol);
        const double vr7 = part(29, 100.0e3, kResTol);
        const double c70 = part(30, 220.0e-12, kCapTol);
        const double rF = r210 * vr7 / (r210 + vr7);
        toneGain_ = rF / r204_;
        noiseGain_ = rF / r205;
        outLp_.setCutoff(1.0 / (dsp::kTwoPi * rF * c70), fs_);

        lowPulse_.prepare(fs_);
        highGate_.prepare(fs_);
        snappyGate_.prepare(fs_);
        low_.bt.prepare(fs_);
        high_.bt.prepare(fs_);
        noiseTap_.prepare(fs_);
        mixSmooth_ = 1.0 - std::exp(-T_ / kMixSmoothSeconds);
        applyParams();
        reset();
    }

    void reset() noexcept {
        lowPulse_.reset();
        highGate_.reset();
        snappyGate_.reset();
        low_.bt.reset();
        high_.bt.reset();
        noiseTap_.reset();
        noiseHp_.reset();
        noiseHp1_.reset();
        vcaLp_.reset();
        outLp_.reset();
        env_ = envOut_ = envTarget_ = 0.0;
        mixLow_ = mixLowTarget_;
        mixHigh_ = mixHighTarget_;
        active_ = false;
    }

    void setParams(const VoiceParams& params) noexcept {
        params_ = dsp::saneParams(params, kVoiceInfo[SD].defaults);
        applyParams();
    }

    void trigger(const Hit& hit) noexcept {
        if (!(hit.volts > 0.0f)) return;
        const double volts = dsp::triggerTransfer(static_cast<double>(hit.volts), kAccentGamma);
        const double width = dsp::kTriggerWidthSeconds / rho_;
        lowPulse_.setNetwork(lowTau_ / rho_, lowDc_);
        lowPulse_.fire(volts, width);
        highGate_.fire(volts * highDivider_, width);
        // SNAPPY: the wiper voltage, less Q47's and the VCA's junction drops.
        const double wiper = volts * (r199_ + snappy_ * vr9_) / (r199_ + vr9_);
        const double env = kEnvKneeVolts * dsp::softplus((wiper - kEnvDropVolts) / kEnvKneeVolts);
        envTarget_ = kEnvCeilingVolts * std::tanh(env / kEnvCeilingVolts);
        snappyGate_.fire(1.0);
        active_ = true;
    }

    // `noise`: the shared unit-RMS white noise bus at the output rate.
    void render(float* out, const float* noise, int n) noexcept {
        for (int i = 0; i < n; ++i) out[i] = active_ ? tick(static_cast<double>(noise[i])) : 0.0f;
    }

    bool isActive() const noexcept { return active_; }

private:
    static constexpr std::uint32_t kComponentBase = 0x0200;
    static constexpr float kCapTol = 0.20f;
    static constexpr float kResTol = 0.05f;

    static constexpr double kCapTrim = 1.0487;         // 249.6 Hz -> 238 Hz
    static constexpr double kRailVolts = 13.5;
    static constexpr double kEnvChargeSeconds = 0.16e-3;  // R186 C51 / beta
    static constexpr double kEnvDropVolts = 1.25;         // Q47 V_BE + D54
    static constexpr double kEnvKneeVolts = 0.06;
    static constexpr double kEnvCeilingVolts = 4.0;  // fitted: VCA headroom
    static constexpr double kVcaSwing = 0.75;        // fitted: collector swing / envelope
    static constexpr double kVcaLowpassHz = 7000.0;  // fitted: the switch is not infinitely fast
    static constexpr double kVcaDuty = 0.5;       // fraction of time Q48 conducts
    static constexpr double kSwitchHardness = 2.5;  // 130 mV rms noise over 2 Vt
    static constexpr double kOutputGain = 0.853;   // fitted
    static constexpr double kAccentGamma = 0.893;  // fitted
    static constexpr double kSleepVolts = 4.0e-6;
    static constexpr double kMixSmoothSeconds = 2.0e-3;  // TONE glide

    struct Resonator {
        dsp::BridgedT bt;
        double a = 0.0, b = 0.0, d = 0.0;
    };

    void applyParams() noexcept {
        rho_ = dsp::semitonesToRatio(dsp::clamp(static_cast<double>(params_.tune), -24.0, 24.0));
        decayMult_ = dsp::decayKnobToMultiplier(static_cast<double>(params_.decay));
        noiseDecay_ = dsp::expDecayCoeff(noiseTau_ * decayMult_, fs_);
        snappy_ = dsp::clamp(static_cast<double>(params_.shape), 0.0, 1.0);
        // TONE: wiper position t from the low-resonator end of VR8.
        const double t = dsp::clamp(static_cast<double>(params_.tone), 0.0, 1.0);
        const double zl = t * vr8_;
        const double zh = (1.0 - t) * vr8_ + r200_;
        const double den = zh * r204_ + zl * r204_ + zl * zh;
        mixLowTarget_ = zh * r204_ / den;
        mixHighTarget_ = zl * r204_ / den;
        // A pot cannot jump; an automated TONE can. Glide while sounding.
        if (!active_) {
            mixLow_ = mixLowTarget_;
            mixHigh_ = mixHighTarget_;
        }
    }

    float tick(double white) noexcept {
        const double xl = lowPulse_.tick([](double v) noexcept { return v; });
        const double xh = highGate_.tick();

        const double al = rho_ * low_.a, bl = rho_ * low_.b;
        low_.bt.tick(al, bl, low_.d / decayMult_, bl * xl);
        low_.bt.clampOutput(kRailVolts, xl);
        const double ah = rho_ * high_.a, bh = rho_ * high_.b;
        high_.bt.tick(ah, bh, high_.d / decayMult_, bh * xh);
        high_.bt.clampOutput(kRailVolts, xh);
        mixLow_ += (mixLowTarget_ - mixLow_) * mixSmooth_;
        mixHigh_ += (mixHighTarget_ - mixHigh_) * mixSmooth_;
        const double tone = mixLow_ * (xl + low_.bt.u()) + mixHigh_ * (xh + high_.bt.u());

        // Snappy envelope and the noise-switched VCA.
        double noise = 0.0;
        if (snappyGate_.active()) {
            const double cover = snappyGate_.tick();
            if (envTarget_ > env_) env_ += (envTarget_ - env_) * envCharge_ * cover;
        }
        const bool noiseOn = env_ > 1.0e-7 || envOut_ > 1.0e-7;
        if (noiseOn) {
            env_ *= noiseDecay_;
            envOut_ += (env_ - envOut_) * envSmooth_;
            const double n = noiseTap_.tick(white);
            const double conduct =
                0.5 + 0.5 * static_cast<double>(dsp::softClip(static_cast<float>(kSwitchHardness * n)));
            noiseHp_.tick(vcaLp_.lowpass(kVcaSwing * envOut_ * conduct));
        } else {
            env_ = envOut_ = 0.0;
            noiseHp_.tick(vcaLp_.lowpass(0.0));
        }
        noise = noiseHp1_.highpass(noiseHp_.highpass());

        // The mixer op-amp cannot swing past its supply rails (a fast accented
        // roll with SNAPPY up would otherwise reach about 15 V).
        const double y = dsp::clamp(outLp_.lowpass(-(toneGain_ * tone + noiseGain_ * noise)), -kRailVolts, kRailVolts);

        if (!noiseOn && !lowPulse_.active() && !highGate_.active() && !snappyGate_.active() &&
            std::abs(y) < kSleepVolts && noiseHp_.stateMagnitude() < kSleepVolts &&
            low_.bt.quieterThan(al, bl, kSleepVolts) && high_.bt.quieterThan(ah, bh, kSleepVolts)) {
            sleep();
        }
        return static_cast<float>(y * (kOutputGain / static_cast<double>(kVoltsPerUnit)));
    }

    void sleep() noexcept {
        low_.bt.reset();
        high_.bt.reset();
        noiseHp_.reset();
        noiseHp1_.reset();
        vcaLp_.reset();
        outLp_.reset();
        env_ = envOut_ = 0.0;
        mixLow_ = mixLowTarget_;
        mixHigh_ = mixHighTarget_;
        active_ = false;
    }

    VoiceParams params_{kVoiceInfo[SD].defaults};
    double fs_ = 48000.0, T_ = 1.0 / 48000.0;

    // Components and derived values.
    double lowTau_ = 4.6e-6, lowDc_ = 0.00675, highDivider_ = 0.0261;
    double vr8_ = 100.0e3, r200_ = 47.0e3, r204_ = 100.0e3;
    double vr9_ = 10.0e3, r199_ = 1.0e3;
    double noiseTau_ = 28.0e-3, envCharge_ = 1.0, envSmooth_ = 1.0;
    double toneGain_ = 0.825, noiseGain_ = 3.05;

    // Parameters.
    double rho_ = 1.0, decayMult_ = 1.0, noiseDecay_ = 0.0, snappy_ = 0.5;
    double mixLow_ = 0.5, mixHigh_ = 0.25, mixLowTarget_ = 0.5, mixHighTarget_ = 0.25;
    double mixSmooth_ = 1.0;

    // State.
    dsp::TriggerPulse lowPulse_;
    dsp::TriggerGate highGate_, snappyGate_;
    Resonator low_, high_;
    dsp::NoiseBusTap noiseTap_;
    dsp::Svf noiseHp_;
    dsp::OnePole noiseHp1_, vcaLp_, outLp_;
    double env_ = 0.0, envOut_ = 0.0, envTarget_ = 0.0;
    bool active_ = false;
};

}  // namespace mw808::voices

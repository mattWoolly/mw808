// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Rim shot: the rim shot / claves circuit in its rim-shot position
// (research/01 §4). Simulated in volts:
//
//   trigger (1 ms, 4..14 V)
//     -> LOW resonator   C115 = C116 4.7n, R315 5.6k, R316 1M: 452.5 Hz, Q 6.7,
//        fed through C111 10n, series diode D62, R301 47k / R302 10k: a long
//        (570 us) pulse that drives the op-amp hard.
//     -> HIGH resonator  C119 2.2n, C117 + C118 4.4n, R312 1k, R308 820k:
//        1786 Hz from the schematic, trimmed to the manual's 1667 Hz; Q 13.5,
//        fed through C114 1n, D64, R310 33k / R311 10k: a 43 us spike.
//     -> swing VCA Q62   both resonators drive the transistor's base. Its
//        collector is fed from the envelope capacitor C24 0.47u (charged by the
//        trigger through R107 1k) via R108 10k and D63. The transistor switches
//        hard, so the output is the envelope chopped by the sign of the mixed
//        resonator signal: a burst of square-ish pulses at the high pitch whose
//        duty is swung by the low one. C24 discharges through the conducting
//        transistor (R108 C24 = 4.7 ms over the duty), and the output stops
//        once the envelope falls to the diode drop.
//     -> high-pass       Sallen-Key C121 = C122 4.7n, R322 10k, R325 330k:
//        589 Hz, Q 2.87.
//     -> mixer IC19 (inverting) and the JFET mute Q74 (see Claves.h).
//
// The switching is anti-aliased with a first-order antiderivative (ADAA) of the
// transistor's conduction law, so the voice runs at the host rate.
//
// Derived: resonator pitches and decays, input networks, envelope charge and
// discharge constants, the high-pass. Fitted to a reference recording: the mix
// of the two resonators at the transistor's base, its bias and switching
// width, the envelope's diode drop and a 13 % trim of its time constant. Fitted to the service manual (3 Vpp normal, 10 Vpp full accent): the
// output gain and the accent exponent.
//
// Non-hardware controls: `tune` scales every capacitor in the tone path (both
// resonators and the high-pass); `decay` (stock 0.5) scales the envelope and
// both resonator decays x0.25..x4.

#pragma once

#include <algorithm>
#include <cmath>

#include "mw808/Voices.h"
#include "mw808/dsp/BridgedT.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Math.h"
#include "mw808/dsp/TriggerPulse.h"

namespace mw808::voices {

class RimShot {
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
        const double c116 = part(0, 4.7e-9, kCapTol);
        const double c115 = part(1, 4.7e-9, kCapTol);
        const double r315 = part(2, 5.6e3, kResTol);
        const double r316 = part(3, 1.0e6, kResTol);
        low_.a = 1.0 / (r316 * c116);
        low_.b = 1.0 / (r315 * c115);
        low_.d = (c116 + c115) / (r316 * c116 * c115);
        // High resonator (shares its parts with the claves: same ids).
        const std::uint32_t cl = 0x0A00 - kComponentBase;
        const double c119 = part(cl + 0, 2.2e-9 * kCapTrim, kCapTol);
        const double c117 = part(cl + 1, 2.2e-9 * kCapTrim, kCapTol);
        const double c118 = part(4, 2.2e-9 * kCapTrim, kCapTol);
        const double r312 = part(cl + 2, 1.0e3, kResTol);
        const double r308 = part(cl + 3, 820.0e3, kResTol);
        const double cOut = c117 + c118;
        high_.a = 1.0 / (r308 * c119);
        high_.b = 1.0 / (r312 * cOut);
        high_.d = (c119 + cOut) / (r308 * c119 * cOut);
        // Input networks.
        const double c111 = part(5, 10.0e-9, kCapTol);
        const double r301 = part(6, 47.0e3, kResTol);
        const double r302 = part(7, 10.0e3, kResTol);
        lowTau_ = c111 * (r301 + r302);
        lowGain_ = r302 / (r301 + r302);
        const double c114 = part(cl + 6, 1.0e-9, kCapTol);
        const double r310 = part(cl + 7, 33.0e3, kResTol);
        const double r311 = part(cl + 8, 10.0e3, kResTol);
        highTau_ = c114 * (r310 + r311);
        highGain_ = r311 / (r310 + r311);
        // VCA envelope.
        const double r107 = part(8, 1.0e3, kResTol);
        const double c24 = part(9, 0.47e-6, kCapTol);
        const double r108 = part(10, 10.0e3, kResTol);
        envCharge_ = 1.0 - std::exp(-T_ / (r107 * c24));
        envTau_ = kEnvTauTrim * r108 * c24;
        // High-pass.
        const double c121 = part(11, 4.7e-9, kCapTol);
        const double c122 = part(12, 4.7e-9, kCapTol);
        const double r322 = part(13, 10.0e3, kResTol);
        const double r325 = part(14, 330.0e3, kResTol);
        hpFreq_ = 1.0 / (dsp::kTwoPi * std::sqrt(c121 * c122 * r322 * r325));
        hpQ_ = std::sqrt(c121 * c122 * r325 / r322) / (c121 + c122);

        lowPulse_.prepare(fs_);
        highPulse_.prepare(fs_);
        envGate_.prepare(fs_);
        low_.bt.prepare(fs_);
        high_.bt.prepare(fs_);
        mute_.prepare(fs_);
        applyParams();
        reset();
    }

    void reset() noexcept {
        lowPulse_.reset();
        highPulse_.reset();
        envGate_.reset();
        low_.bt.reset();
        high_.bt.reset();
        hp_.reset();
        mute_.reset();
        env_ = envTarget_ = 0.0;
        prevBase_ = 0.0;
        prevIntegral_ = switchIntegral(0.0);
        active_ = false;
    }

    void setParams(const VoiceParams& params) noexcept {
        params_ = dsp::saneParams(params, kVoiceInfo[RS].defaults);
        applyParams();
    }

    void trigger(const Hit& hit) noexcept {
        if (!(hit.volts > 0.0f)) return;
        const double volts =
            dsp::triggerTransfer(static_cast<double>(hit.volts), kAccentGamma, kTriggerFloorVolts);
        const double width = dsp::kTriggerWidthSeconds / rho_;
        lowPulse_.setNetwork(lowTau_ / rho_, 0.0, false);
        lowPulse_.fire(volts, width);
        highPulse_.setNetwork(highTau_ / rho_, 0.0, false);
        highPulse_.fire(volts, width);
        envTarget_ = dsp::seriesDiode(volts, 0.6, kInputKneeVolts);  // through D61
        envGate_.fire(1.0);
        mute_.open(kMuteHoldSeconds * std::max(1.0, decayMult_));
        active_ = true;
    }

    void render(float* out, int n) noexcept {
        for (int i = 0; i < n; ++i) out[i] = active_ ? tick() : 0.0f;
    }

    bool isActive() const noexcept { return active_; }

private:
    static constexpr std::uint32_t kComponentBase = 0x0900;
    static constexpr float kCapTol = 0.20f;
    static constexpr float kResTol = 0.05f;

    static constexpr double kCapTrim = 1.0714;  // 1786 Hz -> 1667 Hz
    static constexpr double kRailVolts = 13.5;
    static constexpr double kInputKneeVolts = 0.15;  // soft, so weak triggers fade rather than vanish
    // Swing VCA (fitted, see header). The base signal is
    // kMixLow * V_low - kMixHigh * V_high (the high resonator arrives through
    // the inverter IC20b); the transistor conducts as a sigmoid of it.
    static constexpr double kMixLow = 0.056;
    static constexpr double kMixHigh = 0.230;
    static constexpr double kBiasVolts = 0.003;
    static constexpr double kSwitchVolts = 0.0408;
    static constexpr double kEnvDropVolts = 1.03;  // D63 + the transistor: two junctions
    static constexpr double kEnvKneeVolts = 0.05;
    static constexpr double kEnvTauTrim = 1.13;    // fitted; C24 is a 20 % part
    static constexpr double kEnvLeak = 0.1;        // discharge while not conducting
    static constexpr double kMuteHoldSeconds = 33.0e-3;
    static constexpr double kOutputGain = 0.996;  // fitted
    static constexpr double kAccentGamma = 0.688; // fitted
    static constexpr double kTriggerFloorVolts = 1.8;  // weak hits still clear the diode drops
    static constexpr double kSleepVolts = 4.0e-6;

    struct Resonator {
        dsp::BridgedT bt;
        double a = 0.0, b = 0.0, d = 0.0;
    };

    // Conduction of the transistor against its base voltage: a sigmoid,
    // 0.5 (1 + z / sqrt(1 + z^2)) with z = (base - bias) / width, and its
    // integral over the base voltage (for the anti-aliasing).
    static double switchLaw(double base) noexcept {
        const double z = (base - kBiasVolts) / kSwitchVolts;
        return 0.5 + 0.5 * z / std::sqrt(1.0 + z * z);
    }
    static double switchIntegral(double base) noexcept {
        const double z = (base - kBiasVolts) / kSwitchVolts;
        return 0.5 * kSwitchVolts * (z + std::sqrt(1.0 + z * z));
    }

    void applyParams() noexcept {
        rho_ = dsp::semitonesToRatio(dsp::clamp(static_cast<double>(params_.tune), -24.0, 24.0));
        decayMult_ = dsp::decayKnobToMultiplier(static_cast<double>(params_.decay));
        envDischarge_ = 1.0 - std::exp(-T_ / (envTau_ * decayMult_));
        hp_.setFreqQ(hpFreq_ * rho_, hpQ_, fs_);
    }

    float tick() noexcept {
        const auto diode = [](double v) noexcept { return dsp::seriesDiode(v, 0.6, kInputKneeVolts); };
        const double xl = lowGain_ * lowPulse_.tick(diode);
        const double xh = highGain_ * highPulse_.tick(diode);

        const double al = rho_ * low_.a, bl = rho_ * low_.b;
        low_.bt.tick(al, bl, low_.d / decayMult_, bl * xl);
        low_.bt.clampOutput(kRailVolts, xl);
        const double ah = rho_ * high_.a, bh = rho_ * high_.b;
        high_.bt.tick(ah, bh, high_.d / decayMult_, bh * xh);
        high_.bt.clampOutput(kRailVolts, xh);

        // Swing VCA: conduction averaged over the sample (ADAA).
        const double base = kMixLow * (xl + low_.bt.u()) - kMixHigh * (xh + high_.bt.u());
        const double integral = switchIntegral(base);
        const double step = base - prevBase_;
        double conduct;
        if (std::abs(step) > 1.0e-7) {
            conduct = dsp::clamp((integral - prevIntegral_) / step, 0.0, 1.0);
        } else {
            conduct = switchLaw(0.5 * (base + prevBase_));  // the ADAA limit, at the same half-sample delay
        }
        prevBase_ = base;
        prevIntegral_ = integral;

        if (envGate_.active()) {
            const double cover = envGate_.tick();
            if (envTarget_ > env_) env_ += (envTarget_ - env_) * envCharge_ * cover;
        }
        env_ -= env_ * envDischarge_ * (kEnvLeak + (1.0 - kEnvLeak) * conduct);
        const double swing = dsp::softKnee(env_ - kEnvDropVolts, kEnvKneeVolts) - swingZero_;

        hp_.tick(swing * (1.0 - conduct));
        const double y = -hp_.highpass() * mute_.tick();

        // Asleep once the mute has closed, or once nothing can reach the
        // output any more: the swing below the diode drop is negligible and the
        // resonant high-pass has stopped ringing (its output alone can sit on a
        // zero crossing while it still rings). The resonators only reach the
        // output through the swing, so a looser bound will do for them.
        if (!lowPulse_.active() && !highPulse_.active() && !envGate_.active() &&
            (mute_.closed() ||
             (std::abs(swing) < kSleepVolts && hp_.stateMagnitude() < kSleepVolts &&
              std::abs(y) < kSleepVolts && low_.bt.quieterThan(al, bl, 1.0e-4) &&
              high_.bt.quieterThan(ah, bh, 1.0e-4)))) {
            sleep();
        }
        return static_cast<float>(y * (kOutputGain / static_cast<double>(kVoltsPerUnit)));
    }

    void sleep() noexcept {
        low_.bt.reset();
        high_.bt.reset();
        hp_.reset();
        mute_.reset();
        env_ = 0.0;
        prevBase_ = 0.0;
        prevIntegral_ = switchIntegral(0.0);
        active_ = false;
    }

    VoiceParams params_{kVoiceInfo[RS].defaults};
    double fs_ = 48000.0, T_ = 1.0 / 48000.0;

    double lowTau_ = 570.0e-6, lowGain_ = 0.175, highTau_ = 43.0e-6, highGain_ = 0.23;
    double envCharge_ = 1.0, envTau_ = 4.7e-3, hpFreq_ = 589.0, hpQ_ = 2.87;
    double rho_ = 1.0, decayMult_ = 1.0, envDischarge_ = 0.0;
    double swingZero_ = dsp::softKnee(-kEnvDropVolts, kEnvKneeVolts);

    dsp::TriggerPulse lowPulse_, highPulse_;
    dsp::TriggerGate envGate_;
    Resonator low_, high_;
    dsp::Svf hp_;
    dsp::MuteGate mute_;
    double env_ = 0.0, envTarget_ = 0.0;
    double prevBase_ = 0.0, prevIntegral_ = 0.0;
    bool active_ = false;
};

}  // namespace mw808::voices

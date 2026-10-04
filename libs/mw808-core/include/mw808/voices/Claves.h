// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Claves: the rim shot / claves circuit in its claves position (research/01
// §4). A single high-Q bridged-T resonator, simulated in volts:
//
//   trigger (1 ms, 4..14 V)
//     -> C114 1n, series diode D64, R310 33k / R311 10k: a 43 us spike.
//     -> bridged-T  C119 = C117 2.2n, R312 1k (|| R313 390k), R308 820k:
//        2529 Hz from the schematic, trimmed to the manual's 2500 Hz.
//     -> regeneration through R313 from the inverter IC20b. With ideal parts
//        this loop is past the oscillation threshold (research/01 §4.2); the
//        real one decays in 25 ms because of the op-amp's finite bandwidth. The
//        loop is therefore NOT built from the part values here: the damping is
//        fitted to the service manual's 25 ms to -20 dB (Q about 85).
//     -> inverter IC20b -> R319 220k -> mixer IC19 (470k || LEVEL 100k).
//     -> JFET mute Q74: the mixer is open only for a while after a trigger. In
//        reference recordings it closes at about 33 ms, cutting the tail off at
//        roughly -28 dB; that is part of the sound, and is modelled.
//
// Derived: pitch, the mixer gain (R_f / R319). Fitted: the damping (above), the
// mute hold time (reference recording), the excitation gain and the accent
// exponent (service manual: 2.5 Vpp normal, 8 Vpp full accent).
//
// Non-hardware controls: `tune` scales the capacitors; `decay` (stock 0.5)
// scales the ring time x0.25..x4, and the mute hold with it when lengthened.

#pragma once

#include <algorithm>
#include <cmath>

#include "mw808/Voices.h"
#include "mw808/dsp/BridgedT.h"
#include "mw808/dsp/Math.h"
#include "mw808/dsp/TriggerPulse.h"

namespace mw808::voices {

class Claves {
public:
    void prepare(const VoiceSetup& rawSetup) noexcept {
        const VoiceSetup setup = dsp::saneSetup(rawSetup);
        fs_ = setup.sampleRate;
        auto part = [&setup](std::uint32_t id, double nominal, float tolerance) {
            return nominal * static_cast<double>(dsp::componentScale(
                                 setup.unitSeed, kComponentBase + id, tolerance, setup.variance));
        };
        const double c119 = part(0, 2.2e-9 * kCapTrim, kCapTol);
        const double c117 = part(1, 2.2e-9 * kCapTrim, kCapTol);
        const double r312 = part(2, 1.0e3, kResTol);
        const double r308 = part(3, 820.0e3, kResTol);
        const double r313 = part(4, 390.0e3, kResTol);
        a_ = 1.0 / (r308 * c119);
        b_ = (1.0 / r312 + 1.0 / r313) / c117;
        // The loop sits on the edge of oscillation, so its net damping is the
        // most tolerance-sensitive quantity in the voice.
        dStock_ = 2.0 * std::log(10.0) / kDecaySeconds * part(5, 1.0, kCapTol);

        const double c114 = part(6, 1.0e-9, kCapTol);
        const double r310 = part(7, 33.0e3, kResTol);
        const double r311 = part(8, 10.0e3, kResTol);
        inputTau_ = c114 * (r310 + r311);
        inputGain_ = kInputTrim * r311 / (r310 + r311);

        const double r319 = part(9, 220.0e3, kResTol);
        const double r330 = part(10, 470.0e3, kResTol);
        const double vr16 = part(11, 100.0e3, kResTol);
        mixGain_ = (r330 * vr16 / (r330 + vr16)) / r319;

        pulse_.prepare(fs_);
        bt_.prepare(fs_);
        mute_.prepare(fs_);
        applyParams();
        reset();
    }

    void reset() noexcept {
        pulse_.reset();
        bt_.reset();
        mute_.reset();
        active_ = false;
    }

    void setParams(const VoiceParams& params) noexcept {
        params_ = dsp::saneParams(params, kVoiceInfo[CL].defaults);
        applyParams();
    }

    void trigger(const Hit& hit) noexcept {
        if (!(hit.volts > 0.0f)) return;
        pulse_.setNetwork(inputTau_ / rho_, 0.0, false);
        pulse_.fire(dsp::triggerTransfer(static_cast<double>(hit.volts), kAccentGamma),
                    dsp::kTriggerWidthSeconds / rho_);
        mute_.open(kMuteHoldSeconds * std::max(1.0, decayMult_));
        active_ = true;
    }

    void render(float* out, int n) noexcept {
        for (int i = 0; i < n; ++i) out[i] = active_ ? tick() : 0.0f;
    }

    bool isActive() const noexcept { return active_; }

private:
    static constexpr std::uint32_t kComponentBase = 0x0A00;
    static constexpr float kCapTol = 0.20f;
    static constexpr float kResTol = 0.05f;

    static constexpr double kCapTrim = 1.0116;        // 2529 Hz -> 2500 Hz
    static constexpr double kDecaySeconds = 25.0e-3;  // to -20 dB (service manual)
    static constexpr double kMuteHoldSeconds = 33.0e-3;  // fitted (reference recording)
    static constexpr double kRailVolts = 13.5;
    static constexpr double kInputKneeVolts = 0.15;  // soft, so weak triggers fade rather than vanish
    static constexpr double kInputTrim = 0.356;   // fitted
    static constexpr double kAccentGamma = 0.720; // fitted
    static constexpr double kSleepVolts = 4.0e-6;

    void applyParams() noexcept {
        rho_ = dsp::semitonesToRatio(dsp::clamp(static_cast<double>(params_.tune), -24.0, 24.0));
        decayMult_ = dsp::decayKnobToMultiplier(static_cast<double>(params_.decay));
        d_ = dStock_ / decayMult_;
    }

    float tick() noexcept {
        const double x =
            inputGain_ * pulse_.tick([](double v) noexcept {
                return dsp::seriesDiode(v, 0.6, kInputKneeVolts);
            });
        const double a = rho_ * a_, b = rho_ * b_;
        bt_.tick(a, b, d_, b * x);
        bt_.clampOutput(kRailVolts, x);
        const double y = mixGain_ * (x + bt_.u()) * mute_.tick();
        if (!pulse_.active() && (mute_.closed() || bt_.quieterThan(a, b, kSleepVolts))) {
            bt_.reset();
            mute_.reset();
            active_ = false;
        }
        return static_cast<float>(y / static_cast<double>(kVoltsPerUnit));
    }

    VoiceParams params_{kVoiceInfo[CL].defaults};
    double fs_ = 48000.0;
    double a_ = 0.0, b_ = 0.0, dStock_ = 0.0;
    double inputTau_ = 43.0e-6, inputGain_ = 0.23, mixGain_ = 0.375;
    double rho_ = 1.0, decayMult_ = 1.0, d_ = 0.0;

    dsp::TriggerPulse pulse_;
    dsp::BridgedT bt_;
    dsp::MuteGate mute_;
    bool active_ = false;
};

}  // namespace mw808::voices

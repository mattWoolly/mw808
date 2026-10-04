// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Cowbell (CB). Circuit and values: research/02 §1, §4 (service manual p.6,
// p.13 and the published cowbell model, "[WCB]").
//
//   osc 540 Hz --> VCA Q15 --+
//                            +--> two-input band-pass (~875 Hz) --> level --> out
//   osc 800 Hz --> VCA Q14 --+        both VCAs fed from one envelope (C9)
//
// DERIVED from the schematic:
//  * Sources: only the two trimmed oscillators of the bank, straight into the
//    VCA bases (no band-pass in front).
//  * Swing VCAs with a two-level input: each collector is a square between an
//    upper edge (~ the envelope) and a lower edge that tracks the envelope.
//    Edges: [WCB] Table 1 with the signs as corrected in research/02 §4.3. A
//    two-level input makes the stage linear in the square wave, so the
//    band-limited oscillator is simply scaled; no extra aliasing.
//  * Envelope: C9 0.47u charged by the trigger; R82 33k + C34 1u across it
//    (the "abrupt initial drop": C9 dumps into C34, tau ~10 ms, to about
//    -10 dB), and the two collector resistors R28 = R29 100k, which only load
//    C9 while their transistor conducts (48 % of the time).
//  * Band-pass: R26/R27 10k + C30/C31 2.2n inputs, R24 2.2k, C29 3.3n, C28 10n,
//    R25 470k. Per input H(s) = -(1/(R26 C29)) s^2 / (cubic), solved at
//    prepare time into one real pole (~7.4 kHz) and a resonant pair: magnitude
//    peak 874 Hz, Q 4.6, +10 dB per input; shallow first-order upper skirt.
//
// FITTED:
//  * Trigger coupling: C9 only reaches about a third of the trigger height
//    (scale 0.33), with an expanding power law (1.38). Together they give the
//    service-manual levels, 3.5 Vpp normal and 12 Vpp accented (+10.7 dB, more
//    than the trigger's own +6 dB), and the depth of the initial drop seen in
//    the reference recording (-15 dB by ~40 ms). The knee is shallower on
//    accented hits, so those ring longer, as [WCB] Fig. 3 shows.
//  * A 500k load across C9. With only the schematic loads the tail is about
//    twice too long; with it the envelope follows the reference recording
//    (-20 dB at ~60 ms; service manual 50 ms, recording 78 ms).
//  * Output gain (the passive level network).
//
// Non-hardware: decay (0.5 = stock) scales both envelope capacitors; tune
// scales the oscillators and the band-pass.

#pragma once

#include <cstdint>

#include "mw808/Voices.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Halfband.h"
#include "mw808/dsp/MetalCommon.h"
#include "mw808/dsp/MetalOscBank.h"
#include "mw808/dsp/SwingVca.h"

namespace mw808::voices {

class Cowbell {
public:
    void prepare(const VoiceSetup& setup) noexcept {
        os_ = setup.oversample >= 4 ? 4 : (setup.oversample >= 2 ? 2 : 1);
        rate_ = setup.sampleRate * os_;
        dt_ = 1.0 / rate_;

        const std::uint32_t seed = setup.unitSeed;
        const float var = setup.variance;
        using dsp::componentScale;
        namespace mp = dsp::metalparts;
        c9_ = 0.47e-6 * componentScale(seed, mp::kCowbell + 0U, 0.20f, var);
        c34_ = 1.0e-6 * componentScale(seed, mp::kCowbell + 1U, 0.20f, var);
        // Band-pass parts: the capacitors (20 %) dominate the tuning.
        const double c28 = 10.0e-9 * componentScale(seed, mp::kCowbell + 2U, 0.20f, var);
        const double c29 = 3.3e-9 * componentScale(seed, mp::kCowbell + 3U, 0.20f, var);
        const double cIn = 2.2e-9 * componentScale(seed, mp::kCowbell + 4U, 0.20f, var);
        solveBandPass(cIn, c28, c29);

        bank_.prepare(rate_, seed, var);
        if (!tablesBuilt_) {
            // The published fit is only meaningful above the gate threshold
            // (below ~0.24 V it turns negative, then large): closed below it.
            swing_.build([](double v) {
                return v < kGateVolts ? 0.0 : dsp::swingUpperEdgeCowbell(v) - dsp::swingLowerEdgeCowbell(v);
            });
            tablesBuilt_ = true;
        }
        decimator_.prepare(os_);
        silence_.prepare(setup.sampleRate);
        ctrlN_ = std::max(1, static_cast<int>(rate_ / dsp::kMetalControlHz + 0.5));
        invCtrlN_ = 1.0 / ctrlN_;
        dtc_ = dt_ * ctrlN_;
        attack_ = 1.0 - std::exp(-dtc_ / kAttackTau);
        pulseTicks_ = std::max(1, static_cast<int>(kPulseSeconds / dtc_ + 0.5));

        haveParams_ = false;
        applyParams(params_);
        reset();
    }

    void reset() noexcept {
        bank_.reset();
        clearState();
        active_ = false;
    }

    void setParams(const VoiceParams& raw) noexcept {
        const VoiceParams p = dsp::saneParams(raw);
        if (haveParams_ && p.tune == params_.tune && p.decay == params_.decay) return;
        applyParams(p);
    }

    void trigger(const Hit& hit) noexcept {
        charge_ = kTrigScale * dsp::metalChargeVolts(dsp::saneVolts(hit.volts), kTrigDrop, kTrigPower);
        pulseLeft_ = pulseTicks_;
        ctrlCount_ = 0;
        silence_.restart();
        active_ = true;
    }

    void render(float* out, int n) noexcept {
        if (!active_) {
            bank_.advance(static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(os_));
            for (int i = 0; i < n; ++i) out[i] = 0.0f;
            return;
        }
        for (int i = 0; i < n; ++i) {
            double hi[dsp::MetalDecimator::kMaxFactor];
            for (int k = 0; k < os_; ++k) hi[k] = tick();
            const double y = decimator_.process(hi);
            out[i] = static_cast<float>(y);
            if (silence_.tick(y) && pulseLeft_ == 0) {
                bank_.advance(static_cast<std::uint64_t>(n - 1 - i) * static_cast<std::uint64_t>(os_));
                for (int j = i + 1; j < n; ++j) out[j] = 0.0f;
                clearState();
                active_ = false;
                return;
            }
        }
    }

    bool isActive() const noexcept { return active_; }

    double latencySamples() const noexcept { return decimator_.latencySamples(); }
    const dsp::MetalOscBank& oscillators() const noexcept { return bank_; }

    // ---- schematic values -------------------------------------------------
    static constexpr double kPulseSeconds = 1.0e-3;
    static constexpr double kR82 = 33.0e3, kR28 = 100.0e3;  // R29 = R28
    static constexpr double kR24 = 2.2e3, kR25 = 470.0e3, kR26 = 10.0e3;
    static constexpr double kGateVolts = 0.245;  // where the fitted edges cross
    // ---- fitted -----------------------------------------------------------
    static constexpr double kAttackTau = 1.0e-4;
    static constexpr double kTrigDrop = 1.3, kTrigPower = 1.38, kTrigScale = 0.33;
    static constexpr double kTailLoad = 500.0e3;  // extra load across C9, ohms
    static constexpr double kOutGain = 0.80;

private:
    // Factor a3 s^3 + a2 s^2 + a1 s + a0 (research/02 §4.4) into (s + p)(s^2 + (w0/Q) s + w0^2).
    void solveBandPass(double cIn, double c28, double c29) noexcept {
        const double a0 = 1.0 / kR24;
        const double a1 = 2.0 * cIn + kR26 * cIn / kR24 + (c28 + c29);
        const double a2 = kR26 * cIn * (c28 + c29) + c28 * c29 * kR25;
        const double a3 = kR26 * cIn * c28 * c29 * kR25;
        double s = -a2 / a3;  // the real root is close to this
        for (int i = 0; i < 20; ++i) {
            const double f = ((a3 * s + a2) * s + a1) * s + a0;
            const double df = (3.0 * a3 * s + 2.0 * a2) * s + a1;
            if (df == 0.0) break;
            s -= f / df;
        }
        const double p = -s;
        const double b = a2 / a3 - p;
        const double c = a0 / (a3 * p);
        poleHz_ = p / dsp::kTwoPi;
        centreHz_ = std::sqrt(c) / dsp::kTwoPi;
        q_ = std::sqrt(c) / b;
        bpGain_ = 1.0 / (kR26 * c29 * p);
    }

    void applyParams(const VoiceParams& p) noexcept {
        params_ = p;
        haveParams_ = true;
        const double ratio = dsp::semitonesToRatio(dsp::clamp(static_cast<double>(p.tune), -24.0, 24.0));
        bank_.setTune(ratio);
        bpHp_.setFreqQ(centreHz_ * ratio, q_, rate_);
        bpLp_.setCutoff(poleHz_ * ratio, rate_);
        const double scale = dsp::decayKnobScale(static_cast<double>(p.decay));
        k9_ = dtc_ / (c9_ * scale);
        k34_ = dtc_ / (c34_ * scale);
    }

    void clearState() noexcept {
        bpHp_.reset();
        bpLp_.reset();
        decimator_.reset();
        v9_ = v34_ = 0.0;
        upper_ = swing_now_ = dUpper_ = dSwing_ = 0.0;
        ctrlCount_ = 0;
        pulseLeft_ = 0;
        charge_ = 0.0;
        silence_.restart();
    }

    void control() noexcept {
        if (pulseLeft_ > 0) {
            --pulseLeft_;
            if (charge_ > v9_) v9_ += (charge_ - v9_) * attack_;
        }
        const double swing = swing_(v9_);
        // C9 into C34 through R82, and into the two collectors while they conduct.
        const double i82 = (v9_ - v34_) * (1.0 / kR82);
        const double iVca = 2.0 * dsp::MetalOscBank::kDuty * swing * (1.0 / kR28);
        v9_ -= k9_ * (i82 + iVca + v9_ * (1.0 / kTailLoad));
        v34_ += k34_ * i82;
        if (v9_ < 0.0) v9_ = 0.0;

        const double upperNext = std::max(0.0, dsp::swingUpperEdgeCowbell(v9_));
        const double swingNext = swing_(v9_);
        dUpper_ = (upperNext - upper_) * invCtrlN_;
        dSwing_ = (swingNext - swing_now_) * invCtrlN_;
        ctrlCount_ = ctrlN_;
    }

    double tick() noexcept {
        if (ctrlCount_ <= 0) control();
        --ctrlCount_;
        upper_ += dUpper_;
        swing_now_ += dSwing_;

        double lo = 0.0, hi = 0.0;
        bank_.tickCowbell(lo, hi);
        // Oscillator high -> transistor on -> collector at the lower edge.
        const double on = 1.0 + 0.5 * (lo + hi);  // sum of the two "on" fractions, 0..2
        const double collectors = 2.0 * upper_ - swing_now_ * on;
        bpHp_.tick(collectors);
        const double y = bpLp_.lowpass(bpHp_.highpass());
        return y * (-bpGain_ * kOutGain / kVoltsPerUnit);
    }

    // configuration
    int os_ = 1;
    double rate_ = 48000.0, dt_ = 1.0 / 48000.0, dtc_ = 1.0 / 12000.0, invCtrlN_ = 0.25;
    int ctrlN_ = 4;
    VoiceParams params_{};
    bool haveParams_ = false;
    bool tablesBuilt_ = false;
    double c9_ = 0.47e-6, c34_ = 1.0e-6;
    double poleHz_ = 7370.0, centreHz_ = 875.0, q_ = 4.6, bpGain_ = 0.66;
    double attack_ = 1.0, k9_ = 0.0, k34_ = 0.0;
    int pulseTicks_ = 12;

    // blocks
    dsp::MetalOscBank bank_;
    dsp::SwingRangeTable swing_;
    dsp::Svf bpHp_;
    dsp::OnePole bpLp_;
    dsp::MetalDecimator decimator_;
    dsp::SilenceDetector silence_;

    // state
    double v9_ = 0.0, v34_ = 0.0;
    double upper_ = 0.0, swing_now_ = 0.0, dUpper_ = 0.0, dSwing_ = 0.0;
    double charge_ = 0.0;
    int ctrlCount_ = 0;
    int pulseLeft_ = 0;
    bool active_ = false;
};

}  // namespace mw808::voices

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Maracas (MA). Circuit and values: research/02 §6 (service manual p.6, p.9).
//
//   noise --> gate Q65 <-- envelope (Q66, Q67) --> high-pass 10.6 kHz --> out
//
// DERIVED from the schematic:
//  * Envelope: the trigger charges C135 0.1u (R345 150k across it: 15 ms).
//    While that voltage holds Q67 on, C134 33n charges through R341 470k
//    (15.5 ms) and the gate opens gradually: the maracas swells in. When Q67
//    lets go, C134 is pulled back through D66 and R343 100k (3.3 ms) and the
//    gate shuts fast. A harder hit holds Q67 on for longer, so accent makes
//    the maracas longer as well as louder: the service manual's 25 ms
//    ("short") and 35 ms ("long") to -20 dB are taken as normal and accented.
//  * High-pass: C131 10n into R338 15k (1.06 kHz, first order), then the
//    Sallen-Key C132 = C133 1n, R339 3.3k, R340 68k: 10.62 kHz, Q 2.27 with an
//    ideal source. Output stage with LEVEL at maximum: pole at 8.8 kHz.
//
// FITTED:
//  * High-pass Q 1.0. The gate node drives the filter through R337 10k, which
//    damps the resonance (research/02 §6.2: almost no peak); Q 1.0 reproduces
//    the reference recording's slope below 10 kHz, Q 2.27 does not.
//  * Hold time versus trigger height (20.5 ms at 4 V, 30.5 ms at 14 V), the
//    gate's turn-on offset, and the levels (3 Vpp normal, 5 Vpp accented).
//
// Noise colour: the engine's noise bus is white; this voice applies the later
// units' 7.23 kHz roll-off (dsp::kLateNoiseRolloffHz) to its own input, as
// the reference unit has it (centroid 11.1 kHz, recording 10.8 kHz).
//
// Knobs (none on the hardware): decay stretches the whole envelope
// (x0.25..x4, 0.5 = stock); tune moves the high-pass. Runs at 1x; the
// high-pass is pre-warped (TPT) on its skirt, so it is in the right place at
// 44.1 kHz too, and the level in the audible band is held within 0.3 dB from
// 44.1 to 192 kHz (bandPowerMatch()).

#pragma once

#include <complex>
#include <cstdint>

#include "mw808/Voices.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/MetalCommon.h"

namespace mw808::voices {

class Maracas {
public:
    void prepare(const VoiceSetup& setup) noexcept {
        rate_ = setup.sampleRate;
        dt_ = 1.0 / rate_;
        noiseScale_ = std::sqrt(rate_ / 48000.0);  // constant noise density (see HandClap.h)

        const std::uint32_t seed = setup.unitSeed;
        const float var = setup.variance;
        using dsp::componentScale;
        namespace mp = dsp::metalparts;
        hpScale_ = 1.0 / componentScale(seed, mp::kMaracas + 0U, 0.20f, var);
        c135Scale_ = componentScale(seed, mp::kMaracas + 1U, 0.20f, var);
        c134Scale_ = componentScale(seed, mp::kMaracas + 2U, 0.20f, var);
        silence_.prepare(rate_);
        noiseLp_.setCutoff(dsp::kLateNoiseRolloffHz, rate_);
        levelSlew_ = 1.0 - std::exp(-dt_ / 1.0e-3);

        haveParams_ = false;
        applyParams(params_);
        reset();
    }

    void reset() noexcept {
        clearState();
        active_ = false;
    }

    void setParams(const VoiceParams& raw) noexcept {
        const VoiceParams p = dsp::saneParams(raw);
        if (haveParams_ && p.tune == params_.tune && p.decay == params_.decay) return;
        applyParams(p);
    }

    void trigger(const Hit& hit) noexcept {
        const double v = dsp::saneVolts(hit.volts);
        // C135 peak relative to Q67's threshold; it only tops up (diode D67).
        const double hold = kHoldRatio * std::pow(v * 0.25, kHoldPower);
        if (hold > hold_) hold_ = hold;
        levelTarget_ = std::pow(v * 0.25, v <= 4.0 ? kSoftPower : kAccentPower);
        if (!active_) level_ = levelTarget_;  // otherwise glide (no step in a sounding hit)
        silence_.restart();
        active_ = true;
    }

    void render(float* out, const float* noise, int n) noexcept {
        if (!active_) {
            for (int i = 0; i < n; ++i) out[i] = 0.0f;
            return;
        }
        for (int i = 0; i < n; ++i) {
            const double y = tick(static_cast<double>(noise[i]));
            out[i] = static_cast<float>(y);
            if (silence_.tick(y) && hold_ <= 1.0) {
                for (int j = i + 1; j < n; ++j) out[j] = 0.0f;
                clearState();
                active_ = false;
                return;
            }
        }
    }

    bool isActive() const noexcept { return active_; }

    // Runs at the host rate with no lookahead: no latency (same interface as
    // the oversampled metal voices).
    double latencySamples() const noexcept { return 0.0; }

    // ---- schematic values -------------------------------------------------
    static constexpr double kHoldTau = 15.0e-3;   // R345 C135
    static constexpr double kRiseTau = 15.5e-3;   // R341 C134
    static constexpr double kFallTau = 3.3e-3;    // R343 C134
    static constexpr double kInHpHz = 1061.0, kHpHz = 10620.0, kOutLpHz = 8770.0;
    // ---- fitted -----------------------------------------------------------
    static constexpr double kHpQ = 1.0;
    static constexpr double kSkirtRatio = 0.88;  // pre-warp frequency re f0 (fitted, see applyParams)
    static constexpr double kHoldRatio = 3.92;   // C135 peak / Q67 threshold at a normal hit
    static constexpr double kHoldPower = 0.533;
    static constexpr double kGateOffset = 0.07;  // gate turn-on, as a fraction of full drive
    static constexpr double kSoftPower = 0.75;    // below a normal hit: level follows the pulse height
    static constexpr double kAccentPower = 0.18;  // above: on top of the longer hold
    static constexpr double kOutGain = 0.763;

private:
    void applyParams(const VoiceParams& p) noexcept {
        params_ = p;
        haveParams_ = true;
        const double ratio = dsp::semitonesToRatio(dsp::clamp(static_cast<double>(p.tune), -24.0, 24.0));
        inHp_.setCutoff(kInHpHz * ratio, rate_);
        // Pre-warp a little below f0 rather than at f0: with so little resonance
        // it is the skirt that is heard, and this keeps its -6 dB point within
        // 2 % from 44.1 kHz to 192 kHz (matching at f0 itself pushes the skirt up
        // by 6 % at 44.1 kHz). At high rates the two are the same.
        const double f3 = kSkirtRatio * kHpHz * hpScale_ * ratio;
        hpG_ = dsp::prewarp(std::min(f3, 0.45 * rate_), rate_) / kSkirtRatio;
        hp_.setG(hpG_, 1.0 / kHpQ);
        outLp_.setCutoff(kOutLpHz, rate_);
        bandGain_ = kOutGain * bandPowerMatch(ratio);
        const double decay = dsp::decayKnobScale(static_cast<double>(p.decay));
        holdDecay_ = std::exp(-dt_ / (kHoldTau * c135Scale_ * decay));
        rise_ = 1.0 - std::exp(-dt_ / (kRiseTau * c134Scale_ * decay));
        fall_ = 1.0 - std::exp(-dt_ / (kFallTau * c134Scale_ * decay));
    }

    void clearState() noexcept {
        noiseLp_.reset();
        inHp_.reset();
        hp_.reset();
        outLp_.reset();
        hold_ = 0.0;
        gate_ = 0.0;
        level_ = levelTarget_ = 1.0;
        silence_.restart();
    }

    double tick(double noise) noexcept {
        // C135 decays; Q67 is on while it is above the threshold (hold_ > 1).
        hold_ *= holdDecay_;
        if (hold_ > 1.0) gate_ += (1.0 - gate_) * rise_;
        else gate_ -= gate_ * fall_;
        // Gate transistor: nothing until the drive passes its offset (smooth knee).
        level_ += (levelTarget_ - level_) * levelSlew_;
        const double gain = gate_ * gate_ / (gate_ + kGateOffset) * level_;

        hp_.tick(inHp_.highpass(noiseLp_.tick(noise * noiseScale_) * gain));
        return outLp_.tick(hp_.highpass()) * bandGain_;
    }

    // The maracas lives at 10-20 kHz, where the filters' digital responses
    // fall short of the analog ones at 44.1 / 48 kHz (bilinear high-pass,
    // first-order low-passes near Nyquist): -1.3 dB of power at 48 kHz,
    // -0.1 dB at 192 kHz. Measure the shortfall over the audible band, against
    // the analog chain, and make it up with a gain, so the level does not
    // depend on the rate. Runs in applyParams (64 points, no allocation).
    double bandPowerMatch(double ratio) const noexcept {
        constexpr int kPoints = 64;
        const double fMax = std::min(20000.0, 0.49 * rate_);
        const double w0 = dsp::kTwoPi * kHpHz * hpScale_ * ratio, wIn = dsp::kTwoPi * kInHpHz * ratio;
        const double wNoise = dsp::kTwoPi * dsp::kLateNoiseRolloffHz, wOut = dsp::kTwoPi * kOutLpHz;
        const double gIn = dsp::prewarp(kInHpHz * ratio, rate_);
        double analog = 0.0, digital = 0.0;
        for (int i = 0; i < kPoints; ++i) {
            const double f = fMax * (static_cast<double>(i) + 0.5) / kPoints;
            const std::complex<double> s(0.0, dsp::kTwoPi * f);
            const std::complex<double> ha = (s * s / (s * s + s * (w0 / kHpQ) + w0 * w0)) * (s / (s + wIn)) /
                                            ((1.0 + s / wNoise) * (1.0 + s / wOut));
            const double w = dsp::kTwoPi * f / rate_;
            const std::complex<double> sn(0.0, std::tan(0.5 * w));  // the bilinear (TPT) frequency axis
            const std::complex<double> hd = (sn * sn / (sn * sn + sn * (hpG_ / kHpQ) + hpG_ * hpG_)) * (sn / (sn + gIn)) *
                                            noiseLp_.response(w) * outLp_.response(w);
            analog += std::norm(ha);
            digital += std::norm(hd);
        }
        return std::sqrt(analog / digital);
    }

    // configuration
    double rate_ = 48000.0, dt_ = 1.0 / 48000.0, noiseScale_ = 1.0;
    VoiceParams params_{};
    bool haveParams_ = false;
    double hpScale_ = 1.0, c135Scale_ = 1.0, c134Scale_ = 1.0;
    double holdDecay_ = 0.0, rise_ = 0.0, fall_ = 0.0, levelSlew_ = 1.0;
    double hpG_ = 0.5, bandGain_ = kOutGain;

    // blocks
    dsp::MatchedLowpass noiseLp_;  // the later units' noise roll-off (MetalCommon.h)
    dsp::OnePole inHp_;
    dsp::Svf hp_;
    dsp::MatchedLowpass outLp_;
    dsp::SilenceDetector silence_;

    // state
    double hold_ = 0.0, gate_ = 0.0, level_ = 1.0, levelTarget_ = 1.0;
    bool active_ = false;
};

}  // namespace mw808::voices

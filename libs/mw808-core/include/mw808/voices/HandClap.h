// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Hand clap (CP). Circuit and values: research/02 §5 (service manual p.6, p.9).
//
//   noise --> band-pass 1.25 kHz --+--> OTA VCA  <-- sawtooth "multi-clap" envelope --+
//                                  |                                                  +--> out
//                                  +--> Q70 stage <-- swelling "reverb" envelope -----+
//
// DERIVED from the schematic:
//  * Band-pass: multiple-feedback stage, R332 27k || C127 1.8n in, R333 10k,
//    C128 = C129 4.7n, R334 100k. Exactly
//      H(s) = -[ BP(s) / (R332 C129 w0) + (C127 / C129) HP(s) ],
//    w0 = 2 pi 1254 Hz, Q 1.55: peak ~1.27 kHz, +4.4 dB, with a -8 dB shelf
//    above (the input capacitor).
//  * Sawtooth envelope: C144 0.47u is slammed to -15 V (R367 330: 0.16 ms) on
//    the trigger, again each time it has recovered to the comparator threshold
//    (every ~11 ms) while the 30 ms gate is up, and once more when the gate
//    ends. It recovers through R365 82k || R356 1M (35.6 ms) during the gate
//    and through R356 1M alone (470 ms) afterwards. The VCA gain is
//    exponential in that voltage, so each burst jumps up and falls away in dB,
//    and the last one decays about 13x more slowly: bursts at 0 / 10.9 /
//    21.8 / 30 ms, as in the manual's timing drawing (0 / 10.8 / 22.1 / 29.7).
//  * Reverb path: the envelope is C139 18n (charged by the trigger) feeding
//    C138 47n through R348 1M, which drives Q70's base through R349 1M: it
//    swells in (12 ms) and decays (~80 ms). Q70's collector has C146 18n
//    against R363 10k (884 Hz low-pass); it reaches the summer through C152
//    4.7n + R376 47k (720 Hz high-pass). The output stage with LEVEL at
//    maximum adds a pole at 8.8 kHz.
//
// FITTED (against the service manual and the reference recording):
//  * Exponential-converter depth kappa = 17 nepers over the full C144 swing.
//    With the 470 ms free recovery this puts -20 dB at ~100 ms (service manual
//    100 ms, recording 89 ms) and makes each burst fall ~29 dB before the
//    next (recording: 26..31 dB; the manual's drawing shows ~19 dB, which
//    with the same law would stretch the final decay to ~190 ms).
//  * The slam lasts 0.8 ms (Q73's on-time is not documented).
//  * Reverb gain law x / (x + c) on the swelling envelope (a saturating
//    common-emitter stage) and its level: a dark bed ~31 dB under the burst
//    peaks that lasts ~250 ms and is gone by ~0.5 s, as in the recording.
//    c is a fixed voltage (a normal hit's charge times 0.003): a harder hit
//    charges C139 further above it and keeps Q70 on longer, so accent
//    lengthens the reverb tail rather than only scaling the clap.
//  * Comparator threshold (for the 10.9 ms burst period) and the 30 ms gate
//    (the manual states it; it cannot be derived from R350 C140).
//  * Levels: 6 Vpp normal. The manual's accented figure ("2 Vpp") is a
//    misprint; 12 Vpp (+6 dB) is used.
//
// Noise colour: the engine's noise bus is white; this voice applies the later
// units' 7.23 kHz roll-off (dsp::kLateNoiseRolloffHz) to its own input, as
// the reference unit has it. The band's centroid is then 2.3 kHz against the
// recording's 1.9 kHz (white: 3.3 kHz).
//
// The main path has no high-pass at the summer: the 1.6 kHz corner suggested
// in research/02 §5.6 is not seen in the recording's spectrum.
//
// Knobs (none on the hardware): tone moves the band-pass centre (x0.5..x2,
// 0.5 = stock), shape is the reverb amount (0.5 = stock, 0 = none), decay
// scales the free decay and the reverb time (x0.25..x4), tune shifts the
// band-pass with tone. Runs at 1x; the level in the audible band is held
// within 0.2 dB from 44.1 to 192 kHz (bandPowerMatch()). A sleeping voice
// warms its band-pass up on its own noise at wake (wake()).

#pragma once

#include <complex>
#include <cstdint>

#include "mw808/Voices.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/MetalCommon.h"
#include "mw808/dsp/Noise.h"

namespace mw808::voices {

class HandClap {
public:
    void prepare(const VoiceSetup& setup) noexcept {
        rate_ = setup.sampleRate;
        dt_ = 1.0 / rate_;
        // The noise input is unit RMS at any rate; the analog source has a
        // fixed density, so scale to a constant power per hertz.
        noiseScale_ = std::sqrt(rate_ / 48000.0);

        const std::uint32_t seed = setup.unitSeed;
        const float var = setup.variance;
        using dsp::componentScale;
        namespace mp = dsp::metalparts;
        bpScale_ = 1.0 / componentScale(seed, mp::kClap + 0U, 0.20f, var);
        c144Scale_ = componentScale(seed, mp::kClap + 1U, 0.20f, var);
        revScale_ = componentScale(seed, mp::kClap + 2U, 0.20f, var);
        gateSamples_ = std::max(1, static_cast<int>(kGateSeconds * componentScale(seed, mp::kClap + 3U, 0.20f, var) * rate_ + 0.5));
        slamSamples_ = std::max(1, static_cast<int>(kSlamSeconds * rate_ + 0.5));
        slam_ = std::exp(-dt_ / kSlamTau);
        recoverGate_ = 1.0 - std::exp(-dt_ / (kGateRecoverTau * c144Scale_));
        silence_.prepare(rate_);
        noiseLp_.setCutoff(dsp::kLateNoiseRolloffHz, rate_);
        warmSamples_ = static_cast<int>(kWarmSeconds * rate_);

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
        if (haveParams_ && p.tune == params_.tune && p.decay == params_.decay && p.tone == params_.tone &&
            p.shape == params_.shape)
            return;
        applyParams(p);
    }

    void trigger(const Hit& hit) noexcept {
        if (!active_) wake();
        const double v = dsp::saneVolts(hit.volts);
        // Normal steps and softer: level follows the pulse height. Accent
        // raises the exponential converter's bias: +6 dB at full accent.
        const double level = v <= 4.0 ? v * 0.25 : std::pow(v * 0.25, kAccentPower);
        mainTarget_ = level;
        reverbCharge_ = level;
        gateLeft_ = gateSamples_;
        slamLeft_ = slamSamples_;
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
            if (silence_.tick(y) && gateLeft_ == 0 && slamLeft_ == 0) {
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
    static constexpr double kBpHz = 1253.6, kBpQ = 1.554;
    static constexpr double kBpGain = 1.0003;        // 1 / (R332 C129 w0)
    static constexpr double kShelfGain = 1.8 / 4.7;  // C127 / C129
    static constexpr double kSlamTau = 330.0 * 0.47e-6;
    static constexpr double kGateRecoverTau = 0.47e-6 / (1.0 / 82.0e3 + 1.0 / 1.0e6);
    static constexpr double kFreeRecoverTau = 0.47e-6 * 1.0e6;
    static constexpr double kGateSeconds = 30.0e-3;
    static constexpr double kR348C139 = 1.0e6 * 18.0e-9, kR348C138 = 1.0e6 * 47.0e-9, kR349C138 = 1.0e6 * 47.0e-9;
    static constexpr double kReverbLpHz = 884.0, kReverbHpHz = 720.0, kOutLpHz = 8770.0;
    // ---- fitted -----------------------------------------------------------
    static constexpr double kKappa = 17.0;
    static constexpr double kThreshold = 0.2513;  // C144 recovery (0..1) at which the comparator re-fires
    static constexpr double kSlamSeconds = 0.8e-3;
    static constexpr double kReverbKnee = 0.003;  // c in x / (x + c), re a normal hit's C139 charge
    static constexpr double kReverbGain = 0.04;
    static constexpr double kAccentPower = 0.553;  // (14/4)^0.553 = 2
    static constexpr double kOutGain = 0.626;
    static constexpr double kWarmSeconds = 4.0e-3;  // > 10 band-pass time constants

private:
    void applyParams(const VoiceParams& p) noexcept {
        params_ = p;
        haveParams_ = true;
        const double tune = dsp::semitonesToRatio(dsp::clamp(static_cast<double>(p.tune), -24.0, 24.0));
        const double tone = std::exp2(2.0 * (dsp::clamp(static_cast<double>(p.tone), 0.0, 1.0) - 0.5));
        const double ratio = tune * tone;
        const double bpHz = kBpHz * bpScale_ * ratio;
        bp_.setFreqQ(bpHz, kBpQ, rate_);
        reverbLp_.setCutoff(kReverbLpHz * ratio, rate_);
        reverbHp_.setCutoff(kReverbHpHz * ratio, rate_);
        outLp_.setCutoff(kOutLpHz, rate_);
        outGain_ = kOutGain * bandPowerMatch(bpHz);

        const double decay = dsp::decayKnobScale(static_cast<double>(p.decay));
        recoverFree_ = 1.0 - std::exp(-dt_ / (kFreeRecoverTau * c144Scale_ * decay));
        const double rev = revScale_ * decay;
        k139_ = dt_ / (kR348C139 * rev);
        k138a_ = dt_ / (kR348C138 * rev);
        k138b_ = dt_ / (kR349C138 * rev);
        const double s = 2.0 * dsp::clamp(static_cast<double>(p.shape), 0.0, 1.0);
        reverbAmount_ = kReverbGain * s * s;
        floor_ = std::exp(-kKappa);
        norm_ = 1.0 / (1.0 - floor_);
    }

    // Wake from sleep. The band-pass runs on the noise all the time in the
    // hardware, but a sleeping voice is not rendered, so its state would start
    // at zero and soften the first burst (~0.4 ms to build up). Run a few
    // milliseconds of noise of the same density through it first: a state
    // statistically identical to that of a voice that never slept.
    void wake() noexcept {
        for (int i = 0; i < warmSamples_; ++i) bp_.tick(noiseLp_.tick(static_cast<double>(warmNoise_.next()) * noiseScale_));
    }

    void clearState() noexcept {
        noiseLp_.reset();
        bp_.reset();
        reverbLp_.reset();
        reverbHp_.reset();
        outLp_.reset();
        u_ = 1.0;
        v139_ = v138_ = 0.0;
        mainLevel_ = mainTarget_ = 0.0;
        reverbCharge_ = 0.0;
        gateLeft_ = slamLeft_ = 0;
        silence_.restart();
    }

    double tick(double noise) noexcept {
        // ---- sawtooth envelope: u = 0 right after a slam, recovering to 1 ----
        if (slamLeft_ > 0) {
            --slamLeft_;
            u_ *= slam_;
            mainLevel_ += (mainTarget_ - mainLevel_) * (1.0 - slam_);
            // the trigger also tops up the reverb envelope's first capacitor
            if (reverbCharge_ > v139_) v139_ += (reverbCharge_ - v139_) * (1.0 - slam_);
        } else if (gateLeft_ > 0) {
            u_ += (1.0 - u_) * recoverGate_;
            if (u_ >= kThreshold) slamLeft_ = slamSamples_;  // comparator re-fires
        } else {
            u_ += (1.0 - u_) * recoverFree_;
        }
        if (gateLeft_ > 0 && --gateLeft_ == 0) {
            slamLeft_ = slamSamples_;  // the gate's falling edge: the last clap
        }
        const double gMain = (std::exp(-kKappa * u_) - floor_) * norm_ * mainLevel_;

        // ---- reverb envelope: C139 -> R348 -> C138 -> R349 -> Q70 base ----
        const double i348 = v139_ - v138_;
        v139_ -= k139_ * i348;
        v138_ += k138a_ * i348 - k138b_ * v138_;
        const double gReverb = reverbAmount_ * v138_ / (v138_ + kReverbKnee) * mainTarget_;

        // ---- audio ----
        bp_.tick(noiseLp_.tick(noise * noiseScale_));
        const double band = -(kBpGain * bp_.bandpass() + kShelfGain * bp_.highpass());
        const double main = band * gMain;
        const double reverb = reverbHp_.highpass(reverbLp_.lowpass(band * gReverb));
        return outLp_.tick(main + reverb) * outGain_;
    }

    // The input-capacitor shelf carries the clap's top octave, where the
    // first-order low-passes (noise roll-off, output stage) fall short of
    // their analog responses near Nyquist. As in Maracas.h: measure the main
    // path's power over the audible band against the analog prototype and
    // make up the difference, so the level does not depend on the rate.
    double bandPowerMatch(double bpHz) const noexcept {
        constexpr int kPoints = 64;
        const double fMax = std::min(20000.0, 0.49 * rate_);
        const double w0 = dsp::kTwoPi * bpHz, wNoise = dsp::kTwoPi * dsp::kLateNoiseRolloffHz, wOut = dsp::kTwoPi * kOutLpHz;
        const double g = dsp::prewarp(bpHz, rate_), k = 1.0 / kBpQ;
        double analog = 0.0, digital = 0.0;
        for (int i = 0; i < kPoints; ++i) {
            const double f = fMax * (static_cast<double>(i) + 0.5) / kPoints;
            const std::complex<double> s(0.0, f * dsp::kTwoPi / w0);  // s / w0
            const std::complex<double> ha = (kBpGain * s + kShelfGain * s * s) / (s * s + k * s + 1.0) /
                                            ((1.0 + std::complex<double>(0.0, dsp::kTwoPi * f) / wNoise) *
                                             (1.0 + std::complex<double>(0.0, dsp::kTwoPi * f) / wOut));
            const double w = dsp::kTwoPi * f / rate_;
            const std::complex<double> sn = std::complex<double>(0.0, std::tan(0.5 * w)) / g;  // TPT: s / w0 on the warped axis
            const std::complex<double> hd = (kBpGain * sn + kShelfGain * sn * sn) / (sn * sn + k * sn + 1.0) * noiseLp_.response(w) *
                                            outLp_.response(w);
            analog += std::norm(ha);
            digital += std::norm(hd);
        }
        return std::sqrt(analog / digital);
    }

    // configuration
    double rate_ = 48000.0, dt_ = 1.0 / 48000.0, noiseScale_ = 1.0;
    VoiceParams params_{};
    bool haveParams_ = false;
    double bpScale_ = 1.0, c144Scale_ = 1.0, revScale_ = 1.0;
    double slam_ = 0.0, recoverGate_ = 0.0, recoverFree_ = 0.0;
    double k139_ = 0.0, k138a_ = 0.0, k138b_ = 0.0;
    double reverbAmount_ = 0.0, floor_ = 0.0, norm_ = 1.0, outGain_ = kOutGain;
    int gateSamples_ = 1440, slamSamples_ = 24, warmSamples_ = 192;

    // blocks
    dsp::MatchedLowpass noiseLp_;  // the later units' noise roll-off (see the header comment)
    dsp::WhiteNoise warmNoise_;    // only for wake()
    dsp::Svf bp_;
    dsp::OnePole reverbLp_, reverbHp_;
    dsp::MatchedLowpass outLp_;
    dsp::SilenceDetector silence_;

    // state
    double u_ = 1.0;
    double v139_ = 0.0, v138_ = 0.0;
    double mainLevel_ = 0.0, mainTarget_ = 0.0, reverbCharge_ = 0.0;
    int gateLeft_ = 0, slamLeft_ = 0;
    bool active_ = false;
};

}  // namespace mw808::voices

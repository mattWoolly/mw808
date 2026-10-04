// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Small helpers shared by the metallic voices (cymbal, hi-hats, cowbell) and
// the noise voices (hand clap, maracas): the narrowed trigger range, smooth
// junction drops, the level-pot output pole and a tail-silence detector.
// Header-only, allocation-free.

#pragma once

#include <complex>
#include <cstdint>

#include "mw808/Voices.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Math.h"

namespace mw808::dsp {

// Thermal voltage at ~300 K, in volts.
inline constexpr double kThermalVolts = 0.026;

// Envelope networks of the metal voices are advanced at about this rate and
// ramped in between (their fastest time constant is the 0.1 ms attack).
inline constexpr double kMetalControlHz = 12000.0;

// The noise generator of later production units ("6 lot ~") rolls off above
// 1 / (2 pi R130 C202) = 1 / (2 pi 1 MOhm 22 pF) = 7.23 kHz (research/02 §7),
// and the reference recordings come from such a unit. The engine's noise bus
// stays white (contract), so the hand clap and the maracas apply this pole to
// their own noise input; MatchedLowpass keeps the analog response up to
// Nyquist at every rate.
inline constexpr double kLateNoiseRolloffHz = 7230.0;

// The metal voices' trigger buffer narrows the 4..14 V common trigger to
// 7..14 V (research/03 §3 item 6; the linear map fits both ends). Below the
// 4 V of a normal step the hardware has no defined behaviour: we continue
// with a gentle power law that reaches 0 V at 0 V so weak hits stay musical
// and monotonic (fitted, non-hardware region).
inline double metalTriggerVolts(double hitVolts) noexcept {
    const double v = clamp(hitVolts, 0.0, 14.0);
    if (v >= 4.0) return 7.0 + 0.7 * (v - 4.0);
    return 7.0 * std::pow(v * 0.25, 0.75);
}

// A voltage after one or more junction drops totalling `drop`, with a smooth
// knee: tends to v - drop for large v, is 0 at 0 and strictly increasing, so
// small triggers fade out instead of switching off. Rational form (no exp),
// cheap enough for per-sample use.
inline double softDrop(double v, double drop) noexcept {
    if (v <= 0.0) return 0.0;
    return v * v / (v + drop);
}

// Peak voltage an envelope capacitor reaches for a hit: the narrowed trigger
// minus the junction drops in its charging path, then a compressive power law
// about the normal-hit level. `power` = 1 is the plain circuit; each voice
// fits it so that its accented level lands on the service-manual table
// (< 1 for the cymbal and hats, where the plain circuit over-shoots the
// manual's +6 dB; > 1 for the cowbell, whose +10.7 dB it does not reach).
inline double metalChargeVolts(double hitVolts, double drop, double power) noexcept {
    const double normal = softDrop(7.0, drop);
    const double v = softDrop(metalTriggerVolts(hitVolts), drop);
    return v <= 0.0 ? 0.0 : normal * std::pow(v / normal, power);
}

// Non-hardware decay knobs: 0.5 is the stock circuit, the travel spans
// x0.25 .. x4 (Voices.h).
inline double decayKnobScale(double knob) noexcept {
    return std::exp2(4.0 * (clamp(knob, 0.0, 1.0) - 0.5));
}

// Tunes a TPT state-variable filter as a narrow resonator whose bandwidth, not
// only its centre, matches the analog prototype. The bilinear transform
// prewarped at the centre narrows the band by sin(2a)/(2a), a = pi f / fs
// (-14 % for the 7.1 kHz Q 6 cymbal/hat band-pass at 48 kHz), so it rings
// longer and passes less of the oscillator spectrum, and the swing VCAs behind
// it lose level at low internal rates. Widening k by 2a/sin(2a) restores the
// bandwidth; the band-pass output (k * bp) keeps unity peak gain, and the
// low-pass output must be multiplied by the returned factor to keep its
// analog peak gain (Q). Near Nyquist, where nothing is analog-like anyway, the
// correction is capped.
struct ResonatorCoefs {
    double g, k, lowpassScale;
};
inline ResonatorCoefs matchedResonator(double freqHz, double q, double sampleRate) noexcept {
    const double g = prewarp(freqHz, sampleRate);
    const double twoA = std::min(2.0 * std::atan(g), 1.2);
    const double widen = twoA / std::sin(twoA);
    return {g, widen / std::max(q, 1.0e-6), widen};
}
inline double setResonatorMatched(Svf& f, double freqHz, double q, double sampleRate) noexcept {
    const ResonatorCoefs c = matchedResonator(freqHz, q, sampleRate);
    f.setG(c.g, c.k);
    return c.lowpassScale;
}

// Parameter hygiene for the audio thread: a NaN control value takes the stock
// value, anything else (infinities included) is clamped to the knob range.
// One NaN must not poison a voice's state for good.
inline float saneKnob(float x, float stock, float lo, float hi) noexcept {
    return std::isnan(x) ? stock : clamp(x, lo, hi);
}
inline VoiceParams saneParams(const VoiceParams& p) noexcept {
    return {saneKnob(p.tune, 0.0f, -24.0f, 24.0f), saneKnob(p.decay, 0.5f, 0.0f, 1.0f),
            saneKnob(p.tone, 0.5f, 0.0f, 1.0f), saneKnob(p.shape, 0.5f, 0.0f, 1.0f)};
}
// Trigger height in volts: NaN is a normal hit, the rest is clamped to 0..14 V.
inline double saneVolts(float v) noexcept {
    return std::isnan(v) ? static_cast<double>(Hit::kNormalVolts) : clamp(static_cast<double>(v), 0.0, 14.0);
}

// Residual level loss of a swing-VCA chain at a finite internal rate, as a gain
// that undoes it. The base clamp responds to the PEAKS of its band-limited
// drive, and sampled peaks fall short of the true ones by up to
// 1 - cos(pi f / fs); the transistor then lets go later and the collector
// swings less. With the exact collector update and the bandwidth-matched
// band-pass this is all that is left, and it is smooth in x = (f / fs)^2:
//     loss(dB) = a x / (1 + b x)
// (0.6 dB at 96 kHz and 2 dB at 44.1 kHz for the 7.1 kHz band, converging
// to zero as the rate goes up). a and b are fitted per voice against renders
// at a 768 kHz internal rate; see the voice headers.
inline double swingRateGain(double bandHz, double internalRate, double a, double b) noexcept {
    // Fitted up to x = 0.026 (7.1 kHz at 44.1 kHz). Beyond that the band is
    // close to or past Nyquist (extreme tune at a low rate) and there is
    // nothing left to correct: hold the gain instead of extrapolating it.
    const double r = std::min(bandHz / internalRate, 0.17);
    const double x = r * r;
    return std::pow(10.0, (a * x / (1.0 + b * x)) / 20.0);
}

// First-order low-pass matched to the analog prototype at DC and at Nyquist
// (impulse-invariant pole, zero placed for the Nyquist gain). Used for the
// fixed output-stage poles near 9 kHz, where a bilinear one-pole would lose
// several dB at the top of the band at 44.1 kHz.
class MatchedLowpass {
public:
    void setCutoff(double freqHz, double sampleRate) noexcept {
        const double f = clamp(freqHz, 1.0, 20.0 * sampleRate);
        const double p = std::exp(-kTwoPi * f / sampleRate);
        const double nyq = 0.5 * sampleRate / f;
        const double gN = 1.0 / std::sqrt(1.0 + nyq * nyq);
        b0_ = 0.5 * ((1.0 - p) + gN * (1.0 + p));
        b1_ = 0.5 * ((1.0 - p) - gN * (1.0 + p));
        a1_ = p;
    }
    void reset() noexcept { x1_ = y1_ = 0.0; }
    // Frequency response at w = 2 pi f / fs.
    std::complex<double> response(double w) const noexcept {
        const std::complex<double> zi = std::polar(1.0, -w);
        return (b0_ + b1_ * zi) / (1.0 - a1_ * zi);
    }
    double tick(double x) noexcept {
        const double y = b0_ * x + b1_ * x1_ + a1_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }
    double stateMagnitude() const noexcept { return std::abs(x1_) + std::abs(y1_); }

private:
    double b0_ = 1.0, b1_ = 0.0, a1_ = 0.0;
    double x1_ = 0.0, y1_ = 0.0;
};

// Decides when a voice has rung out: the output has stayed below the sleep
// threshold (about -120 dBFS) for a hold time. The caller then clears its
// state so that render() produces exact zeros.
class SilenceDetector {
public:
    static constexpr double kThreshold = 1.0e-6;

    void prepare(double sampleRate, double holdSeconds = 0.02) noexcept {
        hold_ = std::max(8, static_cast<int>(holdSeconds * sampleRate));
        count_ = 0;
    }
    void restart() noexcept { count_ = 0; }
    // Returns true once the signal has been quiet for the hold time.
    bool tick(double y) noexcept {
        if (std::abs(y) > kThreshold) count_ = 0;
        else ++count_;
        return count_ >= hold_;
    }

private:
    int hold_ = 960;
    int count_ = 0;
};

// Component ids used with componentScale() by the metal and noise voices.
// The oscillator bank ids are shared so that every bank instance built from
// the same unit seed detunes identically (one physical bank in the hardware).
namespace metalparts {
inline constexpr std::uint32_t kOscR = 0x0C00U;      // +0..5
inline constexpr std::uint32_t kOscC = 0x0C10U;      // +0..5
inline constexpr std::uint32_t kOscTrim = 0x0C20U;   // +4, +5 (factory-trimmed pair)
inline constexpr std::uint32_t kBandPass = 0x0C30U;  // +0 BP#1, +1 BP#2 (shared CY / hats)
inline constexpr std::uint32_t kCymbal = 0x0C40U;
inline constexpr std::uint32_t kHats = 0x0C60U;
inline constexpr std::uint32_t kCowbell = 0x0C80U;
inline constexpr std::uint32_t kClap = 0x0CA0U;
inline constexpr std::uint32_t kMaracas = 0x0CC0U;
}  // namespace metalparts

}  // namespace mw808::dsp

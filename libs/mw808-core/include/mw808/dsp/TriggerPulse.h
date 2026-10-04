// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The gated trigger pulse that excites a voice (research/01 §0.1): a 1 ms
// rectangle whose height is the accent voltage, and the passive network that
// shapes it on the way to a resonator's non-inverting input.
//
// The shaping networks have time constants of 5..600 us, shorter than a sample
// at 44.1 kHz, so the pulse runs on an internal sub-step of about 5 us at every
// sample rate and is box-averaged down. The integrators are trapezoidal WITHOUT
// pre-warping, which preserves the area of each spike exactly; the resonators
// respond to that area, so hit levels do not depend on the sample rate.

#pragma once

#include "mw808/Voices.h"
#include "mw808/dsp/Math.h"

namespace mw808::dsp {

inline constexpr double kTriggerWidthSeconds = 1.0e-3;

// Control values as a voice may safely use them: a NaN (a corrupt host value)
// falls back to the stock setting and infinities are pulled into a finite
// range, so one bad automation value cannot poison a voice's state for good.
// The voices clamp the result to their own ranges.
inline VoiceParams saneParams(const VoiceParams& p, const VoiceParams& stock) noexcept {
    const auto fix = [](float x, float fallback, float limit) noexcept {
        return std::isnan(x) ? fallback : clamp(x, -limit, limit);
    };
    VoiceParams s;
    s.tune = fix(p.tune, stock.tune, 1000.0f);
    s.decay = fix(p.decay, stock.decay, 2.0f);
    s.tone = fix(p.tone, stock.tone, 2.0f);
    s.shape = fix(p.shape, stock.shape, 2.0f);
    return s;
}

// The unit variance as a voice may safely use it: 0..1, NaN -> 0 (nominal
// parts). Beyond 1 the +-20 % capacitor spread reaches zero and negative
// values, and a NaN would make every part NaN; either leaves a voice emitting
// NaN for good.
inline VoiceSetup saneSetup(const VoiceSetup& s) noexcept {
    VoiceSetup out = s;
    out.variance = std::isnan(s.variance) ? 0.0f : clamp(s.variance, 0.0f, 1.0f);
    return out;
}

// Plain rectangle, area-exact at the sample rate (the last sample carries the
// fractional remainder). For the slow consumers: envelope followers and the
// snare's undifferentiated resonator feed.
class TriggerGate {
public:
    void prepare(double sampleRate) noexcept {
        fs_ = sampleRate;
        reset();
    }
    void reset() noexcept {
        height_ = 0.0;
        pos_ = high_ = 0.0;
        active_ = false;
    }
    void fire(double volts, double widthSeconds = kTriggerWidthSeconds) noexcept {
        height_ = volts;
        high_ = widthSeconds * fs_;
        pos_ = 0.0;
        active_ = true;
    }
    bool active() const noexcept { return active_; }
    double tick() noexcept {
        if (!active_) return 0.0;
        const double cover = clamp(high_ - pos_, 0.0, 1.0);
        pos_ += 1.0;
        if (pos_ >= high_) active_ = false;
        return height_ * cover;
    }

private:
    double fs_ = 48000.0;
    double height_ = 0.0;
    double pos_ = 0.0, high_ = 0.0;
    bool active_ = false;
};

// Rectangle -> series (C || R_series) into a shunt resistor -> memoryless
// shaper (the clamp diode). The linear part is the low-shelf
//
//   H(s) = (s tau + r) / (s tau + 1),   tau = C (R_series || R_shunt),
//                                       r = R_shunt / (R_series + R_shunt)
//
// i.e. a spike of the full pulse height on each edge that relaxes to r times
// the height. r = 0 is a pure differentiator (series capacitor only).
class TriggerPulse {
public:
    void prepare(double sampleRate) noexcept {
        const double T = 1.0 / sampleRate;
        sub_ = clamp(static_cast<int>(std::ceil(T / kMaxSubStep - 1.0e-9)), 1, 8);
        dt_ = T / static_cast<double>(sub_);
        invSub_ = 1.0 / static_cast<double>(sub_);
        setNetwork(67.0e-6, 0.0);
        reset();
    }
    void reset() noexcept {
        s_ = 0.0;
        height_ = 0.0;
        pos_ = high_ = 0.0;
        active_ = false;
    }
    // `tail`: keep running after the falling edge until its spike has died
    // away. Networks whose diode blocks that spike anyway pass false.
    void setNetwork(double tauSeconds, double dcRatio, bool tail = true) noexcept {
        const double g = std::min(dt_ / (2.0 * tauSeconds), 4.0);
        G_ = g / (1.0 + g);
        dc_ = dcRatio;
        hf_ = 1.0 - dcRatio;
        // Sub-steps until the spike after the falling edge is below 1e-13.
        const double pole = std::abs((1.0 - g) / (1.0 + g));
        tail_ = 4.0 + (tail && pole > 1.0e-9 ? std::ceil(-30.0 / std::log(pole)) : 0.0);
    }
    void fire(double volts, double widthSeconds = kTriggerWidthSeconds) noexcept {
        height_ = volts;
        high_ = widthSeconds / dt_;
        pos_ = 0.0;
        active_ = true;
    }
    bool active() const noexcept { return active_; }

    // One output sample of the shaped excitation, in volts. `shape` maps the
    // linear network output to the voltage after the diode.
    template <typename Shape>
    double tick(Shape&& shape) noexcept {
        if (!active_) return 0.0;
        double acc = 0.0;
        for (int j = 0; j < sub_; ++j) {
            const double p = height_ * clamp(high_ - pos_, 0.0, 1.0);
            pos_ += 1.0;
            const double v = (p - s_) * G_;
            const double lp = v + s_;
            s_ = lp + v;
            acc += shape(hf_ * (p - lp) + dc_ * p);
        }
        if (pos_ >= high_ + tail_) {
            active_ = false;
            s_ = 0.0;
        }
        return acc * invSub_;
    }

private:
    static constexpr double kMaxSubStep = 6.0e-6;

    int sub_ = 4;
    double dt_ = 1.0 / 192000.0, invSub_ = 0.25;
    double G_ = 0.0, dc_ = 0.0, hf_ = 1.0;
    double s_ = 0.0;
    double height_ = 0.0;
    double pos_ = 0.0, high_ = 0.0, tail_ = 4.0;
    bool active_ = false;
};

// The clamp diode to ground at a shaper's output, as a memoryless law with unit
// slope through zero: positive values pass, negative ones saturate at -volts.
inline double clampNegative(double v, double volts) noexcept {
    return v >= 0.0 ? v : -volts * (1.0 - std::exp(v / volts));
}

// A series diode between the differentiator and the resonator input: only the
// part of a positive spike above the diode drop passes. Soft knee (algebraic,
// no transcendentals), exactly 0 for v <= 0, monotonic down to the smallest
// trigger.
inline double seriesDiode(double v, double dropVolts = 0.6, double kneeVolts = 0.05) noexcept {
    if (v <= 0.0) return 0.0;
    const double k2 = kneeVolts * kneeVolts;
    const double d = v - dropVolts;
    return 0.5 * (d + std::sqrt(d * d + k2)) - 0.5 * (std::sqrt(dropVolts * dropVolts + k2) - dropVolts);
}

// The JFET mute on the rim shot / claves mixer: the output is open for a hold
// time after each trigger, then fades out.
class MuteGate {
public:
    void prepare(double sampleRate, double fadeSeconds = 0.5e-3) noexcept {
        fs_ = sampleRate;
        fade_ = expDecayCoeff(fadeSeconds, sampleRate);
        reset();
    }
    void reset() noexcept {
        gain_ = 0.0;
        hold_ = 0.0;
    }
    void open(double holdSeconds) noexcept {
        hold_ = holdSeconds * fs_;
        gain_ = 1.0;
    }
    double tick() noexcept {
        if (hold_ > 0.0) {
            hold_ -= 1.0;
            return 1.0;
        }
        gain_ *= fade_;
        return gain_;
    }
    bool closed() const noexcept { return hold_ <= 0.0 && gain_ < 1.0e-7; }

private:
    double fs_ = 48000.0, fade_ = 0.0;
    double gain_ = 0.0, hold_ = 0.0;
};

// Trigger transfer: the voice's own pulse height for a common-trigger height.
// 4 V maps to 4 V. Above it, `gamma` (fitted per voice to the service-manual
// normal and accent levels) bends the accent range; gamma = 1 is the ideal
// gate. Below it, the same curve runs down to `floorVolts`: a circuit with
// diode thresholds would simply not fire on a weak pulse, and the floor keeps
// weak hits and low velocities audible and monotonic instead.
inline double triggerTransfer(double volts, double gamma, double floorVolts = 0.0) noexcept {
    const double shaped = std::pow(clamp(volts, 1.0e-3, 14.0) * 0.25, gamma);
    return volts >= 4.0 ? 4.0 * shaped : floorVolts + (4.0 - floorVolts) * shaped;
}

}  // namespace mw808::dsp

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The bridged-T resonator of the original machine's tonal voices, kept in its
// own physical state variables so that component changes during a note (the
// bass drum's attack switch and pitch "sigh", the toms' diode clamp, a tune
// glide) behave as they do in the circuit.
//
// Circuit (research/01 §0.2): an op-amp with C1 (inverting input -> junction M)
// and C2 (M -> output) in its feedback path, bridged by R2 (output -> inverting
// input), with a conductance G_M from M to ground. The excitation x drives the
// non-inverting input. Optional regeneration: a second stage returns -G*Vout
// into M through R_fb. With
//
//   w = x - V_M        (voltage across C1; V_M is the junction voltage)
//   u = Vout - x       (the ringing part of the output)
//
// nodal analysis with an ideal op-amp gives
//
//   dw/dt = a u                         a = 1 / (R2 C1)
//   du/dt = -d u - b w + drive          b = G_M / C2
//                                       d = (C1 + C2)/(R2 C1 C2) - G/(R_fb C2)
//                                       drive = [x (G_M + G/R_fb) + i_inj] / C2
//
// so w0^2 = a b, the amplitude decays as exp(-d t / 2) whatever G_M is (tuning
// through the resistor to ground never changes the decay time), and both
// capacitor voltages stay continuous when G_M moves. A textbook SVF has the
// same poles but scales its band-pass state with frequency, which gets the
// amplitude wrong across the bass drum's 130 Hz -> 49 Hz attack switch.
//
// Discretisation: trapezoidal (TPT) integrators with the zero-delay loop solved
// in closed form, per-sample coefficients, frequency pre-warped, and the damping
// corrected so the decay TIME (not Q) is exact at any sample rate.

#pragma once

#include <array>

#include "mw808/dsp/Math.h"

namespace mw808::dsp {

// ln(1 + e^z), safe for any z.
inline double softplus(double z) noexcept {
    if (z > 34.0) return z;
    if (z < -34.0) return 0.0;
    return std::log1p(std::exp(z));
}

// Non-hardware DECAY knobs: 0.5 is the stock circuit, the travel is x0.25..x4
// in decay time.
inline double decayKnobToMultiplier(double knob) noexcept {
    return std::exp2(4.0 * clamp(knob, 0.0, 1.0) - 2.0);
}

// Potentiometer "A" (audio) taper: fraction of the track resistance at rotation
// t. `mid` is the fraction at half rotation (0.1..0.25 for real parts).
inline double audioTaper(double t, double mid) noexcept {
    const double r = 1.0 / mid - 1.0;  // sqrt of the exponential base
    const double base = r * r;
    return (std::pow(base, clamp(t, 0.0, 1.0)) - 1.0) / (base - 1.0);
}

class BridgedT {
public:
    void prepare(double sampleRate) noexcept {
        T_ = 1.0 / sampleRate;
        reset();
    }
    void reset() noexcept { sw_ = su_ = w_ = u_ = 0.0; }

    // Integrator step and damping for one sample, pre-warped for the angular
    // frequency sqrt(w0sq).
    struct Step {
        double h = 0.0;   // trapezoidal half-step, pre-warped
        double dd = 0.0;  // damping, corrected for the decay time
    };
    Step step(double w0sq, double d) const noexcept {
        // y = (w0 T)^2, limited so the warp series below stays accurate.
        const double y = std::min(w0sq * T_ * T_, 2.5);
        const double x2 = 0.25 * y;
        // tan(x)/x for x = w0 T / 2: pre-warps the resonance.
        const double warp =
            1.0 + x2 * (1.0 / 3.0 + x2 * (2.0 / 15.0 + x2 * (17.0 / 315.0 + x2 * (62.0 / 2835.0))));
        // (w0 T)/sin(w0 T): the bilinear map shrinks the real part of a pole
        // pair by sin(w0 T)/(w0 T); undo it so the decay time is preserved.
        const double keep =
            1.0 + y * (1.0 / 6.0 + y * (7.0 / 360.0 + y * (31.0 / 15120.0 + y * (127.0 / 604800.0))));
        return {0.5 * T_ * warp, d * keep};
    }

    // One sample. a, b, d in 1/s and drive in V/s as defined above; all may
    // change every sample. Requires a > 0, b > 0, d >= 0.
    void tick(double a, double b, double d, double drive) noexcept { tick(step(a * b, d), a, b, drive); }

    // One sample with a precomputed step. A voice whose b depends on the state
    // (a diode or transistor load on the junction) warps for its small-signal
    // frequency, so that h does not swing with the state within a cycle.
    void tick(const Step& s, double a, double b, double drive) noexcept {
        const double ha = s.h * a;
        u_ = (su_ + s.h * (drive - b * sw_)) / (1.0 + s.h * s.dd + ha * s.h * b);
        w_ = sw_ + ha * u_;
        sw_ = w_ + ha * u_;
        su_ = u_ + s.h * (drive - s.dd * u_ - b * w_);
    }

    // The w that tick(s, a, b, drive) would produce, without committing it:
    // lets a voice evaluate a state-dependent load at the end of the step
    // (one fixed-point iteration of the implicit solve).
    double trialW(const Step& s, double a, double b, double drive) const noexcept {
        const double ha = s.h * a;
        const double u = (su_ + s.h * (drive - b * sw_)) / (1.0 + s.h * s.dd + ha * s.h * b);
        return sw_ + ha * u;
    }

    // The op-amp cannot swing past its supply rails. Call after tick(), with
    // the same excitation x: the op-amp's output is Vout = x + u, so it is
    // Vout, not the ringing part u alone, that the rails limit.
    void clampOutput(double rail, double x) noexcept {
        const double hi = rail - x, lo = -rail - x;
        if (u_ > hi) {
            u_ = hi;
            su_ = std::min(su_, hi);
        } else if (u_ < lo) {
            u_ = lo;
            su_ = std::max(su_, lo);
        }
    }

    double u() const noexcept { return u_; }  // Vout - x
    double w() const noexcept { return w_; }  // x - V_M
    // w one sample ahead (explicit Euler), for evaluating nonlinear junction
    // loads without a one-sample lag.
    double predictW(double a) const noexcept { return w_ + T_ * a * u_; }

    // True when the ringing amplitude (in volts at the op-amp output) is
    // certainly below `volts`: a u^2 + b w^2 is the oscillation energy.
    bool quieterThan(double a, double b, double volts) const noexcept {
        return a * u_ * u_ + b * w_ * w_ < a * volts * volts;
    }

private:
    double T_ = 1.0 / 48000.0;
    double sw_ = 0.0, su_ = 0.0;
    double w_ = 0.0, u_ = 0.0;
};

// Two anti-parallel diodes in series with a resistor, as a smooth explicit
// law (no per-sample solver):
//
//   i(v) = (w/R) * [ softplus((v - vk)/w) - softplus((-v - vk)/w) ]
//
// For |v| << vk this is the Shockley pair 2 Is sinh(v/w) with
// Is = (w/R) exp(-vk/w); for |v| >> vk it is the series resistor (v - vk)/R.
//
// The voices need i(v)/v every sample, so it is tabulated over the knee (the
// table is rebuilt only when a part value changes) and exact beyond it.
class DiodePairKnee {
public:
    // slopeVolts: n*Vt of the junction. saturationAmps: Is. seriesOhms: R.
    // Rebuilding the table costs a few microseconds, too much to do on every
    // parameter update while a control is automated, so a series resistance
    // within `seriesTolerance` (relative) of the tabulated one keeps the table.
    void set(double slopeVolts, double saturationAmps, double seriesOhms,
             double seriesTolerance = 0.0) noexcept {
        if (slopeVolts == w_ && saturationAmps == is_ &&
            std::abs(seriesOhms - r_) <= seriesTolerance * r_)
            return;
        w_ = slopeVolts;
        is_ = saturationAmps;
        r_ = seriesOhms;
        invR_ = 1.0 / seriesOhms;
        vk_ = w_ * std::log(std::max(w_ * invR_ / saturationAmps, 1.0 + 1.0e-9));
        k_ = std::exp(-2.0 * vk_ / w_);
        g0_ = 2.0 * invR_ / (1.0 + std::exp(vk_ / w_));
        vMax_ = vk_ + 34.0 * w_;
        invStep_ = static_cast<double>(kSize) / vMax_;
        for (int i = 0; i <= kSize; ++i) table_[static_cast<std::size_t>(i)] = exact(static_cast<double>(i) / invStep_);
    }
    // i(v)/v: the conductance the rest of the network sees at voltage v.
    double secantConductance(double v) const noexcept {
        const double av = std::abs(v);
        if (av >= vMax_) return (av - vk_) * invR_ / av;  // the series resistor alone
        const double p = av * invStep_;
        const auto i = static_cast<std::size_t>(p);
        const double a = table_[i];
        return a + (table_[i + 1] - a) * (p - static_cast<double>(i));
    }

private:
    static constexpr int kSize = 256;

    double exact(double av) const noexcept {
        if (av < 1.0e-7) return g0_;
        const double z = (av - vk_) / w_;
        if (z >= 34.0) return (av - vk_) * invR_ / av;
        const double e = std::exp(z);
        return w_ * invR_ * std::log((1.0 + e) / (1.0 + k_ / e)) / av;
    }

    double w_ = 0.0, is_ = 0.0, r_ = 0.0;
    double vk_ = 0.1, invR_ = 1.0e-3, k_ = 0.0, g0_ = 0.0;
    double vMax_ = 1.0, invStep_ = 256.0;
    std::array<double, kSize + 2> table_{};
};

// Soft-knee rectifier without transcendental functions: 0 at d -> -inf, d at
// d -> +inf, knee width `knee`.
inline double softKnee(double d, double knee) noexcept {
    return 0.5 * (d + std::sqrt(d * d + knee * knee));
}

}  // namespace mw808::dsp

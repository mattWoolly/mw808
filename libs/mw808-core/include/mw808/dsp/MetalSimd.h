// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Two-lane (SSE2-width) versions of the filters the metallic voices run at
// their oversampled rate, so that parallel branches of one voice (the
// cymbal's two band-passes and three bands, the two hi-hat output chains)
// cost about one branch. Each lane does exactly the arithmetic of the scalar
// dsp::OnePole / dsp::Svf / dsp::MatchedLowpass, in the same order, so a lane
// is bit-identical to the scalar filter it replaces.
//
// Plain IEEE double arithmetic through the GCC/Clang vector extension: no
// intrinsics, no fast-math, deterministic. Also: e^x and ln x as short
// polynomials (no tables, so they vectorise and stay out of the cache).

#pragma once

#include <bit>
#include <cstdint>

#include "mw808/dsp/Math.h"

namespace mw808::dsp {

using v2d = double __attribute__((vector_size(16)));
using v2u = std::uint64_t __attribute__((vector_size(16)));

inline v2d splat(double x) noexcept { return v2d{x, x}; }
inline v2d vmin(v2d a, v2d b) noexcept { return a < b ? a : b; }
inline v2d vmax(v2d a, v2d b) noexcept { return a > b ? a : b; }

// Vector constants are spelled out as {c, c}: a scalar operand would be
// re-broadcast on every use.
#define MW808_V2(c) (::mw808::dsp::v2d{(c), (c)})

// e^x, |x| <= 700, relative error < 2.1e-7: 2^k by exponent arithmetic times
// 1 + f q(f), a degree-5 fit of 2^f on [-1/2, 1/2] that is exact at f = 0
// (so e^0 = 1 exactly and a quiescent swing VCA sits exactly at rest).
inline v2d fastExp(v2d x) noexcept {
    constexpr v2d magic = MW808_V2(6755399441055744.0);  // 1.5 * 2^52: rounds to an integer
    const v2d t = x * MW808_V2(1.4426950408889634);
    const v2d shifted = t + magic;
    const v2d f = t - (shifted - magic);
    v2d q = MW808_V2(0.0013390872133083975);
    q = q * f + MW808_V2(0.009666378523936642);
    q = q * f + MW808_V2(0.05550357095287175);
    q = q * f + MW808_V2(0.24022348880562866);
    q = q * f + MW808_V2(0.6931471880312667);
    const v2d p = MW808_V2(1.0) + q * f;
    const v2u k = std::bit_cast<v2u>(shifted) - std::bit_cast<v2u>(magic);
    return std::bit_cast<v2d>(std::bit_cast<v2u>(p) + (k << 52));
}
inline double fastExp(double x) noexcept { return fastExp(splat(x))[0]; }

// ln x for 2^-1000 < x < 2^1000, absolute error < 6e-8: the exponent from the
// bits (mantissa reduced to [sqrt(1/2), sqrt(2))) plus a degree-8 fit of ln(1+u).
inline v2d fastLog(v2d x) noexcept {
    constexpr v2u sqrtHalf = {0x3fe6a09e667f3bcdULL, 0x3fe6a09e667f3bcdULL};
    constexpr v2u bias = {1024ULL << 52, 1024ULL << 52};
    constexpr v2u twoPow52 = {0x4330000000000000ULL, 0x4330000000000000ULL};
    const v2u bits = std::bit_cast<v2u>(x);
    const v2u kb = (bits - sqrtHalf + bias) >> 52;              // exponent + 1024
    const v2d m = std::bit_cast<v2d>(bits - (kb << 52) + bias);  // in [sqrt(1/2), sqrt(2))
    const v2d k = std::bit_cast<v2d>(kb | twoPow52) - MW808_V2(4503599627371520.0);  // - (2^52 + 1024)
    const v2d u = m - MW808_V2(1.0);
    v2d p = MW808_V2(-0.10134178870428881);
    p = p * u + MW808_V2(0.16234381447618393);
    p = p * u + MW808_V2(-0.17247050722530874);
    p = p * u + MW808_V2(0.19898544930576012);
    p = p * u + MW808_V2(-0.24970067417066447);
    p = p * u + MW808_V2(0.3333511467362212);
    p = p * u + MW808_V2(-0.5000036317004114);
    p = p * u + MW808_V2(0.9999999425207805);
    return k * MW808_V2(0.6931471805599453) + p * u;
}
inline double fastLog(double x) noexcept { return fastLog(splat(x))[0]; }

// Two dsp::OnePole sections.
class OnePole2 {
public:
    void setCutoff(int lane, double freqHz, double sampleRate) noexcept {
        const double g = prewarp(freqHz, sampleRate);
        G_[lane] = g / (1.0 + g);
    }
    void reset() noexcept { s_ = splat(0.0); }
    v2d lowpass(v2d x) noexcept {
        const v2d v = (x - s_) * G_;
        const v2d y = v + s_;
        s_ = y + v;
        return y;
    }
    v2d highpass(v2d x) noexcept { return x - lowpass(x); }

private:
    v2d G_ = splat(0.0);
    v2d s_ = splat(0.0);
};

// Two dsp::Svf sections.
class Svf2 {
public:
    void setFreqQ(int lane, double freqHz, double q, double sampleRate) noexcept {
        setG(lane, prewarp(freqHz, sampleRate), 1.0 / std::max(q, 1.0e-6));
    }
    void setG(int lane, double g, double k) noexcept {
        k_[lane] = k;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        a1_[lane] = a1;
        a2_[lane] = g * a1;
        a3_[lane] = g * (g * a1);
    }
    void reset() noexcept { ic1_ = ic2_ = lp_ = bp_ = hp_ = splat(0.0); }
    void tick(v2d x) noexcept {
        const v2d v3 = x - ic2_;
        const v2d v1 = a1_ * ic1_ + a2_ * v3;
        const v2d v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = MW808_V2(2.0) * v1 - ic1_;
        ic2_ = MW808_V2(2.0) * v2 - ic2_;
        bp_ = v1;
        lp_ = v2;
        hp_ = x - k_ * v1 - v2;
    }
    v2d lowpass() const noexcept { return lp_; }
    v2d bandpassUnity() const noexcept { return k_ * bp_; }
    v2d highpass() const noexcept { return hp_; }

private:
    v2d k_ = splat(1.0), a1_ = splat(0.0), a2_ = splat(0.0), a3_ = splat(0.0);
    v2d ic1_ = splat(0.0), ic2_ = splat(0.0), lp_ = splat(0.0), bp_ = splat(0.0), hp_ = splat(0.0);
};

// Two dsp::MatchedLowpass sections.
class MatchedLowpass2 {
public:
    void setCutoff(int lane, double freqHz, double sampleRate) noexcept {
        const double f = clamp(freqHz, 1.0, 20.0 * sampleRate);
        const double p = std::exp(-kTwoPi * f / sampleRate);
        const double nyq = 0.5 * sampleRate / f;
        const double gN = 1.0 / std::sqrt(1.0 + nyq * nyq);
        b0_[lane] = 0.5 * ((1.0 - p) + gN * (1.0 + p));
        b1_[lane] = 0.5 * ((1.0 - p) - gN * (1.0 + p));
        a1_[lane] = p;
    }
    void reset() noexcept { x1_ = y1_ = splat(0.0); }
    v2d tick(v2d x) noexcept {
        const v2d y = b0_ * x + b1_ * x1_ + a1_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    v2d b0_ = splat(1.0), b1_ = splat(0.0), a1_ = splat(0.0);
    v2d x1_ = splat(0.0), y1_ = splat(0.0);
};

}  // namespace mw808::dsp

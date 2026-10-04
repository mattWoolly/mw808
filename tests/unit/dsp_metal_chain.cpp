// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The swing-VCA building blocks and the small helpers of the metallic voices,
// plus the aliasing of the oscillator -> band-pass -> VCA -> high-pass chain.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>
#include <vector>

#include "MetalTestUtil.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Halfband.h"
#include "mw808/dsp/MetalCommon.h"
#include "mw808/dsp/MetalOscBank.h"
#include "mw808/dsp/MetalSimd.h"
#include "mw808/dsp/SwingVca.h"

using namespace mw808::dsp;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
std::unique_ptr<SwingTables> tables() {
    auto t = std::make_unique<SwingTables>();
    t->buildCymbalStyle();
    return t;
}
}  // namespace

TEST_CASE("Swing helpers: fast exp and log are accurate, range gates at low envelope", "[metal][vca]") {
    const auto t = tables();
    for (double x = -40.0; x <= 40.0; x += 0.001337) REQUIRE_THAT(fastExp(x), WithinRel(std::exp(x), 2.2e-7));
    REQUIRE(fastExp(0.0) == 1.0);  // a quiescent clamp sits exactly at rest
    for (double x = 1.0e-30; x < 1.0e30; x *= 1.0137) REQUIRE_THAT(fastLog(x), WithinAbs(std::log(x), 7.0e-8));
    REQUIRE(fastLog(1.0) == 0.0);
    // both lanes are evaluated independently
    const v2d e = fastExp(v2d{-3.0, 5.0}), l = fastLog(v2d{0.25, 3.0e7});
    REQUIRE_THAT(e[0], WithinRel(std::exp(-3.0), 2.2e-7));
    REQUIRE_THAT(e[1], WithinRel(std::exp(5.0), 2.2e-7));
    REQUIRE_THAT(l[0], WithinAbs(std::log(0.25), 7.0e-8));
    REQUIRE_THAT(l[1], WithinAbs(std::log(3.0e7), 7.0e-8));
    // range D(Venv): zero below the gate, ~Venv - 0.57 V when the envelope is high, monotonic
    REQUIRE(t->range(0.0) == 0.0);
    REQUIRE(t->range(0.15) == 0.0);
    REQUIRE_THAT(t->range(10.0), WithinAbs(10.0 - 0.566, 0.01));
    double last = 0.0;
    for (double v = 0.0; v <= 16.0; v += 0.01) {
        REQUIRE(t->range(v) >= last);
        last = t->range(v);
    }
    // published lower-edge fit: 0.44 V at 1 V envelope, 0.566 V asymptote
    REQUIRE_THAT(swingLowerEdgeCymbal(1.0), WithinAbs(0.441, 0.005));
    REQUIRE_THAT(swingLowerEdgeCymbal(12.0), WithinAbs(0.566, 0.001));
    // cowbell edges with the corrected signs: a slowly rising floor near 0.4-0.5 V
    REQUIRE_THAT(swingLowerEdgeCowbell(1.0), WithinAbs(0.36, 0.01));
    REQUIRE_THAT(swingLowerEdgeCowbell(5.0), WithinAbs(0.46, 0.01));
    REQUIRE_THAT(swingLowerEdgeCowbell(12.0), WithinAbs(0.50, 0.01));
    REQUIRE_THAT(swingUpperEdgeCowbell(5.0), WithinAbs(5.024, 0.001));
}

TEST_CASE("Base clamp conserves charge: mean drive is exactly the bias", "[metal][vca]") {
    for (double rate : {48000.0, 96000.0, 384000.0}) {
        for (double amp : {0.005, 0.05, 0.3}) {
            SwingBaseClamp clamp;
            clamp.prepare(rate, 1.0);
            clamp.reset();
            double sum = 0.0, peak = 0.0;
            const int n = static_cast<int>(rate);
            for (int i = 0; i < 2 * n; ++i) {
                const double drive = clamp.tick(amp * std::sin(kTwoPi * 7100.0 * i / rate));
                REQUIRE(drive >= 0.0);
                if (i >= n) {
                    sum += drive;
                    peak = std::max(peak, drive);
                }
            }
            INFO("rate " << rate << " amp " << amp << " peak drive " << peak);
            REQUIRE_THAT(sum / n, WithinAbs(1.0, 0.01));
            if (amp > 0.1) REQUIRE(peak > 3.0);  // conduction in spikes
        }
    }
    // no input: sits at the quiescent point
    SwingBaseClamp idle;
    idle.prepare(96000.0, 1.0);
    idle.reset();
    for (int i = 0; i < 100; ++i) REQUIRE_THAT(idle.tick(0.0), WithinAbs(1.0, 1e-9));
}

TEST_CASE("Collector stays between the envelope and its lower edge, and is rate independent", "[metal][vca]") {
    const auto t = tables();
    double means[3] = {};
    const double rates[3] = {96000.0, 192000.0, 768000.0};
    for (int r = 0; r < 3; ++r) {
        const double rate = rates[r];
        SwingBaseClamp clamp;
        SwingCollector col;
        clamp.prepare(rate, 1.0);
        col.prepare(rate, 39.0e3, 3.0e-9, 1.0);
        clamp.reset();
        col.reset();
        SwingSupply supply;
        supply.env = 6.0;
        supply.range = t->range(6.0);
        const int n = static_cast<int>(rate);
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            const double in = 0.1 * std::sin(kTwoPi * 7100.0 * i / rate) + 0.04 * std::sin(kTwoPi * 3300.0 * i / rate);
            const double vc = col.tick(clamp.tick(in), supply);
            REQUIRE(vc <= supply.env + 1e-12);
            REQUIRE(vc >= supply.env - supply.range - 1e-12);
            if (i > n / 2) sum += vc;
        }
        means[r] = sum / (n / 2);
    }
    INFO("collector mean volts at 96k / 192k / 768k: " << means[0] << " " << means[1] << " " << means[2]);
    REQUIRE_THAT(means[0], WithinRel(means[2], 0.06));
    REQUIRE_THAT(means[1], WithinRel(means[2], 0.02));
    // closed gate: the collector simply follows the envelope
    SwingCollector shut;
    shut.prepare(96000.0, 33.0e3, 3.0e-9, 1.0);
    shut.reset();
    SwingSupply none;
    none.env = 0.1;
    for (int i = 0; i < 10; ++i) REQUIRE(shut.tick(50.0, none) == 0.1);
}

// Regression: the collector used to be updated as "recover half a sample,
// conduction impulse, recover half a sample", so even a transistor held in
// saturation by a steady current let the node creep a fraction of a sample's
// recovery off the lower edge (a level loss of 2-3 dB at a 44.1 kHz internal
// rate). With the current held over the sample, it stays on the edge.
TEST_CASE("Collector held in saturation sits exactly on the lower edge", "[metal][vca]") {
    const auto t = tables();
    for (double rate : {44100.0, 96000.0, 768000.0}) {
        SwingCollector col;
        col.prepare(rate, 33.0e3, 3.0e-9, 1.0);
        col.reset();
        SwingSupply supply;
        supply.env = 6.0;
        supply.range = t->range(6.0);
        double vc = 0.0;
        for (int i = 0; i < 200; ++i) vc = col.tick(1.0, supply);  // quiescent base current: K = 47 V >> D
        REQUIRE(vc == supply.env - supply.range);
        // released: recovers towards the envelope with tau = Rc C = 99 us, whatever the rate
        const int n = static_cast<int>(std::lround(99.0e-6 * rate));
        for (int i = 0; i < n; ++i) vc = col.tick(0.0, supply);
        REQUIRE_THAT((supply.env - vc) / supply.range, WithinRel(std::exp(-n / (99.0e-6 * rate)), 1e-9));
    }
}

TEST_CASE("Two-lane blocks are bit-identical to the scalar ones; the pipelined clamp is the clamp, delayed", "[metal][vca]") {
    const auto t = tables();
    const double rate = 96000.0;
    Svf a, b;
    Svf2 ab;
    a.setFreqQ(7117.0, 6.05, rate);
    b.setFreqQ(2498.0, 0.965, rate);
    ab.setFreqQ(0, 7117.0, 6.05, rate);
    ab.setFreqQ(1, 2498.0, 0.965, rate);
    OnePole p, q;
    OnePole2 pq;
    p.setCutoff(1461.5, rate);
    q.setCutoff(7234.3, rate);
    pq.setCutoff(0, 1461.5, rate);
    pq.setCutoff(1, 7234.3, rate);
    MatchedLowpass m, n;
    MatchedLowpass2 mn;
    m.setCutoff(8770.0, rate);
    n.setCutoff(8683.0, rate);
    mn.setCutoff(0, 8770.0, rate);
    mn.setCutoff(1, 8683.0, rate);
    SwingCollector c0, c1;
    SwingSupply s0, s1;
    SwingCollector2 c01;
    c0.prepare(rate, 22.0e3, 1.5e-9, 1.0);
    c1.prepare(rate, 16.5e3, 3.0e-9, 0.8);
    c01.prepare(0, rate, 22.0e3, 1.5e-9, 1.0);
    c01.prepare(1, rate, 16.5e3, 3.0e-9, 0.8);
    SwingBaseClamp2 direct, piped;
    for (int lane : {0, 1}) {
        direct.prepare(lane, rate, lane == 0 ? 1.0 : 0.7);
        piped.prepare(lane, rate, lane == 0 ? 1.0 : 0.7);
    }
    direct.reset();
    piped.reset();
    v2d lastDirect = splat(1.0);
    for (int i = 0; i < 20000; ++i) {
        if (i % 8 == 0) {  // envelopes moving at control rate
            const double e0 = 8.0 * std::exp(-i / 9000.0), e1 = 5.0 * std::exp(-i / 4000.0);
            s0.aim(e0, t->range, 0.125);
            s1.aim(e1, t->range, 0.125);
            c01.aim(0, e0, t->range, 0.125);
            c01.aim(1, e1, t->range, 0.125);
        }
        const double x0 = 0.3 * std::sin(0.4 * i) + 0.1 * std::sin(0.031 * i), x1 = 0.2 * std::sin(0.23 * i + 1.0);
        a.tick(x0);
        b.tick(x1);
        ab.tick(v2d{x0, x1});
        REQUIRE(ab.lowpass()[0] == a.lowpass());
        REQUIRE(ab.highpass()[1] == b.highpass());
        REQUIRE(ab.bandpassUnity()[0] == a.bandpassUnity());
        const v2d pqLow = pq.lowpass(v2d{x0, x1});
        REQUIRE(pqLow[0] == p.lowpass(x0));
        REQUIRE(pqLow[1] == q.lowpass(x1));
        const v2d mnOut = mn.tick(v2d{x0, x1});
        REQUIRE(mnOut[0] == m.tick(x0));
        REQUIRE(mnOut[1] == n.tick(x1));
        const v2d d = direct.tick(v2d{x0, x1});
        const v2d dp = piped.tickPipelined(v2d{x0, x1});
        if (i > 0) {
            REQUIRE(dp[0] == lastDirect[0]);
            REQUIRE(dp[1] == lastDirect[1]);
        }
        lastDirect = d;
        s0.step();
        s1.step();
        const v2d c = c01.tick(d);
        REQUIRE(c[0] == c0.tick(d[0], s0));
        REQUIRE(c[1] == c1.tick(d[1], s1));
    }
}

// Regression: the bilinear band-pass prewarped at its centre is ~14 % too
// narrow at 7.1 kHz / 48 kHz, which starved the swing VCAs at low rates.
TEST_CASE("Bandwidth-matched resonator keeps the analog bandwidth and peak gain", "[metal][helpers]") {
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (double f0 : {3435.0, 7117.0}) {
            Svf r;
            const double lpScale = setResonatorMatched(r, f0, 6.05, rate);
            // steady-state magnitude of k*bp and of the scaled low-pass, by a single-bin DFT
            auto gain = [&](double f, bool lowpass) {
                r.reset();
                const int n = static_cast<int>(rate);
                std::complex<double> acc = 0.0;
                for (int i = 0; i < n; ++i) {
                    const double ph = kTwoPi * f * i / rate;
                    r.tick(std::sin(ph));
                    const double y = lowpass ? r.lowpass() * lpScale : r.bandpassUnity();
                    if (i >= n / 2) acc += y * std::complex<double>(std::sin(ph), std::cos(ph));
                }
                return 2.0 * std::abs(acc) / (n / 2);
            };
            // analog: unity peak, -3 dB at f0 (sqrt(1 + 1/4Q^2) +- 1/2Q)
            const double half = 1.0 / (2.0 * 6.05);
            const double fLo = f0 * (std::sqrt(1.0 + half * half) - half), fHi = f0 * (std::sqrt(1.0 + half * half) + half);
            INFO("rate " << rate << " f0 " << f0);
            REQUIRE_THAT(gain(f0, false), WithinAbs(1.0, 0.01));
            REQUIRE_THAT(gain(fLo, false), WithinAbs(std::sqrt(0.5), 0.02));
            REQUIRE_THAT(gain(fHi, false), WithinAbs(std::sqrt(0.5), 0.03));
            REQUIRE_THAT(gain(f0, true), WithinRel(6.05, 0.02));  // low-pass peak gain Q
        }
    }
}

TEST_CASE("Metal trigger mapping, junction drops and knob scales", "[metal][helpers]") {
    REQUIRE_THAT(metalTriggerVolts(4.0), WithinAbs(7.0, 1e-12));
    REQUIRE_THAT(metalTriggerVolts(14.0), WithinAbs(14.0, 1e-12));
    REQUIRE(metalTriggerVolts(0.0) == 0.0);
    double last = -1.0, lastCharge = -1.0;
    for (double v = 0.0; v <= 14.0; v += 0.05) {
        REQUIRE(metalTriggerVolts(v) > last);
        last = metalTriggerVolts(v);
        const double charge = metalChargeVolts(v, 1.3, 0.8);
        REQUIRE(charge > lastCharge);
        lastCharge = charge;
    }
    REQUIRE(softDrop(0.0, 0.6) == 0.0);
    REQUIRE_THAT(softDrop(10.0, 0.6), WithinAbs(9.4, 0.05));
    REQUIRE_THAT(decayKnobScale(0.5), WithinAbs(1.0, 1e-12));
    REQUIRE_THAT(decayKnobScale(0.0), WithinAbs(0.25, 1e-12));
    REQUIRE_THAT(decayKnobScale(1.0), WithinAbs(4.0, 1e-12));
}

TEST_CASE("Matched low-pass follows the analog pole up to Nyquist", "[metal][helpers]") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        MatchedLowpass lp;
        lp.setCutoff(8770.0, fs);
        for (double f : {100.0, 4000.0, 8770.0, 15000.0, 20000.0}) {
            lp.reset();
            double peak = 0.0;
            const int n = static_cast<int>(fs / 4);
            for (int i = 0; i < n; ++i) {
                const double y = lp.tick(std::sin(kTwoPi * f * i / fs));
                if (i > n / 2) peak = std::max(peak, std::abs(y));
            }
            const double analog = 1.0 / std::sqrt(1.0 + (f / 8770.0) * (f / 8770.0));
            INFO("fs " << fs << " f " << f);
            REQUIRE_THAT(20.0 * std::log10(peak / analog), WithinAbs(0.0, 1.2));
        }
    }
}

TEST_CASE("polyBLEP droop compensation flattens the oscillator spectrum", "[metal][helpers]") {
    // response of the compensator times the polyBLEP kernel (sinc^2) is flat to fs/8
    for (double x : {0.02, 0.05, 0.1, 0.125}) {
        const double w = kTwoPi * x;
        const double sinc = std::sin(kPi * x) / (kPi * x);
        const double comp = 7.0 / 6.0 - (1.0 / 6.0) * std::cos(w);
        REQUIRE_THAT(20.0 * std::log10(comp * sinc * sinc), WithinAbs(0.0, 0.06));
    }
    BlepDroopCompensator c;
    double y = 0.0;
    for (int i = 0; i < 10; ++i) y = c.tick(1.0);
    REQUIRE_THAT(y, WithinAbs(1.0, 1e-12));  // unity at DC
}

// One oscillator (~830 Hz) through the cymbal's high-band chain. Its exact
// output contains only multiples of that frequency; everything else is aliasing.
TEST_CASE("Chain aliasing at 48 kHz: oscillator, band-pass, swing VCA, high-pass", "[metal][alias]") {
    const auto t = tables();
    const double fs = 48000.0;
    const double kOscHz = 829.84;
    const std::size_t n = 1U << 17;
    struct Band { double lo, hi; };
    const Band bands[3] = {{2000.0, 5000.0}, {5000.0, 9000.0}, {9000.0, 16000.0}};
    double asr[3][3] = {};
    const int factors[3] = {1, 2, 4};
    for (int f = 0; f < 3; ++f) {
        const int os = factors[f];
        const double rate = fs * os;
        MetalOscBank bank;
        bank.prepare(rate, 0U, 0.0f);
        // 800 Hz divides 48 kHz, so its aliases would land on its own harmonics;
        // detune it so they do not.
        bank.setTune(kOscHz / 800.0);
        BlepDroopCompensator droop;
        OnePole inHp, hpA;
        Svf bp, hp;
        inHp.setCutoff(7234.3, rate);
        bp.setFreqQ(7117.0, 6.05, rate);
        hpA.setCutoff(7092.0, rate);
        hp.setFreqQ(10400.0, 3.0, rate);
        SwingBaseClamp clamp;
        SwingCollector col;
        clamp.prepare(rate, 1.0);
        col.prepare(rate, 39.0e3, 3.0e-9, 1.0);
        clamp.reset();
        col.reset();
        SwingSupply supply;
        supply.env = 6.0;
        supply.range = t->range(6.0);
        MetalDecimator dec;
        dec.prepare(os);
        auto tick = [&]() {
            double lo = 0.0, hi = 0.0;
            bank.tickCowbell(lo, hi);
            // one oscillator at the r.m.s. level of the six-oscillator mix
            bp.tick(inHp.highpass(droop.tick(hi) * (2.5 / 126.0) * 2.45));
            const double drive = clamp.tick(-(82.0 / 22.0) * (bp.lowpass() + bp.bandpassUnity()));
            hp.tick(hpA.highpass(col.tick(drive, supply)));
            return hp.highpass();
        };
        std::vector<std::complex<double>> buf(n);
        for (int warm = 0; warm < 20000; ++warm) {
            double hiRate[4];
            for (int k = 0; k < os; ++k) hiRate[k] = tick();
            (void)dec.process(hiRate);
        }
        for (std::size_t i = 0; i < n; ++i) {
            double hiRate[4];
            for (int k = 0; k < os; ++k) hiRate[k] = tick();
            const double x = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n);
            const double w = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
            buf[i] = dec.process(hiRate) * w;
        }
        metaltest::fft(buf);
        const double binHz = fs / static_cast<double>(n);
        std::vector<char> harmonic(n / 2 + 1, 0);
        for (int h = 1; h * kOscHz < fs / 2; ++h) {
            const auto centre = static_cast<std::ptrdiff_t>(std::lround(h * kOscHz / binHz));
            for (std::ptrdiff_t k = centre - 6; k <= centre + 6; ++k)
                if (k >= 0 && k <= static_cast<std::ptrdiff_t>(n / 2)) harmonic[static_cast<std::size_t>(k)] = 1;
        }
        for (int b = 0; b < 3; ++b) {
            double sig = 0.0, alias = 0.0;
            for (auto k = static_cast<std::size_t>(bands[b].lo / binHz); k < static_cast<std::size_t>(bands[b].hi / binHz); ++k)
                (harmonic[k] ? sig : alias) += std::norm(buf[k]);
            asr[f][b] = 10.0 * std::log10(alias / sig);
        }
        std::printf("[chain aliasing] 48 kHz, oversample %d: alias/signal %.1f dB (2-5 kHz), %.1f dB (5-9 kHz), %.1f dB (9-16 kHz)\n", os,
                    asr[f][0], asr[f][1], asr[f][2]);
    }
    for (int b = 1; b < 3; ++b) {
        REQUIRE(asr[1][b] < asr[0][b] - 3.0);  // 2x clearly better than polyBLEP alone
        REQUIRE(asr[2][b] < asr[1][b] - 3.0);  // 4x better again
    }
    REQUIRE(asr[1][1] < -30.0);
    REQUIRE(asr[1][2] < -20.0);
}

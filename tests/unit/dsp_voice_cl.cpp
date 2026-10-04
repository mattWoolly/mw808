// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "BridgedTestUtil.h"
#include "mw808/voices/Claves.h"

using namespace bridged_test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using mw808::voices::Claves;

namespace {

const auto prep = [](Claves& v, const VoiceSetup& s) { v.prepare(s); };

constexpr double kPitch = 2500.0;    // service manual
constexpr double kMinus20 = 25.0e-3; // service manual
constexpr double kMuteHold = 33.0e-3;

VoiceParams with(float tune = 0.0f, float decay = 0.5f) {
    VoiceParams p;
    p.tune = tune;
    p.decay = decay;
    return p;
}

std::vector<float> hit(double fs, const VoiceParams& p, float volts = 4.0f, double seconds = 0.5) {
    return renderHit<Claves>(prep, fs, p, volts, seconds);
}

// Envelope time constant from the peak level in two windows.
double tauOf(const std::vector<float>& x, double fs, double t0, double t1, double window) {
    auto peakIn = [&](double a) {
        double p = 0.0;
        for (std::size_t i = static_cast<std::size_t>(a * fs); i < static_cast<std::size_t>((a + window) * fs); ++i)
            p = std::max(p, static_cast<double>(std::abs(x[i])));
        return p;
    };
    return (t1 - t0) / std::log(peakIn(t0) / peakIn(t1));
}

double lastAudible(const std::vector<float>& x, double fs, double relative) {
    const double thr = relative * peakAbs(x);
    std::size_t last = 0;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::abs(x[i]) > thr) last = i;
    return static_cast<double>(last) / fs;
}

}  // namespace

TEST_CASE("CL: 2.5 kHz at every sample rate", "[cl][pitch]") {
    for (double fs : kRates) {
        const auto x = hit(fs, with());
        const double f = zeroCrossFreq(x, fs, 0.002, 0.030);
        INFO("fs " << fs << " pitch " << f);
        CHECK_THAT(f, WithinRel(kPitch, 0.003));
    }
}

TEST_CASE("CL: 25 ms to -20 dB (Q about 85), cut off by the mute gate, at every sample rate", "[cl][decay]") {
    for (double fs : kRates) {
        const auto x = hit(fs, with());
        const double tau = tauOf(x, fs, 0.002, 0.026, 0.0008);
        const double t20 = timeToMinus20dB(x, fs, 0.0004);
        const double q = mw808::dsp::kPi * kPitch * tau;
        INFO("fs " << fs << " tau " << tau * 1e3 << " ms, Q " << q << ", -20 dB at " << t20 * 1e3 << " ms, ends " << lastAudible(x, fs, 1e-3) * 1e3);
        CHECK_THAT(tau, WithinRel(kMinus20 / std::log(10.0), 0.02));
        CHECK_THAT(t20, WithinRel(kMinus20, 0.05));
        CHECK_THAT(q, WithinRel(85.3, 0.03));
        // The JFET mute closes about 33 ms after the trigger: the tail stops
        // near -27 dB instead of ringing on.
        CHECK_THAT(lastAudible(x, fs, 1e-3), WithinAbs(kMuteHold + 0.0015, 0.0015));
        CHECK(peakAbs(std::vector<float>(x.begin() + static_cast<std::ptrdiff_t>(0.040 * fs), x.end())) < 1e-5);
    }
}

TEST_CASE("CL: levels match the service-manual table and rise monotonically with volts", "[cl][level]") {
    for (double fs : kRates) {
        const auto normal = hit(fs, with(), 4.0f), accent = hit(fs, with(), 14.0f);
        INFO("fs " << fs << " normal " << vpp(normal) << " Vpp, accent " << vpp(accent) << " Vpp");
        CHECK_THAT(vpp(normal), WithinRel(2.5, 0.05));
        CHECK_THAT(vpp(accent), WithinRel(8.0, 0.05));
        CHECK_THAT(peakAbs(normal), WithinRel(0.25, 0.08));
        CHECK_THAT(peakAbs(accent), WithinRel(0.8, 0.08));
    }
    checkMonotonicLevel<Claves>(prep, 48000.0, with(), 0.3);
}

TEST_CASE("CL: TUNE transposes, DECAY scales the ring", "[cl][params]") {
    const double fs = 48000.0;
    CHECK_THAT(zeroCrossFreq(hit(fs, with(12.0f)), fs, 0.002, 0.030), WithinRel(2.0 * kPitch, 0.003));
    CHECK_THAT(zeroCrossFreq(hit(fs, with(-12.0f)), fs, 0.002, 0.030), WithinRel(0.5 * kPitch, 0.003));
    CHECK_THAT(zeroCrossFreq(hit(fs, with(7.0f)), fs, 0.002, 0.030), WithinRel(kPitch * std::exp2(7.0 / 12.0), 0.003));
    // Tuning leaves the ring time alone.
    const double tau = tauOf(hit(fs, with()), fs, 0.002, 0.026, 0.0008);
    CHECK_THAT(tauOf(hit(fs, with(12.0f)), fs, 0.002, 0.026, 0.0008), WithinRel(tau, 0.03));
    CHECK_THAT(tauOf(hit(fs, with(-12.0f)), fs, 0.002, 0.026, 0.0016), WithinRel(tau, 0.03));
    CHECK_THAT(vpp(hit(fs, with(12.0f))), WithinRel(vpp(hit(fs, with())), 0.15));
    // DECAY: x0.25 .. x4; the mute hold stretches with long settings.
    CHECK_THAT(tauOf(hit(fs, with(0.0f, 0.0f)), fs, 0.001, 0.007, 0.0008), WithinRel(0.25 * tau, 0.04));
    const auto lng = hit(fs, with(0.0f, 1.0f));
    CHECK_THAT(tauOf(lng, fs, 0.002, 0.090, 0.0008), WithinRel(4.0 * tau, 0.03));
    CHECK_THAT(lastAudible(lng, fs, 1e-3), WithinAbs(4.0 * kMuteHold + 0.0015, 0.002));
    double prev = 0.0;
    for (float d = 0.0f; d <= 1.0f; d += 0.125f) {
        const double e = rms(hit(fs, with(0.0f, d)), fs, 0.0, 0.5);
        CHECK(e > prev);
        prev = e;
    }
}

TEST_CASE("CL: retrigger adds to the ringing state and never clicks", "[cl][retrigger]") {
    const double fs = 192000.0;  // fine time steps: the hits can be placed on a chosen phase
    const double period = 1.0 / kPitch;
    const auto one = hit(fs, with(), 4.0f, 0.2);
    auto second = [&](double gap) {
        return render<Claves>(prep, setupAt(fs), with(), {{0.0, Hit::normal()}, {gap, Hit::normal()}}, 0.2);
    };
    // The resonator is linear, so a second hit superposes on the ring that is
    // still there: in phase it adds, half a period later it cancels. A voice
    // that reset on trigger would sound the same both times.
    const auto inPhase = second(10.0 * period), antiPhase = second(10.5 * period);
    const double a = rms(inPhase, fs, 0.006, 0.012), b = rms(antiPhase, fs, 0.006, 0.012), c = rms(one, fs, 0.006, 0.012);
    INFO("rms after the second hit: in phase " << a << ", anti-phase " << b << ", single hit " << c);
    CHECK(a > 2.2 * c);   // 1 + exp(4 ms / tau)
    CHECK(b < 0.55 * c);  // exp(4.2 ms / tau) - 1
    const std::size_t shift = static_cast<std::size_t>(std::lround(10.0 * period * fs));
    double diffSum = 0.0, diffReset = 0.0;
    for (std::size_t i = shift; i < static_cast<std::size_t>(0.030 * fs); ++i) {
        diffSum = std::max(diffSum, std::abs(static_cast<double>(inPhase[i]) - (static_cast<double>(one[i]) + one[i - shift])));
        diffReset = std::max(diffReset, std::abs(static_cast<double>(inPhase[i]) - one[i - shift]));
    }
    INFO("max diff vs linear sum " << diffSum << ", vs reset-and-replay " << diffReset);
    CHECK(diffSum < 1e-4);
    CHECK(diffReset > 0.5 * peakAbs(one));
    CHECK(maxStep(antiPhase, fs, 10.5 * period, 10.5 * period + 0.001) < 1.3 * maxStep(one, fs, 0.0, 0.001) + 1e-4);
    // Full accent, in phase: the op-amp runs into its rails and the hits no longer add.
    const auto loud = hit(fs, with(), 14.0f, 0.2);
    const auto loud2 = render<Claves>(prep, setupAt(fs), with(), {{0.0, Hit::accented(1.0f)}, {10.0 * period, Hit::accented(1.0f)}}, 0.2);
    CHECK(peakAbs(loud2) < 1.5 * peakAbs(loud));
    // A second hit also re-opens the mute gate.
    const auto late = render<Claves>(prep, setupAt(fs), with(), {{0.0, Hit::normal()}, {0.030, Hit::normal()}}, 0.2);
    CHECK_THAT(lastAudible(late, fs, 1e-3), WithinAbs(0.030 + kMuteHold + 0.0015, 0.0015));
}

TEST_CASE("CL: robustness fuzz", "[cl][fuzz]") {
    std::uint32_t seed = 51;
    for (double fs : kRates) fuzzRobustness<Claves>(prep, fs, seed++, 2.0);
    fuzzRobustness<Claves>(prep, 48000.0, 599, 2.0, 1.0f);
}

TEST_CASE("CL: deterministic; unit variance moves pitch and decay within tolerance", "[cl][determinism]") {
    checkDeterministic<Claves>(prep, 48000.0);
    checkSeedIgnoredAtZeroVariance<Claves>(prep, 48000.0, with());
    const double fs = 96000.0;
    double fMin = 1e9, fMax = 0.0, tMin = 1e9, tMax = 0.0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        const auto x = render<Claves>(prep, setupAt(fs, seed, 1.0f), with(), {{0.0, Hit::normal()}}, 0.2);
        const double f = zeroCrossFreq(x, fs, 0.002, 0.030);
        const double tau = tauOf(x, fs, 0.002, 0.026, 0.0008);
        INFO("seed " << seed << " pitch " << f << " tau " << tau);
        REQUIRE(allFinite(x));
        CHECK(f > kPitch / 1.27);
        CHECK(f < kPitch * 1.27);
        CHECK(tau > 10.86e-3 / 1.25);
        CHECK(tau < 10.86e-3 * 1.25);
        fMin = std::min(fMin, f);
        fMax = std::max(fMax, f);
        tMin = std::min(tMin, tau);
        tMax = std::max(tMax, tau);
    }
    CHECK(fMax / fMin > 1.03);
    CHECK(tMax / tMin > 1.03);
}

TEST_CASE("CL: against the reference recording of a real unit", "[cl][reference]") {
    if (refPath("cl8", "CL.WAV").empty()) SKIP("reference recordings not present");
    Wav w;
    REQUIRE(readWav(refPath("cl8", "CL.WAV"), w));
    const auto m = hit(w.fs, with(), 4.0f, 0.25);
    const double rf = zeroCrossFreq(w.x, w.fs, 0.002, 0.030), mf = zeroCrossFreq(m, w.fs, 0.002, 0.030);
    const double rt = tauOf(w.x, w.fs, 0.003, 0.027, 0.0008), mt = tauOf(m, w.fs, 0.002, 0.026, 0.0008);
    const double re = lastAudible(w.x, w.fs, 0.01), me = lastAudible(m, w.fs, 0.01);
    const double rc = spectrum(w.x, w.fs, 0.0, 0.05).centroid(), mc = spectrum(m, w.fs, 0.0, 0.05).centroid();
    std::printf("\nCL vs reference   pitch Hz %.1f / %.1f   tau ms %.2f / %.2f   mute closes ms %.1f / %.1f   centroid Hz %.0f / %.0f\n", rf, mf,
                rt * 1e3, mt * 1e3, re * 1e3, me * 1e3, rc, mc);
    CHECK_THAT(mf, WithinRel(rf, 0.04));  // the unit sits 3 % below the manual's 2.5 kHz
    CHECK_THAT(mt, WithinRel(rt, 0.12));
    CHECK_THAT(me, WithinAbs(re, 0.003));
    CHECK_THAT(mc, WithinRel(rc, 0.05));
}

TEST_CASE("CL: listening renders", "[.render]") {
    const double fs = 48000.0;
    std::vector<float> all;
    for (float volts : {4.0f, 14.0f, 2.2f}) appendWithGap(all, hit(fs, with(), volts, 0.25), fs);
    for (float tune : {-7.0f, 7.0f}) appendWithGap(all, hit(fs, with(tune), 6.0f, 0.25), fs);
    for (float decay : {0.0f, 1.0f}) appendWithGap(all, hit(fs, with(0.0f, decay), 6.0f, 0.4), fs);
    std::vector<Event> ev;
    for (int i = 0; i < 12; ++i) ev.push_back({0.11 * i + (i % 3 == 2 ? 0.02 : 0.0), Hit{i % 4 == 0 ? 12.0f : 4.0f, 0.0f}});
    appendWithGap(all, render<Claves>(prep, setupAt(fs), with(), ev, 1.6), fs);
    const std::string path = renderDir() + "/cl.wav";
    REQUIRE(writeWav(path, all, fs));
    std::printf("wrote %s\n", path.c_str());
}

TEST_CASE("CL: performance", "[.bench]") {
    const double ns = benchNsPerSample<Claves>(prep, 48000.0, with(0.0f, 1.0f), 0.05);
    std::printf("CL: %.1f ns/sample at 48 kHz (budget 150)\n", ns);
    CHECK(ns < 150.0);
}

// ---------------------------------------------------------------- review fixes

TEST_CASE("CL: non-finite controls cannot poison the voice", "[cl][fuzz]") {
    for (double fs : {44100.0, 192000.0}) checkNonFiniteParams<Claves>(prep, fs);
}

TEST_CASE("CL: sleeps only once inaudible", "[cl][sleep]") {
    for (double fs : {44100.0, 192000.0}) {
        for (float decay : {0.0f, 0.5f, 1.0f}) {
            checkQuietBeforeSleep<Claves>(prep, fs, with(0.0f, decay), 14.0f, 0.001);
            checkQuietBeforeSleep<Claves>(prep, fs, with(-12.0f, decay), 0.5f, 0.002);
        }
    }
}

TEST_CASE("CL: a corrupt unit variance is clamped, not propagated as NaN", "[cl][fuzz]") {
    // Before the fix a NaN or infinite variance made every part NaN and the
    // voice emitted NaN without ever sleeping; variance 10 drove capacitors
    // negative.
    checkBadVariance<Claves>(prep, 48000.0);
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "BridgedTestUtil.h"
#include "mw808/voices/RimShot.h"

using namespace bridged_test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using mw808::voices::RimShot;

namespace {

const auto prep = [](RimShot& v, const VoiceSetup& s) { v.prepare(s); };

constexpr double kHighPitch = 1667.0;  // service manual
constexpr double kLowPitch = 455.0;    // service manual
constexpr double kMinus20 = 10.0e-3;   // service manual

VoiceParams with(float tune = 0.0f, float decay = 0.5f) {
    VoiceParams p;
    p.tune = tune;
    p.decay = decay;
    return p;
}

std::vector<float> hit(double fs, const VoiceParams& p, float volts = 4.0f, double seconds = 0.3) {
    return renderHit<RimShot>(prep, fs, p, volts, seconds);
}

// The swing VCA chops the envelope at the high resonator's pitch; the low
// resonator swings the duty and takes over once the high one has died away.
double highPeak(const std::vector<float>& x, double fs, double scale = 1.0) {
    return peakFreq(x, fs, 1200.0 * scale, 2200.0 * scale, 0.0005 / scale, 0.0045 / scale);
}
double lowPeak(const std::vector<float>& x, double fs, double scale = 1.0) {
    return peakFreq(x, fs, 300.0 * scale, 700.0 * scale, 0.004, 0.014);
}

double lastAudible(const std::vector<float>& x, double fs, double relative) {
    const double thr = relative * peakAbs(x);
    std::size_t last = 0;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::abs(x[i]) > thr) last = i;
    return static_cast<double>(last) / fs;
}

}  // namespace

TEST_CASE("RS: the two resonator pitches show in the output at every sample rate", "[rs][pitch]") {
    double refHigh = 0.0, refLow = 0.0;
    for (double fs : kRates) {
        const auto x = hit(fs, with());
        const double high = highPeak(x, fs), low = lowPeak(x, fs);
        INFO("fs " << fs << " high " << high << " low " << low);
        CHECK_THAT(high, WithinRel(kHighPitch, 0.03));
        // The low partial is a few cycles of a Q 6.7 resonator behind a
        // resonant 590 Hz high-pass: its spectral peak sits above 455 Hz (the
        // reference recording reads 468..495 Hz over the same window).
        CHECK_THAT(low, WithinRel(kLowPitch, 0.20));
        if (fs == kRates[0]) {
            refHigh = high;
            refLow = low;
        }
        CHECK_THAT(high, WithinRel(refHigh, 0.01));
        CHECK_THAT(low, WithinRel(refLow, 0.03));
    }
}

TEST_CASE("RS: about 10 ms to -20 dB, ending abruptly, at every sample rate", "[rs][decay]") {
    double ref = 0.0;
    for (double fs : kRates) {
        const auto x = hit(fs, with());
        const double t20 = timeToMinus20dB(x, fs, 0.002);
        const double end = lastAudible(x, fs, 1e-3);
        INFO("fs " << fs << " -20 dB at " << t20 * 1e3 << " ms, -60 dB at " << end * 1e3 << " ms");
        CHECK_THAT(t20, WithinRel(kMinus20, 0.20));
        // Reference recording: -20 dB at 10.4 ms, -60 dB by 14.8 ms. The
        // envelope falls through the diode drop and the sound stops.
        CHECK(end < 0.022);
        if (fs == kRates[0]) ref = t20;
        CHECK_THAT(t20, WithinRel(ref, 0.06));
    }
}

TEST_CASE("RS: levels match the service-manual table and rise monotonically with volts", "[rs][level]") {
    for (double fs : kRates) {
        const auto normal = hit(fs, with(), 4.0f), accent = hit(fs, with(), 14.0f);
        INFO("fs " << fs << " normal " << vpp(normal) << " Vpp, accent " << vpp(accent) << " Vpp");
        CHECK_THAT(vpp(normal), WithinRel(3.0, 0.08));
        CHECK_THAT(vpp(accent), WithinRel(10.0, 0.08));
    }
    checkMonotonicLevel<RimShot>(prep, 48000.0, with(), 0.2);
}

TEST_CASE("RS: accent lengthens and saturates the burst, not just its level", "[rs][level]") {
    const double fs = 48000.0;
    const auto normal = hit(fs, with(), 4.0f), accent = hit(fs, with(), 14.0f);
    const double tN = timeToMinus20dB(normal, fs, 0.002), tA = timeToMinus20dB(accent, fs, 0.002);
    INFO("-20 dB at " << tN * 1e3 << " ms normal, " << tA * 1e3 << " ms accented");
    CHECK(tA > 1.2 * tN);
    // The VCA is a switch: its output is rich in harmonics of the high partial.
    const auto s = spectrum(normal, fs, 0.0, 0.02);
    const double harmonics = db(s.bandPower(2400, 9000) / s.bandPower(1300, 2100));
    INFO("harmonics above the high partial: " << harmonics << " dB");
    CHECK(harmonics > -12.0);
}

TEST_CASE("RS: TUNE transposes the whole timbre, DECAY scales the burst", "[rs][params]") {
    const double fs = 96000.0;
    const auto base = hit(fs, with());
    CHECK_THAT(highPeak(hit(fs, with(12.0f)), fs, 2.0), WithinRel(2.0 * highPeak(base, fs), 0.03));
    CHECK_THAT(highPeak(hit(fs, with(-12.0f)), fs, 0.5), WithinRel(0.5 * highPeak(base, fs), 0.03));
    const double c0 = spectrum(base, fs, 0.0, 0.03).centroid();
    const double cUp = spectrum(hit(fs, with(12.0f)), fs, 0.0, 0.03).centroid();
    const double cDown = spectrum(hit(fs, with(-12.0f)), fs, 0.0, 0.03).centroid();
    INFO("centroid " << cDown << " / " << c0 << " / " << cUp);
    CHECK(cUp > 1.5 * c0);
    CHECK(cDown < 0.7 * c0);
    CHECK_THAT(vpp(hit(fs, with(12.0f))), WithinRel(vpp(base), 0.25));
    CHECK_THAT(vpp(hit(fs, with(-12.0f))), WithinRel(vpp(base), 0.25));

    double prevLen = 0.0, prevRms = 0.0;
    for (float d : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto x = hit(fs, with(0.0f, d));
        const double len = timeToMinus20dB(x, fs, 0.002);
        const double e = rms(x, fs, 0.0, 0.3);
        INFO("decay " << d << " -20 dB at " << len * 1e3 << " ms, rms " << e);
        CHECK(len > prevLen);
        CHECK(e > prevRms);
        prevLen = len;
        prevRms = e;
    }
    const double mid = timeToMinus20dB(base, fs, 0.002);
    CHECK(timeToMinus20dB(hit(fs, with(0.0f, 0.0f)), fs, 0.002) < 0.7 * mid);  // the 1 ms charge does not scale
    CHECK(timeToMinus20dB(hit(fs, with(0.0f, 1.0f)), fs, 0.002) > 2.5 * mid);
}

TEST_CASE("RS: retrigger excites the ringing state and never clicks", "[rs][retrigger]") {
    const double fs = 48000.0;
    for (double gap : {0.003, 0.007}) {
        const auto one = hit(fs, with(), 4.0f, 0.2);
        const auto two = render<RimShot>(prep, setupAt(fs), with(), {{0.0, Hit::normal()}, {gap, Hit::normal()}}, 0.2);
        const std::size_t shift = static_cast<std::size_t>(std::lround(gap * fs));
        double diffSum = 0.0, diffReset = 0.0;
        for (std::size_t i = shift; i < two.size(); ++i) {
            diffSum = std::max(diffSum, std::abs(static_cast<double>(two[i]) - (static_cast<double>(one[i]) + one[i - shift])));
            diffReset = std::max(diffReset, std::abs(static_cast<double>(two[i]) - one[i - shift]));
        }
        INFO("gap " << gap << " max diff vs linear sum " << diffSum << ", vs reset-and-replay " << diffReset << ", peak " << peakAbs(one));
        CHECK(diffSum > 0.1 * peakAbs(one));    // a switch is as nonlinear as it gets
        CHECK(diffReset > 0.1 * peakAbs(one));  // the resonators were still ringing
        CHECK(peakAbs(two) < 1.6 * peakAbs(one));
        CHECK(maxStep(two, fs, gap, gap + 0.003) < 1.5 * maxStep(one, fs, 0.0, 0.003) + 1e-4);
    }
}

TEST_CASE("RS: robustness fuzz", "[rs][fuzz]") {
    std::uint32_t seed = 41;
    for (double fs : kRates) fuzzRobustness<RimShot>(prep, fs, seed++, 2.0);
    fuzzRobustness<RimShot>(prep, 48000.0, 499, 2.0, 1.0f);
}

TEST_CASE("RS: deterministic; unit variance moves pitch and decay within tolerance", "[rs][determinism]") {
    checkDeterministic<RimShot>(prep, 48000.0);
    checkSeedIgnoredAtZeroVariance<RimShot>(prep, 48000.0, with());
    const double fs = 96000.0;
    double fMin = 1e9, fMax = 0.0, tMin = 1e9, tMax = 0.0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        const auto x = render<RimShot>(prep, setupAt(fs, seed, 1.0f), with(), {{0.0, Hit::normal()}}, 0.2);
        const double f = peakFreq(x, fs, 1100.0, 2400.0, 0.0005, 0.0045);
        const double t20 = timeToMinus20dB(x, fs, 0.002);
        INFO("seed " << seed << " high partial " << f << " -20 dB at " << t20 * 1e3 << " ms");
        REQUIRE(allFinite(x));
        CHECK(f > kHighPitch / 1.3);
        CHECK(f < kHighPitch * 1.3);
        CHECK(t20 > 0.5 * kMinus20);
        CHECK(t20 < 2.0 * kMinus20);
        fMin = std::min(fMin, f);
        fMax = std::max(fMax, f);
        tMin = std::min(tMin, t20);
        tMax = std::max(tMax, t20);
    }
    CHECK(fMax / fMin > 1.03);
    CHECK(tMax / tMin > 1.03);
}

TEST_CASE("RS: against the reference recording of a real unit", "[rs][reference]") {
    if (refPath("rs8", "RS.WAV").empty()) SKIP("reference recordings not present");
    Wav w;
    REQUIRE(readWav(refPath("rs8", "RS.WAV"), w));
    const auto m = hit(w.fs, with(), 4.0f, 0.25);
    const double rh = highPeak(w.x, w.fs), mh = highPeak(m, w.fs);
    const double rl = lowPeak(w.x, w.fs), ml = lowPeak(m, w.fs);
    const double rt = timeToMinus20dB(w.x, w.fs, 0.002), mt = timeToMinus20dB(m, w.fs, 0.002);
    const double re = lastAudible(w.x, w.fs, 0.01), me = lastAudible(m, w.fs, 0.01);
    const auto rs = spectrum(w.x, w.fs, 0.0, 0.03), ms = spectrum(m, w.fs, 0.0, 0.03);
    std::printf("\nRS vs reference   high Hz %.0f / %.0f   low Hz %.0f / %.0f   -20 dB ms %.1f / %.1f   -40 dB ms %.1f / %.1f   centroid Hz %.0f / %.0f\n",
                rh, mh, rl, ml, rt * 1e3, mt * 1e3, re * 1e3, me * 1e3, rs.centroid(), ms.centroid());
    const double bands[][2] = {{300, 650}, {650, 1300}, {1300, 2200}, {2200, 4400}, {4400, 8800}, {8800, 20000}};
    std::printf("                  band power re 1.3-2.2 kHz, dB:");
    for (const auto& b : bands) {
        const double r = db(rs.bandPower(b[0], b[1]) / rs.bandPower(1300, 2200));
        const double mm = db(ms.bandPower(b[0], b[1]) / ms.bandPower(1300, 2200));
        std::printf("  %.0f-%.0f: %.1f / %.1f", b[0], b[1], r, mm);
        CHECK_THAT(mm, WithinAbs(r, 4.0));
    }
    std::printf("\n");
    CHECK_THAT(mh, WithinRel(rh, 0.05));  // the unit's high partial reads 1711 Hz; the manual says 1667
    CHECK_THAT(ml, WithinRel(rl, 0.15));
    CHECK_THAT(mt, WithinRel(rt, 0.15));
    CHECK_THAT(me, WithinAbs(re, 0.004));
    CHECK_THAT(ms.centroid(), WithinRel(rs.centroid(), 0.30));
}

TEST_CASE("RS: listening renders", "[.render]") {
    const double fs = 48000.0;
    std::vector<float> all;
    for (float volts : {4.0f, 14.0f, 2.2f}) appendWithGap(all, hit(fs, with(), volts, 0.25), fs);
    for (float tune : {-7.0f, 7.0f}) appendWithGap(all, hit(fs, with(tune), 6.0f, 0.25), fs);
    for (float decay : {0.0f, 1.0f}) appendWithGap(all, hit(fs, with(0.0f, decay), 6.0f, 0.3), fs);
    std::vector<Event> ev;
    for (int i = 0; i < 12; ++i) ev.push_back({0.11 * i + (i % 3 == 2 ? 0.02 : 0.0), Hit{i % 4 == 0 ? 12.0f : 4.0f, 0.0f}});
    appendWithGap(all, render<RimShot>(prep, setupAt(fs), with(), ev, 1.6), fs);
    const std::string path = renderDir() + "/rs.wav";
    REQUIRE(writeWav(path, all, fs));
    std::printf("wrote %s\n", path.c_str());
}

TEST_CASE("RS: performance", "[.bench]") {
    const double ns = benchNsPerSample<RimShot>(prep, 48000.0, with(0.0f, 1.0f), 0.05);
    std::printf("RS: %.1f ns/sample at 48 kHz (budget 150)\n", ns);
    CHECK(ns < 150.0);
}

// ---------------------------------------------------------------- review fixes

TEST_CASE("RS: non-finite controls cannot poison the voice", "[rs][fuzz]") {
    for (double fs : {44100.0, 192000.0}) checkNonFiniteParams<RimShot>(prep, fs);
}

TEST_CASE("RS: sleeps only once inaudible", "[rs][sleep]") {
    // The early-sleep path used to fire on a zero crossing of the still-ringing
    // 589 Hz high-pass and cut a -70 dBFS tail (short decay, full accent).
    for (double fs : {44100.0, 48000.0, 192000.0}) {
        for (float decay : {0.0f, 0.25f, 0.5f, 1.0f}) {
            for (float volts : {0.5f, 4.0f, 14.0f}) {
                // Windows of about one ringing period of the high-pass (589 Hz,
                // tau 1.6 ms; an octave lower at tune -12).
                checkQuietBeforeSleep<RimShot>(prep, fs, with(0.0f, decay), volts, 0.001);
                checkQuietBeforeSleep<RimShot>(prep, fs, with(-12.0f, decay), volts, 0.002);
            }
        }
    }
}

TEST_CASE("RS: a corrupt unit variance is clamped, not propagated as NaN", "[rs][fuzz]") {
    // Before the fix a NaN or infinite variance made every part NaN and the
    // voice emitted NaN without ever sleeping; variance 10 drove capacitors
    // negative.
    checkBadVariance<RimShot>(prep, 48000.0);
}

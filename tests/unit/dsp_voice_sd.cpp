// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "BridgedTestUtil.h"
#include "mw808/voices/SnareDrum.h"

using namespace bridged_test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using mw808::voices::SnareDrum;

namespace {

const auto prep = [](SnareDrum& v, const VoiceSetup& s) { v.prepare(s); };

constexpr double kLowPitch = 238.0;   // service manual
constexpr double kHighPitch = 476.0;  // service manual
// Passive decays from the schematic: tau = 2 R2 C1 C2 / (C1 + C2).
constexpr double kLowTau = 23.2e-3;
constexpr double kNoiseTau = 28.2e-3;  // C51 (R184 + R185) at 50 % conduction

VoiceParams stock() { return mw808::kVoiceInfo[mw808::SD].defaults; }
VoiceParams with(float tone, float snappy, float tune = 0.0f, float decay = 0.5f) {
    VoiceParams p;
    p.tune = tune;
    p.decay = decay;
    p.tone = tone;
    p.shape = snappy;
    return p;
}

std::vector<float> hit(double fs, const VoiceParams& p, float volts = 4.0f, double seconds = 1.0) {
    return renderHit<SnareDrum>(prep, fs, p, volts, seconds);
}

// Noise content: power above the resonators' reach.
double noisePower(const std::vector<float>& x, double fs, double t0, double t1) {
    return spectrum(x, fs, t0, t1).bandPower(3000.0, 12000.0);
}

}  // namespace

TEST_CASE("SD: both resonators sit on the service-manual pitches at every sample rate", "[sd][pitch]") {
    for (double fs : kRates) {
        // TONE fully anticlockwise, no snappy: the low resonator alone.
        const auto low = hit(fs, with(0.0f, 0.0f));
        const double fLow = zeroCrossFreq(low, fs, 0.006, 0.060);
        // TONE fully clockwise: the high one dominates the first 15 ms.
        const auto high = hit(fs, with(1.0f, 0.0f));
        const double fHigh = peakFreq(high, fs, 380.0, 600.0, 0.0015, 0.020);
        INFO("fs " << fs << " low " << fLow << " high " << fHigh);
        CHECK_THAT(fLow, WithinRel(kLowPitch, 0.005));
        CHECK_THAT(fHigh, WithinRel(kHighPitch, 0.02));
        CHECK_THAT(peakFreq(low, fs, 180.0, 300.0, 0.005, 0.1), WithinRel(kLowPitch, 0.01));
    }
}

TEST_CASE("SD: decays match the passive circuit and the manual, at every sample rate", "[sd][decay]") {
    double refLow = 0.0, refNoise = 0.0;
    for (double fs : kRates) {
        const auto low = hit(fs, with(0.0f, 0.0f));
        const double t60 = t60Fit(low, fs, 1.0 / kLowPitch);
        // Noise: level drop between two windows 60 ms apart.
        const auto snare = hit(fs, with(0.5f, 1.0f));
        const double drop = db(noisePower(snare, fs, 0.020, 0.040) / noisePower(snare, fs, 0.080, 0.100));
        const double t20 = timeToMinus20dB(hit(fs, stock()), fs, 1.0 / kLowPitch);
        INFO("fs " << fs << " low T60 " << t60 << " noise drop " << drop << " dB/60 ms, whole voice -20 dB at " << t20 * 1e3
                   << " ms (manual 60)");
        CHECK_THAT(t60, WithinRel(6.908 * kLowTau, 0.05));
        CHECK_THAT(drop, WithinAbs(8.686 * 0.060 / kNoiseTau, 2.0));
        CHECK_THAT(t20, WithinRel(0.060, 0.25));
        if (fs == kRates[0]) {
            refLow = t60;
            refNoise = drop;
        }
        CHECK_THAT(t60, WithinRel(refLow, 0.02));
        CHECK_THAT(drop, WithinAbs(refNoise, 2.5));  // a different noise realisation at each rate
    }
}

TEST_CASE("SD: levels match the service-manual table and rise monotonically with volts", "[sd][level]") {
    for (double fs : kRates) {
        const auto normal = hit(fs, stock(), 4.0f);
        const auto accent = hit(fs, stock(), 14.0f);
        INFO("fs " << fs << " normal " << vpp(normal) << " Vpp, accent " << vpp(accent) << " Vpp");
        // The peaks are noise peaks: they move a little with the rate.
        CHECK_THAT(vpp(normal), WithinRel(3.0, 0.15));
        CHECK_THAT(vpp(accent), WithinRel(10.0, 0.15));
        // The tone alone is exactly repeatable.
        const auto tone = hit(fs, with(0.5f, 0.0f), 4.0f);
        CHECK_THAT(vpp(tone), WithinRel(2.0, 0.03));
        CHECK_THAT(rms(normal, fs, 0.0, 0.2), WithinRel(rms(hit(44100.0, stock(), 4.0f), 44100.0, 0.0, 0.2), 0.06));
    }
    checkMonotonicLevel<SnareDrum>(prep, 48000.0, stock(), 0.5);
    checkMonotonicLevel<SnareDrum>(prep, 48000.0, with(0.5f, 0.0f), 0.5);
}

TEST_CASE("SD: accent drives the noise and the high resonator harder than the body", "[sd][level]") {
    const double fs = 48000.0;
    const auto normal = hit(fs, stock(), 4.0f), accent = hit(fs, stock(), 14.0f);
    const double noiseUp = db(noisePower(accent, fs, 0.0, 0.1) / noisePower(normal, fs, 0.0, 0.1));
    const auto sN = spectrum(normal, fs, 0.0, 0.1), sA = spectrum(accent, fs, 0.0, 0.1);
    const double lowUp = db(sA.bandPower(200, 280) / sN.bandPower(200, 280));
    INFO("accent raises the noise by " << noiseUp << " dB and the low resonator by " << lowUp << " dB");
    CHECK(noiseUp > lowUp + 1.5);
    CHECK_THAT(lowUp, WithinAbs(20.0 * std::log10(3.5) * 0.893, 1.5));  // the resonator is linear in the pulse
    // With SNAPPY at zero a normal hit has no noise at all, an accented one a little.
    const auto dryN = hit(fs, with(0.5f, 0.0f), 4.0f), dryA = hit(fs, with(0.5f, 0.0f), 14.0f);
    CHECK(db(noisePower(dryN, fs, 0.0, 0.1) / noisePower(normal, fs, 0.0, 0.1)) < -30.0);
    CHECK(noisePower(dryA, fs, 0.0, 0.1) > 20.0 * noisePower(dryN, fs, 0.0, 0.1));
}

TEST_CASE("SD: TONE crossfades the resonators, SNAPPY sets the noise amount, TUNE and DECAY scale", "[sd][params]") {
    const double fs = 48000.0;
    // TONE: high/low balance rises monotonically; the low resonator never disappears.
    double prev = -1e9;
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto s = spectrum(hit(fs, with(t, 0.0f)), fs, 0.0, 0.08);
        const double balance = db(s.bandPower(400, 560) / s.bandPower(200, 280));
        INFO("tone " << t << " high/low " << balance << " dB");
        CHECK(balance > prev + 1.0);
        prev = balance;
    }
    {
        // The crossfade network itself (R200 47k keeps the high resonator below
        // unity even fully clockwise): gains 1/0, 0.50/0.26, 0.24/0.52.
        const auto lo = hit(fs, with(0.0f, 0.0f)), mid = hit(fs, with(0.5f, 0.0f)), hi = hit(fs, with(1.0f, 0.0f));
        const auto sl = spectrum(lo, fs, 0.03, 0.09), sm = spectrum(mid, fs, 0.03, 0.09), sh = spectrum(hi, fs, 0.03, 0.09);
        // After 30 ms only the low resonator is left.
        CHECK_THAT(db(sm.bandPower(200, 280) / sl.bandPower(200, 280)), WithinAbs(20.0 * std::log10(0.496), 0.5));
        CHECK_THAT(db(sh.bandPower(200, 280) / sl.bandPower(200, 280)), WithinAbs(20.0 * std::log10(0.242), 0.5));
    }

    // SNAPPY: amount, not decay.
    double prevNoise = 0.0;
    double dropAtHalf = 0.0;
    for (float sn : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto x = hit(fs, with(0.5f, sn));
        const double n = noisePower(x, fs, 0.0, 0.12);
        const double drop = db(noisePower(x, fs, 0.020, 0.040) / noisePower(x, fs, 0.080, 0.100));
        INFO("snappy " << sn << " noise " << db(n) << " dB, drop " << drop);
        CHECK(n > prevNoise);
        if (sn == 0.5f) dropAtHalf = drop;
        if (sn > 0.5f) CHECK_THAT(drop, WithinAbs(dropAtHalf, 1.5));
        prevNoise = n;
    }
    {
        // Reference recordings: +6.0 dB from 5 to 7.5, +2.9 dB from 7.5 to 10.
        const double n5 = noisePower(hit(fs, with(0.5f, 0.5f)), fs, 0.0, 0.12);
        const double n75 = noisePower(hit(fs, with(0.5f, 0.75f)), fs, 0.0, 0.12);
        const double n10 = noisePower(hit(fs, with(0.5f, 1.0f)), fs, 0.0, 0.12);
        INFO("snappy steps " << db(n75 / n5) << " dB, " << db(n10 / n75) << " dB");
        CHECK_THAT(db(n75 / n5), WithinAbs(6.0, 1.5));
        CHECK_THAT(db(n10 / n75), WithinAbs(2.9, 1.5));
    }
    {
        // Noise filters: 2.75 kHz two-pole high-pass plus 590 Hz one-pole.
        const auto wet = hit(fs, with(0.0f, 1.0f)), dry = hit(fs, with(0.0f, 0.0f));
        std::vector<float> noise(wet.size());
        for (std::size_t i = 0; i < wet.size(); ++i) noise[i] = wet[i] - dry[i];
        const auto s = spectrum(noise, fs, 0.0, 0.15);
        const double tilt = db(s.bandPower(2800, 5600) / s.bandPower(700, 1400));
        INFO("noise 2.8-5.6k vs 0.7-1.4k: " << tilt << " dB, centroid " << s.centroid());
        CHECK(tilt > 15.0);
        CHECK(s.centroid() > 3500.0);
        CHECK(s.centroid() < 7500.0);
    }

    // TUNE: +12 doubles both resonators, leaves the decay and the noise alone.
    const auto base = hit(fs, with(0.0f, 0.0f)), up = hit(fs, with(0.0f, 0.0f, 12.0f));
    CHECK_THAT(zeroCrossFreq(up, fs, 0.006, 0.060), WithinRel(2.0 * zeroCrossFreq(base, fs, 0.006, 0.060), 0.005));
    CHECK_THAT(peakFreq(hit(fs, with(1.0f, 0.0f, 12.0f)), fs, 760.0, 1200.0, 0.001, 0.015), WithinRel(2.0 * kHighPitch, 0.03));
    CHECK_THAT(zeroCrossFreq(hit(fs, with(0.0f, 0.0f, -12.0f)), fs, 0.006, 0.120), WithinRel(0.5 * kLowPitch, 0.005));
    CHECK_THAT(t60Fit(up, fs, 0.5 / kLowPitch), WithinRel(t60Fit(base, fs, 1.0 / kLowPitch), 0.04));
    CHECK_THAT(vpp(up), WithinRel(vpp(base), 0.25));

    // DECAY: x0.25 .. x4 on the resonators and on the noise envelope.
    const double mid = t60Fit(base, fs, 1.0 / kLowPitch);
    CHECK_THAT(t60Fit(hit(fs, with(0.0f, 0.0f, 0.0f, 0.0f)), fs, 1.0 / kLowPitch, -3.0, -30.0), WithinRel(0.25 * mid, 0.08));
    CHECK_THAT(t60Fit(hit(fs, with(0.0f, 0.0f, 0.0f, 1.0f), 4.0f, 3.0), fs, 1.0 / kLowPitch), WithinRel(4.0 * mid, 0.05));
    const auto longNoise = hit(fs, with(0.5f, 1.0f, 0.0f, 1.0f), 4.0f, 2.0);
    CHECK_THAT(db(noisePower(longNoise, fs, 0.020, 0.040) / noisePower(longNoise, fs, 0.260, 0.280)),
               WithinAbs(8.686 * 0.240 / (4.0 * kNoiseTau), 2.0));
}

TEST_CASE("SD: retrigger adds to the ringing state and never clicks", "[sd][retrigger]") {
    const double fs = 48000.0;
    const VoiceParams p = with(0.5f, 0.0f);
    for (double gap : {0.004, 0.011, 0.030}) {
        const std::size_t shift = static_cast<std::size_t>(std::lround(gap * fs));
        auto compare = [&](float volts, double& diffSum, double& diffReset) {
            const auto one = hit(fs, p, volts, 0.5);
            const auto two = render<SnareDrum>(prep, setupAt(fs), p, {{0.0, Hit{volts, 0.0f}}, {gap, Hit{volts, 0.0f}}}, 0.5);
            diffSum = diffReset = 0.0;
            for (std::size_t i = shift; i < two.size(); ++i) {
                diffSum = std::max(diffSum, std::abs(static_cast<double>(two[i]) - (static_cast<double>(one[i]) + one[i - shift])));
                diffReset = std::max(diffReset, std::abs(static_cast<double>(two[i]) - one[i - shift]));
            }
            CHECK(maxStep(two, fs, gap, gap + 0.003) < 1.3 * maxStep(one, fs, 0.0, 0.003) + 1e-4);
            return peakAbs(one);
        };
        double sum4, reset4, sum14, reset14;
        const double pk4 = compare(4.0f, sum4, reset4);
        const double pk14 = compare(14.0f, sum14, reset14);
        INFO("gap " << gap << ": 4 V sum diff " << sum4 << " reset diff " << reset4 << "; 14 V sum diff " << sum14);
        // At normal level the resonators are linear: two hits superpose
        // exactly, which is the ringing state carrying over...
        CHECK(sum4 < 1e-4 * pk4);
        CHECK(reset4 > 0.05 * pk4);
        // ...and at full accent the high resonator's op-amp runs out of supply,
        // so the hits interact.
        if (gap < 0.006) CHECK(sum14 > 0.01 * pk14);
    }
    {
        // With snappy, the noise envelope is topped up, not restarted.
        const auto one = hit(fs, stock(), 4.0f, 0.5);
        const auto two = render<SnareDrum>(prep, setupAt(fs), stock(), {{0.0, Hit::normal()}, {0.03, Hit::normal()}}, 0.5);
        const double n1 = noisePower(one, fs, 0.0, 0.03), n2 = noisePower(two, fs, 0.03, 0.06);
        INFO("noise power, first hit " << n1 << ", second hit window " << n2);
        CHECK(n2 > 0.6 * n1);
        CHECK(n2 < 1.6 * n1);
    }
}

TEST_CASE("SD: robustness fuzz", "[sd][fuzz]") {
    std::uint32_t seed = 21;
    for (double fs : kRates) fuzzRobustness<SnareDrum>(prep, fs, seed++, 8.0);
    fuzzRobustness<SnareDrum>(prep, 48000.0, 199, 8.0, 1.0f);
}

TEST_CASE("SD: deterministic; unit variance moves pitch and decay within tolerance", "[sd][determinism]") {
    checkDeterministic<SnareDrum>(prep, 48000.0);
    checkSeedIgnoredAtZeroVariance<SnareDrum>(prep, 48000.0, stock());

    const double fs = 48000.0;
    double fMin = 1e9, fMax = 0.0, tMin = 1e9, tMax = 0.0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        const auto x = render<SnareDrum>(prep, setupAt(fs, seed, 1.0f), with(0.0f, 0.0f), {{0.0, Hit::normal()}}, 1.0);
        const double f = zeroCrossFreq(x, fs, 0.006, 0.060);
        const double t60 = t60Fit(x, fs, 1.0 / f);
        INFO("seed " << seed << " pitch " << f << " T60 " << t60);
        REQUIRE(allFinite(x));
        CHECK(f > kLowPitch / 1.27);
        CHECK(f < kLowPitch * 1.27);
        CHECK(t60 > 6.908 * kLowTau / 1.3);
        CHECK(t60 < 6.908 * kLowTau * 1.3);
        fMin = std::min(fMin, f);
        fMax = std::max(fMax, f);
        tMin = std::min(tMin, t60);
        tMax = std::max(tMax, t60);
    }
    CHECK(fMax / fMin > 1.03);
    CHECK(tMax / tMin > 1.03);
}

TEST_CASE("SD: against reference recordings of a real unit", "[sd][reference]") {
    // The reference unit carries the later-revision capacitors (research/01
    // §2.2, "PORTION CHANGED"): its resonators sit at 172 / 350 Hz, not at the
    // 238 / 476 Hz of the drawn circuit and the manual's chart. The model is
    // therefore compared at tune = -5.6 semitones.
    struct Ref {
        const char* file;
        float tone, snappy;
    };
    const Ref refs[] = {{"SD0000.WAV", 0.0f, 0.0f}, {"SD5000.WAV", 0.5f, 0.0f}, {"SD5050.WAV", 0.5f, 0.5f},
                        {"SD5075.WAV", 0.5f, 0.75f}, {"SD5010.WAV", 0.5f, 1.0f}, {"SD0010.WAV", 0.0f, 1.0f},
                        {"SD1010.WAV", 1.0f, 1.0f}};
    if (refPath("sd8", refs[0].file).empty()) SKIP("reference recordings not present");
    std::printf("\nSD vs reference   file        low Hz          low T60 s      noise/tone dB    noise drop dB/60ms  centroid Hz\n");
    for (const Ref& r : refs) {
        Wav w;
        REQUIRE(readWav(refPath("sd8", r.file), w));
        const auto m = hit(w.fs, with(r.tone, r.snappy, -5.6f), 4.0f, 0.5);
        auto lowPitch = [&](const std::vector<float>& x) { return peakFreq(x, w.fs, 140.0, 280.0, 0.004, 0.12); };
        auto noiseRel = [&](const std::vector<float>& x) {
            const auto s = spectrum(x, w.fs, 0.0, 0.12);
            return db(s.bandPower(2000, 16000) / s.bandPower(120, 330));
        };
        auto drop = [&](const std::vector<float>& x) {
            return db(noisePower(x, w.fs, 0.020, 0.040) / noisePower(x, w.fs, 0.080, 0.100));
        };
        auto lowDecay = [&](const std::vector<float>& x) {
            // Level of the low resonator in two windows 40 ms apart -> T60.
            const double a = spectrum(x, w.fs, 0.010, 0.040).bandPower(140, 280);
            const double b = spectrum(x, w.fs, 0.050, 0.080).bandPower(140, 280);
            return 60.0 * 0.040 / db(a / b);
        };
        const double rc = spectrum(w.x, w.fs, 0.0, 0.12).centroid(), mc = spectrum(m, w.fs, 0.0, 0.12).centroid();
        std::printf("  %-12s ref/model  %6.1f / %6.1f  %5.3f / %5.3f   %6.1f / %6.1f   %5.1f / %5.1f      %5.0f / %5.0f\n", r.file,
                    lowPitch(w.x), lowPitch(m), lowDecay(w.x), lowDecay(m), noiseRel(w.x), noiseRel(m),
                    r.snappy > 0.0f ? drop(w.x) : 0.0, r.snappy > 0.0f ? drop(m) : 0.0, rc, mc);
        INFO(r.file);
        CHECK_THAT(lowPitch(m), WithinRel(lowPitch(w.x), 0.02));
        CHECK_THAT(lowDecay(m), WithinRel(lowDecay(w.x), 0.30));  // revised capacitors ring a little longer
        if (r.snappy > 0.0f) {
            CHECK_THAT(noiseRel(m), WithinAbs(noiseRel(w.x), 4.0));
            CHECK_THAT(drop(m), WithinAbs(drop(w.x), 4.0));
            CHECK_THAT(mc, WithinRel(rc, 0.45));
        }
    }
}

TEST_CASE("SD: listening renders", "[.render]") {
    const double fs = 48000.0;
    std::vector<float> all;
    for (float volts : {4.0f, 14.0f, 2.2f}) appendWithGap(all, hit(fs, stock(), volts, 0.5), fs);
    for (float tone : {0.0f, 1.0f}) appendWithGap(all, hit(fs, with(tone, 0.5f), 4.0f, 0.5), fs);
    for (float snappy : {0.0f, 1.0f}) appendWithGap(all, hit(fs, with(0.5f, snappy), 4.0f, 0.5), fs);
    for (float decay : {0.0f, 1.0f}) appendWithGap(all, hit(fs, with(0.5f, 0.6f, 0.0f, decay), 9.0f, 1.0), fs);
    for (float tune : {-6.0f, 5.0f}) appendWithGap(all, hit(fs, with(0.5f, 0.6f, tune), 9.0f, 0.5), fs);
    {
        std::vector<Event> ev;
        for (int i = 0; i < 16; ++i) ev.push_back({0.07 * i, Hit{2.0f + 0.75f * static_cast<float>(i), 0.0f}});
        appendWithGap(all, render<SnareDrum>(prep, setupAt(fs), with(0.6f, 0.6f), ev, 1.8), fs);
    }
    const std::string path = renderDir() + "/sd.wav";
    REQUIRE(writeWav(path, all, fs));
    std::printf("wrote %s\n", path.c_str());
}

TEST_CASE("SD: performance", "[.bench]") {
    const double ns = benchNsPerSample<SnareDrum>(prep, 48000.0, with(0.5f, 1.0f, 0.0f, 1.0f), 0.1);
    std::printf("SD: %.1f ns/sample at 48 kHz (budget 150)\n", ns);
    CHECK(ns < 150.0);
}

// ---------------------------------------------------------------- review fixes

TEST_CASE("SD: non-finite controls cannot poison the voice", "[sd][fuzz]") {
    for (double fs : {44100.0, 192000.0}) checkNonFiniteParams<SnareDrum>(prep, fs);
}

TEST_CASE("SD: sleeps only once inaudible", "[sd][sleep]") {
    for (double fs : {44100.0, 192000.0}) {
        checkQuietBeforeSleep<SnareDrum>(prep, fs, stock(), 4.0f, 0.005);
        checkQuietBeforeSleep<SnareDrum>(prep, fs, with(0.0f, 1.0f, -12.0f, 1.0f), 14.0f, 0.009);
        checkQuietBeforeSleep<SnareDrum>(prep, fs, with(1.0f, 0.0f, 12.0f, 0.0f), 0.5f, 0.003);
    }
}

TEST_CASE("SD: an automated TONE jump glides instead of clicking", "[sd][params]") {
    const double fs = 48000.0;
    Rig<SnareDrum> rig(prep, setupAt(fs));
    rig.voice.setParams(with(0.0f, 0.0f));
    rig.voice.trigger(Hit::normal());
    std::vector<float> x;
    rig.run(x, static_cast<int>(0.04 * fs));
    rig.voice.setParams(with(1.0f, 0.0f));
    rig.run(x, static_cast<int>(0.02 * fs));
    const double jump = maxStep(x, fs, 0.04, 0.045);
    const double natural = maxStep(x, fs, 0.03, 0.04);
    INFO("largest step after the jump " << jump << ", before it " << natural);
    CHECK(jump < 1.5 * natural);  // without the glide: about 10x
}

TEST_CASE("SD: the mixer output stays within the op-amp rails in a fast accented roll", "[sd][level]") {
    // Before the fix the IC13a mixer was not limited: eight full-accent hits
    // 2 ms apart with TUNE +12 and SNAPPY up reached about 14.9 V.
    const double fs = 48000.0;
    const double railUnits = 13.5 * 0.853 / static_cast<double>(mw808::kVoltsPerUnit);
    double worst = 0.0;
    for (double gap : {0.0005, 0.002, 0.005}) {
        std::vector<Event> ev;
        for (int i = 0; i < 8; ++i) ev.push_back({gap * i, Hit{14.0f, 0.0f}});
        const auto x = render<SnareDrum>(prep, setupAt(fs), with(0.0f, 1.0f, 12.0f, 1.0f), ev, 0.2);
        worst = std::max(worst, peakAbs(x));
    }
    INFO("worst peak " << worst << " (rail " << railUnits << ")");
    CHECK(worst <= railUnits * (1.0 + 1e-6));
    CHECK(worst > 0.8 * railUnits);  // the roll does drive it to the rail
}

TEST_CASE("SD: a corrupt unit variance is clamped, not propagated as NaN", "[sd][fuzz]") {
    // Before the fix a NaN or infinite variance made every part NaN and the
    // voice emitted NaN without ever sleeping; variance 10 drove capacitors
    // negative.
    checkBadVariance<SnareDrum>(prep, 48000.0);
}

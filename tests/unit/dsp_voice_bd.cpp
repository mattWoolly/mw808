// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "BridgedTestUtil.h"
#include "mw808/voices/BassDrum.h"

using namespace bridged_test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using mw808::voices::BassDrum;

namespace {

const auto prep = [](BassDrum& v, const VoiceSetup& s) { v.prepare(s); };

// Schematic: 1 / (2 pi sqrt(46.05k * 1M * 15n * 15n)).
constexpr double kPitch = 49.44;
constexpr double kAttackPitch = 130.0;

VoiceParams stock() { return mw808::kVoiceInfo[mw808::BD].defaults; }

std::vector<float> hit(double fs, const VoiceParams& p, float volts = 4.0f, double seconds = 2.0, float semis = 0.0f) {
    return renderHit<BassDrum>(prep, fs, p, volts, seconds, semis);
}

// Pitch once the sigh is over: the tail between -26 and -40 dB.
double settledPitch(const std::vector<float>& x, double fs, double t0 = 0.45, double t1 = 0.75) {
    return zeroCrossFreq(x, fs, t0, t1);
}

}  // namespace

TEST_CASE("BD: settled pitch matches the schematic at every sample rate", "[bd][pitch]") {
    for (double fs : kRates) {
        const auto x = hit(fs, stock());
        INFO("fs " << fs);
        CHECK_THAT(settledPitch(x, fs), WithinRel(kPitch, 0.006));
    }
}

TEST_CASE("BD: attack state runs near 130 Hz for one cycle, then the pitch sighs down", "[bd][pitch]") {
    for (double fs : kRates) {
        VoiceParams p = stock();
        p.tone = 1.0f;
        const auto x = hit(fs, p);
        // First downward and upward zero crossings after the pulse: the
        // negative half-cycle of the attack state.
        std::size_t down = 0, up = 0;
        for (std::size_t i = static_cast<std::size_t>(0.0012 * fs); i < x.size(); ++i) {
            if (down == 0 && x[i - 1] >= 0.0f && x[i] < 0.0f) down = i;
            if (down != 0 && x[i - 1] < 0.0f && x[i] >= 0.0f) {
                up = i;
                break;
            }
        }
        REQUIRE(up > down);
        const double halfPeriod = static_cast<double>(up - down) / fs;
        INFO("fs " << fs << " attack half period " << halfPeriod * 1e3 << " ms, ends at " << static_cast<double>(up) / fs * 1e3);
        CHECK_THAT(0.5 / halfPeriod, WithinRel(kAttackPitch, 0.12));
        CHECK_THAT(static_cast<double>(up) / fs, WithinAbs(6.0e-3, 0.8e-3));  // ~6 ms attack (reference: 6.07 ms)

        // Sigh: reference recordings run 63 Hz for the first cycle, 57 Hz
        // around 22 ms, and are back within 1 % by 150 ms.
        const double early = zeroCrossFreq(x, fs, 0.004, 0.024);
        const double mid = zeroCrossFreq(x, fs, 0.020, 0.060);
        const double late = zeroCrossFreq(x, fs, 0.150, 0.300);
        INFO("early " << early << " mid " << mid << " late " << late);
        CHECK_THAT(early, WithinRel(63.2, 0.05));
        CHECK(mid > 1.06 * kPitch);
        CHECK(mid < early);
        CHECK_THAT(late, WithinRel(kPitch, 0.015));
    }
}

TEST_CASE("BD: sigh is driven by amplitude: deeper on accents, longer on long decays", "[bd][pitch]") {
    const double fs = 48000.0;
    const double normal = zeroCrossFreq(hit(fs, stock(), 4.0f), fs, 0.060, 0.140);
    const double accent = zeroCrossFreq(hit(fs, stock(), 14.0f), fs, 0.060, 0.140);
    const double weak = zeroCrossFreq(hit(fs, stock(), 2.2f), fs, 0.060, 0.140);
    INFO("60-140 ms pitch: weak " << weak << " normal " << normal << " accent " << accent);
    CHECK(accent > normal + 0.8);
    CHECK(normal > weak);

    VoiceParams lng = stock(), shrt = stock();
    lng.decay = 1.0f;
    shrt.decay = 0.2f;
    const double fLong = zeroCrossFreq(hit(fs, lng), fs, 0.150, 0.250);
    const double fShort = zeroCrossFreq(hit(fs, shrt), fs, 0.150, 0.250);
    INFO("150-250 ms pitch: long decay " << fLong << " short decay " << fShort);
    CHECK(fLong > fShort + 0.5);  // reference: still +0.65 Hz at 230 ms at full decay
    // ...but both settle to the same pitch in the end.
    CHECK_THAT(zeroCrossFreq(hit(fs, lng, 4.0f, 4.0), fs, 2.0, 3.0), WithinRel(kPitch, 0.006));
}

TEST_CASE("BD: decay times follow the DECAY pot and hold across sample rates", "[bd][decay]") {
    // Closed form (research/01 §1.5): T60 = 13.8 / (d_passive (1 - L)).
    struct Point {
        float knob;
        double t60;       // derived from the schematic; 0 = too short to fit (Q 2.3)
        double minus20;   // service manual: 50 / 300 / 800 ms
    };
    const Point points[] = {{0.0f, 0.0, 0.050}, {0.5f, 0.99, 0.300}, {1.0f, 3.75, 0.800}};
    for (const Point& pt : points) {
        double ref60 = 0.0, ref20 = 0.0;
        for (double fs : kRates) {
            VoiceParams p = stock();
            p.decay = pt.knob;
            const auto x = hit(fs, p, 4.0f, 6.0);
            const double t60 = t60Fit(x, fs, 0.0202);
            const double t20 = timeToMinus20dB(x, fs, 0.0202);
            INFO("decay " << pt.knob << " fs " << fs << " T60 " << t60 << " s, -20 dB at " << t20 * 1e3 << " ms (manual "
                          << pt.minus20 * 1e3 << ")");
            if (fs == kRates[0]) {
                ref60 = t60;
                ref20 = t20;
            }
            CHECK_THAT(t20, WithinRel(ref20, 0.02));
            if (pt.t60 > 0.0) {
                CHECK_THAT(t60, WithinRel(pt.t60, 0.08));
                CHECK_THAT(t60, WithinRel(ref60, 0.02));
            }
            // The manual defines only the 12 o'clock point; its SHORT figure
            // fits the anticlockwise end. Its LONG figure (800 ms) does not
            // fit the schematic (1.25 s) nor the reference unit (1.15 s).
            if (pt.knob == 0.5f) CHECK_THAT(t20, WithinRel(pt.minus20, 0.20));
            if (pt.knob == 0.0f) CHECK_THAT(t20, WithinRel(pt.minus20, 0.30));
            if (pt.knob == 1.0f) CHECK_THAT(t20, WithinRel(1.25, 0.10));
        }
    }
    // Monotonic in the knob, and no setting self-oscillates.
    double prev = 0.0;
    for (float k = 0.0f; k <= 1.0f; k += 0.125f) {
        VoiceParams p = stock();
        p.decay = k;
        const auto x = hit(48000.0, p, 4.0f, 6.0);
        const double t20 = timeToMinus20dB(x, 48000.0, 0.0202);
        INFO("knob " << k << " -20 dB at " << t20);
        CHECK(t20 > prev);
        CHECK(t20 < 1.4);
        prev = t20;
    }
}

TEST_CASE("BD: levels match the service-manual table and rise monotonically with volts", "[bd][level]") {
    for (double fs : kRates) {
        const auto normal = hit(fs, stock(), 4.0f);
        const auto accent = hit(fs, stock(), 14.0f);
        INFO("fs " << fs << " normal " << vpp(normal) << " Vpp, accent " << vpp(accent) << " Vpp");
        CHECK_THAT(vpp(normal), WithinRel(3.5, 0.05));
        CHECK_THAT(vpp(accent), WithinRel(10.0, 0.05));
        CHECK_THAT(peakAbs(normal), WithinRel(0.35, 0.12));
        CHECK_THAT(peakAbs(accent), WithinRel(1.0, 0.12));
    }
    checkMonotonicLevel<BassDrum>(prep, 48000.0, stock(), 1.5);
}

TEST_CASE("BD: accent hits harder and brighter, not just louder", "[bd][level]") {
    const double fs = 48000.0;
    const auto normal = hit(fs, stock(), 4.0f);
    const auto accent = hit(fs, stock(), 14.0f);
    // The attack transient grows faster than the body of the note.
    const double attackRatio = rms(accent, fs, 0.0, 0.006) / rms(normal, fs, 0.0, 0.006);
    const double bodyRatio = rms(accent, fs, 0.05, 0.3) / rms(normal, fs, 0.05, 0.3);
    INFO("attack x" << attackRatio << " body x" << bodyRatio);
    CHECK(attackRatio > 1.25 * bodyRatio);
    // More of the energy sits above the fundamental.
    const auto sN = spectrum(normal, fs, 0.0, 0.05), sA = spectrum(accent, fs, 0.0, 0.05);
    const double brightN = db(sN.bandPower(100, 5000) / sN.bandPower(20, 100));
    const double brightA = db(sA.bandPower(100, 5000) / sA.bandPower(20, 100));
    INFO("100 Hz+ content re fundamental: normal " << brightN << " dB, accent " << brightA << " dB");
    CHECK(brightA > brightN + 1.5);
}

TEST_CASE("BD: TONE sets the click, DECAY the length, SWEEP the sigh, TUNE the pitch", "[bd][params]") {
    const double fs = 48000.0;
    // TONE: a one-pole low-pass, 305 Hz .. 7.2 kHz. The first sample step (the
    // pulse edge) grows with it, the body of the note does not change.
    double prevStep = 0.0;
    double body0 = 0.0;
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        VoiceParams p = stock();
        p.tone = t;
        const auto x = hit(fs, p);
        const double step = maxStep(x, fs, 0.0, 0.0015);
        const double body = rms(x, fs, 0.05, 0.3);
        INFO("tone " << t << " click step " << step << " body rms " << body);
        CHECK(step > 1.15 * prevStep);
        if (t == 0.0f) body0 = body;
        CHECK_THAT(body, WithinRel(body0, 0.03));
        prevStep = step;
    }
    {
        // The corner itself: ratio of 3 kHz to 200 Hz content of the click.
        VoiceParams dark = stock(), bright = stock();
        dark.tone = 0.0f;
        bright.tone = 1.0f;
        const auto sd = spectrum(hit(fs, dark), fs, 0.0, 0.01);
        const auto sb = spectrum(hit(fs, bright), fs, 0.0, 0.01);
        const double diff = db(sb.bandPower(2500, 3500) / sb.bandPower(100, 300)) -
                            db(sd.bandPower(2500, 3500) / sd.bandPower(100, 300));
        INFO("3 kHz click content, bright vs dark: " << diff << " dB");
        // |H| of 305 Hz vs 7.2 kHz one-poles at 3 kHz: about 19 dB apart.
        CHECK_THAT(diff, WithinAbs(19.0, 3.0));
    }

    // SWEEP: 0 removes the sigh, 1 roughly doubles it.
    double depth[3] = {};
    int i = 0;
    for (float s : {0.0f, 0.5f, 1.0f}) {
        VoiceParams p = stock();
        p.shape = s;
        const auto x = hit(fs, p);
        depth[i++] = zeroCrossFreq(x, fs, 0.020, 0.060) / settledPitch(x, fs) - 1.0;
    }
    INFO("sigh depth at 20-60 ms: none " << depth[0] << " stock " << depth[1] << " double " << depth[2]);
    CHECK(depth[0] < 0.015);
    CHECK(depth[1] > 0.06);
    CHECK(depth[2] > 1.5 * depth[1]);
    CHECK(depth[2] < 2.6 * depth[1]);

    // TUNE and per-hit pitch: +12 doubles, and the decay time does not move.
    VoiceParams up = stock();
    up.tune = 12.0f;
    const auto base = hit(fs, stock());
    const auto tuned = hit(fs, up);
    const auto played = hit(fs, stock(), 4.0f, 2.0, 12.0f);
    CHECK_THAT(settledPitch(tuned, fs), WithinRel(2.0 * settledPitch(base, fs), 0.005));
    CHECK_THAT(settledPitch(played, fs), WithinRel(2.0 * settledPitch(base, fs), 0.005));
    CHECK_THAT(t60Fit(tuned, fs, 0.0101), WithinRel(t60Fit(base, fs, 0.0202), 0.05));
    CHECK_THAT(vpp(tuned), WithinRel(vpp(base), 0.15));
    VoiceParams down = stock();
    down.tune = -12.0f;
    CHECK_THAT(settledPitch(hit(fs, down), fs), WithinRel(0.5 * settledPitch(base, fs), 0.005));
}

TEST_CASE("BD: chromatic range of +-36 semitones stays stable and in tune", "[bd][params]") {
    for (double fs : {44100.0, 192000.0}) {
        for (float semis : {-36.0f, -24.0f, -12.0f, 0.0f, 12.0f, 24.0f, 36.0f}) {
            VoiceParams p = stock();
            p.decay = 1.0f;
            const auto x = hit(fs, p, 14.0f, 3.0, semis);
            const double f = zeroCrossFreq(x, fs, 1.0, 3.0);
            INFO("fs " << fs << " semis " << semis << " pitch " << f << " peak " << peakAbs(x));
            REQUIRE(allFinite(x));
            CHECK(peakAbs(x) < 2.0);
            CHECK_THAT(f, WithinRel(kPitch * std::exp2(semis / 12.0), 0.01));
        }
    }
}

TEST_CASE("BD: a new pitch glides the ringing resonator, nothing is reset", "[bd][retrigger]") {
    const double fs = 48000.0;
    VoiceParams p = stock();
    p.decay = 1.0f;
    const auto x = render<BassDrum>(prep, setupAt(fs), p, {{0.0, Hit{4.0f, 0.0f}}, {0.2, Hit{0.6f, 7.0f}}}, 1.5);
    // The weak second hit adds little energy: the note keeps ringing through
    // it at about the same level, now at the new pitch.
    const double before = rms(x, fs, 0.15, 0.2), after = rms(x, fs, 0.22, 0.27);
    INFO("rms before " << before << " after " << after);
    CHECK(after > 0.7 * before);
    CHECK_THAT(zeroCrossFreq(x, fs, 0.8, 1.4), WithinRel(kPitch * std::exp2(7.0 / 12.0), 0.01));
    CHECK(maxStep(x, fs, 0.2, 0.21) < 1.5 * maxStep(x, fs, 0.0, 0.01));
}

TEST_CASE("BD: retrigger excites the ringing state and never clicks", "[bd][retrigger]") {
    const double fs = 48000.0;
    for (double gap : {0.030, 0.085, 0.200}) {
        VoiceParams p = stock();
        const auto one = hit(fs, p, 4.0f, 1.0);
        const auto two = render<BassDrum>(prep, setupAt(fs), p, {{0.0, Hit::normal()}, {gap, Hit::normal()}}, 1.0);
        const std::size_t shift = static_cast<std::size_t>(std::lround(gap * fs));
        double diffSum = 0.0, diffReset = 0.0;
        for (std::size_t i = shift; i < two.size(); ++i) {
            const double sum = static_cast<double>(one[i]) + one[i - shift];
            diffSum = std::max(diffSum, std::abs(two[i] - sum));
            diffReset = std::max(diffReset, std::abs(static_cast<double>(two[i]) - one[i - shift]));
        }
        INFO("gap " << gap << " max diff vs linear sum " << diffSum << ", vs reset-and-replay " << diffReset);
        CHECK(diffSum > 0.02);    // the circuit is nonlinear: hits interact
        CHECK(diffReset > 0.02);  // and the old ring is still there
        CHECK(maxStep(two, fs, gap, gap + 0.003) < 1.3 * maxStep(one, fs, 0.0, 0.003) + 1e-4);
    }
}

TEST_CASE("BD: robustness fuzz", "[bd][fuzz]") {
    std::uint32_t seed = 11;
    for (double fs : kRates) fuzzRobustness<BassDrum>(prep, fs, seed++, 20.0);
    fuzzRobustness<BassDrum>(prep, 48000.0, 99, 25.0, 1.0f);
}

TEST_CASE("BD: deterministic; unit variance moves pitch and decay within tolerance", "[bd][determinism]") {
    checkDeterministic<BassDrum>(prep, 48000.0);
    checkSeedIgnoredAtZeroVariance<BassDrum>(prep, 48000.0, stock());

    const double fs = 48000.0;
    double fMin = 1e9, fMax = 0.0, tMin = 1e9, tMax = 0.0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        VoiceParams p = stock();
        p.decay = 0.3f;
        const auto x = render<BassDrum>(prep, setupAt(fs, seed, 1.0f), p, {{0.0, Hit::normal()}}, 3.0);
        const double f = zeroCrossFreq(x, fs, 0.3, 0.5);
        const double t60 = t60Fit(x, fs, 0.02, -3.0, -30.0);
        INFO("seed " << seed << " pitch " << f << " T60 " << t60);
        REQUIRE(allFinite(x));
        // +-20 % capacitors and +-5 % resistors: f ~ 1/sqrt(R R C C).
        CHECK(f > kPitch / 1.27);
        CHECK(f < kPitch * 1.27);
        CHECK(t60 > 0.15);
        CHECK(t60 < 7.5);
        fMin = std::min(fMin, f);
        fMax = std::max(fMax, f);
        tMin = std::min(tMin, t60);
        tMax = std::max(tMax, t60);
    }
    INFO("pitch " << fMin << ".." << fMax << " Hz, T60 " << tMin << ".." << tMax << " s");
    CHECK(fMax / fMin > 1.03);
    CHECK(tMax / tMin > 1.1);
}

TEST_CASE("BD: against reference recordings of a real unit", "[bd][reference]") {
    struct Ref {
        const char* file;
        float tone, decay;
    };
    const Ref refs[] = {{"BD5000.WAV", 0.5f, 0.0f},  {"BD5025.WAV", 0.5f, 0.25f}, {"BD5050.WAV", 0.5f, 0.5f},
                        {"BD5075.WAV", 0.5f, 0.75f}, {"BD5010.WAV", 0.5f, 1.0f},  {"BD0050.WAV", 0.0f, 0.5f},
                        {"BD1050.WAV", 1.0f, 0.5f}};
    if (refPath("bd8", refs[0].file).empty()) SKIP("reference recordings not present");
    std::printf("\nBD vs reference   file        first-cycle Hz   22-60 ms Hz    settled Hz     T60 s        centroid(0-50ms) Hz\n");
    for (const Ref& r : refs) {
        Wav w;
        REQUIRE(readWav(refPath("bd8", r.file), w));
        VoiceParams p = stock();
        p.tone = r.tone;
        p.decay = r.decay;
        const auto m = hit(w.fs, p, 4.0f, 4.0);
        const bool shortNote = r.decay < 0.2f;
        const double lo = shortNote ? -25.0 : -40.0;
        const double t0 = shortNote ? 0.02 : (r.decay < 0.4f ? 0.08 : 0.25), t1 = shortNote ? 0.07 : (r.decay < 0.4f ? 0.2 : 0.6);
        const double rf1 = zeroCrossFreq(w.x, w.fs, 0.004, 0.024), mf1 = zeroCrossFreq(m, w.fs, 0.004, 0.024);
        const double rf2 = zeroCrossFreq(w.x, w.fs, 0.020, 0.060), mf2 = zeroCrossFreq(m, w.fs, 0.020, 0.060);
        const double rfs = zeroCrossFreq(w.x, w.fs, t0, t1), mfs = zeroCrossFreq(m, w.fs, t0, t1);
        const double rt = t60Fit(w.x, w.fs, 0.0202, -3.0, lo), mt = t60Fit(m, w.fs, 0.0202, -3.0, lo);
        const double rc = spectrum(w.x, w.fs, 0.0, 0.05).centroid(), mc = spectrum(m, w.fs, 0.0, 0.05).centroid();
        std::printf("  %-12s ref/model   %5.1f / %5.1f   %5.1f / %5.1f   %5.2f / %5.2f   %5.2f / %5.2f   %5.0f / %5.0f\n", r.file,
                    rf1, mf1, rf2, mf2, rfs, mfs, rt, mt, rc, mc);
        INFO(r.file);
        CHECK_THAT(mf1, WithinRel(rf1, 0.06));
        // The shortest setting (Q 2.3) is a few heavily damped cycles; zero
        // crossings say little there.
        CHECK_THAT(mf2, WithinRel(rf2, shortNote ? 0.10 : 0.05));
        if (!shortNote) {
            CHECK_THAT(mfs, WithinRel(rfs, 0.012));
            // The knobs on the reference unit were set by hand: its "5" spans 0.95..1.6 s.
            CHECK_THAT(mt, WithinRel(rt, 0.45));
        }
        CHECK_THAT(mc, WithinRel(rc, 0.25));
    }
}

TEST_CASE("BD: listening renders", "[.render]") {
    const double fs = 48000.0;
    std::vector<float> all;
    appendWithGap(all, hit(fs, stock(), 4.0f, 1.2), fs);
    appendWithGap(all, hit(fs, stock(), 14.0f, 1.2), fs);
    appendWithGap(all, hit(fs, stock(), 2.2f, 1.2), fs);
    for (float decay : {0.0f, 0.25f, 0.75f, 1.0f}) {
        VoiceParams p = stock();
        p.decay = decay;
        appendWithGap(all, hit(fs, p, 4.0f, decay > 0.7f ? 3.5 : 1.0), fs);
    }
    for (float tone : {0.0f, 1.0f}) {
        VoiceParams p = stock();
        p.tone = tone;
        appendWithGap(all, hit(fs, p, 9.0f, 1.2), fs);
    }
    for (float shape : {0.0f, 1.0f}) {
        VoiceParams p = stock();
        p.shape = shape;
        p.decay = 0.8f;
        appendWithGap(all, hit(fs, p, 9.0f, 2.0), fs);
    }
    {
        // A chromatic bass line and a roll, to hear the state carry over.
        VoiceParams p = stock();
        p.decay = 0.7f;
        std::vector<Event> ev;
        const float notes[] = {0, 0, 3, 5, -2, 0, 7, 12, 10, 7, 5, 3};
        for (int i = 0; i < 12; ++i) ev.push_back({0.22 * i, Hit{i % 4 == 0 ? 11.0f : 4.0f, notes[i]}});
        for (int i = 0; i < 16; ++i) ev.push_back({2.8 + 0.06 * i, Hit{3.0f + 0.6f * static_cast<float>(i), 0.0f}});
        appendWithGap(all, render<BassDrum>(prep, setupAt(fs), p, ev, 6.0), fs);
    }
    const std::string path = renderDir() + "/bd.wav";
    REQUIRE(writeWav(path, all, fs));
    std::printf("wrote %s\n", path.c_str());
}

TEST_CASE("BD: performance", "[.bench]") {
    VoiceParams p = stock();
    p.decay = 1.0f;
    const double ns = benchNsPerSample<BassDrum>(prep, 48000.0, p, 0.25);
    std::printf("BD: %.1f ns/sample at 48 kHz (budget 150)\n", ns);
    CHECK(ns < 150.0);
}

// ---------------------------------------------------------------- review fixes

TEST_CASE("BD: non-finite controls cannot poison the voice", "[bd][fuzz]") {
    for (double fs : {44100.0, 192000.0}) checkNonFiniteParams<BassDrum>(prep, fs);
}

TEST_CASE("BD: sleeps only once inaudible", "[bd][sleep]") {
    for (double fs : {44100.0, 192000.0}) {
        VoiceParams p = stock();
        checkQuietBeforeSleep<BassDrum>(prep, fs, p, 4.0f, 0.025);
        p.decay = 0.0f;
        p.tune = 12.0f;
        checkQuietBeforeSleep<BassDrum>(prep, fs, p, 14.0f, 0.015);
        p.decay = 1.0f;
        p.tune = -12.0f;
        checkQuietBeforeSleep<BassDrum>(prep, fs, p, 0.5f, 0.045);
    }
}

TEST_CASE("BD: the highest chromatic notes with full SWEEP still die away", "[bd][sleep]") {
    // tune +12 plus a +36 semitone hit is a 790 Hz ring. With the leak taken
    // at the explicitly predicted junction voltage alone, a full-SWEEP hit at
    // 22.05 kHz (and some units at 48 kHz) settled into a never-ending
    // oscillation at -1 dBFS instead of decaying.
    for (double fs : {22050.0, 48000.0}) {
        for (std::uint32_t unit : {0U, 77U, 1234U}) {
            for (float decay : {0.5f, 1.0f}) {
                Rig<BassDrum> rig(prep, setupAt(fs, unit, unit == 0U ? 0.0f : 1.0f));
                rig.voice.setParams(VoiceParams{12.0f, decay, 0.5f, 1.0f});
                rig.voice.trigger(Hit{14.0f, 36.0f});
                std::vector<float> x;
                while (rig.voice.isActive() && x.size() < static_cast<std::size_t>(20.0 * fs)) rig.run(x, 4096, 4096);
                const double end = static_cast<double>(x.size()) / fs;
                const double lastTenth = rms(x, fs, end - 0.1, end);
                INFO("fs " << fs << " unit " << unit << " decay " << decay << " ran " << static_cast<double>(x.size()) / fs
                           << " s, last 0.1 s rms " << lastTenth);
                CHECK_FALSE(rig.voice.isActive());
                // T60 <= 6.9 s by construction (units whose loop sits past the
                // threshold are clamped there), so -130 dB comes within 16 s.
                CHECK(x.size() < static_cast<std::size_t>(16.0 * fs));
                CHECK(lastTenth < 1.0e-5);  // decayed, not stuck (it sat at 0.5 rms)
            }
        }
    }
}

TEST_CASE("BD: an automated TONE jump glides instead of clicking", "[bd][params]") {
    const double fs = 48000.0;
    VoiceParams dark = stock(), bright = stock();
    dark.tone = 0.0f;
    bright.tone = 1.0f;
    Rig<BassDrum> rig(prep, setupAt(fs));
    rig.voice.setParams(dark);
    rig.voice.trigger(Hit::normal());
    std::vector<float> x;
    rig.run(x, static_cast<int>(0.15 * fs));
    rig.voice.setParams(bright);
    rig.run(x, static_cast<int>(0.05 * fs));
    const double jump = maxStep(x, fs, 0.15, 0.155);
    const double natural = maxStep(x, fs, 0.10, 0.15);
    INFO("largest step after the jump " << jump << ", before it " << natural);
    CHECK(jump < 1.5 * natural);  // without the glide: about 9x
}

TEST_CASE("BD: a corrupt unit variance is clamped, not propagated as NaN", "[bd][fuzz]") {
    // Before the fix a NaN or infinite variance made every part NaN and the
    // voice emitted NaN without ever sleeping; variance 10 drove capacitors
    // negative.
    checkBadVariance<BassDrum>(prep, 48000.0);
}

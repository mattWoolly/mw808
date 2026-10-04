// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "BridgedTestUtil.h"
#include "mw808/voices/TomConga.h"

using namespace bridged_test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using mw808::VoiceId;
using mw808::voices::TomConga;

namespace {

struct Spec {
    VoiceId id;
    const char* code;
    const char* refDir;
    double c1, c2;                 // farads (schematic)
    double low, mid, high;         // service-manual tuning chart, Hz
    double minus20;                // service-manual decay, seconds
    double vppNormal, vppAccent;   // service-manual levels
    bool tom;
};

const Spec kSpecs[] = {
    {mw808::LT, "LT", "lt8", 56.0e-9, 59.0e-9, 80, 90, 100, 0.200, 3.5, 12, true},
    {mw808::MT, "MT", "mt8", 35.2e-9, 39.0e-9, 120, 135, 160, 0.130, 3.0, 11, true},
    {mw808::HT, "HT", "ht8", 27.0e-9, 27.6e-9, 165, 185, 220, 0.100, 3.5, 12, true},
    {mw808::LC, "LC", "lc8", 56.0e-9, 12.0e-9, 165, 185, 220, 0.180, 3.5, 12, false},
    {mw808::MC, "MC", "mc8", 27.0e-9, 12.0e-9, 250, 280, 310, 0.100, 3.0, 10, false},
    {mw808::HC, "HC", "hc8", 27.0e-9, 5.6e-9, 370, 400, 455, 0.080, 3.5, 12, false},
};

// Small-signal junction resistance at TUNING position `pot` (0 = fully
// anticlockwise, low pitch): R231 1k, bypassed by the upper part of the 500 ohm
// pot in series with the germanium pair (zero-bias: 300 ohm + 30 mV / 1 uA,
// halved for the pair; fitted to the reference unit), plus the lower part.
double junctionOhms(double pot) {
    const double below = 500.0 * (1.0 - pot), above = 500.0 * pot;
    const double branch = 0.5 * (above + 300.0 + 0.030 / 1.0e-6);
    return 1000.0 * branch / (1000.0 + branch) + below;
}

// Semitone offsets of the ends of the hardware TUNING pot from its centre.
const double kPotLow = 6.0 * std::log2(junctionOhms(0.5) / junctionOhms(0.0));
const double kPotHigh = 6.0 * std::log2(junctionOhms(0.5) / junctionOhms(1.0));

// Small-signal pitch from the schematic at a TUNING position.
double schematicPitch(const Spec& s, double pot) {
    const double r1 = junctionOhms(pot);
    const double rp = r1 * 820.0e3 / (r1 + 820.0e3);
    return 1.0 / (mw808::dsp::kTwoPi * std::sqrt(rp * 820.0e3 * s.c1 * s.c2));
}

// Ring time constant with the fitted inverter gain 0.96 (research/01 §0.2).
double derivedTau(const Spec& s) {
    const double d = (s.c1 + s.c2 - 0.96 * s.c1) / (820.0e3 * s.c1 * s.c2);
    return 2.0 / d;
}

auto prepFor(VoiceId id) {
    return [id](TomConga& v, const VoiceSetup& s) { v.prepare(s, id); };
}

VoiceParams with(float tune, float decay = 0.5f, float noise = 0.5f) {
    VoiceParams p;
    p.tune = tune;
    p.decay = decay;
    p.shape = noise;
    return p;
}

std::vector<float> hit(const Spec& s, double fs, const VoiceParams& p, float volts = 4.0f, double seconds = 1.5) {
    return renderHit<TomConga>(prepFor(s.id), fs, p, volts, seconds);
}

// Pitch of the tail, where the diode clamp has let go. Noise off so the zero
// crossings are clean.
double settledPitch(const Spec& s, double fs, float tune, float volts = 4.0f) {
    const auto x = hit(s, fs, with(tune, 0.5f, 0.0f), volts);
    const double tau = derivedTau(s);
    return zeroCrossFreq(x, fs, 2.5 * tau, 5.0 * tau);
}

double earlyPitch(const Spec& s, double fs, float tune, float volts = 4.0f) {
    const auto x = hit(s, fs, with(tune, 0.5f, 0.0f), volts);
    return zeroCrossFreq(x, fs, 0.004, 0.045);
}

}  // namespace

TEST_CASE("TOM: pitch at the pot centre and ends matches schematic and manual, at every rate", "[tom][pitch]") {
    for (const Spec& s : kSpecs) {
        for (double fs : kRates) {
            const double centre = settledPitch(s, fs, 0.0f);
            const double lowEnd = settledPitch(s, fs, static_cast<float>(kPotLow));
            const double highEnd = settledPitch(s, fs, static_cast<float>(kPotHigh));
            const double scope = earlyPitch(s, fs, 0.0f);  // what a scope on the first cycles reads
            INFO(s.code << " fs " << fs << " centre " << centre << " (early " << scope << ", manual " << s.mid << ") range "
                        << lowEnd << ".." << highEnd << " (manual " << s.low << ".." << s.high << ")");
            CHECK_THAT(centre, WithinRel(schematicPitch(s, 0.5), 0.006));
            CHECK_THAT(lowEnd, WithinRel(schematicPitch(s, 0.0), 0.006));
            CHECK_THAT(highEnd, WithinRel(schematicPitch(s, 1.0), 0.008));
            // The manual's LC row repeats the HT row (research/03 flags it as
            // suspect); the schematic and the reference unit (185/200/225 Hz)
            // both put the low conga higher.
            const bool suspect = s.id == mw808::LC;
            CHECK_THAT(scope, WithinRel(s.mid, suspect ? 0.08 : 0.05));
            CHECK_THAT(lowEnd, WithinRel(s.low, suspect ? 0.10 : 0.07));
            CHECK_THAT(highEnd, WithinRel(s.high, 0.08));
        }
    }
}

TEST_CASE("TOM: the +-12 semitone travel covers the hardware range; +12 doubles the pitch", "[tom][pitch][params]") {
    const double fs = 48000.0;
    for (const Spec& s : kSpecs) {
        const double f0 = settledPitch(s, fs, 0.0f, 1.0f);
        const double up = settledPitch(s, fs, 12.0f, 1.0f);
        const double down = settledPitch(s, fs, -12.0f, 1.0f);
        INFO(s.code << " " << down << " / " << f0 << " / " << up);
        CHECK_THAT(up, WithinRel(2.0 * f0, 0.01));
        CHECK_THAT(down, WithinRel(0.5 * f0, 0.01));
        CHECK(down < s.low);
        CHECK(up > s.high);
        // Monotonic across the travel.
        double prev = 0.0;
        for (float t = -12.0f; t <= 12.0f; t += 3.0f) {
            const double f = settledPitch(s, fs, t, 1.0f);
            CHECK(f > prev);
            prev = f;
        }
    }
}

TEST_CASE("TOM: decay matches the manual, does not depend on tuning or sample rate", "[tom][decay]") {
    for (const Spec& s : kSpecs) {
        const double period = 1.0 / s.mid;
        double ref = 0.0;
        for (double fs : kRates) {
            const auto x = hit(s, fs, with(0.0f, 0.5f, 0.0f));
            const double t60 = t60Fit(x, fs, period);
            const double t20 = timeToMinus20dB(x, fs, period);
            INFO(s.code << " fs " << fs << " T60 " << t60 << " (derived " << 6.908 * derivedTau(s) << ") -20 dB at " << t20 * 1e3
                        << " ms (manual " << s.minus20 * 1e3 << ")");
            CHECK_THAT(t60, WithinRel(6.908 * derivedTau(s), 0.05));
            CHECK_THAT(t20, WithinRel(s.minus20, 0.15));
            if (fs == kRates[0]) ref = t60;
            CHECK_THAT(t60, WithinRel(ref, 0.02));
        }
        const double fs = 48000.0;
        const double mid = t60Fit(hit(s, fs, with(0.0f, 0.5f, 0.0f)), fs, period);
        // Tuning never changes the decay time...
        for (float tune : {static_cast<float>(kPotLow), static_cast<float>(kPotHigh), -12.0f, 12.0f}) {
            const double t = t60Fit(hit(s, fs, with(tune, 0.5f, 0.0f)), fs, period / std::exp2(tune / 12.0));
            INFO(s.code << " tune " << tune << " T60 " << t << " vs " << mid);
            CHECK_THAT(t, WithinRel(mid, 0.05));
        }
        // ...the (non-hardware) DECAY knob does: x0.25 .. x4.
        CHECK_THAT(t60Fit(hit(s, fs, with(0.0f, 0.0f, 0.0f)), fs, period, -3.0, -30.0), WithinRel(0.25 * mid, 0.10));
        CHECK_THAT(t60Fit(hit(s, fs, with(0.0f, 1.0f, 0.0f), 4.0f, 5.0), fs, period), WithinRel(4.0 * mid, 0.05));
    }
}

TEST_CASE("TOM: the diode clamp bends the pitch down as the note decays, more on accents", "[tom][pitch]") {
    const double fs = 48000.0;
    for (const Spec& s : kSpecs) {
        // Hardware pot fully clockwise: the diodes have the most to bypass.
        const float tune = static_cast<float>(kPotHigh);
        auto bend = [&](float volts) { return earlyPitch(s, fs, tune, volts) / settledPitch(s, fs, tune, volts) - 1.0; };
        const double weak = bend(1.0f), normal = bend(4.0f), accent = bend(14.0f);
        INFO(s.code << " bend: 1 V " << weak * 100 << " %, 4 V " << normal * 100 << " %, 14 V " << accent * 100 << " %");
        CHECK(normal > weak);
        CHECK(accent > 1.5 * normal);
        CHECK(accent > 0.01);
        CHECK(accent < 0.25);
        // Reference recordings: about 4 % for the toms, under 1 % for the congas.
        if (s.tom) CHECK_THAT(normal, WithinAbs(0.035, 0.025));
        else CHECK(normal < 0.015);
    }
}

TEST_CASE("TOM: levels follow the service-manual table and rise monotonically with volts", "[tom][level]") {
    for (const Spec& s : kSpecs) {
        for (double fs : kRates) {
            const auto normal = hit(s, fs, with(0.0f), 4.0f);
            const auto accent = hit(s, fs, with(0.0f), 14.0f);
            INFO(s.code << " fs " << fs << " normal " << vpp(normal) << " (manual " << s.vppNormal << ") accent " << vpp(accent)
                        << " (manual " << s.vppAccent << ")");
            // One output gain and one accent law serve all six voices, so each
            // lands near, not on, its "typical" table entry.
            CHECK_THAT(vpp(normal), WithinRel(s.vppNormal, 0.20));
            CHECK_THAT(vpp(accent), WithinRel(s.vppAccent, 0.20));
            // Without the noise tail the level is exactly repeatable across rates.
            CHECK_THAT(vpp(hit(s, fs, with(0.0f, 0.5f, 0.0f))), WithinRel(vpp(hit(s, 44100.0, with(0.0f, 0.5f, 0.0f))), 0.02));
        }
        checkMonotonicLevel<TomConga>(prepFor(s.id), 48000.0, with(0.0f), 0.8);
    }
}

TEST_CASE("TOM: toms carry a band-limited noise tail, congas none", "[tom][params]") {
    const double fs = 48000.0;
    for (const Spec& s : kSpecs) {
        const auto dry = hit(s, fs, with(0.0f, 0.5f, 0.0f));
        const auto stock = hit(s, fs, with(0.0f, 0.5f, 0.5f));
        const auto loud = hit(s, fs, with(0.0f, 0.5f, 1.0f));
        std::vector<float> noise(dry.size()), noise4(dry.size());
        for (std::size_t i = 0; i < dry.size(); ++i) {
            noise[i] = stock[i] - dry[i];
            noise4[i] = loud[i] - dry[i];
        }
        if (!s.tom) {
            CHECK(peakAbs(noise) == 0.0);
            CHECK(peakAbs(noise4) == 0.0);
            continue;
        }
        const auto sp = spectrum(noise, fs, 0.0, 0.4);
        const double inBand = sp.bandPower(100, 900), below = sp.bandPower(0, 50), above = sp.bandPower(2000, 20000);
        const double rel = 20.0 * std::log10(rms(noise, fs, 0.05, 0.35) / rms(dry, fs, 0.05, 0.35));
        const double early = rms(noise, fs, 0.02, 0.06), late = rms(noise, fs, 0.16, 0.20);
        const double tau = 0.14 / std::log(early / late);
        INFO(s.code << " noise centroid " << sp.centroid() << " Hz, level re tone " << rel << " dB, tau " << tau * 1e3 << " ms");
        CHECK(sp.centroid() > 150.0);
        CHECK(sp.centroid() < 700.0);
        CHECK(db(below / inBand) < -15.0);
        CHECK(db(above / inBand) < -25.0);
        CHECK(rel < -8.0);   // "barely perceptible"
        CHECK(rel > -32.0);
        // Envelope: C_env through 2.2M: 86 ms (LT) / 73 ms (MT, HT).
        CHECK_THAT(tau, WithinRel(s.id == mw808::LT ? 0.086 : 0.073, 0.30));
        // SHAPE at maximum: four times the stock amount (until the VCA overloads).
        CHECK_THAT(rms(noise4, fs, 0.1, 0.3) / rms(noise, fs, 0.1, 0.3), WithinRel(4.0, 0.15));
        // Accent raises the noise about as much as the tone.
        const auto dryA = hit(s, fs, with(0.0f, 0.5f, 0.0f), 14.0f), wetA = hit(s, fs, with(0.0f, 0.5f, 0.5f), 14.0f);
        std::vector<float> noiseA(dryA.size());
        for (std::size_t i = 0; i < dryA.size(); ++i) noiseA[i] = wetA[i] - dryA[i];
        const double noiseUp = rms(noiseA, fs, 0.05, 0.3) / rms(noise, fs, 0.05, 0.3);
        const double toneUp = rms(dryA, fs, 0.05, 0.3) / rms(dry, fs, 0.05, 0.3);
        INFO("accent: noise x" << noiseUp << ", tone x" << toneUp);
        CHECK(noiseUp > 0.75 * toneUp);
        CHECK(noiseUp < 1.5 * toneUp);
    }
}

TEST_CASE("TOM: retrigger excites the ringing state and never clicks", "[tom][retrigger]") {
    const double fs = 48000.0;
    for (const Spec& s : kSpecs) {
        const VoiceParams p = with(static_cast<float>(kPotHigh), 0.5f, 0.0f);
        const double gap = 0.031;
        const auto one = hit(s, fs, p, 9.0f, 1.0);
        const auto two = render<TomConga>(prepFor(s.id), setupAt(fs), p, {{0.0, Hit{9.0f, 0.0f}}, {gap, Hit{9.0f, 0.0f}}}, 1.0);
        const std::size_t shift = static_cast<std::size_t>(std::lround(gap * fs));
        double diffSum = 0.0, diffReset = 0.0;
        for (std::size_t i = shift; i < two.size(); ++i) {
            diffSum = std::max(diffSum, std::abs(static_cast<double>(two[i]) - (static_cast<double>(one[i]) + one[i - shift])));
            diffReset = std::max(diffReset, std::abs(static_cast<double>(two[i]) - one[i - shift]));
        }
        INFO(s.code << " max diff vs linear sum " << diffSum << ", vs reset-and-replay " << diffReset << ", peak " << peakAbs(one));
        CHECK(diffSum > 0.01 * peakAbs(one));   // the diode clamp makes the hits interact
        CHECK(diffReset > 0.1 * peakAbs(one));  // the first ring is still there
        CHECK(maxStep(two, fs, gap, gap + 0.003) < 1.3 * maxStep(one, fs, 0.0, 0.003) + 1e-4);
    }
}

TEST_CASE("TOM: robustness fuzz", "[tom][fuzz]") {
    std::uint32_t seed = 31;
    for (const Spec& s : kSpecs) {
        for (double fs : {44100.0, 192000.0}) fuzzRobustness<TomConga>(prepFor(s.id), fs, seed++, 20.0);
        fuzzRobustness<TomConga>(prepFor(s.id), 48000.0, seed++, 25.0, 1.0f);
    }
    fuzzRobustness<TomConga>(prepFor(mw808::MT), 96000.0, seed++, 20.0);
}

TEST_CASE("TOM: deterministic; unit variance moves pitch and decay within tolerance", "[tom][determinism]") {
    const double fs = 48000.0;
    for (const Spec& s : kSpecs) {
        checkDeterministic<TomConga>(prepFor(s.id), fs);
        checkSeedIgnoredAtZeroVariance<TomConga>(prepFor(s.id), fs, with(0.0f));
        double fMin = 1e9, fMax = 0.0, tMin = 1e9, tMax = 0.0;
        for (std::uint32_t seed = 1; seed <= 12; ++seed) {
            const auto x = render<TomConga>(prepFor(s.id), setupAt(fs, seed, 1.0f), with(0.0f, 0.5f, 0.0f), {{0.0, Hit::normal()}}, 4.0);
            const double f = zeroCrossFreq(x, fs, 0.02, 0.12);
            const double t60 = t60Fit(x, fs, 1.0 / f, -3.0, -30.0);
            INFO(s.code << " seed " << seed << " pitch " << f << " T60 " << t60);
            REQUIRE(allFinite(x));
            CHECK(f > schematicPitch(s, 0.5) / 1.27);
            CHECK(f < schematicPitch(s, 0.5) * 1.3);
            // The regeneration balance is tolerance-sensitive (most of all in
            // the congas), as in the hardware; the model caps T60 at 8 s.
            CHECK(t60 > 0.3 * 6.908 * derivedTau(s));
            CHECK(t60 < 8.5);
            fMin = std::min(fMin, f);
            fMax = std::max(fMax, f);
            tMin = std::min(tMin, t60);
            tMax = std::max(tMax, t60);
        }
        INFO(s.code << " pitch " << fMin << ".." << fMax << " T60 " << tMin << ".." << tMax);
        CHECK(fMax / fMin > 1.03);
        CHECK(tMax / tMin > 1.05);
    }
}

TEST_CASE("TOM: against reference recordings of a real unit", "[tom][reference]") {
    if (refPath("lt8", "LT50.WAV").empty()) SKIP("reference recordings not present");
    // File digits are the TUNING knob position: 00, 25, 50, 75, 10 (= 10.0).
    const struct {
        const char* digits;
        double pot;
    } positions[] = {{"00", 0.0}, {"25", 0.25}, {"50", 0.5}, {"75", 0.75}, {"10", 1.0}};
    std::printf("\nTOM vs reference  file       early Hz         late Hz          T60 s          centroid Hz    noise re tone dB\n");
    for (const Spec& s : kSpecs) {
        for (const auto& pos : positions) {
            const std::string file = std::string(s.code) + pos.digits + ".WAV";
            Wav w;
            REQUIRE(readWav(refPath(s.refDir, file), w));
            // Pot position -> junction resistance -> semitones from the centre.
            const float tune = static_cast<float>(6.0 * std::log2(junctionOhms(0.5) / junctionOhms(pos.pot)));
            const auto m = hit(s, w.fs, with(tune), 4.0f, 1.0);
            const double tau = derivedTau(s);
            auto early = [&](const std::vector<float>& x) { return peakFreq(x, w.fs, 0.6 * s.low, 1.4 * s.high, 0.003, 0.06); };
            auto late = [&](const std::vector<float>& x) { return peakFreq(x, w.fs, 0.6 * s.low, 1.4 * s.high, 1.5 * tau, 4.0 * tau); };
            auto t60 = [&](const std::vector<float>& x) { return t60Fit(x, w.fs, 1.0 / s.mid, -3.0, -35.0); };
            auto noiseRel = [&](const std::vector<float>& x) {
                // Power between the harmonics of the tone, 50..350 ms.
                const auto sp = spectrum(x, w.fs, 0.05, 0.35);
                const double f0 = late(x);
                double n = 0.0;
                for (std::size_t i = 0; i < sp.power.size(); ++i) {
                    const double f = static_cast<double>(i) * sp.binHz;
                    if (f < 120.0 || f > 1000.0) continue;
                    const double h = f / f0;
                    if (std::abs(h - std::round(h)) < 0.2) continue;
                    n += sp.power[i];
                }
                return db(n / sp.bandPower(0.85 * f0, 1.15 * f0));
            };
            const double rc = spectrum(w.x, w.fs, 0.0, 0.3).centroid(), mc = spectrum(m, w.fs, 0.0, 0.3).centroid();
            std::printf("  %-10s ref/model  %6.1f / %6.1f  %6.1f / %6.1f  %5.3f / %5.3f  %5.0f / %5.0f   %6.1f / %6.1f\n", file.c_str(),
                        early(w.x), early(m), late(w.x), late(m), t60(w.x), t60(m), rc, mc, noiseRel(w.x), noiseRel(m));
            INFO(file);
            CHECK_THAT(early(m), WithinRel(early(w.x), 0.045));
            CHECK_THAT(late(m), WithinRel(late(w.x), 0.045));
            CHECK_THAT(t60(m), WithinRel(t60(w.x), 0.15));
            CHECK_THAT(mc, WithinRel(rc, 0.12));
            if (s.tom) CHECK_THAT(noiseRel(m), WithinAbs(noiseRel(w.x), 6.0));
        }
    }
}

TEST_CASE("TOM: listening renders", "[.render]") {
    const double fs = 48000.0;
    for (const Spec& s : kSpecs) {
        std::vector<float> all;
        for (float volts : {4.0f, 14.0f, 2.2f}) appendWithGap(all, hit(s, fs, with(0.0f), volts, 0.9), fs);
        for (double tune : {kPotLow, kPotHigh, -7.0, 7.0}) appendWithGap(all, hit(s, fs, with(static_cast<float>(tune)), 6.0f, 0.9), fs);
        for (float decay : {0.0f, 1.0f}) appendWithGap(all, hit(s, fs, with(0.0f, decay), 6.0f, decay > 0.5f ? 2.5 : 0.6), fs);
        if (s.tom)
            for (float noise : {0.0f, 1.0f}) appendWithGap(all, hit(s, fs, with(0.0f, 0.5f, noise), 9.0f, 0.9), fs);
        std::vector<Event> ev;
        for (int i = 0; i < 12; ++i) ev.push_back({0.09 * i, Hit{3.0f + 0.9f * static_cast<float>(i), 0.0f}});
        appendWithGap(all, render<TomConga>(prepFor(s.id), setupAt(fs), with(0.0f), ev, 2.0), fs);
        std::string name = s.code;
        for (char& c : name) c = static_cast<char>(std::tolower(c));
        const std::string path = renderDir() + "/" + name + ".wav";
        REQUIRE(writeWav(path, all, fs));
        std::printf("wrote %s\n", path.c_str());
    }
}

TEST_CASE("TOM: performance", "[.bench]") {
    for (const Spec& s : kSpecs) {
        const double ns = benchNsPerSample<TomConga>(prepFor(s.id), 48000.0, with(0.0f, 1.0f, 1.0f), 0.15);
        std::printf("%s: %.1f ns/sample at 48 kHz (budget 150)\n", s.code, ns);
        CHECK(ns < 150.0);
    }
}

// ---------------------------------------------------------------- review fixes

TEST_CASE("TOM: non-finite controls cannot poison the voice", "[tom][fuzz]") {
    for (const Spec& s : kSpecs) checkNonFiniteParams<TomConga>(prepFor(s.id), 48000.0);
}

TEST_CASE("TOM: sleeps only once inaudible", "[tom][sleep]") {
    for (const Spec& s : kSpecs) {
        const double period = 1.0 / s.mid;
        checkQuietBeforeSleep<TomConga>(prepFor(s.id), 44100.0, with(0.0f), 4.0f, period);
        checkQuietBeforeSleep<TomConga>(prepFor(s.id), 192000.0, with(-12.0f, 1.0f, 1.0f), 14.0f, 2.0 * period);
        checkQuietBeforeSleep<TomConga>(prepFor(s.id), 48000.0, with(12.0f, 0.0f, 0.0f), 0.5f, 0.5 * period);
    }
}

TEST_CASE("TOM: hammering stays within the op-amp rails", "[tom][fuzz]") {
    // A burst of triggers about half a period long pins the resonator at a
    // rail; when it lets go, the coupling capacitor swings the mix amp's input
    // towards twice a rail. The rails limit the op-amp OUTPUTS (pulse + ring at
    // the resonator, and the mix amp itself). Unclamped this reached 3.15 (16 V).
    for (const Spec& s : kSpecs) {
        for (double fs : {48000.0, 176400.0}) {
            for (double burst : {0.005, 0.010, 0.2}) {
                Rig<TomConga> rig(prepFor(s.id), setupAt(fs));
                rig.voice.setParams(with(-12.0f, 0.5f, 1.0f));
                std::vector<float> x;
                for (int i = 0; i < static_cast<int>(burst * fs); ++i) {
                    if (i % 2 == 0) rig.voice.trigger(Hit{(i / 2) % 2 == 0 ? 14.0f : 0.2f, 0.0f});
                    rig.run(x, 1, 1);
                }
                rig.run(x, static_cast<int>(0.3 * fs));
                INFO(s.code << " fs " << fs << " burst " << burst << " peak " << peakAbs(x));
                CHECK(allFinite(x));
                CHECK(peakAbs(x) <= 13.5 * 1.033 / 5.0 + 1e-3);  // the mix amp's rail, through the output gain
            }
        }
    }
}

TEST_CASE("TOM: prepare() does not depend on earlier parameter automation", "[tom][determinism]") {
    for (const Spec& s : kSpecs) {
        const double fs = 48000.0;
        const VoiceParams p = with(0.7f);
        const auto fresh = render<TomConga>(prepFor(s.id), setupAt(fs), p, {{0.0, Hit::accented(1.0f)}}, 0.5);

        Rig<TomConga> rig(prepFor(s.id), setupAt(fs));
        std::vector<float> scratch;
        for (int i = 0; i < 200; ++i) {  // sweep TUNE as an automation lane would
            rig.voice.setParams(with(0.7f + 0.004f * static_cast<float>(i)));
            rig.run(scratch, 16, 16);
        }
        rig.voice.prepare(setupAt(fs), s.id);
        rig.bus.seed(kNoiseSeed);  // the shared noise bus is not the voice's state
        rig.voice.setParams(p);
        rig.voice.trigger(Hit::accented(1.0f));
        std::vector<float> again;
        rig.run(again, static_cast<int>(std::lround(0.5 * fs)));
        bool same = again.size() == fresh.size();
        for (std::size_t i = 0; same && i < fresh.size(); ++i) same = fresh[i] == again[i];
        INFO(s.code);
        CHECK(same);
    }
}

TEST_CASE("TOM: performance with TUNE automated every 64 samples", "[.bench]") {
    for (const Spec& s : kSpecs) {
        Rig<TomConga> rig(prepFor(s.id), setupAt(48000.0));
        std::vector<float> out(64), noise(64);
        rig.bus.fill(noise.data(), 64);
        const int blocks = 48000 * 10 / 64;
        double sink = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            if (b % 100 == 0) rig.voice.trigger(Hit::normal());
            rig.voice.setParams(with(2.0f * std::sin(0.01f * static_cast<float>(b))));  // within the pot travel
            rig.voice.render(out.data(), noise.data(), 64);
            sink += static_cast<double>(out[0]);
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() /
                          (static_cast<double>(blocks) * 64.0);
        std::printf("%s: %.1f ns/sample with TUNE automated (budget 150)\n", s.code, ns);
        CHECK(std::isfinite(sink));
        CHECK(ns < 150.0);  // rebuilding the diode table on every update cost ~185
    }
}

TEST_CASE("TOM: a corrupt unit variance is clamped, not propagated as NaN", "[tom][fuzz]") {
    // Before the fix a NaN or infinite variance made every part NaN and the
    // voice emitted NaN without ever sleeping; variance 10 drove capacitors
    // negative.
    for (const Spec& s : kSpecs) checkBadVariance<TomConga>(prepFor(s.id), 48000.0);
}

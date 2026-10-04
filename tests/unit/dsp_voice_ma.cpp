// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Maracas: high-pass corner, swelling envelope and its abrupt end, levels,
// knobs, robustness, retrigger, determinism, reference recording.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

#include "MetalTestUtil.h"
#include "mw808/dsp/Filters.h"
#include "mw808/voices/Maracas.h"

using namespace metaltest;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using Ma = Noisy<mw808::voices::Maracas>;

namespace {

// The high-pass corner as heard: the frequency at which the hit's spectrum
// (averaged over hits, smoothed over 1 kHz) is 6 dB below its level at 14 kHz.
double cornerHz(double fs, const VoiceParams& p, double scale = 1.0) {
    const Spectrum s = meanSpectrum<Ma>(fs, 1, p, 4.0f, 0.06, 8);
    auto density = [&](double f) { return s.energy(f - 500.0 * scale, f + 500.0 * scale); };
    // divide out the fixed 8.8 kHz output pole and the 7.2 kHz noise roll-off
    // so only the high-pass is measured
    auto shaped = [&](double f) {
        return density(f) * (1.0 + (f / 8770.0) * (f / 8770.0)) * (1.0 + (f / 7230.0) * (f / 7230.0));
    };
    const double top = shaped(14000.0 * scale);
    for (double f = 14000.0 * scale; f > 1000.0 * scale; f -= 50.0 * scale)
        if (shaped(f) < 0.25 * top) return f;
    return 0.0;
}

}  // namespace

TEST_CASE("MA high-pass corner holds at every sample rate, including 44.1 kHz", "[voice][ma][pitch]") {
    double c48 = 0.0;
    for (double fs : kRates) {
        const double c = cornerHz(fs, {});
        std::printf("[ma pitch] fs %.0f: -6 dB point of the high-pass %.0f Hz (analog 10.62 kHz, Q 1.0, measured this way: 7500 Hz)\n", fs, c);
        // With this measure (1 kHz smoothing, 6 dB below the 14 kHz level) the analog
        // prototype, 10.62 kHz Q 1.0 after the 1.06 kHz first-order section, gives 0.706 f0.
        REQUIRE_THAT(c, WithinRel(0.706 * 10620.0, 0.08));
        if (fs == 48000.0) c48 = c;
    }
    for (double fs : kRates) REQUIRE_THAT(cornerHz(fs, {}), WithinRel(c48, 0.05));
}

TEST_CASE("MA tune moves the high-pass", "[voice][ma][pitch][params]") {
    const double c0 = cornerHz(96000.0, {});
    const double down = cornerHz(96000.0, {-12.0f, 0.5f, 0.5f, 0.5f}, 0.5);
    const double up = cornerHz(192000.0, {12.0f, 0.5f, 0.5f, 0.5f}, 2.0);
    std::printf("[ma tune] corner: -12 st %.0f Hz, 0 st %.0f Hz, +12 st %.0f Hz\n", down, c0, up);
    REQUIRE_THAT(down / c0, WithinRel(0.5, 0.08));
    REQUIRE_THAT(up / c0, WithinRel(2.0, 0.08));
}

TEST_CASE("MA envelope: swells in, ends abruptly; 25 ms normal, 35 ms accented", "[voice][ma][decay]") {
    const double normal = meanTimeToDb<Ma>(48000.0, 1, {}, 4.0f, 0.2, -20.0, 8);
    const double accent = meanTimeToDb<Ma>(48000.0, 1, {}, 14.0f, 0.2, -20.0, 8);
    const double t40 = meanTimeToDb<Ma>(48000.0, 1, {}, 4.0f, 0.2, -40.0, 8);
    std::printf("[ma decay] -20 dB at %.1f ms normal (manual 25), %.1f ms accented (manual 35); -40 dB at %.1f ms\n", normal, accent, t40);
    REQUIRE_THAT(normal, WithinRel(25.0, 0.12));
    REQUIRE_THAT(accent, WithinRel(35.0, 0.12));
    REQUIRE(t40 - normal < 8.0);  // abrupt end
    // swell: the first 3 ms are well below the level reached after 12-18 ms
    double early = 0.0, late = 0.0;
    for (int i = 0; i < 8; ++i) {
        const auto x = oneHit<Ma>(48000.0, 1, {}, 4.0f, 0.1, kPreRolls[i]);
        early += std::pow(rms(x, 48000.0, 0.0, 0.003), 2.0);
        late += std::pow(rms(x, 48000.0, 0.012, 0.018), 2.0);
    }
    std::printf("[ma decay] attack: first 3 ms are %.1f dB below the 12-18 ms level\n", -10.0 * std::log10(early / late));
    REQUIRE(early < 0.1 * late);
    for (double fs : kRates) {
        const double t = meanTimeToDb<Ma>(fs, 1, {}, 4.0f, 0.2, -20.0, 8);
        INFO("fs " << fs << ": " << t << " ms");
        REQUIRE_THAT(t, WithinRel(normal, 0.06));
    }
    const double shortT = meanTimeToDb<Ma>(48000.0, 1, {0.0f, 0.0f, 0.5f, 0.5f}, 4.0f, 0.2, -20.0, 8);
    const double longT = meanTimeToDb<Ma>(48000.0, 1, {0.0f, 1.0f, 0.5f, 0.5f}, 4.0f, 0.5, -20.0, 8);
    std::printf("[ma decay] decay knob 0 -> %.1f ms, 1 -> %.1f ms\n", shortT, longT);
    REQUIRE_THAT(shortT, WithinRel(0.25 * normal, 0.25));
    REQUIRE_THAT(longT, WithinRel(4.0 * normal, 0.25));
}

TEST_CASE("MA levels: 3 Vpp normal, 5 Vpp accented, monotonic", "[voice][ma][level]") {
    const double normal = meanHalfVpp<Ma>(48000.0, 1, {}, 4.0f, 0.1), accent = meanHalfVpp<Ma>(48000.0, 1, {}, 14.0f, 0.1);
    std::printf("[ma level] normal %.2f Vpp (manual 3), accented %.2f Vpp (manual 5), accent %+.1f dB (manual +4.4)\n", normal * 10, accent * 10,
                toDb(accent / normal));
    REQUIRE_THAT(toDb(normal / 0.30), WithinAbs(0.0, 1.5));
    REQUIRE_THAT(toDb(accent / 0.50), WithinAbs(0.0, 1.5));
    checkMonotonicLevel([](float v) { return meanHalfVpp<Ma>(48000.0, 1, {}, v, 0.1, 8); });
    // Level in the audible band does not depend on the rate (it was +0.9 / +1.3 dB
    // at 96 / 192 kHz: the digital filters lose 10-20 kHz power near Nyquist).
    auto bandLevelDb = [](double fs) {
        const double len = 0.06;
        Spectrum sp;
        for (int h = 0; h < 48; ++h) {  // the noise varies from hit to hit: average plenty
            Ma ma;
            ma.noiseSeed = 900U + 7919U * static_cast<std::uint32_t>(h);
            accumulate(sp, spectrumOf(renderHit(ma, setupFor(fs, 1), {}, Hit::normal(), len), fs, 0.0, len));
        }
        const double n = fs / sp.binHz;
        return 10.0 * std::log10(2.0 * sp.energy(0.0, 20000.0) / (n * len * fs));
    };
    const double band48 = bandLevelDb(48000.0);
    for (double fs : kRates) {
        const double l = bandLevelDb(fs) - band48;
        std::printf("[ma level] fs %.0f: level below 20 kHz %+.2f dB re 48 kHz\n", fs, l);
        REQUIRE_THAT(l, WithinAbs(0.0, 0.3));
    }
}

TEST_CASE("MA robustness: fuzz, trigger storm, then exact silence", "[voice][ma][robust]") {
    checkRobust<Ma>(48000.0, 1, 5.0);
    checkRobust<Ma>(44100.0, 2, 5.0);
    checkRobust<Ma>(192000.0, 4, 5.0);
}

TEST_CASE("MA parameter hygiene: NaN and infinities", "[voice][ma][robust]") {
    checkNanSafe<Ma>(48000.0, 2);
    checkNanSafe<Ma>(44100.0, 1);
}

TEST_CASE("MA sleeps right after the hit", "[voice][ma][robust]") {
    Ma ma;
    ma.prepare(setupFor(48000.0));
    ma.setParams({});
    ma.trigger(Hit::normal());
    std::vector<float> buf(480);
    double seconds = 0.0;
    while (ma.isActive() && seconds < 5.0) {
        ma.render(buf.data(), 480);
        seconds += 0.01;
    }
    std::printf("[ma sleep] inactive %.2f s after a normal hit\n", seconds);
    REQUIRE(seconds < 0.3);
    ma.render(buf.data(), 480);
    for (float v : buf) REQUIRE(v == 0.0f);
}

TEST_CASE("MA retrigger extends the hold instead of restarting", "[voice][ma][retrigger]") {
    checkRetrigger<Ma>(0.01, 0.2);
    // a second hit while the gate is open: no dip, the envelope just carries on
    Ma ma;
    ma.prepare(setupFor(48000.0));
    ma.setParams({});
    std::vector<float> out;
    appendHit(ma, out, 48000.0, Hit::normal(), 0.015);
    appendHit(ma, out, 48000.0, Hit::normal(), 0.1);
    REQUIRE(rms(out, 48000.0, 0.015, 0.02) > 0.7 * rms(out, 48000.0, 0.010, 0.015));
    REQUIRE(timeToDb(out, 48000.0, -20.0) > 35.0);
}

TEST_CASE("MA determinism and unit variance", "[voice][ma][determinism]") {
    checkDeterminism<Ma>(0.2);
    double minT = 1e9, maxT = 0.0;
    const double nominal = meanTimeToDb<Ma>(48000.0, 1, {}, 4.0f, 0.2, -20.0, 8);
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        const double t = meanTimeToDb<Ma>(48000.0, 1, {}, 4.0f, 0.2, -20.0, 8, seed, 1.0f);
        minT = std::min(minT, t);
        maxT = std::max(maxT, t);
    }
    std::printf("[ma variance] six units: -20 dB at %.1f..%.1f ms (nominal %.1f)\n", minT, maxT, nominal);
    REQUIRE(maxT - minT > 0.5);
    REQUIRE(minT > nominal * 0.75);
    REQUIRE(maxT < nominal * 1.25);
}

TEST_CASE("MA against the reference recording", "[voice][ma][reference]") {
    if (!haveRef("ma8/MA.WAV")) SKIP("reference recording not present");
    Wav wav;
    REQUIRE(readWav16(refDir() + "/ma8/MA.WAV", wav));
    const double d20 = timeToDb(wav.x, wav.fs, -20.0), m20 = meanTimeToDb<Ma>(wav.fs, 1, {}, 4.0f, 0.2, -20.0, 8);
    const Spectrum sRec = spectrumOf(wav.x, wav.fs, 0.0, 0.06);
    const Spectrum sMod = meanSpectrum<Ma>(wav.fs, 1, {}, 4.0f, 0.06, 8);
    const double cRec = sRec.centroid(500.0, 22000.0), cMod = sMod.centroid(500.0, 22000.0);
    std::printf("[ma reference] -20 dB: recording %.1f ms, model %.1f ms\n", d20, m20);
    std::printf("[ma reference] centroid: recording %.0f Hz, model %.0f Hz (white noise bus; the voice applies the later units' 7.2 kHz roll-off)\n",
                cRec, cMod);
    REQUIRE_THAT(m20, WithinRel(d20, 0.12));
    REQUIRE_THAT(cMod, WithinRel(cRec, 0.08));
}

TEST_CASE("MA renders for listening", "[.render]") {
    const double fs = 48000.0;
    Ma ma;
    ma.prepare(setupFor(fs));
    std::vector<float> out;
    ma.setParams({});
    appendHit(ma, out, fs, Hit::normal(), 0.5);
    appendHit(ma, out, fs, Hit::accented(1.0f), 0.5);
    appendHit(ma, out, fs, Hit::weak(), 0.5);
    for (auto p : {VoiceParams{0.0f, 0.0f, 0.5f, 0.5f}, VoiceParams{0.0f, 1.0f, 0.5f, 0.5f}, VoiceParams{-12.0f, 0.5f, 0.5f, 0.5f},
                   VoiceParams{6.0f, 0.5f, 0.5f, 0.5f}}) {
        ma.setParams(p);
        appendHit(ma, out, fs, Hit::normal(), 0.5);
    }
    ma.setParams({});
    for (int i = 0; i < 16; ++i) appendHit(ma, out, fs, i % 4 == 2 ? Hit::accented(0.8f) : Hit::normal(), 0.125);
    appendHit(ma, out, fs, Hit::normal(), 0.5);
    writeWav16(renderDir() + "/ma.wav", out, fs, kRenderGain);
    SUCCEED("wrote ma.wav");
}

TEST_CASE("MA cost per sample", "[.bench]") {
    std::printf("[ma bench] 48 kHz: %.1f ns/sample (including the test's noise generator)\n", benchNsPerSample<Ma>(48000.0, 1, {0.0f, 1.0f, 0.5f, 0.5f}));
}

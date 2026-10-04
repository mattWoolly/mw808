// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Cowbell: oscillator pitches, band-pass, two-stage decay, levels (the largest
// accent swing of the metal voices), robustness, retrigger, determinism.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

#include "MetalTestUtil.h"
#include "mw808/dsp/MetalOscBank.h"
#include "mw808/voices/Cowbell.h"

using namespace metaltest;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using Cb = Plain<mw808::voices::Cowbell>;

namespace {
struct Lines {
    double lowHz, highHz, lowDb;  // the two tones, and the low one relative to the high one
};
Lines linesOf(const std::vector<float>& x, double fs, double scale = 1.0) {
    const Spectrum s = spectrumOf(x, fs, 0.0, 0.4);
    Lines l{};
    l.lowHz = s.peakHz(480.0 * scale, 600.0 * scale, 0.0);
    l.highHz = s.peakHz(720.0 * scale, 880.0 * scale, 0.0);
    l.lowDb = 10.0 * std::log10(s.energy(l.lowHz - 15.0, l.lowHz + 15.0) / s.energy(l.highHz - 15.0, l.highHz + 15.0));
    return l;
}
}  // namespace

TEST_CASE("CB tones are 540 and 800 Hz through an ~875 Hz band-pass", "[voice][cb][pitch]") {
    for (double fs : kRates) {
        const Lines l = linesOf(oneHit<Cb>(fs, 2, {}, 4.0f, 0.5), fs);
        std::printf("[cb pitch] fs %.0f: tones %.1f and %.1f Hz (540 / 800), low tone %.1f dB re high (circuit: about -15)\n", fs, l.lowHz,
                    l.highHz, l.lowDb);
        REQUIRE_THAT(l.lowHz, WithinRel(540.0, 0.006));
        REQUIRE_THAT(l.highHz, WithinRel(800.0, 0.006));
        REQUIRE_THAT(l.lowDb, WithinAbs(-15.0, 3.0));
    }
    const Lines one = linesOf(oneHit<Cb>(48000.0, 1, {}, 4.0f, 0.5), 48000.0);
    REQUIRE_THAT(one.highHz, WithinRel(800.0, 0.006));
    REQUIRE_THAT(one.lowDb, WithinAbs(-15.0, 3.0));
}

TEST_CASE("CB tune +12 doubles oscillators and band-pass", "[voice][cb][pitch][params]") {
    const double fs = 96000.0;
    const Lines up = linesOf(oneHit<Cb>(fs, 2, {12.0f, 0.5f, 0.5f, 0.5f}, 4.0f, 0.5), fs, 2.0);
    const Lines down = linesOf(oneHit<Cb>(fs, 2, {-12.0f, 0.5f, 0.5f, 0.5f}, 4.0f, 0.5), fs, 0.5);
    std::printf("[cb tune] +12 st: %.1f / %.1f Hz (%.1f dB); -12 st: %.1f / %.1f Hz (%.1f dB)\n", up.lowHz, up.highHz, up.lowDb, down.lowHz,
                down.highHz, down.lowDb);
    REQUIRE_THAT(up.lowHz, WithinRel(1080.0, 0.006));
    REQUIRE_THAT(up.highHz, WithinRel(1600.0, 0.006));
    REQUIRE_THAT(down.highHz, WithinRel(400.0, 0.006));
    // the band-pass moved with the oscillators: same balance between the tones
    REQUIRE_THAT(up.lowDb, WithinAbs(-15.0, 3.0));
    REQUIRE_THAT(down.lowDb, WithinAbs(-15.0, 3.0));
}

TEST_CASE("CB decay: abrupt first drop, then the tail", "[voice][cb][decay]") {
    const double t20 = meanTimeToDb<Cb>(48000.0, 2, {}, 4.0f, 1.0, -20.0, 8);
    const double t10 = meanTimeToDb<Cb>(48000.0, 2, {}, 4.0f, 1.0, -10.0, 8);
    const double t40 = meanTimeToDb<Cb>(48000.0, 2, {}, 4.0f, 1.5, -40.0, 8);
    std::printf("[cb decay] -10 dB at %.0f ms, -20 dB at %.0f ms (manual 50, recording 78), -40 dB at %.0f ms (recording 321)\n", t10, t20, t40);
    REQUIRE(t20 > 42.0);
    REQUIRE(t20 < 85.0);
    REQUIRE(t10 < 25.0);            // the first stage: about -10 dB within ~20 ms
    REQUIRE(t40 > 3.0 * t20);       // two-stage: the tail is much slower than the first drop
    const double shortT = meanTimeToDb<Cb>(48000.0, 2, {0.0f, 0.0f, 0.5f, 0.5f}, 4.0f, 1.0, -20.0, 8);
    const double longT = meanTimeToDb<Cb>(48000.0, 2, {0.0f, 1.0f, 0.5f, 0.5f}, 4.0f, 3.0, -20.0, 8);
    std::printf("[cb decay] decay knob 0 -> %.0f ms, 1 -> %.0f ms\n", shortT, longT);
    REQUIRE(shortT < 0.7 * t20);
    REQUIRE(longT > 2.5 * t20);
    for (double fs : kRates) {
        const double t = meanTimeToDb<Cb>(fs, 2, {}, 4.0f, 1.0, -20.0, 8);
        INFO("fs " << fs << ": " << t << " ms");
        REQUIRE_THAT(t, WithinRel(t20, 0.12));
    }
    // accented hits ring longer (shallower first drop)
    REQUIRE(meanTimeToDb<Cb>(48000.0, 2, {}, 14.0f, 1.0, -20.0, 8) > t20);
}

TEST_CASE("CB levels: 3.5 Vpp normal, 12 Vpp accented, monotonic", "[voice][cb][level]") {
    const double normal = meanHalfVpp<Cb>(48000.0, 2, {}, 4.0f, 0.3), accent = meanHalfVpp<Cb>(48000.0, 2, {}, 14.0f, 0.3);
    std::printf("[cb level] normal %.2f Vpp (manual 3.5), accented %.2f Vpp (manual 12), accent %+.1f dB (manual +10.7)\n", normal * 10,
                accent * 10, toDb(accent / normal));
    REQUIRE_THAT(toDb(normal / 0.35), WithinAbs(0.0, 1.0));
    REQUIRE_THAT(toDb(accent / 1.20), WithinAbs(0.0, 1.0));
    checkMonotonicLevel([](float v) { return meanHalfVpp<Cb>(48000.0, 2, {}, v, 0.3, 8); });
    for (double fs : kRates) REQUIRE_THAT(toDb(meanHalfVpp<Cb>(fs, 2, {}, 4.0f, 0.3) / normal), WithinAbs(0.0, 0.7));
    REQUIRE_THAT(toDb(meanHalfVpp<Cb>(48000.0, 1, {}, 4.0f, 0.3) / normal), WithinAbs(0.0, 0.7));
    REQUIRE_THAT(toDb(meanHalfVpp<Cb>(48000.0, 4, {}, 4.0f, 0.3) / normal), WithinAbs(0.0, 0.7));
}

TEST_CASE("CB robustness: fuzz, trigger storm, then exact silence", "[voice][cb][robust]") {
    checkRobust<Cb>(48000.0, 2, 10.0);
    checkRobust<Cb>(44100.0, 1, 10.0);
    checkRobust<Cb>(192000.0, 4, 10.0);
}

TEST_CASE("CB parameter hygiene: NaN and infinities", "[voice][cb][robust]") {
    checkNanSafe<Cb>(48000.0, 2);
    checkNanSafe<Cb>(44100.0, 1);
}

TEST_CASE("CB sleeps, wakes cleanly, and never resets its oscillators", "[voice][cb][robust][phase]") {
    const double fs = 48000.0;
    mw808::voices::Cowbell cb;
    cb.prepare(setupFor(fs, 2));
    cb.setParams({});
    mw808::dsp::MetalOscBank free;
    free.prepare(fs * 2, 0U, 0.0f);
    std::vector<float> buf(48000);
    std::uint64_t ticks = 0;
    auto run = [&](int n) {
        cb.render(buf.data(), n);
        ticks += static_cast<std::uint64_t>(n) * 2U;
    };
    run(3000);
    cb.trigger(Hit::normal());
    run(5001);
    cb.trigger(Hit::accented(1.0f));
    run(137);
    double seconds = 0.0;
    while (cb.isActive() && seconds < 10.0) {
        run(4800);
        seconds += 0.1;
    }
    std::printf("[cb sleep] inactive %.1f s after an accented hit\n", seconds);
    REQUIRE_FALSE(cb.isActive());
    REQUIRE(seconds < 3.0);
    run(12345);
    cb.trigger(Hit::normal());
    run(999);
    REQUIRE(std::abs(buf[0]) < 0.05);
    free.advance(ticks);
    for (int i = 0; i < 6; ++i) REQUIRE(cb.oscillators().phase(i) == free.phase(i));
}

TEST_CASE("CB retrigger carries the ringing state over", "[voice][cb][retrigger]") {
    checkRetrigger<Cb>(0.03, 0.5);
    checkRetrigger<Cb>(0.11, 0.6);
}

TEST_CASE("CB determinism and unit variance", "[voice][cb][determinism]") {
    checkDeterminism<Cb>(0.5);
    double minHz = 1e9, maxHz = 0.0, minDecay = 1e9, maxDecay = 0.0;
    const double nominalDecay = meanTimeToDb<Cb>(48000.0, 2, {}, 4.0f, 1.0, -20.0, 8);
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        const Spectrum s = spectrumOf(oneHit<Cb>(48000.0, 2, {}, 4.0f, 0.5, 0.0, seed, 1.0f), 48000.0, 0.0, 0.4);
        const double hz = s.peakHz(720.0, 880.0, 0.0);
        const double decay = meanTimeToDb<Cb>(48000.0, 2, {}, 4.0f, 1.0, -20.0, 8, seed, 1.0f);
        minHz = std::min(minHz, hz);
        maxHz = std::max(maxHz, hz);
        minDecay = std::min(minDecay, decay);
        maxDecay = std::max(maxDecay, decay);
    }
    std::printf("[cb variance] six units: high tone %.1f..%.1f Hz (trimmed, +-1.5 %%), -20 dB %.0f..%.0f ms (nominal %.0f)\n", minHz, maxHz,
                minDecay, maxDecay, nominalDecay);
    REQUIRE(maxHz - minHz > 1.0);
    REQUIRE(minHz > 800.0 * 0.984);
    REQUIRE(maxHz < 800.0 * 1.016);
    REQUIRE(maxDecay - minDecay > 0.03 * nominalDecay);
    REQUIRE(minDecay > 0.5 * nominalDecay);
    REQUIRE(maxDecay < 2.0 * nominalDecay);
}

TEST_CASE("CB against the reference recording", "[voice][cb][reference]") {
    if (!haveRef("cb8/CB.WAV")) SKIP("reference recording not present");
    Wav wav;
    REQUIRE(readWav16(refDir() + "/cb8/CB.WAV", wav));
    const auto model = oneHit<Cb>(wav.fs, 2, {}, 4.0f, static_cast<double>(wav.x.size()) / wav.fs);
    const Lines rec = linesOf(wav.x, wav.fs, 1.03), mod = linesOf(model, wav.fs);
    const Spectrum sRec = spectrumOf(wav.x, wav.fs), sMod = spectrumOf(model, wav.fs);
    const double t20 = meanTimeToDb<Cb>(wav.fs, 2, {}, 4.0f, 1.0, -20.0, 8), t40 = meanTimeToDb<Cb>(wav.fs, 2, {}, 4.0f, 1.5, -40.0, 8);
    std::printf("[cb reference] tones: recording %.0f / %.0f Hz (low %.1f dB), model %.0f / %.0f Hz (low %.1f dB)\n", rec.lowHz, rec.highHz,
                rec.lowDb, mod.lowHz, mod.highHz, mod.lowDb);
    std::printf("[cb reference] -20 dB: recording %.0f ms, model %.0f ms; -40 dB: %.0f / %.0f ms; centroid %.0f / %.0f Hz\n",
                timeToDb(wav.x, wav.fs, -20.0), t20, timeToDb(wav.x, wav.fs, -40.0), t40, sRec.centroid(100.0, 20000.0),
                sMod.centroid(100.0, 20000.0));
    // the recorded unit's trimmers sit 3 % above the factory figures
    REQUIRE_THAT(mod.highHz, WithinRel(rec.highHz, 0.05));
    REQUIRE_THAT(mod.lowHz, WithinRel(rec.lowHz, 0.05));
    REQUIRE_THAT(mod.lowDb, WithinAbs(rec.lowDb, 4.0));
    REQUIRE_THAT(t20, WithinRel(timeToDb(wav.x, wav.fs, -20.0), 0.40));
    REQUIRE_THAT(t40, WithinRel(timeToDb(wav.x, wav.fs, -40.0), 0.35));
    REQUIRE_THAT(sMod.centroid(100.0, 20000.0), WithinRel(sRec.centroid(100.0, 20000.0), 0.15));
}

TEST_CASE("CB renders for listening", "[.render]") {
    const double fs = 48000.0;
    Cb cb;
    cb.prepare(setupFor(fs, 2));
    std::vector<float> out;
    cb.setParams({});
    appendHit(cb, out, fs, Hit::normal(), 1.0);
    appendHit(cb, out, fs, Hit::accented(1.0f), 1.0);
    appendHit(cb, out, fs, Hit::weak(), 1.0);
    for (auto p : {VoiceParams{0.0f, 0.0f, 0.5f, 0.5f}, VoiceParams{0.0f, 1.0f, 0.5f, 0.5f}, VoiceParams{-7.0f, 0.5f, 0.5f, 0.5f},
                   VoiceParams{5.0f, 0.5f, 0.5f, 0.5f}}) {
        cb.setParams(p);
        appendHit(cb, out, fs, Hit::normal(), 1.2);
    }
    cb.setParams({});
    for (int i = 0; i < 16; ++i) appendHit(cb, out, fs, i % 4 == 0 ? Hit::accented(0.9f) : Hit::normal(), 0.125);
    appendHit(cb, out, fs, Hit::normal(), 1.0);
    writeWav16(renderDir() + "/cb.wav", out, fs, kRenderGain);
    SUCCEED("wrote cb.wav");
}

TEST_CASE("CB cost per sample", "[.bench]") {
    for (int os : {1, 2, 4})
        std::printf("[cb bench] 48 kHz, oversample %d: %.1f ns/sample\n", os, benchNsPerSample<Cb>(48000.0, os, {0.0f, 1.0f, 0.5f, 0.5f}));
}

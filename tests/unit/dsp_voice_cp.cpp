// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Hand clap: band-pass centre, burst timing, decay, levels, tone / reverb /
// decay knobs, robustness, retrigger, determinism, reference recording.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

#include "MetalTestUtil.h"
#include "mw808/dsp/Filters.h"
#include "mw808/voices/HandClap.h"

using namespace metaltest;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using Cp = Noisy<mw808::voices::HandClap>;

namespace {

// Band-pass centre as heard: the middle (geometric) of the -3 dB points of the
// burst spectrum, averaged over hits and smoothed.
template <class A = Cp>
double centreHz(double fs, const VoiceParams& p, double scale = 1.0) {
    const Spectrum s = meanSpectrum<A>(fs, 1, p, 4.0f, 0.2, 8, 0.0, 0.045);
    return s.peakHz(400.0 * scale, 4000.0 * scale, 350.0 * scale);
}

// r.m.s. envelope in 1 ms steps (dB re its maximum), power-averaged over 16 hits.
std::vector<double> burstEnvelope(double fs, const VoiceParams& p, double seconds = 0.2, std::uint32_t seed = 0, float variance = 0.0f) {
    const auto hop = static_cast<std::size_t>(fs / 1000.0);
    std::vector<double> acc;
    for (int h = 0; h < 16; ++h) {
        Cp cp;
        cp.noiseSeed = 1000U + static_cast<std::uint32_t>(h) * 7919U;
        const auto x = renderHit(cp, setupFor(fs, 1, seed, variance), p, Hit::normal(), seconds);
        if (acc.empty()) acc.assign(x.size() / hop, 0.0);
        for (std::size_t i = 0; i < acc.size(); ++i)
            for (std::size_t k = 0; k < hop; ++k) acc[i] += static_cast<double>(x[i * hop + k]) * x[i * hop + k];
    }
    const double peak = *std::max_element(acc.begin(), acc.end());
    for (auto& v : acc) v = 10.0 * std::log10(std::max(v / peak, 1e-12));
    return acc;
}

// Times (ms) of the three re-attacks after the first burst: the largest rises of the envelope.
std::vector<double> reattacks(const std::vector<double>& envDb) {
    std::vector<double> t;
    for (std::size_t i = 4; i < 45 && i + 2 < envDb.size(); ++i)
        if (envDb[i + 2] - envDb[i] > 9.0 && (t.empty() || static_cast<double>(i + 1) - t.back() > 5.0)) t.push_back(static_cast<double>(i + 1));
    return t;
}

}  // namespace

TEST_CASE("CP band-pass sits near 1.27 kHz at every sample rate", "[voice][cp][pitch]") {
    double c48 = 0.0;
    for (double fs : kRates) {
        const double c = centreHz(fs, {});
        std::printf("[cp pitch] fs %.0f: burst spectrum peak %.0f Hz (band-pass 1.27 kHz, Q 1.5)\n", fs, c);
        REQUIRE_THAT(c, WithinRel(1270.0, 0.15));
        if (fs == 48000.0) c48 = c;
    }
    for (double fs : kRates) REQUIRE_THAT(centreHz(fs, {}), WithinRel(c48, 0.08));
}

TEST_CASE("CP tone and tune move the band-pass", "[voice][cp][pitch][params]") {
    const double c0 = centreHz(96000.0, {});
    const double toneUp = centreHz(96000.0, {0.0f, 0.5f, 1.0f, 0.5f}, 2.0), toneDown = centreHz(96000.0, {0.0f, 0.5f, 0.0f, 0.5f}, 0.5);
    const double tuneUp = centreHz(96000.0, {12.0f, 0.5f, 0.5f, 0.5f}, 2.0), tuneDown = centreHz(96000.0, {-12.0f, 0.5f, 0.5f, 0.5f}, 0.5);
    std::printf("[cp tone] centre: tone 0 / 0.5 / 1 = %.0f / %.0f / %.0f Hz; tune -12 / +12 = %.0f / %.0f Hz\n", toneDown, c0, toneUp, tuneDown, tuneUp);
    REQUIRE_THAT(toneUp / c0, WithinRel(2.0, 0.12));
    REQUIRE_THAT(toneDown / c0, WithinRel(0.5, 0.12));
    REQUIRE_THAT(tuneUp / c0, WithinRel(2.0, 0.12));
    REQUIRE_THAT(tuneDown / c0, WithinRel(0.5, 0.12));
}

TEST_CASE("CP bursts at 0 / 11 / 22 / 30 ms, then the free decay", "[voice][cp][decay]") {
    for (double fs : kRates) {
        const auto env = burstEnvelope(fs, {});
        const auto t = reattacks(env);
        REQUIRE(t.size() == 3);
        // depth of the dip before each re-attack
        const double dip1 = *std::min_element(env.begin() + 5, env.begin() + static_cast<std::ptrdiff_t>(t[0]) + 1);
        const double dip2 = *std::min_element(env.begin() + static_cast<std::ptrdiff_t>(t[0]) + 3, env.begin() + static_cast<std::ptrdiff_t>(t[1]) + 1);
        std::printf("[cp bursts] fs %.0f: re-attacks at %.0f / %.0f / %.0f ms (manual drawing 10.8 / 22.1 / 29.7), dips %.0f / %.0f dB\n", fs,
                    t[0], t[1], t[2], dip1, dip2);
        REQUIRE_THAT(t[0], WithinAbs(10.9, 1.6));
        REQUIRE_THAT(t[1], WithinAbs(21.9, 1.6));
        REQUIRE_THAT(t[2], WithinAbs(30.0, 1.6));
        REQUIRE(dip1 < -17.0);
        REQUIRE(dip2 < -17.0);
        // the last burst decays much more slowly than the first three
        REQUIRE(env[static_cast<std::size_t>(t[2]) + 10] > -8.0);
    }
}

TEST_CASE("CP decay: -20 dB near 100 ms, same at every rate; decay knob scales the tail", "[voice][cp][decay][params]") {
    const double t48 = meanTimeToDb<Cp>(48000.0, 1, {}, 4.0f, 1.0, -20.0, 8);
    std::printf("[cp decay] -20 dB at %.0f ms (manual 100, recording 89)\n", t48);
    REQUIRE_THAT(t48, WithinRel(100.0, 0.20));
    for (double fs : kRates) {
        const double t = meanTimeToDb<Cp>(fs, 1, {}, 4.0f, 1.0, -20.0, 8);
        INFO("fs " << fs << ": " << t << " ms");
        REQUIRE_THAT(t, WithinRel(t48, 0.12));
    }
    const double shortT = meanTimeToDb<Cp>(48000.0, 1, {0.0f, 0.0f, 0.5f, 0.5f}, 4.0f, 1.0, -20.0, 8);
    const double longT = meanTimeToDb<Cp>(48000.0, 1, {0.0f, 1.0f, 0.5f, 0.5f}, 4.0f, 2.0, -20.0, 8);
    std::printf("[cp decay] decay knob 0 -> %.0f ms, 1 -> %.0f ms\n", shortT, longT);
    REQUIRE(shortT < 0.6 * t48);   // the 30 ms of bursts do not shrink
    REQUIRE(longT > 2.0 * t48);
}

TEST_CASE("CP reverb knob sets the dark tail", "[voice][cp][params]") {
    auto tailDb = [](float shape) {
        double tail = 0.0, body = 0.0;
        for (int i = 0; i < 8; ++i) {
            const auto x = oneHit<Cp>(48000.0, 1, {0.0f, 0.5f, 0.5f, shape}, 4.0f, 0.5, kPreRolls[i]);
            tail += std::pow(rms(x, 48000.0, 0.20, 0.30), 2.0);
            body += std::pow(rms(x, 48000.0, 0.0, 0.045), 2.0);
        }
        return 10.0 * std::log10(tail / body);
    };
    const double none = tailDb(0.0f), stock = tailDb(0.5f), full = tailDb(1.0f);
    std::printf("[cp reverb] tail (200-300 ms) re the bursts: reverb 0 -> %.1f dB, 0.5 -> %.1f dB, 1 -> %.1f dB\n", none, stock, full);
    REQUIRE(stock > none + 6.0);
    REQUIRE(full > stock + 6.0);
    // the reverb path is low-passed (884 Hz): the tail is darker than the bursts
    const auto x = oneHit<Cp>(48000.0, 1, {0.0f, 0.5f, 0.5f, 1.0f}, 4.0f, 0.5);
    REQUIRE(spectrumOf(x, 48000.0, 0.2, 0.3).centroid(100.0, 20000.0) < 0.75 * spectrumOf(x, 48000.0, 0.0, 0.045).centroid(100.0, 20000.0));
}

// Regression: the reverb knee used to scale with the hit, so an accented clap
// was the normal one turned up. A harder hit now keeps Q70 on for longer.
TEST_CASE("CP accent lengthens the reverb tail, not only the level", "[voice][cp][level]") {
    auto tailDb = [](float volts) {
        double bursts = 0.0, tail = 0.0;
        for (int h = 0; h < 32; ++h) {
            Cp cp;
            cp.noiseSeed = 500U + 7919U * static_cast<std::uint32_t>(h);
            const auto x = renderHit(cp, setupFor(48000.0, 1), {}, Hit{volts, 0.0f}, 0.6);
            bursts += rms(x, 48000.0, 0.0, 0.04) * rms(x, 48000.0, 0.0, 0.04);
            tail += rms(x, 48000.0, 0.4, 0.6) * rms(x, 48000.0, 0.4, 0.6);
        }
        return 10.0 * std::log10(tail / bursts);
    };
    const double weak = tailDb(2.2f), normal = tailDb(4.0f), accent = tailDb(14.0f);
    std::printf("[cp accent] reverb tail (400-600 ms) re the bursts: weak %.1f dB, normal %.1f dB, accented %.1f dB\n", weak, normal, accent);
    REQUIRE(accent > normal + 2.0);
    REQUIRE(weak < normal - 2.0);
}

TEST_CASE("CP levels: 6 Vpp normal, 12 Vpp accented (chosen), monotonic", "[voice][cp][level]") {
    const double normal = meanHalfVpp<Cp>(48000.0, 1, {}, 4.0f, 0.3), accent = meanHalfVpp<Cp>(48000.0, 1, {}, 14.0f, 0.3);
    std::printf("[cp level] normal %.2f Vpp (manual 6), accented %.2f Vpp (manual prints 2, a misprint; 12 chosen)\n", normal * 10, accent * 10);
    REQUIRE_THAT(toDb(normal / 0.60), WithinAbs(0.0, 1.5));
    REQUIRE_THAT(toDb(accent / 1.20), WithinAbs(0.0, 1.5));
    checkMonotonicLevel([](float v) { return meanHalfVpp<Cp>(48000.0, 1, {}, v, 0.3, 4); });
    // The noise is scaled to a constant density and the filters' shortfall near
    // Nyquist is made up, so the level in the audible band does not depend on the rate.
    auto bandLevelDb = [](double fs) {
        const double len = 0.06;
        Spectrum sum;
        for (int h = 0; h < 24; ++h) {
            Cp cp;
            cp.noiseSeed = 300U + 7919U * static_cast<std::uint32_t>(h);
            accumulate(sum, spectrumOf(renderHit(cp, setupFor(fs, 1), {}, Hit::normal(), len), fs, 0.0, len));
        }
        const double n = fs / sum.binHz;
        return 10.0 * std::log10(2.0 * sum.energy(0.0, 20000.0) / (n * len * fs));
    };
    const double band48 = bandLevelDb(48000.0);
    for (double fs : kRates) {
        const double l = bandLevelDb(fs) - band48;
        std::printf("[cp level] fs %.0f: level below 20 kHz %+.2f dB re 48 kHz\n", fs, l);
        REQUIRE_THAT(l, WithinAbs(0.0, 0.3));
    }
}

TEST_CASE("CP robustness: fuzz, trigger storm, then exact silence", "[voice][cp][robust]") {
    checkRobust<Cp>(48000.0, 1, 15.0);
    checkRobust<Cp>(44100.0, 2, 15.0);
    checkRobust<Cp>(192000.0, 4, 15.0);
}

TEST_CASE("CP parameter hygiene: NaN and infinities", "[voice][cp][robust]") {
    checkNanSafe<Cp>(48000.0, 2);
    checkNanSafe<Cp>(44100.0, 1);
}

TEST_CASE("CP sleeps after the tail", "[voice][cp][robust]") {
    Cp cp;
    cp.prepare(setupFor(48000.0));
    cp.setParams({});
    REQUIRE_FALSE(cp.isActive());
    cp.trigger(Hit::normal());
    std::vector<float> buf(4800);
    double seconds = 0.0;
    while (cp.isActive() && seconds < 10.0) {
        cp.render(buf.data(), 4800);
        seconds += 0.1;
    }
    std::printf("[cp sleep] inactive %.1f s after a normal hit\n", seconds);
    REQUIRE(seconds < 3.0);
    cp.render(buf.data(), 4800);
    for (float v : buf) REQUIRE(v == 0.0f);
}

// Wake from sleep: a sleeping voice is not rendered, so its band-pass would
// start from zero. It is warmed up on noise of the same density at wake; the
// first burst must not depend on whether, or how long, the voice slept.
TEST_CASE("CP first burst after sleep equals the first burst of a fresh voice", "[voice][cp][robust]") {
    const double fs = 48000.0;
    auto burstDb = [&](double sleepSeconds) {
        double e = 0.0;
        for (int h = 0; h < 64; ++h) {
            Cp cp;
            cp.noiseSeed = 77U + 7919U * static_cast<std::uint32_t>(h);
            cp.prepare(setupFor(fs, 1));
            std::vector<float> buf(static_cast<std::size_t>(fs * 0.003));
            if (sleepSeconds > 0.0) {
                cp.trigger(Hit::normal());
                std::vector<float> tail(4800);
                while (cp.isActive()) cp.render(tail.data(), 4800);
                for (double t = 0.0; t < sleepSeconds; t += 0.1) cp.render(tail.data(), 4800);
            }
            cp.trigger(Hit::normal());
            cp.render(buf.data(), static_cast<int>(buf.size()));
            for (float v : buf) e += static_cast<double>(v) * v;
        }
        return toDb(std::sqrt(e));
    };
    const double fresh = burstDb(0.0), slept = burstDb(0.5), sleptLong = burstDb(3.0);
    std::printf("[cp wake] first 3 ms: fresh %.2f dB, after 0.5 s asleep %+.2f dB, after 3 s %+.2f dB\n", fresh, slept - fresh, sleptLong - fresh);
    REQUIRE_THAT(slept, WithinAbs(fresh, 0.5));
    REQUIRE_THAT(sleptLong, WithinAbs(fresh, 0.5));
}

TEST_CASE("CP retrigger restarts the burst train without resetting the filter", "[voice][cp][retrigger]") {
    checkRetrigger<Cp>(0.05, 0.6);
    checkRetrigger<Cp>(0.012, 0.4);
}

TEST_CASE("CP determinism and unit variance", "[voice][cp][determinism]") {
    checkDeterminism<Cp>(0.5);
    double minT = 1e9, maxT = 0.0;
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        const auto t = reattacks(burstEnvelope(48000.0, {}, 0.2, seed, 1.0f));
        REQUIRE(t.size() >= 2);
        minT = std::min(minT, t[0]);
        maxT = std::max(maxT, t[0]);
    }
    std::printf("[cp variance] six units: second burst at %.0f..%.0f ms (nominal 10.9; the timing capacitor is +-20 %%)\n", minT, maxT);
    REQUIRE(maxT - minT >= 1.0);
    REQUIRE(minT > 10.9 * 0.70);
    REQUIRE(maxT < 10.9 * 1.25);
}

TEST_CASE("CP against the reference recording", "[voice][cp][reference]") {
    if (!haveRef("cp8/CP.WAV")) SKIP("reference recording not present");
    Wav wav;
    REQUIRE(readWav16(refDir() + "/cp8/CP.WAV", wav));
    // burst times of the recording, same detector
    const auto hop = static_cast<std::size_t>(wav.fs / 1000.0);
    std::vector<double> env(200, 0.0);
    for (std::size_t i = 0; i < env.size(); ++i)
        for (std::size_t k = 0; k < hop; ++k) env[i] += static_cast<double>(wav.x[i * hop + k]) * wav.x[i * hop + k];
    const double peak = *std::max_element(env.begin(), env.end());
    for (auto& v : env) v = 10.0 * std::log10(std::max(v / peak, 1e-12));
    const auto tRec = reattacks(env), tMod = reattacks(burstEnvelope(wav.fs, {}));
    REQUIRE(tRec.size() == 3);
    REQUIRE(tMod.size() == 3);
    const double d20 = timeToDb(wav.x, wav.fs, -20.0), m20 = meanTimeToDb<Cp>(wav.fs, 1, {}, 4.0f, 1.0, -20.0, 8);
    const double d40 = timeToDb(wav.x, wav.fs, -40.0), m40 = meanTimeToDb<Cp>(wav.fs, 1, {}, 4.0f, 1.5, -40.0, 8);
    const Spectrum sRec = spectrumOf(wav.x, wav.fs, 0.0, 0.3);
    const Spectrum sMod = meanSpectrum<Cp>(wav.fs, 1, {}, 4.0f, 0.3, 8);
    const double cRec = sRec.centroid(100.0, 20000.0), cMod = sMod.centroid(100.0, 20000.0);
    const double pRec = spectrumOf(wav.x, wav.fs, 0.0, 0.045).peakHz(400.0, 4000.0, 350.0);
    std::printf("[cp reference] re-attacks: recording %.0f / %.0f / %.0f ms, model %.0f / %.0f / %.0f ms\n", tRec[0], tRec[1], tRec[2], tMod[0],
                tMod[1], tMod[2]);
    std::printf("[cp reference] -20 dB: %.0f / %.0f ms; -40 dB: %.0f / %.0f ms (recording / model)\n", d20, m20, d40, m40);
    std::printf("[cp reference] centroid: recording %.0f Hz, model %.0f Hz (with the later units' 7.2 kHz noise roll-off); burst peak %.0f / %.0f Hz\n",
                cRec, cMod, pRec, centreHz(wav.fs, {}));
    for (int i = 0; i < 3; ++i) REQUIRE_THAT(tMod[static_cast<std::size_t>(i)], WithinAbs(tRec[static_cast<std::size_t>(i)], 2.0));
    REQUIRE_THAT(m20, WithinRel(d20, 0.30));
    REQUIRE_THAT(m40, WithinRel(d40, 0.30));
    REQUIRE_THAT(cMod, WithinRel(cRec, 0.22));  // still ~19 % brighter: see HandClap.h
    REQUIRE_THAT(centreHz(wav.fs, {}), WithinRel(pRec, 0.35));
}

TEST_CASE("CP renders for listening", "[.render]") {
    const double fs = 48000.0;
    Cp cp;
    cp.prepare(setupFor(fs));
    std::vector<float> out;
    cp.setParams({});
    appendHit(cp, out, fs, Hit::normal(), 1.0);
    appendHit(cp, out, fs, Hit::accented(1.0f), 1.0);
    appendHit(cp, out, fs, Hit::weak(), 1.0);
    for (auto p : {VoiceParams{0.0f, 0.5f, 0.0f, 0.5f}, VoiceParams{0.0f, 0.5f, 1.0f, 0.5f}, VoiceParams{0.0f, 0.5f, 0.5f, 0.0f},
                   VoiceParams{0.0f, 0.5f, 0.5f, 1.0f}, VoiceParams{0.0f, 0.0f, 0.5f, 0.5f}, VoiceParams{0.0f, 1.0f, 0.5f, 0.5f}}) {
        cp.setParams(p);
        appendHit(cp, out, fs, Hit::normal(), 1.5);
    }
    cp.setParams({});
    for (int i = 0; i < 8; ++i) appendHit(cp, out, fs, i % 2 == 1 ? Hit::accented(0.8f) : Hit::normal(), 0.25);
    appendHit(cp, out, fs, Hit::normal(), 1.0);
    writeWav16(renderDir() + "/cp.wav", out, fs, kRenderGain);
    SUCCEED("wrote cp.wav");
}

TEST_CASE("CP cost per sample", "[.bench]") {
    std::printf("[cp bench] 48 kHz: %.1f ns/sample (including the test's noise generator)\n", benchNsPerSample<Cp>(48000.0, 1, {0.0f, 1.0f, 0.5f, 1.0f}));
}

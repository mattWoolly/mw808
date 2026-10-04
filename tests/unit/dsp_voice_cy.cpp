// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Cymbal: band-pass tuning, decay against the service manual and the reference
// recordings, levels, knobs, robustness, retrigger, determinism, aliasing.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

#include "MetalTestUtil.h"
#include "mw808/dsp/MetalOscBank.h"
#include "mw808/voices/Cymbal.h"

using namespace metaltest;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using Cy = Plain<mw808::voices::Cymbal>;

namespace {

std::vector<float> hit(double fs, int os, VoiceParams p, float volts, double seconds, double pre = 0.0,
                       std::uint32_t seed = 0, float variance = 0.0f) {
    Cy cy;
    return renderHit(cy, setupFor(fs, os, seed, variance), p, Hit{volts, 0.0f}, seconds, pre);
}

// Mean half-Vpp over a few hit times (the level depends on where the free-running
// oscillators happen to be).
double meanLevel(float volts, VoiceParams p = VoiceParams{}, int hits = 8) {
    const double pres[8] = {0.0, 0.0113, 0.0237, 0.0391, 0.0533, 0.0717, 0.0871, 0.1003};
    double sum = 0.0;
    for (int i = 0; i < hits; ++i) sum += halfVpp(hit(48000.0, 2, p, volts, 0.4, pres[i]));
    return sum / hits;
}

// Mean -20 dB time over a few hit times (the peak that sets the reference
// level depends on the oscillator phases).
double meanDecay(double fs, int os, VoiceParams p, double seconds, int hits = 4, std::uint32_t seed = 0, float variance = 0.0f) {
    const double pres[4] = {0.0, 0.0113, 0.0237, 0.0391};
    double sum = 0.0;
    for (int i = 0; i < hits; ++i) sum += timeToDb(hit(fs, os, p, 4.0f, seconds, pres[i], seed, variance), fs, -20.0);
    return sum / hits;
}

double earlyRmsDb(double fs, int os, VoiceParams p = VoiceParams{}) {
    const double pres[4] = {0.0, 0.0113, 0.0237, 0.0391};
    double sum = 0.0;
    for (double pre : pres) {
        const auto x = hit(fs, os, p, 4.0f, 0.3, pre);
        sum += rms(x, fs, 0.0, 0.2) * rms(x, fs, 0.0, 0.2);
    }
    return toDb(std::sqrt(sum / 4.0));
}

// Low band-pass (BP#1) and high band-pass (BP#2) ringing frequencies as heard at the output.
double lowBandPeak(const std::vector<float>& x, double fs, double scale = 1.0) {
    return spectrumOf(x, fs, 0.1, 0.6).peakHz(2500.0 * scale, 5000.0 * scale, 150.0 * scale);
}
double highBandPeak(const std::vector<float>& x, double fs, double scale = 1.0) {
    return spectrumOf(x, fs, 0.0, 0.03).peakHz(5500.0 * scale, 9000.0 * scale, 300.0 * scale);
}

}  // namespace

TEST_CASE("CY band-pass peaks sit at 3.44 and 7.1 kHz at every sample rate", "[voice][cy][pitch]") {
    double low48 = 0.0, high48 = 0.0;
    for (double fs : kRates) {
        const double low = lowBandPeak(hit(fs, 2, {0.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 0.7), fs);
        const double high = highBandPeak(hit(fs, 2, {0.0f, 0.5f, 1.0f, 0.5f}, 4.0f, 0.1), fs);
        std::printf("[cy pitch] fs %.0f: low band peak %.0f Hz (BP#1 3435), high band peak %.0f Hz (BP#2 7117)\n", fs, low, high);
        // the output peak sits a little above the band-pass centre (high-passes and the pulse shape)
        REQUIRE_THAT(low, WithinRel(3435.0, 0.10));
        REQUIRE_THAT(high, WithinRel(7117.0, 0.08));
        if (fs == 48000.0) {
            low48 = low;
            high48 = high;
        }
    }
    for (double fs : kRates) {
        REQUIRE_THAT(lowBandPeak(hit(fs, 2, {0.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 0.7), fs), WithinRel(low48, 0.03));
        REQUIRE_THAT(highBandPeak(hit(fs, 2, {0.0f, 0.5f, 1.0f, 0.5f}, 4.0f, 0.1), fs), WithinRel(high48, 0.04));
    }
    // oversample 1 (polyBLEP only) still lands on the same peaks
    REQUIRE_THAT(lowBandPeak(hit(48000.0, 1, {0.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 0.7), 48000.0), WithinRel(low48, 0.03));
}

TEST_CASE("CY tune +12 doubles oscillators and filters", "[voice][cy][pitch][params]") {
    const double fs = 96000.0;
    const double low0 = lowBandPeak(hit(fs, 2, {0.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 0.7), fs);
    const double low12 = lowBandPeak(hit(fs, 2, {12.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 0.7), fs, 2.0);
    const double lowM12 = lowBandPeak(hit(fs, 2, {-12.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 0.7), fs, 0.5);
    std::printf("[cy tune] low band peak: -12 st %.0f Hz, 0 st %.0f Hz, +12 st %.0f Hz\n", lowM12, low0, low12);
    REQUIRE_THAT(low12 / low0, WithinRel(2.0, 0.04));
    REQUIRE_THAT(lowM12 / low0, WithinRel(0.5, 0.04));
    mw808::voices::Cymbal cy;
    cy.prepare(setupFor(fs, 2));
    cy.setParams({12.0f, 0.5f, 0.5f, 0.5f});
    for (int i = 0; i < 6; ++i)
        REQUIRE_THAT(cy.oscillators().frequencyHz(i), WithinRel(2.0 * mw808::dsp::MetalOscBank::kNominalHz[static_cast<std::size_t>(i)], 1e-12));
}

TEST_CASE("CY decay: service manual at tone 0, reference unit at tone 0.5", "[voice][cy][decay]") {
    // The DECAY knob works on the low band. With TONE fully down that band is all
    // there is, and the -20 dB times are the service manual's 350 / 800 / 1200 ms.
    // With TONE at 12 o'clock the short high band sets the peak, so the -20 dB
    // point comes earlier: the reference recordings give 291 / 479 / 654 ms.
    const double manual[3] = {350.0, 800.0, 1200.0};
    const double recorded[3] = {291.0, 479.0, 654.0};
    const float knob[3] = {0.0f, 0.5f, 1.0f};
    for (int k = 0; k < 3; ++k) {
        const double body = meanDecay(48000.0, 2, {0.0f, knob[k], 0.0f, 0.5f}, 4.0);
        const double stock = meanDecay(48000.0, 2, {0.0f, knob[k], 0.5f, 0.5f}, 4.0);
        std::printf("[cy decay] decay %.1f: -20 dB at %.0f ms with tone 0 (manual %.0f), %.0f ms with tone 0.5 (recorded unit %.0f)\n",
                    knob[k], body, manual[k], stock, recorded[k]);
        REQUIRE_THAT(body, WithinRel(manual[k], 0.15));
        REQUIRE_THAT(stock, WithinRel(recorded[k], 0.20));
    }
}

// Level must not depend on the sample rate or the oversampling factor (the
// integrator's +-0.3 dB). Before the exact collector update, the bandwidth-
// matched band-passes and the finite-rate correction, the voice was 2.6 dB
// quieter at oversample 1 and 1.2 dB louder at 192 kHz than at 48 kHz x 2.
TEST_CASE("CY decay and level hold across sample rates and oversampling", "[voice][cy][decay][level]") {
    for (float tone : {0.0f, 0.5f, 1.0f}) {
        const VoiceParams p{0.0f, 0.5f, tone, 0.5f};
        const double level48 = earlyRmsDb(48000.0, 2, p);
        for (double fs : kRates) {
            for (int os : {1, 2, 4}) {
                const double l = earlyRmsDb(fs, os, p);
                std::printf("[cy rates] tone %.1f fs %.0f oversample %d: level of the first 200 ms %+.2f dB re 48 kHz x 2\n", tone, fs, os,
                            l - level48);
                REQUIRE_THAT(l, WithinAbs(level48, 0.3));
            }
        }
    }
    // decay, by the steadier r.m.s.-envelope measure
    const double decay48 = ensembleTimeToDb<Cy>(48000.0, 2, {0.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 1.6);
    for (double fs : kRates) {
        for (int os : {1, 2, 4}) {
            if (os != 2 && fs != 48000.0) continue;
            const double d = ensembleTimeToDb<Cy>(fs, os, {0.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 1.6);
            std::printf("[cy rates] fs %.0f oversample %d: r.m.s. -20 dB (tone 0) %.0f ms (48 kHz x 2: %.0f)\n", fs, os, d, decay48);
            REQUIRE_THAT(d, WithinRel(decay48, 0.04));
        }
    }
}

TEST_CASE("CY levels: 3.5 Vpp normal, 7 Vpp accented, monotonic in between", "[voice][cy][level]") {
    const double normal = meanLevel(4.0f), accent = meanLevel(14.0f);
    std::printf("[cy level] normal %.2f Vpp (manual 3.5), accented %.2f Vpp (manual 7), accent %+.1f dB\n", normal * 10.0,
                accent * 10.0, toDb(accent / normal));
    REQUIRE_THAT(toDb(normal / 0.35), WithinAbs(0.0, 1.0));
    REQUIRE_THAT(toDb(accent / 0.70), WithinAbs(0.0, 1.0));
    checkMonotonicLevel([](float v) { return meanLevel(v, VoiceParams{}, 4); });
}

TEST_CASE("CY accent changes the timbre, not only the level", "[voice][cy][level]") {
    const auto soft = hit(48000.0, 2, {}, 4.0f, 1.0), hard = hit(48000.0, 2, {}, 14.0f, 1.0);
    const double cSoft = spectrumOf(soft, 48000.0, 0.0, 0.1).centroid(500.0, 20000.0);
    const double cHard = spectrumOf(hard, 48000.0, 0.0, 0.1).centroid(500.0, 20000.0);
    std::printf("[cy accent] centroid of the first 100 ms: normal %.0f Hz, accented %.0f Hz\n", cSoft, cHard);
    REQUIRE(std::abs(cHard - cSoft) > 0.01 * cSoft);
}

TEST_CASE("CY knobs: decay lengthens, tone brightens and sharpens the attack", "[voice][cy][params]") {
    double last = 0.0;
    for (float d : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const double t = meanDecay(48000.0, 2, {0.0f, d, 0.0f, 0.5f}, 4.0);
        REQUIRE(t > last);
        last = t;
    }
    double lastCentroid = 0.0;
    for (float tone : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto x = hit(48000.0, 2, {0.0f, 0.5f, tone, 0.5f}, 4.0f, 1.0);
        const double c = spectrumOf(x, 48000.0, 0.0, 0.2).centroid(200.0, 20000.0);
        INFO("tone " << tone << " centroid " << c);
        REQUIRE(c > lastCentroid);
        lastCentroid = c;
    }
    // tone 0: the low band swells in (peak after several ms); tone 1: immediate
    const auto dark = hit(48000.0, 2, {0.0f, 0.5f, 0.0f, 0.5f}, 4.0f, 0.3);
    const auto bright = hit(48000.0, 2, {0.0f, 0.5f, 1.0f, 0.5f}, 4.0f, 0.3);
    REQUIRE(rms(dark, 48000.0, 0.0, 0.002) < 0.3 * rms(dark, 48000.0, 0.02, 0.04));
    REQUIRE(rms(bright, 48000.0, 0.0, 0.004) > 0.8 * rms(bright, 48000.0, 0.02, 0.04));
}

TEST_CASE("CY robustness: fuzz, trigger storm, then exact silence", "[voice][cy][robust]") {
    checkRobust<Cy>(48000.0, 2, 20.0);
    checkRobust<Cy>(44100.0, 1, 20.0);
    checkRobust<Cy>(96000.0, 4, 20.0);
    checkRobust<Cy>(192000.0, 2, 20.0);
}

TEST_CASE("CY parameter hygiene: NaN and infinities", "[voice][cy][robust]") {
    checkNanSafe<Cy>(48000.0, 2);
    checkNanSafe<Cy>(44100.0, 1);
}

TEST_CASE("CY sleeps after the tail and wakes without a click", "[voice][cy][robust]") {
    Cy cy;
    const double fs = 48000.0;
    cy.prepare(setupFor(fs, 2));
    cy.setParams({});
    REQUIRE_FALSE(cy.isActive());
    cy.trigger(Hit::normal());
    REQUIRE(cy.isActive());
    std::vector<float> buf(4800);
    double seconds = 0.0;
    while (cy.isActive() && seconds < 20.0) {
        cy.render(buf.data(), 4800);
        seconds += 0.1;
    }
    std::printf("[cy sleep] inactive %.1f s after a normal hit at stock settings\n", seconds);
    REQUIRE_FALSE(cy.isActive());
    REQUIRE(seconds < 8.0);
    cy.trigger(Hit::normal());
    cy.render(buf.data(), 4800);
    REQUIRE(std::abs(buf[0]) < 0.05);  // starts from the oscillators' band-passed level, no jump
    REQUIRE(peakAbs(buf) > 0.1);
}

TEST_CASE("CY retrigger carries the ringing state over", "[voice][cy][retrigger]") {
    checkRetrigger<Cy>(0.12, 1.0);
    checkRetrigger<Cy>(0.031, 0.6);
}

TEST_CASE("CY oscillators are never reset: phases ignore triggers and sleep", "[voice][cy][phase]") {
    const double fs = 48000.0;
    mw808::voices::Cymbal cy;
    cy.prepare(setupFor(fs, 2));
    cy.setParams({});
    mw808::dsp::MetalOscBank free;
    free.prepare(fs * 2, 0U, 0.0f);
    std::vector<float> buf(48000);
    std::uint64_t ticks = 0;
    auto run = [&](int n) {
        cy.render(buf.data(), n);
        ticks += static_cast<std::uint64_t>(n) * 2U;
    };
    run(3000);  // asleep from the start
    cy.trigger(Hit::normal());
    run(7001);
    cy.trigger(Hit::accented(1.0f));
    run(137);
    cy.trigger(Hit::weak());
    for (int i = 0; i < 12 && cy.isActive(); ++i) run(48000);  // ring out, fall asleep mid-block
    REQUIRE_FALSE(cy.isActive());
    run(12345);  // asleep
    cy.trigger(Hit::normal());  // wake (rewinds and replays a few ms internally)
    run(999);
    free.advance(ticks);
    for (int i = 0; i < 6; ++i) REQUIRE(cy.oscillators().phase(i) == free.phase(i));
}

TEST_CASE("CY determinism and unit variance", "[voice][cy][determinism]") {
    checkDeterminism<Cy>(0.5);
    // different units: band-pass tuning and decay move, inside what +-20 % capacitors allow
    const VoiceParams body{0.0f, 0.5f, 0.0f, 0.5f};
    const double nominalPeak = lowBandPeak(hit(48000.0, 2, body, 4.0f, 0.7), 48000.0);
    const double nominalDecay = meanDecay(48000.0, 2, body, 4.0);
    double minPeak = 1e9, maxPeak = 0.0, minDecay = 1e9, maxDecay = 0.0;
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        const double peak = spectrumOf(hit(48000.0, 2, body, 4.0f, 0.7, 0.0, seed, 1.0f), 48000.0, 0.1, 0.6).peakHz(2200.0, 5500.0, 150.0);
        const double decay = meanDecay(48000.0, 2, body, 4.0, 4, seed, 1.0f);
        minPeak = std::min(minPeak, peak);
        maxPeak = std::max(maxPeak, peak);
        minDecay = std::min(minDecay, decay);
        maxDecay = std::max(maxDecay, decay);
    }
    std::printf("[cy variance] six units: low band peak %.0f..%.0f Hz (nominal %.0f), -20 dB %.0f..%.0f ms (nominal %.0f)\n",
                minPeak, maxPeak, nominalPeak, minDecay, maxDecay, nominalDecay);
    REQUIRE(maxPeak - minPeak > 0.01 * nominalPeak);
    REQUIRE(maxDecay - minDecay > 0.02 * nominalDecay);
    REQUIRE(minPeak > nominalPeak / 1.30);
    REQUIRE(maxPeak < nominalPeak * 1.30);
    REQUIRE(minDecay > nominalDecay * 0.6);
    REQUIRE(maxDecay < nominalDecay * 1.5);
}

TEST_CASE("CY against the reference recordings", "[voice][cy][reference]") {
    if (!haveRef("cy8/CY5050.WAV")) SKIP("reference recordings not present");
    struct Row { const char* file; float tone, decay; };
    const Row rows[] = {{"CY0000", 0.0f, 0.0f}, {"CY0050", 0.0f, 0.5f}, {"CY0010", 0.0f, 1.0f},
                        {"CY2550", 0.25f, 0.5f}, {"CY5000", 0.5f, 0.0f}, {"CY5050", 0.5f, 0.5f},
                        {"CY5010", 0.5f, 1.0f}, {"CY1000", 1.0f, 0.0f}, {"CY1050", 1.0f, 0.5f}};
    std::printf("[cy reference] file    : -20 dB ms (rec / model), centroid Hz (rec / model), strongest band Hz (rec / model)\n");
    for (const Row& r : rows) {
        Wav wav;
        REQUIRE(readWav16(refDir() + "/cy8/" + r.file + ".WAV", wav));
        const double seconds = static_cast<double>(wav.x.size()) / wav.fs;
        const auto model = hit(wav.fs, 2, {0.0f, r.decay, r.tone, 0.5f}, 4.0f, seconds);
        const double tRec = timeToDb(wav.x, wav.fs, -20.0), tMod = meanDecay(wav.fs, 2, {0.0f, r.decay, r.tone, 0.5f}, seconds);
        const Spectrum sRec = spectrumOf(wav.x, wav.fs), sMod = spectrumOf(model, wav.fs);
        const double cRec = sRec.centroid(100.0, 20000.0), cMod = sMod.centroid(100.0, 20000.0);
        const double pRec = sRec.peakHz(1000.0, 15000.0, 200.0), pMod = sMod.peakHz(1000.0, 15000.0, 200.0);
        std::printf("[cy reference] %s  : %5.0f / %5.0f   %5.0f / %5.0f   %5.0f / %5.0f\n", r.file, tRec, tMod, cRec, cMod, pRec, pMod);
        // The recordings at tone 10 are clipped at the converter (flat-topped at -2.5 dBFS),
        // which stretches their -20 dB time; those rows are reported, not asserted.
        if (r.tone < 0.9f) REQUIRE_THAT(tMod, WithinRel(tRec, 0.25));
        REQUIRE_THAT(cMod, WithinRel(cRec, 0.30));
    }
}

// The whole voice against itself at a much higher internal rate: the band
// balance must not depend on the oversampling factor. (The aliasing of the
// chain itself is measured on a single oscillator in dsp_metal_chain.cpp.)
TEST_CASE("CY spectrum holds across oversampling factors", "[voice][cy][alias]") {
    auto bandsOf = [](double fs, int os, double out[3]) {
        Spectrum sum;
        for (double pre : {0.0, 0.0113, 0.0237, 0.0391}) accumulate(sum, spectrumOf(hit(fs, os, {}, 4.0f, 0.5, pre), fs));
        const double total = sum.energy(1000.0, 16000.0);
        out[0] = 10.0 * std::log10(sum.energy(2000.0, 5000.0) / total);
        out[1] = 10.0 * std::log10(sum.energy(5000.0, 9000.0) / total);
        out[2] = 10.0 * std::log10(sum.energy(9000.0, 16000.0) / total);
    };
    double ref[3];
    bandsOf(192000.0, 4, ref);
    std::printf("[cy oversampling] reference (768 kHz internal): %.1f dB (2-5 kHz), %.1f dB (5-9 kHz), %.1f dB (9-16 kHz) of the 1-16 kHz energy\n",
                ref[0], ref[1], ref[2]);
    for (int os : {1, 2, 4}) {
        double b[3];
        bandsOf(48000.0, os, b);
        std::printf("[cy oversampling] 48 kHz, oversample %d: %+.1f / %+.1f / %+.1f dB re reference\n", os, b[0] - ref[0], b[1] - ref[1], b[2] - ref[2]);
        for (int k = 0; k < 3; ++k) REQUIRE_THAT(b[k], WithinAbs(ref[k], os == 1 ? 5.0 : 2.5));
    }
}

TEST_CASE("CY latency report", "[voice][cy]") {
    double reported[3] = {};
    for (int i = 0; i < 3; ++i) {
        mw808::voices::Cymbal cy;
        cy.prepare(setupFor(48000.0, 1 << i));
        reported[i] = cy.latencySamples();
        std::printf("[cy latency] oversample %d: %.3f samples\n", 1 << i, reported[i]);
        REQUIRE(reported[i] < 6.5);
    }
    // the reported figures agree with the actual delays (cross-correlation of the
    // same hit rendered at oversample 1, 2 and 4)
    const auto x1 = oneHit<Cy>(48000.0, 1, {}, 4.0f, 0.3), x2 = oneHit<Cy>(48000.0, 2, {}, 4.0f, 0.3), x4 = oneHit<Cy>(48000.0, 4, {}, 4.0f, 0.3);
    const double d21 = measuredLag(x1, x2), d41 = measuredLag(x1, x4);
    std::printf("[cy latency] measured lag re oversample 1: x2 %+.2f (reported %+.2f), x4 %+.2f (reported %+.2f) samples\n", d21,
                reported[1] - reported[0], d41, reported[2] - reported[0]);
    REQUIRE_THAT(d21, WithinAbs(reported[1] - reported[0], 0.35));
    REQUIRE_THAT(d41, WithinAbs(reported[2] - reported[0], 0.35));
}

TEST_CASE("CY renders for listening", "[.render]") {
    const double fs = 48000.0;
    Cy cy;
    cy.prepare(setupFor(fs, 2));
    std::vector<float> out;
    cy.setParams({});
    appendHit(cy, out, fs, Hit::normal(), 2.0);
    appendHit(cy, out, fs, Hit::accented(1.0f), 2.0);
    appendHit(cy, out, fs, Hit::weak(), 1.5);
    for (auto p : {VoiceParams{0.0f, 0.0f, 0.5f, 0.5f}, VoiceParams{0.0f, 1.0f, 0.5f, 0.5f}, VoiceParams{0.0f, 0.5f, 0.0f, 0.5f},
                   VoiceParams{0.0f, 0.5f, 1.0f, 0.5f}, VoiceParams{-5.0f, 0.5f, 0.5f, 0.5f}, VoiceParams{7.0f, 0.5f, 0.5f, 0.5f}}) {
        cy.setParams(p);
        appendHit(cy, out, fs, Hit::normal(), 2.5);
    }
    cy.setParams({});
    for (int i = 0; i < 8; ++i) appendHit(cy, out, fs, i % 4 == 0 ? Hit::accented(0.8f) : Hit::normal(), 0.25);
    appendHit(cy, out, fs, Hit::normal(), 2.0);
    writeWav16(renderDir() + "/cy.wav", out, fs, kRenderGain);
    SUCCEED("wrote " << renderDir() << "/cy.wav");
}

TEST_CASE("CY cost per sample", "[.bench]") {
    for (int os : {1, 2, 4})
        std::printf("[cy bench] 48 kHz, oversample %d: %.1f ns/sample\n", os, benchNsPerSample<Cy>(48000.0, os, {0.0f, 1.0f, 0.5f, 0.5f}));
}

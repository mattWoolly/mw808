// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Open and closed hi-hat: tuning, decay against the service manual, levels,
// the choke, knobs, robustness, retrigger, determinism, reference recordings.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

#include "MetalTestUtil.h"
#include "mw808/dsp/MetalOscBank.h"
#include "mw808/voices/HiHats.h"

using namespace metaltest;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using mw808::voices::HiHats;

namespace {

double bandPeak(const std::vector<float>& x, double fs, double scale = 1.0) {
    return spectrumOf(x, fs, 0.0, 0.08).peakHz(5500.0 * scale, 9000.0 * scale, 300.0 * scale);
}

struct Pair {
    std::vector<float> open, closed;
};

// Open hat at t = 0, optional closed hat at `closedAt` seconds (< 0: none).
Pair renderPair(double fs, double seconds, float openDecay, double closedAt, bool openHit = true, float closedVolts = 4.0f) {
    HiHats h;
    h.prepare(setupFor(fs, 2));
    VoiceParams o{}, c{};
    o.decay = openDecay;
    h.setParams(o, c);
    const auto n = static_cast<std::size_t>(seconds * fs);
    Pair p{std::vector<float>(n), std::vector<float>(n)};
    if (openHit) h.triggerOpen(Hit::normal());
    std::size_t at = 0;
    if (closedAt >= 0.0) {
        at = static_cast<std::size_t>(closedAt * fs);
        if (at > 0) h.render(p.open.data(), p.closed.data(), static_cast<int>(at));
        h.triggerClosed(Hit{closedVolts, 0.0f});
    }
    h.render(p.open.data() + at, p.closed.data() + at, static_cast<int>(n - at));
    return p;
}

}  // namespace

TEST_CASE("Hats ring at the 7.1 kHz band-pass; the closed hat sits higher", "[voice][hats][pitch]") {
    double open48 = 0.0, closedCentroid48 = 0.0;
    for (double fs : kRates) {
        const auto oh = oneHit<OpenHat>(fs, 2, {}, 4.0f, 0.3);
        const auto ch = oneHit<ClosedHat>(fs, 2, {}, 4.0f, 0.3);
        const double openPeak = bandPeak(oh, fs);
        const double openCentroid = spectrumOf(oh, fs, 0.0, 0.2).centroid(1000.0, 20000.0);
        const double closedCentroid = spectrumOf(ch, fs, 0.0, 0.05).centroid(1000.0, 20000.0);
        std::printf("[hats pitch] fs %.0f: OH peak %.0f Hz (band-pass 7117), OH centroid %.0f Hz, CH centroid %.0f Hz\n", fs, openPeak,
                    openCentroid, closedCentroid);
        REQUIRE_THAT(openPeak, WithinRel(7117.0, 0.06));
        REQUIRE(closedCentroid > openCentroid + 1500.0);  // 11.75 kHz high-pass against 7.83 kHz
        if (fs == 48000.0) {
            open48 = openPeak;
            closedCentroid48 = closedCentroid;
        }
    }
    for (double fs : kRates) {
        REQUIRE_THAT(bandPeak(oneHit<OpenHat>(fs, 2, {}, 4.0f, 0.3), fs), WithinRel(open48, 0.03));
        REQUIRE_THAT(spectrumOf(oneHit<ClosedHat>(fs, 2, {}, 4.0f, 0.3), fs, 0.0, 0.05).centroid(1000.0, 20000.0),
                     WithinRel(closedCentroid48, 0.06));
    }
    REQUIRE_THAT(bandPeak(oneHit<OpenHat>(48000.0, 1, {}, 4.0f, 0.3), 48000.0), WithinRel(open48, 0.03));
}

TEST_CASE("Hats tune +12 doubles oscillators and filters", "[voice][hats][pitch][params]") {
    const double fs = 96000.0;
    const double p0 = bandPeak(oneHit<OpenHat>(fs, 2, {0.0f, 0.5f, 0.5f, 0.5f}, 4.0f, 0.3), fs);
    const double p12 = bandPeak(oneHit<OpenHat>(fs, 2, {12.0f, 0.5f, 0.5f, 0.5f}, 4.0f, 0.3), fs, 2.0);
    const double pm12 = bandPeak(oneHit<OpenHat>(fs, 2, {-12.0f, 0.5f, 0.5f, 0.5f}, 4.0f, 0.3), fs, 0.5);
    const double c0 = spectrumOf(oneHit<ClosedHat>(fs, 2, {0.0f, 0.5f, 0.5f, 0.5f}, 4.0f, 0.3), fs, 0.0, 0.05).centroid(500.0, 40000.0);
    const double cm12 = spectrumOf(oneHit<ClosedHat>(fs, 2, {-12.0f, 0.5f, 0.5f, 0.5f}, 4.0f, 0.3), fs, 0.0, 0.05).centroid(250.0, 20000.0);
    std::printf("[hats tune] OH peak: -12 st %.0f, 0 st %.0f, +12 st %.0f Hz; CH centroid 0 st %.0f, -12 st %.0f Hz\n", pm12, p0, p12, c0, cm12);
    REQUIRE_THAT(p12 / p0, WithinRel(2.0, 0.04));
    REQUIRE_THAT(pm12 / p0, WithinRel(0.5, 0.04));
    REQUIRE_THAT(cm12 / c0, WithinRel(0.5, 0.08));
    HiHats h;
    h.prepare(setupFor(fs, 2));
    VoiceParams up{};
    up.tune = 12.0f;
    h.setParams(up, up);
    REQUIRE_THAT(h.oscillators().frequencyHz(5), WithinRel(1600.0, 1e-12));
}

TEST_CASE("OH decay 90 / 450 / 600 ms, CH 50 ms (service manual)", "[voice][hats][decay]") {
    const double manual[3] = {90.0, 450.0, 600.0};
    const float knob[3] = {0.0f, 0.5f, 1.0f};
    for (int k = 0; k < 3; ++k) {
        const double t = meanTimeToDb<OpenHat>(48000.0, 2, {0.0f, knob[k], 0.5f, 0.5f}, 4.0f, 1.5);
        std::printf("[oh decay] decay %.1f: -20 dB at %.0f ms (manual %.0f)\n", knob[k], t, manual[k]);
        REQUIRE_THAT(t, WithinRel(manual[k], 0.12));
    }
    const double ch = meanTimeToDb<ClosedHat>(48000.0, 2, {}, 4.0f, 0.6);
    const double chShort = meanTimeToDb<ClosedHat>(48000.0, 2, {0.0f, 0.0f, 0.5f, 0.5f}, 4.0f, 0.6);
    const double chLong = meanTimeToDb<ClosedHat>(48000.0, 2, {0.0f, 1.0f, 0.5f, 0.5f}, 4.0f, 2.0);
    std::printf("[ch decay] -20 dB at %.0f ms (manual 50); decay knob 0 -> %.0f ms, 1 -> %.0f ms\n", ch, chShort, chLong);
    REQUIRE_THAT(ch, WithinRel(50.0, 0.15));
    REQUIRE(chShort < 0.45 * ch);
    REQUIRE(chLong > 2.5 * ch);
}

TEST_CASE("Hats decay and level hold across sample rates and oversampling", "[voice][hats][decay][level]") {
    const double oh48 = meanTimeToDb<OpenHat>(48000.0, 2, {}, 4.0f, 1.0), ch48 = meanTimeToDb<ClosedHat>(48000.0, 2, {}, 4.0f, 0.5);
    const double ohLevel48 = meanRmsDb<OpenHat>(48000.0, 2, {}, 4.0f, 0.2), chLevel48 = meanRmsDb<ClosedHat>(48000.0, 2, {}, 4.0f, 0.03);
    auto check = [&](double fs, int os, double levelTol) {
        const double oh = meanTimeToDb<OpenHat>(fs, os, {}, 4.0f, 1.0), ch = meanTimeToDb<ClosedHat>(fs, os, {}, 4.0f, 0.5);
        const double ohLevel = meanRmsDb<OpenHat>(fs, os, {}, 4.0f, 0.2), chLevel = meanRmsDb<ClosedHat>(fs, os, {}, 4.0f, 0.03);
        std::printf("[hats rates] fs %.0f os %d: OH -20 dB %.0f ms, level %+.2f dB; CH -20 dB %.0f ms, level %+.2f dB (re 48 kHz os 2)\n", fs,
                    os, oh, ohLevel - ohLevel48, ch, chLevel - chLevel48);
        REQUIRE_THAT(oh, WithinRel(oh48, 0.06));
        REQUIRE_THAT(ch, WithinRel(ch48, 0.15));
        REQUIRE_THAT(ohLevel, WithinAbs(ohLevel48, levelTol));
        REQUIRE_THAT(chLevel, WithinAbs(chLevel48, levelTol));
    };
    // Level must not depend on the rate or the oversampling (the integrator's
    // +-0.3 dB; it used to be -2.6 dB at oversample 1 and +1.2 dB at 192 kHz).
    for (double fs : kRates)
        for (int os : {1, 2, 4}) check(fs, os, 0.3);
}

TEST_CASE("Hats levels: OH 3.5 / 7 Vpp, CH 3 / 6 Vpp, monotonic", "[voice][hats][level]") {
    const double ohN = meanHalfVpp<OpenHat>(48000.0, 2, {}, 4.0f, 0.3), ohA = meanHalfVpp<OpenHat>(48000.0, 2, {}, 14.0f, 0.3);
    const double chN = meanHalfVpp<ClosedHat>(48000.0, 2, {}, 4.0f, 0.2), chA = meanHalfVpp<ClosedHat>(48000.0, 2, {}, 14.0f, 0.2);
    std::printf("[hats level] OH %.2f / %.2f Vpp (manual 3.5 / 7), CH %.2f / %.2f Vpp (manual 3 / 6)\n", ohN * 10, ohA * 10, chN * 10, chA * 10);
    REQUIRE_THAT(toDb(ohN / 0.35), WithinAbs(0.0, 1.0));
    REQUIRE_THAT(toDb(ohA / 0.70), WithinAbs(0.0, 1.0));
    REQUIRE_THAT(toDb(chN / 0.30), WithinAbs(0.0, 1.0));
    REQUIRE_THAT(toDb(chA / 0.60), WithinAbs(0.0, 1.0));
    checkMonotonicLevel([](float v) { return meanHalfVpp<OpenHat>(48000.0, 2, {}, v, 0.3, 4); });
    checkMonotonicLevel([](float v) { return meanHalfVpp<ClosedHat>(48000.0, 2, {}, v, 0.2, 4); });
}

TEST_CASE("OH decay knob sets when the hat is shut", "[voice][hats][params]") {
    double last = 0.0;
    for (float d : {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f}) {
        const double t = meanTimeToDb<OpenHat>(48000.0, 2, {0.0f, d, 0.5f, 0.5f}, 4.0f, 1.5);
        REQUIRE(t > last);
        last = t;
    }
    // the ending is abrupt, as on the hardware: from -20 dB to -40 dB in well under 100 ms at mid decay
    const auto x = oneHit<OpenHat>(48000.0, 2, {}, 4.0f, 1.5);
    REQUIRE(timeToDb(x, 48000.0, -40.0) - timeToDb(x, 48000.0, -20.0) < 100.0);
}

TEST_CASE("Choke: a closed hat fades the open hat out, quickly but not instantly", "[voice][hats][choke]") {
    const double fs = 48000.0;
    for (double at : {0.05, 0.2}) {
        const Pair free = renderPair(fs, 1.0, 1.0f, -1.0);
        const Pair choked = renderPair(fs, 1.0, 1.0f, at);
        const double d1 = toDb(rms(choked.open, fs, at + 0.0, at + 0.004) / rms(free.open, fs, at + 0.0, at + 0.004));
        const double d2 = toDb(rms(choked.open, fs, at + 0.02, at + 0.04) / rms(free.open, fs, at + 0.02, at + 0.04));
        const double d3 = toDb(rms(choked.open, fs, at + 0.06, at + 0.10) / rms(free.open, fs, at + 0.06, at + 0.10));
        const double d4 = toDb(rms(choked.open, fs, at + 0.15, at + 0.30) / rms(free.open, fs, at + 0.15, at + 0.30));
        std::printf("[choke] CH %.0f ms after OH: open hat %.1f dB (0-4 ms), %.1f dB (20-40 ms), %.1f dB (60-100 ms), %.1f dB (150-300 ms) re unchoked\n",
                    at * 1000, d1, d2, d3, d4);
        REQUIRE(d1 > -3.0);    // not an instant cut
        REQUIRE(d2 < -3.0);    // already fading
        REQUIRE(d2 > -25.0);   // ... but still there
        REQUIRE(d3 < -15.0);   // gone about as fast as the closed hat itself
        REQUIRE(d4 < -36.0);
        // no click: the largest sample step around the choke is no larger than without it
        const auto k = static_cast<std::size_t>(at * fs);
        double stepFree = 0.0, stepChoked = 0.0;
        for (std::size_t i = k; i < k + 480; ++i) {
            stepFree = std::max(stepFree, static_cast<double>(std::abs(free.open[i] - free.open[i - 1])));
            stepChoked = std::max(stepChoked, static_cast<double>(std::abs(choked.open[i] - choked.open[i - 1])));
        }
        REQUIRE(stepChoked <= stepFree * 1.05);
    }
    // total open-hat energy after the choke point
    const Pair free = renderPair(fs, 1.5, 1.0f, -1.0), choked = renderPair(fs, 1.5, 1.0f, 0.1);
    const double eFree = rms(free.open, fs, 0.1, 1.5), eChoked = rms(choked.open, fs, 0.1, 1.5);
    std::printf("[choke] open-hat energy after a closed hit at 100 ms: %.1f dB re unchoked\n", toDb(eChoked / eFree));
    REQUIRE(toDb(eChoked / eFree) < -10.0);
}

TEST_CASE("Choke is one-way and holds down an open hat hit during a closed hat", "[voice][hats][choke]") {
    const double fs = 48000.0;
    // the closed hat does not care about the open hat (the only difference is that in
    // one case the voice wakes from sleep, replaying 4 ms of band-pass history)
    const Pair alone = renderPair(fs, 0.5, 0.5f, 0.1, false);
    const Pair withOpen = renderPair(fs, 0.5, 0.5f, 0.1, true);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = 0; i < alone.closed.size(); ++i) {
        diff += (alone.closed[i] - withOpen.closed[i]) * (alone.closed[i] - withOpen.closed[i]);
        ref += alone.closed[i] * alone.closed[i];
    }
    INFO("closed hat with / without an open hat ringing: relative difference " << std::sqrt(diff / ref));
    REQUIRE(diff < 1.0e-4 * ref);
    // open hat triggered 5 ms after a closed hat: short, like a half-open hat
    HiHats h;
    h.prepare(setupFor(fs, 2));
    VoiceParams o{};
    o.decay = 1.0f;
    h.setParams(o, {});
    std::vector<float> open(48000), closed(48000);
    h.triggerClosed(Hit::normal());
    h.render(open.data(), closed.data(), 240);
    h.triggerOpen(Hit::normal());
    h.render(open.data() + 240, closed.data() + 240, 48000 - 240);
    const auto freeOpen = oneHit<OpenHat>(fs, 2, o, 4.0f, 1.0);
    const double held = timeToDb(open, fs, -20.0), freeTime = timeToDb(freeOpen, fs, -20.0);
    std::printf("[choke] open hat 5 ms after a closed hat: -20 dB at %.0f ms (free: %.0f ms)\n", held, freeTime);
    REQUIRE(held < 0.4 * freeTime);
    REQUIRE(peakAbs(open) > 0.05);  // it does sound
}

TEST_CASE("Hats share one bank; separate tunes split it without touching the open hat", "[voice][hats][params]") {
    const double fs = 48000.0;
    auto render = [&](float closedTune, bool hitClosed) {
        HiHats h;
        h.prepare(setupFor(fs, 2));
        VoiceParams o{}, c{};
        c.tune = closedTune;
        h.setParams(o, c);
        Pair p{std::vector<float>(9600), std::vector<float>(9600)};
        h.render(p.open.data(), p.closed.data(), 1000);
        h.triggerOpen(Hit::normal());
        if (hitClosed) h.triggerClosed(Hit::normal());
        h.render(p.open.data(), p.closed.data(), 9600);
        return p;
    };
    // the open hat alone is bit-identical whatever the closed hat's tune
    REQUIRE(render(0.0f, false).open == render(5.0f, false).open);
    const Pair same = render(0.0f, true), up = render(5.0f, true);
    REQUIRE(same.closed != up.closed);
    const double c0 = spectrumOf(same.closed, fs).centroid(1000.0, 22000.0), c5 = spectrumOf(up.closed, fs).centroid(1000.0, 22000.0);
    REQUIRE(c5 > c0 * 1.1);
    // both hats come out with the same delay
    HiHats h;
    h.prepare(setupFor(fs, 2));
    std::printf("[hats latency] oversample 1 / 2 / 4:");
    double reported[3] = {};
    for (int i = 0; i < 3; ++i) {
        HiHats k;
        k.prepare(setupFor(fs, 1 << i));
        reported[i] = k.latencySamples();
        std::printf(" %.3f", reported[i]);
        REQUIRE(reported[i] < 5.5);
    }
    std::printf(" samples (same for OH and CH)\n");
    // as reported: the same hit at oversample 1, 2 and 4, cross-correlated
    const auto o1 = oneHit<OpenHat>(fs, 1, {}, 4.0f, 0.3), o2 = oneHit<OpenHat>(fs, 2, {}, 4.0f, 0.3), o4 = oneHit<OpenHat>(fs, 4, {}, 4.0f, 0.3);
    const double d21 = measuredLag(o1, o2), d41 = measuredLag(o1, o4);
    std::printf("[hats latency] measured lag re oversample 1: x2 %+.2f (reported %+.2f), x4 %+.2f (reported %+.2f)\n", d21,
                reported[1] - reported[0], d41, reported[2] - reported[0]);
    REQUIRE_THAT(d21, WithinAbs(reported[1] - reported[0], 0.35));
    REQUIRE_THAT(d41, WithinAbs(reported[2] - reported[0], 0.35));
}

TEST_CASE("Hats robustness: fuzz, trigger storm, then exact silence", "[voice][hats][robust]") {
    checkRobust<OpenHat>(48000.0, 2, 10.0);
    checkRobust<ClosedHat>(48000.0, 2, 10.0);
    checkRobust<OpenHat>(44100.0, 1, 10.0);
    checkRobust<ClosedHat>(192000.0, 2, 10.0);
    checkRobust<OpenHat>(96000.0, 4, 10.0);
    // both hats at once, independent tunes, everything random
    for (double fs : {44100.0, 96000.0}) {
        HiHats h;
        h.prepare(setupFor(fs, 2, 3U, 1.0f));
        Lcg rng(77U);
        std::vector<float> a(512), b(512);
        double worst = 0.0;
        bool finite = true;
        for (int block = 0; block < 600; ++block) {
            if (rng.unit() < 0.3f) h.setParams(randomParams(rng, rng.unit() < 0.3f), randomParams(rng, rng.unit() < 0.3f));
            if (rng.unit() < 0.4f) h.triggerOpen(Hit{rng.range(0.01f, 14.0f), 0.0f});
            if (rng.unit() < 0.4f) h.triggerClosed(Hit{rng.range(0.01f, 14.0f), 0.0f});
            const int n = 1 + static_cast<int>(rng.next() % 500U);
            h.render(a.data(), b.data(), n);
            for (int i = 0; i < n; ++i) {
                finite = finite && std::isfinite(a[static_cast<std::size_t>(i)]) && std::isfinite(b[static_cast<std::size_t>(i)]);
                worst = std::max({worst, static_cast<double>(std::abs(a[static_cast<std::size_t>(i)])),
                                  static_cast<double>(std::abs(b[static_cast<std::size_t>(i)]))});
            }
        }
        for (int i = 0; i < static_cast<int>(fs); ++i) {  // both triggered on every sample for a second
            h.triggerOpen(Hit{14.0f, 0.0f});
            h.triggerClosed(Hit{14.0f, 0.0f});
            h.render(a.data(), b.data(), 1);
            finite = finite && std::isfinite(a[0]) && std::isfinite(b[0]);
            worst = std::max({worst, static_cast<double>(std::abs(a[0])), static_cast<double>(std::abs(b[0]))});
        }
        REQUIRE(finite);
        REQUIRE(worst < 4.0);
        double waited = 0.0;
        while (h.isActive() && waited < 10.0) {
            h.render(a.data(), b.data(), 512);
            waited += 512.0 / fs;
        }
        REQUIRE_FALSE(h.isActive());
        h.render(a.data(), b.data(), 512);
        for (std::size_t i = 0; i < 512; ++i) {
            REQUIRE(a[i] == 0.0f);
            REQUIRE(b[i] == 0.0f);
        }
    }
}

TEST_CASE("Hats parameter hygiene: NaN and infinities", "[voice][hats][robust]") {
    checkNanSafe<OpenHat>(48000.0, 2);
    checkNanSafe<ClosedHat>(48000.0, 2);
    checkNanSafe<OpenHat>(44100.0, 1);
}

TEST_CASE("Hats sleep after the tail and wake without a click", "[voice][hats][robust]") {
    OpenHat oh;
    oh.prepare(setupFor(48000.0, 2));
    oh.setParams({});
    oh.trigger(Hit::normal());
    std::vector<float> buf(4800);
    double seconds = 0.0;
    while (oh.isActive() && seconds < 10.0) {
        oh.render(buf.data(), 4800);
        seconds += 0.1;
    }
    std::printf("[oh sleep] inactive %.1f s after a normal hit at stock settings\n", seconds);
    REQUIRE(seconds < 2.0);
    oh.trigger(Hit::normal());
    oh.render(buf.data(), 4800);
    REQUIRE(std::abs(buf[0]) < 0.05);
    REQUIRE(peakAbs(buf) > 0.1);
}

TEST_CASE("Hats retrigger carries the ringing state over", "[voice][hats][retrigger]") {
    checkRetrigger<OpenHat>(0.12, 0.8);
    checkRetrigger<ClosedHat>(0.02, 0.3);
    // an open hat retriggered before its timer runs out never gets shut: it sustains
    OpenHat oh;
    oh.prepare(setupFor(48000.0, 2));
    oh.setParams({});
    std::vector<float> out;
    for (int i = 0; i < 8; ++i) appendHit(oh, out, 48000.0, Hit::normal(), 0.2);
    REQUIRE(rms(out, 48000.0, 1.3, 1.6) > 0.3 * rms(out, 48000.0, 0.0, 0.2));
}

TEST_CASE("Hats oscillators are never reset: phases ignore triggers and sleep", "[voice][hats][phase]") {
    const double fs = 48000.0;
    HiHats h;
    h.prepare(setupFor(fs, 2));
    mw808::dsp::MetalOscBank free;
    free.prepare(fs * 2, 0U, 0.0f);
    std::vector<float> a(48000), b(48000);
    std::uint64_t ticks = 0;
    auto run = [&](int n) {
        h.render(a.data(), b.data(), n);
        ticks += static_cast<std::uint64_t>(n) * 2U;
    };
    run(3000);
    h.triggerOpen(Hit::normal());
    run(7001);
    h.triggerClosed(Hit::accented(1.0f));
    run(137);
    h.triggerOpen(Hit::weak());
    for (int i = 0; i < 8 && h.isActive(); ++i) run(48000);
    REQUIRE_FALSE(h.isActive());
    run(12345);
    h.triggerClosed(Hit::normal());
    run(999);
    free.advance(ticks);
    for (int i = 0; i < 6; ++i) REQUIRE(h.oscillators().phase(i) == free.phase(i));
}

TEST_CASE("Hats determinism and unit variance", "[voice][hats][determinism]") {
    checkDeterminism<OpenHat>(0.5);
    checkDeterminism<ClosedHat>(0.3);
    const double nominalPeak = bandPeak(oneHit<OpenHat>(48000.0, 2, {}, 4.0f, 0.3), 48000.0);
    const double nominalDecay = meanTimeToDb<OpenHat>(48000.0, 2, {}, 4.0f, 1.5);
    double minPeak = 1e9, maxPeak = 0.0, minDecay = 1e9, maxDecay = 0.0;
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        const double peak = spectrumOf(oneHit<OpenHat>(48000.0, 2, {}, 4.0f, 0.3, 0.0, seed, 1.0f), 48000.0, 0.0, 0.08).peakHz(5000.0, 10000.0, 300.0);
        const double decay = meanTimeToDb<OpenHat>(48000.0, 2, {}, 4.0f, 1.5, -20.0, 2, seed, 1.0f);
        minPeak = std::min(minPeak, peak);
        maxPeak = std::max(maxPeak, peak);
        minDecay = std::min(minDecay, decay);
        maxDecay = std::max(maxDecay, decay);
    }
    std::printf("[hats variance] six units: OH peak %.0f..%.0f Hz (nominal %.0f), -20 dB %.0f..%.0f ms (nominal %.0f)\n", minPeak, maxPeak,
                nominalPeak, minDecay, maxDecay, nominalDecay);
    REQUIRE(maxPeak - minPeak > 0.01 * nominalPeak);
    REQUIRE(maxDecay - minDecay > 0.02 * nominalDecay);
    REQUIRE(minPeak > nominalPeak / 1.30);
    REQUIRE(maxPeak < nominalPeak * 1.30);
    REQUIRE(minDecay > nominalDecay * 0.7);   // the timer capacitor is +-20 %
    REQUIRE(maxDecay < nominalDecay * 1.3);
}

TEST_CASE("Hats against the reference recordings", "[voice][hats][reference]") {
    if (!haveRef("oh8/OH50.WAV") || !haveRef("ch8/CH.WAV")) SKIP("reference recordings not present");
    struct Row { const char* file; float decay; };
    const Row rows[] = {{"OH00", 0.0f}, {"OH25", 0.25f}, {"OH50", 0.5f}, {"OH75", 0.75f}, {"OH10", 1.0f}};
    std::printf("[hats reference] file : -20 dB ms (rec / model), centroid Hz (rec / model), strongest band Hz (rec / model)\n");
    for (const Row& r : rows) {
        Wav wav;
        REQUIRE(readWav16(refDir() + "/oh8/" + r.file + ".WAV", wav));
        const double seconds = static_cast<double>(wav.x.size()) / wav.fs;
        const auto model = oneHit<OpenHat>(wav.fs, 2, {0.0f, r.decay, 0.5f, 0.5f}, 4.0f, seconds);
        const double tRec = timeToDb(wav.x, wav.fs, -20.0), tMod = timeToDb(model, wav.fs, -20.0);
        const Spectrum sRec = spectrumOf(wav.x, wav.fs), sMod = spectrumOf(model, wav.fs);
        std::printf("[hats reference] %s : %4.0f / %4.0f   %5.0f / %5.0f   %5.0f / %5.0f\n", r.file, tRec, tMod,
                    sRec.centroid(1000.0, 20000.0), sMod.centroid(1000.0, 20000.0), sRec.peakHz(3000.0, 15000.0, 300.0),
                    sMod.peakHz(3000.0, 15000.0, 300.0));
        // the recorded unit's hand-set knob positions run short of the manual's (184 ms at "2.5",
        // 333 ms at "5"); the ends of the travel agree
        if (r.decay == 0.0f || r.decay == 1.0f) REQUIRE_THAT(tMod, WithinRel(tRec, 0.35));
        REQUIRE_THAT(sMod.centroid(1000.0, 20000.0), WithinRel(sRec.centroid(1000.0, 20000.0), 0.15));
        REQUIRE_THAT(sMod.peakHz(3000.0, 15000.0, 300.0), WithinRel(sRec.peakHz(3000.0, 15000.0, 300.0), 0.12));
    }
    Wav wav;
    REQUIRE(readWav16(refDir() + "/ch8/CH.WAV", wav));
    const auto model = oneHit<ClosedHat>(wav.fs, 2, {}, 4.0f, static_cast<double>(wav.x.size()) / wav.fs);
    const Spectrum sRec = spectrumOf(wav.x, wav.fs), sMod = spectrumOf(model, wav.fs);
    const double tRec = timeToDb(wav.x, wav.fs, -20.0), tMod = timeToDb(model, wav.fs, -20.0);
    std::printf("[hats reference] CH   : %4.0f / %4.0f   %5.0f / %5.0f\n", tRec, tMod, sRec.centroid(1000.0, 20000.0), sMod.centroid(1000.0, 20000.0));
    REQUIRE_THAT(tMod, WithinRel(tRec, 0.45));
    REQUIRE_THAT(sMod.centroid(1000.0, 20000.0), WithinRel(sRec.centroid(1000.0, 20000.0), 0.15));
}

TEST_CASE("Hats renders for listening", "[.render]") {
    const double fs = 48000.0;
    HiHats h;
    h.prepare(setupFor(fs, 2));
    std::vector<float> open, closed;
    auto run = [&](double seconds) {
        const auto n = static_cast<std::size_t>(seconds * fs), at = open.size();
        open.resize(at + n);
        closed.resize(at + n);
        h.render(open.data() + at, closed.data() + at, static_cast<int>(n));
    };
    VoiceParams o{}, c{};
    for (float d : {0.0f, 0.5f, 1.0f}) {
        o.decay = d;
        h.setParams(o, c);
        h.triggerOpen(Hit::normal());
        run(1.0);
        h.triggerOpen(Hit::accented(1.0f));
        run(1.0);
    }
    o.decay = 0.5f;
    for (float d : {0.5f, 0.0f, 1.0f}) {
        c.decay = d;
        h.setParams(o, c);
        h.triggerClosed(Hit::normal());
        run(0.4);
        h.triggerClosed(Hit::accented(1.0f));
        run(0.6);
    }
    c.decay = 0.5f;
    o.decay = 0.8f;
    h.setParams(o, c);
    // a bar of 16ths: closed hats with open hats on the off-beats, choked by the next closed hat
    for (int bar = 0; bar < 2; ++bar)
        for (int step = 0; step < 16; ++step) {
            if (step % 4 == 2) h.triggerOpen(step == 10 ? Hit::accented(0.8f) : Hit::normal());
            else h.triggerClosed(step % 4 == 0 ? Hit::accented(0.6f) : Hit::normal());
            run(0.125);
        }
    run(1.0);
    std::vector<float> mix(open.size());
    for (std::size_t i = 0; i < mix.size(); ++i) mix[i] = open[i] + closed[i];
    writeWav16(renderDir() + "/hats.wav", mix, fs, kRenderGain);
    writeWav16(renderDir() + "/oh.wav", open, fs, kRenderGain);
    writeWav16(renderDir() + "/ch.wav", closed, fs, kRenderGain);
    SUCCEED("wrote hats.wav, oh.wav, ch.wav");
}

TEST_CASE("Hats cost per sample", "[.bench]") {
    for (int os : {1, 2, 4}) {
        const double pair = benchNsPerSample<OpenHat>(48000.0, os, {0.0f, 1.0f, 0.5f, 0.5f});
        std::printf("[hats bench] 48 kHz, oversample %d: %.1f ns/sample for the pair (%.1f per hat)\n", os, pair, pair / 2);
    }
}

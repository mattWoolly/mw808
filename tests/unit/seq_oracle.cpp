// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Review regressions for the sequencer, plus an independent brute-force timing
// oracle. The oracle does not share code with Sequencer.cpp: it walks measures
// from PPQ 0 in long double, applies architecture.md §5 literally (variation,
// auto fill, polymeter, accent promotion, shuffle, sub-steps, flam) and
// rounds each hit to the nearest sample. Every block partition must match it
// exactly; only hits within 2e-6 samples of a rounding tie may move by one.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <tuple>
#include <vector>

#include "SeqTestUtil.h"

using namespace seqtest;

namespace {

struct OHit {
    std::int64_t sample = 0;
    int voice = 0;
    float volts = 0.0f;
    int level = 0;
    bool tie = false;  // within 2e-6 samples of a rounding tie
};

struct OScenario {
    double sr = 48000.0, bpm = 120.0, ppqStart = 0.0;
    std::int64_t total = 0;
    float accent = 0.7f, flamMs = 24.0f, swing = 1.0f;
    VariationMode variation = VariationMode::A;
    AutoFill autoFill = AutoFill::Manual;
    int fillSlot = 0;
};

long double stepQnOracle(Scale s) {
    switch (s) {
        case Scale::EighthTriplet: return 1.0L / 3.0L;
        case Scale::SixteenthTriplet: return 1.0L / 6.0L;
        case Scale::ThirtySecond: return 0.125L;
        case Scale::Eighth: return 0.5L;
        default: return 0.25L;
    }
}

std::vector<OHit> oracle(const PatternBank& bank, int base, const OScenario& sc) {
    std::vector<OHit> out;
    const long double spp = static_cast<long double>(sc.sr) * 60.0L / static_cast<long double>(sc.bpm);
    const long double p0 = sc.ppqStart;
    const int fillN = autoFillMeasures(sc.autoFill);
    auto patternAt = [&](std::int64_t m) {
        const bool fill = fillN > 0 && ((m % fillN) + fillN) % fillN == fillN - 1;
        return fill ? (base / kPatternsPerBank) * kPatternsPerBank + kFirstFillSlot + sc.fillSlot : base;
    };
    auto lenOf = [&](int pi) { return std::clamp<int>(bank.patterns[static_cast<std::size_t>(pi)].length, 1, kMaxSteps); };
    auto measureQn = [&](std::int64_t m) {
        const int pi = patternAt(m);
        return lenOf(pi) * stepQnOracle(bank.patterns[static_cast<std::size_t>(pi)].scale);
    };
    auto round = [&](long double t, long double extra, bool& tie) {
        const long double x = (t - p0) * spp + extra + 0.5L;
        const long double fl = std::floor(x);
        tie = (x - fl) < 2e-6L || (fl + 1.0L - x) < 2e-6L;
        return static_cast<std::int64_t>(std::floor(x + 1e-6L));
    };

    std::int64_t m = 0;
    long double start = 0.0L;
    if (p0 >= 0.0L) {
        while (start + measureQn(m) <= p0) start += measureQn(m++);
    } else {
        while (start > p0) start -= measureQn(--m);
    }
    const long double endQn = p0 + static_cast<long double>(sc.total) / spp + 2.0L;
    const long double flamSamples = static_cast<long double>(sc.flamMs) * 0.001L * static_cast<long double>(sc.sr);
    for (; start < endQn; start += measureQn(m++)) {
        const int pi = patternAt(m);
        const Pattern& p = bank.patterns[static_cast<std::size_t>(pi)];
        const int L = lenOf(pi);
        const long double stepQn = stepQnOracle(p.scale);
        const int var = sc.variation == VariationMode::B ? 1
                        : sc.variation == VariationMode::AB ? static_cast<int>(((m % 2) + 2) % 2)
                                                            : 0;
        for (int s = 0; s < L; ++s) {
            const long double t = start + s * stepQn;
            bool stepTie = false;
            const std::int64_t at = round(t, 0.0L, stepTie);
            if (at < -1 || (at == -1 && !stepTie) || at >= sc.total) continue;
            const bool accentOn = p.at(var, kAccentTrack, s).on();
            const long double shuffle =
                (s & 1) != 0 ? (std::min<int>(p.shuffle, 100) / 100.0L) * sc.swing * stepQn / 3.0L : 0.0L;
            for (int v = 0; v < kNumVoices; ++v) {
                const int tl = p.trackLength[static_cast<std::size_t>(v)] != 0
                                   ? std::clamp<int>(p.trackLength[static_cast<std::size_t>(v)], 1, kMaxSteps)
                                   : L;
                const Step c = p.at(var, v, static_cast<int>(((m * L + s) % tl + tl) % tl));
                if (!c.on()) continue;
                int level = static_cast<int>(c.level());
                float volts = 0.0f;
                if (accentOn) {
                    if (level == 1) {
                        level = 2;
                        volts = 4.0f;
                    } else if (level == 2) {
                        level = 3;
                        volts = 4.0f + 10.0f * sc.accent;
                    } else {
                        volts = 4.0f + 10.0f * std::min(1.0f, sc.accent + 0.25f);
                    }
                } else {
                    volts = level == 1 ? 2.2f : (level == 2 ? 4.0f : 4.0f + 10.0f * sc.accent);
                }
                const int n = c.subSteps();
                for (int k = 0; k < n; ++k) {
                    const long double th = t + shuffle + k * stepQn / n;
                    bool tie = false;
                    auto push = [&](std::int64_t smp, float vv, int lv) {
                        if (smp < sc.total) out.push_back({std::max<std::int64_t>(0, smp), v, vv, lv, tie || stepTie});
                    };
                    if (k == 0 && c.flam() && flamSamples >= 1.0L) {
                        push(round(th, 0.0L, tie), std::max(1.5f, 0.6f * volts), 1);
                        push(round(th, flamSamples, tie), volts, level);
                    } else {
                        push(round(th, 0.0L, tie), volts, level);
                    }
                }
            }
        }
    }
    return out;
}

// Mismatch count: exact multiset match; a tie hit may match one sample away.
int mismatches(const std::vector<OHit>& want, const std::vector<Ev>& got) {
    std::multimap<std::tuple<std::int64_t, int, float, int>, int> pool;
    for (const Ev& e : got) pool.insert({{e.sample, e.voice, e.volts, static_cast<int>(e.level)}, 0});
    int bad = 0;
    for (const OHit& w : want) {
        auto it = pool.find({w.sample, w.voice, w.volts, w.level});
        if (it == pool.end() && w.tie) {
            it = pool.find({w.sample - 1, w.voice, w.volts, w.level});
            if (it == pool.end()) it = pool.find({w.sample + 1, w.voice, w.volts, w.level});
        }
        if (it == pool.end()) {
            ++bad;
            continue;
        }
        pool.erase(it);
    }
    return bad + static_cast<int>(pool.size());
}

Pattern oraclePattern(std::mt19937& rng, float density) {
    // Same feature mix as randomPattern, but without probability (the hash is
    // the implementation's choice, not part of the spec's arithmetic).
    return randomPattern(rng, density, false);
}

}  // namespace

TEST_CASE("independent oracle: random patterns, tempos, transports and block partitions", "[seq][oracle]") {
    std::mt19937 rng(0x0EAC1EU);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    int checked = 0;
    for (int it = 0; it < 150; ++it) {
        OScenario sc;
        sc.sr = rates[rng() % 5];
        sc.bpm = (rng() % 3 == 0) ? std::round(20.0 + u(rng) * 380.0) : 20.0 + u(rng) * 380.0;
        const bool internal = rng() % 3 == 0;
        sc.autoFill = static_cast<AutoFill>(rng() % 6);
        sc.ppqStart = internal ? 0.0 : (rng() % 4 == 0 ? std::round(u(rng) * 16.0) : (u(rng) - 0.2) * 40.0);
        sc.total = static_cast<std::int64_t>(sc.sr * (1.0 + u(rng) * 3.0));
        sc.accent = static_cast<float>(u(rng));
        sc.flamMs = rng() % 10 == 0 ? static_cast<float>(u(rng) * 0.02) : static_cast<float>(u(rng) * 100.0);
        sc.swing = rng() % 2 != 0 ? 1.0f : static_cast<float>(u(rng));
        sc.variation = static_cast<VariationMode>(rng() % 3);
        sc.fillSlot = static_cast<int>(rng() % 4);
        const int base = static_cast<int>(rng() % 12) + kPatternsPerBank * static_cast<int>(rng() % 4);

        auto s = makeSeq(sc.sr);
        auto bank = std::make_unique<PatternBank>();
        for (int k = 0; k < 4; ++k) {
            std::mt19937 pr(static_cast<std::uint32_t>(rng()));
            bank->patterns[static_cast<std::size_t>((base / kPatternsPerBank) * kPatternsPerBank + kFirstFillSlot + k)] =
                oraclePattern(pr, 0.2f);
        }
        std::mt19937 pr(static_cast<std::uint32_t>(rng()));
        bank->patterns[static_cast<std::size_t>(base)] = oraclePattern(pr, static_cast<float>(0.05 + u(rng) * 0.3));
        for (int i = 0; i < kNumPatterns; ++i) s->setPattern(i, bank->patterns[static_cast<std::size_t>(i)]);

        Settings st = internal ? internalSettings(sc.bpm) : hostSettings();
        st.accent = sc.accent;
        st.flamMs = sc.flamMs;
        st.swingScale = sc.swing;
        st.variation = sc.variation;
        st.autoFill = sc.autoFill;
        st.fillSlot = static_cast<std::uint8_t>(sc.fillSlot);

        const auto want = oracle(*bank, base, sc);
        std::vector<std::vector<int>> parts{fixedBlocks(sc.total, 64), fixedBlocks(sc.total, 4096),
                                            randomBlocks(sc.total, rng, 3000)};
        if (it % 10 == 0) parts.push_back(fixedBlocks(sc.total, 1));
        for (const auto& blocks : parts) {
            s->reset();
            s->selectPattern(base);
            bool overflow = false;
            const auto got = internal ? runInternal(*s, st, blocks, &overflow)
                                      : runHost(*s, st, sc.sr, sc.bpm, sc.ppqStart, blocks, &overflow);
            if (overflow) continue;
            INFO("scenario " << it << " sr " << sc.sr << " bpm " << sc.bpm << " ppq0 " << sc.ppqStart << " blocks "
                             << blocks.size());
            REQUIRE(mismatches(want, got) == 0);
            ++checked;
        }
    }
    CHECK(checked > 400);
}

TEST_CASE("HostSync follows host tempos outside 20..400: no double or missed steps", "[seq][oracle][review]") {
    // Regression: the PPQ-to-sample mapping used the clamped tempo, so every
    // block of a 10 BPM host looked like a backward jump (each step fired
    // twice) and a 600 BPM host like a forward jump (a third of the steps lost).
    for (double bpm : {1.0, 5.0, 10.0, 19.0, 401.0, 600.0, 999.0}) {
        for (int block : {64, 512, 4096}) {
            auto s = makeSeq(48000.0);
            s->setPattern(0, everyStep(BD));
            const double spp = 48000.0 * 60.0 / bpm;
            const double qn = bpm < 20.0 ? 0.6 : 16.0;
            const auto total = static_cast<std::int64_t>(qn * spp);
            const auto ev = runHost(*s, hostSettings(), 48000.0, bpm, 0.0, fixedBlocks(total, block));
            std::vector<std::int64_t> want;
            for (int k = 0;; ++k) {
                const auto smp = static_cast<std::int64_t>(std::floor(k * 0.25L * spp + 0.5L + 1e-6L));
                if (smp >= total) break;
                want.push_back(smp);
            }
            INFO("host " << bpm << " BPM, block " << block);
            REQUIRE(samplesOf(ev) == want);
            CHECK(s->tempo() == std::clamp(bpm, 20.0, 400.0));  // the reported tempo keeps the contract clamp
        }
    }
}

TEST_CASE("HostSync tempo ramp in large blocks is not a relocation", "[seq][oracle][review]") {
    // Regression: the host integrates a ramp inside each block but reports one
    // BPM per block. With 8192-sample blocks of a 60 -> 400 BPM ramp the PPQ
    // landed > 1 ms from the old-tempo continuation and steps were skipped.
    for (double target : {240.0, 400.0}) {
        for (int block : {512, 4096, 8192}) {
            auto s = makeSeq(48000.0);
            s->setPattern(0, everyStep(BD));
            auto buf = std::make_unique<EventBuffer>();
            double ppq = 0.0;
            std::vector<Ev> ev;
            std::int64_t pos = 0;
            std::vector<double> blockPpq;
            while (ppq < 24.0) {
                const auto bpmAt = [&](double q) { return q < 16.0 ? 60.0 + (target - 60.0) * q / 16.0 : target; };
                Transport t{true, true, ppq, bpmAt(ppq)};
                buf->clear();
                s->process(t, hostSettings(), block, *buf);
                collect(*buf, block, pos, ev);
                for (int i = 0; i < block; ++i) ppq += bpmAt(ppq) / (60.0 * 48000.0);
                pos += block;
            }
            INFO("ramp to " << target << ", block " << block);
            // Every sixteenth up to the final position fires exactly once, in order.
            const auto expected = static_cast<std::size_t>(std::floor(ppq * 4.0)) + 1;
            CHECK((ev.size() == expected || ev.size() + 1 == expected));
            for (std::size_t i = 1; i < ev.size(); ++i) CHECK(ev[i].sample > ev[i - 1].sample);
        }
    }
}

TEST_CASE("recording two live hits of one voice inside one range fires neither step again", "[seq][oracle][review]") {
    // Regression: one suppression slot per voice; the second recordHit
    // overwrote the first, so the first recorded step sounded twice (live hit
    // plus sequencer) when a large block covered several steps.
    auto s = makeSeq(48000.0);
    Settings st = internalSettings(120.0);
    st.record = true;
    Driver d(*s);
    d.block(Transport{}, st, 100);  // steps every 6000 samples; next range starts at 100
    const RecordResult r1 = s->recordHit(SD, false, 5850);   // sample 5950  -> step 1
    const RecordResult r2 = s->recordHit(SD, true, 11850);   // sample 11950 -> step 2
    const RecordResult r3 = s->recordHit(SD, false, 17900);  // sample 18000 -> step 3
    CHECK(r1.step == 1);
    CHECK(r2.step == 2);
    CHECK(r3.step == 3);
    const auto first = d.block(Transport{}, st, 20000);
    CHECK(onlyVoice(first, SD).empty());
    // The written steps play on the next pass.
    const auto next = d.runUntil(st, 96000 + 20000, 512);
    const auto sd = onlyVoice(next, SD);
    REQUIRE(sd.size() == 3);
    CHECK(sd[0].sample == 96000 + 6000);
    CHECK(sd[1].sample == 96000 + 12000);
    CHECK(sd[2].sample == 96000 + 18000);
    CHECK(sd[1].volts > Hit::kNormalVolts);
}

TEST_CASE("a flam shorter than one sample plays the main hit only", "[seq][oracle][review]") {
    // Regression: flamMs = 0 emitted the grace and the main hit on the same
    // sample (two triggers and two MIDI note-ons at once).
    for (float ms : {0.0f, 0.01f, 0.02f}) {
        auto s = makeSeq(48000.0);
        Pattern p;
        p.at(0, SD, 0).setLevel(StepLevel::Normal);
        p.at(0, SD, 0).setFlam(true);
        s->setPattern(0, p);
        Settings st = internalSettings(120.0);
        st.flamMs = ms;
        Driver d(*s);
        const auto ev = d.runUntil(st, 1000, 64);
        INFO("flamMs " << ms);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].sample == 0);
        CHECK(ev[0].volts == Hit::kNormalVolts);
    }
    // One sample and up: grace then main, one sample apart at least.
    auto s = makeSeq(48000.0);
    Pattern p;
    p.at(0, SD, 0).setLevel(StepLevel::Normal);
    p.at(0, SD, 0).setFlam(true);
    s->setPattern(0, p);
    Settings st = internalSettings(120.0);
    st.flamMs = 1.0f / 48.0f + 1e-5f;
    Driver d(*s);
    const auto ev = d.runUntil(st, 1000, 64);
    REQUIRE(ev.size() == 2);
    CHECK(ev[1].sample == ev[0].sample + 1);
}

TEST_CASE("pending queue holds the worst case at a 1000 BPM host", "[seq][oracle][review]") {
    // HostSync now follows host tempos up to 1000 BPM: the densest legal
    // pattern with 100 ms flams must still lose nothing through the queue.
    Pattern p;
    p.length = 64;
    p.scale = Scale::ThirtySecond;
    p.shuffle = 100;
    for (int t = 0; t < kNumTracks; ++t)
        for (int k = 0; k < kMaxSteps; ++k) {
            Step& c = p.at(0, t, k);
            c.setLevel(StepLevel::Accent);
            if (t < kNumVoices) {
                c.setSubSteps(4);
                c.setFlam(true);
            }
        }
    p.steps[1] = p.steps[0];
    auto s = makeSeq(48000.0);
    s->setPattern(0, p);
    Settings st = hostSettings();
    st.flamMs = 100.0f;
    const std::int64_t total = 48000;
    bool overflow = false;
    const auto big = runHost(*s, st, 48000.0, 1000.0, 0.0, fixedBlocks(total, 256), &overflow);
    REQUIRE_FALSE(overflow);
    s->reset();
    const auto tiny = runHost(*s, st, 48000.0, 1000.0, 0.0, fixedBlocks(total, 1));
    CHECK(big.size() > 10000);
    REQUIRE(tiny == big);
}

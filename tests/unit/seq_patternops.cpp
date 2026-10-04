// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Pattern editing helpers (architecture.md §5.6).

#include <catch2/catch_test_macros.hpp>

#include <random>

#include "SeqTestUtil.h"
#include "mw808/seq/PatternOps.h"

using namespace seqtest;

namespace {

int countOn(const Pattern& p, int var, int track, int upTo = kMaxSteps) {
    int n = 0;
    for (int s = 0; s < upTo; ++s) n += p.at(var, track, s).on() ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("clear, copy and variation copy", "[seq][ops]") {
    std::mt19937 rng(1);
    Pattern p = randomPattern(rng, 0.5f);
    const Pattern orig = p;

    clearTrack(p, 0, SD);
    CHECK(countOn(p, 0, SD) == 0);
    CHECK(p.steps[1] == orig.steps[1]);

    copyTrack(p, 0, BD, CH);
    for (int s = 0; s < kMaxSteps; ++s) CHECK(p.at(0, CH, s) == p.at(0, BD, s));
    CHECK(p.trackLength[CH] == p.trackLength[BD]);

    Pattern q;
    copyTrack(orig, 1, kAccentTrack, q, 0, kAccentTrack);
    for (int s = 0; s < kMaxSteps; ++s) CHECK(q.at(0, kAccentTrack, s) == orig.at(1, kAccentTrack, s));

    copyVariation(p, 0, 1);
    CHECK(p.steps[0] == p.steps[1]);

    clearPattern(p, 0);
    for (int t = 0; t < kNumTracks; ++t) CHECK(countOn(p, 0, t) == 0);
    CHECK(countOn(p, 1, BD) > 0);
    clearPattern(p);
    for (int t = 0; t < kNumTracks; ++t) CHECK(countOn(p, 1, t) == 0);
    CHECK(p.length == orig.length);
    CHECK(p.scale == orig.scale);

    // Out-of-range arguments are no-ops.
    Pattern r = orig;
    clearTrack(r, 2, 0);
    clearTrack(r, 0, kNumTracks);
    copyTrack(r, 0, -1, 3);
    shiftTrack(r, -1, 0, 3);
    invertTrack(r, 0, 99);
    copyVariation(r, 0, 5);
    setEuclidean(r, 3, 0, 3, 0);
    randomizeTrack(r, 0, 40, 1.0f, 3);
    CHECK(r == orig);
}

TEST_CASE("shift wraps within the track length", "[seq][ops]") {
    Pattern p;
    p.length = 16;
    p.trackLength[BD] = 5;
    p.at(0, BD, 0).setLevel(StepLevel::Accent);
    p.at(0, BD, 7).setLevel(StepLevel::Normal);  // beyond the 5-step loop: untouched
    shiftTrack(p, 0, BD, 3);
    CHECK(p.at(0, BD, 3).level() == StepLevel::Accent);
    CHECK_FALSE(p.at(0, BD, 0).on());
    CHECK(p.at(0, BD, 7).on());
    shiftTrack(p, 0, BD, -4);  // 3 - 4 = -1 -> 4
    CHECK(p.at(0, BD, 4).level() == StepLevel::Accent);
    shiftTrack(p, 0, BD, 5 * 1000 + 1);
    CHECK(p.at(0, BD, 0).level() == StepLevel::Accent);

    Pattern q = patternWith(SD, {15});
    q.at(0, kAccentTrack, 15).setLevel(StepLevel::Normal);
    shiftPattern(q, 0, 1);
    CHECK(q.at(0, SD, 0).on());
    CHECK(q.at(0, kAccentTrack, 0).on());
    CHECK_FALSE(q.at(0, SD, 15).on());
    CHECK(q.at(1, SD, 15).on());  // variation B untouched
}

TEST_CASE("invert within the track length", "[seq][ops]") {
    Pattern p = patternWith(CH, {0, 2}, 8);
    p.at(0, CH, 0).setFlam(true);
    invertTrack(p, 0, CH);
    CHECK(countOn(p, 0, CH) == 6);
    CHECK_FALSE(p.at(0, CH, 0).on());
    CHECK(p.at(0, CH, 1).bits == static_cast<std::uint8_t>(StepLevel::Normal));
    CHECK_FALSE(p.at(0, CH, 8).on());
}

TEST_CASE("randomize is deterministic, honours density and keeps the hats apart", "[seq][ops]") {
    std::array<float, kNumTracks> dens{};
    dens.fill(0.5f);
    dens[OH] = 0.9f;
    dens[CH] = 0.9f;
    Pattern a, b, c;
    randomizePattern(a, 0, dens, 1234);
    randomizePattern(b, 0, dens, 1234);
    randomizePattern(c, 0, dens, 1235);
    CHECK(a == b);
    CHECK_FALSE(a == c);
    for (int s = 0; s < 16; ++s) CHECK_FALSE((a.at(0, OH, s).on() && a.at(0, CH, s).on()));
    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s) {
            const Step st = a.at(0, t, s);
            CHECK_FALSE(st.flam());
            CHECK(st.subSteps() == 1);
            CHECK(st.probabilityCode() == 0);
        }
    // Density over many seeds.
    int on = 0, total = 0;
    for (std::uint32_t seed = 0; seed < 200; ++seed) {
        Pattern p;
        randomizeTrack(p, 0, SD, 0.3f, seed);
        on += countOn(p, 0, SD, 16);
        total += 16;
        CHECK(countOn(p, 0, SD) == countOn(p, 0, SD, 16));  // nothing beyond the length
    }
    CHECK(static_cast<double>(on) / total > 0.26);
    CHECK(static_cast<double>(on) / total < 0.34);
    // Writing one hat clears the other where they would coincide.
    Pattern h = everyStep(CH);
    randomizeTrack(h, 0, OH, 0.5f, 9);
    for (int s = 0; s < 16; ++s) CHECK(h.at(0, OH, s).on() != h.at(0, CH, s).on());
    Pattern zero;
    randomizeTrack(zero, 0, BD, 0.0f, 9);
    CHECK(countOn(zero, 0, BD) == 0);
    randomizeTrack(zero, 0, BD, 1.0f, 9);
    CHECK(countOn(zero, 0, BD) == 16);
}

TEST_CASE("expand doubles by repetition; Euclidean rhythms", "[seq][ops]") {
    Pattern p = patternWith(BD, {0, 3}, 12);
    p.trackLength[SD] = 5;
    p.at(0, SD, 1).setLevel(StepLevel::Normal);
    REQUIRE(expandPattern(p));
    CHECK(p.length == 24);
    CHECK(p.at(0, BD, 12).on());
    CHECK(p.at(0, BD, 15).on());
    CHECK_FALSE(p.at(0, SD, 13).on());  // own length: not repeated
    p.length = 40;
    CHECK_FALSE(expandPattern(p));
    CHECK(p.length == 40);

    Pattern e;
    e.length = 8;
    setEuclidean(e, 0, CB, 3, 0);  // tresillo x..x..x.
    const bool want[] = {true, false, false, true, false, false, true, false};
    for (int s = 0; s < 8; ++s) CHECK(e.at(0, CB, s).on() == want[s]);
    setEuclidean(e, 0, CB, 3, 1);
    for (int s = 0; s < 8; ++s) CHECK(e.at(0, CB, s).on() == want[(s + 7) % 8]);
    for (int k = 0; k <= 16; ++k) {
        Pattern f;
        setEuclidean(f, 0, RS, k, 0);
        CHECK(countOn(f, 0, RS) == k);
        // Maximally even: gaps differ by at most one.
        int minGap = 99, maxGap = 0, last = -1, first = -1;
        for (int s = 0; s < 16; ++s)
            if (f.at(0, RS, s).on()) {
                if (last >= 0) {
                    minGap = std::min(minGap, s - last);
                    maxGap = std::max(maxGap, s - last);
                } else {
                    first = s;
                }
                last = s;
            }
        if (k >= 2) {
            const int wrap = first + 16 - last;
            minGap = std::min(minGap, wrap);
            maxGap = std::max(maxGap, wrap);
            CHECK(maxGap - minGap <= 1);
        }
    }
    Pattern g;
    setEuclidean(g, 0, RS, 99, 0);
    CHECK(countOn(g, 0, RS) == 16);
}

TEST_CASE("promotedLevel follows the global accent rule", "[seq][ops]") {
    CHECK(promotedLevel(StepLevel::Weak, true) == StepLevel::Normal);
    CHECK(promotedLevel(StepLevel::Normal, true) == StepLevel::Accent);
    CHECK(promotedLevel(StepLevel::Accent, true) == StepLevel::Accent);
    CHECK(promotedLevel(StepLevel::Off, true) == StepLevel::Off);
    CHECK(promotedLevel(StepLevel::Weak, false) == StepLevel::Weak);
}

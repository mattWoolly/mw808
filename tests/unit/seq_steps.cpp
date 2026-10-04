// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Step semantics (architecture.md §5.2-5.4): shuffle, flam, sub-steps,
// probability, polymeter, global accent, variations, pattern switching,
// chains, fills and live recording.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <map>
#include <vector>

#include "SeqTestUtil.h"

using namespace seqtest;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kSr = 48000.0;
constexpr std::int64_t kStep = 6000;     // a sixteenth at 120 BPM / 48 kHz
constexpr std::int64_t kMeasure = 96000;  // 16 sixteenths
}  // namespace

TEST_CASE("shuffle delays odd steps by shuffle/100 x swingScale x step/3", "[seq][steps]") {
    struct Case {
        int shuffle;
        float swing;
        std::int64_t delay;
    };
    for (const Case c : {Case{100, 1.0f, 2000}, Case{50, 1.0f, 1000}, Case{100, 0.5f, 1000}, Case{37, 1.0f, 740},
                         Case{0, 1.0f, 0}, Case{100, 0.0f, 0}}) {
        auto s = makeSeq(kSr);
        Pattern p = everyStep(BD);
        p.shuffle = static_cast<std::uint8_t>(c.shuffle);
        s->setPattern(0, p);
        Settings st = internalSettings(120.0);
        st.swingScale = c.swing;
        Driver d(*s);
        d.runUntil(st, kMeasure, 512);
        REQUIRE(d.events.size() == 16);
        for (int i = 0; i < 16; ++i)
            CHECK(d.events[static_cast<std::size_t>(i)].sample == i * kStep + ((i & 1) != 0 ? c.delay : 0));
    }
}

TEST_CASE("flam: grace at the step, main flamMs later whatever the tempo", "[seq][steps]") {
    for (double bpm : {60.0, 120.0, 300.0}) {
        auto s = makeSeq(kSr);
        Pattern p;
        p.at(0, BD, 0).setLevel(StepLevel::Normal);
        p.at(0, BD, 0).setFlam(true);
        p.at(0, SD, 0).setLevel(StepLevel::Weak);
        p.at(0, SD, 0).setFlam(true);
        p.at(0, LT, 0).setLevel(StepLevel::Accent);
        p.at(0, LT, 0).setFlam(true);
        s->setPattern(0, p);
        Settings st = internalSettings(bpm);
        st.flamMs = 24.0f;
        st.accent = 0.7f;
        Driver d(*s);
        d.runUntil(st, 4000, 100);
        INFO("bpm " << bpm);
        REQUIRE(d.events.size() == 6);
        // Graces at 0 (in voice order), mains 24 ms = 1152 samples later.
        CHECK(d.events[0].sample == 0);
        CHECK(d.events[0].voice == BD);
        CHECK_THAT(d.events[0].volts, WithinAbs(2.4, 1e-6));
        CHECK(d.events[0].level == StepLevel::Weak);
        CHECK(d.events[1].voice == SD);
        CHECK_THAT(d.events[1].volts, WithinAbs(1.5, 1e-6));  // 0.6 x 2.2 V is below the 1.5 V floor
        CHECK(d.events[2].voice == LT);
        CHECK_THAT(d.events[2].volts, WithinAbs(0.6 * 11.0, 1e-5));
        CHECK(d.events[3] == Ev{1152, BD, 4.0f, StepLevel::Normal});
        CHECK(d.events[4] == Ev{1152, SD, 2.2f, StepLevel::Weak});
        CHECK(d.events[5].sample == 1152);
        CHECK_THAT(d.events[5].volts, WithinAbs(11.0, 1e-5));
        CHECK(d.events[5].level == StepLevel::Accent);
    }
}

TEST_CASE("sub-steps: N hits a step/N apart; with flam only the first sub-hit flams", "[seq][steps]") {
    for (int n = 1; n <= 4; ++n) {
        auto s = makeSeq(kSr);
        Pattern p;
        p.at(0, CH, 1).setLevel(StepLevel::Normal);
        p.at(0, CH, 1).setSubSteps(n);
        p.shuffle = 100;  // step 1 is odd: every sub-hit moves with it
        s->setPattern(0, p);
        Driver d(*s);
        d.runUntil(internalSettings(120.0), kMeasure, 333);
        REQUIRE(d.events.size() == static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k)
            CHECK(d.events[static_cast<std::size_t>(k)].sample == kStep + 2000 + k * kStep / n);
    }
    auto s = makeSeq(kSr);
    Pattern p;
    p.at(0, CH, 0).setLevel(StepLevel::Accent);
    p.at(0, CH, 0).setSubSteps(4);
    p.at(0, CH, 0).setFlam(true);
    s->setPattern(0, p);
    Settings st = internalSettings(120.0);
    st.flamMs = 24.0f;
    Driver d(*s);
    d.runUntil(st, kMeasure, 4096);
    REQUIRE(samplesOf(d.events) == std::vector<std::int64_t>{0, 1152, 1500, 3000, 4500});
    for (std::size_t i = 1; i < 5; ++i) CHECK(d.events[i].level == StepLevel::Accent);  // same level
    CHECK(d.events[0].volts < d.events[1].volts);
}

TEST_CASE("probability: frequencies match the code and repeat exactly", "[seq][steps]") {
    auto s = makeSeq(44100.0);
    Pattern p;
    p.length = 16;
    p.scale = Scale::ThirtySecond;
    for (int code = 0; code < 8; ++code)
        for (int st = 0; st < 16; ++st) {
            p.at(0, code, st).setLevel(StepLevel::Normal);
            p.at(0, code, st).setProbabilityCode(code);
        }
    s->setPattern(0, p);
    const Settings st = internalSettings(400.0);
    const int measures = 1500;
    const std::int64_t total = static_cast<std::int64_t>(measures * 2.0 * 60.0 / 400.0 * 44100.0);
    auto first = runInternal(*s, st, fixedBlocks(total, 8192));
    s->reset();
    auto second = runInternal(*s, st, fixedBlocks(total, 1000));
    REQUIRE(first == second);

    std::array<int, 8> counts{};
    for (const Ev& e : first) ++counts[static_cast<std::size_t>(e.voice)];
    const double trials = measures * 16.0;
    for (int code = 0; code < 8; ++code) {
        const double expected = (8 - code) / 8.0;
        INFO("code " << code);
        CHECK_THAT(counts[static_cast<std::size_t>(code)] / trials, WithinAbs(expected, 0.02));
    }
    // Decisions differ from measure to measure (hash of measure, step, track).
    auto onsIn = [&](int voice, std::int64_t measure) {
        std::vector<int> ons;
        const double measureSamples = 2.0 * 60.0 / 400.0 * 44100.0;
        for (const Ev& e : first)
            if (e.voice == voice && static_cast<std::int64_t>(static_cast<double>(e.sample) / measureSamples) == measure)
                ons.push_back(static_cast<int>(std::lround(
                    (static_cast<double>(e.sample) - static_cast<double>(measure) * measureSamples) / (measureSamples / 16))));
        return ons;
    };
    int same = 0;
    for (int m = 0; m < 20; ++m) same += onsIn(4, m) == onsIn(4, m + 1) ? 1 : 0;
    CHECK(same < 3);
}

TEST_CASE("per-track length: a voice of length L reads (measure*length + step) mod L", "[seq][steps]") {
    auto s = makeSeq(kSr);
    Pattern p;
    p.at(0, BD, 0).setLevel(StepLevel::Normal);
    p.trackLength[BD] = 3;
    p.at(0, SD, 2).setLevel(StepLevel::Normal);
    p.trackLength[SD] = 5;
    p.at(0, CH, 20).setLevel(StepLevel::Normal);  // longer than the pattern
    p.trackLength[CH] = 24;
    p.at(0, kAccentTrack, 0).setLevel(StepLevel::Normal);  // follows the pattern (16)
    s->setPattern(0, p);
    Driver d(*s);
    d.runUntil(internalSettings(120.0), 4 * kMeasure, 1024);
    for (const Ev& e : d.events) {
        const std::int64_t n = e.sample / kStep;  // global step index
        REQUIRE(e.sample % kStep == 0);
        if (e.voice == BD) CHECK(n % 3 == 0);
        if (e.voice == SD) CHECK(n % 5 == 2);
        if (e.voice == CH) CHECK(n % 24 == 20);
        // Accent only on pattern step 0 of each measure.
        CHECK((e.level == StepLevel::Accent) == (n % 16 == 0));
    }
    CHECK(onlyVoice(d.events, BD).size() == 22);  // ceil(64 / 3)
    CHECK(onlyVoice(d.events, SD).size() == 13);  // n = 2, 7, ..., 62
    CHECK(onlyVoice(d.events, CH).size() == 2);   // n = 20, 44
}

TEST_CASE("global accent promotes one class; an accented step gets +0.25 knob", "[seq][steps]") {
    struct Expect {
        int voice;
        StepLevel level;
        float volts;
    };
    for (float knob : {0.0f, 0.7f, 0.9f}) {
        auto s = makeSeq(kSr);
        Pattern p;
        for (int st = 0; st < 2; ++st) {
            p.at(0, BD, st).setLevel(StepLevel::Weak);
            p.at(0, SD, st).setLevel(StepLevel::Normal);
            p.at(0, LT, st).setLevel(StepLevel::Accent);
        }
        p.at(0, kAccentTrack, 0).setLevel(StepLevel::Weak);  // any non-zero level means "on"
        s->setPattern(0, p);
        Settings st = internalSettings(120.0);
        st.accent = knob;
        Driver d(*s);
        d.runUntil(st, 2 * kStep, 512);
        const float acc = 4.0f + 10.0f * knob;
        const float acc2 = 4.0f + 10.0f * std::min(1.0f, knob + 0.25f);
        const std::vector<Ev> want = {
            {0, BD, 4.0f, StepLevel::Normal},          {0, SD, acc, StepLevel::Accent},
            {0, LT, acc2, StepLevel::Accent},          {kStep, BD, 2.2f, StepLevel::Weak},
            {kStep, SD, 4.0f, StepLevel::Normal},      {kStep, LT, acc, StepLevel::Accent},
        };
        INFO("knob " << knob);
        REQUIRE(d.events.size() == want.size());
        for (std::size_t i = 0; i < want.size(); ++i) {
            CHECK(d.events[i].sample == want[i].sample);
            CHECK(d.events[i].voice == want[i].voice);
            CHECK(d.events[i].level == want[i].level);
            CHECK_THAT(d.events[i].volts, WithinAbs(want[i].volts, 1e-5));
        }
    }
}

TEST_CASE("AB variation alternates by measure index and starts on A", "[seq][steps]") {
    auto s = makeSeq(kSr);
    Pattern p;
    p.at(0, BD, 0).setLevel(StepLevel::Normal);  // A
    p.at(1, SD, 0).setLevel(StepLevel::Normal);  // B
    s->setPattern(0, p);
    Settings st = internalSettings(120.0);
    st.variation = VariationMode::AB;
    Driver d(*s);
    d.runUntil(st, 4 * kMeasure, 999);
    REQUIRE(d.events.size() == 4);
    CHECK(d.events[0].voice == BD);
    CHECK(d.events[1].voice == SD);
    CHECK(d.events[2].voice == BD);
    CHECK(d.events[3].voice == SD);
    CHECK(s->currentVariation() == 1);

    SECTION("host start in an odd measure plays B") {
        Settings hs = hostSettings();
        hs.variation = VariationMode::AB;
        s->reset();
        Driver h(*s);
        const auto e = h.host(4.0, 120.0, hs, 100);
        REQUIRE(e.size() == 1);
        CHECK(e[0].voice == SD);
        CHECK(s->currentVariation() == 1);
    }
    SECTION("a variation switch takes effect at the next measure") {
        s->reset();
        Driver h(*s);
        Settings a = internalSettings(120.0);
        h.runUntil(a, kMeasure / 2, 512);
        a.variation = VariationMode::B;
        h.runUntil(a, 2 * kMeasure, 512);
        REQUIRE(h.events.size() == 2);
        CHECK(h.events[0].voice == BD);
        CHECK(h.events[1].voice == SD);
    }
}

TEST_CASE("pattern selection: queued at the measure end, immediate, and while stopped", "[seq][steps]") {
    auto s = makeSeq(kSr);
    s->setPattern(0, everyStep(BD));
    s->setPattern(1, everyStep(SD));
    s->setPattern(2, everyStep(CH, 8));
    const Settings st = internalSettings(120.0);
    Driver d(*s);

    SECTION("queued") {
        d.runUntil(st, 33000, 1000);
        s->selectPattern(1);
        CHECK(s->queuedPattern() == 1);
        CHECK(s->currentPattern() == 0);
        CHECK(s->selectedPattern() == 0);
        d.runUntil(st, kMeasure, 1000);  // the downbeat is not processed yet
        CHECK(s->queuedPattern() == 1);
        d.runUntil(st, 2 * kMeasure, 1000);
        for (const Ev& e : d.events) CHECK(e.voice == (e.sample < kMeasure ? BD : SD));
        CHECK(d.events.size() == 32);
        CHECK(s->queuedPattern() == -1);
        CHECK(s->selectedPattern() == 1);
        CHECK(s->currentPattern() == 1);
    }
    SECTION("queued in the block just before the downbeat still lands on it") {
        d.runUntil(st, kMeasure, 4000);
        s->selectPattern(1);
        const auto e = d.runUntil(st, kMeasure + 10, 10);
        REQUIRE(e.size() == 1);
        CHECK(e[0].voice == SD);
    }
    SECTION("re-selecting the current pattern cancels the queue") {
        d.runUntil(st, 10000, 1000);
        s->selectPattern(1);
        s->selectPattern(0);
        CHECK(s->queuedPattern() == -1);
        d.runUntil(st, 2 * kMeasure, 1000);
        for (const Ev& e : d.events) CHECK(e.voice == BD);
    }
    SECTION("immediate keeps the step position") {
        d.runUntil(st, 33000, 1000);  // steps 0..5 fired
        s->selectPattern(1, true);
        CHECK(s->currentPattern() == 1);
        CHECK(s->currentStep() == 5);
        d.runUntil(st, 2 * kMeasure, 1000);
        CHECK(d.events.size() == 32);
        for (const Ev& e : d.events) CHECK(e.voice == (e.sample < 33000 ? BD : SD));
    }
    SECTION("immediate to a shorter pattern ends the measure at its length") {
        d.runUntil(st, 33000, 1000);
        s->selectPattern(2, true);
        d.runUntil(st, 48000 + 8 * kStep, 1000);
        const auto t = samplesOf(onlyVoice(d.events, CH));
        // Steps 6, 7 of pattern 2, then a new 8-step measure from 48000.
        REQUIRE(t.front() == 36000);
        REQUIRE(t[2] == 48000);
        CHECK(t.size() == 10);
        CHECK(s->measureCount() == 1);
    }
    SECTION("while stopped the selection is immediate") {
        s->selectPattern(2);
        CHECK(s->currentPattern() == 2);
        CHECK(s->selectedPattern() == 2);
        CHECK(s->queuedPattern() == -1);
        const auto e = d.runUntil(st, 100, 100);
        REQUIRE(e.size() == 1);
        CHECK(e[0].voice == CH);
    }
    SECTION("a queue pending at stop takes over at once") {
        d.runUntil(st, 10000, 1000);
        s->selectPattern(1);
        Settings off = st;
        off.run = false;
        d.block(Transport{}, off, 100);
        CHECK(s->selectedPattern() == 1);
        CHECK(s->queuedPattern() == -1);
    }
}

TEST_CASE("chains: each measure plays the next entry; relocation walks from PPQ 0", "[seq][steps]") {
    auto s = makeSeq(kSr);
    s->setPattern(2, patternWith(BD, {0}));
    s->setPattern(5, patternWith(SD, {0}, 8));  // 2 qn long
    s->setPattern(7, patternWith(CH, {0}));
    Settings st = hostSettings();
    st.chain = true;

    SECTION("equal lengths") {
        Chain c;
        c.length = 3;
        c.entries[0] = 2;
        c.entries[1] = 7;
        c.entries[2] = 2;
        s->setChain(c);
        const auto ev = runHost(*s, st, kSr, 120.0, 0.0, fixedBlocks(5 * kMeasure, 700));
        REQUIRE(ev.size() == 5);
        const int voices[] = {BD, CH, BD, BD, CH};
        for (int i = 0; i < 5; ++i) {
            CHECK(ev[static_cast<std::size_t>(i)].voice == voices[i]);
            CHECK(ev[static_cast<std::size_t>(i)].sample == i * kMeasure);
        }
        // Relocate into measure 4 (entry 1): pattern 7 is playing.
        s->reset();
        Driver d(*s);
        d.host(17.0, 120.0, st, 100);
        CHECK(s->currentPattern() == 7);
        CHECK(s->measureCount() == 4);
        CHECK(s->selectedPattern() == 0);  // the chain does not touch the selection
    }
    SECTION("mixed lengths: 4 + 2 qn cycle") {
        Chain c;
        c.length = 2;
        c.entries[0] = 2;
        c.entries[1] = 5;
        s->setChain(c);
        Driver d(*s);
        d.host(7.0, 120.0, st, 100);  // cycle 1 (6..12): measure 2 (pattern 2) from 6.0
        CHECK(s->currentPattern() == 2);
        CHECK(s->measureCount() == 2);
        CHECK(s->currentStep() == 4);
        double ppq = 7.0 + 100 / 24000.0;
        while (d.pos < 6 * 24000) {
            d.host(ppq, 120.0, st, 1000);
            ppq += 1000 / 24000.0;
        }
        // Downbeats: SD at 10.0 qn, BD at 12.0 (start was 7.0 at sample 0).
        REQUIRE(d.events.size() == 2);
        CHECK(d.events[0] == Ev{72000, SD, 4.0f, StepLevel::Normal});
        CHECK(d.events[1] == Ev{120000, BD, 4.0f, StepLevel::Normal});
        // A pure function of PPQ: the same blocks from 0 give the same events.
        s->reset();
        const auto ref = runHost(*s, st, kSr, 120.0, 0.0, fixedBlocks(24000 * 13, 4096));
        std::vector<std::int64_t> late;
        for (const Ev& e : ref)
            if (e.sample >= 7 * 24000) late.push_back(e.sample - 7 * 24000);
        CHECK(late == samplesOf(d.events));
    }
}

TEST_CASE("auto fill-in replaces the last measure of each group; manual fill once", "[seq][steps]") {
    auto s = makeSeq(kSr);
    s->setPattern(1, patternWith(BD, {0}));
    s->setPattern(14, patternWith(SD, {0}));  // bank 0, fill slot 2
    s->setPattern(33, patternWith(CB, {0}));  // bank 2 basic
    s->setPattern(46, patternWith(CY, {0}));  // bank 2, fill slot 2
    s->selectPattern(1);
    Settings st = internalSettings(120.0);
    st.fillSlot = 2;

    SECTION("every 4") {
        st.autoFill = AutoFill::Every4;
        Driver d(*s);
        d.runUntil(st, 3 * kMeasure + 100, 1000);
        CHECK(s->currentPattern() == 14);
        CHECK(s->selectedPattern() == 1);
        d.runUntil(st, 9 * kMeasure, 1000);
        const int voices[] = {BD, BD, BD, SD, BD, BD, BD, SD, BD};
        REQUIRE(d.events.size() == 9);
        for (int i = 0; i < 9; ++i) CHECK(d.events[static_cast<std::size_t>(i)].voice == voices[i]);
    }
    SECTION("the fill comes from the bank of the selected pattern") {
        st.autoFill = AutoFill::Every2;
        s->selectPattern(33);
        Driver d(*s);
        d.runUntil(st, 4 * kMeasure, 1000);
        const int voices[] = {CB, CY, CB, CY};
        REQUIRE(d.events.size() == 4);
        for (int i = 0; i < 4; ++i) CHECK(d.events[static_cast<std::size_t>(i)].voice == voices[i]);
    }
    SECTION("manual fill plays once, at the next measure") {
        Driver d(*s);
        d.runUntil(st, kMeasure + 5000, 1000);
        s->requestFill();
        d.runUntil(st, 5 * kMeasure, 1000);
        const int voices[] = {BD, BD, SD, BD, BD};
        REQUIRE(d.events.size() == 5);
        for (int i = 0; i < 5; ++i) CHECK(d.events[static_cast<std::size_t>(i)].voice == voices[i]);
    }
    SECTION("fill requested while stopped plays as an uncounted intro") {
        s->requestFill();
        Driver d(*s);
        d.runUntil(st, 100, 100);
        CHECK(s->currentPattern() == 14);
        CHECK(s->measureCount() == -1);
        d.runUntil(st, 3 * kMeasure, 1000);
        const int voices[] = {SD, BD, BD};
        REQUIRE(d.events.size() == 3);
        for (int i = 0; i < 3; ++i) CHECK(d.events[static_cast<std::size_t>(i)].voice == voices[i]);
        CHECK(s->measureCount() == 1);
    }
    SECTION("host sync with a fill of a different length stays a pure function of PPQ") {
        Pattern shortFill = patternWith(SD, {0}, 8);  // 2 qn
        s->setPattern(14, shortFill);
        Settings hs = hostSettings();
        hs.fillSlot = 2;
        hs.autoFill = AutoFill::Every4;
        // Group = 3 x 4 + 2 = 14 qn. PPQ 27 is in the fill of group 1 (26..28).
        Driver d(*s);
        d.host(27.0, 120.0, hs, 100);
        CHECK(s->measureCount() == 7);
        CHECK(s->currentPattern() == 14);
        s->reset();
        const auto ref = runHost(*s, hs, kSr, 120.0, 0.0, fixedBlocks(24000 * 30, 2048));
        const auto t = samplesOf(ref);
        const std::vector<std::int64_t> want = {0, 4 * 24000, 8 * 24000, 12 * 24000, 14 * 24000,
                                                18 * 24000, 22 * 24000, 26 * 24000, 28 * 24000};
        CHECK(t == want);
        CHECK(ref[3].voice == SD);
        CHECK(ref[7].voice == SD);
    }
}

TEST_CASE("live recording quantises to the nearest step and never double-fires", "[seq][steps]") {
    auto s = makeSeq(kSr);
    s->setPattern(0, patternWith(BD, {0}));
    Settings st = internalSettings(120.0);
    st.record = true;
    Driver d(*s);
    d.runUntil(st, 23000, 1000);  // next step: 4 at 24000

    SECTION("hit just before the upcoming step: recorded there, not fired again this pass") {
        const RecordResult r = s->recordHit(SD, false, 500);  // at 23500
        CHECK(r.changed);
        CHECK(r.pattern == 0);
        CHECK(r.variation == 0);
        CHECK(r.track == SD);
        CHECK(r.step == 4);
        CHECK(r.cell.level() == StepLevel::Normal);
        CHECK(s->bank().patterns[0].at(0, SD, 4).level() == StepLevel::Normal);
        d.runUntil(st, 2 * kMeasure, 1000);
        const auto sd = onlyVoice(d.events, SD);
        REQUIRE(sd.size() == 1);
        CHECK(sd[0].sample == kMeasure + 24000);  // the next pass plays it
    }
    SECTION("hit just after a step that already fired: recorded, plays next pass") {
        d.runUntil(st, 25000, 1000);
        const RecordResult r = s->recordHit(SD, true, 1000);  // at 26000 -> step 4
        CHECK(r.changed);
        CHECK(r.step == 4);
        CHECK(r.cell.level() == StepLevel::Accent);
        d.runUntil(st, 2 * kMeasure, 1000);
        const auto sd = onlyVoice(d.events, SD);
        REQUIRE(sd.size() == 1);
        CHECK(sd[0].sample == kMeasure + 24000);
        CHECK(sd[0].level == StepLevel::Accent);
    }
    SECTION("hit at the end of the measure goes to step 0 and skips the next downbeat once") {
        d.runUntil(st, kMeasure - 1000, 1000);
        const RecordResult r = s->recordHit(BD, true, 800);  // 200 samples before the downbeat
        CHECK(r.step == 0);
        d.runUntil(st, 3 * kMeasure, 1000);
        const auto bd = samplesOf(onlyVoice(d.events, BD));
        CHECK(bd == std::vector<std::int64_t>{0, 2 * kMeasure});
    }
    SECTION("no effect without record or when stopped") {
        Settings noRec = st;
        noRec.record = false;
        d.block(Transport{}, noRec, 100);
        CHECK_FALSE(s->recordHit(SD, false, 0).changed);
        CHECK_FALSE(s->bank().patterns[0].at(0, SD, 4).on());
        auto stopped = makeSeq(kSr);
        CHECK_FALSE(stopped->recordHit(SD, false, 0).changed);
    }
    SECTION("polymeter: the recorded cell is the one that plays at that time") {
        Pattern p = patternWith(BD, {0});
        p.trackLength[CH] = 3;
        s->setPattern(0, p);
        const RecordResult r = s->recordHit(CH, false, 500);  // step 4 of measure 0 -> CH cell 4 mod 3 = 1
        CHECK(r.step == 1);
    }
}

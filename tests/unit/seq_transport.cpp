// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Sequencer transport handling: relocation, host loops, stop/start, tempo
// changes, the Internal and HostNote clocks and mode switches.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "SeqTestUtil.h"

using namespace seqtest;

namespace {
constexpr double kSr = 48000.0;
constexpr double kSpp = 24000.0;  // samples per quarter note at 120 BPM / 48 kHz
}  // namespace

TEST_CASE("a forward jump re-anchors and skips the steps in between", "[seq][transport]") {
    auto s = makeSeq(kSr);
    Pattern p = everyStep(BD);
    p.at(0, SD, 3).setLevel(StepLevel::Normal);
    p.at(0, SD, 3).setFlam(true);
    s->setPattern(0, p);
    Settings st = hostSettings();
    st.flamMs = 100.0f;  // 4800 samples: the main SD hit is still pending at the jump
    Driver d(*s);
    for (int i = 0; i < 38; ++i) d.host(i * 500 / kSpp, 120.0, st, 500);  // to sample 19000
    REQUIRE(onlyVoice(d.events, SD).size() == 1);                         // grace at 18000
    const auto after = d.host(8.5, 120.0, st, 7000);
    // 8.5 qn is step 34 = step 2 of measure 2: fires at offset 0, then step 3 at 6000.
    REQUIRE(after.size() == 3);
    CHECK(after[0].sample - 19000 == 0);
    CHECK(after[0].voice == BD);
    CHECK(after[1].sample - 19000 == 6000);
    CHECK(after[2].sample - 19000 == 6000);
    CHECK(after[2].voice == SD);  // the new step 3 grace; the old pending main hit is gone
    CHECK(after[2].volts < 4.0f);
    CHECK(s->measureCount() == 2);
    CHECK(s->currentStep() == 3);
}

TEST_CASE("a backward jump re-anchors without replaying the stale pending hits", "[seq][transport]") {
    auto s = makeSeq(kSr);
    Pattern p = everyStep(BD);
    p.shuffle = 100;  // odd steps 2000 samples late
    s->setPattern(0, p);
    Driver d(*s);
    const Settings st = hostSettings();
    d.host(0.0, 120.0, st, 7000);  // step 0 at 0; step 1 due at 8000 is pending
    const auto ev = d.host(2.0, 120.0, st, 1000);  // jump back... to 2.0 (a jump either way)
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].sample == 7000);  // step 8 at offset 0
    const auto ev2 = d.host(2.0 + 1000 / kSpp, 120.0, st, 8000);
    REQUIRE(ev2.size() == 1);
    CHECK(ev2[0].sample == 7000 + 8000);  // step 9 (odd) at 6000 + 2000 after the jump
}

TEST_CASE("host loop: the loop start downbeat fires and nothing stale leaks across", "[seq][transport]") {
    auto s = makeSeq(kSr);
    Pattern p = everyStep(BD);
    p.shuffle = 100;
    p.at(0, SD, 15).setLevel(StepLevel::Normal);
    p.at(0, SD, 15).setFlam(true);
    s->setPattern(0, p);
    Settings st = hostSettings();
    st.flamMs = 100.0f;
    // Step 15 sits at 3.75 + 1/12 qn (sample 92000); its main hit would be
    // at 96800, past the loop end at 4 qn (96000).
    for (int blockSize : {500, 512, 4096}) {
        s->reset();
        Driver d(*s);
        for (int pass = 0; pass < 3; ++pass) {
            std::int64_t local = 0;
            while (local < 96000) {
                const int n = static_cast<int>(std::min<std::int64_t>(blockSize, 96000 - local));
                d.host(static_cast<double>(local) / kSpp, 120.0, st, n);
                local += n;
            }
        }
        INFO("block size " << blockSize);
        const auto bd = onlyVoice(d.events, BD);
        const auto sd = onlyVoice(d.events, SD);
        REQUIRE(bd.size() == 48);   // 16 per pass, every downbeat included
        // Graces only: each main hit is dropped by the loop relocation (and
        // the last one is still pending when the run ends).
        REQUIRE(sd.size() == 3);
        CHECK(sd[0].sample == 92000);
        CHECK(sd[1].sample == 96000 + 92000);
        CHECK(sd[2].sample == 192000 + 92000);
        for (int pass = 0; pass < 3; ++pass) CHECK(bd[static_cast<std::size_t>(pass * 16)].sample == pass * 96000);
    }
}

TEST_CASE("transport stop drops pending hits and resets the step; restart is fresh", "[seq][transport]") {
    auto s = makeSeq(kSr);
    Pattern p = patternWith(SD, {0});
    p.at(0, SD, 0).setFlam(true);
    s->setPattern(0, p);
    Settings st = hostSettings();
    st.flamMs = 50.0f;  // main hit at 2400
    Driver d(*s);
    d.host(0.0, 120.0, st, 1000);
    CHECK(s->isPlaying());
    CHECK(s->currentStep() == 0);
    const auto stopped = d.host(1000 / kSpp, 120.0, st, 5000, false);
    CHECK(stopped.empty());
    CHECK_FALSE(s->isPlaying());
    CHECK(s->currentStep() == -1);
    // Many stopped blocks: still nothing.
    for (int i = 0; i < 10; ++i) CHECK(d.host(1.0, 120.0, st, 512, false).empty());
    // Restart at 0: the grace fires at 0 and the main at 2400, once each.
    const auto again = d.host(0.0, 120.0, st, 4000);
    REQUIRE(again.size() == 2);
    CHECK(again[0].sample - (d.pos - 4000) == 0);
    CHECK(again[1].sample - (d.pos - 4000) == 2400);
}

TEST_CASE("Internal clock: run starts at 0, tempo changes take effect per call", "[seq][transport]") {
    auto s = makeSeq(kSr);
    s->setPattern(0, everyStep(BD));
    Settings st = internalSettings(120.0);
    Driver d(*s);
    d.runUntil(st, 15000, 1000);  // steps at 0, 6000, 12000; PPQ now 0.625
    st.internalBpm = 60.0;
    d.runUntil(st, 70000, 777);
    const auto t = samplesOf(d.events);
    // The remaining 0.125 qn to step 3 takes 6000 samples at 60 BPM.
    REQUIRE(t == std::vector<std::int64_t>{0, 6000, 12000, 21000, 33000, 45000, 57000, 69000});
    CHECK(s->tempo() == 60.0);

    // Stop, then start again: back to PPQ 0, measure 0, step 0 at offset 0.
    st.run = false;
    CHECK(d.block(Transport{}, st, 3000).empty());
    CHECK_FALSE(s->isPlaying());
    st.run = true;
    const auto restart = d.block(Transport{}, st, 100);
    REQUIRE(restart.size() == 1);
    CHECK(restart[0].sample == d.pos - 100);
    CHECK(s->measureCount() == 0);
    CHECK(s->currentStep() == 0);
}

TEST_CASE("HostSync follows host tempo changes without seeing a relocation", "[seq][transport]") {
    auto s = makeSeq(kSr);
    s->setPattern(0, everyStep(BD));
    const Settings st = hostSettings();
    Driver d(*s);
    d.host(0.0, 120.0, st, 15000);
    d.host(0.625, 60.0, st, 60000);
    REQUIRE(samplesOf(d.events) == std::vector<std::int64_t>{0, 6000, 12000, 21000, 33000, 45000, 57000, 69000});
}

TEST_CASE("Internal mode uses host tempo when the host provides one", "[seq][transport]") {
    auto s = makeSeq(kSr);
    s->setPattern(0, everyStep(BD));
    const Settings st = internalSettings(120.0);
    Driver d(*s);
    Transport t;
    t.hostValid = true;
    t.playing = false;  // the host transport does not matter in Internal mode
    t.bpm = 60.0;
    t.ppq = 123.0;
    d.block(t, st, 30000);
    CHECK(samplesOf(d.events) == std::vector<std::int64_t>{0, 12000, 24000});
    CHECK(s->tempo() == 60.0);
}

TEST_CASE("HostNote: a held pattern note plays that pattern from its top", "[seq][transport]") {
    auto s = makeSeq(kSr);
    s->setPattern(0, everyStep(BD));
    s->setPattern(3, everyStep(SD));
    Settings st;
    st.mode = Mode::HostNote;
    Driver d(*s);
    CHECK(d.block(Transport{}, st, 1000).empty());
    CHECK_FALSE(s->isPlaying());

    s->notePatternOn(3);
    auto ev = d.block(Transport{}, st, 7000);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0] == Ev{1000, SD, 4.0f, StepLevel::Normal});
    CHECK(ev[1].sample == 7000);
    CHECK(s->selectedPattern() == 3);
    CHECK(s->currentPattern() == 3);

    s->notePatternOn(0);  // a new note restarts from the top with its pattern
    ev = d.block(Transport{}, st, 100);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0] == Ev{8000, BD, 4.0f, StepLevel::Normal});
    CHECK(s->currentStep() == 0);

    s->notePatternOff();
    CHECK(d.block(Transport{}, st, 20000).empty());
    CHECK_FALSE(s->isPlaying());
    CHECK(s->currentStep() == -1);
}

TEST_CASE("mode switches and invalid hosts", "[seq][transport]") {
    auto s = makeSeq(kSr);
    s->setPattern(0, everyStep(BD));
    Driver d(*s);
    SECTION("Off is silent") {
        Settings st;
        st.mode = Mode::Off;
        st.run = true;
        CHECK(d.host(0.0, 120.0, st, 48000).empty());
        CHECK_FALSE(s->isPlaying());
    }
    SECTION("HostSync without a valid host does not play") {
        Transport t;
        t.playing = true;
        t.hostValid = false;
        CHECK(d.block(t, hostSettings(), 48000).empty());
        Transport bad{true, true, std::nan(""), 120.0};
        CHECK(d.block(bad, hostSettings(), 48000).empty());
        CHECK_FALSE(s->isPlaying());
    }
    SECTION("switching HostSync -> Internal while playing restarts at 0") {
        d.host(1.1, 120.0, hostSettings(), 1000);
        CHECK(s->measureCount() == 0);
        const auto ev = d.block(Transport{}, internalSettings(120.0), 100);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].sample == 1000);
        CHECK(s->currentStep() == 0);
    }
    SECTION("zero-length calls change nothing") {
        d.host(0.0, 120.0, hostSettings(), 1000);
        for (int i = 0; i < 5; ++i) CHECK(d.host(1000 / kSpp, 120.0, hostSettings(), 0).empty());
        const auto ev = d.host(1000 / kSpp, 120.0, hostSettings(), 6000);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].sample == 6000);
    }
}

TEST_CASE("reset stops and rewinds but keeps the bank", "[seq][transport]") {
    auto s = makeSeq(kSr);
    Pattern p = everyStep(CB);
    s->setPattern(9, p);
    s->selectPattern(9);
    Driver d(*s);
    d.runUntil(internalSettings(120.0), 30000, 512);
    s->reset();
    CHECK_FALSE(s->isPlaying());
    CHECK(s->currentStep() == -1);
    CHECK(s->bank().patterns[9] == p);
    CHECK(s->selectedPattern() == 9);
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// PatternModel (docs/design/plugin.md §3): edits reach the audio thread, none
// is lost when the command queue is full, whole banks supersede queued edits,
// and what the audio thread records or selects flows back into the model.

#include <catch2/catch_test_macros.hpp>

#include "TestSupport.h"

using namespace plugintest;
using mw808::seq::Pattern;
using mw808::seq::StepLevel;

namespace {

struct Recorder final : mw808::plugin::PatternModel::Listener {
    std::vector<int> patterns;
    int bank = 0, selection = 0, chain = 0;
    void patternChanged(int index) override { patterns.push_back(index); }
    void bankChanged() override { ++bank; }
    void selectionChanged() override { ++selection; }
    void chainChanged() override { ++chain; }
};

const mw808::seq::PatternBank& audioBank(mw808Processor& p) { return p.machineForTests().sequencer().bank(); }

Pattern clavesOn(std::initializer_list<int> steps) {
    Pattern p;
    for (int s : steps) p.at(0, mw808::CL, s).setLevel(StepLevel::Normal);
    return p;
}

}  // namespace

TEST_CASE("patterns: a fresh instance has handed its bank to the audio thread", "[plugin][patterns]") {
    auto p = makeProcessor();
    Host host(*p);
    host.process(64);  // no timer, no service call
    CHECK(sameBank(audioBank(*p), p->patterns().bank()));
    CHECK(p->playState().selectedPattern.load() == p->patterns().selectedPattern());
}

TEST_CASE("patterns: single edits arrive with the next block", "[plugin][patterns]") {
    auto p = makeProcessor();
    Host host(*p);
    Recorder recorder;
    p->patterns().addListener(&recorder);
    Rng rng(11U);

    const Pattern a = randomPattern(rng);
    p->patterns().setPattern(17, a);
    CHECK(p->patterns().pattern(17) == a);
    CHECK(recorder.patterns == std::vector<int>{17});
    host.process(32);
    CHECK(audioBank(*p).patterns[17] == a);

    // edit(): read-modify-write of one pattern.
    p->patterns().edit(17, [](Pattern& pattern) { pattern.at(1, mw808::CB, 63).setLevel(StepLevel::Accent); });
    CHECK(p->patterns().pattern(17).at(1, mw808::CB, 63).level() == StepLevel::Accent);
    CHECK(recorder.patterns == std::vector<int>{17, 17});
    host.process(32);
    CHECK(audioBank(*p).patterns[17] == p->patterns().pattern(17));

    // Setting the same content again is not an edit.
    p->patterns().setPattern(17, p->patterns().pattern(17));
    CHECK(recorder.patterns.size() == 2U);

    const mw808::seq::Chain chain = randomChain(rng);
    p->patterns().setChain(chain);
    CHECK(recorder.chain == 1);
    host.process(32);
    CHECK(audioBank(*p).chain == chain);
    CHECK(p->patterns().chain() == chain);

    // Out-of-range indices are ignored.
    p->patterns().setPattern(-1, a);
    p->patterns().setPattern(64, a);
    p->patterns().edit(99, [](Pattern&) { FAIL("must not be called"); });
    CHECK(recorder.patterns.size() == 2U);

    // Illegal field values never reach the audio thread.
    Pattern bad = a;
    bad.length = 0;
    bad.shuffle = 250;
    bad.scale = static_cast<mw808::seq::Scale>(99);
    p->patterns().setPattern(3, bad);
    host.process(32);
    CHECK(audioBank(*p).patterns[3].length >= 1);
    CHECK(audioBank(*p).patterns[3].shuffle <= 100);
    CHECK(static_cast<int>(audioBank(*p).patterns[3].scale) < static_cast<int>(mw808::seq::Scale::kNumScales));
    CHECK(audioBank(*p).patterns[3] == p->patterns().pattern(3));

    p->patterns().removeListener(&recorder);
}

TEST_CASE("patterns: a full command queue marks edits dirty and re-posts them", "[plugin][patterns]") {
    auto p = makeProcessor();
    Host host(*p);
    host.process(32);
    Rng rng(12U);

    // No audio block runs while 5 rounds of edits to all 64 patterns are made:
    // far more than the queue holds.
    for (int round = 0; round < 5; ++round)
        for (int i = 0; i < mw808::seq::kNumPatterns; ++i) p->patterns().setPattern(i, randomPattern(rng));
    const mw808::seq::Chain chain = randomChain(rng);
    p->patterns().setChain(chain);
    p->patterns().selectPattern(41, true);
    REQUIRE(p->patterns().numDirty() > 0);

    // The model is already right; the audio side is not yet.
    CHECK_FALSE(sameBank(audioBank(*p), p->patterns().bank()));

    // Blocks drain the queue, service passes re-post what did not fit.
    int passes = 0;
    while (p->patterns().numDirty() > 0 && passes < 50) {
        host.process(32);
        p->serviceMessageThread();
        ++passes;
    }
    host.process(32);
    CHECK(p->patterns().numDirty() == 0);
    CHECK(passes < 50);
    CHECK(sameBank(audioBank(*p), p->patterns().bank()));  // every edit arrived, latest content
    CHECK(audioBank(*p).chain == chain);
    CHECK(p->playState().selectedPattern.load() == 41);
    CHECK(p->patterns().selectedPattern() == 41);
}

TEST_CASE("patterns: a new bank supersedes edits still in the queue", "[plugin][patterns]") {
    auto p = makeProcessor();
    Host host(*p);
    host.process(32);
    Rng rng(13U);
    Recorder recorder;
    p->patterns().addListener(&recorder);

    const auto generationBefore = p->patterns().generation();
    for (int i = 0; i < 30; ++i) p->patterns().setPattern(i, randomPattern(rng));  // queued, older generation

    auto bank = std::make_unique<mw808::seq::PatternBank>();
    bank->patterns[2] = clavesOn({0, 4, 8, 12});
    bank->chain = randomChain(rng);
    p->patterns().replaceBank(*bank);
    CHECK(p->patterns().generation() == generationBefore + 1U);
    CHECK(recorder.bank == 1);
    CHECK(sameBank(p->patterns().bank(), *bank));

    host.process(32);
    CHECK(sameBank(audioBank(*p), *bank));  // none of the 30 stale edits applied

    // Edits made after the replacement do apply.
    const Pattern later = randomPattern(rng);
    p->patterns().setPattern(2, later);
    host.process(32);
    CHECK(audioBank(*p).patterns[2] == later);

    // Many replacements in a row: the last one wins, and nothing piles up.
    for (int i = 0; i < 100; ++i) {
        bank->patterns[10] = randomPattern(rng);
        p->patterns().replaceBank(*bank);
        if ((i % 7) == 0) host.process(16);
        if ((i % 13) == 0) p->serviceMessageThread();
    }
    host.process(32);
    p->serviceMessageThread();
    CHECK(sameBank(audioBank(*p), *bank));
    CHECK(sameBank(p->patterns().bank(), *bank));
    p->patterns().removeListener(&recorder);
}

TEST_CASE("patterns: steps recorded on the audio thread reach the model", "[plugin][patterns]") {
    auto p = makeProcessor();
    Host host(*p);
    auto bank = std::make_unique<mw808::seq::PatternBank>();  // empty memory
    p->patterns().replaceBank(*bank);
    p->patterns().selectPattern(0, true);
    setParam(*p, "record", 1.0f);
    host.run(4800, 512);  // rolling
    p->serviceMessageThread();
    REQUIRE(p->playState().playing.load());

    Recorder recorder;
    p->patterns().addListener(&recorder);
    HostListener hostListener(*p);

    // Play the cowbell (normal) and the clap (accent) live.
    host.noteOn(mw808::kVoiceNote[mw808::CB], 96, 100);
    host.process(512);
    host.midi.clear();
    host.run(6000, 512);
    host.noteOn(mw808::kVoiceNote[mw808::CP], 127, 10);
    host.process(512);
    host.midi.clear();
    host.run(1024, 512);

    // Nothing in the model until the service pass drains the notices.
    CHECK(p->patterns().pattern(0) == Pattern{});
    p->serviceMessageThread();

    const Pattern& recorded = p->patterns().pattern(0);
    int cowbells = 0, claps = 0;
    for (int s = 0; s < 16; ++s) {
        if (recorded.at(0, mw808::CB, s).level() == StepLevel::Normal) ++cowbells;
        if (recorded.at(0, mw808::CP, s).level() == StepLevel::Accent) ++claps;
    }
    CHECK(cowbells == 1);
    CHECK(claps == 1);
    CHECK(recorder.patterns == std::vector<int>{0});
    CHECK(hostListener.stateChanges == 1);  // the host is told the session changed

    // Model and audio hold the same pattern, and it is in the saved state.
    host.process(64);
    p->serviceMessageThread();
    CHECK(sameBank(audioBank(*p), p->patterns().bank()));
    auto other = makeProcessor(kRate, kBlock, false);
    loadState(*other, saveState(*p));
    CHECK(other->patterns().pattern(0) == recorded);
    p->patterns().removeListener(&recorder);
}

TEST_CASE("patterns: recording and editing the same pattern at once converges", "[plugin][patterns]") {
    auto p = makeProcessor();
    Host host(*p);
    auto bank = std::make_unique<mw808::seq::PatternBank>();
    p->patterns().replaceBank(*bank);
    setParam(*p, "record", 1.0f);
    host.run(4800, 256);
    p->serviceMessageThread();

    Rng rng(14U);
    for (int i = 0; i < 300; ++i) {
        if ((i % 3) == 0) host.noteOn(mw808::kVoiceNote[static_cast<std::size_t>(randInt(rng, 0, 15))], 100, randInt(rng, 0, 255));
        if ((i % 5) == 0)
            p->patterns().edit(0, [&rng](Pattern& pattern) {
                pattern.at(0, randInt(rng, 0, 15), randInt(rng, 0, 15)).setLevel(StepLevel::Weak);
            });
        if ((i % 41) == 0) p->patterns().replaceBank(*bank);
        host.process(256);
        host.midi.clear();
        if ((i % 2) == 0) p->serviceMessageThread();
    }
    // Stop playing and editing: a few passes later both sides are identical.
    for (int i = 0; i < 8; ++i) {
        host.process(256);
        p->serviceMessageThread();
    }
    CHECK(p->patterns().numDirty() == 0);
    CHECK(sameBank(audioBank(*p), p->patterns().bank()));
}

TEST_CASE("patterns: selection goes both ways", "[plugin][patterns]") {
    auto p = makeProcessor();
    Host host(*p);
    p->patterns().replaceBank(*std::make_unique<mw808::seq::PatternBank>());  // 16-step patterns throughout
    host.head.playing = false;  // stopped: a selection takes over at once
    Recorder recorder;
    p->patterns().addListener(&recorder);

    p->patterns().selectPattern(9);
    CHECK(p->patterns().selectedPattern() == 9);
    CHECK(recorder.selection == 1);
    host.process(64);
    CHECK(p->playState().selectedPattern.load() == 9);
    p->serviceMessageThread();
    CHECK(p->patterns().selectedPattern() == 9);
    CHECK(recorder.selection == 1);  // no echo of our own selection

    p->patterns().selectPattern(64);  // ignored
    p->patterns().selectPattern(-1);
    CHECK(p->patterns().selectedPattern() == 9);

    // While playing, a selection is queued to the end of the measure and the
    // model already shows the target.
    host.head.playing = true;
    host.run(2400, 512);
    p->patterns().selectPattern(11);
    host.process(64);
    CHECK(p->playState().queuedPattern.load() == 11);
    CHECK(p->playState().selectedPattern.load() == 9);
    p->serviceMessageThread();
    CHECK(p->patterns().selectedPattern() == 11);
    host.run(96000 + 4800, 512);  // one measure later
    p->serviceMessageThread();
    CHECK(p->playState().selectedPattern.load() == 11);
    CHECK(p->patterns().selectedPattern() == 11);

    // Host Note mode: a held key selects the pattern on the audio thread, and
    // the model follows through a notice.
    host.head.playing = false;
    setParam(*p, "seq_mode", 3.0f);
    host.run(1024, 512);
    p->serviceMessageThread();
    const int selectionsBefore = recorder.selection;
    host.noteOn(mw808::kPatternNoteBase + 20, 100, 0);
    host.process(512);
    host.midi.clear();
    host.run(48000, 512);
    p->serviceMessageThread();
    if (p->playState().selectedPattern.load() == 20) {
        CHECK(p->patterns().selectedPattern() == 20);
        CHECK(recorder.selection == selectionsBefore + 1);
    } else {
        // The core does not move the basic selection for pattern notes: then
        // the model must not have moved either.
        CHECK(p->patterns().selectedPattern() == p->playState().selectedPattern.load());
    }
    p->patterns().removeListener(&recorder);
}

TEST_CASE("patterns: the service timer runs without an editor", "[plugin][patterns]") {
    // The one test that pumps the message loop, to see the real 30 Hz timer
    // (rather than a direct service call) drain a notice into the model.
    auto p = makeProcessor();
    Host host(*p);
    auto bank = std::make_unique<mw808::seq::PatternBank>();
    p->patterns().replaceBank(*bank);
    setParam(*p, "record", 1.0f);
    host.run(4800, 512);
    // Let the timer see that the audio thread has taken the new bank (until it
    // has, recorded steps are treated as belonging to the old one).
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    host.noteOn(mw808::kVoiceNote[mw808::CL], 96, 0);
    host.process(512);
    host.midi.clear();
    REQUIRE(p->patterns().pattern(p->patterns().selectedPattern()) == Pattern{});

    bool arrived = false;
    for (int i = 0; i < 100 && !arrived; ++i) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        arrived = !(p->patterns().pattern(p->patterns().selectedPattern()) == Pattern{});
    }
    CHECK(arrived);
}

TEST_CASE("patterns: audition, fill and panic reach the audio thread", "[plugin][patterns]") {
    auto p = makeProcessor();
    Host host(*p);
    host.head.playing = false;
    host.process(64);

    const auto before = hits(*p, mw808::CY);
    p->audition(mw808::CY, false);
    p->audition(mw808::CY, true);
    host.process(512);
    CHECK(hits(*p, mw808::CY) == before + 2U);
    CHECK(host.peak(0, 512) > 0.0f);

    // Auditions ignore mute.
    setParam(*p, "cy_mute", 1.0f);
    p->audition(mw808::CY, false);
    host.process(512);
    CHECK(hits(*p, mw808::CY) == before + 3U);

    // Panic silences the ringing cymbal.
    p->panic();
    host.process(512);
    host.process(512);
    CHECK(host.peak(0, 512) == 0.0f);

    // A panic that finds the queue full is retried by the service pass.
    Rng rng(15U);
    p->audition(mw808::CY, true);
    host.process(512);
    REQUIRE(host.peak(0, 512) > 0.0f);
    for (int round = 0; round < 4; ++round)
        for (int i = 0; i < 64; ++i) p->patterns().setPattern(i, randomPattern(rng));
    p->panic();
    for (int i = 0; i < 6; ++i) {
        host.process(512);
        p->serviceMessageThread();
    }
    host.process(512);
    CHECK(host.peak(0, 512) == 0.0f);

    p->requestFill();  // must simply be accepted
    host.process(64);
    CHECK(host.allFinite(64));
}

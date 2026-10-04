// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "CoreTestUtil.h"

using namespace mw808;
using namespace coretest;

namespace {

constexpr double kFs = 48000.0;
constexpr int kMaxBlock = 512;
constexpr int kSixteenth = 6000;  // samples per 16th at 120 BPM, 48 kHz

// Claves on the given steps of pattern 0, variation A. Claves ring ~25 ms, so
// each onset is cleanly separated from the last.
seq::Pattern clavesOn(std::initializer_list<int> steps, seq::StepLevel level = seq::StepLevel::Normal) {
    seq::Pattern p;
    for (int s : steps) p.at(0, CL, s).setLevel(level);
    return p;
}

void post(Machine& m, int index, const seq::Pattern& p, std::uint32_t generation = 0) {
    Command c;
    c.type = Command::Type::SetPattern;
    c.index = index;
    c.pattern = p;
    c.generation = generation;
    REQUIRE(m.post(c));
}

std::uint32_t hits(const Machine& m, VoiceId v) { return m.playState().hits[v].load(); }

}  // namespace

TEST_CASE("A MIDI note sounds at its exact sample offset", "[core][machine]") {
    Machine m;
    m.prepare(kFs, kMaxBlock);
    Runner r{m};
    r.playing = false;
    r.midi = {noteOn(300, kVoiceNote[CL])};
    Buses buses(4096, 1);
    r.run(buses, 4096, 512);

    REQUIRE(peak(buses.left(), 0, 300) == 0.0f);
    const int onset = firstAbove(buses.left(), 1.0e-4f);
    REQUIRE(onset >= 300);
    REQUIRE(onset < 300 + 48);  // the trigger pulse rises within a millisecond
    REQUIRE(hits(m, CL) == 1U);
}

TEST_CASE("Host-synced pattern steps land sample-accurately", "[core][machine]") {
    for (int block : {512, 64, 4096, 333}) {
        Machine m;
        m.prepare(kFs, kMaxBlock);
        post(m, 0, clavesOn({0, 4, 8, 12}));
        Runner r{m};
        const int total = 16 * kSixteenth;  // one measure
        Buses buses(total, 1);
        r.run(buses, total, block);

        INFO("block " << block);
        REQUIRE(hits(m, CL) == 4U);
        for (int beat = 0; beat < 4; ++beat) {
            const int at = beat * 4 * kSixteenth;
            // Quiet just before the step (the previous hit has died away), sounding just after.
            if (beat > 0) REQUIRE(peak(buses.left(), at - 200, at) < 2.0e-4f);
            const int onset = firstAbove(buses.left(), 1.0e-3f, at);
            REQUIRE(onset >= at);
            REQUIRE(onset < at + 48);
        }
    }
}

TEST_CASE("Machine output does not depend on the host block size", "[core][machine]") {
    seq::Pattern p;
    for (int s = 0; s < 16; s += 4) p.at(0, BD, s).setLevel(seq::StepLevel::Normal);
    for (int s = 4; s < 16; s += 8) p.at(0, SD, s).setLevel(seq::StepLevel::Accent);
    for (int s = 0; s < 16; ++s) p.at(0, CH, s).setLevel(seq::StepLevel::Normal);
    p.at(0, CH, 6).setSubSteps(3);
    p.at(0, SD, 12).setFlam(true);
    p.at(0, OH, 14).setLevel(seq::StepLevel::Normal);
    p.at(0, CP, 12).setLevel(seq::StepLevel::Normal);
    p.at(0, seq::kAccentTrack, 0).setLevel(seq::StepLevel::Normal);
    p.shuffle = 40;

    const int total = 32 * kSixteenth;
    std::vector<float> reference;
    for (int block : {512, 64, 1, 2048, 777}) {
        Machine m;
        m.prepare(kFs, kMaxBlock);
        post(m, 0, p);
        Runner r{m};
        Buses buses(total, 1);
        r.run(buses, total, block);
        REQUIRE(allFinite(buses.left()));
        if (reference.empty()) {
            reference = buses.left();
            REQUIRE(peak(reference) > 0.05f);
        } else {
            float worst = 0.0f;
            for (std::size_t i = 0; i < reference.size(); ++i)
                worst = std::max(worst, std::abs(reference[i] - buses.left()[i]));
            INFO("block " << block);
            REQUIRE(worst < 1.0e-5f);
        }
    }
}

TEST_CASE("Mute and solo gate triggers", "[core][machine]") {
    Machine m;
    m.prepare(kFs, kMaxBlock);
    Runner r{m};
    r.playing = false;
    r.midi = {noteOn(0, kVoiceNote[BD]), noteOn(0, kVoiceNote[SD]), noteOn(0, kVoiceNote[CL])};

    SECTION("mute") {
        r.params.mute[SD] = true;
        Buses buses(1024, 1);
        r.run(buses, 1024, 512);
        REQUIRE(hits(m, BD) == 1U);
        REQUIRE(hits(m, SD) == 0U);
        REQUIRE(hits(m, CL) == 1U);
    }
    SECTION("solo") {
        r.params.solo[CL] = true;
        r.params.mute[CL] = true;  // solo wins
        Buses buses(1024, 1);
        r.run(buses, 1024, 512);
        REQUIRE(hits(m, BD) == 0U);
        REQUIRE(hits(m, SD) == 0U);
        REQUIRE(hits(m, CL) == 1U);
    }
}

TEST_CASE("Velocity sets dynamics and the bass channel plays the bass drum chromatically", "[core][machine]") {
    const auto peakFor = [](int velocity) {
        Machine m;
        m.prepare(kFs, kMaxBlock);
        Runner r{m};
        r.playing = false;
        r.midi = {noteOn(0, kVoiceNote[BD], velocity)};
        Buses buses(9600, 1);
        r.run(buses, 9600, 512);
        return peak(buses.left());
    };
    const float soft = peakFor(30), normal = peakFor(96), accent = peakFor(127);
    REQUIRE(soft > 0.0f);
    REQUIRE(soft < normal);
    REQUIRE(normal < accent);

    // Chromatic zone: a note an octave above the root rings an octave higher,
    // so it crosses zero about twice as often.
    const auto crossings = [](int note) {
        Machine m;
        m.prepare(kFs, kMaxBlock);
        Runner r{m};
        r.playing = false;
        r.params.midi.bassChannel = 2;
        r.midi = {noteOn(0, note, 96, 2)};
        Buses buses(24000, 1);
        r.run(buses, 24000, 512);
        int count = 0;
        const auto& x = buses.left();
        for (std::size_t i = 4800; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++count;
        return count;
    };
    const int root = crossings(kBassRootNote), octave = crossings(kBassRootNote + 12);
    REQUIRE(root > 10);
    REQUIRE(octave > root * 17 / 10);
    REQUIRE(octave < root * 23 / 10);
}

TEST_CASE("A posted bank is in place for the next block and outranks stale edits", "[core][machine]") {
    Machine m;
    m.prepare(kFs, kMaxBlock);

    // A stale single edit (generation 0) queued before the bank (generation 1) arrives.
    post(m, 0, clavesOn({0, 1, 2, 3, 4, 5, 6, 7}), 0);

    auto handoff = std::make_unique<BankHandoff>();
    handoff->bank.patterns[0] = clavesOn({0, 8});
    handoff->generation = 1;
    REQUIRE(m.postBank(handoff.release()) == nullptr);

    // A newer handoff supersedes an unconsumed one; the old one comes back to be freed.
    auto second = std::make_unique<BankHandoff>();
    second->bank.patterns[0] = clavesOn({0, 8});
    second->generation = 2;
    std::unique_ptr<BankHandoff> superseded(m.postBank(second.release()));
    REQUIRE(superseded != nullptr);
    REQUIRE(superseded->generation == 1U);

    Runner r{m};
    Buses buses(16 * kSixteenth, 1);
    r.run(buses, 16 * kSixteenth, 512);
    REQUIRE(hits(m, CL) == 2U);  // the bank's pattern, not the stale edit

    std::unique_ptr<BankHandoff> retired(m.takeRetiredBank());
    REQUIRE(retired != nullptr);
    REQUIRE(retired->generation == 2U);
    REQUIRE(m.takeRetiredBank() == nullptr);

    // An edit made against the current generation is applied.
    post(m, 0, clavesOn({0, 4, 8, 12}), 2);
    Buses more(16 * kSixteenth, 1);
    r.startPpq = 4.0;
    r.run(more, 16 * kSixteenth, 512);
    REQUIRE(hits(m, CL) == 6U);
}

TEST_CASE("Sequencer hits go out as MIDI notes with matching note-offs", "[core][machine]") {
    Machine m;
    m.prepare(kFs, kMaxBlock);
    seq::Pattern p = clavesOn({0, 4}, seq::StepLevel::Accent);
    p.at(0, BD, 0).setLevel(seq::StepLevel::Normal);
    post(m, 0, p);
    Runner r{m};
    r.params.midi.midiOut = true;
    Buses buses(16 * kSixteenth, 1);
    r.run(buses, 16 * kSixteenth, 512);

    int ons = 0, offs = 0;
    for (const auto& msg : r.midiOut) {
        REQUIRE((msg.status & 0x0F) == 9);
        if ((msg.status & 0xF0) == 0x90) {
            ++ons;
            if (msg.data1 == kVoiceNote[CL]) REQUIRE(msg.data2 == kVelocityAccent);
            if (msg.data1 == kVoiceNote[BD]) REQUIRE(msg.data2 == kVelocityNormal);
        } else if ((msg.status & 0xF0) == 0x80) {
            ++offs;
        }
    }
    REQUIRE(ons == 3);
    REQUIRE(offs == 3);
    REQUIRE(r.midiOut.front().offset == 0);

    // With MIDI out off nothing is sent.
    Machine quiet;
    quiet.prepare(kFs, kMaxBlock);
    post(quiet, 0, p);
    Runner q{quiet};
    Buses b2(16 * kSixteenth, 1);
    q.run(b2, 16 * kSixteenth, 512);
    REQUIRE(q.midiOut.empty());
}

TEST_CASE("Host Note mode plays a pattern while its note is held", "[core][machine]") {
    Machine m;
    m.prepare(kFs, kMaxBlock);
    post(m, 3, clavesOn({0, 2, 4, 6, 8, 10, 12, 14}));
    Runner r{m};
    r.playing = false;
    r.params.seq.mode = seq::Mode::HostNote;
    // Held for five 8th notes starting at sample 1000, then released.
    r.midi = {noteOn(1000, kPatternNoteBase + 3), noteOff(1000 + 10 * kSixteenth - 100, kPatternNoteBase + 3)};
    Buses buses(24 * kSixteenth, 1);
    r.run(buses, 24 * kSixteenth, 512);

    REQUIRE(hits(m, CL) == 5U);
    REQUIRE(peak(buses.left(), 0, 1000) == 0.0f);
    const int onset = firstAbove(buses.left(), 1.0e-3f);
    REQUIRE(onset >= 1000);
    REQUIRE(onset < 1048);
    REQUIRE_FALSE(m.playState().playing.load());
}

TEST_CASE("Recording a played hit writes the step and tells the UI", "[core][machine]") {
    Machine m;
    m.prepare(kFs, kMaxBlock);
    Runner r{m};
    r.params.seq.record = true;
    Buses warm(kSixteenth, 1);
    r.run(warm, kSixteenth, 512);  // get the sequencer rolling

    // Play a snare just after step 4 (the second beat).
    r.midi = {noteOn(4 * kSixteenth + 300, kVoiceNote[SD], 127)};
    Buses buses(8 * kSixteenth, 1);
    r.run(buses, 7 * kSixteenth, 512, kSixteenth);

    Notice n;
    bool recorded = false;
    while (m.poll(n))
        if (n.type == Notice::Type::StepRecorded) {
            recorded = true;
            REQUIRE(n.pattern == 0);
            REQUIRE(n.track == SD);
            REQUIRE(n.step == 4);
            REQUIRE(n.cell.level() == seq::StepLevel::Accent);
        }
    REQUIRE(recorded);
    REQUIRE(m.sequencer().bank().patterns[0].at(0, SD, 4).level() == seq::StepLevel::Accent);
}

TEST_CASE("Audition, pattern selection and panic commands reach the audio side", "[core][machine]") {
    Machine m;
    m.prepare(kFs, kMaxBlock);
    Runner r{m};
    r.playing = false;

    Command audition;
    audition.type = Command::Type::Audition;
    audition.index = CB;
    audition.value = Hit::kNormalVolts;
    REQUIRE(m.post(audition));
    r.params.mute[CB] = true;  // auditions ignore mute
    Buses buses(2048, 1);
    r.run(buses, 2048, 512);
    REQUIRE(hits(m, CB) == 1U);
    REQUIRE(peak(buses.left()) > 0.001f);

    Command select;
    select.type = Command::Type::SelectPattern;
    select.index = 7;
    REQUIRE(m.post(select));
    Command panic;
    panic.type = Command::Type::Panic;
    REQUIRE(m.post(panic));
    Buses after(1024, 1);
    r.run(after, 1024, 512);
    REQUIRE(m.playState().selectedPattern.load() == 7);
    REQUIRE(peak(after.left()) == 0.0f);  // panic silenced the ringing cowbell
    REQUIRE_FALSE(m.engine().anyVoiceActive());
}

TEST_CASE("Blocks larger than the prepared size and empty blocks are handled", "[core][machine]") {
    Machine m;
    m.prepare(kFs, 64);
    post(m, 0, clavesOn({0, 4, 8, 12}));
    Runner r{m};
    Buses buses(16 * kSixteenth, 1);
    r.run(buses, 16 * kSixteenth, 5000);  // far larger than the prepared 64
    REQUIRE(hits(m, CL) == 4U);
    REQUIRE(allFinite(buses.left()));

    Machine::ProcessArgs args;
    args.out = buses.view();
    args.numSamples = 0;
    m.process(args, r.params);  // must not touch anything or crash
    SUCCEED();
}

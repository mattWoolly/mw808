// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Pattern <-> Standard MIDI File (architecture.md §5.7): byte structure,
// round trips, agreement with the sequencer's timing, hostile input.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <vector>

#include "SeqTestUtil.h"
#include "mw808/seq/MidiFile.h"

using namespace seqtest;

namespace {

using Bytes = std::vector<std::uint8_t>;

std::array<std::uint8_t, kNumVoices> testMap() {
    // Distinct keys, close to the usual drum map.
    return {36, 38, 41, 45, 48, 64, 63, 62, 37, 75, 39, 70, 56, 49, 46, 42};
}

struct ParsedNote {
    std::int64_t tick;
    int note;
    int velocity;
};

struct Parsed {
    int format = -1, tracks = -1, division = -1;
    std::int64_t endTick = -1;
    std::vector<ParsedNote> ons;
    int offs = 0;
    int tempoUs = -1;
    bool hasEmbedded = false;
};

// Independent strict parser for the files we write: no running status, every
// length checked, end-of-track last.
Parsed parseStrict(const Bytes& b) {
    Parsed p;
    std::size_t i = 0;
    auto need = [&](std::size_t n) { REQUIRE(i + n <= b.size()); };
    auto u8 = [&]() {
        need(1);
        return static_cast<int>(b[i++]);
    };
    auto u16 = [&]() {
        const int hi = u8();
        return (hi << 8) | u8();
    };
    auto u32 = [&]() {
        const std::int64_t hi = u16();
        return (hi << 16) | u16();
    };
    auto varLen = [&]() {
        std::int64_t v = 0;
        for (int k = 0; k < 4; ++k) {
            const int c = u8();
            v = (v << 7) | (c & 0x7F);
            if ((c & 0x80) == 0) return v;
        }
        FAIL("variable-length quantity longer than 4 bytes");
        return v;
    };
    REQUIRE(u8() == 'M');
    REQUIRE(u8() == 'T');
    REQUIRE(u8() == 'h');
    REQUIRE(u8() == 'd');
    REQUIRE(u32() == 6);
    p.format = u16();
    p.tracks = u16();
    p.division = u16();
    REQUIRE(u8() == 'M');
    REQUIRE(u8() == 'T');
    REQUIRE(u8() == 'r');
    REQUIRE(u8() == 'k');
    const std::int64_t len = u32();
    REQUIRE(static_cast<std::size_t>(len) == b.size() - i);  // the chunk length is exact
    std::int64_t tick = 0;
    std::vector<int> held(128, 0);
    for (;;) {
        tick += varLen();
        const int status = u8();
        REQUIRE((status & 0x80) != 0);  // we never write running status
        if (status == 0xFF) {
            const int type = u8();
            const std::int64_t l = varLen();
            need(static_cast<std::size_t>(l));
            if (type == 0x51) {
                REQUIRE(l == 3);
                p.tempoUs = (b[i] << 16) | (b[i + 1] << 8) | b[i + 2];
            }
            if (type == 0x7F) p.hasEmbedded = true;
            i += static_cast<std::size_t>(l);
            if (type == 0x2F) {
                REQUIRE(l == 0);
                p.endTick = tick;
                break;
            }
            continue;
        }
        REQUIRE((status == 0x99 || status == 0x89));  // channel 10 only
        const int note = u8(), vel = u8();
        REQUIRE(note < 128);
        REQUIRE(vel < 128);
        if (status == 0x99) {
            REQUIRE(vel > 0);
            REQUIRE(held[static_cast<std::size_t>(note)] == 0);  // no overlapping notes on one key
            held[static_cast<std::size_t>(note)] = 1;
            p.ons.push_back({tick, note, vel});
        } else {
            REQUIRE(held[static_cast<std::size_t>(note)] == 1);
            held[static_cast<std::size_t>(note)] = 0;
            ++p.offs;
        }
    }
    REQUIRE(i == b.size());  // end-of-track is the last event
    for (int h : held) REQUIRE(h == 0);
    return p;
}

Pattern noteFriendlyPattern(std::mt19937& rng) {
    // What notes alone can carry exactly: no probability, no accent row,
    // no shuffle, no per-track lengths.
    Pattern p = randomPattern(rng, 0.25f, false);
    for (int s = 0; s < kMaxSteps; ++s) p.at(0, kAccentTrack, s) = Step{};
    p.shuffle = 0;
    p.trackLength.fill(0);
    // Cells beyond the length do not reach the file.
    for (int t = 0; t < kNumTracks; ++t)
        for (int s = p.length; s < kMaxSteps; ++s) p.at(0, t, s) = Step{};
    // Off cells carry no modifier bits after a read.
    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s)
            if (!p.at(0, t, s).on()) p.at(0, t, s) = Step{};
    return p;
}

}  // namespace

TEST_CASE("written files are structurally valid format-0 SMF", "[seq][midi]") {
    std::mt19937 rng(42);
    for (int c = 0; c < 200; ++c) {
        const Pattern p = randomPattern(rng, 0.3f);
        MidiFileOptions o;
        o.bpm = 60.0 + c;
        o.flamMs = static_cast<float>(c % 50);
        o.oneStepNotes = (c & 1) != 0;
        o.embedPattern = (c % 3) != 0;
        o.includeTempo = (c % 5) != 0;
        o.accentNote = (c % 7) == 0 ? 35 : -1;
        const Bytes b = writeMidiFile(p, c & 1, testMap(), o);
        INFO("case " << c);
        const Parsed f = parseStrict(b);
        CHECK(f.format == 0);
        CHECK(f.tracks == 1);
        CHECK(f.division == 480);
        CHECK(f.endTick == p.length * std::lround(stepLengthQn(p.scale) * 480));
        CHECK(static_cast<int>(f.ons.size()) == f.offs);
        CHECK(f.hasEmbedded == o.embedPattern);
        if (o.includeTempo) CHECK(f.tempoUs == static_cast<int>(std::lround(60.0e6 / o.bpm)));
        for (const ParsedNote& n : f.ons) {
            CHECK(n.tick >= 0);
            CHECK(n.tick < f.endTick);
        }
    }
}

TEST_CASE("note positions, lengths and velocities", "[seq][midi]") {
    Pattern p;
    p.length = 16;
    p.shuffle = 100;
    p.at(0, BD, 0).setLevel(StepLevel::Normal);
    p.at(0, SD, 1).setLevel(StepLevel::Weak);             // odd step: 40 ticks late
    p.at(0, CH, 2).setLevel(StepLevel::Normal);
    p.at(0, CH, 2).setSubSteps(3);                        // 0, 40, 80 ticks into the step
    p.at(0, OH, 4).setLevel(StepLevel::Accent);
    p.at(0, OH, 4).setFlam(true);                         // grace, then main 24 ms later
    p.at(0, kAccentTrack, 0).setLevel(StepLevel::Normal); // BD becomes an accent
    MidiFileOptions o;
    o.bpm = 125.0;  // 24 ms = 0.05 qn = 24 ticks
    o.flamMs = 24.0f;
    o.accentNote = 20;
    const auto m = testMap();
    const Parsed f = parseStrict(writeMidiFile(p, 0, m, o));
    std::vector<ParsedNote> want = {{0, 20, 127},       {0, m[BD], 127},   {160, m[SD], 48}, {240, m[CH], 96},
                                    {280, m[CH], 96},   {320, m[CH], 96},  {480, m[OH], 76}, {504, m[OH], 127}};
    REQUIRE(f.ons.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        INFO("note " << i);
        CHECK(f.ons[i].tick == want[i].tick);
        CHECK(f.ons[i].note == want[i].note);
        CHECK(f.ons[i].velocity == want[i].velocity);
    }
    CHECK(f.tempoUs == 480000);
}

TEST_CASE("round trip through the embedded pattern is exact, probability included", "[seq][midi]") {
    std::mt19937 rng(7);
    for (int c = 0; c < 300; ++c) {
        const Pattern p = randomPattern(rng, 0.3f, true);
        const int var = c & 1;
        const Bytes b = writeMidiFile(p, var, testMap(), MidiFileOptions{});
        Pattern q;
        REQUIRE(readMidiFile(b, testMap(), q, var));
        CHECK(q.length == p.length);
        CHECK(q.scale == p.scale);
        CHECK(q.shuffle == p.shuffle);
        CHECK(q.trackLength == p.trackLength);
        CHECK(q.steps[static_cast<std::size_t>(var)] == p.steps[static_cast<std::size_t>(var)]);
        CHECK(q.steps[static_cast<std::size_t>(1 - var)] == Pattern{}.steps[0]);  // other variation untouched
    }
}

TEST_CASE("round trip through the notes alone (no probability, accent row or shuffle)", "[seq][midi]") {
    std::mt19937 rng(11);
    for (int c = 0; c < 300; ++c) {
        const Pattern p = noteFriendlyPattern(rng);
        MidiFileOptions o;
        o.embedPattern = false;
        o.flamMs = 10.0f;
        o.oneStepNotes = (c & 1) != 0;
        const Bytes b = writeMidiFile(p, 0, testMap(), o);
        Pattern q;
        q.scale = p.scale;  // the import grid is the target pattern's scale
        REQUIRE(readMidiFile(b, testMap(), q, 0));
        INFO("case " << c << " length " << int(p.length) << " scale " << int(p.scale));
        CHECK(q.length == p.length);
        for (int t = 0; t < kNumVoices; ++t)
            for (int s = 0; s < kMaxSteps; ++s) {
                INFO("track " << t << " step " << s);
                REQUIRE(q.at(0, t, s) == p.at(0, t, s));
            }
    }
}

TEST_CASE("file note times agree with the sequencer's hit times", "[seq][midi]") {
    // 125 BPM at 48 kHz: 23040 samples per quarter = 48 samples per tick.
    std::mt19937 rng(5);
    for (int c = 0; c < 60; ++c) {
        Pattern p = randomPattern(rng, 0.2f, false);
        p.trackLength.fill(0);  // every measure identical
        MidiFileOptions o;
        o.embedPattern = false;
        o.bpm = 125.0;
        o.flamMs = 5.0f + static_cast<float>(c % 30);
        o.swingScale = (c % 3 == 0) ? 0.5f : 1.0f;
        const auto m = testMap();
        const Parsed f = parseStrict(writeMidiFile(p, 0, m, o));

        auto s = makeSeq(48000.0);
        s->setPattern(0, p);
        Settings st = internalSettings(125.0);
        st.flamMs = o.flamMs;
        st.swingScale = o.swingScale;
        const std::int64_t measureSamples = f.endTick * 48;
        const auto ev = runInternal(*s, st, fixedBlocks(measureSamples * 2, 1024));
        // Measure 1 (whose window also holds measure 0's spill) equals the
        // file, which wraps late hits to the top.
        std::vector<std::pair<int, std::int64_t>> fromSeq, fromFile;
        for (const Ev& e : ev)
            if (e.sample >= measureSamples) fromSeq.push_back({m[static_cast<std::size_t>(e.voice)], e.sample - measureSamples});
        // A file cannot hold two notes of one key on one tick (e.g. a flam's
        // main hit landing on the next sub-hit): the writer keeps one.
        std::sort(fromSeq.begin(), fromSeq.end());
        fromSeq.erase(std::unique(fromSeq.begin(), fromSeq.end()), fromSeq.end());
        for (auto& x : fromSeq) x.second = std::llround(static_cast<double>(x.second) / 48.0);
        for (const ParsedNote& n : f.ons) fromFile.push_back({n.note, n.tick});
        std::sort(fromSeq.begin(), fromSeq.end());
        std::sort(fromFile.begin(), fromFile.end());
        INFO("case " << c);
        REQUIRE(fromSeq.size() == fromFile.size());
        for (std::size_t i = 0; i < fromSeq.size(); ++i) {
            CHECK(fromSeq[i].first == fromFile[i].first);
            CHECK(std::llabs(fromSeq[i].second - fromFile[i].second) <= 1);
        }
    }
}

TEST_CASE("reading foreign files: format 1, other resolutions, running status", "[seq][midi]") {
    const auto m = testMap();
    // Format 1, 2 tracks, 96 PPQ. Track 1: tempo + text. Track 2: BD on beats,
    // SD on 2 and 4 with running status and note-on velocity 0 as note-off,
    // a sysex in between, and a weak hi-hat on the 'e' of beat 1.
    Bytes b = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0, 96};
    const Bytes t1 = {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xFF, 0x01, 0x03, 'a', 'b', 'c',
                      0x83, 0x00, 0xFF, 0x2F, 0x00};  // ends at 384 ticks = one 4/4 bar
    const Bytes t2 = {0x00, 0x99, m[BD], 100,           // BD at 0
                      0x18, 0x99, m[CH], 40,            // CH at 24 (step 1), weak
                      0x18, 0x99, m[SD], 120,           // SD at 48 (step 2), accent velocity
                      0x00, m[SD], 0,                   // running status: SD off
                      0x30, 0xF0, 0x03, 0x7E, 0x7F, 0xF7,  // sysex at 96
                      0x00, 0x99, m[BD], 90,            // BD at 96 (step 4)
                      0x81, 0x40, 0x89, m[BD], 0,       // +192: off
                      0x00, 0xFF, 0x2F, 0x00};
    auto chunk = [&](const Bytes& t) {
        b.insert(b.end(), {'M', 'T', 'r', 'k'});
        const auto n = static_cast<std::uint32_t>(t.size());
        b.insert(b.end(), {static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16),
                           static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n)});
        b.insert(b.end(), t.begin(), t.end());
    };
    chunk(t1);
    chunk(t2);
    Pattern q;
    REQUIRE(readMidiFile(b, m, q, 1));
    CHECK(q.length == 16);
    CHECK(q.at(1, BD, 0).level() == StepLevel::Normal);
    CHECK(q.at(1, CH, 1).level() == StepLevel::Weak);
    CHECK(q.at(1, SD, 2).level() == StepLevel::Accent);
    CHECK(q.at(1, BD, 4).level() == StepLevel::Normal);
    int on = 0;
    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s) on += q.at(1, t, s).on() ? 1 : 0;
    CHECK(on == 4);
}

TEST_CASE("hostile input never crashes and never half-writes the pattern", "[seq][midi]") {
    std::mt19937 rng(3);
    const Pattern p = randomPattern(rng, 0.4f);
    const Bytes good = writeMidiFile(p, 0, testMap(), MidiFileOptions{});
    Pattern sentinel = everyStep(CY);

    SECTION("every truncation") {
        for (std::size_t n = 0; n < good.size(); ++n) {
            Pattern q = sentinel;
            const Bytes cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n));
            const bool ok = readMidiFile(cut, testMap(), q);
            if (!ok) REQUIRE(q == sentinel);
        }
    }
    SECTION("random corruption") {
        std::uniform_int_distribution<int> byte(0, 255);
        for (int i = 0; i < 3000; ++i) {
            Bytes bad = good;
            const int flips = 1 + i % 8;
            for (int k = 0; k < flips; ++k) bad[rng() % bad.size()] = static_cast<std::uint8_t>(byte(rng));
            Pattern q = sentinel;
            const bool ok = readMidiFile(bad, testMap(), q);
            if (!ok) REQUIRE(q == sentinel);
            REQUIRE(q.length >= 1);
            REQUIRE(q.length <= kMaxSteps);
        }
        for (int i = 0; i < 500; ++i) {
            Bytes junk(static_cast<std::size_t>(rng() % 300));
            for (auto& x : junk) x = static_cast<std::uint8_t>(byte(rng));
            if (junk.size() >= 4) {
                junk[0] = 'M';
                junk[1] = 'T';
                junk[2] = 'h';
                junk[3] = 'd';
            }
            Pattern q = sentinel;
            (void)readMidiFile(junk, testMap(), q);
        }
    }
    SECTION("specific rejections") {
        Pattern q = sentinel;
        Bytes smpte = good;
        smpte[12] = 0xE7;  // negative SMPTE format in the division word
        CHECK_FALSE(readMidiFile(smpte, testMap(), q));
        Bytes magic = good;
        magic[0] = 'X';
        CHECK_FALSE(readMidiFile(magic, testMap(), q));
        CHECK_FALSE(readMidiFile(Bytes{}, testMap(), q));
        Bytes noTracks(good.begin(), good.begin() + 14);
        CHECK_FALSE(readMidiFile(noTracks, testMap(), q));
        CHECK(q == sentinel);
    }
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Factory pattern banks: the step-string notation, its compile-time checker,
// and the pattern data itself. Factory.cpp holds the kits and presets.
//
// ---------------------------------------------------------------------------
// Step notation
// ---------------------------------------------------------------------------
// A variation is one string of tracks separated by ';'. Each track is a
// two-letter code (the voice codes of kVoiceInfo, plus "AC" for the global
// accent row) followed by its steps:
//
//     "BD X...|..x.|..x.|....;"
//
//   .   off                       '|' and ' ' are ignored (they only group
//   o   weak hit (ghost note)         beats for the eye)
//   x   normal hit
//   X   accented hit (this voice only; the AC row accents every voice)
//
// Modifiers follow the hit they change:
//
//   ~       flam (grace hit, then the main hit one flam time later)
//   2 3 4   sub-steps: that many evenly spaced hits inside the step (rolls)
//   ?       probability: each '?' takes 25 % off (x? = 75 %, x?? = 50 %)
//
// On the AC row any hit character means "accent on" and modifiers are not
// allowed. A track has exactly `length` steps, unless the code is followed by
// '@' ("CL@ x.."): then the number of steps written becomes that track's own
// loop length (polymeter). Tracks not written are empty.
//
// Variation B is written as the tracks that CHANGE from A: B starts as a
// copy of A and each track listed replaces A's track completely (same loop
// length). An empty B string makes B a plain copy of A.
//
// Every pattern is parsed and checked at compile time (static_assert below):
// step counts, characters, duplicate tracks, open and closed hat on the same
// step (the closed hat chokes the open one, so it is almost always a typo),
// B identical to A, a name without data or data without a name.

#include "mw808/Factory.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace mw808::factory::detail {

// Declared again in Factory.cpp (no private header in this library).
int bankCount() noexcept;
const BankInfo& bankInfoAt(int index) noexcept;
void buildBankAt(int index, seq::PatternBank& out) noexcept;
std::string_view patternNameAt(int bankIndex, int patternIndex) noexcept;

namespace {

using seq::Scale;
constexpr Scale T8 = Scale::EighthTriplet;      // 3 steps per beat (12 per 4/4 bar)
constexpr Scale T16 = Scale::SixteenthTriplet;  // 6 steps per beat (24 per bar)
constexpr Scale S16 = Scale::Sixteenth;         // 4 steps per beat (16 per bar)
constexpr Scale S32 = Scale::ThirtySecond;      // 8 steps per beat (32 per bar)
constexpr Scale S8 = Scale::Eighth;             // 2 steps per beat (16 = two bars)

struct PatternDef {
    std::string_view name;
    int length = 16;
    Scale scale = S16;
    int shuffle = 0;
    std::string_view a;
    std::string_view b;
    bool hatOverlap = false;  // OH and CH deliberately share a step (none in the factory set)
};

enum Error : int {
    kOk = 0,
    kUnknownTrack,
    kDuplicateTrack,
    kBadChar,
    kStrayModifier,
    kTooManySteps,
    kWrongStepCount,
    kAccentModifier,
    kAccentOwnLength,
    kBadHeader,
    kHatClash,
    kHatOverlapUnused,
    kSameAsA,
    kNameWithoutData,
    kDataWithoutName,
    kEmptyTrack
};

constexpr int trackIndex(std::string_view code) noexcept {
    if (code == "AC") return seq::kAccentTrack;
    for (int v = 0; v < kNumVoices; ++v)
        if (kVoiceInfo[static_cast<std::size_t>(v)].code == code) return v;
    return -1;
}

constexpr std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

// Parse one variation's text into `p`. For B (`isB`), `p` already holds A in
// both variations; listed tracks replace B's copy.
constexpr Error parseVariation(std::string_view text, bool isB, seq::Pattern& p) noexcept {
    const int variation = isB ? 1 : 0;
    std::array<bool, seq::kNumTracks> seen{};
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find(';', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view tok = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (tok.empty()) continue;
        if (tok.size() < 2) return kUnknownTrack;
        const int track = trackIndex(tok.substr(0, 2));
        if (track < 0) return kUnknownTrack;
        if (seen[static_cast<std::size_t>(track)]) return kDuplicateTrack;
        seen[static_cast<std::size_t>(track)] = true;
        const bool accentRow = track == seq::kAccentTrack;

        std::size_t i = 2;
        bool ownLength = false;
        if (i < tok.size() && tok[i] == '@') {
            if (accentRow) return kAccentOwnLength;
            ownLength = true;
            ++i;
        }

        std::array<seq::Step, seq::kMaxSteps> cells{};
        int count = 0;
        bool lastOn = false;
        bool anyOn = false;
        for (; i < tok.size(); ++i) {
            const char c = tok[i];
            if (c == ' ' || c == '|') continue;
            if (c == '.' || c == 'o' || c == 'x' || c == 'X') {
                if (count >= seq::kMaxSteps) return kTooManySteps;
                seq::Step& s = cells[static_cast<std::size_t>(count++)];
                if (c == '.') {
                    lastOn = false;
                    continue;
                }
                if (accentRow && c == 'o') return kBadChar;
                s.setLevel(accentRow ? seq::StepLevel::Normal
                                     : (c == 'o' ? seq::StepLevel::Weak
                                                 : (c == 'x' ? seq::StepLevel::Normal : seq::StepLevel::Accent)));
                lastOn = true;
                anyOn = true;
                continue;
            }
            if (c == '~' || c == '2' || c == '3' || c == '4' || c == '?') {
                if (accentRow) return kAccentModifier;
                if (!lastOn) return kStrayModifier;
                seq::Step& s = cells[static_cast<std::size_t>(count - 1)];
                if (c == '~') {
                    if (s.flam()) return kStrayModifier;
                    s.setFlam(true);
                } else if (c == '?') {
                    if (s.probabilityCode() >= 6) return kStrayModifier;
                    s.setProbabilityCode(s.probabilityCode() + 2);
                } else {
                    if (s.subSteps() != 1) return kStrayModifier;
                    s.setSubSteps(c - '0');
                }
                continue;
            }
            return kBadChar;
        }

        int expected = p.length;
        if (isB) {
            expected = p.lengthOf(track);
        } else if (ownLength) {
            if (count < 1) return kWrongStepCount;
            p.trackLength[static_cast<std::size_t>(track)] = static_cast<std::uint8_t>(count);
            expected = count;
        }
        if (count != expected) return kWrongStepCount;
        // A track written in A must hold something; in B an all-off track
        // is a legitimate "drop this part".
        if (!isB && !anyOn) return kEmptyTrack;

        auto& row = p.steps[static_cast<std::size_t>(variation)][static_cast<std::size_t>(track)];
        row = cells;
    }
    return kOk;
}

constexpr int gcd(int a, int b) noexcept {
    while (b != 0) {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// True if OH and CH ever sound on the same step of a variation, following
// each track's own loop (global step t reads index t mod L).
constexpr bool hatsClash(const seq::Pattern& p, int variation) noexcept {
    const int lo = p.lengthOf(OH), lc = p.lengthOf(CH);
    const int period = lo / gcd(lo, lc) * lc;
    for (int t = 0; t < period; ++t)
        if (p.at(variation, OH, t % lo).on() && p.at(variation, CH, t % lc).on()) return true;
    return false;
}

constexpr Error parsePattern(const PatternDef& d, seq::Pattern& p) noexcept {
    p = seq::Pattern{};
    if (d.a.empty()) return d.name.empty() ? kOk : kNameWithoutData;
    if (d.name.empty()) return kDataWithoutName;
    if (d.length < 1 || d.length > seq::kMaxSteps || d.shuffle < 0 || d.shuffle > 100 ||
        static_cast<int>(d.scale) >= static_cast<int>(Scale::kNumScales))
        return kBadHeader;
    p.length = static_cast<std::uint8_t>(d.length);
    p.scale = d.scale;
    p.shuffle = static_cast<std::uint8_t>(d.shuffle);
    if (const Error e = parseVariation(d.a, false, p); e != kOk) return e;
    p.steps[1] = p.steps[0];
    if (const Error e = parseVariation(d.b, true, p); e != kOk) return e;
    if (!d.b.empty() && p.steps[0] == p.steps[1]) return kSameAsA;
    const bool clash = hatsClash(p, 0) || hatsClash(p, 1);
    if (clash && !d.hatOverlap) return kHatClash;
    if (!clash && d.hatOverlap) return kHatOverlapUnused;
    return kOk;
}

using BankDefs = std::array<PatternDef, seq::kNumPatterns>;

// 0 if every pattern is valid, else (index + 1) * 100 + error code, so a
// failing static_assert names the slot and the reason.
constexpr int checkBank(const BankDefs& defs) noexcept {
    for (std::size_t i = 0; i < defs.size(); ++i) {
        seq::Pattern p{};
        if (const Error e = parsePattern(defs[i], p); e != kOk) return static_cast<int>(i + 1) * 100 + e;
    }
    return 0;
}

// ===========================================================================
// Bank 0: "Factory" — the default memory. Four internal banks, each 12 basic
// rhythms + 4 intro/fill-ins in the genre of that bank, so auto fill always
// plays a fill that suits the groove.
//   A  1-16  Electro, Miami bass, freestyle, funk, breaks
//   B 17-32  Hip-hop, trap, drill, R&B
//   C 33-48  House, techno, garage, footwork
//   D 49-64  Latin, Afro, reggaeton, polymeter and odd grids
// ===========================================================================
constexpr BankDefs kFactory{{
    // ---- A: Electro & Freestyle -------------------------------------------
    {"Electro Cruise", 16, S16, 0,
     "BD X...|..x.|..x.|....;"
     "SD ....|X...|....|X...;"
     "CP ....|x...|....|x...;"
     "CH x.x.|x.x.|x.x.|x.xx;"
     "AC x...|....|x...|....",
     "BD X...|..x.|..x.|.x.x;"
     "CH x.x.|x.x.|x.x.|x...;"
     "OH ....|....|....|..x.;"
     "CB ..x.|....|..x.|...."},
    {"Electro Bounce", 16, S16, 0,
     "BD X..x|..x.|..x.|....;"
     "SD ....|X...|....|X...;"
     "CP ....|X...|....|X...;"
     "CH xoxo|xoxo|xoxo|xoxo;"
     "CB ..x.|....|..x.|..x.;"
     "AC ....|x...|....|x...",
     "BD X..x|..x.|..x.|.x.x;"
     "CH xoxo|xoxo|xoxo|xo..;"
     "OH ....|....|....|..x.;"
     "CB ..x.|..x.|..x.|...."},
    {"Robot Toms", 16, S16, 0,
     "BD X...|...x|..x.|....;"
     "SD ....|X...|....|X...;"
     "CH x.x.|x.x.|x.x.|x.x.;"
     "HT ..x.|....|....|..x.;"
     "MT ....|.x..|....|....;"
     "LT ....|....|.x..|...x;"
     "AC x...|....|x...|....",
     "BD X...|...x|....|x...;"
     "HT ..x.|....|..x.|....;"
     "MT ....|.x..|...x|....;"
     "LT .x..|....|....|.xx."},
    {"Miami Boom", 16, S16, 0,
     "BD X..x|..x.|..x.|.x..;"
     "SD ....|x...|....|x...;"
     "CP ....|X...|....|X...;"
     "CH xoxo|xoxo|xoxo|xoxo;"
     "AC x...|....|x...|....",
     "BD X..x|..x.|x.x.|.x.x;"
     "CP ....|X...|....|X..o;"
     "CH xoxo|xoxo|xoxo|xox3o"},
    {"Miami Ride", 16, S16, 0,
     "BD X..x|....|X..x|..x.;"
     "SD ....|x...|....|x...;"
     "CP ....|X...|....|X...;"
     "CH xox.|xox.|xox.|xox.;"
     "OH ...x|...x|...x|...x",
     "BD X..x|..x.|X..x|.xx.;"
     "CH xox.|xox.|xox.|xoxx;"
     "OH ...x|...x|...x|....;"
     "CB ....|..x.|....|..x."},
    {"Freestyle Heat", 16, S16, 0,
     "BD X..x|..x.|x..x|..x.;"
     "SD ....|X...|....|X...;"
     "CP ....|X...|....|X...;"
     "CH xoxx|xoxx|xoxx|xoxx;"
     "HC ...x|....|...x|....;"
     "LC ....|..x.|....|..x.;"
     "AC x...|....|....|....",
     "BD X..x|..x.|x..x|..xx;"
     "CH xoxx|xoxx|xoxx|x...;"
     "HT ....|....|....|.xx.;"
     "MT ....|....|....|...x"},
    {"Freestyle Clave", 16, S16, 0,
     "BD X...|..x.|x...|..x.;"
     "SD ....|X...|....|X...;"
     "CL x..x|..x.|..x.|x...;"
     "CH x.x.|x.x.|x.x.|x.x.;"
     "CB ..x.|..x.|..x.|..x.",
     "BD X...|..xx|x...|..x.;"
     "CP ....|X...|....|X..x;"
     "HC ...x|..x.|...x|..x.;"
     "MC .x..|....|.x..|...."},
    {"Funk Pocket", 16, S16, 20,
     "BD X..x|....|..x.|.x..;"
     "SD ....|X..o|.o..|X..o;"
     "CH xoxo|xoxo|xoxo|xo..;"
     "OH ....|....|....|..x.;"
     "AC ....|x...|....|x...",
     "BD X..x|..x.|..x.|.x.x;"
     "SD ....|X..o|.o..|X~.o."},
    {"Funk Sixteens", 16, S16, 10,
     "BD X.x.|...x|..x.|....;"
     "SD ....|X...|.o.o|X...;"
     "RS ..o.|....|....|..o.;"
     "CH Xoxo|Xoxx|Xoxo|Xoxo",
     "BD X.x.|...x|x.x.|..x.;"
     "CH Xoxo|Xoxx|Xoxo|Xo..;"
     "OH ....|....|....|..x."},
    {"Breaker", 16, S16, 0,
     "BD X.x.|....|..x.|....;"
     "SD ....|X..o|.o..|X...;"
     "CH x.x.|x.x.|x.x.|x.x.;"
     "AC ....|x...|....|x...",
     "BD X.x.|....|..xx|..x.;"
     "SD ....|X..o|.o..|X.o.;"
     "CH x.x.|x.x.|x.x.|x...;"
     "OH ....|....|....|..x."},
    {"Electro Two-Bar", 32, S16, 0,
     "BD X...|..x.|..x.|....|X...|..x.|x.x.|.x..;"
     "SD ....|X...|....|X...|....|X...|....|X...;"
     "CP ....|x...|....|x...|....|x...|....|x...;"
     "CH x.x.|x.x.|x.x.|x.x.|x.x.|x.x.|x.x.|x.xx;"
     "CB ....|....|....|....|..x.|....|..x.|....;"
     "AC x...|....|x...|....|x...|....|x...|....",
     "BD X...|..x.|..x.|...x|X...|..x.|x.x.|....;"
     "CH x.x.|x.x.|x.x.|x.x.|x.x.|x.x.|x.x.|x...;"
     "HT ....|....|....|....|....|....|....|.x..;"
     "MT ....|....|....|....|....|....|....|..x.;"
     "LT ....|....|....|....|....|....|....|...x"},
    {"Synth Pop", 16, S16, 0,
     "BD X...|....|X.x.|....;"
     "SD ....|X...|....|X...;"
     "CP ....|x...|....|x...;"
     "CH x.x.|x.x.|x.x.|x...;"
     "OH ....|....|....|..x.;"
     "MA oxox|oxox|oxox|oxox",
     "BD X...|...x|X.x.|....;"
     "CH x.x.|x...|x.x.|x...;"
     "OH ....|..x.|....|..x.;"
     "CY X...|....|....|...."},
    // fills A
    {"Tom Run", 16, S16, 0,
     "BD X...|..x.|....|....;"
     "SD ....|X...|....|....;"
     "CH x.x.|x.x.|....|....;"
     "HT ....|....|xx..|....;"
     "MT ....|....|..xx|....;"
     "LT ....|....|....|xxX.;"
     "AC ....|....|x...|x...",
     "HT ....|....|x3.x3.|....;"
     "MT ....|....|....|x3...;"
     "LT ....|....|....|..x3X"},
    {"Snare Roll", 16, S16, 0,
     "BD X...|....|....|....;"
     "SD ....|X...|o.o.|xxXX;"
     "CH x.x.|x.x.|....|....;"
     "AC ....|....|....|..xx",
     "BD X...|..x.|....|....;"
     "SD ....|X...|oooo|x2x2X2X4"},
    {"Clap Build", 16, S16, 0,
     "BD X...|....|X...|....;"
     "CP ....|X...|x.x.|xxX~X~;"
     "CH x.x.|x.x.|....|....;"
     "AC ....|....|....|x...",
     "CP ....|X...|x2.x2.|x3x3X3X4"},
    {"Cowbell Break", 16, S16, 0,
     "BD X...|....|....|....;"
     "CP ....|X...|....|X...;"
     "CB ..x.|x.x.|xx.x|x.X.;"
     "HT ....|...x|....|....;"
     "LT ....|....|....|...x",
     "BD X...|....|..x.|....;"
     "CB x.x.|x.xx|.xx.|X.X.;"
     "HC ....|....|....|..x."},

    // ---- B: Hip-Hop, Trap & R&B -------------------------------------------
    {"Boom Bap", 16, S16, 25,
     "BD X...|...x|..x.|....;"
     "SD ....|X...|....|X...;"
     "CH x.o.|x.o.|x.o.|x.o.;"
     "AC ....|x...|....|x...",
     "BD X...|...x|..x.|.x..;"
     "SD ....|X...|...o|X...;"
     "CH x.o.|x.o.|x.o.|x...;"
     "OH ....|....|....|..x."},
    {"Dusty Swing", 16, S16, 55,
     "BD X...|...x|x...|..x.;"
     "SD ....|X...|....|X...;"
     "RS ..o.|....|.o..|....;"
     "CH x o? x o?|x o? x o?|x o? x o?|x o? x x",
     "BD X...|...x|x.x.|...o;"
     "SD ....|X...|..o.|X..o;"
     "CH x o? x o?|x o? x o?|x o? x o?|x o? x o?"},
    {"Old School", 16, S16, 0,
     "BD X..x|....|x.x.|....;"
     "SD ....|X...|....|X...;"
     "CP ....|x...|....|x...;"
     "CH x.x.|x.x.|x.x.|x.x.;"
     "AC x...|....|x...|....",
     "BD X..x|....|x.x.|..x.;"
     "CH x.x.|x.x.|x.x.|x...;"
     "OH ....|....|....|..x."},
    {"New Jack Swing", 16, S16, 66,
     "BD X..x|..x.|..x.|.x..;"
     "SD ....|X...|....|X...;"
     "CP ....|X...|....|X...;"
     "RS ....|...o|....|...o;"
     "CH xoxo|xoxo|xoxo|xoxo",
     "BD X..x|..x.|..x.|.x.x;"
     "SD ....|X..o|....|X.o.;"
     "CH xoxo|xoxo|xoxo|xo..;"
     "OH ....|....|....|..x."},
    {"Trap Classic", 16, S16, 0,
     "BD X...|...x|...x|....;"
     "CP ....|....|X...|....;"
     "SD ....|....|x...|....;"
     "CH x.x.|x.xx2|x.x.|xx3xx2;"
     "AC ....|....|x...|....",
     "BD X...|...x|..x.|x..x;"
     "CP ....|....|X...|...o;"
     "CH x.x.|x.x.|x.x3.|x4.x2."},
    {"Trap Triplets", 24, T16, 0,
     "BD X.....|......|...x..|....x.;"
     "CP ......|......|X.....|......;"
     "CH x.x.x.|x.x.x.|x.x.x.|xxxxxx;"
     "AC ......|......|x.....|......",
     "BD X.....|..x...|...x..|......;"
     "CH x.x.x.|xxx.x.|x.x.x.|x2.x2.x2.;"
     "RS ......|......|......|...x.."},
    {"Drill Slide", 16, S16, 0,
     "BD X...|....|..x.|....;"
     "SD ....|....|X...|.x..;"
     "CP ....|....|X...|....;"
     "CH x..x|..x.|x..x|..x.;"
     "AC ....|....|x...|....",
     "BD X...|...x|....|..x.;"
     "SD ....|....|X...|..x.;"
     "CH x..x|..x3.|x..x|.x3.."},
    {"Slow Jam", 16, S16, 35,
     "BD X...|...x|x...|....;"
     "CP ....|X...|....|X...;"
     "RS ..o.|....|..o.|.o..;"
     "CH xoxo?|xoxo?|xoxo?|xoxo?;"
     "AC ....|x...|....|x...",
     "BD X...|...x|x..x|....;"
     "CH xoxo?|xoxo?|xoxo?|xo..;"
     "OH ....|....|....|..x.;"
     "LT ....|....|....|...x"},
    {"Late Night R&B", 16, S16, 20,
     "BD X..x|....|..x.|.x..;"
     "SD ....|X...|....|X...;"
     "CP ....|x...|....|x...;"
     "RS ....|...o|....|....;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "BD X..x|....|..x.|.x.x;"
     "SD ....|X...|..o.|X...;"
     "CH x.x.|x.x.|x.x.|x.x2."},
    {"Crunk Stomp", 16, S16, 0,
     "BD X...|..x.|X...|..x.;"
     "SD ....|X...|....|X...;"
     "CP ....|X~...|....|X~...;"
     "CH x.x.|x.x.|x.x.|x.x.;"
     "AC ....|x...|....|x...",
     "BD X...|..x.|X.x.|..xx;"
     "CH x.x.|x.x.|x.x.|x2x2x2x2"},
    {"Lo-Fi Swing", 16, S16, 60,
     "BD X...|...o|..x.|....;"
     "SD ....|X...|....|X...;"
     "RS ....|....|...o|....;"
     "CH x o? x o?|x o? x o?|x o? x o?|x o? x o?",
     "BD X...|...o|..x.|..o.;"
     "SD ....|X..o|....|X...;"
     "CH x o? x o?|x o? x o?|x o? x o?|x o? . o?;"
     "OH ....|....|....|..x?."},
    {"Half-Time Heavy", 16, S16, 0,
     "BD X...|...x|....|..x.;"
     "SD ....|....|X...|....;"
     "CP ....|....|X...|....;"
     "CH x.x.|x.x.|x.x.|x...;"
     "OH ....|....|....|..x.;"
     "AC x...|....|x...|....",
     "BD X...|...x|..x.|....;"
     "SD ....|....|X...|.o..;"
     "CH x.x.|x.x.|x.xx|x..."},
    // fills B
    {"Trap Roll", 16, S16, 0,
     "BD X...|....|....|....;"
     "CP ....|....|X...|....;"
     "SD ....|....|....|..x2x4;"
     "CH x.x2.|x.x3.|x2x2x2x2|x3x3x4x4",
     "BD X...|...x|....|x...;"
     "CH x4...|x4...|x3x3x3x3|x4x4x4x4"},
    {"Hat Stutter", 16, S16, 0,
     "BD X...|....|x...|....;"
     "CP ....|....|....|X...;"
     "CH x2.x2.|x3.x3.|x4.x4.|x4x4x4x4",
     "CH x.x3x|.x3x.|x4x.x4|x4x4X4X4"},
    {"Snare Build", 16, S16, 0,
     "BD X...|....|X...|....;"
     "SD o.o.|o.o.|xxxx|XXX2X4;"
     "AC ....|....|....|x.xx",
     "SD oooo|xxxx|x2x2X2X2|X3X3X4X4;"
     "CP ....|....|....|..xX"},
    {"808 Drop", 16, S16, 0,
     "BD X...|....|....|....;"
     "CP ....|....|....|X...",
     "BD X...|....|....|..x.;"
     "CP ....|....|....|....;"
     "SD ....|....|....|X~..."},

    // ---- C: House & Techno -------------------------------------------------
    {"Deep House", 16, S16, 15,
     "BD x...|x...|x...|x...;"
     "CP ....|x...|....|x...;"
     "OH ..x.|..x.|..x.|..x.;"
     "CH .o.o?|.o.o|.o.o?|.o.o;"
     "RS ....|...x|....|....",
     "RS ...x|....|..x.|...x;"
     "CP ....|x...|....|x..o;"
     "MC ....|....|.x..|...."},
    {"Classic House", 16, S16, 0,
     "BD X...|x...|X...|x...;"
     "CP ....|X...|....|X...;"
     "OH ..x.|..x.|..x.|..x.;"
     "CH xx.x|xx.x|xx.x|xx.x",
     "BD X...|x...|X...|x..x;"
     "CP ....|X...|....|X.x.;"
     "HC ...x|....|..x.|...."},
    {"Garage Two-Step", 16, S16, 45,
     "BD X...|....|..x.|....;"
     "SD ....|x...|....|x...;"
     "RS ....|x...|....|x...;"
     "CH x.xx|.x.x|x.xx|.x.x;"
     "AC ....|x...|....|x...",
     "BD X...|...x|..x.|....;"
     "SD ....|x...|....|x.o.;"
     "CH x.xx|.x.x|x.xx|.x..;"
     "OH ....|....|....|..x."},
    {"Acid Jack", 16, S16, 0,
     "BD X...|X...|X...|X...;"
     "CP ....|x...|....|x...;"
     "RS ..x.|.x..|x..x|....;"
     "CH xxXx|xxXx|xxXx|xxXx",
     "CH xx.x|xx.x|xx.x|xx.x;"
     "OH ..x.|..x.|..x.|..x.;"
     "CB ...x|....|...x|...."},
    {"Techno Drive", 16, S16, 0,
     "BD X...|X...|X...|X...;"
     "CP ....|x...|....|x...;"
     "RS ...x|..x.|...x|.x..;"
     "OH ..x.|..x.|..x.|..x.;"
     "CH xx.x|xx.x|xx.x|xx.x",
     "BD X...|X...|X...|X..x;"
     "CP ....|x...|....|x.x.;"
     "RS ...x|..x.|.x.x|.x.."},
    {"Detroit Pulse", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "CP ....|X...|....|X...;"
     "CH x.xx|x.xx|x.xx|x.xx;"
     "HC ..x.|....|...x|..x.;"
     "LC ....|..x.|....|....",
     "CH xx.x|xx.x|xx.x|xx.x;"
     "OH ..x.|..x.|..x.|..x.;"
     "CL ...x|....|...x|...."},
    {"Minimal Tick", 16, S16, 10,
     "BD X...|x...|x...|x...;"
     "CP ....|....|....|x...;"
     "RS@ ..x..;"
     "CL@ x......;"
     "CH ..x.|..x.|..x.|..x.",
     "CL x..x...;"
     "OH ....|....|....|...x"},
    {"Disco Pump", 16, S16, 0,
     "BD X...|x...|X...|x...;"
     "SD ....|X...|....|X...;"
     "OH ..x.|..x.|..x.|..x.;"
     "CH xx.x|xx.x|xx.x|xx.x",
     "SD ....|X...|....|X.ox;"
     "CP ....|x...|....|x...;"
     "MA oxox|oxox|oxox|oxox"},
    {"Footwork 160", 16, S16, 0,
     "BD X..x|..x.|x..x|..x.;"
     "CP ....|....|....|X...;"
     "RS ....|x...|....|....;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "BD X..x|..x.|x..x|.x3x.;"
     "CP ....|....|....|X..x;"
     "CH x3.x.|x.x3.|x.x.|x3.x."},
    {"Club Bounce", 16, S16, 0,
     "BD X...|x...|x..x|..x.;"
     "CP ....|X...|....|X...;"
     "SD ....|....|...x|....;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "BD X...|x...|x.xx|..x.;"
     "CP ....|X...|..x.|X..x"},
    {"Rolling Techno", 32, S16, 0,
     "BD X...|x...|x...|x...|X...|x...|x...|x...;"
     "CP ....|x...|....|x...|....|x...|....|x..x;"
     "RS ...x|..x.|....|.x..|...x|..x.|..x.|....;"
     "OH ..x.|..x.|..x.|..x.|..x.|..x.|..x.|..x.;"
     "CH x..x|x..x|x..x|x..x|x..x|x..x|x..x|x..x",
     "CH xx.x|xx.x|xx.x|xx.x|xx.x|xx.x|xx.x|xx.x;"
     "RS ...x|..x.|.x..|.x..|...x|..x.|.x.x|..x.;"
     "CY X...|....|....|....|....|....|....|...."},
    {"Lo-Fi House", 16, S16, 30,
     "BD X...|x...|x...|x...;"
     "CP ....|x...|....|x...;"
     "OH ..x?.|..x.|..x?.|..x.;"
     "CH .o.o|.o.o|.o.o|.o.o;"
     "RS ....|..o.|....|...o",
     "BD X...|x...|x..o|x...;"
     "RS ..o.|...o|..o.|...o;"
     "SD ....|....|....|...o"},
    // fills C
    {"House Roll", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "SD ....|....|o.o.|xxXX;"
     "OH ..x.|..x.|....|....",
     "SD ....|o.o.|xxxx|x2x2X2X2;"
     "CP ....|x...|....|X..."},
    {"Clap Stack", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "CP ....|X~...|..x.|X~.x2X4;"
     "OH ..x.|..x.|..x.|....",
     "BD X...|x...|x...|xxxx;"
     "CP ..x.|X~...|x.x.|x3x3X3X4"},
    {"Tom Fill", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "HT ....|..x.|x...|....;"
     "MT ....|....|..x.|x...;"
     "LT ....|....|....|..xX;"
     "OH ..x.|....|....|....",
     "HT ..xx|....|x2...|....;"
     "MT ....|..xx|..x2.|....;"
     "LT ....|....|....|x2.x2X"},
    {"Cymbal Swell", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "CY X...|....|....|....;"
     "CH x.x.|x.x.|xxxx|x2x2x3x4",
     "CY ....|....|....|x...;"
     "CH x...|x...|xxxx|x2x2x3x4;"
     "OH ..x.|..x.|....|...."},

    // ---- D: Latin, Afro & Experimental -------------------------------------
    {"Dembow", 16, S16, 0,
     "BD X...|x...|X...|x...;"
     "SD ...x|..x.|...x|..x.;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "SD ...x|..x.|...x|..xx;"
     "CP ...x|..x.|...x|..x.;"
     "HC .x..|....|.x..|...."},
    {"Moombah", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "CP ...x|..x.|...x|..x.;"
     "CH ..x.|..x.|..x.|..x.;"
     "MC ....|x...|....|x...;"
     "HC x..x|....|x..x|....",
     "CH ....|..x.|....|..x.;"
     "OH ..x.|....|..x.|....;"
     "LT ....|....|....|...x"},
    {"Son Clave 3-2", 16, S8, 0,
     "CL x..x|..x.|..x.|x...;"
     "BD ...x|..x.|...x|..x.;"
     "CB X.x.|X.x.|X.x.|X.x.;"
     "HC ..x.|....|..x.|....;"
     "MC oo.o|ooX.|oo.o|ooX.;"
     "LC ....|...x|....|...x;"
     "MA xoxo|xoxo|xoxo|xoxo",
     "CB X.xx|X.xx|X.xx|X.x.;"
     "LC ....|...x|....|..xx"},
    {"Rumba Clave 3-2", 16, S8, 0,
     "CL x..x|...x|..x.|x...;"
     "BD x...|....|x...|....;"
     "HC .x..|....|.x..|..x.;"
     "MC o.x.|o.x.|o.x.|o.x.;"
     "LC ....|..xx|....|..x.",
     "HC .x.x|....|.xx.|x.x.;"
     "LC ....|..xx|..x.|..xx"},
    {"Bossa Nova", 16, S8, 0,
     "BD x..x|x..x|x..x|x..x;"
     "RS x..x|..x.|..x.|.x..;"
     "CH xoxo|xoxo|xoxo|xoxo",
     "RS ..x.|.x..|x..x|..x.;"
     "MA oxox|oxox|oxox|oxox"},
    {"Cumbia", 16, S16, 0,
     "BD x...|....|x...|....;"
     "RS ....|x...|....|x...;"
     "MA x.xx|x.xx|x.xx|x.xx;"
     "MC ..X.|..X.|..X.|..X.;"
     "HC .o..|.o..|.o..|.o..",
     "BD X...|..x.|X...|..x.;"
     "HC .o.o|.o..|.o.o|.o.x"},
    {"Afro 6/8", 12, T8, 0,
     "CB x.x|.xx|.x.|x.x;"
     "BD x..|...|x..|...;"
     "MA Xoo|xoo|xoo|xoo;"
     "MC ...|x..|...|x..;"
     "LC ...|..x|...|..x;"
     "HC .x.|...|.x.|...",
     "CL x.x|.xx|.x.|x.x;"
     "HC .x.|.x.|.xx|..."},
    {"Afro Swing", 16, S16, 30,
     "BD X...|..x.|x...|..x.;"
     "CP ....|x...|....|x...;"
     "CL x..x|..x.|x..x|..x.;"
     "RS ...x|....|..x.|....;"
     "MA xoxx|xoxx|xoxx|xoxx",
     "BD X...|..x.|x..x|..x.;"
     "RS ...x|..x.|..x.|...x;"
     "HC ....|.x..|....|.x.."},
    {"Three Over Four", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "CP ....|x...|....|x...;"
     "CH ..x.|..x.|..x.|..x.;"
     "CL@ x..;"
     "RS@ ..x..",
     "CL x.x;"
     "RS x...x;"
     "HC ...x|....|...x|...."},
    {"Euclid Garden", 16, S16, 0,
     "BD x..x|..x.|.x..|x...;"
     "CH x.xx|x.xx|x.xx|x.xx;"
     "CL@ x..x.x..x.x.;"
     "HC@ x.x.x..;"
     "RS@ x..x.x..x.x..",
     "BD x..x|..x.|.x..|x.x.;"
     "CL x.x..x.x..x."},
    {"Halftime Shuffle", 12, T8, 0,
     "BD x..|..x|...|.x.;"
     "SD ...|.o.|X..|.oo;"
     "CH x.x|x.x|x.x|x.x;"
     "AC ...|...|x..|...",
     "BD x..|..x|..x|...;"
     "CH x.x|x.x|x.x|x..;"
     "OH ...|...|...|..x"},
    {"Glitch 32", 32, S32, 0,
     "BD X.......|.....x..|x.......|........;"
     "SD ........|X.......|........|X.......;"
     "CH x...x...|x.x.x...|x...xxxx|x.x.x4...;"
     "RS ......x.|........|..x..x..|........",
     "BD X.......|.....x..|x.....x.|...x....;"
     "CH x3...x...|x.x.x2...|x...xxxx|x4...x4...;"
     "CL ........|...x....|........|......x."},
    // fills D
    {"Conga Run", 16, S16, 0,
     "BD X...|....|x...|....;"
     "CL x..x|..x.|..x.|x...;"
     "HC ....|....|xxxx|....;"
     "MC ....|....|....|xx..;"
     "LC ....|....|....|..xX",
     "HC ....|..x2.|x.x2.|....;"
     "MC ....|....|....|x2x..;"
     "LC ....|....|....|..x2X"},
    {"Timbale Fill", 16, S16, 0,
     "BD X...|....|....|...x;"
     "CB X.x.|X.x.|....|....;"
     "HT ....|....|x.xx|....;"
     "MT ....|....|....|x.x.;"
     "LT ....|....|....|.x.X",
     "HT ....|....|xx~xx|....;"
     "MT ....|....|....|x~x.x;"
     "LT ....|....|....|.x~.X"},
    {"Clave Break", 16, S16, 0,
     "BD X...|....|....|....;"
     "CL x..x|..x.|..x.|x...;"
     "HC ....|.x..|....|.x..;"
     "RS ....|....|....|..xx",
     "CL x..x|...x|..x.|x.x.;"
     "CB ....|....|x.x.|x..."},
    {"Polyrhythm Fill", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "HT x..x|..x.|.x..|x..x;"
     "MT .x..|x..x|..x.|.x..;"
     "LT ..x.|.x..|x..x|..x.",
     "HT x3...|x3...|....|....;"
     "MT ....|....|x3...|x3...;"
     "LT ....|....|....|...."},
}};
static_assert(checkBank(kFactory) == 0, "Factory bank: bad pattern (value = (slot + 1) * 100 + error)");

// ===========================================================================
// Bank 2: "Bass Music" — trap, drill, phonk, club and booty bass for long,
// tuned kicks. Internal bank A only.
// ===========================================================================
constexpr BankDefs kBassMusic{{
    {"Sub Trap", 16, S16, 0,
     "BD X...|....|..x.|....;"
     "CP ....|....|X...|....;"
     "CH x.x.|x.x.|x.x.|x.x2x2;"
     "AC ....|....|x...|....",
     "BD X...|...x|..x.|....;"
     "CH x.x.|x.x3.|x.x.|x4.x2."},
    {"Trap Bounce", 16, S16, 0,
     "BD X..x|....|..x.|..x.;"
     "CP ....|....|X...|....;"
     "SD ....|....|X...|...o;"
     "CH xxx.|xxx.|xx3x.|x2x2x2x2",
     "BD X..x|...x|..x.|....;"
     "RS ....|..x.|....|....;"
     "CH x.x.|x.x.|x.x.|x3x3x3x3"},
    {"Rage", 16, S16, 0,
     "BD X..x|..x.|X..x|..x.;"
     "CP ....|....|X...|....;"
     "SD ....|....|X...|....;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "BD X..x|..x.|X.xx|..x.;"
     "CH x.x2.|x.x2.|x.x2.|x2x2x2x2"},
    {"Drill Glide", 16, S16, 0,
     "BD X...|....|....|..x.;"
     "SD ....|...o|X...|..x.;"
     "CP ....|....|X...|....;"
     "CH x..x|..x.|x..x|..x.",
     "BD X...|..x.|....|x...;"
     "CH x..x3|..x.|x.x3.|..x."},
    {"Phonk Cowbell", 16, S16, 0,
     "BD X...|...x|..x.|....;"
     "CP ....|X...|....|X...;"
     "CB X..x|..x.|..x.|x.x.;"
     "CH xoxo|xoxo|xoxo|xoxo",
     "BD X...|...x|..x.|..x.;"
     "CB X..x|..x.|x..x|.xx."},
    {"Memphis Rattle", 16, S16, 0,
     "BD X..x|....|x.x.|....;"
     "SD ....|X...|....|X...;"
     "CB ..x.|....|..x.|....;"
     "CH xoxo|xoxo|xoxo|xoxo",
     "SD ....|X...|....|X..x2;"
     "CH xoxo|xoxo|xoxo|x3ox3o"},
    {"Jersey Kick", 16, S16, 0,
     "BD X..x|..x.|x.x.|x...;"
     "CP ....|X...|....|X...;"
     "CH ..x.|..x.|..x.|..x.",
     "BD X..x|..x.|x.x.|x.xx;"
     "CP ....|X...|..x.|X..."},
    {"Booty Bass", 16, S16, 0,
     "BD X...|..x.|.x..|x.x.;"
     "CP ....|X...|....|X...;"
     "CB ..x.|....|..x.|....;"
     "CH xoxo|xoxo|xoxo|xoxo",
     "BD X..x|..x.|.x..|x.xx;"
     "CH xoxo|xoxo|xoxo|x4ox4o"},
    {"Crunk Hall", 16, S16, 0,
     "BD X...|..X.|X...|..x.;"
     "CP ....|X~...|....|X~...;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "BD X...|..X.|X.x.|..xx;"
     "CH x.x.|x.x.|x.x.|x2x2x2x2"},
    {"Plugg Glow", 16, S16, 0,
     "BD X...|....|....|..x.;"
     "CP ....|....|X...|....;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "BD X...|....|..x.|..x.;"
     "CH x.x.|x.x.|x.x.|x.x3x3"},
    {"Ghetto Tech", 16, S16, 0,
     "BD X...|x...|x...|x...;"
     "CP ....|X...|....|X...;"
     "CH xoxo|xoxo|xoxo|xoxo",
     "BD X...|x...|x..x|x...;"
     "CP ....|X...|..x.|X..x"},
    {"Dubstep Half", 16, S16, 0,
     "BD X...|....|....|..x.;"
     "SD ....|....|X...|....;"
     "CP ....|....|X...|....;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "BD X...|....|..x.|.x..;"
     "SD ....|....|X...|...o"},
    // fills
    {"Roll Up", 16, S16, 0,
     "BD X...|....|....|....;"
     "CP ....|....|X...|....;"
     "SD ....|....|..x.|xxx2x4;"
     "CH x2.x2.|x3.x3.|x2x2x2x2|x4x4x4x4",
     "BD X...|....|x...|....;"
     "CH x3x3x3x3|x3x3x3x3|x4x4x4x4|x4x4X4X4"},
    {"Stutter Kick", 16, S16, 0,
     "BD X...|....|x2.x3.|x4...;"
     "CP ....|....|X...|....;"
     "CH x.x.|x.x.|....|....",
     "BD X...|..x.|x3x3..|x4x4.."},
    {"Triplet Drop", 24, T16, 0,
     "BD X.....|......|......|......;"
     "CP ......|......|X.....|......;"
     "CH xxx...|xxx...|xxxxxx|xxxxxx",
     "CH x.x.x.|x.x.x.|xxxxxx|x2x2x2x2x2x2;"
     "SD ......|......|......|...xxx"},
    {"Clap Gap", 16, S16, 0,
     "BD X...|....|....|....;"
     "CP ....|....|....|X~...",
     "CP ....|....|..x.|X~...;"
     "SD ....|....|....|...x"},
}};
static_assert(checkBank(kBassMusic) == 0, "Bass Music bank: bad pattern (value = (slot + 1) * 100 + error)");

// ===========================================================================
// Bank 3: "World Grooves" — clave, bell and hand-drum rhythms. Clave patterns
// use the eighth-note scale, so one 16-step pass is the two-bar clave cycle
// at the style's real tempo.
// ===========================================================================
constexpr BankDefs kWorld{{
    {"Cascara 2-3", 16, S8, 0,
     "CL ..x.|x...|x..x|..x.;"
     "RS x.x.|xx.x|x.xx|.x.x;"
     "BD ...x|..x.|...x|..x.;"
     "MC oo.o|ooX.|oo.o|ooX.;"
     "HC ..x.|....|..x.|....",
     "RS x.xx|.x.x|x.x.|xx.x;"
     "LC ....|...x|....|...x"},
    {"Mambo Bell", 16, S8, 0,
     "CL x..x|..x.|..x.|x...;"
     "CB X.x.|xx.x|X.xx|.x.x;"
     "BD ...x|..x.|...x|..x.;"
     "MC oo.o|ooX.|oo.o|ooX.;"
     "HC ..x.|....|..x.|....;"
     "LC ....|...x|....|...x",
     "CB X.xx|.x.x|X.x.|xx.x;"
     "HT ....|....|....|..xx"},
    {"Cha-Cha", 16, S16, 0,
     "BD X...|....|..x.|....;"
     "CB X...|x...|X...|x...;"
     "MA x.xx|x...|x.xx|x...;"
     "MC ....|....|....|X.X.;"
     "HC ....|..x.|....|....;"
     "CL ....|x...|..x.|....",
     "MC ....|..x.|....|X.X.;"
     "LC ....|....|....|...x"},
    {"Samba Batucada", 16, S16, 0,
     "LT o...|X...|o...|X...;"
     "BD x...|X...|x...|X...;"
     "SD xoox|xoox|xoox|xoox;"
     "RS x.xx|.xx.|x.xx|.x.x;"
     "MA Xoox|Xoox|Xoox|Xoox",
     "RS x.x.|x.xx|.x.x|x.x.;"
     "CB x.x.|..x.|x.x.|..x."},
    {"Baiao", 16, S16, 0,
     "BD x..x|....|x..x|....;"
     "LT ....|..x.|....|..x.;"
     "CH x.x.|x.x.|x.x.|x.x.;"
     "OH .x.x|.x.x|.x.x|.x.x",
     "BD x..x|..x.|x..x|..x.;"
     "MA xoxo|xoxo|xoxo|xoxo"},
    {"Soca Drive", 16, S16, 0,
     "BD X...|x...|X...|x...;"
     "CP ....|x...|....|x...;"
     "CB ..x.|..x.|..x.|..x.;"
     "CH xx.x|xx.x|xx.x|xx.x;"
     "SD ...o|....|...o|..o.",
     "SD ...o|..x.|...o|..xx;"
     "HT ....|....|....|.x..;"
     "MT ....|....|....|..x."},
    {"Merengue", 16, S16, 0,
     "BD x...|x...|x...|x...;"
     "MA x.xx|x.xx|x.xx|x.xx;"
     "HC ....|..x.|....|..x.;"
     "MC ...x|....|...x|x...;"
     "RS ..x.|..x.|..x.|..x.",
     "HC ..xx|..x.|..xx|..x.;"
     "LC ....|....|....|..xx"},
    {"Habanera", 16, S16, 0,
     "BD x..x|x.x.|x..x|x.x.;"
     "CL x..x|..x.|x..x|..x.;"
     "CH x.x.|x.x.|x.x.|x.x.;"
     "RS ....|x...|....|x...",
     "CL x..x|..x.|..x.|x...;"
     "MA oxox|oxox|oxox|oxox"},
    {"Bembe 12/8", 12, T8, 0,
     "CB x.x|.xx|.x.|x.x;"
     "BD x..|...|x..|...;"
     "MA xoo|xoo|xoo|xoo;"
     "LC ..x|...|..x|...;"
     "MC ...|x.x|...|x..;"
     "HC .x.|...|.x.|.xx",
     "BD x..|..x|x..|..x;"
     "HC .xx|...|.x.|xxx"},
    {"Highlife Bell", 16, S16, 0,
     "CB x.x.|x.x.|xx.x|.x.x;"
     "BD x...|..x.|x...|....;"
     "RS ....|x...|....|x...;"
     "CH .x.x|.x.x|.x.x|.x.x;"
     "MC ..x.|....|..x.|..x.",
     "CB x.xx|.x.x|x.x.|xx.x;"
     "HC ...x|....|...x|...."},
    {"Tresillo Pop", 16, S16, 0,
     "BD x..x|..x.|x..x|..x.;"
     "SD ....|X...|....|X...;"
     "MA xoxo|xoxo|xoxo|xoxo;"
     "CL ...x|....|...x|....",
     "BD x..x|..x.|x..x|.xx.;"
     "CP ....|x...|....|x..x"},
    {"One Drop", 16, S16, 50,
     "BD ....|....|X...|....;"
     "RS ....|....|X...|....;"
     "CH x.x.|x.x.|x.x.|x.x.",
     "RS ....|....|X...|..o.;"
     "CH x.x.|x.x.|x.x.|x...;"
     "OH ....|....|....|..x."},
    // fills
    {"Bongo Roll", 16, S16, 0,
     "CL x..x|..x.|..x.|x...;"
     "BD x...|....|....|....;"
     "HC ....|....|xxxx|x.x.;"
     "MC ....|....|....|.x.X",
     "HC ....|..x.|x2x2x2x2|x.x.;"
     "LC ....|....|....|...x"},
    {"Timbal Abanico", 16, S16, 0,
     "CB X.x.|X.x.|X.x.|....;"
     "RS ....|....|....|x4x4x4.;"
     "CY ....|....|....|...X;"
     "BD x...|....|....|...x",
     "RS ....|....|..x3x3|x4x4x4.;"
     "HT ....|....|x.x.|...."},
    {"Surdo Break", 16, S16, 0,
     "LT X...|X...|X.X.|XXXX;"
     "BD x...|x...|x...|x...;"
     "RS x.xx|.xx.|x.xx|.x.x",
     "LT X...|X..X|X.X.|X3X3X3X;"
     "RS ....|....|....|...."},
    {"Bell Break", 16, S16, 0,
     "CB x.x.|x.xx|.x.x|X.X.;"
     "CL x..x|..x.|..x.|x...;"
     "BD x...|....|....|....",
     "CB X.X.|X.X.|xxxx|X~.X."},
}};
static_assert(checkBank(kWorld) == 0, "World Grooves bank: bad pattern (value = (slot + 1) * 100 + error)");

// ===========================================================================
// Bank 4: "Building Blocks" — single-part patterns to layer, copy tracks from,
// or use as a click; plus the classic "sound check" that plays every voice.
// ===========================================================================
constexpr BankDefs kBlocks{{
    {"Four on the Floor", 16, S16, 0,
     "BD X...|x...|x...|x...",
     "BD X...|x...|x...|x..o"},
    {"Two and Four", 16, S16, 0,
     "SD ....|X...|....|X...;"
     "CP ....|x...|....|x...",
     "SD ....|X..o|.o..|X..o"},
    {"Eighth Hats", 16, S16, 0,
     "CH x.o.|x.o.|x.o.|x.o.",
     "CH x.o.|x.o.|x.o.|x...;"
     "OH ....|....|....|..x."},
    {"Sixteenth Hats", 16, S16, 0,
     "CH xoxo|xoxo|xoxo|xoxo",
     "CH Xo?xo?|Xo?xo?|Xo?xo?|Xo?xo?"},
    {"Offbeat Open Hat", 16, S16, 0,
     "OH ..x.|..x.|..x.|..x.",
     "OH ..x.|..x.|..x.|..x.;"
     "CH xx.x|xx.x|xx.x|xx.x"},
    {"Trap Hats", 16, S16, 0,
     "CH x.x.|x.xx2|x.x.|x.x3x2",
     "CH x.x.|x.x.|x3x3x.|x4.x2."},
    {"Shaker", 16, S16, 0,
     "MA xoxo|xoxo|xoxo|xoxo",
     "MA Xoxo|xoXo|xoxo|Xoxo"},
    {"Son Clave", 16, S8, 0,
     "CL x..x|..x.|..x.|x...",
     "CL ..x.|x...|x..x|..x."},
    {"Rumba Clave", 16, S8, 0,
     "CL x..x|...x|..x.|x...",
     "CL ..x.|x...|x..x|...x"},
    {"Metronome", 16, S16, 0,
     "CL X...|x...|x...|x...",
     "CB X...|x...|x...|x..."},
    {"Voice Check", 16, S16, 0,
     "BD x...|....|....|....;"
     "SD .x..|....|....|....;"
     "LT ..x.|....|....|....;"
     "MT ...x|....|....|....;"
     "HT ....|x...|....|....;"
     "LC ....|.x..|....|....;"
     "MC ....|..x.|....|....;"
     "HC ....|...x|....|....;"
     "RS ....|....|x...|....;"
     "CL ....|....|.x..|....;"
     "CP ....|....|..x.|....;"
     "MA ....|....|...x|....;"
     "CB ....|....|....|x...;"
     "CY ....|....|....|.x..;"
     "OH ....|....|....|..x.;"
     "CH ....|....|....|...x",
     "AC xxxx|xxxx|xxxx|xxxx"},
    {"Dynamics", 16, S16, 0,
     "SD o...|x...|X...|X...;"
     "BD o...|x...|X...|X...;"
     "AC ....|....|....|x...",
     "SD o.o.|x.x.|X.X.|X~.X2."},
    // fills
    {"Snare Sixteenths", 16, S16, 0,
     "SD oooo|oooo|xxxx|XXXX",
     "SD o.o.|oooo|xxxx|XXXX"},
    {"Snare Rush", 16, S16, 0,
     "SD o2o2o2o2|x2x2x2x2|x3x3x3x3|X4X4X4X4",
     "SD ....|o2o2o2o2|x3x3x3x3|X4X4X4X4"},
    {"Tom Cascade", 16, S16, 0,
     "HT xxxx|....|....|....;"
     "MT ....|xxxx|....|....;"
     "LT ....|....|xxxx|xxxX",
     "HT x.xx|x...|....|....;"
     "MT ....|.xx.|xx..|....;"
     "LT ....|....|..xx|x~xxX"},
    {"Crash Out", 16, S16, 0,
     "CY X...|....|....|....;"
     "BD X...|....|....|....",
     "CY X...|....|....|....;"
     "BD X...|....|....|...x;"
     "AC x...|....|....|...."},
}};
static_assert(checkBank(kBlocks) == 0, "Building Blocks bank: bad pattern (value = (slot + 1) * 100 + error)");

constexpr BankDefs kEmpty{};

// Optional song chain per bank (global pattern indices).
struct ChainDef {
    std::uint8_t length = 0;
    std::array<std::uint8_t, 16> entries{};
};

struct BankEntry {
    BankInfo info;
    const BankDefs* defs;
    ChainDef chain;
};

const std::array<BankEntry, 5> kBanks{{
    {{"Factory", "Mixed", 120.0f}, &kFactory, {}},
    {{"Init", "Empty", 120.0f}, &kEmpty, {}},
    // 8-bar arrangement: three bars of Sub Trap, the stutter-kick fill, two bars of
    // Trap Bounce, the roll-up and the clap gap, then round again.
    {{"Bass Music", "Bass", 140.0f}, &kBassMusic, {8, {0, 0, 0, 13, 1, 1, 12, 15}}},
    {{"World Grooves", "Latin", 110.0f}, &kWorld, {}},
    {{"Building Blocks", "Utility", 120.0f}, &kBlocks, {}},
}};

int clampBank(int index) noexcept {
    return index < 0 ? 0 : (index >= static_cast<int>(kBanks.size()) ? static_cast<int>(kBanks.size()) - 1 : index);
}

}  // namespace

int bankCount() noexcept { return static_cast<int>(kBanks.size()); }

const BankInfo& bankInfoAt(int index) noexcept { return kBanks[static_cast<std::size_t>(clampBank(index))].info; }

void buildBankAt(int index, seq::PatternBank& out) noexcept {
    const BankEntry& bank = kBanks[static_cast<std::size_t>(clampBank(index))];
    for (std::size_t i = 0; i < out.patterns.size(); ++i) {
        // Checked at compile time, so this cannot fail; on a failure the slot
        // would simply be left as parsed so far.
        static_cast<void>(parsePattern((*bank.defs)[i], out.patterns[i]));
    }
    out.chain = seq::Chain{};
    out.chain.length = bank.chain.length;
    for (std::size_t i = 0; i < bank.chain.length; ++i) out.chain.entries[i] = bank.chain.entries[i];
}

std::string_view patternNameAt(int bankIndex, int patternIndex) noexcept {
    if (patternIndex < 0 || patternIndex >= seq::kNumPatterns) return {};
    return (*kBanks[static_cast<std::size_t>(clampBank(bankIndex))].defs)[static_cast<std::size_t>(patternIndex)].name;
}

}  // namespace mw808::factory::detail

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Pattern data model. Everything here is trivially copyable, fixed-size POD so
// patterns can cross threads by value through lock-free queues and be written
// to state as raw, versioned bytes.

#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "mw808/Voices.h"

namespace mw808::seq {

inline constexpr int kMaxSteps = 64;                  // 4 pages of 16
inline constexpr int kAccentTrack = kNumVoices;       // global accent row
inline constexpr int kNumTracks = kNumVoices + 1;     // 16 voices + accent
inline constexpr int kNumVariations = 2;              // A, B
inline constexpr int kPatternsPerBank = 16;           // 1-12 basic, 13-16 intro/fill
inline constexpr int kNumBanks = 4;
inline constexpr int kNumPatterns = kNumBanks * kPatternsPerBank;
inline constexpr int kFirstFillSlot = 12;             // index within a bank
inline constexpr int kMaxChainLength = 64;

// Step length. The first four are the original PRE-SCALE positions 1..4.
enum class Scale : std::uint8_t {
    EighthTriplet = 0,     // pre-scale 1: 3 steps per beat
    SixteenthTriplet,      // pre-scale 2: 6 steps per beat
    Sixteenth,             // pre-scale 3: 4 steps per beat (default)
    ThirtySecond,          // pre-scale 4: 8 steps per beat
    Eighth,                // 2 steps per beat
    kNumScales
};

// Step length in quarter notes.
constexpr double stepLengthQn(Scale s) noexcept {
    switch (s) {
        case Scale::EighthTriplet:    return 1.0 / 3.0;
        case Scale::SixteenthTriplet: return 1.0 / 6.0;
        case Scale::Sixteenth:        return 0.25;
        case Scale::ThirtySecond:     return 0.125;
        case Scale::Eighth:           return 0.5;
        default:                      return 0.25;
    }
}

enum class StepLevel : std::uint8_t { Off = 0, Weak = 1, Normal = 2, Accent = 3 };

// One step of one track, packed in a byte:
//   bits 0-1  level        (StepLevel; on the accent track any non-zero = accent on)
//   bit  2    flam         (grace hit, then the main hit one flam-time later)
//   bits 3-4  sub-steps    (0..3 -> x1..x4 evenly spaced hits inside the step)
//   bits 5-7  probability  (0 = always; n = (8-n)/8 chance, i.e. 87.5% .. 12.5%)
struct Step {
    std::uint8_t bits = 0;

    constexpr StepLevel level() const noexcept { return static_cast<StepLevel>(bits & 0x03U); }
    constexpr bool on() const noexcept { return (bits & 0x03U) != 0U; }
    constexpr bool flam() const noexcept { return (bits & 0x04U) != 0U; }
    constexpr int subSteps() const noexcept { return 1 + ((bits >> 3) & 0x03); }
    constexpr int probabilityCode() const noexcept { return (bits >> 5) & 0x07; }
    constexpr float probability() const noexcept {
        return static_cast<float>(8 - probabilityCode()) / 8.0f;
    }

    constexpr void setLevel(StepLevel l) noexcept {
        bits = static_cast<std::uint8_t>((bits & ~0x03U) | static_cast<std::uint8_t>(l));
    }
    constexpr void setFlam(bool f) noexcept {
        bits = static_cast<std::uint8_t>(f ? (bits | 0x04U) : (bits & ~0x04U));
    }
    constexpr void setSubSteps(int n) noexcept {  // 1..4
        const auto code = static_cast<std::uint8_t>((n < 1 ? 1 : (n > 4 ? 4 : n)) - 1);
        bits = static_cast<std::uint8_t>((bits & ~0x18U) | (code << 3));
    }
    constexpr void setProbabilityCode(int c) noexcept {  // 0..7
        const auto code = static_cast<std::uint8_t>(c < 0 ? 0 : (c > 7 ? 7 : c));
        bits = static_cast<std::uint8_t>((bits & ~0xE0U) | (code << 5));
    }

    constexpr bool operator==(const Step&) const noexcept = default;
};

struct Pattern {
    std::uint8_t length = 16;                               // 1..kMaxSteps
    Scale scale = Scale::Sixteenth;
    std::uint8_t shuffle = 0;                               // 0..100: delay of odd steps, % of the maximum (a triplet feel at 100)
    std::uint8_t reserved = 0;
    std::array<std::uint8_t, kNumVoices> trackLength{};     // 0 = follow `length`; else 1..kMaxSteps (polymeter)
    std::array<std::array<std::array<Step, kMaxSteps>, kNumTracks>, kNumVariations> steps{};

    constexpr Step& at(int variation, int track, int step) noexcept {
        return steps[static_cast<std::size_t>(variation)][static_cast<std::size_t>(track)]
                    [static_cast<std::size_t>(step)];
    }
    constexpr const Step& at(int variation, int track, int step) const noexcept {
        return steps[static_cast<std::size_t>(variation)][static_cast<std::size_t>(track)]
                    [static_cast<std::size_t>(step)];
    }
    // Effective loop length of a track (voice tracks may be shorter/longer than
    // the pattern; the accent track always follows the pattern).
    constexpr int lengthOf(int track) const noexcept {
        if (track < kNumVoices && trackLength[static_cast<std::size_t>(track)] != 0)
            return trackLength[static_cast<std::size_t>(track)];
        return length;
    }

    constexpr bool operator==(const Pattern&) const noexcept = default;
};

static_assert(std::is_trivially_copyable_v<Pattern>);

// A song-style chain of patterns. length == 0 means "no chain".
struct Chain {
    std::uint8_t length = 0;
    std::array<std::uint8_t, kMaxChainLength> entries{};    // global pattern indices
    constexpr bool operator==(const Chain&) const noexcept = default;
};

static_assert(std::is_trivially_copyable_v<Chain>);

struct PatternBank {
    std::array<Pattern, kNumPatterns> patterns{};
    Chain chain{};
};

// How A/B variations play (the original BASIC VARIATION switch).
enum class VariationMode : std::uint8_t { A = 0, AB = 1, B = 2 };

// Auto fill-in interval, in measures (pattern passes). The fill replaces the
// last measure of each group. Manual = only when requested.
enum class AutoFill : std::uint8_t { Manual = 0, Every16, Every12, Every8, Every4, Every2 };

constexpr int autoFillMeasures(AutoFill f) noexcept {
    switch (f) {
        case AutoFill::Every16: return 16;
        case AutoFill::Every12: return 12;
        case AutoFill::Every8:  return 8;
        case AutoFill::Every4:  return 4;
        case AutoFill::Every2:  return 2;
        default:                return 0;
    }
}

}  // namespace mw808::seq

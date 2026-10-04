// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Pattern editing helpers (docs/design/architecture.md §5.6). Pure functions
// on a Pattern value: no allocation, no globals, deterministic for a seed.
// Meant for the message thread, but nothing here would hurt the audio thread.
//
// Conventions shared by every function:
//  * `variation` is 0 (A) or 1 (B); `track` is 0..kNumTracks-1 (16 voices,
//    then the global accent row at kAccentTrack). Out-of-range arguments make
//    the call a no-op (it never writes outside the arrays).
//  * Track-wise edits act on the track's own loop length (Pattern::lengthOf),
//    so they respect polymeter: steps beyond that length are left untouched.

#pragma once

#include <array>
#include <cstdint>

#include "mw808/seq/Pattern.h"

namespace mw808::seq {

inline constexpr int kBothVariations = -1;

// Level class of a voice hit once the global accent row is applied
// (architecture.md §5.3): Weak -> Normal, Normal -> Accent, Accent stays Accent
// (the extra +0.25 accent knob is applied by the sequencer to the volts only).
constexpr StepLevel promotedLevel(StepLevel voiceLevel, bool accentOn) noexcept {
    if (!accentOn || voiceLevel == StepLevel::Off) return voiceLevel;
    if (voiceLevel == StepLevel::Weak) return StepLevel::Normal;
    return StepLevel::Accent;
}

// Silence every step (of one variation, or both). Length, scale, shuffle and
// track lengths are kept.
void clearPattern(Pattern& p, int variation = kBothVariations) noexcept;

// Silence one track of one variation (all 64 cells, not only the loop).
void clearTrack(Pattern& p, int variation, int track) noexcept;

// Copy all 64 cells of a track. When both tracks are voice tracks the
// per-track length comes along too. Source and destination may be the same
// pattern (and may overlap: the copy goes through a temporary).
void copyTrack(const Pattern& src, int srcVariation, int srcTrack, Pattern& dst, int dstVariation,
               int dstTrack) noexcept;
void copyTrack(Pattern& p, int variation, int fromTrack, int toTrack) noexcept;

// Rotate a track by `steps` (positive = later), wrapping within its length.
void shiftTrack(Pattern& p, int variation, int track, int steps) noexcept;
// Rotate every track (accent row included), each within its own length.
void shiftPattern(Pattern& p, int variation, int steps) noexcept;

// Within the track length: set steps become Off, empty steps become Normal
// with no flam, sub-steps or probability.
void invertTrack(Pattern& p, int variation, int track) noexcept;

// Copy variation `from` onto `to` (A <-> B).
void copyVariation(Pattern& p, int from, int to) noexcept;

// Fill the track (within its length) at random: each step is on with
// probability `density` (0..1). Levels are mostly Normal with some Weak and
// Accent on voice tracks; flam, sub-step and probability bits are always off.
// Writing OH clears the steps where CH is on and vice versa, so the hats never
// sound together (index-wise; see the note on polymeter in the .cpp).
void randomizeTrack(Pattern& p, int variation, int track, float density, std::uint32_t seed) noexcept;

// randomizeTrack over all 17 tracks with per-track densities. Same seed and
// densities give the same pattern. Afterwards no step has both OH and CH set.
void randomizePattern(Pattern& p, int variation, const std::array<float, kNumTracks>& densities,
                      std::uint32_t seed) noexcept;

// Double the pattern length by repetition (both variations). Tracks that
// follow the pattern length repeat their content; tracks with their own
// length keep it. Returns false (and changes nothing) above kMaxSteps.
bool expandPattern(Pattern& p) noexcept;

// Euclidean rhythm: `hits` maximally even onsets over the track length,
// rotated by `rotation` steps (positive = later). Set steps are Normal; the
// track is cleared first. hits is clamped to 0..length.
void setEuclidean(Pattern& p, int variation, int track, int hits, int rotation) noexcept;

}  // namespace mw808::seq

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Pattern <-> Standard MIDI File (docs/design/architecture.md §5.7).
// Message-thread only: these functions allocate (std::vector).
//
// writeMidiFile: a format-0 file holding ONE measure of one variation, on
// channel 10 at 480 PPQ. Notes sit where the sequencer would play them:
// shuffle, flam (grace note then main note flamMs later) and sub-steps are
// rendered as real positions, the global accent row is folded into the
// velocities (weak / normal / accent = 48 / 96 / 127, grace = 0.6 x main).
// Probability cannot be expressed in notes; every hit is written.
//
// Optionally (on by default) the file also carries the exact pattern in a
// sequencer-specific meta event (FF 7F, non-commercial id 7D, "MW80"), so
// readMidiFile restores an mw808 pattern bit for bit, probability included.
// DAWs ignore that event.
//
// readMidiFile: accepts format 0/1/2 files with a metrical time base. With the
// embedded event present it restores the pattern from it. Otherwise it
// rebuilds steps on the grid of `out.scale`: the length comes from the end of
// the longest track (1..64 steps), the level from the velocity (<72 weak,
// <112 normal, else accent). Per voice it takes each step's onset (odd steps
// may be up to a third of a step late: shuffle), claims the notes that onset
// produced (n evenly spaced sub-step notes, up to 4; for a flam, the main
// note one flam spacing after a grace of 0.6 x its velocity, the spacing
// being the most common such distance in the file), cyclically so hits the
// writer wrapped to the top return to the last steps; leftover notes go to
// their nearest step. Files without such grace/main pairs fall back to "a
// clearly louder note within half a step is the flam's main note". Notes
// alone stay lossy where two hits of one key share a tick, or a flam is longer
// than the measure.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "mw808/Voices.h"
#include "mw808/seq/Pattern.h"

namespace mw808::seq {

inline constexpr int kMidiFilePpq = 480;
inline constexpr std::uint8_t kMidiVelocityWeak = 48;
inline constexpr std::uint8_t kMidiVelocityNormal = 96;
inline constexpr std::uint8_t kMidiVelocityAccent = 127;

struct MidiFileOptions {
    bool includeTempo = true;       // write a tempo meta event
    double bpm = 120.0;             // tempo, and the time base that turns flamMs into ticks
    float flamMs = 24.0f;           // grace-to-main spacing (Settings::flamMs)
    float swingScale = 1.0f;        // global shuffle multiplier (Settings::swingScale)
    bool oneStepNotes = false;      // note length: one step (true) or a 1/16 note (false)
    bool embedPattern = true;       // carry the exact pattern in a sequencer-specific meta event
    int accentNote = -1;            // 0..127: also write the accent row as notes on this key
};

// `noteMap[voice]` is the MIDI note of each voice (0..127).
std::vector<std::uint8_t> writeMidiFile(const Pattern& pattern, int variation,
                                        const std::array<std::uint8_t, kNumVoices>& noteMap,
                                        const MidiFileOptions& options);

// Reads into `out` (variation `variation`, which is cleared first; the other
// variation is kept). Returns false, leaving `out` untouched, when the bytes
// are not a well-formed metrical MIDI file.
bool readMidiFile(const std::vector<std::uint8_t>& bytes, const std::array<std::uint8_t, kNumVoices>& noteMap,
                  Pattern& out, int variation = 0);

}  // namespace mw808::seq

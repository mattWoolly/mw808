// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// MIDI note <-> voice mapping and velocity <-> dynamics conventions. The map
// is General-MIDI compatible, with the extra aliases that drum clips written
// for other emulations of this machine commonly use.

#pragma once

#include <array>
#include <cstdint>

#include "mw808/Voices.h"

namespace mw808 {

// The note each voice sends (MIDI out, MIDI file export) and primarily answers to.
inline constexpr std::array<std::uint8_t, kNumVoices> kVoiceNote{{
    36,  // BD
    38,  // SD
    41,  // LT
    45,  // MT
    48,  // HT
    64,  // LC
    63,  // MC
    62,  // HC
    37,  // RS
    75,  // CL
    39,  // CP
    70,  // MA
    56,  // CB
    49,  // CY
    46,  // OH
    42,  // CH
}};

inline constexpr std::int8_t kNoVoice = -1;

// note -> voice (or kNoVoice).
inline constexpr std::array<std::int8_t, 128> kNoteToVoice = [] {
    std::array<std::int8_t, 128> map{};
    for (auto& m : map) m = kNoVoice;
    for (int v = 0; v < kNumVoices; ++v) map[kVoiceNote[static_cast<std::size_t>(v)]] = static_cast<std::int8_t>(v);
    // Aliases.
    map[35] = BD;
    map[40] = SD;
    map[43] = LT;
    map[47] = MT;
    map[50] = HT;
    map[44] = CH;
    map[51] = CY;
    map[52] = CY;
    map[55] = CY;
    map[57] = CY;
    map[60] = HC;
    map[61] = LC;
    map[82] = MA;
    return map;
}();

// Velocities written for the three step levels (MIDI out and file export).
inline constexpr std::uint8_t kVelocityWeak = 48;
inline constexpr std::uint8_t kVelocityNormal = 96;
inline constexpr std::uint8_t kVelocityAccent = 127;

// HostNote mode: note kPatternNoteBase + i plays pattern i.
inline constexpr int kPatternNoteBase = 36;

// Chromatic bass-drum zone: this note plays the stock pitch.
inline constexpr int kBassRootNote = 36;

// Velocity -> trigger pulse height.
//   velocity-sensitive: 1..100 scales linearly up to the normal 4 V pulse;
//     101..127 moves from normal to the accent level set by the ACCENT knob.
//     So 96 ~= normal, 127 = accent, 48 ~= weak, matching the export velocities.
//   fixed: three classes (weak / normal / accent) by velocity range.
constexpr Hit velocityToHit(int velocity, float accentKnob, bool sensitive) noexcept {
    const int v = velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity);
    if (!sensitive) {
        if (v < 72) return Hit::weak();
        if (v < 112) return Hit::normal();
        return Hit::accented(accentKnob);
    }
    if (v <= 100) {
        const float volts = Hit::kNormalVolts * static_cast<float>(v) / 100.0f;
        return {volts < 0.5f ? 0.5f : volts, 0.0f};
    }
    const float t = static_cast<float>(v - 100) / 27.0f;
    return {Hit::kNormalVolts + (Hit::kMaxVolts - Hit::kNormalVolts) * accentKnob * t, 0.0f};
}

}  // namespace mw808

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Factory content, compiled in as plain data: kits (sound + mixer settings),
// pattern banks, and presets that pair a kit with a bank. Message-thread /
// offline use only (building a bank copies ~140 KB); nothing here is called
// from the audio path.

#pragma once

#include <array>
#include <string_view>

#include "mw808/Engine.h"
#include "mw808/Voices.h"
#include "mw808/seq/Pattern.h"

namespace mw808::factory {

// A kit: every per-voice sound and mixer setting, plus accent and the master section.
struct Kit {
    std::string_view name;
    std::string_view category;  // e.g. "Classic", "Bass", "Driven"
    std::array<VoiceParams, kNumVoices> voice{};
    std::array<ChannelParams, kNumVoices> channel{};
    float accent = 0.7f;
    MasterParams master{};
};

int numKits() noexcept;
const Kit& kit(int index) noexcept;  // index clamped; kit(0) is the stock machine

// A pattern bank: 64 patterns (4 banks x 16; slots 13-16 of each bank are
// intro/fill patterns) and an optional chain.
struct BankInfo {
    std::string_view name;
    std::string_view category;
    float tempo = 120.0f;  // suggested tempo
};

int numBanks() noexcept;
const BankInfo& bankInfo(int index) noexcept;                 // bank 0 is the default memory
void buildBank(int index, seq::PatternBank& out) noexcept;    // overwrites `out`
// Display name of one pattern in a factory bank ("" if the slot is empty).
std::string_view patternName(int bankIndex, int patternIndex) noexcept;

// A full preset: a kit, a bank and the sequencer feel that goes with them.
struct Preset {
    std::string_view name;
    std::string_view category;
    int kit = 0;
    int bank = 0;
    int pattern = 0;       // pattern selected on load
    float tempo = 120.0f;
};

int numPresets() noexcept;
const Preset& preset(int index) noexcept;  // preset(0) is the default state of a new instance

}  // namespace mw808::factory

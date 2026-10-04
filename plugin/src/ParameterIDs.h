// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Every host-visible parameter id (docs/design/plugin.md §2). The ids are
// serialised in sessions and presets: they are FROZEN. Never rename one; add
// new ids instead and bump kParameterVersion for the new parameters.
//
// Per-voice ids are "<voice>_<suffix>", with the voice prefix taken from
// mw808::kVoiceInfo[v].id ("bd", "sd", ...). JUCE-free on purpose.

#pragma once

#include <array>
#include <string>
#include <string_view>

#include "mw808/Voices.h"

namespace mw808::plugin::paramid {

// juce::ParameterID version hint of every parameter shipped so far.
inline constexpr int kParameterVersion = 1;

// --- global ------------------------------------------------------------------
inline constexpr const char* masterVolume = "master_volume";
inline constexpr const char* accent = "accent";
inline constexpr const char* analogOut = "analog_out";
inline constexpr const char* masterDrive = "master_drive";
inline constexpr const char* masterComp = "master_comp";
inline constexpr const char* limiter = "limiter";
inline constexpr const char* quality = "quality";
inline constexpr const char* variance = "variance";
inline constexpr const char* seqMode = "seq_mode";
inline constexpr const char* seqRun = "seq_run";
inline constexpr const char* tempo = "tempo";
inline constexpr const char* shuffleDepth = "shuffle_depth";
inline constexpr const char* flamTime = "flam_time";
inline constexpr const char* variation = "variation";
inline constexpr const char* autoFill = "auto_fill";
inline constexpr const char* fillSlot = "fill_slot";
inline constexpr const char* chain = "chain";
inline constexpr const char* record = "record";
inline constexpr const char* velocitySens = "velocity_sens";
inline constexpr const char* bassChannel = "bass_channel";
inline constexpr const char* midiOut = "midi_out";

inline constexpr int kNumGlobalParams = 21;

// In host order.
inline constexpr std::array<const char*, kNumGlobalParams> kGlobalIds{{
    masterVolume, accent, analogOut, masterDrive, masterComp, limiter, quality, variance,
    seqMode, seqRun, tempo, shuffleDepth, flamTime, variation, autoFill, fillSlot, chain, record,
    velocitySens, bassChannel, midiOut,
}};

// --- per voice ---------------------------------------------------------------
enum class VoiceParam : int { Level = 0, Tune, Decay, Tone, Shape, Pan, Drive, Mute, Solo, Out, kCount };

inline constexpr int kNumVoiceParamKinds = static_cast<int>(VoiceParam::kCount);

inline constexpr std::array<std::string_view, kNumVoiceParamKinds> kVoiceSuffix{{
    "level", "tune", "decay", "tone", "shape", "pan", "drive", "mute", "solo", "out",
}};

// Tone and shape exist only for the voices that name them in kVoiceInfo.
constexpr bool voiceHas(int voice, VoiceParam p) noexcept {
    if (voice < 0 || voice >= kNumVoices) return false;
    const auto& info = kVoiceInfo[static_cast<std::size_t>(voice)];
    if (p == VoiceParam::Tone) return !info.toneName.empty();
    if (p == VoiceParam::Shape) return !info.shapeName.empty();
    return p != VoiceParam::kCount;
}

// "bd_level", "cy_tone", ... (built whether or not the voice has the parameter;
// check voiceHas first).
inline std::string voice(int voiceIndex, VoiceParam p) {
    const auto& info = kVoiceInfo[static_cast<std::size_t>(voiceIndex)];
    std::string id(info.id);
    id += '_';
    id += kVoiceSuffix[static_cast<std::size_t>(p)];
    return id;
}

// Number of parameters the layout holds.
constexpr int totalCount() noexcept {
    int n = kNumGlobalParams;
    for (int v = 0; v < kNumVoices; ++v)
        for (int k = 0; k < kNumVoiceParamKinds; ++k)
            if (voiceHas(v, static_cast<VoiceParam>(k))) ++n;
    return n;
}

inline constexpr int kNumOutputChoices = 12;  // Main, Out 2 .. Out 12

}  // namespace mw808::plugin::paramid

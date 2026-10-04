// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The voice roster and the per-voice contract shared by the DSP voices, the
// engine, the sequencer and the plugin shell. This header is the single source
// of truth for voice order: pattern tracks, parameter blocks, MIDI maps and
// output routing all index by VoiceId.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mw808 {

// Panel order of the original machine, with the switch-paired voices
// (tom/conga, rim/claves, clap/maracas) split into independent voices.
enum VoiceId : std::uint8_t {
    BD = 0,  // bass drum
    SD,      // snare drum
    LT,      // low tom
    MT,      // mid tom
    HT,      // hi tom
    LC,      // low conga
    MC,      // mid conga
    HC,      // hi conga
    RS,      // rim shot
    CL,      // claves
    CP,      // hand clap
    MA,      // maracas
    CB,      // cowbell
    CY,      // cymbal
    OH,      // open hi-hat
    CH,      // closed hi-hat
    kNumVoices
};

// One trigger of a voice circuit.
//
// The original machine fires every voice with a ~1 ms "common trigger" pulse
// whose HEIGHT carries the dynamics: about 4 V for a normal step and 4..14 V
// for an accented step, set by the ACCENT knob. Voices are hit harder, not
// just turned up, so accent changes timbre as well as level. `volts` is that
// pulse height. Values below 4 V (weak steps, low MIDI velocity) must still
// behave gracefully: monotonic level, no discontinuities.
struct Hit {
    float volts = 4.0f;       // trigger pulse height, 0 < volts <= 14
    float pitchSemis = 0.0f;  // per-hit pitch offset (chromatic BD); 0 for sequencer hits

    static constexpr float kNormalVolts = 4.0f;
    static constexpr float kMaxVolts = 14.0f;
    static constexpr float kWeakVolts = 2.2f;

    // accentKnob: 0..1 position of the ACCENT control.
    static constexpr Hit normal() noexcept { return {kNormalVolts, 0.0f}; }
    static constexpr Hit weak() noexcept { return {kWeakVolts, 0.0f}; }
    static constexpr Hit accented(float accentKnob) noexcept {
        return {kNormalVolts + (kMaxVolts - kNormalVolts) * accentKnob, 0.0f};
    }
};

// Sound-shaping controls of one voice. Every voice gets all four; which ones
// it honours is listed in kVoiceInfo. Normalised 0..1 unless noted. The mapping
// from knob position to circuit behaviour lives inside each voice.
//
// Rules every voice follows:
//  * Where the original panel has the knob (BD tone/decay, SD tone/snappy,
//    CY tone/decay, OH decay), 0..1 spans the hardware pot travel.
//  * Where the original has no such knob, 0.5 reproduces the stock circuit
//    exactly and the travel extends both ways (decay roughly x0.25 .. x4).
//  * tune = 0 is the stock pitch (for toms/congas: the centre of the hardware
//    TUNING pot). It scales every pitched element of the voice.
struct VoiceParams {
    float tune = 0.0f;    // semitones, -12 .. +12
    float decay = 0.5f;   // 0..1
    float tone = 0.5f;    // 0..1  (BD tone, SD tone, CP tone, CY tone)
    float shape = 0.5f;   // 0..1  (BD sweep, SD snappy, tom noise, CP reverb)
};

// Fixed at prepare time. Voices must not allocate: prepare() may be called on
// the audio thread when quality or the unit seed changes.
struct VoiceSetup {
    double sampleRate = 48000.0;  // rate of the buffers passed to render()
    int oversample = 1;           // internal oversampling the voice may use (1, 2 or 4)
    std::uint32_t unitSeed = 0;   // component-tolerance seed ("serial number")
    float variance = 0.0f;        // 0 = ideal nominal parts, 1 = full +-20% C / +-5% R spread
};

struct VoiceInfo {
    std::string_view code;       // "BD"
    std::string_view name;       // "Bass Drum"
    std::string_view id;         // "bd" — parameter-id prefix, lowercase
    std::string_view toneName;   // label of `tone`, empty if unused
    std::string_view shapeName;  // label of `shape`, empty if unused
    VoiceParams defaults;        // stock sound
    bool panelTune;              // original panel has a tuning knob
    bool panelDecay;             // original panel has a decay knob
};

inline constexpr std::array<VoiceInfo, kNumVoices> kVoiceInfo{{
    {"BD", "Bass Drum",     "bd", "Tone", "Sweep",  {0.0f, 0.5f, 0.5f, 0.5f}, false, true},
    {"SD", "Snare Drum",    "sd", "Tone", "Snappy", {0.0f, 0.5f, 0.5f, 0.5f}, false, false},
    {"LT", "Low Tom",       "lt", "",     "Noise",  {0.0f, 0.5f, 0.5f, 0.5f}, true,  false},
    {"MT", "Mid Tom",       "mt", "",     "Noise",  {0.0f, 0.5f, 0.5f, 0.5f}, true,  false},
    {"HT", "Hi Tom",        "ht", "",     "Noise",  {0.0f, 0.5f, 0.5f, 0.5f}, true,  false},
    {"LC", "Low Conga",     "lc", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, true,  false},
    {"MC", "Mid Conga",     "mc", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, true,  false},
    {"HC", "Hi Conga",      "hc", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, true,  false},
    {"RS", "Rim Shot",      "rs", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, false, false},
    {"CL", "Claves",        "cl", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, false, false},
    {"CP", "Hand Clap",     "cp", "Tone", "Reverb", {0.0f, 0.5f, 0.5f, 0.5f}, false, false},
    {"MA", "Maracas",       "ma", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, false, false},
    {"CB", "Cowbell",       "cb", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, false, false},
    {"CY", "Cymbal",        "cy", "Tone", "",       {0.0f, 0.5f, 0.5f, 0.5f}, false, true},
    {"OH", "Open Hi-Hat",   "oh", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, false, true},
    {"CH", "Closed Hi-Hat", "ch", "",     "",       {0.0f, 0.5f, 0.5f, 0.5f}, false, false},
}};

// Voice output scale: 1.0 == 5 V peak (10 Vpp) at the voice's own output with
// its LEVEL pot at maximum. The service-manual check table (normal vs accented
// Vpp per voice) is the calibration target, e.g. BD 3.5 Vpp normal -> 0.35 peak,
// 10 Vpp accented -> 1.0 peak.
inline constexpr float kVoltsPerUnit = 5.0f;

}  // namespace mw808

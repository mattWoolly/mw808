// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The APVTS parameter layout (docs/design/plugin.md §2) and the bridge that
// turns the cached parameter atomics into one mw808::MachineParams per block.

#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterIDs.h"
#include "mw808/Factory.h"
#include "mw808/Machine.h"

namespace mw808::plugin {

// A bool parameter whose host-visible value is always exactly 0 or 1.
//
// The stock AudioParameterBool keeps whatever normalised value the host poked
// (0.37 reads back as 0.37 although the parameter is "off"), so a host that
// saves, pokes and restores sees a different number come back. Reporting the
// snapped value keeps state save/restore exact from the host's point of view.
class BoolParameter final : public juce::AudioParameterBool {
public:
    using juce::AudioParameterBool::AudioParameterBool;

private:
    float getValue() const override { return get() ? 1.0f : 0.0f; }
};

// A range whose normalise / denormalise pair round-trips every value the
// parameter can hold (computed in double; `step` > 0 snaps to that decimal
// grid). State restore relies on it: a saved value loads back bit for bit.
juce::NormalisableRange<float> makeExactRange(float low, float high, double step = 0.0);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Every parameter id, in host (layout) order. This is also the canonical order
// of the serialised state.
const std::vector<std::string>& allParameterIds();

// The parameters a kit preset carries: per-voice sound and mixer settings
// (level, tune, decay, tone, shape, pan, drive, output) plus accent and the
// master section. Mute / solo and every sequencer / MIDI setting stay out.
const std::vector<std::string>& kitParameterIds();

// Cached raw-value atomics of every parameter. Construction (message thread)
// looks them up once; snapshot() is plain relaxed loads and safe on the audio
// thread.
class Parameters {
public:
    explicit Parameters(juce::AudioProcessorValueTreeState& apvts);

    // Fills everything except MachineParams::unitSeed (not a parameter).
    void snapshot(MachineParams& out) const noexcept;

    // Parameter values of a factory kit as (id, real-world value) pairs.
    static std::vector<std::pair<std::string, float>> kitValues(const factory::Kit& kit);

private:
    struct VoiceRefs {
        std::atomic<float>* level = nullptr;
        std::atomic<float>* tune = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* tone = nullptr;   // null when the voice has no tone control
        std::atomic<float>* shape = nullptr;  // null when the voice has no shape control
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* drive = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* out = nullptr;
    };

    std::array<VoiceRefs, kNumVoices> voices_{};
    std::atomic<float>* masterVolume_ = nullptr;
    std::atomic<float>* accent_ = nullptr;
    std::atomic<float>* analogOut_ = nullptr;
    std::atomic<float>* masterDrive_ = nullptr;
    std::atomic<float>* masterComp_ = nullptr;
    std::atomic<float>* limiter_ = nullptr;
    std::atomic<float>* quality_ = nullptr;
    std::atomic<float>* variance_ = nullptr;
    std::atomic<float>* seqMode_ = nullptr;
    std::atomic<float>* seqRun_ = nullptr;
    std::atomic<float>* tempo_ = nullptr;
    std::atomic<float>* shuffleDepth_ = nullptr;
    std::atomic<float>* flamTime_ = nullptr;
    std::atomic<float>* variation_ = nullptr;
    std::atomic<float>* autoFill_ = nullptr;
    std::atomic<float>* fillSlot_ = nullptr;
    std::atomic<float>* chain_ = nullptr;
    std::atomic<float>* record_ = nullptr;
    std::atomic<float>* velocitySens_ = nullptr;
    std::atomic<float>* bassChannel_ = nullptr;
    std::atomic<float>* midiOut_ = nullptr;
};

}  // namespace mw808::plugin

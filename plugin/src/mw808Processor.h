// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The plugin shell around mw808::Machine (docs/design/plugin.md §1-§6).
//
// Threads, and why each crossing is safe
//
//   audio thread     processBlock only. It reads the cached parameter atomics,
//                    the unit seed atomic and the host play head, drives the
//                    Machine, and forwards controller messages into a lock-free
//                    FIFO. No allocation, no blocking lock, no ValueTree, no
//                    parameter writes.
//   message thread   the editor, the 30 Hz service timer (running whether or
//                    not an editor is open), every listener callback.
//   host threads     get/setStateInformation, setCurrentProgram, prepareToPlay,
//                    releaseResources and reset may arrive on any thread:
//                      * state: parameters go through APVTS::replaceState (its
//                        own lock); pattern data through PatternModel, which
//                        hands the bank to the audio thread at once and lets
//                        the message thread adopt it into the model;
//                      * program changes off the message thread (the VST3
//                        wrapper delivers them on the audio thread) are only
//                        recorded in an atomic and applied by the timer;
//                      * prepareToPlay takes the process gate, a flag the audio
//                        thread only ever TRIES: a block that collides with a
//                        prepare outputs silence instead of touching buffers
//                        that are being resized;
//                      * reset() only raises a flag the next block honours.

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#if defined(HAS_CLAP_JUCE_EXTENSIONS)
 #include <clap-juce-extensions/clap-juce-extensions.h>
#endif

#include "MidiLearn.h"
#include "Parameters.h"
#include "PresetManager.h"
#include "mw808/Machine.h"
#include "state/PatternModel.h"
#include "state/StateCodec.h"

class mw808Processor final : public juce::AudioProcessor, private juce::Timer {
public:
    using PatternModel = mw808::plugin::PatternModel;
    using PresetManager = mw808::plugin::PresetManager;
    using MidiLearn = mw808::plugin::MidiLearn;
    using UiState = mw808::plugin::UiState;
    using PluginState = mw808::plugin::state::PluginState;

    mw808Processor();
    ~mw808Processor() override;

    // --- juce::AudioProcessor ------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;  // the double-precision overload stays visible (unsupported)
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    // Host programs are the factory full presets.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- the editor's API (plugin.md §6) -------------------------------------
    juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }
    PatternModel& patterns() noexcept { return patterns_; }
    PresetManager& presets() noexcept { return presets_; }
    const mw808::PlayState& playState() const noexcept { return machine_->playState(); }
    void audition(mw808::VoiceId voice, bool accent);  // Command::Audition
    void requestFill();
    void panic();
    MidiLearn& midiLearn() noexcept { return midiLearn_; }
    // Writes one measure of the pattern to a temp .mid file (for drag-and-drop)
    // and returns it; an invalid File on failure.
    juce::File exportPatternToMidiFile(int patternIndex, int variation);
    bool importMidiFile(const juce::File& file, int patternIndex);
    bool importMidiFile(const juce::File& file, int patternIndex, int variation);

    // UI state kept in the session (scale, view, selected track, step-edit mode, page).
    UiState uiState() const { return patterns_.uiState(); }
    void setUiState(const UiState& ui) { patterns_.setUiState(ui); }
    // The unit seed ("serial number") behind Analog Variance.
    std::uint32_t unitSeed() const noexcept { return patterns_.unitSeed(); }
    void setUnitSeed(std::uint32_t seed) { patterns_.setUnitSeed(seed); }
    // How the last setStateInformation went (Ok, or why it fell back to defaults).
    mw808::plugin::state::ReadStatus lastStateLoadStatus() const noexcept { return lastLoadStatus_.load(); }

    // --- shell internals (preset manager, tests, offline drivers) ----------------
    // Everything the 30 Hz timer does, callable directly where no message loop
    // runs. Message thread only.
    void serviceMessageThread();
    // The parameter snapshot processBlock takes (realtime-safe).
    void makeSnapshot(mw808::MachineParams& out) const noexcept;
    // The complete state as plain data. Any non-audio thread.
    std::unique_ptr<PluginState> captureState() const;
    // Replace the state. `sessionExtras` also restores the UI state and the
    // MIDI-learn map (a session load does, a preset load does not). Any non-audio thread.
    void applyState(const PluginState& state, bool sessionExtras);
    // The state of a brand-new instance: factory preset 0.
    std::unique_ptr<PluginState> makeDefaultState() const;
    // Set parameters from real-world values; unknown ids are skipped. With
    // gestures each change is wrapped in begin/endChangeGesture. Message thread.
    void setParameterValues(const std::vector<std::pair<juce::String, float>>& values, bool withGestures);
    // Called by the preset manager when a factory full preset was loaded.
    void factoryProgramLoaded(int index);
    void setMidiExportDirectory(const juce::File& directory) { midiExportDir_ = directory; }
    // The Machine itself, for tests that compare the audio-side pattern memory
    // with the model. Not thread-safe: only while no audio thread is running.
    mw808::Machine& machineForTests() noexcept { return *machine_; }

    static constexpr int kServiceHz = 30;
    static constexpr int kMaxMidiIn = 2048;         // note messages taken per block
    static constexpr int kMaxMidiOutNoteOns = 100;  // per block; see processBlock
    // The most a block writes into the host's MIDI buffer: a note-off for each
    // voice note sounding at its start, then kMaxMidiOutNoteOns note-ons and a
    // note-off for each (a MidiBuffer event is 4 + 2 + 3 bytes). The VST3 and
    // CLAP wrappers reserve exactly 2048 bytes.
    static constexpr std::size_t kMidiOutReserveBytes = 2048;
    static_assert((mw808::kNumVoices + 2 * kMaxMidiOutNoteOns) * (4 + 2 + 3) <= kMidiOutReserveBytes);

private:
    static BusesProperties makeBuses();
    bool isClap() const noexcept;
    void timerCallback() override;
    mw808::seq::Transport readTransport(double fallbackBpm) const noexcept;
    // Audio thread: note-offs for every note MIDI Out left sounding.
    void releaseMidiOut(juce::MidiBuffer& midi, int sample) noexcept;
    void applyPendingProgram();

    juce::AudioProcessorValueTreeState apvts_;
    mw808::plugin::Parameters params_;
    std::vector<juce::RangedAudioParameter*> orderedParams_;  // layout order
    std::vector<std::atomic<float>*> orderedRaw_;             // same order

    std::unique_ptr<mw808::Machine> machine_;
    PatternModel patterns_;
    MidiLearn midiLearn_;
    PresetManager presets_;

    // Process gate: the audio thread tries it, prepareToPlay holds it.
    std::atomic<bool> processGate_{false};
    std::atomic<bool> prepared_{false};
    std::atomic<bool> resetPending_{false};

    std::atomic<int> currentProgram_{0};
    std::atomic<int> pendingProgram_{-1};
    std::atomic<bool> programNotifyPending_{false};
    std::atomic<bool> panicPending_{false};
    std::atomic<mw808::plugin::state::ReadStatus> lastLoadStatus_{mw808::plugin::state::ReadStatus::Ok};

    // Audio-thread scratch (fixed size).
    std::array<mw808::MidiMessage, kMaxMidiIn> midiIn_{};
    mw808::MidiOutBuffer midiOut_{};
    std::array<bool, 128> midiOutSounding_{};          // notes the host was sent and not yet released
    std::atomic<bool> midiOutFlushPending_{false};

    juce::File midiExportDir_;

#if defined(HAS_CLAP_JUCE_EXTENSIONS)
    // Constructed with the processor, which is when the CLAP wrapper marks the instance as CLAP.
    clap_juce_extensions::clap_properties clapProperties_;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(mw808Processor)
};

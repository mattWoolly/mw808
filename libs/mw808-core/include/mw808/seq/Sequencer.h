// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Sample-accurate step sequencer. Pure logic: it turns a transport description
// into timed voice hits and never touches audio. Every method here is called
// on the audio thread and must not allocate, lock or block.
//
// The PUBLIC interface below is a contract with mw808::Machine and the tests;
// private members are free to change. Full semantics: docs/design/architecture.md §5.

#pragma once

#include <array>
#include <cstdint>

#include "mw808/Voices.h"
#include "mw808/seq/Pattern.h"

namespace mw808::seq {

enum class Mode : std::uint8_t {
    Off = 0,    // sequencer silent; voices are played from MIDI only
    HostSync,   // runs while the host transport rolls, locked to host PPQ
    Internal,   // free-running on its own clock, started by Settings::run
    HostNote    // a held MIDI "pattern note" plays that pattern from its top
};

// Host transport at the FIRST sample of the range passed to process().
struct Transport {
    bool hostValid = false;  // host supplied tempo/position
    bool playing = false;    // host transport rolling
    double ppq = 0.0;        // position in quarter notes
    double bpm = 120.0;      // host tempo
};

// Per-block settings (from plugin parameters).
struct Settings {
    Mode mode = Mode::HostSync;
    bool run = false;                  // Internal mode start/stop
    double internalBpm = 120.0;        // used when the host gives no tempo
    float accent = 0.7f;               // ACCENT knob, 0..1
    float flamMs = 24.0f;              // grace-to-main spacing
    float swingScale = 1.0f;           // global multiplier on each pattern's shuffle, 0..1
    VariationMode variation = VariationMode::A;
    AutoFill autoFill = AutoFill::Manual;
    std::uint8_t fillSlot = 0;         // 0..3 -> intro/fill pattern 13..16 of the playing bank
    bool chain = false;                // follow PatternBank::chain instead of looping one pattern
    bool record = false;               // recordHit() writes into the playing pattern
};

struct Event {
    std::int32_t offset = 0;   // sample offset inside the processed range
    VoiceId voice = BD;
    StepLevel level = StepLevel::Normal;  // dynamic class after global accent (for MIDI out)
    Hit hit{};
};

struct EventBuffer {
    static constexpr int kCapacity = 1024;
    std::array<Event, kCapacity> events{};
    int count = 0;

    void clear() noexcept { count = 0; }
    bool push(const Event& e) noexcept {
        if (count >= kCapacity) return false;
        events[static_cast<std::size_t>(count++)] = e;
        return true;
    }
};

struct RecordResult {
    bool changed = false;
    std::uint8_t pattern = 0, variation = 0, track = 0, step = 0;
    Step cell{};
};

class Sequencer {
public:
    Sequencer() noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;  // stop, clear pending hits, rewind; keeps the bank

    // --- pattern memory (audio-thread copy) --------------------------------
    const PatternBank& bank() const noexcept { return bank_; }
    void setPattern(int index, const Pattern& p) noexcept;
    void setChain(const Chain& c) noexcept;

    // --- performance controls ----------------------------------------------
    // Queue a pattern; it takes over at the end of the current measure
    // (immediately when stopped, or when `immediate`, keeping the step position).
    void selectPattern(int index, bool immediate = false) noexcept;
    // Play the selected intro/fill pattern for the next measure, once.
    void requestFill() noexcept;
    // HostNote mode: a pattern note went down / the last one came up.
    void notePatternOn(int index) noexcept;
    void notePatternOff() noexcept;
    // Live recording: quantise a played hit, `sampleOffset` samples into the
    // NEXT process() range, to the nearest step of the playing pattern. Has no
    // effect unless playing with Settings::record set on the previous process().
    RecordResult recordHit(VoiceId voice, bool accent, int sampleOffset) noexcept;

    // --- the clock ----------------------------------------------------------
    // Advance by numSamples and append the hits that fall inside the range to
    // `out` (sorted by offset; offsets are relative to the range start plus
    // `offsetBase`). May be called on consecutive sub-ranges of a host block.
    void process(const Transport& transport, const Settings& settings, int numSamples,
                 EventBuffer& out, int offsetBase = 0) noexcept;

    // --- state for the UI (read on the audio thread, published by Machine) ---
    bool isPlaying() const noexcept { return playing_; }
    int currentPattern() const noexcept { return currentPattern_; }   // the one sounding now (may be a fill)
    int selectedPattern() const noexcept { return selectedPattern_; } // the basic pattern looped/edited
    int queuedPattern() const noexcept { return queuedPattern_; }     // -1 if none
    int currentVariation() const noexcept { return currentVariation_; }
    int currentStep() const noexcept { return currentStep_; }         // 0-based within the measure, -1 when stopped
    std::int64_t measureCount() const noexcept { return measure_; }
    double tempo() const noexcept { return tempo_; }                  // effective BPM in use

private:
    // The implementation owns everything below and may restructure it freely.
    //
    // Timing model (see Sequencer.cpp for the full story): every hit carries
    // an exact musical time in quarter notes plus a tempo-independent delay in
    // samples (the flam). Inside a process() range starting at ppq0 the hit
    // lands on sample floor((ppq - ppq0) * spp + extra + 0.5 + eps). Steps are
    // walked with a cursor (measure start + step index), so each step is
    // decided exactly once whatever the block partition.
    struct Pending {
        double ppq = 0.0;            // musical time (step + shuffle + sub-step)
        double extraSamples = 0.0;   // flam delay on top, in samples
        std::uint64_t order = 0;     // generation order: tie-break at equal offsets
        Event event{};
    };
    struct Scratch {
        std::int64_t offset = 0;
        std::uint64_t order = 0;
        Event event{};
    };
    static constexpr int kMaxPending = 512;
    static constexpr int kMaxScratch = EventBuffer::kCapacity;

    void stopPlayback() noexcept;
    void dropPending() noexcept;
    void clearSuppress() noexcept;
    bool takeSuppressed(int voice, std::int64_t measure, int step) noexcept;
    void locate(double ppq, double spp, bool allowIntro) noexcept;
    void startMeasure(std::int64_t measure, double startPpq, int pattern, bool isFill) noexcept;
    void advanceMeasure(double startPpq) noexcept;
    void fireStep(double stepPpq, double ppq0, double spp, int numSamples) noexcept;
    void emit(double ppq, double extraSamples, const Event& e, double ppq0, double spp,
              int numSamples) noexcept;
    void pushScratch(std::int64_t offset, std::uint64_t order, const Event& e) noexcept;
    int basePatternFor(std::int64_t measure) const noexcept;
    int fillPatternFor(int basePattern) const noexcept;
    bool autoFillAt(std::int64_t measure) const noexcept;
    bool chainActive() const noexcept;
    int variationFor(std::int64_t measure) const noexcept;
    double measureLengthQn(int pattern) const noexcept;

    PatternBank bank_{};
    double sampleRate_ = 48000.0;

    // --- UI-visible state ---
    bool playing_ = false;
    int currentPattern_ = 0;
    int selectedPattern_ = 0;
    int queuedPattern_ = -1;
    int currentVariation_ = 0;
    int currentStep_ = -1;
    std::int64_t measure_ = 0;
    double tempo_ = 120.0;

    // --- settings latched from the latest process() call ---
    Settings settings_{};
    Mode activeMode_ = Mode::Off;     // the mode playback was started in

    // --- clock ---
    double prevPpq0_ = 0.0;           // range start of the previous process() call
    double prevSpp_ = 24000.0;        // samples per quarter note in that call
    int prevN_ = 0;                   // its length in samples
    bool prevRecord_ = false;         // Settings::record in that call
    double segPpq_ = 0.0;             // internal clock: PPQ at the last tempo change
    std::int64_t segSamples_ = 0;     //   samples since then
    double segSpp_ = 24000.0;         //   samples per quarter note since then

    // --- measure cursor ---
    double measureStart_ = 0.0;       // PPQ of step 0 of the current measure
    double stepQn_ = 0.25;            // step length of the current measure (latched)
    int measureLen_ = 16;             // steps in the current measure (latched)
    int nextStep_ = 0;                // next step to fire; == measureLen_ at the measure end

    // --- performance requests ---
    bool fillRequested_ = false;
    bool noteHeld_ = false;
    bool noteRestart_ = false;
    int notePattern_ = 0;

    // Recorded hits that already sounded live: skip that step once. Several
    // per voice, because one process() range can hold more than one upcoming
    // step of a voice (large blocks, fast tempo, rolls played live).
    static constexpr int kSuppressSlots = 8;
    std::array<std::array<std::int64_t, kSuppressSlots>, kNumVoices> suppressMeasure_{};
    std::array<std::array<int, kSuppressSlots>, kNumVoices> suppressStep_{};

    std::uint64_t order_ = 0;

    std::array<Pending, kMaxPending> pending_{};
    int numPending_ = 0;
    std::array<Scratch, kMaxScratch> scratch_{};
    int numScratch_ = 0;
};

}  // namespace mw808::seq

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The whole instrument behind one realtime entry point: the sound engine, the
// step sequencer, MIDI in/out handling and the lock-free links to a UI thread.
// The plugin shell and the CLI renderer both drive a Machine and nothing below it.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "mw808/Engine.h"
#include "mw808/MidiMap.h"
#include "mw808/Voices.h"
#include "mw808/seq/Pattern.h"
#include "mw808/seq/Sequencer.h"
#include "mw808/util/SpscQueue.h"

namespace mw808 {

// A channel-voice MIDI message with its sample offset in the block.
struct MidiMessage {
    std::int32_t offset = 0;
    std::uint8_t status = 0, data1 = 0, data2 = 0;
};

struct MidiOutBuffer {
    static constexpr int kCapacity = 1024;
    std::array<MidiMessage, kCapacity> messages{};
    int count = 0;

    void clear() noexcept { count = 0; }
    bool push(const MidiMessage& m) noexcept {
        if (count >= kCapacity) return false;
        messages[static_cast<std::size_t>(count++)] = m;
        return true;
    }
};

struct MidiSettings {
    bool velocitySensitive = true;  // continuous velocity vs three fixed classes
    std::uint8_t bassChannel = 0;   // 0 = off; 1..16: notes on this channel play BD chromatically
    bool midiOut = false;           // send sequencer hits as notes on channel 10
};

// Everything the audio thread needs for one block, as plain values.
struct MachineParams {
    EngineParams engine{};
    seq::Settings seq{};
    MidiSettings midi{};
    std::array<bool, kNumVoices> mute{};
    std::array<bool, kNumVoices> solo{};
    Quality quality = Quality::Standard;
    std::uint32_t unitSeed = 0;
    float variance = 0.0f;
};

// UI -> audio.
struct Command {
    enum class Type : std::uint8_t {
        SetPattern,     // index, pattern
        SetChain,       // chain
        SelectPattern,  // index; flag = take over immediately
        Fill,           // play the fill pattern for the next measure
        Audition,       // index = voice, value = trigger volts
        Panic           // silence all voices, drop pending hits
    };
    Type type = Type::Panic;
    bool flag = false;
    std::int32_t index = 0;
    float value = 0.0f;
    std::uint32_t generation = 0;  // SetPattern / SetChain: BankHandoff generation the edit was made against
    seq::Chain chain{};
    seq::Pattern pattern{};
};

// A complete pattern memory handed to the audio thread in one piece (state
// restore, preset load). `generation` orders it against queued single edits:
// SetPattern / SetChain commands stamped with an older generation are stale
// and ignored once this bank is in place.
struct BankHandoff {
    seq::PatternBank bank{};
    std::uint32_t generation = 0;
};

// Audio -> UI.
struct Notice {
    enum class Type : std::uint8_t {
        StepRecorded,    // pattern / variation / track / step now hold `cell`
        PatternSelected  // pattern = newly selected basic pattern (changed on the audio side)
    };
    Type type = Type::StepRecorded;
    std::uint8_t pattern = 0, variation = 0, track = 0, step = 0;
    seq::Step cell{};
};

// Play position and activity, published every block for the UI to poll.
struct PlayState {
    std::atomic<bool> playing{false};
    std::atomic<int> currentPattern{0};
    std::atomic<int> selectedPattern{0};
    std::atomic<int> queuedPattern{-1};
    std::atomic<int> variation{0};
    std::atomic<int> step{-1};
    std::atomic<std::int64_t> measure{0};                        // pass count of the sounding pattern (polymeter playheads)
    std::atomic<float> tempo{120.0f};
    std::atomic<float> peakLeft{0.0f}, peakRight{0.0f};          // main-bus block peak
    std::array<std::atomic<std::uint32_t>, kNumVoices> hits{};   // bumps on every trigger
};

class Machine {
public:
    Machine() = default;
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    struct ProcessArgs {
        seq::Transport transport{};         // at the first sample of the block
        const MidiMessage* midiIn = nullptr;  // sorted by offset
        int numMidiIn = 0;
        MidiOutBuffer* midiOut = nullptr;   // optional
        BusBuffers out{};
        int numSamples = 0;
    };

    // Non-realtime: allocates the engine's scratch buffers.
    void prepare(double sampleRate, int maxBlockSize);
    // Realtime-safe: silence and rewind, keeps pattern memory.
    void reset() noexcept;

    // The realtime entry point.
    void process(const ProcessArgs& args, const MachineParams& params) noexcept;

    // --- UI thread (single producer / single consumer each way) -------------
    bool post(const Command& command) noexcept { return commands_.push(command); }
    bool poll(Notice& notice) noexcept { return notices_.pop(notice); }
    const PlayState& playState() const noexcept { return state_; }

    // Replace the whole pattern memory. Takes ownership of a heap-allocated
    // handoff; the very next process() call sees it, however many are posted.
    // Returns a handoff the caller must delete (a superseded one), or nullptr.
    [[nodiscard]] BankHandoff* postBank(BankHandoff* handoff) noexcept {
        return pendingBank_.exchange(handoff, std::memory_order_acq_rel);
    }
    // Handoffs the audio thread has finished with. Call until it returns
    // nullptr and delete each one (message thread).
    [[nodiscard]] BankHandoff* takeRetiredBank() noexcept {
        BankHandoff* h = nullptr;
        return retiredBanks_.pop(h) ? h : nullptr;
    }

    // --- direct access for offline use (renderer, tests): not thread-safe ----
    seq::Sequencer& sequencer() noexcept { return sequencer_; }
    Engine& engine() noexcept { return engine_; }

private:
    struct TimedHit {
        std::int32_t offset = 0;  // within the current chunk
        VoiceId voice = BD;
        seq::StepLevel level = seq::StepLevel::Normal;
        Hit hit{};
        bool fromSequencer = false;
        bool force = false;       // ignores mute/solo (auditions)
    };
    static constexpr int kMaxLiveHits = 512;
    static constexpr int kMaxAuditions = 32;

    void adoptPendingBank() noexcept;
    void drainCommands() noexcept;
    void processChunk(const ProcessArgs& args, const MachineParams& params, int chunkStart, int chunkLen,
                      int& midiIndex) noexcept;
    void fire(const TimedHit& h, const ProcessArgs& args, const MachineParams& params, int chunkStart,
              int& cursor) noexcept;
    void flushNoteOffs(MidiOutBuffer* out, int chunkStart, int chunkLen) noexcept;
    void publish(const ProcessArgs& args) noexcept;
    seq::Transport transportAt(const seq::Transport& t, int offset) const noexcept;

    Engine engine_;
    seq::Sequencer sequencer_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;

    seq::EventBuffer seqEvents_{};
    std::array<TimedHit, kMaxLiveHits> liveHits_{};  // MIDI-played hits of the current chunk, sorted
    int numLiveHits_ = 0;
    std::array<TimedHit, kMaxAuditions> auditions_{};
    int numAuditions_ = 0;

    int activePatternNote_ = -1;
    int lastSelectedPattern_ = 0;

    // MIDI out: samples from the current chunk start until each voice's
    // note-off is due (-1 = no note sounding).
    std::array<std::int32_t, kNumVoices> noteOffAt_{};
    std::int32_t noteLengthSamples_ = 960;

    std::atomic<BankHandoff*> pendingBank_{nullptr};
    util::SpscQueue<BankHandoff*, 64> retiredBanks_;
    std::uint32_t bankGeneration_ = 0;

    util::SpscQueue<Command, 128> commands_;
    util::SpscQueue<Notice, 512> notices_;
    PlayState state_;
};

}  // namespace mw808

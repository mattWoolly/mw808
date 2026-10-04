// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The message-thread owner of everything that is state but not a parameter
// (docs/design/plugin.md §3): the authoritative pattern bank and chain, the
// selected pattern, the unit seed and the editor's UI state. It is also the
// ONLY user of the UI side of mw808::Machine's cross-thread links, so their
// single-producer / single-consumer contracts hold however many threads call in.
//
// Threads
//   message thread  everything, unless noted. Listener callbacks happen here.
//   any non-audio   capture() / restore() / postCommand() / collectRetired():
//                   hosts call get/setStateInformation from worker threads.
//   audio thread    unitSeedForAudio() only (one relaxed atomic load).
//
// How a state restore from a foreign thread stays race-free: the model's own
// data is written by the message thread only. A foreign restore() hands the
// new bank straight to the audio thread (so an offline bounce right after the
// load plays it) and parks a copy in `staged_`; the message thread adopts it
// on its next service() or mutator call and then tells the listeners. Until
// then capture() serves the staged copy, so a save right after a load is exact.
//
// Delivery guarantees
//   * A whole bank goes through Machine::postBank with a fresh generation: the
//     very next audio block uses it, with no timer or message loop involved.
//   * Single edits go through the command queue stamped with the current
//     generation. When the queue is full the item is marked dirty and re-posted
//     from service(); nothing is lost.
//   * Notices from the audio thread (recorded steps, pattern changes) are
//     merged into the model in service(). The cases where a notice and a
//     message-thread change cross are resolved in favour of the model, which
//     is then re-sent, so both sides converge on the same data.

#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <memory>
#include <mutex>

#include <juce_core/juce_core.h>

#include "mw808/Machine.h"
#include "mw808/seq/Pattern.h"

namespace mw808::plugin {

// Editor state kept in the session. Plain values; the model never interprets
// them beyond clamping to the documented ranges.
struct UiState {
    float scale = 1.0f;      // editor scale factor, 0.6 .. 2.0
    int view = 0;            // 0 PANEL, 1 GRID, 2 MIXER
    int selectedTrack = 0;   // 0..15 voices, 16 accent
    int stepEditMode = 0;    // 0 LEVEL, 1 FLAM, 2 SUB, 3 PROB
    int page = 0;            // 0..3
    std::array<juce::String, 3> presetNames{};  // last loaded/saved preset per kind (kit, patterns, full)

    bool operator==(const UiState&) const = default;
};

inline constexpr float kMinUiScale = 0.6f;
inline constexpr float kMaxUiScale = 2.0f;
inline constexpr std::uint32_t kDefaultUnitSeed = 808;

// Force every field into its legal range (patterns reach the audio thread
// unchecked by the core). A valid pattern is returned unchanged, bit for bit.
void sanitise(seq::Pattern& p) noexcept;
void sanitise(seq::Chain& c) noexcept;
void sanitise(UiState& ui) noexcept;

class PatternModel {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void patternChanged(int /*index*/) {}
        virtual void bankChanged() {}        // every pattern and the chain may have changed
        virtual void selectionChanged() {}
        virtual void chainChanged() {}
    };

    // Everything the model holds, for state save / restore.
    struct Snapshot {
        seq::PatternBank bank{};
        int selectedPattern = 0;
        std::uint32_t unitSeed = kDefaultUnitSeed;
        UiState ui{};
    };

    explicit PatternModel(Machine& machine);
    ~PatternModel();

    // Set the starting content. Only for the owner's constructor, before any
    // other thread can reach the model: applies directly on whatever thread
    // this is and calls no listeners.
    void initialise(std::unique_ptr<Snapshot> snapshot);
    PatternModel(const PatternModel&) = delete;
    PatternModel& operator=(const PatternModel&) = delete;

    // --- message thread ----------------------------------------------------
    const seq::Pattern& pattern(int index) const noexcept;  // index clamped
    const seq::PatternBank& bank() const noexcept { return bank_; }
    const seq::Chain& chain() const noexcept { return bank_.chain; }

    void setPattern(int index, const seq::Pattern& p);  // model + Command::SetPattern to audio
    template <class Fn>
    void edit(int index, Fn&& fn) {                      // fn(Pattern&) then setPattern
        if (index < 0 || index >= seq::kNumPatterns) return;
        seq::Pattern p = pattern(index);
        fn(p);
        setPattern(index, p);
    }
    void setChain(const seq::Chain& c);
    void replaceBank(const seq::PatternBank& bank);     // model + Machine::postBank (generation++)

    int selectedPattern() const noexcept { return selected_; }
    void selectPattern(int index, bool immediate = false);

    std::uint32_t unitSeed() const noexcept { return seed_.load(std::memory_order_relaxed); }
    void setUnitSeed(std::uint32_t seed);

    UiState uiState() const;
    void setUiState(const UiState& ui);

    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    // Runs what the processor's 30 Hz timer runs: adopt a staged restore, merge
    // notices, re-post dirty items, free retired bank handoffs.
    void service();

    // True once after pattern data, the chain or the unit seed changed through
    // an edit, a preset load or live recording (not through restore()): the
    // host should be told the session is dirty.
    bool takeSessionDirty() noexcept { return sessionDirty_.exchange(false, std::memory_order_acq_rel); }

    // --- any non-audio thread ------------------------------------------------
    void capture(Snapshot& out) const;
    // Replace everything. On the message thread the model and the listeners
    // are updated before this returns; elsewhere see the header comment.
    // `keepUi` leaves the current UI state in place (preset loads).
    void restore(std::unique_ptr<Snapshot> snapshot, bool keepUi = false);
    // Audition / Fill / Panic / anything that is not model data. False when the
    // command queue is full (the command is dropped).
    bool postCommand(const Command& command);
    // Frees bank handoffs the audio thread has finished with.
    void collectRetired();

    // --- audio thread ----------------------------------------------------------
    std::uint32_t unitSeedForAudio() const noexcept { return seed_.load(std::memory_order_relaxed); }

    // --- diagnostics (tests) -------------------------------------------------
    int numDirty() const;                    // patterns (+chain, +selection) waiting for a re-post
    std::uint32_t generation() const;        // generation of the bank last handed to the audio thread
    bool hasStagedRestore() const noexcept { return hasStaged_.load(std::memory_order_acquire); }

private:
    enum Change : unsigned { kBank = 1U, kSelection = 2U, kChain = 4U };

    // All *Locked functions expect mutex_ to be held.
    void postBankLocked(const seq::PatternBank& bank);
    void postPatternLocked(int index);
    void postChainLocked();
    void postSelectLocked(int index, bool immediate);
    void collectRetiredLocked();
    unsigned adoptStagedLocked();
    void applyLocked(const Snapshot& snapshot);
    void notify(unsigned changes, const std::bitset<seq::kNumPatterns>& patterns);

    Machine& machine_;
    mutable std::mutex mutex_;

    // Model data: written on the message thread only (under mutex_), read on
    // the message thread without it and elsewhere with it.
    seq::PatternBank bank_{};
    int selected_ = 0;
    UiState ui_{};
    std::atomic<std::uint32_t> seed_{kDefaultUnitSeed};

    // A restore that arrived on another thread, waiting for the message thread.
    std::unique_ptr<Snapshot> staged_;
    std::atomic<bool> hasStaged_{false};

    // Link bookkeeping (under mutex_).
    std::uint32_t generation_ = 0;
    std::bitset<seq::kNumPatterns> dirty_{};
    bool chainDirty_ = false;
    bool selectDirty_ = false;
    bool selectDirtyImmediate_ = false;
    bool awaitingBank_ = false;        // a posted bank has not been seen adopted yet
    bool adoptionObserved_ = false;    // ...its handoff came back retired
    int awaitingSelect_ = -1;          // target of a select command the audio side has not shown yet
    std::uint32_t tick_ = 0;           // service() counter
    std::array<std::uint32_t, seq::kNumPatterns> postedAtTick_{};
    std::bitset<seq::kNumPatterns> postedRecently_{};
    std::atomic<bool> sessionDirty_{false};

    juce::ListenerList<Listener> listeners_;
};

}  // namespace mw808::plugin

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "PatternModel.h"

#include <algorithm>
#include <cmath>

#include <juce_events/juce_events.h>

namespace mw808::plugin {

namespace {

bool onMessageThread() noexcept { return juce::MessageManager::existsAndIsCurrentThread(); }

void sanitiseSnapshot(PatternModel::Snapshot& s) noexcept {
    for (auto& p : s.bank.patterns) mw808::plugin::sanitise(p);
    mw808::plugin::sanitise(s.bank.chain);
    s.selectedPattern = std::clamp(s.selectedPattern, 0, seq::kNumPatterns - 1);
    mw808::plugin::sanitise(s.ui);
}

}  // namespace

void sanitise(seq::Pattern& p) noexcept {
    if (p.length < 1 || p.length > seq::kMaxSteps) p.length = 16;
    if (static_cast<int>(p.scale) >= static_cast<int>(seq::Scale::kNumScales)) p.scale = seq::Scale::Sixteenth;
    if (p.shuffle > 100) p.shuffle = 100;
    for (auto& len : p.trackLength)
        if (len > seq::kMaxSteps) len = 0;
}

void sanitise(seq::Chain& c) noexcept {
    if (c.length > seq::kMaxChainLength) c.length = seq::kMaxChainLength;
    for (auto& e : c.entries)
        if (e >= seq::kNumPatterns) e = 0;
}

void sanitise(UiState& ui) noexcept {
    if (!std::isfinite(ui.scale)) ui.scale = 1.0f;
    ui.scale = std::clamp(ui.scale, kMinUiScale, kMaxUiScale);
    ui.view = std::clamp(ui.view, 0, 2);
    ui.selectedTrack = std::clamp(ui.selectedTrack, 0, seq::kNumTracks - 1);
    ui.stepEditMode = std::clamp(ui.stepEditMode, 0, 3);
    ui.page = std::clamp(ui.page, 0, 3);
    for (auto& name : ui.presetNames)
        if (name.length() > 128) name = name.substring(0, 128);
}

PatternModel::PatternModel(Machine& machine) : machine_(machine) {}

PatternModel::~PatternModel() {
    // The Machine frees whatever is still pending or retired in its own
    // destructor; nothing here owns a handoff.
}

const seq::Pattern& PatternModel::pattern(int index) const noexcept {
    return bank_.patterns[static_cast<std::size_t>(std::clamp(index, 0, seq::kNumPatterns - 1))];
}

// --- link helpers (mutex_ held) --------------------------------------------------

void PatternModel::postBankLocked(const seq::PatternBank& bank) {
    // Free what the audio thread has finished with first, so its (bounded)
    // retire queue cannot fill up even where no service pass ever runs.
    collectRetiredLocked();

    auto handoff = std::make_unique<BankHandoff>();
    handoff->bank = bank;
    handoff->generation = ++generation_;
    // A handoff the audio thread never picked up comes straight back.
    delete machine_.postBank(handoff.release());

    // Everything queued or dirty refers to the bank this one replaces.
    dirty_.reset();
    chainDirty_ = false;
    postedRecently_.reset();
    awaitingBank_ = true;
    adoptionObserved_ = false;
}

void PatternModel::postPatternLocked(int index) {
    const auto i = static_cast<std::size_t>(index);
    Command c;
    c.type = Command::Type::SetPattern;
    c.index = index;
    c.generation = generation_;
    c.pattern = bank_.patterns[i];
    if (machine_.post(c)) {
        dirty_.reset(i);
        postedAtTick_[i] = tick_;
        postedRecently_.set(i);
    } else {
        dirty_.set(i);
    }
}

void PatternModel::postChainLocked() {
    Command c;
    c.type = Command::Type::SetChain;
    c.generation = generation_;
    c.chain = bank_.chain;
    chainDirty_ = !machine_.post(c);
}

void PatternModel::postSelectLocked(int index, bool immediate) {
    Command c;
    c.type = Command::Type::SelectPattern;
    c.index = index;
    c.flag = immediate;
    if (machine_.post(c)) {
        selectDirty_ = false;
    } else {
        selectDirty_ = true;
        selectDirtyImmediate_ = immediate;
    }
    awaitingSelect_ = index;
}

void PatternModel::collectRetiredLocked() {
    while (BankHandoff* h = machine_.takeRetiredBank()) {
        if (awaitingBank_ && h->generation == generation_) adoptionObserved_ = true;
        delete h;
    }
}

unsigned PatternModel::adoptStagedLocked() {
    if (staged_ == nullptr) return 0U;
    bank_ = staged_->bank;
    selected_ = staged_->selectedPattern;
    ui_ = staged_->ui;
    staged_.reset();
    hasStaged_.store(false, std::memory_order_release);
    return kBank | kSelection | kChain;
}

void PatternModel::notify(unsigned changes, const std::bitset<seq::kNumPatterns>& patterns) {
    if ((changes & kBank) != 0U) {
        listeners_.call([](Listener& l) { l.bankChanged(); });
    } else {
        for (std::size_t i = 0; i < patterns.size(); ++i)
            if (patterns.test(i)) listeners_.call([i](Listener& l) { l.patternChanged(static_cast<int>(i)); });
        if ((changes & kChain) != 0U) listeners_.call([](Listener& l) { l.chainChanged(); });
    }
    if ((changes & kSelection) != 0U) listeners_.call([](Listener& l) { l.selectionChanged(); });
}

// --- message thread -------------------------------------------------------------

// Every mutator adopts a staged restore INSIDE the same critical section as its
// own change. Adopting first and locking again afterwards left a window in
// which a restore from another thread could post a new bank (and bump the
// generation) between the two: the edit was then made to the old model data,
// stamped with the new generation, applied by the audio thread on top of the
// restored bank, and finally overwritten in the model by the staged copy. The
// two sides stayed different for good.

void PatternModel::setPattern(int index, const seq::Pattern& p) {
    if (index < 0 || index >= seq::kNumPatterns) return;

    seq::Pattern clean = p;
    sanitise(clean);
    unsigned adopted = 0U;
    bool changed = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        adopted = adoptStagedLocked();
        auto& slot = bank_.patterns[static_cast<std::size_t>(index)];
        if (!(slot == clean)) {
            slot = clean;
            postPatternLocked(index);
            changed = true;
        }
    }
    notify(adopted, {});
    if (!changed) return;
    sessionDirty_.store(true, std::memory_order_release);
    listeners_.call([index](Listener& l) { l.patternChanged(index); });
}

void PatternModel::setChain(const seq::Chain& c) {
    seq::Chain clean = c;
    sanitise(clean);
    unsigned adopted = 0U;
    bool changed = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        adopted = adoptStagedLocked();
        if (!(bank_.chain == clean)) {
            bank_.chain = clean;
            postChainLocked();
            changed = true;
        }
    }
    notify(adopted, {});
    if (!changed) return;
    sessionDirty_.store(true, std::memory_order_release);
    listeners_.call([](Listener& l) { l.chainChanged(); });
}

void PatternModel::replaceBank(const seq::PatternBank& bank) {
    unsigned adopted = 0U;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        // `bank` may be bank_ itself (re-send the model as it stands); take
        // the copy before a staged restore replaces bank_ underneath it.
        auto incoming = std::make_unique<seq::PatternBank>(bank);
        adopted = adoptStagedLocked();
        bank_ = *incoming;
        for (auto& p : bank_.patterns) sanitise(p);
        sanitise(bank_.chain);
        postBankLocked(bank_);
    }
    // bankChanged covers whatever the adoption changed, except the selection.
    notify(adopted & kSelection, {});
    sessionDirty_.store(true, std::memory_order_release);
    listeners_.call([](Listener& l) { l.bankChanged(); });
}

void PatternModel::selectPattern(int index, bool immediate) {
    if (index < 0 || index >= seq::kNumPatterns) return;

    unsigned changes = 0U;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        changes = adoptStagedLocked();
        if (selected_ != index) changes |= kSelection;
        selected_ = index;
        // Posted even when unchanged: re-selecting cancels a queued change.
        postSelectLocked(index, immediate);
    }
    notify(changes, {});
}

void PatternModel::setUnitSeed(std::uint32_t seed) {
    if (seed_.exchange(seed, std::memory_order_acq_rel) != seed) sessionDirty_.store(true, std::memory_order_release);
}

UiState PatternModel::uiState() const {
    // A restore staged by another thread is already the session's UI state
    // (capture() saves it); report the same thing.
    if (hasStaged_.load(std::memory_order_acquire)) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (staged_ != nullptr) return staged_->ui;
    }
    return ui_;
}

void PatternModel::setUiState(const UiState& ui) {
    UiState clean = ui;
    sanitise(clean);
    unsigned adopted = 0U;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        adopted = adoptStagedLocked();
        ui_ = clean;
    }
    notify(adopted, {});
}

void PatternModel::service() {
    unsigned changes = 0U;
    std::bitset<seq::kNumPatterns> changed;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ++tick_;
        changes |= adoptStagedLocked();

        // Order matters. Retired handoffs and the audio-side selection are
        // sampled BEFORE the notices are drained: everything the audio thread
        // pushed before those observations is then already in the queue, so
        // this drain sees (and treats as stale) every notice that predates
        // them, and the flags can be cleared safely afterwards.
        collectRetiredLocked();
        const bool adoptionObserved = adoptionObserved_;
        const PlayState& play = machine_.playState();
        const bool selectionShown =
            awaitingSelect_ >= 0 && !selectDirty_ &&
            (play.selectedPattern.load(std::memory_order_relaxed) == awaitingSelect_ ||
             play.queuedPattern.load(std::memory_order_relaxed) == awaitingSelect_);

        Notice n;
        while (machine_.poll(n)) {
            if (n.type == Notice::Type::StepRecorded) {
                if (n.pattern >= seq::kNumPatterns || n.variation >= seq::kNumVariations ||
                    n.track >= seq::kNumTracks || n.step >= seq::kMaxSteps)
                    continue;
                if (awaitingBank_) {
                    // Recorded into a bank that is being replaced (or we cannot
                    // tell): the model wins, send its version of the pattern again.
                    dirty_.set(n.pattern);
                    continue;
                }
                seq::Step& cell = bank_.patterns[n.pattern].at(n.variation, n.track, n.step);
                if (!(cell == n.cell)) {
                    cell = n.cell;
                    changed.set(n.pattern);
                }
                // An edit of this pattern was posted moments ago and may have
                // overwritten the recorded step on the audio side: re-send the
                // merged pattern so both sides hold the same data.
                if (postedRecently_.test(n.pattern)) dirty_.set(n.pattern);
            } else if (n.type == Notice::Type::PatternSelected) {
                // While one of our own selections is in flight the audio side's
                // report is stale by definition.
                if (awaitingSelect_ >= 0) continue;
                if (n.pattern < seq::kNumPatterns && static_cast<int>(n.pattern) != selected_) {
                    selected_ = n.pattern;
                    changes |= kSelection;
                }
            }
        }

        if (adoptionObserved) {
            awaitingBank_ = false;
            adoptionObserved_ = false;
        }
        if (selectionShown) awaitingSelect_ = -1;

        for (std::size_t i = 0; i < postedRecently_.size(); ++i)
            if (postedRecently_.test(i) && tick_ - postedAtTick_[i] >= 2U) postedRecently_.reset(i);

        // Re-post whatever did not fit in the command queue earlier.
        if (dirty_.any())
            for (int i = 0; i < seq::kNumPatterns; ++i)
                if (dirty_.test(static_cast<std::size_t>(i))) {
                    postPatternLocked(i);
                    if (dirty_.test(static_cast<std::size_t>(i))) break;  // still full
                }
        if (chainDirty_) postChainLocked();
        if (selectDirty_) postSelectLocked(selected_, selectDirtyImmediate_);
    }

    if (changed.any()) sessionDirty_.store(true, std::memory_order_release);
    notify(changes, changed);
}

// --- any non-audio thread ------------------------------------------------------------

void PatternModel::capture(Snapshot& out) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (staged_ != nullptr) {
        out = *staged_;
    } else {
        out.bank = bank_;
        out.selectedPattern = selected_;
        out.ui = ui_;
    }
    out.unitSeed = seed_.load(std::memory_order_relaxed);
}

void PatternModel::applyLocked(const Snapshot& snapshot) {
    staged_.reset();
    hasStaged_.store(false, std::memory_order_release);
    bank_ = snapshot.bank;
    selected_ = snapshot.selectedPattern;
    ui_ = snapshot.ui;
    seed_.store(snapshot.unitSeed, std::memory_order_relaxed);
    postBankLocked(bank_);
    postSelectLocked(selected_, true);
    sessionDirty_.store(false, std::memory_order_release);
}

void PatternModel::initialise(std::unique_ptr<Snapshot> snapshot) {
    if (snapshot == nullptr) return;
    sanitiseSnapshot(*snapshot);
    const std::lock_guard<std::mutex> lock(mutex_);
    applyLocked(*snapshot);
}

void PatternModel::restore(std::unique_ptr<Snapshot> snapshot, bool keepUi) {
    if (snapshot == nullptr) return;
    sanitiseSnapshot(*snapshot);

    if (onMessageThread()) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (keepUi) snapshot->ui = staged_ != nullptr ? staged_->ui : ui_;
            applyLocked(*snapshot);
        }
        notify(kBank | kSelection | kChain, {});
        return;
    }

    // Foreign thread: the audio thread gets the data now, the model when the
    // message thread next looks.
    const std::lock_guard<std::mutex> lock(mutex_);
    if (keepUi) snapshot->ui = staged_ != nullptr ? staged_->ui : ui_;
    seed_.store(snapshot->unitSeed, std::memory_order_relaxed);
    postBankLocked(snapshot->bank);
    postSelectLocked(snapshot->selectedPattern, true);
    staged_ = std::move(snapshot);
    hasStaged_.store(true, std::memory_order_release);
    sessionDirty_.store(false, std::memory_order_release);
}

bool PatternModel::postCommand(const Command& command) {
    const std::lock_guard<std::mutex> lock(mutex_);
    return machine_.post(command);
}

void PatternModel::collectRetired() {
    const std::lock_guard<std::mutex> lock(mutex_);
    collectRetiredLocked();
}

int PatternModel::numDirty() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(dirty_.count()) + (chainDirty_ ? 1 : 0) + (selectDirty_ ? 1 : 0);
}

std::uint32_t PatternModel::generation() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
}

}  // namespace mw808::plugin

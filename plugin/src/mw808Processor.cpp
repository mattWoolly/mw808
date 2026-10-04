// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "mw808Processor.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "mw808/Factory.h"
#include "mw808/MidiMap.h"
#include "mw808/seq/MidiFile.h"

// ---------------------------------------------------------------------------
// Editor selection. This is the ONE line to change when the real editor
// (plugin/ui/mw808Editor.h, class mw808Editor) lands: set it to 0. Sources
// under plugin/ui are already globbed into the build. Configuring with
// -DMW808_WITH_UI_SOURCES=OFF defines it to 1 from CMake (shell-only builds).
#ifndef MW808_USE_PLACEHOLDER_EDITOR
 #define MW808_USE_PLACEHOLDER_EDITOR 0
#endif
// ---------------------------------------------------------------------------

#if MW808_USE_PLACEHOLDER_EDITOR
 #include "PlaceholderEditor.h"
using mw808EditorClass = mw808::plugin::PlaceholderEditor;
#else
 #include "mw808Editor.h"
using mw808EditorClass = mw808Editor;
#endif

#ifndef MW808_PRODUCT_NAME
 #define MW808_PRODUCT_NAME "mw808"
#endif

namespace {

using mw808::plugin::state::PluginState;
using mw808::plugin::state::ReadStatus;

bool onMessageThread() noexcept { return juce::MessageManager::existsAndIsCurrentThread(); }

// Holds the process gate for a scope, waiting for a block in flight to finish.
// Never used on the audio thread.
class GateLock {
public:
    explicit GateLock(std::atomic<bool>& gate) : gate_(gate) {
        while (gate_.exchange(true, std::memory_order_acquire)) std::this_thread::yield();
    }
    ~GateLock() { gate_.store(false, std::memory_order_release); }
    GateLock(const GateLock&) = delete;
    GateLock& operator=(const GateLock&) = delete;

private:
    std::atomic<bool>& gate_;
};

// A normalised value close to `normalised` whose real-world value the APVTS
// does not consider approximately equal to that of `normalised` (so setting it
// is not ignored), searching outwards from one part in a million.
float nearbyDistinctValue(const juce::RangedAudioParameter& p, float normalised) {
    const float target = p.convertFrom0to1(normalised);
    for (float delta = 1.0e-6f; delta <= 0.5f; delta *= 4.0f)
        for (const float sign : {1.0f, -1.0f}) {
            const float candidate = normalised + sign * delta;
            if (candidate < 0.0f || candidate > 1.0f) continue;
            if (!juce::approximatelyEqual(p.convertFrom0to1(candidate), target)) return candidate;
        }
    return normalised < 0.5f ? 1.0f : 0.0f;
}

// The audio thread's side: one attempt, no waiting.
class GateTry {
public:
    explicit GateTry(std::atomic<bool>& gate) noexcept
        : gate_(gate), entered_(!gate.exchange(true, std::memory_order_acquire)) {}
    ~GateTry() {
        if (entered_) gate_.store(false, std::memory_order_release);
    }
    GateTry(const GateTry&) = delete;
    GateTry& operator=(const GateTry&) = delete;
    bool entered() const noexcept { return entered_; }

private:
    std::atomic<bool>& gate_;
    bool entered_;
};

}  // namespace

juce::AudioProcessor::BusesProperties mw808Processor::makeBuses() {
    // One stereo main output, plus eleven individual outs the host may enable.
    auto buses = BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 2; i <= mw808::kNumBuses; ++i)
        buses = buses.withOutput("Out " + juce::String(i), juce::AudioChannelSet::stereo(), false);
    return buses;
}

mw808Processor::mw808Processor()
    : juce::AudioProcessor(makeBuses()),
      apvts_(*this, nullptr, juce::Identifier(mw808::plugin::state::kParametersType),
             mw808::plugin::createParameterLayout()),
      params_(apvts_),
      machine_(std::make_unique<mw808::Machine>()),
      patterns_(*machine_),
      midiLearn_(apvts_),
      presets_(*this) {
    for (const auto& id : mw808::plugin::allParameterIds()) {
        orderedParams_.push_back(apvts_.getParameter(juce::String(id)));
        orderedRaw_.push_back(apvts_.getRawParameterValue(juce::String(id)));
        jassert(orderedParams_.back() != nullptr && orderedRaw_.back() != nullptr);
    }

    midiExportDir_ = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(MW808_PRODUCT_NAME "-midi");

    // A new instance is factory preset 0. Nothing else can see the object yet,
    // so this applies directly whatever thread constructs the plugin.
    const auto defaults = makeDefaultState();
    for (std::size_t i = 0; i < orderedParams_.size(); ++i) {
        auto* p = orderedParams_[i];
        const float normalised = p->convertTo0to1(defaults->parameters[i].second);
        if (!juce::exactlyEqual(p->getValue(), normalised)) p->setValueNotifyingHost(normalised);
    }
    patterns_.initialise(std::make_unique<PatternModel::Snapshot>(defaults->model));

    startTimerHz(kServiceHz);
}

mw808Processor::~mw808Processor() { stopTimer(); }

const juce::String mw808Processor::getName() const { return MW808_PRODUCT_NAME; }

// --- audio setup -------------------------------------------------------------------

void mw808Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const GateLock lock(processGate_);
    prepared_.store(false, std::memory_order_release);
    const double rate = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 44100.0;
    machine_->prepare(rate, std::max(1, samplesPerBlock));
    resetPending_.store(false, std::memory_order_release);
    midiOutFlushPending_.store(true, std::memory_order_release);
    prepared_.store(true, std::memory_order_release);
}

void mw808Processor::releaseResources() {
    // Nothing to free: the Machine keeps its (small) scratch buffers, so a host
    // that processes after releasing still gets valid audio.
}

void mw808Processor::reset() {
    // May arrive on any thread; the next block silences the voices.
    resetPending_.store(true, std::memory_order_release);
}

bool mw808Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    for (const auto& in : layouts.inputBuses)
        if (!in.isDisabled()) return false;
    if (layouts.outputBuses.isEmpty()) return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i) {
        const auto& set = layouts.outputBuses.getReference(i);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

// --- audio thread ------------------------------------------------------------------

void mw808Processor::makeSnapshot(mw808::MachineParams& out) const noexcept {
    params_.snapshot(out);
    out.unitSeed = patterns_.unitSeedForAudio();
}

mw808::seq::Transport mw808Processor::readTransport(double fallbackBpm) const noexcept {
    mw808::seq::Transport t;
    t.bpm = fallbackBpm;
    if (auto* head = getPlayHead()) {
        if (const auto position = head->getPosition()) {
            const auto ppq = position->getPpqPosition();
            if (ppq.hasValue() && std::isfinite(*ppq)) {
                t.hostValid = true;
                t.ppq = *ppq;
                t.playing = position->getIsPlaying();
                const auto bpm = position->getBpm();
                if (bpm.hasValue() && std::isfinite(*bpm) && *bpm > 0.0) t.bpm = *bpm;
            }
        }
    }
    return t;
}

void mw808Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    const juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Not prepared yet, or a prepareToPlay is in progress on another thread:
    // silence, and leave the Machine alone.
    const GateTry gate(processGate_);
    if (!gate.entered() || !prepared_.load(std::memory_order_acquire)) {
        buffer.clear();
        midi.clear();
        return;
    }

    // --- MIDI in: notes to the Machine, controllers to MIDI learn -------------
    int numMidiIn = 0;
    const int lastSample = std::max(0, numSamples - 1);
    for (const auto meta : midi) {
        if (meta.numBytes != 3) continue;
        const std::uint8_t status = meta.data[0];
        const int type = status & 0xF0;
        if (type == 0xB0) {
            midiLearn_.handleController(meta.data[1] & 0x7F, meta.data[2] & 0x7F);
        } else if ((type == 0x90 || type == 0x80) && numMidiIn < kMaxMidiIn) {
            mw808::MidiMessage& m = midiIn_[static_cast<std::size_t>(numMidiIn++)];
            m.offset = std::clamp(meta.samplePosition, 0, lastSample);
            m.status = status;
            m.data1 = static_cast<std::uint8_t>(meta.data[1] & 0x7F);
            m.data2 = static_cast<std::uint8_t>(meta.data[2] & 0x7F);
        }
    }
    midi.clear();

    // --- output buses ------------------------------------------------------------
    // Enabled stereo buses map onto the Machine's buses; a bus the host left
    // disabled stays null and its voices fall back to the main mix.
    mw808::Machine::ProcessArgs args;
    constexpr int kTracked = 64;
    std::array<bool, kTracked> written{};
    const int busCount = std::min(getBusCount(false), mw808::kNumBuses);
    for (int b = 0; b < busCount; ++b) {
        const auto* bus = getBus(false, b);
        if (bus == nullptr || !bus->isEnabled() || bus->getNumberOfChannels() != 2) continue;
        const int left = getChannelIndexInProcessBlockBuffer(false, b, 0);
        const int right = getChannelIndexInProcessBlockBuffer(false, b, 1);
        if (left < 0 || right < 0 || left >= numChannels || right >= numChannels || left == right) continue;
        args.out.ch[static_cast<std::size_t>(b)] = {buffer.getWritePointer(left), buffer.getWritePointer(right)};
        if (left < kTracked) written[static_cast<std::size_t>(left)] = true;
        if (right < kTracked) written[static_cast<std::size_t>(right)] = true;
    }

    const auto& mainOut = args.out.ch[0];
    if (mainOut[0] == nullptr || mainOut[1] == nullptr) {
        // No usable main bus (a layout isBusesLayoutSupported rejects).
        buffer.clear();
        return;
    }
    // Whatever the Machine will not overwrite must not carry garbage.
    for (int ch = 0; ch < numChannels; ++ch)
        if (ch >= kTracked || !written[static_cast<std::size_t>(ch)]) buffer.clear(ch, 0, numSamples);

    // --- parameters and transport: one snapshot per block ---------------------------
    mw808::MachineParams params;
    makeSnapshot(params);

    args.transport = readTransport(params.seq.internalBpm);
    // Without a host transport (standalone, some offline hosts) Host Sync
    // would never start: it runs on the internal clock, so Run always works.
    if (!args.transport.hostValid && params.seq.mode == mw808::seq::Mode::HostSync)
        params.seq.mode = mw808::seq::Mode::Internal;

    if (resetPending_.exchange(false, std::memory_order_acq_rel)) machine_->reset();

    args.midiIn = midiIn_.data();
    args.numMidiIn = numMidiIn;
    args.midiOut = &midiOut_;
    args.numSamples = numSamples;
    machine_->process(args, params);

    // --- MIDI out -------------------------------------------------------------------
    // Written into the host's buffer. The block is capped at kMaxMidiOutNoteOns
    // note-ons, and a note-off is passed on only for a note the host was
    // actually sent, so a block never writes more than kMidiOutReserveBytes
    // and no note is ever left hanging. ensureSize() allocates only when the
    // host reserved less than that: the VST3 and CLAP wrappers reserve exactly
    // that much and LV2 more, so for them it is a no-op; the Standalone's
    // player reserves nothing, and there the first block that sends MIDI
    // allocates once.
    const bool flush = midiOutFlushPending_.exchange(false, std::memory_order_acq_rel);
    if (!flush && midiOut_.count == 0) return;
    midi.ensureSize(kMidiOutReserveBytes);
    // The Machine was re-prepared and has forgotten its sounding notes.
    if (flush) releaseMidiOut(midi, 0);
    int noteOns = 0;
    for (int i = 0; i < midiOut_.count; ++i) {
        const mw808::MidiMessage& m = midiOut_.messages[static_cast<std::size_t>(i)];
        const int type = m.status & 0xF0;
        const auto note = static_cast<std::size_t>(m.data1 & 0x7F);
        if (type == 0x90 && m.data2 > 0) {
            if (noteOns >= kMaxMidiOutNoteOns) continue;
            ++noteOns;
            midiOutSounding_[note] = true;
        } else if (type == 0x80 || type == 0x90) {
            if (!midiOutSounding_[note]) continue;
            midiOutSounding_[note] = false;
        } else {
            continue;
        }
        const std::uint8_t bytes[3] = {m.status, m.data1, m.data2};
        midi.addEvent(bytes, 3, std::clamp(static_cast<int>(m.offset), 0, lastSample));
    }
}

void mw808Processor::releaseMidiOut(juce::MidiBuffer& midi, int sample) noexcept {
    for (std::size_t note = 0; note < midiOutSounding_.size(); ++note)
        if (midiOutSounding_[note]) {
            const std::uint8_t bytes[3] = {0x89, static_cast<std::uint8_t>(note), 0};
            midi.addEvent(bytes, 3, sample);
            midiOutSounding_[note] = false;
        }
}

void mw808Processor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    // JUCE's default (silence, MIDI passes through), plus a note-off for every
    // note MIDI Out sent and has not released: they would hang for as long as
    // the bypass lasts. The Machine's own late note-offs for them are dropped
    // after the bypass, since those notes are no longer marked sounding.
    juce::AudioProcessor::processBlockBypassed(buffer, midi);
    const GateTry gate(processGate_);
    if (!gate.entered()) return;
    bool any = false;
    for (const bool sounding : midiOutSounding_) any = any || sounding;
    if (!any) return;
    // The host's MIDI input is still in the buffer (passed through).
    midi.ensureSize(static_cast<std::size_t>(midi.data.size()) + kMidiOutReserveBytes);
    releaseMidiOut(midi, 0);
}

// --- editor --------------------------------------------------------------------------

juce::AudioProcessorEditor* mw808Processor::createEditor() { return new mw808EditorClass(*this); }

// --- programs ------------------------------------------------------------------------

int mw808Processor::getNumPrograms() { return std::max(1, mw808::factory::numPresets()); }

int mw808Processor::getCurrentProgram() { return currentProgram_.load(std::memory_order_relaxed); }

const juce::String mw808Processor::getProgramName(int index) {
    if (index < 0 || index >= mw808::factory::numPresets()) return {};
    return juce::String(std::string(mw808::factory::preset(index).name));
}

void mw808Processor::changeProgramName(int, const juce::String&) {
    // Factory programs are read-only.
}

void mw808Processor::setCurrentProgram(int index) {
    // Hosts probe with bad indices, and wrappers re-send the current one: both are no-ops.
    if (index < 0 || index >= getNumPrograms()) return;
    if (currentProgram_.exchange(index, std::memory_order_acq_rel) == index) return;

    if (onMessageThread()) {
        pendingProgram_.store(-1, std::memory_order_release);
        presets_.load(mw808::plugin::PresetKind::Full, index);
    } else {
        // The VST3 wrapper delivers program changes on the audio thread.
        // Remember the request; the service timer loads it.
        pendingProgram_.store(index, std::memory_order_release);
    }
}

void mw808Processor::applyPendingProgram() {
    const int pending = pendingProgram_.exchange(-1, std::memory_order_acq_rel);
    if (pending >= 0 && pending < mw808::factory::numPresets()) presets_.load(mw808::plugin::PresetKind::Full, pending);
}

void mw808Processor::factoryProgramLoaded(int index) {
    if (index < 0 || index >= getNumPrograms()) return;
    if (currentProgram_.exchange(index, std::memory_order_acq_rel) != index)
        programNotifyPending_.store(true, std::memory_order_release);
}

// --- state ---------------------------------------------------------------------------

std::unique_ptr<PluginState> mw808Processor::makeDefaultState() const {
    auto state = std::make_unique<PluginState>();
    state->parameters.reserve(orderedParams_.size());
    const auto& ids = mw808::plugin::allParameterIds();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto* p = orderedParams_[i];
        state->parameters.emplace_back(juce::String(ids[i]), p->convertFrom0to1(p->getDefaultValue()));
    }

    const auto& preset = mw808::factory::preset(0);
    const auto set = [&state](const juce::String& id, float value) {
        for (auto& [pid, pvalue] : state->parameters)
            if (pid == id) {
                pvalue = value;
                return;
            }
    };
    for (const auto& [id, value] : mw808::plugin::Parameters::kitValues(mw808::factory::kit(preset.kit)))
        set(juce::String(id), value);
    set(mw808::plugin::paramid::tempo, preset.tempo);

    mw808::factory::buildBank(preset.bank, state->model.bank);
    state->model.selectedPattern = std::clamp(preset.pattern, 0, mw808::seq::kNumPatterns - 1);
    state->model.unitSeed = mw808::plugin::kDefaultUnitSeed;
    state->model.ui = UiState{};
    state->model.ui.presetNames[static_cast<std::size_t>(mw808::plugin::PresetKind::Kit)] =
        juce::String(std::string(mw808::factory::kit(preset.kit).name));
    state->model.ui.presetNames[static_cast<std::size_t>(mw808::plugin::PresetKind::Patterns)] =
        juce::String(std::string(mw808::factory::bankInfo(preset.bank).name));
    state->model.ui.presetNames[static_cast<std::size_t>(mw808::plugin::PresetKind::Full)] =
        juce::String(std::string(preset.name));
    state->program = 0;
    return state;
}

std::unique_ptr<PluginState> mw808Processor::captureState() const {
    auto state = std::make_unique<PluginState>();
    const auto& ids = mw808::plugin::allParameterIds();
    state->parameters.reserve(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i)
        state->parameters.emplace_back(juce::String(ids[i]), orderedRaw_[i]->load(std::memory_order_relaxed));
    patterns_.capture(state->model);
    state->midiMap = midiLearn_.getMap();
    state->program = currentProgram_.load(std::memory_order_relaxed);
    return state;
}

void mw808Processor::applyState(const PluginState& state, bool sessionExtras) {
    // Parameters: a complete tree in layout order, every value forced into its
    // parameter's legal set, handed to the APVTS in one piece.
    juce::ValueTree tree{juce::Identifier(mw808::plugin::state::kParametersType)};
    const auto& ids = mw808::plugin::allParameterIds();
    std::vector<float> targets(ids.size(), 0.0f);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto* p = orderedParams_[i];
        float value = p->convertFrom0to1(p->getDefaultValue());
        const juce::String id(ids[i]);
        for (const auto& [sid, svalue] : state.parameters)
            if (sid == id) {
                if (std::isfinite(svalue)) value = p->convertFrom0to1(p->convertTo0to1(svalue));
                break;
            }
        targets[i] = value;
        juce::ValueTree child{juce::Identifier("PARAM")};
        child.setProperty("id", id, nullptr);
        child.setProperty("value", value, nullptr);
        tree.appendChild(child, nullptr);
    }
    // From here on the tree belongs to the APVTS (a ValueTree is a shared
    // reference): it is not touched again on this thread.
    apvts_.replaceState(tree);

    // The APVTS skips a value it considers "approximately equal" to the one it
    // holds (within a float epsilon). A restore has to be exact, so the rare
    // parameter left one rounding step off is moved away and back. "Away" is
    // the nearest value the APVTS will not ignore: the audio thread may run a
    // block in between, and a jump to the far end of the range (full master
    // volume, 300 BPM) would be heard.
    // The parameter's own value (what the host reads back) can be one rounding
    // step off while the APVTS copy is exact: setting it directly is enough.
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto* p = orderedParams_[i];
        const float normalised = p->convertTo0to1(targets[i]);
        if (!juce::exactlyEqual(orderedRaw_[i]->load(std::memory_order_relaxed), targets[i])) {
            p->setValueNotifyingHost(nearbyDistinctValue(*p, normalised));
            p->setValueNotifyingHost(normalised);
        } else if (!juce::exactlyEqual(p->getValue(), normalised)) {
            p->setValueNotifyingHost(normalised);
        }
    }

    patterns_.restore(std::make_unique<PatternModel::Snapshot>(state.model), !sessionExtras);
    if (sessionExtras) {
        midiLearn_.setMap(state.midiMap);
        currentProgram_.store(std::clamp(state.program, 0, getNumPrograms() - 1), std::memory_order_release);
    }
}

void mw808Processor::getStateInformation(juce::MemoryBlock& destData) {
    // A program change still waiting for the timer belongs in the saved state.
    if (onMessageThread()) applyPendingProgram();

    const auto state = captureState();
    if (const auto xml = mw808::plugin::state::toTree(*state).createXml()) copyXmlToBinary(*xml, destData);
}

void mw808Processor::setStateInformation(const void* data, int sizeInBytes) {
    // The state being loaded supersedes any program change not applied yet.
    pendingProgram_.store(-1, std::memory_order_release);

    const auto defaults = makeDefaultState();
    mw808::plugin::state::ReadResult result;
    if (data != nullptr && sizeInBytes > 8) {
        // The block is JUCE's XML container: magic, text length, text. The XML
        // parser accepts a document that simply stops between two elements, so
        // a truncated block is caught here, by its declared length, rather
        // than half-loaded. The text must also be valid UTF-8 (everything this
        // plugin writes is): getXmlFromBinary asserts on anything else.
        const auto declared = static_cast<juce::int64>(
            juce::ByteOrder::littleEndianInt(juce::addBytesToPointer(data, 4)));
        const auto* text = static_cast<const char*>(juce::addBytesToPointer(data, 8));
        // Nesting and size are bounded before parsing too (see StateCodec.h).
        namespace st = mw808::plugin::state;
        if (declared > 0 && declared <= static_cast<juce::int64>(sizeInBytes) - 8 &&
            declared <= static_cast<juce::int64>(st::kMaxStateBytes) &&
            juce::CharPointer_UTF8::isValidString(text, static_cast<int>(declared)) &&
            !st::hasDoctype(text, static_cast<std::size_t>(declared)) &&
            !st::xmlNestingExceeds(text, static_cast<std::size_t>(declared), st::kMaxXmlDepth)) {
            if (const auto xml = getXmlFromBinary(data, sizeInBytes))
                result = mw808::plugin::state::fromTree(juce::ValueTree::fromXml(*xml), *defaults);
        }
    }
    lastLoadStatus_.store(result.status, std::memory_order_release);

    // Unreadable or from a newer build: a known-good state rather than a
    // half-applied one.
    applyState(result.state != nullptr ? *result.state : *defaults, true);

    // CLAP: a state load changes parameter values behind the host's back, and
    // the host has to be asked to read them again. The CLAP wrapper turns
    // "program changed" into exactly that values rescan. The other wrappers
    // resynchronise the host themselves after a state load.
    if (isClap()) {
        if (onMessageThread())
            updateHostDisplay(ChangeDetails().withProgramChanged(true));
        else
            programNotifyPending_.store(true, std::memory_order_release);
    }
}

bool mw808Processor::isClap() const noexcept {
#if defined(HAS_CLAP_JUCE_EXTENSIONS)
    return clapProperties_.is_clap;
#else
    return false;
#endif
}

void mw808Processor::setParameterValues(const std::vector<std::pair<juce::String, float>>& values,
                                        bool withGestures) {
    for (const auto& [id, value] : values) {
        auto* p = apvts_.getParameter(id);
        if (p == nullptr || !std::isfinite(value)) continue;
        const float normalised = p->convertTo0to1(value);
        if (juce::exactlyEqual(p->getValue(), normalised)) continue;
        if (withGestures) p->beginChangeGesture();
        p->setValueNotifyingHost(normalised);
        if (withGestures) p->endChangeGesture();
    }
}

// --- message-thread service ----------------------------------------------------------

void mw808Processor::timerCallback() { serviceMessageThread(); }

void mw808Processor::serviceMessageThread() {
    applyPendingProgram();
    patterns_.service();
    midiLearn_.service();

    if (panicPending_.load(std::memory_order_acquire)) {
        mw808::Command c;
        c.type = mw808::Command::Type::Panic;
        if (patterns_.postCommand(c)) panicPending_.store(false, std::memory_order_release);
    }

    const bool patternsDirty = patterns_.takeSessionDirty();
    const bool midiDirty = midiLearn_.takeSessionDirty();
    const bool programChanged = programNotifyPending_.exchange(false, std::memory_order_acq_rel);
    if (patternsDirty || midiDirty || programChanged)
        updateHostDisplay(ChangeDetails()
                              .withNonParameterStateChanged(patternsDirty || midiDirty)
                              .withProgramChanged(programChanged));
}

// --- editor API ----------------------------------------------------------------------

void mw808Processor::audition(mw808::VoiceId voice, bool accent) {
    if (static_cast<int>(voice) >= mw808::kNumVoices) return;
    mw808::Command c;
    c.type = mw808::Command::Type::Audition;
    c.index = static_cast<std::int32_t>(voice);
    const float accentKnob = apvts_.getRawParameterValue(mw808::plugin::paramid::accent)->load();
    c.value = accent ? mw808::Hit::accented(accentKnob).volts : mw808::Hit::kNormalVolts;
    patterns_.postCommand(c);
}

void mw808Processor::requestFill() {
    mw808::Command c;
    c.type = mw808::Command::Type::Fill;
    patterns_.postCommand(c);
}

void mw808Processor::panic() {
    mw808::Command c;
    c.type = mw808::Command::Type::Panic;
    // A full queue must not swallow a panic: the service timer retries.
    if (!patterns_.postCommand(c)) panicPending_.store(true, std::memory_order_release);
}

juce::File mw808Processor::exportPatternToMidiFile(int patternIndex, int variation) {
    if (patternIndex < 0 || patternIndex >= mw808::seq::kNumPatterns) return {};
    const int var = std::clamp(variation, 0, mw808::seq::kNumVariations - 1);

    mw808::seq::MidiFileOptions options;
    options.bpm = static_cast<double>(apvts_.getRawParameterValue(mw808::plugin::paramid::tempo)->load());
    options.flamMs = apvts_.getRawParameterValue(mw808::plugin::paramid::flamTime)->load();
    options.swingScale = apvts_.getRawParameterValue(mw808::plugin::paramid::shuffleDepth)->load();
    const std::vector<std::uint8_t> bytes =
        mw808::seq::writeMidiFile(patterns_.pattern(patternIndex), var, mw808::kVoiceNote, options);
    if (bytes.empty()) return {};

    if (!midiExportDir_.createDirectory().wasOk()) return {};
    const juce::String bank = juce::String::charToString(
        static_cast<juce::juce_wchar>('A' + patternIndex / mw808::seq::kPatternsPerBank));
    const juce::String slot = juce::String(patternIndex % mw808::seq::kPatternsPerBank + 1).paddedLeft('0', 2);
    const juce::File file = midiExportDir_.getChildFile(juce::String(MW808_PRODUCT_NAME) + " " + bank + slot +
                                                        (var == 0 ? " A" : " B") + ".mid");
    if (!file.replaceWithData(bytes.data(), bytes.size())) return {};
    return file;
}

bool mw808Processor::importMidiFile(const juce::File& file, int patternIndex) {
    return importMidiFile(file, patternIndex, 0);
}

bool mw808Processor::importMidiFile(const juce::File& file, int patternIndex, int variation) {
    if (patternIndex < 0 || patternIndex >= mw808::seq::kNumPatterns) return false;
    constexpr juce::int64 kMaxBytes = 4 * 1024 * 1024;
    if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > kMaxBytes) return false;

    juce::MemoryBlock data;
    if (!file.loadFileAsData(data) || data.getSize() == 0) return false;
    const auto* begin = static_cast<const std::uint8_t*>(data.getData());
    const std::vector<std::uint8_t> bytes(begin, begin + data.getSize());

    mw808::seq::Pattern pattern = patterns_.pattern(patternIndex);
    if (!mw808::seq::readMidiFile(bytes, mw808::kVoiceNote, pattern,
                                  std::clamp(variation, 0, mw808::seq::kNumVariations - 1)))
        return false;
    patterns_.setPattern(patternIndex, pattern);
    return true;
}

// The plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mw808Processor(); }

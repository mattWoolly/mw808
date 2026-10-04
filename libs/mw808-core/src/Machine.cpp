// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "mw808/Machine.h"

#include <algorithm>
#include <cmath>

#include "mw808/dsp/Math.h"

namespace mw808 {

namespace {

constexpr double kMidiNoteSeconds = 0.020;  // length of the notes sent for sequencer hits
constexpr std::uint8_t kMidiOutChannel = 9;  // channel 10, zero-based

std::uint8_t velocityFor(seq::StepLevel level) noexcept {
    switch (level) {
        case seq::StepLevel::Weak:   return kVelocityWeak;
        case seq::StepLevel::Accent: return kVelocityAccent;
        default:                     return kVelocityNormal;
    }
}

bool audible(int voice, const MachineParams& params) noexcept {
    const auto v = static_cast<std::size_t>(voice);
    bool anySolo = false;
    for (bool s : params.solo) anySolo = anySolo || s;
    return anySolo ? params.solo[v] : !params.mute[v];
}

}  // namespace

Machine::~Machine() {
    delete pendingBank_.exchange(nullptr);
    BankHandoff* h = nullptr;
    while (retiredBanks_.pop(h)) delete h;
}

void Machine::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(1, maxBlockSize);
    engine_.prepare(sampleRate_, maxBlock_);
    sequencer_.prepare(sampleRate_);
    noteOffAt_.fill(-1);
    noteLengthSamples_ = std::max(1, static_cast<std::int32_t>(kMidiNoteSeconds * sampleRate_));
    numLiveHits_ = 0;
    numAuditions_ = 0;
    activePatternNote_ = -1;
}

void Machine::reset() noexcept {
    engine_.reset();
    sequencer_.reset();
    numLiveHits_ = 0;
    numAuditions_ = 0;
    activePatternNote_ = -1;
    // noteOffAt_ is left alone so sounding MIDI-out notes still get their note-offs.
}

seq::Transport Machine::transportAt(const seq::Transport& t, int offset) const noexcept {
    seq::Transport out = t;
    if (t.hostValid && t.playing && offset > 0)
        out.ppq += static_cast<double>(offset) * t.bpm / (60.0 * sampleRate_);
    return out;
}

void Machine::adoptPendingBank() noexcept {
    BankHandoff* handoff = pendingBank_.exchange(nullptr, std::memory_order_acq_rel);
    if (handoff == nullptr) return;
    for (int i = 0; i < seq::kNumPatterns; ++i)
        sequencer_.setPattern(i, handoff->bank.patterns[static_cast<std::size_t>(i)]);
    sequencer_.setChain(handoff->bank.chain);
    bankGeneration_ = handoff->generation;
    // Never free on the audio thread. If the retire queue is somehow full the
    // handoff is leaked rather than deleted here.
    retiredBanks_.push(handoff);
}

void Machine::drainCommands() noexcept {
    // Unsigned wrap-around compare: is generation a older than b?
    const auto older = [](std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) < 0; };

    Command c;
    while (commands_.pop(c)) {
        switch (c.type) {
            case Command::Type::SetPattern:
                if (older(c.generation, bankGeneration_)) break;  // edit predates the current bank
                if (c.index >= 0 && c.index < seq::kNumPatterns) sequencer_.setPattern(c.index, c.pattern);
                break;
            case Command::Type::SetChain:
                if (older(c.generation, bankGeneration_)) break;
                sequencer_.setChain(c.chain);
                break;
            case Command::Type::SelectPattern:
                if (c.index >= 0 && c.index < seq::kNumPatterns) {
                    sequencer_.selectPattern(c.index, c.flag);
                    // The UI asked for this one; do not echo it back as a change.
                    if (sequencer_.selectedPattern() == c.index) lastSelectedPattern_ = c.index;
                }
                break;
            case Command::Type::Fill:
                sequencer_.requestFill();
                break;
            case Command::Type::Audition:
                if (c.index >= 0 && c.index < kNumVoices && numAuditions_ < kMaxAuditions) {
                    TimedHit h;
                    h.voice = static_cast<VoiceId>(c.index);
                    h.hit.volts = dsp::clamp(c.value, 0.5f, Hit::kMaxVolts);
                    h.force = true;
                    auditions_[static_cast<std::size_t>(numAuditions_++)] = h;
                }
                break;
            case Command::Type::Panic:
                engine_.reset();
                sequencer_.reset();
                activePatternNote_ = -1;
                break;
        }
    }
}

void Machine::process(const ProcessArgs& args, const MachineParams& params) noexcept {
    if (args.midiOut != nullptr) args.midiOut->clear();

    engine_.configure(params.quality, params.unitSeed, params.variance);
    engine_.setParams(params.engine);
    adoptPendingBank();
    drainCommands();

    if (maxBlock_ <= 0 || args.numSamples <= 0) return;

    int midiIndex = 0;
    for (int start = 0; start < args.numSamples;) {
        const int len = std::min(maxBlock_, args.numSamples - start);
        processChunk(args, params, start, len, midiIndex);
        start += len;
    }

    publish(args);
}

void Machine::processChunk(const ProcessArgs& args, const MachineParams& params, int chunkStart, int chunkLen,
                           int& midiIndex) noexcept {
    const seq::Settings& settings = params.seq;
    const bool hostNote = settings.mode == seq::Mode::HostNote;
    const bool playingBefore = sequencer_.isPlaying();

    seqEvents_.clear();
    numLiveHits_ = 0;
    int seqCursor = 0;  // samples of this chunk the sequencer has already processed
    if (!hostNote) activePatternNote_ = -1;

    // Auditions from the UI sound at the top of the block.
    for (int i = 0; i < numAuditions_; ++i) liveHits_[static_cast<std::size_t>(numLiveHits_++)] = auditions_[static_cast<std::size_t>(i)];
    numAuditions_ = 0;

    // --- MIDI in ------------------------------------------------------------
    while (midiIndex < args.numMidiIn && args.midiIn[midiIndex].offset < chunkStart + chunkLen) {
        const MidiMessage& m = args.midiIn[midiIndex++];
        const int local = dsp::clamp(m.offset - chunkStart, 0, chunkLen - 1);
        const int type = m.status & 0xF0;
        const int channel = (m.status & 0x0F) + 1;
        const int note = m.data1 & 0x7F;
        const int velocity = m.data2 & 0x7F;
        const bool noteOn = type == 0x90 && velocity > 0;
        const bool noteOff = type == 0x80 || (type == 0x90 && velocity == 0);

        if (hostNote) {
            // Notes select and hold patterns; bring the sequencer up to the
            // note's position first so the change lands sample-accurately.
            const int index = note - kPatternNoteBase;
            const bool starts = noteOn && index >= 0 && index < seq::kNumPatterns;
            const bool stops = noteOff && note == activePatternNote_;
            if (!starts && !stops) continue;
            if (local > seqCursor) {
                sequencer_.process(transportAt(args.transport, chunkStart + seqCursor), settings, local - seqCursor,
                                   seqEvents_, seqCursor);
                seqCursor = local;
            }
            if (starts) {
                sequencer_.notePatternOn(index);
                activePatternNote_ = note;
            } else {
                sequencer_.notePatternOff();
                activePatternNote_ = -1;
            }
            continue;
        }

        if (!noteOn || numLiveHits_ >= kMaxLiveHits) continue;

        TimedHit h;
        h.offset = local;
        h.hit = velocityToHit(velocity, settings.accent, params.midi.velocitySensitive);
        h.level = velocity >= 112 ? seq::StepLevel::Accent
                                  : (velocity < 72 ? seq::StepLevel::Weak : seq::StepLevel::Normal);

        if (params.midi.bassChannel != 0 && channel == params.midi.bassChannel) {
            h.voice = BD;
            h.hit.pitchSemis = static_cast<float>(dsp::clamp(note - kBassRootNote, -36, 36));
            liveHits_[static_cast<std::size_t>(numLiveHits_++)] = h;
            continue;
        }

        const std::int8_t voice = kNoteToVoice[static_cast<std::size_t>(note)];
        if (voice == kNoVoice) continue;
        h.voice = static_cast<VoiceId>(voice);
        liveHits_[static_cast<std::size_t>(numLiveHits_++)] = h;

        if (settings.record && playingBefore) {
            const seq::RecordResult r = sequencer_.recordHit(h.voice, h.level == seq::StepLevel::Accent, local);
            if (r.changed) {
                Notice n;
                n.type = Notice::Type::StepRecorded;
                n.pattern = r.pattern;
                n.variation = r.variation;
                n.track = r.track;
                n.step = r.step;
                n.cell = r.cell;
                notices_.push(n);
            }
        }
    }

    // --- sequencer ------------------------------------------------------------
    if (chunkLen > seqCursor) {
        sequencer_.process(transportAt(args.transport, chunkStart + seqCursor), settings, chunkLen - seqCursor,
                           seqEvents_, seqCursor);
    }

    // --- render, splitting at every hit (both lists are sorted by offset) -------
    int cursor = 0;
    int li = 0, si = 0;
    while (li < numLiveHits_ || si < seqEvents_.count) {
        const bool takeLive = si >= seqEvents_.count ||
                              (li < numLiveHits_ && liveHits_[static_cast<std::size_t>(li)].offset <=
                                                        seqEvents_.events[static_cast<std::size_t>(si)].offset);
        TimedHit h;
        if (takeLive) {
            h = liveHits_[static_cast<std::size_t>(li++)];
        } else {
            const seq::Event& e = seqEvents_.events[static_cast<std::size_t>(si++)];
            h.offset = e.offset;
            h.voice = e.voice;
            h.level = e.level;
            h.hit = e.hit;
            h.fromSequencer = true;
        }
        h.offset = dsp::clamp(h.offset, cursor, chunkLen - 1);
        fire(h, args, params, chunkStart, cursor);
    }
    if (cursor < chunkLen) engine_.render(args.out, chunkStart + cursor, chunkLen - cursor);

    flushNoteOffs(args.midiOut, chunkStart, chunkLen);
}

void Machine::fire(const TimedHit& h, const ProcessArgs& args, const MachineParams& params, int chunkStart,
                   int& cursor) noexcept {
    if (h.voice >= kNumVoices) return;
    if (!h.force && !audible(h.voice, params)) return;

    if (h.offset > cursor) {
        engine_.render(args.out, chunkStart + cursor, h.offset - cursor);
        cursor = h.offset;
    }
    engine_.trigger(h.voice, h.hit);
    state_.hits[h.voice].fetch_add(1U, std::memory_order_relaxed);

    if (h.fromSequencer && params.midi.midiOut && args.midiOut != nullptr) {
        const auto v = static_cast<std::size_t>(h.voice);
        const std::uint8_t note = kVoiceNote[v];
        if (noteOffAt_[v] >= 0) {
            // Close the previous note, at its due time or right here, whichever is first.
            const std::int32_t at = std::min(noteOffAt_[v], h.offset);
            args.midiOut->push({chunkStart + at, static_cast<std::uint8_t>(0x80 | kMidiOutChannel), note, 0});
        }
        args.midiOut->push({chunkStart + h.offset, static_cast<std::uint8_t>(0x90 | kMidiOutChannel), note,
                            velocityFor(h.level)});
        noteOffAt_[v] = h.offset + noteLengthSamples_;
    }
}

void Machine::flushNoteOffs(MidiOutBuffer* out, int chunkStart, int chunkLen) noexcept {
    for (std::size_t v = 0; v < kNumVoices; ++v) {
        if (noteOffAt_[v] < 0) continue;
        if (noteOffAt_[v] < chunkLen) {
            if (out != nullptr)
                out->push({chunkStart + noteOffAt_[v], static_cast<std::uint8_t>(0x80 | kMidiOutChannel), kVoiceNote[v], 0});
            noteOffAt_[v] = -1;
        } else {
            noteOffAt_[v] -= chunkLen;
        }
    }
}

void Machine::publish(const ProcessArgs& args) noexcept {
    state_.playing.store(sequencer_.isPlaying(), std::memory_order_relaxed);
    state_.currentPattern.store(sequencer_.currentPattern(), std::memory_order_relaxed);
    state_.selectedPattern.store(sequencer_.selectedPattern(), std::memory_order_relaxed);
    state_.queuedPattern.store(sequencer_.queuedPattern(), std::memory_order_relaxed);
    state_.variation.store(sequencer_.currentVariation(), std::memory_order_relaxed);
    state_.step.store(sequencer_.currentStep(), std::memory_order_relaxed);
    state_.measure.store(sequencer_.measureCount(), std::memory_order_relaxed);
    state_.tempo.store(static_cast<float>(sequencer_.tempo()), std::memory_order_relaxed);

    if (sequencer_.selectedPattern() != lastSelectedPattern_) {
        lastSelectedPattern_ = sequencer_.selectedPattern();
        Notice n;
        n.type = Notice::Type::PatternSelected;
        n.pattern = static_cast<std::uint8_t>(lastSelectedPattern_);
        notices_.push(n);
    }

    float peakL = 0.0f, peakR = 0.0f;
    const auto& mainOut = args.out.ch[0];
    if (mainOut[0] != nullptr)
        for (int i = 0; i < args.numSamples; ++i) peakL = std::max(peakL, std::abs(mainOut[0][i]));
    if (mainOut[1] != nullptr)
        for (int i = 0; i < args.numSamples; ++i) peakR = std::max(peakR, std::abs(mainOut[1][i]));
    state_.peakLeft.store(peakL, std::memory_order_relaxed);
    state_.peakRight.store(peakR, std::memory_order_relaxed);
}

}  // namespace mw808

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Shared helpers for the plugin tests: a host stand-in that owns the audio and
// MIDI buffers and a scripted play head, random content generators, and access
// to the serialised state as XML.
//
// The JUCE message manager is created once for the whole run (TestSupport.cpp),
// on the test's main thread, so that thread counts as the message thread. No
// message loop runs: timers never fire unless a test pumps the loop itself.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "AllocSpy.h"
#include "mw808/MidiMap.h"
#include "mw808Processor.h"

namespace plugintest {

using mw808::plugin::PresetKind;
using mw808::plugin::UiState;
namespace paramid = mw808::plugin::paramid;
namespace state = mw808::plugin::state;

inline constexpr double kRate = 48000.0;
inline constexpr int kBlock = 512;

// A fresh processor (always on the heap: the object is large), prepared unless asked not to.
inline std::unique_ptr<mw808Processor> makeProcessor(double sampleRate = kRate, int blockSize = kBlock,
                                                    bool prepare = true) {
    auto p = std::make_unique<mw808Processor>();
    if (prepare) p->prepareToPlay(sampleRate, blockSize);
    return p;
}

inline juce::RangedAudioParameter& param(mw808Processor& p, const juce::String& id) {
    auto* parameter = p.apvts().getParameter(id);
    jassert(parameter != nullptr);
    return *parameter;
}

// Real-world value the audio thread sees.
inline float raw(mw808Processor& p, const juce::String& id) { return p.apvts().getRawParameterValue(id)->load(); }

// Set by real-world value, as a host or the editor would.
inline void setParam(mw808Processor& p, const juce::String& id, float value) {
    auto& parameter = param(p, id);
    parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
}

// --- play head ---------------------------------------------------------------------

struct FakePlayHead final : juce::AudioPlayHead {
    bool available = true;   // false: getPosition() returns nothing
    bool providePpq = true;
    bool provideBpm = true;
    bool playing = true;
    double ppq = 0.0;
    double bpm = 120.0;

    juce::Optional<PositionInfo> getPosition() const override {
        if (!available) return {};
        PositionInfo info;
        info.setIsPlaying(playing);
        if (providePpq) info.setPpqPosition(ppq);
        if (provideBpm) info.setBpm(bpm);
        return info;
    }
};

// --- host stand-in -------------------------------------------------------------------

// Owns the buffers a host would pass to processBlock and advances a play head.
struct Host {
    explicit Host(mw808Processor& processor, int maxSamples = 8192)
        : p(processor), capacity(maxSamples) {
        storage.setSize(std::max(2, p.getTotalNumOutputChannels()), capacity);
        midi.ensureSize(2048);  // what the VST3 and CLAP wrappers reserve (the Standalone reserves nothing)
        p.setPlayHead(&head);
    }
    ~Host() { p.setPlayHead(nullptr); }

    // Call after changing the bus layout.
    void resize() { storage.setSize(std::max(2, p.getTotalNumOutputChannels()), capacity); }

    // Process one block. MIDI queued with note()/cc() is delivered in it; MIDI
    // the plugin produced is left in `midi` afterwards.
    void process(int numSamples) {
        juce::AudioBuffer<float> view(storage.getArrayOfWritePointers(), storage.getNumChannels(), numSamples);
        if (spy) {
            const allocspy::Scope audioThread;  // counts heap activity inside processBlock only
            p.processBlock(view, midi);
        } else {
            p.processBlock(view, midi);
        }
        if (head.playing && head.providePpq)
            head.ppq += static_cast<double>(numSamples) * head.bpm / (60.0 * sampleRate);
    }

    // Process `total` samples in blocks of `blockSize`.
    void run(int total, int blockSize) {
        for (int done = 0; done < total;) {
            const int n = std::min(blockSize, total - done);
            process(n);
            midi.clear();
            done += n;
        }
    }

    void noteOn(int note, int velocity = 100, int offset = 0, int channel = 10) {
        midi.addEvent(juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(velocity)), offset);
    }
    void noteOff(int note, int offset = 0, int channel = 10) {
        midi.addEvent(juce::MidiMessage::noteOff(channel, note), offset);
    }
    void cc(int controller, int value, int offset = 0, int channel = 1) {
        midi.addEvent(juce::MidiMessage::controllerEvent(channel, controller, value), offset);
    }

    float peak(int channel, int numSamples) const {
        return storage.getMagnitude(channel, 0, numSamples);
    }
    bool allFinite(int numSamples) const {
        for (int ch = 0; ch < storage.getNumChannels(); ++ch) {
            const float* d = storage.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                if (!std::isfinite(d[i])) return false;
        }
        return true;
    }

    mw808Processor& p;
    int capacity;
    bool spy = false;  // arm the allocation spy around every processBlock call
    double sampleRate = kRate;
    juce::AudioBuffer<float> storage;
    juce::MidiBuffer midi;
    FakePlayHead head;
};

// Counts what the plugin tells its host.
struct HostListener final : juce::AudioProcessorListener {
    explicit HostListener(mw808Processor& processor) : p(processor) { p.addListener(this); }
    ~HostListener() override { p.removeListener(this); }

    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++parameterChanges; }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
        if (details.nonParameterStateChanged) ++stateChanges;
        if (details.programChanged) ++programChanges;
    }
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++gestureBegins; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++gestureEnds; }

    mw808Processor& p;
    int parameterChanges = 0, stateChanges = 0, programChanges = 0, gestureBegins = 0, gestureEnds = 0;
};

inline std::uint32_t hits(const mw808Processor& p, mw808::VoiceId v) {
    return p.playState().hits[static_cast<std::size_t>(v)].load();
}

// --- bus layouts ---------------------------------------------------------------------

// Main plus the listed individual outs (bus indices 1..11) enabled.
inline bool setLayout(mw808Processor& p, std::initializer_list<int> enabledAux) {
    juce::AudioProcessor::BusesLayout layout;
    for (int b = 0; b < mw808::kNumBuses; ++b) layout.outputBuses.add(juce::AudioChannelSet::disabled());
    layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    for (int b : enabledAux) layout.outputBuses.getReference(b) = juce::AudioChannelSet::stereo();
    return p.setBusesLayout(layout);
}

inline bool setLayoutAll(mw808Processor& p) { return setLayout(p, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}); }

// --- random content ------------------------------------------------------------------

using Rng = std::mt19937;

inline int randInt(Rng& rng, int low, int high) { return std::uniform_int_distribution<int>(low, high)(rng); }
inline float rand01(Rng& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

// A valid pattern with every field exercised (all Step bits, polymeter, reserved byte).
inline mw808::seq::Pattern randomPattern(Rng& rng) {
    mw808::seq::Pattern p;
    p.length = static_cast<std::uint8_t>(randInt(rng, 1, mw808::seq::kMaxSteps));
    p.scale = static_cast<mw808::seq::Scale>(randInt(rng, 0, static_cast<int>(mw808::seq::Scale::kNumScales) - 1));
    p.shuffle = static_cast<std::uint8_t>(randInt(rng, 0, 100));
    p.reserved = static_cast<std::uint8_t>(randInt(rng, 0, 255));
    for (auto& len : p.trackLength) len = static_cast<std::uint8_t>(randInt(rng, 0, mw808::seq::kMaxSteps));
    for (auto& variation : p.steps)
        for (auto& track : variation)
            for (auto& step : track)
                step.bits = randInt(rng, 0, 3) == 0 ? static_cast<std::uint8_t>(randInt(rng, 0, 255)) : std::uint8_t{0};
    return p;
}

inline mw808::seq::Chain randomChain(Rng& rng) {
    mw808::seq::Chain c;
    c.length = static_cast<std::uint8_t>(randInt(rng, 1, mw808::seq::kMaxChainLength));
    for (auto& e : c.entries) e = static_cast<std::uint8_t>(randInt(rng, 0, mw808::seq::kNumPatterns - 1));
    return c;
}

// Every parameter to a random value it can hold.
inline void randomiseParameters(mw808Processor& p, Rng& rng) {
    for (auto* parameter : p.getParameters()) parameter->setValueNotifyingHost(rand01(rng));
}

inline bool sameBank(const mw808::seq::PatternBank& a, const mw808::seq::PatternBank& b) {
    return std::memcmp(&a, &b, sizeof(mw808::seq::PatternBank)) == 0;
}

// --- serialised state as XML -------------------------------------------------------------

// The header JUCE's copyXmlToBinary writes: magic, text length, text, terminator.
inline constexpr std::uint32_t kXmlMagic = 0x21324356;

inline std::unique_ptr<juce::XmlElement> xmlFromState(const juce::MemoryBlock& block) {
    if (block.getSize() <= 8) return nullptr;
    const auto* bytes = static_cast<const char*>(block.getData());
    const auto length = static_cast<int>(juce::ByteOrder::littleEndianInt(bytes + 4));
    return juce::parseXML(juce::String::fromUTF8(bytes + 8, std::min(length, static_cast<int>(block.getSize()) - 8)));
}

inline juce::MemoryBlock stateFromXml(const juce::XmlElement& xml) {
    const juce::String text = xml.toString(juce::XmlElement::TextFormat().singleLine());
    const auto utf8 = text.toRawUTF8();
    const auto length = static_cast<std::uint32_t>(std::strlen(utf8));
    juce::MemoryOutputStream out;
    out.writeInt(static_cast<int>(kXmlMagic));
    out.writeInt(static_cast<int>(length));
    out.write(utf8, length);
    out.writeByte(0);
    return out.getMemoryBlock();
}

inline juce::MemoryBlock saveState(mw808Processor& p) {
    juce::MemoryBlock block;
    p.getStateInformation(block);
    return block;
}

inline void loadState(mw808Processor& p, const juce::MemoryBlock& block) {
    p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
}

// A directory under the build tree for files the tests write, emptied first.
juce::File scratchDirectory(const juce::String& name);

}  // namespace plugintest

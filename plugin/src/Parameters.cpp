// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "Parameters.h"

#include <algorithm>
#include <cmath>

namespace mw808::plugin {

namespace {

using paramid::VoiceParam;

juce::ParameterID pid(const std::string& id) { return {juce::String(id), paramid::kParameterVersion}; }

juce::String voiceName(int v, const char* what) {
    const auto& info = kVoiceInfo[static_cast<std::size_t>(v)];
    return juce::String(std::string(info.code)) + " " + what;
}

juce::String voiceName(int v, std::string_view what) {
    const auto& info = kVoiceInfo[static_cast<std::size_t>(v)];
    return juce::String(std::string(info.code)) + " " + juce::String(std::string(what));
}

std::unique_ptr<juce::AudioParameterFloat> unitParam(const std::string& id, const juce::String& name, float def) {
    return std::make_unique<juce::AudioParameterFloat>(
        pid(id), name, juce::NormalisableRange<float>(0.0f, 1.0f), def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 3); }));
}

std::unique_ptr<juce::AudioParameterFloat> rangedParam(const std::string& id, const juce::String& name, float low,
                                                       float high, double step, float def, const char* label,
                                                       int decimals) {
    return std::make_unique<juce::AudioParameterFloat>(
        pid(id), name, makeExactRange(low, high, step), def,
        juce::AudioParameterFloatAttributes()
            .withLabel(label)
            .withStringFromValueFunction([decimals](float v, int) {
                // Never show "-0.00".
                const juce::String text(static_cast<double>(v), decimals);
                return text.containsOnly("-0.") ? text.removeCharacters("-") : text;
            }));
}

std::unique_ptr<BoolParameter> boolParam(const std::string& id, const juce::String& name, bool def) {
    return std::make_unique<BoolParameter>(pid(id), name, def);
}

std::unique_ptr<juce::AudioParameterChoice> choiceParam(const std::string& id, const juce::String& name,
                                                        const juce::StringArray& choices, int def) {
    return std::make_unique<juce::AudioParameterChoice>(pid(id), name, choices, def);
}

juce::StringArray outputChoices() {
    juce::StringArray c;
    c.add("Main");
    for (int i = 2; i <= paramid::kNumOutputChoices; ++i) c.add("Out " + juce::String(i));
    return c;
}

juce::StringArray bassChannelChoices() {
    juce::StringArray c;
    c.add("Off");
    for (int i = 1; i <= 16; ++i) c.add(juce::String(i));
    return c;
}

std::vector<std::string> buildAllIds() {
    std::vector<std::string> ids;
    ids.reserve(static_cast<std::size_t>(paramid::totalCount()));
    for (int v = 0; v < kNumVoices; ++v)
        for (int k = 0; k < paramid::kNumVoiceParamKinds; ++k)
            if (paramid::voiceHas(v, static_cast<VoiceParam>(k))) ids.push_back(paramid::voice(v, static_cast<VoiceParam>(k)));
    for (const char* id : paramid::kGlobalIds) ids.emplace_back(id);
    return ids;
}

std::vector<std::string> buildKitIds() {
    std::vector<std::string> ids;
    for (int v = 0; v < kNumVoices; ++v)
        for (auto k : {VoiceParam::Level, VoiceParam::Tune, VoiceParam::Decay, VoiceParam::Tone, VoiceParam::Shape,
                       VoiceParam::Pan, VoiceParam::Drive, VoiceParam::Out})
            if (paramid::voiceHas(v, k)) ids.push_back(paramid::voice(v, k));
    for (const char* id : {paramid::accent, paramid::masterVolume, paramid::analogOut, paramid::masterDrive,
                           paramid::masterComp, paramid::limiter})
        ids.emplace_back(id);
    return ids;
}

bool asBool(const std::atomic<float>* p) noexcept { return p->load(std::memory_order_relaxed) >= 0.5f; }
float asFloat(const std::atomic<float>* p) noexcept { return p->load(std::memory_order_relaxed); }
int asIndex(const std::atomic<float>* p, int count) noexcept {
    const float v = p->load(std::memory_order_relaxed);
    const int i = static_cast<int>(v + 0.5f);
    return i < 0 ? 0 : (i >= count ? count - 1 : i);
}

}  // namespace

juce::NormalisableRange<float> makeExactRange(float low, float high, double step) {
    const auto snap = [step](float start, float end, float v) {
        if (step > 0.0) {
            const double steps = std::floor((static_cast<double>(v) - static_cast<double>(start)) / step + 0.5);
            v = static_cast<float>(static_cast<double>(start) + step * steps);
        }
        return v < start ? start : (v > end ? end : v);
    };
    const auto from01 = [](float start, float end, float n) {
        return static_cast<float>(static_cast<double>(start) +
                                  (static_cast<double>(end) - static_cast<double>(start)) * static_cast<double>(n));
    };
    const auto to01 = [](float start, float end, float v) {
        const double n = (static_cast<double>(v) - static_cast<double>(start)) /
                         (static_cast<double>(end) - static_cast<double>(start));
        return static_cast<float>(n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n));
    };
    juce::NormalisableRange<float> range(low, high, from01, to01, snap);
    range.interval = static_cast<float>(step);
    return range;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const juce::StringArray outputs = outputChoices();

    for (int v = 0; v < kNumVoices; ++v) {
        const auto& info = kVoiceInfo[static_cast<std::size_t>(v)];
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(
            "voice_" + juce::String(std::string(info.id)), juce::String(std::string(info.name)), " | ");

        group->addChild(unitParam(paramid::voice(v, VoiceParam::Level), voiceName(v, "Level"), 0.8f));
        group->addChild(rangedParam(paramid::voice(v, VoiceParam::Tune), voiceName(v, "Tune"), -12.0f, 12.0f, 0.01,
                                    0.0f, "st", 2));
        group->addChild(unitParam(paramid::voice(v, VoiceParam::Decay), voiceName(v, "Decay"), info.defaults.decay));
        if (paramid::voiceHas(v, VoiceParam::Tone))
            group->addChild(
                unitParam(paramid::voice(v, VoiceParam::Tone), voiceName(v, info.toneName), info.defaults.tone));
        if (paramid::voiceHas(v, VoiceParam::Shape))
            group->addChild(
                unitParam(paramid::voice(v, VoiceParam::Shape), voiceName(v, info.shapeName), info.defaults.shape));
        group->addChild(
            rangedParam(paramid::voice(v, VoiceParam::Pan), voiceName(v, "Pan"), -1.0f, 1.0f, 0.0, 0.0f, "", 2));
        group->addChild(unitParam(paramid::voice(v, VoiceParam::Drive), voiceName(v, "Drive"), 0.0f));
        group->addChild(boolParam(paramid::voice(v, VoiceParam::Mute), voiceName(v, "Mute"), false));
        group->addChild(boolParam(paramid::voice(v, VoiceParam::Solo), voiceName(v, "Solo"), false));
        group->addChild(choiceParam(paramid::voice(v, VoiceParam::Out), voiceName(v, "Output"), outputs, 0));

        layout.add(std::move(group));
    }

    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("master", "Master", " | ");
        // 0.01 dB grid: 0 dB has no exact image under a float normalised value
        // on a -60..+12 range, the grid makes the default (and every readout) exact.
        group->addChild(rangedParam(paramid::masterVolume, "Master Volume", -60.0f, 12.0f, 0.01, 0.0f, "dB", 2));
        group->addChild(unitParam(paramid::accent, "Accent", 0.7f));
        group->addChild(boolParam(paramid::analogOut, "Analog Output Stage", true));
        group->addChild(unitParam(paramid::masterDrive, "Master Drive", 0.0f));
        group->addChild(unitParam(paramid::masterComp, "Master Comp", 0.0f));
        group->addChild(boolParam(paramid::limiter, "Limiter", true));
        // Quality and variance rebuild the voices when they change (which
        // silences them), so they are settings, not automation targets.
        group->addChild(std::make_unique<juce::AudioParameterChoice>(
            pid(paramid::quality), "Quality", juce::StringArray{"Eco", "Standard", "High"}, 1,
            juce::AudioParameterChoiceAttributes().withAutomatable(false)));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            pid(paramid::variance), "Analog Variance", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withStringFromValueFunction(
                [](float v, int) { return juce::String(v, 3); })));
        layout.add(std::move(group));
    }

    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("sequencer", "Sequencer", " | ");
        group->addChild(choiceParam(paramid::seqMode, "Sequencer Mode",
                                    juce::StringArray{"Off", "Host Sync", "Internal", "Host Note"}, 1));
        group->addChild(boolParam(paramid::seqRun, "Run", false));
        group->addChild(rangedParam(paramid::tempo, "Tempo", 40.0f, 300.0f, 0.1, 120.0f, "BPM", 1));
        group->addChild(unitParam(paramid::shuffleDepth, "Shuffle Depth", 1.0f));
        group->addChild(rangedParam(paramid::flamTime, "Flam Time", 0.0f, 60.0f, 0.0, 24.0f, "ms", 1));
        group->addChild(choiceParam(paramid::variation, "Variation", juce::StringArray{"A", "AB", "B"}, 0));
        group->addChild(choiceParam(paramid::autoFill, "Auto Fill",
                                    juce::StringArray{"Manual", "16", "12", "8", "4", "2"}, 0));
        group->addChild(choiceParam(paramid::fillSlot, "Fill Pattern", juce::StringArray{"1", "2", "3", "4"}, 0));
        group->addChild(boolParam(paramid::chain, "Chain", false));
        group->addChild(boolParam(paramid::record, "Record", false));
        layout.add(std::move(group));
    }

    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("midi", "MIDI", " | ");
        group->addChild(boolParam(paramid::velocitySens, "Velocity Sensitive", true));
        group->addChild(choiceParam(paramid::bassChannel, "Bass Channel", bassChannelChoices(), 0));
        group->addChild(boolParam(paramid::midiOut, "MIDI Out", false));
        layout.add(std::move(group));
    }

    return layout;
}

const std::vector<std::string>& allParameterIds() {
    static const std::vector<std::string> ids = buildAllIds();
    return ids;
}

const std::vector<std::string>& kitParameterIds() {
    static const std::vector<std::string> ids = buildKitIds();
    return ids;
}

Parameters::Parameters(juce::AudioProcessorValueTreeState& apvts) {
    const auto raw = [&apvts](const std::string& id) {
        auto* p = apvts.getRawParameterValue(juce::String(id));
        jassert(p != nullptr);
        return p;
    };

    for (int v = 0; v < kNumVoices; ++v) {
        auto& r = voices_[static_cast<std::size_t>(v)];
        r.level = raw(paramid::voice(v, VoiceParam::Level));
        r.tune = raw(paramid::voice(v, VoiceParam::Tune));
        r.decay = raw(paramid::voice(v, VoiceParam::Decay));
        if (paramid::voiceHas(v, VoiceParam::Tone)) r.tone = raw(paramid::voice(v, VoiceParam::Tone));
        if (paramid::voiceHas(v, VoiceParam::Shape)) r.shape = raw(paramid::voice(v, VoiceParam::Shape));
        r.pan = raw(paramid::voice(v, VoiceParam::Pan));
        r.drive = raw(paramid::voice(v, VoiceParam::Drive));
        r.mute = raw(paramid::voice(v, VoiceParam::Mute));
        r.solo = raw(paramid::voice(v, VoiceParam::Solo));
        r.out = raw(paramid::voice(v, VoiceParam::Out));
    }

    masterVolume_ = raw(paramid::masterVolume);
    accent_ = raw(paramid::accent);
    analogOut_ = raw(paramid::analogOut);
    masterDrive_ = raw(paramid::masterDrive);
    masterComp_ = raw(paramid::masterComp);
    limiter_ = raw(paramid::limiter);
    quality_ = raw(paramid::quality);
    variance_ = raw(paramid::variance);
    seqMode_ = raw(paramid::seqMode);
    seqRun_ = raw(paramid::seqRun);
    tempo_ = raw(paramid::tempo);
    shuffleDepth_ = raw(paramid::shuffleDepth);
    flamTime_ = raw(paramid::flamTime);
    variation_ = raw(paramid::variation);
    autoFill_ = raw(paramid::autoFill);
    fillSlot_ = raw(paramid::fillSlot);
    chain_ = raw(paramid::chain);
    record_ = raw(paramid::record);
    velocitySens_ = raw(paramid::velocitySens);
    bassChannel_ = raw(paramid::bassChannel);
    midiOut_ = raw(paramid::midiOut);
}

void Parameters::snapshot(MachineParams& out) const noexcept {
    for (std::size_t v = 0; v < kNumVoices; ++v) {
        const auto& r = voices_[v];
        const auto& defaults = kVoiceInfo[v].defaults;

        auto& voice = out.engine.voice[v];
        voice.tune = asFloat(r.tune);
        voice.decay = asFloat(r.decay);
        voice.tone = r.tone != nullptr ? asFloat(r.tone) : defaults.tone;
        voice.shape = r.shape != nullptr ? asFloat(r.shape) : defaults.shape;

        auto& channel = out.engine.channel[v];
        channel.level = asFloat(r.level);
        channel.pan = asFloat(r.pan);
        channel.drive = asFloat(r.drive);
        channel.output = static_cast<std::uint8_t>(asIndex(r.out, kNumBuses));

        out.mute[v] = asBool(r.mute);
        out.solo[v] = asBool(r.solo);
    }

    out.engine.master.volumeDb = asFloat(masterVolume_);
    out.engine.master.analogOut = asBool(analogOut_);
    out.engine.master.drive = asFloat(masterDrive_);
    out.engine.master.comp = asFloat(masterComp_);
    out.engine.master.limiter = asBool(limiter_);

    out.quality = static_cast<Quality>(asIndex(quality_, 3));
    out.variance = asFloat(variance_);

    out.seq.mode = static_cast<seq::Mode>(asIndex(seqMode_, 4));
    out.seq.run = asBool(seqRun_);
    out.seq.internalBpm = static_cast<double>(asFloat(tempo_));
    out.seq.accent = asFloat(accent_);
    out.seq.flamMs = asFloat(flamTime_);
    out.seq.swingScale = asFloat(shuffleDepth_);
    out.seq.variation = static_cast<seq::VariationMode>(asIndex(variation_, 3));
    out.seq.autoFill = static_cast<seq::AutoFill>(asIndex(autoFill_, 6));
    out.seq.fillSlot = static_cast<std::uint8_t>(asIndex(fillSlot_, 4));
    out.seq.chain = asBool(chain_);
    out.seq.record = asBool(record_);

    out.midi.velocitySensitive = asBool(velocitySens_);
    out.midi.bassChannel = static_cast<std::uint8_t>(asIndex(bassChannel_, 17));
    out.midi.midiOut = asBool(midiOut_);
}

std::vector<std::pair<std::string, float>> Parameters::kitValues(const factory::Kit& kit) {
    std::vector<std::pair<std::string, float>> values;
    values.reserve(kitParameterIds().size());
    for (int v = 0; v < kNumVoices; ++v) {
        const auto i = static_cast<std::size_t>(v);
        const auto& voice = kit.voice[i];
        const auto& channel = kit.channel[i];
        values.emplace_back(paramid::voice(v, VoiceParam::Level), channel.level);
        values.emplace_back(paramid::voice(v, VoiceParam::Tune), voice.tune);
        values.emplace_back(paramid::voice(v, VoiceParam::Decay), voice.decay);
        if (paramid::voiceHas(v, VoiceParam::Tone)) values.emplace_back(paramid::voice(v, VoiceParam::Tone), voice.tone);
        if (paramid::voiceHas(v, VoiceParam::Shape))
            values.emplace_back(paramid::voice(v, VoiceParam::Shape), voice.shape);
        values.emplace_back(paramid::voice(v, VoiceParam::Pan), channel.pan);
        values.emplace_back(paramid::voice(v, VoiceParam::Drive), channel.drive);
        values.emplace_back(paramid::voice(v, VoiceParam::Out),
                            static_cast<float>(std::min<int>(channel.output, kNumBuses - 1)));
    }
    values.emplace_back(paramid::accent, kit.accent);
    values.emplace_back(paramid::masterVolume, kit.master.volumeDb);
    values.emplace_back(paramid::analogOut, kit.master.analogOut ? 1.0f : 0.0f);
    values.emplace_back(paramid::masterDrive, kit.master.drive);
    values.emplace_back(paramid::masterComp, kit.master.comp);
    values.emplace_back(paramid::limiter, kit.master.limiter ? 1.0f : 0.0f);
    return values;
}

}  // namespace mw808::plugin

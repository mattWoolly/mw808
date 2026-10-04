// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The parameter layout (docs/design/plugin.md §2) and the per-block snapshot.
// The id list below is written out by hand on purpose: it is the frozen
// contract with saved sessions, so the layout is checked against it, not
// against the code that generates it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "TestSupport.h"

using namespace plugintest;
using Catch::Approx;

namespace {

enum class Kind { Float, Bool, Choice };

struct Expected {
    std::string id;
    std::string name;
    Kind kind = Kind::Float;
    float low = 0.0f, high = 1.0f, def = 0.0f;
    float step = 0.0f;
    int numChoices = 0;
};

struct VoiceSpec {
    const char* prefix;
    const char* code;
    const char* tone;   // nullptr: no tone parameter
    const char* shape;  // nullptr: no shape parameter
};

const VoiceSpec kVoices[16] = {
    {"bd", "BD", "Tone", "Sweep"},  {"sd", "SD", "Tone", "Snappy"}, {"lt", "LT", nullptr, "Noise"},
    {"mt", "MT", nullptr, "Noise"}, {"ht", "HT", nullptr, "Noise"}, {"lc", "LC", nullptr, nullptr},
    {"mc", "MC", nullptr, nullptr}, {"hc", "HC", nullptr, nullptr}, {"rs", "RS", nullptr, nullptr},
    {"cl", "CL", nullptr, nullptr}, {"cp", "CP", "Tone", "Reverb"}, {"ma", "MA", nullptr, nullptr},
    {"cb", "CB", nullptr, nullptr}, {"cy", "CY", "Tone", nullptr},  {"oh", "OH", nullptr, nullptr},
    {"ch", "CH", nullptr, nullptr},
};

std::vector<Expected> expectedLayout() {
    std::vector<Expected> e;
    for (int v = 0; v < 16; ++v) {
        const VoiceSpec& s = kVoices[v];
        const auto& defaults = mw808::kVoiceInfo[static_cast<std::size_t>(v)].defaults;
        const std::string p = s.prefix;
        const std::string c = s.code;
        e.push_back({p + "_level", c + " Level", Kind::Float, 0.0f, 1.0f, 0.8f});
        e.push_back({p + "_tune", c + " Tune", Kind::Float, -12.0f, 12.0f, 0.0f, 0.01f});
        e.push_back({p + "_decay", c + " Decay", Kind::Float, 0.0f, 1.0f, defaults.decay});
        if (s.tone != nullptr) e.push_back({p + "_tone", c + " " + s.tone, Kind::Float, 0.0f, 1.0f, defaults.tone});
        if (s.shape != nullptr)
            e.push_back({p + "_shape", c + " " + s.shape, Kind::Float, 0.0f, 1.0f, defaults.shape});
        e.push_back({p + "_pan", c + " Pan", Kind::Float, -1.0f, 1.0f, 0.0f});
        e.push_back({p + "_drive", c + " Drive", Kind::Float, 0.0f, 1.0f, 0.0f});
        e.push_back({p + "_mute", c + " Mute", Kind::Bool, 0.0f, 1.0f, 0.0f});
        e.push_back({p + "_solo", c + " Solo", Kind::Bool, 0.0f, 1.0f, 0.0f});
        e.push_back({p + "_out", c + " Output", Kind::Choice, 0.0f, 11.0f, 0.0f, 0.0f, 12});
    }
    e.push_back({"master_volume", "Master Volume", Kind::Float, -60.0f, 12.0f, 0.0f, 0.01f});
    e.push_back({"accent", "Accent", Kind::Float, 0.0f, 1.0f, 0.7f});
    e.push_back({"analog_out", "Analog Output Stage", Kind::Bool, 0.0f, 1.0f, 1.0f});
    e.push_back({"master_drive", "Master Drive", Kind::Float, 0.0f, 1.0f, 0.0f});
    e.push_back({"master_comp", "Master Comp", Kind::Float, 0.0f, 1.0f, 0.0f});
    e.push_back({"limiter", "Limiter", Kind::Bool, 0.0f, 1.0f, 1.0f});
    e.push_back({"quality", "Quality", Kind::Choice, 0.0f, 2.0f, 1.0f, 0.0f, 3});
    e.push_back({"variance", "Analog Variance", Kind::Float, 0.0f, 1.0f, 0.0f});
    e.push_back({"seq_mode", "Sequencer Mode", Kind::Choice, 0.0f, 3.0f, 1.0f, 0.0f, 4});
    e.push_back({"seq_run", "Run", Kind::Bool, 0.0f, 1.0f, 0.0f});
    e.push_back({"tempo", "Tempo", Kind::Float, 40.0f, 300.0f, 120.0f, 0.1f});
    e.push_back({"shuffle_depth", "Shuffle Depth", Kind::Float, 0.0f, 1.0f, 1.0f});
    e.push_back({"flam_time", "Flam Time", Kind::Float, 0.0f, 60.0f, 24.0f});
    e.push_back({"variation", "Variation", Kind::Choice, 0.0f, 2.0f, 0.0f, 0.0f, 3});
    e.push_back({"auto_fill", "Auto Fill", Kind::Choice, 0.0f, 5.0f, 0.0f, 0.0f, 6});
    e.push_back({"fill_slot", "Fill Pattern", Kind::Choice, 0.0f, 3.0f, 0.0f, 0.0f, 4});
    e.push_back({"chain", "Chain", Kind::Bool, 0.0f, 1.0f, 0.0f});
    e.push_back({"record", "Record", Kind::Bool, 0.0f, 1.0f, 0.0f});
    e.push_back({"velocity_sens", "Velocity Sensitive", Kind::Bool, 0.0f, 1.0f, 1.0f});
    e.push_back({"bass_channel", "Bass Channel", Kind::Choice, 0.0f, 16.0f, 0.0f, 0.0f, 17});
    e.push_back({"midi_out", "MIDI Out", Kind::Bool, 0.0f, 1.0f, 0.0f});
    return e;
}

juce::StringArray choicesOf(mw808Processor& p, const char* id) {
    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p.apvts().getParameter(id));
    REQUIRE(choice != nullptr);
    return choice->choices;
}

}  // namespace

TEST_CASE("parameters: the layout is the frozen list of plugin.md", "[plugin][parameters]") {
    auto p = makeProcessor(kRate, kBlock, false);
    const auto expected = expectedLayout();
    const auto& params = p->getParameters();

    REQUIRE(expected.size() == 159U);
    REQUIRE(params.size() == static_cast<int>(expected.size()));
    REQUIRE(mw808::plugin::paramid::totalCount() == 159);
    REQUIRE(mw808::plugin::allParameterIds().size() == expected.size());

    std::set<std::string> seen;
    for (int i = 0; i < params.size(); ++i) {
        const Expected& e = expected[static_cast<std::size_t>(i)];
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(params[i]);
        REQUIRE(ranged != nullptr);
        INFO("parameter " << i << " expected id " << e.id);

        // Host order, id, version hint, name.
        CHECK(ranged->paramID.toStdString() == e.id);
        CHECK(mw808::plugin::allParameterIds()[static_cast<std::size_t>(i)] == e.id);
        CHECK(ranged->getVersionHint() == 1);
        CHECK(ranged->getName(64).toStdString() == e.name);
        CHECK(seen.insert(ranged->paramID.toStdString()).second);  // unique
        CHECK(p->apvts().getParameter(juce::String(e.id)) == ranged);

        // Range and default.
        const auto& range = ranged->getNormalisableRange();
        CHECK(range.start == Approx(e.low).margin(1.0e-6));
        CHECK(range.end == Approx(e.high).margin(1.0e-6));
        CHECK(ranged->convertFrom0to1(ranged->getDefaultValue()) == Approx(e.def).margin(1.0e-6));
        CHECK(ranged->convertFrom0to1(0.0f) == Approx(e.low).margin(1.0e-6));
        CHECK(ranged->convertFrom0to1(1.0f) == Approx(e.high).margin(1.0e-6));

        switch (e.kind) {
            case Kind::Bool:
                CHECK(dynamic_cast<juce::AudioParameterBool*>(ranged) != nullptr);
                CHECK(ranged->isBoolean());
                break;
            case Kind::Choice: {
                auto* choice = dynamic_cast<juce::AudioParameterChoice*>(ranged);
                REQUIRE(choice != nullptr);
                CHECK(choice->choices.size() == e.numChoices);
                break;
            }
            case Kind::Float:
                CHECK(dynamic_cast<juce::AudioParameterFloat*>(ranged) != nullptr);
                CHECK(range.interval == Approx(e.step).margin(1.0e-7));
                break;
        }
    }

    // Choice labels, in the order the core enums use.
    CHECK(choicesOf(*p, "seq_mode") == juce::StringArray{"Off", "Host Sync", "Internal", "Host Note"});
    CHECK(choicesOf(*p, "quality") == juce::StringArray{"Eco", "Standard", "High"});
    CHECK(choicesOf(*p, "variation") == juce::StringArray{"A", "AB", "B"});
    CHECK(choicesOf(*p, "auto_fill") == juce::StringArray{"Manual", "16", "12", "8", "4", "2"});
    CHECK(choicesOf(*p, "fill_slot") == juce::StringArray{"1", "2", "3", "4"});
    const auto outs = choicesOf(*p, "bd_out");
    CHECK(outs[0] == "Main");
    CHECK(outs[1] == "Out 2");
    CHECK(outs[11] == "Out 12");
    const auto bass = choicesOf(*p, "bass_channel");
    CHECK(bass[0] == "Off");
    CHECK(bass[1] == "1");
    CHECK(bass[16] == "16");
}

TEST_CASE("parameters: a fresh instance holds factory preset 0", "[plugin][parameters]") {
    auto p = makeProcessor(kRate, kBlock, false);
    const auto& preset = mw808::factory::preset(0);

    // Kit parameters come from the preset's kit, tempo from the preset.
    for (const auto& [id, value] : mw808::plugin::Parameters::kitValues(mw808::factory::kit(preset.kit))) {
        INFO(id);
        auto& parameter = param(*p, juce::String(id));
        CHECK(raw(*p, juce::String(id)) == Approx(parameter.convertFrom0to1(parameter.convertTo0to1(value))).margin(1.0e-6));
    }
    CHECK(raw(*p, "tempo") == Approx(preset.tempo).margin(0.051));

    // Everything else sits at its declared default.
    std::set<std::string> kit(mw808::plugin::kitParameterIds().begin(), mw808::plugin::kitParameterIds().end());
    for (const auto& e : expectedLayout()) {
        if (kit.count(e.id) != 0 || e.id == "tempo") continue;
        INFO(e.id);
        CHECK(raw(*p, juce::String(e.id)) == Approx(e.def).margin(1.0e-6));
    }

    // The pattern memory and selection are the preset's too.
    auto bank = std::make_unique<mw808::seq::PatternBank>();
    mw808::factory::buildBank(preset.bank, *bank);
    CHECK(sameBank(p->patterns().bank(), *bank));
    CHECK(p->patterns().selectedPattern() == preset.pattern);
    CHECK(p->getCurrentProgram() == 0);
}

TEST_CASE("parameters: every value a parameter can hold survives normalise and back", "[plugin][parameters]") {
    // State restore depends on this: a saved value goes through the normalised
    // form on its way back in and has to come out bit-identical.
    auto p = makeProcessor(kRate, kBlock, false);
    Rng rng(20261002U);
    for (auto* base : p->getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(base);
        REQUIRE(ranged != nullptr);
        int mismatches = 0;
        for (int i = 0; i < 20000; ++i) {
            const float held = ranged->convertFrom0to1(rand01(rng));
            const float again = ranged->convertFrom0to1(ranged->convertTo0to1(held));
            if (std::memcmp(&held, &again, sizeof(float)) != 0) ++mismatches;
        }
        INFO(ranged->paramID);
        CHECK(mismatches == 0);
    }
}

TEST_CASE("parameters: switches report exactly 0 or 1 to the host", "[plugin][parameters]") {
    auto p = makeProcessor(kRate, kBlock, false);
    auto& mute = param(*p, "bd_mute");
    for (float poked : {0.37f, 0.5f, 0.93f, 0.0f, 0.49f, 1.0f}) {
        mute.setValueNotifyingHost(poked);
        const float expected = poked >= 0.5f ? 1.0f : 0.0f;
        CHECK(mute.getValue() == expected);
        CHECK(raw(*p, "bd_mute") == expected);
    }
}

TEST_CASE("parameters: the block snapshot carries every parameter into MachineParams", "[plugin][parameters]") {
    auto p = makeProcessor(kRate, kBlock, false);
    Rng rng(808U);

    for (int round = 0; round < 25; ++round) {
        randomiseParameters(*p, rng);
        p->setUnitSeed(static_cast<std::uint32_t>(rng()));

        mw808::MachineParams m;
        p->makeSnapshot(m);

        for (int v = 0; v < mw808::kNumVoices; ++v) {
            const auto i = static_cast<std::size_t>(v);
            const std::string prefix = kVoices[v].prefix;
            const auto id = [&prefix](const char* suffix) { return juce::String(prefix + "_" + suffix); };
            INFO("voice " << prefix);

            CHECK(m.engine.channel[i].level == raw(*p, id("level")));
            CHECK(m.engine.voice[i].tune == raw(*p, id("tune")));
            CHECK(m.engine.voice[i].decay == raw(*p, id("decay")));
            CHECK(m.engine.voice[i].tone ==
                  (kVoices[v].tone != nullptr ? raw(*p, id("tone")) : mw808::kVoiceInfo[i].defaults.tone));
            CHECK(m.engine.voice[i].shape ==
                  (kVoices[v].shape != nullptr ? raw(*p, id("shape")) : mw808::kVoiceInfo[i].defaults.shape));
            CHECK(m.engine.channel[i].pan == raw(*p, id("pan")));
            CHECK(m.engine.channel[i].drive == raw(*p, id("drive")));
            CHECK(m.mute[i] == (raw(*p, id("mute")) >= 0.5f));
            CHECK(m.solo[i] == (raw(*p, id("solo")) >= 0.5f));
            CHECK(static_cast<int>(m.engine.channel[i].output) == static_cast<int>(raw(*p, id("out"))));
            CHECK(m.engine.channel[i].output < mw808::kNumBuses);
        }

        CHECK(m.engine.master.volumeDb == raw(*p, "master_volume"));
        CHECK(m.engine.master.analogOut == (raw(*p, "analog_out") >= 0.5f));
        CHECK(m.engine.master.drive == raw(*p, "master_drive"));
        CHECK(m.engine.master.comp == raw(*p, "master_comp"));
        CHECK(m.engine.master.limiter == (raw(*p, "limiter") >= 0.5f));
        CHECK(static_cast<int>(m.quality) == static_cast<int>(raw(*p, "quality")));
        CHECK(m.variance == raw(*p, "variance"));
        CHECK(m.unitSeed == p->unitSeed());

        CHECK(static_cast<int>(m.seq.mode) == static_cast<int>(raw(*p, "seq_mode")));
        CHECK(m.seq.run == (raw(*p, "seq_run") >= 0.5f));
        CHECK(m.seq.internalBpm == static_cast<double>(raw(*p, "tempo")));
        CHECK(m.seq.accent == raw(*p, "accent"));
        CHECK(m.seq.flamMs == raw(*p, "flam_time"));
        CHECK(m.seq.swingScale == raw(*p, "shuffle_depth"));
        CHECK(static_cast<int>(m.seq.variation) == static_cast<int>(raw(*p, "variation")));
        CHECK(static_cast<int>(m.seq.autoFill) == static_cast<int>(raw(*p, "auto_fill")));
        CHECK(static_cast<int>(m.seq.fillSlot) == static_cast<int>(raw(*p, "fill_slot")));
        CHECK(m.seq.chain == (raw(*p, "chain") >= 0.5f));
        CHECK(m.seq.record == (raw(*p, "record") >= 0.5f));

        CHECK(m.midi.velocitySensitive == (raw(*p, "velocity_sens") >= 0.5f));
        CHECK(static_cast<int>(m.midi.bassChannel) == static_cast<int>(raw(*p, "bass_channel")));
        CHECK(m.midi.midiOut == (raw(*p, "midi_out") >= 0.5f));
    }
}

TEST_CASE("parameters: choice indices line up with the core enums", "[plugin][parameters]") {
    auto p = makeProcessor(kRate, kBlock, false);
    mw808::MachineParams m;

    setParam(*p, "seq_mode", 0.0f);
    p->makeSnapshot(m);
    CHECK(m.seq.mode == mw808::seq::Mode::Off);
    setParam(*p, "seq_mode", 1.0f);
    p->makeSnapshot(m);
    CHECK(m.seq.mode == mw808::seq::Mode::HostSync);
    setParam(*p, "seq_mode", 2.0f);
    p->makeSnapshot(m);
    CHECK(m.seq.mode == mw808::seq::Mode::Internal);
    setParam(*p, "seq_mode", 3.0f);
    p->makeSnapshot(m);
    CHECK(m.seq.mode == mw808::seq::Mode::HostNote);

    setParam(*p, "auto_fill", 1.0f);
    p->makeSnapshot(m);
    CHECK(m.seq.autoFill == mw808::seq::AutoFill::Every16);
    setParam(*p, "auto_fill", 5.0f);
    p->makeSnapshot(m);
    CHECK(m.seq.autoFill == mw808::seq::AutoFill::Every2);

    setParam(*p, "variation", 1.0f);
    p->makeSnapshot(m);
    CHECK(m.seq.variation == mw808::seq::VariationMode::AB);

    setParam(*p, "quality", 2.0f);
    p->makeSnapshot(m);
    CHECK(m.quality == mw808::Quality::High);

    setParam(*p, "ch_out", 11.0f);
    p->makeSnapshot(m);
    CHECK(m.engine.channel[mw808::CH].output == 11);

    setParam(*p, "bass_channel", 16.0f);
    p->makeSnapshot(m);
    CHECK(m.midi.bassChannel == 16);
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// MIDI learn (docs/design/plugin.md §5): controllers travel from the audio
// thread through the FIFO and are applied on the message thread.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestSupport.h"

using namespace plugintest;
using Catch::Approx;

namespace {

struct LearnListener final : mw808::plugin::MidiLearn::Listener {
    int changes = 0;
    void midiLearnChanged() override { ++changes; }
};

void sendCc(Host& host, int controller, int value, int channel = 1) {
    host.midi.clear();
    host.cc(controller, value, 0, channel);
    host.process(64);
    host.midi.clear();
}

}  // namespace

TEST_CASE("midi learn: learn, control, forget", "[plugin][midilearn]") {
    auto p = makeProcessor();
    Host host(*p);
    host.head.playing = false;
    auto& learn = p->midiLearn();
    LearnListener listener;
    learn.addListener(&listener);
    HostListener hostListener(*p);

    CHECK(learn.getMap().empty());
    CHECK_FALSE(learn.isLearning());
    CHECK(learn.controllerFor("bd_decay") == -1);

    // A controller that arrives before learning is armed means nothing.
    sendCc(host, 20, 100);
    p->serviceMessageThread();
    CHECK(learn.getMap().empty());

    learn.beginLearn("bd_decay");
    CHECK(learn.isLearning());
    CHECK(learn.learningParameter() == "bd_decay");
    CHECK(listener.changes == 1);

    const float before = raw(*p, "bd_decay");
    sendCc(host, 74, 127, 5);  // any channel
    // The audio thread only queued it: nothing has changed yet.
    CHECK(raw(*p, "bd_decay") == before);
    CHECK(learn.isLearning());

    p->serviceMessageThread();
    CHECK_FALSE(learn.isLearning());
    CHECK(learn.learningParameter().isEmpty());
    CHECK(learn.controllerFor("bd_decay") == 74);
    CHECK(learn.parameterFor(74) == "bd_decay");
    CHECK(raw(*p, "bd_decay") == 1.0f);  // the learning move already sets the value
    CHECK(listener.changes == 2);
    CHECK(hostListener.gestureBegins == 1);
    CHECK(hostListener.gestureEnds == 1);
    CHECK(hostListener.stateChanges == 1);  // the map is part of the session

    // From now on the controller drives the parameter.
    sendCc(host, 74, 0);
    p->serviceMessageThread();
    CHECK(raw(*p, "bd_decay") == 0.0f);
    sendCc(host, 74, 64);
    p->serviceMessageThread();
    CHECK(raw(*p, "bd_decay") == Approx(64.0f / 127.0f).margin(1.0e-6));

    // Several values in one block: the last one wins, as one change.
    const int gestures = hostListener.gestureBegins;
    host.midi.clear();
    host.cc(74, 10, 0);
    host.cc(74, 20, 5);
    host.cc(74, 30, 9);
    host.process(64);
    host.midi.clear();
    p->serviceMessageThread();
    CHECK(raw(*p, "bd_decay") == Approx(30.0f / 127.0f).margin(1.0e-6));
    CHECK(hostListener.gestureBegins == gestures + 1);

    // Other controllers do nothing.
    sendCc(host, 75, 127);
    p->serviceMessageThread();
    CHECK(raw(*p, "bd_decay") == Approx(30.0f / 127.0f).margin(1.0e-6));

    learn.forget("bd_decay");
    CHECK(learn.controllerFor("bd_decay") == -1);
    CHECK(learn.getMap().empty());
    sendCc(host, 74, 127);
    p->serviceMessageThread();
    CHECK(raw(*p, "bd_decay") == Approx(30.0f / 127.0f).margin(1.0e-6));

    learn.removeListener(&listener);
}

TEST_CASE("midi learn: binding rules", "[plugin][midilearn]") {
    auto p = makeProcessor();
    Host host(*p);
    host.head.playing = false;
    auto& learn = p->midiLearn();

    CHECK(learn.assign(10, "sd_level"));
    CHECK(learn.assign(11, "bd_mute"));
    CHECK(learn.assign(12, "bd_out"));
    CHECK(learn.assign(13, "tempo"));
    CHECK_FALSE(learn.assign(14, "no_such_parameter"));
    CHECK_FALSE(learn.assign(-1, "sd_level"));
    CHECK_FALSE(learn.assign(120, "sd_level"));  // channel-mode controllers
    CHECK_FALSE(learn.assign(128, "sd_level"));

    // One controller per parameter: re-binding moves it.
    CHECK(learn.assign(15, "sd_level"));
    CHECK(learn.controllerFor("sd_level") == 15);
    CHECK(learn.parameterFor(10).isEmpty());
    // One parameter per controller: the newer binding replaces the older.
    CHECK(learn.assign(15, "ch_level"));
    CHECK(learn.controllerFor("sd_level") == -1);
    CHECK(learn.parameterFor(15) == "ch_level");

    // Switches, choices and stepped ranges get values they can hold.
    sendCc(host, 11, 100);
    sendCc(host, 12, 127);
    sendCc(host, 13, 64);
    p->serviceMessageThread();
    CHECK(raw(*p, "bd_mute") == 1.0f);
    CHECK(param(*p, "bd_mute").getValue() == 1.0f);
    CHECK(raw(*p, "bd_out") == 11.0f);
    const float tempo = raw(*p, "tempo");
    CHECK(tempo == Approx(40.0 + 260.0 * 64.0 / 127.0).margin(0.051));
    CHECK(std::abs(tempo * 10.0f - std::round(tempo * 10.0f)) < 1.0e-3f);  // on the 0.1 BPM grid
    sendCc(host, 11, 20);
    p->serviceMessageThread();
    CHECK(raw(*p, "bd_mute") == 0.0f);

    // cancelLearn leaves the map alone.
    learn.beginLearn("cy_tone");
    learn.cancelLearn();
    CHECK_FALSE(learn.isLearning());
    sendCc(host, 50, 60);
    p->serviceMessageThread();
    CHECK(learn.controllerFor("cy_tone") == -1);
    learn.beginLearn("no_such_parameter");
    CHECK_FALSE(learn.isLearning());

    learn.forgetController(11);
    CHECK(learn.controllerFor("bd_mute") == -1);
    CHECK(learn.getMap().size() == 3U);
    learn.clear();
    CHECK(learn.getMap().empty());
}

TEST_CASE("midi learn: the map is saved with the session", "[plugin][midilearn]") {
    auto p = makeProcessor();
    p->midiLearn().assign(1, "master_volume");
    p->midiLearn().assign(7, "bd_level");
    p->midiLearn().assign(119, "ch_decay");
    const auto saved = saveState(*p);

    auto q = makeProcessor();
    q->midiLearn().assign(33, "cy_tone");  // replaced by the load
    LearnListener listener;
    q->midiLearn().addListener(&listener);
    loadState(*q, saved);
    CHECK(listener.changes == 1);
    const auto map = q->midiLearn().getMap();
    REQUIRE(map.size() == 3U);
    CHECK(map[0] == std::pair<int, juce::String>{1, "master_volume"});
    CHECK(map[1] == std::pair<int, juce::String>{7, "bd_level"});
    CHECK(map[2] == std::pair<int, juce::String>{119, "ch_decay"});

    // And it works right away.
    Host host(*q);
    host.head.playing = false;
    sendCc(host, 7, 0);
    q->serviceMessageThread();
    CHECK(raw(*q, "bd_level") == 0.0f);
    q->midiLearn().removeListener(&listener);
}

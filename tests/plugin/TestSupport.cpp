// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "TestSupport.h"

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <juce_events/juce_events.h>

namespace {

// Brings JUCE up for the whole run: the message manager lives on the thread
// that runs the tests, which makes it the message thread.
class JuceEnvironment final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        juce_ = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    }
    void testRunEnded(const Catch::TestRunStats&) override { juce_.reset(); }

private:
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce_;
};

}  // namespace

CATCH_REGISTER_LISTENER(JuceEnvironment)

namespace plugintest {

juce::File scratchDirectory(const juce::String& name) {
    const juce::File root(MW808_TEST_SCRATCH_DIR);
    const juce::File dir = root.getChildFile(juce::File::createLegalFileName(name));
    // Only ever a directory of our own, under the build tree.
    if (dir.isAChildOf(root) && dir.isDirectory()) dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

}  // namespace plugintest

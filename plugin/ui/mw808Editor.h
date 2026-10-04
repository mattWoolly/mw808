// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The mw808 editor (docs/design/plugin.md §7). A 1280 x 720 content component
// is drawn through a scale transform, so the whole face resizes with a fixed
// aspect ratio between 0.6x and 2.0x; the scale is kept in the processor's
// UI state. Static layers are cached per physical scale (CachedBackground).
// A 30 Hz timer polls the processor's PlayState and each part repaints only
// what changed.
//
// Lifetime: every attachment, listener and timer is owned by the component
// that uses it and removed in its destructor, before anything it references;
// the editor can be opened, closed and resized repeatedly while audio runs.
// Nothing here is reachable from the audio thread.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GridView.h"
#include "HeaderBar.h"
#include "MixerView.h"
#include "SequencerStrip.h"
#include "UiCommon.h"
#include "VoicePanel.h"
#include "mw808LookAndFeel.h"
#include "mw808Processor.h"

class mw808Editor final : public juce::AudioProcessorEditor,
                          public juce::DragAndDropContainer,
                          private juce::Timer,
                          private mw808::plugin::MidiLearn::Listener,
                          private mw808::plugin::PatternModel::Listener,
                          private mw808::ui::EditorState::Listener {
public:
    explicit mw808Editor(mw808Processor& owner);
    ~mw808Editor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // --- for tests and tools ------------------------------------------------------
    void setView(mw808::ui::View view);
    mw808::ui::View view() const noexcept { return state_.view(); }
    // Resize the window to a scale factor of the 1280 x 720 base.
    void setScale(float scale);
    float scale() const noexcept { return scale_; }
    static constexpr int baseWidth() noexcept { return mw808::ui::kBaseWidth; }
    static constexpr int baseHeight() noexcept { return mw808::ui::kBaseHeight; }
    // Run one timer tick (what the 30 Hz timer does).
    void pollNow() { timerCallback(); }
    void showChainEditor(bool show);

private:
    void timerCallback() override;
    void midiLearnChanged() override;
    void uiStateChanged() override;
    void bankChanged() override;  // PatternModel: a state restore may carry a new UI state
    void applyView();
    void refreshLearnStates();
    void closeTransientWindows();

    mw808Processor& processor_;
    mw808LookAndFeel lnf_;
    mw808::ui::EditorState state_;
    mw808::ui::Context ctx_;

    // The fixed-size face, scaled by a transform.
    class Content : public juce::Component {
    public:
        Content() { setOpaque(true); }
        void paint(juce::Graphics& g) override { g.fillAll(mw808Colours::panel); }
    };
    Content content_;

    std::unique_ptr<mw808::ui::HeaderBar> header_;
    std::unique_ptr<mw808::ui::VoicePanel> panel_;
    std::unique_ptr<mw808::ui::GridView> grid_;
    std::unique_ptr<mw808::ui::MixerView> mixer_;
    std::unique_ptr<mw808::ui::SequencerStrip> sequencer_;
    std::unique_ptr<mw808::ui::ChainEditor> chainEditor_;
    std::unique_ptr<juce::TooltipWindow> tooltips_;

    float scale_ = 1.0f;
    bool applyingScale_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(mw808Editor)
};

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// GRID view (plugin.md §7.4): all 17 lanes x every step of the selected
// pattern, playhead column highlighted, with the step keys' click semantics.
// One component paints the whole grid; the playhead repaints two columns.

#pragma once

#include "StepEdit.h"
#include "UiCommon.h"

namespace mw808::ui {

class GridView final : public juce::Component,
                       private plugin::PatternModel::Listener,
                       private EditorState::Listener {
public:
    explicit GridView(Context& ctx);
    ~GridView() override;

    void poll(const PlayState& ps);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    void patternChanged(int index) override;
    void bankChanged() override { repaint(); }
    void selectionChanged() override { repaint(); }
    void uiStateChanged() override { repaint(); }
    void visibilityChanged() override { runningStep_ = -1; }  // poll() only runs while shown

    int selectedPattern() const { return ctx_.processor.patterns().selectedPattern(); }
    const seq::Pattern& pattern() const { return ctx_.processor.patterns().pattern(selectedPattern()); }
    int columns() const;
    juce::Rectangle<int> cellBounds(int track, int step) const;
    juce::Rectangle<int> columnBounds(int step) const;
    bool cellAt(juce::Point<int> pos, int& track, int& step) const;
    int laneAt(juce::Point<int> pos) const;  // lane header hit, -1 if none

    Context& ctx_;
    juce::Rectangle<int> headerArea_, gridArea_;
    int runningStep_ = -1;
    int shownVariation_ = 0;
    int hoverTrack_ = -1, hoverStep_ = -1;

    bool painting_ = false;
    seq::Step brush_{};
    int paintTrack_ = -1, lastPainted_ = -1;

    static constexpr int kHeaderWidth = 64;
    static constexpr int kTopRuler = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridView)
};

}  // namespace mw808::ui

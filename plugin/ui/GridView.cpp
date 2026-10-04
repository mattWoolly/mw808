// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "GridView.h"

namespace mw808::ui {

namespace C = mw808Colours;

GridView::GridView(Context& ctx) : ctx_(ctx) {
    setOpaque(true);
    ctx_.processor.patterns().addListener(this);
    ctx_.state.addListener(this);
    shownVariation_ = ctx_.editVariation();
}

GridView::~GridView() {
    ctx_.state.removeListener(this);
    ctx_.processor.patterns().removeListener(this);
}

void GridView::resized() {
    auto r = getLocalBounds().reduced(6);
    headerArea_ = r.removeFromLeft(kHeaderWidth);
    r.removeFromLeft(4);
    gridArea_ = r;
}

int GridView::columns() const {
    const auto& p = pattern();
    int cols = p.length;
    for (int t = 0; t < kNumVoices; ++t) cols = juce::jmax(cols, p.lengthOf(t));
    // Show whole pages so the layout is stable.
    return juce::jlimit(16, seq::kMaxSteps, ((cols + 15) / 16) * 16);
}

juce::Rectangle<int> GridView::columnBounds(int step) const {
    const int cols = columns();
    const auto grid = gridArea_.withTrimmedTop(kTopRuler);
    const float w = static_cast<float>(gridArea_.getWidth()) / static_cast<float>(cols);
    const int x0 = gridArea_.getX() + juce::roundToInt(w * static_cast<float>(step));
    const int x1 = gridArea_.getX() + juce::roundToInt(w * static_cast<float>(step + 1));
    return {x0, gridArea_.getY(), x1 - x0, grid.getBottom() - gridArea_.getY()};
}

juce::Rectangle<int> GridView::cellBounds(int track, int step) const {
    const auto col = columnBounds(step);
    const auto grid = gridArea_.withTrimmedTop(kTopRuler);
    const float h = static_cast<float>(grid.getHeight()) / static_cast<float>(seq::kNumTracks);
    const int y0 = grid.getY() + juce::roundToInt(h * static_cast<float>(track));
    const int y1 = grid.getY() + juce::roundToInt(h * static_cast<float>(track + 1));
    return {col.getX(), y0, col.getWidth(), y1 - y0};
}

bool GridView::cellAt(juce::Point<int> pos, int& track, int& step) const {
    const auto grid = gridArea_.withTrimmedTop(kTopRuler);
    if (!grid.contains(pos)) return false;
    const int cols = columns();
    step = juce::jlimit(0, cols - 1, static_cast<int>((pos.x - gridArea_.getX()) * cols / juce::jmax(1, gridArea_.getWidth())));
    track = juce::jlimit(0, seq::kNumTracks - 1, static_cast<int>((pos.y - grid.getY()) * seq::kNumTracks / juce::jmax(1, grid.getHeight())));
    return true;
}

int GridView::laneAt(juce::Point<int> pos) const {
    const auto header = headerArea_.withTrimmedTop(kTopRuler);
    if (!header.contains(pos)) return -1;
    return juce::jlimit(0, seq::kNumTracks - 1, static_cast<int>((pos.y - header.getY()) * seq::kNumTracks / juce::jmax(1, header.getHeight())));
}

void GridView::paint(juce::Graphics& g) {
    g.fillAll(C::panel);
    const auto& p = pattern();
    const int var = ctx_.editVariation();
    const int cols = columns();
    const int selTrack = ctx_.state.selectedTrack();
    const StepMode mode = ctx_.state.stepMode();
    const auto clip = g.getClipBounds();

    // Ruler: step numbers, page separators.
    g.setFont(mw808LookAndFeel::bold(9.0f));
    for (int s = 0; s < cols; ++s) {
        const auto col = columnBounds(s);
        if (!clip.intersects(col)) continue;
        auto ruler = col.withHeight(kTopRuler);
        const bool inPattern = s < p.length;
        g.setColour(s == runningStep_ ? C::ledRun : (inPattern ? C::creamDim : C::creamFaint));
        g.drawText(juce::String(s + 1), ruler, juce::Justification::centred, false);
    }
    // Variation / mode tag in the header's top-left.
    g.setColour(C::creamDim);
    g.setFont(mw808LookAndFeel::legend(9.0f));
    g.drawText(patternLabel(selectedPattern()) + (var == 0 ? "  A" : "  B"), headerArea_.withHeight(kTopRuler),
               juce::Justification::centredLeft, false);

    for (int t = 0; t < seq::kNumTracks; ++t) {
        const juce::Colour accent = C::trackAccent(t);
        const int len = p.lengthOf(t);
        // Lane header.
        auto lane = cellBounds(t, 0).withX(headerArea_.getX()).withWidth(headerArea_.getWidth());
        if (clip.intersects(lane)) {
            const bool selected = t == selTrack;
            mw808LookAndFeel::drawPlate(g, lane.reduced(0, 1).toFloat(), selected ? C::plateRaised.interpolatedWith(accent, 0.3f) : C::plate,
                                        selected ? accent : C::plateEdge, 2.0f);
            g.setColour(accent);
            g.fillRect(lane.getX() + 2, lane.getY() + 3, 3, lane.getHeight() - 6);
            g.setColour(C::cream);
            g.setFont(mw808LookAndFeel::bold(juce::jlimit(9.0f, 12.0f, static_cast<float>(lane.getHeight()) * 0.5f)));
            const juce::String code = t < kNumVoices ? juce::String(std::string(kVoiceInfo[static_cast<std::size_t>(t)].code)) : juce::String("AC");
            g.drawText(code, lane.reduced(9, 0), juce::Justification::centredLeft, false);
            if (t < kNumVoices && p.trackLength[static_cast<std::size_t>(t)] != 0) {
                g.setColour(C::creamDim);
                g.setFont(mw808LookAndFeel::text(9.0f));
                g.drawText(juce::String(len), lane.reduced(6, 0), juce::Justification::centredRight, false);
            }
        }
        // Cells.
        for (int s = 0; s < cols; ++s) {
            auto cell = cellBounds(t, s);
            if (!clip.intersects(cell)) continue;
            const bool inRange = s < len;
            const seq::Step step = p.at(var, t, s);
            auto inner = cell.reduced(1, 1).toFloat();
            juce::Colour face;
            if (!inRange) face = C::panelDeep;
            // Alternate beats in two inset shades; both stay clearly darker than the panel.
            else if (!step.on()) face = (s / 4) % 2 == 0 ? C::inset : C::insetAlt;
            else {
                const float lift = step.level() == seq::StepLevel::Weak ? 0.45f : step.level() == seq::StepLevel::Normal ? 0.85f : 1.0f;
                face = C::inset.interpolatedWith(accent, lift);
            }
            g.setColour(face);
            g.fillRoundedRectangle(inner, 2.0f);
            if (step.on() && step.level() == seq::StepLevel::Accent) {
                // Accent: a bright bar along the bottom, clear of the centred badge.
                auto bar = inner.reduced(inner.getWidth() * 0.18f, 0.0f);
                bar = bar.removeFromBottom(juce::jmax(2.0f, inner.getHeight() * 0.18f)).translated(0.0f, -1.5f);
                g.setColour(juce::Colours::white.withAlpha(0.55f));
                g.fillRoundedRectangle(bar, 1.0f);
            }
            if (inRange && (s % 4) == 0) {
                g.setColour(C::hairline);
                g.drawVerticalLine(cell.getX(), static_cast<float>(cell.getY()), static_cast<float>(cell.getBottom()));
            }
            if (t == hoverTrack_ && s == hoverStep_) {
                g.setColour(C::hover);
                g.fillRoundedRectangle(inner, 2.0f);
            }
            const juce::String badge = inRange ? stepedit::badge(step, mode, t) : juce::String();
            if (badge.isNotEmpty() && cell.getWidth() >= 14) {
                g.setColour(C::textOn(face));
                g.setFont(mw808LookAndFeel::bold(juce::jlimit(7.0f, 10.0f, static_cast<float>(cell.getHeight()) * 0.42f)));
                g.drawFittedText(badge, cell.reduced(1), juce::Justification::centred, 1, 0.5f);
            }
        }
    }
    // Selected lane outline.
    {
        const auto lane = cellBounds(selTrack, 0).withX(gridArea_.getX()).withWidth(gridArea_.getWidth());
        g.setColour(C::trackAccent(selTrack).withAlpha(0.6f));
        g.drawRoundedRectangle(lane.toFloat().reduced(0.5f), 2.0f, 1.0f);
    }
    // Playhead.
    if (runningStep_ >= 0 && runningStep_ < cols) {
        const auto col = columnBounds(runningStep_);
        g.setColour(C::ledRun.withAlpha(0.16f));
        g.fillRect(col);
        g.setColour(C::ledRun.withAlpha(0.8f));
        g.drawRect(col, 1);
    }
}

void GridView::poll(const PlayState& ps) {
    const bool playing = ps.playing.load(std::memory_order_relaxed);
    // Only the pattern on screen gets a playhead (not a fill or another pattern).
    const bool showing = playing && ps.currentPattern.load(std::memory_order_relaxed) == selectedPattern();
    const int step = showing ? ps.step.load(std::memory_order_relaxed) : -1;
    const int var = ctx_.editVariation();
    if (var != shownVariation_) {
        shownVariation_ = var;
        repaint();
        runningStep_ = step;
        return;
    }
    if (step == runningStep_) return;
    const int cols = columns();
    if (runningStep_ >= 0 && runningStep_ < cols) repaint(columnBounds(runningStep_));
    runningStep_ = step;
    if (runningStep_ >= 0 && runningStep_ < cols) repaint(columnBounds(runningStep_));
}

void GridView::patternChanged(int index) {
    if (index == selectedPattern()) repaint();
}

void GridView::mouseDown(const juce::MouseEvent& e) {
    int track, step;
    const int lane = laneAt(e.getPosition());
    if (lane >= 0) {
        ctx_.state.setSelectedTrack(lane);
        if (lane < kNumVoices && !e.mods.isPopupMenu()) ctx_.processor.audition(static_cast<VoiceId>(lane), e.mods.isShiftDown());
        return;
    }
    if (!cellAt(e.getPosition(), track, step)) return;
    if (step >= pattern().lengthOf(track)) return;
    if (e.mods.isPopupMenu()) {
        stepedit::showStepMenu(ctx_, selectedPattern(), ctx_.editVariation(), track, step, *this);
        return;
    }
    const StepMode mode = ctx_.state.stepMode();
    const bool shift = e.mods.isShiftDown();
    const int var = ctx_.editVariation();
    seq::Step result{};
    ctx_.processor.patterns().edit(selectedPattern(), [&](seq::Pattern& p) {
        seq::Step& s = p.at(var, track, step);
        s = result = stepedit::clicked(s, mode, track, shift);
    });
    painting_ = true;
    brush_ = result;
    paintTrack_ = track;
    lastPainted_ = step;
}

void GridView::mouseDrag(const juce::MouseEvent& e) {
    if (!painting_) return;
    int track, step;
    if (!cellAt(e.getPosition(), track, step)) return;
    track = paintTrack_;  // paint along the lane the drag started in
    if (step == lastPainted_ || step >= pattern().lengthOf(track)) return;
    lastPainted_ = step;
    const StepMode mode = ctx_.state.stepMode();
    const int var = ctx_.editVariation();
    const seq::Step brush = brush_;
    ctx_.processor.patterns().edit(selectedPattern(), [&](seq::Pattern& p) {
        seq::Step& s = p.at(var, track, step);
        s = stepedit::painted(s, brush, mode, track);
    });
}

void GridView::mouseUp(const juce::MouseEvent&) { painting_ = false; }

void GridView::mouseMove(const juce::MouseEvent& e) {
    int track = -1, step = -1;
    if (!cellAt(e.getPosition(), track, step)) { track = -1; step = -1; }
    if (track == hoverTrack_ && step == hoverStep_) return;
    if (hoverTrack_ >= 0) repaint(cellBounds(hoverTrack_, hoverStep_));
    hoverTrack_ = track;
    hoverStep_ = step;
    if (hoverTrack_ >= 0) repaint(cellBounds(hoverTrack_, hoverStep_));
}

void GridView::mouseExit(const juce::MouseEvent&) {
    if (hoverTrack_ >= 0) repaint(cellBounds(hoverTrack_, hoverStep_));
    hoverTrack_ = hoverStep_ = -1;
}

}  // namespace mw808::ui

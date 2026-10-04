// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// PANEL view (plugin.md §7.2): the left global block (TEMPO, ACCENT, MASTER
// VOLUME, sequencer MODE, plus the FLAM and SWING feel controls) and the 16
// voice strips in panel order, each with LEVEL on top, the voice's knobs
// (hardware-original knobs skirted and cream, extended knobs flat and
// dashed), a trigger lamp, MUTE / SOLO and a name plate that auditions the
// voice and selects its track.

#pragma once

#include "UiCommon.h"

namespace mw808::ui {

class NamePlate : public juce::Component, public juce::SettableTooltipClient {
public:
    NamePlate(int voice, juce::Colour accent);
    void setSelected(bool selected);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }

    std::function<void(bool accent)> onAudition;
    std::function<void()> onSelect;

private:
    int voice_;
    juce::Colour accent_;
    bool selected_ = false, hover_ = false, down_ = false;
};

class VoiceStrip : public juce::Component {
public:
    VoiceStrip(Context& ctx, int voice);
    ~VoiceStrip() override;

    void setSelected(bool selected) { plate_.setSelected(selected); }
    void flash() { lamp_.setLit(1.0f); lampAge_ = 0; }
    void tick();  // 30 Hz: fades the lamp

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Context& ctx_;
    int voice_;
    juce::Colour accent_;
    std::unique_ptr<ParamControl> level_;
    juce::OwnedArray<ParamControl> knobs_;   // hardware knobs first, then extended
    Lamp lamp_;
    int lampAge_ = 99;
    std::unique_ptr<ParamButton> mute_, solo_;
    NamePlate plate_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceStrip)
};

class GlobalBlock : public CachedBackground {
public:
    explicit GlobalBlock(Context& ctx);
    void resized() override;

private:
    void paintStatic(juce::Graphics& g) override;

    Context& ctx_;
    std::unique_ptr<ParamControl> tempo_, accent_, master_, flam_, swing_;
    std::unique_ptr<ChoiceButtons> mode_;
    juce::Rectangle<int> modeLegend_, feelTitle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalBlock)
};

class VoicePanel final : public juce::Component, private EditorState::Listener {
public:
    explicit VoicePanel(Context& ctx);
    ~VoicePanel() override;

    void poll(const PlayState& ps);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void uiStateChanged() override;
    // poll() only runs while this view is shown: re-prime the hit counters on
    // return so triggers that happened meanwhile do not flash all at once.
    void visibilityChanged() override { hitsPrimed_ = false; }

    Context& ctx_;
    GlobalBlock global_;
    juce::OwnedArray<VoiceStrip> strips_;
    std::array<std::uint32_t, kNumVoices> lastHits_{};
    bool hitsPrimed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoicePanel)
};

}  // namespace mw808::ui

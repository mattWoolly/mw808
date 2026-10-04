// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// MIXER view (plugin.md §7.5): one channel per voice — pan, drive, output
// assignment, mute, solo, level fader, name plate — and the master section
// (drive, comp, limiter, analog output stage, volume).

#pragma once

#include "UiCommon.h"
#include "VoicePanel.h"

namespace mw808::ui {

class MixerChannel : public juce::Component {
public:
    MixerChannel(Context& ctx, int voice);
    void setSelected(bool selected) { plate_.setSelected(selected); }
    void flash() { lamp_.setLit(1.0f); lampAge_ = 0; }
    void tick();
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Context& ctx_;
    int voice_;
    juce::Colour accent_;
    std::unique_ptr<ParamControl> pan_, drive_, level_;
    std::unique_ptr<ParamValueBox> out_;
    std::unique_ptr<ParamButton> mute_, solo_;
    Lamp lamp_;
    int lampAge_ = 99;
    NamePlate plate_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerChannel)
};

class MasterSection : public CachedBackground {
public:
    explicit MasterSection(Context& ctx);
    void resized() override;

private:
    void paintStatic(juce::Graphics& g) override;
    Context& ctx_;
    std::unique_ptr<ParamControl> drive_, comp_, volume_;
    std::unique_ptr<ParamButton> limiter_, analog_;
    juce::Rectangle<int> title_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterSection)
};

class MixerView final : public juce::Component, private EditorState::Listener {
public:
    explicit MixerView(Context& ctx);
    ~MixerView() override;

    void poll(const PlayState& ps);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void uiStateChanged() override;
    // poll() only runs while this view is shown: re-prime the hit counters on
    // return so triggers that happened meanwhile do not flash all at once.
    void visibilityChanged() override { hitsPrimed_ = false; }

    Context& ctx_;
    juce::OwnedArray<MixerChannel> channels_;
    MasterSection master_;
    std::array<std::uint32_t, kNumVoices> lastHits_{};
    bool hitsPrimed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

}  // namespace mw808::ui

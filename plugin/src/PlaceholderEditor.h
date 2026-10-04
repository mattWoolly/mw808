// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// A stand-in editor so the plugin opens in hosts and validators while the real
// editor (plugin/ui/mw808Editor.h) is being built: a scrolling list of every
// parameter under a banner. mw808Processor.cpp selects the editor class in one
// place.
//
// Deliberately NOT juce::GenericAudioProcessorEditor. Its rows register as
// juce::AudioProcessorListener on open and unregister on close, and JUCE's
// AudioProcessor::ParameterChangeForwarder walks that listener array WITHOUT
// its lock whenever a parameter changes. Hosts change parameters on the audio
// thread (VST3 automation), so opening or closing that editor during playback
// is a use-after-free (found by the stress test under ASan). Every control
// here talks to its parameter through an APVTS attachment, which listens to
// the parameter itself (locked) and never to the processor.

#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "mw808Processor.h"

namespace mw808::plugin {

class PlaceholderEditor final : public juce::AudioProcessorEditor {
public:
    explicit PlaceholderEditor(::mw808Processor& owner) : juce::AudioProcessorEditor(owner) {
        banner_.setText(owner.getName() + "  -  UI under construction", juce::dontSendNotification);
        banner_.setJustificationType(juce::Justification::centred);
        banner_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        banner_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff2a2a2a));
        banner_.setColour(juce::Label::textColourId, juce::Colour(0xfff0e6d2));
        addAndMakeVisible(banner_);

        auto& apvts = owner.apvts();
        for (auto* base : owner.getParameters())
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(base))
                rows_.push_back(std::make_unique<Row>(apvts, *p));
        for (auto& row : rows_) list_.addAndMakeVisible(*row);
        viewport_.setViewedComponent(&list_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);

        setResizable(true, true);
        setResizeLimits(360, 240, 1600, 1400);
        setSize(520, 640);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff1c1c1c)); }

    void resized() override {
        auto area = getLocalBounds();
        banner_.setBounds(area.removeFromTop(kBannerHeight));
        viewport_.setBounds(area);
        const int width = std::max(200, area.getWidth() - viewport_.getScrollBarThickness());
        list_.setSize(width, kRowHeight * static_cast<int>(rows_.size()));
        int y = 0;
        for (auto& row : rows_) {
            row->setBounds(0, y, width, kRowHeight);
            y += kRowHeight;
        }
    }

private:
    static constexpr int kBannerHeight = 36;
    static constexpr int kRowHeight = 28;

    using APVTS = juce::AudioProcessorValueTreeState;

    // One parameter: its name and a control of the matching kind.
    class Row final : public juce::Component {
    public:
        Row(APVTS& apvts, juce::RangedAudioParameter& p) {
            name_.setText(p.getName(64), juce::dontSendNotification);
            name_.setColour(juce::Label::textColourId, juce::Colour(0xfff0e6d2));
            addAndMakeVisible(name_);
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&p)) {
                combo_.addItemList(choice->choices, 1);
                addAndMakeVisible(combo_);
                comboAttachment_ = std::make_unique<APVTS::ComboBoxAttachment>(apvts, p.paramID, combo_);
            } else if (dynamic_cast<juce::AudioParameterBool*>(&p) != nullptr) {
                addAndMakeVisible(toggle_);
                toggleAttachment_ = std::make_unique<APVTS::ButtonAttachment>(apvts, p.paramID, toggle_);
            } else {
                slider_.setSliderStyle(juce::Slider::LinearHorizontal);
                slider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, kRowHeight - 6);
                addAndMakeVisible(slider_);
                sliderAttachment_ = std::make_unique<APVTS::SliderAttachment>(apvts, p.paramID, slider_);
            }
        }

        void resized() override {
            auto area = getLocalBounds().reduced(6, 2);
            name_.setBounds(area.removeFromLeft(area.getWidth() * 2 / 5));
            slider_.setBounds(area);
            toggle_.setBounds(area);
            combo_.setBounds(area);
        }

    private:
        juce::Label name_;
        juce::Slider slider_;
        juce::ToggleButton toggle_;
        juce::ComboBox combo_;
        // Declared after the controls, so destroyed before them.
        std::unique_ptr<APVTS::SliderAttachment> sliderAttachment_;
        std::unique_ptr<APVTS::ButtonAttachment> toggleAttachment_;
        std::unique_ptr<APVTS::ComboBoxAttachment> comboAttachment_;
    };

    juce::Label banner_;
    juce::Viewport viewport_;
    juce::Component list_;
    std::vector<std::unique_ptr<Row>> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderEditor)
};

}  // namespace mw808::plugin

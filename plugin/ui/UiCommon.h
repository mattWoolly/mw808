// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Shared editor plumbing: the base geometry, the context every component gets
// (processor, look-and-feel, session UI state), a cached-background component
// base, and the parameter widgets (knob, fader, buttons, value box) with the
// interaction contract of docs/design/plugin.md §7: drag, shift-drag fine,
// double-click reset, wheel, text entry, right-click MIDI learn.
//
// Message thread only. Every widget talks to parameters through a
// juce::ParameterAttachment, which is destroyed with the widget, before
// anything it references.

#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "mw808Colours.h"
#include "mw808LookAndFeel.h"
#include "mw808Processor.h"

namespace mw808::ui {

// --- geometry ------------------------------------------------------------------
inline constexpr int kBaseWidth = 1280;
inline constexpr int kBaseHeight = 720;
inline constexpr int kHeaderHeight = 48;
inline constexpr int kSequencerHeight = 222;
inline constexpr int kViewTop = kHeaderHeight;
inline constexpr int kViewHeight = kBaseHeight - kHeaderHeight - kSequencerHeight;  // 450
inline constexpr float kMinScale = plugin::kMinUiScale;
inline constexpr float kMaxScale = plugin::kMaxUiScale;

enum class View : int { Panel = 0, Grid = 1, Mixer = 2 };
enum class StepMode : int { Level = 0, Flam = 1, Sub = 2, Prob = 3 };

inline constexpr int kNumStepModes = 4;
inline constexpr const char* kStepModeNames[kNumStepModes] = {"LEVEL", "FLAM", "SUB", "PROB"};

// Which of the original panel's knobs a voice parameter corresponds to
// (Voices.h: BD tone/decay, SD tone/snappy, tom/conga tuning, CY tone/decay, OH decay).
inline bool isHardwareKnob(int voice, plugin::paramid::VoiceParam p) noexcept {
    using plugin::paramid::VoiceParam;
    const auto& info = kVoiceInfo[static_cast<std::size_t>(voice)];
    if (p == VoiceParam::Level) return true;
    if (p == VoiceParam::Tune) return info.panelTune;
    if (p == VoiceParam::Decay) return info.panelDecay;
    if (p == VoiceParam::Tone) return voice == BD || voice == SD || voice == CY;
    if (p == VoiceParam::Shape) return voice == SD;
    return false;
}

// --- session UI state ----------------------------------------------------------------
// The editor-side mirror of plugin::UiState (view, selected track, step-edit
// mode, page, scale). Changes are pushed to the processor so they survive the
// session, and broadcast to the editor's components.
class EditorState {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void uiStateChanged() {}
    };

    explicit EditorState(mw808Processor& p) : processor_(p), state_(p.uiState()) {
        plugin::sanitise(state_);
    }

    View view() const noexcept { return static_cast<View>(state_.view); }
    int selectedTrack() const noexcept { return state_.selectedTrack; }
    StepMode stepMode() const noexcept { return static_cast<StepMode>(state_.stepEditMode); }
    int page() const noexcept { return state_.page; }
    float scale() const noexcept { return state_.scale; }

    void setView(View v) { change([&](plugin::UiState& s) { s.view = static_cast<int>(v); }); }
    void setSelectedTrack(int t) { change([&](plugin::UiState& s) { s.selectedTrack = t; }); }
    void setStepMode(StepMode m) { change([&](plugin::UiState& s) { s.stepEditMode = static_cast<int>(m); }); }
    void setPage(int p) { change([&](plugin::UiState& s) { s.page = p; }); }
    void setScale(float sc) { change([&](plugin::UiState& s) { s.scale = sc; }); }

    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    // Adopt the processor's copy after a session restore while the editor is
    // open (the host loaded a project or undid a state change). Nothing is
    // written back. True when anything changed.
    bool syncFromProcessor() {
        plugin::UiState next = processor_.uiState();
        plugin::sanitise(next);
        next.presetNames = state_.presetNames;  // not the editor's to track
        if (next == state_) return false;
        state_ = next;
        listeners_.call([](Listener& l) { l.uiStateChanged(); });
        return true;
    }

    mw808Processor& processor() noexcept { return processor_; }

private:
    template <class Fn>
    void change(Fn&& fn) {
        plugin::UiState next = state_;
        fn(next);
        plugin::sanitise(next);
        if (next == state_) return;
        state_ = next;
        // Keep the fields the editor does not own (preset names) as the processor has them.
        plugin::UiState stored = processor_.uiState();
        stored.scale = state_.scale;
        stored.view = state_.view;
        stored.selectedTrack = state_.selectedTrack;
        stored.stepEditMode = state_.stepEditMode;
        stored.page = state_.page;
        processor_.setUiState(stored);
        listeners_.call([](Listener& l) { l.uiStateChanged(); });
    }

    mw808Processor& processor_;
    plugin::UiState state_;
    juce::ListenerList<Listener> listeners_;
};

// --- context -------------------------------------------------------------------------
class ParamControl;

struct Context {
    mw808Processor& processor;
    mw808LookAndFeel& lnf;
    EditorState& state;
    juce::Component& menuParent;  // popups are parented here so they stay inside the plugin window
    juce::Array<ParamControl*> controls{};  // every live knob / fader, for the MIDI-learn indicators

    juce::RangedAudioParameter* param(const juce::String& id) const { return processor.apvts().getParameter(id); }
    juce::RangedAudioParameter* param(const std::string& id) const { return param(juce::String(id)); }
    juce::RangedAudioParameter* param(const char* id) const { return param(juce::String(id)); }
    float raw(const char* id) const { return processor.apvts().getRawParameterValue(id)->load(); }
    int choice(const char* id) const { return static_cast<int>(raw(id) + 0.5f); }

    // The variation (0 = A, 1 = B) step edits go to: A in A mode, B in B mode,
    // and the variation that is sounding in AB mode (A when stopped).
    int editVariation() const {
        const int mode = choice(plugin::paramid::variation);
        if (mode == 2) return 1;
        if (mode == 0) return 0;
        const auto& ps = processor.playState();
        return ps.playing.load(std::memory_order_relaxed) ? juce::jlimit(0, 1, ps.variation.load(std::memory_order_relaxed)) : 0;
    }
};

// The right-click menu of every parameter control: MIDI learn / forget, reset.
void showParameterMenu(Context& ctx, juce::RangedAudioParameter& param, juce::Component& target,
                       std::function<void()> onReset);

// --- cached static layer -------------------------------------------------------------
// A component whose static artwork is rendered once per size and physical
// scale into an image, and blitted on every repaint. Subclasses draw the
// static part in paintStatic() and anything dynamic in paintDynamic().
class CachedBackground : public juce::Component {
public:
    void paint(juce::Graphics& g) final;
    void invalidateCache() { cache_ = juce::Image(); }

protected:
    virtual void paintStatic(juce::Graphics& g) = 0;
    virtual void paintDynamic(juce::Graphics&) {}

private:
    juce::Image cache_;
    float cacheScale_ = 0.0f;
    int cacheW_ = 0, cacheH_ = 0;
};

// --- parameter controls ---------------------------------------------------------------

// Knob or fader bound to one parameter. Layout: an optional legend above,
// the control, an optional value readout below (double-click to type).
class ParamControl : public juce::Component, private juce::Label::Listener {
public:
    enum class Style { Knob, Fader };

    ParamControl(Context& ctx, juce::RangedAudioParameter& param, Style style, const juce::String& legend,
                 bool hardware, juce::Colour accent = mw808Colours::arc);
    ~ParamControl() override;

    void setShowValue(bool show);
    void setLegendHeight(float h) { legendHeight_ = h; }
    void setBipolar(bool b) { bipolar_ = b; }
    void setLearnState(bool learning, bool bound);
    juce::RangedAudioParameter& parameter() noexcept { return param_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    juce::Rectangle<int> controlBounds() const noexcept { return controlArea_; }

private:
    void parameterChanged(float denormalised);
    void setNormalisedFromGesture(float norm);
    void refreshValueText();
    void labelTextChanged(juce::Label* label) override;
    void editorShown(juce::Label*, juce::TextEditor&) override;
    void editorHidden(juce::Label*, juce::TextEditor&) override;

    Context& ctx_;
    juce::RangedAudioParameter& param_;
    Style style_;
    juce::String legend_;
    bool hardware_;
    juce::Colour accent_;
    bool bipolar_ = false;
    bool showValue_ = true;
    float legendHeight_ = 10.0f;
    bool hover_ = false;
    bool dragging_ = false;
    bool learning_ = false, bound_ = false;
    float value_ = 0.0f;          // normalised 0..1
    float dragStartValue_ = 0.0f;
    juce::Point<int> dragStart_;
    juce::Rectangle<int> controlArea_, legendArea_, valueArea_;

    juce::Label valueLabel_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamControl)
};

// A latching text button bound to a bool parameter, with the MIDI-learn menu.
class ParamButton : public juce::TextButton {
public:
    ParamButton(Context& ctx, juce::RangedAudioParameter& param, const juce::String& label,
                juce::Colour accent = mw808Colours::orange, bool lamp = false);
    ~ParamButton() override;

    void mouseDown(const juce::MouseEvent& e) override;

private:
    Context& ctx_;
    juce::RangedAudioParameter& param_;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamButton)
};

// A row (or column) of radio buttons bound to a choice parameter.
class ChoiceButtons : public juce::Component {
public:
    ChoiceButtons(Context& ctx, juce::RangedAudioParameter& param, const juce::StringArray& labels, bool vertical,
                  juce::Colour accent = mw808Colours::orange);
    ~ChoiceButtons() override;

    void resized() override;
    void setGap(int gap) { gap_ = gap; resized(); }
    void setFontHeight(float h);
    int numChoices() const noexcept { return buttons_.size(); }

private:
    void select(int index);
    void parameterChanged(float denormalised);

    Context& ctx_;
    juce::RangedAudioParameter& param_;
    bool vertical_;
    int gap_ = 2;
    juce::OwnedArray<juce::TextButton> buttons_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoiceButtons)
};

// A compact value field: legend on top, value in an inset box. Drag or wheel
// changes the value in integer steps, double-click resets, click pops up the
// list when `choices` is set. Not bound to a parameter: the owner supplies
// get/set so it can edit pattern fields (length, scale, shuffle) as well as
// parameters (via the helper below).
class ValueBox : public juce::Component, public juce::SettableTooltipClient {
public:
    ValueBox(const juce::String& legend, int minValue, int maxValue, int defaultValue);

    std::function<int()> getValue;                 // current value
    std::function<void(int)> setValue;             // apply a value (already clamped)
    std::function<juce::String(int)> format;       // text for a value (default: the number)
    juce::StringArray choices;                      // when set, click shows them as a menu
    juce::Component* menuParent = nullptr;
    std::function<void(juce::Component&)> onRightClick;  // e.g. the MIDI-learn menu

    void refresh();   // repaint if the value changed
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }

private:
    void apply(int v);
    juce::String legend_;
    int min_, max_, default_;
    int shown_ = 0;
    bool hover_ = false;
    bool dragged_ = false;
    int dragStartValue_ = 0;
    juce::Point<int> dragStart_;
    float wheelAccum_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValueBox)
};

// Binds a ValueBox to a choice or stepped parameter.
class ParamValueBox : public ValueBox {
public:
    ParamValueBox(Context& ctx, juce::RangedAudioParameter& param, const juce::String& legend, int minValue,
                  int maxValue, bool asChoice);
    ~ParamValueBox() override;

private:
    Context& ctx_;
    juce::RangedAudioParameter& param_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
};

// A plain lamp.
class Lamp : public juce::Component {
public:
    explicit Lamp(juce::Colour on = mw808Colours::ledOn, juce::Colour off = mw808Colours::ledOff) : on_(on), off_(off) {
        setInterceptsMouseClicks(false, false);
    }
    void setLit(float lit) {
        lit = juce::jlimit(0.0f, 1.0f, lit);
        if (std::abs(lit - lit_) < 0.02f) return;
        lit_ = lit;
        repaint();
    }
    float lit() const noexcept { return lit_; }
    void paint(juce::Graphics& g) override { mw808LookAndFeel::drawLamp(g, getLocalBounds().toFloat(), lit_, on_, off_); }

private:
    juce::Colour on_, off_;
    float lit_ = 0.0f;
};

// A flat button whose face is drawn by the LookAndFeel with an accent colour.
inline void styleButton(juce::TextButton& b, juce::Colour accent, bool lamp = false, float fontHeight = 0.0f) {
    b.getProperties().set("accent", static_cast<juce::int64>(accent.getARGB()));
    b.getProperties().set("lamp", lamp);
    if (fontHeight > 0.0f) b.getProperties().set("fontHeight", static_cast<double>(fontHeight));
}

juce::String patternLabel(int patternIndex);  // "A01" .. "D16"

}  // namespace mw808::ui

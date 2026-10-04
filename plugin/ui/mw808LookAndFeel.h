// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The editor's LookAndFeel: fonts, the vector drawing vocabulary every mw808
// component uses (plates, legends, lamps, knobs, faders, keys) and the
// LookAndFeel_V4 overrides for the stock JUCE widgets the editor still uses
// (text buttons, labels, popup menus, text editors, alert windows).
//
// Everything is drawn with juce::Graphics; there are no images, no fonts
// embedded (JUCE's default sans), no OpenGL.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "mw808Colours.h"

class mw808LookAndFeel final : public juce::LookAndFeel_V4 {
public:
    mw808LookAndFeel();
    ~mw808LookAndFeel() override = default;

    // --- fonts (JUCE pixel heights at the 1280 x 720 base) --------------------
    static juce::Font legend(float height = 11.0f);      // uppercase legends, tracked
    static juce::Font text(float height = 12.0f);        // plain text
    static juce::Font bold(float height = 12.0f);
    static juce::Font readout(float height = 13.0f);     // numeric readouts

    // --- drawing vocabulary -----------------------------------------------------
    // A control plate: filled rounded rectangle with a 1 px edge.
    static void drawPlate(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = mw808Colours::plate,
                          juce::Colour edge = mw808Colours::plateEdge, float corner = 4.0f);
    // An inset well (readouts, LED rows).
    static void drawInset(juce::Graphics& g, juce::Rectangle<float> r, float corner = 3.0f);
    // Uppercase legend text, centred.
    static void drawLegend(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
                           juce::Colour colour = mw808Colours::creamDim, float height = 11.0f,
                           juce::Justification just = juce::Justification::centred);
    // A section title with hairlines either side.
    static void drawSectionTitle(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
                                 juce::Colour colour = mw808Colours::creamDim);
    // A round lamp. `lit` 0..1 blends off -> on; a bright core appears above 0.5.
    static void drawLamp(juce::Graphics& g, juce::Rectangle<float> r, float lit, juce::Colour on = mw808Colours::ledOn,
                         juce::Colour off = mw808Colours::ledOff);
    // A rectangular LED bar (step LEDs).
    static void drawLedBar(juce::Graphics& g, juce::Rectangle<float> r, float lit, juce::Colour on,
                           juce::Colour off = mw808Colours::ledOff);

    // A rotary knob. `hardware` selects the skirted, ticked, cream-pointer
    // style of the original panel knobs; the extended knobs are flat with an
    // orange pointer and a dashed ring.
    struct KnobStyle {
        bool hardware = true;
        bool bipolar = false;          // arc drawn from the centre (pan, tune)
        bool hover = false;
        bool enabled = true;
        bool learning = false;         // MIDI learn armed on this control
        bool bound = false;            // a controller is bound to it
        juce::Colour accent = mw808Colours::arc;
    };
    static void drawKnob(juce::Graphics& g, juce::Rectangle<float> r, float value01, const KnobStyle& style);

    // A vertical fader. `value01` 0 = bottom.
    static void drawFader(juce::Graphics& g, juce::Rectangle<float> r, float value01, const KnobStyle& style);

    // One sequencer step key with its state.
    struct KeyState {
        juce::Colour colour = mw808Colours::red;
        int level = 0;                 // 0 off, 1 weak, 2 normal, 3 accent
        bool running = false;
        bool selected = false;         // pattern-select mode: the chosen pattern
        bool hover = false;
        bool down = false;
        juce::String caption;          // "1".."64"
        juce::String badge;            // "F", "x3", "75%"
    };
    static void drawStepKey(juce::Graphics& g, juce::Rectangle<float> r, const KeyState& state);

    // A flat panel button face (shared by the LookAndFeel override and the
    // sequencer's custom buttons).
    static void drawButtonFace(juce::Graphics& g, juce::Rectangle<float> r, bool on, bool hover, bool down,
                               juce::Colour accent, bool lampLeft = false);

    static constexpr float kKnobStartAngle = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float kKnobEndAngle = juce::MathConstants<float>::pi * 2.75f;

    // --- LookAndFeel_V4 overrides ----------------------------------------------
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    void drawLabel(juce::Graphics& g, juce::Label& label) override;
    juce::Font getLabelFont(juce::Label& label) override;
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                   int& idealWidth, int& idealHeight) override;
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path,
                                  juce::Image& cachedImage) override;
    int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return 12; }
    void drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea,
                      juce::TextLayout& layout) override;
    int getAlertWindowButtonHeight() override { return 26; }
    juce::Font getAlertWindowTitleFont() override { return bold(16.0f); }
    juce::Font getAlertWindowMessageFont() override { return text(13.0f); }
    juce::Font getAlertWindowFont() override { return text(13.0f); }
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver,
                       bool isMouseDown) override;
    void drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(mw808LookAndFeel)
};

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "mw808LookAndFeel.h"

#include <cmath>

namespace C = mw808Colours;

namespace {

constexpr float kPi = juce::MathConstants<float>::pi;

juce::Path arcPath(juce::Point<float> centre, float radius, float fromAngle, float toAngle) {
    juce::Path p;
    if (std::abs(toAngle - fromAngle) < 1.0e-4f) return p;
    p.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, fromAngle, toAngle, true);
    return p;
}

}  // namespace

mw808LookAndFeel::mw808LookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, C::panel);
    setColour(juce::TextButton::buttonColourId, C::buttonOff);
    setColour(juce::TextButton::buttonOnColourId, C::buttonOn);
    setColour(juce::TextButton::textColourOffId, C::buttonText);
    setColour(juce::TextButton::textColourOnId, C::cream);
    setColour(juce::Label::textColourId, C::cream);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineWhenEditingColourId, C::orange);
    setColour(juce::Label::backgroundWhenEditingColourId, C::inset);
    setColour(juce::Label::textWhenEditingColourId, C::cream);
    setColour(juce::TextEditor::backgroundColourId, C::inset);
    setColour(juce::TextEditor::textColourId, C::cream);
    setColour(juce::TextEditor::highlightColourId, C::orange.withAlpha(0.5f));
    setColour(juce::TextEditor::highlightedTextColourId, C::cream);
    setColour(juce::TextEditor::outlineColourId, C::plateEdge);
    setColour(juce::TextEditor::focusedOutlineColourId, C::orange);
    setColour(juce::CaretComponent::caretColourId, C::orange);
    setColour(juce::PopupMenu::backgroundColourId, C::panelDeep);
    setColour(juce::PopupMenu::textColourId, C::cream);
    setColour(juce::PopupMenu::headerTextColourId, C::creamDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, C::orange.withAlpha(0.85f));
    setColour(juce::PopupMenu::highlightedTextColourId, C::ink);
    setColour(juce::AlertWindow::backgroundColourId, C::panel);
    setColour(juce::AlertWindow::textColourId, C::cream);
    setColour(juce::AlertWindow::outlineColourId, C::plateEdge);
    setColour(juce::ScrollBar::thumbColourId, C::plateEdge);
    setColour(juce::ScrollBar::trackColourId, C::inset);
    setColour(juce::TooltipWindow::backgroundColourId, C::panelDeep);
    setColour(juce::TooltipWindow::textColourId, C::cream);
    setColour(juce::TooltipWindow::outlineColourId, C::plateEdge);
    setColour(juce::ComboBox::backgroundColourId, C::inset);
    setColour(juce::ComboBox::textColourId, C::cream);
    setColour(juce::ComboBox::outlineColourId, C::plateEdge);
    setColour(juce::ComboBox::arrowColourId, C::creamDim);
    setColour(juce::ListBox::backgroundColourId, C::inset);
    setColour(juce::FileBrowserComponent::currentPathBoxBackgroundColourId, C::inset);
    setColour(juce::FileBrowserComponent::currentPathBoxTextColourId, C::cream);
    setColour(juce::FileBrowserComponent::filenameBoxBackgroundColourId, C::inset);
    setColour(juce::FileBrowserComponent::filenameBoxTextColourId, C::cream);
    setColour(juce::DirectoryContentsDisplayComponent::textColourId, C::cream);
    setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, C::orange.withAlpha(0.6f));
    setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId, C::ink);
}

// --- fonts ---------------------------------------------------------------------

juce::Font mw808LookAndFeel::legend(float height) {
    return juce::Font(juce::FontOptions(height, juce::Font::bold)).withExtraKerningFactor(0.08f);
}
juce::Font mw808LookAndFeel::text(float height) { return juce::Font(juce::FontOptions(height)); }
juce::Font mw808LookAndFeel::bold(float height) { return juce::Font(juce::FontOptions(height, juce::Font::bold)); }
juce::Font mw808LookAndFeel::readout(float height) {
    return juce::Font(juce::FontOptions(height, juce::Font::bold)).withExtraKerningFactor(0.02f);
}

// --- vocabulary ------------------------------------------------------------------

void mw808LookAndFeel::drawPlate(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour edge,
                                 float corner) {
    g.setColour(fill);
    g.fillRoundedRectangle(r, corner);
    g.setColour(edge);
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
}

void mw808LookAndFeel::drawInset(juce::Graphics& g, juce::Rectangle<float> r, float corner) {
    g.setColour(C::inset);
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawHorizontalLine(static_cast<int>(r.getBottom()) - 1, r.getX() + corner, r.getRight() - corner);
}

void mw808LookAndFeel::drawLegend(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
                                  juce::Colour colour, float height, juce::Justification just) {
    g.setColour(colour);
    g.setFont(legend(height));
    g.drawFittedText(s.toUpperCase(), r, just, 1, 0.7f);
}

void mw808LookAndFeel::drawSectionTitle(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
                                        juce::Colour colour) {
    g.setFont(legend(10.0f));
    const juce::String up = s.toUpperCase();
    const int textW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), up) + 12;
    const int cx = r.getCentreX();
    const int cy = r.getCentreY();
    g.setColour(C::hairline);
    if (r.getWidth() > textW + 16) {
        g.drawHorizontalLine(cy, static_cast<float>(r.getX()), static_cast<float>(cx - textW / 2));
        g.drawHorizontalLine(cy, static_cast<float>(cx + textW / 2), static_cast<float>(r.getRight()));
    }
    g.setColour(colour);
    g.drawFittedText(up, r, juce::Justification::centred, 1, 0.7f);
}

void mw808LookAndFeel::drawLamp(juce::Graphics& g, juce::Rectangle<float> r, float lit, juce::Colour on,
                                juce::Colour off) {
    const float d = std::min(r.getWidth(), r.getHeight());
    const auto dome = r.withSizeKeepingCentre(d, d);
    lit = juce::jlimit(0.0f, 1.0f, lit);
    if (lit > 0.3f) {
        g.setColour(on.withAlpha(0.18f * lit));
        g.fillEllipse(dome.expanded(d * 0.45f));
    }
    g.setColour(off.interpolatedWith(on, lit));
    g.fillEllipse(dome);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(dome, 1.0f);
    if (lit > 0.5f) {
        g.setColour(juce::Colours::white.withAlpha(0.55f * (lit - 0.5f) * 2.0f));
        g.fillEllipse(dome.reduced(d * 0.3f).translated(-d * 0.08f, -d * 0.08f));
    }
}

void mw808LookAndFeel::drawLedBar(juce::Graphics& g, juce::Rectangle<float> r, float lit, juce::Colour on,
                                  juce::Colour off) {
    lit = juce::jlimit(0.0f, 1.0f, lit);
    if (lit > 0.3f) {
        g.setColour(on.withAlpha(0.2f * lit));
        g.fillRoundedRectangle(r.expanded(2.0f, 2.0f), 3.0f);
    }
    g.setColour(off.interpolatedWith(on, lit));
    g.fillRoundedRectangle(r, 1.5f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(r.reduced(0.5f), 1.5f, 1.0f);
}

void mw808LookAndFeel::drawKnob(juce::Graphics& g, juce::Rectangle<float> r, float value01, const KnobStyle& style) {
    const float d = std::min(r.getWidth(), r.getHeight());
    const auto area = r.withSizeKeepingCentre(d, d);
    const auto centre = area.getCentre();
    value01 = juce::jlimit(0.0f, 1.0f, value01);
    const float angle = kKnobStartAngle + value01 * (kKnobEndAngle - kKnobStartAngle);
    const float alpha = style.enabled ? 1.0f : 0.45f;

    // Value arc on the outside.
    const float arcR = d * 0.47f;
    const float arcW = juce::jmax(1.5f, d * 0.06f);
    g.setColour(C::knobTrack.withAlpha(alpha));
    g.strokePath(arcPath(centre, arcR, kKnobStartAngle, kKnobEndAngle),
                 juce::PathStrokeType(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const juce::Colour arcColour = (style.hardware ? style.accent : style.accent.interpolatedWith(C::arcExt, 0.35f));
    g.setColour(arcColour.withAlpha(alpha));
    if (style.bipolar) {
        const float mid = (kKnobStartAngle + kKnobEndAngle) * 0.5f;
        g.strokePath(arcPath(centre, arcR, juce::jmin(mid, angle), juce::jmax(mid, angle)),
                     juce::PathStrokeType(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else {
        g.strokePath(arcPath(centre, arcR, kKnobStartAngle, angle),
                     juce::PathStrokeType(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body.
    const float bodyR = d * 0.36f;
    const auto body = juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(centre);
    if (style.hardware) {
        // Skirt with ticks.
        g.setColour(C::creamFaint.withAlpha(0.7f * alpha));
        for (int i = 0; i <= 10; ++i) {
            const float a = kKnobStartAngle + (kKnobEndAngle - kKnobStartAngle) * static_cast<float>(i) / 10.0f;
            const float r0 = d * 0.40f, r1 = d * 0.43f;
            g.drawLine(centre.x + std::sin(a) * r0, centre.y - std::cos(a) * r0, centre.x + std::sin(a) * r1,
                       centre.y - std::cos(a) * r1, 1.0f);
        }
        g.setColour(C::knobBodyDark.withAlpha(alpha));
        g.fillEllipse(body.expanded(d * 0.03f));
        juce::ColourGradient grad(C::knobCapHw.brighter(0.25f), centre.x - bodyR * 0.5f, centre.y - bodyR * 0.6f,
                                  C::knobCapHw.darker(0.35f), centre.x + bodyR * 0.6f, centre.y + bodyR * 0.7f, false);
        g.setGradientFill(grad);
        g.fillEllipse(body);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawEllipse(body, 1.0f);
        // Pointer: a cream bar from near the centre to the rim.
        g.setColour(C::pointerHw.withAlpha(alpha));
        const float p0 = bodyR * 0.25f, p1 = bodyR * 0.92f;
        g.drawLine(centre.x + std::sin(angle) * p0, centre.y - std::cos(angle) * p0, centre.x + std::sin(angle) * p1,
                   centre.y - std::cos(angle) * p1, juce::jmax(2.0f, d * 0.07f));
    } else {
        g.setColour(C::knobCapExt.withAlpha(alpha));
        g.fillEllipse(body);
        g.setColour(C::plateEdge.withAlpha(alpha));
        g.drawEllipse(body, 1.0f);
        // Dashed ring marks the extended knobs.
        juce::Path ring;
        ring.addEllipse(body.reduced(d * 0.05f));
        juce::Path dashed;
        const float dashes[] = {2.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, ring, dashes, 2);
        g.setColour(C::creamFaint.withAlpha(0.8f * alpha));
        g.fillPath(dashed);
        g.setColour(C::pointerExt.withAlpha(alpha));
        const float p0 = bodyR * 0.45f, p1 = bodyR * 0.95f;
        g.drawLine(centre.x + std::sin(angle) * p0, centre.y - std::cos(angle) * p0, centre.x + std::sin(angle) * p1,
                   centre.y - std::cos(angle) * p1, juce::jmax(1.5f, d * 0.05f));
    }

    if (style.hover && style.enabled) {
        g.setColour(C::hover);
        g.fillEllipse(body);
    }
    if (style.learning || style.bound) {
        const float dotD = juce::jmax(4.0f, d * 0.12f);
        auto dot = juce::Rectangle<float>(dotD, dotD).withCentre({area.getRight() - dotD * 0.7f, area.getY() + dotD * 0.7f});
        g.setColour(style.learning ? C::red : C::ledGreenOn);
        g.fillEllipse(dot);
    }
}

void mw808LookAndFeel::drawFader(juce::Graphics& g, juce::Rectangle<float> r, float value01, const KnobStyle& style) {
    value01 = juce::jlimit(0.0f, 1.0f, value01);
    const float alpha = style.enabled ? 1.0f : 0.45f;
    const float capH = juce::jmax(10.0f, r.getHeight() * 0.09f);
    const float travelTop = r.getY() + capH * 0.5f;
    const float travelBottom = r.getBottom() - capH * 0.5f;
    const float cx = r.getCentreX();
    // Slot.
    const auto slot = juce::Rectangle<float>(4.0f, travelBottom - travelTop).withCentre({cx, (travelTop + travelBottom) * 0.5f});
    g.setColour(C::inset.withAlpha(alpha));
    g.fillRoundedRectangle(slot, 2.0f);
    // Scale ticks.
    g.setColour(C::creamFaint.withAlpha(0.6f * alpha));
    for (int i = 0; i <= 10; ++i) {
        const float y = travelTop + (travelBottom - travelTop) * static_cast<float>(i) / 10.0f;
        const float len = (i % 5 == 0) ? 7.0f : 4.0f;
        g.drawHorizontalLine(static_cast<int>(y), cx - 6.0f - len, cx - 6.0f);
        g.drawHorizontalLine(static_cast<int>(y), cx + 6.0f, cx + 6.0f + len);
    }
    // Filled part.
    const float capY = travelBottom - value01 * (travelBottom - travelTop);
    g.setColour(style.accent.withAlpha(0.75f * alpha));
    g.fillRoundedRectangle(juce::Rectangle<float>(cx - 2.0f, capY, 4.0f, travelBottom - capY), 2.0f);
    // Cap.
    const float capW = juce::jmin(r.getWidth() - 2.0f, 22.0f);
    auto cap = juce::Rectangle<float>(capW, capH).withCentre({cx, capY});
    juce::ColourGradient grad(C::knobCapHw.brighter(0.3f), cap.getX(), cap.getY(), C::knobCapHw.darker(0.4f), cap.getX(),
                              cap.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(cap, 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);
    g.setColour(C::pointerHw.withAlpha(alpha));
    g.drawHorizontalLine(static_cast<int>(capY), cap.getX() + 3.0f, cap.getRight() - 3.0f);
    if (style.hover && style.enabled) {
        g.setColour(C::hover);
        g.fillRoundedRectangle(cap, 2.0f);
    }
    if (style.learning || style.bound) {
        auto dot = juce::Rectangle<float>(5.0f, 5.0f).withCentre({r.getRight() - 4.0f, r.getY() + 4.0f});
        g.setColour(style.learning ? C::red : C::ledGreenOn);
        g.fillEllipse(dot);
    }
}

void mw808LookAndFeel::drawStepKey(juce::Graphics& g, juce::Rectangle<float> r, const KeyState& s) {
    const float corner = juce::jmin(4.0f, r.getWidth() * 0.12f);
    const float lift = s.level == 0 ? 0.22f : s.level == 1 ? 0.5f : s.level == 2 ? 0.85f : 1.0f;
    juce::Colour face = C::inset.interpolatedWith(s.colour, lift);
    if (s.down) face = face.darker(0.2f);
    // Shadow / base.
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(r.translated(0.0f, 1.5f), corner);
    juce::ColourGradient grad(face.brighter(0.08f), r.getX(), r.getY(), face.darker(0.15f), r.getX(), r.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, corner);
    if (s.level == 3) {
        // Accent: bright inner plateau.
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.fillRoundedRectangle(r.reduced(r.getWidth() * 0.16f, r.getHeight() * 0.16f), corner * 0.6f);
    }
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
    if (s.selected) {
        g.setColour(C::cream);
        g.drawRoundedRectangle(r.reduced(1.5f), corner, 2.0f);
    }
    if (s.running) {
        g.setColour(C::ledRun.withAlpha(0.9f));
        g.drawRoundedRectangle(r.reduced(1.0f), corner, 2.0f);
    }
    if (s.hover) {
        g.setColour(C::hover);
        g.fillRoundedRectangle(r, corner);
    }
    // Accent keys carry a light plateau under the text: dark ink reads best there.
    const juce::Colour textCol = s.level == 3 ? C::ink : C::textOn(face).withAlpha(s.level == 0 ? 0.75f : 1.0f);
    if (s.caption.isNotEmpty()) {
        g.setColour(textCol);
        g.setFont(bold(juce::jlimit(8.0f, 12.0f, r.getHeight() * 0.22f)));
        g.drawText(s.caption, r.reduced(2.0f).toNearestInt().removeFromBottom(static_cast<int>(r.getHeight() * 0.3f)),
                   juce::Justification::centred, false);
    }
    if (s.badge.isNotEmpty()) {
        g.setColour(textCol);
        g.setFont(bold(juce::jlimit(8.0f, 13.0f, r.getHeight() * 0.26f)));
        g.drawText(s.badge, r.reduced(2.0f).toNearestInt().removeFromTop(static_cast<int>(r.getHeight() * 0.55f)),
                   juce::Justification::centred, false);
    }
}

void mw808LookAndFeel::drawButtonFace(juce::Graphics& g, juce::Rectangle<float> r, bool on, bool hover, bool down,
                                      juce::Colour accent, bool lampLeft) {
    const float corner = 3.0f;
    juce::Colour face = on ? C::buttonOff.interpolatedWith(accent, 0.55f) : C::buttonOff;
    if (down) face = face.darker(0.25f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(r.translated(0.0f, 1.0f), corner);
    juce::ColourGradient grad(face.brighter(0.07f), r.getX(), r.getY(), face.darker(0.12f), r.getX(), r.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, corner);
    g.setColour(on ? accent.withAlpha(0.9f) : C::plateEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
    if (lampLeft) {
        const float d = juce::jmin(6.0f, r.getHeight() * 0.3f);
        drawLamp(g, juce::Rectangle<float>(d, d).withCentre({r.getX() + d * 1.2f, r.getCentreY()}), on ? 1.0f : 0.0f,
                 accent);
    }
    if (hover) {
        g.setColour(C::hover);
        g.fillRoundedRectangle(r, corner);
    }
}

// --- LookAndFeel_V4 overrides ----------------------------------------------------------

void mw808LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    const auto& props = button.getProperties();
    juce::Colour accent = C::orange;
    if (props.contains("accent")) accent = juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(props["accent"])));
    const bool lamp = static_cast<bool>(props["lamp"]);
    drawButtonFace(g, button.getLocalBounds().toFloat().reduced(0.5f), button.getToggleState(),
                   shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown, accent, lamp);
}

void mw808LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) {
    const auto& props = button.getProperties();
    const bool lamp = static_cast<bool>(props["lamp"]);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour(button.getToggleState() ? C::cream : C::buttonText);
    auto area = button.getLocalBounds().reduced(3, 1);
    if (lamp) area.removeFromLeft(static_cast<int>(juce::jmin(6.0f, button.getHeight() * 0.3f) * 1.8f));
    g.drawFittedText(button.getButtonText(), area, juce::Justification::centred, 1, 0.75f);
}

juce::Font mw808LookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight) {
    const auto& props = button.getProperties();
    if (props.contains("fontHeight")) return legend(static_cast<float>(static_cast<double>(props["fontHeight"])));
    return legend(juce::jlimit(8.0f, 12.0f, static_cast<float>(buttonHeight) * 0.45f));
}

void mw808LookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.fillAll(label.findColour(juce::Label::backgroundColourId));
    if (!label.isBeingEdited()) {
        const float alpha = label.isEnabled() ? 1.0f : 0.5f;
        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(getLabelFont(label));
        const auto area = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
        g.drawFittedText(label.getText(), area, label.getJustificationType(), 1, 0.7f);
    }
    g.setColour(label.findColour(juce::Label::outlineColourId));
    g.drawRect(label.getLocalBounds());
}

juce::Font mw808LookAndFeel::getLabelFont(juce::Label& label) { return label.getFont(); }

void mw808LookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)), 3.0f);
}

void mw808LookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    g.setColour(editor.findColour(editor.hasKeyboardFocus(true) ? juce::TextEditor::focusedOutlineColourId
                                                                : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle(juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f),
                           3.0f, 1.0f);
}

void mw808LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.setColour(C::panelDeep);
    g.fillRect(0, 0, width, height);
    g.setColour(C::plateEdge);
    g.drawRect(0, 0, width, height, 1);
}

void mw808LookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                         bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                         const juce::String& text, const juce::String& shortcutKeyText,
                                         const juce::Drawable*, const juce::Colour* textColour) {
    if (isSeparator) {
        g.setColour(C::hairline);
        g.drawHorizontalLine(area.getCentreY(), static_cast<float>(area.getX() + 8), static_cast<float>(area.getRight() - 8));
        return;
    }
    auto r = area.reduced(2, 0);
    if (isHighlighted && isActive) {
        g.setColour(C::orange.withAlpha(0.85f));
        g.fillRoundedRectangle(r.toFloat(), 2.0f);
    }
    juce::Colour col = textColour != nullptr ? *textColour : C::cream;
    if (isHighlighted && isActive) col = C::ink;
    if (!isActive) col = col.withAlpha(0.4f);
    g.setColour(col);
    g.setFont(getPopupMenuFont());
    auto textArea = r.reduced(8, 0);
    const auto tick = textArea.removeFromLeft(14);
    if (isTicked) {
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(tick.getCentre().toFloat()));
    }
    if (hasSubMenu) {
        const auto arrow = textArea.removeFromRight(12).toFloat();
        juce::Path p;
        p.addTriangle(arrow.getCentreX() - 3.0f, arrow.getCentreY() - 4.0f, arrow.getCentreX() - 3.0f,
                      arrow.getCentreY() + 4.0f, arrow.getCentreX() + 3.0f, arrow.getCentreY());
        g.fillPath(p);
    }
    g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty()) {
        g.setFont(text.isEmpty() ? getPopupMenuFont() : getPopupMenuFont().withHeight(11.0f));
        g.drawText(shortcutKeyText, textArea, juce::Justification::centredRight, true);
    }
}

void mw808LookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                  const juce::String& sectionName) {
    g.setColour(C::creamDim);
    g.setFont(legend(10.0f));
    g.drawFittedText(sectionName.toUpperCase(), area.reduced(8, 0), juce::Justification::centredLeft, 1);
    g.setColour(C::hairline);
    g.drawHorizontalLine(area.getBottom() - 1, static_cast<float>(area.getX() + 8), static_cast<float>(area.getRight() - 8));
}

juce::Font mw808LookAndFeel::getPopupMenuFont() { return text(13.0f); }

void mw808LookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                                 int& idealWidth, int& idealHeight) {
    if (isSeparator) {
        idealWidth = 50;
        idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight / 2 : 8;
        return;
    }
    const auto font = getPopupMenuFont();
    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 22;
    idealWidth = juce::GlyphArrangement::getStringWidthInt(font, text) + idealHeight * 2 + 20;
}

void mw808LookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path,
                                                juce::Image&) {
    g.setColour(C::panelDeep.withAlpha(0.97f));
    g.fillPath(path);
    g.setColour(C::plateEdge);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void mw808LookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea,
                                    juce::TextLayout& layout) {
    auto bounds = alert.getLocalBounds().toFloat();
    g.setColour(C::panel);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(C::plateEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(C::orange);
    g.fillRect(bounds.removeFromTop(3.0f).reduced(4.0f, 0.0f));
    g.setColour(C::cream);
    layout.draw(g, textArea.toFloat());
}

void mw808LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                     bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver,
                                     bool) {
    g.setColour(C::inset);
    g.fillRect(x, y, width, height);
    juce::Rectangle<int> thumb = isScrollbarVertical ? juce::Rectangle<int>(x + 2, thumbStartPosition, width - 4, thumbSize)
                                                     : juce::Rectangle<int>(thumbStartPosition, y + 2, thumbSize, height - 4);
    g.setColour(isMouseOver ? C::creamDim : C::plateEdge);
    g.fillRoundedRectangle(thumb.toFloat(), 2.0f);
}

void mw808LookAndFeel::drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool) {
    g.setColour(isMouseOver ? C::creamDim : C::creamFaint);
    for (int i = 1; i <= 3; ++i) {
        const float o = static_cast<float>(i) * 4.0f;
        g.drawLine(static_cast<float>(w) - o, static_cast<float>(h), static_cast<float>(w), static_cast<float>(h) - o, 1.0f);
    }
}

void mw808LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    const juce::Rectangle<float> r(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(C::panelDeep);
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(C::plateEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
    g.setColour(C::cream);
    g.setFont(mw808LookAndFeel::text(12.0f));
    g.drawFittedText(text, r.toNearestInt().reduced(6, 2), juce::Justification::centredLeft, 3);
}

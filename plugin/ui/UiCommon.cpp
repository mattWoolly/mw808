// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "UiCommon.h"

#include <cmath>

namespace mw808::ui {

namespace C = mw808Colours;

namespace {
constexpr float kDragPixelsForFullRange = 200.0f;
constexpr float kFineFactor = 0.1f;
constexpr float kWheelStep = 1.0f / 40.0f;
}  // namespace

juce::String patternLabel(int patternIndex) {
    const int i = juce::jlimit(0, seq::kNumPatterns - 1, patternIndex);
    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + i / seq::kPatternsPerBank)) +
           juce::String(i % seq::kPatternsPerBank + 1).paddedLeft('0', 2);
}

// --- parameter menu ---------------------------------------------------------------------

void showParameterMenu(Context& ctx, juce::RangedAudioParameter& param, juce::Component& target,
                       std::function<void()> onReset) {
    auto& learn = ctx.processor.midiLearn();
    const juce::String id = param.getParameterID();
    const int cc = learn.controllerFor(id);
    const bool learning = learn.isLearning() && learn.learningParameter() == id;

    juce::PopupMenu menu;
    menu.setLookAndFeel(&ctx.lnf);
    menu.addSectionHeader(param.getName(64));
    menu.addItem(1, "Reset to default");
    menu.addSeparator();
    menu.addItem(2, learning ? "Cancel MIDI learn" : "MIDI learn", true, learning);
    menu.addItem(3, cc >= 0 ? "Forget CC " + juce::String(cc) : "No controller bound", cc >= 0);

    juce::Component::SafePointer<juce::Component> safeTarget(&target);
    mw808Processor* processor = &ctx.processor;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target).withParentComponent(&ctx.menuParent),
                       [safeTarget, processor, id, onReset](int result) {
                           if (safeTarget == nullptr) return;
                           auto& ml = processor->midiLearn();
                           switch (result) {
                               case 1:
                                   if (onReset) onReset();
                                   break;
                               case 2:
                                   if (ml.isLearning() && ml.learningParameter() == id) ml.cancelLearn();
                                   else ml.beginLearn(id);
                                   break;
                               case 3: ml.forget(id); break;
                               default: break;
                           }
                       });
}

// --- CachedBackground ---------------------------------------------------------------------

void CachedBackground::paint(juce::Graphics& g) {
    const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    if (cache_.isNull() || cacheW_ != w || cacheH_ != h || std::abs(cacheScale_ - physical) > 1.0e-3f) {
        const int pw = juce::jmax(1, juce::roundToInt(static_cast<float>(w) * physical));
        const int ph = juce::jmax(1, juce::roundToInt(static_cast<float>(h) * physical));
        cache_ = juce::Image(juce::Image::ARGB, pw, ph, true);
        juce::Graphics ig(cache_);
        ig.addTransform(juce::AffineTransform::scale(physical));
        paintStatic(ig);
        cacheW_ = w;
        cacheH_ = h;
        cacheScale_ = physical;
    }
    g.drawImageTransformed(cache_, juce::AffineTransform::scale(1.0f / physical), false);
    paintDynamic(g);
}

// --- ParamControl ----------------------------------------------------------------------------

ParamControl::ParamControl(Context& ctx, juce::RangedAudioParameter& param, Style style, const juce::String& legend,
                           bool hardware, juce::Colour accent)
    : ctx_(ctx), param_(param), style_(style), legend_(legend), hardware_(hardware), accent_(accent) {
    valueLabel_.setJustificationType(juce::Justification::centred);
    valueLabel_.setFont(mw808LookAndFeel::readout(10.0f));
    valueLabel_.setColour(juce::Label::textColourId, C::cream);
    valueLabel_.setEditable(false, true, false);
    valueLabel_.setBorderSize({0, 0, 0, 0});
    valueLabel_.setInterceptsMouseClicks(true, false);
    valueLabel_.addListener(this);
    valueLabel_.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(valueLabel_);
    attachment_ = std::make_unique<juce::ParameterAttachment>(param_, [this](float v) { parameterChanged(v); });
    attachment_->sendInitialUpdate();
    setRepaintsOnMouseActivity(false);
    ctx_.controls.add(this);
}

ParamControl::~ParamControl() {
    ctx_.controls.removeFirstMatchingValue(this);
    // Closed mid-drag (host closes the window): do not leave the host's gesture open.
    if (dragging_ && attachment_ != nullptr) attachment_->endGesture();
    attachment_.reset();
    valueLabel_.removeListener(this);
}

void ParamControl::setShowValue(bool show) {
    showValue_ = show;
    valueLabel_.setVisible(show);
    resized();
}

void ParamControl::setLearnState(bool learning, bool bound) {
    if (learning == learning_ && bound == bound_) return;
    learning_ = learning;
    bound_ = bound;
    repaint(controlArea_);
}

void ParamControl::parameterChanged(float denormalised) {
    const float norm = param_.convertTo0to1(denormalised);
    if (std::abs(norm - value_) < 1.0e-6f && valueLabel_.getText().isNotEmpty()) return;
    value_ = norm;
    refreshValueText();
    repaint(controlArea_);
}

void ParamControl::refreshValueText() {
    if (!showValue_) return;
    juce::String t = param_.getText(value_, 10);
    const juce::String label = param_.getLabel();
    if (label.isNotEmpty() && !t.endsWith(label)) t += " " + label;
    valueLabel_.setText(t, juce::dontSendNotification);
}

void ParamControl::resized() {
    auto r = getLocalBounds();
    const int legendH = legend_.isNotEmpty() ? juce::roundToInt(legendHeight_ + 3.0f) : 0;
    const int valueH = showValue_ ? 12 : 0;
    legendArea_ = r.removeFromTop(legendH);
    valueArea_ = r.removeFromBottom(valueH);
    controlArea_ = r;
    valueLabel_.setBounds(valueArea_.expanded(6, 0).withTrimmedBottom(-1));
    valueLabel_.setFont(mw808LookAndFeel::readout(juce::jlimit(8.0f, 11.0f, static_cast<float>(valueH) - 1.0f)));
}

void ParamControl::paint(juce::Graphics& g) {
    if (legend_.isNotEmpty())
        mw808LookAndFeel::drawLegend(g, legend_, legendArea_.expanded(8, 0), hardware_ ? C::cream : C::creamDim,
                                     legendHeight_);
    mw808LookAndFeel::KnobStyle st;
    st.hardware = hardware_;
    st.bipolar = bipolar_;
    st.hover = hover_ || dragging_;
    st.enabled = isEnabled();
    st.learning = learning_;
    st.bound = bound_;
    st.accent = accent_;
    if (style_ == Style::Knob) mw808LookAndFeel::drawKnob(g, controlArea_.toFloat(), value_, st);
    else mw808LookAndFeel::drawFader(g, controlArea_.toFloat(), value_, st);
}

void ParamControl::setNormalisedFromGesture(float norm) {
    norm = juce::jlimit(0.0f, 1.0f, norm);
    attachment_->setValueAsPartOfGesture(param_.convertFrom0to1(norm));
}

void ParamControl::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        auto* attachment = attachment_.get();
        auto& param = param_;
        showParameterMenu(ctx_, param_, *this, [attachment, &param] {
            attachment->setValueAsCompleteGesture(param.convertFrom0to1(param.getDefaultValue()));
        });
        return;
    }
    if (!controlArea_.contains(e.getPosition())) return;
    dragging_ = true;
    dragStart_ = e.getPosition();
    dragStartValue_ = value_;
    attachment_->beginGesture();
    repaint(controlArea_);
}

void ParamControl::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    const float dy = static_cast<float>(dragStart_.y - e.getPosition().y);
    const float dx = static_cast<float>(e.getPosition().x - dragStart_.x);
    // Vertical drag; a horizontal component helps too, as users expect of knobs.
    const float pixels = style_ == Style::Fader ? dy : dy + dx * 0.5f;
    const float fine = e.mods.isShiftDown() ? kFineFactor : 1.0f;
    float range = kDragPixelsForFullRange;
    if (style_ == Style::Fader) range = juce::jmax(60.0f, static_cast<float>(controlArea_.getHeight()) * 0.9f);
    setNormalisedFromGesture(dragStartValue_ + pixels / range * fine);
}

void ParamControl::mouseUp(const juce::MouseEvent&) {
    if (!dragging_) return;
    dragging_ = false;
    attachment_->endGesture();
    repaint(controlArea_);
}

void ParamControl::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    if (!controlArea_.contains(e.getPosition())) return;
    attachment_->setValueAsCompleteGesture(param_.convertFrom0to1(param_.getDefaultValue()));
}

void ParamControl::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const float delta = (!juce::approximatelyEqual(wheel.deltaY, 0.0f) ? wheel.deltaY : -wheel.deltaX);
    if (juce::approximatelyEqual(delta, 0.0f)) return;
    const float fine = e.mods.isShiftDown() ? kFineFactor : 1.0f;
    const float step = (wheel.isReversed ? -1.0f : 1.0f) * (delta > 0.0f ? 1.0f : -1.0f) * kWheelStep * fine;
    // Choice parameters step one choice per notch.
    const int steps = param_.getNumSteps();
    float norm;
    if (steps > 1 && steps <= 64) {
        const float stepSize = 1.0f / static_cast<float>(steps - 1);
        norm = value_ + (step > 0.0f ? stepSize : -stepSize);
    } else {
        norm = value_ + step;
    }
    attachment_->setValueAsCompleteGesture(param_.convertFrom0to1(juce::jlimit(0.0f, 1.0f, norm)));
}

void ParamControl::mouseEnter(const juce::MouseEvent&) {
    hover_ = true;
    repaint(controlArea_);
}

void ParamControl::mouseExit(const juce::MouseEvent&) {
    hover_ = false;
    repaint(controlArea_);
}

void ParamControl::labelTextChanged(juce::Label* label) {
    const juce::String text = label->getText().trim();
    if (text.isEmpty()) {
        refreshValueText();
        return;
    }
    juce::String number = text;
    const juce::String unit = param_.getLabel();
    if (unit.isNotEmpty() && number.endsWithIgnoreCase(unit)) number = number.dropLastCharacters(unit.length()).trim();
    const float norm = param_.getValueForText(number);
    attachment_->setValueAsCompleteGesture(param_.convertFrom0to1(juce::jlimit(0.0f, 1.0f, norm)));
    refreshValueText();
}

void ParamControl::editorShown(juce::Label*, juce::TextEditor& editor) {
    editor.setJustification(juce::Justification::centred);
    editor.setFont(valueLabel_.getFont());
    editor.setInputRestrictions(12, "0123456789.-+ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz/% ");
    editor.selectAll();
}

void ParamControl::editorHidden(juce::Label*, juce::TextEditor&) { refreshValueText(); }

// --- ParamButton -------------------------------------------------------------------------------

ParamButton::ParamButton(Context& ctx, juce::RangedAudioParameter& param, const juce::String& label, juce::Colour accent,
                         bool lamp)
    : juce::TextButton(label), ctx_(ctx), param_(param) {
    setClickingTogglesState(true);
    styleButton(*this, accent, lamp);
    attachment_ = std::make_unique<juce::ButtonParameterAttachment>(param_, *this);
    attachment_->sendInitialUpdate();
}

ParamButton::~ParamButton() { attachment_.reset(); }

void ParamButton::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        auto& param = param_;
        showParameterMenu(ctx_, param_, *this, [&param] {
            param.beginChangeGesture();
            param.setValueNotifyingHost(param.getDefaultValue());
            param.endChangeGesture();
        });
        return;
    }
    juce::TextButton::mouseDown(e);
}

// --- ChoiceButtons --------------------------------------------------------------------------------

ChoiceButtons::ChoiceButtons(Context& ctx, juce::RangedAudioParameter& param, const juce::StringArray& labels,
                             bool vertical, juce::Colour accent)
    : ctx_(ctx), param_(param), vertical_(vertical) {
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = buttons_.add(new juce::TextButton(labels[i]));
        styleButton(*b, accent);
        b->setClickingTogglesState(false);
        b->setConnectedEdges(0);
        b->onClick = [this, i] { select(i); };
        addAndMakeVisible(b);
    }
    attachment_ = std::make_unique<juce::ParameterAttachment>(param_, [this](float v) { parameterChanged(v); });
    attachment_->sendInitialUpdate();
}

ChoiceButtons::~ChoiceButtons() { attachment_.reset(); }

void ChoiceButtons::setFontHeight(float h) {
    for (auto* b : buttons_) b->getProperties().set("fontHeight", static_cast<double>(h));
}

void ChoiceButtons::resized() {
    const int n = buttons_.size();
    if (n == 0) return;
    auto r = getLocalBounds();
    for (int i = 0; i < n; ++i) {
        juce::Rectangle<int> cell;
        if (vertical_) {
            const int h = (r.getHeight() - gap_ * (n - 1 - i)) / (n - i);
            cell = r.removeFromTop(h);
            r.removeFromTop(gap_);
        } else {
            const int w = (r.getWidth() - gap_ * (n - 1 - i)) / (n - i);
            cell = r.removeFromLeft(w);
            r.removeFromLeft(gap_);
        }
        buttons_[i]->setBounds(cell);
    }
}

void ChoiceButtons::select(int index) {
    const auto& range = param_.getNormalisableRange();
    const float value = range.start + static_cast<float>(index);
    attachment_->setValueAsCompleteGesture(juce::jlimit(range.start, range.end, value));
}

void ChoiceButtons::parameterChanged(float denormalised) {
    const int index = juce::roundToInt(denormalised - param_.getNormalisableRange().start);
    for (int i = 0; i < buttons_.size(); ++i) buttons_[i]->setToggleState(i == index, juce::dontSendNotification);
}

// --- ValueBox ------------------------------------------------------------------------------------------

ValueBox::ValueBox(const juce::String& legend, int minValue, int maxValue, int defaultValue)
    : legend_(legend), min_(minValue), max_(maxValue), default_(defaultValue) {
    setRepaintsOnMouseActivity(false);
}

void ValueBox::refresh() {
    if (!getValue) return;
    const int v = getValue();
    if (v != shown_) {
        shown_ = v;
        repaint();
    }
}

void ValueBox::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    if (getValue) shown_ = getValue();
    if (legend_.isNotEmpty()) {
        mw808LookAndFeel::drawLegend(g, legend_, r.removeFromTop(12), C::creamDim, 10.0f);
        r.removeFromTop(1);
    }
    mw808LookAndFeel::drawInset(g, r.toFloat());
    if (hover_) {
        g.setColour(C::hover);
        g.fillRoundedRectangle(r.toFloat(), 3.0f);
    }
    g.setColour(C::cream);
    g.setFont(mw808LookAndFeel::readout(juce::jlimit(9.0f, 13.0f, static_cast<float>(r.getHeight()) * 0.62f)));
    const juce::String text = format ? format(shown_) : juce::String(shown_);
    g.drawFittedText(text, r.reduced(3, 0), juce::Justification::centred, 1, 0.7f);
}

void ValueBox::apply(int v) {
    v = juce::jlimit(min_, max_, v);
    if (setValue && v != shown_) setValue(v);
    refresh();
}

void ValueBox::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onRightClick) onRightClick(*this);
        return;
    }
    dragged_ = false;
    dragStart_ = e.getPosition();
    dragStartValue_ = getValue ? getValue() : shown_;
}

void ValueBox::mouseDrag(const juce::MouseEvent& e) {
    const int dy = dragStart_.y - e.getPosition().y;
    if (std::abs(dy) > 2) dragged_ = true;
    if (!dragged_) return;
    const int span = max_ - min_;
    const float perPixel = e.mods.isShiftDown() ? 0.05f : juce::jmax(0.1f, static_cast<float>(span) / 150.0f);
    apply(dragStartValue_ + juce::roundToInt(static_cast<float>(dy) * perPixel));
}

void ValueBox::mouseUp(const juce::MouseEvent& e) {
    if (dragged_ || e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown()) return;
    if (choices.isEmpty() || !setValue) return;
    juce::PopupMenu menu;
    if (auto* lnf = dynamic_cast<mw808LookAndFeel*>(&getLookAndFeel())) menu.setLookAndFeel(lnf);
    const int current = getValue ? getValue() : shown_;
    for (int i = 0; i < choices.size(); ++i) menu.addItem(i + 1, choices[i], true, i + min_ == current);
    juce::Component::SafePointer<ValueBox> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withParentComponent(menuParent),
                       [safe](int result) {
                           if (safe == nullptr || result <= 0) return;
                           safe->apply(safe->min_ + result - 1);
                       });
}

void ValueBox::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    apply(default_);
}

void ValueBox::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    const float delta = !juce::approximatelyEqual(wheel.deltaY, 0.0f) ? wheel.deltaY : -wheel.deltaX;
    if (juce::approximatelyEqual(delta, 0.0f)) return;
    wheelAccum_ += (wheel.isReversed ? -delta : delta);
    const int steps = static_cast<int>(wheelAccum_ / 0.08f);
    if (steps == 0) return;
    wheelAccum_ -= static_cast<float>(steps) * 0.08f;
    apply((getValue ? getValue() : shown_) + steps);
}

// --- ParamValueBox -----------------------------------------------------------------------------------------

ParamValueBox::ParamValueBox(Context& ctx, juce::RangedAudioParameter& param, const juce::String& legend, int minValue,
                             int maxValue, bool asChoice)
    : ValueBox(legend, minValue, maxValue, juce::roundToInt(param.convertFrom0to1(param.getDefaultValue()))),
      ctx_(ctx), param_(param) {
    menuParent = &ctx.menuParent;
    if (asChoice) {
        const auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(&param_);
        if (choiceParam != nullptr) choices = choiceParam->choices;
    }
    getValue = [this] { return juce::roundToInt(param_.convertFrom0to1(param_.getValue())); };
    format = [this](int v) {
        if (choices.isEmpty()) return param_.getText(param_.convertTo0to1(static_cast<float>(v)), 8);
        const int i = v - static_cast<int>(param_.getNormalisableRange().start);
        return i >= 0 && i < choices.size() ? choices[i] : juce::String(v);
    };
    setValue = [this](int v) { attachment_->setValueAsCompleteGesture(static_cast<float>(v)); };
    onRightClick = [this](juce::Component& c) {
        auto* attachment = attachment_.get();
        auto& p = param_;
        showParameterMenu(ctx_, param_, c, [attachment, &p] { attachment->setValueAsCompleteGesture(p.convertFrom0to1(p.getDefaultValue())); });
    };
    attachment_ = std::make_unique<juce::ParameterAttachment>(param_, [this](float) { refresh(); });
    attachment_->sendInitialUpdate();
}

ParamValueBox::~ParamValueBox() { attachment_.reset(); }

}  // namespace mw808::ui

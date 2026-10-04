// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "VoicePanel.h"

namespace mw808::ui {

namespace C = mw808Colours;
namespace paramid = plugin::paramid;
using paramid::VoiceParam;

namespace {
constexpr int kGlobalWidth = 150;
constexpr int kGap = 6;
}  // namespace

// --- NamePlate ---------------------------------------------------------------------------

NamePlate::NamePlate(int voice, juce::Colour accent) : voice_(voice), accent_(accent) {
    setRepaintsOnMouseActivity(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Click: audition and select track. Shift-click: accented hit.");
}

void NamePlate::setSelected(bool selected) {
    if (selected == selected_) return;
    selected_ = selected;
    repaint();
}

void NamePlate::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    const auto& info = kVoiceInfo[static_cast<std::size_t>(voice_)];
    juce::Colour fill = down_ ? C::plateRaised.darker(0.3f) : (selected_ ? C::plateRaised.interpolatedWith(accent_, 0.25f) : C::plateRaised);
    mw808LookAndFeel::drawPlate(g, r, fill, selected_ ? accent_ : C::plateEdge, 3.0f);
    if (hover_) {
        g.setColour(C::hover);
        g.fillRoundedRectangle(r, 3.0f);
    }
    auto inner = getLocalBounds().reduced(2, 2);
    g.setColour(selected_ ? C::cream : C::cream.withAlpha(0.9f));
    g.setFont(mw808LookAndFeel::bold(15.0f));
    g.drawText(juce::String(std::string(info.code)), inner.removeFromTop(inner.getHeight() * 11 / 20), juce::Justification::centred, false);
    g.setColour(C::creamDim);
    g.setFont(mw808LookAndFeel::text(10.0f));
    g.drawFittedText(juce::String(std::string(info.name)), inner, juce::Justification::centred, 1, 0.6f);
    // Accent mark.
    g.setColour(accent_);
    g.fillRoundedRectangle(juce::Rectangle<float>(3.0f, 3.0f, 4.0f, 4.0f), 1.0f);
}

void NamePlate::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    down_ = true;
    repaint();
    if (onAudition) onAudition(e.mods.isShiftDown());
    if (onSelect) onSelect();
}

void NamePlate::mouseUp(const juce::MouseEvent&) {
    down_ = false;
    repaint();
}

// --- VoiceStrip ---------------------------------------------------------------------------

VoiceStrip::VoiceStrip(Context& ctx, int voice)
    : ctx_(ctx), voice_(voice), accent_(C::voiceAccent(voice)), lamp_(C::ledOn), plate_(voice, accent_) {
    const auto& info = kVoiceInfo[static_cast<std::size_t>(voice)];
    auto make = [&](VoiceParam p, const juce::String& legend) -> ParamControl* {
        auto* param = ctx_.param(paramid::voice(voice, p));
        if (param == nullptr) return nullptr;
        const bool hw = isHardwareKnob(voice, p);
        auto* k = new ParamControl(ctx_, *param, ParamControl::Style::Knob, legend, hw, hw ? C::arc : C::arcExt);
        if (p == VoiceParam::Tune) k->setBipolar(true);
        return k;
    };
    level_.reset(make(VoiceParam::Level, "LEVEL"));
    if (level_) addAndMakeVisible(*level_);

    struct Entry { VoiceParam p; juce::String legend; };
    juce::Array<Entry> entries;
    entries.add({VoiceParam::Tune, info.panelTune ? "TUNING" : "TUNE"});
    entries.add({VoiceParam::Decay, "DECAY"});
    if (paramid::voiceHas(voice, VoiceParam::Tone)) entries.add({VoiceParam::Tone, juce::String(std::string(info.toneName))});
    if (paramid::voiceHas(voice, VoiceParam::Shape)) entries.add({VoiceParam::Shape, juce::String(std::string(info.shapeName))});
    // Hardware-original knobs first (as on the panel), extended ones below.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& e : entries)
            if (isHardwareKnob(voice, e.p) == (pass == 0))
                if (auto* k = make(e.p, e.legend)) {
                    knobs_.add(k);
                    addAndMakeVisible(k);
                }

    addAndMakeVisible(lamp_);
    if (auto* m = ctx_.param(paramid::voice(voice, VoiceParam::Mute))) {
        mute_ = std::make_unique<ParamButton>(ctx_, *m, "M", C::yellow);
        mute_->setTooltip("Mute (gates triggers; the tail rings out)");
        addAndMakeVisible(*mute_);
    }
    if (auto* s = ctx_.param(paramid::voice(voice, VoiceParam::Solo))) {
        solo_ = std::make_unique<ParamButton>(ctx_, *s, "S", C::ledGreenOn);
        solo_->setTooltip("Solo");
        addAndMakeVisible(*solo_);
    }
    plate_.onAudition = [this](bool accent) { ctx_.processor.audition(static_cast<VoiceId>(voice_), accent); };
    plate_.onSelect = [this] { ctx_.state.setSelectedTrack(voice_); };
    addAndMakeVisible(plate_);
}

VoiceStrip::~VoiceStrip() = default;

void VoiceStrip::tick() {
    if (lampAge_ > 6) return;
    ++lampAge_;
    lamp_.setLit(lampAge_ <= 2 ? 1.0f : juce::jmax(0.0f, 1.0f - static_cast<float>(lampAge_ - 2) / 4.0f));
}

void VoiceStrip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    mw808LookAndFeel::drawPlate(g, r, C::plate, C::plateEdge, 4.0f);
    g.setColour(accent_);
    g.fillRoundedRectangle(r.removeFromTop(4.0f).reduced(4.0f, 0.0f).translated(0.0f, 2.0f), 1.5f);
}

void VoiceStrip::resized() {
    auto r = getLocalBounds().reduced(3, 0);
    r.removeFromTop(9);
    const int w = r.getWidth();
    plate_.setBounds(r.removeFromBottom(38));
    r.removeFromBottom(4);
    auto row = r.removeFromBottom(20);
    {
        auto lampArea = row.removeFromLeft(16);
        lamp_.setBounds(lampArea.withSizeKeepingCentre(10, 10));
        row.removeFromLeft(2);
        const int bw = (row.getWidth() - 2) / 2;
        if (mute_) mute_->setBounds(row.removeFromLeft(bw));
        row.removeFromLeft(2);
        if (solo_) solo_->setBounds(row);
    }
    r.removeFromBottom(6);
    // LEVEL: the big one.
    const int levelH = juce::jmin(88, w + 26);
    if (level_) level_->setBounds(r.removeFromTop(levelH));
    r.removeFromTop(2);
    // Remaining knobs share what is left, at most 68 px each.
    const int n = knobs_.size();
    if (n > 0) {
        const int each = juce::jmin(68, r.getHeight() / n);
        for (auto* k : knobs_) {
            k->setBounds(r.removeFromTop(each).reduced(2, 1));
        }
    }
}

// --- GlobalBlock ----------------------------------------------------------------------------

GlobalBlock::GlobalBlock(Context& ctx) : ctx_(ctx) {
    auto make = [&](const char* id, const juce::String& legend, bool hw) {
        auto* p = ctx_.param(id);
        if (p == nullptr) return std::unique_ptr<ParamControl>();
        auto c = std::make_unique<ParamControl>(ctx_, *p, ParamControl::Style::Knob, legend, hw, hw ? C::arc : C::arcExt);
        c->setLegendHeight(hw ? 11.0f : 10.0f);
        addAndMakeVisible(*c);
        return c;
    };
    tempo_ = make(paramid::tempo, "TEMPO", true);
    accent_ = make(paramid::accent, "ACCENT", true);
    master_ = make(paramid::masterVolume, "MASTER", true);
    flam_ = make(paramid::flamTime, "FLAM", false);
    swing_ = make(paramid::shuffleDepth, "SWING", false);
    if (auto* p = ctx_.param(paramid::seqMode)) {
        mode_ = std::make_unique<ChoiceButtons>(ctx_, *p, juce::StringArray{"OFF", "HOST SYNC", "INTERNAL", "HOST NOTE"}, true, C::orange);
        mode_->setFontHeight(9.5f);
        addAndMakeVisible(*mode_);
    }
}

void GlobalBlock::resized() {
    auto r = getLocalBounds().reduced(8, 8);
    if (tempo_) tempo_->setBounds(r.removeFromTop(132));
    r.removeFromTop(4);
    {
        auto row = r.removeFromTop(78);
        const int half = row.getWidth() / 2;
        if (accent_) accent_->setBounds(row.removeFromLeft(half));
        if (master_) master_->setBounds(row);
    }
    r.removeFromTop(6);
    modeLegend_ = r.removeFromTop(12);
    r.removeFromTop(2);
    if (mode_) mode_->setBounds(r.removeFromTop(92));
    r.removeFromTop(8);
    feelTitle_ = r.removeFromTop(12);
    r.removeFromTop(2);
    {
        auto row = r.removeFromTop(juce::jmin(68, r.getHeight()));
        const int half = row.getWidth() / 2;
        if (flam_) flam_->setBounds(row.removeFromLeft(half));
        if (swing_) swing_->setBounds(row);
    }
    invalidateCache();
}

void GlobalBlock::paintStatic(juce::Graphics& g) {
    mw808LookAndFeel::drawPlate(g, getLocalBounds().toFloat(), C::plate, C::plateEdge, 4.0f);
    mw808LookAndFeel::drawSectionTitle(g, "MODE", modeLegend_);
    mw808LookAndFeel::drawSectionTitle(g, "FEEL", feelTitle_);
}

// --- VoicePanel ------------------------------------------------------------------------------

VoicePanel::VoicePanel(Context& ctx) : ctx_(ctx), global_(ctx) {
    addAndMakeVisible(global_);
    for (int v = 0; v < kNumVoices; ++v) addAndMakeVisible(strips_.add(new VoiceStrip(ctx_, v)));
    ctx_.state.addListener(this);
    uiStateChanged();
}

VoicePanel::~VoicePanel() { ctx_.state.removeListener(this); }

void VoicePanel::paint(juce::Graphics& g) { g.fillAll(C::panel); }

void VoicePanel::resized() {
    auto r = getLocalBounds().reduced(kGap, kGap);
    global_.setBounds(r.removeFromLeft(kGlobalWidth));
    r.removeFromLeft(kGap);
    const int n = strips_.size();
    const int gap = 3;
    for (int i = 0; i < n; ++i) {
        const int w = (r.getWidth() - gap * (n - 1 - i)) / (n - i);
        strips_[i]->setBounds(r.removeFromLeft(w));
        r.removeFromLeft(gap);
    }
}

void VoicePanel::uiStateChanged() {
    const int sel = ctx_.state.selectedTrack();
    for (int v = 0; v < strips_.size(); ++v) strips_[v]->setSelected(v == sel);
}

void VoicePanel::poll(const PlayState& ps) {
    for (int v = 0; v < kNumVoices; ++v) {
        const auto h = ps.hits[static_cast<std::size_t>(v)].load(std::memory_order_relaxed);
        if (hitsPrimed_ && h != lastHits_[static_cast<std::size_t>(v)]) strips_[v]->flash();
        lastHits_[static_cast<std::size_t>(v)] = h;
        strips_[v]->tick();
    }
    hitsPrimed_ = true;
}

}  // namespace mw808::ui

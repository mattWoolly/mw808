// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "MixerView.h"

namespace mw808::ui {

namespace C = mw808Colours;
namespace paramid = plugin::paramid;
using paramid::VoiceParam;

namespace {
constexpr int kMasterWidth = 150;
constexpr int kGap = 6;
}  // namespace

// --- MixerChannel -------------------------------------------------------------------------

MixerChannel::MixerChannel(Context& ctx, int voice)
    : ctx_(ctx), voice_(voice), accent_(C::voiceAccent(voice)), lamp_(C::ledOn), plate_(voice, accent_) {
    auto make = [&](VoiceParam p, const juce::String& legend, ParamControl::Style style, bool hw) {
        auto* param = ctx_.param(paramid::voice(voice, p));
        if (param == nullptr) return std::unique_ptr<ParamControl>();
        auto c = std::make_unique<ParamControl>(ctx_, *param, style, legend, hw, hw ? C::arc : C::arcExt);
        addAndMakeVisible(*c);
        return c;
    };
    pan_ = make(VoiceParam::Pan, "PAN", ParamControl::Style::Knob, false);
    if (pan_) pan_->setBipolar(true);
    drive_ = make(VoiceParam::Drive, "DRIVE", ParamControl::Style::Knob, false);
    level_ = make(VoiceParam::Level, "LEVEL", ParamControl::Style::Fader, true);
    if (auto* p = ctx_.param(paramid::voice(voice, VoiceParam::Out))) {
        out_ = std::make_unique<ParamValueBox>(ctx_, *p, "OUT", 0, paramid::kNumOutputChoices - 1, true);
        out_->setTooltip("Output bus. A voice on an individual out leaves the main mix.");
        addAndMakeVisible(*out_);
    }
    if (auto* m = ctx_.param(paramid::voice(voice, VoiceParam::Mute))) {
        mute_ = std::make_unique<ParamButton>(ctx_, *m, "M", C::yellow);
        addAndMakeVisible(*mute_);
    }
    if (auto* s = ctx_.param(paramid::voice(voice, VoiceParam::Solo))) {
        solo_ = std::make_unique<ParamButton>(ctx_, *s, "S", C::ledGreenOn);
        addAndMakeVisible(*solo_);
    }
    addAndMakeVisible(lamp_);
    plate_.onAudition = [this](bool accent) { ctx_.processor.audition(static_cast<VoiceId>(voice_), accent); };
    plate_.onSelect = [this] { ctx_.state.setSelectedTrack(voice_); };
    addAndMakeVisible(plate_);
}

void MixerChannel::tick() {
    if (lampAge_ > 6) return;
    ++lampAge_;
    lamp_.setLit(lampAge_ <= 2 ? 1.0f : juce::jmax(0.0f, 1.0f - static_cast<float>(lampAge_ - 2) / 4.0f));
}

void MixerChannel::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    mw808LookAndFeel::drawPlate(g, r, C::plate, C::plateEdge, 4.0f);
    g.setColour(accent_);
    g.fillRoundedRectangle(r.removeFromTop(4.0f).reduced(4.0f, 0.0f).translated(0.0f, 2.0f), 1.5f);
}

void MixerChannel::resized() {
    auto r = getLocalBounds().reduced(3, 0);
    r.removeFromTop(9);
    plate_.setBounds(r.removeFromBottom(38));
    r.removeFromBottom(4);
    {
        auto row = r.removeFromBottom(20);
        auto lampArea = row.removeFromLeft(16);
        lamp_.setBounds(lampArea.withSizeKeepingCentre(10, 10));
        row.removeFromLeft(2);
        const int bw = (row.getWidth() - 2) / 2;
        if (mute_) mute_->setBounds(row.removeFromLeft(bw));
        row.removeFromLeft(2);
        if (solo_) solo_->setBounds(row);
    }
    r.removeFromBottom(6);
    if (pan_) pan_->setBounds(r.removeFromTop(60).reduced(4, 0));
    if (drive_) drive_->setBounds(r.removeFromTop(60).reduced(4, 0));
    r.removeFromTop(2);
    if (out_) out_->setBounds(r.removeFromTop(38).reduced(2, 0));
    r.removeFromTop(4);
    if (level_) level_->setBounds(r.reduced(2, 0));
}

// --- MasterSection ------------------------------------------------------------------------

MasterSection::MasterSection(Context& ctx) : ctx_(ctx) {
    auto make = [&](const char* id, const juce::String& legend, ParamControl::Style style, bool hw) {
        auto* p = ctx_.param(id);
        if (p == nullptr) return std::unique_ptr<ParamControl>();
        auto c = std::make_unique<ParamControl>(ctx_, *p, style, legend, hw, hw ? C::arc : C::arcExt);
        addAndMakeVisible(*c);
        return c;
    };
    drive_ = make(paramid::masterDrive, "DRIVE", ParamControl::Style::Knob, false);
    comp_ = make(paramid::masterComp, "COMP", ParamControl::Style::Knob, false);
    volume_ = make(paramid::masterVolume, "MASTER VOLUME", ParamControl::Style::Fader, true);
    if (auto* p = ctx_.param(paramid::limiter)) {
        limiter_ = std::make_unique<ParamButton>(ctx_, *p, "LIMITER", C::yellow, true);
        limiter_->setTooltip("Soft-clip safety limiter at the very end of the main bus");
        addAndMakeVisible(*limiter_);
    }
    if (auto* p = ctx_.param(paramid::analogOut)) {
        analog_ = std::make_unique<ParamButton>(ctx_, *p, "ANALOG OUT", C::orange, true);
        analog_->setTooltip("The hardware output stage: ~48 Hz high-pass and op-amp saturation");
        addAndMakeVisible(*analog_);
    }
}

void MasterSection::resized() {
    auto r = getLocalBounds().reduced(8, 8);
    title_ = r.removeFromTop(12);
    r.removeFromTop(6);
    {
        auto row = r.removeFromTop(70);
        const int half = row.getWidth() / 2;
        if (drive_) drive_->setBounds(row.removeFromLeft(half));
        if (comp_) comp_->setBounds(row);
    }
    r.removeFromTop(6);
    if (analog_) analog_->setBounds(r.removeFromTop(24));
    r.removeFromTop(4);
    if (limiter_) limiter_->setBounds(r.removeFromTop(24));
    r.removeFromTop(10);
    if (volume_) volume_->setBounds(r.reduced(20, 0));
    invalidateCache();
}

void MasterSection::paintStatic(juce::Graphics& g) {
    mw808LookAndFeel::drawPlate(g, getLocalBounds().toFloat(), C::plate, C::plateEdge, 4.0f);
    mw808LookAndFeel::drawSectionTitle(g, "MASTER", title_);
}

// --- MixerView -----------------------------------------------------------------------------

MixerView::MixerView(Context& ctx) : ctx_(ctx), master_(ctx) {
    for (int v = 0; v < kNumVoices; ++v) addAndMakeVisible(channels_.add(new MixerChannel(ctx_, v)));
    addAndMakeVisible(master_);
    ctx_.state.addListener(this);
    uiStateChanged();
}

MixerView::~MixerView() { ctx_.state.removeListener(this); }

void MixerView::paint(juce::Graphics& g) { g.fillAll(C::panel); }

void MixerView::resized() {
    auto r = getLocalBounds().reduced(kGap, kGap);
    master_.setBounds(r.removeFromRight(kMasterWidth));
    r.removeFromRight(kGap);
    const int n = channels_.size();
    const int gap = 3;
    for (int i = 0; i < n; ++i) {
        const int w = (r.getWidth() - gap * (n - 1 - i)) / (n - i);
        channels_[i]->setBounds(r.removeFromLeft(w));
        r.removeFromLeft(gap);
    }
}

void MixerView::uiStateChanged() {
    const int sel = ctx_.state.selectedTrack();
    for (int v = 0; v < channels_.size(); ++v) channels_[v]->setSelected(v == sel);
}

void MixerView::poll(const PlayState& ps) {
    for (int v = 0; v < kNumVoices; ++v) {
        const auto h = ps.hits[static_cast<std::size_t>(v)].load(std::memory_order_relaxed);
        if (hitsPrimed_ && h != lastHits_[static_cast<std::size_t>(v)]) channels_[v]->flash();
        lastHits_[static_cast<std::size_t>(v)] = h;
        channels_[v]->tick();
    }
    hitsPrimed_ = true;
}

}  // namespace mw808::ui

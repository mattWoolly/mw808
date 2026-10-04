// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "SequencerStrip.h"

#include "mw808/seq/PatternOps.h"

namespace mw808::ui {

namespace C = mw808Colours;
namespace paramid = plugin::paramid;

namespace {
constexpr int kStepsPerPage = 16;
constexpr int kNumPages = seq::kMaxSteps / kStepsPerPage;
constexpr int kLeftWidth = 120;
constexpr int kRightWidth = 258;
constexpr int kPad = 8;
constexpr int kBlinkTicks = 8;  // 30 Hz ticks per half period
}  // namespace

// --- stepedit::showStepMenu --------------------------------------------------------------------

namespace stepedit {

void showStepMenu(Context& ctx, int patternIndex, int variation, int track, int step, juce::Component& target) {
    const seq::Step s = ctx.processor.patterns().pattern(patternIndex).at(variation, track, step);
    juce::PopupMenu menu;
    menu.setLookAndFeel(&ctx.lnf);
    menu.addSectionHeader("Step " + juce::String(step + 1));
    const bool accent = isAccentTrack(track);
    if (accent) {
        menu.addItem(1, "Off", true, !s.on());
        menu.addItem(4, "Accent", true, s.on());
    } else {
        menu.addItem(1, "Off", true, s.level() == seq::StepLevel::Off);
        menu.addItem(2, "Weak", true, s.level() == seq::StepLevel::Weak);
        menu.addItem(3, "Normal", true, s.level() == seq::StepLevel::Normal);
        menu.addItem(4, "Accent", true, s.level() == seq::StepLevel::Accent);
        menu.addSeparator();
        menu.addItem(10, "Flam", s.on(), s.flam());
        juce::PopupMenu sub;
        for (int n = 1; n <= 4; ++n) sub.addItem(20 + n, "x" + juce::String(n), s.on(), s.subSteps() == n);
        menu.addSubMenu("Sub-steps", sub, s.on());
        juce::PopupMenu prob;
        for (int c = 0; c <= 7; ++c)
            prob.addItem(30 + c, juce::String(juce::roundToInt(static_cast<float>(8 - c) / 8.0f * 100.0f)) + " %", s.on(),
                         s.probabilityCode() == c);
        menu.addSubMenu("Probability", prob, s.on());
    }
    juce::Component::SafePointer<juce::Component> safe(&target);
    mw808Processor* processor = &ctx.processor;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target).withParentComponent(&ctx.menuParent),
                       [safe, processor, patternIndex, variation, track, step](int result) {
                           if (safe == nullptr || result <= 0) return;
                           processor->patterns().edit(patternIndex, [&](seq::Pattern& p) {
                               seq::Step& st = p.at(variation, track, step);
                               if (result >= 1 && result <= 4) {
                                   st.setLevel(static_cast<seq::StepLevel>(result - 1));
                                   if (!st.on()) { st.setFlam(false); st.setSubSteps(1); st.setProbabilityCode(0); }
                               } else if (result == 10) st.setFlam(!st.flam());
                               else if (result >= 21 && result <= 24) st.setSubSteps(result - 20);
                               else if (result >= 30 && result <= 37) st.setProbabilityCode(result - 30);
                           });
                       });
}

}  // namespace stepedit

// --- StepKey ------------------------------------------------------------------------------------

StepKey::StepKey(int indexInPage) : index_(indexInPage) { setRepaintsOnMouseActivity(false); }

void StepKey::setState(const State& s) {
    if (s.step == state_.step && s.inRange == state_.inRange && s.running == state_.running &&
        s.absoluteStep == state_.absoluteStep && s.mode == state_.mode && s.track == state_.track)
        return;
    state_ = s;
    repaint();
}

void StepKey::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    const juce::Colour colour = C::stepGroup(index_);
    // LED above the key: the step's state, bright white-amber on the running step.
    auto ledRow = r.removeFromTop(9.0f);
    const auto led = ledRow.withSizeKeepingCentre(juce::jmin(ledRow.getWidth() * 0.55f, 26.0f), 6.0f);
    float lit = 0.0f;
    if (state_.inRange && state_.step.on()) lit = state_.step.level() == seq::StepLevel::Weak ? 0.45f : 1.0f;
    if (state_.running) mw808LookAndFeel::drawLedBar(g, led, 1.0f, C::ledRun, C::ledOff);
    else mw808LookAndFeel::drawLedBar(g, led, lit, C::ledOn, C::ledOff);
    r.removeFromTop(4.0f);

    mw808LookAndFeel::KeyState ks;
    ks.colour = state_.inRange ? colour : colour.withMultipliedSaturation(0.3f).darker(0.5f);
    ks.level = state_.inRange ? static_cast<int>(state_.step.level()) : 0;
    ks.running = state_.running;
    ks.hover = hover_;
    ks.down = down_;
    ks.caption = juce::String(state_.absoluteStep + 1);
    ks.badge = state_.inRange ? stepedit::badge(state_.step, state_.mode, state_.track) : juce::String();
    mw808LookAndFeel::drawStepKey(g, r, ks);
}

void StepKey::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onMenu) onMenu(index_, *this);
        return;
    }
    down_ = true;
    repaint();
    if (onPress) onPress(index_, e);
}

void StepKey::mouseDrag(const juce::MouseEvent& e) {
    if (!down_ || onDragOver == nullptr) return;
    auto* parent = getParentComponent();
    if (parent == nullptr) return;
    const auto pos = e.getEventRelativeTo(parent).getPosition();
    for (auto* sibling : parent->getChildren())
        if (auto* key = dynamic_cast<StepKey*>(sibling))
            if (key->getBounds().contains(pos)) {
                onDragOver(key->index());
                return;
            }
}

void StepKey::mouseUp(const juce::MouseEvent&) {
    down_ = false;
    repaint();
}

// --- PatternButton ---------------------------------------------------------------------------------

void PatternButton::setBlink(bool blinking, bool phase) {
    if (blinking == blinking_ && (phase == phase_ || !blinking)) return;
    blinking_ = blinking;
    phase_ = phase;
    repaint();
}

void PatternButton::setPlaying(bool playing) {
    if (playing == playing_) return;
    playing_ = playing;
    repaint();
}

void PatternButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    juce::TextButton::paintButton(g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    if (blinking_ && phase_) {
        g.setColour(C::cream);
        g.drawRoundedRectangle(r.reduced(1.0f), 3.0f, 2.0f);
    }
    if (playing_) {
        g.setColour(C::ledGreenOn);
        g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre({r.getRight() - 4.5f, r.getY() + 4.5f}));
    }
}

// --- MidiDragHandle ---------------------------------------------------------------------------------

MidiDragHandle::MidiDragHandle(Context& ctx) : ctx_(ctx) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip("Drag the selected pattern out as a MIDI file. Drop a .mid file here to import it.");
}

void MidiDragHandle::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    mw808LookAndFeel::drawPlate(g, r, dropHover_ ? C::plateRaised.interpolatedWith(C::orange, 0.4f) : C::plateRaised,
                                dropHover_ ? C::orange : C::plateEdge, 3.0f);
    if (hover_) {
        g.setColour(C::hover);
        g.fillRoundedRectangle(r, 3.0f);
    }
    auto inner = getLocalBounds().reduced(6, 2);
    // Grip dots.
    auto grip = inner.removeFromLeft(14).toFloat();
    g.setColour(C::creamDim);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillEllipse(grip.getX() + 2.0f + static_cast<float>(col) * 5.0f,
                          grip.getCentreY() - 6.0f + static_cast<float>(row) * 5.0f, 2.5f, 2.5f);
    g.setColour(C::cream);
    g.setFont(mw808LookAndFeel::legend(10.0f));
    g.drawFittedText("MIDI", inner.removeFromTop(inner.getHeight() / 2), juce::Justification::centredLeft, 1);
    g.setColour(C::creamDim);
    g.setFont(mw808LookAndFeel::text(9.0f));
    g.drawFittedText(dropHover_ ? "drop to import" : "drag out / drop in", inner, juce::Justification::centredLeft, 1, 0.7f);
}

void MidiDragHandle::mouseDrag(const juce::MouseEvent& e) {
    if (dragStarted_ || e.getDistanceFromDragStart() < 6) return;
    dragStarted_ = true;
    auto& p = ctx_.processor;
    const juce::File file = p.exportPatternToMidiFile(p.patterns().selectedPattern(), ctx_.editVariation());
    if (!file.existsAsFile()) return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this);
}

bool MidiDragHandle::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi")) return true;
    return false;
}

void MidiDragHandle::filesDropped(const juce::StringArray& files, int, int) {
    dropHover_ = false;
    repaint();
    for (const auto& f : files) {
        if (!(f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))) continue;
        auto& p = ctx_.processor;
        p.importMidiFile(juce::File(f), p.patterns().selectedPattern(), ctx_.editVariation());
        break;
    }
}

// --- ChainEditor -------------------------------------------------------------------------------------

ChainEditor::ChainEditor(Context& ctx) : ctx_(ctx) {
    for (auto* b : {&addButton_, &removeButton_, &clearButton_, &closeButton_}) {
        styleButton(*b, C::orange, false, 10.0f);
        addAndMakeVisible(b);
    }
    styleButton(closeButton_, C::cream, false, 10.0f);
    addButton_.onClick = [this] {
        const auto sel = static_cast<std::uint8_t>(ctx_.processor.patterns().selectedPattern());
        edit([sel](seq::Chain& c) {
            if (c.length < seq::kMaxChainLength) c.entries[c.length++] = sel;
        });
    };
    removeButton_.onClick = [this] { edit([](seq::Chain& c) { if (c.length > 0) --c.length; }); };
    clearButton_.onClick = [this] { edit([](seq::Chain& c) { c = seq::Chain{}; }); };
    closeButton_.onClick = [this] { if (onClose) onClose(); };
    ctx_.processor.patterns().addListener(this);
    // The CHAIN ON / OFF note follows the chain parameter.
    if (auto* p = ctx_.param(paramid::chain))
        chainAttachment_ = std::make_unique<juce::ParameterAttachment>(*p, [this](float) { repaint(); });
}

ChainEditor::~ChainEditor() {
    chainAttachment_.reset();
    ctx_.processor.patterns().removeListener(this);
}

void ChainEditor::edit(std::function<void(seq::Chain&)> fn) {
    seq::Chain c = ctx_.processor.patterns().chain();
    fn(c);
    ctx_.processor.patterns().setChain(c);
    repaint();
}

void ChainEditor::resized() {
    auto r = getLocalBounds().reduced(12);
    r.removeFromTop(22);
    auto buttons = r.removeFromBottom(24);
    closeButton_.setBounds(buttons.removeFromRight(70));
    addButton_.setBounds(buttons.removeFromLeft(110));
    buttons.removeFromLeft(4);
    removeButton_.setBounds(buttons.removeFromLeft(100));
    buttons.removeFromLeft(4);
    clearButton_.setBounds(buttons.removeFromLeft(70));
    r.removeFromBottom(8);
    chipArea_ = r;
}

juce::Rectangle<int> ChainEditor::chipBounds(int index) const {
    const int chipW = 46, chipH = 20, gap = 4;
    const int perRow = juce::jmax(1, (chipArea_.getWidth() + gap) / (chipW + gap));
    const int row = index / perRow, col = index % perRow;
    return {chipArea_.getX() + col * (chipW + gap), chipArea_.getY() + row * (chipH + gap), chipW, chipH};
}

void ChainEditor::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    mw808LookAndFeel::drawPlate(g, r, C::panelDeep, C::orange, 5.0f);
    const auto& chain = ctx_.processor.patterns().chain();
    auto title = getLocalBounds().reduced(12).removeFromTop(22);
    g.setColour(C::cream);
    g.setFont(mw808LookAndFeel::legend(12.0f));
    g.drawText("CHAIN   " + juce::String(static_cast<int>(chain.length)) + " / " + juce::String(seq::kMaxChainLength) +
                   "   (each measure plays the next entry; click an entry to remove it)",
               title, juce::Justification::centredLeft, false);
    const bool chainOn = ctx_.raw(paramid::chain) >= 0.5f;
    g.setColour(chainOn ? C::ledGreenOn : C::creamDim);
    g.setFont(mw808LookAndFeel::legend(10.0f));
    g.drawText(chainOn ? "CHAIN ON" : "CHAIN OFF (toggle with the CHAIN button)", title, juce::Justification::centredRight, false);
    for (int i = 0; i < chain.length; ++i) {
        const auto chip = chipBounds(i).toFloat();
        if (chip.getBottom() > static_cast<float>(chipArea_.getBottom())) break;
        const int pat = chain.entries[static_cast<std::size_t>(i)];
        mw808LookAndFeel::drawButtonFace(g, chip, true, false, false, C::stepGroup(pat % seq::kPatternsPerBank));
        g.setColour(C::cream);
        g.setFont(mw808LookAndFeel::bold(11.0f));
        g.drawText(patternLabel(pat), chip.toNearestInt(), juce::Justification::centred, false);
    }
    if (chain.length == 0) {
        g.setColour(C::creamDim);
        g.setFont(mw808LookAndFeel::text(12.0f));
        g.drawText("Empty. Select a pattern in the strip and press ADD SELECTED.", chipArea_, juce::Justification::centred, false);
    }
}

void ChainEditor::mouseDown(const juce::MouseEvent& e) {
    const auto& chain = ctx_.processor.patterns().chain();
    for (int i = 0; i < chain.length; ++i)
        if (chipBounds(i).contains(e.getPosition())) {
            edit([i](seq::Chain& c) {
                for (int k = i; k + 1 < c.length; ++k) c.entries[static_cast<std::size_t>(k)] = c.entries[static_cast<std::size_t>(k + 1)];
                --c.length;
            });
            return;
        }
}

// --- SequencerStrip -----------------------------------------------------------------------------------

SequencerStrip::SequencerStrip(Context& ctx) : ctx_(ctx), midiHandle_(ctx) {
    setOpaque(true);
    auto& model = ctx_.processor.patterns();

    // Left block.
    if (auto* p = ctx_.param(paramid::seqRun)) {
        run_ = std::make_unique<ParamButton>(ctx_, *p, "START / STOP", C::red, false);
        run_->getProperties().set("fontHeight", 11.0);
        run_->setTooltip("Run the internal clock (in Host Sync the host transport plays the sequencer)");
        addAndMakeVisible(*run_);
    }
    addAndMakeVisible(playLamp_);
    if (auto* p = ctx_.param(paramid::variation)) {
        variation_ = std::make_unique<ChoiceButtons>(ctx_, *p, juce::StringArray{"A", "AB", "B"}, false, C::orange);
        addAndMakeVisible(*variation_);
    }
    if (auto* p = ctx_.param(paramid::record)) {
        record_ = std::make_unique<ParamButton>(ctx_, *p, "REC", C::record, true);
        addAndMakeVisible(*record_);
    }
    styleButton(fillButton_, C::yellow, false, 10.0f);
    fillButton_.onClick = [this] { ctx_.processor.requestFill(); };
    fillButton_.setTooltip("Play the fill pattern for the next measure");
    addAndMakeVisible(fillButton_);
    if (auto* p = ctx_.param(paramid::chain)) {
        chain_ = std::make_unique<ParamButton>(ctx_, *p, "CHAIN", C::ledGreenOn, true);
        addAndMakeVisible(*chain_);
    }
    styleButton(chainEditButton_, C::cream, false, 10.0f);
    chainEditButton_.setClickingTogglesState(true);
    chainEditButton_.onClick = [this] { if (onChainEditToggle) onChainEditToggle(chainEditButton_.getToggleState()); };
    addAndMakeVisible(chainEditButton_);

    // Banks, patterns, pages.
    for (int b = 0; b < seq::kNumBanks; ++b) {
        auto* btn = bankButtons_.add(new juce::TextButton(juce::String::charToString(static_cast<juce::juce_wchar>('A' + b))));
        styleButton(*btn, C::cream, false, 11.0f);
        btn->onClick = [this, b] { shownBank_ = b; refreshPatternButtons(); };
        addAndMakeVisible(btn);
    }
    for (int i = 0; i < seq::kPatternsPerBank; ++i) {
        const bool fill = i >= seq::kFirstFillSlot;
        auto* btn = patternButtons_.add(new PatternButton(fill ? "F" + juce::String(i - seq::kFirstFillSlot + 1) : juce::String(i + 1)));
        styleButton(*btn, fill ? C::yellow : C::orange, false, 10.0f);
        btn->onClick = [this, i] {
            const int index = shownBank_ * seq::kPatternsPerBank + i;
            const bool playing = ctx_.processor.playState().playing.load(std::memory_order_relaxed);
            const bool immediate = juce::ModifierKeys::currentModifiers.isShiftDown() || !playing;
            ctx_.processor.patterns().selectPattern(index, immediate);
        };
        btn->setTooltip(fill ? "Intro / fill pattern (click: queue, shift-click: switch now)" : "Click: queue for the next measure. Shift-click: switch now.");
        addAndMakeVisible(btn);
    }
    for (int pg = 0; pg < kNumPages; ++pg) {
        auto* btn = pageButtons_.add(new juce::TextButton(juce::String(pg + 1)));
        styleButton(*btn, C::cream, true, 10.0f);
        btn->onClick = [this, pg] { ctx_.state.setPage(pg); };
        addAndMakeVisible(btn);
    }

    // Keys.
    for (int i = 0; i < kStepsPerPage; ++i) {
        auto* key = keys_.add(new StepKey(i));
        key->onPress = [this](int index, const juce::MouseEvent& e) { stepKeyPressed(index, e); };
        key->onDragOver = [this](int index) { keyDragged(index); };
        key->onMenu = [this](int index, juce::Component& target) {
            const int step = ctx_.state.page() * kStepsPerPage + index;
            if (step >= pattern().lengthOf(ctx_.state.selectedTrack())) return;
            stepedit::showStepMenu(ctx_, selectedPattern(), editVariation(), ctx_.state.selectedTrack(), step, target);
        };
        addAndMakeVisible(key);
    }

    // Tracks.
    for (int t = 0; t < seq::kNumTracks; ++t) {
        const juce::String code = t < kNumVoices ? juce::String(std::string(kVoiceInfo[static_cast<std::size_t>(t)].code)) : juce::String("AC");
        auto* btn = trackButtons_.add(new juce::TextButton(code));
        styleButton(*btn, C::trackAccent(t), false, 10.0f);
        btn->onClick = [this, t] { ctx_.state.setSelectedTrack(t); };
        btn->setTooltip(t < kNumVoices ? juce::String(std::string(kVoiceInfo[static_cast<std::size_t>(t)].name)) : juce::String("Global accent row"));
        addAndMakeVisible(btn);
    }

    // Modes and tools.
    for (int m = 0; m < kNumStepModes; ++m) {
        auto* btn = modeButtons_.add(new juce::TextButton(kStepModeNames[m]));
        styleButton(*btn, C::cream, false, 10.0f);
        btn->onClick = [this, m] { ctx_.state.setStepMode(static_cast<StepMode>(m)); };
        addAndMakeVisible(btn);
    }
    modeButtons_[0]->setTooltip("Click: Off > Normal > Accent. Shift-click: Weak.");
    modeButtons_[1]->setTooltip("Click an active step to toggle its flam");
    modeButtons_[2]->setTooltip("Click an active step to cycle sub-steps x1..x4 (shift: backwards)");
    modeButtons_[3]->setTooltip("Click an active step to cycle its probability (shift: backwards)");
    for (auto* b : {&copyButton_, &pasteButton_, &clearButton_, &shiftLeftButton_, &shiftRightButton_, &randomButton_, &euclidButton_}) {
        styleButton(*b, C::creamDim, false, 9.5f);
        addAndMakeVisible(b);
    }
    copyButton_.setTooltip("Copy the pattern (shift: the selected track)");
    pasteButton_.setTooltip("Paste the pattern (shift: the selected track)");
    clearButton_.setTooltip("Clear this variation (shift: the selected track)");
    shiftLeftButton_.setTooltip("Rotate one step earlier (shift: the selected track)");
    shiftRightButton_.setTooltip("Rotate one step later (shift: the selected track)");
    randomButton_.setTooltip("Random pattern (shift: the selected track)");
    euclidButton_.setTooltip("Euclidean rhythm on the selected track");
    copyButton_.onClick = [this] { copy(juce::ModifierKeys::currentModifiers.isShiftDown()); };
    pasteButton_.onClick = [this] { paste(juce::ModifierKeys::currentModifiers.isShiftDown()); };
    clearButton_.onClick = [this] { clear(juce::ModifierKeys::currentModifiers.isShiftDown()); };
    shiftLeftButton_.onClick = [this] { shift(-1, juce::ModifierKeys::currentModifiers.isShiftDown()); };
    shiftRightButton_.onClick = [this] { shift(1, juce::ModifierKeys::currentModifiers.isShiftDown()); };
    randomButton_.onClick = [this] { randomise(juce::ModifierKeys::currentModifiers.isShiftDown()); };
    euclidButton_.onClick = [this] { showEuclidMenu(); };

    // Right block: pattern fields.
    length_ = std::make_unique<ValueBox>("LENGTH", 1, seq::kMaxSteps, 16);
    length_->getValue = [this] { return static_cast<int>(pattern().length); };
    length_->setValue = [this](int v) {
        ctx_.processor.patterns().edit(selectedPattern(), [v](seq::Pattern& p) { p.length = static_cast<std::uint8_t>(v); });
    };
    scale_ = std::make_unique<ValueBox>("SCALE", 0, static_cast<int>(seq::Scale::kNumScales) - 1, static_cast<int>(seq::Scale::Sixteenth));
    for (int s = 0; s < static_cast<int>(seq::Scale::kNumScales); ++s) scale_->choices.add(stepedit::scaleName(static_cast<seq::Scale>(s)));
    scale_->menuParent = &ctx_.menuParent;
    scale_->getValue = [this] { return static_cast<int>(pattern().scale); };
    scale_->setValue = [this](int v) {
        ctx_.processor.patterns().edit(selectedPattern(), [v](seq::Pattern& p) { p.scale = static_cast<seq::Scale>(v); });
    };
    scale_->format = [](int v) { return juce::String(stepedit::scaleName(static_cast<seq::Scale>(v))); };
    shuffle_ = std::make_unique<ValueBox>("SHUFFLE", 0, 100, 0);
    shuffle_->getValue = [this] { return static_cast<int>(pattern().shuffle); };
    shuffle_->setValue = [this](int v) {
        ctx_.processor.patterns().edit(selectedPattern(), [v](seq::Pattern& p) { p.shuffle = static_cast<std::uint8_t>(v); });
    };
    shuffle_->format = [](int v) { return juce::String(v) + "%"; };
    trackLength_ = std::make_unique<ValueBox>("TRACK LEN", 0, seq::kMaxSteps, 0);
    trackLength_->getValue = [this] {
        const int t = ctx_.state.selectedTrack();
        return t < kNumVoices ? static_cast<int>(pattern().trackLength[static_cast<std::size_t>(t)]) : 0;
    };
    trackLength_->setValue = [this](int v) {
        const int t = ctx_.state.selectedTrack();
        if (t >= kNumVoices) return;
        ctx_.processor.patterns().edit(selectedPattern(), [v, t](seq::Pattern& p) { p.trackLength[static_cast<std::size_t>(t)] = static_cast<std::uint8_t>(v); });
    };
    trackLength_->format = [](int v) { return v == 0 ? juce::String("= PTN") : juce::String(v); };
    trackLength_->setTooltip("Loop length of the selected voice track (polymeter). 0 follows the pattern length.");
    for (auto* vb : {length_.get(), scale_.get(), shuffle_.get(), trackLength_.get()}) addAndMakeVisible(vb);
    if (auto* p = ctx_.param(paramid::autoFill)) {
        autoFill_ = std::make_unique<ParamValueBox>(ctx_, *p, "AUTO FILL", 0, 5, true);
        autoFill_->setTooltip("Play the fill every N measures");
        addAndMakeVisible(*autoFill_);
    }
    if (auto* p = ctx_.param(paramid::fillSlot)) {
        fillSlot_ = std::make_unique<ParamValueBox>(ctx_, *p, "FILL SLOT", 0, 3, true);
        fillSlot_->setTooltip("Which of the bank's four fill patterns plays");
        addAndMakeVisible(*fillSlot_);
    }
    addAndMakeVisible(midiHandle_);

    model.addListener(this);
    ctx_.state.addListener(this);
    shownBank_ = model.selectedPattern() / seq::kPatternsPerBank;
    shownVariation_ = editVariation();
    refreshPatternButtons();
    refreshTrackButtons();
    refreshPageButtons();
    refreshModeButtons();
    refreshKeys();
}

SequencerStrip::~SequencerStrip() {
    ctx_.state.removeListener(this);
    ctx_.processor.patterns().removeListener(this);
}

void SequencerStrip::resized() {
    auto r = getLocalBounds().reduced(kPad, kPad);

    // Left block.
    auto left = r.removeFromLeft(kLeftWidth);
    if (run_) {
        auto row = left.removeFromTop(44);
        playLamp_.setBounds(row.removeFromLeft(14).withSizeKeepingCentre(9, 9));
        row.removeFromLeft(3);
        run_->setBounds(row);
    }
    left.removeFromTop(8);
    if (variation_) variation_->setBounds(left.removeFromTop(22));
    left.removeFromTop(8);
    {
        auto row = left.removeFromTop(22);
        const int half = (row.getWidth() - 4) / 2;
        if (record_) record_->setBounds(row.removeFromLeft(half));
        row.removeFromLeft(4);
        fillButton_.setBounds(row);
    }
    left.removeFromTop(6);
    {
        auto row = left.removeFromTop(22);
        const int half = (row.getWidth() - 4) / 2;
        if (chain_) chain_->setBounds(row.removeFromLeft(half));
        row.removeFromLeft(4);
        chainEditButton_.setBounds(row);
    }
    left.removeFromTop(8);
    leftLegend_ = left;

    // Right block.
    auto right = r.removeFromRight(kRightWidth);
    patternLegend_ = right.removeFromTop(12);
    right.removeFromTop(3);
    auto layoutRow = [&](juce::Rectangle<int> row, std::initializer_list<juce::Component*> items) {
        const int n = static_cast<int>(items.size());
        const int gap = 6;
        int i = 0;
        for (auto* c : items) {
            const int w = (row.getWidth() - gap * (n - 1 - i)) / (n - i);
            auto cell = row.removeFromLeft(w);
            row.removeFromLeft(gap);
            if (c != nullptr) c->setBounds(cell);
            ++i;
        }
    };
    layoutRow(right.removeFromTop(40), {length_.get(), scale_.get(), shuffle_.get()});
    right.removeFromTop(6);
    layoutRow(right.removeFromTop(40), {trackLength_.get(), autoFill_.get(), fillSlot_.get()});
    right.removeFromTop(10);
    midiHandle_.setBounds(right.removeFromTop(34));
    right.removeFromTop(6);
    rightLegend_ = right;

    // Middle.
    r.removeFromLeft(10);
    r.removeFromRight(10);
    auto row1 = r.removeFromTop(22);
    {
        auto banks = row1.removeFromLeft(4 * 24 + 3 * 2);
        for (auto* b : bankButtons_) { b->setBounds(banks.removeFromLeft(24)); banks.removeFromLeft(2); }
        row1.removeFromLeft(8);
        auto pages = row1.removeFromRight(4 * 31 + 3 * 2);
        for (auto* b : pageButtons_) { b->setBounds(pages.removeFromLeft(31)); pages.removeFromLeft(2); }
        row1.removeFromRight(8);
        const int n = patternButtons_.size();
        for (int i = 0; i < n; ++i) {
            const int w = (row1.getWidth() - 2 * (n - 1 - i)) / (n - i);
            patternButtons_[i]->setBounds(row1.removeFromLeft(w));
            row1.removeFromLeft(2);
        }
    }
    r.removeFromTop(8);
    auto keyRow = r.removeFromTop(92);
    {
        const int n = keys_.size();
        const int gap = 4;
        for (int i = 0; i < n; ++i) {
            const int w = (keyRow.getWidth() - gap * (n - 1 - i)) / (n - i);
            keys_[i]->setBounds(keyRow.removeFromLeft(w));
            keyRow.removeFromLeft(gap);
        }
    }
    r.removeFromTop(8);
    auto trackRow = r.removeFromTop(22);
    {
        const int n = trackButtons_.size();
        const int gap = 2;
        for (int i = 0; i < n; ++i) {
            const int w = (trackRow.getWidth() - gap * (n - 1 - i)) / (n - i);
            trackButtons_[i]->setBounds(trackRow.removeFromLeft(w));
            trackRow.removeFromLeft(gap);
        }
    }
    r.removeFromTop(8);
    auto toolRow = r.removeFromTop(24);
    {
        auto modes = toolRow.removeFromLeft(4 * 54 + 3 * 2);
        for (auto* b : modeButtons_) { b->setBounds(modes.removeFromLeft(54)); modes.removeFromLeft(2); }
        toolRow.removeFromLeft(12);
        juce::TextButton* tools[] = {&copyButton_, &pasteButton_, &clearButton_, &shiftLeftButton_, &shiftRightButton_, &randomButton_, &euclidButton_};
        const int n = 7;
        for (int i = 0; i < n; ++i) {
            const int w = (toolRow.getWidth() - 2 * (n - 1 - i)) / (n - i);
            tools[i]->setBounds(toolRow.removeFromLeft(w));
            toolRow.removeFromLeft(2);
        }
    }
    r.removeFromTop(4);
    hintArea_ = r;
    invalidateCache();
}

void SequencerStrip::paintStatic(juce::Graphics& g) {
    g.fillAll(C::panelDeep);
    g.setColour(C::hairline);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    // Group plates behind the left and right blocks.
    auto bounds = getLocalBounds().reduced(kPad, kPad);
    g.setColour(C::panel);
    g.fillRoundedRectangle(bounds.withWidth(kLeftWidth).expanded(4, 4).toFloat(), 4.0f);
    g.fillRoundedRectangle(bounds.removeFromRight(kRightWidth).expanded(4, 4).toFloat(), 4.0f);
    mw808LookAndFeel::drawSectionTitle(g, "PATTERN", patternLegend_);
    // Legends in the spare space.
    // Copies: paintStatic runs again whenever the cache is re-rendered.
    auto leftLegend = leftLegend_;
    mw808LookAndFeel::drawLegend(g, "SEQUENCER", leftLegend.removeFromTop(12), C::creamFaint, 9.0f);
    g.setColour(C::creamFaint);
    g.setFont(mw808LookAndFeel::text(10.0f));
    g.drawFittedText("Click: off > normal > accent.  Shift-click: weak.  Drag to paint.  Right-click a step for its menu.",
                     hintArea_, juce::Justification::centredLeft, 1, 0.8f);
    g.drawFittedText("Hold shift on a tool to work on the selected track only.", rightLegend_, juce::Justification::topLeft, 2, 0.8f);
}

void SequencerStrip::paintDynamic(juce::Graphics&) {}

// --- model / state listeners ----------------------------------------------------------------------------

void SequencerStrip::patternChanged(int index) {
    if (index == selectedPattern()) {
        refreshKeys();
        refreshPageButtons();  // LENGTH / TRACK LEN decide which pages are reachable
        refreshValueBoxes();
    }
}

void SequencerStrip::bankChanged() {
    refreshPatternButtons();
    refreshKeys();
    refreshPageButtons();
    refreshValueBoxes();
}

void SequencerStrip::selectionChanged() {
    shownBank_ = selectedPattern() / seq::kPatternsPerBank;
    refreshPatternButtons();
    refreshKeys();
    refreshPageButtons();
    refreshValueBoxes();
}

void SequencerStrip::uiStateChanged() {
    refreshTrackButtons();
    refreshPageButtons();
    refreshModeButtons();
    refreshKeys();
    refreshValueBoxes();
}

void SequencerStrip::refreshValueBoxes() {
    for (auto* vb : {length_.get(), scale_.get(), shuffle_.get(), trackLength_.get()}) vb->refresh();
    trackLength_->setEnabled(ctx_.state.selectedTrack() < kNumVoices);
}

void SequencerStrip::refreshKeys() {
    const auto& p = pattern();
    const int track = ctx_.state.selectedTrack();
    const int var = editVariation();
    const int page = ctx_.state.page();
    const int len = p.lengthOf(track);
    const StepMode mode = ctx_.state.stepMode();
    for (int i = 0; i < keys_.size(); ++i) {
        StepKey::State s;
        s.absoluteStep = page * kStepsPerPage + i;
        s.inRange = s.absoluteStep < len;
        s.step = p.at(var, track, s.absoluteStep);
        s.running = runningStep_ == s.absoluteStep;
        s.mode = mode;
        s.track = track;
        keys_[i]->setState(s);
    }
}

void SequencerStrip::refreshPatternButtons() {
    const int sel = selectedPattern();
    for (int b = 0; b < bankButtons_.size(); ++b) bankButtons_[b]->setToggleState(b == shownBank_, juce::dontSendNotification);
    for (int i = 0; i < patternButtons_.size(); ++i) {
        const int index = shownBank_ * seq::kPatternsPerBank + i;
        patternButtons_[i]->setToggleState(index == sel, juce::dontSendNotification);
        patternButtons_[i]->setBlink(index == lastQueued_, blinkPhase_);
        patternButtons_[i]->setPlaying(lastPlaying_ && index == lastCurrent_ && index != sel);
    }
}

void SequencerStrip::refreshTrackButtons() {
    const int sel = ctx_.state.selectedTrack();
    for (int t = 0; t < trackButtons_.size(); ++t) trackButtons_[t]->setToggleState(t == sel, juce::dontSendNotification);
}

void SequencerStrip::refreshPageButtons() {
    const int page = ctx_.state.page();
    const int runPage = runningStep_ >= 0 ? runningStep_ / kStepsPerPage : -1;
    const int len = pattern().lengthOf(ctx_.state.selectedTrack());
    for (int pg = 0; pg < pageButtons_.size(); ++pg) {
        pageButtons_[pg]->setToggleState(pg == page, juce::dontSendNotification);
        pageButtons_[pg]->setEnabled(pg * kStepsPerPage < len || pg == page);
        // The lamp shows where the playhead is.
        auto& props = pageButtons_[pg]->getProperties();
        const auto accent = static_cast<juce::int64>((pg == runPage ? C::ledRun : C::cream).getARGB());
        if (static_cast<juce::int64>(props["accent"]) != accent) {
            props.set("accent", accent);
            pageButtons_[pg]->repaint();
        }
    }
}

void SequencerStrip::refreshModeButtons() {
    const int mode = static_cast<int>(ctx_.state.stepMode());
    for (int m = 0; m < modeButtons_.size(); ++m) modeButtons_[m]->setToggleState(m == mode, juce::dontSendNotification);
}

// --- polling -----------------------------------------------------------------------------------------------

void SequencerStrip::poll(const PlayState& ps) {
    const bool playing = ps.playing.load(std::memory_order_relaxed);
    const int queued = ps.queuedPattern.load(std::memory_order_relaxed);
    const int current = ps.currentPattern.load(std::memory_order_relaxed);
    // The keys show the selected pattern: highlight a running step only when
    // that is the pattern sounding (not while another one or a fill plays).
    int step = playing && current == selectedPattern() ? ps.step.load(std::memory_order_relaxed) : -1;
    // A track with its own loop length (polymeter) runs out of phase with the
    // pattern: show where that track is, the way the sequencer reads it.
    const int track = ctx_.state.selectedTrack();
    if (step >= 0 && track < kNumVoices) {
        const seq::Pattern& p = pattern();
        const int trackLen = p.lengthOf(track);
        if (trackLen != p.length && trackLen > 0) {
            const std::int64_t measure = ps.measure.load(std::memory_order_relaxed);
            step = static_cast<int>((measure * p.length + step) % trackLen);
        }
    }
    const int var = editVariation();

    const int lampState = playing ? 1 : 0;
    if (lampState != runLampState_) {
        runLampState_ = lampState;
        playLamp_.setLit(playing ? 1.0f : 0.0f);
    }

    bool keysDirty = false;
    if (step != runningStep_) {
        const int oldPage = runningStep_ / kStepsPerPage, newPage = step / kStepsPerPage;
        runningStep_ = step;
        keysDirty = true;
        if (oldPage != newPage || step < 0) refreshPageButtons();
    }
    if (var != shownVariation_) {
        shownVariation_ = var;
        keysDirty = true;
    }
    if (keysDirty) refreshKeys();

    bool patternsDirty = false;
    if (queued >= 0) {
        if (++blinkTick_ >= kBlinkTicks) {
            blinkTick_ = 0;
            blinkPhase_ = !blinkPhase_;
            patternsDirty = true;
        }
    } else if (blinkPhase_ || blinkTick_ != 0) {
        blinkPhase_ = false;
        blinkTick_ = 0;
        patternsDirty = true;
    }
    if (queued != lastQueued_ || current != lastCurrent_ || playing != lastPlaying_) {
        lastQueued_ = queued;
        lastCurrent_ = current;
        lastPlaying_ = playing;
        patternsDirty = true;
    }
    if (patternsDirty) refreshPatternButtons();
}

// --- step editing -----------------------------------------------------------------------------------------

void SequencerStrip::editStep(int absoluteStep, std::function<seq::Step(seq::Step)> fn) {
    const int track = ctx_.state.selectedTrack();
    const int var = editVariation();
    ctx_.processor.patterns().edit(selectedPattern(), [&](seq::Pattern& p) {
        if (absoluteStep < 0 || absoluteStep >= seq::kMaxSteps) return;
        seq::Step& s = p.at(var, track, absoluteStep);
        s = fn(s);
    });
}

void SequencerStrip::stepKeyPressed(int index, const juce::MouseEvent& e) {
    painting_ = false;  // a press on a dim key must not leave the previous brush armed
    const int step = ctx_.state.page() * kStepsPerPage + index;
    const int track = ctx_.state.selectedTrack();
    if (step >= pattern().lengthOf(track)) return;
    const StepMode mode = ctx_.state.stepMode();
    const bool shift = e.mods.isShiftDown();
    seq::Step result{};
    editStep(step, [&](seq::Step s) {
        result = stepedit::clicked(s, mode, track, shift);
        return result;
    });
    painting_ = true;
    brush_ = result;
    paintTrack_ = track;
    lastPainted_ = step;
}

void SequencerStrip::keyDragged(int index) {
    if (!painting_) return;
    const int step = ctx_.state.page() * kStepsPerPage + index;
    if (step == lastPainted_) return;
    const int track = ctx_.state.selectedTrack();
    if (track != paintTrack_ || step >= pattern().lengthOf(track)) return;
    lastPainted_ = step;
    const StepMode mode = ctx_.state.stepMode();
    const seq::Step brush = brush_;
    editStep(step, [&](seq::Step s) { return stepedit::painted(s, brush, mode, track); });
}

// --- tools -----------------------------------------------------------------------------------------------------

void SequencerStrip::copy(bool trackOnly) {
    const auto& p = pattern();
    if (trackOnly) {
        const int track = ctx_.state.selectedTrack();
        trackClip_ = p.steps[static_cast<std::size_t>(editVariation())][static_cast<std::size_t>(track)];
        trackClipLength_ = track < kNumVoices ? p.trackLength[static_cast<std::size_t>(track)] : 0;
    } else {
        patternClip_ = p;
    }
}

void SequencerStrip::paste(bool trackOnly) {
    if (trackOnly) {
        if (!trackClip_) return;
        const int track = ctx_.state.selectedTrack();
        const int var = editVariation();
        const auto clip = *trackClip_;
        const auto len = trackClipLength_;
        ctx_.processor.patterns().edit(selectedPattern(), [&](seq::Pattern& p) {
            p.steps[static_cast<std::size_t>(var)][static_cast<std::size_t>(track)] = clip;
            if (track < kNumVoices) p.trackLength[static_cast<std::size_t>(track)] = len;
        });
    } else if (patternClip_) {
        ctx_.processor.patterns().setPattern(selectedPattern(), *patternClip_);
    }
}

void SequencerStrip::clear(bool trackOnly) {
    const int track = ctx_.state.selectedTrack();
    const int var = editVariation();
    ctx_.processor.patterns().edit(selectedPattern(), [&](seq::Pattern& p) {
        if (trackOnly) seq::clearTrack(p, var, track);
        else seq::clearPattern(p, var);
    });
}

void SequencerStrip::shift(int steps, bool trackOnly) {
    const int track = ctx_.state.selectedTrack();
    const int var = editVariation();
    ctx_.processor.patterns().edit(selectedPattern(), [&](seq::Pattern& p) {
        if (trackOnly) seq::shiftTrack(p, var, track, steps);
        else seq::shiftPattern(p, var, steps);
    });
}

void SequencerStrip::randomise(bool trackOnly) {
    const int track = ctx_.state.selectedTrack();
    const int var = editVariation();
    const auto seed = static_cast<std::uint32_t>(juce::Time::getMillisecondCounter()) ^ (randomSeed_++ * 2654435761u);
    const auto densities = stepedit::defaultDensities();
    ctx_.processor.patterns().edit(selectedPattern(), [&](seq::Pattern& p) {
        if (trackOnly) seq::randomizeTrack(p, var, track, densities[static_cast<std::size_t>(track)] + 0.2f, seed);
        else seq::randomizePattern(p, var, densities, seed);
    });
}

void SequencerStrip::showEuclidMenu() {
    const int track = ctx_.state.selectedTrack();
    const int len = pattern().lengthOf(track);
    juce::PopupMenu menu;
    menu.setLookAndFeel(&ctx_.lnf);
    menu.addSectionHeader("Euclidean hits over " + juce::String(len) + " steps");
    for (int hits = 1; hits <= juce::jmin(len, 32); ++hits) menu.addItem(hits, juce::String(hits) + (hits == 1 ? " hit" : " hits"));
    menu.addSeparator();
    menu.addItem(100, "Rotate earlier");
    menu.addItem(101, "Rotate later");
    menu.addItem(102, "Invert track");
    juce::Component::SafePointer<SequencerStrip> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&euclidButton_).withParentComponent(&ctx_.menuParent),
                       [safe, track](int result) {
                           if (safe == nullptr || result <= 0) return;
                           auto& self = *safe;
                           const int var = self.editVariation();
                           self.ctx_.processor.patterns().edit(self.selectedPattern(), [&](seq::Pattern& p) {
                               if (result <= 64) seq::setEuclidean(p, var, track, result, 0);
                               else if (result == 100) seq::shiftTrack(p, var, track, -1);
                               else if (result == 101) seq::shiftTrack(p, var, track, 1);
                               else if (result == 102) seq::invertTrack(p, var, track);
                           });
                       });
}

}  // namespace mw808::ui

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "HeaderBar.h"

#include <cmath>

#ifndef MW808_VERSION_STRING
 #define MW808_VERSION_STRING "dev"
#endif

namespace mw808::ui {

namespace C = mw808Colours;
namespace paramid = plugin::paramid;
using plugin::PresetKind;

namespace {

constexpr int kNumViews = 3;
const char* const kViewNames[kNumViews] = {"PANEL", "GRID", "MIXER"};
const char* const kKindNames[plugin::kNumPresetKinds] = {"KIT", "PATTERNS", "FULL"};

// Menu ids of the settings menu.
enum SettingsId {
    kQualityBase = 100,     // + 0..2
    kVarianceBase = 200,    // + 0..4 (0, 25, 50, 75, 100 %)
    kSerialRandom = 300,
    kSerialEnter = 301,
    kScaleBase = 400,       // + index into kScales
    kVelocitySens = 500,
    kMidiOut = 501,
    kBassChannelBase = 600, // + 0..16
    kClearMidiLearn = 700,
    kPanic = 701,
    kAbout = 702,
};

constexpr float kScales[] = {0.6f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
constexpr int kNumScales = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));

}  // namespace

// --- OutputMeter -------------------------------------------------------------------------------

OutputMeter::OutputMeter() { setInterceptsMouseClicks(false, false); }

float OutputMeter::toDisplay(float linear) noexcept {
    if (linear <= 1.0e-5f) return 0.0f;
    const float db = 20.0f * std::log10(linear);
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
}

void OutputMeter::setPeaks(float left, float right) {
    const float l = toDisplay(left), r = toDisplay(right);
    // Fast attack, ~1.5 s fall from full scale at 30 Hz.
    const float fall = 0.022f;
    const float nl = l > shownL_ ? l : juce::jmax(l, shownL_ - fall);
    const float nr = r > shownR_ ? r : juce::jmax(r, shownR_ - fall);
    bool changed = std::abs(nl - shownL_) > 0.002f || std::abs(nr - shownR_) > 0.002f;
    shownL_ = nl;
    shownR_ = nr;
    // Peak hold for ~1 s.
    // Only a moving hold marker counts as a change: at silence (0 >= 0) the meter must not repaint.
    auto hold = [&](float v, float& held, int& age) {
        if (v >= held) {
            if (v > held) changed = true;
            held = v;
            age = 0;
        } else if (++age > 30) {
            held = juce::jmax(v, held - fall * 2.0f);
            changed = true;
        }
    };
    hold(nl, holdL_, holdAgeL_);
    hold(nr, holdR_, holdAgeR_);
    const bool cl = left >= 0.999f, cr = right >= 0.999f;
    if (cl != clipL_ || cr != clipR_) { clipL_ = cl; clipR_ = cr; changed = true; }
    if (changed) repaint();
}

void OutputMeter::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    const auto legend = r.removeFromLeft(22);
    mw808LookAndFeel::drawLegend(g, "OUT", legend, C::creamDim, 9.0f);
    mw808LookAndFeel::drawInset(g, r.toFloat());
    auto inner = r.reduced(3, 3);
    const int barH = (inner.getHeight() - 2) / 2;
    auto drawBar = [&](juce::Rectangle<int> bar, float v, float held, bool clip) {
        const float w = static_cast<float>(bar.getWidth());
        const float zero = (60.0f / 66.0f);  // 0 dBFS position
        // Segments: green to the -12 dB point, yellow to 0 dB, red above.
        const float x12 = w * (48.0f / 66.0f);
        const float x0 = w * zero;
        const float fill = w * v;
        auto seg = [&](float from, float to, juce::Colour c) {
            const float a = juce::jlimit(0.0f, fill, from), b = juce::jlimit(0.0f, fill, to);
            if (b > a) {
                g.setColour(c);
                g.fillRect(static_cast<float>(bar.getX()) + a, static_cast<float>(bar.getY()), b - a,
                           static_cast<float>(bar.getHeight()));
            }
        };
        seg(0.0f, x12, C::meterLow);
        seg(x12, x0, C::meterMid);
        seg(x0, w, C::meterHot);
        if (held > 0.01f) {
            g.setColour(held > zero ? C::meterHot : C::cream);
            g.fillRect(static_cast<float>(bar.getX()) + w * held - 1.0f, static_cast<float>(bar.getY()), 2.0f,
                       static_cast<float>(bar.getHeight()));
        }
        if (clip) {
            g.setColour(C::meterHot);
            g.fillRect(bar.removeFromRight(3));
        }
        // 0 dB tick.
        g.setColour(C::creamFaint);
        g.fillRect(static_cast<float>(bar.getX()) + x0, static_cast<float>(bar.getY()), 1.0f, static_cast<float>(bar.getHeight()));
    };
    drawBar(inner.removeFromTop(barH), shownL_, holdL_, clipL_);
    inner.removeFromTop(2);
    drawBar(inner.removeFromTop(barH), shownR_, holdR_, clipR_);
}

// --- TempoReadout --------------------------------------------------------------------------------

void TempoReadout::set(float bpm, bool playing, const juce::String& source, const juce::String& position) {
    const bool bpmChanged = std::abs(bpm - bpm_) >= 0.05f;
    if (!bpmChanged && playing == playing_ && source == source_ && position == position_) return;
    bpm_ = bpm;
    playing_ = playing;
    source_ = source;
    position_ = position;
    repaint();
}

void TempoReadout::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    mw808LookAndFeel::drawInset(g, r.toFloat());
    auto inner = r.reduced(6, 2);
    auto right = inner.removeFromRight(46);
    g.setColour(playing_ ? C::cream : C::creamDim);
    g.setFont(mw808LookAndFeel::readout(19.0f));
    g.drawText(juce::String(bpm_, 1), inner, juce::Justification::centredLeft, false);
    g.setFont(mw808LookAndFeel::legend(9.0f));
    g.setColour(C::creamDim);
    g.drawText("BPM  " + source_, right.removeFromTop(right.getHeight() / 2), juce::Justification::centredRight, false);
    g.setColour(playing_ ? C::orange : C::creamDim);
    g.drawText(position_, right, juce::Justification::centredRight, false);
}

void TempoReadout::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1) return;
    if (onClick) onClick(*this);
}

void TempoReadout::mouseDoubleClick(const juce::MouseEvent&) {
    if (onDoubleClick) onDoubleClick();
}

// --- HeaderBar ----------------------------------------------------------------------------------------

HeaderBar::HeaderBar(Context& ctx) : ctx_(ctx) {
    setOpaque(true);
    for (int i = 0; i < plugin::kNumPresetKinds; ++i) {
        auto* b = kindButtons_.add(new juce::TextButton(kKindNames[i]));
        styleButton(*b, C::yellow, false, 9.0f);
        b->onClick = [this, i] { setKind(static_cast<PresetKind>(i)); };
        addAndMakeVisible(b);
    }
    styleButton(prevButton_, C::orange);
    styleButton(nextButton_, C::orange);
    styleButton(nameButton_, C::orange, false, 11.0f);
    styleButton(saveButton_, C::orange, false, 9.0f);
    styleButton(presetMenuButton_, C::orange, false, 11.0f);
    prevButton_.onClick = [this] { ctx_.processor.presets().loadPrevious(kind_); };
    nextButton_.onClick = [this] { ctx_.processor.presets().loadNext(kind_); };
    nameButton_.onClick = [this] { showPresetList(); };
    saveButton_.onClick = [this] { askSavePreset(); };
    presetMenuButton_.onClick = [this] { showPresetMenu(); };
    for (auto* b : {&prevButton_, &nextButton_, &nameButton_, &saveButton_, &presetMenuButton_}) addAndMakeVisible(b);
    prevButton_.setTooltip("Previous preset");
    nextButton_.setTooltip("Next preset");
    saveButton_.setTooltip("Save as user preset");

    for (int i = 0; i < kNumViews; ++i) {
        auto* b = viewButtons_.add(new juce::TextButton(kViewNames[i]));
        styleButton(*b, C::cream, false, 10.0f);
        b->onClick = [this, i] { ctx_.state.setView(static_cast<View>(i)); };
        addAndMakeVisible(b);
    }

    tempo_.onClick = [this](juce::Component& target) {
        if (auto* p = ctx_.param(paramid::tempo))
            showParameterMenu(ctx_, *p, target, [p] {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->getDefaultValue());
                p->endChangeGesture();
            });
    };
    tempo_.setTooltip("Effective tempo. The TEMPO knob sets the internal clock.");
    addAndMakeVisible(tempo_);
    addAndMakeVisible(meter_);

    settingsButton_.setButtonText(juce::String::charToString(0x2699));  // gear
    styleButton(settingsButton_, C::cream, false, 15.0f);
    settingsButton_.setTooltip("Settings");
    settingsButton_.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible(settingsButton_);

    ctx_.processor.presets().addListener(this);
    ctx_.processor.patterns().addListener(this);
    ctx_.state.addListener(this);
    setKind(PresetKind::Full);
    uiStateChanged();
}

HeaderBar::~HeaderBar() {
    ctx_.state.removeListener(this);
    ctx_.processor.patterns().removeListener(this);
    ctx_.processor.presets().removeListener(this);
}

void HeaderBar::resized() {
    auto r = getLocalBounds().reduced(10, 9);
    wordmarkArea_ = r.removeFromLeft(172);
    r.removeFromLeft(10);

    // Preset browser.
    presetArea_ = r.removeFromLeft(452);
    auto p = presetArea_;
    for (auto* b : kindButtons_) {
        b->setBounds(p.removeFromLeft(b == kindButtons_[1] ? 62 : 40));
        p.removeFromLeft(2);
    }
    p.removeFromLeft(6);
    prevButton_.setBounds(p.removeFromLeft(24));
    p.removeFromLeft(2);
    presetMenuButton_.setBounds(p.removeFromRight(26));
    p.removeFromRight(2);
    saveButton_.setBounds(p.removeFromRight(44));
    p.removeFromRight(2);
    nextButton_.setBounds(p.removeFromRight(24));
    p.removeFromRight(2);
    nameButton_.setBounds(p);
    r.removeFromLeft(16);

    // View switch.
    viewArea_ = r.removeFromLeft(210);
    auto v = viewArea_;
    for (auto* b : viewButtons_) {
        b->setBounds(v.removeFromLeft(68));
        v.removeFromLeft(3);
    }
    r.removeFromLeft(16);

    settingsButton_.setBounds(r.removeFromRight(30));
    r.removeFromRight(10);
    meter_.setBounds(r.removeFromRight(170));
    r.removeFromRight(10);
    tempo_.setBounds(r);
}

void HeaderBar::paintStatic(juce::Graphics& g) {
    g.fillAll(C::panelDeep);
    g.setColour(C::hairline);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    // Wordmark: lowercase "mw808" with the "mw" in cream and "808" in orange, "mwAudio" mark beneath.
    auto w = wordmarkArea_;
    g.setFont(mw808LookAndFeel::bold(23.0f).withExtraKerningFactor(-0.02f));
    const juce::String mw = "mw", num = "808";
    const int mwW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), mw);
    auto line = w.removeFromTop(24);
    g.setColour(C::cream);
    g.drawText(mw, line, juce::Justification::centredLeft, false);
    g.setColour(C::orange);
    g.drawText(num, line.withTrimmedLeft(mwW), juce::Justification::centredLeft, false);
    g.setColour(C::creamDim);
    g.setFont(mw808LookAndFeel::legend(8.0f).withExtraKerningFactor(0.12f));
    g.drawText("MWAUDIO  .  ANALOG DRUM MACHINE", w.translated(0, -4), juce::Justification::topLeft, false);
    // Hairline separators between the header groups.
    g.setColour(C::hairline);
    for (int x : {presetArea_.getX() - 7, viewArea_.getX() - 9, viewArea_.getRight() + 8})
        g.drawVerticalLine(x, 12.0f, static_cast<float>(getHeight() - 12));
}

void HeaderBar::poll(const PlayState& ps) {
    meter_.setPeaks(ps.peakLeft.load(std::memory_order_relaxed), ps.peakRight.load(std::memory_order_relaxed));
    const bool playing = ps.playing.load(std::memory_order_relaxed);
    const int mode = ctx_.choice(paramid::seqMode);
    juce::String source = mode == 0 ? "OFF" : mode == 1 ? "HOST" : mode == 2 ? "INT" : "NOTE";
    juce::String position;
    if (playing) {
        const int step = ps.step.load(std::memory_order_relaxed);
        position = patternLabel(ps.currentPattern.load(std::memory_order_relaxed)) +
                   (ps.variation.load(std::memory_order_relaxed) == 0 ? " A" : " B");
        if (step >= 0) position += " " + juce::String(step + 1).paddedLeft('0', 2);
    } else {
        position = patternLabel(ctx_.processor.patterns().selectedPattern());
    }
    tempo_.set(ps.tempo.load(std::memory_order_relaxed), playing, source, position);
}

void HeaderBar::uiStateChanged() {
    const int view = static_cast<int>(ctx_.state.view());
    for (int i = 0; i < viewButtons_.size(); ++i) viewButtons_[i]->setToggleState(i == view, juce::dontSendNotification);
}

void HeaderBar::setKind(PresetKind kind) {
    kind_ = kind;
    for (int i = 0; i < kindButtons_.size(); ++i)
        kindButtons_[i]->setToggleState(i == static_cast<int>(kind), juce::dontSendNotification);
    refreshPresetName();
}

void HeaderBar::refreshPresetName() {
    const juce::String name = ctx_.processor.presets().currentName(kind_);
    nameButton_.setButtonText(name.isEmpty() ? juce::String("- no preset -") : name);
}

void HeaderBar::presetListChanged(PresetKind) { refreshPresetName(); }
void HeaderBar::presetLoaded(PresetKind) { refreshPresetName(); }

void HeaderBar::showPresetList() {
    auto& presets = ctx_.processor.presets();
    const int n = presets.numPresets(kind_);
    const int current = presets.currentIndex(kind_);
    juce::PopupMenu menu;
    menu.setLookAndFeel(&ctx_.lnf);
    juce::String lastCategory;
    for (int i = 0; i < n; ++i) {
        const auto entry = presets.preset(kind_, i);
        if (entry.category != lastCategory) {
            lastCategory = entry.category;
            menu.addSectionHeader(lastCategory.isEmpty() ? juce::String("Presets") : lastCategory);
        }
        menu.addItem(i + 1, entry.name, true, i == current);
    }
    if (n == 0) menu.addItem(-1, "No presets", false);
    juce::Component::SafePointer<HeaderBar> safe(this);
    const PresetKind kind = kind_;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&nameButton_).withParentComponent(&ctx_.menuParent)
                           .withMinimumWidth(nameButton_.getWidth()),
                       [safe, kind](int result) {
                           if (safe == nullptr || result <= 0) return;
                           safe->ctx_.processor.presets().load(kind, result - 1);
                       });
}

void HeaderBar::showPresetMenu() {
    auto& presets = ctx_.processor.presets();
    const int current = presets.currentIndex(kind_);
    const bool isUser = current >= 0 && !presets.preset(kind_, current).factory;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&ctx_.lnf);
    menu.addItem(1, "Save as user preset...");
    menu.addItem(2, "Delete user preset", isUser);
    menu.addSeparator();
    menu.addItem(3, "Rescan user presets");
    menu.addItem(4, "Show preset folder");
    menu.addSeparator();
    menu.addItem(5, "Import MIDI file into selected pattern...");
    menu.addItem(6, "Export selected pattern as MIDI file...");
    juce::Component::SafePointer<HeaderBar> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetMenuButton_).withParentComponent(&ctx_.menuParent),
                       [safe](int result) {
                           if (safe == nullptr) return;
                           auto& ctx = safe->ctx_;
                           auto& pm = ctx.processor.presets();
                           switch (result) {
                               case 1: safe->askSavePreset(); break;
                               case 2: {
                                   const int idx = pm.currentIndex(safe->kind_);
                                   if (idx >= 0) pm.deleteUser(safe->kind_, idx);
                                   break;
                               }
                               case 3: pm.refresh(); break;
                               case 4: pm.userDirectory(safe->kind_).revealToUser(); break;
                               case 5: {
                                   auto chooser = std::make_shared<juce::FileChooser>("Import MIDI file", juce::File(), "*.mid;*.midi");
                                   chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                        [safe, chooser](const juce::FileChooser& fc) {
                                                            if (safe == nullptr) return;
                                                            const auto file = fc.getResult();
                                                            if (!file.existsAsFile()) return;
                                                            auto& p = safe->ctx_.processor;
                                                            p.importMidiFile(file, p.patterns().selectedPattern(), safe->ctx_.editVariation());
                                                        });
                                   break;
                               }
                               case 6: {
                                   auto& p = ctx.processor;
                                   const juce::File temp = p.exportPatternToMidiFile(p.patterns().selectedPattern(), ctx.editVariation());
                                   if (!temp.existsAsFile()) return;
                                   auto chooser = std::make_shared<juce::FileChooser>("Export pattern as MIDI file",
                                                                                      juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(temp.getFileName()),
                                                                                      "*.mid");
                                   chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                                            juce::FileBrowserComponent::warnAboutOverwriting,
                                                        [temp, chooser](const juce::FileChooser& fc) {
                                                            auto target = fc.getResult();
                                                            if (target == juce::File()) return;
                                                            if (!target.hasFileExtension("mid")) target = target.withFileExtension("mid");
                                                            temp.copyFileTo(target);
                                                        });
                                   break;
                               }
                               default: break;
                           }
                       });
}

void HeaderBar::askSavePreset() {
    auto* window = new juce::AlertWindow("Save " + juce::String(kKindNames[static_cast<int>(kind_)]).toLowerCase() + " preset",
                                         "Name:", juce::MessageBoxIconType::NoIcon);
    window->setLookAndFeel(&ctx_.lnf);
    window->addTextEditor("name", ctx_.processor.presets().currentName(kind_), {});
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<HeaderBar> safe(this);
    const PresetKind kind = kind_;
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, kind, window](int result) {
                                window->setLookAndFeel(nullptr);
                                if (safe == nullptr || result != 1) return;
                                const juce::String name = window->getTextEditorContents("name").trim();
                                if (name.isNotEmpty()) safe->ctx_.processor.presets().saveUser(kind, name);
                            }),
                            true);
}

void HeaderBar::setChoiceParam(const char* id, int index) {
    if (auto* p = ctx_.param(id)) {
        const auto& range = p->getNormalisableRange();
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(range.start + static_cast<float>(index)));
        p->endChangeGesture();
    }
}

void HeaderBar::setBoolParam(const char* id, bool on) {
    if (auto* p = ctx_.param(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(on ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void HeaderBar::showSettingsMenu() {
    juce::PopupMenu menu;
    menu.setLookAndFeel(&ctx_.lnf);

    {
        juce::PopupMenu sub;
        const int q = ctx_.choice(paramid::quality);
        const char* names[] = {"Eco (no oversampling)", "Standard", "High"};
        for (int i = 0; i < 3; ++i) sub.addItem(kQualityBase + i, names[i], true, q == i);
        menu.addSubMenu("Quality", sub);
    }
    {
        juce::PopupMenu sub;
        const float v = ctx_.raw(paramid::variance);
        for (int i = 0; i <= 4; ++i)
            sub.addItem(kVarianceBase + i, i == 0 ? juce::String("Off (nominal parts)") : juce::String(i * 25) + " %", true,
                        std::abs(v - static_cast<float>(i) * 0.25f) < 0.01f);
        menu.addSubMenu("Analog variance", sub);
    }
    {
        juce::PopupMenu sub;
        sub.addItem(-1, "Serial #" + juce::String(static_cast<int>(ctx_.processor.unitSeed())), false);
        sub.addItem(kSerialRandom, "Randomise");
        sub.addItem(kSerialEnter, "Enter...");
        menu.addSubMenu("Serial number (component spread)", sub);
    }
    {
        juce::PopupMenu sub;
        const float cur = ctx_.state.scale();
        for (int i = 0; i < kNumScales; ++i)
            sub.addItem(kScaleBase + i, juce::String(juce::roundToInt(kScales[i] * 100.0f)) + " %", true,
                        std::abs(cur - kScales[i]) < 0.03f);
        menu.addSubMenu("UI scale", sub);
    }
    menu.addSeparator();
    {
        juce::PopupMenu sub;
        sub.addItem(kVelocitySens, "Velocity sensitive", true, ctx_.raw(paramid::velocitySens) >= 0.5f);
        sub.addItem(kMidiOut, "MIDI out (sequencer notes on channel 10)", true, ctx_.raw(paramid::midiOut) >= 0.5f);
        juce::PopupMenu bass;
        const int bc = ctx_.choice(paramid::bassChannel);
        bass.addItem(kBassChannelBase, "Off", true, bc == 0);
        for (int ch = 1; ch <= 16; ++ch) bass.addItem(kBassChannelBase + ch, "Channel " + juce::String(ch), true, bc == ch);
        sub.addSubMenu("Chromatic bass drum channel", bass);
        sub.addSeparator();
        const int bound = static_cast<int>(ctx_.processor.midiLearn().getMap().size());
        sub.addItem(kClearMidiLearn, "Clear MIDI learn map (" + juce::String(bound) + " bound)", bound > 0);
        menu.addSubMenu("MIDI", sub);
    }
    menu.addItem(kPanic, "Panic (silence all voices)");
    menu.addSeparator();
    menu.addItem(kAbout, "About mw808...");

    juce::Component::SafePointer<HeaderBar> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settingsButton_).withParentComponent(&ctx_.menuParent),
                       [safe](int result) {
                           if (safe == nullptr || result <= 0) return;
                           auto& self = *safe;
                           auto& ctx = self.ctx_;
                           if (result >= kQualityBase && result < kQualityBase + 3) self.setChoiceParam(paramid::quality, result - kQualityBase);
                           else if (result >= kVarianceBase && result <= kVarianceBase + 4) {
                               if (auto* p = ctx.param(paramid::variance)) {
                                   p->beginChangeGesture();
                                   p->setValueNotifyingHost(static_cast<float>(result - kVarianceBase) * 0.25f);
                                   p->endChangeGesture();
                               }
                           } else if (result == kSerialRandom) {
                               ctx.processor.setUnitSeed(static_cast<std::uint32_t>(juce::Random::getSystemRandom().nextInt(1000000)));
                           } else if (result == kSerialEnter) self.askSerialNumber();
                           else if (result >= kScaleBase && result < kScaleBase + kNumScales) {
                               if (self.onScaleRequest) self.onScaleRequest(kScales[result - kScaleBase]);
                           } else if (result == kVelocitySens) self.setBoolParam(paramid::velocitySens, ctx.raw(paramid::velocitySens) < 0.5f);
                           else if (result == kMidiOut) self.setBoolParam(paramid::midiOut, ctx.raw(paramid::midiOut) < 0.5f);
                           else if (result >= kBassChannelBase && result <= kBassChannelBase + 16) self.setChoiceParam(paramid::bassChannel, result - kBassChannelBase);
                           else if (result == kClearMidiLearn) ctx.processor.midiLearn().clear();
                           else if (result == kPanic) { if (self.onPanic) self.onPanic(); else ctx.processor.panic(); }
                           else if (result == kAbout) self.showAbout();
                       });
}

void HeaderBar::askSerialNumber() {
    auto* window = new juce::AlertWindow("Serial number",
                                         "The serial number seeds the component tolerances behind Analog Variance.\n"
                                         "Two instances with the same number sound identical.",
                                         juce::MessageBoxIconType::NoIcon);
    window->setLookAndFeel(&ctx_.lnf);
    window->addTextEditor("serial", juce::String(static_cast<int>(ctx_.processor.unitSeed())), {});
    window->getTextEditor("serial")->setInputRestrictions(9, "0123456789");
    window->addButton("Set", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<HeaderBar> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
                                window->setLookAndFeel(nullptr);
                                if (safe == nullptr || result != 1) return;
                                const juce::String text = window->getTextEditorContents("serial").trim();
                                if (text.isNotEmpty())
                                    safe->ctx_.processor.setUnitSeed(static_cast<std::uint32_t>(text.getLargeIntValue() & 0xffffffff));
                            }),
                            true);
}

void HeaderBar::showAbout() {
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::NoIcon)
            .withTitle("mw808 " MW808_VERSION_STRING)
            .withMessage("Classic analog drum machine emulation with a built-in step sequencer.\n\n"
                         "mwAudio. Licensed under the GNU AGPL v3 or later.\n"
                         "Built with JUCE.")
            .withButton("Close")
            .withAssociatedComponent(&ctx_.menuParent),
        nullptr);
}

}  // namespace mw808::ui

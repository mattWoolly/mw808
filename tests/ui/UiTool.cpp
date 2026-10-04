// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// mw808_ui_tool: drives the real mw808Editor headlessly (see CMakeLists.txt).
// Exit code 0 on success, 1 on a failed check, 2 on bad usage.

#include <sys/resource.h>

#if JUCE_LINUX || defined(__linux__)
 #include <X11/Xlib.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <typeinfo>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "GridView.h"
#include "SequencerStrip.h"
#include "UiCommon.h"
#include "VoicePanel.h"
#include "mw808/MidiMap.h"
#include "mw808/seq/PatternOps.h"
#include "mw808Editor.h"
#include "mw808Processor.h"

namespace {

using mw808::ui::View;
namespace paramid = mw808::plugin::paramid;

int failures = 0;

// Under Xvfb (no window manager) a plain titled window is what pluginval and
// the Standalone use; "temporary" windows need WM atoms that may not exist.
constexpr int kWindowFlags = juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowIsResizable;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

double cpuSeconds() {
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return static_cast<double>(u.ru_utime.tv_sec + u.ru_stime.tv_sec) +
           static_cast<double>(u.ru_utime.tv_usec + u.ru_stime.tv_usec) * 1.0e-6;
}

// --- a host stand-in running processBlock on its own thread ----------------------------------

class AudioThread {
public:
    explicit AudioThread(mw808Processor& p, bool play) : processor_(p), play_(play) {
        buffer_.setSize(juce::jmax(2, processor_.getTotalNumOutputChannels()), kBlock);
        thread_ = std::thread([this] { run(); });
    }
    ~AudioThread() {
        stop_ = true;
        thread_.join();
    }
    long blocks() const noexcept { return blocks_.load(); }

private:
    static constexpr int kBlock = 256;
    struct PlayHead final : juce::AudioPlayHead {
        std::atomic<double> ppq{0.0};
        std::atomic<bool> playing{false};
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo info;
            info.setIsPlaying(playing.load());
            info.setPpqPosition(ppq.load());
            info.setBpm(124.0);
            return info;
        }
    };
    void run() {
        processor_.setPlayHead(&playHead_);
        playHead_.playing = play_;
        juce::Random rng(808);
        while (!stop_) {
            midi_.clear();
            if (play_ && rng.nextInt(8) == 0) {
                const int v = rng.nextInt(mw808::kNumVoices);
                midi_.addEvent(juce::MidiMessage::noteOn(10, mw808::kVoiceNote[static_cast<std::size_t>(v)], static_cast<juce::uint8>(60 + rng.nextInt(67))), 0);
            }
            processor_.processBlock(buffer_, midi_);
            ++blocks_;
            playHead_.ppq = playHead_.ppq.load() + 124.0 / 60.0 * kBlock / 48000.0;
            std::this_thread::sleep_for(std::chrono::microseconds(5300));  // ~ real time at 48 kHz
        }
        processor_.setPlayHead(nullptr);
    }
    mw808Processor& processor_;
    bool play_;
    PlayHead playHead_;
    juce::AudioBuffer<float> buffer_;
    juce::MidiBuffer midi_;
    std::atomic<bool> stop_{false};
    std::atomic<long> blocks_{0};
    std::thread thread_;
};

std::unique_ptr<mw808Processor> makeProcessor() {
    auto p = std::make_unique<mw808Processor>();
    p->prepareToPlay(48000.0, 256);
    return p;
}

void setParam(mw808Processor& p, const char* id, float value) {
    if (auto* param = p.apvts().getParameter(id)) param->setValueNotifyingHost(param->convertTo0to1(value));
}

// Give the screenshots something to show: a few modifiers on the first pattern.
void decoratePattern(mw808Processor& p) {
    auto& model = p.patterns();
    model.edit(model.selectedPattern(), [](mw808::seq::Pattern& pat) {
        auto& sd = pat.at(0, mw808::SD, 4);
        sd.setLevel(mw808::seq::StepLevel::Accent);
        sd.setFlam(true);
        auto& ch = pat.at(0, mw808::CH, 6);
        ch.setLevel(mw808::seq::StepLevel::Normal);
        ch.setSubSteps(3);
        auto& cp = pat.at(0, mw808::CP, 12);
        cp.setLevel(mw808::seq::StepLevel::Weak);
        cp.setProbabilityCode(2);
        pat.at(0, mw808::seq::kAccentTrack, 0).setLevel(mw808::seq::StepLevel::Accent);
        pat.at(0, mw808::seq::kAccentTrack, 8).setLevel(mw808::seq::StepLevel::Accent);
    });
}

// Run the internal clock on this thread for a few blocks so the play state shows a position.
void advanceTransport(mw808Processor& p, int blocks) {
    juce::AudioBuffer<float> buffer(juce::jmax(2, p.getTotalNumOutputChannels()), 256);
    juce::MidiBuffer midi;
    for (int i = 0; i < blocks; ++i) p.processBlock(buffer, midi);
}

bool savePng(const juce::Image& image, const juce::File& file) {
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (!out.openedOk()) return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream(image, out);
}

// --- screenshots -----------------------------------------------------------------------------

int screenshots(const juce::File& dir) {
    dir.createDirectory();
    auto processor = makeProcessor();
    setParam(*processor, paramid::seqMode, 2.0f);  // Internal
    setParam(*processor, paramid::seqRun, 1.0f);
    decoratePattern(*processor);
    processor->serviceMessageThread();
    advanceTransport(*processor, 40);  // ~ 5 steps at 120 BPM
    processor->serviceMessageThread();

    std::unique_ptr<juce::AudioProcessorEditor> base(processor->createEditor());
    auto* editor = dynamic_cast<mw808Editor*>(base.get());
    check(editor != nullptr, "createEditor() returns an mw808Editor");
    if (editor == nullptr) return 1;
    editor->addToDesktop(kWindowFlags);
    editor->setVisible(true);

    const float scales[] = {0.6f, 1.0f, 2.0f};
    const char* names[] = {"panel", "grid", "mixer"};
    for (float s : scales) {
        editor->setScale(s);
        pump(60);
        for (int v = 0; v < 3; ++v) {
            editor->setView(static_cast<View>(v));
            editor->pollNow();
            pump(60);
            const juce::Image image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            const juce::File file = dir.getChildFile(juce::String(names[v]) + "-" + juce::String(juce::roundToInt(s * 100)) + ".png");
            const bool ok = savePng(image, file);
            std::printf("  %s %s (%dx%d)\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
            if (!ok) ++failures;
            check(image.getWidth() == editor->getWidth() && image.getHeight() == editor->getHeight(), "snapshot matches editor size");
        }
    }
    // Extras at 1.0x: chain editor open, popup menu.
    editor->setScale(1.0f);
    editor->setView(View::Panel);
    editor->showChainEditor(true);
    pump(60);
    savePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f), dir.getChildFile("panel-100-chain.png"));
    editor->showChainEditor(false);
    pump(30);
    editor->removeFromDesktop();
    return failures == 0 ? 0 : 1;
}

// --- stress --------------------------------------------------------------------------------

int stress(int iterations) {
    auto processor = makeProcessor();
    setParam(*processor, paramid::seqMode, 1.0f);  // Host Sync, the host thread plays
    AudioThread audio(*processor, true);
    juce::Random rng(1234);
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < iterations; ++i) {
        std::unique_ptr<juce::AudioProcessorEditor> base(processor->createEditor());
        auto* editor = dynamic_cast<mw808Editor*>(base.get());
        if (editor == nullptr) { check(false, "editor type"); break; }
        editor->addToDesktop(kWindowFlags);
        editor->setVisible(true);
        editor->setView(static_cast<View>(i % 3));
        pump(20);
        editor->setScale(0.6f + 1.4f * rng.nextFloat());
        pump(20);
        if (i % 7 == 0) processor->setCurrentProgram(rng.nextInt(juce::jmax(1, processor->getNumPrograms())));
        if (i % 11 == 0) {
            juce::MemoryBlock state;
            processor->getStateInformation(state);
            processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        }
        if (i % 5 == 0) editor->showChainEditor(true);
        processor->patterns().edit(processor->patterns().selectedPattern(), [&](mw808::seq::Pattern& p) {
            p.at(0, rng.nextInt(17), rng.nextInt(16)).setLevel(static_cast<mw808::seq::StepLevel>(rng.nextInt(4)));
        });
        processor->audition(static_cast<mw808::VoiceId>(rng.nextInt(16)), rng.nextBool());
        pump(20);
        editor->removeFromDesktop();
        base.reset();
        // The editor's gone; its timers and listeners must be gone too.
        processor->setStateInformation(nullptr, 0);
        pump(5);
    }
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    std::printf("  %d open/close cycles in %.1f s, audio thread processed %ld blocks\n", iterations, elapsed, audio.blocks());
    check(audio.blocks() > 0, "audio thread kept running");
    return failures == 0 ? 0 : 1;
}

// --- idle CPU ------------------------------------------------------------------------------

int idle(int seconds) {
    auto processor = makeProcessor();
    {
        // Baseline: the message loop and the processor's own 30 Hz service timer, no editor.
        pump(200);
        const double c0 = cpuSeconds();
        pump(seconds * 1000);
        const double used = cpuSeconds() - c0;
        std::printf("  baseline (no editor): %.1f ms CPU over %d s = %.2f%% of one core\n", used * 1000.0, seconds, used / seconds * 100.0);
    }
    std::unique_ptr<juce::AudioProcessorEditor> base(processor->createEditor());
    auto* editor = dynamic_cast<mw808Editor*>(base.get());
    if (editor == nullptr) return 1;
    editor->addToDesktop(kWindowFlags);
    editor->setVisible(true);
    editor->setScale(1.0f);
    pump(300);  // first paint, caches

    {
        const double c0 = cpuSeconds();
        pump(seconds * 1000);
        const double used = cpuSeconds() - c0;
        std::printf("  idle (no audio): %.1f ms CPU over %d s = %.2f%% of one core\n", used * 1000.0, seconds, used / seconds * 100.0);
        check(used / seconds < 0.05, "idle editor below 5% of a core");
    }
    {
        setParam(*processor, paramid::seqMode, 1.0f);
        AudioThread audio(*processor, true);
        pump(300);
        const double c0 = cpuSeconds();
        pump(seconds * 1000);
        const double used = cpuSeconds() - c0;
        std::printf("  playing (audio thread + UI): %.1f ms CPU over %d s = %.2f%% of one core (incl. DSP)\n", used * 1000.0, seconds,
                    used / seconds * 100.0);
    }
    editor->removeFromDesktop();
    return failures == 0 ? 0 : 1;
}

// --- interaction ------------------------------------------------------------------------------

template <class T>
void collect(juce::Component& root, juce::Array<T*>& out) {
    if (auto* t = dynamic_cast<T*>(&root)) out.add(t);
    for (auto* child : root.getChildren()) collect<T>(*child, out);
}

struct Mouse {
    juce::ComponentPeer* peer;
    juce::ComponentPeer::DragInfo unused{};
    juce::Point<float> at(juce::Component& c, float fx = 0.5f, float fy = 0.5f) const {
        const auto screen = c.localPointToGlobal(juce::Point<float>(static_cast<float>(c.getWidth()) * fx, static_cast<float>(c.getHeight()) * fy));
        return peer->globalToLocal(screen);
    }
    void move(juce::Point<float> p, juce::ModifierKeys mods = {}) const {
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, p, mods, 0.0f, 0.0f, juce::Time::currentTimeMillis());
        pump(5);
    }
    // The key modifiers of a synthetic event come from ModifierKeys::currentModifiers
    // (the X11 backend sets it from key events), so set it explicitly here.
    void press(juce::Point<float> p, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) const {
        juce::ModifierKeys::currentModifiers = mods.withoutMouseButtons();
        move(p, mods.withoutMouseButtons());
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, p, mods, 0.0f, 0.0f, juce::Time::currentTimeMillis());
        pump(5);
    }
    void release(juce::Point<float> p, juce::ModifierKeys mods = {}) const {
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, p, mods.withoutMouseButtons(), 0.0f, 0.0f, juce::Time::currentTimeMillis());
        juce::ModifierKeys::currentModifiers = {};
        pump(5);
    }
    void click(juce::Component& c, juce::ModifierKeys extra = {}) const {
        const auto p = at(c);
        press(p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier).withFlags(extra.getRawFlags()));
        release(p, extra);
    }
    void doubleClick(juce::Component& c) const {
        click(c);
        click(c);
    }
    void dragVertical(juce::Component& c, int dy, juce::ModifierKeys extra = {}) const {
        auto p = at(c);
        const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier).withFlags(extra.getRawFlags());
        press(p, mods);
        for (int i = 1; i <= 4; ++i) {
            move(p.translated(0.0f, static_cast<float>(dy) * static_cast<float>(i) / 4.0f), mods);
        }
        release(p.translated(0.0f, static_cast<float>(dy)), extra);
    }
    void wheel(juce::Component& c, float deltaY) const {
        const auto p = at(c);
        move(p);
        juce::MouseWheelDetails d;
        d.deltaX = 0.0f;
        d.deltaY = deltaY;
        d.isReversed = false;
        d.isSmooth = false;
        d.isInertial = false;
        peer->handleMouseWheel(juce::MouseInputSource::InputSourceType::mouse, p, juce::Time::currentTimeMillis(), d);
        pump(5);
    }
};

int interact() {
    auto processor = makeProcessor();
    std::unique_ptr<juce::AudioProcessorEditor> base(processor->createEditor());
    auto* editor = dynamic_cast<mw808Editor*>(base.get());
    if (editor == nullptr) return 1;
    editor->setScale(1.0f);
    editor->addToDesktop(kWindowFlags);
    editor->setVisible(true);
    editor->setView(View::Panel);
    pump(200);
    auto* peer = editor->getPeer();
    check(peer != nullptr, "editor has a desktop peer under Xvfb");
    if (peer == nullptr) return 1;
    Mouse mouse{peer};

    auto raw = [&](const juce::String& id) { return processor->apvts().getRawParameterValue(id)->load(); };

    // Knobs: drag, shift-drag fine, wheel, double-click reset, text entry.
    juce::Array<mw808::ui::ParamControl*> controls;
    collect(*editor, controls);
    mw808::ui::ParamControl* bdLevel = nullptr;
    for (auto* c : controls)
        if (c->parameter().getParameterID() == "bd_level" && c->isShowing()) bdLevel = c;
    check(bdLevel != nullptr, "BD LEVEL knob found in PANEL view");
    if (bdLevel != nullptr) {
        const float before = raw("bd_level");
        mouse.dragVertical(*bdLevel, -40);
        const float after = raw("bd_level");
        check(after > before, "drag up raises BD level");
        mouse.dragVertical(*bdLevel, 10, juce::ModifierKeys::shiftModifier);
        const float fine = raw("bd_level");
        check(fine < after && after - fine < 0.02f, "shift-drag is fine");
        mouse.wheel(*bdLevel, 1.0f);
        check(raw("bd_level") > fine, "wheel up raises the value");
        mouse.doubleClick(*bdLevel);
        check(std::abs(raw("bd_level") - 0.8f) < 1.0e-4f, "double-click resets to default (0.8)");
    }

    // Right-click opens the MIDI learn menu, which then goes away.
    if (bdLevel != nullptr) {
        const auto p = mouse.at(*bdLevel);
        mouse.press(p, juce::ModifierKeys::rightButtonModifier);
        mouse.release(p);
        pump(50);
        check(juce::PopupMenu::dismissAllActiveMenus(), "right-click opened a popup menu");
        pump(30);
    }

    // Step keys: click cycles, shift-click weak, drag paints.
    juce::Array<mw808::ui::StepKey*> keys;
    collect(*editor, keys);
    check(keys.size() == 16, "16 step keys");
    auto& model = processor->patterns();
    const int pat = model.selectedPattern();
    model.edit(pat, [](mw808::seq::Pattern& p) { mw808::seq::clearPattern(p); });
    processor->setUiState([&] { auto s = processor->uiState(); s.selectedTrack = 0; s.stepEditMode = 0; s.page = 0; return s; }());
    if (keys.size() == 16) {
        // Find the key for index 0 (keys are in order of creation).
        mw808::ui::StepKey* k0 = nullptr;
        for (auto* k : keys) if (k->index() == 0) k0 = k;
        mouse.click(*k0);
        check(model.pattern(pat).at(0, 0, 0).level() == mw808::seq::StepLevel::Normal, "click: off -> normal");
        mouse.click(*k0);
        check(model.pattern(pat).at(0, 0, 0).level() == mw808::seq::StepLevel::Accent, "click: normal -> accent");
        mouse.click(*k0);
        check(model.pattern(pat).at(0, 0, 0).level() == mw808::seq::StepLevel::Off, "click: accent -> off");
        mouse.click(*k0, juce::ModifierKeys::shiftModifier);
        check(model.pattern(pat).at(0, 0, 0).level() == mw808::seq::StepLevel::Weak, "shift-click: weak");
        // Drag-paint from key 4 to key 7.
        mw808::ui::StepKey* k4 = nullptr; mw808::ui::StepKey* k7 = nullptr;
        for (auto* k : keys) { if (k->index() == 4) k4 = k; if (k->index() == 7) k7 = k; }
        const auto a = mouse.at(*k4), b = mouse.at(*k7);
        mouse.press(a);
        for (int i = 1; i <= 6; ++i) mouse.move(a + (b - a) * (static_cast<float>(i) / 6.0f), juce::ModifierKeys::leftButtonModifier);
        mouse.release(b);
        bool painted = true;
        for (int s = 4; s <= 7; ++s) painted = painted && model.pattern(pat).at(0, 0, s).level() == mw808::seq::StepLevel::Normal;
        check(painted, "drag paints steps 5..8 normal");
    }

    // GRID view click.
    editor->setView(View::Grid);
    pump(50);
    juce::Array<mw808::ui::GridView*> grids;
    collect(*editor, grids);
    check(grids.size() == 1, "grid view present");
    if (grids.size() == 1) {
        auto* grid = grids[0];
        // Cell (track 1 = SD, step 2): the grid is 64 px header + 4 gap, 16 columns, 17 lanes under a 16 px ruler.
        const auto area = grid->getLocalBounds().reduced(6);
        const int gridX = area.getX() + 64 + 4;
        const float colW = static_cast<float>(area.getWidth() - 68) / 16.0f;
        const float laneH = static_cast<float>(area.getHeight() - 16) / 17.0f;
        const juce::Point<float> local(static_cast<float>(gridX) + colW * 2.5f, static_cast<float>(area.getY() + 16) + laneH * 1.5f);
        const auto p = peer->globalToLocal(grid->localPointToGlobal(local));
        mouse.press(p);
        mouse.release(p);
        check(model.pattern(pat).at(0, 1, 2).level() == mw808::seq::StepLevel::Normal, "grid click sets SD step 3");
    }

    // MIXER: fader drag.
    editor->setView(View::Mixer);
    pump(50);
    controls.clear();
    collect(*editor, controls);
    mw808::ui::ParamControl* sdFader = nullptr;
    for (auto* c : controls)
        if (c->parameter().getParameterID() == "sd_level" && c->isShowing()) sdFader = c;
    check(sdFader != nullptr, "SD level fader found in MIXER view");
    if (sdFader != nullptr) {
        const float before = raw("sd_level");
        mouse.dragVertical(*sdFader, 30);
        check(raw("sd_level") < before, "fader drag down lowers SD level");
    }

    // Buttons: view switch via header, pattern select, run.
    editor->setView(View::Panel);
    pump(30);
    juce::Array<juce::TextButton*> buttons;
    collect(*editor, buttons);
    juce::TextButton* grid = nullptr;
    juce::TextButton* run = nullptr;
    for (auto* b : buttons) {
        if (b->getButtonText() == "GRID") grid = b;
        if (b->getButtonText() == "START / STOP") run = b;
    }
    check(grid != nullptr && run != nullptr, "header and transport buttons found");
    if (grid != nullptr) {
        mouse.click(*grid);
        check(editor->view() == View::Grid, "clicking GRID switches the view");
    }
    if (run != nullptr) {
        mouse.click(*run);
        check(raw(paramid::seqRun) > 0.5f, "START/STOP sets seq_run");
        mouse.click(*run);
        check(raw(paramid::seqRun) < 0.5f, "START/STOP clears seq_run");
    }

    // Resize via setScale persists to the UI state.
    editor->setScale(1.5f);
    pump(30);
    check(std::abs(processor->uiState().scale - 1.5f) < 0.02f, "scale persisted in UI state");
    check(editor->getWidth() == 1920 && editor->getHeight() == 1080, "1.5x window is 1920x1080");

    editor->removeFromDesktop();
    return failures == 0 ? 0 : 1;
}

// --- helpers for the wiring / lifetime / perf modes ----------------------------------------

// Counts host gestures so a test can prove none is left open.
struct GestureCounter final : juce::AudioProcessorListener {
    int begins = 0, ends = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begins; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ends; }
};

template <class T = juce::TextButton>
T* findButton(juce::Component& root, const juce::String& text, juce::Component* within = nullptr) {
    juce::Array<T*> all;
    collect<T>(root, all);
    for (auto* b : all)
        if (b->getButtonText() == text && b->isShowing() && (within == nullptr || within->isParentOf(b))) return b;
    return nullptr;
}

// A button that is a direct child of `parent` (bank "A" vs variation "A").
juce::TextButton* findDirect(juce::Component& parent, const juce::String& text) {
    for (auto* c : parent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c))
            if (b->getButtonText() == text && b->isShowing()) return b;
    return nullptr;
}

template <class T>
T* findOne(juce::Component& root) {
    juce::Array<T*> all;
    collect<T>(root, all);
    for (auto* c : all)
        if (c->isShowing()) return c;
    return all.isEmpty() ? nullptr : all.getFirst();
}

mw808::ui::ParamControl* findControl(juce::Component& root, const juce::String& id) {
    juce::Array<mw808::ui::ParamControl*> all;
    collect(root, all);
    for (auto* c : all)
        if (c->parameter().getParameterID() == id && c->isShowing()) return c;
    return nullptr;
}

// The topmost modal component (an open popup menu or alert), or nullptr.
juce::Component* topModal() {
    auto* mcm = juce::ModalComponentManager::getInstanceWithoutCreating();
    return mcm != nullptr && mcm->getNumModalComponents() > 0 ? mcm->getModalComponent(0) : nullptr;
}

// Pick an item of the open popup menu by keyboard: `downs` x down, optional
// right (into a submenu) + `subDowns` x down, then return.
bool chooseMenuItem(int downs, int subDowns = -1) {
    pump(40);
    auto* menu = topModal();
    if (menu == nullptr) return false;
    for (int i = 0; i < downs; ++i) menu->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    if (subDowns >= 0) {
        menu->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
        pump(40);
        auto* sub = topModal();
        if (sub == nullptr || sub == menu) return false;
        // Opening a submenu by key highlights its first item already.
        for (int i = 0; i < subDowns; ++i) sub->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
        menu = sub;
    }
    menu->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    pump(60);
    return true;
}

struct OpenEditor {
    std::unique_ptr<juce::AudioProcessorEditor> base;
    mw808Editor* editor = nullptr;
    juce::ComponentPeer* peer = nullptr;
    explicit OpenEditor(mw808Processor& p) {
        base.reset(p.createEditor());
        editor = dynamic_cast<mw808Editor*>(base.get());
        if (editor == nullptr) return;
        editor->setScale(1.0f);
        editor->addToDesktop(kWindowFlags);
        editor->setVisible(true);
        pump(100);
        peer = editor->getPeer();
    }
    ~OpenEditor() {
        if (editor != nullptr) editor->removeFromDesktop();
    }
};

// --- wiring: every §7 control path reaches the processor -----------------------------------

int wiring() {
    using namespace mw808;
    auto processor = makeProcessor();
    GestureCounter gestures;
    processor->addListener(&gestures);
    OpenEditor open(*processor);
    if (open.editor == nullptr || open.peer == nullptr) { check(false, "editor + peer"); return 1; }
    auto& ed = *open.editor;
    Mouse mouse{open.peer};
    auto raw = [&](const juce::String& id) { return processor->apvts().getRawParameterValue(id)->load(); };
    auto& model = processor->patterns();
    auto* strip = findOne<ui::SequencerStrip>(ed);
    check(strip != nullptr, "sequencer strip present");
    if (strip == nullptr) return 1;
    auto click = [&](juce::Component* c, juce::ModifierKeys mods = {}) { if (c != nullptr) mouse.click(*c, mods); pump(10); };

    // --- pattern / bank selection (stopped: immediate) ---
    click(findDirect(*strip, "B"));
    click(findButton<ui::PatternButton>(*strip, "3"));
    check(model.selectedPattern() == 16 + 2, "bank B + pattern 3 selects B03");
    click(findDirect(*strip, "A"));
    click(findButton<ui::PatternButton>(*strip, "1"));
    check(model.selectedPattern() == 0, "bank A + pattern 1 selects A01");
    const int pat = model.selectedPattern();
    model.edit(pat, [](seq::Pattern& p) { seq::clearPattern(p); p.length = 16; for (auto& l : p.trackLength) l = 0; });

    // --- track selection + step modes ---
    juce::Array<ui::StepKey*> keys;
    collect(ed, keys);
    auto key = [&](int i) -> ui::StepKey* { for (auto* k : keys) if (k->index() == i) return k; return nullptr; };
    click(findButton(*strip, "SD"));
    check(processor->uiState().selectedTrack == SD, "track button SD selects the SD track");
    click(key(2));
    check(model.pattern(pat).at(0, SD, 2).level() == seq::StepLevel::Normal, "key click edits the selected track (SD)");
    click(findButton(*strip, "FLAM"));
    check(processor->uiState().stepEditMode == 1, "FLAM mode button");
    click(key(2));
    check(model.pattern(pat).at(0, SD, 2).flam(), "FLAM mode: click toggles flam on an active step");
    click(findButton(*strip, "SUB"));
    click(key(2));
    click(key(2));
    check(model.pattern(pat).at(0, SD, 2).subSteps() == 3, "SUB mode: two clicks give x3");
    click(key(2), juce::ModifierKeys::shiftModifier);
    check(model.pattern(pat).at(0, SD, 2).subSteps() == 2, "SUB mode: shift-click steps back");
    click(findButton(*strip, "PROB"));
    click(key(2));
    check(model.pattern(pat).at(0, SD, 2).probabilityCode() == 1, "PROB mode: click lowers the probability one notch");
    click(key(5));
    check(model.pattern(pat).at(0, SD, 5).level() == seq::StepLevel::Normal && !model.pattern(pat).at(0, SD, 5).flam(),
          "PROB mode on an empty step: switches it on, nothing else");
    click(findButton(*strip, "LEVEL"));
    click(findButton(*strip, "AC"));
    click(key(0));
    check(model.pattern(pat).at(0, seq::kAccentTrack, 0).on(), "AC track: click sets an accent step");
    click(findButton(*strip, "BD"));

    // --- right-click step menu: "Accent" (Off, Weak, Normal, Accent -> 4 downs) ---
    {
        const auto p = mouse.at(*key(9));
        mouse.press(p, juce::ModifierKeys::rightButtonModifier);
        mouse.release(p);
        check(chooseMenuItem(4), "step menu opened");
        check(model.pattern(pat).at(0, BD, 9).level() == seq::StepLevel::Accent, "step menu: Accent");
    }

    // --- pages follow LENGTH ---
    auto pageButton = [&](const juce::String& n) -> juce::TextButton* {
        juce::Array<juce::TextButton*> all; collect(*strip, all);
        for (auto* b : all) if (dynamic_cast<ui::PatternButton*>(b) == nullptr && b->getButtonText() == n && b->getProperties()["lamp"]) return b;
        return nullptr; };
    auto* page2 = pageButton("2");
    check(page2 != nullptr && !page2->isEnabled(), "page 2 disabled at length 16");
    model.edit(pat, [](seq::Pattern& p) { p.length = 32; });
    pump(10);
    check(page2 != nullptr && page2->isEnabled(), "page 2 enabled once LENGTH is 32 (no other event needed)");
    click(page2);
    check(processor->uiState().page == 1, "page 2 button selects page 2");
    click(key(0));
    check(model.pattern(pat).at(0, BD, 16).on(), "key 1 on page 2 edits step 17");
    click(pageButton("1"));
    check(processor->uiState().page == 0, "page 1 button selects page 1");

    // --- LENGTH value box: wheel ---
    if (auto* lengthBox = [&]() -> ui::ValueBox* { juce::Array<ui::ValueBox*> all; collect(*strip, all); return all.isEmpty() ? nullptr : all.getFirst(); }()) {
        const int before = model.pattern(pat).length;
        mouse.wheel(*lengthBox, 0.5f);
        check(model.pattern(pat).length > before, "LENGTH box: wheel raises the length");
    }

    // --- variation / record / chain / run ---
    {
        juce::Array<ui::ChoiceButtons*> choices; collect(*strip, choices);
        ui::ChoiceButtons* variation = nullptr;
        for (auto* c : choices) if (c->numChoices() == 3) variation = c;
        if (variation != nullptr) {
            click(findButton(*variation, "AB"));
            check(processor->apvts().getParameter(paramid::variation)->getValue() > 0.4f &&
                  processor->apvts().getParameter(paramid::variation)->getValue() < 0.6f, "variation AB");
            click(findButton(*variation, "B"));
            click(key(3));
            check(model.pattern(model.selectedPattern()).at(1, BD, 3).on(), "variation B: key edits variation B");
            click(findButton(*variation, "A"));
        } else check(false, "variation buttons");
    }
    click(findDirect(*strip, "A"));
    click(findButton<ui::PatternButton>(*strip, "1"));
    click(findButton(*strip, "REC"));
    check(raw(paramid::record) > 0.5f, "REC sets record");
    click(findButton(*strip, "REC"));
    click(findButton(*strip, "CHAIN"));
    check(raw(paramid::chain) > 0.5f, "CHAIN sets chain");

    // --- chain editor ---
    click(findButton(*strip, "EDIT"));
    pump(30);
    auto* chainEd = findOne<ui::ChainEditor>(ed);
    check(chainEd != nullptr && chainEd->isShowing(), "EDIT opens the chain editor");
    if (chainEd != nullptr) {
        model.setChain({});
        click(findButton(*chainEd, "ADD SELECTED"));
        click(findButton(*chainEd, "ADD SELECTED"));
        check(model.chain().length == 2 && model.chain().entries[0] == pat, "ADD SELECTED appends the selected pattern");
        click(findButton(*chainEd, "REMOVE LAST"));
        check(model.chain().length == 1, "REMOVE LAST");
        click(findButton(*chainEd, "CLEAR"));
        check(model.chain().length == 0, "CLEAR empties the chain");
        click(findButton(*chainEd, "CLOSE"));
        pump(50);
        check(findOne<ui::ChainEditor>(ed) == nullptr, "CLOSE removes the overlay");
        check(!findButton(*strip, "EDIT")->getToggleState(), "EDIT button follows CLOSE");
    }
    click(findButton(*strip, "CHAIN"));

    // --- tools ---
    model.edit(pat, [](seq::Pattern& p) { seq::clearPattern(p); p.length = 16; p.at(0, BD, 0).setLevel(seq::StepLevel::Normal); });
    click(findButton(*strip, "SHIFT >"));
    check(model.pattern(pat).at(0, BD, 1).on() && !model.pattern(pat).at(0, BD, 0).on(), "SHIFT > rotates later");
    click(findButton(*strip, "< SHIFT"));
    check(model.pattern(pat).at(0, BD, 0).on(), "< SHIFT rotates earlier");
    click(findButton(*strip, "COPY"));
    click(findButton<ui::PatternButton>(*strip, "2"));
    check(model.selectedPattern() == 1, "pattern 2 selected");
    click(findButton(*strip, "PASTE"));
    check(model.pattern(1).at(0, BD, 0).on(), "COPY / PASTE copies the pattern to A02");
    click(findButton(*strip, "CLEAR"));
    check(!model.pattern(1).at(0, BD, 0).on(), "CLEAR clears the variation");
    click(findButton(*strip, "RANDOM"));
    {
        int on = 0;
        for (int t = 0; t < seq::kNumTracks; ++t) for (int st = 0; st < 16; ++st) on += model.pattern(1).at(0, t, st).on() ? 1 : 0;
        check(on > 0, "RANDOM writes steps");
    }
    click(findButton(*strip, "EUCLID"));
    check(chooseMenuItem(5), "EUCLID menu opened");
    {
        int on = 0;
        for (int st = 0; st < 16; ++st) on += model.pattern(1).at(0, BD, st).on() ? 1 : 0;
        check(on == 5, "EUCLID: 5 hits on BD");
    }

    // --- MIDI drop in + export ---
    {
        const juce::File f = processor->exportPatternToMidiFile(1, 0);
        check(f.existsAsFile(), "exportPatternToMidiFile writes a .mid");
        auto* handle = findOne<ui::MidiDragHandle>(ed);
        model.edit(2, [](seq::Pattern& p) { seq::clearPattern(p); });
        click(findButton<ui::PatternButton>(*strip, "3"));
        if (handle != nullptr && f.existsAsFile()) {
            check(handle->isInterestedInFileDrag({f.getFullPathName()}), "drag handle accepts .mid");
            handle->filesDropped({f.getFullPathName()}, 5, 5);
            int on = 0;
            for (int st = 0; st < 16; ++st) on += model.pattern(2).at(0, BD, st).on() ? 1 : 0;
            check(on == 5, "dropping the .mid imports it into the selected pattern");
        }
    }

    // --- FILL reaches the sequencer: the fill pattern plays within two measures ---
    {
        click(findButton<ui::PatternButton>(*strip, "1"));
        setParam(*processor, paramid::seqMode, 2.0f);
        click(findButton(*strip, "START / STOP"));
        advanceTransport(*processor, 10);
        click(findButton(*strip, "FILL"));
        const int fillIndex = seq::kFirstFillSlot + juce::roundToInt(raw(paramid::fillSlot));
        bool sawFill = false;
        for (int i = 0; i < 1000 && !sawFill; ++i) {
            advanceTransport(*processor, 1);
            sawFill = processor->playState().currentPattern.load() == fillIndex;
        }
        check(sawFill, "FILL: the fill pattern plays");
        click(findButton(*strip, "START / STOP"));
        advanceTransport(*processor, 2);
    }

    // --- knob text entry ---
    if (auto* acc = findControl(ed, "accent")) {
        juce::Array<juce::Label*> labels; collect(*acc, labels);
        if (!labels.isEmpty()) {
            labels[0]->showEditor();
            if (auto* te = labels[0]->getCurrentTextEditor()) te->setText("0.25", false);
            labels[0]->hideEditor(false);
            check(std::abs(processor->apvts().getParameter("accent")->convertFrom0to1(processor->apvts().getParameter("accent")->getValue()) - 0.25f) < 0.01f,
                  "value readout text entry sets ACCENT to 0.25");
        } else check(false, "accent readout label");
    }

    // --- MIDI learn via the right-click menu ---
    if (auto* knob = findControl(ed, "bd_decay")) {
        const auto p = mouse.at(*knob);
        mouse.press(p, juce::ModifierKeys::rightButtonModifier);
        mouse.release(p);
        check(chooseMenuItem(2), "parameter menu opened");  // Reset, MIDI learn
        check(processor->midiLearn().isLearning() && processor->midiLearn().learningParameter() == "bd_decay",
              "menu 'MIDI learn' arms learn for bd_decay");
        juce::AudioBuffer<float> buffer(juce::jmax(2, processor->getTotalNumOutputChannels()), 256);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 100), 0);
        processor->processBlock(buffer, midi);
        processor->serviceMessageThread();
        pump(20);
        check(processor->midiLearn().controllerFor("bd_decay") == 21, "CC 21 bound to bd_decay");
        // Forget via the menu: Reset, MIDI learn, Forget.
        mouse.press(p, juce::ModifierKeys::rightButtonModifier);
        mouse.release(p);
        check(chooseMenuItem(3), "parameter menu reopened");
        check(processor->midiLearn().controllerFor("bd_decay") < 0, "menu 'Forget CC' unbinds");
    }

    // --- PANEL: audition + select via the name plate, mute / solo ---
    {
        juce::Array<ui::NamePlate*> plates; collect(ed, plates);
        ui::NamePlate* cp = nullptr;
        for (auto* pl : plates) if (pl->isShowing() && pl->getTooltip().isNotEmpty() && plates.indexOf(pl) == CP) cp = pl;
        if (cp != nullptr) {
            const auto before = processor->playState().hits[CP].load();
            click(cp);
            advanceTransport(*processor, 2);
            check(processor->uiState().selectedTrack == CP, "name plate selects its track");
            check(processor->playState().hits[CP].load() != before, "name plate auditions the voice");
        } else check(false, "CP name plate");
        auto* voicePanel = findOne<ui::VoicePanel>(ed);
        juce::Array<ui::ParamButton*> pbs; collect(*voicePanel, pbs);
        int mutes = 0;
        for (auto* b : pbs) if (b->getButtonText() == "M" && mutes++ == SD) click(b);
        check(raw("sd_mute") > 0.5f, "PANEL mute button sets sd_mute");
    }

    // --- MIXER: pan knob, OUT box menu, solo ---
    ed.setView(ui::View::Mixer);
    pump(40);
    if (auto* pan = findControl(ed, "lt_pan")) {
        mouse.dragVertical(*pan, -30);
        check(raw("lt_pan") > 0.05f, "MIXER pan knob");
    } else check(false, "lt_pan knob");
    {
        auto* mixer = findOne<ui::MixerView>(ed);
        juce::Array<ui::ParamValueBox*> boxes; collect(*mixer, boxes);
        if (boxes.size() >= 2) {
            click(boxes[1]);  // SD OUT: Main, Out 2, ... -> 3 downs = Out 3
            check(chooseMenuItem(3), "OUT menu opened");
            check(juce::roundToInt(raw("sd_out")) == 2, "OUT menu assigns SD to output 3");
        } else check(false, "OUT boxes");
        if (auto* limiter = findButton<ui::ParamButton>(*mixer, "LIMITER")) {
            const float before = raw(paramid::limiter);
            click(limiter);
            check(std::abs(raw(paramid::limiter) - before) > 0.5f, "LIMITER button toggles");
        }
        if (auto* analog = findButton<ui::ParamButton>(*mixer, "ANALOG OUT")) {
            const float before = raw(paramid::analogOut);
            click(analog);
            check(std::abs(raw(paramid::analogOut) - before) > 0.5f, "ANALOG OUT button toggles");
        }
        check(findControl(ed, "master_comp") != nullptr && findControl(ed, "master_drive") != nullptr &&
              findControl(ed, "master_volume") != nullptr, "master DRIVE / COMP / VOLUME present");
    }

    // --- GRID: lane header selects + auditions, drag paints along a lane ---
    ed.setView(ui::View::Grid);
    pump(40);
    if (auto* grid = findOne<ui::GridView>(ed)) {
        model.edit(model.selectedPattern(), [](seq::Pattern& p) { seq::clearPattern(p); p.length = 16; });
        const auto area = grid->getLocalBounds().reduced(6);
        const float colW = static_cast<float>(area.getWidth() - 68) / 16.0f;
        const float laneH = static_cast<float>(area.getHeight() - 16) / 17.0f;
        auto cell = [&](int t, int st) {
            return open.peer->globalToLocal(grid->localPointToGlobal(juce::Point<float>(
                static_cast<float>(area.getX() + 68) + colW * (static_cast<float>(st) + 0.5f),
                static_cast<float>(area.getY() + 16) + laneH * (static_cast<float>(t) + 0.5f))));
        };
        const auto lane = open.peer->globalToLocal(grid->localPointToGlobal(juce::Point<float>(
            static_cast<float>(area.getX() + 30), static_cast<float>(area.getY() + 16) + laneH * (static_cast<float>(HT) + 0.5f))));
        mouse.press(lane);
        mouse.release(lane);
        check(processor->uiState().selectedTrack == HT, "GRID lane header selects HT");
        const auto a = cell(CH, 0), b = cell(CH, 5);
        mouse.press(a);
        for (int i = 1; i <= 10; ++i) mouse.move(a + (b - a) * (static_cast<float>(i) / 10.0f), juce::ModifierKeys::leftButtonModifier);
        mouse.release(b);
        bool all = true;
        for (int st = 0; st <= 5; ++st) all = all && model.pattern(model.selectedPattern()).at(0, CH, st).on();
        check(all, "GRID drag paints CH steps 1..6");
    }

    // --- header: view switch, preset next / kind, settings menu ---
    auto* header = findOne<ui::HeaderBar>(ed);
    click(findButton(*header, "PANEL"));
    check(ed.view() == ui::View::Panel, "PANEL button");
    click(findButton(*header, "MIXER"));
    check(ed.view() == ui::View::Mixer, "MIXER button");
    {
        const auto before = processor->presets().currentName(plugin::PresetKind::Full);
        click(findButton(*header, ">"));
        check(processor->presets().currentName(plugin::PresetKind::Full) != before, "> loads the next full preset");
        click(findButton(*header, "<"));
        check(processor->presets().currentName(plugin::PresetKind::Full) == before, "< goes back");
        click(findButton(*header, "KIT"));
        const auto kitBefore = processor->presets().currentName(plugin::PresetKind::Kit);
        click(findButton(*header, ">"));
        check(processor->presets().currentName(plugin::PresetKind::Kit) != kitBefore, "KIT kind: > loads the next kit");
        click(findButton(*header, "FULL"));
    }
    {
        // Settings: Quality > High (submenu 1, third item).
        auto* gear = findButton(*header, juce::String::charToString(0x2699));
        click(gear);
        check(chooseMenuItem(1, 2), "settings menu: Quality submenu");
        check(juce::roundToInt(raw(paramid::quality)) == 2, "settings: Quality High");
        // UI scale (4th submenu) > 125 % (fourth item).
        click(gear);
        check(chooseMenuItem(4, 3), "settings menu: UI scale submenu");
        pump(60);
        check(std::abs(ed.scale() - 1.25f) < 0.01f && std::abs(processor->uiState().scale - 1.25f) < 0.01f, "settings: UI scale 125 %");
        ed.setScale(1.0f);
        pump(30);
    }

    // --- session restore while open: the editor follows the restored UI state ---
    {
        juce::MemoryBlock state;
        auto ui = processor->uiState();
        ui.view = 1; ui.selectedTrack = 7;
        processor->setUiState(ui);
        processor->getStateInformation(state);
        ed.setView(ui::View::Panel);
        processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        pump(60);
        check(ed.view() == ui::View::Grid, "state restore with the editor open switches it to the stored view");
    }

    // --- gestures balanced ---
    pump(100);
    check(gestures.begins == gestures.ends, "every gesture begun was ended");
    std::printf("  gestures: %d begin / %d end\n", gestures.begins, gestures.ends);
    processor->removeListener(&gestures);
    return failures == 0 ? 0 : 1;
}

// --- lifetime: close the editor with menus, alerts and drags in flight ------------------------

int lifetime(int iterations) {
    using namespace mw808;
    auto processor = makeProcessor();
    setParam(*processor, paramid::seqMode, 1.0f);
    GestureCounter gestures;
    processor->addListener(&gestures);
    AudioThread audio(*processor, true);
    for (int i = 0; i < iterations; ++i) {
        auto open = std::make_unique<OpenEditor>(*processor);
        if (open->editor == nullptr || open->peer == nullptr) { check(false, "editor + peer"); break; }
        auto& ed = *open->editor;
        Mouse mouse{open->peer};
        ed.setView(static_cast<ui::View>(i % 3));
        pump(20);
        auto* strip = findOne<ui::SequencerStrip>(ed);
        auto* header = findOne<ui::HeaderBar>(ed);
        switch (i % 9) {
            case 0: {  // parameter menu open
                if (auto* c = findControl(ed, i % 3 == 2 ? "bd_pan" : "master_volume")) {
                    const auto p = mouse.at(*c);
                    mouse.press(p, juce::ModifierKeys::rightButtonModifier);
                    mouse.release(p);
                }
                break;
            }
            case 1: {  // settings menu with a submenu open
                mouse.click(*findButton(*header, juce::String::charToString(0x2699)));
                pump(30);
                if (auto* m = topModal()) { m->keyPressed(juce::KeyPress(juce::KeyPress::downKey)); m->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)); }
                break;
            }
            case 2: mouse.click(*findButton(*header, "SAVE")); break;  // save AlertWindow (desktop window)
            case 3: {  // knob mid-drag
                if (auto* c = findControl(ed, i % 3 == 2 ? "sd_drive" : "tempo")) {
                    const auto p = mouse.at(*c);
                    mouse.press(p);
                    mouse.move(p.translated(0.0f, -20.0f), juce::ModifierKeys::leftButtonModifier);
                }
                break;
            }
            case 4: {  // step key mid-paint
                juce::Array<ui::StepKey*> keys; collect(*strip, keys);
                const auto a = mouse.at(*keys[0]), b = mouse.at(*keys[3]);
                mouse.press(a);
                mouse.move(b, juce::ModifierKeys::leftButtonModifier);
                break;
            }
            case 5: {  // external drag of the .mid in flight
                if (auto* h = findOne<ui::MidiDragHandle>(ed)) {
                    const auto p = mouse.at(*h);
                    mouse.press(p);
                    for (int k = 1; k <= 4; ++k) mouse.move(p.translated(0.0f, -5.0f * static_cast<float>(k)), juce::ModifierKeys::leftButtonModifier);
                }
                break;
            }
            case 6: mouse.click(*findButton(*header, "...")); break;  // preset menu
            case 7: {  // serial number AlertWindow via the settings menu (submenu 3, item 3 "Enter...")
                mouse.click(*findButton(*header, juce::String::charToString(0x2699)));
                chooseMenuItem(3, 2);
                break;
            }
            case 8: {  // chain editor + scale value box menu + about box
                ed.showChainEditor(true);
                juce::Array<ui::ValueBox*> boxes; collect(*strip, boxes);
                if (boxes.size() > 1) mouse.click(*boxes[1]);
                break;
            }
            default: break;
        }
        pump(15);
        if (i % 4 == 0) processor->setCurrentProgram(i % juce::jmax(1, processor->getNumPrograms()));
        // Host closes the window now, whatever is open.
        open.reset();
        pump(30);
        // Nothing of ours may still be modal.
        check(topModal() == nullptr || juce::String(typeid(*topModal()).name()).contains("FileChooser"), "no modal window left behind");
        if (failures > 0) { std::printf("  iteration %d (case %d)\n", i, i % 9); break; }
    }
    juce::ModifierKeys::currentModifiers = {};
    pump(200);
    std::printf("  %d lifetime cycles, audio thread processed %ld blocks, gestures %d begin / %d end\n", iterations, audio.blocks(),
                gestures.begins, gestures.ends);
    check(gestures.begins == gestures.ends, "no host gesture left open by a closed editor");
    processor->removeListener(&gestures);
    return failures == 0 ? 0 : 1;
}

// --- perf: what actually gets repainted while the sequencer runs -------------------------------

struct RepaintProbe final : juce::Component {
    RepaintProbe() { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override {
        ++frames;
        // The clip is a rectangle list; sample it on an 8 px lattice for the real dirty area.
        auto& ctx = g.getInternalContext();
        const auto bounds = g.getClipBounds();
        long hit = 0, total = 0;
        for (int y = 0; y < getHeight(); y += 8)
            for (int x = 0; x < getWidth(); x += 8) {
                ++total;
                if (bounds.contains(x, y) && ctx.clipRegionIntersects({x, y, 1, 1})) ++hit;
            }
        const double frac = static_cast<double>(hit) / static_cast<double>(juce::jmax(1L, total));
        area += frac;
        maxFrac = juce::jmax(maxFrac, frac);
        if (frac > 0.9) ++fullFrames;
    }
    void reset() { frames = fullFrames = 0; area = maxFrac = 0.0; }
    long frames = 0, fullFrames = 0;
    double area = 0.0, maxFrac = 0.0;
};

int perf(int seconds) {
    using namespace mw808;
    auto processor = makeProcessor();
    setParam(*processor, paramid::seqMode, 1.0f);
    OpenEditor open(*processor);
    if (open.editor == nullptr) return 1;
    auto& ed = *open.editor;
    RepaintProbe probe;
    ed.addAndMakeVisible(probe);
    probe.setBounds(ed.getLocalBounds());
    const char* names[] = {"PANEL", "GRID", "MIXER"};
    // Stopped, nothing sounding: the editor should not paint at all.
    for (int v = 0; v < 3; ++v) {
        ed.setView(static_cast<ui::View>(v));
        pump(1500);  // let lamps fade and the meter fall
        probe.reset();
        pump(seconds * 1000);
        std::printf("  %-5s stopped: %.2f frames/s\n", names[v], static_cast<double>(probe.frames) / seconds);
        check(probe.frames <= seconds, "stopped editor is (nearly) paint-free");
    }
    AudioThread audio(*processor, true);
    for (int v = 0; v < 3; ++v) {
        ed.setView(static_cast<ui::View>(v));
        pump(500);
        probe.reset();
        const double c0 = cpuSeconds();
        pump(seconds * 1000);
        const double used = cpuSeconds() - c0;
        std::printf("  %-5s playing: %.1f frames/s, mean dirty %.1f%% of the window per frame, max %.1f%%, %ld full repaints; CPU %.2f%% of a core (incl. DSP)\n",
                    names[v], static_cast<double>(probe.frames) / seconds, probe.frames > 0 ? probe.area / static_cast<double>(probe.frames) * 100.0 : 0.0,
                    probe.maxFrac * 100.0, probe.fullFrames, used / seconds * 100.0);
        check(probe.fullFrames == 0, "no full-window repaint while playing");
    }
    ed.removeChildComponent(&probe);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: mw808_ui_tool screenshots <dir> | stress <n> | idle <seconds> | interact | wiring | lifetime <n> | perf <seconds>\n");
        return 2;
    }
    // JUCE installs its X error handler only in standalone JUCE apps; this is a
    // plain console app, and Xlib's default handler exits the process on the
    // first harmless BadAtom a bare Xvfb (no window manager) produces.
#if defined(__linux__)
    XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
#endif
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String cmd(argv[1]);
    int result = 2;
    std::printf("mw808_ui_tool %s\n", argv[1]);
    if (cmd == "screenshots" && argc >= 3) result = screenshots(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
    else if (cmd == "stress") result = stress(argc >= 3 ? juce::String(argv[2]).getIntValue() : 200);
    else if (cmd == "idle") result = idle(argc >= 3 ? juce::jmax(1, juce::String(argv[2]).getIntValue()) : 5);
    else if (cmd == "interact") result = interact();
    else if (cmd == "wiring") result = wiring();
    else if (cmd == "lifetime") result = lifetime(argc >= 3 ? juce::String(argv[2]).getIntValue() : 90);
    else if (cmd == "perf") result = perf(argc >= 3 ? juce::jmax(1, juce::String(argv[2]).getIntValue()) : 5);
    else std::fprintf(stderr, "unknown command\n");
    juce::PopupMenu::dismissAllActiveMenus();
    pump(20);
    std::printf("%s: %s (%d failure%s)\n", argv[1], result == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return result;
}

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The always-visible sequencer strip (plugin.md §7.3): START/STOP, the 16 step
// keys with LEDs, page 1-4, the track selector (16 voices + AC), variation
// A / AB / B, the bank A-D x 16 pattern selector (queued pattern blinks),
// length / scale / shuffle / track length, step-edit mode LEVEL / FLAM / SUB /
// PROB, fill controls, chain toggle + editor, record, the pattern tools
// (copy, paste, clear, shift, random, euclid) and the MIDI drag handle.
//
// Pattern data is read from and written through PatternModel on the message
// thread; the play position comes from PlayState through poll().

#pragma once

#include <optional>

#include "StepEdit.h"
#include "UiCommon.h"

namespace mw808::ui {

// One step key with its LED.
class StepKey : public juce::Component {
public:
    explicit StepKey(int indexInPage);

    struct State {
        seq::Step step{};
        bool inRange = true;      // within the track's loop length
        bool running = false;
        int absoluteStep = 0;     // 0..63
        StepMode mode = StepMode::Level;
        int track = 0;
    };
    void setState(const State& s);
    const State& state() const noexcept { return state_; }
    int index() const noexcept { return index_; }

    std::function<void(int index, const juce::MouseEvent&)> onPress;
    std::function<void(int index)> onDragOver;
    std::function<void(int index, juce::Component&)> onMenu;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }

private:
    int index_;
    State state_;
    bool hover_ = false, down_ = false;
};

// A pattern slot button that can blink (queued pattern).
class PatternButton : public juce::TextButton {
public:
    using juce::TextButton::TextButton;
    void setBlink(bool blinking, bool phase);
    void setPlaying(bool playing);
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    bool blinking_ = false, phase_ = false, playing_ = false;
};

// Drag the selected pattern out as a .mid file; drop a .mid file to import it.
class MidiDragHandle : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient {
public:
    explicit MidiDragHandle(Context& ctx);
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { dragStarted_ = false; }
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dropHover_ = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dropHover_ = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    Context& ctx_;
    bool hover_ = false, dropHover_ = false, dragStarted_ = false;
};

// The chain editor overlay (shown by the editor over the view area).
class ChainEditor : public juce::Component, private plugin::PatternModel::Listener {
public:
    explicit ChainEditor(Context& ctx);
    ~ChainEditor() override;
    std::function<void()> onClose;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void chainChanged() override { repaint(); }
    void bankChanged() override { repaint(); }
    juce::Rectangle<int> chipBounds(int index) const;
    void edit(std::function<void(seq::Chain&)> fn);

    Context& ctx_;
    juce::TextButton addButton_{"ADD SELECTED"}, removeButton_{"REMOVE LAST"}, clearButton_{"CLEAR"}, closeButton_{"CLOSE"};
    juce::Rectangle<int> chipArea_;
    std::unique_ptr<juce::ParameterAttachment> chainAttachment_;
};

class SequencerStrip final : public CachedBackground,
                             private plugin::PatternModel::Listener,
                             private EditorState::Listener {
public:
    explicit SequencerStrip(Context& ctx);
    ~SequencerStrip() override;

    void poll(const PlayState& ps);
    std::function<void(bool)> onChainEditToggle;
    // The editor tells the strip when the chain editor is shown or hidden, so
    // the EDIT button follows however the overlay was closed.
    void setChainEditorOpen(bool open) { chainEditButton_.setToggleState(open, juce::dontSendNotification); }

    void resized() override;

private:
    void paintStatic(juce::Graphics& g) override;
    void paintDynamic(juce::Graphics& g) override;

    // PatternModel::Listener
    void patternChanged(int index) override;
    void bankChanged() override;
    void selectionChanged() override;
    // EditorState::Listener
    void uiStateChanged() override;

    void refreshKeys();
    void refreshPatternButtons();
    void refreshTrackButtons();
    void refreshPageButtons();
    void refreshModeButtons();
    void refreshValueBoxes();

    void stepKeyPressed(int index, const juce::MouseEvent& e);
    void keyDragged(int index);
    void editStep(int absoluteStep, std::function<seq::Step(seq::Step)> fn);
    int selectedPattern() const { return ctx_.processor.patterns().selectedPattern(); }
    const seq::Pattern& pattern() const { return ctx_.processor.patterns().pattern(selectedPattern()); }
    int editVariation() const { return ctx_.editVariation(); }

    // Tools.
    void copy(bool trackOnly);
    void paste(bool trackOnly);
    void clear(bool trackOnly);
    void shift(int steps, bool trackOnly);
    void randomise(bool trackOnly);
    void showEuclidMenu();

    Context& ctx_;

    // Left block.
    std::unique_ptr<ParamButton> run_;
    Lamp playLamp_{mw808Colours::ledGreenOn, mw808Colours::ledGreenOff};
    std::unique_ptr<ChoiceButtons> variation_;
    std::unique_ptr<ParamButton> record_, chain_;
    juce::TextButton fillButton_{"FILL"}, chainEditButton_{"EDIT"};

    // Middle.
    juce::OwnedArray<juce::TextButton> bankButtons_;
    juce::OwnedArray<PatternButton> patternButtons_;
    juce::OwnedArray<juce::TextButton> pageButtons_;
    juce::OwnedArray<StepKey> keys_;
    juce::OwnedArray<juce::TextButton> trackButtons_;
    juce::OwnedArray<juce::TextButton> modeButtons_;
    juce::TextButton copyButton_{"COPY"}, pasteButton_{"PASTE"}, clearButton_{"CLEAR"}, shiftLeftButton_{"< SHIFT"},
        shiftRightButton_{"SHIFT >"}, randomButton_{"RANDOM"}, euclidButton_{"EUCLID"};

    // Right block.
    std::unique_ptr<ValueBox> length_, scale_, shuffle_, trackLength_;
    std::unique_ptr<ParamValueBox> autoFill_, fillSlot_;
    MidiDragHandle midiHandle_;

    juce::Rectangle<int> patternLegend_, hintArea_, leftLegend_, rightLegend_;

    int shownBank_ = 0;
    int runningStep_ = -1;
    int shownVariation_ = 0;
    int blinkTick_ = 0;
    bool blinkPhase_ = false;
    int lastQueued_ = -1;
    int lastCurrent_ = -1;
    bool lastPlaying_ = false;
    int runLampState_ = -1;

    // Drag painting.
    bool painting_ = false;
    seq::Step brush_{};
    int paintTrack_ = 0;
    int lastPainted_ = -1;

    // Clipboard.
    std::optional<seq::Pattern> patternClip_;
    std::optional<std::array<seq::Step, seq::kMaxSteps>> trackClip_;
    std::uint8_t trackClipLength_ = 0;
    std::uint32_t randomSeed_ = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerStrip)
};

}  // namespace mw808::ui

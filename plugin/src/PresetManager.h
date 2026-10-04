// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Presets (docs/design/plugin.md §4). Three kinds, one file format:
//
//   kit       per-voice sound and mixer parameters (level, tune, decay, tone,
//             shape, pan, drive, output) plus accent and the master section.
//             Mute / solo and the sequencer and MIDI settings are not part of a kit.
//   patterns  the pattern bank and the chain.
//   full      everything in the plugin state. Loading one leaves the editor's
//             UI state (scale, view, ...) and the MIDI-learn map alone: those
//             belong to the session, not to a sound.
//
// Factory presets are compiled in (mw808/Factory.h) and come first in every
// list; user presets are files "<name>.mw808" (ValueTree XML, root
// <mw808preset kind="...">) under <root>/{Kits,Patterns,Full}/, where <root>
// defaults to ~/.config/mwAudio/mw808/Presets (or $MW808_PRESET_DIR).
//
// Message thread only. Nothing touches the disk until a list is first asked
// for, a preset is saved or refresh() is called.

#pragma once

#include <array>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

class mw808Processor;

namespace mw808::plugin {

enum class PresetKind : int { Kit = 0, Patterns = 1, Full = 2 };
inline constexpr int kNumPresetKinds = 3;

struct PresetEntry {
    juce::String name;
    juce::String category;  // factory category, or "User"
    bool factory = false;
    int factoryIndex = -1;  // index into mw808::factory when `factory`
    juce::File file;        // the file when not `factory`
};

class PresetManager {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void presetListChanged(PresetKind) {}
        virtual void presetLoaded(PresetKind) {}
    };

    explicit PresetManager(::mw808Processor& processor);

    // --- where user presets live -------------------------------------------
    static juce::File defaultUserRoot();
    void setUserRoot(const juce::File& root);  // tests point this at a temp directory
    juce::File userRoot() const { return root_; }
    juce::File userDirectory(PresetKind kind) const;

    // --- lists ------------------------------------------------------------------
    void refresh();  // rescan the user directories (creating them when missing)
    int numPresets(PresetKind kind);
    int numFactoryPresets(PresetKind kind) const;
    PresetEntry preset(PresetKind kind, int index);  // default entry when out of range
    juce::StringArray names(PresetKind kind);

    // --- actions ------------------------------------------------------------------
    bool load(PresetKind kind, int index);
    bool loadFile(const juce::File& file);                    // the kind comes from the file
    bool saveUser(PresetKind kind, const juce::String& name); // replaces a user preset of the same name
    bool deleteUser(PresetKind kind, int index);              // user presets only
    bool loadNext(PresetKind kind) { return step(kind, +1); }
    bool loadPrevious(PresetKind kind) { return step(kind, -1); }

    // The preset last loaded or saved, by name (kept in the session state).
    juce::String currentName(PresetKind kind) const;
    int currentIndex(PresetKind kind);  // -1 when the name matches nothing in the list

    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    static constexpr const char* kFileExtension = ".mw808";

private:
    void ensureScanned();
    bool step(PresetKind kind, int delta);
    bool loadFactory(PresetKind kind, int factoryIndex);
    bool applyTree(const juce::ValueTree& presetTree, const juce::String& name);
    void setCurrentName(PresetKind kind, const juce::String& name);

    ::mw808Processor& processor_;
    juce::File root_;
    bool scanned_ = false;
    std::array<std::vector<PresetEntry>, kNumPresetKinds> user_{};
    juce::ListenerList<Listener> listeners_;
};

}  // namespace mw808::plugin

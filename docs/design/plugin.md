# mw808 plugin shell and UI

How the JUCE plugin wraps `mw808::Machine` (see `architecture.md` for the core). This is
the contract between the processor/state code (`plugin/src`), the UI (`plugin/ui`) and the
tests (`tests/plugin`).

## 1. Identity and formats

| | |
|---|---|
| Product name | `mw808` (one CMake variable, `MW_PRODUCT_NAME`, so a rename is one line) |
| Company | `mwAudio`, manufacturer code `mwAu`, plugin code `mw80` |
| Bundle / CLAP id | `com.mwaudio.mw808` |
| LV2 URI | `https://github.com/mattWoolly/mw808` |
| Formats (Linux) | VST3, LV2, Standalone, plus CLAP through clap-juce-extensions |
| Category | instrument / drum: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT TRUE`, `IS_MIDI_EFFECT FALSE`; VST3 categories `Instrument Drum`; CLAP features `instrument drum-machine stereo` |
| Defines | `JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0` |
| Classes | `mw808Processor`, `mw808Editor`, `mw808LookAndFeel`, `mw808Colours` |

Buses: one stereo main output (enabled) and 11 stereo individual outputs named
`Out 2` … `Out 12` (disabled by default; a layout is supported when Main is stereo and
every other bus is stereo or disabled). No audio inputs. Latency 0. Tail 5 s.

## 2. Parameters (APVTS, `ParameterID{id, 1}`, ids are frozen once shipped)

`plugin/src/ParameterIDs.h` holds every id as a constant and builds the per-voice ids from
`kVoiceInfo[v].id`. Ranges never change at runtime.

Per voice, for each of the 16 voices (prefix `bd`, `sd`, `lt`, `mt`, `ht`, `lc`, `mc`, `hc`,
`rs`, `cl`, `cp`, `ma`, `cb`, `cy`, `oh`, `ch`):

| id | name | range | default |
|---|---|---|---|
| `<v>_level` | `<CODE> Level` | 0..1 | 0.8 |
| `<v>_tune` | `<CODE> Tune` | −12..+12 st, step 0.01 | 0 |
| `<v>_decay` | `<CODE> Decay` | 0..1 | `kVoiceInfo[v].defaults.decay` |
| `<v>_tone` | `<CODE> <toneName>` | 0..1 | defaults.tone — only where `toneName` is non-empty |
| `<v>_shape` | `<CODE> <shapeName>` | 0..1 | defaults.shape — only where `shapeName` is non-empty |
| `<v>_pan` | `<CODE> Pan` | −1..+1 | 0 |
| `<v>_drive` | `<CODE> Drive` | 0..1 | 0 |
| `<v>_mute` | `<CODE> Mute` | bool | off |
| `<v>_solo` | `<CODE> Solo` | bool | off |
| `<v>_out` | `<CODE> Output` | choice `Main`, `Out 2` … `Out 12` | Main |

Global:

| id | name | range | default |
|---|---|---|---|
| `master_volume` | Master Volume | −60..+12 dB | 0 |
| `accent` | Accent | 0..1 | 0.7 |
| `analog_out` | Analog Output Stage | bool | on |
| `master_drive` | Master Drive | 0..1 | 0 |
| `master_comp` | Master Comp | 0..1 | 0 |
| `limiter` | Limiter | bool | on |
| `quality` | Quality | choice Eco, Standard, High | Standard |
| `variance` | Analog Variance | 0..1 | 0 |
| `seq_mode` | Sequencer Mode | choice Off, Host Sync, Internal, Host Note | Host Sync |
| `seq_run` | Run | bool | off |
| `tempo` | Tempo | 40..300 BPM, step 0.1 | 120 |
| `shuffle_depth` | Shuffle Depth | 0..1 | 1 |
| `flam_time` | Flam Time | 0..60 ms | 24 |
| `variation` | Variation | choice A, AB, B | A |
| `auto_fill` | Auto Fill | choice Manual, 16, 12, 8, 4, 2 | Manual |
| `fill_slot` | Fill Pattern | choice 1, 2, 3, 4 | 1 |
| `chain` | Chain | bool | off |
| `record` | Record | bool | off |
| `velocity_sens` | Velocity Sensitive | bool | on |
| `bass_channel` | Bass Channel | choice Off, 1 … 16 | Off |
| `midi_out` | MIDI Out | bool | off |

Rules: a parameter listener never writes another parameter. `processBlock` reads the cached
`std::atomic<float>*` of every parameter into one `mw808::MachineParams` snapshot per block.

When the host supplies no transport (standalone), `Host Sync` behaves as `Internal`, so
the Run button always works.

## 3. Non-parameter state

Owned on the message thread by `PatternModel` (`plugin/src/state/PatternModel.h`):

- the authoritative `mw808::seq::PatternBank` (64 patterns + chain),
- the selected pattern index,
- the unit seed ("serial number", `uint32`),
- MIDI-learn map (CC number → parameter id),
- UI state: scale factor, active view, selected track, step-edit mode, selected page.

`PatternModel` API (message thread only; a mutex guards it against `getStateInformation`
arriving on another thread — the audio thread never touches it):

```cpp
const mw808::seq::Pattern& pattern(int index) const;
void setPattern(int index, const mw808::seq::Pattern&);        // model + Command::SetPattern to audio
template <class Fn> void edit(int index, Fn&& fn);             // fn(Pattern&) then setPattern
void setChain(const mw808::seq::Chain&);
void replaceBank(const mw808::seq::PatternBank&);              // model + Machine::postBank (generation++)
int  selectedPattern() const;   void selectPattern(int index, bool immediate = false);
void addListener(Listener*);    // patternChanged(index), bankChanged(), selectionChanged()
```

Delivery guarantees:
- `replaceBank` (state restore, preset load) goes through `Machine::postBank`, so the very
  next audio block uses it, even when no UI timer runs (offline bounce right after load).
- Single edits go through the command queue stamped with the current bank generation.
  If the queue is full the pattern index is marked dirty and re-posted from the
  processor's timer; edits are never lost.
- The processor runs a message-thread timer (30 Hz) whether or not the editor is open. It
  drains `Machine::poll` notices into the model (`StepRecorded`, `PatternSelected`),
  re-posts dirty patterns and deletes retired bank handoffs.

Serialised state (`getStateInformation`): a ValueTree `mw808` with `stateVersion = 1`,
containing the APVTS state, and a `sequencer` child holding each non-empty pattern as a
base64 blob of its raw bytes (`Pattern` is all `uint8_t`, so the layout is stable; store
`sizeof(Pattern)` and a `patternFormat = 1` and refuse blobs that do not match), the chain,
selected pattern, unit seed, MIDI-learn map and UI state. `setStateInformation` restores
exactly what was saved (bit-for-bit patterns; parameters via APVTS `replaceState`), runs the
migration chain for older `stateVersion`s, and falls back to defaults — never crashes — on
unknown future versions or corrupt data.

## 4. Presets

Three kinds, one file format (ValueTree XML, extension `.mw808`), `kind` attribute:

- **kit** — the per-voice sound and mixer parameters plus accent and master section
  (not the sequencer parameters),
- **patterns** — the pattern bank and chain,
- **full** — everything in the plugin state.

Factory content is compiled in (`mw808/Factory.h` in the core supplies kits and pattern
banks as plain data; the shell maps kits onto parameters). User presets live in
`~/.config/mwAudio/mw808/Presets/{Kits,Patterns,Full}/`. `PresetManager` lists factory +
user presets per kind, loads, saves, deletes user presets, and steps next/previous. Host
programs: expose the factory full presets through `getNumPrograms()` etc.

Loading a kit sets parameters with `setValueNotifyingHost` inside begin/end gestures.

## 5. MIDI

- Notes → voices through `mw808::kNoteToVoice` (any channel). Velocity → dynamics per
  `mw808::velocityToHit`. Notes on `bass_channel` play the bass drum chromatically.
- `Host Note` mode: notes from 36 up select and hold patterns.
- MIDI learn: a parameter can be bound to a CC (right-click a control → Learn / Forget).
  CC values set the parameter with `setValueNotifyingHost` on the message thread (the
  audio thread forwards CCs through a lock-free FIFO; it never calls into APVTS).
- MIDI out (when `midi_out`): sequencer hits as channel-10 notes from `Machine`.

## 6. Processor API used by the editor

```cpp
juce::AudioProcessorValueTreeState& apvts();
PatternModel& patterns();
PresetManager& presets();
const mw808::PlayState& playState() const;      // atomics: step, pattern, variation, hits[], peaks
void audition(mw808::VoiceId, bool accent);     // Command::Audition
void requestFill();
void panic();
MidiLearn& midiLearn();
juce::File exportPatternToMidiFile(int patternIndex, int variation);   // temp .mid for drag-and-drop
bool importMidiFile(const juce::File&, int patternIndex);
```

## 7. Editor

Vector-drawn (JUCE `Graphics`, no images, no OpenGL, no WebView), static layers cached to
an image per scale factor. Base size 1280 × 720, fixed aspect, resizable 0.6×–2.0× with the
scale stored in state. Follows the mwAudio style guide
(`/home/mwoolly/projects/ssl4k-2/docs/MW_AUDIO_STYLE_GUIDE.md`): lowercase `mw` wordmark,
`mwAudio` mark, a `mw808Colours` palette table.

Look: a dark charcoal panel with cream legends and the classic warm colour run on the step
keys — four red, four orange, four yellow, four off-white — and matching accent colours on
the voice strips. It should read at a glance as "that drum machine" without copying any
maker's logo or lettering.

Layout (top to bottom):

1. **Header** — wordmark, preset browser (kind selector, previous/next, name, save, menu),
   view switch **PANEL / GRID / MIXER**, tempo readout, output meter, settings (quality,
   variance, serial number, UI scale, MIDI options, about).
2. **Voice strips** (PANEL view) — 16 columns in voice order. Each: LEVEL (large knob) on
   top, then the voice's smaller knobs (TUNE, DECAY, and TONE / shape where it has them,
   with the hardware-original knobs visually distinguished from the extended ones), a
   trigger LED that flashes on `playState().hits[v]`, mute and solo buttons, and a name
   plate that auditions the voice when clicked and selects its track for step editing.
   Left block: ACCENT, MASTER VOLUME, TEMPO (large), sequencer MODE.
3. **Sequencer strip** (always visible) — START/STOP, the 16 step keys with LEDs showing the
   selected track of the selected pattern (the running step is highlighted), page selector
   1–4 for patterns up to 64 steps, track selector (16 voices + AC), variation A / AB / B,
   pattern selector (bank A–D × 16, queued pattern blinks), length, scale, shuffle,
   step-edit mode (LEVEL / FLAM / SUB / PROB), fill (manual trigger, auto interval, slot),
   chain toggle and editor, record, tools (copy, paste, clear, shift, random, euclid), and
   a drag handle that drags the pattern out as a MIDI file.
   Step keys: click cycles Off → Normal → Accent → Off (shift-click: Weak); in FLAM / SUB /
   PROB mode a click edits that property of an active step. Click-drag paints.
4. **GRID view** replaces the voice strips with all 17 lanes × steps for the whole pattern
   (current page set visible, playhead column highlighted), same click semantics.
5. **MIXER view** replaces the voice strips with one channel per voice: pan, drive, output
   assignment, mute, solo, level, plus the master section (drive, comp, limiter, analog
   output stage, volume).

Every knob: drag vertically, shift-drag fine, double-click resets, wheel adjusts, text
entry on double-click of the value readout, right-click menu with MIDI learn. Every
control is attached through APVTS attachments; pattern editing goes through `PatternModel`.
The editor polls `playState()` on a 30–60 Hz timer and repaints only what changed.

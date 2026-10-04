# mw808 architecture

mw808 is a circuit-informed emulation of the classic 16-voice analog drum machine, with a
built-in step sequencer, for Linux (VST3, CLAP, LV2, standalone). This document is the
contract between the modules. Research that backs the numbers lives in `research/01..06`.

## 1. Rules that apply everywhere

- **No brand names.** No manufacturer names or their product marks (model numbers with
  the maker's prefix, the maker's name, the maker's product slogans) in code, UI strings,
  presets, file names or binary strings. `scripts/forbidden_tokens.txt` is the list. Research documents are the only exception. The product is `mw808`,
  company `mwAudio`. Say "the original machine", "the hardware".
- **Licence header** on every source, CMake and shell file:
  `// SPDX-License-Identifier: AGPL-3.0-or-later` /
  `// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.`
- **C++20, g++ 13.** No `-ffast-math`. The core is deterministic: same input, same
  bytes out, on the same toolchain.
- **Realtime safety.** Anything reachable from `Machine::process` never allocates, locks,
  does I/O or throws. All state is fixed-size members. `prepare()` of voices is also
  allocation-free (the engine re-prepares voices on the audio thread when quality or the
  unit seed changes).
- **No state reset on retrigger.** Voices are monophonic circuits; a new hit excites
  whatever is still ringing. That overlap is a large part of the sound. `reset()` is only
  for transport-independent silence (prepare, panic).
- **Sample-rate independence.** Pitch, decay times and levels must hold from 44.1 kHz to
  192 kHz. Use the TPT filters in `mw808/dsp/Filters.h` (pre-warped) for anything tuned.
- `std::atomic<T>` only for T of 8 bytes or less (16-byte atomics are not lock-free here).

## 2. Layout and ownership

```
libs/mw808-core/include/mw808/
  Voices.h            voice roster, Hit, VoiceParams, VoiceSetup, kVoiceInfo   (contract)
  dsp/Math.h Filters.h Noise.h      shared primitives                          (contract)
  dsp/<other>.h       further primitives, added by whoever needs them
  voices/*.h          the 16 voices, header-only                               (§3)
  seq/Pattern.h       pattern POD model                                        (contract)
  seq/Sequencer.h     sequencer public API                                     (contract)
  seq/PatternOps.h    pattern editing helpers            (§5.6)
  seq/MidiFile.h      pattern -> standard MIDI file      (§5.7)
  Engine.h            voices + mixer + buses + output stage
  Machine.h           Engine + Sequencer + MIDI + cross-thread queues
libs/mw808-core/src/  Engine.cpp Machine.cpp seq/*.cpp
plugin/               JUCE shell (processor, parameters, state, presets) and UI
tools/mw808-render/   CLI: render voices/patterns to WAV, benchmark
tests/unit/           Catch2, one executable per file
```

CMake targets: `mw808_dsp` (INTERFACE, headers: dsp + voices), `mw808_seq` (STATIC,
`src/seq/*.cpp`), `mw808_core` (STATIC, Engine + Machine, links both). Sources and tests
are globbed, so adding a file needs no CMake edit. A test file `tests/unit/<prefix>_<name>.cpp`
becomes executable `mw808_test_<prefix>_<name>`; the prefix picks what it links:
`dsp_` → `mw808_dsp`, `seq_` → `mw808_seq`, `core_` → `mw808_core`.

## 3. Voice contract

Every voice is a concrete class in `namespace mw808::voices`, header-only, with:

```cpp
void prepare(const VoiceSetup&) noexcept;   // no allocation
void reset() noexcept;                      // silence; clear all state
void setParams(const VoiceParams&) noexcept;// called before render, at most once per sub-block
void trigger(const Hit&) noexcept;          // takes effect at the next render sample
void render(float* out, int n) noexcept;    // OVERWRITES out[0..n) with mono samples at VoiceSetup::sampleRate
bool isActive() const noexcept;             // false => render would produce exact zeros; engine skips it
```

Voices that use the shared noise generator take it as an input instead:
`void render(float* out, const float* noise, int n) noexcept;` where `noise` is unit-RMS
white noise at the same rate (`mw808::dsp::WhiteNoise`). Every such voice gets the SAME
buffer, as in the hardware (one generator; the toms take a low-passed copy of it).

Classes and files:

| File | Class | Voices | render signature |
|---|---|---|---|
| `voices/BassDrum.h` | `BassDrum` | BD | `(out, n)` |
| `voices/SnareDrum.h` | `SnareDrum` | SD | `(out, noise, n)` |
| `voices/TomConga.h` | `TomConga` | LT MT HT LC MC HC — `prepare(setup, VoiceId which)` | `(out, noise, n)` |
| `voices/RimShot.h` | `RimShot` | RS | `(out, n)` |
| `voices/Claves.h` | `Claves` | CL | `(out, n)` |
| `voices/HandClap.h` | `HandClap` | CP | `(out, noise, n)` |
| `voices/Maracas.h` | `Maracas` | MA | `(out, noise, n)` |
| `voices/Cowbell.h` | `Cowbell` | CB | `(out, n)` |
| `voices/Cymbal.h` | `Cymbal` | CY | `(out, n)` |
| `voices/HiHats.h` | `HiHats` | OH + CH | see below |

`HiHats` models both hats and the choke between them:

```cpp
void prepare(const VoiceSetup&) noexcept;
void reset() noexcept;
void setParams(const VoiceParams& open, const VoiceParams& closed) noexcept;
void triggerOpen(const Hit&) noexcept;
void triggerClosed(const Hit&) noexcept;    // also chokes the open hat (fast fade, as the circuit does)
void render(float* outOpen, float* outClosed, int n) noexcept;
bool isActive() const noexcept;
```

Contract details:

- **Dynamics come from `Hit::volts`** (the common-trigger pulse height: 4 V normal, 4–14 V
  accented, below 4 V for weak steps and low velocities). Model accent as the circuit
  being hit harder, with the timbre change that follows. Response must be monotonic and
  click-free down to ~0.5 V. The metal voices and cowbell saw a 7–14 V range in hardware;
  map the 4–14 V contract range onto it inside the voice.
- **Output scale:** 1.0 = 5 V peak at the voice output with LEVEL at maximum
  (`kVoltsPerUnit`). Calibrate normal and full-accent peaks to the service-manual check
  table (research/03, research/01, research/02).
- **Parameters** (`VoiceParams`): `tune` in semitones (0 = stock), `decay`, `tone`, `shape`
  normalised 0..1 with the meaning in `kVoiceInfo`. Hardware knobs span the hardware pot
  travel (with the hardware taper); non-hardware knobs are stock at 0.5. `tune` scales
  every pitched element. `Hit::pitchSemis` adds to `tune` for that hit (BD honours it for
  chromatic play; other voices may ignore it).
- **Oversampling** is private to the voice: `VoiceSetup::oversample` (1, 2 or 4) is the
  factor it may run internally, decimating to `sampleRate` before returning. The metal
  voices (CB, CY, OH, CH) need it; band-limit their square oscillators (polyBLEP) as well.
  Free-running oscillators are never phase-reset by a trigger, and keep advancing while
  the voice sleeps: the engine calls `render()` on the metal voices every block even when
  inactive, and an inactive metal voice only advances its oscillator phases and writes zeros.
  Each metal voice owns its own six-oscillator bank instance (so each has its own tune);
  identical seeds and tunes give identical oscillators, which reproduces the shared bank.
- **Unit variance:** component values are scaled with
  `dsp::componentScale(unitSeed, componentId, tolerance, variance)`; `variance = 0` is the
  nominal schematic.
- **Latency:** oversampled voices expose `latencySamples()` (a few samples at most, from the
  decimator). The engine does not compensate it; a 3–6 sample offset between voices is far
  below audibility and keeps the plugin at zero reported latency.
- **Sleep:** `isActive()` goes false once the output has decayed below about −120 dBFS so
  the engine can skip the voice. Do not let a sleeping voice wake with a click.

Each voice file starts with a comment block that states the circuit it models, the
component values used and where they come from, and what is fitted rather than derived.

## 4. Engine and Machine

`Engine` owns the 16 voices, the shared noise source, the mixer and the output stage.
`Machine` owns an `Engine` and a `Sequencer`, merges sequencer hits with MIDI-played
hits, splits each block at event offsets (sample-accurate), and carries the lock-free
queues between the UI and the audio thread. The plugin shell and the CLI renderer both
drive a `Machine`; neither talks to voices or the sequencer directly.

- Buses: 12 stereo outputs — Main plus 11 individual outs. Each voice has an output
  assignment; a voice routed to an individual out leaves the main mix, as on the hardware.
- Per-voice channel: level, pan, drive, mute, solo, output. Mute and solo gate TRIGGERS,
  so a muted voice finishes its tail.
- Main bus only: optional analog output stage (master high-pass near 48 Hz, soft op-amp
  saturation), master compressor, limiter, master volume.
- MIDI: channel-agnostic drum map (GM-compatible), velocity → `Hit::volts`; a chromatic
  bass-drum zone; pattern-select notes in HostNote mode; optional MIDI out of sequencer hits.

## 5. Sequencer semantics

Public API: `seq/Sequencer.h`. Data model: `seq/Pattern.h`.

### 5.1 Clock
- Position is measured in quarter notes (PPQ). Step length comes from the pattern's
  `Scale`; a "measure" is one pass of the pattern (`length` steps).
- `HostSync`: plays while `Transport::playing`; step times are derived from host PPQ on
  every call (never accumulate samples while synced). `Internal`: plays while
  `Settings::run`; PPQ is accumulated internally from tempo, starting at 0 on each start.
  `HostNote`: plays from the top of the given pattern while a pattern note is held,
  using the internal clock. `Off`: silent.
- Tempo: host BPM when `hostValid`, else `internalBpm`. Clamp to 20..400.
- A step fires when its time falls in the half-open range `[start, end)` of the processed
  samples. A start exactly on a step boundary fires that step at offset 0. Use `floor`,
  so negative PPQ (pre-roll) works.
- Relocation: if host PPQ differs from the expected continuation by more than a small
  tolerance, drop pending hits, re-anchor, and do not fire the steps that were skipped.
- When playing stops, drop pending hits and set `currentStep()` to −1.

### 5.2 Measures, variations, chain, fill
- In `HostSync` with no manual pattern changes, playback is a pure function of host PPQ:
  measure index = `floor(ppq / measureLengthQn)`, anchor = that × measure length, and
  playback starts mid-pattern when the transport starts mid-measure.
- `VariationMode::AB` plays A on even measures and B on odd ones; it starts on A.
- A queued pattern (`selectPattern`) takes over at the end of the current measure. When
  stopped it takes over at once. With `immediate`, it takes over now and keeps the step position.
- Chain (`Settings::chain` and `bank.chain.length > 0`): each measure plays the next chain
  entry. On start/relocation in `HostSync`, find the entry by walking the chain from PPQ 0.
- Auto fill-in every N measures: the intro/fill pattern (`kFirstFillSlot + fillSlot` of the
  bank the selected pattern is in) replaces measure N−1 of each group of N.
  `requestFill()` plays it for the next measure, once. `currentPattern()` reports the
  fill while it sounds; `selectedPattern()` keeps reporting the basic pattern.

### 5.3 Steps
- Per-track length (`Pattern::trackLength`, polymeter): a voice track of length L reads
  step `(measure * length + stepInMeasure) mod L`. The accent track follows the pattern.
- Level of a hit: `Weak` → `Hit::weak()`, `Normal` → `Hit::normal()`,
  `Accent` → `Hit::accented(accent)`. A set step on the global accent track raises every
  voice hit on that step by one class (Weak→Normal, Normal→Accent); a step that is already
  Accent becomes `Hit::accented(min(1, accent + 0.25))`.
- Shuffle delays odd steps (0-based index 1, 3, 5…) by
  `shuffle/100 × swingScale × stepLength/3`.
- Sub-steps ×N: N hits spaced `stepLength/N` apart, same level.
- Flam: a grace hit at the step time with 0.6× the volts (not below 1.5 V), then the main
  hit `flamMs` later. With sub-steps, only the first sub-hit flams.
- Probability: decided by a deterministic hash of (measure, step, track), so a render
  repeats exactly.
- Hits that fall after the end of the processed range wait in a fixed-size pending queue.

### 5.4 Recording
`recordHit(voice, accent, sampleOffset)` quantises to the nearest step (ignoring shuffle)
of the playing pattern and variation and sets it to Normal or Accent. If the nearest
step is the upcoming one, the sequencer must not fire that step again on this pass (the
live hit already sounded).

### 5.5 State for the UI
`currentStep()`, `currentPattern()`, `selectedPattern()`, `queuedPattern()`,
`currentVariation()`, `isPlaying()`, `measureCount()`, `tempo()`.

### 5.6 `seq/PatternOps.h` (message-thread helpers, pure functions on `Pattern`)
`clearPattern`, `clearTrack`, `copyTrack`, `shiftTrack(±steps, wraps within the track length)`,
`shiftPattern`, `invertTrack`, `copyVariation(A↔B)`, `randomizeTrack(track, density 0..1, seed)`
and `randomizePattern(densities per track, seed)` (deterministic for a seed; keeps flam and
sub-step bits off; hats get no simultaneous OH+CH), `expandPattern` (double length by
repetition), `setEuclidean(track, hits, rotation)`.

### 5.7 `seq/MidiFile.h`
`std::vector<std::uint8_t> writeMidiFile(const Pattern&, int variation, const std::array<std::uint8_t, kNumVoices>& noteMap, const MidiFileOptions&)`
writes a format-0 standard MIDI file of one measure (channel 10, 480 PPQ, notes of one
step or 1/16 length, velocities weak/normal/accent = 48/96/127, shuffle, flam and
sub-steps rendered as real note positions; tempo meta event optional).
Plus `bool readMidiFile(bytes, noteMap, Pattern& out)` for import (quantise to the pattern scale).

## 6. Threading model (plugin)

- The **message thread** owns the authoritative `PatternBank` (the model the UI edits and
  state save/load reads). The **audio thread** owns the copy inside `Sequencer`.
- UI → audio: a lock-free SPSC queue of POD commands (set pattern by value, set chain,
  select pattern, fill, audition a voice, …). Audio → UI: a queue of small notifications
  (step recorded, pattern selection changed) plus atomics for the play position and
  per-voice hit counters. Nothing else is shared.

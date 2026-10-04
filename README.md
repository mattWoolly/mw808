<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- mw808: classic analog drum machine emulation. Copyright (C) 2026 mwAudio. -->

# mw808

mw808 is a circuit-level emulation of the classic 16-voice analog drum computer from 1980, with a 64-pattern step sequencer, built as a Linux-native VST3, CLAP, LV2 and standalone instrument. Every voice is modelled from the service-manual schematic: the bass drum is a bridged-T resonator at 49.5 Hz with the transistor leak that makes its pitch sigh after a loud hit, the cymbal is six free-running square oscillators through two band-passes and three clipping transistor amplifiers, and accent drives the circuits harder the way the 4–14 V trigger pulse did, so an accented hit changes tone as well as level.

It installs to `~/.vst3`, `~/.clap` and `~/.lv2` and has been validated with pluginval at strictness 10 and with clap-validator 0.4.1 (0 failures).

## The voices

Sixteen voices, the hardware's switch-shared pairs (tom/conga, rim shot/claves, clap/maracas) split out so all of them can play at once. Each has the original panel knobs plus Tune and Decay, so the extended range sits around the stock sound rather than replacing it: 0 semitones and decay 0.5 are the unmodified circuit.

Measured against the service manual's check table (decay to −20 dB, peak-to-peak output at normal and full accent, 44.1 kHz):

| Voice | Pitch (Hz) | Manual | −20 dB (ms) | Manual | Vpp normal / accent | Manual |
|---|---|---|---|---|---|---|
| Bass drum | 49.6 | 49.4 (schematic) | 343 | 300 | 3.50 / 10.0 | 3.5 / 10 |
| Snare | 237.9 + 475.2 | 238 + 476 | 59–63 | 60 | 3.1 / 10.3 | 3 / 10 |
| Low tom | 88.8 | 80–100 | 207 | 200 | 3.27 / 11.5 | 3.5 / 12 |
| Mid conga | 283.4 | 250–310 | 98 | 100 | 2.55 / 8.58 | 3 / 10 |
| Rim shot | 1665 + 478 | 1667 + 455 | 11.0 | 10 | 3.00 / 10.0 | 3 / 10 |
| Claves | 2500.5 | 2500 | 25.4 | 25 | 2.50 / 7.99 | 2.5 / 8 |
| Cymbal (tone 0) | | | 380 / 848 / 1149 | 350 / 800 / 1200 | 3.50 / 7.32 | 3.5 / 7 |
| Open hat | | | 97 / 451 / 584 | 90 / 450 / 600 | 3.50 / 7.07 | 3.5 / 7 |
| Cowbell | 540 + 800 | 540 + 800 | 56 | 50 | 3.62 / 12.5 | 3.5 / 12 |
| Hand clap | | | 104 | 100 | 6.00 / 12.0 | 6 / 12 (accent cell is a misprint) |

These are our own measurements of the model, taken by a reviewer with measurement code written independently of the voice code. Where the model and the manual disagree, the model usually agrees with the CC0 sample set of a real unit (serial 103852) that the tests compare against: that unit's bass drum at full decay rings 1.2 s, not the manual's 800 ms, and so does ours. Fitted values (where a circuit's ideal maths self-oscillates, or a transistor's curve isn't published) are named in the comment block at the top of each voice header in `libs/mw808-core/include/mw808/voices/`.

Things the hardware does that most emulations skip, and this one keeps:

- Retriggering never resets a voice. A new hit lands on whatever is still ringing, so fast rolls build up instead of restarting.
- The metal oscillators free-run whether anything is playing or not, so no two cymbal hits start at the same phase. They are band-limited (polyBLEP) and the metal chains run 2× oversampled at 44.1/48 kHz in Standard quality, 4× in High.
- The closed hat chokes the open hat through the same discharge path as the circuit: a fast fade lasting about as long as the closed hit, not a hard cut.
- Snare, clap and maracas share one noise generator, as they did on the board.
- An optional output stage models the master high-pass near 48 Hz and op-amp saturation.
- Analog Variance spreads component values within their real tolerances (±20 % capacitors, ±5 % resistors) from a stored serial number, so a kit can sound like one particular unit and keep sounding like it.

## The sequencer

Four banks of 16 patterns. Slots 1–12 are basic rhythms and 13–16 are intro/fill patterns, the hardware's memory layout. Each pattern has A and B variations, up to 64 steps, and a step scale of 1/8 triplet, 1/16 triplet, 1/16, 1/32 or 1/8 (the first four are the original pre-scale positions).

Per step: weak, normal or accent, flam, ×2/×3/×4 sub-steps, and probability in eighths. Per pattern: shuffle and a global accent row. Per track: its own loop length, for polymeter. Then A / AB / B variation play, auto fill every 2, 4, 8, 12 or 16 measures (or manual fill), pattern chaining, live recording from MIDI, copy/paste/shift/clear/randomise/Euclidean tools, and drag-out of a pattern as a MIDI file (or drop a `.mid` in).

Timing is computed from the host's position every block rather than by counting samples, so it never drifts. A review oracle that recomputed every hit time independently from the spec found 0 mismatches across 3,000 random scenarios and 861,000 hits, at block sizes from 1 to 4096 samples.

Sequencer modes: **Host Sync** follows the DAW transport (and runs on its own clock when there is no host transport, as in the standalone), **Internal** runs free at its own tempo, **Host Note** plays pattern N while MIDI note 36+N is held, and **Off** leaves the voices to MIDI alone.

## MIDI

Notes play voices on any channel with a General-MIDI-compatible map: BD 36, RS 37, SD 38, CP 39, LT 41, CH 42, MT 45, OH 46, HT 48, CY 49, CB 56, HC 62, MC 63, LC 64, MA 70, CL 75, plus the usual aliases (35, 40, 43, 44, 47, 50, 51, …). Velocity 96 is a normal hit and 127 is a full accent at the current ACCENT setting; below that, level and drive fall off together. Set a Bass Channel and notes on that channel play the bass drum chromatically (±36 semitones from note 36). MIDI Out sends sequencer hits on channel 10. Any parameter can be MIDI-learned from its right-click menu.

## Mixer and outputs

Every voice has level, pan, drive, mute, solo and an output assignment: the main mix or one of 11 stereo individual outputs. A voice sent to its own output leaves the main mix, as on the hardware. Mute and solo stop triggers, so a muted voice finishes its tail. The main bus carries the analog output stage, a one-knob compressor, a soft limiter and master volume; the individual outputs bypass all of it.

## Presets

19 kits, 5 pattern banks (112 named patterns covering electro, hip-hop, trap, Miami bass, house, techno, R&B, Latin and Afro-Cuban clave patterns, and some polymeter experiments) and 40 full presets, all compiled in. Kits, pattern banks and full states save separately as `.mw808` files under `~/.config/mwAudio/mw808/Presets/{Kits,Patterns,Full}/`. A new instance opens on the Init preset, the stock kit playing the first Factory pattern at 120 BPM.

## The interface

Three views over one sequencer strip. PANEL shows the 16 voice strips in hardware order with the global block (tempo, accent, master, sequencer mode, flam time and swing depth); the hardware's own knobs have skirted caps and the extended ones flat caps with an orange pointer. GRID shows all 17 lanes (16 voices plus accent) for the whole pattern. MIXER shows pan, drive, routing and faders. The step keys run red, orange, yellow and off-white in groups of four.

The editor is vector-drawn, resizes from 0.6× to 2.0× and remembers its size. With the sequencer running it redraws at 30 frames per second but touches about 1 % of the window per frame.

## Performance

Release build on this machine (Xeon E5-2699 v4), 48 kHz, Standard quality, a dense worst-case pattern with all 16 voices busy: about 1.5 µs of DSP per sample (7.4 % of one core), and the slowest block measured took 11.3 % of its real-time deadline. Silent voices cost nothing beyond the metal oscillators turning over. The audio thread does no allocation, locking or file access; a test counts every `malloc` and `operator new` during `processBlock` across block sizes 1 to 8192 and requires zero.

The CLI prints the same table for any machine:

```
build/linux-release/tools/mw808-render/mw808-render bench
```

## Building

Requires g++ 13 or newer, CMake 3.25+, Ninja, and the usual JUCE Linux dev packages (ALSA, JACK, FreeType, X11, Xrandr, Xinerama, Xcursor). Dependencies (JUCE 8.0.13, clap-juce-extensions, Catch2 3.7.1) come from `.deps/` when present and are fetched at pinned versions otherwise.

```
cmake --preset linux-release
cmake --build --preset linux-release
ctest --test-dir build/linux-release -j 16
tools/install-user.sh build/linux-release          # --dry-run to preview, --uninstall to remove
```

That test run is 381 tests: per-voice unit tests (pitch, decay and level at 44.1–192 kHz, fuzzing, determinism, comparison with the reference samples where present), the sequencer oracle, 52 bit-exact golden renders, plugin state round-trips and a 30-second multi-threaded stress test, a UI suite that drives every control through real mouse events, and the three format validators. The validators download pinned, checksummed releases on first use and skip (exit 77) when offline.

Golden renders only change on purpose: `BLESS_REASON="why" tools/bless_goldens.sh build/linux-release`.

`mw808-render` renders voices, patterns (from the factory banks or a small text format, `mw808-render help pattern-format`) and per-voice stems to WAV without a host, and exports patterns as MIDI files.

## Layout

```
libs/mw808-core/   JUCE-free C++20 core: voices, sequencer, engine, factory content
plugin/            JUCE shell (src/) and editor (ui/)
tools/             mw808-render, golden blessing, validators, install
tests/             unit, golden, plugin, ui
docs/design/       architecture.md (core contracts), plugin.md (shell and UI contracts)
research/          the circuit and product research behind the numbers above
```

## What it doesn't do yet

- The bass drum's first 50 ms are symmetric; the reference unit's negative lobes are about 15 % taller and narrower, and the mechanism isn't identified yet.
- Pattern switching happens at the end of the measure. The hardware's rule that a pattern picked during beat 1 starts on beat 2 is not modelled, and neither is the ~1.9 ms timing jitter of its interrupt clock.
- In the GRID view, a track with its own loop length shows the pattern's playhead rather than its own (the step keys show the right one).
- LV2 is checked structurally only (bundle, Turtle, exported descriptor); lv2lint isn't installed here. No DAW other than the validators has loaded it yet.

## Licence

AGPL-3.0-or-later (JUCE 8 is AGPLv3 or commercial). Copyright © 2026 mwAudio. The reference recordings the tests compare against are a CC0 sample set of one real unit (source and licence in `research/05-emulation-techniques.md`); they live in `.deps/ref-samples/` for tests only and are not part of the plugin.

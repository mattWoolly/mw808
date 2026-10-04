# 04 — TR-808 Software Emulation Feature Landscape

Purpose: define the feature **superset** mw808 must implement, anchored on D16 Group Nepheton 2 (owner's reference), cross-checked against Roland's own TR-808 plugin and other 808-style products. Facts are cited inline by short tag; full URLs in **Sources** at the bottom. Anything not confirmed from a source is marked **UNVERIFIED**.

---

## 0. Design-critical summary

- **Nepheton 2 = 17 independent voices**: BD, LG (Laser Gun, non-808 extra), SD, RS, CP, LC, MC, HC, LT, MT, HT, CH, OH, CY, CL, MA, CB. On the real 808, pairs (LT/LC, MT/MC, HT/HC, RS/CL, CP/MA) share a selector switch and output; Nepheton makes all 16 + LG independent. Roland's plugin keeps 11 parts with A/B instrument switches (polyphony 11). [N2M p.6] [RC808]
- **Per-voice synth knobs in Nepheton are few** (2–3 per voice, much like the hardware). The depth comes from a **per-voice channel strip** (HP/LP + shared resonance, 2-band para EQ, look-ahead compressor), **2 FX buses × 5 serial slots**, and a **master chain** (3-band compressor + limiter with soft clip). [N2M p.7–28]
- **Nepheton 2 lacks BD tune** (a known criticism). Roland adds BD and CB tune, per-voice GAIN (drive), PAN, and BD/SD COMP. Superset: tune + decay on **every** voice, plus drive/comp. [MT-N2] [RC808] [RC808M]
- **Outputs**: N2 has **16 stereo outs**, routable from instruments, buses, triggers or master, with "skip downstream processing" checkboxes. N1 had 12 outs (stereo or mono). Roland has Main + 11 sub-outs (one per part). [N2M p.30] [N1M p.3] [RC808M p.8]
- **Sequencer (N2)**: 48 patterns (banks A–D × 12), up to **64 steps**, step scale 1/64–1/4 × {full, dotted, triplet}, shuffle, Normal/Accent per step per track, **Global Accent row**, articulations **Flam** and **Sub Step ×2/×3/×4**, Tap record, Randomizer (per-track density plus seed prev/next), copy/paste/shift, Follow, pattern queuing at pattern end, **Host Note mode** (48 MIDI notes trigger the 48 patterns), MIDI out, drag-and-drop MIDI export. [N2M p.33–48]
- **Roland plugin sequencer extras**: 8 variations (A–H) per pattern, **per-instrument last step** (polymeter), **per-instrument shuffle**, weak/normal/accent steps, **9 flam spacings** (0 and 20–48 ms in 4 ms steps), drag out as **MIDI or audio**, 128 patterns + 128 kits per bank. [RC808M p.4–8]
- **External (sound-module) mode velocity**: N2 uses continuous velocity (127 levels). N1 used three levels: 48 = normal, 96 = accent, 127 = accent + total accent. N1 also mapped those values to MIDI out. [N2M p.31] [N1M p.5]
- **Default drum map (Nepheton, GM-based, with duplicates)**: BD 35/36, RS 37, SD 38/40, CP 39, LT 41/45, CH 42/44, HT 43/50, OH 46, MT 47/48, CY 49/51/52/59, CB 56, LG 58, HC 60/62, LC 61/64, MC 63, MA 70, CL 75. [D16KB41]
- **Platform gap**: none of the major commercial 808 emulations (Nepheton 2, Roland TR-808, ADM) ships a Linux build. Nepheton 2 is Win VST2/VST3/AAX and Mac AU/VST2/VST3/AAX. A Linux-native VST3/CLAP 808 is an open niche. [N2P] [ADM-KVR]

---

## 1. D16 Group Nepheton 2: full feature inventory

Primary source: *Nepheton 2 User Manual* (dated 7 March 2024), plus the product page. Price €119; formats VST2/VST3/AU/AAX; Windows 7+ (32/64-bit), macOS 10.13+ (64-bit only); sample rate ≥ 44.1 kHz; current build 2.0.3. [N2P] [N2M p.2]

### 1.1 Instruments and per-voice synth controls

Every voice has **Level**, applied after its effect strip. Voice-specific controls (manual wording kept, including D16's spelling "Tunning"): [N2M p.6–7]

| Voice | Controls (Nepheton 2) | On original 808 panel? [TR808OM p.2–4] |
|---|---|---|
| Bass Drum | Tone (volume of initial click), Decay (amp release), **Sweep** (release time of frequency envelope) | Level, Tone, Decay. **Sweep is an addition** |
| Snare Drum | Tone (crossfade between one and two audible oscillators), Decay (**noise** amp release), Snappy (crossfade noisy vs muddy) | Level, Tone, Snappy. **Decay is an addition** |
| Low/Mid/High Conga | Tuning (base freq), Decay | Level, Tuning (shared with Tom via switch). **Decay is an addition** |
| Low/Mid/High Tom | Tuning, Decay, **Snappy** (crossfade dirty vs clean) | Level, Tuning. **Decay and Snappy are additions** |
| Claves | Tuning, Decay | Level only. **Both are additions** |
| Rim Shot | Tuning, Decay | Level only. **Both are additions** |
| Maracas | Tone (noise colour) | Level only. **Addition** |
| Hand Clap | Tone (filter cutoff), **Reverb** (reverb decay time, i.e. the clap tail) | Level only. **Both are additions** |
| Cow Bell | Tuning, Decay | Level only. **Both are additions** |
| Laser Gun | Depth (freq-env modulation), Decay, Sweep (freq-env release) | **Not an 808 voice.** D16 extra |
| Cymbal | Tone (attack), Decay, **HH Frq** (base freq of the square oscillators feeding the metallic noise) | Level, Tone, Decay. **HH Frq is an addition** |
| Open / Closed Hihat | Tone (noise colour), Decay, HH Frq | OH: Level, Decay. CH: Level only. **Additions** |

Notes:
- **HH Frq exists on CY, OH and CH**, which implies the six-square-oscillator metal source can be retuned. It is unclear whether the three voices share one oscillator bank (as on the 808) or each has its own copy. **UNVERIFIED**.
- The 808's **Accent** channel is not an instrument here. Accent is handled entirely by sequencer velocity (§1.9).
- MusicTech: "missed a trick by not letting you freely tune the kick drum or some of the other sounds." [MT-N2]
- The product page also lists "**Audible Trigger Outputs**" and the trigger modes "MIDI notes, MIDI CC or GUI". The manual does not document CC triggering. **UNVERIFIED** how CC triggering works. [N2P]

### 1.2 Mute / Solo / Activity
- Mute/Solo LED per instrument. Muting **blocks new triggers but lets the current envelope finish**, so muting never cuts a sound off. [N2M p.8] The same behaviour is documented for N1. [N1M p.3]
- A per-instrument **activity LED** flashes on trigger. [N2M p.8]

### 1.3 Per-instrument channel strip ("Strips" view) [N2M p.8–14]
Signal order: synth → **Filter/EQ** → **Compressor** (order swappable via "Pre Eq") → Audio Out / Bus Send.
- **Filter/EQ** (bypass LED): Hi Cut, Low Cut, one **shared Resonance**; 2 parametric bands, each with Freq, Width (**4 to 0.5 octaves**), Gain (**±24 dB**) and its own enable.
- **Compressor** (bypass LED): Look-ahead **Off / 1 ms / 5 ms**; Pre Eq toggle; Threshold **0 to −48 dB**; Ratio **1:1 to 1:20**; Attack **1–500 ms**; Release **1–500 ms**.
- **Audio Out**: Route To {Master, Bus 1, Bus 2}; **Bus Slot 1–5** (the entry point into the bus's serial chain); **St. Spread** (L/R inter-channel delay); Panning; Level.
- **Bus Send**: Route To {Bus 1, Bus 2}; Bus Slot; St. Spread; Panning; **Pre Lev** (send independent of Level); Amount.
- Each strip has **instrument presets** (synth + strip). Filter/EQ and Comp sections have **copy/paste**.

### 1.4 FX buses [N2M p.15–26]
- **2 buses × 5 serial slots**. Drag tabs to reorder; effects can be swapped between buses; each bus has an output Level. A "More" button reveals full parameters.
- **Bus 1 Setup**: Bus 1 can split to Bus 2 (Amount, target slot, Pre Level).
- 10 slots in total, filled from **8 algorithms**. EQ and Dynamics get two instances each:
  - **Distortion**: diode-clipper style. Single-knob pre-compression ("Dynamics") placed before the clip or before the split; optional 2-way crossover (Split Freq, Gap in ±octaves) so only the high band is clipped; Preamp, Threshold, Knee; Low Out, Clip Out, FX dry/wet.
  - **Parametric EQ** (4-band): Low/High bands each switch between Cut and Shelf (Res/Width, Freq, Gain); 2 bells (Freq, Gain, Width).
  - **Delay**: Rate 1–1000 ms, or synced to 1/32, 1/16, 1/8, 1/4 × {Full, Dot, Tri}; Feedback; Spread; filter in the feedback loop {Off, LP, BP, HP} with Cutoff and Reso; FX.
  - **Reverb**: algorithmic. Pre-delay; Early section (Size, Diffusion, Color, Modulate); Late section (Size, Diffusion, Color, Feedback, Modulate); ER/LT mix; FX.
  - **Chorus**: dual-line. Offset, Depth, Feedback, LFO Rate, Spatial, Lo/Hi Cut, Fatness, Gain, FX.
  - **Bitcrusher**: Dither, Bits; anti-alias "Approximation filter" (offset relative to Nyquist); Resampler **44 Hz–44.1 kHz** with Jitter; Images filter; FX.
  - **Dynamics**: Comp mode (look-ahead Off/1/5 ms, Threshold 0 to −48 dB, Ratio 1:1–1:20, **sidechain from any other Nepheton instrument**, Attack/Hold/Release 1–500 ms, Make Up, FX) or **VCA mode**: an AHR envelope triggered by up to 3 chosen instruments, with Modulation ±, i.e. ducking or gating, and Gain.
  - **Filter**: {Off, LP, BP, HP}, Cutoff, Reso. Modulation from an LFO (0.01–20 Hz or synced Full/Dot/Tri, waveform select, stereo spread) or from an AHR envelope triggered by up to 3 instruments.

### 1.5 Master chain [N2M p.26–28]
- **3-band compressor**: per band Enable, Solo, Attack/Release 1–500 ms, Threshold 0 to −48 dB, Ratio 1:1–1:20, Make Up, GR meter. Two crossover knobs; look-ahead Off/1/5 ms.
- **Limiter**: Attack, Release (1–500 ms), Threshold, Output volume (also exposed outside the Master view), and a **Soft Clip** option (diode-like soft clipper on peaks above 0 dB).
- The product page also mentions "Master Bus Presets". [N2P]

### 1.6 Trigger Outs ("audible trigger") [N2M p.29]
Emulates the click that the hardware trigger pulse makes. **Up to 3** trigger sounds, each with Enable, Source instrument, Route To {Master, Bus 1, Bus 2}, Bus Slot, Pan, Level. Off by default.

### 1.7 Plugin audio outputs [N2M p.30–31]
- **16 stereo outputs.** By default only Master → Out 1 is active.
- Options → Audio Outputs → Edit Setup: any output can carry an instrument, a trigger, a bus or the master. A per-assignment checkbox picks whether the signal still flows downstream (buses and master) or is **taken off for that output only**.
- Output setups can be saved and loaded as files.

### 1.8 Play modes [N2M p.31–33]
- **Internal** (Int Seq on): the internal sequencer plays.
- **External** (Int Seq off): sound module driven by MIDI notes. The default map "follows the General MIDI standard". Velocity is continuous ("127 levels"), with no accent/normal quantisation. Only the sound section is active.
- **Drum MIDI Map editor**: choose an instrument, then click keys on an on-screen keyboard (several notes per instrument allowed) or MIDI-learn from a keyboard. The range is **4 octaves starting at the 3rd octave**. The map can be restored to factory, loaded, or saved.

### 1.9 Internal sequencer [N2M p.33–48]
**Storage and selection**
- **48 patterns = 4 banks (A–D) × 12** pattern buttons. Default pattern A-1.
- **Trigger modes**, both using the same 48-pattern storage:
  - **Live**: pick patterns in the GUI. A newly selected pattern waits until **the current pattern finishes its last step**; the queued pattern shows a blue LED.
  - **Host Note**: each of 48 MIDI notes (from C of the 2nd-lowest octave, spanning 4 octaves) plays one pattern while the note is held. Note-off stops it. Host transport Start is ignored.
- **Host Link Active**: off = sync to the host position once at start, then free-run on the internal clock; on = re-align to the host position continuously (follows DAW loops and jumps).
- **Follow**: the edited pattern follows the playing pattern. With Follow off, blue = being edited and red = playing.
- **MIDI Output** works in both Live and Host Note modes. Velocity comes from the Normal/Accent/Global-Accent values. External mode passes MIDI through. MIDI Out can be disabled to avoid loopbacks.

**Grid editing**
- One row per instrument (17), plus a **Global Accent** row (on/off only).
- Step types: **Normal** (red) and **Accented** (brown). Ctrl-drag draws the other type.
- Per-pattern velocity values: **Base** and **Accent** (as MIDI velocities), plus **Gl. Acc.** boost, which is added on top of Base or Accent. Accent and Global Accent stack.
- **Articulations** per step: None, **Flam**, **Sub Step ×2, ×3, ×4**. Right-click cycles through them.
  - Flam (per-pattern settings): **Delay** as a fraction of step length (100% = half a step), and grace-note **Volume** as % of the main note.
  - Sub Step (per-pattern settings): **Start** and **End** volume as % of step volume, linearly interpolated across the repeats. This gives a velocity ramp for rolls.
- **Pattern properties**: **Scale** = step note value 1/64 to 1/4 × {Full, Dotted, Triplet}; **Length up to 64 steps**; **Shuffle** amount. Length and shuffle are per pattern; there is **no per-track length**.
- **Edit functions**: select tracks by clicking names (Ctrl for multi-select). Copy, paste and clear tracks; shift selected tracks left/right; shift the whole pattern left/right; copy, paste and clear the whole pattern.
- **Pattern browser**: load into a slot or drag patterns into slots (several at once). Save. **Export** by dragging out as a **MIDI clip** or in native pattern format (works between instances).
- **Randomizer** (Random tab): pick tracks (none picked = all), set a per-track **density** knob, choose modifiers (accented/non-accented/both steps, articulations), then **Next** draws a new seed and **Prev** goes back through earlier seeds. The grid stays editable.
- **Tap mode**: records only while the sequencer runs. The upper half of a pad records an accent and the lower half a normal step. Recording from MIDI uses the drum map; **velocity > 64 → Accent**, otherwise Normal.

### 1.10 MIDI, automation, options [N2M p.49–54]
- **MIDI learn** from any parameter's right-click menu (Learn / Clear). CC maps can be saved and loaded.
- **Host automation is opt-in per parameter**. D16 says "thousands of sound parameters" is more than hosts handle, so parameters are enabled from the context menu. Automation templates can be saved and loaded. A **MIDI/Automation Summary** window lists the assignments.
- Right-click shows a parameter's value (no text entry). Ctrl/Cmd-drag gives fine adjustment. Double-click resets to the preset/default value.
- **GUI**: several skin sizes, a **System Scale** factor and HiDPI support. [N2M p.53] [N2P]
- **Quality**: separate **Real-time** and **Offline** quality settings (a higher setting costs more CPU). The levels are not documented; presumably oversampling (**UNVERIFIED**). MusicTech notes that CPU "can get a touch high" at full quality with all effects. [N2M p.53] [MT-N2]
- **Default State**: save the current state as the default for new instances, or restore factory. [N2M p.54]

### 1.11 Preset system [N2M p.55–72]
Hierarchy:
- **Scene**: all sound parameters, all 48 patterns and the Int Seq state.
  - **Drum Kit**: all sound parameters (17 voices, their strips, both buses, master).
    - **Instrument Preset**: synth + strip for one voice.
    - **Master Preset**: multiband compressor + limiter.
  - **Pattern**: one pattern with scale, length and shuffle.

Browser: Factory (read-only) and User sources. Tag **Groups** filter in cascade, with an Any/All operator per group. Search by name. **Favourites** are stored globally; **Pins** are stored per project. The info pane shows Author and Description. Presets support rename, delete and multi-select; users can create their own tags and groups; presets import and export by drag-and-drop. The factory library has more than **1200 presets and patterns**. [N2P]

### 1.12 Nepheton 1 (for comparison; some ideas worth keeping) [N1M]
- 17 voices, the same set as N2 including **Laser Gun**, with nearly the same knobs (N1 has no HH Frq on Cymbal).
- **12 outputs, stereo or mono** (switchable); output assigned by dragging an LED; a default assignment can be saved.
- **Native mode**: 16 patterns = **12 Regular + 4 Fill-In**. Each pattern has **4 parts A1, A2, B1, B2** of 0–16 steps (A1 and B1 must be at least 1 step). **Basic Variation A / B / AB**, **I/F Variation**, and **Measures auto fill-in** {Manual, 2, 4, 8, 12, 16}: with 16, the fill plays every 16th bar. **Tap** queues a manual fill. This is a faithful copy of the 808 workflow (§5).
- **Host mode**: **96 simple patterns** (≤16 steps) triggered by MIDI notes C-1 to B-8. A "Note Off Mode" option controls whether note-off stops the pattern.
- **Tempo scale**: 4 options of 8, 4, 6 or 3 steps per quarter note, matching the 808 pre-scale. Shuffle is stored per part letter.
- **Int.Sync**: an independent internal tempo per pattern, or host tempo.
- **Velocity convention** (External in and MIDI out): **48 = normal, 96 = accent, 127 = accent + total accent**. Accent "also causes subtle changes to the sound character", not just level.
- **Host play detection** fallback: start on transport change, or start on the "lowest C note".
- Randomizer with a per-instrument intensity. Undo. XML pattern/preset/map files. Fixed bank of 128 presets.

---

## 2. Nepheton MIDI note map and outputs

### 2.1 Default External-mode drum map (Nepheton, D16 helpdesk) [D16KB41]
D16 note names use C-2 = MIDI 36; octave labels differ between hosts. Several notes per instrument are intentional ("easier to find… drum roll").

| MIDI | D16 name | Voice |   | MIDI | D16 name | Voice |
|---:|---|---|---|---:|---|---|
| 35 | B-1 | BD | | 50 | D-3 | HT |
| 36 | C-2 | BD | | 51 | D#3 | CY |
| 37 | C#2 | RS | | 52 | E-3 | CY |
| 38 | D-2 | SD | | 56 | G#3 | CB |
| 39 | D#2 | CP | | 58 | A#3 | **LG** |
| 40 | E-2 | SD | | 59 | B-3 | CY |
| 41 | F-2 | LT | | 60 | C-4 | HC |
| 42 | F#2 | CH | | 61 | C#4 | LC |
| 43 | G-2 | HT | | 62 | D-4 | HC |
| 44 | G#2 | CH | | 63 | D#4 | MC |
| 45 | A-2 | LT | | 64 | E-4 | LC |
| 46 | A#2 | OH | | 70 | A#4 | MA |
| 47 | B-2 | MT | | 75 | D#5 | CL |
| 48 | C-3 | MT | | | | |
| 49 | C#3 | CY | | | | |

- The helpdesk article is titled "Nepheton 1 External Mode – Drum Map". The N2 manual says only that the default map is "General MIDI based" and can be restored to factory. **That N2's factory map is identical is UNVERIFIED**, but very likely.
- Note: **HT 43/50 and LT 41/45 follow GM numbering**, where 43 is "High Floor Tom", so HT does not sit above MT in pitch order. Do not "fix" this if Nepheton compatibility is the goal.
- For comparison, **Roland TR-808 plugin**: 24–31 = variation select A–H; 35/36 BD; 37 RS/CL; 38/40 SD; 39 CP/MA; 41/43 LT/LC; 42/44 CH; 45/47 MT/MC; 46 OH; 48/50 HT/HC; 49 CY; 51 CB. Roland puts CB on 51 (GM ride), whereas Nepheton uses GM cowbell 56. [RC808M p.8]

### 2.2 Output counts
| Product | Audio outputs |
|---|---|
| Nepheton 2 | **16 stereo**, freely assignable (instrument, trigger, bus or master), with a per-assignment "bypass downstream" option [N2M p.30] |
| Nepheton 1 | 12, stereo or mono [N1M p.3] |
| Roland TR-808 plugin | Main mix + Sub 1–11 (BD, SD, LT/LC, MT/MC, HT/HC, RS/CL, CP/MA, CH, OH, CY, CB) [RC808M p.8] |
| AudioRealism ADM | 11 individual unprocessed + 2 master processed (multi-out on VST only) [ADM-KVR] |
| Original TR-808 | Master Hi/Lo + 11 multi-outs (CH, OH, CY, CB, CP/MA, RS/CL, HT/HC, MT/MC, LT/LC, SD, BD) + 3 trigger outs (CB, CP, AC) [TR808OM p.2] |

---

## 3. Survey of other products

### 3.1 Roland TR-808 Software Rhythm Composer (Roland Cloud, ACB) [RC808] [RC808M]
- ACB circuit modelling. **11 parts** with A/B switches (LT/LC, MT/MC, HT/HC, RS/CL, CP/MA). Max polyphony 11.
- Panel knobs as on the hardware, plus a **PANEL** button that switches to **GAIN** ("overdrive the internal circuitry"), **PAN** and **TUNE** per instrument. The product page says "tunable bass drum and cowbell". The fixed CC map exposes TUNE for BD, LT, MT, HT, RS, CP, CH, OH and CB, DECAY for most voices, and **BD COMP / SD COMP**. Which voices actually respond to TUNE is unclear from the manual (**UNVERIFIED**).
- **Fixed CC map** (receive): 9 Shuffle; 20–24 BD Tune/Tone/Comp/Decay/Level; 25–29 SD Tone/Snappy/Comp/Decay/Level; 46–48 LT; 49–51 MT; 52–54 HT; 55–57 RS; 58–60 CP; 61–63 CH Tune/Decay/Level; 71 Total Accent; 80–82 OH; 83–85 CY Tone/Decay/Level; 86–88 CB Tune/Decay/Level. MIDI learn is also available, one CC per control.
- **Memory**: a bank holds 128 patterns + 128 kits; the number of banks is unlimited; banks are files.
- **Pattern** = 8 **variations A–H** × 16 steps. **Per-instrument LAST STEP** gives polymeter. **Per-instrument shuffle** slider is multiplied by the global SHUFFLE knob. A **TOTAL ACCENT** row has an ACCENT LEVEL knob.
- Step articulations: **WEAK** (soft hit), **FLAM** (Type 1 = 0 ms, Types 2–9 = 20–48 ms in 4 ms steps), **SUB STEP** (duplets, triplets, quadruplets). Weak hits can be combined with flam or sub step. A SCALE button selects the step scale (values **UNVERIFIED**).
- Variation copy/paste/clear. Per-instrument copy, paste, clear, **INVERT** and **ALL**. Mute/solo per lane.
- **Drag variation to the DAW as MIDI or audio** (selected in options). **Position Lock to DAW** on/off. Layout Original/Compact, Zoom, "Optimize for Lower CPU Usage".
- Integrates with TR-8S/TR-08 hardware. Win/Mac only.

### 3.2 Roland TR-08 (Boutique hardware, ACB) [TR08]
- Adds "Compressor, Gain, Tune, and Pan for selected instruments". Decay on BD, CY, OH. Accent level knob.
- Sequencer: "16 steps with 16 sub-steps", plus 12×16 patterns and 4×16 intro/fill (16 banks). Beats keep playing while switching write modes.
- **10 separate outputs via USB audio**. **Track-selectable trigger out**.

### 3.3 Roland TR-8S (hardware) [TR8S]
- 8 variations (A–H) × 16 steps. **Sub-steps** of 2/3/4 per step. Flams, accents, velocity. **LAST STEP per instrument or pattern**. **Per-step probability %**. **Motion recording** of knob moves. 2 custom fills per pattern, plus **Scatter** and **Auto Fill In**.
- Per channel: **TUNE, DECAY and an assignable CTRL** knob. The 808 ACB bass drum has an **Attack** parameter. INST FX (filter, distortion, compression). Reverb and Delay sends per instrument. Sample import. Several separate outputs. [TR8S] [RID-TR8S]

### 3.4 AudioRealism ADM (Drum Machine) [ADM-KVR] [ADM-RK]
- Covers 808, 909 and 606: **25 modelled drum generators + 4 PCM players = 34 sounds**. Polyphony is 11 parts.
- **32-step** sequencer, **128 patterns**, **4 prescales per pattern**, **flam per step**, **shuffle per pattern**, **global accent per step**.
- **Pattern Controlled FX (PCF)**: a per-step value for any parameter (cutoff, Q, snappy, "mangle" distortion, FX routing). This is the equivalent of Elektron-style parameter locks.
- Pattern transforms: expand, shrink, swap, duplicate steps, randomize pattern or part, cut/copy/paste, undo.
- MIDI CC learn. **11 unprocessed outs + 2 master**. User WAV/AIFF samples in parts. **ReBirth pattern import and TR-909 SysEx import**. VST/VST3/AU, Win/macOS. About €95.

### 3.5 Wave Alchemy Revolution (Kontakt, sample-based) and Triaz
- **Revolution**: sampled 808, 909, 606, 707, 727 and more (multisampled, not synthesised). **32-step** sequencer with **per-step modulation routings ("step locks")**; classic TR-style 16-button sequencer with accent, host sync, swing and MIDI in; a "Character" tape-saturation knob; filters, EQ, compressors and transient shapers per voice; 160 reverb IRs. [WA-REV]
- **Triaz** (native VST/AU/AAX, sample-based): 3 layers per voice blended with an XY pad. The sequencer has **polyrhythmic per-lane length, rate and swing**, **probability**, "**slop**" humanisation, a **pitch sequencer** and a **motion (parameter) sequencer**. [WA-TRIAZ]

### 3.6 Softube Heartbeat (808/909-inspired, not a clone) [SB-HB]
- 8 instruments (2 kicks, one of them 808-like; 2 snares; CH/OH; 2 percussion; cymbal). Modelled synthesis, with sampled waveforms mixed into kick, snare and hats.
- Per-channel mixer with reverb and filter-delay aux sends, Ping/Pong auto-pan, pan, mute/solo and a one-knob EQ. Master section has Dyna-mite dynamics, EQ, Width, valve saturation and a mono-ising filter.
- **No step sequencer.** The "Auto Layer Machine" handles layering and fills.

### 3.7 Tactile Sounds TS-808 (free, Windows) [TS808-ST] [TS808-HHM]
- Built in SynthEdit. Voices come from **SPICE simulations of the 808 circuits** plus spectral analysis of samples.
- Controls: tune, tone, decay, snap, reverb (clap), velocity, level, pan, output selection. "Most of the voices have more parameters and a greater range than their hardware predecessor." All parameters are automatable.
- No internal sequencer (**UNVERIFIED**). 32-bit era, Windows only.

### 3.8 Synsonic BD-808 / BD-808 Pro (kick only) [SYN-SA17] [SYN-SA18]
- Free BD-808, built from a "detailed analysis of the original 808 Bass Drum circuit". Adds **Tune + Fine tune**, **extended decay range**, **MIDI-synced tune** (playable chromatically as a bass synth) and accents.
- Pro (€10): envelope **Compression**, **Drive** (with attack-phase control), **Glide** {Up, Down, Up/Down}, and velocity → Accent/Decay/Comp/Drive/Tone.
- **This is the "trap 808" feature set: a chromatic, gliding long-decay BD.**

### 3.9 SC-808 (SuperCollider) [SC808-GN] [SC808-SA]
- A synthesised emulation ("emulates the sound (not strict modelling) based on the original circuit"). Runs on SuperCollider, so it works on **Linux**. MIDI note and CC control, live-coding friendly, source can be edited.
- The "Advanced" tier adds separate outputs, filters (Moog, 303, HPF), **retunable cymbal, hi-hat and cowbell**, **long decays** and **adjustable decay on toms and congas**.
- The licence of the free version is **UNVERIFIED**. It is described as open source and freely editable.

### 3.10 iO-808 (web) [IO808]
- React/Redux + Web Audio. All sounds synthesised, based on SOS *Synth Secrets* and the 808 block diagrams. MIT licence according to the ecosystem summary; the README excerpt fetched did not state a licence (**UNVERIFIED**).
- Recreates the panel; the README lists **PLAY and COMPOSE modes, PRE-SCALE and TAP as unimplemented**.

### 3.11 Behringer RD-8 MkII (hardware analogue clone; sets expectations) [RD8]
- 16 sounds. **64-step** sequencer with **poly-meter**, step/note repeat and mute/solo per track. **Wave Designer** (attack/sustain shaper) per part. Dual-mode LP/HP **filter** whose cutoff movement can be recorded.
- 11 analogue outputs. 256 patterns and 16 songs.

### 3.12 Others (brief)
- **Arturia Spark / Spark 2**: multi-machine drum plugin that includes "analogue recreations in the spirit of the TR-808". Not 808-specific. [ART]
- **Cherry Audio**: **no 808 product found**. Its vintage drum emulations are the CR-78 and the KR-55C (Korg KR-55A/B). [CA]
- **Ableton DS Kick** (synth kick, M4L): Pitch (Hz), Drive, OT (overtones), Attack, Click switch, Decay, Env (pitch-mod amount). A useful reference for "808-style kick" knobs. Logic and Ableton 808 kits are sample-based and skipped. [ABL]
- **Open-source DSP references** (techniques only; do not copy code):
  - **Mutable Instruments Plaits** (MIT DSP code). "Analog bass drum = a bridged T-network excited by a nicely shaped pulse" (with attack sharpness/overdrive, brightness and decay). Snare = several bridged-T modes + BP-filtered noise. Hi-hat = "a bunch of square oscillators… mixed with clocked noise, sent to a HPF, then to a VCA". [PLAITS]
  - **Geonkick** (GPLv3, Linux, LV2/VST3/JACK): general percussion synth with up to 16 instruments and multi-out. Not an 808 clone. [GEON]
  - **OneTrick CRYPTID** (GPLv3, VST3/CLAP): FM drum synth "in the fearsome frame of an 808". [OT]
  - **ChowKick** (Chowdhury DSP): physically inspired kick synth. Licence not checked here. [CHOW]
  - No maintained open-source, full TR-808 **plugin** (VST3/CLAP/LV2) with a sequencer was found. **This is a gap mw808 can fill.**

---

## 4. Feature matrix

Legend: **Y** = yes; **P** = partial/limited; **N** = no; **?** = not confirmed. "N2" = Nepheton 2, "N1" = Nepheton 1, "R808" = Roland TR-808 plugin, "8S" = TR-8S (hardware), "08" = TR-08, "ADM" = AudioRealism, "REV" = WA Revolution, "HB" = Heartbeat, "TS" = TS-808, "BD" = Synsonic BD-808 Pro, "SC" = SC-808 Advanced, "RD8" = Behringer RD-8 MkII.

### 4.1 Sound engine and mixing

| Feature | N2 | N1 | R808 | 08 | 8S | ADM | REV | HB | TS | BD | SC | RD8 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Synthesised (not samples) | Y | Y | Y (ACB) | Y | Y/samples | Y + PCM | N | mostly | Y | Y | Y | analogue |
| All 16 808 voices | Y | Y | Y (11 parts) | Y | Y | Y | Y | N | Y? | BD only | Y | Y |
| Pairs independent (16 simultaneous) | Y | Y | N (A/B switch) | ? | ? | ? | ? | – | ? | – | ? | ? |
| Extra non-808 voice(s) | LG | LG | N | N | Y | 909/606 | Y | Y | ? | N | N | N |
| BD tune | **N** | N | Y | Y | Y | ? | Y | Y | Y | Y | Y | ? |
| Tune on most voices | P (toms, congas, RS, CL, CB, HH Frq) | P | Y (CC map) | P | Y | ? | Y | Y | Y | – | Y | ? |
| Decay on most voices | Y | Y | Y | P | Y | ? | Y | Y | Y | Y | Y | ? |
| BD pitch-sweep control | Y (Sweep) | Y | ? | ? | ? | ? | ? | Y | ? | Y | ? | ? |
| BD attack/click control | Y (Tone) | Y | Y (Tone) | ? | Y (Attack) | ? | ? | Y | ? | Y | ? | ? |
| Chromatic BD / glide | N | N | N | N | N | N | ? | N | N | **Y** | ? | N |
| Per-voice drive/gain | via bus dist. | N | Y (GAIN) | Y | Y | Y (mangle) | Y | P | N | Y | N | N |
| Per-voice compressor | Y | N | BD/SD | Y | Y (INST FX) | ? | Y | N | N | Y | N | N |
| Per-voice filter/EQ | Y | N | N | N | Y | Y | Y | P | N | N | Y | master filter |
| Per-voice pan | Y | N | Y | Y | Y | ? | Y | Y | Y | – | ? | N |
| Transient shaper | via VCA mode | N | N | N | ? | N | Y | N | N | N | N | **Y** |
| Send FX (reverb/delay) | Y (2 buses) | N | N | N | Y | Y | Y | Y | N | N | N | N |
| Master dynamics/limiter | Y (3-band + lim) | N | N | N | Y | Y | Y | Y | N | N | N | N |
| Audible trigger-click emulation | Y (3) | Y (1) | N | N | N | N | N | N | N | N | N | N |
| Multi-out count | 16 st | 12 | 12 | 10 (USB) | several | 13 | Kontakt | ? | Y | 1 | Y | 11 |
| Quality / oversampling setting | Y (RT/offline) | N | "Lower CPU" | – | – | ? | – | ? | ? | ? | – | – |
| Mute (lets tail ring) / Solo | Y | Y | Y | Y | Y | ? | Y | Y | ? | – | ? | Y |
| Linux native | **N** | N | N | – | – | N | N | N | N | N | **Y** | – |

### 4.2 Sequencer, MIDI and workflow

| Feature | N2 | N1 | R808 | 8S | ADM | REV | Triaz | RD8 |
|---|---|---|---|---|---|---|---|---|
| Internal sequencer | Y | Y | Y | Y | Y | Y | Y | Y |
| Max steps per pattern | 64 | 4×16 parts | 8×16 var. | 8×16 | 32 | 32 | ? | 64 |
| Pattern count | 48 (A–D×12) | 16 + 96 | 128/bank, ∞ banks | ? | 128 | ? | ? | 256 |
| 808-style A/B variation + I/F fills + auto-fill | N | **Y** | P (A–H) | Y (fills) | ? | ? | ? | ? |
| Step scale / prescale | Y (1/64–1/4 ×F/D/T) | Y (4) | Y | ? | Y (4) | ? | Y | ? |
| Shuffle/swing | per pattern | per part | global × per-instr | Y | per pattern | Y | per lane | ? |
| Per-track length (polymeter) | **N** | N | **Y** | Y | ? | ? | Y | Y |
| Per-step accent per track | Y | Y | N (weak/normal) | Y | N | Y | ? | ? |
| Global (total) accent row | Y | Y | Y | Y | Y | ? | ? | Y |
| Weak/ghost step | N | N | Y | velocity | N | ? | ? | ? |
| Flam | Y (delay %, vol %) | N | Y (9 types, ms) | Y | Y | ? | ? | ? |
| Ratchet/sub-steps | ×2/×3/×4 + vel ramp | N | ×2/×3/×4 | ×2/×3/×4 | ? | ? | ? | step repeat |
| Per-step probability | N | N | N | **Y** | N | ? | **Y** | ? |
| Per-step parameter locks | N | N | N | motion rec | **Y (PCF)** | **Y** | **Y** | cutoff rec |
| Randomizer | Y (density, seed history) | Y | N | Scatter | Y | ? | ? | ? |
| Tap / real-time record | Y (GUI + MIDI, vel>64=acc) | Y (quantised) | ? | Y | ? | ? | ? | Y |
| Copy/paste/clear/shift | Y | Y (cyclic shift) | Y + invert/all | Y | Y + expand/shrink | ? | ? | ? |
| Pattern switch at pattern end (queue) | Y | Y | ? | Y | ? | ? | ? | Y |
| Pattern select via MIDI note | Y (Host Note, 48) | Y (96) | Y (var. A–H, 24–31) | ? | ? | ? | ? | ? |
| Host sync modes | once vs continuous | host vs internal tempo | Position Lock on/off | – | ? | Y | Y | – |
| MIDI out of sequence | Y | Y | ? | Y | ? | ? | ? | Y |
| Drag pattern → DAW | MIDI | N | **MIDI or audio** | – | ? | ? | ? | – |
| External mode (MIDI → voices) | Y (continuous vel) | Y (48/96/127) | Y | Y | Y | Y | Y | Y |
| Editable note map | Y (GUI + learn) | file only | N (fixed) | ? | ? | ? | ? | ? |
| MIDI CC learn | Y (+ map files) | Y | Y (+ fixed map) | – | Y | Y | ? | – |
| Hierarchical presets (scene/kit/voice/pattern) | Y | P | patterns + kits | Y | ? | ? | ? | – |
| Tag-based browser | Y | N | N | – | N | ? | ? | – |
| GUI scaling / HiDPI | Y | N | Zoom + Compact | – | ? | ? | ? | – |
| Import foreign patterns | N | N | N | N | **ReBirth/909 SysEx** | ? | ? | ? |

---

## 5. Original TR-808 behaviour to reproduce (for faithfulness) [TR808OM p.1–4]

- 11 voice sections plus **Accent**, 32 rhythm patterns. Each rhythm has 2 "Modes" (A and B), each one measure of up to 16 steps. A 1st part and 2nd part combine to allow up to 32 evenly spaced steps per measure and odd time signatures.
- Beats can be divided into **3, 4, 6 or 8** steps via **PRE-SCALE**. Factory patterns use pre-scale 3 for 16 steps in 4/4.
- **Mode selector**: PATTERN CLEAR, 1st PART, 2nd PART, MANUAL PLAY, PLAY, COMPOSE.
- **Instrument/Track selector** (12 positions): AC, BD, SD, LT/LC, MT/MC, HT/HC, RS/CL, CP/MA, CB, CY, OH, CH. In Play/Compose it selects one of **12 Rhythm Tracks** (songs) of **64 measures each**, **768 measures** in total.
- **Step buttons**: **Basic Rhythm 1–12** and **Intro/Fill-In 1–4**.
- **Basic Variation** switch: A / AB / B.
- **I/F Variation** switch, plus **Auto Fill In** {Manual, 16, 12, 8, 4, 2}: the fill plays every Nth measure. **TAP** inserts a fill manually.
- **Tempo**: 40 click-stop coarse positions plus a Fine knob.
- Accent LEVEL "sets the amount of emphasis given to the other voices". There is **only one global accent track** (no per-instrument accent on the original).
- **Rear panel**: Master Out Hi/Lo; 11 Multi Outs; **Trigger Out CB, CP, AC**; footswitch Start/Stop and Fill-In; DIN SYNC in/out.
- An "authentic mode" in mw808 should therefore offer:
  - global accent only;
  - paired voices that share an output;
  - pre-scale {3, 4, 6, 8 steps per beat};
  - A/B/AB variations and I/F fills with auto-fill;
  - 12 rhythm tracks.

---

## 6. Table stakes (every serious 808 plugin has these)

1. All 808 voices synthesised, with the original panel controls: Level on all; BD Tone/Decay; SD Tone/Snappy; Tom/Conga Tuning; CY Tone/Decay; OH Decay. Plus the accent behaviour, which changes timbre as well as level (N1 explicitly says accent "also causes subtle changes to the sound character").
2. **Extended controls**: decay and tune on most voices (Nepheton, Roland, TS-808, SC-808 Adv), with **BD tune** treated as essential (Roland, TR-8S, Synsonic; its absence is N2's top criticism).
3. Per-voice **level, pan, mute/solo**, where mute lets the current tail finish.
4. **Multi-out**: at least one output per voice (11–16) plus a main mix, assignable per voice.
5. **External mode**: MIDI note → voice with a GM-compatible default map; velocity → level/accent.
6. **Internal step sequencer** synced to host transport/tempo: 16 steps minimum, accent (global row), shuffle, pattern memory with switching at pattern end, mute/solo per lane.
7. **Flam** and **sub-steps/ratchets ×2/×3/×4**. N2, Roland and TR-8S all have these.
8. Copy/paste/clear for tracks and patterns.
9. **Drag-and-drop MIDI export** of patterns (N2, Roland).
10. **MIDI learn** for CC control and host-automatable parameters.
11. Presets for kits and patterns, plus a factory pattern library.
12. Resizable / HiDPI GUI.

## 7. Differentiators and nice-to-haves (pick for the superset)

**From Nepheton 2**
- Per-voice channel strip: HP/LP + shared resonance, 2-band para EQ, look-ahead compressor.
- 2 serial FX buses with a selectable entry slot; 8 algorithms; sidechain and VCA ducking driven by instrument triggers.
- Master 3-band compressor + limiter with soft clip.
- 16 stereo outs with a "bypass downstream" option.
- Audible trigger-click emulation.
- Separate real-time and offline quality settings.
- Randomizer with per-track density and seed history.
- Tap record with velocity → accent.
- Host Note pattern triggering (48 notes).
- Two host-link modes (sync once vs continuous).
- Hierarchical presets (scene, kit, voice, master, pattern) with a tag browser.
- Opt-in automation and saveable CC maps.

**From Roland TR-808 / TR-8S**
- Per-instrument **last step** (polymeter) and per-instrument **shuffle** scaled by the global knob.
- 8 variations A–H.
- **Weak** steps (three dynamic levels: weak/normal/accent).
- Flam spacing in **ms** (0, 20–48 ms).
- Drag out as **audio** as well as MIDI.
- Per-voice **GAIN/overdrive**; BD/SD comp.
- **Per-step probability**; motion recording; scatter/auto-fill.
- Pattern Invert/All tools.
- Fixed documented CC map (handy for hardware controllers).

**From AudioRealism / Wave Alchemy / Triaz / Elektron culture**
- **Per-step parameter locks** (ADM PCF, Revolution step-locks, Triaz motion sequencer).
- Pattern expand/shrink.
- **Humanise ("slop")**.
- Per-lane rate.
- **ReBirth / TR-909 SysEx pattern import**. A related option for mw808 is MIDI-file import.

**From Synsonic, SC-808 and the trap use case**
- **Chromatic BD** (MIDI note → BD pitch, i.e. key-tracked tune) with **glide** (Up/Down/Both) and very long decay. Also drive and envelope compression on BD.

**From Nepheton 1 and the original 808 (authenticity)**
- 808 workflow mode: A/B/AB variations, 4 intro/fill patterns, auto fill-in every N measures, pre-scale 3/4/6/8, **12 rhythm tracks (songs)**.
- Velocity convention 48/96/127.
- Paired-voice mode.

**From the RD-8**
- Per-voice transient shaper ("Wave Designer").
- Master LP/HP performance filter with recordable cutoff.

**Unique to mw808**
- **Linux-native VST3 + CLAP**, likely also LV2/standalone (no commercial competitor offers this).
- CLAP per-note modulation and polyphonic expression on BD.
- **Zero-allocation real-time path** (stability).

## 8. UI layout conventions users expect

Derived from the original panel [TR808OM p.2] and the Nepheton 1, Nepheton 2 and Roland plugin GUIs.

1. **Top strip = voice sections, left to right in 808 order**: AC | BD | SD | LT/LC | MT/MC | HT/HC | RS/CL | CP/MA | CB | CY | OH | CH.
   - Each section is a vertical column: **LEVEL knob on top**, then TONE/TUNING/SNAPPY/DECAY knobs underneath.
   - Name plates at the bottom of each column, with a split label for paired voices (e.g. "L.Conga / L.Tom") and a small selector switch.
   - Nepheton expands this to 17 columns and adds a mute/solo LED and an activity LED per voice. Roland keeps 11 columns and adds a PANEL toggle for Gain/Pan/Tune.
2. **Left block**: the global controls.
   - MODE selector and Instrument/Track selector.
   - Big **TEMPO** dial with a Fine control.
   - Auto Fill In, and Master Volume (bottom-right on the 808).
3. **Bottom row = 16 step buttons** in four colour groups of four: steps 1–4 red, 5–8 orange, 9–12 yellow, 13–16 off-white. The SOS TR-8 review confirms the "808-styled red, orange, yellow and white" scheme [SOS-TR8]. The scanned manual is monochrome, so the exact grouping is from common knowledge and **UNVERIFIED** from a primary source.
   - Each button has an LED above it.
   - The buttons double as **Basic Rhythm 1–12 / Intro-Fill 1–4** pattern selectors.
4. **Left of the step row**: Basic Variation A/AB/B switch with LEDs, START/STOP, and the PRE-SCALE 1–4 indicator. **Right of it**: I/F Variation and the TAP button.
5. **Brand area**: the signature "Rhythm Composer" script plus a wood or dark chassis.
6. **Modern expectation, as in N2 and the Roland plugin**:
   - a toggleable **full-grid sequencer view** with one lane per voice, all lanes visible;
   - a pattern selector with bank and pattern buttons;
   - a pattern-properties column (scale, length, shuffle, velocities, flam and sub-step settings);
   - preset browsers in the header;
   - an Options/cog menu (outputs, MIDI, quality, GUI size).
   - The classic panel view and the grid view are both expected.

## 9. Concrete recommendations for mw808 (derived)

- **Voices**: 16 independent 808 voices (N2-style). Add a "hardware pair" option where the paired voices share a channel and choke each other. An optional 17th voice for N2 parity (e.g. a laser/zap BD variant) is low priority.
- **Per-voice parameters**:
  - original knobs, plus Tune and Decay on all voices;
  - BD Sweep (pitch-env time) and Attack/Click;
  - SD Tone, Snappy and Noise Decay;
  - CP Tone and Reverb (tail);
  - HH/CY metal frequency;
  - Drive/Gain, Pan, Level.
- **Strip**: HP/LP, 2-band EQ and comp per voice. Two FX sends/buses with reverb, delay and distortion as a minimum. Master compressor + limiter/soft-clip.
- **Outputs**: 16 stereo (CLAP/VST3 multi-out); default is everything to Main; per-voice output assignment.
- **Sequencer**:
  - 4 banks × 16 patterns (a superset of N2's 48);
  - up to 64 steps per pattern, plus per-track length;
  - scale 1/64–1/4 × {straight, dotted, triplet}, plus an 808 pre-scale preset;
  - shuffle global × per-track;
  - step states off / weak / normal / accent, plus a global accent row;
  - flam (ms or % of step);
  - sub-steps ×2/×3/×4 with a start→end velocity ramp;
  - per-step probability; optional per-step parameter locks;
  - randomizer with density and seeds; tap/MIDI record;
  - pattern queue at pattern end; Host Note pattern mode;
  - 808 A/B/AB + fill + auto-fill mode;
  - drag MIDI out (audio export optional);
  - MIDI out; host-link modes (sync once vs continuous).
- **MIDI**: default map = the Nepheton table in §2.1, so N2 users' clips just work. Editable map with learn. External velocity is continuous, with an optional 3-level quantise using 48/96/127.

---

## 10. UNVERIFIED / open items
- Whether Nepheton 2's factory drum map exactly equals the N1 helpdesk table.
- Nepheton 2 quality-setting levels (oversampling factor) and how MIDI CC triggering works.
- Whether Nepheton's CY/OH/CH share one metal oscillator bank (HH Frq is exposed on all three).
- Which Roland TR-808 plugin voices actually accept TUNE; the plugin's SCALE options.
- Exact 808 step-button colour grouping from a primary (colour) source.
- TS-808 sequencer presence and output count. SC-808 and iO-808 licences.
- Behringer RD-8 MkII per-voice parameter extras beyond the Wave Designer.

## Sources
- [N2P] D16 Nepheton 2 product page — https://d16.pl/nepheton2
- [N2M] Nepheton 2 User Manual (7 Mar 2024) — https://m.d16.pl/pub/manuals/Nepheton%202-manual-gb.pdf
- [N1M] Nepheton (1) User Manual — https://m.d16.pl/pub/manuals/Nepheton-manual-gb.pdf
- [D16KB41] D16 Helpdesk, "Nepheton 1 External Mode – Drum Map" — https://helpdesk.d16.pl/knowledge_base/article/41
- [MT-N2] MusicTech Nepheton 2 review — https://musictech.com/reviews/software-instruments/d16-group-nepheton-2-review
- [RC808] Roland TR-808 Software Rhythm Composer product page — https://www.roland.com/global/products/rc_tr-808/
- [RC808M] Roland TR-808 Software Rhythm Composer Owner's Manual (2018) — https://r2.gear4music.com/media/68/683758/download_683758.pdf (Roland index: https://www.roland.com/global/support/by_product/rc_tr-808/owners_manuals/)
- [TR808OM] Roland TR-808 original Owner's Manual (scan) — https://static.roland.com/assets/media/pdf/TR-808_OM.pdf
- [TR08] Roland TR-08 product page — https://www.roland.com/global/products/tr-08/
- [TR8S] Roland TR-8S features — https://www.roland.com/global/products/tr-8s/features/
- [RID-TR8S] Roland Indonesia TR-8S editing guide — https://rolandindonesia.com/the-ultimate-guide-to-the-tr-8s-rhythm-performer-advanced-editing-of-individual-instruments
- [ADM-KVR] AudioRealism ADM on KVR — https://www.kvraudio.com/product/2553
- [ADM-RK] rekkerd.org ADM news — https://rekkerd.org/audiorealism-celebrates-909-day-with-30-off-adm-drum-machine-plugin/
- [WA-REV] Wave Alchemy Revolution announcement — https://synthanatomy.com/2016/12/wave-alchemy-released-revolution-drum.html
- [WA-TRIAZ] Wave Alchemy Triaz — https://synthanatomy.com/2022/01/wave-alchemy-triaz-a-polyrhythmic-organic-sequencing-drum-engine-for-kontakt-player.html and https://www.musicradar.com/news/wave-alchemy-triaz
- [SB-HB] Softube Heartbeat — https://bedroomproducersblog.com/?p=26850 , https://www.musicradar.com/reviews/tech/softube-heartbeat-628086 , https://softube.com/manuals/heartbeat
- [TS808-ST] Synthtopia TS-808 — https://www.synthtopia.com/content/2010/04/24/free-virtual-roland-tr-808-drum-machine-ts-808/
- [TS808-HHM] HipHopMakers TS-808 — https://hiphopmakers.com/ts-808-vst-plugin-by-tactile-sounds
- [SYN-SA17] Synsonic BD-808 free — https://synthanatomy.com/2017/05/design-your-own-808-909-kick-sounds.html
- [SYN-SA18] Synsonic BD-808 Pro — https://synthanatomy.com/2018/03/design-808-style-bass-drum-sounds-bd-808-pro-plugin.html
- [SC808-GN] SC-808 (gearnews) — https://www.gearnews.com/sc-808-a-free-tr-808-drum-machine-built-on-code-and-running-on-supercollider
- [SC808-SA] SC-808 Advanced — https://synthanatomy.com/2020/09/sc-808-the-supercollider-tr-808-now-has-an-advanced-version.html
- [IO808] iO-808 README — https://github.com/vincentriemer/io-808/blob/master/README.md
- [RD8] Behringer RD-8 MkII (retailer spec) — https://www.perfectcircuit.com/behringer-rd-8mkii.html , https://thomann.ae/behringer_rd_8_mkii.htm
- [ART] Arturia Spark — https://www.synthtopia.com/?p=40282 , https://rubadub.co.uk/products/arturia-spark2
- [CA] Cherry Audio CR-78 / KR-55C — https://www.soundonsound.com/news/cr-78-first-virtual-drum-machine-cherry-audio , https://synthanatomy.com/2025/03/cherry-audio-kr-55c-review-and-sound-demo-supercharged-korg-kr-55a-and-kr-55b-emulation.html
- [ABL] Ableton Live manual, Max for Live devices (DS Kick) — https://www.ableton.com/en/live-manual/12/max-for-live-devices
- [PLAITS] Mutable Instruments Plaits manual — https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/
- [GEON] Geonkick — https://github.com/Geonkick-Synthesizer/geonkick
- [OT] OneTrick CRYPTID spec (Audinux) — https://copr-be.cloud.fedoraproject.org/results/ycollet/audinux/fedora-42-x86_64/08800625-onetrick-cryptid/cryptid.spec
- [CHOW] Chowdhury DSP projects — https://git.tdem.in/Chowdhury-DSP
- [SOS-TR8] Sound On Sound Roland TR-8 review (step colours) — https://www.soundonsound.com/node/4906431

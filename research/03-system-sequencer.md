# 03 — TR-808 as a System: Panel, Sequencer, Accent, Outputs, Timing, Quirks

Scope: everything outside the individual voice circuits: front panel, CPU sequencer, memory layout, the accent / common-trigger path, the clock, sync and trigger I/O, the mix bus and master output, and the system-level quirks. Voice DSP is covered by the other research files. This one covers how voices are triggered, how hard, when, and how they are summed.

## Sources (primary first)

| Tag | Source | URL |
|---|---|---|
| **[OM]** | Roland TR-808 Operation Manual (scanned original, 42 pp.) | https://static.roland.com/assets/media/pdf/TR-808_OM.pdf |
| **[SN]** | Roland TR-808 Service Notes, 1st ed. 15 Jun 1981 (3rd printing 1986). Read from the scanned PDF already in the project scratchpad (r01/sm.pdf). Page numbers refer to the service notes. | (copy of the publicly circulated service manual, e.g. manuals.fdiskc.com) |
| **[W-BD]** | K. J. Werner, J. S. Abel, J. O. Smith, "A Physically-Informed, Circuit-Bendable, Digital Model of the Roland TR-808 Bass Drum Circuit", DAFx-14 | https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf |
| **[W-CY]** | Werner, Abel, Smith, "The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model", ICMC/SMC 2014 | https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf |
| **[TR08]** | Roland Boutique TR-08 Owner's Manual (modern re-implementation; used only to confirm pre-scale semantics) | https://static.roland.com/assets/media/pdf/TR-08_eng03_W.pdf |
| **[RC808]** | Roland TR-808 Software Rhythm Composer (plugin) owner's manual | https://r2.gear4music.com/media/68/683758/download_683758.pdf ; https://www.roland.com/global/products/rc_tr-808/ |
| **[NEP]** | D16 Nepheton 2 product page and MusicTech review | https://d16.pl/nepheton2 ; https://musictech.com/reviews/software-instruments/d16-group-nepheton-2-review |
| **[MBX]** | MIDIbox forum, "MB808 tighter as a TR808" (reports Innerclock Systems jitter test) | https://forum.midibox.org/t/mb808-tighter-as-a-tr808/14762 |
| **[TR8S]** | Roland TR-8S reference manual and coverage of probability features | https://static.roland.com/assets/media/pdf/TR-8S_Reference_eng04_W.pdf ; https://www.gearnews.com/new-roland-tr-8s-tr-6s-firmware-brings-more-drums-fx-and-probability |
| **[S24]** | DIN sync / Sync24 summary | https://www.wikipedia.org/wiki/Sync24 |

Tags: **[DERIVED]** marks numbers I calculated from component values in [SN]. **UNVERIFIED** marks claims I could not confirm in a source.

**Fact-check status (2026-10-02):** an independent pass re-read the scanned [SN] pages 1, 3, 4, 5, 6, 13, 14 and 15 and [OM] pages 1, 11, 12, 15, 16, 18, 19 and 22 directly from the images. It also re-read [W-BD], [W-CY], the [RC808] manual, the Nepheton 2 user manual (7 Mar 2024) and the [TR08] manual, and recomputed every R/C-derived figure. Inline tags: **CONFIRMED** (seen in a primary source or recomputed from schematic values), **UNVERIFIED** (no primary source found), **CONFLICTING** (sources disagree; alternatives listed). Corrections are marked "*corrected from X*". Details are in the **Verification log** at the end of this file.

---

## 1. Front panel inventory

### 1.1 Voice section (top row, left to right, as printed on the panel and in [OM] p.2 and p.22)

| # | Panel label | Controls (top to bottom) | Selector switch | Notes |
|---|---|---|---|---|
| 1 | **AC** ACCENT | LEVEL | — | Global accent amount ("Accent level 0 dB–10 dB" [SN p.1]). Makes no sound of its own. |
| 2 | **BD** BASS DRUM | LEVEL, TONE, DECAY | — | |
| 3 | **SD** SNARE DRUM | LEVEL, TONE, SNAPPY | — | |
| 4 | **LT/LC** LOW TOM / LOW CONGA | LEVEL, TUNING | LT ↔ LC | One circuit. SW8 swaps a capacitor (frequency) and a resistor (level) [SN p.6]. |
| 5 | **MT/MC** MID TOM / MID CONGA | LEVEL, TUNING | MT ↔ MC | Same principle. |
| 6 | **HT/HC** HI TOM / HI CONGA | LEVEL, TUNING | HT ↔ HC | Same principle. |
| 7 | **RS/CL** RIM SHOT / CLAVES | LEVEL | RS ↔ CL | |
| 8 | **CP/MA** HAND CLAP / MARACAS | LEVEL | CP ↔ MA | |
| 9 | **CB** COW BELL | LEVEL | — | |
| 10 | **CY** CYMBAL | LEVEL, TONE, DECAY | — | |
| 11 | **OH** OPEN HIHAT | LEVEL, DECAY | — | |
| 12 | **CH** CLOSED HIHAT | LEVEL | — | |
| — | **MASTER VOLUME** | single knob, bottom right of the voice row | — | Acts only on the summed MASTER OUT. It has no effect on the individual outputs [OM p.6]. |

Control counts from [OM p.22]: 11 LEVEL plus 1 ACCENT LEVEL; TONE ×3 (BD, SD, CY); DECAY ×3 (BD, CY, OH); SNAPPY ×1 (SD); TUNING ×3 (LC=LT, MC=MT, HC=HT); VOICE selector ×5 (LC-LT, MC-MT, HC-HT, CL-RS, MA-CP).

### 1.2 Control / sequencer section ([OM pp.2–5, 22])

- **POWER** on/off push switch (top left).
- **MODE selector**, rotary at the far upper left. Clockwise from lower left: **PATTERN CLEAR**, **1st PART** (pattern write), **2nd PART** (pattern write), **MANUAL PLAY**, **PLAY** (rhythm-track play), **COMPOSE** (rhythm-track write). Only MANUAL PLAY and PLAY protect memory at power off [OM p.3]. The first three positions are colour-coded yellow on the panel [OM p.3].
- **INSTRUMENT SELECT / RHYTHM TRACK selector**, 12 positions with two jobs. When writing patterns it picks the voice being edited: AC, BD, SD, LT(LC), MT(MC), HT(HC), RS(CL), CP(MA), CB, CY, OH, CH. In PLAY/COMPOSE it picks rhythm track 1–12. For a paired voice it does not matter which twin you program: the panel switch decides which one sounds [OM p.3].
- **CLEAR** button (red, below the mode selector). Its jobs: PATTERN CLEAR, TRACK CLEAR, PRE-SCALE SET, STEP NUMBER SET [OM p.22].
- **TEMPO**, large knob with "forty click-stop positions", plus a **FINE** knob to its right that "varies the tempo slightly beyond the range of each click stop position" [OM p.4]. Range: quarter note = 40–300 [OM p.22; SN p.1].
- **AUTO FILL IN selector**: MANUAL, 16, 12, 8, 4, 2 [OM p.4, p.22].
- **BASIC VARIATION switch**, 3 positions A / AB / B, with A and B indicator LEDs above START/STOP [OM p.4, p.12].
- **START/STOP** button (lower left).
- **PRE-SCALE selector**, 4 positions 1–4, beside the step keys. The printed "scales" above the step keys show the step grid for each position [OM p.15].
- **1st PART / 2nd PART** LEDs.
- **16 STEP buttons**, each with an LED. They are labelled in two groups along the bottom edge: BASIC RHYTHM 1–12 (keys 1–12) and INTRO/FILL IN 1–4 (keys 13–16) [OM p.4].
- **I/F VARIATION switch**, 2 positions A / B (no AB) [OM p.5, p.12].
- **TAP** button (right of the INTRO/FILL IN keys). Its jobs: TAP real-time write, INTRO SET, FILL IN TRIGGER [OM p.22].

**Step key colours.** The 16 keys are grouped by four: 1–4 red, 5–8 orange, 9–12 yellow, 13–16 off-white. This matches every photo and every modern reissue ("808-styled red, orange, yellow and white"). The black-and-white scans do not show colour, and I found no text source giving the exact mapping, so treat the per-group mapping as **UNVERIFIED** (high confidence). Two knob colours are confirmed by the parts call-outs: KNOB N-180 RED and N-168 ORANGE [SN p.1].

### 1.3 Rear panel ([OM p.2, p.7, p.22; SN p.1])

Left to right:
- MASTER OUT **HI** (6 V p-p, 1 kΩ) and **LO** (0.6 V p-p, 3 kΩ). Both are specified with every voice LEVEL at the red mark and ACCENT at full CW. **CONFIRMED** ([SN p.1] spec block; [OM p.22]).
- MULTI OUT ×11, each 1 kΩ: CH, OH, CY, CB, CP(MA), RS(CL), HT(HC), MT(MC), LT(LC), SD, BD. **CONFIRMED** ([SN p.1, p.13 JACK-B]).
- TRIGGER OUT ×3: CB, CP(MA), AC. Each gives +15 V, 20 ms, 1 kΩ. **CONFIRMED** ([SN p.1]; [OM p.22]).
- PEDAL SW ×2 (Roland DP-2): START/STOP and FILL-IN (the second duplicates TAP).
- SYNC: 5-pin DIN plus an INPUT/OUTPUT slide switch. Pin 1 = START/STOP, pin 2 = GND, pin 3 = CLOCK (**CONFIRMED**, [OM p.22]). The unit "will not operate on its own" with the switch at INPUT [OM p.6].

---

## 2. Sequencer architecture (the "computer" in "Computer Controlled")

### 2.1 Hardware ([SN p.3–4])
- CPU: NEC **µPD650C-085** (4-bit), clocked at **500 kHz** (2 µs/cycle, trimmer IFT-1) [SN p.14]. **CONFIRMED**.
- RAM: **four 1K×4-bit CMOS RAMs** (µPD444C), battery-backed (3 × 1.5 V cells) [SN p.3, p.4, p.13; OM p.8]. **CONFIRMED**.
- The CPU's ports drive the instrument data lines directly: **12 instrument-data lines** (BD, SD, LT, MT, HT, RS, CP, CB, CY, OH, CH, AC) on ports PD/PE/PF. These are time-shared with RAM addressing. Port PI-2 outputs the master **trigger pulse** [SN p.3 table]. **CONFIRMED**. Pin map from the [SN p.3] table: PD0–3 = CH, OH, CY, CB; PE0–3 = CP, RS, HT, MT; PF0–3 = LT, SD, BD, AC.
- Panel scanning runs off an **interrupt clock of ~1.9 ms** (trimmer TM-1). Each interrupt reads the STATUS group (TEMPO CLOCK, START/STOP, TAP, FILL IN). The four step-switch groups are read once every 4 interrupts, which is **7.6 ms** [SN p.3, Fig. 2]. **CONFIRMED** (text and Fig. 2; the 1.9 ms trim target is also in the [SN p.14] adjustment table).

### 2.2 Clock and tempo ([SN p.5, p.14])
- The tempo oscillator is analog: an anti-log current source (Q13/Q14) discharges C10 into a Schmitt trigger (IC1). Its output is divided by 2 in a toggle flip-flop (IC2A) to make the **TEMPO CLOCK**.
- **Quarter note = 24 tempo clocks.** **CONFIRMED**: [SN p.5] says "With TR-808, ♩ is defined to have 24 clocks". The same sentence prints the range as "400–300", an obvious misprint for the 40–300 in [SN p.1] and [OM p.22]. The tempo-clock period runs from **8.33 ms (300 BPM)** to **~65 ms (≈38.5 BPM)** (*corrected from "≈40 BPM": 24 × 65 ms = 1.56 s per quarter = 38.5 BPM; the ±5 ms check tolerance spans ≈36–42 BPM. Recomputed from [SN p.14].*). Factory trim: 8.33 ms with TEMPO and FINE at full CW, and 65 ± 5 ms check with TEMPO full CCW and FINE full CW [SN p.14]. **CONFIRMED**. The nominal panel range is 40–300. Because the clock is an RC oscillator, real tempo drifts with temperature and component ageing, and the knob has no calibrated BPM scale.
- **Start behaviour:** the tempo oscillator runs all the time, even when stopped. Pressing START stops it for **~9 ms** to give the CPU preparation time, then restarts it from a defined phase, so the first clock is aligned to the button press [SN p.5]. **CONFIRMED** (text and Fig. 8).
- Sync IN: the internal tempo and start/stop circuits are bypassed and the DIN clock and start/stop signals replace them [SN p.5].

### 2.3 Step resolution: the PRE-SCALE selector

[OM p.1]: "Each beat (or quarter note) can be divided into 3, 4, 6, 8 individual steps". [TR08] confirms the mapping. The tick counts are **[DERIVED]** from 24 clocks per quarter, and they match the manual's worked examples (12 steps at PS1 = one 4/4 bar, 32 steps at PS4 = one 4/4 bar, 12 steps at PS3 = one 3/4 bar). **CONFIRMED**: the [OM p.1] quote was re-read. [OM p.15] Fig. 5 shows 12 steps at PS3 in 3/4, and [OM p.16] Fig. 6 shows 12 steps at PS1 as one 4/4 bar of 8th-note triplets. The [TR08] panel legend reads "1: 8th note triplet, 2: 16th note triplet, 3: 16th note, 4: 32nd note".

| Pre-scale | Step value | Steps per quarter | Tempo clocks per step | Step length in quarters | Steps for one 4/4 bar |
|---|---|---|---|---|---|
| 1 | 8th-note triplet | 3 | 8 | 1/3 | 12 |
| 2 | 16th-note triplet | 6 | 4 | 1/6 | 24 (16 + 8 using both parts) |
| 3 | 16th note (factory default for all presets) | 4 | 6 | 1/4 | 16 |
| 4 | 32nd note | 8 | 3 | 1/8 | 32 (16 + 16 using both parts) |

- The shortest step is PS4 at 300 BPM: 3 × 8.33 ms = **25 ms** between steps.
- A measure is always "steps × step value". The pre-scale does **not** change what counts as a quarter note: tempo is always 24 clocks per quarter.
- Mixed meters inside one bar (for example 16ths plus triplets) are not possible. The manual tells you to split the bar into two patterns and chain them in a rhythm track [OM p.20 note 2].

### 2.4 Pattern memory layout ([OM pp.3–4, 11–19, 22])

```
16 rhythm slots = 12 BASIC RHYTHM (keys 1–12) + 4 INTRO/FILL IN (keys 13–16)
  × 2 variations (A, B)                         -> 32 patterns ("16 × 2")
     each pattern = 1st PART (0–16 steps) + 2nd PART (0–16 steps)  -> 1–32 steps / measure
        each step = 12 bits: BD SD LT MT HT RS CP CB CY OH CH + AC
```
- **Pre-scale and step count are shared between A and B** of the same rhythm: setting one sets the other [OM p.16]. The 1st and 2nd PART counts are separate. After CLEAR, the 1st part is 16 steps and the 2nd part is 0 [OM p.18]. **CONFIRMED** (both pages re-read; "32 different Rhythm patterns (16 × 2)" [OM p.18]; "1–32 steps" [OM p.22; SN p.1]).
- **Setting the last step:** in 1st PART (or 2nd PART) write mode, while running, choose PRE-SCALE, then **hold CLEAR and press the step key** that should be the last step. Pressing CLEAR alone applies the pre-scale. Example for 20 steps: 16 + 4, 10 + 10, or 4 + 16 [OM pp.15–18, Fig. 8].
- The 2nd part always plays after the 1st, in the order 1st, 2nd, 1st, 2nd... The two parts cannot be played separately [OM p.18].
- Clearing in the AB position erases both A and B [OM p.16].
- An INTRO/FILL IN pattern cannot be shorter than a quarter note [OM p.20 note 3].

**Factory presets** [OM pp.23–]: BR-1/BR-2 practice program, BR-3 Rock, BR-4 Bossa Nova, BR-5 Mambo, BR-6 Samba, BR-7…BR-11 "rhythm for track memory", BR-12 "rhythm for checking sounds" (all voices at once). I/F 1–3 are the rhythm-track fills and I/F 4 is a practice program. All are 16 steps at pre-scale 3. The manual includes full step charts with AC rows, which could be transcribed into presets.

### 2.5 Programming methods
- **Step write:** pick a voice (or AC) with INSTRUMENT SELECT, then toggle steps on keys 1–16 while the pattern runs. The LED chase shows the playhead [OM p.13].
- **TAP write (real time):** while running in a write mode, each TAP press writes the selected instrument at the current step, quantised to the step grid. Step-switch scanning runs every 7.6 ms [SN p.3], so tap capture is coarse.
- There is no velocity per voice. There is no per-voice accent. **AC is one global row.**

### 2.6 Playback modes and transition rules
- **MANUAL PLAY:** pick a basic rhythm with keys 1–12. The flashing LED marks the rhythm that has priority.
  - **Pattern change rule:** the 808 finishes the current measure, then switches. **Exception:** if the new rhythm is picked *during the first beat* of the measure, it starts on the second beat of that same measure [OM p.11]. **CONFIRMED** (verbatim: "if the new Rhythm is selected on the first beat of the measure the new Rhythm will begin on the second beat of that same measure"). In COMPOSE you hear it the same way, but the track stores it as a whole measure [OM p.20].
- **Variation:** A, B, or AB (A then B, repeating, always starting on A). A change of the variation switch takes effect on the first beat of the next measure [OM p.12]. I/F VARIATION works the same way but has only A or B. **CONFIRMED**, with one nuance from [OM p.12]: "always beginning in the A Mode" applies from START. If you switch from B to AB *while running*, it "will also move in a continuous fashion from B to A, B, A". So a mid-play switch to AB carries on from the current variation and does not jump back to A.
- **INTRO:** press TAP while stopped to arm the selected I/F rhythm. It then plays once before the basic rhythm, for example INTRO, A, B, A, B. The intro does not count toward auto-fill counting [OM pp.11–12]. **CONFIRMED**.
- **FILL IN, manual:** with AUTO FILL IN at MANUAL, pressing TAP while running plays the I/F rhythm at the end of the current measure, once, then returns to the basic rhythm [OM p.12].
- **FILL IN, auto:** at 2/4/8/12/16 the fill replaces every Nth measure. TAP can still add extra fills. Turn back to MANUAL to cancel [OM p.12]. **CONFIRMED** (selector positions MANUAL, 16, 12, 8, 4, 2 [OM p.22]).
- **Rhythm Track (song) = PLAY / COMPOSE.**
  - There are **12 tracks of 64 measures each, 768 measures in total**. A composition that runs past the end of a track continues into the next one automatically [OM p.3, p.19, p.22]. **CONFIRMED** ([OM p.19]: "It is not necessary to manually switch to the next Track once you have used up the 64 measures ... The switching will be done automatically for you").
  - COMPOSE: select the track, press CLEAR, press START. Every measure that plays is recorded as you switch rhythms live with the step keys.
  - STOP ends the composition. The measure in which STOP is pressed becomes the last measure, and that whole measure is stored even though the sound cuts off. A blank pattern at the end gives a clean stop.
  - **Only the rhythm number is stored, not the variation switch.** A/B is a live "play" control during track playback too [OM p.19]. **CONFIRMED**.
  - Editing: COMPOSE without CLEAR re-records from the measure where you start switching. The total length cannot change [OM p.20].
  - When a track reaches its end it repeats unless stopped [OM p.19].
- **START/STOP** can be pedal-controlled. TAP/FILL can be pedal-controlled.

---

## 3. Accent and the common trigger (electrical)

From [SN p.4, "Trigger Gate", Fig. 6–7] and [W-BD §3]:

1. At every step (the "shortest rhythm step" rate), the CPU puts out a **10 µs** pulse on PI-2. At the same time it presents that step's instrument data on PD/PE/PF. **CONFIRMED** ([SN p.4] text and Fig. 6). Nuance from [SN p.3]: the PI-2 pulses are generated "regardless of TR-808 function mode (Start/Stop) and of presence or absence of rhythm patterns", spaced at "the shortest rhythm patterns" interval. The pulse train therefore runs even when stopped, and voices sound only when instrument data is present. The exact pulse grid is **UNVERIFIED**: the step lengths of 8/4/6/3 tempo clocks share no common grid coarser than one clock, so the CPU most likely emits one pulse per tempo clock or per current step. This makes no audible difference.
2. The pulse is too short to trigger a voice, so the monostable IC6 stretches it to **~1 ms** (time constant set by R100·C23 + R102·C27). The service notes call any trigger width outside 1 ms "a cause of deteriorative voices" [SN p.14]. **CONFIRMED** ([SN p.4] "widened to approx. 1ms"; [W-BD §3] "a 1-ms long pulse"). The individual R100/C23/R102/C27 values were not re-read.
3. IC6's output drives the **ACCENT circuit (Q31–Q34)**, which produces the **COMMON TRIG** pulse. This pulse goes **in parallel to every voice gate**. Each voice gate ANDs COMMON TRIG with its own instrument-data bit. The gate output amplitude follows the COMMON TRIG amplitude, so **one voltage sets how hard every voice on that step is hit.**
4. **Unaccented step:** Q34 shunts VR3 (the ACCENT LEVEL pot), and Q32's base sits at about +5 V, giving a **trigger amplitude of ≈4 V**. This level is **fixed and does not depend on the ACCENT knob.**
   **CONFIRMED** (verbatim in [SN p.4]: "the base of Q32 becomes approx. +5V and trig amplitude is approx. 4V").
5. **Accented step** (AC data high): VR3 sets Q32's base anywhere between **5 V and 15 V**, giving a **trigger amplitude of ≈4–14 V**. So the ACCENT knob only changes the *accented* hits. At minimum, an accented hit is about the same as an unaccented one. At maximum it is about 3.5× the voltage (20·log10(14/4) ≈ 10.9 dB, which matches the "0 dB–10 dB" spec). **CONFIRMED** ([SN p.4] "a voltage between 5V and 15V according to VR3 ... trig pulses of approx. 4–14V"; [W-BD §3] "between 4–14 V, depending on VR3"; "Accent level 0dB–10dB" [SN p.1]).
6. **CB, CY, OH and CH** get a narrowed trigger range of **7–14 V** from a buffer (½ IC2, pins 1–3) on the voicing board, "to increase S/N ratio" [SN p.4; W-CY §5]. **CONFIRMED** (both sources). Their *trigger-voltage* swing is therefore only about **2× (+6 dB)** at most. **CONFLICTING for CB:** the [SN p.14] check table gives CB 3.5 → 12 Vpp (+10.7 dB) even though CB is in the narrowed group, while CY/OH/CH do measure about +6 dB. Output swing is not simply proportional to trigger voltage, because the CB's VCA gates (Q14/Q15) and envelope are nonlinear. Calibrate CB to the table (+10.7 dB) and do not cap it at +6 dB. The exact mapping from the common trigger to 7–14 V is **UNVERIFIED**. A linear map, V' = 7 + 0.7·(V − 4), fits both endpoints.
7. The accent is a *drive level into nonlinear circuits*, not a post-gain stage. Werner: "The effect of the varying accent level is complex, and depends heavily on nonlinear downstream subcircuits" (cymbal: attack smoother, envelope generators, swing-type VCAs) [W-CY §5]. For the BD, the trigger amplitude scales the pulse-shaper rising edge, while the falling edge is clamped near one diode drop (0.71 V) whatever the accent [W-BD §4]. **CONFIRMED with precision added**: [W-BD §4] says the rising edge equals V_TRIG and the falling edge is "approximately equal to V_TRIG·R162/(R162+R163) + 0.71". The falling-edge *response* is "largely independent of the pulse amplitude", but its amplitude still has a small V_TRIG-proportional term. [SN p.5] adds that the BD's halved-time-constant attack trick (Q41–Q43, ~4 ms) is described as acting "when ACCENT is present". Accent therefore changes the attack/click relative to the body, and through larger swings it changes the leakage-driven pitch "sigh" [W-BD §8.2]. For the toms and congas, larger amplitude means more diode conduction (D80/D81), which means a higher initial pitch with a stronger downward glide [SN p.6].
8. **Measured accent response per voice**, from the service-manual check table [SN p.14]. Measured at the MULTI OUT with voice LEVEL at full CW and DECAY/TONE at 12 o'clock. "Normal" is with ACCENT at full CCW; "Accent" is with ACCENT at full CW. Values are "typical and variable". **CONFIRMED**: every cell below was re-read against the scan and every dB ratio recomputed. Decay is defined in the [SN p.14] figure as time to 1/10 Vpp (−20 dB). The procedure reads: "set ACCENT LEVEL to FCCW position and the VOICE LEVEL to FCW, then turn ACCENT FCW". Frequency cells are printed as period in ms with Hz in brackets.

| Voice | Normal Vpp | Accent Vpp | Ratio (dB) | Freq (Hz, low / mid / high tuning) | Decay to 1/10 Vpp (ms, short / mid / long) |
|---|---|---|---|---|---|
| BD | 3.5 | 10 | +9.1 | 56 (18 ms period) | 50 / 300 / 800 |
| SD | 3 | 10 | +10.5 | 238 & 476 (two bridged-T resonators) | 60 |
| LC | 3.5 | 12 | +10.7 | 165 / 185 / 220 | 180 |
| LT | 3.5 | 12 | +10.7 | 80 / 90 / 100 | 200 |
| MC | 3 | 10 | +10.5 | 250 / 280 / 310 | 100 |
| MT | 3 | 11 | +11.3 | 120 / 135 / 160 | 130 |
| HC | 3.5 | 12 | +10.7 | 370 / 400 / 455 | 80 |
| HT | 3.5 | 12 | +10.7 | 165 / 185 / 220 | 100 |
| CL | 2.5 | 8 | +10.1 | 2500 | 25 |
| RS | 3 | 10 | +10.5 | 455 & 1667 | 10 |
| MA | 3 | 5 | +4.4 | — | 25 (short) / 35 (long) |
| CP | 6 | 2 (sic) | ? | — | 100 |
| CB | 3.5 | 12 | +10.7 | 540 & 800 | 50 |
| CY | 3.5 | 7 | +6.0 | — | 350 / 800 / 1200 |
| OH | 3.5 | 7 | +6.0 | — | 90 / 450 / 600 |
| CH | 3 | 6 | +6.0 | — | 50 |

The CP accent figure of "2" is almost certainly a misprint in the table (perhaps 12). Treat it as **UNVERIFIED**. The scan really does print "6 / 2". The CP "normal" 6 Vpp does match the 6 Vpp target of the CP offset trim (TM-3, LEVEL FCW) in the [SN p.14] adjustment table, so the 6 is probably right and only the accent cell is suspect. The +6 dB ceiling for CY/OH/CH agrees with the 7–14 V narrowing. CB does not follow it (+10.7 dB; see item 6). MA's small swing (+4.4 dB) suggests it is driven partly into saturation.

Additional table caveats from the fact-check:
- **HT row = LC row as printed**: 6.1/5.4/4.5 ms (165/185/220 Hz) appears in both rows of the scan. It is transcribed faithfully, but it is **CONFLICTING/suspect**: HT and LC share a circuit with different capacitors, so identical tuning is plausible but could also be a copy error in the table. Cross-check against the tom research file.
- **BD frequency is CONFLICTING**: the table gives 18 ms (56 Hz). [W-BD §5] computes the bridged-T centre as ≈49.5 Hz and calls 56 Hz "close". [SN p.5] text describes the "inherent oscillation period" as 16 ms (62.5 Hz). Treat 49.5–62.5 Hz as the plausible span, with 56 Hz as the measured typical value.

**Implementation guidance:**
- Model a per-step trigger voltage: `Vtrig = accented ? lerp(4, 14, accentKnob) : 4` for the main voices, and the 7–14 V remap for CB/CY/OH/CH.
- Feed `Vtrig` into each voice model as an *excitation amplitude*. Do not apply it as output gain.
- Use the table above as a calibration target: with the knob at maximum, peak output should rise by the dB in the Ratio column. That includes CB at about +10.7 dB despite its 7–14 V trigger.

---

## 4. Timing character

- **Clock source:** an analog RC tempo oscillator (24 ppqn) inside a CPU loop that polls it. The STATUS lines, including TEMPO CLOCK, are read **once per ~1.9 ms interrupt** [SN p.3]. (**CONFIRMED**: "statuses are checked each time an INT signal is applied every 1.9ms so that the CPU can respond to the status change promptly".) **[DERIVED / inference]** So a step trigger can only fire on the interrupt grid. It lags the true clock edge by 0 to ~1.9 ms, with a near-uniform distribution. The error is per event and does not add up over time (the step count follows clock edges). The interrupt clock itself is an RC Schmitt oscillator that differs from unit to unit ("Variations in HD14584 hysteresis sometimes deviate Clock Rate out of specified frequency range" [SN p.15, main-board mod 1]).
- **Measurement:** Innerclock Systems' test, as reported on [MBX], found a TR-808 with a maximum deviation of **87 samples at 44.1 kHz (≈1.97 ms)** between quarter notes at 120 BPM. That fits the 1.9 ms interrupt-grid explanation well. **CONFLICTING (minor):** [MBX] confirms "the tested 808 is 87" but gives no sample rate for Innerclock's own capture; the [MBX] poster measured at 44.1 kHz. Innerclock's published method (via greatsynthesizers.com, "Timing – the litmus test") records at **48 kHz**, which would make 87 samples ≈ **1.81 ms**. Either way the result is ≈1.8–2.0 ms, consistent with the 1.9 ms interrupt. The Innerclock results page (innerclocksystems.com "New ICS Litmus") now returns 404, so the primary figure could not be re-read. Note that uniform 0–1.9 ms onset error gives inter-onset deviations of up to ±1.9 ms, which matches a ~87-sample maximum. A claim on the same forum that the 808 fires voices on one step one after another is **contradicted by [SN p.4]**: COMMON TRIG goes to all gates at once, so voices on the same step start together, within analog propagation time. The cited [MBX] thread page itself does not contain that claim, so its source is **UNVERIFIED**.
- **No swing or shuffle exists on the original**, and there is no flam, no per-step velocity and no probability. Swing was only possible from outside, by sending a modulated DIN clock (for example Innerclock Sync-Gen; https://nl.ra.co/reviews/10846).
- **Tempo drift:** the analog oscillator means slow tempo drift; its size is **UNVERIFIED**. Supply ripple from shorted stages "causing TEMPO CLOCK to be unstable" is listed as a fault symptom [SN p.14].
- **Start latency:** about 9 ms from START to the restart of the tempo oscillator [SN p.5]. **CONFIRMED**.

**Recommendation for mw808:**
- Default to sample-accurate timing.
- Offer an optional "Vintage timing" mode that delays each step onset by `U(0, 1.9 ms)`, or quantises onsets to a free-running 1.9 ms ± small-drift grid. All voices on a step get the same offset.
- Offer an optional slow tempo-wander LFO, very small, for example ±0.1–0.3 %. That magnitude is **UNVERIFIED**, a design choice.

---

## 5. Sync and trigger I/O

- **DIN sync (Sync24):** 24 ppqn clock plus a run/stop level, TTL (0 / +5 V) [S24]. Pins: 1 START/STOP, 2 GND, 3 CLOCK [OM p.22]. Roland never published a formal Sync24 spec [S24].
  - At OUTPUT the 808 is master. It drives the CSQ-600, Jupiter-8 arpeggio, MC-4 and others.
  - At INPUT it is slave, and its tempo and start/stop are ignored [OM p.7; SN p.5].
  - The tempo clock runs even when stopped [SN p.5], so the DIN clock line probably toggles while stopped. That is **UNVERIFIED** at the jack.
  - Early units were changed (S/N 010600 and later) to raise the DIN pin-5 input resistance from 220 kΩ to 1.5 MΩ to "prevent false triggering". A 0.01 µF capacitor from DIN pin 2 to chassis was added for static protection [SN p.15]. **CONFIRMED** (main-board mod 4: R25 220K→1.5M; the 0.01 µF cap is listed against CPU pin 37).
- **TRIGGER OUT CB, CP(MA), AC:** +15 V, **20 ms**, 1 kΩ. They follow the CB row, the CP row and the AC row of the playing pattern [OM p.7; SN p.1]. **CONFIRMED** (values).
  - They are independent of the voice LEVEL knobs. The manual suggests programming trigger-only rhythms and silencing the voice with its LEVEL control.
  - The AC trigger fires on every accented step, even when no voice plays.
  - They are commonly used to clock arpeggiators and step sequencers (JP-4/JP-8 ext clock, SH-09 gate, CSQ-100 step) [OM pp.7, 9–10].
  - Plugin equivalent: MIDI note-out or audio-rate pulse outputs. D16 includes "audible trigger outputs" [NEP].

---

## 6. Mix bus and master output (read from [SN p.13] schematic, VG3116-140 voicing board and JACK-B board OP3116-110)

```
voice buffer --1k--> MULTI OUT jack tip (1 kΩ out)
                     normalled switch contact --R_mix--+
                                                       |  (all 11 voices)
                                       mix bus --C17 1µF--> IC4a (−) summing amp
                                                             Rf = R75 33k || C18 220p
                       --C19 1µF--> VR1 MASTER VOLUME 50kΩ(A) --C23 1µF--R77 22k--> IC4b (−)
                                                             Rf = R76 33k || C22 220p  (Q13 FET mute)
                       --> D9/D10 rail clamps --> C1 10µF BP --R14 1k--> HI jack
                                                          \--R13 27k--+--> LO jack
                                                                     R12 3.3k to GND
```

- **Jacks remove voices from the mix.** Each MULTI OUT is a switched jack. Plugging in opens the normalled contact that feeds the mix resistor, so the voice leaves MASTER OUT and appears only at its own jack, still after its own LEVEL knob [OM p.6: "Connecting any of these jacks removes that instrument from the summed MASTER OUT signal"].
- **Mix resistors** [SN p.13 JACK-B]: BD **22 k**; SD, LT/LC, MT/MC, HT/HC, RS/CL, CP/MA **33 k**; OH **56 k**; CH, CB, CY **47 k**. **CONFIRMED** (re-read from the JACK-B board OP3116-110: R5 22k BD; R4/R2/R1/R3/R6/R7 33k; R10 56k OH; R11/R8/R9 47k CH/CB/CY. Normalled jack contacts as described). With R75 = 33 k feedback and each voice's 1 k series output resistor included, the summing-stage gains are **[DERIVED]** and were **recomputed and CONFIRMED** (33/23 = 1.435; 33/34 = 0.971; 33/48 = 0.6875; 33/57 = 0.579). The 1 k series resistor was seen on the voicing-board voices (e.g. R166, R169, R172) and is assumed for the main-board voices, in line with the "Multi output 1 kΩ" spec. Leaving it out would shift each gain by only about 0.3 dB.

| Voice | Summing gain | dB |
|---|---|---|
| BD | 1.43 | +3.1 |
| SD, toms/congas, RS/CL, CP/MA | 0.97 | −0.25 |
| CH, CB, CY | 0.69 | −3.3 |
| OH | 0.58 | −4.7 |

  So the 808's own fixed mix pushes the BD up and the metallic voices down.
- **Master stage:** the gain is 33 k / 22 k = **1.5 (+3.5 dB)** after the master-volume pot. **CONFIRMED** (R77 22k in, R76 33k feedback; VR1 50kΩ(A)). Two inverting stages give non-inverted polarity overall. ~~D9/D10 clamp the output at the ±B5 rails, so very hot accented sums can hard-clip on the master bus.~~ *Corrected:* D9/D10 go from the IC4b output to ±B5 (≈ ±15 V, filtered branches of the TA7179P ±15 V regulator). An op-amp on ±15 V cannot drive its own output past its rails, so these diodes only protect the output against external voltages; they do not set the clip level. Any master-bus clipping comes from **op-amp output saturation** (roughly ±13 V for a 4558-class part, ≈ 26 Vpp, ≈ +13 dB above the 6 Vpp HI spec) **[DERIVED, UNVERIFIED by measurement]**. That can happen in IC4a (the fixed-gain summer, before the volume pot) as well as IC4b: an accented BD alone is about 10 Vpp × 1.43 ≈ 14 Vpp at IC4a with its LEVEL at max. The manual warns that accented hits make "sudden high level output signals" that distort amp preamps [OM p.6].
- **Low-pass:** each of the two stages has 33 k ∥ 220 pF, which is **f ≈ 21.9 kHz** [DERIVED]. **CONFIRMED** (C18 and C22 both 220p; 1/(2π·33k·220p) = 21.92 kHz). Two gentle first-order poles: inaudible except as a slight treble softening.
- **High-pass, important:** C17 (1 µF) feeds the virtual ground from a bus whose source impedance is all the mix resistors in parallel (≈3.3 kΩ with no jacks plugged). That gives a first-order **HPF of ≈48 Hz** on the summed output [DERIVED]. **CONFIRMED by independent recomputation from the schematic**: the mix bus leaves JACK-B pin 13 and enters the voicing board at pin 6, then C17 ("1/50", i.e. 1 µF 50 V) feeds IC4 pin 2. The Thevenin source at the bus is the parallel of (R_mix + 1 k) for all 11 voices: 1/(1/23 + 6/34 + 1/57 + 3/48) k = 3.333 kΩ, so f_c = 1/(2π·3.333k·1µF) = **47.7 Hz**. This still has not been *measured* on hardware, and electrolytic tolerance (typically ±20 %) puts the real corner anywhere in ≈40–60 Hz. The BD fundamental is about 56 Hz, so on MASTER OUT the BD loses around 2–3 dB of fundamental compared with its MULTI OUT (recomputed: −2.4 dB at 56 Hz; −2.9 dB at Werner's 49.5 Hz; first-order phase lead of ~40°). Plugging voices into their own jacks removes their resistors and lowers this corner a little (e.g. BD unplugged from the mix → 3.90 kΩ → 40.8 Hz). The other coupling caps give corners of ~~≈3 Hz~~ **≈3–10 Hz** (C19 into VR1 50 k; *corrected:* at full volume C19 also sees R77 22 k through C23, so the load is 50k ∥ 22k ≈ 15 k → 10.4 Hz) and ≈7 Hz (C23 into R77 22 k), which are negligible (together < 0.3 dB at 56 Hz). It is cheap to model and audibly relevant.
- **HI / LO outputs:** LO = HI × 3.3/(27 + 3.3) ≈ **−19.3 dB** (matches 6 V p-p → 0.6 V p-p). Output impedances: HI ≈ 1 kΩ, LO ≈ 2.9 kΩ. **CONFIRMED** (R14 1k to HI; R13 27k / R12 3.3k divider to LO; C1 10 µF bipolar coupling; 20·log10(3.3/30.3) = −19.26 dB; 3.3k ∥ 27k = 2.94 kΩ; spec "Lo 0.6V p-p/3KΩ").
- **Power on/off muting:** a FET (Q13, 2SK30A) across the master stage, driven by the Q10–Q12 detector (−15 V within 1–2 s at power on, 0 V at power off) [SN p.5, p.13]. **CONFIRMED** ("Power on: 0V 1-2sec −15V"). No audible effect in normal use.
- **Noise floor:** no numeric spec found (**UNVERIFIED**). The shared noise generator measures **130 mV rms** at TP-4 [SN p.14] (**CONFIRMED**). Several noise paths are gated on purpose to cut leakage. For example, normally-conducting Q74 blocks IC19 except during the trigger, "to eliminate noise leaking from IC20, especially for CL — relatively large amount, being wired for high Q" [SN p.6]. The plugin could offer an optional faint hiss/hum floor, default off.

---

## 7. Polyphony, voice sharing, retrigger

- **11 voice circuits plus AC on 12 trigger lines.** Each circuit is a single instance, so every voice is monophonic. All voices can sound at once.
- **Pairs that share one trigger line and one panel selector** (switched on the panel, not per step):
  - **LT/LC, MT/MC, HT/HC:** literally the same circuit with a swapped capacitor and level resistor (SW8: C77 frequency, R224 level) [SN p.6]. **CONFIRMED** (the text uses LT/LC as the worked example; the others "are composed of the circuits based on the same principle"). Toms also mix in noise ("pink noise… for artificial reverberation"); conga mode does not [SN p.6]. The [SN] sentence literally says "mixed for Low Tom" inside the LT/LC example, so extending it to all three toms rests on the "same principle" statement. **CONFIRMED (inferred for MT/HT)**.
  - **RS/CL:** separate resonant networks (IC20 for CL, IC21 path for RS) behind one trigger, output through buffer IC19 [SN p.6]. *Refined:* in CL mode the IC20 bridged-T goes to IC19, and the IC21 contribution "can be ignored because of its minimized level". In RS mode R313 is disconnected, so IC20b only buffers IC20a, and IC20b's output is combined with IC21's in the swing-type VCA Q62 (envelope R107/C24). RS therefore uses **both** resonators (455 Hz and 1667 Hz per the table) plus a VCA.
  - **CP/MA:** both use the shared white noise. CP has its band-pass (IC21) and the sawtooth-envelope VCA plus reverb-tail VCA; MA has its own gate (Q65), filter (Q68) and envelope (Q66/Q67). Both go through buffer IC19 [SN p.6].
- **Shared oscillators:** CY, OH and CH all draw from the **same six Schmitt-trigger square oscillators**. Two of the six are the CB's [SN p.6; W-CY §3]. **CONFIRMED** ([SN p.6]: "six Schmitt triggers including two for CB generator"; [W-CY §3]: a single HD14584, oscillators #5–6 trimmed to 800 and 540 Hz). They can sound together, but they share pitch content, so their beating patterns are correlated.
- **Hi-hat choke:** "When the CLOSED HI-HAT (CH) is triggered while the OH circuit is activated, Q23 turns on… the decay time of the OH circuit terminates" [SN p.6]. **CONFIRMED** (Q23 driven via R173). **CH chokes OH.** OH does not choke CH. CY is independent.
- **Retrigger:** there is no voice reset. A new 1 ms pulse excites the resonant filters and envelope caps *from whatever state they are in*. Werner names "inaccurate behavior when a new note is triggered before the previous one has died out" and the "machine gun effect" as common emulation pitfalls [W-BD §1]. Emulation must therefore keep filter and envelope state and add the new excitation on top. It must never zero the state and must never spawn a second voice.
- **Crosstalk quirk:** "High frequency from CP generator induces irregular oscillation on other generators triggered at the same time." This was fixed from S/N 010600 by filtering the common trigger (R101 10 k→100 k, C201 22 pF) [SN p.15]. **CONFIRMED** (main-board mod 7, IC6 TC4011BP: R101 10K→100K, C201 0→22p, "* Filter out CP high frequencies"). Pre-010600 units can show CP-correlated artifacts on simultaneous hits. [SN p.15] also notes that "some of the modifications were done at the factory on some products bearing serial number earlier than indicated", so the serial number is only a guide.

---

## 8. Unit-to-unit variance and documented quirks

- Factory trimmers: CPU clock (IFT-1), interrupt clock (TM-1), tempo (TM-2), CP offset (TM-3, 6 Vpp target), noise level (TM-4, 130 mV rms), cowbell oscillators (TM-1/TM-2 on the voicing board: 1.85 ms (540 Hz) and 1.25 ms (800 Hz)) [SN p.14]. So the CB pitch and the noise level differ per unit. **CONFIRMED** (all trim targets re-read; 1/1.85 ms = 540.5 Hz, 1/1.25 ms = 800 Hz; [W-CY] agrees on 800/540 Hz).
- The check table is labelled "values are typical and variable" [SN p.14]. Werner explains "why unmodded bass drums can sound very different from one another" through component tolerances [W-BD §1].
- **Production changes that alter the sound** [SN p.15]:
  - CP gain reduced (R346 1 k→680, R332 22 k→27 k) because "CP sound overmatches the rest in level". Early units have a louder clap. **CONFIRMED** (mod 2, S/N 010600; applies to "both proper and reverberation components").
  - Voice-gate resistors 22 k→10 k (R106, R154, R182, R213, R242, R268, R298) to "ensure sufficiency of gate drive signal voltage at lower COMMON TRIG amplitude". On early units, unaccented (4 V) hits could be weak or unreliable. **CONFIRMED** (mod 6, S/N 010600). The "weak or unreliable" consequence is an inference.
  - Noise-generator DC decoupling changed because op-amp bias variation caused an offset that reduced gain margin. **CONFIRMED** (mod 5: R129 330K→short, R311 330K→100K, R127 4.7K→10µ (C200), C202 0→22p).
  - Cowbell oscillator component changes (C6, R44, R45) so the CB could be trimmed into range. **CONFIRMED** (voicing-board mod 1, effective from S/N **000300**, not 010600: C6 0.01→0.022, R44 390K→150K, R45 330K→100K).
  - Interrupt-clock capacitor C11 changed (0.068 → 0.047 µF, with an optional 0.039 µF C203 in parallel) because HD14584 hysteresis spread put the 1.9 ms interrupt out of range (main-board mod 1, S/N 000300). **CONFIRMED**. The interrupt rate, and with it the timing-jitter grid of §4, therefore varies from unit to unit.
- High-current sag: low-impedance loads on many outputs firing together "can cause DC source to drop" [SN p.14]. This is a fault mode, not a feature.
- **Hi-hat bleed and BD self-oscillation** at maximum decay are widely cited folklore. I found **no primary source** for either on a stock unit (**UNVERIFIED**). The BD long decay is specified at ~800 ms to −20 dB at maximum [SN p.14] (**CONFIRMED**: "LONG" column, decay measured to 1/10 Vpp). Leakage gating exists precisely to suppress bleed (Q74, see §6).

---

## 9. How software 808s usually extend the sequencer (NON-ORIGINAL features)

None of these exist on the TR-808. They are listed as targets for feature parity.

| Feature | Roland TR-808 plugin [RC808] | D16 Nepheton 2 [NEP] | Roland TR-8S / TR-08 [TR8S][TR08] |
|---|---|---|---|
| Steps / length | 16 steps per variation; **per-instrument LAST STEP** (polymeter) | up to **64 steps** (**CONFIRMED**, Nepheton 2 manual "Length – Sets pattern length, up to 64 steps"); step Scale 1/64 to 1/4 with Full/Dotted/Triplet modifier | TR-08 keeps 1st/2nd part (32); TR-8S per-track last step |
| Variations / patterns | **8 variations A–H**, chainable; MIDI notes 24–31 select A–H (**CONFIRMED**, [RC808] manual) | **4 banks (A–D) × 12 patterns = 48** internal patterns, selectable by 48 MIDI notes (*corrected from "4 × 12 plus extended/simple banks; sources disagree": the Nepheton 2 user manual (7 Mar 2024) describes only the 48-pattern bank*) | TR-08: 256 patterns with A/B (**CONFIRMED**, [TR08] "The TR-08 has 256 rhythm patterns, and two variations (A and B)") |
| Shuffle | global SHUFFLE × **per-instrument shuffle** amount (**CONFIRMED**) | per-pattern shuffle | shuffle (delays even 16ths) |
| Flam | per step; **9 flam types: 0 ms, then 20–48 ms in 4 ms steps** (**CONFIRMED**: "TYPE1: 0msec; TYPE2–9: 20–48 msec in 4 msec units") | per-step flam, delay set as % of step length | yes |
| Sub-steps / ratchets | per step: duplets, triplets, quadruplets (**CONFIRMED**) | substeps 2 / 3 / 4 with start/end volume (**CONFIRMED**) | sub step |
| Dynamics | global **TOTAL ACCENT** row (authentic) plus **WEAK** (soft) per-step hits | accented/unaccented velocity plus global accent boost | weak beats |
| Probability | — | — (randomizer) | per-step, sub-step and master probability (TR-8S fw 2.x; press coverage ties it to the v2.5 update, 2022. The version number is not checked against Roland release notes) |
| Extra voice params | per-voice GAIN (drives the circuit), PAN, TUNE (BD only) | Tune/Decay/extras on every voice; extra voices (17 total) | TR-08: extra decay/tune options |
| Outputs | Mix plus 11 sub-outs (BD, SD, LT/LC, MT/MC, HT/HC, RS/CL, CP/MA, CH, OH, CY, CB) (**CONFIRMED**, [RC808] multi-out table) | **16 stereo outputs** (*corrected from "12 vs 16; conflicting": the Nepheton 2 user manual says "It has 16 stereo outputs that Buses or the Master Output can be routed to"*) | — |
| Other | drag pattern to DAW as MIDI/audio; MIDI learn | tap record, randomizer, MIDI export, trigger outs, per-channel EQ/comp, FX buses | — |

Note map used by [RC808]: BD 35/36, RS/CL 37, SD 38/40, CP/MA 39, CH 42/44, LT 41/43, OH 46, MT 45/47, CY 49, HT 48/50, CB 51. **CONFIRMED** against the [RC808] manual table. *Corrected from "General MIDI note map":* it is GM-like but not GM. GM puts Cowbell on 56, while 51 is GM "Ride Cymbal 1". This is still a sensible default for mw808 because it matches Roland's plugin. Consider also accepting 56 for CB.

---

## 10. Design checklist distilled for mw808

1. **Sequencer core:** a tick engine at 24 ppqn × N (internally use 96 or 960 ppqn for flam and sub-steps). Step lengths of 8/4/6/3 ticks per pre-scale 1/2/3/4. Host-synced via PPQ position. Implement **1st/2nd PART (up to 32 steps), pre-scale, A/B/AB, 12 + 4 I/F, auto fill 2/4/8/12/16 / manual TAP, intro, and the "switch during beat 1 → start at beat 2" rule.**
2. **Rhythm Track:** 12 × 64 measures that overflow into the next track. Store the rhythm number only, with variation as a live control (authentic mode). Optionally store the variation as well (extended mode).
3. **Accent:** a per-step global AC row creating `Vtrig ∈ {4 V, 4–14 V}`; CB/CY/OH/CH remapped to 7–14 V; fed into the voice nonlinearities. Calibrate to the dB table in §3 (CY/OH/CH ≈ +6 dB, **CB ≈ +10.7 dB**). Extended mode: per-voice accent lanes and WEAK hits (non-original).
4. **Trigger pulse:** 1 ms excitation into each voice model. Never reset voice state on retrigger. CH chokes OH. Pairs (LT/LC, MT/MC, HT/HC, RS/CL, CP/MA) are a per-voice switch: authentic as global, optionally per-step in extended mode.
5. **Mix bus:** fixed internal mix gains (BD +3.1 dB, OH −4.7 dB, CH/CB/CY −3.3 dB, others ≈0 dB); a ≈48 Hz first-order HPF (recomputed 47.7 Hz; make the corner depend on which voices are routed out) and two ≈22 kHz first-order LPFs on the master sum; master gain 1.5×; optional op-amp-style saturation at about ±13 V equivalent (≈ +13 dB over nominal), placed at both the summer and the master stage, not as a diode clamp. Any voice routed to its own plugin output is **removed from the main mix** (authentic jack behaviour). The individual outs skip the HPF.
6. **Trigger outs:** CB, CP, AC as MIDI-out notes (and optionally audio pulse outs), each fired by its row regardless of LEVEL.
7. **Timing:** sample-accurate by default, with an optional "vintage" 0–1.9 ms per-step onset jitter and slow tempo drift.
8. **Extended sequencer (non-original, for parity with Nepheton 2 / Roland plugin):** up to 64 steps, per-instrument last step, global plus per-instrument shuffle, flam (0, 20–48 ms), sub-steps 2/3/4, weak hits, per-step probability, 8 variations, pattern-to-MIDI drag-out.

---

## Verification log

Fact-check pass of 2026-10-02 by an independent checker. Method: primary-source images were re-read directly, not via the researcher's notes, and every R/C-derived figure was recomputed.

**Primary sources re-read (scanned images, rendered at 200–300 dpi):**
- [SN] TR-808 Service Notes, 15 Jun 1981 (project scratchpad `r01/sm.pdf`):
  - p.1 spec block: storage 1–32 steps, 64 × 12; tempo 40–300; HI 6 Vpp/1 kΩ; LO 0.6 Vpp/3 kΩ; trigger +15 V/20 ms/1 kΩ; accent 0–10 dB.
  - p.3 circuit description, pin table and Fig. 2: 1.9 ms INT, 7.6 ms switch scan, PI-2 trigger pulses run continuously, PD/PE/PF instrument pin map.
  - p.4 RAM (µPD444C ×4) and Trigger Gate text: 10 µs → ~1 ms, IC6, Q31–Q34, 4 V unaccented, 5–15 V base and 4–14 V trigger, 7–14 V for CB/CY/OH/CH via ½ IC2.
  - p.5 START/STOP and tempo clock: 9 ms start stop, "24 clocks" per quarter, 8.3–65 ms, "400–300" misprint, ÷2 T-FF IC2A; muting Q13 and power detector timings; BD text giving a 16 ms inherent period and 4 ms Q43.
  - p.6 voice descriptions: SW8 C77/R224; D80/D81; Q74 leakage gating; RS/CL routing; CP/MA; CB; CY six Schmitt oscillators; OH choke via Q23/R173.
  - p.13 schematic, VG3116-140 master section and JACK-B OP3116-110: all 11 mix resistors, C17 1/50, R75 33k ∥ C18 220p, C19 1/50, VR1 50k(A), C23 1/50, R77 22k, R76 33k ∥ C22 220p, Q13 2SK30A mute, D9/D10 to ±B5, C1 10/25 BP, R14 1k, R13 27k, R12 3.3k.
  - p.14 adjustment table (IFT-1 500 kHz; TM-1 1.9 ms; TM-2 8.33 ms; 65 ± 5 ms check; TM-4 130 mV rms; TM-3 CP 6 Vpp; CB 1.85/1.25 ms) and the full voice check table (all 16 rows, cell by cell).
  - p.15 design changes: main mods 1–9 and voicing mods 1–2, with serial numbers and part values.
- [OM] TR-808 Operation Manual (scratchpad `tr808_om.pdf`; PDF page = manual page + 2):
  - p.1: "divided into 3, 4, 6, 8 ... steps"; 768 measures; 12 × 64.
  - p.11: switching rule, verbatim; 32 patterns; BR-3 Rock; BR-12 all voices.
  - p.12: fill-in MANUAL and AUTO behaviour; intro not counted; AB "always beginning in the A Mode", but B→AB mid-play continues B, A, B.
  - p.15 Fig. 5: 12 steps at PS3 in 3/4.
  - p.16 Fig. 6: 12 steps at PS1 per 4/4 bar; A/B share pre-scale and step count; clearing in AB erases both.
  - p.18: after CLEAR, 1st = 16 and 2nd = 0; Fig. 8 20-step splits; 16 × 2 = 32.
  - p.19: track auto-continues into the next track; variation not stored.
  - p.22: full spec page, DIN pins 1/2/3.
- [W-BD] (scratchpad `bd.txt`): §3 1 ms pulse of 4–14 V; §4 pulse-shaper edge amplitudes and the 0.71 V diode; ≈49.5 Hz computed centre vs the 56 Hz table.
- [W-CY] (scratchpad `cym2.txt`): six HD14584 oscillators; #5–6 at 800/540 Hz; Vct "narrowed to 7–14 V"; "effect of the varying accent level is complex".
- [RC808] Roland TR-808 Software Rhythm Composer manual (gear4music PDF, text-extracted): flam TYPE1 0 ms, TYPE2–9 20–48 ms in 4 ms units; sub-step duplets/triplets/quadruplets; WEAK; A–H variations; per-instrument LAST STEP and shuffle; mix plus 11 sub-outs; note map including 24–31 for variation select.
- [TR08] Roland TR-08 owner's manual (static.roland.com PDF, text-extracted): pre-scale legend 1 = 8th triplet, 2 = 16th triplet, 3 = 16th, 4 = 32nd; 256 patterns with A/B; 40–300 BPM.
- Nepheton 2 user manual, 7 Mar 2024 (scratchpad `r01/neph.txt`): length up to 64 steps; Scale 1/64–1/4 with dotted/triplet; 4 banks × 12 = 48 patterns; 16 stereo outputs; flam delay as % of step; sub-step ×2/×3/×4; trigger outs; randomizer.

**Recomputations (all agree with the document unless noted):**
- Tempo: 24 × 8.33 ms = 200 ms per quarter = 300 BPM. ✔ 24 × 65 ms = 1.56 s = **38.5 BPM** (document said ≈40; corrected). Fastest step 3 × 8.33 = 25 ms. ✔
- Accent dB column: 20·log10 of each Accent/Normal pair. All rows ✔ (+9.1, +10.5, +10.7, +11.3, +10.1, +4.4, +6.0).
- Mix gains: 33/(22+1), 33/(33+1), 33/(47+1), 33/(56+1) = +3.14, −0.26, −3.25, −4.75 dB. ✔
- Master gain 33/22 = 1.5 (+3.52 dB). ✔ LPF 1/(2π·33k·220p) = 21.9 kHz. ✔
- Master-sum HPF: R_par of (R_mix + 1k) over 11 voices = 3.333 kΩ; f_c = 1/(2π·3.333k·1 µF) = **47.7 Hz**. ✔ BD loss at 56 Hz = −2.4 dB; at 49.5 Hz = −2.9 dB. With BD routed out: 3.90 kΩ → 40.8 Hz.
- C19 corner: 3.2 Hz with a 50k load only, but 10.4 Hz at full volume (50k ∥ 22k). Corrected from "≈3 Hz". C23/R77: 7.2 Hz. ✔
- LO divider: 3.3/30.3 → −19.26 dB. ✔ LO output impedance 3.3k ∥ 27k = 2.94 kΩ. ✔
- CB trims: 1/1.85 ms = 540.5 Hz and 1/1.25 ms = 800 Hz. ✔ Table periods to Hz (BD 18 ms → 55.6 Hz, etc.) ✔
- Jitter: 87 samples = 1.97 ms at 44.1 kHz, or 1.81 ms at 48 kHz.

**Corrections made:**
1. Tempo minimum ≈40 → ≈38.5 BPM at the 65 ms check point.
2. D9/D10 are output-protection diodes, not the clip mechanism. Clipping is op-amp saturation, possible at IC4a as well as IC4b.
3. The C19 HPF corner depends on the volume setting (≈3–10 Hz), not a fixed ≈3 Hz.
4. CB accent: the 7–14 V trigger narrowing does **not** cap CB at +6 dB. The table shows +10.7 dB.
5. The cowbell mod is effective from S/N 000300, not 010600. Added the C11 interrupt-clock mod (S/N 000300).
6. Nepheton 2: 48 patterns (4 × 12) and 16 stereo outputs per the manual. The earlier "conflicting" notes are resolved.
7. The [RC808] note map is GM-like, not GM: CB is on 51, while GM puts Cowbell on 56.
8. RS/CL routing refined: RS uses both resonators (IC20a via IC20b, and IC21) into VCA Q62.

**Remaining uncertain (not resolvable from the available primary sources):**
- The 47.7 Hz master HPF is confirmed from the schematic only and has not been measured on hardware. Capacitor tolerance gives ≈40–60 Hz.
- The CP accent cell "2 Vpp" in [SN p.14] is a probable misprint. The CP normal 6 Vpp agrees with the TM-3 trim target.
- The HT row in [SN p.14] is identical to the LC row (165/185/220 Hz), which could be a table copy error.
- BD frequency: 56 Hz (table) vs 49.5 Hz ([W-BD] calculation) vs 62.5 Hz ([SN p.5] text, "16 ms").
- The exact transfer from 4–14 V to 7–14 V for CB/CY/OH/CH is unknown; the linear map is an assumption.
- The PI-2 pulse grid (per tempo clock or per step) is unknown and makes no audible difference.
- The Innerclock 808 jitter figure comes only from secondary sources; the original page now returns 404. The "sequential voice firing" forum claim was not found on the cited [MBX] page.
- Still unverified from the original file: tempo-drift magnitude, master headroom (the ≈ +13 dB figure is an estimate), noise-floor spec, hi-hat bleed, BD self-oscillation, step-key colour mapping, DIN clock toggling while stopped, and the R100/C23/R102/C27 values behind the 1 ms monostable.
- TR-8S probability firmware version: "2.x" is supported by press coverage that ties it to v2.5. Not checked against Roland's release notes.

**Web sources consulted in this pass:**
- https://forum.midibox.org/t/mb808-tighter-as-a-tr808/14762 ("the tested 808 is 87")
- https://greatsynthesizers.com/en/general/timing-the-litmus-test/ (Innerclock method: 120 BPM, 48 kHz)
- http://www.innerclocksystems.com/New%20ICS%20Litmus.html (404)
- https://static.roland.com/assets/media/pdf/TR-08_eng03_W.pdf
- https://r2.gear4music.com/media/68/683758/download_683758.pdf
- https://synthanatomy.com/2022/08/roland-tr-8s-v2-5-tr-6s-v1-5-firmware-updates-brings-new-drums-fx-and-more.html
- https://www.gearnews.com/new-roland-tr-8s-tr-6s-firmware-brings-more-drums-fx-and-probability

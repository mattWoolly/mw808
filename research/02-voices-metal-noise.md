# 02 — TR-808 metallic and noise voices (CY, OH, CH, CB, CP, MA, noise source)

Scope: circuit-level facts for the voices that use the shared square-wave bank or the shared
noise source, written so DSP can be built straight from it. Values come from the Roland service
notes unless marked otherwise.

## Sources and notation

| Tag | Source | URL |
|---|---|---|
| **[SM]** | Roland, *TR-808 Service Notes*, 1st ed., 15 June 1981 (second printing). Scanned schematics, circuit description (p.5–6) and the adjustment/"checking voices" table (p.14). Voicing-board schematic is on p.13, CP/MA on p.9, noise generator on p.8. | http://manuals.fdiskc.com/flat/Roland%20TR-808%20Service%20Manual.pdf |
| **[WCY]** | K. J. Werner, J. S. Abel, J. O. Smith, "The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model", ICMC/SMC 2014 | https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf |
| **[WCB]** | Werner, Abel, Smith, "More Cowbell: a Physically-Informed, Circuit-Bendable, Digital Model of the TR-808 Cowbell", AES 137th Conv., paper 9207, 2014 | https://michaelzfreeman.org/wp-content/uploads/2018/05/More-cowbell-a-physically-informed-circuit-bendable-digital-model-of-the-TR-808-cowbell.pdf (also https://www.researchgate.net/publication/267629988) |
| **[BTX]** | Baratatronix, "Roland TR-808 Cowbell Bandpass Filter Frequency" (evaluates [WCB]'s transfer function) | https://www.baratatronix.com/blog/808-cowbell-bandpass-filter-frequency |
| **[SOS-CY]** | G. Reid, Synth Secrets "Practical Cymbal Synthesis", Sound On Sound, Jul 2002 | https://www.soundonsound.com/techniques/practical-cymbal-synthesis |
| **[SOS-CB]** | G. Reid, Synth Secrets "Synthesizing Cowbells & Claves", Sound On Sound, Sep 2002 | https://www.soundonsound.com/techniques/synthesizing-cowbells-claves |
| **[NEPH]** | D16 Group, *Nepheton 2 user manual* (synth-view parameter list) | https://m.d16.pl/pub/manuals/Nepheton%202-manual-gb.pdf |
| **[CHA]** | Cherry Audio / Misfit Audio "Transistor 808 Hats" docs | https://docs.cherryaudio.com/misfit-audio/transistor-808-hats |

Labels used below:
- **DERIVED** means I calculated it from [SM] component values, using ideal op-amps and standard filter formulas or a numerical nodal solve. Two checks support the method: my numbers for the cymbal band-pass filters (3435 Hz and 7096 Hz) match Werner's 3440 and 7100 Hz, and my result for the 3rd-order cymbal high-pass (peak about 10.4 kHz) matches his "around 10500 Hz".
- **UNVERIFIED** means I could not confirm it from a primary source.

Fact-check tags (added by an independent verification pass, 2026-10-02; see "Verification log" at the end):
- **CONFIRMED**: re-read from the primary source (the [SM] scan, [WCY]/[WCB] PDFs, [NEPH] PDF) and/or recomputed from schematic part values with an independent nodal (MNA) solver, and the result agrees.
- **CORRECTED**: the earlier value was wrong; the note says what it was and why.
- **CONFLICTING**: sources or reasonable models disagree; the alternatives are listed.
- **UNVERIFIED**: still not confirmed by a primary source.
- [SM] note: the scan's front page says "First Edition, Second Printing (July 12, 1983 E2)" and the footer says "3rd Printing June 1986 C-2"; every page is dated JUN. 15, 1981. The page numbers cited in this file match the scan.

Part references (R/C/Q/VR/TM) are voicing-board numbers from [SM] p.13 unless the text says main board.

---

## 0. TL;DR for the DSP designer

1. **One bank of six free-running square oscillators** (one HD14584 hex Schmitt inverter at about 5 V) feeds CY, OH and CH. Its two trimmable oscillators also drive the CB directly. The oscillators run all the time and are **never phase-reset on a trigger**. Frequencies: **205.3, 304.4, 369.6, 522.7, 540 (trim), 800 (trim) Hz**. Duty cycle is 47.98%. [WCY][SM] **CONFIRMED** (R/C values re-read from [SM] p.13; all six recomputed with f = 2.0699/(RC)).
2. **Cymbal**: the six oscillators are summed equally. Two parallel bridged-T band-passes follow: **BP1 at about 3.44 kHz** and **BP2 at about 7.1 kHz**, both Q about 6. **CONFIRMED** (nodal solve: 3435 Hz Q 5.96, 7094 Hz Q 6.03).
   - Three bands come out of them, each with its own swing-type VCA and envelope: low band from BP1; mid and high bands from BP2.
   - Each band then has its own high-pass (2nd order at 2.5 kHz Q 1, 2nd order at 8.8 kHz Q 1, and 3rd order resonant at about 10.4 kHz).
   - A passive tone-crossfade network and an output stage follow. [WCY]
3. **OH and CH** take the **BP2 (7.1 kHz) output only**. Each runs through its own swing-type VCA and a resonant 2nd-order high-pass (**OH about 7.8 kHz, CH about 11.7 kHz, both Q about 2.5**, DERIVED). **CONFIRMED** (parts re-read; fc 7.83 / 11.75 kHz, Q 2.51 recomputed).
   - CH has a fixed decay of about 50 ms. **CONFIRMED** [SM p.14]
   - OH decay runs from about 90 to 600 ms. **CONFIRMED** [SM p.14]
   - **A CH trigger chokes OH**: transistor Q23 dumps the OH envelope capacitor for as long as the CH envelope is up. [SM] **CONFIRMED** (SM p.6 text + schematic R173/Q23/R134).
4. **Cowbell**: the 540 Hz and 800 Hz oscillators each go through their own swing-type VCA. One shared envelope drives both VCAs.
   - The envelope has a sharp attack and **two-stage decay**: a fast drop of about 10 dB (τ about 10 ms), then a tail (τ about 70 ms). Both figures are DERIVED. **CONFLICTING (model-dependent)**: the decoupled estimate gives τ1 10.6 ms / τ2 ≈ 70 ms; solving the coupled C9/C34 network with the ≈50k VCA load from t = 0 gives τ1 ≈ 7.9 ms / τ2 ≈ 99 ms. Both reach −20 dB at ≈ 50–55 ms, matching [SM]'s 50 ms.
   - A 4th-order band-pass follows, with **peak about 870–880 Hz** and Q about 5. This is not the "2.64 kHz" figure that circulates online. [WCB][BTX] **CONFIRMED** for frequency (nodal solve: magnitude peak 874 Hz; [BTX] 881 Hz geometric centre). **Q CORRECTED** to ≈ 4.6–4.8 per input (see §4.4).
5. **Clap**: white noise goes through a band-pass at about **1.27 kHz, Q about 1.5** (DERIVED). **CORRECTED from "1.0 kHz, Q 1.4"**: the input network R332‖C127 feeds the bridged-T *midpoint* (the R333 node), not the inverting input, so the stage is a standard multiple-feedback band-pass ([SM] p.9, re-read). From there two VCAs run in parallel:
   - (a) an exponential VCA driven by a **sawtooth "multi-clap" envelope**: bursts at about **0, 11, 22, 30 ms** (the 4th at the end of the **30 ms** gate), and then a free decay; **CONFIRMED** ([SM] Fig.13, pixel-measured 0 / 10.8 / 22.1 / 29.7 ms);
   - (b) a second transistor VCA (Q70) with a slower, delayed "reverb" envelope. [SM] **CORRECTED**: Q70 is an ordinary common-emitter stage whose *base bias* is the envelope; it is not a swing-type VCA (its collector goes to the +15 V rail through R363 10k).
6. **Maracas**: the white noise is gated and then goes through a resonant 2nd-order **high-pass at about 10.6 kHz** (DERIVED). The envelope is short; [SM] lists 25–35 ms. **CONFIRMED** (HP fc 10.62 kHz, Q 2.27 recomputed; 25/35 ms re-read).
7. **Noise source**: a reverse-biased base-emitter junction of a 2SC828 transistor, amplified about 38–214× and trimmed to **130 mV rms**. Later production units roll it off above about **7.2 kHz**. A ~270 Hz low-passed copy ("PN") goes to the toms and congas. [SM] **CONFIRMED** (all parts re-read from [SM] p.8; PN fc 273 Hz, Q 0.74, −3 dB 284 Hz).
8. **Swing-type VCAs** are deliberately biased so they clip asymmetrically. They are the main source of the metallic sizzle, so model them as nonlinear. [SM p.5][WCY §8]
9. **Oversample** (Werner used 4× for the cymbal and 8× for the cowbell). The hat and cymbal paths high-pass at 7–12 kHz and include a resonant peak near 10.4 kHz, so aliased square-wave partials in the 10–20 kHz band would be clearly audible.

---

## 1. Shared six-oscillator bank (CY / OH / CH / CB)

### 1.1 Circuit
Each oscillator is one HD14584 Schmitt-trigger inverter with a resistor from output to input and a capacitor from input to ground (an RC relaxation oscillator). [WCY §3][WCB §3]

With supply V_DD = 5 V and the HD14584's typical thresholds (V_OH = 5 V, V_OL = 0 V, V_T+ = 2.7 V, V_T− = 2.1 V) [WCB fn.3]:

- t_charge = RC · ln((V_OH − V_T−)/(V_OH − V_T+)) = 0.2318 RC
- t_discharge = RC · ln((V_OL − V_T+)/(V_OL − V_T−)) = 0.2513 RC
- So **f ≈ 2.070 / (R·C)**, **duty D = 47.98%** and amplitude A = 5 V p-p. [WCY eqs 1–3][WCB eqs 2–5]
- Duty cycle is independent of R and C.
- Edges are under 250 ns, so treat them as instantaneous. [WCY fn.8]

The 5 V rail for the HD14584 comes from Q9, an emitter follower biased by R61 10k / R60 5.6k from +15 V, with C12 100 µF. [SM p.13][WCY Fig.1]

### 1.2 Oscillator table (component values from [SM] p.13 and [WCY] Fig.1; frequencies from [WCY] §3)

| # | Inverter pins | R | C | Nominal f | Notes |
|---|---|---|---|---|---|
| 1 | 5→6 | R40 560k | C1 0.018 µF | **205.3 Hz** | fixed |
| 2 | 3→4 | R41 560k | C2 0.01 µF | **369.6 Hz** | fixed |
| 3 | 1→2 | R42 680k | C3 0.01 µF | **304.4 Hz** | fixed |
| 4 | 9→8 | R43 220k | C4 0.018 µF | **522.7 Hz** | fixed |
| 5 | 11→10 | R45 100k + TM2 220k(B) | C5 0.018 µF | range 359.4–1149.9 Hz, factory **800 Hz** (period 1.25 ms at TP7) | "COWBELL FREQ.2"; also the CB high tone |
| 6 | 13→12 | R44 150k + TM1 220k(B) | C6 0.022 µF | range 254.3–627.2 Hz, factory **540 Hz** (period 1.85 ms at TP6) | "COWBELL FREQ.1"; also the CB low tone |

- Werner's text gives 205.3, 369.6, 304.4 and 522.7 Hz in oscillator order #1–#4. The commonly quoted list (205.3, 304.4, 369.6, 522.7, 540, 800) is the same set sorted, so it is **CONFIRMED**. [WCY §3]
  - Re-check: every R and C in this table was re-read from the [SM] p.13 scan, and f = 1/(RC·(ln(2.9/2.3) + ln(2.7/2.1))) = 2.0699/(RC) reproduces 205.3, 369.6, 304.4, 522.7, 359.4–1149.9 and 254.3–627.2 Hz exactly. **CONFIRMED.** The trims sit at R ≈ 143.7k (osc5, 800 Hz) and ≈ 174.2k (osc6, 540 Hz).
- The 540 and 800 Hz figures are factory trims, confirmed by the [SM] adjustment table: "CB (COW BELL) FREQUENCY — TP-6, TM-1, 1.85 ms/cycle; TP-7, TM-2, 1.25 ms/cycle". The [SM] check table lists "CB H 1.25 ms (800), L 1.85 ms (540)". [SM p.14] **CONFIRMED** (re-read).
- Musically, 540 Hz is C5 + 55 cents and 800 Hz is G5 + 35 cents, an out-of-tune fifth of 6.80 semitones. [WCB §1] **CONFIRMED** (recomputed: +54.5 c, +35.0 c, 6.804 st).
- Early units (production lots 1–2; [WCY] fn.9 says "prior to serial #000300") used different oscillator 5/6 parts: R44 = 390k, R45 = 330k. The schematic note also swaps the 1.25 and 1.85 ms labels for those lots. The early-lot capacitor values are **UNVERIFIED**. (Clarified: with the labels swapped, early osc6 targets 1.25 ms / 800 Hz and early osc5 targets 1.85 ms / 540 Hz. With the late capacitors, 390k + TM1 on 22 nF tops out at 241 Hz and 330k + TM2 on 18 nF at 349 Hz, so the early capacitors must have been different. [WCY] fn.9 says so: "different values for the resistors and capacitors".) [SM p.13]
- Unit-to-unit variation is real. Robin Whittle attributes individual cymbal character to component tolerance, especially in the trimmed oscillators 5/6. [WCY fn.7] A useful product feature is a small per-instance random detune, or a "unit" seed.

### 1.3 Mixing into the cymbal and hat filters
- All six outputs feed through equal **120 kΩ** resistors (R35, R37, R39, R46, R48, R50) into one node, which has **R53 = 1 kΩ** to ground. [WCY Fig.1, eq.4]
- Per oscillator: gain ≈ (1k ‖ 24k)/(120k + 1k ‖ 24k) ≈ 1/126 (−42 dB), or about 40 mV p-p each, so the summed node V_sum is at most about 0.24 V p-p. **All six have equal weight.** DERIVED. **CONFIRMED** (recomputed 0.00794; the six 120k resistors re-read from [SM]). The BP input networks (C10+R52, C11+R55) load this node slightly; this is ignored here.
- R53 is a well-known bend point. Werner notes that replacing it with a pot "dramatically alters the timbre", because it sets how hard the VCAs clip. [WCY §12–13]
- The cowbell does **not** use this mix. Oscillators 6 and 5 drive the CB VCAs directly through R62 33k / R63 2.2k and R64 33k / R65 2.2k, which is about 0.31 V p-p each. [SM p.13] **CONFIRMED** (2.2/35.2 × 5 V = 0.31 V unloaded). [WCB] fn.7 notes that the Q14/Q15 base current is not negligible, so the real division is lower ([WCB]'s SPICE-fitted coupling: −28.59 dB, HP at 5.273 Hz).

### 1.4 DSP notes
- Run the six oscillators continuously from plugin start (or from a seeded random phase). Do not reset them on note-on, because the real circuit's partial phases differ on every hit.
- Use 47.98% duty and band-limited squares (polyBLEP or similar), or a naive square at ≥4× oversampling as Werner did. Werner argues the downstream filtering masks aliasing [WCY fn.8], but the hat paths high-pass hard at 7–12 kHz, so be conservative.
- Optional extras seen in commercial clones: a global "HH Frq" or pitch control that scales all six oscillators [NEPH][CHA], and voltage-starve / per-oscillator mute bends [WCY §12].

---

## 2. Cymbal (CY)

### 2.1 Signal flow ([WCY] Fig.1/2, [SM] p.6 "CY", [SM] p.13)

```
6 osc ──R(120k)×6──┬── V_sum (R53 1k to gnd)
                   ├─► BP#1 (IC3a, ~3.44 kHz, Q≈6) ─► C46 22n ─► VCA#1 (Q18) ─► HP#1 (2nd-ord, Q25 follower) ─► LPF R123/C57 ─► R121 10k ─┐ node B
                   └─► BP#2 (IC3b, ~7.1 kHz, Q≈6) ─┬─► C44 22n ─► VCA#2 (Q17) ─► HP#2 (2nd-ord, gain 2) ─► C56/R120 10k ───────────────┘ node B ─R129 15k─┐
                                                   ├─► C42 22n ─► VCA#3 (Q16) ─► HP#3 (3rd-ord, gain 2.77) ─► C55/R112 22k ─► node A ─R119 22k──┤
                                                   └─► (to OH / CH VCAs via C67, C71 22n)                                                       ├─► V_tone ─► C90 ─► IC6 out stage (LEVEL VR6) ─► CY out
     Tone VR4 20k(B): pin1=node A, pin3─R125 2.2k─node B, wiper=gnd
```

[SM] p.6 describes the split: "separated into high and low range components by two filters composed of IC3. The high range component from pin 7 of IC3 is further separated into two frequency ranges. The output of the gate Q16 has the highest frequency component … Its decay time is short. The output of Q17 is in a frequency range slightly lower … and its decay time is controllable. These three signals … are outputted with their level ratio controlled by VR4."

### 2.2 Band-pass filters (bridged-T in an inverting op-amp's feedback; IC3 = µPC4558)
Feedback network: two equal capacitors in series from output to the inverting input, a small resistor from their midpoint to ground, and a large resistor bridging output to input. Input is a series R–C into the inverting node. [WCY §4, eq.5][SM Fig.11]

[SM] Fig.11 gives the generic formulas f = 1/(2π√(R1R2C1C2)) and Q = √(R2/R1) / (√(C1/C2) + √(C2/C1)).

| | Input | Feedback T | f0 | Q | Peak gain |
|---|---|---|---|---|---|
| BP#1 | C10 3.3 nF + R52 33k | C13 = C14 = 6.8 nF, R56 560 Ω, R57 82k | **3440 Hz** [WCY]; 3435 Hz DERIVED | **≈6.0** DERIVED | ≈ +23 dB DERIVED |
| BP#2 | C11 1 nF + R55 22k | C15 = C16 = 3.3 nF, R58 560 Ω, R59 82k | **7100 Hz** [WCY]; 7096 Hz DERIVED | **≈6.1** DERIVED | ≈ +24 dB DERIVED |

**CONFIRMED.** Parts re-read from [SM] p.13 and [WCY] Fig.1. An independent MNA solve gives BP#1 a 3435 Hz peak, −3 dB 3140–3713 Hz, Q 5.96, +23.0 dB, and BP#2 a 7094 Hz peak, −3 dB 6510–7683 Hz, Q 6.03, +24.1 dB.

These filters strongly favour upper overtones of the squares and suppress the fundamentals. In the time domain, every edge of any oscillator "kicks" the resonator, which then rings. [WCY §4, Fig.5]

### 2.3 Trigger and attack smoother
- The CPU ANDs the instrument bit with the common trigger V_ct. The result is a **1 ms pulse** whose amplitude is the accent level: **7–14 V**, set by the global ACCENT pot (main-board VR3). [WCY §5][WCB §4]
- Attack smoother (Q19, R84 10k, R85 100, R86 100, C35 47 µF): model it as a one-pole low-pass on (V_trig − 0.7258 V) with **τ = 0.1024 ms**. [WCY §6, eq.6] **CONFIRMED** (re-read: V_BE = 0.7258, τ = 1.0244·10⁻⁴ s; parts re-read from [SM]). The 7–14 V range and 1 ms width are **CONFIRMED** as [WCY] §5 / [WCB] §4 statements citing [SM]. The [SM] itself only gives the panel "Accent level 0dB–10dB" (p.1).

### 2.4 Envelope generators ([WCY] §7; component values from [WCY] Fig.1 / [SM] p.13)
Diodes are modelled as ideal switches with V_on ≈ 0.59 V. Each envelope is a peak-hold capacitor: it charges almost instantly during the 1 ms trigger and then discharges.

- **EG#1 (the DECAY knob)**: D8 charges C41 1 µF, which discharges through R93 470k in series with VR2 2 MΩ(B), the CY DECAY knob wired as a variable resistor.
  - τ = (470k + k·2M)·1 µF = **0.47 s → 2.47 s**. DERIVED from the schematic. Werner's release equation has the same form. **CONFIRMED** from the [SM] p.13 scan: R93 470k goes to VR2 pin 3, the wiper is tied to pin 3, and pin 1 is grounded, so the two are in series and FCW gives maximum resistance. Note that [WCY] eq.9 as printed reads "VR2·k·R93·C41" (a product, dimensionally wrong); read it as (VR2·k + R93)·C41. [WCY]'s text also names the diode D6 in one place, but the schematic shows D8.
  - Q20 buffers it as an emitter follower (V_BE ≈ 0.62 V).
  - Output EG#1.2: R105 33k into C45 2.2 µF, a high-shelf/low-pass smoothing with τ ≈ (R104 ‖ R105)·C45 ≈ 29 ms. This feeds VCA#1 (low band) through R104 22k.
  - Output EG#1.3: divider R92 33k / R89 10k (×0.23). This feeds VCA#2 (mid band) through R91 33k.
- **EG#2 (fixed)**: D7 charges C40 1 µF, then R88 33k into C39 0.47 µF, feeding VCA#2 through R90 33k. Rough discharge τ ≈ 1.47 µF × (66k plus VCA load) ≈ 0.1 s. UNVERIFIED (load-dependent).
  - Added: only C40 is charged by the trigger, and C39 fills through R88. **EG#2 at the VCA therefore has a delayed peak, not an instant attack.** A linear two-capacitor solve (R90 into roughly 0 V) gives τ ≈ 6.9 ms and 75 ms, peaking at ≈ 0.35 × V(C40) at ≈ 18 ms. [WCY] Fig.7 (SPICE and model) shows V_EG#2 peaking at ≈ 5.5 V around 25 ms and falling to about 3 V by 0.1 s. **CONFIRMED in shape** against [WCY] Fig.7.
- **EG#3 (fixed, shortest band)**: D6 charges C38 1 µF, with R87 22k into C37 2.2 µF, feeding VCA#3 (high band) through R94 39k. Rough τ ≤ 3.2 µF × 39k ≈ 0.12 s. UNVERIFIED.
  - Added: R87 + C37 is a *series* RC shunt across C38, the same "abrupt initial drop" trick as the cowbell's R82/C34. Decoupled estimate: the level falls toward C38/(C38 + C37) = 0.31 (−10 dB) with τ ≈ 22k·(1 µ‖2.2 µ series) ≈ 15 ms, then tails. A coupled linear solve with the 39k load gives τ ≈ 12 ms and ≈ 160 ms. [WCY] Fig.7 shows V_EG#3 peaking at ≈ 12.5 V at about 0.5–1 ms, about 3 V by 50 ms, and about 1.5 V by 0.2 s. The tail value stays **UNVERIFIED** (VCA-load dependent).
  - [SM] and [SOS-CY] both say the highest band decays fastest and the DECAY knob only affects the lower bands. In practice EG#1 sets the low band entirely and part of the mid band. **CONFIRMED** ([SM] p.6: Q16 "decay time is short", Q17 "decay time is controllable"; schematic: EG#1 feeds VCA#1 via R104 and VCA#2 via R91).
- **EG#1.2 also has a slow swell** (added): C45 2.2 µF fills through R105 while EG#1.1 decays slowly. [WCY] Fig.7 shows V_EG#1.2 peaking at ≈ 6 V around **0.15–0.2 s** after the hit, and EG#1.3 sitting at a ≈ 2.5 V plateau from about 1 ms to 0.3 s. The low band therefore swells in rather than starting at full level. This is design-critical for the cymbal's "bloom". **CONFIRMED** from [WCY] Fig.7.

**Overall measured CY decay** (time to fall to 1/10 of peak p-p, i.e. −20 dB) [SM p.14]: **350 ms (DECAY min) / 800 ms (12 o'clock) / 1200 ms (max)**. **CONFIRMED** (re-read; the table columns are SHORT/MID/LONG, the text says the controls are "set at 12 o'clock" for MID, and the figure defines decay as the time to 1/10 Vpp).

### 2.5 Swing-type VCAs, the sizzle ([SM] p.5 Fig.12; [WCY] §8)
- Circuit: an NPN common-emitter stage (Q16/Q17/Q18, emitter R 100 Ω). The base is AC-coupled (22 nF) and biased only by a 2 MΩ chain (two 1 MΩ resistors) from +15 V.
- The collector has **no V_CC**. It returns through a diode (D5/D11/D12) and a resistor (39k / 33k‖33k / 22k) to the **envelope voltage**. Shunt caps of 1.5 nF sit on both sides of the diode in VCA#2 and #3.
- [SM] p.5: "The swing type VCA … is used to generate metalic sound (noise). This circuit features its output waveform having many high harmonic components to provide ringing metalic sound."
- [WCY] §8.1 calls the bias scheme "inherently unstable … leads to clipping — a wild swing of the output between V_EG and V_lowerEdge, apparent intended behaviour."
- Behavioural model [WCY] §8.2–8.3:
  - V_est = V_env − R_c · I_ES · (e^{V_BE/V_T} − 1)
  - V_lowerEdge = α0·e^{−β0·V_env^γ0} + α1·e^{−β1·V_env^γ1} + α2, with (α, β, γ) = (−1.396, 1.063, 1.000) and (0.825, 0.837, 1.447), and α2 = 0.566. **CONFIRMED** ([WCY] Table 1 re-read).
  - The output is a soft clip of V_est between V_lowerEdge and V_env: V_out = (V_est − V_env) / (1 + |(V_est − V_env)/(V_env − V_lowerEdge)|^α)^{1/α} + V_env, with **α = 3.5**. **CONFIRMED** ([WCY] eq.15; note the absolute value inside the bracket).
  - In plain terms: the output can never exceed the envelope (upper rail = V_env), and below it is floored by a diode/saturation edge that also tracks the envelope. Small inputs are amplified roughly exponentially, so the waveform rectifies and clips hard, which adds dense high harmonics.
  - The diode gates the output completely off once the envelope is low.
- Implementation suggestion: treat each swing VCA as a memoryless asymmetric soft-clipper whose two rails are functions of the envelope. Run it at ≥4× oversampling.

### 2.6 High-pass filters per band ([WCY] §9; values from Fig.1)
| | Topology | Parts | Response |
|---|---|---|---|
| HP#1 (low band) | 2nd-order unity-gain Sallen-Key, Q25 emitter-follower buffer | C48 = C59 = 1.5 nF, R124 22k (feedback), R127 82k (to ground) | **fc ≈ 2.50 kHz, Q ≈ 0.97** DERIVED. Followed by C58 10 nF (DC block; omitted earlier), R123 100k / C57 3.3 nF (first-order LP, ≈ 482 Hz) and R121 10k into the tone node. |
| HP#2 (mid band) | 2nd-order Sallen-Key, op-amp gain 2 (R114 = R115 = 39k) | C51 = C52 = 1 nF, R107 18k (feedback), R113 18k | **fc ≈ 8.84 kHz, Q ≈ 1.0**, +6 dB passband DERIVED |
| HP#3 (high band) | 3rd-order (C49 3.3 nF / R106 6.8k first-order section, then a Sallen-Key with C53 = C54 = 1 nF and R108 = R109 = 18k), op-amp gain 1 + 39k/22k = 2.77 | | **Resonant peak ≈ 10.4 kHz** (+23 dB re a 1 V source, ideal-source DERIVED; [WCY] says "around 10500 Hz"). Steep below. |

Note: the VCA output nodes are not stiff sources (R94 39k pull-up on VCA#3). In my numerical check, source impedance lowers the HP#3 resonance peak considerably. Werner models the VCAs as ideal sources, so expect to tune by ear or against reference audio.

**CONFIRMED** (all parts re-read from [SM] p.13; independent MNA). HP#1: 2.50 kHz, Q 0.965, +1.0 dB bump. HP#2: 8.84 kHz, Q 1.00, +6 dB passband, +7.3 dB peak near 12.5 kHz. HP#3 with an ideal source: +23.0 dB peak at 10.43 kHz. HP#3 fed through 39k: −0.7 dB at about 11.4 kHz, so the resonance is almost fully damped. The real source impedance lies between these two cases and is time-varying (diode/VCA state). This is **CONFLICTING by model**; Werner's ideal-source "≈10500 Hz" is the upper bound on peakiness.

### 2.7 Tone stage and output
- Tone ([WCY] §10, [SM] p.13):
  - High band (from HP#3): C55 10 nF and R112 22k into node A, then R119 22k to V_tone.
  - Mid band: C56 10 nF and R120 10k into node B. Low band: R121 10k into node B. Node B then goes through R129 15k to V_tone.
  - **VR4 20 kΩ (B)**: one end at node A, the other end through R125 2.2k to node B, **wiper to ground**. It works as a crossfade/shunt between "high band" and "low+mid bands".
  - [WCY]: "The primary effect of the tone control is to change the amount of attenuation in the third band. However, the center frequencies of all bands and the attenuation of the first and second bands are also somewhat affected … weakly-separated, non-orthogonal controls."
  - Which way is "bright" on the knob is UNVERIFIED. Update: **DERIVED, probably FCW = brighter.** On every other voicing-board pot the [SM] pin numbering puts pin 3 at the clockwise end. For example VR2 (decay) and VR6 (level) have the wiper tied to pin 3 and reach maximum resistance at FCW, and VR5 (CB level) has pin 3 hot and pin 1 grounded. With the VR4 wiper at pin 3 (FCW), node B (low + mid) is shunted to ground by just R125 2.2k while node A (high) sees the full 20k, so the high band dominates. At FCCW the wiper grounds node A directly and kills the high band. Still not confirmed against a real unit or a primary statement.
- Output buffer and level ([WCY] §11): C90 10 nF into the inverting input of IC6 (feedback R128 470k ‖ C77 220 pF). **CY LEVEL VR6 100 kΩ(A)** in series with C78 33 µF shunts the feedback. Werner says the stage also behaves as a differentiator (+6 dB/oct) across the audio band. **CONFIRMED** (parts re-read; [WCY] §11 text "6 dB/octave rising slope").

---

## 3. Open hi-hat (OH) and closed hi-hat (CH)

### 3.1 Signal source
- Both hats take the **BP#2 (≈7.1 kHz) output** of the cymbal filter, the "high range component from 1/2 IC3". They do not use BP#1. [SM p.6 "OH", "CH"]
- OH couples in through C67 22 nF to VCA Q27. CH couples in through C71 22 nF to VCA Q30. Each VCA is biased by two 1 MΩ resistors from +15 V (R148/R149 and R151/R150), with emitter resistors R145 / R152 = 100 Ω. [SM p.13]
- So the hats are the cymbal's upper band, reshaped by their own VCA and high-pass. The 808 has no hi-hat tuning control.

### 3.2 OH envelope and DECAY ([SM] p.13; topology read from the schematic, behaviour partly UNVERIFIED)
- **Attack**: the OH trigger (Q3 → Q7, then divider R131 22k / R130 10k, then R135 10k) turns on Q24. (Q7 added: the divider hangs off Q7, not Q3; [SM] p.13 re-read.) Q24 charges **C62 0.47 µF** almost instantly from +15 V through R137 470 Ω (decoupled by R136 100 Ω / C61 47 µF).
- **Release paths from C62**:
  - (a) R139 1 MΩ to ground, giving τ = 0.47 s as the longest possible decay.
  - (b) R134 100k into the paired collectors of Q22 and Q23.
    - **Q22** is a current sink. Its base is fed from +15 V through **OH DECAY VR3 2 MΩ(B)**, R132 100k, a node with C60 10 µF to ground, and R133 10k. Q21 shorts that node during the trigger. So the sink current depends on the DECAY setting and ramps in with τ ≈ 0.1 s after each hit. This gives a non-exponential decay that steepens over time (DERIVED; exact curve UNVERIFIED).
    - **Q23** is the choke transistor (§3.4).
- C62 is buffered by Q29, an emitter follower (R140 33k in, R143 33k load). Q29 feeds the swing VCA's collector supply through R144 33k → D13 → Q27 collector, with C65 and C64 = 1.5 nF.
- **Measured OH decay** (to −20 dB) [SM p.14]: **90 ms (min) / 450 ms (12 o'clock) / 600 ms (max)**. The knob law is strongly non-linear: most of the change happens in the first half of the rotation. Map the plugin's decay parameter so these three points are hit. **CONFIRMED** (re-read). Parts in this subsection (C62 0.47 µF, R137 470, R136 100 / C61 47 µF, R139 1M, R134 100k, VR3 2M(B), R132 100k, R133 10k, C60 10 µF, R140/R143/R144 33k, C64/C65 1.5 nF) are also **CONFIRMED** (re-read).

### 3.3 CH envelope ([SM] p.13)
- The CH trigger goes through R141 33k to Q28, an emitter follower, which charges **C63 0.47 µF**.
- C63 is the CH envelope. It discharges through R142 33k into the Q30 swing-VCA collector (via D14, with C70 and C69 = 1.5 nF) and through R173 330k into the base of Q23.
- Rough τ ≈ 0.47 µF × (33k ‖ 330k) ≈ 14 ms, before VCA-threshold effects. DERIVED, load-dependent. **CONFIRMED** arithmetic (14.1 ms) and parts.
- **Measured CH decay: 50 ms (fixed)** [SM p.14]. There is no user decay control. **CONFIRMED.**

### 3.4 Choke (CH cuts OH)
- [SM] p.6: "When the CLOSED HI-HAT (CH) is triggered while the OH circuit is activated, Q23 turns on by the voltage applied through R173. At this moment, the decay time of the OH circuit terminates."
- Mechanism: the CH envelope capacitor C63 drives Q23's base through R173 330k. Q23 then pulls the OH envelope capacitor C62 to ground through R134 100k.
- Choke time constant ≈ 100k × 0.47 µF ≈ **47 ms** maximum, and faster in practice because the swing VCA gates off once the envelope falls below its diode threshold. Q23 stays on roughly as long as the CH envelope is above about 0.6 V, i.e. about the length of the CH note. DERIVED. **CONFIRMED** (quote and parts re-read; 47 ms recomputed).
- So the choke is **a fast but not instantaneous fade (about 5–30 ms effective)**, not a hard cut. An OH triggered during or right after a CH note is held down until C63 has discharged. UNVERIFIED nuance; worth checking against recordings.
- The choke is one-way: OH never cuts CH. OH and CH can sound together if OH is triggered after the CH envelope has died. In the internal sequencer both lanes are independent tracks.

### 3.5 Hat high-pass filters and output stages ([SM] p.13; DERIVED responses)
| | HPF (unity Sallen-Key, emitter follower) | Response | Output stage (IC7 halves) |
|---|---|---|---|
| OH | C66 = C68 = 1.5 nF, R147 2.7k (feedback), R146 68k (to ground), Q26 follower (R156 22k to −15 V) | **fc ≈ 7.83 kHz, Q ≈ 2.5** (≈ +8 dB resonant bump) | C80 470 pF + R160 39k in; R159 470k ‖ C79 220 pF feedback; **OH LEVEL VR7 100k(A)** + C81 33 µF |
| CH | C72 = C73 = 1 nF, R153 2.7k, R155 68k, Q31 follower (R154 22k) | **fc ≈ 11.7 kHz, Q ≈ 2.5** | C74 470 pF + R162 33k in; R161 470k ‖ C82 220 pF; **CH LEVEL VR8 100k(A)** + C83 33 µF |

HPF parts and responses: **CONFIRMED** (re-read; MNA: OH fc 7.83 kHz, Q 2.51, +8.2 dB peak at 8.16 kHz; CH fc 11.75 kHz, Q 2.51, peak at 12.2 kHz; ideal source).

~~The output stage's input capacitor and feedback capacitor (470 pF in, 220 pF in feedback) make a broad band-pass. It rises below about 1.5 kHz, has a flat gain of C_in/C_f ≈ 2.1 between about 1.5 and 8.7 kHz, and rolls off above about 8.7 kHz (OH) or 9.6 kHz (CH).~~ **CORRECTED.** The old text ignored the level pot, which sits in series with 33 µF across the 470k feedback resistor, and got the CH input-network pole wrong (9.6 kHz; it is 1/(2π·33k·470p) = 10.26 kHz).
- The stage response is −sR_f·C_in / ((1 + sR_f·C_f)(1 + sR_in·C_in)), with R_f = 470k ‖ VR (at audio frequencies) and C_f = 220 pF. The input poles are OH 8.68 kHz (39k/470p) and CH 10.26 kHz (33k/470p).
- **At LEVEL FCW (VR = 100k, so R_f ≈ 82.5k)** the feedback pole moves to ≈ 8.8 kHz. The stage then acts as a **+6 dB/oct differentiator up to ≈ 9 kHz** with no flat region. MNA results: OH −12.4 dB at 1 kHz, −0.8 dB at 5 kHz, +0.4 dB at 10 kHz, peak +0.5 dB at ≈ 8.7 kHz, −3 dB band ≈ 3.6–21 kHz. CH: peak +1.2 dB at ≈ 9.5 kHz, −3 dB band ≈ 3.9–23 kHz.
- At lower LEVEL settings R_f shrinks and the feedback pole moves higher, so tone also changes slightly with level. This is the same behaviour Werner describes for the cymbal output stage. DERIVED.
- Combined with the resonant high-pass, the hats get a peaky presence region around 8–12 kHz rather than a flat top end.
- The CH high-pass sits about 0.6 octave above the OH high-pass, which is a big part of why CH sounds thinner and "tickier".

### 3.6 Levels [SM p.14]
Output p-p at voice LEVEL fully clockwise, with accent off / accent on:
- OH: **3.5 / 7 Vpp**
- CH: **3 / 6 Vpp**
- CY: **3.5 / 7 Vpp**
- **CONFIRMED** (re-read). [SM]: "set ACCENT LEVEL to FCCW position and the VOICE LEVEL to FCW, then turn ACCENT FCW", measured at the MULTI OUT jack.

---

## 4. Cowbell (CB)

### 4.1 Signal flow ([WCB] §2, Fig.2; [SM] p.6 "CB")
```
osc6 540 Hz ─R62 33k/R63 2.2k─C32 10µ─► VCA Q15 (swing) ─D3─┐
osc5 800 Hz ─R64 33k/R65 2.2k─C33 10µ─► VCA Q14 (swing) ─D4─┤   collector supply = shared envelope (C9) via R28 / R29 100k
                                                            └─► 2-input 4th-order band-pass (IC2) ─► C75 1µ ─► CB LEVEL VR5 5k(A) ─► IC6 buffer ─► out
```
[SM]: "Each oscillation output passes the corresponding exclusive gate (VCA, Q14, Q15) and mixed by the filter IC2."

### 4.2 Envelope: sharp attack, two-stage decay
- A trigger (1 ms, 7–14 V) charges **C9 0.47 µF** through D2 (plus R122).
- C9 discharges into: R28 100k and R29 100k (the two VCA collector paths, about 50k together), and **R82 33k in series with C34 1 µF**.
- [SM] p.6: "A series of R82 and C34 connected in parallel with C9 forms an envelope having abrupt level decay at the initial trailing edge to emphasize attack effect." [WCB] eqs 6–12 give the full nonlinear ODE system.
- DERIVED two-stage model (ignoring VCA nonlinearity):
  - **Stage 1** (C9 dumps charge into C34 through R82): τ1 = R82·(C9·C34/(C9 + C34)) = 33k × 0.32 µF ≈ **10.6 ms**. The level falls toward C9/(C9 + C34) = **0.32 of peak (−9.9 dB)**.
  - **Stage 2** (C9 + C34 = 1.47 µF discharging into the about 50k VCA load): τ2 ≈ **70 ms**. VCA thresholds make the audible tail shorter.
  - Re-check: parts (C9 0.47 µF, R82 33k, C34 1 µF, R28 = R29 100k) **CONFIRMED** from [SM] p.13 and [WCB] Fig.1. The 10.6 ms / −9.9 dB / ≈ 73 ms arithmetic is **CONFIRMED**. However, **CONFLICTING by model**: solving the coupled two-capacitor network with the 50k load present from t = 0 gives eigen-time-constants of **≈ 7.9 ms and ≈ 99 ms**, −10 dB at 12.7 ms and −20 dB at 54.5 ms. Either set is defensible; the coupled one is the better linear model. For fidelity, use [WCB] eqs 10–12 (nonlinear, including R122 and the diode) instead of either.
- **Measured CB decay** (to −20 dB) [SM p.14]: **50 ms**. There is no decay knob. **CONFIRMED.**
- [WCB] Fig.3: accent changes both peak level and the envelope's time evolution. The curves for V_ct = 8–14 V are not simple shifts, so derive the envelope from the accent voltage rather than just scaling it.

### 4.3 Swing-type VCAs (bi-level input, simplified) [WCB §6]
- Input bias network: a first-order high-pass at **f_c = 5.273 Hz**, passband attenuation **−28.59 dB**, bias **+0.4455 V**. **CONFIRMED** ([WCB] §6.1 re-read).
- Because the inputs are two-level squares, the VCA output is modelled as a square wave whose high and low levels follow the envelope:
  - V_upperEdge = α3·V_env + α4 = **1.011·V_env − 0.03122**. **CORRECTED from "+0.03122"**: [WCB] Table 1 gives α4 = −0.03122.
  - V_lowerEdge = α0·e^{−β0·V_env^γ0} + α1·e^{−β1·V_env^γ1} + α2, with **α0 = +14.97, α1 = −0.01872, α2 = −14.33, β0 = 0.01837, β1 = 1.813, γ0 = −0.3179, γ1 = −2.988**. **CORRECTED** (signs restored from the [WCB] Table 1 image; the earlier text also had **γ positive, which was wrong**: both γ are negative).
  - Sanity check with the corrected signs: V_lowerEdge = 0.36 / 0.41 / 0.46 / 0.48 / 0.50 V at V_env = 1 / 2 / 5 / 8 / 12 V. That is a slowly rising floor near the 0.4455 V bias, which is plausible. With the old positive γ the formula does not give this.
- Practical consequence: the square's amplitude, and some of its DC offset, track the envelope, with a soft knee as the envelope approaches zero.

### 4.4 Band-pass filter (IC2): the commonly quoted 2.64 kHz is wrong for the real circuit
- Topology: a multiple-feedback band-pass with **two series R–C inputs** (R26 10k + C30 2.2 nF from Q15; R27 10k + C31 2.2 nF from Q14) into node X.
  - R24 2.2k from X to ground; C29 3.3 nF from X to the output; C28 10 nF from X to the inverting input; R25 470k from the inverting input to the output.
  - This is 4th order. [WCB §7, eq.15] [SM p.13]
- Topology and parts: **CONFIRMED** (re-read from [SM] p.13 and [WCB] Fig.1). Strictly, each input path is 3rd order: a series C plus the two-capacitor MFB core.
- Response:
  - [WCB] Fig.5 / [BTX]: **f0 ≈ 881 Hz**, −3 dB points 794–977 Hz, **Q ≈ 4.8**, peak gain about 10 dB (per input). **CONFIRMED** ([BTX] page re-read: 881.05 Hz, Q 4.817, −3 dB 794.3–977.2 Hz, 10.04 dB).
  - ~~My nodal solve (DERIVED, ideal sources): peak ≈ 871 Hz, Q ≈ 5.7, +11.8 dB per input, +17.9 dB when both inputs carry the same signal.~~ **CORRECTED.** An independent MNA solve with ideal sources, with the other input's source at 0 V (the superposition case, as in [WCB]), gives a **magnitude peak at 874 Hz, −3 dB 793–986 Hz (geometric centre 884 Hz), Q ≈ 4.6, +10.0 dB per input, and +16.1 dB with both inputs driven identically.** This agrees with [BTX]/[WCB]. The old "Q 5.7 / +11.8 dB" figures roughly reproduce only if the other input is left *floating* (870 Hz, Q 5.3, +11.2 dB), which is not the ideal-source case.
- Effect: the 800 Hz fundamental sits almost at the peak (+7.5 dB, i.e. −2.5 dB re peak). The 540 Hz fundamental is at −7.6 dB, **≈ 15 dB below 800 Hz**. **CONFIRMED.**
  - ~~Upper harmonics (1.6, 2.4 kHz…) roll off at 12 dB/oct or more.~~ **CORRECTED**: the upper skirt is shallow. Relative to the peak, MNA gives 1.6 kHz −10.8 dB, 2.4 kHz −12.8 dB, 4 kHz −14.2 dB, 8 kHz −16.9 dB, 16 kHz −21 dB, which approaches first order. The lower skirt is steep (≈ −33 dB at 270 Hz).
  - So the "honk" is mostly 800 Hz plus a weaker 540 Hz, with odd harmonics only moderately attenuated. Do not model the top as heavily filtered.
- [SOS-CB]'s **587 / 845 Hz with a 2.64 kHz band-pass** was an ear-matched patch on a Nord Micro Modular against a CR-8000 sample, not a circuit measurement. Do **not** use it for a faithful model. **CONFIRMED** (SOS page: Reid used "the CR8000 CompuRhythm" as the sonic reference, swept two oscillators by ear, and found "a centre frequency of 2.64kHz worked well").

### 4.5 Level stage
CB LEVEL VR5 5 kΩ(A) sits between C75 1 µF and C76 22 nF (R116 8.2k, R117 22k), then IC6 is a unity buffer. "To a first-order approximation … a passive voltage divider with two DC blocking stages." [WCB §8] **CONFIRMED** (parts re-read).

### 4.6 Levels [SM p.14]
**3.5 Vpp normal / 12 Vpp accent**. This is the largest accent swing of any metal voice, so the CB is very accent-sensitive. **CONFIRMED** (re-read). For context, LC/LT/HC/HT also show 3.5 → 12 Vpp in the same table, so the CB ties with those for the largest swing on the machine rather than standing alone.

---

## 5. Hand clap (CP)

### 5.1 Signal flow ([SM] p.6 "CP/MA", p.9 schematic)
```
WN ─► IC21 MFB band-pass (~1.27 kHz, Q≈1.5) ─┬─► C136 22n─R372 39k/R382 1k ─► IC22 BA662 (OTA VCA) ─► buffer ─► C150 1n + R378 100k ─┐
                                               │      ▲ exp. converter Q71/Q72 ◄─ sawtooth EG (IC23 AN6912 ×4, C144)                  │
                                               └─► R342 15k/R346 680─C137 33n ─► Q70 CE stage (base biased by "reverb" EG:            ├─► IC19 Σ (CP/MA LEVEL VR17 100k(A)) ─► CP/MA out
                                                    D69→C139 18n─R348 1M─C138 47n─R349 1M→base) ─► C152 4.7n + R376 47k ─────────────┘
```
(Diagram **CORRECTED**: filter type and frequency; Q70 is not swing-type; summing components added. Parts re-read from [SM] p.9.)
[SM]: "White noise passed through the band pass filter (IC21) is applied to two VCAs in parallel to have different envelopes. These envelopes are combined to obtain sound source for the CP sound generator. Since an envelope with a relatively long decay time is applied to the VCA Q70, output from this VCA constitutes reverberation of CP sound. The output envelope at the VCA (IC22, Q71 and Q72) is a unique sawtooth shape, and is a main component of this sound generator."

### 5.2 Band-pass (IC21)
- Topology: an inverting op-amp with a **bridged-T in the feedback**: C129 = C128 = 4.7 nF, R333 10k to ground, R334 100k bridging. **CONFIRMED** (re-read).
- Input impedance is **R332 27k ‖ C127 1.8 nF**. R332 is 22k in lots 1–5. The input capacitor adds a high-frequency tilt (corner about 3.3 kHz). [SM p.9] Parts **CONFIRMED**.
- **CORRECTED topology.** The [SM] p.9 scan shows R332‖C127 landing on the **C129/C128 junction, the same node as R333**, not on the op-amp's inverting input. The circuit is therefore a standard multiple-feedback (Deliyannis) band-pass: R1 = R332 (input), R2 = R333 (shunt), C1 = C129 (to output), C2 = C128 (to inverting input), R3 = R334 (feedback).
- ~~[SM] Fig.11 formula: f = 1/(2π·4.7 nF·√(10k·100k)) = 1071 Hz, Q = √10 / 2 = 1.58.~~ This ignores R332. The MFB formula without C127 gives f0 = 1/(2πC·√(R3·(R1‖R2))) = **1254 Hz** and Q = ½·√(R3/(R1‖R2)) = **1.85**, with centre gain −R3/(2R1) = −1.85 (+5.3 dB).
- ~~Full nodal solve with the input network (DERIVED): peak ≈ 1.00 kHz, −3 dB 616–1337 Hz, Q ≈ 1.4, about +17 dB.~~ **CORRECTED.** An MNA solve of the corrected topology with C127 gives a **peak ≈ 1.27 kHz, −3 dB ≈ 930–1790 Hz, Q ≈ 1.5, about +4.4 dB**. Lots 1–5 (R332 22k): 1.30 kHz, Q 1.57, +6.0 dB. (The old numbers reproduce exactly if the input is wrongly wired to the inverting node, which confirms that this was the error.)
- The forum estimate of "1140 Hz, Q 1.95" (UNVERIFIED secondary) is closer to the corrected figure than the old 1.0 kHz was.
- See also §5.6: the IC19 summing stage shapes the clap further.

### 5.3 Sawtooth multi-clap envelope ([SM] p.6 text and Fig.13)
Mechanism (IC23 is an AN6912 quad comparator with open-collector outputs on a −15 V supply):
- The trigger is integrated by R350 / C140 into a **30 ms-wide gate pulse** (IC23 pin 1). **CONFIRMED** ([SM] p.6: "integrated by R350 and C140, and converted into pulses of 30ms wide"). Note that R350·C140 = 1M × 2.7 nF is only 2.7 ms, so the 30 ms width depends on the comparator threshold and the ramp, not on that RC alone. Take the 30 ms from [SM]; do not rederive it from the RC.
- During the gate, a relaxation loop works like this:
  - C144 (0.47 µF) is **slammed to −15 V** (Q73 on; R367 330 Ω).
  - It then discharges through R365 82k (‖ R356 1M) back toward 0 V, so τ ≈ 36 ms. DERIVED. **CONFIRMED** (parts re-read; [SM] p.6: "discharged through R365 and D71"; 35.6 ms recomputed).
  - When it crosses a comparator threshold, it is slammed again.
- This produces a **sawtooth of repeated sharp attacks with exponential decays**.
- At the end of the gate, R357 and C141 differentiate the edge into one more reset pulse. Then C144 is left to recover freely, which gives **the last clap a long decay**.
- The C144 voltage drives the base of Q72 in a **Q71/Q72 exponential converter**. Its collector current is the control current of the **BA662 OTA** (IC22). TM3 "C.P. OFFSET" plus the accent signal (D68, C143, R362) bias Q71. "Converted exponentially" [SM]: amplitude ∝ exp(k·V_C144). So in dB, each burst decays roughly along the shape of C144's recovery.

Timing, scaled from [SM] Fig.13. The drawing is labelled with a 30 ms bar. Pixel-scaled, so ±1–2 ms:
| Event | t (ms) |
|---|---|
| Burst 1 (trigger) | 0 |
| Burst 2 | ≈ 11 |
| Burst 3 | ≈ 22–23 |
| Burst 4 (end of the 30 ms gate; cuts burst 3 short, "advanced to the middle of the third time") | ≈ 30 |
| Burst 4 free decay | the [SM] trace dies away over roughly 15–25 ms after it; ends near 50 ms |

- **CONFIRMED** by a pixel measurement of [SM] Fig.13 (30 ms bar = 332 px): burst onsets in trace 7 (IC22 pin 8) and the C144 resets in trace 4 fall at **0, 10.8, 22.1 and 29.7 ms** (±0.5 ms of drawing). The last burst's envelope is drawn reaching zero at ≈ 47 ms.
- So the 808 clap is **three short bursts about 11 ms apart, a fourth at about 30 ms (only about 7–8 ms after the third) with a longer tail, plus the parallel reverb tail**.
- Within each burst, amplitude jumps instantly and decays exponentially. ~~In the drawing it falls to about 35–50% before the next reset.~~ **CORRECTED** by measuring the drawn envelope width in trace 7. Bursts 1 and 2 fall from full width to **≈ 10–12% (≈ −19 dB)** just before the next reset, a drawn τ of roughly 4–5 ms. Burst 3 is cut off at ≈ 30% by the 4th reset. The 4th burst decays with a drawn τ of ≈ 6 ms. This matches the TM3 adjustment trace (0.5 Vpp residual on 6 Vpp, −21.6 dB). The drawing is schematic, so treat these as **approximate**.
- [SM] TP-5 adjustment (TM3): play CP with level fully clockwise and adjust for a "6 Vpp" burst pattern with about "0.5 Vpp" residual between and after bursts [SM p.14 adjustment table]. In other words, the VCA is never quite closed between bursts. Model a small floor of about −22 dB re peak inside the 30 ms window. **CONFIRMED** (re-read: "write, play CP at a tempo w/ LEVEL FCW", trace labelled 6 Vpp / 0.5 Vpp).

### 5.4 Reverb path (Q70)
- The noise feeding Q70 is attenuated by R342 15k / R346 680 Ω (680 Ω is 1k for lots 1–5), about −27 dB, and AC-coupled through C137 33 nF into ~~a swing-type stage~~ **a common-emitter stage** (**CORRECTED**). Q70 has its collector at +15 V (B8) through R363 10k and its emitter at ground through R369 330 Ω. The envelope sets its *base bias* through R349 1M, so it is a bias-modulated VCA, not a swing-type VCA whose collector supply is the envelope. Parts **CONFIRMED** ([SM] p.9).
  - Added: C146 18 nF from the Q70 collector to ground forms a ≈ 884 Hz low-pass with R363 (DERIVED). The reverb path is therefore darker than the main path. It reaches IC19 through C152 4.7 nF + R376 47k.
  - Added: the [SM] p.9 "PORTION CHANGED" box says "CP: 10k (called R200) is connected across −15V line and Q70 emitter". In later units this pulls the Q70 emitter negative, which lowers its turn-on threshold and lengthens the audible reverb. It is worth an "early/late unit" switch.
- Envelope: Q69, a trigger buffer, charges C138 47 nF and C139 18 nF through D69. They decay through R348 1 MΩ and R349 1 MΩ into Q70's base.
  - **CORRECTED topology**: D69 charges **only C139** (18 nF). C138 (47 nF) sits on the far side of R348 1M and fills through it, then discharges through R349 1M into the Q70 base. The reverb envelope therefore **swells in**.
  - A linear two-capacitor solve, with Q70's base modelled as 0–200k to ground, gives eigen-time-constants of **≈ 12 ms (rise) and ≈ 71–84 ms (decay)**. If only C139 is charged, the C138 voltage peaks at ≈ 18% of the C139 charge around 25–27 ms. If Q69 holds C139 up for longer (for example for the gate), the peak is larger.
  - ~~Rough τ ≈ 65 nF × 0.5–1 MΩ ≈ 35–65 ms.~~ **CORRECTED** to the above (DERIVED). The exact level and threshold remain **UNVERIFIED** and depend on base current and R200. Q70 passes signal only while its base is above about V_BE.
  - After the VCA nonlinearity, this gives a tail audible to about 100–200 ms. (UNVERIFIED; consistent with the decay τ above.)
- **Measured CP decay** (to −20 dB) [SM p.14]: **100 ms** (fixed; no knob). **CONFIRMED** (re-read).
- A common product addition is a "Reverb/tail" control. Nepheton 2 has CP "Tone" (filter cutoff) and "Reverb" (decay time) [NEPH].

### 5.5 Levels
- [SM] p.14 prints CP amplitude as **6 Vpp normal / 2 Vpp accent**. The "2" is almost certainly a misprint, perhaps for 12; UNVERIFIED. **CONFIRMED as printed** (re-read: the scan clearly shows "6" and "2"). It is still a likely misprint; what the accent value should be is **UNVERIFIED**.
- The TP-5 adjustment uses 6 Vpp.

### 5.6 Shared output stage IC19 (added; DERIVED from [SM] p.9 parts, which are CONFIRMED)
- IC19 is an inverting summer: R375 470k ‖ C149 220 pF feedback, with **CP/MA LEVEL VR17 100k(A)** in series with C145 33 µF across it. Like the hat stages, R_f ≈ 470k ‖ VR at audio frequencies, so at LEVEL max the feedback pole is ≈ 8.8 kHz.
- The inputs are series R–C pairs: CP main C150 1 nF + R378 100k (pole 1.59 kHz); CP reverb C152 4.7 nF + R376 47k (pole 720 Hz); MA C151 10 nF + R377 100k (pole 159 Hz).
- At LEVEL max, MNA gives:
  - CP main path: rises +6 dB/oct to a broad peak near 3.7 kHz (−3 dB ≈ 1.2–11.6 kHz). Combined with the 1.27 kHz IC21 band-pass, this tilts the clap brighter than IC21 alone suggests.
  - MA path: flat from ≈ 150 Hz, rolling off above ≈ 9 kHz. This partly tames the 10.6 kHz resonant high-pass.
- In the scan, the R376 line joins the far end of R378, and that wire runs to the IC19 summing node. So the CP main, CP reverb and MA paths appear to be summed at the IC19 virtual ground. This mostly answers the old open question about where Q70 sums. The wire crosses the dense p.9 area, so this is **PLAUSIBLE, not fully traced**.

---

## 6. Maracas (MA)

### 6.1 Signal flow ([SM] p.6 "MA", p.9 schematic)
"White noise is gated by Q65 and supplied to the same buffer IC19 as for the CP sound generator through the filter Q68. Envelope for MA sound generator is generated by Q66 and Q67."

```
WN ─R335 6.8k─C130 47n─► Q65 gate (bias R336 1M) ─D65─► node (C131 10n, envelope via R337 10k from Q66)
   ─► HPF: C132 1n, C133 1n, R339 3.3k (fb), R340 68k, Q68 follower, R338 15k input shunt
   ─► C151 10n + R377 100k ─► IC19 (shared CP/MA LEVEL VR17) ─► CP/MA out
```

### 6.2 High-pass
2nd-order unity Sallen-Key: **fc ≈ 10.6 kHz, Q ≈ 2.3** (DERIVED; source loading by R337 / R338 will lower the effective Q). The maracas is therefore almost purely top-octave noise with a resonant edge. **CONFIRMED** (parts C132 = C133 1 nF, R339 3.3k, R340 68k re-read; fc 10.62 kHz, Q 2.27, ideal-source peak +7.3 dB at 11.2 kHz).
- Added: the input of this filter is C131 10 nF into R338 15k to ground, an extra ≈ 1.06 kHz first-order high-pass, fed from the gate node through R337 10k. With that network in place, MNA shows the resonance largely damped: peak −2.5 dB at ≈ 12.6 kHz, relative to the gate node. The IC19 output stage then rolls off above ≈ 9 kHz at LEVEL max (§5.6). The resonant "edge" is therefore milder than the ideal Q 2.3 suggests.

### 6.3 Envelope
- The trigger goes through D67 into C135 0.1 µF ‖ R345 150k. This stretches the 1 ms trigger with **τ ≈ 15 ms**.
- R344 220k drives Q67, an inverter with R343 100k pull-up.
- Shaping cap **C134 33 nF**: slow path through R341 470k (**τ ≈ 15.5 ms**), fast path through D66 and R343 100k (**τ ≈ 3.3 ms**).
- Q66 follows C134 and drives the gate node through R337 10k.
- Net result: a short envelope whose attack is a ramp of a few ms to about 15 ms rather than an instant step, followed by a decay of about 15–30 ms. Polarity and exact shape are UNVERIFIED (read from the schematic only).
- **Measured MA timing** [SM p.14 "M" row]: decay **25 ms (short column) / 35 ms (long column)**. MA has no decay knob, so the two columns probably reflect accent off/on or unit spread (UNVERIFIED). **CONFIRMED as printed** (re-read: 25 under SHORT, "—" under MID, 35 under LONG). The meaning is still **UNVERIFIED**. The table footnote "values are typical and variable" supports the unit-spread reading.
- Amplitude: **3 Vpp normal / 5 Vpp accent**. **CONFIRMED** (re-read).
- Envelope parts (C135 0.1 µF, R345 150k, R344 220k, R343 100k, R341 470k, C134 33 nF, R337 10k) **CONFIRMED** (re-read); time constants 15 / 15.5 / 3.3 ms recomputed.

---

## 7. Shared noise generator ([SM] p.8, main board)

- **Source**: transistor **Q35 2SC828 (rank R)** with its base-emitter junction reverse-biased into breakdown. The emitter is fed from +15 V through R124 100 Ω / C29 47 µF filtering and **R128 1 MΩ**; the base is grounded.
  - This is avalanche/zener junction noise, roughly white in the audio band. The breakdown current is only a few µA.
- **Amplifier**: IC24a, non-inverting.
  - AC-coupled by C31 1 µF; R131 is 300k (100k in later lots), so the input high-pass is below 2 Hz.
  - Gain = 1 + R130 1M / (R127 4.7k + TM4 22k(B)) ≈ **38.5× to 214×** (32–47 dB). TM4 is the "noise level" trim.
- **Calibration**: "NOISE GENERATOR — AC voltmeter to TP-4 — adjust TM-4 — **130 mV rms**" [SM p.14]. The schematic also states "W.N. LEVEL 130 mV r.m.s".
- **Later production (marked "6 lot ~")**: a 22 pF capacitor (C202) across R130 and C200 10 µF in the gain-set leg. This adds a first-order roll-off from about **7.2 kHz** (1/(2π·1 MΩ·22 pF)) toward unity gain. Early units therefore have brighter noise than later ones. DERIVED; worth exposing as a hidden "early/late unit" option.
- **Outputs**:
  - **WN** (white noise) goes to CP (IC21) and MA (Q65). The SD snare noise also comes from this bus.
  - **PN** comes from IC24b, a unity-gain Sallen-Key **low-pass** (R133 = R132 = 22k, C32 39 nF, C33 18 nF), giving **fc ≈ 273 Hz, Q ≈ 0.74** (DERIVED). It goes to the tom/conga circuits ("P.N." on p.9). It is low-passed noise, not true pink noise.
- **CONFIRMED** (the whole noise generator re-read from [SM] p.8, both the early drawing and the "6 lot ~" drawing):
  - Early: R130 1M feedback; TM4 22k(B) in series with R127 4.7k to ground; R129 330k into pin 2; R131 300k. Gain 1 + 1M/(4.7k…26.7k) = 38.5–214× (31.7–46.6 dB).
  - "6 lot ~": C202 22 pF ("surface mounting") across R130; TM4 to ground via C200 10/16; R131 100k; no R127/R129. Pole 1/(2π·1M·22p) = 7.23 kHz.
  - PN: R133 = R132 22k, C32 39 nF (feedback), C33 18 nF; ω0 273 Hz, Q 0.736, −3 dB at 284 Hz.
  - Calibration: "W.N. LEVEL 130mV r.m.s" on the schematic and "TM-4 130mV rms" in the adjustment table.
  - Transistor: "2SC828RNZ".
  - Small fix to the "32–47 dB" above: 31.7–46.6 dB.
- **Sharing**: there is one physical source, so CP, MA and SD noise are the **same random signal**. They are fully correlated: simultaneous hits sum coherently and do not average out. For strict faithfulness, use one noise generator per plugin instance feeding all noise voices, and do not reseed it per note.

---

## 8. Panel controls, outputs and internal trims for these voices

From [SM] schematic labels (p.8, p.9, p.13) and the specs (p.1).

| Voice | Panel controls | Pot | Individual out |
|---|---|---|---|
| Cymbal (CY) | LEVEL, TONE, DECAY | VR6 100k(A), VR4 20k(B), VR2 2M(B) | yes (CY) |
| Open hi-hat (OH) | LEVEL, DECAY | VR7 100k(A), VR3 2M(B) | yes (OH) |
| Closed hi-hat (CH) | LEVEL | VR8 100k(A) | yes (CH) |
| Cowbell (CB) | LEVEL | VR5 5k(A) | yes (CB); also a TRIG OUT |
| Hand clap / Maracas (CP/MA) | LEVEL (shared) plus the CP/MA instrument-select switch (SW12) | VR17 100k(A) | yes (one shared CP/MA jack); CP(MA) TRIG OUT |
| Global | ACCENT level (AC), MASTER VOLUME | main-board VR3 10k(B); VR1 50k(A) | Mix out Hi (6 Vpp / 1 kΩ) and Lo (0.6 Vpp / 3 kΩ) |

- Accent range: "**0 dB – 10 dB**" [SM p.1]. Common trigger amplitude is 7–14 V [WCY][WCB]. **CONFIRMED** (both re-read).
- Trigger outputs: "+15 V, 20 ms; 1 kΩ — CB / CP(MA) / AC" [SM p.1]. **CONFIRMED.**
- Pot values in the table above are **CONFIRMED** from the [SM] schematics: VR2 2M(B), VR3 2M(B), VR4 20k(B), VR5 5k(A), VR6/7/8 100k(A), VR17 100k(A), main-board AC LEVEL VR3 10k(B), and MASTER VOLUME VR1 50k(A). The master Hi/Lo out specs (6 Vpp / 1 kΩ; 0.6 Vpp / 3 kΩ) are re-read from [SM] p.1. They were measured with voices at the red mark and AC at FCW.
- Internal trims, not on the panel but candidates for "advanced" parameters:
  - TM1: CB/osc6 frequency, 540 Hz
  - TM2: CB/osc5 frequency, 800 Hz
  - TM3: CP offset (residual level between clap bursts)
  - TM4: noise level
- There are **no** factory controls for hat tuning, hat tone, CH decay, CB tuning or decay, CP tone or tail, or MA anything.

**Extra controls seen in commercial emulations** (useful for "all features any popular 808 VST has"):
- Nepheton 2 adds, per voice [NEPH]:
  - CY: Tone ("Attack"), Decay, **HH Frq** (base frequency of the square oscillators)
  - OH and CH: **Tone** ("noise colour"), **Decay** (on CH too), **HH Frq**
  - CB: **Tuning, Decay**
  - CP: **Tone** (filter cutoff), **Reverb** (decay time)
  - MA: **Tone** (noise colour)
  - Every voice also gets a channel strip (EQ, compression, HP/LP filters).
  - **CONFIRMED** from the [NEPH] PDF p.7 list. Exact wording: CY "Tone – Attack", "HH Frq – Base frequency for a square oscillators"; OH/CH "Tone – Noise colour", "Decay", "HH Frq"; CB "Tunning – Base frequency", "Decay"; CP "Tone – Filter's cutoff frequency", "Reverb – Reverb's decay time"; MA "Tone – Noise color". Strips: Low/High Cut with shared resonance, EQ, compressor (ratio 1:1–1:20, attack/release 1–500 ms). Every voice has Level.
- Cherry Audio's 808 Hats module exposes [CHA] (**CONFIRMED**, docs page re-read):
  - Pitch (all six oscillators together)
  - Color (crossfade between lower and upper oscillator waves; the docs do not say "3 and 3")
  - Partial Balance
  - Body (master high-pass)
  - Impact (click)
  - Choke and accent inputs, plus Open Hat Decay and Accent Level
- Werner's circuit-bend set [WCY][WCB]:
  - per-oscillator tune and mute
  - band-pass and high-pass tune or bypass
  - EG#1/EG#3 release
  - R53 drive (the input level that sets VCA clipping)
  - voltage starve (sets HD14584 supply, changing frequency, duty and amplitude together)
  - CB oscillator balance and band-pass frequency/Q

---

## 9. Implementation checklist (mw808)

1. **Shared oscillator bank** (6 × polyBLEP squares, duty 0.4798, free-running, optional per-unit detune of ±1–3%). Feed the summed mix (equal weights) to CY BP1/BP2, and feed oscillators 5 and 6 directly to the CB.
2. **Cymbal**:
   - Two bridged-T band-pass biquads (3440 Hz Q 6; 7100 Hz Q 6, peak-normalised, plus the input series-RC high-pass). Tune them with the bilinear transform pre-warped at the centre frequency [WCY §12].
   - Three swing-VCA nonlinearities with envelopes:
     - EG1: τ 0.47–2.47 s, DECAY-controlled, low band, partly mid. The low-band envelope (EG#1.2) swells, peaking at ≈ 0.15–0.2 s ([WCY] Fig.7).
     - EG2: fixed, about 0.1 s, mid. Delayed peak at ≈ 18–25 ms (C40 → R88 → C39).
     - EG3: fixed, short, high. Peaks within ≈ 1 ms, then a two-stage fall (R87/C37 dump to about −10 dB with τ ≈ 12–15 ms, then a tail).
   - Three high-passes (2.5 kHz Q 1; 8.8 kHz Q 1 ×2 gain; 3rd-order resonant about 10.4 kHz).
   - Tone crossfade between {high} and {low + mid}.
   - Validate decay against [SM]: 350 / 800 / 1200 ms to −20 dB at DECAY min / mid / max.
3. **OH/CH**:
   - Tap the BP2 output. Run separate swing VCAs, then a 2nd-order high-pass (OH 7.8 kHz Q 2.5; CH 11.7 kHz Q 2.5), then the output stage. ~~a broad output band-pass (about 1.5–9 kHz)~~ **CORRECTED**: model the output stage as −sR_f·C_in/((1 + sR_f·C_f)(1 + sR_in·C_in)), with R_f = 470k ‖ LEVEL-pot. At full level this is a +6 dB/oct rise into a peak around 8.7–9.5 kHz (§3.5).
   - OH envelope: one-pole with a time-varying release so that decay hits 90 / 450 / 600 ms at DECAY min / mid / max.
   - CH: about 50 ms to −20 dB.
   - Choke: a CH trigger forces the OH envelope release τ down to ≤ about 10–20 ms while the CH envelope is active (not a hard zero, to avoid clicks).
4. **Cowbell**:
   - Two swing-VCA-shaped squares (the levels track the envelope).
   - Two-stage envelope (instant attack; τ1 about 10.6 ms down to −10 dB, then τ2 about 70 ms; −20 dB at about 50 ms per [SM]). The coupled linear solve gives τ 7.9 / 99 ms instead; prefer [WCB] eqs 10–12.
   - Band-pass peaking at about 870–880 Hz, Q about 4.6–4.8 (**CORRECTED from "Q about 5"/"5.7"**), +10 dB per input. Use the [WCB] eq.15 coefficients with the [SM] part values. Keep the shallow upper skirt.
   - VCA edges from [WCB] Table 1 with the **corrected signs** (§4.3).
   - Big accent dependence (3.5 → 12 Vpp).
5. **Clap**:
   - Noise into an MFB band-pass at about **1.27 kHz, Q 1.5** (**CORRECTED from 1.0 kHz Q 1.4**), then the IC19 output-stage tilt (§5.6).
   - Sawtooth envelope generator: bursts at 0 / 11 / 22 / 30 ms, each an instant jump to peak followed by exponential decay (τ ≈ 4–5 ms, reaching ≈ −19 dB before the next reset). Floor about −22 dB inside the 30 ms window. The last burst decays freely.
   - Add the parallel "reverb" path (a bias-modulated CE VCA, ≈ 884 Hz low-pass at its collector). Its envelope swells in with τ ≈ 12 ms and decays with τ ≈ 70–85 ms (**CORRECTED from "35–65 ms τ, swing-VCA"**). Mix to taste and calibrate to −20 dB at about 100 ms overall.
6. **Maracas**: shared white noise into a gate, then a resonant high-pass at about 10.6 kHz Q 2.3, with a short envelope (≤ 15 ms rise, −20 dB by 25–35 ms).
7. **Noise**: one white-noise generator shared by SD / CP / MA, about −15 dBu (130 mV rms) reference level, with an optional 7.2 kHz one-pole for "late unit". The PN low-passed copy (273 Hz) goes to the toms and congas.
8. **Accent**: drive envelopes from an accent-dependent "trigger voltage" (7 → 14 V mapped from the global ACCENT knob, 0–10 dB) rather than post-scaling. Werner shows accent reshapes the envelopes (CB) and the clipping (CY/hat VCAs).
9. **Oversampling**: ≥4× for the metallic paths, because of the swing-VCA nonlinearity, the resonant high-passes at 8–12 kHz and the square-wave aliasing. Pre-warp all filters at their centre or corner frequencies.

---

## 10. Open questions / UNVERIFIED items
- Exact EG#2 / EG#3 release time constants for the cymbal, and the OH decay curve shape. Both are load-dependent through the swing VCAs. Werner's companion site (https://ccrma.stanford.edu/~kwerner/papers/icmcsmc2014.html, linked from [WCY]) may hold the 5th-order tone-stage coefficients and audio; I did not retrieve it. (Partly narrowed: [WCY] Fig.7 gives the EG shapes and peak times, see §2.4. The tails remain load-dependent.)
- ~~The sign convention of the [WCB] Table 1 coefficients.~~ **RESOLVED** (§4.3).
- The CP accent amplitude in the [SM] table ("2 Vpp", probably a misprint). Confirmed as printed; the true value is unknown.
- The meaning of the MA 25/35 ms columns.
- Which direction of CY TONE is brighter. Now DERIVED as FCW = brighter from the pot pin convention; not confirmed on hardware.
- The early-lot (serial < 300) oscillator 5/6 capacitor values.
- Exact summing point of the CP reverb VCA (Q70) into IC19. Now PLAUSIBLE: the IC19 virtual ground via C152 + R376 (§5.6).
- New: the level and threshold of the clap reverb envelope with and without R200 (the 10k "PORTION CHANGED" mod).
- New: how stiff the VCA output nodes are as sources. This decides whether cymbal HP#3 rings at +23 dB (ideal source) or is nearly flat (39k source), and similarly how much the maracas high-pass resonates.

---

## Verification log

Independent fact-check pass, 2026-10-02. Method: primary documents were downloaded and read directly (scanned schematic images zoomed at 400 dpi; paper figures and tables viewed as rendered pages, not only text extraction). Every filter was recomputed with a small modified-nodal-analysis (MNA) solver: ideal op-amps (open-loop gain 1e8), emitter followers as unity VCVS, and 20 000-point log sweeps. The envelope networks were solved as linear 2-capacitor ODE systems (eigen-time-constants plus matrix-exponential step responses).

**Sources actually retrieved and read**
- [SM] full 16-page scan (manuals.fdiskc.com). Pages read: p.1 specs and pot list; p.5 Fig.11/12 and text; p.6 circuit description and Fig.13; p.8 main board (noise generator, early and "6 lot ~" variants, AC LEVEL VR3); p.9 CP/MA (IC21, IC22, IC23, Q65–Q73, IC19, "PORTION CHANGED" box); p.13 voicing board (oscillators, CB, CY, OH/CH, master); p.14 adjustment and voice-check tables.
- [WCY] ICMC/SMC 2014 PDF: §3–§13, Fig.1, Fig.4, Fig.7, Table 1, eqs 1–17, footnotes 7–10 and 21.
- [WCB] AES 137 PDF (michaelzfreeman.org mirror): §2–§6, Fig.1, Fig.3, Table 1, eqs 2–14, footnotes 3–7.
- [BTX] blog page (values quoted). [SOS-CB] page (reference machine and method). [NEPH] Nepheton 2 manual PDF p.6–7 and the Strips section. [CHA] Cherry Audio docs page.
- Not retrieved: [SOS-CY] (its one claim is independently confirmed by [SM] p.6) and Werner's CCRMA companion site.

**Checked and CONFIRMED**
- Oscillator bank:
  - All 6 R/C values; f = 2.0699/(RC) reproduces all six nominal values and both trim ranges.
  - Duty 47.98% from the HD14584 thresholds.
  - TM1/TM2 = 1.85/1.25 ms at TP6/TP7.
  - Early-lot R44 390k / R45 330k and the swapped labels.
  - Mix 6 × 120k into R53 1k (gain 0.00794); CB taps 33k/2.2k.
  - 540/800 Hz cents and the 6.80-semitone interval.
- Cymbal:
  - BP1/BP2 parts and response (3435 Hz Q 5.96 +23.0 dB; 7094 Hz Q 6.03 +24.1 dB).
  - HP1/HP2/HP3 parts and response (2.50 kHz Q 0.965; 8.84 kHz Q 1.0 ×2; 3rd-order peak 10.43 kHz with an ideal source).
  - EG#1 series R93 + VR2 wiring, so τ = 0.47–2.47 s.
  - EG#1.2 τ 29 ms; EG#1.3 divider 0.233.
  - Attack-smoother constants; [WCY] Table 1 and α = 3.5.
  - Tone and output-stage parts; decay 350/800/1200 ms; levels 3.5/7 Vpp.
- Hats:
  - All VCA, envelope, choke and HPF parts.
  - OH 7.83 kHz / CH 11.75 kHz, Q 2.51.
  - CH τ 14.1 ms; choke τ 47 ms; R139 τ 0.47 s.
  - The [SM] p.6 choke quote.
  - OH 90/450/600 ms; CH 50 ms; levels.
- Cowbell:
  - Envelope parts and the [SM] p.6 quote; decay 50 ms; levels 3.5/12 Vpp.
  - BP parts, peak 874 Hz (magnitude) / 881–884 Hz (geometric).
  - [WCB] VCA bias numbers (5.273 Hz, −28.59 dB, 0.4455 V).
  - SOS 587/845 Hz / 2.64 kHz as by ear against a CR-8000.
- Clap:
  - IC21 parts; 30 ms gate ([SM] text).
  - C144 τ 35.6 ms (R365 ‖ R356).
  - Burst timing 0/10.8/22.1/29.7 ms (pixel-measured, Fig.13).
  - TM3 6 Vpp / 0.5 Vpp; decay 100 ms; amplitude 6 / "2" Vpp as printed.
- Maracas: HPF parts (10.62 kHz, Q 2.27); envelope parts and time constants; 25/35 ms and 3/5 Vpp as printed.
- Noise: every part in both the early and the "6 lot ~" variants; gain 38.5–214×; the 7.23 kHz late roll-off; 130 mV rms; PN 273 Hz Q 0.736.
- Panel and specs: accent 0–10 dB; trigger out +15 V / 20 ms / 1 kΩ; master out levels; all pot values.
- Commercial controls: [NEPH] and [CHA] control lists.

**CORRECTED**
1. Clap band-pass (§5.2, TL;DR 5, checklist 5):
   - Old: 1.0 kHz, Q 1.4, +17 dB.
   - New: ≈ 1.27 kHz, Q ≈ 1.5, +4.4 dB (MFB; lots 1–5: 1.30 kHz).
   - Cause: the input R332‖C127 goes to the T-midpoint, not the inverting input. The old numbers reproduce exactly with the wrong wiring.
2. Clap reverb (§5.4):
   - Q70 is a bias-modulated CE stage, not swing-type.
   - Only C139 is charged directly, so the envelope swells in.
   - τ ≈ 12 ms rise / 71–84 ms decay, replacing "35–65 ms".
   - Added C146 (≈ 884 Hz collector LP), C152/R376 summing, and the R200 later-unit mod.
3. Clap burst depth (§5.3): bursts fall to ≈ 10–12% (−19 dB) before each reset, not 35–50%.
4. Cowbell band-pass Q and gain (§4.4): Q 5.7 / +11.8 dB → Q ≈ 4.6 / +10.0 dB per input (+16.1 dB coherent). Matches [BTX] / [WCB].
5. Cowbell band-pass upper skirt (§4.4): not "≥ 12 dB/oct". It is shallow: −13 dB at 2.4 kHz and −17 dB at 8 kHz re peak.
6. Cowbell VCA fit (§4.3):
   - V_upperEdge offset: +0.03122 → −0.03122.
   - Signs restored: α0 +, α1 −, α2 −.
   - γ0, γ1 are negative (−0.3179, −2.988), not positive.
7. Hat output stage (§3.5):
   - It is not flat from 1.5 to 8.7 kHz. The level pot (VR7/VR8 100k in series with 33 µF) shunts R_f, so at full level the stage is a +6 dB/oct rise into a peak at about 8.7–9.5 kHz.
   - CH input pole: 9.6 → 10.26 kHz.
8. Minor fixes:
   - Noise-amp gain in dB (32–47 → 31.7–46.6).
   - OH trigger path goes via Q7.
   - Omitted C58 10 nF in the CY low band.
   - Early-lot oscillator target swapped (early osc6 targets 800 Hz).
   - CB "largest accent swing" is shared with the toms/congas.
   - [CHA] Color is not specified as "3 + 3" oscillators.

**CONFLICTING (alternatives kept in text)**
- Cowbell envelope time constants: decoupled 10.6 / ≈ 70 ms vs coupled 7.9 / 99 ms. Both give −20 dB at ≈ 50–55 ms.
- Cymbal HP#3 resonance: +23 dB at 10.4 kHz with an ideal source vs ≈ −0.7 dB at 11.4 kHz with a 39k source. The real case lies in between and is time-varying.

**Still UNVERIFIED**
- Cymbal EG#2/EG#3 tails and the OH decay curve. [WCY] Fig.7 shapes are recorded but load-dependent.
- The CP accent amplitude (the printed "2 Vpp").
- What the MA 25/35 ms columns mean.
- CY TONE direction (DERIVED: FCW = brighter, from the pin convention).
- Early-lot oscillator 5/6 capacitors.
- The exact Q70 → IC19 wire (PLAUSIBLE).
- Clap reverb level and threshold with and without R200.
- The [SM] "30 ms" gate cannot be derived from R350·C140 = 2.7 ms alone. Taken as stated by [SM].

# 01 — TR-808 bridged-T resonator voices (BD, SD, toms/congas, RS/CL, maracas)

Scope: circuit-level behaviour of the 808's "bridged-T" voices, written so the DSP can be built from it.
Component values were read directly off the schematic in the Roland service notes (scan, 400 dpi).

**Labels used below.**
- **[SM pN]**: Roland *TR-808 Service Notes*, 1st ed., 15 Jun 1981 (2nd printing), page N. https://manuals.fdiskc.com/flat/Roland%20TR-808%20Service%20Manual.pdf
- **[W14]**: Werner, Abel, Smith, "A physically-informed, circuit-bendable, digital model of the Roland TR-808 bass drum circuit", DAFx-14. https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf
- **DERIVED**: I calculated it from the schematic values with standard formulas. It has not been checked against measurements.
- **UNVERIFIED**: I could not confirm it from a primary source.

**Fact-check tags (added by an independent verification pass, 2026-10-02; see "Verification log" at the end).**
- **CONFIRMED**: re-read from the primary source (400 dpi service-manual scan, Werner's PDF rendered as images, or the cited page), or recomputed from the confirmed component values and matched.
- **UNVERIFIED**: no primary source found; treat as an estimate.
- **CONFLICTING**: sources disagree; the alternatives are listed.
- **Corrected from X**: the original text was wrong or imprecise; the source of the correction follows.

---

## 0. The common machinery

### 0.1 Trigger pulse and accent [SM p4]
- The CPU's step pulse is only 10 µs wide. Monostable IC6 stretches it to **≈1 ms**.
- The ACCENT circuit (Q31–Q34) then sets the pulse height. The result is the **COMMON TRIG**, which goes to every voice in parallel.
  - Unaccented step: trigger amplitude ≈ **4 V**. **CONFIRMED** [SM p4: "base of Q32 becomes approx. +5V and trig amplitude is approx. 4V"].
  - Accented step: **≈4–14 V**, set by the global ACCENT knob (VR3). **CONFIRMED** [SM p4: "a voltage between 5V and 15V according to VR3 setting is applied to the base of Q32, and is converted into trig pulses of approx. 4–14V"].
  - The spec sheet gives accent level as "0 dB–10 dB" [SM p1]. **CONFIRMED**. Note 10 dB = ×3.16, slightly less than the 4→14 V ratio (×3.5 = 10.9 dB); both are nominal.
  - 10 µs step pulse stretched to ≈1 ms by IC6: **CONFIRMED** [SM p4 text and Fig. 6].
- Each voice has a gate transistor pair (BD: Q39/Q40, SD: Q45/Q46, LT: Q50/Q51, MT: Q54, HT: Q57, RS/CL: Q59/Q60). The pair ANDs its instrument-data line with COMMON TRIG. The voice therefore receives a 1 ms rectangular pulse whose **height equals the accent voltage** [SM p4, W14 §3].
- CB, CY, OH and CH get a narrower 7–14 V range [SM p4]. **CONFIRMED** ("narrowed to 7V–14V by 1/2 IC2 (pins 1–3) on the voicing board to increase S/N ratio"). That does not apply to the voices in this document.
- **Design consequence.** In hardware, accent is not a gain change. It is the size of the excitation, and every voice responds to it through its own (sometimes nonlinear) circuit. The service manual's level table [SM p14] measures each voice with accent at minimum ("NORMAL") and at maximum ("ACCENT"). Table values **CONFIRMED** against the 400 dpi scan. Measurement conditions, also from [SM p14 "Checking voices"]: scope on the voice's MULTI OUT, **voice LEVEL at FCW (maximum)**, ACCENT knob FCCW for NORMAL then FCW for ACCENT, and that voice's DECAY/TONE etc. at 12 o'clock. The Vpp figures are therefore at maximum level, not at the "red mark" used for the master-output spec.

| Voice | Normal Vpp | Accent Vpp | Ratio |
|---|---|---|---|
| BD | 3.5 | 10 | 2.86 (9.1 dB) |
| SD | 3 | 10 | 3.33 |
| LC/LT/HC/HT | 3.5 | 12 | 3.43 |
| MC | 3 | 10 | 3.33 |
| MT | 3 | 11 | 3.67 |
| CL | 2.5 | 8 | 3.2 |
| RS | 3 | 10 | 3.33 |
| MA | 3 | 5 | 1.67 |

  The trigger itself spans 4→14 V, a ratio of 3.5. The linear voices track it almost proportionally. BD tracks it less because of the diode clamp in its pulse shaper.
- Factory change at serial 010600: the voice-gate resistors R106, R154, R182, R213, R242, R268 and R298 went from 22 k to 10 k. The aim was enough gate drive at low COMMON TRIG amplitudes [SM p15]. **CONFIRMED** (main board change 6: "Ensure sufficiency of gate drive signal voltage at lower COMMON TRIG amplitude. * Increase gain"; the same page notes some units below that serial were modified at the factory). The schematic marks these resistors "22k for 1~5 lot". Early units therefore trigger more weakly at low accent.

### 0.2 The bridged-T resonator [SM p5, Fig. 11]
- Topology: an op-amp with a bridged-T network in its negative feedback path.
  - Two capacitors C1 and C2 run in series from the inverting input to the output.
  - R1 runs from the capacitor junction to ground.
  - R2 bridges from the output to the inverting input.
  - The excitation pulse goes into the **non-inverting** input.
- The result is a band-pass filter that rings as a decaying sinusoid when hit with a pulse. No VCA or envelope generator is needed.
- Formulas (Roland):
  - `f = 1 / (2π·sqrt(R1·R2·C1·C2))`
  - `Q = sqrt(R2/R1) / (sqrt(C1/C2) + sqrt(C2/C1))`
  - "the decay time becomes longer as Q increases" [SM p5].
- Every 808 voice uses a bridged-T in some form [W14 fn.15]. **CONFIRMED** (fn. 15: bridged-T networks are also used as band-pass filters in CP, CB, CY, OH/CH).
- Both formulas **CONFIRMED** by independent nodal analysis (ideal op-amp, + input as excitation): denominator `s²·C1·C2·R2 + s·(C1+C2) + 1/R1`.
- **Regenerative feedback.** Most voices (BD, toms/congas, claves) add a second inverting op-amp stage. Its output goes back into the capacitor junction through a large resistor (BD R170 470 k; toms R230/R259/R286 820 k; CL R313 390 k). **CONFIRMED** on the schematic [SM p9]. This raises Q well above the passive formula; compare the passive and effective Q in each voice's table. **A model built from the passive formula alone will decay too fast.** Either model the feedback loop, or fit Q to the measured decay times below.
- **Closed form for the regenerated Q (DERIVED by the fact-checker; added).** Let the feedback stage deliver V_fb = −G·V_out into the junction through R_fb, with C1 the capacitor on the op-amp's inverting-input side. Nodal analysis gives the denominator `s²·C1·C2·R2 + s·(C1 + C2 − G·C1·R2/R_fb) + 1/(R1‖R_fb)`, so
  - `f` is the passive formula with R1 replaced by R1‖R_fb (a negligible shift for toms/CL);
  - `Q_eff = Q_passive · (C1+C2) / (C1 + C2 − G·C1·R2/R_fb)`;
  - the loop self-oscillates when `G ≥ G_crit = (C1+C2)·R_fb / (C1·R2)`.
  - For the toms/congas (G = 1, R_fb = R2 = 820 k) this collapses to `Q_eff = ω0·C1·R2` and an amplitude time constant `τ = 2·R2·C1`, **independent of the TUNING pot**. The predictions match Roland's decay table closely (see §3.2), which independently validates both the topology reading and this formula. Use it instead of pure curve fitting.

### 0.3 How Roland defines decay time [SM p14, "Checking voices"]
- Decay time is measured from the start of the waveform until the envelope falls to **1/10 of Vpp, i.e. −20 dB**. **CONFIRMED** for the 1/10 Vpp criterion. Corrected from "from the first peak": in the [SM p14] diagram the DECAY TIME bracket begins at the onset of the waveform, not at its first peak. The difference is under a quarter period, so it matters only for BD (≈5 ms).
- Conditions: probe on the MULTI OUT jack, that voice's DECAY, TONE etc. at 12 o'clock, voice LEVEL at FCW. **CONFIRMED**. The BD "MID" decay is therefore the 12 o'clock setting. SHORT/LONG are presumably FCCW/FCW (UNVERIFIED; the manual does not say).
- Conversion: for an exponential, τ = T₋₂₀dB / ln(10) = T / 2.303, and the effective resonator Q = π·f·τ. **CONFIRMED** (amplitude envelope e^(−t/τ) with τ = 2Q/ω₀).

### 0.4 Master table: Roland's "typical and variable" values [SM p14]

| Voice | Freq LOW | Freq MID (typ.) | Freq HIGH | Decay (−20 dB) | τ (DERIVED) | Effective Q (DERIVED) |
|---|---|---|---|---|---|---|
| BD | — | **56 Hz** (18 ms period) | — | short 50 / mid 300 / long 800 ms | 22 / 130 / 347 ms | 3.4 / 20 / 54 |
| SD H | — | **476 Hz** (2.1 ms) | — | 60 ms (whole voice) | — | — |
| SD L | — | **238 Hz** (4.2 ms) | — | (same) | 26 ms | ~19.5 |
| LT | 80 Hz | **90 Hz** | 100 Hz | 200 ms | 87 ms | 24.6 |
| MT | 120 | **135** | 160 | 130 ms | 57 ms | 24 |
| HT | 165 | **185** | 220 | 100 ms | 43 ms | 25 |
| LC | 165 | **185** | 220 | 180 ms | 78 ms | 45 |
| MC | 250 | **280** | 310 | 100 ms | 43 ms | 38 |
| HC | 370 | **400** | 455 | 80 ms | 35 ms | 44 |
| CL | — | **2500 Hz** | — | 25 ms | 11 ms | 85 |
| RS H | — | **1667 Hz** | — | 10 ms (whole voice) | — | — |
| RS L | — | **455 Hz** | — | (same) | — | — |
| MA | — | — | — | 25 (short) / 35 (long) ms | — | — |

Verification of this table:
- Every Freq and Decay entry is **CONFIRMED** against the 400 dpi [SM p14] scan, including the LC/HT 165/185/220 duplication and MA 25 (SHORT column) / 35 (LONG column). The periods the manual prints alongside, e.g. BD 18 ms, SD 2.1/4.2 ms, LT 12.5/11.1/10 ms, are consistent with the Hz values.
- The τ column is **CONFIRMED** by recomputation.
- The effective-Q column is **CONFIRMED** by recomputation, with one caveat: the BD Qs (3.4/20/54) use **49.5 Hz**, not the 56 Hz shown in the same row. At 56 Hz they would be 3.8/23/61.
- **Prediction check (DERIVED by fact-checker, closed form in §0.2):**

  | Voice | Predicted −20 dB decay | Roland |
  |---|---|---|
  | BD, DECAY at 12 o'clock | 330 ms | 300 |
  | LT | 212 ms | 200 |
  | MT | 133 ms | 130 |
  | HT | 102 ms | 100 |
  | MC | 102 ms | 100 |
  | LC | 212 ms | 180 |
  | HC | 102 ms | 80 |
  | SD low resonator (passive) | 51 ms | 60 |

  Toms and MC agree within 6 %. LC and HC come out 18–28 % long; see §3.2.

How to read the table:
- For toms and congas, LOW and HIGH are the ends of the TUNING knob's range.
- Toms have no decay control. Their decay times are fixed.
- MA has no decay knob. What the 25/35 ms columns mean is UNVERIFIED (possibly normal versus accent).
- Note that the 56 Hz BD figure is a nominal chart value. See §1.4.

Other common facts:
- Noise generator: about **130 mV rms** at TP-4 [SM p14 adjustment table]. **CONFIRMED** (set with trimmer TM-4, AC voltmeter on TP-4). Note the noise generator itself was revised at serial 010600 [SM p15 main board change 5: R129 330K→short, R311 330K→100K, R127 4.7K→10µ, C202 0→22p] to remove a DC offset that cut gain margin. The source is a "selected noise" 2SC828 transistor [SM p16 parts list]. A white-noise bus (W.N.) and a pink-noise bus (P.N.) run across the voicing board [SM p9].
- Supply rails are ±15 V (the B2/B3/… rails on the schematic). Tiptop also notes "+/-15V of the original 808" (Tiptop BD808 manual, https://www.polynominal.com/sample-packs/TIPTOP-bd808/Tiptop_Audio_BD808_user-manual.pdf).
- Op-amps are µPC4558 dual parts [SM p16, W14 §2].
- Every voice's output stage is an inverting op-amp. Its LEVEL pot (100 k, A taper) sits **in the feedback path**, in series with a 33 µF cap and in parallel with a 470 k resistor. The level knob is therefore a gain control, not a divider. **CONFIRMED** for SD (VR7/C68/R210), LT/LC (VR10/C82/R235), MT/MC (VR12/C95/R266), HT/HC (VR14), RS/CL (VR16/C126/R330) [SM p9]. BD is the exception (§1.8; corrected from "§1.6", which is the attack section): its VR4 is a divider [W14 Table 1].

---

## 1. Bass drum (BD)

### 1.1 Signal flow [W14 §2, SM p5, SM p9]

```
COMMON TRIG ─┬─ Q39/Q40 gate (AND with BD data) → 1 ms pulse, 4–14 V
             │        │
             │        ├─► PULSE SHAPER (C40‖R163 series, R162 shunt, D53 clamp) ─► IC12a (+) input
             │        │                                                                   │
             │        └─► ENVELOPE GEN Q41/Q42 ─► Q43 shorts R165 (attack pitch jump ~4–6 ms)
             │                           └─► Q42 turn-on → C39/D52/R161 RETRIGGER PULSE ─► bridged-T centre node
             │
   IC12a bridged-T (C41, C42, R167 bridge, R165+R166 to ground via Q43 node, R161, R170)
             │  ▲
             │  └── R170 470k ◄── FEEDBACK BUFFER IC12b (R164 in, R169‖(VR6 DECAY + C43) feedback)
             ▼
   TONE low-pass (R171 + R172‖VR5, into C45) ─► LEVEL VR4 (C47 → VR4 → C49) ─► Q44 emitter follower ─► C50 ─► R175 ─► BD out
```

Werner splits the circuit into blocks at the points where low-impedance outputs drive high-impedance inputs, so loading between blocks is negligible [W14 §2]:
1. Trigger logic.
2. Pulse shaper.
3. Bridged-T.
4. Feedback buffer.
5. Envelope generator with retrigger pulse.
6. Tone, level and output stage.

### 1.2 Component values (read from [SM p9]; designators match [W14])

**CONFIRMED.** Every value in this table that also appears in [W14 Fig. 1] was cross-checked against that schematic (rendered at 200 dpi) and matches: R152–R173, R176, R177, C38–C47, C49, VR4–VR6, D52, D53. The only parts not shown in W14 Fig. 1 are R174, R175, C48 and C50; they were read from [SM p9] and are consistent with the overview scan.

| Block | Parts |
|---|---|
| Gate | R152 22k, R153 10k, R154 10k (22k early lots), R155 4.7k, Q39, Q40 |
| Pulse shaper | **C40 0.015 µF ‖ R163 100k** (series), **R162 4.7k** to ground, **D53** clamp to ground |
| Bridged-T core (IC12a, pins 1–3) | **C41 = C42 = 0.015 µF**, **R167 1 MΩ** (bridge, output → inverting input), **R166 6.8k + R165 47k** (R1 path to ground; Q43 shorts R165), **R161 1 MΩ** (retrigger input), **R170 470k** (feedback input) |
| Envelope generator | Q41 (base R157 8.2k / R158 2.7k from the trigger), **R156 1 MΩ, C38 0.1 µF** timing, Q42 (R160 22k collector load), Q43 base via R159 100k |
| Retrigger | **C39 0.033 µF, D52, R161 1 MΩ** |
| Feedback buffer (IC12b, pins 5–7) | **R164 47k** input, **R169 47k** feedback, in parallel with **VR6 500k (B taper) DECAY in series with C43 33 µF**; R168 22k from the + input to ground |
| Tone | **R171 220 Ω**, **R172 10k ‖ VR5 10k (C taper) TONE** (as a variable resistor), into **C45 0.1 µF** |
| Level / output | C47 0.47 µF → **VR4 100k (A) LEVEL** → C49 0.47 µF → R177 82k → Q44 (R176 100k bias, R173 6.8k emitter to −B2), C48 220 pF, C50 1 µF, R174 100k, R175 1k |

### 1.3 Pulse shaper [W14 §4]
- Without the diode, this block is a passive low-shelf: `H(s) = (R162·R163·C40·s + R162) / (R162·R163·C40·s + R162 + R163)`.
- Rising edge:
  - The output jumps to the full trigger voltage V_trig (4–14 V).
  - It then relaxes to V_trig·R162/(R162+R163) = **0.045·V_trig**.
  - Time constant τ = C40·(R162‖R163) ≈ **67 µs** (DERIVED; recomputed 67.3 µs, **CONFIRMED**). The 0.045 settle ratio also recomputes (4.7/104.7 = 0.0449).
- Falling edge (1 ms later):
  - The output swings negative, and D53 clips it at about **−0.71 V**.
  - Its size is therefore **almost independent of accent**. Werner gives the falling-edge step as ≈ V_trig·R162/(R162+R163) + 0.71.
- Werner's memoryless clip model: `V_shaped = V+` for V+ ≥ 0, and `0.71·(e^{V+} − 1)` for V+ < 0.
- **Accent behaviour.** The positive kick scales with accent, but the negative kick at 1 ms stays constant. This is the main reason accent on the BD is "harder attack" and not simply "louder", and why the BD ratio in §0.1 is below 3.5×.

### 1.4 Bridged-T and centre frequency [W14 §5, §7]
- Core transfer function from the + input (Werner eq. 5): `H_bt1(s) = (β2·s² + β1·s + β0) / (α2·s² + α1·s + α0)`, with
  - β2 = α2 = R_eff·R167·C41·C42
  - β1 = R_eff·C41 + R167·C41 + R_eff·C42
  - α1 = R_eff·(C41+C42)
  - β0 = α0 = 1

  This is a non-inverting band-pass, i.e. unity plus a resonant peak. Coefficients **CONFIRMED**: W14 §5 (eq. 5) was re-read from the rendered page image, and the coefficients agree with independent nodal analysis.
- R_eff in steady state = R161 ‖ (R165+R166) ‖ R170 = 1M ‖ 53.8k ‖ 470k = **46.05 kΩ**. **CONFIRMED** (recomputed 46.05 k; Werner uses the same substitution).
  - This gives **f_c ≈ 49.4 Hz** (my calculation; recomputed 49.44 Hz, **CONFIRMED**). Werner gets **≈49.5 Hz** (**CONFIRMED**, W14 §5).
  - Passive Q ≈ 2.3 (DERIVED; recomputed 2.33, **CONFIRMED**). The feedback buffer raises it (§1.5).
- Roland's tuning chart says **56 Hz** [SM p14] (printed as 18 ms period). Werner notes the computed value is "close to" that chart entry [W14 §5]. **CONFIRMED** (both).
- **CONFLICTING: three Roland/derived BD periods.**
  - Chart [SM p14]: 18 ms, i.e. 56 Hz.
  - BD circuit description [SM p5]: Q43's 4 ms ON time is "1/2 × 1/2 of **16 ms** of the inherent oscillation period", i.e. 62.5 Hz.
  - Schematic math: 20.2 ms, i.e. 49.4 Hz.

  The audio evidence below favours ≈49.5 Hz for the steady-state pitch. The 56/62.5 Hz figures look like nominal or rounded design numbers, possibly reflecting the sigh-raised early pitch.
- Measured: f0 analysis of a TR-808 sample library (the "Trisample"/Tristar library; hardware provenance not stated) on 0.2 s windows, energy-weighted. Main finding: "In the non-'driven' presets, the maximum of the distribution corresponds to f0=49.48 Hz". "Short" presets show a secondary local maximum at **51.05 Hz**, from the samples' earliest windows (arXiv 2502.07524, https://arxiv.org/html/2502.07524v1). Corrected from "an STFT analysis of 37 long TR-808 samples found a median f0 of ca. 49.5 Hz": the 49.48 Hz figure is the **peak of the f0 distribution** over the non-driven presets. The **37 "long" samples** were used for the initial-sweep statistic (§1.7), not for the 49.5 Hz figure. **CONFIRMED** with that correction.
- **Use 49.5 Hz as the default steady-state pitch.** Treat 56 Hz as the start of the attack / sigh (consistent with W14 Fig. 11, which starts at ≈57–58 Hz).
- Feedback input path (decay loop): `H_bt2(s) = −(R_k·R167·C41·s) / (R_k·R170·R167·C41·C42·s² + R_k·R170·(C41+C42)·s + R_k + R170)`, with R_k = R161 ‖ (R165+R166) [W14 eq. 7]. The retrigger input path H_bt3 is the same with R161 and R170 swapped.
  - **Corrected:** the s² coefficient was "R_k·R167·C41·C42". **W14 eq. 7 prints α2 = R‖·R167·C41·C42 exactly as the original line had it, but that term is dimensionally inconsistent** with α1 = R‖·R170·(C41+C42) and α0 = R‖+R170. Independent nodal analysis gives α2 = R‖·**R170**·R167·C41·C42. This is a typo in the paper; do not copy it into code. With the corrected α2, the resonance of H_bt2 is at the same 49.4 Hz, with peak gain −R167·C41/(R170·(C41+C42)) = **−1.064**.

### 1.5 Decay: the feedback buffer, and why the stock 808 does not self-oscillate [W14 §6, SM p5]
- IC12b is an inverting amplifier with a high-shelf response, `H_fb(s) = −(β1·s + β0) / (α1·s + α0)` (Werner eq. 6; the sign is from the inverting topology). The coefficients are:
  - β1 = R169·VR6k·C43
  - β0 = R169
  - α1 = R164·(R169 + VR6k)·C43
  - α0 = R164
  
  Here VR6k = 500 kΩ·k, with k the knob position 0–1.

  **CONFIRMED** against W14 eq. 6, read from the rendered page. The paper puts the minus sign inside the coefficients (β1 = −R169·VR6k·C43, β0 = −R169). Note the shape: at DC (C43 open) the gain is −R169/R164 = −1. At audio it falls to −(R169‖VR6k)/R164, with the shelf corner well below 1 Hz (W14 Fig. 7). So it is a shelf that *cuts* audio relative to DC.
- At audio frequencies C43 (33 µF, about 96 Ω at 50 Hz) is effectively a short. The loop gain magnitude is therefore `|G| ≈ (R169 ‖ VR6·k) / R164` (DERIVED). With the pot at:
  - k = 0 → **0** (shortest decay, 50 ms)
  - 0.02 → 0.18
  - 0.1 → 0.52
  - 0.25 → 0.73
  - 0.5 → 0.84
  - 1.0 → **0.914**
  
  The G values are **CONFIRMED** by recomputation (0.175, 0.515, 0.727, 0.842, 0.914). VR6 is "500k(B)", a linear taper, so k is the rotation (**CONFIRMED** in W14 Fig. 1 / SM p9). The decay knob's response is very nonlinear: most of the change in gain happens in the first 10–20 % of its travel. The decay time itself rises steeply as G approaches the threshold.
- **Why it does not self-oscillate (DERIVED; corrected).** Because R169 = R164 = 47 k and VR6 is in parallel with R169, the buffer's gain can never exceed 0.914.
  - **Corrected from "the loop oscillates at G = 1".** The loop gain at f_c is G × |H_bt2(f_c)| = G × 1.064 (§1.4). By the §0.2 closed form, the threshold is G_crit = (C41+C42)·R170/(C41·R167) = 0.030 µF·470 k/(0.015 µF·1 M) = **0.940**, not 1.0.
  - The stock maximum of 0.914 is therefore **97 % of the threshold**. That explains why a mere ±5–20 % component tolerance lets some clones (Tiptop) or units sit right at self-oscillation.
  - Predicted −20 dB decays from Q_eff = Q_p·(C41+C42)/(C41+C42 − G·C41·R167/R170) at 49.4 Hz:

    | k | Q_eff | −20 dB decay |
    |---|---|---|
    | 0 | 2.3 | ≈35 ms |
    | 0.25 | 10.3 | 152 ms |
    | 0.5 (12 o'clock) | 22.3 | **330 ms** |
    | 0.75 | 42.6 | 632 ms |
    | 1.0 | 84.5 | 1.25 s |

  - Against Roland's 50 / **300** / 800 ms (short / mid / long): the 12 o'clock "MID" point, the one Roland defines, agrees within 10 %.
  - The FCW figure is **CONFLICTING**: 1.25 s ideal versus 800 ms [SM p14]. That close to threshold, Q is extremely sensitive to tolerance and to the op-amp's finite gain. Calibrate k = 1 to ≈800 ms, either by trimming G_max to ≈0.90 or by using the Roland figure directly.
  - The longest stock decay is 800 ms to −20 dB [SM p14] (**CONFIRMED** table value).
  - Tiptop's 1:1 clone extended the decay "to the maximum amount possible by this circuit, right before self-oscillating occurs", and notes "Not all BD808's will self-oscillate, this is a matter of the tolerance" (Tiptop BD808 manual, URL above). **CONFIRMED** (manual text extracted). The "+/-15V of the original 808" quote (§0.4) is also **CONFIRMED** there.
  - Robin Whittle's 808 mod extends the decay so that "any decay time from the normal minimum to infinity can be achieved" (http://www.firstpr.com.au/rwi/tr-808/). **CONFIRMED**. The full sentence is "The Bass Drum decay range is extended to include self-oscillation, so any decay time from the normal minimum to infinity can be achieved with the Decay knob."
  - Forum lore says reducing **R164** below 47 k raises the gain. The original thread (gearspace) could not be fetched, so this is UNVERIFIED, though it is consistent with the formula.
- Implementation notes:
  - Map the decay knob to the buffer gain G = (47k ‖ 500k·k) / 47k.
  - Expose an "extended decay" mod that lets G_max reach the threshold. **Corrected from "about 1.0 or higher; soft clipper needed above 1"**: the threshold is G ≈ 0.94 with stock parts, so infinite decay sits at ≈0.94 and anything above it grows. A soft clipper is needed whenever G ≥ G_crit. In hardware the op-amp rails clip it.
  - The bridged-T and the buffer form a **delay-free loop**. Werner inserts one unit delay after the buffer ("negligible effects" because all features are far below fs), or the loop can be resolved analytically before discretisation [W14 §10]. **CONFIRMED** (W14 §10). Fact-checker caution: because stock G is 97 % of threshold, even a small unit-delay phase error measurably changes the long-decay Q. Prefer resolving the loop analytically, i.e. combining the analog prototypes before the bilinear transform, as W14 also suggests.

### 1.6 Attack: the frequency jump (not the sigh) [W14 §8.1, SM p5]
- On a trigger, Q41 turns on, Q42 turns off, and Q43 turns on and **shorts R165**. R_eff drops from R165+R166 to R166 (in parallel with R161 and R170).
  - DERIVED: R_eff 46.05 k → **6.66 k**, so f_c goes from 49.4 Hz to **≈130 Hz**, and passive Q from 2.3 to 6.1. That is "more than an octave" [W14]. **CONFIRMED** (recomputed 6.658 k → 130.0 Hz, Q 6.13; W14 §8 says "by more than an octave during the attack").
  - Roland's simplified description: "the filter's time constant — when ACCENT is present — is halved and has a resonance on twice its inherent frequency for a half cycle period" [SM p5]. **CONFIRMED** quote. Corrected: the original quote dropped the qualifier "when ACCENT is present".
    - This is **CONFLICTING** with W14, whose mechanism (Q42 drives Q43 from the 1 ms trigger) operates on every hit, accented or not.
    - The circuit supports W14 (DERIVED): even an unaccented 4 V trigger puts ≈4 × 2.7/(8.2+2.7) ≈ 1 V on Q41's base through R157/R158, which is enough to switch it. Roland's wording may mean the effect is only *pronounced* with accent.
    - Model: apply the attack window on every hit. Its audible effect scales with excitation level anyway.
- Duration:
  - Roland: Q43's ON period is set by R156·C38 and "equals 4ms which is 1/2 × 1/2 of 16ms of the inherent oscillation period" [SM p5]. **CONFIRMED** quote. Note that R156·C38 = 1 MΩ × 0.1 µF = 100 ms, so the 4–6 ms window is a threshold-crossing time of the Q41/Q42 network, not a simple RC constant. Do not derive it from R156·C38 directly.
  - Werner: the Q42 collector "doesn't settle back down to ground until approximately 5 ms after the trigger swings low", about **6 ms** in total [W14 §8.1]. **CONFIRMED** (W14 §8.1 also says "≈6 ms").
  - A physical replica measured **≈6.2 ms** against the 4 ms spec (Wolfram blog, https://blog.wolfram.com/2021/12/29/digital-vintage-sound-modeling-analog-drums-with-the-wolfram-language-and-system-modeler/). Corrected from "6.2–6.3 ms" with no caveat:
    - The blog's hardware replica ran on **±12 V Eurorack power**, not the 808's ±15 V. Its author says this makes the capacitor charge slower and the pulse longer.
    - The blog's simulation gave ≈5.3 ms.
    - So the stock ±15 V figure is probably nearer 5 ms.
  - **Use ~4–6 ms, and make it a hidden or "attack tune" parameter.**
- Perceptually this does not sound like a pitch shift (it lasts less than one period at the higher frequency). It makes the attack "punchier" and "crisper" [W14 §8.1].
- **Retrigger pulse.** When Q42 turns back on after ~4 ms, C39 (0.033 µF) discharges through R161 (1 MΩ) and D52, kicking the resonator again at its normal frequency. Without this, energy at the normal frequency would already have decayed and the note would jump in amplitude [SM p5, W14 §8.1].
  - Werner models it as a high-pass (C39/R161) followed by a diode clipper, the same way as the pulse shaper.
  - Changing C38 moves both the envelope length and the retrigger timing. That makes a good "bend" parameter.

### 1.7 Pitch sigh: the slow downward glide [W14 §8.2, Fig. 9, Fig. 11]
- The sigh is caused by **leakage through R161**, not by the attack envelope. **CONFIRMED** (W14 §8.2: "This sigh is actually a consequence of leakage through R161"; footnote 17 adds that the nonlinearity "is related to the physics of the p–n junction in Q43, not D52").
- Mechanism: when the bridged-T centre node V_comm swings below about one diode drop under ground, Q43's base is lifted and Q43 draws a collector current i_C. That lowers R_eff, which raises f_c. **The effect only operates on the negative half-cycles, and only while the amplitude is large.**
- Werner's memoryless fit (eq. 8): `i_C = −(m/α)·ln(1 + e^{−α(V_comm − V0)})`, with **α = 14.3150, V0 = −0.5560 V, m = 1.4765·10⁻⁵ A/V**.
  - The sign of V0 is inferred from the knee at ≈ −0.55 V in Fig. 9, because the text extraction dropped the minus sign. **CONFIRMED** directly: the rendered W14 p. 6 prints "V0 = −0.5560", and eq. 8 as `iC = −log(1 + e^{−α(Vcomm − V0)})·m/α`. α = 14.3150 and m = 1.4765·10⁻⁵ are also **CONFIRMED**.
  - Cross-check: i_C ≈ −14 µA at V_comm = −1.5 V, which matches the figure. **CONFIRMED** (recomputed −13.9 µA; Fig. 9 reaches ≈ −13 to −14 µA at −1.5 V).
- Then (eq. 9): `R_eff = V_comm·R166·(R165+R166) / (V_comm·(R165+R166) − R165·(V_comm − R166·i_C))`. When i_C = 0 this reduces to R165+R166. **CONFIRMED** against the rendered eq. 9.
  - Fact-checker note: at i_C = 0 the expression is literally (R165+R166)·R166/R166. Code should special-case |V_comm| → 0, where the formula is 0/0.
  - The model must track V_comm (the sum of the three inputs through transfer functions H_c1–H_c3) and update the bridged-T coefficients every sample.
- Shape of the result (Fig. 11, SPICE and model):
  - Instantaneous frequency starts at **~57–58 Hz**. **CONFIRMED** (W14 Fig. 11, read from the rendered page).
  - It shows **one upward blip per cycle** (scalloped, not a smooth curve). The blips shrink as amplitude falls. **CONFIRMED**: about 15 blips in 0.3 s at ≈50 Hz. Their peaks fall from ≈56 Hz at 50 ms to ≈52 Hz at 150 ms.
  - Also visible in Fig. 11: a brief **dip to ≈48 Hz around 15–25 ms**, just after the attack window and retrigger. A model should not be "corrected" to remove it.
  - It settles to the **≈49.5 Hz** baseline by about **200–250 ms**. Refined from "by about 200–250 ms": the blips are mostly gone by 250 ms, but sub-1 Hz scallops remain to ≈300 ms in Fig. 11, which is a single, presumably stock-setting, note.
  - arXiv 2502.07524: "the median range for the initial frequency sweep is close to one half-tone" (37 "long" samples), and "After the initial 0.4s-long attack, 'long' 808 samples are based on a single low-frequency sine wave". **CONFIRMED** quotes. CONFLICTING in magnitude:
    - W14 Fig. 11's 57.5 → 49.5 Hz is ≈2.6 semitones.
    - The library's median sweep is ≈1 semitone.
    - Likely causes are window length (0.2 s windows average over the blips), the decay/accent settings sampled, and unit-to-unit spread.
    - Treat sigh depth as a calibratable parameter, between ≈1 and ≈2.6 st.
- **Consequences to reproduce.**
  - The sigh is **amplitude-dependent**: accented notes sigh more.
  - Long-decay notes sigh for longer because the amplitude stays above the knee longer.
  - Retriggering while a note is still ringing adds to the existing filter state, so it must **never reset state**. Each hit then differs slightly and the "machine-gun effect" is avoided [W14 §11, Fig. 12].

### 1.8 Tone, level and output [W14 §9, SM p9]
- **Tone** is a 1st-order passive low-pass, fc = 1/(2π·R_eq·C45), where R_eq = R171 + (R172 ‖ VR5·l).
  - VR5 at 0: R_eq = 220 Ω, fc ≈ **7.2 kHz** (bright, clicky). **CONFIRMED** (recomputed 7.23 kHz; R171 220 Ω, C45 0.1 µF per W14 Fig. 1).
  - VR5 at max: R_eq = 5.22 kΩ, fc ≈ **305 Hz** (dark). **CONFIRMED** (recomputed 305 Hz; R172 10k ‖ VR5 10k(C)). Which knob direction is "max" resistance is UNVERIFIED; check the pot wiring before mapping.
  - These numbers are DERIVED and ignore loading by C47/VR4, which the paper says is minor.
  - VR5 is C-taper (reverse-log), so map the knob accordingly.
  - The click therefore comes from the pulse-shaper spike and the 130 Hz attack segment passing through this low-pass. TONE is mostly a "click level" control; Nepheton 2 describes its BD Tone as "Volume of the initial click" (https://d16.pl/pub/manuals/Nepheton%202-manual-gb.pdf, p7).
- **Level** (VR4 100k A taper) is a voltage divider between C47 and C49 [W14 Table 1].
- **Output high-passes:** C47/VR4, C49/R176 and C50/R174 are each about 1.6–3.4 Hz (DERIVED). They matter for DC and very long tails, but not for timbre. **CONFIRMED** (recomputed 3.4 / 3.4 / 1.6 Hz).
- Stated output levels: 3.5 Vpp normal, 10 Vpp accent at the multi-out [SM p14]. **CONFIRMED** (at LEVEL FCW, see §0.1).

### 1.9 Why units sound different [W14 §11, Tiptop manual, SM p15]
- Component tolerances were **±20 % on capacitors and ±5 % on resistors**. Inside a feedback loop these change gain, f_c, Q and decay significantly [W14 §11]. **CONFIRMED** (W14 §11 verbatim: "the voice circuits featured ±20% capacitors and ±5% resistors"). Tiptop's manual corroborates: "5% resistors and other parts with wide tolerances". For example, ±20 % on C41 and C42 together moves f_c by about ±20 %; recomputed −17 %/+25 %, since f ∝ 1/C. The G_crit ratio in §1.5 depends on C41/(C41+C42), so mismatched caps also move the self-oscillation threshold.
- Gate resistor R154 is 22 k on early lots versus 10 k later, which changes trigger amplitude and so the attack.
- Ageing: Tiptop found its reference 808s "sounded different from each other" and tuned its clone to the schematic math.
- **Suggested product feature:** a "unit variance" / "component tolerance" control that randomises a per-instance seed for C41, C42, R_eff and so on within tolerance.

### 1.10 Bends and mods exposed by emulations
- Werner's model exposes **tuning, attack tuning, extended decay and input pulse width** as "bends" [W14 §10]. **CONFIRMED**. Suggested mappings (DERIVED):
  - **Tune:** scale R_eff, or equivalently scale C41 and C42 together; f ∝ 1/sqrt(R_eff·C41·C42). For ±1 octave, scale R_eff by ×4 to ÷4. A hardware tune mod typically replaces part of R165/R166 with a pot (UNVERIFIED as a specific published mod). Whittle offers a "Bass Drum Tuning knob [that] enables higher and lower tuning" (http://www.firstpr.com.au/rwi/tr-808/).
  - **Attack tune:** scale R166 during the Q43 window.
  - **Attack / envelope time:** scale C38 (4–6 ms nominal).
  - **Pulse width:** the 1 ms trigger width. It changes the spacing between the positive and negative kicks.
  - **Extended decay:** G_max above 0.914 (see §1.5). Infinite decay is at G ≈ 0.94, not 1.0.
  - **Sigh on/off or amount:** scale the i_C nonlinearity. Werner lists "disconnecting the pitch sigh" as an example architectural bend [W14 §2].
  - **Bypass tone stage**, also from [W14 §2].
- Commercial reference points:
  - D16 Nepheton 2 BD: **Tone** (click level), **Decay**, **Sweep** ("release time of frequency envelope"), plus Level (Nepheton 2 manual p7). **CONFIRMED** against the manual text (page 7, as cited): "Tone – Volume of the initial click", "Decay – Amplitude release time", "Sweep – Release time of frequency envelope".
  - Roland's own TR-808 plug-in (ACB): Roland's product page **CONFIRMS** "Individual instrument gain lets you overdrive the internal circuitry" and "the ability to tune the bass drum and cowbell". The phrase "tuning and decay on each instrument" does **not** appear on Roland's page: UNVERIFIED, retailer copy only.
  - Tiptop BD808: an output stage that can reach 20 Vpp for overdrive, plus independent accent.

---

## 2. Snare drum (SD)

### 2.1 Signal flow [SM p6, SM p9]

```
1 ms trig (4–14 V) from Q45/Q46
  ├─► C57 0.0068µ ‖ R189 100k → R190 680 to gnd ─► IC14a (+)  LOW resonator  (~238–250 Hz) ─► C63 0.047µ ─► VR8 TONE end "1"
  ├─► R191 8.2k / R192 220 divider (≈2.6 %)      ─► IC14b (+)  HIGH resonator (~476–499 Hz) ─► R200 47k → C65 0.047µ ─► VR8 end "3"
  │                                                                      VR8 100k(B) wiper ─► R204 100k ─┐
  └─► VR9 SNAPPY 10k(B) (+R199 1k to gnd) ─► R186 33k ─► Q47 envelope follower ─► C51 0.47µ ...      │
                    snappy envelope ─► R184 15k / C52 0.1µ / R185 15k ─► D54 ─► Q48 "swing" VCA    │
     WHITE NOISE ─► C56 0.022µ ─► Q48 base (R194 2.2M bias)                                         │
                    VCA out ─► C66/C67 0.0018µ + R201 22k / R202 47k, Q49 follower (2-pole HPF)       │
                    ─► C69 0.01µ ─► R205 27k ──────────────────────────────────────────────────────┤
                                                       IC13a inverting mixer (R210 470k ‖ C70 220p ‖ VR7 LEVEL 100k(A)+C68 33µ) ─► SD out
```

### 2.2 Resonators [SM p6 "two bridged T-networks for fundamental waveforms and harmonic waveforms"; values SM p9]

**CONFIRMED.** All SD values in §2.1 and §2.2 were re-read from the SD section of the 400 dpi [SM p9] scan: C57–C61, C63, C65, C66, C67, C69, R184–R205, R210, VR7–VR9, D54. The f and Q columns recompute to 249.6 / 499.0 Hz and Q 17.4 / 10.7. The 238 / 476 Hz chart values are **CONFIRMED** [SM p14]. Neither resonator has a regeneration stage (**CONFIRMED** on the schematic).

| | C1 / C2 | R1 (to gnd) | R2 (bridge) | f (DERIVED) | Roland chart | Passive Q / τ (DERIVED) |
|---|---|---|---|---|---|---|
| Low (IC14a) | C58 0.027 µF / C59 0.027 µF | R196 680 Ω | R197 820 k | **249.6 Hz** | **238 Hz** (4.2 ms) | Q ≈ 17.4, τ ≈ 22 ms |
| High (IC14b) | C60 0.0068 µF / C61 0.0068 µF | R195 2.2 k | R198 1 MΩ | **499 Hz** | **476 Hz** (2.1 ms) | Q ≈ 10.7, τ ≈ 6.8 ms |

- The two resonators sit almost exactly an octave apart.
- The calculated values are about 5 % above Roland's chart, consistent with a component-tolerance or chart offset. **Use 238 / 476 Hz as the defaults.**
- Neither resonator has regenerative feedback, and the passive Q of the low mode matches the measured 60 ms voice decay well.
- **Excitation differs per resonator (DERIVED from values).**
  - The low resonator gets a *differentiated* spike: C57/R190, τ ≈ 4.6 µs, so effectively an impulse at the rising edge plus a negative one at the falling edge.
  - The high resonator gets a scaled-down *1 ms rectangle*. Its two edges are 1 ms apart, which is half the period of a ~500 Hz tone, so the second edge reinforces the ringing.
  - A model should feed each resonator through its own shaper.
- **"PORTION CHANGED" (later revision) printed on the schematic [SM p9]:** SD **C58 .027→.056, C61 .0068→.015, R191 8.2k→10k, R200 47k→0**. **CONFIRMED** (box at top right of p9, which also lists a CP change).
  - DERIVED effect: low mode ≈ **173 Hz**, high mode ≈ **336 Hz**. **CONFIRMED** by recomputation: 173.3 Hz with C58 .056 / C59 .027, and 336.0 Hz with C60 .0068 / C61 .015. Only one cap of each pair changes.
  - Correction to the semitone figure (was "both drop by about 6.5 semitones"): the change is **−6.3 st (low) and −6.8 st (high)**. The high/low ratio narrows slightly, from 2.00 to 1.94. The high resonator's output resistor is removed, making it louder in the TONE mix, and its excitation divider is slightly smaller.
  - **CONFLICTING / UNVERIFIED applicability.** The box gives no serial number, and it is absent from the serial-indexed change list on [SM p15].
    - The same manual's frequency chart [SM p14] still lists 238/476 Hz, which matches the *drawn* (pre-change) values within the 5 % offset seen elsewhere.
    - No primary measurement of a late-serial snare was found.
    - So it is unconfirmed whether 173/336 Hz is "production" or a running change.
  - The model should offer a "rev" switch or simply a wide SD tune range.
- SOS "Synth Secrets" says the snappy contour "is attenuated and added to the trigger itself, and the composite signal is then directed to both oscillators" (https://www.soundonsound.com/techniques/practical-snare-drum-synthesis). **CONFLICTING: SOS quote CONFIRMED verbatim, but contradicted by the schematic.** On my reading of the schematic, VR9 only taps the trigger pulse to drive the snappy envelope generator Q47. Fact-checker re-read of the 400 dpi SD section agrees:
  - VR9 (10k B) sits between the Q46 trigger line and R199 1k to ground. Its wiper feeds only R186 → Q47.
  - Q47's emitter (C51) feeds only R184/R185 → D54/Q48 (the VCA).
  - There is no path from C51 or Q47 to IC14's inputs. The resonators see only the raw trigger via C57/R189/R190 and R191/R192.

  SOS's own text says the contour generator was "left off the Roland diagram". Their description is a simplification. **Follow the schematic.**

### 2.3 TONE (VR8 100k B taper) [SM p6]
- "The output ratio of the two can be changed by VR8 to tailor sound characteristic."
- VR8 is a **crossfade pot**: the low resonator is on one end, the high resonator (through R200 47k) on the other, and the wiper goes to the mixer through R204 100k.
- Because the high-resonator leg has R200 in series, the high mode is attenuated even with the knob fully over (until the R200→0 revision).
- Nepheton 2 describes SD Tone as "Crossfade between audible one and two oscillators".

### 2.4 Noise path and SNAPPY (VR9 10k B taper) [SM p6, p9]
- "The amplitude of snappy envelope can be controlled by VR9" [SM p6]. **CONFIRMED** verbatim. SNAPPY scales the **amplitude** of the noise envelope. It does not change the decay time.
  - The envelope peak is proportional to trigger amplitude × VR9 position, so accent also brightens the snare.
- **Envelope.** Q47 (emitter follower) charges **C51 0.47 µF** during the 1 ms pulse. It then decays through R184 15k / C52 0.1 µF / R185 15k into the Q48 VCA.
  - DERIVED τ ≈ 14 ms or more, so about 30–35 ms to −20 dB, with a slightly rounded onset from the C52 RC. **UNVERIFIED** as a measurement.
    - The discharge path runs through R184+R185 (30 k) into a diode and transistor base. The decay is therefore not a clean RC and ends abruptly once D54/Q48 stop conducting.
    - C51 × 30 k = 14 ms is a lower bound on τ.
    - Roland's whole-voice 60 ms figure is consistent with it.
  - Confirmation that C51 is the snappy-decay cap: a common mod replaces "C51 from 0.47µF to 2.2µF" with "a 250k (B) Potentiometer with a 22k Resistor in series" in parallel, for variable snappy decay (electro-music thread, https://electro-music.com/forum/viewtopic.php?p=452698). **CONFIRMED**, with a caveat: the source is a 2017 forum post in a stripboard-clone thread ("C51 (C6 in the stripboard version) (0,47µf) changed to 2,2µf, in parallel a 250k (B) Potentiometer with a 22k Resistor in series"). It is not a published Roland or Whittle mod.
- **VCA.** Q48 is a "swing type VCA" [SM p5 Fig. 12]. The envelope feeds the collector through a resistor and diode (D54), and noise (C56 0.022 µF) drives the base. The output, taken at the envelope/diode node, is the envelope chopped by the noise, which is rich in harmonics.
- **Filter.**
  - After the VCA comes a unity-gain 2-pole Sallen-Key **high-pass**: C66 = C67 = 0.0018 µF, R201 22k (to the Q49 emitter), R202 47k (to ground). DERIVED: **fc ≈ 2.75 kHz, Q ≈ 0.73**. **CONFIRMED** (recomputed 2.750 kHz. For the equal-C unity-gain SK HPF, Q = ½·√(R_ground/R_feedback) = ½·√(47/22) = 0.731. The resistor roles were re-read from the schematic.)
  - Then C69 0.01 µF into R205 27k at the virtual ground, a further 1st-order HPF at about **590 Hz** (DERIVED; recomputed 589 Hz, **CONFIRMED**).
  - The noise sums into IC13a with about 3.7× the gain of the tone path (100k/27k; corrected from "470k/27k versus 470k/100k"). The feedback is R210 470k ‖ (VR7 + C68) ‖ C70, so the absolute gains depend on the LEVEL setting, but the ratio does not. The tone path also sees VR8's wiper resistance (up to 25 k) in series with R204, so the real ratio is about 3.7–4.6×. **DERIVED.**
  - Whittle's snare mod: Whittle's wording is "Reduced cutoff frequency in the Snappy noise pulse high pass filter, to make it gutsier in the lower mid-range" (corrected from the paraphrased quote). It confirms the HPF matters. **CONFIRMED.**
- Nepheton 2: SD "Decay – Noise amplitude release time", "Snappy – Crossfade between noisy and muddy sound". Nepheton adds a noise-decay knob that the original lacks. **CONFIRMED** (manual p7).

### 2.5 SD mods seen in the wild (Whittle, http://www.firstpr.com.au/rwi/tr-808/)
- "Two independent tuning pots ... for upper and lower resonators". "Tuning is at the normal pitch when these pots are anticlockwise (ACW) and is reduced by around an octave when they are clockwise (CW)". **CONFIRMED** (second quote verbatim; first is a paraphrase of "There are two Snare Tuning pots, one for the upper resonator ... and one for the lower").
- A sharper snappy attack.
- A lower HPF cutoff.
- Boosted tonal components.
- Snappy-decay pot (electro-music, above).
- **Suggested extra knobs:** SD Tune (lo/hi linked, 0 to −12 st), SD Snappy Decay, Noise HPF (Tone 2).

---

## 3. Toms and congas: LT/LC, MT/MC, HT/HC (shared circuits)

### 3.1 Signal flow [SM p6, SM p9]
- Three identical circuits: LT/LC on IC15, MT/MC on IC17, HT/HC on IC18.
- A front-panel slide switch per pair (SW8/SW9/SW10) selects tom or conga by switching:
  - an extra capacitor,
  - an input-attenuation resistor,
  - and the noise path in or out.

```
1 ms trig ─► Cin (LT .0068µ, MT/HT .0027µ) differentiator, D56/D58/D60 clamps neg. spike
         ─► R226/R255/R282 22k ─► (+) of bridged-T op-amp; R225 4.7k / R254 6.8k / R281 5.6k to gnd
            [conga position: extra shunt R224 1.5k / R253 2.2k / R280 1.5k to gnd → smaller excitation]
   bridged-T: C1, C2 (+ switched C for tom), R2 = 820k, R1 = R231(1k) + part of TUNING VR (500 Ω B)
              with anti-parallel Ge diodes (1S188FM) across R231 → amplitude-dependent R1 → pitch drop
   2nd half op-amp: inverter (33k/33k) ─► R230/R259/R286 820k back into C1/C2 junction (regeneration, fixed Q)
   inverter out ─► 100k ─► C (0.047/0.039/0.033µ) ─► level/mix amp (IC13b / IC16a / IC16b, LEVEL VR 100k A in fb, 470k ‖ 0.001µ)
TOM ONLY: trig ─► 4.7k ─► diode ─► C (LT .039µ, MT/HT .033µ) noise envelope, discharges via 2.2 MΩ
          PINK NOISE ─► 15k/3.3k divider ─► .047µ ─► 33k ─► transistor VCA (Q52/Q55/Q58)
          ─► band-pass (HPF .047/.033/.027µ + 22k; LPF 22k + .018/.015/.01µ) ─► switch (tom only) ─► 100k into mix amp
```

### 3.2 Resonator values and frequencies (values from [SM p9]; DERIVED frequencies against [SM p14])

**CONFIRMED** at 400 dpi for LT/LC and MT/MC; HT/HC values read from the overview scan:
- LT/LC: C76 .056 (inverting-input side), C78 .012, C77 .047 switched by SW8 across C78, R228 820k bridge, R230 820k regen from the IC15b inverter (R229/R227 33k), R231 1k, VR11 500 Ω(B), D80/D81 1S188FM, R224 1.5k switched shunt.
- MT: the MT throw of SW9 connects C89 .0082 from the inverting-input node to the C90/C92 junction, i.e. **in parallel with C90 (C1)**. This was verified at full resolution. C91 .027 parallels C92.

Every f in the table recomputes to ±0.1 Hz.
- R2 = 820 k in every circuit.
- R1 = R231/R260/R287 (1 k) + 0 to 500 Ω of TUNING pot (VR11/VR13/VR15, 500 Ω B taper), so **R1 spans 1.0–1.5 kΩ**.

| Voice | C1 | C2 | f at R1 = 1.0 k / 1.25 k / 1.5 k (DERIVED) | Roland LOW/MID/HIGH | Decay (−20 dB) |
|---|---|---|---|---|---|
| **LT** | C76 0.056 | C78 0.012 **+ C77 0.047** = 0.059 | 96.7 / 86.5 / 78.9 Hz | 80 / 90 / 100 Hz | 200 ms |
| **LC** | C76 0.056 | C78 0.012 | 214 / 192 / 175 Hz | 165 / 185 / 220 Hz | 180 ms |
| **MT** | C90 0.027 **+ C89 0.0082** = 0.0352 | C92 0.012 **+ C91 0.027** = 0.039 | 150 / 134 / 122 Hz | 120 / 135 / 160 Hz | 130 ms |
| **MC** | C90 0.027 | C92 0.012 | 309 / 276 / 252 Hz | 250 / 280 / 310 Hz | 100 ms |
| **HT** | C103 0.027 | C105 0.0056 **+ C104 0.022** = 0.0276 | 204 / 182 / 166 Hz | 165 / 185 / 220 Hz | 100 ms |
| **HC** | C103 0.027 | C105 0.0056 | 452 / 404 / 369 Hz | 370 / 400 / 455 Hz | 80 ms |

- The calculated ranges match Roland's tuning ranges within a few percent in every case. That confirms the topology reading: the tom position parallels an extra capacitor onto C2 (and onto C1 as well for MT), and the tuning pot varies R1 between 1.0 and 1.5 kΩ.
- **The tuning knob covers about ±0.35 octave (ratio 1.22–1.25).**
- Note that **HT and LC share the same range (165–220 Hz)** [SM p14].
- Passive Q ≈ 13 (LT). The 820 k regenerative feedback from the inverter roughly doubles it to the measured ≈ 25 (toms) and ≈ 40–45 (congas). Fit to the decay times.
  - **Now derivable rather than fitted (fact-checker, DERIVED).** Since the regen resistor equals the bridge resistor (820 k) and the inverter gain is 1, §0.2 gives Q_eff = Q_p·(C1+C2)/C2 = ω0·C1·R2 and **τ = 2·R2·C1**, independent of tuning.

    | Voice | τ | Q at mid tuning | Predicted −20 dB | Roland |
    |---|---|---|---|---|
    | LT | 91.8 ms | 25.0 | 212 ms | 200 |
    | MT | 57.7 ms | 24.4 | 133 ms | 130 |
    | HT | 44.3 ms | 25.4 | 102 ms | 100 |
    | LC | 91.8 ms | 55.4 | 212 ms | 180 |
    | MC | 44.3 ms | 38.4 | 102 ms | 100 |
    | HC | 44.3 ms | 56.3 | 102 ms | 80 |

  - The toms and MC are **CONFIRMED** against [SM p14] within 6 %.
  - LC and HC are **CONFLICTING**: ideal theory gives conga Q ≈ 55–56 versus ≈ 44–45 fitted from Roland's decays. The ideal model over-predicts conga ring by ≈ 20 %. A likely cause is the conga mode's large C1/C2 ratio, which makes Q_eff more sensitive to the inverter's and op-amp's non-idealities and to tolerance.
  - Recommendation: build the regen loop physically, then trim conga Q (or τ) to Roland's 180/100/80 ms.
  - Consequence for "Tom Decay" knobs: tuning does not change decay in stock hardware. Do not couple them.
- The toms and congas have no decay pot. Nepheton 2 and Whittle add one; Whittle's is "ACW is the normal decay time and CW is a much shorter decay time". **CONFIRMED** (Whittle verbatim; Nepheton 2 manual p7 lists Decay for both toms and congas).

### 3.3 Pitch bend on the attack [SM p6]
- Roland: "While the oscillation is large in amplitude immediately after triggering, it is on a higher frequency due to conductions of D80 and D81, which reduce time constant of the filter. As the resonance is damped, its frequency is lowered by the effect of increasing diodes' internal resistance. Timbre variations corresponding to time elapse will appreciably be heard as in the case of Bass Drum."
- The diodes are **1S188FM germanium** (parts list: "IS188FM Ge diode" [SM p16]), so the threshold is low (about 0.2–0.3 V, typical Ge, UNVERIFIED for this part).
  - Part number **CONFIRMED** on the schematic ("1S188FM" beside D80–D85).
  - It is a Sanyo germanium *point-contact* FM-detector diode (radiomuseum.org). A secondary datasheet summary gives V_F ≈ 1 V at its rated forward current (max I_F ≈ 4 mA). That value is UNVERIFIED and is a max-current figure, not a knee.
  - Point-contact Ge diodes have a soft knee from ≈0.1–0.2 V and a high bulk resistance (hundreds of Ω). They will not fully short R231 at the ≈1–5 V signal levels here. That supports the "few semitones, not 1.7×" expectation below.
- They form an anti-parallel pair from the C1/C2 junction to one end of the tuning pot. R231 (1 k) runs from the junction to the pot wiper. Large signal therefore puts the diodes in parallel with R231 plus part of the pot.
- **DERIVED upper bound:** with the diodes fully conducting, R1 falls from (1 k + R_b) to (1 k ‖ R_a) + R_b, i.e. to about 1/3 of its value. **f could rise by up to about 1.7× at the very start of a hit.** **CONFIRMED** as arithmetic: R1 ratio 0.33–0.36 across the pot travel, giving f × 1.67–1.73 with ideal (zero-resistance) diodes. The topology was re-read at 400 dpi: R231 runs from the junction to the VR wiper (pin 2), the diodes run from the junction to pot pin 1, and pin 3 goes to ground. In practice the diodes conduct only partially, so expect a *downward* bend of a few semitones over the first tens of ms, larger on accented hits. The exact magnitude is UNVERIFIED; measure or SPICE it.
- Implementation: R1(t) = R231 ‖ (R_a + r_d(v)) + R_b, with r_d from a Ge diode (Shockley) model driven by the junction voltage. This is the same per-sample coefficient update as the BD sigh.

### 3.4 The "reverb" noise on toms only [SM p6, SM p9]
- Roland: "Pink noise with a slightly longer decay time is mixed for Low Tom to provide artificial reveberation." All three tom circuits have this path; the manual describes LT/LC "as an example".
- In conga position the noise output is grounded by the tom/conga switch.
- **Envelope (DERIVED).**
  - The trigger charges C72 0.039 µF (LT) or C85/C100 0.033 µF (MT/HT) through 4.7 k and a diode.
  - The cap discharges through R218/R247/R276 (**2.2 MΩ**): τ ≈ 86 ms (LT), 73 ms (MT/HT), which is about 170–200 ms to −20 dB. That is "slightly longer" than the tone. Values **CONFIRMED** (C72 .039, C85 .033, R218/R247 2.2M on the schematic) and τ recomputed (85.8 / 72.6 ms).
    - Caveat (DERIVED): the 2.2 MΩ feeds the VCA transistor's base through R220/R248 33k. The VCA therefore stops conducting once the cap falls to about one V_BE.
    - The audible tail is shorter than the RC suggests and ends with a soft cutoff, not an exponential.
    - Note also that MT/HT's τ (73 ms) is *longer* than their tone τ (58 / 44 ms), while LT's (86 ms) is about equal to its tone's 92 ms.
  - The envelope and the pink noise (via a 15 k / 3.3 k divider) sum into the base of a transistor VCA (Q52/Q55/Q58).
- **Band-limiting (DERIVED).** The noise is filtered to the tom's own region:

| | HPF (cap + 22k) | LPF (22k + cap) |
|---|---|---|
| LT | 154 Hz (C74 .047) | 402 Hz (C75 .018) |
| MT | 219 Hz (C87 .033) | 482 Hz (C88 .015) |
| HT | 268 Hz (C101 .027) | 723 Hz (C102 .01) |

  Cut-offs **CONFIRMED** by recomputation from the confirmed parts (C74 .047, R223 22k, R222 22k, C75 .018 for LT). These are single-section figures. The HPF and LPF share a node (R222 loads R223), so the real band is somewhat narrower and lower in level (DERIVED).

  The result is a low-level "rumble", which Whittle calls a filtered noise pulse that is "barely perceptible". **CONFIRMED** (Whittle: "a pulse of filtered noise, which is barely perceptible, but gives them something of a rumbling sound").
- Pink noise is attenuated about 5.5× by the divider before the VCA. The mix level is low.
- Nepheton 2 exposes this as tom "Snappy – Crossfade between dirty and clean sound". **CONFIRMED** (manual p7). It is listed for **Low/Mid/High Tom only**: Nepheton's congas have Tunning and Decay but no Snappy, which matches the hardware grounding the noise in conga mode.
- Whittle's Noise Level pot at fully CW: corrected from the paraphrase "occasional Tom circuit overload and sporadic crackles". Verbatim: "Fully CW produces an extreme level of noise such that the Tom 'rumble' circuits are occasionally overloaded, **even when not being triggered**, so there will be sporadic crackles." **CONFIRMED.** The noise bus can overload the tom VCA, which is a nice optional saturation behaviour.

### 3.5 Output/mixer note
The tom mix amps have 0.001 µF across the 470 k feedback, in parallel with LEVEL (≤ 100 k). DERIVED: a gentle low-pass at about **1.9 kHz at full level**, higher at lower level settings, which softens the attack click. SD uses 220 pF in the same position (about 8.8 kHz). **CONFIRMED** (C83/C97 .001, C70 220p on the schematic; recomputed 1.93 kHz and 8.77 kHz with feedback = 470k ‖ 100k).

---

## 4. Rim shot / claves (RS/CL, shared circuit, switch SW11)

### 4.1 Signal flow [SM p6, SM p9]

```
1 ms trig (Q59/Q60)
 ├─► C111 0.01µ → D62 → R301 47k / R302 10k (R307 100k shunt) ─► IC21 bridged-T "LOW" (RS only)
 ├─► C114 0.001µ → D64 → R310 33k (R306 22k, R311 10k)         ─► IC20a bridged-T "HIGH"
 │       IC20a junction ─ R312 1k to gnd; SW11 connects junction to either:
 │          CL: R313 390k ◄─ IC20b (inverter, 33k/33k) output   → regenerative, high-Q → CLAVES
 │          RS: C118 0.0022µ ◄─ IC20a output (R313 disconnected) → lower freq; IC20b = buffer
 ├─► (RS) D61 → R107 1k → C24 0.47µ → R108 10k → D63 : envelope for Q62 "swing" VCA
 │       IC21 out (R318 220k / R317 10k / R320 22k) + IC20b out ─► Q62 (via C120 10µ)
 │       VCA out ─► 2-pole HPF (C121 = C122 = 0.0047µ, R322 10k fb, R325 330k) Q63 follower ─► R324 ─► SW11 "RS"
 └─► Q61 / C113 0.047µ / R305 1M ─► Q74 2SK30A JFET mute: opens IC19 only around a trigger (kills CL leakage)
CL: IC20b ─► R319 220k ─► SW11 "CL" ─► C125 0.01µ ─► IC19 mixer (R330 470k ‖ C153 22p ‖ VR16 LEVEL 100k(A)+C126 33µ) ─► RS/CL out
```

### 4.2 Resonators (values [SM p9], DERIVED f against [SM p14])

| | C1 / C2 | R1 | R2 | f DERIVED | Roland chart | Passive Q |
|---|---|---|---|---|---|---|
| CL (IC20a, CL mode) | C119 0.0022 / C117 0.0022 | R312 1k | R308 820k | **2526 Hz** | **2500 Hz** (0.4 ms) | 14.3, raised a lot by the R313 regen: effective Q ≈ 85 from the 25 ms decay |
| RS high (IC20a, RS mode) | C119 0.0022 / (C117 + C118) 0.0044 | R312 1k | R308 820k | **1786 Hz** | **1667 Hz** (0.6 ms) | 13.5, no regen |
| RS low (IC21) | C116 0.0047 / C115 0.0047 | R315 5.6k | R316 1M | **452.5 Hz** | **455 Hz** (2.2 ms) | 6.7 |

- Values **CONFIRMED** at 400 dpi:
  - CL/RS-high resonator: C119 = C117 = .0022, R312 1k, R308 820k, R313 390k, C118 .0022 switched by SW11 from IC20a's output to the junction (so RS mode puts C118 ‖ C117).
  - RS-low resonator: C115 = C116 = .0047, R315 5.6k, R316 1M.
  - Excitation and mixing: C111 .01, D62, R301 47k, R302 10k, R307 100k, C114 .001, D64, R310 33k, R306 22k, R311 10k, R314/R309 33k, R318 220k, R317 10k, R320 22k, R319 220k.

  All three f values recompute (2529 Hz with the 390 k loading, 1786 Hz, 452.5 Hz).
  - The RS-high calculation lands **7 % above** Roland's 1667 Hz. That is a larger gap than the other voices show (≈1–5 %). UNVERIFIED why; C118 tolerance is the obvious candidate.
- **CL is at or past the ideal oscillation threshold (fact-checker, DERIVED).** With G = 1 (IC20b inverter), R_fb = R313 390k, R2 = R308 820k and C1 = C2, §0.2 gives G_crit = (C1+C2)·R313/(C1·R308) = 2·390/820 = **0.951 < 1**.
  - An ideal-op-amp model of the claves therefore has *negative* damping and self-oscillates.
  - The real circuit decays in 25 ms (Q ≈ 85) only because of non-idealities: the µPC4558's finite gain and phase at 2.5 kHz, and tolerances.
  - This is consistent with Roland's remark below about CL being "wired for high Q", and with the need for the Q74 mute.
  - **Do not model the CL regen loop with ideal parts.** Use a resonator with Q fitted to ≈85 (25 ms to −20 dB), or include an op-amp GBW model and verify the decay.
- Roland: "Normally-conducting Q74 remains off only while trigger pulse is transferred ... to eliminate noise leaking from IC20, especially for CL — relatively large amount, being wired for high Q" [SM p6]. **CONFIRMED** verbatim.
  - The CL resonator is near self-oscillation, and the JFET mute gate is part of its sound.
  - DERIVED gate hold: C113·R305 ≈ 47 ms time constant.
  - In software this maps to "voice output enabled for a window after the trigger". It is not needed unless noise is modelled.
- In CL mode the IC21 (455 Hz) output still reaches IC20b's output through R320, but "can be ignored because of its minimized level due to impedance imbalance at pin 7 of IC20b" [SM p6]. **Claves is a single 2.5 kHz resonator.** **CONFIRMED** verbatim. The DERIVED gate hold C113·R305 = 0.047 µF × 1 MΩ = 47 ms recomputes; component values are from the overview scan.

### 4.3 Rim shot nonlinearity [SM p5 Fig. 12, SM p6]
- Roland: "The output of IC20b is applied to Q62 together with the output of IC21. The envelope applied to Q62 is formed by R107 and C24 ... VCA of this type is intended to provide many high harmonics in the output signals."
- The RS sound is therefore:
  1. Two resonators, about **455 Hz and about 1.67 kHz**, are summed.
  2. They drive a **transistor "swing" VCA**. This acts like a switch or half-wave clipper gated by a short exponential envelope.
     - DERIVED: τ ≈ R108·C24 = 4.7 ms, so about 11 ms to −20 dB. Roland's table says **RS decay 10 ms**. Values **CONFIRMED** at 400 dpi (D61 → R107 1k → C24 .47/50 → R108 10k → D63).
     - Roland attributes the envelope to "R107 and C24". R107·C24 = 0.47 ms is the charge (attack) time; the 4.7 ms discharge via R108 is the fact-checker's reading, and is consistent with the 10 ms figure.
     - Separately: the passive RS-low resonator alone has τ = 4.7 ms (Q 6.7), which coincidentally gives the same ≈11 ms. The voice decay is set by both.
  3. The result goes through a **resonant 2-pole Sallen-Key HPF**. DERIVED: **fc ≈ 590 Hz, Q ≈ 2.9**. **CONFIRMED** (C121 = C122 = .0047, R322 10k to the Q63 emitter, R325 330k to ground; recomputed 589 Hz, Q = ½·√(330/10) = 2.87).
- Model it as `(resonator sum) → nonlinear VCA: out = env · clip/rectify(sig)` (or a transistor model), followed by an HPF at 590 Hz, Q 2.9. The VCA step produces the characteristic clicky, metallic "tick". This is the main nonlinearity among the bridged-T voices; there is no true ring modulator.
- Nepheton 2 gives RS and CL "Tunning – Base frequency" and "Decay", both added beyond the original. **CONFIRMED** (manual p7).

---

## 5. Maracas (MA) (not bridged-T; brief) [SM p6, SM p9]
- Roland: "White noise is gated by Q65 and supplied to the same buffer IC19 as for the CP sound generator through the filter Q68. Envelope for MA sound generator is generated by Q66 and Q67."
- MA shares an output and level control with CP: the CP/MA switch SW12 and VR17.
- **Envelope (DERIVED).**
  - The trigger goes through D67 into C135 0.1 µF ‖ R345 150 k (τ 15 ms), then R344 220 k to Q67.
  - Q67 drives an RC (R341 470 k / D66 / C134 0.033 µF) to Q66, so the **rise is slowed**. The secondary summary describes it as a triangular, extended-attack envelope (scrapbox summary of the service manual, https://scrapbox.io/0b5vr/TR-808; secondary source).
  - Values **CONFIRMED** at 400 dpi (C135 .1, R345 150k, R344 220k, R343 100k, R341 470k with D66 across it, C134 .033, R337 10k). Fact-checker reading of D66's orientation (DERIVED):
    - When Q67 turns on, C134 discharges only through R341. This is the slow attack: τ ≈ 470k × 0.033 µF ≈ **15.5 ms**.
    - When Q67 turns off, C134 recharges through R343 and D66. This is the faster release: τ ≈ 100k × 0.033 µF ≈ **3.3 ms**.
    - This gives the extended-attack, quick-release shape. UNVERIFIED against a scope trace.
  - Roland's table gives 25/35 ms. **CONFIRMED**: printed in the SHORT and LONG columns respectively.
- **Filter (DERIVED).** Q68 is a 2-pole Sallen-Key HPF: C132 = C133 = 0.001 µF, R339 3.3 k, R340 68 k, so **fc ≈ 10.6 kHz, Q ≈ 2.3**. There is also R338 15k / C131 0.01 µF shaping ahead of it. **CONFIRMED** (R339 is the feedback leg to the emitter and R340 goes to ground; recomputed 10.62 kHz, Q 2.27).
- Levels: 3 Vpp normal, 5 Vpp accent [SM p14]. That is the weakest accent response of any voice. **CONFIRMED.**
- Nepheton 2: MA "Tone – Noise color". **CONFIRMED** (manual p7).

---

## 6. Implementation guidance distilled (for the DSP design)
1. **One generic "808 bridged-T" block.** Use a 2nd-order resonator whose coefficients come from (R1, R2, C1, C2) and are updated per sample. Its inputs are:
   - the shaped trigger into the + input,
   - a regenerative feedback input (gain G),
   - and, for BD, a third input for the retrigger pulse.

   Use a topology that tolerates time-varying coefficients: Werner used TDF-II and suggests the normalised ladder or phasor filters as better options [W14 §10]. A TPT/SVF resonator is the usual modern choice. Coefficients can come from the bilinear transform without pre-warping, because all features are far below fs [W14 §10]. The claves at 2.5 kHz should still be pre-warped or oversampled.
2. **Never reset filter state on retrigger.** The overlap behaviour is part of the sound [W14].
3. **Accent = excitation amplitude (4 V → 14 V, about 3.5×) into the nonlinear parts.** Do not scale the output. This gives:
   - BD: constant negative kick plus a stronger sigh.
   - Toms: more diode pitch bend.
   - SD: snappy envelope height (noise louder relative to tone).
   - RS: VCA saturation.
4. **BD-specific state machine:**
   - 0 to 4–6 ms: R165 shorted (f ≈ 130 Hz).
   - At the end of that window: retrigger pulse through C39/R161.
   - Throughout: per-sample R_eff(V_comm) sigh nonlinearity (Werner eq. 8–9).
   - Feedback-buffer gain G(k) = (47k ‖ 500k·k) / 47k, at most 0.914 stock. Self-oscillation threshold G_crit ≈ 0.94 (§1.5; corrected from an implied 1.0).
   - Tone: 1-pole LP from 305 Hz to 7.2 kHz.
5. **Default tunings:** BD 49.5 Hz steady state (sigh from about 57 Hz); SD 238/476 Hz; toms 90/135/185 Hz; congas 185/280/400 Hz; RS 455 + 1667 Hz; CL 2500 Hz. All **CONFIRMED** against [SM p14] and W14. The BD 49.5 Hz figure stays CONFLICTING with Roland's 56 Hz; see §1.4.
6. **Fit Q to Roland's decay table** (−20 dB times) rather than trusting passive Q, wherever regeneration is present. Fact-checker addition: the §0.2 closed form Q_eff = Q_p·(C1+C2)/(C1+C2 − G·C1·R2/R_fb) predicts the toms, MC and the BD 12 o'clock decay within 10 %. Use it as the physical model and trim only:
   - BD at FCW (≈800 ms, not the ideal 1.25 s);
   - the congas LC/HC (ideal ≈20 % too long);
   - CL (ideal-unstable; fit Q ≈ 85).
7. **Extra knobs worth exposing**, all grounded in real hardware mods:
   - BD Tune (±12 st), BD Attack/"Sweep" time (C38), BD Sigh amount, BD Extended Decay (G_max up to the ≈0.94 threshold and slightly beyond, with a soft clip; corrected from "1.0 or more"), BD drive/overdrive.
   - SD Tune (lo/hi), Snappy Decay (C51), noise HPF.
   - Tom/Conga Decay (shorter only, per Whittle), Tom noise amount. Stock decay does not depend on tuning (τ = 2·R2·C1, §3.2).
   - RS/CL Tune and Decay.
   - A per-instance component-tolerance seed.
   - SD "revision" switch (the C58/C61 change).

## Sources
- Roland TR-808 Service Notes (1981): https://manuals.fdiskc.com/flat/Roland%20TR-808%20Service%20Manual.pdf. Pages used:
  - p1: specs
  - p4: trigger gate and accent
  - p5: bridged-T, swing VCA, BD description
  - p6: SD, toms, RS/CL, MA descriptions
  - p9: voicing schematic and component values, "Portion changed"
  - p14: amplitude, frequency and decay table; definition of decay
  - p15: design changes
  - p16: parts list (pot tapers, Ge diodes)
- Werner, Abel, Smith, DAFx-14 BD paper: https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf
- Werner, Abel, Smith, "The TR-808 Cymbal", ICMC-SMC 2014: https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf (context only)
- "Harmonic and transposition constraints arising from the use of the Roland TR-808 bass drum" (arXiv 2502.07524): https://arxiv.org/html/2502.07524v1
- Robin Whittle, Modifications for the TR-808: http://www.firstpr.com.au/rwi/tr-808/
- Tiptop Audio BD808 manual: https://www.polynominal.com/sample-packs/TIPTOP-bd808/Tiptop_Audio_BD808_user-manual.pdf
- Wolfram blog, modelling the 808 kick: https://blog.wolfram.com/2021/12/29/digital-vintage-sound-modeling-analog-drums-with-the-wolfram-language-and-system-modeler/
- SOS Synth Secrets, Practical Snare Drum Synthesis: https://www.soundonsound.com/techniques/practical-snare-drum-synthesis
- electro-music.com snare mod thread: https://electro-music.com/forum/viewtopic.php?p=452698
- D16 Nepheton 2 product page and manual: https://d16.pl/nepheton2, https://d16.pl/pub/manuals/Nepheton%202-manual-gb.pdf
- Roland TR-808 software (ACB): https://www.roland.com/uk/products/rc_tr-808/
- Secondary summary (frequency list from the service manual): https://scrapbox.io/0b5vr/TR-808
- Radiomuseum entry for the 1S188 / 1S188FM (Sanyo Ge point-contact detector diode): https://www.radiomuseum.org/tubes/tube_1s188.html

## Verification log

Independent fact-check pass, 2026-10-02. Method: primary sources were read as images wherever text extraction was lossy, and every DERIVED number was recomputed in Python from the confirmed component values.

**Primary sources re-read**

1. **Roland TR-808 Service Notes** (local copy of the cited PDF, 400 dpi CCITT scans; page 9 extracted at full 6614×4672 resolution).
   - p1 spec block: accent 0–10 dB.
   - p4 Trigger Gate text and Figs. 6–7: 10 µs → ≈1 ms; 4 V unaccented; 5–15 V on Q32 → 4–14 V trig; CB/CY/OH/CH 7–14 V.
   - p5 Sound Generators / Bass Drum text: Q43 4 ms; the "16 ms" period; the "when ACCENT is present" qualifier.
   - p6 SD, LT/LC, RS/CL text: all quotes verified verbatim.
   - p9 schematic, crops at full resolution:
     - SD section, all parts;
     - LT/LC section (topology of C77, R231, VR11, D80/D81, R224);
     - MT/MC (C89 parallel to C90 via SW9: verified);
     - RS/CL (C117–C119, R312, R313, IC20b inverter, Q62 envelope R107/C24/R108, Q63 HPF);
     - MA (Q65–Q68);
     - the "PORTION CHANGED" box.
   - p14 "Checking voices" procedure, the decay-time definition diagram, the full amplitude/frequency/decay table, and the adjustment table (noise 130 mV rms).
   - p15 design changes (gate resistors 22k→10k at 010600; noise generator change).
2. **Werner/Abel/Smith DAFx-14**, downloaded and rendered at 150–200 dpi.
   - Fig. 1 schematic: every BD value matched.
   - Eq. 5, 6, 7, 8, 9 and Table 1.
   - §5: 49.5 Hz, and "close to" 56 Hz.
   - §8.1: ≈6 ms, and "5 ms after the trigger swings low".
   - §8.2: R161 leakage; α = 14.3150, V0 = −0.5560 (minus sign printed), m = 1.4765e−5.
   - Figs. 9 and 11, read visually.
   - §11: ±20 % caps, ±5 % resistors.
3. **arXiv 2502.07524 (HTML).** 49.48 Hz is the distribution peak (not a 37-sample median); 51.05 Hz secondary for short presets; the 37 long samples give a median sweep of about a half-tone; "0.4s-long attack". The data come from a sample library, so hardware provenance is unknown.
4. **Wolfram blog (2021).** Hardware replica ≈6.2 ms on ±12 V; simulation ≈5.3 ms; Roland spec 4 ms.
5. **Tiptop BD808 manual (PDF text).** "+/-15V of the original 808"; "right before self-oscillating occurs"; "Not all BD808's will self-oscillate, this is a matter of the tolerance"; 20 Vpp; 5 % resistors.
6. **Whittle TR-808 mods page.** BD tuning knob; decay extended to self-oscillation; two snare tuning pots, about an octave down; snappy HPF "gutsier"; tom decay ACW/CW; tom noise "barely perceptible" and crackles "even when not being triggered".
7. **D16 Nepheton 2 manual (local text extract, p7).** All per-instrument parameter descriptions. Toms have Snappy; congas do not.
8. **SOS Synth Secrets.** The snappy-to-oscillators quote is verbatim, but the schematic contradicts it.
9. **electro-music thread p=452698.** The C51 2.2 µF + 250k(B) + 22k mod, from a 2017 stripboard-clone post.
10. **Roland ACB product page.** Overdrive and BD/CB tuning quotes confirmed; "tuning and decay on each instrument" not found there.
11. **1S188FM.** Radiomuseum identifies it as a Sanyo Ge point-contact FM detector diode. No primary datasheet curve was found.

**Recomputed (all match the document unless listed under corrections)**
- Bridged-T f/Q for:
  - BD (49.44 Hz, Q 2.33; attack 130.0 Hz, Q 6.13);
  - SD (249.6 / 499.0 Hz; revision 173.3 / 336.0 Hz);
  - all six tom/conga f triplets;
  - CL 2526 Hz; RS 1786 / 452.5 Hz.
- BD G(k) series; pulse-shaper τ and ratio.
- Tone LP 7.23 kHz / 305 Hz; output HPFs.
- τ and Q columns of the master table.
- SK HPFs: SD 2.75 kHz / Q 0.73, RS 589 Hz / Q 2.87, MA 10.6 kHz / Q 2.27, using Q = ½·√(R_ground/R_feedback). Resistor roles were verified on the schematic.
- Tom noise τ (85.8 / 72.6 ms) and band edges; mix-amp LPs (1.93 kHz / 8.77 kHz); Q74 gate (47 ms); RS envelope (4.7 ms).
- Werner eq. 5 coefficients re-derived symbolically (sympy); they match.

**New derivation added:** the regenerated-Q closed form and oscillation threshold (§0.2), derived by nodal analysis and checked against Roland's decay table:
- toms and MC within 6 %;
- BD 12 o'clock 330 vs 300 ms;
- congas LC/HC about 20 % long;
- BD FCW 1.25 s vs 800 ms;
- CL ideal-unstable (G_crit 0.951).

**Corrections made**
- Decay measured from waveform onset, not first peak.
- W14 eq. 7 α2 typo: R170 is missing from the printed s² term.
- BD self-oscillation threshold is G ≈ 0.94, not 1.0; extended-decay guidance updated accordingly.
- Roland's attack description carries "when ACCENT is present".
- arXiv 49.5 Hz figure is the distribution peak, and the 37 samples belong to the sweep statistic.
- Wolfram 6.2 ms was measured on ±12 V.
- SD revision shifts are −6.3 / −6.8 st, not "about 6.5" for both.
- SD noise/tone gain ratio basis: 100k/27k, with VR8 wiper resistance in the tone path.
- Whittle quotes made verbatim.
- Section cross-reference §1.6 → §1.8.
- Nepheton congas have no Snappy.
- ACB "tuning and decay on each instrument" not on Roland's page.

**Still uncertain**
- BD steady pitch: 49.5 Hz (math, W14, sample library) vs Roland's 56 Hz chart and the 62.5 Hz implied by the p5 text.
- Sigh depth: ≈2.6 st in the W14 SPICE vs ≈1 st median in the sample library.
- Whether the "PORTION CHANGED" SD values (173/336 Hz) apply to production units, and from which serial.
- Exact tom diode pitch-bend depth (1S188FM I–V curve not found).
- Why the conga decays and the RS-high pitch deviate from the ideal predictions.
- BD SHORT/LONG knob positions in Roland's table.
- MA envelope shape (DERIVED from D66 orientation only).
- The ACB per-instrument parameter list.

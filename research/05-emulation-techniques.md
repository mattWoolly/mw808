# 05 — How TR-808 emulations are built: DSP techniques and the math to implement them

Scope: modelling approaches, the bridged-T resonator (math + discretisation), the metallic oscillator bank, noise/clap, realtime and performance practice, the sample-accurate sequencer, analog variance, and validation. Wherever possible I give component values, Hz, ms and formulas you can code directly.

Tags used below:
- **[MEASURED]**: numbers I measured myself in this session from the CC0 Fischer TR-808 sample set (serial #103852, 44.1 kHz/16-bit, individual outputs; see §8). The analysis scripts were throwaway.
- **[DERIVED]**: my own algebra from the cited component values or transfer functions. Check it before relying on it.
- **UNVERIFIED**: I could not confirm it from a primary source.

No source code is reproduced. Open-source projects are described at the technique level only.

---

## 0. Executive summary (design-critical facts)

1. **The core of every 808 tonal voice is a bridged-T band-pass in an op-amp's feedback path, kicked by a 1 ms trigger pulse.** BD, SD, toms/congas and RS/CL use it to ring. CP, CB, CY and the hats use it as a plain band-pass. [Werner DAFx-14, fn. 15]
2. **Bridged-T math (ideal op-amp)**, with capacitive arms C1, C2, a bridge resistor Rb and a resistor R to ground:
   `ω0 = 1/√(R·Rb·C1·C2)`, `ω0/Q = (C1+C2)/(Rb·C1·C2)` (independent of R), and `Q = √(Rb·C1·C2/R)/(C1+C2)`, which is `½√(Rb/R)` when C1 = C2.
   The amplitude decay time constant is `τ = 2Q/ω0 = 2·Rb·C1·C2/(C1+C2)`, i.e. `Rb·C` for equal capacitors. **Retuning through R changes pitch but not the decay time.** [DERIVED from Werner's H(s)]
3. **BD values** (Werner thesis Tables 2.3/4.2): R167 = 1 MΩ, C41 = C42 = 15 nF, R165 = 47 k, R166 = 6.8 k, R161 = 1 M, R170 = 470 k, R169 = 47 k, R164 = 47 k (one table says 4.7 k; 47 k is the value consistent with measurements), VR6 = 500 k, C43 = 33 µF, C39 = 33 nF.
   - Normal state: Reff = 46.05 kΩ, so **f0 = 49.4 Hz** and bare Q = 2.33. [MEASURED] settled pitch is **49.6 Hz** on every sample.
   - Attack state (Q43 shorts R165 for about 6 ms): Reff = 6.66 kΩ, so **f ≈ 130 Hz**, Q ≈ 6.1. [MEASURED] the first cycle lasts about 6 ms (≈ 166 Hz equivalent).
4. **BD decay = positive feedback around the resonator.** Bare τ = R167·C = 15 ms. Loop gain at resonance is `L = [(R169‖VR6·k)/R164]·[R167·C41/(R170·(C41+C42))]`, and the closed-loop decay rate is `σ = (1−L)·(C41+C42)/(2·R167·C41·C42)`, giving `T60 = 6.91/σ`.
   - Predicted T60: 0.10 s at minimum decay, 3.75 s at maximum.
   - [MEASURED]: 0.12 s and 3.69 s.
   - The middle of the knob range depends on the pot taper; fit it from the samples.
5. **The BD "pitch sigh" depends on amplitude, not time.** It comes from Q43 conducting when the internal node Vcomm swings more than about 0.56 V negative, which lowers the effective R.
   - [MEASURED]: about 57 Hz at 22 ms, falling to 49.65 Hz once the amplitude drops below about 0.3 of peak. A long decay keeps the sigh going longer.
   - Model it as resonator self-FM through a soft-knee (softplus) function, not as a fixed pitch envelope.
6. **Never reset resonator state on retrigger.** Add the new excitation into the ringing filter. This avoids the "machine-gun effect" and is why consecutive 808 hits differ. [Werner DAFx-14 §11]
7. **Discretisation choice:** a TPT/ZDF state-variable filter (pre-warped, stable under per-sample modulation) for all resonators, or the Mathews–Smith phasor filter where amplitude-preserving frequency modulation matters.
   - Werner used bilinear TDF-II plus a fictitious unit delay to break the BD feedback loop.
   - With an SVF the feedback loop folds exactly into the damping term: `k_eff = (1−L)/Q`.
8. **Metallic bank:** six Schmitt-trigger squares at 205.3, 304.4, 369.6, 522.7, 540 (trimmed) and 800 (trimmed) Hz, duty ≈ 48 %.
   - The bank is shared by CB, CY, OH and CH, and it free-runs, so the phase is random on every hit.
   - [DERIVED by simulation at 48 kHz]: alias-to-signal ratio in the 2–5, 5–9 and 9–16 kHz bands is naive −21/−14/−8 dB; naive at 4× −33/−26/−20 dB; **polyBLEP at 1× −67/−47/−28 dB; polyBLEP at 2× −84/−67/−50 dB**.
   - Use polyBLEP + 2× oversampling for metallic voices. This matters more than anything else in the metallic chain.
9. **Clap** = noise → BPF ≈ 1 kHz, gated by a repeating sawtooth envelope (3 bursts about 10–12 ms apart) summed with a longer "reverb" decay.
   - [MEASURED]: bursts at ≈ 2, 12 and 25 ms, the main hit at ≈ 37 ms, and a spectral peak at 1005 Hz.
10. **Sequencer:** derive every step time from the host PPQ in each block. Never accumulate sample counts. Use half-open intervals. Split the block at loop ends, detect jumps, and fire at the sample offset.

---

## 1. Modelling approaches

### 1.1 Werner, Abel & Smith (CCRMA): physically-informed, circuit-bendable block models

- Papers:
  - **BD**, DAFx-14: https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf
  - **Cymbal**, ICMC/SMC 2014 (CC-BY 3.0): https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf
  - **Cowbell**, AES 137th Conv. 2014 (paywalled): https://aes2.org/publications/elibrary-page/?id=17530
- **Method:**
  - Split the schematic into blocks wherever a low-output-impedance stage drives a high-input-impedance stage, so loading between blocks is negligible.
  - Derive each block's continuous-time transfer function by nodal analysis, written in terms of component values so "mods" are just value changes.
  - Model nonlinear parts as **memoryless nonlinearities after linear filters**, with parameters fitted to SPICE.
  - Discretise with the bilinear transform. Werner used TDF-II, no pre-warp for the BD, and 4× oversampling for the cymbal.
  - Break delay-free loops with a unit delay. [BD §10; Cymbal §12]
- **BD block list** (DAFx-14 Fig. 1/2):
  - trigger logic (1 ms pulse, amplitude = common-trigger voltage 4–14 V set by the global accent pot VR3)
  - pulse shaper (nonlinear low-shelf)
  - bridged-T resonator with three inputs: pulse, feedback buffer, retrigger pulse
  - feedback buffer (sets decay)
  - envelope generator (Q41/Q42) that raises f0 during the attack and fires the retrigger pulse
  - output stage: tone low-pass → level → high-pass
- **Werner's conclusion:** "the architecture of the 808 bass drum and complex interactions between subcircuits are more important than subtle device nonlinearities" (DAFx-14 §12). This is the key design guidance: get the topology and time-variation right before fussing over transistor curves.
- **Cymbal block list** (ICMC 2014):
  - 6 Schmitt oscillators → passive mix (attenuation set by R53) → **two** active BPFs (3rd-order, ≈ 3440 Hz and ≈ 7100 Hz)
  - trigger → attack smoother → three envelope generators → three "swing-type" VCAs (nonlinear)
  - three Sallen-Key HPFs (2nd, 2nd, 3rd order; HPF #3 resonates near 10.5 kHz)
  - 5th-order passive tone network → level/output buffer, which is a differentiator with a +6 dB/oct slope in band
- **Cowbell** (AES abstract): the same approach, with the BPF solved by Mason's gain formula. Details are paywalled.
  - Sound On Sound measured the two tones at ≈ 587 and 845 Hz, a BPF at 2.64 kHz, and a two-stage "abrupt initial drop, then tail" envelope, quoting Roland's notes on R82/C34 ∥ C9. https://www.soundonsound.com/techniques/synthesizing-cowbells-claves
  - [MEASURED] the Fischer CB: spectral peaks at 559 and 824 Hz; overall T60 ≈ 0.7 s.

### 1.2 Werner's PhD thesis: wave digital filters (WDF) on the 808 BD

- *Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters*, Stanford 2016. Full PDF: https://stacks.stanford.edu/file/druid:jy057cz8322/KurtJamesWernerDissertation-augmented.pdf
  - Note: the thesis is about the **BD only**. It contains no snare, hat, clap or cowbell chapters. Those exist only as the 2014 papers above, or not at all.
- **Ch. 1** (linear WDF): the BD output/tone filter, with values in Table 1.1:
  - R171 = 220 Ω, R172 = 10 k, R173 = 6.8 k, R174 = 100 k, R176 = 100 k, R177 = 82 k
  - VR4 = 100 k, VR5 = 10 k
  - C45 = 0.1 µF, C47 = 0.47 µF, C48 = 220 pF, C49 = 0.47 µF, C50 = 1 µF
  - 2SC945 emitter follower
- **Ch. 2:** an MNA-derived **R-type adaptor** for non-series/parallel topologies, demonstrated on the bridged-T resonator, with and without feedback (Table 2.3).
- **Ch. 3:** single nonlinearity, the pulse shaper diode. R162 = 4.7 k, R163 = 100 k, C40 = 15 nF.
- **Ch. 4:** multiple nonlinearities.
  - The envelope generator (Table 4.1): R156 = 1 M, R157 = 8.2 k, R158 = 2.7 k, R159 = 100 k (tuned to 106 k), R160 = 22 k, R161 = 1 M, C38 = 0.1 µF, C39 = 33 nF, plus a fictitious 6.8 nF "C0" added for Newton–Raphson convergence.
  - The **nonlinear bridged-T** (Table 4.2), with the Q43 transistor and D52 diode solved by Newton–Raphson.
- **Accuracy and cost notes from the conclusion:**
  - Each stage is fine at 44.1 kHz on its own, but errors accumulate in the full chain, so the full-BD comparison ran at **44.1 kHz × 8**.
  - The 1 ms pulse edges were ramped over 4/44100 s to ease the solver.
  - WDF cost grows roughly linearly with element count. State-space cost grows roughly quadratically with the number of reactive elements.
- **Implication for mw808:** a full WDF BD is feasible (chowdsp_wdf has R-type adaptors) but costs 8× oversampling plus Newton iterations. The block model in §2 reproduces the measured behaviour at 1× for a tiny fraction of that cost. Recommended: block model as the default engine. A WDF engine is optional and later.

### 1.3 Roland ACB (what is publicly known)

- Press release, 2014: ACB "utilizes original design specs, consultation with original engineers, and a detailed part-by-part analysis of each analog circuit using Roland's collection of pristinely archived drum machines." https://www.roland.com/us/company/press_releases/2014/1781/
- RC TR-808 plug-in page: ACB "involves carefully analyzing original hardware units, circuit diagrams, and other historical data". Features listed there include:
  - per-instrument gain to "overdrive the internal circuitry"
  - flams, sub-steps, soft hits
  - per-instrument last step and shuffle
  - tuning on select drums
  - https://www.roland.com/us/products/rc_tr-808/
- No algorithm, oversampling or solver details have been published. Third-party claims that ACB models "minute pitch variations, decay fluctuations" are reseller and blog copy: **UNVERIFIED**. Werner (DAFx-14 fn. 2) calls ACB "presumably a form of physical modeling".

### 1.4 D16 Nepheton 2 (what is publicly known)

- The product page says only that the sounds are "fully synthesized". It gives no oversampling or circuit-model details. https://d16.pl/nepheton2
  - Werner notes that the original Nepheton's marketing "mention[s] circuit modeling" (DAFx-14 fn. 2).
- Publicly listed features (useful for the feature list):
  - per-step flam and sub-step variants
  - tap, randomiser, hundreds of patterns
  - pattern triggering via MIDI note, CC or GUI
  - accent/non-accent velocity with a global accent boost
  - per-instrument EQ (HP/LP with resonance + 2-band parametric) and compression
  - two FX buses, a 3-band master compressor, a limiter with soft clip
  - audible trigger outputs, MIDI drum-map editor, drag-and-drop MIDI export, MIDI learn
  - Nepheton 1 had 12 outputs.
- Everything beyond "synthesised" about the engine is **UNVERIFIED**.

### 1.5 Behavioural / modal approximations (simpler emulations)

- **Mutable Instruments Plaits "analog bass drum"** (MIT, "808 bass drum model, revisited"). Technique only:
  - a 1 ms pulse whose height scales with accent
  - a one-pole high-pass of the pulse plus a ≈ 4.4 % DC leak (matching R162/(R162+R163) = 0.045), then an asymmetric soft "diode"
  - a separate 6 ms "FM pulse" that raises the resonator frequency during the attack (Q41/Q42/Q43)
  - a retrigger pulse at the end of the FM pulse that decays exponentially (C39/R161)
  - "self-FM": resonator low-pass output → soft diode → frequency offset, which is the amplitude-dependent sigh (R170 leakage)
  - an SVF resonator whose Q scales with frequency so decay time stays constant
  - a one-pole tone low-pass that also mixes in some of the exciter
  - https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/drums/analog_bass_drum.h
- **Plaits hi-hat:**
  - six naive square phase accumulators summed (no anti-aliasing)
  - SVF band-pass colouration
  - a "swing VCA" made as an asymmetric soft clipper: about 40× more gain on positive half-waves than negative, then x/(1+|x|)
  - an exponential envelope with an optional two-stage decay
  - an SVF high-pass
  - https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/drums/hi_hat.h
- **Modal / impulse-invariant:**
  - A pure decaying sinusoid `A·e^{−σt}·sin(ωt+φ)` per resonant mode, with σ and ω taken from the bridged-T formulas in §2.
  - Cheap, and exact in frequency and decay at any sample rate.
  - Misses the excitation-dependent interactions (retrigger into a ringing resonator, accent-dependent clipping) unless you drive it as a filter (§2.4) rather than as an envelope × oscillator.
  - **Avoid "envelope × sine oscillator" BDs.** They reset phase on every hit and produce the machine-gun effect.

---

## 2. The bridged-T resonator

### 2.1 Transfer function [Werner DAFx-14 eq. 5; algebra re-derived]

Topology:
- op-amp, non-inverting input driven by the shaped pulse V+
- feedback network from output Vo to the inverting input:
  - bridge resistor **Rb** (BD: R167) directly from Vo to V−
  - capacitive arms **C1** (V− → node M) and **C2** (M → Vo); BD: C41, C42
  - resistor **R** from M to ground; BD: Reff

With ideal op-amp assumptions:

```
Vo/V+ = 1 + H_bp(s)

H_bp(s) =            Rb·C1·s
          ------------------------------------
          R·Rb·C1·C2·s² + R·(C1+C2)·s + 1
```

This is identical to Werner's β/α coefficients: β2 = α2 = Reff·R167·C41·C42, β1 = Reff·C41 + R167·C41 + Reff·C42, α1 = Reff·(C41+C42), β0 = α0 = 1.

Normalised to the standard band-pass form `K·(ω0/Q)·s / (s² + (ω0/Q)·s + ω0²)`:

| quantity | general | C1 = C2 = C |
|---|---|---|
| ω0 | 1/√(R·Rb·C1·C2) | 1/(C·√(R·Rb)) |
| bandwidth ω0/Q (rad/s) | (C1+C2)/(Rb·C1·C2) — **no R** | 2/(Rb·C) |
| Q | √(Rb·C1·C2/R)/(C1+C2) | ½·√(Rb/R) |
| peak gain K | Rb·C1/(R·(C1+C2)) | Rb/(2R) = 2Q² |
| envelope τ = 2Q/ω0 | 2·Rb·C1·C2/(C1+C2) | Rb·C |
| ringing freq ωd | ω0·√(1 − 1/(4Q²)) | |

Two consequences for implementation:
- **Tuning by R (BD Q43, tom/conga tuning pots) moves f0 ∝ 1/√R while the decay time stays fixed.** Q rises as f0 rises. So a "tune" knob should change only f0 and keep σ constant. Don't keep Q constant.
- The "+1" direct path means the output carries a scaled copy of the excitation pulse, which is part of the click.

Werner's service-notes formula for f0 matches: `fc = 1/(2π√(Reff·R167·C41·C42))` (DAFx-14 fn. 16).

### 2.2 Numbers for the BD [DERIVED from thesis Table 2.3 / 4.2 values]

- **Normal state:** Reff = R161 ‖ (R165+R166) ‖ R170 = 1 M ‖ 53.8 k ‖ 470 k = **46.05 kΩ**.
  - f0 = **49.4 Hz**. Werner's plot shows ≈ 49.5 Hz; the Roland chart says "typical" 56 Hz.
  - Bare Q = 2.33. Bandwidth 133.3 rad/s. Bare τ = R167·C = **15 ms**.
- **Attack state** (Q43 on, collector grounded, Reff → R166 ‖ R161 ‖ R170): **6.66 kΩ**, f ≈ **130 Hz**, Q ≈ 6.1. It lasts until the envelope generator falls, ≈ 6 ms (DAFx-14 §8.1).
  - Wolfram SystemModeler replication: pulse 5.3 ms simulated vs 6.2 ms on hardware. https://blog.wolfram.com/2021/12/29/digital-vintage-sound-modeling-analog-drums-with-the-wolfram-language-and-system-modeler/
- **[MEASURED] Fischer BD**, per-cycle zero-crossing periods, tone 5 / decay 5:
  - first cycle ≈ 6.0 ms (≈ 166 Hz equivalent)
  - then 63.1 → 57.1 (22 ms) → 54.3 (39 ms) → 52.5 (58 ms) → 51.2 (77 ms) → 50.4 (96 ms) → 49.96 (116 ms) → 49.65 Hz (≥ 156 ms) and stays there
  - **Settled pitch is 49.6 Hz in all 25 BD samples, independent of the tone and decay knobs.**

### 2.3 Decay = regenerative feedback (the feedback buffer)

- The feedback buffer (R164, R169, VR6, C43; Werner eq. 6) is a shelf. Its DC gain is R169/R164, and above about 0.01–0.1 Hz its gain is `G_fb = (R169 ‖ VR6·k)/R164`, so it is flat across the audio band.
  - k ∈ [0, 1] is the decay knob position, before the taper is applied.
- Its output re-enters the bridged-T through R170. That path (Werner Hbt2, eq. 7) has the **same poles** as Hbt1. Its peak gain is `G_bt = R167·C41/(R170·(C41+C42)) = 1.064`, which is independent of Reff.
  - Note: the α2 coefficient in the extracted PDF text is missing an R170 factor. Without it the poles would be at ≈ 34 kHz, which is plainly wrong.
- The loop is net-positive (two inverting stages), and the buffer is a constant gain near f0. So the closed loop is a band-pass whose bandwidth is scaled by `(1−L)`, where `L = G_fb·G_bt`:

```
σ_closed = (1 − L) · (C41+C42) / (2·R167·C41·C42)       [1/s]
T60      = ln(1000)/σ_closed = 6.908/σ_closed
f0 unchanged (ωd → ω0 as damping falls, hence 49.4 Hz not 48.3 Hz)
```

**Validation [DERIVED vs MEASURED]**, with R164 = 47 k (the 4.7 k in thesis Table 4.2 would give L ≈ 9.7, i.e. self-oscillation, so it must be a typo):

| decay pot | model L | model T60 | measured T60 (Fischer, median of 5 tone settings) |
|---|---|---|---|
| 0 | 0.000 | 0.10 s | 0.12 s (0.11–0.12) |
| 2.5/10 | — | — | 0.34 s → implied L = 0.69 (VR6 ≈ 87 k) |
| 5/10 | — | — | 1.22 s (0.96–1.62!) → L = 0.915 (VR6 ≈ 289 k) |
| 7.5/10 | — | — | 2.01 s → L = 0.948 (VR6 ≈ 386 k) |
| 10 | 0.972 | 3.75 s | 3.69 s (3.68–3.70) |

- Both endpoints match within measurement error.
- Mid-range points need the pot taper. The thesis says the BD pots are (A)/(C) taper, but which pot is which is inconsistent between DAFx-14 and the thesis: **UNVERIFIED**.
- Recommendation: map the knob to VR6·taper(k) and fit taper(k) to the five measured points (≈ 0.01, 0.17, 0.58, 0.77, 0.99).
- The 0.96–1.62 s spread at "5" shows that hand-set knob positions on the reference unit are noisy.

**Implementation trick:** in an SVF with damping coefficient k_d = 1/Q, implement the feedback loop simply as `k_d,eff = (1 − L)/Q_bare`. This is exact for a constant-gain loop, has no delay-free-loop problem, and needs no fictitious unit delay.
- To also model the buffer's sub-audio shelf and the "+1" pulse feedthrough entering the loop, keep it as an explicit loop. Solve it with zero-delay feedback (one linear equation per sample), or accept Werner's unit delay. f0/fs is tiny, so the error is negligible (DAFx-14 §10).

### 2.4 Excitation: the pulse shaper and the retrigger pulse

- **Trigger:** a rectangular pulse 1 ms wide, amplitude V_ct = 4–14 V (global accent, VR3).
  - For the cymbal the range is narrowed to 7–14 V.
  - Accent is a **voltage into nonlinear stages**, not a post-gain. It changes timbre.
- **Pulse shaper** (R162 = 4.7 k, R163 = 100 k, C40 = 15 nF):
  - `H(s) = (R162·R163·C40·s + R162) / (R162·R163·C40·s + R162 + R163)` [Werner eq. 3]
  - Corners [DERIVED]: zero at 1/(R163·C40) → 106 Hz; pole at (R162+R163)/(R162·R163·C40) → 2.36 kHz.
  - HF gain 1. DC gain R162/(R162+R163) = 0.045.
  - Follow it with a **negative-side diode limiter** that lets the output fall to about −0.71 V at most and passes positive values unchanged (Werner eq. 4).
  - A smooth form with unit slope at 0 [my parametrisation]: `y = x` for x ≥ 0; `y = −0.71·(1 − e^{x/0.71})` for x < 0.
- **Rising edge** amplitude = V_ct. **Falling edge** ≈ V_ct·R162/(R162+R163) + 0.71 V, which is almost independent of accent. [Werner §4]
- **Cheap equivalent:** the band-pass responds to the derivative of a step, so you can excite an impulse-driven resonator (phasor/modal) with two impulses: +ΔV_rise at t = 0 and −ΔV_fall at t = 1 ms. Pass them through the pulse shaper's HF character, or just use the measured ratio.
- **Retrigger pulse:** C39 (33 nF) → R161 (1 M) into the bridged-T centre, clipped by D52.
  - It fires when the envelope generator collapses (≈ 6 ms) and restarts energy at the normal f0, so the amplitude doesn't jump after the attack shift.
  - Its decay τ ≈ C39·R161 = **33 ms** [DERIVED].
  - Model it as a high-pass of the envelope-generator output followed by a diode clipper (Werner §8.1). Practical form: inject a small negative step that decays with τ = 33 ms at the moment the attack state ends.
- **Attack-state switch:** at t = 0, switch Reff from 46 k to 6.66 k; switch back at ≈ 6 ms.
  - Smooth the transition over about 0.1–0.2 ms. The envelope generator output isn't an ideal step: it rises fast, then falls over a few ms (Werner Fig. and thesis Fig. 4.10).
  - Its shape is nearly independent of accent (thesis Ch. 4).

### 2.5 The pitch sigh (Q43 leakage)

- Mechanism (DAFx-14 §8.2): when Vcomm (the node joining R166/C41/C42) swings below about −0.56 V, Q43 conducts. Its collector current iC raises the conductance of the R165/R166 branch, which lowers Reff and raises f0.
- Werner's fitted model:
  - **iC** = softplus-shaped function of Vcomm: `iC ∝ (m/α)·ln(1 + e^{−α(Vcomm − V0)})`, with α = 14.315, V0 = 0.556 V (sign convention garbled in the PDF: **UNVERIFIED**), m = 1.4765×10⁻⁵ A/V.
  - Effective branch (eq. 9), rewritten [DERIVED] to avoid the 0/0 at Vcomm = 0:
    `i_branch = (Vcomm + R165·iC)/(R165 + R166)`
    so the shunt conductance is `G = i_branch/Vcomm`. When iC = 0 this reduces to 1/(R165+R166).
- **Practical model:**
  - `f0(t) = f_base · (1 + c · softplus(a·(A(t) − A_knee)))`.
  - A(t) is the resonator's own amplitude: the SVF low-pass state is proportional to the Vcomm swing, or use a peak follower on it.
  - Calibrate c, a and A_knee against the [MEASURED] trajectory: Δf ≈ +7.5 Hz at A = 0.84·peak, +4.6 at 0.73, +2.8 at 0.65, +1.6 at 0.58, +0.8 at 0.53, +0.3 at 0.48, ≈ 0 below about 0.44 (tone 5 / decay 5).
  - Because it is amplitude-driven, long decays sigh for longer (decay 10: still +0.65 Hz at 230 ms), and higher accents sigh more. A fixed-time exponential pitch envelope gets both of these wrong.
- The arXiv study of 808 BD samples agrees: median f0 49.48 Hz, an initial sweep of about one semitone, settling within about 0.4 s. https://arxiv.org/html/2502.07524v1

### 2.6 Discretisation options (time-varying)

| method | pros | cons | use for |
|---|---|---|---|
| **TPT / ZDF SVF** (trapezoidal; Zavalishin *Art of VA Filter Design*; Simper/Cytomic `SvfLinearTrapOptimised2.pdf`) | exact pre-warp at fc (g = tan(π·fc/fs)); LP/BP/HP from one structure, so any 2nd-order numerator by mixing; well-behaved under per-sample modulation; ≈ 6–8 mul/sample | needs tan(): use a polynomial for f < fs/8, e.g. g ≈ x(1 + x²/3 + 2x⁴/15) with x = π·f/fs | **default for every bridged-T, BPF and HPF** |
| Bilinear biquad, TDF-II (Werner) | simple; fine for static filters | coefficient jumps produce transients; poles near z = 1 at 50 Hz @ 192 kHz need double precision | static output filters |
| **Mathews–Smith phasor filter** (complex one-pole `z[n] = r·e^{jθ}·z[n−1] + x[n]`) | frequency and damping change without changing amplitude; impulse-invariant (no warping) | numerator fixed (pure BP/impulse); drive it with impulses or derivatives | BD/tom resonator when sweeping f per sample; an alternative to the SVF. https://ccrma.stanford.edu/~jos/smac03maxjos/ |
| WDF with R-type adaptor + Newton (Werner thesis) | component-level fidelity, including Q43 and D52 | 8× oversampling for the full chain; iteration cost; complexity | optional "deluxe" engine |

**SVF realisation of an arbitrary 2nd-order H(s)** [standard result]:
- Make the denominator monic: `s² + (ω0/Q)s + ω0²`.
- With SVF outputs normalised so that HP = s²/D, BP = ω0·s/D, LP = ω0²/D, the numerator `b2·s² + b1·s + b0` is obtained as `y = b2·HP + (b1/ω0)·BP + (b0/ω0²)·LP`.
- For higher orders (cymbal BPFs are 3rd order, HPF #3 is 3rd order, the tone stage is 5th order): factor the denominator once per parameter change (offline or on the message thread), then cascade SVF and 1-pole TPT sections. Never do root-finding on the audio thread per sample.

**Precision:** keep resonator state in `double`, or at least use TPT (state is in integrator form, not direct-form). At 96–192 kHz, 50 Hz poles sit about 1e-3 from the unit circle.

**Sample-rate independence:** define everything in Hz, seconds and ohms/farads. Recompute coefficients in prepare(). The bridged-T bandwidth/decay must not depend on fs. Check this by rendering at 44.1, 48, 96 and 192 kHz and comparing T60 and f0 (§8).

### 2.7 Toms/congas/rim/claves (same machinery)

- [MEASURED] Fischer toms: the first cycle is ≈ 1.45–1.55× the settled pitch, then pitch settles with a small downward drift. This is the same attack-shift signature as the BD.
  - LT: 81 Hz (tune 0) → 89.5 (5) → 101–104 Hz (10), drifting 104 → 99.8 over about 120 ms at tune 10.
  - MT (5): 136 Hz.
  - HT (5): 189 Hz.
  - The circuit mechanism for the toms is **UNVERIFIED** here (see the circuit research file). Implement the same "attack Reff + amplitude-dependent sigh" hooks with per-voice constants.
- [MEASURED] Claves: a steady 2.42 kHz ring. Rim shot: about 1.7–1.8 kHz for the first cycles, then about 480–520 Hz components.

---

## 3. Metallic voices (CB, CY, OH, CH)

### 3.1 The oscillator bank

- Six HD14584 Schmitt-trigger RC oscillators, **shared** by CB, CY, OH and CH. Nominal frequencies (Werner ICMC §4):
  - #1 205.3 Hz, #2 369.6 Hz, #3 304.4 Hz, #4 522.7 Hz
  - #5 359.4–1149.9 Hz, trimmed to **800 Hz** (TM2)
  - #6 254.3–627.2 Hz, trimmed to **540 Hz** (TM1)
  - #5 and #6 also form the cowbell.
  - Early units (before serial #000300) used different RC values.
- Period: `T = RC·[ln((VOH−VT−)/(VOH−VT+)) + ln((VOL−VT+)/(VOL−VT−))]`. The duty cycle doesn't depend on RC: **D = 47.98 %**, amplitude 5 V at a 5 V supply.
  - Supply "starve" changes the thresholds, hence pitch, duty and amplitude: a good bend.
- Mixing (R53 network) attenuates each oscillator into the BPFs. Replacing R53 with a pot is a dramatic timbre bend.
- **Implementation:**
  - One shared bank running at the metallic oversampled rate, phases free-running from plugin start.
  - Each hit therefore lands at a random phase constellation. This is authentic per-hit variation; do **not** reset phases on trigger.
  - While all four metallic voices sleep, keep the bank "virtually" running by advancing phases analytically (`phase += inc·n mod 1`) on wake.
  - Per-oscillator tolerance: multiply RC by (1 + ε_i). Expose CB tuning (#5/#6) as the classic mod.

### 3.2 Anti-aliasing

The six squares have edges every ≥ 0.6 ms and harmonics falling at only 6 dB/oct. The 808 then **boosts** exactly the aliasing region: BPFs at 3.44 and 7.1 kHz, HPFs up to about 10 kHz, and a differentiating output stage. Werner skipped band-limiting (ICMC fn. 8) and relied on 4× oversampling.

[DERIVED by simulation] Sum of the six oscillators at 48 kHz output. Alias energy / harmonic energy per band:

| scheme | 2–5 kHz | 5–9 kHz | 9–16 kHz | relative cost |
|---|---|---|---|---|
| naive 1× | −21 dB | −14 dB | −8 dB | 1 |
| naive 2× (decimated) | −27 | −20 | −14 | 2 + decimator |
| naive 4× (Werner) | −33 | −26 | −20 | 4 + decimator |
| **polyBLEP 1×** | −67 | −47 | −28 | ≈ 1.2 |
| **polyBLEP 2×** | −84 | −67 | −50 | ≈ 2.5 incl. half-band |

- polyBLEP (Välimäki & Huovilainen 2007; 2-sample polynomial residual added at each edge with its fractional position) costs a few multiplies per edge. The bank produces at most about 10 k edges/s, so the cost is negligible.
- minBLEP (Brandt, ICMC 2001, https://www.cs.cmu.edu/~eli/papers/icmc01-hardsync.pdf) is better still but needs a table and per-voice ring buffers. Unnecessary here.
- **The nonlinear swing VCA after the BPFs creates new aliasing.** Run the whole metallic chain (bank → BPF → VCA → HPF) at **2× when the host rate is below 88.2 kHz**, and at 1× at or above 88.2 kHz. Decimate with a polyphase half-band FIR or IIR.
  - Alternative at 1×: antiderivative anti-aliasing (ADAA) on the VCA nonlinearity (Parker, Zavalishin, Le Bivic, DAFx-16, https://dafx.de/paper-archive/details/vem_XXF5qBbfiWOH2RVVAA).
- **Recommendation:** polyBLEP squares + 2× metallic chain (+ optionally ADAA in the VCA).

### 3.3 Filters

- Cymbal BPF #1 is 3rd order: `(β2 s² + β1 s)/(α3 s³ + α2 s² + α1 s + 1)` with
  - β2 = −R56·R57·(C13+C14)·C10, β1 = −R57·C10
  - α3 = R56·R57·R52·C13·C14·C10
  - α2 = R56·R57·C13·C14 + R56·R52·(C13+C14)·C10
  - α1 = R56·C13 + R56·C14 + R52·C10
  - Centres: ≈ 3440 Hz (BPF #1) and ≈ 7100 Hz (BPF #2).
  - HPF #1 is a unity-gain 2nd-order Sallen-Key (R124, R127, C48, C59). HPF #3 is 3rd order with a resonant peak near 10.5 kHz.
  - Component values are in the schematic; see the circuit research file. [Werner ICMC §4, §9]
- These features sit at 0.07–0.24·fs at 44.1 kHz. The bilinear transform **must** be pre-warped (TPT does this at fc), or run at 2×/4× (Werner rendered at 4×).
- [MEASURED] Fischer spectra:
  - CH/OH strongest peaks 6.3–7.6 kHz; centroid ≈ 9.7–12 kHz
  - CY (5/5) peaks 2.5–3.4 kHz and 6.6 kHz; centroid ≈ 7.4 kHz
  - CB peaks 559/824 Hz + harmonics; centroid ≈ 5.6 kHz

### 3.4 Envelope generators (diode-switched RC)

- Werner ICMC §7: a diode as an ideal switch (V_on fitted to 0.5899 V).
  - **Attack:** output follows (input − V_on) instantly.
  - **Release:** exponential, `dV/dt = −V/(R·C)`, with R = (decay pot ‖ fixed resistor).
- Follow-on stages:
  - an emitter follower (−V_BE, fitted 0.6195 V)
  - a passive high-shelf `R104/(s·R104·R105·C45 + R104 + R105)`
  - a divider
- **Attack smoother** before the EGs: a one-pole with τ = **0.102 ms** acting on (V_trig − 0.7258 V).
- **Implementation:**
  - `y += (target − y)·(1 − e^{−1/(τ·fs)})`. Compute the coefficient exactly, never with a 1/(τ·fs) Euler approximation at short τ. Werner used forward Euler at 4×.
  - Release τ from the pot taper.
  - The CH/OH/CY decay knobs change τ only.
  - CH chokes OH (they share the circuit; see the circuit file).
- [MEASURED] T60s: CH ≈ 0.10 s; OH 0.16 s (decay 0) → 1.1 s (5) → 1.4 s (10); CY (5/5) ≈ 3.6 s; CB ≈ 0.7 s.

### 3.5 "Swing-type" VCA (nonlinear)

- Physics (Werner ICMC §8): a common-emitter stage whose V_CC is the envelope voltage.
  - A diode gates the output fully off at low envelope.
  - The output saturates between V_env and a "lower edge" given by a sum of two stretched exponentials of V_env (coefficients α = −1.396, 0.825, 0.566; β = 1.063, 0.837; γ = 1.000, 1.447; Table 1).
  - It is combined through a smooth clip of sharpness α = 3.5 (eq. 15).
- **Practical behavioural form** (as in Plaits):
  - asymmetric gain: high on positive half-waves, low (≈ 1/40) on negative
  - then a soft saturator x/(1+|x|)
  - plus a small offset
  - times the envelope
- What matters audibly:
  - **Gain depends on signal level.** Louder oscillator mixes (R53 bend, accent) clip "sporadically" and change the texture.
  - **Even-harmonic asymmetry.**
- Oversample it (§3.2) or use ADAA.

---

## 4. Noise and the clap

### 4.1 Noise generation

- Use a per-voice, allocation-free PRNG (xorshift / xoshiro128+ / PCG32) → uniform float in [−1, 1). Never use `rand()`: it has global state, can be locked, and is slow.
- The 808's noise is analog junction noise. Its exact spectrum is **UNVERIFIED** here. WDR-8 (§9) found that **violet** (differentiated white) noise fitted their SPICE-calibrated snare best (README). Make the "noise colour" a per-voice constant and calibrate it against references.
- Noise voices (SD snappy, CP, MA) need no oversampling: white noise is already full-band, so there is nothing to alias except through nonlinear VCAs.
- **Seed policy:** seed the PRNG deterministically from (instance seed, voice id). Offline bounces are then repeatable, which users expect, while successive hits still differ.

### 4.2 Clap: multi-pulse envelope

- Architecture (SOS: https://www.soundonsound.com/node/4918183; KVR thread: https://www.kvraudio.com/forum/viewtopic.php?p=4765735):
  - white noise → BPF ≈ 1 kHz → VCA
  - the VCA is driven by (a) a **sawtooth generator that runs for about 3 cycles** (instant rise, fast decay, period ≈ 10 ms) and stops
  - and by (b) a separate longer decay envelope, the "reverb" tail
- KVR also claims a linear AD with about 40 ms decay clamps the saw, and a tail of about 1.5 s. Both are forum claims: **UNVERIFIED**.
- [MEASURED] Fischer CP:
  - envelope peaks at **2.2, 12.4, 25.4 ms** (≈ 10–13 ms spacing), then the main burst peaking at **36.8 ms**
  - then decay: −12 dB @ 50 ms, −20 dB @ 80 ms, −31 dB @ 100 ms, a long low tail ≈ −30…−32 dB from 100 to 200 ms, −42 dB @ 300 ms
  - spectral peak **1005 Hz**, centroid 3.2 kHz
- **Implementation:**
  - `env(t) = Σ_{i=0..2} A_i·saw_decay(t − i·T_p) + A_tail·e^{−(t − t_tail)/τ_tail}·u(t − t_tail)`
  - T_p ≈ 11–12 ms. Each burst decays quickly (≈ T_p). Tail onset t_tail ≈ 25–35 ms.
  - Fit τ_tail and the 2-slope tail from the CP sample.
  - Use per-burst amplitude ratios from the measurements: the first burst is about 4 dB below the later ones. Shape the envelope as RC charge/discharge (exponential), not linear, unless the circuit analysis says otherwise.
  - Keep the burst timing **slightly irregular**: the period comes from an RC oscillator, so allow tolerance on it.

---

## 5. Realtime safety and performance

### 5.1 Hard rules on the audio thread

- No allocation, locks, syscalls, logging, file I/O, `std::function` that may allocate, or last `shared_ptr` release. Ross Bencina, "Real-time audio programming 101": http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing
- Preallocate everything in `prepareToPlay`: voices, oversampling buffers, event queues sized for the worst case (e.g. 16 voices × max steps per block × flam/ratchet multiplicity).
- GUI → audio: pattern edits through a lock-free SPSC FIFO, or a double-buffered pattern with an atomic pointer swap. **The audio thread never frees the old buffer**; reclaim it on the message thread.
- Audio → GUI: playing step, meters and trigger LEDs as atomics or through a FIFO.
- Parameters: read via atomics (e.g. JUCE APVTS raw parameter pointers) once per block or sub-block.
- **Verify, don't trust:** build a test config with Clang's **RealtimeSanitizer** (`-fsanitize=realtime`, `[[clang::nonblocking]]` on process), https://clang.llvm.org/docs/RealtimeSanitizer.html. Also run pluginval and clap-validator in CI.

### 5.2 Denormals

- Exponentially decaying resonators and envelopes sink into subnormals. On x86 this causes 10–100× slowdowns and CPU spikes exactly when sounds fade out.
- Set FTZ/DAZ (MXCSR) at the top of every process call (JUCE `ScopedNoDenormals`). On aarch64 set FPCR.FZ.
- Belt and braces: when a voice goes to sleep, hard-zero its states.

### 5.3 Per-voice sleep

- The 808 has 11 voice circuits (with switch-selected instrument pairs: LT/LC, MT/MC, HT/HC, RS/CL, CP/MA). Each circuit is **monophonic**, and a retrigger re-excites the same state. Model each as one voice object, not a polyphonic pool.
- **Sleep test:** envelope below −120 dBFS **and** resonator state energy (e.g. LP² + BP²) below a threshold for at least one block **and** no pending events. Then zero the state and skip processing.
- The BD at max decay has T60 ≈ 3.7 s, so sleeping after −120 dB takes about 7 s. That's fine.
- Wake on trigger. The metallic bank advances its phases analytically while asleep (§3.1).

### 5.4 Sample-accurate events

- Collect all events for the block into a preallocated, offset-sorted list: host MIDI notes, internal sequencer triggers, accent, and choke/mute changes.
- Render voice-by-voice in sub-blocks between event offsets, or check per sample against the next event index. Both are sample-accurate.
- Triggers latch their accent and "velocity" at the trigger sample.
- **Ordering at the same sample:** apply parameter changes first, then chokes (CH kills OH), then triggers.

### 5.5 Parameter smoothing

- Continuous knobs (level, tone, decay, tune): one-pole or linear ramp, 5–20 ms.
- Coefficient-heavy parameters (tan()): recompute every 8–32 samples and interpolate g/k linearly in between. TPT tolerates this.
- **Do not smooth** trigger-latched values (accent level, step velocity) or discrete switches (instrument select: switch only while the voice is silent, or crossfade over about 5 ms).
- Decay changes mid-note are audible on hardware and should act immediately, smoothed only against zipper noise.

### 5.6 SIMD and oversampling budget

- **Lanes** that pair naturally:
  - the 6 metallic oscillators (pad to 8: AVX float8, or 2× SSE float4)
  - the 3 cymbal bands / VCAs / HPFs
  - LT/MT/HT/LC/MC/HC resonators, which share structure, so 4- or 8-lane SVF across voices with structure-of-arrays state
- Libraries: xsimd (BSD-3), JUCE `SIMDRegister`, or `std::experimental::simd`. Avoid per-lane branches: use masks for "attack state" switching.
- **Oversampling per voice:**
  - **BD, toms/congas, RS/CL, SD, CP, MA: 1×.** Their resonant content is far below Nyquist, and the nonlinearities (pulse diode, sigh) act on low-frequency signals.
  - **CB, CY, OH, CH: 2× below 88.2 kHz**, using polyBLEP and a polyphase half-band decimator.
- Cost estimate [DERIVED, order of magnitude]: ≈ 50–150 flops per voice-sample at 1×. A full kit at 48 kHz, with the metallic chain at 2×, is a few MFLOP/s, i.e. well under 1 % of one modern core. Performance risk lies in denormals and allocation, not arithmetic.
- **Optional quality modes:** "Eco" (1×, polyBLEP + ADAA) and "HQ" (metallic chain at 4×, everything else at 2×), for offline rendering.

---

## 6. Sample-accurate sequencer locked to host transport

### 6.1 Inputs per block

- **JUCE `AudioPlayHead::PositionInfo`** (most fields are `Optional`; hosts may omit them): `getPpqPosition`, `getBpm`, `getIsPlaying`, `getIsLooping`, `getLoopPoints`, `getPpqPositionOfLastBarStart`, `getTimeSignature`, `getTimeInSamples`, `getBarCount`, `getHostTimeNs`. https://docs.juce.com/master/classAudioPlayHead_1_1PositionInfo.html
- **CLAP `clap_event_transport`**: `song_pos_beats` (fixed point), `tempo`, `tempo_inc` (per-sample tempo increment until the next transport event), loop start/end, `bar_start`, `bar_number`, and flags (IS_PLAYING, IS_LOOP_ACTIVE, HAS_TEMPO, …).
  - **Transport events may arrive mid-block**, which gives sample-accurate tempo changes. https://github.com/free-audio/clap/blob/main/include/clap/events.h
- VST3 equivalent: `ProcessContext` (projectTimeMusic, tempo, barPositionMusic, cycleStart/EndMusic, state flags).

### 6.2 Algorithm (stateless with respect to the host timeline)

```
spp      = fs * 60 / bpm                         // samples per quarter note
ppq0     = host ppq at sample 0 of this block
ppq1     = ppq0 + N / spp                        // (integrate tempo_inc if CLAP gives it)
stepLen  = quarter-notes per step  (1/4 for 16ths; 1/6 for 16th-triplets, 1/8 for 32nds, ...)
for each step index n with  t_n in [ppq0, ppq1):          // half-open interval
    t_n    = n*stepLen + swingOffset(n)                    // swing on odd 16ths only
    offset = floor((t_n - ppq0) * spp + 0.5)              // clamp to [0, N-1]
    emit triggers for pattern[(n - patternStartStep) mod patternLength] at offset
```

- **Recompute from the host PPQ every block. Never accumulate your own sample counter while synced.** Accumulation drifts, and it breaks on tempo changes, loops, jumps and pre-roll.
- **Enumerate candidate steps** as `n` from `ceil((ppq0 − maxSwing)/stepLen) − 1` to `floor(ppq1/stepLen) + 1`, testing each swung time against [ppq0, ppq1). Use `floor`, not truncation: pre-roll PPQ can be negative.
- **Boundary epsilon:** compare with a tolerance of about 1e-9 quarter notes, assigning a step exactly at ppq1 to the next block. This prevents double triggers and missed triggers at block edges, which is the most common bug.
- **Swing/shuffle:** delay odd 16ths by `(s − 0.5)·2·stepLen`, where s ∈ [0.50, 0.75] is "swing %" in MPC/909 notation.
  - The original 808 had no shuffle; the 909 introduced it. That is my understanding, not checked against a primary source in this session: **UNVERIFIED** here; see the hardware research file. Every popular plugin offers it (Roland RC-808 has it per instrument).
  - Swing must also apply to sub-steps, ratchets and flams relative to the swung step.
- **Flam:** a second trigger offset by a few ms (or a fraction of a step) with reduced accent.
- **Sub-steps / ratchets:** subdivide the step into k equal triggers.
- **Tempo change mid-block:** JUCE provides one BPM per block. Treat it as constant; the next block's host PPQ corrects any error. CLAP's `tempo_inc` and mid-block transport events let you integrate exactly, so split the block at each transport event.
- **Loops:** if looping and ppq1 > loopEnd, treat the block as two virtual segments, [ppq0, loopEnd) and [loopStart, loopStart + (ppq1 − loopEnd)), with the sample offsets continuing.
  - Many hosts split blocks at loop boundaries anyway, but don't rely on it.
- **Jump detection:** if `|ppq0 − expectedPpq| > 1 sample` (where expectedPpq = previous ppq1), or ppq goes backwards, treat it as a relocation. Don't fire the "missed" steps. Re-derive the pattern position from ppq (bar-aligned via `ppqPositionOfLastBarStart` when available).
- **Transport start:** if playing starts exactly on a step (ppq0 == t_n), fire it at offset 0. Include the lower bound of the interval.
- **Host stopped (free-run mode):** run an internal clock (own BPM, sample-accumulating phase is fine here). When the host starts, switch to synced mode and re-derive position. Expose "Sync to host / Internal / MIDI-note pattern trigger" modes (Nepheton exposes MIDI note/CC/GUI pattern triggering).
- **Accent:** on the 808, accent is a per-step track that applies to all instruments on that step. It raises the common-trigger voltage (§2.4), which plugs into the nonlinear models, not the output gain.
- **Last-step / pattern length:** per instrument (RC-808, polymeter) and per pattern. Compute each lane's index as `(globalStep − laneStart) mod laneLen`.
- **Offline render:** the same code path. Must be deterministic (seeded RNG, §4.1).
- **Pitfalls checklist:**
  - Optional fields absent (no PPQ: fall back to internal clock).
  - BPM ≤ 0 or NaN guard.
  - Block size 1 and huge blocks (8192).
  - Sample-rate change between prepare calls.
  - Steps landing in a zero-length block.
  - Pattern edit arriving mid-block: apply at the next block boundary.
  - Swing pushing the last 16th past the bar end: wrap correctly.

---

## 7. Analog variance

Physical sources, in order of importance for realism:

1. **Retrigger state carry-over** (resonators keep ringing) and **free-running metallic oscillator phases.** These alone make consecutive hits different; Werner Fig. 12 shows this. They are deterministic physics, not randomness. Always implement them.
2. **Noise realisation** per hit (SD, CP, MA).
3. **Component tolerance per unit:** the 808 voice circuits used **±20 % capacitors and ±5 % resistors**. Inside feedback loops these strongly change gain, f0, Q and decay, which explains why "unmodded bass drums can sound very different from one another" (Werner DAFx-14 §11).
   - Implement as a per-instance "unit" seed that draws multipliers per component, recomputes derived f0/Q/τ/L, and is stored with the preset.
   - Default spread: well inside spec. My measured BD (49.6 Hz) sits within 0.4 % of the nominal calculation, and the BD decay loop at L ≈ 0.97 is extremely sensitive: a 3 % change in L at max decay changes T60 by more than 2×. **Clamp L < 0.995** to prevent self-oscillation.
4. **Oscillator trims and RC drift** for the metallic bank: a static per-unit offset; a slow random walk is optional.
5. **Thermal/supply drift and noise floor:**
   - Drift magnitudes are **UNVERIFIED**. A small (≤ 0.1–0.3 %) slow random walk on pitch is a design choice, not sourced data.
   - [MEASURED] the Fischer recordings' tails sit at −80 to −85 dBFS against peaks of about −2.5 dBFS. So the hardware noise floor (including the 1994 recording chain) is **≤ about −78 dB re peak**. If you add a noise floor, keep it at or below that and make it switchable (default off).
6. **Commercial practice:** none of D16, Roland or AudioRealism publish their variance models. Roland's marketing mentions none explicitly. Plugin "random/analog" knobs are typically per-hit jitter of pitch, decay and level. Treat that as a separate, clearly labelled "humanise" feature, distinct from the faithful component-tolerance model.

---

## 8. Validation

### 8.1 Reference material (licences)

- **Michael Fischer / Technopolis TR-808 sample set (1994)**, packaged with a **CC0-1.0** LICENSE by TidalCycles: https://github.com/tidalcycles/sounds-tr808-fischer
  - Contents: 116 one-shots, 16-bit/44.1 kHz, recorded from individual outputs of serial #103852, level at max.
  - Knob grid at 0/2.5/5/7.5/10 positions: BD and SD (tone × decay/snappy, 25 each), CY (25), toms/congas tuning ×5, OH decay ×5, plus singles of RS, CL, CP, MA, CB, CH.
  - File naming: `BDttdd`, where "10" means position 10.0.
  - Caveats:
    - The original 1994 README says "ABSOLUTELY FREE" but gives no formal licence; the CC0 file was added by the repo maintainers. Treat the provenance as "free, CC0 as distributed", and keep the samples in a test-data folder, not shipped in the plugin.
    - Samples are trimmed (BD decay 0 is 0.25 s long; decay 10 is 3 s), so measure decay slopes, not tail lengths.
    - Knob positions were set by hand, which explains the decay-5 spread.
- **Werner's companion pages** (SPICE and model audio, listen-only; no licence stated): https://ccrma.stanford.edu/~kwerner/papers/dafx14.html and …/icmcsmc2014.html
- **SPICE:** an LTspice model of each voice from the service-notes schematic. This is the approach of Werner and WDR-8. It gives noise-free, component-exact references and lets you validate mods. The service notes PDF is mirrored by Eric Archer: http://www.ericarcher.net/wp-content/uploads/2014/07/808-svc-man.pdf (the link returned HTML in this session: **UNVERIFIED** availability).
- The sample sets on freesound.org have per-file licences (CC0 / CC-BY / CC-BY-NC); check each one. I did not vet them.

### 8.2 Metrics and procedure

1. **Render harness:** offline render of each voice at the same knob grid, 44.1 kHz, accent off, single hit. Align on the trigger by cross-correlating the first 5 ms (BD/toms), or by the onset (noise voices).
2. **Pitch trajectory (BD, toms, congas, RS/CL, CB):**
   - Use per-cycle zero-crossing periods with sub-sample interpolation on a low-passed signal. This is robust and is what produced the tables above.
   - The alternative is the Hilbert phase derivative, as in Werner Fig. 11.
   - Targets: settled f0 within ±1 % (BD 49.6 Hz ± 0.5); first-cycle period within ±10 %; sigh trajectory RMS error < 2 % over 0–300 ms.
3. **Decay:**
   - Fit a line to the dB peak envelope (one window per period) between −3 and −45 dB from the peak, and report T60 = −60/slope. Alternatively use Schroeder backward integration.
   - Target: ±10 % at each grid point after fitting the pot taper.
4. **Spectral:**
   - Short-time spectral centroid (first 50 ms) and Welch PSD over the whole hit.
   - Log-spectral distance on 1/3-octave bands from 30 Hz to 16 kHz: target < about 2 dB for the metallic and noise voices. Waveform nulling is meaningless there.
5. **Envelope shape:** onset structure for CP (burst times ±1 ms), attack shift duration for BD (≈ 6 ms), two-stage CB envelope.
6. **Behavioural tests no static sample can check:**
   - Retrigger at 50, 100 and 200 ms intervals (no machine-gun effect; the sum must differ from a reset-and-replay).
   - Accent sweep: timbre changes, not just level.
   - Sample-rate invariance: f0, T60 and centroid within 1 % across 44.1/48/96/192 kHz.
   - Denormal/CPU test with a long silent tail.
   - Aliasing: render a high CB tuning and look for non-harmonic lines below the 80 dB floor.
7. **CI:** store golden renders and fail on metric regressions. Tolerance-based, not bit-exact, because SIMD and compiler versions differ.

---

## 9. Open-source references (read for technique; do not copy code)

| repo | licence | what is worth studying |
|---|---|---|
| https://github.com/pichenettes/eurorack (Plaits `dsp/drums/analog_bass_drum.h`, `analog_snare_drum.h`, `hi_hat.h`; also Peaks drums) | MIT (code) | Compact behavioural 808 models: separate attack-FM pulse and retrigger pulse, amplitude-driven self-FM for the sigh, SVF resonators with frequency-scaled Q, swing-VCA as an asymmetric soft clip, 2-stage hat envelope. Uses naive squares (aliasing). |
| https://github.com/jatinchowdhury18/WaveDigitalFilters (`TR_808/`: PulseShaper, SnareResonator, HatResonator, OutputFilter) | GPL-3.0 | WDF implementations "of the TR-808 Bass Drum circuit as derived in Werner's dissertation", with JUCE plugin scaffolding. GPL: study only. |
| https://github.com/Chowdhury-DSP/chowdsp_wdf | BSD-3-Clause | Header-only C++ WDF library with R-type adaptors and diode models. Usable directly if we build an optional WDF BD engine. |
| https://github.com/Simon-L/WDR-8-rack | GPL-3.0 per README (no LICENSE detected by GitHub) | VCV Rack WDF 808 snare. LTspice-calibrated; notes on which subcircuits matter (EG Q47, shell resonators, diode-clipper VCA, violet noise). |
| https://github.com/vincentriemer/io-808 | MIT | Faithful 808 **UI and sequencer behaviour** reference (web). The sound engine is simple Web Audio. |
| https://github.com/quamplex/geonkick | GPL-3.0 | Linux-native percussion synth (LV2/VST3). Linux plugin build and realtime structure; not 808-specific. |
| https://github.com/WinkoErades/rs808lv2 | unclear (NOASSERTION) | Linux LV2 808-style kit "translated from SuperCollider" synthdefs. Behavioural; useful only as a parameter-set comparison. |
| https://github.com/free-audio/clap | MIT | Transport and event semantics (sample-accurate tempo). |

---

## 10. Recommended engine architecture for mw808 (synthesis of the above)

1. **Per-voice "block model" in the Werner style**, at 1× for tonal voices:
   - trigger pulse (1 ms, accent voltage 4–14 V)
   - pulse-shaper shelf + diode
   - TPT-SVF bridged-T, with:
     - Reff(t) from the attack state plus the amplitude-dependent sigh
     - damping scaled by (1−L(decay))
     - additive excitation and no state reset
   - retrigger pulse (τ 33 ms)
   - tone/level/HPF output stage from the Table 1.1 values
2. **A shared metallic oscillator bank** (polyBLEP, free-running, per-unit tolerance) feeding CB/CY/OH/CH chains at 2× (< 88.2 kHz):
   - pre-warped TPT BPF/HPF cascades factored from Werner's polynomials
   - diode-switched RC envelopes
   - a swing-VCA soft-clip, with ADAA optional
3. **Noise voices:** per-voice seeded PRNG with calibrated colour. Clap as a 3-burst saw envelope + tail.
4. **Realtime:** preallocated monophonic voice objects, FTZ/DAZ, sleep with analytic phase advance, sample-accurate sub-block event processing, atomics/FIFOs, RealtimeSanitizer in CI.
5. **Sequencer:** stateless PPQ derivation per block, half-open intervals, loop splitting, jump detection, swing/flam/sub-steps, per-lane length, internal free-run clock.
6. **Variance:** a per-unit component-tolerance seed (stored in the preset), separate from an optional "humanise" jitter.
7. **Validation:** an offline harness against the CC0 Fischer grid using the metrics in §8, plus LTspice references for mods.

---

## Sources

- Werner, Abel, Smith, *A Physically-Informed, Circuit-Bendable, Digital Model of the Roland TR-808 Bass Drum Circuit*, DAFx-14. https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf
- Werner, Abel, Smith, *The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model*, ICMC/SMC 2014 (CC-BY 3.0). https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf
- Werner, Abel, Smith, *More Cowbell…*, AES 137 (2014). https://aes2.org/publications/elibrary-page/?id=17530 ; CCRMA abstract page https://ccrma.stanford.edu/papers/more-cowbell-physically-informed-circuit-bendable-digital-model-of-tr-808-cowbell
- Werner, *Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters*, PhD thesis, Stanford 2016. https://purl.stanford.edu/jy057cz8322 ; PDF https://stacks.stanford.edu/file/druid:jy057cz8322/KurtJamesWernerDissertation-augmented.pdf
- Werner publication list: https://ccrma.stanford.edu/~kwerner
- Wolfram blog, *Digital Vintage Sound: Modeling Analog Drums…* (2021). https://blog.wolfram.com/2021/12/29/digital-vintage-sound-modeling-analog-drums-with-the-wolfram-language-and-system-modeler/
- *Harmonic and Transposition Constraints Arising from the Use of the Roland TR-808 Bass Drum* (arXiv 2502.07524). https://arxiv.org/html/2502.07524v1
- Sound On Sound Synth Secrets, cowbells & claves: https://www.soundonsound.com/techniques/synthesizing-cowbells-claves ; clap: https://www.soundonsound.com/node/4918183
- KVR forum thread on the 808 clap (secondary): https://www.kvraudio.com/forum/viewtopic.php?p=4765735
- Roland AIRA/ACB press release (2014): https://www.roland.com/us/company/press_releases/2014/1781/ ; Roland Cloud TR-808: https://www.roland.com/us/products/rc_tr-808/
- D16 Nepheton 2: https://d16.pl/nepheton2
- Mathews & Smith, *Methods for synthesizing very high Q parametrically well behaved two pole filters*, SMAC-03. https://ccrma.stanford.edu/~jos/smac03maxjos/
- A. Simper (Cytomic), *SvfLinearTrapOptimised2*. https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf ; V. Zavalishin, *The Art of VA Filter Design* (Native Instruments, free PDF).
- Parker, Zavalishin, Le Bivic, *Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution*, DAFx-16. https://dafx.de/paper-archive/details/vem_XXF5qBbfiWOH2RVVAA
- Brandt, *Hard Sync Without Aliasing*, ICMC 2001 (minBLEP). https://www.cs.cmu.edu/~eli/papers/icmc01-hardsync.pdf ; Välimäki & Huovilainen, *Antialiasing Oscillators in Subtractive Synthesis*, IEEE SPM 2007 (polyBLEP).
- JUCE `AudioPlayHead::PositionInfo`: https://docs.juce.com/master/classAudioPlayHead_1_1PositionInfo.html ; CLAP events: https://github.com/free-audio/clap/blob/main/include/clap/events.h
- Clang RealtimeSanitizer: https://clang.llvm.org/docs/RealtimeSanitizer.html ; R. Bencina, *Real-time audio programming 101*: http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing
- Fischer TR-808 samples (CC0 as distributed): https://github.com/tidalcycles/sounds-tr808-fischer

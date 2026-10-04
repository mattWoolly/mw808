// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Cymbal (CY). Circuit and values: research/02 §1-§2 (service manual p.13 and
// the published cymbal model, "[WCY]").
//
//   six squares --120k x6 / 1k--+--> BP#1 3.44 kHz --> VCA#1 --> HP#1 --> LP --+
//                               |                                              +--> tone --> out
//                               +--> BP#2 7.10 kHz -+-> VCA#2 --> HP#2 (x2) ---+     pot
//                                                   +-> VCA#3 --> HP#3 (x2.77) ----> (high band)
//
// DERIVED from the schematic (ideal op-amps):
//  * Mix: each oscillator reaches the summing node at 1/126 of its 5 Vpp.
//  * BP#1/#2: bridged-T in the feedback of an inverting stage with a series
//    R-C input. Exactly H(s) = -(Rb/Rin) HP1(s) [LP2(s) + BPunity(s)], with
//    BP#1: C10 3.3n, R52 33k, C13 = C14 6.8n, R56 560, R57 82k (3435 Hz, Q 6.05)
//    BP#2: C11 1n,   R55 22k, C15 = C16 3.3n, R58 560, R59 82k (7117 Hz, Q 6.05)
//  * Envelopes, as RC networks charged through diodes by the 1 ms trigger
//    (attack smoother tau 0.1024 ms, drops 0.7258 + 0.59 V):
//      EG#1  C41 1u, R93 470k + DECAY 2M, follower Q20 (0.62 V)
//      EG#1.2 R105 33k -> C45 2.2u, feeding VCA#1 through R104 22k: the low
//             band SWELLS in (tau ~29 ms) and is what DECAY controls
//      EG#1.3 divider R92 33k / R89 10k, to VCA#2 through R91 33k
//      EG#2  C40 1u -R88 33k- C39 0.47u, to VCA#2 through R90 33k (peaks ~20 ms)
//      EG#3  C38 1u with R87 22k + C37 2.2u across it, to VCA#3 through R94 39k
//    The VCAs load their envelopes: the current each collector draws is taken
//    from its capacitor, sample by sample.
//  * Swing VCAs: see dsp/SwingVca.h. Rc = 22k / 33k||(33k + 7.7k) / 39k;
//    2 x 1.5 nF at the collectors of VCA#2 and VCA#3, HP#1's 1.5 nF input
//    capacitor at VCA#1.
//  * HP#1 2.50 kHz Q 0.965, then R123 100k / C57 3.3n loaded by R121 10k and
//    the tone network (a ~3.9 kHz low-pass, 20 dB down: this is why the low
//    band needs its large VCA swing); HP#2 8.84 kHz Q 1.0, gain 2; HP#3
//    first-order 7.09 kHz, then a resonant second-order section, gain 2.77.
//  * TONE: VR4 20k(B) shunts the high band at one end and low+mid (through
//    2.2k) at the other; gains from the resistor network (R112/R119 22k,
//    R120/R121 10k, R129 15k). Clockwise is brighter (the reference
//    recordings confirm it).
//  * Output stage with LEVEL at maximum: feedback pole 470k||100k with 220 pF
//    = 8.8 kHz.
//
// FITTED:
//  * Q20 base loading (2.2 MOhm across C41): DECAY then spans 0.39..1.16 s of
//    envelope time constant instead of 0.47..2.47 s, and the low band's
//    -20 dB times come out at 380 / 855 / 1150 ms (service manual 350 / 800 /
//    1200; reference unit at tone 0: 370 / 864 / 1165).
//  * HP#3: 10.4 kHz, Q 3. The schematic gives a +23 dB peak at 10.4 kHz with
//    an ideal source and almost none with the real 39k source (research/02
//    §2.6, CONFLICTING); Q 3 matches the recordings' 10-12 kHz level.
//  * Mid and high band gains (0.45 and 0.6 of the schematic values), from the
//    band balance of the reference recordings across the tone grid.
//  * Trigger coupling power (0.75) and output gain: service-manual levels,
//    3.5 Vpp normal and 7 Vpp accented.
//  * The finite-rate level correction of the mid and high bands
//    (dsp::swingRateGain), against renders at a 768 kHz internal rate.
//
// What is NOT fitted: with TONE at 12 o'clock the short, bright bands set the
// peak, so the -20 dB point (measured from the peak, as the service manual
// does) arrives at 275 / 495 / 720 ms. That is what the reference unit does
// too (291 / 479 / 654 ms); the manual's 350 / 800 / 1200 ms are reproduced by
// the low band on its own (TONE at minimum).
//
// tune scales the oscillators and every audio-path time constant; envelope
// times do not move. Latency (latencySamples()): one internal sample of
// oscillator droop compensation, five of processing pipeline, and the
// decimator: 6.0 / 4.4 / 3.4 output samples at oversample 1 / 2 / 4.
//
// Level does not depend on the sample rate or the oversampling factor (within
// 0.15 dB from a 44.1 kHz to a 768 kHz internal rate): see the bandwidth-
// matched band-passes and dsp::swingRateGain in MetalCommon.h.

#pragma once

#include <cstdint>

#include "mw808/Voices.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Halfband.h"
#include "mw808/dsp/MetalCommon.h"
#include "mw808/dsp/MetalOscBank.h"
#include "mw808/dsp/MetalSimd.h"
#include "mw808/dsp/SwingVca.h"

namespace mw808::voices {

class Cymbal {
public:
    void prepare(const VoiceSetup& setup) noexcept {
        os_ = setup.oversample >= 4 ? 4 : (setup.oversample >= 2 ? 2 : 1);
        rate_ = setup.sampleRate * os_;
        dt_ = 1.0 / rate_;

        const std::uint32_t seed = setup.unitSeed;
        const float var = setup.variance;
        using dsp::componentScale;
        namespace mp = dsp::metalparts;
        // Capacitors +-20 %, resistors +-5 %. One scale per RC product that matters.
        bp1Scale_ = 1.0 / std::sqrt(static_cast<double>(componentScale(seed, mp::kBandPass + 0U, 0.20f, var)) *
                                    componentScale(seed, mp::kBandPass + 2U, 0.20f, var));
        bp2Scale_ = 1.0 / std::sqrt(static_cast<double>(componentScale(seed, mp::kBandPass + 1U, 0.20f, var)) *
                                    componentScale(seed, mp::kBandPass + 3U, 0.20f, var));
        c41_ = 1.0e-6 * componentScale(seed, mp::kCymbal + 0U, 0.20f, var);
        c45_ = 2.2e-6 * componentScale(seed, mp::kCymbal + 1U, 0.20f, var);
        c40_ = 1.0e-6 * componentScale(seed, mp::kCymbal + 2U, 0.20f, var);
        c39_ = 0.47e-6 * componentScale(seed, mp::kCymbal + 3U, 0.20f, var);
        c38_ = 1.0e-6 * componentScale(seed, mp::kCymbal + 4U, 0.20f, var);
        c37_ = 2.2e-6 * componentScale(seed, mp::kCymbal + 5U, 0.20f, var);
        hp1Scale_ = 1.0 / componentScale(seed, mp::kCymbal + 6U, 0.20f, var);
        hp2Scale_ = 1.0 / componentScale(seed, mp::kCymbal + 7U, 0.20f, var);
        hp3Scale_ = 1.0 / componentScale(seed, mp::kCymbal + 8U, 0.20f, var);

        bank_.prepare(rate_, seed, var);
        tables_.buildCymbalStyle();
        decimator_.prepare(os_);
        silence_.prepare(setup.sampleRate);
        // Envelopes run every ctrlN_ ticks (~12 kHz) and are ramped in between.
        ctrlN_ = std::max(1, static_cast<int>(rate_ / dsp::kMetalControlHz + 0.5));
        invCtrlN_ = 1.0 / ctrlN_;
        dtc_ = dt_ * ctrlN_;
        attack_ = 1.0 - std::exp(-dtc_ / kAttackTau);
        k45_ = dtc_ / c45_;
        k40_ = dtc_ / c40_;
        k39_ = dtc_ / c39_;
        k38_ = dtc_ / c38_;
        k37_ = dtc_ / c37_;
        pulseTicks_ = std::max(1, static_cast<int>(kPulseSeconds / dtc_ + 0.5));
        warmSamples_ = static_cast<int>(kWarmSeconds * rate_);

        haveParams_ = false;
        applyParams(params_);
        reset();
    }

    void reset() noexcept {
        bank_.reset();
        clearState();
        active_ = false;
    }

    void setParams(const VoiceParams& raw) noexcept {
        const VoiceParams p = dsp::saneParams(raw);
        if (haveParams_ && p.tune == params_.tune && p.decay == params_.decay && p.tone == params_.tone) return;
        applyParams(p);
    }

    void trigger(const Hit& hit) noexcept {
        if (!active_) wake();
        charge_ = dsp::metalChargeVolts(dsp::saneVolts(hit.volts), kTrigDrop, kTrigPower);
        pulseLeft_ = pulseTicks_;
        ctrlCount_ = 0;  // the envelopes see the trigger on the very next tick
        silence_.restart();
    }

    void render(float* out, int n) noexcept {
        if (!active_) {
            bank_.advance(static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(os_));
            for (int i = 0; i < n; ++i) out[i] = 0.0f;
            return;
        }
        for (int i = 0; i < n; ++i) {
            double hi[dsp::MetalDecimator::kMaxFactor];
            for (int k = 0; k < os_; ++k) hi[k] = tick();
            const double y = decimator_.process(hi);
            out[i] = static_cast<float>(y);
            if (silence_.tick(y) && pulseLeft_ == 0) {
                // Rung out: the rest of the block is exact silence.
                bank_.advance(static_cast<std::uint64_t>(n - 1 - i) * static_cast<std::uint64_t>(os_));
                for (int j = i + 1; j < n; ++j) out[j] = 0.0f;
                clearState();
                active_ = false;
                return;
            }
        }
    }

    bool isActive() const noexcept { return active_; }

    // Low-frequency group delay in output samples: the decimator, one internal
    // sample of oscillator droop compensation and the processing pipeline.
    double latencySamples() const noexcept {
        return decimator_.latencySamples() + static_cast<double>(1 + kPipelineTicks) / os_;
    }
    static constexpr int kPipelineTicks = 5;  // pipeline registers in tick()
    const dsp::MetalOscBank& oscillators() const noexcept { return bank_; }

private:
    // ---- schematic values -------------------------------------------------
    static constexpr double kOscVolts = 2.5 / 126.0;  // +-2.5 V square through 120k into 1k||24k
    static constexpr double kBp1Hz = 3435.0, kBp1Q = 6.05, kBp1Gain = 82.0 / 33.0, kBp1InHz = 1461.5;
    static constexpr double kBp2Hz = 7117.0, kBp2Q = 6.05, kBp2Gain = 82.0 / 22.0, kBp2InHz = 7234.3;
    static constexpr double kAttackTau = 1.0244e-4, kPulseSeconds = 1.0e-3;
    static constexpr double kR93 = 470.0e3, kVr2 = 2.0e6;
    static constexpr double kFollowerDrop = 0.62;
    static constexpr double kR105 = 33.0e3, kR104 = 22.0e3;
    static constexpr double kDivider13 = 10.0 / 43.0, kR91 = 33.0e3 + 7.67e3;  // + divider source
    static constexpr double kR88 = 33.0e3, kR90 = 33.0e3;
    static constexpr double kR87 = 22.0e3, kR94 = 39.0e3;
    static constexpr double kShunt = 3.0e-9;      // 2 x 1.5 nF at VCA#2 / VCA#3
    static constexpr double kShuntLow = 1.5e-9;   // VCA#1: only HP#1's input capacitor C48
    static constexpr double kHp1Hz = 2498.0, kHp1Q = 0.965;
    static constexpr double kR123 = 100.0e3, kR121 = 10.0e3, kC57 = 3.3e-9;
    static constexpr double kHp2Hz = 8842.0, kHp2Q = 1.0, kHp2Gain = 2.0;
    static constexpr double kHp3aHz = 7092.0, kHp3Gain = 2.77;
    static constexpr double kOutLpHz = 8770.0;
    // ---- fitted -----------------------------------------------------------
    static constexpr double kFollowerLoad = 2.2e6;  // Q20 base current, as a resistance across C41
    static constexpr double kHp3Q = 3.0;
    static constexpr double kHp3Hz = 10400.0;
    static constexpr double kTrigDrop = 0.7258 + 0.59;  // smoother Vbe + diode (schematic)
    static constexpr double kTrigPower = 0.75;
    static constexpr double kGainMid = 0.45, kGainHigh = 0.6;
    static constexpr double kOutGain = 4.52;
    // Finite-rate level correction (dsp::swingRateGain), fitted against a 768 kHz render.
    static constexpr double kRateMidA = 131.0, kRateMidB = 16.6;
    static constexpr double kRateHighA = 152.0, kRateHighB = 17.7;
    static constexpr double kWarmSeconds = 4.0e-3;

    void applyParams(const VoiceParams& p) noexcept {
        params_ = p;
        haveParams_ = true;
        const double ratio = dsp::semitonesToRatio(static_cast<double>(p.tune));  // p is sane: +-24
        const double inv = 1.0 / ratio;
        bank_.setTune(ratio);

        // Lane 0: BP#1 and the low band; lane 1: BP#2 and the mid band.
        inHp_.setCutoff(0, kBp1InHz * bp1Scale_ * ratio, rate_);
        inHp_.setCutoff(1, kBp2InHz * bp2Scale_ * ratio, rate_);
        const dsp::ResonatorCoefs r1 = dsp::matchedResonator(kBp1Hz * bp1Scale_ * ratio, kBp1Q, rate_);
        const dsp::ResonatorCoefs r2 = dsp::matchedResonator(kBp2Hz * bp2Scale_ * ratio, kBp2Q, rate_);
        bp_.setG(0, r1.g, r1.k);
        bp_.setG(1, r2.g, r2.k);
        bpLpGain_ = dsp::v2d{-kBp1Gain * r1.lowpassScale, -kBp2Gain * r2.lowpassScale};

        clamp_.prepare(0, rate_, inv);
        clamp_.prepare(1, rate_, inv);
        rc2_ = 1.0 / (1.0 / kR90 + 1.0 / kR91);
        vca12_.prepare(0, rate_, kR104, kShuntLow, inv);
        vca12_.prepare(1, rate_, rc2_, kShunt, inv);
        vca3_.prepare(rate_, kR94, kShunt, inv);

        hp12_.setFreqQ(0, kHp1Hz * hp1Scale_ * ratio, kHp1Q, rate_);
        hp12_.setFreqQ(1, kHp2Hz * hp2Scale_ * ratio, kHp2Q, rate_);
        lowLpHp3a_.setCutoff(1, kHp3aHz * hp3Scale_ * ratio, rate_);
        hp3_.setFreqQ(kHp3Hz * hp3Scale_ * ratio, kHp3Q, rate_);
        outLp_.setCutoff(kOutLpHz * ratio, rate_);

        // DECAY: R93 + VR2 (B taper, wired as a rheostat) across C41, with the follower load.
        const double k = dsp::clamp(static_cast<double>(p.decay), 0.0, 1.0);
        const double rPot = kR93 + k * kVr2;
        const double r = rPot * kFollowerLoad / (rPot + kFollowerLoad);
        decay41_ = std::exp(-dtc_ / (r * c41_));

        // TONE: VR4 20k(B), wiper to ground. Node A (high band) at the CCW end,
        // node B (low + mid) through R125 2.2k at the CW end.
        const double t = dsp::clamp(static_cast<double>(p.tone), 0.0, 1.0);
        const double rA = t * 20.0e3;
        const double rB = 2.2e3 + (1.0 - t) * 20.0e3;
        const double rAp = rA * 22.0e3 / (rA + 22.0e3);
        mixHigh_ = kGainHigh * rAp / (22.0e3 + rAp);
        // Node B: the pot leg, R129 15k to the output stage, the mid band through
        // R120 10k and the low band through R123 100k + R121 10k (C57 between them).
        const double gBnoLow = 1.0 / rB + 1.0 / 15.0e3 + 1.0 / 10.0e3;
        const double zB = 1.0 / gBnoLow;
        const double toOut = 22.0e3 / 15.0e3;  // currents into the output stage, scaled by 22k
        mixLow_ = zB / (kR123 + kR121 + zB) * toOut;
        const double gB = gBnoLow + 1.0 / (kR123 + kR121);
        mixMid_ = kGainMid * (1.0 / 10.0e3) / gB * toOut;
        // Output gain, and the finite-rate level correction (MetalCommon.h) of
        // the two bands behind the 7.1 kHz band-pass. The low band's 3.4 kHz
        // drive is well resolved even at 44.1 kHz and needs none.
        const double out = kOutGain / kVoltsPerUnit;
        const double bp2 = kBp2Hz * bp2Scale_ * ratio;
        mixLow_ *= out;
        mixMid_ *= out * dsp::swingRateGain(bp2, rate_, kRateMidA, kRateMidB);
        mixHigh_ *= out * dsp::swingRateGain(bp2, rate_, kRateHighA, kRateHighB);
        const double rLp = kR123 * (kR121 + zB) / (kR123 + kR121 + zB);
        lowLpHp3a_.setCutoff(0, ratio / (dsp::kTwoPi * rLp * kC57), rate_);
    }

    void clearState() noexcept {
        droop_.reset();
        inHp_.reset();
        bp_.reset();
        clamp_.reset();
        vca12_.reset();
        vca3_.reset();
        hp12_.reset();
        lowLpHp3a_.reset();
        hp3_.reset();
        outLp_.reset();
        decimator_.reset();
        v41_ = v45_ = v40_ = v39_ = v38_ = v37_ = 0.0;
        sup3_.reset();
        sumC12_ = dsp::splat(0.0);
        sumC3_ = 0.0;
        bandOut_ = drive_ = c12_ = lowMid_ = dsp::splat(0.0);
        c3_ = hp3In_ = 0.0;
        ctrlCount_ = 0;
        ctrlSpan_ = 0;
        pulseLeft_ = 0;
        charge_ = 0.0;
        silence_.restart();
    }

    // Wake from sleep: replay the last few milliseconds of the oscillators
    // through the band-passes and base clamps (envelopes at zero, so nothing
    // reaches the output) and continue from "now".
    void wake() noexcept {
        bank_.rewind(static_cast<std::uint64_t>(warmSamples_));
        for (int i = 0; i < warmSamples_; ++i) (void)tick();
        active_ = true;
    }

    // Control tick: advance the envelope networks by one control period and
    // aim the three VCA supplies at the new values.
    void control() noexcept {
        // Mean collector voltages since the last control tick (what the
        // envelope capacitors were actually feeding).
        dsp::v2d c12 = vca12_.collectorVolts();
        double c3 = vca3_.collectorVolts();
        if (ctrlSpan_ > 0) {
            const double inv = ctrlSpan_ == ctrlN_ ? invCtrlN_ : 1.0 / ctrlSpan_;
            c12 = sumC12_ * inv;
            c3 = sumC3_ * inv;
        }
        const double c1 = c12[0], c2 = c12[1];
        sumC12_ = dsp::splat(0.0);
        sumC3_ = 0.0;
        ctrlSpan_ = 0;

        if (pulseLeft_ > 0) {
            --pulseLeft_;
            if (charge_ > v41_) v41_ += (charge_ - v41_) * attack_;
            if (charge_ > v40_) v40_ += (charge_ - v40_) * attack_;
            if (charge_ > v38_) v38_ += (charge_ - v38_) * attack_;
        }
        v41_ *= decay41_;
        const double e11 = dsp::softDrop(v41_, kFollowerDrop);
        const double e13 = e11 * kDivider13;
        // EG#1.2: C45 fills through R105 and feeds VCA#1's collector through R104.
        v45_ += k45_ * ((e11 - v45_) * (1.0 / kR105) - (v45_ - c1) * (1.0 / kR104));
        // EG#2: C40 -> R88 -> C39 -> R90 -> VCA#2.
        const double i88 = (v40_ - v39_) * (1.0 / kR88);
        const double i90 = (v39_ - c2) * (1.0 / kR90);
        v40_ -= k40_ * i88;
        v39_ += k39_ * (i88 - i90);
        const double env2 = (v39_ * (1.0 / kR90) + e13 * (1.0 / kR91)) * rc2_;
        // EG#3: C38 with R87 + C37 across it, feeding VCA#3 through R94.
        const double i87 = (v38_ - v37_) * (1.0 / kR87);
        const double i94 = (v38_ - c3) * (1.0 / kR94);
        v38_ -= k38_ * (i87 + i94);
        v37_ += k37_ * i87;

        vca12_.aim(0, v45_, tables_.range, invCtrlN_);
        vca12_.aim(1, env2, tables_.range, invCtrlN_);
        sup3_.aim(v38_, tables_.range, invCtrlN_);
        ctrlCount_ = ctrlN_;
    }

    // One internal sample. Software-pipelined: each stage works on what the
    // stage before it produced on the previous tick, so the stages are
    // independent within a tick and the CPU overlaps them (the chain from
    // oscillators to output is ~250 cycles long; one tick at a time it was
    // latency-bound). Costs kPipelineTicks internal samples of delay.
    double tick() noexcept {
        if (ctrlCount_ <= 0) control();
        --ctrlCount_;
        ++ctrlSpan_;
        sup3_.step();

        // ---- stage 5: HP#3's resonant section, tone mix, output stage ----
        hp3_.tick(hp3In_);
        const double mixed = mixLow_ * lowMid_[0] + mixMid_ * (lowMid_[1] * kHp2Gain) + mixHigh_ * (hp3_.highpass() * kHp3Gain);
        const double out = outLp_.tick(mixed);

        // ---- stage 4: HP#1 / HP#2; the low band's low-pass and HP#3's first-order section ----
        hp12_.tick(c12_);
        const dsp::v2d hp12 = hp12_.highpass();
        const dsp::v2d lowAndC3 = dsp::v2d{hp12[0], c3_};
        const dsp::v2d lp = lowLpHp3a_.lowpass(lowAndC3);
        lowMid_ = dsp::v2d{lp[0], hp12[1]};
        hp3In_ = c3_ - lp[1];

        // ---- stage 3: collectors (VCA#2 and #3 hang on the same band-pass: one base clamp) ----
        c12_ = vca12_.tick(drive_);
        c3_ = vca3_.tick(drive_[1], sup3_);
        sumC12_ += c12_;
        sumC3_ += c3_;

        // ---- stages 2a/2b: base clamps (pipelined inside) ----
        drive_ = clamp_.tickPipelined(bandOut_);

        // ---- stage 1: oscillators and band-passes (lane 0: BP#1, lane 1: BP#2) ----
        const double s = droop_.tick(bank_.tickSum()) * kOscVolts;
        bp_.tick(inHp_.highpass(dsp::splat(s)));
        bandOut_ = bp_.lowpass() * bpLpGain_ - bpGain_ * bp_.bandpassUnity();
        return out;
    }

    // configuration
    int os_ = 1;
    double rate_ = 48000.0, dt_ = 1.0 / 48000.0, dtc_ = 1.0 / 12000.0, invCtrlN_ = 0.25;
    int ctrlN_ = 4;
    VoiceParams params_{};
    bool haveParams_ = false;
    double bp1Scale_ = 1.0, bp2Scale_ = 1.0, hp1Scale_ = 1.0, hp2Scale_ = 1.0, hp3Scale_ = 1.0;
    // band-pass output gains: -(Rb/Rin) on both terms, the low-pass term with
    // the resonator's bandwidth-matching scale
    dsp::v2d bpLpGain_ = dsp::v2d{-kBp1Gain, -kBp2Gain};
    static constexpr dsp::v2d bpGain_ = dsp::v2d{kBp1Gain, kBp2Gain};
    double c41_ = 1.0e-6, c45_ = 2.2e-6, c40_ = 1.0e-6, c39_ = 0.47e-6, c38_ = 1.0e-6, c37_ = 2.2e-6;
    double attack_ = 1.0, decay41_ = 1.0, rc2_ = 16.5e3;
    double k45_ = 0.0, k40_ = 0.0, k39_ = 0.0, k38_ = 0.0, k37_ = 0.0;  // dt / C per control tick
    double mixLow_ = 0.05, mixMid_ = 0.4, mixHigh_ = 0.2;
    int pulseTicks_ = 12, warmSamples_ = 0;

    // blocks
    dsp::MetalOscBank bank_;
    dsp::BlepDroopCompensator droop_;
    dsp::OnePole2 inHp_;      // the band-passes' input R-C (lane 0: BP#1, lane 1: BP#2)
    dsp::Svf2 bp_;            // BP#1, BP#2
    dsp::SwingBaseClamp2 clamp_;
    dsp::SwingCollector2 vca12_;  // VCA#1 and VCA#2 with their supplies
    dsp::SwingCollector vca3_;
    dsp::SwingSupply sup3_;
    dsp::Svf2 hp12_;          // HP#1, HP#2
    dsp::OnePole2 lowLpHp3a_;  // low band R123/C57 low-pass, HP#3 first-order section
    dsp::Svf hp3_;
    dsp::SwingTables tables_;
    dsp::MatchedLowpass outLp_;
    dsp::MetalDecimator decimator_;
    dsp::SilenceDetector silence_;

    // state
    double v41_ = 0.0, v45_ = 0.0, v40_ = 0.0, v39_ = 0.0, v38_ = 0.0, v37_ = 0.0;
    double charge_ = 0.0;
    dsp::v2d sumC12_ = dsp::splat(0.0);
    double sumC3_ = 0.0;
    // pipeline registers (see tick())
    dsp::v2d bandOut_ = dsp::splat(0.0), drive_ = dsp::splat(0.0), c12_ = dsp::splat(0.0), lowMid_ = dsp::splat(0.0);
    double c3_ = 0.0, hp3In_ = 0.0;
    int ctrlCount_ = 0, ctrlSpan_ = 0;
    int pulseLeft_ = 0;
    bool active_ = false;
};

}  // namespace mw808::voices

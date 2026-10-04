// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Open and closed hi-hat (OH, CH) and the choke between them. Circuit and
// values: research/02 §1, §3 (service manual p.6, p.13).
//
//   six squares --> BP#2 7.1 kHz -+-> VCA Q27 --> HP 7.83 kHz Q 2.5 --> out stage --> OH
//                                 +-> VCA Q30 --> HP 11.75 kHz Q 2.5 --> out stage --> CH
//
// DERIVED from the schematic:
//  * Source: the cymbal's BP#2 (C11 1n, R55 22k, C15 = C16 3.3n, R58 560,
//    R59 82k), see Cymbal.h. Both VCA bases hang on it through 22 nF with the
//    same 2 MOhm bias, so one base clamp serves both (dsp/SwingVca.h).
//  * Swing VCAs: Rc = R144 / R142 33k, 2 x 1.5 nF at each collector.
//  * OH envelope: C62 0.47u charged through R137 470 (tau 0.22 ms), buffered
//    by follower Q29 (0.62 V). It leaks through R139 1M, and is DUMPED through
//    R134 100k when Q22 or Q23 saturates:
//      - Q22 is a timer. Its base node (C60 10u) is shorted by the trigger and
//        then charges from +15 V through DECAY VR3 2M(B) + R132 100k; Q22
//        turns on when the node reaches ~0.6 V, i.e. after
//        0.041 * (VR3 + 100k) * 10u = 41 ms .. 0.86 s. So DECAY sets WHEN the
//        hat is shut, not how fast: a slow fall (tau ~0.4 s) and then a dump
//        (tau 47 ms). With the nominal parts this gives 96 / 449 / 593 ms to
//        -20 dB (service manual: 90 / 450 / 600) and the abrupt endings heard
//        in the reference recordings. Retriggered before the timer runs out,
//        the open hat is never shut.
//      - Q23 is the choke: its base is fed from the CH envelope through R173
//        330k, so a closed-hat hit dumps C62 for as long as the closed hat
//        sounds. A fast fade, not a cut; an open hat hit during that time is
//        held down too.
//  * CH envelope: C63 0.47u charged by follower Q28; it discharges INTO its
//    own VCA through R142 33k (the collector current is the discharge), plus
//    R173 into Q23's base.
//  * High-passes: OH C66 = C68 1.5n, R147 2.7k, R146 68k (7.83 kHz, Q 2.51);
//    CH C72 = C73 1n, R153 2.7k, R155 68k (11.75 kHz, Q 2.51).
//  * Output stages with LEVEL at maximum: C80 470p + R160 39k in (OH, 8.68 kHz)
//    / R162 33k (CH, 10.26 kHz), feedback 470k||100k with 220 pF (8.77 kHz):
//    a +6 dB/oct rise into a broad peak near 9 kHz.
//
// FITTED:
//  * Follower loading of C62 (8 MOhm, about beta x the 40k emitter load) on
//    the slow OH fall: -20 dB at ~620 ms with DECAY at maximum.
//  * CH envelope capacitor scaled x1.4 so the stock closed hat reaches
//    -20 dB in ~50 ms (service manual 50 ms, reference recording 39 ms).
//  * Trigger coupling powers (0.76 / 0.77) and output gains: service-manual
//    levels (OH 3.5 / 7 Vpp, CH 3 / 6 Vpp normal / accented).
//  * The finite-rate level correction (dsp::swingRateGain, per hat), against
//    renders at a 768 kHz internal rate: the level holds within 0.15 dB from
//    44.1 kHz x 1 to 192 kHz x 4.
//  * Q22 / Q23 turn-on voltages (0.60 / 0.62 V); their turn-on is one thermal
//    voltage soft.
//
// Non-hardware: CH decay (0.5 = stock, scales C63), and tune per hat. With
// equal tunes both hats share one oscillator bank and band-pass, as in the
// hardware; with different tunes the closed hat gets its own copy.

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

class HiHats {
public:
    void prepare(const VoiceSetup& setup) noexcept {
        os_ = setup.oversample >= 4 ? 4 : (setup.oversample >= 2 ? 2 : 1);
        rate_ = setup.sampleRate * os_;
        dt_ = 1.0 / rate_;

        const std::uint32_t seed = setup.unitSeed;
        const float var = setup.variance;
        using dsp::componentScale;
        namespace mp = dsp::metalparts;
        bpScale_ = 1.0 / std::sqrt(static_cast<double>(componentScale(seed, mp::kBandPass + 1U, 0.20f, var)) *
                                   componentScale(seed, mp::kBandPass + 3U, 0.20f, var));
        c62_ = 0.47e-6 * componentScale(seed, mp::kHats + 0U, 0.20f, var);
        c63_ = 0.47e-6 * componentScale(seed, mp::kHats + 1U, 0.20f, var);
        c60_ = 10.0e-6 * componentScale(seed, mp::kHats + 2U, 0.20f, var);
        hpOpenScale_ = 1.0 / componentScale(seed, mp::kHats + 3U, 0.20f, var);
        hpClosedScale_ = 1.0 / componentScale(seed, mp::kHats + 4U, 0.20f, var);

        bankA_.prepare(rate_, seed, var);
        bankB_.prepare(rate_, seed, var);
        tables_.buildCymbalStyle();
        decOpen_.prepare(os_);
        decClosed_.prepare(os_);
        silence_.prepare(setup.sampleRate);
        ctrlN_ = std::max(1, static_cast<int>(rate_ / dsp::kMetalControlHz + 0.5));
        invCtrlN_ = 1.0 / ctrlN_;
        dtc_ = dt_ * ctrlN_;
        attackOpen_ = 1.0 - std::exp(-dtc_ / (kR137 * c62_));
        attackClosed_ = 1.0 - std::exp(-dtc_ / kClosedAttackTau);
        pulseTicks_ = std::max(1, static_cast<int>(kPulseSeconds / dtc_ + 0.5));
        warmSamples_ = static_cast<int>(kWarmSeconds * rate_);

        haveParams_ = false;
        applyParams(open_, closed_);
        reset();
    }

    void reset() noexcept {
        bankA_.reset();
        bankB_ = bankA_;
        clearState();
        active_ = false;
    }

    void setParams(const VoiceParams& rawOpen, const VoiceParams& rawClosed) noexcept {
        const VoiceParams open = dsp::saneParams(rawOpen), closed = dsp::saneParams(rawClosed);
        if (haveParams_ && open.tune == open_.tune && open.decay == open_.decay &&
            closed.tune == closed_.tune && closed.decay == closed_.decay)
            return;
        applyParams(open, closed);
    }

    void triggerOpen(const Hit& hit) noexcept {
        if (!active_) wake();
        chargeOpen_ = dsp::metalChargeVolts(dsp::saneVolts(hit.volts), kOpenTrigDrop, kOpenTrigPower);
        pulseOpen_ = pulseTicks_;
        ctrlCount_ = 0;
        silence_.restart();
    }

    // Also chokes the open hat: Q23 dumps the open-hat envelope while the
    // closed-hat envelope is up.
    void triggerClosed(const Hit& hit) noexcept {
        if (!active_) wake();
        chargeClosed_ = dsp::metalChargeVolts(dsp::saneVolts(hit.volts), kClosedTrigDrop, kClosedTrigPower);
        pulseClosed_ = pulseTicks_;
        ctrlCount_ = 0;
        silence_.restart();
    }

    void render(float* outOpen, float* outClosed, int n) noexcept {
        if (!active_) {
            sleepFor(n);
            for (int i = 0; i < n; ++i) outOpen[i] = outClosed[i] = 0.0f;
            return;
        }
        for (int i = 0; i < n; ++i) {
            double hiO[dsp::MetalDecimator::kMaxFactor];
            double hiC[dsp::MetalDecimator::kMaxFactor];
            for (int k = 0; k < os_; ++k) {
                const dsp::v2d y = tick();
                hiO[k] = y[0];
                hiC[k] = y[1];
            }
            const double yo = decOpen_.process(hiO);
            const double yc = decClosed_.process(hiC);
            outOpen[i] = static_cast<float>(yo);
            outClosed[i] = static_cast<float>(yc);
            if (silence_.tick(std::abs(yo) + std::abs(yc)) && pulseOpen_ == 0 && pulseClosed_ == 0) {
                sleepFor(n - 1 - i);
                for (int j = i + 1; j < n; ++j) outOpen[j] = outClosed[j] = 0.0f;
                clearState();
                active_ = false;
                return;
            }
        }
    }

    bool isActive() const noexcept { return active_; }

    // Low-frequency group delay in output samples, identical for both hats:
    // the decimator, one internal sample of oscillator droop compensation and
    // the processing pipeline (5.0 / 3.9 / 3.1 at oversample 1 / 2 / 4).
    double latencySamples() const noexcept {
        return decOpen_.latencySamples() + static_cast<double>(1 + kPipelineTicks) / os_;
    }
    static constexpr int kPipelineTicks = 4;  // pipeline registers in tick()
    const dsp::MetalOscBank& oscillators() const noexcept { return bankA_; }
    // Diagnostic: the open-hat envelope capacitor, volts.
    double openEnvelopeVolts() const noexcept { return v62_; }

    // ---- schematic values -------------------------------------------------
    static constexpr double kOscVolts = 2.5 / 126.0;
    static constexpr double kBpHz = 7117.0, kBpQ = 6.05, kBpGain = 82.0 / 22.0, kBpInHz = 7234.3;
    static constexpr double kPulseSeconds = 1.0e-3;
    static constexpr double kR137 = 470.0, kR139 = 1.0e6, kR134 = 100.0e3;
    static constexpr double kVr3 = 2.0e6, kR132 = 100.0e3, kSupply = 15.0;
    static constexpr double kFollowerDrop = 0.62;
    static constexpr double kR144 = 33.0e3, kR142 = 33.0e3, kR173 = 330.0e3;
    static constexpr double kShunt = 3.0e-9;
    static constexpr double kHpOpenHz = 7830.0, kHpClosedHz = 11750.0, kHpQ = 2.51;
    static constexpr double kOutHpOpenHz = 8683.0, kOutHpClosedHz = 10261.0, kOutLpHz = 8770.0;
    // ---- fitted -----------------------------------------------------------
    static constexpr double kFollowerLoad = 8.0e6;    // Q29 base current, as a resistance across C62
    static constexpr double kClosedCapScale = 1.4;  // effective C63
    static constexpr double kTimerOn = 0.60, kChokeOn = 0.62;  // Q22 / Q23 turn-on, volts
    static constexpr double kClosedAttackTau = 1.0e-4;
    static constexpr double kOpenTrigDrop = 1.3, kOpenTrigPower = 0.76;
    static constexpr double kClosedTrigDrop = 1.3, kClosedTrigPower = 0.77;
    static constexpr double kOpenGain = 1.231, kClosedGain = 2.073;
    // Finite-rate level correction (dsp::swingRateGain), fitted against a 768 kHz render.
    static constexpr double kRateOpenA = 108.4, kRateOpenB = 6.7;
    static constexpr double kRateClosedA = 165.4, kRateClosedB = 22.4;
    static constexpr double kWarmSeconds = 4.0e-3;

private:
    void applyParams(const VoiceParams& open, const VoiceParams& closed) noexcept {
        open_ = open;
        closed_ = closed;
        haveParams_ = true;
        const bool split = open.tune != closed.tune;
        if (split && !split_) {
            // The closed hat gets its own copy of the bank and band-pass, taken
            // over from the shared one as it is now (the clamp's second lane
            // already carries the same signal), so the split is seamless.
            bankB_ = bankA_;
            droopB_ = droopA_;
            inHpB_ = inHpA_;
            bpB_ = bpA_;
        }
        split_ = split;
        const double ratioO = dsp::semitonesToRatio(static_cast<double>(open.tune));
        const double ratioC = dsp::semitonesToRatio(static_cast<double>(closed.tune));

        bankA_.setTune(ratioO);
        inHpA_.setCutoff(kBpInHz * bpScale_ * ratioO, rate_);
        bpLpA_ = dsp::setResonatorMatched(bpA_, kBpHz * bpScale_ * ratioO, kBpQ, rate_);
        clamp_.prepare(0, rate_, 1.0 / ratioO);
        clamp_.prepare(1, rate_, 1.0 / (split_ ? ratioC : ratioO));
        if (split_) {
            bankB_.setTune(ratioC);
            inHpB_.setCutoff(kBpInHz * bpScale_ * ratioC, rate_);
            bpLpB_ = dsp::setResonatorMatched(bpB_, kBpHz * bpScale_ * ratioC, kBpQ, rate_);
        }

        // Lane 0: open hat, lane 1: closed hat.
        col_.prepare(0, rate_, kR144, kShunt, 1.0 / ratioO);
        col_.prepare(1, rate_, kR142, kShunt, 1.0 / ratioC);
        hp_.setFreqQ(0, kHpOpenHz * hpOpenScale_ * ratioO, kHpQ, rate_);
        hp_.setFreqQ(1, kHpClosedHz * hpClosedScale_ * ratioC, kHpQ, rate_);
        outHp_.setCutoff(0, kOutHpOpenHz * ratioO, rate_);
        outHp_.setCutoff(1, kOutHpClosedHz * ratioC, rate_);
        outLp_.setCutoff(0, kOutLpHz * ratioO, rate_);
        outLp_.setCutoff(1, kOutLpHz * ratioC, rate_);
        // Output gains with the finite-rate level correction (MetalCommon.h).
        outScale_ = dsp::v2d{kOpenGain / kVoltsPerUnit * dsp::swingRateGain(kBpHz * bpScale_ * ratioO, rate_, kRateOpenA, kRateOpenB),
                             kClosedGain / kVoltsPerUnit * dsp::swingRateGain(kBpHz * bpScale_ * ratioC, rate_, kRateClosedA, kRateClosedB)};

        // OH DECAY: VR3 (B taper) in series with R132 charges the timer node.
        const double k = dsp::clamp(static_cast<double>(open.decay), 0.0, 1.0);
        timerRate_ = dtc_ / ((k * kVr3 + kR132) * c60_);
        const double leak = kR139 * kFollowerLoad / (kR139 + kFollowerLoad);
        leak62_ = dtc_ / (leak * c62_);
        dump62_ = dtc_ / (kR134 * c62_);
        // CH decay (non-hardware): scales C63.
        const double c63 = c63_ * kClosedCapScale * dsp::decayKnobScale(static_cast<double>(closed.decay));
        k63_ = dtc_ / c63;
    }

    void clearState() noexcept {
        droopA_.reset();
        droopB_.reset();
        inHpA_.reset();
        bpA_.reset();
        inHpB_.reset();
        bpB_.reset();
        clamp_.reset();
        col_.reset();
        hp_.reset();
        outHp_.reset();
        outLp_.reset();
        decOpen_.reset();
        decClosed_.reset();
        v62_ = v63_ = vTimer_ = 0.0;
        sumClosed_ = 0.0;
        bandOut_ = drive_ = c_ = dsp::splat(0.0);
        ctrlCount_ = ctrlSpan_ = 0;
        pulseOpen_ = pulseClosed_ = 0;
        chargeOpen_ = chargeClosed_ = 0.0;
        silence_.restart();
    }

    void sleepFor(int outputSamples) noexcept {
        const std::uint64_t ticks = static_cast<std::uint64_t>(outputSamples) * static_cast<std::uint64_t>(os_);
        bankA_.advance(ticks);
        if (split_) bankB_.advance(ticks);
    }

    // Wake from sleep: replay the last few milliseconds of the oscillators
    // through the band-pass and base clamp (envelopes at zero, so nothing
    // reaches the outputs) and continue from "now".
    void wake() noexcept {
        const auto warm = static_cast<std::uint64_t>(warmSamples_);
        bankA_.rewind(warm);
        if (split_) bankB_.rewind(warm);
        for (int i = 0; i < warmSamples_; ++i) (void)tick();
        active_ = true;
    }

    static double logistic(double x) noexcept { return 1.0 / (1.0 + std::exp(-dsp::clamp(x, -40.0, 40.0))); }

    void control() noexcept {
        double cClosed = col_.collectorVolts()[1];
        if (ctrlSpan_ > 0) cClosed = sumClosed_ * (ctrlSpan_ == ctrlN_ ? invCtrlN_ : 1.0 / ctrlSpan_);
        sumClosed_ = 0.0;
        ctrlSpan_ = 0;

        if (pulseOpen_ > 0) {
            --pulseOpen_;
            if (chargeOpen_ > v62_) v62_ += (chargeOpen_ - v62_) * attackOpen_;
            vTimer_ = 0.0;  // Q21 shorts the timer node for the length of the trigger
        }
        if (pulseClosed_ > 0) {
            --pulseClosed_;
            if (chargeClosed_ > v63_) v63_ += (chargeClosed_ - v63_) * attackClosed_;
        }
        // Timer node: towards +15 V, clamped by Q22's base-emitter junction.
        vTimer_ += timerRate_ * (kSupply - vTimer_);
        if (vTimer_ > kTimerOn + 0.1) vTimer_ = kTimerOn + 0.1;
        // Q22 (timer) and Q23 (choke) pull C62 down through R134 once they conduct.
        const double q22 = logistic((vTimer_ - kTimerOn) * (1.0 / dsp::kThermalVolts));
        const double q23 = logistic((v63_ - kChokeOn) * (1.0 / dsp::kThermalVolts));
        const double sink = 1.0 - (1.0 - q22) * (1.0 - q23);
        v62_ -= v62_ * (leak62_ + sink * dump62_);
        // C63 discharges into the closed-hat VCA and into Q23's base.
        const double base = v63_ > kChokeOn ? (v63_ - kChokeOn) * (1.0 / kR173) : 0.0;
        v63_ -= k63_ * ((v63_ - cClosed) * (1.0 / kR142) + base);
        if (v63_ < 0.0) v63_ = 0.0;

        col_.aim(0, dsp::softDrop(v62_, kFollowerDrop), tables_.range, invCtrlN_);
        col_.aim(1, v63_, tables_.range, invCtrlN_);
        ctrlCount_ = ctrlN_;
    }

    // One internal sample; lane 0: open hat, lane 1: closed hat.
    // Software-pipelined like Cymbal::tick(): each stage works on what the one
    // before it produced on the previous tick. Costs kPipelineTicks samples.
    dsp::v2d tick() noexcept {
        if (ctrlCount_ <= 0) control();
        --ctrlCount_;
        ++ctrlSpan_;

        // ---- stage 4: high-passes and output stages ----
        hp_.tick(c_);
        const dsp::v2d out = outLp_.tick(outHp_.highpass(hp_.highpass())) * outScale_;

        // ---- stage 3: collectors ----
        c_ = col_.tick(drive_);
        sumClosed_ += c_[1];

        // ---- stages 2a/2b: base clamp(s) ----
        drive_ = clamp_.tickPipelined(bandOut_);

        // ---- stage 1: oscillators and band-pass(es) ----
        const double sA = droopA_.tick(bankA_.tickSum()) * kOscVolts;
        bpA_.tick(inHpA_.highpass(sA));
        const double dA = -kBpGain * (bpA_.lowpass() * bpLpA_ + bpA_.bandpassUnity());
        double dB = dA;
        if (split_) {
            const double sB = droopB_.tick(bankB_.tickSum()) * kOscVolts;
            bpB_.tick(inHpB_.highpass(sB));
            dB = -kBpGain * (bpB_.lowpass() * bpLpB_ + bpB_.bandpassUnity());
        }
        bandOut_ = dsp::v2d{dA, dB};
        return out;
    }

    // configuration
    int os_ = 1;
    double rate_ = 48000.0, dt_ = 1.0 / 48000.0, dtc_ = 1.0 / 12000.0, invCtrlN_ = 0.25;
    int ctrlN_ = 4;
    VoiceParams open_{}, closed_{};
    bool haveParams_ = false;
    bool split_ = false;
    double bpScale_ = 1.0, hpOpenScale_ = 1.0, hpClosedScale_ = 1.0;
    double bpLpA_ = 1.0, bpLpB_ = 1.0;
    dsp::v2d outScale_ = dsp::splat(0.3);  // low-pass output scale of the bandwidth-matched resonators
    double c62_ = 0.47e-6, c63_ = 0.47e-6, c60_ = 10.0e-6;
    double attackOpen_ = 1.0, attackClosed_ = 1.0;
    double timerRate_ = 0.0, leak62_ = 0.0, dump62_ = 0.0, k63_ = 0.0;
    int pulseTicks_ = 12, warmSamples_ = 0;

    // blocks
    dsp::MetalOscBank bankA_, bankB_;
    dsp::BlepDroopCompensator droopA_, droopB_;
    dsp::OnePole inHpA_, inHpB_;
    dsp::Svf bpA_, bpB_;
    dsp::SwingBaseClamp2 clamp_;  // lane 1 is the closed hat's own band-pass when split, else a copy
    // lane 0: open hat, lane 1: closed hat
    dsp::SwingCollector2 col_;
    dsp::Svf2 hp_;
    dsp::OnePole2 outHp_;
    dsp::MatchedLowpass2 outLp_;
    dsp::SwingTables tables_;
    dsp::MetalDecimator decOpen_, decClosed_;
    dsp::SilenceDetector silence_;

    // state
    double v62_ = 0.0, v63_ = 0.0, vTimer_ = 0.0;
    double chargeOpen_ = 0.0, chargeClosed_ = 0.0;
    double sumClosed_ = 0.0;
    dsp::v2d bandOut_ = dsp::splat(0.0), drive_ = dsp::splat(0.0), c_ = dsp::splat(0.0);  // pipeline registers
    int ctrlCount_ = 0, ctrlSpan_ = 0;
    int pulseOpen_ = 0, pulseClosed_ = 0;
    bool active_ = false;
};

}  // namespace mw808::voices

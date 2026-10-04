// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The "swing-type" VCA of the metallic voices (research/02 §2.5, §4.3): an
// NPN common-emitter stage with no collector supply of its own. The collector
// returns through a diode and a resistor Rc to the ENVELOPE voltage, and the
// base is AC-coupled (22 nF) and biased only by 2 MOhm from +15 V. The stage
// therefore clips between the envelope voltage (transistor off) and a lower,
// saturation/diode edge that tracks the envelope. That clipping is the sizzle.
//
// Three mechanisms are modelled:
//
//  1. Base clamp (derived). The 2 MOhm chain feeds a fixed Ib0 = 7.2 uA and the
//     coupling capacitor blocks DC, so the base-emitter junction works as a
//     clamp: C dVc/dt = Ib0 (e^w - 1), w = (Vbe - Vbe0)/VT. In y = e^-w this is
//     LINEAR, dy/dt = (1 - y)/tau0 with tau0 = C VT / Ib0 = 79 us, so each
//     sample is solved exactly: an input step multiplies y by e^(-dV/VT), then
//     y relaxes towards 1. No iteration, unconditionally stable. The transistor
//     conducts in spikes at the positive peaks of the drive; the charge of each
//     spike follows exactly from charge balance on the coupling capacitor.
//  2. Collector swing. The collector wants to drop K e^w below the envelope,
//     K = beta Ib0 Rc (beta = 200, fitted), but cannot pass a lower edge that
//     tracks the envelope: range D(Venv) = Venv - VlowerEdge(Venv), the
//     published fit of [WCY] Table 1.
//  3. Collector capacitance (2 x 1.5 nF on VCA#2/#3 and both hats; the
//     following high-pass's 1.5 nF input capacitor on VCA#1). The transistor
//     pulls the node down fast and Rc lets it recover slowly: sharp falling
//     edges, exponential recoveries.
//
// Where this departs from the published model: [WCY] eq. 15 is a memoryless
// soft clip (sharpness 3.5) between the envelope and the lower edge. Here the
// edge is abrupt and the rounding comes from the node's own RC dynamics; a
// static curve re-applied every sample to a node with memory would leak at a
// rate proportional to the sample rate.
//
// D(Venv) is tabulated at prepare time (SwingRangeTable); it is exactly zero
// below the gate threshold (~0.2 V), which is what finally silences a voice.
// Envelopes move slowly, so the voices advance them at a control rate of about
// 12 kHz and hand each VCA a linearly ramped SwingSupply.

#pragma once

#include <array>
#include <cstdint>

#include "mw808/dsp/MetalCommon.h"
#include "mw808/dsp/MetalSimd.h"

namespace mw808::dsp {

// Lower clipping edge of the cymbal / hi-hat swing VCA versus envelope volts:
// [WCY] Table 1 (research/02 §2.5, CONFIRMED).
inline double swingLowerEdgeCymbal(double v) noexcept {
    return -1.396 * std::exp(-1.063 * v) + 0.825 * std::exp(-0.837 * std::pow(v, 1.447)) + 0.566;
}

// Cowbell VCA edges versus envelope volts: [WCB] Table 1 with the signs as
// corrected in research/02 §4.3 (alpha0 +, alpha1 -, alpha2 -, both gammas
// negative, alpha4 = -0.03122).
inline double swingLowerEdgeCowbell(double v) noexcept {
    if (v < 1.0e-3) return 0.0;
    return 14.97 * std::exp(-0.01837 * std::pow(v, -0.3179)) -
           0.01872 * std::exp(-1.813 * std::pow(v, -2.988)) - 14.33;
}
inline double swingUpperEdgeCowbell(double v) noexcept { return 1.011 * v - 0.03122; }

// Output range D(Venv) >= 0 of a swing VCA, tabulated over 0..16 V.
class SwingRangeTable {
public:
    static constexpr int kSize = 2048;
    static constexpr double kMaxVolts = 16.0;
    // Subtracted before the clamp at zero so the gate closes in finite time
    // (the fits approach zero asymptotically); 2 mV is -70 dB re a 6 V swing.
    static constexpr double kGateEpsilon = 0.002;

    template <typename F>
    void build(F&& rangeOfEnv) noexcept {
        for (int i = 0; i <= kSize; ++i) {
            const double v = kMaxVolts * static_cast<double>(i) / static_cast<double>(kSize);
            table_[static_cast<std::size_t>(i)] =
                static_cast<float>(std::max(0.0, rangeOfEnv(v) - kGateEpsilon));
        }
        table_[0] = 0.0f;
    }

    double operator()(double envVolts) const noexcept {
        const double x = clamp(envVolts, 0.0, kMaxVolts) * (static_cast<double>(kSize) / kMaxVolts);
        const int i = std::min(static_cast<int>(x), kSize - 1);
        const double f = x - static_cast<double>(i);
        const double a = table_[static_cast<std::size_t>(i)];
        const double b = table_[static_cast<std::size_t>(i + 1)];
        return a + (b - a) * f;
    }

private:
    std::array<float, kSize + 1> table_{};
};

// The curve a voice with swing VCAs needs. Built once, in prepare().
struct SwingTables {
    SwingRangeTable range;
    bool built = false;

    void buildCymbalStyle() noexcept {
        if (built) return;
        range.build([](double v) { return v - swingLowerEdgeCymbal(v); });
        built = true;
    }
};

// The envelope seen by one swing VCA, advanced by the voice at control rate
// and ramped linearly in between: envelope volts and the range D(env).
struct SwingSupply {
    double env = 0.0, range = 0.0;
    double dEnv = 0.0, dRange = 0.0;

    void reset() noexcept { *this = SwingSupply{}; }
    // Head for `envNext` over the next 1/invSteps ticks.
    void aim(double envNext, const SwingRangeTable& table, double invSteps) noexcept {
        dEnv = (envNext - env) * invSteps;
        dRange = (table(envNext) - range) * invSteps;
    }
    void step() noexcept {
        env += dEnv;
        range += dRange;
    }
};

// Mechanism 1: the base clamp. One per coupling capacitor signal; VCAs that
// are fed from the same band-pass through identical base networks share it
// (their base voltages are the same waveform).
//
// The base current rises in spikes much narrower than a sample (only near the
// positive peaks of the drive, where the junction recharges the coupling
// capacitor), so sampling e^w at the sample instants would miss most of them.
// Instead tick() returns the MEAN of e^w over the sample, which charge balance
// on the coupling capacitor gives exactly:
//     mean(e^w) = 1 + tau0/dt * (dVin/VT - dw).
// The collector nodes integrate that current, so the result converges as the
// rate goes up instead of depending on where the samples happen to fall.
//
// Two lanes, so a voice with two coupling capacitors (the cymbal) runs both
// for the price of one; SwingBaseClamp below is one lane of it. e^x and ln x
// are the short polynomials of MetalSimd.h. Their errors cannot accumulate:
// the ln y that enters the drive is the one stored for the next sample, so
// the charge telescopes exactly over any stretch of samples.
class SwingBaseClamp2 {
public:
    static constexpr double kBiasAmps = (15.0 - 0.65) / 2.0e6;  // two 1 MOhm from +15 V
    static constexpr double kCouplingFarads = 22.0e-9;
    static constexpr double kMaxStep = 14.0;  // per-sample input step, in VT (keeps y in range)

    // timeScale < 1 speeds the stage up (tune).
    void prepare(int lane, double rate, double timeScale) noexcept {
        const double tau0 = kCouplingFarads * kThermalVolts / kBiasAmps * timeScale;
        const double r = std::exp(-0.5 / (tau0 * rate));
        a_[lane] = 1.0 - r;
        b_[lane] = r * (1.0 - r);
        r2_[lane] = r * r;
        tauOverDt_[lane] = tau0 * rate;
    }
    void reset() noexcept {
        y_ = splat(1.0);
        lnY_ = splat(0.0);
        lastIn_ = splat(0.0);
        stepDv_ = splat(0.0);
        stepExp_ = splat(1.0);
    }

    // in: AC drive at the coupling capacitor, volts. Returns the mean of e^w
    // over this sample: the base (and wanted collector) current relative to
    // its quiescent value.
    v2d tick(v2d in) noexcept {
        const v2d dv = vmin(vmax((in - lastIn_) * MW808_V2(1.0 / kThermalVolts), MW808_V2(-kMaxStep)), MW808_V2(kMaxStep));
        lastIn_ = in;
        // y = e^-w: relax towards 1 with tau0 for half a sample, take the
        // input step (y *= e^-dv), relax for the other half. Exact for a
        // piecewise-constant input; folded into one expression.
        const v2d y = vmin(a_ + fastExp(-dv) * (b_ + r2_ * y_), MW808_V2(1.0e30));
        const v2d lnY = fastLog(y);
        const v2d drive = MW808_V2(1.0) + tauOverDt_ * (dv + lnY - lnY_);
        y_ = y;
        lnY_ = lnY;
        return vmax(drive, MW808_V2(0.0));  // branch-free: the drive's sign flips every cycle
    }

    // The same, software-pipelined: returns the drive for the input given on
    // the PREVIOUS call (one sample late). The input step's e^-dv for the
    // next sample is computed alongside the current sample's y and ln y, so
    // the two long dependency chains overlap. Bit-identical to tick(), delayed.
    v2d tickPipelined(v2d in) noexcept {
        const v2d y = vmin(a_ + stepExp_ * (b_ + r2_ * y_), MW808_V2(1.0e30));
        const v2d lnY = fastLog(y);
        const v2d drive = MW808_V2(1.0) + tauOverDt_ * (stepDv_ + lnY - lnY_);
        y_ = y;
        lnY_ = lnY;
        stepDv_ = vmin(vmax((in - lastIn_) * MW808_V2(1.0 / kThermalVolts), MW808_V2(-kMaxStep)), MW808_V2(kMaxStep));
        stepExp_ = fastExp(-stepDv_);
        lastIn_ = in;
        return vmax(drive, MW808_V2(0.0));
    }

private:
    v2d a_ = splat(0.1), b_ = splat(0.09), r2_ = splat(0.81), tauOverDt_ = splat(8.0);
    v2d y_ = splat(1.0), lnY_ = splat(0.0), lastIn_ = splat(0.0);
    v2d stepDv_ = splat(0.0), stepExp_ = splat(1.0);  // tickPipelined: the pending input step
};

// One base clamp (both lanes of SwingBaseClamp2 carry the same signal).
class SwingBaseClamp {
public:
    static constexpr double kBiasAmps = SwingBaseClamp2::kBiasAmps;
    static constexpr double kCouplingFarads = SwingBaseClamp2::kCouplingFarads;

    void prepare(double rate, double timeScale) noexcept {
        pair_.prepare(0, rate, timeScale);
        pair_.prepare(1, rate, timeScale);
    }
    void reset() noexcept { pair_.reset(); }
    double tick(double in) noexcept { return pair_.tick(splat(in))[0]; }

private:
    SwingBaseClamp2 pair_;
};

// Mechanisms 2 and 3: the collector node of one VCA.
//
// In x = Venv - Vc (how far the node sits below the envelope):
//     tauC dx/dt = -x + K e^w,   K = beta Ib0 Rc,   0 <= x <= D(Venv).
// At rest e^w = 1 and K (~50 V) is far beyond D, so the transistor is
// saturated: the node sits on the lower edge, and the sound is made by the
// stretches where the drive pulls the base down, the transistor lets go and
// the node recovers towards the envelope through Rc.
//
// Integration: the collector current is held at its exact per-sample mean
// (from the base clamp) and the linear ODE is solved exactly over the sample,
// then limited at the edge. While the mean current can hold the node down
// (K e^w >= x) the node stays on the edge for the whole sample. (The earlier
// "impulse at mid-sample, then recover half a sample" form let the node creep
// off the edge during every saturated sample: a level loss of 2.5 dB at a
// 44.1 kHz internal rate that shrank as the rate went up.)
class SwingCollector {
public:
    static constexpr double kBeta = 200.0;  // fitted (typical small-signal NPN)

    // collectorOhms: Rc. shuntFarads: capacitance at the collector node (> 0).
    void prepare(double rate, double collectorOhms, double shuntFarads, double timeScale) noexcept {
        const double tauC = std::max(collectorOhms * shuntFarads * timeScale, 1.0e-7);
        recover_ = std::exp(-1.0 / (tauC * rate));
        pull_ = kBeta * SwingBaseClamp::kBiasAmps * collectorOhms;
    }
    void reset() noexcept { vc_ = 0.0; }

    // drive: mean e^w over the sample, from the base clamp. Returns the
    // collector-side node voltage, between supply.env - supply.range and supply.env.
    double tick(double drive, const SwingSupply& supply) noexcept {
        // The diode stops the node from sitting above the envelope.
        const double below = std::max(supply.env - vc_, 0.0);
        const double target = pull_ * drive;  // where the node would settle at this current
        const double x = target + (below - target) * recover_;
        vc_ = supply.env - std::min(x, supply.range);  // saturation at the lower edge
        return vc_;
    }

    double collectorVolts() const noexcept { return vc_; }

private:
    double recover_ = 0.8;
    double pull_ = 50.0;
    double vc_ = 0.0;
};

// Two collector nodes with their supplies, one per lane (each lane does the
// arithmetic of SwingCollector / SwingSupply exactly).
class SwingCollector2 {
public:
    void prepare(int lane, double rate, double collectorOhms, double shuntFarads, double timeScale) noexcept {
        const double tauC = std::max(collectorOhms * shuntFarads * timeScale, 1.0e-7);
        recover_[lane] = std::exp(-1.0 / (tauC * rate));
        pull_[lane] = SwingCollector::kBeta * SwingBaseClamp::kBiasAmps * collectorOhms;
    }
    void reset() noexcept {
        vc_ = splat(0.0);
        env_ = range_ = dEnv_ = dRange_ = splat(0.0);
    }
    // The supplies, as SwingSupply::aim / step.
    void aim(int lane, double envNext, const SwingRangeTable& table, double invSteps) noexcept {
        dEnv_[lane] = (envNext - env_[lane]) * invSteps;
        dRange_[lane] = (table(envNext) - range_[lane]) * invSteps;
    }
    v2d tick(v2d drive) noexcept {
        env_ += dEnv_;
        range_ += dRange_;
        const v2d below = vmax(env_ - vc_, MW808_V2(0.0));
        const v2d target = pull_ * drive;
        const v2d x = target + (below - target) * recover_;
        vc_ = env_ - vmin(x, range_);
        return vc_;
    }
    v2d collectorVolts() const noexcept { return vc_; }
    v2d envelopeVolts() const noexcept { return env_; }

private:
    v2d recover_ = splat(0.8), pull_ = splat(50.0);
    v2d vc_ = splat(0.0), env_ = splat(0.0), range_ = splat(0.0), dEnv_ = splat(0.0), dRange_ = splat(0.0);
};

}  // namespace mw808::dsp

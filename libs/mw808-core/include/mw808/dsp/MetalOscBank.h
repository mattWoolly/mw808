// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The six free-running Schmitt-trigger square oscillators behind the cymbal,
// the hi-hats and the cowbell (research/02 §1).
//
// Circuit: six RC relaxation oscillators around one hex Schmitt inverter on a
// 5 V rail. f = 2.0699 / (R C); the duty cycle, 47.98 %, comes from the
// inverter thresholds alone and is the same for all six. Nominal values:
//
//   index  R            C        f (Hz)
//     0    560k         18 nF    205.3
//     1    680k         10 nF    304.4
//     2    560k         10 nF    369.6
//     3    220k         18 nF    522.7
//     4    150k + trim  22 nF    540    (factory trim, cowbell low tone)
//     5    100k + trim  18 nF    800    (factory trim, cowbell high tone)
//
// Model:
//  * Phases are 64-bit integers: phase(t) = start + increment * t, exactly.
//    The bank can therefore be moved over any number of samples in one step
//    (`advance`, `rewind`) while a voice sleeps, and two banks given the same
//    calls stay bit-identical forever.
//  * Triggers never touch the bank. Start phases are fixed constants, so every
//    bank instance with the same seed and tune is the same physical bank.
//  * Edges are band-limited with a two-sample polyBLEP at the fractional edge
//    position (research/05 §3.2).
//  * Event-driven: between edges the output is a constant, so a tick is one
//    counter compare; the per-edge work happens on the ~10 % of samples that
//    sit next to an edge.
//  * Unit variance: the four fixed oscillators move with their R (5 %) and C
//    (20 %) parts; the two trimmed ones only by a trim error (1.5 %, fitted:
//    the factory sets them against a period on a scope).
//
// Output: each oscillator swings -1..+1 (the 0..5 V square, AC-centred but
// keeping the small DC offset of the 47.98 % duty).

#pragma once

#include <array>
#include <cstdint>

#include "mw808/dsp/MetalCommon.h"

namespace mw808::dsp {

class MetalOscBank {
public:
    static constexpr int kNumOsc = 6;
    static constexpr double kDuty = 0.4798;
    static constexpr std::array<double, kNumOsc> kNominalHz{205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
    static constexpr int kCowbellLow = 4;
    static constexpr int kCowbellHigh = 5;

    // `rate` is the rate tick() runs at (the voice's internal, oversampled rate).
    void prepare(double rate, std::uint32_t unitSeed, float variance) noexcept {
        rate_ = rate;
        for (std::size_t k = 0; k < kN; ++k) {
            const auto id = static_cast<std::uint32_t>(k);
            double scale;
            if (static_cast<int>(k) >= kCowbellLow) {
                scale = componentScale(unitSeed, metalparts::kOscTrim + id, 0.015f, variance);
            } else {
                const double r = componentScale(unitSeed, metalparts::kOscR + id, 0.05f, variance);
                const double c = componentScale(unitSeed, metalparts::kOscC + id, 0.20f, variance);
                scale = 1.0 / (r * c);
            }
            unitHz_[k] = kNominalHz[k] * scale;
        }
        reset();
    }

    // Start-of-time phases. Not called on trigger, only on prepare/panic.
    void reset() noexcept {
        for (std::size_t k = 0; k < kN; ++k) {
            const auto id = static_cast<std::uint32_t>(k);
            const std::uint64_t hi = hash32(0x5eed0b0cU, id);
            const std::uint64_t lo = hash32(0x0b0c5eedU, id);
            start_[k] = (hi << 32) | lo;
        }
        tick_ = 0;
        setIncrements();
        reschedule();
    }

    // Frequency ratio applied to all six oscillators (2^(semitones/12)).
    void setTune(double ratio) noexcept {
        if (ratio == tune_) return;
        rebase();
        tune_ = ratio;
        setIncrements();
        reschedule();
    }

    double frequencyHz(int i) const noexcept { return unitHz_[static_cast<std::size_t>(i)] * tune_; }
    std::uint64_t phase(int i) const noexcept {
        const auto k = static_cast<std::size_t>(i);
        return start_[k] + inc_[k] * tick_;
    }

    // Let `samples` ticks pass without producing output (exact).
    void advance(std::uint64_t samples) noexcept {
        tick_ += samples;
        reschedule();
    }

    // Step back `samples` ticks (exact). A waking voice uses this to re-run
    // the last few milliseconds through its filters, so it wakes with the
    // band-pass state it would have had if it had never slept.
    void rewind(std::uint64_t samples) noexcept {
        tick_ -= samples;
        reschedule();
    }

    // Equal-weight sum of all six (range -6..+6).
    double tickSum() noexcept {
        step();
        return sum_;
    }

    // The two trimmed oscillators on their own (cowbell); the other four keep running.
    void tickCowbell(double& low, double& high) noexcept {
        step();
        low = level_[static_cast<std::size_t>(kCowbellLow)] + corr_[static_cast<std::size_t>(kCowbellLow)];
        high = level_[static_cast<std::size_t>(kCowbellHigh)] + corr_[static_cast<std::size_t>(kCowbellHigh)];
    }

private:
    static constexpr std::size_t kN = static_cast<std::size_t>(kNumOsc);
    static constexpr double kTwoPow64 = 18446744073709551616.0;
    static constexpr std::uint64_t kDutyPhase = static_cast<std::uint64_t>(kDuty * kTwoPow64);

    void setIncrements() noexcept {
        for (std::size_t k = 0; k < kN; ++k) {
            const double cyclesPerSample = clamp(unitHz_[k] * tune_ / rate_, 1.0e-9, 0.2);
            inc_[k] = static_cast<std::uint64_t>(cyclesPerSample * kTwoPow64);
            invInc_[k] = 1.0 / static_cast<double>(inc_[k]);
        }
    }

    // Fold the elapsed ticks into the start phases (before an increment change).
    void rebase() noexcept {
        for (std::size_t k = 0; k < kN; ++k) start_[k] += inc_[k] * tick_;
        tick_ = 0;
    }

    // Phase distance from p to the next edge, and that edge's direction.
    static std::uint64_t toNextEdge(std::uint64_t p, double& direction) noexcept {
        if (p < kDutyPhase) {
            direction = -1.0;  // falling edge at the duty point
            return kDutyPhase - p;
        }
        direction = 1.0;  // rising edge at the wrap
        return 0U - p;
    }

    // Work out levels and the next edge of every oscillator from the phases.
    // An edge that falls before the next sample is prepared as a pending
    // "after the edge" correction, so the next tick is exactly what a bank
    // that had been ticking all along would produce.
    void reschedule() noexcept {
        pending_ = false;
        dirty_ = false;
        for (std::size_t k = 0; k < kN; ++k) {
            const std::uint64_t p = start_[k] + inc_[k] * tick_;
            level_[k] = p < kDutyPhase ? 1.0 : -1.0;
            corr_[k] = 0.0;
            flip_[k] = 0.0;
            later_[k] = 0.0;
            double dir = 0.0;
            const std::uint64_t d = toNextEdge(p, dir);
            const std::uint64_t pre = (d - 1U) / inc_[k];  // ticks until the last sample before the edge
            if (pre == 0U) {
                const double tb = static_cast<double>(d) * invInc_[k];
                flip_[k] = 2.0 * dir;
                later_[k] = -dir * tb * tb;
                pending_ = true;
                // and the edge after that one
                const std::uint64_t pNext = p + inc_[k];
                const std::uint64_t dNext = toNextEdge(pNext, dir);
                event_[k] = tick_ + 1U + (dNext - 1U) / inc_[k];
            } else {
                event_[k] = tick_ + pre;
            }
        }
        refreshNext();
        refreshSum();
    }

    void refreshNext() noexcept {
        next_ = event_[0];
        for (std::size_t k = 1; k < kN; ++k) next_ = event_[k] - tick_ < next_ - tick_ ? event_[k] : next_;
    }

    void refreshSum() noexcept {
        double s = 0.0;
        for (std::size_t k = 0; k < kN; ++k) s += level_[k] + corr_[k];
        sum_ = s;
    }

    void step() noexcept {
        ++tick_;
        if (!dirty_ && !pending_ && tick_ != next_) return;  // nothing near an edge
        slowStep();
    }

    void slowStep() noexcept {
        // Samples just after an edge: apply the flips and corrections queued
        // on the sample before; otherwise clear last sample's corrections.
        for (std::size_t k = 0; k < kN; ++k) {
            level_[k] += flip_[k];
            corr_[k] = later_[k];
            flip_[k] = 0.0;
            later_[k] = 0.0;
        }
        dirty_ = pending_;  // corrections now in corr_ must be cleared next tick
        pending_ = false;
        if (tick_ == next_) {
            for (std::size_t k = 0; k < kN; ++k) {
                if (event_[k] != tick_) continue;
                // Last sample before an edge of oscillator k.
                const std::uint64_t p = start_[k] + inc_[k] * tick_;
                double dir = 0.0;
                const std::uint64_t d = toNextEdge(p, dir);
                const double tb = static_cast<double>(d) * invInc_[k];  // samples to the edge, (0, 1]
                corr_[k] += dir * (1.0 - tb) * (1.0 - tb);
                flip_[k] = 2.0 * dir;
                later_[k] = -dir * tb * tb;
                pending_ = true;
                dirty_ = true;
                const std::uint64_t pNext = p + inc_[k];
                const std::uint64_t dNext = toNextEdge(pNext, dir);
                event_[k] = tick_ + 1U + (dNext - 1U) / inc_[k];
            }
            refreshNext();
        }
        refreshSum();
    }

    double rate_ = 96000.0;
    double tune_ = 1.0;
    std::array<double, kN> unitHz_{kNominalHz};
    std::array<std::uint64_t, kN> start_{};
    std::array<std::uint64_t, kN> inc_{1U, 1U, 1U, 1U, 1U, 1U};
    std::array<double, kN> invInc_{};
    std::array<std::uint64_t, kN> event_{};
    std::array<double, kN> level_{};
    std::array<double, kN> corr_{};   // polyBLEP residual of the current sample
    std::array<double, kN> flip_{};   // level change taking effect on the next sample
    std::array<double, kN> later_{};  // polyBLEP residual of the next sample
    std::uint64_t tick_ = 0;
    std::uint64_t next_ = 0;
    double sum_ = 0.0;
    bool pending_ = false;
    bool dirty_ = false;
};

// The two-sample polyBLEP rounds each edge with a triangular kernel, i.e. it
// droops as sinc^2(f/fs): -0.65 dB at 7.1 kHz when ticking at 48 kHz. The
// swing VCAs behind the band-passes are strongly level-dependent, so that
// droop would make the voices duller and quieter at low rates. This 3-tap
// symmetric filter cancels it to second order (flat within 0.05 dB up to
// fs/8) at the cost of one tick of delay.
class BlepDroopCompensator {
public:
    void reset() noexcept { x1_ = x2_ = 0.0; }
    double tick(double x) noexcept {
        const double y = (7.0 / 6.0) * x1_ - (1.0 / 12.0) * (x + x2_);
        x2_ = x1_;
        x1_ = x;
        return y;
    }

private:
    double x1_ = 0.0, x2_ = 0.0;
};

}  // namespace mw808::dsp

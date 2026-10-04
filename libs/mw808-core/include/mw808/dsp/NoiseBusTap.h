// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Sample-rate-independent taps of the shared noise bus for the bridged-T
// voices. The bus (dsp::WhiteNoise) is unit-RMS white noise at the host rate,
// so its spectral density falls as the rate rises; a voice that wants the same
// audible noise at every rate has to undo that.

#pragma once

#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Math.h"

namespace mw808::dsp {

// Linear consumers (noise through filters and a linear VCA): scale the bus so
// its density equals that of unit-RMS white noise at 48 kHz.
inline double noiseDensityGain(double sampleRate) noexcept {
    return std::sqrt(sampleRate / 48000.0);
}

// Nonlinear consumers (noise switching a transistor): the statistics of the
// signal itself must not depend on the rate, so band-limit the bus to a fixed
// bandwidth and renormalise it to unit RMS.
class NoiseBusTap {
public:
    void prepare(double sampleRate, double bandwidthHz = 14000.0) noexcept {
        lp_.setFreqQ(std::min(bandwidthHz, 0.45 * sampleRate), 0.70710678, sampleRate);
        // Unit-RMS white in -> RMS out is the root of the impulse-response
        // energy. A fixed short loop: prepare() must not allocate.
        lp_.reset();
        double energy = 0.0;
        for (int i = 0; i < 512; ++i) {
            lp_.tick(i == 0 ? 1.0 : 0.0);
            energy += lp_.lowpass() * lp_.lowpass();
        }
        gain_ = 1.0 / std::sqrt(std::max(energy, 1.0e-12));
        lp_.reset();
    }
    void reset() noexcept { lp_.reset(); }
    double tick(double white) noexcept {
        lp_.tick(white);
        return lp_.lowpass() * gain_;
    }

private:
    Svf lp_;
    double gain_ = 1.0;
};

}  // namespace mw808::dsp

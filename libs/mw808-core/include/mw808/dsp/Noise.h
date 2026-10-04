// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The machine's single noise generator. One instance feeds every noise-using
// voice, as in the original, so their noise is correlated.

#pragma once

#include <cstdint>

namespace mw808::dsp {

// White noise, approximately Gaussian (sum of four uniforms), unit RMS.
// xorshift32 core: deterministic for a given seed, never yields zero state.
class WhiteNoise {
public:
    void seed(std::uint32_t s) noexcept { state_ = s != 0U ? s : 0x1234abcdU; }

    float next() noexcept {
        // Sum of 4 uniforms in [-1,1): variance 4/3, so scale by sqrt(3)/2.
        const float sum = uniform() + uniform() + uniform() + uniform();
        return sum * 0.8660254f;
    }

    void fill(float* out, int n) noexcept {
        for (int i = 0; i < n; ++i) out[i] = next();
    }

private:
    float uniform() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(static_cast<std::int32_t>(state_)) * (1.0f / 2147483648.0f);
    }

    std::uint32_t state_ = 0x1234abcdU;
};

}  // namespace mw808::dsp

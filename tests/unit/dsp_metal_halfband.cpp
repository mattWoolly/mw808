// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The half-band decimators of the metallic voices: pass-band flatness,
// stop-band rejection (as what actually folds into the output) and latency.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "MetalTestUtil.h"
#include "mw808/dsp/Halfband.h"

using namespace mw808::dsp;
using Catch::Matchers::WithinAbs;

namespace {

// Gain (dB) from a sine at `freq` (at the input rate fsOut * factor) to the
// output component it lands on after decimation.
double gainDb(int factor, double fsOut, double freq, double* delaySamples = nullptr) {
    const double fsIn = fsOut * factor;
    MetalDecimator dec;
    dec.prepare(factor);
    const std::size_t n = 16384, skip = 4096;
    std::vector<float> out(n + skip);
    std::size_t t = 0;
    for (std::size_t i = 0; i < n + skip; ++i) {
        double hi[4];
        for (int k = 0; k < factor; ++k, ++t) hi[k] = std::sin(kTwoPi * freq * static_cast<double>(t) / fsIn);
        out[i] = static_cast<float>(dec.process(hi));
    }
    // where the tone lands after folding
    double folded = std::fmod(freq, fsOut);
    if (folded > fsOut / 2) folded = fsOut - folded;
    const double amp = metaltest::toneAmplitude(out, fsOut, folded, skip, n);
    if (delaySamples != nullptr) {
        // phase of the output against the input sine sampled at the output instants
        std::complex<double> acc(0.0, 0.0);
        for (std::size_t i = skip; i < skip + n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i - skip) / static_cast<double>(n));
            // the output sample i corresponds to the newest input sample, index factor*i + factor - 1
            const double ph = kTwoPi * freq * static_cast<double>(factor * i + static_cast<std::size_t>(factor) - 1) / fsIn;
            acc += static_cast<double>(out[i]) * w * std::complex<double>(std::sin(ph), std::cos(ph));
        }
        const double lag = -std::atan2(acc.imag(), acc.real());  // radians of delay
        *delaySamples = lag / (kTwoPi * freq / fsOut);
    }
    return 20.0 * std::log10(std::max(amp, 1.0e-12));
}

}  // namespace

TEST_CASE("Half-band 2x: flat to 18 kHz, stop-band below -70 dB (44.1 kHz output)", "[metal][halfband]") {
    const double fs = 44100.0;
    double ripple = 0.0;
    for (double f : {50.0, 400.0, 1000.0, 4000.0, 8000.0, 12000.0, 15000.0, 17000.0, 18000.0})
        ripple = std::max(ripple, std::abs(gainDb(2, fs, f)));
    double worstStop = -300.0;
    for (double f = 26100.0; f <= 44000.0; f += 357.0) worstStop = std::max(worstStop, gainDb(2, fs, f));
    const double at20k = gainDb(2, fs, 20000.0);
    std::printf("[half-band 2x @44.1k] pass-band deviation to 18 kHz: %.5f dB, at 20 kHz: %.3f dB, worst stop-band (26.1-44.1 kHz): %.1f dB\n",
                ripple, at20k, worstStop);
    REQUIRE(ripple < 0.01);
    REQUIRE(worstStop < -70.0);
    REQUIRE(at20k > -0.5);
}

TEST_CASE("Half-band 4x: flat to 18 kHz, everything that folds is below -70 dB", "[metal][halfband]") {
    const double fs = 44100.0;
    double ripple = 0.0;
    for (double f : {50.0, 1000.0, 8000.0, 15000.0, 18000.0}) ripple = std::max(ripple, std::abs(gainDb(4, fs, f)));
    double worstStop = -300.0;
    for (double f = 26100.0; f <= 88000.0; f += 613.0) worstStop = std::max(worstStop, gainDb(4, fs, f));
    std::printf("[half-band 4x @44.1k] pass-band deviation to 18 kHz: %.5f dB, worst stop-band (26.1-88.2 kHz): %.1f dB\n",
                ripple, worstStop);
    REQUIRE(ripple < 0.01);
    REQUIRE(worstStop < -70.0);
}

TEST_CASE("Half-band at 48 kHz output keeps 20 kHz", "[metal][halfband]") {
    REQUIRE(std::abs(gainDb(2, 48000.0, 20000.0)) < 0.01);
    REQUIRE(std::abs(gainDb(4, 48000.0, 20000.0)) < 0.01);
    REQUIRE(gainDb(2, 48000.0, 30000.0) < -70.0);
}

TEST_CASE("Decimator latency is small and as reported", "[metal][halfband]") {
    for (int factor : {1, 2, 4}) {
        MetalDecimator dec;
        dec.prepare(factor);
        double lowDelay = 0.0, tenK = 0.0;
        (void)gainDb(factor, 48000.0, 500.0, &lowDelay);
        (void)gainDb(factor, 48000.0, 10000.0, &tenK);
        std::printf("[decimator latency] factor %d: %.3f samples at 500 Hz, %.3f at 10 kHz (reported %.3f)\n", factor,
                    lowDelay, tenK, dec.latencySamples());
        REQUIRE_THAT(lowDelay, WithinAbs(dec.latencySamples(), 0.05));
        REQUIRE(lowDelay < 2.5);
    }
}

TEST_CASE("Decimator factor 1 is the identity and reset clears it", "[metal][halfband]") {
    MetalDecimator dec;
    dec.prepare(1);
    double x = 0.37;
    REQUIRE(dec.process(&x) == 0.37);
    dec.prepare(2);
    double pair[2] = {1.0, -1.0};
    const double first = dec.process(pair);
    (void)dec.process(pair);
    dec.reset();
    REQUIRE(dec.process(pair) == first);
}

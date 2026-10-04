// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The six-oscillator bank: frequencies, duty, exact sleeping, tune, unit
// variance, equivalence with a per-sample polyBLEP reference, and aliasing.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "MetalTestUtil.h"
#include "mw808/dsp/Halfband.h"
#include "mw808/dsp/MetalOscBank.h"

using namespace mw808::dsp;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kTwoPow64 = 18446744073709551616.0;

// Straightforward per-sample polyBLEP square, the textbook form the bank's
// event-driven implementation must reproduce.
double referenceSquare(std::uint64_t p, std::uint64_t inc) {
    const auto duty = static_cast<std::uint64_t>(MetalOscBank::kDuty * kTwoPow64);
    const double inv = 1.0 / static_cast<double>(inc);
    double y = p < duty ? 1.0 : -1.0;
    auto after = [](double t) { return t + t - t * t - 1.0; };
    auto before = [](double t) { return (1.0 - t) * (1.0 - t); };
    if (p < inc) y += after(static_cast<double>(p) * inv);
    else if (p > 0U - inc) y += before(static_cast<double>(0U - p) * inv);
    const std::uint64_t q = p - duty;
    if (q < inc) y -= after(static_cast<double>(q) * inv);
    else if (q > 0U - inc) y -= before(static_cast<double>(0U - q) * inv);
    return y;
}

std::array<std::uint64_t, 6> incrementsOf(MetalOscBank bank) {
    std::array<std::uint64_t, 6> before{}, inc{};
    for (int i = 0; i < 6; ++i) before[static_cast<std::size_t>(i)] = bank.phase(i);
    (void)bank.tickSum();
    for (int i = 0; i < 6; ++i) inc[static_cast<std::size_t>(i)] = bank.phase(i) - before[static_cast<std::size_t>(i)];
    return inc;
}

}  // namespace

TEST_CASE("Bank output equals the per-sample polyBLEP reference", "[metal][bank]") {
    for (double rate : {44100.0, 96000.0, 192000.0}) {
        for (double tune : {1.0, 2.0, 0.5}) {
            MetalOscBank bank;
            bank.prepare(rate, 3U, 1.0f);
            bank.setTune(tune);
            const auto inc = incrementsOf(bank);
            double worst = 0.0;
            for (int n = 0; n < 200000; ++n) {
                const double got = bank.tickSum();
                double want = 0.0;
                for (int i = 0; i < 6; ++i) want += referenceSquare(bank.phase(i), inc[static_cast<std::size_t>(i)]);
                worst = std::max(worst, std::abs(got - want));
            }
            INFO("rate " << rate << " tune " << tune);
            REQUIRE(worst < 1.0e-12);
        }
    }
}

TEST_CASE("Bank cowbell pair equals the reference too", "[metal][bank]") {
    MetalOscBank bank;
    bank.prepare(96000.0, 0U, 0.0f);
    const auto inc = incrementsOf(bank);
    double worst = 0.0;
    for (int n = 0; n < 100000; ++n) {
        double lo = 0.0, hi = 0.0;
        bank.tickCowbell(lo, hi);
        worst = std::max(worst, std::abs(lo - referenceSquare(bank.phase(4), inc[4])));
        worst = std::max(worst, std::abs(hi - referenceSquare(bank.phase(5), inc[5])));
    }
    REQUIRE(worst < 1.0e-12);
}

TEST_CASE("Bank frequencies and duty match the schematic at every rate", "[metal][bank]") {
    const double nominal[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        MetalOscBank bank;
        bank.prepare(rate, 0U, 0.0f);
        const auto inc = incrementsOf(bank);
        for (int i = 0; i < 6; ++i) {
            const double hz = static_cast<double>(inc[static_cast<std::size_t>(i)]) / kTwoPow64 * rate;
            REQUIRE_THAT(hz, WithinRel(nominal[i], 1.0e-9));
            REQUIRE_THAT(bank.frequencyHz(i), WithinRel(nominal[i], 1.0e-12));
        }
        // Duty and pitch of the 800 Hz oscillator from its waveform.
        const int n = static_cast<int>(rate * 2.0);
        double high = 0.0, lo = 0.0, hi = 0.0, prev = 0.0, first = -1.0, last = -1.0;
        int rises = 0;
        for (int k = 0; k < n; ++k) {
            bank.tickCowbell(lo, hi);
            high += 0.5 * (hi + 1.0);
            if (k > 0 && prev < 0.0 && hi >= 0.0) {
                const double t = (k - 1) + prev / (prev - hi);
                if (first < 0.0) first = t;
                last = t;
                ++rises;
            }
            prev = hi;
        }
        REQUIRE_THAT(high / n, WithinAbs(0.4798, 0.0005));
        REQUIRE_THAT((rises - 1) * rate / (last - first), WithinRel(800.0, 1.0e-4));
    }
}

TEST_CASE("Bank can sleep: advance and rewind are exact", "[metal][bank]") {
    MetalOscBank ticking, sleeping;
    ticking.prepare(96000.0, 5U, 1.0f);
    sleeping.prepare(96000.0, 5U, 1.0f);
    for (int round = 0; round < 20; ++round) {
        const int n = 1 + round * 7919;
        for (int i = 0; i < n; ++i) (void)ticking.tickSum();
        sleeping.advance(static_cast<std::uint64_t>(n));
        for (int i = 0; i < 6; ++i) REQUIRE(ticking.phase(i) == sleeping.phase(i));
        // and they continue identically, bit for bit
        for (int i = 0; i < 3000; ++i) REQUIRE(ticking.tickSum() == sleeping.tickSum());
    }
    // rewind undoes advance exactly
    MetalOscBank a, b;
    a.prepare(88200.0, 1U, 0.0f);
    b.prepare(88200.0, 1U, 0.0f);
    for (int i = 0; i < 12345; ++i) {
        (void)a.tickSum();
        (void)b.tickSum();
    }
    b.advance(777U);
    b.rewind(777U);
    for (int i = 0; i < 6; ++i) REQUIRE(a.phase(i) == b.phase(i));
    for (int i = 0; i < 3000; ++i) REQUIRE(a.tickSum() == b.tickSum());
}

TEST_CASE("Bank tune scales all six, and keeps phase continuity", "[metal][bank]") {
    MetalOscBank bank;
    bank.prepare(96000.0, 0U, 0.0f);
    for (int i = 0; i < 1000; ++i) (void)bank.tickSum();
    std::array<std::uint64_t, 6> before{};
    for (int i = 0; i < 6; ++i) before[static_cast<std::size_t>(i)] = bank.phase(i);
    bank.setTune(2.0);
    for (int i = 0; i < 6; ++i) {
        REQUIRE(bank.phase(i) == before[static_cast<std::size_t>(i)]);
        REQUIRE_THAT(bank.frequencyHz(i), WithinRel(2.0 * MetalOscBank::kNominalHz[static_cast<std::size_t>(i)], 1.0e-12));
    }
    bank.reset();  // reset restores the start phases but keeps the tune
    REQUIRE_THAT(bank.frequencyHz(5), WithinRel(1600.0, 1.0e-12));
}

TEST_CASE("Bank unit variance detunes within tolerance; same seed, same bank", "[metal][bank]") {
    MetalOscBank nominal, a, a2, b, zero;
    nominal.prepare(96000.0, 0U, 0.0f);
    a.prepare(96000.0, 11U, 1.0f);
    a2.prepare(96000.0, 11U, 1.0f);
    b.prepare(96000.0, 12U, 1.0f);
    zero.prepare(96000.0, 11U, 0.0f);
    bool anyDifferent = false;
    for (int i = 0; i < 6; ++i) {
        const double ratio = a.frequencyHz(i) / nominal.frequencyHz(i);
        // fixed oscillators: 1/(R C) with 5 % and 20 % parts; trimmed pair: 1.5 %
        const double bound = i < 4 ? 1.0 / (0.95 * 0.80) : 1.015;
        REQUIRE(ratio <= bound + 1e-9);
        REQUIRE(ratio >= (i < 4 ? 1.0 / (1.05 * 1.20) : 0.985) - 1e-9);
        REQUIRE(a.frequencyHz(i) == a2.frequencyHz(i));
        REQUIRE(zero.frequencyHz(i) == nominal.frequencyHz(i));
        anyDifferent = anyDifferent || a.frequencyHz(i) != b.frequencyHz(i);
        REQUIRE(a.phase(i) == nominal.phase(i));  // start phases do not depend on the seed
    }
    REQUIRE(anyDifferent);
}

// Alias-to-signal ratio of the bare bank (after decimation to 48 kHz):
// everything that is not on a harmonic of one of the six oscillators.
TEST_CASE("Bank aliasing at 48 kHz for oversample 1, 2 and 4", "[metal][bank][alias]") {
    const double fs = 48000.0;
    const std::size_t n = 1U << 17;
    struct Band { double lo, hi; };
    const Band bands[3] = {{2000.0, 5000.0}, {5000.0, 9000.0}, {9000.0, 16000.0}};
    double asr[3][3] = {};
    const int factors[3] = {1, 2, 4};
    for (int f = 0; f < 3; ++f) {
        const int os = factors[f];
        MetalOscBank bank;
        bank.prepare(fs * os, 0U, 0.0f);
        MetalDecimator dec;
        dec.prepare(os);
        std::vector<std::complex<double>> buf(n);
        for (int warm = 0; warm < 1000; ++warm) {
            double hi[4];
            for (int k = 0; k < os; ++k) hi[k] = bank.tickSum();
            (void)dec.process(hi);
        }
        for (std::size_t i = 0; i < n; ++i) {
            double hi[4];
            for (int k = 0; k < os; ++k) hi[k] = bank.tickSum();
            // 4-term Blackman-Harris: side lobes below -92 dB
            const double x = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n);
            const double w = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
            buf[i] = dec.process(hi) * w;
        }
        metaltest::fft(buf);
        const double binHz = fs / static_cast<double>(n);
        std::vector<char> harmonic(n / 2 + 1, 0);
        for (int o = 0; o < 6; ++o) {
            const double f0 = bank.frequencyHz(o);
            for (int h = 1; h * f0 < fs / 2; ++h) {
                const auto centre = static_cast<std::ptrdiff_t>(std::lround(h * f0 / binHz));
                for (std::ptrdiff_t k = centre - 6; k <= centre + 6; ++k)
                    if (k >= 0 && k <= static_cast<std::ptrdiff_t>(n / 2)) harmonic[static_cast<std::size_t>(k)] = 1;
            }
        }
        for (int b = 0; b < 3; ++b) {
            double sig = 0.0, alias = 0.0;
            for (auto k = static_cast<std::size_t>(bands[b].lo / binHz); k < static_cast<std::size_t>(bands[b].hi / binHz); ++k)
                (harmonic[k] ? sig : alias) += std::norm(buf[k]);
            asr[f][b] = 10.0 * std::log10(alias / sig);
        }
        std::printf("[bank aliasing] 48 kHz, oversample %d: alias/signal %.1f dB (2-5 kHz), %.1f dB (5-9 kHz), %.1f dB (9-16 kHz)\n",
                    os, asr[f][0], asr[f][1], asr[f][2]);
    }
    // polyBLEP alone (research/05 §3.2 predicts about -67 / -47 / -28 dB)
    REQUIRE(asr[0][0] < -55.0);
    REQUIRE(asr[0][1] < -40.0);
    REQUIRE(asr[0][2] < -24.0);
    // polyBLEP + 2x (predicted -84 / -67 / -50 dB)
    REQUIRE(asr[1][0] < -70.0);
    REQUIRE(asr[1][1] < -58.0);
    REQUIRE(asr[1][2] < -44.0);
    // 4x is better again
    REQUIRE(asr[2][2] < asr[1][2] - 6.0);
}

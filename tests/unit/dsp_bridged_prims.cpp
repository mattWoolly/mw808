// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The primitives shared by the bridged-T voices: the resonator's rail clamp,
// the tabulated diode pair, and control-value sanitising.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <utility>

#include "mw808/dsp/BridgedT.h"
#include "mw808/dsp/TriggerPulse.h"

using Catch::Matchers::WithinRel;
namespace dsp = mw808::dsp;

TEST_CASE("BridgedT: the rails limit the op-amp output, pulse included", "[bridged][prims]") {
    // A 100 Hz resonator driven hard while its + input sits at x = 10 V: the
    // output Vout = x + u may not leave +-13.5 V, so u is held in
    // [-23.5, 3.5] V, not in +-13.5 V.
    dsp::BridgedT bt;
    const double fs = 48000.0, rail = 13.5, x = 10.0;
    bt.prepare(fs);
    const double w0 = dsp::kTwoPi * 100.0, a = w0, b = w0, d = 10.0;
    double hi = -1e9, lo = 1e9;
    for (int i = 0; i < 4800; ++i) {
        bt.tick(a, b, d, b * x * 50.0);
        bt.clampOutput(rail, x);
        hi = std::max(hi, x + bt.u());
        lo = std::min(lo, x + bt.u());
    }
    CHECK(hi <= rail + 1e-12);
    CHECK(lo >= -rail - 1e-12);
    CHECK(hi > rail - 1e-6);  // it does get there
}

TEST_CASE("DiodePairKnee: the table follows its series resistance, within the given tolerance", "[bridged][prims]") {
    dsp::DiodePairKnee exact, lazy;
    exact.set(0.03, 1e-6, 600.0);
    lazy.set(0.03, 1e-6, 600.0, 0.02);
    // Small moves keep the table...
    lazy.set(0.03, 1e-6, 610.0, 0.02);
    CHECK(lazy.secantConductance(2.0) == exact.secantConductance(2.0));
    // ...a larger one rebuilds it exactly.
    exact.set(0.03, 1e-6, 640.0);
    lazy.set(0.03, 1e-6, 640.0, 0.02);
    for (double v : {-3.0, -0.4, -0.05, 0.0, 0.01, 0.1, 0.3, 1.0, 5.0})
        CHECK(lazy.secantConductance(v) == exact.secantConductance(v));
    // Far beyond the knee it is the series resistor.
    CHECK_THAT(exact.secantConductance(50.0) * 50.0, WithinRel((50.0 - 0.03 * std::log(0.03 / 640.0 / 1e-6)) / 640.0, 1e-9));
}

TEST_CASE("saneParams: NaN falls back to stock, infinities are pulled in", "[bridged][prims]") {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const mw808::VoiceParams stock{0.0f, 0.5f, 0.5f, 0.5f};
    const auto a = dsp::saneParams({nan, nan, nan, nan}, stock);
    CHECK(a.tune == 0.0f);
    CHECK(a.decay == 0.5f);
    CHECK(a.tone == 0.5f);
    CHECK(a.shape == 0.5f);
    const auto b = dsp::saneParams({inf, -inf, inf, -inf}, stock);
    CHECK(std::isfinite(b.tune));
    CHECK(b.tune > 100.0f);
    CHECK(b.decay <= 0.0f);
    CHECK(b.tone >= 1.0f);
    CHECK(b.shape <= 0.0f);
    const mw808::VoiceParams ok{-7.5f, 0.25f, 0.75f, 1.0f};
    const auto c = dsp::saneParams(ok, stock);
    CHECK(c.tune == ok.tune);
    CHECK(c.decay == ok.decay);
    CHECK(c.tone == ok.tone);
    CHECK(c.shape == ok.shape);
}

TEST_CASE("saneSetup: the unit variance is clamped to 0..1, NaN means nominal parts", "[bridged][prims]") {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    mw808::VoiceSetup s;
    s.sampleRate = 96000.0;
    s.unitSeed = 42U;
    s.oversample = 2;
    for (const auto& [in, want] : {std::pair{nan, 0.0f}, {inf, 1.0f}, {-inf, 0.0f}, {-0.5f, 0.0f}, {7.0f, 1.0f}, {0.3f, 0.3f}}) {
        s.variance = in;
        const auto out = dsp::saneSetup(s);
        CHECK(out.variance == want);
        CHECK(out.sampleRate == s.sampleRate);
        CHECK(out.unitSeed == s.unitSeed);
        CHECK(out.oversample == s.oversample);
    }
}

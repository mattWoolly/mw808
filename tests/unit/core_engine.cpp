// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CoreTestUtil.h"

using namespace mw808;
using namespace coretest;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

// Render `total` samples in kBlock chunks.
void renderAll(Engine& e, Buses& buses, int total, int block = kBlock) {
    for (int pos = 0; pos < total; pos += block) e.render(buses.view(), pos, std::min(block, total - pos));
}

}  // namespace

TEST_CASE("Engine is exactly silent until something is triggered", "[core][engine]") {
    Engine e;
    e.prepare(kFs, kBlock);
    Buses buses(4096);
    renderAll(e, buses, 4096);
    REQUIRE(peak(buses.left()) == 0.0f);
    REQUIRE(peak(buses.right()) == 0.0f);
    REQUIRE_FALSE(e.anyVoiceActive());
}

TEST_CASE("Every voice sounds, stays bounded and returns to exact silence", "[core][engine]") {
    for (int v = 0; v < kNumVoices; ++v) {
        Engine e;
        e.prepare(kFs, kBlock);
        const int total = static_cast<int>(kFs * 12.0);
        Buses buses(total, 1);
        e.trigger(static_cast<VoiceId>(v), Hit::accented(1.0f));
        renderAll(e, buses, total);

        INFO("voice " << kVoiceInfo[static_cast<std::size_t>(v)].code);
        REQUIRE(allFinite(buses.left()));
        const float p = peak(buses.left());
        REQUIRE(p > 0.02f);
        REQUIRE(p < 0.99f);
        // Centre pan: both channels identical.
        REQUIRE(same(buses.left(), buses.right()));
        // Fully decayed and asleep by the end; the last second is exact zeros.
        REQUIRE_FALSE(e.anyVoiceActive());
        REQUIRE(peak(buses.left(), total - static_cast<int>(kFs), total) == 0.0f);
    }
}

TEST_CASE("render overwrites its range and nothing else", "[core][engine]") {
    Engine e;
    e.prepare(kFs, kBlock);
    Buses buses(1024);
    for (int b = 0; b < kNumBuses; ++b) {
        std::fill(buses.left(b).begin(), buses.left(b).end(), 7.0f);
        std::fill(buses.right(b).begin(), buses.right(b).end(), 7.0f);
    }
    e.render(buses.view(), 256, 512);
    for (int b = 0; b < kNumBuses; ++b) {
        REQUIRE(buses.left(b)[255] == 7.0f);
        REQUIRE(buses.left(b)[256] == 0.0f);
        REQUIRE(buses.right(b)[767] == 0.0f);
        REQUIRE(buses.right(b)[768] == 7.0f);
    }
}

TEST_CASE("Individual outputs take a voice out of the main mix", "[core][engine]") {
    EngineParams params;
    params.channel[SD].output = 3;

    SECTION("connected bus") {
        Engine e;
        e.prepare(kFs, kBlock);
        e.setParams(params);
        Buses buses(8192);
        e.trigger(SD, Hit::normal());
        renderAll(e, buses, 8192);
        REQUIRE(peak(buses.left(0)) == 0.0f);
        REQUIRE(peak(buses.left(3)) > 0.01f);
        REQUIRE(peak(buses.left(2)) == 0.0f);
    }
    SECTION("unconnected bus falls back to main") {
        Engine e;
        e.prepare(kFs, kBlock);
        e.setParams(params);
        Buses buses(8192, 1);
        e.trigger(SD, Hit::normal());
        renderAll(e, buses, 8192);
        REQUIRE(peak(buses.left(0)) > 0.01f);
    }
}

TEST_CASE("Pan and level act on the channel", "[core][engine]") {
    Engine e;
    e.prepare(kFs, kBlock);
    EngineParams params;
    params.master.analogOut = false;
    params.master.limiter = false;
    params.channel[CL].pan = -1.0f;
    e.setParams(params);
    Buses hardLeft(4096, 1);
    e.trigger(CL, Hit::normal());
    renderAll(e, hardLeft, 4096);
    REQUIRE(peak(hardLeft.left()) > 0.01f);
    REQUIRE(peak(hardLeft.right()) < 1.0e-6f);

    Engine quiet;
    quiet.prepare(kFs, kBlock);
    params.channel[CL].pan = 0.0f;
    params.channel[CL].level = 0.0f;
    quiet.setParams(params);
    Buses silent(4096, 1);
    quiet.trigger(CL, Hit::normal());
    renderAll(quiet, silent, 4096);
    REQUIRE(peak(silent.left()) == 0.0f);
}

TEST_CASE("Engine output does not depend on how the block is split", "[core][engine]") {
    const int total = 24000;
    std::vector<float> reference;
    for (int block : {512, 64, 7, 333}) {
        Engine e;
        e.prepare(kFs, 512);
        Buses buses(total, 1);
        // The same hits at the same absolute samples, whatever the block size.
        const int hitAt[] = {0, 3000, 3001, 9000, 15000};
        const VoiceId hitVoice[] = {BD, SD, CH, OH, CP};
        int next = 0;
        for (int pos = 0; pos < total;) {
            while (next < 5 && hitAt[next] == pos) {
                e.trigger(hitVoice[next], Hit::accented(0.5f));
                ++next;
            }
            int n = std::min(block, total - pos);
            if (next < 5) n = std::min(n, hitAt[next] - pos);
            e.render(buses.view(), pos, n);
            pos += n;
        }
        if (reference.empty()) {
            reference = buses.left();
            REQUIRE(peak(reference) > 0.05f);
        } else {
            float worst = 0.0f;
            for (std::size_t i = 0; i < reference.size(); ++i)
                worst = std::max(worst, std::abs(reference[i] - buses.left()[i]));
            INFO("block " << block);
            REQUIRE(worst < 1.0e-5f);
        }
    }
}

TEST_CASE("The limiter holds the main bus under its ceiling", "[core][engine]") {
    Engine e;
    e.prepare(kFs, kBlock);
    EngineParams params;
    for (auto& c : params.channel) {
        c.level = 1.0f;
        c.drive = 0.5f;
    }
    params.master.volumeDb = 12.0f;
    params.master.drive = 1.0f;
    params.master.comp = 1.0f;
    e.setParams(params);
    Buses buses(48000, 1);
    for (int v = 0; v < kNumVoices; ++v) e.trigger(static_cast<VoiceId>(v), Hit::accented(1.0f));
    renderAll(e, buses, 48000);
    REQUIRE(allFinite(buses.left()));
    REQUIRE(peak(buses.left()) <= 0.98f);
    REQUIRE(peak(buses.left()) > 0.5f);
}

TEST_CASE("configure re-prepares only when something changes", "[core][engine]") {
    Engine e;
    e.prepare(kFs, kBlock);
    e.configure(Quality::Standard, 0, 0.0f);
    REQUIRE(e.oversampleFactor() == 2);
    e.trigger(BD, Hit::normal());
    Buses buses(512, 1);
    e.render(buses.view(), 0, 512);
    REQUIRE(e.anyVoiceActive());

    e.configure(Quality::Standard, 0, 0.0f);  // unchanged: voice keeps ringing
    REQUIRE(e.anyVoiceActive());

    e.configure(Quality::High, 0, 0.0f);      // changed: voices are re-prepared (silent)
    REQUIRE(e.oversampleFactor() == 4);
    REQUIRE_FALSE(e.anyVoiceActive());

    e.configure(Quality::Eco, 0, 0.0f);
    REQUIRE(e.oversampleFactor() == 1);
}

TEST_CASE("Unit seed and variance change the sound; zero variance ignores the seed", "[core][engine]") {
    const auto renderBd = [](std::uint32_t seed, float variance) {
        Engine e;
        e.prepare(kFs, kBlock);
        e.configure(Quality::Standard, seed, variance);
        Buses buses(9600, 1);
        e.trigger(MC, Hit::normal());  // a conga: no noise path, so only part tolerances can differ
        renderAll(e, buses, 9600);
        return buses.left();
    };
    REQUIRE(same(renderBd(1, 0.0f), renderBd(99, 0.0f)));
    REQUIRE_FALSE(same(renderBd(1, 1.0f), renderBd(99, 1.0f)));
    REQUIRE(same(renderBd(5, 1.0f), renderBd(5, 1.0f)));
}

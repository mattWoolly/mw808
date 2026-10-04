// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The factory library: kits, pattern banks and presets are legal, complete,
// deterministic, and every pattern actually plays through a Machine.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <utility>

#include "CoreTestUtil.h"
#include "mw808/Factory.h"

using namespace mw808;
using namespace coretest;
namespace fx = mw808::factory;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

std::unique_ptr<seq::PatternBank> build(int bank) {
    auto b = std::make_unique<seq::PatternBank>();
    fx::buildBank(bank, *b);
    return b;
}

bool variationEmpty(const seq::Pattern& p, int var) {
    for (int t = 0; t < seq::kNumTracks; ++t)
        for (int s = 0; s < seq::kMaxSteps; ++s)
            if (p.at(var, t, s).bits != 0) return false;
    return true;
}
bool patternEmpty(const seq::Pattern& p) { return variationEmpty(p, 0) && variationEmpty(p, 1); }

bool voiceHits(const seq::Pattern& p, int var) {
    for (int t = 0; t < kNumVoices; ++t)
        for (int s = 0; s < p.lengthOf(t); ++s)
            if (p.at(var, t, s).on()) return true;
    return false;
}

// OH and CH sounding on the same step, following each track's own loop.
bool hatsCollide(const seq::Pattern& p, int var) {
    const int lo = p.lengthOf(OH), lc = p.lengthOf(CH);
    const int period = lo / std::gcd(lo, lc) * lc;
    for (int t = 0; t < period; ++t)
        if (p.at(var, OH, t % lo).on() && p.at(var, CH, t % lc).on()) return true;
    return false;
}

// Patterns allowed to put OH and CH on one step (CH chokes OH). None today;
// list {bank, pattern} here if one is ever written on purpose.
const std::set<std::pair<int, int>> kHatOverlapExceptions{};

// Kit and tempo a pattern is auditioned with: its preset if it has one, else
// the kit that suits its bank section.
std::pair<int, float> kitAndTempoFor(int bank, int pattern) {
    for (int i = 0; i < fx::numPresets(); ++i) {
        const fx::Preset& p = fx::preset(i);
        if (p.bank == bank && p.pattern == pattern && i != 0) return {p.kit, p.tempo};
    }
    const float tempo = fx::bankInfo(bank).tempo;
    if (bank == 0) {
        constexpr int bySection[4] = {1, 2, 7, 10};  // Electro Tight, Sub Boom, Deep House, Latin
        return {bySection[pattern / seq::kPatternsPerBank], tempo};
    }
    if (bank == 2) return {2, tempo};
    if (bank == 3) return {10, tempo};
    return {0, tempo};
}

MachineParams paramsFor(const fx::Kit& k) {
    MachineParams mp;
    mp.engine.voice = k.voice;
    mp.engine.channel = k.channel;
    mp.engine.master = k.master;
    mp.seq.accent = k.accent;
    mp.seq.variation = seq::VariationMode::AB;  // two measures: A then B
    return mp;
}

struct RenderResult {
    float peak = 0.0f;
    bool finite = true;
    std::uint32_t hits = 0;
};

// Two measures of one pattern of a bank, loaded the way the plugin loads a
// preset (whole bank handoff, then a pattern selection), host transport rolling.
RenderResult renderPattern(const seq::PatternBank& bank, int pattern, const fx::Kit& k, double bpm) {
    auto m = std::make_unique<Machine>();
    m->prepare(kFs, kBlock);
    auto handoff = std::make_unique<BankHandoff>();
    handoff->bank = bank;
    handoff->generation = 1;
    REQUIRE(m->postBank(handoff.release()) == nullptr);
    Command select;
    select.type = Command::Type::SelectPattern;
    select.index = pattern;
    REQUIRE(m->post(select));

    const seq::Pattern& p = bank.patterns[static_cast<std::size_t>(pattern)];
    const double measureQn = p.length * seq::stepLengthQn(p.scale);
    const int total = static_cast<int>(2.0 * measureQn * 60.0 / bpm * kFs);

    Runner r{*m};
    r.params = paramsFor(k);
    r.bpm = bpm;
    Buses buses(total, 1);
    r.run(buses, total, kBlock);

    RenderResult res;
    res.finite = allFinite(buses.left()) && allFinite(buses.right());
    res.peak = std::max(peak(buses.left()), peak(buses.right()));
    for (int v = 0; v < kNumVoices; ++v) res.hits += m->playState().hits[static_cast<std::size_t>(v)].load();
    std::unique_ptr<BankHandoff> retired(m->takeRetiredBank());
    return res;
}

}  // namespace

TEST_CASE("The library has the promised size", "[core][factory]") {
    REQUIRE(fx::numKits() >= 16);
    REQUIRE(fx::numBanks() >= 4);
    REQUIRE(fx::numPresets() >= 20);
}

TEST_CASE("Every kit parameter is in range and names are unique", "[core][factory]") {
    std::set<std::string> names;
    for (int i = 0; i < fx::numKits(); ++i) {
        const fx::Kit& k = fx::kit(i);
        INFO("kit " << i << " " << k.name);
        REQUIRE_FALSE(k.name.empty());
        REQUIRE_FALSE(k.category.empty());
        REQUIRE(names.insert(std::string(k.name)).second);
        REQUIRE(k.accent >= 0.0f);
        REQUIRE(k.accent <= 1.0f);
        for (int v = 0; v < kNumVoices; ++v) {
            INFO("voice " << kVoiceInfo[static_cast<std::size_t>(v)].code);
            const VoiceParams& p = k.voice[static_cast<std::size_t>(v)];
            const ChannelParams& c = k.channel[static_cast<std::size_t>(v)];
            REQUIRE(p.tune >= -12.0f);
            REQUIRE(p.tune <= 12.0f);
            for (float x : {p.decay, p.tone, p.shape, c.level, c.drive}) {
                REQUIRE(x >= 0.0f);
                REQUIRE(x <= 1.0f);
            }
            REQUIRE(c.pan >= -1.0f);
            REQUIRE(c.pan <= 1.0f);
            REQUIRE(c.output == 0);
            REQUIRE(c.level >= 0.4f);  // nothing silently switched off
        }
        REQUIRE(k.master.volumeDb >= -6.0f);
        REQUIRE(k.master.volumeDb <= 3.0f);
        REQUIRE(k.master.drive >= 0.0f);
        REQUIRE(k.master.drive <= 1.0f);
        REQUIRE(k.master.comp >= 0.0f);
        REQUIRE(k.master.comp <= 1.0f);
        REQUIRE(k.master.limiter);
        REQUIRE(k.master.analogOut);
    }
}

TEST_CASE("Kit 0 is the stock machine", "[core][factory]") {
    const fx::Kit& k = fx::kit(0);
    REQUIRE(k.name == "Stock");
    for (int v = 0; v < kNumVoices; ++v) {
        const auto sv = static_cast<std::size_t>(v);
        const VoiceParams& d = kVoiceInfo[sv].defaults;
        REQUIRE(k.voice[sv].tune == d.tune);
        REQUIRE(k.voice[sv].decay == d.decay);
        REQUIRE(k.voice[sv].tone == d.tone);
        REQUIRE(k.voice[sv].shape == d.shape);
        REQUIRE(k.channel[sv].level == 0.8f);
        REQUIRE(k.channel[sv].pan == 0.0f);
        REQUIRE(k.channel[sv].drive == 0.0f);
        REQUIRE(k.channel[sv].output == 0);
    }
    REQUIRE(k.accent == 0.7f);
    const MasterParams stock{};
    REQUIRE(k.master.volumeDb == stock.volumeDb);
    REQUIRE(k.master.analogOut == stock.analogOut);
    REQUIRE(k.master.drive == stock.drive);
    REQUIRE(k.master.comp == stock.comp);
    REQUIRE(k.master.limiter == stock.limiter);
}

TEST_CASE("Every bank builds into legal, named patterns", "[core][factory]") {
    std::set<std::string> bankNames;
    for (int b = 0; b < fx::numBanks(); ++b) {
        const fx::BankInfo& info = fx::bankInfo(b);
        INFO("bank " << b << " " << info.name);
        REQUIRE_FALSE(info.name.empty());
        REQUIRE_FALSE(info.category.empty());
        REQUIRE(bankNames.insert(std::string(info.name)).second);
        REQUIRE(info.tempo >= 40.0f);
        REQUIRE(info.tempo <= 300.0f);

        const auto bank = build(b);
        std::set<std::string> names;
        for (int i = 0; i < seq::kNumPatterns; ++i) {
            const seq::Pattern& p = bank->patterns[static_cast<std::size_t>(i)];
            const std::string_view name = fx::patternName(b, i);
            INFO("pattern " << i << " " << name);
            REQUIRE(name.empty() == patternEmpty(p));
            REQUIRE(p.length >= 1);
            REQUIRE(p.length <= seq::kMaxSteps);
            REQUIRE(static_cast<int>(p.scale) < static_cast<int>(seq::Scale::kNumScales));
            REQUIRE(p.shuffle <= 100);
            REQUIRE(p.reserved == 0);
            if (name.empty()) {
                REQUIRE(p == seq::Pattern{});  // empty slots are fully default
                continue;
            }
            REQUIRE(names.insert(std::string(name)).second);
            for (int t = 0; t < kNumVoices; ++t) REQUIRE(p.trackLength[static_cast<std::size_t>(t)] <= seq::kMaxSteps);
            for (int var = 0; var < seq::kNumVariations; ++var) {
                REQUIRE(voiceHits(p, var));  // neither variation is silent (AB never drops out)
                for (int t = 0; t < seq::kNumTracks; ++t)
                    for (int s = 0; s < seq::kMaxSteps; ++s) {
                        const seq::Step c = p.at(var, t, s);
                        // Nothing hides beyond a track's loop.
                        if (s >= p.lengthOf(t)) REQUIRE(c.bits == 0);
                        // The accent row carries a bare on/off.
                        if (t == seq::kAccentTrack) REQUIRE((c.bits & ~0x03U) == 0U);
                        // Modifiers only on hits.
                        if (!c.on()) REQUIRE(c.bits == 0);
                    }
            }
        }
        // A chain, if any, points at real patterns.
        REQUIRE(bank->chain.length <= seq::kMaxChainLength);
        for (int c = 0; c < bank->chain.length; ++c) {
            const int idx = bank->chain.entries[static_cast<std::size_t>(c)];
            REQUIRE(idx < seq::kNumPatterns);
            REQUIRE_FALSE(fx::patternName(b, idx).empty());
        }
    }
}

TEST_CASE("Bank 0 fills all four internal banks with basic and fill patterns", "[core][factory]") {
    REQUIRE(fx::bankInfo(0).name == "Factory");
    const auto bank = build(0);
    for (int i = 0; i < seq::kNumPatterns; ++i) {
        const seq::Pattern& p = bank->patterns[static_cast<std::size_t>(i)];
        INFO("pattern " << i << " " << fx::patternName(0, i));
        REQUIRE_FALSE(fx::patternName(0, i).empty());
        REQUIRE_FALSE(variationEmpty(p, 0));
        REQUIRE_FALSE(variationEmpty(p, 1));
        REQUIRE(p.steps[0] != p.steps[1]);  // B is a real variation everywhere in bank 0
    }
    // Slots 13-16 of each internal bank are fills: one measure, at most 32 steps,
    // and denser toward the end than the start (they build into the downbeat).
    for (int section = 0; section < seq::kNumBanks; ++section)
        for (int f = 0; f < 4; ++f) {
            const int idx = section * seq::kPatternsPerBank + seq::kFirstFillSlot + f;
            const seq::Pattern& p = bank->patterns[static_cast<std::size_t>(idx)];
            INFO("fill " << idx << " " << fx::patternName(0, idx));
            REQUIRE(p.length <= 32);
            for (int var = 0; var < 2; ++var) {
                int first = 0, second = 0;
                for (int t = 0; t < kNumVoices; ++t)
                    for (int s = 0; s < p.lengthOf(t); ++s) {
                        const seq::Step c = p.at(var, t, s);
                        if (!c.on()) continue;
                        (s < p.lengthOf(t) / 2 ? first : second) += c.subSteps();
                    }
                REQUIRE(second >= first);
            }
        }
}

TEST_CASE("The Init bank is empty memory", "[core][factory]") {
    int init = -1;
    for (int b = 0; b < fx::numBanks(); ++b)
        if (fx::bankInfo(b).name == "Init") init = b;
    REQUIRE(init > 0);
    const auto bank = build(init);
    for (const auto& p : bank->patterns) REQUIRE(p == seq::Pattern{});
    REQUIRE(bank->chain.length == 0);
}

TEST_CASE("A and B differ wherever B is written", "[core][factory]") {
    // Every factory pattern ships a B variation; none is a plain copy of A.
    int checked = 0;
    for (int b = 0; b < fx::numBanks(); ++b) {
        const auto bank = build(b);
        for (int i = 0; i < seq::kNumPatterns; ++i) {
            if (fx::patternName(b, i).empty()) continue;
            const seq::Pattern& p = bank->patterns[static_cast<std::size_t>(i)];
            INFO("bank " << b << " pattern " << i << " " << fx::patternName(b, i));
            REQUIRE(p.steps[0] != p.steps[1]);
            ++checked;
        }
    }
    REQUIRE(checked >= 100);
}

TEST_CASE("Open and closed hat never share a step", "[core][factory]") {
    for (int b = 0; b < fx::numBanks(); ++b) {
        const auto bank = build(b);
        for (int i = 0; i < seq::kNumPatterns; ++i) {
            if (kHatOverlapExceptions.count({b, i}) != 0) continue;
            const seq::Pattern& p = bank->patterns[static_cast<std::size_t>(i)];
            INFO("bank " << b << " pattern " << i << " " << fx::patternName(b, i));
            REQUIRE_FALSE(hatsCollide(p, 0));
            REQUIRE_FALSE(hatsCollide(p, 1));
        }
    }
}

TEST_CASE("The library exercises the sequencer features", "[core][factory]") {
    int weak = 0, accentVoice = 0, accentRow = 0, flam = 0, sub = 0, prob = 0, onSteps = 0;
    int shuffled = 0, notSixteen = 0, otherScale = 0, poly = 0, longPatterns = 0;
    std::set<int> scales;
    for (int b = 0; b < fx::numBanks(); ++b) {
        const auto bank = build(b);
        for (int i = 0; i < seq::kNumPatterns; ++i) {
            if (fx::patternName(b, i).empty()) continue;
            const seq::Pattern& p = bank->patterns[static_cast<std::size_t>(i)];
            shuffled += p.shuffle > 0;
            notSixteen += p.length != 16;
            longPatterns += p.length >= 32;
            otherScale += p.scale != seq::Scale::Sixteenth;
            scales.insert(static_cast<int>(p.scale));
            for (int t = 0; t < kNumVoices; ++t) poly += p.trackLength[static_cast<std::size_t>(t)] != 0;
            for (int var = 0; var < 2; ++var)
                for (int t = 0; t < seq::kNumTracks; ++t)
                    for (int s = 0; s < p.lengthOf(t); ++s) {
                        const seq::Step c = p.at(var, t, s);
                        if (!c.on()) continue;
                        if (t == seq::kAccentTrack) {
                            ++accentRow;
                            continue;
                        }
                        ++onSteps;
                        weak += c.level() == seq::StepLevel::Weak;
                        accentVoice += c.level() == seq::StepLevel::Accent;
                        flam += c.flam();
                        sub += c.subSteps() > 1;
                        prob += c.probabilityCode() != 0;
                    }
        }
    }
    INFO("weak " << weak << " accent " << accentVoice << " AC " << accentRow << " flam " << flam << " sub " << sub
                 << " prob " << prob << " of " << onSteps);
    REQUIRE(weak > 50);
    REQUIRE(accentVoice > 50);
    REQUIRE(accentRow > 20);
    REQUIRE(flam >= 10);
    REQUIRE(sub >= 50);
    REQUIRE(prob >= 10);
    REQUIRE(prob * 20 < onSteps);  // probability stays a seasoning (< 5 % of hits)
    REQUIRE(shuffled >= 10);
    REQUIRE(notSixteen >= 8);
    REQUIRE(longPatterns >= 3);
    REQUIRE(otherScale >= 8);
    REQUIRE(scales.size() == static_cast<std::size_t>(seq::Scale::kNumScales));  // every pre-scale
    REQUIRE(poly >= 5);
}

TEST_CASE("Clave patterns are written correctly", "[core][factory]") {
    // Son 3-2 and rumba 3-2 over one 16-pulse cycle.
    const auto onsets = [](const seq::Pattern& p, int var, VoiceId v) {
        std::set<int> s;
        for (int i = 0; i < p.lengthOf(v); ++i)
            if (p.at(var, v, i).on()) s.insert(i);
        return s;
    };
    const std::set<int> son32{0, 3, 6, 10, 12}, son23{2, 4, 8, 11, 14}, rumba32{0, 3, 7, 10, 12};
    const auto bank0 = build(0);
    const auto find = [](int b, std::string_view name) {
        for (int i = 0; i < seq::kNumPatterns; ++i)
            if (fx::patternName(b, i) == name) return i;
        return -1;
    };
    const int son = find(0, "Son Clave 3-2"), rumba = find(0, "Rumba Clave 3-2");
    REQUIRE(son >= 0);
    REQUIRE(rumba >= 0);
    REQUIRE(onsets(bank0->patterns[static_cast<std::size_t>(son)], 0, CL) == son32);
    REQUIRE(onsets(bank0->patterns[static_cast<std::size_t>(rumba)], 0, CL) == rumba32);
    REQUIRE(onsets(bank0->patterns[static_cast<std::size_t>(find(0, "Freestyle Clave"))], 0, CL) == son32);

    int blocks = -1;
    for (int b = 0; b < fx::numBanks(); ++b)
        if (fx::bankInfo(b).name == "Building Blocks") blocks = b;
    REQUIRE(blocks > 0);
    const auto bb = build(blocks);
    const seq::Pattern& sc = bb->patterns[static_cast<std::size_t>(find(blocks, "Son Clave"))];
    REQUIRE(onsets(sc, 0, CL) == son32);
    REQUIRE(onsets(sc, 1, CL) == son23);

    // The 6/8 bell: the standard seven-stroke pattern over twelve pulses.
    const seq::Pattern& afro = bank0->patterns[static_cast<std::size_t>(find(0, "Afro 6/8"))];
    REQUIRE(afro.length == 12);
    REQUIRE(afro.scale == seq::Scale::EighthTriplet);
    REQUIRE(onsets(afro, 0, CB) == std::set<int>{0, 2, 4, 5, 7, 9, 11});
}

TEST_CASE("Presets reference valid kits, banks and patterns", "[core][factory]") {
    const fx::Preset& init = fx::preset(0);
    REQUIRE(init.kit == 0);
    REQUIRE(init.bank == 0);
    REQUIRE(init.pattern == 0);
    REQUIRE(init.tempo == 120.0f);
    REQUIRE_FALSE(fx::patternName(0, 0).empty());

    std::set<std::string> names;
    std::set<int> kitsUsed;
    for (int i = 0; i < fx::numPresets(); ++i) {
        const fx::Preset& p = fx::preset(i);
        INFO("preset " << i << " " << p.name);
        REQUIRE_FALSE(p.name.empty());
        REQUIRE_FALSE(p.category.empty());
        REQUIRE(names.insert(std::string(p.name)).second);
        REQUIRE(p.kit >= 0);
        REQUIRE(p.kit < fx::numKits());
        REQUIRE(p.bank >= 0);
        REQUIRE(p.bank < fx::numBanks());
        REQUIRE(p.pattern >= 0);
        REQUIRE(p.pattern < seq::kNumPatterns);
        REQUIRE(p.tempo >= 40.0f);
        REQUIRE(p.tempo <= 300.0f);
        if (fx::bankInfo(p.bank).name != "Init") REQUIRE_FALSE(fx::patternName(p.bank, p.pattern).empty());
        kitsUsed.insert(p.kit);
    }
    REQUIRE(static_cast<int>(kitsUsed.size()) == fx::numKits());  // every kit has a home
}

TEST_CASE("Out-of-range indices clamp", "[core][factory]") {
    REQUIRE(&fx::kit(-1) == &fx::kit(0));
    REQUIRE(&fx::kit(1000) == &fx::kit(fx::numKits() - 1));
    REQUIRE(&fx::bankInfo(-3) == &fx::bankInfo(0));
    REQUIRE(&fx::bankInfo(99) == &fx::bankInfo(fx::numBanks() - 1));
    REQUIRE(&fx::preset(-1) == &fx::preset(0));
    REQUIRE(&fx::preset(1 << 20) == &fx::preset(fx::numPresets() - 1));

    const auto first = build(0), low = build(-7);
    REQUIRE(first->patterns == low->patterns);
    const auto last = build(fx::numBanks() - 1), high = build(500);
    REQUIRE(last->patterns == high->patterns);
    REQUIRE(last->chain == high->chain);

    REQUIRE(fx::patternName(-1, 0) == fx::patternName(0, 0));
    REQUIRE(fx::patternName(99, 0) == fx::patternName(fx::numBanks() - 1, 0));
    REQUIRE(fx::patternName(0, -1).empty());
    REQUIRE(fx::patternName(0, seq::kNumPatterns).empty());
}

TEST_CASE("buildBank is deterministic and overwrites everything", "[core][factory]") {
    for (int b = 0; b < fx::numBanks(); ++b) {
        const auto clean = build(b);
        auto dirty = std::make_unique<seq::PatternBank>();
        std::memset(static_cast<void*>(dirty.get()), 0x5A, sizeof(seq::PatternBank));
        fx::buildBank(b, *dirty);
        INFO("bank " << b);
        REQUIRE(dirty->patterns == clean->patterns);
        REQUIRE(dirty->chain == clean->chain);
        REQUIRE(std::memcmp(dirty.get(), clean.get(), sizeof(seq::PatternBank)) == 0);
    }
}

TEST_CASE("Every pattern of every bank plays cleanly with its kit", "[core][factory][render]") {
    int rendered = 0;
    float loudest = 0.0f, quietest = 1.0f;
    for (int b = 0; b < fx::numBanks(); ++b) {
        const auto bank = build(b);
        for (int i = 0; i < seq::kNumPatterns; ++i) {
            const bool empty = fx::patternName(b, i).empty();
            if (empty && b != 1) continue;  // render the Init bank's empty slots once to prove silence
            if (empty && i > 0) continue;
            const auto [kitIndex, tempo] = kitAndTempoFor(b, i);
            const RenderResult r = renderPattern(*bank, i, fx::kit(kitIndex), tempo);
            INFO("bank " << b << " pattern " << i << " " << fx::patternName(b, i) << " kit "
                         << fx::kit(kitIndex).name << " peak " << r.peak);
            REQUIRE(r.finite);
            REQUIRE(r.peak < 1.0f);
            if (empty) {
                REQUIRE(r.peak == 0.0f);
                REQUIRE(r.hits == 0U);
            } else {
                REQUIRE(r.peak > 0.01f);
                REQUIRE(r.hits > 0U);
                loudest = std::max(loudest, r.peak);
                quietest = std::min(quietest, r.peak);
                ++rendered;
            }
        }
    }
    INFO("loudest " << loudest << " quietest " << quietest);
    REQUIRE(rendered >= 100);
}

TEST_CASE("Every kit plays the first pattern cleanly", "[core][factory][render]") {
    const auto bank = build(0);
    for (int k = 0; k < fx::numKits(); ++k) {
        const RenderResult r = renderPattern(*bank, 0, fx::kit(k), 120.0);
        INFO("kit " << k << " " << fx::kit(k).name << " peak " << r.peak);
        REQUIRE(r.finite);
        REQUIRE(r.peak < 1.0f);
        REQUIRE(r.peak > 0.05f);
    }
}

// Not run by default: prints the peak of every pattern with its kit, for
// balancing kit levels by ear and eye.
TEST_CASE("Factory render levels", "[.][factory-levels]") {
    for (int b = 0; b < fx::numBanks(); ++b) {
        const auto bank = build(b);
        for (int i = 0; i < seq::kNumPatterns; ++i) {
            if (fx::patternName(b, i).empty()) continue;
            const auto [kitIndex, tempo] = kitAndTempoFor(b, i);
            const RenderResult r = renderPattern(*bank, i, fx::kit(kitIndex), tempo);
            std::printf("%d %2d %-22s %-18s %6.1f bpm peak %.3f\n", b, i, std::string(fx::patternName(b, i)).c_str(),
                        std::string(fx::kit(kitIndex).name).c_str(), static_cast<double>(tempo),
                        static_cast<double>(r.peak));
        }
    }
    const auto bank0 = build(0);
    for (int k = 0; k < fx::numKits(); ++k) {
        const RenderResult r = renderPattern(*bank0, 0, fx::kit(k), 120.0);
        std::printf("kit %2d %-18s peak %.3f\n", k, std::string(fx::kit(k).name).c_str(), static_cast<double>(r.peak));
    }
}

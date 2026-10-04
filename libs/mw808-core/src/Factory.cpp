// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Factory content: kits and presets, and the public entry points of
// mw808/Factory.h. The pattern banks (and the step notation they are written
// in) live in FactoryPatterns.cpp.
//
// Kits are built at compile time from the stock machine (kVoiceInfo defaults,
// level 0.8, centred, clean) and change only what they need to. A few knob
// conventions worth knowing when reading them:
//   * BD decay 0.5 is the hardware's 12 o'clock (about 330 ms to -20 dB);
//     0.9+ is the long "sub" kick. BD tune -2 puts the stock 49 Hz near F1.
//   * Non-hardware knobs are stock at 0.5 and scale roughly x0.25..x4.
//   * Toms/congas: tune 0 is the centre of the hardware TUNING pot
//     (LT 86.5, MT 134, HT 182, LC 192, MC 276, HC 404 Hz).
//   * Kits that drive voices or the master pull the master volume down so a
//     preset change does not jump in loudness; the limiter stays on.

#include "mw808/Factory.h"

#include <array>
#include <cstddef>

namespace mw808::factory {

namespace detail {
// Defined in FactoryPatterns.cpp.
int bankCount() noexcept;
const BankInfo& bankInfoAt(int index) noexcept;
void buildBankAt(int index, seq::PatternBank& out) noexcept;
std::string_view patternNameAt(int bankIndex, int patternIndex) noexcept;
}  // namespace detail

namespace {

constexpr Kit stockKit(std::string_view name, std::string_view category) noexcept {
    Kit k{};
    k.name = name;
    k.category = category;
    for (std::size_t v = 0; v < kNumVoices; ++v) {
        k.voice[v] = kVoiceInfo[v].defaults;
        k.channel[v] = ChannelParams{};
    }
    k.accent = 0.7f;
    k.master = MasterParams{};
    return k;
}

// Sound: tune (semitones), decay, tone, shape.
constexpr void snd(Kit& k, VoiceId v, float tune, float decay, float tone = 0.5f, float shape = 0.5f) noexcept {
    k.voice[v] = VoiceParams{tune, decay, tone, shape};
}
// Mixer: level, pan, drive.
constexpr void mix(Kit& k, VoiceId v, float level, float pan = 0.0f, float drive = 0.0f) noexcept {
    k.channel[v].level = level;
    k.channel[v].pan = pan;
    k.channel[v].drive = drive;
}
constexpr void master(Kit& k, float volumeDb, float drive, float comp) noexcept {
    k.master.volumeDb = volumeDb;
    k.master.drive = drive;
    k.master.comp = comp;
    k.master.analogOut = true;
    k.master.limiter = true;
}

// 0 — the reference machine.
constexpr Kit kitStock() noexcept { return stockKit("Stock", "Classic"); }

// 1 — short, clicky kick, crisp snare and clap, bright hats: early-80s electro.
constexpr Kit kitElectroTight() noexcept {
    Kit k = stockKit("Electro Tight", "Electro");
    snd(k, BD, 0.0f, 0.38f, 0.65f, 0.45f);
    snd(k, SD, 0.0f, 0.42f, 0.60f, 0.70f);
    snd(k, LT, 0.0f, 0.55f, 0.5f, 0.40f);
    snd(k, MT, 0.0f, 0.55f, 0.5f, 0.40f);
    snd(k, HT, 0.0f, 0.55f, 0.5f, 0.40f);
    snd(k, CP, 0.0f, 0.45f, 0.60f, 0.35f);
    snd(k, CH, 0.5f, 0.35f);
    snd(k, OH, 0.5f, 0.35f);
    snd(k, CY, 0.0f, 0.40f, 0.60f);
    mix(k, BD, 0.85f);
    mix(k, SD, 0.75f);
    mix(k, LT, 0.70f, -0.30f);
    mix(k, MT, 0.70f, 0.00f);
    mix(k, HT, 0.70f, 0.30f);
    mix(k, CP, 0.75f);
    mix(k, RS, 0.70f, 0.15f);
    mix(k, CL, 0.70f, -0.25f);
    mix(k, CB, 0.65f, 0.20f);
    mix(k, CY, 0.60f, -0.20f);
    mix(k, OH, 0.65f, 0.15f);
    mix(k, CH, 0.70f, 0.15f);
    k.accent = 0.75f;
    master(k, -1.0f, 0.0f, 0.15f);
    return k;
}

// 2 — the long, tuned sub kick (BD near F1) with a touch of drive.
constexpr Kit kitSubBoom() noexcept {
    Kit k = stockKit("Sub Boom", "Bass");
    snd(k, BD, -2.0f, 0.93f, 0.35f, 0.70f);
    snd(k, SD, 0.5f, 0.45f, 0.65f, 0.65f);
    snd(k, LT, -3.0f, 0.70f, 0.5f, 0.30f);
    snd(k, MT, -3.0f, 0.70f, 0.5f, 0.30f);
    snd(k, HT, -3.0f, 0.65f, 0.5f, 0.30f);
    snd(k, CP, 0.0f, 0.55f, 0.55f, 0.55f);
    snd(k, CH, 1.0f, 0.30f);
    snd(k, OH, 1.0f, 0.30f);
    mix(k, BD, 0.85f, 0.0f, 0.15f);
    mix(k, SD, 0.65f);
    mix(k, CP, 0.80f);
    mix(k, RS, 0.60f, 0.15f);
    mix(k, LT, 0.70f, -0.30f);
    mix(k, MT, 0.70f, 0.00f);
    mix(k, HT, 0.70f, 0.30f);
    mix(k, CH, 0.65f, 0.10f);
    mix(k, OH, 0.60f, 0.10f);
    k.accent = 0.60f;
    master(k, -1.0f, 0.0f, 0.20f);
    return k;
}

// 3 — the same sub kick pushed into distortion, brighter so it cuts on small speakers.
constexpr Kit kitBlownSub() noexcept {
    Kit k = stockKit("Blown Sub", "Driven");
    snd(k, BD, -2.0f, 0.90f, 0.55f, 0.65f);
    snd(k, SD, 0.5f, 0.45f, 0.60f, 0.60f);
    snd(k, CP, 0.0f, 0.50f, 0.60f, 0.45f);
    snd(k, CH, 2.0f, 0.25f);
    snd(k, OH, 2.0f, 0.30f);
    mix(k, BD, 0.80f, 0.0f, 0.55f);
    mix(k, SD, 0.65f, 0.0f, 0.25f);
    mix(k, CP, 0.75f, 0.0f, 0.20f);
    mix(k, RS, 0.60f, 0.15f, 0.20f);
    mix(k, CH, 0.65f, 0.10f);
    mix(k, OH, 0.60f, 0.10f);
    k.accent = 0.65f;
    master(k, -2.0f, 0.30f, 0.30f);
    return k;
}

// 4 — Miami bass: long boomy kick at stock pitch, loud claps, cowbell forward.
constexpr Kit kitMiami() noexcept {
    Kit k = stockKit("Miami Low End", "Bass");
    snd(k, BD, 0.0f, 0.85f, 0.45f, 0.60f);
    snd(k, SD, 0.0f, 0.50f, 0.55f, 0.60f);
    snd(k, CP, 0.0f, 0.50f, 0.55f, 0.50f);
    snd(k, LT, 0.0f, 0.60f, 0.5f, 0.45f);
    snd(k, MT, 0.0f, 0.60f, 0.5f, 0.45f);
    snd(k, HT, 0.0f, 0.60f, 0.5f, 0.45f);
    snd(k, CH, 0.5f, 0.40f);
    snd(k, OH, 0.5f, 0.40f);
    mix(k, BD, 0.85f, 0.0f, 0.10f);
    mix(k, SD, 0.70f);
    mix(k, CP, 0.80f);
    mix(k, CB, 0.70f, -0.20f);
    mix(k, LT, 0.75f, -0.30f);
    mix(k, MT, 0.75f, 0.00f);
    mix(k, HT, 0.75f, 0.30f);
    mix(k, CH, 0.70f, 0.15f);
    mix(k, OH, 0.65f, 0.15f);
    k.accent = 0.75f;
    master(k, -0.5f, 0.0f, 0.10f);
    return k;
}

// 5 — boom bap: shorter, darker, driven kick and snare, dull hats, bus glue.
constexpr Kit kitBoomBap() noexcept {
    Kit k = stockKit("Boom Bap Dust", "Hip-Hop");
    snd(k, BD, -0.5f, 0.45f, 0.35f, 0.50f);
    snd(k, SD, -1.0f, 0.45f, 0.35f, 0.45f);
    snd(k, CP, -0.5f, 0.45f, 0.45f, 0.30f);
    snd(k, CH, -1.5f, 0.35f);
    snd(k, OH, -1.5f, 0.30f);
    mix(k, BD, 0.85f, 0.0f, 0.30f);
    mix(k, SD, 0.80f, 0.0f, 0.35f);
    mix(k, RS, 0.65f, -0.10f);
    mix(k, CP, 0.65f);
    mix(k, CH, 0.60f, 0.15f, 0.20f);
    mix(k, OH, 0.55f, 0.15f, 0.20f);
    k.accent = 0.65f;
    master(k, -2.0f, 0.35f, 0.35f);
    return k;
}

// 6 — slow jam / R&B: warm long kick, soft snare, big reverb clap, quiet hats, deep toms.
constexpr Kit kitSlowJam() noexcept {
    Kit k = stockKit("Slow Jam Silk", "R&B");
    snd(k, BD, -1.0f, 0.75f, 0.30f, 0.55f);
    snd(k, SD, -1.0f, 0.55f, 0.45f, 0.40f);
    snd(k, CP, -1.0f, 0.65f, 0.45f, 0.80f);
    snd(k, LT, -2.0f, 0.75f, 0.5f, 0.35f);
    snd(k, MT, -2.0f, 0.75f, 0.5f, 0.35f);
    snd(k, HT, -2.0f, 0.70f, 0.5f, 0.35f);
    snd(k, CH, 0.0f, 0.45f);
    snd(k, OH, 0.0f, 0.40f);
    mix(k, BD, 0.85f);
    mix(k, SD, 0.70f);
    mix(k, CP, 0.75f);
    mix(k, RS, 0.60f, -0.15f);
    mix(k, LT, 0.75f, -0.30f);
    mix(k, MT, 0.75f, 0.00f);
    mix(k, HT, 0.75f, 0.30f);
    mix(k, CB, 0.55f, 0.20f);
    mix(k, CH, 0.55f, 0.20f);
    mix(k, OH, 0.50f, 0.20f);
    k.accent = 0.50f;
    master(k, 0.0f, 0.0f, 0.10f);
    return k;
}

// 7 — deep house: rounded kick, roomy clap, soft offbeat hats, rim and congas around it.
constexpr Kit kitDeepHouse() noexcept {
    Kit k = stockKit("Deep House", "House");
    snd(k, BD, -0.5f, 0.58f, 0.40f, 0.50f);
    snd(k, CP, -0.5f, 0.60f, 0.45f, 0.70f);
    snd(k, OH, 0.0f, 0.42f);
    snd(k, CH, 0.0f, 0.40f);
    mix(k, BD, 0.85f);
    mix(k, CP, 0.70f);
    mix(k, SD, 0.65f);
    mix(k, RS, 0.60f, -0.20f);
    mix(k, LC, 0.60f, -0.30f);
    mix(k, MC, 0.60f, 0.10f);
    mix(k, HC, 0.60f, 0.35f);
    mix(k, OH, 0.60f, 0.15f);
    mix(k, CH, 0.55f, 0.15f);
    k.accent = 0.60f;
    master(k, 0.0f, 0.0f, 0.15f);
    return k;
}

// 8 — techno: hard, bright, driven kick; tight percussion; master drive and comp.
constexpr Kit kitTechno() noexcept {
    Kit k = stockKit("Techno Drive", "Techno");
    snd(k, BD, 0.0f, 0.50f, 0.75f, 0.55f);
    snd(k, CP, 0.5f, 0.45f, 0.60f, 0.45f);
    snd(k, CH, 1.0f, 0.40f);
    snd(k, OH, 1.0f, 0.40f);
    snd(k, CY, 0.0f, 0.60f, 0.60f);
    mix(k, BD, 0.90f, 0.0f, 0.40f);
    mix(k, CP, 0.75f, 0.0f, 0.20f);
    mix(k, RS, 0.70f, 0.20f, 0.20f);
    mix(k, CH, 0.65f, 0.15f);
    mix(k, OH, 0.65f, -0.15f);
    mix(k, CY, 0.55f, -0.25f);
    k.accent = 0.80f;
    master(k, -2.5f, 0.25f, 0.35f);
    return k;
}

// 9 — lo-fi: everything darker and detuned down, driven per voice and on the bus.
constexpr Kit kitLoFi() noexcept {
    Kit k = stockKit("Lo-Fi Crunch", "Driven");
    snd(k, BD, -1.0f, 0.50f, 0.25f, 0.45f);
    snd(k, SD, -1.5f, 0.45f, 0.30f, 0.50f);
    snd(k, CP, -1.0f, 0.50f, 0.35f, 0.40f);
    snd(k, RS, -1.0f, 0.50f);
    snd(k, CH, -3.0f, 0.30f);
    snd(k, OH, -3.0f, 0.35f);
    snd(k, CY, -2.0f, 0.45f, 0.30f);
    mix(k, BD, 0.85f, 0.0f, 0.40f);
    mix(k, SD, 0.75f, 0.0f, 0.45f);
    mix(k, CP, 0.70f, 0.0f, 0.30f);
    mix(k, RS, 0.65f, -0.10f, 0.30f);
    mix(k, CH, 0.65f, 0.15f, 0.30f);
    mix(k, OH, 0.55f, 0.15f, 0.30f);
    k.accent = 0.55f;
    master(k, -3.0f, 0.55f, 0.45f);
    return k;
}

// 10 — Latin percussion: congas, claves, cowbell and maracas forward and spread;
// the toms tuned up to stand in for timbales.
constexpr Kit kitLatin() noexcept {
    Kit k = stockKit("Latin Percussion", "Latin");
    snd(k, BD, 0.0f, 0.55f, 0.45f, 0.50f);
    snd(k, LT, 2.0f, 0.50f, 0.5f, 0.20f);
    snd(k, MT, 3.0f, 0.50f, 0.5f, 0.20f);
    snd(k, HT, 3.0f, 0.50f, 0.5f, 0.20f);
    snd(k, LC, -1.0f, 0.60f);
    snd(k, MC, 0.0f, 0.55f);
    snd(k, HC, 0.5f, 0.50f);
    snd(k, MA, 0.0f, 0.55f);
    mix(k, BD, 0.75f);
    mix(k, SD, 0.60f);
    mix(k, LT, 0.65f, -0.15f);
    mix(k, MT, 0.65f, 0.05f);
    mix(k, HT, 0.65f, 0.25f);
    mix(k, LC, 0.80f, -0.35f);
    mix(k, MC, 0.75f, -0.10f);
    mix(k, HC, 0.75f, 0.30f);
    mix(k, RS, 0.65f, 0.20f);
    mix(k, CL, 0.70f, 0.40f);
    mix(k, MA, 0.65f, 0.50f);
    mix(k, CB, 0.70f, -0.25f);
    mix(k, CH, 0.55f, 0.15f);
    mix(k, OH, 0.55f, 0.15f);
    k.accent = 0.65f;
    master(k, 0.0f, 0.0f, 0.10f);
    return k;
}

// 11 — the stock sound laid out across the stereo field, toms and congas as a
// drummer would sit them, hats right, cymbal left.
constexpr Kit kitWide() noexcept {
    Kit k = stockKit("Wide Stereo", "Classic");
    mix(k, SD, 0.80f, 0.05f);
    mix(k, LT, 0.80f, -0.60f);
    mix(k, MT, 0.80f, 0.00f);
    mix(k, HT, 0.80f, 0.60f);
    mix(k, LC, 0.80f, -0.70f);
    mix(k, MC, 0.80f, -0.20f);
    mix(k, HC, 0.80f, 0.50f);
    mix(k, RS, 0.80f, -0.30f);
    mix(k, CL, 0.80f, 0.70f);
    mix(k, MA, 0.80f, -0.70f);
    mix(k, CB, 0.80f, 0.35f);
    mix(k, CY, 0.80f, -0.50f);
    mix(k, OH, 0.80f, 0.45f);
    mix(k, CH, 0.80f, 0.35f);
    return k;
}

// 12 — toms and congas tuned to F minor (toms F2 C3 F3, congas Ab3 C4 F4, within
// a few cents), longer rings and less tom noise, for melodic tom runs.
constexpr Kit kitMelodicToms() noexcept {
    Kit k = stockKit("Melodic Toms", "Tuned");
    snd(k, LT, 0.16f, 0.70f, 0.5f, 0.35f);
    snd(k, MT, -0.42f, 0.70f, 0.5f, 0.35f);
    snd(k, HT, -0.72f, 0.70f, 0.5f, 0.35f);
    snd(k, LC, 1.36f, 0.65f);
    snd(k, MC, -0.93f, 0.65f);
    snd(k, HC, -2.52f, 0.65f);
    snd(k, BD, -0.3f, 0.55f, 0.50f, 0.50f);
    mix(k, LT, 0.80f, -0.40f);
    mix(k, MT, 0.80f, 0.00f);
    mix(k, HT, 0.80f, 0.40f);
    mix(k, LC, 0.75f, -0.30f);
    mix(k, MC, 0.75f, 0.00f);
    mix(k, HC, 0.75f, 0.30f);
    mix(k, CH, 0.70f, 0.15f);
    mix(k, OH, 0.70f, 0.15f);
    return k;
}

// 13 — toms and congas dropped low and long (LT near 57 Hz) for trap and R&B
// fills, under a long, low kick.
constexpr Kit kitDeepToms() noexcept {
    Kit k = stockKit("Deep Toms", "Tuned");
    snd(k, BD, -3.0f, 0.88f, 0.35f, 0.60f);
    snd(k, LT, -7.0f, 0.80f, 0.5f, 0.25f);
    snd(k, MT, -7.0f, 0.80f, 0.5f, 0.25f);
    snd(k, HT, -7.0f, 0.75f, 0.5f, 0.25f);
    snd(k, LC, -5.0f, 0.70f);
    snd(k, MC, -5.0f, 0.70f);
    snd(k, HC, -5.0f, 0.70f);
    snd(k, CP, 0.0f, 0.55f, 0.50f, 0.60f);
    snd(k, CH, 0.5f, 0.30f);
    mix(k, BD, 0.85f, 0.0f, 0.10f);
    mix(k, LT, 0.80f, -0.35f);
    mix(k, MT, 0.80f, 0.00f);
    mix(k, HT, 0.80f, 0.35f);
    mix(k, LC, 0.75f, -0.25f);
    mix(k, MC, 0.75f, 0.05f);
    mix(k, HC, 0.75f, 0.30f);
    mix(k, CH, 0.65f, 0.10f);
    mix(k, OH, 0.60f, 0.10f);
    k.accent = 0.60f;
    master(k, -1.0f, 0.0f, 0.15f);
    return k;
}

// 14 — bright pop: clicky kick, snappy snare, open top end, strong accents.
constexpr Kit kitBrightPop() noexcept {
    Kit k = stockKit("Bright Pop", "Classic");
    snd(k, BD, 0.5f, 0.45f, 0.75f, 0.50f);
    snd(k, SD, 0.5f, 0.50f, 0.70f, 0.75f);
    snd(k, CP, 0.5f, 0.50f, 0.65f, 0.55f);
    snd(k, CH, 1.5f, 0.45f);
    snd(k, OH, 1.5f, 0.45f);
    snd(k, CY, 0.5f, 0.55f, 0.75f);
    mix(k, SD, 0.75f);
    mix(k, CP, 0.75f);
    mix(k, MA, 0.65f, -0.30f);
    mix(k, CH, 0.70f, 0.20f);
    mix(k, OH, 0.65f, 0.20f);
    mix(k, CY, 0.60f, -0.25f);
    mix(k, LT, 0.80f, -0.30f);
    mix(k, HT, 0.80f, 0.30f);
    k.accent = 0.80f;
    master(k, -0.5f, 0.0f, 0.20f);
    return k;
}

// 15 — phonk: cowbell tuned up and distorted for riffs, long driven sub kick.
constexpr Kit kitPhonk() noexcept {
    Kit k = stockKit("Cowbell Phonk", "Driven");
    snd(k, BD, -2.0f, 0.88f, 0.45f, 0.60f);
    snd(k, CB, 3.0f, 0.60f);
    snd(k, CP, 0.0f, 0.50f, 0.55f, 0.40f);
    snd(k, SD, 0.0f, 0.45f, 0.55f, 0.60f);
    snd(k, CH, 0.5f, 0.30f);
    mix(k, BD, 0.80f, 0.0f, 0.35f);
    mix(k, CB, 0.80f, 0.0f, 0.50f);
    mix(k, CP, 0.75f, 0.0f, 0.30f);
    mix(k, SD, 0.70f, 0.0f, 0.20f);
    mix(k, CH, 0.65f, 0.15f, 0.20f);
    mix(k, OH, 0.60f, 0.15f);
    k.accent = 0.70f;
    master(k, -2.0f, 0.40f, 0.30f);
    return k;
}

// 16 — minimal: everything short and dry, rim and claves up front.
constexpr Kit kitMinimal() noexcept {
    Kit k = stockKit("Minimal Click", "Techno");
    snd(k, BD, 0.0f, 0.32f, 0.60f, 0.40f);
    snd(k, SD, 0.0f, 0.30f, 0.60f, 0.50f);
    snd(k, RS, 0.5f, 0.35f);
    snd(k, CL, 0.0f, 0.40f);
    snd(k, CP, 0.5f, 0.30f, 0.55f, 0.20f);
    snd(k, LC, 0.0f, 0.35f);
    snd(k, MC, 0.0f, 0.35f);
    snd(k, HC, 0.0f, 0.35f);
    snd(k, CH, 0.5f, 0.20f);
    snd(k, OH, 0.5f, 0.20f);
    mix(k, BD, 0.85f);
    mix(k, RS, 0.75f, 0.25f);
    mix(k, CL, 0.70f, -0.30f);
    mix(k, CP, 0.65f);
    mix(k, HC, 0.70f, 0.40f);
    mix(k, CH, 0.65f, 0.10f);
    mix(k, OH, 0.60f, 0.10f);
    k.accent = 0.60f;
    master(k, 0.0f, 0.0f, 0.10f);
    return k;
}

// 17 — an old, slightly tired unit: everything a hair flat and darker, levels
// uneven as on a machine nobody has recalibrated, hot accent, gentle bus drive.
constexpr Kit kitWorn() noexcept {
    Kit k = stockKit("Worn Unit", "Classic");
    snd(k, BD, -0.3f, 0.60f, 0.35f, 0.55f);
    snd(k, SD, -0.4f, 0.50f, 0.40f, 0.45f);
    snd(k, CP, -0.2f, 0.55f, 0.45f, 0.55f);
    snd(k, CB, -0.35f, 0.55f);
    snd(k, CY, -0.6f, 0.60f, 0.35f);
    snd(k, OH, -0.8f, 0.55f);
    snd(k, CH, -0.8f, 0.50f);
    mix(k, BD, 0.82f);
    mix(k, SD, 0.76f);
    mix(k, CP, 0.85f);
    mix(k, RS, 0.72f);
    mix(k, CB, 0.74f);
    mix(k, MA, 0.70f);
    mix(k, CH, 0.74f);
    mix(k, OH, 0.70f);
    k.accent = 0.85f;
    master(k, -1.0f, 0.20f, 0.0f);
    return k;
}

// 18 — reggaeton: punchy kick, tight high snare and clap, conga slaps.
constexpr Kit kitReggaeton() noexcept {
    Kit k = stockKit("Reggaeton", "Latin");
    snd(k, BD, 0.5f, 0.55f, 0.50f, 0.50f);
    snd(k, SD, 1.0f, 0.45f, 0.60f, 0.60f);
    snd(k, CP, 0.5f, 0.45f, 0.55f, 0.40f);
    snd(k, CH, 1.0f, 0.35f);
    snd(k, OH, 1.0f, 0.35f);
    mix(k, BD, 0.85f);
    mix(k, SD, 0.75f);
    mix(k, CP, 0.70f);
    mix(k, RS, 0.65f, -0.15f);
    mix(k, HC, 0.65f, 0.30f);
    mix(k, MC, 0.65f, -0.20f);
    mix(k, CH, 0.65f, 0.15f);
    mix(k, OH, 0.60f, 0.15f);
    mix(k, LT, 0.75f, -0.25f);
    k.accent = 0.70f;
    master(k, -1.5f, 0.0f, 0.25f);
    return k;
}

constexpr std::array<Kit, 19> kKits{{
    kitStock(),        //  0
    kitElectroTight(), //  1
    kitSubBoom(),      //  2
    kitBlownSub(),     //  3
    kitMiami(),        //  4
    kitBoomBap(),      //  5
    kitSlowJam(),      //  6
    kitDeepHouse(),    //  7
    kitTechno(),       //  8
    kitLoFi(),         //  9
    kitLatin(),        // 10
    kitWide(),         // 11
    kitMelodicToms(),  // 12
    kitDeepToms(),     // 13
    kitBrightPop(),    // 14
    kitPhonk(),        // 15
    kitMinimal(),      // 16
    kitWorn(),         // 17
    kitReggaeton(),    // 18
}};

constexpr bool in(float x, float lo, float hi) noexcept { return x >= lo && x <= hi; }

constexpr bool kitsValid() noexcept {
    for (const Kit& k : kKits) {
        if (k.name.empty() || k.category.empty() || !in(k.accent, 0.0f, 1.0f)) return false;
        for (std::size_t v = 0; v < kNumVoices; ++v) {
            const VoiceParams& p = k.voice[v];
            const ChannelParams& c = k.channel[v];
            if (!in(p.tune, -12.0f, 12.0f) || !in(p.decay, 0.0f, 1.0f) || !in(p.tone, 0.0f, 1.0f) ||
                !in(p.shape, 0.0f, 1.0f))
                return false;
            if (!in(c.level, 0.0f, 1.0f) || !in(c.pan, -1.0f, 1.0f) || !in(c.drive, 0.0f, 1.0f) || c.output != 0)
                return false;
        }
        if (!in(k.master.volumeDb, -6.0f, 3.0f) || !in(k.master.drive, 0.0f, 1.0f) ||
            !in(k.master.comp, 0.0f, 1.0f) || !k.master.limiter || !k.master.analogOut)
            return false;
    }
    return true;
}
static_assert(kitsValid(), "factory kit parameter out of range");

// Pattern indices are global (0..63): internal bank A = 0-15, B = 16-31,
// C = 32-47, D = 48-63; slots 12-15 of each are the fills.
// Banks: 0 Factory, 1 Init, 2 Bass Music, 3 World Grooves, 4 Building Blocks.
constexpr std::array<Preset, 40> kPresets{{
    // name                 category        kit bank pattern tempo
    {"Init",                "Basic",         0, 0,  0, 120.0f},
    {"Electro Cruise",      "Electro",       1, 0,  0, 124.0f},
    {"Electro Bounce",      "Electro",       1, 0,  1, 126.0f},
    {"Robot Toms",          "Electro",      12, 0,  2, 118.0f},
    {"Miami Boom",          "Miami Bass",    4, 0,  3, 132.0f},
    {"Freestyle Heat",      "Freestyle",    14, 0,  5, 118.0f},
    {"Funk Pocket",         "Funk",         17, 0,  7, 100.0f},
    {"Breaker",             "Breakbeat",     1, 0,  9, 112.0f},
    {"Synth Pop",           "Pop",          14, 0, 11, 118.0f},
    {"Boom Bap",            "Hip-Hop",       5, 0, 16,  90.0f},
    {"Dusty Swing",         "Hip-Hop",       9, 0, 17,  86.0f},
    {"New Jack Swing",      "R&B",          11, 0, 19, 104.0f},
    {"Trap Classic",        "Trap",          2, 0, 20, 140.0f},
    {"Trap Triplets",       "Trap",          3, 0, 21, 140.0f},
    {"Drill Slide",         "Trap",          2, 0, 22, 142.0f},
    {"Slow Jam",            "R&B",           6, 0, 23,  68.0f},
    {"Late Night R&B",      "R&B",          13, 0, 24,  84.0f},
    {"Half-Time Heavy",     "Half-Time",     3, 0, 27, 140.0f},
    {"Deep House",          "House",         7, 0, 32, 122.0f},
    {"Classic House",       "House",         0, 0, 33, 124.0f},
    {"Garage Two-Step",     "House",        11, 0, 34, 132.0f},
    {"Techno Drive",        "Techno",        8, 0, 36, 132.0f},
    {"Minimal Tick",        "Techno",       16, 0, 38, 125.0f},
    {"Footwork 160",        "Club",          2, 0, 40, 160.0f},
    {"Lo-Fi House",         "House",         9, 0, 43, 118.0f},
    {"Dembow",              "Reggaeton",    18, 0, 48,  96.0f},
    {"Son Clave 3-2",       "Latin",        10, 0, 50, 104.0f},
    {"Bossa Nova",          "Latin",        10, 0, 52, 130.0f},
    {"Afro 6/8",            "Afro",         10, 0, 54, 110.0f},
    {"Three Over Four",     "Experimental", 11, 0, 56, 118.0f},
    {"Glitch 32",           "Experimental", 16, 0, 59,  96.0f},
    {"Sub Trap",            "Trap",          2, 2,  0, 140.0f},
    {"Rage",                "Trap",          3, 2,  2, 155.0f},
    {"Phonk Cowbell",       "Phonk",        15, 2,  4, 130.0f},
    {"Memphis Rattle",      "Phonk",        15, 2,  5,  70.0f},
    {"Booty Bass",          "Miami Bass",    4, 2,  7, 130.0f},
    {"Ghetto Tech",         "Club",          1, 2, 10, 155.0f},
    {"Cascara 2-3",         "Latin",        10, 3,  0, 180.0f},
    {"One Drop",            "Reggae",        6, 3, 11,  76.0f},
    {"Empty Canvas",        "Basic",         0, 1,  0, 120.0f},
}};

template <class T, std::size_t N>
constexpr int clampIndex(const std::array<T, N>&, int index) noexcept {
    return index < 0 ? 0 : (index >= static_cast<int>(N) ? static_cast<int>(N) - 1 : index);
}

}  // namespace

int numKits() noexcept { return static_cast<int>(kKits.size()); }
const Kit& kit(int index) noexcept { return kKits[static_cast<std::size_t>(clampIndex(kKits, index))]; }

int numBanks() noexcept { return detail::bankCount(); }
const BankInfo& bankInfo(int index) noexcept { return detail::bankInfoAt(index); }
void buildBank(int index, seq::PatternBank& out) noexcept { detail::buildBankAt(index, out); }
std::string_view patternName(int bankIndex, int patternIndex) noexcept {
    return detail::patternNameAt(bankIndex, patternIndex);
}

int numPresets() noexcept { return static_cast<int>(kPresets.size()); }
const Preset& preset(int index) noexcept { return kPresets[static_cast<std::size_t>(clampIndex(kPresets, index))]; }

}  // namespace mw808::factory

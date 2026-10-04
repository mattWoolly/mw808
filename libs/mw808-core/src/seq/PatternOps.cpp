// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Pattern editing helpers. See PatternOps.h for the contract of each call.
//
// Randomisation uses the shared dsp::hash32 mixer keyed on (seed, track,
// step), so a result depends only on its arguments: no hidden RNG state.
// The OH/CH exclusion is enforced per step index. When the two hat tracks have
// different polymeter lengths their steps drift against each other from
// measure to measure, so index-wise exclusion cannot rule out every overlap;
// with equal lengths (the normal case) it is exact.

#include "mw808/seq/PatternOps.h"

#include "mw808/dsp/Math.h"

namespace mw808::seq {

namespace {

bool validVariation(int v) noexcept { return v >= 0 && v < kNumVariations; }
bool validTrack(int t) noexcept { return t >= 0 && t < kNumTracks; }

int clampedLength(const Pattern& p, int track) noexcept {
    const int len = p.lengthOf(track);
    return len < 1 ? 1 : (len > kMaxSteps ? kMaxSteps : len);
}

int floorMod(int a, int m) noexcept {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

// Uniform in [0, 1) from (seed, track, step, salt).
float unitRandom(std::uint32_t seed, int track, int step, std::uint32_t salt) noexcept {
    const std::uint32_t id = (static_cast<std::uint32_t>(track) << 16) ^ (static_cast<std::uint32_t>(step) << 4) ^ salt;
    const std::uint32_t h = dsp::hash32(seed, id);
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

// Keep the hats exclusive after `written` (OH or CH) was filled: the other
// hat loses its steps where `written` is now on.
void enforceHatExclusion(Pattern& p, int variation, int written) noexcept {
    if (written != OH && written != CH) return;
    const int other = written == OH ? CH : OH;
    const int len = clampedLength(p, written);
    for (int s = 0; s < len; ++s)
        if (p.at(variation, written, s).on()) p.at(variation, other, s) = Step{};
}

void randomizeTrackNoHats(Pattern& p, int variation, int track, float density, std::uint32_t seed) noexcept {
    const float d = density != density ? 0.0f : (density < 0.0f ? 0.0f : (density > 1.0f ? 1.0f : density));
    const int len = clampedLength(p, track);
    for (int s = 0; s < len; ++s) {
        Step cell{};
        if (unitRandom(seed, track, s, 0x1U) < d) {
            if (track == kAccentTrack) {
                cell.setLevel(StepLevel::Normal);
            } else {
                const float r = unitRandom(seed, track, s, 0x2U);
                cell.setLevel(r < 0.15f ? StepLevel::Weak : (r < 0.85f ? StepLevel::Normal : StepLevel::Accent));
            }
        }
        p.at(variation, track, s) = cell;
    }
}

}  // namespace

void clearPattern(Pattern& p, int variation) noexcept {
    for (int v = 0; v < kNumVariations; ++v) {
        if (variation != kBothVariations && v != variation) continue;
        for (int t = 0; t < kNumTracks; ++t)
            for (int s = 0; s < kMaxSteps; ++s) p.at(v, t, s) = Step{};
    }
}

void clearTrack(Pattern& p, int variation, int track) noexcept {
    if (!validVariation(variation) || !validTrack(track)) return;
    for (int s = 0; s < kMaxSteps; ++s) p.at(variation, track, s) = Step{};
}

void copyTrack(const Pattern& src, int srcVariation, int srcTrack, Pattern& dst, int dstVariation,
               int dstTrack) noexcept {
    if (!validVariation(srcVariation) || !validVariation(dstVariation) || !validTrack(srcTrack) ||
        !validTrack(dstTrack))
        return;
    std::array<Step, kMaxSteps> tmp{};
    for (int s = 0; s < kMaxSteps; ++s) tmp[static_cast<std::size_t>(s)] = src.at(srcVariation, srcTrack, s);
    const std::uint8_t len = (srcTrack < kNumVoices) ? src.trackLength[static_cast<std::size_t>(srcTrack)] : 0;
    for (int s = 0; s < kMaxSteps; ++s) dst.at(dstVariation, dstTrack, s) = tmp[static_cast<std::size_t>(s)];
    if (srcTrack < kNumVoices && dstTrack < kNumVoices) dst.trackLength[static_cast<std::size_t>(dstTrack)] = len;
}

void copyTrack(Pattern& p, int variation, int fromTrack, int toTrack) noexcept {
    copyTrack(p, variation, fromTrack, p, variation, toTrack);
}

void shiftTrack(Pattern& p, int variation, int track, int steps) noexcept {
    if (!validVariation(variation) || !validTrack(track)) return;
    const int len = clampedLength(p, track);
    const int k = floorMod(steps, len);
    if (k == 0) return;
    std::array<Step, kMaxSteps> tmp{};
    for (int s = 0; s < len; ++s) tmp[static_cast<std::size_t>((s + k) % len)] = p.at(variation, track, s);
    for (int s = 0; s < len; ++s) p.at(variation, track, s) = tmp[static_cast<std::size_t>(s)];
}

void shiftPattern(Pattern& p, int variation, int steps) noexcept {
    if (!validVariation(variation)) return;
    for (int t = 0; t < kNumTracks; ++t) shiftTrack(p, variation, t, steps);
}

void invertTrack(Pattern& p, int variation, int track) noexcept {
    if (!validVariation(variation) || !validTrack(track)) return;
    const int len = clampedLength(p, track);
    for (int s = 0; s < len; ++s) {
        Step& cell = p.at(variation, track, s);
        Step next{};
        if (!cell.on()) next.setLevel(StepLevel::Normal);
        cell = next;
    }
}

void copyVariation(Pattern& p, int from, int to) noexcept {
    if (!validVariation(from) || !validVariation(to) || from == to) return;
    p.steps[static_cast<std::size_t>(to)] = p.steps[static_cast<std::size_t>(from)];
}

void randomizeTrack(Pattern& p, int variation, int track, float density, std::uint32_t seed) noexcept {
    if (!validVariation(variation) || !validTrack(track)) return;
    randomizeTrackNoHats(p, variation, track, density, seed);
    enforceHatExclusion(p, variation, track);
}

void randomizePattern(Pattern& p, int variation, const std::array<float, kNumTracks>& densities,
                      std::uint32_t seed) noexcept {
    if (!validVariation(variation)) return;
    for (int t = 0; t < kNumTracks; ++t)
        randomizeTrackNoHats(p, variation, t, densities[static_cast<std::size_t>(t)], seed);
    // Resolve OH+CH collisions with a coin per step, so neither hat always wins.
    const int len = clampedLength(p, OH) < clampedLength(p, CH) ? clampedLength(p, OH) : clampedLength(p, CH);
    for (int s = 0; s < len; ++s) {
        Step& oh = p.at(variation, OH, s);
        Step& ch = p.at(variation, CH, s);
        if (oh.on() && ch.on()) {
            if (unitRandom(seed, kNumTracks, s, 0x3U) < 0.5f)
                oh = Step{};
            else
                ch = Step{};
        }
    }
}

bool expandPattern(Pattern& p) noexcept {
    const int len = p.length < 1 ? 1 : p.length;
    if (len * 2 > kMaxSteps) return false;
    for (int v = 0; v < kNumVariations; ++v) {
        for (int t = 0; t < kNumTracks; ++t) {
            if (t < kNumVoices && p.trackLength[static_cast<std::size_t>(t)] != 0) continue;
            for (int s = 0; s < len; ++s) p.at(v, t, s + len) = p.at(v, t, s);
        }
    }
    p.length = static_cast<std::uint8_t>(len * 2);
    return true;
}

void setEuclidean(Pattern& p, int variation, int track, int hits, int rotation) noexcept {
    if (!validVariation(variation) || !validTrack(track)) return;
    const int len = clampedLength(p, track);
    const int k = hits < 0 ? 0 : (hits > len ? len : hits);
    clearTrack(p, variation, track);
    const int rot = floorMod(rotation, len);  // reduce first: s - INT_MIN would overflow
    for (int s = 0; s < len; ++s) {
        const int i = floorMod(s - rot, len);
        // Bresenham form of the Euclidean (Bjorklund) rhythm: onset at 0.
        if ((i * k) % len < k) p.at(variation, track, s).setLevel(StepLevel::Normal);
    }
}

}  // namespace mw808::seq

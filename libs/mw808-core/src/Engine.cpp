// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "mw808/Engine.h"

#include <algorithm>
#include <cmath>

#include "mw808/dsp/Math.h"

namespace mw808 {

namespace {

// Voice units (1.0 = 5 V peak) to plugin output. Chosen so a fully accented
// bass drum at the default LEVEL lands near -7 dBFS on the main bus, leaving
// room for the rest of the kit on the same step.
constexpr float kOutputScale = 0.5f;

// Gain smoothing time for level/pan moves.
constexpr double kGainSmoothSeconds = 0.004;

// Hardware master high-pass (C and R of the output coupling network), Hz.
constexpr double kMasterHighpassHz = 47.7;

// Output op-amp headroom in output units: far above normal levels, so the
// stage is nearly linear until it is driven.
constexpr float kSaturationCeiling = 2.0f;

// Limiter: linear below the knee, smooth approach to the ceiling above it.
constexpr float kLimiterKnee = 0.7f;
constexpr float kLimiterCeiling = 0.98f;

constexpr float kCompRatio = 4.0f;

float limit(float x) noexcept {
    const float a = std::abs(x);
    if (a <= kLimiterKnee) return x;
    const float range = kLimiterCeiling - kLimiterKnee;
    const float y = kLimiterKnee + range * std::tanh((a - kLimiterKnee) / range);
    return x < 0.0f ? -y : y;
}

// Overdrive with rough loudness compensation. amount in (0, 1].
float overdrive(float x, float pre, float post) noexcept { return std::tanh(x * pre) * post; }

bool sameParams(const VoiceParams& a, const VoiceParams& b) noexcept {
    return a.tune == b.tune && a.decay == b.decay && a.tone == b.tone && a.shape == b.shape;
}

}  // namespace

const std::array<float, kNumVoices>& Engine::mixGains() noexcept {
    // From the mix-bus resistor values (research/03): BD +3.1 dB, OH -4.7 dB,
    // CH / CB / CY -3.3 dB, everything else about 0 dB.
    static const std::array<float, kNumVoices> gains = [] {
        std::array<float, kNumVoices> g{};
        g.fill(1.0f);
        g[BD] = 1.4289f;  // +3.1 dB
        g[OH] = 0.5821f;  // -4.7 dB
        g[CH] = 0.6839f;  // -3.3 dB
        g[CB] = 0.6839f;
        g[CY] = 0.6839f;
        return g;
    }();
    return gains;
}

void Engine::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(1, maxBlockSize);

    const auto size = static_cast<std::size_t>(maxBlock_);
    noiseBuf_.assign(size, 0.0f);
    voiceBuf_.assign(size, 0.0f);
    voiceBuf2_.assign(size, 0.0f);
    mainL_.assign(size, 0.0f);
    mainR_.assign(size, 0.0f);

    gainSmooth_ = static_cast<float>(1.0 - dsp::expDecayCoeff(kGainSmoothSeconds, sampleRate_));
    compAttack_ = static_cast<float>(1.0 - dsp::expDecayCoeff(0.010, sampleRate_));
    compRelease_ = static_cast<float>(1.0 - dsp::expDecayCoeff(0.120, sampleRate_));
    for (auto& hp : masterHp_) hp.setCutoff(kMasterHighpassHz, sampleRate_);

    prepared_ = true;
    prepareVoices();
}

void Engine::configure(Quality quality, std::uint32_t unitSeed, float variance) noexcept {
    if (quality == quality_ && unitSeed == unitSeed_ && variance == variance_) return;
    quality_ = quality;
    unitSeed_ = unitSeed;
    variance_ = variance;
    if (prepared_) prepareVoices();
}

void Engine::prepareVoices() noexcept {
    const bool lowRate = sampleRate_ < 88200.0;
    switch (quality_) {
        case Quality::Eco:      oversample_ = 1; break;
        case Quality::Standard: oversample_ = lowRate ? 2 : 1; break;
        case Quality::High:     oversample_ = lowRate ? 4 : 2; break;
    }

    VoiceSetup setup;
    setup.sampleRate = sampleRate_;
    setup.oversample = oversample_;
    setup.variance = variance_;

    // The four metal voices share one oscillator bank in the hardware, so they
    // get the same seed (identical detune and phases). Every other voice draws
    // its own part tolerances.
    const auto seedFor = [this](std::uint32_t voice) { return dsp::hash32(unitSeed_, 0x100U + voice); };
    const std::uint32_t metalSeed = dsp::hash32(unitSeed_, 0x200U);

    setup.unitSeed = seedFor(BD);
    bd_.prepare(setup);
    setup.unitSeed = seedFor(SD);
    sd_.prepare(setup);
    for (int i = 0; i < 6; ++i) {
        const auto which = static_cast<VoiceId>(LT + i);
        setup.unitSeed = seedFor(which);
        toms_[static_cast<std::size_t>(i)].prepare(setup, which);
    }
    setup.unitSeed = seedFor(RS);
    rs_.prepare(setup);
    setup.unitSeed = seedFor(CL);
    cl_.prepare(setup);
    setup.unitSeed = seedFor(CP);
    cp_.prepare(setup);
    setup.unitSeed = seedFor(MA);
    ma_.prepare(setup);

    setup.unitSeed = metalSeed;
    cb_.prepare(setup);
    cy_.prepare(setup);
    hats_.prepare(setup);

    noise_.seed(dsp::hash32(unitSeed_, 0x300U));

    paramsSent_ = false;
    for (auto& c : channels_) c.primed = false;
    masterPrimed_ = false;
    compEnv_ = 0.0f;
    for (auto& hp : masterHp_) hp.reset();
}

void Engine::reset() noexcept {
    bd_.reset();
    sd_.reset();
    for (auto& t : toms_) t.reset();
    rs_.reset();
    cl_.reset();
    cp_.reset();
    ma_.reset();
    cb_.reset();
    cy_.reset();
    hats_.reset();
    for (auto& c : channels_) c.primed = false;
    masterPrimed_ = false;
    compEnv_ = 0.0f;
    for (auto& hp : masterHp_) hp.reset();
}

void Engine::pushVoiceParams() noexcept {
    const auto& p = params_.voice;
    const auto changed = [&](int v) {
        return !paramsSent_ || !sameParams(p[static_cast<std::size_t>(v)], sentParams_[static_cast<std::size_t>(v)]);
    };

    if (changed(BD)) bd_.setParams(p[BD]);
    if (changed(SD)) sd_.setParams(p[SD]);
    for (int i = 0; i < 6; ++i)
        if (changed(LT + i)) toms_[static_cast<std::size_t>(i)].setParams(p[static_cast<std::size_t>(LT + i)]);
    if (changed(RS)) rs_.setParams(p[RS]);
    if (changed(CL)) cl_.setParams(p[CL]);
    if (changed(CP)) cp_.setParams(p[CP]);
    if (changed(MA)) ma_.setParams(p[MA]);
    if (changed(CB)) cb_.setParams(p[CB]);
    if (changed(CY)) cy_.setParams(p[CY]);
    if (changed(OH) || changed(CH)) hats_.setParams(p[OH], p[CH]);

    sentParams_ = p;
    paramsSent_ = true;
}

void Engine::trigger(VoiceId voice, const Hit& hit) noexcept {
    if (!prepared_) return;
    // Parameters must reach a voice before its first trigger.
    if (!paramsSent_) pushVoiceParams();

    switch (voice) {
        case BD: bd_.trigger(hit); break;
        case SD: sd_.trigger(hit); break;
        case LT: case MT: case HT: case LC: case MC: case HC:
            toms_[static_cast<std::size_t>(voice - LT)].trigger(hit);
            break;
        case RS: rs_.trigger(hit); break;
        case CL: cl_.trigger(hit); break;
        case CP: cp_.trigger(hit); break;
        case MA: ma_.trigger(hit); break;
        case CB: cb_.trigger(hit); break;
        case CY: cy_.trigger(hit); break;
        case OH: hats_.triggerOpen(hit); break;
        case CH: hats_.triggerClosed(hit); break;
        default: break;
    }
}

bool Engine::anyVoiceActive() const noexcept {
    if (bd_.isActive() || sd_.isActive() || rs_.isActive() || cl_.isActive() || cp_.isActive() ||
        ma_.isActive() || cb_.isActive() || cy_.isActive() || hats_.isActive())
        return true;
    for (const auto& t : toms_)
        if (t.isActive()) return true;
    return false;
}

void Engine::mixVoice(int voice, const BusBuffers& out, int offset, int n) noexcept {
    const auto v = static_cast<std::size_t>(voice);
    const ChannelParams& ch = params_.channel[v];
    ChannelState& state = channels_[v];
    float* src = (voice == CH) ? voiceBuf2_.data() : voiceBuf_.data();

    // Per-voice overdrive, before the level pot.
    if (ch.drive > 0.0f) {
        const float d = dsp::clamp(ch.drive, 0.0f, 1.0f);
        const float pre = 1.0f + 15.0f * d * d;
        const float post = 1.0f / std::sqrt(pre);
        for (int i = 0; i < n; ++i) src[i] = overdrive(src[i], pre, post);
    }

    // Level pot (audio-ish taper), fixed mix-bus gain, constant-power pan
    // normalised to unity at centre.
    const float level = dsp::clamp(ch.level, 0.0f, 1.0f);
    const float gain = level * level * mixGains()[v] * kOutputScale;
    const float angle = (dsp::clamp(ch.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * static_cast<float>(dsp::kPi);
    const float targetL = gain * 1.41421356f * std::cos(angle);
    const float targetR = gain * 1.41421356f * std::sin(angle);

    if (!state.primed) {
        state.gainL = targetL;
        state.gainR = targetR;
        state.primed = true;
    }

    // Destination: the assigned individual out if it is connected, else main.
    float* dstL = mainL_.data();
    float* dstR = mainR_.data();
    const int bus = ch.output;
    if (bus > 0 && bus < kNumBuses) {
        const auto& pair = out.ch[static_cast<std::size_t>(bus)];
        if (pair[0] != nullptr && pair[1] != nullptr) {
            dstL = pair[0] + offset;
            dstR = pair[1] + offset;
        }
    }

    const bool settled = std::abs(targetL - state.gainL) < 1.0e-6f && std::abs(targetR - state.gainR) < 1.0e-6f;
    if (settled) {
        const float gl = targetL, gr = targetR;
        state.gainL = gl;
        state.gainR = gr;
        for (int i = 0; i < n; ++i) {
            dstL[i] += src[i] * gl;
            dstR[i] += src[i] * gr;
        }
    } else {
        float gl = state.gainL, gr = state.gainR;
        for (int i = 0; i < n; ++i) {
            gl += (targetL - gl) * gainSmooth_;
            gr += (targetR - gr) * gainSmooth_;
            dstL[i] += src[i] * gl;
            dstR[i] += src[i] * gr;
        }
        state.gainL = gl;
        state.gainR = gr;
    }
}

void Engine::processMaster(float* left, float* right, int n) noexcept {
    const MasterParams& m = params_.master;

    const float targetGain = dsp::dbToGain(dsp::clamp(m.volumeDb, -60.0f, 12.0f));
    const float targetDrive = dsp::dbToGain(24.0f * dsp::clamp(m.drive, 0.0f, 1.0f));
    if (!masterPrimed_) {
        masterGain_ = targetGain;
        driveGain_ = targetDrive;
        masterPrimed_ = true;
    }

    if (m.analogOut) {
        // Drive moves rarely: glide it per block, not per sample.
        const float blockSmooth = 1.0f - std::pow(1.0f - gainSmooth_, static_cast<float>(n));
        driveGain_ += (targetDrive - driveGain_) * blockSmooth;
        const float pre = driveGain_ / kSaturationCeiling;
        const float post = kSaturationCeiling / std::pow(driveGain_, 0.75f);
        for (int i = 0; i < n; ++i) {
            const auto l = static_cast<float>(masterHp_[0].highpass(left[i]));
            const auto r = static_cast<float>(masterHp_[1].highpass(right[i]));
            left[i] = dsp::softClip(l * pre) * post;
            right[i] = dsp::softClip(r * pre) * post;
        }
    }

    if (m.comp > 0.0f) {
        const float amount = dsp::clamp(m.comp, 0.0f, 1.0f);
        const float thresholdDb = -30.0f * amount;
        const float slope = 1.0f / kCompRatio - 1.0f;
        const float makeup = dsp::dbToGain(-thresholdDb * (1.0f - 1.0f / kCompRatio) * 0.6f);
        for (int i = 0; i < n; ++i) {
            const float peak = std::max(std::abs(left[i]), std::abs(right[i]));
            compEnv_ += (peak - compEnv_) * (peak > compEnv_ ? compAttack_ : compRelease_);
            const float overDb = dsp::gainToDb(compEnv_) - thresholdDb;
            const float g = overDb > 0.0f ? dsp::dbToGain(overDb * slope) * makeup : makeup;
            left[i] *= g;
            right[i] *= g;
        }
    } else {
        compEnv_ = 0.0f;
    }

    for (int i = 0; i < n; ++i) {
        masterGain_ += (targetGain - masterGain_) * gainSmooth_;
        left[i] *= masterGain_;
        right[i] *= masterGain_;
    }

    if (m.limiter) {
        for (int i = 0; i < n; ++i) {
            left[i] = limit(left[i]);
            right[i] = limit(right[i]);
        }
    }
}

void Engine::render(const BusBuffers& out, int offset, int numSamples) noexcept {
    if (!prepared_ || numSamples <= 0) return;
    const int n = std::min(numSamples, maxBlock_);

    pushVoiceParams();

    // The noise generator free-runs whether or not anything listens to it.
    noise_.fill(noiseBuf_.data(), n);

    std::fill_n(mainL_.data(), n, 0.0f);
    std::fill_n(mainR_.data(), n, 0.0f);
    for (std::size_t b = 1; b < kNumBuses; ++b) {
        const auto& pair = out.ch[b];
        if (pair[0] != nullptr && pair[1] != nullptr) {
            std::fill_n(pair[0] + offset, n, 0.0f);
            std::fill_n(pair[1] + offset, n, 0.0f);
        }
    }

    float* buf = voiceBuf_.data();
    const float* noise = noiseBuf_.data();
    const auto sleep = [this](int v) { channels_[static_cast<std::size_t>(v)].primed = false; };

    if (bd_.isActive()) { bd_.render(buf, n); mixVoice(BD, out, offset, n); } else sleep(BD);
    if (sd_.isActive()) { sd_.render(buf, noise, n); mixVoice(SD, out, offset, n); } else sleep(SD);
    for (int i = 0; i < 6; ++i) {
        auto& tom = toms_[static_cast<std::size_t>(i)];
        if (tom.isActive()) { tom.render(buf, noise, n); mixVoice(LT + i, out, offset, n); } else sleep(LT + i);
    }
    if (rs_.isActive()) { rs_.render(buf, n); mixVoice(RS, out, offset, n); } else sleep(RS);
    if (cl_.isActive()) { cl_.render(buf, n); mixVoice(CL, out, offset, n); } else sleep(CL);
    if (cp_.isActive()) { cp_.render(buf, noise, n); mixVoice(CP, out, offset, n); } else sleep(CP);
    if (ma_.isActive()) { ma_.render(buf, noise, n); mixVoice(MA, out, offset, n); } else sleep(MA);
    // The metal voices share free-running oscillators that keep turning while
    // the voice is silent, so they are always rendered: asleep, render() only
    // advances the oscillator phases and writes zeros, which is cheap.
    if (cb_.isActive()) { cb_.render(buf, n); mixVoice(CB, out, offset, n); } else { cb_.render(buf, n); sleep(CB); }
    if (cy_.isActive()) { cy_.render(buf, n); mixVoice(CY, out, offset, n); } else { cy_.render(buf, n); sleep(CY); }
    if (hats_.isActive()) {
        hats_.render(buf, voiceBuf2_.data(), n);
        mixVoice(OH, out, offset, n);
        mixVoice(CH, out, offset, n);
    } else {
        hats_.render(buf, voiceBuf2_.data(), n);
        sleep(OH);
        sleep(CH);
    }

    processMaster(mainL_.data(), mainR_.data(), n);

    const auto& mainOut = out.ch[0];
    if (mainOut[0] != nullptr) std::copy_n(mainL_.data(), n, mainOut[0] + offset);
    if (mainOut[1] != nullptr) std::copy_n(mainR_.data(), n, mainOut[1] + offset);
}

}  // namespace mw808

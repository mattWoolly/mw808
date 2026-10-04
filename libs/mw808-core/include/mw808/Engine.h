// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The sound engine: 16 voices, the shared noise generator, the mixer with its
// 12 stereo buses and the main-bus output stage. No sequencing, no MIDI — the
// caller triggers voices and asks for audio (see Machine).

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "mw808/Voices.h"
#include "mw808/dsp/Filters.h"
#include "mw808/dsp/Noise.h"
#include "mw808/voices/BassDrum.h"
#include "mw808/voices/Claves.h"
#include "mw808/voices/Cowbell.h"
#include "mw808/voices/Cymbal.h"
#include "mw808/voices/HandClap.h"
#include "mw808/voices/HiHats.h"
#include "mw808/voices/Maracas.h"
#include "mw808/voices/RimShot.h"
#include "mw808/voices/SnareDrum.h"
#include "mw808/voices/TomConga.h"

namespace mw808 {

// Stereo output buses: 0 is the main mix, 1..11 are the individual outs.
inline constexpr int kNumBuses = 12;

enum class Quality : std::uint8_t {
    Eco = 0,   // no oversampling
    Standard,  // metal voices oversampled 2x below 88.2 kHz
    High       // 4x below 88.2 kHz, 2x above
};

// Mixer channel of one voice.
struct ChannelParams {
    float level = 0.8f;       // LEVEL knob, 0..1
    float pan = 0.0f;         // -1 (left) .. +1 (right)
    float drive = 0.0f;       // per-voice overdrive, 0 = clean
    std::uint8_t output = 0;  // bus index; a voice on an individual out leaves the main mix
};

// Main-bus processing. Individual outs bypass all of it.
struct MasterParams {
    float volumeDb = 0.0f;
    bool analogOut = true;  // the hardware output stage: ~48 Hz high-pass and op-amp saturation
    float drive = 0.0f;     // 0..1, pushes the output stage harder (level-compensated)
    float comp = 0.0f;      // 0..1 one-knob bus compressor, 0 = off
    bool limiter = true;    // soft-clip safety limiter at the very end
};

struct EngineParams {
    std::array<VoiceParams, kNumVoices> voice{};
    std::array<ChannelParams, kNumVoices> channel{};
    MasterParams master{};
};

// Destination pointers, [bus][left/right]. A null pair means the bus is not
// connected; voices routed there fall back to the main mix. Bus 0 must be valid.
struct BusBuffers {
    std::array<std::array<float*, 2>, kNumBuses> ch{};
};

class Engine {
public:
    // Allocates scratch buffers. Call from the non-realtime prepare path.
    void prepare(double sampleRate, int maxBlockSize);

    // Realtime-safe. Re-prepares the voices when anything changes (which
    // silences them); a no-op otherwise.
    void configure(Quality quality, std::uint32_t unitSeed, float variance) noexcept;

    // Silence everything and clear all state.
    void reset() noexcept;

    void setParams(const EngineParams& params) noexcept { params_ = params; }
    void trigger(VoiceId voice, const Hit& hit) noexcept;

    // Render numSamples and OVERWRITE every connected bus in [offset, offset + numSamples).
    // numSamples must not exceed the prepared maxBlockSize.
    void render(const BusBuffers& out, int offset, int numSamples) noexcept;

    bool anyVoiceActive() const noexcept;
    double sampleRate() const noexcept { return sampleRate_; }
    int maxBlockSize() const noexcept { return maxBlock_; }
    int oversampleFactor() const noexcept { return oversample_; }

    // Fixed gains of the hardware mix bus (linear), by voice.
    static const std::array<float, kNumVoices>& mixGains() noexcept;

private:
    void prepareVoices() noexcept;
    void pushVoiceParams() noexcept;
    void mixVoice(int voice, const BusBuffers& out, int offset, int n) noexcept;
    void processMaster(float* left, float* right, int n) noexcept;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    bool prepared_ = false;

    Quality quality_ = Quality::Standard;
    std::uint32_t unitSeed_ = 0;
    float variance_ = 0.0f;
    int oversample_ = 1;

    EngineParams params_{};
    std::array<VoiceParams, kNumVoices> sentParams_{};
    bool paramsSent_ = false;

    // Voices.
    voices::BassDrum bd_;
    voices::SnareDrum sd_;
    std::array<voices::TomConga, 6> toms_;  // LT MT HT LC MC HC
    voices::RimShot rs_;
    voices::Claves cl_;
    voices::HandClap cp_;
    voices::Maracas ma_;
    voices::Cowbell cb_;
    voices::Cymbal cy_;
    voices::HiHats hats_;

    dsp::WhiteNoise noise_;

    // Smoothed left/right gain per voice.
    struct ChannelState {
        float gainL = 0.0f, gainR = 0.0f;
        bool primed = false;
    };
    std::array<ChannelState, kNumVoices> channels_{};
    float gainSmooth_ = 1.0f;

    // Master state.
    std::array<dsp::OnePole, 2> masterHp_{};
    float compEnv_ = 0.0f;
    float compAttack_ = 0.0f, compRelease_ = 0.0f;
    float masterGain_ = 1.0f;
    float driveGain_ = 1.0f;
    bool masterPrimed_ = false;

    // Scratch (sized maxBlock_).
    std::vector<float> noiseBuf_, voiceBuf_, voiceBuf2_, mainL_, mainR_;
};

}  // namespace mw808

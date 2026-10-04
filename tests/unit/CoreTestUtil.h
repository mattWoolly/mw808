// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Helpers for the Engine / Machine tests: owned bus storage and block-wise rendering.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "mw808/Machine.h"

namespace coretest {

// Stereo storage for every bus, with a BusBuffers view onto it.
struct Buses {
    explicit Buses(int numSamples, int connected = mw808::kNumBuses) : length(numSamples) {
        for (int b = 0; b < mw808::kNumBuses; ++b)
            for (int c = 0; c < 2; ++c)
                data[b][c].assign(static_cast<std::size_t>(b < connected ? numSamples : 0), 0.0f);
    }

    // View starting `offset` samples in.
    mw808::BusBuffers view(int offset = 0) {
        mw808::BusBuffers v;
        for (int b = 0; b < mw808::kNumBuses; ++b)
            for (int c = 0; c < 2; ++c)
                v.ch[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)] =
                    data[b][c].empty() ? nullptr : data[b][c].data() + offset;
        return v;
    }

    std::vector<float>& left(int bus = 0) { return data[bus][0]; }
    std::vector<float>& right(int bus = 0) { return data[bus][1]; }

    int length;
    std::vector<float> data[mw808::kNumBuses][2];
};

inline float peak(const std::vector<float>& x, int from = 0, int to = -1) {
    const int end = to < 0 ? static_cast<int>(x.size()) : to;
    float p = 0.0f;
    for (int i = from; i < end; ++i) p = std::max(p, std::abs(x[static_cast<std::size_t>(i)]));
    return p;
}

inline bool allFinite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

// Bit-identical buffers (kept out of REQUIRE so a failure does not print them).
inline bool same(const std::vector<float>& a, const std::vector<float>& b) { return a == b; }

// First sample whose magnitude exceeds `threshold`, or -1.
inline int firstAbove(const std::vector<float>& x, float threshold, int from = 0) {
    for (int i = from; i < static_cast<int>(x.size()); ++i)
        if (std::abs(x[static_cast<std::size_t>(i)]) > threshold) return i;
    return -1;
}

// Drive a Machine over `total` samples in blocks of `blockSize`, with a rolling
// host transport and optional MIDI (absolute sample offsets, sorted).
struct Runner {
    mw808::Machine& machine;
    mw808::MachineParams params{};
    double sampleRate = 48000.0;
    double bpm = 120.0;
    bool hostValid = true;
    bool playing = true;
    double startPpq = 0.0;
    std::vector<mw808::MidiMessage> midi{};      // absolute offsets
    std::vector<mw808::MidiMessage> midiOut{};   // collected, absolute offsets

    void run(Buses& buses, int total, int blockSize, int startSample = 0) {
        std::vector<mw808::MidiMessage> blockMidi;
        mw808::MidiOutBuffer out;
        for (int pos = startSample; pos < startSample + total;) {
            const int n = std::min(blockSize, startSample + total - pos);
            blockMidi.clear();
            for (const auto& m : midi)
                if (m.offset >= pos && m.offset < pos + n) {
                    auto local = m;
                    local.offset -= pos;
                    blockMidi.push_back(local);
                }
            mw808::Machine::ProcessArgs args;
            args.transport.hostValid = hostValid;
            args.transport.playing = playing;
            args.transport.bpm = bpm;
            args.transport.ppq = startPpq + static_cast<double>(pos) * bpm / (60.0 * sampleRate);
            args.midiIn = blockMidi.data();
            args.numMidiIn = static_cast<int>(blockMidi.size());
            args.midiOut = &out;
            args.out = buses.view(pos);
            args.numSamples = n;
            machine.process(args, params);
            for (int i = 0; i < out.count; ++i) {
                auto m = out.messages[static_cast<std::size_t>(i)];
                m.offset += pos;
                midiOut.push_back(m);
            }
            pos += n;
        }
    }
};

inline mw808::MidiMessage noteOn(int offset, int note, int velocity = 96, int channel = 10) {
    return {offset, static_cast<std::uint8_t>(0x90 | (channel - 1)), static_cast<std::uint8_t>(note),
            static_cast<std::uint8_t>(velocity)};
}
inline mw808::MidiMessage noteOff(int offset, int note, int channel = 10) {
    return {offset, static_cast<std::uint8_t>(0x80 | (channel - 1)), static_cast<std::uint8_t>(note), 0};
}

}  // namespace coretest

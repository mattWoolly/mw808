// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Shared helpers for the metallic / noise voice tests (cymbal, hi-hats,
// cowbell, hand clap, maracas): rendering adapters that give every voice the
// same face, envelope and spectrum measurements, WAV I/O for the reference
// recordings and the listening renders, and the behaviour checks that every
// voice must pass.

#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mw808/Voices.h"
#include "mw808/dsp/Math.h"
#include "mw808/dsp/Noise.h"
#include "mw808/voices/HiHats.h"

namespace metaltest {

using mw808::Hit;
using mw808::VoiceParams;
using mw808::VoiceSetup;

inline constexpr double kRates[] = {44100.0, 48000.0, 96000.0, 192000.0};

// ---------------------------------------------------------------------------
// Adapters: one interface for every voice under test.
// ---------------------------------------------------------------------------

// A voice with render(out, n).
template <class V>
struct Plain {
    V voice;
    void prepare(const VoiceSetup& s) { voice.prepare(s); }
    void setParams(const VoiceParams& p) { voice.setParams(p); }
    void trigger(const Hit& h) { voice.trigger(h); }
    void render(float* out, int n) { voice.render(out, n); }
    bool isActive() const { return voice.isActive(); }
    void reset() { voice.reset(); }
};

// A voice with render(out, noise, n); owns the "shared" noise generator.
template <class V>
struct Noisy {
    V voice;
    mw808::dsp::WhiteNoise noise;
    std::uint32_t noiseSeed = 12345U;
    std::vector<float> buffer;
    void prepare(const VoiceSetup& s) {
        voice.prepare(s);
        noise.seed(noiseSeed);
    }
    void setParams(const VoiceParams& p) { voice.setParams(p); }
    void trigger(const Hit& h) { voice.trigger(h); }
    void render(float* out, int n) {
        buffer.resize(static_cast<std::size_t>(n));
        noise.fill(buffer.data(), n);
        voice.render(out, buffer.data(), n);
    }
    bool isActive() const { return voice.isActive(); }
    void reset() { voice.reset(); }
};

// One hat of the HiHats pair, as a single voice.
template <bool Open>
struct Hat {
    mw808::voices::HiHats voice;
    VoiceParams other{};
    std::vector<float> scratch;
    void prepare(const VoiceSetup& s) { voice.prepare(s); }
    void setParams(const VoiceParams& p) {
        // keep both hats on the same tune so they share the bank, as in the hardware
        other.tune = p.tune;
        if (Open) voice.setParams(p, other);
        else voice.setParams(other, p);
    }
    void trigger(const Hit& h) {
        if (Open) voice.triggerOpen(h);
        else voice.triggerClosed(h);
    }
    void render(float* out, int n) {
        scratch.resize(static_cast<std::size_t>(n));
        if (Open) voice.render(out, scratch.data(), n);
        else voice.render(scratch.data(), out, n);
    }
    bool isActive() const { return voice.isActive(); }
    void reset() { voice.reset(); }
};
using OpenHat = Hat<true>;
using ClosedHat = Hat<false>;

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

inline VoiceSetup setupFor(double fs, int os = 2, std::uint32_t seed = 0, float variance = 0.0f) {
    return VoiceSetup{fs, os, seed, variance};
}

// Prepare, optionally let `preSeconds` pass (moves the oscillator phases / the
// noise), hit once and render `seconds`.
template <class A>
std::vector<float> renderHit(A& a, const VoiceSetup& s, const VoiceParams& p, Hit hit, double seconds,
                             double preSeconds = 0.0) {
    a.prepare(s);
    a.setParams(p);
    if (preSeconds > 0.0) {
        std::vector<float> pre(static_cast<std::size_t>(preSeconds * s.sampleRate));
        a.render(pre.data(), static_cast<int>(pre.size()));
    }
    a.trigger(hit);
    std::vector<float> out(static_cast<std::size_t>(seconds * s.sampleRate));
    // render in uneven blocks, as a host would
    std::size_t pos = 0;
    int block = 37;
    while (pos < out.size()) {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), out.size() - pos));
        a.render(out.data() + pos, n);
        pos += static_cast<std::size_t>(n);
        block = block == 37 ? 256 : (block == 256 ? 61 : 37);
    }
    return out;
}

// A fresh voice of adapter type A, one hit, `seconds` of output.
template <class A>
std::vector<float> oneHit(double fs, int os, const VoiceParams& p, float volts, double seconds, double pre = 0.0,
                          std::uint32_t seed = 0, float variance = 0.0f) {
    A a;
    return renderHit(a, setupFor(fs, os, seed, variance), p, Hit{volts, 0.0f}, seconds, pre);
}

// Hit times used to average over the free-running oscillator phases (or, for
// the noise voices, over different stretches of the noise).
inline constexpr double kPreRolls[8] = {0.0, 0.0113, 0.0237, 0.0391, 0.0533, 0.0717, 0.0871, 0.1003};

// ---------------------------------------------------------------------------
// Level and envelope
// ---------------------------------------------------------------------------

inline double peakAbs(const std::vector<float>& x) {
    double p = 0.0;
    for (float v : x) p = std::max(p, static_cast<double>(std::abs(v)));
    return p;
}

// Half the peak-to-peak swing: the service manual's Vpp, in output units
// (1.0 = 5 V peak = 10 Vpp).
inline double halfVpp(const std::vector<float>& x) {
    double lo = 0.0, hi = 0.0;
    for (float v : x) {
        lo = std::min(lo, static_cast<double>(v));
        hi = std::max(hi, static_cast<double>(v));
    }
    return 0.5 * (hi - lo);
}

inline bool allFinite(const std::vector<float>& x) {
    for (float v : x)
        if (!std::isfinite(v)) return false;
    return true;
}

inline double toDb(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

// Peak envelope: max |x| in 5 ms windows, 1 ms hop.
inline std::vector<double> peakEnvelope(const std::vector<float>& x, double fs) {
    const auto win = static_cast<std::size_t>(fs * 0.005), hop = static_cast<std::size_t>(fs * 0.001);
    std::vector<double> env;
    for (std::size_t i = 0; i + win < x.size(); i += hop) {
        double p = 0.0;
        for (std::size_t k = i; k < i + win; ++k) p = std::max(p, static_cast<double>(std::abs(x[k])));
        env.push_back(p);
    }
    return env;
}

// The service manual's decay measure: the last time (ms, from the start of
// the buffer) at which the peak envelope is still within `db` of its maximum.
inline double timeToDb(const std::vector<float>& x, double fs, double db) {
    const auto env = peakEnvelope(x, fs);
    if (env.empty()) return 0.0;
    const double peak = *std::max_element(env.begin(), env.end());
    const double thr = peak * std::pow(10.0, db / 20.0);
    std::size_t last = 0;
    for (std::size_t i = 0; i < env.size(); ++i)
        if (env[i] >= thr) last = i;
    return static_cast<double>(last) * (std::floor(fs * 0.001) / fs) * 1000.0;
}

inline double rms(const std::vector<float>& x, double fs, double t0, double t1) {
    const auto a = static_cast<std::size_t>(t0 * fs);
    const auto b = std::min(x.size(), static_cast<std::size_t>(t1 * fs));
    double s = 0.0;
    for (std::size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i];
    return b > a ? std::sqrt(s / static_cast<double>(b - a)) : 0.0;
}

// Mean over several hit times of the half-Vpp level and of the time to `db`.
template <class A>
double meanHalfVpp(double fs, int os, const VoiceParams& p, float volts, double seconds, int hits = 8) {
    double sum = 0.0;
    for (int i = 0; i < hits; ++i) sum += halfVpp(oneHit<A>(fs, os, p, volts, seconds, kPreRolls[i]));
    return sum / hits;
}
template <class A>
double meanTimeToDb(double fs, int os, const VoiceParams& p, float volts, double seconds, double db = -20.0, int hits = 4,
                    std::uint32_t seed = 0, float variance = 0.0f) {
    double sum = 0.0;
    for (int i = 0; i < hits; ++i) sum += timeToDb(oneHit<A>(fs, os, p, volts, seconds, kPreRolls[i], seed, variance), fs, db);
    return sum / hits;
}
// A steadier decay figure for comparing renders (rates, oversampling): the
// time (ms) at which the r.m.s. envelope (5 ms windows, power-averaged over
// `hits` hit times) has fallen `db` below its maximum. The service-manual
// figure above hangs on single peaks, which move with the oscillator phases
// and with how the band-limited waveform happens to be sampled.
template <class A>
double ensembleTimeToDb(double fs, int os, const VoiceParams& p, float volts, double seconds, double db = -20.0, int hits = 8) {
    const auto w = static_cast<std::size_t>(0.005 * fs);
    std::vector<double> env;
    for (int i = 0; i < hits; ++i) {
        const auto x = oneHit<A>(fs, os, p, volts, seconds, kPreRolls[i]);
        env.resize(x.size() / w, 0.0);
        for (std::size_t j = 0; j < env.size(); ++j)
            for (std::size_t k = 0; k < w; ++k) env[j] += static_cast<double>(x[j * w + k]) * x[j * w + k];
    }
    const auto peak = std::max_element(env.begin(), env.end());
    const double thr = *peak * std::pow(10.0, db / 10.0);
    auto last = static_cast<std::size_t>(peak - env.begin());
    for (std::size_t j = last; j < env.size(); ++j)
        if (env[j] >= thr) last = j;
    if (last + 1 >= env.size()) return seconds * 1000.0;
    const double a = std::log10(env[last] / *peak), b = std::log10(env[last + 1] / *peak), t = db / 10.0;
    return (static_cast<double>(last) + 0.5 + (t - a) / (b - a)) * 5.0;
}

// r.m.s. level (dB) of the first `window` seconds, power-averaged over hits.
template <class A>
double meanRmsDb(double fs, int os, const VoiceParams& p, float volts, double window, int hits = 4) {
    double sum = 0.0;
    for (int i = 0; i < hits; ++i) {
        const double r = rms(oneHit<A>(fs, os, p, volts, window + 0.01, kPreRolls[i]), fs, 0.0, window);
        sum += r * r;
    }
    return toDb(std::sqrt(sum / hits));
}
// ---------------------------------------------------------------------------
// Spectra
// ---------------------------------------------------------------------------

inline void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * mw808::dsp::kPi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct Spectrum {
    std::vector<double> power;  // |X|^2 for bins 0..N/2
    double binHz = 1.0;
    double energy(double f0, double f1) const {
        double e = 0.0;
        const auto a = static_cast<std::size_t>(std::max(0.0, f0 / binHz));
        const auto b = std::min(power.size(), static_cast<std::size_t>(f1 / binHz) + 1);
        for (std::size_t i = a; i < b; ++i) e += power[i];
        return e;
    }
    double centroid(double f0, double f1) const {
        double num = 0.0, den = 0.0;
        const auto a = static_cast<std::size_t>(std::max(0.0, f0 / binHz));
        const auto b = std::min(power.size(), static_cast<std::size_t>(f1 / binHz) + 1);
        for (std::size_t i = a; i < b; ++i) {
            num += static_cast<double>(i) * binHz * power[i];
            den += power[i];
        }
        return den > 0.0 ? num / den : 0.0;
    }
    // Frequency of the strongest bin in [f0, f1] after smoothing over +-smoothHz.
    double peakHz(double f0, double f1, double smoothHz) const {
        const auto half = static_cast<std::ptrdiff_t>(std::max(0.0, smoothHz / binHz));
        const auto a = static_cast<std::ptrdiff_t>(f0 / binHz);
        const auto b = std::min(static_cast<std::ptrdiff_t>(power.size()) - 1, static_cast<std::ptrdiff_t>(f1 / binHz));
        double best = -1.0;
        std::ptrdiff_t at = a;
        // running sum
        double sum = 0.0;
        for (std::ptrdiff_t i = a - half; i <= a + half; ++i)
            if (i >= 0 && i < static_cast<std::ptrdiff_t>(power.size())) sum += power[static_cast<std::size_t>(i)];
        for (std::ptrdiff_t i = a; i <= b; ++i) {
            if (sum > best) {
                best = sum;
                at = i;
            }
            const std::ptrdiff_t out = i - half, in = i + half + 1;
            if (out >= 0 && out < static_cast<std::ptrdiff_t>(power.size())) sum -= power[static_cast<std::size_t>(out)];
            if (in >= 0 && in < static_cast<std::ptrdiff_t>(power.size())) sum += power[static_cast<std::size_t>(in)];
        }
        return static_cast<double>(at) * binHz;
    }
};

// Hann-windowed power spectrum of x[t0..t1) (seconds), zero-padded to a power of two.
inline Spectrum spectrumOf(const std::vector<float>& x, double fs, double t0 = 0.0, double t1 = -1.0) {
    const auto a = static_cast<std::size_t>(t0 * fs);
    const auto b = t1 < 0.0 ? x.size() : std::min(x.size(), static_cast<std::size_t>(t1 * fs));
    const std::size_t len = b > a ? b - a : 0;
    std::size_t n = 1;
    while (n < len) n <<= 1;
    std::vector<std::complex<double>> buf(n);
    for (std::size_t i = 0; i < len; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * mw808::dsp::kPi * static_cast<double>(i) / static_cast<double>(len));
        buf[i] = static_cast<double>(x[a + i]) * w;
    }
    fft(buf);
    Spectrum s;
    s.binHz = fs / static_cast<double>(n);
    s.power.resize(n / 2 + 1);
    for (std::size_t i = 0; i <= n / 2; ++i) s.power[i] = std::norm(buf[i]);
    return s;
}

// Sum of power spectra (same length inputs): for noise voices, average over hits.
inline void accumulate(Spectrum& into, const Spectrum& s) {
    if (into.power.empty()) {
        into = s;
        return;
    }
    for (std::size_t i = 0; i < into.power.size() && i < s.power.size(); ++i) into.power[i] += s.power[i];
}

// Power spectrum summed over several hits.
template <class A>
Spectrum meanSpectrum(double fs, int os, const VoiceParams& p, float volts, double seconds, int hits = 4, double t0 = 0.0,
                      double t1 = -1.0) {
    Spectrum sum;
    for (int i = 0; i < hits; ++i) accumulate(sum, spectrumOf(oneHit<A>(fs, os, p, volts, seconds, kPreRolls[i]), fs, t0, t1));
    return sum;
}

// Amplitude of the component at `freq` (Hann-windowed single-bin DFT).
inline double toneAmplitude(const std::vector<float>& x, double fs, double freq, std::size_t from = 0,
                            std::size_t count = 0) {
    const std::size_t n = count == 0 ? x.size() - from : count;
    std::complex<double> acc(0.0, 0.0);
    double wsum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * mw808::dsp::kPi * static_cast<double>(i) / static_cast<double>(n));
        const double ph = 2.0 * mw808::dsp::kPi * freq * static_cast<double>(i) / fs;
        acc += static_cast<double>(x[from + i]) * w * std::complex<double>(std::cos(ph), -std::sin(ph));
        wsum += w;
    }
    return 2.0 * std::abs(acc) / wsum;
}

// ---------------------------------------------------------------------------
// WAV I/O
// ---------------------------------------------------------------------------

struct Wav {
    std::vector<float> x;
    double fs = 0.0;
};

// Minimal reader for the 16-bit mono PCM reference recordings.
inline bool readWav16(const std::string& path, Wav& wav) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 44 || std::string(d.begin(), d.begin() + 4) != "RIFF") return false;
    auto u32 = [&](std::size_t o) {
        return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1]) << 8) |
               (static_cast<std::uint32_t>(d[o + 2]) << 16) | (static_cast<std::uint32_t>(d[o + 3]) << 24);
    };
    auto u16 = [&](std::size_t o) { return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1]) << 8); };
    std::size_t pos = 12;
    std::uint32_t channels = 1, bits = 16;
    while (pos + 8 <= d.size()) {
        const std::string id(d.begin() + static_cast<std::ptrdiff_t>(pos), d.begin() + static_cast<std::ptrdiff_t>(pos + 4));
        const std::size_t size = u32(pos + 4);
        if (id == "fmt ") {
            channels = u16(pos + 10);
            wav.fs = static_cast<double>(u32(pos + 12));
            bits = u16(pos + 22);
        } else if (id == "data") {
            if (bits != 16 || channels == 0) return false;
            const std::size_t end = std::min(d.size(), pos + 8 + size);
            for (std::size_t i = pos + 8; i + 2 * channels <= end; i += 2 * channels) {
                const auto s = static_cast<std::int16_t>(u16(i));
                wav.x.push_back(static_cast<float>(s) / 32768.0f);
            }
            return !wav.x.empty();
        }
        pos += 8 + size + (size & 1U);
    }
    return false;
}

inline void writeWav16(const std::string& path, const std::vector<float>& x, double fs, double gain = 1.0) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(x.size() * 2);
    f.write("RIFF", 4);
    u32(36 + bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(static_cast<std::uint32_t>(fs));
    u32(static_cast<std::uint32_t>(fs) * 2);
    u16(2);
    u16(16);
    f.write("data", 4);
    u32(bytes);
    for (float v : x) {
        const double c = std::max(-1.0, std::min(1.0, static_cast<double>(v) * gain));
        u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lrint(c * 32767.0))));
    }
}

// Listening renders are written 6 dB down: accented cowbell and clap hits reach
// 1.2 (12 Vpp) and more on noise peaks, which would clip a 16-bit file at unity.
inline constexpr double kRenderGain = 0.5;

inline std::string refDir() { return MW808_REF_SAMPLES_DIR; }
inline bool haveRef(const std::string& relative) { return std::filesystem::exists(refDir() + "/" + relative); }
inline std::string renderDir() {
    const std::string dir = std::string(MW808_SOURCE_DIR) + "/build/agent-metal/renders";
    std::filesystem::create_directories(dir);
    return dir;
}

// Append `seconds` of a hit (prepared voice) to `out`.
template <class A>
void appendHit(A& a, std::vector<float>& out, double fs, Hit hit, double seconds) {
    a.trigger(hit);
    const std::size_t n = static_cast<std::size_t>(seconds * fs), at = out.size();
    out.resize(at + n);
    a.render(out.data() + at, static_cast<int>(n));
}

// ---------------------------------------------------------------------------
// Checks every voice must pass
// ---------------------------------------------------------------------------

struct Lcg {
    std::uint32_t s;
    explicit Lcg(std::uint32_t seed) : s(seed) {}
    std::uint32_t next() { return s = s * 1664525U + 1013904223U; }
    float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
};

inline VoiceParams randomParams(Lcg& r, bool extremes) {
    auto pick = [&](float lo, float hi) {
        if (!extremes) return r.range(lo, hi);
        return r.unit() < 0.5f ? lo : hi;
    };
    return VoiceParams{pick(-12.0f, 12.0f), pick(0.0f, 1.0f), pick(0.0f, 1.0f), pick(0.0f, 1.0f)};
}

// Robustness: random parameters and triggers (including a trigger on every
// sample for one second, and parameters at their extremes) must stay finite
// and bounded; then the voice must fall silent and stay exactly silent.
template <class A>
void checkRobust(double fs, int os, double maxSleepSeconds) {
    A a;
    a.prepare(setupFor(fs, os, 99U, 1.0f));
    Lcg rng(0xC0FFEEU + static_cast<std::uint32_t>(fs) + static_cast<std::uint32_t>(os));
    std::vector<float> buf(512);
    double worst = 0.0;
    bool finite = true;
    auto run = [&](int n) {
        a.render(buf.data(), n);
        for (int i = 0; i < n; ++i) {
            finite = finite && std::isfinite(buf[static_cast<std::size_t>(i)]);
            worst = std::max(worst, static_cast<double>(std::abs(buf[static_cast<std::size_t>(i)])));
        }
    };
    // 1. random parameters, random trigger times and heights
    for (int block = 0; block < 400; ++block) {
        if (rng.unit() < 0.3f) a.setParams(randomParams(rng, rng.unit() < 0.3f));
        if (rng.unit() < 0.4f) a.trigger(Hit{rng.range(0.01f, 14.0f), rng.range(-12.0f, 12.0f)});
        run(1 + static_cast<int>(rng.next() % 500U));
    }
    // 2. a trigger on every sample for one second, at full accent, parameters at extremes
    for (int corner = 0; corner < 2; ++corner) {
        a.setParams(corner == 0 ? VoiceParams{12.0f, 1.0f, 1.0f, 1.0f} : VoiceParams{-12.0f, 0.0f, 0.0f, 0.0f});
        for (int i = 0; i < static_cast<int>(fs / 2); ++i) {
            a.trigger(Hit{14.0f, 0.0f});
            run(1);
        }
    }
    a.setParams(VoiceParams{12.0f, 1.0f, 1.0f, 1.0f});
    for (int i = 0; i < 2000; ++i) {
        a.trigger(Hit{14.0f, 0.0f});
        run(1);
    }
    INFO("fs " << fs << " os " << os << " worst |x| " << worst);
    REQUIRE(finite);
    REQUIRE(worst < 4.0);

    // 3. silence: must go inactive within the bound, and then output exact zeros
    double waited = 0.0;
    while (a.isActive() && waited < maxSleepSeconds) {
        run(512);
        waited += 512.0 / fs;
    }
    INFO("went inactive after " << waited << " s");
    REQUIRE(finite);
    REQUIRE(worst < 4.0);
    REQUIRE_FALSE(a.isActive());
    for (int k = 0; k < 4; ++k) {
        a.render(buf.data(), 512);
        for (float v : buf) REQUIRE(v == 0.0f);
    }
    REQUIRE_FALSE(a.isActive());
}

// Parameter hygiene: a NaN control value takes the stock value, a NaN
// trigger height is a normal hit, infinities are clamped; nothing poisons
// the voice for good (one NaN used to leave a voice's filters NaN forever).
template <class A>
void checkNanSafe(double fs, int os) {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    auto render = [&](const VoiceParams& p, Hit h) {
        A a;
        a.prepare(setupFor(fs, os));
        a.setParams(p);
        a.trigger(h);
        std::vector<float> out(static_cast<std::size_t>(0.1 * fs));
        a.render(out.data(), static_cast<int>(out.size()));
        return out;
    };
    const auto stock = render(VoiceParams{}, Hit::normal());
    REQUIRE(render(VoiceParams{nan, nan, nan, nan}, Hit::normal()) == stock);
    REQUIRE(render(VoiceParams{}, Hit{nan, nan}) == stock);
    // infinities are the ends of the ranges
    REQUIRE(render(VoiceParams{inf, inf, inf, inf}, Hit{inf, inf}) == render(VoiceParams{24.0f, 1.0f, 1.0f, 1.0f}, Hit{14.0f, 0.0f}));
    REQUIRE(render(VoiceParams{-inf, -inf, -inf, -inf}, Hit{-inf, -inf}) == render(VoiceParams{-24.0f, 0.0f, 0.0f, 0.0f}, Hit{0.0f, 0.0f}));
    // garbage on every sample, then a normal life: finite, bounded, falls silent
    A a;
    a.prepare(setupFor(fs, os, 7U, 1.0f));
    const float junk[] = {nan, inf, -inf, 1.0e30f, -1.0e30f, 1.0e-40f, -0.0f, 36.0f, -36.0f};
    std::vector<float> one(1);
    bool finite = true;
    double worst = 0.0;
    for (int i = 0; i < static_cast<int>(fs / 4); ++i) {
        const float j = junk[static_cast<std::size_t>(i) % 9U], k = junk[static_cast<std::size_t>(i / 9) % 9U];
        a.setParams(VoiceParams{j, k, j, k});
        if (i % 3 == 0) a.trigger(Hit{k, j});
        a.render(one.data(), 1);
        finite = finite && std::isfinite(one[0]);
        worst = std::max(worst, static_cast<double>(std::abs(one[0])));
    }
    // Tune flipping between +-24 semitones on every sample (far outside the
    // contract: -12..+12, setParams once per sub-block) pumps the filters at
    // oversample 1; the output must stay finite and bounded, not quiet.
    INFO("fs " << fs << " os " << os << " worst |x| with junk on every sample " << worst);
    REQUIRE(finite);
    REQUIRE(worst < 32.0);
    worst = 0.0;
    a.setParams(VoiceParams{});
    a.trigger(Hit::normal());
    std::vector<float> buf(512);
    for (int b = 0; b < 200; ++b) {
        a.render(buf.data(), 512);
        for (float v : buf) {
            finite = finite && std::isfinite(v);
            worst = std::max(worst, static_cast<double>(std::abs(v)));
        }
    }
    INFO("fs " << fs << " os " << os << " worst |x| " << worst);
    REQUIRE(finite);
    REQUIRE(worst < 4.0);
    double waited = 0.0;
    while (a.isActive() && waited < 30.0) {
        a.render(buf.data(), 512);
        waited += 512.0 / fs;
    }
    REQUIRE_FALSE(a.isActive());
}

// Determinism and unit variance (see each voice's own test for the pitch /
// decay numbers): same seed and calls => identical samples; variance 0 =>
// independent of the seed; variance 1 => different seeds differ.
template <class A>
void checkDeterminism(double seconds) {
    const VoiceParams p{};
    auto render = [&](std::uint32_t seed, float variance) {
        A a;
        a.prepare(setupFor(48000.0, 2, seed, variance));
        a.setParams(p);
        std::vector<float> out(static_cast<std::size_t>(seconds * 48000.0));
        std::vector<float> pre(1000);
        a.render(pre.data(), 1000);
        a.trigger(Hit::normal());
        a.render(out.data(), static_cast<int>(out.size() / 2));
        a.trigger(Hit::accented(0.7f));
        a.render(out.data() + out.size() / 2, static_cast<int>(out.size() - out.size() / 2));
        return out;
    };
    const auto a1 = render(7U, 1.0f), a2 = render(7U, 1.0f);
    REQUIRE(a1 == a2);
    const auto n1 = render(1U, 0.0f), n2 = render(4242U, 0.0f);
    REQUIRE(n1 == n2);
    const auto b = render(8U, 1.0f);
    REQUIRE(a1 != b);
    REQUIRE(a1 != n1);
    REQUIRE(allFinite(a1));
}

// Retrigger: the second hit lands on a circuit that is still ringing, so two
// close hits differ from one hit summed with a delayed copy of itself; and the
// retrigger makes no step larger than the attack of a hit from silence.
template <class A>
void checkRetrigger(double gapSeconds, double seconds, const VoiceParams& p = VoiceParams{}) {
    const double fs = 48000.0;
    const auto gap = static_cast<std::size_t>(gapSeconds * fs);
    const auto n = static_cast<std::size_t>(seconds * fs);
    const VoiceSetup s = setupFor(fs, 2);

    A one;
    const std::vector<float> single = renderHit(one, s, p, Hit::normal(), seconds);

    A two;
    two.prepare(s);
    two.setParams(p);
    std::vector<float> twice(n);
    two.trigger(Hit::normal());
    two.render(twice.data(), static_cast<int>(gap));
    two.trigger(Hit::normal());
    two.render(twice.data() + gap, static_cast<int>(n - gap));

    double diff = 0.0, ref = 0.0, stepRetrig = 0.0, stepAttack = 0.0;
    for (std::size_t i = gap; i < n; ++i) {
        const double summed = static_cast<double>(single[i]) + single[i - gap];
        diff += (twice[i] - summed) * (twice[i] - summed);
        ref += summed * summed;
    }
    const auto window = static_cast<std::size_t>(0.004 * fs);
    for (std::size_t i = 1; i < window; ++i) {
        stepAttack = std::max(stepAttack, static_cast<double>(std::abs(single[i] - single[i - 1])));
        stepRetrig = std::max(stepRetrig, static_cast<double>(std::abs(twice[gap + i] - twice[gap + i - 1])));
    }
    // largest sample-to-sample step anywhere in the single hit: the "natural" ceiling
    double stepAny = 0.0;
    for (std::size_t i = 1; i < n; ++i) stepAny = std::max(stepAny, static_cast<double>(std::abs(single[i] - single[i - 1])));
    INFO("state carry-over: difference / sum energy = " << diff / ref << ", retrigger step " << stepRetrig
                                                        << " vs attack step " << stepAttack << " (max any " << stepAny << ")");
    REQUIRE(diff > 1.0e-4 * ref);              // not a reset-and-replay
    REQUIRE(stepRetrig <= 1.5 * stepAny + 1.0e-6);  // no click beyond the natural attack
    REQUIRE(allFinite(twice));
}

// Level rises monotonically with the trigger height from 0.5 V to 14 V.
// `levelOf(volts)` returns the (averaged) half-Vpp for that hit.
template <class F>
void checkMonotonicLevel(F&& levelOf) {
    const float volts[] = {0.5f, 1.0f, 1.5f, 2.2f, 3.0f, 4.0f, 5.5f, 7.0f, 9.0f, 11.0f, 14.0f};
    double last = 0.0;
    for (float v : volts) {
        const double level = levelOf(v);
        INFO("volts " << v << " level " << level << " previous " << last);
        REQUIRE(level > last);
        last = level;
    }
}

// Delay of b relative to a, in samples (cross-correlation peak within +-12
// samples, refined by a parabola through the three best lags).
inline double measuredLag(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    double best = -1.0e300;
    int bestLag = 0;
    double c[25] = {};
    for (int lag = -12; lag <= 12; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 12; i + 12 < n; ++i)
            sum += static_cast<double>(a[i]) * static_cast<double>(b[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i) + lag)]);
        c[lag + 12] = sum;
        if (sum > best) {
            best = sum;
            bestLag = lag;
        }
    }
    if (bestLag == -12 || bestLag == 12) return bestLag;
    const double l = c[bestLag + 11], m = c[bestLag + 12], r = c[bestLag + 13];
    return bestLag + 0.5 * (l - r) / (l - 2.0 * m + r);
}

// ns per output sample while sounding (retriggered every 100 ms).
template <class A>
double benchNsPerSample(double fs, int os, const VoiceParams& p) {
    A a;
    a.prepare(setupFor(fs, os));
    a.setParams(p);
    std::vector<float> buf(256);
    const int blocks = static_cast<int>(fs * 4.0 / 256.0);
    const int perHit = static_cast<int>(fs * 0.1 / 256.0);
    double best = 1.0e30;
    for (int rep = 0; rep < 5; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            if (b % perHit == 0) a.trigger(Hit::accented(1.0f));
            a.render(buf.data(), 256);
        }
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / (blocks * 256.0));
    }
    return best;
}

}  // namespace metaltest

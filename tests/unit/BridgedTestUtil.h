// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Shared helpers for the bridged-T voice tests (bass drum, snare, toms/congas,
// rim shot, claves): a render rig that drives any voice sample-accurately,
// signal measurements (pitch, decay, level, spectrum), WAV I/O for the
// reference recordings and listening renders, and the test bodies that are the
// same for every voice (robustness fuzz, determinism, sleep).

#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "mw808/Voices.h"
#include "mw808/dsp/Math.h"
#include "mw808/dsp/Noise.h"

namespace bridged_test {

using mw808::Hit;
using mw808::VoiceParams;
using mw808::VoiceSetup;

inline constexpr double kRates[] = {44100.0, 48000.0, 96000.0, 192000.0};
inline constexpr std::uint32_t kNoiseSeed = 0x808U;

// ---------------------------------------------------------------- render rig

template <class V>
void renderBlock(V& voice, float* out, const float* noise, int n) {
    if constexpr (requires { voice.render(out, noise, n); }) {
        voice.render(out, noise, n);
    } else {
        (void)noise;
        voice.render(out, n);
    }
}

struct Event {
    double seconds;
    Hit hit;
};

inline VoiceSetup setupAt(double fs, std::uint32_t unitSeed = 0, float variance = 0.0f) {
    VoiceSetup s;
    s.sampleRate = fs;
    s.oversample = 1;
    s.unitSeed = unitSeed;
    s.variance = variance;
    return s;
}

// A prepared voice plus the shared noise bus. `prep(voice, setup)` prepares it
// (the tom/conga class needs its VoiceId there).
template <class V>
struct Rig {
    V voice;
    mw808::dsp::WhiteNoise bus;
    std::vector<float> noise;
    double fs = 48000.0;

    template <class Prep>
    Rig(Prep&& prep, const VoiceSetup& setup, std::uint32_t noiseSeed = kNoiseSeed) : fs(setup.sampleRate) {
        prep(voice, setup);
        bus.seed(noiseSeed);
    }

    // Append n samples to `out`, in blocks of at most `block`.
    void run(std::vector<float>& out, int n, int block = 64) {
        const std::size_t start = out.size();
        out.resize(start + static_cast<std::size_t>(n));
        noise.resize(static_cast<std::size_t>(std::max(block, 1)));
        int done = 0;
        while (done < n) {
            const int m = std::min(block, n - done);
            bus.fill(noise.data(), m);
            renderBlock(voice, out.data() + start + static_cast<std::size_t>(done), noise.data(), m);
            done += m;
        }
    }
};

// One-shot render: prepare, set params, fire the events at their sample
// positions, render `seconds`.
template <class V, class Prep>
std::vector<float> render(Prep&& prep, const VoiceSetup& setup, const VoiceParams& params,
                          const std::vector<Event>& events, double seconds) {
    Rig<V> rig(prep, setup);
    rig.voice.setParams(params);
    std::vector<float> out;
    const int total = static_cast<int>(std::lround(seconds * setup.sampleRate));
    out.reserve(static_cast<std::size_t>(total));
    int pos = 0;
    for (const Event& e : events) {
        const int at = std::min(total, static_cast<int>(std::lround(e.seconds * setup.sampleRate)));
        if (at > pos) {
            rig.run(out, at - pos);
            pos = at;
        }
        rig.voice.trigger(e.hit);
    }
    if (total > pos) rig.run(out, total - pos);
    return out;
}

template <class V, class Prep>
std::vector<float> renderHit(Prep&& prep, double fs, const VoiceParams& params, float volts,
                             double seconds, float pitchSemis = 0.0f) {
    return render<V>(prep, setupAt(fs), params, {{0.0, Hit{volts, pitchSemis}}}, seconds);
}

// ------------------------------------------------------------- measurements

inline bool allFinite(const std::vector<float>& x) {
    for (float v : x)
        if (!std::isfinite(v)) return false;
    return true;
}

inline double peakAbs(const std::vector<float>& x) {
    double p = 0.0;
    for (float v : x) p = std::max(p, static_cast<double>(std::abs(v)));
    return p;
}

// Peak-to-peak in volts at the voice output (1.0 == 5 V).
inline double vpp(const std::vector<float>& x) {
    if (x.empty()) return 0.0;
    const auto [lo, hi] = std::minmax_element(x.begin(), x.end());
    return (static_cast<double>(*hi) - static_cast<double>(*lo)) * static_cast<double>(mw808::kVoltsPerUnit);
}

inline double rms(const std::vector<float>& x, double fs, double t0, double t1) {
    const std::size_t a = static_cast<std::size_t>(t0 * fs);
    const std::size_t b = std::min(x.size(), static_cast<std::size_t>(t1 * fs));
    double s = 0.0;
    for (std::size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i];
    return b > a ? std::sqrt(s / static_cast<double>(b - a)) : 0.0;
}

// Mean frequency from upward zero crossings between t0 and t1 (linear
// interpolation of each crossing). 0 if fewer than two crossings.
inline double zeroCrossFreq(const std::vector<float>& x, double fs, double t0, double t1) {
    const std::size_t a = std::max<std::size_t>(1, static_cast<std::size_t>(t0 * fs));
    const std::size_t b = std::min(x.size(), static_cast<std::size_t>(t1 * fs));
    double first = -1.0, last = -1.0;
    int count = 0;
    for (std::size_t i = a; i < b; ++i) {
        const double p = x[i - 1], q = x[i];
        if (p < 0.0 && q >= 0.0) {
            const double t = static_cast<double>(i - 1) + p / (p - q);
            if (first < 0.0) first = t;
            last = t;
            ++count;
        }
    }
    return count > 1 ? (count - 1) * fs / (last - first) : 0.0;
}

// Magnitude of the Hann-windowed DTFT of x[a, b) at `freq`.
inline double dtftMagnitude(const std::vector<float>& x, double fs, std::size_t a, std::size_t b, double freq) {
    const double w = mw808::dsp::kTwoPi * freq / fs;
    const double n = static_cast<double>(b - a);
    std::complex<double> acc = 0.0;
    const std::complex<double> rot = std::polar(1.0, -w);
    std::complex<double> ph = 1.0;
    for (std::size_t i = a; i < b; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(mw808::dsp::kTwoPi * static_cast<double>(i - a) / n);
        acc += ph * (hann * static_cast<double>(x[i]));
        ph *= rot;
    }
    return std::abs(acc) / n;
}

// Frequency of the strongest spectral peak in [fLo, fHi], over [t0, t1).
inline double peakFreq(const std::vector<float>& x, double fs, double fLo, double fHi, double t0, double t1) {
    const std::size_t a = static_cast<std::size_t>(t0 * fs);
    const std::size_t b = std::min(x.size(), static_cast<std::size_t>(t1 * fs));
    if (b <= a + 8) return 0.0;
    double best = fLo, bestMag = -1.0;
    const int coarse = 240;
    for (int i = 0; i <= coarse; ++i) {
        const double f = fLo + (fHi - fLo) * i / coarse;
        const double m = dtftMagnitude(x, fs, a, b, f);
        if (m > bestMag) {
            bestMag = m;
            best = f;
        }
    }
    double span = (fHi - fLo) / coarse;
    for (int pass = 0; pass < 4; ++pass) {
        const double lo = best - span, hi = best + span;
        for (int i = 0; i <= 20; ++i) {
            const double f = lo + (hi - lo) * i / 20;
            const double m = dtftMagnitude(x, fs, a, b, f);
            if (m > bestMag) {
                bestMag = m;
                best = f;
            }
        }
        span /= 10.0;
    }
    return best;
}

// Peak envelope: maximum |x| in each consecutive window of `windowSeconds`.
struct Envelope {
    std::vector<double> t;   // window centre, seconds
    std::vector<double> pk;  // peak magnitude
};

inline Envelope peakEnvelope(const std::vector<float>& x, double fs, double windowSeconds) {
    Envelope e;
    const std::size_t w = std::max<std::size_t>(1, static_cast<std::size_t>(windowSeconds * fs));
    for (std::size_t i = 0; i + w <= x.size(); i += w) {
        double p = 0.0;
        for (std::size_t j = i; j < i + w; ++j) p = std::max(p, static_cast<double>(std::abs(x[j])));
        e.t.push_back((static_cast<double>(i) + 0.5 * static_cast<double>(w)) / fs);
        e.pk.push_back(p);
    }
    return e;
}

// The service manual's decay time: from the start of the waveform until the
// envelope has fallen to 1/10 of its peak (-20 dB). The envelope is the peak
// hold of |x| (the last sample that still reaches 1/10 of the peak), so no
// window is needed; the argument is kept for call-site symmetry with t60Fit.
inline double timeToMinus20dB(const std::vector<float>& x, double fs, [[maybe_unused]] double windowSeconds) {
    const double peak = peakAbs(x);
    if (peak <= 0.0) return 0.0;
    std::size_t onset = 0;
    while (onset < x.size() && std::abs(x[onset]) < 1.0e-3 * peak) ++onset;
    // Last sample that still reaches 1/10 of the peak.
    std::size_t last = onset;
    for (std::size_t i = onset; i < x.size(); ++i)
        if (std::abs(x[i]) >= 0.1 * peak) last = i;
    return static_cast<double>(last - onset) / fs;
}

// T60 from a straight-line fit to the dB peak envelope between -3 and -45 dB
// below its maximum.
inline double t60Fit(const std::vector<float>& x, double fs, double windowSeconds, double hiDb = -3.0, double loDb = -45.0) {
    const Envelope e = peakEnvelope(x, fs, windowSeconds);
    if (e.pk.empty()) return 0.0;
    const std::size_t im = static_cast<std::size_t>(std::max_element(e.pk.begin(), e.pk.end()) - e.pk.begin());
    const double ref = e.pk[im];
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (std::size_t i = im; i < e.pk.size(); ++i) {
        if (e.pk[i] <= 0.0) break;
        const double db = 20.0 * std::log10(e.pk[i] / ref);
        if (db < loDb) break;
        if (db > hiDb) continue;
        sx += e.t[i];
        sy += db;
        sxx += e.t[i] * e.t[i];
        sxy += e.t[i] * db;
        ++n;
    }
    if (n < 3) return 0.0;
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return slope < 0.0 ? -60.0 / slope : 0.0;
}

// In-place radix-2 FFT.
inline void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -mw808::dsp::kTwoPi / static_cast<double>(len));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w = 1.0;
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Power spectrum of x[t0, t1) (Hann window, zero-padded to a power of two).
struct Spectrum {
    std::vector<double> power;
    double binHz = 0.0;
    double bandPower(double lo, double hi) const {
        double s = 0.0;
        for (std::size_t i = 0; i < power.size(); ++i) {
            const double f = static_cast<double>(i) * binHz;
            if (f >= lo && f < hi) s += power[i];
        }
        return s;
    }
    double centroid(double lo = 20.0, double hi = 20000.0) const {
        double num = 0.0, den = 0.0;
        for (std::size_t i = 0; i < power.size(); ++i) {
            const double f = static_cast<double>(i) * binHz;
            if (f >= lo && f < hi) {
                num += f * power[i];
                den += power[i];
            }
        }
        return den > 0.0 ? num / den : 0.0;
    }
};

inline Spectrum spectrum(const std::vector<float>& x, double fs, double t0, double t1) {
    const std::size_t a = static_cast<std::size_t>(t0 * fs);
    const std::size_t b = std::min(x.size(), static_cast<std::size_t>(t1 * fs));
    Spectrum s;
    if (b <= a) return s;
    std::size_t n = 1;
    while (n < b - a) n <<= 1;
    std::vector<std::complex<double>> buf(n);
    const double len = static_cast<double>(b - a);
    for (std::size_t i = a; i < b; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(mw808::dsp::kTwoPi * static_cast<double>(i - a) / len);
        buf[i - a] = hann * static_cast<double>(x[i]);
    }
    fft(buf);
    s.power.resize(n / 2);
    for (std::size_t i = 0; i < n / 2; ++i) s.power[i] = std::norm(buf[i]);
    s.binHz = fs / static_cast<double>(n);
    return s;
}

inline double db(double ratio) { return 10.0 * std::log10(std::max(ratio, 1.0e-30)); }

// Largest sample-to-sample step in [t0, t1).
inline double maxStep(const std::vector<float>& x, double fs, double t0, double t1) {
    const std::size_t a = std::max<std::size_t>(1, static_cast<std::size_t>(t0 * fs));
    const std::size_t b = std::min(x.size(), static_cast<std::size_t>(t1 * fs));
    double m = 0.0;
    for (std::size_t i = a; i < b; ++i)
        m = std::max(m, std::abs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1])));
    return m;
}

// ------------------------------------------------------------------ WAV I/O

struct Wav {
    double fs = 0.0;
    std::vector<float> x;
};

// 16-bit PCM, first channel. Tolerates a truncated final byte (the reference
// set has a few).
inline bool readWav(const std::string& path, Wav& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 44 || std::string(d.begin(), d.begin() + 4) != "RIFF") return false;
    auto u16 = [&d](std::size_t p) { return static_cast<unsigned>(d[p] | (d[p + 1] << 8)); };
    auto u32 = [&d](std::size_t p) {
        return static_cast<std::uint32_t>(d[p]) | (static_cast<std::uint32_t>(d[p + 1]) << 8) |
               (static_cast<std::uint32_t>(d[p + 2]) << 16) | (static_cast<std::uint32_t>(d[p + 3]) << 24);
    };
    unsigned channels = 1, bits = 16;
    std::size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const std::string id(d.begin() + static_cast<std::ptrdiff_t>(pos), d.begin() + static_cast<std::ptrdiff_t>(pos) + 4);
        const std::size_t len = u32(pos + 4);
        const std::size_t body = pos + 8;
        if (id == "fmt " && body + 16 <= d.size()) {
            channels = u16(body + 2);
            out.fs = static_cast<double>(u32(body + 4));
            bits = u16(body + 14);
        } else if (id == "data") {
            if (bits != 16 || channels == 0) return false;
            const std::size_t end = std::min(d.size(), body + len);
            const std::size_t frame = 2 * channels;
            for (std::size_t p = body; p + frame <= end; p += frame) {
                const auto s = static_cast<std::int16_t>(u16(p));
                out.x.push_back(static_cast<float>(s) / 32768.0f);
            }
            return !out.x.empty();
        }
        pos = body + len + (len & 1U);
    }
    return false;
}

// 32-bit float mono (levels above 1.0 survive).
inline bool writeWav(const std::string& path, const std::vector<float>& x, double fs) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto put32 = [&f](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto put16 = [&f](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(x.size() * 4);
    f.write("RIFF", 4);
    put32(36 + bytes);
    f.write("WAVEfmt ", 8);
    put32(16);
    put16(3);
    put16(1);
    put32(static_cast<std::uint32_t>(fs));
    put32(static_cast<std::uint32_t>(fs) * 4);
    put16(4);
    put16(32);
    f.write("data", 4);
    put32(bytes);
    f.write(reinterpret_cast<const char*>(x.data()), static_cast<std::streamsize>(bytes));
    return static_cast<bool>(f);
}

// Reference recordings (CC0, test data only). Empty string if absent.
inline std::string refPath(const std::string& dir, const std::string& file) {
    const std::filesystem::path p = std::filesystem::path(MW808_REF_SAMPLES_DIR) / dir / file;
    std::error_code ec;
    return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

inline std::string renderDir() {
    const char* env = std::getenv("MW808_RENDER_DIR");
    const std::filesystem::path p =
        env != nullptr ? std::filesystem::path(env)
                       : std::filesystem::path(MW808_SOURCE_DIR) / "build" / "agent-bridged" / "renders";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

// Concatenate renders with a gap, for listening.
inline void appendWithGap(std::vector<float>& all, const std::vector<float>& x, double fs, double gapSeconds = 0.15) {
    all.insert(all.end(), x.begin(), x.end());
    all.insert(all.end(), static_cast<std::size_t>(gapSeconds * fs), 0.0f);
}

// -------------------------------------------------- test bodies shared by voices

struct Rng {
    std::uint32_t s;
    std::uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uni() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }  // [0, 1)
    float range(float lo, float hi) { return lo + (hi - lo) * uni(); }
};

inline VoiceParams randomParams(Rng& rng) {
    auto knob = [&rng]() {
        const float u = rng.uni();
        if (u < 0.15f) return 0.0f;
        if (u > 0.85f) return 1.0f;
        return rng.uni();
    };
    VoiceParams p;
    const float u = rng.uni();
    p.tune = u < 0.15f ? -12.0f : (u > 0.85f ? 12.0f : rng.range(-12.0f, 12.0f));
    p.decay = knob();
    p.tone = knob();
    p.shape = knob();
    return p;
}

// No op-amp in these circuits swings past its +-15 V supply: 3.0 (15 V) bounds
// every voice output whatever it is fed.
inline constexpr double kRailBound = 3.0;

// Random parameters and trigger times/volts, then a trigger on every sample for
// one second with the knobs at extremes, then parameters AND triggers changing
// on every sample, then silence. Every sample must be finite and within the
// rails; the voice must go to sleep within `maxTailSeconds` and then render
// exact zeros.
template <class V, class Prep>
void fuzzRobustness(Prep&& prep, double fs, std::uint32_t seed, double maxTailSeconds, float variance = 0.0f) {
    Rng rng{seed | 1U};
    Rig<V> rig(prep, setupAt(fs, seed, variance));
    std::vector<float> out;
    double worst = 0.0;
    bool finite = true;
    auto check = [&]() {
        finite = finite && allFinite(out);
        worst = std::max(worst, peakAbs(out));
        out.clear();
    };

    // 1. Random playing: 2 s.
    int remaining = static_cast<int>(2.0 * fs);
    while (remaining > 0) {
        rig.voice.setParams(randomParams(rng));
        const int hits = static_cast<int>(rng.next() % 4U);
        for (int h = 0; h < hits; ++h) {
            Hit hit;
            hit.volts = rng.uni() < 0.2f ? 14.0f : rng.range(0.01f, 14.0f);
            hit.pitchSemis = rng.uni() < 0.5f ? 0.0f : rng.range(-36.0f, 36.0f);
            rig.voice.trigger(hit);
            rig.run(out, 1 + static_cast<int>(rng.next() % 64U), 1 + static_cast<int>(rng.next() % 32U));
        }
        const int n = 1 + static_cast<int>(rng.next() % 2048U);
        rig.run(out, n, 1 + static_cast<int>(rng.next() % 256U));
        remaining -= n;
        check();
    }

    // 2. A trigger on every sample for a second, at each knob extreme.
    const VoiceParams extremes[] = {
        {12.0f, 1.0f, 1.0f, 1.0f}, {-12.0f, 1.0f, 0.0f, 1.0f}, {12.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.5f, 1.0f}};
    for (const VoiceParams& p : extremes) {
        rig.voice.setParams(p);
        const int n = static_cast<int>(0.25 * fs);
        for (int i = 0; i < n; ++i) {
            Hit hit;
            hit.volts = (i % 3 == 0) ? 14.0f : rng.range(0.01f, 14.0f);
            hit.pitchSemis = (i % 1000 < 500) ? 36.0f : -36.0f;
            rig.voice.trigger(hit);
            rig.run(out, 1, 1);
        }
        check();
    }

    // 2b. Parameters and triggers on every sample, 0.1 s.
    for (int i = 0; i < static_cast<int>(0.1 * fs); ++i) {
        rig.voice.setParams(randomParams(rng));
        if (rng.uni() < 0.5f) rig.voice.trigger(Hit{rng.range(0.01f, 14.0f), rng.range(-36.0f, 36.0f)});
        rig.run(out, 1, 1);
    }
    check();

    // 3. Silence: the tail must end.
    rig.voice.setParams(VoiceParams{0.0f, 1.0f, 0.5f, 1.0f});
    const int limit = static_cast<int>(maxTailSeconds * fs);
    int waited = 0;
    while (rig.voice.isActive() && waited < limit) {
        rig.run(out, 4096, 4096);
        waited += 4096;
        check();
    }
    INFO("fs " << fs << " seed " << seed << " worst |x| " << worst << " tail " << waited / fs << " s");
    CHECK(finite);
    CHECK(worst < kRailBound);
    REQUIRE_FALSE(rig.voice.isActive());
    rig.run(out, 4096, 512);
    bool zeros = true;
    for (float v : out) zeros = zeros && v == 0.0f;
    CHECK(zeros);
}

// Non-finite control values (a corrupt automation value) must not poison the
// voice: output stays finite, and with sane values back it sleeps as usual.
template <class V, class Prep>
void checkNonFiniteParams(Prep&& prep, double fs) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const VoiceParams bad[] = {{nan, 0.5f, 0.5f, 0.5f}, {0.0f, nan, 0.5f, 0.5f}, {0.0f, 0.5f, nan, 0.5f},
                               {0.0f, 0.5f, 0.5f, nan}, {nan, nan, nan, nan},       {inf, inf, inf, inf},
                               {-inf, -inf, -inf, -inf}, {1e30f, -1e30f, 1e30f, -1e30f}};
    Rig<V> rig(prep, setupAt(fs));
    std::vector<float> out;
    for (const VoiceParams& p : bad) {
        rig.voice.setParams(VoiceParams{});
        rig.voice.trigger(Hit::accented(1.0f));
        rig.run(out, 64);
        rig.voice.setParams(p);  // while ringing
        rig.run(out, 256);
        rig.voice.trigger(Hit{4.0f, inf});
        rig.voice.trigger(Hit{inf, nan});
        rig.voice.trigger(Hit{nan, 0.0f});
        rig.run(out, 256);
    }
    INFO("fs " << fs);
    CHECK(allFinite(out));
    CHECK(peakAbs(out) < kRailBound);
    CHECK(peakAbs(out) > 0.01);
    rig.voice.setParams(VoiceParams{});
    out.clear();
    int waited = 0;
    while (rig.voice.isActive() && waited < static_cast<int>(30.0 * fs)) {
        rig.run(out, 4096, 4096);
        waited += 4096;
    }
    CHECK(allFinite(out));
    CHECK_FALSE(rig.voice.isActive());
}

// A corrupt unit variance (NaN, infinite, negative, far above 1) reaches the
// voices unclamped from the engine. It must behave as the nearest legal value:
// finite output, and the voice still goes to sleep.
template <class V, class Prep>
void checkBadVariance(Prep&& prep, double fs) {
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f, 3.0f, 10.0f};
    for (float variance : bad) {
        for (std::uint32_t seed : {1U, 7U, 23U, 99U}) {
            Rig<V> rig(prep, setupAt(fs, seed, variance));
            rig.voice.trigger(Hit::accented(1.0f));
            std::vector<float> out;
            while (rig.voice.isActive() && out.size() < static_cast<std::size_t>(30.0 * fs)) rig.run(out, 4096, 4096);
            INFO("variance " << variance << " seed " << seed);
            CHECK(allFinite(out));
            CHECK(peakAbs(out) < kRailBound);
            CHECK(peakAbs(out) > 0.01);
            CHECK_FALSE(rig.voice.isActive());
        }
    }
    // Clamped, not merely made finite: 10 behaves exactly as 1.
    const auto a = render<V>(prep, setupAt(fs, 5U, 10.0f), VoiceParams{}, {{0.0, Hit::normal()}}, 0.3);
    const auto b = render<V>(prep, setupAt(fs, 5U, 1.0f), VoiceParams{}, {{0.0, Hit::normal()}}, 0.3);
    bool same = a.size() == b.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
    CHECK(same);
}

// The voice may only go to sleep once it is inaudible: nothing in the last
// `windowSeconds` (about one period of its lowest partial) before isActive()
// falls may exceed about -114 dBFS, or the truncated tail is a click.
template <class V, class Prep>
void checkQuietBeforeSleep(Prep&& prep, double fs, const VoiceParams& params, float volts, double windowSeconds) {
    Rig<V> rig(prep, setupAt(fs));
    rig.voice.setParams(params);
    rig.voice.trigger(Hit{volts, 0.0f});
    std::vector<float> out;
    const std::size_t w = static_cast<std::size_t>(windowSeconds * fs);
    while (rig.voice.isActive() && out.size() < static_cast<std::size_t>(30.0 * fs)) rig.run(out, 1, 1);
    REQUIRE_FALSE(rig.voice.isActive());
    const std::size_t from = out.size() > w ? out.size() - w : 0;
    const double tail = peakAbs(std::vector<float>(out.begin() + static_cast<std::ptrdiff_t>(from), out.end()));
    INFO("fs " << fs << " volts " << volts << " tune " << params.tune << " decay " << params.decay << " slept at "
               << static_cast<double>(out.size()) / fs << " s, last " << windowSeconds * 1e3 << " ms peak " << tail);
    CHECK(tail < 2.0e-6);
}

// Two instances given the same seed and calls must agree bit for bit.
template <class V, class Prep>
void checkDeterministic(Prep&& prep, double fs) {
    auto play = [&](std::vector<float>& out) {
        Rng rng{0xC0FFEEU};
        Rig<V> rig(prep, setupAt(fs, 77U, 0.5f));
        for (int i = 0; i < 40; ++i) {
            rig.voice.setParams(randomParams(rng));
            rig.voice.trigger(Hit{rng.range(0.5f, 14.0f), 0.0f});
            rig.run(out, 200 + static_cast<int>(rng.next() % 3000U), 1 + static_cast<int>(rng.next() % 128U));
        }
    };
    std::vector<float> a, b;
    play(a);
    play(b);
    REQUIRE(a.size() == b.size());
    bool same = true;
    for (std::size_t i = 0; i < a.size(); ++i) same = same && a[i] == b[i];
    CHECK(same);
    CHECK(peakAbs(a) > 0.01);
}

// variance = 0 must ignore the unit seed entirely.
template <class V, class Prep>
void checkSeedIgnoredAtZeroVariance(Prep&& prep, double fs, const VoiceParams& params) {
    const auto a = render<V>(prep, setupAt(fs, 1U, 0.0f), params, {{0.0, Hit::normal()}}, 0.3);
    const auto b = render<V>(prep, setupAt(fs, 987654U, 0.0f), params, {{0.0, Hit::normal()}}, 0.3);
    bool same = a.size() == b.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
    CHECK(same);
}

// Peak level must rise monotonically with the trigger voltage, 0.5 V .. 14 V.
template <class V, class Prep>
void checkMonotonicLevel(Prep&& prep, double fs, const VoiceParams& params, double seconds) {
    const float volts[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.2f, 3.0f, 4.0f, 5.5f, 7.0f, 9.0f, 11.0f, 12.5f, 14.0f};
    double prevPeak = 0.0, prevRms = 0.0;
    for (float v : volts) {
        const auto x = renderHit<V>(prep, fs, params, v, seconds);
        const double pk = peakAbs(x);
        const double r = rms(x, fs, 0.0, seconds);
        INFO("volts " << v << " peak " << pk << " rms " << r);
        CHECK(pk > prevPeak);
        CHECK(r > prevRms);
        prevPeak = pk;
        prevRms = r;
    }
}

// A sounding voice, re-hit every `periodSeconds`: nanoseconds per output sample.
template <class V, class Prep>
double benchNsPerSample(Prep&& prep, double fs, const VoiceParams& params, double periodSeconds, double seconds = 20.0) {
    VoiceSetup setup = setupAt(fs);
    setup.oversample = 2;
    Rig<V> rig(prep, setup);
    rig.voice.setParams(params);
    const int block = 64;
    std::vector<float> out(static_cast<std::size_t>(block)), noise(static_cast<std::size_t>(block));
    rig.bus.fill(noise.data(), block);
    const int blocks = static_cast<int>(seconds * fs) / block;
    const int every = std::max(1, static_cast<int>(periodSeconds * fs) / block);
    double sink = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
        if (b % every == 0) rig.voice.trigger((b / every) % 4 == 0 ? Hit::accented(1.0f) : Hit::normal());
        renderBlock(rig.voice, out.data(), noise.data(), block);
        sink += static_cast<double>(out[0]);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    CHECK(std::isfinite(sink));
    return ns / (static_cast<double>(blocks) * block);
}

}  // namespace bridged_test

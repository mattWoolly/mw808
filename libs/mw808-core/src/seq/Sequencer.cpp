// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Sample-accurate step sequencer (docs/design/architecture.md §5).
//
// What the original machine did (research/03 §2): a 4-bit CPU counts 24
// tempo clocks per quarter note and fires one common trigger per step, its
// height carrying the global accent (4 V normal, 4..14 V accented, research/03
// §3). Pre-scale sets 3/6/4/8 steps per quarter; a pattern switch waits for
// the end of the measure; A/B/AB variations and auto fill-in every 2..16
// measures replace whole measures. Shuffle, flam, sub-steps, weak steps,
// probability and polymeter are later software-machine extensions
// (research/03 §9). The hardware's 0..1.9 ms interrupt-grid jitter
// (research/03 §4) is NOT modelled here: timing is exact, as research/03 §4
// recommends for the default.
//
// Timing model (research/05 §6.2):
//  * Every hit has an exact time in quarter notes (step + shuffle + sub-step)
//    plus a tempo-independent delay in samples (the flam, which is in ms).
//  * In a range that starts at ppq0, a hit lands on sample
//        floor((ppq - ppq0) * spp + extra + 0.5 + kSnapEps)
//    (round to nearest; kSnapEps keeps exact half-sample positions, common at
//    44.1 kHz, from flipping under ~1e-12 qn of host PPQ noise). It belongs to
//    this range when that offset is < numSamples; otherwise it waits in the
//    pending queue, which keeps the musical time, so the decision is the same
//    for any block partition.
//  * Steps are walked with a cursor (measure start + next step index): a step
//    is decided once, when its own sample enters a range. Pattern switches,
//    fills and chain moves happen lazily when the next measure's downbeat
//    enters a range, so a request made any time before then still applies.
//  * HostSync derives position from host PPQ on every call; Internal/HostNote
//    use a clock of (PPQ at last tempo change + samples since / spp), so a
//    constant tempo gives the same PPQ for any partition.
//  * HostSync converts PPQ to samples at the host's own tempo (1..1000 BPM);
//    the 20..400 clamp applies to tempo() and to the internal clock.
//  * Relocation: host PPQ more than max(2 samples, 1 ms) outside the
//    continuation of the previous call, taken at any tempo between the
//    previous and the current host tempo (step change or ramp). Smaller
//    differences are host rounding and are absorbed (a hit that the noise
//    pushes before the range start is emitted at offset 0, never twice).

#include "mw808/seq/Sequencer.h"

#include <algorithm>
#include <cmath>

#include "mw808/dsp/Math.h"
#include "mw808/seq/PatternOps.h"

namespace mw808::seq {

namespace {

constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 400.0;
// HostSync only: the range of host tempos the PPQ-to-sample mapping follows
// (hosts offer roughly 1..999 BPM). tempo() still reports kMinBpm..kMaxBpm.
constexpr double kMinHostBpm = 1.0;
constexpr double kMaxHostBpm = 1000.0;
constexpr double kSnapEps = 1.0e-6;          // samples
constexpr double kMaxAbsPpq = 1.0e9;         // beyond this a host position is treated as invalid
constexpr float kMaxFlamMs = 100.0f;
constexpr float kGraceRatio = 0.6f;          // grace hit volts / main hit volts (§5.3)
constexpr float kGraceMinVolts = 1.5f;
constexpr float kExtraAccentForAccentStep = 0.25f;

template <typename T>
T finiteOr(T x, T fallback) noexcept {
    return std::isfinite(x) ? x : fallback;
}

std::int64_t floorDiv(std::int64_t a, std::int64_t m) noexcept {
    std::int64_t q = a / m;
    if ((a % m != 0) && ((a < 0) != (m < 0))) --q;
    return q;
}

std::int64_t floorMod(std::int64_t a, std::int64_t m) noexcept {
    const std::int64_t r = a % m;
    return r < 0 ? r + m : r;
}

int gcd(int a, int b) noexcept {
    while (b != 0) {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int patternLength(const Pattern& p) noexcept {
    return p.length < 1 ? 1 : (p.length > kMaxSteps ? kMaxSteps : p.length);
}

int trackLength(const Pattern& p, int track) noexcept {
    const int len = p.lengthOf(track);
    return len < 1 ? 1 : (len > kMaxSteps ? kMaxSteps : len);
}

double stepQnOf(const Pattern& p) noexcept { return stepLengthQn(p.scale); }

Settings sanitize(const Settings& in) noexcept {
    Settings s = in;
    if (static_cast<unsigned>(s.mode) > static_cast<unsigned>(Mode::HostNote)) s.mode = Mode::Off;
    s.internalBpm = finiteOr(s.internalBpm, 120.0);
    s.accent = dsp::clamp(finiteOr(s.accent, 0.7f), 0.0f, 1.0f);
    s.flamMs = dsp::clamp(finiteOr(s.flamMs, 24.0f), 0.0f, kMaxFlamMs);
    s.swingScale = dsp::clamp(finiteOr(s.swingScale, 1.0f), 0.0f, 1.0f);
    if (static_cast<unsigned>(s.variation) > static_cast<unsigned>(VariationMode::B)) s.variation = VariationMode::A;
    if (static_cast<unsigned>(s.autoFill) > static_cast<unsigned>(AutoFill::Every2)) s.autoFill = AutoFill::Manual;
    if (s.fillSlot > 3) s.fillSlot = 3;
    return s;
}

std::int64_t offsetOf(double ppq, double extraSamples, double ppq0, double spp) noexcept {
    double x = (ppq - ppq0) * spp + extraSamples + 0.5 + kSnapEps;
    if (!(x < 4.0e18)) x = 4.0e18;  // also catches NaN
    if (x < -4.0e18) x = -4.0e18;
    return static_cast<std::int64_t>(std::floor(x));
}

// Probability decision, a pure function of (measure, step, track).
bool probabilityPasses(int code, std::int64_t measure, int step, int track) noexcept {
    if (code <= 0) return true;
    const auto m = static_cast<std::uint64_t>(measure);
    std::uint32_t h = dsp::hash32(0x808A5EEDU, static_cast<std::uint32_t>(m & 0xffffffffU));
    h = dsp::hash32(h, static_cast<std::uint32_t>(m >> 32));
    h = dsp::hash32(h, static_cast<std::uint32_t>(step * 64 + track));
    const double u = static_cast<double>(h >> 8) * (1.0 / 16777216.0);
    return u < static_cast<double>(8 - code) / 8.0;
}

}  // namespace

Sequencer::Sequencer() noexcept { clearSuppress(); }

void Sequencer::prepare(double sampleRate) noexcept {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? dsp::clamp(sampleRate, 1000.0, 1.0e6) : 48000.0;
    reset();
}

void Sequencer::reset() noexcept {
    stopPlayback();
    activeMode_ = Mode::Off;
    measure_ = 0;
    measureStart_ = 0.0;
    nextStep_ = 0;
    fillRequested_ = false;
    noteHeld_ = false;
    noteRestart_ = false;
    prevPpq0_ = 0.0;
    prevN_ = 0;
    prevSpp_ = sampleRate_ * 60.0 / tempo_;
    prevRecord_ = false;
    segPpq_ = 0.0;
    segSamples_ = 0;
    segSpp_ = prevSpp_;
    order_ = 0;
    numScratch_ = 0;
}

void Sequencer::setPattern(int index, const Pattern& p) noexcept {
    if (index < 0 || index >= kNumPatterns) return;
    // Content edits apply to steps not yet fired. Length and scale of the
    // measure that is playing are latched; a new length applies next measure.
    bank_.patterns[static_cast<std::size_t>(index)] = p;
}

void Sequencer::setChain(const Chain& c) noexcept { bank_.chain = c; }

void Sequencer::selectPattern(int index, bool immediate) noexcept {
    if (index < 0 || index >= kNumPatterns) return;
    if (!playing_) {
        selectedPattern_ = index;
        currentPattern_ = index;
        queuedPattern_ = -1;
        return;
    }
    if (!immediate) {
        // Re-selecting the pattern already selected cancels a queued change.
        queuedPattern_ = (index == selectedPattern_) ? -1 : index;
        return;
    }
    selectedPattern_ = index;
    queuedPattern_ = -1;
    if (chainActive()) return;  // the chain decides what sounds
    // Take over now, keeping the step position: the next step keeps its time.
    const Pattern& p = bank_.patterns[static_cast<std::size_t>(index)];
    const double nextTime = measureStart_ + nextStep_ * stepQn_;
    const int newLen = patternLength(p);
    const double newStepQn = stepQnOf(p);
    const int ns = std::min(nextStep_, newLen);
    currentPattern_ = index;
    measureLen_ = newLen;
    stepQn_ = newStepQn;
    nextStep_ = ns;
    measureStart_ = nextTime - ns * newStepQn;
    if (currentStep_ >= newLen) currentStep_ = newLen - 1;
}

void Sequencer::requestFill() noexcept { fillRequested_ = true; }

void Sequencer::notePatternOn(int index) noexcept {
    if (index < 0 || index >= kNumPatterns) return;
    notePattern_ = index;
    noteHeld_ = true;
    noteRestart_ = true;
}

void Sequencer::notePatternOff() noexcept {
    noteHeld_ = false;
    noteRestart_ = false;
}

RecordResult Sequencer::recordHit(VoiceId voice, bool accent, int sampleOffset) noexcept {
    RecordResult r;
    if (!playing_ || !prevRecord_ || static_cast<int>(voice) >= kNumVoices) return r;
    const int v = static_cast<int>(voice);
    const double t = prevPpq0_ + (static_cast<double>(prevN_) + std::max(0, sampleOffset)) / prevSpp_;
    // Nearest step of the playing measure's grid, ignoring shuffle.
    const double rel = std::floor((t - measureStart_) / stepQn_ + 0.5);
    const auto stepAbs = static_cast<std::int64_t>(dsp::clamp(rel, -1.0e15, 1.0e15));
    const std::int64_t ahead = floorDiv(stepAbs, measureLen_);
    const int s = static_cast<int>(floorMod(stepAbs, measureLen_));
    const std::int64_t m = measure_ + ahead;

    Pattern& p = bank_.patterns[static_cast<std::size_t>(currentPattern_)];
    const int var = currentVariation_;
    const int idx = static_cast<int>(floorMod(m * measureLen_ + s, trackLength(p, v)));
    Step& cell = p.at(var, v, idx);
    Step next = cell.on() ? cell : Step{};
    next.setLevel(accent ? StepLevel::Accent : StepLevel::Normal);
    r.changed = !(next == cell);
    cell = next;
    r.pattern = static_cast<std::uint8_t>(currentPattern_);
    r.variation = static_cast<std::uint8_t>(var);
    r.track = static_cast<std::uint8_t>(v);
    r.step = static_cast<std::uint8_t>(idx);
    r.cell = next;

    // The live hit already sounded: if that step has not fired yet on this
    // pass, it must not fire again.
    std::int64_t upM = measure_;
    int upS = nextStep_;
    if (upS >= measureLen_) {
        ++upM;
        upS = 0;
    }
    if (m > upM || (m == upM && s >= upS)) {
        // Reuse a slot that is free, already holds this step, or names a step
        // that is now behind the cursor (it can never match again); if all
        // are live, replace the earliest.
        auto& ms = suppressMeasure_[static_cast<std::size_t>(v)];
        auto& ss = suppressStep_[static_cast<std::size_t>(v)];
        int slot = 0;
        for (int i = 0; i < kSuppressSlots; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const bool stale = ss[k] < 0 || ms[k] < upM || (ms[k] == upM && ss[k] < upS);
            if (stale || (ms[k] == m && ss[k] == s)) {
                slot = i;
                break;
            }
            const auto best = static_cast<std::size_t>(slot);
            if (ms[k] < ms[best] || (ms[k] == ms[best] && ss[k] < ss[best])) slot = i;
        }
        ms[static_cast<std::size_t>(slot)] = m;
        ss[static_cast<std::size_t>(slot)] = s;
    }
    return r;
}

// ---------------------------------------------------------------------------

void Sequencer::dropPending() noexcept { numPending_ = 0; }

void Sequencer::clearSuppress() noexcept {
    for (auto& a : suppressMeasure_) a.fill(0);
    for (auto& a : suppressStep_) a.fill(-1);
}

bool Sequencer::takeSuppressed(int voice, std::int64_t measure, int step) noexcept {
    auto& ms = suppressMeasure_[static_cast<std::size_t>(voice)];
    auto& ss = suppressStep_[static_cast<std::size_t>(voice)];
    for (std::size_t k = 0; k < ss.size(); ++k) {
        if (ss[k] == step && ms[k] == measure) {
            ss[k] = -1;
            return true;
        }
    }
    return false;
}

void Sequencer::stopPlayback() noexcept {
    playing_ = false;
    currentStep_ = -1;
    dropPending();
    clearSuppress();
    if (queuedPattern_ >= 0) selectedPattern_ = queuedPattern_;  // stopped: takes over at once
    queuedPattern_ = -1;
    currentPattern_ = selectedPattern_;
    nextStep_ = 0;
}

bool Sequencer::chainActive() const noexcept {
    return settings_.chain && bank_.chain.length > 0 && settings_.mode != Mode::HostNote;
}

int Sequencer::basePatternFor(std::int64_t measure) const noexcept {
    if (!chainActive()) return selectedPattern_;
    const int len = std::min<int>(bank_.chain.length, kMaxChainLength);
    const int e = bank_.chain.entries[static_cast<std::size_t>(floorMod(measure, len))];
    return e < kNumPatterns ? e : kNumPatterns - 1;
}

int Sequencer::fillPatternFor(int basePattern) const noexcept {
    return (basePattern / kPatternsPerBank) * kPatternsPerBank + kFirstFillSlot + settings_.fillSlot;
}

bool Sequencer::autoFillAt(std::int64_t measure) const noexcept {
    const int n = autoFillMeasures(settings_.autoFill);
    return n > 0 && floorMod(measure, n) == n - 1;
}

int Sequencer::variationFor(std::int64_t measure) const noexcept {
    switch (settings_.variation) {
        case VariationMode::B: return 1;
        case VariationMode::AB: return static_cast<int>(floorMod(measure, 2));
        default: return 0;
    }
}

double Sequencer::measureLengthQn(int pattern) const noexcept {
    const Pattern& p = bank_.patterns[static_cast<std::size_t>(pattern)];
    return patternLength(p) * stepQnOf(p);
}

void Sequencer::startMeasure(std::int64_t measure, double startPpq, int pattern, bool /*isFill*/) noexcept {
    const Pattern& p = bank_.patterns[static_cast<std::size_t>(pattern)];
    measure_ = measure;
    measureStart_ = startPpq;
    currentPattern_ = pattern;
    measureLen_ = patternLength(p);
    stepQn_ = stepQnOf(p);
    currentVariation_ = variationFor(measure);
    nextStep_ = 0;
}

void Sequencer::advanceMeasure(double startPpq) noexcept {
    const std::int64_t m = measure_ + 1;
    if (queuedPattern_ >= 0) {
        selectedPattern_ = queuedPattern_;
        queuedPattern_ = -1;
    }
    const int base = basePatternFor(m);
    const bool fill = autoFillAt(m) || fillRequested_;
    fillRequested_ = false;
    startMeasure(m, startPpq, fill ? fillPatternFor(base) : base, fill);
}

// Put the cursor on the first step whose sample is at or after `ppq`, with
// the measure structure a pure function of PPQ from 0 (chain and auto fill
// included), so a relocation lands where uninterrupted playback would be.
void Sequencer::locate(double ppq, double spp, bool allowIntro) noexcept {
    if (allowIntro && fillRequested_ && activeMode_ == Mode::Internal) {
        // Fill requested while stopped: it plays once as an intro before the
        // basic rhythm and is not counted (research/03 §2.6).
        fillRequested_ = false;
        startMeasure(-1, ppq, fillPatternFor(basePatternFor(0)), true);
        currentVariation_ = variationFor(0);
        currentStep_ = -1;
        return;
    }

    const int chainLen = chainActive() ? std::min<int>(bank_.chain.length, kMaxChainLength) : 1;
    const int fillN = std::max(1, autoFillMeasures(settings_.autoFill));
    const int period = chainLen / gcd(chainLen, fillN) * fillN;  // <= 64 * 16
    auto patternAt = [&](std::int64_t k) {
        const int base = basePatternFor(k);
        return autoFillAt(k) ? fillPatternFor(base) : base;
    };
    double cycle = 0.0;
    for (int k = 0; k < period; ++k) cycle += measureLengthQn(patternAt(k));

    const double c = std::floor(ppq / cycle);
    double start = c * cycle;
    double rest = ppq - start;
    int k = 0;
    while (k < period - 1) {
        const double len = measureLengthQn(patternAt(k));
        if (rest < len) break;
        rest -= len;
        start += len;
        ++k;
    }
    const std::int64_t measure = static_cast<std::int64_t>(c) * period + k;
    startMeasure(measure, start, patternAt(measure), autoFillAt(measure));

    int s = static_cast<int>(dsp::clamp(std::floor((ppq - start) / stepQn_), 0.0, static_cast<double>(measureLen_)));
    if (s < measureLen_ && offsetOf(start + s * stepQn_, 0.0, ppq, spp) < 0) ++s;
    nextStep_ = s;
    currentStep_ = s > 0 ? s - 1 : -1;
}

void Sequencer::pushScratch(std::int64_t offset, std::uint64_t order, const Event& e) noexcept {
    if (numScratch_ >= kMaxScratch) return;  // more hits than one block can carry: drop
    Scratch& s = scratch_[static_cast<std::size_t>(numScratch_++)];
    s.offset = offset < 0 ? 0 : offset;
    s.order = order;
    s.event = e;
}

void Sequencer::emit(double ppq, double extraSamples, const Event& e, double ppq0, double spp,
                     int numSamples) noexcept {
    const std::int64_t off = offsetOf(ppq, extraSamples, ppq0, spp);
    const std::uint64_t order = order_++;
    if (off < numSamples) {
        pushScratch(off, order, e);
    } else if (numPending_ < kMaxPending) {
        Pending& p = pending_[static_cast<std::size_t>(numPending_++)];
        p.ppq = ppq;
        p.extraSamples = extraSamples;
        p.order = order;
        p.event = e;
    }
    // else: pending queue full, the hit is dropped (bounded by design).
}

void Sequencer::fireStep(double stepPpq, double ppq0, double spp, int numSamples) noexcept {
    const Pattern& p = bank_.patterns[static_cast<std::size_t>(currentPattern_)];
    const int s = nextStep_;
    const int var = currentVariation_;
    const bool accentOn = p.at(var, kAccentTrack, s).on();
    const int shuffle = std::min<int>(p.shuffle, 100);
    const double shuffleQn =
        (s & 1) != 0 ? (shuffle / 100.0) * static_cast<double>(settings_.swingScale) * stepQn_ / 3.0 : 0.0;
    const double flamSamples = static_cast<double>(settings_.flamMs) * 0.001 * sampleRate_;
    // A flam shorter than one sample would put the grace and the main hit on
    // the same sample: two triggers (and two MIDI note-ons) at once. Play the
    // main hit alone then, as the MIDI file writer does for equal ticks.
    const bool flamAudible = flamSamples >= 1.0;
    const std::int64_t polyIndex = measure_ * measureLen_ + s;
    const float accent = settings_.accent;

    for (int v = 0; v < kNumVoices; ++v) {
        const Step cell = p.at(var, v, static_cast<int>(floorMod(polyIndex, trackLength(p, v))));
        if (!cell.on()) continue;
        if (takeSuppressed(v, measure_, s)) continue;
        if (!probabilityPasses(cell.probabilityCode(), measure_, s, v)) continue;

        const StepLevel level = promotedLevel(cell.level(), accentOn);
        Hit hit;
        if (level == StepLevel::Weak) {
            hit = Hit::weak();
        } else if (level == StepLevel::Normal) {
            hit = Hit::normal();
        } else if (accentOn && cell.level() == StepLevel::Accent) {
            hit = Hit::accented(std::min(1.0f, accent + kExtraAccentForAccentStep));
        } else {
            hit = Hit::accented(accent);
        }
        Event e;
        e.voice = static_cast<VoiceId>(v);
        e.level = level;
        e.hit = hit;

        const int subs = cell.subSteps();
        const double subQn = stepQn_ / subs;
        for (int k = 0; k < subs; ++k) {
            const double t = stepPpq + shuffleQn + k * subQn;
            if (k == 0 && cell.flam() && flamAudible) {
                Event grace = e;
                grace.level = StepLevel::Weak;
                grace.hit.volts = std::max(kGraceMinVolts, kGraceRatio * hit.volts);
                emit(t, 0.0, grace, ppq0, spp, numSamples);
                emit(t, flamSamples, e, ppq0, spp, numSamples);
            } else {
                emit(t, 0.0, e, ppq0, spp, numSamples);
            }
        }
    }
}

void Sequencer::process(const Transport& transport, const Settings& settingsIn, int numSamples, EventBuffer& out,
                        int offsetBase) noexcept {
    settings_ = sanitize(settingsIn);
    const Settings& st = settings_;

    const bool hostTempo = transport.hostValid && std::isfinite(transport.bpm) && transport.bpm > 0.0;
    double bpm = hostTempo ? transport.bpm : st.internalBpm;
    bpm = dsp::clamp(bpm, kMinBpm, kMaxBpm);
    tempo_ = bpm;
    if (numSamples <= 0) return;
    const int n = numSamples;
    // In HostSync the host owns the position: its PPQ advances at the host's
    // real tempo whatever we clamp. Converting PPQ to samples at the clamped
    // tempo would put every block outside the relocation tolerance (a 600 BPM
    // host lost a third of its steps; a 10 BPM host double-fired every step),
    // so the mapping follows the host rate within a wider sanity range.
    const double rateBpm = (st.mode == Mode::HostSync && hostTempo)
                               ? dsp::clamp(transport.bpm, kMinHostBpm, kMaxHostBpm)
                               : bpm;
    const double spp = sampleRate_ * 60.0 / rateBpm;

    // --- what should be playing ---------------------------------------------
    bool want = false;
    switch (st.mode) {
        case Mode::HostSync:
            want = transport.hostValid && transport.playing && std::isfinite(transport.ppq) &&
                   std::abs(transport.ppq) < kMaxAbsPpq;
            break;
        case Mode::Internal: want = st.run; break;
        case Mode::HostNote: want = noteHeld_; break;
        default: want = false; break;
    }
    if (playing_ && (st.mode != activeMode_ || (st.mode == Mode::HostNote && noteRestart_))) stopPlayback();
    if (!want) {
        if (playing_) stopPlayback();
        if (st.mode != Mode::HostNote) noteRestart_ = false;
        currentVariation_ = st.variation == VariationMode::B ? 1 : 0;
        prevRecord_ = false;
        return;
    }

    // --- position -------------------------------------------------------------
    const bool starting = !playing_;
    bool relocate = false;
    double ppq0 = 0.0;
    if (st.mode == Mode::HostSync) {
        ppq0 = transport.ppq;
        if (!starting) {
            // The host advanced the previous range at some tempo between the
            // one it reported then and the one it reports now (a step change
            // lands on either end, a ramp in between), so the continuation is
            // a window, widened by the tolerance. Checking only the old tempo
            // turned large blocks of a steep ramp into false relocations that
            // skipped steps (8192-sample blocks, 60 -> 400 BPM over 4 bars).
            const double eOld = prevPpq0_ + static_cast<double>(prevN_) / prevSpp_;
            const double eNew = prevPpq0_ + static_cast<double>(prevN_) / spp;
            const double tolQn = std::max(2.0, 0.001 * sampleRate_) / spp;
            const double lo = std::min(eOld, eNew) - tolQn;
            const double hi = std::max(eOld, eNew) + tolQn;
            if (!(ppq0 >= lo && ppq0 <= hi)) relocate = true;
        }
    } else {
        if (starting) {
            segPpq_ = 0.0;
            segSamples_ = 0;
            segSpp_ = spp;
        } else if (spp != segSpp_) {  // tempo change: takes effect from this call
            segPpq_ += static_cast<double>(segSamples_) / segSpp_;
            segSamples_ = 0;
            segSpp_ = spp;
        }
        ppq0 = segPpq_ + static_cast<double>(segSamples_) / segSpp_;
    }

    if (starting) {
        playing_ = true;
        activeMode_ = st.mode;
        dropPending();
        clearSuppress();
        if (st.mode == Mode::HostNote) {
            selectedPattern_ = notePattern_;
            queuedPattern_ = -1;
            noteRestart_ = false;
        }
        locate(ppq0, spp, true);
    } else if (relocate) {
        dropPending();
        clearSuppress();
        locate(ppq0, spp, false);
    }

    // --- hits ---------------------------------------------------------------
    numScratch_ = 0;
    int keep = 0;
    for (int i = 0; i < numPending_; ++i) {
        const Pending& p = pending_[static_cast<std::size_t>(i)];
        const std::int64_t off = offsetOf(p.ppq, p.extraSamples, ppq0, spp);
        if (off < n)
            pushScratch(off, p.order, p.event);
        else
            pending_[static_cast<std::size_t>(keep++)] = p;
    }
    numPending_ = keep;

    for (;;) {
        const double t = measureStart_ + nextStep_ * stepQn_;
        if (offsetOf(t, 0.0, ppq0, spp) >= n) break;
        if (nextStep_ >= measureLen_) {
            advanceMeasure(t);
            continue;
        }
        fireStep(t, ppq0, spp, n);
        currentStep_ = nextStep_;
        ++nextStep_;
    }

    std::sort(scratch_.begin(), scratch_.begin() + numScratch_, [](const Scratch& a, const Scratch& b) {
        return a.offset != b.offset ? a.offset < b.offset : a.order < b.order;
    });
    for (int i = 0; i < numScratch_; ++i) {
        Event e = scratch_[static_cast<std::size_t>(i)].event;
        e.offset = static_cast<std::int32_t>(scratch_[static_cast<std::size_t>(i)].offset + offsetBase);
        if (!out.push(e)) break;
    }
    numScratch_ = 0;

    prevPpq0_ = ppq0;
    prevN_ = n;
    prevSpp_ = spp;
    prevRecord_ = st.record;
    if (st.mode != Mode::HostSync) segSamples_ += n;
}

}  // namespace mw808::seq

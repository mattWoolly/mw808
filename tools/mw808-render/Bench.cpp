// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The `bench` subcommand: realtime cost of a full Machine on a dense
// worst-case pattern (all 16 voices retriggered on every step with sub-steps,
// so every voice rings continuously; master drive, compressor, analog stage
// and limiter all on; per-voice drive on).
//
// For each configuration (rate x quality x block size) it runs `repeats`
// independent renders of `seconds` of audio after a short untimed warm-up
// and times every process() call with steady_clock. Reported:
//   ns/sample  median over repeats of (total time / samples)
//   CPU %      that, as a share of one core in real time
//   p99.9      99.9th percentile block time over all repeats
//   worst      the slowest single block. "worst" is the best-of-N value (the
//              smallest per-repeat maximum), which filters one-off scheduler
//              preemptions; "worst-all" is the raw maximum over every repeat.
//   % deadln   worst / (block / rate): the stability number.
// Gate: exit 1 when "worst" exceeds --gate percent (default 25) of the
// deadline for any 48 kHz Standard configuration.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Common.h"

#ifndef MW808_VERSION
#define MW808_VERSION "unknown"
#endif
#ifndef MW808_BUILD_TYPE
#define MW808_BUILD_TYPE "unknown"
#endif

namespace mw808::tools {

namespace {

seq::Pattern densePattern()
{
    seq::Pattern p;
    p.length = 16;
    p.scale = seq::Scale::Sixteenth;
    p.shuffle = 30;
    for (int v = 0; v < kNumVoices; ++v)
    {
        for (int s = 0; s < 16; ++s)
        {
            // The closed hat chokes the open one; keep it sparse so the open
            // hat rings (the expensive case) while both stay busy.
            if (v == CH && s % 4 != 3)
                continue;
            seq::Step& st = p.at(0, v, s);
            st.setLevel(s % 2 == 0 ? seq::StepLevel::Accent : seq::StepLevel::Normal);
            st.setSubSteps(2 + (s + v) % 3);
            st.setFlam(s % 4 == 2);
        }
    }
    for (int s = 0; s < 16; s += 4)
        p.at(0, seq::kAccentTrack, s).setLevel(seq::StepLevel::Normal);
    return p;
}

MachineParams worstCaseParams(Quality q)
{
    MachineParams params;
    for (std::size_t v = 0; v < kNumVoices; ++v)
    {
        params.engine.voice[v] = kVoiceInfo[v].defaults;
        params.engine.voice[v].decay = 0.75f; // longer tails: more voices overlap
        params.engine.channel[v].drive = 0.3f;
        params.engine.channel[v].pan = (static_cast<float>(v) - 7.5f) / 8.0f;
    }
    params.engine.master.analogOut = true;
    params.engine.master.drive = 0.4f;
    params.engine.master.comp = 0.5f;
    params.engine.master.limiter = true;
    params.seq.mode = seq::Mode::HostSync;
    params.seq.accent = 0.8f;
    params.quality = q;
    return params;
}

struct RunResult
{
    double totalNs = 0.0;
    double worstNs = 0.0;
    std::int64_t samples = 0;
    std::vector<double> blockNs;
};

RunResult runOnce(double rate, Quality q, int block, double seconds, double tempo,
                  const seq::Pattern& pattern, const std::array<bool, kNumVoices>& mute,
                  bool keepBlocks)
{
    auto machine = std::make_unique<Machine>();
    machine->prepare(rate, block);
    machine->sequencer().setPattern(0, pattern);
    machine->sequencer().selectPattern(0);
    MachineParams params = worstCaseParams(q);
    params.mute = mute;

    std::vector<float> left(static_cast<std::size_t>(block)),
        right(static_cast<std::size_t>(block));
    BusBuffers out;
    out.ch[0][0] = left.data();
    out.ch[0][1] = right.data();

    const auto warmBlocks = static_cast<std::int64_t>(std::ceil(0.5 * rate / block));
    const auto timedBlocks = static_cast<std::int64_t>(std::ceil(seconds * rate / block));

    RunResult res;
    if (keepBlocks)
        res.blockNs.reserve(static_cast<std::size_t>(timedBlocks));
    std::int64_t pos = 0;
    for (std::int64_t b = 0; b < warmBlocks + timedBlocks; ++b)
    {
        Machine::ProcessArgs a;
        a.transport.hostValid = true;
        a.transport.playing = true;
        a.transport.bpm = tempo;
        a.transport.ppq = static_cast<double>(pos) * tempo / (60.0 * rate);
        a.out = out;
        a.numSamples = block;

        const auto t0 = std::chrono::steady_clock::now();
        machine->process(a, params);
        const auto t1 = std::chrono::steady_clock::now();
        pos += block;

        if (b < warmBlocks)
            continue;
        const double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        res.totalNs += ns;
        res.worstNs = std::max(res.worstNs, ns);
        res.samples += block;
        if (keepBlocks)
            res.blockNs.push_back(ns);
    }
    // Keep the output alive so nothing is optimised away.
    volatile float sink = left[0] + right[static_cast<std::size_t>(block - 1)];
    (void)sink;
    return res;
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n == 0 ? 0.0 : (n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]));
}

std::string cpuModel()
{
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("model name", 0) == 0)
        {
            const std::size_t colon = line.find(':');
            return colon == std::string::npos ? line : line.substr(colon + 2);
        }
    return "unknown CPU";
}

} // namespace

int cmdBench(const std::vector<std::string>& args)
{
    std::vector<double> rates{44100.0, 48000.0, 96000.0};
    std::vector<Quality> qualities{Quality::Eco, Quality::Standard, Quality::High};
    std::vector<int> blocks{32, 128, 1024};
    double seconds = 4.0, gatePct = 25.0, tempo = 160.0;
    int repeats = 3;
    bool gate = true, perVoice = true;

    ArgReader r(args);
    while (r.more())
    {
        const std::string opt = r.option();
        if (opt == "--rates")
        {
            rates.clear();
            for (const auto& t : splitList(r.value()))
                rates.push_back(parseDouble(opt, t, 8000.0, 384000.0));
        }
        else if (opt == "--qualities")
        {
            qualities.clear();
            for (const auto& t : splitList(r.value()))
                qualities.push_back(parseQuality(t));
        }
        else if (opt == "--blocks")
        {
            blocks.clear();
            for (const auto& t : splitList(r.value()))
                blocks.push_back(parseInt(opt, t, 1, 16384));
        }
        else if (opt == "--seconds")
            seconds = parseDouble(opt, r.value(), 0.1, 600.0);
        else if (opt == "--repeats")
            repeats = parseInt(opt, r.value(), 1, 100);
        else if (opt == "--tempo")
            tempo = parseDouble(opt, r.value(), 20.0, 400.0);
        else if (opt == "--gate")
            gatePct = parseDouble(opt, r.value(), 0.1, 1000.0);
        else if (opt == "--no-gate")
        {
            r.flag();
            gate = false;
        }
        else if (opt == "--no-per-voice")
        {
            r.flag();
            perVoice = false;
        }
        else
            r.unknown("bench");
    }

    const seq::Pattern pattern = densePattern();
    const std::array<bool, kNumVoices> noMute{};

    std::printf("mw808-render bench  (mw808 %s, %s build, g++ %s)\n", MW808_VERSION,
                MW808_BUILD_TYPE, __VERSION__);
    std::printf("CPU: %s\n", cpuModel().c_str());
    std::printf("dense pattern: 16 voices on every step with x2..x4 sub-steps and flams, %.0f BPM; "
                "master drive, comp,\n"
                "analog stage and limiter on. %d repeat(s) x %.1f s per configuration after a 0.5 "
                "s warm-up.\n"
                "worst = best-of-%d slowest block (filters one-off preemption); worst-all = raw "
                "maximum.\n\n",
                tempo, repeats, seconds, repeats);
    std::printf("%8s %-8s %6s | %9s %7s | %9s %10s %10s %10s | %8s\n", "rate", "quality", "block",
                "ns/sample", "CPU %", "p99.9 us", "worst us", "worst-all", "deadln us", "% deadln");
    std::printf("---------------------------+-------------------+----------------------------------"
                "------------+---------"
                "\n");

    bool gateEvaluated = false, gateFailed = false;
    double gateWorstPct = 0.0;
    for (double rate : rates)
    {
        for (Quality q : qualities)
        {
            for (int block : blocks)
            {
                std::vector<double> avgs, worsts, all;
                double worstAll = 0.0;
                for (int i = 0; i < repeats; ++i)
                {
                    const RunResult res =
                        runOnce(rate, q, block, seconds, tempo, pattern, noMute, true);
                    avgs.push_back(res.totalNs / static_cast<double>(res.samples));
                    worsts.push_back(res.worstNs);
                    worstAll = std::max(worstAll, res.worstNs);
                    all.insert(all.end(), res.blockNs.begin(), res.blockNs.end());
                }
                std::sort(all.begin(), all.end());
                const double p999 =
                    all[std::min(all.size() - 1, static_cast<std::size_t>(
                                                     0.999 * static_cast<double>(all.size())))];
                const double nsPerSample = median(avgs);
                const double cpu = nsPerSample * rate / 1.0e9 * 100.0;
                const double worst = *std::min_element(worsts.begin(), worsts.end());
                const double deadlineNs = static_cast<double>(block) / rate * 1.0e9;
                const double pct = worst / deadlineNs * 100.0;
                const bool gated = std::abs(rate - 48000.0) < 0.5 && q == Quality::Standard;
                if (gated)
                {
                    gateEvaluated = true;
                    gateWorstPct = std::max(gateWorstPct, pct);
                    if (pct > gatePct)
                        gateFailed = true;
                }
                std::printf("%8.0f %-8s %6d | %9.1f %7.2f | %9.1f %10.1f %10.1f %10.1f | %7.2f%s\n",
                            rate, qualityName(q), block, nsPerSample, cpu, p999 / 1000.0,
                            worst / 1000.0, worstAll / 1000.0, deadlineNs / 1000.0, pct,
                            gated ? (pct > gatePct ? " FAIL" : " *") : "");
                std::fflush(stdout);
            }
        }
    }

    if (perVoice)
    {
        // Incremental cost of each voice alone (mute gates triggers, so a muted
        // voice never wakes): 48 kHz Standard, block 128, min over repeats.
        const double rate = 48000.0;
        const int block = 128;
        const auto costOf = [&](const std::array<bool, kNumVoices>& mute) {
            double best = 1.0e300;
            for (int i = 0; i < repeats; ++i)
            {
                const RunResult res =
                    runOnce(rate, Quality::Standard, block, seconds, tempo, pattern, mute, false);
                best = std::min(best, res.totalNs / static_cast<double>(res.samples));
            }
            return best;
        };
        std::array<bool, kNumVoices> allMuted{};
        allMuted.fill(true);
        const double base = costOf(allMuted);
        std::printf("\nper-voice cost, 48000 Hz Standard, block 128 (each voice alone, minus the "
                    "all-muted baseline)\n"
                    "  baseline = every voice muted: sequencer, mixer, master section and the\n"
                    "  metal voices' oscillators, which free-run even while silent\n");
        std::printf("  %-4s %-14s %9s %8s\n", "code", "voice", "ns/sample", "CPU %");
        std::printf("  %-4s %-14s %9.1f %8.3f\n", "--", "baseline", base,
                    base * rate / 1.0e9 * 100.0);
        double sum = 0.0;
        for (int v = 0; v < kNumVoices; ++v)
        {
            std::array<bool, kNumVoices> mute = allMuted;
            mute[static_cast<std::size_t>(v)] = false;
            const double cost = std::max(0.0, costOf(mute) - base);
            sum += cost;
            std::printf("  %-4s %-14s %9.1f %8.3f%s\n",
                        std::string(kVoiceInfo[static_cast<std::size_t>(v)].code).c_str(),
                        std::string(kVoiceInfo[static_cast<std::size_t>(v)].name).c_str(), cost,
                        cost * rate / 1.0e9 * 100.0,
                        (v == OH || v == CH) ? "  (renders the shared hi-hat circuit)" : "");
            std::fflush(stdout);
        }
        std::printf("  %-4s %-14s %9.1f %8.3f\n", "sum", "", sum, sum * rate / 1.0e9 * 100.0);
    }

    if (gate)
    {
        if (!gateEvaluated)
            std::printf("\ngate: not evaluated (no 48000 Hz Standard configuration in this run)\n");
        else if (gateFailed)
        {
            std::printf("\nGATE FAILED: worst block at 48 kHz Standard is %.2f %% of its deadline "
                        "(limit %.1f %%)\n",
                        gateWorstPct, gatePct);
            return 1;
        }
        else
            std::printf("\ngate passed: worst block at 48 kHz Standard is %.2f %% of its deadline "
                        "(limit %.1f %%)\n",
                        gateWorstPct, gatePct);
    }
    return 0;
}

} // namespace mw808::tools

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// mw808-wavcompare: compares a candidate render against a blessed reference
// and prints diagnostics a reviewer can act on without opening a DAW: format,
// peak/RMS of both, max |diff| and where it is, RMS of the difference in dBFS
// and relative to the reference, the first differing sample, and the
// difference per tenth of the file.
//
//   mw808-wavcompare --candidate new.wav --blessed ref.wav [--tol <x>|lsb:<n>]
//
// Pass when the formats match and max |diff| <= tol. Default tol is one LSB of
// the blessed file's bit depth (0 for float files): the reference platform is
// this machine and the core is deterministic, so a blessed render reproduces
// bit for bit; one LSB absorbs a single rounding flip at the quantiser.
// Exit codes: 0 pass, 1 mismatch, 2 usage or unreadable file.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Wav.h"

namespace {

double toDb(double x)
{
    return x > 0.0 ? 20.0 * std::log10(x) : -999.0;
}

void usage()
{
    std::fputs(
        "usage: mw808-wavcompare --candidate <file.wav> --blessed <file.wav> [--tol <abs>|lsb:<n>] "
        "[--quiet]\n"
        "       exit 0 = match within tolerance, 1 = mismatch, 2 = usage / unreadable file\n",
        stderr);
}

} // namespace

int main(int argc, char** argv)
{
    using namespace mw808::tools;
    std::string candidatePath, blessedPath, tolText;
    bool quiet = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc)
            {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--candidate")
            candidatePath = next();
        else if (a == "--blessed")
            blessedPath = next();
        else if (a == "--tol")
            tolText = next();
        else if (a == "--quiet")
            quiet = true;
        else if (a == "--help" || a == "-h")
        {
            usage();
            return 0;
        }
        else
        {
            usage();
            return 2;
        }
    }
    if (candidatePath.empty() || blessedPath.empty())
    {
        usage();
        return 2;
    }

    WavData cand, ref;
    std::string error;
    if (!readWav(candidatePath, cand, error) || !readWav(blessedPath, ref, error))
    {
        std::fprintf(stderr, "mw808-wavcompare: %s\n", error.c_str());
        return 2;
    }

    const double lsb = lsbOf(ref.bitsPerSample, ref.isFloat);
    double tol = lsb;
    if (!tolText.empty())
    {
        char* end = nullptr;
        if (tolText.rfind("lsb:", 0) == 0)
            tol = std::strtod(tolText.c_str() + 4, &end) * lsb;
        else
            tol = std::strtod(tolText.c_str(), &end);
        if (end == nullptr || *end != '\0' || !(tol >= 0.0))
        {
            usage();
            return 2;
        }
    }

    std::printf("golden compare\n  candidate: %s\n  blessed  : %s\n", candidatePath.c_str(),
                blessedPath.c_str());
    const auto fmt = [](const WavData& w) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%d ch, %d Hz, %d-bit %s, %d frames", w.channels,
                      w.sampleRate, w.bitsPerSample, w.isFloat ? "float" : "PCM", w.numFrames());
        return std::string(buf);
    };
    std::printf("  format   : candidate %s\n             blessed   %s\n", fmt(cand).c_str(),
                fmt(ref).c_str());

    if (cand.channels != ref.channels || cand.sampleRate != ref.sampleRate
        || cand.numFrames() != ref.numFrames())
    {
        std::printf("RESULT: FAIL (format or length differs)\n");
        return 1;
    }

    const int ch = ref.channels;
    const int frames = ref.numFrames();
    double maxDiff = 0.0, sumSqDiff = 0.0, sumSqRef = 0.0, peakRef = 0.0, peakCand = 0.0;
    std::size_t maxAt = 0, firstAt = 0, differing = 0;
    bool anyDiff = false;
    constexpr int kSegments = 10;
    double segMax[kSegments] = {};
    for (std::size_t i = 0; i < ref.samples.size(); ++i)
    {
        const double a = cand.samples[i], b = ref.samples[i];
        const double d = std::abs(a - b);
        peakRef = std::max(peakRef, std::abs(b));
        peakCand = std::max(peakCand, std::abs(a));
        sumSqRef += b * b;
        sumSqDiff += d * d;
        if (d > 0.0)
        {
            ++differing;
            if (!anyDiff)
            {
                anyDiff = true;
                firstAt = i;
            }
        }
        if (d > maxDiff)
        {
            maxDiff = d;
            maxAt = i;
        }
        const int frame = static_cast<int>(i / static_cast<std::size_t>(ch));
        const int seg = frames > 0 ? std::min(kSegments - 1, frame * kSegments / frames) : 0;
        segMax[seg] = std::max(segMax[seg], d);
    }
    const double n = static_cast<double>(std::max<std::size_t>(1, ref.samples.size()));
    const double rmsRef = std::sqrt(sumSqRef / n), rmsDiff = std::sqrt(sumSqDiff / n);
    const double rate = static_cast<double>(ref.sampleRate);

    std::printf("  blessed  : peak %.2f dBFS, rms %.2f dBFS\n", toDb(peakRef), toDb(rmsRef));
    std::printf("  candidate: peak %.2f dBFS\n", toDb(peakCand));
    if (!anyDiff)
    {
        std::printf("  identical: every sample matches\nRESULT: PASS (bit-exact)\n");
        return 0;
    }
    const std::size_t maxFrame = maxAt / static_cast<std::size_t>(ch),
                      firstFrame = firstAt / static_cast<std::size_t>(ch);
    char lsbBuf[64] = "";
    if (lsb > 0.0)
        std::snprintf(lsbBuf, sizeof lsbBuf, " = %.1f LSB", maxDiff / lsb);
    const std::string lsbText = lsbBuf;
    std::printf("  max |diff|: %.3g (%.2f dBFS%s) at frame %zu ch %zu (t = %.5f s)\n", maxDiff,
                toDb(maxDiff), lsbText.c_str(), maxFrame, maxAt % static_cast<std::size_t>(ch),
                static_cast<double>(maxFrame) / rate);
    std::printf("  rms diff  : %.2f dBFS (%.2f dB relative to the blessed rms)\n", toDb(rmsDiff),
                toDb(rmsDiff) - toDb(rmsRef));
    std::printf("  differing : %zu of %zu samples; first at frame %zu ch %zu (t = %.5f s): "
                "candidate %.8f, blessed "
                "%.8f\n",
                differing, ref.samples.size(), firstFrame, firstAt % static_cast<std::size_t>(ch),
                static_cast<double>(firstFrame) / rate, static_cast<double>(cand.samples[firstAt]),
                static_cast<double>(ref.samples[firstAt]));
    if (!quiet)
    {
        std::printf("  max |diff| per tenth of the file (dBFS):\n   ");
        for (int s = 0; s < kSegments; ++s)
            std::printf(" %7.1f", toDb(segMax[s]));
        std::printf("\n");
    }
    const bool pass = maxDiff <= tol;
    std::printf("RESULT: %s (max |diff| %.3g %s tolerance %.3g)\n", pass ? "PASS" : "FAIL", maxDiff,
                pass ? "<=" : ">", tol);
    return pass ? 0 : 1;
}

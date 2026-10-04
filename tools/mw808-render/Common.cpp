// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "Common.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "PatternText.h"
#include "Wav.h"
#include "mw808/Factory.h"

namespace mw808::tools {

namespace {

std::string lower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// --- ArgReader -----------------------------------------------------------------

std::string ArgReader::option()
{
    const std::string& a = args_[pos_++];
    if (a.size() < 2 || a[0] != '-')
        throw UsageError("unexpected argument '" + a + "'");
    const std::size_t eq = a.find('=');
    if (a.rfind("--", 0) == 0 && eq != std::string::npos)
    {
        current_ = a.substr(0, eq);
        inline_ = a.substr(eq + 1);
        hasInline_ = true;
    }
    else
    {
        current_ = a;
        inline_.clear();
        hasInline_ = false;
    }
    return current_;
}

std::string ArgReader::value()
{
    if (hasInline_)
    {
        hasInline_ = false;
        return inline_;
    }
    if (pos_ >= args_.size())
        throw UsageError("option " + current_ + " needs a value");
    return args_[pos_++];
}

void ArgReader::flag()
{
    if (hasInline_)
        throw UsageError("option " + current_ + " takes no value");
}

void ArgReader::unknown(const std::string& subcommand) const
{
    throw UsageError("unknown option '" + current_ + "' for '" + subcommand + "'");
}

// --- value parsing -----------------------------------------------------------------

double parseDouble(const std::string& option, const std::string& text, double lo, double hi)
{
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (text.empty() || end == nullptr || *end != '\0' || !std::isfinite(v) || v < lo || v > hi)
    {
        char range[96];
        std::snprintf(range, sizeof range, "%g..%g", lo, hi);
        throw UsageError(option + ": '" + text + "' is not a number in " + range);
    }
    return v;
}

int parseInt(const std::string& option, const std::string& text, int lo, int hi)
{
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || end == nullptr || *end != '\0' || v < lo || v > hi)
        throw UsageError(option + ": '" + text + "' is not an integer in " + std::to_string(lo)
                         + ".." + std::to_string(hi));
    return static_cast<int>(v);
}

std::uint32_t parseU32(const std::string& option, const std::string& text)
{
    char* end = nullptr;
    const unsigned long long v = std::strtoull(text.c_str(), &end, 0);
    if (text.empty() || text[0] == '-' || end == nullptr || *end != '\0' || v > 0xFFFFFFFFULL)
        throw UsageError(option + ": '" + text + "' is not an unsigned 32-bit integer");
    return static_cast<std::uint32_t>(v);
}

std::vector<std::string> splitList(const std::string& text, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        if (c == sep)
        {
            out.push_back(cur);
            cur.clear();
        }
        else if (c != ' ')
            cur += c;
    }
    out.push_back(cur);
    return out;
}

VoiceId parseVoice(const std::string& text)
{
    const std::string l = lower(text);
    for (int v = 0; v < kNumVoices; ++v)
        if (kVoiceInfo[static_cast<std::size_t>(v)].id == l)
            return static_cast<VoiceId>(v);
    char* end = nullptr;
    const long n = std::strtol(text.c_str(), &end, 10);
    if (!text.empty() && end != nullptr && *end == '\0' && n >= 0 && n < kNumVoices)
        return static_cast<VoiceId>(n);
    throw UsageError("unknown voice '" + text
                     + "' (use bd sd lt mt ht lc mc hc rs cl cp ma cb cy oh ch, or 0..15)");
}

Quality parseQuality(const std::string& text)
{
    const std::string l = lower(text);
    if (l == "eco" || l == "0")
        return Quality::Eco;
    if (l == "standard" || l == "std" || l == "1")
        return Quality::Standard;
    if (l == "high" || l == "2")
        return Quality::High;
    throw UsageError("unknown quality '" + text + "' (use eco, standard, high)");
}

const char* qualityName(Quality q)
{
    switch (q)
    {
        case Quality::Eco:
            return "Eco";
        case Quality::Standard:
            return "Standard";
        case Quality::High:
            return "High";
    }
    return "?";
}

seq::VariationMode parseVariationMode(const std::string& text)
{
    const std::string l = lower(text);
    if (l == "a")
        return seq::VariationMode::A;
    if (l == "ab")
        return seq::VariationMode::AB;
    if (l == "b")
        return seq::VariationMode::B;
    throw UsageError("--variation: use A, AB or B");
}

int parsePatternIndex(const std::string& text)
{
    if (!text.empty() && std::isalpha(static_cast<unsigned char>(text[0])))
    {
        const int bank = std::toupper(static_cast<unsigned char>(text[0])) - 'A';
        if (bank < 0 || bank >= seq::kNumBanks)
            throw UsageError("--pattern: bank letter must be A..D");
        const int slot = parseInt("--pattern", text.substr(1), 1, seq::kPatternsPerBank);
        return bank * seq::kPatternsPerBank + slot - 1;
    }
    return parseInt("--pattern", text, 0, seq::kNumPatterns - 1);
}

std::string patternLabel(int index)
{
    return std::string(1, static_cast<char>('A' + index / seq::kPatternsPerBank))
           + std::to_string(index % seq::kPatternsPerBank + 1);
}

void applySetting(MachineParams& params, const std::string& assignment)
{
    const std::size_t eq = assignment.find('=');
    const std::size_t dot = assignment.find('.');
    if (eq == std::string::npos || dot == std::string::npos || dot > eq)
        throw UsageError("--set: expected <voice|master>.<field>=<value>, got '" + assignment
                         + "'");
    const std::string target = lower(assignment.substr(0, dot));
    const std::string field = lower(assignment.substr(dot + 1, eq - dot - 1));
    const std::string text = assignment.substr(eq + 1);
    const std::string opt = "--set " + target + "." + field;

    if (target == "master")
    {
        auto& m = params.engine.master;
        if (field == "volume")
            m.volumeDb = static_cast<float>(parseDouble(opt, text, -60.0, 12.0));
        else if (field == "analog")
            m.analogOut = parseInt(opt, text, 0, 1) != 0;
        else if (field == "drive")
            m.drive = static_cast<float>(parseDouble(opt, text, 0.0, 1.0));
        else if (field == "comp")
            m.comp = static_cast<float>(parseDouble(opt, text, 0.0, 1.0));
        else if (field == "limiter")
            m.limiter = parseInt(opt, text, 0, 1) != 0;
        else
            throw UsageError(opt + ": unknown master field (volume analog drive comp limiter)");
        return;
    }

    const auto v = static_cast<std::size_t>(parseVoice(target));
    auto& vp = params.engine.voice[v];
    auto& ch = params.engine.channel[v];
    if (field == "tune")
        vp.tune = static_cast<float>(parseDouble(opt, text, -12.0, 12.0));
    else if (field == "decay")
        vp.decay = static_cast<float>(parseDouble(opt, text, 0.0, 1.0));
    else if (field == "tone")
        vp.tone = static_cast<float>(parseDouble(opt, text, 0.0, 1.0));
    else if (field == "shape")
        vp.shape = static_cast<float>(parseDouble(opt, text, 0.0, 1.0));
    else if (field == "level")
        ch.level = static_cast<float>(parseDouble(opt, text, 0.0, 1.0));
    else if (field == "pan")
        ch.pan = static_cast<float>(parseDouble(opt, text, -1.0, 1.0));
    else if (field == "drive")
        ch.drive = static_cast<float>(parseDouble(opt, text, 0.0, 1.0));
    else
        throw UsageError(opt + ": unknown voice field (tune decay tone shape level pan drive)");
}

// --- buses and output ----------------------------------------------------------------

BusStorage::BusStorage(int numSamples, bool allBuses)
{
    for (int b = 0; b < kNumBuses; ++b)
        for (auto& ch : data[static_cast<std::size_t>(b)])
            ch.assign(static_cast<std::size_t>(b == 0 || allBuses ? numSamples : 0), 0.0f);
}

BusBuffers BusStorage::view(int offset)
{
    BusBuffers v;
    for (std::size_t b = 0; b < kNumBuses; ++b)
        for (std::size_t c = 0; c < 2; ++c)
            v.ch[b][c] = data[b][c].empty() ? nullptr : data[b][c].data() + offset;
    return v;
}

void writeStereoOrMono(const std::string& path, const std::vector<float>& left,
                       const std::vector<float>& right, int numFrames, int sampleRate, int channels,
                       const std::string& formatText, bool quiet)
{
    SampleFormat format{};
    if (!parseSampleFormat(formatText, format))
        throw UsageError("--format: use 16, 24 or 32f");

    std::vector<float> mono;
    std::vector<const float*> planes;
    if (channels == 1)
    {
        mono.resize(static_cast<std::size_t>(numFrames));
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] = (left[i] + right[i]) * 0.5f;
        planes.push_back(mono.data());
    }
    else
    {
        planes.push_back(left.data());
        planes.push_back(right.data());
    }

    WriteStats stats;
    std::string error;
    if (!writeWav(path, planes, numFrames, sampleRate, format, &stats, error))
        throw RunError(error);
    if (!quiet)
    {
        char peak[48] = "silent";
        if (stats.peak > 0.0f)
            std::snprintf(peak, sizeof peak, "peak %.2f dBFS",
                          20.0 * std::log10(static_cast<double>(stats.peak)));
        std::fprintf(stderr, "wrote %s: %d frames, %d ch, %d Hz, %s, %s%s\n", path.c_str(),
                     numFrames, channels, sampleRate, sampleFormatName(format), peak,
                     stats.clipped > 0 ? " (CLIPPED)" : "");
    }
    if (stats.clipped > 0)
        std::fprintf(stderr, "warning: %s: %lld samples clipped\n", path.c_str(),
                     static_cast<long long>(stats.clipped));
}

// --- pattern sources -----------------------------------------------------------------

void loadPatterns(const PatternSource& source, LoadedPatterns& out)
{
    out.bank = seq::PatternBank{};
    if (!source.textFile.empty())
    {
        PatternText text;
        std::string error;
        if (!loadPatternText(source.textFile, text, error))
            throw RunError(error);
        out.bank.patterns[0] = text.pattern;
        out.selected = 0;
        out.name = text.name;
        out.tempo = text.tempo;
        return;
    }
    if (source.bank < 0 || source.bank >= factory::numBanks())
        throw UsageError("--bank: factory bank " + std::to_string(source.bank)
                         + " does not exist (0.." + std::to_string(factory::numBanks() - 1) + ")");
    factory::buildBank(source.bank, out.bank);
    out.selected = source.pattern;
    out.name = std::string(factory::patternName(source.bank, source.pattern));
    out.tempo = static_cast<double>(factory::bankInfo(source.bank).tempo);
}

bool readPatternSourceOption(ArgReader& r, const std::string& opt, PatternSource& src)
{
    if (opt == "--bank")
        src.bank = parseInt(opt, r.value(), 0, 1 << 20);
    else if (opt == "--pattern")
        src.pattern = parsePatternIndex(r.value());
    else if (opt == "--text")
        src.textFile = r.value();
    else
        return false;
    return true;
}

} // namespace mw808::tools

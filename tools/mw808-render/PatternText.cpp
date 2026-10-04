// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

#include "PatternText.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mw808::tools {

namespace {

std::string lower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upper(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Track index for a code (0..15 voices, 16 = AC), or -1.
int trackFromCode(const std::string& code)
{
    const std::string u = upper(code);
    if (u == "AC")
        return seq::kAccentTrack;
    for (int v = 0; v < kNumVoices; ++v)
        if (kVoiceInfo[static_cast<std::size_t>(v)].code == u)
            return v;
    return -1;
}

bool parseInt(const std::string& s, int lo, int hi, int& out)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || v < lo || v > hi)
        return false;
    out = static_cast<int>(v);
    return true;
}

bool parseScale(const std::string& s, seq::Scale& out)
{
    const std::string l = lower(s);
    if (l == "8t" || l == "1")
        out = seq::Scale::EighthTriplet;
    else if (l == "16t" || l == "2")
        out = seq::Scale::SixteenthTriplet;
    else if (l == "16" || l == "16th" || l == "3")
        out = seq::Scale::Sixteenth;
    else if (l == "32" || l == "32nd" || l == "4")
        out = seq::Scale::ThirtySecond;
    else if (l == "8" || l == "8th")
        out = seq::Scale::Eighth;
    else
        return false;
    return true;
}

// Parses the step tokens of one track line into `cells`. Returns the number of
// steps written, or -1 with `error` set.
int parseSteps(const std::string& text, int track, std::array<seq::Step, seq::kMaxSteps>& cells,
               std::string& error)
{
    int n = 0;
    std::size_t i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '|')
        {
            ++i;
            continue;
        }
        if (n >= seq::kMaxSteps)
        {
            error = "more than 64 steps";
            return -1;
        }
        seq::Step step;
        switch (c)
        {
            case '-':
            case '.':
                break;
            case 'o':
                step.setLevel(seq::StepLevel::Weak);
                break;
            case 'x':
                step.setLevel(seq::StepLevel::Normal);
                break;
            case 'X':
                step.setLevel(seq::StepLevel::Accent);
                break;
            default:
                error = std::string("unknown step character '") + c + "' (use - . o x X)";
                return -1;
        }
        if (track == seq::kAccentTrack && step.on())
            step.setLevel(seq::StepLevel::Normal);
        ++i;

        if (i < text.size() && text[i] == '(')
        {
            const std::size_t close = text.find(')', i);
            if (close == std::string::npos)
            {
                error = "unterminated '(' modifier";
                return -1;
            }
            const std::string mods = text.substr(i + 1, close - i - 1);
            for (std::size_t k = 0; k < mods.size(); ++k)
            {
                const char m = mods[k];
                if (m == 'f' || m == 'F')
                    step.setFlam(true);
                else if (m >= '1' && m <= '4')
                    step.setSubSteps(m - '0');
                else if ((m == 'p' || m == 'P') && k + 1 < mods.size() && mods[k + 1] >= '0'
                         && mods[k + 1] <= '7')
                    step.setProbabilityCode(mods[++k] - '0');
                else if (m != ' ')
                {
                    error = "bad modifier '" + mods + "' (use f, 1..4, p0..p7)";
                    return -1;
                }
            }
            if (!step.on())
            {
                error = "modifier on an off step";
                return -1;
            }
            i = close + 1;
        }
        cells[static_cast<std::size_t>(n++)] = step;
    }
    return n;
}

std::string stepToken(const seq::Step& s, bool accentRow)
{
    std::string t;
    switch (s.level())
    {
        case seq::StepLevel::Off:
            return "-";
        case seq::StepLevel::Weak:
            t = accentRow ? "x" : "o";
            break;
        case seq::StepLevel::Normal:
            t = "x";
            break;
        case seq::StepLevel::Accent:
            t = accentRow ? "x" : "X";
            break;
    }
    std::string mods;
    if (s.flam())
        mods += 'f';
    if (s.subSteps() > 1)
        mods += static_cast<char>('0' + s.subSteps());
    if (s.probabilityCode() != 0)
    {
        mods += 'p';
        mods += static_cast<char>('0' + s.probabilityCode());
    }
    if (!mods.empty())
        t += "(" + mods + ")";
    return t;
}

} // namespace

const char* scaleText(seq::Scale s)
{
    switch (s)
    {
        case seq::Scale::EighthTriplet:
            return "8t";
        case seq::Scale::SixteenthTriplet:
            return "16t";
        case seq::Scale::Sixteenth:
            return "16";
        case seq::Scale::ThirtySecond:
            return "32";
        case seq::Scale::Eighth:
            return "8";
        case seq::Scale::kNumScales:
            break;
    }
    return "16";
}

bool parsePatternText(const std::string& text, PatternText& out, std::string& error)
{
    PatternText result;
    seq::Pattern& p = result.pattern;
    int variation = 0;

    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    const auto fail = [&](const std::string& msg) {
        error = "line " + std::to_string(lineNo) + ": " + msg;
        return false;
    };

    while (std::getline(in, raw))
    {
        ++lineNo;
        const std::size_t hash = raw.find('#');
        std::string line = hash == std::string::npos ? raw : raw.substr(0, hash);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key))
            continue;
        std::string rest;
        std::getline(ls, rest);
        const std::size_t first = rest.find_first_not_of(" \t");
        rest = first == std::string::npos ? std::string() : rest.substr(first);
        const std::string k = lower(key);

        if (k == "name")
        {
            result.name = rest;
        }
        else if (k == "length")
        {
            int v = 0;
            if (!parseInt(rest, 1, seq::kMaxSteps, v))
                return fail("length must be 1..64");
            p.length = static_cast<std::uint8_t>(v);
        }
        else if (k == "scale")
        {
            if (!parseScale(rest, p.scale))
                return fail("scale must be one of 8t 16t 16 32 8");
        }
        else if (k == "shuffle")
        {
            int v = 0;
            if (!parseInt(rest, 0, 100, v))
                return fail("shuffle must be 0..100");
            p.shuffle = static_cast<std::uint8_t>(v);
        }
        else if (k == "tempo")
        {
            char* end = nullptr;
            const double v = std::strtod(rest.c_str(), &end);
            if (rest.empty() || end == nullptr || *end != '\0' || !(v >= 20.0 && v <= 400.0))
                return fail("tempo must be 20..400 BPM");
            result.tempo = v;
        }
        else if (k == "tracklength")
        {
            std::istringstream ts(rest);
            std::string code, len;
            ts >> code >> len;
            const int track = trackFromCode(code);
            int v = 0;
            if (track < 0 || track >= kNumVoices)
                return fail("tracklength needs a voice code (not AC)");
            if (!parseInt(len, 1, seq::kMaxSteps, v))
                return fail("track length must be 1..64");
            p.trackLength[static_cast<std::size_t>(track)] = static_cast<std::uint8_t>(v);
        }
        else if (k == "variation")
        {
            const std::string v = upper(rest);
            if (v == "A")
                variation = 0;
            else if (v == "B")
                variation = 1;
            else
                return fail("variation must be A or B");
        }
        else
        {
            const int track = trackFromCode(key);
            if (track < 0)
                return fail("unknown keyword or track '" + key + "'");
            std::array<seq::Step, seq::kMaxSteps> cells{};
            std::string stepError;
            if (parseSteps(rest, track, cells, stepError) < 0)
                return fail(stepError);
            p.steps[static_cast<std::size_t>(variation)][static_cast<std::size_t>(track)] = cells;
        }
    }

    out = result;
    return true;
}

bool loadPatternText(const std::string& path, PatternText& out, std::string& error)
{
    std::ifstream f(path);
    if (!f)
    {
        error = "cannot open pattern file '" + path + "'";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!parsePatternText(ss.str(), out, error))
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

std::string formatPatternText(const seq::Pattern& pattern, const std::string& name, double tempo)
{
    std::ostringstream o;
    if (!name.empty())
        o << "name     " << name << "\n";
    o << "length   " << static_cast<int>(pattern.length) << "\n";
    o << "scale    " << scaleText(pattern.scale) << "\n";
    o << "shuffle  " << static_cast<int>(pattern.shuffle) << "\n";
    if (tempo > 0.0)
        o << "tempo    " << tempo << "\n";
    for (int v = 0; v < kNumVoices; ++v)
        if (pattern.trackLength[static_cast<std::size_t>(v)] != 0)
            o << "tracklength " << kVoiceInfo[static_cast<std::size_t>(v)].code << " "
              << static_cast<int>(pattern.trackLength[static_cast<std::size_t>(v)]) << "\n";

    for (int var = 0; var < seq::kNumVariations; ++var)
    {
        bool any = false;
        for (int t = 0; t < seq::kNumTracks && !any; ++t)
            for (int s = 0; s < seq::kMaxSteps && !any; ++s)
                any = pattern.at(var, t, s).bits != 0;
        if (var == 1 && !any)
            continue;
        o << "\nvariation " << (var == 0 ? "A" : "B") << "\n";
        for (int t = 0; t < seq::kNumTracks; ++t)
        {
            int last = -1;
            for (int s = 0; s < seq::kMaxSteps; ++s)
                if (pattern.at(var, t, s).bits != 0)
                    last = s;
            if (last < 0)
                continue;
            const int count = std::max(last + 1, pattern.lengthOf(t));
            o << (t == seq::kAccentTrack ? std::string_view("AC")
                                         : kVoiceInfo[static_cast<std::size_t>(t)].code)
              << "  ";
            for (int s = 0; s < count; ++s)
            {
                if (s > 0 && s % 4 == 0)
                    o << ' ';
                o << stepToken(pattern.at(var, t, s), t == seq::kAccentTrack);
            }
            o << "\n";
        }
    }
    return o.str();
}

} // namespace mw808::tools

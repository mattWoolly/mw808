// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// Shared plumbing of the mw808-render subcommands: argument reading, value
// parsing, bus storage and the pattern sources.

#pragma once

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "mw808/Machine.h"

namespace mw808::tools {

// Bad command line: printed with a hint, exit code 2.
struct UsageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Runtime failure (I/O, invalid file content): exit code 1.
struct RunError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Walks "--name value", "--name=value" and bare flags.
class ArgReader
{
public:
    explicit ArgReader(std::vector<std::string> args)
        : args_(std::move(args))
    {}

    bool more() const { return pos_ < args_.size(); }
    // Next option name (e.g. "--rate"). Throws on a stray positional argument.
    std::string option();
    // Value of the option just read.
    std::string value();
    // The option just read is a flag: it must not carry "=value".
    void flag();
    [[noreturn]] void unknown(const std::string& subcommand) const;

private:
    std::vector<std::string> args_;
    std::size_t pos_ = 0;
    std::string current_;
    std::string inline_;
    bool hasInline_ = false;
};

double parseDouble(const std::string& option, const std::string& text, double lo, double hi);
int parseInt(const std::string& option, const std::string& text, int lo, int hi);
std::uint32_t parseU32(const std::string& option, const std::string& text);
std::vector<std::string> splitList(const std::string& text, char sep = ',');

VoiceId parseVoice(const std::string& text);
Quality parseQuality(const std::string& text);
const char* qualityName(Quality q);
seq::VariationMode parseVariationMode(const std::string& text);
// Global pattern index from "0".."63" or "A1".."D16".
int parsePatternIndex(const std::string& text);
std::string patternLabel(int index); // "A1".."D16"

// Applies one "--set key=value" override. Keys:
//   <voice>.tune|decay|tone|shape|level|pan|drive   e.g. bd.decay=0.8
//   master.volume (dB) | master.analog (0/1) | master.drive | master.comp | master.limiter (0/1)
void applySetting(MachineParams& params, const std::string& assignment);

// Stereo storage for every bus, with a BusBuffers view at an offset.
struct BusStorage
{
    BusStorage(int numSamples, bool allBuses);
    BusBuffers view(int offset);
    std::vector<float>& left(int bus) { return data[static_cast<std::size_t>(bus)][0]; }
    std::vector<float>& right(int bus) { return data[static_cast<std::size_t>(bus)][1]; }

    std::array<std::array<std::vector<float>, 2>, kNumBuses> data;
};

// Writes bus 0..N of `storage` (or the given pair) to a WAV, mono = (L+R)/2.
void writeStereoOrMono(const std::string& path, const std::vector<float>& left,
                       const std::vector<float>& right, int numFrames, int sampleRate, int channels,
                       const std::string& formatText, bool quiet);

// Where a pattern comes from: a factory bank or a text file.
struct PatternSource
{
    int bank = 0;         // factory bank index
    int pattern = 0;      // global pattern index in that bank
    std::string textFile; // when set, the bank is empty but for pattern 0 = this file
};

struct LoadedPatterns
{
    seq::PatternBank bank{};
    int selected = 0;
    std::string name;
    double tempo = 0.0; // suggested; 0 = none
};

// Throws RunError when the text file does not parse.
void loadPatterns(const PatternSource& source, LoadedPatterns& out);

// Pattern-source options shared by `pattern` and `midi`; returns true if consumed.
bool readPatternSourceOption(ArgReader& r, const std::string& opt, PatternSource& src);

int cmdVoice(const std::vector<std::string>& args);
int cmdPattern(const std::vector<std::string>& args);
int cmdMidi(const std::vector<std::string>& args);
int cmdBench(const std::vector<std::string>& args);

} // namespace mw808::tools

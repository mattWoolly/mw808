// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The `voice`, `pattern` and `midi` subcommands. Everything is rendered
// through a full mw808::Machine, exactly as the plugin drives it: voice hits
// are UI auditions posted at block boundaries, patterns play from the
// sequencer in HostSync against a rolling host transport.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>

#include "Common.h"
#include "PatternText.h"
#include "mw808/Factory.h"
#include "mw808/MidiMap.h"
#include "mw808/seq/MidiFile.h"

namespace mw808::tools {

namespace {

constexpr double kMinRate = 8000.0;
constexpr double kMaxRate = 384000.0;
constexpr int kMaxBlock = 16384;
constexpr double kMaxSeconds = 3600.0;

void setCleanMaster(MasterParams& m)
{
    m = MasterParams{};
    m.analogOut = false;
    m.limiter = false;
    m.drive = 0.0f;
    m.comp = 0.0f;
    m.volumeDb = 0.0f;
}

seq::AutoFill parseAutoFill(const std::string& text)
{
    const int n = parseInt("--auto-fill", text, 0, 16);
    switch (n)
    {
        case 0:
            return seq::AutoFill::Manual;
        case 2:
            return seq::AutoFill::Every2;
        case 4:
            return seq::AutoFill::Every4;
        case 8:
            return seq::AutoFill::Every8;
        case 12:
            return seq::AutoFill::Every12;
        case 16:
            return seq::AutoFill::Every16;
        default:
            throw UsageError("--auto-fill: use 0 (manual), 2, 4, 8, 12 or 16");
    }
}

bool patternEmpty(const seq::Pattern& p)
{
    for (const auto& var : p.steps)
        for (const auto& track : var)
            for (const auto& s : track)
                if (s.on())
                    return false;
    return true;
}

std::string requireOut(const std::string& out, const char* sub)
{
    if (out.empty())
        throw UsageError(std::string(sub) + ": --out <file> is required");
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------
// voice
// ---------------------------------------------------------------------------------

int cmdVoice(const std::vector<std::string>& args)
{
    int voiceIndex = -1;
    std::vector<std::string> hitTokens{"normal"};
    double spacing = 0.5, length = -1.0, rate = 48000.0, accent = 0.7, level = 1.0, pan = 0.0;
    int block = 256, channels = 2;
    Quality quality = Quality::Standard;
    std::uint32_t seed = 0;
    float variance = 0.0f;
    std::string masterMode = "clean", format = "24", out;
    bool quiet = false;
    struct Override
    {
        bool set = false;
        float value = 0.0f;
    } tune, decay, tone, shape;
    std::vector<std::string> sets;

    ArgReader r(args);
    while (r.more())
    {
        const std::string opt = r.option();
        if (opt == "--voice")
            voiceIndex = parseVoice(r.value());
        else if (opt == "--hits" || opt == "--volts")
            hitTokens = splitList(r.value());
        else if (opt == "--spacing")
            spacing = parseDouble(opt, r.value(), 0.001, 60.0);
        else if (opt == "--length")
            length = parseDouble(opt, r.value(), 0.001, kMaxSeconds);
        else if (opt == "--accent")
            accent = parseDouble(opt, r.value(), 0.0, 1.0);
        else if (opt == "--tune")
            tune = {true, static_cast<float>(parseDouble(opt, r.value(), -12.0, 12.0))};
        else if (opt == "--decay")
            decay = {true, static_cast<float>(parseDouble(opt, r.value(), 0.0, 1.0))};
        else if (opt == "--tone")
            tone = {true, static_cast<float>(parseDouble(opt, r.value(), 0.0, 1.0))};
        else if (opt == "--shape")
            shape = {true, static_cast<float>(parseDouble(opt, r.value(), 0.0, 1.0))};
        else if (opt == "--level")
            level = parseDouble(opt, r.value(), 0.0, 1.0);
        else if (opt == "--pan")
            pan = parseDouble(opt, r.value(), -1.0, 1.0);
        else if (opt == "--master")
            masterMode = r.value();
        else if (opt == "--set")
            sets.push_back(r.value());
        else if (opt == "--rate")
            rate = parseDouble(opt, r.value(), kMinRate, kMaxRate);
        else if (opt == "--block")
            block = parseInt(opt, r.value(), 1, kMaxBlock);
        else if (opt == "--quality")
            quality = parseQuality(r.value());
        else if (opt == "--seed")
            seed = parseU32(opt, r.value());
        else if (opt == "--variance")
            variance = static_cast<float>(parseDouble(opt, r.value(), 0.0, 1.0));
        else if (opt == "--channels")
            channels = parseInt(opt, r.value(), 1, 2);
        else if (opt == "--format")
            format = r.value();
        else if (opt == "--out" || opt == "-o")
            out = r.value();
        else if (opt == "--quiet" || opt == "-q")
        {
            r.flag();
            quiet = true;
        }
        else
            r.unknown("voice");
    }
    if (voiceIndex < 0)
        throw UsageError("voice: --voice <code> is required");
    requireOut(out, "voice");
    if (masterMode != "clean" && masterMode != "stock")
        throw UsageError("--master: use clean or stock");

    const auto v = static_cast<std::size_t>(voiceIndex);
    std::vector<float> volts;
    for (const auto& t : hitTokens)
    {
        if (t == "weak")
            volts.push_back(Hit::weak().volts);
        else if (t == "normal")
            volts.push_back(Hit::normal().volts);
        else if (t == "accent")
            volts.push_back(Hit::accented(static_cast<float>(accent)).volts);
        else
            volts.push_back(static_cast<float>(
                parseDouble("--hits", t, 0.5, static_cast<double>(Hit::kMaxVolts))));
    }
    if (volts.empty())
        throw UsageError("--hits: need at least one hit");

    MachineParams params;
    params.seq.mode = seq::Mode::Off;
    params.seq.accent = static_cast<float>(accent);
    params.quality = quality;
    params.unitSeed = seed;
    params.variance = variance;
    for (std::size_t i = 0; i < kNumVoices; ++i)
        params.engine.voice[i] = kVoiceInfo[i].defaults;
    auto& vp = params.engine.voice[v];
    if (tune.set)
        vp.tune = tune.value;
    if (decay.set)
        vp.decay = decay.value;
    if (tone.set)
        vp.tone = tone.value;
    if (shape.set)
        vp.shape = shape.value;
    params.engine.channel[v].level = static_cast<float>(level);
    params.engine.channel[v].pan = static_cast<float>(pan);
    if (masterMode == "clean")
        setCleanMaster(params.engine.master);
    for (const auto& s : sets)
        applySetting(params, s);

    if (length < 0.0)
        length = spacing * static_cast<double>(volts.size());
    const auto total = static_cast<int>(std::llround(length * rate));
    std::vector<int> hitAt;
    for (std::size_t i = 0; i < volts.size(); ++i)
        hitAt.push_back(static_cast<int>(std::llround(static_cast<double>(i) * spacing * rate)));
    if (spacing * rate < 1.0)
        throw UsageError("--spacing is shorter than one sample");

    auto machine = std::make_unique<Machine>();
    machine->prepare(rate, block);
    BusStorage buses(total, false);

    std::size_t next = 0;
    int fired = 0;
    for (int pos = 0; pos < total;)
    {
        while (next < hitAt.size() && hitAt[next] <= pos)
        {
            Command c;
            c.type = Command::Type::Audition;
            c.index = voiceIndex;
            c.value = volts[next];
            if (!machine->post(c))
                throw RunError("command queue full");
            ++next;
            ++fired;
        }
        int n = std::min(block, total - pos);
        if (next < hitAt.size())
            n = std::min(n, hitAt[next] - pos);
        Machine::ProcessArgs a;
        a.out = buses.view(pos);
        a.numSamples = n;
        machine->process(a, params);
        pos += n;
    }

    if (!quiet)
    {
        std::fprintf(stderr, "voice %s (%s): %d hit(s) at", std::string(kVoiceInfo[v].code).c_str(),
                     std::string(kVoiceInfo[v].name).c_str(), fired);
        for (int i = 0; i < fired; ++i)
            std::fprintf(stderr, " %.2fV", static_cast<double>(volts[static_cast<std::size_t>(i)]));
        std::fprintf(
            stderr,
            ", tune %.2f decay %.3f tone %.3f shape %.3f, %s, %.0f Hz, seed %u, variance %.2f\n",
            static_cast<double>(vp.tune), static_cast<double>(vp.decay),
            static_cast<double>(vp.tone), static_cast<double>(vp.shape), qualityName(quality), rate,
            seed, static_cast<double>(variance));
    }
    if (static_cast<std::size_t>(fired) < volts.size())
        std::fprintf(stderr, "warning: %zu hit(s) fall after --length and were not rendered\n",
                     volts.size() - static_cast<std::size_t>(fired));

    writeStereoOrMono(out, buses.left(0), buses.right(0), total, static_cast<int>(rate), channels,
                      format, quiet);
    return 0;
}

// ---------------------------------------------------------------------------------
// pattern
// ---------------------------------------------------------------------------------

int cmdPattern(const std::vector<std::string>& args)
{
    PatternSource src;
    int measures = 2, block = 512, channels = 2, kitIndex = 0;
    double tail = 1.0, tempo = 0.0, rate = 48000.0, swing = 1.0, flamMs = 24.0, accent = -1.0;
    Quality quality = Quality::Standard;
    std::uint32_t seed = 0;
    float variance = 0.0f;
    seq::VariationMode variation = seq::VariationMode::A;
    seq::AutoFill autoFill = seq::AutoFill::Manual;
    bool chain = false, printText = false, quiet = false;
    std::string format = "24", out, stemsDir;
    std::vector<std::string> sets;

    ArgReader r(args);
    while (r.more())
    {
        const std::string opt = r.option();
        if (readPatternSourceOption(r, opt, src))
            continue;
        if (opt == "--measures")
            measures = parseInt(opt, r.value(), 1, 10000);
        else if (opt == "--tail")
            tail = parseDouble(opt, r.value(), 0.0, 60.0);
        else if (opt == "--tempo")
            tempo = parseDouble(opt, r.value(), 20.0, 400.0);
        else if (opt == "--rate")
            rate = parseDouble(opt, r.value(), kMinRate, kMaxRate);
        else if (opt == "--block")
            block = parseInt(opt, r.value(), 1, kMaxBlock);
        else if (opt == "--quality")
            quality = parseQuality(r.value());
        else if (opt == "--seed")
            seed = parseU32(opt, r.value());
        else if (opt == "--variance")
            variance = static_cast<float>(parseDouble(opt, r.value(), 0.0, 1.0));
        else if (opt == "--kit")
        {
            const std::string k = r.value();
            kitIndex = k == "stock" ? -1 : parseInt(opt, k, 0, 1 << 20);
        }
        else if (opt == "--accent")
            accent = parseDouble(opt, r.value(), 0.0, 1.0);
        else if (opt == "--variation")
            variation = parseVariationMode(r.value());
        else if (opt == "--swing-scale")
            swing = parseDouble(opt, r.value(), 0.0, 1.0);
        else if (opt == "--flam-ms")
            flamMs = parseDouble(opt, r.value(), 0.0, 60.0);
        else if (opt == "--auto-fill")
            autoFill = parseAutoFill(r.value());
        else if (opt == "--chain")
        {
            r.flag();
            chain = true;
        }
        else if (opt == "--set")
            sets.push_back(r.value());
        else if (opt == "--channels")
            channels = parseInt(opt, r.value(), 1, 2);
        else if (opt == "--format")
            format = r.value();
        else if (opt == "--stems")
            stemsDir = r.value();
        else if (opt == "--out" || opt == "-o")
            out = r.value();
        else if (opt == "--print-text")
        {
            r.flag();
            printText = true;
        }
        else if (opt == "--quiet" || opt == "-q")
        {
            r.flag();
            quiet = true;
        }
        else
            r.unknown("pattern");
    }

    auto loaded = std::make_unique<LoadedPatterns>();
    loadPatterns(src, *loaded);
    const seq::Pattern& selected =
        loaded->bank.patterns[static_cast<std::size_t>(loaded->selected)];
    if (tempo <= 0.0)
        tempo = loaded->tempo > 0.0 ? loaded->tempo : 120.0;

    if (printText)
    {
        std::cout << "# mw808 pattern "
                  << (src.textFile.empty() ? patternLabel(loaded->selected) : src.textFile) << "\n"
                  << formatPatternText(selected, loaded->name, tempo);
        if (out.empty())
            return 0;
    }
    requireOut(out, "pattern");
    if (kitIndex >= factory::numKits())
        throw UsageError("--kit: factory kit " + std::to_string(kitIndex) + " does not exist (0.."
                         + std::to_string(factory::numKits() - 1) + ")");
    if (patternEmpty(selected))
        std::fprintf(stderr, "warning: pattern %s is empty\n",
                     patternLabel(loaded->selected).c_str());
    if (chain && loaded->bank.chain.length == 0)
        std::fprintf(stderr, "warning: --chain given but the bank has no chain\n");

    // "--kit stock": the built-in defaults (every voice at its stock settings,
    // default mixer and master), independent of the factory library.
    factory::Kit stockKit;
    stockKit.name = "stock defaults";
    for (std::size_t v = 0; v < kNumVoices; ++v)
        stockKit.voice[v] = kVoiceInfo[v].defaults;
    const factory::Kit& kit = kitIndex < 0 ? stockKit : factory::kit(kitIndex);
    MachineParams params;
    params.engine.voice = kit.voice;
    params.engine.channel = kit.channel;
    params.engine.master = kit.master;
    params.seq.mode = seq::Mode::HostSync;
    params.seq.accent = accent >= 0.0 ? static_cast<float>(accent) : kit.accent;
    params.seq.flamMs = static_cast<float>(flamMs);
    params.seq.swingScale = static_cast<float>(swing);
    params.seq.variation = variation;
    params.seq.autoFill = autoFill;
    params.seq.chain = chain;
    params.seq.internalBpm = tempo;
    params.quality = quality;
    params.unitSeed = seed;
    params.variance = variance;
    for (const auto& s : sets)
        applySetting(params, s);

    const double measureQn =
        static_cast<double>(selected.length) * seq::stepLengthQn(selected.scale);
    const double seconds = static_cast<double>(measures) * measureQn * 60.0 / tempo + tail;
    if (seconds > kMaxSeconds)
        throw UsageError("render would be longer than an hour");
    const auto total = static_cast<int>(std::llround(seconds * rate));

    // One pass of the machine. `outputs` routes voices to buses; only bus 0 is
    // connected unless `allBuses`, so voices assigned elsewhere fall back to the
    // main mix exactly as in a host with the individual outs disabled.
    const auto renderPass = [&](const std::array<std::uint8_t, kNumVoices>* outputs,
                                bool allBuses) {
        auto storage = std::make_unique<BusStorage>(total, allBuses);
        auto machine = std::make_unique<Machine>();
        machine->prepare(rate, block);
        for (int i = 0; i < seq::kNumPatterns; ++i)
            machine->sequencer().setPattern(i, loaded->bank.patterns[static_cast<std::size_t>(i)]);
        machine->sequencer().setChain(loaded->bank.chain);
        machine->sequencer().selectPattern(loaded->selected);

        MachineParams p = params;
        if (outputs != nullptr)
            for (std::size_t v = 0; v < kNumVoices; ++v)
                p.engine.channel[v].output = (*outputs)[v];

        for (int pos = 0; pos < total;)
        {
            const int n = std::min(block, total - pos);
            Machine::ProcessArgs a;
            a.transport.hostValid = true;
            a.transport.playing = true;
            a.transport.bpm = tempo;
            a.transport.ppq = static_cast<double>(pos) * tempo / (60.0 * rate);
            a.out = storage->view(pos);
            a.numSamples = n;
            machine->process(a, p);
            pos += n;
        }
        return storage;
    };

    if (!quiet)
        std::fprintf(stderr,
                     "pattern %s%s%s: %d step(s) at scale %s, shuffle %d; %d measure(s) at %.2f "
                     "BPM + %.2f s tail; "
                     "kit %d (%s), %s, %.0f Hz, block %d\n",
                     src.textFile.empty() ? patternLabel(loaded->selected).c_str()
                                          : src.textFile.c_str(),
                     loaded->name.empty() ? "" : " ", loaded->name.c_str(),
                     static_cast<int>(selected.length), scaleText(selected.scale),
                     static_cast<int>(selected.shuffle), measures, tempo, tail, kitIndex,
                     std::string(kit.name).c_str(), qualityName(quality), rate, block);

    {
        const auto main = renderPass(nullptr, false);
        writeStereoOrMono(out, main->left(0), main->right(0), total, static_cast<int>(rate),
                          channels, format, quiet);
    }

    if (!stemsDir.empty())
    {
        // 11 individual outs for 16 voices: two passes. Stems are taken from
        // the individual outs, so they are pre-master (no output stage,
        // compressor, limiter or master volume), as on the hardware.
        std::array<std::uint8_t, kNumVoices> routeA{}, routeB{};
        for (std::size_t v = 0; v < kNumVoices; ++v)
        {
            routeA[v] = v < 11 ? static_cast<std::uint8_t>(v + 1) : 0;
            routeB[v] = v >= 11 ? static_cast<std::uint8_t>(v - 10) : 0;
        }
        const auto passA = renderPass(&routeA, true);
        const auto passB = renderPass(&routeB, true);
        for (std::size_t v = 0; v < kNumVoices; ++v)
        {
            BusStorage& s = v < 11 ? *passA : *passB;
            const int bus = v < 11 ? static_cast<int>(v) + 1 : static_cast<int>(v) - 10;
            const std::string path = stemsDir + "/" + std::string(kVoiceInfo[v].id) + ".wav";
            writeStereoOrMono(path, s.left(bus), s.right(bus), total, static_cast<int>(rate),
                              channels, format, quiet);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------------
// midi
// ---------------------------------------------------------------------------------

int cmdMidi(const std::vector<std::string>& args)
{
    PatternSource src;
    seq::MidiFileOptions options;
    int variation = 0;
    double tempo = 0.0;
    std::string out;
    bool quiet = false;

    ArgReader r(args);
    while (r.more())
    {
        const std::string opt = r.option();
        if (readPatternSourceOption(r, opt, src))
            continue;
        if (opt == "--variation")
        {
            const std::string v = r.value();
            if (v == "A" || v == "a" || v == "0")
                variation = 0;
            else if (v == "B" || v == "b" || v == "1")
                variation = 1;
            else
                throw UsageError("--variation: use A or B");
        }
        else if (opt == "--tempo")
            tempo = parseDouble(opt, r.value(), 20.0, 400.0);
        else if (opt == "--flam-ms")
            options.flamMs = static_cast<float>(parseDouble(opt, r.value(), 0.0, 60.0));
        else if (opt == "--swing-scale")
            options.swingScale = static_cast<float>(parseDouble(opt, r.value(), 0.0, 1.0));
        else if (opt == "--one-step-notes")
        {
            r.flag();
            options.oneStepNotes = true;
        }
        else if (opt == "--no-tempo")
        {
            r.flag();
            options.includeTempo = false;
        }
        else if (opt == "--no-embed")
        {
            r.flag();
            options.embedPattern = false;
        }
        else if (opt == "--accent-note")
            options.accentNote = parseInt(opt, r.value(), 0, 127);
        else if (opt == "--out" || opt == "-o")
            out = r.value();
        else if (opt == "--quiet" || opt == "-q")
        {
            r.flag();
            quiet = true;
        }
        else
            r.unknown("midi");
    }
    requireOut(out, "midi");

    auto loaded = std::make_unique<LoadedPatterns>();
    loadPatterns(src, *loaded);
    options.bpm = tempo > 0.0 ? tempo : (loaded->tempo > 0.0 ? loaded->tempo : 120.0);
    const seq::Pattern& pattern = loaded->bank.patterns[static_cast<std::size_t>(loaded->selected)];

    const std::vector<std::uint8_t> bytes =
        seq::writeMidiFile(pattern, variation, kVoiceNote, options);
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f)
        throw RunError("cannot open '" + out + "' for writing");
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    if (!f)
        throw RunError("write failed for '" + out + "'");
    if (!quiet)
        std::fprintf(stderr, "wrote %s: %zu bytes, pattern %s variation %c, %.2f BPM%s\n",
                     out.c_str(), bytes.size(),
                     src.textFile.empty() ? patternLabel(loaded->selected).c_str()
                                          : src.textFile.c_str(),
                     variation == 0 ? 'A' : 'B', options.bpm,
                     options.embedPattern ? ", exact pattern embedded" : "");
    return 0;
}

} // namespace mw808::tools

// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// mw808-render: JUCE-free command-line front end to the mw808 core. Renders
// voices and patterns to WAV, exports patterns as MIDI files and measures
// realtime performance. Links mw808_core only.

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "Common.h"
#include "PatternText.h"

#ifndef MW808_VERSION
#define MW808_VERSION "unknown"
#endif
#ifndef MW808_BUILD_TYPE
#define MW808_BUILD_TYPE "unknown"
#endif

namespace {

constexpr const char* kUsage =
    R"(mw808-render - render, export and benchmark the mw808 drum machine core

USAGE
  mw808-render <command> [options]
  mw808-render --help | help [voice|pattern|midi|bench|pattern-format]
  mw808-render --version

COMMANDS
  voice     render one or more hits of a single voice to a WAV file
  pattern   render measures of a pattern through the full machine (sequencer
            in host-sync against a rolling transport) to a WAV file
  midi      export one variation of a pattern as a standard MIDI file
  bench     realtime performance report for a dense worst-case pattern

Options take "--name value" or "--name=value". Exit codes: 0 ok, 1 runtime
failure (I/O, bad pattern file, bench gate failed), 2 bad command line.
Every render is deterministic: same build, same options, same bytes.
)";

constexpr const char* kVoiceHelp = R"(mw808-render voice --voice <code> --out <file.wav> [options]

  Plays hits on one voice through a complete machine (as UI auditions at
  exact sample positions). The voice's channel is set to LEVEL 1.0, centre
  pan, routed to the main bus; other voices stay silent.

  --voice <code>        bd sd lt mt ht lc mc hc rs cl cp ma cb cy oh ch, or 0..15
  --hits <list>         comma list of hits, each "weak" (2.2 V), "normal" (4 V),
                        "accent" (from --accent) or a trigger height in volts
                        0.5..14. Alias: --volts. Default: normal
  --accent <0..1>       ACCENT knob used by "accent" hits (default 0.7)
  --spacing <s>         seconds between hits (default 0.5); hit 1 is at t = 0
  --length <s>          total length (default: hits x spacing)
  --tune <st>           -12..+12 semitones (default: the voice's stock value)
  --decay/--tone/--shape <0..1>   (default: the voice's stock values)
  --level <0..1>        channel level (default 1.0)
  --pan <-1..1>         channel pan (default 0)
  --master clean|stock  clean (default): analog output stage, compressor and
                        limiter off, 0 dB. stock: the plugin's default master
  --set k=v             any other machine setting, repeatable (see "pattern")
  --rate <Hz>           sample rate (default 48000)
  --block <n>           processing block size (default 256)
  --quality eco|standard|high     oversampling of the metal voices (default standard)
  --seed <n>            unit seed / "serial number" (default 0)
  --variance <0..1>     component tolerance spread (default 0 = nominal parts)
  --channels 1|2        1 writes (L+R)/2 (default 2)
  --format 16|24|32f    sample format (default 24)
  --out, -o <file>      output WAV
  --quiet, -q           no progress on stderr

  Example:
    mw808-render voice --voice bd --hits normal,accent --spacing 1 --decay 0.8 -o bd.wav
)";

constexpr const char* kPatternHelp = R"(mw808-render pattern [source] --out <file.wav> [options]

  Source (one of):
    --bank <n> --pattern <i>   factory bank n (default 0) and pattern i, given
                               as 0..63 or A1..D16 (default 0 = A1)
    --text <file>              a pattern in the text format (help pattern-format)

  Timing:
    --measures <n>        measures (passes of the pattern) to render (default 2)
    --tail <s>            extra seconds after the last measure (default 1.0)
    --tempo <bpm>         default: the text file's tempo, else the factory
                          bank's suggested tempo, else 120
    --variation A|AB|B    basic variation switch (default A)
    --swing-scale <0..1>  global multiplier on the pattern's shuffle (default 1)
    --flam-ms <ms>        flam spacing (default 24)
    --auto-fill <n>       fill every n measures: 0 (manual, default) 2 4 8 12 16
    --chain               follow the bank's chain (factory banks)

  Sound:
    --kit <n>|stock       factory kit n (default 0), or "stock": every voice at
                          its stock settings with the default mixer and master,
                          independent of the factory library
    --accent <0..1>       ACCENT knob (default: the kit's)
    --set <k>=<v>         override one setting, repeatable:
                            <voice>.tune|decay|tone|shape|level|pan|drive
                            master.volume (dB) | master.analog 0|1 |
                            master.drive | master.comp | master.limiter 0|1
                          e.g. --set bd.decay=0.9 --set master.limiter=0
    --rate --block --quality --seed --variance   as for "voice" (block default 512)

  Output:
    --out, -o <file>      main mix WAV (the individual outs disabled, so every
                          voice is in the main mix)
    --stems <dir>         also write one stereo stem per voice, <dir>/<id>.wav
                          (bd.wav, sd.wav, ...), taken from the individual outs:
                          pre-master, like the hardware's separate outputs.
                          The directory must exist.
    --channels 1|2, --format 16|24|32f, --quiet   as for "voice"
    --print-text          print the selected pattern in the text format to
                          stdout (no render unless --out is also given)

  The machine runs in host-sync mode against a transport that starts rolling
  at PPQ 0 on sample 0, so step 1 of measure 1 sounds at sample 0.

  Example:
    mw808-render pattern --text beat.txt --measures 4 --tempo 128 -o beat.wav --stems stems/
)";

constexpr const char* kMidiHelp = R"(mw808-render midi [source] --out <file.mid> [options]

  Writes one measure of one variation as a format-0 standard MIDI file
  (channel 10, 480 PPQ; flams, sub-steps and shuffle as real note positions,
  weak/normal/accent velocities 48/96/127, notes from the mw808 drum map).

  Source: --bank/--pattern or --text, as for "pattern".
  --variation A|B       which variation to export (default A)
  --tempo <bpm>         tempo meta event and flam timing base (default as for "pattern")
  --flam-ms <ms>        flam spacing (default 24)
  --swing-scale <0..1>  shuffle multiplier (default 1)
  --one-step-notes      notes last one step (default: a 1/16 note)
  --no-tempo            omit the tempo meta event
  --no-embed            omit the exact-pattern meta event mw808 uses on import
  --accent-note <n>     also write the accent row as notes on key n
  --out, -o <file>      output .mid
)";

constexpr const char* kBenchHelp = R"(mw808-render bench [options]

  Realtime performance of a full machine on a dense worst-case pattern: all 16
  voices retriggered on every step with x2..x4 sub-steps and flams so each one
  rings continuously, accents, shuffle, per-voice drive, and the whole master
  section on. For every rate x quality x block size it reports the average cost
  (ns per output sample, CPU % of one core) and the slowest single block
  against that block's realtime deadline, then the cost of each voice alone.
  Build with the linux-release preset for meaningful numbers.

  --rates <list>        default 44100,48000,96000
  --qualities <list>    default eco,standard,high
  --blocks <list>       default 32,128,1024
  --seconds <s>         timed audio per run (default 4)
  --repeats <n>         independent runs per configuration (default 3)
  --tempo <bpm>         default 160
  --gate <pct>          fail (exit 1) when the worst block of any 48 kHz Standard
                        configuration exceeds this % of its deadline (default 25)
  --no-gate             report only
  --no-per-voice        skip the per-voice table

  "worst" is the smallest per-run maximum over the repeats (one-off scheduler
  preemptions are not the engine's fault); "worst-all" is the raw maximum.
)";

void printHelp(const std::string& topic)
{
    if (topic == "voice")
        std::fputs(kVoiceHelp, stdout);
    else if (topic == "pattern")
        std::fputs(kPatternHelp, stdout);
    else if (topic == "midi")
        std::fputs(kMidiHelp, stdout);
    else if (topic == "bench")
        std::fputs(kBenchHelp, stdout);
    else if (topic == "pattern-format")
        std::fputs(mw808::tools::kPatternTextHelp, stdout);
    else
    {
        std::fputs(kUsage, stdout);
        std::fputs("\n", stdout);
        std::fputs(kVoiceHelp, stdout);
        std::fputs("\n", stdout);
        std::fputs(kPatternHelp, stdout);
        std::fputs("\n", stdout);
        std::fputs(kMidiHelp, stdout);
        std::fputs("\n", stdout);
        std::fputs(kBenchHelp, stdout);
        std::fputs("\n", stdout);
        std::fputs(mw808::tools::kPatternTextHelp, stdout);
    }
}

bool wantsHelp(const std::vector<std::string>& args)
{
    for (const auto& a : args)
        if (a == "--help" || a == "-h")
            return true;
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    using namespace mw808::tools;
    if (argc < 2)
    {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    if (command == "--help" || command == "-h" || command == "help")
    {
        printHelp(args.empty() ? std::string() : args[0]);
        return 0;
    }
    if (command == "--version")
    {
        std::printf("mw808-render %s (%s build, g++ %s)\n", MW808_VERSION, MW808_BUILD_TYPE,
                    __VERSION__);
        return 0;
    }
    if (wantsHelp(args))
    {
        printHelp(command);
        return 0;
    }

    try
    {
        if (command == "voice")
            return cmdVoice(args);
        if (command == "pattern")
            return cmdPattern(args);
        if (command == "midi")
            return cmdMidi(args);
        if (command == "bench")
            return cmdBench(args);
        throw UsageError("unknown command '" + command + "'");
    }
    catch (const UsageError& e)
    {
        std::fprintf(stderr, "mw808-render: %s\n(see: mw808-render help %s)\n", e.what(),
                     command == "voice" || command == "pattern" || command == "midi"
                             || command == "bench"
                         ? command.c_str()
                         : "");
        return 2;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "mw808-render: error: %s\n", e.what());
        return 1;
    }
}

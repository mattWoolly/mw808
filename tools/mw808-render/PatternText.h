// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// A small human-writable text format for one mw808 pattern (both variations),
// used by mw808-render and the golden-render cases. Full description in
// kPatternTextHelp below (also printed by `mw808-render help pattern-format`).

#pragma once

#include <string>

#include "mw808/seq/Pattern.h"

namespace mw808::tools {

struct PatternText
{
    seq::Pattern pattern{};
    double tempo = 0.0; // from a `tempo` header; 0 when absent
    std::string name;   // from a `name` header
};

// Parses `text`. On failure returns false and sets `error` to "line N: ...".
bool parsePatternText(const std::string& text, PatternText& out, std::string& error);
bool loadPatternText(const std::string& path, PatternText& out, std::string& error);

// Text of a scale as the format writes it: "8t" "16t" "16" "32" "8".
const char* scaleText(seq::Scale scale);

// Writes a pattern in the same format (round-trips through parsePatternText).
std::string formatPatternText(const seq::Pattern& pattern, const std::string& name, double tempo);

inline constexpr const char* kPatternTextHelp = R"(PATTERN TEXT FORMAT

  One pattern per file, UTF-8/ASCII, one statement per line. '#' starts a
  comment (to the end of the line). Blank lines are ignored. Keywords and
  track names are case-insensitive; step characters are not.

  Headers (any order, each optional):
    name <text>            display name (informational)
    length <1..64>         pattern length in steps            (default 16)
    scale <s>              step length: 8t | 16t | 16 | 32 | 8 (default 16)
                           8t = eighth triplet (3/beat), 16t = sixteenth
                           triplet (6/beat), 16 = sixteenth, 32 = thirty-
                           second, 8 = eighth. Also accepted: 16th, 32nd, 8th.
    shuffle <0..100>       delay of odd steps, % of the maximum (default 0)
    tempo <bpm>            suggested tempo; --tempo on the command line wins
    tracklength <TR> <n>   polymeter: track TR loops every n steps (1..64)
    variation A|B          following track lines fill variation A or B
                           (default A)

  Track lines:  <TR> <steps>
    TR is a voice code  BD SD LT MT HT LC MC HC RS CL CP MA CB CY OH CH
    or AC for the global accent row.

    <steps> is one token per step, starting at step 1. Spaces and '|' are
    ignored, so  x---|x---|x---|x---  is fine. Steps not written are off; at
    most 64 steps per line.

      -  or  .     off
      o            weak
      x            normal
      X            accent
    On the AC row any of o x X switches the global accent on for that step.

    A step character may be followed by modifiers in parentheses:
      (f)          flam: grace hit, then the main hit one flam time later
      (2) (3) (4)  sub-steps: that many evenly spaced hits inside the step
      (pN)         probability code N = 0..7, chance (8-N)/8 (0 = always)
    Modifiers combine, e.g.  X(f)  x(3)  x(f2p4)

  Example:
    name     Basic beat
    length   16
    scale    16
    shuffle  20
    tempo    120
    BD  x---x---x---x---
    SD  ----X-------X(f)---
    CH  x-x-x-x-x-x-x(3)-x-
    OH  --------------x-
    AC  x-------x-------
)";

} // namespace mw808::tools

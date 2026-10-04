// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The step-editing rules shared by the sequencer's step keys and the GRID
// view (plugin.md §7.3): a click cycles Off -> Normal -> Accent -> Off
// (shift-click toggles Weak); in FLAM / SUB / PROB mode a click edits that
// property of an active step; a drag paints the first step's result onto the
// steps it passes. Pure functions on seq::Step.

#pragma once

#include "UiCommon.h"
#include "mw808/seq/Pattern.h"

namespace mw808::ui::stepedit {

inline bool isAccentTrack(int track) noexcept { return track >= seq::kAccentTrack; }

// The step after a click in `mode` on `track`.
inline seq::Step clicked(seq::Step s, StepMode mode, int track, bool shift) noexcept {
    using seq::StepLevel;
    if (isAccentTrack(track)) {
        s.setLevel(s.on() ? StepLevel::Off : StepLevel::Accent);
        s.setFlam(false);
        s.setSubSteps(1);
        s.setProbabilityCode(0);
        return s;
    }
    switch (mode) {
        case StepMode::Level:
            if (shift) {
                s.setLevel(s.level() == StepLevel::Weak ? StepLevel::Off : StepLevel::Weak);
            } else {
                switch (s.level()) {
                    case StepLevel::Off: s.setLevel(StepLevel::Normal); break;
                    case StepLevel::Weak: s.setLevel(StepLevel::Normal); break;
                    case StepLevel::Normal: s.setLevel(StepLevel::Accent); break;
                    case StepLevel::Accent: s.setLevel(StepLevel::Off); break;
                }
            }
            if (!s.on()) {  // a cleared step forgets its modifiers
                s.setFlam(false);
                s.setSubSteps(1);
                s.setProbabilityCode(0);
            }
            return s;
        case StepMode::Flam:
            if (!s.on()) s.setLevel(StepLevel::Normal);
            else s.setFlam(!s.flam());
            return s;
        case StepMode::Sub: {
            if (!s.on()) { s.setLevel(StepLevel::Normal); return s; }
            int n = s.subSteps() + (shift ? -1 : 1);
            if (n > 4) n = 1;
            if (n < 1) n = 4;
            s.setSubSteps(n);
            return s;
        }
        case StepMode::Prob: {
            if (!s.on()) { s.setLevel(StepLevel::Normal); return s; }
            int c = s.probabilityCode() + (shift ? -1 : 1);
            if (c > 7) c = 0;
            if (c < 0) c = 7;
            s.setProbabilityCode(c);
            return s;
        }
    }
    return s;
}

// Paint the result of a click (`brush`) onto another step: in LEVEL mode the
// level is copied (clearing modifiers when off); in the other modes the
// property is copied onto active steps only.
inline seq::Step painted(seq::Step target, seq::Step brush, StepMode mode, int track) noexcept {
    if (isAccentTrack(track) || mode == StepMode::Level) {
        target.setLevel(brush.level());
        if (!target.on()) {
            target.setFlam(false);
            target.setSubSteps(1);
            target.setProbabilityCode(0);
        }
        return target;
    }
    if (!target.on()) return target;
    if (mode == StepMode::Flam) target.setFlam(brush.flam());
    else if (mode == StepMode::Sub) target.setSubSteps(brush.subSteps());
    else if (mode == StepMode::Prob) target.setProbabilityCode(brush.probabilityCode());
    return target;
}

// Short badge for a step in a mode ("F", "x3", "75%"); empty when stock.
inline juce::String badge(seq::Step s, StepMode mode, int track) {
    if (!s.on() || isAccentTrack(track)) return {};
    if (mode == StepMode::Flam) return s.flam() ? "F" : juce::String();
    if (mode == StepMode::Sub) return s.subSteps() > 1 ? "x" + juce::String(s.subSteps()) : juce::String();
    if (mode == StepMode::Prob)
        return s.probabilityCode() > 0 ? juce::String(juce::roundToInt(s.probability() * 100.0f)) + "%" : juce::String();
    juce::String b;
    if (s.flam()) b << "F";
    if (s.subSteps() > 1) b << (b.isEmpty() ? "" : " ") << "x" << s.subSteps();
    if (s.probabilityCode() > 0) b << (b.isEmpty() ? "" : " ") << "?";
    return b;
}

inline const char* scaleName(seq::Scale s) noexcept {
    if (s == seq::Scale::EighthTriplet) return "1/8T";
    if (s == seq::Scale::SixteenthTriplet) return "1/16T";
    if (s == seq::Scale::Sixteenth) return "1/16";
    if (s == seq::Scale::ThirtySecond) return "1/32";
    if (s == seq::Scale::Eighth) return "1/8";
    return "?";
}

// Default randomiser densities per track (voice order, then accent).
inline std::array<float, seq::kNumTracks> defaultDensities() noexcept {
    return {{0.45f, 0.35f, 0.12f, 0.12f, 0.12f, 0.10f, 0.10f, 0.10f, 0.15f, 0.12f, 0.20f, 0.40f, 0.10f, 0.08f, 0.30f,
             0.60f, 0.20f}};
}

// A level-cycle popup for a step (right-click), shared by keys and grid.
void showStepMenu(Context& ctx, int patternIndex, int variation, int track, int step, juce::Component& target);

}  // namespace mw808::ui::stepedit

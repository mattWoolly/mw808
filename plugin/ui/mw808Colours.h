// SPDX-License-Identifier: AGPL-3.0-or-later
// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
//
// The mw808 palette (docs/design/plugin.md §7): a dark charcoal panel, cream
// legends, and the classic warm run — red, orange, yellow, off-white — on the
// step keys and as the voice accents. Every colour the editor draws comes from
// this table so the look can be tuned in one place.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include "mw808/Voices.h"

namespace mw808Colours {

// --- panel -------------------------------------------------------------------
inline const juce::Colour panel          {0xff232326};  // the chassis
inline const juce::Colour panelDeep      {0xff18181b};  // header / sequencer strip ground
inline const juce::Colour plate          {0xff2b2b2f};  // voice strips, mixer channels
inline const juce::Colour plateRaised    {0xff34343a};  // control plates
inline const juce::Colour plateEdge      {0xff46464d};  // 1 px plate outlines
inline const juce::Colour inset          {0xff121214};  // readouts, LED wells, meters
inline const juce::Colour insetAlt       {0xff1a1a1e};  // alternate beat groups in the GRID
inline const juce::Colour hairline       {0xff3a3a40};  // separators

// --- legends -----------------------------------------------------------------
inline const juce::Colour cream          {0xffefe6d2};  // primary legend
inline const juce::Colour creamDim       {0xffa39c8e};  // secondary legend
inline const juce::Colour creamFaint     {0xff6d685f};  // disabled / hairline legend
inline const juce::Colour ink            {0xff15151a};  // text on light keys

// --- the warm run ------------------------------------------------------------
inline const juce::Colour red            {0xffd8432b};
inline const juce::Colour orange         {0xffea8a2c};
inline const juce::Colour yellow         {0xffe9c43a};
inline const juce::Colour offWhite       {0xffece5d3};

// --- controls ----------------------------------------------------------------
inline const juce::Colour knobBody       {0xff3a3a41};
inline const juce::Colour knobBodyDark   {0xff2a2a2f};
inline const juce::Colour knobCapHw      {0xff4d4d55};  // hardware-original knobs: skirted cap
inline const juce::Colour knobCapExt     {0xff34343a};  // extended knobs: flat cap
inline const juce::Colour knobTrack      {0xff1c1c1f};  // value-arc track
inline const juce::Colour arc            {0xffea8a2c};  // value arc (orange)
inline const juce::Colour arcExt         {0xffb98a52};  // value arc of extended knobs
inline const juce::Colour pointerHw      {0xffefe6d2};
inline const juce::Colour pointerExt     {0xffea8a2c};
inline const juce::Colour buttonOff      {0xff2e2e33};
inline const juce::Colour buttonOn       {0xff5a4a3a};
inline const juce::Colour buttonText     {0xffd9d1bf};
inline const juce::Colour focus          {0xffefe6d2};
inline const juce::Colour hover          {0x22ffffff};

// --- lamps -------------------------------------------------------------------
inline const juce::Colour ledOff         {0xff3d1a14};
inline const juce::Colour ledOn          {0xffff6a44};
inline const juce::Colour ledRun         {0xfffff1c8};  // running-step highlight
inline const juce::Colour ledGreenOff    {0xff1b3220};
inline const juce::Colour ledGreenOn     {0xff6fe08a};
inline const juce::Colour meterLow       {0xff8ac47a};
inline const juce::Colour meterMid       {0xffe9c43a};
inline const juce::Colour meterHot       {0xffd8432b};
inline const juce::Colour record         {0xffd8432b};

// --- the warm run by index ---------------------------------------------------
// Step keys: 0-3 red, 4-7 orange, 8-11 yellow, 12-15 off-white.
inline juce::Colour stepGroup(int stepInPage) noexcept {
    const int g = ((stepInPage % 16) + 16) % 16 / 4;
    return g == 0 ? red : g == 1 ? orange : g == 2 ? yellow : offWhite;
}

// Voice accents follow the same run across the 16 strips.
inline juce::Colour voiceAccent(int voice) noexcept { return stepGroup(voice); }

inline juce::Colour trackAccent(int track) noexcept {
    return track >= mw808::kNumVoices ? cream : voiceAccent(track);
}

// Text that reads on a given key colour.
inline juce::Colour textOn(juce::Colour background) noexcept {
    return background.getPerceivedBrightness() > 0.6f ? ink : cream;
}

}  // namespace mw808Colours

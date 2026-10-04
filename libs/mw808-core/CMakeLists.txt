# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# The JUCE-free core, in three targets so each layer builds and tests alone:
#   mw808_dsp   INTERFACE  dsp primitives + the 16 voices (header-only)
#   mw808_seq   STATIC     pattern model, sequencer, MIDI file I/O
#   mw808_core  STATIC     Engine + Machine (links the two above)
# Zero dependencies beyond the C++20 standard library.

add_library(mw808_dsp INTERFACE)
target_include_directories(mw808_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(mw808_dsp INTERFACE cxx_std_20)

set(MW808_CORE_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
# FP discipline: deterministic renders. No fast-math, no FMA contraction.
set(MW808_CORE_FP -ffp-contract=off)
target_compile_options(mw808_dsp INTERFACE ${MW808_CORE_FP})

file(GLOB MW808_SEQ_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/seq/*.cpp")
if(MW808_SEQ_SOURCES)
    add_library(mw808_seq STATIC ${MW808_SEQ_SOURCES})
    target_include_directories(mw808_seq PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(mw808_seq PUBLIC cxx_std_20)
    target_compile_options(mw808_seq PRIVATE ${MW808_CORE_WARNINGS} ${MW808_CORE_FP})
    # PIC: these static libs are linked into the plugin shared objects.
    set_target_properties(mw808_seq PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

file(GLOB MW808_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(MW808_CORE_SOURCES AND TARGET mw808_seq)
    add_library(mw808_core STATIC ${MW808_CORE_SOURCES})
    target_link_libraries(mw808_core PUBLIC mw808_dsp mw808_seq)
    target_compile_options(mw808_core PRIVATE ${MW808_CORE_WARNINGS} ${MW808_CORE_FP})
    set_target_properties(mw808_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

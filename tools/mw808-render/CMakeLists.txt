# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# mw808-render      JUCE-free CLI: voice / pattern renders, MIDI export, benchmark.
# mw808-wavcompare  golden-render comparer with diagnostics (tests/golden).
# Both link the core only (the comparer not even that).

set(MW808_TOOL_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)

if(CMAKE_BUILD_TYPE)
    set(_mw808_build_type "${CMAKE_BUILD_TYPE}")
else()
    set(_mw808_build_type "multi-config")
endif()

add_library(mw808_tool_wav STATIC Wav.cpp)
target_include_directories(mw808_tool_wav PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(mw808_tool_wav PUBLIC cxx_std_20)
target_compile_options(mw808_tool_wav PRIVATE ${MW808_TOOL_WARNINGS})

if(TARGET mw808_core)
    add_executable(mw808-render
        main.cpp
        Common.cpp
        Render.cpp
        Bench.cpp
        PatternText.cpp)
    target_link_libraries(mw808-render PRIVATE mw808_core mw808_tool_wav)
    target_compile_options(mw808-render PRIVATE ${MW808_TOOL_WARNINGS} -ffp-contract=off)
    target_compile_definitions(mw808-render PRIVATE
        MW808_VERSION="${PROJECT_VERSION}"
        MW808_BUILD_TYPE="${_mw808_build_type}")
endif()

add_executable(mw808-wavcompare WavCompare.cpp)
target_link_libraries(mw808-wavcompare PRIVATE mw808_tool_wav)
target_compile_options(mw808-wavcompare PRIVATE ${MW808_TOOL_WARNINGS})

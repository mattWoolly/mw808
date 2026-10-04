# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# mw808_plugin_tests: a JUCE-linked Catch2 console executable that compiles the
# plugin shell sources directly (no plugin-format wrapper) and drives the
# processor the way a host would. Registered with CTest test by test.

juce_add_console_app(mw808_plugin_tests
    PRODUCT_NAME "mw808_plugin_tests"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

file(GLOB MW808_PLUGIN_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB MW808_SHELL_SOURCES_FOR_TESTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/plugin/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/plugin/src/state/*.cpp")
# The editor sources too, so the test binary still links once the processor
# creates the real editor (unless MW808_WITH_UI_SOURCES is OFF, see plugin/CMakeLists.txt).
set(MW808_UI_SOURCES_FOR_TESTS "")
if(MW808_WITH_UI_SOURCES)
    file(GLOB_RECURSE MW808_UI_SOURCES_FOR_TESTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/plugin/ui/*.cpp")
endif()

target_sources(mw808_plugin_tests PRIVATE
    ${MW808_PLUGIN_TEST_SOURCES}
    ${MW808_SHELL_SOURCES_FOR_TESTS}
    ${MW808_UI_SOURCES_FOR_TESTS})

target_include_directories(mw808_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/plugin/src"
    "${CMAKE_SOURCE_DIR}/plugin/ui"
    "${CMAKE_SOURCE_DIR}/plugin")

# Same treatment of the core headers as the plugin target (see plugin/CMakeLists.txt).
target_include_directories(mw808_plugin_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/libs/mw808-core/include")

target_compile_definitions(mw808_plugin_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # One test pumps the message loop to see the real 30 Hz timer do its work.
    JUCE_MODAL_LOOPS_PERMITTED=1
    MW808_PRODUCT_NAME="mw808"
    MW808_VERSION_STRING="${PROJECT_VERSION}"
    MW808_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch"
    $<$<NOT:$<BOOL:${MW808_WITH_UI_SOURCES}>>:MW808_USE_PLACEHOLDER_EDITOR=1>)

target_link_libraries(mw808_plugin_tests PRIVATE
    mw808_core
    juce::juce_audio_utils
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# The tests compare floats exactly on purpose (restored values, exact silence);
# the shell sources compiled alongside keep the full warning set.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${MW808_PLUGIN_TEST_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
endif()

# The allocation spy (AllocSpy.cpp) replaces operator new/delete and, because
# JUCE's HeapBlock calls malloc directly, also wraps the C allocator at link time.
if(UNIX AND NOT APPLE)
    target_link_options(mw808_plugin_tests PRIVATE
        "LINKER:--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=posix_memalign,--wrap=aligned_alloc,--wrap=memalign")
    target_compile_definitions(mw808_plugin_tests PRIVATE MW808_TEST_WRAPS_MALLOC=1)
endif()

catch_discover_tests(mw808_plugin_tests TEST_PREFIX "plugin: ")

# The cross-thread stress test at full length (label `stress`; the suite above
# runs it for 2 s). Meant for the TSan and ASan lanes as much as for Release.
add_test(NAME "stress: 30 s of audio, host and message threads at once"
    COMMAND mw808_plugin_tests "[stress]")
set_tests_properties("stress: 30 s of audio, host and message threads at once" PROPERTIES
    LABELS stress
    TIMEOUT 900
    ENVIRONMENT "MW808_STRESS_SECONDS=30")

# ---------------------------------------------------------------------------
# Host smoke (label `host-smoke`): the Standalone starts, runs for a few
# seconds and quits by itself with exit code 0. The script exits 77 when the
# Standalone is not built or there is no display and no xvfb-run, which CTest
# reports as skipped. (The format validators live in tools/validators.)
if(UNIX AND NOT APPLE)
    add_test(NAME "host-smoke: Standalone starts and quits cleanly"
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run_standalone_smoke.sh")
    set_tests_properties("host-smoke: Standalone starts and quits cleanly" PROPERTIES
        LABELS host-smoke
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        ENVIRONMENT "MW808_BUILD_DIR=${CMAKE_BINARY_DIR}")
endif()

# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# The JUCE plugin shell (docs/design/plugin.md): one juce_add_plugin target
# exporting VST3 / LV2 / Standalone, plus CLAP through clap-juce-extensions.
# JUCE and the CLAP shim are fetched by the top-level CMakeLists.

# The product name lives here and nowhere else: the plugin name, the bundle and
# CLAP ids, the LV2 URI and the preset directory all derive from it.
set(MW_PRODUCT_NAME "mw808")
string(TOLOWER "${MW_PRODUCT_NAME}" MW_PRODUCT_ID)
set(MW_PLUGIN_CODE "mw80")

# One line to drop a format (JUCE's LV2 export is the least mature of the three).
set(MW808_PLUGIN_FORMATS VST3 LV2 Standalone)

juce_add_plugin(mw808
    PRODUCT_NAME                "${MW_PRODUCT_NAME}"
    VERSION                     "${PROJECT_VERSION}"
    COMPANY_NAME                "mwAudio"
    BUNDLE_ID                   "com.mwaudio.${MW_PRODUCT_ID}"
    PLUGIN_MANUFACTURER_CODE    mwAu
    PLUGIN_CODE                 ${MW_PLUGIN_CODE}
    DESCRIPTION                 "Classic analog drum machine with step sequencer"
    FORMATS                     ${MW808_PLUGIN_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    # The editor has text-entry fields (value readouts, preset names).
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Instrument Drum
    LV2URI                      "https://github.com/mattWoolly/${MW_PRODUCT_NAME}"
    NEEDS_CURL                  FALSE
    NEEDS_WEB_BROWSER           FALSE
    # Installing into ~/.vst3 etc. is a separate, explicit step on Linux.
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

# Shell sources and (when they exist) the editor sources under plugin/ui are
# globbed, so adding a file needs no CMake edit.
#
# MW808_WITH_UI_SOURCES=OFF leaves plugin/ui out of the plugin and the plugin
# tests and builds them with the placeholder editor (it defines
# MW808_USE_PLACEHOLDER_EDITOR=1). For working on the shell while the editor
# is mid-construction; release builds keep the default ON.
option(MW808_WITH_UI_SOURCES "Compile the editor sources under plugin/ui" ON)
file(GLOB MW808_SHELL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/state/*.cpp")
set(MW808_UI_SOURCES "")
if(MW808_WITH_UI_SOURCES)
    file(GLOB_RECURSE MW808_UI_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp")
endif()

target_sources(mw808 PRIVATE ${MW808_SHELL_SOURCES} ${MW808_UI_SOURCES})

target_include_directories(mw808 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}")
# The core is built and tested under its own warning set. JUCE's recommended
# flags add -Wfloat-equal, -Wswitch-enum and -Wsign-conversion, which flag
# deliberate code in the core's headers (exact "did this parameter change"
# compares); seen from the shell those headers are a dependency, not shell code.
target_include_directories(mw808 SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/libs/mw808-core/include")

target_compile_definitions(mw808
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    PRIVATE
        MW808_PRODUCT_NAME="${MW_PRODUCT_NAME}"
        MW808_VERSION_STRING="${PROJECT_VERSION}"
        $<$<NOT:$<BOOL:${MW808_WITH_UI_SOURCES}>>:MW808_USE_PLACEHOLDER_EDITOR=1>)

target_link_libraries(mw808
    PRIVATE
        mw808_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# JUCE's LTO flag is a bare -flto, which links its partitions one at a time
# (and says so in a warning). Let the linker use the available cores.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(mw808 PUBLIC $<$<CONFIG:Release>:-flto=auto>)
endif()

# Do not set CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES here. With it the CLAP
# wrapper splits a host block at parameter events into several processBlock
# calls that all report the transport position of the block START, so the
# sequencer in Host Sync would see the play head jump back at every split and
# re-anchor each time. Unsplit blocks (the default) are what it expects.
clap_juce_extensions_plugin(
    TARGET          mw808
    CLAP_ID         "com.mwaudio.${MW_PRODUCT_ID}"
    CLAP_FEATURES   instrument drum-machine stereo)

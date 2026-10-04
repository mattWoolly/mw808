# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# mw808_ui_tool: a JUCE console app that compiles the plugin shell and editor
# sources directly and drives the real mw808Editor headlessly (software
# renderer, under xvfb-run):
#   screenshots <dir>   PNGs of PANEL / GRID / MIXER at 0.6x, 1.0x, 2.0x
#   stress <n>          open / close / resize the editor n times while an audio
#                       thread runs processBlock (run under ASan too)
#   idle <seconds>      CPU time of an open, idle editor with the 30 Hz timer running
#   interact            scripted mouse interaction on every control kind
#   wiring              every plugin.md §7 control path, driven by mouse / menu keys,
#                       checked at the processor (params, PatternModel, MidiLearn, presets)
#   lifetime <n>        close the editor with popup menus, alert windows, a knob
#                       drag, a step paint or a MIDI drag-out in flight (audio running)
#   perf <seconds>      frames/s and dirty area per frame, stopped and playing
# Each is registered as a CTest "ui: ..." test when xvfb-run (or a display) is available.
#
# Integrator: tests/CMakeLists.txt needs `add_subdirectory(ui)` next to the
# plugin tests (after the plugin target exists).

if(NOT TARGET mw808 OR NOT TARGET mw808_core)
    message(STATUS "mw808_ui_tool: plugin target not built, skipped")
    return()
endif()

juce_add_console_app(mw808_ui_tool
    PRODUCT_NAME "mw808_ui_tool"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

file(GLOB MW808_UI_TOOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB MW808_SHELL_SOURCES_FOR_UI CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/plugin/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/plugin/src/state/*.cpp")
file(GLOB_RECURSE MW808_UI_SOURCES_FOR_UI CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/plugin/ui/*.cpp")

target_sources(mw808_ui_tool PRIVATE
    ${MW808_UI_TOOL_SOURCES}
    ${MW808_SHELL_SOURCES_FOR_UI}
    ${MW808_UI_SOURCES_FOR_UI})

target_include_directories(mw808_ui_tool PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_SOURCE_DIR}/plugin/src"
    "${CMAKE_SOURCE_DIR}/plugin/ui"
    "${CMAKE_SOURCE_DIR}/plugin")
target_include_directories(mw808_ui_tool SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/libs/mw808-core/include")

target_compile_definitions(mw808_ui_tool PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # The tool pumps the message loop itself (runDispatchLoopUntil).
    JUCE_MODAL_LOOPS_PERMITTED=1
    MW808_PRODUCT_NAME="mw808"
    MW808_VERSION_STRING="${PROJECT_VERSION}")

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(mw808_ui_tool PRIVATE X11::X11)
endif()

target_link_libraries(mw808_ui_tool PRIVATE
    mw808_core
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Headless runs: prefer xvfb-run; fall back to a live DISPLAY.
find_program(MW808_XVFB_RUN xvfb-run)
if(MW808_XVFB_RUN)
    set(_mw808_ui_launcher "${MW808_XVFB_RUN}" -a)
elseif(DEFINED ENV{DISPLAY})
    set(_mw808_ui_launcher "")
else()
    message(STATUS "mw808_ui_tool: no xvfb-run and no DISPLAY, UI tests not registered")
    return()
endif()

set(_mw808_screens_dir "${CMAKE_BINARY_DIR}/screens")
add_test(NAME "ui: screenshots of PANEL / GRID / MIXER at 0.6x 1.0x 2.0x"
    COMMAND ${_mw808_ui_launcher} $<TARGET_FILE:mw808_ui_tool> screenshots "${_mw808_screens_dir}")
add_test(NAME "ui: open / close / resize the editor 200 times with audio running"
    COMMAND ${_mw808_ui_launcher} $<TARGET_FILE:mw808_ui_tool> stress 200)
add_test(NAME "ui: idle editor CPU with the 30 Hz timer"
    COMMAND ${_mw808_ui_launcher} $<TARGET_FILE:mw808_ui_tool> idle 5)
add_test(NAME "ui: scripted interaction on every control kind"
    COMMAND ${_mw808_ui_launcher} $<TARGET_FILE:mw808_ui_tool> interact)
add_test(NAME "ui: every editor control path reaches the processor"
    COMMAND ${_mw808_ui_launcher} $<TARGET_FILE:mw808_ui_tool> wiring)
add_test(NAME "ui: editor closed with menus, dialogs and drags in flight"
    COMMAND ${_mw808_ui_launcher} $<TARGET_FILE:mw808_ui_tool> lifetime 90)
add_test(NAME "ui: repaint rate and dirty area, stopped and playing"
    COMMAND ${_mw808_ui_launcher} $<TARGET_FILE:mw808_ui_tool> perf 3)
set_tests_properties(
    "ui: screenshots of PANEL / GRID / MIXER at 0.6x 1.0x 2.0x"
    "ui: open / close / resize the editor 200 times with audio running"
    "ui: idle editor CPU with the 30 Hz timer"
    "ui: scripted interaction on every control kind"
    "ui: every editor control path reaches the processor"
    "ui: editor closed with menus, dialogs and drags in flight"
    "ui: repaint rate and dirty area, stopped and playing"
    PROPERTIES LABELS ui TIMEOUT 600 RUN_SERIAL TRUE)

# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.

cmake_minimum_required(VERSION 3.25)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The release version lives in VERSION (one line, MAJOR.MINOR.PATCH) and nowhere else.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MW808_VERSION_FROM_FILE LIMIT_COUNT 1)
string(STRIP "${MW808_VERSION_FROM_FILE}" MW808_VERSION_FROM_FILE)
if(NOT MW808_VERSION_FROM_FILE MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must hold MAJOR.MINOR.PATCH, got '${MW808_VERSION_FROM_FILE}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

project(mw808 VERSION ${MW808_VERSION_FROM_FILE} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# FP discipline: no -ffast-math anywhere. The core is deterministic (same
# input -> bit-identical render on the same toolchain), which the golden
# renders in tests/ rely on.
# ---------------------------------------------------------------------------

option(MW808_BUILD_PLUGIN "Build the JUCE plugin (VST3/CLAP/LV2/Standalone)" ON)
option(MW808_BUILD_TESTS  "Build the Catch2 test suites"                    ON)
option(MW808_BUILD_TOOLS  "Build the CLI render/benchmark tools"            ON)

include(cmake/Deps.cmake)

# Enable testing BEFORE any add_subdirectory so catch_discover_tests registers.
enable_testing()

add_subdirectory(libs/mw808-core)     # mw808_core (static, JUCE-free)

if(MW808_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/mw808-render/CMakeLists.txt")
    add_subdirectory(tools/mw808-render)
endif()

if(MW808_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt")
    mw808_fetch_juce()
    mw808_fetch_clap_extensions()
    add_subdirectory(plugin)
endif()

if(MW808_BUILD_TESTS)
    mw808_fetch_catch2()
    include(Catch)
    add_subdirectory(tests)
    # Script-driven checks: licence headers, branding audit, format validators.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ProjectChecks.cmake")
        include(tools/ProjectChecks.cmake)
    endif()
endif()

# Copies the Release plugin bundles into ~/.vst3, ~/.clap and ~/.lv2. Never runs
# as part of a build; invoke explicitly: cmake --build <dir> --target install_user
if(MW808_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/install-user.sh")
    add_custom_target(install_user
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/install-user.sh" "${CMAKE_BINARY_DIR}"
        USES_TERMINAL
        VERBATIM)
endif()

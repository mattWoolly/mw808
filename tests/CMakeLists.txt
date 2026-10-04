# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# One Catch2 executable per tests/unit/<prefix>_<name>.cpp. The prefix picks the
# library under test: dsp_ -> mw808_dsp, seq_ -> mw808_seq, core_ -> mw808_core.

file(GLOB MW808_UNIT_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")

foreach(test_src IN LISTS MW808_UNIT_TESTS)
    get_filename_component(test_name "${test_src}" NAME_WE)
    string(REGEX MATCH "^[a-z]+" test_prefix "${test_name}")
    if(test_prefix STREQUAL "dsp")
        set(test_lib mw808_dsp)
    elseif(test_prefix STREQUAL "seq")
        set(test_lib mw808_seq)
    elseif(test_prefix STREQUAL "core")
        set(test_lib mw808_core)
    else()
        message(FATAL_ERROR "tests/unit/${test_name}.cpp: prefix must be dsp_, seq_ or core_")
    endif()
    if(NOT TARGET ${test_lib})
        continue()
    endif()

    set(test_target "mw808_test_${test_name}")
    add_executable(${test_target} "${test_src}")
    target_link_libraries(${test_target} PRIVATE ${test_lib} Catch2::Catch2WithMain)
    target_include_directories(${test_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    target_compile_definitions(${test_target} PRIVATE
        MW808_REF_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/.deps/ref-samples"
        MW808_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    catch_discover_tests(${test_target} TEST_PREFIX "${test_name}: ")
endforeach()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/golden/CMakeLists.txt")
    add_subdirectory(golden)
endif()

if(MW808_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt")
    add_subdirectory(plugin)
    if(TARGET mw808 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui/CMakeLists.txt")
        add_subdirectory(ui)
    endif()
endif()

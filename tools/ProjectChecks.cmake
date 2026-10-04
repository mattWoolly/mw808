# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# Script-driven CTests, included from the top-level CMakeLists.txt when tests
# are enabled:
#   license_headers          tools/check_license_headers.sh        (LABEL hygiene)
#   branding_audit           scripts/branding-audit.sh --binaries   (LABEL hygiene)
#   validators: ...          tools/validators/run_*.sh              (LABEL validators)
# Validators exit 77 when their tool or the plugin artefact is missing, which
# CTest reports as skipped (SKIP_RETURN_CODE 77). They receive this build
# directory through MW808_BUILD_DIR and as their first argument.

find_program(MW808_BASH bash)
if(NOT MW808_BASH)
    message(STATUS "bash not found: licence, branding and validator checks not registered")
    return()
endif()

set(_mw808_root "${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME license_headers
    COMMAND "${MW808_BASH}" "${_mw808_root}/tools/check_license_headers.sh" "${_mw808_root}")
set_tests_properties(license_headers PROPERTIES LABELS hygiene TIMEOUT 120)

add_test(NAME branding_audit
    COMMAND "${MW808_BASH}" "${_mw808_root}/scripts/branding-audit.sh" --binaries "${CMAKE_BINARY_DIR}" "${_mw808_root}")
set_tests_properties(branding_audit PROPERTIES LABELS hygiene TIMEOUT 300)

foreach(_v IN ITEMS
        "pluginval VST3|run_pluginval.sh|1800"
        "clap-validator CLAP|run_clap_validator.sh|1800"
        "LV2 bundle checks|run_lv2_checks.sh|300")
    string(REPLACE "|" ";" _v "${_v}")
    list(GET _v 0 _name)
    list(GET _v 1 _script)
    list(GET _v 2 _timeout)
    add_test(NAME "validators: ${_name}"
        COMMAND "${MW808_BASH}" "${_mw808_root}/tools/validators/${_script}" "${CMAKE_BINARY_DIR}")
    set_tests_properties("validators: ${_name}" PROPERTIES
        LABELS validators
        SKIP_RETURN_CODE 77
        TIMEOUT ${_timeout}
        RUN_SERIAL TRUE
        ENVIRONMENT "MW808_BUILD_DIR=${CMAKE_BINARY_DIR}")
endforeach()

# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# One golden case, run as `cmake -P` by CTest (see CMakeLists.txt here).
# Renders the case into SCRATCH_DIR and compares it with the blessed render.
#
# -D RENDER_BIN   mw808-render
# -D COMPARE_BIN  mw808-wavcompare
# -D CASE_ID      case id
# -D REFERENCE    id whose blessed file to compare against, or "-" (= CASE_ID)
# -D TOL          mw808-wavcompare --tol value, or "-" for its default (1 LSB)
# -D ARGS         mw808-render arguments separated by '|'
# -D BLESSED_DIR  tests/golden/blessed
# -D SCRATCH_DIR  where candidate renders go

foreach(_v RENDER_BIN COMPARE_BIN CASE_ID REFERENCE TOL ARGS BLESSED_DIR SCRATCH_DIR)
    if(NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
        message(FATAL_ERROR "RunGoldenCase: -D ${_v}=... is missing")
    endif()
endforeach()

set(_ref "${CASE_ID}")
if(NOT REFERENCE STREQUAL "-")
    set(_ref "${REFERENCE}")
endif()
set(_blessed "${BLESSED_DIR}/${_ref}.wav")
set(_candidate "${SCRATCH_DIR}/${CASE_ID}.wav")
file(MAKE_DIRECTORY "${SCRATCH_DIR}")

if(NOT EXISTS "${_blessed}")
    message(FATAL_ERROR "golden ${CASE_ID}: no blessed render ${_blessed}\n"
        "  bless it: BLESS_REASON=\"why\" tools/bless_goldens.sh <build-dir>")
endif()

string(REPLACE "|" ";" _args "${ARGS}")
execute_process(
    COMMAND "${RENDER_BIN}" ${_args} --quiet --out "${_candidate}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "golden ${CASE_ID}: render failed (rc ${_rc})\n${_out}${_err}")
endif()

set(_tol_args "")
if(NOT TOL STREQUAL "-")
    set(_tol_args --tol "${TOL}")
endif()
execute_process(
    COMMAND "${COMPARE_BIN}" --candidate "${_candidate}" --blessed "${_blessed}" ${_tol_args}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
message("${_out}${_err}")
if(NOT _rc EQUAL 0)
    string(REPLACE "|" " " _args_print "${ARGS}")
    message(FATAL_ERROR "golden ${CASE_ID}: does not match ${_blessed} (rc ${_rc})\n"
        "  candidate kept at ${_candidate}\n"
        "  render it again: ${RENDER_BIN} ${_args_print} --out <file.wav>")
endif()

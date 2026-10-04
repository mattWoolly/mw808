#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# Shared helpers for the format-validator scripts in this directory. Sourced,
# not run. Every validator follows the same CTest contract:
#   exit 0   the artefact passed
#   exit 1   the artefact failed, or a downloaded tool failed its checksum
#   exit 77  skip: the tool cannot be fetched, or no artefact is built
#
# Build directory: $MW808_VALIDATE_BUILD_DIR when set (lets `ctest -L validators`
# in one tree check the plugin of another), else the first argument, else
# $MW808_BUILD_DIR, else build/linux-release.
# Tool cache: <repo>/build/validator-tools (override with $MW808_VALIDATOR_CACHE).

SKIP_EXIT=77

_mw808_lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MW808_REPO_ROOT="$(cd "${_mw808_lib_dir}/../.." && pwd)"
MW808_TOOLS_CACHE="${MW808_VALIDATOR_CACHE:-${MW808_REPO_ROOT}/build/validator-tools}"

# Plugin identity (docs/design/plugin.md §1).
MW808_PRODUCT="mw808"
MW808_LV2_URI="https://github.com/mattWoolly/mw808"

log()  { printf '%s: %s\n' "${MW808_TOOL_NAME:-validator}" "$*"; }
warn() { printf '%s: %s\n' "${MW808_TOOL_NAME:-validator}" "$*" >&2; }

skip() {
    warn "$* - SKIPPING (exit ${SKIP_EXIT})"
    exit "${SKIP_EXIT}"
}

# resolve_build_dir [arg]: sets BUILD_DIR (absolute), skips if it does not exist.
resolve_build_dir() {
    BUILD_DIR="${MW808_VALIDATE_BUILD_DIR:-${1:-${MW808_BUILD_DIR:-${MW808_REPO_ROOT}/build/linux-release}}}"
    case "${BUILD_DIR}" in /*) ;; *) BUILD_DIR="$(pwd)/${BUILD_DIR}" ;; esac
    if [ ! -d "${BUILD_DIR}" ]; then
        skip "build directory ${BUILD_DIR} does not exist"
    fi
    log "build directory: ${BUILD_DIR}"
}

# find_artefacts <type d|f> <glob>: prints matching plugin artefacts under the
# JUCE artefact tree of BUILD_DIR (never inside _deps or another bundle).
# Build trees nested inside BUILD_DIR (a directory holding its own
# CMakeCache.txt, e.g. sanitizer sub-builds) belong to another configuration
# and are skipped; validate them by passing their directory explicitly.
find_artefacts() {
    local type="$1" glob="$2" a n skip
    local -a nested=()
    while IFS= read -r n; do
        nested+=("$(dirname "${n}")")
    done < <(find "${BUILD_DIR}" -mindepth 2 \( -name _deps -o -name CMakeFiles -o -name '*_artefacts' \) -prune -o \
                -name CMakeCache.txt -type f -print 2>/dev/null)
    while IFS= read -r a; do
        skip=0
        for n in ${nested[@]:+"${nested[@]}"}; do
            case "${a}" in "${n}"/*) skip=1; break ;; esac
        done
        [ "${skip}" -eq 1 ] || printf '%s\n' "${a}"
    done < <(find "${BUILD_DIR}" \( -name _deps -o -name CMakeFiles \) -prune -o \
                -path '*_artefacts/*' -type "${type}" -name "${glob}" -print 2>/dev/null | sort)
}

# xvfb wrapper for anything that may open a window on a headless box.
# MW808_FORCE_XVFB=1 uses it even when DISPLAY is set (keeps editor windows off
# the desktop of a developer machine).
XVFB=()
setup_xvfb() {
    if [ -z "${DISPLAY:-}" ] || [ -n "${MW808_FORCE_XVFB:-}" ]; then
        if command -v xvfb-run >/dev/null 2>&1; then
            log "wrapping in xvfb-run -a (DISPLAY='${DISPLAY:-}', MW808_FORCE_XVFB='${MW808_FORCE_XVFB:-}')"
            XVFB=(xvfb-run -a)
        else
            warn "no DISPLAY and no xvfb-run: GUI tests may fail"
        fi
    fi
}

sha256_of() { sha256sum "$1" | cut -d' ' -f1; }

# download <url> <dest> <sha256>: returns 0 ok, 1 offline/unavailable, 2 checksum mismatch.
download() {
    local url="$1" dest="$2" want="$3" got
    mkdir -p "$(dirname "${dest}")"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 2 --connect-timeout 20 -o "${dest}.part" "${url}" || { rm -f "${dest}.part"; return 1; }
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "${dest}.part" "${url}" || { rm -f "${dest}.part"; return 1; }
    else
        warn "neither curl nor wget is installed"
        return 1
    fi
    got="$(sha256_of "${dest}.part")"
    if [ "${got}" != "${want}" ]; then
        warn "CHECKSUM MISMATCH for ${url}"
        warn "  expected ${want}"
        warn "  got      ${got}"
        rm -f "${dest}.part"
        return 2
    fi
    mv -f "${dest}.part" "${dest}"
    log "downloaded ${url} (sha256 ${got:0:16}... verified)"
    return 0
}

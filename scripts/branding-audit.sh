#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# branding-audit.sh — fails when a forbidden brand token (scripts/forbidden_tokens.txt)
# appears in the project's text files or in the strings of built binaries.
#
# Usage:
#   scripts/branding-audit.sh [--binaries <build-dir>]... [--all-builds] [repo-root]
#
#   text scan    every text file under the repo root (source, UI strings,
#                presets, scripts, CMake, docs, README, tests, tools), except
#                research/ (sources may be cited there), .deps/, build*/, .git/
#                and the token file itself. Binary files are skipped.
#   --binaries   also run `strings` over the shipped plugin artefacts in
#                <build-dir> (everything under *_artefacts/<config>/{VST3,CLAP,
#                LV2,Standalone}/: .so, .clap, standalone, generated .ttl /
#                .json) and the command-line tools. Intermediate static
#                archives (*_SharedCode.a) are not shipped and not scanned:
#                their compressed/random byte runs produce chance matches.
#   --all-builds the same for every directory under <repo>/build/.
#
# Matching: case-insensitive, at word boundaries, each token an extended regex.
# Exit codes: 0 clean, 1 forbidden token found, 2 usage or configuration error.

set -euo pipefail

repo_root=""
declare -a build_dirs=()
all_builds=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --binaries)
            [ "$#" -ge 2 ] || { echo "branding-audit: --binaries needs a build directory" >&2; exit 2; }
            build_dirs+=("$2")
            shift 2
            ;;
        --all-builds) all_builds=1; shift ;;
        -h|--help) sed -n '4,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "branding-audit: unknown option $1" >&2; exit 2 ;;
        *) repo_root="$1"; shift ;;
    esac
done

if [ -z "${repo_root}" ]; then
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
repo_root="$(cd "${repo_root}" && pwd)"
token_file="${repo_root}/scripts/forbidden_tokens.txt"
[ -f "${token_file}" ] || { echo "branding-audit: ${token_file} not found" >&2; exit 2; }

# All tokens for the text scan; the ones without a "text:" prefix for binaries.
mapfile -t tokens < <(grep -vE '^[[:space:]]*(#|$)' "${token_file}" | sed 's/^text://')
mapfile -t bin_tokens < <(grep -vE '^[[:space:]]*(#|$)|^text:' "${token_file}")
[ "${#tokens[@]}" -gt 0 ] && [ "${#bin_tokens[@]}" -gt 0 ] \
    || { echo "branding-audit: no tokens in ${token_file}" >&2; exit 2; }
pattern="$(printf '%s\n' "${tokens[@]}" | paste -sd '|')"
bin_pattern="$(printf '%s\n' "${bin_tokens[@]}" | paste -sd '|')"
# Validate the combined regexes once (grep exits 2 on a bad expression).
for p in "${pattern}" "${bin_pattern}"; do
    rc=0
    grep -qwiE "${p}" <<< "x" || rc=$?
    if [ "${rc}" -eq 2 ]; then
        echo "branding-audit: invalid regular expression in ${token_file}" >&2
        exit 2
    fi
done

if [ "${all_builds}" -eq 1 ] && [ -d "${repo_root}/build" ]; then
    while IFS= read -r d; do build_dirs+=("$d"); done \
        < <(find "${repo_root}/build" -mindepth 1 -maxdepth 1 -type d | sort)
fi

echo "branding audit: ${#tokens[@]} token pattern(s) (${#bin_tokens[@]} for binaries) from ${token_file#"${repo_root}"/}"
hits=0

# --- text files ------------------------------------------------------------------------
text_hits="$(mktemp)"
trap 'rm -f "${text_hits}"' EXIT
scanned=0
cd "${repo_root}"
while IFS= read -r -d '' f; do
    scanned=$((scanned + 1))
    rc=0
    grep -IHnwiE "${pattern}" "$f" >> "${text_hits}" || rc=$?
    if [ "${rc}" -gt 1 ]; then
        echo "branding-audit: cannot read ${f}" >&2
        exit 2
    fi
done < <(find . \
    \( -path ./research -o -path ./.deps -o -path ./.git -o -path './build' -o -path './build-*' \
       -o -path './cmake-build-*' -o -name node_modules -o -path ./.cache \) -prune -o \
    -type f ! -path ./scripts/forbidden_tokens.txt -print0)
n="$(wc -l < "${text_hits}")"
if [ "${n}" -gt 0 ]; then
    echo "FAIL: ${n} line(s) in text files:"
    sed 's/^\.\//  /' "${text_hits}"
    hits=$((hits + n))
else
    echo "text files: ${scanned} scanned, clean"
fi

# --- built binaries --------------------------------------------------------------------
for bd in ${build_dirs[@]:+"${build_dirs[@]}"}; do
    if [ ! -d "${bd}" ]; then
        echo "binaries: ${bd} does not exist, skipped"
        continue
    fi
    mapfile -d '' bins < <(find "${bd}" \( -name _deps -o -name CMakeFiles \) -prune -o -type f \
        \( -path '*_artefacts/*/VST3/*' -o -path '*_artefacts/*/CLAP/*' -o -path '*_artefacts/*/LV2/*' \
           -o -path '*_artefacts/*/Standalone/*' -o -name mw808-render -o -name mw808-wavcompare \) -print0 2>/dev/null)
    if [ "${#bins[@]}" -eq 0 ]; then
        echo "binaries: nothing built in ${bd}"
        continue
    fi
    bin_hits=0
    for b in "${bins[@]}"; do
        found="$(strings -a -n 4 "$b" 2>/dev/null | grep -nwiE "${bin_pattern}" | head -n 20 || true)"
        if [ -n "${found}" ]; then
            echo "FAIL: ${b}"
            printf '%s\n' "${found}" | sed 's/^/  /'
            bin_hits=$((bin_hits + $(printf '%s\n' "${found}" | wc -l)))
        fi
    done
    if [ "${bin_hits}" -eq 0 ]; then
        echo "binaries: ${#bins[@]} file(s) in ${bd}, clean"
    fi
    hits=$((hits + bin_hits))
done

if [ "${hits}" -gt 0 ]; then
    echo "RESULT: FAIL (${hits} forbidden-token hit(s))"
    exit 1
fi
echo "RESULT: PASS"

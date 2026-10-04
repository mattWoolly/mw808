#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# check_license_headers.sh [repo-root] — every C/C++, CMake and shell file
# under libs/ plugin/ tools/ tests/ scripts/ cmake/ (plus the top-level
# CMakeLists.txt) must open with the two-line licence header:
#
#   // SPDX-License-Identifier: AGPL-3.0-or-later                  (.h .hpp .cpp .cc)
#   // mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
#   # SPDX-License-Identifier: AGPL-3.0-or-later                   (.cmake CMakeLists.txt .sh)
#   # mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.
#
# A shell script may have its #! line first. Files are found with `find`, not
# git, so the check works in a tree that is not a repository. Build trees and
# third-party checkouts (build*/, .deps/, _deps/) are skipped.
# Exit: 0 all present, 1 a header is missing or wrong, 2 nothing found.

set -euo pipefail

root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "${root}"

spdx="SPDX-License-Identifier: AGPL-3.0-or-later"
notice="mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio."

declare -a dirs=()
for d in libs plugin tools tests scripts cmake; do
    [ -d "$d" ] && dirs+=("$d")
done

checked=0
bad=0
while IFS= read -r -d '' f; do
    checked=$((checked + 1))
    case "$f" in
        *.h|*.hpp|*.cpp|*.cc) prefix="//" ;;
        *) prefix="#" ;;
    esac
    first="$(sed -n '1p' "$f")"
    second="$(sed -n '2p' "$f")"
    if [[ "$f" == *.sh && "${first}" == '#!'* ]]; then
        first="${second}"
        second="$(sed -n '3p' "$f")"
    fi
    if [ "${first}" != "${prefix} ${spdx}" ] || [ "${second}" != "${prefix} ${notice}" ]; then
        echo "MISSING OR WRONG HEADER: ${f#./}"
        echo "    line 1: ${first:0:100}"
        echo "    line 2: ${second:0:100}"
        bad=$((bad + 1))
    fi
done < <(
    {
        [ -f CMakeLists.txt ] && printf '%s\0' ./CMakeLists.txt
        if [ "${#dirs[@]}" -gt 0 ]; then
            find "${dirs[@]}" -type d \( -name 'build*' -o -name .deps -o -name _deps -o -name node_modules \) -prune -o \
                -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cc' -o -name '*.cmake' \
                -o -name CMakeLists.txt -o -name '*.sh' \) -print0
        fi
    } | sort -z
)

if [ "${checked}" -eq 0 ]; then
    echo "license headers: no files found under ${root}" >&2
    exit 2
fi
if [ "${bad}" -ne 0 ]; then
    echo "license headers: ${bad} of ${checked} file(s) lack the two-line header:"
    echo "    // ${spdx}      (C/C++; '#' instead of '//' in CMake and shell files)"
    echo "    // ${notice}"
    exit 1
fi
echo "license headers: all ${checked} file(s) carry the two-line header"

# 06 — Local conventions of the mwAudio plugin line (input to mw808)

Scope: read-only survey of the sibling plugin projects on this machine, so mw808
matches the house conventions. Everything here was read from local files on
2026-10-02. Paths are absolute. No web sources: every claim cites the local file
it came from. Anything inferred rather than read is marked **INFERRED**, and
anything I could not confirm is marked **UNVERIFIED**.

Projects surveyed:

| Project | Product id | Framework | Build deps | Last activity | Role for mw808 |
|---|---|---|---|---|---|
| `/home/mwoolly/projects/mwStime` | `mwStime` (Akai timestretch) | JUCE 8.0.13 + clap-juce-extensions + Catch2 3.7.1, all FetchContent | C++20 | 2026-07-07 | **Primary template**: JUCE-free core lib, presets, golden renders, validators, UI goldens |
| `/home/mwoolly/projects/mw140` | `mw140` (plate reverb) | JUCE 8.0.12 + clap-juce-ext as **git submodules** in `libs/` | C++20 | 2026-05-02 | Identity/branding discipline, JSON presets, allocator/lock spy tests, packaging |
| `/home/mwoolly/projects/mw160` | `MW160` (VCA comp) | JUCE **7.0.12** + Catch2 3.5.4 FetchContent | C++17 | 2026-06-08 | Older; simple pluginval script, no-alloc test exe |
| `/home/mwoolly/projects/ssl4k-2` | `mw4000` (console strip) | JUCE **7.0.12** + clap-juce-ext submodules | C++20 | 2026-04-14 | Holds the **house style guide**; .deb/.rpm + `~/.vst3`/`~/.clap` install |
| `/home/mwoolly/projects/ssl4k` | `MW4000` (older) | DPF (not JUCE) | C++17 | 2026-04-04 | Superseded by ssl4k-2; ignore |
| `/home/mwoolly/projects/mw90` | `mw90` (digital reverb) | iPlug2 + raw VST3 SDK, Cairo/X11 UI | C++17 | 2026-04-14 | Only source of a Linux `~/.vst3` POST_BUILD deploy |
| `/home/mwoolly/projects/mw303` | `mw12F` (acid synth, **in progress right now**, files dated 2026-10-02 21:16) | JUCE 8.0.13 FetchContent, IS_SYNTH | C++20 | today | Closest-shaped sibling (instrument), but volatile: do not depend on its files |

---

## 1. Identity: company, codes, bundle ids

### 1.1 The written standard

The house style guide is `/home/mwoolly/projects/ssl4k-2/docs/MW_AUDIO_STYLE_GUIDE.md`.
mw140's `ORCHESTRATOR.md` §4 cites it as the authority:
"Reference the style guide at `../ssl4k-2/docs/MW_AUDIO_STYLE_GUIDE.md` for full details".

| Field | Rule (style guide) |
|---|---|
| Company name | `mwAudio` (one token, lowercase `mw`, capital `A`) |
| Manufacturer code | `mwAu`, shared by every plugin |
| Bundle id | `com.mwaudio.<productName>`, all lowercase |
| CLAP id | same as bundle id |
| Product name | lowercase `mw` prefix, e.g. `mw160`, `mw4000`, `mwVnylSim` |
| Plugin code | `mw` + 2 chars, e.g. `mw16`, `mw40`, `mwVn` |
| Classes/files | `<productName>Processor`, `<productName>Editor`, `<productName>LookAndFeel`, `<productName>Colours` (files named like the class, e.g. `mw4000LookAndFeel.h`) |
| Parameter ids | `snake_case` `constexpr const char*` in a central `ParameterIDs.h`; `juce::ParameterID{id, 1}` version tags; no brand names (they are serialized) |
| Preset dir | `[userApplicationDataDirectory]/mwAudio/<productName>/Presets/` → Linux `~/.config/mwAudio/<productName>/Presets/`; create on first access; migrate legacy `mw Audio/<ProductName>/Presets/` |
| Faceplate | product name shown with lowercase `mw` |
| Licence | "GPL v3 (JUCE GPL tier). Copyright holder: `mwAudio`" (see §6: outdated for JUCE 8) |
| Brands | "Never use original manufacturer brand names in code, comments, or docs" (e.g. "classic 4000E-style console", "classic hardware sampler") |
| CI | `build.yml` / `build-and-validate.yml`, `release.yml` on `v*.*.*` tags, `VERSION` file at repo root, pluginval strictness 5+ in CI and 10 for release |

### 1.2 What each project actually ships

Extracted from each `juce_add_plugin(...)` / `config.h`:

| Project | PRODUCT_NAME | COMPANY_NAME | MFR code | PLUGIN_CODE | BUNDLE_ID / CLAP_ID | Notes |
|---|---|---|---|---|---|---|
| mw140 | `mw140` | `mwAudio` | `mwAu` | `mw14` | `com.mwaudio.mw140` | style-guide compliant; `DESCRIPTION`, `VST3_CATEGORIES "Fx" "Reverb"`, `CLAP_FEATURES audio-effect reverb` |
| ssl4k-2 | `mw4000` | `mwAudio` | `mwAu` | `mw40` | `com.mwaudio.mw4000` | compliant |
| mw160 | `MW160` | `"mw Audio"` (legacy) | `mwAu` | `mw16` | `com.mwaudio.mw160` | pre-rename spelling |
| mwVnylSim | `mwVnylSim` | `mwAudio` | `mwAu` | `mwVn` | `com.mwAudio.mwVnylSim` (mixed case, deviates) | |
| mw90 | `mw90` | `mwAudio` (`PLUGIN_VENDOR`) | n/a | n/a | `com.mwaudio.mw90` | iPlug2 |
| mw303 (WIP) | `mw12F` via `MW_PRODUCT_NAME` | `mwAudio` | `mwAu` | `m12F` | `com.mwaudio.${MW_PRODUCT_ID}` | `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT/OUTPUT TRUE`, `VST3_CATEGORIES Instrument Synth`, `CLAP_FEATURES instrument synthesizer mono`, `LV2URI https://github.com/mattWoolly/${MW_PRODUCT_NAME}` |
| **mwStime** | `mwStime` | **`mattWoolly`** | **`MwSt`** | **`MwS1`** | **`com.mattwoolly.mwstime`** | **deviates from the style guide**; `LV2URI "https://github.com/mattWoolly/mwStime"` |

What a host shows (from `~/.config/REAPER/reaper-vstplugins64.ini`):
`mw140 (mwAudio)`, `mw160 (mwAudio)`, `mw4000 (mwAudio)`, `mw90 Reverb (mwAudio)`,
`mwVnylSim (mwAudio)`, but **`mwStime (mattWoolly)`**. So the newest project broke the
house identity. mw808 should follow the style guide (`mwAudio` / `mwAu` /
`com.mwaudio.mw808`), not mwStime.

Useful pattern from mw303 (`/home/mwoolly/projects/mw303/CMakeLists.txt`): the
product name is set once at the top and everything else derives from it:

```cmake
# The product name lives here and nowhere else. Plugin name, bundle/CLAP ids and
# the LV2 URI all derive from it; source code uses the neutral `acid` namespace.
set(MW_PRODUCT_NAME "mw12F")
set(MW_PRODUCT_ID   "mw12f")
set(MW_PLUGIN_CODE  "m12F")
```

That makes a late rename (the "cool name" decision) a one-line change.

### 1.3 Trademark hygiene (binding in this line)

- Style guide: no manufacturer brand names in code, comments or docs.
- mw140 goes further. `docs/PRESET_SPEC.md` §0 and `docs/TEST_STRATEGY.md` §0
  ban the brand, its parent company, the founder and the lead engineer from
  every doc, source file, comment, asset filename, test fixture, commit message,
  PR title/body and **built-binary string**. Permitted wording looks like
  "classic plate reverb" or "vintage German plate".
- Enforcement: `/home/mwoolly/projects/mw140/scripts/branding-audit.sh` greps git-tracked
  text files case-insensitively at word boundaries against
  `scripts/forbidden_tokens.txt`, which the scan excludes from itself. With
  `--binaries` it also runs `strings` over built binaries. Exit codes:
  0 clean, 1 hit, 2 usage error.
- User memory (`~/.claude/projects/-home-mwoolly-projects-mw160/memory/user_audio_developer.md`):
  "Trademark hygiene is important to this user ... treat this as a binding constraint."
- Model numbers do appear in product names (mw140, mw160, mw4000, mw90), so
  **`mw808` fits the naming pattern**. The tokens to keep out of code, UI, presets and
  binaries are things like the manufacturer name, "TR-808" and "Rhythm Composer".
  Use wording such as "classic analog drum machine". **INFERRED**: research/ notes
  can cite sources by name, as mw140's research did before its scrub pass, but
  shipped code and UI cannot.
- Artist names: allowed only in preset `historical_reference` metadata, never
  in preset names (mw140 PRESET_SPEC §0; mw140 memory `project_mw140_phase1_decisions.md`).

---

## 2. Directory layout

### 2.1 mwStime (the template to copy)

From `/home/mwoolly/projects/mwStime/docs/design/architecture.md` §8 and the tree on disk:

```
mwStime/
├── CMakeLists.txt            # top level: options, Fetch*, add_subdirectory(...)
├── CMakePresets.json         # default / linux-default (Ninja) / windows-default / tsan
├── cmake/
│   ├── FetchJUCE.cmake       # JUCE 8.0.13, GIT_SHALLOW
│   ├── FetchClapExtensions.cmake  # pinned COMMIT 51a9359…
│   └── FetchCatch2.cmake     # v3.7.1 + appends ${catch2_SOURCE_DIR}/extras to module path
├── libs/mwstime-core/        # PURE C++20 static lib, no JUCE ever
│   ├── CMakeLists.txt
│   ├── include/mws/{core,engine,model,stretch}/*.h
│   └── src/{core,engine,model,stretch}/*.cpp
├── plugin/
│   ├── CMakeLists.txt        # juce_add_plugin + clap_juce_extensions_plugin
│   ├── src/                  # PluginProcessor, PluginEditor, EngineHost, Published.h (RCU), …
│   │   └── state/            # Parameters, StateTree, Migrations, FactoryPresets, StateBlobCache
│   └── ui/                   # Faceplate, FaceplateSpec/Geometry, EditorResize.h, LcdDisplay, …
│       └── lookandfeel/SeriesLookAndFeel.{h,cpp}
├── tools/
│   ├── mwstime-render/       # JUCE-free CLI golden renderer (links core only)
│   ├── gen_golden_inputs/    # deterministic synthetic test-signal generator
│   ├── golden_compare/       # two-stage comparer with diagnostics
│   ├── bless_goldens.sh  bless_ui_goldens.sh  run_golden_case.sh  run_ui_golden_case.sh
│   └── check_license_headers.sh
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/                 # JUCE-free Catch2, one file per core module + TestAllocationCounter.h
│   ├── golden/{inputs,blessed,cases.json,RunGoldenCase.cmake,CMakeLists.txt}
│   ├── plugin/               # JUCE-linked Catch2 console app + validator scripts
│   └── ui/{screenshot/,blessed/*.png,CMakeLists.txt}
├── docs/{BUILDING.md,CONTRIBUTING.md,MANUAL.md,QA-REPORT.md,design/,research/,qa/}
├── plan/{ORCHESTRATION.md,backlog/NNN-*.md,decisions/NNN-*.md (ADRs)}
├── .github/workflows/ci.yml
├── LICENSE (AGPLv3), .gitignore, .markdownlint.jsonc
```

### 2.2 mw140 / ssl4k-2 (older flat layout)

`source/` (flat, `mw140Processor.cpp`, `mw140Editor.cpp`, `mw140LookAndFeel.cpp`,
DSP classes alongside), `libs/JUCE` + `libs/clap-juce-extensions` (submodules),
`assets/{fonts,patina,textures}`, `Presets/Factory/*.mw140preset.json`,
`resources/icons/mw140.png`, `scripts/{branding-audit.sh,packaging/{linux,macos,windows}}`,
`tests/{unit,dsp,realtime,golden,ui-golden,benchmarks,packaging,util}`, `docs/`,
`research/`, `VERSION`, `.clang-format`, `.clang-tidy`, `.editorconfig`.
ssl4k-2 has the same shape with `source/{dsp,gui,preset}` and `packaging/{linux,macos,windows}`.

---

## 3. CMake structure and dependency acquisition

### 3.1 How each project gets its dependencies

| Project | JUCE | clap-juce-extensions | Catch2 |
|---|---|---|---|
| mwStime | FetchContent `8.0.13`, `GIT_SHALLOW TRUE` (`cmake/FetchJUCE.cmake`) | FetchContent **commit** `51a9359315298de632cf44e9d7524940868441e6` (`cmake/FetchClapExtensions.cmake`) | FetchContent `v3.7.1`, shallow |
| mw303 (WIP) | same `8.0.13` | same commit ("pinned to the commit the rest of the mwAudio line uses") | same `v3.7.1` |
| mw140 | submodule `libs/JUCE` at `8.0.12`, `add_subdirectory(libs/JUCE)` | submodule at `e1f67893…`, `add_subdirectory(... EXCLUDE_FROM_ALL)` | FetchContent `v3.5.2` in `tests/CMakeLists.txt` |
| ssl4k-2 | submodule `libs/JUCE` at **7.0.12** | submodule `e1f67893…` | FetchContent v3.5.2 |
| mw160 | FetchContent **7.0.12** | none | FetchContent v3.5.4 |

The comment in mwStime's `cmake/FetchClapExtensions.cmake` explains the pin:

> Pinned to a commit hash, not a release tag: the newest upstream release tag
> (0.26.0) predates JUCE 7/8 support and fails to compile against JUCE 8
> (includes JUCE-6-era wrapper paths). The commit below is upstream main as of
> 2026-06-01, which builds cleanly against the JUCE tag in FetchJUCE.cmake.
> Must be included AFTER cmake/FetchJUCE.cmake. Bumping the pin is an EXPLICIT
> backlog task, never an incidental upgrade.

`architecture.md` §7: "JUCE 8 + deps via FetchContent (no submodules —
agent-friendly clean clones)". mwStime's `docs/BUILDING.md` recommends sharing the
download cache with `-DFETCHCONTENT_BASE_DIR="$HOME/.cache/mwstime-fc"`. That
directory does **not** exist on this machine (`ls ~/.cache` shows no fc cache).

### 3.2 Reusable local checkouts (full paths)

Verified with `git describe`, `rev-parse` and the `project(... VERSION ...)` line on 2026-10-02:

**JUCE 8.0.13** (the version mw808 should pin):
- `/home/mwoolly/projects/mwStime/build/linux-default/_deps/juce-src` (git checkout, tag `8.0.13`). **Use this one.**
- `/home/mwoolly/projects/mw303/build/_deps/juce-src` (`8.0.13`, created today by an in-progress session, so it may disappear or change. Avoid.)

**JUCE 8.0.12** (fallback):
- `/home/mwoolly/projects/mw140/libs/JUCE` (submodule; the "vendored JUCE 8.0.12")
- `/home/mwoolly/projects/mw303VinylSim/libs/JUCE`
- `/home/mwoolly/projects/mwVnylSim/build/_deps/juce-src`

Not suitable: JUCE 7.0.12 in `mw160/build/_deps/juce-src` and `ssl4k-2/libs/JUCE`;
JUCE 8.0.4 in `mwTerminalSynth/build/_deps/juce-src`.

**clap-juce-extensions**:
- commit `51a9359315298de632cf44e9d7524940868441e6` (2026-06-01, the current line pin):
  `/home/mwoolly/projects/mwStime/build/linux-default/_deps/clap-juce-extensions-src`
  (its `clap-libs/clap` and `clap-libs/clap-helpers` submodules are populated) and
  `/home/mwoolly/projects/mw303/build/_deps/clap-juce-extensions-src` (volatile).
- commit `e1f67893cc409a40c1154fa2e78c97046da24ce0` (2026-03-09, older):
  `/home/mwoolly/projects/mw140/libs/clap-juce-extensions`, `/home/mwoolly/projects/ssl4k-2/libs/clap-juce-extensions`.
- Licence: MIT (`LICENSE.md` in the checkout).

**Catch2**:
- v3.7.1: `/home/mwoolly/projects/mwStime/build/linux-default/_deps/catch2-src`
  (also `mwStime/build/tsan/_deps/catch2-src`, `mwStime/build/scratch/_deps/catch2-src`)
- v3.5.2: `mw140/build/_deps/catch2-src`, `mw303VinylSim/build/_deps/catch2-src`, `ssl4k-2/build/_deps/catch2-src`
- v3.5.4: `mw160/build/_deps/catch2-src`

How to reuse them offline without changing the pinned declarations: keep the
same `FetchContent_Declare` pins as mwStime and, at configure time, point
FetchContent at a local copy:

```sh
cmake --preset linux-release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=<copy of juce-src> \
  -DFETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS=<copy of clap-juce-extensions-src> \
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=<copy of catch2-src>
```

**INFERRED** (CMake semantics, not tested here): the variable suffix is the
upper-cased content name with hyphens kept. **Recommendation**: `cp -a` the three
directories into a cache that mw808 owns (for example `~/.cache/mwaudio-fc/`)
instead of pointing at another project's `build/`, which can be wiped at any time.

### 3.3 Top-level CMake shape (mwStime, to copy)

`/home/mwoolly/projects/mwStime/CMakeLists.txt`:
- `cmake_minimum_required(VERSION 3.25)`, `project(... VERSION 0.1.0 LANGUAGES C CXX)`,
  `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`.
- `option(MWS_BUILD_PLUGIN "Build the JUCE plugin" ON)`. JUCE and clap-juce-ext are
  included **only when the plugin is built**, so the core plus golden suite configures
  without JUCE. The `tsan` preset relies on this by setting `MWS_BUILD_PLUGIN=OFF`.
- `include(cmake/FetchCatch2.cmake)`, then `enable_testing()` **before** any
  `add_subdirectory`. The comment explains that `catch_discover_tests` in sub-trees
  needs testing enabled in the parent scope.
- Order: `libs/<core>` → `tools/*` → (if plugin) `FetchJUCE` + `FetchClapExtensions` + `plugin` → `tests`.

Other projects (mw140, ssl4k-2) read the version from a `VERSION` file:
`file(READ VERSION ...)` + `string(STRIP ...)` + `project(... VERSION ${...})`.
The style guide asks for that file. They also set `CMAKE_EXPORT_COMPILE_COMMANDS ON`,
macOS universal-arch flags, and `CMP0141`/`Embedded` MSVC debug info. These are
irrelevant on Linux but harmless.

`CMakePresets.json` (mwStime, schema version 6): `linux-default` = Ninja +
RelWithDebInfo, binaryDir `${sourceDir}/build/linux-default`, `condition` hostSystemName
== Linux. The `tsan` preset adds `-fsanitize=thread -fno-omit-frame-pointer -g`,
Debug, and plugin OFF. Build presets set `"jobs": 6`. Test presets set `outputOnFailure: true`.

### 3.4 Plugin target (mwStime `plugin/CMakeLists.txt`)

```cmake
juce_add_plugin(mwStime
    PRODUCT_NAME "mwStime"  COMPANY_NAME mattWoolly  BUNDLE_ID com.mattwoolly.mwstime
    PLUGIN_MANUFACTURER_CODE MwSt  PLUGIN_CODE MwS1
    FORMATS ${MWS_PLUGIN_FORMATS}            # Linux: VST3 LV2 Standalone
    IS_SYNTH FALSE  NEEDS_MIDI_INPUT TRUE  NEEDS_MIDI_OUTPUT FALSE  IS_MIDI_EFFECT FALSE
    LV2URI "https://github.com/mattWoolly/mwStime"
    COPY_PLUGIN_AFTER_BUILD ${MWS_COPY_PLUGIN_AFTER_BUILD})   # TRUE on macOS only
target_compile_definitions(mwStime PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(mwStime PRIVATE mwstime_core juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
clap_juce_extensions_plugin(TARGET mwStime CLAP_ID "com.mattwoolly.mwstime" CLAP_FEATURES audio-effect stereo)
```

The other JUCE projects also define `JUCE_DISPLAY_SPLASH_SCREEN=0` (mw140, mw160,
ssl4k-2, mw303). Test console apps also add `JUCE_MODAL_LOOPS_PERMITTED=0`,
`NEEDS_CURL FALSE` and `NEEDS_WEB_BROWSER FALSE`.

**Hard local constraint**: this machine has no `libcurl` or `webkit2gtk-4.1` dev
packages (`pkg-config` cannot find them). JUCE builds here only because of
`JUCE_USE_CURL=0` / `JUCE_WEB_BROWSER=0`. Present: alsa 1.2.11, freetype2, fontconfig 2.15,
gl, jack 1.9.21, gtk+-3.0, x11, xrandr, xinerama/xcursor/xcomposite/xext/xrender dev packages.

---

## 4. Plugin formats on Linux and installation

### 4.1 Formats built

- mwStime: Linux `VST3 LV2 Standalone` + CLAP (shim). ADR
  `/home/mwoolly/projects/mwStime/plan/decisions/002-plugin-formats-v1.md` chose LV2 at v1.
  It records that JUCE's LV2 export "is its flakiest" and that dropping LV2 later is a one-line change.
- mw140 / ssl4k-2: `VST3 AU Standalone` + CLAP. No LV2.
- mw303 (WIP instrument): `VST3 LV2 Standalone` + CLAP.
- The style guide's minimum is VST3 + AU + Standalone, with CLAP optional.

Artefact paths are `<build>/<subdir>/<Target>_artefacts/<Config>/{VST3,LV2,CLAP,Standalone}/`,
for example `mwStime/build/linux-default/plugin/mwStime_artefacts/RelWithDebInfo/{CLAP,LV2,Standalone,VST3}`.

### 4.2 How plugins reach `~/.vst3`, `~/.clap`, `~/.lv2`

There is **no `cmake --install` target in any JUCE project**. Plugins are installed by:

1. **Manual copy** (README instructions). From ssl4k-2 `README.md` §Install:
   ```
   cp -r build/mw4000_artefacts/Release/VST3/mw4000.vst3 ~/.vst3/
   cp -r build/mw4000_artefacts/Release/CLAP/mw4000.clap ~/.clap/
   ```
2. **A user-install script** generated by `/home/mwoolly/projects/ssl4k-2/packaging/linux/build-packages.sh`
   (lines ~169–195): `VST3_DEST="$HOME/.vst3"`, `CLAP_DEST="$HOME/.clap"`,
   `rm -rf "$VST3_DEST/mw4000.vst3"` then copy. It also builds `.deb`/`.rpm` with
   system paths `/usr/lib/vst3/<p>.vst3/`, `/usr/lib/clap/<p>.clap`, `/usr/bin/<p>`,
   `/usr/share/applications/<p>.desktop`.
3. **POST_BUILD auto-deploy** (mw90 only, raw VST3 SDK): `add_custom_command(TARGET mw90 POST_BUILD ...)`
   creates `~/.vst3/mw90.vst3/Contents/x86_64-linux/` and copies the `.so`, then
   `cmake -E touch "${VST3_BUNDLE_DIR}"` ("so Reaper detects the updated binary").
4. JUCE `COPY_PLUGIN_AFTER_BUILD` is used **only on macOS** (mwStime), and set FALSE elsewhere.
5. mw140 packaging: `scripts/packaging/linux/build_deb.sh` (`/usr/lib/vst3`) and
   `build_appimage.sh` (Standalone AppImage; pinned appimagetool 1.9.1 + SHA-256).

Currently installed (2026-10-02): `~/.vst3/{mw140,mw160,mw4000,mw90,mwStime,mwVnylSim}.vst3`,
`~/.clap/{mwStime.clap,SSL4K.clap}`, `~/.lv2/mwStime.lv2/` (holding `libmwStime.so`,
`manifest.ttl`, `dsp.ttl` and `ui.ttl`). mwStime's installed binaries are **~116–121 MB unstripped
RelWithDebInfo** (`file`: "with debug_info, not stripped"). Don't repeat that:
install a Release build. mwVnylSim strips Release VST3 on Linux via a POST_BUILD
`${CMAKE_STRIP} --strip-unneeded "$<TARGET_FILE:mwVnylSim_VST3>"` (REPAIR-032, "cuts
the binary roughly in half").

Note `.gitignore` in mwStime ignores `*.clap`, `*.vst3`, `*.so`, `build/` and `_deps/`.

---

## 5. Compile flags, LTO, FP discipline

- All JUCE projects link `juce::juce_recommended_config_flags`,
  `juce_recommended_lto_flags` and `juce_recommended_warning_flags`. From the
  JUCE 8.0.13 source (`extras/Build/CMake/JUCEHelperTargets.cmake`):
  - config: Debug `-g -O0`; **Release `-O3`**; RelWithDebInfo gets nothing extra, so CMake's default `-O2 -g` applies.
  - LTO: `-flto` **only for `$<CONFIG:Release>`**. The RelWithDebInfo presets get no LTO.
  - GCC warnings: `-Wall -Wextra -Wpedantic -Wstrict-aliasing -Wuninitialized -Wunused-parameter -Wsign-compare -Wsign-conversion -Wunreachable-code -Wcast-align -Wswitch-enum -Wredundant-decls -Wshadow -Wfloat-equal -Wmissing-field-initializers -Woverloaded-virtual -Wreorder -Wzero-as-null-pointer-constant` (plus some `-Wno-*`).
- mwStime FP discipline (`CMakeLists.txt` header and `libs/mwstime-core/CMakeLists.txt`):
  **no `-ffast-math` anywhere** ("never added; do not add it"), and
  `-ffp-contract=off` on the core target (`/fp:precise` on MSVC) so golden renders are
  deterministic. Applied with generator expressions per compiler id.
- The core static lib has `POSITION_INDEPENDENT_CODE ON`. The hard-won Linux
  lesson: a non-PIC static lib linked into the LV2/VST3/CLAP `.so` fails with
  "relocation R_X86_64_PC32 ... recompile with -fPIC" (GNU ld reports it as
  "final link failed: bad value"). mw303 sets `CMAKE_POSITION_INDEPENDENT_CODE ON` globally.
- ssl4k (DPF) used plain `-Wall -Wextra -Wpedantic`. No project sets `-Werror`.
  mw140's `.clang-tidy` deliberately does not set WarningsAsErrors.
- Toolchain on this machine: g++ 13.3.0, cmake 3.28.3, ninja at `~/.local/bin/ninja`
  (also the ninja-build 1.11.1 package). **No clang, lld, mold, ccache or sccache installed.**
  CPU: Xeon E5-2699 v4 @ 2.20 GHz, 44 threads (`nproc`). This is the same CPU family
  mw140 benchmarked on, where its modal bank measured "naïve=96% core, SSE2=31%" (mw140
  memory `project_mw140_overview.md`). Budget CPU with that in mind.
- Formatting: mw140/ssl4k-2 `.clang-format` = `BasedOnStyle: Google, IndentWidth: 2,
  ColumnLimit: 100`. mwStime has no `.clang-format`; it uses 4-space indent and Allman
  braces for functions and control blocks. Either is acceptable; pick one and commit a `.clang-format`.

---

## 6. Licence and SPDX headers

| Project | LICENSE file | Source header |
|---|---|---|
| mwStime | AGPLv3 full text | two lines, `//` (or `#` in CMake/sh, `<!-- -->` in md): `// SPDX-License-Identifier: AGPL-3.0-or-later` / `// mwStime — Akai S-series timestretch emulation. Copyright (C) 2026 mattWoolly.` |
| mw140 | GPLv3 notice, "Copyright (C) 2026 mwAudio" | three lines: `// mw140 — classic steel-plate reverb emulation` / `// Copyright (C) 2026 mwAudio` / `// Licensed under GPL v3 (see LICENSE).` |
| ssl4k-2 | **"All rights reserved" proprietary text** | README says GPLv3: an internal inconsistency |
| style guide | "GPL v3 (JUCE GPL tier). Copyright holder: mwAudio" | n/a |

Important: **JUCE 8 modules are AGPLv3 / commercial, not GPLv3.** The local checkout
`.../_deps/juce-src/LICENSE.md` says "The JUCE Framework modules are dual-licensed under
the AGPLv3 and the commercial JUCE licence". The style guide's "JUCE GPL tier" dates
from JUCE 7. mwStime's AGPL-3.0-or-later is the consistent choice for a JUCE 8 build.
**Recommendation for mw808**: `AGPL-3.0-or-later`, copyright holder `mwAudio`. This
deviates from the style-guide text, so flag it for the user. If they insist on
GPLv3, note the JUCE 8 AGPL obligation in the README.

Enforcement: `/home/mwoolly/projects/mwStime/tools/check_license_headers.sh` requires
the SPDX line within the first 3 lines of every `.h/.hpp/.cpp/.cc/.mm` under
`libs/ plugin/ tools/ tests/`. It is registered as CTest `license_headers` (non-Windows).
**Caveat**: it enumerates files with `git ls-files`, so in a non-git directory it checks
nothing and passes silently. `/home/mwoolly/projects/mw808` is **not a git repo** today.
Either `git init` first or switch the checker to `find`.

mwStime's `.markdownlint.jsonc` disables MD013 (line length), MD033 (inline HTML,
needed for the SPDX comment) and MD041 (first line is the SPDX comment, not a heading).

---

## 7. README and docs structure

mwStime `README.md` sections, in order: SPDX comment lines → `# mwStime` → bold
one-paragraph pitch with formats → 4 bullet features → an "Honesty first" blockquote →
What it does → Models → (mode explanation) → Formats & host notes (table) →
Screenshots (taken from the blessed UI goldens) → User manual (link to `docs/MANUAL.md`) →
Build from source (preset trio + link to `docs/BUILDING.md`) → **Authenticity
statement** (each claim is "Research-pinned", "Pragmatic invention (PI)" or
"Deliberate deviation" + ADR) → License → Credits & citations (links to `docs/research/*`).

ssl4k-2 `README.md`: Title + one-line tagline + badges (Build, Release, GPLv3, test count) →
Quick Start → Signal Flow → DSP → Controls (per section) → GUI → Factory Presets (table) →
Building (Prerequisites, Linux system deps apt line, Build, artefact tree, Install
per OS) → Testing (category table) → Architecture (tree) → CI → Releases → Licence →
Acknowledgments. mw140's README is a stub with a mandatory "Branding" disclaimer paragraph:
"`mw140` is an original mwAudio product. It is not affiliated with, endorsed by, or
derived from any specific hardware manufacturer...".

Design docs: mwStime keeps `docs/design/{architecture,dsp-engine,testing-strategy,ui-design}.md`
and ADRs in `plan/decisions/NNN-*.md` (Status/Date/Context/Options/Decision/Consequences),
with backlog tasks in `plan/backlog/NNN-*.md` (YAML front matter: id, title, status,
depends-on, component, estimated-size). mw140 uses `docs/{DSP_SPEC,VISUAL_SPEC,PRESET_SPEC,TEST_STRATEGY,BACKLOG}.md`.

The authenticity labels (PI = pragmatic invention) are a strong convention worth
copying for an 808 emulation: every numeric DSP constant is either cited or labelled (PI).

---

## 8. Test strategy

### 8.1 Layers (mwStime `docs/design/testing-strategy.md` §1)

1. Unit tests of the core (Catch2 via CTest, JUCE-free binary `mwstime_tests`).
2. Property/invariant tests (Catch2): "authenticity contracts" that are never weakened without an ADR.
3. Golden-render regression: a JUCE-free CLI renderer + comparer.
4. Format validation: pluginval (VST3), clap-validator (CLAP), lv2lint/lv2_validate/Carla (LV2).
5. State round-trip / automation tests: a JUCE-linked Catch2 console app `mwstime_plugin_tests`.
6. Host smoke: scripted REAPER plus manual checklists (`tests/plugin/host-matrix.md`).
7. Adversarial QA fleet (agent checklists): NaN/inf/denormal hunting, extreme params,
   SR matrix 44.1/48/88.2/96/192 kHz, 30-minute soak, automation spam at 1000 changes/s.

The rule: everything runs from one `ctest --preset <p>`. Catch2 tags don't map to CTest
labels, so labelled subsets are registered as extra `add_test(NAME "<label>: ..." COMMAND <bin> "[tag]")`
entries with `set_tests_properties(... LABELS <label>)`. Labels in use: `tsan`, `host-smoke`,
`golden`, `ui-golden`, `validators`, `qa-adversarial`. Test names start with the
label so `ctest -R <label>` selects them too.

### 8.2 Golden renders

- `tests/golden/cases.json` is the single source of truth. Each case has `id`, `input`, `params`,
  `bitDepth` (16 by default so each WAV stays under 1 MB, committed to plain git with no LFS),
  `policy` (`exact` | `tolerance`) and `note`. CMake parses it with `string(JSON ...)` and registers
  one CTest per case (`golden_<id>`, LABEL `golden`). The runner is `RunGoldenCase.cmake`
  (`cmake -P`, so it is cross-platform).
- The comparer prints diagnostics on mismatch: max/RMS diff, first divergent sample,
  and a 1/3-octave difference table, so failures can be debugged without a DAW.
- Tolerance policy: bit-exact on the reference platform; off-reference float stages
  use max abs ≤ 1e-6. **mwStime's reference platform is macOS arm64.** For mw808 the
  reference is this Linux x86_64 box.
- Blessing: `cmake --build --preset <p> --target bless_goldens` wraps `tools/bless_goldens.sh`.
  It **refuses unless `BLESS_REASON="..."` is set** and writes `blessed/MANIFEST.json`
  (engineVersion, engineVersionHash, date, blesser, platform, reason, caseCount).
- A `goldeninputs` CTest regenerates the synthetic input corpus and asserts it is byte-identical.
  Inputs are original synthetic material only: no copyrighted audio in the repo.
- mw160 keeps simpler `.golden` files read via a `GOLDEN_DIR` compile definition.
  mw140 uses git LFS plus `tests/util/wav_compare.h`.

### 8.3 UI golden screenshots

- mwStime `tests/ui/`: a `juce_add_console_app(ui_screenshot ...)` that compiles the real
  UI components (no PluginEditor, so no timers or animation), runs under
  `juce::ScopedJuceInitialiser_GUI`, renders into `juce::Image(..., juce::SoftwareImageType())`,
  and writes PNGs via `juce::PNGImageFormat`. There is one CTest per state (LABEL `ui-golden`),
  plus a `bless_ui_goldens` target and committed `tests/ui/blessed/*.png` + `MANIFEST.json`.
  Exact compare happens only on the reference platform; elsewhere it is a tolerance
  compare, because "cross-platform AA is not deterministic".
- mw140 `tests/ui-golden/`: PNG goldens per scale (0.8/1.0/1.25/1.5/2.0) × mode, an
  `OffscreenRender.{h,cpp}` helper, colour-blindness simulation PNGs and an
  `AccessibilityContrastTest`.
- mw303VinylSim memory: render the editor with `createComponentSnapshot` →
  `PNGImageFormat::writeImageToStream` at min/default/max size so a multimodal agent
  can inspect it. mw140 ORCHESTRATOR §3.9 makes screenshots mandatory evidence for visual work.

### 8.4 Realtime-safety tests

- mwStime: `tests/unit/TestAllocationCounter.h` declares
  `extern std::atomic<std::size_t> mwsTestGlobalAllocationCount;`. Exactly **one** global
  `operator new/new[]/delete` replacement per test binary increments it
  (`tests/plugin/test_alloc_counter.cpp`, and `test_butterworth.cpp` in the core binary).
  Tests snapshot the counter around audio-thread calls and require a delta of 0.
- mw140 `tests/realtime/AllocatorSpyTest.cpp`: the same idea plus a **thread-local sentinel**
  (only counted inside processBlock), and it **also interposes malloc/calloc/realloc/free
  via dlsym**. Reason given: "JUCE's juce::HeapBlock uses std::malloc()/free() directly ...
  NOT operator new", so an `AudioBuffer::setSize` on the audio thread would slip past
  an operator-new-only spy. It renders 10 s of deterministic pink noise (splitmix64 seed 0).
  `LockSpyTest.cpp` is a static grep for `std::mutex|std::lock|CriticalSection|SpinLock|ScopedLock`
  plus a dynamic smoke. There is also an ASan lane and denormal tests (`DenormalCpuTest`).
- mw160: a separate executable `MW160NoAllocTests` that overrides global operator new.
- mwStime adds a `tsan` preset and `[tsan]`-tagged publish/swap stress tests.

### 8.5 Format validators: scripts, commands, binaries

All three mwStime scripts are in `/home/mwoolly/projects/mwStime/tests/plugin/`. CTest
registers them with `LABELS validators`, `SKIP_RETURN_CODE 77`, `TIMEOUT 1200` and
`ENVIRONMENT MWS_BUILD_DIR=${CMAKE_BINARY_DIR}`. Exit codes: 0 pass, 1 fail, 77 skip
(tool unavailable or no artefact). A checksum mismatch is a **hard fail**.

**`run_pluginval.sh`**:
- Pin `PLUGINVAL_VERSION="v1.0.4"`. Linux URL
  `https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip`.
  SHA-256 comes from env `MWS_PLUGINVAL_LINUX_SHA256`; the in-script default is a placeholder.
  CI uses `c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352` (`.github/workflows/ci.yml`).
- Cached at `<repo>/build/validator-tools/pluginval-v1.0.4/pluginval`
  (`mwStime/build/validator-tools/` is currently **empty**).
- Wraps in `xvfb-run -a` when `$DISPLAY` is unset. Finds `*.vst3` dirs under the build dir.
- Command:
  `pluginval --strictness-level 10 --validate-in-process --repeat 3 --randomise [--disabled-tests <ABS path>] --validate <x.vst3>`
- **Tracked waiver** in `pluginval-disabled-tests.txt`: `Plugin state restoration`.
  An automatable `AudioParameterBool` returns the raw un-snapped normalized value from
  `getValue()`, so pluginval's poke/save/restore/compare cycle flags it intermittently.
  The file path must be **absolute**, because pluginval treats a relative value as a
  comma-token list.

**`run_clap_validator.sh`**:
- Pin `0.3.2`, Linux URL
  `https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-ubuntu-18.04.tar.gz`,
  exe inside `binaries/clap-validator`. SHA from `MWS_CLAP_VALIDATOR_LINUX_SHA256`;
  CI uses `1476ed68f5657e76050e0c4f19790c02d819ecc62c35fd465059d21f05169cb1`.
- Command: `clap-validator validate <x.clap>` (xvfb-wrapped if headless).

**`run_lv2_checks.sh`**:
- Runs `lv2lint -s <LV2URI>`, then `lv2_validate <bundle>/*.ttl` (fallback `sord_validate`),
  then a **required** Carla load smoke: `timeout 25 carla-single native lv2 <bundle> <uri>`,
  where rc 124 (killed while still running) counts as PASS. Exports
  `LV2_PATH=$(dirname bundle):$LV2_PATH`. The tools are expected from apt
  (`lv2lint lilv-utils sordi carla xvfb`) and are **not installed on this machine**.
- CI note (ci.yml): on ubuntu-22.04 lv2lint isn't packaged; schema-less `sord_validate`
  flags every standard LV2 extension property as "undefined" (a false positive); and
  `carla-single` fails to start on the runner. So the LV2 check self-skips in CI and
  is a "local box / QA gate".

**`run_reaper_smoke.sh`**: probes `command -v reaper`, `/opt/REAPER/reaper` and
`~/opt/REAPER/reaper`. It **never launches a GUI host** (that "blocks forever").
Without `tests/plugin/reaper_smoke.lua` (never authored) it self-skips. If the script
existed it would run `reaper -new -nosplash -ignoreerrors <script>` and read a PASS/FAIL
status file, because REAPER's exit code is unreliable for batch scripts. **REAPER IS
installed here** (`/usr/local/bin/reaper` → `/opt/REAPER/reaper`), so the probe would
find it. Anyone adding a Lua script must make sure REAPER exits by itself.

**mw160** `scripts/validate_plugin.sh [build_dir] [strictness=5]`: uses `pluginval` on PATH,
else `${BUILD_DIR}/pluginval`, else downloads v1.0.4. Runs
`pluginval --validate-in-process --strictness-level N --validate <vst3>`, xvfb-wrapped if headless.

**mwVnylSim / mw303VinylSim QA** commonly used:
`pluginval --strictness-level 10 --skip-gui-tests --timeout-ms 120000 --validate ~/.vst3/<p>.vst3`
(plus `--random-seed 0x1`, `--repeat 10 --randomise`). Their notes record ~1.4% intermittent
pluginval teardown crashes on Linux, attributed to JUCE/pluginval shutdown races
(`mwVnylSim/docs/review/FINAL_QA_C_realtime_pluginval.md`): "re-run 2-3×".

**Validator binaries already on this machine** (`find /` for `pluginval*` / `clap-validator*`, run 2026-10-02):

| Tool | Path | Notes |
|---|---|---|
| pluginval 1.0.4 (Linux ELF; `--version` → "pluginval - 1.0.4") | `/home/mwoolly/projects/mw160/build/pluginval` | stable location |
| same binary (identical sha256 `d3d342ea…`) | `/home/mwoolly/projects/mw303VinylSim/tools/pluginval` | stable; repo-local staging convention from mw303VinylSim memory |
| same + zip | `/home/mwoolly/projects/mw140/.worktrees/qa-phase6-sweep/tools/pluginval/{pluginval,pluginval_Linux.zip}` | zip sha256 = `c01c49d8…` (matches the CI pin) |
| pluginval 1.0.4 + zip | `/tmp/claude-1000/-home-mwoolly-projects-mw303/<session>/scratchpad/tools/pv104/pluginval` | another session's scratch: ephemeral |
| clap-validator **0.3.2** (Linux) | `/tmp/claude-1000/-home-mwoolly-projects-mw303/dad63690-756f-4b60-b4c2-2627c3ad5533/scratchpad/tools/cv032/clap-validator` | ephemeral; **the only Linux 0.3.2 binary found** |
| clap-validator **0.4.1** (`0.3.2-127-g152b982-ubuntu-22.04` build) | `.../scratchpad/tools/cv041/clap-validator` | ephemeral |
| clap-validator 0.3.2 | `/home/mwoolly/projects/postscript-stuff/postscript-backup/mwStime/build/validator-tools/clap-validator-0.3.2/binaries/clap-validator` | **Mach-O (macOS) binary, unusable here** |
| pluginval.app | same backup dir | macOS, unusable |

Nothing is on `PATH` (`which pluginval clap-validator` finds nothing). `xvfb-run` and
`Xvfb` are installed. **Recommendation**: copy pluginval and the clap-validator binary
into `mw808/build/validator-tools/` (or `mw808/tools/`, gitignored) at bring-up, and
pin download URL + sha256 in the scripts as mwStime does.

Fresh lesson from the concurrent mw303 session's scratch log (`cv041-mwstime.log`):
clap-validator 0.4.1 against mwStime gave 44 tests, 27 passed, 7 failed, 1 warning
and 9 skipped. The failures were the four `param-fuzz-*` tests ("Timed out after 45
seconds") and `state-reproducibility-{basic,binary,buffered}` ("After reloading the
state, these parameter values changed without a rescan request"). The warning was
`state-invalid-random` ("The plugin loaded random bytes successfully"). Implications
for mw808: processing must stay cheap under fuzzed parameters; `setStateInformation`
must restore exactly what was saved, and parameter listeners must never write other
parameters; garbage state should be rejected. Its schema-loaded `lv2_validate` run on
mwStime's TTL also reported "Property `<…:Program>` has no label" errors from JUCE's
LV2 exporter (`lv2v.log`). These are **observed in another session's scratch files and UNVERIFIED by me**.

### 8.6 Headless Linux

All GUI-touching validator steps are `xvfb-run -a`-wrapped when `$DISPLAY` is unset.
The UI goldens use the software renderer. mwStime's Standalone "host-smoke" row runs
the JUCE-free engine directly with no window and no transport, so headless CI never
opens a window.

### 8.7 Process lessons from user memory

- Verify "build clean / zero warnings" from a **fresh build dir or worktree**. Incremental
  builds hide warnings (mw303VinylSim-2 memory `feedback_verify_build_clean_on_fresh_worktree.md`;
  it caught `juce::Font::getStringWidthFloat` deprecation warnings that only showed up on a clean build).
- Sub-agent sandboxes may be denied binary execution (pluginval/ctest). The orchestrator
  pre-stages logs, WAVs and PNGs under a dated dir and passes the path to agents
  (`feedback_agent_sandbox_binary_exec.md`).
- Catch2 `TEST_CASE` names and any `.ps1`/`.nsi` files must be **ASCII-only**: no `§`,
  em-dash or arrows. Windows ctest round-trips the names through a codepage and
  the filter stops matching (`mw140/memory/feedback_windows_ctest_ascii_names.md`).
  Harmless on Linux, but keep names ASCII for portability.
- Implementer and reviewer must be separate agents. TDD is mandatory in the mw140 pattern.

---

## 9. UI conventions

| Project | Base canvas | Resize | Rendering | Fonts | Palette |
|---|---|---|---|---|---|
| mwStime | 1000 × 380 | fixed aspect, 0.6×–2.0×, `setResizable(true, false)` + one explicit `ResizableCornerComponent`; scale persisted in state `uiState/scaleFactor`; hamburger menu offers 75/100/150/200 % | **100 % procedural vector** (`juce::Graphics`/`Path`), static layers cached per (size, model) into a `juce::Image`, dynamic layers drawn on top; budget < 2 ms full repaint; **no OpenGL** ("Linux driver variance"); **no image assets** | stock sans-serif only; LCD uses a code-defined 5×7 bitmap font (`LcdFont.h`) drawn as rounded-rect "pixels" | per-model `FaceplateSpec` struct (chassis, legend, accent, lcdBack/Ink/Glow hex values); geometry never forks per model |
| mw140 | 720 × 360 | 0.8×–2.0×, snap {0.8, 1.0, 1.25, 1.5, 2.0}, fixed aspect, custom constrainer | vector + small PNG textures (`assets/patina/micrograin.png`, `assets/textures/brushgrain.png`, generated by `tools/gen_textures.py`), knob/meter images cached per scale in `HashMap<float, Image>`, 2× supersampled | **D-DIN** (SIL OFL) embedded via `juce_add_binary_data` (`mw140_BinaryData`) and loaded with `Typeface::createSystemTypefaceFor` | `mw140Colours` namespace, 24 named roles with hex values (VISUAL_SPEC §4) |
| mw160 | n/a | n/a | vector, `src/gui/{CustomLookAndFeel,Fonts,Geometry,LedMeter,Palette,Screws}.h` | Inter + JetBrains Mono embedded (`FontBinaryData`) | `Palette.h` |
| ssl4k-2 | 960 × 730 | 1/3×–2×, locked aspect, responsive breakpoints | vector; meters/analyser at 45–60 fps fed by lock-free SPSC taps | n/a | `mw4000Colours`, `mw4000LookAndFeel` |

Shared patterns:
- The LookAndFeel derives from `juce::LookAndFeel_V4` and overrides `drawRotarySlider`,
  `drawLinearSlider`, `drawButtonBackground`/`drawButtonText`.
- Region geometry is defined at the base canvas size (`FaceplateGeometry.h` / VISUAL_SPEC §3)
  and scaled in `resized()` with `scaledRegion(region, w, h)`. Resize math lives in a
  header-only `EditorResize.h` (`kAspectRatio`, `kMinScale`, `kMaxScale`,
  `configureConstrainer(c)`) so it is unit-testable headlessly (`test_resize.cpp`).
- Display models are pure C++ (mwStime `LcdPageModel`), so what the panel shows is
  unit-testable without a GUI.
- Accessibility: all controls focusable, tooltips show hardware units, screen-reader
  names set. mw140 has a contrast test and colour-blind simulations.
- mw140 VISUAL_SPEC §11 lists animation **anti-patterns** (no idle animation). §12 handles
  per-monitor scale via `Desktop::getDisplays()`. Faceplates show the `mwAudio` mark and
  product name in lowercase `mw`.
- No project uses WebView UIs. mwStime ADR-005 rejected it for "Linux WebView fragility, host-window quirks".

---

## 10. State and preset conventions

**mwStime** (`plugin/src/state/*`):
- Parameters live in an APVTS (`juce::AudioProcessorValueTreeState`, root id `"PARAMETERS"`).
  IDs are `inline constexpr const char*` in `namespace paramid` ("host-visible and
  serialized — never rename"). **Ranges are fixed supersets and never remapped at
  runtime**, because VST3 hosts cache parameter info; clamping happens in the engine.
  Choice indices match the core enums index-for-index.
- A `Parameters` bridge caches `std::atomic<float>*` (from `getRawParameterValue`) at
  construction. `makeSnapshot()` is plain relaxed atomic loads into a trivially-copyable
  POD `ParamSnapshot` that the audio thread uses once per block.
- Non-parameter state is a separate `juce::ValueTree` ("mwsState") carrying `stateVersion`
  (`kStateVersion = 1`), UI state (`scaleFactor`, page) and so on. Serialization root
  `"mwStime"` wraps the APVTS tree, the state tree and an optional binary blob child.
- `Migrations.cpp`: an explicit chain `if (fromVersion < 1) tree = migrateV0ToV1(tree);`.
  A **future** version falls back to defaults and sets `unknownVersionFallback=true` for
  the UI. `readPluginState` never throws and returns `valid=false` on garbage. Defaults are
  filled by `ensureDefaults` without overwriting existing values.
- `getStateInformation` never does heavy work on the message thread: blobs are
  pre-encoded and only memcpy'd ("host-autosave reality").
- Factory presets are **C++ data** (`FactoryPresets.cpp`, name → `ParamSnapshot`) exposed
  through the host program API. Program 0 is "Default"; `setCurrentProgram` ignores
  out-of-range indices ("some hosts probe with bad indices") and applies presets
  atomically. Presets are cross-checked against the golden `cases.json` in a test so
  they cannot drift.

**mw140** (`docs/PRESET_SPEC.md`):
- JSON preset files `Presets/Factory/NN_snake_name.mw140preset.json`, embedded with
  `juce_add_binary_data(mw140FactoryPresets HEADER_NAME mw140FactoryPresets.h NAMESPACE mw140FactoryPresetsData ...)`.
  The `MW140_FACTORY_FROM_DISK` option reads them from disk for authoring.
- Schema: `schema_version`, `mw140_version` (SemVer), `name`, `category`
  (a fixed enum of 7), `author` ("mwAudio"), `description`, `tags`, `historical_reference`,
  `notes`, `audio_demo`, `created`, `parameters{param_id: value}`. Choices are stored as
  label strings and bools as JSON booleans. UTF-8, LF, 2-space indent, fixed key order.
- Two orthogonal versions: `schema_version` (shape) and `ParameterID{id, 1}` (meaning).
  Migrations are a list of `{from, to, transform(juce::var&)}`. Unknown fields are
  ignored, missing params get defaults, and out-of-range floats are clamped and logged.
- User presets: `~/.config/mwAudio/mw140/Presets/<Sanitised>.mw140preset.json`.

**ssl4k-2 / mw160**: XML presets via a `PresetManager`, in the legacy
`mw Audio/MW4000/Presets` paths with migration to `mwAudio/mw4000/Presets` (ssl4k-2
`source/preset/PresetManager.cpp`).

---

## 11. Realtime-safety patterns in processBlock

From mwStime `PluginProcessor.cpp`, `Published.h` and `architecture.md` §4:
- `juce::ScopedNoDenormals` at the top of `processBlock`.
- Clear unused output channels, then take one `ParamSnapshot` per block (atomic loads only).
- **Never** allocate, lock, do file IO or touch a ValueTree on the audio thread. Scratch
  buffers are sized in `prepareToPlay` (`setSize(..., avoidReallocating=false)` there only).
- Cross-thread buffers use `Published<T>`, an RCU over `std::shared_ptr<const T>`. The
  audio thread `acquire()`s once per block and `retire()`s into a lock-free SPSC
  **graveyard FIFO**. The message thread drains it on a timer, so destructors never run
  on the audio thread. The header is JUCE-free so TSan tests can cover it.
- `std::atomic<SomeStruct>` must be lock-free on Linux/GCC x86-64. A 16-byte struct is
  not `is_always_lock_free` there (without `-mcx16`), unlike macOS/arm64; this broke the
  Linux build (mwStime backlog `058`). The fix: keep atomically-published structs ≤ 8 bytes
  and add `static_assert(sizeof(T) <= 8)` alongside `static_assert(std::atomic<T>::is_always_lock_free)`.
- Mode/model switches cross-fade over one block, rendering both paths into preallocated
  scratch. Parameter listeners that change latency run on the message thread and call
  `setLatencySamples`.
- Transport is read once per block via `getPlayHead()->getPosition()` (`getIsPlaying`,
  `getPpqPosition`, `getBpm`, `getTimeSignature`) into a plain struct, so the engine
  never sees JUCE types. This is directly reusable for the mw808 sequencer's host sync.
- Audio → UI meters and scopes go through lock-free FIFOs (`juce::AbstractFifo` / SPSC)
  that a UI timer polls (ssl4k-2 and mwStime).
- mw140: −200 dBFS denormal-guard noise injection, a silence-detection gate after 5 s
  below −120 dBFS to save CPU, and oversampling factor via a compile definition.
- ssl4k (DPF): FTZ/DAZ, snapshot-at-block-start, per-sample gain smoothing, crossfaded mode switches.

---

## 12. Hard-won Linux / JUCE lessons (collected)

1. **PIC**: static core libs linked into plugin `.so` files need `POSITION_INDEPENDENT_CODE ON` (mwStime core CMake comment).
2. **Lock-free atomics**: keep `std::atomic<struct>` ≤ 8 bytes on GCC x86-64 (mwStime 058).
3. **No curl/webkit** on this box: always `JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`, `NEEDS_CURL FALSE`, `NEEDS_WEB_BROWSER FALSE`.
4. **Headless**: wrap pluginval, clap-validator and Carla in `xvfb-run -a` when `$DISPLAY` is unset.
5. **pluginval `--disabled-tests` needs an absolute path.** `AudioParameterBool` + "Plugin state restoration" is a known flake (mwStime waiver; mwStime backlog 048c).
6. **clap-juce-extensions** release tag 0.26.0 does not build against JUCE 8. Pin commit `51a9359…`.
7. **LV2**: JUCE's LV2 exporter is the least mature. Validation tooling isn't on this box or on ubuntu-22.04 CI. Schema-less `sord_validate` gives false positives. Keep LV2 cheap to drop (mwStime ADR-002).
8. **REAPER on Linux**: touch the bundle dir after replacing a `.so` so REAPER rescans (mw90 CMake). Never launch the REAPER GUI from scripts. REAPER caches plugin info in `~/.config/REAPER/reaper-vstplugins64.ini` / `reaper-clap-linux-x86_64.ini`. **INFERRED**: delete the mw808 line or rescan after an identity change.
9. **Binary size**: RelWithDebInfo plugins are ~120 MB. Install Release, stripped with `--strip-unneeded` (mwVnylSim).
10. **pluginval teardown flakes** on Linux at about 1–2 %: re-run before filing (mwVnylSim QA).
11. **Verify clean builds from a fresh dir**; incremental builds hide warnings.
12. **No OpenGL / WebView** for UIs (mwStime ADR-005).
13. No X11-embedding, XEmbed or Wayland-specific notes were found in any sibling doc (**UNVERIFIED** whether the projects hit such issues). mw90's Cairo/X11 view is custom iPlug2/VST3 code and does not transfer to JUCE.

---

## 13. Recommended skeleton for mw808

### 13.1 Identity (style-guide compliant, rename-friendly)

```cmake
# Top of /home/mwoolly/projects/mw808/CMakeLists.txt. The product name lives here only.
set(MW_PRODUCT_NAME "mw808")        # display / PRODUCT_NAME (final "cool name" can replace this)
set(MW_PRODUCT_ID   "mw808")        # lowercase → bundle/CLAP id, preset dir
set(MW_PLUGIN_CODE  "mw80")         # style guide: mw + 2 chars (unused by mw14/mw16/mw40/mwVn/MwS1/m12F)
# COMPANY_NAME "mwAudio", PLUGIN_MANUFACTURER_CODE mwAu,
# BUNDLE_ID / CLAP_ID "com.mwaudio.${MW_PRODUCT_ID}",
# LV2URI "https://github.com/mattWoolly/${MW_PRODUCT_NAME}"   (mwStime/mw303 pattern)
```

Instrument flags, following the mw303 instrument pattern plus drum-machine categories:
`IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT TRUE` (only if the sequencer
emits MIDI, otherwise FALSE), `IS_MIDI_EFFECT FALSE`, `EDITOR_WANTS_KEYBOARD_FOCUS FALSE`,
`VST3_CATEGORIES Instrument Drum`, `CLAP_FEATURES instrument drum-machine stereo`.
**INFERRED**: "Instrument|Drum" and the CLAP feature string `drum-machine` come from the
VST3/CLAP specs and were not verified against local files. Add
`JUCE_DISPLAY_SPLASH_SCREEN=0`, `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0` and
`JUCE_VST3_CAN_REPLACE_VST2=0`. Classes: `mw808Processor`, `mw808Editor`,
`mw808LookAndFeel`, `mw808Colours`. Core namespace: neutral (e.g. `mw808::dsp`), with
no brand names anywhere.

### 13.2 Layout (mwStime shape)

```
mw808/
├── CMakeLists.txt  CMakePresets.json  VERSION  LICENSE (AGPLv3, © mwAudio)  README.md
├── .gitignore (copy mwStime's)  .clang-format  .markdownlint.jsonc
├── cmake/FetchJUCE.cmake (8.0.13)  FetchClapExtensions.cmake (51a9359…)  FetchCatch2.cmake (v3.7.1)
├── libs/mw808-core/            # JUCE-free C++20 static lib, PIC ON, -ffp-contract=off, no -ffast-math
│   ├── include/mw808/{voices,seq,fx,core}/*.h
│   └── src/...
├── plugin/
│   ├── CMakeLists.txt          # juce_add_plugin + clap_juce_extensions_plugin
│   ├── src/{mw808Processor,mw808Editor}.{h,cpp}  ParameterIDs.h  Published.h (copy RCU pattern)
│   │   └── state/{Parameters,StateTree,Migrations,FactoryPresets}.{h,cpp}
│   └── ui/{mw808LookAndFeel,mw808Colours,Faceplate,FaceplateGeometry,EditorResize}.h/.cpp …
├── tools/
│   ├── mw808-render/           # JUCE-free CLI: render a pattern/case to WAV (golden driver)
│   ├── gen_golden_inputs/  golden_compare/
│   ├── bless_goldens.sh  bless_ui_goldens.sh  check_license_headers.sh  install-user.sh
├── tests/
│   ├── unit/ (+ TestAllocationCounter.h)   golden/{cases.json,blessed/}   plugin/   ui/
│   └── plugin/{run_pluginval.sh,run_clap_validator.sh,run_lv2_checks.sh,pluginval-disabled-tests.txt}
├── docs/{BUILDING.md,MANUAL.md,design/,research/}  plan/{backlog/,decisions/}
├── research/                   # this research round (01–06-*.md)
└── scripts/{branding-audit.sh,forbidden_tokens.txt}   # mw140 pattern
```

### 13.3 Build, presets and install

- Presets: `linux-release` (Ninja, **Release**: gets `-O3` + `-flto` from the JUCE helper
  targets; used for installs and benchmarks), `linux-default` (Ninja, RelWithDebInfo for
  dev), `tsan` (plugin OFF) and an `asan` preset (mw140 has an ASan lane).
- Options: `MW808_BUILD_PLUGIN` (ON), `MW808_BUILD_TESTS` (ON). Fetch JUCE and the CLAP
  shim only when the plugin is built.
- Formats on Linux: `VST3 LV2 Standalone` + CLAP via the shim. Keep the format list
  in a variable so LV2 can be dropped with one line.
- Offline bring-up: copy the 8.0.13 JUCE, `51a9359` clap-juce-extensions and v3.7.1
  Catch2 trees listed in §3.2 into a cache mw808 owns and pass `FETCHCONTENT_SOURCE_DIR_*`.
- Install: add `tools/install-user.sh` (none exists in the line; this follows the
  ssl4k-2 user-install pattern). It should copy the Release artefacts to `~/.vst3/mw808.vst3`
  (rm -rf first), `~/.clap/mw808.clap` and `~/.lv2/mw808.lv2`, run
  `strip --strip-unneeded` on the `.so` files, and `touch` the bundle dirs. Optionally
  add a CMake `install_user` custom target that calls it. Do not use
  `COPY_PLUGIN_AFTER_BUILD` on Linux (the line only uses it on macOS).

### 13.4 Tests to copy

- JUCE-free `mw808_tests` (Catch2 + `catch_discover_tests`): per-voice unit and property
  tests, plus sequencer timing tests (sample-accurate step placement against a scripted
  transport, through the plain transport struct).
- Golden renders: `cases.json` → one CTest per case, `golden_compare` with diagnostics,
  `BLESS_REASON`-gated blessing, `MANIFEST.json`. **Reference platform = this Linux
  x86_64 box** (g++ 13).
- JUCE-linked `mw808_plugin_tests` console app: parameter layout, state round-trip,
  migrations (including the future-version fallback), preset ↔ golden cross-check, resize
  math, and an allocation counter around `processBlock`. Copy mw140's malloc interposition
  as well as operator new, because JUCE's `HeapBlock` uses malloc.
- `ui_screenshot` console app: software renderer → PNG goldens (LABEL `ui-golden`), plus
  PNG dumps at min/default/max scale for visual review.
- Validators (LABEL `validators`, `SKIP_RETURN_CODE 77`): copy the mwStime scripts, but
  set the Linux SHA-256 pins to the verified values (`c01c49d8…` for pluginval_Linux.zip
  v1.0.4, `1476ed68…` for clap-validator 0.3.2 ubuntu-18.04.tar.gz) and seed
  `build/validator-tools/` from `/home/mwoolly/projects/mw160/build/pluginval`.
  Consider also running clap-validator 0.4.x, which is stricter: state reproducibility
  and fuzz timeouts.
- `license_headers` and `branding_audit` CTests. Use `find` or `git init` so they don't pass vacuously.
- Labels: `golden`, `ui-golden`, `validators`, `host-smoke`, `tsan`, `qa-adversarial`.
  Test names start with the label and are ASCII-only.

### 13.5 Code conventions

- SPDX two-line header on every source, CMake and shell file:
  `// SPDX-License-Identifier: AGPL-3.0-or-later` /
  `// mw808 — classic analog drum machine emulation. Copyright (C) 2026 mwAudio.`
- APVTS with fixed superset ranges, `ParameterID{id, 1}`, snake_case IDs in `ParameterIDs.h`.
  Cache `std::atomic<float>*` into a per-block POD snapshot.
- A non-parameter ValueTree with `stateVersion` + an explicit migration chain. The
  sequencer patterns and song data belong here, not in APVTS.
- Factory kits/patterns as data, embedded via `juce_add_binary_data` (mw140 JSON
  pattern) or C++ tables (mwStime). User presets go in `~/.config/mwAudio/mw808/Presets/`.
- In processBlock: `ScopedNoDenormals`, no alloc/lock/IO/ValueTree, `Published<T>` RCU +
  graveyard for any cross-thread data (pattern edits from UI to audio), lock-free FIFOs
  to the UI for step position and meters, and a transport snapshot per block.
- UI: procedural vector faceplate with static layers cached per scale, fixed aspect,
  0.6×–2.0×, scale persisted in state, a `mw808Colours` palette table, an embedded OFL
  font if one is wanted (mw140/mw160 pattern), no OpenGL or WebView, and lowercase `mw`
  wordmark plus the `mwAudio` mark on the faceplate.
- README in the mwStime order with an Authenticity statement and the mw140 "not
  affiliated" branding paragraph. Docs go in `docs/design/*` with ADRs in `plan/decisions/`.

### 13.6 Open decisions to surface to the user

1. Licence: AGPL-3.0-or-later, which JUCE 8 requires and mwStime uses, versus the style
   guide's GPLv3. Copyright holder `mwAudio` either way.
2. Final product name and plugin code. The style-guide pattern gives `mw808` / `mw80`.
   mw303 shows the line also accepts invented names (`mw12F`).
3. Whether LV2 ships at v1. mwStime did ship it; mw140 and ssl4k-2 did not.
4. `git init` the project now so the git-based header/branding checks work. Per the
   user's global rules, commit only when asked.

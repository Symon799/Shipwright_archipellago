# Local quality validation

Milestones 1 and 2 provide local component tests and targeted, opt-in game scenarios.
Fast/integration validation needs no ROM, running game, credentials or public
Archipelago server. Game validation uses existing local archives and disposable saves.

## Quick Start (Windows)

Double-click these files in the repository's `tests/windows/` directory. Their consoles stay open:

| Command | Purpose |
| --- | --- |
| `check-env.bat` | Discover compiler, CMake, Python and dependency headers |
| `check-fast.bat` | Whitespace checks, C++ Debug **and** Release, Python regressions |
| `check-integration.bat` | AP session/localhost transport, pack placements and release packaging |
| `prepare-e2e.bat` | Enable and compile the opt-in Debug game scenarios after normal build setup |
| `check-e2e.bat` | Launch isolated Windows game scenarios using the local Debug build |
| `check-all.bat` | Run selected suites; game remains NOT_RUN unless explicitly enabled |

Non-interactive equivalents, from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check.ps1 -Suite Env
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check.ps1 -Suite Fast
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check.ps1 -Suite Integration
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check.ps1 -Suite All
```

Python is the cross-platform runner:

```text
python scripts/check.py env
python scripts/check.py fast --configuration Both --jobs 2
python scripts/check.py integration
python scripts/check.py all
```

The `check-*` runners do not download dependencies or configure the game's root
CMake project. Fast/integration compile only the standalone project in `tests/`.
`prepare-e2e` uses the normal game build system; its existing vcpkg automation
can update dependency metadata. No new test framework is downloaded.

## Prerequisites

- Python 3.10+, CMake/CTest 3.26+, Git, and a C++20 compiler.
- On Windows, Visual Studio 2022 with the C++ workload and Windows SDK.
- Existing nlohmann-json headers and **the ImGui headers used by this game**.
  The latter are normally in `build/x64-v143/_deps/imgui-src` after preparing
  the game's dependencies. This suite never fetches them silently.

Component tests need no GPU or game assets. The Python packaging/export tests
can run independently without a C++ compiler. AP transport additionally uses the
existing `apclientpp`, `wswrap`, `asio`, `websocketpp` and `valijson` directories
under the selected game's `_deps` cache. Missing dependencies are BLOCKED;
there is no implicit network bootstrap.

If discovery fails, supply paths explicitly:

```powershell
python scripts/check.py fast --json-include-dir C:/Users/Symon/vcpkg/installed/x64-windows-static/include --imgui-include-dir build/x64-v143/_deps/imgui-src
```

Environment alternatives are `SOH_JSON_INCLUDE_DIR` and
`SOH_IMGUI_INCLUDE_DIR`. The JSON directory must contain
`nlohmann/json.hpp`; the ImGui directory must contain `imgui.h`.
`--game-build-dir` changes only the dependency search location.
For Linux/macOS use `--generator Ninja` or the default local generator.
Use a separate `--build-dir` when changing generators/toolchains.

## Reports and Failure Reproduction

Every completed invocation produces a unique directory under
`build/validation-reports/` with `summary.json`, command logs and JUnit XML.
Compiled tests live in `build/validation/`. Both directories are Git-ignored.
Reports contain the commit, dirty state, detected environment, exact commands,
durations, per-suite outcomes and known limitations. They contain local paths;
review logs before sharing them. An interrupted directory without a summary
is not a completed validation.

| Exit code | Meaning |
| --- | --- |
| `0` | Every **selected** suite passed |
| `1` | A check failed, a report was malformed or required tests were absent |
| `2` | Incomplete: BLOCKED, SKIPPED or NOT_RUN; overall PARTIAL |

`check-all` returning 2 without an explicitly enabled game run is expected, not a release approval.
`check-fast` and `check-integration` should return 0. A missing header blocks
C++ validation rather than producing a false success. Zero discovered tests
is an error. No retry masks failures. Configure/build/test commands have
timeouts; the runner terminates only its own timed-out command tree.

For an individual C++ regression, use the command recorded in the log, or:

```powershell
ctest --test-dir build/validation -C Debug -R viewport_ownership --output-on-failure --no-tests=error
ctest --test-dir build/validation -C Release -R calibration --output-on-failure --no-tests=error
python -m unittest discover -s scripts/tests -p test_release_packaging.py -v
```

The `checks_active` test intentionally exits with failure. CTest's `WILL_FAIL`
inverts **only this negative control** to prove assertions remain active in
Release. Other tests use always-on `CHECK`, not `assert` erased by `NDEBUG`.
CTest normalizes this result in its JUnit report; this is not an ignored failure.

## Coverage and Limits

The tests execute the production headers/helpers directly. They do not copy
functions out of source files, duplicate the randomizer graph, or replace the
algorithm under test with a mock.

| Family | Verified | Not proved by this suite |
| --- | --- | --- |
| Calibration | Production anchor mapping and fallback edge cases | Accurate player position on every artwork |
| Display modes | Four modes, legacy migration, invalid values | Actual pause/rendering transitions |
| Check totals | Checked precedence, reachable/unreachable aggregation; AP visibility decision policy | Complete reachability graph and seed option matrix |
| ImGui viewport | Geometry proxy never acquires backend ownership | Real GPU/ImGui shutdown integration |
| Win32 shutdown | Late focus/resize/destruction messages after detachment | Whole game teardown with an AP client and active worker |
| Reference exporter | Synthetic IA4/resource headers/PNG output and errors | Official ROM contents |
| Packaging | Flat Windows ZIP; Linux/macOS filenames; config/pack/o2r; missing/corrupt/unsafe/private inputs; texture freshness | AppImage/DMG installation, native platform execution or all possible secrets |
| AP session | Replay suppression, ordered delivery, saved index, seed/slot identity, version parsing, scouts/reset/exclusions | Actual item granting or shops in Zelda |
| AP transport | Real apclientpp/schema/callbacks against localhost: data package, scouts, foreign-game location names, checks, replay and changed port | TLS/compression and all production Archipelago callbacks |
| Pack placements | Production numeric/placement parser, legacy default sizes, multiple placements, invalid/nonfinite/overflow data | Every editor feature and arbitrary mod format |
| Runner | Reliable codes/reports and isolation fixtures that never import personal saves/config | A game feature merely because the runner itself works |

Windows baseline measured on this machine: 6 CTest cases per configuration
(5 behavioral tests + 1 assertion guard), 12 Python fast tests, 9 packaging
tests. The 6 Python runner checks are infrastructure checks, not extra tracker
behaviors. Integration adds 3 CTest cases per configuration, including the real
network probe. Incremental fast validation takes a few seconds; packaging about
one second, transport a few seconds. First compilation varies. Baseline reports are
local artefacts, not committed snapshots.

Git whitespace checks currently exclude submodules. No submodule is modified.
GitHub Actions integration is deliberately reserved for milestone 3.

## Build and Asset Freshness

`compile-debug.bat`, `compile-release.bat` and `Generate build.bat` at the repository root regenerate
`soh.o2r` even when an old archive exists. Release packaging requires an explicit
`--soh-otr`; the local release wrapper additionally compares the packed pause
texture with its source PNG. A mismatch fails before replacing the last good
release archive. Windows defaults to 2 build jobs; override `BUILD_JOBS` or
`VCTOOLS_VERSION` in the environment if needed. VS Code's Windows tasks now use
the same `build/x64-v143` directory and toolset as the build wrappers.

Packaging fixtures are entirely synthetic. No ROM or personal save is copied
by validation. The release command is distinct from validation and still uses
the pinned resource pack URL/hash already configured in the repository.

## Opt-In Game Scenarios (Windows)

After the normal Debug build/dependencies are prepared, run
`tests/windows/prepare-e2e.bat`, then `tests/windows/check-e2e.bat`. The local
build has already been prepared during implementation. Preparation enables
`SOH_ENABLE_QUALITY_SCENARIOS=ON` **only in the Debug scenario source**;
normal launches do nothing special, and Release builds refuse this mode.

Terminal equivalents:

```powershell
python scripts/check.py e2e --game-exe x64/Debug/soh.exe
python scripts/check.py e2e --game-exe x64/Debug/soh.exe --e2e-scenarios pause-3d
python scripts/check.py all --game-exe x64/Debug/soh.exe
```

`SOH_E2E_EXE` is an alternative to `--game-exe`. `--game-assets` optionally
selects the directory containing existing `soh.o2r` and original `oot.o2r`.
No raw ROM extraction, asset download, seed generation or server login occurs.

The runner creates a fresh private runtime per scenario inside the report
directory, stages its own EXE, references the two existing archives using local
hard links, and generates its own PNG/pack/config. It never imports your mods,
save files, UUID or connection profiles. Hard links require the same volume and
are read-only **by usage**, not by changing the original file's attributes.
Hashes are checked after execution; archive links are removed from completed
runs before reports can be shared, including failures and handled interruptions.
If the runner is forcibly killed, remove its archive links before sharing that
runtime. No result means failure, not success.

The game refuses validation without an explicit disposable-runtime manifest,
when its EXE is outside that runtime, or when app-directory resolution escapes
the runtime. It uses the standard load boundary/hooks, real Start/shoulder
pause input, the same map toggle action as normal controls, and actual rendered
marker/popup rectangles. It never forces pause states to make assertions pass.

Scenarios:

- `pause-3d` / `pause-2d`: threaded save/index roundtrip, Config profile
  persistence, disposable scene load, vanilla pause/navigation/close, map open,
  multicheck hover/enter, blocked gameplay movement, close/reopen, exit with
  the map and popup still open, then persistence in a fresh process.
- `exit-loading-3d` / `exit-loading-2d`: close with an outstanding real texture
  request or decoded result; normal shutdown must join/clean up safely.
- `pack-invalid`: actual archive/metadata loading reports an invalid root
  without crashing.

On the implementation machine, the complete `all --game-exe x64/Debug/soh.exe`
run passed in about 94 seconds (warm component builds, seven game processes,
plus five isolation-guard probes). This is not a performance budget for every GPU.

Success requires both normal process exit and the game's result written **after
shutdown**. Phase progress, final assertions, logs and resource hashes remain
in the report. Startup/phase/whole-process timeouts are bounded. GUI windows
will appear during these opt-in tests; do not drive the controller/mouse during
the automated run. This is not a screenshot-based visual certification.

## Remaining Targeted Coverage

A Light Arrow packet alone does not prove delivery or shop behavior in Zelda.
Those need a purpose-made AP save/seed fixture, not your ordinary progression.
The current game scenarios deliberately use a disposable vanilla save. Full
Rando reachability across inventories/options, production AP callbacks with a
real loaded seed, keyboard/device binding paths, links/hint costs, and reopening
the tracker from an already-open vanilla pause still need dedicated scenarios.
No public server is necessary for these: extend the localhost fixture and the
opt-in game driver. Never mock the progression algorithm being verified.

Keep manual checks for real controller rebinding, 3D readability/projection,
animation/audio and native window-close gestures. Linux/macOS graphics E2E and
the CI quality gate are not implemented here; CI integration is milestone 3.

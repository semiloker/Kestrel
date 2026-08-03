# Kestrel — Architecture

This document describes Kestrel's internal architecture for developers working on the codebase. For user-facing information, see [README.md](README.md); for data-handling behavior, see [PRIVACY.md](PRIVACY.md).

## 1. Overview

Kestrel is a native Windows desktop overlay/HUD application written in C++20. It reports battery health, CPU/GPU load, power draw, temperatures, memory, and network/disk activity, and captures real per-application frame-time statistics (FPS, 1%/0.1% lows, CPU/GPU-bound classification) using **ETW (Event Tracing for Windows)** — the same mechanism Intel PresentMon uses.

- **Platform**: Win32 only (Direct2D/DirectWrite, DXGI, PDH, ETW, WMI, Task Scheduler COM API). No cross-platform abstraction layer exists or is intended.
- **Toolchain**: C++20, built with CMake (≥3.20) using MinGW-w64 GCC, targeting both MSYS2 `MINGW64` and `UCRT64` environments.
- **Output**: a single statically-linked `kestrel.exe`. No installer is required, no kernel driver is installed, no background service runs.
- **Process model**: single process, single window class with two `HWND`s (settings window + overlay). No IPC framework.
- **Data handling**: no telemetry or analytics. The only outbound network activity is user-initiated GitHub release checks (see [PRIVACY.md](PRIVACY.md)).

## 2. Repository layout

| Path | Contents |
|---|---|
| `CMakeLists.txt` / `makefile` | Build definitions. CMake is the primary/maintained build; the makefile is a simpler alternate path. |
| `app.manifest` | Win32 application manifest — execution level (`asInvoker`) and DPI awareness. |
| `app.rc` | Resource script (icon, version info), compiled via `windres`. |
| `src/*.cpp`, `include/*.h` | All application source. **Flat** — no subdirectories. One module per concern. |
| `tests/*.cpp` | Catch2 unit tests, built only when `KESTREL_BUILD_TESTS=ON`. |
| `tools/` | `check_style.py` (CI style linter), `make_logo.py` (reproducible icon/logo generation). |
| `packaging/` | Distribution manifests: `winget/`, `scoop/`, `chocolatey/`, `msix/`. |
| `.github/workflows/` | `ci.yml` (build/test/lint matrix), `release.yml` (tag-gated signed release pipeline). |
| `assets/` | Icon/logo source assets consumed by the resource compiler and packaging. |
| `lang/` | `example.lang` — i18n string-table template. |

### Naming convention

Source files use a flat, non-hierarchical layout with a `_bi` suffix on most module names (e.g. `resource_usage_bi.cpp` / `resource_usage_bi.h`, `capture_manager_bi.cpp`). Module boundaries are enforced by this naming and by header interfaces, not by directory structure — there is no `src/ui/`, `src/core/`, etc. When adding a new module, follow the existing `<name>_bi.{h,cpp}` pattern and place both files at the top level of `include/`/`src/`.

## 3. Application lifecycle

`WinMain` (`src/main.cpp`) is the sole entry point. Bootstrap sequence:

1. **Logging init** — `log_bi::init()` opens `%APPDATA%\Kestrel\kestrel.log`.
2. **Update handoff wait** — if launched with `--wait-for-pid <pid>` (i.e. as the replacement process during a self-update), block until the previous process exits.
3. **CLI verb dispatch** — `autostart_bi::handleCommandLine()` handles `--install-task` / `--remove-task` (Task Scheduler autostart registration) and exits; `handleSettingsCommandLine()` handles `--export-settings` / `--import-settings` and exits.
4. **Elevation relaunch check** (`src/main.cpp` ~line 1992) — if the process is not elevated (`autostart_bi::isElevated()`) but a Task Scheduler task pointing at this executable exists (`autostart_bi::taskPointsToThisExe()`), relaunch through that task (`autostart_bi::runTask()`) to gain admin rights without a UAC prompt, then exit the unelevated instance. A `--from-task` / `--autostart` flag prevents relaunch loops.
5. **Single-instance enforcement** — a named mutex (`Local\Kestrel_SingleInstance`, see `APP_MUTEX_NAME` in `include/app_identity_bi.h`) with logic to detect and replace crashed instances and to hand off from an old version during self-update.
6. **Window construction** — build `win_bi`, `Register()` the window class (`win_bi::WndProc` as the procedure), `Create()` the window.
7. **Message loop** — classic Win32 `GetMessage`/`DispatchMessage` loop.
8. **Shutdown** — on exit, optionally relaunch a staged update binary (apply/rollback), then `log_bi::shutdown()`.

`win_bi` (`include/main.h`, `src/main.cpp`) is the central controller: it owns every subsystem, implements `WndProc`, and drives the per-tick refresh cycle via `UpdateOverlayHud()`, called from timer ticks and various UI event handlers.

## 4. Component map

### UI / rendering
| Module | Responsibility |
|---|---|
| `init_d2d1_bi`, `init_dwrite_bi` | Direct2D and DirectWrite device/resource setup. |
| `draw_batteryinfo_bi` | Draws the main settings window's tabs (Battery Info, Settings, Capture, Appearance, About). |
| `overlay_bi` | The always-on-top, click-through, per-pixel-alpha HUD overlay window — a separate `HWND` (`APP_OVERLAY_CLASS`) from the settings window, in the same process. |
| `hud_bi` | Metric/series data model backing the overlay (rolling graph history, per-metric availability/visibility flags; see `hud_metric_bi metrics[HUD_M_COUNT]` in `include/hud_bi.h`). |
| `dpi_bi` | Per-monitor DPI scaling helper. |
| `tray_icon_bi` | System tray icon lifecycle, including recovery after Explorer restarts. |
| `hotkey_manager_bi` | Global hotkey registration. |
| `i18n_bi` | String-table localization backed by `lang/*.lang`. |

### Data / capture engine
| Module | Responsibility |
|---|---|
| `etw_bi` | Owns the ETW real-time trace session (DXGI/D3D9/DXGKRNL providers), a dedicated consumer thread, and a frame ring buffer drained by the UI thread. |
| `frame_stats_bi` | Statistical aggregation (1%/0.1% low FPS via percentile of raw frame intervals). |
| `capture_bi`, `capture_manager_bi` | Recording a capture session: accumulates frames and power/battery samples, finalizes asynchronously on a background thread, writes CSVs, maintains run history (`index.csv`). |
| `resource_usage_bi` | CPU/GPU/RAM/disk/network sampling via PDH performance counters, WMI, `GlobalMemoryStatusEx`, DXGI adapter enumeration; runs its own background sampler thread. |
| `gpu_sensor_bi`, `mahm_sensor_bi` | GPU temperature/adapter querying (WDDM) and optional CPU temperature read from MSI Afterburner's shared-memory block, falling back to ACPI thermal zone. |
| `BatteryInfo` | Battery chemistry/capacity/rate/cycle-count via `IOCTL_BATTERY_*` and WMI `BatteryCycleCount`. |

### Platform services
| Module | Responsibility |
|---|---|
| `paths_bi` | Resolves `%APPDATA%\Kestrel\` as the single data directory; UTF-8/UTF-16 conversion helpers. |
| `settings_bi` | INI-based settings persistence (`settings.ini`), per-application profiles keyed by executable name, JSON import/export. |
| `logger_bi` | File logger (`kestrel.log`). |
| `autostart_bi` | "Start with Windows" integration via a `Run` registry value (normal) or a Task Scheduler task at `TASK_RUNLEVEL_HIGHEST` (elevated autostart). |
| `update_bi` | Self-updater: checks GitHub Releases over HTTPS (WinHTTP), downloads, verifies, stages, and atomically swaps the running executable, with rollback. |
| `csv_bi.h` | CSV writer/reader used by capture export. |

### Cross-cutting
- `interfaces_bi.h` defines:
  - `Result<T, E = std::string>` — a `std::variant`-based result type used throughout instead of C++ exceptions, plus a `Result<void, E>` specialization.
  - Abstract interfaces `IResourceUsage`, `IBatteryInfo`, `IETWTrace`, `IOverlay`, implemented by the corresponding concrete `_bi` classes. These exist primarily to support unit testing rather than as a general plugin mechanism — `win_bi` mostly depends on the concrete types directly.

### Inter-component communication
There is no formal IPC. The overlay and the settings window are two `HWND`s in one process, coordinated through `win_bi`. The only cross-process interactions are: the Task Scheduler-mediated elevation relaunch (§3, step 4), the self-update parent/child handoff via `--wait-for-pid` (§6), and reading MSI Afterburner's shared-memory block (read-only, no driver installed by Kestrel).

## 5. Concurrency model

Kestrel offloads slow OS/hardware queries to background threads so the UI thread and the timer-driven render loop never block:

| Thread | Created in | Purpose |
|---|---|---|
| ETW consumer | `etw_bi.cpp` (`traceThread`, via `CreateThread`) | Runs `ProcessTrace` for the ETW session and computes per-process present intervals/FPS into a frame ring buffer. |
| Resource sampler | `resource_usage_bi.cpp` (`samplerEntry`, via `CreateThread`) | Periodic PDH/WMI/DXGI sampling at a configurable interval. |
| Capture finalizer | `capture_manager_bi.cpp` (`finalizeThread`, via `CreateThread`) | Finalizes a completed capture session (aggregation, CSV write) without blocking the UI. |
| Update worker | `update_bi.cpp` (`threadEntry`, via `CreateThread`) | Runs the check/download/verify sequence for self-update, signaling the UI thread via a custom `WM_APP_UPDATE` message. |

All are raw Win32 `CreateThread` (not `std::thread`), synchronized with `CRITICAL_SECTION` and/or `std::atomic` around the shared state each thread hands back to the UI thread. When adding a new background data source, follow the existing pattern: own thread, minimal critical section around the handoff buffer, and either a poll from `UpdateOverlayHud()` or a custom window message to signal the UI thread — do not block `WndProc` on a live OS query.

## 6. Build & CI/CD

- **Build**: CMake ≥3.20, C++20, GCC via MinGW-w64. Both `MINGW64` and `UCRT64` MSYS2 environments are built in CI. A precompiled header (`include/pch.h.in`) speeds up compilation. `app.rc` is compiled separately via `windres`. The binary statically links `libgcc`/`libstdc++`/`libwinpthread` for a dependency-free portable executable.
- **Tests**: optional `kestrel_tests` target (`KESTREL_BUILD_TESTS` CMake option) using Catch2, fetched via `FetchContent` at a pinned commit. It links a subset of production `.cpp` files directly (e.g. `hud_bi.cpp`, `frame_stats_bi.cpp`).
- **CI** (`.github/workflows/ci.yml`), on `windows-latest`, matrixed over MinGW64/UCRT64:
  - `version-consistency` — cross-checks the version number across `CMakeLists.txt`, `app_identity_bi.h`, and `app.manifest`.
  - `build` — configure/build, verify the artifact exists and its `ProductName` resource, build and run unit tests, full clean rebuild, optional Authenticode signing (secret-gated).
  - `warnings` — rebuild with `-Werror`.
  - `style` — `tools/check_style.py`.
  - `static-analysis` — `cppcheck`.
  - `spelling` — `codespell`.
  - `logo` — regenerates the logo/icon via `tools/make_logo.py` and diffs byte-for-byte against committed assets.
  - `docs` — validates README links resolve and contains no emoji.
- **Release** (`.github/workflows/release.yml`), triggered on `v*` tags: validates tag/commit/source-version consistency, calls `ci.yml` as a gate, then re-verifies the built artifact (size, `ProductName`, `FileVersion`, signature), computes its SHA-256, builds the release ZIP, re-expands and re-hashes it to confirm round-trip integrity, and publishes via `softprops/action-gh-release`.
- **Packaging** (`packaging/`): Winget, Scoop, Chocolatey manifests, and an MSIX package (`packaging/msix/AppxManifest.xml`) for Microsoft Store submission, requesting `rescap:runFullTrust` (required because ETW/WMI access is incompatible with a sandboxed UWP container).

## 7. Security model

Kestrel runs unprivileged by default and only elevates when the user opts in.

- **No forced elevation**: `app.manifest` requests `asInvoker`, so normal launches never trigger a UAC prompt. Without elevation, ETW frame-timing metrics are simply unavailable (shown as unset in the HUD) rather than the app failing.
- **Opt-in elevation via Task Scheduler**: instead of prompting for UAC on every launch, Kestrel registers a Task Scheduler task at `TASK_RUNLEVEL_HIGHEST` (`autostart_bi::runTask`, COM `ITaskService`/`IPrincipal` API in `src/autostart_bi.cpp`). Windows launches this task elevated at logon; `WinMain` detects the unelevated case and relaunches through the task (§3, step 4).
- **UIPI workaround**: `ChangeWindowMessageFilterEx` (`src/main.cpp:839`) explicitly allows the `TaskbarCreated` message through User Interface Privilege Isolation, so the tray icon survives an Explorer restart while running elevated.
- **Self-update verification chain** (`src/update_bi.cpp`):
  - Update checks/downloads only happen on explicit user action, never automatically.
  - Downloaded binaries are checked against a SHA-256 hash (via `bcrypt.h`/CNG) using a constant-time-style comparison, and against an Authenticode signature (`WinVerifyTrust`) when the binary is signed.
  - PE header sanity checks (`IMAGE_DOS_SIGNATURE`/`IMAGE_NT_SIGNATURE`) run before a downloaded file is trusted.
  - The staged file is re-verified immediately before it replaces the running executable; a backup of the previous version is retained for rollback if the replacement process fails to launch.
  - A "verified guard" handle (deny write/delete) is held open across the process handoff to prevent a TOCTOU swap of the verified file.
- **Data directory integrity**: `paths_bi` checks that `%APPDATA%\Kestrel\` is not a reparse point before trusting it, mitigating symlink/junction attacks.
- **Supply chain**: optional Authenticode signing in CI, independently re-verified at release-publish time; deterministic release artifacts (hash computed, then the ZIP is re-expanded and re-hashed to confirm it round-trips); tag-to-commit-to-source-version consistency enforced before a release is published.
- **No sandboxing**: Kestrel is a full-trust Win32 app by design — ETW/WMI access is incompatible with AppContainer/UWP sandboxing (the MSIX manifest explicitly requests `runFullTrust`). The security posture instead relies on minimal default privilege, explicit/opt-in elevation, no DLL injection into other processes (deliberately avoided — see README's anti-cheat/ban-risk rationale), no kernel driver, and no background network activity.

## 8. Testing

Catch2 unit tests live in `tests/*.cpp`, built only when `KESTREL_BUILD_TESTS=ON`:

- `test_frame_stats.cpp`, `test_frame_stats_boundaries.cpp` — FPS/percentile statistics and edge cases.
- `test_capture_boundaries.cpp` — capture session edge cases (frame/time limits).
- `test_csv.cpp`, `test_csv_boundaries.cpp` — CSV read/write.
- `test_hud_series.cpp` — HUD rolling-series data model.

Only pure-logic modules (e.g. `hud_bi.cpp`, `frame_stats_bi.cpp`) are linked into the test binary; UI/Win32/ETW code is not unit tested directly since it depends on live OS/hardware state. CI's structural checks — version consistency, style, static analysis, reproducible-asset diffing, and release artifact round-trip verification — serve as additional integration-level guardrails beyond the Catch2 suite.

## 9. Extending Kestrel

- **Adding a new metric/sensor** (e.g. a new hardware counter): add a field to the relevant `Info` struct in `interfaces_bi.h` (e.g. `IResourceUsage::CpuInfo`), populate it in the corresponding concrete class (`resource_usage_bi`, `gpu_sensor_bi`, etc.), add a `hud_metric_id_bi` entry and wire it into `hud_bi` for overlay display, then surface any new user-facing toggle in `settings_bi` and `draw_batteryinfo_bi`.
- **Adding a settings field**: extend `settings_bi`'s INI schema and the JSON import/export path together, so `--export-settings`/`--import-settings` stay consistent with the UI.
- **Testability**: prefer coding against the `IResourceUsage`/`IBatteryInfo`/`IETWTrace`/`IOverlay` interfaces where a new component needs to be exercised without live hardware, following the existing pattern rather than adding hard dependencies on concrete `_bi` classes.
- **Coding conventions**: follow `.clang-tidy` and `tools/check_style.py` for linting; use `Result<T, E>` rather than exceptions for fallible operations, consistent with the rest of the codebase.
- **No plugin architecture**: Kestrel is intentionally a monolith. Extension points are limited to i18n string tables (`lang/*.lang`) and per-application settings profiles — there is no dynamic module-loading system to hook into.

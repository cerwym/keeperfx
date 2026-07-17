# KeeperFX - CMake Build Guide (Vita + Preset Reference)

## Purpose

This document explains:
- How to build KeeperFX for PlayStation Vita.
- Which flags are set by each Vita preset.
- What every configure preset and build preset in `CMakePresets.json` does.

This is the source-of-truth companion to runtime/deployment docs in `docs/platforms/vita/vita-setup.md`.

## Prerequisites

- CMake 3.20+
- Ninja
- Docker (recommended workflow in this repository)
- Vita SDK environment (`VITASDK`) when running native SDK builds

Repository-specific build wrappers already exist as VS Code tasks:
- `Build Vita Release`
- `Build Vita RelDebug`
- `Build Vita Debug`

## Vita Build Process

### Recommended (Docker task flow)

Use one of the existing tasks:
- `Build Vita Release`
- `Build Vita RelDebug`
- `Build Vita Debug`

These tasks run:

```bash
# Release
docker compose -f build/docker/compose.yml run --rm vitasdk bash -c 'cmake --preset vita-release && cmake --build --preset vita-release'

# RelWithDebInfo
docker compose -f build/docker/compose.yml run --rm vitasdk bash -c 'cmake --preset vita-reldebug && cmake --build --preset vita-reldebug'

# Debug
docker compose -f build/docker/compose.yml run --rm vitasdk bash -c 'cmake --preset vita-debug && cmake --build --preset vita-debug'
```

### Direct CMake flow

```bash
cmake --preset vita-debug
cmake --build --preset vita-debug
```

Swap `vita-debug` with `vita-reldebug` or `vita-release` as needed.

## Vita Preset Quick Reference

| Preset | Build type | Key cache variables | Intended use |
|---|---|---|---|
| `vita-release` | `Release` | `PLATFORM_VITA=ON` | Shipping/performance package build. |
| `vita-reldebug` | `RelWithDebInfo` | `PLATFORM_VITA=ON` | Crash analysis and profiling with symbols, near-release optimization. |
| `vita-debug` | `Debug` | `PLATFORM_VITA=ON`, `VITA_PERF_LOG=ON`, `VITA_SCE_DIAG=ON` | Deep diagnostics and subsystem tracing during development. |

## Vita Debug Flags And Purpose

When you run `cmake --preset vita-debug`, these preset cache variables are set:

| Variable | Value | Purpose |
|---|---|---|
| `CMAKE_TOOLCHAIN_FILE` | `$env{VITASDK}/share/vita.toolchain.cmake` | Uses Vita cross-toolchain. |
| `CMAKE_BUILD_TYPE` | `Debug` | Enables debug configuration path. |
| `PLATFORM_VITA` | `ON` | Selects Vita platform code and dependencies. |
| `VITA_PERF_LOG` | `ON` | Enables startup/load timing logs (compile definition `VITA_PERF_LOG=1` on `keeperfx`). |
| `VITA_SCE_DIAG` | `ON` | Enables SCE diagnostic hooks (compile definition `VITA_SCE_DIAG=1`). |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | `on` | Generates `compile_commands.json` for tooling. |

Additional compile/link behavior triggered by Vita CMake logic:

| Item | Value | Purpose |
|---|---|---|
| `PLATFORM_VITA=1` | compile definition | Enables Vita-specific code paths. |
| `DEBUG=1` (Debug config) | compile definition | Project-wide debug macro. |
| `-mcpu=cortex-a9 -mfpu=neon -ffast-math` | compile options | Vita CPU/FPU tuning. |
| `-Wl,--wrap,malloc/free/calloc/realloc/memalign` | link options | Routes allocations through vitaGL allocator wrappers. |

## Build Artifacts

For Vita presets, configure output is under:
- `out/build/<configure-preset>/`

Build presets `vita-release`, `vita-reldebug`, and `vita-debug` target:
- `keeperfx.vpk`

## Configure Preset Table (All Presets)

Source: `CMakePresets.json` (`configurePresets`).

| Preset | Hidden | Inherits | Key cache variables | Description / purpose |
|---|---|---|---|---|
| `base` | Yes | None | `CMAKE_INSTALL_PREFIX`, `CMAKE_TOOLCHAIN_FILE=vcpkg` | Generic Ninja base config. |
| `Debug` | Yes | None | `CMAKE_BUILD_TYPE=Debug` | Shared debug build-type preset. |
| `Release` | Yes | None | `CMAKE_BUILD_TYPE=RelWithDebInfo` | Shared release-like build-type preset with symbols. |
| `MSVC-x64-Base` | Yes | `base` | `cl.exe`, `VCPKG_TARGET_TRIPLET=x64-windows-static`, static MSVC runtime | Base for native Windows x64 MSVC static builds. |
| `x64-windows-static-debug` | No | `MSVC-x64-Base`, `Debug` | `KEEPERFX_TRACY=ON`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Native Windows x64 MSVC debug (static). |
| `x64-windows-static-release` | No | `MSVC-x64-Base`, `Release` | `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Native Windows x64 MSVC release (static). |
| `x64-windows-static-debug-asan` | No | `MSVC-x64-Base`, `Debug` | `KEEPERFX_SANITIZERS=ON` | Native Windows x64 MSVC debug + AddressSanitizer. |
| `x64-windows-static-reldebug-asan` | No | `MSVC-x64-Base`, `Release` | `KEEPERFX_SANITIZERS=ON` | Native Windows x64 MSVC RelWithDebInfo + AddressSanitizer. |
| `linux-base` | Yes | None | `CMAKE_INSTALL_PREFIX` | Linux native base without vcpkg toolchain override. |
| `cross-base` | Yes | None | `CMAKE_INSTALL_PREFIX` | Cross-compile base for Docker/Linux flows. |
| `windows-x86-release` | No | `cross-base`, `Release` | `CMAKE_TOOLCHAIN_FILE=build/cmake/mingw32.cmake`, `CMAKE_BUILD_TYPE=Release`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Docker/Linux cross-compile to Windows x86 release (release CI). |
| `windows-x86-debug` | No | `cross-base`, `Debug` | `CMAKE_TOOLCHAIN_FILE=build/cmake/mingw32.cmake`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Docker/Linux cross-compile to Windows x86 debug. |
| `windows-x64-release` | No | `cross-base`, `Release` | `CMAKE_TOOLCHAIN_FILE=build/cmake/mingw64.cmake`, `CMAKE_BUILD_TYPE=Release`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Docker/Linux cross-compile to Windows x64 release. |
| `linux-x64-release` | No | `linux-base`, `Release` | `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Linux native x64 release build. |
| `wasm-release` | No | `base` | Emscripten toolchain, `CMAKE_BUILD_TYPE=Release`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | WebAssembly build via Emscripten. |
| `3ds-release` | No | `base` | 3DS toolchain, `PLATFORM_3DS=ON`, `CMAKE_BUILD_TYPE=Release`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Nintendo 3DS release build. |
| `vita-release` | No | `base` | Vita toolchain, `PLATFORM_VITA=ON`, `CMAKE_BUILD_TYPE=Release`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | PlayStation Vita release build. |
| `vita-reldebug` | No | `base` | Vita toolchain, `PLATFORM_VITA=ON`, `CMAKE_BUILD_TYPE=RelWithDebInfo`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Vita release-like build with symbols for crash analysis. |
| `vita-debug` | No | `base` | Vita toolchain, `PLATFORM_VITA=ON`, `VITA_PERF_LOG=ON`, `VITA_SCE_DIAG=ON`, `CMAKE_BUILD_TYPE=Debug`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Vita diagnostics/perf-debug build. |
| `switch-release` | No | `base` | Switch toolchain, `PLATFORM_SWITCH=ON`, `CMAKE_BUILD_TYPE=Release`, `CMAKE_EXPORT_COMPILE_COMMANDS=on` | Nintendo Switch release build. |

## Build Preset Table (All Presets)

Source: `CMakePresets.json` (`buildPresets`).

| Preset | Hidden | Inherits | Configure preset | Targets | Description / purpose |
|---|---|---|---|---|---|
| `Base` | Yes | None | `Debug` | None | Base build behavior (`jobs=8`, `verbose=true`). |
| `StandardLog` | Yes | `Base` | Inherited | `keeperfx` | Standard-log executable target base. |
| `x64-windows-static-debug` | No | `StandardLog` | `x64-windows-static-debug` | Inherited | Native Windows x64 MSVC debug build. |
| `x64-windows-static-release` | No | `StandardLog` | `x64-windows-static-release` | Inherited | Native Windows x64 MSVC release build. |
| `x64-windows-static-debug-asan` | No | `StandardLog` | `x64-windows-static-debug-asan` | Inherited | Native Windows x64 MSVC debug + ASan build. |
| `x64-windows-static-reldebug-asan` | No | `StandardLog` | `x64-windows-static-reldebug-asan` | Inherited | Native Windows x64 MSVC RelWithDebInfo + ASan build. |
| `windows-x86-release` | No | `StandardLog` | `windows-x86-release` | Inherited | Windows x86 release build target (release CI). |
| `windows-x86-debug` | No | `StandardLog` | `windows-x86-debug` | Inherited | Windows x86 debug build target. |
| `windows-x64-release` | No | `StandardLog` | `windows-x64-release` | Inherited | Windows x64 release build target. |
| `linux-x64-release` | No | `StandardLog` | `linux-x64-release` | Inherited | Linux x64 release build target. |
| `wasm-release` | No | `StandardLog` | `wasm-release` | Inherited | WebAssembly release build target. |
| `3ds-release` | No | `StandardLog` | `3ds-release` | Inherited | Nintendo 3DS release build target. |
| `vita-release` | No | `StandardLog` | `vita-release` | `keeperfx.vpk` | Vita release package build. |
| `vita-reldebug` | No | `StandardLog` | `vita-reldebug` | `keeperfx.vpk` | Vita RelWithDebInfo package build. |
| `vita-debug` | No | `StandardLog` | `vita-debug` | `keeperfx.vpk` | Vita debug package build with diagnostics enabled at configure stage. |
| `switch-release` | No | `StandardLog` | `switch-release` | Inherited | Nintendo Switch release build target. |

## Notes For Vita Contributors

- Vita builds in this repo are preset-driven; avoid ad-hoc cache changes unless testing a specific issue.
- `vita-debug` is intended for instrumentation and diagnostics, not performance evaluation.
- `vita-reldebug` is generally preferred when you need symbols without full debug overhead.
- `vita-release` is the closest to shipping behavior.

## See Also

- `docs/platforms/vita/vita-setup.md` (device install and runtime setup)
- `docs/platforms/vita/vita-subsystem-reference.md` (graphics/audio subsystem details)
- `docs/platforms/vita/vita-sysmodule-analysis.md` (SCE module and decoder analysis)

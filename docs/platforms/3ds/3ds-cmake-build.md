# KeeperFX - CMake Build Guide (Nintendo 3DS)

## Purpose

This document explains how to build KeeperFX for Nintendo 3DS and which CMake presets are available for different build configurations.

## Prerequisites

- CMake 3.20+
- Ninja
- Docker (recommended workflow in this repository)
- 3DS SDK environment (`DEVKITPRO` and `DEVKITARM`) when running native SDK builds

Repository-specific build wrappers already exist as VS Code tasks:
- `Build 3DS Release`
- `Build 3DS RelDebug`
- `Build 3DS Debug`

## 3DS Build Process

### Recommended (Docker task flow)

Use one of the existing tasks:
- `Build 3DS Release`
- `Build 3DS RelDebug`
- `Build 3DS Debug`

These tasks run:

```bash
# Release
docker compose -f docker/compose.yml run --rm 3ds-sdk bash -c 'cmake --preset 3ds-release && cmake --build --preset 3ds-release'

# RelWithDebInfo
docker compose -f docker/compose.yml run --rm 3ds-sdk bash -c 'cmake --preset 3ds-reldebug && cmake --build --preset 3ds-reldebug'

# Debug
docker compose -f docker/compose.yml run --rm 3ds-sdk bash -c 'cmake --preset 3ds-debug && cmake --build --preset 3ds-debug'
```

### Direct CMake flow

```bash
cmake --preset 3ds-release
cmake --build --preset 3ds-release
```

Swap `3ds-release` with `3ds-reldebug` or `3ds-debug` as needed.

## 3DS Preset Quick Reference

| Preset | Build type | Key cache variables | Intended use |
|---|---|---|---|
| `3ds-release` | `Release` | `PLATFORM_3DS=ON` | Shipping/performance package build. |
| `3ds-reldebug` | `RelWithDebInfo` | `PLATFORM_3DS=ON` | Crash analysis and profiling with symbols, near-release optimization. |
| `3ds-debug` | `Debug` | `PLATFORM_3DS=ON`, `THREADED_AI=ON` | Deep diagnostics and subsystem tracing during development. |

## 3DS Debug Flags and Purpose

When you run `cmake --preset 3ds-debug`, these preset cache variables are set:

| Variable | Value | Purpose |
|---|---|---|
| `CMAKE_TOOLCHAIN_FILE` | `$env{DEVKITPRO}/devkitARM/arm-none-eabi.cmake` or similar | Uses 3DS cross-toolchain. |
| `CMAKE_BUILD_TYPE` | `Debug` | Enables debug configuration path. |
| `PLATFORM_3DS` | `ON` | Selects 3DS platform code and dependencies. |
| `THREADED_AI` | `ON` | Enables threaded AI for comprehensive diagnostics. |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | `on` | Generates `compile_commands.json` for tooling. |

Additional compile/link behavior triggered by 3DS CMake logic:

| Item | Value | Purpose |
|---|---|---|
| `PLATFORM_3DS=1` | compile definition | Enables 3DS-specific code paths. |
| `DEBUG=1` (Debug config) | compile definition | Project-wide debug macro. |
| `-mcpu=cortex-a9 -mfpu=neon` | compile options | 3DS CPU/FPU tuning. |

## Build Artifacts

For 3DS presets, configure output is under:
- `out/build/<configure-preset>/`

Build presets `3ds-release`, `3ds-reldebug`, and `3ds-debug` target:
- `keeperfx.3dsx` — 3DSX executable (executable format for 3DS homebrew)

## Output Artifact Formats

### 3DSX (.3dsx)
- Standard 3DS homebrew executable format
- Contains ARM ELF code, icon, and meta information
- Deployable via FBI (File Browser Cia) or DevKit tools
- Maximum file size typically ~512 MB

## Platform-Specific Build Tips

### ARM Architecture Considerations
- 3DS uses ARMv6k architecture (older than Vita's ARMv7)
- Compiler optimization flags are tuned for ARM Cortex-A9 performance
- Fast math is enabled by default for graphics operations

### Memory Constraints
- The 3DS has limited system RAM (~128 MB available for apps)
- Debug builds may be memory-constrained during linking
- Use `RelWithDebInfo` for production profiling to balance symbol retention and code size

### Homebrew Deployment
- `.3dsx` files are directly installable on homebrewed 3DS consoles
- FBI or HBMenu can load executables from SD card `3ds/` directory
- No special signing or encryption required (unlike retail releases)

### Debug Symbol Preservation
- `3ds-reldebug` retains full DWARF debug symbols for crash analysis
- Symbol size can be 2-3x the release build size
- Useful for post-mortem debugging when device access is limited

## Notes for 3DS Contributors

- 3DS builds in this repo are preset-driven; avoid ad-hoc cache changes unless testing a specific issue.
- `3ds-debug` is intended for instrumentation and diagnostics, not performance evaluation.
- `3ds-reldebug` is generally preferred when you need symbols without full debug overhead.
- `3ds-release` is the closest to shipping behavior.

## See Also

- `docs/platforms/3ds/DEBUGGING_SETUP.md` (GDB debugging and crash dump analysis)

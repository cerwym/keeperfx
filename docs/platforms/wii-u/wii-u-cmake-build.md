# KeeperFX - CMake Build Guide (Nintendo Wii U)

## Purpose

This document explains how to build KeeperFX for Nintendo Wii U and which CMake presets are available for different build configurations.

## Prerequisites

- CMake 3.20+
- Ninja
- Docker (recommended workflow in this repository)
- Wii U SDK environment (`WIIU_SDK` or similar) when running native SDK builds

Repository-specific build wrappers already exist as VS Code tasks:
- `Build Wii U Release`
- `Build Wii U RelDebug`
- `Build Wii U Debug`

## Wii U Build Process

### Recommended (Docker task flow)

Use one of the existing tasks:
- `Build Wii U Release`
- `Build Wii U RelDebug`
- `Build Wii U Debug`

These tasks run:

```bash
# Release
docker compose -f docker/compose.yml run --rm wii-u-sdk bash -c 'cmake --preset wii-u-release && cmake --build --preset wii-u-release'

# RelWithDebInfo
docker compose -f docker/compose.yml run --rm wii-u-sdk bash -c 'cmake --preset wii-u-reldebug && cmake --build --preset wii-u-reldebug'

# Debug
docker compose -f docker/compose.yml run --rm wii-u-sdk bash -c 'cmake --preset wii-u-debug && cmake --build --preset wii-u-debug'
```

### Direct CMake flow

```bash
cmake --preset wii-u-release
cmake --build --preset wii-u-release
```

Swap `wii-u-release` with `wii-u-reldebug` or `wii-u-debug` as needed.

## Wii U Preset Quick Reference

| Preset | Build type | Key cache variables | Intended use |
|---|---|---|---|
| `wii-u-release` | `Release` | `PLATFORM_WIIU=ON` | Shipping/performance package build. |
| `wii-u-reldebug` | `RelWithDebInfo` | `PLATFORM_WIIU=ON` | Crash analysis and profiling with symbols, near-release optimization. |
| `wii-u-debug` | `Debug` | `PLATFORM_WIIU=ON`, `WIIU_DEBUG_LOGGING=ON` | Deep diagnostics and subsystem tracing during development. |

## Wii U Debug Flags and Purpose

When you run `cmake --preset wii-u-debug`, these preset cache variables are set:

| Variable | Value | Purpose |
|---|---|---|
| `CMAKE_TOOLCHAIN_FILE` | Wii U cross-toolchain path | Uses Wii U cross-compiler configuration. |
| `CMAKE_BUILD_TYPE` | `Debug` | Enables debug configuration path. |
| `PLATFORM_WIIU` | `ON` | Selects Wii U platform code and dependencies. |
| `WIIU_DEBUG_LOGGING` | `ON` | Enables debug logging output to Wii U console. |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | `on` | Generates `compile_commands.json` for tooling. |

Additional compile/link behavior triggered by Wii U CMake logic:

| Item | Value | Purpose |
|---|---|---|
| `PLATFORM_WIIU=1` | compile definition | Enables Wii U-specific code paths. |
| `DEBUG=1` (Debug config) | compile definition | Project-wide debug macro. |
| `-mcpu=powerpc -mwii` | compile options | Wii U PowerPC CPU tuning. |

## Build Artifacts

For Wii U presets, configure output is under:
- `out/build/<configure-preset>/`

Build presets `wii-u-release`, `wii-u-reldebug`, and `wii-u-debug` target:
- `keeperfx.rpx` — Retail executable format (Wii U standard)
- `keeperfx.elf` — Executable and Linking Format (intermediate, contains debug symbols)

## Output Artifact Formats

### RPX (.rpx)
- Standard Wii U executable format for retail and homebrew
- Contains compiled PowerPC code and resources
- Deployable to Wii U SD card and loadable via Homebrew Channel or custom tools
- Stripped or unstripped depending on build type

### ELF (.elf)
- Intermediate format with full debug symbols retained
- Used during development for symbol lookup and crash analysis
- Not directly executable on Wii U hardware
- Useful for comparing builds or detailed analysis

## Platform-Specific Build Tips

### PowerPC Architecture Considerations
- Wii U uses PowerPC architecture (different from Vita's ARMv7)
- Compiler optimization flags are tuned for PowerPC performance
- Endianness and CPU-specific instructions differ from ARM

### Memory Constraints
- Wii U has limited available system RAM
- Debug builds may be memory-constrained during linking
- Use `RelWithDebInfo` for production profiling to balance symbol retention and code size

### Homebrew Deployment
- `.rpx` files are deployable to Wii U Homebrew Channel
- Typically loaded from SD card (`/wiiu/apps/keeperfx/`)
- No special signing or encryption required for homebrew

### Debug Symbol Preservation
- `wii-u-reldebug` retains full debug symbols for crash analysis
- Symbol size can be significantly larger than release build
- Useful for post-mortem debugging when device access is limited

## Notes for Wii U Contributors

- Wii U builds in this repo are preset-driven; avoid ad-hoc cache changes unless testing a specific issue.
- `wii-u-debug` is intended for instrumentation and diagnostics, not performance evaluation.
- `wii-u-reldebug` is generally preferred when you need symbols without full debug overhead.
- `wii-u-release` is the closest to shipping behavior.

## See Also

- `docs/platforms/wii-u/DEBUGGING_SETUP.md` (GDB debugging and crash dump analysis)

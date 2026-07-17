# KeeperFX - CMake Build Guide (Nintendo Switch)

## Purpose

This document explains how to build KeeperFX for Nintendo Switch and which CMake presets are available for different build configurations.

## Prerequisites

- CMake 3.20+
- Ninja
- Docker (recommended workflow in this repository)
- Switch SDK environment (`DEVKITPRO` and `DEVKITARM`) when running native SDK builds

Repository-specific build wrappers already exist as VS Code tasks:
- `Build Switch Release`
- `Build Switch RelDebug`
- `Build Switch Debug`

## Switch Build Process

### Recommended (Docker task flow)

Use one of the existing tasks:
- `Build Switch Release`
- `Build Switch RelDebug`
- `Build Switch Debug`

These tasks run:

```bash
# Release
docker compose -f build/docker/compose.yml run --rm switch-sdk bash -c 'cmake --preset switch-release && cmake --build --preset switch-release'

# RelWithDebInfo
docker compose -f build/docker/compose.yml run --rm switch-sdk bash -c 'cmake --preset switch-reldebug && cmake --build --preset switch-reldebug'

# Debug
docker compose -f build/docker/compose.yml run --rm switch-sdk bash -c 'cmake --preset switch-debug && cmake --build --preset switch-debug'
```

### Direct CMake flow

```bash
cmake --preset switch-release
cmake --build --preset switch-release
```

Swap `switch-release` with `switch-reldebug` or `switch-debug` as needed.

## Switch Preset Quick Reference

| Preset | Build type | Key cache variables | Intended use |
|---|---|---|---|
| `switch-release` | `Release` | `PLATFORM_SWITCH=ON` | Shipping/performance package build. |
| `switch-reldebug` | `RelWithDebInfo` | `PLATFORM_SWITCH=ON` | Crash analysis and profiling with symbols, near-release optimization. |
| `switch-debug` | `Debug` | `PLATFORM_SWITCH=ON`, `SWITCH_DEBUG_LOGGING=ON` | Deep diagnostics and subsystem tracing during development. |

## Switch Debug Flags and Purpose

When you run `cmake --preset switch-debug`, these preset cache variables are set:

| Variable | Value | Purpose |
|---|---|---|
| `CMAKE_TOOLCHAIN_FILE` | `$env{DEVKITPRO}/cmake/Switch.cmake` | Uses Switch cross-toolchain. |
| `CMAKE_BUILD_TYPE` | `Debug` | Enables debug configuration path. |
| `PLATFORM_SWITCH` | `ON` | Selects Switch platform code and dependencies. |
| `SWITCH_DEBUG_LOGGING` | `ON` | Enables debug logging output to Switch console. |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | `on` | Generates `compile_commands.json` for tooling. |

Additional compile/link behavior triggered by Switch CMake logic:

| Item | Value | Purpose |
|---|---|---|
| `PLATFORM_SWITCH=1` | compile definition | Enables Switch-specific code paths. |
| `DEBUG=1` (Debug config) | compile definition | Project-wide debug macro. |
| `-march=armv8-a -mtune=cortex-a57` | compile options | Switch ARM CPU tuning. |

## Build Artifacts

For Switch presets, configure output is under:
- `out/build/<configure-preset>/`

Build presets `switch-release`, `switch-reldebug`, and `switch-debug` target:
- `keeperfx.nsp` — Nintendo Submission Package (standard Switch executable format)
- `keeperfx.elf` — Executable and Linking Format (intermediate, contains debug symbols)

## Output Artifact Formats

### NSP (.nsp)
- Standard Nintendo Switch executable format for retail and homebrew
- Contains compiled ARMv8 code, metadata, and resources
- Deployable to Switch via homebrew tools or official mechanisms
- Equivalent to XCI but in package form for digital distribution

### ELF (.elf)
- Intermediate format with full debug symbols retained
- Used during development for symbol lookup and crash analysis
- Not directly executable on Switch hardware
- Useful for comparing builds or detailed analysis

## Platform-Specific Build Tips

### ARMv8 Architecture Considerations
- Switch uses ARMv8-A architecture (64-bit ARM)
- Compiler optimization flags are tuned for ARM Cortex-A57 performance
- More modern architecture than 3DS or Wii U

### Memory Constraints
- Switch has more available RAM than 3DS or Wii U
- Debug builds should be manageable size-wise
- Use `RelWithDebInfo` for production profiling to balance symbol retention and code size

### Homebrew Deployment
- `.nsp` files are deployable to Switch via homebrew installers
- Typically loaded through emuNAND or custom firmware
- No special signing or encryption required for homebrew

### Debug Symbol Preservation
- `switch-reldebug` retains full debug symbols for crash analysis
- Symbol size can be 1-2x the release build size
- Useful for post-mortem debugging when device access is limited

### ARMv8 GDB Debugging
- Switch supports aarch64-linux-gnu-gdb for source-level debugging
- Network debugging protocol available via homebrew tools
- More mature debugging ecosystem than other supported platforms

## Notes for Switch Contributors

- Switch builds in this repo are preset-driven; avoid ad-hoc cache changes unless testing a specific issue.
- `switch-debug` is intended for instrumentation and diagnostics, not performance evaluation.
- `switch-reldebug` is generally preferred when you need symbols without full debug overhead.
- `switch-release` is the closest to shipping behavior.

## See Also

- `docs/platforms/switch/DEBUGGING_SETUP.md` (GDB debugging and file-based logging)

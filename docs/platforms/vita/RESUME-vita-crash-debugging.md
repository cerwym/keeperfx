# Resume: Vita Crash Debugging Infrastructure

> **Branch**: `feature/vita-hardware-acceleration`
> **Date paused**: 2026-03-07
> **Status**: Implementation complete, uncommitted — needs review, commit, and push

---

## What was built

A comprehensive crash debugging toolkit for the PS Vita port, spanning 5 phases:

### Phase 1 — Python Crash Tool (`tools/vita_crash/`)
Replaces the old PowerShell crash script with a cross-platform Python tool that runs in Docker.

| Module | Purpose |
|---|---|
| `__main__.py` | CLI entry — argparse, auto-detects ELF from `out/build/vita-{preset}/keeperfx` |
| `ftp_client.py` | FTP dump discovery/download from Vita (port 1337) |
| `core_parser.py` | `.psp2dmp` binary parser — Module/Thread/Register data classes, NOTE types |
| `symbolicate.py` | DWARF CFI stack walking + heuristic scan + batch `addr2line -fCia` |
| `report.py` | Text + HTML output, Catppuccin Mocha theme, automated `_diagnose()` |
| `docker-entry.sh` | Docker entry point, installs pyelftools to `/tools/pylib` volume |
| `requirements.txt` | `pyelftools>=0.31` |

### Phase 2 — Docker Integration
- `tools/vita-parse-crash.ps1` — Rewritten to invoke Docker with the Python package
- VS Code tasks added: "Parse Local Crash Dump", "Parse Vita Crash Dump"

### Phase 3 — On-Device Crash Handler + Breadcrumbs
- `src/platform/PlatformVita.cpp` — Enhanced crash handler: ARM inline asm register capture, `sceKernelGetThreadInfo`, free memory info, battery %, breadcrumb trail dump, structured `crash.log`
- `src/platform/kfx_breadcrumb.h` — Ring buffer (64 slots), `KFX_BREADCRUMB(label)` macro, guarded by `#if defined(PLATFORM_VITA) && !defined(NDEBUG)`
- `KFX_BREADCRUMB()` calls added to: `game_loop`, `keeper_gameplay_loop`, `LbScreenSwap`, `LbScreenSetup`, `init_level`, `get_inputs` (in `src/main.cpp`, `src/main_game.c`, `src/bflib_video.c`, `src/front_input.c`)

### Phase 4 — Polish & Verification
- Automated diagnosis hints: NULL pointer, stack overflow, OOM, alignment faults
- Multi-thread trace support in report output
- All `#ifdef` guards verified for clean non-Vita builds

### Phase 5C.6 — VS Code GDB Launch Config
- `.vscode/launch.json` — "Vita Remote Debug (GDB)" config: `arm-vita-eabi-gdb`, `miDebuggerServerAddress=${input:vitaGdbIp}:31337`

### Phase 6 — Docker Infrastructure
Renamed the shared Vita SDK Docker image from `keeperfx-build-vitasdk` to `build-vitasdk` for reuse across non-keeperfx Vita projects. Simplified the keeperfx Dockerfile to a single `FROM` line now that a standalone `cerwym/build-vitasdk` repo hosts the full image.

- `.github/workflows/docker-publish.yml` — image name `keeperfx-build-vitasdk` → `build-vitasdk`
- `.github/workflows/ci-homebrew.yml` — 2 container refs updated
- `.github/workflows/release.yml` — `gnuton/vitasdk-docker:latest` → `ghcr.io/${{ github.repository_owner }}/build-vitasdk:latest`
- `docker/compose.yml` — image name updated
- `docker/vitasdk/Dockerfile` — replaced 42-line Dockerfile with 7-line `FROM ghcr.io/cerwym/build-vitasdk:latest`
- `tools/vita-parse-crash.ps1` — image name + comment updated

### Phase 7 — vitaGL Razor GPU Support + Build Fixes
Enabled `HAVE_RAZOR=1` on the vitaGL ExternalProject build so that Razor GPU Capture APIs are compiled in, allowing GPU profiling on-device.

- `CMakeLists.txt` — added `HAVE_RAZOR=1` to vitaGL BUILD_COMMAND and INSTALL_COMMAND; added `SceRazorCapture_stub` to both `target_link_libraries` lines (required by vitaGL's `sceRazorGpuCaptureEnableSalvage`)
- `src/platform/PlatformVita.cpp` — added `#include <psp2/kernel/sysmem.h>` (needed for `SceKernelFreeMemorySizeInfo`); replaced illegal ARM `str pc, [Rn]` with `adr r1, .` + `str r1` workaround (ARM doesn't allow PC as source in store instructions)

---

## Files changed (uncommitted)

**Modified** (19 files):
- `.github/workflows/ci-homebrew.yml`, `.github/workflows/docker-publish.yml`, `.github/workflows/release.yml`
- `.vscode/extensions.json`, `.vscode/launch.json`, `.vscode/tasks.json`
- `CMakeLists.txt`
- `docker/compose.yml`, `docker/vitasdk/Dockerfile`
- `docs/platforms/vita/vita-setup.md`
- `keeperfx.code-workspace`
- `src/bflib_video.c`, `src/front_input.c`, `src/main.cpp`, `src/main_game.c`
- `src/platform/PlatformVita.cpp`, `src/platform/vita_malloc_wrap.c`
- `src/renderer/RendererVita.cpp`
- `tools/vita-parse-crash.ps1`

**Deleted:**
- `docs/coding_style_for_eclipse.xml`

**New** (untracked):
- `docs/platforms/vita/vita-cmake-build.md`
- `src/platform/kfx_breadcrumb.h`
- `tools/vita_crash/` (entire directory — 7 files)

---

## What to do next

1. **Review** all changes: `git diff` and `git diff --stat`
2. **Stage and commit** — suggested split:
   - Commit 1: Phase 6 (Docker infra) — workflows, Dockerfile, compose.yml, vita-parse-crash.ps1 image refs
   - Commit 2: Phase 7 (Razor GPU + build fixes) — `CMakeLists.txt`, `PlatformVita.cpp` (sysmem.h, ARM asm fix)
   - Commit 3: Phase 3 (crash handler + breadcrumbs) — `PlatformVita.cpp` crash handler, `kfx_breadcrumb.h`, breadcrumb call sites
   - Commit 4: Phase 1+2 (Python crash tool + Docker integration) — `tools/vita_crash/`, `vita-parse-crash.ps1`, tasks
   - Commit 5: Phase 5C.6 (GDB launch config) — `launch.json`
   - Commit 6: Housekeeping — `extensions.json`, `keeperfx.code-workspace`, deleted `coding_style_for_eclipse.xml`
   - Or one combined commit if preferred
3. **Push** to `feature/vita-hardware-acceleration`
4. **Test end-to-end**: trigger a crash on Vita → run "Parse Vita Crash Dump" task → verify HTML report

---

## Related repos

The GDB remote debugging stack lives in two sibling repos (see their own RESUME docs):
- `C:\Users\peter\source\repos\vita\kvdb\` — Kernel debugger plugin (`.skprx`)
- `C:\Users\peter\source\repos\vita\vdbtcp\` — TCP bridge plugin (`.suprx`)

The shared Vita SDK Docker image lives in its own repo:
- `C:\Users\peter\source\repos\vita\build-vitasdk\` — [github.com/cerwym/build-vitasdk](https://github.com/cerwym/build-vitasdk)
- Published as `ghcr.io/cerwym/build-vitasdk:latest`
- keeperfx's `docker/vitasdk/Dockerfile` now just does `FROM ghcr.io/cerwym/build-vitasdk:latest`

GitHub issues were filed: 12 on `cerwym/kvdb`, 4 on `cerwym/vdbtcp`.

# KeeperFX

![KeeperFX Logo](/docs/assets/readme-banner.png)

![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen?style=flat-square)
![Release](https://img.shields.io/github/v/release/dkfans/keeperfx?style=flat-square)
![Downloads](https://img.shields.io/github/downloads/dkfans/keeperfx/total?style=flat-square)
[![Discord](https://img.shields.io/discord/480505152806191114?style=flat-square)](https://discord.gg/hE4p7vy2Hb)

[Visit our website](https://keeperfx.net) | [Join our Discord (Keeper Klan)](https://discord.gg/hE4p7vy2Hb)


## Intro
KeeperFX (Dungeon Keeper Fan eXpansion) is an open-source project that aims to fix up, enhance and modernize 
the classic dungeon management game, [Dungeon Keeper](https://en.wikipedia.org/wiki/Dungeon_Keeper).
This project is dedicated to providing an improved and customizable gaming experience while staying true to the spirit of the original game.

KeeperFX is a standalone game but requires a copy of the original game files as proof of ownership.
These files can be automatically copied from your old CDs, or from a digital edition like the ones from EA or GOG.

Originally, KeeperFX started out as a decompilation project, where we took the original game executables and reversed them back into usable code. 
Currently the whole codebase of Dungeon Keeper is remade and all code has been rewritten.


## Features
- Windows 7/10/11 support
- Higher screen resolutions
- Increased FPS, decoupled gfx and game logic
- Improved and modernized controls
- Many bugfixes
- Map, campaign and modding customizability
- Improved AI
- Modern multiplayer protocol
- Additional campaigns, maps, creatures and other content
- ...


## How to play

Installation instructions and a FAQ can be found on the [Github Wiki](https://github.com/dkfans/keeperfx/wiki).

You will need the original Dungeon Keeper files, either from an old CD or from the digital edition available on
[EA](https://www.ea.com/games/dungeon-keeper/dungeon-keeper),
[GOG](https://www.gog.com/game/dungeon_keeper)
or [Steam](https://store.steampowered.com/app/1996630/Dungeon_Keeper_Gold/).


## Development

### Building with CMake + vcpkg

KeeperFX uses CMake with vcpkg for all dependency management. All platforms are built
via Docker-based CI or locally using the same CMake presets.

#### Prerequisites
- CMake 3.20 or later
- Git (with submodule support)
- A C++20-capable compiler:
  - **Windows (native):** Visual Studio 2022 or later (MSVC)
  - **Windows (cross):** MinGW-w64 cross-compiler (inside the Docker image)
  - **Linux:** GCC or Clang
- Docker (optional, for cross-compile — uses the same images as CI)

#### Clone
```bash
git clone --recursive https://github.com/dkfans/keeperfx.git
cd keeperfx
```

#### Build — Windows x64 (MSVC, native)
```bash
# Bootstrap vcpkg (first time only)
.\external\vcpkg\bootstrap-vcpkg.bat -disableMetrics

# Configure & build
cmake --preset x64-windows-static-debug
cmake --build --preset x64-windows-static-debug
```

#### Build — Windows x64 (cross-compile via Docker)
```bash
# Pull the pre-built Docker image
docker pull ghcr.io/cerwym/keeperfx-build-mingw64:latest

# Run the build inside the container
docker run --rm -v $(pwd):/src ghcr.io/cerwym/keeperfx-build-mingw64:latest bash -c "
  external/vcpkg/bootstrap-vcpkg.sh -disableMetrics
  external/vcpkg/vcpkg install --triplet x64-mingw-cross \
    --overlay-triplets build/vcpkg-triplets --x-install-root=vcpkg_installed
  cmake --preset windows-x64-release
  cmake --build --preset windows-x64-release
"
```

#### Build — Linux x64
```bash
docker run --rm -v $(pwd):/src ghcr.io/cerwym/keeperfx-build-linux:latest bash -c "
  external/vcpkg/bootstrap-vcpkg.sh -disableMetrics
  external/vcpkg/vcpkg install --triplet x64-linux-keeperfx \
    --overlay-triplets build/vcpkg-triplets --x-install-root=vcpkg_installed
  cmake --preset linux-x64-release
  cmake --build --preset linux-x64-release
"
```

#### Available CMake presets

| Preset | Compiler | Target |
|--------|----------|--------|
| `x64-windows-static-debug` | MSVC x64 | Windows debug |
| `x64-windows-static-release` | MSVC x64 | Windows release |
| `x64-windows-static-debug-asan` | MSVC x64 | Windows debug + AddressSanitizer |
| `x64-windows-static-reldebug-asan` | MSVC x64 | Windows RelWithDebInfo + AddressSanitizer |
| `windows-x64-release` | MinGW-w64 x64 | Windows x64 release (Docker) |
| `windows-x86-release` | MinGW-w64 x86 | Windows x86 release (Docker) |
| `linux-x64-release` | GCC x64 | Linux release (Docker) |
| `linux-x64-asan` | GCC x64 | Linux + AddressSanitizer |

#### Notes
- vcpkg provides **all** external dependencies (SDL2, FFmpeg, OpenAL, LuaJIT, etc.)
- All libraries are built as **static** — no DLLs to distribute
- The binary cache warms on first run; subsequent builds skip unchanged packages

### Building with Make (Legacy)

> ⚠️ **DEPRECATION WARNING**: The Makefile build system is being phased out in favor of CMake.
> Please use CMake for new development.

For legacy Make-based builds, see the [legacy build documentation](https://github.com/dkfans/keeperfx/wiki/Building-KeeperFX).

### Development Resources

To get started with KeeperFX development, refer to the [Development Guide](https://github.com/dkfans/keeperfx/wiki/Building-KeeperFX) for 
detailed instructions on setting up a development environment and building KeeperFX from source.

If you wish to discuss development, you can join the [Keeper Klan discord](https://discord.gg/hE4p7vy2Hb) and ask to 
be added to the KeeperFX development channel.

For a CLion workflow using Docker cross-builds and local Windows GDB, see `docs/clion-windows-debug.md`.

### Windows debug runtime from the mingw devcontainer
- Build/package task for full debug runtime layout: `Build & Assemble Windows Debug Runtime`.
- Output path: `out/package/windows-x86-debug/` — identical structure to the release package.
- **Staying in the devcontainer (recommended):** press F5 and choose `Container F5: Attach via gdbserver (Windows Host)`.
  - preLaunchTask builds the debug runtime and copies `gdbserver.exe` into the package, then prints the exact host command.
  - On the Windows host run that printed command (one terminal, stays open).
  - VS Code connects via `host.docker.internal:2159` using the cross-compile GDB inside the container; full breakpoints, step, locals, and source navigation work.
- **From a Windows-hosted VS Code session:** use `Host F5: Build + Assemble + Debug (Windows gdb)` instead (runs gdb natively, no gdbserver needed).
- Host debugger preflight (`Verify Windows Host GDB`) checks `.vscode/gdb.exe` and fails fast with instructions if missing.
- First-time requirement: initialize `.deploy/` once so layered runtime assets are available for assembly:
  `powershell -ExecutionPolicy Bypass -File scripts/init-deploy.ps1 -DungeonKeeperPath "C:\\Path\\To\\Dungeon Keeper"`.
  When working on a dev branch, use `-UseAlpha` to also overlay the latest alpha patch:
  `powershell -ExecutionPolicy Bypass -File scripts/init-deploy.ps1 -UseAlpha`.
  DK path is cached in `~/.keeperfx-dev/` and reused across git worktrees automatically.


## Components
| Component | Language | Info |
|---|---|---|
| [KeeperFX](https://github.com/dkfans/keeperfx) | C, C++ | - |
| [Launcher](https://github.com/dkfans/keeperfx-launcherwx) | C++ | Official Launcher to edit settings and start the game with run options. |
| [FXGraphics](https://github.com/dkfans/FXGraphics) | - | Sources of KeeperFX graphics files. |
| [FXSounds](https://github.com/dkfans/FXsounds) | - | Sources of KeeperFX audio files. |
| [Masterserver](https://github.com/dkfans/keeperfx-masterserver) | PHP (CLI) | Multiplayer masterserver. Allows players to easily find public lobbies of others. |
| [Website](https://github.com/dkfans/keeperfx-website) | PHP | https://keeperfx.net |


## Tools
| Tool | Usage |
|---|---|
| sndbanker | Makes usable ingame sounds from SFX archives. |
| po2ngdat | Converts `.po` files (language) to `.dat`. |
| png2bestpal | Decides the best in-game color palette for an image and creates a `.pal` file. |
| png2ico | Converts `.png` files to `.ico`. |
| pngpal2raw | Creates a `.raw` image file that can be used by the game from a `.png` and a `.pal` (palette) file. The palette file can be created with _png2bestpal_. |
| rnctools | Handles the RNC compression of many original DK data files. |
| dkillconv | An unfinished tool to convert a map to a text based format. |


## Further Improvements
KeeperFX could be further improved in these key areas:
- Multiplayer performance and features
- Expand and improve AI / Computer player behavior
- Improve pathfinding performance
- Expand creative freedom for modders even further
- Native cross-platform support
- Improve code readability and maintainability
- Lua support
- ...


## Contributing
We welcome contributions from the community to improve and expand KeeperFX.
- Report bugs by opening [issues](https://github.com/dkfans/keeperfx/issues).
- Submit feature requests and discuss potential improvements.
- Contribute code by creating pull requests. 


## Code Signing Policy
Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).


## License
This project is licensed under the [GNU General Public License v2.0](LICENSE).
Feel free to use, modify, and distribute it according to the terms of this license.

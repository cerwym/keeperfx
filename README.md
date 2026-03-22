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

### Building with CMake (Recommended)

KeeperFX uses CMake as its primary build system. To build the project:

#### Prerequisites
- CMake 3.20 or later
- MinGW-w64 (for Windows builds)
- vcpkg (optional, for dependency management)
- Git

#### Build Steps
```bash
# Clone the repository
git clone https://github.com/dkfans/keeperfx.git
cd keeperfx

# Configure with CMake preset
cmake --preset windows-x64-release

# Build
cmake --build --preset windows-x64-release
```

This will:
1. Download all required dependencies (SDL2, ffmpeg, OpenAL, etc.)
2. Download build tools (png2ico, po2ngdat, sndbanker, etc.)
3. Compile keeperfx.exe and keeperfx_hvlog.exe
4. Build the test suite

**Available presets:**
- `windows-x64-release` - Release build with standard logging
- `x86-MinGW32-Debug` - Debug build
- `x86-MinGW32-Release` - Release build

To build the test executable:
```bash
cmake --build --preset windows-x64-release --target tests
```

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
  `powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -DungeonKeeperPath "C:\\Path\\To\\Dungeon Keeper"`.


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

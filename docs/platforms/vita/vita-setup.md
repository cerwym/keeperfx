# KeeperFX — PlayStation Vita Setup Guide

## Requirements

- A PS Vita (or PS TV) running [HENkaku](https://henkaku.xyz/) homebrew firmware
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell) for file management
- A legal copy of **Dungeon Keeper** (GOG or CD version)
- The KeeperFX `.vpk` release package

## Building From Source

For Vita source builds, CMake presets, and full preset tables, see:

- `docs/platforms/vita/vita-cmake-build.md`

## Installing the VPK

1. Copy `keeperfx.vpk` to your Vita via VitaShell (USB or FTP).
2. Navigate to the `.vpk` in VitaShell and press **×** to install.
3. The game will appear as **KeeperFX** in the LiveArea.

## SD Card Data Layout

All game data lives under `ux0:data/keeperfx/`. Create this directory if it does not exist.

```
ux0:data/keeperfx/
├── keeperfx.cfg           ← minimum config (see below)
├── data/                  ← KeeperFX release data files
├── fxdata/                ← KeeperFX release FX data
├── hdata/                 ← KeeperFX release high-colour data
├── campgns/               ← KeeperFX campaign files
├── config/                ← KeeperFX config files
├── lang/                  ← KeeperFX language files
├── levels/                ← KeeperFX level files
├── data/*.dat             ← Original Dungeon Keeper data (from GOG/CD)
└── sound/                 ← Original Dungeon Keeper sound effects
```

### Files from the KeeperFX release package

Copy the following directories from the KeeperFX release archive:

| Directory | Description |
|-----------|-------------|
| `data/`   | KeeperFX engine data (sprites, tilesets, etc.) |
| `fxdata/` | KeeperFX FX data |
| `hdata/`  | High-colour assets |
| `campgns/` | Campaign definitions |
| `config/` | Game configuration files |
| `lang/`   | Language strings |
| `levels/` | Level maps |

### Files from the original Dungeon Keeper (GOG / CD)

Extract your GOG installer or copy from the CD. The key files are:

| Path | Description |
|------|-------------|
| `data/*.dat`, `data/*.pal`, `data/*.tab` | Original game data archives |
| `data/*.raw`, `data/*.col`               | Palette and colour data |
| `sound/`                                 | Sound effects and music |

> **GOG users**: Use [InnoExtract](https://constexpr.org/innoextract/) to extract files from the GOG installer without running it on Windows.

## Minimum `keeperfx.cfg`

Create `ux0:data/keeperfx/keeperfx.cfg` with at least:

```
INSTALL_PATH .
LANGUAGE ENG
```

`INSTALL_PATH .` tells KeeperFX that original DK data is in the same directory as KeeperFX data (both under `ux0:data/keeperfx/`). Change `ENG` to your language code if needed (`FRE`, `GER`, `ITA`, `SPA`, `SWE`, `POL`, `DUT`, `HUN`).

## Crash Logs

If the game crashes, a log is written to:

```
ux0:data/keeperfx/crash.log
```

The full game log (including errors) is at:

```
ux0:data/keeperfx/keeperfx.log
```

Both files can be viewed with VitaShell's text viewer or copied to a PC for diagnosis.

## Known Limitations

- FMV intro videos are skipped (game continues to main menu immediately).
- Network multiplayer is not supported.
- Screenshot and movie recording are not supported.
- Runtime renderer switching is not available (Vita always uses the SDL2 backend).

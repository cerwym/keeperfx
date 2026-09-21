# Save layout check

Saved games, the continue file, replays, high scores, the network config and
multiplayer resync are written as raw copies of C structs. Any change to the
layout of those structs makes older files unloadable, or worse, makes them
load with wrong values and no error.

This tool reads the exact layout of every saved type from the compiler's debug
information and compares two versions of the source. The
`save-format-check` workflow runs it on every pull request that touches a
header.

## Results

| Result | Meaning |
|---|---|
| identical | No change to saved data |
| compatible | Only union members added or removed; existing data kept its place |
| refused | A saved type changed size; older files won't load |
| silent | Same size, but existing data moved; older files load **wrong values** |

A PR that is refused or silent fails the check. If the break is intended, a
maintainer accepts it with the `save-break-accepted` label.

## Files

| File | Purpose |
|---|---|
| `roots.txt` | Saved types, the headers that declare them, and the files they end up in. Add a line here for a new saved file |
| `extract.sh` | Compiles a probe of the headers with `i686-w64-mingw32-gcc` and writes every nested field's offset and size (via `walk.py` in gdb) |
| `fetch_sdl_headers.sh` | Downloads the SDL3 and SDL3_mixer MinGW headers, at the versions in `build/cmake/modules/Dependencies.cmake` |
| `compare.py` | Classifies the change and writes `result.json` and a Markdown summary |
| `check_pr.sh` | All of the above for the current checkout against a base commit |
| `replay_history.py` | Runs the comparison over a branch's history, to check the tool itself |

## Running it locally

Needs `i686-w64-mingw32-gcc`, `gdb-multiarch`, `python3`, `curl` and `git`
(for example Debian or Ubuntu, or WSL). From the repository root:

```bash
bash tools/save-layout/check_pr.sh upstream/master /tmp/save-format
```

It prints the result; the details are in `/tmp/save-format/summary.md`.

## Checking the tool against history

Over upstream `master` from 2026-03-27 to 2026-09-21 (490 commits), the check
gives `identical=447,compatible=2,refused=36,silent=5`:

```bash
python3 tools/save-layout/replay_history.py extract /tmp/history --since 2026-03-21
python3 tools/save-layout/replay_history.py check /tmp/history --expect identical=447,compatible=2,refused=36,silent=5
```

Commits from before the SDL3 migration (#5085) also need the SDL2 MinGW
headers in `SDL_INCLUDES`.

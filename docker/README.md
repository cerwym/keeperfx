# KeeperFX Docker Build Images

These Docker images provide consistent, reproducible build environments for
KeeperFX across all target platforms.  The same images are used in GitHub
Actions CI so your local build matches CI exactly.

## Images

| Image | Target | Preset |
|---|---|---|
| `keeperfx-build-mingw32` | Windows x86 (cross-compile) | `windows-x86-release` |
| `keeperfx-build-mingw64` | Windows x64 (cross-compile) | `windows-x64-release` |
| `keeperfx-build-linux`   | Linux x86_64 (native)       | `linux-x64-release`   |
| `keeperfx-build-wasm`    | WebAssembly                  | `wasm-release`        |

Homebrew targets (Vita, 3DS, Switch) re-use upstream images
(`gnuton/vitasdk-docker`, `devkitpro/devkitarm`, `devkitpro/devkita64`) and
are not listed here.

There is also one local-only image that is **never pushed to any registry**:

| Image | Purpose |
|---|---|
| `keeperfx-dk-originals:local` | Caches the 16 original Dungeon Keeper files required by KeeperFX. Built once per machine by `.vscode/init_dk_layer.ps1`. |

---

## Prerequisites (Windows)

- [Docker Desktop for Windows](https://docs.docker.com/desktop/install/windows-install/) installed and running
- The KeeperFX repository checked out with submodules:
  ```pwsh
  git clone --recurse-submodules https://github.com/<org>/keeperfx
  ```
- **Nothing else.** No MinGW, no CMake, no SDL2 dev libs, no WSL, no local toolchain of any kind.

---

## Local dev workflow (Windows + Docker Desktop)

All builds run via Docker Compose from the repository root.  The source tree
is mounted as `/src` inside the container; build output lands in
`out/build/<preset>/` on the host.

### Pull all images

```pwsh
docker compose -f docker/compose.yml pull
```

Override `DOCKER_ORG` only if using a fork with its own published images:
```pwsh
$env:DOCKER_ORG = "myorg"
docker compose -f docker/compose.yml pull
```

### Build Windows x86

```pwsh
docker compose -f docker/compose.yml run mingw32 `
  bash -c "cmake --preset windows-x86-release && cmake --build --preset windows-x86-release"
```

Output: `out/build/windows-x86-release/keeperfx.exe`

### Build Windows x64

```pwsh
docker compose -f docker/compose.yml run mingw64 `
  bash -c "cmake --preset windows-x64-release && cmake --build --preset windows-x64-release"
```

> **Note:** Windows x64 requires x86_64 prebuilt packages in `dkfans/kfx-deps`.
> Until those are published the link step will fail.

### Build Linux x86_64

```pwsh
docker compose -f docker/compose.yml run linux `
  bash -c "cmake --preset linux-x64-release && cmake --build --preset linux-x64-release"
```

### Build WebAssembly

```pwsh
docker compose -f docker/compose.yml run wasm `
  bash -c "cmake --preset wasm-release && cmake --build --preset wasm-release"
```

### Package graphics / sounds / language files

```pwsh
docker compose -f docker/compose.yml run linux bash -c "make pkg-gfx"
docker compose -f docker/compose.yml run linux bash -c "make pkg-sfx"
docker compose -f docker/compose.yml run linux bash -c "make pkg-lang"
```

---

## VS Code integration (layered deploy)

The `.vscode/` tasks use the images above automatically.
Use **Ctrl+Shift+B** to build and deploy to `.deploy/` for local testing.

On first use on a new machine, VS Code will ask for your Dungeon Keeper
GOG/Steam install path once.  This is cached into a local Docker image
(`keeperfx-dk-originals:local`) and never asked again — across all
worktrees on that machine.

---

## Building the images yourself

If you need to modify a Dockerfile and rebuild locally:

```pwsh
# From the repository root
docker build -t keeperfx-build-mingw32 -f docker/mingw32/Dockerfile .
docker build -t keeperfx-build-mingw64 -f docker/mingw64/Dockerfile .
docker build -t keeperfx-build-linux   -f docker/linux/Dockerfile   .
docker build -t keeperfx-build-wasm    -f docker/wasm/Dockerfile    .
```

---

## Image versioning

Images are published by the `docker-publish.yml` workflow whenever their
Dockerfile changes (or on manual dispatch) on `master`, `dev`, or `release/**`
branches.  They are tagged `latest` plus a `sha-<7char>` tag tied to the
triggering commit for reproducibility.

## kfx-deps caching

The `keeperfx-build-mingw32` image pre-downloads all kfx-deps tarballs into
`/opt/kfx-deps/`.  `deps/CMakeLists.txt` reads the `KFX_DEPS_CACHE`
environment variable and copies tarballs from there instead of fetching them
at configure time, eliminating network access during the build.


## Images

| Image | Target | Preset |
|---|---|---|
| `keeperfx-build-mingw32` | Windows x86 (cross-compile) | `windows-x86-release` |
| `keeperfx-build-mingw64` | Windows x64 (cross-compile) | `windows-x64-release` |
| `keeperfx-build-linux`   | Linux x86_64 (native)       | `linux-x64-release`   |
| `keeperfx-build-wasm`    | WebAssembly                  | `wasm-release`        |

Homebrew targets (Vita, 3DS, Switch) re-use upstream images
(`gnuton/vitasdk-docker`, `devkitpro/devkitarm`, `devkitpro/devkita64`) and
are not listed here.

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/) installed and running
- The KeeperFX repository checked out with submodules:
  ```sh
  git clone --recurse-submodules https://github.com/<org>/keeperfx
  ```

## Building locally

Run from the repository root.  The source tree is mounted read-write at `/src`
inside the container so build output lands in `out/build/<preset>/` on your
host machine.

### Windows x86 (cross-compile via Docker)

```sh
docker pull ghcr.io/<org>/keeperfx-build-mingw32:latest

docker run --rm \
  -v "$(pwd):/src" \
  ghcr.io/<org>/keeperfx-build-mingw32:latest \
  bash -c "cmake --preset windows-x86-release && cmake --build --preset windows-x86-release"
```

### Windows x64 (cross-compile via Docker)

```sh
docker pull ghcr.io/<org>/keeperfx-build-mingw64:latest

docker run --rm \
  -v "$(pwd):/src" \
  ghcr.io/<org>/keeperfx-build-mingw64:latest \
  bash -c "cmake --preset windows-x64-release && cmake --build --preset windows-x64-release"
```

> **Note:** The Windows x64 build requires x86_64 prebuilt packages in
> `dkfans/kfx-deps`.  Until those are published the configure step will
> download i686 (32-bit) deps which will cause link errors.

### Linux x86_64

```sh
docker pull ghcr.io/<org>/keeperfx-build-linux:latest

docker run --rm \
  -v "$(pwd):/src" \
  ghcr.io/<org>/keeperfx-build-linux:latest \
  bash -c "cmake --preset linux-x64-release && cmake --build --preset linux-x64-release"
```

### WebAssembly

```sh
docker pull ghcr.io/<org>/keeperfx-build-wasm:latest

docker run --rm \
  -v "$(pwd):/src" \
  ghcr.io/<org>/keeperfx-build-wasm:latest \
  bash -c "cmake --preset wasm-release && cmake --build --preset wasm-release"
```

## Building the images yourself

If you need to modify a Dockerfile and rebuild:

```sh
# From the repository root
docker build -t keeperfx-build-mingw32 -f docker/mingw32/Dockerfile .
docker build -t keeperfx-build-mingw64 -f docker/mingw64/Dockerfile .
docker build -t keeperfx-build-linux   -f docker/linux/Dockerfile   .
docker build -t keeperfx-build-wasm    -f docker/wasm/Dockerfile    .
```

## Image versioning

Images are published by the `docker-publish.yml` workflow whenever their
Dockerfile changes (or on manual dispatch).  They are tagged `latest` plus a
`sha-<7char>` tag tied to the triggering commit for reproducibility.

## kfx-deps caching

The `keeperfx-build-mingw32` image pre-downloads all kfx-deps tarballs into
`/opt/kfx-deps/`.  `deps/CMakeLists.txt` reads the `KFX_DEPS_CACHE`
environment variable and copies tarballs from there instead of fetching them
at configure time, eliminating network access during the build.

#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$(pwd)}"
preset="windows-x86-debug"
compose_file="$workspace/docker/compose.yml"
exe_src="$workspace/out/build/$preset/keeperfx.exe"
deploy_dir="$workspace/.deploy"

do_local_build() {
  (
    cd "$workspace"
    cmake --preset "$preset"
    cmake --build --preset "$preset"
  )
}

do_compose_build() {
  docker compose -f "$compose_file" run --rm mingw32 bash -lc "cmake --preset $preset && cmake --build --preset $preset"
}

if command -v i686-w64-mingw32-gcc-posix >/dev/null 2>&1; then
  echo "Using local MinGW toolchain..."
  do_local_build
elif command -v docker >/dev/null 2>&1 && [[ -f "$compose_file" ]]; then
  echo "Local MinGW toolchain not found; using docker compose..."
  do_compose_build
else
  echo "No MinGW toolchain found and docker is unavailable." >&2
  echo "Install MinGW toolchain or Docker Desktop, then retry." >&2
  exit 127
fi

if [[ ! -f "$exe_src" ]]; then
  echo "Build finished but executable is missing: $exe_src" >&2
  exit 1
fi

mkdir -p "$deploy_dir"
cp -f "$exe_src" "$deploy_dir/keeperfx.exe"
echo "keeperfx.exe deployed to $deploy_dir/keeperfx.exe"

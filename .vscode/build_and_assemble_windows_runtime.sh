#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$(pwd)}"
preset="${2:-windows-x86-release}"
compose_file="$workspace/docker/compose.yml"

reconfigure() {
  (
    cd "$workspace"
    cmake --preset "$preset"
  )
}

build_preset() {
  (
    cd "$workspace"
    cmake --build --preset "$preset"
  )
}

build_in_compose() {
  echo "Using docker compose mingw32 builder..."
  docker compose -f "$compose_file" run --rm mingw32 bash -lc "cmake --preset $preset && cmake --build --preset $preset"
}

to_windows_path() {
  local p="$1"
  if command -v wslpath >/dev/null 2>&1; then
    wslpath -w "$p"
  elif command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$p"
  else
    printf '%s' "$p"
  fi
}

assemble_runtime_with_fallback() {
  if "$workspace/.vscode/assemble_windows_runtime.sh" "$workspace" "$preset"; then
    return 0
  fi

  echo "Bash assembly failed; attempting PowerShell runtime assembly fallback..."

  local ps_cmd=""
  if command -v powershell.exe >/dev/null 2>&1; then
    ps_cmd="powershell.exe"
  elif command -v powershell >/dev/null 2>&1; then
    ps_cmd="powershell"
  else
    echo "No PowerShell executable found for fallback assembly." >&2
    return 1
  fi

  local ps_script
  local ws_path
  ps_script="$(to_windows_path "$workspace/.vscode/assemble_windows_runtime.ps1")"
  ws_path="$(to_windows_path "$workspace")"

  "$ps_cmd" -ExecutionPolicy Bypass -File "$ps_script" -WorkspaceFolder "$ws_path" -Preset "$preset"
}

repair_cache_if_needed() {
  local build_dir="$workspace/out/build/$preset"
  rm -f "$build_dir/CMakeCache.txt" || return 1
  rm -rf "$build_dir/CMakeFiles" || return 1
}

if command -v i686-w64-mingw32-gcc-posix >/dev/null 2>&1; then
  echo "Detected mingw toolchain in current environment; building directly..."
  if ! reconfigure; then
    echo "Initial configure failed; repairing stale CMake cache and retrying..."
    if repair_cache_if_needed && reconfigure; then
      :
    else
      echo "Cache repair failed (likely permissions). Falling back to docker compose build..."
      build_in_compose
    fi
  else
    build_preset
  fi
else
  build_in_compose
fi

assemble_runtime_with_fallback

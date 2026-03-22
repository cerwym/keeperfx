#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$(pwd)}"
preset="${2:-windows-x86-debug}"
compose_file="$workspace/docker/compose.yml"
build_dir="$workspace/out/build/$preset"
deploy_dir="$workspace/.deploy"
exe_src="$build_dir/keeperfx.exe"

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
  docker compose -f "$compose_file" run --rm mingw32 bash -lc "cmake --preset $preset && cmake --build --preset $preset"
}

repair_cache_if_needed() {
  rm -f "$build_dir/CMakeCache.txt" || return 1
  rm -rf "$build_dir/CMakeFiles" || return 1
}

copy_dir_newer() {
  local src="$1"
  local dst="$2"
  if [[ -d "$src" ]]; then
    mkdir -p "$dst"
    cp -au "$src/." "$dst/"
  fi
}

copy_file_if_exists() {
  local src="$1"
  local dst="$2"
  if [[ -f "$src" ]]; then
    cp -fu "$src" "$dst"
  fi
}

if [[ ! -d "$deploy_dir" ]]; then
  echo ".deploy is missing. Run Init .deploy/ once before using container F5 debugging." >&2
  exit 1
fi

echo "Building $preset ..."
if command -v i686-w64-mingw32-gcc-posix >/dev/null 2>&1; then
  if [[ -f "$build_dir/build.ninja" || -f "$build_dir/Makefile" ]]; then
    echo "Existing build tree detected; running incremental build only..."
    build_preset
  else
    if ! reconfigure; then
      echo "Configure failed, attempting cache repair..."
      if repair_cache_if_needed && reconfigure; then
        :
      elif [[ -f "$compose_file" ]] && command -v docker >/dev/null 2>&1; then
        echo "Cache repair failed; falling back to docker compose build..."
        build_in_compose
      else
        echo "Build configure failed and no docker fallback available." >&2
        exit 1
      fi
    fi
    build_preset
  fi
elif [[ -f "$compose_file" ]] && command -v docker >/dev/null 2>&1; then
  echo "MinGW toolchain not found locally; using docker compose build..."
  build_in_compose
else
  echo "No MinGW toolchain and no docker compose fallback available." >&2
  exit 1
fi

if [[ ! -f "$exe_src" ]]; then
  echo "Expected executable not found: $exe_src" >&2
  exit 1
fi

echo "Syncing runtime into .deploy (incremental)..."
mkdir -p "$deploy_dir"
cp -f "$exe_src" "$deploy_dir/keeperfx.exe"

copy_dir_newer "$workspace/pkg/data" "$deploy_dir/data"
copy_dir_newer "$workspace/pkg/ldata" "$deploy_dir/ldata"

# Keep DK/release-provided sound banks for runtime. pkg/sound assets are not
# guaranteed to match the expected bank layout consumed by bflib_sndlib.

copy_dir_newer "$workspace/config/creatrs" "$deploy_dir/creatrs"
copy_dir_newer "$workspace/config/fxdata" "$deploy_dir/fxdata"
copy_dir_newer "$workspace/pkg/fxdata" "$deploy_dir/fxdata"
copy_dir_newer "$workspace/config/mods" "$deploy_dir/mods"
copy_dir_newer "$workspace/campgns" "$deploy_dir/campgns"
copy_dir_newer "$workspace/levels" "$deploy_dir/levels"
copy_dir_newer "$workspace/pkg/campgns" "$deploy_dir/campgns"
copy_dir_newer "$workspace/pkg/levels" "$deploy_dir/levels"

copy_file_if_exists "$workspace/config/keeperfx.cfg" "$deploy_dir/keeperfx.cfg"
copy_file_if_exists "$workspace/docs/keeperfx_readme.txt" "$deploy_dir/keeperfx_readme.txt"

for dll in SDL2.dll SDL2_image.dll SDL2_mixer.dll SDL2_net.dll; do
  copy_file_if_exists "$workspace/sdl/for_final_package/$dll" "$deploy_dir/$dll"
done

echo "Running runtime asset preflight checks ..."
bash "$workspace/.vscode/validate_runtime_assets.sh" "$workspace" ".deploy"

if ! command -v winegdb >/dev/null 2>&1 && ! command -v winedbg >/dev/null 2>&1; then
  echo "" >&2
  echo "────────────────────────────────────────────────────────────────" >&2
  echo "  Wine debugger not found — this profile (winegdb) won't work." >&2
  echo "" >&2
  echo "  Two options:" >&2
  echo "  1) Install Wine: run task 'Install Devcontainer Wine Debugger'" >&2
  echo "     then press F5 again with this profile." >&2
  echo "" >&2
  echo "  2) (No install needed) Press F5 and choose:" >&2
  echo "     'Container F5: Attach via gdbserver (Windows Host)'" >&2
  echo "     — builds the full debug runtime, copies gdbserver.exe," >&2
  echo "       and tells you one command to run on your Windows host." >&2
  echo "────────────────────────────────────────────────────────────────" >&2
  exit 1
fi

echo "Devcontainer F5 prep complete: $deploy_dir/keeperfx.exe"

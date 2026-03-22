#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$(pwd)}"
preset="${2:-windows-x86-release}"

case "$preset" in
  windows-x86-release|windows-x86-debug) ;;
  *) echo "Unsupported preset: $preset" >&2; exit 1 ;;
esac

deploy_dir="$workspace/.deploy"
exe_path="$workspace/out/build/$preset/keeperfx.exe"
out_dir="$workspace/out/package/$preset"

[[ -d "$deploy_dir" ]] || { echo ".deploy not initialized. Run Init .deploy/ first." >&2; exit 1; }
[[ -f "$exe_path" ]] || { echo "Executable not found: $exe_path" >&2; exit 1; }

rm -rf "$out_dir"
mkdir -p "$out_dir"
cp -a "$deploy_dir/." "$out_dir/"

if [[ -f "$workspace/config/keeperfx.cfg" ]]; then
  cp -f "$workspace/config/keeperfx.cfg" "$out_dir/keeperfx.cfg"
fi
if [[ -d "$workspace/config/creatrs" ]]; then
  cp -a "$workspace/config/creatrs" "$out_dir/creatrs"
fi
if [[ -d "$workspace/config/fxdata" ]]; then
  cp -a "$workspace/config/fxdata" "$out_dir/fxdata"
fi
if [[ -d "$workspace/config/mods" ]]; then
  cp -a "$workspace/config/mods" "$out_dir/mods"
fi
if [[ -d "$workspace/campgns" ]]; then
  cp -a "$workspace/campgns" "$out_dir/campgns"
fi
if [[ -d "$workspace/levels" ]]; then
  cp -a "$workspace/levels" "$out_dir/levels"
fi
if [[ -f "$workspace/docs/keeperfx_readme.txt" ]]; then
  cp -f "$workspace/docs/keeperfx_readme.txt" "$out_dir/keeperfx_readme.txt"
fi

for dll in SDL2.dll SDL2_image.dll SDL2_mixer.dll SDL2_net.dll; do
  if [[ -f "$workspace/sdl/for_final_package/$dll" ]]; then
    cp -f "$workspace/sdl/for_final_package/$dll" "$out_dir/$dll"
  fi
done

if [[ -d "$workspace/pkg/data" ]]; then
  mkdir -p "$out_dir/data"
  cp -a "$workspace/pkg/data/." "$out_dir/data/"
fi
if [[ -d "$workspace/pkg/ldata" ]]; then
  mkdir -p "$out_dir/ldata"
  cp -a "$workspace/pkg/ldata/." "$out_dir/ldata/"
fi
if [[ -d "$workspace/pkg/fxdata" ]]; then
  mkdir -p "$out_dir/fxdata"
  cp -a "$workspace/pkg/fxdata/." "$out_dir/fxdata/"
fi
if [[ -d "$workspace/pkg/campgns" ]]; then
  mkdir -p "$out_dir/campgns"
  cp -a "$workspace/pkg/campgns/." "$out_dir/campgns/"
fi
if [[ -d "$workspace/pkg/levels" ]]; then
  mkdir -p "$out_dir/levels"
  cp -a "$workspace/pkg/levels/." "$out_dir/levels/"
fi

# Keep DK/release-provided sound banks for runtime. pkg/sound assets are not
# guaranteed to match the expected bank layout consumed by bflib_sndlib.

cp "$exe_path" "$out_dir/keeperfx.exe"

[[ -f "$out_dir/keeperfx.exe" ]] || { echo "Missing keeperfx.exe in package output" >&2; exit 1; }
[[ -f "$out_dir/keeperfx.cfg" ]] || { echo "Missing keeperfx.cfg in package output" >&2; exit 1; }
[[ -f "$out_dir/SDL2.dll" ]] || { echo "Missing SDL2.dll in package output" >&2; exit 1; }
[[ -d "$out_dir/data" ]] || { echo "Missing data directory in package output" >&2; exit 1; }
[[ -d "$out_dir/sound" ]] || { echo "Missing sound directory in package output" >&2; exit 1; }
[[ -d "$out_dir/campgns" ]] || { echo "Missing campgns directory in package output" >&2; exit 1; }
[[ -d "$out_dir/levels" ]] || { echo "Missing levels directory in package output" >&2; exit 1; }
[[ -d "$out_dir/fxdata" ]] || { echo "Missing fxdata directory in package output" >&2; exit 1; }

echo "Runtime package ready: $out_dir"

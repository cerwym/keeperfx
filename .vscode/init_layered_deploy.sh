#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$(pwd)}"
dk_install_path="${2:-}"

deploy_dir="$workspace/.deploy"
deploy_data="$deploy_dir/data"
deploy_sound="$deploy_dir/sound"
deploy_levels="$deploy_dir/levels"
compose_file="$workspace/docker/compose.yml"

mkdir -p "$deploy_data" "$deploy_sound" "$deploy_levels"

if [[ -n "$dk_install_path" ]]; then
  "$workspace/.vscode/init_dk_layer.sh" "$workspace" "$dk_install_path"
else
  "$workspace/.vscode/init_dk_layer.sh" "$workspace"
fi

if [[ -d "$workspace/.local/dk-stage/data" && -d "$workspace/.local/dk-stage/sound" ]]; then
  echo "Copying DK originals from normalized local stage ..."
  cp -a "$workspace/.local/dk-stage/data/." "$deploy_data/"
  cp -a "$workspace/.local/dk-stage/sound/." "$deploy_sound/"
else
  echo "Normalized stage unavailable; copying originals directly from cached DK path ..."
  config_path="$workspace/.local/dk-install-path.txt"
  if [[ ! -f "$config_path" ]]; then
    echo "Missing cached DK path; rerun Init DK Originals Layer." >&2
    exit 1
  fi
  dk_root="$(tr -d '\r\n' < "$config_path")"
  cp -a "$dk_root/data/." "$deploy_data/"
  cp -a "$dk_root/sound/." "$deploy_sound/"
fi

config_path="$workspace/.local/dk-install-path.txt"
if [[ -f "$config_path" ]]; then
  dk_root="$(tr -d '\r\n' < "$config_path")"
  if [[ -d "$dk_root/levels" ]]; then
    echo "Copying original DK levels into .deploy/levels ..."
    cp -a "$dk_root/levels/." "$deploy_levels/"
  fi
fi

echo "Building KeeperFX asset packages ..."
if command -v docker >/dev/null 2>&1 && [[ -f "$compose_file" ]]; then
  docker compose -f "$compose_file" run --rm linux bash -lc "make -j1 pkg-gfx && make -j1 pkg-languages && make -j1 pkg-sfx"
else
  (
    cd "$workspace"
    make -j1 pkg-gfx
    make -j1 pkg-languages
    make -j1 pkg-sfx
  )
fi

if [[ -d "$workspace/pkg/data" ]]; then
  cp -a "$workspace/pkg/data/." "$deploy_data/"
fi
if [[ -d "$workspace/pkg/ldata" ]]; then
  mkdir -p "$deploy_dir/ldata"
  cp -a "$workspace/pkg/ldata/." "$deploy_dir/ldata/"
fi
if [[ -d "$workspace/pkg/fxdata" ]]; then
  mkdir -p "$deploy_dir/fxdata"
  cp -a "$workspace/pkg/fxdata/." "$deploy_dir/fxdata/"
fi
if [[ -d "$workspace/pkg/campgns" ]]; then
  mkdir -p "$deploy_dir/campgns"
  cp -a "$workspace/pkg/campgns/." "$deploy_dir/campgns/"
fi
if [[ -d "$workspace/pkg/levels" ]]; then
  mkdir -p "$deploy_dir/levels"
  cp -a "$workspace/pkg/levels/." "$deploy_dir/levels/"
fi

# Keep DK/release-provided sound banks for runtime. pkg/sound assets are not
# guaranteed to match the expected bank layout consumed by bflib_sndlib.

# KeeperFX community maps (classic/standard/lostlvls) ship their binary data
# only in the official "complete" release archive, not in the git repo.
# Download and extract them when needed.
kfx_release_url="https://github.com/dkfans/keeperfx/releases/download/v1.3.1/keeperfx_1_3_1_complete.7z"
kfx_cache_dir="$workspace/.local/kfx-complete"
kfx_archive="$kfx_cache_dir/keeperfx_1_3_1_complete.7z"
kfx_extracted="$kfx_cache_dir/extracted"

need_release=0
for pack in classic standard lostlvls; do
  pack_dir="$deploy_levels/$pack"
  dat_count=$(find "$pack_dir" -maxdepth 1 -type f -iname '*.dat' 2>/dev/null | wc -l | tr -d '[:space:]')
  if [[ "$dat_count" -eq 0 ]]; then
    need_release=1
    break
  fi
done

if [[ "$need_release" -eq 1 ]] && command -v 7z >/dev/null 2>&1; then
  echo "Community map binaries missing; sourcing from KeeperFX release archive..."
  mkdir -p "$kfx_cache_dir"
  if [[ ! -f "$kfx_archive" ]]; then
    echo "Downloading keeperfx complete release (~356 MB)..."
    curl -fSL -o "$kfx_archive" "$kfx_release_url"
  fi
  if [[ ! -d "$kfx_extracted/levels" ]]; then
    echo "Extracting map pack levels..."
    mkdir -p "$kfx_extracted"
    7z x "$kfx_archive" -o"$kfx_extracted" -y 'levels/classic/*' 'levels/standard/*' 'levels/lostlvls/*' >/dev/null
  fi
  for pack in classic standard lostlvls; do
    src="$kfx_extracted/levels/$pack"
    dst="$deploy_levels/$pack"
    if [[ -d "$src" ]]; then
      mkdir -p "$dst"
      cp -a "$src/." "$dst/"
      echo "  Hydrated levels/$pack from release archive."
    fi
  done
elif [[ "$need_release" -eq 1 ]]; then
  echo "WARNING: Community map binaries missing and 7z not found. Free Play maps will be unavailable." >&2
fi

echo "Running runtime asset preflight checks ..."
bash "$workspace/.vscode/validate_runtime_assets.sh" "$workspace" ".deploy"

echo ".deploy is initialized and layered for host runtime testing."

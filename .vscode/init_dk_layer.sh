#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$(pwd)}"
dk_install_path="${2:-}"

required=(
  "data/bluepal.dat"
  "data/bluepall.dat"
  "data/dogpal.pal"
  "data/hitpall.dat"
  "data/lightng.pal"
  "data/main.pal"
  "data/mapfadeg.dat"
  "data/redpal.col"
  "data/redpall.dat"
  "data/slab0-0.dat"
  "data/slab0-1.dat"
  "data/vampal.pal"
  "data/whitepal.col"
  "sound/atmos1.sbk"
  "sound/atmos2.sbk"
  "sound/bullfrog.sbk"
)

mkdir -p "$workspace/.local"
config_path="$workspace/.local/dk-install-path.txt"

if [[ -z "$dk_install_path" && -f "$config_path" ]]; then
  dk_install_path="$(tr -d '\r\n' < "$config_path")"
fi

if [[ -z "$dk_install_path" ]]; then
  read -r -p "Enter path to original Dungeon Keeper install (contains data/ and sound/): " dk_install_path
fi

if [[ -z "$dk_install_path" ]]; then
  echo "No Dungeon Keeper install path provided." >&2
  exit 1
fi

if [[ ! -d "$dk_install_path" ]]; then
  echo "Invalid DK path: $dk_install_path" >&2
  exit 1
fi

missing=()
for rel in "${required[@]}"; do
  if [[ ! -f "$dk_install_path/$rel" ]]; then
    missing+=("$rel")
  fi
done
if (( ${#missing[@]} > 0 )); then
  echo "DK install path is missing required files: ${missing[*]}" >&2
  exit 1
fi

printf "%s" "$dk_install_path" > "$config_path"

dk_stage="$workspace/.local/dk-stage"
rm -rf "$dk_stage"
mkdir -p "$dk_stage/data" "$dk_stage/sound"

for rel in "${required[@]}"; do
  src="$(find "$dk_install_path" -type f -iname "$(basename "$rel")" | head -n 1 || true)"
  if [[ -z "$src" ]]; then
    echo "Required file not found in DK install (case-insensitive lookup): $rel" >&2
    exit 1
  fi
  cp -f "$src" "$dk_stage/$rel"
done

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker not found. Cached DK path only; Docker DK image was not built." >&2
  exit 0
fi

echo "Building local DK originals image keeperfx-dk-originals:local ..."
(
  cd "$workspace"
  docker build --build-context "dk=$dk_stage" -f docker/dk-originals/Dockerfile -t keeperfx-dk-originals:local .
)

echo "DK originals layer is ready: keeperfx-dk-originals:local"

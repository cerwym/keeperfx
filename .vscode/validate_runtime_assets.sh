#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$(pwd)}"
deploy_subdir="${2:-.deploy}"

deploy_dir="$workspace/$deploy_subdir"
deploy_levels="$deploy_dir/levels"
src_levels="$workspace/levels"
report_path="$deploy_dir/runtime-asset-report.txt"

missing=()
notes=()

add_missing() {
  missing+=("$1")
}

add_note() {
  notes+=("$1")
}

get_config_value() {
  local cfg="$1"
  local key="$2"
  awk -v k="$key" '
    BEGIN { IGNORECASE=0 }
    $0 ~ "^[[:space:]]*" k "[[:space:]]*=" {
      sub(/^[[:space:]]*[^=]+=[[:space:]]*/, "", $0)
      sub(/[[:space:]]*;.*/, "", $0)
      sub(/[[:space:]]*\/\/.*/, "", $0)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0)
      gsub(/^"|"$/, "", $0)
      gsub(/^'"'"'|'"'"'$/, "", $0)
      print $0
      exit
    }
  ' "$cfg"
}

map_name_from_script() {
  local script_path="$1"
  awk '
    /^[[:space:]]*NAME_TEXT[[:space:]]+"/ {
      sub(/^[[:space:]]*NAME_TEXT[[:space:]]+"/, "", $0)
      sub(/".*/, "", $0)
      print $0
      exit
    }
  ' "$script_path"
}

ensure_pack_map_assets() {
  local pack="$1"
  local pack_dir="$2"
  shift 2
  local ids=("$@")
  local copied=0

  for id in "${ids[@]}"; do
    local dest_dat="$pack_dir/map${id}.dat"
    if [[ -f "$dest_dat" ]]; then
      continue
    fi

    local had_any=0
    local exts=(dat clm slb tng own wlb)
    for ext in "${exts[@]}"; do
      local src_l="$deploy_levels/map${id}.${ext}"
      local src_u="$deploy_levels/MAP${id}.${ext^^}"
      local src=""
      if [[ -f "$src_l" ]]; then
        src="$src_l"
      elif [[ -f "$src_u" ]]; then
        src="$src_u"
      fi
      if [[ -n "$src" ]]; then
        cp -f "$src" "$pack_dir/map${id}.${ext}"
        copied=$((copied + 1))
        had_any=1
      fi
    done

    if [[ "$had_any" -eq 0 ]]; then
      add_missing "Missing map binary for pack '$pack', map$id: expected map${id}.dat (+ related files) from DK/repo"
    fi
  done

  if [[ "$copied" -gt 0 ]]; then
    add_note "Hydrated $copied map asset files into levels/$pack from DK-level root files."
  fi
}

[[ -d "$deploy_dir" ]] || { echo "Deploy directory not found: $deploy_dir" >&2; exit 1; }

for req_dir in data sound levels fxdata campgns ldata; do
  [[ -d "$deploy_dir/$req_dir" ]] || add_missing "Missing runtime directory: $req_dir"
done

for req_file in keeperfx.cfg fxdata/gtext_eng.dat; do
  [[ -f "$deploy_dir/$req_file" ]] || add_missing "Missing runtime file: $req_file"
done

if [[ -d "$deploy_levels" ]]; then
  for pack in classic standard lostlvls; do
    cfg="$deploy_levels/$pack.cfg"
    if [[ ! -f "$cfg" ]]; then
      add_missing "Missing map-pack config: levels/$pack.cfg"
      continue
    fi

    levels_location="$(get_config_value "$cfg" LEVELS_LOCATION || true)"
    if [[ -z "$levels_location" ]]; then
      add_missing "Missing LEVELS_LOCATION in levels/$pack.cfg"
      continue
    fi

    pack_dir="$deploy_dir/$levels_location"
    if [[ ! -d "$pack_dir" ]]; then
      mkdir -p "$pack_dir"
      add_note "Created missing levels location directory: $levels_location"
    fi

    mapfile -t src_ids < <(find "$src_levels/$pack" -maxdepth 1 -type f -iname 'map*.txt' -printf '%f\n' 2>/dev/null | sed -E 's/^[mM][aA][pP]([0-9]{5})\.txt$/\1/' | grep -E '^[0-9]{5}$' | sort -u)
    if [[ "${#src_ids[@]}" -eq 0 ]]; then
      add_missing "No source map scripts found for pack '$pack' under repository levels/$pack"
      continue
    fi

    ensure_pack_map_assets "$pack" "$pack_dir" "${src_ids[@]}"

    lif_count=$(find "$pack_dir" -maxdepth 1 -type f \( -iname '*.lif' -o -iname '*.lof' \) | wc -l | tr -d '[:space:]')
    if [[ "$lif_count" -eq 0 ]]; then
      lif_path="$pack_dir/$pack.lif"
      : > "$lif_path"
      entries=0
      for id in "${src_ids[@]}"; do
        if [[ -f "$pack_dir/map${id}.dat" ]]; then
          name="$(map_name_from_script "$src_levels/$pack/map${id}.txt" || true)"
          [[ -n "$name" ]] || name="Map $id"
          printf '%d, %s\n' "$((10#$id))" "$name" >> "$lif_path"
          entries=$((entries + 1))
        fi
      done
      if [[ "$entries" -gt 0 ]]; then
        add_note "Generated levels metadata file: $lif_path"
        lif_count=1
      else
        rm -f "$lif_path"
      fi
    fi

    if [[ "$lif_count" -eq 0 ]]; then
      add_missing "Pack '$pack' has no .lif/.lof metadata in $levels_location"
    fi
  done
fi

{
  echo "Runtime asset preflight report ($(date '+%Y-%m-%d %H:%M:%S'))"
  echo "Workspace: $workspace"
  echo "Deploy: $deploy_dir"
  echo
  if [[ "${#notes[@]}" -gt 0 ]]; then
    echo "Applied fixes:"
    for n in "${notes[@]}"; do
      echo "- $n"
    done
    echo
  fi

  if [[ "${#missing[@]}" -eq 0 ]]; then
    echo "Status: OK"
    echo "All required runtime assets for current checks are present."
  else
    echo "Status: FAILED"
    echo "Missing assets:"
    for m in "${missing[@]}"; do
      echo "- $m"
    done
  fi
} > "$report_path"

if [[ "${#missing[@]}" -eq 0 ]]; then
  echo "Runtime asset preflight passed."
  echo "Report: $report_path"
  exit 0
fi

echo "Runtime asset preflight failed." >&2
echo "Report: $report_path" >&2
for m in "${missing[@]}"; do
  echo " - $m" >&2
done
exit 2

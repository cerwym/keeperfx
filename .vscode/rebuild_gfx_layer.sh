#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$(pwd)}"
deploy_dir="$workspace/.deploy"
compose_file="$workspace/docker/compose.yml"

if [[ ! -d "$deploy_dir" ]]; then
  echo ".deploy does not exist. Run Init .deploy/ first."
  exit 1
fi

if [[ ! -f "$compose_file" ]]; then
  echo "Compose file not found: $compose_file"
  exit 1
fi

echo "Rebuilding gfx package in linux container..."
docker compose -f "$compose_file" run --rm linux bash -lc "make pkg-gfx"

echo "Staging pkg/data, pkg/ldata, pkg/fxdata, pkg/campgns and pkg/levels into .deploy..."
mkdir -p "$deploy_dir/data"
if [[ -d "$workspace/pkg/data" ]]; then
  cp -a "$workspace/pkg/data/." "$deploy_dir/data/"
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

echo "GFX layer refreshed in .deploy/data, .deploy/ldata, .deploy/fxdata, .deploy/campgns and .deploy/levels."

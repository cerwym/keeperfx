#!/usr/bin/env bash
# rebuild_gfx_layer.sh — Rebuild Layer 2 (KFX generated gfx data) into .deploy/
#
# Hash-based caching: only reruns make pkg-gfx when the gfx submodule commit
# has changed or pkg/data/ doesn't exist. Use --force to always rebuild.
#
# Works on Linux, macOS, and Windows (Git Bash / WSL).
#
# Usage:
#   .vscode/rebuild_gfx_layer.sh [--force]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WF="$(cd "$SCRIPT_DIR/.." && pwd)"
FORCE=0
for arg in "$@"; do [[ "$arg" == "--force" ]] && FORCE=1; done

PKG_DATA="$WF/pkg/data"
PKG_LDATA="$WF/pkg/ldata"
HASH_FILE="$WF/pkg/.gfx-hash"
DEPLOY="$WF/.deploy"

C_CYAN='\033[0;36m' C_GREEN='\033[0;32m' C_YELLOW='\033[1;33m'
C_RED='\033[0;31m'  C_GRAY='\033[0;90m'  C_RESET='\033[0m'

log()  { echo -e "${C_CYAN}$*${C_RESET}"; }
ok()   { echo -e "  ${C_GREEN}✓ $*${C_RESET}"; }
warn() { echo -e "  ${C_YELLOW}⚠  $*${C_RESET}"; }
err()  { echo -e "${C_RED}ERROR: $*${C_RESET}"; exit 1; }

log "=== KeeperFX Layer 2: GFX Data ==="
echo

[[ -d "$DEPLOY" ]] || err ".deploy/ not found. Run 'Init .deploy/' first."

# ── Ensure gfx submodule is initialized ───────────────────────────────────────
SUB_STATUS=$(git -C "$WF" submodule status gfx 2>&1 || true)
if [[ "$SUB_STATUS" == -* ]]; then
    echo -e "  ${C_YELLOW}Initializing gfx submodule...${C_RESET}"
    git -C "$WF" submodule update --init gfx || \
        err "gfx submodule init failed. SSH key required?\n  URL: $(git -C "$WF" config submodule.gfx.url)"
    SUB_STATUS=$(git -C "$WF" submodule status gfx 2>&1)
fi

# Parse commit hash (strip leading +/- and trailing path)
CURRENT_HASH=$(echo "$SUB_STATUS" | grep -oE '[0-9a-f]{40}' | head -1)
[[ -n "$CURRENT_HASH" ]] || err "Could not determine gfx submodule hash."

CACHED_HASH=$(cat "$HASH_FILE" 2>/dev/null | tr -d '[:space:]' || true)
SHORT="${CURRENT_HASH:0:8}"

# ── Build if needed ────────────────────────────────────────────────────────────
if [[ $FORCE -eq 0 && -d "$PKG_DATA" && "$CURRENT_HASH" == "$CACHED_HASH" ]]; then
    ok "gfx data up to date (hash $SHORT)"
    echo -e "  ${C_GRAY}Use --force to rebuild anyway.${C_RESET}"
else
    if   [[ $FORCE -eq 1 ]];          then REASON="forced"
    elif [[ ! -d "$PKG_DATA" ]];      then REASON="no cache"
    else                                   REASON="submodule changed"; fi
    echo -e "  ${C_YELLOW}Building pkg-gfx ($REASON, hash $SHORT)...${C_RESET}"
    echo

    docker compose -f "$WF/docker/compose.yml" run --rm linux bash -c "make pkg-gfx"

    mkdir -p "$(dirname "$HASH_FILE")"
    echo "$CURRENT_HASH" > "$HASH_FILE"
    ok "pkg-gfx built (hash $SHORT)"
fi

# ── Deploy into .deploy/ ───────────────────────────────────────────────────────
echo
echo -e "${C_CYAN}Deploying to .deploy/...${C_RESET}"

if [[ -d "$PKG_DATA" ]]; then
    cp -r "$PKG_DATA"/. "$DEPLOY/data/"
    ok "pkg/data/ deployed"
else
    warn "pkg/data/ not found"
fi

if [[ -d "$PKG_LDATA" ]]; then
    mkdir -p "$DEPLOY/ldata"
    cp -r "$PKG_LDATA"/. "$DEPLOY/ldata/"
    ok "pkg/ldata/ deployed"
fi

echo
log "=== Layer 2 complete ==="

#!/usr/bin/env bash
# fix-intellisense.sh — Ensures compile_commands.json has workspace-correct paths
#
# Called by postStartCommand in devcontainer.json files.
# Handles the mismatch between Docker Compose builds (mounted at /src/)
# and devcontainers (mounted at /workspaces/<folder>/).
#
# Logic:
#   1. CMakeCache missing or has stale /src/ paths → full reconfigure (slow)
#   2. CMakeCache correct but compile_commands.json has /src/ paths → sed rewrite (fast)
#   3. Both correct → no-op
#
# Usage: fix-intellisense.sh <preset>
# Example: fix-intellisense.sh vita-debug

set -e

PRESET="${1:?Usage: fix-intellisense.sh <preset>}"
BUILD_DIR="out/build/$PRESET"
WORKSPACE_ROOT="$(pwd)"

# 1) CMakeCache stale or missing → full reconfigure
if ! grep -q 'CMAKE_HOME_DIRECTORY:INTERNAL=/workspaces' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    echo "CMake cache stale or missing — reconfiguring $PRESET..."
    rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
    cmake --preset "$PRESET" 2>&1 | tail -20
    echo "✓ IntelliSense ready ($PRESET)."
    exit 0
fi

# 2) CMakeCache is fine but compile_commands.json still has Docker /src/ paths
if grep -q '"/src/' "$BUILD_DIR/compile_commands.json" 2>/dev/null; then
    echo "Rewriting stale /src/ paths in compile_commands.json..."
    sed -i "s|/src/|$WORKSPACE_ROOT/|g" "$BUILD_DIR/compile_commands.json"
    echo "✓ IntelliSense ready ($PRESET) — paths corrected."
    exit 0
fi

echo "✓ IntelliSense ready ($PRESET)."

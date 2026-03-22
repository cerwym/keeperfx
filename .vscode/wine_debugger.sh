#!/usr/bin/env bash
set -euo pipefail

if command -v winegdb >/dev/null 2>&1; then
  exec winegdb "$@"
fi

if command -v winedbg >/dev/null 2>&1; then
  exec winedbg "$@"
fi

echo "No Wine debugger found (winegdb/winedbg). Run task: Install Devcontainer Wine Debugger." >&2
exit 1

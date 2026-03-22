#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This helper currently supports apt-based containers only." >&2
  echo "Install Wine + MinGW GDB manually, then verify with:" >&2
  echo "  command -v winegdb || command -v winedbg" >&2
  echo "  command -v i686-w64-mingw32-gdb" >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  SUDO=""
elif command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  echo "Need root or sudo to install packages." >&2
  exit 1
fi

echo "Installing Wine debugger tooling and MinGW i686 GDB ..."
$SUDO apt-get update

# KeeperFX Windows target is x86; ensure 32-bit Wine support is available.
if ! dpkg --print-foreign-architectures | grep -qx "i386"; then
  if [[ -n "$SUDO" ]]; then
    $SUDO dpkg --add-architecture i386
  else
    dpkg --add-architecture i386
  fi
  $SUDO apt-get update
fi

packages=(wine wine32 gdb gdb-mingw-w64-i686)

if [[ -n "$SUDO" ]]; then
  if ! $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"; then
    # Fallback name on some distros/images.
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends wine wine32 gdb gdb-mingw-w64
  fi
else
  if ! DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"; then
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends wine wine32 gdb gdb-mingw-w64
  fi
fi

if command -v winegdb >/dev/null 2>&1 || command -v winedbg >/dev/null 2>&1; then
  echo "Wine debugger installed successfully: $(command -v winegdb || command -v winedbg)"
else
  echo "Installation finished but no Wine debugger command was found (winegdb/winedbg)." >&2
  exit 1
fi

if command -v i686-w64-mingw32-gdb >/dev/null 2>&1; then
  echo "MinGW i686 GDB installed successfully: $(command -v i686-w64-mingw32-gdb)"
else
  echo "Installation finished but i686-w64-mingw32-gdb was not found." >&2
  exit 1
fi

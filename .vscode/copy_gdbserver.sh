#!/usr/bin/env bash
# Locates gdbserver.exe from the MinGW cross-toolchain (either in the current
# environment or by extracting from the mingw32 Docker service) and copies it
# into the debug runtime package.  After copying it prints the exact host-side
# command the user must run before pressing F5 to attach.
set -euo pipefail

workspace="${1:-$(pwd)}"
preset="${2:-windows-x86-debug}"
port="${3:-2159}"

out_dir="$workspace/out/package/$preset"
compose_file="$workspace/docker/compose.yml"

[[ -d "$out_dir" ]] || {
  echo "Package directory not found: $out_dir" >&2
  echo "Run 'Build & Assemble Windows Debug Runtime' first." >&2
  exit 1
}

# Search PATH / well-known MinGW locations in the active environment.
locate_gdbserver_local() {
  find /usr /opt 2>/dev/null -name 'gdbserver.exe' -print -quit 2>/dev/null || true
}

# Extract gdbserver.exe from the mingw32 Docker service when we are outside it.
extract_gdbserver_from_container() {
  local tmpdir
  tmpdir="$(mktemp -d)"
  local cmd='f=$(find /usr /opt 2>/dev/null -name gdbserver.exe -print -quit 2>/dev/null); [ -n "$f" ] || exit 1; cat "$f"'
  if docker compose -f "$compose_file" run --rm --entrypoint='sh' mingw32 \
       -c "$cmd" > "$tmpdir/gdbserver.exe" 2>/dev/null \
     && [[ -s "$tmpdir/gdbserver.exe" ]]; then
    printf '%s' "$tmpdir/gdbserver.exe"
    return 0
  fi
  rm -rf "$tmpdir"
  return 1
}

gdbserver_src="$(locate_gdbserver_local)"

if [[ -z "$gdbserver_src" ]]; then
  if command -v docker >/dev/null 2>&1 && [[ -f "$compose_file" ]]; then
    echo "gdbserver.exe not in current environment — extracting from mingw32 builder image..."
    gdbserver_src="$(extract_gdbserver_from_container || true)"
  fi
fi

if [[ -n "$gdbserver_src" && -f "$gdbserver_src" ]]; then
  cp -f "$gdbserver_src" "$out_dir/gdbserver.exe"
  echo "gdbserver.exe copied → $out_dir/"
else
  echo "" >&2
  echo "WARNING: Could not locate gdbserver.exe automatically." >&2
  echo "Manually place an i686 MinGW gdbserver.exe in:" >&2
  printf "  %s\n" "$out_dir" >&2
fi

cat <<MSG

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  HOST STEP — one terminal on your Windows host:

  cd  out\\package\\${preset}
  .\\gdbserver.exe :${port} keeperfx.exe -nointro -alex

  Leave that window open, then in VS Code (container) press F5 and
  choose "Container F5: Attach via gdbserver (Windows Host)".
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
MSG

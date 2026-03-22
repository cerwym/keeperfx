#!/usr/bin/env bash
# Fetch and parse the latest KeeperFX crash dump from the PS Vita.
#
# Runs the vita_crash Python tool. When executed inside a Vita SDK environment
# (devcontainer or Docker image), runs natively. Otherwise shells out to Docker.
# Downloads crash dumps via FTP, parses the .psp2dmp binary, symbolicates the
# stack using DWARF debug info, and produces text + HTML reports.
#
# Usage:
#   ./tools/vita-parse-crash.sh                          # auto-latest from Vita
#   ./tools/vita-parse-crash.sh --interactive             # pick a dump
#   ./tools/vita-parse-crash.sh --dump out/vita-dumps/crash.psp2dmp
#   ./tools/vita-parse-crash.sh --vita-ip 192.168.0.100
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DOCKER_ORG="${DOCKER_ORG:-cerwym}"
VITASDK_IMAGE="ghcr.io/$DOCKER_ORG/build-vitasdk:latest"
TOOLS_VOLUME="keeperfx-vita-tools"

# --- Parse arguments ---
VITA_IP="${VITA_IP:-192.168.0.66}"
VITA_PORT=1337
DUMP=""
INTERACTIVE=false
ALL=false
FORMAT="all"
OPEN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --vita-ip)   VITA_IP="$2";   shift 2 ;;
        --vita-port) VITA_PORT="$2"; shift 2 ;;
        --dump)      DUMP="$2";      shift 2 ;;
        --format)    FORMAT="$2";    shift 2 ;;
        --interactive) INTERACTIVE=true; shift ;;
        --all)       ALL=true;       shift ;;
        --open)      OPEN=true;      shift ;;
        -h|--help)
            echo "Usage: $(basename "$0") [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --vita-ip IP       Vita IP address (default: 192.168.0.66)"
            echo "  --vita-port PORT   Vita FTP port (default: 1337)"
            echo "  --dump FILE        Local .psp2dmp file (skip FTP download)"
            echo "  --format FORMAT    Output: text, html, or all (default: all)"
            echo "  --interactive      Select dump from FTP listing"
            echo "  --all              Show all dumps, not just eboot.bin ones"
            echo "  --open             Open HTML report in browser/VS Code preview"
            echo "  -h, --help         Show this help"
            exit 0
            ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# --- Build tool arguments ---
TOOL_ARGS=(
    "--vita-ip" "$VITA_IP"
    "--vita-port" "$VITA_PORT"
    "--format" "$FORMAT"
)

if [[ -n "$DUMP" ]]; then
    TOOL_ARGS+=("--dump" "$DUMP")
fi

$INTERACTIVE && TOOL_ARGS+=("--interactive")
$ALL && TOOL_ARGS+=("--all")
$OPEN && TOOL_ARGS+=("--open")

# --- Detect native Vita SDK environment ---
if command -v arm-vita-eabi-addr2line &>/dev/null && command -v python3 &>/dev/null; then
    # Ensure pyelftools is installed (baked into the vitasdk image;
    # this is a safety net for manual environments).
    if ! python3 -c 'import elftools' 2>/dev/null; then
        echo "Installing pyelftools..." >&2
        python3 -m pip install --quiet -r "$SCRIPT_DIR/vita_crash/requirements.txt" \
            || pip3 install --quiet -r "$SCRIPT_DIR/vita_crash/requirements.txt"
    fi

    cd "$REPO_ROOT"
    PYTHONPATH="${SCRIPT_DIR}${PYTHONPATH:+:$PYTHONPATH}" \
        exec python3 -m vita_crash "${TOOL_ARGS[@]}"
fi

# --- Fallback: run via Docker ---

DOCKER_VOLUMES=(
    "-v" "${TOOLS_VOLUME}:/tools"
    "-v" "${REPO_ROOT}:/src"
)

if [[ -n "$DUMP" ]]; then
    # Re-map --dump for Docker volume mount
    DUMP_REALPATH="$(realpath "$DUMP")"
    DUMP_DIR="$(dirname "$DUMP_REALPATH")"
    DUMP_NAME="$(basename "$DUMP_REALPATH")"
    # Override the --dump arg with the container path
    TOOL_ARGS=()
    TOOL_ARGS+=("--vita-ip" "$VITA_IP" "--vita-port" "$VITA_PORT" "--format" "$FORMAT")
    TOOL_ARGS+=("--dump" "/dumps/$DUMP_NAME")
    $INTERACTIVE && TOOL_ARGS+=("--interactive")
    $ALL && TOOL_ARGS+=("--all")
    DOCKER_VOLUMES+=("-v" "${DUMP_DIR}:/dumps")
fi

docker run --rm \
    "${DOCKER_VOLUMES[@]}" \
    -w /src \
    "$VITASDK_IMAGE" \
    bash /src/tools/vita_crash/docker-entry.sh "${TOOL_ARGS[@]}"

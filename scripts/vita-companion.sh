#!/usr/bin/env bash
# vita-companion.sh — Manage vitacompanion, noASLR, and PrincessLog on a PS Vita
#
# Prerequisites: curl, nc (netcat) on the host; FTP server on the Vita (port 1337).
# vitacompanion itself provides port 1337 once installed; for first-time setup
# use VitaShell's built-in FTP (press SELECT in VitaShell).
#
# Usage:
#   ./scripts/vita-companion.sh setup          # Download plugins + upload + patch taiHEN config
#   ./scripts/vita-companion.sh deploy-eboot   # Upload eboot.bin from latest build
#   ./scripts/vita-companion.sh launch         # Kill + relaunch KeeperFX
#   ./scripts/vita-companion.sh deploy-launch  # deploy-eboot + launch in one step
#   ./scripts/vita-companion.sh wait-for-gdb   # poll port 1234 until vita-uvdb stub is ready
#   ./scripts/vita-companion.sh reboot         # Reboot the Vita
#   ./scripts/vita-companion.sh fetch-logs     # Download kfx_boot/preinit/keeperfx logs to out/vita-logs/
#   ./scripts/vita-companion.sh log [port]     # Listen for PrincessLog output (default 8080)
#   ./scripts/vita-companion.sh screen <on|off>
#
# Environment:
#   VITA_IP   — Vita IP address (default: 192.168.0.66)
#   VITA_FTP  — FTP port         (default: 1337)
#   VITA_CMD  — Command port     (default: 1338)
#   VITA_PRESET — Build preset   (default: vita-reldebug)

set -euo pipefail

VITA_IP="${VITA_IP:-192.168.0.66}"
VITA_FTP="${VITA_FTP:-1337}"
VITA_CMD="${VITA_CMD:-1338}"
VITA_PRESET="${VITA_PRESET:-vita-reldebug}"
TITLE_ID="KFXV00001"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="$(cd "$SCRIPT_DIR/.." && pwd)"
PLUGIN_DIR="$WORKSPACE/out/vita-plugins"
BUILD_DIR="$WORKSPACE/out/build/$VITA_PRESET"

# Colours
RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'

info()  { echo -e "${CYAN}[vita]${NC} $*"; }
ok()    { echo -e "${GREEN}[vita]${NC} $*"; }
warn()  { echo -e "${YELLOW}[vita]${NC} $*"; }
err()   { echo -e "${RED}[vita]${NC} $*" >&2; }

vita_cmd() {
    # Send a command to vitacompanion's TCP command server
    local cmd="$1"
    if command -v nc &>/dev/null; then
        echo "$cmd" | nc -w 2 "$VITA_IP" "$VITA_CMD" 2>/dev/null || true
    else
        # Fallback: bash /dev/tcp (works without netcat installed)
        (echo "$cmd" > "/dev/tcp/${VITA_IP}/${VITA_CMD}") 2>/dev/null || true
    fi
}

ftp_upload() {
    # Upload a local file to a path on the Vita
    local local_file="$1"
    local remote_path="$2"
    if [[ ! -f "$local_file" ]]; then
        err "File not found: $local_file"
        return 1
    fi
    info "Uploading $(basename "$local_file") → $remote_path"
    curl -s --ftp-method nocwd --ftp-create-dirs \
        -T "$local_file" \
        "ftp://${VITA_IP}:${VITA_FTP}/${remote_path}" || {
        err "FTP upload failed. Is the Vita FTP server running on ${VITA_IP}:${VITA_FTP}?"
        err "For first-time setup, open VitaShell and press SELECT to start its FTP server."
        return 1
    }
}

ftp_download() {
    # Download a file from the Vita
    local remote_path="$1"
    local local_file="$2"
    mkdir -p "$(dirname "$local_file")"
    curl -s --ftp-method nocwd \
        -o "$local_file" \
        "ftp://${VITA_IP}:${VITA_FTP}/${remote_path}" || {
        err "FTP download failed: $remote_path"
        return 1
    }
}

# ── SETUP: Download plugin releases and upload to Vita ────────────────

cmd_setup() {
    info "Setting up vitacompanion + noASLR + PrincessLog on ${VITA_IP}..."
    mkdir -p "$PLUGIN_DIR"

    # --- Download vitacompanion.suprx ---
    if [[ ! -f "$PLUGIN_DIR/vitacompanion.suprx" ]]; then
        info "Downloading vitacompanion.suprx..."
        curl -sL -o "$PLUGIN_DIR/vitacompanion.suprx" \
            "https://github.com/devnoname120/vitacompanion/releases/download/1.00/vitacompanion.suprx"
        ok "Downloaded vitacompanion.suprx"
    else
        ok "vitacompanion.suprx already cached"
    fi

    # --- Download noASLR ---
    if [[ ! -f "$PLUGIN_DIR/noaslr.skprx" ]]; then
        info "Downloading noaslr.skprx..."
        curl -sL -o "$PLUGIN_DIR/noaslr.skprx" \
            "https://github.com/TeamFAPS/PSVita-RE-tools/raw/master/noASLR/release/noaslr.skprx"
        ok "Downloaded noaslr.skprx"
    else
        ok "noaslr.skprx already cached"
    fi

    # --- Download PrincessLog ---
    if [[ ! -f "$PLUGIN_DIR/net_logging_mgr.skprx" ]]; then
        info "Downloading PrincessLog (net_logging_mgr.skprx)..."
        curl -sL -o "$PLUGIN_DIR/net_logging_mgr.skprx" \
            "https://github.com/TeamFAPS/PSVita-RE-tools/raw/master/PrincessLog/build/net_logging_mgr.skprx"
        ok "Downloaded net_logging_mgr.skprx"
    else
        ok "net_logging_mgr.skprx already cached"
    fi

    if [[ ! -f "$PLUGIN_DIR/NetLoggingMgrSettings.vpk" ]]; then
        info "Downloading PrincessLog settings app..."
        curl -sL -o "$PLUGIN_DIR/NetLoggingMgrSettings.vpk" \
            "https://github.com/TeamFAPS/PSVita-RE-tools/raw/master/PrincessLog/build/NetLoggingMgrSettings.vpk"
        ok "Downloaded NetLoggingMgrSettings.vpk"
    else
        ok "NetLoggingMgrSettings.vpk already cached"
    fi

    info ""
    info "=== Plugin Upload ==="
    info "Make sure FTP is running on the Vita (VitaShell → SELECT, or vitacompanion if already installed)."
    info ""

    # Upload plugins to ur0:/tai/
    ftp_upload "$PLUGIN_DIR/vitacompanion.suprx"    "ur0:/tai/vitacompanion.suprx"
    ftp_upload "$PLUGIN_DIR/noaslr.skprx"           "ur0:/tai/noaslr.skprx"
    ftp_upload "$PLUGIN_DIR/net_logging_mgr.skprx"  "ur0:/tai/net_logging_mgr.skprx"

    # Upload PrincessLog settings VPK (user installs via VitaShell)
    ftp_upload "$PLUGIN_DIR/NetLoggingMgrSettings.vpk" "ux0:/data/NetLoggingMgrSettings.vpk"

    info ""
    info "=== taiHEN Config Update ==="
    info "Downloading current ur0:/tai/config.txt..."

    local tai_config="$PLUGIN_DIR/config.txt"
    local tai_backup="$PLUGIN_DIR/config.txt.backup"

    ftp_download "ur0:/tai/config.txt" "$tai_config"
    cp "$tai_config" "$tai_backup"
    ok "Backed up config.txt → $(basename "$tai_backup")"

    local modified=false

    # Add *KERNEL entries (noASLR + PrincessLog kernel module)
    if ! grep -q "noaslr.skprx" "$tai_config"; then
        # Ensure *KERNEL section exists
        if ! grep -q '^\*KERNEL' "$tai_config"; then
            echo -e "\n*KERNEL" >> "$tai_config"
        fi
        # Add after *KERNEL line
        sed -i '/^\*KERNEL/a ur0:tai/noaslr.skprx' "$tai_config"
        modified=true
        ok "Added noaslr.skprx to *KERNEL"
    else
        warn "noaslr.skprx already in config"
    fi

    if ! grep -q "net_logging_mgr.skprx" "$tai_config"; then
        if ! grep -q '^\*KERNEL' "$tai_config"; then
            echo -e "\n*KERNEL" >> "$tai_config"
        fi
        sed -i '/^\*KERNEL/a ur0:tai/net_logging_mgr.skprx' "$tai_config"
        modified=true
        ok "Added net_logging_mgr.skprx to *KERNEL"
    else
        warn "net_logging_mgr.skprx already in config"
    fi

    # Add *main entry (vitacompanion user module — loads for all apps)
    if ! grep -q "vitacompanion.suprx" "$tai_config"; then
        # Ensure *main section exists
        if ! grep -q '^\*main' "$tai_config"; then
            echo -e "\n*main" >> "$tai_config"
        fi
        sed -i '/^\*main/a ur0:tai/vitacompanion.suprx' "$tai_config"
        modified=true
        ok "Added vitacompanion.suprx to *main"
    else
        warn "vitacompanion.suprx already in config"
    fi

    if $modified; then
        info "Uploading updated config.txt..."
        ftp_upload "$tai_config" "ur0:/tai/config.txt"
        ok "taiHEN config updated on Vita"
    else
        ok "No config changes needed"
    fi

    echo ""
    ok "=== Setup Complete ==="
    info "1. Install PrincessLog settings app: open VitaShell → navigate to"
    info "   ux0:/data/NetLoggingMgrSettings.vpk → install it"
    info "2. Launch NetLoggingMgrSettings → set your PC's IP & port (default 8080) → Save"
    info "3. Reboot the Vita (run: ./scripts/vita-companion.sh reboot)"
    info "4. After reboot, vitacompanion FTP+commands will be active automatically"
    info "5. To see logs: ./scripts/vita-companion.sh log"
}

# ── DEPLOY: Upload eboot.bin to the Vita app directory ────────────────

cmd_deploy_eboot() {
    local self_file="$BUILD_DIR/keeperfx.self"
    if [[ ! -f "$self_file" ]]; then
        err "Build output not found: $self_file"
        err "Run 'Build Vita $(echo "$VITA_PRESET" | sed 's/vita-//' | sed 's/.*/\u&/')' task first."
        return 1
    fi
    # eboot.bin on the Vita is the .self renamed
    ftp_upload "$self_file" "ux0:/app/${TITLE_ID}/eboot.bin"
    ok "eboot.bin deployed to ux0:/app/${TITLE_ID}/"
}

# ── KILL: Kill running app on the Vita ──────────────────────────────

cmd_kill() {
    info "Killing running apps on ${VITA_IP}..."
    vita_cmd "destroy"
    ok "Kill command sent"
}

# ── LAUNCH: Kill running apps and launch KeeperFX ────────────────────

cmd_launch() {
    info "Killing running apps..."
    vita_cmd "destroy"
    sleep 1
    info "Launching ${TITLE_ID}..."
    vita_cmd "launch ${TITLE_ID}"
    ok "Launch command sent"
}

# ── DEPLOY + LAUNCH ──────────────────────────────────────────────────

cmd_deploy_launch() {
    # If the GDB stub port is already open the game is sitting there blocked —
    # kill it first so the FTP upload doesn't fail on a locked eboot.bin.
    if nc -z -w1 "${VITA_IP}" 1234 2>/dev/null; then
        warn "GDB stub already listening — killing stale instance before upload..."
        vita_cmd "destroy"
        sleep 2
    fi
    cmd_deploy_eboot
    cmd_launch
}

# ── WAIT FOR GDB STUB ────────────────────────────────────────────────

cmd_wait_for_gdb() {
    local port="${1:-1234}"
    local timeout_sec="${2:-60}"
    info "Waiting for vita-uvdb GDB stub on ${VITA_IP}:${port} (timeout ${timeout_sec}s)..."
    local i=0
    while [ $i -lt $timeout_sec ]; do
        if nc -z -w1 "${VITA_IP}" "${port}" 2>/dev/null; then
            ok "GDB stub is ready — attach with F5"
            return 0
        fi
        sleep 1
        i=$((i + 1))
    done
    err "Timed out after ${timeout_sec}s waiting for GDB stub on ${VITA_IP}:${port}"
    exit 1
}

# ── REBOOT ───────────────────────────────────────────────────────────

cmd_reboot() {
    info "Rebooting Vita..."
    vita_cmd "reboot"
    ok "Reboot command sent"
}

# ── LOG: Listen for PrincessLog network output ───────────────────────

cmd_log() {
    local port="${1:-8080}"
    info "Listening for PrincessLog output on port ${port}..."
    info "Make sure PrincessLog is configured to send to this machine's IP on port ${port}"
    info "Press Ctrl+C to stop"
    echo "---"
    if command -v nc &>/dev/null; then
        nc -kl "$port"
    elif command -v socat &>/dev/null; then
        socat TCP-LISTEN:"$port",reuseaddr,fork -
    else
        err "No netcat or socat found. Install one with: apt-get install -y netcat-openbsd"
        err "Or run: socat TCP-LISTEN:${port},reuseaddr,fork -"
        return 1
    fi
}

# ── FETCH-LOGS: Pull device log files from Vita ──────────────────────

cmd_fetch_logs() {
    local out_dir="$WORKSPACE/out/vita-logs"
    mkdir -p "$out_dir"
    info "Fetching log files from ${VITA_IP}:${VITA_FTP} → out/vita-logs/"
    local failed=0
    for remote in \
        "ux0:/data/vitaGL.log" \
        "ux0:/data/keeperfx/kfx_boot.log" \
        "ux0:/data/keeperfx/kfx_preinit.log" \
        "ux0:/data/keeperfx/keeperfx.log" \
        "ux0:/data/keeperfx/profiler.log"
    do
        local fname
        fname="$(basename "$remote")"
        info "  $remote ..."
        if curl -s --ftp-method nocwd --connect-timeout 8 -m 15 \
               -o "$out_dir/$fname" \
               "ftp://${VITA_IP}:${VITA_FTP}/${remote}" 2>&1; then
            if [[ -s "$out_dir/$fname" ]]; then
                ok "  → saved to out/vita-logs/$fname ($(wc -c < "$out_dir/$fname") bytes)"
            else
                warn "  → empty or not found: $remote"
                failed=$((failed + 1))
            fi
        else
            err "  → FTP download failed: $remote"
            failed=$((failed + 1))
        fi
    done
    if [[ $failed -eq 0 ]]; then
        ok "All logs saved to out/vita-logs/"
        echo ""
        echo "=== vitaGL.log ==="
        cat "$out_dir/vitaGL.log" 2>/dev/null || echo '(empty)'
        echo ""
        echo "=== kfx_boot.log ==="
        cat "$out_dir/kfx_boot.log" 2>/dev/null || echo '(empty)'
        echo ""
        echo "=== kfx_preinit.log ==="
        cat "$out_dir/kfx_preinit.log" 2>/dev/null || echo '(empty)'
    else
        err "$failed log file(s) missing. Make sure:"
        err "  - Vita is powered on"
        err "  - KeeperFX has been launched at least once (creates the log files)"
        err "  - VitaCompanion FTP is running on ${VITA_IP}:${VITA_FTP}"
        return 1
    fi
}

# ── SCREEN ───────────────────────────────────────────────────────────

cmd_screen() {
    local state="${1:-}"
    if [[ "$state" != "on" && "$state" != "off" ]]; then
        err "Usage: $0 screen <on|off>"
        return 1
    fi
    info "Screen ${state}..."
    vita_cmd "screen ${state}"
    ok "Screen command sent"
}

# ── SETUP-UVDB: Install kubridge on Vita for vita-uvdb GDB stub ──────────────
# vita-uvdb (https://github.com/sleirsgoevy/vita-uvdb) embeds a GDB stub inside
# the keeperfx binary.  It requires kubridge.skprx loaded as a *KERNEL plugin.
# Run this once per Vita — no rerun needed unless kubridge is removed.

cmd_setup_uvdb() {
    info "Setting up kubridge v0.3.1_hotfix for vita-uvdb on ${VITA_IP}..."
    mkdir -p "$PLUGIN_DIR"

    local KUBRIDGE_URL="https://github.com/bythos14/kubridge/releases/download/v0.3.1_hotfix/kubridge.skprx"
    local kubridge_dest="$PLUGIN_DIR/kubridge.skprx"

    if [[ ! -f "$kubridge_dest" ]]; then
        info "Downloading kubridge.skprx..."
        curl -fsSL -o "$kubridge_dest" "$KUBRIDGE_URL"
        ok "Downloaded kubridge.skprx"
    else
        ok "kubridge.skprx already cached"
    fi

    info ""
    info "=== Plugin Upload ==="
    info "Make sure FTP is running on the Vita (vitacompanion or VitaShell → SELECT)."
    ftp_upload "$kubridge_dest" "ur0:/tai/kubridge.skprx"

    info "=== taiHEN Config Update ==="
    local tai_config="$PLUGIN_DIR/config.txt"
    local tai_backup="$PLUGIN_DIR/config.txt.backup.uvdb"
    ftp_download "ur0:/tai/config.txt" "$tai_config"
    cp "$tai_config" "$tai_backup"
    ok "Backed up config.txt → $(basename "$tai_backup")"

    if ! grep -q "kubridge.skprx" "$tai_config"; then
        if ! grep -q '^\*KERNEL' "$tai_config"; then
            echo -e "\n*KERNEL" >> "$tai_config"
        fi
        sed -i '/^\*KERNEL/a ur0:tai/kubridge.skprx' "$tai_config"
        info "Uploading updated config.txt..."
        ftp_upload "$tai_config" "ur0:/tai/config.txt"
        ok "Added kubridge.skprx to *KERNEL"
    else
        warn "kubridge.skprx already in config — no change"
    fi

    echo ""
    ok "=== Setup Complete ==="
    info "1. Reboot the Vita to load kubridge:  ./scripts/vita-companion.sh reboot"
    info "2. Rebuild the vitasdk Docker image (once, for kubridge_stub.a):"
    info "   docker compose -f build/docker/compose.yml build vitasdk"
    info "3. Build the vita-gdb preset:"
    info "   cmake --preset vita-gdb && cmake --build --preset vita-gdb"
    info "4. Deploy and launch, then attach GDB from VS Code (F5 with 'Debug KeeperFX (Vita GDB)')."
    info "   The game will block at startup until GDB connects on port 1234."
}

# ── MAIN ─────────────────────────────────────────────────────────────

usage() {
    echo "Usage: $0 <command> [args]"
    echo ""
    echo "Commands:"
    echo "  setup            Download plugins, upload to Vita, patch taiHEN config"
    echo "  setup-uvdb       Install kubridge on Vita for vita-uvdb GDB stub (one-time)"
    echo "  deploy-eboot     Upload eboot.bin from build output to Vita"
    echo "  kill             Kill the running app on the Vita
  launch           Kill running apps + launch KeeperFX"
    echo "  deploy-launch    Deploy eboot + launch (combined)"
    echo "  wait-for-gdb     Poll port 1234 until vita-uvdb GDB stub is ready"
    echo "  reboot           Reboot the Vita"
    echo "  fetch-logs       Download kfx_boot.log, kfx_preinit.log, keeperfx.log → out/vita-logs/"
    echo "  log [port]       Listen for PrincessLog output (default: 8080)"
    echo "  screen <on|off>  Turn Vita screen on or off"
    echo ""
    echo "Environment variables:"
    echo "  VITA_IP=$VITA_IP  VITA_FTP=$VITA_FTP  VITA_CMD=$VITA_CMD  VITA_PRESET=$VITA_PRESET"
}

case "${1:-}" in
    setup)          cmd_setup ;;
    setup-uvdb)     cmd_setup_uvdb ;;
    deploy-eboot)   cmd_deploy_eboot ;;
    kill)           cmd_kill ;;
    launch)         cmd_launch ;;
    deploy-launch)  cmd_deploy_launch ;;
    wait-for-gdb)   cmd_wait_for_gdb "${2:-1234}" "${3:-60}" ;;
    reboot)         cmd_reboot ;;
    fetch-logs)     cmd_fetch_logs ;;
    log)            cmd_log "${2:-8080}" ;;
    screen)         cmd_screen "${2:-}" ;;
    -h|--help|"")   usage ;;
    *)              err "Unknown command: $1"; usage; exit 1 ;;
esac

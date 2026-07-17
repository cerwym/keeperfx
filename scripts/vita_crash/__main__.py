"""
CLI entry point for the Vita crash analysis tool.

Usage:
    python -m vita_crash [options]

Run from Docker:
    docker compose -f build/docker/compose.yml run --rm vitasdk \
        python3 /src/scripts/vita_crash --dump /src/out/vita-dumps/crash.psp2dmp
"""

import argparse
import os
import sys
from typing import Optional

from .core_parser import parse_core_dump
from .symbolicate import (
    build_stack_trace, heuristic_stack_walk, StackTrace,
    _find_elf, _find_code_segment, symbolicate_addresses,
)
from .report import format_text, format_html


def _find_elf_auto(build_dir: str) -> Optional[str]:
    """Auto-detect the most recently built Vita ELF."""
    for preset in ("vita-debug", "vita-reldebug", "vita-release"):
        path = os.path.join(build_dir, preset, "keeperfx")
        if os.path.isfile(path):
            return path
    return None


def _resolve_elf(args_elf: Optional[str], source_root: str) -> Optional[str]:
    """Resolve ELF path from argument or auto-detection."""
    if args_elf:
        if os.path.isfile(args_elf):
            return args_elf
        print(f"ERROR: ELF not found: {args_elf}", file=sys.stderr)
        return None

    # Auto-detect from build output
    build_dir = os.path.join(source_root, "out", "build")
    candidates = []
    for preset in ("vita-debug", "vita-reldebug", "vita-release"):
        path = os.path.join(build_dir, preset, "keeperfx")
        if os.path.isfile(path):
            candidates.append(path)

    if not candidates:
        print("WARNING: No Vita ELF found in out/build/. "
              "Symbolication disabled. Build first or use --elf.",
              file=sys.stderr)
        return None

    # Pick most recently modified
    best = max(candidates, key=os.path.getmtime)
    print(f"Using ELF: {best}", file=sys.stderr)
    return best


def main():
    parser = argparse.ArgumentParser(
        prog="vita_crash",
        description="PS Vita crash dump analysis tool for KeeperFX",
    )
    parser.add_argument("--vita-ip", default=os.environ.get("VITA_IP", "192.168.0.66"),
                        help="Vita IP address (default: $VITA_IP env var, or 192.168.0.66)")
    parser.add_argument("--vita-port", type=int, default=1337,
                        help="Vita FTP port (default: 1337)")
    parser.add_argument("--dump", metavar="FILE",
                        help="Local .psp2dmp file (skip FTP download)")
    parser.add_argument("--elf", metavar="FILE",
                        help="Path to ELF binary (auto-detected from out/build/)")
    parser.add_argument("--interactive", action="store_true",
                        help="Prompt to select dump from FTP listing")
    parser.add_argument("--all", action="store_true",
                        help="Show all dumps, not just eboot.bin ones")
    parser.add_argument("--format", choices=("text", "html", "all"), default="all",
                        help="Output format (default: all)")
    parser.add_argument("--output", metavar="DIR", default="out/vita-dumps",
                        help="Output directory (default: out/vita-dumps/)")
    parser.add_argument("--stack-depth", type=int, default=32,
                        help="Max stack frames to unwind (default: 32)")
    parser.add_argument("--crash-log", metavar="FILE",
                        help="Also parse on-device crash.log")
    parser.add_argument("--source-root", metavar="DIR",
                        help="Source root for context display (default: auto)")
    parser.add_argument("--open", action="store_true",
                        help="Open HTML report in browser/VS Code preview after generation")
    parser.add_argument("--addr2line", default="arm-vita-eabi-addr2line",
                        help="Path to addr2line (default: arm-vita-eabi-addr2line)")

    args = parser.parse_args()

    # Determine source root (repo root)
    source_root = args.source_root
    if not source_root:
        # Walk up from this script's directory looking for root CMakeLists.txt
        # (one containing "project(") to distinguish from subdirectory ones.
        d = os.path.dirname(os.path.abspath(__file__))
        while d != os.path.dirname(d):
            d = os.path.dirname(d)
            cml = os.path.join(d, "CMakeLists.txt")
            if os.path.isfile(cml):
                with open(cml) as f:
                    if "project(" in f.read():
                        source_root = d
                        break
        if not source_root:
            source_root = os.getcwd()

    # Get the dump file
    dump_path = args.dump
    auto_downloaded = False
    if not dump_path:
        dump_path = _download_dump(args)
        auto_downloaded = True
        if not dump_path:
            sys.exit(1)

    if not os.path.isfile(dump_path):
        print(f"ERROR: Dump file not found: {dump_path}", file=sys.stderr)
        sys.exit(1)

    # If we auto-downloaded the latest dump, refresh logs from Vita to match
    if auto_downloaded:
        print("Auto-refreshing logs from Vita to match latest crash dump...", file=sys.stderr)
        _fetch_latest_logs_from_vita(source_root, args)

    # Parse the core dump
    print(f"Parsing: {dump_path}", file=sys.stderr)
    try:
        dump = parse_core_dump(dump_path)
    except Exception as e:
        print(f"ERROR: Failed to parse dump: {e}", file=sys.stderr)
        sys.exit(1)

    if not dump.threads:
        print("WARNING: No thread info found in dump", file=sys.stderr)

    # Resolve ELF
    elf_path = _resolve_elf(args.elf, source_root)

    # Build stack traces for all threads
    traces = []
    for thread in dump.threads:
        trace = build_stack_trace(
            dump, thread, elf_path,
            max_depth=args.stack_depth,
            addr2line=args.addr2line,
        )
        traces.append(trace)

    # Resolve register values that are code addresses to function names
    register_symbols = {}
    ct = dump.crashed_thread
    if ct and ct.regs and elf_path:
        r = ct.regs
        code_addrs = []
        for i in range(13):
            if 0x81000000 <= r.r[i] < 0x82000000:
                code_addrs.append(r.r[i])
        for val in (r.sp, r.lr, r.pc):
            if 0x81000000 <= val < 0x82000000:
                code_addrs.append(val)
        if code_addrs:
            register_symbols = symbolicate_addresses(
                code_addrs, elf_path, args.addr2line)

    # Also try to parse crash.log if available
    crash_log_content = None
    crash_log_path = args.crash_log
    if not crash_log_path and not args.dump:
        # Try to download from Vita
        crash_log_path = _download_crash_log(args)
    if crash_log_path and os.path.isfile(crash_log_path):
        with open(crash_log_path, "r", errors="replace") as f:
            crash_log_content = f.read()
        print(f"On-device crash log: {crash_log_path}", file=sys.stderr)

    # Harvest log files written by "Fetch Vita Logs" task (out/vita-logs/)
    vita_logs: dict = {}
    logs_dir = os.path.join(source_root, "out", "vita-logs")
    if os.path.isdir(logs_dir):
        for fname in ("kfx_boot.log", "kfx_preinit.log", "keeperfx.log", "profiler.log", "vitaGL.log"):
            fpath = os.path.join(logs_dir, fname)
            if os.path.isfile(fpath) and os.path.getsize(fpath) > 0:
                with open(fpath, "r", errors="replace") as f:
                    vita_logs[fname] = f.read()
                print(f"Log harvested: {fname} ({os.path.getsize(fpath):,} bytes)", file=sys.stderr)

    # Generate reports
    os.makedirs(args.output, exist_ok=True)

    if args.format in ("text", "all"):
        text = format_text(dump, traces, source_root, register_symbols,
                           vita_logs=vita_logs or None)
        text_path = os.path.join(args.output, "crash_report.txt")
        with open(text_path, "w", encoding="utf-8") as f:
            f.write(text)
            if crash_log_content:
                f.write("\n\n═══ ON-DEVICE CRASH LOG ═══\n")
                f.write(crash_log_content)
        print(f"\nText report saved: {text_path}", file=sys.stderr)

    html_path = None
    if args.format in ("html", "all"):
        html_content = format_html(dump, traces, source_root, register_symbols,
                                   vita_logs=vita_logs or None)
        html_path = os.path.join(args.output, "crash_report.html")
        with open(html_path, "w", encoding="utf-8") as f:
            f.write(html_content)
        print(f"HTML report saved: {html_path}", file=sys.stderr)

    # Summary
    ct = dump.crashed_thread
    if ct:
        print(f"\nCrash: {ct.stop_reason_str} in thread {ct.name} at PC=0x{ct.pc:08x}",
              file=sys.stderr)

    # Open HTML report in browser / VS Code Simple Browser
    if args.open and html_path and os.path.isfile(html_path):
        _open_html_preview(html_path)


def _open_html_preview(html_path: str):
    """Serve the HTML on a temporary local port and open in the browser."""
    import http.server
    import threading
    import webbrowser

    abs_path = os.path.abspath(html_path)
    serve_dir = os.path.dirname(abs_path)
    filename = os.path.basename(abs_path)

    handler = http.server.SimpleHTTPRequestHandler
    srv = http.server.HTTPServer(("127.0.0.1", 0), handler)
    port = srv.server_address[1]

    orig_dir = os.getcwd()
    os.chdir(serve_dir)

    def serve():
        for _ in range(10):
            srv.handle_request()
        os.chdir(orig_dir)

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()

    url = f"http://localhost:{port}/{filename}"
    print(f"Opening preview: {url}", file=sys.stderr)
    webbrowser.open(url)

    thread.join(timeout=10)


def _download_dump(args) -> Optional[str]:
    """Download a dump file from the Vita via FTP."""
    try:
        from .ftp_client import connect, list_dumps, download_dump
    except ImportError as e:
        print(f"FTP support unavailable: {e}", file=sys.stderr)
        return None

    try:
        print(f"Connecting to {args.vita_ip}:{args.vita_port}...", file=sys.stderr)
        ftp = connect(args.vita_ip, args.vita_port)
    except Exception as e:
        print(f"ERROR: Cannot connect to Vita FTP: {e}", file=sys.stderr)
        print("Use --dump to analyze a local dump file instead.", file=sys.stderr)
        return None

    try:
        dumps = list_dumps(ftp, include_all=args.all)
        if not dumps:
            print("No .psp2dmp files found on Vita.", file=sys.stderr)
            return None

        if args.interactive:
            print("\nAvailable dumps:", file=sys.stderr)
            for i, d in enumerate(dumps):
                print(f"  [{i}] {d.filename}  ({d.size} bytes)", file=sys.stderr)
            try:
                choice = input(f"Select [0-{len(dumps)-1}, default={len(dumps)-1}]: ")
                idx = int(choice) if choice.strip() else len(dumps) - 1
            except (ValueError, EOFError):
                idx = len(dumps) - 1
            selected = dumps[idx]
        else:
            # Auto-latest: pick the last one (list_dumps sorts by Unix timestamp)
            selected = dumps[-1]

        print(f"Downloading: {selected.filename}", file=sys.stderr)
        return download_dump(ftp, selected.filename, args.output)
    finally:
        ftp.quit()


def _download_crash_log(args) -> Optional[str]:
    """Try to download crash.log from the Vita."""
    try:
        from .ftp_client import connect, download_crash_log
        ftp = connect(args.vita_ip, args.vita_port)
        path = download_crash_log(ftp, args.output)
        ftp.quit()
        return path
    except Exception:
        return None


def _fetch_latest_logs_from_vita(source_root: str, args) -> None:
    """Fetch latest logs from Vita via vita-companion.sh script.
    
    This ensures that harvested logs correspond to the crash event being analyzed,
    rather than using stale logs from a previous session.
    """
    import subprocess
    
    script_path = os.path.join(source_root, "tools", "vita-companion.sh")
    if not os.path.isfile(script_path):
        print("WARNING: vita-companion.sh not found, skipping log refresh", file=sys.stderr)
        return
    
    try:
        print("Running: bash scripts/vita-companion.sh fetch-logs", file=sys.stderr)
        result = subprocess.run(
            ["bash", script_path, "fetch-logs"],
            cwd=source_root,
            capture_output=True,
            text=True,
            timeout=60
        )
        if result.returncode != 0:
            print(f"WARNING: fetch-logs failed (exit code {result.returncode})", file=sys.stderr)
            if result.stderr:
                print(result.stderr, file=sys.stderr)
        else:
            print("Logs refreshed from Vita", file=sys.stderr)
    except subprocess.TimeoutExpired:
        print("WARNING: fetch-logs timed out after 60s", file=sys.stderr)
    except Exception as e:
        print(f"WARNING: Failed to fetch logs from Vita: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()

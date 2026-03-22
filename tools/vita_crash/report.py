"""
Output formatters for crash reports — text and HTML.
"""

import base64
import html
import os
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from .core_parser import CoreDump, Thread, ThreadRegisters
from .sce_error_codes import SCE_ERROR_CODES, SCE_ERROR_DESCRIPTIONS
from .symbolicate import StackTrace, StackFrame, SourceLocation


# ── Profiler Data Extraction ─────────────────────────────────────────────

def _extract_profiler_frames(trace: 'StackTrace') -> List['StackFrame']:
    """Extract stack profiler-related frames from the crash stack trace.
    
    Returns frames from StackMonitor_*, snprintf, fprintf, malloc—showing where
    the profiler code crashed and what it was sampling.
    """
    profiler_keywords = [
        "StackMonitor", "snprintf", "fprintf", "malloc", "vsnprintf",
        "prepare_file_path", "load_texture_map", "load_level_file"
    ]
    
    return [f for f in trace.frames
            if f.source and any(kw in (f.source.function or "") for kw in profiler_keywords)]


def _extract_stack_measurements(dump: CoreDump) -> Optional[Dict[str, any]]:
    """Extract stack measurements from crash data.
    
    Calculates stack usage from:
    - Vita main thread stack: 4MB starting at ~0x836xxxxx (grows downward)
    - Current SP from crashed thread registers
    - Stack frames in call trace
    
    Returns dict with measurements:
    - total_size: 4 MB (standard Vita main thread stack)
    - current_sp: SP value at crash
    - estimated_used: estimated bytes consumed (calculated from SP)
    - stack_depth: estimated depth from register analysis
    """
    ct = dump.crashed_thread
    if not ct or not ct.regs:
        return None
    
    # Vita main thread stack is 4 MB (sceUserMainThreadStackSize)
    VITA_MAIN_STACK_SIZE = 4 * 1024 * 1024  # 4 MB
    VITA_STACK_TOP = 0x83600000  # Typical stack base (grows toward lower addresses)
    
    measurements = {
        "total_size": VITA_MAIN_STACK_SIZE,
        "current_sp": ct.regs.sp,
        "stack_base": VITA_STACK_TOP,
    }
    
    # Estimate used stack: distance from SP to stack base
    if VITA_STACK_TOP and ct.regs.sp < VITA_STACK_TOP:
        estimated_used = VITA_STACK_TOP - ct.regs.sp
        measurements["estimated_used"] = estimated_used
        measurements["remaining"] = VITA_MAIN_STACK_SIZE - estimated_used
        measurements["usage_percent"] = (estimated_used * 100.0) / VITA_MAIN_STACK_SIZE
    
    return measurements


# ── ARM register decoding (per ARM DDI 0406C §B1.3, §B4.1.58) ──────────

# CPSR M[4:0] → mode name (Table B1-1, ARM DDI 0406C §B1.3)
_ARM_MODES = {
    0b10000: "USR",
    0b10001: "FIQ",
    0b10010: "IRQ",
    0b10011: "SVC",
    0b10110: "MON",
    0b10111: "ABT",
    0b11010: "HYP",
    0b11011: "UND",
    0b11111: "SYS",
}

# CPSR J:T → instruction set (§A2.5.1)
_ISET_STATE = {
    (0, 0): "ARM",
    (0, 1): "Thumb",
    (1, 0): "Jazelle",
    (1, 1): "ThumbEE",
}

# FPSCR RMode[23:22] → rounding mode name (§B4.1.58)
_FP_RMODE = {
    0b00: "RN",   # Round to Nearest
    0b01: "RP",   # Round towards Plus Infinity
    0b10: "RM",   # Round towards Minus Infinity
    0b11: "RZ",   # Round towards Zero
}


def _decode_cpsr(cpsr: int) -> str:
    """Decode CPSR into a human-readable summary (ARM DDI 0406C §B1.3.3)."""
    # Condition flags [31:28]
    n = bool(cpsr & (1 << 31))
    z = bool(cpsr & (1 << 30))
    c = bool(cpsr & (1 << 29))
    v = bool(cpsr & (1 << 28))
    q = bool(cpsr & (1 << 27))  # Saturation flag

    # Instruction set state: J[24], T[5]
    j = (cpsr >> 24) & 1
    t = (cpsr >> 5) & 1
    iset = _ISET_STATE.get((j, t), "?")

    # Endianness E[9]
    endian = "BE" if cpsr & (1 << 9) else "LE"

    # Interrupt masks A[8], I[7], F[6]
    a = bool(cpsr & (1 << 8))
    irq = bool(cpsr & (1 << 7))
    fiq = bool(cpsr & (1 << 6))

    # GE[19:16] for SIMD
    ge = (cpsr >> 16) & 0xF

    # IT[7:2] = CPSR[15:10], IT[1:0] = CPSR[26:25]
    it_hi = (cpsr >> 10) & 0x3F
    it_lo = (cpsr >> 25) & 0x3
    it_state = (it_hi << 2) | it_lo

    # Mode M[4:0]
    mode = _ARM_MODES.get(cpsr & 0x1F, f"0x{cpsr & 0x1F:02x}")

    parts = [f"{mode}/{iset}"]
    parts.append(f"NZCVQ={''.join(str(int(f)) for f in (n, z, c, v, q))}")
    parts.append(endian)
    if it_state:
        parts.append(f"IT=0x{it_state:02x}")
    if ge:
        parts.append(f"GE=0x{ge:x}")
    masks = []
    if a: masks.append("A")
    if irq: masks.append("I")
    if fiq: masks.append("F")
    if masks:
        parts.append(f"mask:{','.join(masks)}")
    return "  ".join(parts)


def _decode_fpscr(fpscr: int) -> str:
    """Decode FPSCR into a human-readable summary (ARM DDI 0406C §B4.1.58)."""
    # Control bits
    dn = bool(fpscr & (1 << 25))   # Default NaN
    fz = bool(fpscr & (1 << 24))   # Flush-to-zero
    rmode = _FP_RMODE.get((fpscr >> 22) & 3, "?")
    ahp = bool(fpscr & (1 << 26))  # Alternative half-precision

    # Cumulative exception flags [7, 4:0]
    exc_bits = [
        ("IOC", fpscr & 1),          # Invalid Operation
        ("DZC", fpscr & 2),          # Division by Zero
        ("OFC", fpscr & 4),          # Overflow
        ("UFC", fpscr & 8),          # Underflow
        ("IXC", fpscr & 0x10),       # Inexact
        ("IDC", fpscr & 0x80),       # Input Denormal
    ]
    raised = [name for name, bit in exc_bits if bit]

    parts = [f"DN={int(dn)}  FZ={int(fz)}  {rmode}"]
    if ahp:
        parts.append("AHP")
    if raised:
        parts.append(f"exceptions:{','.join(raised)}")
    return "  ".join(parts)


# ── ARM calling convention (AAPCS) ──────────────────────────────────────

_REG_ROLES = {
    "R0": "argument 1 / return value",
    "R1": "argument 2 / return value (64-bit)",
    "R2": "argument 3",
    "R3": "argument 4",
    "R4": "callee-saved local",
    "R5": "callee-saved local",
    "R6": "callee-saved local",
    "R7": "callee-saved local (frame pointer in Thumb)",
    "R8": "callee-saved local",
    "R9": "callee-saved local (platform register)",
    "R10": "callee-saved local",
    "R11": "callee-saved local (frame pointer in ARM)",
    "R12": "scratch (intra-procedure call)",
    "SP": "stack pointer",
    "LR": "return address (link register)",
    "PC": "program counter (current instruction)",
}

_SENTINELS = {
    0xDEADBEEF: "debug fill — register never written",
    0xCCCCCCCC: "MSVC uninit stack marker",
    0xBAADF00D: "LocalAlloc uninit heap marker",
    0xFEEEFEEE: "HeapFree marker",
    0xABABABAB: "HeapAlloc guard bytes",
    0x7F80DEAD: "VFP uninit sentinel",
    0x7FF8DEAD: "VFP NaN sentinel",
}


def _classify_register(name: str, value: int, dump: CoreDump,
                       reg_symbols: Optional[Dict[int, SourceLocation]] = None
                       ) -> Tuple[str, str, str]:
    """Classify a register value into (category, short_label, tooltip_text).

    Categories: null, code, stack, heap, kernel, sentinel, sce_error,
                small_int, unknown.
    """
    role = _REG_ROLES.get(name, "")

    # NULL
    if value == 0:
        return ("null", "NULL",
                f"{name} = 0x00000000 — NULL pointer.\nRole: {role}" if role
                else f"{name} = 0x00000000 — NULL pointer.")

    # Sentinel values (debug fill patterns)
    if value in _SENTINELS:
        desc = _SENTINELS[value]
        return ("sentinel", desc.split(" — ")[0] if " — " in desc else "sentinel",
                f"{name} = 0x{value:08x} — {desc}.\nRole: {role}" if role
                else f"{name} = 0x{value:08x} — {desc}.")

    # SCE error code (0x80XXXXXX / 0xC0XXXXXX)
    if _is_sce_error(value):
        decoded = _decode_sce_error(value) or f"SCE error 0x{value:08x}"
        return ("sce-error", "SCE error",
                f"{name} = 0x{value:08x} — {decoded}.\nRole: {role}" if role
                else f"{name} = 0x{value:08x} — {decoded}.")

    # Code address (0x81000000 – 0x82000000)
    if 0x81000000 <= value < 0x82000000:
        module_ref = dump.resolve_address(value) or "code address"
        sym = ""
        if reg_symbols and value in reg_symbols:
            loc = reg_symbols[value]
            sym = f"\nFunction: {loc.function}"
            if loc.file and loc.file != "??":
                sym += f"  ({loc.file}:{loc.line})"
        return ("code", module_ref,
                f"{name} = 0x{value:08x} — {module_ref}{sym}.\nRole: {role}" if role
                else f"{name} = 0x{value:08x} — {module_ref}{sym}.")

    # Stack address (0x83000000 – 0x84000000)
    if 0x83000000 <= value < 0x84000000:
        return ("stack", "stack address",
                f"{name} = 0x{value:08x} — stack memory.\nRole: {role}" if role
                else f"{name} = 0x{value:08x} — stack memory.")

    # Heap address (0x84000000 – 0x88000000)
    if 0x84000000 <= value < 0x88000000:
        return ("heap", "heap address",
                f"{name} = 0x{value:08x} — heap memory.\nRole: {role}" if role
                else f"{name} = 0x{value:08x} — heap memory.")

    # Kernel address (0xE0000000+)
    if value >= 0xE0000000:
        module_ref = dump.resolve_address(value) or "kernel address"
        return ("kernel", module_ref,
                f"{name} = 0x{value:08x} — {module_ref} (kernel).\nRole: {role}" if role
                else f"{name} = 0x{value:08x} — {module_ref} (kernel).")

    # Small integer (0x0001 – 0xFFFF)
    if 0 < value <= 0xFFFF:
        return ("small-int", f"integer {value}",
                f"{name} = {value} (0x{value:x}) — small integer.\nRole: {role}" if role
                else f"{name} = {value} (0x{value:x}) — small integer.")

    # Unknown / unclassified
    return ("unknown", "",
            f"{name} = 0x{value:08x}.\nRole: {role}" if role
            else f"{name} = 0x{value:08x}.")


def _cpsr_plain_english(cpsr: int) -> str:
    """One-line plain-English summary of CPSR for non-ARM developers."""
    mode = _ARM_MODES.get(cpsr & 0x1F, "unknown")
    mode_desc = {"USR": "normal user program", "SYS": "system mode",
                 "SVC": "supervisor call handler", "IRQ": "interrupt handler",
                 "FIQ": "fast interrupt handler", "ABT": "abort handler",
                 "UND": "undefined instruction handler", "HYP": "hypervisor",
                 "MON": "secure monitor"}.get(mode, mode)
    j = (cpsr >> 24) & 1
    t = (cpsr >> 5) & 1
    iset = _ISET_STATE.get((j, t), ("?",))
    iset_desc = {"ARM": "standard (ARM) instructions",
                 "Thumb": "compact (Thumb) instructions",
                 "Jazelle": "Java bytecode (Jazelle)",
                 "ThumbEE": "Thumb with extensions"}.get(iset, iset)
    endian = "big-endian" if cpsr & (1 << 9) else "little-endian"
    return f"Running as: {mode_desc}, using {iset_desc}, {endian} byte order."


def _fpscr_plain_english(fpscr: int) -> str:
    """One-line plain-English summary of FPSCR for non-ARM developers."""
    parts = ["FPU:"]
    if fpscr & (1 << 25):
        parts.append("default-NaN on,")
    if fpscr & (1 << 24):
        parts.append("flush-to-zero on,")
    rmode = (fpscr >> 22) & 3
    rmode_desc = {0: "round-to-nearest", 1: "round towards +\u221e",
                  2: "round towards \u2212\u221e", 3: "round-to-zero"}.get(rmode, "?")
    parts.append(f"{rmode_desc}.")
    # Cumulative exceptions
    exc = []
    if fpscr & 0x01: exc.append("an invalid operation")
    if fpscr & 0x02: exc.append("a division by zero")
    if fpscr & 0x04: exc.append("an overflow")
    if fpscr & 0x08: exc.append("an underflow")
    if fpscr & 0x10: exc.append("an inexact result")
    if fpscr & 0x80: exc.append("a denormal input")
    if exc:
        parts.append(f"Flagged: {', '.join(exc)}.")
    return " ".join(parts)


# ── Text Formatter ──────────────────────────────────────────────────────

def format_text(dump: CoreDump, traces: List[StackTrace],
                source_root: Optional[str] = None,
                register_symbols: Optional[Dict[int, SourceLocation]] = None,
                vita_logs: Optional[Dict[str, str]] = None) -> str:
    """Generate a human-readable text crash report."""
    lines = []
    ct = dump.crashed_thread

    lines.append("═" * 60)
    lines.append("  CRASH REPORT: KeeperFX Vita")
    lines.append("═" * 60)
    lines.append(f"  Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")

    if ct:
        lines.append(f"  Exception:  {ct.stop_reason_str}")
        lines.append(f"  Thread:     {ct.name} (TID 0x{ct.tid:x})")
        lines.append(f"  PC:         0x{ct.pc:08x}")
        if ct.regs:
            lines.append(f"  LR:         0x{ct.regs.lr:08x}")
    lines.append("")

    # Crashed thread trace first
    for trace in traces:
        if ct and trace.thread_id == ct.tid:
            lines.append(_format_trace_text(trace, source_root, is_crashed=True))
            break

    # Register dump for crashed thread
    if ct and ct.regs:
        lines.append("")
        lines.append("REGISTERS:")
        r = ct.regs
        for i in range(0, 13, 4):
            row = "  "
            for j in range(i, min(i + 4, 13)):
                row += f"R{j:<2}=0x{r.r[j]:08x}  "
            lines.append(row.rstrip())
        lines.append(f"  SP =0x{r.sp:08x}  LR =0x{r.lr:08x}  PC =0x{r.pc:08x}")
        lines.append(f"  CPSR=0x{r.cpsr:08x}  [{_decode_cpsr(r.cpsr)}]")
        lines.append(f"  FPSCR=0x{r.fpscr:08x}  [{_decode_fpscr(r.fpscr)}]")

        lines.append("")
        lines.append(f"  {_cpsr_plain_english(r.cpsr)}")
        lines.append(f"  {_fpscr_plain_english(r.fpscr)}")

        # Register annotations
        lines.append("")
        lines.append("REGISTER ANNOTATIONS:")
        all_regs = [(f"R{i}", r.r[i]) for i in range(13)]
        all_regs += [("SP", r.sp), ("LR", r.lr), ("PC", r.pc)]
        # Group consecutive NULL callee-saved registers
        null_group = []
        for name, val in all_regs:
            cat, label, tip = _classify_register(name, val, dump, register_symbols)
            if cat == "null" and name not in ("SP", "LR", "PC"):
                null_group.append(name)
                continue
            if null_group:
                lines.append(f"  {', '.join(null_group):>12} = NULL")
                null_group = []
            sym = ""
            if cat == "code" and register_symbols and val in register_symbols:
                loc = register_symbols[val]
                sym = f" \u2192 {loc.function}"
                if loc.file and loc.file != "??":
                    sym += f"  ({loc.file}:{loc.line})"
            elif label:
                sym = f" ({label})"
            role = _REG_ROLES.get(name, "")
            role_str = f"  [{role}]" if role else ""
            lines.append(f"  {name:>12} = 0x{val:08x}{sym}{role_str}")
        if null_group:
            lines.append(f"  {', '.join(null_group):>12} = NULL")

    # Diagnosis hints
    diag = _diagnose(dump, ct)
    if diag:
        lines.append("")
        lines.append("DIAGNOSIS:")
        for d in diag:
            lines.append(f"  {d}")

    # Profiler measurements (stack monitoring frames from crash trace)
    profiler_frames = []
    for trace in traces:
        if ct and trace.thread_id == ct.tid:
            profiler_frames = _extract_profiler_frames(trace)
            break
    
    # Stack measurements from crash data
    stack_measurements = _extract_stack_measurements(dump)
    if stack_measurements:
        lines.append("")
        lines.append("STACK MEASUREMENTS AT CRASH:")
        lines.append(f"  Total Stack Size:     {stack_measurements['total_size']:>12,} bytes ({stack_measurements['total_size'] / (1024*1024):.2f} MB)")
        lines.append(f"  Stack Base (top):     0x{stack_measurements['stack_base']:08x}")
        lines.append(f"  Current SP:           0x{stack_measurements['current_sp']:08x}")
        if "estimated_used" in stack_measurements:
            lines.append(f"  Estimated Used:       {stack_measurements['estimated_used']:>12,} bytes ({stack_measurements['estimated_used'] / (1024*1024):.2f} MB, {stack_measurements['usage_percent']:.1f}%)")
            lines.append(f"  Remaining:            {stack_measurements['remaining']:>12,} bytes ({stack_measurements['remaining'] / (1024*1024):.2f} MB)")
    
    if profiler_frames:
        lines.append("")
        lines.append("PROFILER STACK FRAMES (where crash occurred):")
        for frame in profiler_frames:
            src = frame.source
            if src:
                func = src.function or "unknown"
                file_info = f"{src.file}:{src.line}" if src.file and src.file != "??" else "??"
                lines.append(f"  #{frame.index:<2}  {func} @ {file_info}")
                lines.append(f"       0x{frame.address:08x}  ({frame.module_ref or 'unknown'})")

    # Other threads
    other_traces = [t for t in traces if not ct or t.thread_id != ct.tid]
    if other_traces:
        lines.append("")
        lines.append("─" * 60)
        lines.append("  OTHER THREADS")
        lines.append("─" * 60)
        for trace in other_traces:
            lines.append(_format_trace_text(trace, source_root, is_crashed=False))

    # All threads summary
    lines.append("")
    lines.append("─" * 60)
    lines.append("  THREAD SUMMARY")
    lines.append("─" * 60)
    for t in dump.threads:
        status = "** CRASHED **" if t.crashed else "Waiting" if t.status == 8 else f"Status 0x{t.status:x}"
        lines.append(f"  {t.name:<30} TID=0x{t.tid:08x}  {status}")
        lines.append(f"    PC=0x{t.pc:08x}  ({dump.resolve_address(t.pc) or 'unknown'})")

    # Module list
    if dump.modules:
        lines.append("")
        lines.append("─" * 60)
        lines.append("  LOADED MODULES")
        lines.append("─" * 60)
        for mod in dump.modules:
            lines.append(f"  {mod.name}")
            for seg in mod.segments:
                lines.append(
                    f"    @{seg.index + 1}: 0x{seg.base:08x} - 0x{seg.base + seg.size:08x}"
                    f"  ({seg.size:>10,} bytes)"
                )

    lines.append("")
    lines.append("═" * 60)
    return "\n".join(lines)


def _format_trace_text(trace: StackTrace, source_root: Optional[str],
                       is_crashed: bool) -> str:
    """Format a single thread's stack trace as text."""
    lines = []
    header = "CALL STACK" if is_crashed else f"Thread: {trace.thread_name}"
    lines.append(f"\n{header} (most recent call first):")

    for frame in trace.frames:
        addr_str = f"0x{frame.address:08x}"
        if frame.source:
            func = frame.source.function
            src = ""
            if frame.source.file and frame.source.file != "??":
                src = f"  {frame.source.file}:{frame.source.line}"
            lines.append(f"  #{frame.index:<3} {func}{src}")
            lines.append(f"       {addr_str}  ({frame.module_ref or 'unknown'})")

            # Show inlined chain
            loc = frame.source.inlined_from
            while loc:
                src = ""
                if loc.file and loc.file != "??":
                    src = f"  {loc.file}:{loc.line}"
                lines.append(f"       [inlined] {loc.function}{src}")
                loc = loc.inlined_from

            # Show source context if available
            if source_root and frame.source.file != "??" and frame.source.line > 0:
                context = _read_source_context(source_root, frame.source.file,
                                               frame.source.line)
                if context:
                    for ctx_line in context:
                        lines.append(f"       | {ctx_line}")
        else:
            lines.append(f"  #{frame.index:<3} {addr_str}  ({frame.module_ref or 'unknown'})")

    return "\n".join(lines)


def _read_source_context(source_root: str, filepath: str, line: int,
                         context: int = 2) -> List[str]:
    """Read source lines around the crash point."""
    # Handle various path formats from addr2line
    # Strip leading /src/ if present (Docker mount point)
    for prefix in ("/src/", "src/", "./"):
        if filepath.startswith(prefix):
            filepath = filepath[len(prefix):]
            break

    full_path = os.path.join(source_root, filepath)
    if not os.path.isfile(full_path):
        return []

    try:
        with open(full_path, "r", encoding="utf-8", errors="replace") as f:
            all_lines = f.readlines()

        start = max(0, line - context - 1)
        end = min(len(all_lines), line + context)
        result = []
        for i in range(start, end):
            marker = ">>>" if i == line - 1 else "   "
            result.append(f"{marker} {i + 1:>5}: {all_lines[i].rstrip()}")
        return result
    except OSError:
        return []


def _diagnose(dump: CoreDump, thread: Optional[Thread]) -> List[str]:
    """Generate automated diagnosis hints."""
    hints = []
    if not thread or not thread.regs:
        return hints

    r = thread.regs

    # NULL pointer detection
    null_regs = []
    for i in range(13):
        if r.r[i] == 0:
            null_regs.append(f"R{i}")
    if r.sp == 0:
        null_regs.append("SP")

    exc_kind = thread.stop_reason & 0xF

    if exc_kind == 4:  # Data abort (0x30004, etc.)
        hints.append("Data abort — attempted to read/write an invalid memory address.")
        if null_regs:
            hints.append(f"NULL registers: {', '.join(null_regs)} — likely NULL pointer dereference.")

    elif exc_kind == 3:  # Prefetch abort (0x20003, 0x30003, etc.)
        hints.append("Prefetch abort — CPU failed to fetch the next instruction.")
        # regs.pc holds the faulting fetch address; thread.pc may differ
        fault_pc = r.pc
        if fault_pc != thread.pc:
            hints.append(f"Faulting address: 0x{fault_pc:08x} (CPU tried to execute here).")
        if fault_pc < 0x1000:
            hints.append(f"Address 0x{fault_pc:08x} is near zero — call through NULL function pointer.")
            hints.append(f"LR=0x{r.lr:08x} — the caller that branched to NULL.")
        elif not (0x81000000 <= fault_pc < 0x82000000):
            hints.append(f"Address 0x{fault_pc:08x} is outside the code segment — jump to corrupted address (bad function pointer, vtable, or stack smash).")

    elif exc_kind == 1:  # Undefined instruction (0x10002, etc.)
        hints.append("Undefined instruction — CPU encountered an invalid opcode.")
        # Check ARM/Thumb mode mismatch (ARM DDI 0406C §A2.5.1)
        thumb = bool(r.cpsr & (1 << 5))
        pc_thumb = bool(r.pc & 1)
        if thumb:
            hints.append("CPU was in Thumb mode (CPSR.T=1).")
        else:
            hints.append("CPU was in ARM mode (CPSR.T=0).")
        if thumb != pc_thumb and exc_kind == 1:
            hints.append("ARM/Thumb mode mismatch detected — possible BLX to wrong-mode address.")
        hints.append("This can indicate: corrupted code memory, wrong ARM/Thumb mode, or stack smashing.")

    # Processor state context from CPSR (ARM DDI 0406C §B1.3)
    if r.cpsr:
        mode = r.cpsr & 0x1F
        mode_name = _ARM_MODES.get(mode)
        # If we ended up in ABT mode, that's the exception handler's mode
        if mode_name and mode_name not in ("USR", "SYS"):
            hints.append(f"Processor mode: {mode_name} (M=0b{mode:05b}) — exception context.")

    # FPSCR cumulative exceptions (ARM DDI 0406C §B4.1.58)
    if r.fpscr:
        fp_exc = []
        if r.fpscr & 0x01: fp_exc.append("IOC (Invalid Operation)")
        if r.fpscr & 0x02: fp_exc.append("DZC (Division by Zero)")
        if r.fpscr & 0x04: fp_exc.append("OFC (Overflow)")
        if r.fpscr & 0x08: fp_exc.append("UFC (Underflow)")
        if r.fpscr & 0x10: fp_exc.append("IXC (Inexact)")
        if r.fpscr & 0x80: fp_exc.append("IDC (Input Denormal)")
        if fp_exc:
            hints.append(f"FPU exceptions flagged: {', '.join(fp_exc)}.")

    # Stack overflow detection (Vita default thread stack is at ~0x836xxxxx range)
    if 0x83600000 <= r.sp <= 0x83600100:
        hints.append(f"SP=0x{r.sp:08x} is very close to the stack base — possible stack overflow.")

    # Check if crash is in malloc/memory allocation
    crash_ref = dump.resolve_address(r.pc)
    if crash_ref and any(kw in crash_ref.lower() for kw in ("malloc", "vglmalloc", "alloc")):
        hints.append("Crash in memory allocator — possible out-of-memory condition.")

    # SCE error code detection in registers
    sce_errors = _scan_sce_errors(r)
    if sce_errors:
        hints.append("SCE error codes detected in registers:")
        for reg_name, code, desc in sce_errors:
            hints.append(f"  {reg_name}=0x{code:08x} → {desc}")

    return hints


# ── SCE Error Code Resolution ──────────────────────────────────────────

# Facility codes from https://wiki.henkaku.xyz/vita/Error_Codes
_SCE_FACILITIES = {
    0x001: "ERRNO",
    0x002: "KERNEL",
    0x003: "KERNEL_EXT",
    0x004: "KERNEL_S",
    0x005: "KERNEL_SEXT",
    0x006: "KERNEL_B",
    0x007: "KERNEL_BEXT",
    0x008: "DECI_USR",
    0x009: "DECI_SYS",
    0x00A: "COREDUMP",
    0x00C: "REGISTRY",
    0x00F: "SBL",
    0x010: "VSH",
    0x011: "UTILITY",
    0x012: "SYSFILE",
    0x013: "MSAPP",
    0x014: "PFS",
    0x018: "UPDATER",
    0x020: "SHAREDFB",
    0x022: "MEMSTICK",
    0x024: "USB",
    0x025: "SYSCON",
    0x026: "AUDIO",
    0x027: "LFLASH",
    0x028: "LFATFS",
    0x029: "DISPLAY",
    0x02B: "POWER",
    0x02C: "AUDIOROUTING",
    0x02D: "MEDIASYNC",
    0x02E: "CAMERA",
    0x034: "CTRL",
    0x035: "TOUCH",
    0x036: "MOTION",
    0x03F: "PERIPH",
    0x041: "NETWORK",
    0x042: "SAS",
    0x043: "HTTP",
    0x044: "WAVE",
    0x045: "SND",
    0x046: "FONT",
    0x047: "P3DA",
    0x048: "HASH",
    0x049: "PRNG",
    0x04A: "NGS",
    0x04B: "ECHO",
    0x04C: "GPU",
    0x04D: "SUPLHA",
    0x04E: "VOICE",
    0x04F: "HEAP",
    0x052: "OPENPSID",
    0x053: "DNAS",
    0x054: "MTP",
    0x055: "NP",
    0x056: "COMPRESSION",
    0x057: "DBGFONT",
    0x058: "PERF",
    0x059: "FIBER",
    0x05A: "SYSMODULE",
    0x05B: "GXM",
    0x05C: "CES",
    0x05D: "GXT",
    0x05F: "LIBRARY",
    0x060: "CODECENGINE",
    0x061: "MPEG",
    0x062: "AVC",
    0x063: "ATRAC",
    0x064: "ASF",
    0x065: "JPEG",
    0x066: "AVI",
    0x067: "MP3",
    0x068: "G729",
    0x069: "PNG",
    0x06A: "AVPLAYER",
    0x06B: "AACENC",
    0x077: "RUDP",
    0x07F: "CODEC",
    0x080: "APPMGR",
    0x081: "ULT",
    0x082: "FIOS",
    0x083: "BASE64",
    0x084: "INIFILE",
    0x085: "XML",
    0x086: "AUDIOENC",
    0x087: "NPDRM",
    0x088: "PHYSICSEFFECTS",
    0x089: "SYSTEM_GESTURE",
    0x08B: "FACE",
    0x08C: "SMART",
    0x090: "SAMPLE_UTILITY",
    0x091: "NGS_QUICK_SYNTH",
    0x092: "JSON",
    0x100: "SCREAM",
}


def _decode_sce_error(code: int) -> Optional[str]:
    """Decode a 0x80XXXXXX SCE error code into a human-readable description."""
    if (code & 0x80000000) == 0:
        return None

    is_fatal = bool(code & 0x40000000)
    facility = (code >> 16) & 0xFFF
    error_num = code & 0xFFFF

    facility_name = _SCE_FACILITIES.get(facility)
    if not facility_name:
        facility_name = f"UNKNOWN(0x{facility:03x})"

    severity = "FATAL" if is_fatal else "error"

    # Look up the exact named constant and description
    named = SCE_ERROR_CODES.get(code)
    desc = SCE_ERROR_DESCRIPTIONS.get(code)
    if named and desc:
        return f"{severity}: {named} [{facility_name}] — {desc}"
    if named:
        return f"{severity}: {named} [{facility_name}]"
    return f"{severity}: {facility_name} (0x{error_num:04x})"


def _is_sce_error(val: int) -> bool:
    """Check if a value looks like an SCE error code, not a memory address."""
    if (val & 0x80000000) == 0:
        return False
    # Vita memory regions that look like error codes but aren't:
    # 0x81000000-0x82000000 = user code segment
    # 0x83000000-0x84000000 = user stack
    # 0x84000000-0x88000000 = user heap / shared memory
    # 0xE0000000-0xFFFFFFFF = kernel addresses
    # Real SCE errors: 0x80XXXXXX and 0xC0XXXXXX (fatal), facility in bits 27-16
    if val >= 0xE0000000:
        return False
    if 0x81000000 <= val < 0x88000000:
        return False
    facility = (val >> 16) & 0xFFF
    return facility in _SCE_FACILITIES


def _scan_sce_errors(regs) -> List[tuple]:
    """Scan all registers for SCE error codes (0x80XXXXXX pattern)."""
    results = []
    for i in range(13):
        val = regs.r[i]
        if _is_sce_error(val):
            results.append((f"R{i}", val, _decode_sce_error(val)))
    if _is_sce_error(regs.lr):
        results.append(("LR", regs.lr, _decode_sce_error(regs.lr)))
    return results


# ── HTML Formatter ──────────────────────────────────────────────────────

_BANNER_PATH = Path(__file__).resolve().parents[2] / "res" / "keeperfx_icon256-24bpp.png"


def _load_banner_b64() -> str:
    """Load the KeeperFX icon as a base64 data URI for embedding."""
    try:
        data = _BANNER_PATH.read_bytes()
        return "data:image/png;base64," + base64.b64encode(data).decode()
    except OSError:
        return ""


def format_html(dump: CoreDump, traces: List[StackTrace],
                source_root: Optional[str] = None,
                register_symbols: Optional[Dict[int, SourceLocation]] = None,
                vita_logs: Optional[Dict[str, str]] = None) -> str:
    """Generate a self-contained HTML crash report."""
    ct = dump.crashed_thread
    h = html.escape

    banner_b64 = _load_banner_b64() if _BANNER_PATH.exists() else ""
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    parts = [
        "<!DOCTYPE html>",
        "<html><head><meta charset='utf-8'>",
        "<title>KeeperFX Vita Crash Report</title>",
        "<style>",
        _CSS,
        "</style></head><body>",
        "<div class='container'>",
        # Banner box - mimics the top banner image on the site
        "<div class='content-box'>",
        "<div class='banner'>",
        f"<img src='{banner_b64}' alt='KeeperFX' class='banner-icon'>" if banner_b64 else "",
        "<h1>Vita Crash Report</h1>",
        "</div>",
        "</div>",
    ]

    # Crash info box
    if ct:
        parts.append("<div class='content-box'>")
        parts.append("<div class='content-header'><h2>Exception</h2></div>")
        parts.append("<div class='content-body'>")
        parts.append(f"<p><strong>Type:</strong> {h(ct.stop_reason_str)}</p>")
        parts.append(f"<p><strong>Thread:</strong> {h(ct.name)} (TID 0x{ct.tid:x})</p>")
        parts.append(f"<p><strong>PC:</strong> <code>0x{ct.pc:08x}</code></p>")
        if ct.regs:
            parts.append(f"<p><strong>LR:</strong> <code>0x{ct.regs.lr:08x}</code></p>")
        parts.append("</div></div>")

    # Diagnosis box — tabbed: Diagnosis hints + profiler stack frames + one tab per harvested log
    diag = _diagnose(dump, ct)
    profiler_frames = []
    stack_measurements = _extract_stack_measurements(dump)
    for trace in traces:
        if ct and trace.thread_id == ct.tid:
            profiler_frames = _extract_profiler_frames(trace)
            break
    _LOG_ORDER = ["kfx_boot.log", "kfx_preinit.log", "keeperfx.log", "profiler.log", "vitaGL.log"]
    log_tabs = []
    if vita_logs:
        log_tabs = sorted(vita_logs.keys(),
                          key=lambda k: (_LOG_ORDER.index(k) if k in _LOG_ORDER else len(_LOG_ORDER)))
    if diag or profiler_frames or stack_measurements or log_tabs:
        parts.append("<div class='content-box diagnosis'>")
        parts.append("<div class='content-header'><h2>Diagnosis</h2></div>")
        parts.append("<div class='content-body'>")
        # Build tab list: always start with 'hints' if there are any hints, then profiler, then logs
        all_tabs = []
        if diag:
            all_tabs.append(("diag-hints", "Hints"))
        if profiler_frames:
            all_tabs.append(("diag-profiler", "Profiler Stack"))
        for fname in log_tabs:
            tab_id = "diag-log-" + fname.replace(".", "-").replace("_", "-")
            label = fname.removesuffix(".log") if hasattr(str, 'removesuffix') else fname[:-4] if fname.endswith(".log") else fname
            all_tabs.append((tab_id, label))
        if len(all_tabs) > 1:
            # Render tab strip
            parts.append("<div class='diag-tabs'>")
            for i, (tid, tlabel) in enumerate(all_tabs):
                active = " active" if i == 0 else ""
                parts.append(f"<button class='diag-tab{active}' data-tab='{tid}'>{h(tlabel)}</button>")
            parts.append("</div>")
        # Render tab panels
        for i, (tid, _) in enumerate(all_tabs):
            hidden = "" if i == 0 else " style='display:none'"
            parts.append(f"<div class='diag-panel' id='{tid}'{hidden}>")
            if tid == "diag-hints":
                for d in diag:
                    parts.append(f"<p>{h(d)}</p>")
            elif tid == "diag-profiler":
                parts_profiler = []
                if stack_measurements:
                    parts_profiler.append("<p><strong>Stack Measurements at Crash:</strong></p>")
                    parts_profiler.append(f"<p><code>Total Stack: {stack_measurements['total_size']:,} bytes ({stack_measurements['total_size'] / (1024*1024):.2f} MB)<br/>")
                    parts_profiler.append(f"Stack Base: 0x{stack_measurements['stack_base']:08x}<br/>")
                    parts_profiler.append(f"Current SP: 0x{stack_measurements['current_sp']:08x}<br/>")
                    if "estimated_used" in stack_measurements:
                        parts_profiler.append(f"Used: {stack_measurements['estimated_used']:,} bytes ({stack_measurements['estimated_used'] / (1024*1024):.2f} MB, {stack_measurements['usage_percent']:.1f}%)<br/>")
                        parts_profiler.append(f"Remaining: {stack_measurements['remaining']:,} bytes ({stack_measurements['remaining'] / (1024*1024):.2f} MB)</code></p>")
                if profiler_frames:
                    parts_profiler.append("<p><strong>Stack frames where profiler crashed:</strong></p>")
                    parts_profiler.append("<pre class='source log-source'>")
                    for frame in profiler_frames:
                        src = frame.source
                        if src:
                            func = src.function or "unknown"
                            file_info = f"{src.file}:{src.line}" if src.file and src.file != "??" else "??"
                            parts_profiler.append(f"#{frame.index:<2}  {h(func)} @ {h(file_info)}")
                            parts_profiler.append(f"     0x{frame.address:08x}  ({h(frame.module_ref or 'unknown')})")
                    parts_profiler.append("</pre>")
                if not stack_measurements and not profiler_frames:
                    parts_profiler.append("<p><em>(no profiler data)</em></p>")
                parts.extend(parts_profiler)
            else:
                fname = next(f for f in log_tabs if ("diag-log-" + f.replace(".", "-").replace("_", "-")) == tid)
                content = vita_logs[fname]
                if content.strip():
                    lines_out = []
                    for lineno, line in enumerate(content.splitlines(), 1):
                        lines_out.append(f"   {lineno:>5}: {h(line)}")
                    parts.append("<pre class='source log-source'>" + "\n".join(lines_out) + "</pre>")
                else:
                    parts.append("<p><em>(empty)</em></p>")
            parts.append("</div>")
        parts.append("</div></div>")

    # Crashed thread stack trace
    for trace in traces:
        if ct and trace.thread_id == ct.tid:
            parts.append(_format_trace_html(trace, source_root, is_crashed=True))
            break

    # Registers box
    if ct and ct.regs:
        parts.append("<div class='content-box'>")
        parts.append("<div class='content-header'><h2>Registers</h2></div>")
        parts.append("<div class='content-body'>")

        # Color legend
        parts.append("<div class='reg-legend'>")
        parts.append("<span class='leg-item'><span class='leg-dot null-dot'></span> NULL</span>")
        parts.append("<span class='leg-item'><span class='leg-dot code-dot'></span> Code address</span>")
        parts.append("<span class='leg-item'><span class='leg-dot stack-dot'></span> Stack address</span>")
        parts.append("<span class='leg-item'><span class='leg-dot heap-dot'></span> Heap address</span>")
        parts.append("<span class='leg-item'><span class='leg-dot sentinel-dot'></span> Uninitialized</span>")
        parts.append("<span class='leg-item'><span class='leg-dot sce-error-dot'></span> SCE error code</span>")
        parts.append("<span class='leg-item'><span class='leg-dot kernel-dot'></span> Kernel address</span>")
        parts.append("</div>")

        parts.append("<table class='regs'>")
        r = ct.regs
        for i in range(0, 13, 4):
            parts.append("<tr>")
            for j in range(i, min(i + 4, 13)):
                cat, label, tip = _classify_register(f"R{j}", r.r[j], dump, register_symbols)
                sym_html = ""
                if cat == "code" and register_symbols and r.r[j] in register_symbols:
                    loc = register_symbols[r.r[j]]
                    sym_html = f"<br><span class='reg-sym'>{h(loc.function)}</span>"
                parts.append(f"<td>R{j}</td><td class='{cat}' data-tooltip='{h(tip)}'>"
                             f"<code>0x{r.r[j]:08x}</code>{sym_html}</td>")
            parts.append("</tr>")

        # SP / LR / PC row
        parts.append("<tr>")
        for name, val in [("SP", r.sp), ("LR", r.lr), ("PC", r.pc)]:
            cat, label, tip = _classify_register(name, val, dump, register_symbols)
            sym_html = ""
            if cat == "code" and register_symbols and val in register_symbols:
                loc = register_symbols[val]
                sym_html = f"<br><span class='reg-sym'>{h(loc.function)}</span>"
            parts.append(f"<td>{name}</td><td class='{cat}' data-tooltip='{h(tip)}'>"
                         f"<code>0x{val:08x}</code>{sym_html}</td>")
        parts.append("</tr></table>")

        # Plain-English CPSR/FPSCR summaries
        parts.append("<div class='psr-decode'>")
        parts.append(f"<p class='psr-plain'>{h(_cpsr_plain_english(r.cpsr))}</p>")
        parts.append(f"<p><strong>CPSR</strong> <code>0x{r.cpsr:08x}</code> &mdash; {h(_decode_cpsr(r.cpsr))}</p>")
        parts.append(f"<p class='psr-plain'>{h(_fpscr_plain_english(r.fpscr))}</p>")
        parts.append(f"<p><strong>FPSCR</strong> <code>0x{r.fpscr:08x}</code> &mdash; {h(_decode_fpscr(r.fpscr))}</p>")
        parts.append("</div>")
        parts.append("</div></div>")

    # Other threads
    other_traces = [t for t in traces if not ct or t.thread_id != ct.tid]
    if other_traces:
        parts.append("<details><summary>Other Threads</summary>")
        for trace in other_traces:
            parts.append(_format_trace_html(trace, source_root, is_crashed=False))
        parts.append("</details>")

    # Thread summary box
    parts.append("<details><summary>Thread Summary</summary>")
    parts.append("<div class='content-box'>")
    parts.append("<div class='content-body'>")
    parts.append("<table class='threads'><thead><tr><th>Name</th><th>TID</th><th>Status</th><th>PC</th></tr></thead><tbody>")
    for t in dump.threads:
        status = "CRASHED" if t.crashed else "Waiting" if t.status == 8 else f"0x{t.status:x}"
        cls = "crashed" if t.crashed else ""
        ref = dump.resolve_address(t.pc) or "unknown"
        parts.append(f"<tr class='{cls}'><td>{h(t.name)}</td><td><code>0x{t.tid:08x}</code></td>")
        parts.append(f"<td>{status}</td><td><code>0x{t.pc:08x}</code> ({h(ref)})</td></tr>")
    parts.append("</tbody></table>")
    parts.append("</div></div></details>")

    # Modules box
    if dump.modules:
        parts.append("<details><summary>Loaded Modules</summary>")
        parts.append("<div class='content-box'>")
        parts.append("<div class='content-body'>")
        parts.append("<table class='modules'><thead><tr><th>Module</th><th>Segment</th><th>Base</th><th>Size</th></tr></thead><tbody>")
        for mod in dump.modules:
            for seg in mod.segments:
                parts.append(
                    f"<tr><td>{h(mod.name)}</td><td>@{seg.index + 1}</td>"
                    f"<td><code>0x{seg.base:08x}</code></td>"
                    f"<td>{seg.size:,}</td></tr>"
                )
        parts.append("</tbody></table>")
        parts.append("</div></div></details>")

    # Footer - matches the site's footer
    parts.append("<footer>")
    parts.append("KeeperFX &mdash; Open Source Dungeon Keeper Remake &amp; Fan Expansion")
    parts.append(f"<br>Generated {timestamp}")
    parts.append("</footer>")

    parts.append("</div>")
    parts.append(_TOOLTIP_JS)
    parts.append("</body></html>")
    return "\n".join(parts)


def _format_trace_html(trace: StackTrace, source_root: Optional[str],
                       is_crashed: bool) -> str:
    """Format a single thread's stack trace as HTML."""
    h = html.escape
    parts = []

    title = "Call Stack" if is_crashed else f"Thread: {trace.thread_name}"
    tag = "h2" if is_crashed else "h3"
    parts.append("<div class='content-box'>")
    parts.append(f"<div class='content-header'><{tag}>{h(title)}</{tag}></div>")
    parts.append("<div class='content-body'>")
    parts.append("<div class='stack-trace'>")

    for frame in trace.frames:
        cls = "frame crashed" if is_crashed and frame.index == 0 else "frame"
        parts.append(f"<div class='{cls}'>")
        parts.append(f"<span class='frame-idx'>#{frame.index}</span>")

        if frame.source:
            parts.append(f"<span class='func'>{h(frame.source.function)}</span>")
            if frame.source.file and frame.source.file != "??":
                parts.append(f"<span class='loc'>{h(frame.source.file)}:{frame.source.line}</span>")
            parts.append(f"<code class='addr'>0x{frame.address:08x}</code>")

            # Inline chain
            loc = frame.source.inlined_from
            while loc:
                parts.append(f"<div class='inline'>[inlined] {h(loc.function)}")
                if loc.file and loc.file != "??":
                    parts.append(f" <span class='loc'>{h(loc.file)}:{loc.line}</span>")
                parts.append("</div>")
                loc = loc.inlined_from

            # Source context
            if source_root and frame.source.file != "??" and frame.source.line > 0:
                context = _read_source_context(source_root, frame.source.file,
                                               frame.source.line)
                if context:
                    parts.append("<pre class='source'>")
                    for ctx_line in context:
                        escaped = h(ctx_line)
                        if ctx_line.startswith(">>>"):
                            escaped = f"<mark>{escaped}</mark>"
                        parts.append(escaped)
                    parts.append("</pre>")
        else:
            parts.append(f"<code class='addr'>0x{frame.address:08x}</code>")
            if frame.module_ref:
                parts.append(f"<span class='loc'>{h(frame.module_ref)}</span>")

        parts.append("</div>")

    parts.append("</div>")
    parts.append("</div></div>")
    return "\n".join(parts)


_CSS = """
@import url('https://fonts.googleapis.com/css2?family=Cinzel:wght@400;600&family=Nunito:wght@400;600&display=swap');
* { color: rgb(215, 197, 182); box-sizing: border-box; }
body { font-family: 'Nunito', sans-serif; background-color: #040404; color: rgb(215, 197, 182);
       margin: 0; padding: 20px; }
.container { max-width: 1000px; margin: 0 auto; }
h1, h2, h3, h4, h5, h6 { font-family: 'Cinzel', serif; color: #efefef; }
hr { background-color: #333; height: 2px; border: none; opacity: 1; }
a { color: #ff4217; text-decoration: none; }
a:hover { text-decoration: underline; }

/* Content box system — matches keeperfx.net */
.content-box { background: rgba(11, 11, 11, 0.52); border: 2px solid #1c1c1c; }
.content-box + .content-box { margin-top: 30px; }
.content-header { border-bottom: 2px solid #1c1c1c; margin: 0 20px; padding: 20px 0 5px 0; }
.content-header h2, .content-header h3 { font-size: 22px; margin: 0; border: 0; }
.content-body { padding: 20px; }
.content-body p { padding-left: 15px; padding-right: 15px; }

/* Banner */
.banner { display: flex; align-items: center; gap: 16px; padding: 15px 20px; }
.banner h1 { margin: 0; border: 0; font-size: 28px; }
.banner-icon { width: 64px; height: 64px; }

/* Crash info (exception box) */
.content-box:has(.content-header h2) .content-body strong { color: #efefef; }

/* Diagnosis box */
.diagnosis { border-left: 4px solid #cd8e00; }
.diagnosis .content-header h2 { color: #cd8e00; }

/* Code styling — matches site */
code { background-color: rgba(30, 30, 30, 0.7); border: 1px solid rgba(50, 50, 50, 1);
       padding: 4px; padding-bottom: 3px; color: rgb(238, 231, 224); font-family: monospace;
       font-size: initial; }

/* Stack trace */
.stack-trace { margin: 0; }
.frame { padding: 8px 12px; margin: 4px 0; background: rgba(6, 6, 6, 0.666);
         border: 2px solid #1c1c1c; border-left: 4px solid #1c1c1c; }
.frame.crashed { border-left-color: #ff4217; background: rgba(30, 8, 8, 0.7); }
.frame-idx { color: #616161; margin-right: 8px; font-weight: bold; font-family: 'Cinzel', serif; }
.func { color: #ff4217; font-weight: bold; margin-right: 12px; }
.loc { color: rgb(215, 197, 182); }
.addr { color: #616161; margin-left: 8px; }
.inline { color: #616161; margin-left: 32px; font-size: 0.9em; }
pre.source { background: rgba(30, 30, 30, 0.7); border: 1px solid rgba(50, 50, 50, 1);
             padding: 8px; margin: 4px 0 0 32px; font-size: 0.85em; overflow-x: auto;
             color: rgb(238, 231, 224); }
pre.source mark { background: rgba(255, 66, 23, 0.2); color: #ff4217; }

/* Tables — matches site */
table { border-collapse: collapse; margin: 0; width: 100%; border: 2px solid rgba(0,0,0,0.4); }
thead { background-color: rgba(0, 0, 0, 0.65); }
tbody { background-color: rgba(0, 0, 0, 0.4); }
th { font-family: 'Cinzel', serif; border: 0; padding: 6px 8px; text-align: left; }
td { border: 0; padding: 4px 8px; }
.regs td { padding: 4px 12px; cursor: default; position: relative; }
.regs .null code { color: #ff4217; }
.regs .code code { color: #89b4fa; }
.regs .stack code { color: #a6e3a1; }
.regs .heap code { color: #cba6f7; }
.regs .sentinel code { color: #6c7086; }
.regs .sce-error code { color: #f9e2af; }
.regs .kernel code { color: #fab387; }
.regs .small-int code { color: rgb(238, 231, 224); }
.reg-sym { display: block; font-size: 0.8em; color: #89b4fa; font-family: monospace;
           white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 180px; }

/* Register legend */
.reg-legend { display: flex; flex-wrap: wrap; gap: 12px; padding: 8px 12px; margin-bottom: 12px;
              background: rgba(6, 6, 6, 0.666); border: 2px solid #1c1c1c; font-size: 0.85em; }
.leg-item { display: inline-flex; align-items: center; gap: 4px; }
.leg-dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; }
.null-dot { background: #ff4217; }
.code-dot { background: #89b4fa; }
.stack-dot { background: #a6e3a1; }
.heap-dot { background: #cba6f7; }
.sentinel-dot { background: #6c7086; }
.sce-error-dot { background: #f9e2af; }
.kernel-dot { background: #fab387; }

/* Register tooltip */
.reg-tooltip { position: fixed; max-width: 360px; padding: 10px 14px; background: #1e1e2e;
               border: 2px solid #45475a; color: rgb(215, 197, 182); font-size: 0.85em;
               line-height: 1.5; white-space: pre-wrap; pointer-events: none; z-index: 1000;
               box-shadow: 0 4px 12px rgba(0,0,0,0.6); }

.psr-decode { padding: 8px 12px; margin-top: 12px; background: rgba(6, 6, 6, 0.666);
              border: 2px solid #1c1c1c; font-size: 0.9em; }
.psr-decode p { padding: 2px 8px; margin: 4px 0; }
.psr-decode .psr-plain { color: #a6e3a1; font-style: italic; margin-bottom: 2px; }
.psr-decode strong { color: #89b4fa; font-family: 'Cinzel', serif; }
.threads .crashed { background: rgba(30, 8, 8, 0.7); }

/* Diagnosis tabs */
.diag-tabs { display: flex; gap: 4px; margin-bottom: 12px; border-bottom: 2px solid #1c1c1c; padding-bottom: 4px; flex-wrap: wrap; }
.diag-tab { background: rgba(20,20,20,0.7); border: 2px solid #1c1c1c; color: rgb(215, 197, 182);
            padding: 5px 14px; cursor: pointer; font-family: 'Cinzel', serif; font-size: 0.85em;
            transition: background 0.15s; }
.diag-tab:hover { background: rgba(40,40,40,0.9); color: #efefef; }
.diag-tab.active { background: rgba(40,10,0,0.7); border-color: #cd8e00; color: #cd8e00; }
.diag-panel p { padding-left: 8px; margin: 6px 0; }
/* Log content (inside diagnosis tabs) */
pre.log-source { max-height: 500px; overflow-y: auto; margin-left: 0; white-space: pre-wrap; word-break: break-all; }

/* Details/summary — section toggles */
details { margin-top: 30px; }
summary { cursor: pointer; color: #ff4217; font-weight: 600; padding: 8px 0;
          font-family: 'Cinzel', serif; font-size: 20px; }
summary:hover { color: #efefef; }

/* Footer — matches site */
footer { padding-top: 30px; font-size: 14px; text-align: center; color: #777; padding-bottom: 15px; }
"""

_TOOLTIP_JS = """
<script>
(function() {
  var tip = document.createElement('div');
  tip.className = 'reg-tooltip';
  tip.style.display = 'none';
  document.body.appendChild(tip);

  document.querySelectorAll('[data-tooltip]').forEach(function(el) {
    el.addEventListener('mouseenter', function(e) {
      tip.textContent = el.getAttribute('data-tooltip');
      tip.style.display = 'block';
      positionTip(e);
    });
    el.addEventListener('mousemove', positionTip);
    el.addEventListener('mouseleave', function() {
      tip.style.display = 'none';
    });
  });

  function positionTip(e) {
    var x = e.clientX + 12, y = e.clientY + 12;
    var r = tip.getBoundingClientRect();
    if (x + r.width > window.innerWidth) x = e.clientX - r.width - 8;
    if (y + r.height > window.innerHeight) y = e.clientY - r.height - 8;
    tip.style.left = x + 'px';
    tip.style.top = y + 'px';
  }

  // Diagnosis tab switching
  document.querySelectorAll('.diag-tab').forEach(function(btn) {
    btn.addEventListener('click', function() {
      var target = btn.getAttribute('data-tab');
      btn.closest('.content-body').querySelectorAll('.diag-tab').forEach(function(b) {
        b.classList.remove('active');
      });
      btn.classList.add('active');
      btn.closest('.content-body').querySelectorAll('.diag-panel').forEach(function(panel) {
        panel.style.display = panel.id === target ? '' : 'none';
      });
    });
  });
})();
</script>
"""

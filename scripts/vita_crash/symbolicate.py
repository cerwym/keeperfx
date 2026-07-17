"""
Stack walking and symbolication for PS Vita crash dumps.

Uses DWARF debug info (via pyelftools) for frame unwinding,
and arm-vita-eabi-addr2line for source line resolution.
"""

import os
import re
import struct
import subprocess
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple

from .core_parser import CoreDump, Thread, MemoryRegion

# Vita module loader always maps the first code segment at this base.
ELF_BASE_ADDRESS = 0x81000000


@dataclass
class SourceLocation:
    """Resolved source location for a code address."""
    function: str
    file: str
    line: int
    inlined_from: Optional["SourceLocation"] = None

    def __str__(self):
        loc = f"{self.function}"
        if self.file and self.file != "??":
            loc += f"  {self.file}:{self.line}"
        if self.inlined_from:
            loc += f"\n      [inlined from {self.inlined_from}]"
        return loc


@dataclass
class StackFrame:
    """A single frame in the call stack."""
    index: int
    address: int
    offset_in_module: Optional[int] = None  # offset from ELF base
    module_ref: Optional[str] = None  # e.g. "keeperfx@1 + 0x1c8ee4"
    source: Optional[SourceLocation] = None


@dataclass
class StackTrace:
    """Complete stack trace for a thread."""
    thread_name: str
    thread_id: int
    exception_type: str
    frames: List[StackFrame] = field(default_factory=list)


def _find_elf(elf_path: Optional[str], build_dir: str = "out/build") -> Optional[str]:
    """Find the most recently modified Vita ELF binary."""
    if elf_path and os.path.isfile(elf_path):
        return elf_path

    candidates = []
    for preset in ("vita-debug", "vita-reldebug", "vita-release"):
        path = os.path.join(build_dir, preset, "keeperfx")
        if os.path.isfile(path):
            candidates.append(path)

    if not candidates:
        return None

    # Newest modification time = most recently deployed build
    return max(candidates, key=os.path.getmtime)


def _find_code_segment(dump: CoreDump, module_name: str = "keeperfx") -> Optional[Tuple[int, int]]:
    """Find the code segment (index 0) base and size for a module."""
    for mod in dump.modules:
        if mod.name == module_name:
            for seg in mod.segments:
                if seg.index == 0:  # code segment
                    return seg.base, seg.size
    return None


def _find_text_range(elf_path: str) -> Optional[Tuple[int, int]]:
    """Return the runtime (base, size) of the .text section.

    Parses ELF section headers to get the actual executable code range,
    excluding .rodata and other non-executable sections that share the
    same LOAD segment.  Vita ELFs use pre-linked addresses (sh_addr
    already includes the 0x81000000 base), so no offset is needed.
    """
    try:
        from elftools.elf.elffile import ELFFile
        with open(elf_path, "rb") as f:
            elf = ELFFile(f)
            text = elf.get_section_by_name(".text")
            if text:
                return (text["sh_addr"], text["sh_size"])
    except Exception:
        pass
    return None


# Regex matching symbols that are NOT real functions:
#   .LC14, .LFB0, .LBB2, .LBE3  — GCC local/constant/block labels
#   $a, $d, $t                   — ARM ELF mapping symbols
_BAD_SYMBOL_RE = re.compile(r"^(\.[A-Z]+\d+|\$[adtx])$")


def heuristic_stack_walk(dump: CoreDump, thread: Thread, max_depth: int = 32,
                         elf_path: Optional[str] = None) -> List[int]:
    """
    Scan stack memory for return addresses in the code segment.

    This is a heuristic approach — it finds addresses on the stack that
    fall within the executable code range. Not all will be actual return
    addresses, but it's a good starting point.
    """
    # Prefer .text section range (excludes .rodata) when ELF is available;
    # fall back to the full code segment from the dump.
    text_range = _find_text_range(elf_path) if elf_path else None
    code_seg = text_range or _find_code_segment(dump)
    if not code_seg:
        return []

    code_base, code_size = code_seg
    code_end = code_base + code_size

    addresses = []
    if not thread.regs:
        return addresses

    # Add PC and LR first — use the full segment range for these two so
    # we never lose the actual crash address even if it's outside .text
    # (e.g. executing from a trampoline in .plt).
    full_seg = _find_code_segment(dump)
    if full_seg:
        seg_base, seg_size = full_seg
        seg_end = seg_base + seg_size
        if seg_base <= thread.regs.pc < seg_end:
            addresses.append(thread.regs.pc)
        if seg_base <= thread.regs.lr < seg_end:
            addresses.append(thread.regs.lr)
    else:
        if code_base <= thread.regs.pc < code_end:
            addresses.append(thread.regs.pc)
        if code_base <= thread.regs.lr < code_end:
            addresses.append(thread.regs.lr)

    # Scan stack region around SP
    sp = thread.regs.sp
    # Scan 512 bytes below SP and 2048 bytes above
    scan_start = sp - 512
    scan_end = sp + 2048

    for addr in range(scan_start, scan_end, 4):
        val = dump.read_u32(addr)
        if val is None:
            continue
        # Check if value looks like a code address (with Thumb bit cleared)
        candidate = val & ~1  # Clear Thumb bit
        if code_base <= candidate < code_end:
            if candidate not in addresses:
                addresses.append(candidate)
                if len(addresses) >= max_depth:
                    break

    return addresses


def dwarf_stack_walk(dump: CoreDump, thread: Thread, elf_path: str,
                     max_depth: int = 32) -> List[int]:
    """
    Walk the stack using DWARF Call Frame Information (CFI).

    Uses pyelftools to parse .debug_frame/.eh_frame from the ELF,
    then applies CFI rules starting from the crashed PC/SP to recover
    each caller's return address.
    """
    try:
        from elftools.elf.elffile import ELFFile
        from elftools.dwarf.callframe import CIE, FDE
    except ImportError:
        return []

    if not thread.regs:
        return []

    code_seg = _find_code_segment(dump)
    if not code_seg:
        return []

    code_base = code_seg[0]
    addresses = []

    try:
        with open(elf_path, "rb") as f:
            elf = ELFFile(f)

            if not elf.has_dwarf_info():
                return []

            dwarf = elf.get_dwarf_info()

            # Try both .debug_frame and .eh_frame
            cfi_entries = None
            if dwarf.has_CFI():
                cfi_entries = dwarf.CFI_entries()
            elif dwarf.has_EH_CFI():
                cfi_entries = dwarf.EH_CFI_entries()

            if not cfi_entries:
                return []

            # Build FDE lookup: map address ranges to FDEs
            fde_list = []
            for entry in cfi_entries:
                if isinstance(entry, FDE):
                    fde_list.append(entry)

            # Start unwinding from current registers
            # The ELF addresses are relative to 0 — the module base offset
            # needs to be subtracted to get ELF-relative addresses
            pc = thread.regs.pc
            sp = thread.regs.sp

            addresses.append(pc)
            if code_base <= (thread.regs.lr & ~1) < code_base + code_seg[1]:
                addresses.append(thread.regs.lr & ~1)

            # For each frame, find the FDE covering the current PC,
            # apply its CFI rules to compute the previous frame's SP and RA
            current_sp = sp
            current_pc = pc - code_base  # ELF-relative

            for _ in range(max_depth):
                fde = None
                for f in fde_list:
                    if f['initial_location'] <= current_pc < f['initial_location'] + f['address_range']:
                        fde = f
                        break

                if fde is None:
                    break

                # Get the CFA (Canonical Frame Address) rule
                # and the return address rule from the FDE
                decoded = fde.get_decoded()
                table = decoded.table

                if not table:
                    break

                # Find the row for our current PC
                row = None
                for r in table:
                    if r['pc'] <= current_pc:
                        row = r
                    else:
                        break

                if row is None:
                    break

                # Extract CFA rule
                cfa = row.get('cfa')
                if cfa is None:
                    break

                if hasattr(cfa, 'reg') and hasattr(cfa, 'offset'):
                    # CFA = register + offset
                    # ARM: reg 13 = SP, reg 11 = R11 (FP)
                    if cfa.reg == 13:
                        new_sp = current_sp + cfa.offset
                    else:
                        break  # Can't resolve other registers without full state
                else:
                    break

                # Get return address (RA) — ARM: register 14 (LR)
                ra_rule = row.get(14)
                if ra_rule is None:
                    break

                if hasattr(ra_rule, 'offset'):
                    # RA saved at CFA + offset
                    ra_addr = new_sp + ra_rule.offset
                    ra = dump.read_u32(ra_addr)
                    if ra is None:
                        break
                else:
                    break

                ra_cleared = ra & ~1
                if ra_cleared < code_base or ra_cleared >= code_base + code_seg[1]:
                    break

                if ra_cleared not in addresses:
                    addresses.append(ra_cleared)

                current_sp = new_sp
                current_pc = ra_cleared - code_base

    except Exception:
        pass  # Fall back to heuristic

    return addresses


def symbolicate_addresses(addresses: List[int], elf_path: str,
                          addr2line: str = "arm-vita-eabi-addr2line") -> Dict[int, SourceLocation]:
    """
    Resolve addresses to source locations using addr2line.

    Uses -fCia flags for:
      -f: function names
      -C: demangle C++ names
      -i: inline chain resolution
      -a: print address
    """
    if not addresses or not elf_path:
        return {}

    # Build addr2line command with all addresses
    hex_addrs = [f"0x{a:x}" for a in addresses]
    cmd = [addr2line, "-e", elf_path, "-fCia"] + hex_addrs

    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return {}

    if result.returncode != 0:
        return {}

    # Parse output: for each address, addr2line outputs:
    #   0x<addr>
    #   <function>
    #   <file>:<line>
    # With -i, inlined functions produce additional function/file pairs
    # before the final (non-inlined) location.
    locations: Dict[int, SourceLocation] = {}
    lines = result.stdout.strip().split("\n")

    i = 0
    while i < len(lines):
        line = lines[i].strip()
        # Look for address line (0x...)
        if line.startswith("0x"):
            try:
                addr = int(line, 16)
            except ValueError:
                i += 1
                continue

            # Collect function/file pairs (may be multiple for inlines)
            pairs = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("0x"):
                func_line = lines[i].strip()
                i += 1
                if i < len(lines) and not lines[i].strip().startswith("0x"):
                    file_line = lines[i].strip()
                    i += 1
                else:
                    file_line = "??:0"

                # Parse file:line
                match = re.match(r"^(.+):(\d+)(?:\s.*)?$", file_line)
                if match:
                    filepath = match.group(1)
                    lineno = int(match.group(2))
                else:
                    filepath = file_line
                    lineno = 0

                pairs.append(SourceLocation(
                    function=func_line if func_line != "??" else "unknown",
                    file=filepath,
                    line=lineno,
                ))

            if pairs:
                # Last pair is the actual location; earlier ones are inlined callers
                loc = pairs[-1]
                for prev in reversed(pairs[:-1]):
                    prev.inlined_from = loc
                    loc = prev
                locations[addr] = pairs[0] if len(pairs) == 1 else pairs[0]

        else:
            i += 1

    return locations


def build_stack_trace(dump: CoreDump, thread: Thread, elf_path: Optional[str],
                      max_depth: int = 32,
                      addr2line: str = "arm-vita-eabi-addr2line") -> StackTrace:
    """Build a complete symbolicated stack trace for a thread."""
    trace = StackTrace(
        thread_name=thread.name,
        thread_id=thread.tid,
        exception_type=thread.stop_reason_str,
    )

    code_seg = _find_code_segment(dump)

    # Collect candidate return addresses.
    # Start with the thread-info PC (the original crash/wait PC) and
    # register PC/LR, then supplement with heuristic stack scan.
    addresses = []

    # Thread-info PC is the most important address
    if thread.pc:
        addresses.append(thread.pc)

    if thread.regs:
        # Register PC may differ from thread-info PC (e.g. after exception
        # dispatch), include it if distinct
        reg_pc = thread.regs.pc
        if reg_pc and reg_pc not in addresses:
            addresses.append(reg_pc)

        lr = thread.regs.lr & ~1  # clear Thumb bit
        if lr and lr not in addresses:
            addresses.append(lr)

    # Heuristic stack scan for more return addresses
    heuristic = heuristic_stack_walk(dump, thread, max_depth, elf_path=elf_path)
    for addr in heuristic:
        if addr not in addresses:
            addresses.append(addr)

    addresses = addresses[:max_depth]

    # Symbolicate all addresses in one batch
    symbolicated = {}
    if elf_path and addresses:
        symbolicated = symbolicate_addresses(addresses, elf_path, addr2line)

    # Number of "anchor" addresses (thread-PC, reg-PC, LR) that are always
    # kept regardless of symbol quality — they are the real crash context.
    n_anchors = len(addresses) - len(heuristic)

    # Build frames
    for idx, addr in enumerate(addresses):
        module_ref = dump.resolve_address(addr)
        offset = None
        if code_seg:
            code_base, code_size = code_seg
            if code_base <= addr < code_base + code_size:
                offset = addr - code_base

        source = symbolicated.get(addr)

        # Filter heuristic frames with bad symbols (.LC*, $a/$d/$t, fully
        # unresolved).  Anchor frames (PC, LR) are always kept.
        if idx >= n_anchors and source:
            if _BAD_SYMBOL_RE.match(source.function):
                continue
            if source.function == "unknown" and source.file == "??" and source.line == 0:
                continue

        frame = StackFrame(
            index=len(trace.frames),
            address=addr,
            offset_in_module=offset,
            module_ref=module_ref,
            source=source,
        )
        trace.frames.append(frame)

    return trace

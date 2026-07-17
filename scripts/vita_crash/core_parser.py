"""
Binary parser for PS Vita .psp2dmp crash dump files.

.psp2dmp format: gzip-compressed ELF with:
  - PT_NOTE segments containing MODULE_INFO, THREAD_INFO, THREAD_REG_INFO
  - PT_LOAD segments containing raw memory pages (stack, heap)

Note descriptor format (common header for THREAD_INFO, THREAD_REG_INFO,
MODULE_INFO): 12 bytes — (total_count:u32, count_in_note:u32, entry_size:u32).
For MODULE_INFO, entry_size=0 (variable-length entries).
Multiple PT_NOTE segments may contain notes of the same type; entries are
accumulated across all segments.
"""

import gzip
import struct
import io
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple


# ── ELF constants ──────────────────────────────────────────────────────

ELF_MAGIC = b"\x7fELF"
PT_NOTE = 4
PT_LOAD = 1

# Vita core NOTE types (from SCE kernel coredump format)
NT_PSP2_THREAD_INFO = 0x1003
NT_PSP2_THREAD_REG_INFO = 0x1004
NT_PSP2_MODULE_INFO = 0x1005

# Known stop reasons — encoding: (severity << 16) | SceExcpKind
# Low nibble maps to ARM exception vector: 1=undef, 2=SWI, 3=prefetch abort, 4=data abort
STOP_REASONS = {
    0x00000: "No exception",
    0x10002: "Undefined instruction",
    0x20003: "Prefetch abort",
    0x30003: "Prefetch abort",
    0x30004: "Data abort",
}

# Fallback: decode low nibble as SceExcpKind when exact value is unknown
_EXCEPTION_TYPES = {
    1: "Undefined instruction",
    2: "Software interrupt",
    3: "Prefetch abort",
    4: "Data abort",
}


@dataclass
class ModuleSegment:
    """A single segment (text or data) of a loaded module."""
    index: int
    base: int
    size: int
    perms: int  # rwx flags


@dataclass
class Module:
    """A loaded module in the crash dump."""
    name: str
    segments: List[ModuleSegment] = field(default_factory=list)

    def contains(self, addr: int) -> Optional[Tuple[int, int]]:
        """Return (segment_index, offset) if addr falls within this module."""
        for seg in self.segments:
            if seg.base <= addr < seg.base + seg.size:
                return seg.index, addr - seg.base
        return None


@dataclass
class ThreadRegisters:
    """ARM register set for a thread."""
    r: List[int] = field(default_factory=lambda: [0] * 13)  # R0-R12
    sp: int = 0
    lr: int = 0
    pc: int = 0
    cpsr: int = 0
    fpscr: int = 0


@dataclass
class Thread:
    """A thread captured in the crash dump."""
    name: str
    tid: int
    stop_reason: int
    status: int
    pc: int
    regs: Optional[ThreadRegisters] = None

    @property
    def stop_reason_str(self) -> str:
        if self.stop_reason in STOP_REASONS:
            return STOP_REASONS[self.stop_reason]
        kind = self.stop_reason & 0xF
        exc = _EXCEPTION_TYPES.get(kind)
        if exc:
            return f"{exc} (0x{self.stop_reason:x})"
        return f"Unknown exception (0x{self.stop_reason:x})"

    @property
    def crashed(self) -> bool:
        return self.stop_reason != 0


@dataclass
class MemoryRegion:
    """A raw memory region from PT_LOAD."""
    vaddr: int
    data: bytes

    def contains(self, addr: int) -> bool:
        return self.vaddr <= addr < self.vaddr + len(self.data)

    def read_u32(self, addr: int) -> Optional[int]:
        if not self.contains(addr) or not self.contains(addr + 3):
            return None
        off = addr - self.vaddr
        return struct.unpack_from("<I", self.data, off)[0]


@dataclass
class CoreDump:
    """Parsed PS Vita core dump."""
    modules: List[Module] = field(default_factory=list)
    threads: List[Thread] = field(default_factory=list)
    memory: List[MemoryRegion] = field(default_factory=list)

    @property
    def crashed_thread(self) -> Optional[Thread]:
        for t in self.threads:
            if t.crashed:
                return t
        return None

    def resolve_address(self, addr: int) -> Optional[str]:
        """Resolve an address to module@segment + offset."""
        for mod in self.modules:
            result = mod.contains(addr)
            if result:
                seg_idx, offset = result
                return f"{mod.name}@{seg_idx + 1} + 0x{offset:x}"
        return None

    def read_u32(self, addr: int) -> Optional[int]:
        """Read a 32-bit LE value from memory."""
        for region in self.memory:
            val = region.read_u32(addr)
            if val is not None:
                return val
        return None


def _align4(n: int) -> int:
    return (n + 3) & ~3


def _read_cstr(data: bytes, offset: int) -> str:
    """Read a null-terminated string from bytes."""
    end = data.index(b"\x00", offset) if b"\x00" in data[offset:] else len(data)
    return data[offset:end].decode("utf-8", errors="replace")


def _parse_elf_header(data: bytes) -> dict:
    """Parse minimal ELF32 header fields."""
    if data[:4] != ELF_MAGIC:
        raise ValueError("Not an ELF file")
    # ELF32 header: e_phoff at offset 28, e_phentsize at 42, e_phnum at 44
    e_phoff = struct.unpack_from("<I", data, 28)[0]
    e_phentsize = struct.unpack_from("<H", data, 42)[0]
    e_phnum = struct.unpack_from("<H", data, 44)[0]
    return {"phoff": e_phoff, "phentsize": e_phentsize, "phnum": e_phnum}


def _parse_phdr(data: bytes, offset: int) -> dict:
    """Parse an ELF32 program header."""
    fields = struct.unpack_from("<IIIIIIII", data, offset)
    return {
        "type": fields[0],
        "offset": fields[1],
        "vaddr": fields[2],
        "paddr": fields[3],
        "filesz": fields[4],
        "memsz": fields[5],
        "flags": fields[6],
        "align": fields[7],
    }


def _parse_notes(note_data: bytes) -> list:
    """Parse ELF NOTE entries from a segment."""
    notes = []
    off = 0
    while off < len(note_data):
        if off + 12 > len(note_data):
            break
        namesz, descsz, ntype = struct.unpack_from("<III", note_data, off)
        off += 12
        name = note_data[off : off + namesz].rstrip(b"\x00").decode("ascii", errors="replace")
        off += _align4(namesz)
        desc = note_data[off : off + descsz]
        off += _align4(descsz)
        notes.append({"name": name, "type": ntype, "desc": desc})
    return notes


def _parse_module_info_v2(desc: bytes, modules: List[Module]):
    """
    Parse MODULE_INFO (type 0x1005) note descriptor.

    Descriptor layout:
      12-byte header: total(u32), count(u32), entry_size(u32=0 for variable)
      Variable-length module entries (accumulated into modules list).

    Per-module entry layout:
      +0:  uid (u32)
      +4:  7 u32 fields (sdk_version, flags, type, addresses)
      +32: name (32 bytes, null-terminated)
      +64: type_field (u32, typically 2)
      +68: fingerprint (u32, module NID)
      +72: num_segments (u32)
      +76: reserved (u32, 0)
      +80: segments (num_segments × 20 bytes each)
      then 20 bytes trailing data (exidx pointers + null)

    Per-segment (20 bytes):
      +0: flags (u32, 0x05=text, 0x06=data)
      +4: vaddr (u32)
      +8: memsz (u32)
      +12: alignment (u32)
      +16: extra (u32)

    Entry stride: 76 + num_segments * 20 + 20
    """
    if len(desc) < 12:
        return

    _total, count, _entry_size = struct.unpack_from("<III", desc, 0)
    off = 12

    for _ in range(count):
        if off + 80 > len(desc):
            break

        name_bytes = desc[off + 32 : off + 64]
        name = name_bytes.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        num_segments = struct.unpack_from("<I", desc, off + 72)[0]

        # Sanity-check segment count
        if num_segments > 8:
            break

        mod = Module(name=name)
        seg_off = off + 80
        for seg_idx in range(num_segments):
            if seg_off + 20 > len(desc):
                break
            flags, vaddr, memsz, alignment, _extra = struct.unpack_from(
                "<IIIII", desc, seg_off
            )
            perms = 0
            if flags & 0x04:
                perms |= 1  # read
            if flags & 0x01:
                perms |= 2  # execute
            if flags & 0x02:
                perms |= 4  # write
            mod.segments.append(ModuleSegment(
                index=seg_idx,
                base=vaddr,
                size=memsz,
                perms=perms,
            ))
            seg_off += 20

        modules.append(mod)
        # Advance: 76 (fixed header) + num_segments * 20 (segs) + 20 (trailing)
        off += 76 + num_segments * 20 + 20


def _parse_thread_info(desc: bytes, threads: List[Thread]):
    """
    Parse THREAD_INFO (type 0x1003) note descriptor.

    Descriptor layout:
      12-byte header: total(u32), count_in_note(u32), entry_size(u32)
      Followed by count_in_note entries of entry_size bytes each.

    Per-entry layout (200 bytes):
      +0:   tid (u32)
      +4:   name (32 bytes, null-terminated)
      +48:  pc (u32)
      +112: stop_reason (u32)

    Entries are accumulated into the threads list across multiple notes.
    """
    if len(desc) < 12:
        return

    _total, count, entry_size = struct.unpack_from("<III", desc, 0)

    if entry_size == 0 or count == 0:
        return

    for i in range(count):
        off = 12 + i * entry_size
        # Need at least 116 bytes to read through stop_reason at +112
        if off + 116 > len(desc):
            break

        tid = struct.unpack_from("<I", desc, off)[0]
        name_bytes = desc[off + 4 : off + 36]
        name = name_bytes.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        pc = struct.unpack_from("<I", desc, off + 48)[0]
        stop_reason = struct.unpack_from("<I", desc, off + 112)[0]

        # Derive status: 0 for crashed, 8 (waiting) otherwise
        status = 0 if stop_reason != 0 else 8

        threads.append(Thread(
            name=name,
            tid=tid,
            stop_reason=stop_reason,
            status=status,
            pc=pc,
        ))


def _parse_thread_reg_info(desc: bytes, threads: List[Thread]):
    """
    Parse THREAD_REG_INFO (type 0x1004) and attach register sets to threads.

    Descriptor layout:
      12-byte header: total(u32), count_in_note(u32), entry_size(u32)
      Followed by count_in_note entries of entry_size bytes each.

    Per-entry layout (372–376 bytes):
      +0:  tid (u32)
      +4:  R0 (u32)
      +8:  R1 (u32)
      ...
      +52: R12 (u32)
      +56: SP (u32)
      +60: LR (u32)
      +64: PC (u32)
      +68: CPSR (u32)
      +72: VFP/NEON registers (remaining bytes)

    Entries are matched to threads by tid.
    """
    if len(desc) < 12:
        return

    _total, count, entry_size = struct.unpack_from("<III", desc, 0)

    if entry_size == 0 or count == 0:
        return

    for i in range(count):
        off = 12 + i * entry_size
        # Need at least 72 bytes to read through CPSR at +68
        if off + 72 > len(desc):
            break

        tid = struct.unpack_from("<I", desc, off)[0]
        # R0-R12 at +4..+52, SP at +56, LR at +60, PC at +64, CPSR at +68
        regs_raw = struct.unpack_from("<17I", desc, off + 4)
        # FPSCR at +72 (first field of VFP save area)
        fpscr = 0
        if off + 76 <= len(desc):
            fpscr = struct.unpack_from("<I", desc, off + 72)[0]

        reg = ThreadRegisters(
            r=list(regs_raw[:13]),
            sp=regs_raw[13],
            lr=regs_raw[14],
            pc=regs_raw[15],
            cpsr=regs_raw[16],
            fpscr=fpscr,
        )

        for t in threads:
            if t.tid == tid:
                t.regs = reg
                break


def parse_core_dump(path: str) -> CoreDump:
    """
    Parse a .psp2dmp file and return a CoreDump.

    The file may be gzip-compressed or raw ELF.
    Notes of the same type may appear in multiple PT_NOTE segments;
    entities are accumulated across all of them.
    """
    with open(path, "rb") as f:
        raw = f.read()

    # Try gzip decompression
    try:
        data = gzip.decompress(raw)
    except (gzip.BadGzipFile, OSError):
        data = raw

    if data[:4] != ELF_MAGIC:
        raise ValueError(f"Not a valid ELF core dump: {path}")

    hdr = _parse_elf_header(data)
    dump = CoreDump()

    # Collect all program headers
    phdrs = []
    for i in range(hdr["phnum"]):
        offset = hdr["phoff"] + i * hdr["phentsize"]
        phdrs.append(_parse_phdr(data, offset))

    # Parse NOTE segments — accumulate entities across all segments
    for ph in phdrs:
        if ph["type"] == PT_NOTE:
            note_data = data[ph["offset"] : ph["offset"] + ph["filesz"]]
            for note in _parse_notes(note_data):
                if note["type"] == NT_PSP2_MODULE_INFO:
                    _parse_module_info_v2(note["desc"], dump.modules)
                elif note["type"] == NT_PSP2_THREAD_INFO:
                    _parse_thread_info(note["desc"], dump.threads)
                elif note["type"] == NT_PSP2_THREAD_REG_INFO:
                    _parse_thread_reg_info(note["desc"], dump.threads)

    # Parse LOAD segments for memory
    for ph in phdrs:
        if ph["type"] == PT_LOAD and ph["filesz"] > 0:
            dump.memory.append(MemoryRegion(
                vaddr=ph["vaddr"],
                data=data[ph["offset"] : ph["offset"] + ph["filesz"]],
            ))

    return dump

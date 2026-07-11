#!/usr/bin/env python3
"""reSF2 S3E Binary Analysis Pipeline

This script provides a full analysis pipeline for the Shadow Fight 2 S3E binary:
  1. Extract ShadowFight2.s3e from the APK (assets/ShadowFight2.s3e)
  2. LZMA1-decompress the .s3e file to get the raw S3E payload
  3. Parse the S3E header (XE3U magic, 76 bytes)
  4. Extract the embedded Marmalade config text (s3e.icf)
  5. Parse the import-name table (346 function names)
  6. Parse the fixup/relocation table (4 sections: symbols, internal relocs, external relocs)
  7. Extract all strings from the code section
  8. Disassemble the entry point and key functions using Capstone (x86_64)

Usage:
  python3 s3e_analyze.py <path_to_apk_or_s3e_or_bin>

The script auto-detects the input type:
  - .apk → extract assets/ShadowFight2.s3e first
  - .s3e → LZMA-decompress first
  - .bin (already decompressed) → parse directly

Output goes to /home/z/my-project/work/s3e_analysis/
"""
from __future__ import annotations

import lzma
import os
import re
import struct
import sys
import zipfile
from pathlib import Path
from typing import Optional

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

WORK_DIR = Path("/home/z/my-project/work/s3e_analysis")
WORK_DIR.mkdir(parents=True, exist_ok=True)

# S3E header constants (from docs/09_s3e_binary_format.md)
S3E_MAGIC = b"XE3U"
S3E_HEADER_SIZE = 0x4C  # 76 bytes
BASE_ADDR = 0x4A000000  # load base address


# ─── Stage 1: Input preprocessing ──────────────────────────────────────

def extract_s3e_from_apk(apk_path: str) -> bytes:
    """Extract assets/ShadowFight2.s3e from the APK (ZIP) and return its
    raw LZMA-compressed bytes."""
    print(f"[apk] extracting assets/ShadowFight2.s3e from {apk_path}", flush=True)
    with zipfile.ZipFile(apk_path, "r") as zf:
        # Find the s3e file — it might be at different paths
        s3e_names = [n for n in zf.namelist() if n.endswith(".s3e")]
        if not s3e_names:
            raise SystemExit("no .s3e file found in APK")
        print(f"[apk] found s3e files: {s3e_names}", flush=True)
        s3e_name = s3e_names[0]
        if len(s3e_names) > 1:
            # Prefer assets/ShadowFight2.s3e
            for n in s3e_names:
                if "ShadowFight2" in n:
                    s3e_name = n
                    break
        print(f"[apk] using: {s3e_name}", flush=True)
        data = zf.read(s3e_name)
        print(f"[apk] s3e compressed size: {len(data)} bytes", flush=True)
        return data


def lzma_decompress_s3e(compressed: bytes) -> bytes:
    """Decompress the LZMA1 legacy stream (13-byte header + LZMA data).

    Header layout:
      0x00  1  props byte (lc/lp/pb encoded)
      0x01  4  dict_size (LE)
      0x05  8  uncomp_size (LE)
      0x0d  …  LZMA1 stream
    """
    if len(compressed) < 13:
        raise SystemExit("s3e file too small for LZMA header")
    props = compressed[0]
    dict_size = struct.unpack("<I", compressed[1:5])[0]
    uncomp_size = struct.unpack("<Q", compressed[5:13])[0]
    lc = props % 9
    rem = props // 9
    lp = rem % 5
    pb = rem // 5
    print(f"[lzma] props=0x{props:02x} dict_size={dict_size} "
          f"uncomp_size={uncomp_size} lc={lc} lp={lp} pb={pb}", flush=True)
    filt = [{"id": lzma.FILTER_LZMA1, "dict_size": dict_size,
             "lc": lc, "lp": lp, "pb": pb,
             "mode": lzma.MODE_NORMAL, "preset": 6}]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=filt)
    out = dec.decompress(compressed[13:], max_length=uncomp_size)
    print(f"[lzma] decompressed: {len(out)} bytes "
          f"(expected {uncomp_size}, match={len(out)==uncomp_size})", flush=True)
    if len(out) != uncomp_size:
        print(f"[lzma] WARNING: size mismatch!", flush=True)
    return out


# ─── Stage 2: S3E header parsing ───────────────────────────────────────

def parse_header(data: bytes) -> dict:
    """Parse the 76-byte S3E header."""
    if len(data) < S3E_HEADER_SIZE:
        raise SystemExit(f"data too small for header: {len(data)} < {S3E_HEADER_SIZE}")
    magic = data[0:4]
    if magic != S3E_MAGIC:
        raise SystemExit(f"bad magic: {magic!r} (expected {S3E_MAGIC!r})")
    h = {}
    h["magic"] = magic.decode("ascii")
    h["u32_04"] = struct.unpack_from("<I", data, 0x04)[0]
    h["u32_08"] = struct.unpack_from("<I", data, 0x08)[0]
    h["flags"] = struct.unpack_from("<H", data, 0x08)[0]
    h["arch"] = struct.unpack_from("<H", data, 0x0A)[0]
    h["fixup_offset"] = struct.unpack_from("<I", data, 0x0C)[0]
    h["fixup_size"] = struct.unpack_from("<I", data, 0x10)[0]
    h["code_offset"] = struct.unpack_from("<I", data, 0x14)[0]
    h["code_file_size"] = struct.unpack_from("<I", data, 0x18)[0]
    h["code_mem_size"] = struct.unpack_from("<I", data, 0x1C)[0]
    h["sig_offset"] = struct.unpack_from("<I", data, 0x20)[0]
    h["sig_size"] = struct.unpack_from("<I", data, 0x24)[0]
    h["entry_offset"] = struct.unpack_from("<I", data, 0x28)[0]
    h["config_offset"] = struct.unpack_from("<I", data, 0x2C)[0]
    h["config_size"] = struct.unpack_from("<I", data, 0x30)[0]
    h["base_addr"] = struct.unpack_from("<I", data, 0x34)[0]
    h["extra_offset"] = struct.unpack_from("<I", data, 0x38)[0]
    h["extra_size"] = struct.unpack_from("<I", data, 0x3C)[0]
    h["ext_header_size"] = struct.unpack_from("<I", data, 0x40)[0]
    h["data_seg_offset"] = struct.unpack_from("<I", data, 0x44)[0]
    h["is_juice"] = struct.unpack_from("<I", data, 0x48)[0]
    return h


ARCH_NAMES = [
    "ARMv4t", "ARMv4", "ARMv5t", "ARMv5te", "ARMv5tej", "ARMv6", "ARMv6k",
    "ARMv6t2", "ARMv6z", "x86", "PPC", "AMD64", "x86_64", "ARMv7a",
    "ARMv8a", "ARMv8a-aarch64", "NACL-x86_64",
]


def format_header(h: dict) -> str:
    """Pretty-print the header."""
    arch_idx = h["arch"] & 0xFF
    arch_name = ARCH_NAMES[arch_idx] if arch_idx < len(ARCH_NAMES) else f"unknown({arch_idx})"
    vfp = (h["arch"] >> 8) != 0
    lines = [
        "=== S3E HEADER ===",
        f"  magic            = {h['magic']!r}",
        f"  version          = 0x{h['u32_04']:08x}",
        f"  flags            = 0x{h['flags']:04x} ({bin(h['flags'])})",
        f"  arch             = 0x{h['arch']:04x} ({arch_name}{' with VFP' if vfp else ''})",
        f"  fixup_offset     = 0x{h['fixup_offset']:08x}",
        f"  fixup_size       = 0x{h['fixup_size']:08x} ({h['fixup_size']} bytes)",
        f"  code_offset      = 0x{h['code_offset']:08x}",
        f"  code_file_size   = 0x{h['code_file_size']:08x} ({h['code_file_size']} bytes)",
        f"  code_mem_size    = 0x{h['code_mem_size']:08x} ({h['code_mem_size']} bytes)",
        f"  sig_offset       = 0x{h['sig_offset']:08x}",
        f"  sig_size         = 0x{h['sig_size']:08x} ({h['sig_size']} bytes)",
        f"  entry_offset     = 0x{h['entry_offset']:08x}",
        f"  config_offset    = 0x{h['config_offset']:08x}",
        f"  config_size      = 0x{h['config_size']:08x} ({h['config_size']} bytes)",
        f"  base_addr        = 0x{h['base_addr']:08x}",
        f"  extra_offset     = 0x{h['extra_offset']:08x}",
        f"  extra_size       = 0x{h['extra_size']:08x} ({h['extra_size']} bytes)",
        f"  ext_header_size  = 0x{h['ext_header_size']:08x}",
        f"  data_seg_offset  = 0x{h['data_seg_offset']:08x}",
        f"  is_juice         = 0x{h['is_juice']:08x}",
    ]
    # Compute section sizes
    bss_size = h["code_mem_size"] - h["code_file_size"]
    if h["data_seg_offset"] > 0:
        code_size = h["data_seg_offset"]
        data_size = h["code_file_size"] - h["data_seg_offset"]
        lines.append(f"  --- sections ---")
        lines.append(f"  code section     = 0x{code_size:08x} ({code_size} bytes)")
        lines.append(f"  data section     = 0x{data_size:08x} ({data_size} bytes)")
    else:
        lines.append(f"  code+data        = 0x{h['code_file_size']:08x}")
    lines.append(f"  bss (implicit)   = 0x{bss_size:08x} ({bss_size} bytes)")
    lines.append(f"  --- memory layout ---")
    lines.append(f"  code vaddr       = 0x{h['base_addr']:08x}")
    if h["data_seg_offset"] > 0:
        lines.append(f"  data vaddr       = 0x{h['base_addr'] + h['data_seg_offset']:08x}")
    lines.append(f"  bss vaddr        = 0x{h['base_addr'] + h['code_file_size']:08x}")
    return "\n".join(lines)


# ─── Stage 3: Config text extraction ───────────────────────────────────

def extract_config(data: bytes, h: dict) -> str:
    """Extract the embedded Marmalade config text (s3e.icf)."""
    off = h["config_offset"]
    size = h["config_size"]
    return data[off:off + size].decode("latin-1", errors="replace")


# ─── Stage 4: Import-name table parsing ────────────────────────────────

def parse_import_names(data: bytes, h: dict) -> list[tuple[int, str]]:
    """Parse the import-name table at fixup_offset.

    The fixup table starts with section type 0 (symbol names), which has:
      4 bytes: type (0)
      4 bytes: section size + 8
      2 bytes: symbol count
      N null-terminated strings
    """
    imports = []
    pos = h["fixup_offset"]
    section_type = struct.unpack_from("<I", data, pos)[0]
    section_size = struct.unpack_from("<I", data, pos + 4)[0] - 8
    if section_type != 0:
        print(f"[imports] WARNING: first fixup section is type {section_type}, expected 0", flush=True)
        return imports
    pos += 8
    symbol_count = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    print(f"[imports] {symbol_count} symbols in section 0", flush=True)
    for i in range(symbol_count):
        end = data.index(b"\x00", pos)
        name = data[pos:end].decode("latin-1", errors="replace")
        imports.append((pos, name))
        pos = end + 1
    return imports


# ─── Stage 5: Fixup/relocation table parsing ───────────────────────────

def parse_fixup_table(data: bytes, h: dict) -> dict:
    """Parse all sections of the fixup table.

    The fixup table contains 4 sections:
      type 0: external symbol names
      type 1: internal relocations (4-byte offsets into GOT)
      type 2/3/4: external relocations (offset + symbol index)
    """
    result = {"sections": [], "symbols": [], "internal_relocs": [], "external_relocs": []}
    pos = h["fixup_offset"]
    end = h["fixup_offset"] + h["fixup_size"]
    section_idx = 0
    while pos < end:
        if pos + 8 > len(data):
            break
        section_type = struct.unpack_from("<I", data, pos)[0]
        section_size = struct.unpack_from("<I", data, pos + 4)[0] - 8
        section_start = pos + 8
        section_end = section_start + section_size
        result["sections"].append({
            "index": section_idx,
            "type": section_type,
            "offset": pos,
            "size": section_size,
        })
        print(f"[fixup] section {section_idx}: type={section_type} "
              f"size=0x{section_size:x} at file offset 0x{pos:x}", flush=True)

        if section_type == 0:
            # Symbol names
            sym_count = struct.unpack_from("<H", data, section_start)[0]
            sp = section_start + 2
            for i in range(sym_count):
                se = data.index(b"\x00", sp)
                name = data[sp:se].decode("latin-1", errors="replace")
                result["symbols"].append(name)
                sp = se + 1

        elif section_type == 1:
            # Internal relocations
            reloc_count = struct.unpack_from("<I", data, section_start)[0]
            rp = section_start + 4
            for i in range(reloc_count):
                offset = struct.unpack_from("<I", data, rp)[0]
                result["internal_relocs"].append(offset)
                rp += 4

        elif section_type in (2, 3, 4):
            # External relocations
            reloc_count = struct.unpack_from("<I", data, section_start)[0]
            rp = section_start + 4
            for i in range(reloc_count):
                hi = struct.unpack_from("<H", data, rp)[0]
                lo = struct.unpack_from("<H", data, rp + 2)[0]
                offset = (hi << 16) | lo
                sym_idx = struct.unpack_from("<H", data, rp + 4)[0]
                result["external_relocs"].append((offset, sym_idx))
                rp += 6

        pos = section_end
        section_idx += 1

    return result


# ─── Stage 6: String extraction ────────────────────────────────────────

def extract_strings(data: bytes, min_len: int = 4) -> list[tuple[int, str]]:
    """Extract all printable ASCII strings of length >= min_len from the
    code+data section."""
    code_start = 0x45251  # standard code offset
    code_end = len(data)
    result = []
    current = bytearray()
    current_start = 0
    for i in range(code_start, code_end):
        b = data[i]
        if 0x20 <= b <= 0x7e:
            if not current:
                current_start = i
            current.append(b)
        else:
            if len(current) >= min_len:
                result.append((current_start, current.decode("ascii")))
            current = bytearray()
    if len(current) >= min_len:
        result.append((current_start, current.decode("ascii")))
    return result


def classify_strings(strings: list[tuple[int, str]]) -> dict:
    """Classify strings by category for easier analysis."""
    categories = {
        "physics": [],
        "animation": [],
        "rendering": [],
        "location": [],
        "dz_archive": [],
        "rtti_class": [],
        "glsl_shader": [],
        "assertion": [],
        "filepath": [],
        "other": [],
    }
    for offset, s in strings:
        if s.startswith("N7cocos2d") or "CCSprite" in s or "CCNode" in s:
            categories["rtti_class"].append((offset, s))
        elif "ModelPhysics" in s or "Physics" in s or "Verlet" in s:
            categories["physics"].append((offset, s))
        elif "Animation" in s or "MoveInside" in s or "IntervalAttack" in s:
            categories["animation"].append((offset, s))
        elif "texture" in s.lower() or "Shader" in s or "gl_Frag" in s:
            categories["rendering"].append((offset, s))
        elif "ImageLayer" in s or "background" in s.lower() or "parallax" in s.lower():
            categories["location"].append((offset, s))
        elif "dzip" in s.lower() or "derbh" in s.lower() or "decompress" in s.lower():
            categories["dz_archive"].append((offset, s))
        elif "precision" in s and "float" in s:
            categories["glsl_shader"].append((offset, s))
        elif "assert" in s.lower() or "Assertion" in s:
            categories["assertion"].append((offset, s))
        elif "/" in s and "." in s and len(s) < 200:
            categories["filepath"].append((offset, s))
        else:
            categories["other"].append((offset, s))
    return categories


# ─── Stage 7: Disassembly ──────────────────────────────────────────────

def disassemble_entry(data: bytes, h: dict, num_bytes: int = 256) -> str:
    """Disassemble the entry point using Capstone (x86_64)."""
    if h["entry_offset"] != 0:
        print(f"[disasm] entry_offset is 0x{h['entry_offset']:08x} (non-zero)", flush=True)
        entry_file_offset = h["code_offset"] + h["entry_offset"]
    else:
        # When entry_offset is 0, the entry point is at the start of the code section
        entry_file_offset = h["code_offset"]

    print(f"[disasm] entry file offset = 0x{entry_file_offset:x}, "
          f"vaddr = 0x{h['base_addr'] + h['entry_offset']:08x}", flush=True)

    code = data[entry_file_offset:entry_file_offset + num_bytes]
    if len(code) == 0:
        return "; could not read code at entry point"

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    lines = [
        f"; Entry point disassembly",
        f"; File offset: 0x{entry_file_offset:x}",
        f"; Virtual address: 0x{h['base_addr'] + h['entry_offset']:08x}",
        f"; Base address: 0x{h['base_addr']:08x}",
        f"; Architecture: x86_64",
        "",
    ]
    for insn in md.disasm(code, h["base_addr"] + h["entry_offset"]):
        lines.append(f"  0x{insn.address:08x}:  {insn.mnemonic:8s} {insn.op_str}")
    return "\n".join(lines)


def disassemble_function(data: bytes, h: dict, file_offset: int,
                         num_bytes: int = 512, name: str = "") -> str:
    """Disassemble a function at the given file offset."""
    code = data[file_offset:file_offset + num_bytes]
    if len(code) == 0:
        return f"; no code at offset 0x{file_offset:x}"
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    vaddr = h["base_addr"] + (file_offset - h["code_offset"])
    lines = [f"; Function: {name or 'sub_' + format(vaddr, '08x')}",
             f"; File offset: 0x{file_offset:x}",
             f"; Virtual address: 0x{vaddr:08x}", ""]
    for insn in md.disasm(code, vaddr):
        lines.append(f"  0x{insn.address:08x}:  {insn.mnemonic:8s} {insn.op_str}")
        # Stop at first RET if it looks like end of function
        if insn.mnemonic == "ret" and len(lines) > 10:
            break
    return "\n".join(lines)


# ─── Main pipeline ─────────────────────────────────────────────────────

def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: s3e_analyze.py <apk|s3e|bin>", file=sys.stderr)
        return 1

    input_path = sys.argv[1]
    print(f"[main] input: {input_path}", flush=True)

    # Determine input type and get decompressed S3E bytes
    if input_path.endswith(".apk"):
        compressed = extract_s3e_from_apk(input_path)
        s3e_data = lzma_decompress_s3e(compressed)
        # Cache the decompressed binary
        bin_path = WORK_DIR / "ShadowFight2.bin"
        bin_path.write_bytes(s3e_data)
        print(f"[main] cached decompressed binary: {bin_path}", flush=True)
    elif input_path.endswith(".s3e"):
        compressed = Path(input_path).read_bytes()
        s3e_data = lzma_decompress_s3e(compressed)
        bin_path = WORK_DIR / "ShadowFight2.bin"
        bin_path.write_bytes(s3e_data)
    else:
        # Assume already decompressed .bin
        s3e_data = Path(input_path).read_bytes()
        bin_path = Path(input_path)

    print(f"[main] S3E payload: {len(s3e_data)} bytes", flush=True)

    # Parse header
    header = parse_header(s3e_data)
    header_str = format_header(header)
    print(header_str, flush=True)
    (WORK_DIR / "header.txt").write_text(header_str)

    # Extract config
    config = extract_config(s3e_data, header)
    (WORK_DIR / "config.icf").write_text(config)
    print(f"[main] config text: {len(config)} bytes → config.icf", flush=True)

    # Parse imports
    imports = parse_import_names(s3e_data, header)
    imports_str = "\n".join(f"0x{off:08x} {name}" for off, name in imports)
    (WORK_DIR / "imports.txt").write_text(
        f"# {len(imports)} imported function names\n# Format: <offset> <name>\n\n"
        + imports_str
    )
    print(f"[main] imports: {len(imports)} functions → imports.txt", flush=True)

    # Parse fixup table
    fixups = parse_fixup_table(s3e_data, header)
    fixup_str = (
        f"=== FIXUP TABLE ===\n"
        f"Sections: {len(fixups['sections'])}\n"
        f"Symbols: {len(fixups['symbols'])}\n"
        f"Internal relocs: {len(fixups['internal_relocs'])}\n"
        f"External relocs: {len(fixups['external_relocs'])}\n\n"
    )
    for s in fixups["sections"]:
        fixup_str += f"  section {s['index']}: type={s['type']} size=0x{s['size']:x} @ 0x{s['offset']:x}\n"
    (WORK_DIR / "fixup_table.txt").write_text(fixup_str)
    print(f"[main] fixup table: {len(fixups['sections'])} sections → fixup_table.txt", flush=True)

    # Extract strings
    strings = extract_strings(s3e_data, min_len=4)
    categorized = classify_strings(strings)
    strings_str = f"=== STRING TABLE ({len(strings)} strings) ===\n\n"
    for cat, entries in categorized.items():
        if entries:
            strings_str += f"\n--- {cat} ({len(entries)} strings) ---\n"
            for off, s in entries[:50]:  # limit to 50 per category
                strings_str += f"  0x{off:08x}: {s[:120]!r}\n"
            if len(entries) > 50:
                strings_str += f"  ... and {len(entries)-50} more\n"
    (WORK_DIR / "strings.txt").write_text(strings_str)
    print(f"[main] strings: {len(strings)} extracted → strings.txt", flush=True)
    for cat, entries in categorized.items():
        if entries:
            print(f"    {cat}: {len(entries)}", flush=True)

    # Disassemble entry point
    entry_disasm = disassemble_entry(s3e_data, header, num_bytes=512)
    (WORK_DIR / "entry_disasm.asm").write_text(entry_disasm)
    print(f"[main] entry point disassembly → entry_disasm.asm", flush=True)

    # Summary
    summary = f"""
=== S3E ANALYSIS SUMMARY ===
Input: {input_path}
Payload size: {len(s3e_data)} bytes ({len(s3e_data)/1024/1024:.2f} MB)
Architecture: {ARCH_NAMES[header['arch'] & 0xFF]}
Base address: 0x{header['base_addr']:08x}
Code offset: 0x{header['code_offset']:08x}
Code size: {header['code_file_size']} bytes
Imports: {len(imports)} function names
Fixup sections: {len(fixups['sections'])}
  Symbols: {len(fixups['symbols'])}
  Internal relocs: {len(fixups['internal_relocs'])}
  External relocs: {len(fixups['external_relocs'])}
Strings: {len(strings)} total
Output dir: {WORK_DIR}

Files generated:
  header.txt        — parsed header
  config.icf        — embedded Marmalade config
  imports.txt       — imported function names
  fixup_table.txt   — relocation table summary
  strings.txt       — categorized string table
  entry_disasm.asm  — entry point disassembly
  ShadowFight2.bin  — cached decompressed binary
"""
    print(summary, flush=True)
    (WORK_DIR / "summary.txt").write_text(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())

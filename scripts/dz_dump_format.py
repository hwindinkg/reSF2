#!/usr/bin/env python3
"""Dump raw DTRZ file table to understand the exact format.

The existing parse_dz.py and dz_final.py disagree on:
  1. Name order (files first vs dirs first)
  2. File attribute table size (file_count*6 vs dir_count*6)
  3. File table field layout (u24+u8 vs full u32)

This script dumps the raw bytes so we can determine the correct format.
"""
import struct
import sys
from pathlib import Path


def dump_dz_header(dz_path: str):
    with open(dz_path, "rb") as f:
        data = f.read()

    print(f"=== {dz_path} ({len(data)} bytes) ===\n")

    # Header
    magic = data[:4]
    num_files = struct.unpack_from('<H', data, 4)[0]
    num_dirs = struct.unpack_from('<H', data, 6)[0]
    version = data[8]
    print(f"magic={magic!r} num_files={num_files} num_dirs={num_dirs} version={version}")

    # Read all names (files first, then dirs — per dz_final.py)
    pos = 9
    all_names = []
    for _ in range(num_files + num_dirs):
        end = data.index(b'\x00', pos)
        name = data[pos:end].decode('utf-8', errors='replace')
        all_names.append(name)
        pos = end + 1

    file_names = all_names[:num_files]
    dir_names = all_names[num_files:]
    print(f"\nFirst 5 file names: {file_names[:5]}")
    print(f"First 5 dir names:  {dir_names[:5]}")
    print(f"Names section ends at offset 0x{pos:x}")

    # After names: file attribute table (6 bytes per file, per dz_final.py)
    attr_start = pos
    print(f"\nFile attribute table at 0x{attr_start:x} ({num_files} * 6 = {num_files*6} bytes)")
    print(f"  First 3 entries (raw hex):")
    for i in range(min(3, num_files)):
        off = attr_start + i * 6
        raw = data[off:off+6]
        print(f"    [{i}] {raw.hex()} = {struct.unpack_from('<HHH', data, off)}")

    pos += num_files * 6

    # After file attributes: lengths header (4 bytes per dz_final.py)
    lengths_start = pos
    print(f"\nLengths header at 0x{lengths_start:x} (4 bytes)")
    raw = data[lengths_start:lengths_start+4]
    print(f"  raw: {raw.hex()}")
    pos += 4

    # File table: num_files * 16 bytes
    ft_start = pos
    print(f"\nFile table at 0x{ft_start:x} ({num_files} * 16 = {num_files*16} bytes)")
    print(f"  First 5 entries (raw hex + 2 interpretations):")
    for i in range(min(5, num_files)):
        off = ft_start + i * 16
        raw = data[off:off+16]
        # Interpretation 1: 4 x (u24 + u8) — per README
        f0, f1, f2, f3 = struct.unpack_from('<IIII', data, off)
        v0_24 = f0 & 0xFFFFFF; t0 = (f0 >> 24) & 0xFF
        offset_24 = f1 & 0xFFFFFF; t1 = (f1 >> 24) & 0xFF
        comp_24 = f2 & 0xFFFFFF; t2 = (f2 >> 24) & 0xFF
        uncomp_24 = f3 & 0xFFFFFF; t3 = (f3 >> 24) & 0xFF
        # Interpretation 2: 4 x u32 — per dz_final.py
        u32_0, u32_1, u32_2, u32_3 = struct.unpack_from('<IIII', data, off)
        print(f"    [{i}] {raw.hex()}")
        print(f"        name={file_names[i]!r}")
        print(f"        u24+u8: val0={v0_24} t0={t0} | off={offset_24} t1={t1} | comp={comp_24} t2={t2} | uncomp={uncomp_24} t3={t3}")
        print(f"        u32:   {u32_0} {u32_1} {u32_2} {u32_3}")

    # Check if offsets point into the file
    print(f"\n  Offset validation (u24+u8 interpretation):")
    for i in range(min(5, num_files)):
        off = ft_start + i * 16
        f1 = struct.unpack_from('<I', data, off + 4)[0]
        offset_24 = f1 & 0xFFFFFF
        f2 = struct.unpack_from('<I', data, off + 8)[0]
        comp_24 = f2 & 0xFFFFFF
        f3 = struct.unpack_from('<I', data, off + 12)[0]
        uncomp_24 = f3 & 0xFFFFFF
        t3 = (f3 >> 24) & 0xFF
        end = offset_24 + comp_24
        in_range = offset_24 < len(data) and end <= len(data)
        print(f"    [{i}] {file_names[i]}: off=0x{offset_24:x} comp={comp_24} uncomp={uncomp_24} type={t3} end=0x{end:x} {'OK' if in_range else 'OUT OF RANGE'}")


if __name__ == "__main__":
    dz_path = sys.argv[1] if len(sys.argv) > 1 else \
        "/home/z/my-project/work/sf2_data/sf2/assets/assets/files.dz"
    dump_dz_header(dz_path)

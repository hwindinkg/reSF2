#!/usr/bin/env python3
"""DZ archive parser — CORRECTED format.

The correct DTRZ file table format (16 bytes per file):
  Field 0 (4 bytes): u24 LE uncomp_size + u8 CRC
  Field 1 (4 bytes): u24 LE data_offset  + u8 CRC
  Field 2 (4 bytes): u24 LE comp_size    + u8 type (1=copy, 2=zlib, 4=DZ, 5=LZMA)
  Field 3 (4 bytes): u24 LE reserved(0)  + u8 CRC

All offsets are relative to the start of the data section (which begins
right after the file table).

The old parse_dz.py had the field order wrong (it read type from field 3
instead of field 2, and uncomp_size from field 3 instead of field 0).
This caused all types to be misread and sizes to be garbage.
"""
import struct
import sys
import os
import zlib
from pathlib import Path
from collections import Counter
import math


def shannon_entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = Counter(data)
    total = len(data)
    return -sum(c/total * math.log2(c/total) for c in counts.values() if c > 0)


def parse_dz_correct(dz_path: str):
    with open(dz_path, "rb") as f:
        data = f.read()

    if data[:4] != b"DTRZ":
        raise ValueError(f"Not a DTRZ file: magic={data[:4]!r}")

    num_files = struct.unpack_from('<H', data, 4)[0]
    num_dirs = struct.unpack_from('<H', data, 6)[0]
    version = data[8]
    print(f"DTRZ: {num_files} files, {num_dirs} dirs, version={version}")

    # Read names: files first, then dirs (per dz_final.py)
    pos = 9
    file_names = []
    for _ in range(num_files):
        end = data.index(b'\x00', pos)
        file_names.append(data[pos:end].decode('utf-8', errors='replace'))
        pos = end + 1

    dir_names = []
    for _ in range(num_dirs):
        end = data.index(b'\x00', pos)
        dir_names.append(data[pos:end].decode('utf-8', errors='replace'))
        pos = end + 1

    print(f"  First 5 files: {file_names[:5]}")
    print(f"  First 5 dirs:  {dir_names[:5]}")

    # File attribute table: num_files * 6 bytes
    pos += num_files * 6

    # Lengths header: 4 bytes
    lengths = struct.unpack_from('<HH', data, pos)
    print(f"  Lengths header: {lengths}")
    pos += 4

    # File table: num_files * 16 bytes
    file_table = []
    data_section_start = pos + num_files * 16
    print(f"  File table at 0x{pos:x}, data section at 0x{data_section_start:x}")

    for i in range(num_files):
        off = pos + i * 16
        f0, f1, f2, f3 = struct.unpack_from('<IIII', data, off)
        uncomp_size = f0 & 0xFFFFFF
        crc0 = (f0 >> 24) & 0xFF
        data_offset = f1 & 0xFFFFFF
        crc1 = (f1 >> 24) & 0xFF
        comp_size = f2 & 0xFFFFFF
        comp_type = (f2 >> 24) & 0xFF
        reserved = f3 & 0xFFFFFF
        crc3 = (f3 >> 24) & 0xFF

        file_table.append({
            'name': file_names[i],
            'uncomp_size': uncomp_size,
            'data_offset': data_offset,
            'comp_size': comp_size,
            'type': comp_type,
        })

    # Type distribution
    type_counts = Counter(ft['type'] for ft in file_table)
    print(f"  Type distribution: {dict(sorted(type_counts.items()))}")

    return data, data_section_start, file_table


def main():
    dz_path = sys.argv[1] if len(sys.argv) > 1 else \
        "/home/z/my-project/work/sf2_data/sf2/assets/assets/files.dz"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else \
        "/home/z/my-project/work/dz_extracted"

    data, data_start, file_table = parse_dz_correct(dz_path)

    print(f"\n=== First 10 files ===")
    for ft in file_table[:10]:
        abs_off = data_start + ft['data_offset']
        print(f"  {ft['name']:40s} type={ft['type']} comp={ft['comp_size']:6d} uncomp={ft['uncomp_size']:6d} off=0x{ft['data_offset']:x} (abs=0x{abs_off:x})")

    # Analyze entropy of first few DZ blocks
    print(f"\n=== Entropy analysis (first 10 DZ blocks) ===")
    dz_blocks = [ft for ft in file_table if ft['type'] == 4]
    for ft in dz_blocks[:10]:
        start = data_start + ft['data_offset']
        end = start + ft['comp_size']
        payload = data[start:end]
        ent = shannon_entropy(payload)
        print(f"  {ft['name']:40s} comp={ft['comp_size']:6d} uncomp={ft['uncomp_size']:6d} ent={ent:.3f} first8={payload[:8].hex()}")

    # Try to extract type=1 (copy) and type=2 (zlib) files
    print(f"\n=== Extracting non-DZ files ===")
    os.makedirs(out_dir, exist_ok=True)
    extracted = 0
    for ft in file_table:
        if ft['type'] == 1:  # copy
            start = data_start + ft['data_offset']
            payload = data[start:start + ft['comp_size']]
            out_path = os.path.join(out_dir, ft['name'].replace('\\', '/'))
            os.makedirs(os.path.dirname(out_path), exist_ok=True) if os.path.dirname(out_path) else None
            with open(out_path, 'wb') as f:
                f.write(payload)
            extracted += 1
            if extracted <= 5:
                print(f"  [copy] {ft['name']} ({len(payload)} bytes)")
        elif ft['type'] == 2:  # zlib
            start = data_start + ft['data_offset']
            payload = data[start:start + ft['comp_size']]
            try:
                content = zlib.decompress(payload)
                out_path = os.path.join(out_dir, ft['name'].replace('\\', '/'))
                os.makedirs(os.path.dirname(out_path), exist_ok=True) if os.path.dirname(out_path) else None
                with open(out_path, 'wb') as f:
                    f.write(content)
                extracted += 1
                if extracted <= 5:
                    print(f"  [zlib] {ft['name']} ({len(content)} bytes)")
            except Exception as e:
                print(f"  [zlib FAIL] {ft['name']}: {e}")

    print(f"\nExtracted {extracted} non-DZ files to {out_dir}")

    # For DZ files, save the compressed blocks for later analysis
    print(f"\n=== Saving DZ compressed blocks for analysis ===")
    dz_out = os.path.join(out_dir, "_dz_blocks")
    os.makedirs(dz_out, exist_ok=True)
    for i, ft in enumerate(dz_blocks[:20]):
        start = data_start + ft['data_offset']
        payload = data[start:start + ft['comp_size']]
        block_path = os.path.join(dz_out, f"{i:03d}_{ft['name'].replace('/', '_').replace(chr(92), '_')}.dzblock")
        with open(block_path, 'wb') as f:
            f.write(payload)
        # Also save metadata
        meta_path = block_path + ".meta"
        with open(meta_path, 'w') as f:
            f.write(f"name={ft['name']}\n")
            f.write(f"comp_size={ft['comp_size']}\n")
            f.write(f"uncomp_size={ft['uncomp_size']}\n")
            f.write(f"type={ft['type']}\n")
    print(f"  Saved {min(20, len(dz_blocks))} DZ blocks to {dz_out}")

    return 0


if __name__ == "__main__":
    sys.exit(main())

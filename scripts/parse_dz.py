#!/usr/bin/env python3
"""
DTRZ (.dz) archive parser for Marmalade SDK's derbh format.

Header layout (from RE notes in engine/reverse/dz/README.md):
  - 4 bytes: "DTRZ" magic
  - u16 LE: num_files
  - u16 LE: num_dirs
  - u8:     version
  - Names list: null-terminated UTF-8 strings (num_files + num_dirs of them)
  - Dir assignment table: num_dirs * 6 bytes (name_idx u16 + sentinel u16 + parent u16)
  - File table: num_files * 16 bytes (4 x u24 LE + u8 type)
      field 0: u24=0,          u8=CRC
      field 1: u24=offset,     u8=CRC
      field 2: u24=comp_size,  u8=CRC
      field 3: u24=uncomp_size, u8=type (1=copy, 2=zlib, 3=bzip, 4=DZ, 5=LZMA)
  - Data section: file payloads at the given offsets

This script extracts files whose type is 1 (copy, no compression) or
2 (zlib). Type 4 (DZ arithmetic coding) is not yet supported.
"""
import struct
import sys
import os
import zlib
from pathlib import Path


def parse_dz(path: str):
    with open(path, "rb") as f:
        data = f.read()

    if data[:4] != b"DTRZ":
        raise ValueError(f"Not a DTRZ file: magic={data[:4]!r}")

    # Parse header
    num_files = struct.unpack_from("<H", data, 4)[0]
    num_dirs  = struct.unpack_from("<H", data, 6)[0]
    version   = data[8]
    print(f"DTRZ: {num_files} files, {num_dirs} dirs, version={version}")

    # Parse names list
    pos = 9
    names = []
    total_names = num_files + num_dirs
    for _ in range(total_names):
        end = data.index(b"\x00", pos)
        names.append(data[pos:end].decode("utf-8", errors="replace"))
        pos = end + 1

    dir_names = names[:num_dirs]
    file_names = names[num_dirs:]
    print(f"  Parsed {len(dir_names)} dir names, {len(file_names)} file names")
    print(f"  First 5 dirs:  {dir_names[:5]}")
    print(f"  First 5 files: {file_names[:5]}")

    # Dir assignment table: num_dirs * 6 bytes
    # Each: name_idx u16, sentinel u16, parent u16
    dir_table = []
    for i in range(num_dirs):
        off = pos + i * 6
        name_idx, sentinel, parent = struct.unpack_from("<HHH", data, off)
        dir_table.append((name_idx, sentinel, parent))
    pos += num_dirs * 6

    # File table: num_files * 16 bytes
    # 4 fields of (u24 LE value, u8 type/crc)
    file_table = []
    for i in range(num_files):
        off = pos + i * 16
        # Read 4 u32 LE values, low 24 bits = value, high 8 bits = type/crc
        f0, f1, f2, f3 = struct.unpack_from("<IIII", data, off)
        val0 = f0 & 0xFFFFFF; type0 = (f0 >> 24) & 0xFF
        offset = f1 & 0xFFFFFF; crc1 = (f1 >> 24) & 0xFF
        comp_size = f2 & 0xFFFFFF; crc2 = (f2 >> 24) & 0xFF
        uncomp_size = f3 & 0xFFFFFF; comp_type = (f3 >> 24) & 0xFF
        file_table.append({
            "name": file_names[i],
            "offset": offset,
            "comp_size": comp_size,
            "uncomp_size": uncomp_size,
            "type": comp_type,
        })
    pos += num_files * 16

    # Data section starts at `pos`. But offsets in the table are relative to
    # the start of the file? Let's check by trying both.
    data_start = pos
    print(f"  Data section starts at file offset 0x{data_start:x}")

    # Count types
    type_counts = {}
    for ft in file_table:
        type_counts[ft["type"]] = type_counts.get(ft["type"], 0) + 1
    print(f"  Type counts: {type_counts}")

    return data, data_start, file_table, dir_names


def extract(data, data_start, file_table, out_dir, wanted_names=None):
    """Extract files. If wanted_names is None, extract all that we can decode."""
    os.makedirs(out_dir, exist_ok=True)

    type_names = {1: "copy", 2: "zlib", 3: "bzip", 4: "DZ", 5: "LZMA"}

    extracted = 0
    skipped = 0
    failed = 0
    for ft in file_table:
        name = ft["name"]
        if wanted_names and name not in wanted_names:
            continue

        # Try offset relative to file start first, then relative to data_start
        for base in (0, data_start):
            start = base + ft["offset"]
            end = start + ft["comp_size"]
            if end <= len(data):
                break
        else:
            print(f"  SKIP {name}: offset out of range")
            skipped += 1
            continue

        payload = data[start:end]
        t = ft["type"]
        try:
            if t == 1:  # copy
                content = payload
            elif t == 2:  # zlib
                content = zlib.decompress(payload)
            elif t == 4:  # DZ - unsupported
                print(f"  SKIP {name}: DZ compression (unsupported)")
                skipped += 1
                continue
            elif t == 5:  # LZMA
                print(f"  SKIP {name}: LZMA compression (unsupported)")
                skipped += 1
                continue
            else:
                print(f"  SKIP {name}: unknown type {t}")
                skipped += 1
                continue
        except Exception as e:
            print(f"  FAIL {name}: {e}")
            failed += 1
            continue

        # Verify size
        if len(content) != ft["uncomp_size"] and t != 1:
            print(f"  WARN {name}: size mismatch {len(content)} != {ft['uncomp_size']}")

        out_path = os.path.join(out_dir, name)
        os.makedirs(os.path.dirname(out_path), exist_ok=True) if os.path.dirname(name) else None
        with open(out_path, "wb") as f:
            f.write(content)
        extracted += 1
        if extracted <= 20:
            print(f"  OK   {name} ({len(content)} bytes, type={type_names.get(t, t)})")

    print(f"\nSummary: {extracted} extracted, {skipped} skipped, {failed} failed")
    return extracted


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <file.dz> <out_dir> [name1 name2 ...]")
        sys.exit(1)

    dz_path = sys.argv[1]
    out_dir = sys.argv[2]
    wanted = set(sys.argv[3:]) if len(sys.argv) > 3 else None

    data, data_start, file_table, _ = parse_dz(dz_path)
    extract(data, data_start, file_table, out_dir, wanted)

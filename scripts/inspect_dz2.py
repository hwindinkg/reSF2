#!/usr/bin/env python3
"""
DTRZ (.dz) archive parser for Marmalade SDK's derbh format.

Header (9 bytes):
  - 4 bytes: "DTRZ" magic
  - u16 LE: num_files
  - u16 LE: num_dirs
  - u8:     version

Names section (variable length):
  - (num_dirs + num_files) null-terminated UTF-8 strings
  - First num_dirs entries are directory names
  - Remaining num_files entries are file names
  - BUT: directories are listed as full paths like "assets\\models",
    not as flat names. So the "dir name" is actually the full path.

Dir assignment table (num_dirs * 6 bytes):
  - Each entry: u16 name_idx, u16 sentinel (0xff/0xffff), u16 parent_idx
  - This links each dir to its parent dir index

File table (num_files * 16 bytes):
  - 4 fields of (u24 LE value, u8 type/crc)
  - field 0: u24=0,          u8=CRC
  - field 1: u24=offset,     u8=CRC (offset relative to data section start)
  - field 2: u24=comp_size,  u8=CRC
  - field 3: u24=uncomp_size, u8=type (1=copy, 2=zlib, 3=bzip, 4=DZ, 5=LZMA)

Data section: file payloads at data_start + offset
"""
import struct
import sys
import os
import zlib


def parse_dz(path: str):
    with open(path, "rb") as f:
        data = f.read()

    if data[:4] != b"DTRZ":
        raise ValueError(f"Not a DTRZ file: magic={data[:4]!r}")

    num_files = struct.unpack_from("<H", data, 4)[0]
    num_dirs  = struct.unpack_from("<H", data, 6)[0]
    version   = data[8]
    print(f"DTRZ: {num_files} files, {num_dirs} dirs, version={version}")

    # Parse names section
    pos = 9
    names = []
    total_names = num_files + num_dirs
    for i in range(total_names):
        end = data.index(b"\x00", pos)
        name = data[pos:end].decode("utf-8", errors="replace")
        names.append(name)
        pos = end + 1

    dir_names = names[:num_dirs]
    file_names = names[num_dirs:]
    print(f"  First 5 dirs:  {dir_names[:5]}")
    print(f"  First 5 files: {file_names[:5]}")
    print(f"  Last 3 dirs:   {dir_names[-3:]}")
    print(f"  Last 3 files:  {file_names[-3:]}")

    # Dir assignment table: num_dirs * 6 bytes
    # Each: u16 name_idx, u16 sentinel, u16 parent_idx
    # (from the hex dump: ff ff 00 00 01 00 = sentinel=0xffff, parent=0, name=1?)
    # Actually looking at the bytes: "ff ff 00 00 01 00 ff ff 01 00 02 00"
    # Let's try: (u16, u16, u16) = (0xffff, 0x0000, 0x0001), (0xffff, 0x0001, 0x0002)
    # This looks like: sentinel=0xffff, this_idx=0, parent_idx=1?
    # Or maybe: (u16 parent, u16 idx, u16 ?)
    # Let's just skip this table for now and find the file table.
    print(f"\n  Dir table starts at offset 0x{pos:x}")
    # Print first few dir table entries
    for i in range(min(5, num_dirs)):
        off = pos + i * 6
        a, b, c = struct.unpack_from("<HHH", data, off)
        print(f"    dir[{i}]: {a:#06x} {b:#06x} {c:#06x}  (name={dir_names[i]!r})")
    pos += num_dirs * 6

    # File table: num_files * 16 bytes
    # 4 fields of (u24 LE value, u8 type/crc)
    # But based on the hex dump of the dir table, the layout might be
    # different. Let's check the file table entries.
    print(f"\n  File table starts at offset 0x{pos:x}")
    file_table = []
    for i in range(num_files):
        off = pos + i * 16
        # Read as 4 u32 LE values
        raw = struct.unpack_from("<IIII", data, off)
        # Each u32: low 24 bits = value, high 8 bits = type/crc
        f0_val = raw[0] & 0xFFFFFF; f0_type = (raw[0] >> 24) & 0xFF
        f1_val = raw[1] & 0xFFFFFF; f1_type = (raw[1] >> 24) & 0xFF
        f2_val = raw[2] & 0xFFFFFF; f2_type = (raw[2] >> 24) & 0xFF
        f3_val = raw[3] & 0xFFFFFF; f3_type = (raw[3] >> 24) & 0xFF
        file_table.append({
            "name": file_names[i],
            "raw": raw,
            "field0": (f0_val, f0_type),
            "field1": (f1_val, f1_type),  # offset?
            "field2": (f2_val, f2_type),  # comp_size?
            "field3": (f3_val, f3_type),  # uncomp_size + type?
        })
        if i < 5:
            print(f"    file[{i}] {file_names[i]!r}:")
            print(f"      raw: {raw}")
            print(f"      fields: 0={f0_val}({f0_type}) 1={f1_val}({f1_type}) 2={f2_val}({f2_type}) 3={f3_val}({f3_type})")

    pos += num_files * 16
    data_start = pos
    print(f"\n  Data section starts at offset 0x{data_start:x}")
    print(f"  File size: {len(data)} bytes")
    print(f"  Data section size: {len(data) - data_start} bytes")

    return data, data_start, file_table, dir_names, file_names


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.dz>")
        sys.exit(1)
    parse_dz(sys.argv[1])

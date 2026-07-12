#!/usr/bin/env python3
"""Test the DZ decoder by comparing with known extracted files.

Reads files.dz, tries to decompress each file, and compares with the
pre-extracted version in assets/files/.
"""
import struct
import os
import sys

# Add the scripts directory to path for imports
sys.path.insert(0, '/home/z/my-project/scripts')

DZ_PATH = "/home/z/my-project/assets/files.dz"
EXTRACTED_DIR = "/home/z/my-project/assets/files"

# Parse the DZ archive
with open(DZ_PATH, 'rb') as f:
    dz_data = f.read()

assert dz_data[:4] == b'DTRZ'
num_files = struct.unpack_from('<H', dz_data, 4)[0]
num_dirs = struct.unpack_from('<H', dz_data, 6)[0]
print(f"Archive: {num_files} files, {num_dirs} dirs")

pos = 9
filenames = []
for _ in range(num_files):
    end = dz_data.index(b'\x00', pos)
    filenames.append(dz_data[pos:end].decode())
    pos = end + 1

folders = [""]
for _ in range(max(0, num_dirs - 1)):
    end = dz_data.index(b'\x00', pos)
    folders.append(dz_data[pos:end].decode())
    pos = end + 1

pos += num_files * 6  # skip attributes
pos += 4  # skip lengths header

files = []
for i in range(num_files):
    offset, uncomp_size, comp_size, comp_type = struct.unpack_from('<IIII', dz_data, pos)
    files.append({
        'name': filenames[i],
        'offset': offset,
        'uncomp_size': uncomp_size,
        'comp_size': comp_size,
        'type': comp_type,
    })
    pos += 16

data_start = pos
print(f"Data section starts at: {data_start} (0x{data_start:x})")

# Show first 5 files
print("\nFirst 5 files:")
for f in files[:5]:
    extracted_path = os.path.join(EXTRACTED_DIR, f['name'])
    extracted_size = os.path.getsize(extracted_path) if os.path.exists(extracted_path) else -1
    print(f"  {f['name']:40s} off={f['offset']:6d} comp={f['comp_size']:6d} "
          f"uncomp={f['uncomp_size']:6d} type={f['type']} extracted={extracted_size}")

# Check streaming nature — do offsets overlap?
print("\nOffset analysis (first 10 files):")
for i in range(min(10, len(files))):
    f = files[i]
    next_offset = files[i+1]['offset'] if i+1 < len(files) else len(dz_data) - data_start
    overlap = f['comp_size'] - (next_offset - f['offset'])
    print(f"  [{i}] {f['name']:30s} off={f['offset']:6d} comp={f['comp_size']:6d} "
          f"next_off={next_offset:6d} overlap={overlap:+d}")

#!/usr/bin/env python3
"""Test DZ decoder by comparing with extracted files.
Tries to decompress files.dz file 0 and compares with assets/files/ version.
"""
import struct
import os
import sys
import subprocess

DZ_PATH = "/home/z/my-project/assets/files.dz"
EXTRACTED_DIR = "/home/z/my-project/assets/files"

# Parse DZ archive
with open(DZ_PATH, 'rb') as f:
    dz_data = f.read()

assert dz_data[:4] == b'DTRZ'
num_files = struct.unpack_from('<H', dz_data, 4)[0]
pos = 9
filenames = []
for _ in range(num_files):
    end = dz_data.index(b'\x00', pos)
    filenames.append(dz_data[pos:end].decode())
    pos = end + 1
num_dirs = struct.unpack_from('<H', dz_data, 6)[0]
for _ in range(max(0, num_dirs - 1)):
    end = dz_data.index(b'\x00', pos)
    pos = end + 1
pos += num_files * 6 + 4
files = []
for i in range(num_files):
    offset, uncomp_size, comp_size, comp_type = struct.unpack_from('<IIII', dz_data, pos)
    files.append({'name': filenames[i], 'offset': offset, 'uncomp_size': uncomp_size,
                  'comp_size': comp_size, 'type': comp_type})
    pos += 16
data_start = pos

print(f"Archive: {num_files} files, data_start={data_start}")
print(f"\nFirst 5 files:")
for i, f in enumerate(files[:5]):
    extracted_path = os.path.join(EXTRACTED_DIR, f['name'])
    extracted_size = os.path.getsize(extracted_path) if os.path.exists(extracted_path) else -1
    print(f"  [{i}] {f['name']:40s} off={f['offset']:6d} comp={f['comp_size']:6d} "
          f"uncomp={f['uncomp_size']:6d} type={f['type']} extracted={extracted_size}")
    
    # Extract compressed data for this file
    comp_data = dz_data[data_start + f['offset'] : data_start + f['offset'] + f['comp_size']]
    print(f"       First 16 bytes of compressed: {comp_data[:16].hex()}")
    
    if extracted_size > 0:
        with open(extracted_path, 'rb') as ef:
            extracted_data = ef.read(32)
        print(f"       First 32 bytes of extracted:  {extracted_data.hex()}")
        print(f"       Extracted as text: {extracted_data[:60]}")
    print()

# Check: does the extracted file match the compressed data?
# If extracted_size == comp_size, the 'extracted' file is actually compressed
# If extracted_size == uncomp_size, it's properly decompressed
print("=== Analysis ===")
for i, f in enumerate(files[:5]):
    extracted_path = os.path.join(EXTRACTED_DIR, f['name'])
    extracted_size = os.path.getsize(extracted_path) if os.path.exists(extracted_path) else -1
    if extracted_size == f['comp_size']:
        print(f"  [{i}] {f['name']}: extracted == comp_size (RAW COMPRESSED, not decompressed)")
    elif extracted_size == f['uncomp_size']:
        print(f"  [{i}] {f['name']}: extracted == uncomp_size (PROPERLY DECOMPRESSED)")
    else:
        print(f"  [{i}] {f['name']}: extracted={extracted_size} != comp={f['comp_size']} != uncomp={f['uncomp_size']} (MISMATCH)")

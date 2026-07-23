#!/usr/bin/env python3
"""Debug DZ stream: dump header, try simple range decode, analyze structure."""

import struct
import sys

DZ_PATH = "assets/files.dz"

with open(DZ_PATH, "rb") as f:
    dz = f.read()

# Parse archive
nf = struct.unpack_from('<H', dz, 4)[0]
nd = struct.unpack_from('<H', dz, 6)[0]
pos = 9
for _ in range(nf):
    while pos < len(dz) and dz[pos] != 0: pos += 1
    pos += 1
for _ in range(nd):
    while pos < len(dz) and dz[pos] != 0: pos += 1
    pos += 1
pos += nf * 6 + 4

# File table
files = []
for i in range(nf):
    f0 = struct.unpack_from('<I', dz, pos)[0]
    f1 = struct.unpack_from('<I', dz, pos+4)[0]
    f2 = struct.unpack_from('<I', dz, pos+8)[0]
    f3 = struct.unpack_from('<I', dz, pos+12)[0]
    off = f1 & 0x00FFFFFF
    cmp = f2 & 0x00FFFFFF
    unc = f0 & 0x00FFFFFF
    typ = (f2 >> 24) & 0xFF
    files.append((off, cmp, unc, typ))
    pos += 16

data_start = pos
print(f"Data section at offset {data_start} (0x{data_start:x})")
print(f"Data section size: {len(dz) - data_start} bytes\n")

# Show all files with their offsets
print(f"{'#':>4} {'name':30s} {'off':>6} {'cmp':>6} {'unc':>6} {'type':>4}")
print("-"*65)
for i, (off, cmp, unc, typ) in enumerate(files[:30]):
    name_end = 9 + dz[9:].index(b'\x00' if i == 0 else b'\x00')
    # Just use index
    print(f"{i:4d} {'file_'+str(i):30s} {off:6d} {cmp:6d} {unc:6d} {typ:4d}")

print("\n--- Data section header dump ---")
ds = dz[data_start:]
print(f"Bytes 0-31 hex: {' '.join(f'{b:02x}' for b in ds[:32])}")
print(f"Bytes 0-7 as values:")
print(f"  [0] = 0x{ds[0]:02x} (props?)")
print(f"  [1] = 0x{ds[1]:02x} (code byte 3?)")
print(f"  [2] = 0x{ds[2]:02x} (code byte 2?)")
print(f"  [3] = 0x{ds[3]:02x} (code byte 1?)")
print(f"  [4] = 0x{ds[4]:02x} (code byte 0?)")
print(f"  code BE = 0x{ds[1]:02x}{ds[2]:02x}{ds[3]:02x}{ds[4]:02x}")
print(f"  code LE = 0x{struct.unpack_from('<I', ds, 1)[0]:08x}")
print(f"  first 5 = window = {ds[:5].hex()}")

# Try interpreting as different algo types
# Check if first byte 0x08 = 8 = LZMA properties
# LZMA: props byte = lc+lp+pb
lc = ds[0] & 0x0F  # literal context bits
pb = (ds[0] >> 4) & 0x0F  # pos bits
lp = (ds[0] >> 6) & 0x03  # literal pos bits
print(f"\n  If LZMA props: lc={lc}, pb={pb}, lp={lp} (unlikely for 0x{ds[0]:02x})")

# Byte frequency analysis of first 256 bytes
print(f"\n--- Byte frequency of first 256 data bytes ---")
freq = {}
for i in range(min(256, len(ds))):
    b = ds[i]
    freq[b] = freq.get(b, 0) + 1
top = sorted(freq.items(), key=lambda x: -x[1])[:10]
print(f"  Top bytes: {[(f'0x{b:02x}',c) for b,c in top]}")

# Try a very simple range decode
print(f"\n--- Simple range decode test ---")

def simple_range_decode(data, max_out=100):
    """Simplest possible range decoder: uniform probs."""
    if len(data) < 5:
        return b""
    
    range_val = 0xFFFFFFFF
    code = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4]
    pos = 5
    out = []
    
    while len(out) < max_out and pos < len(data):
        # normalize
        while range_val < 0x1000000 and pos < len(data):
            range_val <<= 8
            code = (code << 8) | data[pos]
            pos += 1
        
        # decode one byte by feeding 8 bits
        byte = 0
        for _ in range(8):
            while range_val < 0x1000000 and pos < len(data):
                range_val <<= 8
                code = (code << 8) | data[pos]
                pos += 1
            
            # uniform prob = 0x400 (1024/2048 = 50%)
            prob = 0x400
            bound = (range_val >> 11) * prob
            
            if code < bound:
                range_val = bound
                bit = 0
                prob += (0x800 - prob) >> 5
            else:
                code -= bound
                range_val -= bound
                bit = 1
                prob -= prob >> 5
            
            byte = (byte << 1) | bit
        
        out.append(byte)
    
    return bytes(out)

result = simple_range_decode(ds, 64)
print(f"  Simple uniform decode: {len(result)} bytes")
if result:
    print(f"  Hex: {' '.join(f'{b:02x}' for b in result)}")
    print(f"  Text: {''.join(chr(b) if 32 <= b < 127 else '.' for b in result)}")

# Try with different prob initialization
print(f"\n--- Try: first byte = tree decode init count ---")
# What if ds[0] is not lzma props but just an initial count value?
# e.g., 0x08 = 8 initial bits to decode
init_bits = ds[0]
print(f"  If first byte = init_bits = {init_bits}")

# Try: what if the header is just 5 bytes of initial window,
# and the code is BIG ENDIAN?

def try_decode_window(data, code_bytes, init_code, max_out=100):
    """Try decode with explicit window/code setup."""
    range_val = 0xFFFFFFFF
    code = 0
    pos = code_bytes  # start after window
    for i in range(4):
        if pos < len(data):
            code = (code << 8) | data[pos]
            pos += 1
        else:
            code <<= 8
    out = []
    
    while len(out) < max_out and pos < len(data):
        while range_val < 0x1000000 and pos < len(data):
            range_val <<= 8
            code = (code << 8) | data[pos]
            pos += 1
        
        byte = 0
        for _ in range(8):
            while range_val < 0x1000000 and pos < len(data):
                range_val <<= 8
                code = (code << 8) | data[pos]
                pos += 1
            
            prob = 0x400
            bound = (range_val >> 11) * prob
            
            if code < bound:
                range_val = bound
                bit = 0
            else:
                code -= bound
                range_val -= bound
                bit = 1
            byte = (byte << 1) | bit
        out.append(byte)
    return bytes(out)

print("\n--- Try code from bytes 0-3 (BE) ---")
r = try_decode_window(ds, 4, 0, 32)
print(f"  Result: {' '.join(f'{b:02x}' for b in r)}")

print("\n--- Try code from bytes 0-3 (LE u32) ---")
code_le = struct.unpack_from('<I', ds, 0)[0]
print(f"  code = 0x{code_le:08x}")
range_val = 0xFFFFFFFF
code = code_le
pos = 4
out = []
# Read LE-style: code already in LE u32 format
while len(out) < 32 and pos < len(ds):
    while range_val < 0x1000000 and pos < len(ds):
        range_val <<= 8
        code = (code << 8) | ds[pos]
        pos += 1
    byte = 0
    for _ in range(8):
        while range_val < 0x1000000 and pos < len(ds):
            range_val <<= 8
            code = (code << 8) | ds[pos]
            pos += 1
        prob = 0x400
        bound = (range_val >> 11) * prob
        if code < bound:
            range_val = bound
        else:
            code -= bound
            range_val -= bound
            byte |= 1
        byte <<= 1
    byte >>= 1
    out.append(byte)
print(f"  Result: {' '.join(f'{b:02x}' for b in bytes(out))}")

# File 0 analysis: off=3, comp=23, unc=25
f0_off, f0_cmp, f0_unc, f0_typ = files[0]
print(f"\n--- File 0 specific analysis ---")
print(f"  offset={f0_off}, comp={f0_cmp}, uncomp={f0_unc}, type={f0_typ}")

# The reading code treats 'off' as output position, not input position
# So file 0 starts at output offset 3, is 25 bytes long
# To decompress file 0, we decompress the stream from byte 0 
# and get output[3..27]
print(f"\n  If off=3 is output offset:")
print(f"  We need to decompress at least {f0_off + f0_unc} = {f0_off + f0_unc} bytes of output")
print(f"  File 0 data = output[{f0_off}..{f0_off + f0_unc - 1}]")

# But the compressed size is 23. What does this mean?
# If off=3 and comp=23:
#  - The compressed data for file 0 starts at data_section + 3
#  - There are 23 compressed bytes
#  - These decompress to 25 bytes
# But then the streaming nature: files share compressed data

# Check what 23 bytes at +3 look like
file0_comp = ds[f0_off:f0_off + f0_cmp]
print(f"  File 0 raw ({len(file0_comp)} bytes): {' '.join(f'{b:02x}' for b in file0_comp)}")

# Does the full stream decompressed as one give file 0 at offset 3?
# Let's try: decompress the FULL data section, look at output[3..27]

# Try treating comp_size as blocks of 256 bytes
print(f"\n  If comp_size is in 256-byte blocks:")
print(f"  Actual comp = {f0_cmp} * 256 = {f0_cmp * 256}")
print(f"  Or comp_size itself is the byte count = {f0_cmp}")

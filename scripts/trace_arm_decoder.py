#!/usr/bin/env python3
"""
Trace the ARM DZ decoder (libs3e_android.so) on OUR assets/files.dz.
Hooks input buffer reads and traces every byte consumed.
"""
import struct, sys, os
from unicorn import *
from unicorn.arm_const import *

REPO = "E:/reSF2"
SO_PATH = f"{REPO}/reverse/binaries/libs3e_android.so"
DZ_PATH = f"{REPO}/assets/files.dz"

# Key addresses (from prior analysis)
ADDR_MALLOC  = 0x06e770
ADDR_FREE    = 0x06e5f8
ADDR_REALLOC = 0x06ea68

# The s3eCompressionDecomp wrapper (type-4 manager that calls the decoder)
# Let's directly call the low-level decoder if we can find it.
# From dz_decode_final.py: CODER_INIT=0x50be4, CODER_READ=0x51f60
# But these might be wrong. Let's use the trace approach.

# Read the ARM .so
with open(SO_PATH, "rb") as f:
    SO = f.read()

# Parse ELF for segments
def parse_elf():
    e_phoff = struct.unpack_from("<I", SO, 0x1c)[0]
    e_phentsize = struct.unpack_from("<H", SO, 0x2a)[0]
    e_phnum = struct.unpack_from("<H", SO, 0x2c)[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p = struct.unpack_from("<IIIIIIII", SO, off)
        if p[0] == 1:  # PT_LOAD
            segs.append((p[2], p[1], p[4], p[5]))
    e_shoff = struct.unpack_from("<I", SO, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", SO, 0x2e)[0]
    e_shnum = struct.unpack_from("<H", SO, 0x30)[0]
    dynsym_off = None
    for i in range(e_shnum):
        s = struct.unpack_from("<IIIIIIIIII", SO, e_shoff + i*e_shentsize)
        if s[1] == 11:  # SHT_DYNSYM
            dynsym = s
            dynstr = struct.unpack_from("<IIIIIIIIII", SO, e_shoff + dynsym[6]*e_shentsize)
            break
    syms = {}
    entsize = dynsym[9] or 16
    dynstr_data = SO[dynstr[4]:dynstr[4]+dynstr[5]]
    for i in range(dynsym[5]//entsize):
        off = dynsym[4] + i*entsize
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", SO, off)
        end = dynstr_data.find(b'\x00', st_name)
        nm = dynstr_data[st_name:end].decode('latin-1')
        syms[nm] = st_value
    return segs, syms

segs, syms = parse_elf()

print("=== libs3e_android.so symbols ===")
for name in sorted(syms.keys()):
    if 'compression' in name.lower() or 'decomp' in name.lower() or 'derbh' in name.lower() or 'dz' in name.lower():
        print(f"  {name}: 0x{syms[name]:x}")

# Try to find decoder functions
for prefix in ['s3eCompressionDecompRead', 's3eCompressionDecomp', 's3eCompressionDecompInit']:
    if prefix in syms:
        print(f"\n{prefix} @ 0x{syms[prefix]:x}")

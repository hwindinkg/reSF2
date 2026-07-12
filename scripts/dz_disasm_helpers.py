#!/usr/bin/env python3
"""Annotated disassembly of the DZ decode function at 0x389f8.

We trace through the function and add comments explaining what each
block does, so we can port the algorithm to C++.
"""
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

SO_PATH = "/home/z/my-project/work/sf2_data/sf2/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
md.detail = True

# Also disassemble the helper functions called from 0x389f8:
# - 0x3751c (called from 0x38d28)
# - 0x37adc (called from 0x38d60 and 0x38eec)
# - 0xc94c (called from 0x38fb8)

def disasm_func(addr, max_size, label):
    print(f"\n{'=' * 80}")
    print(f"{label} at 0x{addr:x}")
    print('=' * 80)
    code = so_data[addr:addr + max_size]
    for ins in md.disasm(code, addr):
        print(f"  0x{ins.address:08x}: {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}")
        if ins.mnemonic == 'pop' and 'pc' in ins.op_str.lower():
            break

# Helper at 0x3751c — this is the bit/model decode function
disasm_func(0x3751c, 0x400, "Helper 0x3751c (bit/model decode)")

# Helper at 0x37adc — range coder decode
disasm_func(0x37adc, 0x400, "Helper 0x37adc (range coder)")

# Helper at 0xc94c — memcpy or similar
disasm_func(0xc94c, 0x200, "Helper 0xc94c (memcpy?)")

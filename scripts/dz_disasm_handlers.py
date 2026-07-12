#!/usr/bin/env python3
"""Disassemble the DZ read handler at 0x51f60 and the coder init at 0x50be4."""
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

SO_PATH = "/home/z/my-project/work/sf2_data/sf2/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

md = Cs(CS_ARCH_ARM, CS_MODE_ARM)

def disasm(addr, size, label):
    print("=" * 80)
    print(f"{label} at 0x{addr:x}")
    print("=" * 80)
    code = so_data[addr:addr + size]
    for ins in md.disasm(code, addr):
        print(f"  0x{ins.address:08x}: {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}")
        if ins.mnemonic == 'pop' and 'pc' in ins.op_str.lower():
            break

# DZ read handler
disasm(0x51f60, 0x800, "DZ read handler")

# Coder init
print()
disasm(0x50be4, 0x400, "DZ coder init")

# s3eCompressionDecomp
print()
disasm(0x51c1c, 0x400, "s3eCompressionDecomp")

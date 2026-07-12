#!/usr/bin/env python3
"""Disassemble the DZ decoder function at 0x389f8 in libs3e_android.so.

Goal: understand the algorithm precisely so we can port it to C++.
We'll dump the entire function with full disassembly, then identify:
  - Range coder initialization
  - Context model (5-byte window + CRC32 hash)
  - Literal decoding
  - Match decoding (offset + length)
  - Reference tables (RefOffsetTables, RefLengthTables)
"""
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

SO_PATH = "/home/z/my-project/work/sf2_data/sf2/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
md.detail = True

# Disassemble the DZ decode function at 0x389f8
# We don't know its length, so we'll disassemble a large chunk and stop
# when we hit a POP {.., PC} (function epilogue)
print("=" * 80)
print("DZ decode function at 0x389f8")
print("=" * 80)

# Try to find the function size by looking for the next function or epilogue
# Disassemble up to 16KB (should be enough for any reasonable function)
code = so_data[0x389f8:0x389f8 + 0x4000]
ins_count = 0
last_addr = 0x389f8
for ins in md.disasm(code, 0x389f8):
    print(f"  0x{ins.address:08x}: {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}")
    last_addr = ins.address
    ins_count += 1
    # Stop at POP with PC (function return)
    if ins.mnemonic == 'pop' and 'pc' in ins.op_str.lower():
        # Print a few more instructions in case there's a jump table
        break
    if ins_count > 2000:
        print("  ... (stopping after 2000 instructions)")
        break

print(f"\nTotal instructions: {ins_count}")
print(f"Last address: 0x{last_addr:x}")

#!/usr/bin/env python3
"""Examine the compression coder function pointer table at 0xc3000."""
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

SO_PATH = "/home/z/my-project/work/sf2_data/sf2/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

# The function pointer table is documented at 0xc3000
# But the README says the table entries are at 0xc8514 + i * 0x88
# Let's check both locations

print("=== Function pointer table at 0xc3000 ===")
for i in range(8):
    addr = 0xc3000 + i * 4
    val = struct.unpack_from("<I", so_data, addr)[0]
    print(f"  [0x{addr:x}] = 0x{val:08x}")

print("\n=== Coder table at 0xc8514 (4 slots, 0x88 bytes each) ===")
for slot in range(4):
    base = 0xc8514 + slot * 0x88
    print(f"\n  Slot {slot} (base 0x{base:x}):")
    for offset in [0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x58, 0x5c, 0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c, 0x80, 0x84]:
        addr = base + offset
        val = struct.unpack_from("<I", so_data, addr)[0]
        print(f"    +0x{offset:02x} [0x{addr:x}] = 0x{val:08x}")

# Also check what s3eCompressionDecompInit does at 0x51414
print("\n=== s3eCompressionDecompInit at 0x51414 ===")
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
code = so_data[0x51414:0x51414 + 0x200]
for ins in md.disasm(code, 0x51414):
    print(f"  0x{ins.address:08x}: {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}")
    if ins.mnemonic == 'pop' and 'pc' in ins.op_str.lower():
        break

# Check the coder init at 0x51250
print("\n=== Coder setup at 0x51250 ===")
code = so_data[0x51250:0x51250 + 0x400]
for ins in md.disasm(code, 0x51250):
    print(f"  0x{ins.address:08x}: {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}")
    if ins.mnemonic == 'pop' and 'pc' in ins.op_str.lower():
        break

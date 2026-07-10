#!/usr/bin/env python3
"""
Emulate the DZ decompression from libs3e_android.so using Unicorn ARM.

Strategy:
1. Load libs3e_android.so into Unicorn ARM emulator
2. Set up memory for context struct + input/output buffers
3. Call s3eCompressionDecompInit(ctx, type=4) to init DZ mode
4. Feed compressed data via s3eCompressionDecompRead
5. Collect decompressed output

Key addresses (from nm -D):
  s3eCompressionDecompInit  = 0x00051414
  s3eCompressionDecompRead  = 0x00051a10
  s3eCompressionDecomp      = 0x00051c1c
  s3eCompressionDecompFinal = 0x00051250
"""
import struct, sys, os
from unicorn import *
from unicorn.arm_const import *

# Load the .so
SO_PATH = "/home/z/my-project/work/apk_extracted/apktool/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

# Parse ELF
import struct as st
e_phoff = st.unpack_from("<I", so_data, 0x1c)[0]
e_phentsize = st.unpack_from("<H", so_data, 0x2a)[0]
e_phnum = st.unpack_from("<H", so_data, 0x2c)[0]

# The .so's loadable segments
BASE_ADDR = 0x00000000  # load at 0 (PIE library)
segments = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
        st.unpack_from("<IIIIIIII", so_data, off)
    if p_type == 1:  # PT_LOAD
        segments.append((p_vaddr, p_offset, p_filesz, p_memsz))
        
print(f"PT_LOAD segments: {len(segments)}")
for vaddr, offset, filesz, memsz in segments:
    print(f"  vaddr=0x{vaddr:08x} offset=0x{offset:x} filesz=0x{filesz:x} memsz=0x{memsz:x}")

# Create Unicorn ARM emulator
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)

# Map memory for the .so (map a large region covering all segments)
max_vaddr = max(v + m for v, _, _, m in segments)
so_map_size = (max_vaddr + 0xFFF) & ~0xFFF
mu.mem_map(BASE_ADDR, so_map_size)
print(f"\nMapped .so at 0x{BASE_ADDR:x}, size 0x{so_map_size:x}")

# Load segments
for vaddr, offset, filesz, memsz in segments:
    data = so_data[offset:offset + filesz]
    mu.mem_write(vaddr, data)
    print(f"  Loaded segment at 0x{vaddr:x} ({filesz} bytes)")

# Map stack
STACK_BASE = 0x80000000
STACK_SIZE = 0x100000
mu.mem_map(STACK_BASE, STACK_SIZE)
sp = STACK_BASE + STACK_SIZE - 0x100

# Map heap for context struct + buffers
HEAP_BASE = 0x90000000
HEAP_SIZE = 0x100000
mu.mem_map(HEAP_BASE, HEAP_SIZE)
heap_ptr = HEAP_BASE

def heap_alloc(size):
    global heap_ptr
    ptr = heap_ptr
    heap_ptr += (size + 0xF) & ~0xF
    return ptr

# Read the compressed data for file 0 from files.dz
DZ_PATH = "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"
with open(DZ_PATH, "rb") as f:
    dz_data = f.read()

# File table at 0x141a, entry 0:
# offset=4971, comp_size=139, uncomp_size=760
# Data section starts at 0x1b9a (after 81 x 16-byte entries at 0x141a)
# Wait — we need to find where the data ACTUALLY starts
# 
# Actually, let me try: the data for file 0 starts at some base + 4971
# Let me try multiple bases

# For now, let me just try to call s3eCompressionDecompInit and see if it works
# The Init function signature (from ARM calling convention):
#   r0 = type (0-4, we want 4 for DZ)
#   r1 = pointer to init params (struct with input/output info)
#   returns: pointer to context, or NULL on failure

# But actually, looking at the disassembly more carefully:
# s3eCompressionDecompInit @ 0x51414:
#   cmp r0, #4          ; check type <= 4
#   push {r4-r11, lr}
#   mov r6, r0          ; r6 = type
#   ...
#   ldr ip, [pc, #0x590]
#   add ip, pc, ip      ; ip = global data table
#   ldr r3, [ip, #0x244] ; r3 = some counter
#   ...
#   ldrb r0, [r0, #0x20] ; check if slot is available
#   ...
# This function manages a global table of 5 compression slots (one per type).
# It initializes the slot for the given type.

# Let me just try calling it with type=4
print("\n=== Calling s3eCompressionDecompInit(type=4) ===")

# Set up registers
mu.reg_write(UC_ARM_REG_SP, sp)
mu.reg_write(UC_ARM_REG_LR, 0xDEAD0000)  # return address (will stop here)

# Map the return address
mu.mem_map(0xDEAD0000 & ~0xFFF, 0x1000)
mu.mem_write(0xDEAD0000, b'\xfe\xde\xff\xe7')  # b #0xDEAD0000 (infinite loop)

try:
    mu.emu_start(0x51414, 0xDEAD0000, timeout=10*1000000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"  Returned: r0 = 0x{r0:x}")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"  Error: {e} at PC=0x{pc:x}")
    # Check what went wrong
    r0 = mu.reg_read(UC_ARM_REG_R0)
    r1 = mu.reg_read(UC_ARM_REG_R1)
    r2 = mu.reg_read(UC_ARM_REG_R2)
    r3 = mu.reg_read(UC_ARM_REG_R3)
    print(f"  r0=0x{r0:x} r1=0x{r1:x} r2=0x{r2:x} r3=0x{r3:x}")

# The function uses PC-relative loads to access a global table.
# Let me check if the table is properly loaded.
# The instruction at 0x51430: ldr ip, [pc, #0x590]
# PC = 0x51430 + 8 = 0x51438 (ARM pipeline)
# So ip = [0x51438 + 0x590] = [0x519c8]
print(f"\n  Checking global table at 0x519c8...")
try:
    val = struct.unpack_from("<I", so_data, 0x519c8)[0]
    print(f"  [0x519c8] = 0x{val:x}")
    # This is a PC-relative offset. The actual table is at:
    # 0x51438 + val (where 0x51438 is the PC when the ldr executed)
    # Wait, the add ip, pc, ip instruction at 0x51434:
    #   ip = ip + (PC + 8) = val + 0x51434 + 8 = val + 0x5143c
    table_addr = val + 0x5143c
    print(f"  Table at: 0x{table_addr:x}")
    # Read the table entry
    table_val = struct.unpack_from("<I", so_data, table_addr)[0] if table_addr < len(so_data) else 0
    print(f"  [0x{table_addr:x}] = 0x{table_val:x}")
except:
    print("  Could not read table")

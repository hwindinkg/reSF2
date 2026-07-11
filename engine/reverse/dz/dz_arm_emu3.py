#!/usr/bin/env python3
"""DZ decompression via ARM emulation — with better memory + crash debugging."""
import struct
from unicorn import *
from unicorn.arm_const import *

SO_PATH = "/home/z/my-project/work/apk_extracted/apktool/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

e_phoff = struct.unpack_from("<I", so_data, 0x1c)[0]
e_phentsize = struct.unpack_from("<H", so_data, 0x2a)[0]
e_phnum = struct.unpack_from("<H", so_data, 0x2c)[0]
segments = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
        struct.unpack_from("<IIIIIIII", so_data, off)
    if p_type == 1:
        segments.append((p_vaddr, p_offset, p_filesz, p_memsz))

mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)

# Map a LARGE region (128 MB) to avoid unmapped memory issues
mu.mem_map(0, 0x8000000)  # 128 MB at 0x0

for vaddr, offset, filesz, memsz in segments:
    data = so_data[offset:offset + filesz]
    mu.mem_write(vaddr, data)

# Stack
mu.mem_map(0x80000000, 0x100000)
sp = 0x80000000 + 0x100000 - 0x1000

# Heap
mu.mem_map(0x90000000, 0x4000000)  # 64 MB heap
heap_ptr = 0x90000000
def heap_alloc(size):
    global heap_ptr
    ptr = heap_ptr
    heap_ptr += (size + 0xF) & ~0xF
    return ptr

# Hook to catch unmapped memory accesses
def hook_mem_invalid(uc, access, address, size, value, user_data):
    access_type = {1: "READ", 2: "WRITE", 3: "FETCH"}.get(access, "UNKNOWN")
    pc = uc.reg_read(UC_ARM_REG_PC)
    print(f"  MEM FAULT: {access_type} at 0x{address:x}, size={size}, PC=0x{pc:x}")
    
    # If it's a write to uninitialized memory, map it and continue
    if access == 2:  # WRITE
        page = address & ~0xFFF
        try:
            uc.mem_map(page, 0x1000)
            print(f"    -> Mapped 0x{page:x}-0x{page+0x1000:x}, continuing")
            return True  # retry
        except:
            pass
    elif access == 1:  # READ
        page = address & ~0xFFF
        try:
            uc.mem_map(page, 0x1000)
            print(f"    -> Mapped 0x{page:x} for read, continuing")
            return True
        except:
            pass
    return False

mu.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_READ_UNMAPPED, hook_mem_invalid)

# Return address
RET_ADDR = 0xDEAD0000
mu.mem_map(0xDEAD0000 & ~0xFFF, 0x1000)
mu.mem_write(RET_ADDR, b'\xfe\xde\xff\xe7')

# Read compressed data
DZ_PATH = "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"
with open(DZ_PATH, "rb") as f:
    dz_data = f.read()

# File 0: offset=4971, comp_size=139, uncomp_size=760
# Try data section base = 0x1b9a (after 81 x 16-byte entries)
comp_offset = 0x1b9a + 4971  # = 0x2f05
comp_data = dz_data[comp_offset:comp_offset + 139]

print(f"Compressed data: {len(comp_data)} bytes at 0x{comp_offset:x}")
print(f"  First 16: {comp_data[:16].hex()}")

# Set up buffers
INPUT_BUF = heap_alloc(0x10000)
mu.mem_write(INPUT_BUF, comp_data)
OUTPUT_BUF = heap_alloc(0x100000)
INPUT_SIZE_ADDR = heap_alloc(16)
OUTPUT_SIZE_ADDR = heap_alloc(16)
mu.mem_write(INPUT_SIZE_ADDR, struct.pack("<I", 139))
mu.mem_write(OUTPUT_SIZE_ADDR, struct.pack("<I", 760))

# Push type=4 onto stack
sp_call = sp - 4
mu.mem_write(sp_call, struct.pack("<I", 4))

mu.reg_write(UC_ARM_REG_SP, sp_call)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, INPUT_BUF)
mu.reg_write(UC_ARM_REG_R1, OUTPUT_BUF)
mu.reg_write(UC_ARM_REG_R2, INPUT_SIZE_ADDR)
mu.reg_write(UC_ARM_REG_R3, OUTPUT_SIZE_ADDR)

print(f"\nCalling s3eCompressionDecomp(0x{INPUT_BUF:x}, 0x{OUTPUT_BUF:x}, 0x{INPUT_SIZE_ADDR:x}, 0x{OUTPUT_SIZE_ADDR:x}, type=4)")

try:
    mu.emu_start(0x51c1c, RET_ADDR, timeout=60*1000000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"\nReturned: r0 = 0x{r0:x}")
    
    # Check output
    out_data = bytes(mu.mem_read(OUTPUT_BUF, 760))
    nonzero = sum(1 for b in out_data if b != 0)
    print(f"Output: {nonzero}/760 non-zero bytes")
    if nonzero > 0:
        print(f"  First 200: {out_data[:200]!r}")
        with open("/home/z/my-project/work/dz_extracted/file0_test.bin", "wb") as f:
            f.write(out_data)
        print(f"  Saved to dz_extracted/file0_test.bin")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"\nError: {e} at PC=0x{pc:x}")
    # Print some registers
    for i in range(13):
        print(f"  r{i} = 0x{mu.reg_read(UC_ARM_REG_R0 + i):x}")
    print(f"  sp  = 0x{mu.reg_read(UC_ARM_REG_SP):x}")
    print(f"  lr  = 0x{mu.reg_read(UC_ARM_REG_LR):x}")

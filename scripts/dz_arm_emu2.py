#!/usr/bin/env python3
"""
Emulate DZ decompression via Unicorn ARM.
Calls s3eCompressionDecomp directly with proper setup.
"""
import struct
from unicorn import *
from unicorn.arm_const import *

SO_PATH = "/home/z/my-project/work/apk_extracted/apktool/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

# Parse ELF
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

# Map all memory (including BSS)
max_vaddr = max(v + m for v, _, _, m in segments)
so_map_size = (max_vaddr + 0xFFFF) & ~0xFFFF
mu.mem_map(0, so_map_size)

for vaddr, offset, filesz, memsz in segments:
    data = so_data[offset:offset + filesz]
    mu.mem_write(vaddr, data)

# Stack
STACK_BASE = 0x80000000
STACK_SIZE = 0x100000
mu.mem_map(STACK_BASE, STACK_SIZE)
sp = STACK_BASE + STACK_SIZE - 0x1000

# Heap (for malloc)
HEAP_BASE = 0x90000000
HEAP_SIZE = 0x2000000  # 32 MB
mu.mem_map(HEAP_BASE, HEAP_SIZE)
heap_ptr = HEAP_BASE

def heap_alloc(size):
    global heap_ptr
    ptr = heap_ptr
    heap_ptr += (size + 0xF) & ~0xF
    return ptr

# Hook to intercept function calls (BL instructions)
# The .so uses PLT entries for external functions. Let me find the PLT.
# Actually, Marmalade's loader resolves imports at load time. Since we're
# loading the .so directly, we need to provide the imports ourselves.
# 
# The key import is s3eMallocBase (for memory allocation).
# But wait — s3eMallocBase is EXPORTED by this .so (it's the loader).
# So internal calls to malloc go through internal function pointers.
#
# Let me hook ALL BL instructions and log which functions are called.

call_log = []
def hook_code(uc, address, size, user_data):
    # Read the instruction
    code = uc.mem_read(address, 4)
    insn = struct.unpack("<I", code)[0]
    
    # Check if it's a BL instruction: 0xEBxxxxxx
    if (insn & 0xFF000000) == 0xEB000000:
        # Extract offset (24-bit signed, shifted left 2)
        offset = insn & 0x00FFFFFF
        if offset & 0x800000:
            offset -= 0x1000000
        target = address + 8 + (offset << 2)
        if target < 0x100000:  # only log calls within the .so
            call_log.append((address, target))
            if len(call_log) <= 100:
                pass  # don't print during execution for speed

mu.hook_add(UC_HOOK_CODE, hook_code)

# Return address (stop point)
RET_ADDR = 0xDEAD0000
mu.mem_map(0xDEAD0000 & ~0xFFF, 0x1000)
mu.mem_write(RET_ADDR, b'\xfe\xde\xff\xe7')  # b . (infinite loop)

# Read compressed data for file 0 from files.dz
DZ_PATH = "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"
with open(DZ_PATH, "rb") as f:
    dz_data = f.read()

# File 0: offset=4971, comp_size=139, uncomp_size=760
# We need to find the correct data section base.
# The data section might start at different offsets.
# Let me try: data starts right after the file table.
# File table: 81 entries x 16 bytes at 0x141a = ends at 0x1b9a
# But we also need to account for the 39 remaining entries.
# 
# For now, let me try base = 0x1b9a + 39*16 = 0x1b9a + 0x270 = 0x1e0a
# (assuming remaining entries are also 16 bytes)
# OR base = 0x1b9a (only 81 entries in file table)

# Actually, let me try ALL possible bases and see which one gives valid DZ data
# The first byte of DZ-compressed data should be recognizable

# For now, let me just try calling the function with file 0's data at various offsets
data_section_bases = [0x1b9a, 0x1e0a, 0x2590]  # try multiple

for base in data_section_bases:
    comp_offset = base + 4971
    if comp_offset + 139 > len(dz_data):
        continue
    
    comp_data = dz_data[comp_offset:comp_offset + 139]
    print(f"\n=== Trying base=0x{base:x}, comp_data at 0x{comp_offset:x} ===")
    print(f"  First 16 bytes: {comp_data[:16].hex()}")
    
    # Set up input buffer
    INPUT_BUF = heap_alloc(0x10000)
    mu.mem_write(INPUT_BUF, comp_data)
    
    # Set up output buffer
    OUTPUT_BUF = heap_alloc(0x100000)  # 1 MB
    
    # Set up input_size and output_size variables
    INPUT_SIZE_ADDR = heap_alloc(16)
    OUTPUT_SIZE_ADDR = heap_alloc(16)
    mu.mem_write(INPUT_SIZE_ADDR, struct.pack("<I", 139))
    mu.mem_write(OUTPUT_SIZE_ADDR, struct.pack("<I", 760))
    
    # Set up registers for s3eCompressionDecomp:
    # r0 = input pointer
    # r1 = output pointer
    # r2 = pointer to input_size
    # r3 = pointer to output_size
    # [sp, #0x38] = type (4) — this is the 5th arg, on the stack
    # 
    # Wait, ARM calling convention: first 4 args in r0-r3, rest on stack.
    # But [sp, #0x38] means the 5th arg is at sp+0x38 AFTER the push.
    # The push saves 9 registers (36 bytes) + sub sp, #0x14 (20 bytes) = 56 = 0x38
    # So the 5th arg was at the original sp+0 before the call.
    # That means we need to push it onto the stack BEFORE the call.
    
    # Push type=4 onto stack
    sp_call = sp - 4
    mu.mem_write(sp_call, struct.pack("<I", 4))
    
    mu.reg_write(UC_ARM_REG_SP, sp_call)
    mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
    mu.reg_write(UC_ARM_REG_R0, INPUT_BUF)
    mu.reg_write(UC_ARM_REG_R1, OUTPUT_BUF)
    mu.reg_write(UC_ARM_REG_R2, INPUT_SIZE_ADDR)
    mu.reg_write(UC_ARM_REG_R3, OUTPUT_SIZE_ADDR)
    
    call_log.clear()
    
    try:
        mu.emu_start(0x51c1c, RET_ADDR, timeout=30*1000000)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  Returned: r0 = 0x{r0:x}")
        
        # Check output
        out_data = bytes(mu.mem_read(OUTPUT_BUF, 760))
        if any(b != 0 for b in out_data[:100]):
            print(f"  Output has data! First 200 bytes:")
            print(f"    {out_data[:200]!r}")
            # Save to file
            with open("/home/z/my-project/work/dz_extracted/file0_test.bin", "wb") as f:
                f.write(out_data)
            print(f"  Saved to dz_extracted/file0_test.bin")
            break
        else:
            print(f"  Output is all zeros (decompression failed)")
            
    except UcError as e:
        pc = mu.reg_read(UC_ARM_REG_PC)
        print(f"  Error: {e} at PC=0x{pc:x}")
        
        # Print call log
        print(f"  Call log ({len(call_log)} calls):")
        seen = set()
        for call_addr, target in call_log[-30:]:
            if target not in seen:
                seen.add(target)
                print(f"    0x{call_addr:08x} -> 0x{target:08x}")
    
    break  # only try first base for now

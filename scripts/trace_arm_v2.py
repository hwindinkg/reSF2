#!/usr/bin/env python3
"""
Trace ARM DZ decoder with full code hooks.
Maps libs3e_android.so and directly calls the simple-path init (FUN_000388a4),
then decodes ONE element from the forge.xml stream, tracing every prob table access.
"""
import struct, sys, os
from unicorn import *
from unicorn.arm_const import *

REPO = "E:/reSF2"
SO_PATH = f"{REPO}/reverse/binaries/libs3e_android.so"
DZ_PATH = f"{REPO}/assets/files.dz"

# Load .so
with open(SO_PATH, "rb") as f:
    SO = f.read()

# Parse ELF segments
e_phoff = struct.unpack_from("<I", SO, 0x1c)[0]
e_phentsize = struct.unpack_from("<H", SO, 0x2a)[0]
e_phnum = struct.unpack_from("<H", SO, 0x2c)[0]
segments = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p = struct.unpack_from("<IIIIIIII", SO, off)
    if p[0] == 1:  # PT_LOAD
        segments.append((p[2], p[1], p[4], p[5]))

# Create emulator - map segments individually
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
for vaddr, offset, filesz, memsz in segments:
    # Round down to page boundary
    page_start = vaddr & ~0xFFF
    page_end = ((vaddr + memsz - 1) & ~0xFFF) + 0x1000
    try:
        mu.mem_map(page_start, page_end - page_start)
    except UcError:
        pass  # might overlap
    mu.mem_write(vaddr, SO[offset:offset + filesz])
    if memsz > filesz:
        try:
            mu.mem_write(vaddr + filesz, b'\x00' * (memsz - filesz))
        except:
            pass  # might be beyond loaded

# Stack
STACK_BASE = 0x80000000
STACK_SIZE = 0x100000
mu.mem_map(STACK_BASE, STACK_SIZE)
sp = STACK_BASE + STACK_SIZE - 0x1000

# Simple bump allocator heap (use high address to avoid conflicts)
HEAP_BASE = 0x30000000
HEAP_SIZE = 0x8000000
mu.mem_map(HEAP_BASE, HEAP_SIZE)
heap_ptr = [HEAP_BASE]

# Input/output buffer area
BUF_BASE = 0x20000000
BUF_SIZE = 0x10000000
mu.mem_map(BUF_BASE, BUF_SIZE)

# TLS area
TLS_BASE = 0x70000000
TLS_SIZE = 0x10000
mu.mem_map(TLS_BASE, TLS_SIZE)
mu.mem_write(TLS_BASE, b'\x00' * TLS_SIZE)

# Read files.dz - proper DTRZ parser
with open(DZ_PATH, "rb") as f:
    dz = f.read()
assert dz[:4] == b'DTRZ'
num_files = struct.unpack_from("<H", dz, 4)[0]
num_dirs  = struct.unpack_from("<H", dz, 6)[0]

pos = 9
file_names = []
for _ in range(num_files):
    end = dz.index(b"\x00", pos)
    file_names.append(dz[pos:end].decode("utf-8", errors="replace"))
    pos = end + 1
dir_names = []
for _ in range(num_dirs):
    end = dz.index(b"\x00", pos)
    dir_names.append(dz[pos:end].decode("utf-8", errors="replace"))
    pos = end + 1
pos += num_files * 6  # file attribute table
pos += 4               # lengths header

# File table
file_table = []
for i in range(num_files):
    f0, f1, f2, f3 = struct.unpack_from("<IIII", dz, pos)
    unc  = f0 & 0xFFFFFF
    off  = f1 & 0xFFFFFF
    comp = f2 & 0xFFFFFF
    typ  = (f2 >> 24) & 0xFF
    file_table.append({
        "name": file_names[i], "offset": off,
        "comp_size": comp, "uncomp_size": unc, "type": typ
    })
    pos += 16
data_start = pos

# Find forge.xml
forge = None
for ft in file_table:
    if ft['name'] == 'forge.xml':
        forge = ft
        break
assert forge is not None, "forge.xml not found!"
print(f"forge.xml: offset=0x{forge['offset']:x}, comp_size={forge['comp_size']}, uncomp_size={forge['uncomp_size']}, type={forge['type']}")

comp_start = data_start + forge['offset']
comp_data = dz[comp_start:comp_start + forge['comp_size']]
print(f"Compressed data ({len(comp_data)} bytes): {comp_data[:32].hex()}")

# ─── DIRECT CALL: simple-path init (FUN_000388a4) ───
# From ARM analysis: FUN_000388a4 takes (ctx, param_2=0) for simple path.
# The simple path allocates a 0x500-byte prob table (640 uint16 entries),
# sets up 10 sub-table pointers at ctx+0x20..ctx+0x3f.

# First we need a context buffer to work with
CTX_ADDR = 0x21000000
mu.mem_write(CTX_ADDR, b'\x00' * 0x10000)

# Set up a return address
RET_ADDR = 0x24000000
mu.mem_write(RET_ADDR, b'\xfe\xde\xff\xe7')  # UDF #0xdead (in BUF_BASE area)

# Ghidra image base = 0x10000, so ELF vaddr = Ghidra_addr - 0x10000
IMAGE_BASE = 0x10000

# Simple-path init: FUN_000388a4 (Ghidra 0x388a4 → ELF 0x288a4)
INIT_ADDR = 0x388a4 - IMAGE_BASE  # = 0x288a4
# Simple-path decode: FUN_000385b4 (Ghidra 0x385b4 → ELF 0x285b4)
DECODE_ADDR = 0x385b4 - IMAGE_BASE  # = 0x285b4

# Before calling init, we need to handle the function 0x800fc (TLS accessor)
# and any allocator stubs.
# Let's set up hooks for known function addresses

# Find s3eMallocBase and s3eFreeBase
# Parse dynamic symbols
e_shoff = struct.unpack_from("<I", SO, 0x20)[0]
e_shentsize = struct.unpack_from("<H", SO, 0x2e)[0]
e_shnum = struct.unpack_from("<H", SO, 0x30)[0]
e_shstrndx = struct.unpack_from("<H", SO, 0x32)[0]
sections = [struct.unpack_from("<IIIIIIIIII", SO, e_shoff + i*e_shentsize) for i in range(e_shnum)]
shstrtab = sections[e_shstrndx]
shstrtab_data = SO[shstrtab[4]:shstrtab[4]+shstrtab[5]]
def sec_name(i):
    s = sections[i]
    end = shstrtab_data.find(b'\x00', s[0])
    return shstrtab_data[s[0]:end].decode('latin-1')

dynsym = dynstr = None
for i, s in enumerate(sections):
    n = sec_name(i)
    if n == '.dynsym': dynsym = s
    elif n == '.dynstr': dynstr = s

dynstr_data = SO[dynstr[4]:dynstr[4]+dynstr[5]]
symbols = {}
for i in range(dynsym[5] // 16):
    off = dynsym[4] + i * 16
    st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", SO, off)
    end = dynstr_data.find(b'\x00', st_name)
    nm = dynstr_data[st_name:end].decode('latin-1')
    symbols[nm] = st_value

malloc_addr = symbols.get('s3eMallocBase', 0)
free_addr = symbols.get('s3eFreeBase', 0)
print(f"malloc=0x{malloc_addr:x} free=0x{free_addr:x}")

# Handle unmapped memory
def hook_mem(uc, access, address, size, value, user_data):
    page = address & ~0xFFF
    access_name = {1:"READ", 2:"WRITE", 4:"FETCH", 3:"RW", 5:"R_FETCH", 6:"W_FETCH", 7:"ALL"}.get(access, f"UNKNOWN({access})")
    try:
        mu.mem_map(page, 0x1000)
        print(f"  [MAP] {access_name} at 0x{address:x} size={size} page=0x{page:x} mapped")
        return True
    except UcError as e:
        print(f"  [MAP FAIL] {access_name} at 0x{address:x} page=0x{page:x}: {e}")
        return False
mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED, hook_mem)

# Also hook FETCH_PROT (executing from non-executable memory)
def hook_mem_prot(uc, access, address, size, value, user_data):
    access_name = {1:"READ", 2:"WRITE", 4:"FETCH", 3:"RW", 5:"R_FETCH", 6:"W_FETCH", 7:"ALL"}.get(access, f"UNKNOWN({access})")
    print(f"  [PROT] {access_name} at 0x{address:x} size={size}")
    # Try to map as executable
    return False  # don't handle it, let's see the error
mu.hook_add(UC_HOOK_MEM_READ_PROT | UC_HOOK_MEM_WRITE_PROT | UC_HOOK_MEM_FETCH_PROT, hook_mem_prot)

# Hook for allocator / TLS
def hook_code(uc, address, size, user_data):
    if address == malloc_addr:
        # Bump allocator
        sz = uc.reg_read(UC_ARM_REG_R0)
        sz = (sz + 3) & ~3
        ptr = heap_ptr[0]
        heap_ptr[0] = ptr + sz
        uc.reg_write(UC_ARM_REG_R0, ptr)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    elif address == free_addr:
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    elif address == 0x700fc:  # Ghidra 0x800fc - IMAGE_BASE = ELF vaddr
        # Actually s3eMemoryGetInt inside the function at Ghidra 0x800e0
        uc.reg_write(UC_ARM_REG_R0, TLS_BASE)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

if malloc_addr:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=malloc_addr, end=malloc_addr+1)
if free_addr:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=free_addr, end=free_addr+1)
mu.hook_add(UC_HOOK_CODE, hook_code, begin=0x700fc, end=0x700fc+1)

# Set up vtable for context
# FUN_000388a4 expects ctx[4] to point to a table where:
#   table[0] = init function (called with r0=ctx, r1=1, r2=0x74)
#   table[4] = alloc function (called with r0=ctx, r1=1, r2=size) → returns ptr

INIT_STUB_ADDR = 0x23000100
ALLOC_STUB_ADDR = 0x23000200
VTABLE_ADDR = 0x23000000

# Init stub - mov r0,#0; bx lr
mu.mem_write(INIT_STUB_ADDR, b'\x00\x00\xa0\xe3\x1e\xff\x2f\xe1')

# Alloc stub - will be caught by hook_vtable hook
mu.mem_write(ALLOC_STUB_ADDR, b'\xfe\xde\xff\xe7')  # UDF just in case

# Register hooks for the stub addresses (MUST be before the table write that references them)
def hook_vtable(uc, address, size, user_data):
    if address == INIT_STUB_ADDR:
        # Init function: r0=ctx, r1=flag, r2=size
        # Should allocate a buffer of size r2 (0x74) and return pointer
        sz = uc.reg_read(UC_ARM_REG_R2)
        sz = (sz + 3) & ~3  # align to 4
        ptr = heap_ptr[0]
        heap_ptr[0] = ptr + sz
        uc.reg_write(UC_ARM_REG_R0, ptr)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    elif address == ALLOC_STUB_ADDR:
        # Alloc function: r0=ctx, r1=1, r2=size
        sz = uc.reg_read(UC_ARM_REG_R2)
        sz = (sz + 3) & ~3  # align to 4
        ptr = heap_ptr[0]
        heap_ptr[0] = ptr + sz
        uc.reg_write(UC_ARM_REG_R0, ptr)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

mu.hook_add(UC_HOOK_CODE, hook_vtable, begin=INIT_STUB_ADDR, end=INIT_STUB_ADDR+4)
mu.hook_add(UC_HOOK_CODE, hook_vtable, begin=ALLOC_STUB_ADDR, end=ALLOC_STUB_ADDR+4)

# Write vtable pointing to stubs
mu.mem_write(VTABLE_ADDR, struct.pack("<II", INIT_STUB_ADDR, ALLOC_STUB_ADDR))
mu.mem_write(CTX_ADDR + 0x04, struct.pack("<I", VTABLE_ADDR))

# Add trace hook at function entry
def hook_entry(uc, address, size, user_data):
    r0 = uc.reg_read(UC_ARM_REG_R0)
    r1 = uc.reg_read(UC_ARM_REG_R1)
    r2 = uc.reg_read(UC_ARM_REG_R2)
    r3 = uc.reg_read(UC_ARM_REG_R3)
    lr = uc.reg_read(UC_ARM_REG_LR)
    # Read the first few words at PC
    try:
        code_at_pc = mu.mem_read(address, 16)
        code_hex = code_at_pc.hex()
    except:
        code_hex = "???"
    print(f"  [{address:#x}] Entry: r0=0x{r0:08x} r1=0x{r1:08x} r2=0x{r2:08x} r3=0x{r3:08x} lr=0x{lr:08x} code={code_hex}")

mu.hook_add(UC_HOOK_CODE, hook_entry, begin=INIT_ADDR, end=INIT_ADDR+4)

# Also trace the bad address to see what comes before
def hook_trap(uc, address, size, user_data):
    r0 = uc.reg_read(UC_ARM_REG_R0)
    r1 = uc.reg_read(UC_ARM_REG_R1)
    lr = uc.reg_read(UC_ARM_REG_LR)
    print(f"  [{address:#x}] TRAP: r0=0x{r0:08x} r1=0x{r1:08x} lr=0x{lr:08x}")
mu.hook_add(UC_HOOK_CODE, hook_trap, begin=0x41c0, end=0x41c0+4)

# Call FUN_000388a4(ctx, 0) for simple-path init
print(f"\nCalling simple-path init FUN_000388a4 @ 0x{INIT_ADDR:x}...")
sp_call = sp - 0x200
mu.reg_write(UC_ARM_REG_SP, sp_call)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, CTX_ADDR)  # context buffer
mu.reg_write(UC_ARM_REG_R1, 0)          # param_2 = 0 (simple path)

try:
    mu.emu_start(INIT_ADDR, RET_ADDR, timeout=10*1000000, count=100000)
    print("  Init returned OK!")
    
    # Check what init wrote - first what's at CTX_ADDR
    ctx_bytes = mu.mem_read(CTX_ADDR, 0x200)
    ctx32 = struct.unpack_from("<128I", ctx_bytes, 0)
    print("  First 16 dwords of context:")
    for i in range(16):
        print(f"    ctx[{i:02x}] @ 0x{CTX_ADDR+i*4:x} = 0x{ctx32[i]:08x}")
    
    # The "result" buffer was stored at ctx+0x1ac
    r10_addr = ctx32[0x1ac // 4] if 0x1ac // 4 < len(ctx32) else 0
    print(f"  Result buffer (ctx+0x1ac) = 0x{r10_addr:x}")
    
    if r10_addr and r10_addr >= HEAP_BASE and r10_addr < HEAP_BASE + HEAP_SIZE:
        # Read the result buffer
        res_bytes = mu.mem_read(r10_addr, 0x200)
        res32 = struct.unpack_from("<128I", res_bytes, 0)
        print("  Result buffer (first 32 dwords):")
        for i in range(32):
            print(f"    res[{i:02x}] @ 0x{r10_addr+i*4:x} = 0x{res32[i]:08x}")
        
        # Sub-table pointers at result+0x20..0x44
        print("  Sub-table pointers:")
        for i in range(8, 18):
            ptr = res32[i]
            print(f"    res[{i:02x}] = 0x{ptr:08x}")
        
        # The prob table base should be at res[0x20/4] = res[8]
        prob_base = res32[8]
        if prob_base >= HEAP_BASE and prob_base < HEAP_BASE + HEAP_SIZE:
            prob_data = mu.mem_read(prob_base, 0x500)
            prob16 = struct.unpack_from(f"<{0x500//2}H", prob_data, 0)
            print(f"\n  Prob table at 0x{prob_base:x}:")
            print("   idx   hex   dec")
            for i in range(min(20, len(prob16))):
                print(f"    {i:3d}  0x{prob16[i]:04x}  {prob16[i]:5d}")
            nonzero = sum(1 for p in prob16 if p != 0)
            print(f"  Non-zero entries: {nonzero}/{len(prob16)}")
        
        print(f"\n  Context dump (offset 0x00 to 0x100):")
        ctx_dump = mu.mem_read(CTX_ADDR, 0x100)
        for i in range(0, 0x100, 16):
            hex_str = ' '.join(f'{b:02x}' for b in ctx_dump[i:i+16])
            print(f"    0x{i:03x}: {hex_str}")
    
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"Error: {e} at PC=0x{pc:x}")
    for i in range(14):
        try:
            print(f"  r{i} = 0x{mu.reg_read(UC_ARM_REG_R0 + i):08x}")
        except:
            pass
    sp_val = mu.reg_read(UC_ARM_REG_SP)
    print(f"  sp = 0x{sp_val:08x}")
    print(f"  lr = 0x{mu.reg_read(UC_ARM_REG_LR):08x}")

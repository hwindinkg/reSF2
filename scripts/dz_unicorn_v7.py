#!/usr/bin/env python3
"""DZ decompression via Unicorn — manually set up coder table and call s3eCompressionDecomp.

Strategy:
1. Load the .so into Unicorn
2. Manually set up the coder function pointer table at 0xc8514
3. Provide stubs for s3eMalloc/s3eFree (simple bump allocator)
4. Call s3eCompressionDecomp(in, out, &in_size, &out_size, type=4)

The coder table at 0xc8514 has 4 slots (0x88 bytes each):
  +0x64 = init function pointer
  +0x68 = read function pointer
  +0x78 = type (4 = DZ)

Function pointers (from .data analysis):
  type=4 DZ coder init = 0x000b3378
  DZ read handler = 0x51f60
"""
import struct, os, sys
from unicorn import *
from unicorn.arm_const import *

SO_PATH = "/home/z/my-project/work/sf2_data/sf2/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

# Parse ELF
e_phoff = struct.unpack_from("<I", so_data, 0x1c)[0]
e_phentsize = struct.unpack_from("<H", so_data, 0x2a)[0]
e_phnum = struct.unpack_from("<H", so_data, 0x2c)[0]
segments = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p = struct.unpack_from("<IIIIIIII", so_data, off)
    if p[0] == 1: segments.append((p[2], p[1], p[4], p[5]))

e_shoff = struct.unpack_from("<I", so_data, 0x20)[0]
e_shentsize = struct.unpack_from("<H", so_data, 0x2e)[0]
e_shnum = struct.unpack_from("<H", so_data, 0x30)[0]
e_shstrndx = struct.unpack_from("<H", so_data, 0x32)[0]
sections = [struct.unpack_from("<IIIIIIIIII", so_data, e_shoff + i*e_shentsize) for i in range(e_shnum)]
shstrtab = sections[e_shstrndx]
shstrtab_data = so_data[shstrtab[4]:shstrtab[4]+shstrtab[5]]
def get_name(idx):
    end = shstrtab_data.find(b'\x00', idx)
    return shstrtab_data[idx:end].decode()
rel_dyn = dynsym = dynstr = rel_plt = None
for s in sections:
    n = get_name(s[0])
    if n == '.rel.dyn': rel_dyn = s
    elif n == '.rel.plt': rel_plt = s
    elif n == '.dynsym': dynsym = s
    elif n == '.dynstr': dynstr = s
dynstr_data = so_data[dynstr[4]:dynstr[4]+dynstr[5]]
symbols = []
for i in range(dynsym[5] // dynsym[6]):
    off = dynsym[4] + i * dynsym[6]
    st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", so_data, off)
    end = dynstr_data.find(b'\x00', st_name)
    symbols.append({'name': dynstr_data[st_name:end].decode(), 'value': st_value, 'shndx': st_shndx})

# Find symbol addresses we need
sym_map = {s['name']: s['value'] for s in symbols if s['value']}
print("Key symbols:")
for name in ['s3eCompressionDecomp', 's3eCompressionDecompInit',
             's3eMallocBase', 's3eFreeBase', 's3eMalloc', 's3eFree',
             's3eMemoryAlloc', 's3eMemoryFree']:
    if name in sym_map:
        print(f"  {name} = 0x{sym_map[name]:x}")

# Create emulator
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(0, 0x10000000)  # 256 MB for .so
for vaddr, offset, filesz, memsz in segments:
    mu.mem_write(vaddr, so_data[offset:offset + filesz])
    # Zero-fill .bss (memsz > filesz)
    if memsz > filesz:
        mu.mem_write(vaddr + filesz, b'\x00' * (memsz - filesz))

# Apply relocations
for i in range(rel_dyn[5] // 8):
    off = rel_dyn[4] + i * 8
    r_offset, r_info = struct.unpack_from("<II", so_data, off)
    r_type = r_info & 0xFF; r_sym = r_info >> 8
    if r_type in (2, 21, 22) and r_sym < len(symbols):
        sym = symbols[r_sym]
        if sym['value'] != 0 or sym['shndx'] != 0:
            mu.mem_write(r_offset, struct.pack("<I", sym['value']))

# Stack and heap
mu.mem_map(0x80000000, 0x100000)
sp = 0x80000000 + 0x100000 - 0x1000
mu.mem_map(0x90000000, 0x4000000)  # 64MB heap

# Bump allocator for s3eMalloc stubs
heap_ptr = 0x90000000
malloc_addr = sym_map.get('s3eMallocBase', 0)
free_addr = sym_map.get('s3eFreeBase', 0)

# Write malloc/free stubs
# s3eMallocBase: r0 = size, returns ptr
MALLOC_CODE = b'\x00\x00\x80\xe2\x01\x00\x50\xe2\x04\x00\x90\x08\x1e\xff\x2f\xe1'
# eor r0, r0, r0 ; sub r0, r0, #1 ; str r0, [r0, #4] ; bx lr  -- doesn't make sense
# Actually let me write proper ARM:
# mov r0, #0x90000000 (but that's a complex immediate)
# Better: use a global variable for heap_ptr

# Allocate a heap pointer global
HEAP_PTR_GLOBAL = 0xc9000  # in .bss area
mu.mem_write(HEAP_PTR_GLOBAL, struct.pack("<I", 0x90000000))

# s3eMallocBase stub:
#   ldr r1, =HEAP_PTR_GLOBAL
#   ldr r2, [r1]
#   add r2, r2, r0  (r0 = size, align to 4)
#   str r2, [r1]
#   mov r0, r2 (old ptr) -- wait, we need the OLD value
# Actually:
#   ldr r1, =HEAP_PTR_GLOBAL
#   ldr r0, [r1]        ; r0 = current heap ptr (return value)
#   ldr r2, [r1]        ; r2 = current heap ptr
#   add r2, r2, r1(size)  ; but r1 is now the global addr...
# Let me use a simpler approach: write the stub in C-like pseudocode and compile by hand

# s3eMallocBase(size_t size):
#   prev = heap_ptr_global
#   heap_ptr_global = prev + (size + 3) & ~3  (align to 4)
#   return prev
MALLOC_ASM = bytes([
    # ldr r1, [pc, #0]    ; r1 = address of HEAP_PTR_GLOBAL (literal pool)
    0x01, 0x10, 0x9f, 0xe5,
    # ldr r0, [r1]        ; r0 = *heap_ptr_global (current ptr = return value)
    0x00, 0x00, 0x91, 0xe5,
    # add r2, r0, r0(size)  -- but r0 is now the return value...
    # Wait, we need size. But size was in r0, and we just overwrote it.
    # Let me restructure:
])
# Actually, let me use a different approach. Write the stub at a known address
# and use Python to generate the machine code.

# Simplest: use a hook
def hook_code(uc, address, size, user_data):
    global heap_ptr
    if address == malloc_addr:
        # s3eMallocBase(size) -> ptr
        alloc_size = uc.reg_read(UC_ARM_REG_R0)
        alloc_size = (alloc_size + 3) & ~3  # align to 4
        ptr = heap_ptr
        heap_ptr += alloc_size
        uc.reg_write(UC_ARM_REG_R0, ptr)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    elif address == free_addr:
        # s3eFreeBase(ptr, size) -> void
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    elif address == 0x800fc:
        # Mystery function called by coder init
        # Returns a pointer to thread-local storage or global state
        # Let's return a pointer to a zeroed buffer
        global TLS_PTR
        uc.reg_write(UC_ARM_REG_R0, TLS_PTR)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

TLS_PTR = 0x95000000
mu.mem_map(TLS_PTR, 0x10000)
mu.mem_write(TLS_PTR, b'\x00' * 0x10000)

if malloc_addr:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=malloc_addr, end=malloc_addr+1)
    print(f"Hooked s3eMallocBase at 0x{malloc_addr:x}")
if free_addr:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=free_addr, end=free_addr+1)
    print(f"Hooked s3eFreeBase at 0x{free_addr:x}")
mu.hook_add(UC_HOOK_CODE, hook_code, begin=0x800fc, end=0x800fc+1)
print("Hooked 0x800fc (TLS accessor)")

# Also hook mutex functions (called by coder init)
# 0x5b6c4 = mutex lock, 0x5b7d4 = mutex unlock
for addr in [0x5b6c4, 0x5b7d4]:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=addr, end=addr+1)

# Set up PLT stubs (return 0 for all other functions)
STUB_BASE = 0x70000000
mu.mem_map(STUB_BASE, 0x10000)
stub_ptr = STUB_BASE
STUB_CODE = b'\x00\x00\xa0\xe3\x1e\xff\x2f\xe1'  # mov r0, #0; bx lr
for i in range(rel_plt[5] // 8):
    off = rel_plt[4] + i * 8
    r_offset, r_info = struct.unpack_from("<II", so_data, off)
    r_sym = r_info >> 8
    sym = symbols[r_sym]
    name = sym['name']
    # Don't stub s3eMalloc/s3eFree (already hooked)
    if 'alloc' in name.lower() and 's3e' in name.lower():
        if malloc_addr:
            mu.mem_write(r_offset, struct.pack("<I", malloc_addr))
            continue
    if 'free' in name.lower() and 's3e' in name.lower():
        if free_addr:
            mu.mem_write(r_offset, struct.pack("<I", free_addr))
            continue
    mu.mem_write(stub_ptr, STUB_CODE)
    mu.mem_write(r_offset, struct.pack("<I", stub_ptr))
    stub_ptr += 16

def hook_mem(uc, access, address, size, value, user_data):
    page = address & ~0xFFF
    try: uc.mem_map(page, 0x1000); return True
    except: return False
mu.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_READ_UNMAPPED, hook_mem)

RET_ADDR = 0xDEAD0000
mu.mem_map(RET_ADDR & ~0xFFF, 0x1000)
mu.mem_write(RET_ADDR, b'\xfe\xde\xff\xe7')

# Call init_array constructors to set up global state
print("\nCalling init_array constructors...")
for i in range(8):
    val = struct.unpack_from("<I", so_data, 0xc0108 + i * 4)[0]
    if val == 0: continue
    mu.reg_write(UC_ARM_REG_SP, sp)
    mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
    try:
        mu.emu_start(val, RET_ADDR, timeout=5*1000000, count=10000)
    except:
        pass

# Manually set up the DZ coder table at 0xc8514
# Each slot is 0x88 bytes. Slot 0 is for type=4 (DZ).
# From the README and disassembly:
#   +0x64 = init function = 0x000b3378 (DZ coder init)
#   +0x68 = read function = 0x51f60 (DZ read handler)
#   +0x78 = type = 4
print("\nSetting up DZ coder table...")
CODER_TABLE = 0xc8514
# Clear all 4 slots
mu.mem_write(CODER_TABLE, b'\x00' * (4 * 0x88))
# Set up slot 0
mu.mem_write(CODER_TABLE + 0x64, struct.pack("<I", 0x000b3378))  # init
mu.mem_write(CODER_TABLE + 0x68, struct.pack("<I", 0x51f60))     # read
mu.mem_write(CODER_TABLE + 0x78, struct.pack("<I", 4))           # type

# Read compressed data
DZ_PATH = "/home/z/my-project/work/sf2_data/sf2/assets/assets/files.dz"
with open(DZ_PATH, "rb") as f:
    dz_data = f.read()

# Parse archive
assert dz_data[:4] == b'DTRZ'
num_files = struct.unpack_from('<H', dz_data, 4)[0]
pos = 9
filenames = []
for _ in range(num_files):
    end = dz_data.index(b'\x00', pos)
    filenames.append(dz_data[pos:end].decode())
    pos = end + 1
num_dirs = struct.unpack_from('<H', dz_data, 6)[0]
for _ in range(max(0, num_dirs - 1)):
    end = dz_data.index(b'\x00', pos)
    pos = end + 1
pos += num_files * 6 + 4
files = []
for i in range(num_files):
    offset, uncomp_size, comp_size, comp_type = struct.unpack_from('<IIII', dz_data, pos)
    files.append({'name': filenames[i], 'offset': offset, 'uncomp_size': uncomp_size,
                  'comp_size': comp_size, 'type': comp_type})
    pos += 16
data_start = pos

f = files[0]
print(f"\nDecompressing: {f['name']} (comp={f['comp_size']}, uncomp={f['uncomp_size']})")

# DZ is a streaming format — all files share one continuous stream.
# To decompress file N, we must decode files 0..N first.
# For the first file, we just need its compressed data.
comp_data = dz_data[data_start + f['offset'] : data_start + f['offset'] + f['comp_size']]

# Set up buffers
INPUT_BUF = 0x91000000
mu.mem_write(INPUT_BUF, comp_data)
OUTPUT_BUF = 0x92000000
mu.mem_write(OUTPUT_BUF, b'\x00' * f['uncomp_size'])
INPUT_SIZE_ADDR = 0x94000000
OUTPUT_SIZE_ADDR = 0x94000010
mu.mem_write(INPUT_SIZE_ADDR, struct.pack("<I", len(comp_data)))
mu.mem_write(OUTPUT_SIZE_ADDR, struct.pack("<I", f['uncomp_size']))

# Call s3eCompressionDecomp(in, out, &in_size, &out_size, type=4)
# Signature: int s3eCompressionDecomp(void* in, void* out, uint32* in_size, uint32* out_size, int type)
sp_call = sp - 0x20
mu.mem_write(sp_call, struct.pack("<I", 4))  # 5th arg: type = 4

mu.reg_write(UC_ARM_REG_SP, sp_call)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, INPUT_BUF)
mu.reg_write(UC_ARM_REG_R1, OUTPUT_BUF)
mu.reg_write(UC_ARM_REG_R2, INPUT_SIZE_ADDR)
mu.reg_write(UC_ARM_REG_R3, OUTPUT_SIZE_ADDR)

print(f"Calling s3eCompressionDecomp at 0x51c1c...")
try:
    mu.emu_start(0x51c1c, RET_ADDR, timeout=120*1000000, count=50000000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"Returned: r0 = {r0}")
    in_consumed = struct.unpack("<I", mu.mem_read(INPUT_SIZE_ADDR, 4))[0]
    out_produced = struct.unpack("<I", mu.mem_read(OUTPUT_SIZE_ADDR, 4))[0]
    print(f"Input consumed: {in_consumed}/{len(comp_data)}")
    print(f"Output produced: {out_produced}/{f['uncomp_size']}")
    out_data = bytes(mu.mem_read(OUTPUT_BUF, f['uncomp_size']))
    nonzero = sum(1 for b in out_data if b != 0)
    print(f"Non-zero bytes: {nonzero}/{f['uncomp_size']}")
    if nonzero > 10:
        print(f"First 200 bytes: {out_data[:200]!r}")
        os.makedirs("/home/z/my-project/work/dz_extracted", exist_ok=True)
        out_path = f"/home/z/my-project/work/dz_extracted/{f['name']}"
        with open(out_path, "wb") as fout:
            fout.write(out_data)
        print(f"SAVED to {out_path}!")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"Error: {e} at PC=0x{pc:x}")
    for i in range(13):
        print(f"  r{i} = 0x{mu.reg_read(UC_ARM_REG_R0 + i):x}")

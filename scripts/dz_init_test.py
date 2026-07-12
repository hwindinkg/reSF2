#!/usr/bin/env python3
"""DZ decompression — call init then read functions directly.

Bypass s3eCompressionDecomp entirely. Instead:
1. Call s3eCompressionDecompInit(type=4) to get a context handle
2. Call the read function repeatedly to decompress

From disassembly:
- s3eCompressionDecompInit(r0=type, r1=read_callback, r2=close_callback) at 0x51414
  Actually let me check the signature properly.
"""
import struct, os, sys
from unicorn import *
from unicorn.arm_const import *
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

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
sym_map = {s['name']: s['value'] for s in symbols if s['value']}

# Create emulator
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(0, 0x10000000)
for vaddr, offset, filesz, memsz in segments:
    mu.mem_write(vaddr, so_data[offset:offset + filesz])
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
mu.mem_map(0x90000000, 0x4000000)

# Bump allocator
heap_ptr = [0x90000000]
malloc_addr = sym_map.get('s3eMallocBase', 0x6e5e8)  # fallback
free_addr = sym_map.get('s3eFreeBase', 0x6e5f8)

# Thread-local storage / global state pointer
TLS_PTR = 0x95000000
mu.mem_map(TLS_PTR, 0x10000)
mu.mem_write(TLS_PTR, b'\x00' * 0x10000)

def hook_code(uc, address, size, user_data):
    if address == malloc_addr:
        alloc_size = uc.reg_read(UC_ARM_REG_R0)
        alloc_size = (alloc_size + 3) & ~3
        ptr = heap_ptr[0]
        heap_ptr[0] += alloc_size
        uc.reg_write(UC_ARM_REG_R0, ptr)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    elif address == free_addr:
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
    elif address == 0x800fc:
        # Mystery function - returns TLS pointer
        uc.reg_write(UC_ARM_REG_R0, TLS_PTR)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

if malloc_addr:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=malloc_addr, end=malloc_addr+4)
if free_addr:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=free_addr, end=free_addr+4)
mu.hook_add(UC_HOOK_CODE, hook_code, begin=0x800fc, end=0x800fc+4)
# Mutex functions
for addr in [0x5b6c4, 0x5b7d4, 0x5be3c, 0x5be58, 0x5be78]:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=addr, end=addr+4)

# PLT stubs
STUB_BASE = 0x70000000
mu.mem_map(STUB_BASE, 0x10000)
stub_ptr = STUB_BASE
STUB_CODE = b'\x00\x00\xa0\xe3\x1e\xff\x2f\xe1'
for i in range(rel_plt[5] // 8):
    off = rel_plt[4] + i * 8
    r_offset, r_info = struct.unpack_from("<II", so_data, off)
    r_sym = r_info >> 8
    sym = symbols[r_sym]
    name = sym['name']
    if 'alloc' in name.lower() and 's3e' in name.lower() and malloc_addr:
        mu.mem_write(r_offset, struct.pack("<I", malloc_addr))
    elif 'free' in name.lower() and 's3e' in name.lower() and free_addr:
        mu.mem_write(r_offset, struct.pack("<I", free_addr))
    else:
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

# Call init_array constructors
print("Calling init_array constructors...")
for i in range(8):
    val = struct.unpack_from("<I", so_data, 0xc0108 + i * 4)[0]
    if val == 0: continue
    mu.reg_write(UC_ARM_REG_SP, sp)
    mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
    try:
        mu.emu_start(val, RET_ADDR, timeout=5*1000000, count=10000)
    except:
        pass

# Now call s3eCompressionDecompInit(type=4)
# Signature: void* s3eCompressionDecompInit(int type)
# r0 = type (4)
print("\nCalling s3eCompressionDecompInit(4)...")
mu.reg_write(UC_ARM_REG_SP, sp)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, 4)
try:
    mu.emu_start(0x51414, RET_ADDR, timeout=30*1000000, count=10000000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"  Returned: 0x{r0:x}")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"  Error: {e} at PC=0x{pc:x}")

# Check the global coder table to see what was set up
print("\nCoder table at 0xc8514 after init:")
for slot in range(4):
    base = 0xc8514 + slot * 0x88
    init_fn = struct.unpack("<I", mu.mem_read(base + 0x64, 4))[0]
    read_fn = struct.unpack("<I", mu.mem_read(base + 0x68, 4))[0]
    type_val = struct.unpack("<I", mu.mem_read(base + 0x78, 4))[0]
    if init_fn or read_fn or type_val:
        print(f"  Slot {slot}: init=0x{init_fn:08x} read=0x{read_fn:08x} type={type_val}")

# Also check the global registry at 0xc3138+0x244 = 0xc337c
# The init function reads [ip + 0x244] where ip = 0xc8514 - X
# Actually let's check 0xc5758 (0xc8514 - 0x2dbc? no...)
# From 0x51430: ldr ip, [pc, #0x590] = 0x51438 + 0x590 = 0x519c8
# val = *0x519c8, then ip = 0x51438 + 8 + val
val = struct.unpack("<I", mu.mem_read(0x519c8, 4))[0] if 0x519c8 < 0x10000000 else 0
# Actually 0x519c8 is in .text, read from so_data
val = struct.unpack_from("<I", so_data, 0x519c8)[0]
ip = 0x51434 + 8 + val  # PC at 0x51434 is 0x51434, +8 = 0x5143c... wait
# LDR ip, [pc, #0x590]: PC = 0x51430, addr = (0x51430 & ~3) + 8 + 0x590 = 0x51430 + 8 + 0x590 = 0x519c8
# ADD ip, pc, ip: PC = 0x51434, +8 = 0x5143c
# ip = 0x5143c + val
ip = 0x5143c + struct.unpack('<i', struct.pack('<I', val))[0]
print(f"\nGlobal registry base: 0x{ip:x}")
# [ip + 0x244] is a counter (incremented by 1 each call)
counter = struct.unpack("<I", mu.mem_read(ip + 0x244, 4))[0]
print(f"Counter [ip+0x244]: {counter}")
# Check slots [ip+0x20] is a flag byte
for off in [0x20, 0x21, 0x22, 0x23]:
    flag = mu.mem_read(ip + off, 1)[0]
    print(f"  [ip+0x{off:02x}] = {flag}")

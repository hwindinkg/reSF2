#!/usr/bin/env python3
"""DZ decompression — full Unicorn emulation with proper coder init.

Strategy:
1. Run init_array constructors (they set up the coder function pointer table)
2. Call s3eCompressionDecompInit(4) to get a context handle
3. The handle is actually the slot pointer (0xc8514 + slot * 0x88)
4. Set up the context's input/output buffers
5. Call the read function (slot+0x68) to decompress

Key insight from disassembly:
- s3eCompressionDecompInit(type) returns a slot index (0-3), NOT a pointer
- The slot at 0xc8514 + index * 0x88 contains:
  +0x14 = input buffer
  +0x18 = output position
  +0x20 = output buffer (set by s3eCompressionDecomp)
  +0x80 = input size
  +0x84 = error flag
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

mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(0, 0x10000000)
for vaddr, offset, filesz, memsz in segments:
    mu.mem_write(vaddr, so_data[offset:offset + filesz])
    if memsz > filesz:
        mu.mem_write(vaddr + filesz, b'\x00' * (memsz - filesz))

for i in range(rel_dyn[5] // 8):
    off = rel_dyn[4] + i * 8
    r_offset, r_info = struct.unpack_from("<II", so_data, off)
    r_type = r_info & 0xFF; r_sym = r_info >> 8
    if r_type in (2, 21, 22) and r_sym < len(symbols):
        sym = symbols[r_sym]
        if sym['value'] != 0 or sym['shndx'] != 0:
            mu.mem_write(r_offset, struct.pack("<I", sym['value']))

mu.mem_map(0x80000000, 0x100000)
sp = 0x80000000 + 0x100000 - 0x1000
mu.mem_map(0x90000000, 0x4000000)

heap_ptr = [0x90000000]
malloc_addr = sym_map.get('s3eMallocBase', 0)
free_addr = sym_map.get('s3eFreeBase', 0)
TLS_PTR = 0x95000000
mu.mem_map(TLS_PTR, 0x10000)
mu.mem_write(TLS_PTR, b'\x00' * 0x10000)

# Track function calls for debugging
call_log = []
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
        uc.reg_write(UC_ARM_REG_R0, TLS_PTR)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

if malloc_addr:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=malloc_addr, end=malloc_addr+4)
if free_addr:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=free_addr, end=free_addr+4)
mu.hook_add(UC_HOOK_CODE, hook_code, begin=0x800fc, end=0x800fc+4)
for addr in [0x5b6c4, 0x5b7d4, 0x5be3c, 0x5be58, 0x5be78, 0x5be98, 0x5beb8]:
    mu.hook_add(UC_HOOK_CODE, hook_code, begin=addr, end=addr+4)

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
    try: mu.emu_start(val, RET_ADDR, timeout=5*1000000, count=10000)
    except: pass

# Check coder table
print("\nCoder table after init_array:")
for slot in range(4):
    base = 0xc8514 + slot * 0x88
    init_fn = struct.unpack("<I", mu.mem_read(base + 0x64, 4))[0]
    read_fn = struct.unpack("<I", mu.mem_read(base + 0x68, 4))[0]
    type_val = struct.unpack("<I", mu.mem_read(base + 0x78, 4))[0]
    flag = mu.mem_read(base + 0x20, 1)[0]
    if init_fn or read_fn or type_val or flag:
        print(f"  Slot {slot}: flag={flag} init=0x{init_fn:08x} read=0x{read_fn:08x} type={type_val}")

# Call s3eCompressionDecompInit(4)
print("\nCalling s3eCompressionDecompInit(4)...")
mu.reg_write(UC_ARM_REG_SP, sp)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, 4)
try:
    mu.emu_start(0x51414, RET_ADDR, timeout=30*1000000, count=10000000)
    handle = mu.reg_read(UC_ARM_REG_R0)
    print(f"  Returned: 0x{handle:x}")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"  Error: {e} at PC=0x{pc:x}")

# Check slot 3 (DZ type=4 uses slot 3 based on counter)
print("\nCoder table after init(4):")
for slot in range(4):
    base = 0xc8514 + slot * 0x88
    init_fn = struct.unpack("<I", mu.mem_read(base + 0x64, 4))[0]
    read_fn = struct.unpack("<I", mu.mem_read(base + 0x68, 4))[0]
    type_val = struct.unpack("<I", mu.mem_read(base + 0x78, 4))[0]
    flag = mu.mem_read(base + 0x20, 1)[0]
    if init_fn or read_fn or type_val or flag:
        print(f"  Slot {slot} (0x{base:x}): flag={flag} init=0x{init_fn:08x} read=0x{read_fn:08x} type={type_val}")

# The init function at 0x000ebbe0 is in .bss — it was set by the init_array
# constructors at runtime. Let's check what's actually there now.
# 0xebbe0 > 0xc3134 (file size), so it's in the mapped memory
print(f"\nFunction at 0xebbe0 (first 32 bytes):")
try:
    code_bytes = mu.mem_read(0xebbe0, 32)
    print(f"  {code_bytes.hex()}")
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    for ins in md.disasm(bytes(code_bytes), 0xebbe0):
        print(f"  0x{ins.address:08x}: {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}")
        if ins.mnemonic == 'pop' and 'pc' in ins.op_str.lower():
            break
except:
    print("  Could not read")

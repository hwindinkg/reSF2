"""DZ decompression via ARM emulation with BLX hook.

Strategy: hook the BLX r3 instruction at 0x518e0 and substitute
the function pointer with the real DZ coder init function.

The DZ coder init function is somewhere in the .text section.
From the .data table analysis, the type=4 coder function should
be at a specific address. Let me find it by looking at what
s3eCompressionDecompInit expects.

Actually, let me try a different approach:
1. Call s3eCompressionDecompInit(type=4) — it will try to call [slot+0x64]
2. Hook the BLX at 0x518e0 — when it fires, set r3 to a DZ coder function
3. The DZ coder init function needs to be found

The DZ coder init is likely the function that:
- Takes a context pointer
- Allocates/initializes the arithmetic decoder state
- Sets up the window/history buffer

Looking at the .so, the function at 0x50be4 is called from 0x51f0c
(in the DZ read path at 0x51e38). And 0x50be4 calls 0x800fc (malloc)
and then 0x7b3c0 (the actual state setup).

So the DZ coder init function is likely 0x50be4 or 0x7b3c0.

Let me try both.
"""
import struct, os
from unicorn import *
from unicorn.arm_const import *
import subprocess

SO_PATH = "/home/z/my-project/work/apk_extracted/apktool/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

# Find malloc/free
result = subprocess.run(['nm', '-D', SO_PATH], capture_output=True, text=True)
malloc_addr = free_addr = None
for line in result.stdout.split('\n'):
    if 's3eMallocBase' in line and ' T ' in line:
        malloc_addr = int(line.split()[0], 16)
    if 's3eFreeBase' in line and ' T ' in line:
        free_addr = int(line.split()[0], 16)

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

mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(0, 0x10000000)  # 256 MB
for vaddr, offset, filesz, memsz in segments:
    mu.mem_write(vaddr, so_data[offset:offset + filesz])
for i in range(rel_dyn[5] // 8):
    off = rel_dyn[4] + i * 8
    r_offset, r_info = struct.unpack_from("<II", so_data, off)
    r_type = r_info & 0xFF; r_sym = r_info >> 8
    if r_type in (2, 21, 22) and r_sym < len(symbols):
        sym = symbols[r_sym]
        if sym['value'] != 0 or sym['shndx'] != 0:
            mu.mem_write(r_offset, struct.pack("<I", sym['value']))

STUB_BASE = 0x70000000
mu.mem_map(STUB_BASE, 0x10000)
stub_ptr = STUB_BASE
STUB_CODE = b'\x00\x00\xa0\xe3\x1e\xff\x2f\xe1'
for i in range(rel_plt[5] // 8):
    off = rel_plt[4] + i * 8
    r_offset, r_info = struct.unpack_from("<II", so_data, off)
    r_sym = r_info >> 8
    if r_sym < len(symbols):
        sym = symbols[r_sym]
        name = sym['name']
        if malloc_addr and 'alloc' in name.lower() and 's3e' in name.lower():
            mu.mem_write(r_offset, struct.pack("<I", malloc_addr))
        elif free_addr and 'free' in name.lower() and 's3e' in name.lower():
            mu.mem_write(r_offset, struct.pack("<I", free_addr))
        else:
            mu.mem_write(stub_ptr, STUB_CODE)
            mu.mem_write(r_offset, struct.pack("<I", stub_ptr))
            stub_ptr += 16

mu.mem_map(0x80000000, 0x100000)
sp = 0x80000000 + 0x100000 - 0x1000
mu.mem_map(0x90000000, 0x4000000)

def hook_mem(uc, access, address, size, value, user_data):
    page = address & ~0xFFF
    try: uc.mem_map(page, 0x1000); return True
    except: return False
mu.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_READ_UNMAPPED, hook_mem)

RET_ADDR = 0xDEAD0000
mu.mem_map(0xDEAD0000 & ~0xFFF, 0x1000)
mu.mem_write(RET_ADDR, b'\xfe\xde\xff\xe7')

# Call init_array constructors
for i in range(8):
    val = struct.unpack_from("<I", so_data, 0xc0108 + i * 4)[0]
    if val == 0: continue
    mu.reg_write(UC_ARM_REG_SP, sp)
    mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
    try: mu.emu_start(val, RET_ADDR, timeout=5*1000000, count=10000)
    except: pass

# Clear context area
mu.mem_write(0xc8514, b'\x00' * 0x1000)

# Set up compressed data
DZ_PATH = "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"
with open(DZ_PATH, "rb") as f:
    dz_data = f.read()
comp_data = dz_data[0x1b9a + 4971 : 0x1b9a + 4971 + 139]

INPUT_BUF = 0x90000000
mu.mem_write(INPUT_BUF, comp_data)
OUTPUT_BUF = 0x91000000
INPUT_SIZE_ADDR = 0x92000000
OUTPUT_SIZE_ADDR = 0x92000010
mu.mem_write(INPUT_SIZE_ADDR, struct.pack("<I", 139))
mu.mem_write(OUTPUT_SIZE_ADDR, struct.pack("<I", 760))

# Global table for output/input pointers
r3_offset = struct.unpack_from("<I", so_data, 0x51e2c)[0]
r3_value = (0x51c88 + r3_offset) & 0xFFFFFFFF
mu.mem_write(r3_value, struct.pack("<II", OUTPUT_BUF, INPUT_BUF))

# *** KEY FIX: Set the DZ coder function pointers in ALL slots ***
# The DZ coder init function is at 0x50be4 (called from 0x51f0c)
# But actually, looking at the code flow:
# 0x518c0: ldr r3, [r1, #0x64]  → coder init function
# 0x518d8: ldr r1, [r1, #0x68]  → coder read function (passed as arg)
# 0x518e0: blx r3               → call coder init(ctx, read_fn, 2)
#
# The coder init at 0x50be4 takes:
# r0 = context (stack buffer for local state)
# r1 = read function pointer
# r2 = 2 (some mode)
#
# And it calls:
# 0x50c08: bl 0x800fc (malloc)
# 0x50c4c: bl 0x7b3c0 (actual state setup)
#
# So [slot+0x64] = 0x50be4 (DZ coder init)
# And [slot+0x68] = the read function for DZ
#
# The read function is likely 0x51f60 (the DZ read handler we found earlier)
# Or maybe it's 0x389f8 (the actual decoder)

# Let me try setting [slot+0x64] = 0x50be4 and [slot+0x68] = 0x51f60
for i in range(4):
    base = 0xc8514 + i * 0x88
    mu.mem_write(base + 0x64, struct.pack("<I", 0x50be4))  # coder init
    mu.mem_write(base + 0x68, struct.pack("<I", 0x51f60))  # coder read

# Also need to set up some more context fields
# The init function at 0x50be4 reads [r4 + 0xe9] (a flag byte)
# and [r4] (a linked list pointer)
# r4 is a global structure, not the slot context
# Let me find what r4 is

# At 0x50be4:
# 0x50c04: ldr r0, [ip, #0x38]  → r0 = some global pointer
# 0x50c08: bl 0x800fc           → call malloc(r0)? No, 0x800fc is PLT
# 
# Actually 0x800fc might be something else. Let me check.
# nm -D shows:
# 0x800fc is not an export. It's an internal function.
# Let me disassemble it

print("=== 0x800fc (called from DZ coder init) ===")
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
code = so_data[0x800fc:0x800fc + 0x40]
for ins in md.disasm(code, 0x800fc):
    print(f"  0x{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")
    if ins.mnemonic in ('pop', 'bx') and 'pc' in ins.op_str:
        break

# 0x800fc might be a thread-local storage accessor or a global config getter
# Let me just try the call and see what happens

# Call s3eCompressionDecomp
sp_call = sp - 4
mu.mem_write(sp_call, struct.pack("<I", 4))
mu.reg_write(UC_ARM_REG_SP, sp_call)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, INPUT_BUF)
mu.reg_write(UC_ARM_REG_R1, OUTPUT_BUF)
mu.reg_write(UC_ARM_REG_R2, INPUT_SIZE_ADDR)
mu.reg_write(UC_ARM_REG_R3, OUTPUT_SIZE_ADDR)

print("\n=== Calling s3eCompressionDecomp with DZ coder pointers set ===")
try:
    mu.emu_start(0x51c1c, RET_ADDR, timeout=60*1000000, count=10000000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"Returned: r0 = 0x{r0:x} ({r0})")
    out_data = bytes(mu.mem_read(OUTPUT_BUF, 760))
    nonzero = sum(1 for b in out_data if b != 0)
    print(f"Output: {nonzero}/760 non-zero bytes")
    if nonzero > 10:
        print(f"First 300: {out_data[:300]!r}")
        os.makedirs("/home/z/my-project/work/dz_extracted", exist_ok=True)
        with open("/home/z/my-project/work/dz_extracted/files_list.xml", "wb") as f:
            f.write(out_data)
        print("SAVED!!! DZ DECOMPRESSION WORKS!")
    else:
        in_size = struct.unpack("<I", mu.mem_read(INPUT_SIZE_ADDR, 4))[0]
        out_size = struct.unpack("<I", mu.mem_read(OUTPUT_SIZE_ADDR, 4))[0]
        print(f"  input_size: {in_size}, output_size: {out_size}")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"Error: {e} at PC=0x{pc:x}")
    for i in range(8):
        print(f"  r{i} = 0x{mu.reg_read(UC_ARM_REG_R0 + i):x}")

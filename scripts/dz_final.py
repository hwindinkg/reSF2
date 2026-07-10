import struct, os, sys
from unicorn import *
from unicorn.arm_const import *

SO_PATH = "/home/z/my-project/work/apk_extracted/apktool/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

# Parse segments
e_phoff = struct.unpack_from("<I", so_data, 0x1c)[0]
e_phentsize = struct.unpack_from("<H", so_data, 0x2a)[0]
e_phnum = struct.unpack_from("<H", so_data, 0x2c)[0]
segments = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p = struct.unpack_from("<IIIIIIII", so_data, off)
    if p[0] == 1: segments.append((p[2], p[1], p[4], p[5]))

# Parse sections
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

# Create emulator
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(0, 0x8000000)
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

# Set DZ init function pointer in all 4 slots
# Also set compression type = 4
for i in range(4):
    base = 0xc8514 + i * 0x88
    mu.mem_write(base + 0x64, struct.pack("<I", 0x000b3378))  # init func
    mu.mem_write(base + 0x78, struct.pack("<I", 4))           # type = DZ

# Set up compressed data
DZ_PATH = "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"
with open(DZ_PATH, "rb") as f:
    dz_data = f.read()
comp_data = dz_data[0x1b9a + 4971 : 0x1b9a + 4971 + 139]

INPUT_BUF = 0x90000000
mu.mem_write(INPUT_BUF, comp_data)
OUTPUT_BUF = 0x90010000
INPUT_SIZE_ADDR = 0x90110000
OUTPUT_SIZE_ADDR = 0x90110010
mu.mem_write(INPUT_SIZE_ADDR, struct.pack("<I", 139))
mu.mem_write(OUTPUT_SIZE_ADDR, struct.pack("<I", 760))

# Set up global table for output/input pointers
# r3 = global table (computed by s3eCompressionDecomp)
r3_offset = struct.unpack_from("<I", so_data, 0x51e2c)[0]
r3_value = (0x51c88 + r3_offset) & 0xFFFFFFFF
mu.mem_write(r3_value, struct.pack("<II", OUTPUT_BUF, INPUT_BUF))

# Call s3eCompressionDecomp
sp_call = sp - 4
mu.mem_write(sp_call, struct.pack("<I", 4))
mu.reg_write(UC_ARM_REG_SP, sp_call)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, INPUT_BUF)
mu.reg_write(UC_ARM_REG_R1, OUTPUT_BUF)
mu.reg_write(UC_ARM_REG_R2, INPUT_SIZE_ADDR)
mu.reg_write(UC_ARM_REG_R3, OUTPUT_SIZE_ADDR)

print("Calling s3eCompressionDecomp...")
try:
    mu.emu_start(0x51c1c, RET_ADDR, timeout=120*1000000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"Returned: r0 = 0x{r0:x}")
    out_data = bytes(mu.mem_read(OUTPUT_BUF, 760))
    nonzero = sum(1 for b in out_data if b != 0)
    print(f"Output: {nonzero}/760 non-zero bytes")
    if nonzero > 10:
        print(f"First 300: {out_data[:300]!r}")
        os.makedirs("/home/z/my-project/work/dz_extracted", exist_ok=True)
        with open("/home/z/my-project/work/dz_extracted/files_list.xml", "wb") as f:
            f.write(out_data)
        print("SAVED!")
    else:
        in_size = struct.unpack("<I", mu.mem_read(INPUT_SIZE_ADDR, 4))[0]
        out_size = struct.unpack("<I", mu.mem_read(OUTPUT_SIZE_ADDR, 4))[0]
        print(f"input_size: {in_size}, output_size: {out_size}")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"Error: {e} at PC=0x{pc:x}")
    for i in range(8):
        print(f"  r{i} = 0x{mu.reg_read(UC_ARM_REG_R0 + i):x}")

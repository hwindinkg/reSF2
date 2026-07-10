#!/usr/bin/env python3
"""DZ decompression via ARM emulation — with ELF relocation processing."""
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
e_shoff = struct.unpack_from("<I", so_data, 0x20)[0]
e_shentsize = struct.unpack_from("<H", so_data, 0x2e)[0]
e_shnum = struct.unpack_from("<H", so_data, 0x30)[0]
e_shstrndx = struct.unpack_from("<H", so_data, 0x32)[0]

segments = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
        struct.unpack_from("<IIIIIIII", so_data, off)
    if p_type == 1:
        segments.append((p_vaddr, p_offset, p_filesz, p_memsz))

# Parse section headers to find .rel.dyn and .dynsym
sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = \
        struct.unpack_from("<IIIIIIIIII", so_data, off)
    sections.append({
        'name_idx': sh_name, 'type': sh_type, 'flags': sh_flags,
        'addr': sh_addr, 'offset': sh_offset, 'size': sh_size,
        'link': sh_link, 'info': sh_info, 'entsize': sh_entsize
    })

# Get section name string table
shstrtab = sections[e_shstrndx]
shstrtab_data = so_data[shstrtab['offset']:shstrtab['offset']+shstrtab['size']]

def get_section_name(idx):
    end = shstrtab_data.find(b'\x00', idx)
    return shstrtab_data[idx:end].decode()

# Find .rel.dyn, .dynsym, .dynstr
rel_dyn = None
dynsym = None
dynstr = None
for s in sections:
    name = get_section_name(s['name_idx'])
    if name == '.rel.dyn' or name == '.rel.plt':
        if rel_dyn is None:
            rel_dyn = s
        else:
            # Merge .rel.plt into rel_dyn
            pass
    elif name == '.dynsym':
        dynsym = s
    elif name == '.dynstr':
        dynstr = s

print(f"rel.dyn: offset=0x{rel_dyn['offset']:x} size=0x{rel_dyn['size']:x}" if rel_dyn else "No .rel.dyn")
print(f"dynsym: offset=0x{dynsym['offset']:x} size=0x{dynsym['size']:x}" if dynsym else "No .dynsym")

# Read dynsym entries
dynstr_data = so_data[dynstr['offset']:dynstr['offset']+dynstr['size']]
symbols = []
for i in range(dynsym['size'] // dynsym['entsize']):
    off = dynsym['offset'] + i * dynsym['entsize']
    st_name, st_value, st_size, st_info, st_other, st_shndx = \
        struct.unpack_from("<IIIBBH", so_data, off)
    end = dynstr_data.find(b'\x00', st_name)
    name = dynstr_data[st_name:end].decode()
    symbols.append({'name': name, 'value': st_value, 'size': st_size, 'shndx': st_shndx})

# Create Unicorn
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(0, 0x8000000)  # 128 MB

for vaddr, offset, filesz, memsz in segments:
    mu.mem_write(vaddr, so_data[offset:offset + filesz])

# Apply relocations
# ARM relocation types:
# R_ARM_RELATIVE = 23: *(addr) += base
# R_ARM_ABS32 = 2: *(addr) = S + A (symbol value + addend)
# R_ARM_GLOB_DAT = 21: same as ABS32
# R_ARM_JUMP_SLOT = 22: PLT entry

R_ARM_ABS32 = 2
R_ARM_GLOB_DAT = 21
R_ARM_JUMP_SLOT = 22
R_ARM_RELATIVE = 23

reloc_count = 0
for s in [rel_dyn]:
    if s is None: continue
    for i in range(s['size'] // 8):  # Each reloc is 8 bytes
        off = s['offset'] + i * 8
        r_offset, r_info = struct.unpack_from("<II", so_data, off)
        r_type = r_info & 0xFF
        r_sym = r_info >> 8
        
        if r_type == R_ARM_RELATIVE:
            # *(addr) += base (base = 0 in our case, so no change needed)
            # But actually the value at r_offset IS the addend
            # For RELATIVE: new_value = base + old_value
            # Since base = 0, new_value = old_value (no change)
            pass
        elif r_type in (R_ARM_ABS32, R_ARM_GLOB_DAT, R_ARM_JUMP_SLOT):
            if r_sym < len(symbols):
                sym = symbols[r_sym]
                # Write symbol value at r_offset
                if sym['value'] != 0 or sym['shndx'] != 0:
                    mu.mem_write(r_offset, struct.pack("<I", sym['value']))
                    reloc_count += 1
        reloc_count += 1

print(f"Processed {reloc_count} relocations")

# Stack + heap
mu.mem_map(0x80000000, 0x100000)
sp = 0x80000000 + 0x100000 - 0x1000
mu.mem_map(0x90000000, 0x4000000)
heap_ptr = 0x90000000

def hook_mem_invalid(uc, access, address, size, value, user_data):
    access_type = {1: "READ", 2: "WRITE", 3: "FETCH"}.get(access, "UNKNOWN")
    pc = uc.reg_read(UC_ARM_REG_PC)
    page = address & ~0xFFF
    try:
        uc.mem_map(page, 0x1000)
        return True
    except:
        pass
    return False

mu.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_READ_UNMAPPED, hook_mem_invalid)

RET_ADDR = 0xDEAD0000
mu.mem_map(0xDEAD0000 & ~0xFFF, 0x1000)
mu.mem_write(RET_ADDR, b'\xfe\xde\xff\xe7')

# Read compressed data
DZ_PATH = "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"
with open(DZ_PATH, "rb") as f:
    dz_data = f.read()

comp_offset = 0x1b9a + 4971
comp_data = dz_data[comp_offset:comp_offset + 139]

INPUT_BUF = heap_ptr; heap_ptr += 0x10000
mu.mem_write(INPUT_BUF, comp_data)
OUTPUT_BUF = heap_ptr; heap_ptr += 0x100000
INPUT_SIZE_ADDR = heap_ptr; heap_ptr += 16
OUTPUT_SIZE_ADDR = heap_ptr; heap_ptr += 16
mu.mem_write(INPUT_SIZE_ADDR, struct.pack("<I", 139))
mu.mem_write(OUTPUT_SIZE_ADDR, struct.pack("<I", 760))

sp_call = sp - 4
mu.mem_write(sp_call, struct.pack("<I", 4))

mu.reg_write(UC_ARM_REG_SP, sp_call)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, INPUT_BUF)
mu.reg_write(UC_ARM_REG_R1, OUTPUT_BUF)
mu.reg_write(UC_ARM_REG_R2, INPUT_SIZE_ADDR)
mu.reg_write(UC_ARM_REG_R3, OUTPUT_SIZE_ADDR)

print(f"\nCalling s3eCompressionDecomp(type=4)")

try:
    mu.emu_start(0x51c1c, RET_ADDR, timeout=60*1000000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"Returned: r0 = 0x{r0:x}")
    out_data = bytes(mu.mem_read(OUTPUT_BUF, 760))
    nonzero = sum(1 for b in out_data if b != 0)
    print(f"Output: {nonzero}/760 non-zero bytes")
    if nonzero > 0:
        print(f"  First 300: {out_data[:300]!r}")
        with open("/home/z/my-project/work/dz_extracted/file0_test.bin", "wb") as f:
            f.write(out_data[:760])
        print("  SAVED!")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"Error: {e} at PC=0x{pc:x}")
    r0 = mu.reg_read(UC_ARM_REG_R0)
    r1 = mu.reg_read(UC_ARM_REG_R1)
    r2 = mu.reg_read(UC_ARM_REG_R2)
    print(f"  r0=0x{r0:x} r1=0x{r1:x} r2=0x{r2:x}")

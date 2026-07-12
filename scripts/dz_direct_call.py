#!/usr/bin/env python3
"""DZ decompression via direct call to decode function at 0x389f8.

Strategy: bypass s3eCompressionDecomp and the coder init entirely.
Set up the context structure manually based on disassembly analysis,
then call the decode function directly.

Context structure layout (from disassembly of 0x389f8):
  sb+0x00 = linked list ptr (set to 0)
  sb+0x04 = ? (set to 0)
  sb+0x10 = bit context table ptr (allocated)
  sb+0x14 = input buffer ptr
  sb+0x18 = output position (set to 0)
  sb+0x1c = range (init to -1 = 0xFFFFFFFF)
  sb+0x20 = code (init from window[1..4])
  sb+0x24 = bytes consumed from input
  sb+0x28 = input size
  sb+0x2c = input buffer ptr (copy)
  sb+0x30 = ? (set to 0)
  sb+0x34 = ? (set to 0)
  sb+0x38 = ? (set to 1)
  sb+0x3c = ? (set to 1)
  sb+0x40 = ? (set to 1)
  sb+0x44 = ? (set to 1)
  sb+0x48 = counter (init to 0x112)
  sb+0x4c = flag (init to 0)
  sb+0x50 = ? (set to 0)
  sb+0x58 = window position (init to 0)
  sb+0x5c..0x60 = 5-byte context window
  sb+0x84 = error flag (set to 0)
"""
import struct, os, sys
from unicorn import *
from unicorn.arm_const import *
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

SO_PATH = "/home/z/my-project/work/sf2_data/sf2/lib/armeabi-v7a/libs3e_android.so"
with open(SO_PATH, "rb") as f:
    so_data = f.read()

# Parse ELF segments
e_phoff = struct.unpack_from("<I", so_data, 0x1c)[0]
e_phentsize = struct.unpack_from("<H", so_data, 0x2a)[0]
e_phnum = struct.unpack_from("<H", so_data, 0x2c)[0]
segments = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p = struct.unpack_from("<IIIIIIII", so_data, off)
    if p[0] == 1: segments.append((p[2], p[1], p[4], p[5]))

# Parse sections for relocations
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
mu.mem_map(0, 0x10000000)  # 256 MB
for vaddr, offset, filesz, memsz in segments:
    mu.mem_write(vaddr, so_data[offset:offset + filesz])
# Apply relocations
for i in range(rel_dyn[5] // 8):
    off = rel_dyn[4] + i * 8
    r_offset, r_info = struct.unpack_from("<II", so_data, off)
    r_type = r_info & 0xFF; r_sym = r_info >> 8
    if r_type in (2, 21, 22) and r_sym < len(symbols):
        sym = symbols[r_sym]
        if sym['value'] != 0 or sym['shndx'] != 0:
            mu.mem_write(r_offset, struct.pack("<I", sym['value']))
# PLT stubs
STUB_BASE = 0x70000000
mu.mem_map(STUB_BASE, 0x10000)
stub_ptr = STUB_BASE
STUB_CODE = b'\x00\x00\xa0\xe3\x1e\xff\x2f\xe1'  # mov r0, #0; bx lr
for i in range(rel_plt[5] // 8):
    off = rel_plt[4] + i * 8
    r_offset, r_info = struct.unpack_from("<II", so_data, off)
    mu.mem_write(stub_ptr, STUB_CODE)
    mu.mem_write(r_offset, struct.pack("<I", stub_ptr))
    stub_ptr += 16

# Stack and heap
mu.mem_map(0x80000000, 0x100000)
sp = 0x80000000 + 0x100000 - 0x1000
mu.mem_map(0x90000000, 0x4000000)

def hook_mem(uc, access, address, size, value, user_data):
    page = address & ~0xFFF
    try: uc.mem_map(page, 0x1000); return True
    except: return False
mu.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_READ_UNMAPPED, hook_mem)

RET_ADDR = 0xDEAD0000
mu.mem_map(RET_ADDR & ~0xFFF, 0x1000)
mu.mem_write(RET_ADDR, b'\xfe\xde\xff\xe7')  # undefined instruction (stops emulation)

# Read compressed data from files.dz
DZ_PATH = "/home/z/my-project/work/sf2_data/sf2/assets/assets/files.dz"
with open(DZ_PATH, "rb") as f:
    dz_data = f.read()

# Parse the DZ archive to find the first file
# Header: DTRZ + u16 num_files + u16 num_dirs + u8 version
assert dz_data[:4] == b'DTRZ'
num_files = struct.unpack_from('<H', dz_data, 4)[0]
num_dirs = struct.unpack_from('<H', dz_data, 6)[0]
pos = 9
filenames = []
for _ in range(num_files):
    end = dz_data.index(b'\x00', pos)
    filenames.append(dz_data[pos:end].decode())
    pos = end + 1
folders = [""]
for _ in range(max(0, num_dirs - 1)):
    end = dz_data.index(b'\x00', pos)
    folders.append(dz_data[pos:end].decode())
    pos = end + 1
pos += num_files * 6  # skip attributes
pos += 4  # skip lengths header
files = []
for i in range(num_files):
    offset, uncomp_size, comp_size, comp_type = struct.unpack_from('<IIII', dz_data, pos)
    files.append({'name': filenames[i], 'offset': offset, 'uncomp_size': uncomp_size,
                  'comp_size': comp_size, 'type': comp_type})
    pos += 16

# Find data section start
data_start = pos
print(f"Archive: {num_files} files, data starts at offset {data_start} (0x{data_start:x})")
print(f"First 5 files:")
for f in files[:5]:
    print(f"  {f['name']:40s} off={f['offset']:6d} comp={f['comp_size']:6d} uncomp={f['uncomp_size']:6d} type={f['type']}")

# Try to decompress the first file
f = files[0]
print(f"\nDecompressing: {f['name']} (comp={f['comp_size']}, uncomp={f['uncomp_size']})")

# The offsets in the file table are relative to the data section start
comp_data = dz_data[data_start + f['offset'] : data_start + f['offset'] + f['comp_size']]
print(f"Compressed data ({len(comp_data)} bytes): {comp_data[:32].hex()}")

# Set up compressed data in emulator memory
INPUT_BUF = 0x91000000
mu.mem_write(INPUT_BUF, comp_data)

# Set up output buffer
OUTPUT_BUF = 0x92000000
mu.mem_write(OUTPUT_BUF, b'\x00' * f['uncomp_size'])

# Set up the context structure manually
# The context is ~0x88 bytes (based on field accesses up to sb+0x84)
CONTEXT = 0x93000000
mu.mem_write(CONTEXT, b'\x00' * 0x100)

# Set up parameter addresses
INPUT_SIZE_ADDR = 0x94000000
mu.mem_map(INPUT_SIZE_ADDR, 0x1000)  # ensure mapped
OUTPUT_SIZE_ADDR = 0x94000010
FLAG_ADDR = 0x94000020
WRITTEN_ADDR = 0x94000030

# Initialize context fields based on disassembly analysis:
# sb+0x14 = input buffer pointer
mu.mem_write(CONTEXT + 0x14, struct.pack("<I", INPUT_BUF))
# sb+0x1c = range = 0xFFFFFFFF
mu.mem_write(CONTEXT + 0x1c, struct.pack("<I", 0xFFFFFFFF))
# sb+0x20 = code (will be set from window below)
# sb+0x24 = 0 (bytes consumed)
# sb+0x28 = input size
mu.mem_write(CONTEXT + 0x28, struct.pack("<I", len(comp_data)))
# sb+0x2c = input buffer ptr (copy)
mu.mem_write(CONTEXT + 0x2c, struct.pack("<I", INPUT_BUF))
# sb+0x48 = 0x112 (counter)
mu.mem_write(CONTEXT + 0x48, struct.pack("<I", 0x112))

# Initialize the 5-byte window with the first 5 bytes of compressed data
# (The range coder reads the code from the window)
window = comp_data[:5]
mu.mem_write(CONTEXT + 0x5c, window)
# Set code = window[1]<<24 | window[2]<<16 | window[3]<<8 | window[4]
code = (window[1] << 24) | (window[2] << 16) | (window[3] << 8) | window[4]
mu.mem_write(CONTEXT + 0x20, struct.pack("<I", code))
# Advance input pointer past the 5 window bytes
mu.mem_write(CONTEXT + 0x14, struct.pack("<I", INPUT_BUF + 5))
mu.mem_write(CONTEXT + 0x24, struct.pack("<I", 5))

# Set up stack for the call to 0x389f8
# Args: r0=context, r1=output, r2=out_size_ptr, r3=in_size_ptr
# [sp, #0x50] = in_size_ptr (another)
# [sp, #0x54] = flag ptr
# [sp, #0x58] = out_written_ptr
INPUT_SIZE_ADDR = 0x94000000
OUTPUT_SIZE_ADDR = 0x94000010
FLAG_ADDR = 0x94000020
WRITTEN_ADDR = 0x94000030

remaining_in = len(comp_data) - 5
mu.mem_write(INPUT_SIZE_ADDR, struct.pack("<I", remaining_in))
mu.mem_write(OUTPUT_SIZE_ADDR, struct.pack("<I", f['uncomp_size']))
mu.mem_write(FLAG_ADDR, struct.pack("<I", 0))
mu.mem_write(WRITTEN_ADDR, struct.pack("<I", 0))

# Set up stack: the function reads [sp, #0x50], [sp, #0x54], [sp, #0x58]
# These are 5th, 6th, 7th args (beyond r0-r3)
# Push them onto the stack
sp_call = sp - 0x60  # leave room for stack frame
# Stack layout (from disassembly):
# [sp, #0x50] = INPUT_SIZE_ADDR (5th arg)
# [sp, #0x54] = FLAG_ADDR (6th arg)
# [sp, #0x58] = WRITTEN_ADDR (7th arg)
mu.mem_write(sp_call + 0x50, struct.pack("<I", INPUT_SIZE_ADDR))
mu.mem_write(sp_call + 0x54, struct.pack("<I", FLAG_ADDR))
mu.mem_write(sp_call + 0x58, struct.pack("<I", WRITTEN_ADDR))

mu.reg_write(UC_ARM_REG_SP, sp_call)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, CONTEXT)
mu.reg_write(UC_ARM_REG_R1, OUTPUT_BUF)
mu.reg_write(UC_ARM_REG_R2, OUTPUT_SIZE_ADDR)
mu.reg_write(UC_ARM_REG_R3, INPUT_SIZE_ADDR)

print(f"\nCalling DZ decode function at 0x389f8...")
print(f"  context=0x{CONTEXT:x}, output=0x{OUTPUT_BUF:x}")
print(f"  input_size={remaining_in}, output_size={f['uncomp_size']}")
print(f"  window={window.hex()}, code=0x{code:08x}")

try:
    mu.emu_start(0x389f8, RET_ADDR, timeout=60*1000000, count=10000000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"\nReturned: r0 = {r0}")
    written = struct.unpack("<I", mu.mem_read(WRITTEN_ADDR, 4))[0]
    print(f"Bytes written: {written}")
    out_data = bytes(mu.mem_read(OUTPUT_BUF, f['uncomp_size']))
    nonzero = sum(1 for b in out_data if b != 0)
    print(f"Non-zero bytes: {nonzero}/{f['uncomp_size']}")
    if nonzero > 10:
        print(f"First 200 bytes: {out_data[:200]!r}")
        os.makedirs("/home/z/my-project/work/dz_extracted", exist_ok=True)
        out_path = f"/home/z/my-project/work/dz_extracted/{f['name']}"
        with open(out_path, "wb") as fout:
            fout.write(out_data)
        print(f"SAVED to {out_path}")
    else:
        in_remaining = struct.unpack("<I", mu.mem_read(INPUT_SIZE_ADDR, 4))[0]
        print(f"Input remaining: {in_remaining}")
        # Check context state
        range_val = struct.unpack("<I", mu.mem_read(CONTEXT + 0x1c, 4))[0]
        code_val = struct.unpack("<I", mu.mem_read(CONTEXT + 0x20, 4))[0]
        window_val = bytes(mu.mem_read(CONTEXT + 0x5c, 5))
        print(f"Range: 0x{range_val:08x}, Code: 0x{code_val:08x}")
        print(f"Window: {window_val.hex()}")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"Error: {e} at PC=0x{pc:x}")
    for i in range(13):
        print(f"  r{i} = 0x{mu.reg_read(UC_ARM_REG_R0 + i):x}")

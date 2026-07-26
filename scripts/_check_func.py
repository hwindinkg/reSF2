from capstone import *
import struct

with open('reverse/binaries/libs3e_android.so','rb') as f: so = f.read()
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)

# Check what's at 0x96a64 (called from 0x5167c)
code = so[0x96a64:0x96a64+20]
print('=== At 0x96a64 ===')
for i in md.disasm(code, 0x96a64):
    print(f'  0x{i.address:05x}: {i.mnemonic} {i.op_str}')

print(f'\nFirst 16 bytes: {code[:16].hex()}')

# Check what section
e_shoff = struct.unpack_from('<I', so, 0x20)[0]
e_shentsize = struct.unpack_from('<H', so, 0x2e)[0]
e_shnum = struct.unpack_from('<H', so, 0x30)[0]
e_shstrndx = struct.unpack_from('<H', so, 0x32)[0]
shstrtab_off = struct.unpack_from('<I', so, e_shoff + e_shstrndx * e_shentsize + 16)[0]

print()
for i in range(e_shnum):
    sh_name = struct.unpack_from('<I', so, e_shoff + i * e_shentsize)[0]
    vals = struct.unpack_from('<IIIIIIIIII', so, e_shoff + i * e_shentsize)
    name = so[shstrtab_off + sh_name:].split(b'\x00')[0].decode()
    sh_addr, sh_size = vals[3], vals[5]
    if sh_addr <= 0x96a64 < sh_addr + sh_size:
        print(f'At 0x96a64: section "{name}" addr=0x{sh_addr:x} sz={sh_size}')

# Also check ALL the PLT entries and what they point to
# Find memset's PLT entry and GOT entry
print('\n=== PLT entries near 0xc444 ===')
for i in range(148):
    r_off, r_info = struct.unpack_from("<II", so, 0xbe50 + i * 8)
    sym_idx = r_info >> 8
    st_name = struct.unpack_from("<I", so, 0x1470 + sym_idx * 16)[0]
    end = st_name
    while so[0x41b0+end] != 0: end += 1
    name = so[0x41b0+st_name:0x41b0+end].decode('latin-1')
    plt_addr = 0xc2f0 + 16 + i * 12
    if plt_addr <= 0xc44c < plt_addr + 12:
        print(f'  PLT 0x{plt_addr:05x}-0x{plt_addr+11:x}: {name} (reloc {i})')
        print(f'  GOT entry at 0x{r_off:05x}')
        got_val = struct.unpack_from('<I', so, r_off)[0]
        print(f'  GOT value: 0x{got_val:08x}')

# Is the PLT code mixed with Thumb?
print('\n=== Check if 0xc444 is Thumb or ARM ===')
# Check if there's any Thumb code near 0x96a64  
print('\n=== Check 0x96a64 context ===')
md2 = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
code = so[0x96a64:0x96a64+20]
for i in md2.disasm(code, 0x96a64 | 1):
    print(f'  Thumb 0x{i.address:x}: {i.mnemonic} {i.op_str}')

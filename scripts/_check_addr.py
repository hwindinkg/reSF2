import struct

with open('reverse/binaries/libs3e_android.so','rb') as f: so = f.read()

e_phoff = struct.unpack_from('<I', so, 0x1c)[0]
e_shoff = struct.unpack_from('<I', so, 0x20)[0]
e_shentsize = struct.unpack_from('<H', so, 0x2e)[0]
e_shnum = struct.unpack_from('<H', so, 0x30)[0]
e_shstrndx = struct.unpack_from('<H', so, 0x32)[0]

shstrtab_off = struct.unpack_from('<I', so, e_shoff + e_shstrndx * e_shentsize + 16)[0]

print("Section check:")
for i in range(e_shnum):
    sh_name = struct.unpack_from('<I', so, e_shoff + i * e_shentsize)[0]
    vals = struct.unpack_from('<IIIIIIIIII', so, e_shoff + i * e_shentsize)
    name = so[shstrtab_off + sh_name:].split(b'\x00')[0].decode()
    sh_addr, sh_offset, sh_size = vals[3], vals[4], vals[5]
    if sh_addr <= 0xc44c < sh_addr + sh_size:
        print(f'  VA 0xc44c in "{name}" addr=0x{sh_addr:x} off=0x{sh_offset:x} sz={sh_size}')
        data = so[sh_offset + (0xc44c - sh_addr):sh_offset + (0xc44c - sh_addr) + 16]
        print(f'  bytes: {data.hex()}')

# Get .dynsym and .dynstr
print("\nPLT/GOT structure:")
for i in range(e_shnum):
    vals = struct.unpack_from('<IIIIIIIIII', so, e_shoff + i * e_shentsize)
    sh_name = vals[0]
    name = so[shstrtab_off + sh_name:].split(b'\x00')[0].decode()
    if 'plt' in name.lower() or 'got' in name.lower():
        print(f'  {name}: addr=0x{vals[3]:x} off=0x{vals[4]:x} sz={vals[5]}')

# Check what's at 0xc44c more carefully
# The data segment PH2 maps file region to VA region
print("\nPH2 details:")
for i in range(6):
    p = struct.unpack_from("<IIIIIIII", so, 0x34 + i * 32)
    if p[0] == 1 and p[2] <= 0xc44c < p[2] + p[5]:
        print(f'  PT_LOAD: vaddr=0x{p[2]:x} offset=0x{p[1]:x} filesz={p[4]} memsz={p[5]}')
        file_off = p[1] + (0xc44c - p[2])
        print(f'  File offset for 0xc44c: 0x{file_off:x}')
        data = so[file_off:file_off+4]
        print(f'  Data: {data.hex()}')

# Check dynamic symbol table
print("\nDynamic symbols:")
for i in range(e_shnum):
    vals = struct.unpack_from('<IIIIIIIIII', so, e_shoff + i * e_shentsize)
    if vals[1] == 11:  # SHT_DYNSYM
        dynsym_off = vals[4]
        dynsym_sz = vals[5]
        # find dynstr
        strtab_link = vals[6]
        strtab_vals = struct.unpack_from('<IIIIIIIIII', so, e_shoff + strtab_link * e_shentsize)
        dynstr_off = strtab_vals[4]
        print(f'  dynsym @0x{dynsym_off:x} sz={dynsym_sz}')
        print(f'  dynstr @0x{dynstr_off:x}')
        
        for j in range(dynsym_sz // 16):
            st_name, st_value, st_size, st_info, st_other, st_shndx = \
                struct.unpack_from('<IIIBBH', so, dynsym_off + j * 16)
            if st_value and st_value <= 0xc44c < st_value + st_size:
                name = so[dynstr_off + st_name:].split(b'\x00')[0].decode()
                print(f'    -> {name} value=0x{st_value:x} sz={st_size}')
        
        # Just find any sym near 0xc44c
        for j in range(dynsym_sz // 16):
            st_name, st_value, st_size, st_info, st_other, st_shndx = \
                struct.unpack_from('<IIIBBH', so, dynsym_off + j * 16)
            name = so[dynstr_off + st_name:].split(b'\x00')[0].decode()
            if abs(st_value - 0xc44c) < 4096 and st_value:
                print(f'  Near: {name} @0x{st_value:x} sz={st_size}')
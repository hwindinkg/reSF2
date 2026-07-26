import struct

with open('reverse/binaries/libs3e_android.so','rb') as f: so = f.read()

dynstr = so[0x41b0:0x41b0+0x3b28]

# Map PLT addresses to symbol names
plt_by_name = {}
for i in range(148):
    r_off, r_info = struct.unpack_from("<II", so, 0xbe50 + i * 8)
    sym_idx = r_info >> 8
    st_name = struct.unpack_from("<I", so, 0x1470 + sym_idx * 16)[0]
    end = st_name
    while dynstr[end] != 0: end += 1
    name = dynstr[st_name:end].decode("latin-1", errors="replace")
    plt_addr = 0xc2f0 + 16 + i * 12
    plt_by_name[plt_addr] = name

# Check entry 27 (0xc444)
for addr in sorted(plt_by_name.keys()):
    if 0xc440 <= addr <= 0xc450:
        print(f'  PLT 0x{addr:05x}: {plt_by_name[addr]}')

# What does the original code at this PLT entry look like?
# Read the bytes at 0xc444
# File offset = VA (since PH1 maps 1:1)
print(f'\nOriginal PLT entry 27 bytes at 0xc444:')
data = so[0xc444:0xc444+12]
for i in range(0, 12, 4):
    instr = struct.unpack('<I', data[i:i+4])[0]
    print(f'  +{i}: 0x{instr:08x}')

print(f'\nWhat was at PLT header (0xc2f0):')
data = so[0xc2f0:0xc300]
for i in range(0, 16, 4):
    instr = struct.unpack('<I', data[i:i+4])[0]
    print(f'  +{i}: 0x{instr:08x}')

import struct
with open('E:/reSF2/assets/files.dz', 'rb') as f:
    dz = f.read()
# forge.xml entry: off=29237 comp=6954
comp = dz[29237:29237+6954]
print(f'Full compressed data ({len(comp)} bytes):')
print(f'  First 32 hex: {comp[:32].hex()}')
header = comp[:13]
print(f'Header (13 bytes): {header.hex()}')
stream = comp[13:]
print(f'Compressed stream ({len(stream)} bytes):')
print(f'  First 16 hex: {stream[:16].hex()}')
if len(stream) >= 4:
    rc_init = struct.unpack_from('<I', stream, 0)[0]
    print(f'  Range coder initial value: 0x{rc_init:08x}')
    print(f'  Valid: code=0x{rc_init:08x} < 0xFFFFFFFF => {rc_init < 0xFFFFFFFF}')

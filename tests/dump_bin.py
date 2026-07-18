import struct, zlib

with open('assets/animations.dz', 'rb') as f:
    data = f.read()

num_files = struct.unpack_from('<H', data, 4)[0]
num_dirs = struct.unpack_from('<H', data, 6)[0]
print(f'DTRZ: {num_files} files, {num_dirs} dirs')

pos = 9
filenames = []
for i in range(num_files):
    end = data.index(0, pos)
    filenames.append(data[pos:end].decode('utf-8', errors='replace'))
    pos = end + 1

folders = ['']
for i in range(num_dirs - 1):
    end = data.index(0, pos)
    folders.append(data[pos:end].decode('utf-8', errors='replace'))
    pos = end + 1

pos += num_files * 6 + 4

table = {}
for i in range(num_files):
    off_raw = struct.unpack_from('<I', data, pos)[0]
    len0_raw = struct.unpack_from('<I', data, pos+4)[0]
    len1_raw = struct.unpack_from('<I', data, pos+8)[0]
    typ_raw = struct.unpack_from('<I', data, pos+12)[0]
    table[filenames[i]] = {
        'offset': off_raw & 0xFFFFFF,
        'uncomp_size': len0_raw & 0xFFFFFF,
        'comp_size': len1_raw & 0xFFFFFF,
        'type': typ_raw & 0xFF,
    }
    pos += 16

def decompress(name):
    te = table[name]
    raw = data[te['offset']:te['offset'] + te['comp_size']]
    if raw[:2] != b'\x1f\x8b':
        return None
    dec_obj = zlib.decompressobj(-zlib.MAX_WBITS)
    d = dec_obj.decompress(raw[10:])
    d += dec_obj.flush()
    return d

# Analyze animation files
bin_names = [n for n in table if n.endswith('.bin')]
print(f'\n=== Analyzing first 10 .bin files ===\n')

for name in bin_names[:10]:
    dec = decompress(name)
    if not dec or len(dec) < 8:
        continue
    
    fc = struct.unpack_from('<I', dec, 0)[0]  # first_u32 = frame count?
    sz = len(dec)
    body = sz - 4
    
    # Try 202 floats per frame
    flt_body = body // 4
    extra = body % 4
    
    print(f'{name}:')
    print(f'  size={sz} first_u32={fc}')
    print(f'  body={body} bytes = {flt_body} floats (+{extra} extra bytes)')
    
    if fc > 0 and flt_body % fc == 0:
        fpf = flt_body // fc
        print(f'  -> {fc} frames x {fpf} floats per frame')
    
    if fc > 0 and flt_body % 202 == 0:
        fcount = flt_body // 202
        print(f'  -> 202 floats per frame, {fcount} frames (fc={fc})')
    
    # Try first_u32 as something else
    # Maybe it's a version number? Let's check the bytes
    fc_bytes = struct.pack('<I', fc)
    print(f'  first_u32 bytes: {fc_bytes.hex()}')
    
    # Check: what if format is: u16 frames, u16 nodes?
    fc_16 = struct.unpack_from('<H', dec, 0)[0]
    nc_16 = struct.unpack_from('<H', dec, 2)[0]
    print(f'  as u16x2: frames={fc_16} nodes={nc_16}')
    
    # If fc_16 = frames and nc_16 = nodes:
    # each node has 5 floats per frame (x, y, rot, sx, sy)
    if fc_16 > 0 and nc_16 > 0:
        expected = fc_16 * nc_16 * 5 * 4
        print(f'    expected size for frames*nodes*5floats: {expected + 4}')
        if expected + 4 == sz:
            print(f'    *** MATCH: {fc_16}frames x {nc_16}nodes x 5floats!')
    
    # And 202 = 5 * 40 + 2 — maybe 40 nodes + 2 extra values?
    # 40 nodes * 5 floats = 200, + 2 = 202
    print(f'    202 = 40*5 + 2 (40 nodes + 2 extra floats)')
    if fc_16 > 0 and nc_16 != 40:
        print(f'    (but node_count={nc_16}, not 40)')
    
    # Print the raw bytes starting at offset 4
    print(f'  bytes 4-35: {dec[4:36].hex()}')
    
    # Print as floats from offset 4:
    floats_4 = list(struct.unpack_from('<f', dec, 4 + i*4) for i in range(8))
    print(f'  first 8 floats from offset 4:')
    for i, f in enumerate(floats_4):
        print(f'    [{i}] {f}')
    
    # Print as floats from offset 8:
    floats_8 = list(struct.unpack_from('<f', dec, 8 + i*4) for i in range(8))
    print(f'  first 8 floats from offset 8: {floats_8}')
    
    print()

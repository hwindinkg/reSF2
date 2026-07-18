import struct, gzip, sys

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

files = []
for i in range(num_files):
    offset, len0, len1, typ = struct.unpack_from('<IIII', data, pos)
    files.append({'name': filenames[i], 'offset': offset,
                  'uncomp_size': len0, 'comp_type': typ})
    pos += 16

bin_files = [f for f in files if '.bin' in f['name']]
print(f'\n.bin files: {len(bin_files)}')

# Check if the DZ file table has CRC bytes in the high 8 bits
print('\n=== Checking DZ file table CRC bytes ===')
# Find the file table position again
pos2 = 9
for i in range(num_files):
    end = data.index(0, pos2)
    pos2 = end + 1
for i in range(num_dirs - 1):
    end = data.index(0, pos2)
    pos2 = end + 1
pos2 += num_files * 6 + 4

# Read raw file table with CRC breakdown
print('First 5 file table entries (raw u32 + CRC breakdown):')
for i in range(min(5, num_files)):
    off_raw = struct.unpack_from('<I', data, pos2)[0]
    len0_raw = struct.unpack_from('<I', data, pos2+4)[0]
    len1_raw = struct.unpack_from('<I', data, pos2+8)[0]
    typ_raw = struct.unpack_from('<I', data, pos2+12)[0]
    
    # Try as u24 + u8 CRC
    off_val = off_raw & 0xFFFFFF
    off_crc = (off_raw >> 24) & 0xFF
    len_val = len0_raw & 0xFFFFFF
    len_crc = (len0_raw >> 24) & 0xFF
    comp_val = len1_raw & 0xFFFFFF
    comp_crc = (len1_raw >> 24) & 0xFF
    typ_val = typ_raw & 0xFFFFFF
    typ_crc = (typ_raw >> 24) & 0xFF
    
    name = filenames[i] if i < len(filenames) else '?'
    print(f'  [{i}] {name}:')
    print(f'    raw: off=0x{off_raw:x} len0=0x{len0_raw:x} len1=0x{len1_raw:x} type=0x{typ_raw:x}')
    print(f'    u24: off=0x{off_val:x} uncompr={len_val} comp={comp_val} type={typ_val}')
    print(f'    CRC: off=0x{off_crc:x} uncompr=0x{len_crc:x} comp=0x{comp_crc:x} type=0x{typ_crc:x}')
    
    pos2 += 16

# Re-read file table properly indexed
import gzip, io, struct

pos2 = 9
for i in range(num_files):
    end = data.index(0, pos2)
    pos2 = end + 1
for i in range(num_dirs - 1):
    end = data.index(0, pos2)
    pos2 = end + 1
pos2 += num_files * 6 + 4

table_entries = []
for i in range(num_files):
    off_raw = struct.unpack_from('<I', data, pos2)[0]
    len0_raw = struct.unpack_from('<I', data, pos2+4)[0]
    len1_raw = struct.unpack_from('<I', data, pos2+8)[0]
    typ_raw = struct.unpack_from('<I', data, pos2+12)[0]
    table_entries.append({
        'name': filenames[i] if i < len(filenames) else '?',
        'offset': off_raw & 0xFFFFFF,
        'uncomp_size': len0_raw & 0xFFFFFF,
        'comp_size': len1_raw & 0xFFFFFF,
        'type': typ_raw & 0xFF,
    })
    pos2 += 16

# Now index by filename
te_by_name = {te['name']: te for te in table_entries}

# First try XML files (should be text)  
for xml_name in ['animations_list.xml', 'settings.xml']:
    te = te_by_name.get(xml_name)
    if not te:
        continue
    print(f'\n=== XML: {xml_name} ===')
    print(f'offset=0x{te["offset"]:x} uncomp={te["uncomp_size"]} comp={te["comp_size"]}')
    
    raw = data[te['offset']:te['offset'] + te['comp_size']]
    print(f'  First 16 bytes: {raw[:16].hex()}')
    
    # Try gzip
    try:
        dec = gzip.decompress(raw)
        print(f'  GZIP success: {len(dec)} bytes')
        print(f'  Content preview: {dec[:200]}')
    except Exception as e:
        print(f'  GZIP error: {e}')
        # Try as-is (maybe not compressed despite type=8)
        try:
            text = raw.decode('utf-8')
            print(f'  Raw text: {text[:200]}')
        except:
            # Maybe it's XOR obfuscated
            key = 0x1F
            xored = bytes(b ^ key for b in raw[:32])
            print(f'  XOR key=0x1F: {xored.hex()}')

# SUCCESS: gzip data at offset 10 with no trailer!
# Now analyze the actual .bin format
import zlib, struct

def decompress_bin(bf_id):
    bf = bin_files[bf_id]
    te = te_by_name[bf['name']]
    raw = data[te['offset']:te['offset'] + te['comp_size']]
    dec_obj = zlib.decompressobj(-zlib.MAX_WBITS)
    dec = dec_obj.decompress(raw[10:])
    dec += dec_obj.flush()
    return dec, bf['name'], len(dec)

print('\n=== .bin Format Analysis ===')
for idx in range(min(5, len(bin_files))):
    dec, name, sz = decompress_bin(idx)
    if len(dec) < 8:
        continue
    
    first_u32 = struct.unpack_from('<I', dec, 0)[0]
    remaining = len(dec) - 4
    nf = remaining // 4 if remaining % 4 == 0 else 0
    
    print(f'\n[{idx}] {name} ({sz} bytes):')
    print(f'  first_u32={first_u32} (0x{first_u32:x})')
    print(f'  remaining={remaining} bytes = {nf} floats')
    
    # Try frame_count * 202 floats
    if first_u32 > 0 and nf % 202 == 0:
        fc = nf // 202
        print(f'  -> {fc} frames x 202 floats')
    
    # Also try other divisors
    if nf > 0 and first_u32 > 0:
        ratio = nf / first_u32
        print(f'  ratio nf/first_u32 = {ratio:.3f}')
        if ratio == int(ratio):
            print(f'  -> {first_u32} entries x {int(ratio)} floats')
    
    # Hex dump first 80 bytes with float interpretation
    print(f'  Hex dump + float interpretation:')
    for i in range(0, min(80, len(dec)), 16):
        hex_str = ' '.join(f'{b:02x}' for b in dec[i:i+16])
        # Read as 4 floats
        flts = []
        for j in range(0, 16, 4):
            if i + j + 4 > len(dec): break
            f = struct.unpack_from('<f', dec, i + j)[0]
            flts.append(f)
        flt_str = ' '.join(f'{f:12.4f}' if abs(f) < 1e6 else f'{f:12.1e}' for f in flts)
        print(f'    {hex_str}  |{flt_str}|')
    
    # Look at a few specific float positions
    if nf >= 202:
        floats = list(struct.unpack_from(f'<{nf}f', dec, 4))
        print(f'  Sample of every 202nd float: {floats[0::202][:10]}')
    
    first_u32 = struct.unpack_from('<I', dec, 0)[0]
    remaining = len(dec) - 4
    num_floats = remaining // 4 if remaining > 0 and remaining % 4 == 0 else 0
    
    print(f'\n[{idx}] {bf["name"]} ({len(dec)} bytes)')
    print(f'    first_u32={first_u32} (0x{first_u32:x})')
    print(f'    floats after header: {num_floats}')
    
    # Check divisors
    if num_floats > 0:
        for nf in [10, 15, 20, 30, 50, 100, 150, 175, 200, 202, 210, 250, 300]:
            if num_floats % nf == 0:
                print(f'      -> {num_floats // nf} entries x {nf} floats')
    
    # Hex dump first 48 bytes
    for i in range(0, min(48, len(dec)), 16):
        hex_str = ' '.join(f'{b:02x}' for b in dec[i:i+16])
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in dec[i:i+16])
        print(f'    {hex_str}  {ascii_str}')

# Check if first file has name hints about node structure
print('\n\nFirst 20 .bin filenames:')
for f in bin_files[:20]:
    comp = data[f['offset']:min(f['offset']+100, len(data))]
    print(f'  {f["name"]} (uncomp={f["uncomp_size"]})')

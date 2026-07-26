"""Reverse-engineer the DZ archive format."""
import struct

with open(r"E:\reSF2\assets\files.dz", "rb") as f:
    dz = bytearray(f.read())

print("DZ magic:", repr(bytes(dz[:4])))
print("Full DZ size:", len(dz))

# First 32 bytes of header  
print("\nDZ header hex:")
for i in range(0, min(64, len(dz)), 16):
    hex_str = " ".join("%02x" % dz[i+j] for j in range(16))
    ascii_str = "".join(chr(dz[i+j]) if 32 <= dz[i+j] < 127 else "." for j in range(16))
    print("  %04x: %s  %s" % (i, hex_str, ascii_str))

# Try to understand the format
nfiles = struct.unpack_from("<H", dz, 4)[0]
ndirs = struct.unpack_from("<H", dz, 6)[0]
print("\nnfiles=%d ndirs=%d" % (nfiles, ndirs))
print("byte[8]=0x%02x" % dz[8])

# Parse names
pos = 9
print("\n=== File names ===")
names = []
for i in range(nfiles):
    end = dz.index(b"\x00", pos) if b"\x00" in dz[pos:] else len(dz)
    name = dz[pos:end].decode("utf-8", errors="replace")
    names.append(name)
    print("  [%d] %s" % (i, name if len(name)<60 else name[:60]+"..."))
    pos = end + 1

print("\n=== Directory names ===")
for i in range(ndirs):
    end = dz.index(b"\x00", pos) if b"\x00" in dz[pos:] else len(dz)
    dname = dz[pos:end].decode("utf-8", errors="replace")
    print("  [%d] %s" % (i, dname))
    pos = end + 1

# After names, what's next?
print("\n=== After names (raw hex before file table) ===")
print("pos=%d (0x%x)" % (pos, pos))
for i in range(0, min(64, len(dz)-pos), 16):
    off = pos + i
    hex_str = " ".join("%02x" % dz[off+j] for j in range(16))
    ascii_str = "".join(chr(dz[off+j]) if 32 <= dz[off+j] < 127 else "." for j in range(16))
    print("  %04x: %s  %s" % (off, hex_str, ascii_str))

# Try: after names there's a 6-byte-per-file mapping table
print("\n=== Try: 6-byte pre-file table ===")
table_size = nfiles * 6
print("Proposed table size: %d bytes (%d entries x 6)" % (table_size, nfiles))
for i in range(min(5, nfiles)):
    entry = dz[pos + i*6:pos + i*6 + 6]
    val = struct.unpack_from("<H", entry)[0]
    print("  [%d] %s -> %d" % (i, entry.hex(), val))

# Then +4
table_end = pos + table_size + 4
print("\nData start pos: %d (0x%x)" % (table_end, table_end))

# Parse file entries at table_end
print("\n=== File entries (16 bytes each) ===")
for i in range(min(5, nfiles)):
    f0, f1, f2, f3 = struct.unpack_from("<IIII", dz, table_end + i*16)
    print("  [%d] unc_size=%d off=%d comp_size=%d type=%d vars=%d" % (
        i, f0, f1 & 0xFFFFFF, f2 & 0xFFFFFF, (f2 >> 24) & 0xFF, f3
    ))

# Check forge.xml specifically
for i in range(nfiles):
    f0, f1, f2, f3 = struct.unpack_from("<IIII", dz, table_end + i*16)
    if names[i] == "forge.xml":
        offset = f1 & 0xFFFFFF
        comp = f2 & 0xFFFFFF
        unc = f0 & 0xFFFFFF
        typ = (f2 >> 24) & 0xFF
        data = dz[table_end + offset:table_end + offset + comp]
        print("\n=== forge.xml ===")
        print("  offset=%d comp=%d unc=%d type=%d" % (offset, comp, unc, typ))
        print("  data (%d bytes) hex: %s" % (len(data), data[:32].hex()))
        print("  data as ASCII-ish: %s" % repr(bytes(data[:64])))
        # Check if first byte might be BFINAL/BTYPE
        if len(data) > 0:
            b = data[0]
            print("  byte 0: 0x%02x binary=%s" % (b, bin(b)))
            print("    BFINAL=%d BTYPE=%d" % (b & 1, (b >> 1) & 3))
        
        # Try inflate with negative window bits (no header) but different offset
        import zlib
        for jump in range(-4, 8):
            off = jump
            if off < 0:
                continue
            if off >= len(data):
                continue
            try:
                # Maybe there's a small header before raw deflate
                result = zlib.decompress(bytes(data[off:]), -15)
                print("  RAW deflate at offset %d: OK, %d bytes" % (off, len(result)))
                xml_start = result[:min(80, len(result))]
                print("    starts with: %s" % repr(xml_start))
            except Exception as e:
                pass

# Also check all files to find patterns  
print("\n=== All file entries ===")
for i in range(nfiles):
    f0, f1, f2, f3 = struct.unpack_from("<IIII", dz, table_end + i*16)
    offset = f1 & 0xFFFFFF
    comp = f2 & 0xFFFFFF
    unc = f0 & 0xFFFFFF
    typ = (f2 >> 24) & 0xFF
    unc_high = f0 >> 24
    print("  [%3d] %-30s unc=%5d off=%5d comp=%5d type=%d uc_h=%d" % (
        i, names[i][:30], unc, offset, comp, typ, unc_high
    ))

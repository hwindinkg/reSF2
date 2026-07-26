import struct

with open(r"E:\reSF2\assets\files.dz", "rb") as f:
    dz = f.read()

# Find forge.xml's data entry at the raw byte level
# Skip to the file table area
nfiles = struct.unpack_from("<H", dz, 4)[0]
ndirs = struct.unpack_from("<H", dz, 6)[0]

# Parse names to reach the table
pos = 9
names = []
for _ in range(nfiles):
    end = dz.index(b"\x00", pos)
    names.append(dz[pos:end].decode("utf-8", errors="replace"))
    pos = end + 1
for _ in range(ndirs):
    end = dz.index(b"\x00", pos)
    pos = end + 1

print("After dir names, pos=%d (0x%x)" % (pos, pos))

# Raw dump of the table area before file entries
print("\n=== Raw table (first 64 bytes) ===")
for i in range(0, 64, 16):
    hex_str = " ".join("%02x" % dz[pos+i+j] for j in range(16))
    print("  +%03x: %s" % (i, hex_str))

# Try to understand the table: maybe it's nfiles * 4 bytes
table_sizes = [0, 4, 6, 8]
for sz in table_sizes:
    tbl = nfiles * sz
    rest = len(dz) - pos - tbl
    print("\n  If %d bytes/entry: table=%d, rest=%d (includes 4-byte header? %s)" % (sz, tbl, rest, "yes" if rest-4 > 0 else "no"))

# Try 4 bytes/entry (200 = 0xc8 entries)
print("\n=== Try 4-byte table ===")
pos_try = pos
with_extra = 4  # 4 byte header?
start = pos_try + nfiles * 4 + with_extra
for i in range(min(5, nfiles)):
    entry_val = struct.unpack_from("<I", dz, pos_try + i*4)[0]
    print("  [%d] val=%d (0x%x)" % (i, entry_val, entry_val))

print("\n  Data would start at: %d (0x%x)" % (start, start))

# Show hex at that position
print("\n  Raw at data start:")
for i in range(0, min(80, len(dz)-start), 16):
    hex_str = " ".join("%02x" % dz[start+i+j] for j in range(min(16, len(dz)-start-i)))
    print("    +%03x: %s" % (i, hex_str))

# Now try to find forge.xml by looking at the central directory 
# A tar-like approach: scan for file headers
print("\n=== Scanning for potential file entry headers ===")
# Look for patterns like type=4 (0x04) near the data area
forge_idx = names.index("forge.xml")
print("forge.xml is file index %d" % forge_idx)

# The raw entry at the table + forge_idx * entry_size
# Let me try different entry sizes and see which gives reasonable values
for entry_size in [8, 12, 16, 20, 24, 32]:
    print("\n--- Entry size: %d bytes ---" % entry_size)
    remaining = len(dz) - pos
    n_entries = remaining // entry_size
    print("  Could fit %d entries" % n_entries)
    
    if forge_idx < n_entries and forge_idx * entry_size + entry_size <= remaining:
        raw = dz[pos + forge_idx * entry_size: pos + (forge_idx + 1) * entry_size]
        print("  forge entry raw: %s" % raw.hex())
        
        # Try interpreting as various field combos
        print("  As <II:         %s" % str(struct.unpack_from("<II", raw)))
        print("  As <III:        %s" % str(struct.unpack_from("<III", raw)))
        
        if entry_size >= 8:
            a, b = struct.unpack_from("<II", raw)
            if b < len(dz) and b > 0:
                # Could b be an offset?
                print("   offset=%d, look at data there:" % b)
                if b + min(32, len(dz)-b) > b:
                    print("    data: %s" % dz[b:b+min(32, len(dz)-b)].hex())

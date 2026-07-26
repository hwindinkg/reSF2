import struct, zlib, gzip

with open(r"E:\reSF2\assets\files.dz", "rb") as f: dz = f.read()
nfiles = struct.unpack_from("<H", dz, 4)[0]
ndirs = struct.unpack_from("<H", dz, 6)[0]

pos = 9
names = []
for _ in range(nfiles):
    end = dz.index(b"\x00", pos)
    names.append(dz[pos:end].decode("utf-8", errors="replace"))
    pos = end + 1
for _ in range(ndirs):
    pos = dz.index(b"\x00", pos) + 1
pos += nfiles * 6 + 4

files = []
for i in range(nfiles):
    f0, f1, f2, f3 = struct.unpack_from("<IIII", dz, pos)
    files.append({
        "name": names[i],
        "offset": f1 & 0xFFFFFF,
        "comp_size": f2 & 0xFFFFFF,
        "uncomp_size": f0 & 0xFFFFFF,
        "type": (f2 >> 24) & 0xFF
    })
    pos += 16

data_start = pos
forge = next(f for f in files if f["name"] == "forge.xml")
print("forge.xml: comp=%d unc=%d type=%d" % (forge["comp_size"], forge["uncomp_size"], forge["type"]))

comp_data = dz[data_start + forge["offset"]:data_start + forge["offset"] + forge["comp_size"]]
print("Compressed data (%d bytes): %s" % (len(comp_data), comp_data[:16].hex()))

# Try various decompressions
for name, wbits in [("RAW deflate", -15), ("zlib", 15), ("zlib+auto", 15+32)]:
    try:
        result = zlib.decompress(comp_data, wbits)
        print("%s OK: %d bytes" % (name, len(result)))
        xml_start = result[:min(80, len(result))]
        print("  starts with: %s" % repr(xml_start))
    except Exception as e:
        print("%s FAIL: %s" % (name, e))

try:
    result = gzip.decompress(comp_data)
    print("gzip OK: %d bytes" % len(result))
    print("  starts with: %s" % repr(result[:80]))
except Exception as e:
    print("gzip FAIL: %s" % e)

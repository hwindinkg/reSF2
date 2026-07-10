#!/usr/bin/env python3
"""Detailed inspection of DTRZ header to figure out the real layout."""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"

with open(path, "rb") as f:
    data = f.read()

print(f"File size: {len(data)} bytes")
print(f"Magic: {data[:4]!r}")

# Try various interpretations of the header
# Hypothesis 1: 4-byte magic + u16 num_files + u16 num_dirs + u8 version
n1, n2, v = struct.unpack_from("<HHB", data, 4)
print(f"\nHypothesis 1: num_files={n1}, num_dirs={n2}, version={v}")

# Hypothesis 2: 4-byte magic + u32 num + ...
n32 = struct.unpack_from("<I", data, 4)[0]
print(f"Hypothesis 2: u32 at offset 4 = {n32} (0x{n32:x})")
n32b = struct.unpack_from("<I", data, 8)[0]
print(f"             u32 at offset 8 = {n32b} (0x{n32b:x})")

# Dump the first 64 bytes as both hex and ascii
print("\nFirst 64 bytes:")
for i in range(0, 64, 16):
    hex_part = " ".join(f"{b:02x}" for b in data[i:i+16])
    ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in data[i:i+16])
    print(f"  {i:04x}: {hex_part}  {ascii_part}")

# Find where the names section starts. Let's look for the first long run of
# null-terminated strings.
# The names section starts somewhere after the header. Let's find the first
# null byte after "DTRZ".
first_null = data.index(b"\x00", 4)
print(f"\nFirst null byte after magic: at offset {first_null}")
print(f"Bytes 4..{first_null}: {data[4:first_null]!r}")

# Look at the names section. We know "files_list.xml" appears right after the
# header. Let's find where "files_list.xml\0" starts.
marker = b"files_list.xml"
pos = data.find(marker)
print(f"\n'files_list.xml' starts at offset {pos}")
print(f"Bytes before it (header): {data[:pos].hex()}")

# So the header is `data[:pos]`. Let's see what those bytes look like.
header = data[:pos]
print(f"Header length: {len(header)} bytes")
print(f"Header hex: {header.hex()}")

# After "files_list.xml\0" come more null-terminated strings (filenames).
# Let's enumerate them all.
print("\nAll null-terminated strings starting at offset", pos)
end_of_names = pos
count = 0
p = pos
names = []
while p < len(data):
    end = data.find(b"\x00", p)
    if end == -1:
        break
    s = data[p:end]
    if len(s) == 0:
        # Empty string - might be padding or end of names
        p = end + 1
        continue
    # Check if it looks like a filename (printable ASCII)
    try:
        name = s.decode("utf-8")
        if all(c.isprintable() or c in "/\\" for c in name):
            names.append((p, name))
            count += 1
            if count <= 5 or count > 220 and count <= 230:
                print(f"  [{count:3d}] @{p:06x}: {name!r}")
        else:
            # Non-printable - probably end of names section
            print(f"  [end] @{p:06x}: non-printable, first bytes {s[:16].hex()}")
            end_of_names = p
            break
    except:
        print(f"  [end] @{p:06x}: decode failed, first bytes {s[:16].hex()}")
        end_of_names = p
        break
    p = end + 1

print(f"\nTotal names found: {len(names)}")
if names:
    print(f"Names section: offset {names[0][0]} to {end_of_names}")
    print(f"Last name: {names[-1]!r}")

# What comes right after the names section?
print(f"\nBytes after names (first 64 bytes at offset {end_of_names}):")
for i in range(0, 64, 16):
    off = end_of_names + i
    if off + 16 > len(data):
        break
    hex_part = " ".join(f"{b:02x}" for b in data[off:off+16])
    ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in data[off:off+16])
    print(f"  {off:06x}: {hex_part}  {ascii_part}")

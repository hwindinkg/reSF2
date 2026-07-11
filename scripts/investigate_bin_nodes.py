#!/usr/bin/env python3
"""Investigate the weird nodes 19-26 in the bin file."""
import struct
import os

ROOT = "/home/z/my-project"
BIN_FILE = os.path.join(ROOT, "assets/animations/binary/fists1_stance_idle.bin")

with open(BIN_FILE, 'rb') as f:
    data = f.read()

fc = struct.unpack('<I', data[:4])[0]
print(f"frame_count: {fc}")

# Parse all frames
offset = 4
frames = []
for fi in range(fc):
    skip = data[offset]
    nc = struct.unpack('<I', data[offset+1:offset+5])[0]
    offset += 5
    nodes = []
    for i in range(nc):
        x, y, z = struct.unpack('<fff', data[offset:offset+12])
        nodes.append((x, y, -z))
        offset += 12
    frames.append((skip, nodes))

# Print node[18] (NPivot) and node[19] across all frames
print(f"\n=== NPivot (node 18) across frames ===")
for fi, (skip, nodes) in enumerate(frames):
    if 18 < len(nodes):
        x, y, z = nodes[18]
        print(f"  frame {fi}: skip={skip}, pos=({x:.2f}, {y:.2f}, {z:.2f})")

print(f"\n=== node 19 across frames (should be Weapon-Node1_1) ===")
for fi, (skip, nodes) in enumerate(frames):
    if 19 < len(nodes):
        x, y, z = nodes[19]
        print(f"  frame {fi}: pos=({x:.2f}, {y:.2f}, {z:.2f})")

print(f"\n=== node 20 across frames (should be Weapon-Node2_1) ===")
for fi, (skip, nodes) in enumerate(frames[:5]):
    if 20 < len(nodes):
        x, y, z = nodes[20]
        print(f"  frame {fi}: pos=({x:.2f}, {y:.2f}, {z:.2f})")

# Also check high_punch.bin — maybe its nodes 19-26 are different
print("\n\n=== high_punch.bin ===")
BIN2 = os.path.join(ROOT, "assets/animations/binary/high_punch.bin")
with open(BIN2, 'rb') as f:
    data2 = f.read()
fc2 = struct.unpack('<I', data2[:4])[0]
print(f"frame_count: {fc2}")
offset = 4
for fi in range(min(3, fc2)):
    skip = data2[offset]
    nc = struct.unpack('<I', data2[offset+1:offset+5])[0]
    offset += 5
    print(f"  frame {fi}: skip={skip}, nc={nc}")
    for i in [0, 1, 7, 13, 15, 17, 18, 19, 20, 21, 27, 43, 54, 66]:
        if i < nc:
            x, y, z = struct.unpack('<fff', data2[offset+i*12:offset+i*12+12])
            print(f"    node[{i}]: ({x:.2f}, {y:.2f}, {-z:.2f})")
    offset += nc * 12

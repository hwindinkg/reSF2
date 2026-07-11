#!/usr/bin/env python3
"""Verify .bin node order matches skeleton.xml."""
import struct
import re
import os

ROOT = "/home/z/my-project"
SKEL_XML = os.path.join(ROOT, "assets/models/skeleton.xml")
BIN_FILE = os.path.join(ROOT, "assets/animations/binary/fists1_stance_idle.bin")

# Parse skeleton.xml — extract all nodes with X/Y/Z in XML order
def parse_skeleton(path):
    with open(path, 'r', encoding='cp1251') as f:
        xml = f.read()
    # Find <Nodes>...</Nodes>
    m = re.search(r'<Nodes>(.*?)</Nodes>', xml, re.DOTALL)
    if not m:
        return []
    nodes_xml = m.group(1)
    # Find all tags with X= and Y= attributes
    pattern = re.compile(r'<([\w\-]+)\s+([^>]+?)/>', re.DOTALL)
    nodes = []
    for m in pattern.finditer(nodes_xml):
        tag_name = m.group(1)
        attrs = m.group(2)
        if 'X="' in attrs and 'Y="' in attrs:
            x = float(re.search(r'X="([^"]+)"', attrs).group(1))
            y = float(re.search(r'Y="([^"]+)"', attrs).group(1))
            z = float(re.search(r'Z="([^"]+)"', attrs).group(1))
            type_m = re.search(r'Type="([^"]+)"', attrs)
            type_ = type_m.group(1) if type_m else "?"
            nodes.append((tag_name, type_, x, y, z))
    return nodes

# Parse .bin file
def parse_bin(path):
    with open(path, 'rb') as f:
        data = f.read()
    fc = struct.unpack('<I', data[:4])[0]
    offset = 4
    frames = []
    for fi in range(fc):
        if offset + 5 > len(data):
            break
        skip = data[offset]
        nc = struct.unpack('<I', data[offset+1:offset+5])[0]
        offset += 5
        nodes = []
        for i in range(nc):
            if offset + 12 > len(data):
                break
            x, y, z = struct.unpack('<fff', data[offset:offset+12])
            nodes.append((x, y, -z))  # bin stores -Z
            offset += 12
        frames.append(nodes)
    return fc, frames

skel_nodes = parse_skel_nodes = parse_skeleton(SKEL_XML)
fc, frames = parse_bin(BIN_FILE)

print(f"skeleton.xml: {len(skel_nodes)} nodes")
print(f"bin: {fc} frames, {len(frames[0])} nodes per frame")
print()
print(f"{'idx':>3} {'name':<20} {'type':<12} {'rest_x':>8} {'rest_y':>8}  {'bin0_x':>8} {'bin0_y':>8}  {'dx':>7} {'dy':>7}")
print("-" * 110)
for i, (name, type_, rx, ry, rz) in enumerate(skel_nodes):
    if i >= len(frames[0]):
        break
    bx, by, bz = frames[0][i]
    # NPivot should match between rest and bin (modulo world offset)
    npivot_idx = next((j for j, n in enumerate(skel_nodes) if n[0] == 'NPivot'), -1)
    if npivot_idx >= 0 and npivot_idx < len(frames[0]):
        npx, npy = skel_nodes[npivot_idx][2], skel_nodes[npivot_idx][3]
        bnpivot_x, bnpivot_y = frames[0][npivot_idx][0], frames[0][npivot_idx][1]
        # delta from NPivot in rest vs bin
        rest_dx = rx - npx
        rest_dy = ry - npy
        bin_dx = bx - bnpivot_x
        bin_dy = by - bnpivot_y
        diff_x = bin_dx - rest_dx
        diff_y = bin_dy - rest_dy
        marker = " *" if abs(diff_x) > 30 or abs(diff_y) > 30 else ""
        print(f"{i:>3} {name:<20} {type_:<12} {rx:>8.2f} {ry:>8.2f}  {bx:>8.2f} {by:>8.2f}  {diff_x:>7.2f} {diff_y:>7.2f}{marker}")

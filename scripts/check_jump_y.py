#!/usr/bin/env python3
"""Check NPivot Y trajectory for jump animations to debug Y smoothness."""
import struct
import os
import sys

BIN_DIR = "/home/z/my-project/assets/animations/binary"

def read_bin(path):
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 4:
        return None
    fc = struct.unpack_from('<I', data, 0)[0]
    if fc <= 0 or fc > 10000:
        return None
    frames = []
    offset = 4
    for fi in range(fc):
        if offset + 5 > len(data):
            break
        skip = data[offset]
        nc = struct.unpack_from('<I', data, offset + 1)[0]
        offset += 5
        nodes = []
        for i in range(nc):
            if offset + 12 > len(data):
                break
            x, y, neg_z = struct.unpack_from('<fff', data, offset)
            offset += 12
            nodes.append((x, y, -neg_z))
        frames.append(nodes)
    return fc, frames

# skeleton.xml node order (from previous analysis)
# NPivot is index 18 in the .bin file
NPIVOT_IDX = 18

for anim_name in ['jump', 'jump_away', 'front_flip', 'back_flip', 'back_handflip',
                  'air_punch', 'air_axe_kick', 'stance_idle', 'fists_idle']:
    path = os.path.join(BIN_DIR, anim_name + '.bin')
    if not os.path.exists(path):
        print(f"{anim_name}: NOT FOUND")
        continue
    result = read_bin(path)
    if not result:
        print(f"{anim_name}: FAILED TO READ")
        continue
    fc, frames = result
    print(f"\n=== {anim_name} ({fc} frames) ===")
    if len(frames) > 0 and len(frames[0]) > NPIVOT_IDX:
        print(f"  Node count: {len(frames[0])}")
        print(f"  NPivot (idx={NPIVOT_IDX}) trajectory:")
        for fi in range(min(fc, 30)):
            x, y, z = frames[fi][NPIVOT_IDX]
            print(f"    frame {fi:3d}: X={x:8.2f}  Y={y:8.2f}  Z={z:8.2f}")
        if fc > 30:
            x, y, z = frames[fc-1][NPIVOT_IDX]
            print(f"    frame {fc-1:3d} (LAST): X={x:8.2f}  Y={y:8.2f}  Z={z:8.2f}")

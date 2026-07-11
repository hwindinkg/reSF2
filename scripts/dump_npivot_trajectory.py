#!/usr/bin/env python3
"""Dump NPivot trajectory in animation .bin files to understand root motion.

The .bin format:
  u32 frame_count (LE)
  Per frame:
    byte 0: skip byte (1=keyframe, 5=interframe)
    bytes 1-4: u32 node_count (LE)
    bytes 5+: node_count × 3 floats (X, Y, -Z) LE

Node order: ALL skeleton.xml nodes in XML order (67 total).
NPivot is one of these nodes — we need to find its index.
"""
import struct
import sys
import os
from pathlib import Path


def load_skeleton_order(skeleton_path: str):
    """Load node names from skeleton.xml in XML order.
    Format: <NodeName Type="Node" X="..." Y="..." .../>
    The node name is the XML tag name (e.g. <NPivot Type="Node" .../>).
    """
    with open(skeleton_path, 'r') as f:
        xml = f.read()
    names = []
    import re
    # Match <TagName Type="(Node|CenterOfMass|MacroNode)" ... />
    # The tag name is the first word after <
    for m in re.finditer(r'<(\w+)\s+Type="(Node|CenterOfMass|MacroNode)"', xml):
        names.append(m.group(1))
    return names


def parse_bin(path: str):
    """Parse a .bin animation file. Returns (frame_count, frames) where
    frames[i] = list of (x, y, z) tuples for each node."""
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 5:
        return 0, []
    frame_count = struct.unpack_from('<I', data, 0)[0]
    frames = []
    offset = 4
    for fi in range(frame_count):
        if offset >= len(data):
            break
        skip = data[offset]
        offset += 1
        if offset + 4 > len(data):
            break
        node_count = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        nodes = []
        for ni in range(node_count):
            if offset + 12 > len(data):
                break
            x, y, z = struct.unpack_from('<fff', data, offset)
            offset += 12
            nodes.append((x, y, z))
        frames.append(nodes)
    return frame_count, frames


def main():
    # Find skeleton.xml
    skel_candidates = [
        "/home/z/my-project/assets/models/skeleton.xml",
        "/home/z/my-project/work/sf2_data/sf2/assets/assets/models/skeleton.xml",
    ]
    skel_path = None
    for p in skel_candidates:
        if os.path.exists(p):
            skel_path = p
            break
    if not skel_path:
        print("skeleton.xml not found")
        return 1

    node_names = load_skeleton_order(skel_path)
    print(f"Skeleton nodes: {len(node_names)}")
    npivot_idx = node_names.index("NPivot") if "NPivot" in node_names else -1
    print(f"NPivot index: {npivot_idx}")
    if npivot_idx < 0:
        print("NPivot not found!")
        return 1

    # Find animation .bin files
    anim_dirs = [
        "/home/z/my-project/assets/animations/binary",
        "/home/z/my-project/work/sf2_data/sf2/assets/assets/animations/binary",
    ]
    anim_dir = None
    for d in anim_dirs:
        if os.path.isdir(d):
            anim_dir = d
            break
    if not anim_dir:
        print("animations/binary not found")
        return 1

    # Analyze key animations
    targets = sys.argv[1:] if len(sys.argv) > 1 else [
        "fists_idle", "fists1_stance_idle",
        "step_forward", "step_back",
        "high_punch", "low_punch", "double_punch", "spinning_punch", "upper_cut",
        "high_kick", "front_kick", "back_kick", "sweep", "low_kick",
        "forward_roll", "back_roll",
        "jump", "jump_kick",
        "dash_forward", "dash_back",
        "knockdown", "getup",
    ]

    print(f"\n{'Animation':<25s} {'Frames':>6s} {'NPivot[0]':>12s} {'NPivot[last]':>14s} {'Delta X':>10s} {'MinX':>8s} {'MaxX':>8s}")
    print("-" * 90)

    for name in targets:
        bin_path = os.path.join(anim_dir, name + ".bin")
        if not os.path.exists(bin_path):
            print(f"{name:<25s} NOT FOUND")
            continue
        fc, frames = parse_bin(bin_path)
        if fc == 0 or not frames:
            print(f"{name:<25s} EMPTY")
            continue
        # Extract NPivot X trajectory
        npivot_xs = []
        npivot_ys = []
        for f in frames:
            if npivot_idx < len(f):
                npivot_xs.append(f[npivot_idx][0])
                npivot_ys.append(f[npivot_idx][1])
        if not npivot_xs:
            print(f"{name:<25s} NO NPIVOT DATA")
            continue
        delta = npivot_xs[-1] - npivot_xs[0]
        print(f"{name:<25s} {fc:6d} ({npivot_xs[0]:8.2f},{npivot_ys[0]:7.2f}) ({npivot_xs[-1]:8.2f},{npivot_ys[-1]:7.2f}) {delta:8.2f} {min(npivot_xs):8.2f} {max(npivot_xs):8.2f}")

    # Detailed dump for step_forward and step_back
    print("\n=== Detailed NPivot trajectory ===")
    for name in ["step_forward", "step_back", "fists_idle"]:
        bin_path = os.path.join(anim_dir, name + ".bin")
        if not os.path.exists(bin_path):
            continue
        fc, frames = parse_bin(bin_path)
        print(f"\n--- {name} ({fc} frames) ---")
        for i, f in enumerate(frames):
            if npivot_idx < len(f):
                x, y, z = f[npivot_idx]
                print(f"  frame {i:3d}: NPivot = ({x:8.2f}, {y:8.2f}, {z:8.2f})")


if __name__ == "__main__":
    main()

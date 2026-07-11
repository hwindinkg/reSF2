#!/usr/bin/env python3
"""Check NPivot progression in step_forward.bin and step_back.bin."""
import struct
import os

ROOT = "/home/z/my-project"
files = ["step_forward.bin", "step_back.bin", "fists1_stance_idle.bin"]

for fn in files:
    path = os.path.join(ROOT, "assets/animations/binary", fn)
    with open(path, 'rb') as f:
        data = f.read()
    fc = struct.unpack('<I', data[:4])[0]
    offset = 4
    npivot_positions = []
    for fi in range(fc):
        skip = data[offset]
        nc = struct.unpack('<I', data[offset+1:offset+5])[0]
        offset += 5
        for i in range(nc):
            x, y, z = struct.unpack('<fff', data[offset:offset+12])
            if i == 18:  # NPivot index
                npivot_positions.append((x, y, -z))
            offset += 12
    print(f"\n=== {fn} ({fc} frames) ===")
    print(f"NPivot X progression:")
    for fi, (x, y, z) in enumerate(npivot_positions):
        delta = npivot_positions[fi][0] - npivot_positions[0][0] if fi > 0 else 0
        print(f"  frame {fi:2d}: X={x:8.2f}  Y={y:8.2f}  Z={z:8.2f}  deltaX_from_f0={delta:8.2f}")
    if len(npivot_positions) > 1:
        total_dx = npivot_positions[-1][0] - npivot_positions[0][0]
        print(f"  Total X delta (f0 → last): {total_dx:.2f}")
        # Check for wrap-around (large negative deltas between consecutive frames)
        for fi in range(1, len(npivot_positions)):
            d = npivot_positions[fi][0] - npivot_positions[fi-1][0]
            if abs(d) > 30:
                print(f"  *** Large delta at frame {fi}: {d:.2f} (wrap-around?)")

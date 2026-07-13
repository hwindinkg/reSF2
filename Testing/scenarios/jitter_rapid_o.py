#!/usr/bin/env python3
"""Generate jittered rapid-O scenarios to reproduce 'hit without animation' bug.
Creates 10 variants with ±2 frame jitter on each key press."""
import random
import os

random.seed(42)  # deterministic
base_frames = [180, 190, 200, 210, 220]  # 5 O presses, 10 frames apart

for variant in range(10):
    lines = ["# Jittered rapid O variant {}".format(variant)]
    for base in base_frames:
        jitter = random.randint(-2, 2)
        press = base + jitter
        release = press + 1 + random.randint(0, 1)
        lines.append("frame {} keydown O".format(press))
        lines.append("frame {} keyup O".format(release))
    fname = "Testing/scenarios/14_jitter_o_{}.txt".format(variant)
    with open(fname, 'w') as f:
        f.write("\n".join(lines) + "\n")
    print("wrote {}".format(fname))

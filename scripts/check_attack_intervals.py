#!/usr/bin/env python3
"""Check attack intervals for all combat moves in moves.xml."""
import re
import os

MOVES_XML = "/home/z/my-project/assets/animations/moves.xml"

with open(MOVES_XML, 'r', encoding='utf-8') as f:
    xml = f.read()

# Find all combat moves and their attack intervals
moves_to_check = [
    "HighPunch", "HeavyPunch", "LowPunch", "DoublePunch", 
    "SpinningPunch", "UpperCut",
    "HighKick", "FrontKick", "BackKick", "Sweep", "LowKick"
]

for move_name in moves_to_check:
    # Find the Move tag
    pattern = rf'<Move Name="{move_name}"[^>]*>(.*?)</Move>'
    m = re.search(pattern, xml, re.DOTALL)
    if not m:
        print(f"{move_name}: NOT FOUND")
        continue
    inner = m.group(1)
    # Find Attack interval
    attack_m = re.search(r'<Interval[^>]*Type="Attack"[^>]*Start="(\d+)"[^>]*End="(\d+)"', inner)
    if not attack_m:
        # Try reverse order (End before Start)
        attack_m = re.search(r'<Interval[^>]*Type="Attack"[^>]*End="(\d+)"[^>]*Start="(\d+)"', inner)
        if attack_m:
            print(f"{move_name}: Attack End={attack_m.group(1)} Start={attack_m.group(2)} (reversed!)")
        else:
            print(f"{move_name}: NO Attack interval found")
    else:
        start = attack_m.group(1)
        end = attack_m.group(2)
        # Also find FileName
        fn_m = re.search(r'FileName="([^"]+)"', m.group(0))
        fn = fn_m.group(1) if fn_m else "?"
        # Find FirstFrame
        ff_m = re.search(r'FirstFrame="(\d+)"', m.group(0))
        ff = ff_m.group(1) if ff_m else "0"
        print(f"{move_name} ({fn}): Attack Start={start} End={end}  FirstFrame={ff}")

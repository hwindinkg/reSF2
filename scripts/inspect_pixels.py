#!/usr/bin/env python3
"""Quick pixel inspection of specific regions."""
import sys
from PIL import Image

img = Image.open(sys.argv[1]).convert("RGBA")
w, h = img.size

# Check the spine area: NPivot screen (628, 284) to NStomach (626, 268)
# Sample a vertical strip at x=625-630, y=260-290
print("Spine area (NPivot -> NStomach):")
for y in range(260, 295, 2):
    row = ""
    for x in range(622, 632):
        p = img.getpixel((x, y))
        if p[0] > 150 and p[1] > 150 and p[2] > 150:
            row += "#"
        elif p[0] > 200 and p[1] < 100 and p[2] < 100:
            row += "R"
        else:
            row += "."
    print(f"  y={y}: {row}")

# Check the head area: screen ~(640, 213)
print("\nHead area:")
for y in range(200, 230, 2):
    row = ""
    for x in range(625, 655):
        p = img.getpixel((x, y))
        if p[0] > 200 and 150 < p[1] < 230 and p[2] < 150:
            row += "H"  # head orange
        elif p[0] > 150 and p[1] > 150 and p[2] > 150:
            row += "#"  # light
        elif p[0] > 200 and p[1] < 100 and p[2] < 100:
            row += "R"  # red joint
        else:
            row += "."
    print(f"  y={y}: {row}")

# Check the feet area
print("\nFeet area (NPivot screen ~628, 360):")
for y in range(350, 380, 2):
    row = ""
    for x in range(615, 660):
        p = img.getpixel((x, y))
        if p[0] > 150 and p[1] > 150 and p[2] > 150:
            row += "#"
        elif p[0] > 200 and p[1] < 100 and p[2] < 100:
            row += "R"
        else:
            row += "."
    print(f"  y={y}: {row}")

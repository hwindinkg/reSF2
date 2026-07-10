#!/usr/bin/env python3
"""Find all character pixels in a wider region."""
import sys
from PIL import Image

img = Image.open(sys.argv[1]).convert("RGBA")
w, h = img.size

# Character is centered at screen (640, 360), spans ~200px tall
# Sample the full character bounding box
light_count = 0
red_count = 0
total = 0
min_x, min_y, max_x, max_y = w, h, 0, 0
for y in range(150, 500):
    for x in range(500, 800):
        total += 1
        p = img.getpixel((x, y))
        if p[0] > 150 and p[1] > 150 and p[2] > 150:
            light_count += 1
            if x < min_x: min_x = x
            if y < min_y: min_y = y
            if x > max_x: max_x = x
            if y > max_y: max_y = y
        if p[0] > 200 and p[1] < 100 and p[2] < 100:
            red_count += 1

print(f"Region: 500-800 x 150-500 ({total} pixels)")
print(f"Light (lines): {light_count}")
print(f"Red (joints):  {red_count}")
if light_count > 0:
    print(f"Light bbox: ({min_x},{min_y}) to ({max_x},{max_y})")

# Also count orange (head)
orange_count = 0
for y in range(150, 500):
    for x in range(500, 800):
        p = img.getpixel((x, y))
        if p[0] > 200 and 150 < p[1] < 230 and p[2] < 150:
            orange_count += 1
print(f"Orange (head): {orange_count}")

#!/usr/bin/env python3
"""Sample pixels around the expected character position."""
import sys
from PIL import Image

img = Image.open(sys.argv[1]).convert("RGBA")
w, h = img.size

# The character is at world (player_x, player_y) = (690, -93)
# Camera is at (690, -93), zoom 0.85
# So character center is at screen (640, 360)
# Character spans roughly 210 world units tall * 0.85 zoom = ~180 screen pixels
# So character is from screen y = 360-90 to 360+90 (after shifting feet to 0)

# Sample a grid around the center
print("Sampling 20x20 grid around screen center (640, 360):")
for j in range(-10, 11, 2):
    row = ""
    for i in range(-10, 11, 2):
        x, y = 640 + i*5, 360 + j*5
        if 0 <= x < w and 0 <= y < h:
            p = img.getpixel((x, y))
            # Mark non-bg pixels
            if p[0] > 150 and p[1] > 150 and p[2] > 150:
                row += "##"  # light (character)
            elif p[0] < 50 and p[1] < 30 and p[2] < 20:
                row += ".."  # dark bg
            else:
                row += "=="  # medium (dojo bg)
    print(row)

# Also count light pixels (character) in the center region
light_count = 0
for y in range(280, 440):
    for x in range(560, 720):
        p = img.getpixel((x, y))
        if p[0] > 150 and p[1] > 150 and p[2] > 150:
            light_count += 1
print(f"\nLight pixels in center region (560-720, 280-440): {light_count}")

# Count red pixels (joints)
red_count = 0
for y in range(280, 440):
    for x in range(560, 720):
        p = img.getpixel((x, y))
        if p[0] > 200 and p[1] < 100 and p[2] < 100:
            red_count += 1
print(f"Red pixels in center region: {red_count}")

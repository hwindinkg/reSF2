#!/usr/bin/env python3
"""Analyze a screenshot to find non-background pixels (character)."""
import sys
from PIL import Image

img = Image.open(sys.argv[1]).convert("RGBA")
w, h = img.size
print(f"Image: {w}x{h}")

# Sample the background color from a corner
bg = img.getpixel((10, 10))
print(f"Background color at (10,10): {bg}")

# Find non-background pixels (character/objects)
non_bg_count = 0
min_x, min_y, max_x, max_y = w, h, 0, 0
sample_pixels = []
for y in range(0, h, 4):
    for x in range(0, w, 4):
        p = img.getpixel((x, y))
        # Allow some tolerance
        if abs(p[0]-bg[0]) > 30 or abs(p[1]-bg[1]) > 30 or abs(p[2]-bg[2]) > 30:
            non_bg_count += 1
            if x < min_x: min_x = x
            if y < min_y: min_y = y
            if x > max_x: max_x = x
            if y > max_y: max_y = y

print(f"Non-background pixels (sampled): {non_bg_count}")
if non_bg_count > 0:
    print(f"Bounding box: ({min_x},{min_y}) to ({max_x},{max_y})")

# Sample the center of the image
cx, cy = w//2, h//2
print(f"\nCenter pixel ({cx},{cy}): {img.getpixel((cx, cy))}")

# Sample the player position (should be at world x=690, y=-93)
# Camera is at (0, -93), zoom=0.85, view_width=1280, view_height=720
# World (690, -93) -> screen (1280/2 + 690*0.85, 720/2 - (-93+93)*0.85) = (640+586, 360) = (1226, 360)
px = 640 + int(690 * 0.85)
py = 360
print(f"Player world (690,-93) -> screen ({px},{py})")
print(f"  Pixel: {img.getpixel((px, py))}")
print(f"  Pixel (px-10,py): {img.getpixel((px-10, py))}")
print(f"  Pixel (px+10,py): {img.getpixel((px+10, py))}")
print(f"  Pixel (px,py-30): {img.getpixel((px, py-30))}")
print(f"  Pixel (px,py-100): {img.getpixel((px, py-100))}")

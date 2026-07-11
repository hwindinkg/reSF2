#!/usr/bin/env python3
"""Check plist atlas frame metadata for dojo background."""
import os
import re
import sys

# Find all plist files for dojo location
ROOT = "/home/z/my-project"
# Check where assets are loaded from — look for the actual asset paths
import subprocess

# The user runs: resf2_app.exe --assets E:\reSF2\sf2\assets
# So assets are at E:\reSF2\sf2\assets on Windows.
# On our side, we only have assets/animations and assets/models in the repo.
# The location textures (bg.plist, atlas_layer1.plist, etc.) are NOT in the repo.
# They come from the user's sf2.7z extraction.

# Let's check if there are any .plist files in the repo
result = subprocess.run(['find', ROOT, '-name', '*.plist', '-type', 'f'],
                       capture_output=True, text=True)
print("Plist files in repo:")
print(result.stdout[:500] if result.stdout else "  (none)")

# Check what's in download/
result = subprocess.run(['find', f'{ROOT}/download', '-name', '*.plist', '-type', 'f'],
                       capture_output=True, text=True)
print("\nPlist files in download/:")
print(result.stdout[:500] if result.stdout else "  (none)")

# Check the params.xml loading — it's loaded from the user's assets dir
# We can't inspect it directly, but we know from logs:
# Atlas 'bg': 2 frames
# Atlas 'atlas_layer1': 2 frames
# Atlas 'atlas_layer2': 3 frames
# Atlas 'atlas_layer3': 7 frames

# The "rotated on its side" issue means the background texture is being
# rendered with a 90° rotation. This happens when:
# 1. The plist frame has rotated=true, AND
# 2. Our renderer doesn't account for the rotation in UV mapping

# In our render_location(), we compute UVs:
#   if (frame.rotated):
#     u0 = atlas_x / tw;  v0 = atlas_y / th;
#     u1 = (atlas_x + atlas_h) / tw;  v1 = (atlas_y + atlas_w) / th;
# This swaps w/h in UV space but does NOT swap the rendered quad dimensions.
# The quad is always rendered as img.w × img.h.
# If the frame is rotated, the atlas stores it as (atlas_h wide × atlas_w tall),
# but the original sprite is (atlas_w wide × atlas_h tall).
# So we need to swap img.w and img.h when frame.rotated is true.

print("\n=== Diagnosis ===")
print("The 'background on its side' issue is caused by rotated atlas frames.")
print("When frame.rotated=true:")
print("  - Atlas stores sprite rotated 90° CW (atlas_h wide × atlas_w tall)")
print("  - Original sprite is (atlas_w wide × atlas_h tall)")
print("  - Our UV mapping swaps w/h in UV space (correct)")
print("  - BUT we render the quad as img.w × img.h (WRONG for rotated)")
print("  - Should render as img.h × img.w when rotated")
print("  - OR un-rotate the pixels during cropping (like we do for HUD)")

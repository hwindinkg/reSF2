#!/usr/bin/env python3
"""Create a contact sheet of all screenshots for easy review."""
import os
from PIL import Image, ImageDraw, ImageFont

screenshots_dir = "/home/z/my-project/download/screenshots"
output_path = "/home/z/my-project/download/reSF2_stage7.3_contact_sheet.png"

# Collect screenshots in order
files = sorted([f for f in os.listdir(screenshots_dir) if f.endswith(".png")])
print(f"Found {len(files)} screenshots")

# Load images
images = []
for f in files:
    img = Image.open(os.path.join(screenshots_dir, f)).convert("RGB")
    images.append((f, img))

# Layout: 3 columns
cols = 3
rows = (len(images) + cols - 1) // cols
thumb_w, thumb_h = 640, 360  # half-size thumbnails
margin = 10
label_h = 30

sheet_w = cols * (thumb_w + margin) + margin
sheet_h = rows * (thumb_h + label_h + margin) + margin

sheet = Image.new("RGB", (sheet_w, sheet_h), (20, 20, 20))
draw = ImageDraw.Draw(sheet)

# Try to load a font
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16)
except:
    font = ImageFont.load_default()

for i, (name, img) in enumerate(images):
    row = i // cols
    col = i % cols
    x = margin + col * (thumb_w + margin)
    y = margin + row * (thumb_h + label_h + margin)
    # Resize image to thumbnail
    thumb = img.resize((thumb_w, thumb_h), Image.LANCZOS)
    sheet.paste(thumb, (x, y + label_h))
    # Draw label
    label = name.replace(".png", "").replace("_", " ")
    draw.text((x + 5, y + 5), label, fill=(255, 255, 255), font=font)

sheet.save(output_path)
print(f"Contact sheet saved to: {output_path}")
print(f"Size: {sheet_w}x{sheet_h}")

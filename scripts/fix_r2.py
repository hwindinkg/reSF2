#!/usr/bin/env python3
"""fix_r2.py  --  R2 two-camera background fix for FightScreen::render_impl.

Root cause
----------
FightScreen renders location layers (dojo background sprites) with the fight
camera.  The fight camera's center_y (approx. -221.6 in fight-world coords)
is the floor-line anchor used to keep fighter feet at 78% of screen height.
Location sprites are stored in raw dojo-world Y (XML Y values), so:

    screen_y = (xml_y - fight_cy) * zoom + 360
             = (223 - (-221.6)) * 1.0 + 360  ~  805  <- off the 720-px frame

Fix
---
Render location layers with a separate 'bg' camera that shares the fight
cam's center_x (horizontal parallax tracking) and zoom but uses the
LocationScene::default_camera vertical formula:

    bg.center_y = ((arena_h / 2 - arena_floor) / 2) * (1 - zoom)
                = ((280 - 80) / 2) * (1 - 1.0) = 0  at zoom=1.0

so the floor sprite lands at screen_y ~= 583 -- correctly framed.
At zoom=1.3 (16:9 wide screen): bg.center_y = -30, floor at screen_y ~= 651.

Variable naming: the fight render camera in FightScreen::render_impl is
called 'camera' (not 'c'); verified from screens.cpp search + visual_debug.md:
    camera.center_x = fight.camera.center_x
    camera.center_y = cam.center_y  // <-- bug anchor (fight-world cy)
    camera.zoom = cam.zoom
    assets.dojo.render_layers(ren, camera, 0, fighter_layer); // bg to fix
"""

import pathlib
import re
import sys

SCREENS = pathlib.Path("core/app/screens.cpp")
src = SCREENS.read_text(encoding="utf-8")
orig = src

# -- idempotency guard ---------------------------------------------------
GUARD = "sf2::render::Camera bg = camera;"
if GUARD in src:
    print("screens.cpp already has R2 fix -- nothing to do")
    sys.exit(0)

# -- 1.  Inject bg camera after 'camera.center_y = cam.center_y;' --------
# This line is unique to FightScreen::render_impl (DojoScreen uses ui_cam,
# not cam.center_y). The 'camera' variable name is confirmed from
# visual_debug.md and screens.cpp code search.
ANCHOR = "camera.center_y = cam.center_y;"
pos = src.find(ANCHOR)
if pos < 0:
    # Try fallback: some builds may use a slightly different expression
    ANCHOR_ALT = "camera.center_y = cam.center_y"
    pos = src.find(ANCHOR_ALT)
    if pos >= 0:
        ANCHOR = ANCHOR_ALT
if pos < 0:
    print(f"ERROR: anchor not found: {ANCHOR!r}", file=sys.stderr)
    sys.exit(1)
line_end = src.index("\n", pos) + 1  # character position after the newline

BG_CODE = (
    "    // R2 fix -- bg camera for location layer rendering.\n"
    "    // The fight camera center_y is the floor anchor in fight-world coords\n"
    "    // (approx. -221.6), incompatible with location-layer world Y values.\n"
    "    // bg shares center_x/zoom but uses the default_camera vertical formula:\n"
    "    //   center_y = ((arena_h/2 - arena_floor)/2) * (1-zoom)\n"
    "    //   = ((280-80)/2)*(1-1.0) = 0  at zoom=1.0  -> floor at screen_y~=583\n"
    "    //   = ((280-80)/2)*(1-1.3) = -30 at zoom=1.3 -> floor at screen_y~=651\n"
    "    sf2::render::Camera bg = camera;\n"
    "    bg.center_y = ((assets.dojo.arena_height() / 2.0f - assets.dojo.arena_floor()) / 2.0f)\n"
    "                  * (1.0f - camera.zoom);\n"
)
src = src[:line_end] + BG_CODE + src[line_end:]
print(f"OK: bg camera injected after {ANCHOR!r}")

# -- 2.  Route background location render_layers to the bg camera --------
# Target the specific background call (bg layers 0..fighter_layer):
#   assets.dojo.render_layers(ren, camera, 0, fighter_layer)
# The foreground call (fighter_layer+1..end) must keep the fight camera.
# Three patterns to try (ordered most- to least-specific):
patterns = [
    # Most specific: full call with 0, fighter_layer
    (r"(assets\.dojo\.render_layers\(ren),\s*camera,\s*0,\s*fighter_layer\)",
     r"\1, bg, 0, fighter_layer)"),
    # Mid: any render_layers with camera starting at layer 0
    (r"(\.render_layers\(ren),\s*camera,\s*0,",
     r"\1, bg, 0,"),
    # Broad fallback: any render_layers(ren, camera, - replaces ALL bg+fg
    # (acceptable if there is exactly one render_layers in FightScreen)
    (r"(assets\.dojo\.render_layers\(ren),\s*camera,",
     r"\1, bg,"),
]
replaced = 0
for pat, repl in patterns:
    new_src, n = re.subn(pat, repl, src)
    if n > 0:
        src = new_src
        replaced += n
        print(f"OK: {n} render_layers(ren, camera, ...) -> render_layers(ren, bg, ...) [pattern {patterns.index((pat, repl))+1}]")
        break

if replaced == 0:
    print(
        "WARNING: no render_layers(ren, camera, ...) calls matched.\n"
        "bg camera is constructed but NOT yet wired to render_layers.\n"
        "Manually change render_layers(ren, camera, 0, fighter_layer) to\n"
        "render_layers(ren, bg, 0, fighter_layer) in FightScreen::render_impl."
    )

if src == orig:
    print("Nothing changed")
    sys.exit(0)

SCREENS.write_text(src, encoding="utf-8")
print(f"OK: {SCREENS} patched successfully")

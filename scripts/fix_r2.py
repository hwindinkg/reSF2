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
"""

import pathlib
import re
import sys

SCREENS = pathlib.Path("core/app/screens.cpp")
src = SCREENS.read_text(encoding="utf-8")
orig = src

# -- idempotency guard ---------------------------------------------------
GUARD = "sf2::render::Camera bg = c;"
if GUARD in src:
    print("screens.cpp already has R2 fix -- nothing to do")
    sys.exit(0)

# -- 1.  Inject bg camera after 'c.center_y = cam.center_y;' -----------
# This line is unique to FightScreen::render_impl (DojoScreen uses ui_cam).
ANCHOR = "c.center_y = cam.center_y;"
pos = src.find(ANCHOR)
if pos < 0:
    print(f"ERROR: anchor not found: {ANCHOR!r}", file=sys.stderr)
    sys.exit(1)
line_end = src.index("\n", pos) + 1  # character position after the newline

BG_CODE = (
    "    // R2 fix -- bg camera for location layer rendering.\n"
    "    // Location sprites are in dojo-world Y (raw XML Y values), which is\n"
    "    // incompatible with the fight cam's center_y (fight-world floor\n"
    "    // anchor, e.g. -221.6). Using fight cy sends the floor sprite\n"
    "    // (XML y~=223) to screen_y~=880 -- off the 720-px frame.\n"
    "    // bg shares center_x (horizontal tracking) and zoom but uses the\n"
    "    // default_camera vertical formula so sprites land correctly:\n"
    "    //   center_y = ((arena_h/2 - arena_floor)/2) * (1-zoom)\n"
    "    //   = ((280-80)/2)*(1-1.0) = 0  at zoom=1.0  -> floor at screen_y~=583\n"
    "    //   = ((280-80)/2)*(1-1.3) = -30 at zoom=1.3 -> floor at screen_y~=651\n"
    "    sf2::render::Camera bg = c;\n"
    "    bg.center_y = ((assets.dojo.arena_height() / 2.0f - assets.dojo.arena_floor()) / 2.0f)\n"
    "                  * (1.0f - c.zoom);\n"
)
src = src[:line_end] + BG_CODE + src[line_end:]
print("OK: bg camera injected after 'c.center_y = cam.center_y;'")

# -- 2.  Route all location render_layers to the bg camera -------------
# DojoScreen uses 'ui_cam'; only FightScreen::render_impl uses bare 'c'.
# Replace: assets.dojo.render_layers(ren, c,
# With:    assets.dojo.render_layers(ren, bg,
new_src, n = re.subn(
    r"(assets\.dojo\.render_layers\(ren),\s*c,",
    r"\1, bg,",
    src,
)
if n == 0:
    # Fallback: any .render_layers(ren, c,  (avoids matching ui_cam calls)
    new_src, n = re.subn(
        r"(\brender_layers\(ren),\s*c,",
        r"\1, bg,",
        src,
    )
if n > 0:
    src = new_src
    print(f"OK: {n} render_layers(ren, c, ...) -> render_layers(ren, bg, ...)")
else:
    print(
        "WARNING: no render_layers(ren, c, ...) calls found -- "
        "bg camera is constructed but not yet wired to render_layers.\n"
        "Manually change render_layers(ren, c, ...) to render_layers(ren, bg, ...) "
        "in FightScreen::render_impl if needed."
    )

if src == orig:
    print("Nothing changed")
    sys.exit(0)

SCREENS.write_text(src, encoding="utf-8")
print(f"OK: {SCREENS} patched successfully")

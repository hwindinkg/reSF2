#!/usr/bin/env python3
"""fix_r3.py  --  R3 capsule disc-cap ends for FightScreen::draw_capsules.

Root cause
----------
FightScreen::render_impl contains a draw_capsules lambda that renders each
capsule as a thick quad (stroked line segment) but does NOT add circle discs
at the two endpoints.  The JS reference (Dk, draw_capsules) draws:
  * thick quad  (stroke = radius1 * 2)
  * disc cap at p1  (12 fan triangles, radius = stroke/2)
  * disc cap at p2  (12 fan triangles, radius = stroke/2)

The DojoScreen's draw_dojo_figure helper already has the correct full
implementation (including draw_disc).  This script patches the FIGHT-SCREEN
version to match.

Approach
--------
1. Locate the draw_capsules lambda by its capture list signature.
2. Within that lambda's body, find the first quad draw call:
       ren.draw_triangles(quad, 6, r, g, b, 1.0f);
3. After that line, inject disc caps using variables already in scope:
       stroke  -- half-width * 2 (cap.radius1 * 2 * camera.zoom)
       sx1,sy1 / sx2,sy2 -- screen endpoints
       r,g,b  -- fighter colour (from lambda-captured f.fighter)
"""

import pathlib
import re
import sys

SCREENS = pathlib.Path("core/app/screens.cpp")
src = SCREENS.read_text(encoding="utf-8")
orig = src

# -- idempotency guard ---------------------------------------------------
GUARD = "// R3 fix -- disc caps at capsule endpoints"
if GUARD in src:
    print("screens.cpp already has R3 fix -- nothing to do")
    sys.exit(0)

# -- 1.  Find the draw_capsules lambda -----------------------------------
# The lambda is declared as:
#   auto draw_capsules = [&camera, &ren](const sf2::scene::FightFighter& f) {
LAMBDA_START = "auto draw_capsules = [&camera, &ren]"
start_pos = src.find(LAMBDA_START)
if start_pos < 0:
    print(f"ERROR: draw_capsules lambda not found ({LAMBDA_START!r})",
          file=sys.stderr)
    sys.exit(1)
print(f"OK: found draw_capsules at char {start_pos}")

# -- 2.  Within the lambda, find the quad draw: --------------------------
#   ren.draw_triangles(quad, 6, r, g, b, 1.0f);
# Use the first occurrence after the lambda start (before the next }; that
# closes the lambda, but it's safer to just take the first match).
# Also accept a trailing comma variant (some build variants have 0.95f alpha).
QUAD_PAT = re.compile(
    r"ren\.draw_triangles\(quad,\s*6,\s*r,\s*g,\s*b,\s*[^)]+\);"
)
m = QUAD_PAT.search(src, start_pos)
if m is None:
    # Fallback: any 6-triangle draw named quad inside the lambda area.
    QUAD_PAT2 = re.compile(r"ren\.draw_triangles\(quad,")
    m = QUAD_PAT2.search(src, start_pos)
if m is None:
    print("ERROR: quad draw not found inside draw_capsules", file=sys.stderr)
    sys.exit(1)
quad_line_end = src.index("\n", m.end()) + 1
print(f"OK: found quad draw at char {m.start()}")

# -- 3.  Inject disc caps after the quad draw ----------------------------
# Variables in scope at that point:
#   stroke  = cap.radius1 * 2.0f * camera.zoom   (thick quad half-width * 2)
#   sx1, sy1 = screen position of endpoint 1 (bone end1)
#   sx2, sy2 = screen position of endpoint 2 (bone end2)
#   r, g, b  = fighter colour (f.fighter.color_r/g/b)
CAPS_CODE = (
    "        // R3 fix -- disc caps at capsule endpoints (JS Dk / draw_capsules circles).\n"
    "        {\n"
    "            constexpr float kCapPi  = 3.14159265358979323846f;\n"
    "            constexpr int   kCapSeg = 12;\n"
    "            const float cap_r = stroke * 0.5f;\n"
    "            const float step  = 2.0f * kCapPi / static_cast<float>(kCapSeg);\n"
    "            const float cap_pts[2][2] = {{sx1, sy1}, {sx2, sy2}};\n"
    "            for (int _ep = 0; _ep < 2; ++_ep) {\n"
    "                const float dcx = cap_pts[_ep][0];\n"
    "                const float dcy = cap_pts[_ep][1];\n"
    "                for (int _s = 0; _s < kCapSeg; ++_s) {\n"
    "                    const float a0 = static_cast<float>(_s)     * step;\n"
    "                    const float a1 = static_cast<float>(_s + 1) * step;\n"
    "                    float _tri[6] = {\n"
    "                        dcx,                      dcy,\n"
    "                        dcx + std::cos(a0)*cap_r, dcy + std::sin(a0)*cap_r,\n"
    "                        dcx + std::cos(a1)*cap_r, dcy + std::sin(a1)*cap_r,\n"
    "                    };\n"
    "                    ren.draw_triangles(_tri, 3, r, g, b, 1.0f);\n"
    "                }\n"
    "            }\n"
    "        }\n"
)
src = src[:quad_line_end] + CAPS_CODE + src[quad_line_end:]
print("OK: disc caps injected after quad draw")

if src == orig:
    print("Nothing changed")
    sys.exit(0)

SCREENS.write_text(src, encoding="utf-8")
print(f"OK: {SCREENS} patched successfully")

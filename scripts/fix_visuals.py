#!/usr/bin/env python3
"""Apply visual-correctness fixes identified in reference/traces/visual_debug.md.

R1 — Fighter projection parallax factor 0 -> 1  (screens.cpp ~L792)
  The FightScreen project lambda called world_to_screen_x(v[i], 0.0f).
  Factor 0 omits the Io-offset term (arena_center_x - center_x ~ 148 px),
  so fighters appear 148 px to the left of where they should be AND slide
  horizontally as the camera pans while the background stays fixed.
  Fix: factor 1 (foreground), matching fighter_probe/main.cpp:380 and the
  renderer.hpp:60 documentation.
  The draw_capsules lambda already used 1.0f (fix_draw_order.py patched it);
  only the triangle-vertex projection lambda is affected here.

KTX mips==0 — ASTC textures with numberOfMipmapLevels=0 (KTX1 auto-chain)
  were rejected by the `mips < 1` guard.  KTX1 spec: 0 means 'generate the
  full mip chain at load time'.  We only ever read mip level 0, so treating
  0 as 1 is safe and unlocks every ASTC UI / menu / controller atlas that
  ships with this value.

All fixes are idempotent — safe to run multiple times.
"""

import pathlib
import sys

FIXES = [
    # --- R1: FightScreen project lambda parallax factor 0 -> 1 ---------------
    {
        "file": "core/app/screens.cpp",
        "old": (
            "            out[i] = camera.world_to_screen_x(v[i], 0.0f);\n"
            "            out[i + 1] = camera.world_to_screen_y(v[i + 1]);"
        ),
        "new": (
            "            // Fighters are foreground objects (parallax factor 1).\n"
            "            // Factor 0 was a bug that omits the Io camera-offset term\n"
            "            // (~148 px), causing fighters to slide relative to the\n"
            "            // background as the camera pans. Fix per visual_debug.md R1.\n"
            "            out[i] = camera.world_to_screen_x(v[i], 1.0f);\n"
            "            out[i + 1] = camera.world_to_screen_y(v[i + 1]);"
        ),
        "desc": "screens.cpp: FightScreen project lambda parallax 0 -> 1",
    },
    # --- R1 consistency: demo apps (not shipped in game binary) --------------
    {
        "file": "app/ai_demo/main.cpp",
        "old": (
            "            out[i] = camera.world_to_screen_x(v[i], 0.0f);\n"
            "            out[i + 1] = camera.world_to_screen_y(v[i + 1]);"
        ),
        "new": (
            "            out[i] = camera.world_to_screen_x(v[i], 1.0f);  // foreground\n"
            "            out[i + 1] = camera.world_to_screen_y(v[i + 1]);"
        ),
        "desc": "ai_demo: project lambda parallax 0 -> 1 (consistency)",
    },
    {
        "file": "app/fight_demo/main.cpp",
        "old": (
            "            // Fighters are world-space objects at parallax factor 0 (the\n"
            "            // camera offset applies once, not twice).\n"
            "            out[i] = camera.world_to_screen_x(v[i], 0.0f);"
        ),
        "new": (
            "            // Fighters are foreground (parallax factor 1). The old comment\n"
            "            // about \"factor 0 applies the offset once\" was incorrect; see\n"
            "            // renderer.hpp:60 and visual_debug.md R1 for the full analysis.\n"
            "            out[i] = camera.world_to_screen_x(v[i], 1.0f);"
        ),
        "desc": "fight_demo: project lambda parallax 0 -> 1 + fix stale comment",
    },
    {
        "file": "app/fight_controller_demo/main.cpp",
        "old": (
            "            out[i] = camera.world_to_screen_x(v[i], 0.0f);\n"
            "            out[i + 1] = camera.world_to_screen_y(v[i + 1]);"
        ),
        "new": (
            "            out[i] = camera.world_to_screen_x(v[i], 1.0f);  // foreground\n"
            "            out[i + 1] = camera.world_to_screen_y(v[i + 1]);"
        ),
        "desc": "fight_controller_demo: project lambda parallax 0 -> 1",
    },
    # --- KTX mips==0: unlock ASTC atlases with auto mip chain ----------------
    {
        "file": "core/data/ktx.cpp",
        "old": (
            "    if (array_elements != 0 || faces != 1 || mips < 1) {\n"
            "        return false;\n"
            "    }"
        ),
        "new": (
            "    // KTX1 spec: numberOfMipmapLevels==0 means 'generate full mip chain at\n"
            "    // load time'; treat it as 1 here -- we only ever read mip level 0.\n"
            "    // The old `|| mips < 1` guard incorrectly rejected those textures,\n"
            "    // silently blocking every ASTC atlas that ships with mips=0.\n"
            "    if (array_elements != 0 || faces != 1) {\n"
            "        return false;\n"
            "    }"
        ),
        "desc": "ktx.cpp: accept mips==0 (KTX1 auto chain) -- unlocks ASTC UI atlases",
    },
]

errors = 0
for fix in FIXES:
    path = pathlib.Path(fix["file"])
    if not path.exists():
        print(f"[skip] {path}: not found (run from repo root?)")
        continue
    content = path.read_text(encoding="utf-8")
    if fix["new"] in content:
        print(f"[ok]   {path}: {fix['desc']} -- already applied")
        continue
    if fix["old"] not in content:
        print(
            f"[warn] {path}: {fix['desc']} -- old pattern not found; "
            "may already be applied or file was restructured"
        )
        continue
    content = content.replace(fix["old"], fix["new"], 1)
    path.write_text(content, encoding="utf-8")
    print(f"[fix]  {path}: {fix['desc']}")

if errors:
    sys.exit(1)
print("fix_visuals.py: done")

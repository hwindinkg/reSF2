#!/usr/bin/env python3
"""Apply the FightScreen draw-order fix to core/app/screens.cpp.

JS reference: ev.Gf L845 — enemy is drawn at z=-0.001 (behind),
player is drawn at z=0 (on top).  The C++ port had them reversed.

Run from the repo root:  python3 scripts/fix_draw_order.py
Idempotent: if the fix is already applied the script exits cleanly.
"""
import pathlib
import sys

SRC = pathlib.Path('core/app/screens.cpp')

if not SRC.exists():
    print(f'ERROR: {SRC} not found — run from the repo root.', file=sys.stderr)
    sys.exit(1)

content = SRC.read_text(encoding='utf-8')

# --- Pattern that exists in the un-patched file --------------------------------
OLD = (
    '    draw_capsules(fight_->player());\n'
    '    draw_capsules(fight_->enemy());\n'
    '    ren.draw_triangles(pv.data(), pv.size() / 2, fight_->player().fighter.color_r(),\n'
    '                       fight_->player().fighter.color_g(), fight_->player().fighter.color_b());\n'
    '    ren.draw_triangles(ev.data(), ev.size() / 2, fight_->enemy().fighter.color_r(),\n'
    '                       fight_->enemy().fighter.color_g(), fight_->enemy().fighter.color_b());\n'
)

# --- Corrected draw order matching the JS original ----------------------------
NEW = (
    '    // JS draw order (ev.Gf L845): enemy z=-0.001 (behind), player z=0 (on top).\n'
    '    // Draw enemy FIRST, then player on top \xe2\x80\x94 matching the original.\n'
    '    draw_capsules(fight_->enemy());\n'
    '    ren.draw_triangles(ev.data(), ev.size() / 2, fight_->enemy().fighter.color_r(),\n'
    '                       fight_->enemy().fighter.color_g(), fight_->enemy().fighter.color_b());\n'
    '    draw_capsules(fight_->player());\n'
    '    ren.draw_triangles(pv.data(), pv.size() / 2, fight_->player().fighter.color_r(),\n'
    '                       fight_->player().fighter.color_g(), fight_->player().fighter.color_b());\n'
)

if NEW in content:
    print(f'{SRC}: draw-order fix already applied — nothing to do.')
    sys.exit(0)

if OLD not in content:
    print(f'ERROR: expected pattern not found in {SRC}.', file=sys.stderr)
    print('The file may have been restructured; please apply the fix manually.', file=sys.stderr)
    sys.exit(1)

patched = content.replace(OLD, NEW, 1)
assert patched.count(OLD) == 0, 'Replacement left residual OLD pattern'
SRC.write_text(patched, encoding='utf-8')
print(f'{SRC}: draw-order fix applied successfully.')

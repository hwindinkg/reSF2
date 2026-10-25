#!/usr/bin/env python3
"""ui_diff.py - region-level diff between the port and the oracle captures.

Both sides ship the same screenshots:
  oracle: reference/traces/oracle_matrix/<screen>.png
  port:   reference/traces/port_matrix/<screen>.png
(and reference/traces/ui/{oracle,port}_*.png for the tour shots)

This tool reports WHERE the two differ, not just that they do: it splits the
image into a grid, ranks the cells by mean absolute difference, prints the worst
ones with their pixel rects, and writes a heatmap next to the report so the
region can be inspected by eye.

Usage:
  python reference/tools/ui_diff.py fight_stance [--grid 16x9] [--top 12]
  python reference/tools/ui_diff.py --a A.png --b B.png [--label name]
Exit: 0 always (it is a report); --fail-on N returns 1 when the worst cell is
above N (for wiring into a gate later).
"""

import argparse
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ORACLE = os.path.join(ROOT, "reference", "traces", "oracle_matrix")
PORT = os.path.join(ROOT, "reference", "traces", "port_matrix")
UI = os.path.join(ROOT, "reference", "traces", "ui")
OUT = os.path.join(ROOT, ".harness", "artifacts", "ui_diff")


def load(path):
    img = Image.open(path).convert("RGB")
    return np.asarray(img, dtype=np.int16)


def resolve(name):
    """Find the oracle/port pair for a screen name."""
    cands = [
        (os.path.join(ORACLE, name + ".png"), os.path.join(PORT, name + ".png")),
        (os.path.join(UI, "oracle_" + name + ".png"),
         os.path.join(UI, "port_" + name + ".png")),
        (os.path.join(UI, "oracle_" + name + ".png"),
         os.path.join(UI, "port_" + name.lstrip("c_") + ".png")),
    ]
    for a, b in cands:
        if os.path.exists(a) and os.path.exists(b):
            return a, b
    return None, None


def heatmap_png(a, b, path):
    """Write a side-by-side image with the difference amplified."""
    h = min(a.shape[0], b.shape[0])
    w = min(a.shape[1], b.shape[1])
    d = np.abs(a[:h, :w] - b[:h, :w]).astype(np.uint8)
    # amplify x4 so small offsets are visible
    d = np.clip(d.astype(np.int16) * 4, 0, 255).astype(np.uint8)
    out = np.concatenate([a[:h, :w].astype(np.uint8), b[:h, :w].astype(np.uint8), d],
                         axis=1)
    Image.fromarray(out).save(path)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("screen", nargs="?", default="")
    ap.add_argument("--a", default="")
    ap.add_argument("--b", default="")
    ap.add_argument("--label", default="")
    ap.add_argument("--grid", default="16x9")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--fail-on", type=float, default=-1.0)
    a = ap.parse_args()

    if a.a and a.b:
        pa, pb = a.a, a.b
        label = a.label or "pair"
    else:
        if not a.screen:
            print("give a screen name or --a/--b")
            return 2
        pa, pb = resolve(a.screen)
        label = a.screen
        if not pa:
            print("no oracle/port pair for %r" % a.screen)
            return 2

    ia, ib = load(pa), load(pb)
    h = min(ia.shape[0], ib.shape[0])
    w = min(ia.shape[1], ib.shape[1])
    ia, ib = ia[:h, :w], ib[:h, :w]

    gx, gy = (int(v) for v in a.grid.lower().split("x"))
    cw, ch = w // gx, h // gy
    cells = []
    for cy in range(gy):
        for cx in range(gx):
            sa = ia[cy * ch:(cy + 1) * ch, cx * cw:(cx + 1) * cw]
            sb = ib[cy * ch:(cy + 1) * ch, cx * cw:(cx + 1) * cw]
            mad = float(np.abs(sa - sb).mean())
            cells.append((mad, cx * cw, cy * ch, cw, ch))
    cells.sort(reverse=True)

    total = float(np.abs(ia - ib).mean())
    print("=== ui diff %s ===" % label)
    print("oracle: %s" % os.path.relpath(pa, ROOT))
    print("port  : %s" % os.path.relpath(pb, ROOT))
    print("size %dx%d  grid %dx%d  mean|diff| = %.2f" % (w, h, gx, gy, total))
    print("worst cells (mean abs diff, rect x,y,w,h):")
    for mad, x, y, cw2, ch2 in cells[:a.top]:
        print("  %7.2f   %4d,%4d %4dx%-4d" % (mad, x, y, cw2, ch2))

    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, label.replace("/", "_") + "_heat.png")
    heatmap_png(ia, ib, out)
    print("heatmap (oracle | port | diff x4): %s" % os.path.relpath(out, ROOT))

    if a.fail_on >= 0 and cells and cells[0][0] > a.fail_on:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

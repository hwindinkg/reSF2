#!/usr/bin/env python3
"""ui_diff.py - screenshot pixel-diff gate for the UI harness (Dojo wave).

Compares oracle vs port screenshots at 1280x720 (asserts size) and reports
the % of pixels whose max channel abs-delta exceeds --threshold (default
12/255). Writes a red-overlay diff PNG + a side-by-side PNG per pair into
--out (default reference/traces/ui/). Exit 0 always (numbers are the gate;
MASTER_TODO holds the per-screen thresholds) unless inputs are missing.

Usage:
  python reference/tools/ui_diff.py --pairs ui_dojo:dojo,ui_map:map [...]
  python reference/tools/ui_diff.py --all   (uses the state list below)

Inputs: reference/traces/ui/oracle_<name>.png + port_<name>.png.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

STATES = ["dojo", "dojo_modal", "map", "shop", "shop_tab2", "profile",
          "fight", "pause", "results", "settings"]

W, H = 1280, 720


def load(path):
    im = Image.open(path).convert("RGB")
    if im.size != (W, H):
        raise SystemExit("bad size %s: %s (want 1280x720)" % (path, im.size))
    return np.asarray(im).astype(np.int16)


def diff_pair(odir, name, thresh, port_name=None):
    pname = port_name or name
    a = load(os.path.join(odir, "oracle_%s.png" % name))
    b = load(os.path.join(odir, "port_%s.png" % pname))
    d = np.abs(a - b).max(axis=2)
    mask = d > thresh
    pct = 100.0 * mask.mean()
    overlay = np.asarray(Image.open(
        os.path.join(odir, "port_%s.png" % pname)).convert("RGB")).copy()
    overlay[mask] = [255, 0, 0]
    Image.fromarray(overlay).save(os.path.join(odir, "diff_%s.png" % name))
    side = np.concatenate(
        [np.asarray(Image.open(os.path.join(odir, "oracle_%s.png" % name)).convert("RGB")),
         np.asarray(Image.open(os.path.join(odir, "port_%s.png" % pname)).convert("RGB"))],
        axis=1)
    Image.fromarray(side).save(os.path.join(odir, "side_%s.png" % name))
    return pct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="reference/traces/ui")
    ap.add_argument("--threshold", type=int, default=12)
    ap.add_argument("--pairs", default="")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()
    names = STATES if args.all else [p.split(":")[0] for p in args.pairs.split(",") if p]
    pmap = {}
    for p in args.pairs.split(","):
        if ":" in p:
            a, b = p.split(":", 1)
            pmap[a] = b or a
    if not names:
        raise SystemExit("no states (use --all or --pairs oracle:port,...)")
    print("screen,pct_over_threshold")
    for n in names:
        try:
            print("%s,%.2f" % (n, diff_pair(args.dir, n, args.threshold, pmap.get(n))))
        except FileNotFoundError as e:
            print("%s,MISSING %s" % (n, e))
        except SystemExit as e:
            print("%s,ERROR %s" % (n, e))


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""location_lint.py - read-only audit for basename-fallback resolution risk.

Scans the location loading path:

  - core/scene/location_scene.cpp (ClassName -> frame map, later-packs-win)
  - core/app/app.cpp (decode_atlas_any prefix scan)
  - core/app/screens.cpp (per-stage prefix scan + global texture_alias map)
  - assets/**/params.xml (ClassName refs per stage, e.g. background_1 /
    layer_3_1 / layer_3_2 / left / right / pixel_1)
  - assets under Textures/Locations/ analogues + location_effects/
    (exact file-basename collisions across different dirs)
  - reference/www/res/locations/*/*.json (atlas frame filename collisions
    across different stages)

Exact-path rule (Eclipse CONTENT.md): a texture/effect MUST resolve by its
exact stage-qualified path (``<stage>/<file>`` or ``<stage>/<ClassName>``),
never by bare basename. Any basename-fallback lookup (directory scan +
``filename()`` / ``rfind(prefix)`` match, or a process-global
``frame-name -> texture`` map) mis-resolves as soon as two stages share a
basename — and they do, pervasively (background_1 in 53 stages, left/right
in 52, pixel_1 in 54, 161 shared atlas frame names across www stages).

This tool only READS. It never writes, never overrides game JS, never
touches core/. Exit 0 = clean (no cross-stage basename sharing found),
exit 1 = FAIL (collisions found — a basename fallback would mis-resolve).

Usage:
  python reference/tools/location_lint.py [--root .]
  python reference/tools/location_lint.py --focus background_1,left,pixel_1
"""
import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

# C++ files that form the location loading path.
LOADER_SOURCES = [
    "core/scene/location_scene.cpp",
    "core/scene/location_scene.hpp",
    "core/app/app.cpp",
    "core/app/screens.cpp",
]

# Substrings that indicate basename/prefix-fallback resolution in C++.
# (Informational only — the FAIL verdict is driven by actual collisions,
# since the patterns below are confirmed present in the current tree.)
RISKY_PATTERNS = [
    "directory_iterator",
    ".filename()",
    "rfind(stem",
    "rfind(prefix",
    "rfind(loc_prefix",
    "rfind(location_",
    "texture_alias",
    "frames[f.name] =",
]

# ClassNames called out by the audit request.
FOCUS_NAMES = {"background_1", "layer3", "layer_3_1", "layer_3_2",
               "left", "right", "pixel_1", "pixel"}

# Asset subtrees analogous to Textures/ / Locations/ / Location_effects/.
# Both resolution variants (1536 vs 768) are included on purpose: they share
# basenames by design, so a basename-only lookup cannot tell them apart.
ASSET_SCAN_DIRS = [
    "assets/1536/location_effects",
    "assets/768/location_effects",
    "assets/assets/1536/location_effects",
    "assets/assets/768/location_effects",
    "assets/1536/textures",
    "assets/768/textures",
    "assets/assets/1536/textures",
    "assets/assets/768/textures",
    "assets/locations",
]


def repo_root(explicit):
    if explicit:
        return os.path.abspath(explicit)
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.dirname(os.path.dirname(here))


def scan_sources(root):
    """Report basename-fallback idioms in the loader sources (read-only)."""
    hits = []
    for rel in LOADER_SOURCES:
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            hits.append((rel, 0, "MISSING", ""))
            continue
        with open(path, encoding="utf-8", errors="replace") as fh:
            for i, line in enumerate(fh, 1):
                for pat in RISKY_PATTERNS:
                    if pat in line:
                        hits.append((rel, i, pat, line.strip()[:160]))
    return hits


def scan_asset_classnames(root):
    """Map ClassName -> set of stage dirs from every assets/**/params.xml."""
    byname = defaultdict(set)
    n_params = 0
    for dirpath, _dirs, files in os.walk(os.path.join(root, "assets")):
        if "params.xml" not in files:
            continue
        path = os.path.join(dirpath, "params.xml")
        try:
            tree = ET.parse(path)
        except ET.ParseError:
            continue
        n_params += 1
        stage = os.path.relpath(dirpath, os.path.join(root, "assets"))
        for el in tree.getroot().iter():
            cls = el.get("ClassName")
            if cls:
                byname[cls].add(stage)
    shared = {k: sorted(v) for k, v in byname.items() if len(v) > 1}
    return n_params, byname, shared


def scan_www_frames(root):
    """Map atlas frame filename -> set of stage/json files under www."""
    loc = os.path.join(root, "reference", "www", "res", "locations")
    byframe = defaultdict(set)
    n_json = 0
    n_stages = 0
    if not os.path.isdir(loc):
        return 0, 0, byframe, {}
    for stage in sorted(os.listdir(loc)):
        sdir = os.path.join(loc, stage)
        if not os.path.isdir(sdir):
            continue
        n_stages += 1
        for name in os.listdir(sdir):
            if not name.endswith(".json"):
                continue
            path = os.path.join(sdir, name)
            try:
                with open(path, encoding="utf-8") as fh:
                    doc = json.load(fh)
            except (ValueError, OSError):
                continue
            n_json += 1
            frames = doc.get("frames", [])
            if isinstance(frames, dict):
                frames = [{"filename": k} for k in frames.keys()]
            for fr in frames:
                if isinstance(fr, dict) and fr.get("filename"):
                    byframe[fr["filename"]].add(stage + "/" + name)
    cross = {}
    for k, v in byframe.items():
        stages = {s.split("/")[0] for s in v}
        if len(stages) > 1:
            cross[k] = sorted(v)
    return n_stages, n_json, byframe, cross


def scan_asset_basenames(root):
    """Map lower-cased file basename -> set of full paths under scan dirs."""
    bybase = defaultdict(set)
    for rel in ASSET_SCAN_DIRS:
        top = os.path.join(root, rel)
        if not os.path.isdir(top):
            continue
        for dirpath, _dirs, files in os.walk(top):
            for f in files:
                bybase[f.lower()].add(os.path.join(dirpath, f))
    coll = {k: sorted(v) for k, v in bybase.items() if len(v) > 1}
    return bybase, coll


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=None,
                    help="repo root (default: inferred from script path)")
    ap.add_argument("--focus", default=",".join(sorted(FOCUS_NAMES)),
                    help="comma-separated ClassNames to spotlight")
    ap.add_argument("--show", type=int, default=20,
                    help="max collisions shown per section (default 20)")
    args = ap.parse_args()
    root = repo_root(args.root)
    focus = {s for s in args.focus.split(",") if s}

    src_hits = scan_sources(root)
    n_params, _byname, shared = scan_asset_classnames(root)
    n_stages, n_json, byframe, cross = scan_www_frames(root)
    _bybase, basecoll = scan_asset_basenames(root)

    print("== location_lint (read-only audit) ==")
    print("root: %s" % root)
    print("loader sources scanned: %d (%d risky-idiom hits)" %
          (len(LOADER_SOURCES), len([h for h in src_hits if h[1] > 0])))
    for rel, lineno, pat, line in src_hits:
        if lineno == 0:
            print("  MISSING %s" % rel)
    # Show a compact per-pattern count plus the two load-bearing sites.
    counts = defaultdict(int)
    for _rel, lineno, pat, _line in src_hits:
        if lineno > 0:
            counts[pat] += 1
    for pat in RISKY_PATTERNS:
        if counts.get(pat):
            print("  pattern %-20s x%d" % (repr(pat), counts[pat]))

    print("assets params.xml files: %d, distinct ClassNames shared "
          "across stages: %d" % (n_params, len(shared)))
    shown = 0
    for name in sorted(shared):
        if shown >= args.show:
            break
        mark = "  <-- FOCUS" if name in focus else ""
        print("  COLLIDE ClassName %-16s %d stages e.g. %s%s" %
              (repr(name), len(shared[name]), shared[name][:3], mark))
        shown += 1
    if len(shared) > shown:
        print("  ... and %d more shared ClassNames" % (len(shared) - shown))
    for name in sorted(focus):
        if name in shared:
            print("  focus %-12s %d stages" % (repr(name), len(shared[name])))

    print("www stages: %d, atlas jsons: %d, cross-stage frame "
          "collisions: %d" % (n_stages, n_json, len(cross)))
    shown = 0
    for name in sorted(cross):
        if shown >= args.show:
            break
        print("  COLLIDE frame %-24s e.g. %s" % (repr(name), cross[name][:3]))
        shown += 1
    if len(cross) > shown:
        print("  ... and %d more shared frames" % (len(cross) - shown))
    if "pixel_1" in byframe:
        holders = {s.split("/")[0] for s in byframe["pixel_1"]}
        print("  pixel_1 in %d www stages (solid-fill sentinel, "
              "per-stage by exact path)" % len(holders))

    print("asset basename collisions (same basename, different dirs): %d" %
          len(basecoll))
    shown = 0
    for name in sorted(basecoll):
        if shown >= args.show:
            break
        print("  COLLIDE file %-24s %d paths e.g. %s" %
              (repr(name), len(basecoll[name]),
               [os.path.relpath(p, root) for p in basecoll[name][:2]]))
        shown += 1
    if len(basecoll) > shown:
        print("  ... and %d more basename collisions" % (len(basecoll) - shown))

    n_coll = len(shared) + len(cross) + len(basecoll)
    if n_coll:
        print("LOCATION_LINT: FAIL (%d collision groups: %d ClassName + "
              "%d frame + %d basename; any basename fallback under "
              "Textures/Locations/Location_effects would mis-resolve)" %
              (n_coll, len(shared), len(cross), len(basecoll)))
        return 1
    print("LOCATION_LINT: PASS (no cross-stage basename sharing found)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

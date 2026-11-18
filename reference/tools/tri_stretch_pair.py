#!/usr/bin/env python3
"""tri_stretch_pair.py - worst stretched MESH triangle, port vs oracle, one scenario.

Why this tool exists: the player's screenshot shows a giant black triangle across
the bamboo grove. Capsule-EDGE metrics (`SF2_TRACE_EDGES`) cannot see it; the
visible deformation comes from `<Figures Type="Triangle">` mesh triangles, whose
vertices are bones. This measures exactly those, on BOTH sides of one scenario,
so "the port stretches it more" can be answered on the player's own fight instead
of an unrelated one.

Inputs (written by `harness/gates.py --full` for every scenario):

  .harness/artifacts/<scenario>/native_pose.jsonl   - port, `--dump-pose`
      header `{"t":"bones","Me":[names...],"Enemy":[names...]}` + frame rows
  .harness/artifacts/<scenario>/oracle_pose.jsonl   - JS oracle, frame rows only

Geometry (bind/rest coordinates and the triangle list) comes from the game's own
model XMLs: `<Nodes>` carry X/Y, `<Figures Type="Triangle" Node1..3>` carry the
three bone names. A triangle is measured only when all three nodes exist in that
fighter's bone list, so what is reported is the worst ACTIVE mesh triangle.

Roles are crossed exactly as `reference/tools/compare_pose.py` crosses them: the
native `Me` (205 bones) is the oracle's `Enemy`, the native `Enemy` (456) is the
oracle's `Me`. The mapping is derived from the bone counts in the two traces, so
it fails loudly rather than silently comparing the wrong fighter.

Metric: live longest side / bind longest side, per triangle. The ratio is
scale-invariant, and both sides solve the same XML, so a higher number on one
side means that side really stretches the mesh more.

Usage:
  python reference/tools/tri_stretch_pair.py [scenario]     # default: survival

Exit codes: 0 report produced, 2 a required trace is missing.
"""

import glob
import io
import json
import os
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART_ROOT = os.path.join(ROOT, ".harness", "artifacts")
MODELS = os.path.join(ROOT, "assets", "files", "assets", "models")
TOP = 8  # worst triangles printed per fighter


def parse_models():
    """[(file, {node name: (x, y)}, [(n1, n2, n3), ...]), ...] from every model."""
    out = []
    for path in glob.glob(os.path.join(MODELS, "*.xml")):
        try:
            txt = io.open(path, encoding="utf-8", errors="replace").read()
            tree = ET.fromstring(txt.encode("utf-8"))
        except (OSError, ET.ParseError):
            continue
        coords = {}
        tris = []
        for el in tree.iter():
            if el.tag == "Nodes":
                for nd in el:
                    if nd.get("X") is not None and nd.get("Y") is not None:
                        coords[nd.tag] = (float(nd.get("X")), float(nd.get("Y")))
            elif el.tag == "Figures":
                for fg in el:
                    if fg.get("Type") != "Triangle":
                        continue
                    a, b, c = fg.get("Node1"), fg.get("Node2"), fg.get("Node3")
                    if a and b and c:
                        tris.append((a, b, c))
        if tris and coords:
            out.append((os.path.basename(path), coords, tris))
    return out


def load_trace(path):
    """(bones header or None, [frame rows])."""
    header, frames = None, []
    if not os.path.exists(path):
        return header, frames
    with io.open(path, encoding="utf-8", errors="replace") as fin:
        for line in fin:
            i = line.find('{"t":"')
            if i < 0:
                continue
            try:
                row = json.loads(line[i:].strip())
            except ValueError:
                continue
            if row.get("t") == "bones":
                header = row
            elif row.get("t") == "frame":
                frames.append(row)
    return header, frames


def dist(a, b):
    return ((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2) ** 0.5


def resolve(names, models):
    """Triangles measurable in this fighter: (index triple, bind longest, label)."""
    index = {n: i for i, n in enumerate(names)}
    seen = set()
    out = []
    for _fn, coords, tris in models:
        for t in tris:
            if t in seen or not all(n in index and n in coords for n in t):
                continue
            seen.add(t)
            bind = max(dist(coords[t[0]], coords[t[1]]),
                       dist(coords[t[1]], coords[t[2]]),
                       dist(coords[t[2]], coords[t[0]]))
            if bind < 1e-3:
                continue
            out.append(((index[t[0]], index[t[1]], index[t[2]]), bind, "/".join(t)))
    return out


def worst_per_triangle(frames, role, tris):
    """{label: max(live longest side / bind longest side)} over the trace."""
    best = {}
    for fr in frames:
        for fg in fr.get("fighters", []):
            if fg.get("id") != role:
                continue
            bones = fg.get("bones")
            if not bones:
                continue
            for tri, bind, label in tris:
                a, b, c = bones[tri[0]], bones[tri[1]], bones[tri[2]]
                live = max(dist(a, b), dist(b, c), dist(c, a))
                ratio = live / bind
                if ratio > best.get(label, 0.0):
                    best[label] = ratio
    return best


def main(argv):
    scenario = argv[1] if len(argv) > 1 else "survival"
    art = os.path.join(ART_ROOT, scenario)
    nhead, nframes = load_trace(os.path.join(art, "native_pose.jsonl"))
    _ohead, oframes = load_trace(os.path.join(art, "oracle_pose.jsonl"))
    if nhead is None or not nframes or not oframes:
        print("missing traces under %s" % art, file=sys.stderr)
        print("run: python harness/gates.py --only diff:%s  (fills the artifacts)" % scenario,
              file=sys.stderr)
        return 2
    models = parse_models()
    print("scenario=%s  models=%d  native frames=%d  oracle frames=%d"
          % (scenario, len(models), len(nframes), len(oframes)))
    by_len = {len(fg["bones"]): fg["id"] for fg in oframes[0]["fighters"]}
    for nrole in ("Me", "Enemy"):
        names = nhead[nrole]
        orole = by_len.get(len(names))
        if orole is None:
            print("no oracle role with %d bones - cannot pair %s" % (len(names), nrole))
            continue
        tris = resolve(names, models)
        nb = worst_per_triangle(nframes, nrole, tris)
        ob = worst_per_triangle(oframes, orole, tris)
        print("\n== native %s (%d bones, %d active triangles) <-> oracle %s"
              % (nrole, len(names), len(tris), orole))
        print("   %-58s %8s %8s %6s" % ("triangle", "port", "oracle", "delta"))
        rows = sorted(set(nb) | set(ob), key=lambda k: -max(nb.get(k, 0.0), ob.get(k, 0.0)))
        for k in rows[:TOP]:
            p, o = nb.get(k, 0.0), ob.get(k, 0.0)
            print("   %-58s %8.2f %8.2f %+6.2f" % (k, p, o, p - o))
        worse = [k for k in rows if nb.get(k, 0.0) > ob.get(k, 0.0) + 0.01]
        print("   triangles the PORT stretches more: %d of %d" % (len(worse), len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

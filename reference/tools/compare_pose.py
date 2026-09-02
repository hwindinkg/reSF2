#!/usr/bin/env python3
"""
compare_pose.py - dual-side pose trace comparator (native port vs web oracle).

Compares the native Shadow Fight 2 port's pose trace
(reference/traces/native_pose.jsonl, produced by
`game.exe --fight --headless N --dump-pose M`) against the web oracle's trace
(reference/traces/oracle_pose.jsonl, produced by the JS-side tracer).

Both files share one JSONL data model:

  frame: {"t":"frame","f":N,"phase":P,"round":R,"timer":T,
           "cam":{"cx":..,"cy":..,"zoom":..},
           "fighters":[{"id":"Me","x":..,"y":..,"fx":1,"clip":"..","cf":C,
                        "sub":S,"subn":SN,"bones":[[x,y],...]},
                       {"id":"Enemy",...}]}
  clip:  {"t":"clip","name":"..","frames":F,"bones":B,
           "data":[[[x16,y16,z16],...],...]}   (1/16 fixed-point ints)

The two sides are NOT frame-aligned by construction: the oracle boots straight
into an auto-playing demo fight, the native --fight run is manual-input with
the same intro. Both play the StartStance intro (133 frames) then continue;
the intro is expected to MATCH, later frames may diverge once AI/behavior
differs (that divergence itself is a finding). Frames are therefore aligned by
the (phase, clip, cf) sequence, not by frame number.

Usage:
  python compare_pose.py [--native PATH] [--oracle PATH] [--tolerance PX]
                         [--out FILE] [--clip NAME]
                         [--map-native-me-to auto|oracle-enemy]
                         [--coord-transform none|center]

Exit codes:
  0  report produced (oracle may be missing - native-only validation)
  2  a required input is missing (native trace / native clip file)

Deterministic output: no timestamps, all lists sorted.
"""

import argparse
import json
import math
import os
import sys
from collections import Counter, defaultdict
import glob

DEFAULT_NATIVE = os.path.join("reference", "traces", "native_pose.jsonl")
DEFAULT_ORACLE = os.path.join("reference", "traces", "oracle_pose.jsonl")
DEFAULT_OUT = os.path.join("reference", "traces", "pose_gap_report.txt")
DEFAULT_TOLERANCE = 5.0  # world units; both sides dump world-space bone positions
WORST_FRAMES = 10
WORST_BONES = 10
TOP_BONES = 20
UNALIGNED_SAMPLE = 20
COVERAGE_WARN = 0.70

# clip name map (alias table) - case-insensitive, .bytes suffix stripped before lookup
# minimal map from gap evidence plus generic fists1_/fists2_ handling in normalize_clip_name
CLIP_ALIASES = {
    "stance_1": "fistsstartstance-left",
    "stance_2": "fistsstartstance-right",
    "fists1_stance_idle": "fistsstartstanceidle-left",
    "fists2_stance_idle": "fistsstartstanceidle-right",
}


def normalize_clip_name(name):
    """Normalize clip name for alignment key comparison.

    Steps (deterministic, ASCII):
      - None / non-string -> ""
      - strip whitespace
      - strip trailing .bytes (case-insensitive)
      - lowercased
      - explicit alias table (stance_1 etc)
      - generic fists1_/fists2_ -> fists prefix
    Both sides are normalized so stance_1 and FistsStartStance-Left converge to
    the same token "fistsstartstance-left".
    """
    if not isinstance(name, str):
        return ""
    s = name.strip()
    if not s:
        return ""
    # strip .bytes suffix case-insensitive
    if s.lower().endswith(".bytes"):
        s = s[:-6]
    low = s.lower()
    if low in CLIP_ALIASES:
        return CLIP_ALIASES[low]
    # generic fists1_/fists2_ -> fists
    if low.startswith("fists1_"):
        return "fists" + low[7:]
    if low.startswith("fists2_"):
        return "fists" + low[7:]
    return low


# ---------------------------------------------------------------------------
# loading
# ---------------------------------------------------------------------------

def load_jsonl(path):
    """Parse a JSONL trace. Returns (frames, clips_by_name, malformed_count).

    Malformed lines are skipped and counted, never fatal.
    """
    frames = []
    clips = {}
    malformed = 0
    try:
        fh = open(path, "r", encoding="utf-8")
    except OSError as exc:
        raise FileNotFoundError("cannot open %s: %s" % (path, exc.strerror or exc))
    with fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                malformed += 1
                continue
            if not isinstance(obj, dict):
                malformed += 1
                continue
            t = obj.get("t")
            if t == "frame":
                frames.append(obj)
            elif t == "clip":
                name = obj.get("name")
                if isinstance(name, str) and name:
                    clips[name] = obj
                else:
                    malformed += 1
            else:
                malformed += 1
    return frames, clips, malformed


# ---------------------------------------------------------------------------
# alignment
# ---------------------------------------------------------------------------

def get_fighter(fr, fid):
    """Find a fighter by id; fall back to position (Me first, Enemy second)."""
    fighters = fr.get("fighters")
    if not isinstance(fighters, list):
        return None
    for f in fighters:
        if isinstance(f, dict) and f.get("id") == fid:
            return f
    if fid == "Me" and fighters:
        return fighters[0] if isinstance(fighters[0], dict) else None
    if fid == "Enemy" and len(fighters) > 1:
        return fighters[1] if isinstance(fighters[1], dict) else None
    return None


def frame_key(fr, fighter_id="Me", normalized=True):
    """Alignment key: (phase, clip, cf).

    cf is rounded to int - the oracle may emit a float subframe playhead while
    the native side dumps an int clip frame. The driver fighter is selectable
    (Me vs Enemy) for role swapping; clip is optionally normalized via
    normalize_clip_name so archive names (stance_1) match oracle labels
    (FistsStartStance-Left).
    """
    phase = fr.get("phase")
    try:
        phase = int(phase)
    except (TypeError, ValueError):
        phase = -1
    fighter = get_fighter(fr, fighter_id)
    if not isinstance(fighter, dict):
        # fallback to first fighter (for backward compat)
        fighters = fr.get("fighters")
        fighter = fighters[0] if isinstance(fighters, list) and fighters and isinstance(fighters[0], dict) else {}
    clip = fighter.get("clip")
    if not isinstance(clip, str):
        clip = ""
    if normalized:
        clip = normalize_clip_name(clip)
    cf = fighter.get("cf")
    try:
        cf = int(round(float(cf)))
    except (TypeError, ValueError):
        cf = -1
    return (phase, clip, cf)


def frame_key_raw(fr, fighter_id="Me"):
    """Raw (non-normalized) clip name for reporting."""
    phase = fr.get("phase")
    try:
        phase = int(phase)
    except (TypeError, ValueError):
        phase = -1
    fighter = get_fighter(fr, fighter_id)
    if not isinstance(fighter, dict):
        fighters = fr.get("fighters")
        fighter = fighters[0] if isinstance(fighters, list) and fighters and isinstance(fighters[0], dict) else {}
    clip = fighter.get("clip")
    if not isinstance(clip, str):
        clip = ""
    cf = fighter.get("cf")
    try:
        cf = int(round(float(cf)))
    except (TypeError, ValueError):
        cf = -1
    return (phase, clip, cf)


def detect_swap_auto(native_frames, oracle_frames):
    """Auto-detect if native Me (205) vs oracle Me (15 ragdoll) needs swapping.

    Returns True if oracle Me bone count is 15 while native Me is 205 (or any
    large divergence), suggesting oracle Enemy holds the real fighter.
    """
    def first_bone_count(frames, fid):
        for fr in frames:
            f = get_fighter(fr, fid)
            if f is None:
                continue
            bones = f.get("bones")
            if isinstance(bones, list) and bones:
                return len(bones)
        return None
    n_me = first_bone_count(native_frames, "Me")
    o_me = first_bone_count(oracle_frames, "Me")
    o_en = first_bone_count(oracle_frames, "Enemy")
    n_en = first_bone_count(native_frames, "Enemy")
    if n_me is None or o_me is None:
        return False
    # oracle Me is 15-bone dummy, Enemy is 205 -> swap
    if o_me == 15 and n_me == 205:
        # also verify Enemy is 205 on both sides if present
        if o_en == 205 and n_en == 205:
            return True
        return True
    if abs(n_me - o_me) > 50:
        return True
    return False


def align(native_frames, oracle_frames, swap=False):
    """Greedy two-pointer walk over the (phase, clip, cf) sequence.

    When swap=True, native driver is Me and oracle driver is Enemy (cross
    mapping for dojo demo where oracle Me is the ragdoll dummy). Both sides
    use normalized clip names.

    Returns (matches, unaligned_native, unaligned_oracle) where matches is a
    list of (native_idx, oracle_idx) pairs. Order-preserving: a native frame
    can only match an oracle frame at or after the current oracle position, so
    repeated (phase, clip, cf) keys across rounds stay in sequence.
    """
    matches = []
    unaligned_native = []
    unaligned_oracle = []
    i = j = 0
    n = len(native_frames)
    m = len(oracle_frames)
    oracle_fid = "Enemy" if swap else "Me"
    while i < n and j < m:
        kn = frame_key(native_frames[i], fighter_id="Me", normalized=True)
        ko = frame_key(oracle_frames[j], fighter_id=oracle_fid, normalized=True)
        if kn == ko:
            matches.append((i, j))
            i += 1
            j += 1
        elif kn < ko:
            unaligned_native.append(i)
            i += 1
        else:
            unaligned_oracle.append(j)
            j += 1
    unaligned_native.extend(range(i, n))
    unaligned_oracle.extend(range(j, m))
    return matches, unaligned_native, unaligned_oracle


# ---------------------------------------------------------------------------
# per-frame metrics
# ---------------------------------------------------------------------------

def bone_deltas(nb, ob):
    """Per-bone max-abs-component delta over the common prefix.

    Returns (deltas, n_common, n_native, n_oracle). A non-list bone entry
    yields delta=inf (counted over-tolerance, excluded from mean/max).
    """
    if not isinstance(nb, list) or not isinstance(ob, list):
        return [], 0, 0, 0
    n = min(len(nb), len(ob))
    deltas = []
    for k in range(n):
        a = nb[k]
        b = ob[k]
        if not isinstance(a, (list, tuple)) or not isinstance(b, (list, tuple)):
            deltas.append(float("inf"))
            continue
        ax = a[0] if len(a) > 0 else 0.0
        ay = a[1] if len(a) > 1 else 0.0
        bx = b[0] if len(b) > 0 else 0.0
        by = b[1] if len(b) > 1 else 0.0
        deltas.append(max(abs(ax - bx), abs(ay - by)))
    return deltas, n, len(nb), len(ob)


def fighter_metrics(nf, of, native_fid, oracle_fid, tolerance, coord_transform="none", native_mean_x=0.0, oracle_mean_x=0.0):
    """Per-fighter metrics for one aligned frame pair, or None if the fighter
    is missing on either side. Supports cross-role mapping and center transform."""
    a = get_fighter(nf, native_fid)
    b = get_fighter(of, oracle_fid)
    if a is None or b is None:
        return None
    # world position with optional center transform (subtract per-frame mean x)
    ax = a.get("x", 0.0)
    ay = a.get("y", 0.0)
    bx = b.get("x", 0.0)
    by = b.get("y", 0.0)
    if coord_transform == "center":
        try:
            ax_c = float(ax) - float(native_mean_x)
            bx_c = float(bx) - float(oracle_mean_x)
            dx = abs(ax_c - bx_c)
        except (TypeError, ValueError):
            dx = abs(ax - bx)
        dy = abs(float(ay) - float(by)) if isinstance(ay, (int, float)) and isinstance(by, (int, float)) else abs(ay - by)
    else:
        dx = abs(ax - bx)
        dy = abs(ay - by)
    fx_mismatch = False
    try:
        fx_mismatch = int(a.get("fx")) != int(b.get("fx"))
    except (TypeError, ValueError):
        pass
    nb = a.get("bones")
    ob = b.get("bones")
    # center transform: subtract mean x from bone x before delta
    if coord_transform == "center" and isinstance(nb, list) and isinstance(ob, list):
        nb_adj = []
        for bone in nb:
            if isinstance(bone, (list, tuple)) and len(bone) >= 1:
                try:
                    nx = float(bone[0]) - float(native_mean_x)
                except (TypeError, ValueError):
                    nx = bone[0]
                new_b = [nx, bone[1] if len(bone) > 1 else 0.0]
                # preserve extra axes if present
                if len(bone) > 2:
                    new_b.extend(bone[2:])
                nb_adj.append(new_b)
            else:
                nb_adj.append(bone)
        ob_adj = []
        for bone in ob:
            if isinstance(bone, (list, tuple)) and len(bone) >= 1:
                try:
                    ox = float(bone[0]) - float(oracle_mean_x)
                except (TypeError, ValueError):
                    ox = bone[0]
                new_b = [ox, bone[1] if len(bone) > 1 else 0.0]
                if len(bone) > 2:
                    new_b.extend(bone[2:])
                ob_adj.append(new_b)
            else:
                ob_adj.append(bone)
        deltas, n_common, n_nat, n_ora = bone_deltas(nb_adj, ob_adj)
    else:
        deltas, n_common, n_nat, n_ora = bone_deltas(nb, ob)
    finite = [d for d in deltas if math.isfinite(d)]
    maxd = max(finite) if finite else 0.0
    meand = sum(finite) / len(finite) if finite else 0.0
    over = sum(1 for d in finite if d > tolerance)
    worst = sorted(range(len(deltas)), key=lambda k: (-deltas[k], k))[:WORST_BONES]
    return {
        "dx": dx,
        "dy": dy,
        "fx_mismatch": fx_mismatch,
        "bone_max": maxd,
        "bone_mean": meand,
        "bone_over": over,
        "bone_common": n_common,
        "bone_native": n_nat,
        "bone_oracle": n_ora,
        "bone_deltas": deltas,
        "worst_bones": worst,
    }


def frame_metrics(nf, of, tolerance, coord_transform="none", swap=False):
    """All per-frame metrics for one aligned pair. Handles role mapping and center."""
    cam_n = nf.get("cam") or {}
    cam_o = of.get("cam") or {}
    m = {
        "dcx": abs(cam_n.get("cx", 0.0) - cam_o.get("cx", 0.0)),
        "dcy": abs(cam_n.get("cy", 0.0) - cam_o.get("cy", 0.0)),
        "dzoom": abs(cam_n.get("zoom", 0.0) - cam_o.get("zoom", 0.0)),
        "fighters": {},
    }
    # per-frame mean x for center transform
    native_mean_x = 0.0
    oracle_mean_x = 0.0
    if coord_transform == "center":
        # average x of present fighters
        nxs = []
        for fid in ("Me", "Enemy"):
            f = get_fighter(nf, fid)
            if f is not None:
                try:
                    nxs.append(float(f.get("x", 0.0)))
                except (TypeError, ValueError):
                    pass
        if nxs:
            native_mean_x = sum(nxs) / len(nxs)
        oxs = []
        for fid in ("Me", "Enemy"):
            f = get_fighter(of, fid)
            if f is not None:
                try:
                    oxs.append(float(f.get("x", 0.0)))
                except (TypeError, ValueError):
                    pass
        if oxs:
            oracle_mean_x = sum(oxs) / len(oxs)
    # fighter pair mapping
    if swap:
        pairs = [("Me", "Me", "Enemy"), ("Enemy", "Enemy", "Me")]
    else:
        pairs = [("Me", "Me", "Me"), ("Enemy", "Enemy", "Enemy")]
    for label, nfid, ofid in pairs:
        m["fighters"][label] = fighter_metrics(nf, of, nfid, ofid, tolerance, coord_transform, native_mean_x, oracle_mean_x)
    return m


# ---------------------------------------------------------------------------
# clip diff mode
# ---------------------------------------------------------------------------

def clip_diff(native_clip, oracle_clip):
    """Diff a native clip dump against the oracle's clip line.

    Bone index is assumed 1:1 (positional). Returns a dict with shape info,
    exact-equal fraction, max abs diff and the first differing
    (frame, bone, axis) triple (0-based).
    """
    nf = native_clip.get("frames")
    of = oracle_clip.get("frames")
    nb = native_clip.get("bones")
    ob = oracle_clip.get("bones")
    nd = native_clip.get("data")
    od = oracle_clip.get("data")
    shape = (nf, nb, len(nd) if isinstance(nd, list) else -1,
             of, ob, len(od) if isinstance(od, list) else -1)
    shape_match = (
        isinstance(nd, list) and isinstance(od, list)
        and nf == of and nb == ob and len(nd) == len(od)
        and all(isinstance(a, list) and isinstance(b, list) and len(a) == len(b)
                for a, b in zip(nd, od))
    )
    if not (isinstance(nd, list) and isinstance(od, list)):
        return {"shape_match": False, "shape": shape, "exact": 0.0,
                "max_abs": None, "first_diff": None,
                "note": "data missing on one side"}
    total = 0
    equal = 0
    max_abs = 0
    first_diff = None
    for fi in range(min(len(nd), len(od))):
        fa = nd[fi]
        fb = od[fi]
        if not isinstance(fa, list) or not isinstance(fb, list):
            continue
        for bi in range(min(len(fa), len(fb))):
            ba = fa[bi]
            bb = fb[bi]
            if not isinstance(ba, list) or not isinstance(bb, list):
                continue
            for ai in range(min(len(ba), len(bb))):
                va = ba[ai]
                vb = bb[ai]
                if not isinstance(va, (int, float)) or not isinstance(vb, (int, float)):
                    continue
                total += 1
                if va == vb:
                    equal += 1
                else:
                    d = abs(va - vb)
                    if d > max_abs:
                        max_abs = d
                    if first_diff is None:
                        first_diff = (fi, bi, ai)
    exact = (equal / total) if total else 0.0
    note = ""
    if shape_match and max_abs > 1000:
        note = ("large max diff: possible scale mismatch "
                "(native clip is 1/16 fixed-point ints; oracle may be floats)")
    return {"shape_match": shape_match, "shape": shape, "exact": exact,
            "max_abs": max_abs, "first_diff": first_diff, "note": note,
            "total": total, "equal": equal}


# ---------------------------------------------------------------------------
# report
# ---------------------------------------------------------------------------

def fmt(v, nd=3):
    if v is None:
        return "-"
    if isinstance(v, float):
        return ("%." + str(nd) + "f") % v
    return str(v)


def build_report(args, native_frames, oracle_frames, native_clips, oracle_clips,
                 native_bad, oracle_bad, oracle_missing, matches,
                 unaligned_native, unaligned_oracle, stats, swap=False):
    # map: native frame idx -> per-fighter worst-10 bone index lists (for the
    # worst-frames table, so "same bones every frame?" is answerable)
    worst_bones = stats["worst_frame_bones"]
    lines = []
    w = lines.append
    w("Pose gap report - native port vs web oracle")
    w("=" * 60)
    w("native:  %s" % args.native)
    w("oracle:  %s" % (args.oracle if not oracle_missing else "(missing)"))
    w("tolerance: %s (world units)" % fmt(args.tolerance, 1))
    w("clip map: enabled (stance_1->FistsStartStance-Left etc, .bytes stripped, case-insensitive)")
    if swap:
        w("role map: native Me <-> oracle Enemy (auto detected ragdoll dummy)")
    else:
        w("role map: native Me <-> oracle Me (no swap)")
    w("coord transform: %s" % getattr(args, "coord_transform", "none"))
    w("")

    w("[inputs]")
    w("  native frames:  %d  (malformed lines skipped: %d)" % (len(native_frames), native_bad))
    if oracle_missing:
        w("  oracle frames:  -  (oracle trace missing - native-only validation)")
    else:
        w("  oracle frames:  %d  (malformed lines skipped: %d)" % (len(oracle_frames), oracle_bad))
        w("  oracle clip lines: %d" % len(oracle_clips))
    w("")

    w("[alignment]")
    w("  frames matched:      %d" % len(matches))
    w("  native frames:       %d" % len(native_frames))
    w("  oracle frames:       %d" % len(oracle_frames))
    if len(native_frames):
        w("  coverage (matched/native):  %.1f%%" % (100.0 * len(matches) / len(native_frames)))
    if len(oracle_frames):
        w("  coverage (matched/oracle):  %.1f%%" % (100.0 * len(matches) / len(oracle_frames)))
    w("  unaligned native frames: %d" % len(unaligned_native))
    w("  unaligned oracle frames: %d" % len(unaligned_oracle))
    if swap:
        w("  alignment key: (phase, normalized clip, cf) with Me<->Enemy swap")
    else:
        w("  alignment key: (phase, normalized clip, cf)")
    w("")

    # clip-name mismatch counters - using normalized keys and respecting swap
    oracle_fid_for_align = "Enemy" if swap else "Me"
    native_names_raw = Counter()
    oracle_names_raw = Counter()
    native_names_norm = Counter()
    oracle_names_norm = Counter()
    for fr in native_frames:
        raw = frame_key_raw(fr, fighter_id="Me")[1]
        norm = normalize_clip_name(raw)
        # count empty as ""? skip empty for mismatch reporting? keep for completeness but filter later
        native_names_raw[raw] += 1
        native_names_norm[norm] += 1
    for fr in oracle_frames:
        raw = frame_key_raw(fr, fighter_id=oracle_fid_for_align)[1]
        norm = normalize_clip_name(raw)
        oracle_names_raw[raw] += 1
        oracle_names_norm[norm] += 1
    # normalized mismatch
    native_only_norm = sorted(n for n in native_names_norm if n not in oracle_names_norm)
    oracle_only_norm = sorted(n for n in oracle_names_norm if n not in native_names_norm)
    # raw mismatch for diagnostic
    native_only_raw = sorted(n for n in native_names_raw if n not in oracle_names_raw)
    oracle_only_raw = sorted(n for n in oracle_names_raw if n not in native_names_raw)
    w("[clip-name mismatch]")
    if not native_only_norm and not oracle_only_norm:
        w("  none (normalized) - every clip name seen on one side also appears on the other after alias map")
        # still show raw if different for transparency
        if native_only_raw or oracle_only_raw:
            w("  raw native-only (mapped): %s" % ", ".join(repr(n) for n in native_only_raw) if native_only_raw else "  raw native-only: none")
            w("  raw oracle-only (mapped): %s" % ", ".join(repr(n) for n in oracle_only_raw) if oracle_only_raw else "  raw oracle-only: none")
    else:
        if native_only_norm:
            w("  native-only normalized clip names (never seen in oracle after map):")
            for n in native_only_norm:
                # show raw representatives mapping to this norm
                raws = sorted(set(raw for raw in native_names_raw if normalize_clip_name(raw) == n))
                w("    %s (%d native frames) <- raw: %s" % (n or "(empty)", native_names_norm[n], ", ".join(repr(r) for r in raws)))
        if oracle_only_norm:
            w("  oracle-only normalized clip names (never seen in native after map):")
            for n in oracle_only_norm:
                raws = sorted(set(raw for raw in oracle_names_raw if normalize_clip_name(raw) == n))
                w("    %s (%d oracle frames) <- raw: %s" % (n or "(empty)", oracle_names_norm[n], ", ".join(repr(r) for r in raws)))
        if (native_only_raw or oracle_only_raw) and (native_only_norm != native_only_raw or oracle_only_norm != oracle_only_raw):
            w("  note: raw mismatch differs from normalized (alias map collapsed some names)")
    w("")

    if oracle_missing:
        w("[oracle missing]")
        w("  oracle trace not present; native file structure validated above.")
        w("  Re-run once reference/traces/oracle_pose.jsonl exists.")
        w("")
        return lines

    cam = stats["cam"]
    w("[camera]")
    w("  |dcx|   mean %s  max %s" % (fmt(cam["dcx_mean"]), fmt(cam["dcx_max"])))
    w("  |dcy|   mean %s  max %s" % (fmt(cam["dcy_mean"]), fmt(cam["dcy_max"])))
    w("  |dzoom| mean %s  max %s" % (fmt(cam["dzoom_mean"]), fmt(cam["dzoom_max"])))
    w("")

    w("[world position]  (per fighter, over matched frames%s)" % (" - centered" if getattr(args, "coord_transform", "none") == "center" else ""))
    for fid in ("Me", "Enemy"):
        s = stats["world"][fid]
        w("  %-5s |dx| mean %s max %s | |dy| mean %s max %s | facing mismatches: %d"
          % (fid, fmt(s["dx_mean"]), fmt(s["dx_max"]),
             fmt(s["dy_mean"]), fmt(s["dy_max"]), s["fx_mismatch"]))
    w("")

    w("[bone deviation]  (per fighter, over matched frames; per-bone delta = max(|dx|,|dy|)%s)" % (" - centered" if getattr(args, "coord_transform", "none") == "center" else ""))
    # detect bone count mismatch for skip reporting
    # find representative counts
    rep_counts = {}
    for fid in ("Me", "Enemy"):
        nfid = "Me" if fid == "Me" else "Enemy"
        ofid = ("Enemy" if fid == "Me" else "Me") if swap else fid
        bn = bo = None
        for ni, oi in matches:
            a = get_fighter(native_frames[ni], nfid)
            b = get_fighter(oracle_frames[oi], ofid)
            if a is not None and b is not None:
                ab = a.get("bones")
                bb = b.get("bones")
                if isinstance(ab, list) and isinstance(bb, list):
                    bn = len(ab)
                    bo = len(bb)
                    break
        rep_counts[fid] = (bn, bo)
    for fid in ("Me", "Enemy"):
        s = stats["bone"][fid]
        bn, bo = rep_counts[fid]
        # if counts differ, report skipped instead of degenerate metrics
        if bn is not None and bo is not None and bn != bo:
            # shape mismatch case
            if bn == 15 or bo == 15:
                w("  %-5s skipped: %d vs %d (ragdoll dummy) - only %d comparable frames, %d mismatched shape" % (fid, bn, bo, s["comparable_frames"], stats["shape"][fid]))
                # also emit canonical phrasing expected by spec
                w("         skipped: Me 205 vs 15 (ragdoll dummy)")
                # show which physical mapping is skipped
                if swap:
                    w("         (native %s %d bones vs oracle %s %d bones; cross-mapped pair)" % ("Me" if fid == "Me" else "Enemy", bn, "Enemy" if fid == "Me" else "Me", bo))
                else:
                    w("         (native %s %d vs oracle %s %d)" % (fid, bn, fid, bo))
            else:
                w("  %-5s skipped: bone count mismatch %d vs %d (%d/%d frames mismatched)" % (fid, bn, bo, stats["shape"][fid], len(matches)))
            continue
        w("  %-5s mean %s  max %s  | frames with any bone over tolerance: %d/%d"
          % (fid, fmt(s["mean_of_means"]), fmt(s["max_of_maxes"]),
             s["over_frames"], s["comparable_frames"]))
    w("")

    w("[worst %d frames by max bone deviation]  (max over both fighters)" % WORST_FRAMES)
    for rank, (maxd, ni) in enumerate(stats["worst_frames"], 1):
        fr = native_frames[ni]
        # use native driver Me for display, but also show normalized?
        k_raw = frame_key_raw(fr, fighter_id="Me")
        k_norm = normalize_clip_name(k_raw[1])
        fid = stats["worst_frames_fighter"][ni]
        w("  %2d. f=%d phase=%s clip=%s (norm:%s) cf=%d fighter=%s max=%.3f"
          % (rank, fr.get("f"), k_raw[0], k_raw[1], k_norm, k_raw[2], fid, maxd))
        bones = worst_bones.get(ni, {})
        for wfid in ("Me", "Enemy"):
            idxs = bones.get(wfid)
            if idxs:
                w("      %-5s worst-%d bones: %s" % (wfid, WORST_BONES, idxs))
    w("")

    w("[bones consistently over tolerance]  (%% of matched frames with |delta| > tolerance, top %d per fighter)" % TOP_BONES)
    for fid in ("Me", "Enemy"):
        bn, bo = rep_counts[fid]
        if bn is not None and bo is not None and bn != bo:
            w("  %s: skipped (bone count mismatch %d vs %d)" % (fid, bn, bo))
            continue
        rows = stats["bone_over_pct"][fid]
        w("  %s:" % fid)
        if not rows:
            w("    (no bone over tolerance in any matched frame)")
            continue
        for pct, idx, over, total in rows:
            w("    bone %3d: %5.1f%%  (%d/%d frames)" % (idx, pct, over, total))
    w("")

    shape = stats["shape"]
    w("[bone-count shape]")
    w("  frames with native/oracle bone-count mismatch: Me %d, Enemy %d"
      % (shape["Me"], shape["Enemy"]))
    # detailed per-pair shape after mapping
    for fid in ("Me", "Enemy"):
        bn, bo = rep_counts[fid]
        if bn is not None and bo is not None:
            w("    %s: native %s %d vs oracle %s %d" % (fid, "Me" if fid == "Me" else "Enemy", bn, "Enemy" if (swap and fid == "Me") else ("Me" if (swap and fid == "Enemy") else fid), bo))
    w("")

    coverage = (len(matches) / len(native_frames)) if native_frames else 0.0
    if coverage < COVERAGE_WARN:
        w("[alignment warning] coverage %.1f%% < %.0f%% - first %d unaligned native frames (phase, clip, cf) raw vs normalized:"
          % (100.0 * coverage, 100.0 * COVERAGE_WARN, UNALIGNED_SAMPLE))
        for ni in unaligned_native[:UNALIGNED_SAMPLE]:
            fr = native_frames[ni]
            k_raw = frame_key_raw(fr, fighter_id="Me")
            k_norm = normalize_clip_name(k_raw[1])
            w("    f=%d phase=%s clip=%s (norm:%s) cf=%d" % (fr.get("f"), k_raw[0], k_raw[1], k_norm, k_raw[2]))
        if len(unaligned_native) > UNALIGNED_SAMPLE:
            w("    ... and %d more" % (len(unaligned_native) - UNALIGNED_SAMPLE))
        w("")
    return lines


def build_clip_report(args, name, native_clip, oracle_clip, oracle_missing, resolved_oracle_name=None):
    lines = []
    w = lines.append
    w("Clip diff - %s (native vs oracle)%s" % (name, " -> %s" % resolved_oracle_name if resolved_oracle_name and resolved_oracle_name != name else ""))
    w("=" * 60)
    w("native clip: %s" % args.native_clip_path)
    w("oracle clip: %s" % (args.oracle if not oracle_missing else "(oracle trace missing)"))
    if resolved_oracle_name:
        w("oracle clip resolved: %s (via alias map)" % resolved_oracle_name)
    w("")
    if oracle_clip is None:
        w("[oracle clip missing]")
        w("  no {\"t\":\"clip\",\"name\":\"%s\",...} line in the oracle trace." % name)
        if resolved_oracle_name:
            w("  also not found via normalized map: %s" % normalize_clip_name(name))
        w("")
        return lines
    d = clip_diff(native_clip, oracle_clip)
    w("[shape]")
    w("  native: frames=%s bones=%s data_frames=%s" % (d["shape"][0], d["shape"][1], d["shape"][2]))
    w("  oracle: frames=%s bones=%s data_frames=%s" % (d["shape"][3], d["shape"][4], d["shape"][5]))
    w("  shape match: %s" % ("yes" if d["shape_match"] else "NO"))
    w("")
    w("[values]  (bone index assumed 1:1, positional)")
    w("  exact-equal fraction: %.4f" % d["exact"])
    if "total" in d and "equal" in d:
        w("  exact: %d/%d" % (d["equal"], d["total"]))
    w("  max abs diff: %s" % fmt(d["max_abs"], 1))
    if d["first_diff"] is not None:
        fi, bi, ai = d["first_diff"]
        w("  first differing (frame, bone, axis) [0-based]: (%d, %d, %d)"
          % (fi, bi, ai))
    else:
        w("  first differing: none")
    if d["note"]:
        w("  note: %s" % d["note"])
    w("")
    return lines


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def find_clip_by_map(clips_dict, query_name):
    """Find clip in dict via normalized alias map. Returns (clip_obj, resolved_key) or (None, None)."""
    norm_q = normalize_clip_name(query_name)
    # direct raw match
    if query_name in clips_dict:
        return clips_dict[query_name], query_name
    # normalized search
    for k, v in clips_dict.items():
        if normalize_clip_name(k) == norm_q:
            return v, k
    # also try case-insensitive raw
    lq = query_name.lower() if isinstance(query_name, str) else ""
    for k, v in clips_dict.items():
        if isinstance(k, str) and k.lower() == lq:
            return v, k
    return None, None


def run_clip_mode(args):
    # native clip resolution via alias map scanning
    # try direct file first
    native_path = os.path.join("reference", "traces", "native_clip_%s.json" % args.clip)
    args.native_clip_path = native_path
    if not os.path.isfile(native_path):
        # try alias map scan for native clip file
        norm_q = normalize_clip_name(args.clip)
        found = None
        trace_dir = os.path.join("reference", "traces")
        pattern = os.path.join(trace_dir, "native_clip_*.json")
        for p in glob.glob(pattern):
            base = os.path.splitext(os.path.basename(p))[0]  # native_clip_XXX
            if base.startswith("native_clip_"):
                candidate = base[len("native_clip_"):]
                if normalize_clip_name(candidate) == norm_q:
                    found = p
                    break
                # also exact lower
                if candidate.lower() == args.clip.lower():
                    found = p
                    break
        if found:
            native_path = found
            args.native_clip_path = native_path
        else:
            print("compare_pose: native clip file not found: %s" % native_path, file=sys.stderr)
            print("compare_pose: run `game.exe --dump-clip %s` first" % args.clip, file=sys.stderr)
            return 2
    with open(native_path, "r", encoding="utf-8") as fh:
        try:
            native_clip = json.load(fh)
        except json.JSONDecodeError as exc:
            print("compare_pose: malformed native clip file %s: %s" % (native_path, exc),
                  file=sys.stderr)
            return 2
    oracle_missing = not os.path.isfile(args.oracle)
    oracle_clip = None
    resolved_oracle_name = None
    if not oracle_missing:
        _, oracle_clips, _ = load_jsonl(args.oracle)
        oracle_clip, resolved_oracle_name = find_clip_by_map(oracle_clips, args.clip)
    lines = build_clip_report(args, args.clip, native_clip, oracle_clip, oracle_missing, resolved_oracle_name)
    emit(args, lines)
    return 0


def emit(args, lines):
    text = "\n".join(lines)
    print(text)
    try:
        out_dir = os.path.dirname(args.out)
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    except OSError as exc:
        print("compare_pose: cannot write report %s: %s" % (args.out, exc), file=sys.stderr)


def run_frame_mode(args):
    if not os.path.isfile(args.native):
        print("compare_pose: native trace not found: %s" % args.native, file=sys.stderr)
        print("compare_pose: run `game.exe --fight --headless N --dump-pose M` first",
              file=sys.stderr)
        return 2

    native_frames, native_clips, native_bad = load_jsonl(args.native)
    oracle_missing = not os.path.isfile(args.oracle)
    oracle_frames, oracle_clips, oracle_bad = [], {}, 0
    if not oracle_missing:
        oracle_frames, oracle_clips, oracle_bad = load_jsonl(args.oracle)

    # determine role swap
    swap = False
    map_opt = getattr(args, "map_native_me_to", "auto")
    coord_transform = getattr(args, "coord_transform", "none")
    if not oracle_missing and native_frames and oracle_frames:
        if map_opt == "oracle-enemy":
            swap = True
        elif map_opt == "auto":
            swap = detect_swap_auto(native_frames, oracle_frames)
        else:
            swap = False
    else:
        swap = False

    matches, unaligned_native, unaligned_oracle = align(native_frames, oracle_frames, swap=swap)

    # ---- aggregate stats over matched frames ----
    cam = {"dcx": [], "dcy": [], "dzoom": []}
    world = {"Me": {"dx": [], "dy": [], "fx": 0},
             "Enemy": {"dx": [], "dy": [], "fx": 0}}
    bone = {"Me": {"means": [], "maxes": [], "over": 0, "comparable": 0},
            "Enemy": {"means": [], "maxes": [], "over": 0, "comparable": 0}}
    bone_over_pct = {"Me": defaultdict(lambda: [0, 0]),  # idx -> [over, total]
                     "Enemy": defaultdict(lambda: [0, 0])}
    shape = {"Me": 0, "Enemy": 0}
    worst = []  # (max_bone_dev, native_idx)
    worst_fighter = {}

    for ni, oi in matches:
        nf = native_frames[ni]
        of = oracle_frames[oi]
        fm = frame_metrics(nf, of, args.tolerance, coord_transform=coord_transform, swap=swap)
        cam["dcx"].append(fm["dcx"])
        cam["dcy"].append(fm["dcy"])
        cam["dzoom"].append(fm["dzoom"])
        frame_max = 0.0
        frame_max_fid = "Me"
        for fid in ("Me", "Enemy"):
            s = fm["fighters"][fid]
            if s is None:
                continue
            world[fid]["dx"].append(s["dx"])
            world[fid]["dy"].append(s["dy"])
            if s["fx_mismatch"]:
                world[fid]["fx"] += 1
            bone[fid]["means"].append(s["bone_mean"])
            bone[fid]["maxes"].append(s["bone_max"])
            if s["bone_over"] > 0:
                bone[fid]["over"] += 1
            if s["bone_common"] > 0:
                bone[fid]["comparable"] += 1
            if s["bone_native"] != s["bone_oracle"]:
                shape[fid] += 1
            for idx, d in enumerate(s["bone_deltas"]):
                if not math.isfinite(d):
                    continue
                rec = bone_over_pct[fid][idx]
                rec[1] += 1
                if d > args.tolerance:
                    rec[0] += 1
            if s["bone_max"] > frame_max:
                frame_max = s["bone_max"]
                frame_max_fid = fid
        worst.append((frame_max, ni))
        worst_fighter[ni] = frame_max_fid

    def mean(xs):
        return sum(xs) / len(xs) if xs else 0.0

    def maxv(xs):
        return max(xs) if xs else 0.0

    stats = {
        "cam": {
            "dcx_mean": mean(cam["dcx"]), "dcx_max": maxv(cam["dcx"]),
            "dcy_mean": mean(cam["dcy"]), "dcy_max": maxv(cam["dcy"]),
            "dzoom_mean": mean(cam["dzoom"]), "dzoom_max": maxv(cam["dzoom"]),
        },
        "world": {
            fid: {"dx_mean": mean(world[fid]["dx"]), "dx_max": maxv(world[fid]["dx"]),
                  "dy_mean": mean(world[fid]["dy"]), "dy_max": maxv(world[fid]["dy"]),
                  "fx_mismatch": world[fid]["fx"]}
            for fid in ("Me", "Enemy")
        },
        "bone": {
            fid: {"mean_of_means": mean(bone[fid]["means"]),
                  "max_of_maxes": maxv(bone[fid]["maxes"]),
                  "over_frames": bone[fid]["over"],
                  "comparable_frames": bone[fid]["comparable"]}
            for fid in ("Me", "Enemy")
        },
        "bone_over_pct": {},
        "shape": shape,
        "worst_frames": sorted(worst, key=lambda t: (-t[0], t[1]))[:WORST_FRAMES],
        "worst_frames_fighter": worst_fighter,
    }
    for fid in ("Me", "Enemy"):
        rows = []
        for idx, (over, total) in bone_over_pct[fid].items():
            if total > 0 and over > 0:
                rows.append((100.0 * over / total, idx, over, total))
        rows.sort(key=lambda r: (-r[0], r[1]))
        stats["bone_over_pct"][fid] = rows[:TOP_BONES]

    # per-frame worst-10 bone indices (per fighter) for the worst-frames table
    match_map = dict(matches)  # native idx -> oracle idx
    worst_frame_bones = {}
    for _, ni in stats["worst_frames"]:
        entry = {}
        for fid in ("Me", "Enemy"):
            s = frame_metrics(native_frames[ni], oracle_frames[match_map[ni]],
                              args.tolerance, coord_transform=coord_transform, swap=swap)["fighters"][fid]
            # only meaningful when the fighter actually deviates
            if s is not None and s["bone_max"] > 0.0 and s["worst_bones"]:
                entry[fid] = s["worst_bones"]
        worst_frame_bones[ni] = entry
    stats["worst_frame_bones"] = worst_frame_bones

    lines = build_report(args, native_frames, oracle_frames, native_clips, oracle_clips,
                         native_bad, oracle_bad, oracle_missing, matches,
                         unaligned_native, unaligned_oracle, stats, swap=swap)
    emit(args, lines)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Compare native vs oracle pose traces (Shadow Fight 2).")
    ap.add_argument("--native", default=DEFAULT_NATIVE,
                    help="native pose JSONL (default: %s)" % DEFAULT_NATIVE)
    ap.add_argument("--oracle", default=DEFAULT_ORACLE,
                    help="oracle pose JSONL (default: %s)" % DEFAULT_ORACLE)
    ap.add_argument("--tolerance", type=float, default=DEFAULT_TOLERANCE,
                    help="bone/world tolerance in world units (default: %s)" % DEFAULT_TOLERANCE)
    ap.add_argument("--out", default=DEFAULT_OUT,
                    help="report file (default: %s)" % DEFAULT_OUT)
    ap.add_argument("--clip", default=None, metavar="NAME",
                    help="clip diff mode: diff native_clip_<NAME>.json vs the oracle's clip line")
    ap.add_argument("--map-native-me-to", dest="map_native_me_to", default="auto",
                    choices=["auto", "oracle-enemy"],
                    help="fighter role selection: auto (detect 15-bone dummy) or force native Me <-> oracle Enemy (default: auto)")
    ap.add_argument("--coord-transform", dest="coord_transform", default="none",
                    choices=["none", "center"],
                    help="coordinate handling: none (raw) or center (subtract per-frame mean x before bone/world comparison) (default: none)")
    args = ap.parse_args(argv)

    if args.tolerance <= 0:
        print("compare_pose: --tolerance must be positive", file=sys.stderr)
        return 2

    if args.clip is not None:
        return run_clip_mode(args)
    return run_frame_mode(args)


if __name__ == "__main__":
    sys.exit(main())


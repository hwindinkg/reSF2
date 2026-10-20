#!/usr/bin/env python3
"""
compare_state.py - mechanical LOGIC-state differential (native port vs web oracle).

Companion to compare_pose.py. Where compare_pose.py diffs the RENDERED pose
(bones/camera), this diffs the full FIGHT LOGIC STATE the two builds hold each
frame, so logic divergences are found by diffing rather than by reading code.

Both sides emit one JSONL line per live fight frame with the same schema:

  {"t":"state","f":F,"phase":P,"round":R,"timer":T,"nf":NF,"result":E,
   "cam":{"cx":..,"cy":..,"zoom":..},
   "counters":{"m$":0|1,"lW":0|1},
   "rules":[n_total,n_active],
   "callouts":[n_me,n_en],
   "fighters":[{"id":"Me","hp":..,"mhp":..,"combo":C,"style":S,"unhit":0|1,
                "shock":0|1,"block":0|1,"invuln":0|1,"move":"..","af":A,
                "rag":0|1,"ragf":N,"iv":[["name",type],...]},
               {"id":"Enemy",...}]}

Producers:
  native: `game.exe --fight --headless N --dump-state M`
          -> reference/traces/native_state.jsonl
  oracle: reference/runner/trace.js (the `{"t":"state"}` line, captured by
          OracleShell.exe into reference/traces/console.log, extracted with
          reference/tools/extract_state.py) -> reference/traces/oracle_state.jsonl

Alignment is the SAME as compare_pose.py: a greedy two-pointer walk over the
(phase, normalized move name, clip frame `af`) sequence of the driver fighter.
The oracle's Me is the ragdoll DUMMY in the dojo demo (compare_pose.py's
`detect_swap_auto`), so the default `--map-native-me-to auto` maps native Me
<-> oracle Enemy.

Usage:
  python compare_state.py [--native PATH] [--oracle PATH] [--out FILE]
                          [--map-native-me-to auto|oracle-enemy]

Exit codes:
  0  report produced
  2  a required input is missing (native state trace)

Deterministic output: no timestamps.
"""

import argparse
import json
import os
import re
import sys
from collections import Counter

DEFAULT_NATIVE = os.path.join("reference", "traces", "native_state.jsonl")
DEFAULT_ORACLE = os.path.join("reference", "traces", "oracle_state.jsonl")
DEFAULT_OUT = os.path.join("reference", "traces", "state_gap_report.txt")
FLOAT_TOL = 0.001

# Fields emitted but NOT counted toward the divergence total, with the reason.
# (A field is only informational when the two sides structurally CANNOT hold
# the same quantity; this is reported explicitly, never silently dropped.)
#
# EMPTY as of the state-harness extension: every emitted field now holds the
# SAME JS quantity on both sides, so all of them are counted.
#   nf       -> oracle `Sf.NF` (HUD countdown, `fight.ha.mb`)  == native `hud_timer()`
#   timer    -> oracle `Sf.NF` (raw countdown)                 == native `round_.time_nf`
#               (the JS `$t.time` is write-once 0 — NOT the countdown)
#   rules.*  -> oracle `fight.Da.jh()` (`p.o.Yh ? CV : Ae`)    == native `rules_`
#   callouts -> oracle `Sf.Id.Jh.Gu` / `Sf.je.Jh.Gu` (live)    == native `callout_display_`
#
# `fighters.*.x`: the oracle's x is the POSED body x (it includes the
# location scene Shift), the native's is the logical world x. Measured on
# 2026-10-20 from the gate artifacts: the difference is a constant per
# location and the same for both fighters (dojo 0.000, moon 0.598,
# arena/autumn 22.211), i.e. a global scene shift, not a fighter position.
# The comparable quantity is `fighters.sep_x` (the x distance between the two
# fighters), which IS counted; the absolute x stays visible as a diagnostic.
INFORMATIONAL = {"fighters.Me.x", "fighters.Enemy.x"}

# clip/move name normalization (same rule as compare_pose.py)
CLIP_ALIASES = {
    "stance_1": "fistsstartstance-left",
    "stance_2": "fistsstartstance-right",
    "fists1_stance_idle": "fistsstartstanceidle-left",
    "fists2_stance_idle": "fistsstartstanceidle-right",
}


def normalize_move(name):
    if not isinstance(name, str):
        return ""
    s = name.strip()
    if not s:
        return ""
    if s.lower().endswith(".bytes"):
        s = s[:-6]
    low = s.lower()
    if low in CLIP_ALIASES:
        return CLIP_ALIASES[low]
    if low.startswith("fists1_"):
        return "fists" + low[7:]
    if low.startswith("fists2_"):
        return "fists" + low[7:]
    return low


def normalize_interval(iv):
    """Normalize one [name,type] pair.

    The native `active_intervals_` substitutes "typeN" for an interval with no
    `Name` attr; the JS `xj` entry keeps an empty name. Collapse "typeN" -> ""
    (keeping the type) so the two naming conventions do not read as a logic
    divergence.
    """
    if not isinstance(iv, list) or len(iv) < 2:
        return None
    name = iv[0] if isinstance(iv[0], str) else ""
    typ = iv[1] if isinstance(iv[1], int) else -1
    if re.fullmatch(r"type\d+", name):
        name = ""
    return (name, typ)


def normalize_ivs(ivs):
    out = []
    if isinstance(ivs, list):
        for iv in ivs:
            n = normalize_interval(iv)
            if n is not None:
                out.append(n)
    out.sort()
    return out


def load_jsonl(path):
    frames = []
    bad = 0
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                bad += 1
                continue
            if isinstance(obj, dict) and obj.get("t") == "state":
                frames.append(obj)
            else:
                bad += 1
    return frames, bad


def get_fighter(fr, fid):
    fighters = fr.get("fighters")
    if not isinstance(fighters, list):
        return None
    for f in fighters:
        if isinstance(f, dict) and f.get("id") == fid:
            return f
    if fid == "Me" and fighters and isinstance(fighters[0], dict):
        return fighters[0]
    if fid == "Enemy" and len(fighters) > 1 and isinstance(fighters[1], dict):
        return fighters[1]
    return None


def driver_move(fr, fid):
    f = get_fighter(fr, fid)
    if not isinstance(f, dict):
        return ""
    return normalize_move(f.get("move"))


def driver_af(fr, fid):
    f = get_fighter(fr, fid)
    if not isinstance(f, dict):
        return -1
    af = f.get("af")
    try:
        return int(af)
    except (TypeError, ValueError):
        return -1


def frame_key(fr, fid):
    try:
        phase = int(fr.get("phase"))
    except (TypeError, ValueError):
        phase = -1
    return (phase, driver_move(fr, fid), driver_af(fr, fid))


def is_dummy(fr, fid):
    f = get_fighter(fr, fid)
    if not isinstance(f, dict):
        return False
    if normalize_move(f.get("move")) == "physicaldummy":
        return True
    return f.get("rag") == 1 and (f.get("ragf") or 0) > 0


def align_by_frame(native_frames, oracle_frames, oracle_fid="Me"):
    """Pair the two traces by FIGHT FRAME (plus a constant offset).

    Both producers emit exactly ONE line per fight frame from their first LIVE
    frame, so `f` is the strongest alignment key. The offset is found by a
    bounded search for the shift that maximises `(f + offset, phase)` matches
    (the traces' first frames differ by their intro length — e.g. BOSS_LYNX
    131 — and the dojo viewer carries one extra move-less lead frame, so the
    raw first-frame delta is not reliable). Returns (matches, un_nat, un_ora)
    or None when `f` is unusable.
    """
    if not native_frames or not oracle_frames:
        return None
    try:
        d0 = int(oracle_frames[0].get("f")) - int(native_frames[0].get("f"))
    except (TypeError, ValueError):
        return None
    o_by = {}
    for j, of in enumerate(oracle_frames):
        key = (of.get("f"), of.get("phase")) + frame_key(of, oracle_fid)[1:]
        if key not in o_by:
            o_by[key] = j
    n_keys = []
    for nf in native_frames:
        try:
            fnum = int(nf.get("f"))
        except (TypeError, ValueError):
            n_keys.append(None)
            continue
        n_keys.append((fnum, nf.get("phase")) + frame_key(nf, "Me")[1:])

    def count(off):
        c = 0
        for k in n_keys:
            if k is not None and (k[0] + off,) + k[1:] in o_by:
                c += 1
        return c

    best_off, best_key = d0, (-1, 0)
    for off in range(d0 - 300, d0 + 301):
        c = count(off)
        # Tie-break toward the raw first-frame delta (the traces repeat short
        # frames, so several shifts can match equally well).
        key = (c, -abs(off - d0))
        if key > best_key:
            best_key, best_off = key, off
    # Final pairing uses (frame, phase) only: the offset is pinned by the
    # strict key above, and the field comparison must still SEE a divergent
    # frame (matching it is the point) rather than drop it as "unaligned".
    o_by2 = {}
    for j, of in enumerate(oracle_frames):
        k2 = (of.get("f"), of.get("phase"))
        if k2 not in o_by2:
            o_by2[k2] = j
    matches = []
    used = set()
    for i, nf in enumerate(native_frames):
        try:
            k2 = (int(nf.get("f")) + best_off, nf.get("phase"))
        except (TypeError, ValueError):
            continue
        j = o_by2.get(k2)
        if j is not None and j not in used:
            matches.append((i, j))
            used.add(j)
    matched_i = set(m[0] for m in matches)
    un_nat = [i for i in range(len(native_frames)) if i not in matched_i]
    un_ora = [j for j in range(len(oracle_frames)) if j not in used]
    return matches, un_nat, un_ora


def move_set(fr, fid):
    f = get_fighter(fr, fid)
    if not isinstance(f, dict):
        return set()
    m = normalize_move(f.get("move"))
    return {m} if m else set()


def detect_swap(native_frames, oracle_frames):
    """True when native Me holds the real fighter and oracle Me the dummy."""
    n_me = sum(1 for fr in native_frames if is_dummy(fr, "Me"))
    o_me = sum(1 for fr in oracle_frames if is_dummy(fr, "Me"))
    o_en = sum(1 for fr in oracle_frames if is_dummy(fr, "Enemy"))
    if o_me > o_en and n_me < len(native_frames) // 2:
        return True
    # Non-dummy fights (a real BOSS/tournament bout): the JS labels
    # `fight.pb` as "Me", but for the shipped battles `pb` is the ENEMY side
    # (the dojo dummy and the BOSS_LYNX claws both sit in the oracle's "Me").
    # Decide by move-name overlap: does the native Me look more like the
    # oracle's Enemy or its Me?
    n_moves = set()
    for fr in native_frames:
        n_moves |= move_set(fr, "Me")
    o_me_moves = set()
    o_en_moves = set()
    for fr in oracle_frames:
        o_me_moves |= move_set(fr, "Me")
        o_en_moves |= move_set(fr, "Enemy")
    if not n_moves:
        return False
    return len(n_moves & o_en_moves) > len(n_moves & o_me_moves)


def align(native_frames, oracle_frames, swap):
    oracle_fid = "Enemy" if swap else "Me"
    matches, un_nat, un_ora = [], [], []
    i = j = 0
    n, m = len(native_frames), len(oracle_frames)
    while i < n and j < m:
        kn = frame_key(native_frames[i], "Me")
        ko = frame_key(oracle_frames[j], oracle_fid)
        if kn == ko:
            matches.append((i, j))
            i += 1
            j += 1
        elif kn < ko:
            un_nat.append(i)
            i += 1
        else:
            un_ora.append(j)
            j += 1
    un_nat.extend(range(i, n))
    un_ora.extend(range(j, m))
    return matches, un_nat, un_ora


# ---------------------------------------------------------------------------
# per-field comparison
# ---------------------------------------------------------------------------

def scalar_eq(a, b, tol=0.0):
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return abs(float(a) - float(b)) <= tol
    return a == b


def collect_fields(nf, of, swap):
    """Return an ordered dict {field_label: (native_value, oracle_value)} for
    one aligned frame pair, with the fighter role map applied."""
    out = {}
    out["phase"] = (nf.get("phase"), of.get("phase"))
    out["round"] = (nf.get("round"), of.get("round"))
    out["result"] = (nf.get("result"), of.get("result"))
    out["rules.total"] = ((nf.get("rules") or [None])[0], (of.get("rules") or [None])[0])
    out["rules.active"] = ((nf.get("rules") or [None, None])[1], (of.get("rules") or [None, None])[1])
    ncm = (nf.get("counters") or {})
    ocm = (of.get("counters") or {})
    out["counters.m$"] = (ncm.get("m$"), ocm.get("m$"))
    out["counters.lW"] = (ncm.get("lW"), ocm.get("lW"))
    # informational (emitted, not counted)
    out["nf"] = (nf.get("nf"), of.get("nf"))
    out["timer"] = (nf.get("timer"), of.get("timer"))
    out["callouts"] = (nf.get("callouts"), of.get("callouts"))

    pairs = [("Me", "Me", "Enemy"), ("Enemy", "Enemy", "Me")] if swap else \
            [("Me", "Me", "Me"), ("Enemy", "Enemy", "Enemy")]
    for label, nfid, ofid in pairs:
        a = get_fighter(nf, nfid)
        b = get_fighter(of, ofid)
        if not isinstance(a, dict) or not isinstance(b, dict):
            continue
        p = "fighters.%s." % label
        # Absolute/derived POSITION is only one quantity for a STANDING body:
        # for a ragdoll the oracle reports the posed ragdoll body while the
        # native reports the fighter root (measured on the dojo dummy: dy up to
        # ~4 units, d|x| ~2 mm, on every frame). The ragdoll's bones are
        # compared by compare_pose.py; the state harness skips the position
        # fields on ragdoll frames instead of reporting a false divergence.
        standing = a.get("rag") == 0 and b.get("rag") == 0
        for k in ("w", "hp", "mhp", "combo", "style", "unhit", "shock", "block",
                  "invuln", "move", "af", "rag", "ragf"):
            va = a.get(k)
            vb = b.get(k)
            if k == "move":
                va = normalize_move(va)
                vb = normalize_move(vb)
            out[p + k] = (va, vb)
        if standing:
            out[p + "x"] = (a.get("x"), b.get("x"))
            out[p + "y"] = (a.get("y"), b.get("y"))
        out[p + "iv"] = (normalize_ivs(a.get("iv")), normalize_ivs(b.get("iv")))

    # Positions: the absolute x is NOT one quantity across locations. The
    # oracle reports the POSED body x (`f.oa.Fe().ma.x`), which carries the
    # location's scene Shift; the native reports the logical world x
    # (`Fighter::world_x`). Measured: the offset is constant per location and
    # IDENTICAL for both fighters (dojo 0.000, moon 0.598, arena/autumn
    # 22.211), so a global shift is invisible in the relative geometry. The
    # counted position field is therefore the x distance between the two
    # fighters (shift-invariant, abs() makes the oracle role mapping
    # irrelevant). `fighters.*.x` stays emitted as a diagnostic (INFORMATIONAL).
    def x_of(fr, fid):
        f = get_fighter(fr, fid)
        return f.get("x") if isinstance(f, dict) else None

    n_me, n_en = x_of(nf, "Me"), x_of(nf, "Enemy")
    o_me, o_en = x_of(of, "Me"), x_of(of, "Enemy")
    if None not in (n_me, n_en, o_me, o_en):
        # Only a fully standing pair has one position quantity (see above).
        standing = True
        for fr, fid in ((nf, "Me"), (nf, "Enemy"), (of, "Me"), (of, "Enemy")):
            f = get_fighter(fr, fid)
            if not isinstance(f, dict) or f.get("rag") != 0:
                standing = False
                break
        if standing:
            out["fighters.sep_x"] = (
                round(abs(n_en - n_me), 4), round(abs(o_en - o_me), 4))
    return out


def build_report(args, native_frames, oracle_frames, native_bad, oracle_bad,
                 matches, un_nat, un_ora, stats, swap):
    L = []
    w = L.append
    w("State gap report - native port vs web oracle (FIGHT LOGIC)")
    w("=" * 72)
    if getattr(args, "label", ""):
        w("scenario: %s" % args.label)
    w("native:  %s" % args.native)
    w("oracle:  %s" % args.oracle)
    w("role map: %s" % ("native Me <-> oracle Enemy (oracle Me holds the "
                        "enemy-side fighter: the dojo dummy / the boss)"
                        if swap else "native Me <-> oracle Me"))
    w("alignment key: %s" % getattr(args, "align_kind", "auto"))
    w("")
    w("[inputs]")
    w("  native state frames: %d  (skipped lines: %d)" % (len(native_frames), native_bad))
    w("  oracle state frames: %d  (skipped lines: %d)" % (len(oracle_frames), oracle_bad))
    w("")
    w("[alignment]")
    w("  frames matched:        %d" % len(matches))
    w("  unaligned native:      %d" % len(un_nat))
    w("  unaligned oracle:      %d" % len(un_ora))
    if native_frames:
        w("  coverage (matched/native): %.1f%%" % (100.0 * len(matches) / len(native_frames)))
    w("")

    if not matches:
        w("[no aligned frames] - cannot diff; check the scenario / role map.")
        return L

    w("[field divergences]  (over %d matched frames; first = native f / oracle f)"
      % len(matches))
    w("  %-26s %-22s %s" % ("field", "first divergence", "mismatches / compared"))
    counted_total = 0
    rows = []
    for field, s in stats.items():
        # A field is informational when the two sides structurally cannot hold
        # the same quantity: either it is listed in INFORMATIONAL, or the
        # oracle exposed NO value at all for the matched frames (e.g. the
        # tutorial fight is built with `ha == null` (`v.m1a` L50365), so it has
        # no `Sf` HUD timer at all — `nf`/`timer` are then structurally absent,
        # not divergent).
        info = field in INFORMATIONAL or (
            s.get("oracle_none", 0) > 0 and s["oracle_none"] == s["compared"])
        if s["mismatch"] and not info:
            counted_total += s["mismatch"]
        rows.append((field, s, info))
    # counted fields first (by mismatch desc, then name), informational last
    rows.sort(key=lambda r: (r[2], -r[1]["mismatch"], r[0]))
    for field, s, info in rows:
        tag = " [info]" if info else ""
        if s["first"] is None:
            first = "-"
            sample = ""
        else:
            nf_f, of_f, nv, ov = s["first"]
            first = "f=%s / f=%s" % (nf_f, of_f)
            sample = "  (%s vs %s)" % (fmt(nv), fmt(ov))
        w("  %-26s %-22s %d / %d%s%s"
          % (field, first, s["mismatch"], s["compared"], tag, sample))
    w("")
    w("[summary]")
    w("  fields compared:      %d" % sum(1 for f, s, i in rows if not i))
    w("  fields diverging:     %d" % sum(1 for f, s, i in rows if not i and s["mismatch"]))
    w("  total divergences:    %d" % counted_total)
    if INFORMATIONAL:
        w("  informational fields (emitted, not counted): %s"
          % ", ".join(sorted(INFORMATIONAL)))
    else:
        w("  informational fields: none (every emitted field is counted)")
    dyn = sorted(f for f, s, i in rows if i and f not in INFORMATIONAL)
    if dyn:
        w("  informational (oracle exposes no value in this scenario): %s"
          % ", ".join(dyn))
    w("")

    # first-divergence detail: which fields break first, in frame order
    w("[divergence timeline]  (aligned index -> native f / oracle f: fields that first break here)")
    by_first = {}
    for field, s, info in rows:
        if info or s["first"] is None:
            continue
        key = (s["first"][0], s["first"][1])
        by_first.setdefault(key, []).append(field)
    for (nf_f, of_f) in sorted(by_first, key=lambda k: (k[0], k[1]))[:25]:
        w("  nat f=%-5s ora f=%-5s : %s"
          % (nf_f, of_f, ", ".join(sorted(by_first[(nf_f, of_f)]))))
    w("")
    return L


def fmt(v):
    if isinstance(v, float):
        return "%.3f" % v
    return str(v)


def run(args):
    if not os.path.isfile(args.native):
        print("compare_state: native state trace not found: %s" % args.native, file=sys.stderr)
        print("compare_state: run `game.exe --fight --headless N --dump-state M` first",
              file=sys.stderr)
        return 2
    native_frames, native_bad = load_jsonl(args.native)
    oracle_missing = not os.path.isfile(args.oracle)
    if oracle_missing:
        oracle_frames, oracle_bad = [], 0
    else:
        oracle_frames, oracle_bad = load_jsonl(args.oracle)

    swap = False
    if native_frames and oracle_frames:
        if args.map_native_me_to == "oracle-enemy":
            swap = True
        elif args.map_native_me_to == "auto":
            swap = detect_swap(native_frames, oracle_frames)

    matches, un_nat, un_ora = align(native_frames, oracle_frames, swap)
    if args.align in ("auto", "frame"):
        fm = align_by_frame(native_frames, oracle_frames,
                            "Enemy" if swap else "Me")
        if fm is not None and (args.align == "frame" or len(fm[0]) > len(matches)):
            matches, un_nat, un_ora = fm
            align_kind = "fight frame + offset"
        else:
            align_kind = "(phase, move, af) walk"
    else:
        align_kind = "(phase, move, af) walk"
    args.align_kind = align_kind

    stats = {}
    for ni, oi in matches:
        fields = collect_fields(native_frames[ni], oracle_frames[oi], swap)
        for field, (nv, ov) in fields.items():
            s = stats.setdefault(field, {"mismatch": 0, "compared": 0, "first": None})
            s["compared"] += 1
            ok = False
            if field in ("fighters.Me.hp", "fighters.Enemy.hp",
                         "fighters.Me.mhp", "fighters.Enemy.mhp",
                         "fighters.Me.x", "fighters.Enemy.x",
                         "fighters.Me.y", "fighters.Enemy.y",
                         "fighters.sep_x"):
                # Positions are floats on both sides (native `%.4f`, oracle
                # rounded to 4 decimals): compare with the same tolerance the
                # hp fields use, so 1e-4 rounding never reads as a divergence.
                ok = scalar_eq(nv, ov, FLOAT_TOL)
            else:
                ok = nv == ov
            if not ok:
                s["mismatch"] += 1
                if s["first"] is None:
                    s["first"] = (native_frames[ni].get("f"), oracle_frames[oi].get("f"), nv, ov)
            if ov is None:
                s["oracle_none"] = s.get("oracle_none", 0) + 1

    lines = build_report(args, native_frames, oracle_frames, native_bad, oracle_bad,
                         matches, un_nat, un_ora, stats, swap)
    if oracle_missing:
        lines.append("[oracle missing] oracle state trace not present; native file "
                     "validated only. Re-run once %s exists." % args.oracle)
    text = "\n".join(lines)
    print(text)
    try:
        out_dir = os.path.dirname(args.out)
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    except OSError as exc:
        print("compare_state: cannot write report %s: %s" % (args.out, exc), file=sys.stderr)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Compare native vs oracle FIGHT LOGIC state traces (Shadow Fight 2).")
    ap.add_argument("--native", default=DEFAULT_NATIVE)
    ap.add_argument("--oracle", default=DEFAULT_ORACLE)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--map-native-me-to", dest="map_native_me_to", default="auto",
                    choices=["auto", "oracle-enemy"])
    ap.add_argument("--align", default="auto", choices=["auto", "frame", "walk"],
                    help="alignment strategy: 'frame' pairs by fight frame plus "
                         "a constant offset (the strongest key — both producers "
                         "emit one line per fight frame), 'walk' is the greedy "
                         "(phase, move, af) two-pointer walk, 'auto' picks "
                         "whichever yields more matched frames")
    ap.add_argument("--label", default="",
                    help="scenario label shown in the report header "
                         "(e.g. 'dojo-demo', 'tournament-ladder', "
                         "'boss-lynx', 'survival', 'shop', 'quests')")
    args = ap.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
compare_audio.py - mechanical AUDIO differential (native port vs web oracle).

Diffs the per-fight-frame audio trace the two builds emit, so a missing /
extra SFX, a wrong music track, or a bus-state divergence is found by diffing
instead of by reading code.

Both sides emit one JSONL line per live fight frame with the same schema:

  {"t":"audio","f":F,"phase":P,
   "plays":[["snd_hit1",0], ...],   # SFX started this frame (+ loop flag)
   "stops":["snd_loop", ...],       # SFX stopped this frame
   "sfx":1,"music":1,               # bus gains (JS ta.$D / ta.ZD; 0=muted)
   "music_track":"fight10_black_warrior"}

Producers:
  native: `game.exe --fight --dump-audio M ...`
          -> reference/traces/native_audio.jsonl
  oracle: reference/runner/trace.js (the `{"t":"audio"}` line, captured by
          OracleShell.exe into reference/traces/console.log, extracted with
          `python reference/tools/extract_layer.py audio`)
          -> reference/traces/oracle_audio.jsonl

Field sources (both sides read the SAME JS quantity):
  plays       JS `Ss.play(id, loop)` (L100082, hooked) name from `Ss.kWa`
              (L100054) == native `AudioEngine::play` (name = `ta.WBa` key)
  stops       JS `Ss.stop(id)` (L100139)                     == native `AudioEngine::stop`
  sfx/music   JS `Ss.cMa`/`Ss.uF` (bus writes, ta.WT/ta.VT)  == native sfx_bus_gain/music_bus_gain
  music_track JS music `Ss.play` name normalized (basename, `_music` stripped)
              == native `music_track()` (the JS `ta.u0` track name)

Usage:
  python compare_audio.py [--native PATH] [--oracle PATH] [--out FILE] [--label L]
"""

import argparse
import json
import os
import sys

DEFAULT_NATIVE = os.path.join("reference", "traces", "native_audio.jsonl")
DEFAULT_ORACLE = os.path.join("reference", "traces", "oracle_audio.jsonl")
DEFAULT_OUT = os.path.join("reference", "traces", "audio_gap_report.txt")


def load(path):
    rows = []
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
            if isinstance(obj, dict) and obj.get("t") == "audio":
                rows.append(obj)
            else:
                bad += 1
    return rows, bad


def key(row):
    return (row.get("f"), row.get("phase"))


def index(rows):
    d = {}
    for i, r in enumerate(rows):
        k = key(r)
        if k not in d:
            d[k] = i
    return d


def norm_plays(plays):
    out = []
    if isinstance(plays, list):
        for p in plays:
            if isinstance(p, list) and len(p) >= 1:
                out.append((str(p[0]), int(p[1]) if len(p) > 1 else 0))
    out.sort()
    return out


def norm_stops(stops):
    out = [str(s) for s in stops] if isinstance(stops, list) else []
    out.sort()
    return out


def norm_track(v):
    return "" if v is None else str(v)


def fmt(v):
    return str(v)


def run(args):
    if not os.path.isfile(args.native):
        print("compare_audio: native audio trace not found: %s" % args.native,
              file=sys.stderr)
        print("compare_audio: run `game.exe --fight --dump-audio N ...` first",
              file=sys.stderr)
        return 2
    nat, nbad = load(args.native)
    ora_missing = not os.path.isfile(args.oracle)
    ora, obad = ([], 0) if ora_missing else load(args.oracle)

    # Both producers start at their OWN first live fight frame (the oracle
    # navigates hub -> Map -> fight, so its first frame is larger — e.g. the
    # BOSS_LYNX fight starts at oracle f=131 while the native direct-boot
    # fight starts at f=0). Align with a constant frame offset found by a
    # bounded search for the shift that maximises (f+off, phase) matches —
    # the same strategy as compare_state.py --align auto.
    off = 0
    if nat and ora:
        try:
            off = int(ora[0].get("f")) - int(nat[0].get("f"))
        except (TypeError, ValueError):
            off = 0
        okeys = set(key(r) for r in ora)
        best, best_off = -1, off
        for cand in range(off - 400, off + 401):
            c = sum(1 for r in nat if (r.get("f") + cand, r.get("phase")) in okeys)
            if c > best:
                best, best_off = c, cand
        off = best_off
    oidx = {}
    for i, r in enumerate(ora):
        k = key(r)
        if k not in oidx:
            oidx[k] = i

    fields = ("plays", "stops", "sfx", "music", "music_track")
    stats = {f: {"mismatch": 0, "compared": 0, "first": None} for f in fields}
    nat_only = 0
    matched = 0
    # Per-frame totals over the MATCHED window ONLY (the port's trace is
    # truncated by its --headless budget, so the oracle's longer tail is not
    # comparable; restricting both sides to the aligned frames makes the
    # per-event totals a fair check).
    nat_tot = {}
    ora_tot = {}
    diverging_frames = []

    for r in nat:
        j = oidx.get((r.get("f") + off, r.get("phase")))
        if j is None:
            nat_only += 1
            continue
        o = ora[j]
        matched += 1
        for (n, l) in norm_plays(r.get("plays")):
            nat_tot[n] = nat_tot.get(n, 0) + 1
        for (n, l) in norm_plays(o.get("plays")):
            ora_tot[n] = ora_tot.get(n, 0) + 1
        pairs = {
            "plays": (norm_plays(r.get("plays")), norm_plays(o.get("plays"))),
            "stops": (norm_stops(r.get("stops")), norm_stops(o.get("stops"))),
            "sfx": (r.get("sfx"), o.get("sfx")),
            "music": (r.get("music"), o.get("music")),
            "music_track": (norm_track(r.get("music_track")),
                            norm_track(o.get("music_track"))),
        }
        for f, (nv, ov) in pairs.items():
            s = stats[f]
            s["compared"] += 1
            if nv != ov:
                s["mismatch"] += 1
                if s["first"] is None:
                    s["first"] = (r.get("f"), o.get("f"), nv, ov)
                if f in ("plays", "stops"):
                    diverging_frames.append((r.get("f"), o.get("f"), f, nv, ov))

    L = []
    w = L.append
    w("Audio gap report - native port vs web oracle (AUDIO)")
    w("=" * 72)
    if args.label:
        w("scenario: %s" % args.label)
    w("native:  %s" % args.native)
    w("oracle:  %s" % args.oracle)
    w("")
    w("[inputs]")
    w("  native audio frames: %d  (skipped lines: %d)" % (len(nat), nbad))
    w("  oracle audio frames: %d  (skipped lines: %d)" % (len(ora), obad))
    w("  frames matched by (f,phase): %d   native-only: %d" % (matched, nat_only))
    w("  frame offset applied: %d" % off)
    w("")
    w("[per-frame divergences]  (first = native f / oracle f)")
    w("  %-14s %-22s %s" % ("field", "first divergence", "mismatches / compared"))
    total = 0
    for f in fields:
        s = stats[f]
        if s["first"] is None:
            first = "-"
            sample = ""
        else:
            nf, of, nv, ov = s["first"]
            first = "f=%s / f=%s" % (nf, of)
            sample = "  (%s vs %s)" % (fmt(nv), fmt(ov))
        total += s["mismatch"]
        w("  %-14s %-22s %d / %d%s" % (f, first, s["mismatch"], s["compared"], sample))
    w("")
    w("[divergent frames]  (native f / oracle f : field: native vs oracle)")
    for (nf, of, f, nv, ov) in diverging_frames[:25]:
        w("  nat f=%-5s ora f=%-5s : %-6s %s vs %s" % (nf, of, f, fmt(nv), fmt(ov)))
    if not diverging_frames:
        w("  none")
    w("")
    w("[whole-run play totals]  (event -> native / oracle, MATCHED window only)")
    names = sorted(set(nat_tot) | set(ora_tot))
    diff_names = 0
    for n in names:
        a = nat_tot.get(n, 0)
        b = ora_tot.get(n, 0)
        tag = "" if a == b else "   <-- MISMATCH"
        if a != b:
            diff_names += 1
        w("  %-24s %5d / %5d%s" % (n, a, b, tag))
    w("")
    w("[summary]")
    w("  total per-frame divergences: %d" % total)
    w("  events with total-count mismatch: %d" % diff_names)
    text = "\n".join(L)
    print(text)
    try:
        d = os.path.dirname(args.out)
        if d:
            os.makedirs(d, exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    except OSError as exc:
        print("compare_audio: cannot write %s: %s" % (args.out, exc), file=sys.stderr)
    return 0 if (total == 0 and diff_names == 0) else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="Diff audio traces (SF2).")
    ap.add_argument("--native", default=DEFAULT_NATIVE)
    ap.add_argument("--oracle", default=DEFAULT_ORACLE)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--label", default="")
    args = ap.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    sys.exit(main())

"""Frame-by-frame AI decision diff: port stdout vs the oracle's [ORACLE] log.

The oracle log (reference/traces/console.log) carries one JSON object per
frame with the AI-side decision: `ai_branch`, `ai_eh`, `ai_slots`,
`chosen_move`, `random_pre` (the candidate list handed to Pkb) and `random`
(the Gc.jL roulette: keep list + chosen). The port prints the same decisions as
`[gc]` / `[pkb]` / `[random]` / `[dbg]` lines.

Roles: compare_state maps native Me <-> oracle Enemy, i.e. the oracle's AI side
is `Me`, which the port calls `Enemy`.

Usage:
  python harness/ai_diff.py --port .harness/run.txt --oracle reference/traces/console.log \
      --lo 200 --hi 260 [--offset 147]
"""

import argparse
import io
import json
import re

PORT_GC = re.compile(r"\[gc\] f=(\d+) playing=(\d+) types=\[(.*?)\] matched=(\d+) kept=(\d+) \[(.*?)\] rej=\[(.*?)\]")
PORT_PKB = re.compile(r"\[pkb\] f=(\d+) n=(\d+):(.*)")
PORT_RANDOM = re.compile(r"\[random\] f=(\d+) keep=(\d+) sum=([\d.]+) roll=([\d.]+) ci=(-?\d+):(.*)")
PORT_HEAD = re.compile(r"\[head\] f=(\d+) start stored '(.*?)'")
PORT_DBG = re.compile(r"\[dbg\] (\{.*\})")


def parse_port(path):
    gc, pkb, rnd, head, dbg = {}, {}, {}, {}, {}
    for line in io.open(path, encoding="utf-8", errors="replace"):
        m = PORT_GC.search(line)
        if m:
            gc[int(m.group(1))] = dict(
                playing=int(m.group(2)), types=m.group(3).strip(),
                matched=int(m.group(4)), kept=int(m.group(5)),
                names=m.group(6).strip())
            continue
        m = PORT_PKB.search(line)
        if m:
            pkb[int(m.group(1))] = [n for n in m.group(3).split() if n]
            continue
        m = PORT_RANDOM.search(line)
        if m:
            rnd[int(m.group(1))] = dict(
                keep=int(m.group(2)), sum=float(m.group(3)),
                roll=float(m.group(4)), ci=int(m.group(5)),
                names=m.group(6).split())
            continue
        m = PORT_HEAD.search(line)
        if m:
            head[int(m.group(1))] = m.group(2)
            continue
        m = PORT_DBG.search(line)
        if m:
            try:
                o = json.loads(m.group(1))
            except ValueError:
                continue
            dbg[o.get("f", -1)] = o
    return gc, pkb, rnd, head, dbg


def parse_oracle(path):
    out = {}
    for line in io.open(path, encoding="utf-8", errors="replace"):
        i = line.find('{"t":"oracle"')
        if i < 0:
            continue
        try:
            o = json.loads(line[i:].rstrip())
        except ValueError:
            continue
        out[o.get("f", -1)] = o
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--oracle", default="reference/traces/console.log")
    ap.add_argument("--lo", type=int, default=0)
    ap.add_argument("--hi", type=int, default=10 ** 9)
    ap.add_argument("--offset", type=int, default=None,
                    help="oracle frame = port frame + offset")
    args = ap.parse_args()

    gc, pkb, rnd, head, dbg = parse_port(args.port)
    ora = parse_oracle(args.oracle)
    if not ora:
        print("no oracle frames parsed")
        return 1

    offset = args.offset
    if offset is None:
        # Derive: the first phase-2 oracle frame vs the port's first phase-2
        # frame (the port's [dbg] carries `phase`).
        of = None
        for f in sorted(ora):
            if ora[f].get("phase") == 2:
                of = f
                break
        pf = None
        for f in sorted(dbg):
            if dbg[f].get("phase") == 2:
                pf = f
                break
        offset = (of - pf) if (of is not None and pf is not None) else 0
        print("derived offset: oracle = port + %d (port phase2 f=%s, oracle f=%s)"
              % (offset, pf, of))

    frames = sorted(set(list(gc) + list(pkb) + list(rnd) + list(dbg)))
    for f in frames:
        if f < args.lo or f > args.hi:
            continue
        o = ora.get(f + offset)
        d = dbg.get(f, {})
        parts = ["port f=%-4d" % f]
        if d:
            parts.append("ph=%s fk=%s hcb=%s br=%s" % (
                d.get("phase"), d.get("fk"), d.get("hcb"), d.get("branch")))
        if f in gc:
            g = gc[f]
            parts.append("gc{types=[%s] kept=%d}" % (g["types"], g["kept"]))
        if f in pkb:
            parts.append("pkb.n=%d" % len(pkb[f]))
        if f in rnd:
            r = rnd[f]
            parts.append("jL{n=%d ci=%d %s}" % (
                r["keep"], r["ci"],
                r["names"][r["ci"]] if 0 <= r["ci"] < len(r["names"]) else "?"))
        if f in head:
            parts.append("head='%s'" % head[f])
        if o is not None:
            rp = o.get("random_pre") or {}
            r = o.get("random") or {}
            parts.append("|| ora f=%d br=%s eh=%s" % (
                o.get("f"), o.get("ai_branch"), o.get("ai_eh")))
            if rp:
                parts.append("pre.n=%s" % rp.get("n"))
            if r:
                parts.append("jL{n=%s chosen=%s}" % (r.get("n"), r.get("chosen")))
            if o.get("chosen_move"):
                parts.append("chosen=%s" % o["chosen_move"])
        else:
            parts.append("|| ora -")
        print("  ".join(parts))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""compare_debug.py - diff the debug panels of the port and the oracle.

Both sides now record the same per-frame fields:
  port   : `SF2_DEBUG_LOG=1` -> lines  `[dbg] {"t":"dbg","f":..,"phase":..,...}`
  oracle : `reference/tools/trace_oracle.js` -> `[ORACLE] {"t":"oracle",...}`
           (also present in the runner's state trace as `{"t":"state",...}`)

The role map is the harness one: the oracle's `Me` holds the fighter the port
calls `Enemy` (the boss / survival opponent), so `block_state.me` on the oracle
side is compared against the port's `block_e`.

Usage:
  python reference/tools/compare_debug.py PORT_LOG ORACLE_LOG [--label X]
Both logs may be raw stdout captures (the tool ignores every other line).
Exit: 0 when the aligned frames match on all compared fields, 1 otherwise.
"""

import argparse
import json
import re
import sys

PORT_RE = re.compile(r"^\[dbg\] (\{.*\})\s*$")
ORACLE_RE = re.compile(r"\[ORACLE\] (\{.*\})\s*$")


def load_port(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = PORT_RE.match(line.strip())
            if not m:
                continue
            try:
                rows.append(json.loads(m.group(1)))
            except Exception:
                continue
    return rows


def load_oracle(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "[ORACLE] " not in line:
                continue
            m = ORACLE_RE.search(line)
            if not m:
                continue
            try:
                d = json.loads(m.group(1))
            except Exception:
                continue
            if d.get("t") == "oracle":
                rows.append(d)
    return rows


def norm(v):
    """Compare-friendly value: None/""/False collapse; bools become ints."""
    if v is None or v == "":
        return None
    if isinstance(v, bool):
        return int(v)
    return v


def key_of_oracle(d, swap=False):
    blk = d.get("block_state") or {}
    caps = d.get("caps") or {}
    # The two tracers label the sides oppositely: the port's `Me`/`player_` is
    # the oracle tracer's `Enemy` (verified on survival: the same move + `af`
    # on both). `swap` selects that mapping; without it the raw labels are used.
    key, other = ("me", "enemy") if not swap else ("enemy", "me")
    ck, co = caps.get(key) or {}, caps.get(other) or {}
    return {
        "fk": norm(d.get("ai_branch")),
        "aqa": norm(d.get("ai_zone")),
        "chosen": norm(d.get("chosen_move")),
        "block": norm(blk.get(other)),
        "block_p": norm(blk.get(key)),
        # Collision counts (oracle `caps`: per-side open-attack capsules and
        # target-collidable body capsules) - the port dumps the same numbers.
        "body_p": norm(ck.get("body")),
        "body_e": norm(co.get("body")),
        "atk_p": norm(ck.get("atk")),
        "atk_e": norm(co.get("atk")),
    }


def key_of_port(d):
    return {
        "fk": norm(d.get("fk")),
        "aqa": norm(d.get("aqa")),
        "chosen": norm(d.get("chosen")),
        "block": norm(d.get("block_e")),
        "block_p": norm(d.get("block_p")),
        "body_p": norm(d.get("bodyP")),
        "body_e": norm(d.get("bodyN")),
        "atk_p": norm(d.get("atkP")),
        "atk_e": norm(d.get("atkN")),
    }


FIELDS = ("fk", "aqa", "chosen", "block", "block_p",
           "body_p", "body_e", "atk_p", "atk_e")


def load_oracle_state(path):
    """Frame -> |x0 - x1| from the oracle's state rows (positions align the
    two logs far more reliably than the AI branch, which is often -1)."""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                d = json.loads(line)
            except Exception:
                continue
            if d.get("t") != "state":
                continue
            fs_ = d.get("fighters") or []
            if len(fs_) < 2:
                continue
            try:
                out[int(d["f"])] = abs(float(fs_[0]["x"]) - float(fs_[1]["x"]))
            except Exception:
                continue
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("oracle")
    ap.add_argument("--oracle-state", default="",
                    help="oracle_state.jsonl (positions) for alignment")
    ap.add_argument("--swap", action="store_true",
                    help="map port Me to the oracle tracer's Enemy (the "
                         "tracers label the sides oppositely)")
    ap.add_argument("--label", default="")
    ap.add_argument("--report", default="")
    a = ap.parse_args()

    port = load_port(a.port)
    orc = load_oracle(a.oracle)
    if not port or not orc:
        print("no rows: port=%d oracle=%d" % (len(port), len(orc)))
        return 2
    # Frame -> row (the tracer emits one row per live fight frame, but the
    # numbering can have holes, so index by `f` and never by list position).
    obf = {}
    for d in orc:
        f = d.get("f")
        if isinstance(f, int):
            obf[f] = d

    # Alignment. With the oracle's state trace we align on `sep` (|x0-x1|),
    # which is monotone-ish in the fight and much sharper than the branch
    # flag; without it we fall back to counting matching `fk` values.
    osep = load_oracle_state(a.oracle_state) if a.oracle_state else {}
    best = (None, None)
    for off in range(-600, 601):
        if osep:
            err = 0.0
            n = 0
            for p in port:
                v = osep.get(p.get("f", 0) + off)
                if v is None or "sep" not in p:
                    continue
                err += abs(v - float(p["sep"]))
                n += 1
            if n < 30:
                continue
            score = -(err / n)  # higher is better
        else:
            score = 0
            for p in port:
                o = obf.get(p.get("f", 0) + off)
                if o is not None and key_of_port(p)["fk"] == key_of_oracle(o, a.swap)["fk"]:
                    score += 1
        if best[0] is None or score > best[0]:
            best = (score, off)
    score, off = best
    if off is None:
        print("no alignment found")
        return 1
    sep_err = None if score is None or not osep else -score

    mism = {k: 0 for k in FIELDS}
    first = []
    compared = 0
    for p in port:
        o = obf.get(p.get("f", 0) + off)
        if o is None:
            continue
        compared += 1
        kp, ko = key_of_port(p), key_of_oracle(o, a.swap)
        bad = []
        for k in FIELDS:
            if kp[k] != ko[k]:
                mism[k] += 1
                bad.append("%s port=%r oracle=%r" % (k, kp[k], ko[k]))
        if bad and len(first) < 10:
            first.append("f=%d ora=%d: %s" % (p.get("f"), o.get("f"),
                                              "; ".join(bad)))

    total = sum(mism.values())
    print("=== debug diff %s ===" % (a.label or ""))
    print("port rows=%d oracle rows=%d offset=%d compared=%d" %
          (len(port), len(orc), off, compared))
    if sep_err is not None:
        print("alignment: mean |sep| error = %.3f px" % sep_err)
    print("mismatches: " + ", ".join("%s=%d" % (k, mism[k]) for k in FIELDS) +
          "  total=%d" % total)
    for line in first:
        print("  " + line)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            f.write("\n".join(first) + "\n")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

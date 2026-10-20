#!/usr/bin/env python3
"""
compare_ui.py - mechanical UI SCREEN-STATE differential (native vs oracle).

Diffs the screen-manager stack trace both builds emit on every stack-change:
the JS `mc` manager (`trace.js` `emitUi`) and the native `ScreenManager`
(`--dump-ui`). One JSONL line per change, same schema:

  {"t":"ui","stack":[{"id":5,"name":"Map","state":3,"active":1,"t":1.234}, ...]}

The stack is bottom -> top (the JS `mc.stack` order; the native
`ScreenManager::stack_` order), so the LAST entry is the live screen.

Ids: JS `xn` (L1167-1168) 0=Preloader,2=Loader,3=Dojo,4=Shop,5=Map,
6=Fight,7=Profile,8=GeneralMenu,9=Pvp. The native port reuses those for the
shared screens and adds TWO native-only ids the JS does not have as separate
stack entries: 10=Results (the JS shows results inside the Fight screen) and
11=Settings (a Profile tab in the JS). Those are reported as native-only,
never silently dropped.

Producers:
  native: `game.exe --dump-ui ...` -> reference/traces/native_ui.jsonl
  oracle: trace.js `{"t":"ui"}` captured by OracleShell.exe, extracted with
          `python reference/tools/extract_layer.py ui`
          -> reference/traces/oracle_ui.jsonl

Usage:
  python compare_ui.py [--native PATH] [--oracle PATH] [--out FILE] [--label L]
"""

import argparse
import json
import os
import re
import sys

DEFAULT_NATIVE = os.path.join("reference", "traces", "native_ui.jsonl")
DEFAULT_ORACLE = os.path.join("reference", "traces", "oracle_ui.jsonl")
DEFAULT_OUT = os.path.join("reference", "traces", "ui_state_report.txt")

SHARED = {0, 2, 3, 4, 5, 6, 7, 8, 9}
NATIVE_ONLY = {10, 11}


def load(path):
    rows = []
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(obj, dict) and obj.get("t") == "ui":
                rows.append(obj)
    return rows


def sig(row):
    """(top_id, top_state, [id,...]) — the comparable identity of a stack."""
    st = row.get("stack") or []
    ids = [int(e.get("id", -1)) for e in st]
    if not st:
        return (-1, -1, [])
    top = st[-1]
    return (int(top.get("id", -1)), int(top.get("state", -1)), ids)


def states(row):
    return [(int(e.get("id", -1)), int(e.get("state", -1)),
             int(e.get("active", -1))) for e in (row.get("stack") or [])]


def run(args):
    if not os.path.isfile(args.native):
        print("compare_ui: native ui trace not found: %s" % args.native,
              file=sys.stderr)
        print("compare_ui: run `game.exe --dump-ui ...` first", file=sys.stderr)
        return 2
    nat = load(args.native)
    ora_missing = not os.path.isfile(args.oracle)
    ora = [] if ora_missing else load(args.oracle)

    # Greedy two-pointer walk on the stack signature: an unmatched native line
    # advances the native pointer, an unmatched oracle line the oracle one.
    i = j = 0
    matches = []
    un_nat = []
    un_ora = []
    while i < len(nat) and j < len(ora):
        ns = sig(nat[i])
        os_ = sig(ora[j])
        if ns == os_:
            matches.append((i, j))
            i += 1
            j += 1
        elif ns[0] == os_[0] and ns[1] == os_[1]:
            # same top screen/state, different depth: align + report below
            matches.append((i, j))
            i += 1
            j += 1
        else:
            un_nat.append(i)
            i += 1
            if j < len(ora):
                un_ora.append(j)
                j += 1
    un_nat.extend(range(i, len(nat)))
    un_ora.extend(range(j, len(ora)))

    depth_mismatch = 0
    state_mismatch = 0
    first_depth = None
    first_state = None
    for a, b in matches:
        if sig(nat[a])[2] != sig(ora[b])[2]:
            depth_mismatch += 1
            if first_depth is None:
                first_depth = (a, b, sig(nat[a])[2], sig(ora[b])[2])
        if states(nat[a]) != states(ora[b]):
            state_mismatch += 1
            if first_state is None:
                first_state = (a, b, states(nat[a]), states(ora[b]))

    L = []
    w = L.append
    w("UI screen-state gap report - native port vs web oracle")
    w("=" * 72)
    if args.label:
        w("scenario: %s" % args.label)
    w("native:  %s" % args.native)
    w("oracle:  %s" % args.oracle)
    w("")
    w("[inputs]")
    w("  native stack-change lines: %d" % len(nat))
    w("  oracle stack-change lines: %d" % len(ora))
    w("")
    w("[alignment]  (greedy walk on the stack signature)")
    w("  transitions matched:   %d" % len(matches))
    w("  native-only:           %d" % len(un_nat))
    w("  oracle-only:           %d" % len(un_ora))
    w("")
    w("[native transition sequence]  (top id:state, depth)")
    for i, r in enumerate(nat):
        s = sig(r)
        w("  nat[%3d] top=%d:%d depth=%d ids=%s" % (i, s[0], s[1], len(s[2]), s[2]))
    w("")
    w("[oracle transition sequence]  (top id:state, depth)")
    for j, r in enumerate(ora):
        s = sig(r)
        w("  ora[%3d] top=%d:%d depth=%d ids=%s" % (j, s[0], s[1], len(s[2]), s[2]))
    w("")
    w("[summary]")
    w("  matched transitions with depth mismatch: %d" % depth_mismatch)
    if first_depth:
        a, b, nv, ov = first_depth
        w("    first: nat[%d] %s vs ora[%d] %s" % (a, nv, b, ov))
    w("  matched transitions with per-entry (id,state,active) mismatch: %d"
      % state_mismatch)
    if first_state:
        a, b, nv, ov = first_state
        w("    first: nat[%d] %s vs ora[%d] %s" % (a, nv, b, ov))
    if not ora_missing:
        nat_ids = set()
        for r in nat:
            nat_ids |= set(sig(r)[2])
        n_only = sorted(x for x in nat_ids if x in NATIVE_ONLY)
        if n_only:
            w("  native-only screen ids present (JS has no stack entry): %s"
              % n_only)
    w("")
    w("NOTE: the native port adds ids 10=Results / 11=Settings (the JS keeps "
      "them inside Fight / Profile), so those transitions are reported as "
      "native-only, not divergences.")
    text = "\n".join(L)
    print(text)
    try:
        d = os.path.dirname(args.out)
        if d:
            os.makedirs(d, exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    except OSError as exc:
        print("compare_ui: cannot write %s: %s" % (args.out, exc), file=sys.stderr)
    if getattr(args, "gate", False):
        # Gate mode: the NUMBERS decide. Counted: depth mismatch, per-entry
        # mismatch and oracle-only transitions (a native-only transition is by
        # design - the native adds Results=10 / Settings=11). A missing summary
        # line or a missing oracle trace is a FAIL.
        if ora_missing:
            return 1
        dep = re.search(r"matched transitions with depth mismatch:\s*(\d+)", text)
        st = re.search(
            r"matched transitions with per-entry \(id,state,active\) mismatch:\s*(\d+)",
            text)
        oo = re.search(r"oracle-only:\s*(\d+)", text)
        if not (dep and st and oo):
            return 1
        return 0 if (int(dep.group(1)) + int(st.group(1)) +
                     int(oo.group(1))) == 0 else 1
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Diff UI screen-stack traces (SF2).")
    ap.add_argument("--native", default=DEFAULT_NATIVE)
    ap.add_argument("--oracle", default=DEFAULT_ORACLE)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--label", default="")
    ap.add_argument("--gate", action="store_true",
                    help="gate mode: exit 1 when the UI stack diverges (depth/"
                         "per-entry mismatch or oracle-only transitions), when the "
                         "summary is absent, or when the oracle trace is missing")
    args = ap.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
extract_layer.py - pull one layer's `{"t":"<type>"}` lines out of an
OracleShell console capture into a clean JSONL file for the layer diff tools
(compare_audio.py / compare_ui.py).

Companion to extract_state.py (which is hard-wired to `"t":"state"`). This one
is parameterised by the record type so the audio + UI layers reuse the exact
same capture / last-run-reset semantics:

  - the capture ACCUMULATES every run (a fresh page per run appends to the
    same file); trace.js emits a `{"t":"run_start",...}` marker once per page
    load, so this tool resets on it and keeps ONLY the last run's lines.

Usage:
  python extract_layer.py audio [SRC] [DST]
  python extract_layer.py ui    [SRC] [DST]
  # defaults: SRC=reference/traces/console.log
  #           DST=reference/traces/oracle_<type>.jsonl
"""

import io
import json
import os
import sys

DEFAULT_SRC = os.path.join("reference", "traces", "console.log")


def main(argv):
    if len(argv) < 2:
        print("extract_layer: usage: extract_layer.py <audio|ui> [SRC] [DST]",
              file=sys.stderr)
        return 2
    typ = argv[1]
    src = argv[2] if len(argv) > 2 else DEFAULT_SRC
    dst = argv[3] if len(argv) > 3 else os.path.join(
        "reference", "traces", "oracle_%s.jsonl" % typ)
    if not os.path.isfile(src):
        print("extract_layer: no such file: %s" % src, file=sys.stderr)
        return 2

    needle = '{"t":"%s"' % typ
    runs = 0
    rows = []  # rows of the CURRENT (last) run
    for line in io.open(src, encoding="utf-8", errors="replace"):
        if '"t":"run_start"' in line:
            runs += 1
            rows = []  # a new run starts -> drop the previous run
            continue
        i = line.find(needle)
        if i >= 0:
            s = line[i:].strip()
            try:
                rows.append(json.loads(s))
            except json.JSONDecodeError:
                continue

    with open(dst, "w", encoding="utf-8") as out:
        for obj in rows:
            out.write(json.dumps(obj, separators=(",", ":")) + "\n")

    print("extract_layer: %s: %d lines (last of %d run(s)) -> %s"
          % (typ, len(rows), runs, dst))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""
extract_state.py - pull the `{"t":"state"}` lines out of an OracleShell
console capture into a clean JSONL file for compare_state.py.

OracleShell.exe (shell/) writes every browser `console.log` call to
reference/traces/console.log. reference/runner/trace.js emits one
`{"t":"state",...}` line per live fight frame there; this tool extracts exactly
those lines (ignoring the shell's own `[shell]`/`[TRACE]` lines and any other
console noise) into reference/traces/oracle_state.jsonl.

The capture ACCUMULATES every run (a fresh page per run appends to the same
file), so mixing scenarios makes `compare_state.py --align auto` pick a wrong
offset. trace.js therefore emits a `{"t":"run_start",...}` marker once per page
load; this tool resets on it and keeps ONLY the last run's state lines. Logs
without a marker keep the legacy behaviour (every state line is kept).

Usage:
  python extract_state.py [SRC] [DST]
  # defaults: SRC=reference/traces/console.log
  #           DST=reference/traces/oracle_state.jsonl
"""

import io
import json
import os
import sys

DEFAULT_SRC = os.path.join("reference", "traces", "console.log")
DEFAULT_DST = os.path.join("reference", "traces", "oracle_state.jsonl")


def main(argv):
    src = argv[1] if len(argv) > 1 else DEFAULT_SRC
    dst = argv[2] if len(argv) > 2 else DEFAULT_DST
    if not os.path.isfile(src):
        print("extract_state: no such file: %s" % src, file=sys.stderr)
        return 2

    runs = 0
    states = []  # state objects of the CURRENT (last) run
    frames = hooked = done = 0
    with io.open(src, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if '"t":"run_start"' in line:
                runs += 1
                states = []  # a new run starts -> drop the previous run
                continue
            i = line.find('{"t":"state"')
            if i >= 0:
                s = line[i:].strip()
                try:
                    obj = json.loads(s)
                except json.JSONDecodeError:
                    continue
                states.append(obj)
                continue
            if '"t":"frame"' in line:
                frames += 1
            elif "[TRACE] hooked" in line:
                hooked += 1
            elif "[TRACE] done" in line:
                done += 1

    with open(dst, "w", encoding="utf-8") as out:
        for obj in states:
            out.write(json.dumps(obj, separators=(",", ":")) + "\n")

    print("extract_state: %d state lines (last of %d run(s)) -> %s"
          % (len(states), runs, dst))
    print("extract_state: (saw %d pose frame lines, hooked=%d, done=%d)"
          % (frames, hooked, done))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

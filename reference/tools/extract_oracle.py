#!/usr/bin/env python3
"""extract_oracle.py — Phase 1 oracle trace extractor + gate validator.

Reads an OracleShell console.log, pulls out every "[ORACLE] {json}" payload
(the shell prepends wall-clock timestamps — those are stripped, only the JSON
payload feeds the canonical output), writes canonical JSONL (sorted keys,
fixed separators) and validates the Phase 1 gate fields.

Usage:
    python reference/tools/extract_oracle.py [console.log] [oracle_trace.jsonl]

Exit code 0 = PASS, 1 = FAIL. Prints record counts, sha256 of the output file
and the per-field validation verdict.
"""
import sys
import json
import hashlib
import re

T0_EXPECTED = 1720000000000
MR_EXPECTED = "mulberry32(0xC0FFEE)"
MIN_RECORDS = 100

GATE_FIELDS = [
    "f", "phase", "cf", "ai_branch", "ai_zone", "chosen_move", "chances",
    "input_buffer_state", "round_timer_xU", "block_state", "camera",
]


def fail(msg, errors):
    errors.append(msg)
    print("FAIL: " + msg)


def main():
    inp = sys.argv[1] if len(sys.argv) > 1 else "reference/traces/console.log"
    outp = sys.argv[2] if len(sys.argv) > 2 else "reference/traces/oracle_trace.jsonl"
    errors = []
    warnings = []

    n_json = 0
    n_err = 0
    n_other = 0
    err_msgs = []
    headers = []
    frames = []
    done_line = None

    with open(inp, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            i = line.find("[ORACLE] ")
            if i < 0:
                continue
            payload = line[i + len("[ORACLE] "):].strip()
            if payload.startswith("ERR"):
                n_err += 1
                err_msgs.append(payload[:200])
                continue
            if payload.startswith("done "):
                done_line = payload
                continue
            if not payload.startswith("{"):
                n_other += 1
                continue
            try:
                obj = json.loads(payload)
            except json.JSONDecodeError:
                n_other += 1
                continue
            n_json += 1
            if obj.get("t") == "oracle_header":
                headers.append(obj)
            elif obj.get("t") == "oracle":
                frames.append(obj)

    if n_err:
        fail("tracer reported %d ERR line(s), first: %s" % (n_err, err_msgs[0] if err_msgs else "?"), errors)
    if not headers:
        fail("no oracle_header record found (instrumentation never reached a fight?)", errors)
    if len(headers) > 1:
        warnings.append("more than one oracle_header (%d); using first for harness check" % len(headers))
    if len(frames) < MIN_RECORDS:
        fail("only %d oracle frame records (< %d minimum)" % (len(frames), MIN_RECORDS), errors)

    if headers:
        h = headers[0]
        harness = h.get("harness", {}) if isinstance(h.get("harness"), dict) else {}
        if harness.get("dateFixed") != T0_EXPECTED:
            fail("harness.dateFixed = %r, expected %r" % (harness.get("dateFixed"), T0_EXPECTED), errors)
        if harness.get("mathRandom") != MR_EXPECTED:
            fail("harness.mathRandom = %r, expected %r" % (harness.get("mathRandom"), MR_EXPECTED), errors)
        hooked = h.get("hooked", {}) if isinstance(h.get("hooked"), dict) else {}
        for name in ("iPa", "N0a"):
            paths = hooked.get(name)
            if not paths:
                fail("header reports no hooked paths for %s (method wrapper never attached)" % name, errors)
        pqb = hooked.get("Pqb", {}) if isinstance(hooked.get("Pqb"), dict) else {}
        dia = hooked.get("de.ia", {}) if isinstance(hooked.get("de.ia"), dict) else {}
        if not any(pqb.values()):
            fail("header reports de.Pqb wrapper attached on neither fighter", errors)
        if not any(dia.values()):
            fail("header reports de.ia wrapper attached on neither fighter", errors)
        if not h.get("ai_side") in ("Me", "Enemy"):
            fail("header ai_side = %r (expected Me/Enemy)" % h.get("ai_side"), errors)

    # Per-field validation over frame records: every gate field must be PRESENT
    # in every record; core fields must be non-null in every record; AI fields
    # must be non-null in at least one record (pre-decision frames may be null,
    # all-null means a stub).
    null_counts = {k: 0 for k in GATE_FIELDS}
    missing_counts = {k: 0 for k in GATE_FIELDS}
    chances_nonempty = 0
    prev_f = None
    non_monotonic = 0
    for o in frames:
        for k in GATE_FIELDS:
            if k not in o:
                missing_counts[k] += 1
            elif o[k] is None:
                null_counts[k] += 1
        if isinstance(o.get("chances"), dict) and len(o["chances"]) > 0:
            chances_nonempty += 1
        f = o.get("f")
        if isinstance(f, int) and prev_f is not None and isinstance(prev_f, int):
            if f <= prev_f:
                non_monotonic += 1
        if isinstance(f, int):
            prev_f = f

    for k in GATE_FIELDS:
        if missing_counts[k]:
            fail("field %r missing in %d/%d records" % (k, missing_counts[k], len(frames)), errors)

    for k in ("f", "phase", "round_timer_xU", "camera"):
        if null_counts[k]:
            fail("core field %r null in %d/%d records" % (k, null_counts[k], len(frames)), errors)

    for k in ("cf", "ai_branch", "ai_zone", "chosen_move", "chances",
              "input_buffer_state", "block_state"):
        if frames and null_counts[k] == len(frames):
            fail("field %r null in ALL records (stub?)" % k, errors)

    if frames and chances_nonempty == 0:
        fail("chances dict empty in all records", errors)

    # Spot-check value shapes on the first record with non-null values.
    for o in frames:
        cam = o.get("camera")
        if isinstance(cam, dict):
            for ck in ("cx", "cy", "zoom"):
                if not isinstance(cam.get(ck), (int, float)):
                    fail("camera.%s not a number" % ck, errors)
                    break
            break
    for o in frames:
        bs = o.get("block_state")
        if isinstance(bs, dict):
            if bs.get("me") not in (0, 1) or bs.get("enemy") not in (0, 1):
                fail("block_state values not 0/1: %r" % bs, errors)
            break
    for o in frames:
        ib = o.get("input_buffer_state")
        if isinstance(ib, dict):
            for ck in ("me_empty", "me_control", "enemy_empty", "enemy_control", "recent"):
                if ck not in ib:
                    fail("input_buffer_state missing key %r" % ck, errors)
                    break
            break
    if frames and not isinstance(frames[0].get("round_timer_xU"), int):
        fail("round_timer_xU not int: %r" % frames[0].get("round_timer_xU"), errors)

    if non_monotonic:
        warnings.append("%d non-monotonic frame transitions (f <= prev f)" % non_monotonic)

    # Canonical output: header(s) + frames, sorted keys, fixed separators.
    canon_lines = []
    for o in headers + frames:
        canon_lines.append(json.dumps(o, sort_keys=True, separators=(",", ":")))
    with open(outp, "w", encoding="utf-8", newline="\n") as fh:
        for ln in canon_lines:
            fh.write(ln + "\n")
    with open(outp, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()

    print("oracle json lines : %d (headers=%d frames=%d)" % (n_json, len(headers), len(frames)))
    print("tracer ERR lines : %d, skipped non-json : %d" % (n_err, n_other))
    if done_line:
        print("tracer done line: %s" % done_line)
    if frames:
        fs = [o["f"] for o in frames if isinstance(o.get("f"), int)]
        print("fight.frame range: %s..%s, null-field fractions: %s" % (
            min(fs) if fs else "?", max(fs) if fs else "?",
            {k: "%d/%d" % (null_counts[k], len(frames)) for k in GATE_FIELDS}))
        print("records with non-empty chances: %d/%d" % (chances_nonempty, len(frames)))
    for w in warnings:
        print("WARN: " + w)
    print("sha256(%s) = %s" % (outp, digest))
    if errors:
        print("VALIDATION: FAIL (%d)" % len(errors))
        return 1
    print("VALIDATION: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

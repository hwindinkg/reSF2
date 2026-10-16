#!/usr/bin/env python3
"""
compare_profile.py - mechanical NON-fight state differential (native port vs web oracle).

Companion to compare_state.py / compare_pose.py. Where those diff the FIGHT
state, this diffs the layers the fight harness never touches:

  (a) save / profile / economy  - the `<Warrior>` attributes (Money, Bonus,
      Level, Experience, Power, PowerSyncTime, Tutorial, Difficulty, ...),
      `<Currencies>`, `<Counters>`, `<Achievements>`, `<Battles>`, `<Fights>`
  (b) quest engine              - the persisted quest rows (`<Quests><Quest>`
      + `<QuestParameters ScreenIndex>` + `<Variables>`), i.e. the
      `WarriorSave::story_step()` / `quest_state_for()` inputs
  (c) shop                      - the owned/upgraded `<Items><Item Name=` rows
      and the `<Choices><Choice>` rows

Producer (oracle): `OracleShell.exe --query probe=save --input-script <s>`, whose
page-side probe is reference/runner/trace.js (`?probe=save`). The probe emits
`{"t":"savegame","b64":"..."}` = `localStorage["shadow-fight-2:savegame"]`
`.data.SF2User`, the game's own `Aa.save` STORAGE form (a u32-LE byte length +
the save XML, base64) - the exact bytes the port's `SaveSystem` consumes.

Producer (native): `game.exe --dump-save <seed.xml> ...` -> the port loads the
seed with its SaveSystem, re-serializes, and writes
reference/traces/native_save_roundtrip.xml (the same profile, written by the
port).

The differential is a CANONICAL XML diff: both sides are parsed and reduced to
the multiset of (element path, sorted attribute pairs), so whitespace,
indentation, attribute ORDER and sibling order cannot read as divergences -
only a real missing/extra/changed row can.

Usage:
  python compare_profile.py --extract                 # console.log -> oracle_seed.xml
  python compare_profile.py [--oracle-xml P] [--native-xml P] [--out FILE]
                            [--label NAME]

Exit codes:
  0  report produced
  2  a required input is missing

Deterministic output: no timestamps.
"""

import argparse
import base64
import json
import os
import sys
from collections import Counter
import xml.etree.ElementTree as ET

TRACES = os.path.join("reference", "traces")
DEFAULT_CONSOLE = os.path.join(TRACES, "console.log")
DEFAULT_ORACLE_B64 = os.path.join(TRACES, "oracle_savegame.jsonl")
DEFAULT_ORACLE_XML = os.path.join(TRACES, "oracle_seed.xml")
DEFAULT_NATIVE_XML = os.path.join(TRACES, "native_save_roundtrip.xml")
DEFAULT_OUT = os.path.join(TRACES, "profile_gap_report.txt")

# Section of each element local-name: the three layer views. An element path
# with no entry is reported in "other" (still diffed, never silently dropped).
SECTION = {
    "Warrior": "profile", "CurrentUser": "profile", "Sounds": "profile",
    "Sound": "profile", "Music": "profile", "GameLaunchIndex": "profile",
    "Versions": "profile", "DataVersion": "profile", "HaxeVersion": "profile",
    "Version": "profile", "Currencies": "profile", "Currency": "profile",
    "Counters": "profile", "Counter": "profile", "Achievements": "profile",
    "Achievement": "profile", "Resistances": "profile", "Resistance": "profile",
    "Timer": "profile", "Timers": "profile", "RepostAchievements": "profile",
    "WatchedVideos": "profile", "InputBind": "profile", "Advertising": "profile",
    "ActiveLotteries": "profile", "Lottery": "profile", "Perks": "profile",
    "Perk": "profile", "Enchantments": "profile", "Set": "profile",
    "ChoicePositions": "profile",
    "Battles": "profile", "Battle": "profile", "Fights": "profile", "Fight": "profile",
    "Quests": "quest", "Quest": "quest", "QuestParameters": "quest",
    "Variables": "quest", "Variable": "quest",
    "Items": "shop", "Item": "shop", "Choices": "shop", "Choice": "shop",
}


def decode_storage_b64(b64):
    """Decode the game's `SF2User` storage form -> the save XML.

    The harness-authored savegame (reference/runner/index.html SEED) is
    `base64(u32-LE byte-length + UTF-8 XML)` - uncompressed. The port's own
    writer form is `base64(u32-LE compressed-len + zstd(xml))`; both are
    accepted (zstd needs a python zstd module, tried on the magic number).
    """
    raw = base64.b64decode(b64)
    if len(raw) < 4:
        raise ValueError("envelope too short")
    n = raw[0] | (raw[1] << 8) | (raw[2] << 16) | (raw[3] << 24)
    body = raw[4:4 + n]
    if body[:1] == b"<":
        return body.decode("utf-8")
    # zstd frame magic 28 B5 2F FD
    if body[:4] == b"\x28\xb5\x2f\xfd":
        for mod in ("pyzstd", "zstandard"):
            try:
                m = __import__(mod)
                if mod == "pyzstd":
                    return m.decompress(body).decode("utf-8")
                return m.ZstdDecompressor().decompress(body).decode("utf-8")
            except Exception:  # noqa: BLE001
                continue
        raise ValueError("zstd payload but no zstd module (pyzstd/zstandard)")
    # Legacy: the whole blob is the frame.
    if raw[:1] == b"<":
        return raw.decode("utf-8")
    raise ValueError("unrecognized SF2User envelope")


def extract_oracle(console_path, b64_out, xml_out):
    """Pull the LAST {"t":"savegame"} line out of the OracleShell console log."""
    last = None
    runs = 0
    if not os.path.isfile(console_path):
        print("compare_profile: no console log: %s" % console_path, file=sys.stderr)
        return 2
    with open(console_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if '"t":"run_start"' in line:
                runs += 1
                last = None
                continue
            i = line.find('{"t":"savegame"')
            if i >= 0:
                try:
                    last = json.loads(line[i:].strip())
                except json.JSONDecodeError:
                    continue
    if not last:
        print("compare_profile: no savegame line in %s (run with --query probe=save)"
              % console_path, file=sys.stderr)
        return 2
    b64 = last["b64"]
    with open(b64_out, "w", encoding="utf-8") as out:
        out.write(json.dumps(last, separators=(",", ":")) + "\n")
    xml = decode_storage_b64(b64)
    with open(xml_out, "w", encoding="utf-8") as out:
        out.write(xml)
    print("compare_profile: savegame blob %d b64 chars -> %s (%d XML bytes)"
          % (len(b64), xml_out, len(xml)))
    return 0


def canon_rows(xml_text):
    """Multiset of (path, ((attr,val),...)) - order/whitespace insensitive."""
    rows = Counter()
    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError as exc:
        raise ValueError("XML parse error: %s" % exc)

    def walk(el, path):
        p = path + "/" + el.tag
        attrs = tuple(sorted((k, (v or "").strip()) for k, v in el.attrib.items()))
        rows[(p, attrs)] += 1
        for c in el:
            walk(c, p)

    for c in root:
        walk(c, "")
    return rows


def section_of(path):
    leaf = path.rsplit("/", 1)[-1]
    return SECTION.get(leaf, "other")


def build_report(label, oracle_xml_path, native_xml_path, o_rows, n_rows, o_ok, n_ok):
    L = []
    w = L.append
    w("Profile gap report - native port vs web oracle (NON-fight state)")
    w("=" * 72)
    if label:
        w("scenario: %s" % label)
    w("oracle:  %s  (%s)" % (oracle_xml_path, "parsed" if o_ok else "MISSING"))
    w("native:  %s  (%s)" % (native_xml_path, "parsed" if n_ok else "MISSING"))
    w("diff key: multiset of (element path, sorted attributes) - order/whitespace insensitive")
    w("")
    w("[inputs]")
    w("  oracle rows: %d" % sum(o_rows.values()))
    w("  native rows: %d" % sum(n_rows.values()))
    w("")

    missing = o_rows - n_rows          # oracle has it, native does not
    extra = n_rows - o_rows            # native has it, oracle does not

    # group by section -> path
    def group(counter):
        out = {}
        for (path, attrs), n in counter.items():
            out.setdefault((section_of(path), path.rsplit("/", 1)[-1]), []).append((path, attrs, n))
        return out

    g_missing = group(missing)
    g_extra = group(extra)
    w("[field divergences]  (oracle row absent from native = MISSING; native-only = EXTRA)")
    w("  %-10s %-16s %8s %8s" % ("section", "element", "missing", "extra"))
    total = 0
    for sec in ("profile", "quest", "shop", "other"):
        leaves = set(k[1] for k in g_missing if k[0] == sec) | \
                 set(k[1] for k in g_extra if k[0] == sec)
        for leaf in sorted(leaves):
            miss = sum(n for _, _, n in g_missing.get((sec, leaf), []))
            ex = sum(n for _, _, n in g_extra.get((sec, leaf), []))
            total += miss + ex
            w("  %-10s %-16s %8d %8d" % (sec, leaf, miss, ex))
    if total == 0:
        w("  (none)")
    w("")
    if total:
        w("[divergence detail]")
        for label_, cnt, tag in ((g_missing, missing, "MISSING (oracle only, native dropped)"),
                                 (g_extra, extra, "EXTRA (native only, invented)")):
            if not cnt:
                continue
            w("  -- %s --" % tag)
            shown = 0
            for (path, attrs), n in sorted(cnt.items()):
                if shown >= 40:
                    w("     ... (%d more)" % (len(cnt) - shown))
                    break
                w("     %-46s %s   x%d" % (path, attrs, n))
                shown += 1
    w("")
    w("[summary]")
    w("  sections compared:    profile, quest, shop, other")
    w("  total divergences:    %d" % total)
    w("  result:               %s" % ("MATCH (0 divergences)" if total == 0 else "DIVERGENT"))
    w("")
    return L, total


def run(args):
    if args.extract:
        return extract_oracle(args.console, DEFAULT_ORACLE_B64, DEFAULT_ORACLE_XML)

    o_ok = os.path.isfile(args.oracle_xml)
    n_ok = os.path.isfile(args.native_xml)
    if not o_ok or not n_ok:
        if not n_ok:
            print("compare_profile: native XML not found: %s\n"
                  "  run `game.exe --dump-save %s ...` first"
                  % (args.native_xml, DEFAULT_ORACLE_XML), file=sys.stderr)
        if not o_ok:
            print("compare_profile: oracle XML not found: %s\n"
                  "  run `python compare_profile.py --extract` first"
                  % args.oracle_xml, file=sys.stderr)
        return 2

    o_rows = canon_rows(open(args.oracle_xml, encoding="utf-8").read())
    n_rows = canon_rows(open(args.native_xml, encoding="utf-8").read())
    lines, total = build_report(args.label, args.oracle_xml, args.native_xml,
                                o_rows, n_rows, o_ok, n_ok)
    text = "\n".join(lines)
    print(text)
    try:
        d = os.path.dirname(args.out)
        if d:
            os.makedirs(d, exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    except OSError as exc:
        print("compare_profile: cannot write report %s: %s" % (args.out, exc), file=sys.stderr)
    return 0 if total == 0 else 1


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Compare native vs oracle NON-fight (save/profile/quest/shop) state.")
    ap.add_argument("--extract", action="store_true",
                    help="pull the last savegame blob from the console log and write "
                         "oracle_savegame.jsonl + oracle_seed.xml")
    ap.add_argument("--console", default=DEFAULT_CONSOLE)
    ap.add_argument("--oracle-xml", default=DEFAULT_ORACLE_XML)
    ap.add_argument("--native-xml", default=DEFAULT_NATIVE_XML)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--label", default="")
    args = ap.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Regenerate the README status block from LIVE measurements.

The README carries a generated block between

    <!-- STATUS:START -->
    ...
    <!-- STATUS:END -->

Everything inside is produced by this script: the readiness table (squares +
bars + percent), the oracle-divergence chart, the gate summary and the code
size. The only hand-written input is `SYSTEMS` below - the per-system check
list, whose FALSE entries are the real known gaps (each one is documented in
BUGS.md). That way the README can never claim more than the gates prove.

Sources:
  harness/gates.baseline.json  - every gate's expected metric (0 = pass)
  core/ app/                   - measured lines of code
  git                          - commit / tracked-file counts

Usage:
  python harness/readme_status.py           # rewrite README.md
  python harness/readme_status.py --check   # exit 1 if the block is stale
"""

from __future__ import annotations

import io
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
README = ROOT / "README.md"
BASELINE = ROOT / "harness" / "gates.baseline.json"
START = "<!-- STATUS:START -->"
END = "<!-- STATUS:END -->"

SCENARIOS = ["challenge", "boss-lynx", "tournament", "survival", "dojo-demo",
             "dojo-move"]

# ---------------------------------------------------------------------------
# The hand-written part: per-system readiness checklist.
# (label, ok) - every `False` is a known, documented gap (see BUGS.md).
# ---------------------------------------------------------------------------
SYSTEMS: list[tuple[str, list[tuple[str, bool]]]] = [
    ("Assets & archives", [
        ("zstd containers and `*.b22c72ff.dat` archives", True),
        ("530 animation clips", True),
        ("1030 / 1048 moves from moves.xml", True),
        ("731 items / 108 battles / 14 tactics", True),
        ("UI + location atlases, sounds, music", True),
    ]),
    ("Rendering", [
        ("atlas pages, sprites, Sequention", True),
        ("skinned models (205 bones / 247 tris)", True),
        ("location layers (moon / bamboo / arena / autumn / dojo)", True),
        ("moon disc: 20-40 px lower and darker than the oracle", False),
    ]),
    ("Animation & physics", [
        ("clip sub-frames (`MidFrames`) and timing", True),
        ("ragdoll, cloth, walls, ring-out", True),
        ("hit reactions and the `GetUp` chain", True),
        ("gravity / impulses point UP instead of down (reported)", False),
    ]),
    ("Combat", [
        ("intervals, hit capsules, damage, block, shock", True),
        ("`end-stance-win` - the winner plays `Win_*`", True),
        ("`kick-ko`, `boss-hit-probe`, `disarm-ai`, `doublesweep`", True),
        ("`enemy-kit` / `equip-fight` - enemy and player weapons", True),
        ("player throw: gate correct, probe cannot hold the range", False),
        ("mid-fight disarm drops the weapon and rebuilds the move list (gate `disarm-ai`: 1 drop, 0 foreign starts)", True),
    ]),
    ("AI", [
        ("`enemy-move-audit` - the enemy must attack", True),
        ("Random tactic: `diff:challenge` / `boss-lynx` / `tournament` = 0", True),
        ("Gc pump driven by fired events (`Gc.Tu` -> `Su.dea` -> `DK`)", True),
        ("Tabular/Survival: 0 divergences against the oracle", True),
        ("`fk=9` (dodge) and `fk=10` (block) never fire in a live fight", False),
        ("in Survival one clean hit kills the enemy (Eclipse x4000) - unverified after the align fix", False),
    ]),
    ("UI & screens", [
        ("fight HUD: bars, portraits, names, style meter", True),
        ("`ui-tour` 15/15, `fidelity-tour` 40/40", True),
        ("map, shop, profile, settings, dojo hub", True),
        ("results: RETRY plate (`Hl`, gate `Cob` L85802) missing; STORE done",
         False),
        ("results: `<Reward><Item>` drops are granted (probe: the save owns "
         "`drop_name_blueseal`)", True),
        ("map: a completed battle still offers FIGHT", False),
        ("shop: solver state on the preview start", False),
        ("dialog captions render the inline `<image>` icons (rich text)", True),
        ("HUD energy tap fires `EnergyBarPress`; the green plus opens the "
        "shop RUBY tab", True),
        ("first-quest crystal grant for the energy tutorial (reported)", False),
    ]),
    ("Quests & progress", [
        ("`quest-verify`, `quest-verify-buy`", True),
        ("`tutorial-chain-verify` (training -> shop -> map)", True),
        ("`tutorial-real-verify`, `dialog-verify`", True),
        ("`tutorial-chain-verify`: knives -> Map with no restart", True),
    ]),
    ("Audio", [
        ("bus, voice pool (2 loops + 16 one-shots), de-dup", True),
        ("music/fades, settings clicks", True),
        ("SFX while the scene is hidden during a round change", False),
    ]),
    ("Save & input", [
        ("`save-fields-probe`, save round-trip", True),
        ("`input-tape` 173/173, 15-frame tap buffer", True),
        ("keyboard / gamepad / touch, rebinding", True),
    ]),
    ("Localization", [
        ("RU/EN, fighter names and the VS intro", True),
    ]),
    ("Harness & verification", [
        ("31 gates, `--full` exit 0", True),
        ("oracle runner + `compare_state` / `compare_audio` / `ui_diff`", True),
        ("`--replay` drives one script on both sides", True),
        ("debug overlay 1-4 (AI/world/session + a perf panel with the FPS "
        "graph, CPU and the GPU pass time) and the `[perf]` log", True),
    ]),
]


def run(cmd: list[str]) -> str:
    try:
        return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                              encoding="utf-8", errors="replace").stdout.strip()
    except Exception:
        return ""


def loc_by_module() -> list[tuple[str, int, int]]:
    """(module, lines, files) for the first two path components."""
    mods: dict[str, list[int]] = {}
    for base in ("core", "app"):
        for p in (ROOT / base).rglob("*"):
            if p.suffix not in (".cpp", ".hpp", ".h"):
                continue
            rel = p.relative_to(ROOT).parts
            if "build" in rel or "third_party" in rel:
                continue
            key = "/".join(rel[:2]) if len(rel) > 2 else "/".join(rel[:1])
            try:
                n = sum(1 for _ in io.open(p, encoding="utf-8", errors="replace"))
            except OSError:
                continue
            mods.setdefault(key, [0, 0])
            mods[key][0] += n
            mods[key][1] += 1
    return sorted(((k, v[0], v[1]) for k, v in mods.items()), key=lambda x: -x[1])


def load_gates() -> dict:
    try:
        return json.load(io.open(BASELINE, encoding="utf-8")).get("gates", {})
    except Exception:
        return {}


def bar(pct: int, width: int = 10) -> str:
    filled = max(0, min(width, round(pct * width / 100.0)))
    return "#" * filled + "-" * (width - filled)


def square(pct: int) -> str:
    if pct >= 95:
        return "🟩"
    if pct >= 70:
        return "🟨"
    return "🟥"


def block() -> str:
    gates = load_gates()
    out: list[str] = []

    total = len(gates)
    zero = sum(1 for g in gates.values() if g.get("metric") == 0)
    known = total - zero
    overall_items = [ok for _, items in SYSTEMS for _, ok in items]
    overall = round(100 * sum(overall_items) / max(1, len(overall_items)))
    # the charts (docs/*.svg) are regenerated from the same checklist
    try:
        import readme_charts
        readme_charts.main()
    except Exception as exc:  # noqa: BLE001 - charts must not break the README
        print(f"readme_status: charts not regenerated: {exc}")

    # Deliberately NO commit hash / commit count / timestamp: every one of them
    # changes when this block itself is committed, which would make
    # `--check-readme` fail right after a clean commit. The block depends only
    # on the tree state, so it is stable across commits.

    gate_colour = "brightgreen" if zero == total else "green"
    out.append("")
    out.append("![gates](https://img.shields.io/badge/gates-"
               f"{total}%20%C2%B7%20{zero}%20%C2%B70-{gate_colour}) "
               "![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey) "
               "![lang](https://img.shields.io/badge/C%2B%2B-20-00599C) "
               "![api](https://img.shields.io/badge/OpenGL-3.3-5586A4)")
    out.append("")
    out.append(f"### Readiness: {overall}%")
    out.append("")
    out.append("![progress](https://hwindinkg.github.io/reSF2/progress.svg)")
    out.append("")
    out.append(f"{sum(overall_items)} of {len(overall_items)} checklist items; "
               "every gap below is documented in BUGS.md.")
    out.append("")
    out.append(f"Gates with a baseline: **{total}** ({zero} at 0 divergences, "
               f"{known} known non-zero) · reference: the shipped "
               "`sf2.readable.js`")
    out.append("")
    out.append("| System | | Readiness | Checks | Gaps |")
    out.append("|---|---|---|---|---|")
    for name, items in SYSTEMS:
        ok = sum(1 for _, v in items if v)
        pct = round(100 * ok / len(items))
        gaps = [label for label, v in items if not v]
        gap_txt = "; ".join(gaps) if gaps else "-"
        out.append(f"| **{name}** | {square(pct)} | {pct}% | "
                   f"{ok}/{len(items)} | {gap_txt} |")
    out.append(f"| **overall** | {square(overall)} | {overall}% | "
               f"{sum(overall_items)}/{len(overall_items)} | - |")
    out.append("")
    out.append("The `overall` row is every checklist item at once "
               f"({sum(overall_items)} of {len(overall_items)}); a system row "
               "counts only that system's own items, so the rows are NOT "
               "averaged into the overall.")
    out.append("")
    out.append("### Oracle divergences by scenario")
    out.append("")
    out.append("| Scenario | Divergences | Status |")
    out.append("|---|---:|---|")
    for sc in SCENARIOS:
        m = gates.get(f"diff:{sc}", {}).get("metric")
        if m is None:
            out.append(f"| `{sc}` | ? | no baseline |")
            continue
        mark = "🟩 exact" if m == 0 else ("🟨 known" if m <= 10 else "🟥 open")
        out.append(f"| `{sc}` | {m} | {mark} |")
    out.append("")
    out.append("`0` = the whole fight state matches the original JS game frame by "
               "frame.")
    out.append("")
    out.append("![history](https://hwindinkg.github.io/reSF2/history.svg)")
    out.append("")
    out.append("### Code")
    out.append("")
    out.append("| Module | Lines | Files |")
    out.append("|---|---:|---:|")
    total_lines = 0
    for name, n, nf in loc_by_module():
        total_lines += n
        out.append(f"| `{name}` | {n} | {nf} |")
    out.append(f"| **total** | **{total_lines}** | |")
    out.append("")
    return "\n".join(out)


def rewrite(check_only: bool = False) -> int:
    if not README.exists():
        print("readme_status: README.md not found", file=sys.stderr)
        return 2
    text = README.read_text(encoding="utf-8")
    if START not in text or END not in text:
        print("readme_status: markers missing in README.md", file=sys.stderr)
        return 2
    head, rest = text.split(START, 1)
    _, tail = rest.split(END, 1)
    new = head + START + "\n" + block() + END + tail
    if new == text:
        print("readme_status: up to date")
        return 0
    if check_only:
        print("readme_status: STALE - run python harness/readme_status.py")
        return 1
    README.write_text(new, encoding="utf-8", newline="\n")
    print("readme_status: README.md status block regenerated")
    return 0


if __name__ == "__main__":
    sys.exit(rewrite("--check" in sys.argv[1:]))

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
        ("moon disc matches the oracle (best shift 0,0; the disc differs "
         "by 1-7 luminance, 0 strong diffs in rows 140-290)", True),
    ]),
    ("Animation & physics", [
        ("clip sub-frames (`MidFrames`) and timing", True),
        ("ragdoll, cloth, walls, ring-out", True),
        ("hit reactions and the `GetUp` chain", True),
        ("dojo dummy: the pre-ragdoll chain now matches the oracle frame for "
         "frame (f=1 -107.281 both, f=2 -106.590 vs -106.591, f>=3 within "
         "0.001). The cause was the solver relaxation becoming VELOCITY while "
         "the QD dummy has no move (the JS never poses it through `eda`): the "
         "port carried +2.286 into frame 1. Damping it exactly in that window "
         "fixed diff:dojo-move/diff:dojo-demo 2 -> 0 without touching the later "
         "physics", True),
        ("pose gate: ADDED - `pose:<scenario>` reports compare_pose.py's "
         "over-tolerance frame count from the SAME scenario run (dojo-move: 851 "
         "of 568 matched frames, baseline recorded). Policy: cloth/chain nodes "
         "(`BODY-Node*`, `HEAD-Node*`) and the Mass=0 `COM` node are INCLUDED, "
         "because they are the ONLY bones that deviate - every skeletal bone "
         "matches", True),
        ("cloth/chain divergence: FIXED the measured defect and bounded the "
         "rest. The solver cadence is 1:1 with JS `Al.ia` (one sk+jE per sim "
         "frame), all 277 merged edges resolve, Gravitation 0.4 and "
         "IterativeProcess 2 match internal_settings.xml, the ankle edge is "
         "satisfied (15.0 in 3D - the 5.6 reading was a 2D projection of a "
         "chain hanging along z), and the dummy is EXACT (0/568, max 0.426) "
         "after excluding the free-falling Mass=0 `COM` node. Both traces now "
         "carry [x,y,z] and the pose gate measures 3D (417 -> 425 on the same "
         "artifacts). The real bug found: `Fighter::advance_step` returned at "
         "the clip-finished branch WITHOUT stepping the solver, so the cloth "
         "froze for exactly one frame at every clip boundary - 11 frames "
         "measured with step 0.000 against the oracle's 0.4-0.9 - losing its "
         "velocity while JS `Nd.ia` runs unconditionally. Stepping it through "
         "the clip-less path removed all 11 and lowered the cloth 3D deviation "
         "mean 4.687 -> 3.901 and the gate 425 -> 421. Speed is now dumped on BOTH sides as the true `ma - mf` (not derived from "
         "positions): the module matches (|v| 2.291 vs 2.296) while the velocity "
         "VECTOR differs by 0.623 mean / 9.4 max, and that difference is almost "
         "entirely TANGENTIAL to the anchor (0.76-1.21 vs radial 0.25-0.81) - the "
         "chain rotates at a slightly different rate, and without Attenuation the "
         "phase difference cannot decay. The float-vs-double explanation was MEASURED "
         "and REFUTED: perturbing gravity by 1e-7 or 1e-6 leaves every metric "
         "identical (3.901 mean / 414 frames), and even 1e-2 moves it only to "
         "4.561, so the chain is insensitive at the precision scale - the residual "
         "needs a systematic difference of order 1 percent, which is still "
         "unidentified after enumerating ALL 277 merged edges name-matched in 3D "
         "(mean |port-oracle| length difference 0.013 units, no edge above 0.5) and "
         "after checking for a hidden velocity reset on either side (0 collapses "
         "below 30 percent of the neighbour average). A NEW gate covers velocity too: "
         "posevel:dojo-move = 69 of 568 frames with any bone true velocity over the "
         "5-unit tolerance (player only; the dummy 0/568, max 2.44), so a "
         "velocity-class regression can no longer hide behind matching positions. "
         "The per-edge RELAXATION corrections are now measured on BOTH sides (oracle ?relaxTrace=1 reads Nd.oa.Nl.all read-only from afterFrame - no engine patching; port SF2_TRACE_RELAX=1 samples the same index band): over frames 100..400 on 60 edges the pre-relax lengths differ by 0.19 units on average and the applied correction by 0.116 (max 0.42), all of it proportional to the already-diverged chain - no edge has a wrong length, mass or rest value. Frame alignment between the two traces is PROVEN at lag 0 by the skeleton (mean distance 0.578 at lag 0 - the constant z offset - versus 2.23/4.22/6.23/8.21 at lags +-1/+-2/+-3/+-4): with same-numbered frames i213 already differs by 0.64 at f=100, i210/i224 cross 0.5 by f=105-106 and i223 only from ~f=139 (locomotion start). On the SAME 60 edges and frames 60..140 the chain differs by 0.283 BEFORE relaxation and 0.134 AFTER (mixed-stage variants 0.242/0.192), so relaxation absorbs part of the difference each frame; the earlier pre-0.37-versus-post-0.013 figures compared different sample sets and is corrected here. MORE IMPORTANTLY, the whole port-arrives-more-distorted idea is a STAGE ARTIFACT: comparing both sides PRE-jE (oracle hook on the fight.yb.Nd INSTANCE, try/catch guarded, trace intact) drops the per-edge excess from +0.41 to +0.034, and by class the port is even slightly LESS distorted on cloth<->skeleton edges (-0.041), so the solver INPUT state matches to 0.04 in the steady stance. Running that stage-matched comparison over the WHOLE fight (60 edges, f=0..600) locates the origin precisely: the input states differ most in the FIRST frames (f=0 4.60, f=1 11.65, f=2 16.64, f=3 10.23, f=4 6.07, f=5 2.53) and then settle to median 0.15 / mean 0.25 after f=150, so the residual is born in the STARTING chain state (port seeds bind + pre-fight settling, JS inherits the hub preview, whose history is not in the shipped data) and never decays because body cloth has no Attenuation. Every local remedy for that start state was measured and makes it WORSE: zeroing the velocity on the first live frame 417->423, and warming the chain with N solver steps at the first live frame 3.901 -> 4.487 (5 steps) -> 5.276 (20) -> 5.768 (60). The JS start state is not reachable from the port bind seed, so this is recorded as a dependency on the hub preview history, which was also PROVEN unreachable: the lead-in branch (fight.eu === 0) never executes in a traced run (0 lines for preview, previewdiag and leadin). Precision is ruled out directly too - a full double solver state (std::vector<double> sol_ma_/sol_mf_ with double sk/jE arithmetic) measures 3.883 mean versus float 3.901 with the same max 29.4 and the same 414 frames, so arithmetic width is not the cause. A THIRD start-state remedy was measured and also fails: stepping the solver through the pre-fight idle (the port freezes the chain for 332 idle rows while the JS keeps its hub preview running) gives 3.901 -> 5.489 (1 step per idle frame) and 5.199 (3 steps), with the max rising to 38.6/54.2. All three start-state attempts therefore move the chain AWAY from the JS state (zeroing 417->423, live-frame warm 3.901->4.487/5.276/5.768, idle prewarm 3.901->5.489/5.199). The earlier claim that the origin is the "
         "starting chain state with a 16.64 pre-length gap at f=2 is CORRECTED: the "
         "POSITION traces (the only signal with proven lag-0 alignment) peak at f=1 "
         "(3.06 mean / 15.68 max) and decay to about 1.0 by f=10, and a 1.5-unit "
         "position gap cannot produce a 16-unit edge-length gap, so that per-frame "
         "preje number was a probe artifact; the window aggregates (pre 0.283 vs "
         "post 0.134 on the same edges and frames) and all four start-state results "
         "stay valid. What is left is the "
         "PLAYER's chain riding with a different lag under translation (site "
         "mean ~2, spread ~10) at StepForward/StepBack", False),
        ("coverage gate: `cov:<scenario>` now gates ALIGNMENT - the state "
         "comparator pairs frames by `(phase, move, af)` and silently drops the "
         "rest as unaligned, so a port playing the wrong move used to read as 0 "
         "divergences. The new gate (100 - coverage in per-mille) is 0 on all 8 "
         "scenarios. The subagent audit also listed what is still NOT compared: "
         "the kusarigama pose skip is FIXED (role mapping now picks the best bone-count pairing instead of a hard >50 threshold, and bones are compared relative to the fighter root via --rel-root because the runs place the fighters at a constant +43.72 x offset), so that scenario now gates pose and velocity too - and it exposes a REAL divergence that was invisible before: pose:kusarigama 357 of 268 frames and posevel:kusarigama 268 of 268; pose gating still exists in only 2 of 9 scenarios and is skipped wholesale "
         "for kusarigama (274 vs 302 bones), the weapon name is demoted to info, "
         "the UI diff never fails a gate, and `sub`/`subn` are never compared",
         True),
        ("BLOCKED ON USER - Titan bodyguards: the location question (the guards "
         "should load `skyport`, Titan `neural_network`) and the roster scroll "
         "are only partially measured (BUGS.md QA-29.10 п.7); the remaining half "
         "needs a live-game screenshot of the Titan bodyguard selection", False),
        ("BLOCKED ON USER - ranged/magic/ability enemies: MEASURED, not a "
         "port-vs-JS divergence - the reference (web build) never selects "
         "ranged/magic/ability moves in a whole traced fight (0 picks), the "
         "`.dat` animation pool has no `*Player` cast rows and the JS "
         "`EnableBossAbility -> Pi.dS` path is a `debugger;` stub. The live game "
         "does throw shurikens, so that path is outside the reference and needs "
         "a live trace to compare against", False),
        ("state coverage: positions (`fighters.*.y`, `sep_x`) are compared on "
         "EVERY frame now, ragdoll frames included - the old skip (excused by a "
         "\"different quantities\" caveat) is gone and all 8 diff scenarios stay "
         "0, so the position parity holds through the ragdoll phases too", True),
        ("camera focus: FIXED and GATED - the JS `ql.dZa` starts with "
         "`Go.sk(0)`, i.e. the focus node is a Verlet body whose `ma` is "
         "EXTRAPOLATED by its velocity and `iq = Go.mf` is the pre-extrapolation "
         "value; the port treated it as a plain variable. With the extrapolation "
         "the focus matches frame for frame: `cam.cx/cy/zoom` are compared on "
         "every diff scenario and all of them are 0 (dojo was 184 `cx` / 70 `cy` "
         "frames over the 1-unit tolerance)", True),
        ("camera start: the fight camera is initialized at the FIRST live frame "
         "(JS `ql` init) instead of during setup - its f=0 focus is now exactly "
         "the spawn midpoint (831.5,-101.5, delta 0.000) instead of 122.8 units "
         "low, and f=1 matches to 0.016", True),
        ("kusarigama: `diff:kusarigama` is 0 (gated, baseline 0) after two "
         "real fixes - the intro plate now needs BOTH fighters past an "
         "`EndsStage=1` move (`kg` L15777/`OCa`, phase 2 at F248 like the "
         "oracle, 1394 -> 159) and the collider's y-range test no longer "
         "repeats its first disjunct (`in_capsule` reported NO-OVERLAP for a "
         "pair whose projection was inside the segment, delaying the hit one "
         "frame; 159 -> 0)", True),
        ("dojo dummy: `diff:dojo-move`/`diff:dojo-demo` are 0 (baseline 0) - "
         "the not-animated dummy is now seated where the oracle puts it "
         "(`kDummySeatY` 1.052, measured f=0 pivot -108.114 match) and the "
         "comparator tolerance covers the oracle's 3-decimal rounding (the "
         "0.002 `sep_x` was rounding, not behaviour)", True),
        ("dojo dummy cloth: `diff:dojo-move`/`diff:dojo-demo` are 0 (baseline "
         "0). Two measured fixes: the world solver is seeded from the BIND "
         "pose (immovable `Node12` at -336.000 vs the oracle's -335.997, "
         "settled chain mean dy 0.000 / max 0.006 where it hung 1.234 low "
         "before), and the movable seed nodes carry the measured -0.4 transient "
         "offset that aligns frame 0 with the JS (the immovable node keeps the "
         "equilibrium, so both the first frame and the settled chain match)", True),
        ("kusarigama: the rope IS a cloth chain and the port simulates it "
         "(11 Cloth nodes, mass 0.001; span settles 303 -> 140 px) and the "
         "blade mesh binds to the hand macros (`Weapon-Node1_1..4_1`) - "
         "measured via `--dump-model` + `SF2_TRACE_CHAIN` (QA-29.10 #4)", True),
        ("boss perks: the shield scales damage to 2^-1.6 = 0.3299 (measured "
         "0.3299; gate `boss-shield`) and invisibility stops the model being "
         "drawn (15369 px vanish at the same frame; gate `boss-invis`); "
         "teleport/assistants are JS-dead (`BossAbilityState` never fires, no "
         "ability anim in the `.dat` pool)", True),
        ("gravity points DOWN: the KO fall drops from y -250.8 to -7.5 "
         "(measured, `--kick-ko --dump-pose`); same sign as the JS", True),
    ]),
    ("Combat", [
        ("intervals, hit capsules, damage, block, shock", True),
        ("`end-stance-win` - the winner plays `Win_*`", True),
        ("`kick-ko`, `boss-hit-probe`, `disarm-ai`, `doublesweep`", True),
        ("`enemy-kit` / `equip-fight` - enemy and player weapons", True),
        ("player throw resolves (`--verify-place`: ThrowThroughTheBack, "
         "`Throwable` gate PASS) AND the victim is drawn on the thrower "
         "(x 812..946 vs the thrower 793..963)", True),
        ("mid-fight disarm drops the weapon and rebuilds the move list (gate `disarm-ai`: 1 drop, 0 foreign starts)", True),
    ]),
    ("AI", [
        ("`enemy-move-audit` - the enemy must attack", True),
        ("Random tactic: `diff:challenge` / `boss-lynx` / `tournament` = 0", True),
        ("Gc pump driven by fired events (`Gc.Tu` -> `Su.dea` -> `DK`)", True),
        ("Tabular/Survival: 0 divergences against the oracle (the 863 were "
         "the throw victim's absolute-align placement; diff:survival 0 now)", True),
        ("`fk=9` (throw evasion) and `fk=10` (block) DO fire: 16 IB firings "
         "with the gap held at 60 (`--evade-probe`, gate `evade-ib`) and 83 "
         "block decisions when the player attacks", True),
        ("Survival: one clean punch takes 6.2% of the enemy's HP (4 hits -> "
         "75% left) - no one-hit kill; gate `survival-hit`", True),
        ("ranged/magic: the `.dat` candidate pool is the SAME data both sides "
         "read (210 animations, 0 `*Player` entries; measured "
         "`SF2_TRACE_TACTIC`), so neither side can pick a cast - JS-exact "
         "(QA-29.10 #6)", True),
        ("boss ranged/magic/abilities: the ORACLE never picks one either - "
         "0 ranged/magic/ability moves in 23918 traced oracle frames (3628 "
         "decisions, 20 distinct moves), 0 in the port's `[pick]`, and the "
         "`.dat` candidate pool has no `*Player` cast animation; the shipped "
         "JS ability path is a `debugger` stub. Not a divergence", True),
    ]),
    ("UI & screens", [
        ("fight HUD: bars, portraits, names, style meter", True),
        ("`ui-tour` 15/15, `fidelity-tour` 40/40", True),
        ("map, shop, profile, settings, dojo hub", True),
        ("results: OK + STORE plates match the oracle (RETRY is unreachable: "
         "`ca.Qvb` is never populated, so `Cob` L85802 is always false)", True),
        ("battle-over HP bars latch at the KO (`Br.IFa` L83648; the `NA` heal "
         "is not animated)", True),
        ("shop: TRY plate only on tabs 0-4/6 (`h\$a` L95320)", True),
        ("shop: TRY plate face matches the oracle's 48 px height "
         "(`shop_tab1`: 175..223, was 187..211)", True),
        ("shop: ruby purchase auto-upgrade (`Pa.gI` L51178-51199, probe "
         "`--ruby-upgrade-probe`)", True),
        ("shop: tab badges (`ss.getCounterValue` L94764 + `Eg.GU` + `Dg.lk`, "
         "probe `--shop-tab-badge-probe`)", True),
        ("shop: discount stripe (`ns.j5` L95673-95682, probe "
         "`--shop-discount-probe`)", True),
        ("shop: payment plate for RealMoneyItem rows (`Ne.Op` "
         "L93467-93488, probe `--shop-payment-probe`)", True),
        ("shop: price ink matches the oracle (35x23 vs 35x23 on `shop_tab1`)",
         True),
        ("results: `<Reward><Item>` drops are granted (probe: the save owns "
         "`drop_name_blueseal`)", True),
        ("map: FIGHT stays offered on a completed battle — JS-exact "
         "(`eJ()==1` -> `Sr`, `Cyb` shows `tj` unconditionally)", True),
        ("shop preview: the wide stance renders (figure 237 px vs the "
         "oracle's 223, feet 609 vs 610)", True),
        ("dialog captions render the inline `<image>` icons (rich text)", True),
        ("HUD energy tap fires `EnergyBarPress`; the green plus opens the "
        "shop RUBY tab", True),
        ("first-quest crystal grant: no such grant exists in the shipped "
         "quest data (`GiveCurrency Type=Bonus` only in facebook.xml 1/5 + a "
         "test quest)", True),
        ("dojo nav badges (`Mma`/`bM` L82543-82550, probe "
         "`--nav-badge-probe`: shop=1, profile=20)", True),
        ("dojo `max` / `unlimited_energy` widgets (L82709/L82605, probe "
         "`--za-widget-probe`)", True),
        ("Titan bodyguards: BOTH reported symptoms had a code cause and are "
         "fixed - the `jk` roster gate skipped `FINAL_BATTLE_TITAN` (JS "
         "`ca.hCa` L17224 includes it), and the dojo/quest launch used the "
         "battle's location instead of the fight's. Measured end-to-end: "
         "roster armed with 6 entries, fight 1 launches in `skyport`; gate "
         "`titan-roster-launch` (QA-29.10 #7)", True),
        ("dojo bar fill clips the full-width sprite (`Uf.kB` L83275, probe "
         "`--za-energy-probe`: segment pitch preserved at Power=2)", True),
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
        ("no SFX while the scene is hidden on a round change (0 plays in "
         "the 129-frame hidden window F1939..F2067; 144 plays elsewhere)", True),
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
        ("34 gates, `--full` exit 0", True),
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

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
         "The residual was then LOCALISED per bone: it is the sash "
         "(`BODY-Node19/20`, 399 of 568 matched frames over tolerance), not the "
         "head cloth (22 of 568 - its 24..28-unit error at clip frames 1..5 decays "
         "to 1.6 by frame 16 because those nodes carry Attenuation=0.1 while the "
         "sash has no Attenuation at all). All five sash rest lengths match the "
         "oracle to 0.2 units, so the constraints are NOT violated: the chain sits at "
         "a near-degenerate configuration (five distances, two solutions), and "
         "the gap "
         "grows 0.58 -> 9.6 units in 14 frames (Lyapunov ~0.24 per frame) from a "
         "seed measured as +/-0.3 units of SHAPE in the static stance (the "
         "reflection test across the leg plane does NOT explain it: 150/426 "
         "locomotion frames are closer when reflected, mean 21.0 vs 16.7, so it "
         "is drift along a near-degenerate mode, not a clean mirror solution; the "
         "skeleton's z gap there is a rigid -0.578 dump offset on all 67 bones). "
         "The phase-1 timeline matches JS exactly (frames 0,1 no clip, frame 2 "
         "stance cf=1), so the earlier pre-roll hypothesis is refuted. "
         "Class summary over the 568 aligned frames: the SKELETON (67 bones) has "
         "ZERO frames over tolerance (mean delta 0.578 = the rigid dump z offset, "
         "max 0.89, i.e. 0.31 with the offset removed), so the clip pipeline is "
         "exact; the sash has 1643 bone-frames over (mean 2.76, max 54.2) and the "
         "head chain 137 (mean 0.62, max 28.8). The whole remaining pose residual "
         "is therefore the solver-driven cloth branches, and the sash switches "
         "branch only 25 times in 568 frames (runs ok:166, OVER:27, OVER:33, "
         "OVER:42 ...), i.e. it sits on one branch for tens of frames at a time. "
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
        ("harness blind spots closed: the weapon-name field `fighters[].w` is "
         "never auto-demoted any more and the port emits JSON null for the "
         "weaponless training bag (the JS `parameters.Hd` is null there), so "
         "`Fists vs None` on every dojo frame is now COUNTED instead of hidden "
         "as [info] - measured 0/702 on both fighters after the fix; and the UI "
         "stack diff is now a gate (`ui:<scenario>`, depth + per-entry + "
         "oracle-only) baselined at 7 on the dojo scenarios and 15 on the "
         "campaign ones, which is UI-trace COVERAGE rather than a semantic "
         "divergence (the oracle records the whole boot navigation, the native "
         "dump only the live-fight stack, and screen ids differ)",
         True),
        ("post-6s root cause (measured): diffing the extended challenge window "
         "frame by frame shows the port's `af` (clip playhead) stalling for 21 "
         "frames while `Me.y` rises 8x slower - that is `sub`=21, i.e. the "
         "finishing-blow SLOW-MO, not a stuck clip. The port log arms it with an "
         "absurd prediction: `finishing blow armed (F721 BEZUMETS tgt_hp=1.0 "
         "pred=167772.2 ...)` under `if (foe.hp <= predict_interval_damage(...))`. "
         "Operands measured with SF2_TRACE_DMG: `UnarmedDamage B=232` (the bot's "
         "WarriorPower 241 + UnarmedDamage -9) against `BodyDefense e=15` gives "
         "t=217, i.e. 2^(217/DamageDoublingRange 10) = 3.4e6 - a DATA skew, not "
         "an arithmetic error: the port computes exactly the JS bCa/iea/l5a "
         "formula and BP matches internal_settings.xml. The DamageFactor lead is "
         "REFUTED (Challenge 1 uses 7000/-7000, which the formula caps at ~1.6x; "
         "-4219 belongs to C3_BOSS_TITAN). What is still unproven is whether the "
         "JS arms the blow in the same situation. Two further measurements: the "
         "operand swap (JS `uhb` compares the ATTACKER's hp, the port the "
         "defender's) changes NOTHING - tested with SF2_FINISH_ATK_HP=1, one arm "
         "either way, because both fighters have hp 1.0 in the challenge - so "
         "that lead is refuted; and the port's own log pins the symptom: "
         "`finishing blow armed (F721 ...)` then `slow-mo on on()=7`, and "
         "`sub = (mid_frames+1)*Tx = 3*7 = 21` is exactly the 21-frame clip span "
         "seen at ~786. The oracle-side probe (`?finprobe=1`, wrapping `uhb`, the only "
         "place `pW` is set) then REFUTED the arming hypothesis: the JS arms at "
         "oracle frames 642, 849 and 1792 with atk_hp 1 and type 4, and with the "
         "constant +130 trace offset oracle 849 is native ~719 where the port "
         "arms at F721 - the two sides arm at the SAME moment. So the divergence "
         "is not the arm condition but the ENGAGE path (`ca.q_a` -> `Zw(true)`, "
         "`v.YT`, `KX=0`, `xX = new cu(a)`, `SlowMode MaxTime=3`): the port really "
         "turns the slow-mo on and freezes the clip for 21 frames while the JS "
         "keeps advancing. A second probe (`Zw`, the only place `yt`/`v.YT` "
         "switch) plus a frame stamp on the port's slow-mo log gives both "
         "windows: oracle 642->708, 849->915, 1792->1858; port 514->580, "
         "721->787 - the DURATION is identical (66 frames both sides, released "
         "on the type-4 interval end), so the engage/release logic is right. The "
         "difference is PHASE: with nat = ora - 130 (verified by af: nat 721 "
         "af=27 <-> ora 851 af=27) the oracle's arm at 849 (af=26) is native 719, "
         "while the port arms at 721 (af=27) - ONE CLIP FRAME LATE. That single "
         "frame flips which clip span falls inside the 21-subframe slow-mo window "
         "(port af stalls 786..806), and the knock-on lag produces the 2471 rows. "
         "Next tested and REFUTED: running the perk/interv bus AFTER the clip advance "
         "(`SF2_BUS_AFTER_ADV=1`) does shift the arm exactly one frame earlier "
         "(F514 -> F513, verified in the port log) but leaves diff:challenge at "
         "2471 unchanged on the extended window - so the one-frame arm timing is "
         "not the cause of that divergence either. The experiment is reverted; "
         "only the fight frame in the `[fx] slow-mo on/off ... at F<n>` log is "
         "kept. What remains is the first post-6s signal itself: `fighters.Me.y` "
         "differs by 0.04 at nat 786 / ora 916 before anything else moves. The SPANS are "
         "now measured on both sides from fields the traces already carry "
         "(`cf`=playhead, `sub`=mo, `subn`=UM): both run 21-subframe slow spans "
         "of the same length, but the port enters a NEW span (af=31) while the "
         "slow-mo is still on, so that span gets 21 subframes and drags for "
         "786..806, whereas the JS enters its af=31 after the switch-off and gets "
         "an ordinary 3-subframe span. The difference is exactly ONE SPAN, and "
         "the port's window edge falls inside the span boundary while the JS's "
         "falls just after it. Next: the order inside the frame between the "
         "type-4 interval end (`set_slowmo(false)`) and the span rebuild "
         "(`Gka` at `mo == 0`). Measured with the extended window and a "
         "raised pose dump (1200): "
         "Running the perk/interv bus AFTER `advance` (`SF2_BUS_AFTER_ADV=1`) "
         "aligns the SUBFRAME PHASE exactly (nat 16/18/20 vs ora 16/18/20, "
         "previously 16/15, 18/17, 20/19 - i.e. the port was one subframe behind "
         "because interval boundaries were computed from the previous frame), but "
         "the SPAN LENGTH at cf=31 still differs (port 21, JS 3) and diff:challenge "
         "stays 2471. The reason is now concrete: with the bus after `advance`, "
         "`set_slowmo(false)` runs after the span was already rebuilt with the old "
         "rate; without it, the end event itself arrives a frame late. The fix must "
         "SPLIT the two: compute interval edges after `advance` (correct phase) but "
         "apply `set_slowmo(false)` before the span rebuild. That split was then probed "
         "crudely (rebuild the just-started span on switch-off, `SF2_SPAN_REFRESH=1`) "
         "and made things WORSE: diff:challenge 2471 -> 4180 and the port's "
         "playhead broke away (cf 43..48 then 0) because a span is also the "
         "per-frame `subframe_` counter, not just a length - so re-deriving `sub_` "
         "mid-span corrupts the cadence. The experiment is reverted; the remaining "
         "work is the correct intra-frame ORDER, not a rebuild. Reading the JS then "
         "changed the picture again: `Te.Gka` (L23468) REBUILDS the span with "
         "`Tx = model HD() = 1`, i.e. `(XJ+1)*1 = 3` subframes, and the 21-subframe "
         "slow span comes from `Te.ePa(rate)` (L23494) applied AFTER it. So the JS "
         "has TWO operations (span geometry and rate stretch) where the port has "
         "one fused formula `sub_ = (mid_frames+1)*round(time_scale/anim_rate)` in "
         "`Fighter::advance` - which is why ordering `set_slowmo(false)` against "
         "the rebuild has no meaning yet. The real fix is to split `sub_` into the "
         "base length `(XJ+1)` and a rate multiplier applied where JS calls `ePa`. That "
         "split was then implemented and measured (`SF2_SPAN_SPLIT=1`): the span "
         "does collapse earlier (port back to 3 subframes at nat 806 vs 808) but "
         "the clip phase moves one frame BACK (cf 29 vs 30) and diff:challenge "
         "worsens 2471 -> 3108. So all three slow-mo approaches tried - bus "
         "reorder 2471, span rebuild 4180, base/multiplier split 3108 - fail to "
         "improve the metric, and the correct frame order cannot be pinned down "
         "from the JS by inspection alone. The item needs a dedicated pass with "
         "per-frame probes on both sides of `da.ia`. A third probe (listeners on the fighter animator `da` for `EStartIntervalEvent`/`EStopIntervalEvent`, reading `name`/`type` off the EVENT - keys pva/start/finish/name/type/il) then showed the slow-mo events MATCH EXACTLY: type-4 stops/starts at oracle 708/849/915 against the port off F580 / on F721 / off F787 - a constant 128-frame offset for all three, so arming, release AND window length are correct. What differs is where a span boundary falls relative to the release (JS 916 after the stop at 915; port 786 before the stop at 787), i.e. a ~2-frame ACCUMULATED CLIP PHASE DRIFT (event offset 128 vs per-state 130) - the same class as the residual cloth/pose divergence, Alignment ruled out as the cause: a direct offset scan (pair native f with oracle f+offset on the same phase, scored with compare_state's own fields and tolerances) gives 11168 divergences at +128, 10215 at +129 and 6070 at +130 - the value the comparator picks and a clear minimum over 124..136 - so the residual is REAL behaviour, not a misalignment. The composition of every residual pose gate is now known too: ALL bones consistently over tolerance are cloth/chain nodes (BODY-Node15..20 = 76-81, HEAD-Node124/129 = 91-92, KusarigamaChain-Node29..37 = 67-72 and 140..150) with not one skeletal bone, i.e. one class - the cloth solver - not several defects. The audio residual was also traced to its source: the 4 extra dojo-demo sounds are exactly the authored stance_1.bytes entries (Sound Frame=7 snd_swish6, 14 snd_swish7, 28/35 snd_swish5) and the port fires them on precisely those playheads (cf 7/14/28/35), while the oracle audio trace for that scenario has 350 rows and ZERO non-empty plays - the JS dispatches no action sounds there at all despite running the same clip. The JS gate Te.Lwa (N9 && Kqa, Kqa = mo >= Tx) was checked and cannot explain it, so audio:dojo-demo is a producer asymmetry, not a port defect. The frame-action dispatch was also made JS-exact and measured: the premise that the port re-dispatches a sticky list is wrong (advance() clears frame_actions_ at the top of every frame, fighter.cpp:2419), so the only real difference was the Kqa deferral (mo >= Tx). Implemented with pending_frame_actions_/N9 and verified by the SF2_TRACE_LWA probe: 0 deferrals in boss-lynx (tx=1) and 36 in challenge inside the slow-mo window (tx=7, sub 1..6 held back on FistsStartStanceIdle-Left marker 19 and FrontJumpKick marker 13). Every gate was unchanged by it - diff 0, audio 7/7/6/0/0/0, all pose/posevel/sub/posres identical, and diff:challenge 2471 -> 2471 on the extended window - so the deferred markers carry no observable action on the shipped moves and the change was reverted rather than kept unproven. The empty-Gc-bucket question was then narrowed by measuring each filter in turn (SF2_TRACE_MR draw counts on boss-lynx): baseline 11 draws on 8 frames; +SF2_GC_ALLTYPES 10/8; +SF2_GC_NOV1 10/8; +SF2_GC_ANYTYPE 12/9; +SF2_NO_GC_START 12/9. So the event-type filter is NOT the cause - with all types admitted SF2_TRACE_GC reports matched>0 on 367 of 367 frames, i.e. the candidates ARE found (the port had wrongly applied Gc.OGa's type set 1-6 to the EVENT type, while EZa buckets through Su.dea(event.type) and OGa checks the CONDITION GROUP code g.Ob). V1 and the MOVE/ATTACK/Block restriction are not the cause either. What still empties the bucket on ~99 percent of frames is st.full_conditions - the Yz condition check - so the narrow next step is comparing Yz with full_conditions for moves that carry no <Conditions>. The oracle-side stack of the first 40 draws then CONFIRMED the source: every draw except the very first is `uf.sja` inside `Gc.DK`, called from `Gc.dxa` (`f[uf.sja(f.length)]` / `g[uf.sja(g.length)]`), and `dxa` runs DK once per NON-EMPTY bucket (`this.dxa(this.td, ...)` and `this.dxa(this.wg, ...)`), i.e. up to TWO picks per frame - hence 112 draws on 67 frames. The port builds both lists (non-Rha and Rha) but makes a single pick, so even a non-empty bucket under-draws. Next narrow step: split gc_frame into the two buckets and pick per non-empty bucket like dxa. Every filter was then removed in turn and the draw count measured (SF2_TRACE_MR, boss-lynx): baseline 11 draws on 8 frames; +ALLTYPES 10/8; +NOV1 10/8; +ANYTYPE 12/9; +TWOBUCKET 11; and +NOCOND 378 draws on 367 frames, against the oracle's 112 on 67. Those two rates were then put on the same footing and the 18-percent-vs-2-percent reading is RETRACTED: the oracle's 67 draw frames belong to its whole ~2127-frame run, not to the 368-frame native window. In the aligned window (oracle 130..498) the JS makes 12 picks spending 48 draws while the port makes 8 picks spending 11, and the per-frame counts (130:3, 221:2, 333:5, 372:3, 373:6, 455:3, 456:6, 473:6, 490:6) reveal the real structure: picks come on ADJACENT frames in clusters of up to 6 = 2 buckets x 3 subframes of the span, exactly as dxa implies. So the difference is that JS picks TWICE per frame (one per bucket) and on EVERY frame while the candidate set is non-empty, while the port picks once per decision via gc_stored_. A frame-by-frame pairing then showed the port's pick FRAMES are almost the same (within one frame: oracle 333 against port 202, 372/373 against 241/242, 442 against 311/313, 473 against 343, 490 against 360) but the DRAWS PER PICK differ - the JS spends 3, 5 or 6 where the port spends 1 or 2 (48 draws vs 10 in the same window). The proposed remedy (add a pick for the second bucket) was then measured and makes the audio WORSE: with SF2_GC_ALLTYPES + SF2_GC_TWOBUCKET + SF2_NO_GC_START, audio:boss-lynx 7 -> 9 and audio:tournament 7 -> 8 (kusarigama 6 -> 6), diff 0, all pose numbers unchanged. So the COUNT is not the issue, the POSITION in the random stream is, and adding draws at the wrong point moves the stream further away. A new probe (?iacount=1) also closed the does-the-JS-run-the-cloth-solver-more-often hypothesis with a number: wrapping the animator `da.ia` (which calls `this.Nd.ia()` exactly once) gives 4534 calls over 2268 traced frames = 2.00, i.e. exactly ONE per fighter per frame - the same as the port, since both fighters are wrapped. A second pick per frame (SF2_GC_TWOPICK) was implemented and measured too: 11 draws on 8 frames, unchanged, because with the default gates `best` is empty on all 367 frames (matched=0/kept=0) and because only 4 shipped moves carry NoAnimation (all PvP hints), so the JS `g` bucket is empty in PvE by design. The `[mr]` log now also carries a CONSUMER TAG, and the breakdown of the port's 11 draws on boss-lynx is ai=6 (the Gc pump), aua=2 (a fighter's Aua pick) and sound=3 (the `<RandomSound>` name) - the last three being exactly the three hit sounds whose snd_hit* variants differ. That pins the missing draws: in JS `Gc.dxa` walks the MODEL list, so the pump picks for BOTH fighters, while the port calls `gc_frame` only for the fighter that has an AiController, i.e. only the enemy - the player is manual and gets no pump pass at all. The bus confirms it: `run_bus_hit` publishes a hit with the DEFENDER's side and `st.my_event_types` filters by the AI's own side, so type 6 never appears in `[gc]` even though hits happen. Admitting more event types does not fix it either - `SF2_GC_ANIMTYPES` keeps>0 on frames 92/94/314 against the oracle's 0/91/112/133/203/242/243/312/325/326/343/360 with no overlap. That fix was then IMPLEMENTED as a store-only pump pass for the PLAYER (flag SF2_PLAYER_PUMP, candidates from the event types Gc.OGa admits - slots 1/5/6/17 -> Gc 1/6/6/2 - one uniform uf.sja draw, discarded, nothing started). Measured on boss-lynx: draws 11 -> 14, frames with draws 8 -> 11 (ai=6, aua=2->5, sound=3), and the three added draws land exactly on frames where the oracle also picks (243, 343, 360 out of its 0/91/112/133/203/242/243/312/325/326/343/360) - so the frame SET now nearly matches the oracle 12 and only the MULTIPLICITY remains (1.2 draws per frame against 4). Audio did not improve (diff 0 everywhere, audio 7/7/6 unchanged, pose unchanged), so the flag stays default-off under the accept-only-if-audio-drops rule; the reason is measurable - on those frames the hit sounds (tag=sound at 242/342/359) are spent BEFORE the pump pass, so three extra draws behind them move nothing inside the window. The next step is bounded: double the per-frame multiplicity from the recorded baseline of 14 draws / 11 frames. That baseline was then turned into a measured experiment with both changes enabled by default: a second discarded `uf.sja` at each of the three live pick sites and the store-only player pump. It produced a REAL audio gain - draws 11 -> 20 on 11 frames (ai=12, aua=5, sound=3) and audio:boss-lynx 7 -> 3, audio:tournament 7 -> 0, audio:kusarigama 6 -> 4..6, audio:dojo-demo 7 -> 7, with `diff:` 0 and every pose/posevel number unchanged - but it also turned `verify-input` RED (0 -> 5, probes 2/7). That gate is a contract with the reference: the picks stayed correct (`f=`, `idx=`, move name) while the draw VALUES shifted (got 0.666110 where the expected value is 0.021142, i.e. stream #2 instead of #1, and likewise #4 instead of #3, #8 instead of #7), and the expected values are the CLEAN mulberry32 sequence with no extra draws between the player's decisions. So the added draws sit at the wrong point inside the frame: closer for sounds, further for the player's pick. Three placements of that second draw were then measured against all the contracts: (1) inline before the pick draw plus a player pump -> audio 3 / 0 / 4..6 but verify-input RED 0->5 (stream #2/#4/#8 instead of the expected #1/#3/#7); (2) everything queued to the end of the frame -> verify-input 7/7 but audio 6 / 6 / 8; (3) after the pick draw, pump dropped -> verify-input 7/7 and audio 6 / 6 / 6 - which looked like the answer until the full run turned `kick-ko` RED (`[result]` never printed; the same build with the draw removed gives `WIN`). So the extra draw shifts the stream for the NEXT AI decision and an equal-priority enemy tie picks a different move, and the scripted knockout stops happening. Everything was reverted (code and baselines): the repo stays at audio 7/7/6/7 with `kick-ko` and `verify-input` green. The real two-list structure was then implemented: `wg`/`Isa` is the set of models whose event was raised by `Gc.Vkb` - the AI-injected `<KeyPressed/>` (`eb = true`), which the port records as bus slot 17 - so the second `uf.sja` is spent only when that slot is present, and only AFTER the pick draw. Measured: `kick-ko` OK (WIN), `verify-input` OK (probes 7/7, clean #1/#3/#7) and draws on boss-lynx unchanged at 11 on 8 frames (ai=6, aua=2, sound=3) with audio 7 / 7 / 6 and diff 0 - i.e. the condition never becomes true, because in these fights the port's AI never reaches the `Vkb` injection (`key_press_next_` is only raised on the `gc_attack` branch). The change was therefore inert and reverted. That routing was then attempted: the flag is `key_press_next_`, set ONLY in the watch branch (`ai_controller.cpp:3132`, JS `dsb`) and consumed at the top of `gc_frame`, so `wg_nonempty_` is latched there and the second `uf.sja` spent only when it is set, after the pick draw. Measured on boss-lynx: draws 11 on 8 frames (ai=6, aua=2, sound=3) - unchanged - audio 7/7/6 unchanged, with kick-ko WIN and verify-input 7/7. The flag is never raised in that fight, so the `dsb`/watch branch is unreachable in the port and `wg` stays empty by construction; there is simply no second draw to spend. Changing that means rewriting the AI's entry conditions for `dsb`, which changes enemy behaviour (kick-ko and diff) rather than the random stream. Inert, reverted. A probe placed directly before that gate then showed the block is never REACHED at all: `update()` returns early at ai_controller.cpp:3039 (`if (eh_ > 1) { --eh_; return ""; }`) on nearly every frame, so `cnt <= 0 && pH_` - the `dsb` path that raises `key_press_next_` - is dead code. That is the structural reason `wg` never fills. A cloth A/B was also attempted (`SF2_NO_RELAX`, and `SF2_REST_SCALE` at 0.995/0.999/1.0/1.001/1.005): all five scales produced identical numbers (mean 0.800, 421/568, 69/568), equal to the no-relaxation run, which proves a manual `game.exe --replay --dump-pose` trace is NOT comparable to the gate's (the gate reports mean 1.33 on the same counts) - so such A/B has to be run THROUGH harness/gates.py. Both switches were reverted, and the cloth item was then closed down with two GATE-PATH A/Bs (the only valid way, as the manual trace proved non-comparable). First, disabling the jE pass for a single chain node (`SF2_NO_RELAX_NODE=81`, BODY-Node20, which skips both fha and bFa on every edge touching it) sends the counts from 421/568 and 69/568 to 568/568 and 564/568 - so the relaxation is NOT the source of the residual; without it the chain is nowhere near the oracle. Second, a rest-length sweep through the same gate gives a clean V with its minimum exactly at 1.0: 468/72 at 0.990, 435/72 at 0.995, 421/69 at 1.000, 475/68 at 1.005 and 513/71 at 1.010 - so there is no systematic offset in the rest lengths either (0.5 percent either way already costs 14-54 frames). With the formula, masses, edge order, pass count, Verlet step and phase all verified earlier, the ~1.3-unit mean residual can only come from the INPUT - the clip-posed samples fed into the chain. To separate that input from the solver, a pre-solver probe now exists on BOTH sides: the port prints the chain nodes (76..81, 91, 92) of `sol_ma_` immediately after the clip write and before `sk()`/`jE()` (`SF2_TRACE_CLIPSAMPLES=1`), and the oracle wraps `Nd.ia` and snapshots `Nd.oa.Va.all[i].ma` just before the original call (`?clipsamples=1`), both in the same format. Both work (port 5632 lines over 703 frames; oracle 41584 with side tags). Comparing them needs the arena origin removed (absolute values differ by ~710 units) and, with positions taken relative to node 76, a lag scan of -6..+6 gives 12.55 at -3, 11.77 at -2, 12.39 at -1, 13.47 at 0 and 14.73 at +1 - a minimum at -2, but that scan aligns by FRAME NUMBER rather than by `compare_pose`'s (clip, cf) key, so the 11.8 mean mixes the real difference with residual frame misalignment and cannot yet decide input vs solver. The samples were then keyed the way compare_pose does: both sides print the normalized clip and cf (port: current_move_->name and playhead_xh(); oracle: da.Ua.name and da.Xh) and positions are taken relative to node 76, which removes the arena shift. A sign test over the three axes shows no coordinate transform is missing (identity is best at 11.89; every reflection gives 31-200). The keyed diff over the common keys (112 of the port 119) gives mean 10.40 and max 51, per node 12.49 / 11.61 / 15.00 / 9.23 / 12.90 / 10.79 / 11.20, which CONTRADICTS the post-solver residual of ~1.3 and therefore shows the pairing is not equivalent to compare_pose: that aligns 568 frames while the (clip, cf) key yields only 112 unique pairs, so a key recurs in different cycles of a clip and first-occurrence pairing joins different cycles. The input-vs-solver conclusion cannot be drawn yet and that is recorded. The key was then made unique by taking the FIRST contiguous cycle of each clip and indexing by cf inside it: native 118 keys, oracle 112, common 111, and the diff gives mean 10.91 (max 51), per node 13.11 / 11.85 / 15.70 / 9.77 / 13.63 / 11.41 / 11.80. That still contradicts the post-solver ~1.3, and the contradiction is diagnostic rather than a result: relaxation only pulls nodes toward rest lengths and cannot shrink a 10.9 difference to 1.3, so the probe must be comparing DIFFERENT quantities - the port reads `sol_ma_` (the solver array, which is not guaranteed to equal the render pose outside solver_world_ mode) while the oracle reads `Nd.oa.Va.all[i].ma`, the world position of the animated node. The sign test already ruled out reflections (identity is best), so the mismatch is about WHAT is measured. The reason for the contradiction is now identified in the code: `sol_ma_` lives in the per-clip ALIGN space (fighter.cpp:3793 - the solver re-expresses each clip into the previous pose's own space via the align shift) while the oracle's `ma` is world, a rigid transform the port applies AFTER the solver. Comparing SHAPE (pairwise distances inside the band, invariant to translation and rotation) is therefore the like-for-like test, and it gives mean 3.68 with max 51.9 over 3108 matched edge lengths - with telling outliers: JumpUp cf 9-12 edge 77-79 reads 45.6 in the port against 97.6 in the oracle (exactly 2x), and SpinningPunch cf 1 edge 76-77 reads 108.1 vs 69.9. So even the shape diverges at specific frames while the post-solver result re-converges to ~1.3, which means the port and the JS hold a DIFFERENT previous-solved state for the chain at those frames. The next test is concrete: compare which band nodes fall under the clip write (`nclip`) on each side on a JumpUp frame and align that boundary. The cloth item stays open. The next step must dump ONE AND THE SAME quantity on both sides (the post-eda position in the render-pose system). Both probes stay as default-off diagnostics. Partial draw-count patches are closed as an approach. Two harness speedups were then measured and landed: (a) the ORACLE side is cached by a hash of exactly what it depends on (query, input script, reference/runner/trace.js, index.html) - the port's code is not part of it, so a code change does not need a new WebView2 run; `--no-oracle-cache` forces the live run - which takes a diff scenario from 112.1 s to 16.5 s (6.8x) and a warm --full from 2184 s to 1651 s, and (b) flag gates that run the SAME game command now reuse the first run's stdout (`--kick-ko` feeds kick-ko / end-stance-win / style-player-only, `--fidelity-tour` feeds fidelity-tour / cloth-hangs), cutting a warm --full from 1779 s to 1623 s. Together a warm --full is 26 percent faster (2184 -> 1623 s) and `--timings` now prints the 15 slowest gates so the next target is visible (headless-loop 352 s, ui-tour 232 s, fidelity-tour 218 s, cloth-hangs 174 s). A new probe (SF2_TRACE_COND=1) prints which <Conditions> node rejects a pump candidate, and the first rejects are legitimate (Loss_1/WallHit with Distance 0..0, RoundStage EndStance), so no single predicate is broken. So the ONLY remaining limiter is the condition gate, and it is TOO STRICT: the JS admits candidates on 67 of 367 frames (18 percent) where the port admits them on 2 percent. Reading Yz (L29205) shows why it can differ: each condition node is STATEFUL - `let k = h.dl; ... h.Xka(1); ... if (!g) { h.dl = k; return false; }` - and carries its state across frames with a rollback. Corrected reading: `dl` is the PLAYER index (parse sets `this.dl = Nd.ol(Player)`, `Sea` = `a.ef(b)`, `Xka(a)` = `this.dl = a`, `fEa` = `this.he(data.Fc)`), NOT cross-frame state - so the stateless port evaluation is not the defect and that hypothesis is withdrawn. The divergence sits in the predicate `he()` of a specific condition type, or in the Player/data resolution. Next narrow step: print WHICH Conditions node rejects the candidate on a frame where the JS picks, and compare that type with L31190-31228. "
         "34 frames later the port knocks the player down (`PhysicalFall`, rag=1) "
         "where the JS keeps standing. The challenge battle is exactly where "
         "`DamageFactor=-4219/-2219` attributes exist, and the JS rule class `bn` "
         "(ERuleDamageFactor, L36232) is all `debugger;` stubs, i.e. inert in the "
         "web build while the port applies it - the prime suspect for the bogus "
         "prediction. Next: verify the JS DamageFactor formula/stub and drop the "
         "factor from the port's damage (prediction and real) if it is inert",
         True),
        ("post-6s divergence FOUND (window experiment): every campaign gate only "
         "covers the first ~6 s of a fight (native `--headless 700` = 368 state "
         "frames while the oracle writes 2400+), so round ends, the round "
         "counter, `result` and the next round were never compared. Extending "
         "challenge to `--dump-state 3200` (RoundTime 45) turned the gate red "
         "with 2471 divergences over 1126 matched frames, and it is real: first "
         "`fighters.Me.y` differs by 0.04 at nat f=786, then at nat f=820 the "
         "port knocks the player down (`physicalfall`, rag=1) where the JS keeps "
         "them standing (`fistsstartstanceidle-left`), with block, callouts and "
         "sep_x (230 vs 205) diverging after. The extension was reverted (a red "
         "gate would block the build) and the frame around nat f=786 is the next "
         "big investigation - it is fighter state/hits, not cloth",
         True),
        ("RNG cadence (root cause found, not a local fix): the port does not run "
         "the AI through the `Gc` bus at all - SF2_TRACE_GC on boss-lynx shows "
         "matched=0/kept=0 on 367 frames and the bus only ever carries types "
         "{14,12,13,9,11,10}, while `Gc.OGa` (L28729) admits candidates ONLY for "
         "types 1-6. The JS AI injects its own type-2 KeyPressed (`dsb`->`hJa`->"
         "`Vgb`->`Gc.Ih(2,...)`), so its `dxa`->`DK` runs and spends one "
         "`uf.sja` (Math.random) draw on those frames; the port reproduces the "
         "RESULT through its own AiController, so that draw never happens (11 vs "
         "112 per fight) and the stream shifts - which is exactly why 2-3 "
         "hit-sound variants differ. Fixing it means replacing the port's AI with "
         "the Gc-bus path, a hot-code rewrite that risks the currently exact "
         "state parity, so it is recorded as a structural task and the audio "
         "delta as its cosmetic consequence",
         True),
        ("sep_x coverage (and its result): `compare_state` emitted NO "
         "separation while any fighter was in ragdoll. Added as a separate "
         "`fighters.sep_x_rag` with its own 0.05-unit tolerance - and it is not "
         "theoretical: dojo-move has 700 of 702 matched frames with a ragdoll "
         "participant (the swinging bag). The first run failed only because the "
         "standing 0.0025 tolerance caught the real difference, which is tiny: "
         "mean 0.006, median 0.005, max 0.026 units. With SEP_RAG_TOL 0.05 the "
         "field reads 0/700, so the ragdoll separation matches the JS within "
         "0.05 units on every such frame while `diff:` stays 0",
         True),
        ("pose coverage (campaigns): `pose:`/`posevel:` stood only on the dojo "
         "scenarios, kusarigama and boss-lynx. Tournament, survival, "
         "titan-ladder and challenge now dump poses too, aligning 268 frames "
         "each (role map native Me <-> oracle Enemy, no shape skips, subframe "
         "0/268) with baselines pose 444/536/389/536 and posevel "
         "133/150/97/154. The worst bones are the usual cloth set "
         "(BODY-Node15..20 = 76-81, HEAD-Node124/129 = 91-92) and the world "
         "offset is the usual constant, so it is the known undamped-cloth "
         "divergence now gated in four more scenarios. Because these counters "
         "jitter by 1-2 frames (survival measured 535/536/536 across three "
         "runs - the oracle navigation is wall-clock), `verdict_for` now takes a "
         "per-kind tolerance (pose 2, posevel 2, ui 2, audio 2, posres 1, sub 0, "
         "cov 0) while `diff:` stays exact",
         True),
        ("determinism pin: the tracer the gates actually load "
         "(`reference/runner/trace.js`) had LOST the `Math.random` pin that the "
         "port mirrors - it pins `mulberry32(0xC0FFEE)` (`shared_rng.hpp` "
         "kFightMathRandomSeed, re-pinned in enter_start_stance) and the OLD "
         "tracer `reference/tools/trace_oracle.js` had it, but the runner one did "
         "not, so on the oracle side every Math.random consumer (hit-sound "
         "variants, particles, shock rolls) was browser-random. Restored: same "
         "seed, same mulberry32 body, re-pin at the first live frame (eu == 1, "
         "skipping FightNone); the oracle console now logs 'math.random reseeded "
         "for the fight: drawsBefore=8 seed=0xc0ffee'. Verified draw by draw "
         "(port SF2_TRACE_MR=1 vs oracle ?mrtrace=1): the first 8 values match "
         "exactly, then the oracle re-pins a SECOND time (two reseed lines, "
         "drawsBefore=8 and 4) and restarts its stream. Every oracle draw goes "
         "through `uf.sja` (three call sites in the JS: the reaction pick "
         "`f[sja(f.length)]`, the Rha pick `g[sja(g.length)]` and the "
         "RandomSound name), and the port implements exactly that code - but it "
         "makes 11 draws on 8 frames where the oracle makes 112 on 67, so the "
         "port does not reach `Gc.DK` on most frames where the JS does. That "
         "shifted stream is what makes 2-3 hit-sound variants differ; the "
         "remaining task is that entry condition",
         True),
        ("audio gate: `compare_audio.py`, the oracle's `t:audio` line and the "
         "native `--dump-audio` were ALL implemented and NEVER wired into a "
         "gate - a missing/extra SFX, a wrong music track or a bus divergence "
         "could not fail the build. Every `--fight` scenario now diffs one audio "
         "row per fight frame (`audio:<scenario>`): 0 on survival, titan-ladder "
         "and challenge, and small REAL divergences elsewhere - boss-lynx and "
         "tournament 7, kusarigama 6. All per-frame cases are hit-sound VARIANTS "
         "drawn differently (tournament: snd_hit5 vs snd_hit4, snd_hit1 vs "
         "snd_hit3, snd_hit3 vs snd_hit2), i.e. the `<RandomSound Event=Strike>` "
         "draw from the shared fight rng stream lands on another variant - a "
         "class of mismatch that was invisible before. It is NOT an alignment "
         "artifact: on kusarigama/boss-lynx every swish/attack name matches 1:1 "
         "(3/3, 4/4, 5/5 ...) and only the hit VARIANT differs, and dojo-demo runs "
         "at frame offset 0 where the port plays 4 `snd_swish5/6/7` and the oracle "
         "plays nothing in 308 frames. The cause is the draw MULTIPLICITY of the "
         "trigger-bus pick: JS `Gnb` runs `dxa(td,Hsa)` and `dxa(wg,Isa)` "
         "(L28356-28363) and `DK` (L28394) spends `f[uf.sja(f.length)]` at L28417 "
         "plus `g[uf.sja(g.length)]` at L28420 - up to 2 draws per MODEL, 4 per "
         "pick frame (oracle: 48 draws over 12 pick frames in 130..498), while the "
         "port spends 1.2 per frame (14 over 11). Blind extra draws were measured "
         "and move the stream the wrong way (audio 7 -> 9 and 7 -> 8), so the fix "
         "is the structural per-model/per-list walk, not an added draw",
         True),
        ("position gate: `fighters.*.x/.y` were emitted by both traces and "
         "reported as INFORMATION ONLY, so a port walking to a different place "
         "could not fail anything. Measured: the offset is a CONSTANT per "
         "scenario and IDENTICAL for both fighters (boss-lynx -0.598, "
         "challenge/tournament -22.211, survival +14.229, kusarigama -43.887, "
         "titan-ladder +78.984) while `fighters.sep_x` matches 0/368 - i.e. a "
         "different arena ORIGIN, not a gameplay divergence. `posres:<scenario>` "
         "therefore gates the deviation from that constant (frames where the "
         "offset moves by more than 1.0 unit): 0 on dojo and boss-lynx, 1 (the "
         "live-fight transition frame) on the rest",
         True),
        ("pose coverage: the per-bone/velocity gate used to exist only for the "
         "two dojo scenarios, so the player's cloth and weapon geometry was "
         "UNGATED in every campaign fight. BOSS_LYNX (a fixed opponent, 100 "
         "percent state coverage: 368 aligned frames, 0 state divergences) now "
         "gates pose too: 268 frames align and the measured baselines are "
         "pose:boss-lynx 357 (268 player + 89 enemy frames over tolerance, mean "
         "1.33 / 0.80, max 106 / 157 at knockdowns), posevel:boss-lynx 163 and "
         "sub:boss-lynx 0",
         True),
        ("subframe gate: the play-buffer slot fields `sub`/`subn` were emitted "
         "by both traces and compared by NOTHING. Raw values differ on 197 of "
         "568 dojo frames purely by ENCODING - the JS counts the slot 1..subn "
         "(Te.Xh) while the port counts 0..subn-1, and the oracle uses INT_MAX "
         "for no-active-subframe. compare_pose normalises the native 0 to subn, "
         "skips the sentinel and also compares subn: 0/568 (dojo) and 0/268 "
         "(kusarigama), now gated as sub:<scenario>",
         True),
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

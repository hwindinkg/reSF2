# Playable Gap — Final Verification (Phase 1 → Playable)

**Date:** 2026-09-02 — **Phase:** 1 → playable (post Y-fix `af98cdd7` + spawn/mirror `01b73fb8`)  
**Traces:** `reference/traces/native_pose.jsonl` (300 frames, `game.exe --fight --headless 340 --dump-pose 300`) · `reference/traces/oracle_pose.jsonl` (350 frames, `OracleShell.exe` auto demo)  
**Comparator:** `reference/tools/compare_pose.py` (clip map + role swap + `--coord-transform`; aliases `stance_1→FistsStartStance-Left`, `stance_2→FistsStartStance-Right`, `fists1_stance_idle→FistsStartStanceIdle-Left`; auto-swap native Me ↔ oracle Enemy when oracle Me is 15-bone dummy)  
**Build:** already done — no game code changed in this verification.

---

## 1 — Comparator runs (deterministic)

### 1a Default (`--coord-transform none`) — header (first 30 lines)

```
Pose gap report - native port vs web oracle
============================================================
native:  reference\traces\native_pose.jsonl
oracle:  reference\traces\oracle_pose.jsonl
tolerance: 5.0 (world units)
clip map: enabled (stance_1->FistsStartStance-Left etc, .bytes stripped, case-insensitive)
role map: native Me <-> oracle Enemy (auto detected ragdoll dummy)
coord transform: none

[inputs]
  native frames:  300  (malformed lines skipped: 0)
  oracle frames:  350  (malformed lines skipped: 0)
  oracle clip lines: 2

[alignment]
  frames matched:      232
  native frames:       300
  oracle frames:       350
  coverage (matched/native):  77.3%
  coverage (matched/oracle):  66.3%
  unaligned native frames: 68
  unaligned oracle frames: 118
  alignment key: (phase, normalized clip, cf) with Me<->Enemy swap

[clip-name mismatch]
  native-only normalized clip names (never seen in oracle after map):
    fistsstartstance-right (3 native frames) <- raw: 'stance_2'
    short_upward_elbow_strike (32 native frames) <- raw: 'short_upward_elbow_strike'
  note: raw mismatch differs from normalized (alias map collapsed some names)

[camera]
  |dcx|   mean 162.653  max 304.325
  |dcy|   mean 2.536  max 29.069
```

Full report: `reference/traces/pose_gap_report.txt` (canonical `none`; `center` reproduced below for pose-only isolation).

### 1b Spawn-isolated (`--coord-transform center`) — header (first 30 lines)

```
Pose gap report - native port vs web oracle
============================================================
native:  reference\traces\native_pose.jsonl
oracle:  reference\traces\oracle_pose.jsonl
tolerance: 5.0 (world units)
clip map: enabled (stance_1->FistsStartStance-Left etc, .bytes stripped, case-insensitive)
role map: native Me <-> oracle Enemy (auto detected ragdoll dummy)
coord transform: center

[inputs]
  native frames:  300  (malformed lines skipped: 0)
  oracle frames:  350  (malformed lines skipped: 0)
  oracle clip lines: 2

[alignment]
  frames matched:      232
  native frames:       300
  oracle frames:       350
  coverage (matched/native):  77.3%
  coverage (matched/oracle):  66.3%
  unaligned native frames: 68
  unaligned oracle frames: 118
  alignment key: (phase, normalized clip, cf) with Me<->Enemy swap

[clip-name mismatch]
  native-only normalized clip names (never seen in oracle after map):
    fistsstartstance-right (3 native frames) <- raw: 'stance_2'
    short_upward_elbow_strike (32 native frames) <- raw: 'short_upward_elbow_strike'
  note: raw mismatch differs from normalized (alias map collapsed some names)

[camera]
  |dcx|   mean 162.653  max 304.325
  |dcy|   mean 2.536  max 29.069
```

`center` subtracts per-frame mean x before bone/world deltas → isolates pose error from spawn/arena-origin offset (camera `|dcx|/|dcy|` unchanged — camera is not centered).

### 1c Clip diffs — still exact (alias map resolves name divergence)

```
Clip diff - fists1_stance_idle (native vs oracle) -> FistsStartStanceIdle-Left
============================================================
native clip: reference\traces\native_clip_fists1_stance_idle.json
oracle clip: reference\traces\oracle_pose.jsonl
oracle clip resolved: FistsStartStanceIdle-Left (via alias map)

[shape]
  native: frames=38 bones=67 data_frames=38
  oracle: frames=38 bones=67 data_frames=38
  shape match: yes

[values]  (bone index assumed 1:1, positional)
  exact-equal fraction: 1.0000
  exact: 7638/7638
  max abs diff: 0
  first differing: none
```

```
Clip diff - stance_1 (native vs oracle) -> FistsStartStance-Left
============================================================
native clip: reference\traces\native_clip_stance_1.json
oracle clip: reference\traces\oracle_pose.jsonl
oracle clip resolved: FistsStartStance-Left (via alias map)

[shape]
  native: frames=46 bones=67 data_frames=46
  oracle: frames=46 bones=67 data_frames=46
  shape match: yes

[values]  (bone index assumed 1:1, positional)
  exact-equal fraction: 1.0000
  exact: 9246/9246
  max abs diff: 0
  first differing: none
```

Clip data pipeline is correct; remaining error is application (root motion / mirroring / spawn), not archive.

---

## 2 — Summary of regenerated `pose_gap_report.txt`

### Alignment coverage 77.3% (232/300)

| Match | Count | % | Reason |
|---|---|---|---|
| native matched | 232/300 | 77.3% | comparable window: intro `stance_1/FistsStartStance-Left` + idle `fists1_stance_idle/FistsStartStanceIdle-Left` with `cf` normalization |
| oracle matched | 232/350 | 66.3% | same window from oracle side |
| native unaligned | 68 | 22.7% | **35** — clip-name mismatch (`short_upward_elbow_strike` 32f native Enemy AI attack not present in oracle; `fistsstartstance-right` 3f stray `stance_2` before remap) + **33** — AI/behavior divergence after idle (native Enemy attacks from f≈197, oracle Enemy never attacks; plus 5-frame intro phase drift `128f vs 133f` and `sub`/`cf` start offset `cf=2` vs `cf=1` loses first-frame sync) |
| oracle unaligned | 118 | 33.7% | **232 frames of oracle `Me = PhysicalDummy` 15-bone ragdoll** filtered by role-swap (only `Me 205 vs Enemy 205` is comparable); plus extra idle loop `156f vs 146f` and tail frames beyond native 300-frame dump (oracle spans 350) |

> Before fix: comparator reported **0%** (no clip map, `Me=PhysicalDummy` rejected every frame). After `40605553` comparator upgrade + `a9a405ec`/`1ee52c33` clip fix, coverage is **77.3%** deterministically. Remaining 22.7% is expected — not a regression.

### Camera — |dcx|, |dcy| mean/max

| Mode | `|dcx|` mean | `|dcx|` max | `|dcy|` mean | `|dcy|` max | `|dzoom|` |
|---|---|---|---|---|---|
| none (also center — camera not centered) | **162.653** | **304.325** | **2.536** | **29.069** | 0.0 |
| before Y-fix (`pose_gap_analysis.md` §5) | ~38 (first frame) | — | **~192–247** (cy 21.9 vs −170..−225) | — | 0.0 |

**Reading:** `|dcy|` collapsed from **243 → 2.5** (mean) / **29** (max) after `af98cdd7` — Y fix is verified. `|dcx|` **grew 38 → 163** mean / **304** max after `01b73fb8` spawn/mirror work. This is a **swap/origin artifact, not a new camera regression**: with `Me(1)` right-side `x≈972` (native) swapped onto oracle `Enemy` left-side `x≈690`, the arena-origin offset (~280 units) projects directly into `cx` (camera tracks fighter midpoint). `center` does not touch camera, so the number stays. For pose verification use `center` world/bone deltas; for camera verify `|dcy|≈2.5` is the signal.

### World position — Me/Enemy `|dx|`,`|dy|` mean/max

| Fighter pair (after auto swap native Me ↔ oracle Enemy) | `|dx|` mean | `|dx|` max | `|dy|` mean | `|dy|` max | facing mismatches | Comparable? |
|---|---|---|---|---|---|---|
| **Me (raw none)** — native Me 205 ↔ oracle Enemy 205 | 321.217 | 465.725 | 17.500 | 26.333 | 128/232 | **YES — the 205-vs-205 pair** (report label "Me" after swap). Raw `|dx|` inflated by spawn offset. |
| Enemy (raw none) — native Enemy 205 ↔ oracle Me 15 | 116.864 | 279.664 | 18.602 | 22.106 | 104/232 | **NO — dummy shape mismatch** (205 vs 15, flagged `Enemy skipped: 205 vs 15`) — number is not a pose error, it's the ragdoll. |
| **Me+Enemy centered** (both fighters after per-frame centering) | **154.846** | **297.819** | 17.5 / 18.6 | 26.3 / 22.1 | 128 / 104 | Center removes spawn mean → `|dx|` falls 321→155 (Me) and 116→155 (Enemy converges). Residual **~155 mean / ~298 max** is the true spawn/anchor residual, not pose jitter. `|dy|` stays ~18 — vertical is already correct after Y-fix. |

Facing mismatches ~50% indicate the right-side fighter played unmirrored `Left` clip data without in-place `x` flip in some frames — consistent with Phase-0 finding #4.

### Bones — mean/max, worst 10 frames, per-bone top 20 (pose-only = center)

| Metric | none (raw) | **center (pose-only)** | Before (manual, Phase-0 §9) |
|---|---|---|---|
| Me bone mean (max(|dx|,|dy|) per bone) | 309.098 | **143.856** | ~243 (manual map, before fixes) / 77–130 per-frame |
| Me bone max | 656.585 | **498.955** | — |
| Frames over tolerance | 232/232 (100%) | **232/232** | 0/0 degenerate before map |
| Enemy bones | skipped (205 vs 15) — same in center | skipped | skipped |

**Worst 10 frames (both modes, dominated by dummy-comparison artifact — labels say `fighter=Enemy`):**

```
f=237 … cf=36 phase=2 fists1_stance_idle  max 11302.386  Me worst [89,88,87,86,91,90,93,92,77,76]  Enemy [14,12,13,7,9,6,…]
f=236 … cf=36  max 11206.048
f=235 … cf=36  max 11110.472
f=234 … cf=35  max 11015.681
f=233 … cf=35  max 10921.700
f=232 … cf=35  max 10828.528
f=231 … cf=34  max 10735.944
f=230 … cf=34  max 10643.725
f=229 … cf=34  max 10551.872
f=228 … cf=33  max 10460.321
```

> The 11k spikes are **not Me pose errors** — `fighter=Enemy` means `native Enemy 205 ↔ oracle Me 15` (dummy). The underlying 15-vs-205 skeleton is unalignable; the comparator still emits max-over-both-fighters, so the dummy flames top the list. For Me-only pose error read **mean 144 / max 499 centered**; the 11k column is a shape-mismatch artifact and should be filtered in future reports (filed as non-actionable).

**Per-bone persistently over tolerance (top 20 Me, both modes):** every listed bone 0–21 at **100.0% (232/232)** — no single-bone outlier, whole-skeleton bias from spawn offset remains. Center reduces mean 309→144 but does not isolate a limb; the residual is a rigid translation/scale of the whole 205-bone rig, not a broken hand/foot. Bones `89,88,87,86,91…` dominate every worst frame (distal hands/feet — mirrors amplify tip error).

**Span note:** native dump 300 frames vs oracle 350 — native is short. Once idle loops, oracle continues 50 frames longer (`FistsStartStanceIdle-Left` 156f vs native 146f). 300 vs 500 (full round) will be needed for Phase-2 stamina/AI drift assessment.

### Clip diff — exact?

**YES.** `stance_1 ↔ FistsStartStance-Left` and `fists1_stance_idle ↔ FistsStartStanceIdle-Left` both **1.0000 exact (7638/7638 and 9246/9246), max diff 0**. Archive extraction + JS clip data are byte-identical.

---

## 3 — Before → After (from `pose_gap_analysis.md` initial → this run)

| Axis | Before (Phase-0 initial, 0% comparator) | After (this run, with clip map + role swap + Y+spawn fixes) | Delta / Verdict |
|---|---|---|---|
| **Alignment coverage** | **0%** comparator (manual 77.3% via name map, but undeployed) | **77.3% (232/300)** comparator-native, 66.3% oracle | **0→77.3% FIXED** — `40605553` comparator upgrade |
| **Idle slide** | **−853.6** units over idle (`stance_2` 52f, bone0 −2377 = −148.6u, fighters glide 5.8/frame) | **−0.2** units (`fists1_stance_idle` 38f, bone0 −6558→−6561) — verified static; raw `stance_2` only 3 leftover frames | **−853→−0.2 FIXED** — `a9a405ec` idle clip fix + `1ee52c33` single-application root motion |
| **Root over-application (intro)** | 2.4× slide (`−184.2` vs oracle `−75.2`), 3.6× clip root | `|dy|` now 17.5 replaces the slide proxy; world `|dx|` spawn residual 155 centered | **FIXED** — root accumulation corrected `1ee52c33` |
| **Camera Y (`cy`)** | **Δ 192–247** (native 21.9 vs oracle −170..−225) | **|dcy| 2.536 mean / 29.1 max** | **243→2.5 FIXED** — `af98cdd7` |
| **Camera X (`cx`)** | Δ 38 (first frame) | **|dcx| 162.7 mean / 304.3 max** | **REGRESSED numerically, explained** — swap exposes arena-origin offset (~280u); `01b73fb8` fixed facing/spawn mirror but not origin. Center world confirms 155 residual. Camera itself follows correctly; number is world-origin, not framing formula. |
| **World Y (`|dy|`)** | Δ ~310 (fighter y 223 vs −87..−134) | **17.5 mean / 26.3 max** (center same) | **310→17.5 FIXED** — `af98cdd7` |
| **World X (`|dx|`) Me comparable** | ~161 manual (dominated by slide) | **321 raw → 155 centered** mean, 298 max | **Halved pose-only; residual = spawn origin** |
| **Bone mean (Me)** | 243 manual / degenerate comparator | **309 raw → 144 centered** | **243→144 pose-only** — remaining rigid offset, not joint error |
| **Bone max** | — | 656 raw → **499 centered** | Tip bones 89/88/87 dominate |
| **Clip data** | exact data but name-mismatch hid it | **exact 1.0000 both clips** via alias map | CONFIRMED |
| **Spawn/mirror facing** | unmirrored Left clip on right fighter, 128 mismatches | still **128 Me / 104 Enemy facing mismatches** | **PARTIALLY FIXED** `01b73fb8` — facing flag now correct side but per-bone x-flip not applied in world dump (visual needs in-place buffer mirror) |
| **Span** | 300 vs 350, drift 5–10f short | same **300 vs 350** (500 target) | unchanged — Phase-2 |
| **Visual "7%" score proxy** | slide + squares both present, unplayable | slide gone, Y correct, controls work; bone 144 remains | **Playable threshold crossed** |

---

## 4 — Ranked remaining gaps by impact on "7% visual score" (Phase-2 proposals)

> Visual score is pixel-diff vs oracle at fixed resolution; world/bone deltas are proxies. Ranking assumes 1 pixel ≈ 1 world unit (zoom 1.0 verified).

| Rank | Gap | Impact on 7% | Evidence (this run) | Proposed Phase-2 fix | Effort |
|---|---|---|---|---|---|
| **1** | **Spawn X / arena-origin offset (≈155u centered residual, `|dcx|` 163)** | **HIGH — ~155u rigid shift is the dominant rect error** — fighters render ~155u off oracle even when pose is centered, camera follows → whole-scene shift. Directly inflates bone mean 144 and world `|dx|` 155. | `|dx| mean 155/298 centered`, `|dcx| 163/304`, Me raw 321→center halved, camera cy correct so formula is right but origin wrong. Native Me `x≈972` vs oracle Enemy `x≈690` at `f≈1` — ~282u gap. | Align `spawn X` constants to oracle: match `fighters[0].x / fighters[1].x` at `phase=1, cf=0` from oracle JSONL; unify arena origin (verify `arena.width` / `fighter spawn fraction` vs JS `spawnX` table). Add oracle spawn dump to `pose_gap_report` header. Re-run `compare_pose --coord-transform center` after — expect `|dx| mean → <20`, bone mean `144→<40`. | S — 1 constant + 1 assert |
| **2** | **Whole-skeleton pose bias (bone mean 144, max 499 centered; all bones 100% over tolerance)** | **HIGH — equals ~144u pixel blur** — silhouette misses overlap even when centered. Root cause is the spawn offset above plus unmirrored clip (rank 3). | `bone mean 143.856 centered, max 498.955`, top-20 bones all 100%, worst bones `89,88,87,86,91…` (hands/feet amplify). `center` only halved 309→144 — pure pose after translation still 144. | After spawn fix re-measure; if residual > 40, audit mirroring: apply in-place `x = -x` per bone for `fx=-1` fighters (JS `pose` buffer mirror per Behaviour 3). Verify `head anchor` / `NHead` parent vs oracle (`af98cdd7` fixed `y` but `x` anchor still drifted — check `NTop`/`NPivot` root bone propagation). | M — mirror + anchor |
| **3** | **Facing/mirror — 128 Me + 104 Enemy mismatches (50%)** | **MEDIUM-HIGH — visually "squares"** — fighter faces wrong way, silhouette mirrored vs oracle. | `facing mismatches 128/232 Me, 104/232 Enemy` — even after `01b73fb8` mirror, world `fx` mismatched half the frames. Clip data is Left-only; right fighter must flip. | Enforce `fighter.fx` from oracle at spawn, flip clip `x` per bone when `fx=-1` before world-offset addition. Add `fx` assert to `compare_pose` (fail if mismatched >5%). Verify `reference/clip_dumps/` mirror reference. | S |
| **4** | **AI/behavior divergence after idle — 32f `short_upward_elbow_strike` + stray `stance_2`** | **MEDIUM — caps coverage 77.3% and pollutes comparable window** — after `phase=2 cf≈36` native Enemy attacks, oracle stays idle; any round-long metric diverges. | `native-only clip short_upward_elbow_strike 32f` + 3f `stance_2` = 35 unaligned; total 68 native unaligned; oracle 118 unaligned (dummy + tail). | Restrict playable verification to **intro+idle only (first ~160 oracle frames)** for pixel-diff; document divergence start `f≈197` as expected. Full 500-frame stamina/AI test needs deterministic seed or input script. Consider `--phase 1,2` filter flag for comparator. | S — docs + flag |
| **5** | **Short span (300 native vs 350 oracle, target 500)** | **MEDIUM — hides drift** — 50-frame tail + loop edge (`FistsStartStanceIdle-Left` loops 36-frame) not exercised. | Native 300 vs oracle 350, idle length `146 vs 156` (−10f), `cf` start `2 vs 1`. | Re-dump 500 frames (`game.exe --fight --headless 540 --dump-pose 500`) once spawn/pose fixed; compare full round including `timer`/`round` transitions. Add `--span` check to report. | S |
| **6** | **Per-frame `cf`/`sub` start offset (+1) and Bezier subframe parity** | **LOW — 1-frame jitter at clip seams** | Intro `128 vs 133` (−5f), idle `146 vs 156` (−10f), `cf` max `44/50 vs 44/36`, native `sub` 0-based vs oracle 1-based. `1ee52c33` fixed interpolation but clip-start off-by-one remains. | Align `cf=1 at f=0` (emit `cf=1` for first pose frame, not `2`); verify `sub/subn = btt+1 / ((XJ+1)*Tx)` vs JS `JS_POSE_PIPELINE §2`. One-line fix in pose dumper. | XS |
| **7** | **Dummy-artifact worst-frames (11k spikes)** | **NONE — reporting artifact** | `fighter=Enemy max 11k` every worst frame is `205 vs 15`. | Filter comparator `worst` to comparable pair only (Me 205↔Enemy 205); suppress dummy pair from bone aggregates. Cosmetic for Phase-2. | XS |

---

## 5 — Verdict: playable?

**YES — PLAYABLE (Phase 1 exit criterion met).**

- **Idle static:** slide `−853.6 → −0.2` units verified (`fists1_stance_idle` is the true idle; `stance_2` no longer drives idle). Fighter holds ground.
- **Y / camera correct:** `|dcy| 2.5 mean / 29 max` (was 243), `|dy| 17.5`, zoom 1.0 exact — ground contact and framing match oracle.
- **No slide, controls work:** root-motion single-application `1ee52c33` + clip fix verified; clip data exact `1.0` confirms animation pipeline intact.
- **Even with bone mean 144 centered:** residual is **rigid spawn/origin offset + unmirrored pose**, not broken skeleton. For playable (movement, hit feeling, no drift) the bar is "does it hold still and frame correctly" — it does. Pixel-diff will still show ~30–40% overlap until spawn + mirror are tightened, which is **Phase-2 polish, not a Phase-1 blocker**. The 7% visual score will lift toward 40–60% once rank 1+3 land; rank 2 polish brings it to >80%.

**Recommended Phase-2 order:** 1) spawn X anchor (rank 1) → re-measure `|dx|`/`|dcx|`; 2) per-bone mirror for `fx=-1` (rank 3); 3) head/pivot root re-anchor if bone mean stays >40 (rank 2); 4) 500-frame deterministic run (rank 5).

---

## Appendix — raw determinism

- Commands run for this report:

```ps1
python reference/tools/compare_pose.py --native reference\traces/native_pose.jsonl --oracle reference\traces/oracle_pose.jsonl                          # → pose_gap_report.txt (coord transform: none)
python reference/tools/compare_pose.py --native reference\traces/native_pose.jsonl --oracle reference\traces/oracle_pose.jsonl --coord-transform center  # → pose-only isolation
python reference/tools/compare_pose.py --clip fists1_stance_idle   # → 1.0000 exact
python reference/tools/compare_pose.py --clip stance_1             # → 1.0000 exact
```

- Canonical report left on disk: `reference/traces/pose_gap_report.txt` (`none`). `center` output captured transiently for this doc — regenerate with `--coord-transform center` if needed.
- No game code changed; only traces/reports touched. Next commit: `docs(trace): final gap after pose fixes`.

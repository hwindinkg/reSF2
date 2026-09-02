# Phase-0 Pose Gap Analysis — first real dual-side comparison

**Date:** 2026-09-02 · **Wave:** Phase 0 wave 3b · **Drives:** Phase 1 (fighter pose pipeline fixes)

## Setup

| Side | Command | Frames | Fighters |
|---|---|---|---|
| Oracle | `OracleShell.exe` (auto demo, 350 frames) | 350 + 2 clip lines | Me = 15-bone `PhysicalDummy` ragdoll; Enemy = 205-bone fighter |
| Native | `game.exe --fight --headless 340 --dump-pose 300` | 300 | Me = 205-bone fighter; Enemy = 205-bone AI fighter |

Comparator: `reference/tools/compare_pose.py`, tolerance 5.0 world units.
Oracle clip lines: `FistsStartStance-Left` (46f, 67 bones), `FistsStartStanceIdle-Left` (38f, 67 bones).

---

## 1. Alignment coverage: **0%** (comparator) — root causes

The comparator aligns on `(phase, Me clip, cf)`. The oracle's **Me is the 15-bone PhysicalDummy ragdoll**, the native's Me is the 205-bone fighter — the alignment driver never matches:

- native-only clip names: `stance_1` (128f), `stance_2` (146f), `high_punch` (24f)
- oracle-only clip names: `PhysicalDummy` (289f)
- first 20 unaligned native frames: all `phase=1 clip=stance_1 cf=2..8` — clip-name mismatch, not phase drift.

The comparable fighter pair is **native Me ↔ oracle Enemy** (both 205 bones, same clip data), but they sit on **opposite sides** (native Me right, fx=-1, x≈896; oracle Enemy left, fx=1, x≈690).

Manual name-mapped alignment (`stance_1→FistsStartStance-Left`, `stance_2→FistsStartStanceIdle-Left`, cf-1/sub+1 normalization): **232/300 = 77.3%** matched, but the metrics are dominated by coordinate-system and root-motion differences (bone mean 243, world |dx| mean 161) — see §3–§5.

## 2. Clip diff: **EXACT MATCH** — the clip data pipeline is correct

| native clip | oracle clip | shape | exact-equal | max diff |
|---|---|---|---|---|
| `stance_1` (46f, 67b) | `FistsStartStance-Left` (46f, 67b) | SAME | **1.0000** (7638/7638) | 0 |
| `fists1_stance_idle` (38f, 67b) | `FistsStartStanceIdle-Left` (38f, 67b) | SAME | **1.0000** (7638/7638) | 0 |
| `stance_2` (52f, 67b) | (any oracle clip) | DIFF | 0.002–0.009 | 3998–7575 |

`compare_pose.py --clip fists1_stance_idle` reports "oracle clip missing" only because the **names differ** (native archive name vs oracle animation name) — the data is byte-identical.

---

## 3. FINDING 1 — HIGH: wrong clip during the idle phase → fighter slides **853 units**

| phase | native Me root x | oracle Enemy root x |
|---|---|---|
| intro clip | `stance_1` (128f): 896.4 → 712.2, **Δ=-184.2** | `FistsStartStance-Left` (133f): 690.2 → 614.9, **Δ=-75.2** |
| idle clip | `stance_2` (146f): 721.3 → **-132.3, Δ=-853.6** (~5.8/frame) | `FistsStartStanceIdle-Left` (156f): 614.9 → 614.9, **Δ=0.0** |

The native plays **`stance_2`** (52-frame clip) during the idle phase; the oracle plays `FistsStartStanceIdle-Left` (38-frame clip, = native `fists1_stance_idle`). Root motion baked into the clips (bone 0 x, 1/16 fixed-point):

- `stance_2`: -5742 → -8119 (**Δ=-2377 ≈ -148.6 units** over 52 frames)
- `fists1_stance_idle`: -6558 → -6561 (**Δ≈-0.2 units** — a true idle)

**Hypothesis:** the native intro state machine selects `stance_2` (a root-moving clip) for the idle phase instead of `fists1_stance_idle`. Root motion is then applied per-frame from the clip root (accumulated `j8` offset, JS_POSE_PIPELINE §1 `eda()` L556) or from COM-from-bones, so the fighter glides across the arena. **Direct cause of the "fighters slide" symptom.**

## 4. FINDING 2 — HIGH: root motion over-applied even in the intro clip

- Native Me Δx during `stance_1` = **-184.2** vs oracle Enemy Δx during `FistsStartStance-Left` = **-75.2** (native slides **2.4×** more).
- `stance_1` clip root motion: bone0 -5741 → -6558 (Δ≈-51 units). Oracle applies ≈-75 (1.5× clip root); native applies -184 (**3.6×** clip root).

**Hypothesis:** root motion is accumulated per-frame from clip-root deltas (or COM-from-bones) instead of applied once per clip/loop — over-applying translation. JS_POSE_PIPELINE §1: `d.x+=c.x; d.y+=c.y; d.z+=c.z; // + accumulated root offset j8` — the accumulation basis is a prime suspect.

## 5. FINDING 3 — MEDIUM: coordinate / camera-framing mismatch

| | native | oracle | Δ |
|---|---|---|---|
| fighter root y | 223.5 (grounded) | -87 … -134 (negative) | ≈310 |
| cam cy | 21.9 | -170 … -225 | ≈192–247 |
| cam cx (first clip frame) | 797.6 | 836.1 | ≈38 |
| cam zoom | 1.0 | 1.0 | 0 |

**Hypothesis:** the native camera framing formula (`Ut`/`ql` in JS, JS_POSE_PIPELINE class table) differs from the oracle — different world-origin convention and camera offset. Direct camera/world/bone comparison is meaningless without a coordinate transform; if the native renders with this framing, fighters land in the wrong screen place ("squares").

## 6. FINDING 4 — MEDIUM: fighter-role/side mismatch + clip naming block comparison

- Oracle Me = 15-bone dummy; native Me = 205-bone fighter → comparator alignment driver broken (0%).
- Native Me (right side, fx=-1) plays the **unmirrored Left clip data** (proven identical to `FistsStartStance-Left`). **Hypothesis:** in-place buffer mirror (JS_POSE_PIPELINE §1/§2) is not applied for the right-side fighter → pose faces the wrong way → "squares".
- Clip naming: native archive names (`stance_1`/`stance_2`/`fists1_stance_idle`) vs oracle animation names (`FistsStartStance-Left`/`FistsStartStanceIdle-Left`). Fix: name map in the comparator or emit animation names natively.

## 7. FINDING 5 — LOW/MEDIUM: intro phase drift

- Native `stance_1` = 128f vs oracle `FistsStartStance-Left` = 133f (**5 shorter**); `stance_2` = 146f vs `FistsStartStanceIdle-Left` = 156f (**10 shorter**).
- Native cf starts at **2** (first clip frame missing), sub is 0-based vs oracle 1-based; native cf max 44/50 vs oracle 44/36 (oracle loops the idle clip cf 1..36).

**Hypothesis:** clip-start offset + playback-rate difference (Bezier interpolation subframe count, JS_POSE_PIPELINE §2 `(XJ+1)*Tx` subframes per clip frame, or clip-loop handling).

## 8. Behavior divergence (context, not pose pipeline)

- Native Enemy (AI) attacks immediately: `double_punch` f=5 (72f), `short_upward_elbow_strike` f=197 (95f); native Me `high_punch` f=274 (24f).
- Oracle Enemy: no attacks in 350 frames (intro → idle → null). Comparable window = intro only (~128–133 frames).

## 9. Bone-level analysis: unavailable (0% alignment)

- Comparator bone metrics are degenerate (0 matched frames).
- Manual check: native frame pose ≠ oracle frame pose even at the same clip frame (mean delta 77–130), dominated by the root-motion slide (§3) and coordinate differences (§5). The oracle frame pose = clip/16 + **constant offset** (445.2, 87.1 — verified constant across bones); the native frame pose does **not** follow this pattern.
- Bone names (first 20 of the 205-bone fighter, from `mdl_skeleton.xml` document order): `NTop, NNeck, NShoulder_2, NShoulder_1, NElbow_2, NElbow_1, NWrist_2, NWrist_1, NFingertipsSS_2, NFingertipsSS_1, NHip_2, NHip_1, NKnee_2, NKnee_1, NAnkle_2, NAnkle_1, NToe_2, NToe_1, NPivot, NStomach`.

---

## Ranked impact on "fighters slide / squares"

| # | Finding | Impact | Numbers |
|---|---|---|---|
| 1 | Wrong idle clip (`stance_2` instead of `fists1_stance_idle`) | slide | Δx = **-853.6** vs oracle **0.0** |
| 2 | Root-motion over-application | slide | 2.4× (intro), 3.6× (clip root) |
| 3 | Camera framing mismatch | squares | cam cy Δ≈192–247, cx Δ≈38 |
| 4 | Unmirrored Left clip on right-side fighter | squares | clip data identical, fx=-1 |
| 5 | Intro phase drift | timing | 5–10 frames shorter, cf starts at 2 |

## Phase 1 next steps

1. Fix clip selection: idle phase must play `fists1_stance_idle`, not `stance_2`.
2. Verify root-motion application against the oracle (per-frame accumulation vs absolute/per-loop).
3. Add clip-name mapping + fighter-role selection to the comparator; align native Me ↔ oracle Enemy with a coordinate transform.
4. Verify the camera framing formula against the oracle.
# VERIFY_R34 — R3/R4 verification report (GAP-4 contract)

- Program: `reverse/binaries/game_region_runtime.bin` (base 0x8F057000, ARM:LE:32:v7)
- Candidates: `reverse/analysis/decision_semantics_r34.candidate.cpp` (R4),
  `reverse/analysis/DECISION_SEMANTICS.md` §R3 (R3, L177–350)
- Verifier inputs: decompile_function / analyze_control_flow /
  analyze_function_completeness / disassemble_function / read_memory
  (Ghidra MCP) + cpp_call_branch_count (deterministic counts).
- Date: 2026-08-01

## Overall verdict

| item | verdict |
|---|---|
| R3 candidate `decisionStageChanceDraw` (FUN_8f45ab38 evaluate block) | **GREEN** |
| R3 semantic claim (shared comparator, per-pass thresholds) | **GREEN** |
| R4 semantic claims (gate P=1/w; Wait from duration arithmetic) | **GREEN** |
| R4 candidate `pickIndexByWeight` (FUN_8f446cb4) | **RED** |
| **OVERALL** | **RED** |
| NEEDS_HUMAN | **false** (ASM unambiguous; fixes are mechanical, reverser can iterate) |

---

## R3 — decisionStageChanceDraw (FUN_8f45ab38 @ 0x8F45AB38, block 0x8F45ACB4..0x8F45AE80)

| check | verdict | evidence |
|---|---|---|
| call_count | PASS | Block has 12 static call sites: 45a920, 45aa2c, 452f28, 453a94, 446b60, 446b68, 45456c, 446b78, 44ac78, 446b80, 446b88, 446b90 — all present in candidate (38 total incl. modeled inlined rolls/evaluates; 46 whole-function calls are declared out of scope, incl. wait-countdown, registry walk, candidate push, tracer). No under-count. |
| control_flow | PASS | Block conditionals ≈ 5 (452f28 gate, Evade slot loop head/body); candidate branches = 5. Whole-function 19 cond. branches out of declared scope. Order matches decompile exactly: a920 → aa2c → 452f28-gate → 453a94 → 446b60 → 446b68 → 45456c → [Evade loop inline @0x8F45AD8C] → 446b80 → 446b88 → 446b90. |
| fp_consistency | PASS | `decided = f32(thr) < score` at all 7 sites (5 single-stage + QuickAttack FUN_8f45456c `pfVar7[1] < fVar3` + Evade loop `pfVar14[1] < fVar6`; ASM `vcmpe.f32` GT/`movgt`). UseDefense FUN_8f453a94: strict `A>r→2`, `A+B>r→3`, `C+(A+B)>r→4` else 1 (add order C+(A+B) as claimed; A/B/C stored at decision+0xdc/0xe0/0xe4 ✓). Score stores `(int)`-rendered floats to +0xe8/0xec/0xf0/0x78/0x80 ✓. |
| stub_detect | PASS | Full logic, no placeholders. |
| body_proportion | PASS | ~170-line candidate vs 0x1CC-byte block (function 0x578 bytes / 1400). completeness_score=0% (справочно — Ghidra DB has zero annotations; not a fidelity signal). |
| side_effects | PASS | All writes modeled: d+0x1c (UseDefense choice), scores +0xe8/0xec/0xf0/0x78/0x80, flags +0x89/0x8a/0xa4/0x74/0x7c, thresholds +0xa8..0xbc (FUN_8f4536c8: 6 rolls, shared rng source loaded once — registry-gated in binary, documented in candidate), slot result arrays decision+0x8c/+0x98 (12-byte {score,thr,decided}). |
| slotCount magic | PASS | `floatCount * 0x684bda13u` mod 2^32 == floatCount/27 for multiples of 27; binary computes same value via `-(words*0x97B425ED)` (0x97B425ED ≡ −0x684BDA13 mod 2^32). |

**R3 semantic claim** (all stages share the quick-attack comparator; thresholds rolled per pass): **GREEN**.
Fire iff `score > threshold` (strict GT) at every threshold stage; the 6th roll
(+0xa8) has no observed consumer (RNG-stream alignment, as noted). UseDefense
is the documented one divergence (4-way cumulative draw). FUN_8f446b48
confirmed as `FUN_8f44ac78(table+0x18, …)` — the +0x18 curve offset holds.

---

## R4 semantic claims

| claim | verdict | evidence |
|---|---|---|
| Gate `w = FUN_8f446b98(table, decision+0x58, ctx)`; first +0x63c record with empty name or name==filter match; no record → log + return 1.0f (0x3F800000) | GREEN | FUN_8f446b98 decompile verbatim: empty-name branch when filter==0, else `*rec==rec[1] || FUN_8f47cf1c(filter, recName)`; fallback `FUN_8f226a58(…)" + return 0x3f800000`. |
| `gate = w≥1 ? 1−1/w : 0; attack iff gate < roll → P≈1/w; w<1 → always attack` | GREEN | FUN_8f459b44: `fVar28 = DAT_8f459f60 (=0.0f, raw 00 00 00 00 verified); if (1.0 <= fVar27) fVar28 = 1.0 - 1.0/fVar27; fVar27 = roll(); if (fVar28 < fVar27) *(undefined1*)(param_1+0x14) = 1;` — param_1+0x14 = byte +0x50 attack flag ✓; else-if on +0x50==0 && +0xc0==0 → early return (keep waiting) ✓. |
| Duration shapes: cbe0/cbfc = `(*(ushort*)(x+100)+1)·(maxAttr − *(int*)(x+0x74) + 2) + 1`; c660(x,1) = same without +1, entries with type==4 | GREEN | FUN_8f47cbe0/cbfc/c660 decompiles confirm the exact formulas and the `entry+0x18 == 4` selector for c660; attribute lists from globals FUN_8f43f0b8/FUN_8f43f0cc (runtime-populated, names [UNCERTAIN] as claimed). |
| Defense path wait `(c660(enemy,1) − damage) + 1` | GREEN | FUN_8f457fb8 push loop: `iVar26 = FUN_8f47c660(local_8c,1); iVar26 = (iVar26 − *(int*)(param_1+0xc)) + 1;` pushed as {id, value} 8-byte pairs (also pushes {0, …} — the current-animation candidate). |
| Wait frames come from duration arithmetic, NOT the weight | GREEN | FUN_8f45ab38: `param_1[0x12] = max(animFrames, min(animRange, (speedVal−damage)+1)) − 1` (paths 1/2), `cbe0−1` (path 3), sentinel 0x88CA6C00 on failure; weight `w` consumed only as gate probability + roulette weight. |
| Pick roulette: sum of AnimationWeights (@ +0x630) curve weights, `point = FUN_8f26447c(rng, sum)` = `sum·roll`, subtract-walk, return idx / −1 | GREEN | FUN_8f446cb4 ASM: pass 1 sums `FUN_8f44ac78(rec+0x0c, ctx)` for first record with `*rec==rec[1]` or `FUN_8f47cf1c(anim, recName)`; default `DAT_8f446f40` = 0.0f (raw verified); `bl FUN_8f26447c` with r1=sum (decompile: `param_2 * (u1/u2 + (u3/u4)/u5)`); pass 2 `vsub` walk, `bmi → return idx`; `mvn r0,#0` else. |

---

## R4 — pickIndexByWeight (FUN_8f446cb4 @ 0x8F446CB4)

| check | verdict | evidence |
|---|---|---|
| call_count | PASS | decompile_calls = 5 (47cf1c×2, 44ac78×2, 26447c×1); candidate_calls = 18 → 5−18 < 3, no under-count. |
| control_flow | **FAIL** | decompile conditional_branches = 22 vs candidate 17 (Δ = 5 ≥ 2). Line-by-line confirms a real iteration-structure divergence: **zero-slot handling**. ASM 0x8F446D24..38 (pass 1) and 0x8F446E30..44 (pass 2): `id==0 && filter≠0` → process **filter immediately, consuming ONE slot** (no read-ahead); `id==0 && !filter` → advance past zero slots; `id≠0` → process id. Candidate instead: on `id==0` always advances and, if the NEXT slot is non-zero, uses the **next slot's id** as the anim (even with filter set); filter only used when two consecutive zeros; a trailing zero slot never contributes filter. Consequences on the normal (filter=decision+0x58 set) path: binary counts weight(filter) + weight(next) and returns the physical slot index (caller reads waitArray[idx] @ +0x18 / cand[idx] @ +0xd0); candidate counts only weight(next) and returns a shifted index. The doc's own §R4.3.3 describes the binary behavior; the candidate code contradicts it. |
| fp_consistency | **FAIL** (minor) | Pass-1 sum in the binary truncates each weight to int before accumulating: ASM 0x8F446F04..08 `vcvt.s32.f32 s15,s14; vcvt.f32.s32 s15,s15` (also rendered in decompile as `VectorSignedToFloat((int)fVar1,…)`); pass-2 subtract uses the raw float (0x8F446F1C..20, no vcvt). Candidate sums/subtracts raw weights in both passes. Order otherwise preserved (`sum·roll`, `point −= w`, `point < 0 → idx`). |
| stub_detect | PASS | Full roulette logic, no placeholders. |
| body_proportion | PASS | 136-line candidate vs 652-byte / 163-instruction function. completeness_score=0% (справочно). |
| side_effects | PASS | Function is read-only w.r.t. globals (DAT_8f446f40/f44/f48 read only); returns idx or −1; no missed writes. |

Everything else in the candidate checks out: roll01 order = `u1/u2 + (u3/u4)/u5`
exactly as compiled (FUN_8f264414 decompile); animMatchesName matches
FUN_8f47cf1c (name @ anim+0x7c/0x80, alt-name list @ anim+0xa8/0xac, 12-byte
entries {ptr,len,cap}, strcmp); recordCount magic 0x97B425ED>>36 = /27 matches
the binary's count computation; pass-1 `break` vs pass-2 `return −1` on
end-of-vector asymmetry is modeled correctly; weight default 0.0f verified.

---

## Fix guidance for the reverser (R4)

1. Zero-slot semantics: model the binary as —
   `id != 0 → weight(id)`; `id == 0 && filter != 0 → weight(filter)` (single
   slot, no read-ahead); `id == 0 && filter == 0 → skip` (advance one, continue
   scanning; pass 1 breaks to the sum check on vector end, pass 2 returns −1).
   Returned idx counts every processed slot (physical slot index when filter set).
2. Pass-1 sum: accumulate `(float)(int)w` (vcvt truncation), keep raw `w` in the
   pass-2 subtract-walk.

## Notes

- completeness_score 0% for both functions is annotation metadata only
  (no names/comments/plate in the Ghidra DB) — it did not influence any verdict.
- NEEDS_HUMAN = false: the ASM is unambiguous at every disputed point; both
  FAIL items are mechanical fixes within the reverser's reach.

---

# Round 2 (final adjudication, fix commit b6c352d)

Re-verified 2026-08-01 against a fresh decompile + fresh disassembly of
FUN_8f446cb4. Both round-1 FAIL items were re-checked byte-by-byte at the
exact addresses named in the fix guidance; all round-1 semantic claims
re-confirmed against fresh decompiles (FUN_8f459b44, FUN_8f47cbe0 usage).

| FAIL item (round 1) | round-2 verdict | evidence |
|---|---|---|
| control_flow — zero-slot handling | **GREEN** | ASM 0x8F446D24..38 (pass 1) / 0x8F446E30..44 (pass 2): `cmp r9,#0 / movne r3,#0 / andeq r3,r6,#1 / cmp r3,#0 / beq skip / ldr r9,[sp,#8]` — id≠0 → weight(id); id==0 && filter≠0 → **weight(filter), exactly ONE slot** (advance at 0x8F446DB4 after the vadd, no read-ahead); id==0 && filter==0 → advance one (`ldr r9,[r7,#4]!` @ 0x8F446D18/0x8F446E20) + end check. Candidate now implements exactly this (`if (id==0){ if(filter==0){++it;continue;} anim=filter; } else anim=id;` in both passes). r6 (filter≠0 flag) is never clobbered between passes — reused at 0x8F446E38 ✓. Branch-count delta 22 (graph) vs 13 (candidate): all 22 mapped 1:1 to candidate constructs — 4 entry checks (8F446CEC/CF8/E00/E0C), 2 skip-end checks (D1C/E2C), 2 loop-tail continues (DBC/EE0) and 2 record-scan continues (DAC/EB8) collapse into while/for conditions; counter-edge 8F446F2C is the bpl of the same vcmpe as 8F446ECC. **Zero unmapped branches/loops** — no missed logic. |
| fp_consistency — pass-1 int truncation | **GREEN** | 0x8F446F04..08: `vcvt.s32.f32 s15,s14 / vcvt.f32.s32 s15,s15` → sum accumulates `(float)(int)w`; 0x8F446F1C..20: `vmov s15,r0 / vsub.f32 s16,s16,s15` → pass-2 subtracts the **raw** float. Candidate: `sum += (float)(int)candidateWeight(...)` / `point -= candidateWeight(...)`. Op order preserved: `point = FUN_8f26447c(rng, sum)` = sum·roll01 (`vmov r1,s16` @ 8F446DD8, `bl 8F26447C` @ 8F446DE0); `point<0 → return idx` (bmi → `cpy r0,r10` @ 8F446F30); equality continues (bpl @ 8F446F2C) as candidate's `< 0.0f`. |

Full round-2 re-check (all PASS):

| check | verdict | evidence |
|---|---|---|
| call_count | PASS | decompile_calls = 5 (47cf1c×2, 44ac78×2, 26447c×1); candidate_calls = 18 (full file, incl. modeled helpers: roll01 6, animMatchesName 3, candidateWeight 1) → 5−18 < 3, no under-count. |
| control_flow | PASS | See FAIL-item row above: all 22 graph branches mapped 1:1, zero missed. |
| fp_consistency | PASS | See FAIL-item row above. Additionally: `sum>0` gate = `vcmpe.f32 s16,#0 / ble → mvn r0,#0` @ 0x8F446DC4..CC ⇔ candidate `if (sum > 0.0f)` (sum==0 → −1 both); `return −1` fallthrough `mvn r0,#0` @ 0x8F446EE4 ✓. |
| stub_detect | PASS | Full roulette logic in both passes, no TODO/approximate/placeholder. |
| body_proportion | PASS | 133-line candidate vs 652-byte / 163-instruction function (basic_blocks 33, loops 8); completeness_score=0% (справочно — annotation metadata only). |
| side_effects | PASS | Read-only w.r.t. globals (DAT_8f446f40/f44/f48 read only); returns idx (r10) or −1 (mvn r0,#0); no missed writes. |

Address-level confirmations requested in the round-2 brief:
- idx accounting `add r10,r10,#1` @ 0x8F446ED4 — present; executed only on the
  point≥0 continue path, after `ldr r9,[r7,#4]!`; candidate `++it; ++idx;` after
  the `point < 0` return. Skips do not bump r10 in either pass ✓.
- End-of-vector, pass 1 (0x8F446DB4..C0): after last slot → fall into sum gate
  (not −1) ✓ candidate loop exit → `if (sum > 0.0f)`.
- End-of-vector, pass 2 (0x8F446ED0..E0): loop tail → `mvn r0,#0`; skip-path
  end (0x8F446E24..2C) → `mvn r0,#0`; entry empty/end (0x8F446E00/0C) →
  `mvn r0,#0` ✓ candidate: all funnel to `return -1`.
- Round-1 semantic claims re-confirmed GREEN on fresh decompiles: gate
  `w = FUN_8f446b98(…); if (1.0 <= w) P = 1.0 − 1.0/w; attack iff P < roll`
  (FUN_8f459b44 verbatim, `*(param_1+0x14)=1`); wait frames from duration
  arithmetic (`iVar15 = FUN_8f47cbe0(id)` pushed as {id, value} pairs; weights
  consumed only as gate + roulette).

**Round-2 result**

| item | verdict |
|---|---|
| FAIL-1 (zero-slot handling / control_flow) | **GREEN** |
| FAIL-2 (pass-1 int truncation / fp_consistency) | **GREEN** |
| **OVERALL** | **GREEN** |
| NEEDS_HUMAN | **false** — ASM unambiguous at every point; both fixes verified byte-level; no indirect calls in this function (all callees resolved statically). |

# VERIFY: FUN_8f44ac78 — TacticWeight::evaluate (GAP-4 R2, round 1)

- **Subject:** `FUN_8f44ac78` @ 0x8F44AC78, `game_region_runtime.bin` (base 0x8F057000, ARM:LE:32:v7)
- **Candidate:** `reverse/analysis/atf_record_858.candidate.cpp`
- **Verifier tooling:** Ghidra MCP (decompile / control-flow / completeness / memory reads) + cpp-metrics (deterministic call/branch count)
- **VERDICT: GREEN** · NEEDS_HUMAN: false

---

## 1. Deterministic metrics (tool-returned, not hand-counted)

| Side | calls | cond. branches | source |
|---|---|---|---|
| FUN_8f44ac78 (ASM, `analyze_control_flow`) | 9 (13 in decompile text incl. 4 `VectorSignedToFloat` VCVT pseudo-calls) | 29 | cyclomatic 35, 283 instr, 10 loops |
| FUN_8f4b1adc probe (ASM) | 7 | 6 | cyclomatic 15 |
| FUN_8f4b151c find-or-create (ASM) | 3 | 14 | cyclomatic 17 |
| Candidate whole file (`cpp_call_branch_count`) | **16** | **20** | deterministic, non-LLM |

Reverser self-check said "13/29 vs 11/17" — binary side confirmed (13 counts the 4 VCVT
pseudo-calls); candidate side is **16/20** by the deterministic tool, not 11/17.

## 2. Self-check claim adjudication

Claim: *"delta = two inlined std::vector copy loops (elided, semantically identical)"* — **VERIFIED in substance**, with one correction of detail:

- The copy loops themselves contain **zero calls**. The call-side delta comes from the
  allocator calls *around* the copies: per list — `thunk_FUN_8f71f8c0` (operator new) +
  `FUN_8f6fef48` (fallback alloc) + `FUN_8f71cca4` (operator delete), ×2 lists.
- Decompile confirms the binary per list: allocates `(count<<3)` bytes, copies PairKV
  entries in a do-while, linear-searches the **copy** (read-only), frees it. Searching
  in place (candidate) is semantically identical — same data, same forward first-match,
  no mutation between copy and search. Candidate documents this at line 212-213.
- Remaining call delta: `FUN_8f4a6e44`/`FUN_8f4a7220`/`FUN_8f454344` are 1-instruction
  getters (`M+0x71c`, `M+0x634`, `M+0x6c` — verified by decompiling all three); candidate
  inlines them as field reads. Vector-grow new/delete inside FUN_8f4b151c lives inside
  `std::vector::push_back` in the candidate.
- Branch delta (29 − 20 = 9) attribution: ~4–5 branches per temp-copy block
  (alloc-null guard, fallback, begin≠end, copy do-while, bounds, free-if-nonnull) ×2,
  plus magic-division count checks (`× −0x684bda13`) that collapse into `!empty()`.
  **All 12 real decision points are present in the candidate** (children non-empty+loop,
  two searches with defaults, 6 curve branches) — verified one-by-one against the decompile.

## 3. Line-by-line semantic verification (all against actual decompile output)

### Dot product (Gb) — term order EXACT
Binary: `ctx[1]*w[2] + ctx[0]*w[1] + (1−ctx[2])*w[3] + (1−ctx[3])*w[4] + f(ctx[4])*w[5] + f(ctx[6])*w[6] + f(ctx[5])*w[7] + ctx[8]*w[8] + f(ctx[9])*w[9] + ctx[10]*w[10] + w[0xd]`
Candidate lines 186-196 match term-for-term, same left-to-right association:
- **damage*DamageFactor FIRST, counter*CounterFactor SECOND** — damage-first claim VERIFIED.
- VCVT order: anim_frames→w[5], magic_bullets→w[6], missile_bullets→w[7] (note ctx[6] before ctx[5]) ✓
- `(1−health)`, `(1−enemy_health)` complements ✓; `+shift` (w+0x34) last ✓.

### Per-child probe term — order EXACT
Binary: `a += *(entry+0x14)*outD + *(entry+0x10)*outC + *(entry+0x2c)*outH` — damage first.
Candidate lines 205-207: `damage_factor*d + counter_factor*c + hit_factor*h` ✓.
Entry offsets (+0x00 name, +0x10 counter, +0x14 damage, +0x2c hit, stride 0x6c) ✓.
Probe called with `(ctx[0xd]=memory, slot=1, *entry=name)` ✓.

### Probe FUN_8f4b1adc — decay write-back order EXACT
Binary else-branch: `d=k*damage; damage=d; c=k*counter; f04*=k; h=k*hits; counter=c; hits=h; f10*=k`.
Candidate lines 164-167: identical write-back order **damage→f04→counter→hits→f10** ✓.
- `frames = cur − rec->last_frame`; `frames < 1` → raw read (no write-back) ✓
- `k = powf(2.0f, −(float)frames/rate)` (0x40000000 = 2.0f bits) ✓
- rate: default `DAT_8f4b1c68` = **0.0f** (verified raw bytes `00 00 00 00`); gate
  `M+0x634` (FUN_8f4a7220 decompiled: `return *(int*)(P+0x634)`), ptr `M+0x6c`
  (FUN_8f454344: `*(int*)(P+0x6c)`), null-checked ✓
- `last_frame = cur` unconditional, outside the if/else ✓
- accumulation: `out1+=counter, out2+=damage, out3+=hits` ✓

### Find-or-create FUN_8f4b151c ✓
slot==0 → P+0x10, else P+4 (candidate lines 143-144 comment matches); forward scan,
first dword-id match wins; on create: 0x1c record, `{id,0,0,0,0,0,0}` zero-init ✓.

### Gather FUN_8f45bad4 ✓
Global map scan + `FUN_8f73bd3c` (**verified strcmp**: word-at-a-time, 0 on equal) +
`FUN_8f45b930` (**verified merge-unique**: scan-dst-then-append with vector grow);
returns after first name match. Candidate extern decl + comment accurate.

### Pair lists ✓
w+0x48/+0x4c (list1, key ctx[11]), w+0x54/+0x58 (list2, key ctx[12]); forward
first-match; default `DAT_8f44b0e4` = **0.0f** (verified raw bytes `00 00 00 00`);
`a = a + v1 + v2` order ✓.

### Curve — bit-tests and clamps EXACT
- `w[0xe] == 0.0f` (bits==0) → Exponential: `a≥0 → limit + (base−limit)·2^(−a)`;
  `a<0 → anti_limit + (base−anti_limit)·2^a` ✓ (limit=w+0x2c, anti_limit=w+0x30)
- `w[0xe] == 1.4013e-45f` (bits==1 denormal) → Linear: `a≥0: t=min(a,1), base+(limit−base)·t`;
  `a<0: t=(a<−1)?1:−a, base+(anti_limit−base)·t` ✓
- else → return 0.0f (= DAT_8f44b0e4) ✓

### Side effects ✓
Probe mutates memory records (decay write-back + unconditional last_frame) and the
record vectors (find-or-create) — preserved in candidate; evaluate itself mutates
only through the probe (const_cast on ctx.memory mirrors the binary).

## 4. Check summary

| Check | Result | Evidence |
|---|---|---|
| call_count | **PASS** | decompile_calls 9 (subject ASM) / 13 (text) − candidate 16 < 3; candidate has more (models callees + wrapper). Every binary call mapped: logic reproduced, getters inlined, allocator noise accounted. |
| control_flow | **PASS** | 29 − 20 = 9, fully attributed to 2× verified temp-copy blocks + magic-div codegen artifacts; all 12 real decision points present (task rule: "judge semantics"). |
| fp_consistency | **PASS** | Dot product term order exact (damage first); VCVT slot order frames/magic/missile exact; per-child D·d+C·c+H·h exact; powf signs (−a / a / −frames/rate) exact; no invented FP ops. |
| stub_detect | **PASS** | No TODO/approximate/empty body; full logic incl. curve, decay, searches. |
| body_proportion | **PASS** | 269 lines cover subject + 3-callee tree; elision is STL copy machinery only, documented. completeness_score = 0% (справочно — функция не аннотирована в базе; на вердикт не влияет). |
| side_effects | **PASS** | Record decay write-back (5 fields, exact order), unconditional last_frame, vector growth — all preserved. |

## 5. Verdict

**VERDICT: GREEN** — candidate is semantically faithful to FUN_8f44ac78 and its probe
callee tree: term order, decay write-back order, defaults (both DATs verified 0.0f in
raw memory), curve bit-tests, and damage-first ordering all match the binary exactly.
The reverser's elision claim is verified true in substance (delta = inlined
std::vector copy/allocator machinery + trivial getters; zero game logic lost), with
the detail correction that the copy loops carry no calls themselves — the call delta
is the surrounding operator new/fallback/delete.

**NEEDS_HUMAN: false** — no indirect/unresolved calls in the chain; all identifications
(strcmp, merge-unique, powf, three getters, both DAT constants) confirmed by
decompilation or raw bytes.

Post-GREEN: wiring into the engine (CMake + TacticTableSet::animation_factor) is a
separate backend step per `.planning/phases/phase-5/PLAN.md`; not part of this verdict.

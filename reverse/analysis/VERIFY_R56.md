# VERIFY_R56 — R5/R6/v7 candidate + negative-claim verification (GAP-4 R5/R6/v7 contract)

Verifier: @re-verifier (Ghidra MCP + cpp-metrics). Program: `game_region_runtime.bin`
(base `0x8F057000`, ARM:LE:32:v7). Subjects: 5 candidates in
`reverse/analysis/memory_indexing_r56.candidate.cpp`, 3 load-bearing negative claims.
Method: decompile + line-by-line comparison, raw-ASM cross-checks where the decompiler
rendering is suspect, deterministic call/branch counts via cpp-metrics, xref audits for
negative claims. Completeness scores are informational only (0% — no annotations applied;
does not affect verdicts).

## Overall verdict

**RED** — one load-bearing negative claim (c) is contradicted by the dump's own memory.
All 5 function candidates are logic-exact. **NEEDS_HUMAN: true** (unresolvable statically:
populated map, no statically-discoverable writer).

| Item | Verdict |
|---|---|
| 1. `atf_record_find_or_create` (FUN_8f4b151c) | GREEN |
| 2. `atf_memory_feed_strike` + `atf_memory_feed_counter` (FUN_8f4b173c / FUN_8f4b1830) | GREEN |
| 3. `animplayer_select_intervals` (FUN_8f47b528) | GREEN |
| 4. `tactic_score_quick_attacks` (FUN_8f45456c) | GREEN |
| 5. `atf_gather_record_ids` (FUN_8f45bad4) | GREEN (logic) — see claim (c) caveat |
| (a) `<Memory>` parses only Strikes/RoundFactor, defaults 10.0f | GREEN |
| (b) probe D(+0x08)/H(+0x14) never fed; only C(+0x0c) live | GREEN |
| (c) v=7 map @ 0x8F86F258 empty at runtime | **RED** — dump shows populated map (983 entries) |

---

## 1. atf_record_find_or_create — FUN_8f4b151c @ 0x8F4B151C — GREEN

Decompile: slot select (`param_2==0` → +0x10 else +0x4), linear scan of 7-dword records
(step `piVar3+7`), count==0 guard (`iVar2*0x49249249==0`), end==cap realloc, fresh-record
zero-init, return = old end. Candidate matches line-by-line.

- **Growth policy — verified in ASM, doubling confirmed** (decompiler's `iVar2*0x6db6db6e`
  rendering checked against disasm): `8f4b1538-1554` computes dwords×0x49249249 (mod 2^32),
  negated; `8f4b1628 movs r9,r6,lsl#1` = 2×dwords×0x49249249 ≡ 2×count records (d = 7×count
  ⇒ d×0x6DB6DB6E mod 2^32 = 2×count exactly); `8f4b15f4 mov r9,#1` for count==0; `8f4b15f8`
  ×7 dwords, `1600` ×4 bytes = 28×newcap. **`newcap = count ? 2*count : 1` records — confirmed.**
- **Initial capacity 20 records — confirmed in ASM** (FUN_8f4b0dac): `8f4b0df0 add r2,r0,#0x230`
  → slot1 cap = alloc+0x230; `8f4b0e30 add r3,r6,#0x230` → slot0 cap = alloc+0x230
  (decompiler mis-rendered the second as `+0x8c`; disasm shows 0x230 — doc claim correct).
- Zero-init: binary writes id, then +0x18, then +0x04..+0x14; candidate writes id, +0x04..+0x18.
  Same value set, all zeros — no observable difference.
- Return = appended record (`piVar3 + iVar2`, iVar2 = 7×count dwords = old end) ✓.
- **Fidelity note (inside the pre-declared elided machinery):** the realloc copy loop
  (`8f4b167c-16bc`, `8f4b16d0-16f4`) copies dwords [0..5] and writes [6]=0 for every moved
  record — i.e. the binary zeroes `last_frame` of all records on every growth. The candidate
  (std::vector semantics) preserves it. Both the doc's "realloc copy loop elided" claim and
  the ATF_RECORD_858 precedent cover this; observable only post-realloc (rare) and only via
  decay timing. Recorded here so the doc doesn't claim bit-exact vector semantics.

| metric | decompile | candidate | adjudication |
|---|---|---|---|
| calls | 3 | 9 | PASS — candidate has *more* (std::vector API); not a shortfall |
| branches | 14 | 4 | PASS (adjudicated) — delta 10 is 100% realloc machinery: alloc-null fallback `8f4b1610/14`, newcap==0 `8f4b1628/34`, count==0→1 `8f4b1620/24`, copy-loop entry/continue `8f4b1638/3c`,`16b0/bc`, free-old `16f8/fc`, end==cap `15a0/a4`. All functional branches (slot select, count guard, scan entry, per-record compare, loop continue, full-check, newcap ternary) present in candidate. |

fp_consistency: PASS — no fp ops in binary, none invented. stub_detect: PASS.
side_effects: PASS — all 7 record dwords written; capacity/end updates model vector growth.
body_proportion: PASS — 27 candidate lines vs 136 instructions; completeness 0% (informational).

## 2. atf_memory_feed_strike / atf_memory_feed_counter — FUN_8f4b173c / FUN_8f4b1830 — GREEN

Decompiles match the candidates **exactly** (term order, write-back order, defaults):

- Accessors verified: `FUN_8f4a6e44 = *(p+0x71c)` (cur), `FUN_8f4a7220 = *(p+0x634)` (AI gate),
  `FUN_8f454344 = *(p+0x6c)` (tactic ptr). Default `DAT_8f4b182c`/`DAT_8f4b1910` = 0.0f
  (read_memory: 0x00000000 ✓).
- Decay: `powf(2.0f, -frames/rate)` via FUN_8f72ed40(0x40000000, …) ✓; `frames = cur - rec[+0x18]`,
  `frames<1` fast path reads locals ✓.
- Write-back order FUN_8f4b173c: `+0x08, +0x0c, local +0x04, +0x14, local +0x10` — candidate
  `damage, counter, sd, hits, sc` ✓ exact. FUN_8f4b1830: `+0x08, +0x04, +0x14, local +0x0c,
  +0x10` — candidate exact ✓.
- `last_frame = cur` unconditional; adds on the decayed base (`amount + sd`, `sc + 1.0f`,
  `c + 1.0f`) ✓.
- **Feed sites verified:** FUN_8f4aa998 entry `*(+0x71c)+=1` (hits-taken counter) and tail
  `FUN_8f4b173c(victim_root+0x638, 1, anim_id, damage)` / `FUN_8f4b173c(attacker_root+0x638, 0,
  anim_id, damage)` / `FUN_8f4b1c6c(...)` with anim_id = `victim+0x19c` = FUN_8f460278(attacker
  +0x630), damage = `victim+0x1a4` ✓ mirrored slots confirmed. FUN_8f4a5478: "Uninterrupt"
  (len 0xb, memcmp site 0x8f4a54bc) → `FUN_8f4b1830(root+0x638, 1, anim)` and
  `FUN_8f4b1830(root+0x638, 0, anim)` ✓.

| metric | decompile | candidate | adjudication |
|---|---|---|---|
| calls | 5 (3 accessors + find-or-create + powf) | 3 (helper inlines accessors + find-or-create + pow) | PASS — delta 2 < 3, accessor inlining pre-declared & verified |
| branches | 1 (`frames<1`) | 1 | PASS |

fp_consistency: PASS (above). stub_detect: PASS. side_effects: PASS. body: PASS — 38+25 lines
vs 60+56 instr; completeness 0% (informational).

## 3. animplayer_select_intervals — FUN_8f47b528 @ 0x8F47B528 — GREEN

The decompiler's bool rendering of the window block is indeed mangled
(`SBORROW4`/`iVar13 < 0 != bVar14`); verified against raw ASM as the doc directs:

- Entry clears: `param_3[1]=*param_3; param_4[1]=*param_4` (first two statements) ✓.
- Iteration source: `*(*(move+0x94)+0x28)` / `+0x2c` ✓.
- Clamps at `0x8f47b57c-0x8f47b5a4`: `s = max(start(+0x04), move+0x74)` (`cmp r3,r0; cpycc r3,r0`),
  `e = min(end(+0x08), (u8)move+0x78)` (`cmp r2,r1; cpyge r2,r1`) ✓ candidate `if (s<base) s=base; if (w<=e) e=w` exact.
- Dispatch: `cmp r3,r8; cmple r8,r2; bgt 0x8f47b62c` — active iff `s <= frame` (no upper bound) ✓.
- Expiring: `0x8f47b62c cmp r4,r2; bne` — `frame-1 == e` (r4 = frame−1 precomputed at
  `0x8f47b574`) ✓ candidate `frame-1 == (uint32_t)e` exact.
- Filter (param_5 = std::map header): null check `0x8f47b5a8 cmp r10,#0` + header->parent==0
  `0x8f47b5b4-5bc` → not filtered; walk `0x8f47b5c0-5f4` (active) and `0x8f47b64c-680`
  (expiring, cmp operands swapped — identical lower_bound semantics): key≥type → result=node,
  right(+0x08); key<type → left(+0x0c); skip iff `result != header && result->key <= type`
  (`ble/bge` to loop-continue). Candidate's single `interval_type_filtered` implements the same
  walk for both paths ✓. (Candidate labels +0x0c "prev [UNCERTAIN NAME]" — it is the left child
  in the walk; naming only.)
- Pushes: active → param_3 (`0x8f47b5f8-614`), expiring → param_4 (`0x8f47b684-6ac`), both with
  inline realloc at `0x8f47b6b0`/`0x8f47b778` — elided as push_back (pre-declared).

| metric | decompile | candidate | adjudication |
|---|---|---|---|
| calls | 8 | 7 | PASS — delta 1 < 3 (memcpy/free/alloc thunks) |
| branches | 31 | 10 | PASS (adjudicated) — delta 21 = two push-back realloc paths (≈8 each: count==0, newcap==0, alloc-null, memcpy-guard, free-old, size checks) + filter-walk duplication; every *functional* branch (clears, loop, clamps, dispatch, frame−1==e, filter null/header/walk/result, pushes) is present in the candidate |

fp_consistency: PASS — no fp ops. stub_detect: PASS. side_effects: PASS — both vectors cleared
then refilled; rec pointer stored, not copied. body: PASS — 52 lines vs 202 instr; completeness
0% (informational).

## 4. tactic_score_quick_attacks — FUN_8f45456c @ 0x8F45456C — GREEN

ASM-verified (decompiler reuses one variable for two offsets; disasm resolves it):

- `tactic = st+0x6c` → `FUN_8f446b70` (tactic+0x1f8 vector) → begin/end.
- Loop count: `(end-begin)` dwords × magic ≡ count via low-word ×(1/27 mod 2^32) — verified
  arithmetic: `8f454594-5a4` chain; `8f4545a8 cmp r8,#0` early-out; loop count = entries ✓.
- Entry stride 0x6c: `8f4545b8 add r2,r4,r4,lsl#3` = 9×r4 with `r4 += 0xc` → 108 bytes ✓;
  weight at entry+0xc (`add r0,r0,#0xc`); `bl 0x8f44ac78` (TacticWeight::evaluate) ✓.
- Slot stride 0xc: `ldr r5,[r10,#0x8c]; add r5,r5,r4` ✓ `{score@+0, threshold@+4, decided@+8}`.
- `str r0,[r5,#0x0]` score; `vldr s14,[r5,#0x4]` threshold; `vcmpe.f32 s15,s14; movgt r3,#1`
  → `decided = score > threshold = threshold < score`; `strb r3,[r5,#0x8]` ✓.
- Threshold written only by the resize path (FUN_8f45a920/aa2c — separate, not in this
  function); this loop never touches it → persists ✓ doc §4.2 exact.

| metric | decompile | candidate | adjudication |
|---|---|---|---|
| calls | 2 (FUN_8f446b70, FUN_8f44ac78) | 3 (+ end accessor, inlined in binary) | PASS — not a shortfall |
| branches | 2 (count guard, loop end) | 1 (for-loop) | PASS — delta 1 < 2; `for` ≡ guard+end |

fp_consistency: PASS — single fp compare, order preserved. stub_detect: PASS.
side_effects: PASS — slot.score/decided only. body: PASS — 14 lines vs 40 instr; completeness
0% (informational).

## 5. atf_gather_record_ids — FUN_8f45bad4 @ 0x8F45BAD4 — GREEN (function logic)

- Map base verified: pool @0x8F45BB54 = 0x413768, add-site pc = 0x8F45BAE4 → **0x8F86F24C**;
  header `{begin@0x8F86F258, end@0x8F86F25C, cap@0x8F86F260}` ✓ doc anchors exact.
- Scan: 4-byte element stride (`ldr r5,[r4,#0x4]!`), element = handle; `FUN_8f490a58` is an
  identity accessor (empty body, r0 preserved) → `ldr r1,[r0,#0]` = element[0] = name cstr;
  `strcmp` FUN_8f73bd3c; on hit `FUN_8f490a5c(handle) = handle+0xc` = ids vector →
  `FUN_8f45b930(out, ids)` merge-unique, returns appended count; miss → 0 ✓ candidate exact.
- merge_unique (FUN_8f45b930) verified: outer scan of src, inner scan of dst, append-if-absent
  (with realloc), return `new_count − old_count` = added ✓.
- **Structural notes (no behavioral impact on the function):** (i) the container is
  `std::vector<4-byte handles>` (stride 4 confirmed in bad4/b750/bb58), not a by-value vector
  of 0x18-byte entries — the candidate's `std::vector<NameIdsEntry>` models by value; (ii) the
  entry object is larger than 0x18 — GC (FUN_8f45b550) frees buffers at +0x00 (name string),
  +0x0c (ids), **+0x18 and +0x24** — two more embedded members the doc/candidate don't model.
  Only +0x00/+0x0c are used by the gather, so function semantics are identical either way;
  a future engine implementation should model `std::vector<Entry*>` with the full 0x30-byte
  entry.

| metric | decompile | candidate | adjudication |
|---|---|---|---|
| calls | 4 | 4 | PASS |
| branches | 4 | 6 (merge inlined) | PASS — candidate has more; no shortfall |

fp_consistency: PASS. stub_detect: PASS. side_effects: PASS — appends to out only, returns
added count. body: PASS — 24 lines vs 32 instr; completeness 0% (informational).
**Caveat:** the candidate's comment "the map stays empty at runtime, so this gather always
returns 0 here" is tied to claim (c), which FAILED (below). The function's logic stands; the
runtime-behavior claim does not.

---

## Negative claims

### (a) `<Memory>` parses ONLY Strikes/RoundFactor; defaults 10.0f — GREEN

Disassembly `0x8f448b90-0x8f448c0c` verified instruction-by-instruction:
`ldr r1,[0x8f449850]` → "Memory" (0x8F797A1C) → child lookup → if elem: "Strikes"
(0x8F797A24, pool 0x8f449854) → attr → `read_float` → `str r0,[r3,#0x0]` @0x8f448be4
(tactic+0x00); "RoundFactor" (0x8F797A2C, pool 0x8f449858) → `read_float` →
`str r0,[r12,#0x4]` @0x8f448c08 (tactic+0x04); the very next key is "UseDefense"
(pool 0x8f44985c @0x8f448c0c). **No third attribute, no depth constant.** Strings verified in
memory: `"Memory\0\0Strikes\0RoundFactor\0UseDe…"` @0x8F797A1C ✓. Defaults: ctor prologue
`0x8f4488c0-88e0`: `movt r2,0x4120` (10.0f) stored to +0x00 and +0x04 ✓ both 10.0f.

### (b) probe D(+0x08)/H(+0x14) never fed in this build; only C(+0x0c) live — GREEN

Complete writer enumeration of the 0x1c-byte record: FUN_8f4b151c (fresh-record zero-init
only), FUN_8f4b173c (+0x04 strike_damage +=, +0x10 strike_count +=), FUN_8f4b1830 (+0x0c
counter +=), FUN_8f4b1914 (decay write-backs only; outputs to params), FUN_8f4a84e8
(round-end in-place ×RoundFactor on all five accumulators: +0x04/+0x08/+0x0c/+0x10/+0x14,
per-slot-vector loops at 0x63c/0x640 and 0x648/0x64c — layout +0x638 {owner, slot1@+0x04,
slot0@+0x10} re-confirmed). **No function ever adds to +0x08 or +0x14.** Probes
FUN_8f4b1adc / FUN_8f4b1914 read them after decay as D/C/H (+0x08/+0x0c/+0x14). Feed sites
are only the two verified in §2. → D and H are decay-only fields; only C is event-fed. Doc
§2.4 table exact.

### (c) v=7 map @ 0x8F86F258 has no runtime writer → empty at runtime — **RED**

- **Static half — CONFIRMED:** full xref audit of 0x8F86F258/0x8F86F25C/0x8F86F260:
  WRITEs only from FUN_8f45d110 (static init, zeroing, @0x8f45d264/268/270/274) and
  FUN_8f45b550 (GC truncation, @0x8f45b6c4). Readers: FUN_8f45bad4, FUN_8f45bb58,
  FUN_8f45b750, FUN_8f45b4e8, FUN_8f45b550. No append site in any resolved reference.
- **Runtime half — CONTRADICTED by the dump itself:** live memory at 0x8F86F258 =
  `{0x87003A10, 0x8700496C, 0x87004A10}` — a canonical populated std::vector header:
  begin ≠ end, 983 four-byte elements (0x8700496C − 0x87003A10 = 0xF5C = 3932 = 983×4),
  capacity 1024 (0x1000 bytes). The dump is a live post-init snapshot: the intern pool at
  0x8F86F24C shows live heap pointers `{0x873282D8, 0x87329080, 0x873292D8}` (loader ran).
  Static init would have written 0,0,0 to the header; it did not stick → **a runtime writer
  populated the map**.
- **Writer hunt (negative):** byte-pattern search for the map's address (58 F2 86 8F) and the
  base (4C F2 86 8F) across the whole dump returns **no matches** — the address is not stored
  in any data table, so the writer must compute it arithmetically (unresolved computed
  addressing, exactly the risk the doc's §9 uncertainty #5 admits, now with positive evidence
  it materialized), or the dump mixes code/data from different states.
- **Consequences:** doc §5.3/§0 statements "FUN_8f45bad4 always returns 0 → probe term 0.0f →
  QA/Evade expansion structurally inert (all-zero)" are **not established for runtime
  behavior** of this build. The static "no direct writer" observation stands and is useful,
  but the runtime-empty conclusion is false on the evidence of the dump's own memory.
- **NEEDS_HUMAN:** resolve via live-debugger trace (watch 0x8F86F258 for writes, e.g.
  Frida/watchpoint on 0x8F86F258 during .atf load and gameplay) or a full-binary search for
  computed-address writers (e.g. searches for the 0x413768 pool constant / instructions
  building 0x8F86F24C from other bases, including Thumb or un-created functions).

---

## Verifier metrics table (my session; doc §10 self-check numbers in parentheses)

| function | decompile calls/branches | candidate calls/branches | delta accounted for |
|---|---|---|---|
| FUN_8f4b151c | 3 / 14 (3/14) | 9 / 4 (9/4) | realloc machinery (enumerated above); doubling verified in ASM |
| FUN_8f4b173c | 5 / 1 (6/2) | 3 / 1 (4/3) | 3 accessors inlined into decay-rate helper; exact write-back order |
| FUN_8f4b1830 | 5 / 1 (–) | 3 / 1 (–) | same |
| FUN_8f47b528 | 8 / 31 (10/30) | 7 / 10 (9/10) | two push-back realloc paths elided; window block ASM-verified |
| FUN_8f45456c | 2 / 2 (2/3) | 3 / 1 (3/1) | end-accessor inlined in binary; for ≡ guard+loop |
| FUN_8f45bad4 | 4 / 4 (4/3) | 4 / 6 (4/6) | merge inlined in candidate; stride-4 container verified |

Self-check deltas adjudicated as claimed (vector-realloc elision + accessor inlining); minor
±1–2 count differences vs the reverser's session are measurement noise, not logic deltas.

## Files

- Candidates: `reverse/analysis/memory_indexing_r56.candidate.cpp` (not modified)
- Analysis doc: `reverse/analysis/MEMORY_INDEXING_R56.md` (not modified; see §"needs human"
  items: §5.3 runtime-empty claim, §9 uncertainty #5, §0 TL;DR row 4 "structurally inert")
- This report: `reverse/analysis/VERIFY_R56.md`

# ATF_RECORD_858 — `.atf` container layout and the AnimationFactors probe (R2)

GAP-4 step B4 / ADR-005 R2. Binary: `reverse/binaries/game_region_runtime.bin`
(base `0x8F057000`, ARM:LE:32:v7). Cross-checked against 40+ real files in
`assets/tactics/*.atf` (all 1577 version-framed files walk cleanly to EOF).

**Headline correction to the old parser view** (`engine/reverse/atf_tactics.hpp`
L21-28, L47): the "858-byte record of u8 animation indices" does **not** exist.
The 858 bytes are the **u8 string-length table of string pool A** (858 pooled
animation names). The engine parser's "6-byte binary prefix" (`flags_a,
flags_b, extra, pad, stride`) is a misread: the first 4 bytes are the **u32
blob size** (`0x0000440A` = 17418 in `knobsticks_knobsticks.atf`), and the
"stride u16 = 858" is the **string count of pool A**, the first field of the
blob. Everything the old parser called "the record" is pool-A metadata.

---

## 1. Container layout (inflated payload)

`.atf` on disk = zlib stream (`78 DA`). Inflated payload = a sequence of
**groups**, repeated to EOF (reader loop `FUN_8f4514D8` / `FUN_8F450F90`;
cursor helpers `FUN_8f21f2b8/3a8/64c/670/5e4`):

```
group (v=0, v=1):  u32 version, cstr weapon_a, cstr weapon_b,
                   u32 blob_size, u8[blob_size] blob
group (v=2, v=7):  u32 version, cstr weapon_a,               (single-weapon form)
                   u32 blob_size, u8[blob_size] blob
```

Observed across all 1577 files (6082 groups): v=0 ×2985, v=1 ×2985, v=2 ×56,
v=7 ×56, no other versions. Pair files (`a_b.atf`) typically hold 4 groups —
both weapon orderings × versions {0,1} (e.g. `crescentknives_knobsticks.atf`:
`(CrescentKnives,Knobsticks)@v1, @v0, (Knobsticks,CrescentKnives)@v1, @v0`).
Single-weapon files (`axes.atf`, `_knobsticks.atf`, `_.atf`) hold v=2 (attack
table) + v=7 (per-animation record ids). Empty weapon name = unarmed.

- **Loader (pairs)** `FUN_8f4514D8` @ `0x8F4514D8`: builds
  `assets/tactics/<a>_<b>.atf` (`FUN_8f450250`; `"assets/tactics/"` @
  `0x8F797F08`, `".atf"` @ `0x8F797F18`), inflates (`FUN_8f2065e4` @
  `0x8F2065E4` — fread 0x1000 chunks → inflate; `"zip error"` @ `0x8F797F1E`),
  then per group: u32 version → 2×cstr → size-prefixed blob → parse.
- **Loader (single)** `FUN_8f450f90` @ `0x8F450F90`: builds
  `assets/tactics/<a>.atf` (`FUN_8f4509f0`); per group: u32 version → 1×cstr →
  v==7 ? nested sub-records : size-prefixed blob.
- **Dedup + registry**: `FUN_8f43f2e0` (skip already-loaded `(a,b)` for this
  version; `"Skipped load tactics: %s - %s"` @ `0x8F797888`) and
  `FUN_8f43f918` push `{obj, nameA, nameB}` (0x1c bytes) into a global
  **3-slot registry indexed by version** @ `0x8F86ED50` (3 slots ×
  `std::vector`; GC `FUN_8f440248` iterates `while (i != 3)`). Slots 0/1 = the
  two table variants; slot 2 = the v=2 single-weapon tables.
- Version semantics (empirical): where both exist, **v=0 carries the full
  table** (e.g. group3 of `crescentknives_knobsticks.atf`: 232 vec12 / 672
  pool0 / 6124 floats / 5452 u32s) and **v=1 the reduced one** (same file: 2
  vec28 / 2 vec12 / 0 pool0); for trivial tables they are byte-identical
  (`knobsticks_knobsticks.atf` g0 == g1). [UNCERTAIN] which of player/enemy
  each variant serves — the decision code indexes the registry by a version
  it derives per fighter.

## 2. Blob layout (v=0/1/2 attack-table blobs)

Deserializer: `FUN_8f44FF08` @ `0x8F44FF08` → `FUN_8f44e0cc` (pool A) +
`FUN_8f44e394` (pool B) + `FUN_8f44eab4` (payload), into a 0xdc-byte object
(`FUN_8f440b58` ctor). Read order (validated to EOF on every blob walked):

| offset | field | semantics |
|---|---|---|
| +0 | `u16 countA` (**=858 everywhere**) | string pool A entry count — the old parser's "858-byte record" is the next field |
| +2 | `u8 lensA[countA]` | per-string lengths (values 0..~40 — what looked like "u8 animation indices") |
| +2+countA | `char stringsA[ΣlensA]` | concatenated animation names, **no separators**; each entry interned via `FUN_8f45b7e4` → interned-string object ptr (intern pool @ `0x8F86F24C`; unknown name → 0 and the record is upgraded, see §4) |
| … | `u16 countB`, `u8 lensB[countB]`, `char stringsB[…]` | string pool B — **weapon type names** (`""`, `Axes`, `Batons`, `BattleHammers`, …), kept as raw `std::string`s |
| … | `u32 N` | record count (records become 0x10-byte headers in the obj+0xc0 pointer vector) |
| | `u32 cA` | pool size — vec24 pool (obj+0x18), 24-byte elems |
| | `u32 cB` | pool size — vec28 pool B (obj+0x30), 28-byte elems |
| | `u32 cC` | pool size — vec12 pool (obj+0x48), 12-byte elems |
| | `u32 cD` | pool size — id pool (obj+0xa8), u32 interned-anim ptrs |
| | `u32 cE` | pool size — float pool (obj+0x60) |
| | `u32 cF` | pool size — u32 pool (obj+0x90) |
| | `u16 animIdx[N]` | per record: pool-A index → record's animation id (interned ptr; 0 if the name is not interned) |
| | `i16 scale` | fixed-point multiplier for the float pool (10 in all files seen) |
| | `i16 fdata[cE]` | float pool source: `scale==0 ? (float)v : (float)(scale * v)` (int product, then VCVT) |
| | `u32 udata[cF]` | u32 pool, raw memcpy |
| | `u16 idIdx[cD]` | id pool: pool-A index per slot (out-of-range → 0, counted) |
| | nested u16-counted slices (below) | per-record row data |

Nested row data, per record `r` (0..N-1):

| read | into | semantics |
|---|---|---|
| `u16 cntG` | record slice | this record's `cntG` vec24 elems from the vec24 pool (record+0x08/+0x0c = slice begin/end) |
| per vec24: `u16 idxB` | vec24+0x00 (std::string) | **weapon name from pool B** (empty = default/any) |
| per vec24: `u16 cntH` | vec24+0x10/+0x14 | slice of `cntH` vec28-B elems from pool B (obj+0x30) |
| per vec28B: `cstr` | vec28B+0x0c | a name [UNCERTAIN NAME] (FUN_8f21fe48; consumed by stage-3 via `FUN_8f453288`) |
| per vec28B: `u16 cntI` | vec28B+0x04/+0x08 | slice of `cntI` vec12 elems from the vec12 pool |
| per vec28B: `u16 val` | vec28B+0x18 | u16 value [UNCERTAIN NAME] |
| per vec12: `u16 cntJ` | vec12+0x04/+0x08 | slice of `cntJ` pool0 elems from the obj+0x00 pool (28-byte elems) |
| per pool0 elem: (implicit) | pool0+0x00 | next u32 from the **id pool** (record/outcome animation id) |
| per pool0 elem: `u16 cntK` | pool0+0x08/+0x0c | slice of `cntK` floats from the float pool — **distance-window edges** |
| per pool0 elem: `u16 cntL` | pool0+0x14/+0x18 | slice of `cntL` u32s from the u32 pool — **per-window outcome ids** |

Semantics (confirmed by the consumer `FUN_8f457fb8` @ `0x8F457FB8`, the
stage-3 TableAttack candidate selection): the blob is a **distance-indexed
attack table**. Record = one enemy/self animation context (pool-A anim id);
vec24 = per-weapon-type branch (pool-B name); vec28B = a condition row
(named, with an attached u16); vec12 = a sub-condition group; pool0 elem =
one **outcome case**: `{anim/outcome id, float window-edges[], u32
window-outcomes[]}`. At decision time the stage compares the current
(adjusted) distance against the float edges, picks the window index
(`u22 = first edge > distance`), and takes `u32_slice[window-1]`; `> 0` →
the outcome id joins the roulette candidate list (with its frame count).

Minimal real example (`knobsticks_knobsticks.atf` g0): N=1, record anim =
`KnobsticksStartStanceIdle`, one vec24 with weapon `""` (any) and cntH=0 —
a trivial idle-only table.

### v=7 blobs (per-animation record-id sets)

`axes.atf` g1 etc.: `u32 count`, then `count × { cstr anim_name, sub-record }`
(`FUN_8f450f90` v==7 branch). Sub-record (`FUN_8f446528` @ `0x8F446528`,
parsed into the interned animation object's +0x34 struct):

```
u32 blob_size
u32 name_count, cstr names[name_count]   (FUN_8f44638C — linked animation names)
u32 data[N]                              (u32 pool)
vec<0xc> elems with {+4/+8} slices into data  (vtable'd elements, FUN_8f445C30)
```

These per-animation name→ids links are what the probe's global map
(§3, `FUN_8f45bad4`, map vector @ `0x8F86F258`, entries
`{std::string name, vector<u32> ids, …}`) serves at decision time.
[UNCERTAIN] the exact writer that merges v=7 data into the map — the merge
helper is `FUN_8f45b930` (insert-unique), also used by the probe.

## 3. The AnimationFactors probe (`a.a6.S5a`)

### Where it lives

`FUN_8f44ac78` @ **`0x8F44AC78`** = `TacticWeight::evaluate(ctx)`:
`Gb()` dot product **+ per-child probe term** + two pair-list lookups, then
the Linear/Exponential curve. Called from the decision pipeline
(`FUN_8f45ab38` @ `0x8F45AB38`) per chance curve and per QuickAttack/Evade
entry (`FUN_8f44AC78(entry+0xc, ctx)`).

### Native weight layout (from `FUN_8f44c474` = `parse_weight`)

| off | field | off | field |
|---|---|---|---|
| +0x00 | Base | +0x20 | HitFactor |
| +0x04 | CounterFactor | +0x24 | ChildFramesFactor |
| +0x08 | DamageFactor | +0x28 | DistanceFactor |
| +0x0c | HealthFactor | +0x2c | Limit (read as attr `"AntiLimit"+4` → `"Limit"`) |
| +0x10 | EnemyHealthFactor | +0x30 | AntiLimit |
| +0x14 | AnimationFramesFactor | +0x34 | Shift |
| +0x18 | MagicBulletFactor | +0x38 | FactorType (u32 bits: 0=Exp, 1=Linear) |
| +0x1c | MissileBulletFactor | +0x3c | `<AnimationFactors>` children vector |

**R2 finding:** the native parser reads **15 scalar attributes — there is NO
scalar `AnimationFactors` attribute**. `"AnimationFactors"` (@ `0x8F797B60`)
appears only as a child *element* name. The engine's current
`a += ctx.animation_factor * animation_factors;` term
(`tactic_settings.cpp:43`) is therefore a JS-port approximation; the binary
computes the term inline, per child, as `child.DamageFactor·D +
child.CounterFactor·C + child.HitFactor·H`. Also note the binary's dot
product order is **damage first, then counter** — opposite of the engine's
current `score()`.

Children (0x6c-byte entries, parsed by `FUN_8f44b4e4`): `{cstr name @+0x00,
TacticWeight fields @+0x0c}` — so entry+0x10 = its CounterFactor, +0x14 =
DamageFactor, +0x2c = HitFactor. Real shape: `<AnimationFactors
Animation="Throw" DamageFactor="4" CounterFactor="0.5"/>`.

### Probe mechanics (FUN_8f4b1adc @ 0x8F4B1ADC)

For each child of the scored weight:

1. `ids = map[child.name]` — global `{name → record-id list}` map
   (`FUN_8f45bad4`; built from v=7 groups; merge-unique `FUN_8f45b930`).
2. Per id: `rec = find_or_create(memory.records_slot1, id)` — 0x1c-byte
   records `{id, f04, damage, counter, f10, hits, last_frame}`
   (`FUN_8f4b151c`; slot==1 → vector at P+4).
3. Lazy exponential decay: `frames = cur_frame − rec.last_frame`;
   if `frames ≥ 1`: `k = powf(2, −frames/rate)`; all five record floats are
   scaled by `k` (write-back); else use stored values. `rate` defaults to
   `0.0f` (`DAT_8f4b1c68`) and is overridden via `*(M+0x6c)` (a `float*`)
   only when `*(M+0x634) != 0`. `cur_frame = *(M+0x71c)`.
4. `rec.last_frame = cur_frame` (unconditional).
5. Sum: `C += rec.counter`, `D += rec.damage`, `H += rec.hits`.
6. Score term: `a += child.damage_factor*D + child.counter_factor*C +
   child.hit_factor*H`.

The sibling `FUN_8f4b1914` reads ONE record the same way to fill the ctx's
own `counter/damage/hits` (context builder `FUN_8f45342C` @ `0x8F45342C`).
So the whole AnimationFactors mechanism is a **decaying per-animation memory
of counters/damage/hits**: a `<AnimationFactors Animation="RangedMissile"
HitFactor="-0.3" DamageFactor="-1"/>` entry says "the more the enemy has
recently been hit by / dealt damage with RangedMissile, the lower this
candidate's weight". The accumulators are fed by combat events [open point —
feed site not pinned in this pass; it is the TacticMemory R5/R6 domain].

### What `TacticTableSet::animation_factor(anim, target)` should compute

There is no (anim,target)→float table. Mapping the native semantics onto the
ADR-005 D5 interface:

```
animation_factor(anim, target) =
    if anim's weight has an <AnimationFactors Animation="target"> child:
        child.DamageFactor·D(target) + child.CounterFactor·C(target) + child.HitFactor·H(target)
    else 0.0f   // neutral, never an error
```

with `C/D/H(target)` the decayed accumulator sums for `target`'s record-id
set from the fight memory (v=7-built), decayed per §3. Until the engine's
TacticMemory accumulators exist (R5/R6), the neutral `0.0f` default is
behaviorally exact for empty memory: all records start zeroed, so
`C = D = H = 0` and every term is 0.

Note for the post-GREEN wiring (backend-owned): because there is no scalar
`AnimationFactors` coefficient in this build, the D5 score line
`ctx.animation_factor * animation_factors` should become a **sum of the
per-child terms** (or `animation_factors` pinned to 1), otherwise the probe
gets double-scaled. Flagged, not implemented — wiring is out of R2 scope.

## 4. Edge path: unknown animation names

Pool-A interning is lookup-only (`FUN_8f45b7e4`: found → interned-object ptr;
missing → 0). Records whose pool-A index resolves to 0 are upgraded from the
0x10-byte header to a full 0x170-byte runtime record object
(`FUN_8f47f02c`), collected in a fixup list, and finalized with a virtual
call (`vtable+4`) in the parser epilogue (`FUN_8f44eab4` tail). Normal
tables never take this path (all pool-A names resolve).

## 5. Anchors

| what | address |
|---|---|
| pair path builder (`assets/tactics/A_B.atf`) | `FUN_8f450250` @ `0x8F450250` |
| single path builder (`assets/tactics/A.atf`) | `FUN_8f4509f0` @ `0x8F4509F0` |
| pair loader loop | `FUN_8f4514d8` @ `0x8F4514D8` |
| single loader loop (v=7 branch) | `FUN_8f450f90` @ `0x8F450F90` |
| zlib inflate | `FUN_8f2065e4` @ `0x8F2065E4` |
| dedup check / register | `FUN_8f43f2e0` / `FUN_8f43f918` |
| table registry (3 version slots) | `0x8F86ED50` (GC `FUN_8f440248`) |
| blob parse entry / payload deserializer | `FUN_8f44ff08` / `FUN_8f44eab4` |
| string pool A / pool B readers | `FUN_8f44e0cc` / `FUN_8f44e394` |
| intern pool | `0x8F86F24C` (`FUN_8f45b7e4`) |
| v=7 sub-record parser | `FUN_8f446528` (+ `FUN_8f44638c`) |
| **TacticWeight::evaluate (score+probe+curve)** | **`FUN_8f44ac78` @ `0x8F44AC78`** |
| **probe accumulator (decay+sums)** | **`FUN_8f4b1adc` @ `0x8F4B1ADC`** |
| single-record variant (ctx counter/damage/hits) | `FUN_8f4b1914` |
| name→ids map gather | `FUN_8f45bad4` (map @ `0x8F86F258`) |
| record find-or-create (0x1c) | `FUN_8f4b151c` |
| frame / decay-flag / rate-ptr accessors | `FUN_8f4a6e44` (+0x71c) / `FUN_8f4a7220` (+0x634) / `FUN_8f454344` (+0x6c) |
| powf | `FUN_8f72ed40` |
| decision ctx builder / pipeline entry | `FUN_8f45342c` / `FUN_8f45ab38` |
| stage-3 TableAttack selection (blob consumer) | `FUN_8f457fb8` |
| rate defaults / pair defaults | `DAT_8f4b1c68` = 0.0f, `DAT_8f4b1ad8` = 0.0f, `DAT_8f44b0e4` = 0.0f |
| parse_weight (15 attrs) | `FUN_8f44c474` |
| `<AnimationFactors>` children parser | `FUN_8f44b4e4` |

## 6. Candidate C++ (embedded)

Full candidate also at `reverse/analysis/atf_record_858.candidate.cpp`.
The two reversed functions, verbatim-matched (term order, write-back order,
defaults):

```cpp
// [ORIGINAL] FUN_8f4b1adc @ 0x8F4B1ADC — per-child probe accumulator.
static void atf_probe_accumulate(TacticMemoryNative* mem, int slot,
                                 const char* child_name,
                                 float* out_counter, float* out_damage,
                                 float* out_hits)
{
    *out_counter = 0.0f;
    *out_damage = 0.0f;
    *out_hits = 0.0f;
    std::vector<std::uint32_t> ids;
    atf_gather_record_ids(child_name, ids);              // FUN_8f45BAD4
    std::vector<AtfMemoryRecord>& vec =
        slot ? mem->records_slot1 : mem->records_slot0;  // slot==1 -> P+4
    for (std::uint32_t id : ids) {
        AtfMemoryRecord* rec = atf_record_find_or_create(vec, id); // FUN_8f4B151C
        const int cur = mem->frame_counter;              // FUN_8f4A6E44: M+0x71c
        float rate = 0.0f;                               // DAT_8f4b1c68 = 0.0f
        if (mem->decay_enabled != 0) {                   // FUN_8f4A7220: M+0x634
            const float* p = mem->decay_rate;            // FUN_8f454344:  M+0x6c
            if (p != nullptr) rate = *p;
        }
        const int frames = cur - rec->last_frame;
        float c, d, h;
        if (frames < 1) {
            c = rec->counter;
            d = rec->damage;
            h = rec->hits;
        } else {
            const float k = std::pow(2.0f, -(float)frames / rate);  // FUN_8f72ED40
            d = k * rec->damage;   rec->damage  = d;
            c = k * rec->counter;  rec->f04     = rec->f04 * k;
            h = k * rec->hits;     rec->counter = c;
            rec->hits = h;         rec->f10     = rec->f10 * k;
        }
        rec->last_frame = cur;                           // unconditional
        *out_counter += c;
        *out_damage   += d;
        *out_hits     += h;
    }
}

// [ORIGINAL] FUN_8f44ac78 @ 0x8F44AC78 — TacticWeight::evaluate.
float tactic_weight_evaluate(const TacticWeightNative& w, const TacticCtxNative& ctx)
{
    // Gb() — native term order (damage FIRST, then counter):
    float a = ctx.damage * w.damage_factor
            + ctx.counter * w.counter_factor
            + (1.0f - ctx.health) * w.health_factor
            + (1.0f - ctx.enemy_health) * w.enemy_health_factor
            + (float)ctx.anim_frames * w.animation_frames_factor
            + (float)ctx.magic_bullets * w.magic_bullet_factor
            + (float)ctx.missile_bullets * w.missile_bullet_factor
            + ctx.hits * w.hit_factor
            + (float)ctx.child_frames * w.child_frames_factor
            + ctx.distance * w.distance_factor
            + w.shift;

    // a.a6.S5a probe — per <AnimationFactors> child:
    if (!w.children.empty()) {
        for (const AnimFactorEntry& child : w.children) {
            float c = 0.0f, d = 0.0f, h = 0.0f;
            atf_probe_accumulate((TacticMemoryNative*)ctx.memory, /*slot=*/1,
                                 child.name, &c, &d, &h);
            a += child.damage_factor * d
               + child.counter_factor * c
               + child.hit_factor * h;
        }
    }

    // pair-list lookups (first match from begin; default DAT_8f44b0e4 = 0.0f):
    float v1 = 0.0f;
    for (const PairKV& kv : w.pair_list1) {
        if (kv.key == ctx.pair_key1) { v1 = kv.value; break; }
    }
    float v2 = 0.0f;
    for (const PairKV& kv : w.pair_list2) {
        if (kv.key == ctx.pair_key2) { v2 = kv.value; break; }
    }
    a = a + v1 + v2;

    // curve (FactorType +0x38 bits: 0 = Exponential, 1 = Linear):
    if (w.factor_type_bits == 0) {
        if (a >= 0.0f) {
            return w.limit + (w.base - w.limit) * std::pow(2.0f, -a);
        }
        return w.anti_limit + (w.base - w.anti_limit) * std::pow(2.0f, a);
    }
    if (w.factor_type_bits == 1) {
        if (a >= 0.0f) {
            float t = 1.0f;
            if (a <= 1.0f) t = a;
            return w.base + (w.limit - w.base) * t;
        }
        const float t = (a < -1.0f) ? 1.0f : -a;
        return w.base + (w.anti_limit - w.base) * t;
    }
    return 0.0f;                                         // DAT_8f44b0e4
}

// Engine-facing mapping (ADR-005 D5):
float animation_factor(const TacticWeightNative& anim_weight,
                       const char* target, TacticMemoryNative* memory)
{
    for (const AnimFactorEntry& child : anim_weight.children) {
        if (child.name == nullptr || target == nullptr) continue;
        if (std::strcmp(child.name, target) != 0) continue;   // FUN_8f73BD3C
        float c = 0.0f, d = 0.0f, h = 0.0f;
        atf_probe_accumulate(memory, /*slot=*/1, child.name, &c, &d, &h);
        return child.damage_factor * d
             + child.counter_factor * c
             + child.hit_factor * h;
    }
    return 0.0f;    // absent child/table/records: neutral
}
```

## 7. Verifier record (RE-Verification contract)

No `@re-verifier` task/subagent tool is exposed in this session, so the
mandatory loop could not be dispatched here. In place of a verdict, the
deterministic self-check with `cpp-metrics` (the verifier's own metric):

| | calls | branches |
|---|---|---|
| decompiled `FUN_8f44ac78` | 13 | 29 |
| candidate `tactic_weight_evaluate` (+probe, +find_or_create) | 11 | 17 |

The branch delta is fully accounted for by the two inlined `std::vector`
copy loops of the pair lists in the decompile (2 copies × {alloc if/else,
copy do-while, size if} ≈ 10 branches) and their alloc/free calls (≈ 6
calls) — the candidate elides the copies (semantically identical, noted in
the code). Term order, decay write-back order, defaults (0.0f), curve
bit-tests, and the damage-first dot product were matched line-by-line
against the decompile.

**Verdict status: NEEDS_HUMAN** (no subagent channel available; docs-only
deliverable per the R2 contract). The probe stays `0.0f` neutral in the
engine; `engine/game/tactic_tables.cpp` untouched; nothing added to CMake.
If `@re-verifier` later returns GREEN on `0x8F44AC78` + this candidate, the
wiring in §3 ("What animation_factor should compute") is the exact spec to
apply.

## 8. Open points

- v=0 vs v=1 registry slot selection per fighter [UNCERTAIN] — the pipeline
  picks the slot at decision time; not pinned in this pass.
- Accumulator feed site (what increments `rec.damage/counter/hits` on combat
  events) — TacticMemory R5/R6 domain.
- vec28B `+0x0c` name and `+0x18` u16 semantics [UNCERTAIN NAME]; consumed
  by stage-3 via `FUN_8f453288`.
- The `{name → ids}` map writer (v=7 merge into `0x8F86F258`) — merge helper
  confirmed (`FUN_8f45b930`), writer site not pinned.

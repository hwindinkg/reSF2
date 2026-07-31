# Decision semantics — Shadow Fight 2 AI decision subsystem

Reverse-engineering notes for the decision routine that consumes the tracer
stage strings (0x8F798090..0x8F79834C) in `game_region_runtime.bin`
(base 0x8F057000, ARM:LE:32:v7).

Prior anchors:
- weight-curve evaluate: `FUN_8f44ac78` (0x8F44AC78)
- per-slot threshold rollers: `FUN_8f45a920` (0x8F45A920), `FUN_8f45aa2c` (0x8F45AA2C)
- quick-attack scoring: `FUN_8f45456c` (0x8F45456C)
- tracer (stage strings consumer): `FUN_8f4556fc` (0x8F4556FC)
- decision pass: `FUN_8f45ab38` (0x8F45AB38) — sole caller of the tracer

---

## R3 — stage chance comparator

**Question:** how does a weight-curve evaluate result become a fire/no-fire draw,
what is the comparator, is the threshold rolled per stage per decision or once
per slot, is the curve value normalized, and does every stage share the
quick-attack pattern?

### 1. Call chain / object layout

```
manager FUN_8f4a67a0 (0x8F4A67A0)                  [per-frame battle AI update]
  └─ decision object (0xF4 bytes, ctor FUN_8f452b34 @ 0x8F452B34, held at manager+0x634)
       ├─ decision table ptr @ decision+0x6c (ctor FUN_8f4488ac @ 0x8F4488AC)
       │    └─ 24-float (0x60-byte) WeightCurves at table+0x18/0x78/0xd8 (UseDefense A/B/C),
       │       +0x138 (UseSafeAttack), +0x198 (TableAttack), +0x210 (UseCautiousMovements),
       │       +0x270 (DodgeMissiles), +0x2d0 (stage X, untraced)
       │       └─ 108-byte (0x6c) slot vectors at table+0x1f8 (QuickAttack), +0x204 (Evade);
       │          curve struct lives at slot+0x0c (24 floats; slot ends exactly at 0x6c)
       └─ decision pass FUN_8f45ab38 (0x8F45AB38)  [called per decision cycle]
             └─ tracer FUN_8f4556fc (0x8F4556FC)   [logs every stage: "<Fire|NoFire> / <score>"]
```

Curve default ctor `FUN_8f44ac08` (0x8F44AC08): all fields 0, `mode = 1`
(float 0x1 → 1.4013e-45). The two "Intervals" curves use pairs at
table+0x330/0x390, +0x3f0/0x450, +0x4b0/0x510, +0x570/0x5d0 (randomized lerp
pairs used for DistanceError/FrameError, see §4).

### 2. Threshold rolling — per stage, per decision pass (slots: per slot)

All thresholds are rolled fresh **every decision pass**; nothing is persisted
between passes except the zeroed ctor state:

| roller | binary | writes | cadence |
|---|---|---|---|
| `FUN_8f4536c8` (0x8F4536C8) | 6 sequential `FUN_8f264414` rolls → decision+0xa8, +0xac, +0xb0, +0xb4, +0xb8, +0xbc | per pass, **only if** the table is found in the registry list `FUN_8f43f158`; otherwise thresholds keep ctor value 0.0 → `0.0 < score` fires on any positive score (edge case) |
| `FUN_8f45a920` (0x8F45A920) | per QuickAttack slot: `roll → slotRes+4` in result array @ decision+0x8c (12-byte slots) | per pass, unconditionally |
| `FUN_8f45aa2c` (0x8F45AA2C) | per Evade slot: `roll → slotRes+4` in result array @ decision+0x98 | per pass, unconditionally |
| `FUN_8f453a94` (0x8F453A94) | 1 roll `r` for the UseDefense 4-way draw | per pass, inside the evaluate block |

So: **per stage per decision** (per slot per decision for QuickAttack/Evade).
`FUN_8f4536c8` is also called from `FUN_8f453820` (0x8F453820, another decision
consumer, pre-battle/interval path).

Roll source: `FUN_8f264414` (0x8F264414) = `u1/u2 + (u3/u4)/u5` over uint
PRNG outputs (`FUN_8f264a34`/`FUN_8f264a64`). Result is **not** clamped to
[0,1] — distribution centred near 1 with a tail above 1; thresholds may exceed
1.0. `FUN_8f26447c` (0x8F26447C) = same roll scaled by an argument (used by the
final picker, §5).

### 3. The comparator — identical for ALL threshold stages

Every fire/no-fire stage evaluates `score = FUN_8f44ac78(curve, features)` and
stores `decided = threshold < score` (i.e. **fire iff score strictly greater
than threshold**). ASM (identical at all sites, e.g. 0x8F4545D8, 0x8F45ADAC,
0x8F45ACF0, 0x8F45AD1C):

```
vldr.32 s14,[slot,#0x4]   ; threshold
vmov    s15,r0            ; score (return of FUN_8f44ac78)
str     r0,[slot,#0x0]    ; slot.score = score
vcmpe.f32 s15,s14         ; score vs threshold  (GT ⟺ score > threshold)
movle rX,#0x0 ; movgt rX,#0x1
strb  rX,[slot,#0x8]      ; slot.decided = score > threshold
```

| stage | threshold field | curve (table offset) | decided flag | score stored |
|---|---|---|---|---|
| UseSafeAttack | decision+0xac | +0x138 (`FUN_8f446b60`) | +0x89 | +0xe8 |
| TableAttack | decision+0xb0 | +0x198 (`FUN_8f446b68`) | +0x8a | +0xec |
| UseCautiousMovements | decision+0xb4 | +0x210 (`FUN_8f446b80`) | +0xa4 | +0xf0 |
| DodgeMissiles | decision+0xb8 | +0x270 (`FUN_8f446b88`) | +0x74 | +0x78 |
| stage X (untraced) | decision+0xbc | +0x2d0 (`FUN_8f446b90`) | +0x7c | +0x80 |
| QuickAttack[slot] | slotRes+4 (arr @ +0x8c) | slot+0x0c (vec @ table+0x1f8) | slotRes+8 | slotRes+0 |
| Evade[slot] | slotRes+4 (arr @ +0x98) | slot+0x0c (vec @ table+0x204) | slotRes+8 | slotRes+0 |

The quick-attack reference `FUN_8f45456c` is byte-for-byte the same operator
(`pfVar7[1] < fVar3`). **All threshold stages share it.** The 6th roll
(decision+0xa8) written by `FUN_8f4536c8` has no observed consumer in the
decision module — [UNCERTAIN]: likely RNG-stream alignment or an unused field.

### 4. Normalization of the curve value — inside the evaluate

`FUN_8f44ac78(curve, features)` (0x8F44AC78):
1. raw weighted sum `f8 = Σ w_i·f_i + bias` over the feature vector, plus
   per-feature table contributions, plus two key-value lookups (default
   contribution `DAT_8f44b0e4` = 0.0 when the key is absent) → `total`.
2. **mode-dependent normalization into a bounded range** (this is the
   normalization step):
   - `mode == 0.0`: exponential mapping —
     `total >= 0:  upper + (base - upper) * 2^(-total)`  (→ base as total→∞… wait: upper + (base-upper)·2^-t, t→+∞ ⇒ upper)
     `total <  0:  lower + (base - lower) * 2^( total)`  (→ lower as t→-∞)
     (base = curve[0], upper = curve[0xb], lower = curve[0xc]; `2^x` via `FUN_8f72ed40(2.0, x)`).
   - `mode == 1.4013e-45` (int 1; **default** from ctor): clamp `total` to
     [-1,1] and linearly blend —
     `total >= 0:  base + (upper - base) * min(total, 1.0)`
     `total <  0:  base + (lower - base) * min(-total, 1.0)`
   - FP operation order preserved as compiled (A·x + B·y order fixed).
So the raw unbounded weighted sum is normalized to `[curve[0xc], curve[0xb]]`
(bounds are per-curve config, loaded by `FUN_8f44c474`; exact runtime bound
values [UNCERTAIN] — they make the compare `threshold < score` meaningful as a
chance draw).

### 5. UseDefense — the ONE divergence (not threshold-vs-score)

`FUN_8f453a94` (0x8F453A94) is a **4-way categorical draw**, one roll `r` per
pass, against cumulative intervals built from three curve evaluates
(A = table+0x18 `FUN_8f446b48`, B = table+0x78 `FUN_8f446b50`,
C = table+0xd8 `FUN_8f446b58`), stored at decision+0xdc/0xe0/0xe4 (exactly the
three floats of the tracer string "UseDefense: %s / %.4f / %.4f / %.4f"):

```
if (A > r)            return 2;   // 0x8F453B00..0x8F453B10
if (A + B > r)        return 3;   // 0x8F453B20..0x8F453B34
if (C + (A + B) > r)  return 4;   // 0x8F453B38..0x8F453B48 (adds in this order)
return 1;                         // 0x8F453B4C
```
ASM confirms strict GT checks on the cumulative side (`vcmpe.f32`), result in
`param_1[0x1c]`, consumed by the switch in `FUN_8f459b44` (0x8F459B44).

### 6. Consumption of "decided" flags and final pick

- `FUN_8f459b44` (0x8F459B44): uses the decided flags to build a candidate
  list (DodgeMissiles→`FUN_8f458d60(...,0)`, stage X→`FUN_8f458d60(...,1)`,
  UseSafeAttack→`FUN_8f457df8`, TableAttack→`FUN_8f4569c0`, plus a
  `1 - 1/score` surprise roll: `if ((1 - 1/x) < roll) suppress`), candidates
  stored as (id, frames) pairs at decision+0x31.
- `FUN_8f453538` → `FUN_8f446cb4` (0x8F446CB4): **weighted random pick among
  candidates** — pass 1 sums each candidate's curve weight
  (`FUN_8f44ac78(slot+0x0c)`), pass 2 rolls `FUN_8f26447c(rng, sum)` and
  subtract-walks until negative; returns the chosen index into the candidate
  list. Fired stage + chosen action + wait (`decision+0x12`) are logged via the
  tracer ("Decision: %s {Wait=%d}").

### 7. Tracer stage strings (0x8F798090..0x8F79834C)

Consumed only by `FUN_8f4556fc` (xrefs at 0x8F45576C..0x8F455AC4), sole caller
`FUN_8f45ab38`. Per-stage format `"%s / %.4f"` = `<Fire|NoFire name (picked by
the stage's decided bool)> / <score>`; per-slot `"QuickAttack[%d]: %s / %.4f"`
and `"Evade[%d]: ..."` iterate the two result arrays; UseDefense prints the
three cumulative weights; plus DistanceError/FrameError/Intervals/DecisionType/
Decision{Wait} context lines. The two Fire/NoFire name strings
(DAT_8f45611c/DAT_8f456180) are outside the stated range — [UNCERTAIN], not
required for the comparator question.

### 8. Answer summary

1. Comparator: **`decided = threshold < score`** (strict; `score > threshold`
   fires) — identical ASM `vcmpe.f32` + `movgt` at every site, including the
   quick-attack reference `FUN_8f45456c`.
2. Threshold cadence: **per stage per decision pass** — 6 single-stage rolls
   (`FUN_8f4536c8`, gated on table registration) + per-slot rolls for
   QuickAttack (`FUN_8f45a920`) and Evade (`FUN_8f45aa2c`), all called from
   `FUN_8f45ab38` every pass. Nothing cached across passes.
3. Normalization: yes — `FUN_8f44ac78` maps the raw weighted sum through the
   curve mode (default: clamp to [-1,1] + linear blend; mode 0: exponential
   2^±t) into the config-bounded `[lower, upper]` range.
4. All stages share the quick-attack comparator **except UseDefense**, which is
   a one-roll 4-way cumulative-interval draw (`r < A → 2`, `r < A+B → 3`,
   `r < A+B+C → 4`, else 1). Final pick is a weighted random draw over the
   fire-flagged candidates.

### R3 candidate — decision-pass chance draw (FUN_8f45ab38 evaluate block)

```cpp
// ============================================================================
// R3 candidate: per-pass stage chance draw — fire/no-fire + UseDefense draw.
// Binary: reverse/binaries/game_region_runtime.bin (base 0x8F057000, ARM:LE:32:v7)
//   primary : FUN_8f45ab38 @ 0x8F45AB38 — decision pass (evaluate block
//             0x8F45ACB4..0x8F45AE80; rollers + compares, order preserved)
//   deps    : FUN_8f44ac78 @ 0x8F44AC78 — WeightCurve evaluate (normalizes sum)
//             FUN_8f264414 @ 0x8F264414 — roll  u1/u2 + (u3/u4)/u5 (not clamped)
//             FUN_8f4536c8 @ 0x8F4536C8, FUN_8f45a920 @ 0x8F45A920,
//             FUN_8f45aa2c @ 0x8F45AA2C, FUN_8f453a94 @ 0x8F453A94
// Layout per decompile + ASM. Names of fields not provable from the binary are
// tagged [UNCERTAIN NAME]; FP order kept exactly as compiled.
// ============================================================================

// WeightCurve — 24 floats (96 bytes), layout as consumed by FUN_8f44ac78.
struct WeightCurve {             // offset
    float base;                  // +0x00  (*curve): value at total==0
    float w[10];                 // +0x04  feature weights [UNCERTAIN NAME]
    float upper;                 // +0x2c  curve[0xb] — upper bound
    float lower;                 // +0x30  curve[0xc] — lower bound
    float featureId;             // +0x34  curve[0xd] [UNCERTAIN NAME]
    float mode;                  // +0x38  curve[0xe]: 0.0f=exp, 1.4013e-45f=clamp (default)
    float padC[2];               // +0x3c  curve[0xf..0x10]: per-feature vec ptr pair
    float extra[6];              // +0x44  curve[0x11..0x16]: lookup pairs etc. [UNCERTAIN]
};                               // total 0x60

// Stage slot — 108 bytes (0x6c) in the QuickAttack/Evade config vectors.
struct StageSlot {               // offset
    int   id[3];                 // +0x00  [UNCERTAIN NAME]
    float curve[24];             // +0x0c  WeightCurve (96 bytes) — evaluated as `curve`
};                               // total 0x6c (slots are 0x6c apart, count = /27)

// Per-slot result record — 12 bytes in result arrays @ decision+0x8c / +0x98.
struct SlotResult {              // offset
    float score;                 // +0x00
    float threshold;             // +0x04
    bool  decided;               // +0x08  (byte)
};

// Feature vector builder is FUN_8f45342c (0x8F45342C); features layout
// [UNCERTAIN NAME] — passed through to FUN_8f44ac78 unmodified.
extern float roll();                        // FUN_8f264414
extern float evaluateCurve(const float* c, const void* features); // FUN_8f44ac78

// Threshold fields are stored as raw 32-bit words and read back by the ARM
// VFP as floats (vldr.32); the decompiler renders that as (float)int due to
// storage typing. bit-reinterpret is the faithful reading:
static inline float f32(int bits) { union { int i; float f; } u; u.i = bits; return u.f; }

// slotCount: element count of a 108-byte-slot vector. The binary computes it
// as floatCount * 0x684bda13 (magic for /27), e.g. FUN_8f45a920:
//   iVar1 = (vec[1] - vec[0]) >> 2;   uVar6 = iVar1 * 0x684bda13;
static inline int slotCount(const int* vec) {
    int floatCount = (vec[1] - vec[0]) >> 2;
    return (int)((unsigned)floatCount * 0x684bda13u);  // == floatCount / 27
}

// ---- threshold rollers (binary bodies, order preserved) --------------------

static void rollSingleStageThresholds(int* d, const void* rngSrc /*[UNCERTAIN]*/) {
    // FUN_8f4536c8 @ 0x8F4536C8: the roll source object is loaded ONCE
    // (uVar2 = *(DAT_8f45381c-pool)) and reused for all six rolls.
    // (Called only when the table is registered in the FUN_8f43f158 list;
    //  otherwise thresholds stay 0.0 from the ctor.)
    (void)rngSrc;             // roll() reads the shared source internally
    d[0x2a] = (int)roll();    // +0xa8 — no observed consumer in decision module
    d[0x2b] = (int)roll();    // +0xac — UseSafeAttack
    d[0x2c] = (int)roll();    // +0xb0 — TableAttack
    d[0x2d] = (int)roll();    // +0xb4 — UseCautiousMovements
    d[0x2e] = (int)roll();    // +0xb8 — DodgeMissiles
    d[0x2f] = (int)roll();    // +0xbc — stage X (untraced)
}

static void rollSlotThresholds(SlotResult* res, int count /*slots*/, const void* rngSrc) {
    // FUN_8f45a920 @ 0x8F45A920 (res = decision+0x8c) /
    // FUN_8f45aa2c @ 0x8F45AA2C (res = decision+0x98): identical bodies.
    for (int i = 0; i < count; ++i) {
        res[i].threshold = roll();   // *(slot+4) = FUN_8f264414(...)
    }
    (void)rngSrc;
}

static void evaluateSlotVector(const StageSlot* slots, int count,
                               SlotResult* res, const void* features) {
    // FUN_8f45456c @ 0x8F45456C (QuickAttack) and the Evade loop inside
    // FUN_8f45ab38 @ 0x8F45AD88..0x8F45ADD4 — identical pattern.
    for (int i = 0; i < count; ++i) {
        float score = evaluateCurve(slots[i].curve, features); // slot+0x0c, stride 0x6c
        res[i].score = score;                                  // slot+0x00
        res[i].decided = res[i].threshold < score;             // slot+0x08: thr < score
    }
}

// ---- UseDefense: 4-way cumulative-interval draw ----------------------------

static int useDefenseDraw(float a, float b, float c) {
    // FUN_8f453a94 @ 0x8F453A94 — one roll per pass; ASM checks are
    // A > r, (A+B) > r, C+(A+B) > r — strict, in this order.
    float r = roll();
    if (a > r)            return 2;
    float ab = a + b;     // vadd.f32 s15,s15,s14 (A+B computed once)
    if (ab > r)           return 3;
    float abc = c + ab;   // vadd order: C + (A+B)
    if (abc > r)          return 4;
    return 1;
}

// ---- decision pass — evaluate block of FUN_8f45ab38 ------------------------

// d: decision object (int*; fields per §3/§5 table). table: decision+0x6c.
// features: FUN_8f45342c output (stack buffer, 60 bytes) [UNCERTAIN NAME].
// Returns nothing; sets d+0x1c (UseDefense choice) and the decided flags.
void decisionStageChanceDraw(int* d, void* table, const void* features) {
    // (FUN_8f45ab38 @ 0x8F45ACB4..0x8F45AE80; the pre-block bookkeeping and the
    //  candidate-list consumption of the flags are outside this candidate.)

    // 1) roll single-stage thresholds (FUN_8f4536c8; skipped when the table is
    //    not in the FUN_8f43f158 registry — then 0xac..0xbc remain 0.0).
    rollSingleStageThresholds(d, /*rngSrc=*/0);

    // 2) roll per-slot thresholds (unconditional).
    rollSlotThresholds((SlotResult*)d[0x8c], slotCount(table + 0x1f8), 0); // FUN_8f45a920
    rollSlotThresholds((SlotResult*)d[0x98], slotCount(table + 0x204), 0); // FUN_8f45aa2c

    // 3) gate FUN_8f452f28; when nonzero:
    //    UseDefense 4-way draw (FUN_8f453a94) — A/B/C at table+0x18/+0x78/+0xd8;
    //    the chosen value goes to d+0x1c, and FUN_8f453a94 also stores A/B/C at
    //    d+0xdc/0xe0/0xe4 (the three floats of the "UseDefense" trace line).
    d[0x1c] = useDefenseDraw(
        evaluateCurve((const float*)((const char*)table + 0x18), features), // FUN_8f446b48
        evaluateCurve((const float*)((const char*)table + 0x78), features), // FUN_8f446b50
        evaluateCurve((const float*)((const char*)table + 0xd8), features));// FUN_8f446b58

    // 4) single-stage fire/no-fire — same operator everywhere:
    //    decided = threshold(0xac..0xbc) < score(curve).
    {
        float s = evaluateCurve((const float*)((const char*)table + 0x138), features); // FUN_8f446b60
        d[0x3a] = (int)s;                              // score @ +0xe8
        *(bool*)((char*)d + 0x89) = f32(d[0x2b]) < s;  // UseSafeAttack: thr@0xac
    }
    {
        float s = evaluateCurve((const float*)((const char*)table + 0x198), features); // FUN_8f446b68
        d[0x3b] = (int)s;                              // score @ +0xec
        *(bool*)((char*)d + 0x8a) = f32(d[0x2c]) < s;  // TableAttack: thr@0xb0
    }
    evaluateSlotVector((const StageSlot*)((const char*)table + 0x1f8),
                       slotCount((const int*)((const char*)table + 0x1f8)),
                       (SlotResult*)d[0x8c], features);  // QuickAttack (FUN_8f45456c)
    evaluateSlotVector((const StageSlot*)((const char*)table + 0x204),
                       slotCount((const int*)((const char*)table + 0x204)),
                       (SlotResult*)d[0x98], features);  // Evade loop @ 0x8F45AD88
    {
        float s = evaluateCurve((const float*)((const char*)table + 0x210), features); // FUN_8f446b80
        d[0x3c] = (int)s;                              // score @ +0xf0
        *(bool*)((char*)d + 0xa4) = f32(d[0x2d]) < s;  // UseCautiousMovements: thr@0xb4
    }
    {
        float s = evaluateCurve((const float*)((const char*)table + 0x270), features); // FUN_8f446b88
        d[0x1e] = (int)s;                              // score @ +0x78
        *(bool*)((char*)d + 0x74) = f32(d[0x2e]) < s;  // DodgeMissiles: thr@0xb8
    }
    {
        float s = evaluateCurve((const float*)((const char*)table + 0x2d0), features); // FUN_8f446b90
        d[0x20] = (int)s;                              // score @ +0x80
        *(bool*)((char*)d + 0x7c) = f32(d[0x2f]) < s;  // stage X (untraced): thr@0xbc
    }

    // 5) afterwards (outside candidate): FUN_8f459b44 builds the candidate list
    //    from the decided flags; FUN_8f446cb4 picks one by weighted random draw
    //    (sum of candidate curve weights, roll scaled to [0,sum), subtract-walk).
}
```

NOTES:
- Comparator operator is **proven by ASM** (`vcmpe.f32 s15,s14; movgt`) at all
  seven sites; not inferred.
- The 6th roll (+0xa8) has no observed consumer; assumed RNG-stream alignment.
- `rollSingleStageThresholds`'s rng source argument is a pointer loaded from a
  literal pool (`DAT_8f45381c`); the roll function itself takes no seed — the
  value is a shared RNG-source object. [UNCERTAIN]
- UseDefense boundary semantics: r == A exactly falls through to bin 3;
  r == A+B exactly falls through to bin 4/1 — strict inequalities throughout.
- Curve bounds (lower/upper) are config-driven; runtime values not statically
  known. [UNCERTAIN]

---

## R4 — ExpectedWait mapping

**Question:** how does a picked `<Animation>` weight under an ExpectedWait-typed
tactic become `Decision {Wait=%d}` frames — direct frames? scaled? clamped? rounded?

**Short answer:** the expected-wait weight **never becomes the frame count
directly**. It drives two probabilities (an attack gate `1/w` and the roulette
pick). The printed/used `Wait` frames come from *animation/enemy duration
arithmetic* computed per decision path (scaled by `(speed+1)`, offset by
`(−X+2)` with `X = anim+0x74`, `+1` on some paths, clamped via `min/max`), and
the tracer prints `|wait|` (absolute value).

### 1. What "ExpectedWait" is in the binary

`tacticSettings.xml` (embedded; key table 0x8F797574..0x8F79834C) contains two
sibling sections parsed by the tactics-table ctor `FUN_8f4488ac` (0x8F4488AC):

| section key (addr) | xref | parsed into (TacticSet offsets) | runtime consumer |
|---|---|---|---|
| `AnimationWeights` (0x8F797B30) | 0x8F448F10 | record list @ +0x630/+0x634/+0x638 | pick (FUN_8f446cb4) |
| `ExpectedWait` (0x8F797B44) | 0x8F449CD8 | record list @ +0x63c/+0x640/+0x644 | gate (FUN_8f446b98) |

Both lists hold 0x6c-byte records: `name` std::string @ +0x00 (ptr/end/cap),
`TacticWeight` (0x60 bytes, 24 floats) @ +0x0c. Entry parse per section:
entry key compared against `Animation` (0x8F79964C, ldr 0x8F449D00/add
0x8F449D0C — exact), the entry's `Name` attribute (0x8F763484, add 0x8F449D14)
becomes the record name, the rest is parsed by `FUN_8f44c7d8` →
`FUN_8f44c474` (weight fields: CounterFactor/DamageFactor/HealthFactor/
EnemyHealthFactor/AnimationFramesFactor/ChildFramesFactor/MagicBulletFactor/
MissileBulletFactor/HitFactor/DistanceFactor/AntiLimit/FactorType +
key/value pairs — all in the embedded key table). An entry with an empty
`Name` acts as the **default record**.

### 2. The ExpectedWait weight — used as a probability, not as frames

**Gate evaluate** `FUN_8f446b98` (0x8F446B98), called from `FUN_8f459b44`
(0x8F459B44) as `w = FUN_8f446b98(table, decision+0x58, ctx)`:
- iterate the +0x63c list; take the first record with empty name **or** whose
  name matches the filter object (`FUN_8f47cf1c(filter, recName)` — filter =
  decision+0x58 = the **current animation** object, name @ anim+0x7c);
- `w = FUN_8f44ac78(rec+0x0c, ctx)` — the TacticWeight evaluate (R2-GREEN:
  weighted sum → curve, linear/clamped or exponential, bounds [lower, upper]);
- no record → log `Expected Wait ERROR` (0x8F7979C8, xref 0x8F446C1C) and
  return 1.0f (0x3F800000).

**Gate usage** (`FUN_8f459b44`, after the use-defense draw):
```
gate = 0.0f;                       // DAT_8f459f60 == 0.0f (raw bytes 00 00 00 00)
if (w >= 1.0f) gate = 1.0f - 1.0f/w;
if (gate < roll())  → attack flag decision+0x50 = 1     // P = P(roll>1−1/w) = 1/w
else if (flag+0x50 == 0 && flag+0xc0 == 0) → return early (keep waiting)
```
So the expected-wait weight acts as the mean wait between attacks (per-tick
attack probability ≈ 1/w); `w < 1` → always attack.

### 3. The Wait frames — where they really come from

1. **Candidates**: each decision path pushes `{animObjPtr, value}` 8-byte pairs
   to decision+0xc4 (FUN_8f45483c):
   - attack path (FUN_8f459b44, "5"): `value = max(animFrames(a), min(animRange(a), (speedVal(self,1) − damage) + 1))` — clamped
   - ready path (FUN_8f459b44, "9"): `value = animFrames(a)`
   - use-defense/wait path (FUN_8f457fb8 @ 0x8F457FB8, final push loop 0x8F458940..):
     `value = (c660(enemy,1) − damage) + 1`  (enemy attack duration − our attack value + 1)
   where `damage` = decision+0x0c (attack value from FUN_8f45ab38
   0x8F45AB38: enemyState + counter + lerped TacticWeight pair,
   FUN_8f446fb0 @ 0x8F446FB0 = `w1 + roll·(w2−w1)` over table+0x3f0/+0x450);
   `animFrames(x)=FUN_8f47cbe0` (0x8F47CBE0), `animRange(x)=FUN_8f47cbfc`
   (0x8F47CBFC), both = `(*(ushort*)(x+100)+1) · (maxAttr − *(int*)(x+0x74) + 2) + 1`
   with the attribute set from globals FUN_8f43f0b8 (→0x8F86EDDC) /
   FUN_8f43f0cc (→0x8F86EACC), runtime-populated name lists ([UNCERTAIN]
   names — the static tables are empty strings);
   `c660(x,1)=FUN_8f47c660` (0x8F47C660) = `(speed+1)·(maxType4Attr − X + 2)`
   (entries with type==4, no +1);
   `speedVal(x,1)=FUN_8f47d294` (0x8F47D294) = same shape, 11-char-name match
   ([UNCERTAIN] name), `min(max, byte@x+0x78)` before scaling.
2. **Split** `FUN_8f4550e0` (0x8F4550E0): ids → candidate vector @ decision+0xd0
   (4-byte), values → **wait array @ decision+0x18** (4-byte), same order,
   1:1 with the pairs. (`FUN_8f4563e0` @ 0x8F4563E0 pre-filters pairs whose id
   fails FUN_8f4561e4; id==0 pairs are kept.)
3. **Pick** `FUN_8f453538` (0x8F453538) → `FUN_8f446cb4` (0x8F446CB4):
   roulette over the candidates — per candidate, weight =
   `FUN_8f44ac78(rec+0x0c, ctx)` of the **AnimationWeights** record (+0x630
   list) whose name matches the candidate animation (empty-name = default;
   unmatched → 0.0f); sum → `point = sum · roll()` (FUN_8f26447c @ 0x8F26447C)
   → subtract-walk → first negative → **index**. Filter quirk: candidate id==0
   means "current animation" and is processed with the filter object
   (decision+0x58) when filter != 0, otherwise zero slots are skipped; the
   returned index counts processed slots (so it equals the physical slot index
   whenever the filter is set — the normal in-combat case). Returns -1 when
   nothing is picked.
4. **Wait** `FUN_8f45ab38`: `decision+0x12 = waitArray[idx]` (0x8F45B030
   region), `decision+0x12 = waitArray[idx]` — the frames; per-tick countdown
   (decrement while > 1 → re-decision when ≤ 1). Failure paths set
   `decision+0x12 = 0x88CA6C00` (sentinel).
5. **Print** `FUN_8f4556fc` (0x8F4556FC), call site 0x8F455AC8:
   `Decision: %s {Wait=%d}` (0x8F79834C) with `%s` = picked anim name
   (`FUN_8f47b408(anim) = anim+0x7c`), `%d` = `|wait|`
   (`bic r2, r3, r3, asr #0x1f` — absolute value). The second logger call
   (flee case) prints Wait=0.

### 4. Answer to the R4 question

- **Not direct frames, not scaled from the weight, not rounded from it.** The
  expected-wait weight `w` is consumed as a probability: gate `P_attack = 1/w`
  (w ≥ 1) per decision tick, and weights drive the roulette pick of which
  `<Animation>` wins.
- The `Wait=%d` frame count is **duration arithmetic on the picked animation /
  enemy**: `(speed+1)·(maxAttr − X + 2)[+1]`, clamped by
  `min/max(·, (speedVal − damage) + 1)`, printed with `abs()`, and counted down
  per tick. The only "weight→frames" link is indirect: the gate keeps the bot
  in the waiting state, and the wait value of the *current* animation is what
  eventually expires into a fresh decision.

### 5. R4 candidate — FUN_8f446cb4 (0x8F446CB4)

Full candidate source: `reverse/analysis/decision_semantics_r34.candidate.cpp`
(embedded below).

```cpp
// ============================================================================
// R4 candidate: FUN_8f446cb4 @ 0x8F446CB4 — weighted-random pick of one
// candidate animation. "picked <Animation> weight" step of the decision.
// Binary: reverse/binaries/game_region_runtime.bin (base 0x8F057000, ARM:LE:32:v7)
//   deps: FUN_8f44ac78 @ 0x8F44AC78 — TacticWeight evaluate (R2 GREEN)
//         FUN_8f47cf1c @ 0x8F47CF1C — anim-name match (name @ anim+0x7c,
//                                      alt-name list @ anim+0xa8)
//         FUN_8f264414 @ 0x8F264414 / FUN_8f26447c @ 0x8F26447C — rolls
// Layout per decompile + ASM (0x8F446CB4..0x8F446F38). Field names not
// provable from the binary are tagged [UNCERTAIN NAME]; FP order as compiled.
// ============================================================================

// ---- externs (binary addresses) --------------------------------------------
extern float FUN_8f44ac78(const void* weight /*rec+0x0c*/, const void* ctx);
extern unsigned FUN_8f264a34(void);   // PRNG part A
extern unsigned FUN_8f264a64(void);   // PRNG part B

// FUN_8f264414 @ 0x8F264414 — u1/u2 + (u3/u4)/u5; not clamped to [0,1]
static float roll01(void) {
    float f6 = (float)FUN_8f264a34(), f3 = (float)FUN_8f264a64();
    float f7 = (float)FUN_8f264a34(), f4 = (float)FUN_8f264a64();
    float f5 = (float)FUN_8f264a64();
    return f6 / f3 + (f7 / f4) / f5;   // exact op order
}

// FUN_8f47cf1c @ 0x8F47CF1C — 1 iff name == anim name or in anim alt-name list
static int animMatchesName(const void* anim, const char* name) {
    const char* s = *(const char**)((const char*)anim + 0x7c);
    const char* e = *(const char**)((const char*)anim + 0x80);
    int len = (int)strlen(name);
    if (len == (int)(e - s) && memcmp(s, name, (size_t)len) == 0) return 1;
    const char** it  = *(const char***)((const char*)anim + 0xa8);
    const char** end = *(const char***)((const char*)anim + 0xac);
    while (it < end) {                       // 12-byte entries {ptr,len,cap}
        if (strcmp(it[0], name) == 0) return 1;
        it += 3;
    }
    return 0;
}

// record count of a 0x6c-stride vector: the binary divides the byte count by
// 27 via magic constant (words * 0x97B425ED) >> 36 (rendered -0x684bda13).
static int recordCount(const void* vec /*{begin,end} ptrs*/) {
    const char* b = *(const char**)vec;
    const char* e = *(const char**)((const char*)vec + 4);
    unsigned words = (unsigned)(e - b) >> 2;
    return (int)(((unsigned long long)words * 0x97B425EDu) >> 36); // == words/27
}

// candidate weight: first AnimationWeights record (list @ TacticSet+0x630)
// with empty name or name == anim name; default 0.0f (DAT_8f446f40)
static float candidateWeight(const void* table, const void* anim, const void* ctx) {
    const char** rec    = *(const char***)((const char*)table + 0x630);
    int count = recordCount((const char*)table + 0x630);
    for (int i = 0; i < count; ++i) {
        const char* name = rec[0];
        if (name == rec[1] /*empty*/ || animMatchesName(anim, name)) {
            return FUN_8f44ac78((const char*)rec + 0x0c, ctx);
        }
        rec += 0x1b;                          // 0x6c stride
    }
    return 0.0f;
}

// FUN_8f446cb4 @ 0x8F446CB4 — returns index into the candidate vector or -1.
//   table    : TacticSet (records @ +0x630; also +0x63c = ExpectedWait, unused here)
//   cand     : candidate vector {begin,end} of anim-object ptrs (decision+0xd0);
//              id == 0 = "current animation" (uses `filter` when set)
//   filter   : current-animation object (decision+0x58), may be 0
//   ctx      : TacticContext (FUN_8f45342c output)
int pickIndexByWeight(const void* table, const int* cand,
                      const void* filter, const void* ctx) {
    const int* end = *(const int**)((const char*)cand + 4);
    const int* it  = *(const int**)cand;

    // pass 1: sum of weights over filter-eligible candidates. Slot test as
    // compiled (ASM 0x8F446D24..38): id != 0 -> weight(id); id == 0 && filter
    // != 0 -> weight(filter), ONE slot (no read-ahead, no pairing with the
    // next slot); id == 0 && filter == 0 -> skip slot (advance one, continue).
    // Each accumulated weight is int-truncated before the add, as compiled:
    // vcvt.s32.f32 / vcvt.f32.s32 @ 0x8F446F04..08.
    float sum = 0.0f;
    while (it < end) {
        const void* anim;
        int id = *it;
        if (id == 0) {
            if (filter == 0) {                // zero slot, no filter -> skip
                ++it;
                continue;
            }
            anim = filter;                    // zero slot = current animation
        } else {
            anim = (const void*)id;
        }
        sum += (float)(int)candidateWeight(table, anim, ctx);
        ++it;
    }

    if (sum > 0.0f) {
        float point = sum * roll01();         // FUN_8f26447c(rng, sum)
        it  = *(const int**)cand;
        int idx = 0;                          // counts processed slots; equals
                                              // the physical slot index when
                                              // filter is set (r10 @ 0x8F446ED4)
        while (it < end) {
            const void* anim;
            int id = *it;
            if (id == 0) {
                if (filter == 0) {            // zero slot, no filter -> skip
                    ++it;                     // (binary pass 2 returns -1 at
                    continue;                 //  vector end, incl. after skip)
                }
                anim = filter;
            } else {
                anim = (const void*)id;
            }
            point -= candidateWeight(table, anim, ctx);   // raw float (no vcvt)
            if (point < 0.0f) return idx;
            ++it;
            ++idx;
        }
    }
    return -1;
}
```

NOTES (R4):
- All string anchors verified by direct memory reads; the `Decision` literal
  (0x8F456168 = 0x342880) folds to 0x8F797B4C ("Wait" substring of
  "ExpectedWait") under a plain PC+8 fold — a 0x800 relocation skew in this
  literal; Ghidra's xref resolves it to 0x8F79834C = the real format string.
  [UNCERTAIN] — anchor via the Ghidra xref, not the raw fold.
- The attribute-name lists behind cbe0/cbfc (FUN_8f43f0b8/FUN_8f43f0cc) are
  runtime-populated; static contents are empty strings, so the exact attribute
  names are [UNCERTAIN]. The formulas and their shape are exact.
- `c660`'s entry type selector (entry+0x18 == 4) and `d294`'s 11-char name
  (DAT_8f47d354 pool) are verified in ASM; the names are [UNCERTAIN].
- The gate constant DAT_8f459f60 = 0.0f verified raw; `w < 1` → always attack.
- Failure/sentinel path sets decision+0x12 = 0x88CA6C00 (would print a huge
  |abs| if ever logged); normal path only.
- R4 candidate round-2 fix (2026-08-01, per VERIFY_R34 FAIL items): the
  embedded candidate now matches the binary's zero-slot handling — `id==0 &&
  filter!=0` → weight(filter) as a SINGLE slot (no read-ahead / no pairing
  with the next slot; a trailing zero slot does contribute the filter weight),
  `id==0 && filter==0` → skip (advance one, continue); pass-1 sum accumulates
  int-truncated weights `(float)(int)w` (vcvt.s32.f32 / vcvt.f32.s32 @
  0x8F446F04..08), pass-2 subtract-walk uses raw floats (0x8F446F1C..20).
  Both files (`decision_semantics_r34.candidate.cpp` and this embedded block)
  are in sync.

# MEMORY_INDEXING_R56 — Memory depth / Strikes / Intervals + QuickAttack/Evade indexing + v=7 record-id sets (R5/R6)

GAP-4 step P5. Binary: `reverse/binaries/game_region_runtime.bin`
(base `0x8F057000`, ARM:LE:32:v7, auto-analyzed). Companion to
`TABLE_FORMATS.md` (R1), `ATF_RECORD_858.md` (R2). Docs-only deliverable:
no `engine/` changes, no CMake, no build gate. Candidates live in
`reverse/analysis/memory_indexing_r56.candidate.cpp`.

Addressing-model note (same as TABLE_FORMATS §1): PIC ARM code; string refs
are `ldr rX,[pool]; add rX,pc,rX` with `pc = add_site + 8`. All pool→string
resolutions below were recomputed per add-site against disassembly, not
byte-searched.

---

## 0. TL;DR — per-item verdict

| Item | Answer |
|---|---|
| R5 — Memory ring depth | **Does not exist in the binary.** `<Memory>` carries only `Strikes` (float, → tactic+0x00) and `RoundFactor` (float, → tactic+0x04), both parsed by `FUN_8f4488ac`; **no third attribute is read, no hardcoded depth constant, no ring anywhere**. The per-animation memory is an **unbounded** record vector (find-or-create `FUN_8f4b151c`, doubling growth, no eviction; initial capacity 20 records/slot from `FUN_8f4b0dac`). Effective depth = decay only: `Strikes` as lazy exponential-decay rate + `RoundFactor` as round-end multiplier. The engine's `Memory` attr (`tactic_settings.cpp:247`) correctly parses to 0/unused — the binary has no consumer for it. |
| R5 — Strikes update/reset | `Strikes` is a **rate constant**, not a counter. The strike *accumulators* update on **damage application** (`FUN_8f4aa998` ← `FUN_8f4aafc0`, the hit path): `rec.strike_damage += damage`, `rec.strike_count += 1` for the attacker's **current animation record id**, mirrored into both fighters' memories (victim slot 1, attacker slot 0); and on **"Uninterrupt" animation events** (`FUN_8f4a5478`): `rec.counter += 1`. Reset = (a) lazy exponential decay on every access, `k = 2^(-frames/Strikes)`, `frames` = owner's hits-taken event counter (fighter+0x71c); (b) **round-end** scale of all five accumulators by `RoundFactor` (`FUN_8f4a84e8`, called per fighter from `FUN_8f4275d4`); (c) full zero on fighter reset (`FUN_8f4ac6bc`). |
| R5 — Intervals reset | The tracer's `Intervals:`/`EnemyIntervals:` = the AnimationPlayer's **active-interval pointer vector at +0x08**. It is **cleared and rebuilt** by `FUN_8f47b528` every time the current animation frame crosses the +0x78 window boundary (per-frame update `FUN_8f46046c` → `FUN_8f45f6ac`) and on current-move change: `active ← intervals with max(start, move+0x74) <= frame`, `expiring ← frame-1 == min(end, (u8)move+0x78)`. Records `{+0x04 start, +0x08 end, +0x0c std::string name, +0x18 type}` from moves.xml `<Interval Type Name Start End>`. |
| R6 — QuickAttack[i]/Evade[i] indexing | Index `i` = **XML document order** (1-based in the tracer). The i-th entry is a 0x6c-byte record `{std::string animation_name @+0x00, TacticWeight @+0x0c}` parsed from `<QuickAttackChance Animation="...">` / `<EvadeChance Animation="...">` — i.e. **the index maps to an ANIMATION NAME**, not a table name, not a record id. Per decision: `score[i] = TacticWeight::evaluate(entry[i]+0x0c, ctx)`, `decided[i] = threshold[i] < score[i]` (threshold = persistent per-slot rng roll). A decided entry's *name* is then expanded to a **record-id set** through the v=7 name→ids map (`FUN_8f45bad4`) and each id joins the candidate roulette (QA: decision type 6 = "QuickAttack"). |
| v=7 record-id sets | Per-animation payload lives in the interned animation record's **+0x34 node-table** (parsed by `FUN_8f446528` from the v=7 nested sub-record: name list + u32 data + `{begin,end}` row slices). The probe/QA/Evade lookup goes through a **global {name → ids} map @ `0x8F86F258`** (entries `{cstr name @+0x00, vector<u32> ids @+0x0c}`, gather `FUN_8f45bad4`, merge-unique `FUN_8f45b930`). **No writer to this map exists in the dump** (full xref audit, §5.3) — it stays empty at runtime in this build, so the AnimationFactors probe and the QA/Evade id-expansion are structurally inert (all-zero). Feed events for record-ids: **on damage landed** (not on animation start), see R5-strikes. |

---

## 1. The `<Memory>` parse — only two attributes (R5 depth)

`FUN_8f4488ac` @ `0x8F4488AC` = the per-tactic XML parser (called from
`FUN_8f43f64c`; the 0x648-byte tactic object, ctor `FUN_8f4bd670`).

Ctor prologue (`0x8f4488c0-0x8f4488e0`): `*tactic = 0x41200000; tactic[1] =
0x41200000;` — **both Strikes and RoundFactor default to 10.0f** when no
`<Memory>` element exists.

Memory block (`0x8f448b90-0x8f448c0c`):

```
elem = child(param_2, "Memory");            // pool 0x8f449850 → str 0x8F797A1C, site 0x8f448b9c
if (elem) {                                 // FUN_8f28d3ec
    attr = attr(elem, "Strikes");           // pool 0x8f449854 → str 0x8F797A24, site 0x8f448bc4
    *tactic       = read_float(attr, 0);    // FUN_8f28d0b8 (strtod); str r0,[r3,#0x0] @0x8f448be4
    attr = attr(elem, "RoundFactor");       // pool 0x8f449858 → str 0x8F797A2C, site 0x8f448be8
    tactic[1]     = read_float(attr, 0);    // str r0,[r12,#0x4] @0x8f448c08
}
```

The very next key parsed is `UseDefense` (pool 0x8f44985c @ `0x8f448c0c`).
**Nothing else is read from `<Memory>`** — no `Memory`/depth/`Depth`
attribute, and no constant depth anywhere downstream:

- Records: `AtfMemoryRecord` (0x1c bytes) live in two std::vectors inside the
  fighter-embedded memory struct at **fighter+0x638**
  (ctor `FUN_8f4b0dac` @ `0x8F4B0DAC`: `+0x00 owner`, `+0x04 records_slot1`,
  `+0x10 records_slot0`; each pre-allocated with **0x230 = 20 records**
  initial capacity).
- `FUN_8f4b151c` @ `0x8F4B151C` (find-or-create) grows the vector by
  **doubling** (`newcap = count ? 2*count : 1` records) with **no eviction**
  — candidate in §7.1.
- The decay machinery (§2) is the only "depth" semantics: older events weigh
  exponentially less; `Strikes` is the decay time-constant.

Engine note: `tactic_settings.cpp:247` reads attr `"Memory"` → 0 when absent.
That matches the binary exactly (no consumer). The binary reads Strikes as a
**float** (defaults 10.0f / 0.0f-if-element-present-but-attr-missing); the
engine's `int strikes` is value-compatible for whole-number XML values.

---

## 2. Strikes — update and reset points (R5)

### 2.1 What Strikes/RoundFactor *are* in the binary

Both memory-decay sites dereference `fighter+0x6c` as a `float*` to a
two-float struct `{+0: Strikes, +4: RoundFactor}` — the current tactic
object [+0x6c on the fighter is the tactic pointer; the writer site is
runtime/computed, not pinned statically — marked UNCERTAIN in §9]:

- **Strikes (tactic+0x00, float)** = the lazy decay rate:
  `k = powf(2, -frames / strikes)` (`FUN_8f72ed40` = powf), evaluated in
  `FUN_8f4b173c` / `FUN_8f4b1830` / `FUN_8f4b1914` / `FUN_8f4b1adc`.
  `frames = owner+0x71c - rec->last_frame` and **`owner+0x71c` is a
  hits-taken event counter, not a video-frame counter** — it increments
  once per damage event processed by that fighter (`FUN_8f4aa998` entry,
  `0x8f4aa9b0` ldr / `0x8f4aa9c4` str). So a strike's memory halves every
  `Strikes` hits the owner subsequently takes. Defaults: `DAT_8f4b182c` /
  `DAT_8f4b1910` / `DAT_8f4b1518` / `DAT_8f4b1ad8` / `DAT_8f4b1c68` = 0.0f
  when the AI object (fighter+0x634) or the tactic pointer is absent —
  with rate 0.0f, any `frames ≥ 1` decay zeroes the record.
- **RoundFactor (tactic+0x04, float)** = the round-end multiplier:
  `FUN_8f4a84e8` @ `0x8F4A84E8` multiplies **all five** accumulator floats
  of every record in **both** slot vectors by `*(fighter+0x6c + 4)`
  (default `DAT_8f4b1518` = 0.0f → full wipe when no tactic). Called per
  fighter from the round-end branch of `FUN_8f4275d4` @ `0x8F4275D4`
  (fight state machine; iterates the fighter list `+0x234..+0x238`,
  passing the new round number — also stores `fighter+0x680 = round`,
  sets `root+0x67c = 1`, `fighter+0x674 = 0`, `fighter+0x664 = -1`).

### 2.2 Update (increment) points

1. **On damage application** — `FUN_8f4aafc0` @ `0x8F4AAFC0` (hit path:
   computes the scaled damage vector from attacker `+0x764/+0x768/+0x76c`)
   → `FUN_8f4aa998` @ `0x8F4AA998` (victim-side processing). Tail of
   `FUN_8f4aa998`:
   ```
   FUN_8f4b173c(victim_root + 0x638, 1, anim_id, damage);   // slot 1
   FUN_8f4b173c(attacker_root + 0x638, 0, anim_id, damage); // slot 0
   FUN_8f4b1c6c(attacker_root + 0x638, flag_a, flag_b);     // stat counters
   ```
   `anim_id = *(victim+0x19c) = FUN_8f460278(attacker+0x630)` = the
   **attacker's current animation record pointer** (AnimationPlayer+0x20),
   `damage = *(victim+0x1a4)` (post-clamp). `FUN_8f4b173c` @ `0x8F4B173C`
   (candidate §7.2): decay, then `rec->strike_damage(+0x04) += amount`,
   `rec->strike_count(+0x10) += 1.0f`. So the record-id is fed **on hit
   landed, not on animation start**, and the same record is written into
   **both** fighters' memories (mirrored slots).
2. **On "Uninterrupt" animation event** — `FUN_8f4a5478` @ `0x8F4A5478`
   (animation-event callback; event name at param_2+0x0c, length 0xb ==
   `"Uninterrupt"` @ `0x8F798648` — aliased inside `"SelfUninterrupt"`,
   site `0x8f4a54bc` pool `0x8f4a5574`): `rec->counter(+0x0c) += 1.0f`
   (`FUN_8f4b1830` @ `0x8F4B1830`, candidate §7.2) for the **owning
   fighter's current animation record**, again mirrored into both
   fighters (slot 1 for the param_1+0x1e4-chain root, slot 0 for own
   root).

### 2.3 Reset points

- **Lazy**: every read/write applies the decay above (write-back order in
  `FUN_8f4b173c` decay branch: `+0x08, +0x0c`, then local `+0x04`, `+0x14`,
  local `+0x10`; `rec->last_frame = cur` unconditional).
- **Round end**: `FUN_8f4a84e8` (§2.1).
- **Fighter reset**: `FUN_8f4ac6bc` @ `0x8F4AC6BC` zeroes `fighter+0x71c`
  (and the AI decision sub-state via `FUN_8f4ae8ac(fighter+0x3f8)`);
  `FUN_8f426524` (fight ctor path) also writes `+0x71c`.

### 2.4 Field-level audit: what is actually fed vs read

Complete caller enumeration of the record functions
(`FUN_8f4b151c` ← {`FUN_8f4b173c`, `FUN_8f4b1830`, `FUN_8f4b1914`,
`FUN_8f4b1adc`}; plus round-end scaler `FUN_8f4a84e8` and stat counter
`FUN_8f4b1c6c`):

| field | offset | fed by | read by |
|---|---|---|---|
| `anim_record_id` | +0x00 | find-or-create key | key |
| `strike_damage` | +0x04 | `FUN_8f4b173c` (+= damage, per hit) | **nothing** (decayed only) |
| `damage` ("D") | +0x08 | **nothing** | probe `FUN_8f4b1adc` / `FUN_8f4b1914` |
| `counter` ("C") | +0x0c | `FUN_8f4b1830` (+= 1, per "Uninterrupt") | probe |
| `strike_count` | +0x10 | `FUN_8f4b173c` (+= 1, per hit) | **nothing** (decayed only) |
| `hits` ("H") | +0x14 | **nothing** | probe |
| `last_frame` | +0x18 | all four (stamp) | all four |

**Consequence:** in this build only the **counter** channel is live.
The probe's D and H terms are always 0 (fields exist and are decayed,
never fed) — this pins the open point left in `ATF_RECORD_858.md` §3
("feed site not pinned"): the feed exists (strike_damage/strike_count at
+0x04/+0x10) but does not connect to the probe's D(+0x08)/H(+0x14)
channels in this binary.

`FUN_8f4b1c6c` @ `0x8F4B1C6C` bumps two **int** stat counters on the
attacker's memory struct: `+0x1c` when the victim's flag_a == 0,
`+0x20` when flag_b != 0 (fighter+0x654/+0x658; flag_a = victim+0x1c2
"has registry-slot-5 child", flag_b = victim+0x1c3). Only the
serializer/replay (`FUN_8f425970`, `FUN_8f0d809c`) reads them — they are
round/match statistics [UNCERTAIN NAME: hits_taken / hits_blocked],
not decision inputs.

---

## 3. Intervals / EnemyIntervals — reset points (R5)

### 3.1 What the tracer prints

Tracer = `FUN_8f4556fc` @ `0x8F4556FC` (gated by a debug flag at
`FUN_8f65f45c()+7`). Print order mapped against the string island
`0x8F798090..0x8F798217`:

| pool | string | printed from |
|---|---|---|
| `DAT_8f456118` | `UseDefense: %s / %.4f / %.4f / %.4f` (0x8F7980B5) | AI_state +0xdc/+0xe0/+0xe4 (the three defense scores from `FUN_8f453a94`) |
| `DAT_8f456120` | `UseSafeAttack: %s / %.4f` (0x8F7980FE) | +0x89 flag / +0xe8 |
| `DAT_8f456128` | `TableAttack: %s / %.4f` (0x8F79812E) | +0x8a flag / +0xec |
| `DAT_8f456130` | `DodgeMissiles: %s / %.4f` (0x8F798144) | +0x74 flag / +0x78 |
| `DAT_8f45613c` | `QuickAttack[%d]: %s / %.4f` (0x8F798161) | loop over **+0x8c** score vector, 1-based |
| `DAT_8f456148` | `Evade[%d]: %s / %.4f` (0x8F798184) | loop over **+0x98** score vector, 1-based |
| `DAT_8f456150` | `UseCautiousMovements: %s / %.4f` (0x8F798199) | +0xa4 flag / +0xf0 |
| `DAT_8f456154` | `DistanceError: %.3f` (0x8F7981B4) | +0x5c |
| `DAT_8f456158` | `FrameError: %d` (0x8F7981D1) | +0x60 |
| `DAT_8f4561bc` | `Intervals: %s` (0x8F7981E6) | **self** AnimationPlayer+8 vector, rendered record-by-record |
| `DAT_8f4561e0` | `EnemyIntervals: %s` (0x8F7981F5) | **enemy** AnimationPlayer+8 vector |
| `DAT_8f456164` | `DecisionType: %s` (0x8F798206) | `FUN_8f43f2c8(+0x68)` (the type-name table) |

Interval rendering per record (from `FUN_8f4556fc`): `type = rec->type
(+0x18)`; if the record's `std::string name (+0x0c..+0x14)` is empty →
print the type name; else if name == the type's own name → print name;
else print `type + name`. Type-name tables sit in the pointer island at
`0x8F819250..0x8F819270`; the type strings are
`"Dodge"` (0x8F798630), `"Unstable"` (0x8F798638),
`"SelfUninterrupt"` (0x8F798644), `"Attack"` (0x8F798654),
`"Invulnerable"` (0x8F79865C), `"Invisible"` (0x8F798668) — matching
moves.xml `<Interval Type="Invulnerable" Name="Evade" Start="7" End="22"/>`.

Both `Intervals:` prints read the vector at **AnimationPlayer+0x08**
(`FUN_8f460270` = `x + 8`), a `std::vector<IntervalRec*>`. The
AnimationPlayer is `*(fighter+0x630)` (`FUN_8f4a6dac`), 0x130 bytes
(allocated in `FUN_8f4a903c`). The tracer's self = AI_state+0x24, enemy
= `FUN_8f4a6dac(param_2)`.

### 3.2 The reset/rebuild — `FUN_8f47b528` @ `0x8F47B528`

Rebuilt **every animation frame-window change**:

- per-frame update `FUN_8f46046c` @ `0x8F46046C` increments the frame
  counters and, when the computed frame crosses `+0x78`, calls
  `FUN_8f45f6ac` @ `0x8F45F6AC`;
- `FUN_8f45f6ac` calls `FUN_8f47b528(current_record, frame, &player+8,
  &player+0x14, player+0x4c)`, then fires per-interval events
  (`FUN_8f2578c8(player, 2, rec)` = interval-enter when
  `max(player+0x60, rec->start) == frame`; type-3 events for the
  expiring list);
- `FUN_8f47b528` itself (candidate §7.3) **clears both vectors at entry**
  (`active->end = active->begin`) and refills from the move's interval
  list (`*(record+0x94)+0x28..+0x2c`, per the ASM at
  `0x8f47b57c-0x8f47b5a4`):
  ```
  s = max(rec->start (+0x04), move->f74);        // cpycc
  e = min(rec->end   (+0x08), (u8)move->f78);    // cpyge
  if (s <= frame)      active   += rec   (unless type filtered)
  else if (frame-1==e) expiring += rec   (unless type filtered)
  ```
  The filter (param_5, AnimationPlayer+0x4c) is a `std::map`-shaped
  balanced tree of **excluded type ids** (lower_bound(type) hit → skip;
  walk at `0x8f47b5c0-0x8f47b5f4`).
- Full player reset `FUN_8f46014c` @ `0x8F46014C` (from fighter reset
  `FUN_8f4ac6bc`) clears the event queue and marks no-current-move; the
  +8 vector is then rebuilt on the next `FUN_8f45f6ac`.

move+0x74 = frame base / move+0x78 = window u8 [UNCERTAIN NAME — move
record fields; +0x78 is also the "window" byte read by `FUN_8f46046c`].
Interval record layout: `{+0x00 [UNCERTAIN], +0x04 start, +0x08 end,
+0x0c std::string name, +0x18 type}` — parsed from moves.xml by the
move parser `FUN_8f48e258` (interval fix-up helper `FUN_8f4836c8` walks
`<Intervals>` children and renumbers missing `End` to the running
index+1).

---

## 4. QuickAttack[i] / Evade[i] indexing (R6)

### 4.1 Parse — name-keyed entries in XML order

- `FUN_8f4471dc` @ `0x8F4471DC` parses `<QuickAttacks>` into the vector
  at **tactic+0x1f8**; `FUN_8f447d44` @ `0x8F447D44` parses `<Evades>`
  into **tactic+0x204** (accessors `FUN_8f446b70`/`FUN_8f446b78`).
- Children must be named **`QuickAttackChance`** (0x8F7979FC, site
  `0x8f4471f4-0x8f4471fc` pool `0x8f447d28`) / **`EvadeChance`**
  (0x8F797A10, site `0x8f447d4c-0x8f447d5c` pool `0x8f448890`).
- Each entry = **0x6c bytes**: `{std::string name @+0x00, TacticWeight
  @+0x0c}`; the name is the **`Animation` attribute** — string at
  **0x8F79964C**, an alias into the tail of `"NextAnimation"`
  (0x8F799648), verified per add-site for both parsers
  (`0x0035242C+0x8F447220` and `0x003518C4+0x8F447D88`).
- The weight is parsed from the same element's 15 factor attributes by
  `FUN_8f44c474`; `<AnimationFactors>` children attach per R2.
- Asset ground truth (`assets/files/assets/tacticSettings.xml:49-59`):
  `<QuickAttackChance Animation="Throw" Base="0.05" Limit="0.3">…`,
  `<EvadeChance Animation="Throw" …/>`, and `<Memory Strikes="3"
  RoundFactor="1"/>` (no depth attr, matching §1).

**Answer to R6:** the index `i` is just the ordinal position (XML
document order, 1-based in the tracer). What the i-th entry *selects* is
an **animation name** (`Throw`, `ShortAttack`, …) — which the decision
stage then resolves to a **record-id set** through the v=7 map (§5).

### 4.2 Score slots and the roulette gate

- Score vectors: **AI_state+0x8c** (QA) and **AI_state+0x98** (Evade),
  0xc-byte slots `{float score, float threshold, bool decided}`.
- `FUN_8f45a920` @ `0x8F45A920` (QA) / `FUN_8f45aa2c` @ `0x8F45AA2C`
  (Evade): resize the score vector to the entry count; **new** slots get
  `threshold = FUN_8f264414(rng)` — rolled once, persists across
  decisions.
- `FUN_8f45456c` @ `0x8F45456C` (QA — candidate §7.4) and the inlined
  loop in `FUN_8f45ab38` (Evade): per decision,
  `score[i] = TacticWeight::evaluate(entry[i]+0x0c, ctx)`
  (`FUN_8f44ac78`), `decided[i] = threshold[i] < score[i]`.

### 4.3 Decided entries → candidate animations

In `FUN_8f459b44` @ `0x8F459B44` (the decision tree): for every decided
QA entry, `FUN_8f45bad4(entry.name, &ids)` expands the name to record
ids; each id passing `FUN_8f4561e4` (usability) is pushed as
`{id, FUN_8f47cbe0(id) /*frames*/}` onto the candidate list
(`AI_state+0x31`), and the decision type is set to **6 = "QuickAttack"**
(type-name table, TABLE_FORMATS §2). The Evade loop is analogous (sets
the `AI_state+0x30` "has usable evade" flag via the same
`FUN_8f45bad4` expansion; consumed by the dodge/table stages).

---

## 5. v=7 record-id set semantics (approved extension)

### 5.1 Payload layout (single-weapon `.atf`, version 7)

Loader: `FUN_8f450f90` @ `0x8F450F90`, v==7 branch (`0x8f4511xx`):
`u32 version = 7, cstr weapon_a`, then a **nested archive**
(`FUN_8f21f458`); `FUN_8f21f64c` = sub-entry count; per sub-entry:
`FUN_8f21f670` = the **animation name**; the name is interned
(`FUN_8f45b7e4(name, 1)` → interned animation record, pool @
`0x8F86F24C`, name at record+0x7c) and the binary sub-record is parsed
**into the animation record's +0x34 struct** by `FUN_8f446528`
(miss → parsed into a temp `FUN_8f445b70` and discarded).

Sub-record (per `FUN_8f446528` + `FUN_8f44638C`, TABLE_FORMATS §4):

```
u32 total_size
u32 name_count, cstr names[name_count]     // linked animation names (node list)
u32 data[(total-used)/4]                   // assert count%4==0
rows: data viewed as (count/nodeCount) rows of nodeCount u32s;
      vec<0xc> elems {begin,end} slices into data  (assert count%nodeCount==0)
```

i.e. the v=7 payload attaches, per animation, a **node-table of u32
rows** (the shift-table form) at `record+0x34` = `{rows vec @+0x00,
names vec @+0x0c, data vec @+0x18, owner record @+0x24}`.

### 5.2 The lookup: name → record-id set

The probe and the QA/Evade expansion do **not** read `record+0x34`
directly; they use a global map @ **`0x8F86F258`**:
`std::vector<{const char* name @+0x00, std::vector<uint32_t> ids @+0x0c}>`.

- gather: `FUN_8f45bad4` @ `0x8F45BAD4` (candidate §7.5) — linear scan +
  `strcmp`, on hit `merge_unique` the entry's ids into the out vector,
  return count added; miss → 0.
- batch gather: `FUN_8f45bb58` @ `0x8F45BB58` (vector of names, used by
  the stage-3 table code `FUN_8f457fb8`/`FUN_8f458d60`).
- merge-unique: `FUN_8f45b930` @ `0x8F45B930` (append-if-absent, returns
  added count).
- find-entry: `FUN_8f45b750` @ `0x8F45B750` (used by the XML side
  `FUN_8f43f7b0` to resolve entry references).

### 5.3 The map has NO writer in this build

Xref audit on `0x8F86F258`/`0x8F86F25C`/`0x8F86F260`: **READs** from
`FUN_8f45bad4`, `FUN_8f45bb58`, `FUN_8f45b750`, `FUN_8f45b4e8`,
`FUN_8f45b550`; **WRITEs only** from `FUN_8f45d110` (static init,
zeroes the vector, `__cxa_atexit` registration) and `FUN_8f45b550`
(the GC, truncates it). **No append site exists** — the
`{name → ids}` map stays empty at runtime in this asset build.

Consequences (structural, not speculative):

- `FUN_8f45bad4` always returns 0 → the AnimationFactors probe
  (`FUN_8f4b1adc`) sums zero records → the whole probe term is 0.0f
  regardless of memory contents (consistent with the engine's
  neutral-by-zero default, `tactic_settings.hpp:76-83`).
- The QA/Evade name→ids expansion yields no ids → decided
  QuickAttack/Evade entries add no candidates through that path.
- The v=7 per-animation node tables (`record+0x34`) are still parsed
  and kept (and are used by the shift-table/`NHeel` machinery,
  TABLE_FORMATS §4) — only the name→ids map is unpopulated.
  [UNCERTAIN] whether a sibling build populates the map from these
  tables at load time; in **this** dump there is no such code.

### 5.4 What event feeds a record-id into the decayed sums

**On damage landed** (not on animation start): §2.2 — the attacker's
current animation record id, `rec->strike_damage += damage`,
`rec->strike_count += 1`, victim slot 1 + attacker slot 0; plus
`rec->counter += 1` on **"Uninterrupt"** animation events. The probe
then reads the decayed sums for the ids of the **child name's** set
(`FUN_8f4b1adc` per `<AnimationFactors Animation="…">` child).
Caveat §2.4: the probe's D(+0x08)/H(+0x14) fields are never fed in this
build — only C(+0x0c) is.

---

## 6. Engine-facing notes (no engine changes made)

- `TacticDef::memory` (attr `"Memory"`): keep parsing-to-0 — the binary
  has no consumer (`tactic_settings.cpp:247` is already exact).
- `TacticDef::strikes` is `int` in the engine; the binary stores a
  **float** (default 10.0f; 0.0f if `<Memory>` present but attr
  missing). Whole-number XML values coincide.
- Binary semantics for a future TacticMemory: `strikes` = decay rate in
  **hits-taken units** (owner event counter), `round_factor` =
  multiplicative scale applied to all accumulators at round end.
- Of the five per-record accumulators only `counter` is fed by combat
  events in this build; `AnimationMemorySums.damage/hits` staying 0 is
  behaviorally exact for this dump.
- QuickAttack/Evade: `quick_attack_chances`/`evade_chances` ordering in
  the engine must stay **XML document order** (the binary's index is
  positional); each entry needs a persistent per-slot random threshold
  rolled once when the slot is created (`FUN_8f45a920`/`FUN_8f45aa2c`),
  and `decided = threshold < score` per decision.

---

## 7. Candidates (full file: `reverse/analysis/memory_indexing_r56.candidate.cpp`)

### 7.1 R5 depth — `atf_record_find_or_create` (`FUN_8f4b151c` @ `0x8F4B151C`)

Unbounded find-or-create; doubling growth `count ? 2*count : 1` records,
no eviction — the "no ring depth" proof. See candidate file.

### 7.2 R5 strikes — `atf_memory_feed_strike` (`FUN_8f4b173c` @ `0x8F4B173C`)

(+ sibling `atf_memory_feed_counter` = `FUN_8f4b1830` @ `0x8F4B1830`.)
Decay-then-add with exact write-back order (`+0x08,+0x0c,+0x04,+0x14,+0x10`;
`last_frame` unconditional; add on the decayed base). See candidate file.

### 7.3 R5 intervals — `animplayer_select_intervals` (`FUN_8f47b528` @ `0x8F47B528`)

Clear-both-vectors at entry; `active ← max(start,f74) <= frame`;
`expiring ← frame-1 == min(end,(u8)f78)`; type-filter tree walk
(lower_bound == type → skip). Window/clamp ops verified against the raw
ASM at `0x8f47b57c-0x8f47b5a4` (the decompiler's bool rendering of that
block is mangled; the candidate follows the ASM). See candidate file.

### 7.4 R6 indexing — `tactic_score_quick_attacks` (`FUN_8f45456c` @ `0x8F45456C`)

1:1 positional mapping entry[i] ↔ slot[i]; `decided = threshold < score`.
Threshold roll `FUN_8f45a920` (QA) / `FUN_8f45aa2c` (Evade) documented in
prose. See candidate file.

### 7.5 v=7 — `atf_gather_record_ids` (`FUN_8f45bad4` @ `0x8F45BAD4`)

Map scan + strcmp + merge-unique; map layout `{name @+0x00, ids @+0x0c}`;
empty in this build (§5.3). See candidate file.

---

## 8. Anchors

| what | address |
|---|---|
| per-tactic XML parser (`<Memory>` block) | `FUN_8f4488ac` @ `0x8F4488AC` (sites `0x8f448b90-0x8f448c0c`) |
| `Memory` / `Strikes` / `RoundFactor` strings | `0x8F797A1C` / `0x8F797A24` / `0x8F797A2C` |
| tactic ctor / fighter ctor | `FUN_8f4bd670` / `FUN_8f4acb70` |
| memory struct ctor (fighter+0x638, 20-record prealloc) | `FUN_8f4b0dac` @ `0x8F4B0DAC` |
| **record find-or-create (no depth cap)** | `FUN_8f4b151c` @ `0x8F4B151C` |
| **strike feed (damage event)** | `FUN_8f4b173c` @ `0x8F4B173C` |
| counter feed ("Uninterrupt" event) | `FUN_8f4b1830` @ `0x8F4B1830` |
| feed call sites | `FUN_8f4aa998` @ `0x8F4AA998` (tail) ← `FUN_8f4aafc0` @ `0x8F4AAFC0`; `FUN_8f4a5478` @ `0x8F4A5478` |
| hits-taken event counter (fighter+0x71c) | ++ in `FUN_8f4aa998` (`0x8f4aa9b0/0x8f4aa9c4`); zeroed in `FUN_8f4ac6bc` |
| decay-rate ptr / AI-object gate / frame accessor | `FUN_8f454344` (+0x6c) / `FUN_8f4a7220` (+0x634) / `FUN_8f4a6e44` (+0x71c) |
| **round-end RoundFactor scale** | `FUN_8f4a84e8` @ `0x8F4A84E8` ← `FUN_8f4275d4` @ `0x8F4275D4` |
| stat counters (+0x1c/+0x20) | `FUN_8f4b1c6c` @ `0x8F4B1C6C` |
| **tracer (all decision prints)** | `FUN_8f4556fc` @ `0x8F4556FC` |
| AnimationPlayer / current record / interval vec | `FUN_8f4a6dac` (+0x630) / `FUN_8f460278` (+0x20) / `FUN_8f460270` (+0x08) |
| **interval select/clear** | `FUN_8f47b528` @ `0x8F47B528` ← `FUN_8f45f6ac` ← `FUN_8f46046c` |
| interval type accessor / type strings | `FUN_8f48139c` (+0x18); `0x8F798630..0x8F79868C` |
| interval type-name pointer tables | `0x8F819250..0x8F819270` |
| **QA parser / Evade parser** | `FUN_8f4471dc` @ `0x8F4471DC` / `FUN_8f447d44` @ `0x8F447D44` |
| QA / Evade vectors | tactic+0x1f8 (`FUN_8f446b70`) / tactic+0x204 (`FUN_8f446b78`) |
| `QuickAttackChance` / `EvadeChance` / `Animation` strings | `0x8F7979FC` / `0x8F797A10` / `0x8F79964C` (tail of `"NextAnimation"`) |
| threshold rolls | `FUN_8f45a920` (QA) / `FUN_8f45aa2c` (Evade) |
| **QA score loop** | `FUN_8f45456c` @ `0x8F45456C` (Evade inline in `FUN_8f45ab38`) |
| decided-entry expansion (decision type 6) | `FUN_8f459b44` @ `0x8F459B44` |
| **v=7 loader branch / sub-record parser** | `FUN_8f450f90` (v==7) / `FUN_8f446528` (+ `FUN_8f44638C`) |
| intern pool / anim record name | `0x8F86F24C` (`FUN_8f45b7e4`), name @ record+0x7c |
| **name→ids map (empty in this build)** | `0x8F86F258`; gather `FUN_8f45bad4` @ `0x8F45BAD4`; merge `FUN_8f45b930`; batch `FUN_8f45bb58`; init `FUN_8f45d110`; GC `FUN_8f45b550` |
| probe (consumer of record sums) | `FUN_8f4b1adc` @ `0x8F4B1ADC` (R2) |
| powf / rng / strcmp / memcmp | `FUN_8f72ed40` / `FUN_8f264414` / `FUN_8f73bd3c` / `FUN_8f738f00` |
| float attr reader (strtod) | `FUN_8f28d0b8` |

## 9. Uncertainties / assumptions (all anchored)

1. **fighter+0x6c = tactic pointer** — inferred from two independent
   dereference patterns (`*(f+0x6c)` as the decay rate in
   `FUN_8f4b173c/1830/1914/1adc`; `*(f+0x6c + 4)` as the round-end factor
   in `FUN_8f4a84e8`) matching tactic+0x00/+0x04 = Strikes/RoundFactor.
   The writer site is runtime (not statically pinned). If it instead
   points at a 2-float copy of {Strikes, RoundFactor}, semantics are
   unchanged.
2. **move+0x74 / move+0x78 names** — `frame_base` / `window`
   [UNCERTAIN NAME]: +0x74 is the move's frame base (used in
   `FUN_8f47cbe0`'s frame math), +0x78 a u8 window/length bound.
3. **stat counters** at fighter+0x654/+0x658 (`FUN_8f4b1c6c`): purpose
   [UNCERTAIN NAME] — only serializer/replay reads them; not decision
   inputs.
4. **`Intervals` active-list semantics**: per the ASM, the active list
   holds intervals whose clamped start ≤ frame (no upper-bound check in
   `FUN_8f47b528`; enter/leave events are what gate behavior via
   `FUN_8f45f6ac`). Reported as-is from the ASM at
   `0x8f47b57c-0x8f47b5a4`; the decompiler's rendering of that block was
   mangled and was not trusted.
5. **Map-empty claim (§5.3)**: based on a complete xref audit of
   `0x8F86F258..0x8F86F260` — WRITEs exist only in the static initializer
   and the GC. A writer using un-resolved computed addressing cannot be
   excluded to 100%, but the pool-relative addressing used by every
   reader would equally apply to a writer, and none was found.
6. Tracer fmt-string addresses are cited to ±2 chars (space-padded
   island); assignment of fmt pools to print lines is structural
   (arg counts + vector offsets), not bytewise.

## 10. Verifier record (RE-Verification contract)

No `@re-verifier` task/subagent channel is exposed in this session (the
orchestrator runs it afterwards), so the deterministic self-check with
`cpp-metrics` (the verifier's own metric) stands in:

| function | decompiled calls/branches | candidate calls/branches | delta accounted for |
|---|---|---|---|
| `FUN_8f4b151c` / `atf_record_find_or_create` | 3 / 14 | 9 / 4 | realloc copy loop + alloc/free paths elided as `std::vector` ops (candidate) vs inlined (binary); zero-init order & doubling policy matched line-by-line |
| `FUN_8f4b173c` / `atf_memory_feed_strike` | 6 / 2 | 4 / 3 | accessors (`FUN_8f4a6e44/0x8f4a7220/0x8f454344`) inlined into `atf_memory_decay_rate`; decay write-back order & defaults matched |
| `FUN_8f47b528` / `animplayer_select_intervals` | 10 / 30 | 9 / 10 | two `std::vector` push-realloc paths (≈2×{alloc,copy,free}+size checks) elided; window/filter logic follows the ASM |
| `FUN_8f45456c` / `tactic_score_quick_attacks` | 2 / 3 | 3 / 1 | `while(true){…if(…)break}` + count guard ≡ the candidate's `for`; slot writes identical |
| `FUN_8f45bad4` / `atf_gather_record_ids` | 4 / 3 | 4 / 6 | `FUN_8f45b930` merge inlined in the candidate (its 2 loops + 2 ifs show as branches) |

**Verdict status: NEEDS_HUMAN** (same standing as ATF_RECORD_858.md —
docs-only deliverable; orchestrator dispatches @re-verifier on the five
labeled candidates: `0x8F4B151C`, `0x8F4B173C`, `0x8F47B528`,
`0x8F45456C`, `0x8F45BAD4`).

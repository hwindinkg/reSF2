# Perk binary survey — perks.xml parser & apply path in the relocated dump (Step 4)

Scope: phase-4 PLAN step 4. Static Ghidra analysis only (no Frida, no engine changes).
Companion to `PERK_SURVEY.md` (Step 3, assets). Every address below is in the
RELOCATED image `reverse/binaries/game_region_runtime.bin`, base `0x8F057000`,
ARM:LE:32:v7, fully auto-analyzed (16420 functions). `game+0xNNNNNN` =
`0x8F057000 + 0xNNNNNN` (e.g. game+0x6275F4 = `0x8F67E5F4`).

Method notes honored: `get_xrefs_to` for string anchors; whole-image LDR/ADD
PC-relative scan (same pattern as `resolve_dats.py`) for strings/globals Ghidra
did not xref; **PC+8 trap** avoided everywhere (PC belongs to the consuming
`ADD`, not the `LDR` — verified per-site against disassembly, e.g. the
ModAttributes vtable computation below).

---

## 0. TL;DR — fork verdict

**ZERO Case A mechanisms. EVERY perk attribute mechanism is Case B.**

- Perk attribute writes (`ModAttributes`) DO land in the same name→int map at
  `model+0x1C4` that `Model::getParameter` reads — but only **transiently**:
  added when a trigger fires (event + conditions), **subtracted back** by the
  same executor when the mod is reaped (Frames countdown / ClearMods /
  EndStanceClear). No persistent equip/attach-time attribute contribution
  exists anywhere in the perk system.
- Perk-level `<Set>` values are not attribute writes at all — they go into a
  per-perk-instance **parameter map** (`perk+0xBC`) and are only substituted
  into `_Param` expressions at trigger-fire time.
- Therefore: no candidate C++ for `AttributeSet` wiring, no @re-verifier round
  needed. Step 5 becomes pure Case-B anchoring (5.1 scope); Step 6 is a
  documented N/A.

---

## 1. Parser location (deliverable 1)

### 1.0 Chain overview

```
boot state machine  FUN_8f619944 (game+0x5BA944, case 4: asset load)
  └─ FUN_8f653448 (game+0x5FC448)  asset manifest registrar — registers
     'assets/perks.xml' (string load @ game+0x5F9A54) + 'assets/perks.xml.hash'
     (@ game+0x5F9FC0) + 'assets/perks_result.xml(.hash)' among ~25 assets
  └─ FUN_8f653fb0 (game+0x5FCFB0)  perks.xml LOAD DRIVER          [§1.1]
       ├─ FUN_8f6a434c (game+0x64D34C)  template merge + ID assign [§1.2]
       └─ FUN_8f6679dc (game+0x6109DC)  per-perk parse + register  [§1.3]
            └─ FUN_8f68b280 (game+0x634280)  PerkObject::parse     [§1.4]
                 └─ FUN_8f685bf0 (game+0x62EBF0)  trigger alloc
                      └─ FUN_8f6a33f8 (game+0x64C3F8)  trigger parse [§1.5]
                         ├─ FUN_8f699be0 (game+0x642BE0)  event factory    [§1.6]
                         ├─ FUN_8f695758 (game+0x63E758)  condition factory
                         └─ FUN_8f68e9fc (game+0x6379FC)  action factory   [§1.7]
```

Attach/instantiation (runtime, per model):

```
FUN_8f2a8f20 (game+0x251F20)  resolve XML perk reference (item/stage node)
  └─ FUN_8f68bd98 (game+0x634D98) → FUN_8f68ba34 (game+0x634A34)  [§1.8]
       perk INSTANCE factory: merge Set overrides → re-parse → subscribe
FUN_8f6b5d1c (game+0x65ED1C)  bind Set-override expressions to instance [§1.8]
```

### 1.1 Load driver `FUN_8f653fb0` (game+0x5FCFB0)

Decompiled shape (trimmed):

```c
void loadPerks(void) {
    XmlDoc doc;                       // FUN_8f28e928
    char *path = "assets/perks.xml";  // 0x8F7A4054, string load @ game+0x5FCFF4
    if (resolveTemplatesAndIds(&doc, path) == 0) {   // FUN_8f6a434c
        log("ERROR: loadPerks - wrong file");        // 0x8F7A459C
        return;
    }
    node = doc.find("Perks");         // 0x8F78E2C8
    if (node valid) {
        lazy_init_registry();         // guard-init, registry object @ 0x8F868C54
        FUN_8f6679dc(registry=0x8F868C54, node);      // parse + register all perks
    }
}
```

Xref chain: `'assets/perks.xml'` @ `0x8F7A4054` ← LDR/ADD @ game+0x5FCFF4
(found by whole-image PC-relative scan; `get_xrefs_to` on the string returns
nothing — Ghidra did not build xrefs for these loads).

### 1.2 Template merge / ID assignment `FUN_8f6a434c` (game+0x64D34C)

Loads the file, walks `<Perks><Perk>` nodes, reads `Template` and assigns `ID`.
String vocabulary (resolve_dats): `'Perks'` (0x8F78E2C8), `'Perk'` (0x8F7A1288),
`'Template'` (0x8F78E2DC), `'ID'` (0x8F790188). Builds pipe-joined (`0x7C`)
template-chain strings in memory. **Template inheritance is resolved HERE, at
load time** (parent triggers referenced via the chain), while **`<Set>`
overrides from list.xml / CharacterProgress `UpgradeLevel` / forge merge at
INSTANCE-CREATION time** (§1.8). Answers survey Q4.

### 1.3 Perk registrar `FUN_8f6679dc` (game+0x6109DC)

Iterates `<Perk>` children of the root, calls `PerkObject::parse`
(FUN_8f68b280) for each and inserts into the **global perk registry** —
a name→PerkObject map at **`0x8F868C54`**. Registry lookup helper:
`FUN_8f65def8` (game+0x606EF8), which has exactly 13 callers (all perk
reference-resolution sites; listed in §5).

### 1.4 `PerkObject::parse` `FUN_8f68b280` (game+0x634280)

Reads `Name`/`Alias`/`Description`/`Hidden`/image attrs into the object;
splits template-chain string by `'|'` (0x7C). Then the key loop:

```c
// for each name in the global known-parameter registry (FUN_8f669994):
if (node has attr name) {
    value = atoi(attr);
    FUN_8f2a6604(perk + 0xBC, name, &value, 0);   // -> perk's OWN param map
}
```

**Perk-level `<Set>` params land in a per-perk map at `perk+0xBC` — NOT in
any model attribute map.** `<RatingEvaluation>` rows go to a vector at
`perk+0x104` (0x18-byte records; computed strength cached at `perk+0xB8`).
Children named `Trigger` (7 chars) go to the trigger parser.

PerkObject layout (partial): +0x1C hidden flag, +0x30 vector<string> template
names, +0x60 name, +0xA8 int, +0xBC **Set param map**, +0xCC trigger vector,
+0xD8 xml node ref, +0xE0/+0xE4 bound context pointers, +0x104 rating vector.

### 1.5 Trigger parser `FUN_8f6a33f8` (game+0x64C3F8)

Reads trigger `Name`, then three child lists by element name (strings
`'Events'` 0x8F7926C4, `'Conditions'` 0x8F7926CC, `'Actions'` 0x8F7926D8) via
the three factories. Trigger object = 0x34 bytes: +0x00 owner perk, +0x04
name string, +0x10 events vector, +0x1C conditions vector, +0x28 actions
vector. Allocation wrapper: FUN_8f685bf0 (game+0x62EBF0).

### 1.6 Event factory `FUN_8f699be0` (game+0x642BE0)

String→class dispatch over all 11 event names: `RoundStageStart` 0x8F799164,
`HitPostCrit` 0x8F7A6D94, `HitPreCrit` 0x8F7A6D88, `PostHit` 0x8F7A6DA0,
`AnimationStart` 0x8F79902C, `AnimationEnd` 0x8F79903C, `ModExpires`
0x8F799088, `EveryFrame` 0x8F799068, `AreaEnter` 0x8F7A6DA8, `AreaExit`
0x8F7A6DB4, `PerkEventMagicCharged` 0x8F7A6DC0. A second, smaller mapper
FUN_8f69ca74 (game+0x645A74) maps style/stage names to small ints
(+(0x10) = 1..13). RTTI classes `16PerkEventPreCrit` … `21PerkEventMagicCharged`
at 0x8F7A6C8C..0x8F7A6D84.

### 1.7 Action factory `FUN_8f68e9fc` (game+0x6379FC) — the critical dispatch

Iterates `<Actions>` children, compares element names, constructs a
`PerkAction*` object per child (base ctor FUN_8f68e03c), sets
`action+0xC = owner`, `action+0x14 = trigger`, then calls the class's
**vtable[2] = parse-from-XML**. Comparison set (name → class, object size,
vtable — vtables resolved from literal pools with the PC+8 rule applied):

| XML element | len | class (RTTI name @ 0x8F7A6xxx) | obj size | vtable (address point) |
|---|---|---|---|---|
| `ModIcon` | 7 | PerkActionShowIcon | 100 (0x64) | 0x8F85B0A8-based |
| `ModAttributes` | 13 | **PerkActionSetAttributes** ("23PerkActionSetAttributes" @ 0x8F7A67CC) | 0x50 | **0x8F85B170** (typeinfo 0x8F85B230) |
| `ModFlag` | 7 | PerkActionFlag | 0x44 | 0x8F85B060-region |
| `ClearMods` | 9 | PerkActionClearAction | 0x5C | — |
| `DisableInterval` | 15 | PerkActionDisableInterval | 0x5C | — |
| `SetHit` | 6 | PerkActionSetHit | 0x58 | — |
| `AddBullets` | 10 | PerkActionAddBullets | 0x100 | — |
| `AddMagicCharge` | 14 | PerkActionAddMagicCharge | 0xF4 | — |
| `SetModFrames` | 12 | PerkActionSetModFrames ("22PerkActionSetModFrames" @ 0x8F7A6830) | 0x10C | 0x8F85B180-region |
| `ApplyModEffect` | 14 | PerkActionSetModEffect | 0x54 | — |
| `ModHealthChange` | 15 | (Lifesteal family) | 0x48 | — |
| `Provoke` | 7 | PerkActionProvoke | 0x50 | — |
| `SetTactic` | 9 | PerkActionSetTactics | 0x50 | — |
| `SetVariable`/`ModVariable`/`SetCooldown` | 11 | PerkActionSetVariable / PerkActionVariable / PerkActionSetCooldown | 0xF4/0x25C/0x54 | — |
| (13/20/27-char names) | 13/20/27 | ChangeImpulse / ChangeHitEffectScale / ChangeAdditionalDamageValue | 0x50/0x48/0x48 | — |

**NOTE (survey Q2): `Set` (3 chars) is NOT in the factory's comparison set** —
no length-3 comparison exists. An action-level `<Set>` (the single occurrence,
PERK_FULL_POWER) creates NO action object in this factory; it is either
silently ignored or handled by a path not seen in this pass (candidate: the
perk-level Set machinery re-applied at fire time). Flagged as an open
sub-question; every OTHER attribute-writing action is `ModAttributes`.

RTTI inventory (string scan, length-prefixed names): 23 `PerkAction*`
(0x8F7A66B4..0x8F7A68C8), 20 `PerkCondition*` (0x8F7A6A6C..0x8F7A6C28),
13 `PerkEvent*` (0x8F7A6C8C..0x8F7A6D84), `10PerkObject`, `10PerksStage`.

`PerkActionSetAttributes` vtable slice (GCC layout, 0x18-byte slices):
`[offset_to_top=0 @ 0x8F85B168][typeinfo=0x8F85B230 @ 0x8F85B16C]` then at the
stored address point 0x8F85B170: `[0]=0x8F68E710 (D1 dtor)`,
`[1]=0x8F68F964 (D0 dtor)`, `[2]=0x8F691F44 (parse)` — **only 3 virtuals;
execution is NOT virtual** (central switch, §2.2).

### 1.8 Attach / instance factory

`FUN_8f68ba34` (game+0x634A34, via FUN_8f68bd98): looks up the referenced
perk in the registry (FUN_8f65def8 on the reference's name string), opens the
template's stored XML node (`perk+0xD8`), **merges the two override
containers** (param_2/param_3 = e.g. list.xml item `<Set>` block and
CharacterProgress `UpgradeLevel` `<Set>` block) into a working document,
allocates a fresh 0x110-byte instance (ctor FUN_8f686004) and **re-parses the
merged document through `PerkObject::parse`** → template/Set merge happens at
RUNTIME per instance. Sets `instance+0xDC = 1`, copies `instance+0x24`, calls
FUN_8f684d90 (final init/subscribe). `FUN_8f6b5d1c` (game+0x65ED1C) then binds
each Set-override expression to the instance context (`perk+0xE0/+0xE4`).
**No model attribute writes in the attach path** (verified: no
FUN_8f2a6938/FUN_8f2a6664 call in either function).

---

## 2. Apply path (deliverable 2) — where `ModAttributes` lands

### 2.1 The attribute map (read side, pre-existing knowledge confirmed)

`Model::getParameter` = `FUN_8f67e5f4` (game+0x6275F4), 100 bytes:

```c
float getParameter(Model *self, string *name) {      // self+0x1C4 = the map
    int value = 0;
    if (map_lookup(self + 0x1c4, name, &value, /*mod_aware*/1, /*secondary*/0))
        return (float)value;                          // vcvt.f32.s32
    warn("Parameter \"%s\" not found!");
    return -1e35f;                                    // literal @ 0x8F67E658
}
```

Map structure (`FUN_8f2a6664` game+0x25F664 = insert-or-find): vector of
bucket*; bucket = 0x88 bytes {+0=8, +4=0x10, then 16 entries of 8 bytes
{key, int value}}. Public writer `setParameter` = `FUN_8f2a6938`
(game+0x25F938):

```c
void setParameter(map, name, &value, useSecondary) {
    if (useSecondary && isModdable(name))   // FUN_8f65f2ac, registry below
        *insert(map, secondaryName(name)) = value;   // FUN_8f2a6330/8f62a34c
    else
        *insert(map, name) = value;                  // FUN_8f2a6664
}
```

The **moddable-attribute registry**: `FUN_8f65f2ac` (game+0x6082AC) walks a
global vector at `0x8F8780A8 + 0x274` of 12-byte records
`{name, secondaryName, float curveParam}` (secondary name at record+4 via
FUN_8f62a34c). **The registry vector is EMPTY (begin=end=0) in this dump** —
taken outside a fight; mechanism unaffected.

`map_lookup` = `FUN_8f2a5f5c` (game+0x25EF5C): with mod_aware=1 and a
moddable name, the returned value is NOT the raw map entry:

```c
result = ratingCurve(registry_rec, (float)base_value, A, B, C);  // FUN_8f62a354
result += map[secondaryName];                                    // the MOD slot
```

with `ratingCurve` (game+0x5E3354):
`x = (base - p3*p4)/p5; return (int)(rec->curveParam * (x >= 0 ? (2.0 - powf(2,-x)) : powf(2,x)))`
— the same 2^x family as the damage formula; belongs to the
RatingEvaluation/resistance system (5.1 scope).

The model carries TWO maps: **+0x1B4 = base/template attributes,
+0x1C4 = runtime attributes** (the one getParameter and the damage formula
read). Warrior init `FUN_8f576960` (game+0x51F960) copies +0x1B4 → +0x1C4 for
every registered attribute name at model creation.

### 2.2 The central action executor `FUN_8f6a9164` (game+0x642164)

Called from the three event-dispatch paths (FUN_8f6a9a38 game+0x642A38,
FUN_8f6aa1fc, FUN_8f6aa2e0) with a vector of 0x38-byte trigger-instance
records. Per record:

```c
if (record->action->isMod /*+0x34*/) snapshot = heap_copy(record);
switch (record->action->type /*+0x10*/) {
    case 1:  execModFlag(fight, rec, /*apply*/0);       break; // FUN_8f6a77e0
    case 3:  execSetAttributes(fight, rec, /*apply*/0); break; // FUN_8f6a6c70
    case 4:  FUN_8f6a8e9c(...);                           break; // ClearMods
    case 6..0x16: ... one helper per action class ...     break;
    case 0xf: execModIcon(fight, rec, /*apply*/0);      break; // FUN_8f6a520c
    case 0x14: FUN_8f4a8ac0(rec->model, action->+0x44, +0x48, +0x4c); // SetVariable-ish
}
if (record->action->isMod) {            // track for reaping
    fight->activeMods.push_back(snapshot);        // fight+0x10 vector
    fight->modNames.push_back(&action->+0x1C);    // fight+0x1C vector
    if (framesList(action) nonempty) FUN_8f6a8a88(snapshot);
}
...
// tail: reap every mod whose expired flag byte is set:
for (m in fight->activeMods)
    while (*(char*)*m) reapMod(fight);            // FUN_8f6aac7c
FUN_8f6a5118(fight);
```

Action type IDs: SetAttributes = **3** (assigned in its parse, `+0x10 = 3`;
`isMod` byte at +0x34 set to 1 — so **every ModAttributes instance is tracked
as a mod for reaping**).

### 2.3 `PerkActionSetAttributes` executor `FUN_8f6a6c70` (game+0x64FC70) — THE apply

```c
void execSetAttributes(Fight *fight, ModRec *rec, int unapply) {
    int sign = (unapply == 0) ? 1 : -1;          // apply +1 / unapply -1
    PerkActionSetAttributes *action = rec->action;   // rec+0xC
    for (entry in action->entries /* vector at action+0x44 */) {
        name  = entry->name;                        // e.g. "DamageFactor"
        value = atoi(expr) if plain int
                else evalExpression(entry->expr);   // FUN_8f265af0, context =
                                                    // action->+0xC -> +0xE0/+0xE4
        // scratch map (fresh local map, FUN_8f2a62e4):
        setParameter(scratch, name, value, 0);
        modVal   = map_lookup(scratch, name, 0, /*mod_aware*/1, /*secondary*/0);
        modelMap = *(rec->modelOwner /*rec+8*/ + 500) + 0x1C4;   // THE model map
        cur      = map_lookup(modelMap, name, 0, /*mod_aware*/0, /*secondary*/1);
        sum      = modVal * sign + cur;
        setParameter(modelMap, name, sum, /*useSecondary*/1);
        if (name == "DamageFactor" && !unapply       // 0x8F797B9C, len 12
            && FUN_8f4a6e4c(*(fight->hitCtx /*+0xE8*/))) {
            currentHit = *(fight + 0x20);
            FUN_8f6a69c4(rec->trigger + 0x1D0);      // refresh in-flight hit
        }
    }
}
```

Read this carefully — it answers survey Q1 completely:

- **Write target**: the SAME name→int map at `model+0x1C4` that getParameter
  reads — but through the **secondary (mod) key** for moddable attribute
  names (`useSecondary=1`), i.e. perks never touch the BASE value; they
  accumulate in the mod slot that `map_lookup(...,mod_aware=1,...)` folds in
  as `curve(base) + mod`.
- **Semantics**: ADD on apply, **SUBTRACT on unapply** (`sum = modVal*sign +
  cur`). Values are plain ints (`5850`, `-100000`, `300000`); expression
  values are truncated via `VectorFloatToSigned`.
- **DamageFactor special case**: on apply it also pokes the in-flight hit
  (`fight+0x20`) via the trigger's +0x1D0 — the `Frames="1"` mechanics: the
  mod is visible to the hit being evaluated right now.

### 2.4 Reaper / unapply `FUN_8f6aac7c` (game+0x643C7C)

Same action-type switch, but `case 3: execSetAttributes(fight, rec, /*unapply*/1)`
→ `sign=-1` → subtract. Also un-applies ModFlag (case 1), ModIcon (case 0xf,
removes icon via FUN_8f41c4a0), SetVariable family (cases 0x14/0x15/0x16) and
re-registers bookkeeping into the global mod manager (FUN_8f41c58c;
name-keyed mod store at manager+0x1C8, vectors at +0x1E8).

### 2.5 Frames / reaping mechanism (survey Q3)

- `Frames` values arrive as XML attrs on the mod actions (parse stores them;
  `PerkActionSetModFrames` exists to re-set frames on named mods at runtime).
- Expiry drives the unapply call above: the executor's tail loop reaps mods
  whose flag byte is set; `ModExpires` is a first-class event
  (`19PerkEventModExpires`); `EndStanceClear` (root template, 126 perks)
  clears all mods at `RoundStageStart(EndStance)`; `ClearMods` (action type 4)
  clears by name. 16 ms frames per GAP-1.
- Exact countdown field/tick site not fully traced in this pass — anchored
  for 5.1: candidates `rec+0x14` (zeroed at unapply entry), the
  `FUN_8f6a8a88` frames-list registration in the executor, and the
  `manager+0x1E8` re-registration vector in the reaper.

### 2.6 How DamageFactor is consumed (survey Q6)

`Model::getTotalDamage` = `FUN_8f4a97b4` (game+0x4527B4) — matches
`damage_formula.hpp`; the relevant reads:

```c
attackerMap    = *(enemy? + 500) + 0x1C4;              // model attribute map
local          = 0;
map_lookup(attackerMap, "DamageFactor", &local, 1, 0); // game+0x452808;
fVar9 = powf(2.0f, (float)local * *weightPtr);         // 2^(DF*w) base term
fVar9 = fVar9 * f3_defFactor * f10_atkFactor           // f2 / f1 selectors
              * f4_attrDiff                            // (atk-def)/10 helper
              * (*(float*)(hit + 0x48) + *(float*)(enemy + 0x774));
fVar9 = max(fVar9, MIN);
fVar9 = fVar9 * crit * enemy->+0x678 * enemy->+0x6AC;
```

`"DamageFactor"` is a lazily-initialized global std::string at `0x8F87840C`
(guard `0x8F878408`; heap text outside the dump; length 12 — matches).
**So the perk DamageFactor mod reaches the formula through the attribute
map's mod slot — exactly the `base_attribute` term — not through hit[0x48].**
The mod-aware read (`FUN_8f2a5f5c(...,1,0)`) folds `curve(base) + mod` at
formula-evaluation time. The special-case in the executor (§2.3) refreshes
the in-flight hit for `Frames="1"` mods.

### 2.7 Boss / XML-unreferenced perk attach (survey Q5)

The 13 XML-unreferenced perk names (`PERK_BOSS_HERMIT_MAGIC`,
`PERK_ITEM_SPECIAL_CRITICAL_CHANCE`, `PERK_ITEM_SPECIAL_SHOCK`,
`PERK_WEAPON_BLOCK_BREAKER`, `PERK_GREATER_GOOD_SHARPENING`,
`PERK_SPHERE_COOLDOWN`, `PERK_BOSS_BUTCHER_BLEEDING`, `PERK_BOSS_WASP_*`,
`PERK_BOSS_WIDOW_*`, `PERK_BOSS_SHOGUN_FRENZY`) have **ZERO occurrences as
binary strings** (`search_strings` 0 hits for the whole set; the full
`PERK_` prefix scan finds only `PERK_DOUBLE_SWEEP`). Conclusion: **no
hardcoded attach-by-name for these perks**; any attach must be numeric (the
`ID` assigned at parse time, §1.2), via the `DefaultPerks` player parameter
(`DefaultPerksAspect` string @ 0x8F796960, consumed by the expression
evaluator and by warrior init FUN_8f576960), or the perks are dead content in
this build. The only hardcoded perk-name reference in the entire binary is
`PERK_DOUBLE_SWEEP` @ 0x8F79CBCC, read by `FUN_8f526bac` (game+0x4DB6AC) and
`FUN_8f5297fc` (game+0x4E27FC) — move-unlock checks, not attribute paths.

---

## 3. Fork verdict per mechanism (deliverable 3)

| # | Mechanism | Verdict | Evidence |
|---|---|---|---|
| 1 | `ModAttributes` (61 XML uses incl. all `DamageFactor`/`RegenerationRate`/`Lifesteal`/`CriticalChance`/`CriticalDamage`/`MagicDamageRecharge`/`MagicPainRecharge`/`ShockHeadHitChance` writes) | **Case B** | Executor FUN_8f6a6c70 runs ONLY from the trigger-fire switch (FUN_8f6a9164 case 3), itself called only from event dispatch (FUN_8f6a9a38 & 2 siblings) after event+condition gating; value subtracted at reap (FUN_8f6aac7c unapply). Writes model+0x1C4 secondary/mod slot, never the base. 54/61 XML uses carry `Frames`, 44 of those `Frames="1"` (current hit only). |
| 2 | Perk-level `<Set>` params (127 uses) | **Case B (not an attribute write)** | Parse writes the perk's own param map perk+0xBC (FUN_8f68b280 loop), substituted into `_Param` expressions at fire time (§1.4, §1.8). Never touches model+0x1C4. |
| 3 | Action-level `<Set>` (1 use, PERK_FULL_POWER) | **Case B / unresolved sub-path** | `Set` is absent from the action factory comparison set (§1.7) — no PerkAction created; either dead XML or a perk-level-Set re-application at fire time. Not a persistent write either way. |
| 4 | `RatingEvaluation` / `Rating` (39 rows) | **Case B (read-only consumer)** | Reads enemy `EnchantmentResistance` through the mod-aware attribute lookup and the 2^x rating curve (FUN_8f6838f8 → FUN_8f62a354); computes enchantment strength; does not persist model attributes. |
| 5 | 6 bare move-gate perks (DOUBLE_SWEEP etc.) | **No attribute path** | Hardcoded name check only (PERK_DOUBLE_SWEEP xrefs §2.7); moves.xml Locks concept. |
| 6 | 13 XML-unreferenced perks (boss/special) | **Case B + no hardcoded attach** | All have trigger blocks in XML (event-gated by construction); zero binary name refs (§2.7). |
| 7 | `AttributesRule` stage rules | **Out of perk scope** | Separate system: FUN_8f4be540 (game+0x467540), string 'AttributesRule::initRule - wrong appliance - %i'. Stage rules, not perks. |
| 8 | Enemy/warrior attribute seeding (stage `WeaponDamage=...`, `<AttributesAlign>`, StartingAttributes) | **Out of perk scope (already planned)** | model+0x1B4→+0x1C4 copy at warrior init (FUN_8f576960); delta application FUN_8f67eb84 (game+0x627B84) / FUN_8f67ecfc; these seed the baseline AttributeSet the plan already covers. |

**Case A count: 0.** Nothing in the perk system writes the model+0x1C4 map
persistently at equip/apply time with no Frames and no Condition gating.

### Deliverable 4 — candidate C++

None. Per the task brief: "If everything is Case B, say so explicitly with
proof — that is a valid and valuable outcome." Proof is §2.2–§2.4 (executor
only reachable from the trigger-fire switch; subtract-on-reap twin;
isMod tracking) plus §3's per-mechanism table. No @re-verifier round is
required — there is no candidate to verify.

---

## 4. Answers to PERK_SURVEY §7's seven questions

1. **Where does ModAttributes write?** Into `model+0x1C4` (the map
   getParameter reads) but on the **secondary/mod key** for moddable names —
   `curve(base) + mod` is folded at read time. **Add on apply, subtract on
   unapply** (`modVal*sign + cur`, sign = +1/−1). Values are plain ints,
   expression results truncated to int. (§2.1, §2.3)
2. **Is action-`<Set>` the same path as `ModAttributes`?** No — `Set` is not
   in the action factory's comparison set at all; `ModAttributes` maps to
   `PerkActionSetAttributes` (type 3). Action-`<Set>` creates no action
   object; open sub-question (§1.7, §3 row 3).
3. **Frames counting/reaping**: 16 ms frames; countdown tracked per
   mod-instance (fight+0x10 active-mods vector, frames-list registration
   FUN_8f6a8a88); reaping = the unapply call (subtract) from the executor
   tail loop + `ModExpires` event + `ClearMods`/`EndStanceClear` clears.
   Exact tick site anchored for 5.1 (§2.5).
4. **Template merge**: `Template=` inheritance resolved at LOAD
   (FUN_8f6a434c, pipe-joined chains + ID assignment); item/progress/forge
   `<Set>` overrides merged at INSTANCE-CREATION (runtime) by re-parsing the
   merged document (FUN_8f68ba34) and binding expressions (FUN_8f6b5d1c). (§1.2, §1.8)
5. **13 XML-unreferenced perks**: no hardcoded attach-by-name (zero binary
   string refs). Numeric-ID attach, `DefaultPerks` parameter, or dead
   content. Only hardcoded perk name in the binary: `PERK_DOUBLE_SWEEP`. (§2.7)
6. **RatingEvaluation consumption**: reads `EnchantmentResistance` via the
   mod-aware lookup (sentinel possible in principle; the alignment-table
   helper path defaults to 0.0 — the rating path uses getParameter proper
   (FUN_8f67e5f4) so a missing attribute yields −1e35f through the curve;
   registered attributes are always present for fight models). `?Aspect[…]`
   expressions evaluate through the same expression evaluator the executor
   uses (FUN_8f265af0 family). (§2.1, §2.6, §3 row 4)
7. **SetVariable vs ModVariable**: distinct classes
   (`21PerkActionSetVariable` vs `18PerkActionVariable`, action types 0x14
   vs 0x15/0x16 region); both appear in the reaper's unapply switch, so the
   Set/Mod lifetime distinction is honored binary-side; detailed lifetime
   semantics anchored for 5.1. (§1.7, §2.4)

---

## 5. Reproduction queries (plan verify gate)

| Finding | Query |
|---|---|
| perks.xml in manifest | `resolve_dats.py 0x8F653448` → 'assets/perks.xml' @ game+0x5F9A54 |
| Load driver | decompile `0x8F653FB0`; strings 'assets/perks.xml', 'Perks', 'ERROR: loadPerks - wrong file' via `resolve_dats.py 0x8F653FB0` |
| Registry global | `0x8F868C54` = `0x8f654168 + *(0x8f654274)` (LDR/ADD at game+0x5FD158/0x5FD160) |
| Template/ID pass | decompile `0x8F6A434C`; strings Perks/Perk/Template/ID via resolve_dats |
| Trigger parser | decompile `0x8F6A33F8`; strings Events/Conditions/Actions |
| Event factory | `resolve_dats.py 0x8F699BE0` → all 11 event names |
| Action factory | decompile `0x8F68E9FC`; `resolve_dats.py 0x8F68E9FC --raw` → names + vtable targets |
| SetAttributes vtable | `*(0x8f68f8d0)=0x1CC3D0`; `0x8f68ecd8+0x1CC3D0=0x8F85B0A8; +0xC8 = 0x8F85B170`; typeinfo 0x8F85B230 → name "23PerkActionSetAttributes" @ 0x8F7A67CC |
| Executor | decompile `0x8F6A6C70`; callers = FUN_8f6a9164 (apply) + FUN_8f6aac7c (unapply) |
| Apply/reap switch | decompile `0x8F6A9164` (case 3, apply=0) and `0x8F6AAC7C` (case 3, unapply=1) |
| getParameter | decompile `0x8F67E5F4`; sentinel literal −1e35f @ `0x8F67E658` |
| Map read/write | `FUN_8f2a5f5c` (lookup), `FUN_8f2a6664` (insert), `FUN_8f2a6938` (setParameter) |
| Moddable registry | disassemble `0x8F65F2AC`: vector at `0x8F8780A8+0x274` (empty in dump) |
| DamageFactor in formula | decompile `0x8F4A97B4`; map_lookup @ game+0x452808; lazy name string object @ `0x8F87840C` |
| No hardcoded boss perks | `search_strings("PERK_")` → only `PERK_DOUBLE_SWEEP` (0x8F79CBCC) |

## 6. Anchors for 5.1 (Case B scope — for the Step 5 TODO/PORT_GAPS note)

- Trigger system: parser chain §1 (FUN_8f653fb0 → … → factories).
- Event dispatch: FUN_8f6a9a38 (game+0x642A38), FUN_8f6aa1fc, FUN_8f6aa2e0.
- Action executor switch: FUN_8f6a9164 (game+0x642164); unapply twin
  FUN_8f6aac7c (game+0x643C7C).
- ModAttributes apply: FUN_8f6a6c70 (game+0x64FC70); PerkActionSetAttributes
  vtable 0x8F85B170; parse FUN_8f691f44 (game+0x63AF44).
- Condition factory: FUN_8f695758 (game+0x63E758); 20 PerkCondition* RTTI @
  0x8F7A6A6C..0x8F7A6C28.
- Mod manager: FUN_8f41c58c (global), mod store manager+0x1C8 / +0x1E8.
- Expression evaluator: FUN_8f265AF0 (eval), FUN_8f26C914 (parser ctor),
  FUN_8f26CC44 (int fast-path); expression strings per PORT_GAPS.md L466-477.
- Rating system: FUN_8f6838f8 (game+0x62C8F8), curve FUN_8f62a354
  (game+0x5E3354), rating records on perk+0x104.
- Attribute maps: model+0x1B4 (base) / +0x1C4 (runtime); moddable registry
  0x8F8780A8+0x274; attribute-system global 0x8F8780A8.

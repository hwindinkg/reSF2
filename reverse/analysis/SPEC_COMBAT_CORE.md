# SPEC_COMBAT_CORE — magic charge / facing / duck

Deep-RE specification of three core combat mechanics of the ORIGINAL game code
(Shadow Fight 2, s3e/Marmalade ARM engine), derived from the relocated runtime
dump `reverse/binaries/game_region_runtime.bin` (image base `0x8F057000`,
ARM:LE:32:v7, Ghidra, 16422 functions).

Method notes (apply to every address below):

- The dump is relocated, so branch targets/imports resolve; the ARM PC+8
  literal-pool pattern `ldr rX,[lit]; add rX,pc,rX` was resolved manually
  (string targets recomputed as `(add_pc+8) + value(lit)`) everywhere a
  "`DAT_xxx + -0x70...`" expression appeared in decompilation.
- Game data used as ground truth: `reverse/data/animations/moves.xml`,
  `reverse/data/list.xml` (same files the binary parses at boot — the
  moves.xml parser is `FUN_8f48f530`, see Q1.6).
- "Fighter" = the `Fight::createFighter` object (`FUN_8f41e6a8`, 0x77C bytes,
  ctor `FUN_8f4acb70`, vtable base `0x8F842AD0`, vptr = base+8).
  "Fight" = the fight-session object (methods `0x8F41xxxx..0x8F42xxxx`).

---

## Q1 — MAGIC CHARGE

### Verified semantics

1. **State (per fighter)**
   - `Fighter+0x6EC` — magic charge accumulator, **float**, normalised 0..1.
   - `Fighter+0x6F0` — magic availability **count** (0/1). 0 = not charged,
     1 = charged, magic button active.
   - `Fighter+0x684` — count of negative magic deltas (consumes), stat counter.
   - `Fighter+0x6F4` — ranged analogue of +0x6F0; `Fighter+0x6EC/0x6F0` pair is
     read by Fight::init into `Fight+0x1F8` (count) / `Fight+0x1FC` (charge).
   - Evidence: `FUN_8f4a80d8` disassembly (below), `FUN_8f4a80d0` =
     `return fighter+0x6F0`, `FUN_8f4a8050` = `return fighter+0x6EC`,
     `Fight::init` `FUN_8f41f250`.

2. **What increments it** — every **landed attack interval hit** on either
   fighter (attacker AND victim both gain charge, in two calls inside
   `Fight::applyHit`). Increment is **damage-scaled**, **multiplicative** in
   three `powf(2.0f, x)` recharge factors, then **clamped to [0,1]**.
   - **Suppressed** when the ATTACKING move carries `NoMagicRecharge="1"`
     (MoveDef `+0x148` flag) — moves.xml: `RangedMissile`, `MagicMissile`,
     `MagicAcidCloud`, `RaidMissile` (and their derived `MagicMissileFly`
     family) are the real cases; the magic projectile/weapon bodies do not
     charge the caster.
   - **Suppressed** when the charging fighter's count is already 1
     (full bar never overcharges).
   - `moves.xml` `Template MagicPlayer` adds condition
     `<ModExists Name="Concussion" Not="1"/>` — a concussed fighter cannot
     CAST, but the charge logic itself has no other gate.

3. **Increment amount (formula)** — `FUN_8f4a9660`:
   ```
   if (recipient.magicCount != 0) return;
   if (recipient.magicCount == 0)
       recipient.charge += clamp(
           pow2_factor(attacker, blockedFlag) *
           powf(2.0f, rechargeAttr(recipient)) *
           pow2_factor(victim, criticalFlag) *
           damage, 0.0f, 1.0f);
   FUN_8f4a80d8(recipient);
   ```
   - `damage` = hit interval's `Damage Value` (`interval+0x48`, e.g.
     `Value="1"` for punches, `Value="12"` for heavy attacks in moves.xml).
   - `pow2_factor(fighter, flag)` = `flag ? powf(2.0f, attrBase*mods) : 1.0f`
     (`FUN_8f4a94f0` / `FUN_8f4a95a8`, attributes from the two global
     recharge records `FUN_8f65fc28`/`FUN_8f65fcc0`).
   - `rechargeAttr` = `PainRecharge`-family attribute of the charging fighter
     for the attacker role / `DamageRecharge`-family for the victim role
     (`FUN_8f65f0f0` → `FUN_8f65f000` "PainRecharge",
     `FUN_8f65f1e8` → `FUN_8f65f0f8` "DamageRecharge"), evaluated with the
     model's mod attributes (`model+0x1C4`, perk `ModAttributes` slot).
   - Attribute strings (verified block at `0x8F7A4BE4`): `InitialCharge`,
     `PainRecharge`, `DamageRecharge`, plus `Antilimit`, `DoublingRange`,
     `Hits`; lookups log `"InitialCharge/PainRecharge/DamageRecharge for
     Magic not found"` when absent (returns 0).
   - **[UNCERTAIN]** exact attribution of the two block/critical flags
     (`hit+0x1C2` = victim in `Type Block` interval, `hit+0x1C3` = critical):
     the factor direction ("factor applied when flag set") is verified in
     `FUN_8f4a94f0/95a8`; the gameplay meaning of `+0x1C2` was inferred from
     `Fighter::onHit` (`FUN_8f4aa998`: `+0x1C2 = interval-type-5 found on the
     VICTIM's current move`). Verify against live play if exact per-case
     charge rates matter.

4. **Activation threshold** — `charge >= 1.0f`:
   `FUN_8f4a80d8` (disassembly verified):
   ```
   r5 = fighter+0x6EC;  vldr s15,[r5]; vcmpe.f32 s15, 1.0
   if (s15 >= 1.0) {
       count = fighter+0x6F0 + 1;
       if (count > 1) log("Wrong magic count %d", count);
       fighter+0x6F0 = count;            // capped to 1 later
       *(float*)fighter+0x6EC = 0.0f;    // bar reset
   }
   if (count <= 1) {
       if (model+0x8D == 0) return;      // no magic equipped -> silent
       if (count == 0) sendCommand(fighter, 0xC, {0x10, charge, -1, -1});
   } else {
       fighter+0x6F0 = 1;
       if (model+0x8D == 0) return;
       sendCommand(fighter, 0xC, {0x10, 1.0f, -1, -1});   // "magic ready"
   }
   ```
   - `model` = `fighter+0x1F4`, byte `model+0x8D` = "has magic equipped"
     gate; `sendCommand` = `FUN_8f2578c8(fighter, 0xC, args)` → the model
     exposes charge value; the fight HUD binds `MagicCharge` (expression
     variable, factory `FUN_8f6879c0`, getter `FUN_8f4a80d0` = count at
     `+0x6F0`) and the progress bar to this state. **[UNCERTAIN]** which
     binding drives the bar fill (count vs charge float) — both exist.
   - The `MagicCharged` perk event fires when the charge completes
     (event-name mapper `FUN_8f69ca74`; see also `PERK_BINARY_SURVEY.md`,
     `PerkEventMagicCharged` `0x8F7A6DC0`, event fired from 2 sites).

5. **Round start** — `Fight::init` → per fighter `FUN_8f41c8e4(fight, 1|2)`:
   ```
   fighter = (id==1) ? fight+0x6E0 : fight+0x6E4;
   fighter+0x6F0 = 0;                                   // count = 0
   charge = InitialChargeAttr(fighter);                 // FUN_8f65eff8
   fighter+0x6EC = clamp(charge, 0.0f, 1.0f);           // FUN_8f4a80d8
   ```
   (InitialCharge is the pre-filled portion of the bar; `FUN_8f65eff8`
   matches the 13-char name `InitialCharge`, error string at `0x8F7A4BF4`.)

6. **Magic button / cast** — the magic button is an input **key** of type
   `"Magic"` (`moves.xml` key vocabulary: `Magic` next to `Kick`/`Punch`/
   `Ranged`/`Super`), `PressType="Tap"`. Press selects the equipped magic's
   player move, `Type="ATTACK"`, `Priority="110"`,
   `Template="1key|MagicPlayer"` (`MagicBombPlayer`…`MagicDarkWavePlayer`,
   moves.xml lines ≈29297+); the move's `TacticWeapon`/Locks filter by the
   equipped magic item `SubType` (`FireBall`, `EnergyBall`, … from
   `list.xml` `Type="Magic" SubType=… MagicDamage=…`). Casting consumes the
   charge via command type 0 with delta −1 (`Fighter::applyCommand`
   `FUN_8f4a83b8`: type at `cmd+0x1C` (0=magic, 1=ranged), value at
   `cmd+0x20`; negative delta counted at `+0x684`; >1 logged as
   `"Wrong magic count %d"`).

7. **Item/attribute plumbing (xref chain)**
   - `list.xml` magic items carry `MagicDamage` (+`RaidChargeDamage` for
     RaidConsumable) — parsed as the magic bullet damage
     (`Fight::fillMagicAndMissilesBuffer`, strings at `0x8F7967B0/E8`).
   - Model attribute slots registered by `FUN_8f661390`: `model+0xD8` =
     weapon, `+0xDC` = armor, `+0xE0` = helmet, `+0xE4` = ranged, `+0xE8` =
     magic; `Fighter::updateAttributes` (`FUN_8f4aa424`) feeds them to the
     attribute engine every frame.
   - `MagicRechargeRate` string `0x8F7A62E4` → config writer `FUN_8f67e970`
     (writes `+0x34/+0x38/+0x3C`); `MagicCharge` string `0x8F7A6544` →
     binding factory `FUN_8f6879c0` and expression engine `FUN_8f695758`.
   - `RechargeMagicEachRound` rule (`FUN_8f4cfd48`, string `0x8F79AD0C`) is
     the fight-rule counterpart that re-fills charge at each round start.

### Key function addresses

| role | address | notes |
|---|---|---|
| charge state recompute / threshold | `0x8F4A80D8` | vcmpe 1.0, count cap, cmd 0xC |
| charge increment | `0x8F4A9660` | formula, clamp, full-skip |
| per-hit charge call site | `0x8F420F9C` | `Fight::applyHit`, both fighters |
| NoMagicRecharge read gate | `0x8F47D378` | `MoveDef+0x148`, `x=0 → charge` |
| NoMagicRecharge parse/write | `0x8F48F530` / `0x8F47D380` | moves.xml MoveDef builder |
| InitialCharge at round start | `0x8F41C8E4` (caller `FUN_8f41f250`) | clamp 0..1 |
| attribute getters | `0x8F65EFF8` InitialCharge, `0x8F65F000` PainRecharge, `0x8F65F0F8` DamageRecharge | “for Magic not found” errors |
| flat attribute getters | `0x8F661E10` PainRecharge, `0x8F661F04` DamageRecharge | |
| powf factors | `0x8F4A94F0`, `0x8F4A95A8`, `0x8F72ED40` (powf) | |
| magic count getter / applyCommand | `0x8F4A80D0`, `0x8F4A83B8` | count, consume |
| MagicCharged event map | `0x8F69CA74` | name→id |
| hit pipeline | `Fight::update 0x8F4278C8` → `processHits 0x8F41A8B0` → `Fighter::tryHit 0x8F4AB2C4` → `Fighter::onHit 0x8F4AA998` → `applyHit 0x8F420F9C` | |

### Candidate C++ fragment (label: `magic-charge@0x8F4A9660/0x8F4A80D8/0x8F420F9C`)

```cpp
// --- per-landed-hit (Fight::applyHit, 0x8F420F9C) ---
// hit: {attacker, victim, interval}; interval->damageValue = float @ +0x48
// gate: attacker current MoveDef NoMagicRecharge flag @ MoveDef+0x148
if (!moveDefNoMagicRecharge(attacker->currentMove)) {          // 0x8F47D378
    float dmg = interval->damageValue;                          // interval+0x48
    addMagicCharge(attacker, dmg, victim, hit->blocked, hit->critical, 0);
    addMagicCharge(victim,   dmg, attacker, hit->blocked, hit->critical, 1);
}

// --- charge accumulation (0x8F4A9660) ---
// role 0: charging the attacker; role 1: charging the victim
void addMagicCharge(Fighter* self, float damage, Fighter* other,
                    bool blocked, bool critical, int role) {
    if (self->magicCount(+0x6F0) != 0) return;      // full bar: no overcharge
    float f2 = pow2Factor(self, blocked);           // 0x8F4A94F0, flag->2^(attr*mod) else 1.0
    float f3 = pow2Factor(other, critical);         // 0x8F4A95A8
    float f1 = (role == 0) ? painRecharge(self)     // 0x8F65F000 w/ mods (+0x1C4)
                           : damageRecharge(self);  // 0x8F65F0F8 w/ mods
    if (self->magicCount(+0x6F0) == 0) {
        float c = self->magicCharge(+0x6EC) + f2 * powf(2.0f, f1) * f3 * damage;
        self->magicCharge(+0x6EC) = clamp(c, 0.0f, 1.0f);
    }
    recomputeMagicState(self);                      // 0x8F4A80D8
}

// --- threshold crossing (0x8F4A80D8, asm-verified) ---
void recomputeMagicState(Fighter* f) {
    if (f->magicCharge(+0x6EC) >= 1.0f) {
        int count = f->magicCount(+0x6F0) + 1;
        if (count > 1) log("Wrong magic count %d", count);
        f->magicCount(+0x6F0) = count;
        f->magicCharge(+0x6EC) = 0.0f;
    }
    int count = f->magicCount(+0x6F0);
    if (count > 1) count = f->magicCount(+0x6F0) = 1;
    if (*(byte*)(f->model(+0x1F4) + 0x8D) == 0) return;   // no magic equipped
    sendCommand(f, 0xC, count == 0 ? f->magicCharge(+0x6EC) : 1.0f);
}

// --- round start (Fight::init, 0x8F41C8E4) ---
fighter->magicCount(+0x6F0) = 0;
fighter->magicCharge(+0x6EC) = clamp(initialChargeAttr(fighter), 0.0f, 1.0f); // 0x8F65EFF8
recomputeMagicState(fighter);
```

---

## Q2 — FACING LOGIC

### Verified semantics

The fighter's facing is an explicit **mirror state** on the model
(`ConditionModelMirrored::isEqual` `FUN_8f473458` reads it; `ModelMirrored`
condition id from factory `FUN_8f488c18`). The facing is changed **only by
the `SetDirection` action that runs when a move starts** (plus the round-start
stance); it is **never** updated by movement/position code:

- **Turn to face the enemy** (`<SetDirection><From Player="Me" Object="Nodes"
  Part="NPivot"/><To Player="Enemy" Object="Nodes" Part="NPivot"/></SetDirection>`):
  - `StageStance` (round start) — fighters always open facing each other.
  - `Controlled` template — the base of ALL player-controlled moves:
    `1key/2key/3key/Central/Back/Up/UpBack/UpForward/Forward/DownForward/
    Down/DownBack/Throw` all derive from `Controlled`, hence **every attack,
    block, jump, magic cast and dodge turns the fighter to face the enemy at
    move start** (`MagicXXXPlayer` = `1key|MagicPlayer` → `Controlled` ✓).
  - `SetDirectionIdleStance` / `SetDirectionTransition` — returning to idle
    re-faces the enemy (after an enemy crosses behind, e.g. jump-over).
- **Cases that must NOT turn** (the "turns away" bug surface):
  - `StepForward` / `StepBack` (templates `Step|Forward` / `Step|Back|Retreat`)
    have **no** `SetDirection` — walking forward or backward keeps facing;
    `Retreat` is an empty template. Backward walk = retreat while still
    facing the enemy.
  - `Hit` reaction template has `<SetDirection><Impulse Reverse="1"/>…` —
    only the knockback **impulse** direction is reversed (away from the
    attacker); the model mirror is NOT flipped by hit reactions.
  - `GetUp` (`From Me.NNeck → To Me.NPivot`) — keep own facing on get-up.
  - Ranged/magic **missiles** have their own
    `<SetDirection><From Parent Wall Back → To Parent Wall Front>` — the
    missile flies in the caster's facing direction.
- In the binary the direction/alignment primitives are:
  `SetDirection` string `0x8F7994C0` (template merge `FUN_8f483a3c`, move
  builder `FUN_8f48e258`); direction state slots on the fighter
  `+0x8C/+0x90/+0x94` read by `ConditionDirection::isEqual` `FUN_8f46af7c`
  (type 1→+0x8C, 2→+0x90, 3→+0x94); `ConditionModelMirrored` `0x8F798E2C`;
  node-alignment helpers `FUN_8f08e93c/8f08e970` (vector math).

**[UNCERTAIN]** the exact runtime address of the SetDirection action executor
(the action object is constructed in the move builder `FUN_8f48e258` and
dispatched via its vtable at move start; the vtable dispatch site was not
pinned). The mirror-write itself was not located in the Model class; the
condition readers and the data contract above are verified.

### Candidate C++ fragment (label: `facing@moves.xml-SetDirection + 0x8F46AF7C/0x8F473458`)

```cpp
// At move start, after the move-selection picks the new MoveDef
// (deferred one frame: Fighter+0x218 request applied in Fighter::update 0x8F4AC4B4):
void applySetDirection(Fighter* self, DirectionAction* a) {   // SetDirection action
    if (a->hasImpulse) {                       // <Impulse Reverse="1"/> (Hit template)
        self->impulseDir = a->reverse ? awayFrom(self->enemy) : toward(self->enemy);
        return;                                // mirror NOT changed
    }
    // From Me.<node> -> To Enemy.<node>: face the enemy pivot
    Vec3 from = nodeWorldPos(self->model, a->fromNode);        // e.g. NPivot
    Vec3 to   = nodeWorldPos(self->enemy->model, a->toNode);
    self->model->setMirror(from.x <= to.x);    // forward (+X) points at enemy
}

// Guard: only these move families carry SetDirection (data-verified, moves.xml):
//   StageStance, SetDirectionIdleStance, SetDirectionTransition, Controlled(*all 1key/2key/3key moves)
// NO SetDirection on: Step/ForwardStep/BackStep(Retreat), Hit (impulse-only), GetUp
```

---

## Q3 — DUCK REPEAT (spam-S must not restart the duck animation)

### Verified semantics

`Move Name="Duck" Template="1key|Down|NotTitan" Type="MOVE"
FileName="duck.bin"` — selection is gated by THREE stacked guards, all
data-verified in moves.xml and enforced by the move-selection engine:

1. **Fresh tap only** — `Keys <Key Type="Down" PressType="Tap"/>`: only a
   key *edge* (fresh press) can select Duck. A held `S` is `Hold`, never a
   Tap, so holding S after the duck started does nothing (this alone kills
   the "held-key restarts every frame" bug).
2. **SemiUninterrupt window** — `1key` template adds condition
   `<CurrentInterval Name="SemiUninterrupt" Not="1"/>`; Duck's own intervals:
   `SemiUninterrupt End="4"` (frames 0–4), `Uninterrupt Start=5 End=11`,
   `Block/Throwable` from frame 12. While the fighter is inside the current
   move's SemiUninterrupt interval, no 1key move can start.
3. **Anti-restart (the decisive guard)** — `Controlled` template condition:
   ```
   <Operator Type="And" Not="1">
     <CurrentAnimation Name="$Move"/>        <!-- $Move = the move being considered (Duck) -->
     <CurrentInterval Name="SemiUninterrupt"/>
   </Operator>
   ```
   i.e. the candidate move is rejected when
   `currentAnimation == "Duck" && currentInterval == "SemiUninterrupt"` —
   spamming S during the first 4 frames of the duck animation cannot
   re-trigger it. A re-tap after the SemiUninterrupt window legitimately
   restarts duck (original behavior: releasing and re-pressing down mid-duck
   re-duck).
4. Deferred move switch: the selected move is queued at `Fighter+0x218` and
   applied at the **start of the next frame** in `Fighter::update`
   (`FUN_8f4ac4b4`: `if (+0x218) { applyMove…; +0x218 = 0; }`), so rapid
   same-frame re-selections collapse into one switch.
5. Movement analogue (`Step` template): steps are guarded by
   `Not(SelfUninterrupt AND (Step|DoubleStep))` — the same pattern for walk.

The move-selection/condition machinery in the binary: condition factories
`FUN_8f488c18` (name→id table incl. `Direction` `0x8F799544`,
`ModelMirrored` `0x8F799598`), condition evaluators
`FUN_8f46af7c` (Direction), `FUN_8f473458` (ModelMirrored);
move parser `FUN_8f48f530` (MoveDef: priorities, keys, intervals).
**[UNCERTAIN]** the exact address of the per-frame Keys/interval condition
evaluation loop (the `Keys Tap/Hold` checker object was not pinned by name);
the three guard clauses above are data-verified and their condition
classes exist in the factory.

### Candidate C++ fragment (label: `duck-guard@moves.xml-Controlled/1key + 0x8F4AC4B4`)

```cpp
// Move-selection eligibility for a candidate move M (player side).
// Data-verified clauses; order as authored in moves.xml.
bool canSelectMove(Fighter* f, MoveDef* m) {
    if (!keysTapOrHold(f, m->keys)) return false;      // Tap=edge only (Duck: Down/Tap)
    if (currentInterval(f, "SemiUninterrupt") && m->needs1key) return false; // 1key guard
    // Controlled anti-restart:  NOT( currentAnim==M && SemiUninterrupt )
    if (m->isControlled && f->currentAnim == m->name
        && currentInterval(f, "SemiUninterrupt")) return false;
    if (m->isStep && currentInterval(f, "SelfUninterrupt")
        && (f->currentAnim == "Step" || f->currentAnim == "DoubleStep")) return false;
    return true;
}
// Duck intervals (frames): SemiUninterrupt 0..4, Uninterrupt 5..11, Block/Throwable 12..
```

---

## [UNCERTAIN] summary

| item | why | impact |
|---|---|---|
| block/critical flag direction in the charge factors | flag semantics inferred from `FUN_8f4aa998` (+0x1C2) | exact charge rates per blocked/critical hit |
| SetDirection runtime executor address | action dispatched via vtable built in `FUN_8f48e258` | facing flip site not byte-pinned |
| Keys (Tap/Hold) evaluator address | condition object not name-anchored | duck fresh-press guard site not byte-pinned |
| which HUD binding drives the bar fill | two bindings exist (`MagicCharge`→count, `+0x6EC`→float) | UI-only |
| base source of PainRecharge/DamageRecharge values | list.xml magic items carry only `MagicDamage`; recharge attrs come from item records/perk `ModAttributes` (`MagicDamageRecharge`/`MagicPainRecharge`, see `PERK_SURVEY.md`) | magnitude of `2^(rate)` factors |

# core/scene — fight physics + damage (Phase 3.3)

Ported from `reference/www/sf2.502f0946.js`. This file documents the JS
study for the fight-physics path: hit detection, the damage formula, HP
application, knockback and bounds. All line numbers are 1-based against
the current build (they match the JS_MAP.md line numbers).

## 1. Hit detection

**When it runs:** every fight frame, `ca.ia` (L388-390) -> `ca.Enb` (L390)
alternates the fighters and calls `wd.tKa` (L499) -> `wd.HZa` (L499) ->
`Fu.ia` (the melee-attack controller, class `Cl` L566). `Fu.ia` requires an
active Attack interval (`da.yD(4)`) and runs the capsule-vs-capsule test
against the target's collidable body.

**Which shapes collide:** the *attacker's* AttackingParts edges vs the
*target's* collidable edges. Both are **capsules** (swept spheres between
two bone endpoints):

- The attacker's list is `Te.GY` (L566): `Te.xqb` (L566) resolves each
  Attack-interval `<AttackingParts><Edge Name=..>` against the attacker's
  own model capsule table (`Dl.RAa` L573, the `Nl.all` list built by
  `Yc.jjb` L572). Edge names ending in `_1`/`_2` are mirrored (`rw`).
- The target's list is `oa.Nl.oI` (L566): the collidable (`Collisible="1"`)
  edges of the target's physics body. Each is a `yu` (L803): `Ula`/`Pda`
  (endpoint positions, refreshed each frame by `Lnb` L803), `gb` (radius),
  `Eda` (the 2D line), `HC` (BodyPart), `Xi` (Defense).
- The test itself is `Bz` (L12): **2D swept-capsule vs capsule on the XZ
  plane** (y is the up axis and is dropped — SF2 fighters fight on the
  ground plane). `Cl.W1a` (L566) calls `Bz(attackerP1, attackerP2,
  attackerRadius+margin, targetP1, targetP2, targetRadius+margin, ...)`.
  `Bz` computes the line equations of both segments, rejects when both
  endpoints of one segment are on the same side beyond the summed radius,
  handles the crossing case (segments intersect) and the endpoint-vs-
  capsule cases (`Ls` L12). On hit `zXa` (L566) records the hit capsule
  (`strike.Py` = attacker capsule, `strike.KD` = target capsule) and the
  closest points (`strike.n$`/`strike.o$`).

**AttackingParts -> model edges:** the Attack interval's `<Edge Name>`
list names edges of the attacker's own skeleton model (e.g. HighPunch:
`EForearm_2`, `EHand_2`, `EFingers_2`). These map 1:1 to the attacker's
`yu` capsules (the `Nl.all` entries with the same names). The **target's**
capsules are *its own* model edges (the skeleton's `EHead`, `EChest`,
`EStomach`, `EThigh_*`, ... with `Collisible="1"`).

## 2. The damage formula — `wd.bCa(a, b, c, d, e)` (L513-514)

`a` = the Attack interval (`Ul`), `b` = block flag, `c` = crit flag,
`d` = the target capsule (`KD`), `e` = the interval's attack-attribute
list (`QX`). `this` = the defender; `f = this.jb` = the **attacker** (the
fighter's `jb` is `HB[0]` = the first enemy, L502 `Zka`).

```
bCa(a,b,c,d,e){
  f = this.jb;                          // the ATTACKER
  // RaidChargeDamage debug guard (skipped)
  d = wd.LAa(a, b, d);                  // (1) the defense attribute name
  h = v.ACa();                          // (2) DamageFactor Base = 0.0001
  k.G = min(attacker.DamageFactor, v.E9a()=20000);
  h = 2^(h * k.G);                      // (3) DamageFactor multiplier
  b = this.kea(b);                      // (4) block  : 2^(defender.BlockDamageFactor * 0.0001)
  c = f.qea(c);                         // (5) crit   : 2^(attacker.CriticalDamage * 0.0001)
  g = v.iea(f.qb, f.params, this.params, a.SZ, d);   // (6) balance
  g = (a.Xb + f.Ly) * g * b * c * h * f.params.UZ;   // (7) base * all
  g = max(g, 0);
  g = f.params.c2a(d, g);               // (8) armor: defense=="Fists" -> *M_
  g *= a.Cea(f.qb ? 1 : 2).bp;          // (9) interval per-side mult (default 1)
  g *= f.dta;                           // (10) fighter dta (1)
  return g *= f.so;                     // (11) fighter so (1)
}
```

Terms in order:

1. **Defense attr** (`wd.LAa` L536): `a.KP` (the interval's `<Defense>`
   names) non-empty -> `KP[0]`; else if blocked -> `v.pYa` =
   `BlockDefense Attribute` = **"BodyDefense"**; else the target capsule's
   `Xi` (`BodyDefense`/`HeadDefense`); else `v.lNa` (SlowMotion Defense,
   empty).
2. **DamageFactor base** (`v.ACa` = `v.Ypa`) = `0.0001` (internal_settings
   `<DamageFactor Base="0.0001">`).
3. **DamageFactor multiplier** = `2^(0.0001 * min(attacker.DamageFactor,
   20000))` — `v.E9a` = `v.Zpa` = 20000 (MaxValue).
4. **Block multiplier** (`kea` L512) = `2^(defender.BlockDamageFactor *
   0.0001)` when blocked, else 1. `v.VY` = `<BlockDamageFactor Base=
   "0.0001" Attribute="BlockDamageFactor">`.
5. **Crit multiplier** (`qea` L512) = `2^(attacker.CriticalDamage *
   0.0001)` when crit, else 1. `v.HZ` = `<CriticalHit><Damage Base=
   "0.0001" Attribute="CriticalDamage">`.
6. **Balance** (`v.iea` L1205 -> `pAa` L1204 -> `l5a` L1206): for each
   attack attribute `(name, shift)` in `a.SZ` (from the sub-`<Damage
   Type=.. Shift=..>`), `B = attacker.attr(name) + shift`, `A = align(
   name)` (the `AlignTargetAttributes` list, empty by default); `e` = the
   defender's defense attr value. The Align delta loop (`Ci.a5a` L800 over
   the attacker's `IY` AttributesAlign list) is empty for the default
   warriors, so `W = (B - e)`. `t = max(t, W)`. Result = `2^(t / v.BP)`
   with `v.BP` = `<DamageDoublingRange Value="10">`.
7. **Base × multipliers**: `(a.Xb + f.Ly) * balance * block * crit *
   damageFactor * attacker.UZ`. `a.Xb` = the interval's `<Damage
   Value=..>` (0.11 for HighPunch); `f.Ly` = the attacker's extra base
   damage (0); `UZ` = TrustFailed modifier (clamped 0..1, default 1).
8. **Armor** (`xc.c2a` L820): if the defense attr == `"Fists"` -> `g *= M_`
   (the attacker's `FistsDamageMod`, default 1).
9. **Interval per-side multiplier** (`Ul.Cea` L774 + `Vm` L776): `bp`
   defaults to 1; a fight rule (`bn` L436) can set it.
10/11. **Fighter scalars** `dta` and `so` (both default 1; set by
    `zla`/`TT` for summons/modifiers).

### Hand computation for the demo (zero-attr fighters)

- HighPunch: `a.Xb = 0.11`, `a.SZ = [("UnarmedDamage", -10)]`.
- Attacker (player): `UnarmedDamage = 0` (Fists/Body items), all other
  attrs 0. Defender (punchbag): `BodyDefense = 0`.
- `d = "BodyDefense"` (the target capsule's Defense).
- `h = 2^(0.0001 * 0) = 1`; `b = 1`; `c = 1`.
- balance: `B = 0 + (-10) = -10`, `e = 0`, `t = -10` ->
  `2^(-10/10) = 0.5`.
- `g = 0.11 * 0.5 * 1 * 1 * 1 * 1 = 0.055`; armor/Vm/dta/so = 1.
- **Final: 0.055.**

## 3. Hit application — `ca.Cgb(a)` (L394-397)

`a` = the event (`Ri`): `a.model` = the defender, `a.Pd` = the attacker,
`a.data` = the Attack interval. The defender's `wd.strike` (L509) filled
`a.model.Bb` (the `pu` hit record L558) and computed `Bb.bR` before Cgb
runs.

```
Cgb(a){
  b = a.model.Bb;  c = a.data;
  c.a3 && (b.se = false);              // NoCritical disables crit
  this.Sba(a.model, b, 7);             // dispatch the hit event
  d = a.model.parameters.gd;           // current HP
  d < b.bR ? (b.Zi = d + 0.01, b.Iza = true)   // lethal check: leave 0.01
          : (b.Iza = false);
  // FightNone clears crit/shock/head-hit
  // shock/head-hit disarms (skipped)
  b.ep = !this.Dga;
  // impulse-to-body push (the knockback, see §4)
  b.block || (a.model.hT(5), this.Dga = true);  // play hit reaction
  // HUD update (skipped)
  a.model.ws && (b.Zi = 0);            // invulnerable -> no damage
  a.model.$db(b.Zi, ...);              // queue the damage number
  this.aM(a.model, -b.Zi);             // HP -= Zi  (xc.du clamps [0, Zn])
  // attribute-based bonus damage, magic, KO check
}
```

- **Lethal floor:** `hp < bR -> Zi = hp + 0.01` (the fighter survives with
  0.01 HP; KO is only when `gd <= 0` after the decrement — `xc.Jfa` L817).
- **HP decrement chain:** `Cgb` -> `aM(model, -Zi)` (L391) -> `wd.Laa(-Zi)`
  (L522) -> `xc.Laa` (L816) -> `xc.du(gd - Zi)` (L816) -> `parameters.gd`
  clamped to `[0, Zn]`.
- The native port (`apply_damage`) keeps the lethal check + the 0.01 floor
  + the HP decrement.

## 4. Knockback

- **Impulse vector** (`wd.Kwb` L509): `d = (interval.Impulse X, Y, Z)`,
  then `d.x *= attacker.facing; d.x *= attacker.scale.x; ...` — the
  impulse is mirrored with the facing.
- **Application** (`wd.Bva` L509 -> `IH.strike` = `Bl.strike` L582):
  `b = hitDistance / capsuleLength` (how far along the target capsule the
  hit landed, clamped to 1); node1 += `d * (1-b) / weight1`;
  node2 += `d * b / weight2` — the impulse is split onto the two endpoint
  bones by the hit position and scaled by their masses (XML `Mass`).
- **Damping:** the node positions integrate through `Vc.sk` (L796):
  `velocity = ma - mf; if (jy) velocity *= (1 - bI)` (bI = node
  `Attenuation`, 0 for the skeleton) and `gravity += fDa/HD²` (0.4/1²).
  The full ragdoll integration is out of scope for this milestone; the
  native port applies the same impulse split + weight scaling + the
  bounds clamp, and drives the fighter's world-x directly.

## 5. Bounds

- The arena bounds come from the location params (`Bf.init` L474):
  `NU = Wall`, `ct = Floor`, `width`, `height`. The dojo:
  `Width=1960 Wall=80 Floor=80`.
- Fight setup (`ca.ggb` L383): `v.tFa = location.NU` (80),
  `v.NKa = location.width - NU` (1880). The fighters' body nodes are
  clamped every frame by `Al.ia` (L582) -> `fha` (L582):
  `x < NO ? x = NO : x > MO ? x = MO` with `[NO, MO] = [v.tFa, v.NKa]`
  (set by `ca.HT` L403 -> `wd.qMa` L498 -> `Nd.pMa`), and `y >= 0`
  (floor) via `P6a` (L582).
- The native port clamps the demo fighter's x to `[wall, width-wall]`
  and y to `>= 0`.

## 6. Block / parry

- `wd.Nbb()` (L512) = `da.yD(5) != null` — a **Block interval** is active
  on the defender.
- The block only changes the *defense attribute selection* (`wd.LAa` L536
  returns `v.pYa = "BodyDefense"` when blocking) and the *block
  multiplier* (`kea`: `2^(defender.BlockDamageFactor * 0.0001)`).
- The Default enemy template carries `BlockDamageFactor="-23219"` ->
  `2^(-2.3219) ≈ 0.2003` (an 80% reduction). With BlockDamageFactor=0 the
  block has NO effect (the demo's punchbag case).


## 7. AI — tactics parsing + decision loop (Phase 3.4)

Ported from `reference/www/sf2.502f0946.js`. The AI drives a fighter the
same way the game's `de` class (g="E2", L589-621) does. This section
documents the tactics file format (from the actual bytes + the JS parser),
the tactic settings XML, and the decision loop — with line refs.

### 7.1 The tactics .dat files

`reference/www/res/tactics/*.dat` (1710 files) are **zstd-compressed**
(magic `28 B5 2F FD`) multi-group archives. After decompression the payload
is a sequence of groups (JS `Si.cxb` L653-654):

```
per group:
  u32  version        (0/1 = weapon-pair tables, 2 = single-weapon, 7 = per-anim)
  cstr weapon_a       (e.g. "Fists"; "" = unarmed)
  cstr weapon_b       (only for version 0/1)
  u32  blob_size
  u8[blob_size] blob
```

The blob (JS `sb.load` L649 + `Wlb`/`bmb` L652-653 + `tab` L649-652) is a
**binary decision table**, NOT Haxe serialization:

```
u16 countA            (858 for the fists tables — the animation-name pool)
u8  lensA[countA]     (length table)
char stringsA[...]    (concatenated pool-A animation names — REAL MOVE names
                       like "HighPunch", "StepBack", "FistsStartStanceIdle-Right")
u16 countB            (weapon-name pool, 57 for fists)
u8  lensB[countB], char stringsB[...]
u32 fm_count          (Ku "table records")
u32 il_count          (Il "container records")
u32 ju_count          (Ju "condition rows")
u32 hu_count          (Hu "frame rows")
u32 gu_count          (Gu "outcome" count)
u32 float_pool_size
u32 u32_pool_size
u16 fm_anim_idx[fm_count]        (record -> pool-A index)
i16 scale, i16 fdata[float_pool_size]   (float pool, scaled: v * scale)
u32 udata[u32_pool_size]                (u32 pool)
u16 id_idx[gu_count]                    (Gu -> pool-A index)
nested, per FM record:
  u16 cntG (vec24 count)
  per vec24: u16 weaponIdx (pool B), u16 cntH (row count)
    per row (vec28B): cstr label, u16 cntJ (frame count)
      per frame: (cntJ>0) i16 Rda, then cntJ x:
        u16 cntK (outcome count)
        per outcome (Gu): u16 id-pool offset (shared, sequential),
                          u16 pa (float count), u16 da (u32 count)
        then the outcome's float windows + u32 outcomes read from the
        shared pools in order (JS L652: `t`/`z` offsets advance per record)
```

Semantics: a record is keyed by the **enemy's current animation**
(pool-A name); its rows are per-condition groups; each outcome is
`{anim, float window-edges[], u32 window-outcomes[]}` — at decision time
the AI compares the (adjusted) distance against the edges, picks the
window, and takes the u32 outcome id (>0 -> the outcome anim joins the
candidate list). The `_.2ae51655.dat` unarmed file is 102304 compressed /
169100 decompressed; `fists_fists.0c5a246d.dat` is 18863 / 169100 with 2
groups (v1 + v0) of 7 records each.

Native parser: `core/scene/ai.cpp` (`tactics_parse_file` + the `sb` blob
reader) — widths match the JS `cd` reader (`ea`=u8, `ie`=u16, `Zd`=i16,
`ti`=u32).

### 7.2 tactic_settings.xml (JS `P` g="E5" + `Md` g="EB", L623-643)

The `<Tactics>` section lists named tactics; each `<Tactic>` carries:
- `AnimationWeights` — `<Animation Name Base CounterFactor DamageFactor
  HealthFactor EnemyHealthFactor AnimationFramesFactor ChildFramesFactor
  MagicBulletFactor MissileBulletFactor HitFactor DistanceFactor Limit
  AntiLimit Shift ConditionalDesigionFactor FactorType>` weight curves
  (the JS `cc` class, L644-648).
- `UseDefense` — `CounterAttackChance`/`DodgeChance`/`BlockChance` curves.
- `UseSafeAttackChance`, `TableAttackChance`, `DodgeMissilesChance`,
  `DodgeMagicChance`, `CautiousMovementsChance` curves.
- `QuickAttacks`/`Evades` — `QuickAttackChance`/`EvadeChance` slots
  (`Animation` names + the chance curve + optional Conditions).
- `ExpectedWait` — per-anim wait curves.
- `DistanceError`/`FrameError`/`ResponseDelay`/`EnemyResponseDelay` —
  `<Min>/<Max>` curve pairs.
- `Memory Strikes RoundFactor`.

Template inheritance: `<Tactic Template="UseTables">` copies the template's
missing attributes + child elements (JS `kh.z0a` L655-657) — the native
`resolve_template` mirrors `kh.Feb`/`H2`/`Cha` (L656-657).

### 7.3 The decision loop (JS `de` L589-621)

Per fight frame the game calls `ca.Ea` (L385) -> `nzb` (facing lock) ->
`ia` -> `Hnb` -> each fighter `wd.ia` (L498) -> `Anb` (L499) -> `Ykb`
(L500) -> `de.ia(opponent, ...)`:

- `de.ia` (L592-594): snapshots the fight state into `Ue` (`mQ` L620),
  manages the wait counter `eh`, gates on `hcb` (L598-599 — no NoDecision
  interval/move active), evaluates the distance category `dqb` (L600 —
  CounterAttack/Dodge/Block ranges from the UseDefense cumulative draw),
  then `Pqb` (L604-608) makes the core decision.
- `Pqb`: facing lock (`b6a` L603 x enemy facing), dodge missiles/magic
  (`fCa` L599), the safe-attack/attack-table branch (when the enemy is in
  its move start and the AI is uninterruptible), the distance-based
  response (CounterAttack range -> safe attack / throw; Dodge range ->
  throw; Block range -> block), then the QuickAttack/Evade slots (`Nwa`
  L603).
- The safe-attack table lookup `YAa`/`Q6a` (L608-610) + attack table `XAa`
  (L611-612) read the tactics blob records by the enemy's current animation
  and pick the distance-windowed outcomes; `Gea` (L613-616) is the throw
  table.
- The candidate list (`wb`/`ld`) is filtered by `V1` (L601-602 — the move's
  TACTICS conditions only, `Fc.gm=!1` so Keys pass) and picked by the
  weighted roulette `jL`/`Md.jL` (L640) over the tactic's AnimationWeights.
- The chosen move's wait becomes `eh`; `Ykb` (L500) starts it via
  `Okb` -> `NS` (L505) -> `da.Skb` — the native `Fighter::ai_start_move`.

The native `AiController` (`core/scene/ai.hpp`/`ai_controller.cpp`) ports
this structure 1:1. **Documented native approximations:**
- the fighter's body-part animation frames (`vd`) are reduced to the single
  current animation + frame (the native fighter has one merged body);
- the strike-memory accumulators (JS `cc.Gb`'s per-anim damage/counter/
  hits, fed by combat events) are 0 — the AnimationFactors probe term
  contributes nothing yet;
- the QuickAttack slot tag `ShortAttack` (from the old game) resolves to
  the `Punch`-tagged Fists moves in this build;
- the tactic's `Conditions` sub-trees on QuickAttack/Evade slots are parsed
  but not evaluated (the shipped tactic_settings uses condition-free
  slots).

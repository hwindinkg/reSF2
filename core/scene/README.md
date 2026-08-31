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

## 8. Fight controller — rounds, phases, timer, win/lose, HUD (Phase 3.5)

Ported from `ca` (sf2.502f0946.js L379-433). The controller turns the
fighter/moves/physics/AI pieces into a complete fight.

### 8.1 The fight phases (`eu` 0-3, JS `xF` L388)

| `eu` | Phase | JS entry | Native |
|---|---|---|---|
| 1 | StartStance | `FNa` (L409) → `xF(1)` | `enter_start_stance()` |
| 2 | Fight | `Rkb` (L410) → `xF(2)` + HUD `play()` (L2037 sets `round.Vt=!0`) | `enter_fight()` |
| 3 | EndStance | `E3a` (L412) → `i4a` (L409) → `xF(3)` | `enter_end_stance()` |
| 0 | idle | `bob` (L401) → `xF(0)` before `tx()` | — |

`xF` propagates `Je` (the fighter stance) to every fighter so the move
conditions' `RoundStage` (conditions.hpp `round_stage`) matches the phase.
The **StartStance → Fight transition** is the animation-stop handler
(`kg` L387): when the StartStance clip ends (`OCa()`), `eu==1` → `Am()`
(and the HUD shows the FIGHT! label) or `xF(2)` directly for FightNone.
The native port holds phase 1 for the clip length (the tutorial trace
shows 134 frames / ~2.2s — `reference/traces/console.log`).

### 8.2 The round flow (`tx` L407, `Z2` L408-409)

- `tx()`: `round.Vt=!1; round.eL=Da.pT; round.time=0; round.gma=Da.R4`.
  `eL` = the Rounds attribute (the ROUNDS-TO-WIN threshold), `gma` = the
  RoundTime attribute (the HUD countdown seconds).
- `Z2()`: `round.round++`, snapshot HP/moves, `MHa()` → each fighter
  `c.Z2(round.round)` + `parameters.nob()` (round flags reset), then phase 1.
- Between rounds the fighters are **healed by `Da.qDa`** (`NA` L414:
  `c.jT(this.Da.qDa)` — HealthRecovery, default 1) — NOT a full reset.

### 8.3 The round-end check (`Onb` L411)

```
Onb(){ if(this.h9){ a = wo.nB.ng >= round.eL;   // ROUNDS-TO-WIN
        if(!cY&&!TG){ h9=false;
          b = wo.nB.qb && Rk < pf.length-1;      // player has a next enemy
          c = Da.sR();                           // multi-fight battle
          !a&&c||a&&b ? (next fight / next opponent)
          : a ? bea(wo.nB)                       // BATTLE END
          : (NA(); Z2()) } } }                   // next round
```

- **KO**: a fighter `gd<=0` (`xc.Jfa` L817). The round winner is the
  HIGHER-HP fighter (`vfa` L413 — with both alive a PVP rule compares HP).
- **Timeout**: only with the `ERuleTimeoutWin` fight rule (JS `BT` L392
  sets `ey=2`). The shipped stages.xml has no timeout rule; the demo
  forces it for the timeout test. The timeout winner is the ENEMY
  (`E3a` c==3: `a.ng++` on `Zb`).
- **Ringout**: the `ERuleRingout` rule (`BT` L392 sets `ey=4`), applied
  by the rules system — not in the shipped stages.

`E3a` (L412): the winner's `ng` (rounds won) increments; when
`ng >= round.eL` (Rounds) → `bea` (L413) = the battle end (`xJ=!0`,
the results flow, `Da.PU=Rk`). Otherwise `NA()` (recover) + `Z2()`
(next round).

### 8.4 The timer

`round.time` starts at 0 (`tx`) and the HUD countdown runs from
`round.gma` (RoundTime): `Sf.iPa` (L2035) `NF = xU/60` where
`xU = gma*60+1` decrements 1/sec while `round.Vt` (phase 2). The native
port advances `round.time` during phase 2 and the HUD shows
`gma - ceil(time)`.

### 8.5 The HUD (`Ar` L2016-2019 + `Sf` L2032-2040)

- HP bars: `Br` (L2021-2026) — three 1px-wide atlas frames stretched to
  the bar width: `HealthBar_Empty` (bg), `HealthBar_Full` (current),
  `HealthBar_Hit` (trailing damage). The layout (Sf.layout L2036-2037):
  the bars sit at `(viewW*.5 ± 520*c)` with a scale `c` that shrinks with
  the view; the timer `Kp` centered between them.
- The timer digits: `Sf.Kp/OA` (L2033-2035) — the `ea` text renders the
  countdown with the digits font. The fonts are BMFv3: `fight/digits.fnt`
  (page `digits_0.png` = `fight/digits.png`, 256x256) and
  `fight/round.fnt` (page `round_0.png` = `fight/round.png`, 544x256).
  Decoded glyph rects (this port parses the BMF blocks):
  - digits.fnt: '0' (0,119,84,79), '1' (0,207,0,34), ... advance ~1-3 px.
- The round indicators: `Er` (L2020-2021) — `Round_Done` / `Round_Undone`
  dots (ui.png frames).
- The FIGHT!/Round labels: `Cr` (L2021-2026) — the `fight` / `round` /
  `label_win` / `label_lose` / `timesup` images (the fight/ui atlas +
  the round.png sheet).

The demo renders a functional first-pass HUD (flat HP bars, the digit
glyph quads for the timer, round dots) at the JS bar positions; the exact
per-pixel `Sf.layout` scale curve is approximated (flag: the exact-layout
gap).

### 8.6 The fight screen (`ai`/`ma`) and the camera

- The fight screen (`ma` L1837 / `ai` L2004-2010) drives the fight:
  `case 5: Za.F().update(); ma.YL(Ig, dt)` (L2006-2008) → `ca.Ea` (L385)
  → `ca.ia` (L388) = the per-frame fight update. The camera (`ma.Sya`
  L1833) centers on the fight midpoint and zoom-fits the arena.
- The native `FightCamera::framing` mirrors the fight-start view
  (center on the fighters' midpoint, zoom = min(1, viewW/span)); the JS
  intro curve (`Sya`'s smoothed zoom/pan) is simplified.


---

## 9. App shell — screen manager, save/load, main menu, map (Phase 3.6a)

Ported from `reference/www/sf2.502f0946.js`. All line numbers are 1-based
against the current build (matching JS_MAP.md).

### 9.1 The main loop -> screen manager chain

The game's frame driver (`Pg`, L47-60) runs a fixed 60 Hz update decoupled
from the rAF render rate (`Us`, L135: `Gy` accumulator, `Bm` = 1/60):

```
requestAnimationFrame (Qg.start, L135)
 └─ Pg.xeb(dt) (L56)
     ├─ Kk.update() window
     ├─ Ts.aa(dt) task scheduler
     ├─ input screens Y3() per-frame poll (Ik/Hk/lf/Jk/rf)
     └─ fixed 60 Hz: Pg.aa(1/60) (L57)
         ├─ input states update
         ├─ root.oja(dt)  (Db scene graph, L29)
         │   └─ mc.aa(dt) (L124) → $d.aa(dt) (L121) → elements.oja
         └─ Pg.Ea(alpha) (L57): root.nja → mc.Ea (L125) → $d.Ea → elements.nja
```

The screen manager `mc` (L122-127): `mc.K` singleton, `stack[]` of `$d`
screen states, `Taa(cls, caller, info)` pushes a screen (L126), `aa(a)`
(L124) runs the transition state machine (`pu` 1..3) then updates every
active state (`d.active && d.aa(a)`, the JS L125 gate), `Ea(a)` (L125)
renders. `B()` (L124) tears down.

The screen-state base `$d` (L119-122): `elements` (a `Db` "Elements"
node), `content`/`node` render nodes, `time`, `active`, `state` (the
`Te(n)` state machine, L121: 2/3/5 = active, else inactive), `Mr`
(manager), `caller`, `info`. `jI(cls, info)` (L120) = `Mr.Taa(cls, this,
info)` — the push helper every screen uses.

The native port (`core/app/screen_manager.{hpp,cpp}`) keeps the stack +
the active-gate + update/render passes; the JS `ae` fade transitions are
replaced by an instantaneous switch (flagged: the exact fade is out of
scope — the screen flow is what matters this phase).

### 9.2 The save/load — users.xml (JS `Aa`/`SF2User`)

- Storage keys (JS L2462): `Aa.WU = "SF2User"`, `Aa.Y6 = "SF2Packs"`,
  `cg.P6 = "SF2Flags"`.
- `Aa.load()` (L70-71): reads `SF2User` (a `Ck` storage handle), zstd+
  base64 decodes (`ri.decode`), `Rb.parse` → the users XML document.
  `Aa.save(a)` (L71): `a.stringify("\t", !0)` → `ri.encode` → store.
- There is no literal "users.xml" in the JS — the SF2User save IS the
  serialized users.xml document (JS_MAP §6.2).
- The template: asset id 9 = `users_default.b7da2019.xml` (G.rq L2490);
  extracted copy `reference/extracted/xml/res/users_default.xml`. The
  Warrior: Money=0, Strength=3, Level=1, Power=5, Tutorial=MOVE,
  Weapon=Fists, Armor=Body, Helm=Head, Ranged=NoRanged, Magic=NoMagic,
  Skeleton=Skeleton, CurrentZone=ZONE_1, plus the equipped `<Items>`.
- First run: `L.aia` (L65): `this.BJ = !Aa.Ue() && Aa.init()` — when no
  save exists the game re-initializes from the default.

The native `SaveSystem` (`core/app/save_system.{hpp,cpp}`) mirrors this:
`load()` returns the template when no save file exists, `save()` rewrites
the Warrior progression into the document (preserving the whole
users.xml) and writes the file.

### 9.3 The GeneralMenu screen (screen 8)

- Enum: `xn` L1167-1168 (`GeneralMenu` = 8).
- The main-menu UI: the `za` top bar (L1972-1977: `topPanel` frame from
  the misc atlas, money/energy counters) over the dojo background. The
  four tab buttons are the `cs` class (L2188-2189):
  `a(0, y.WRa, y.YRa, y.XRa)` = Progress, `a(1, y.bSa, ...)` = Strikes,
  `a(2, y.TRa, ...)` = Achiev, `a(3, y.ZRa, ...)` = Seal — the
  `buttons/Progress[_active|_pushed]` etc. frames from the profile atlas.
- The entry buttons (Fight/Shop/Profile) are the menu atlas frames
  (`menu.aaef83fb.json`): `Dojo_normal/_active/_pushed`, `Map_*`,
  `Shop_*`, `Profile_*` — the Fight entry is the Dojo button.
- Clicking Fight → `wa.F().mp(5)` → the Map screen (the JS `qxa` L1213:
  `a.type != "FightPVP" && wa.F().mp(5)` after a battle result; the menu
  itself navigates via `wa.F().mp`).

### 9.4 The Map screen (screen 5)

- `Ya` (L2124-2132, `dJ(){return 5}`): the battle-node screen. The
  backgrounds are `map/part0..6` (asset ids 336..324, G.rq L2490); the
  `map0` frame is 2046x854.
- The battle nodes come from stages.xml `<Zone>/<Battle>` with `X`/`Y`
  positions (the JS `Ch`/`qb` parsers, L1224/L1404). Node placement
  (`qe.X0a`, L2144): `node_x = X*uM + bg.w/2`, `node_y = -Y*uM + bg.h/2`
  (uM ≈ 1; the node button art `BattleBtnBase/base_<name>` +
  `BattleBtnActive/active_<name>` from map/buttons.json).
- Clicking a node → `wa.F().mp(6, battle)` → the Fight screen (the JS
  `Ya` battle-start path L2131-2132 + `v.kD` L1217).

### 9.5 The screen-transition flow

```
Preloader(0) → Loader(2) → Dojo(3) → GeneralMenu(8)
  (boot: L66-67 `lbb(Rg)` → the Preloader; `ad.load` L1969 pushes the
   target screen via `Zd.load(a)` L1837)
GeneralMenu(8) --Fight--> Map(5) --node--> Fight(6) --result--> Map(5)
  (the results flow `v.kD(new Fh, ...)` L1217 + `qxa` L1213 pops back)
```

The native shell boots straight to GeneralMenu (skipping the web
loading screens) and wires Menu → Map → placeholder Fight result → back
to Map.

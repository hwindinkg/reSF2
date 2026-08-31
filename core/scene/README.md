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

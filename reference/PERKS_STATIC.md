# PERKS_STATIC — perk defs, triggers/actions, enchant budgets (web build)

Static-only spec from `reference/www/sf2.502f0946.js` (2533 lines, 1-based)
plus on-disk `reference/extracted/xml/res/perks.xml` (202 `Perk` rows,
byte-identical to www-bundle `res/perks.xml`) and `internal_settings.xml`
(budgets). `OPEN (needs runtime trace)` where noted.
Companions: `reference/SHOP_STATIC.md` §9 (item-side enchant binding),
`reference/COMBAT_STATIC.md` (damage gates perks feed).

Minified names: `Be` perk def (L1328-1349), `dw`/`v.Rg` perk registry
(L1188-1191), `Hf` asset preprocessor (L1360), `Kw` trigger executor (L1349),
`Fw` action factory (L1375-1376), `Ac/Cp/Dp/Ep/Hh` event classes
(L1318-1324), `Iw` trigger instance (L1333-1334), `Jw` rating row (L1334),
`Lv`/`v.cba` enchant budget (L1178-1179), `Oi/Lt` offer bundles (L349-351).

---

## 1. Registry: `dw` / `v.Rg` (L1188-1191)

- `R3[]` = `Be` defs (`parse`, L1189); `XS[]` = perk-tree unlocks
  (`bPa`, L1189: `Name/Level/Description/Move` + `UpgradeLevel` tiers via
  `j0a`); `jn(name)` find-or-null (L1190-1191); `i0a/j0a/rja` Set-clones
  (L1190).
- Sources: `td.Pdb`: `Hf.y0a(310); v.Rg.parse(310)` (L1160); `td.Vib` (asset
  1315): `v.Rg.bPa(<Perks>)` (L1161; re-fed by `Jmb`).
- `Hf.y0a` (L1360): stamps collected trigger names into each `<Perk
  Template="A|B|…">` (`qGa` + `MWa`).
- Asset id → file: **OPEN (needs runtime `Ja.ki` trace)** — but content-shape
  matches `res/perks.xml` exactly (`<Perk Name/Level/Description/Move>`,
  `<Set>`, `<RatingEvaluation>`, `<UpgradeLevel>`, `<Trigger>`), and the
  bundle copy == extracted copy (202 rows, AVENGER sample identical).

## 2. Perk def `Be` (L1328-1334)

- Fields (L1328-1329): `Wh=1` default, **`Wh=0` iff `PerkType=="Combo"`**
  (L1332); `qq[]=Template-split + name`; `attributes` (any `v.eo` attr);
  `iC` = `<Set>` map (`Zjb`, L1334); `iM` = `BarSetAttribute`-resolved int +
  `BarShift` (L1332); `Tc` = tier; `x4[]` = `<RatingEvaluation><Rating>`
  rows (`Ujb→Jw`, L1334); `Dm[]` = triggers (`okb→Iw`, L1333-1334);
  `SP/isActive`, `image/move/AR/Cg/bP/Pva`.
- `clone(set,rating)` (L1329-1330): rebase onto `Rg.jn(name)` + Set attrs.
- `eea(a)` scaling (L1349): `a>=0 ? lha-(lha-1)*2^(-a/cda) :
  tva+2^(a/cda)` with `v.CY=(cda=108, lha=1.2, tva=0)` from
  `<Aspect DoublingRange/Limit/Antilimit>` (`Mib`, L1220). Used by enchant
  damage mods (`iea`, L813-814: `F*=1+(ff-1)*eea(A-zBa)`).

## 3. Triggers and actions

- Events — `Ac.create` (L1318-1319): `AnimationEnd/Start`, `AreaEnter/Exit`,
  `EveryFrame` (`Cp.Step`, L1323), `HitPreCrit/HitPostCrit`
  (`Hh`: `Defense/Block/Critical/Shock/Animation/DamageMin/Max`, L1320-1322),
  `IntervalEnd/Start`, `ModExpires` (`Dp.Name/Namespace`, L1324),
  `PerkEventMagicCharged`, `PostHit`, `RoundStageStart`.
- `Be.Ava/okb/yqb` (L1333-1334): trigger var substitution (`_Var` refs).
- Actions — `Fw` factory (L1375-1376, ~30): `AddMagicCharge`,
  `ApplyModEffect`, `ChangeAdditionalDamageValue`, `ChangeHitEffectScale`,
  `ChangeImpulse`, `ChangeModelColor`, `ClearMods`, `DisableInterval`,
  `Lifesteal`, `MarkPerkAsUsed`, `ModAttributes`, `ModFlag`,
  `ModHealthChange`, `ModIcon`, `ModInvisibility`, `MoveModel`, `Provoke`,
  `SetCooldown`, `SetDarkness`, `SetHit`, `SetModFrames`, `SetModVariable`,
  `SetRangeVariable`, `SetTactic`, `ShowDebugLine`, `SlowModel`,
  `StealMagicMod`, `Switch`, `TurnOffCollision`, … Executed by `Kw.c8a`
  (L1349). Quest-visible: `MagicBullet=bh`, `MagicCharge=my` (L1339).
- Disk samples (`perks.xml`): `PERK_AVENGER`
  (`Set Chance=0.3 Frames=300 DamageFactor=5850`; triggers
  `HitPostCrit[Me,Critical=1]` + `ModExists[Icon!1]`/`Random[_Chance]` →
  `ModIcon`/`ModAttributes`/`SetHit[Critical=1]`/`ClearMods`);
  `PERK_DESPERATE` (`DamageFactor=3219 HealthLimit=0.1 Step=20`,
  `EveryFrame`); `PERK_HELM_BREAKER` (`5850/0.2/300` vs HeadDefense);
  `PERK_MIRROR` (`5850/0.2/120` on PostHit-Block).

## 4. Enchant budgets and selection

- `X.uh(map,key,box)` (L106): `has ? (box.G=get, true) : false`.
- `Lv.parse` (L1178): `AntiCheatSettings/EnchantmentsCountExclusion` → DV
  map. **On disk (`internal_settings.xml`) exactly 2 rows, both `=3`**:
  `WEAPON_SUPER_DAISHO`, `WEAPON_SHADOW_DAISHO`. `sOa(name,Ia)` returns
  bool + budget in `G` (L1178-1179); absent → false (default path).
- `zf.Kia` (L1257-1258, verbatim): split `Wh==0→b` / `else→c`; if `c` all
  ⊆ catalog `GF` → push `c[0]`, then with budget `G` push `c[1..G-2]`
  (`h>=G-1` break); if `be` still empty → single dead pass; push `b[0]`;
  top up from name-map while `be.length<d.size`.
- Gates: `Xz(item)` (L1221-1222): every equipped weapon/armor/helm/ranged/
  magic entry (`bxa`, L1222) must `nEa(name)`; used by `Yfa` (first Wh==0
  with Xz, L304), `Wk` (L812: owned Wh==0 needs Xz), `rs.xI` dim (L2283).
- `SP` flag (L812): weapon-granted perks (`f.SP = (type==vg)`);
  `P2a/nob` (L819) toggle `isActive` off/on.
- `p.BD(item,owned?)` (L220-221): owned entry's `be` else catalog `GF`.
  `Yfa/o0a` (L304), `Pma/Wk` (L811-812), `D8a` recipe hooks (L303).
- Shop binding (SHOP_STATIC §9 + lines): `qs` pane (L2280), `rs` cards
  (L2282-2283), `gC` tab (L2287), `Pa.Osb` Aspect stamp via `ye.gea`
  (L1233; `ye.v7` L914-915), `mY` persist (L1261), `VXa/anb` apply (L1262),
  `dDa` re-apply (L1233-1234), `c0a/R_a` removal (L1261-1262), `wAa`
  first-Wh0 (L1262), `Vjb/yH` RecipeDelivery (L1257), `Lt/Oi` offer bundles
  (L349-351: `Oi{xu:item, fT, UK delivery, VCa key}`, `T_` parse).

## OPEN (needs runtime trace)

1. `Ja.ki(310)` → file proof (§1): RESOLVED (round 5) — `G.rq` manifest
   order = asset ids (COMBAT_STATIC App. B): **310→perks.xml**.
2. `Kw.c8a` live fire order + `ModAttributes Frames` countdown/`ModExpires`
   lifecycle; `MarkPerkAsUsed` semantics.
3. `Pma/Wk` rebuild timing vs `BD` (owned vs catalog races on buy/equip).
4. `c0a/R_a` + `Vjb` recipe flows (all `debugger`-guarded paths).
5. `Be.ZFa/bhb/Ffb` expression evaluators (L1344) against live vars.

---

## 5. Perk evaluator semantics (implementable)

### 5.1 Trigger flow (static order)

1. Fight events enter the `bc` bus (`ca.Sba→tb.Gj`, L393; `Gj` dispatch
   L1364): per `ej` entry → `h8a(type)` slot → per trigger `v_a`.
2. `v_a` (L1364): `c8a(Wa)` perk lookup → must be `enabled` →
   `bBa` (model's `Fw` entry, L1366-1367) → event gate
   `t0a(info)` → `Axa(model, …)` (L1359: **all** `rb` conditions must pass,
   `cb`-negation aware; `Hc` events pre-matched by `isEqual`).
3. `Iw.parse` (L1359): `Name`, `Events=Ac.create`, `Conditions=ec.create`,
   `Actions=Ma.create`. `Kw` slots (`h8a` L1350) route by trigger type;
   `Pob/UKa` (L1366-1367) build `Fw` queue entries (`jp` wrappers);
   `FE` (L1366-1367) re-fires owned actions.
4. `Ma.create` = the 31-name action factory (L1374-1377):
   `AddBullets→Gp`, `AddMagicCharge→Hp`, `ApplyModEffect→Ip`,
   `ChangeAdditionalDamageValue→Jp`, `ChangeHitEffectScale→Kp`,
   `ChangeImpulse→Lp`, `ChangeModelColor→Mp`, `ClearMods→Np`,
   `DisableInterval→Op`, `Lifesteal→Pp`, `MarkPerkAsUsed→Qp`,
   `ModAttributes→Rp`, `ModFlag→Sp`, `ModHealthChange→Tp`, `ModIcon→Up`,
   `ModInvisibility→Vp`, `MoveModel→Wp`, `Provoke→Xp`, `SetCooldown→Yp`,
   `SetDarkness→Zp`, `SetHit→$p`, `SetModFrames→aq`, `SetModVariable→bq`,
   `SetRangeVariable→cq`, `SetTactic→dq`, `ShowDebugLine→eq`,
   `SlowModel→fq`, `StealMagicMod→Kf`, `Switch→gq`,
   `TurnOffCollision→hq` (L1403: `vZ` toggle).

### 5.2 `fw` dispatch → per-action semantics (L1290-1300)

`lF`: `nm?new jp(d):d` (`jp` state wrapper L1370; `Tb.nm` default true
L1378), then by `action.type`; `ia` ticks `Uf/Iv` frames and calls
`JNa` revert on expiry (L1290/L1298-1299); `pP/Z_a` cleanup (L1300-1301).

| Type | Action (class) | XML attrs | `fw` method → effect (inputs/outputs/state) |
|---|---|---|---|
| 1 | ModIcon (`Up`) | Image/ShowExpiration/ExpirationVer | `bLa`: show icon (`image/kx/sz`) via `model.cka` (L1292-1293) |
| 3 | ModAttributes (`Rp`) | any `v.eo` attr → `aP` expr map | `VKa`: `±attr` add on target (sign by apply/remove); `DamageFactor` also records `Bb.Tua` (L1293) |
| 4 | ClearMods (`Np`) | Name | `Vob`: arm `qw` on same-named mods (+`bc.iAa` namespace) (L1297) |
| 6 | DisableInterval (`Op`) | IntervalName/IntervalType | `Yob`: `hT(G0)` by type else `F4` by name (L1294) |
| 7 | AddBullets (`Gp`) | BulletType/Value expr | `Rob`: `MagicBullet→hZ(n)+LA` (L1295) |
| 8 | AddMagicCharge (`Hp`) | Value expr | `Sob`: `Hwa(n)+LA` (L1295) |
| 9 | SetHit (`$p`) | Critical/Block/Shock/Disarm/Damage | `ppb`: override `Bb.se/Ub/Yi/block`, set `bR/Zi` (L1294-1295) |
| 10 | SetModFrames (`aq`) | Name/Frames expr | `dpb`: retime named mod `Uf/Iv` (L1295) |
| 11 | ApplyModEffect (`Ip`) | Name/Type(Pulse/Stack)/StackCount | `cpb`: fire named mod's `U4` (L1296) |
| 12 | ModHealthChange (`Tp`) | PerFrameValue | `Inb` (via `znb`, L1290): `aM(model,O3)` every frame (L1298) |
| 13 | Provoke (`Xp`) | Trigger | `jpb`: fire `SL` trigger set via `tb.Pob` (L1296) |
| 14 | Lifesteal (`Pp`) | DamagePart | `apb`: `aM(model, VZ·Zi·so-ratio)` (L1294) |
| 15 | ModInvisibility (`Vp`) | — | `cLa`: `hpb(model)` (L1294) |
| 16 | SetTactic (`dq`) | Name | `qpb`: `yZa(LL)` (L1295) |
| 17 | SetModVariable (`bq`) | Value expr | `dka`: `Fc.Q3.set(name,result)` (L1299) |
| 18 | SetRangeVariable (`cq`) | Value/Min/Max exprs | `rpb`: `Q3.set` clamped (L1299-1300) |
| 19 | SetCooldown (`Yp`) | Frames/Button | `npb`: `wKa(button)+b5` (L1300) |
| 20 | ChangeImpulse (`Lp`) | MultiplierX/Y/Z | `YKa`: `gob()`/`YLa(x,y,z)` (L1293-1294) |
| 21 | ChangeHitEffectScale (`Kp`) | Scale | `XKa`: `Qz` set (L1293-1294) |
| 22 | ChangeAdditionalDamageValue (`Jp`) | Value expr | `WKa`: `Ly` set (+`Tua` record) (L1294) |
| 25 | SetDarkness (`Zp`) | Opacity/Color/FrameTimer/Show | `opb`: `Nqb` overlay (L1297-1298) |
| 26 | Switch (`gq`) | Value + Case/Default children | `tpb`: `Z4a()` returns null (`debugger`) → **effectively inert** (L1402/L1296-1297) |
| 27 | StealMagicMod (`Kf`) | — | `aLa`: `$o` swap enemy magic + priority-0 pin + move merge (L1398-1401) |
| 28 | SlowModel (`fq`) | Speed/IsRulePerk | `gLa`: `KT(hU,speed)` timescale (L1398) |
| 29 | ChangeModelColor (`Mp`) | Color | `ZKa`: `Qs` (L1381) |
| 30 | TurnOffCollision (`hq`) | bool | `S`: `Nl.oI[].vZ` toggle (L1403) |
| 31 | MoveModel (`Wp`) | From/To/Offsets/Axis/LerpSpeed | `S`: `Ow` lerp animation (L1385-1387) |
| 5/23/24 | ModFlag (`Sp`) / ShowDebugLine (`eq`) / MarkPerkAsUsed (`Qp`) | — | **no `fw` case → inert in fight** (flag effect, if any, via mod namespace — OPEN-KEPT) |

No `Ma` action carries `type=2`. `Ma` base has no `S()` — only types
30/31 route to class `S()`; all others route to `fw` methods above.
`JNa` revert covers types 1,3,15,20,21,22,27,28,29,30 (L1298-1299).

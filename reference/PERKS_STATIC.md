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

1. `Ja.ki(310)` → file proof (§1).
2. `Kw.c8a` live fire order + `ModAttributes Frames` countdown/`ModExpires`
   lifecycle; `MarkPerkAsUsed` semantics.
3. `Pma/Wk` rebuild timing vs `BD` (owned vs catalog races on buy/equip).
4. `c0a/R_a` + `Vjb` recipe flows (all `debugger`-guarded paths).
5. `Be.ZFa/bhb/Ffb` expression evaluators (L1344) against live vars.

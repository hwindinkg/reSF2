# RANGED_MAGIC_STATIC — bullets, charges, magic visuals (web build)

Static-only spec from `reference/www/sf2.502f0946.js` (2533 lines, 1-based)
for Stream 1's ranged/magic round. Charge intake (`jA`/`j6a`/`W7a`) already
lives in COMBAT_STATIC §A7 + §5.1 — linked, not duplicated. `OPEN (needs
runtime trace)` where noted.

## 1. Bullet state: `bh` / `dO`

- Init `my=kaa=bh=dO=0` (`wd` ctor, L490).
- `bh` (magic bullets): `+=` via `hZ(n)` (L505); cap `bh>1 → zL(1)` in
  `LA()` (L505); reset `zL(0)` in `yKa` (L504); per-round `fmb` (L398).
  `LA()` also converts full charge: `my>=1 → hZ(1), yL(0)` (L505).
- `dO` (raid/ranged bullets): `+=` ONLY via `vZa` (L525; perk path
  `Tvb s6==1`, L519). No round reset, no consume site in static text.
- `K0()` (L505): `ig==NoRanged ? 1 : -1` (has-ranged flag; AI range
  callers L594/596/620/680).
- `StartingBullets` / `RangedQuantity`: **0 JS refs** (round-5 verdict,
  COMBAT_STATIC App. B OPEN-KEPT). Initial ammo for either pool is not
  statically resolvable.

## 2. `lp` condition eval (L1313)

`MagicBullet → a=bh`, `RaidChargeBullet → a=dO`, else false; then
`Ag.Wb()` range check `xE(a)`. Shipped uses: 2 `Bullets` conditions
(`MagicBullet Min=1`, `RaidChargeBullet Min=2`) — C++ evaluates these
against stubbed 0.0 (documented OPEN in trigger.hpp).

## 3. `AddBullets` exec gap

JS `Rob` (L1295): `MagicBullet → hZ(n)+LA`. No C++ branch (log-only).
BUG-low: 1 shipped use (`PERK_DIRECTOR_SET_PLOT_TWIST`, MagicBullet +1).

## 4. `NoBulletsReplenishment` marker `cj` (L869)

Parse-only rule (`Ib` clone); `replenish` has 0 JS hits — no refill path
exists statically for it to suppress. Behavior OPEN-KEPT (likely a
round-start refill in the native-driven flow, or dead marker). Shipped on
all 15 Survival nodes (Player scope).

## 5. Charges `my`

- `yL(a)` clamp 0..1 (L494); `Hwa(a)`: `bh==0 && yL(my+a)` — accrues only
  with no bullets (L504-505).
- Spawn `yL(vo.vE)/zL(vo.cl)` (L402); round `yKa`: `zL(0)+yL(InitialCharge)`
  (L504); intake `Jma` (L521 → COMBAT_STATIC §A7).
- `sp` eval (L1314): `xE(b.my)`.
- `MagicCharged` events (L1318-1319) + slot 8 never fired (trigger.hpp
  documents OPEN).

## 6. `AddMagicCharge` exec gap

JS `Sob` (L1295): `Hwa(n)+LA`. No C++ branch (log-only). BUG-low: 1 use
(`PERK_BOSS_HERMIT_MAGIC`, `_Charge`).

## 7. `jPa` / `IOa` fight-start flows (L524, via `Pma` L398)

`jPa`: `nga(!0)` + `JB.vob/u5(Mo)` (move-set rebuild);
`IOa`: `lga(!0)` + `QF` + `Su.xKa/FT` + `x6` (interval rebuild).

## 8. Magic visuals loader

- Effect sprites: `G.qf("magic/"+fileName+".png")` preload (L730,
  `Yl.preload`) and `+".json"` + `+".png"` at spawn (L838-839).
- **Absent** as loose files under `reference/www/res` (no `magic/` dir)
  and under `assets/` (which holds only item/enchant *icons*,
  e.g. `ut_items/icon/magic_fire_ball.png`). They ship as manifest asset
  ids 394-551 (`magic/mgc_*` png+json pairs); models are
  `magic_ktx/dds.dat` (ids 392-393) — COMBAT_STATIC App. B proof.

## OPEN (needs runtime trace)

1. Initial `bh`/`dO` ammo sourcing (see §1).
2. `cj` gated behavior (§4).
3. Ranged-attack interval shapes (no collected lines — moves-side work).

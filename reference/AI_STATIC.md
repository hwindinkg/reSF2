# AI static spec (web build) — extracted from `reference/www/sf2.502f0946.js` (2534 lines)

Static-only. Anything not resolvable without a runtime trace is marked
`OPEN (needs runtime trace)`. Line numbers are exact for the current file.
Related: `reference/JS_GAMEPLAY.md` (behavioral notes), `core/data/README.md`
(asset containers), `reference/traces/README.md` (oracle record format).

## 1. Where tactic/chance VALUES live (data files)

- `reference/www/res/xml.9e0b4b10.dat` → entry `res/tactic_settings.xml`
  (21 564 bytes, root `<TacticsSettings>`). Children: `BothBot`, `Tactics`,
  `Random`, `ItemEquivalents`, `NoDecision`, `IgnoredEnemyAnimations`,
  `RandomizingEnemyAnimation`, `CautiousMovements`, `SafeDodges`,
  `EmergencyDodges`, `EvadeThrowDodges`, `EvadeUnsafeDodges`,
  `UnexpectedMoves`, `MissileAnimations`, `MagicAnimations`, `DistanceNode`,
  `Debug`, `ConditionalDecisions`. Tactics present: `Standard`, `Test`,
  `NoTables`, `UseTables`, `Sensei`, `Lynx_Ranged`, `Shogun`,
  `Shogun_Aggressive`, `Titan_Aggressive`, `Careful`, `Aggressive`,
  `Beginner` (+ more). Parsed by `P.Bmb` (L626-629).
- Per-animation move tables: `tactics/<name>.dat` and
  `tactics/<a>_<b>.dat` (lower-cased, `Si.I6a`/`Si.J6a`, L653-655), zstd via
  `Og.GI`, parsed by `sb.load` (L649-653). Evaluated through `Gc`/`Md`.
- Loader ids (OPEN: id→file mapping for `Ja.ki` unresolved): `P.zmb` (L623)
  reads asset `272` (BothBot/Tactics/ItemEquivalents/NoDecision/… lists) and
  asset `1314` (`TablesReduction` step `P.sp`, `MovementsTables`,
  `MissileTables`, `MoveLengthIntervals`, `OutcomeTables`).

## 2. Tactic object `Md` (L636-643) — parsed chance fields

Ctor parses (L637-638): `Memory` → `KW.{f6(Strikes), Q4(RoundFactor)}`;
`UseDefense` → `Tpa(CounterAttackChance)`, `lqa(DodgeChance)`,
`spa(BlockChance)`; `sua(UseSafeAttackChance)`; `vO(TableAttackChance)`;
`QuickAttacks`/`QuickAttacks21a` → `$E` list (`Tjb`); `Evades`/`Evades21a` →
`nD` list (`njb`); `Apa(CautiousMovementsChance)`;
`qqa(DodgeMissilesChance)`; `nqa(DodgeMagicChance)`; `DistanceError` → `Mu`,
`FrameError` → `lN`, `ResponseDelay` → `z$`, `EnemyResponseDelay` → `v8`
(all via `Md.K3`, Min/Max pairs, L642); `AnimationWeights` → `$oa`;
`ExpectedWait` → `x8`. `Type`: Random/Tabular/other (`getType`, L642-643).

Per-frame evaluators (L639): `U5a→Tpa`, `t6a→lqa`, `u5a→spa`, `k9a→sua`,
`a9a→vO`, `A5a→Apa`, `v6a→qqa`, `u6a→nqa` (all `X.Gb(JV)` on the `Ue`
feature context); `F6a(cs,Dqa)` = ExpectedWait (L639);
`jL(a,b,c)` = weighted roulette over `$oa` (L640, draw `Da.pg.s4(d)`);
`iCa` = weight via `Gb` (L640); `yea/j0` = `Md.I0(Mu/lN)` (L640-641,
`I0` → `Da.pg.dT`, L643); `gfa` (ResponseDelay, L640-641),
`Aea` (EnemyResponseDelay, used in `XAa`, L611).

## 3. Chance formula `cc.Gb` (L643-648)

`parse` reads (L644-645): `Base/eq`, `CounterFactor/b8`, `DamageFactor/l8`,
`HealthFactor/Uqa`, `EnemyHealthFactor/wqa`, `AnimationFramesFactor/Toa`,
`ChildFramesFactor/Epa`, `MagicBulletFactor/Mra`, `MissileBulletFactor/Zra`,
`HitFactor/V8`, `DistanceFactor/kqa`, `Shift/Fk`, `ConditionalDesigionFactor`
(`Opa`, sic — parsed, L644; old native-RE claim "0 matches" is WRONG for web),
`Limit/BW`, `AntiLimit/dV`, `FactorType` (`arb`: Linear→1/Exponential→0),
`AnimationFactors/BM` (per-animation sub-`cc`), `CurrentAnimation`
(`hsa` Me / `vqa` Enemy lists).
`Gb(a,b)` (L646-647): linear combo of `Ue` context features + `BM` terms +
`mAa` current-animation terms + (`zZ.includes(b)` → `Opa`); scaling `NYa`
(Exponential, L647-648 — note `Math.pow(2,…)`) or `QYa` (Linear, L648).
`Gb` draws `Math.random` (via `oa.eT`) **iff** `min != max` (L1146-1153,
`class Ie`, L1152) — i.e. tactic *ranges* are an unseeded entropy source on
the per-frame AI path.

## 4. Decision controller `de` (L589-621)

- Ctor (L589-591): `fk=-1`, `aqa=1`, `eh=1`, `CZ=bda=tba=qPa=vO=Awa=pua=
  oua=tua=dua=Bpa=rqa=oqa=0`, `wb/ld/E7/u$` lists, `Uu/JV/Dqa/Eqa/Ol/zk`
  `Ue` contexts, `Gc=null` (set from `Ca.Gc` by `Ndb`, L597-598).
- `ia(a)` (L592-594): `R0` gate → `mQ` snapshot → `Fl`/`q7` frame math →
  `mW`/`oC` wait handling → `eh` countdown (`eh>1` → wait) → `QJa` on enemy
  clip change → `csb`/`Zqb` (roll `t4` thresholds) → `hcb` gate → `aqa=dqb`
  → evaluated chances (`qPa/rua`, `vO/caa`, `Awa/nG`, `pua/pqa`, `oua/mqa`)
  → `wb.len=Pqb` → `k_a`/`Nwa` safe-attack additions (unless `F8`/`fk==11`;
  non-empty `iN.zZ` forces `fk=11`) → `dsb` or roulette `jL(ld)` →
  `eh=vs[a]`, return `ld[a]` (or null).
- `R0` (L608): `de.tY ? (Ca.Fj ? true : P.fP) : false` (`P.fP` = BothBot,
  L626; `Fj` = NotAI flag).
- `hcb` (L598): false (decide) unless a `NoDecision` interval
  (`P.osa`) is on own `Ji`, or enemy `cs` move matches `P.psa`, or own move
  is null. `dsb` (L600): `fk=-2`, `mW`, `eh=-2^31` (watch).
- `QJa` (L594-595): rebuilds `zk`, **5× `Da.jf()` rolls**
  (`tua/dua/Bpa/rqa/oqa`), `Mu=yea`, `lN=j0`. Called on enemy clip change
  (L593: clip in `RandomizingEnemyAnimation` list `P.Vsa`, L628) and `jwb`.
- `dqb` (L600): `b=Da.jf()` vs cumulative `CZ → 2`, `+bda → 3`,
  `+tba → 4`, else `1`.
- `Pqb` (L604-608): facing check `b6a` (`oC=3` if turned); missile/magic
  dodge `VAa` (`fk=2`); `$x/Fl` + `Ycb`/`Lbb` (L620-621) gates; `rua→YAa`
  (safe attack, `fk=1`); `caa→XAa` (attack table, `fk=0`); `nG`→cautious
  list (`fk=5`); zone switch on `aqa`: `2→YAa(fk=1)/Gea(fk=2)`,
  `3→Gea(fk=2)`, `4→fk=10` (block, 0 moves), default `oC=1,pH`
  (`fk` stays -1); `$E/cO` quick attacks (`fk=6`); `nD/hN` evade/`IB`;
  `F6a` ExpectedWait → `XW`; `XW||IB` → `XAa(fk=0)` / `H9a` IB-list
  (`fk=9`) / `nCa` cautious list (`fk=5`) / `oC=3`.
- Observed `fk` writes: `-2,-1,0,1,2,5,6,9,10,11`. `fk=3,4,7,8` have NO
  static assignment — OPEN (unused or runtime-only).
- Helpers: `YAa` safe-attack (L608-611), `XAa` attack table (L611-612,
  uses `Aea`/EnemyResponseDelay: `b=Aea(Eqa)`, `b=Fl+b` — Ju-frame horizon),
  `Gea` dodge (L613-616, uses `yD(2)`),
  `VAa` missile/magic dodge (L617), `fCa` facing/range (L599),
  `b6a` facing sign (L603), `k_a`/`Nwa` safe-attack tables (L603),
  `csb`/`bsb` quick-attack rolls (L618-619), `Zqb`/`Yqb` evade rolls
  (L619), `mQ` feature snapshot (L620), `h2a` candidate/waits split
  (L608), `de.gfa` (ResponseDelay+1, L597) — consumed ONLY via the `$x`
  cache (see below, no direct caller — that is correct, not OPEN).
- Event routing (CORRECTED 2026-09-04 — `mwb` found, `wd` level):
  `mwb(a)` calls `this.nf.jwb(b)` (own de, b=`jb` opponent) /
  `b.nf.iwb(a)` (enemy de) when bot, else `LLa(b)` (sets `ds`).
  `jwb(a)` (L596-597): on enemy move change → `QJa(a)` (5 rolls + Mu/lN)
  → unless `mcb(c)` → build `Ol` → **`$x=gfa(Ol)`** — ResponseDelay is
  CACHED per enemy-move, never re-rolled per pass. `iwb(a)`: `cs` update +
  `icb(cs)&&(eh=1)`. Port detects the move change by enemy anim name
  (documented proxy); `iwb`'s `eh=1` reset is OPEN (mwb call context
  unconfirmed — needs a live trace).
- Move validity `V1` (L601-602): in `me` + tactics conditions `FQ(2)`.
  Hit reaction `Gc.DK` (L673-676) picks via `uf.sja` (**Math.random**).

## 5. PRNG + all seeding points

- `Da` (L2352): `jf→pg.jf`, `cT(a,b)` (percent roll — NOTE the shortcut:
  `a>b ? true : pg.cT(a,b)`, i.e. NO stream draw when `a>b`),
  `rL` (reseed from `ed.getDate().getTime()` = wall clock),
  `IT(a)` (seed set).
- `Rk`: `jf()=B0()/2^31 + B0()/2^31/2^31` (**two** LCG draws combined),
  `s4/dT/cT` range helpers. `Xx` (L2366): `mf=(mf*1103515245+12345) mod
  2^31` (glibc `rand` constants — portable to C++).
- Seeds: `L.seed=getDate()%2^31` at module init (L67-68); `L.web` (L68):
  `Da.pg=new Rk(L.seed)`; `L.pfb` (L67): `seed=(seed+1)%2^31`; per-fight
  reseeds: `qob` (L900: `cz==1` → `IT(Qm)` or `rL()`, then `rL()` again);
  `v.vJa` (L1208: `FightPeriodic` → `IT(Wc.fv)` + `rL()`); tournament/du
  paths ~L1340-1430 (`N4`, `hAa`, `pmb`, `qmb` — exact lines OPEN).
- Unseeded draws on AI-adjacent paths: `at.RGa→Math.random` (L~1153;
  `uf.OKa=new at`); `uf.sja` at L674 (`Gc.DK`) and L733 (context OPEN);
  `uf.RJa` at L115, L531-532 (contexts OPEN); `oa.eT` at `Ie.Gb`
  (L1146-1153) + particles. `Math.random` override in the harness is
  therefore load-bearing for determinism (recorded in trace headers).

## 6. Input path (record/replay reference)

- `Df` (L453): `{control, index}` only (`NCa` = 1-8 directional,
  `k$a` = 14). Subclasses: `vl` gamepad (`Gfa()=2`), `wl` keyboard
  (`Gfa()=1`), `Se` touch (`Gfa()=0`). **No coordinates survive past the
  device mapping — replaying (control, index, type) is sufficient.**
- `Za` (L453-459): `hS` press → `ca.N0a` (control≠0 + `DEa`), `n3`
  release → `ca.O0a`; `DEa` lesson gates: keyboard 9/10 (`wra/mra`),
  touch 1-8 (`zra`), 11 (`xra`), 12 (`ora`). Keyboard map (L457):
  controls 1-8 (arrows via `sc.OD`), 9,10,11,12,13,14. Gamepad map (L458).
- `Nc.Lh` (L461): `H7a(Gfa(), index)` → registry → `jxb` → `E7a` → fighter.
  Replay objects need `{control, index, Gfa()}` — plain object + closure.
- `ze` stick (L463-471): `GBa` maps deflection to sectors 1-8
  (`nia` down / `Qgb` move / `oia` up → `tia`/`uia`/`y3` bus).
- `ca.N0a` (L426): `LBa` mirror remap → phase1 `WC` buffer / phase2 `yJa`.
  `O0a` (L426): release (`WC` clear + `Gmb`). `llb` (L429): phase-transition
  flush `yJa(WC)`. `yJa`/`Gmb` (L501): 11 punch / 12 kick gates, 9/10/14
  gates, else `Kl.Sgb`/`Xgb` buffer (`zl`, L797-799; `Lea`, L512/680/799).

## 7. Tick / timer / fight loop

- `Us` (L135): `Gy=Bm=1/60` fixed step; accumulator with 0.25 clamp
  (`xeb`, exact line OPEN). `ca.ia` (L388): `frame++` + `Hnb` (per-fighter
  `c.ia()`, `Anb`, `Bg.ia()` tactics, `Bg.Bx`).
- `Sf.iPa` (L2036): `--xU; NF=xU/60|0`; `xU=round.gma*60+1` on reset.
  Classes: `ca` fight controller (L379), `Sf` fight screen (L2033),
  `Te` fighter body (L545), `wd` fighter logic (L490), `Gc` event bus (L669),
  `Ue` feature context (L643), `P.sp` frame quantum (L598/L623).

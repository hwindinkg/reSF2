# Combat static spec (web build) — extracted from `reference/www/sf2.502f0946.js`

Static-only. Anything not resolvable without a runtime trace is marked
`OPEN (needs runtime trace)`. Line numbers are exact for the current file.
Companion: `reference/AI_STATIC.md` (AI tree; `Gc.DK` choice cited there §4
is detailed here §5.1.5). Golden harness: `reference/tools/combat_golden.js`
(transcribes the pure gates below verbatim; run `node
reference/tools/combat_golden.js`).

Classes (minified names): `wd` fighter logic (L490), `Te` anim-interval list
(L545; `yD/hT/SZa` L553-554), `Cl` geometric hit-tester (L566; `ia` L566-567),
`Gc` move/reaction selector (L669; `DK` L673-674), `fe/Ul` interval defs
(L773-778), `Eh` damage-factor pair (L1180), `hw` Shock config (L1194-1196),
`ca` fight controller (L379; `Enb/tKa` poll L390, `Sba/Egb/Dgb/Cgb` L393-396),
`pu` per-hit record `Bb` (L543), `fw` action appliers (L1289; `Yob/ppb`
L1294-1295).

## 0. Interval types and containers

- `fe` ctor/init (L773): `type` passed in; `init()` remaps by anim Name:
  `Unstable→1`, `Uninterrupt→2`, `SelfUninterrupt→3`.
- `fe.G0(name)` (L774): `Attack→4`, `Block→5`, `Invisible→7`,
  `Invulnerable→6`, else `0`.
- `Te.yD(type)` (L553): first `xj` entry with `.type==a`, else null.
  `Te.cBa(name)` (L554): by `.name`. `Te.hT(type)` (L554): removes ALL
  entries of type (reverse loop). `Te.F4(name)` (L554): removes by name.
  `Te.Hja(names)` (L554): `F4` each. `Te.SZa(names)` (L554): true if ANY
  name present.
- `Ul extends fe`, `super(4)` = Attack interval (L774). Ctor defaults (L774):
  `jga=false, Xb=0, a3=false, HC=null, kw=gR=hR=0, sP=0, DDa=DL=false,
  iga=hga=null, SZ/KP=[]`. `Ul.B8a(frame)` (L775): hit-part name whose
  `[start,end]` window contains the frame. `Ul.XL/Cea` (L775): charge
  multipliers `k$/FV` selected 1/2/else-default.
- `Ul.J3` parse (L775-776): `DL=!NoEffect` (default true);
  `IgnoresBlock` → `DDa=true, hga=Name.split("|")`;
  `IgnoresInvulnerable` → `jga=true, iga=Name.split("|")`;
  `AttackingParts` → `jba[]`, `aEa=jba.length>0`.
- `Ul` damage parse (L777-778, via `qjb` L777-778): `Hit` children →
  `Wsa[]` windows; `Impulse` → `kw/gR/hR`; `Combo` → `sP` (Time, else 0);
  `Damage` → `Xb` (Value), `a3` (NoCritical), `HC` (BodyPart, else "");
  children `Damage` → `SZ.push(Ba(Type,Shift))`, `Defense` → `KP.push(Type)`.

## 5.1 Block / parry

- Presence: `wd.qYa()=da.yD(5)`; `wd.Nbb()=qYa()!=null` (L514).
- `strike()` order on TARGET (`this`=target, `e`=attacker, `g`=attacker's
  `yD(4)`, L509-510): `Vb.data=g; Vb.UC=g.B8a(e.M0())`;
  **block-break first**: `g.DDa&&(g.hga.length==0?this.hT(5):this.Hja(g.hga))`
  (L509) — empties target's own Block intervals (all, or named);
  then `Bb.block=this.Nbb()`; `Bb.JP=wd.LAa(g,block,a)` (L510).
- `wd.LAa(a,b,c)` static (L536): `KP=a.KP; KP.length>0→KP[0]`;
  else blocked→`v.pYa` (BlockDefense attribute name, L1156);
  else weapon `Xi` non-empty→`Xi`, else `v.lNa` (SlowMotion.Defense, L1156).
  `JP` is a *defense attribute-name string*, consumed by `$db(Zi,i6a,JP)`
  (L395; `$db` appends to `i_` L523; `i6a` picks max-Shift `SZ` Type, L430).
- Blocked consequences (L510): `block||(e.JCa()||this.Era++,e.dca())`
  (`JCa`=combo-counter head else `Vx.v1`, L525; `Era` init 0, L490);
  `se=!block&&!g.a3&&v.Lcb(this.A9a())` (L510) — blocked ⇒ never crit;
  `A9a()=pga?100:gya.p8a(jb)` (L529), `Lcb(a)=Da.cT(a*100)` (L1204;
  `Da.cT` shortcut `a>b→true`, AI_STATIC §5).
- Post-hit break (L395, `ca.Cgb` tail): `b.block||(a.model.hT(5),this.Dga=!0)`
  — an UNBLOCKED hit strips the victim's Block intervals and latches
  `Dga` (round-scoped: init false L380, reset per round L409).
  `b.ep=!this.Dga` (L394) ⇒ `ep` = first landed hit of the round.
  Damage intake: `lrb(bk,dir,se?1/60:1/120)` iff `DL` (L395);
  `se&&Rub(...)` (L395); `Gzb(...)` HUD hook (L395); `Zi` spend via
  `aM→Laa` (L391); `Jma(c,a.Pd,block,se,!1)` magic-charge intake:
  `Hwa(pow(2,e)*c*b*a)+LA()` with `e=j6a/W7a`, `c=kea`, `b=qea` (L521;
  `j6a=PainRecharge`-ish, `W7a`, L1186 — attr names OPEN).
- Reaction choice `Gc.DK(a,b,c)` (L673-674): partition candidates into
  `d` (usable now: `c||!h.eb||h.animation.Rha`), `f`/`g` by `Rha` via
  `Aua` (priority keep-highest, L673); `f.length>0&&(e=f[sja(len)])`;
  `g.length>0&&Ukb(g[sja(len)].animation)` (`sja=floor(RGa()*a)`,
  `RJa=RGa()`, L115; `RGa→Math.random`, AI_STATIC §5);
  `d.length>0?(e&&d.push(e),Pkb(a,d)):e&&(MS?jJa:Nsb+Ek+sign...)`
  (L674). `Pkb/Nsb/jJa/Ukb` geometry+priority tails: OPEN (needs trace).
  (AI_STATIC §4: this is the same `DK` that picks post-hit reactions.)
- Scripted breaks: `Yob`: `Vz!=""→hT(G0(Vz))` (by TYPE) else `mw!=""→F4(mw)`
  (by name) (L1294). Net replay: `ppb` copies `se/Ub/Yi/block` + `bR/Zi`
  from action when `>-1`/non-null (L1294-1295).

## 5.2 Combos (chaining)

- Poll: `ca.Enb` (L390) calls `tKa` on every `Ra` each frame, alternating
  direction (`frame%2`). `wd.tKa(a)` gate (L499):
  `PCa()&&!kh&&da.Ua!=null&&!Nd.nk` else false → `HZa(jb,a)`.
- `wd.HZa(a,b=false)` (L500-501):
  `d=da.yD(4)`; null ⇒ false. Else (`a`=target, default `jb`):
  `(target.yD(6)==null || (d.jga&&d.iga.length==0) ||
  (d.jga&&target.SZa(d.iga)))` AND `Fu.ia(target.oa, da.GY, d)` ⇒
  `(b||Kwb(a,d), c=true)`; return `c`.
  I.e.: no chain while target holds an Invulnerable interval unless the
  attack bypasses it (named-part bypass via `iga`⊆target anims, or empty
  `iga` blanket bypass); `b=true` = test-only (no impulse).
- `Cl.ia(a,b,c)` geometric test (L566-567): `dW==c→false` (one-shot per
  attack object); `!c.aEa→true` (no AttackingParts = always connects);
  else body-part volumes `W1a` over `b` (`GY`) list; records `strike`
  `KD/Py/n$/o$` on success. Volumes/`Bz` math: OPEN (needs trace).
- `Kwb(a,b)` (L509): impulse `H(kw,gR,hR)` from the INTERVAL, mirrored by
  `da.hd()`, scaled by `JG`, `sP` copied; then `target.strike(...)`.
- Windows: `B8a(M0())` names the hit part (L509-510); `sP` = combo-link
  Time from XML (L777). `XL(qb?2:1)` advances charge side (L511).
- NOT chaining: `v.ntb MinHits / v.otb Time` (L1155) feed the HUD combo
  counter / `ERuleWinCombo` (L392-393) — no effect on `HZa`.

## 5.3 Crit / shock-knockdown / disarm / weapon

- Crit flag: `Bb.se` set pre-damage (L510, §5.1); `a3` (NoCritical) forces
  false twice: pre-roll (L510) and `Cgb` head `c.a3&&(b.se=!1)` (L394).
  Factor: `kea(flag)=IAa(flag,VY.Mk,VY.Bc)`,
  `qea(flag)=IAa(flag,HZ.Mk,HZ.Bc)` (L513);
  `IAa(a,b,c)=a?pow(2,attr(b)*c):1` (L512-513);
  `Eh.parse`: `Mk=Attribute||"COM"`, `Bc=Base` (L1180);
  `VY=BlockDamageFactor`, `HZ=CriticalHit.Damage` nodes (L1156; defaults
  L2480). Values are data (OPEN).
- Damage `bCa(a,b,c,d,e)` (L513-514): `d=LAa(a,b,d)` RECOMPUTED (overwrites
  strike's `JP`); `h=pow(2,ACa()*min(zCa-attr,E9a))`;
  `b=kea(b)` (target, block flag), `c=attacker.qea(c)` (se flag);
  `g=iea(atk?qb:params,SZ,JP)`; `g=(Xb+atkLy)*g*b*c*h*UZ`;
  `max(0)`; `c2a` cap; `*=Cea(atk?qb?1:2).bp`; `*=dta`; `*=so`.
  `iea→l5a(k)=pow(2,k/BP)` (L1206), `BP=DamageDoublingRange.Value` (L1156);
  `pAa` armor interpolation (L1204-1205) is data-driven (OPEN values).
  Health clamp in `Cgb` (L394): `gd<bR→Zi=gd+.01,Iza=true`.
- Crit/shock decider `wd.R8a(attacker)` on target (L531-532), verbatim:
  `ecb→true` (`ecb=false`, L2475); `vc→false`;
  `b=Zi/atk.so`; `c=Orb(ws?0:b)` (weapon strikes accumulate 0);
  `a=iya*hya-attr`; `d=pDa*oDa-attr`; `e=f=false`;
  `se&&(e=a*b>uf.RJa())`; `Uq&&!block&&(f=d*b>uf.RJa())`;
  `return (c||f)?true:e`.
  Single return feeds BOTH `Bb.Ub=R8a(e); Bb.Yi=Bb.Ub` (L511).
  `Uq=(HC=="Head"||weaponHC=="Head")` (L511).
- Pain: `Orb(a){sr+=a; return !vc&&sr>threshold}` (L517);
  decay `Pnb`: `sr=max(sr-Xza,0)` per frame (L528).
  `hw` (`v.Ub`) parse (L1194-1196): `threshold=Treshold.Value`,
  `Xza=FrameReduction.Value`, `MFa=LooseningDelay.Frames`,
  `Au=Weapon.Name`, `EPa/FPa=SetAttribute Name/Value`,
  `iya/hya=CriticalHitChance Base/Attribute`,
  `pDa/oDa=HeadHitChance Base/Attribute`, `kw/gR/hR=Impulse X/Y/Z`.
  (`v.Ub` wired at L1158.)
- Shock apply `Cgb` (L394): `b.Ub&&(vc?b.Ub=false:(vc=true))`.
- Disarm apply `Cgb` (L394): `b.Yi&&(d=$b(Au);
  sn||d?.Hd.name==ownHd ? Yi=false : (sn=true, kwb()))`.
  `kwb(): Wx<0&&(Wx=MFa)` (L522). `Pnb` timer (L528):
  `!vc&&Wx>=0&&(Wx==0&&Wqb(),Wx--)`.
  `Wqb()` (L527-528): if `!vc`: swap to `Au` item (`$o(b,true,true,false)`
  + `EPa/FPa` attr set), `vc=true`, fling held parts by `kw/gR/hR÷weight`,
  `nf.Wsb(Yb)`, drop event. Init: `sr=0,vc=sn=false,Wx=-1,ws=false` (L490);
  `ws` via `ola` (L493; clone L536) — setter source OPEN.
  C-post (L396): `se||(Uq&&!block)||Ub→ZAa→DL` slow-mo; `ow=se; vc=Ub`;
  `Ih(6/7)` event bus; `fe.wqb/Cqb` HUD; punchbag rule (`sI==Qxa→vc`, L396).
- EVENT flags `ca.Sba(a,b,c)` (L393): `Defense=JP, Animation=aI,
  Critical=se, Shock=Ub, Block=block, Damage=Zi` → `Gj(a,c)`.
  Stages: `Egb→5` (pre-damage, L393-394), `Dgb→6` (L394), `Cgb→7` (L394),
  magic `Gj→8` (L396). Golden asserts stage-7 vectors.
- `Bb` record fields (`pu`, L543): `block/se/Ub/Yi/Uq/ep/Iza, Zi/bR,
  JP/aI/KD/Py/Gva/ODa, target, fg/bk/nJa, CJa[], r0a`.

## OPEN (needs runtime trace)

1. `Cl.W1a/Bz` volumes, `Pkb/Nsb/jJa/Ukb` reaction geometry (L567-568,
   L674-676); `Gc.EZa/w_a` gating inputs (L676, AI_STATIC §4).
2. `A9a` crit-base values, `pga` setter (L529); `VYa/HZ` XML numbers
   (L1156); `pAa` armor tables, `ACa/zCa/E9a`, `Cea.bp`, `c2a` (L513-514,
   L1201-1206); `Jma` attr names (L521, L1186).
3. `wd.ws` setter beyond init/clone (L490, L493, L536); `Vx.v1/wgb`,
   `Era` reset scope (L510, L525); `Dga` per-round only (L380/L409 — confirm
   no per-exchange reset in a live fight).
4. `MFa/Xza/threshold/Au/EPa` concrete numbers (fight-settings XML Egypt —
   file id OPEN, AI_STATIC §2); `Wsb/m$/fe.wqb/Cqb/ZAa/DL` presentation tails
   (L396-397).

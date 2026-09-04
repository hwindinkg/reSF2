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

## APPENDIX A — resolved §OPEN data values (static, from disk + JS)

Appendix only: no restructuring above. All XML below is
`reference/extracted/xml/res/internal_settings.xml` unless noted; JS lines
are `reference/www/sf2.502f0946.js`. Values confirmed present on disk;
formulas transcribed verbatim.

### A1. `A9a` / `pga` (crit-chance base)

- `A9a(){return this.pga?100:v.gya.p8a(this.jb)}` (L529).
- `pga` init `false` (`this.pga=!1`, `wd` ctor L490); the ONLY setter is the
  debug cheat `case 22: b.pb.pga=!b.pb.pga` (L438). Shipped runtime value:
  `pga=false` ⇒ `A9a()=gya.p8a(jb)`.
- `v.gya` (`Pv`) parsed from `<CriticalHit>` (L1157); `Pv.parse` (L1181):
  `sH=(Probability.Base, Probability.Attribute)`,
  `ps=(Damage.Base, Damage.Attribute)`;
  `p8a(a)`: attr present `? sH.first*attr.G : sH.first` (L1181-1182).
- XML: `<Probability Base="0.0001" Attribute="CriticalChance"/>`,
  `<Damage Base="0.0001" Attribute="CriticalDamage"/>`.
- Resolved: normal crit base = **0.0001** (× `CriticalChance` attr value when
  the fighter has it); `pga=true` forces **100** (debug only).

### A2. `VY` / `HZ` (block / crit damage factors)

- `Eh.parse`: `Mk=Attribute||"COM"`, `Bc=Base` (L1180).
- `v.VY←<BlockDamageFactor>`, `v.HZ←<CriticalHit><Damage>` (L1156).
- XML: `<BlockDamageFactor Base="0.0001" Attribute="BlockDamageFactor"/>`;
  `<Damage Base="0.0001" Attribute="CriticalDamage"/>`.
- Resolved: **VY=(Mk=BlockDamageFactor, Bc=0.0001)**,
  **HZ=(Mk=CriticalDamage, Bc=0.0001)**; consumed via
  `IAa(flag,Mk,Bc)=flag?pow(2,attr(Mk)*Bc):1` (L512-513).
- Bonus (same lines): `v.pYa` (`LAa` block fallback, L536) =
  `<BlockDefense Attribute="BodyDefense"/>` ⇒ **"BodyDefense"**;
  `v.lNa` (unblocked fallback, L536) = `<SlowMotion Defense="BodyDefense"/>`
  (L1154) ⇒ **"BodyDefense"**; `v.BP` (`l5a` divisor, L1206) =
  `<DamageDoublingRange Value="10"/>` ⇒ **10**;
  `v.lT` (resistance path, L1422) =
  `<ResistanceDoublingRange Value="500"/>` ⇒ **500**.

### A3. `pAa` armor interpolation (tables, L1204-1205)

- Verbatim signature: `pAa(a,b,c,d,e,f,g,h)` — `k=Bh.Gb(wv,e)` align target,
  `Ci.a5a((a?c:b).IY,x)` candidate deltas, per-`SZ` loop with
  `bp/shift` blend, `v.eNa` eclipse filter, `v.Seb.g6a(C)` aspect gate
  (`Aspect DoublingRange="108" Limit="1.2" Antilimit="0"`), `v.BP` log-scale
  clamp; double-call guard `k>10&&(k=pAa(...))` in `iea` (L1205-1206).
- `v.wv` (`AlignTargetAttributes`, L1157): **WeaponDamage 12,
  UnarmedDamage 0, BodyDefense 12, HeadDefense 5, RangedDamage 12,
  MagicDamage 12, EnchantmentResistance 12**.
- Per-warrior rows (`stages.xml` `<AttributesAlign><Delta Factor Shift
  Priority>`): Punchbag `<Delta Factor="1" Shift="0"/>`; Dojo_Disciple /
  tutorial `Man_Kungfu` `<Delta Factor="0" Shift="0" Priority="1"/>` +
  `<Delta Factor="1" Shift="-10" Priority="1"/>`.
- Per-fight shifts (`stages.xml` `<Rules><Attributes DamageFactor="±N"
  ApplyTo="Player|Bot">`): e.g. `2000/-2000` (Zone 1 boss), `-3219`,
  `-4219/-2219` (survival ladder). These feed the same `DamageFactor`
  attribute `pAa` reads.

### A4. `ACa` / `zCa` / `E9a` (raid/attr damage exponent, L513)

- Setters (L1201) + wire-up (L1155):
  `utb(DamageFactor.Base)→v.Ypa=ACa`,
  `ttb(DamageFactor.Attribute)→v.Xpa=zCa`,
  `vtb(DamageFactor.MaxValue default 2E4)→v.Zpa=E9a`.
- XML `<DamageFactor Base="0.0001" Attribute="DamageFactor"/>` carries **no**
  `MaxValue` ⇒ default applies.
- Resolved: **ACa=0.0001, zCa="DamageFactor", E9a=20000**.
  (JS defaults L2480: `Ypa=0, Xpa="", Zpa=2E4` — overwritten by parse.)

### A5. `Cea.bp` (charge multiplier, L514/L775)

- `Vm` ctor: `bp=Rja=1, JU=KU=false` (L778);
  `XL()`: `JU&&KU&&(bp*=Rja)`.
- `bn` (`ERuleDamageFactor`) parse (L856): `zUa=Factor default 1`,
  `lVa=RepeatFactor default 1`.
- Disk: **no `ERuleDamageFactor` element in any extracted XML**
  (grep over `reference/extracted/**/*.xml` = 0 hits) ⇒ `JU/KU` never latch
  in shipped data ⇒ **bp=1 always** (static; a runtime trace would only
  confirm absence). (`perks.xml` `DamageFactor=` mods are `ModAttributes`, a
  different path.)

### A6. `c2a` (fists cap, L514/L820)

- `c2a(a,b)`: any `a[]=="Fists" ? b*this.M_ : b` (L820);
  `M_=FistsDamageMod default 1` (L187 `u.H(...,1)`).
- Disk: **no `FistsDamageMod` in any shipped XML** ⇒ `M_=1` ⇒ **`c2a`
  is the identity** for all shipped data.

### A7. `Jma` charge attrs (L521/L1186)

- `Jma(a,b,c,d,e)`: `e ? (e2=j6a, c=kea, b=qea) : (e2=W7a, …)`,
  then `Hwa(pow(2,e2)*c*b*a)+LA()` (L521).
- `Yv`: `j6a=JAa=DamageRecharge`, `W7a=yBa=PainRecharge`,
  `a7a=InitialCharge` (L1186); `AQ(name,params)=params? Bc*attr(Mk) : Bc`.
- XML `<Magic>`: `InitialCharge Base=0.0001 Attr=MagicInitialCharge`;
  `PainRecharge Base=0.0001 Attr=MagicPainRecharge`;
  `DamageRecharge Base=0.0001 Attr=MagicDamageRecharge`.
- Resolved: **j6a→(MagicDamageRecharge, 1e-4)**,
  **W7a→(MagicPainRecharge, 1e-4)**; `kea/qea` reuse A2 `VY/HZ`.

### A8. Shock (`hw`/`v.Ub`) numbers + settings file id

- Settings root: `td.wjb(Ja.ki(1292))` (L1153); the same `wjb` parses
  `Attributes/RatingEvaluation/DifficultyEvaluation/SlowMode/SlowMotion/
  DamageFactor/BlockDamageFactor/CriticalHit/Shock/Camera/…` (L1153-1158).
  On-disk content match for ALL of these nodes is
  `reference/extracted/xml/res/internal_settings.xml`
  (numeric bundle id `1292` itself is loader-resolved — id→file proof needs
  a runtime trace of `Ja.ki/G.qf`, still OPEN only for that mapping).
- `<Shock>` (parse L1194-1196): **threshold (`Treshold.Value`) = 999**;
  **Xza (`FrameReduction.Value`) = 0.001**; **MFa
  (`LooseningDelay.Frames`) = 12**; **Au (`Weapon.Name`) = "Fists"**;
  **EPa/FPa (`SetAttribute Name/Value`) = WeaponDamage/0**;
  **iya/hya (`CriticalHitChance Base/Attribute`) = 0.0001 /
  "ShockCriticalHitChance"**; **pDa/oDa (`HeadHitChance Base/Attribute`) =
  0.0001 / "ShockHeadHitChance"**; **kw/gR/hR (`Impulse X/Y/Z`, X,Z absent)
  = 0 / -0.5 / 0**.

### A9. `Cl.W1a` / `Bz` hit volumes (L567, L12-14)

- `Bz(a,b,c,d,e,f,g,h,k,l)` verbatim (L12 + continuation L13-14):
  `c+=f; if(ic.f2(c)) return Cz(d,e,a,b,g)?(…,!0):!1;`
  `l??=Wy(d,e); n=l·a+c; f=l·b+c;`
  `if(0<=n*f && c<|n| && c<|f|) return !1;`
  `k??=Wy(a,b); q=k·d+c; r=k·e+c;`
  `return 0<=q*r && c<|q| && c<|r| ? !1 : q*r<0 && n*f<0 ? (lerp, !0)
  : Ls(n,…) ? … : Ls(f,…) ? … : Ls(q,…) ? … : Ls(r,…) ? … : !1`
  (`Wy`=line normal, `Cz`=2-D segment intersect L14, `Ls`=point project —
  all static, L12-14).
- `W1a(a,b)` (L567): for each body part `k` of `a=Nl.oI` with `k.vZ`:
  `Bz(d,e,c, k.Ula,k.Pda,k.gb, W8,X8, f,k.Eda)` → on hit
  `zXa` records `strike.Py/KD/n$/o$` (L567-568).
- Segment fields (`yu`, L791-793): `Ula/Pda` endpoints via `Uy()` blend,
  `gb` radius, `Eda` normal, `vZ` enable flag (toggled by `hq` type-30,
  L1403); margins (`zu`, L793): `Margin1/Margin2/Radius1(×2=stroke)`.
- Resolved (static): full test expressions above. **Per-frame segment
  positions/values stay OPEN (needs runtime trace)** — they live in animated
  skeleton nodes, not on disk.

### Remaining OPEN (unchanged)

`Pkb/Nsb/jJa/Ukb` reaction geometry (L674-676); `Gc.EZa/w_a` gating inputs
(L676); `wd.ws` setter beyond init/clone (L490/L493/L536); `Vx.v1/wgb`,
`Era` reset scope (L510/L525); `Dga` per-round-only confirm (L380/L409);
`Wsb/m$/fe.wqb/Cqb/ZAa/DL` presentation tails (L396-397);
`Ja.ki(1292)`→file numeric proof (§A8).

### APPENDIX B — magic / charge-bar data locations

Verdict: Stream 3 is confirmed — **no `magic/*.json` ships under
`reference/www/res`** (dir listing + `G.rq` manifest, L2490, have no magic
entries). Magic visuals resolve only through the asset loader:
`G.qf("magic/"+fileName+".png")` preload (L730, `Yl.preload`) and
`G.qf("magic/"+fileName+".json")` + `.png` at effect spawn (L838-839);
models live in `magic_dds/ktx.dat`. Gameplay numbers live here:

- `internal_settings.xml` `<Magic>`: `InitialCharge/PainRecharge/
  DamageRecharge` all `Base=0.0001` (`jA`/`Yv` tables, L1186; `AQ`
  name→`Bc*attr(Mk)`); `<StartingBullets Attribute="RangedQuantity">`;
  `Regeneration Base=0.000001 Attr=RegenerationRate`,
  `Lifesteal Base=0.0001 Attr=Lifesteal` (`Bja/kha`, L1158).
- `list.xml` Magic rows (35 extracted / 41 bundle): `MagicDamage/Level/
  Price|BonusPrice/PaidItem`, e.g. `MAGIC_AE21_SPIRIT_PILLAR`
  (dmg 5, lvl 1, 105 gems, Paid).
- `tactic_settings.xml` `<MagicAnimations>` (referenced `P.Lra`, L628).
- Fighter charge state (no data table — live only): `my` charge 0..1
  (`yL` clamp, L494), `bh` bullets (`zL`); spawn `yL(vo.vE)/zL(vo.cl)`
  (L402); `yKa`: `zL(0)+yL(InitialCharge)` (L504);
  `Hwa(a)`: `bh==0 && yL(my+a)` (L504-505); `LA()`: `my>=1 → hZ(1)`
  bullet, `bh>1 → zL(1)` cap (L505); intake via `Jma` (§A7).
  `ju` regen pools (`UNa=500…`, L545) tick in `MOa` (L532).
- Conditions/events: `MagicBullet→bh` / `RaidChargeBullet→dO` (`lp`,
  L1313); `MagicCharge` (`sp`, L1303); `MagicCharged` (L1318),
  `PerkEventMagicCharged` (L1319); quest probes `MagicBullet/MagicCharge`
  (L1339).
- Ranged ammo: `K0()` (`NoRanged?1:-1`, L505); `StartingBullets` has no JS
  literal — resolution OPEN (needs runtime trace).

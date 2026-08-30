# JS_MAP — Shadow Fight 2 Web Engine Structure Map

**Source:** `reference/www/sf2.502f0946.js` (1,601,954 bytes, 2531 very long lines, Haxe 4.3.7 output)
**Companion:** `reference/www/microsite-game-interface.js` (GameInterface bridge, skimmed)
**Purpose:** authoritative reference for (a) per-frame state-tracer instrumentation and (b) a later native C++ port.

All line numbers are 1-based against `sf2.502f0946.js`. Class names are the mangled Haxe identifiers; `g="XX"` is the Haxe class id, `u=Parent` the inheritance link. Methods are listed as `name()` with the line of their definition.

---

## 1. Main Loop (frame driver)

### 1.1 The rAF ticker — `Qg` (g="47", L135)

```js
class Qg {                       // L135
  constructor(){ this.startTime=this.Oia=this.counter=0; this.first=!0; this.now=0;
                 this.handle=-1; this.active=!1; this.y4a=60 }
  Qh(){}                         // per-frame callback, assigned by the app
  start(){ ... requestAnimationFrame ... this.Qh(e/1E3) ... }   // L135
  stop(){ ... cancelAnimationFrame ... }
  static Pha(a){ queueMicrotask(a) }
}
```

- `Pg.Sc` is a `Qg`; the app wires `Pg.Sc.Qh = Pg.xeb` at **L49** (`this.Sc.Qh=w(this,this.xeb)`).
- Each rAF tick calls `Qh(dtSeconds)` → `Pg.xeb(dt)`.

### 1.2 The app per-frame callback — `Pg.xeb(a)` (L56)

```js
xeb(a){ if(!this.VC) if(this.F_) this.Hda(); else {
  this.window.update();          // Kk window (canvas resize / context)
  this.plb.aa(a);               // Ts task scheduler
  for(PD) d.Y3();               // input screens' per-frame poll (Ik/Hk/lf/Jk/rf)
  this.time+=a;
  b=this.sk; c=w(this,this.aa); // Us fixed-timestep accumulator
  b.vp=!0; b.Gy+=a*b.NL; b.Gy>.25&&(b.Gy=.25);
  for(a=!1; b.Gy>=b.Bm;) b.txb++, c(b.Bm), b.Qza+=b.Bm, b.Gy-=b.Bm, b.vp=!1, a=!0;
  a&&(this.Ea(b.Gy/b.Bm), b.Z2a++) } }
```

- `Us` (g="46", L135): `Gy=.0166667` accumulator, `Bm=.0166667` (1/60 s fixed step), `NL=1` (time scale), counters `txb/Qza/Z2a`.
- **The game runs a fixed 60 Hz update** (`Pg.aa(1/60)`) decoupled from rAF rate; render happens once per rAF with an alpha blend factor.

### 1.3 Update pass — `Pg.aa(a)` (L57)

```js
aa(a){ let b=this.options.mha&&this.AFa(), c=0, d=this.PD;
  for(;c<d.length;){ let e=d[c]; ++c; e!=null&&e.state.update(a) }  // input states
  this.root.oja(a);             // scene graph update (Db.oja)
  this.Oh.notify();             // global event bus
  b&&this.Ha.rJa() }
```

### 1.4 Render pass — `Pg.Ea(a)` (L57)

```js
Ea(a){ if(this.Ha.vp()){ ... this.Ha.clear(); this.root.nja(a); ... } this.Ha.aQ() }
```

### 1.5 Scene graph — `Db` (g="1", L28-30), `O` (g="2", L30-31), `dd` (g="9", L37-40)

- `Db` — scene node base. `oja(a)` (L29) = **update pass**: `this.aa(a)` then recurse children; `nja(a)` (L29) = **render pass**: `this.Ea(a)` then recurse children. Fields: `active, J6, Yg, parent, af (firstChild), Ma (nextSibling), sfb, rfb, time, name`.
- `O` — display object: `Db` + `this.node=new Ea` (2D render node). `C(x)/D(y)/la(scale)` proxy to node.
- `dd` — 3D container (NOT a `Db`): `children[]`, `Zm[]` (components), `node=new Hd`. `xma()` updates children+components. `tWa()/vWa()/uWa()/wWa()` create mesh/capsule/model/other components. Used by `fighter.go` ("Model").

### 1.6 Screen manager — `mc` (g="41", L122-127)

- `mc.K` singleton; `stack[]` of `$d` screen states; `Taa(cls, caller, info)` pushes a screen; `aa(a)` (L124) updates the stack (transitions + `d.aa(a)` per active state); `Ea(a)` (L125) renders.
- `$d` (g="3F", L119-122) — screen-state base: `time, active, state(0..7), elements(Db "Elements"), content(Ea), node(Hd), info, Mr(manager), caller, system`. `aa(a){this.elements.oja(a)}` (L121), `Ea(a){this.elements.nja(a)}` (L121), `Te(n)` state machine, `jI(cls,info)` pushes a new screen.
- `ae` (g="42", L127-133) — screen transitions.

### 1.7 Per-frame chain (fight active)

```
requestAnimationFrame  (Qg.start, L135)
 └─ Qg.Qh(dt) = Pg.xeb (L56)
     ├─ Kk.update() (window)
     ├─ Ts.aa(dt) (task scheduler)
     ├─ input screens Y3() (Ik/Hk/lf/Jk/rf)
     └─ fixed 60 Hz: Pg.aa(1/60) (L57)
         ├─ input states update (Hs/Is/mf/se)
         ├─ root.oja(dt) (Db, L29)
         │   └─ mc.aa(dt) (L124) → $d.aa(dt) (L121) → elements.oja
         │       └─ ai.aa(dt) (FIGHT screen, L2006-2008)
         │           └─ case 5: Za.F().update(); ma.YL(Ig,dt) (L1832)
         │               └─ Ig.go.node.Cx(dt); Ig.go.node.tg(); Ig.Ea()   // ca.Ea (L385)
         │                   └─ ca.ia() (L388-390)   ← FIGHT UPDATE
         │                       ├─ per fighter in Ra: c.ia()  → wd.ia() (L498)
         │                       ├─ Enb() attack checks, Bg.ia() hit manager
         │                       ├─ Onb() round-end check (L411)
         │                       └─ ha.ia() HUD
         └─ Pg.Ea(alpha) (L57): root.nja(alpha) → mc.Ea → ai.Ea (L2008)
             └─ ma.Sya(Ig) camera setup (L1833); Za.F().Ea()
```

---

## 2. Fighter class — `wd` (g="C6", L490-536)

`wd` is the fighter/character. Subclass `ih` (L589) is the "sub-fighter" (summoned/extra body). `wd.fya()` (L535-536) is the factory that builds a `wd` (or cached `ih`) from params.

### 2.1 Constructor fields (L490-492)

| Field | Purpose |
|---|---|
| `parameters` (xc/El) | **fighter params: HP, position, equipment, flags** (see §2.3) |
| `da` (Te) | **animation controller** — `da.Ua` = current animation, `da.hd()` = facing (±1), `da.yD(4)` = attack interval, `da.yD(5)` = block interval |
| `oa` | physics body — `oa.Fe().ma` = world position (H), `oa.oL(pos)` = set position, `oa.Va.all` = body parts |
| `zP` (string) | **current animation name** (set by `z3()`, L517) |
| `Je` (int) | stance/phase: 1=StartStance, 2=Fight, 3=EndStance, 7=TryOn |
| `round` (int) | current round number (set by `Z2(a)`, L522) |
| `lb` / `jb` | current move (script) / its id; `HB[]` = move list |
| `me` / `Mo` | move arrays (melee / ranged) |
| `Bb` (pu) | **hit record** — `bR`=raw damage, `Zi`=final damage, `block`, `se`=critical, `Ub`=shock, `Uq`=head hit, `fg`=hit position, `Pd`=target, `Iza`=lethal |
| `Vb` (Ri) | event data carrier (`data`, `model`, `Pd`) |
| `Kl` (zl) | fighter event bus (events 0/1 → `mS`/`nS`) |
| `Cn` (tu) | mods/effects manager |
| `nf` (de) | weapon controller |
| `Nd` (Al) / `IH` (Bl) / `Fu` (Cl) | movement / hitbox / melee-attack controllers |
| `i_` (yl) | **damage accumulator** (fed by `$db`, drained by HUD) |
| `Qta` (ht) | timer |
| `Vx` (iu) | sub-fighter state (used when `lb==null`) |
| `go` (dd "Model"), `MW` (Fk mesh), `Jba` (Ek capsules) | 3D scene nodes |
| `color` (H), `JG` (H), `isVisible`, `pga` (god mode), `sn` (disarmed), `vc` (weapon-in-hand), `ws` (invulnerable), `GM`, `Era` (hit counter), `sI`, `sr`, `my` (move progress 0..1), `bh` (magic bullets), `dO`, `yV`, `so`, `xda`, `wN`, `tG`, `Wx`, `UG`, `CI`, `dz`, `Iqa`, `sP`, `index`, `kaa` (event counter), `cacheName`, `Fc` (Ae), `Ba`, `Ef[]`, `vd[]` (body parts), `QW` (Map), `UTa` (Map), `Ita`, `Koa`, `xpa`, `dta`, `rR`, `Yi`, `ow`, `pR`, `gm`, `sN`, `cache` (su) |

### 2.2 Key methods

| Method | Line | Purpose |
|---|---|---|
| `init()` | L495-496 | build from `parameters`: `oL(parameters.position)` sets spawn |
| `Ulb()` | L496-498 | full rebuild: creates `da` (Te), `nf` (de), `Nd/IH/Fu`, wires `Kl` + `da` events |
| `ia()` | L498-499 | **per-frame update**: queued-animation handling, `da.ia()`, `Nd.ia()`, `oa.v6()`, `oa.Qja()`, `Qta.Ea()`, `MOa()` (magic regen), fires `nr` event with `Vb.data=kaa++` |
| `oL(a)` | L501 | **set position**: `oa.oL(a); Nd.jE(); oa.v6(); oa.Qja(); oa.CKa()` |
| `z3(a,b,c)` | L517 | **set current animation name**: `dz=a; zP=b; CI=c` (called by `ca.z3`, L423) |
| `strike(a,b,c,d,e,f)` | L509-511 | execute attack: fills `Bb`, computes `Bb.bR=bCa(...)`, `Bb.Zi=bR`, then `ca.Cgb(Vb)` |
| `bCa(a,b,c,d,e)` | L513-514 | **damage formula** (see §3.3) |
| `$db(a,b,c)` | L523 | queue damage into `i_` (yl) for HUD display |
| `du(a)` | L522 | `parameters.du(a)` → HP set (see §3.4) |
| `hT(a)` / `F4(a)` / `Hja(a)` | L521 | play hit / knockback / stagger animation via `da` |
| `Jma(a,b,c,d,e)` | L521 | damage-number popup + magic-bullet logic |
| `Z2(a)` | L522 | set `round=a`, reset stance |
| `bw()` | L532 | fighter id: `0` = `ca.yb` (enemy), `1` = `ca.pb` (player), `-1` otherwise |
| `K0()` | L505 | ranged flag: `parameters.ig.Yb=="NoRanged" ? -1 : 1` |
| `A9a()` | L529 | crit chance source: `pga?100 : v.gya.p8a(jb)` |
| `R8a(a)` | L531 | shock/knockdown roll |
| `h7a(a)` | L528-529 | build default `El` params template |
| `fya(a,b,c,d)` static | L535-536 | fighter factory (cache-aware) |
| `LAa(a,b,c)` static | L536 | block/parry result lookup |
| `mea(a,b)` static | L535 | compare fighters |

### 2.3 Fighter params — `xc` (g="166" base, L804-820) / `El` (g="166", L822)

`El extends xc` (adds `pp` perk, `gr`). `wd.parameters` is an `El`/`xc`.

| Field | Purpose |
|---|---|
| `gd` | **current HP** |
| `Zn` | **max HP** |
| `position` (H) | spawn position |
| `qb` (bool) | **is player** (true=player, false=enemy) |
| `$s` | fighter name |
| `level`, `EP`, `Xb`, `PP`, `UZ`, `M_`, `w4`, `n6`, `ng`, `number`, `Iq`, `ul` | stats / progression |
| `Of` (weapon), `Hd` (weapon), `hg` (helm), `Lg` (armor), `ig` (ranged), `Mg` (magic) | equipment (item refs) |
| `attributes` (ud) | attribute map (damage multipliers etc.) |
| `Oa[]`, `lx[]`, `TE[]`, `AK[]`, `items[]`, `groups[]` | item/group lists |
| `Gc` | magic item |
| `ZV` (bool) | invulnerable flag (debug) |
| `br`, `cE`, `kh`, `zd`, `sn`, `sJ`, `pw`, `Fj`, `wu`, `QD`, `lR`, `voice`, `fK`, `eP`, `aB`, `L5`, `vba`, `W3`, `C_`, `Zia`, `Cda`, `$ia`, `Dda`, `random`, `zt`, `node` | misc flags/data |

Key methods: `du(a)` (L816) — **HP setter** `gd = ZV?Zn : clamp(a,0,Zn)`; `Laa(a)` (L816) — `du(gd+a)` (damage/heal); `jT(a)` (L817) — heal; `uob()` (L817) — full heal; `Jfa()` (L817) — `gd<=0` (KO check); `c2a(a,b)` (L820) — armor reduction; `Fd(type)` — equipment by slot; `hk(type,item)` — equip; `AMa(a)` (L816) — set `ZV`; `nob()`/`Pma()` (L819) — activate/deactivate items; `clone()` (L817).

---

## 3. Fight controller — `ca` (g="AC", L379-433)

`ca` is the battle controller. One instance per fight; `ca.h8` = current instance (L379); `ca.Ka()` = current instance accessor (used everywhere).

### 3.1 Constructor fields (L379-382)

| Field | Purpose |
|---|---|
| `pb` (wd) | **player fighter** |
| `yb` (wd) | **enemy fighter** |
| `Ra[]` | all fighters in the arena (incl. summons) |
| `kc` (xc) | player params |
| `Zb` (xc) | current enemy params (`pf[Rk]`) |
| `pf[]` (xc) | enemy params list (multi-fight battles) |
| `round` ($t) | **round state**: `round.round` (number), `round.time` (fight timer), `round.eL` (round length), `round.gma`, `round.Vt` (running), `round.Mcb` (raid) — class `$t` at L1239 |
| `eu` (int) | **fight phase**: 0=idle, 1=StartStance, 2=Fight, 3=EndStance |
| `frame` (int) | frame counter (incremented in `ia()`) |
| `wn` (bool) | pause |
| `yt` (bool) | slow-mo; `KX` slow-mo timer |
| `Da` (dl) | battle instance (type, location, fight list) |
| `ha` (Ar) | HUD |
| `Ta` (ql) | fight camera (`Ta.ia` = Ut controller) |
| `ud` (du) | battle manager (rounds, sides) — created in `N1a()` L416 |
| `fe` (Vt) | fight rules/achievements |
| `ze` (Zt) | round-result tracker |
| `tb` (bc) | event bus (`tb.Gj(model,type,flag)` dispatches; `tb.mg` context map) |
| `Bg` (Gc) | hit/event manager |
| `location` (Bf) | arena (spawn points `Yia`/`B_`, bounds `NU`/`width`) |
| `xX` (cu) | hitstun timer; `mV` (bu) flash timer |
| `wo` (Yt), `Pm`/`vo` (rl) | round-state snapshots (HP, move progress, magic) |
| `pya` | last hit record |
| `kh`, `Dga`, `uN`, `JJ`, `xJ`, `h9`, `cY`, `TG`, `pW`, `Bra`, `o9`, `lW`, `QG`, `Iga`, `dY`, `m$`, `vR`, `Wga`, `sga`, `Ncb`, `X1`, `T9`, `P8`, `g9`, `LEa`, `ey` | fight flags |

### 3.2 Key methods

| Method | Line | Purpose |
|---|---|---|
| `ggb()` | L383 | fight start: `round.round=0` |
| `Ea()` | L385 | **per-frame update**: pause→HUD layout; else `nzb()` (AI), `ia()`, `xX.Qh(this)` (hitstun), `mV.Qh(Ta)` (flash) |
| `ia()` | L388-390 | **main fight update**: `frame++`; `Hnb()` updates each fighter (`c.ia()`), `Enb()` attack checks, `Bg.ia()`, `Onb()` round-end, `ha.ia()`, `Nw()`, `kob()`, slow-mo timer |
| `xF(a)` | L388 | set phase `eu=a`; propagates `Je` to fighters |
| `wA/vA` | L385 | interval start/stop events |
| `Pf` | L386 | animation-start event (end-of-fight logic) |
| `kg` | L387 | animation-stop event |
| `nr` | L387 | step-frame event |
| `Cgb(a)` | L394-397 | **hit/damage application** (see §3.4) |
| `Sba(a,b,c)` | L393 | dispatch hit event (Defense/Animation/Critical/Shock/Block/Damage context) |
| `Egb/Dgb` | L393-394 | pre/post damage events |
| `aM(a,b)` | L391 | can-act check + `Laa(b)` (HP delta) |
| `VOa(a,b)` | L391 | player-id → fighter (1=yb, 2=pb) |
| `fA(a,b)` | L399-400 | move command (PhysicalFall) |
| `Zw(a,b)` | L415-416 | slow-mo toggle (`v.YT`) |
| `uhb` | L415 | hit-interrupt check |
| `Onb()` | L411 | **round-end check** (timeout / KO / ringout) |
| `E3a(a,b,c)` | L412-413 | apply round result (sets `zd`, `Iq`, `kh` on params) |
| `bea(a)` | L413-414 | battle end (gameComplete/gameOver via `Ca`) |
| `Z2()` | L408-409 | **round start**: snapshot HP/move state, `round.round++`, `MHa()` sets each fighter's `round` |
| `MHa()` | L409 | `c.Z2(this.round.round); c.parameters.nob()` per fighter |
| `tx()` | L407 | round init: `round.Vt=!1; round.eL=Da.pT; round.time=0; round.gma=Da.R4; round.Mcb=...` |
| `bob()` | L400-401 | restart fight (`round.round=0`) |
| `$K()` | L401-402 | previous round (`round.round--`) |
| `mfb(a)` | L404-405 | next opponent (`Rk++`, `Zb=pf[Rk]`, `pb=Gf(Zb)`) |
| `Gf(a)` | L403-404 | **create fighter**: `new wd(a); wI(); index=Ta.Gf(...); Ra.push(b)` |
| `o1a()` | L403 | create both fighters: `yb=Gf(kc); pb=Gf(Zb)` |
| `HT(a)` | L419 | wire fighter events to `ca` handlers |
| `z3(a)` | L423 | animation-change handler → `fighter.z3(name,...)` sets `zP` |
| `D7a(a)` | L423 | player-id → fighter (0=yb, 1=pb) |
| `Ema(a)` | L420 | snapshot fighter state into `ze` |
| `Dyb(a,b)` | L420-421 | update round-result trackers |
| `KZa(a)` | L421-422 | battle-end achievements |
| `JZa(a)` | L422 | round-end achievements |
| `hCa(...)` static | L431 | fight setup (fills `sl` setup data) |
| `Yxa(...)` static (in `v`) | L1209 | `new ca(a,b,c,d)` — fight factory |
| `E7a(a)` | L382 | fighter by id (-1=null, 0=yb, 1=pb) |
| `Rea(a)` | L397 | fighter by id (1=yb, 2=pb) |
| `BT(a)` | L392 | rule events (LifeSteal/Regeneration/Ringout/Crazy/HotGround/...) |
| `jT(a)` | L393 | heal fighter by id |
| `Pma()` | L398 | round-start cleanup |
| `NA()` | L414 | reset fighter params (heal, clear flags) |
| `IKa()` | L416 | pre-round setup |
| `tja()`/`Qlb()` | L417-418 | respawn player/enemy fighter |
| `PC(a,b)` | L419 | dispatch phase event |
| `fmb(a)` | L398 | `yKa()` by id |
| `cka/U4` | L392 | HUD button actions (attack/block) |
| `wP/HU/iT/WK` | L397-398 | HUD helpers |
| `Kla` | L397 | camera shake |
| `Irb` | L397 | damage-number popup |
| `Zgb` | L397 | refresh fighter move lists |
| `LBa(a)` | L399 | round-order remap |
| `Mvb` | L399 | sort pending achievements |
| `Gqb(a)` | L399 | set player magic |
| `Ewb` | L404 | setup round intro |
| `vhb` | L410 | HUD button callback (next round / restart / fight) |
| `Qg(a)` | L410 | HUD event dispatch |
| `Mfb(a)` | L411 | debug move keys |
| `Jfb/Kfb` | L416 | camera focus events |
| `fxa` | L416 | clear `pR` flags |
| `Fda` | L416 | per-fighter `Fda()` |
| `N1a` | L416 | create `ud` battle manager |
| `Tlb/Slb` | L417-418 | equipment-change checks |
| `$Oa` | L418 | ? |
| `q_a` | L416 | resume from hitstun |
| `FNa` | L409 | phase 1 (StartStance) |
| `Rkb` | L410 | phase 2 (Fight) |
| `i4a` | L410 | phase 3 (EndStance) |
| `Am` | L410 | HUD `Zy()` |
| `h4a` | L413 | set `JJ` |
| `vfa(a)` | L414 | winner params |
| `L4a/TYa` | L424 | round-end cleanup |
| `UMa/kHa` | L424 | achievement popup |
| `uS/AS/fS` | L424 | camera effects |
| `gia` | L423 | hit event → `Bg.Ih(16,...)` |
| `Ihb` | L423 | weapon-change event |
| `EE` | L422 | achievement counter event |
| `ZAa` | L422 | shock/crit/head-hit lookup |
| `X0/e$a/RCa/UCa/D$a/E$a/S0` | L382-383 | HUD/state accessors |
| `Jtb(a)` | L382 | toggle `uN` |
| `S0` | L382 | `!ce.wzb` |

### 3.3 Damage formula — `wd.bCa(a,b,c,d,e)` (L513-514)

```js
bCa(a,b,c,d,e){ let f=this.jb;                 // attacker's current move
  d=wd.LAa(a,b,d);                             // base damage from hitbox (KP[0] / block / parry)
  h=v.ACa();                                   // difficulty multiplier
  k.G=Math.min(k.G,v.E9a()|0)|0;               // cap
  h=Math.pow(2,h*k.G);
  b=this.kea(b);                               // attack attribute multiplier (v.VY)
  c=f.qea(c);                                  // defense attribute multiplier (v.HZ)
  g=v.iea(f.parameters.qb,f.parameters,this.parameters,g,d);  // balance formula
  g=(a.Xb+f.Ly)*g*b*c*h*f.parameters.UZ;       // final raw damage
  g=Math.max(g,0);
  g=f.parameters.c2a(e,g);                     // armor reduction ("Fists" → *M_)
  g*=a.Cea(f.parameters.qb?1:2).bp;            // per-target multiplier
  g*=f.dta;
  return g*=f.so }
```

### 3.4 Damage application — `ca.Cgb(a)` (L394-397)

```js
Cgb(a){ let b=a.model.Bb; var c=a.data;        // b=hit record, c=event data
  c.a3&&(b.se=!1);
  this.Sba(a.model,b,7);                       // dispatch hit event
  var d=a.model.parameters.gd;                 // current HP
  d<b.bR?(b.Zi=d+.01,b.Iza=!0):b.Iza=!1;       // lethal check (leaves 0.01 HP)
  ...
  b.block||(a.model.hT(5),this.Dga=!0);        // play hit anim if not blocked
  d=a.model.parameters;
  this.ha!=null&&this.ha.Gzb(b.aI,b.Zi,b.target,b.ep,b.Uq,b.se,b.block,b.Ub); // HUD
  a.model.ws&&(b.Zi=0);                        // invulnerable → no damage
  a.model.$db(b.Zi,this.i6a(c),b.JP);          // queue damage display (yl)
  this.aM(a.model,-b.Zi);                      // ★ HP -= Zi  (→ xc.Laa → xc.du → gd)
  this.udb(a.model.jb,b.Zi);                   // attribute-based bonus damage
  ...
  c=d.gd<=0;                                   // KO check
  ... }
```

**HP decrement chain:** `ca.Cgb` (L394) → `aM(model,-Zi)` (L391) → `wd.Laa(-Zi)` (L522) → `xc.Laa(a)` (L816) → `xc.du(gd-Zi)` (L816) → `parameters.gd` updated. KO when `gd<=0` (`xc.Jfa`, L817).

---

## 4. Input

### 4.1 Key-name → code map — `Gz()` (L24-26), `Os` (ac dict), `ey.hi()` (L2421)

```js
function Gz(){ function a(d,e){ Os.v[d]=e } Os=new ac; ... }   // L24-26
class ey { static hi(a){ Os==null&&Gz(); return Os.v[a] } }    // L2421
```

Full map (Haxe `js.html.KeyCode` enum values, NOT browser keyCodes):

| Key name | Code | Key name | Code |
|---|---|---|---|
| Space | 32 | Quote | 39 |
| Comma | 44 | Minus | 45 |
| Period | 46 | Slash | 47 |
| Digit0-9 | 48-57 | Semicolon | 59 |
| Equal | 61 | BracketLeft | 91 |
| Backslash | 92 | BracketRight | 93 |
| Backquote | 96 | KeyA-Z | 65-90 |
| F1-F12 | 122-133 | ArrowUp | 133 |
| ArrowLeft | 134 | ArrowRight | 135 |
| ArrowDown | 136 | EKeyNumpad0-9 | 137-146 |
| NumpadAdd | 147 | NumpadDecimal | 148 |
| NumpadMultiply | 149 | NumpadSubtract | 150 |
| NumpadEqual | 151 | NumpadComma | 152 |
| NumpadEnter | 153 | NumpadDivide | 154 |
| NumLock | 155 | Escape | 156 |
| Backspace | 157 | Tab | 158 |
| Enter | 159 | ControlLeft/Right | 160/161 |
| ShiftLeft/Right | 162/163 | AltLeft/Right | 164/165 |
| PageUp/Down | 166/167 | Insert | 168 |
| Delete | 169 | Home | 170 |
| End | 171 | CapsLock | 172 |
| Pause | 173 | ScrollLock | 174 |
| PrintScreen | 175 | | |

### 4.2 Input screens (all extend `nc`, g="4C0", L2415)

`Pg.PD[]` holds one input screen per slot; `Pg.m0(i)` / `q$a()/Kfa()/Nfa()/dd()/HCa()` (L48) access them:

| Slot | Class | Line | `DQ()` | Type |
|---|---|---|---|---|
| 0 | `Ik` (g="4C6") | L2422-2424 | 0 | **keyboard** — `keydown`→`eFa` (L2422), `keyup`→`gFa` (L2422); `state` = `Hs` (L2424); events `IE`/`DHa`; `tBa(a)` modifier flags (shift=1, ctrl=2, alt=4, repeat=8) |
| 1 | `Hk` (g="4C8") | L2424-2428 | 1 | **mouse** — `mousedown`→`DGa`, `mouseup`→`FGa`, `mousemove`→`EGa`, `wheel`→`Af`; `state` = `Is` (L2428: `position`, `zw`, `wheelDelta`, `jM`, `gl`, `cn`) |
| 2 | `lf` (g="4CB") | L2437-2440 | 2 | **touch (legacy)** — `touchstart`→`pOa`, `touchmove`→`oOa`, `touchend`→`o6`; `state` = `mf` (L2441) |
| 3 | `Jk` (g="4CA") | L2429-2435 | 3 | **pointer (unified mouse+touch)** — `pointerdown`→`pJa`, `pointerup`→`HK`, `pointercancel`→`oJa`, `pointermove`→`qJa`, window `pointerup`→`CGa`; `state` = `se` (L2435-2437: per-touch `position[]`, `hdb[]`, `zw[]`, `jM[]`, `gl[]`, `type[]`, `C4[]`, `cn[]`) |
| 4 | `rf` (g="4C1") | L2415-2418 | 4 | extra input (purpose unverified; likely gamepad/extra) |

- `nc` (L2415): `{state, enabled}`; `AF(a)` enable/disable; `Y3()` per-frame poll.
- `Yd` (L2418): input-state base — `buttons[]` (each a `dy`, L2415), `sl(b)` press, `release(b)`, `Db(a)` is-down, `xh(a)` pressed-this-frame, `We(a)` released-this-frame, `O_a()` clear-all, `update(a)`.
- `fc` (g="490", L2350): 2D point `{x,y}`. `Fs` (L2420): velocity smoother. `Gs` (L2421): click detector.

### 4.3 Key press → game action

- Keyboard events land in `Ik.state` (`Hs`). Consumers poll `L.K.Kfa().state` (keyboard state) or listen to `L.K.Kfa().IE` (keydown bus).
- **Debug/cheat keys** — `fb` (g="B5", L435-438): `fb.init()` wires `fb.IE` to `L.K.Kfa().IE` (L435); active only when `L.K.fi` (debug flag). `fb.IE(a,b,c)` maps key codes → `fb.Lf(n)` → `ca.Ka()` actions (only in phase `eu==2`):

| Key(s) | `Lf(n)` | Action on `ca.Ka()` |
|---|---|---|
| Digit0 / EKeyNumpad0 (48/137) | 22 | `pb.pga=!pb.pga` (player god mode) |
| Digit1 / Numpad1 (49/138) | 4 | `fA(!1,!1)` |
| Digit2 / Numpad2 (50/139) | 5 | `fA(!1,!0)` |
| Digit3 / Numpad3 (51/140) | 9 | `$K()` (previous round) |
| Digit4 / Numpad4 (52/141) | 10 | `bob()` (restart fight) |
| Digit5 / Numpad5 (53/142) | 6 | `fA(!0,!1)` |
| Digit6 / Numpad6 (54/143) | 7 | `fA(!0,!0)` |
| Digit7 / Numpad7 (55/144) | 18 | `yb.hZ(1); yb.LA(); tb.Gj(yb,8,!0)` (enemy magic) |
| Digit8 / Numpad8 (56/145) | 20 | `yb.dca()` (enemy attack) |
| Digit9 / Numpad9 (57/146) | 21 | HUD `mb.Id.Mab()` |
| KeyB (66) | 15 | (unverified) |
| KeyM (77) | 8 | (unverified) |
| KeyU (85) | 16 | (unverified) |
| F1 (121) | 13 | `Jtb(!uN)` |
| F3 (123) | 11 | (unverified) |
| Minus / Numpad7 (45/124) | 1 | `wn=!wn` (pause) |
| Numpad8 (125) | 2 | `LEa=!LEa` |
| Numpad9 (126) | 23 | `yb.parameters.AMa(!yb.parameters.ZV)` (enemy invulnerable) |
| NumpadDivide (127) | 24 | `pb.parameters.AMa(!pb.parameters.ZV)` (player invulnerable) |
| NumpadMultiply (130) | 17 | (unverified) |
| NumpadSubtract (131) | 14 | `Ta.Uyb()` (camera) |
| NumpadDecimal (132) | 12 | cycle test magic (`pb.s5(...)`) |
| Equal / ArrowRight (61/135) | 3 | `wn&&ia()` (step frame) |

- **Real player controls:** movement = tap/click on ground → `ma.Bd(target)` (L1836) converts screen point (`N.kn(0)`) to world and moves the fighter (consumed in `db.aa`, L1839, via `L.K.dd().Db(0)` = pointer button 0). Attack/block = HUD buttons (`Ar.mb.Id`/`mb.je`) → `ca.cka`/`ca.U4` (L392). Magic = `ca.Gqb`/`wd.s5`/`wd.NT` (L399/L505).

---

## 5. Screen / scene state

### 5.1 Screen enum — `xn` (g="24C", L1167-1168)

```js
static iOa(a){ switch(a){ case 0:return"Preloader"; case 2:return"Loader"; case 3:return"Dojo";
  case 4:return"Shop"; case 5:return"Map"; case 6:return"Fight"; case 7:return"Profile";
  case 8:return"GeneralMenu"; case 9:return"Pvp"; default:return"" } }
static jOa(a){ switch(a){ case "Dojo":return 3; case "Fight":return 6; case "GeneralMenu":return 8;
  case "Loader":return 2; case "Map":return 5; case "Preloader":return 0; case "Profile":return 7;
  case "Pvp":return 9; case "Shop":return 4; default:return 11 } }
```

### 5.2 Current-screen accessors

- **Screen manager:** `mc.K` (L122) — `stack[]` of `$d` states; top of stack = current screen. `$d.DQ()` returns the screen id (e.g. `ai.dJ()==6` = Fight, L2005).
- **Fight screen:** `ma.Jg()` (L1836) returns current fight screen (`ma.ma`); `ai.get()` (L2010) returns it if it is the Fight screen.
- **Fight screen classes:** `ma` (g="3C1", L1837) base; `ai` (g="3FF", L2004-2010) main Fight screen (`dJ()==6`); `Tf` (g="3F5", L1972) and `Oa` (g="467", L2285) other fight variants (Dojo/PVP).
- **Game-flow manager:** `ha.F()` (L1014-1020) tracks the battle-type screen stack (`xy[]` of `be`), loaded battles (`Dh[]`), and `ta` (Bj). Updated every frame by root node `qi` (L1014).
- **Battle type enum:** `be` (g="1C8", L1011): `ifa(a){return a=="Fight"?6:a=="Dojo"?3:a=="Map"?5:-1}`.
- **Fight types** (from `p.Wab`, L181-182): `DUMMY→FightNone, TUTORIAL→FightTutorial, CHALLENGE→FightChallenge, BOSSES→FightBosses, TOURNAMENT→FightTournament, STORY→FightStory, SURVIVAL→FightSurvival, TACTICS→FightFriendly, AUTO→FightAuto, AI→FightAi, HIDDEN→FightUnregister, FAKE→FightFake, PVP→FightPVP, ...` — lookup `p.o.b0(key)`/`rAa(type)` (L180).
- **Stance enum:** `iz.XBa` (L447): `EndStance=3, Fight=2, PeacefulRestore=6, PeacefulStart=4, ShopPurchase=5, ...`.
- **Player enum:** `Jf` (L1302): `"Me"→1, "Enemy"→2`; `OBa`: `EndStance=3, Fight=2, StartStance=1`.

---

## 6. Save / state

### 6.1 Storage bridge

- `Ca` (g="5", L34-36) — static wrapper over `window.GameInterface`: `hasFeature`, `R1a(name)` → `new Ck(name)` (storage handle), `gameReady/gameStart/gameComplete/gameOver/gamePause/gameResume/gameQuit`, `getCurrentLanguage`, `onPauseStateChange/onMuteStateChange`, `isMuted/isPaused`, `showInterstitialAd/showRewardedAd/isRewardedAdAvailable`, `storage.redeemCode`, `onOffsetChange/getOffsets`, `sendPreloadProgress`.
- `Ck` (g="7", L36-37) — storage handle: `load()` → `window.GameInterface.storage.getItem(name)`, `save(a)` → `setItem`, `delete()` → `removeItem`.
- `Qs` (g="4", L33) — IAP: `buyProduct`, `getProducts`, `consumeProduct`, `onEvent`.
- `Xs.Mea()` (L2347-2348) — `window.localStorage` accessor (fallback path).
- microsite-game-interface.js: `window.GameInterface` is defined with `storage` (prefix `"famobi"`, key `"savegame"`, backed by localStorage/IndexedDB), `iap` (Xsolla), and lifecycle methods; final line L61734: `window.gameInterface && (window.GameInterface = window.gameInterface)`.

### 6.2 Save keys (static init, L2462)

| Key | Constant | Used by | Content |
|---|---|---|---|
| `SF2User` | `Aa.WU` | `Aa.load()/save()` (L70-73) | main user save — serialized XML (`Rb` document), zstd+base64 encoded (`ri.encode`), stored via `Ca.R1a` |
| `SF2Packs` | `Aa.Y6` | `Aa.OJ()/hka()` (L73) | packs/bundles save |
| `SF2Flags` | `cg.P6` | `cg` (L68-69) | flags JSON (`i`=H1, `m`=VF, `p`=zK, `f`=qQ) |

- `Aa` (g="16", L70-73): `load()` reads `SF2User`, decodes (zstd `ri.decode` → `cd` → `Rb.parse`); `save(a)` encodes and writes; `Ddb()` imports `.sf2` file; `Dpb()` exports `.sf2`; `Aa.flags` = `cg` instance.
- `Tg` (g="15", L69) — localStorage wrapper (`Xs.Mea().getItem/setItem/removeItem`).
- `p.o.save(!0)` (L181) — game-state save trigger.
- Sentry crash state (`L.Emb`, L68): `a.state={packs:..., users:..., seed:...}` — "users" = `Aa.load().stringify("")` (the SF2User save), "packs" = `Aa.OJ().stringify("")`.
- No literal `users.xml` string in the JS; the "users.xml" of the recovery docs corresponds to the `SF2User` save (serialized XML document).

---

## 7. Asset loading

### 7.1 Asset manager — `G` (g="4B0", L2390-2398)

- `G.init()` (L2390): `G.data=new jd` (id→decoded asset), `G.WJ=new jd` (archives), `G.nE=new If` (loader queue), `G.lang`, `G.Mca` (image fmt), `G.ZZ` (texture fmt dds/ktx), `G.HI` (audio fmt), `G.rq` (id→path), `G.Sra` (count).
- Entry points: `G.load(id)` (L2391), `G.Odb(ids)`, `G.iS(id,cb)` (load+callback), `G.bg(id)` (id→path with `{lang}/{image}/{audio}/{scale}` substitution, L2393), `G.qf(path)` (path→id, L2394), `G.ln(id)` (asset→string), `G.Oq(id)` (asset→`kb`), `G.setData(id,data)`, `G.Qr(id)` (release), `G.aga(id)` (is archive), `G.z7a(id)`/`G.Crb(id,data)` (archive data), `G.pEa(id)` (is image), `G.J1(id)` (is audio), `G.Kcb(id)` (is "-p." texture), `G.Iea(id,ext)` (extension swap), `G.u1a(name)` (dynamic id), `G.V6a()`/`G.N8a()` (all ids).
- `If` (L2385) — loader queue (`load(a)`); `$x` (L2385) — per-asset loader entry (`id`, `Mzb` fn, `rn` ready).
- `vz` (g="4B2", L2399) — XHR loader (`responseType` by extension: mp4→blob, json/xml→text, else arraybuffer).
- `Tc` (g="4B3", L2400) — audio decoder (`AudioContext.decodeAudioData`).
- `Ja` (g="D", L42-46) — XML/archive loader: `Ja.xml` (asset 0), `Ja.Ra` (315/314), `Ja.Lk` (name→data map), `Ja.ki(id)` (parse XML asset), `Ja.Dka(id)` (load name map), `Ja.Mda(archive,name)` (extract file), `Ja.Vxb()` (bulk-load 394-551).

### 7.2 Compression / decode

- **zstd decoder: `si` (L136-152)** — `read(a,b)` decompresses zstd frames (magic `28 B5 2F FD` = 3126568 check at L139). Internals: `dt` (L153) frame context, `$k` (L153) frame result, `ui` (L153) huffman table, `nT`/`xpb`/`wpb`/`N2a`/`Gob`/`kr`/`Mva`/`oZa`/`R0a`/`TJa`/`jYa`.
  - **NOTE:** the recovery repo's ASSET-ANALYSIS.md names the zstd class `ti` (line 154 of an older `sf2.js`). In THIS build `ti` (L113) is a **time-of-day class** (`cR`=hours, `I2`=minutes, `Y4`=seconds, `Jca`=days) and the zstd decoder is **`si` (L136)**. Class names were re-mangled between builds.
- `oy` (g="4AF", L2389-2390) — zstd + SHA-256 XOR decrypt wrapper: `oy.vza(kb)` → decompress/decrypt → `kb`.
- `cd` (L105 `Kb.l`) — binary reader (`ea()`, `Yt()`, `ie()`, `ek()`, `ti()`, `z4()`, `cub()`).
- `kb` (g="4D3", L2319) — byte buffer (`jl(arrayBuffer)`, `f3(str)`, `b.zv`).
- `ve` (g="4D4", L1490) — texture/atlas decoder (`e3(kb)`).
- `Ns` (g="4D5", L1674) — texture header parser (used by `bz`, L22); `Ms` (g="4D6", L1695) — texture decoder (`read(kb)`); `te`/`mx` (L1674) — header/result structs.
- `qc` (g="4D7", L1455) — image wrapper (`{data,width,height,Tbb}`); `qc.load(src)` loads via blob/ImageBitmap.
- `Mg` (L2323) — base64 decode; `ri` (L2443) — base64 encode.
- `Wg` (g="488", L2336) — XML parser (`Wg.parse(str)` → `ia` document); `Vk` (g="489", L2343) — XML printer; `ia` (g="28", L101-105) — XML node class; `u` (L2455) — XML attribute helpers (`ka` bool, `I` int, `H` float).
- `Gd` (L2447) / `rc` (L2446) — deferred / promise wrapper.

### 7.3 Model / animation loaders

- `Ja.Dka(id)` (L45) — loads the name→data map (models/animations archives).
- `wd.fya` (L535-536) — fighter factory: `a.h7a(b)` builds params, `new ih(b)` or cached `wd`; `wd.Ulb()` (L496) builds `da` (Te animation controller), `nf` (de weapon), `Nd/IH/Fu` controllers.
- `Te` (g="D4", L566) — animation controller (plays `da.Ua`, fires `EStartAnimationEvent`/`EStopAnimationEvent`/`EAnimationInterruptedEvent`/`EStartIntervalEvent`/`EStopIntervalEvent`/`EActionStart`).
- `Bf` (g="D5", L473) — arena/location (parsed from XML; `Yia`/`B_` spawn points).
- `xd` (L1264) — physics constants from XML (`FrictionForce`, `Gravitation`, ...).
- `Ch` (L1224) — fight-list parser (`Ch.parse(xml)` → `qw` entries); `ai.LY=Ch.o5a()` (L2005).

---

## 8. How to hook a per-frame state tracer

### 8.1 Recommended wrap point

Wrap **`Pg.aa(a)` (L57)** — it runs exactly once per fixed 60 Hz tick and is the single choke point through which every update (input states, scene graph, screen manager, fight) flows. A tracer inserted at the top of `Pg.aa` sees a consistent, complete frame snapshot.

Alternative/refined wrap points:
- **`ca.ia()` (L388)** — fight-only, runs once per tick while a fight is active; gives `ca.frame` (L388) as the fight frame counter.
- **`wd.ia()` (L498)** — per-fighter; wrap to capture each fighter's state individually (note: called once per fighter per tick).
- **`ma.YL(a,b)` (L1832)** — the exact call that drives the fight controller from the screen (`Ig.Ea()`).

### 8.2 Fields to read per frame

```
fight = ca.Ka()                       // null when no fight active
frame = fight.frame                   // fight frame counter (L388)
phase = fight.eu                      // 0 idle / 1 StartStance / 2 Fight / 3 EndStance
round = fight.round.round             // round number ($t, L1239)
timer = fight.round.time              // round timer (seconds)
roundLen = fight.round.eL             // round length
paused = fight.wn                     // pause flag
slowmo = fight.yt                     // slow-mo flag

player = fight.pb                     // wd (player fighter)
enemy  = fight.yb                     // wd (enemy fighter)

// per fighter f (wd):
f.parameters.gd                       // current HP
f.parameters.Zn                       // max HP
f.parameters.qb                       // is player
f.parameters.$s                       // name
f.oa.Fe().ma.x / .y / .z              // world position (H)  ← position
f.da.hd()                             // facing (±1)
f.zP                                  // current animation name (set by ca.z3 → wd.z3, L517)
f.Je                                  // stance (1 StartStance / 2 Fight / 3 EndStance / 7 TryOn)
f.round                               // fighter's round number
f.lb / f.jb                           // current move / move id
f.my                                  // move progress 0..1
f.Bb.bR / f.Bb.Zi                     // last hit raw/final damage
f.Bb.block / f.Bb.se / f.Bb.Ub / f.Bb.Uq  // last hit flags
f.sn / f.vc / f.ws / f.pga            // disarmed / weapon-in-hand / invulnerable / god-mode
f.bh                                  // magic bullets
f.Era                                 // hits taken counter
```

### 8.3 Practical notes

- The tracer must be injected **before** the game's own update mutates state — wrap `Pg.aa` and snapshot at entry, or wrap `ca.ia` and snapshot after `frame++` but before fighter updates.
- `ca.Ka()` returns the live fight controller; `ma.Jg()`/`ai.get()` (L1836/L2010) give the fight screen; `L.K.dd()` (L48) is the pointer input state (not the fight screen).
- All positions are in world units (arena `Bf`); screen conversion via `N.kn(0)` (L85) / `ma.Bd` (L1836).
- Time scale: `v.on()` (L1201) — slow-mo multiplies it; `L.K.sk.Bm` (L135) is the fixed 1/60 step.

---

## 9. Honest gaps / unverified

- `rf` (PD[4], L2415-2418): purpose unverified (likely gamepad/extra input; `DQ()`=4).
- `fb.Lf` cases 8, 11, 15, 16, 17 (L437-438): exact actions not traced (no visible `ca` call in the captured window).
- `ha` (L1014) `ta`/`Flb` (Bj) fields: battle-journal semantics partially inferred.
- `wd` fields `Fc` (Ae), `Ba`, `QW`, `UTa`, `cache` (su), `Ja/P9/Ml/Vu/Js/qs` (ju/ku/lu/mu/nu/ou) internals: mapped by name only.
- `du` (L894) battle-manager internals (`Ih` event dispatch, `f_a` rules): mapped at entry level only.
- `S` (L944) script base / `qo` (L1086) battle script: entry points identified; full script opcode semantics not traced.
- `Ut` (L823) camera controller internals (`Rf`, `qh`): mapped by usage only.
- The exact `i6a(c)` helper in `ca.Cgb` (L394) was not located (damage-display variant selector).
- `wd.ia()`'s `Qnb/Mnb/Bnb/RZa/Pnb/Kzb/MOa/Dmb/Ax` sub-steps: behavior inferred from names/context, not fully traced.
- microsite-game-interface.js internals (Firebase/Xsolla) intentionally not mapped beyond the GameInterface surface.
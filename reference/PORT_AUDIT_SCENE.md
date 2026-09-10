# PORT_AUDIT_SCENE — scene / location / camera pipeline

Scope: `reference/www/sf2.502f0946.js` (the web oracle) vs
`core/scene/location_scene.cpp/.hpp`, `core/scene/renderer.cpp/.hpp`,
`core/scene/fight_camera_sya.hpp`, `core/scene/fight.*`, `core/app/screens.cpp`
(the live draw sites), with `reference/www/res/locations/**` as data.
This is a **read-only audit** (no code changed, no commit). Line cites are
`JS Lnnn` for the oracle and `file:line` for the native.

Honesty note up front: the `dojo` UI-gate (`.tmp_dojogate/oracle_dojo.png` vs
`port_dojo.png`, `.planning/MASTER_TODO.md` L411-604, gate 68.70-68.83 %) is
**not a clean hub-vs-hub comparison**. `MASTER_TODO.md` L423 records
`dojo | NONE (oracle boots to tutorial fight, not a hub)`: the oracle capture is
the **JS tutorial fight in the dojo**, while `port_dojo.png` is the **hub**
screen. So a slice of the gate gap is a state/screen mismatch, not a pixel bug.
The pipeline defects below are what the code does vs what the oracle math says;
they are the actionable part.

---

## 0. Divergence summary (ranked)

| # | Item | Verdict | Native site | Δ vs oracle |
|---|------|---------|-------------|-------------|
| D1 | Fight-path **vertical camera** (`center_y` = smoothed focus y + unconditional `layer_vshift`) | **INVENTED** | `fight_camera_sya.hpp:79-81,121-122`; `renderer.hpp:60,69-71`; `screens.cpp:2982` | **+92.9 px at spawn, growing toward ~+250 px** — whole scene (layers + fighters) sits low; floor/masks fall off the bottom |
| D2 | Fight-path **horizontal re-center** (`kCxOff`: `center_x=focus−980`, `arena_center_x=0`) | **INVENTED** | `screens.cpp:2979-2981,3004` | **+Io·zoom = +193.05 px** for every layer and both fighters (pan right; oracle keeps `cam.pos.x=0`) |
| D3 | Hub `default_camera` static Sya framing | **APPROXIMATED** | `location_scene.cpp:279-335`; `screens.cpp:1570` | exact only at spawn (`Io=148.5`); frozen — the JS `Tf` hub runs the **live** fight camera |
| D4 | `Xrb(F9·(1−Bj))` vs `setScale(Bj)` branch | **INVENTED/APPROXIMATED** | `renderer.hpp:60,69-71` (always adds `layer_vshift`, uses render `zoom` not `layer_zoom`) | −30 px folded into every scaled layer; cancelled at the hub by `center_y`, **uncancelled on the fight path** |
| D5 | Per-layer z (`−3·i`) and per-sprite z (`−0.01·j`) | **MISSING** (neutral) | `location_scene.cpp:346-361` | order preserved by insertion, so dojo composition is unaffected today |
| D6 | SimpleEffect: live Transparency + Oscillation/Speed/Reappear/Rotation | **APPROXIMATED** | `location_scene.cpp:70-83,260-270` | static α=0.45 vs animated 0.45–0.75 (dojo `layer_4`) |
| D7 | ParticleEffect / NewParticleEffect + `OnBackground` bg/fg routing | **MISSING** | skipped `location_scene.cpp:272`; JS `QIa` L481, `Gfb` L729, `tl.Nt` L842 | dojo has none (neutral); required by other locations |
| D8 | ModelsViewer spawn positions (`PlayerPosition*`, `EnemyPosition*`) | **APPROXIMATED** | ignored in load; sim spawns used | dojo XML (690,−93)/(973,−110) equals sim — neutral today |
| D9 | Image `Rotation` attr | **MISSING** | `location_scene.cpp:87-176` (no `Rotation` read); JS `R3a` L487 `b.Wg(e)` | dojo has none — neutral today |
| D10 | Letterbox (`N.BK` side bars; `Sya` top/bottom bars) | **MISSING** | no bar draw anywhere | no-op at 16:9 (`N.BK=0`); breaks other aspects |
| D11 | `m$a() = arena_h·Bj` (Sya denominator) | **APPROXIMATED** | `fight_camera_sya.hpp:39` uses `arena_h` | equal only while `Bj=1` |
| D12 | `tl` container offset for **effects** (`x=−W/2, y=H/2−floor`) | **PARTIAL** | fighters get it (`screens.cpp:3028,3031`), sparks/magic do not (`screens.cpp:654-655,680-681`) | effects placed without the 200 px y / 980 px x container shift |
| D13 | trim / rotated atlas handling | **FAITHFUL** (verified) | `location_scene.cpp:123-163`; `renderer.cpp:49-81` | matches `pi.VJa` L1703 + `Vs.Qq` L1705 + `R.Cb/Th` L1613-1615 |
| D14 | `pixel_1` solid mask (`Na.cd`, anchor .5) | **FAITHFUL** | `location_scene.cpp:100-114`; JS `ujb` L477 | matches |
| D15 | Layer `Scaling`→`ij`, `Type=2`→`lEa`, `setScale(Bj)` selection | **FAITHFUL** | `location_scene.cpp:353` | selection matches; only the *value* path (D4) is wrong |
| D16 | Parallax `Wrb(Io·bp)` and `Io = W/2 − focus` formula | **FAITHFUL** (formula) | `renderer.hpp:62-68` | formula right; wired wrong on the fight path (D2) and frozen on the hub (D3) |

---

## 1. JS spec — the location renderer

### 1.1 Params XML model (`Bf`, `Bf.init` L474-475)

`Bf.init` (L474-475) parses `locations/<name>/<name>_params.xml` and reads the
`Root` attributes:

```
Pages          -> number of atlas PNG pages (dojo Pages=1)
FrictionForce  -> xd.bAa (physics; native unused)
Wall           -> this.NU          (dojo 80)
Floor          -> this.ct          (dojo 80)   <-- the container y anchor
PositionY      -> this.Tza         (dojo absent)
Color          -> this.N2          (dojo 0x000000; fighter silhouette fill)
Width          -> this.width       (dojo 1960)
Height         -> this.height      (dojo 560)
```

`z9a()` (L475) = `((Yia + B_)·0.5)` → the spawn midpoint (player+enemy)/2, the
camera's initial focus. `oCa()` (L475) = `(width/2, height/2, 0, 1)`.
Atlas pages are `locations/<name>/<name>.png`, then `-2`, `-3` … (`L474`;
dojo → `dojo.d31b1e71.json` + `dojo.b920e18e.webp`).

### 1.2 Layer construction (`Bf.zjb` L475-477, `Qi` L487-489)

```
Bf.init: c=0; for each child layer L: this.zjb(L, c); c += -3     (L475)
Bf.zjb(L, b):
    b = new Qi(b)                       // b = the layer's depth z
    b.ij   = Scaling > 0                // -> "scaled layer" flag
    b.type = Type                       // 1 = sprites, 2 = ModelsViewer
    b.bp   = Factor                     // parallax factor
    b.re   = Atlas or null
    child switch (L476-477):
      ModelsViewer      -> this.Yia / this.B_ = (PlayerPositionX/Y, EnemyX/Y, z=0)
      ParticleEffect|   -> this.QIa(child, layer)
        NewParticleEffect
      Image             -> this.ujb(child, pagePath, layer)
      SimpleEffect      -> this.UIa(child, pagePath, layer)
    this.Ct.push(b)
    b.lEa() && (this.hn = b)            // hn = the LAST ModelsViewer layer
```

`Qi` constructor (L487): the layer node has `translate = (0,0,a)`, i.e.
**z = the value passed from `init`** (0, −3, −6, …). `lEa()` = `type==2` (L487).

### 1.3 Image elements (`Bf.ujb` L477 + `Bf.R3a` L486-487)

```
ujb(node, page, layer):
    cls = node.ClassName
    tint = node.has(Color) ? Na.cd(parseInt(Color)) : null      // Na.cd L1448
    if cls == "pixel_1":
        w = Width; h = Height
        s = R.Ed( argb(tint), w|0, h|0 ); s.Rn(.5,.5)           // solid, centered
    else:
        s = R.$(E.get(page), cls)                               // atlas sprite
        if tint: apply to s.sf()
    Bf.R3a(node, s)
    layer.NWa(s)                                                // append + z
    node.has(Flip) && s.Hr(true)

R3a(node, s):                                                   // L486-487
    X=W, Y=H, rot=Rotation, w=Width, h=Height
    s.C(X); s.D(Y);                    // position (sprite CENTER, anchor .5)
    s.Rh(w / s.fa.x); s.mj(h / s.fa.y); // scale = XML size / SOURCE size
    s.Wg(rot); s.ik(.5,.5);            // rotation, anchor = center
```

`fa` = the atlas `sourceSize` (Pj carries it from `Vs.Qq` L1705). So the scale
denominator is the **untrimmed source size**, exactly as the native uses it when
`trimmed` (see §1.11).

### 1.4 Node / z construction, and the layer append order

```
Qi.NWa(s)  (L487): go.node.appendChild(s.L); s.Dla(this.QH); this.QH += -0.01
Qi.pWa(s)  (L487): go.node.appendChild(s.Y.L); this.P7.push(s); s.Y.Dla(this.QH); this.QH += -0.01
Dla(a)     (L1599): this.L.Jb.translate.z = a
```

So **each layer node has z = −3·layerIndex**, and **each sprite inside a layer
has z = −0.01·spriteIndex** (QH starts 0 per `Qi`). `Ut.UWa` (L832) appends the
layer nodes to the Render node in XML order:

```
UWa(): Rf = new tl; Rf.init(Lb)
       for each layer L: this.go.nd(L.go)        // XML order
       Cu = new Qi(lastLayer.ldb + -3); this.go.nd(Cu.go)
```

The `tl` container (fighters + effects) is attached **inside `hn`** — the
ModelsViewer layer node (`tl.init` L843: `a.hn.go.nd(this.go)`). Therefore the
draw order is: layers 0..7, then inside layer 8 the `tl` children (bg effects,
fighters, fg effects), then layers 9..10 and the `Cu` node. This is exactly the
"bg → fighters → fg" split the native implements (D5 keeps it equivalent while
insertion order is the only sort key).

### 1.5 SimpleEffect (`Bf.UIa` L478-479, `xl`, `bkb` L479-481)

```
UIa:
  if Type == "Picture":
      d = new xl(0, node); d.ALa(Pause)
      d.ala(page, ClassName, Width, Height)     // static picture sprite
  if Type == "Sequention": ... (skipped when v.Qcb)
  d.setPosition(X, Y)
  this.bkb(node, d)                             // the modifier block
  node.has(Flip) && d.Y.Hr(true)
  layer.pWa(d)
bkb (L479-481) reads the child modifiers:
  OscillationX/Y  -> Mrb/Nrb(Offset) + bXa/cXa(Period,Value,Ease)
  ReappearX/Y     -> gsb/hsb
  Rotation        -> $sb(StartAngle)+Zsb(Offset)+GXa(Period,Value,Ease)
  SimpleEffect    -> nested UIa
  Speed           -> zsb(X,Y)
  Transparency    -> irb(Offset) + KWa(Period,Value,Ease)
```

The `Transparency` value at rest is the first `Point/@Value`/100 (the `xl.ia`
path evaluates the `KWa` key list each frame; `zh` with `ar=0` = first key).
Dojo `layer_4` keys: 45,75,55,75 → live α oscillates 0.45–0.75.

### 1.6 Particle effects + `OnBackground` (`QIa` L481-482, `Yl` L729)

```
QIa(node, layer): if !v.AEa: layer.fXa(new jh(node.st(), X, Y))
Yl.parse (L729): .. Gfb = OnBackground (bool) .. Scale, TimeScale, Looped, Backwards, PackName
```

Effects are routed at draw time through the `tl` container's `Xm` split
(`tl.Nt` L842-843):

```
tl.Nt(a): a.Gfb ? this.Gq.Nt(a) : this.Hq.Nt(a)   // Gq = bg effects, Hq = fg effects
```

### 1.7 ModelsViewer (`Bf.zjb` L476)

Sets `this.Yia = (PlayerPositionX, PlayerPositionY, 0)` and
`this.B_ = (EnemyPositionX, EnemyPositionY, 0)`; z=0. These are the raw
container-local spawns (dojo: player 690,−93; enemy 973,−110).

### 1.8 Parallax, layer scale and offsets (`Ut`, L823-833)

```
Ut.init(a)  (L823): Lb=a; Bj=1; F9=(Lb.height/2 - Lb.ct)/2; mwa(); UWa(); V0a(); s1a()
Ut.mwa()    (L823-824):
    Ira = N.height / Lb.height
    b   = N.width
    N.lc > N.sTa ? ( N.BK = round((N.width - N.sTa*N.height)/2);
                     b = (N.lc <= N.sTa) ? N.width : N.height*N.sTa )
                 : N.BK = 0
    nC = b / Ira; if (N.lc < 1) nC *= N.lc
    NW = nC / Lb.width
Ut.xCa()    (L831): min( nC / (ECa() + 300), 1 )        // ECa = |playerX - enemyX|
Ut.Al(focus, a, b, c, e) (L826-827):
    e == null && (e = 0)
    mwa()
    Io = Lb.width/2 - focus.x
    Bj = e>0 ? e : xCa()
    ... maxWidth / kJa pan-follow terms ...
    Bj = Kga ? NW : max(Bj, NW)
    d  = (Lb.width - v.LC.oGa)*Bj*0.5 - nC*0.5          // pano limit
    if (|Xia|<eps || |X3|<eps) Io *= Bj
    Io = clamp(Io, -d, +d)
    for each layer L:
        L.lEa() || L.ij ? L.setScale(Bj) : L.Xrb(F9*(1-Bj))
        L.Wrb(Io * L.bp)                                 // translate.x = Io*Factor
    Ut.kyb(vB, Io - (Lb.width/2 - c.x)*Bj, hn.translate.y + 2*F9*Bj + 10)
```

Setters (L488-489): `setScale(a)` → `scale.xy = a`, `scale.z = 1`;
`Wrb(a)` → `translate.x = a` (y,z kept); `Xrb(a)` → `translate.y = a`
(x,z kept). A **scaled** layer therefore has `translate.y = 0` and no `F9`
term at all.

### 1.9 Camera fit (`ma.Sya` L1833) and the frame camera (`ql` L362-371)

```
ql.tyb (L363): Du.ma = midpoint of the two fighters' CoM nodes
ql.dZa (L363-365): the 200/50-clamped chase on Go.ma (the focus)
ql.c3a (L366): this.ia.Al(this.Go.ma, Du.ma, h$.ma, i$.ma, IJ ? Bf.currentScale : 0)

ma.Sya(a) (L1833):
    b = N.Ta; b.Dwa()                     // origin = (viewW/2, viewH/2)
    c = N.lc                              // aspect
    d = a.Ta.ia.Rf.qh.ECa()               // fighter span
    e = a.Ta.ia.m$a()                     // = Lb.height * Bj
    f = N.height / e
    f *= (c<.45 ? .45 : c>1 ? 1 : c)
    if (c < .8) f *= .8 + ((clamp(c,.5,.8)-.5)/.3)*.2
    f *= min(N.width/(d*f + 100), 1)
    d = .6 + ((clamp(c,.5,1)-.5)/.5)*.7
    d != 0 && (f < d ? f = d : b.C(0))    // min-zoom OR camera x := 0
    b.tMa(f)                              // zoom
    c < 1 && b.D(round((N.height - e*f)/2)/f*.5)   // portrait vertical shift
    ... set ma.Kq.P / ma.Kq.W ...
    letterbox: N.BK>0 -> side bars; top/bottom bars from the computed extents
```

Camera transform (`class Vg.uA` L79-80): `screen = (world − position)*zoom +
origin`. `N.aa` (L85) resets it each frame: `N.Ta.K4()` (position=0, zoom=1),
`kT()` (origin=0), `ba(w,h)`; then `Sya` sets origin to the centre and the zoom.
At 16:9 `f < d`, so `b.C(0)` is **not** executed — but `K4` already left
`position.x = 0`. Hence the JS always projects with camera position `(0,0)`.

### 1.10 1280×720 dojo numbers (derived, all from the formulas above)

`N.width=1280, N.height=720, N.lc=16/9, N.sTa=2.5, N.BK=0` (`L2462`);
`Ira=720/560=1.28571`, `nC=1280/1.28571=995.56`, `NW=995.56/1960=0.50794`,
`F9=(280−80)/2=100`. Spawn `focus=(831.5,−101.5)` ⇒ `Io=980−831.5=148.5`.
`ECa=283` ⇒ `xCa=min(995.56/583,1)=1` ⇒ `Bj=1`. `Sya`: `f=720/560=1.2857`,
aspect/Narrow/width-fit all ×1, `d=1.3` ⇒ **f=1.3**. Projection:
**`screen = world·1.3 + (640,360)`**.

The `tl` container (`L843`): `x = −W/2 = −980`, `y = H/2 − Floor = 200`, `z=0`.

| layer (Factor) | sprite | screen centre | screen bbox |
|---|---|---|---|
| L0 bg (0.4) | `_0015_bg` | (717.2, 386.0) | x[−541,1976] y[53,719] |
| L1 (0.5) | `_0014_mountains` | (769.0, 491.3) | x[349,1189] y[264,719] |
| L2 (0.65) | `_0013_temple` | (735.6, 522.5) | x[338,1133] y[362,683] |
| L3 (0.65) | `_0012_bridge` | (760.9, 579.7) | x[399,1123] y[474,685] |
| L4 (0.8) | `_0011_tree_and_light` | (683.9, 433.4) | x[423,945] y[218,649] |
| L5 (1.0) | `_0010_Wall` | (833.0, 386.0) | x[−425,2091] y[53,719] |
| L6 (1.0) | `_0009_lamp_left` | (292.9, 133.8) | x[222,364] y[53,214] |
| L6 (1.0) | `_0008_lamp_right` | (1387.5, 132.5) | off right |
| L7 (0.95) | `_0001_go_table` | (1664.5, 581.6) | off right |
| L9 (1.0) | `layer_4` | (1648.8, 340.1) | x[1259,2039] — off right at spawn |
| L9 (1.0) | floor tiles ×8 | y 650.5, x −331.8 … 1997.9 | tile pitch 332.8 px |
| L9 (1.0) | `dojo_punch_bag_holder` | (820.0, 95.4) | x[618,1022] y[1,190] |
| L9 (1.0) | `left_wall` / `right_wall` | (−337,360) / (2003,360) | off-screen at spawn |
| L10 (1.0) | `pixel_1` side ×2 | (−607.4,360)/(2273.4,360) | off-screen |
| L10 (1.0) | `pixel_1` top ×2 | y ≤ 66 / 67.5 | blackout above |
| L10 (1.0) | `pixel_1` bottom | y ≥ 646 | blackout below |
| fighters | Enemy (973,−110) | (824.0, 477.0) | world x = 973+148.5−980 = 141.5 |
| fighters | Player (690,−93) | (456.0, 499.1) | world x = −141.5 |

**The key structural fact:** the tiles, walls and fighters are authored
**centred on x=0** (tiles −896…896, walls ±900, and the `tl` container subtracts
`W/2`), while the fighter XML spawns are left-origin. The parallax `Io·Factor`
is what ties the two coordinate frames together; a wrong `Io` shears every
Factor band by a different amount — this is the mechanism behind the observed
"six offset bands" (one per distinct Factor 0.4/0.5/0.65/0.8/0.95/1.0) and the
vertical seam where a high-Factor edge (wall `x=833`) no longer matches a
low-Factor edge (bg/mountains).

### 1.11 Trim / rotated atlas (`pi.VJa` L1703, `Vs.Qq` L1705, `R.Cb`/`R.Th` L1613-1615)

```
pi.VJa: Iq(filename, frame=(x,y,w,h), spriteSourceSize=(x,y,w,h),
            sourceSize=(w,h), trimmed, rotated, pivot)
Vs.Qq : Pj(id, name, fa=sourceSize, frame, yx=pivot, qj=spriteSourceSize, dL=rotated, ...)
R.Th  (L1613-1615): translate.x = -(l*h)+h+b-f+d  (the trimmed content is
            re-centred into the source frame), scale applied by Rx/Ry.
```

Net: world position = XML(X,Y) + (trim + frame/2 − source/2)·scale. The native
renders the negation-free form at `renderer.cpp:76-81` and uses `source_w/h` as
the scale denominator when `trimmed` (`location_scene.cpp:142-151`) — verified
against dojo `dojo_floor_1` (trim_y=4, frame_h=60, source_h=64 ⇒ +2·scale) and
`left_wall`/`right_wall`/`dojo_punch_bag_holder`. Dojo has **no rotated frame**
(0 of the shipped frames are `rotated`), so the transposed-UV branch
(`renderer.cpp:53-60`) is neutral today.

---

## 2. Scene composition (what draws behind / in front of the fighters)

The JS tree (dojo):

```
Render (Ut.go)
├─ layer_0 … layer_7            (bg: sky/mountains/temple/bridge/tree/wall/lamps/table)
├─ layer_8  (= hn, ModelsViewer)
│   └─ tl  ("RenderContainer", L843-844)
│       ├─ qh  (ev, fighters)   Gf L845: first fighter z=-0.001, second z=0
│       │       + dust particles (Ut.ryb L824)
│       ├─ Gq  (Xm bg effects, z=+0.01)     // OnBackground=true  (tl.Nt L842)
│       └─ Hq  (Xm fg effects, z=+0.01)     // OnBackground=false
├─ layer_9   (floor tiles, side walls, punchbag holder, layer_4 glow)
├─ layer_10  (pixel_1 blackout masks)
└─ Cu        (camera-glued node, z = lastLayerZ − 3; glow vB + shadow WV)
```

`tl.init` (L843) sets the container `translate.x = −width/2`,
`translate.y = height/2 − ct` (dojo → −980, +200), `translate.z = 0`.
`Gq`/`Hq` sit at `z=+0.01`; the first fighter at `z=−0.001`, second at `z=0`
(`ev.Gf` L845). The native reproduces the **ordering** (screens.cpp:3013-3014
`render_layers(0, fighter_layer)`, then fighters, then
`render_layers(fighter_layer+1, n)` at 3171-3174) but not the z values (D5).
`Ut.ryb` dust and `Ut.kyb` glow live in the fighter container / Render node and
are **not ported**.

The `ev.Gf` (L845) also fixes a subtlety: the **first registered fighter becomes
`Rw` (z=−0.001, behind)** and the **second `pF` (z=0, on top)**. The native
honours this (`screens.cpp:3140-3154` draws enemy first, player second).

---

## 3. dojo-vs-fight (the `Tf` hub)

`Tf` (`L1969-1972`) is the **dojo fight scene**; it is NOT a static hub:

```
Tf.init (L1971): Ig = a location+fighter pair; D1()
Tf.aa   (L1972): YL(Ig, a)  ->  a.go.node.Cx(b); a.Ea()   // render the fight
Tf.Ea   (L1972): if (Ig) this.Sya(Ig)                      // the live Sya camera
```

`Tf.Ea` calls **the same `ma.Sya`** the fight uses, and `Ut.Al` runs with the
**live `Go.ma` focus**. So in the oracle the dojo background is framed by the
fight camera (`zoom=1.3`, `Io=W/2−focus`), not by a static identity camera.
The native hub (`DojoScreen::render_impl`, `screens.cpp:1566-1572`) instead uses
`LocationScene::default_camera` (a frozen `Io=148.5`), while only the fight
screen (`FightScreen::render_impl`) drives the live `FightCamera`.

Consequences:
- At the spawn focus (831.5) both agree ⇒ the hub is *correct at frame 0*.
- Any focus change (the `ql.dZa` chase; the hub's idle fight; the intro) makes
  the hub diverge, because `default_camera` never recomputes `Io`.
- The gate's oracle capture is the tutorial **fight**, so it carries the live
  fight framing; the port hub does not — part of the 68.7 % gap is this
  screen/state mismatch, part is D1/D2 leaking into the fight path.

`m$a()` (L823) is `Lb.height * Bj`, i.e. the Sya denominator and the zoom are
**layer-zoom dependent**; the native uses `arena_h` (D11), which is only equal
while `Bj=1` (16:9).

---

## 4. Native implementation map (file:line) and the exact deltas

### 4.1 Camera / renderer

- `core/scene/renderer.hpp:40-71` — `Camera` and the projection
  `screen_x = (world_x + Io·factor − center_x)·zoom + view_w/2`,
  `screen_y = (world_y + F9·(1−zoom) − center_y)·zoom + view_h/2`,
  `layer_vshift() = ((arena_h/2−floor)/2)·(1−zoom)`.
  `Io` is computed as `arena_center_x − center_x` (`:62`).
- `core/scene/renderer.cpp:24-105` — quad build (anchor, UV, trim, layer scale).
- `core/scene/location_scene.cpp:279-335` — `default_camera`.
- `core/scene/fight_camera_sya.hpp:29-127` — `framing_sya_impl`.
- `core/app/screens.cpp:1551-1576` (hub) and `:2961-3174` (fight) — draw sites.

### 4.2 D1 — invented vertical camera (fight)

Oracle: the render camera's y offset is **0** (`N.Ta.K4` L79/85 ⇒ position=(0,0);
`Sya` only calls `b.D(...)` when `aspect<1`). Scaled layers have
`translate.y = 0` (no `Xrb`, L826-827). So `screen_y = world_y·1.3 + 360`.
Native `framing_sya_impl` computes a smoothed `center_y = go_y_`
(`:80-81,121-122`) and the renderer adds `layer_vshift` to **every** sprite
(`renderer.hpp:69-71`). Delta:

```
Δy = (F9·(1−zoom) − center_y)·zoom
      = (−30 − go_y)·1.3
   go_y = −101.5 (spawn, first frame)  -> Δy ≈ +92.9 px
   go_y -> −105.1 (du_y_ from floor@0.78) -> Δy ≈ +97.5 px
```

The whole scene (location sprites *and* fighters, since both go through
`world_to_screen_y`) is pushed down by ~93 px, growing as the chase runs. The
floor tiles (oracle y[612,689]) land at y≈745 ⇒ **off the 720 view**, and the
bottom `pixel_1` mask (oracle y≥646) is pushed to y≥786 ⇒ the "bottom strip
missing" symptom.

### 4.3 D2 — invented horizontal re-center (fight)

Native (`screens.cpp:2979-2981,3004`): `kCxOff = W/2 = 980`;
`camera.center_x = focus − 980`; `camera.arena_center_x = W/2 − 980 = 0`.
Then `Io = 0 − (focus−980) = 980 − focus` (correct), but the projection also
subtracts `center_x = focus−980`, so:

```
native_sx = (world + Io·factor + (980 − focus))·zoom + 640
oracle_sx = (world + Io·factor)·zoom + 640            (camera pos = 0)
Δx = Io·zoom = 148.5 · 1.3 = +193.05 px               (every sprite/fighter)
```

The fighters are additionally pre-shifted by `world = v − 980` (`:3031`), which
is algebraically consistent with the oracle's `tl` container `−W/2`, so the
*composition* survives; only the global framing is panned +193 px. The hub path
does **not** do this (it uses `center_x=0`), which is why the two native paths
disagree with each other as well as with JS.

### 4.4 D3 — hub static framing

`location_scene.cpp:292` hard-codes `arena_center_x = 148.5` with `center_x=0`.
At spawn this equals `Io`; it is **frozen**. The comment at `:285-291` derives
the number from the spawn midpoint, i.e. it is a per-frame JS quantity baked
into a constant. With `camera.zoom` recomputed by Sya but `Io` fixed, the hub
cannot follow the live focus.

### 4.5 D4 — `Xrb` vs `setScale`

`location_scene.cpp:353` selects the scaled branch correctly
(`type==2 || scaling`). But `renderer.hpp:60` always adds
`layer_vshift = F9·(1−zoom)` using the **render** zoom, and `:87-90` multiplies
the position by `layer_scale`. In the oracle the `Xrb` term is
`F9·(1−Bj)` applied **only** to non-scaled layers; scaled layers get
`setScale(Bj)` and no y term (`Ut.Al` L826-827). At the hub `center_y` happens
to cancel `layer_vshift`, so the bug is invisible; on the fight path it becomes
part of D1.

### 4.6 The dojo data, re-derived from the native formula

Running the native hub formula (`center_x=0, arena_center_x=148.5, zoom=1.3,
center_y=−30, layer_zoom=1`) reproduces the §1.10 table **exactly** for the
location sprites (the −30 `layer_vshift` and −30 `center_y` cancel). What the
native does **not** reproduce:

- `layer_4` glow is off-screen at spawn in both (x≈1649), so the oracle's
  visible glow comes from the **live focus** (larger `Io`) or the `kyb` glow,
  neither of which the native draws.
- The dust particles (`Ut.ryb`), the `vB` glow sprite (`Ut.V0a`), and the `WV`
  shadow (`Ut.s1a`) are absent.
- The hub idle figure/punchbag are drawn in **screen space** with `fig_cam`
  (`screens.cpp:1582-1629`, `1690`), not in container space — a native
  invention that cannot line up with the location's floor once the framing is
  fixed.

---

## 5. Exact wire-spec for a faithful rewrite

**W1 — one camera convention (all screens).**
```
render.zoom        = Sya_f                                  // 1.3 @16:9
render.layer_zoom  = Ut.Bj = min(nC/(span+300),1)           // 1 @16:9
render.center_x    = 0        // JS N.Ta position.x (K4; Sya b.C(0) is a no-op path)
render.center_y    = 0        // JS N.Ta position.y (portrait shift only when aspect<1)
render.arena_center_x = Io = Lb.width/2 - focus.x           // recomputed per frame
render.arena_center_y = 0     // (or the portrait shift)
```
Delete `kCxOff` (`screens.cpp:2979-2981,3004`) and the `center_y`/`go_y_`
injection into the render camera. `framing_sya_impl` keeps `go_y_` for the
chase state but must **not** feed it to `Camera.center_y`.

**W2 — layer transform (exact `Ut.Al` + `Qi`).**
```
for i, layer in layers:
    if layer.type == 2 or layer.scaling:
        scale_xy = layer_zoom;  translate_y = 0
    else:
        scale_xy = 1;           translate_y = F9*(1 - layer_zoom)
    translate_x = Io * layer.factor
    translate_z = -3 * i                       // Bf.init c += -3
    for j, sprite in layer.sprites:
        sprite.translate_z = -0.01 * j         // Qi.NWa/Dla
        draw(sprite, world = translate + sprite.local)
```

**W3 — fighter container (exact `tl.init` + `ev.Gf`).**
```
container.translate = (-Lb.width/2, Lb.height/2 - Lb.ct, 0)
fighter_world = model_local + container.translate      // model_local = pose
// draw enemy (registered first, z=-0.001) before player (z=0)
```

**W4 — effects.**
Route each effect by `OnBackground` (`Gfb`): bg→`Gq` (z=+0.01), fg→`Hq`
(z=+0.01), both inside `tl`; draw them between the fighters, in registration
order. Sparks/magic must use the same `tl` container offset as the fighters.

**W5 — `Sya`.**
`e = arena_h * layer_zoom` (not `arena_h`); keep the 0.45..1 aspect clamp, the
`<0.8` extra term, the width fit `min(viewW/(span*f+100),1)`, the min-zoom
`0.6..1.3`, and the `aspect<1` portrait shift. Implement the letterbox bars
(`N.BK` side bars, `Sya` top/bottom extents).

**W6 — data completeness.** Read `Rotation` (`R3a`), the `SimpleEffect`
modifier block (Oscillation/Reappear/Speed/Rotation, live `Transparency` KWa),
and the `ParticleEffect`/`NewParticleEffect` layers (`QIa`).

### Ranked fix list

1. **W1 (D1+D2)** — remove the invented vertical camera and the `kCxOff`
   re-center; unify on `center_x=0, arena_center_x=Io`. Expected: kills the
   ~93 px vertical and ~193 px horizontal global offsets; floor/masks return to
   the oracle rows (tiles y[612,689], bottom mask y≥646).
2. **W1 (D3)** — drive the hub through the live `FightCamera` (`Sya`+`Al`), or
   feed `default_camera` the live focus so `arena_center_x` is per-frame. This
   aligns the hub with the oracle tutorial-fight framing and likely moves the
   gate the most.
3. **D4/W2** — implement `Xrb` only for non-scaled layers, keyed on
   `layer_zoom`; remove the unconditional `layer_vshift` from the renderer.
4. **W3** — canonical fighter placement from `tl` (`−W/2, +H/2−floor`) and the
   XML spawns; fix the hub idle-figure/bag to use the same space.
5. **W4** — effects container + `OnBackground` routing.
6. **W6** — `Rotation`, live `Transparency`, `ParticleEffect` layers.
7. **W5** — `m$a()=arena_h·Bj`, letterbox bars.
8. **D5** — carry `z` fields (layer `−3i`, sprite `−0.01j`) even though
   insertion order currently suffices, so a future depth sort cannot regress
   the dojo.

### What is already correct (do not touch)

Trim/rotated atlas (`D13`), `pixel_1` solid masks (`D14`), the
`Scaling`/`Type`→branch selection (`D15`), the `Io = W/2 − focus` formula
(`D16`), the enemy-behind fighter order, and the bg→fighters→fg layer split.

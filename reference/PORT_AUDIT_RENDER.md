# PORT_AUDIT_RENDER — fighter / model / effects / HUD render audit (web JS → native C++)

**Scope:** audit only. No code changed by this document.
**Sources (byte-verified this session):**
- Game: `reference/www/sf2.502f0946.js` (1 601 954 B, Haxe 4.3.7). Line numbers are
  1-based against this file.
- Data: `reference/www/res/models.473fd74f.dat` (408 models),
  `animations.b22c72ff.dat` (566 clips), `animations_dojo.3314a7de.dat`,
  dojo location params + atlas.
- Native: `core/scene/model.cpp|hpp`, `core/scene/fighter.cpp|hpp`,
  `core/scene/renderer.cpp|hpp`, `core/scene/fight_camera_sya.hpp`,
  `core/scene/effects.cpp|hpp`, `core/scene/fight.cpp|hpp`,
  `core/app/screens.cpp|hpp`, `core/app/app.cpp`, `core/app/fight_assets.hpp`,
  `core/data/anim_archive.cpp`, `core/data/atlas.cpp`, `core/render/sprite_batch.cpp`.

**Companions (link, not re-derived here):**
`JS_RENDER.md` (JS render pipeline, camera, layers, effects, HUD classes),
`MODEL_FORMAT.md` (model XML schema + JS skin path), `COMBAT_STATIC.md`
(block/crit/damage), `AI_STATIC.md` (AI/tree), `DOJO_BG_STATIC.md` (dojo bg),
`FONT_METRICS.md` (fnt metrics), `reference/traces/pose_gap_analysis.md`,
`reference/traces/visual_debug.md`, `reference/extracted/scene/diff_report.txt`
(measured pixel deltas).

**Runtime measurements used below (this session):**
`build/app/anim_model_probe/Release/anim_model_probe.exe` and
`build/app/fighter_probe/Release/fighter_probe.exe` (both re-run here):
- merged fighter = **205 bones, 247 triangles, 84 capsules**.
  Parts: `mdl_skeleton` 67 bones (54 Node + 12 MacroNode + 1 COM), 0 tris;
  `mdl_body` 19 bones (15 Node + 4 MacroNode), 29 tris, 82 capsules;
  `mdl_head` 119 bones (8 Node + 111 MacroNode), 218 tris, 2 capsules.
- clip `fists1_stance_idle` = 38 frames, **67 bones** (skeleton-sized).
  ⇒ merged bones **67..204 (body+head) are NOT clip-driven** — the ragdoll
  solver + MacroNode derivation own them (matches JS, see §2.4).

---

## 0. Pipeline map (JS → native, stage by stage)

| Stage | JS anchor | Native | Verdict |
|---|---|---|---|
| Archive fetch | `Yc.load` (L568), `Ja.Lh` (L45) | `load_archive` + `find_entry` (`app.cpp:323-337`) | FAITHFUL |
| Node parse | `Yc.Ijb` (L571) | `parse_bone` (`model.cpp:17-36`) | FAITHFUL |
| Figure parse | `Yc.Uib/nkb` (L570,574) | `model.cpp:88-131` | FAITHFUL |
| Edge parse | `Yc.jjb` (L572) | `model.cpp:139-159` | FAITHFUL |
| Merge | `Yc.load` loop (L568) | `build_fighter_model` (`model.cpp:164-197`) | FAITHFUL |
| Clip parse | `vu`/`jc` ctor | `anim_clip_parse` (`anim_archive.cpp:76-125`) | FAITHFUL |
| Clip apply | `Te.eda` (L556 ≈ char 283173) | `Fighter::sample` (`fighter.cpp:645-653`) | APPROXIMATED |
| Ragdoll | `Al.ia` = `sk`+`jE` (char 296238+) | solver loop (`fighter.cpp:691-792`) | APPROXIMATED |
| Macro derive | `Fl.seb` (char 406302) | `compute_macro` (`fighter.cpp:754-791`) | FAITHFUL |
| Skin | `dv.ia` (L840) | `build_vertices` (`fighter.cpp:889-931`) | APPROXIMATED (z-sort) |
| Draw | `Yi`/`Ph` (`Fk.update` L841) | `draw_triangles` (`screens.cpp:3150-3154`) | APPROXIMATED |
| Capsule draw | `Dk`/`zu` (L835-836) | `draw_capsules` (`screens.cpp:3050-3138`) | APPROXIMATED |
| Camera | `ma.Sya` (L1833) + `Ut.Al` (L826) | `framing_sya_impl` (`fight_camera_sya.hpp:29-127`) | FAITHFUL |
| Sparks | `av` (L833) + `Ut.ryb` (L824) | `EffectSystem` (`effects.cpp`) | INVENTED |
| Magic | `cv.lwb` (L838-839) | `MagicEffects` flat quads (`screens.cpp:671-690`) | APPROXIMATED |
| HUD layout | `Sf.layout` (L2036-2038) | hardcoded (`screens.cpp:3183-3185`) | APPROXIMATED |
| HP bar | `Br` (L2011-2014) | `draw_hp_bar` (`screens.cpp:3200-3242`) | APPROXIMATED |
| Timer | `Sf.iPa` (L2036) | `screens.cpp:3244-3272` | APPROXIMATED |
| Round pips | `Er` (L2021-2022) | `screens.cpp:3274-3296` | APPROXIMATED |
| Banner | `Cr` (L2022-2027) | `draw_fight_banner` (`screens.cpp:572-633`) | APPROXIMATED |

---

## 1. Mesh build from models.dat

### 1.1 Nodes → bones (FAITHFUL)
`parse_bone` (`model.cpp:17-36`) matches `Yc.Ijb`:
`x = X; y = -Y; z = Z`, and **MacroNode x is negated** (`if (b.is_macro) b.x = -b.x;`
`model.cpp:25-27`) — exactly `e.x*=-1` in `Yc.Ijb`. `Cloth/Attenuation/Fixed`
are read (`model.cpp:32-34`). `CenterOfMass` is kept as an ordinary bone
(name `COM`; the game's `_CenterOfMass_` root), and `MacroNode` child lists
(`NodesCount`/`ChildNodeN`/`LCCN`) are captured (`model.cpp:64-79`).

Bone order = document order, first-definition-wins across merged parts
(`model.cpp:52-60`, `model.cpp:168-175`) — matches the `Xca` map guard
(`Yc.Ijb` L572: `Xa(...)||set`). **Clip bone index = merged bone index**, the
critical invariant for §2.

### 1.2 Figures → triangles (FAITHFUL)
`<Figures Type="Triangle" Node1/2/3>` (`model.cpp:91-99`) stores names; they are
resolved in `build_fighter_model` (`model.cpp:176-184`) via `bone_by_name`, and a
triangle is **dropped if any node is unresolved** (`model.cpp:180`). This mirrors
`Yc.mkb` (L574) → `dv.DXa` (L840): resolve `a.Ic(name)`, dedup via
`kM.indexOf` (`dv.Vea`), push 3 indices into `zU`.

**Exact vertex/index construction:** JS keeps a **deduped node list `kM`** plus an
index array `zU`; `dv.init()` builds `qu = jma(zU)` (Int32Array) and
`Xg = Array(kM.length*2)`; `dv.ia()` (L840) writes each deduped node's
current `ma.x, ma.y` (Z dropped) into `Xg`. Native has no separate dedup step —
`resolved_tris` are indices into the merged bone list, which is already the dedup
list, and `build_vertices` (`fighter.cpp:921-929`) **expands** each triangle to 3
vertices (no index buffer). Geometry is identical; only the representation differs.

- **`<Figures Type="Quad">` split (INVENTED, neutral):** `model.cpp:100-120`
  splits a quad into (1,2,3)+(1,3,4). JS has no Quad path; no shipped model uses
  Quad (probe: 0 quads), so it is inert.
- **Capsule figures (`Type="Capsule"`)** parse `Edge/Radius1/2/Margin1/2`
  (`model.cpp:121-128`) — matches `Yc.Tib` (L573). Rendered in §4.2.
- **Edges** parse `End1/End2/Length/Radius/Margin1/2/Collisible/BodyPart/Defense`
  (`model.cpp:139-159`) — matches `Yc.jjb` (L572). `Type="Muscle"` edges are
  parsed into the same `edges` vector (native has no separate muscle list; the
  solver treats both as rest-length constraints — see §2.4).

### 1.3 Merged part set (APPROXIMATED)
`app.cpp:330-337` + `fight_assets.hpp:73-82` merge **skeleton + (weapon) +
(armor|body) + (helm|head)**. With the default Fists loadout `weapon/armor/helm`
are empty (`app.cpp:333-336`), so the merged model is `skeleton + body + head`
= 205 bones / 247 tris / 84 capsules. The JS `xc.cM` (`L809-810`) pushes the
equipped item `Model` attributes (weapon/armor/helm) into the same single `Dl`.
**Divergence:** any equipped weapon/armor/helm mesh is never merged/rendered
(the `rebuild_player_model` path exists but the base fight uses empties). For the
Fists demo this is correct (Fists has no `Model`); for armed fights it is MISSING.

---

## 2. Animation sampling

### 2.1 Clip parse v0/v1 (FAITHFUL)
`anim_clip_parse` (`anim_archive.cpp:76-125`):
- **v1**: `u8` frame count; per frame `u16` bone count + `i16 x,y,z`; position
  `(x/16, -y/16, z/16)` (`anim_archive.cpp:86-100`).
- **v0**: `u32` frame count; per frame `1 skip` + `u32` bone count + `f32 x,y,z`;
  position `(x, -y, z)` (`anim_archive.cpp:104-118`).
Y-negation at parse matches the game (`b.y = -attr Y` in the model, `-Y` in the clip).

### 2.2 Frame/bone indexing (FAITHFUL)
`sample` clamps `frame` to `[0, len-1]` (`fighter.cpp:533`), takes
`nclip = min(clip_bones, n)` (`fighter.cpp:547`) and maps **clip bone i → merged
bone i** (`fighter.cpp:562-581`). Because the merged model is skeleton-first and
the clip has 67 skeleton bones, bones 0..66 are clip-driven and 67..204 keep the
solver state — the exact JS structure (`Te.eda` loops `Va.all`, indexes
`fq[c][mo]` sized to the clip's `ZW`).

### 2.3 Interpolation (FAITHFUL)
JS `Te.Gka`/`wu.f6a` builds a quadratic Bézier `P0=mid(a,b), P1=b, P2=mid(b,c)`
at `t=(mo+1)/UM`, `UM=(XJ+1)·Tx`. Native (`fighter.cpp:551-582`):
`t_bez=(subframe+1)/sub`, `w0=(1-t)², w1=2(1-t)t, w2=t²` over frames
`f, f+1, f+2`; falls back to linear at `sub_i==1`.
Pacing `sub=(mid_frames+1)` (`fighter.cpp:461`, `fighter.cpp:340`) matches
`(XJ+1)·HD()`. Verified against the oracle trace: `stance_1` 46f/FirstFrame 2 →
F2..F134 = (46−2)·3+1 = **133 exact** (`pose_gap_analysis.md §2`).

### 2.4 Solver + macro (APPROXIMATED)
Because the clip only has 67 bones, body cloth (15 Node) + head nodes (8 Node)
are simulated and the 115 MacroNodes are derived. Native ports JS:
- `Vc.sk` (char ~405620): `v = ma−mf`; if damped `v *= 1−bI`; `ma += v; ma.y += a`
  (`a = fDa/(HD·HD) = 0.4`); `mf = ma`. Native `fighter.cpp:691-712` matches
  (grav `0.4`, cloth damping `1−attenuation`).
- `Al.sk()` guard (char 296938): `!NG && (nk || jy || oa.vc&&vc) && sk()`.
  Native skips `fixed || macro` (`fighter.cpp:695`); `nk` is always true in a live
  fight, so this is equivalent for the movable non-clip bones.
- `jE` edge relaxation: JS `IterativeProcess` (`xd.jE`); native `kEdgeIters=2`
  mass-weighted relax (`fighter.cpp:713-753`). Matches `yu.bFa`.
- `Fl.seb` (char 406302): `if(Ega) Ega=false; else { mf=ma; ma=Σ child.ma·weight }`.
  Native `compute_macro` (`fighter.cpp:754-791`) is the same weighted sum, and only
  for macros with `i >= nclip` that were not clip-posed.

**Native deviations (candidates for the reported stretch):**
1. **Warmup 600 steps** (`fighter.cpp:58`, `fighter.cpp:668-671`) is INVENTED. The
   game's cloth settles during the Dojo-hub display; the native boots straight in.
2. **COM-delta state translation** across clip switches (`fighter.cpp:621-644`) is
   INVENTED (the JS whole fighter teleports with its world position; the solver
   space continuity is the native's interpretation).
3. The native integrates **clip-driven bones too** (only `fixed||macro` are
   skipped), exactly like JS `Al.sk` when `nk` is true — but the JS clip bones are
   re-pinned by `Te.eda` every frame, so any mismatch in *when* eda runs relative
   to `sk` can produce a one-frame overshoot. Re-verify the frame order:
   JS `ia()` = `da.ia()` (Te.eda) → `Nd.ia()` (Al: sk+jE) → `oa.Qja()` (macros).

### 2.5 Root motion (APPROXIMATED)
Native applies only the **COM x delta per clip frame, divided over subframes**
(`fighter.cpp:471-490`). JS `Gub`/`Gla` shift the whole clip buffer by the align
offset `Fk` (`EObjectAnimation/EObjectNodes/EObjectPivot/EObjectWall`) once, plus
the accumulated `j8` root offset in `Te.eda`. **No align offset, no y/z root
motion in native.** `pose_gap_analysis.md §3-4` measured a 2.4–3.6× root
over-application historically; the current distribution-over-subframe loop is the
attempted fix — re-measure.

---

## 3. Mirror + projection + scale (where a stretch can arise)

### 3.1 Facing mirror (APPROXIMATED)
Native (`fighter.cpp:512-518`): local x is mirrored about the COM wherever
`facing<0`: `pos_x = (px[i]−px[com])·f + x`, `f = ±1`.

JS: `Te.Qeb` (L550) negates the **entire clip buffer x** only when
`this.FX==-1 && this.jc.Neb()`; `Neb()` is align-gated. Native mirrors whenever
`facing<0` (no `Neb` gate). For symmetric/aligned clips this is a no-op; for
asymmetric root-relative clips it can differ. Also: knockback offsets are added
**after** the mirror (`fighter.cpp:869-872`), so per-bone knockback is not
mirrored — negligible (impulses are already world-directed).

### 3.2 `_1`/`_2` mirror swap (APPROXIMATED)
Native builds `_1↔_2` pairs (`fighter.cpp:63-73`) and swaps on
`world-order(prev) != buffer-order` (`fighter.cpp:820-841`). JS `Te.Peb` (L560)
→ `Te.MYa`/`Te.lwa` (L566) decides the swap from crossed left/right bone x, and
`Te.xqb` (L553) swaps edge-event names. Structurally similar heuristic, not the
exact `lwa` predicate.

### 3.3 Projection + scale (APPROXIMATED — the main risk area)
`FightScreen::render_impl` projection `project()` (`screens.cpp:3021-3035`):
```
kContY   = arena_h/2 − floor            (container offset; tl.init L843)
kCxOff   = arena_width/2                (sim(0-based) → art(centered) re-center)
sx = camera.world_to_screen_x(world_x − kCxOff, 1.0f)
sy = camera.world_to_screen_y(world_y + kContY)
```
If vectors are resolved by that formula the geometry is placed correctly for the
dojo (kContY fix landed). **Remaining numeric deltas:**
- **Layer zoom Bj is not applied to fighters.** `world_to_screen` uses
  `camera.zoom = f` only; JS scales the fighter-containing `hn` layer by
  `Bj = Ut.xCa() = min(1, nC/(span+300))` (`Ut.Al` L826-827) *and* the camera by
  `f` → effective fighter scale `f·Bj`. At 16:9/dojo, `nC = viewW/(viewH/arenaH)
  = 1280/(720/560) = 995.6`, so `Bj = 1` while `span ≤ 695.6` (typical). Only in
  wide zoom-outs (`span > ~695`) is the native fighter **1/Bj too large**
  (e.g. span 900 → Bj 0.829 → 1.21×). Layers *do* get `layer_scale=Bj`
  (`screens.cpp:2991`, `sprite_to_quad` `renderer.cpp:87-90`), so the fighters
  and background dis-agree in that regime. **Fix:** multiply the fighter world
  coords by `camera.layer_zoom` in `project()`.
- **Winding:** model `DoubleSided="-1"`; native emits one winding and the GL
  state has **no face culling** (`begin_frame` enables only `GL_BLEND`, no
  `GL_CULL_FACE`; `sprite_batch.cpp` binds no cull) → both faces visible,
  matching `Ph`'s `(t&2)` reversed re-emit (L1581-1583). FAITHFUL.
- **Per-axis scale:** none anywhere — scale is uniform (`camera.zoom`). A *true*
  stretch is therefore **not** a projection issue; it comes from the pose
  (solver/cloth, §2.4) or the **triangle draw order** (§4.1).
- **Z-sort (INVENTED):** `build_vertices` stable-sorts triangles by mean pose z
  (`fighter.cpp:905-920`). JS `dv.ia` emits triangles in **XML document order**
  (`dv.zU` push order, `Fv.qu`). Sorting changes which limb is on top in
  overlaps → reads as "some triangles wrong" against the oracle. This is the most
  likely direct cause of the reported "some triangles wrong".

### 3.4 World placement (APPROXIMATED)
Native anchors the **COM solved position** at `(x,y)` (`fighter.cpp:850-865`),
matching the oracle (`world_y` is the COM, not the feet — verified −93 vs feet
~5). But `px[com]` is the **solved** COM (integrated in §2.4), not the clip COM
`vec`; if the solver moves the COM one frame, the whole body shifts. The JS
`wd.oL` offsets all bones relative to the (clip/`Dl.mea`) COM. Verify `com_y`
uses the clip value, not the post-solve value.

---

## 4. Draw order + colors / alpha

### 4.1 Order (FAITHFUL with the z-sort caveat)
Native (`screens.cpp:3149-3154`): **enemy first** (`Rw`, z=−.001) then **player**
(`pF`, z=0) — matches `ev.Gf` L845. Background layers → fighters → sparks/magic →
foreground layers (`screens.cpp:3013-3014`, `3161-3162`, `3171-3174`), matching
`tl.init`'s ordering (models between fog and floor). Within a fighter, capsules
are drawn **before** the mesh (`draw_capsules` then `draw_triangles`), whereas JS
`dd("Model")` child order is Mesh → ModelCapsules → weapon (`wd` ctor L492), i.e.
**mesh first, capsules over it**. Minor overlap delta.
The intra-mesh order is the INVENTED z-sort (§3.3).

### 4.2 Colors / alpha (APPROXIMATED)
- Fighter fill = location root color (`ev.Gf` `aXa(a.oa, Lb.N2)` L825). Native
  default black and `set_fighter_color` from the location root (`fight.cpp:278`,
  `app.cpp`/dojo). FAITHFUL.
- JS `Ph` (L1570-1583) draws the mesh as a Path2D with **fill + stroke** (double
  pass, alpha). Native `draw_triangles` is a single flat fill, alpha 1.0
  (`screens.cpp:3150-3154`). APPROXIMATED (no stroke pass).
- Capsules: JS `Dk` (L835-836) = one stroked segment per `<Capsule_*>` figure,
  `stroke = Radius1*2`, continuous round ends. Native dedups by edge name keeping
  **max Radius1** (`screens.cpp:3058-3065`), draws a straight quad + two
  12-segment discs (`screens.cpp:3066,3106-3138`). Delta: JS has 84 figure
  capsules with per-figure radii; native collapses duplicates by edge
  (e.g. EThigh 12+15 → 15) and approximates the arc with 12 chords. The JS arc
  flatten uses `precision=0.1` → `n = π/(2·acos(1−0.1/r))`.

---

## 5. Effects

### 5.1 Banners (APPROXIMATED)
- JS `Cr` (L2022-2027): an **atlas image** `E.get(1310)` frames (`y.BQa`, `y.zQa`,
  `y.wQa`, `y.Kna`, `y.Lna`, …) + a bitmap-font `round` label
  (`fontSize·1.6`), positioned by `layout()` using `ma.Kq` and scaled by
  `min(800, min(N−J, W−P))/image.w · .6`.
- Native (`screens.cpp:572-633`): draws **text** ("ROUND N"/"FIGHT!"/"K.O."/
  "VICTORY"/"DEFEAT") with the **menu font** and an invented envelope
  (`banner_scale_at` 15% scale-in, `banner_alpha_at` last 20% fade; hold cap 0.8
  at `screens.cpp:589-592`). Text content is driven by `FightController::banner_text()`
  (`fight.cpp:1879-1890`), not from data. Uses no `Cr` atlas art.

### 5.2 Sparks (INVENTED)
JS `av` (L833-834): texture 260, spawn per particle
`fg=(x/200 + rand(−40..40)/10, y/200 + rand(−60..20)/10)`, update
`fg.y += .2`, rotation `atan2(fg.y,fg.x)·57.3·(fg.x<0?−1:1)`, scale `.3`;
spawned by `Ut.ryb` (L824) with a `count`, cleared when `uba>90`. Native
`EffectSystem` (`effects.cpp`) is a private-LCG fan: 8–14 particles, speed 2–6,
spread ±0.9 rad, gravity 0.35, life 20–30, size 3–7, warm palette. Deliberately
deterministic (never touches the fight `roll01`). **Tuning is invented**, not the
JS numbers.

### 5.3 Magic (APPROXIMATED)
JS `cv.lwb` (L838-839): frame animation from `magic/{name}.json` + `.png` via `ni`,
`iterations`/`RLa`/`wrb`, speed `NL/60`, attached to a bone via `bv`. Native
draws flat tinted screen-space quads (`screens.cpp:671-690`, `MagicEffects`), no
frame animation, no bone attach.

### 5.4 Missing
- **Hit flash `Hyb`** (L825: sprite 1306, `lo`, sine alpha via `kyb`) — **MISSING**
  (no hit sprite draw in native).
- **Off-screen marker arrows `sXa`** (L827-828, texture 1300, frames "0".."19")
  — **MISSING**.
- **Ground/air effect layers `Xm`** (`Gq`/`Hq`, z=+.01, L843-844) — **MISSING**
  as separate layers.
- **Location particles `Ah`/`ar`, `jh` ParticleEffect, SimpleEffect Oscillation/
  Reappear** — **MISSING** (see `JS_RENDER.md §4.5`).
- **Camera shake** exists as `FightCamera::shake` (`fight.cpp:76`) but is an
  approximation of `ql.DL`/`d3a`/`Byb`.
- **Per-fighter shadow** — correctly **ABSENT** (JS has none; the old native
  ellipse was removed, `screens.cpp:3016-3019`). FAITHFUL.

---

## 6. Fight HUD

### 6.1 Layout `Sf.layout` (APPROXIMATED — 1280×720 numbers)
JS (L2036-2038), for aspect `c = viewW/viewH`:
```
d = clamp(c, .4, 1.5); e = clamp(d, 1, 1.1);
c0 = min(viewW, viewH)/2; f = c0*.07; if (d<1) f += (1-d)*200;
g = 1 + ((clamp(d,.4,1.5)>) −1)/.5*.1;   // =1+((d−1)/.5)*.1
c0 = c0/675*g;
Id.node.C((J+N)/2 − 520·c0·e);   Id.node.D(P + 150·c0 + f·g)
Kp.D(Id.node.ra − 120·c0);       Kp.ua(120·c0)   // timer font px
Jn.node.D(Id.node.ra + clamp(1−d,0,1)*25)
Er.kva(425): x = ±(130 + 425 − tan(25°)*43) = ±534.95
```
At 16:9 (1280×720): `d=1.5, e=1.1, g=1.1, c0=360/675*1.1=0.58667`,
`f=25.2`, `f·g=27.72`, `150·c0=88.0` → panel **X = 640 ∓ 335.58 = 304.4 / 975.6**,
**Y = P + 115.72**, timer font **= 70.4 px**.

Native (`screens.cpp:3179-3185`): `bar_w=440, bar_h=25, bar_y=115.7`,
`bar_cx = 640 ∓ 520*0.5867*1.1 = 640 ∓ 335.6`. **X matches; Y matches only if
`Kq.P` (world-top screen y) = 0** — native drops the `P` term (no `ma.Kq`
implementation). `bar_h 25 ≈ 43*0.5867 = 25.2` ✓. **`bar_w=440` is not from JS**
(the JS `Br` bar is local `uL(425)` scaled by the panel `c0=0.5867` → ≈249 px).

### 6.2 HP bars (APPROXIMATED)
Native `draw_hp_bar` (`screens.cpp:3200-3242`): `HealthBar_Empty` bg,
`HealthBar_Hit`/`HealthBarBlue_Hit` leak under `HealthBar_Full`/`HealthBarBlue_Full`
fill, with a two-layer decay (`HudBarDecay`, `screens.cpp:174-224`) mirroring
`Br.Qyb` (L2012-2013: `v5`/`g5`, 10/30-frame tweens, `zO` hold). Segment stripes
(`Br.b_` L2013) are not modeled. Frame height 43 matches (`krb()` L2012).

### 6.3 Timer (APPROXIMATED)
JS `Sf.iPa` (L2036): `--xU; NF = xU/60|0; Kp.V((NF<10?"0":"")+max(0,NF))`
→ **zero-padded 2-digit**, font `120·c0 = 70.4 px`. Native
(`screens.cpp:3246-3260`): `std::to_string(max(0, time_nf))` — **no zero pad**,
and `scale=0.75` on a ~80 px digit glyph ≈ **60 px** (≈15 % small).

### 6.4 Round pips (APPROXIMATED)
JS `Er` (L2021-2022): pip size `e = (rounds==2 ? 40 : 32)`, pitch
`e + e/2 = 48` (or **60** when rounds==2), rotation `25°·(type==0?−1:+1)`,
height `Pb(43)`, anchored via `kva(425)` at `±534.95` (plus per-pip pitch).
Native (`screens.cpp:3274-3296`): `18×18` pips at pitch **22 px**, **no tilt**,
x = `78 + i*22` (player) / `1280−78−18 − i*22` (enemy). Delta: pitch 22 vs 48/60,
size 18 vs 32/40, tilt 0 vs ±25°.

### 6.5 Pause / Next / gamepad
- Pause icon `Jn` (`fight/ui` `FightPause`) at `(1216, 40)` — native
  (`screens.cpp:3346`) hardcodes 1216/40; JS `Jn` is laid out via `layout()`
  (top-right, `D(Id.node.ra + …)`).
- Next button (`FightPause`) at `(640, 432)` — JS `vhb` L410 case 1 (between
  rounds) with a scale animation; native flat fallback + label.
- Gamepad (`draw_gamepad`, `screens.cpp:2576`): node 440×596 local, `e=150`,
  joystick base frame 466, button hit r=115 — ported from the JS `fu` layout
  (`screens.cpp:261-291`). Pad *icons* are the original controller atlas art
  (proven), so the "wrong icons" report was a display/scale issue, not art.

---

## 7. Verdict matrix (file:line + numeric delta)

| # | Item | Native | Verdict | Delta |
|---|---|---|---|---|
| 1 | Node parse (y=−Y, macro x=−x) | `model.cpp:17-36` | FAITHFUL | 0 |
| 2 | Bone order / first-wins | `model.cpp:52-81,164-197` | FAITHFUL | 0 |
| 3 | Triangle resolve (drop missing) | `model.cpp:88-99,176-184` | FAITHFUL | 0 |
| 4 | Quad split | `model.cpp:100-120` | INVENTED (inert) | 0 shipped |
| 5 | Capsule/edge parse | `model.cpp:121-159` | FAITHFUL | 0 |
| 6 | Merged part set | `app.cpp:330-337`,`fight_assets.hpp:73-82` | APPROXIMATED | weapon/armor/helm missing |
| 7 | Clip parse v0/v1 (y=−y) | `anim_archive.cpp:76-125` | FAITHFUL | 0 (clip bytes 1.0000 vs oracle) |
| 8 | Bone index map | `fighter.cpp:546-547` | FAITHFUL | 0 |
| 9 | Bézier interpolation + pacing | `fighter.cpp:551-582,459-511` | FAITHFUL | 0 (durations exact) |
| 10 | Root motion | `fighter.cpp:471-490` | APPROXIMATED | x-only; align `Fk` missing; historical 2.4–3.6× |
| 11 | Ragdoll solver | `fighter.cpp:691-792` | APPROXIMATED | warmup 600 invented; order = eda→sk→jE→macro |
| 12 | Macro derive | `fighter.cpp:754-791` | FAITHFUL | 0 (Σ child·LCC) |
| 13 | Mirror swap `_1/_2` | `fighter.cpp:820-841` | APPROXIMATED | heuristic vs `lwa` |
| 14 | Facing mirror | `fighter.cpp:512-518` | APPROXIMATED | no `Neb` align gate |
| 15 | Triangle z-sort | `fighter.cpp:905-920` | INVENTED | JS = document order |
| 16 | Winding / no-cull | `sprite_batch.cpp:180-218` | FAITHFUL | DoubleSided ok |
| 17 | Enemy/player order | `screens.cpp:3149-3154` | FAITHFUL | enemy z=−.001 first |
| 18 | bg/fg layer split | `screens.cpp:3013-3014,3171-3174` | FAITHFUL | — |
| 19 | Fighter projection | `screens.cpp:3021-3035` | APPROXIMATED | no `layer_zoom`; kCxOff/kContY inventions |
| 20 | Capsule render | `screens.cpp:3050-3138` | APPROXIMATED | dedup+12-seg vs per-figure arcs |
| 21 | Shadow | absent | FAITHFUL | JS none |
| 22 | Banner | `screens.cpp:572-633` | APPROXIMATED | text vs `Cr` atlas art |
| 23 | Sparks | `effects.cpp` | INVENTED | JS `av` numbers not used |
| 24 | Magic fx | `screens.cpp:671-690` | APPROXIMATED | quads vs `ni` frames |
| 25 | Hit flash `Hyb` | — | MISSING | — |
| 26 | Marker arrows `sXa` | — | MISSING | — |
| 27 | Effect layers `Xm` | — | MISSING | — |
| 28 | Camera shake | `fight.cpp:76` | APPROXIMATED | — |
| 29 | HUD bars | `screens.cpp:3200-3242` | APPROXIMATED | bar_w 440 vs 425·c≈249 |
| 30 | Timer | `screens.cpp:3244-3272` | APPROXIMATED | no zero-pad; ~60 vs 70.4 px |
| 31 | Round pips | `screens.cpp:3274-3296` | APPROXIMATED | pitch 22 vs 48/60; tilt 0 vs 25° |
| 32 | HUD layout `Sf`/`Kq` | `screens.cpp:3183-3185` | APPROXIMATED | Y drops `Kq.P`; hardcoded 16:9 |
| 33 | Layers/parallax camera | `fight_camera_sya.hpp`, `renderer.cpp` | FAITHFUL | F9/vshift/Io match L826 |

---

## 8. Wire-spec (data contract + exact transforms)

**A. Model wire.** `models.*.dat` entry → XML →:
- bones: `{name, x=X, y=−Y, z=Z, macro(x=−X), mass, cloth, attenuation, fixed}` in doc order, first-wins across parts.
- triangles: `{n1,n2,n3}` → resolve via bone name map → `{i1,i2,i3}` (skip if any unresolved).
- capsules: `{edge, radius1, radius2, margin1, margin2}`.
- edges: `{name, end1, end2, length, radius, margin1, margin2, collisible, body_part, defense}`.
- merge: skeleton first, then parts in `lx` order.

**B. Pose wire.** `clip = {version, frames[{bones[{x,y,z}]}]}` (y negated, /16 for v1).
`sample(clip, frame, world_x, world_y, facing)`:
1. interpolate Bézier `f,f+1,f+2` at `t=(subframe+1)/(MidFrames+1)`.
2. `eda`: for `i<nclip`: `mf=ma; ma=clip`.
3. `sk`: for movable non-macro: `ma += (ma−mf)·damp + (0,0.4,0)`; `mf=ma`.
4. `jE` ×2: rest-length mass-weighted relax over all edges.
5. `Fl.seb`: for `i≥nclip` macros: `ma=Σ child.ma·LCC`; `mf=ma`.
6. mirror `_1/_2` swap when `facing<0` and world/buffer order disagree.
7. world: `pos = ((p[i]−p[COM])·facing + world_x, (p[i].y−p[COM].y) + world_y)`, plus knockback.
8. `compensate`: multiply world coords by `camera.layer_zoom` before projection (**missing**).

**C. Projection wire.** `screen_x = (wx·Bj + (arena_cx−center_x)·1 − center_x)·zoom + view_w/2`;
`screen_y = (wy·Bj + ((arena_h/2−floor)/2)(1−zoom) − center_y)·zoom + view_h/2`.
Add the container offset `kContY = arena_h/2 − floor` once; apply the art re-center
`kCxOff = arena_width/2` once (native applies it in both `center_x` and `project`).

**D. Capsule wire.** For each `<Capsule_*>`: segment `[end1.pos, end2.pos]`,
half-width `Radius1`, round caps; **no dedup** (or dedup only when two figures
share the same edge and one is a subset).

**E. HUD wire.** `Kq = {N=viewW, P=world-top screen-y, W=arena_h·Bj}`;
`c0=min(N−J,W−P)`; bars at `(J+N)/2 ∓ 520·c0'·e, P+150·c0'+f·g`; timer font
`120·c0'`; pips pitch `1.5·e`, tilt `±25°`, `e=(rounds==2?40:32)`.

---

## 9. Ranked fix list

**P0 — visual correctness (triangle/pose):**
1. **Restore document draw order** in `fighter.cpp:905-920` (remove/guard the z-sort),
   or prove it against the oracle. Directly targets "some triangles wrong".
2. **Apply `camera.layer_zoom` (Bj) to the fighter projection** (`screens.cpp:3021-3035`)
   so fighters and layers scale together for `span > ~695`.
3. **Re-validate the solver**: warmup 600 (`fighter.cpp:58,668`) and COM-delta
   translation (`fighter.cpp:621-644`) are invented. Compare cloth edge lengths vs
   the oracle idle pose; a mis-settled cloth is the prime "stretch" suspect.
4. **Root motion**: implement the JS align offset (`Gub`/`Gla`, `Te` L557-560) and
   confirm §2.3→2.5, re-measuring the 2.4–3.6× over-application
   (`pose_gap_analysis.md §4`).

**P1 — HUD parity:**
5. HP bar geometry from `Sf.layout`: width ≈ `425·c0` (native 440), Y term `+Kq.P`.
6. Round pips `Er`: pitch 48/60, size 32/40, tilt ±25° (`screens.cpp:3274-3296`).
7. Timer: zero-pad to 2 digits; font `120·c0 = 70.4 px` (`screens.cpp:3248-3260`).
8. Capsules: render **per figure** (no max-radius dedup) with continuous round caps.
9. Banner: use the `Cr` atlas frames (`E.get(1310)`) + the `round` glyph font and
   the `layout()` geometry, not menu-font text.

**P2 — missing effects:**
10. Hit flash `Hyb` (sprite 1306), marker arrows `sXa` (1300), `Xm` ground/air
    layers, `Ah`/`ar`/`jh` particles, `ni` magic frames (`magic/*.json`).
11. Merge equipped weapon/armor/helm models into the fighter (`rebuild_merged`
    already supports it — wire the equipped `Model` attributes).
12. Facing mirror gate `Neb` + `lwa` bone-pair detection for exact `Te.Qeb`/`Te.Peb`.

---

## 10. Divergence summary (for the orchestrator)

1. **Mesh build is faithful** (nodes/triangles/edges/capsules/merge/first-wins).
   Only equipment parts (weapon/armor/helm) are not merged in the base fight.
2. **Clip parse + interpolation + pacing are faithful** (bytes match oracle
   `1.0000`; durations exact). 67-bone clips drive only the 67 skeleton bones;
   body/head (138 bones) are solver+macro — verified.
3. **The solver is an approximation**: warmup 600 and COM-delta continuity are
   invented; the JS frame order eda→sk→jE→macro is roughly followed. Prime suspect
   for residual cloth "stretch".
4. **Projection's real delta**: fighters do not inherit the layer zoom Bj
   (only matters for `span > ~695`); `kCxOff`/`kContY` are native inventions to
   reconcile sim vs art space. No per-axis scale exists, so a true "stretch" is
   pose-side, not projection-side.
5. **Triangle z-sort is invented** (JS = document order) — likely "some triangles wrong".
6. **Draw order enemy→player and bg→fighters→fg match** JS; per-fighter capsule
   order is reversed (capsules drawn over mesh vs JS mesh over capsules).
7. **HUD is approximated with concrete numeric deltas**: bar width 440 vs ~249,
   round-pip pitch 22 vs 48/60 and no 25° tilt, timer not zero-padded and ~60 vs
   70.4 px, Y drops `Kq.P`.
8. **Effects**: sparks invented, magic approximated, hit flash/markers/`Xm`
   layers/particles missing; shadow correctly absent; banners are text not `Cr` art.
9. **No code changed; no commit; `.planning/STATE.md` untouched.**

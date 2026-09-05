# DOJO_BG_STATIC — Dojo interior location, HUD bar, portraits, bag (web build)

Support research for Stream 1's Dojo render fix. Static-only, from
`reference/www/sf2.502f0946.js` (2533 lines, 1-based) + on-disk files
(all read-only). Anything not found is marked OPEN with where I looked.

## 1. Which location the Dojo home screen uses

- Dojo home = `Tf`, `dJ()=3` (L1969-1972). `Tf.init` (L1971):
  `a=p.V$a().a0("FightNone")[0].g0(0)` — first FightNone fight of the
  start zone (Punchbag `Training`, `Location="dojo"`, stages.xml);
  override `a.location=p.o_.Dk` if set; `this.Ig=v.m1a(a)` drives the
  scene (dojo runs a live fight controller, `aa→YL`, L1971-1972).
- Verdict: **same location id as the fight dojo** (`dojo`), NOT a
  separate home asset. (`dojo_shop` exists under `locations/` but `Tf`
  never references it — 0 JS hits.)
- Dojo asset ids (`Tf.Yv`, L1970): models 753/754/755 (dojo variants),
  314/315 base models, animations 1354/1355/1317/1316 etc.

## 2. Dojo layer list (authoritative)

File: `reference/www/res/locations/dojo/dojo_params.b78df4b4.xml`
(`<Root Width=1960 Wall=80 Pages=1 Height=560 Floor=80>`).
Atlas manifest: `reference/www/res/locations/dojo/dojo.d31b1e71.json`
(24 frames; images only as `.avif`/`.ktx`/`.webp` in the same dir —
no plain PNG ships in www).

| # | Factor (parallax) | ClassName → atlas frame (px) |
|---|---|---|
| 1 | 0.4 | `_0015_bg` 1936×512 (sky backdrop) |
| 2 | 0.5 | `_0014_mountains` 646×350 |
| 3 | 0.65 | `_0013_temple` 612×247 (garden temple) |
| 4 | 0.65 | `_0012_bridge` 557×162 |
| 5 | 0.8 | `_0011_tree_and_light` 402×331 |
| 6 | 1.0 | `_0010_Wall` 1936×512 (interior wall) |
| 7 | 1.0 | `_0009_lamp_left` 109×124 + `_0008_lamp_right` 109×122 |
| 8 | 0.95 | `_0001_go_table` 422×73 |
| 9 | 1.0 | `ModelsViewer` (spawns P(690,-93) / E(973,-110), no art) |
| 10 | 1.0 | `layer_4` 504×484 (animated `SimpleEffect` transparency loop) + `dojo_floor_1/2` tiles 256×60 + `dojo_punch_bag_holder` 312×146 + `left_wall`/`right_wall` + `pixel_1` masks (letterbox) |

Naming note: no frame is literally called pagoda/shoji/beams. Closest
matches: garden pagoda → `_0013_temple`; interior beams → `_0010_Wall`;
lanterns → lamp_left/right. The unused atlas rows (`_0002.._0007`
boss weapons, `_0000_arena_floor`) belong to other locations sharing
the sheet family.

## 3. Top HUD bar composition (`za` chrome, L1973)

- `PL=R.$(E.get(260), y.QRa)` = `topPanel`, misc atlas (manifest
  260→`ui/misc.{image}`), + children `wr` (level) + `xr` (power/energy)
  + `yr` (coins/gems).
- `xr` (L1984): icon `energy`, np `unlimited_energy`, `zr` progress bar.
- `wr` (L1986): icon `level`, np `max`, `Uf` progress bar.
- `yr` (L1990-1995): icon `gold`, `ruby`, `ea` text labels (`Dq`/`au`
  "0"/"00"), `+` button = `AddMoney`/`AddMoney_Pressed` (`M1a`,
  L1995 — created ONLY with `hasFeature("iap")`).
- `star` (`PRa`) is NOT on this bar — achievements `ns` widget (L2303).

## 4. МЕНЮ scroll-tab — OPEN

No dedicated МЕНЮ frame found (all `y.*` constants L2463-2466 scanned;
no menu-tab frame in any `ui/*.json`). Menu scroll titles are text
(`Y.na("menu")`, L1977); tab strips are `Le` sprites (§2 of
UI_EXCLUSIVITY). If Stream 1 means a specific tab strip, cite its
screen — Shop tabs are `ss` (SHOP_STATIC §4).

## 5. Sensei portrait (green-circle)

- Files (on disk): `reference/www/res/users/images/
  character_sensei_small.{2045b126.avif,afa88293.webp}` (256²) and
  `character_sensei.{1e05d2f6.avif,7f2b509e.webp}` (+`_young`
  variants). Manifest has the same names.
- Loader `oe` (L1823): `res/users/images/<name>.png` → hashed file;
  512px, or 256px when name contains `_small`. Quest dialogs pass
  `Image="character_sensei_small"` (`He`, L1045-1048).
- Pixels (sampled): corners opaque dark brown (42,33,15), center
  near-white — **no green ring baked in**. The green circle is dialog
  chrome (mask/ring drawn around the portrait). OPEN: exact ring draw
  call not located (He image path L1045-1048 has no circle primitive;
  likely Xc dialog frame or shell-side).

## 6. Punching-bag sprite

- Items (`list.xml`): `PunchingBag` (`mdl_punching_bag`, Armor),
  `SkeletonPunchingBag` (`mdl_skeleton_punching_bag`, Skeleton) — 3D
  models in `models.dat`, worn by the Punchbag dummy fighter. No bag
  PNG exists.
- Decor: `dojo_punch_bag_holder` 312×146 (beam mount, §2 row 10).

## OPEN

1. МЕНЮ scroll-tab (§4).
2. Green-circle ring draw site (§5).
3. `Tf.D1()`/`G.Qr` decor extras (L1971) — not inventoried.

## 7. Lower half (below the arena floor band)

Source: `reference/www/res/locations/dojo/dojo_params.b78df4b4.xml`
(single-line XML, L1) + `reference/www/res/locations/dojo/dojo.d31b1e71.json`
(23 frames) + JS cites below. All `X/Y` below are element CENTER
(`Bf.R3a` L486: `b.C(X);b.D(Y);...;b.ik(.5,.5)` — anchor 0.5,0.5).

### 7.1 Root: Wall=80 / Floor=80 / Height=560 semantics

- Root L1: `<Root Width="1960" Wall="80" Pages="1" Height="560"
  Floor="80" Color="0x000000">`.
- Parse `Bf.init` L474: `this.NU=u.H(...get("Wall"))`,
  `this.ct=u.H(...get("Floor"))`, `this.Tza=...PositionY`,
  `this.N2=...Color`, `this.width=...Width`; L475:
  `this.height=...get("Height")`.
- Bounds `ca` L381-382: `v.tFa=this.location.NU`,
  `v.NKa=this.location.width-this.location.NU` → Wall=80 is the
  left/right arena clamp (80 .. 1880 in arena X, origin at left edge).
- Floor=80 (`ct`) is the vertical floor offset: `tl.init` L843:
  `translate.x=-a.width/2`, `translate.y=a.height/2-a.ct`
  (=280-80=200 for dojo); `Ut.init` L823:
  `F9=(Lb.height/2-Lb.ct)/2` (=100); `Ut.mwa`/`Al` use `ct`
  (L823-824, L826-827: `setScale` vs `Xrb(F9*(1-Bj))`, `Wrb(Io*bp)`).
- Half-extent helper `oCa` L475:
  `return new H(this.width/2,this.height/2,0,1)` (980 x 280).

### 7.2 Which y each lower-band element sits at (XML centers)

Layer 7 (Factor 0.95): `_0001_go_table` X=647 Y=170.5 W=422 H=73
(atlas frame 422x73, exact) → spans Y ~134..207, X 436..858.
Layer 9 (Factor 1, AFTER combat layer): `layer_4` Picture X=627.5
Y=-15.312 W=600 H=529.375 (`SimpleEffect` Transparency loop,
parsed `UIa` L478-479); 8x `dojo_floor_1/2` tiles Y=223.5 X=-896..
896 step 256, W=256 H=59 (atlas frames 256x60, src 256x64) →
band Y ~194..253, X -1024..1024 (2048 wide > 1960 arena);
alternation 2,1,2,1,2,1,2,1 from X=-896; `dojo_punch_bag_holder`
X=-10 Y=-203.5 W=310 H=145 (atlas 312x146, src 316x148) →
Y -276..-131; `left_wall` X=-900 Y=0 W=74.175 H=560 → X -937..-863,
Y -280..280 (full height); `right_wall` X=900 Y=0 W=78.379 H=560 →
X 861..939, Y -280..280 (atlas left_wall 70x512 src 80x512,
right_wall 73x512 src 79x512; XML→atlas scale via `R3a` Rh/mj L486).
Layer 10 (Factor 1, last): 5x `pixel_1` (1x1 atlas frame):
sides X=∓1108 W=350 H=860 Y=0 → X 933..1283 / -1283..-933,
Y -430..430; top Y=-426 W=1960 H=400 → Y -626..-226;
mid-top Y=-325 W=2000 H=200 → Y -425..-225;
bottom Y=320 W=2000 H=200 → Y 220..420.

### 7.3 pixel_1 masks / letterbox

- Constructor `ujb` L477: `e=="pixel_1" ? (w=Width,h=Height,
  b=R.Ed(color,w,h), b.Rn(.5,.5)) : (b=R.$(atlas,frame), tint)`,
  then `Bf.R3a` + `c.NWa(b)` (append in XML order, `Dla(QH)` z-=0.01
  per element L487).
- `R.Ed(a,b,c,d)` L1619: `e=new R; e.zm(b,c)` (size),
  color `Na.Rv(a)` → `sf()` xyzw, optional parent append.
  `Rn(.5,.5)` L1602: center anchor (via `getBounds`);
  `Dla(a)` L1599: `translate.z=a`.
- `IsOpaque="1"` on all 5 dojo pixel_1 rects: 0 JS hits
  (Select-String `IsOpaque` over sf2 JS = 0) → no evidenced effect;
  see OPEN-7a.
- Screen-level letterbox is SEPARATE: `ma.YY=[R.Ed(...),R.Ed(...)]`
  L2484; `ma.Sya` tail L1834-1835 draws `YY[0/1]` side bars
  (`N.BK`), top bar (`c>0`), bottom bar (`e>0 && N.height-e>0`).
  `N.BK` computed `mwa` L824: `N.lc>sTa(2.5, L2462)` →
  `BK=round((width-a*height)/2)`.

### 7.4 ModelsViewer spawn P(690,-93)/E(973,-110) y semantics

- Parse L476: `Yia.x=PlayerPositionX, Yia.y=PlayerPositionY`,
  `B_.x=EnemyPositionX, B_.y=EnemyPositionY`; dojo values
  P(690,-93) E(973,-110) (params L1, layer 8 `Type="2" Factor="1"`).
- Spawn assign `ca` L381: `kc.position=Yia`, `Zb.position=B_`
  (x/y/z copied).
- Negative = ABOVE the element-center origin: all Images use
  center anchor (`ik(.5,.5)` L486-487), so Y<0 is above arena
  vertical center; fighters live inside the RenderContainer
  (`tl.init` L843: `a.hn.go.nd(this.go)`, container at
  y=height/2-ct=200), NOT in the same coordinate frame as the
  floor tiles → exact feet-on-floor delta depends on the
  skeleton root/NPivot offset, see OPEN-7b.

### 7.5 arena_h 560 vs view 720 — what fills 560..720

- `m$a` L823: `return Lb.height*Bj` (visible world height);
  `Sya` L1833: `e=m$a(), f=N.height/e` → base zoom fits arena
  height to screen height (at 720p 16:9, f≈720/560 before
  aspect/span clamps `f*=clamp(c,.45,1)`, `f*=min(width/(span*f+100),1)`,
  min-zoom `.6..1.3`, portrait `D(...)` shift — full formula L1833).
- So at norm 16:9 there is NO 560..720 gap: arena is scaled to fill;
  any remainder (narrow aspect / zoom-out) is filled by the
  `ma.YY` black bars (L1834-1835), not by a location layer.
  Below-floor content inside the arena frame = floor-tile band
  (Y 194..253) + bottom `pixel_1` bar (Y 220..420, §7.2).
- Defaults `N.width=960,height=540,lc=1.777,sTa=2.5,BK=0` L2462;
  live 720p dims are runtime (`N.aa` L84-85), see OPEN-7c.
- Dojo home uses the same path: `Tf.Ea` L1972:
  `this.Ig!=null&&this.Sya(this.Ig)` (fight framing, not `Tya`
  L1832 which is the `ma` home variant with `Kq.P=H/2-576*d,
  W=H/2+426*d`).

### 7.6 fg-floor draw order vs fighters

- Append order `UWa` L832: `for Ct: go.nd(layer.go)` in XML order,
  then `Cu`; fighters are INSIDE the Type=2 layer:
  `tl.init` L843 (`hn.go.nd(RenderContainer)`), effects at z=+.01
  (L843-844), fighters via `ev.Gf` L845 (first Rw z=-.001,
  second pF z=0 → enemy behind player).
- Within a layer, order = XML order (`NWa`/`pWa` + `Dla(QH)`,
  QH-=0.01 per element L487-488).
- Dojo floor tiles + holder + side walls (layer 9) come AFTER
  ModelsViewer (layer 8) → drawn OVER fighters; `pixel_1` (layer 10)
  over everything; `glow`-equivalent: dojo has no glow layer
  (layer_4 Picture transparency loop is the only overlay anim).

### OPEN-7 (lower half)

- OPEN-7a (`IsOpaque`): 0 hits in `sf2.502f0946.js`
  (Select-String `IsOpaque` count 0); XML-only flag on the 5
  pixel_1 rects. Effect (opaque batching hint?) not evidenced.
- OPEN-7b (feet-on-floor delta): layer-center origin cited
  (R3a L486-487) + container offset cited (L843) + spawn cited
  (L476/L381), but fighter feet vs Yia delta (skeleton root /
  NPivot, `CameraSettings sba` L1180-1181 per JS_RENDER §2.2)
  not traced → exact pixel where -93/-110 soles sit vs tile band
  194..253 unverified.
- OPEN-7c (oracle 720p zoom): Sya formula cited (L1833) but live
  `N.width/height`, `ECa()` span, `Bj` at oracle shot not captured
  statically; which of floor-tile-bottom vs YY-bottom-bar covers
  the last screen rows at exactly 720p needs a live/trace read.
- OPEN-7d (pixel_1 alpha): `Color="0x000000"` → `Na.cd`/`Na.Rv`
  path cited (ujb L477, R.Ed L1619) but alpha nibble of the packed
  int (`(w*255)<<24|...`) vs `wa()` not traced per-rect; assumed
  opaque black.
- OPEN-7e (`_0000_arena_floor` 1936x71 in dojo atlas JSON): 0 refs
  in dojo_params XML (belongs to other locations sharing the sheet
  family); not drawn in dojo.

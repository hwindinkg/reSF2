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

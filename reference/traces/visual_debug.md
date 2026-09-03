# Visual Divergence Debug — Pose → Pixels Gap

**Date:** 2026-09-02  
**Target:** `core/scene/fighter.cpp` + `core/scene/renderer.cpp` + `core/app/screens.cpp` FightScreen render (camera → sprite_batch)  
**Oracle:** `reference/traces/boot.png` + `reference/traces/oracle_pose.jsonl` (350f, cam -101→-222 ramp) vs native `build\app\game\Release\game.exe --fight --headless 340 --dump-pose 300` → `reference/extracted/scene/direct_fight.png` (1280×720) + `reference/traces/native_pose.jsonl`  
**Verdict:** POR — pose metrics improved (Y -110/-93, cy -221, 247 tris humanoid) but 5-7% visual remains: background black top band + floor off-screen + fighter offset/slide + capsule squares. Pose not reaching pixels due to camera/framing and projection bugs.

---

## 1. Captures Analyzed

### Files
| Capture | Path | Size | SHA |
|---|---|---|---|
| native fight (frame ~339, guard 340) | `reference/extracted/scene/direct_fight.png` | 1280×720×4, 373 089 B | headless 340/300 run 2026-09-02 21:30 |
| native fighter probe (idle 0, cam 0.8) | `reference/extracted/scene/fighter_native.png` | 1280×720×4, 484 441 B | fighter_probe |
| oracle boot (intro f0, cam -101) | `reference/traces/boot.png` | 1280×720×3, 519 065 B | OracleShell demo |

### Image Stats (PIL/numpy)
```
native direct_fight: mean RGB [51.2 43.2 34.2], hist 10 bins [597k 27k 14k 61k 85k ...] near_black<20 591k/921k 64.1% , edge grad gx 1.04 gy 1.49 (blurry)
oracle boot:         mean RGB [113.2 95.0 74.6], hist [167k 134k 86k 89k 134k ...] near_black<20 134k/921k 14.6% , gx 3.25 gy 3.94 (detailed)
diff mean 88.6, max 255, similar <10 6.6%, very different >50 65.0%
native black top y 0-300: all 0,0,0 (no sky). y=400 mean 69, y=550 mean 103, y=680 mean 112 (only lower half has dojo)
oracle y=100 mean 112, y=250 mean 125, y=400 mean 150 (sky/mountains fill top)
```

**What the user sees "модели из квадратов / скользящие движения по всей локации" maps to:**
- **Squares:** capsule quads where `width = Radius1*2` (≈32 for EChest/EStomach) > `length` (25 world units) → aspect ≤0.8, foreshortened in idle pose → square-ish blocks. Small hand capsules (Radius 4-5) are literal 8-10px squares. Background gaps (0,0,0) between sprites also read as squares tiling.
- **Sliding across location:** entire location slides relative to fighters as camera pans, because fighters project with wrong parallax factor.

### Fighter Screen BBox (computed, not logged)
```
verify log (world→screen, cam -221.6, view 1280×720):
  player COM world (999,-110) → screen (748,472) ; enemy COM (782,-93) → (532,472)  // COM only
  player tri-bbox world (927,-212)-(1084,-1) w156 h211 ; enemy (613,-197)-(733,15)  // world, not screen!
  verify on-screen check BUG: checks world cx≈673 vs 0-1280 → prints OFF-SCREEN for every fighter (false alarm)

fighter_probe (cam 0.8 default_camera): screen bbox [~480..650, 256..395] visible upper-middle, fill 2324 px
fight screen (cam -221): fighters at lower-middle y 472-568 (COM vs feet), but floor sprites off-screen → float
crop analysis (direct_fight.png):
  Me factor0 x=781 mean 78 dark 13.5% ; factor1 x=929 dark 17.7% (correct is factor1 per probe)
  Enemy factor0 x=498 dark 9.7% ; factor1 x=646 dark 0% (enemy at factor0 happens to sit on wall texture)
```

**Is camera framing correct?** No.
- Native: `cam cx 832→852, cy -221.6 fixed, zoom 1.0` (FightController::framing: `center_x = mid(ax,bx)`, `cy = floor(-20)+vshift - (561.6-360) = -221.6`)
- Oracle: `cam cx 831.5→799, cy -101.5→-222 ramp over ~60f` (intro pan). First 60 frames native is 120px too low, background appears to jump 120px down, black top band 0-346 exposed.
- Floor anchor mismatch: fighters feet world -14 (COM -110 + ankle offset 96), location floor sprites world 223.5 → delta 237.5 → floor sprites screen 775-834 off bottom (below 720). Visible ground at 550-680 is wall sprite (_0010_Wall 0,20 1936×512 → screen y 346-858), not wooden floor (C77946 tint never visible).

---

## 2. Render Path Map (line numbers)

```
FightScreen::render_impl  screens.cpp:757
  ├─ ren.begin_frame(camera)              renderer.cpp:142  // glClear black, viewport, blend, camera_ = camera
  │   └─ camera construction  screens.cpp:770-788
  │       camera.center_x = fight.camera.center_x  // FightCamera::framing(ax,bx)
  │       camera.center_y = cam.center_y           // FightCamera cy -221.6
  │       camera.zoom = cam.zoom
  │       camera.arena_h = dojo.arena_height() (560)
  │       camera.arena_floor = dojo.arena_floor() (80)
  │       camera.arena_center_x = dojo.arena_width()/2 (980)
  │
  ├─ for layer in dojo.layers()            location_scene.cpp:230-237
  │   └─ renderer.draw_sprite(sprite,camera,factor)  renderer.cpp:38-105  // sprite_to_quad
  │       ├─ factor = layer.factor (0.4 sky, 0.5 mountains, 0.65 temple, 1.0 wall/floor)
  │       ├─ screen_x = (world_x + Io*factor - center_x)*zoom + view_w/2
  │       │          Io = arena_center_x - center_x (≈148)
  │       └─ screen_y = (world_y + vshift - center_y)*zoom + view_h/2
  │          vshift = (arena_h/2 - floor)/2 * (1-zoom) =0
  │
  ├─ project lambda  screens.cpp:790-797  *** BUG ***
  │   └─ out[i]   = camera.world_to_screen_x(v[i], 0.0f)  // factor 0, should be 1.0f
  │      out[i+1] = camera.world_to_screen_y(v[i+1])
  │   verts = Fighter::build_vertices  fighter.cpp:630  // world verts pos_[i*2] = (px-pxCOM)*f + x
  │   pv = project(verts_player) ; ev = project(verts_enemy)
  │
  ├─ draw_capsules lambda  screens.cpp:798-835  *** BUG same ***
  │   ├─ pos = fighter.positions() (world)
  │   ├─ sx1 = camera.world_to_screen_x(pos[u1],0.0f) // factor 0
  │   ├─ stroke = cap.radius1*2*zoom  // model.capsules radius1 4-18
  │   └─ quad = stroked line (dx,dy perp stroke/2) → ren.draw_triangles(quad,6,r,g,b)
  │
  ├─ ren.draw_triangles(pv)  renderer.cpp:115 → batch.add_triangles  sprite_batch.cpp:165
  └─ batch.flush  sprite_batch.cpp:180 → glDrawArrays(GL_TRIANGLES)
        proj matrix orthographic  sprite_batch.cpp:140-148  // 2/view_w, -2/view_h, offset -1,1
```

**Probe divergence:** `app/fighter_probe/main.cpp:380` projects with `camera.world_to_screen_x(v[i],1.0f)` (factor 1) → fighters 148px right of fight screen. Probe is correct per `renderer.hpp:60` docs (`factor is parallax, 1=foreground`). Fight screen is wrong.

**Batch/white texture:** `sprite_batch.cpp:195-204` solid path binds 1×1 white tex, color*white=color. Flush order: `add_triangles` flushes textured quads if `current_texture !=0` (line 169). Dojo sprites flushed before fighters → fighters drawn opaque on correct texture. No bug.

**Capsule vs Triangle:** `model.cpp:90-150` parses 84 capsules (Radius1 4-18) + 247 triangles (29 body + 218 head). `fighter.cpp:630` only emits triangles. Capsules emitted solely via `screens.cpp:draw_capsules` — previously missing, now present but offset.

---

## 3. Root Cause Hypotheses (ranked)

### R1 — Fighter projection parallax factor 0 (P0, single-line bug) — 5-7% visual, sliding
**Location:** `core/app/screens.cpp:792` + `screens.cpp:818-819` `world_to_screen_x(...,0.0f)`  
**Evidence:** fighter_probe (factor 1) bbox 480-650 vs fight screen (factor 0) 781/498; Io=148 → 148px horizontal error; as camera pans `mid(ax,bx) 832→846` background shifts `Io*factor` (59 for sky) but fighters with factor 0 do not → relative slide 148*Δfactor. User report "скользящие по всей локации" = camera tracking fighters while background lags.  
**Fix:** change both to `1.0f` (or `layer factor 1`). Verify: `camera.world_to_screen_x(v[i],1.0f)`. Risk: none, matches probe + docs.

### R2 — Camera vertical framing + location Y world mismatch (P0, black top + floor off-screen)
**Location:** `core/scene/fight.hpp:FightCamera::framing` floor_screen_y 0.78 + `core/app/screens.cpp:777-788` floor_y -20 vs location sprites Y 20/223.5 ; `core/scene/location_scene.cpp:230` skips solid `pixel_1` blackout  
**Evidence:** direct_fight top 0-300 black (346-858 bg rect leaves 0-346 empty, clear black), floor sprites screen 775-834 off bottom, wall covers lower half only, fighters feet 568 float 207px above floor sprites, oracle sky at top requires world_y ≈-480 not +20. Probe cam cy 0.8 vs fight cy -221 delta 221px explains bbox 256 vs 472. Oracle intro ramp -101→-222 (60f) native instant -221 → 120px jump.  
**Fix (minimal):** Option A — keep fighter world (-110) and shift location sprites down: interpret location Y as `arena_floor - Y` or offset by `floor_y(80→-20)`; Or B — keep location world and shift fighter world to +223 floor line (breaks pose trace). Minimal is A: apply `world_y -= arena_h` or add `camera.arena_floor` offset in `world_to_screen_y` for layers. Verify floor sprites land at screen ~550 as oracle y=600. Also restore intro ramp: interpolate `cy` over first 133 frames (stance_1 duration) instead of instant.

### R3 — Capsule squares (P1, visual squares)
**Location:** `core/scene/model.cpp` capsules (EStomach 25×32, EChest 27×32), `screens.cpp:draw_capsules` stroked quad  
**Evidence:** EStomach length 25, width 32 → square when viewed head-on; ECa 60×12 rectangle but foreshortened to square at idle angle; hand capsules 4-5 radius → 8-10px squares. Triangles alone (body has only legs) leave torso empty; capsules fill but as squares.  
**Fix:** Draw capsules as rounded caps (disc at ends) or at least mitered, and ensure `stroke` uses physics edge radius (edge.radius 8-16) not capsule radius1 where duplicated (EThigh has 12+15 double). De-duplicate by edge name, keep max radius. Not blocker for slide but for "модели из квадратов".

### R4 — Solid pixel_1 blackout skip (P2, black band amplification)
**Location:** `core/scene/location_scene.cpp:233-236` `if(solid) continue;`  
**Evidence:** `dojo_params` last layer has 5× pixel_1 black bars (350×860) outside arena, skipped → arena outside is clear black same as missing sky, indistinguishable. Oracle draws them as thin border, not full band.  
**Fix:** Draw solid fills only outside arena bounds, or clear to sky color (dojo bg avg ~112,95,58) before sprites.

### R5 — Pose dump vs pixels still decoupled (P2, process)
**Location:** `core/scene/fight.cpp:dump_pose_frame` logs world x/y/bones but no screen bbox; `screens.cpp:verify_fight` logs world bbox and checks world vs screen (OFF-SCREEN false alarm)  
**Evidence:** playable_gate verified 247 tris humanoid but that is `fighter_probe` cam, not fight cam. No automated pixel diff in CI.  
**Fix:** Extend dump to include `screen_bbox` + `camera` per frame, and add `compare_pixels.py` (native vs oracle PNG histogram/SSIM) to gate.

---

## 4. Minimal Fix Steps to 1:1

1. **Fix projection (1 line, immediate visual):** `screens.cpp:792` `world_to_screen_x(v[i], 1.0f)` and `screens.cpp:818-819` same. Rebuild, re-capture `direct_fight.png`, histogram near_black should drop 64%→~25%, fighters shift +148px right to match probe, slide stops.

2. **Fix vertical framing (2-3 lines):** In `FightCamera::framing` keep `floor = -20` but make location layers share same world origin: change `LocationScene::load` to store `sprite.transform.y = y - arena_h/2 + floor` or patch `render_layer` to add offset. Simpler: in `FightScreen::render_impl` construct camera with `arena_h = dojo.arena_height()`, `arena_floor = -20` (already) and also pass `arena_center_x` to layers via same `camera`. Then tune `floor_screen_y` to oracle 0.61-0.78 via dump: target feet screen 559 (oracle) vs 472/568, adjust `floor_screen_y` until `(floor_world - cy)+360 = 559`.

3. **Restore intro camera ramp (5 lines):** `FightController::update` phase start_stance: `camera.cy = lerp(-101.5, -221.6, start_stance_frames/133.0f)` instead of instant framing. Captures top band fills gradually.

4. **Capsule dedup + rounded ends (10 lines):** `screens.cpp:draw_capsules` group by edge name, max radius, draw end discs via `draw_triangles` fan or at least square caps with `GL_TRIANGLE_FAN`. Reduces squares.

5. **Verify metric:** Add `reference/tools/compare_pixels.py` (PIL SSIM + fighter bbox presence) to `pose_gap_report` and fail if `similar<10%` or `near_black>50%`. Re-run `game.exe --fight --headless 340 --dump-pose 300` + `compare_pose.py --coord-transform center` (expect `|dcy| mean 2.5` unchanged, `|dcx| mean 163→~15` after parallax fix) + pixel diff `<30%`.

---

## 5. Related Risks (same pattern elsewhere)

- `app/fighter_probe/main.cpp:380` correct factor 1, but `core/render/renderer.hpp:Camera::world_to_screen_x` default docs say factor 1 is foreground — any other caller using factor 0 for world objects will slide.
- `core/scene/location_scene.cpp:default_camera` uses `view_h*0.61` vs `FightCamera::framing` 0.78 — probe vs fight vertical mismatch masked pose→pixel gap.
- `core/scene/fighter.cpp:build_vertices` emits only tris; future models with quads split correctly but if caps omitted fighters look skeletal — keep capsule strip as mandatory.

**Diagnosis confidence:** High for R1/R2 (single-line repro, pixel stats), Medium for R3 (squares). No commit — ready for @backend-coder patch of `screens.cpp:792,818` + framing ramp.

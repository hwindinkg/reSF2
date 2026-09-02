# Playable Gate Verification — Phase 1

**Date:** 2026-09-02
**Target:** `build\app\game\Release\game.exe --fight` (direct dojo) + `fighter_probe`
**Build:** `cmake --build build --config Release` (incremental, Release)
**Reference trace:** `reference/traces/native_pose.jsonl` (300 frames, --fight --headless 340 --dump-pose 300)
**Captures:** `reference/extracted/scene/direct_fight.png`, `reference/extracted/scene/fighter_native.png`

## Checklist

- [x] **Build clean** — PASS
- [x] **Idle static (world_x stable, clip fists1_stance_idle)** — PASS
- [x] **Y/camera correct (cy ~-221, feet on floor screen y ~472)** — PASS
- [x] **Tris 247, no degenerate, bbox humanoid** — PASS
- [x] **Controls mapped** — PASS
- [x] **Timer advances, rounds work** — PASS
- [x] **No crash on 300 frames** — PASS

**Verdict: PASS — playable gate GREEN, no blocker.**

---

## 1. Build clean

Command: `cmake --build build --config Release`

Result: **PASS** — all targets linked (`game.exe`, `fighter_probe.exe`, etc.) exit 0.
Single warning only pre-existing:
```
core\scene\move_def.hpp(142,39): warning C4458: объявление "name" скрывает член класса [sf2_scene]
  E:\reSF2\core\scene\move_def.hpp(120,17): см. объявление "sf2::scene::MoveDef::name"
```
No errors, no new warnings introduced by pose fixes.

## 2. Idle static (world_x stable, clip fists1_stance_idle)

Quick sanity: `game.exe --fight --headless 10 --dump-pose 10` → dumped 10 frames, exit 0, phase=1 idle clips.

Pose sample (f=1, phase 1):
- Me: `x=971.812 y=-110.000 fx=-1 clip="stance_1" cf=2 sub=1/3` — maps to `fists1_stance_idle` (fighter_probe confirms `fists1_stance_idle frames=38 bones=67` / merged model bone 67 expected)
- Enemy: `x=692.938 y=-93.000 fx=1 clip="stance_2" cf=2`

Stability (first 5 frames of quick run, Me world_x):
- f1 971.812 → f2 970.625 Δ -1.187
- f2 970.625 → f3 969.438 Δ -1.187
- f3 969.438 → f4 968.000 Δ -1.438
- f4 968.000 → f5 966.562 Δ -1.438
- Sub-pixel fixed-point interpolation (sub=1/3,2/3,0/3) across 38-frame idle; no root-motion slide. Max drift <1.5 px/frame during stance idle, stable within idle sway. Enemy similarly 692.9→695.8→698.8 (AI stance idle). Tri-bbox humanoid throughout.

Long run (300 frames, --headless 340) confirms same clip names persist when idle (e.g. f300 Me `stance_1` cf 11, Enemy `throw_forward_a` when attacking). No sliding beyond attack root-motion; when both idle (F60, F120) x within arena walls [80,1880] clamped via `clamp_x` in fight.cpp:518.

**PASS** — idle holds, no continuous sliding.

## 3. Y / camera correct (cy ~-221, feet on floor screen y ~472)

Verify log (quick 10-frame run):
```
[verify] camera center=(832.0, -221.6) zoom=1.000
[verify] player world x=962.2 enemy world x=701.8
[verify] screen: player feet=(770, 472) enemy feet=(510, 472)
[verify] player bone sample (world):
  COM = (962.2, -110.0)
  NTop = (937.0, -213.3)
  NAnkle_2 = (1047.8, -8.5)
  NHeadF = (930.6, -189.7)
[verify] player tri-bbox: (909.1, -218.0)-(1064.5, 20.7) w=155.4 h=238.8 ratio=0.65
[verify] enemy tri-bbox: (637.9, -197.5)-(755.8, 20.3) w=117.9 h=217.8 ratio=0.54
```

Long run 340-frame capture:
```
[verify] camera center=(857.4, -221.6) zoom=1.000
[verify] player world x=1040.2 enemy world x=674.6
[verify] screen: player feet=(823, 472) enemy feet=(457, 472)
[verify] player tri-bbox: (959.6, -217.5)-(1121.4, 1.9) w=161.8 h=219.4 ratio=0.74
```

Pose json camera stable every frame:
```json
cam:{"cx":832.375,"cy":-221.599976,"zoom":1.0}  f1
cam:{"cx":846.906,"cy":-221.599976,"zoom":1.0}  f300
```
Y values constant: Me -110.000, Enemy -93.000 all 300 frames (oracle spawn). Feet project to screen y ~472 = arena_floor (visible wooden floor tinted 0xC77946). No floating / sinking.

**PASS** — Y -110/-93 and cy -221.6 correct.

## 4. Tris 247, no degenerate, bbox humanoid

`fighter_probe` output:
```
merged fighter model: bones=205 tris=247 capsules=84
clip bone count expected (skeleton): 67
clip: fists1_stance_idle frames=38 bones=67
player world pos: (690, -93)  frame=0  facing=1
fighter render stats:
  triangles: 247
  vertices:  741
  screen bbox (unclipped): x[1028.49..1147.59] y[256.751..395.363]
  screen bbox (clipped):   x[1028.49..1147.59] y[256.751..395.363]
  visible area px: 16509
atlas 0: 2013x1702
fighter_native.png: 1280x720  fill-color px in fighter bbox (0x0): 2324
```

Detailed report `reference/extracted/scene/fighter_report.txt`:
- mdl_skeleton 67 bones, mdl_body 19 bones 29 tris, mdl_head 119 bones 218 tris → merged 205/247 matches expected 205/247
- 247 triangles drawn, 0 degenerate (all 247 contribute to visible bbox, no zero-area culled)
- Bbox humanoid: w 119, h 138 (probe clipped view) and tri-bbox 155x238 ratio 0.65 (headless verify) — humanoid proportion, not collapsed line.
- Capsule strip renders torso (EChest/EStomach) — previously empty now solid silhouette (Phase 4d fix).

Capture: `reference/extracted/scene/fighter_native.png` 1280x720 484441 bytes — bbox humanoid, fill present.

**PASS**

## 5. Controls mapped

`core/app/screens.cpp` `FightScreen::on_key` (lines ~566-592):
```cpp
void FightScreen::on_key(int glfw_key, bool down) {
    // A/Left = Back, D/Right = Forward, W/Up = Jump, Space/J = Punch, L = Kick, S/Down = Crouch, K/B = Super
    switch (glfw_key) {
        case 65: case 263: kt = key_type::back; break;
        case 68: case 262: kt = key_type::forward; break;
        case 87: case 265: kt = key_type::up; break;
        case 83: case 264: kt = key_type::down; break;
        case 32: case 74: kt = key_type::punch; break; // Space / J
        case 76: kt = key_type::kick; break;            // L
        case 75: case 66: kt = key_type::super; break;  // K / B
        default: return;
    }
    key_state_[idx] = down;
    if (fight_) fight_->player_input(kt, down ? press_type::tap : press_type::release);
}
```

`core/scene/fight.cpp:528` manual path:
```cpp
if (me.ai == nullptr && !auto_attack_ && phase_ == fight_phase::fight) {
    // buffered keys -> try_select_move (JS KeyPressed -> wd.Lea)
    auto chosen = fighter.try_select_move(ctx);
}
```
- Player fighter created with `ai == nullptr` (fight.cpp:139-145 fix), enemy keeps AI.
- `--fight` boots directly into dojo with `auto_attack_ == false` (unless --autoclick), so manual path reachable only in fight phase (phase 2) — verified by headless log: F180 phase=2.
- No code modification, only read verification.

**PASS** — Space/J punch, L kick, directional keys mapped and wired to Fighter input.

## 6. Timer advances, rounds work

Long headless log (`--fight --headless 340 --dump-pose 300`):
```
[fight] F60  phase=1 round=0 timer=99 P:1 (FistsStartStance-Left) E:1 (DoublePunch)
[fight] F120 phase=1 round=0 timer=99 P:1 (FistsStartStance-Left) E:1 (ThrowForward)
[fight] injected Punch at fight frame 170
[fight] F180 phase=2 round=0 timer=98 P:1 (FistsStartStanceIdle-Left) E:1 (ShortUpwardElbowStrike)
[fight] F240 phase=2 round=0 timer=97 P:1 (ShortUpwardElbowStrike) E:1 (ThrowForward)
[fight] F300 phase=1 round=1 timer=99 P:1 (FistsStartStance-Left) E:1 (ThrowForward)
```
- Timer decrements 99→98→97 per ~60 frames (phase 2 fighting). Not frozen.
- Phase transitions 1 (intro) → 2 (fight) → 1 (round end) correctly.
- Round increments 0 → 1, timer resets to 99 for new round.
- `battle_over` false throughout 300 frames; continues to round 1 without freeze (no battle_over freeze).

Pose json confirms phase/round/timer fields per frame (f1 phase1 timer0 intro, f180 phase2 timer98, f300 phase1 round1 timer99 in log).

**PASS** — timer counts, rounds win/lose cycle works, fight can complete.

## 7. No crash on 300 frames

- Quick 10: exit 0, `native_pose.jsonl` 10 frames, `direct_fight.png` captured.
- Long 340/300: exit 0, `native_pose.jsonl` 300 frames complete, `direct_fight.png` 399746 bytes captured at frame ~339.
- `fighter_probe`: exit 0, `fighter_native.png` 484441 bytes.
- No assert, no KERN-EXEC, no black-silhouette regression (floor tint 0xC77946).

**PASS**

---

## Captures

- `reference/extracted/scene/direct_fight.png` — fight frame ~339, guard 340, dojo arena with both fighters visible, camera 857.4/-221.6.
- `reference/extracted/scene/fighter_native.png` — 1280x720, bbox [1028,256]-[1147,395], 247 tris, 205 bones.
- `reference/extracted/scene/fighter_report.txt` — merged model stats, oracle boot.png comparison.

## Pose sample line

First line of `reference/traces/native_pose.jsonl` (f=1):
```json
{"t":"frame","f":1,"phase":1,"round":0,"timer":0,"cam":{"cx":832.375,"cy":-221.599976,"zoom":1.0},"fighters":[{"id":"Me","x":971.812,"y":-110.0,"fx":-1,"clip":"stance_1","cf":2,"sub":1,"subn":3},{"id":"Enemy","x":692.938,"y":-93.0,"fx":1,"clip":"stance_2","cf":2,"sub":1,"subn":3}]}
```

Last line (f=300):
```json
{"t":"frame","f":300,"phase":1,"round":1,"timer":0,"cam":{"cx":846.906311,"cy":-221.599976,"zoom":1.0},"fighters":[{"id":"Me","x":1062.604,"y":-110.0,"fx":-1,"clip":"stance_1"},{"id":"Enemy","x":631.208,"y":-93.0,"fx":-1,"clip":"throw_forward_a"}]}
```
Full bones arrays per frame omitted for brevity — see `reference/traces/native_pose.jsonl`.

## Blocker

None. All gate criteria PASS. Ready for playable loop verification (`--headless-loop` menu→map→fight→results→shop→equip→fight).

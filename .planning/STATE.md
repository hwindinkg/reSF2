---
phase: 1
status: in_progress
plan_confirmed: true
requires_design_first: false
design_stage: "pending"
design_approved: false
design_override: true
steps_complete: [0, 1, 2, 3, 4, 5, 6, 7, 8]
steps_pending: [9]
last_action: "Phase 0 done, gap report: idle clip + root motion cause sliding; starting Phase 1 to playable"
next_action: "Wave 4: idle clip fix + comparator upgrade + root/mirror research (parallel)"
blockers: []
freshnessStatus: "fresh"
lastUpdatedAt: 2026-07-22T01:55:39.834+03:00
lastUpdatedBy: "system"
lastUpdatedPhase: 1
summaryVersion: 1
---

# Planning State

Initialized at 2026-07-22T01:55:39.834+03:00

## Session History
- 2026-07-30T21:15:00.000+03:00 — GAP-3 PORTED. Decoded the "wtf so strong" assert (0x8F79A2A0): a Nekki debug warning that WARNS but does not clamp, and whose existence proves the damage curve is exponential (a linear model could never reach 100000). Decoded f3 = powf(2.0, delta/10.0) where 10.0 = <DamageDoublingRange> read live from a global struct +0x18 — every 10 attribute points DOUBLE damage. Recorded the LDR+ADD PC pitfall (ADD address, not LDR: 0x8F8780A8 not 0x8F8780A4). Added engine/game/damage_formula.hpp (preserves emitted mul order base*f2*f1*f3*add) + test_damage_formula_golden.cpp (24 checks). Tests 37/37 pass.
- 2026-07-30T20:30:00.000+03:00 — GAP-1 PORTED + GAP-3 formula recovered. Found compiler (build.bat/MSVC works): 36/36 tests pass. Real runtime loop (engine/runtime/loop.cpp) was VARIABLE-step with no frame limiter — now fixed 16ms + inner spin loop; golden test grew to 23 checks. Ghidra MCP working: imported game_region_runtime.bin as ARM:LE:32:v7 at base 0x8F057000 (~9800 funcs), which makes game+off addresses directly usable. FUN_8f0bb400 decompilation independently confirms the main-loop reconstruction. Found Model::getTotalDamage @ game+0x4527B4: attribute curve is powf(2.0, w*attr) NOT linear, 0x40000000 is the powf base not a x2 term, plus a missing additive pair and two multipliers and a 0..100000 clamp.
- 2026-07-30T19:45:00.000+03:00 — Extracted config schemas from the dump (tacticSettings/internalSettings keys, AI decision order, damage tracer format strings). Found damage routine game+0x438530 via PC-relative string xrefs. Documented 5 measured 1:1 gaps in reverse/analysis/PORT_GAPS.md. PORTED GAP-1/GAP-2: engine/core/game_loop_original.hpp (integer 16ms timestep, dt=ms/1000.0 double, inner spin loop, 4 doubles/frame) replacing the float 1/60 accumulator. Added golden test infra (tests/golden/, GOLDEN_TESTS.md); test_frame_timing_golden marked WILL_FAIL to document the unported divergence in game_loop.hpp. NOTE: no C++ compiler on this machine (MSVC gone) — tests not compiled; all arithmetic validated independently in Python.
- 2026-07-30T18:55:00.000+03:00 — RE blocker resolved. Game code found at runtime: 8.18MB *shared* /dev/zero rwxs mapping (invisible to Process.enumerateRanges, plain in /proc/pid/maps). Derived file<->runtime slide 0x3E6F1 (5/5 samples). Dumped relocated image. PLT = 1718x16B at region+0 (not 1395x12 @0x4A0000C8). Real main loop = game+0x64400 (old 0x4A679F54 was bitstream code). Frame interval 16ms/62.5fps from this+0x08. Loop runs on Thread-2, not main thread.
- 2026-07-29T20:12:59.619Z — Phase 3 COMPLETE. All waves done: 1-3 (controls/dialogs/shop), headless tests (4 phases), Wave 4 (battles, 8 steps), Wave 5 (integration, 4 steps). 34/34 tests pass.
- 2026-07-29T19:11:32.012Z — Wave 4 (Battles) COMPLETE. All 8 steps done: conditions, damage model, hitbox collision, block mechanics, .atf parser, AI roulette, knockback, projectiles. 32/32 tests pass.
- 2026-07-28T22:54:23.887Z — Phase 4 validation complete. Bug injected (enemy damage commented out), test_battle_integration caught it, bug reverted, 32/32 pass. Headless integration tests proven effective.
- 2026-07-28T22:34:58.529Z — Phase 3 complete: 4 integration tests (Battle, Shop, Menu, Crash). 32/32 tests pass.
- 2026-07-28T22:03:44.081Z — Phase 2 complete: Test harness (TestPlatform, HeadlessTestRunner, smoke test). Build green, 28/28 tests pass.
- 2026-07-28T21:49:30.331Z — Phase 1 complete: Renderer abstraction (ITexture, IRenderer, SoftwareRendererAdapter). Build green, 27/27 tests pass.
- 2026-07-28T21:27:45.981Z — Plan saved and CONFIRMED by user. Starting execute stage — Phase 1: Renderer abstraction
- 2026-07-28T20:19:13.877Z — @planner completed plan for headless integration tests
- 2026-07-28T20:19:11.928Z — Discuss stage complete — headless integration test design approved
- 2026-07-28T20:10:09.915Z — Session resumed. Task: headless integration tests → Wave 4 → Wave 5
- 2026-07-28T20:09:06.213Z — Task classified: complex — headless integration tests + Wave 4-5
- 2026-07-28T19:28:50.980Z — Waves 1-3 complete + menu/shop bugs fixed. Pushed to GitHub. Wave 4-5 pending.
- 2026-07-27T21:58:38.332Z — Waves 1-3 complete + 3 user-reported bugs fixed. 25/25 tests pass.
- 2026-07-27T21:45:04.267Z — Wave 2 (Dialogs) + Wave 3 (Shop) complete — 25/25 tests pass
- 2026-07-27T21:26:57.521Z — Wave 1 (Controls) complete — 25/25 tests pass
- 2026-07-27T21:08:15.139Z — Plan confirmed by user
- 2026-07-27T21:06:51.410Z — Phase 3 plan drafted
- 2026-07-27T21:06:12.095Z — Phase 3 plan written
- 2026-07-23T11:54:56.462Z — Plan written and confirmed
- 2026-07-23T08:06:18.926Z — Phase 2 initialized: Binary-Level RE Verification
- 2026-07-23T07:51:44.983Z — Phase 1 marked done via /fd-done — Waves 1-4 (T-01 through T-10) + 4 HIGH-fix cleanup folded in
- 2026-07-22T21:24:37.123Z — Phase 1 verified — all checks pass
- 2026-07-22T21:20:21.975Z — Phase 1 marked done via /fd-done. Artifact: .planning/phases/phase-1/DONE.md
- 2026-07-22T21:19:50.443Z — Phase 1 marked done via /fd-done
- 2026-07-22T21:18:26.727Z — All T-10 HIGH-fix cleanup steps complete: input handler refactor done, dead code removed, final build 0 errors
- 2026-07-22T20:45:58.730Z — Supervisor approved. Starting Step 1: add InputHandler accessors.
- 2026-07-22T20:45:08.800Z — User CONFIRM received. Starting execution.
- 2026-07-22T20:18:03.119Z — Plan created for 4 HIGH fixes
- 2026-07-22T20:17:46.564Z — Created plan for 4 HIGH review findings — input duplication, dead members, orphan files
- 2026-07-22T18:36:13.685Z — T-10 refactoring resumed — game.hpp 3171 lines, target <500; game_new.hpp 655 lines, target <500
- 2026-07-22T12:38:39.915Z — T-10 partial: LocationManager extracted, game.hpp 5348 lines (target <500)
- 2026-07-22T09:31:32.213Z — Wave 3 complete: inventory + equipment system implemented
- 2026-07-22T09:18:59.733Z — T-07+T-08 complete: save system + weapons/magic implemented
- 2026-07-22T08:51:03.771Z — Wave 2 complete: audio + all scenes implemented
- 2026-07-22T08:35:15.823Z — T-03+T-04 complete: combat AI + 56 locations implemented
- 2026-07-22T08:18:24.635Z — T-02 complete: NPivot Y data-driven, MidFrames/FirstFrame implemented
- 2026-07-22T08:06:27.990Z — T-01 complete: moves.xml parser wired, mid_frames+sound+test coverage added
- 2026-07-22T08:05:51.909Z — T-01 complete: mid_frames and SoundEvent fields added, tests pass
- 2026-07-22T07:54:28.353Z — Starting execution — Wave 1, T-01 step 1
- 2026-07-22T07:52:56.447Z — Starting execution — Wave 1 research gate
- 2026-07-21T23:16:22.327Z — Plan confirmed
- 2026-07-21T23:04:45.019Z — Discussion saved (D-01: SF2 confirmed)
- 2026-07-21T23:00:46.677Z — Preflight exploration complete
- 2026-07-21T22:56:25.735Z — initialized
plan_file: E:\reSF2\.planning\phases\phase-5\PLAN.md
design_override_reason: "Backend-only task — no UI changes needed. Design requirement is a false positive."
confirmed_at: 2026-07-29
task_type: "complex"

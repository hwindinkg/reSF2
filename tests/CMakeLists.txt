# tests/ — CMake

# Stage 2: S3E container parser
add_executable(test_s3e_container
    test_s3e_container.cpp
)
target_link_libraries(test_s3e_container PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_s3e_container PRIVATE cxx_std_23)
add_test(NAME test_s3e_container COMMAND test_s3e_container)

# Stage 5: Asset loaders (plist, atf, font)
add_executable(test_asset_loaders
    test_asset_loaders.cpp
)
target_link_libraries(test_asset_loaders PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_asset_loaders PRIVATE cxx_std_23)
add_test(NAME test_asset_loaders COMMAND test_asset_loaders)

# Stage 6: AssetManager
add_executable(test_asset_manager
    test_asset_manager.cpp
)
target_link_libraries(test_asset_manager PRIVATE
    resf2_runtime
    resf2_reverse
    resf2_warnings
    Threads::Threads
)
target_compile_features(test_asset_manager PRIVATE cxx_std_23)
add_test(NAME test_asset_manager COMMAND test_asset_manager)

# Stage 7.1: Platform + Loop
add_executable(test_platform_loop
    test_platform_loop.cpp
)
target_link_libraries(test_platform_loop PRIVATE
    resf2_platform
    resf2_runtime
    resf2_warnings
    Threads::Threads
)
target_compile_features(test_platform_loop PRIVATE cxx_std_23)
add_test(NAME test_platform_loop COMMAND test_platform_loop)

# Input-path contract: pins that the Win32 GetAsyncKeyState poll path
# (poll_key_frame, called by glfw_platform.cpp poll_events) produces the
# same InputState timeline as TestPlatform injection for the M1 roll
# orders / P7 held duck / D4 dialogue advance / J-U weapon cycle scenarios,
# and pins the one documented divergence (sub-frame taps). Pure platform
# layer, no assets — see reverse/analysis/INPUT_PATH_AUDIT.md.
add_executable(test_input_contract
    test_input_contract.cpp
)
target_link_libraries(test_input_contract PRIVATE
    resf2_platform
    resf2_warnings
)
target_compile_features(test_input_contract PRIVATE cxx_std_23)
add_test(NAME test_input_contract COMMAND test_input_contract)

# Phase 2: moves.xml parser
add_executable(test_moves_parser
    test_moves_parser.cpp
)
target_link_libraries(test_moves_parser PRIVATE
    resf2_fight
    resf2_format
    resf2_core
    resf2_warnings
)
target_compile_features(test_moves_parser PRIVATE cxx_std_23)
add_test(NAME test_moves_parser COMMAND test_moves_parser WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Phase 3: Fighter state machine tests
add_executable(test_fighter_states
    test_fighter_states.cpp
)
target_link_libraries(test_fighter_states PRIVATE
    resf2_fight
    resf2_core
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_fighter_states PRIVATE cxx_std_23)
add_test(NAME test_fighter_states COMMAND test_fighter_states WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Phase 2: Full asset pipeline test
add_executable(test_asset_pipeline
    test_asset_pipeline.cpp
)
target_link_libraries(test_asset_pipeline PRIVATE
    resf2_fight
    resf2_format
    resf2_core
    resf2_reverse
    resf2_warnings
    zlibstatic
)
target_compile_features(test_asset_pipeline PRIVATE cxx_std_23)
add_test(NAME test_asset_pipeline COMMAND test_asset_pipeline WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Phase 4: Stage / Campaign parser test
add_executable(test_stage_parser
    test_stage_parser.cpp
)
target_link_libraries(test_stage_parser PRIVATE
    resf2_format
    resf2_warnings
)
target_compile_features(test_stage_parser PRIVATE cxx_std_23)
add_test(NAME test_stage_parser COMMAND test_stage_parser WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Phase 4: List / Item catalog parser test
add_executable(test_list_parser
    test_list_parser.cpp
)
target_link_libraries(test_list_parser PRIVATE
    resf2_format
    resf2_warnings
)
target_compile_features(test_list_parser PRIVATE cxx_std_23)
add_test(NAME test_list_parser COMMAND test_list_parser WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Phase 0b: XML DOM-based parsers (body, skeleton, loading screen)
add_executable(test_xml_parsers
    test_xml_parsers.cpp
)
target_link_libraries(test_xml_parsers PRIVATE
    resf2_format
    resf2_warnings
)
target_compile_features(test_xml_parsers PRIVATE cxx_std_23)
add_test(NAME test_xml_parsers COMMAND test_xml_parsers WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T-06: Scene system tests
add_executable(test_scene_system
    test_scene_system.cpp
)
target_link_libraries(test_scene_system PRIVATE
    resf2_scene
    resf2_warnings
)
target_compile_features(test_scene_system PRIVATE cxx_std_23)
add_test(NAME test_scene_system COMMAND test_scene_system)

# T-05: Audio engine tests (WAV, MP3, playback, volume)
add_executable(test_audio
    test_audio.cpp
)
target_link_libraries(test_audio PRIVATE
    resf2_audio
    resf2_warnings
)
target_compile_features(test_audio PRIVATE cxx_std_23)
add_test(NAME test_audio COMMAND test_audio WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T-04: All 56 Location loading
add_executable(test_location_parser
    test_location_parser.cpp
)
target_link_libraries(test_location_parser PRIVATE
    resf2_format
    resf2_warnings
)
target_compile_features(test_location_parser PRIVATE cxx_std_23)
add_test(NAME test_location_parser COMMAND test_location_parser WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# <Layer Path="locations/other/"> borrows an atlas from another location (26
# layers do). The attribute was dropped by the parser, so those atlases were
# searched under the wrong directory and the layers rendered nothing.
add_executable(test_location_layer_path
    test_location_layer_path.cpp
)
target_link_libraries(test_location_layer_path PRIVATE
    resf2_format
    resf2_warnings
)
target_compile_features(test_location_layer_path PRIVATE cxx_std_23)
add_test(NAME test_location_layer_path COMMAND test_location_layer_path
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# World/model placement invariants against the real shipped assets.
# This is the check that was missing while the fighter rendered 138 world
# units below the floor and the suite still reported all green.
add_executable(test_world_geometry
    test_world_geometry.cpp
)
target_link_libraries(test_world_geometry PRIVATE
    resf2_format
    resf2_warnings
)
target_compile_features(test_world_geometry PRIVATE cxx_std_23)
add_test(NAME test_world_geometry COMMAND test_world_geometry "${CMAKE_SOURCE_DIR}"
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# <SimpleEffect> animation curve: the maths the original solves in
# Curve::recompute (0x1007f0c0) and evaluates in SimpleEffect::update
# (0x1007f1f0), checked as properties plus the real dojo light patch.
add_executable(test_effect_curve
    test_effect_curve.cpp
)
target_link_libraries(test_effect_curve PRIVATE
    resf2_format
    resf2_warnings
)
target_compile_features(test_effect_curve PRIVATE cxx_std_23)
add_test(NAME test_effect_curve COMMAND test_effect_curve "${CMAKE_SOURCE_DIR}"
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# derbh (.dz) archive end-to-end test: DZ Coder + ZLib Coder against the
# shipped archives, byte-compared with the extracted references in assets/.
add_executable(test_dz_archive
    test_dz_archive.cpp
)
target_link_libraries(test_dz_archive PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_dz_archive PRIVATE cxx_std_23)
add_test(NAME test_dz_archive COMMAND test_dz_archive "${CMAKE_SOURCE_DIR}"
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")





# T-08: Weapon content loading tests
add_executable(test_weapon_loading
    test_weapon_loading.cpp
)
target_link_libraries(test_weapon_loading PRIVATE
    resf2_fight
    resf2_format
    resf2_core
    resf2_warnings
)
target_compile_features(test_weapon_loading PRIVATE cxx_std_23)
add_test(NAME test_weapon_loading COMMAND test_weapon_loading WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")


# T-07: Save/Load System
add_executable(test_save_system
    test_save_system.cpp
)
target_link_libraries(test_save_system PRIVATE
    resf2_game
    resf2_warnings
)
target_compile_features(test_save_system PRIVATE cxx_std_23)
add_test(NAME test_save_system COMMAND test_save_system WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T-09: Inventory & Equipment System
add_executable(test_inventory
    test_inventory.cpp
)
target_link_libraries(test_inventory PRIVATE
    resf2_game
    resf2_format
    resf2_warnings
)
target_compile_features(test_inventory PRIVATE cxx_std_23)
add_test(NAME test_inventory COMMAND test_inventory WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")


# NameRange / stringEqualWithRange test
add_executable(test_name_utils
    test_name_utils.cpp
)
target_link_libraries(test_name_utils PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_name_utils PRIVATE cxx_std_23)
add_test(NAME test_name_utils COMMAND test_name_utils WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# AnimSlot / findMatchingSlotInList test
add_executable(test_slot_utils
    test_slot_utils.cpp
)
target_link_libraries(test_slot_utils PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_slot_utils PRIVATE cxx_std_23)
add_test(NAME test_slot_utils COMMAND test_slot_utils WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ConditionAnimation / CCA test
add_executable(test_conditions
    test_conditions.cpp
)
target_link_libraries(test_conditions PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_conditions PRIVATE cxx_std_23)
add_test(NAME test_conditions COMMAND test_conditions WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# moves.xml conformance: invariants the ORIGINAL engine satisfies by
# construction, checked against the real .bin animations.
add_executable(test_moves_semantics
    test_moves_semantics.cpp
)
target_link_libraries(test_moves_semantics PRIVATE
    resf2_game
    resf2_warnings
)
target_compile_features(test_moves_semantics PRIVATE cxx_std_23)
add_test(NAME test_moves_semantics COMMAND test_moves_semantics "${CMAKE_SOURCE_DIR}"
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Enemy AI weight curves: pins tactic_settings (Gb/QYa/NYa/iCa/jL) to the
# values the PC build produces from tacticSettings.xml.
add_executable(test_tactic_weights
    test_tactic_weights.cpp
)
target_link_libraries(test_tactic_weights PRIVATE
    resf2_game
    resf2_warnings
)
target_compile_features(test_tactic_weights PRIVATE cxx_std_23)
add_test(NAME test_tactic_weights COMMAND test_tactic_weights
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# TacticDecisionPipeline (ADR-005 D1): the seven-stage AI decision in the
# tracer's fixed order (UseDefense..UseCautiousMovements + epilogue),
# DecisionTrace line order, first-hit-wins, R3/R4/R6 defaults.
add_executable(test_tactic_decision_trace
    test_tactic_decision_trace.cpp
)
target_link_libraries(test_tactic_decision_trace PRIVATE
    resf2_game
    resf2_warnings
)
target_compile_features(test_tactic_decision_trace PRIVATE cxx_std_23)
add_test(NAME test_tactic_decision_trace COMMAND test_tactic_decision_trace
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# TacticMemory state model (ADR-005 D8, R5-verified): unbounded per-animation
# decayed records (no ring depth), action logs, frame countdowns, round-end
# scaling — semantics per reverse/analysis/MEMORY_INDEXING_R56.md.
add_executable(test_tactic_memory
    test_tactic_memory.cpp
)
target_link_libraries(test_tactic_memory PRIVATE
    resf2_game
    resf2_warnings
)
target_compile_features(test_tactic_memory PRIVATE cxx_std_23)
add_test(NAME test_tactic_memory COMMAND test_tactic_memory
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(test_tactic_memory PROPERTIES TIMEOUT 60)

# TacticTableSet family registry (ADR-005 D3): real .atf dump load, family
# presence bits, Header-name keyed lookup, stub families, neutral probe.
add_executable(test_tactic_tables
    test_tactic_tables.cpp
)
target_link_libraries(test_tactic_tables PRIVATE
    resf2_game
    resf2_reverse
    resf2_warnings
    zlibstatic
)
target_compile_features(test_tactic_tables PRIVATE cxx_std_23)
add_test(NAME test_tactic_tables COMMAND test_tactic_tables
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(test_tactic_tables PROPERTIES TIMEOUT 60)

# InputHandler unit tests — double-tap detection from idle and walking.
# These tests would FAIL on the old code that required move_state_ == 2
# for double-tap detection.
add_executable(test_input_handler
    test_input_handler.cpp
)
target_link_libraries(test_input_handler PRIVATE
    resf2_game
    resf2_platform
    resf2_warnings
)
target_compile_features(test_input_handler PRIVATE cxx_std_23)
add_test(NAME test_input_handler COMMAND test_input_handler
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Step cooldown integration test — proves that rapid A/D taps are gated
# by the 200 ms step cooldown, preventing the "tap-spam walks backward"
# regression. Runs the real resf2_app with a fast-tap input script and
# asserts the fighter actually moved forward.
add_executable(test_step_cooldown
    test_step_cooldown.cpp
)
target_link_libraries(test_step_cooldown PRIVATE resf2_warnings)
target_compile_features(test_step_cooldown PRIVATE cxx_std_23)
set_target_properties(test_step_cooldown PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)
add_dependencies(test_step_cooldown resf2_app)
add_test(NAME test_step_cooldown
         COMMAND test_step_cooldown "$<TARGET_FILE:resf2_app>" "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(test_step_cooldown PROPERTIES TIMEOUT 180)

# ============================================================================
# Trace replay — replay captured Frida traces against the engine.
#
# Unlike the in-tree integration tests above, this one spawns resf2_app as a
# subprocess (same pattern as test_step_cooldown / test_input_trace). The
# trace JSON lives under reverse/traces/; the test converts it to an input
# script, runs the app, parses [STATE] lines, and compares to expected_states.
# ============================================================================

add_executable(test_trace_replay
    integration/test_trace_replay.cpp
)
target_link_libraries(test_trace_replay PRIVATE resf2_warnings)
target_compile_features(test_trace_replay PRIVATE cxx_std_23)
set_target_properties(test_trace_replay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)
add_dependencies(test_trace_replay resf2_app)
add_test(NAME test_trace_replay
         COMMAND test_trace_replay
                 "$<TARGET_FILE:resf2_app>"
                 "${CMAKE_SOURCE_DIR}"
                 "${CMAKE_SOURCE_DIR}/reverse/traces/example_trace.json"
)
set_tests_properties(test_trace_replay PROPERTIES TIMEOUT 180)

# ============================================================================
# Wave 10A: real-binary E2E harness (tests/e2e/).
#
# The soak-6 defects live in real-play behavior that hermetic injection cannot
# see. These tests boot the REAL resf2_app with an --input-script and assert
# through the app's own diagnostics ([STATE] dump, resf2_debug.log) — the same
# pattern as test_step_cooldown / test_input_trace. The runner library is
# shared by every Wave-10A defect test.
# ============================================================================
add_library(resf2_e2e STATIC
    e2e/e2e_runner.cpp
)
target_include_directories(resf2_e2e PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(resf2_e2e PRIVATE resf2_warnings)
target_compile_features(resf2_e2e PRIVATE cxx_std_23)

function(add_e2e_test test_name)
    add_executable(${test_name} e2e/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE resf2_e2e resf2_warnings)
    target_compile_features(${test_name} PRIVATE cxx_std_23)
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    )
    add_dependencies(${test_name} resf2_app)
    add_test(NAME ${test_name}
             COMMAND ${test_name} "$<TARGET_FILE:resf2_app>" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 180)
endfunction()

# Canary: the harness itself (spawn + script + dump + parse + debug log) works
# against the real binary before any defect probes are built on top of it.
add_e2e_test(test_e2e_smoke)

# Wave 10A defect 1 (KNOCKBACK OVERFLOW): the sparring partner is knocked by
# scripted punches; his world x must stay inside the location's world box.
add_e2e_test(test_e2e_knockback_bounds)

# Wave 10A defect 2 (LOCATION BOUNDS): a long forward walk (root motion) and
# the back walk must clamp the PLAYER at the location world box, both walls.
add_e2e_test(test_e2e_location_bounds)

# Wave 10B soak D1 (KNOCKBACK OVERFLOW IN BATTLE): the SAME herd recipe as
# test_e2e_knockback_bounds, but booted straight into Battle — the enemy's
# world x must stay inside the location world box at every frame of a real
# fight (the soak reported "враг отбрасывается очень далеко, может и
# улететь за локацию").
add_e2e_test(test_e2e_battle_knockback_bounds)

# Wave 10B soak D2 (LOCATION BOUNDS IN BATTLE): the SAME long-walk recipe
# as test_e2e_location_bounds, but in the fight arena — the player must
# clamp at the location world box, both walls (the soak reported "у локаций
# нет границ, за которые нельзя выйти").
add_e2e_test(test_e2e_battle_location_bounds)

# Wave 10A defect 3 (FIGHT TIMER): the round countdown renders in the fight
# HUD and a timeout win continues the story (tutorial COMPLETE + follow-up
# dialogue). Boots the real binary into the Sensei tutorial flow with a
# short round clock and walks through both round timeouts.
add_e2e_test(test_e2e_fight_timer)

# Wave 10A defect 4 (DIALOGUE DIM): the dialogue must keep the location
# visible behind the parchment. Reads rendered pixels ([PIXEL] probe) and
# asserts the background corner stays lit while a dialog is open.
add_e2e_test(test_e2e_dialogue_background)

# Wave 10A defect 5 (SHOP SCROLL-DOWN + CURRENCY): scripted S presses must
# scroll the category list (visible-window probe) and the price row must
# show the coin icon, not the ruby gem.
add_e2e_test(test_e2e_shop_scroll_currency)

# Wave 10A defect 6 (MAGIC BUTTON): with a magic item equipped the fight
# HUD must render the magic button (btn_magic_normal from the fight buttons
# atlas). Boots a battle with --equip-magic MAGIC_FIRE_BALL.
add_e2e_test(test_e2e_magic_button)

# Wave 10B soak D7 (KNIVES + P WRONG MOVE): with knives equipped, ONE kick
# press must play the unarmed kick (HighKick — moves.xml has no knives kick
# templates), never knives_double_slash (a 2key|Central|Weapon PUNCH move).
# Boots a battle with --equip-weapon WEAPON_KNIVES and a single P tap.
add_e2e_test(test_e2e_knives_kick)

# Wave 11A M1 (MAGIC CHARGE): landed punches must fill the magic bar; at
# full the magic button (X) must select the equipped magic's player move
# (FireballPlayer) and cast it, consuming the charge. Boots a battle with
# --equip-magic MAGIC_FIRE_BALL, walks in, spams O, then presses X.
add_e2e_test(test_e2e_magic_charge)

# Wave 11A M2 (FACING): an attack press must ALWAYS turn the fighter to
# face the enemy (SetDirection at Controlled move start); back-walk and hit
# reactions must NEVER change the mirror. Three battle runs (attack behind,
# back-walk, hit reaction).
add_e2e_test(test_e2e_facing)

# Wave 11A M3 (DUCK REPEAT): spamming S inside the duck's SemiUninterrupt
# window must not restart the duck animation (fresh-tap edge + Controlled
# anti-restart). Boots a battle and taps S once per frame six times.
add_e2e_test(test_e2e_duck_repeat)

# Wave 11A M4 (CRITS + KNOCKDOWN): the crit roll (attr * 0.0001) fires on
# unblocked hits, crits multiply damage by 2^(0.0001*attr) and knock the
# enemy down into the fall family + bodyfall sound. Battle runs with the
# --crit-attr hook (deterministic) and without (seeded Default attr 1000).
add_e2e_test(test_e2e_crit_knockdown)

# Wave 11B W1 (BAG PHYSICS): the bag swings ONLY on a real capsule
# collision; the engine's distance fallback (|player_x - bag_x| < 200,
# game.cpp:5177-5228) is an invention to delete. Three runs: far punch (no
# swing), real punch (swing proportional to moves.xml <Impulse>), tutorial
# counter (advances only on real hits, FIRST_FIGHT still reached).
add_e2e_test(test_e2e_bag_physics)

# Wave 11B W2 (ENEMY Y + BOUNDS): enemy world Y = <ModelsViewer
# EnemyPositionY> (dojo -105); fighters stop AT the wall objects
# (<Image ClassName="left|right"> anchors, dojo +-680), not at
# +-width/2 (+-980) - both the walk clamp and the knockback clamp.
add_e2e_test(test_e2e_enemy_y_bounds)

# ============================================================================
# Phase 2: Headless integration test harness
# ============================================================================

# Helper function for integration tests that use the HeadlessTestRunner.
# Each integration test links against the full game + renderer + platform stack
# and runs headlessly via the software renderer (no GPU/GL required).
function(add_integration_test test_name)
    add_executable(${test_name}
        integration/${test_name}.cpp
        headless_test_runner.cpp
        test_platform.cpp
    )
    target_link_libraries(${test_name} PRIVATE
        resf2_game
        resf2_renderer
        resf2_platform
        resf2_scene
        resf2_format
        resf2_reverse
        resf2_warnings
        Threads::Threads
    )
    target_compile_features(${test_name} PRIVATE cxx_std_23)
    add_test(NAME ${test_name}
             COMMAND ${test_name}
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endfunction()

# Smoke test: verifies HeadlessTestRunner can instantiate and run frames
# without crashing. This is the canary that the harness itself works.
add_integration_test(test_headless_runner)

# Phase 3 integration tests
add_integration_test(test_shop_integration)
add_integration_test(test_menu_integration)
add_integration_test(test_crash_stability)
# Crash stability runs 1004 frames through the software renderer; needs more time
set_tests_properties(test_crash_stability PROPERTIES TIMEOUT 300)

# Soak-fix Wave 2: behavioral movement/input tests for the SOAK_TRIAGE.md §2
# defects (M1 back-roll key order, M2 roll displacement, M3 jump drift, M4
# dash/step pacing, M5 deferred turn). Five dojo scenarios; 120 like the
# wave-1 suites so a parallel full-suite run does not starve them.
add_integration_test(test_soak_movement_defects)
# Five dojo scenarios with full stance intros and roll/dash playouts (~12k
# frames of headless game time); 300 like test_full_battle's tolerance.
set_tests_properties(test_soak_movement_defects PROPERTIES TIMEOUT 300)

# Soak-fix Wave 5: behavioral UI tests for the SOAK_TRIAGE.md §5 defects
# (U1 enemy weapon geometry, U2 shop purchase flow + layout, U3 settings
# layout, U4 Profile click, U5 menu hides after navigation, U6 unfold
# animation). Six scenes; 300 like the wave-4 suite.
add_integration_test(test_soak_ui_defects)
set_tests_properties(test_soak_ui_defects PROPERTIES TIMEOUT 300)

# Soak S1-S3: audio voice/hit-sound selection (wave 6). The battle-scene
# half waits for a real enemy attack, which can take tens of seconds.
add_integration_test(test_soak_audio_defects)
set_tests_properties(test_soak_audio_defects PROPERTIES TIMEOUT 300)

# Soak-fix Wave 7a: behavioral parser/combat tests for the SOAK_TRIAGE.md §8
# defects (P1 weapon model filename, P2 battle hit target = enemy fighter,
# P3 armor/helm model attach, P7 held-key duck auto-repeat). Four scenes;
# 300 like the wave-5 suite. [R4b] 300 was marginal under parallel ctest
# load (measured ~160-270 s direct on 2026-08-03).
add_integration_test(test_soak_parser_defects)
set_tests_properties(test_soak_parser_defects PROPERTIES TIMEOUT 600)

# Soak re-soak-3 regression probes: R1 the fighter body/weapon render path
# (dark silhouette + weapon at the hand, vs the yellow robe/offset-weapon
# reports) and R2 the battle hit test (enemy's own model loads and is the
# hit target; mid-range punches connect). Short: one dojo + one battle.
add_integration_test(test_soak_re3_defects)
set_tests_properties(test_soak_re3_defects PROPERTIES TIMEOUT 300)

# Soak re-soak-4 (R4a render: the default Armor="Body"/Helm="Head" save must
# not double-draw the fighter). Probes for R4b (weapon edges + cycle) and
# R4c (authored battle reach) grow in the follow-up commits.
add_integration_test(test_soak_re4_defects)
set_tests_properties(test_soak_re4_defects PROPERTIES TIMEOUT 300)

# Soak-fix Wave 9B (re-soak-5): SHOP + STORY regressions. S1 item icons
# resolve from image/ut_items/icon, S2 the shop's centre column renders the
# category item list (filter/scroll/row-click), S3 the BUY hit-test at the
# desktop resolution + loud failed-buy diagnostics, S4 the preview weapon at
# the hand, S5 the story continues after the first win (Sensei dialogue ->
# knives beat -> FirstGuardBeaten: Lynx/May + tournament unlock). Shop/quest/
# scene files only; combat (Track A) untouched.
add_integration_test(test_soak_shop_story_defects)
set_tests_properties(test_soak_shop_story_defects PROPERTIES TIMEOUT 300)

# Soak-fix Wave 9A (re-soak-5): COMBAT FEEL regressions — F1 hit feedback
# (reaction anim + impact sound + hit_blade effect + authored knockback),
# F2 enemy block spam (duty cycle bound + reactive gating), F3 root-motion
# tail (no drift after the animation ends). One battle + one dojo scenario.
add_integration_test(test_soak_wave9a_defects)
set_tests_properties(test_soak_wave9a_defects PROPERTIES TIMEOUT 600)

# ---------------------------------------------------------------------------
# Hardcode-fidelity wave (HARDCODE_AUDIT.md, commit 2cf966e): the engine must
# read ASSET FILES, not invent substitutes (weapon model map, enemy anims,
# HUD stats, projectile data, defense labels). Each HIGH item is a RED-first
# probe against the REAL device data (reverse/data/list.xml, stages.xml,
# moves.xml, users.xml, models/*).
# ---------------------------------------------------------------------------
add_integration_test(test_hardcode_fidelity)
# Seven battle/dojo scene inits (H02/H05/H06/H07/H08/H09/H10) at ~25-30 s of
# asset/audio load each plus the H05/H08 live-battle windows; measured
# ~300 s direct (2026-08-05) — 600 like the wave-9a battle-heavy soak.
set_tests_properties(test_hardcode_fidelity PROPERTIES TIMEOUT 600)


# Soak-fix Wave 7a (P10): moves.xml parser fidelity — the REAL device file
# (reverse/data/animations/moves.xml, LIVE_GAME_EVIDENCE Q3) parsed by the
# engine's own loader, field-by-field vs the verbatim reference values.
add_executable(test_moves_parser_fidelity test_moves_parser_fidelity.cpp)
target_link_libraries(test_moves_parser_fidelity PRIVATE
    resf2_game
    resf2_format
    resf2_warnings
)
target_compile_features(test_moves_parser_fidelity PRIVATE cxx_std_23)
add_test(NAME test_moves_parser_fidelity COMMAND test_moves_parser_fidelity
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(test_moves_parser_fidelity PROPERTIES TIMEOUT 60)

# Phase 5: Screenshot comparison — renders headlessly, compares against
# reference PNGs using MSE, and saves diff images on failure.
add_integration_test(test_screenshot_compare)

# ---------------------------------------------------------------------------
# Wave 8: golden parser-fidelity suite (tests/integration/test_parser_fidelity.cpp).
# Every assertion is a value the ORIGINAL engine produces from the REAL device
# files (reverse/data/users.xml, reverse/data/packs.xml, assets/* authentic
# tree) per reverse/analysis/LIVE_BOOT_TRACE.md (commit 2d5c955). Runs the
# headless harness for the boot-order probe, so it links the full stack.
# ---------------------------------------------------------------------------
add_integration_test(test_parser_fidelity)
set_tests_properties(test_parser_fidelity PROPERTIES TIMEOUT 300)

# ---------------------------------------------------------------------------
# Golden tests — behaviour captured from the ORIGINAL binary via Frida.
#
# These encode measured facts about the real game, not assumptions, so a
# failure means the engine diverges from the original (a 1:1 gap), not that
# the test is stale. Reference data lives in tests/golden/, provenance in
# reverse/analysis/PORT_GAPS.md and reverse/analysis/RUNTIME_MAP.md.
#
# test_frame_timing_golden is EXPECTED TO FAIL until GAP-1/GAP-2 are ported:
# the original uses an integer 16 ms timestep (62.5 fps) with dt = ms/1000.0
# as double, while engine/core/game_loop.hpp uses a float 1/60 accumulator.
# ---------------------------------------------------------------------------
add_executable(test_frame_timing_golden test_frame_timing_golden.cpp)
target_link_libraries(test_frame_timing_golden PRIVATE resf2_warnings)
target_compile_features(test_frame_timing_golden PRIVATE cxx_std_23)
add_test(NAME test_frame_timing_golden
         COMMAND test_frame_timing_golden
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(test_frame_timing_golden PROPERTIES TIMEOUT 60)
# Documents a known divergence; flip to FALSE once the timestep is ported.
set_tests_properties(test_frame_timing_golden PROPERTIES WILL_FAIL TRUE)

# Damage formula (GAP-3): Model::getTotalDamage @ game+0x4527B4, recovered by
# decompiling the relocated dump in Ghidra. Unlike the timing test this one is
# expected to PASS -- it exercises engine/game/damage_formula.hpp, the ported
# implementation, rather than the old linear model still in game.cpp.
add_executable(test_damage_formula_golden test_damage_formula_golden.cpp)
target_link_libraries(test_damage_formula_golden PRIVATE resf2_warnings)
target_compile_features(test_damage_formula_golden PRIVATE cxx_std_23)
add_test(NAME test_damage_formula_golden
         COMMAND test_damage_formula_golden
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(test_damage_formula_golden PROPERTIES TIMEOUT 60)

# Character attributes (Model::getParameter @ game+0x6275F4) — the state the
# damage formula reads. Pins the integer storage, the -1e35 "absent" sentinel
# (which is NOT 0), and the fact that the alignment-table lookup at
# game+0x60DF98 is a separate lookup defaulting to 0.0.
add_executable(test_attributes_golden test_attributes_golden.cpp)
target_link_libraries(test_attributes_golden PRIVATE resf2_warnings)
target_compile_features(test_attributes_golden PRIVATE cxx_std_23)
add_test(NAME test_attributes_golden
         COMMAND test_attributes_golden
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(test_attributes_golden PROPERTIES TIMEOUT 60)

# Damage wiring (GAP-3): equipment -> AttributeSet aggregation feeding
# get_total_damage. Exercises engine/game/attribute_aggregation.hpp against
# the real inventory/list-parser types, so it links resf2_game + resf2_format
# like test_inventory does.
add_executable(test_damage_wiring test_damage_wiring.cpp)
target_link_libraries(test_damage_wiring PRIVATE
    resf2_game
    resf2_format
    resf2_warnings
)
target_compile_features(test_damage_wiring PRIVATE cxx_std_23)
add_test(NAME test_damage_wiring
         COMMAND test_damage_wiring
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(test_damage_wiring PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# Specification tests.
#
# These are written against the SHIPPED DATA and the reversed binary, not
# against the engine's current behaviour. The original game satisfies them by
# construction; reSF2 does not yet. A failure therefore names a concrete
# missing capability rather than a regression, which is the point: it turns
# "the shop/menu/story doesn't really work" into an enumerated, checkable list.
#
# They are marked WILL_FAIL where the capability is known to be absent, so the
# suite stays green while still recording the gap. Remove that property as each
# subsystem lands.
# ---------------------------------------------------------------------------
function(add_spec_test test_name)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE
        resf2_format
        resf2_reverse
        resf2_warnings
    )
    target_compile_features(${test_name} PRIVATE cxx_std_23)
    add_test(NAME ${test_name} COMMAND ${test_name}
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endfunction()

# Progression: stages.xml / quests.xml / perks.xml / forge.xml drive the story
# map, quest dispatch, passive perks and forge unlock flow.
add_spec_test(test_stage_spec)
add_spec_test(test_quest_spec)

# Combat model: internalSettings.xml plus moves.xml determine the damage/block
# curve and the hit windows.
# Settings / localization: userSettings.xml is read at startup by the original,
# and reSF2 now reads the shipped localization file instead of hardcoding rus.
add_spec_test(test_settings_spec)

# Save system: usersDefault.xml / user.xml are parsed, but the XML loader still
# needs more 1:1 coverage for edge cases from the shipped save format.
# Control scheme: moves.xml drives every binding. PressType (Tap vs Hold) was
# being dropped, collapsing 212 held-key moves onto their tap variants.
# Needs resf2_game for the move loader.
add_executable(test_input_spec test_input_spec.cpp)
target_link_libraries(test_input_spec PRIVATE
    resf2_game resf2_renderer resf2_platform resf2_scene
    resf2_format resf2_reverse resf2_warnings Threads::Threads)
target_compile_features(test_input_spec PRIVATE cxx_std_23)
add_test(NAME test_input_spec COMMAND test_input_spec
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(test_input_spec PROPERTIES TIMEOUT 120)

# Diagnostic: print the parsed key bindings of specific moves.
add_executable(tool_move_probe tool_move_probe.cpp)
target_link_libraries(tool_move_probe PRIVATE
    resf2_game resf2_renderer resf2_platform resf2_scene
    resf2_format resf2_reverse resf2_warnings Threads::Threads)
target_compile_features(tool_move_probe PRIVATE cxx_std_23)

# Diagnostic: dump what the XML reader actually produced for a given file.
add_executable(tool_xml_probe tool_xml_probe.cpp)
target_link_libraries(tool_xml_probe PRIVATE resf2_format resf2_warnings)
target_compile_features(tool_xml_probe PRIVATE cxx_std_23)

# ---------------------------------------------------------------------------
# tool_visual_audit — not a test, a diagnostic. Renders the dojo headlessly and
# prints every geometric quantity the on-screen composition depends on (camera
# framing, fighter transform, bag model), plus a PNG capture. Used to turn
# "the picture looks wrong" into measurable divergences from the original's
# reference screenshot.
#
#   cmake --build build --config Release --target tool_visual_audit
#   build/bin/Release/tool_visual_audit assets screenshots_audit
# ---------------------------------------------------------------------------
add_executable(tool_visual_audit
    tool_visual_audit.cpp
    headless_test_runner.cpp
    test_platform.cpp
)
target_link_libraries(tool_visual_audit PRIVATE
    resf2_game
    resf2_renderer
    resf2_platform
    resf2_scene
    resf2_format
    resf2_reverse
    resf2_warnings
    Threads::Threads
)
target_compile_features(tool_visual_audit PRIVATE cxx_std_23)

# Diagnostic: dump authored root-motion (NPivot / Align-anchor) trajectories of movement anims.
add_executable(tool_root_motion_probe tool_root_motion_probe.cpp)
target_link_libraries(tool_root_motion_probe PRIVATE
    resf2_game
    resf2_warnings
)
target_compile_features(tool_root_motion_probe PRIVATE cxx_std_23)


# Diagnostic: reproduce the soak D4 map-FIGHT dialogue flow.
add_executable(tool_d4_probe
    tool_d4_probe.cpp
    headless_test_runner.cpp
    test_platform.cpp
)
target_link_libraries(tool_d4_probe PRIVATE
    resf2_game
    resf2_renderer
    resf2_platform
    resf2_scene
    resf2_format
    resf2_reverse
    resf2_warnings
    Threads::Threads
)
target_compile_features(tool_d4_probe PRIVATE cxx_std_23)

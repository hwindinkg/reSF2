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

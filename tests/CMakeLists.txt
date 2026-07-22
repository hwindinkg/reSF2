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

# DZ decoder utility unit tests (bit tree, CRC32)
add_executable(test_dz_decoder_util
    test_dz_decoder_util.cpp
)
target_link_libraries(test_dz_decoder_util PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_dz_decoder_util PRIVATE cxx_std_23)
add_test(NAME test_dz_decoder_util COMMAND test_dz_decoder_util)

# DZ first-byte debug tracer
add_executable(test_dz_first_byte
    test_dz_first_byte.cpp
)
target_link_libraries(test_dz_first_byte PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_dz_first_byte PRIVATE cxx_std_23)
add_test(NAME test_dz_first_byte COMMAND test_dz_first_byte)

# DZ probability layout analysis
add_executable(test_dz_prob_layout
    test_dz_prob_layout.cpp
)
target_link_libraries(test_dz_prob_layout PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_dz_prob_layout PRIVATE cxx_std_23)
add_test(NAME test_dz_prob_layout COMMAND test_dz_prob_layout)

# DZ RangeSettings parser unit test
add_executable(test_dz_range_settings
    test_dz_range_settings.cpp
)
target_link_libraries(test_dz_range_settings PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_dz_range_settings PRIVATE cxx_std_23)
add_test(NAME test_dz_range_settings COMMAND test_dz_range_settings)

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

# DZ decoder test against real .dz block data
add_executable(test_dz_decode
    test_dz_decode.cpp
)
target_link_libraries(test_dz_decode PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_dz_decode PRIVATE cxx_std_23)
add_test(NAME test_dz_decode COMMAND test_dz_decode WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

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

# JSON Atlas parser test (for sf2_pc format)
add_executable(test_json_atlas
    ../test_json_atlas.cpp
)
target_link_libraries(test_json_atlas PRIVATE
    resf2_format
    resf2_core
    resf2_reverse
    zlibstatic
    resf2_warnings
)
target_compile_features(test_json_atlas PRIVATE cxx_std_23)
add_test(NAME test_json_atlas COMMAND test_json_atlas WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

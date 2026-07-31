add_library(resf2_game STATIC
    game.cpp
    helpers.cpp
    save.cpp
    player.cpp
    inventory.cpp
    shop.cpp
    input_handler.cpp
    animation_player.cpp
    combat.cpp
    asset_manager.cpp
    location_manager.cpp
    tactic_settings.cpp
    tactic_tables.cpp
    tactic_pipeline.cpp
    condition_system.cpp
    settings_loader.cpp
)

target_include_directories(resf2_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(resf2_game PUBLIC
    resf2_platform
    resf2_renderer
    resf2_runtime
    resf2_scene
    resf2_reverse
    resf2_format
    resf2_audio
    resf2_warnings
    ZLIB::ZLIB
)

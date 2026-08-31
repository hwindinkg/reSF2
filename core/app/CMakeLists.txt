# core/app/ — the native app shell (Phase 3.6a): screen manager, save
# system, main-menu/map/battle-result screens. Portable C++17 (the GLFW
# window lives in core/render via the Renderer).
add_library(sf2_app STATIC
    app.cpp
    item_catalog.cpp
    save_system.cpp
    screen_manager.cpp
    screens.cpp
)
target_include_directories(sf2_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sf2_app PUBLIC sf2_scene sf2_render sf2_data glfw)

if(MSVC)
    target_compile_options(sf2_app PRIVATE /W4)
else()
    target_compile_options(sf2_app PRIVATE -Wall -Wextra)
endif()

# app/game/ - the playable shell (Phase 3.6a): GLFW window, screen manager,
# save/load (users.xml), main menu, map. Usage:
#   game [res_root] [save_path] [--headless N] [--autoclick]
add_executable(game main.cpp)
target_link_libraries(game PRIVATE sf2_app sf2_scene sf2_render sf2_data glfw)

if(MSVC)
    target_compile_options(game PRIVATE /W4)
else()
    target_compile_options(game PRIVATE -Wall -Wextra)
endif()

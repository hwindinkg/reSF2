# app/fight_controller_demo/ - the complete fight (Phase 3.5).
#
# Loads a battle from stages.xml (the "Training" dojo battle), runs the full
# fight through the native FightController (rounds, phases StartStance/Fight/
# EndStance, timer, round-end KO/timeout, win/lose flow), logs the fight per
# second, and renders the fight HUD (HP bars + timer + round dots).
# Saves reference/extracted/scene/fight_controller_*.png and
# fight_timeout_*.png.
#
# Usage: fight_controller_demo [res_root]
# Defaults to reference/www/res at the repo root.
add_executable(fight_controller_demo main.cpp)
target_link_libraries(fight_controller_demo PRIVATE sf2_scene sf2_render sf2_data glfw)

if(MSVC)
    target_compile_options(fight_controller_demo PRIVATE /W4)
else()
    target_compile_options(fight_controller_demo PRIVATE -Wall -Wextra)
endif()

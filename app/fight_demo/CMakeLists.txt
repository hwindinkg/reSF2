# app/fight_demo/ - 2-fighter damage demo (Phase 3.3).
#
# Places the player + a punching-bag enemy in the dojo, drives a HighPunch,
# and logs the full fight-physics chain: hit detection (Bz capsule test),
# the bCa damage formula, HP application (0.01 lethal floor), knockback
# (Bl.strike impulse + Al.fha bounds), block and crit. Saves
# reference/extracted/scene/fight_demo_hit.png.
#
# Usage: fight_demo [res_root]
# Defaults to reference/www/res at the repo root.
add_executable(fight_demo main.cpp)
target_link_libraries(fight_demo PRIVATE sf2_scene sf2_render sf2_data glfw)

if(MSVC)
    target_compile_options(fight_demo PRIVATE /W4)
else()
    target_compile_options(fight_demo PRIVATE -Wall -Wextra)
endif()

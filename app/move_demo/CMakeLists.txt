# app/move_demo/ — controllable fighter: input -> move selection -> clip
# playback (Phase 3.2b).
#
# Loads the dojo, builds the player fighter (mdl_skeleton + mdl_body +
# mdl_head, Fists weapon), builds its HB move list from moves.xml
# (TacticWeapon="Fists"), then simulates 60 s of updates with injected
# Punch / Forward taps and logs per-frame move state. Renders 3 PNGs of the
# HighPunch clip at frames 1/5/9 to prove the pose animates.
#
# Usage: move_demo [res_root]
# Defaults to reference/www/res at the repo root.
add_executable(move_demo main.cpp)
target_link_libraries(move_demo PRIVATE sf2_scene sf2_render sf2_data glfw)

if(MSVC)
    target_compile_options(move_demo PRIVATE /W4)
else()
    target_compile_options(move_demo PRIVATE -Wall -Wextra)
endif()

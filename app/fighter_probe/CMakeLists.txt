# app/fighter_probe/ — native fighter in the dojo (Phase 3.1b).
#
# Renders the dojo background (reusing location_scene), then builds and
# draws the player fighter (mdl_skeleton + mdl_body + mdl_head; the Fists
# item has no model) posed with the dojo fists1_stance_idle clip at frame 0,
# anchored at the dojo player spawn. Saves
# reference/extracted/scene/fighter_native.png and prints:
#   - merged model stats (bones/tris)
#   - rendered triangle count, bounding box
#   - silhouette overlap with the oracle boot.png in the player region
#
# Usage: fighter_probe [res_root]
# Defaults to reference/www/res at the repo root.
add_executable(fighter_probe main.cpp)
target_link_libraries(fighter_probe PRIVATE sf2_scene sf2_render sf2_data glfw)

if(MSVC)
    target_compile_options(fighter_probe PRIVATE /W4)
else()
    target_compile_options(fighter_probe PRIVATE -Wall -Wextra)
endif()

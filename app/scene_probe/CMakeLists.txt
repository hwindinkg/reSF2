# app/scene_probe/ — native location renderer probe.
#
# 1. Renders the DOJO location scene with the extracted camera + parallax
#    (fight-start framing: camera centered on arena, zoom = view_w/arena_w)
#    -> reference/extracted/scene/dojo_native.png.
# 2. Renders the ARENA location scene the same way
#    -> reference/extracted/scene/arena_native.png.
# 3. Structurally compares dojo_native.png against the oracle
#    reference/traces/boot.png (color-histogram overlap, per-frame
#    ClassName presence, layer order).
# 4. Prints pixel stats for both native renders.
# 5. Writes reference/extracted/scene/compare_report.txt.
#
# Usage: scene_probe [res_root]
# Defaults to reference/www/res at the repo root.
add_executable(scene_probe main.cpp)
target_link_libraries(scene_probe PRIVATE sf2_scene sf2_render sf2_data glfw)

if(MSVC)
    target_compile_options(scene_probe PRIVATE /W4)
else()
    target_compile_options(scene_probe PRIVATE -Wall -Wextra)
endif()

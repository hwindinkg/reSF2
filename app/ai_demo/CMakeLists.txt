# app/ai_demo/ - AI-vs-AI fight demo (Phase 3.4).
#
# Two Fists fighters in the dojo, both driven by the native AI controller
# (core/scene/ai.* — the port of the game's `de` class). Loads the Fists
# tactics files + tactic_settings.xml, runs a 60s fight at 60 Hz, logs per
# second, applies hit/damage/knockback, and saves
# reference/extracted/scene/ai_demo.png at the first hit moment.
#
# Usage: ai_demo [res_root]
# Defaults to reference/www/res at the repo root.
add_executable(ai_demo main.cpp)
target_link_libraries(ai_demo PRIVATE sf2_scene sf2_render sf2_data glfw)

if(MSVC)
    target_compile_options(ai_demo PRIVATE /W4)
else()
    target_compile_options(ai_demo PRIVATE -Wall -Wextra)
endif()

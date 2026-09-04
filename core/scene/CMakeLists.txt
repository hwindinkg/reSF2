# core/scene/ — scene graph (node/sprite), camera + parallax, location scene
# loader (params XML + atlas JSON + atlas texture -> sprite layers).
add_library(sf2_scene STATIC
    ai.cpp
    ai_controller.cpp
    conditions.cpp
    damage.cpp
    effects.cpp
    fight.cpp
    fighter.cpp
    location_scene.cpp
    magic_effects.cpp
    model.cpp
    move_def.cpp
    physics.cpp
    renderer.cpp
    sprite.cpp
)
# Include root is the parent (core/) so sources can use the prefixed
# "scene/..." and "render/..." includes.
target_include_directories(sf2_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sf2_scene PUBLIC sf2_render sf2_data sf2_audio)

if(MSVC)
    target_compile_options(sf2_scene PRIVATE /W4)
else()
    target_compile_options(sf2_scene PRIVATE -Wall -Wextra)
endif()

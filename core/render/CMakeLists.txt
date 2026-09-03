# core/render/ — GL context, runtime function loading, sprite batching,
# GPU texture upload. Depends on sf2_data (Texture decode) and GLFW
# (window/context abstraction; fetched by core/data's FetchContent setup —
# declared at the top level below so both render and scene can use it).
add_library(sf2_render STATIC
    gl.cpp
    sprite_batch.cpp
    texture_gpu.cpp
)
# Include root is the parent (core/) so sources can use the prefixed
# "render/..." includes, matching core/scene's "scene/..." style.
target_include_directories(sf2_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sf2_render PUBLIC sf2_data glfw)

if(MSVC)
    target_compile_options(sf2_render PRIVATE /W4)
else()
    target_compile_options(sf2_render PRIVATE -Wall -Wextra)
endif()

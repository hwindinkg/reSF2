# engine/scene/ — CMake

add_library(resf2_scene STATIC
    scene_system.cpp
    scenes.cpp
)

target_include_directories(resf2_scene PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(resf2_scene PUBLIC cxx_std_23)
target_link_libraries(resf2_scene PUBLIC resf2_warnings)
target_link_libraries(resf2_scene PRIVATE resf2_renderer resf2_platform)

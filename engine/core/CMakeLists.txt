add_library(resf2_core STATIC
    math.cpp
    node.cpp
    state.cpp
    input_system.cpp
    asset_manager.cpp
)

target_include_directories(resf2_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(resf2_core PUBLIC
    resf2_platform
    resf2_reverse
)

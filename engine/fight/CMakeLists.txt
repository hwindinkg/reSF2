add_library(resf2_fight STATIC
    animation.cpp
    moves.cpp
)

target_include_directories(resf2_fight PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(resf2_fight PUBLIC
    resf2_core
    resf2_format
)

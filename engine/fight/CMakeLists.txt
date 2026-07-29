add_library(resf2_fight STATIC
    animation.cpp
    moves.cpp
    fighter.cpp
    ai.cpp
)

target_include_directories(resf2_fight PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(resf2_fight PUBLIC
    resf2_core
    resf2_format
)

target_link_libraries(resf2_fight PRIVATE
    resf2_reverse
    resf2_game
)

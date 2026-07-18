add_library(resf2_ui STATIC
    button.cpp
    text.cpp
)

target_include_directories(resf2_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(resf2_ui PUBLIC
    resf2_core
)

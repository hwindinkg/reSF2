add_executable(asset_explorer main.cpp)
target_link_libraries(asset_explorer PRIVATE sf2_data)

if(MSVC)
    target_compile_options(asset_explorer PRIVATE /W4)
else()
    target_compile_options(asset_explorer PRIVATE -Wall -Wextra)
endif()
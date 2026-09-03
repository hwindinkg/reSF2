add_executable(texture_probe main.cpp)
target_link_libraries(texture_probe PRIVATE sf2_data)

if(MSVC)
    target_compile_options(texture_probe PRIVATE /W4)
else()
    target_compile_options(texture_probe PRIVATE -Wall -Wextra)
endif()

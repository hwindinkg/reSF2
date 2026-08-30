add_executable(anim_model_probe main.cpp)
target_link_libraries(anim_model_probe PRIVATE sf2_data)

if(MSVC)
    target_compile_options(anim_model_probe PRIVATE /W4)
else()
    target_compile_options(anim_model_probe PRIVATE -Wall -Wextra)
endif()
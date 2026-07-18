add_library(resf2_audio STATIC
    audio.cpp
)
target_include_directories(resf2_audio PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(resf2_audio PUBLIC cxx_std_17)

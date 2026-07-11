# engine/runtime/ — CMake

# Stage 6-7 target: libresf2_runtime
# Contains AssetManager (Stage 6) + main Loop (Stage 7.1).

# ZLIB and Threads are found/downloaded at top-level CMakeLists.txt.

add_library(resf2_runtime STATIC
    asset_manager.cpp
    loop.cpp
)

target_include_directories(resf2_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(resf2_runtime PUBLIC cxx_std_23)
target_link_libraries(resf2_runtime PUBLIC
    resf2_warnings
    resf2_reverse
    resf2_platform
)
target_link_libraries(resf2_runtime PRIVATE
    ZLIB::ZLIB
    Threads::Threads
)

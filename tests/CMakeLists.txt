# tests/ — CMake

# Stage 2: S3E container parser
add_executable(test_s3e_container
    test_s3e_container.cpp
)
target_link_libraries(test_s3e_container PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_s3e_container PRIVATE cxx_std_23)
add_test(NAME test_s3e_container COMMAND test_s3e_container)

# Stage 5: Asset loaders (plist, atf, font)
add_executable(test_asset_loaders
    test_asset_loaders.cpp
)
target_link_libraries(test_asset_loaders PRIVATE
    resf2_reverse
    resf2_warnings
)
target_compile_features(test_asset_loaders PRIVATE cxx_std_23)
add_test(NAME test_asset_loaders COMMAND test_asset_loaders)

# Stage 6: AssetManager
add_executable(test_asset_manager
    test_asset_manager.cpp
)
target_link_libraries(test_asset_manager PRIVATE
    resf2_runtime
    resf2_reverse
    resf2_warnings
    Threads::Threads
)
target_compile_features(test_asset_manager PRIVATE cxx_std_23)
add_test(NAME test_asset_manager COMMAND test_asset_manager)

# Stage 7.1: Platform + Loop
add_executable(test_platform_loop
    test_platform_loop.cpp
)
target_link_libraries(test_platform_loop PRIVATE
    resf2_platform
    resf2_runtime
    resf2_warnings
    Threads::Threads
)
target_compile_features(test_platform_loop PRIVATE cxx_std_23)
add_test(NAME test_platform_loop COMMAND test_platform_loop)

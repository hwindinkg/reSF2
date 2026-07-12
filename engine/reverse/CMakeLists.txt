# engine/reverse/ — CMake

# Stage 2-5 target: libresf2_reverse
# Read-only parsers for original game binary formats:
#   .s3e   (Marmalade S3E container)        - Stage 2
#   .plist (Cocos2d-x TexturePacker v2)     - Stage 5
#   .atf   (zlib-compressed tactics blob)   - Stage 5
#   .fnt   (AngelCode BMFont bitmap font)   - Stage 5
#   .dz    (Marmalade derbh archive)        - Stage 5 (deferred: DZ algorithm)
#
# No dependencies on any other resf2 target. Links against zlib for .atf.

# ZLIB is found/downloaded at top-level CMakeLists.txt — just link here.

add_library(resf2_reverse STATIC
    s3e_container.cpp
    plist_atlas.cpp
    atf_tactics.cpp
    bitmap_font.cpp
    dz_reader.cpp
)

target_include_directories(resf2_reverse PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(resf2_reverse PUBLIC cxx_std_23)
target_link_libraries(resf2_reverse PUBLIC resf2_warnings PRIVATE ZLIB::ZLIB)

# Make zlib include dirs available to all consumers of resf2_reverse
# (tests include atf_tactics.hpp which #include <zlib.h>)
if(TARGET ZLIB::ZLIB)
    target_link_libraries(resf2_reverse PUBLIC ZLIB::ZLIB)
endif()

include(FetchContent)

# zstd — Zstandard decompression (RFC 8478). The web game ships .dat assets
# compressed with zstd (magic 28 B5 2F FD); the native port must decode them.
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake
)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zstd)

# pugixml — XML parsing (the game's XML assets are plain UTF-8 XML).
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
)
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pugixml)

# libwebp — WebP decoding for the game's .webp atlas variants.
FetchContent_Declare(
    libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG v1.4.0
    GIT_SHALLOW TRUE
)
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libwebp)

# nlohmann/json — single-header JSON for the TexturePacker atlas metadata.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(sf2_data STATIC
    zstd_stream.cpp
    xml_doc.cpp
    xml_archive.cpp
    anim_archive.cpp
    texture.cpp
    png.cpp
    webp.cpp
    dds.cpp
    ktx.cpp
    atlas.cpp
    font.cpp
    third_party/astc/astc_decomp.cpp
)
target_include_directories(sf2_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# libwebp's public headers live in src/webp/; webpdecoder is the target.
target_include_directories(sf2_data PRIVATE
    ${libwebp_SOURCE_DIR}/src
    ${libwebp_BINARY_DIR})
target_link_libraries(sf2_data PUBLIC libzstd_static pugixml::pugixml
    webpdecoder nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(sf2_data PRIVATE /W4)
else()
    target_compile_options(sf2_data PRIVATE -Wall -Wextra)
endif()

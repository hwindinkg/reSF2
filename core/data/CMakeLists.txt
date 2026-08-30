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

add_library(sf2_data STATIC
    zstd_stream.cpp
    xml_doc.cpp
    xml_archive.cpp
    anim_archive.cpp
)
target_include_directories(sf2_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sf2_data PUBLIC libzstd_static pugixml::pugixml)

if(MSVC)
    target_compile_options(sf2_data PRIVATE /W4)
else()
    target_compile_options(sf2_data PRIVATE -Wall -Wextra)
endif()
# reSF2 — top-level CMake

cmake_minimum_required(VERSION 3.24)

project(reSF2
    VERSION 0.0.3
    DESCRIPTION "Clean-room reimplementation of the Shadow Fight 2 engine"
    LANGUAGES CXX)

# --- Global compile features ---------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- Options -------------------------------------------------------------
option(RESF2_BUILD_TESTS   "Build unit and integration tests" ON)
option(RESF2_BUILD_TOOLS   "Build offline asset tools"         ON)
option(RESF2_WERROR        "Treat warnings as errors"          OFF)
option(RESF2_ENABLE_SAN    "Enable sanitizers (Clang/GCC)"     OFF)
option(RESF2_BUILD_RUNTIME "Build resf2_runtime executable"    ON)
option(RESF2_USE_GLFW      "Build GLFW platform backend"       ON)
option(RESF2_BUILD_HEADLESS "Build resf2_headless (software renderer, no GPU)" OFF)

# --- Warning flags -------------------------------------------------------
add_library(resf2_warnings INTERFACE)
target_compile_features(resf2_warnings INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(resf2_warnings INTERFACE
        /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor
        /wd4127
        $<$<BOOL:${RESF2_WERROR}>:/WX>)
else()
    target_compile_options(resf2_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Woverloaded-virtual
        -Wconversion -Wnull-dereference -Wdouble-promotion
        -Wformat=2 -Wimplicit-fallthrough
        $<$<BOOL:${RESF2_WERROR}>:-Werror>)
endif()

if(RESF2_ENABLE_SAN AND NOT MSVC)
    target_compile_options(resf2_warnings INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(resf2_warnings INTERFACE
        -fsanitize=address,undefined)
endif()

# --- Output dirs ---------------------------------------------------------
set(RESF2_ARCHIVE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib")
set(RESF2_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${RESF2_ARCHIVE_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RESF2_RUNTIME_OUTPUT_DIR}")

# --- Threading -----------------------------------------------------------
# On Windows with MSVC, Threads::Threads resolves to the native Win32 threads
set(THREADS_PREFER_PTHREAD_FLAG OFF)
find_package(Threads REQUIRED)

# --- Dependencies: ZLIB --------------------------------------------------
# Try system zlib first; if not found, auto-download via FetchContent.
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "ZLIB not found on system — auto-downloading via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
        GIT_SHALLOW    TRUE
    )
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    # Alias the target for consistent usage
    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
endif()

# --- Dependencies: GLFW (auto-download via FetchContent) -----------------
if(RESF2_USE_GLFW)
    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    # Suppress GLFW's own tests/docs/builds
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

# --- Subdirectories ------------------------------------------------------
add_subdirectory(engine)
# Renderer is part of engine subdirs

if(RESF2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RESF2_BUILD_TOOLS)
    add_subdirectory(scripts)
endif()

# --- Main executable -----------------------------------------------------
if(RESF2_BUILD_RUNTIME)
    add_executable(resf2_app main.cpp)
    target_link_libraries(resf2_app PRIVATE
        resf2_platform
        resf2_renderer
        resf2_runtime
        resf2_scene
        resf2_warnings
    )
    target_compile_features(resf2_app PRIVATE cxx_std_23)
    set_target_properties(resf2_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RESF2_RUNTIME_OUTPUT_DIR}"
    )
endif()

# --- Headless executable (software renderer, no GPU/GLFW needed) ---------
if(RESF2_BUILD_HEADLESS)
    add_executable(resf2_headless headless_main.cpp)
    target_link_libraries(resf2_headless PRIVATE
        resf2_renderer
        resf2_reverse
        resf2_warnings
    )
    target_compile_features(resf2_headless PRIVATE cxx_std_23)
    set_target_properties(resf2_headless PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RESF2_RUNTIME_OUTPUT_DIR}"
    )
endif()

# --- Print configuration summary -----------------------------------------
message(STATUS "")
message(STATUS "reSF2 ${PROJECT_VERSION}")
message(STATUS "  C++ standard        : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type          : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler            : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Warnings as errors  : ${RESF2_WERROR}")
message(STATUS "  Sanitizers          : ${RESF2_ENABLE_SAN}")
message(STATUS "  Tests               : ${RESF2_BUILD_TESTS}")
message(STATUS "  Runtime executable  : ${RESF2_BUILD_RUNTIME}")
message(STATUS "  GLFW backend        : ${RESF2_USE_GLFW}")
message(STATUS "")

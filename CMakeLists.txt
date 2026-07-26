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
    # [ORIGINAL] Sandboxes/CI without wayland-scanner must not enable the
    # Wayland backend (GLFW 3.4 FetchContent hard-fails at configure time:
    # "Failed to find wayland-scanner"). Force X11-only on Linux. Optional
    # X11 components (Xcursor/Xrandr/Xinerama/Xi) are auto-disabled by
    # GLFW's find_package(X11) when their headers are absent, so a reduced
    # build still links against core X11.
    if(NOT WIN32 AND NOT APPLE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11    ON  CACHE BOOL "" FORCE)
    endif()
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
        resf2_game
    )
    target_compile_features(resf2_app PRIVATE cxx_std_23)
    set_target_properties(resf2_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RESF2_RUNTIME_OUTPUT_DIR}"
    )

    # Integration trace: drive the real app with a scripted input sequence and
    # assert on the animation state machine it produces. Registered here rather
    # than in tests/ because resf2_app does not exist yet at that point.
    #
    # This is the check that was missing while the fighter froze on the last
    # frame of the intro stance and walking was refused outright. Both gates
    # that can refuse an animation print "[ANIM] Rejected", so any refusal
    # fails the test, and the fighter is required to reach the idle stance.
    if(RESF2_BUILD_TESTS)
        add_executable(test_input_trace tests/test_input_trace.cpp)
        target_link_libraries(test_input_trace PRIVATE resf2_warnings)
        target_compile_features(test_input_trace PRIVATE cxx_std_23)
        set_target_properties(test_input_trace PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${RESF2_RUNTIME_OUTPUT_DIR}")
        add_dependencies(test_input_trace resf2_app)
        add_test(NAME test_input_trace
                 COMMAND test_input_trace "$<TARGET_FILE:resf2_app>" "${CMAKE_SOURCE_DIR}"
                 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        set_tests_properties(test_input_trace PROPERTIES TIMEOUT 180)
    endif()
endif()

# Port removed

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

# --- DZ archive extraction (build-time only, Windows host) -----------------
# Extracts .dz archives to the fallback directories the engine expects.
# On non-Windows platforms, the extracted files must already exist in the repo
# (committed to assets/files/, assets/animations/).
set(RESF2_DZIP_EXE "${CMAKE_SOURCE_DIR}/download/dzip.exe" CACHE FILEPATH "Path to dzip.exe for DZ extraction")
set(RESF2_DZ_FILES "${CMAKE_SOURCE_DIR}/assets/files.dz" CACHE FILEPATH "DZ archive: files.dz")
set(RESF2_DZ_ANIM "${CMAKE_SOURCE_DIR}/assets/animations.dz" CACHE FILEPATH "DZ archive: animations.dz")

if(EXISTS "${RESF2_DZIP_EXE}")
    add_custom_target(dz_extract
        COMMAND "${RESF2_DZIP_EXE}" --decompress "${RESF2_DZ_FILES}"   WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/assets"
        COMMAND "${RESF2_DZIP_EXE}" --decompress "${RESF2_DZ_ANIM}"    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/assets"
        COMMENT "Extracting .dz archives to assets/files/ and assets/animations/..."
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

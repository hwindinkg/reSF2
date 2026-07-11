# engine/platform/ — CMake

# Stage 7.1 target: libresf2_platform
# Contains the platform abstraction interface + NullPlatform (headless).
# GLFW backend is added when RESF2_USE_GLFW=ON.

add_library(resf2_platform STATIC
    platform.cpp
)

target_include_directories(resf2_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(resf2_platform PUBLIC cxx_std_23)
target_link_libraries(resf2_platform PUBLIC resf2_warnings)

# Threading needed for sleep_ms / yield
target_link_libraries(resf2_platform PRIVATE Threads::Threads)

# --- GLFW backend (optional, for desktop platforms) ---
if(RESF2_USE_GLFW)
    target_sources(resf2_platform PRIVATE glfw_platform.cpp)
    target_link_libraries(resf2_platform PRIVATE glfw)

    if(WIN32)
        target_link_libraries(resf2_platform PRIVATE opengl32)
    elseif(APPLE)
        find_library(OPENGL_LIBRARY OpenGL REQUIRED)
        target_link_libraries(resf2_platform PRIVATE ${OPENGL_LIBRARY})
    else()
        target_link_libraries(resf2_platform PRIVATE GL)
    endif()

    # On Windows, GLFW needs the Win32 subsystem
    if(WIN32)
        target_compile_definitions(resf2_platform PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
    endif()
endif()

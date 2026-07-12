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
        # [ORIGINAL] Use find_package(OpenGL) so we get the FULL resolved path
        # to libGL (OPENGL_gl_LIBRARY / OPENGL_LIBRARIES). A bare `GL` link
        # name fails on sandboxes without a system libgl-dev symlink, because
        # CMAKE_LIBRARY_PATH only feeds find_library(), not the link -L flags.
        # The full path links directly with no -L required.
        find_package(OpenGL REQUIRED)
        target_link_libraries(resf2_platform PRIVATE ${OPENGL_LIBRARIES})
        # [ORIGINAL] GLFW's glfw3.h does `#include <GL/gl.h>`; on sandboxes
        # without system libgl-dev, OPENGL_INCLUDE_DIR points at a
        # user-extracted prefix holding GL/gl.h. Add it explicitly.
        if(OPENGL_INCLUDE_DIR)
            target_include_directories(resf2_platform PRIVATE ${OPENGL_INCLUDE_DIR})
        endif()
    endif()

    # On Windows, GLFW needs the Win32 subsystem
    if(WIN32)
        target_compile_definitions(resf2_platform PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
        # We bypass GLFW key events on Windows and call GetAsyncKeyState()
        # directly, which lives in user32.dll. opengl32 already pulls in
        # user32 implicitly, but link it explicitly so the dependency is
        # self-documenting and survives future link-order changes.
        target_link_libraries(resf2_platform PRIVATE user32)
    endif()
endif()

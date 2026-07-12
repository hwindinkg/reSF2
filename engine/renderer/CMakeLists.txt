# engine/renderer/ — CMake

# Always build the software renderer (no GL dependency) and the shared
# stb_image implementation unit.
set(RESF2_RENDERER_SOURCES
    software_renderer.cpp
    stb_image_impl.cpp
)

# The GL renderer + loader require GL headers; only build them when GL is found.
set(RESF2_NEED_GL FALSE)

if(WIN32)
    list(APPEND RESF2_RENDERER_SOURCES renderer.cpp gl_loader.cpp)
    set(RESF2_GL_LIBS opengl32)
    set(RESF2_NEED_GL TRUE)
elseif(APPLE)
    find_library(OPENGL_LIBRARY OpenGL REQUIRED)
    list(APPEND RESF2_RENDERER_SOURCES renderer.cpp gl_loader.cpp)
    set(RESF2_GL_LIBS ${OPENGL_LIBRARY})
    set(RESF2_NEED_GL TRUE)
else()
    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        list(APPEND RESF2_RENDERER_SOURCES renderer.cpp gl_loader.cpp)
        set(RESF2_GL_LIBS ${OPENGL_LIBRARIES})
        set(RESF2_NEED_GL TRUE)
    else()
        # Headless build: GL not available, software renderer only.
        message(STATUS "OpenGL not found — building software renderer only.")
    endif()
endif()

add_library(resf2_renderer STATIC ${RESF2_RENDERER_SOURCES})

target_include_directories(resf2_renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# [ORIGINAL] find_package(OpenGL) sets OPENGL_INCLUDE_DIR (the dir containing
# GL/gl.h). On sandboxes without system libgl-dev, that dir is a user-extracted
# prefix; without adding it here, renderer.cpp/gl_loader.cpp fail with
# "GL/gl.h: No such file or directory" even though the lib was found.
if(RESF2_NEED_GL AND OPENGL_INCLUDE_DIR)
    target_include_directories(resf2_renderer PRIVATE ${OPENGL_INCLUDE_DIR})
endif()

target_compile_features(resf2_renderer PUBLIC cxx_std_23)
target_link_libraries(resf2_renderer PUBLIC resf2_warnings)

if(RESF2_NEED_GL)
    target_link_libraries(resf2_renderer PRIVATE ${RESF2_GL_LIBS})
else()
    target_compile_definitions(resf2_renderer PUBLIC RESF2_NO_GL=1)
endif()

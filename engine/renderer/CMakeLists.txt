# engine/renderer/ — CMake

# Always build the software renderer (no GL dependency) and the shared
# stb_image implementation unit.
set(RESF2_RENDERER_SOURCES
    software_renderer.cpp
    stb_image_impl.cpp
)

# WebP decoder sources
set(RESF2_WEBP_SOURCES
    webp/src/dec/alpha_dec.c
    webp/src/dec/buffer_dec.c
    webp/src/dec/frame_dec.c
    webp/src/dec/idec_dec.c
    webp/src/dec/io_dec.c
    webp/src/dec/quant_dec.c
    webp/src/dec/tree_dec.c
    webp/src/dec/vp8_dec.c
    webp/src/dec/vp8l_dec.c
    webp/src/dec/webp_dec.c
    webp/src/dsp/alpha_processing.c
    webp/src/dsp/alpha_processing_sse2.c
    webp/src/dsp/alpha_processing_sse41.c
    webp/src/dsp/cpu.c
    webp/src/dsp/dec.c
    webp/src/dsp/dec_clip_tables.c
    webp/src/dsp/dec_sse2.c
    webp/src/dsp/dec_sse41.c
    webp/src/dsp/filters.c
    webp/src/dsp/filters_sse2.c
    webp/src/dsp/lossless.c
    webp/src/dsp/lossless_sse2.c
    webp/src/dsp/lossless_sse41.c
    webp/src/dsp/rescaler.c
    webp/src/dsp/rescaler_sse2.c
    webp/src/dsp/upsampling.c
    webp/src/dsp/upsampling_sse2.c
    webp/src/dsp/upsampling_sse41.c
    webp/src/dsp/yuv.c
    webp/src/dsp/yuv_sse2.c
    webp/src/dsp/yuv_sse41.c
    webp/src/utils/bit_reader_utils.c
    webp/src/utils/color_cache_utils.c
    webp/src/utils/filters_utils.c
    webp/src/utils/huffman_utils.c
    webp/src/utils/quant_levels_dec_utils.c
    webp/src/utils/random_utils.c
    webp/src/utils/rescaler_utils.c
    webp/src/utils/thread_utils.c
    webp/src/utils/utils.c
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
target_sources(resf2_renderer PRIVATE ${RESF2_WEBP_SOURCES})

target_include_directories(resf2_renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/webp
    ${CMAKE_CURRENT_SOURCE_DIR}/webp/src
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

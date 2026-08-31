// Minimal GL header for the SF2 native renderer.
//
// Provides the constants (GL_TRIANGLES, GL_BLEND, ...) and the
// `GLuint`/`GLfloat` typedefs the sprite renderer and texture upload use,
// without pulling in a platform GL headers (we are on a core profile loaded
// at runtime). Values match the OpenGL 3.3 core specification.

#ifndef SF2_RENDER_GL_TYPES_HPP
#define SF2_RENDER_GL_TYPES_HPP

#include <cstdint>

using GLuint = unsigned int;
using GLint = int;
using GLfloat = float;
using GLsizei = int;
using GLsizeiptr = std::intptr_t;
using GLenum = unsigned int;

// clear bits
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x100

// capabilities / state
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_SCISSOR_TEST 0x0C11

// blend factors
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

// boolean uniforms / attrib normalization
#define GL_FALSE 0
#define GL_TRUE 1

// buffer targets / usage
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8

// attribute types
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406

// textures
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MAX_LEVEL 0x813D

// shader types
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

// draw modes
#define GL_TRIANGLES 0x0004

#endif // SF2_RENDER_GL_TYPES_HPP

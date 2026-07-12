// engine/renderer/gl_loader.hpp
//
// Minimal OpenGL function loader for Windows/Linux/macOS.
// Defines ALL missing GL types and constants for Windows (which only
// ships OpenGL 1.1 headers), then declares function pointers.

#pragma once

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

// [ORIGINAL] On Linux/macOS, GL_GLEXT_PROTOTYPES MUST be defined BEFORE
// <GL/gl.h> so that <GL/glext.h> (pulled in by gl.h) emits real prototypes
// for GL 2.0+ functions (glCreateShader, glGenBuffers, glUseProgram,
// glUniform*, glActiveTexture, glVertexAttribPointer, ...). Mesa's gl.h
// includes glext.h internally, and glext.h gates all prototype emission on
// GL_GLEXT_PROTOTYPES. Defining it AFTER gl.h is a no-op because glext.h is
// already include-guarded by then (verified this session: the original
// ordering produced 'glUseProgram was not declared in this scope' on GCC).
//
// On Windows we do NOT want glext prototypes: we load GL 2.0+ at runtime via
// wglGetProcAddress (see gl_loader.cpp), so the symbols are our own extern
// pointers, not libGL prototypes.
#ifndef _WIN32
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

// ---- Define missing GL types (Windows GL/gl.h only has OpenGL 1.1) ----
#ifndef GL_VERSION_1_5
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#endif

#ifndef GL_VERSION_2_0
typedef char GLchar;
#endif

// ---- Define missing GL constants ----
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                   0x8892
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW                   0x88E8
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE                  0x812F
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                       0x84C0
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER                  0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER                0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS                 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS                    0x8B82
#endif

// ---- Function pointer types ----
typedef void (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const GLvoid*, GLenum);
typedef void (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint);
typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint);
typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar*);
typedef void (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY *PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void (APIENTRY *PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (APIENTRY *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum);

// ---- Global function pointers (same names as GL functions) ----
#ifdef _WIN32
// On Windows, declare as extern pointers (loaded at runtime)
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLUNIFORM4FPROC glUniform4f;
extern PFNGLUNIFORM1FPROC glUniform1f;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLACTIVETEXTUREPROC glActiveTexture;

void init_gl_functions();
#else
// [ORIGINAL] Linux/macOS path.
// GL_GLEXT_PROTOTYPES was defined above <GL/gl.h>, so gl.h already pulled in
// <GL/glext.h> with real prototypes for GL 2.0+ functions. Those symbols are
// exported by Mesa's libGL.so.1 (verified this session):
//   nm -D /usr/lib/x86_64-linux-gnu/libGL.so.1 | grep glCreateShader
//   -> 0000000000049660 T glCreateShader
//   -> 000000000004c620 T glGenBuffers
//   -> ... (all GL 2.0 symbols present)
// and resolve at static link time from -lGL. No runtime loading needed,
// unlike the Windows wglGetProcAddress path above.
#define init_gl_functions() ((void)0)
#endif

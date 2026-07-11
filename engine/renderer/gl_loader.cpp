// engine/renderer/gl_loader.cpp

#include "gl_loader.hpp"

#ifdef _WIN32

PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORM4FPROC glUniform4f = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;

void init_gl_functions() {
    if (glGenBuffers) return;
    auto gp = [](const char* n) -> void* { return wglGetProcAddress(n); };
    glGenBuffers = (PFNGLGENBUFFERSPROC)gp("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)gp("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)gp("glBufferData");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)gp("glDeleteBuffers");
    glCreateShader = (PFNGLCREATESHADERPROC)gp("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)gp("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)gp("glCompileShader");
    glDeleteShader = (PFNGLDELETESHADERPROC)gp("glDeleteShader");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)gp("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)gp("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)gp("glLinkProgram");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)gp("glDeleteProgram");
    glUseProgram = (PFNGLUSEPROGRAMPROC)gp("glUseProgram");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)gp("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)gp("glGetShaderInfoLog");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)gp("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)gp("glGetProgramInfoLog");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)gp("glGetUniformLocation");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)gp("glUniformMatrix4fv");
    glUniform1i = (PFNGLUNIFORM1IPROC)gp("glUniform1i");
    glUniform4f = (PFNGLUNIFORM4FPROC)gp("glUniform4f");
    glUniform1f = (PFNGLUNIFORM1FPROC)gp("glUniform1f");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)gp("glEnableVertexAttribArray");
    glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)gp("glDisableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)gp("glVertexAttribPointer");
    glActiveTexture = (PFNGLACTIVETEXTUREPROC)gp("glActiveTexture");
}

#endif

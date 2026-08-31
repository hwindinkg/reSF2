// GL entry points used by the SF2 native renderer, listed as
// SF2_GL_FUNC(return_type, name, parameters). Included twice:
//   - inside namespace sf2::render::gl with SF2_GL_FUNC defined to declare
//     `name##_t` typedefs and `name` function pointers (gl.cpp),
//   - by gl.hpp consumers through the renderer headers.
//
// Every function is resolved at runtime via glfwGetProcAddress. Keeping the
// list here means the loader and the declarations can never drift.

#ifndef SF2_GL_FUNC
#error "gl_loader.inc must be included with SF2_GL_FUNC defined"
#endif

// --- 1.0 / 1.1 core state + drawing ---------------------------------------
SF2_GL_FUNC(void, glEnable, (unsigned int cap))
SF2_GL_FUNC(void, glDisable, (unsigned int cap))
SF2_GL_FUNC(void, glClearColor, (float r, float g, float b, float a))
SF2_GL_FUNC(void, glClear, (unsigned int mask))
SF2_GL_FUNC(void, glViewport, (int x, int y, int w, int h))
SF2_GL_FUNC(void, glBlendFunc, (unsigned int sfactor, unsigned int dfactor))
SF2_GL_FUNC(void, glPixelStorei, (unsigned int pname, int param))
SF2_GL_FUNC(void, glReadBuffer, (unsigned int mode))
SF2_GL_FUNC(void, glReadPixels, (int x, int y, int w, int h, unsigned int format,
                                 unsigned int type, void* pixels))

// --- buffers + VAO ---------------------------------------------------------
SF2_GL_FUNC(void, glGenBuffers, (int n, unsigned int* bufs))
SF2_GL_FUNC(void, glDeleteBuffers, (int n, const unsigned int* bufs))
SF2_GL_FUNC(void, glBindBuffer, (unsigned int target, unsigned int buffer))
SF2_GL_FUNC(void, glBufferData, (unsigned int target, long long size, const void* data,
                                 unsigned int usage))
SF2_GL_FUNC(void, glGenVertexArrays, (int n, unsigned int* arrays))
SF2_GL_FUNC(void, glDeleteVertexArrays, (int n, const unsigned int* arrays))
SF2_GL_FUNC(void, glBindVertexArray, (unsigned int array))
SF2_GL_FUNC(void, glEnableVertexAttribArray, (unsigned int index))
SF2_GL_FUNC(void, glDisableVertexAttribArray, (unsigned int index))
SF2_GL_FUNC(void, glVertexAttribPointer, (unsigned int index, int size, unsigned int type,
                                          unsigned char normalized, int stride, const void* ptr))

// --- shaders ---------------------------------------------------------------
SF2_GL_FUNC(unsigned int, glCreateShader, (unsigned int type))
SF2_GL_FUNC(void, glDeleteShader, (unsigned int shader))
SF2_GL_FUNC(void, glShaderSource, (unsigned int shader, int count, const char** source,
                                   const int* length))
SF2_GL_FUNC(void, glCompileShader, (unsigned int shader))
SF2_GL_FUNC(void, glGetShaderiv, (unsigned int shader, unsigned int pname, int* params))
SF2_GL_FUNC(void, glGetShaderInfoLog, (unsigned int shader, int buf_size, int* length,
                                       char* info_log))
SF2_GL_FUNC(unsigned int, glCreateProgram, (void))
SF2_GL_FUNC(void, glDeleteProgram, (unsigned int program))
SF2_GL_FUNC(void, glAttachShader, (unsigned int program, unsigned int shader))
SF2_GL_FUNC(void, glLinkProgram, (unsigned int program))
SF2_GL_FUNC(void, glGetProgramiv, (unsigned int program, unsigned int pname, int* params))
SF2_GL_FUNC(void, glGetProgramInfoLog, (unsigned int program, int buf_size, int* length,
                                        char* info_log))
SF2_GL_FUNC(void, glUseProgram, (unsigned int program))
SF2_GL_FUNC(int, glGetUniformLocation, (unsigned int program, const char* name))
SF2_GL_FUNC(void, glUniformMatrix4fv, (int location, int count, unsigned char transpose,
                                       const float* value))
SF2_GL_FUNC(void, glUniform1i, (int location, int v0))

// --- textures --------------------------------------------------------------
SF2_GL_FUNC(void, glGenTextures, (int n, unsigned int* textures))
SF2_GL_FUNC(void, glDeleteTextures, (int n, const unsigned int* textures))
SF2_GL_FUNC(void, glBindTexture, (unsigned int target, unsigned int texture))
SF2_GL_FUNC(void, glTexImage2D, (unsigned int target, int level, int internalformat, int width,
                                 int height, int border, unsigned int format, unsigned int type,
                                 const void* pixels))
SF2_GL_FUNC(void, glTexParameteri, (unsigned int target, unsigned int pname, int param))
SF2_GL_FUNC(void, glGenerateMipmap, (unsigned int target))
SF2_GL_FUNC(void, glActiveTexture, (unsigned int texture))

// --- draw ------------------------------------------------------------------
SF2_GL_FUNC(void, glDrawArrays, (unsigned int mode, int first, int count))

#undef SF2_GL_FUNC

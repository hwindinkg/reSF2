// GPU texture upload — the only place core/data textures meet GL.

#include "render/texture_gpu.hpp"

#include "render/gl.hpp"
#include "texture.hpp"

namespace sf2::render {

GLuint upload_texture_rgba(const sf2::data::Texture& tex) {
    if (tex.w <= 0 || tex.h <= 0 || tex.rgba.empty()) {
        return 0;
    }
    GLuint id = 0;
    gl::glGenTextures(1, &id);
    gl::glBindTexture(GL_TEXTURE_2D, id);
    gl::glPixelStorei(0x0D05 /* GL_UNPACK_ALIGNMENT */, 1);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex.w, tex.h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, tex.rgba.data());
    gl::glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

void delete_texture(GLuint texture) {
    if (texture != 0) {
        gl::glDeleteTextures(1, &texture);
    }
}

} // namespace sf2::render

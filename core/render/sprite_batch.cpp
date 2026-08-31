// Sprite batch implementation: shader compile/link, VBO/VAO setup, quad
// accumulation and batched draws (one glDrawArrays per texture switch).

#include "render/sprite_batch.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "render/gl.hpp"

namespace sf2::render {

namespace {

const char* kVertexSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform mat4 u_proj;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
}
)GLSL";

const char* kFragmentSrc = R"GLSL(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
out vec4 frag;
void main() {
    vec4 texel = texture(u_tex, v_uv);
    frag = texel * v_color;
}
)GLSL";

unsigned int compile_shader(unsigned int type, const char* src, const char* label) {
    unsigned int shader = gl::glCreateShader(type);
    gl::glShaderSource(shader, 1, &src, nullptr);
    gl::glCompileShader(shader);
    int ok = 0;
    gl::glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[1024] = {0};
        gl::glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        std::fprintf(stderr, "gl shader compile error (%s): %s\n", label, log);
        gl::glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

SpriteBatch::~SpriteBatch() { shutdown(); }

bool SpriteBatch::init(int view_w, int view_h) {
    view_w_ = view_w;
    view_h_ = view_h;

    const unsigned int vs = compile_shader(GL_VERTEX_SHADER, kVertexSrc, "sprite_vs");
    const unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentSrc, "sprite_fs");
    if (vs == 0 || fs == 0) {
        return false;
    }
    program_ = gl::glCreateProgram();
    gl::glAttachShader(program_, vs);
    gl::glAttachShader(program_, fs);
    gl::glLinkProgram(program_);
    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);
    int ok = 0;
    gl::glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[1024] = {0};
        gl::glGetProgramInfoLog(program_, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        std::fprintf(stderr, "gl program link error: %s\n", log);
        gl::glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    uniform_proj_ = gl::glGetUniformLocation(program_, "u_proj");
    uniform_tex_ = gl::glGetUniformLocation(program_, "u_tex");

    gl::glGenVertexArrays(1, &vao_);
    gl::glBindVertexArray(vao_);
    gl::glGenBuffers(1, &vbo_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // 2 pos + 2 uv + 4 color, all float.
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              reinterpret_cast<const void*>(offsetof(SpriteVertex, x)));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              reinterpret_cast<const void*>(offsetof(SpriteVertex, u)));
    gl::glEnableVertexAttribArray(2);
    gl::glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              reinterpret_cast<const void*>(offsetof(SpriteVertex, r)));
    gl::glBindVertexArray(0);

    // 1x1 white texture: the solid-color path binds it so the fragment
    // shader's texture() sample is complete (sampling texture 0 in a core
    // profile returns black).
    const std::uint8_t white[4] = {255, 255, 255, 255};
    gl::glGenTextures(1, &white_tex_);
    gl::glBindTexture(GL_TEXTURE_2D, white_tex_);
    gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, white);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2600 /*GL_NEAREST*/);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, 0x2600 /*GL_NEAREST*/);
    gl::glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void SpriteBatch::shutdown() {
    if (white_tex_ != 0) {
        gl::glDeleteTextures(1, &white_tex_);
        white_tex_ = 0;
    }
    if (vbo_ != 0) {
        gl::glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        gl::glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (program_ != 0) {
        gl::glDeleteProgram(program_);
        program_ = 0;
    }
}

void SpriteBatch::set_viewport(int view_w, int view_h) {
    view_w_ = view_w;
    view_h_ = view_h;
    gl::glViewport(0, 0, view_w_, view_h_);
}

void SpriteBatch::add_quad(const SpriteQuad& quad, unsigned int texture) {
    if (!vertices_.empty() && texture != current_texture_) {
        draw_batch();
    }
    current_texture_ = texture;
    for (int i = 0; i < 6; ++i) {
        vertices_.push_back(quad.v[i]);
    }
}

void SpriteBatch::add_triangles(const float* verts, std::size_t vertex_count,
                                float r, float g, float b, float a) {
    if (vertex_count == 0 || verts == nullptr) {
        return;
    }
    if (!vertices_.empty() && current_texture_ != 0) {
        draw_batch();  // flush textured quads first (solid path binds no texture)
    }
    current_texture_ = 0;
    for (std::size_t i = 0; i < vertex_count; ++i) {
        SpriteVertex v;
        v.x = verts[i * 2];
        v.y = verts[i * 2 + 1];
        v.u = v.v = 0.0f;
        v.r = r;
        v.g = g;
        v.b = b;
        v.a = a;
        vertices_.push_back(v);
    }
}

void SpriteBatch::flush() { draw_batch(); }

void SpriteBatch::draw_batch() {
    if (vertices_.empty()) {
        current_texture_ = 0;
        return;
    }
    gl::glUseProgram(program_);
    gl::glBindVertexArray(vao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // Upload all accumulated vertices.
    gl::glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices_.size() * sizeof(SpriteVertex)),
                     vertices_.data(), GL_STREAM_DRAW);

    // Orthographic projection: screen pixel (0,0) top-left -> NDC.
    // x_ndc = 2*x/view_w - 1 ; y_ndc = 1 - 2*y/view_h.
    const float proj[16] = {
        2.0f / static_cast<float>(view_w_), 0.0f, 0.0f, 0.0f,
        0.0f,                                -2.0f / static_cast<float>(view_h_), 0.0f, 0.0f,
        0.0f,                                0.0f, 1.0f, 0.0f,
        -1.0f,                               1.0f, 0.0f, 1.0f,
    };
    gl::glUniformMatrix4fv(uniform_proj_, 1, GL_FALSE, proj);

    if (current_texture_ != 0) {
        gl::glActiveTexture(GL_TEXTURE0);
        gl::glBindTexture(GL_TEXTURE_2D, current_texture_);
        gl::glUniform1i(uniform_tex_, 0);
    } else {
        // Solid-color path: bind the 1x1 white texture — color * white = color.
        gl::glActiveTexture(GL_TEXTURE0);
        gl::glBindTexture(GL_TEXTURE_2D, white_tex_);
        gl::glUniform1i(uniform_tex_, 0);
    }

    gl::glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    vertices_.clear();
    current_texture_ = 0;
}

} // namespace sf2::render

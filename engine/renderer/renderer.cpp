#include "renderer.hpp"
#include "gl_loader.hpp"

#include "stb_image.h"

#include <algorithm>  // [ORIGINAL] std::min/std::max with initializer_list (draw_line_screen)
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace resf2::renderer {

// ---- Mat4 ----
Mat4 Mat4::identity() { return Mat4{}; }

Mat4 Mat4::ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
    Mat4 m;
    float rml = right - left, tmb = top - bottom, fmn = farZ - nearZ;
    m.m[0] = 2.0f / rml; m.m[5] = 2.0f / tmb; m.m[10] = -2.0f / fmn;
    m.m[12] = -(right + left) / rml; m.m[13] = -(top + bottom) / tmb; m.m[14] = -(farZ + nearZ) / fmn;
    return m;
}

// ---- Texture2D ----
Texture2D::Texture2D() = default;
Texture2D::~Texture2D() { if (gl_id_) glDeleteTextures(1, &gl_id_); }

void Texture2D::init_rgba(int width, int height, const std::uint8_t* pixels) {
    if (gl_id_) glDeleteTextures(1, &gl_id_);
    glGenTextures(1, &gl_id_);
    glBindTexture(GL_TEXTURE_2D, gl_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    width_ = width; height_ = height;
}

bool Texture2D::init_from_png(const std::uint8_t* data, std::size_t size) {
    int w, h, channels;
    stbi_uc* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    if (!pixels) return false;
    init_rgba(w, h, pixels);
    stbi_image_free(pixels);
    return true;
}

void Texture2D::bind(std::uint32_t unit) const {
    if (unit > 0) glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, gl_id_);
}

// ---- ShaderProgram ----
ShaderProgram::ShaderProgram() = default;
ShaderProgram::~ShaderProgram() {
    if (program_) { glUseProgram(0); glDeleteProgram(program_); }
    if (vertex_shader_) glDeleteShader(vertex_shader_);
    if (fragment_shader_) glDeleteShader(fragment_shader_);
}

static unsigned int compile_shader(unsigned int type, const std::string& src) {
    unsigned int shader = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(shader, 1, (const GLchar* const*)&p, nullptr);
    glCompileShader(shader);
    int ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(shader, 1024, nullptr, log);
        std::fprintf(stderr, "Shader error: %s\n", log);
        glDeleteShader(shader); return 0;
    }
    return shader;
}

bool ShaderProgram::init(const std::string& vs, const std::string& fs) {
    init_gl_functions();
    vertex_shader_ = compile_shader(GL_VERTEX_SHADER, vs);
    if (!vertex_shader_) return false;
    fragment_shader_ = compile_shader(GL_FRAGMENT_SHADER, fs);
    if (!fragment_shader_) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vertex_shader_);
    glAttachShader(program_, fragment_shader_);
    glLinkProgram(program_);
    int ok = 0; glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(program_, 1024, nullptr, log); std::fprintf(stderr, "Link error: %s\n", log); return false; }
    return true;
}

void ShaderProgram::use() const { glUseProgram(program_); }
void ShaderProgram::set_uniform_mat4(const char* n, const Mat4& m) const { int l = glGetUniformLocation(program_, n); if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, m.m); }
void ShaderProgram::set_uniform_1i(const char* n, int v) const { int l = glGetUniformLocation(program_, n); if (l >= 0) glUniform1i(l, v); }
void ShaderProgram::set_uniform_4f(const char* n, float x, float y, float z, float w) const { int l = glGetUniformLocation(program_, n); if (l >= 0) glUniform4f(l, x, y, z, w); }
void ShaderProgram::set_uniform_1f(const char* n, float v) const { int l = glGetUniformLocation(program_, n); if (l >= 0) glUniform1f(l, v); }

// ---- SpriteBatch ----
SpriteBatch::SpriteBatch() = default;
SpriteBatch::~SpriteBatch() { if (vbo_) glDeleteBuffers(1, &vbo_); }

bool SpriteBatch::init() { init_gl_functions(); glGenBuffers(1, &vbo_); vertices_.reserve(kMaxVertices); return vbo_ != 0; }

void SpriteBatch::begin(const Texture2D& tex, const ShaderProgram& sh, const Mat4& mvp) {
    current_texture_ = &tex; current_shader_ = &sh; batching_ = true; vertices_.clear();
    sh.use(); sh.set_uniform_mat4("u_MVP", mvp); sh.set_uniform_1i("u_texture", 0); tex.bind(0);
}

void SpriteBatch::draw_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color4B color) {
    if (vertices_.size() + 6 > kMaxVertices) flush();
    SpriteVertex v; v.r = color.r; v.g = color.g; v.b = color.b; v.a = color.a;
    // Y-UP world coords: (x,y) = bottom-left, (x+w, y+h) = top-right.
    // UV: v0 = top of frame in PNG, v1 = bottom of frame in PNG.
    // stbi_load gives PNG top at row 0. glTexImage2D puts row 0 at GL V=0.
    // So GL V=0 = top of PNG, GL V=1 = bottom of PNG.
    // Atlas V coords map DIRECTLY to GL V coords — NO FLIP needed.
    // World bottom (y) → PNG bottom (v1) → GL V = v1
    // World top    (y+h) → PNG top    (v0) → GL V = v0
    v.x = x;     v.y = y;     v.u = u0; v.v = v1; vertices_.push_back(v);  // bottom-left
    v.x = x + w; v.y = y;     v.u = u1; v.v = v1; vertices_.push_back(v);  // bottom-right
    v.x = x;     v.y = y + h; v.u = u0; v.v = v0; vertices_.push_back(v);  // top-left
    v.x = x + w; v.y = y;     v.u = u1; v.v = v1; vertices_.push_back(v);  // bottom-right
    v.x = x + w; v.y = y + h; v.u = u1; v.v = v0; vertices_.push_back(v);  // top-right
    v.x = x;     v.y = y + h; v.u = u0; v.v = v0; vertices_.push_back(v);  // top-left
}

void SpriteBatch::draw_quad_screen(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color4B color) {
    if (vertices_.size() + 6 > kMaxVertices) flush();
    SpriteVertex v; v.r = color.r; v.g = color.g; v.b = color.b; v.a = color.a;
    // Y-DOWN screen coords: (x,y) = top-left
    // stbi_load puts PNG row 0 (top) first. glTexImage2D puts row 0 at GL V=0.
    // So GL V=0 = top of PNG. Atlas V coords map directly — NO FLIP.
    v.x = x;     v.y = y;     v.u = u0; v.v = v0; vertices_.push_back(v);  // top-left
    v.x = x + w; v.y = y;     v.u = u1; v.v = v0; vertices_.push_back(v);  // top-right
    v.x = x;     v.y = y + h; v.u = u0; v.v = v1; vertices_.push_back(v);  // bottom-left
    v.x = x + w; v.y = y;     v.u = u1; v.v = v0; vertices_.push_back(v);  // top-right
    v.x = x + w; v.y = y + h; v.u = u1; v.v = v1; vertices_.push_back(v);  // bottom-right
    v.x = x;     v.y = y + h; v.u = u0; v.v = v1; vertices_.push_back(v);  // bottom-left
}

void SpriteBatch::draw_triangle(
    float x0, float y0, float x1, float y1, float x2, float y2,
    Color4B color)
{
    if (vertices_.size() + 3 > kMaxVertices) flush();
    SpriteVertex v; v.r = color.r; v.g = color.g; v.b = color.b; v.a = color.a;
    v.u = 0; v.v = 0;  // solid color
    v.x = x0; v.y = y0; vertices_.push_back(v);
    v.x = x1; v.y = y1; vertices_.push_back(v);
    v.x = x2; v.y = y2; vertices_.push_back(v);
}

void SpriteBatch::flush() {
    if (vertices_.empty() || !current_shader_) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(SpriteVertex), vertices_.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, r));
    glDrawArrays(GL_TRIANGLES, 0, (int)vertices_.size());
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
    vertices_.clear();
}

void SpriteBatch::end() { flush(); batching_ = false; current_texture_ = nullptr; current_shader_ = nullptr; }

// ---- Camera2D ----
Camera2D::Camera2D(float vw, float vh) : view_width_(vw), view_height_(vh) {}

void Camera2D::update(std::uint32_t dt_ms) {
    float dt = dt_ms / 1000.0f;
    float lerp = 1.0f - std::exp(-dt * 10.0f);
    x_ += (target_x_ - x_) * lerp; y_ += (target_y_ - y_) * lerp; zoom_ += (target_zoom_ - zoom_) * lerp;
    if (shake_duration_ > 0) { shake_elapsed_ += dt_ms; if (shake_elapsed_ >= shake_duration_) { shake_amplitude_ = 0; shake_duration_ = 0; } }
}

Mat4 Camera2D::view_projection() const {
    float hw = view_width_ / (2.0f * zoom_), hh = view_height_ / (2.0f * zoom_);
    float sx = 0, sy = 0;
    if (shake_amplitude_ > 0) {
        float t = (float)shake_elapsed_ / shake_duration_, d = 1.0f - t;
        sx = ((float)rand() / RAND_MAX - 0.5f) * shake_amplitude_ * d * 2;
        sy = ((float)rand() / RAND_MAX - 0.5f) * shake_amplitude_ * d * 2;
    }
    // Y-UP world coordinate system (cocos2d-x convention):
    // bottom = y - hh (smaller Y = down), top = y + hh (larger Y = up).
    // This is the standard OpenGL ortho projection with Y-UP.
    return Mat4::ortho(x_ - hw + sx, x_ + hw + sx, y_ - hh + sy, y_ + hh + sy);
}

void Camera2D::shake(float amp, std::uint32_t dur) { shake_amplitude_ = amp; shake_duration_ = dur; shake_elapsed_ = 0; }

// ---- Renderer ----
static const char* kVS = R"(#version 120
attribute vec2 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
uniform mat4 u_MVP;
varying vec2 v_texCoord;
varying vec4 v_color;
void main() { gl_Position = u_MVP * vec4(a_position, 0.0, 1.0); v_texCoord = a_texCoord; v_color = a_color; }
)";

static const char* kFS = R"(#version 120
varying vec2 v_texCoord;
varying vec4 v_color;
uniform sampler2D u_texture;
void main() { gl_FragColor = v_color * texture2D(u_texture, v_texCoord); }
)";

Renderer::Renderer() = default;
Renderer::~Renderer() { shutdown(); }

bool Renderer::init(int w, int h) {
    init_gl_functions();
    width_ = w; height_ = h;
    glViewport(0, 0, w, h);
    default_shader_ = std::make_unique<ShaderProgram>();
    if (!default_shader_->init(kVS, kFS)) return false;
    batch_ = std::make_unique<SpriteBatch>();
    if (!batch_->init()) return false;
    camera_ = Camera2D((float)w, (float)h);
    camera_.set_target((float)w / 2, (float)h / 2);
    // 1x1 white texture for filled shapes (rects, circles, lines).
    white_tex_ = std::make_unique<Texture2D>();
    const std::uint8_t white[4] = {255, 255, 255, 255};
    white_tex_->init_rgba(1, 1, white);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void Renderer::shutdown() { batch_.reset(); default_shader_.reset(); white_tex_.reset(); }

void Renderer::resize(int w, int h) {
    width_ = w; height_ = h;
    glViewport(0, 0, w, h);
    camera_ = Camera2D((float)w, (float)h);
    camera_.set_target((float)w / 2, (float)h / 2);
}

void Renderer::begin_frame() { glClear(GL_COLOR_BUFFER_BIT); camera_.update(16); }
void Renderer::end_frame() {}

void Renderer::draw_textured_quad(const Texture2D& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color4B color) {
    Mat4 mvp = camera_.view_projection();
    batch_->begin(tex, *default_shader_, mvp);
    batch_->draw_quad(x, y, w, h, u0, v0, u1, v1, color);
    batch_->end();
}

void Renderer::set_clear_color(float r, float g, float b, float a) { glClearColor(r, g, b, a); }

// ---- Screen-space & primitive rendering ----
// Screen-space uses Y-DOWN (origin top-left, like raster displays).
// This is separate from world-space (Y-UP, origin center).
// draw_quad_screen handles the UV mapping for Y-DOWN coordinates.

static Mat4 screen_proj(int w, int h) {
    return Mat4::ortho(0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f);
}

void Renderer::draw_textured_quad_screen(
    const Texture2D& tex, float x, float y, float w, float h,
    float u0, float v0, float u1, float v1, Color4B color)
{
    Mat4 mvp = screen_proj(width_, height_);
    batch_->begin(tex, *default_shader_, mvp);
    batch_->draw_quad_screen(x, y, w, h, u0, v0, u1, v1, color);
    batch_->end();
}

void Renderer::draw_filled_rect_screen(float x, float y, float w, float h,
                                       Color4B color) {
    Mat4 mvp = screen_proj(width_, height_);
    batch_->begin(*white_tex_, *default_shader_, mvp);
    batch_->draw_quad_screen(x, y, w, h, 0, 0, 1, 1, color);
    batch_->end();
}

void Renderer::draw_filled_triangle_screen(
    float x0, float y0, float x1, float y1, float x2, float y2,
    Color4B color)
{
    Mat4 mvp = screen_proj(width_, height_);
    batch_->begin(*white_tex_, *default_shader_, mvp);
    batch_->draw_triangle(x0, y0, x1, y1, x2, y2, color);
    batch_->end();
}

void Renderer::draw_filled_triangle_world(
    float x0, float y0, float x1, float y1, float x2, float y2,
    Color4B color)
{
    Mat4 mvp = camera_.view_projection();
    batch_->begin(*white_tex_, *default_shader_, mvp);
    batch_->draw_triangle(x0, y0, x1, y1, x2, y2, color);
    batch_->end();
}

void Renderer::draw_filled_circle_screen(float cx, float cy, float radius,
                                         Color4B color) {
    Mat4 mvp = screen_proj(width_, height_);
    batch_->begin(*white_tex_, *default_shader_, mvp);
    const int segs = 16;
    float step = 6.28318530f / segs;
    for (int i = 0; i < segs; ++i) {
        float a0 = i * step, a1 = (i + 1) * step;
        batch_->draw_triangle(cx, cy,
            cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
            cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color);
    }
    batch_->end();
}

void Renderer::draw_filled_circle_world(float cx, float cy, float radius,
                                        Color4B color) {
    Mat4 mvp = camera_.view_projection();
    batch_->begin(*white_tex_, *default_shader_, mvp);
    const int segs = 12;
    float step = 6.28318530f / segs;
    for (int i = 0; i < segs; ++i) {
        float a0 = i * step, a1 = (i + 1) * step;
        batch_->draw_triangle(cx, cy,
            cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
            cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color);
    }
    batch_->end();
}

void Renderer::draw_line_screen(float x0, float y0, float x1, float y1,
                                Color4B color) {
    // Approximate as a thin filled rectangle along the line direction.
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    float ux = dx / len, uy = dy / len;        // unit direction
    float px = -uy, py = ux;                    // perpendicular
    float thickness = 1.0f;
    // Four corners of the thin rect.
    float hx = px * thickness * 0.5f, hy = py * thickness * 0.5f;
    float corners[4][2] = {
        {x0 - hx, y0 - hy},
        {x0 + hx, y0 + hy},
        {x1 + hx, y1 + hy},
        {x1 - hx, y1 - hy},
    };
    float minx = std::min({corners[0][0], corners[1][0], corners[2][0], corners[3][0]});
    float miny = std::min({corners[0][1], corners[1][1], corners[2][1], corners[3][1]});
    float maxx = std::max({corners[0][0], corners[1][0], corners[2][0], corners[3][0]});
    float maxy = std::max({corners[0][1], corners[1][1], corners[2][1], corners[3][1]});
    draw_filled_rect_screen(minx, miny, maxx - minx, maxy - miny, color);
}

void Renderer::draw_line_world(float x0, float y0, float x1, float y1,
                               Color4B color) {
    // Convert world coords (Y-UP) to screen coords, then draw a screen-space line.
    // Y-UP: positive world Y -> smaller screen Y (upward).
    float hw = (float)width_  / (2.0f * camera_.zoom());
    float hh = (float)height_ / (2.0f * camera_.zoom());
    float left = camera_.x() - hw, right = camera_.x() + hw;
    float bottom = camera_.y() - hh, top = camera_.y() + hh;
    auto w2s = [&](float wx, float wy, float& sx, float& sy) {
        sx = (wx - left) / (right - left) * (float)width_;
        // Y-UP world -> Y-DOWN screen: invert Y
        sy = (1.0f - (wy - bottom) / (top - bottom)) * (float)height_;
    };
    float sx0, sy0, sx1, sy1;
    w2s(x0, y0, sx0, sy0);
    w2s(x1, y1, sx1, sy1);
    draw_line_screen(sx0, sy0, sx1, sy1, color);
}

}  // namespace resf2::renderer

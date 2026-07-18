#pragma once

#include <string>
#include <vector>
#include "../core/math.hpp"
#include "../core/renderer2d.hpp"

namespace resf2::ui {

// Bitmap font glyph from AngelCode .fnt format
struct Glyph {
    int id = 0;
    int x = 0, y = 0;
    int w = 0, h = 0;
    int ox = 0, oy = 0;
    int advance = 0;
};

class BitmapFont {
public:
    bool load_from_fnt(const std::string& fnt_text);
    bool load_from_fnt_data(const uint8_t* data, size_t size);

    void set_texture(uint32_t tex_id) { texture_id_ = tex_id; }
    uint32_t texture_id() const { return texture_id_; }

    float measure_width(const std::string& text, float scale = 1.0f) const;
    float measure_height(const std::string& text, float scale = 1.0f) const;

    void render(core::Renderer2D& r, const std::string& text,
                float x, float y, float scale = 1.0f,
                uint32_t color = 0xFFFFFFFF) const;

    int line_height() const { return line_height_; }

private:
    std::vector<Glyph> glyphs_;
    int line_height_ = 0;
    int base_ = 0;
    int tex_w_ = 0, tex_h_ = 0;
    uint32_t texture_id_ = 0;

    const Glyph* find_glyph(int id) const;
};

// HUD elements
class HUD {
public:
    void set_health(float current, float max) { health_ = current; max_health_ = max; }
    void set_energy(float current, float max) { energy_ = current; max_energy_ = max; }
    void set_gold(int amount) { gold_ = amount; }
    void set_level(int level) { level_ = level; }
    void set_texture_bar_bg(uint32_t t) { bar_bg_ = t; }
    void set_texture_bar_fill(uint32_t t) { bar_fill_ = t; }

    void render(core::Renderer2D& r);

private:
    float health_ = 1.0f, max_health_ = 1.0f;
    float energy_ = 1.0f, max_energy_ = 1.0f;
    int gold_ = 0;
    int level_ = 1;
    uint32_t bar_bg_ = 0, bar_fill_ = 0;
};

} // namespace resf2::ui

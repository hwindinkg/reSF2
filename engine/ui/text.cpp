#include "text.hpp"
#include <cstdio>
#include <cstring>
#include <sstream>

namespace resf2::ui {

bool BitmapFont::load_from_fnt(const std::string& fnt_text) {
    return load_from_fnt_data((const uint8_t*)fnt_text.data(), fnt_text.size());
}

bool BitmapFont::load_from_fnt_data(const uint8_t* data, size_t size) {
    std::string text((const char*)data, size);
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.substr(0, 6) == "char id") {
            Glyph g;
            int ch = 0;
            int ok = sscanf_s(line.c_str(),
                "char id=%d x=%d y=%d width=%d height=%d xoffset=%d yoffset=%d xadvance=%d page=%*d",
                &ch, &g.x, &g.y, &g.w, &g.h, &g.ox, &g.oy, &g.advance);
            if (ok >= 7) {
                g.id = ch;
                glyphs_.push_back(g);
            }
        } else if (line.substr(0, 8) == "common l") {
            sscanf_s(line.c_str(), "common lineHeight=%d base=%d scaleW=%d scaleH=%d",
                &line_height_, &base_, &tex_w_, &tex_h_);
        }
    }

    return !glyphs_.empty();
}

const Glyph* BitmapFont::find_glyph(int id) const {
    for (auto& g : glyphs_)
        if (g.id == id) return &g;
    return nullptr;
}

float BitmapFont::measure_width(const std::string& text, float scale) const {
    float w = 0;
    for (auto c : text) {
        auto* g = find_glyph((int)c);
        if (g) w += g->advance * scale;
    }
    return w;
}

float BitmapFont::measure_height(const std::string& text, float scale) const {
    return line_height_ * scale;
}

void BitmapFont::render(core::Renderer2D& r, const std::string& text,
                        float x, float y, float scale, uint32_t color) const {
    float cx = x;
    float cy = y;
    for (auto c : text) {
        if (c == '\n') {
            cx = x;
            cy += line_height_ * scale;
            continue;
        }
        auto* g = find_glyph((int)c);
        if (!g) continue;
        float gw = g->w * scale;
        float gh = g->h * scale;
        float gx = cx + g->ox * scale;
        float gy = cy + g->oy * scale;
        float u0 = (float)g->x / tex_w_;
        float v0 = (float)g->y / tex_h_;
        float u1 = (float)(g->x + g->w) / tex_w_;
        float v1 = (float)(g->y + g->h) / tex_h_;

        core::DrawQuad q;
        q.x = gx; q.y = gy; q.w = gw; q.h = gh;
        q.u0 = u0; q.v0 = v0; q.u1 = u1; q.v1 = v1;
        q.texture_id = texture_id_;
        q.color = color;
        r.draw_quad(q);

        cx += g->advance * scale;
    }
}

void HUD::render(core::Renderer2D& r) {
    const float bar_w = 200, bar_h = 16;
    const float bar_x = 10, bar_y = 10;

    // Health bar background
    r.draw_rect(bar_x, bar_y, bar_w, bar_h, 0xFF333333);
    // Health bar fill
    float hp_ratio = max_health_ > 0 ? health_ / max_health_ : 0;
    r.draw_rect(bar_x, bar_y, bar_w * hp_ratio, bar_h, 0xFF00AA00);

    // Energy bar below health
    float en_y = bar_y + bar_h + 4;
    r.draw_rect(bar_x, en_y, bar_w, bar_h, 0xFF333333);
    float en_ratio = max_energy_ > 0 ? energy_ / max_energy_ : 0;
    r.draw_rect(bar_x, en_y, bar_w * en_ratio, bar_h, 0xFF00AAAA);
}

} // namespace resf2::ui

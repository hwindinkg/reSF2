// engine/game/hud_renderer.cpp
//
// HudRenderer implementation — HUD, menu, and dialog rendering.

#include "hud_renderer.hpp"
#include "asset_manager.hpp"
#include "location_manager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ren = resf2::renderer;

namespace resf2::game {

// ---------- Inline text rendering helper ----------

static void render_text_inline(
    ren::Renderer& renderer,
    const std::shared_ptr<resf2::reverse::font::ParsedFont>& font,
    const std::unique_ptr<ren::Texture2D>& font_tex,
    const std::string& text, float x, float y,
    float scale, ren::Color4B color
) {
    if (!font || !font_tex) return;
    float cx = x;
    for (char c : text) {
        int32_t cp = (uint8_t)c;
        if (cp >= 0xC0 && cp <= 0xFF) cp = 0x0410 + (cp - 0xC0);
        auto it = font->char_index.find(cp);
        if (it == font->char_index.end()) {
            it = font->char_index.find(32);
            if (it == font->char_index.end()) continue;
        }
        auto& ch = font->chars[it->second];
        if (ch.width > 0 && ch.height > 0) {
            float u0 = (float)ch.x / font->common.scale_w;
            float v0 = (float)ch.y / font->common.scale_h;
            float u1 = (float)(ch.x + ch.width) / font->common.scale_w;
            float v1 = (float)(ch.y + ch.height) / font->common.scale_h;
            renderer.draw_textured_quad_screen(*font_tex,
                cx + ch.xoffset * scale,
                y + ch.yoffset * scale,
                ch.width * scale, ch.height * scale,
                u0, v0, u1, v1, color);
        }
        cx += ch.xadvance * scale;
    }
}

// ========== render_hud ==========

void HudRenderer::render_hud(
    ren::Renderer& renderer, plat::Platform& platform,
    float player_health, float player_max_health,
    float enemy_health, float enemy_max_health,
    float player_energy, float player_max_energy,
    int currency, int /*wins*/, int /*losses*/,
    const std::string& /*current_location_name*/,
    float /*menu_anim_progress*/,
    Overlay overlay,
    float /*zoom*/,
    bool is_battle_mode,
    const std::vector<std::string>& /*completed_levels*/,
    const std::vector<std::pair<std::string, std::string>>& /*dialogue_lines*/,
    int /*dialogue_index*/,
    const std::unordered_map<int, std::unique_ptr<ren::Texture2D>>& /*zone_bg_textures*/
) {
    auto& hud = assets_.hud_textures();
    auto& font = assets_.hud_font();
    auto& font_tex = assets_.hud_font_tex();

    // Top panel
    auto panel_it = hud.find("Top_Panel");
    if (panel_it != hud.end()) {
        auto& tex = panel_it->second;
        float panel_h = 50.0f;
        float tile_w = panel_h * (float)tex->width() / (float)tex->height();
        float x = 0;
        float win_w = (float)platform.window_width();
        while (x < win_w) {
            float draw_w = std::min(tile_w, win_w - x);
            float u1 = draw_w / tile_w;
            renderer.draw_textured_quad_screen(*tex, x, 0, draw_w, panel_h, 0, 0, u1, 1.0f);
            x += draw_w;
        }
    } else {
        renderer.draw_filled_rect_screen(0, 0, (float)platform.window_width(), 50, ren::Color4B{0,0,0,180});
    }

    // Gold
    auto gold_it = hud.find("gold");
    if (gold_it != hud.end()) renderer.draw_textured_quad_screen(*gold_it->second, 10, 9, 32, 32);
    render_text_inline(renderer, font, font_tex, std::to_string(currency), 50, 15, 0.32f, {255,240,200,255});

    // Energy
    auto energy_it = hud.find("energy");
    if (energy_it != hud.end()) renderer.draw_textured_quad_screen(*energy_it->second, 180, 9, 32, 32);
    render_text_inline(renderer, font, font_tex,
        std::to_string((int)player_energy) + " / " + std::to_string((int)player_max_energy),
        220, 15, 0.32f, {200,230,255,255});

    // Level bar
    auto lvlbar_it = hud.find("Level_bar");
    if (lvlbar_it != hud.end()) renderer.draw_textured_quad_screen(*lvlbar_it->second, 330, 15, 120, 20);
    render_text_inline(renderer, font, font_tex, "LVL 1", 460, 15, 0.30f, {255,255,255,255});

    // Menu button (left side)
    auto btn_it = hud.find("btn_menu");
    if (btn_it != hud.end()) {
        renderer.draw_textured_quad_screen(*btn_it->second, 10, 58, 130, 40);
    } else {
        renderer.draw_filled_rect_screen(10, 58, 130, 40, ren::Color4B{60,40,30,220});
        render_text_inline(renderer, font, font_tex, "MENU", 20, 70, 0.30f, {255,220,180,255});
    }

    // Health bars (only in battle mode)
    if (is_battle_mode) {
        // Player health
        float hx = 10.0f, hy = 105.0f, hw = 200.0f, hh = 16.0f;
        renderer.draw_filled_rect_screen(hx, hy, hw, hh, ren::Color4B{40,20,20,200});
        float player_pct = player_max_health > 0 ? player_health / player_max_health : 0;
        if (player_pct > 0) renderer.draw_filled_rect_screen(hx+2, hy+2, (hw-4)*player_pct, hh-4, ren::Color4B{80,200,80,220});
        render_text_inline(renderer, font, font_tex, "HP", hx+4, hy+2, 0.25f, {255,255,255,220});
        render_text_inline(renderer, font, font_tex, std::to_string((int)player_health), hx+hw-40, hy+2, 0.25f, {255,255,255,220});

        // Enemy health (right side)
        float ehx = platform.window_width() - 210.0f, ehy = 105.0f;
        renderer.draw_filled_rect_screen(ehx, ehy, hw, hh, ren::Color4B{40,20,20,200});
        float enemy_pct = enemy_max_health > 0 ? enemy_health / enemy_max_health : 0;
        if (enemy_pct > 0) renderer.draw_filled_rect_screen(ehx+2, ehy+2, (hw-4)*enemy_pct, hh-4, ren::Color4B{200,80,80,220});
    }

    // Energy bar (bottom center)
    if (is_battle_mode) {
        float bx = (float)platform.window_width()/2 - 100.0f, by = (float)platform.window_height() - 30.0f;
        float bw = 200.0f, bh = 12.0f;
        renderer.draw_filled_rect_screen(bx, by, bw, bh, ren::Color4B{20,20,40,180});
        float energy_pct = player_max_energy > 0 ? player_energy / player_max_energy : 0;
        if (energy_pct > 0) renderer.draw_filled_rect_screen(bx+2, by+2, (bw-4)*energy_pct, bh-4, ren::Color4B{60,120,255,200});
    }
}

// ========== render_menu_expanded ==========

void HudRenderer::render_menu_expanded(
    ren::Renderer& renderer, plat::Platform& platform,
    float progress, int /*currency*/, int /*wins*/, int /*losses*/,
    const std::string& /*equipped_weapon*/,
    const std::string& /*current_location_name*/,
    const std::vector<std::string>& /*completed_levels*/
) {
    if (progress <= 0.0f) return;

    float ww = (float)platform.window_width();
    float wh = (float)platform.window_height();
    auto& scroll = assets_.scroll_textures();
    auto& menu = assets_.menu_textures();

    // Semi-transparent backdrop
    renderer.draw_filled_rect_screen(0, 0, ww, wh, ren::Color4B{0,0,0,160});

    // Menu panel — animated expand from top
    float eased = progress;  // linear for now
    float panel_h = 400.0f * eased;
    float btn_x = 120.0f;
    float btn_y = 60.0f;

    renderer.draw_filled_rect_screen(btn_x, btn_y, 120, panel_h, ren::Color4B{40,30,20,230});

    // Scroll menu using textures (if loaded)
    auto left_tex = scroll.find("scroll_left");
    auto center_tex = scroll.find("scroll_center");
    auto right_tex = scroll.find("scroll_right");

    if (left_tex != scroll.end() && center_tex != scroll.end() && right_tex != scroll.end()) {
        float roll_h = 40.0f;
        float cap_w = (float)left_tex->second->width() * (roll_h / (float)left_tex->second->height());
        float center_w = ww - 2 * cap_w;
        ren::Color4B roll_col{255,255,255,200};
        renderer.draw_textured_quad_screen(*left_tex->second, btn_x, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
        renderer.draw_textured_quad_screen(*center_tex->second, btn_x+cap_w, btn_y, center_w, roll_h, 0,0,1,1, roll_col);
        renderer.draw_textured_quad_screen(*right_tex->second, btn_x+cap_w+center_w, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
    }

    // Menu items
    struct MenuItem { const char* label; float y_offset; };
    MenuItem items[] = {
        {"MAP", 80}, {"SHOP", 140}, {"SETTINGS", 200},
        {"INVENTORY", 260}, {"PROFILE", 320}, {"EXIT", 380}
    };

    auto& font = assets_.hud_font();
    auto& font_tex = assets_.hud_font_tex();
    for (auto& item : items) {
        float iy = btn_y + item.y_offset;
        if (iy < btn_y + panel_h) {
            renderer.draw_filled_rect_screen(btn_x+10, iy, 100, 36, ren::Color4B{60,50,40,200});
            render_text_inline(renderer, font, font_tex, item.label, btn_x+20, iy+8, 0.35f, {220,200,180,255});
        }
    }

    (void)menu; // menu_textures used elsewhere
}

// ========== render_dialog_overlay ==========

void HudRenderer::render_dialog_overlay(
    ren::Renderer& renderer, plat::Platform& platform,
    const std::vector<std::pair<std::string, std::string>>& dialogue_lines,
    int dialogue_index
) {
    if (dialogue_lines.empty()) return;
    float ww = (float)platform.window_width();
    float wh = (float)platform.window_height();

    // Dark backdrop
    renderer.draw_filled_rect_screen(0, 0, ww, wh, ren::Color4B{0,0,0,180});

    // Dialog panel
    float pw = ww * 0.8f, ph = 200.0f;
    float px = (ww - pw) / 2.0f, py = wh - ph - 40.0f;
    renderer.draw_filled_rect_screen(px, py, pw, ph, ren::Color4B{20,15,10,240});

    // Border
    ren::Color4B border{80,60,40,255};
    renderer.draw_filled_rect_screen(px, py, pw, 3, border);
    renderer.draw_filled_rect_screen(px, py+ph-3, pw, 3, border);
    renderer.draw_filled_rect_screen(px, py, 3, ph, border);
    renderer.draw_filled_rect_screen(px+pw-3, py, 3, ph, border);

    auto& font = assets_.hud_font();
    auto& font_tex = assets_.hud_font_tex();

    // Speaker name
    if (dialogue_index >= 0 && dialogue_index < (int)dialogue_lines.size()) {
        auto& [speaker, text] = dialogue_lines[dialogue_index];
        render_text_inline(renderer, font, font_tex, speaker, px+20, py+15, 0.35f, {255,200,100,255});
        render_text_inline(renderer, font, font_tex, text, px+20, py+55, 0.30f, {220,220,220,255});
    }

    // Continue indicator
    render_text_inline(renderer, font, font_tex, "[SPACE to continue]", px+pw-200, py+ph-30, 0.25f, {160,160,160,200});
}

} // namespace resf2::game

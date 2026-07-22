// engine/scene/scenes.cpp
//
// Concrete Scene implementations.

#include "scenes.hpp"
#include "scene_system.hpp"

#include "../renderer/renderer.hpp"
#include "../platform/platform.hpp"
#include "../format/stage_parser.hpp"
#include "../format/list_parser.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace resf2::scene {
namespace ren = resf2::renderer;

// Helper: draw a simple filled rectangle in screen space (for UI overlays).
// The renderer uses world coordinates; for UI we just use a large ortho
// projection. For now we use the renderer's existing text + colored quad
// drawing via debug lines. This is intentionally minimal — the real UI
// will use proper texture atlases once the Cocos2d-x UI format is decoded.

// Helper: get key state from platform input
static bool key_pressed(const platform::InputState& input, platform::Key k) {
    return input.keys_just_pressed[(size_t)k];
}

static bool key_down(const platform::InputState& input, platform::Key k) {
    return input.keys_down[(size_t)k];
}

// Helper: check if any pointer was just pressed at (x,y) within a rect
static bool clicked_in(const platform::InputState& input,
                       float x, float y, float w, float h) {
    for (const auto& p : input.pointers) {
        if (p.just_pressed &&
            p.x >= x && p.x <= x + w &&
            p.y >= y && p.y <= y + h) {
            return true;
        }
    }
    return false;
}

// ============================================================
// BootScene
// ============================================================

void BootScene::on_enter(SceneContext&) {
    std::printf("[boot] splash\n");
    elapsed_ms_ = 0;
}

void BootScene::on_update(SceneContext& ctx) {
    elapsed_ms_ += ctx.dt_ms;
    if (elapsed_ms_ >= kBootDurationMs) {
        ctx.host.request_scene_transition(SceneId::Loading);
    }
}

void BootScene::on_render(SceneContext&) {
    // Just clear to black (renderer's clear color is already set)
}

// ============================================================
// LoadingScene
// ============================================================

void LoadingScene::on_enter(SceneContext& ctx) {
    std::printf("[loading] start\n");
    elapsed_ms_ = 0;
    loading_started_ = false;
}

void LoadingScene::on_update(SceneContext& ctx) {
    elapsed_ms_ += ctx.dt_ms;
    // Start asset loading on the first update (not in on_enter, to allow
    // the loading screen to render at least one frame first).
    if (!loading_started_ && elapsed_ms_ > 100) {
        loading_started_ = true;
        ctx.host.host_load_location();
        std::printf("[loading] assets loaded\n");
    }
    if (loading_started_ && elapsed_ms_ >= kMinDisplayMs) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
    }
}

void LoadingScene::on_render(SceneContext& ctx) {
    // Render the loading screen via the host.
    ctx.host.host_render_loading();
}

// ============================================================
// MainMenuScene
// ============================================================

void MainMenuScene::on_enter(SceneContext& ctx) {
    std::printf("[mainmenu] enter\n");
    // Reset menu/overlay state
    ctx.host.host_reset_menu_state();
    ctx.host.host_set_battle_mode(false);
    // Always reload dojo location (battle may have swapped it)
    ctx.host.host_load_battle_location("dojo");
    // Start menu music
    ctx.host.host_start_menu_music();
}

void MainMenuScene::on_update(SceneContext& ctx) {
    // Delegate dojo gameplay (movement, combat, animation, physics, overlays)
    // to the host. The host handles A/D, Space, K, M, T, Esc, etc.
    ctx.host.host_update_gameplay(ctx.dt_ms);

    // After the host has processed gameplay input, check for menu-item clicks
    // that trigger scene transitions. These are scene-specific.
    const auto& input = ctx.platform.input();

    // Menu items are positioned BELOW the menu button (which is at y=58,
    // h=40, so ends at y=98). Items start at y=103 to avoid overlap.
    // Each item is 45px tall.
    // Menu items — positioned to match the scroll menu (render_menu_expanded)
    // which draws icons at: btn_x=10, paper_y=95, icon_y = 109 + idx*64
    struct MenuItem { const char* name; SceneId target; float y; };
    MenuItem items[] = {
        {"Dojo",     SceneId::MainMenu, 109.0f + 0 * 64.0f},
        {"Map",      SceneId::Map,      109.0f + 1 * 64.0f},
        {"Shop",     SceneId::Shop,     109.0f + 2 * 64.0f},
        {"Dialogue", SceneId::Dialogue, 109.0f + 3 * 64.0f},
        {"Settings", SceneId::Settings, 109.0f + 4 * 64.0f},
        {"Profile",  SceneId::Profile,  109.0f + 5 * 64.0f},
    };
    for (const auto& item : items) {
        if (clicked_in(input, 10.0f, item.y, 130.0f, 40.0f)) {
            ctx.host.host_play_ui_click();
            std::printf("[mainmenu] clicked '%s' -> %s\n",
                        item.name, scene_name(item.target));
            // Set up dialogue if going to Dialogue
            if (item.target == SceneId::Dialogue) {
                ctx.host.host_set_dialogue({
                    {"Sly", "Welcome back, fighter."},
                    {"Sly", "The tournament awaits. Are you ready?"},
                    {"Narrator", "Round 1 - Fight!"},
                });
                ctx.host.host_set_current_level("test_battle");
            }
            ctx.host.request_scene_transition(item.target);
            return;
        }
    }

    // Keyboard shortcut: 'N' for New Game (go to Map)
    if (key_pressed(input, platform::Key::N)) {
        ctx.host.request_scene_transition(SceneId::Map);
    }
}

void MainMenuScene::on_render(SceneContext& ctx) {
    // The host renders the dojo, character, bag, HUD, and menu overlay.
    ctx.host.host_render_scene();
}

void MainMenuScene::on_exit(SceneContext& ctx) {
    std::printf("[mainmenu] exit\n");
    ctx.host.host_stop_music();
}

bool MainMenuScene::on_quit_request(SceneContext&) {
    // Allow quit from main menu
    return true;
}

// ============================================================
// MapScene
// ============================================================

void MapScene::on_enter(SceneContext& ctx) {
    std::printf("[map] enter\n"); selected_ = 0; scroll_x_ = 0; scroll_target_x_ = 0; selected_battle_ = nullptr;
    ctx.renderer.set_clear_color(0.03f, 0.03f, 0.08f, 1.0f);
    auto* stages = ctx.host.host_get_stages();
    if (stages && !stages->zones.empty()) {
        zone_battles_.clear();
        for (const auto& zone : stages->zones) {
            std::vector<size_t> indices;
            for (size_t bi = 0; bi < zone.battles.size(); ++bi) {
                const auto& battle = zone.battles[bi];
                if (battle.type == "HIDDEN" || battle.type == "FAKE" || battle.name.find("LOCKED") != std::string::npos || battle.name.find("ECLIPSE") != std::string::npos || battle.name.find("INTERMISSION") != std::string::npos) continue;
                indices.push_back(bi);
            }
            if (!indices.empty()) zone_battles_.push_back({zone, indices});
        }
        std::printf("[map] loaded %zu zones\n", zone_battles_.size());
    }
}

void MapScene::update_selected_battle() {
    selected_battle_ = nullptr; reward_money_ = 0; reward_exp_ = 0; fight_power_ = 0;
    if (selected_ >= 0 && (size_t)selected_ < zone_battles_.size()) {
        auto& z = zone_battles_[selected_];
        selected_zone_name_ = z.zone.name;
        if (!z.battle_indices.empty()) {
            size_t bi = z.battle_indices[0];  // first visible battle
            selected_battle_ = &z.zone.battles[bi];
            if (!selected_battle_->fights.empty()) {
                reward_money_ = selected_battle_->fights[0].reward.money;
                reward_exp_ = selected_battle_->fights[0].reward.exp;
                fight_power_ = selected_battle_->fights[0].power;
            }
        }
    }
}


void MapScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input(); if (zone_battles_.empty()) return;
    // Horizontal scroll: Right/D = next zone, Left/A = previous zone
    float max_scroll_x = std::max(0.0f, (float)zone_battles_.size() * 240.0f - ctx.platform.window_width() + 280.0f);
    bool changed = false;
    if (key_pressed(input, platform::Key::ArrowRight) || key_pressed(input, platform::Key::D)) { selected_++; changed = true; if (selected_ >= (int)zone_battles_.size()) selected_ = (int)zone_battles_.size() - 1; }
    if (key_pressed(input, platform::Key::ArrowLeft) || key_pressed(input, platform::Key::A)) { selected_--; changed = true; if (selected_ < 0) selected_ = 0; }
    if (changed) {
        float sel_x = selected_ * 240.0f;
        if (sel_x > scroll_target_x_ + ctx.platform.window_width() - 400.0f) scroll_target_x_ = sel_x - ctx.platform.window_width() + 420.0f;
        if (sel_x < scroll_target_x_ + 60.0f) scroll_target_x_ = std::max(0.0f, sel_x - 60.0f);
        scroll_target_x_ = std::max(0.0f, std::min(scroll_target_x_, max_scroll_x));
        update_selected_battle();
    }
    scroll_x_ += (scroll_target_x_ - scroll_x_) * std::min(1.0f, ctx.dt_ms * 0.008f);
    if (key_pressed(input, platform::Key::Escape)) { ctx.host.request_scene_transition(SceneId::MainMenu); return; }
    if (clicked_in(input, 10, 10, 60, 40)) { ctx.host.request_scene_transition(SceneId::MainMenu); return; }
    // FIGHT button
    if (selected_battle_ && !selected_battle_->fights.empty()) {
        float bar_h = 60.0f;
        float px = ctx.platform.window_width() * 0.72f, py = bar_h + 40.0f, pw = ctx.platform.window_width() * 0.26f;
        float panel_h = ctx.platform.window_height() - bar_h - 100.0f;
        float fbx = px + 8, fby = py + panel_h - 65.0f, fbw = pw - 16, fbh = 60.0f;
        if (clicked_in(input, fbx, fby, fbw, fbh)) {
            ctx.host.host_set_current_level(selected_zone_name_ + "/" + selected_battle_->name);
            ctx.host.host_set_battle_location(selected_battle_->location);
            ctx.host.host_set_dialogue({{"Sly", selected_battle_->name},{"Narrator","Location: "+selected_battle_->location}});
            ctx.host.request_scene_transition(SceneId::Dialogue); return;
        }
    }
    // Click on zone panels (horizontal layout)
    float x0 = 50.0f, zone_w = 220.0f;
    for (size_t zi = 0; zi < zone_battles_.size(); ++zi) {
        float zx = x0 + zi * 240.0f - scroll_x_;
        if (zx < -220.0f || zx > ctx.platform.window_width() + 20.0f) continue;
        if (clicked_in(input, zx, 100, zone_w, 40)) { selected_ = (int)zi; update_selected_battle(); }
        auto& z = zone_battles_[zi];
        for (size_t bi = 0; bi < z.battle_indices.size(); ++bi) {
            float by = 160.0f + bi * 50.0f;
            if (clicked_in(input, zx + 10, by, zone_w - 20, 45)) {
                selected_ = (int)zi;
                selected_battle_ = &z.zone.battles[z.battle_indices[bi]];
                selected_zone_name_ = z.zone.name;
                if (!selected_battle_->fights.empty()) { reward_money_ = selected_battle_->fights[0].reward.money; reward_exp_ = selected_battle_->fights[0].reward.exp; fight_power_ = selected_battle_->fights[0].power; }
            }
        }
    }
}

void MapScene::on_render(SceneContext& ctx) {
    auto& r = ctx.renderer; float w = (float)ctx.platform.window_width(), h = (float)ctx.platform.window_height();
    float bar_h = 60.0f;
    r.draw_filled_rect_screen(0, 0, w, bar_h, {20, 20, 40, 230});
    r.draw_filled_rect_screen(10, 10, 60, bar_h - 20, {50, 50, 70, 200});
    ctx.host.host_render_text("< BACK", 20, 18, 0.35f, 220, 220, 240, 255);
    r.draw_filled_rect_screen(80, 10, w - 160, bar_h - 20, {30, 30, 50, 180});
    ctx.host.host_render_text("MAP", w / 2 - 40, 15, 0.45f, 200, 200, 220, 255);
    if (zone_battles_.empty()) return;
    size_t zi = (size_t)std::max(0, std::min(selected_, (int)zone_battles_.size() - 1));
    auto& z = zone_battles_[zi];
    int zone_num = 0;
    if (z.zone.name.find("ZONE_") != std::string::npos)
        zone_num = std::atoi(z.zone.name.c_str() + z.zone.name.find("ZONE_") + 5);
    bool textured = false;
    if (zone_num >= 1 && zone_num <= 7)
        textured = ctx.host.host_render_zone_bg(zone_num, 0, bar_h, w, h - bar_h);
    if (!textured) {
        r.draw_filled_rect_screen(0, bar_h, w, h - bar_h, {10, 10, 25, 255});
        ctx.host.host_render_text(z.zone.name, w/2 - 60, h/2 - 20, 0.4f, 180, 180, 200, 255);
    }
    for (size_t bi = 0; bi < z.battle_indices.size(); ++bi) {
        size_t idx = z.battle_indices[bi];
        const auto& battle = z.zone.battles[idx];
        bool completed = ctx.host.host_is_level_completed(z.zone.name + "/" + battle.name);
        float nx = (battle.x + 500.0f) / 1000.0f;
        float ny = (battle.y + 300.0f) / 600.0f;
        nx = std::max(0.05f, std::min(0.95f, nx));
        ny = std::max(0.05f, std::min(0.95f, ny));
        float bx = 80 + nx * (w - 160);
        float by = bar_h + 40 + ny * (h - bar_h - 100);
        resf2::renderer::Color4B col = {180, 180, 180, 200};
        if (battle.type == "BOSSES" || battle.type == "FINAL_BATTLE" || battle.type == "FINAL_BATTLE_TITAN") col = {220, 30, 30, 230};
        else if (battle.type == "TOURNAMENT") col = {50, 100, 220, 210};
        else if (battle.type == "CHALLENGE") col = {50, 200, 70, 210};
        else if (battle.type == "SURVIVAL") col = {210, 170, 30, 210};
        if (completed) col = {50, 210, 50, 220};
        float radius = (battle.type == "BOSSES" || battle.type == "FINAL_BATTLE" || battle.type == "FINAL_BATTLE_TITAN") ? 16.0f : 11.0f;
        if (selected_battle_ == &battle) radius += 3;
        r.draw_filled_circle_screen(bx + 2, by + 2, radius, {0, 0, 0, 100});
        r.draw_filled_circle_screen(bx, by, radius, col);
        ctx.host.host_render_text(battle.name, bx + radius + 5, by - 7, 0.24f, 200, 200, 220, 220);
    }
    if (selected_battle_) {
        float px = w * 0.74f, py = bar_h + 20.0f, pw = w * 0.24f, panel_h = h - bar_h - 80.0f;
        r.draw_filled_rect_screen(px - 4, py - 4, pw + 8, panel_h + 8, {0, 0, 0, 80});
        r.draw_filled_rect_screen(px, py, pw, 2, {200, 170, 100, 200});
        r.draw_filled_rect_screen(px + 4, py + 10, pw - 8, 35, {50, 50, 70, 220});
        ctx.host.host_render_text(selected_battle_->name, px + 10, py + 18, 0.28f, 220, 220, 240, 255);
        float ry = py + 55.0f;
        r.draw_filled_rect_screen(px + 4, ry, pw - 8, 80, {25, 25, 40, 220});
        ctx.host.host_render_text("+" + std::to_string(reward_money_), px + 60, ry + 10, 0.28f, 255, 220, 100, 255);
        ctx.host.host_render_text("+" + std::to_string(reward_exp_), px + 60, ry + 45, 0.28f, 140, 190, 255, 255);
        float pow_y = ry + 95.0f;
        r.draw_filled_rect_screen(px + 4, pow_y, pw - 8, 35, (fight_power_ > 0) ? resf2::renderer::Color4B{160, 50, 20, 220} : resf2::renderer::Color4B{40, 40, 45, 200});
        if (fight_power_ > 0) ctx.host.host_render_text("POWER " + std::to_string(fight_power_), px + 10, pow_y + 8, 0.25f, 255, 200, 140, 255);
        float fbx = px + 4, fby = py + panel_h - 60.0f, fbw = pw - 8, fbh = 55.0f;
        r.draw_filled_rect_screen(fbx, fby, fbw, fbh, {160, 30, 20, 240});
        r.draw_filled_rect_screen(fbx + 3, fby + 3, fbw - 6, fbh - 6, {200, 50, 30, 210});
        ctx.host.host_render_text("FIGHT", fbx + fbw/2 - 25, fby + 18, 0.32f, 255, 255, 255, 255);
    }
    if (zi > 0) {
        float ax = 80.0f, ay = h / 2.0f - 15.0f;
        r.draw_filled_rect_screen(ax, ay, 30, 30, {50, 50, 70, 180});
        ctx.host.host_render_text("<", ax + 8, ay + 5, 0.30f, 220, 220, 200, 255);
    }
    if (zi + 1 < zone_battles_.size()) {
        float ax = w - 110.0f, ay = h / 2.0f - 15.0f;
        r.draw_filled_rect_screen(ax, ay, 30, 30, {50, 50, 70, 180});
        ctx.host.host_render_text(">", ax + 8, ay + 5, 0.30f, 220, 220, 200, 255);
    }
    ctx.host.host_render_text(z.zone.name, w/2 - 60, bar_h + 8, 0.30f, 200, 200, 220, 200);
}

// ============================================================
// DialogueScene
// ============================================================

void DialogueScene::on_enter(SceneContext& ctx) {
    std::printf("[dialogue] enter\n");
    ctx.host.host_reset_menu_state();
    current_line_ = 0;
    text_reveal_ms_ = 0;
}

void DialogueScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    const auto& lines = ctx.host.host_get_dialogue();
    size_t total_lines = lines.size();

    // Advance text reveal
    text_reveal_ms_ += ctx.dt_ms;

    auto advance = [&]() {
        current_line_++;
        text_reveal_ms_ = 0;
        if (current_line_ >= total_lines) {
            ctx.host.request_scene_transition(SceneId::Battle);
        }
    };

    // Click / Space / Enter: advance to next line
    if (key_pressed(input, platform::Key::Space) ||
        key_pressed(input, platform::Key::Enter)) {
        advance();
    }

    // Esc: skip dialogue -> back to MainMenu
    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
    }

    // Click anywhere to advance
    for (const auto& p : input.pointers) {
        if (p.just_pressed) { advance(); break; }
    }
}

void DialogueScene::on_render(SceneContext& ctx) {
    const auto& lines = ctx.host.host_get_dialogue();
    auto& r = ctx.renderer;
    float w = (float)ctx.platform.window_width(), h = (float)ctx.platform.window_height();

    // Dim background
    r.draw_filled_rect_screen(0, 0, w, h, {0, 0, 0, 160});

    if (current_line_ >= lines.size()) return;

    // Dialogue box (centered bottom)
    float box_w = w * 0.7f, box_h = 180.0f;
    float box_x = (w - box_w) / 2.0f, box_y = h - box_h - 40.0f;
    r.draw_filled_rect_screen(box_x, box_y, box_w, box_h, {20, 20, 40, 230});
    r.draw_filled_rect_screen(box_x, box_y, box_w, 2, {200, 170, 100, 255});
    r.draw_filled_rect_screen(box_x, box_y + box_h - 2, box_w, 2, {200, 170, 100, 255});
    r.draw_filled_rect_screen(box_x, box_y, 2, box_h, {200, 170, 100, 255});
    r.draw_filled_rect_screen(box_x + box_w - 2, box_y, 2, box_h, {200, 170, 100, 255});

    // Speaker name
    ctx.host.host_render_text(lines[current_line_].first,
        box_x + 20, box_y + 15, 0.35f, 255, 220, 150, 255);

    // Text (with typewriter effect)
    const std::string& full_text = lines[current_line_].second;
    size_t chars_visible = text_reveal_ms_ / kCharRevealMs;
    std::string visible = full_text.substr(0, std::min(chars_visible, full_text.size()));
    ctx.host.host_render_text(visible, box_x + 20, box_y + 60, 0.30f, 220, 220, 240, 255);

    // "Click to continue" hint
    if (chars_visible >= full_text.size()) {
        ctx.host.host_render_text("Click to continue...",
            box_x + box_w - 220, box_y + box_h - 35, 0.22f, 150, 150, 170, 180);
    }
}

// ============================================================
// BattleScene
// ============================================================

void BattleScene::on_enter(SceneContext& ctx) {
    std::printf("[battle] enter\n");
    battle_timer_ms_ = 0;
    guard_timer_ms_ = 0;
    ctx.host.host_reset_menu_state();
    ctx.host.host_set_battle_mode(true);
    ctx.host.host_set_show_enemy(true);
    // Load the actual battle location (from Map selection) instead of dojo
    std::string loc = ctx.host.host_get_battle_location();
    if (!loc.empty() && loc != "dojo") {
        std::printf("[battle] loading battle location: %s\n", loc.c_str());
        ctx.host.host_load_battle_location(loc);
    } else if (!ctx.host.host_location_loaded()) {
        ctx.host.host_load_location();
    }
    // Start battle music
    ctx.host.host_start_battle_music();
}

void BattleScene::on_update(SceneContext& ctx) {
    battle_timer_ms_ += ctx.dt_ms;
    guard_timer_ms_ += ctx.dt_ms;

    ctx.host.host_update_gameplay(ctx.dt_ms);

    const auto& input = ctx.platform.input();

    // Guard: prevent immediate transitions for the first 500ms.
    // This prevents accidental key carryover from the dialogue scene
    // (e.g., Space/Enter key being detected as a new press in Battle).
    if (guard_timer_ms_ < kGuardMs) return;

    if (key_pressed(input, platform::Key::Y)) {
        std::printf("[battle] victory!\n");
        ctx.host.host_set_battle_result("victory");
        ctx.host.request_scene_transition(SceneId::Results);
        return;
    }
    if (key_pressed(input, platform::Key::L)) {
        std::printf("[battle] defeat!\n");
        ctx.host.host_set_battle_result("defeat");
        ctx.host.request_scene_transition(SceneId::Results);
        return;
    }
    if (battle_timer_ms_ >= kBattleMaxMs) {
        std::printf("[battle] timeout -> results\n");
        ctx.host.request_scene_transition(SceneId::Results);
    }
}

void BattleScene::on_render(SceneContext& ctx) {
    // Host renders the dojo + character + bag + HUD
    ctx.host.host_render_scene();

    // Debug: show "BATTLE" so user knows they're in Battle scene
    ctx.host.host_render_text("BATTLE",
        ctx.platform.window_width() / 2.0f - 40,
        ctx.platform.window_height() - 100.0f,
        0.4f, 220, 60, 40, 200);
    ctx.host.host_render_text("[Y] Win  [L] Lose  [Esc] Forfeit",
        20, ctx.platform.window_height() - 55.0f,
        0.22f, 180, 180, 180, 180);
}

void BattleScene::on_exit(SceneContext& ctx) {
    std::printf("[battle] exit\n");
    ctx.host.host_stop_music();
}

bool BattleScene::on_quit_request(SceneContext& ctx) {
    // From battle, Esc goes to Results (forfeit) rather than quitting the app
    ctx.host.host_set_battle_result("defeat");
    ctx.host.request_scene_transition(SceneId::Results);
    return false;
}

// ============================================================
// ResultsScene
// ============================================================

void ResultsScene::on_enter(SceneContext& ctx) {
    std::printf("[results] enter\n");
    ctx.host.host_reset_menu_state();
    guard_ms_ = 0;
    saved_ = false;

    // Play result sound
    auto result = ctx.host.host_get_battle_result();
    ctx.host.host_play_result_sound(result);
    is_victory_ = (result == "victory");
    reward_gold_ = is_victory_ ? 50 : 10;
    reward_xp_ = is_victory_ ? 100 : 20;

    if (is_victory_) {
        std::printf("[results] victory! rewards: %d gold, %d XP\n", reward_gold_, reward_xp_);
        // Add currency reward
        ctx.host.host_add_currency(reward_gold_);
        // Mark level as completed
        std::string level = ctx.host.host_get_current_level();
        if (!level.empty()) {
            ctx.host.host_add_completed_level(level);
        }
        // Track win
        ctx.host.host_add_win();
    } else {
        std::printf("[results] defeat\n");
        ctx.host.host_add_loss();
    }
    ctx.host.host_save_progress();
    saved_ = true;
}

void ResultsScene::on_update(SceneContext& ctx) {
    guard_ms_ += ctx.dt_ms;
    if (guard_ms_ < kGuardMs) return;

    const auto& input = ctx.platform.input();

    // Continue button area (bottom center)
    float w = (float)ctx.platform.window_width();
    float h = (float)ctx.platform.window_height();
    float btn_w = 220.0f, btn_h = 55.0f;
    float btn_x = (w - btn_w) * 0.5f;
    float btn_y = h * 0.78f;

    if (clicked_in(input, btn_x, btn_y, btn_w, btn_h) ||
        key_pressed(input, platform::Key::Space) ||
        key_pressed(input, platform::Key::Enter)) {
        // Victory: go to Map to continue, defeat: back to MainMenu
        if (is_victory_) {
            ctx.host.request_scene_transition(SceneId::Map);
        } else {
            ctx.host.request_scene_transition(SceneId::MainMenu);
        }
        return;
    }
    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
    }
}

void ResultsScene::on_render(SceneContext& ctx) {
    auto& r = ctx.renderer;
    float w = (float)ctx.platform.window_width();
    float h = (float)ctx.platform.window_height();

    // Background
    if (is_victory_) {
        r.draw_filled_rect_screen(0, 0, w, h, {20, 50, 20, 255});
    } else {
        r.draw_filled_rect_screen(0, 0, w, h, {50, 20, 20, 255});
    }

    // Header text
    const char* title = is_victory_ ? "VICTORY" : "DEFEAT";
    uint8_t title_r = is_victory_ ? 100 : 255;
    uint8_t title_g = is_victory_ ? 255 : 60;
    uint8_t title_b = is_victory_ ? 60 : 40;
    ctx.host.host_render_text(title, w * 0.5f - 80, h * 0.12f, 0.7f, title_r, title_g, title_b, 255);

    // Rewards panel
    float panel_w = 360.0f, panel_h = 200.0f;
    float px = (w - panel_w) * 0.5f, py = h * 0.28f;
    r.draw_filled_rect_screen(px, py, panel_w, panel_h, {25, 25, 40, 230});
    r.draw_filled_rect_screen(px, py, panel_w, 2, {200, 170, 100, 200});
    r.draw_filled_rect_screen(px, py + panel_h - 2, panel_w, 2, {200, 170, 100, 200});

    ctx.host.host_render_text("Rewards", px + 120, py + 15, 0.35f, 220, 220, 240, 255);

    // Gold reward
    ctx.host.host_render_text("Gold:", px + 40, py + 70, 0.28f, 200, 200, 220, 255);
    ctx.host.host_render_text("+" + std::to_string(reward_gold_), px + 180, py + 70, 0.28f, 255, 220, 100, 255);

    // XP reward
    ctx.host.host_render_text("XP:", px + 40, py + 115, 0.28f, 200, 200, 220, 255);
    ctx.host.host_render_text("+" + std::to_string(reward_xp_), px + 180, py + 115, 0.28f, 140, 190, 255, 255);

    // Continue button
    float btn_w = 220.0f, btn_h = 55.0f;
    float btn_x = (w - btn_w) * 0.5f, btn_y = h * 0.78f;
    r.draw_filled_rect_screen(btn_x, btn_y, btn_w, btn_h, {60, 50, 40, 240});
    r.draw_filled_rect_screen(btn_x + 3, btn_y + 3, btn_w - 6, btn_h - 6, {90, 70, 50, 210});
    ctx.host.host_render_text(is_victory_ ? "CONTINUE" : "BACK TO MENU",
        btn_x + btn_w * 0.5f - 60, btn_y + 17, 0.28f, 255, 255, 255, 255);
}

// ============================================================
// ProfileScene
// ============================================================

void ProfileScene::on_enter(SceneContext& ctx) {
    std::printf("[profile] enter\n");
    ctx.renderer.set_clear_color(0.03f, 0.03f, 0.06f, 1.0f);
}

void ProfileScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    // Back button (top-left)
    if (clicked_in(input, 10, 10, 80, 40) ||
        key_pressed(input, platform::Key::Escape) ||
        key_pressed(input, platform::Key::M)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
    }
}

void ProfileScene::on_render(SceneContext& ctx) {
    auto& r = ctx.renderer;
    float w = (float)ctx.platform.window_width();
    float h = (float)ctx.platform.window_height();
    float bar_h = 60.0f;

    // Top bar
    r.draw_filled_rect_screen(0, 0, w, bar_h, {20, 20, 40, 230});
    r.draw_filled_rect_screen(10, 10, 80, bar_h - 20, {50, 50, 70, 200});
    ctx.host.host_render_text("< BACK", 20, 18, 0.32f, 220, 220, 240, 255);
    r.draw_filled_rect_screen(100, 10, w - 160, bar_h - 20, {30, 30, 50, 180});
    ctx.host.host_render_text("PROFILE", w * 0.5f - 50, 15, 0.40f, 200, 200, 220, 255);

    // Player stats panel
    int level = ctx.host.host_get_player_level();
    int wins = ctx.host.host_get_wins();
    int losses = ctx.host.host_get_losses();
    int currency = ctx.host.host_get_currency();

    float panel_x = w * 0.1f, panel_y = bar_h + 30.0f;
    float panel_w = w * 0.8f, panel_h = h * 0.5f;

    r.draw_filled_rect_screen(panel_x, panel_y, panel_w, panel_h, {25, 25, 40, 220});
    r.draw_filled_rect_screen(panel_x, panel_y, panel_w, 2, {200, 170, 100, 200});
    r.draw_filled_rect_screen(panel_x, panel_y + panel_h - 2, panel_w, 2, {200, 170, 100, 200});

    ctx.host.host_render_text("Player Stats", panel_x + panel_w * 0.5f - 60, panel_y + 15, 0.35f, 220, 220, 240, 255);

    float sy = panel_y + 60.0f;
    float line_h = 35.0f;

    ctx.host.host_render_text("Level", panel_x + 40, sy, 0.28f, 200, 200, 220, 255);
    ctx.host.host_render_text(std::to_string(level), panel_x + panel_w - 100, sy, 0.28f, 255, 220, 100, 255);
    sy += line_h;

    ctx.host.host_render_text("Gold", panel_x + 40, sy, 0.28f, 200, 200, 220, 255);
    ctx.host.host_render_text(std::to_string(currency), panel_x + panel_w - 100, sy, 0.28f, 255, 220, 100, 255);
    sy += line_h;

    ctx.host.host_render_text("Wins", panel_x + 40, sy, 0.28f, 200, 200, 220, 255);
    ctx.host.host_render_text(std::to_string(wins), panel_x + panel_w - 100, sy, 0.28f, 100, 255, 100, 255);
    sy += line_h;

    ctx.host.host_render_text("Losses", panel_x + 40, sy, 0.28f, 200, 200, 220, 255);
    ctx.host.host_render_text(std::to_string(losses), panel_x + panel_w - 100, sy, 0.28f, 255, 100, 100, 255);
    sy += line_h;

    int total = wins + losses;
    if (total > 0) {
        int win_rate = (wins * 100) / total;
        ctx.host.host_render_text("Win Rate", panel_x + 40, sy, 0.28f, 200, 200, 220, 255);
        ctx.host.host_render_text(std::to_string(win_rate) + "%", panel_x + panel_w - 100, sy, 0.28f, 140, 190, 255, 255);
    }
}

// ============================================================
// ShopScene
// ============================================================

void ShopScene::on_enter(SceneContext& ctx) {
    std::printf("[shop] enter\n");
    ctx.renderer.set_clear_color(0.04f, 0.04f, 0.07f, 1.0f);
    // Load items from list.xml via host
    auto* list_data = ctx.host.host_get_list_data();
    if (list_data && !list_data->items.empty()) {
        std::printf("[shop] loaded %zu items from catalog\n", list_data->items.size());
    }
}

void ShopScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    // Back button (top-left) or Esc
    if (clicked_in(input, 10, 10, 80, 40) ||
        key_pressed(input, platform::Key::Escape) ||
        key_pressed(input, platform::Key::M)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }

    auto* list_data = ctx.host.host_get_list_data();
    if (!list_data) return;

    float w = (float)ctx.platform.window_width();
    float bar_h = 60.0f;

    // Click detection for buy buttons
    int currency = ctx.host.host_get_currency();
    float bx = w - 120.0f;
    float cy = bar_h + 20.0f;
    std::string categories[] = {"Weapon", "Armor", "Helm", "Ranged", "Magic"};
    for (auto& cat : categories) {
        int cnt = 0;
        int idx = 0;
        for (const auto& entry : list_data->items) {
            if (entry.type == cat && !entry.shop_hide && !entry.hidden && entry.price > 0) {
                if (cnt == 0) {
                    cy += 35.0f; // skip category header
                }
                float iy = cy + idx * 40.0f;
                if (clicked_in(input, bx, iy, 80, 34)) {
                    std::printf("[shop] buy %s (%d gold)\n", entry.name.c_str(), entry.price);
                    if (currency >= entry.price) {
                        std::printf("[shop] purchased %s!\n", entry.name.c_str());
                        ctx.host.host_spend_currency(entry.price);
                        // Auto-save after purchase
                        ctx.host.host_save_progress();
                    } else {
                        std::printf("[shop] not enough gold!\n");
                    }
                }
                cnt++;
                idx++;
            }
        }
        if (cnt > 0) {
            cy += cnt * 40.0f + 10.0f;
        }
    }
}

void ShopScene::on_render(SceneContext& ctx) {
    auto& r = ctx.renderer;
    float w = (float)ctx.platform.window_width();
    float h = (float)ctx.platform.window_height();
    float bar_h = 60.0f;

    // Top bar
    r.draw_filled_rect_screen(0, 0, w, bar_h, {20, 20, 40, 230});
    r.draw_filled_rect_screen(10, 10, 80, bar_h - 20, {50, 50, 70, 200});
    ctx.host.host_render_text("< BACK", 20, 18, 0.32f, 220, 220, 240, 255);
    r.draw_filled_rect_screen(100, 10, w - 200, bar_h - 20, {30, 30, 50, 180});
    ctx.host.host_render_text("SHOP", w * 0.5f - 30, 15, 0.40f, 200, 200, 220, 255);

    // Gold display
    int currency = ctx.host.host_get_currency();
    std::string gold_str = "Gold: " + std::to_string(currency);
    ctx.host.host_render_text(gold_str, w - 140, 18, 0.28f, 255, 220, 100, 255);

    auto* list_data = ctx.host.host_get_list_data();
    if (!list_data || list_data->items.empty()) {
        ctx.host.host_render_text("No items available", w * 0.5f - 80, h * 0.5f, 0.30f, 150, 150, 170, 200);
        return;
    }

    // Draw items organized by type
    std::string categories[] = {"Weapon", "Armor", "Helm", "Ranged", "Magic"};
    float cy = bar_h + 20.0f;

    for (auto& cat : categories) {
        int cnt = 0;
        // Category header
        // Draw items
        int idx = 0;
        for (const auto& entry : list_data->items) {
            if (entry.type == cat && !entry.shop_hide && !entry.hidden && entry.price > 0) {
                if (cnt == 0) {
                    // Category header
                    r.draw_filled_rect_screen(20, cy, w - 40, 30, {40, 40, 55, 220});
                    ctx.host.host_render_text(cat, 30, cy + 5, 0.28f, 200, 170, 100, 255);
                    cy += 35.0f;
                }
                float iy = cy + idx * 40.0f;
                // Item row background (alternating)
                r.draw_filled_rect_screen(25, iy, w - 50, 36, (cnt % 2 == 0) ?
                    ren::Color4B{30, 30, 45, 200} : ren::Color4B{25, 25, 40, 200});
                // Item name
                ctx.host.host_render_text(entry.name, 35, iy + 7, 0.24f, 200, 200, 220, 255);
                // Item level
                if (entry.level > 0) {
                    ctx.host.host_render_text("Lv." + std::to_string(entry.level), 200, iy + 7, 0.22f, 150, 150, 170, 200);
                }
                // Price
                std::string price_str = std::to_string(entry.price) + "g";
                ctx.host.host_render_text(price_str, w - 200, iy + 7, 0.24f, 255, 220, 100, 255);
                // Buy button
                float bx = w - 115.0f;
                bool can_afford = currency >= entry.price;
                r.draw_filled_rect_screen(bx, iy + 2, 80, 32, can_afford ?
                    ren::Color4B{50, 120, 50, 220} : ren::Color4B{60, 60, 60, 200});
                ctx.host.host_render_text("BUY", bx + 20, iy + 6, 0.24f, 255, 255, 255, 255);
                cnt++;
                idx++;
            }
        }
        if (cnt > 0) {
            cy += cnt * 40.0f + 10.0f;
        }
    }
}

// ============================================================
// SettingsScene
// ============================================================

void SettingsScene::on_enter(SceneContext& ctx) {
    std::printf("[settings] enter\n");
    ctx.renderer.set_clear_color(0.03f, 0.03f, 0.06f, 1.0f);
}

void SettingsScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    float w = (float)ctx.platform.window_width();
    float h = (float)ctx.platform.window_height();

    // Back button (top-left) or Esc
    if (clicked_in(input, 10, 10, 80, 40) ||
        key_pressed(input, platform::Key::Escape) ||
        key_pressed(input, platform::Key::M)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }

    // Volume sliders (click-drag simulation)
    float panel_x = w * 0.15f, panel_w = w * 0.7f;
    float sy = h * 0.15f;

    // Master volume slider track
    float slider_x = panel_x + 120.0f, slider_w = panel_w - 140.0f;
    float slider_y = sy + 20.0f, slider_h = 30.0f;
    // In a real implementation, these would adjust audio engine volume.
    // For now, just detect clicks on the slider track.
    if (clicked_in(input, slider_x, slider_y, slider_w, slider_h)) {
        std::printf("[settings] master volume adjusted (stub)\n");
    }

    sy += 80.0f;
    float music_slider_y = sy + 20.0f;
    if (clicked_in(input, slider_x, music_slider_y, slider_w, slider_h)) {
        std::printf("[settings] music volume adjusted (stub)\n");
    }

    // Language selector
    sy += 80.0f;
    float lang_btn_w = 100.0f, lang_btn_h = 36.0f;
    float lang_x = panel_x;
    if (clicked_in(input, lang_x, sy, lang_btn_w, lang_btn_h)) {
        std::printf("[settings] language: English selected (stub)\n");
    }
    if (clicked_in(input, lang_x + 120, sy, lang_btn_w, lang_btn_h)) {
        std::printf("[settings] language: Russian selected (stub)\n");
    }
}

void SettingsScene::on_render(SceneContext& ctx) {
    auto& r = ctx.renderer;
    float w = (float)ctx.platform.window_width();
    float h = (float)ctx.platform.window_height();
    float bar_h = 60.0f;

    // Top bar
    r.draw_filled_rect_screen(0, 0, w, bar_h, {20, 20, 40, 230});
    r.draw_filled_rect_screen(10, 10, 80, bar_h - 20, {50, 50, 70, 200});
    ctx.host.host_render_text("< BACK", 20, 18, 0.32f, 220, 220, 240, 255);
    r.draw_filled_rect_screen(100, 10, w - 160, bar_h - 20, {30, 30, 50, 180});
    ctx.host.host_render_text("SETTINGS", w * 0.5f - 50, 15, 0.40f, 200, 200, 220, 255);

    float panel_x = w * 0.15f, panel_w = w * 0.7f;
    float panel_h = h * 0.7f, panel_y = bar_h + 30.0f;

    // Settings panel background
    r.draw_filled_rect_screen(panel_x, panel_y, panel_w, panel_h, {25, 25, 40, 220});
    r.draw_filled_rect_screen(panel_x, panel_y, panel_w, 2, {200, 170, 100, 200});
    r.draw_filled_rect_screen(panel_x, panel_y + panel_h - 2, panel_w, 2, {200, 170, 100, 200});

    float sy = panel_y + 30.0f;

    // Master volume
    ctx.host.host_render_text("Master Volume", panel_x + 20, sy, 0.28f, 200, 200, 220, 255);
    float slider_x = panel_x + 140.0f, slider_w = panel_w - 160.0f;
    r.draw_filled_rect_screen(slider_x, sy + 20, slider_w, 30, {50, 50, 60, 200});
    // Volume bar fill (75% default)
    r.draw_filled_rect_screen(slider_x + 2, sy + 22, slider_w * 0.75f - 4.0f, 26, {100, 180, 100, 200});

    sy += 80.0f;

    // Music volume
    ctx.host.host_render_text("Music Volume", panel_x + 20, sy, 0.28f, 200, 200, 220, 255);
    r.draw_filled_rect_screen(slider_x, sy + 20, slider_w, 30, {50, 50, 60, 200});
    // Volume bar fill (50% default)
    r.draw_filled_rect_screen(slider_x + 2, sy + 22, slider_w * 0.5f - 4.0f, 26, {100, 150, 180, 200});

    sy += 80.0f;

    // Language
    ctx.host.host_render_text("Language", panel_x + 20, sy, 0.28f, 200, 200, 220, 255);
    sy += 30.0f;
    // English button
    r.draw_filled_rect_screen(panel_x + 20, sy, 100, 36, {60, 60, 80, 220});
    ctx.host.host_render_text("English", panel_x + 30, sy + 7, 0.24f, 220, 220, 240, 255);
    // Russian button
    r.draw_filled_rect_screen(panel_x + 140, sy, 100, 36, {40, 40, 55, 200});
    ctx.host.host_render_text("Russian", panel_x + 152, sy + 7, 0.24f, 180, 180, 200, 200);

    sy += 70.0f;

    // Controls info
    r.draw_filled_rect_screen(panel_x, panel_y + panel_h - 170.0f, panel_w, 170.0f, {20, 20, 35, 220});
    ctx.host.host_render_text("Controls", panel_x + 20, panel_y + panel_h - 158, 0.28f, 200, 170, 100, 255);
    ctx.host.host_render_text("W/A/S/D - Move", panel_x + 20, panel_y + panel_h - 125, 0.22f, 180, 180, 200, 200);
    ctx.host.host_render_text("O - Punch    P - Kick", panel_x + 20, panel_y + panel_h - 100, 0.22f, 180, 180, 200, 200);
    ctx.host.host_render_text("M - Menu     Esc - Back", panel_x + 20, panel_y + panel_h - 75, 0.22f, 180, 180, 200, 200);
    ctx.host.host_render_text("B - Toggle enemy/bag  N - Map", panel_x + 20, panel_y + panel_h - 50, 0.22f, 180, 180, 200, 200);
}

}  // namespace resf2::scene

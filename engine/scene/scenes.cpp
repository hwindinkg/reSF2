// engine/scene/scenes.cpp
//
// Concrete Scene implementations.

#include "scenes.hpp"
#include "scene_system.hpp"

#include "../renderer/renderer.hpp"
#include "../platform/platform.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace resf2::scene {

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
    // Make sure the location is loaded. If we came from Boot->Loading,
    // it's already loaded. If we came back from Battle/Map, it's also
    // loaded. But be defensive.
    if (!ctx.host.host_location_loaded()) {
        ctx.host.host_load_location();
    }
}

void MainMenuScene::on_update(SceneContext& ctx) {
    // Delegate dojo gameplay (movement, combat, animation, physics, overlays)
    // to the host. The host handles A/D, Space, K, M, T, Esc, etc.
    ctx.host.host_update_gameplay(ctx.dt_ms);

    // After the host has processed gameplay input, check for menu-item clicks
    // that trigger scene transitions. These are scene-specific.
    const auto& input = ctx.platform.input();

    // Menu items are at x=10, y=58+i*45, w=130, h=40 (approximate).
    // i=0: Story, i=1: Shop, i=2: Settings, i=3: Continue/Dialogue
    struct MenuItem { const char* name; SceneId target; float y; };
    MenuItem items[] = {
        {"Story",    SceneId::Map,      58.0f + 0 * 45.0f},
        {"Shop",     SceneId::Shop,     58.0f + 1 * 45.0f},
        {"Settings", SceneId::Settings, 58.0f + 2 * 45.0f},
        {"Test Dialog", SceneId::Dialogue, 58.0f + 3 * 45.0f},
    };
    for (const auto& item : items) {
        if (clicked_in(input, 10.0f, item.y, 130.0f, 40.0f)) {
            std::printf("[mainmenu] clicked '%s' -> %s\n",
                        item.name, scene_name(item.target));
            // Set up dialogue if going to Dialogue
            if (item.target == SceneId::Dialogue) {
                ctx.host.host_set_dialogue({
                    {"Sly", "Welcome back, fighter."},
                    {"Sly", "The tournament awaits. Are you ready?"},
                    {"Narrator", "Round 1 — Fight!"},
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

void MainMenuScene::on_exit(SceneContext&) {
    std::printf("[mainmenu] exit\n");
}

bool MainMenuScene::on_quit_request(SceneContext&) {
    // Allow quit from main menu
    return true;
}

// ============================================================
// MapScene
// ============================================================

void MapScene::on_enter(SceneContext&) {
    std::printf("[map] enter\n");
    selected_ = 0;
}

void MapScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();

    // Navigate with arrows / W,S
    if (key_pressed(input, platform::Key::ArrowDown) ||
        key_pressed(input, platform::Key::S)) {
        selected_ = (selected_ + 1) % (int)levels_.size();
    }
    if (key_pressed(input, platform::Key::ArrowUp) ||
        key_pressed(input, platform::Key::W)) {
        selected_ = (selected_ - 1 + (int)levels_.size()) % (int)levels_.size();
    }

    // Enter: select level -> go to Dialogue
    if (key_pressed(input, platform::Key::Enter) ||
        key_pressed(input, platform::Key::Space)) {
        std::printf("[map] selected: %s\n", levels_[selected_].c_str());
        ctx.host.host_set_current_level(levels_[selected_]);
        ctx.host.host_set_dialogue({
            {"Sly", "You chose: " + levels_[selected_]},
            {"Narrator", "Prepare for battle!"},
        });
        ctx.host.request_scene_transition(SceneId::Dialogue);
        return;
    }

    // Esc: back to main menu
    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }

    // Click on a level entry
    float y0 = 200.0f;
    float line_h = 50.0f;
    for (int i = 0; i < (int)levels_.size(); ++i) {
        if (clicked_in(input, 200.0f, y0 + i * line_h, 880.0f, line_h - 10.0f)) {
            selected_ = i;
            ctx.host.host_set_current_level(levels_[i]);
            ctx.host.host_set_dialogue({
                {"Sly", "You chose: " + levels_[i]},
                {"Narrator", "Prepare for battle!"},
            });
            ctx.host.request_scene_transition(SceneId::Dialogue);
            return;
        }
    }
}

void MapScene::on_render(SceneContext& ctx) {
    // Render a simple list. We use the host's text renderer via the
    // renderer's debug text drawing. For now, we draw colored rects.
    // The actual text will be rendered by the host's render_text method
    // which we invoke through a thin wrapper.
    //
    // Since we don't have direct access to the host's render_text from
    // here (it's a private method of Game), we'll just draw the UI as
    // colored quads via the renderer. The host will be updated to expose
    // a render_text method through SceneHost if needed.
    (void)ctx;
}

// ============================================================
// ShopScene
// ============================================================

void ShopScene::on_enter(SceneContext&) {
    std::printf("[shop] enter (stub)\n");
}

void ShopScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    if (key_pressed(input, platform::Key::Escape) ||
        key_pressed(input, platform::Key::M)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
    }
}

void ShopScene::on_render(SceneContext&) {
    // Stub — just a placeholder. The host could render a background here.
}

// ============================================================
// SettingsScene
// ============================================================

void SettingsScene::on_enter(SceneContext&) {
    std::printf("[settings] enter (stub)\n");
}

void SettingsScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    if (key_pressed(input, platform::Key::Escape) ||
        key_pressed(input, platform::Key::M)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
    }
}

void SettingsScene::on_render(SceneContext&) {
    // Stub
}

// ============================================================
// DialogueScene
// ============================================================

void DialogueScene::on_enter(SceneContext& ctx) {
    std::printf("[dialogue] enter\n");
    current_line_ = 0;
    text_reveal_ms_ = 0;
}

void DialogueScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();

    // Advance text reveal
    text_reveal_ms_ += ctx.dt_ms;

    // Click / Space / Enter: advance to next line (or skip typewriter)
    if (key_pressed(input, platform::Key::Space) ||
        key_pressed(input, platform::Key::Enter)) {
        // If text is still revealing, first skip to full text
        // (typewriter skip). For simplicity, we just advance.
        current_line_++;
        text_reveal_ms_ = 0;
        if (current_line_ >= 3) {  // TODO: get line count from host
            // Dialogue ended -> go to Battle
            ctx.host.request_scene_transition(SceneId::Battle);
        }
    }

    // Esc: skip dialogue -> back to MainMenu
    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
    }

    // Click anywhere to advance
    for (const auto& p : input.pointers) {
        if (p.just_pressed) {
            current_line_++;
            text_reveal_ms_ = 0;
            if (current_line_ >= 3) {
                ctx.host.request_scene_transition(SceneId::Battle);
            }
            break;
        }
    }
}

void DialogueScene::on_render(SceneContext&) {
    // The dialogue box is rendered by the host (existing dialog overlay).
    // We just drive the advancement logic here.
}

// ============================================================
// BattleScene
// ============================================================

void BattleScene::on_enter(SceneContext& ctx) {
    std::printf("[battle] enter\n");
    battle_timer_ms_ = 0;
    if (!ctx.host.host_location_loaded()) {
        ctx.host.host_load_location();
    }
}

void BattleScene::on_update(SceneContext& ctx) {
    battle_timer_ms_ += ctx.dt_ms;

    // Delegate dojo gameplay (movement, combat, animation, physics, overlays)
    // to the host. The host handles A/D, Space, K, etc.
    ctx.host.host_update_gameplay(ctx.dt_ms);

    const auto& input = ctx.platform.input();

    // Win condition: press 'Y' to declare victory (debug).
    // Loss condition: press 'L' to declare defeat (debug).
    // In the future, these will be triggered by actual game logic
    // (enemy HP reaching 0, player HP reaching 0, timer expiry).
    if (key_pressed(input, platform::Key::Y)) {
        std::printf("[battle] victory!\n");
        ctx.host.request_scene_transition(SceneId::Results);
        return;
    }
    if (key_pressed(input, platform::Key::L)) {
        std::printf("[battle] defeat!\n");
        ctx.host.request_scene_transition(SceneId::Results);
        return;
    }

    // Auto-end battle after timeout (debug)
    if (battle_timer_ms_ >= kBattleMaxMs) {
        std::printf("[battle] timeout -> results\n");
        ctx.host.request_scene_transition(SceneId::Results);
    }
}

void BattleScene::on_render(SceneContext& ctx) {
    // Host renders the dojo + character + bag + HUD
    ctx.host.host_render_scene();
}

void BattleScene::on_exit(SceneContext&) {
    std::printf("[battle] exit\n");
}

bool BattleScene::on_quit_request(SceneContext& ctx) {
    // From battle, Esc goes to Results (forfeit) rather than quitting the app
    ctx.host.request_scene_transition(SceneId::Results);
    return false;
}

// ============================================================
// ResultsScene
// ============================================================

void ResultsScene::on_enter(SceneContext& ctx) {
    std::printf("[results] enter\n");
    // Save progress on entering results
    ctx.host.host_save_progress();
}

void ResultsScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    // Space/Enter/Click: return to main menu
    if (key_pressed(input, platform::Key::Space) ||
        key_pressed(input, platform::Key::Enter)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }
    for (const auto& p : input.pointers) {
        if (p.just_pressed) {
            ctx.host.request_scene_transition(SceneId::MainMenu);
            return;
        }
    }
    // Esc: also back to menu
    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
    }
}

void ResultsScene::on_render(SceneContext&) {
    // Stub — the host could render a victory/defeat background
}

}  // namespace resf2::scene

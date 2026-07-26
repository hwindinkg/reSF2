// engine/scene/scenes.cpp
//
// Concrete Scene implementations.

#include "scenes.hpp"
#include "scene_system.hpp"

#include "../renderer/renderer.hpp"
#include "../platform/platform.hpp"
#include "../format/stage_parser.hpp"
#include "../format/list_parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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
    nodes_.clear(); selected_node_ = 0;
    // The map is a painted sheet edge to edge; the clear colour only shows if
    // the sheet is missing, so keep it parchment brown rather than navy.
    ctx.renderer.set_clear_color(0.14f, 0.10f, 0.06f, 1.0f);
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
        // --scene map:N opens straight onto zone N, so a specific zone can be
        // captured and compared against the reference screenshot.
        const int want = ctx.host.start_scene_arg();
        if (want >= 0 && want < (int)zone_battles_.size()) selected_ = want;
    }
}

// [ORIGINAL] The zone sheet a zone is painted on. stages.xml names the story
// zones ZONE_1..ZONE_7 and the tutorial zone "Punchbag"; the sheets are
// assets/1536/image/zones/1.jpg..7.jpg. The tutorial shares sheet 1 — it is
// the same corner of the world, which is why the original shows the dojo and
// the first bosses on the same parchment.
int MapScene::zone_sheet_index() const {
    if (selected_ < 0 || (size_t)selected_ >= zone_battles_.size()) return 1;
    const std::string& n = zone_battles_[selected_].zone.name;
    const auto pos = n.find("ZONE_");
    if (pos != std::string::npos) {
        const int idx = std::atoi(n.c_str() + pos + 5);
        if (idx >= 1 && idx <= 7) return idx;
    }
    return 1;
}

// [ORIGINAL] Which frame of the battle atlases a node uses. The atlases ship
// one frame per battle KIND, not per battle: base_tournament, base_survival,
// base_duel, base_training, base_challenge, base_final_battle, plus one per
// boss (base_lynx, base_samurai, base_hermit, ...). Boss battles are named
// BOSS_<NAME> in stages.xml and carry INTERMISSION / ECLIPSEMODE / REPLAYABLE
// variants of the same node, so the icon is the name with that decoration
// stripped and lower-cased.
static std::string battle_icon_name(const resf2::format::StageBattle& b) {
    std::string n = b.name;
    if (n.rfind("BOSS_", 0) == 0) n = n.substr(5);
    for (const char* suffix : {"_INTERMISSION", "_ECLIPSEMODE", "_REPLAYABLE"}) {
        const auto p = n.find(suffix);
        if (p != std::string::npos) n = n.substr(0, p);
    }
    for (auto& c : n) c = static_cast<char>(std::tolower((unsigned char)c));
    return n;
}

// Not every battle name has a frame of its own — the tutorial zone's "Bosses"
// is one. Fall back on the Type, which the atlas does cover.
static std::string battle_icon_fallback(const resf2::format::StageBattle& b) {
    const std::string& t = b.type;
    if (t == "DUMMY" || t == "TUTORIAL") return "training";
    if (t == "TOURNAMENT") return "tournament";
    if (t == "SURVIVAL") return "survival";
    if (t == "PERIODIC") return "duel";
    if (t == "CHALLENGE") return "challenge";
    if (t == "ASCENSION") return "ascension";
    if (t.rfind("FINAL_BATTLE", 0) == 0) return "final_battle";
    return "duel";
}

// The label under a node. The original localizes them: Tournament / Survival /
// Duel are keys in their own right, bosses use character<Name>. Falling back
// to the raw stages.xml name keeps an unlocalized zone readable rather than
// blank.
static std::string battle_label(SceneContext& ctx, const resf2::format::StageBattle& b) {
    std::string bare = b.name;
    if (bare.rfind("BOSS_", 0) == 0) bare = bare.substr(5);
    for (const char* suffix : {"_INTERMISSION", "_ECLIPSEMODE", "_REPLAYABLE"}) {
        const auto p = bare.find(suffix);
        if (p != std::string::npos) bare = bare.substr(0, p);
    }
    std::string pretty = bare;
    for (size_t i = 1; i < pretty.size(); ++i)
        pretty[i] = static_cast<char>(std::tolower((unsigned char)pretty[i]));

    for (const std::string& key : {bare, pretty, "character" + pretty}) {
        std::string s = ctx.host.host_localized(key);
        if (!s.empty()) return s;
    }
    return bare;
}

void MapScene::rebuild_nodes(SceneContext& ctx) {
    nodes_.clear();
    if (selected_ < 0 || (size_t)selected_ >= zone_battles_.size()) return;
    const auto& z = zone_battles_[selected_];
    for (size_t bi : z.battle_indices) {
        const auto& battle = z.zone.battles[bi];
        // A battle without map coordinates is not a node — the HIDDEN ambushes
        // and the FAKE placeholders have none.
        if (battle.x == 0.0f && battle.y == 0.0f && battle.type != "DUMMY") continue;
        Node n;
        n.battle = &battle;
        n.icon = battle_icon_name(battle);
        n.icon_fallback = battle_icon_fallback(battle);
        n.label = battle_label(ctx, battle);
        n.x = battle.x;
        n.y = battle.y;
        n.completed = ctx.host.host_is_level_completed(z.zone.name + "/" + battle.name);
        nodes_.push_back(n);
    }
    if (selected_node_ >= (int)nodes_.size()) selected_node_ = 0;
    select_node((size_t)selected_node_);
}

void MapScene::select_node(size_t i) {
    selected_battle_ = nullptr;
    reward_money_ = reward_exp_ = fight_power_ = 0;
    if (i >= nodes_.size()) return;
    selected_node_ = (int)i;
    selected_battle_ = nodes_[i].battle;
    if (selected_ >= 0 && (size_t)selected_ < zone_battles_.size())
        selected_zone_name_ = zone_battles_[selected_].zone.name;
    if (selected_battle_ && !selected_battle_->fights.empty()) {
        reward_money_ = selected_battle_->fights[0].reward.money;
        reward_exp_ = selected_battle_->fights[0].reward.exp;
        fight_power_ = selected_battle_->fights[0].power;
    }
}

void MapScene::update_selected_battle() {
    if (selected_ >= 0 && (size_t)selected_ < zone_battles_.size())
        selected_zone_name_ = zone_battles_[selected_].zone.name;
    selected_node_ = 0;
    nodes_.clear();          // rebuilt on the next render, which has ctx
    selected_battle_ = nullptr;
    reward_money_ = reward_exp_ = fight_power_ = 0;
}

// ---------------------------------------------------------------------------
// Layout. The side scroll takes the right edge, the map fills the rest under
// the top panel — the arrangement in the original's map screen.
// ---------------------------------------------------------------------------
namespace {
struct MapLayout {
    float panel_h = 0;     // top HUD strip
    float map_x = 0, map_y = 0, map_w = 0, map_h = 0;
    float scroll_x = 0, scroll_y = 0, scroll_w = 0, scroll_h = 0;
    float fight_x = 0, fight_y = 0, fight_w = 0, fight_h = 0;
};

// [ORIGINAL] Text on this screen is sized the same way the HUD numerals are
// (PORT_PLAN 6.1): from the viewport height, not from constants. The bitmap
// font's line box is ~115 px at scale 1, so `text_scale(px)` asks for a height
// in pixels and gets the scale back. Sizing by hand is how the FIGHT caption
// first came out four times too big and ran off the panel.
constexpr float kFontLineBoxPx = 115.0f;
inline float text_scale(float wanted_px) { return wanted_px / kFontLineBoxPx; }

MapLayout map_layout(float w, float h) {
    MapLayout L;
    L.panel_h = h * 0.085f;               // same rule as the HUD (PORT_PLAN 6.1)
    L.scroll_w = w * 0.235f;
    L.scroll_x = w - L.scroll_w - w * 0.012f;
    L.scroll_y = L.panel_h + h * 0.055f;
    L.scroll_h = h - L.scroll_y - h * 0.105f;
    L.map_x = 0;
    L.map_y = L.panel_h;
    L.map_w = w;
    L.map_h = h - L.panel_h;
    L.fight_w = L.scroll_w * 0.86f;
    L.fight_h = L.scroll_h * 0.13f;
    L.fight_x = L.scroll_x + (L.scroll_w - L.fight_w) * 0.5f;
    L.fight_y = L.scroll_y + L.scroll_h - L.fight_h - L.scroll_h * 0.11f;
    return L;
}
}  // namespace

void MapScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    if (zone_battles_.empty()) return;
    const float w = (float)ctx.platform.window_width();
    const float h = (float)ctx.platform.window_height();
    const MapLayout L = map_layout(w, h);

    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }

    // --- zone paging -------------------------------------------------------
    // Left/Right (and A/D) move between ZONES, which is what the row of page
    // dots along the bottom stands for. They used to step between the battle
    // nodes of one zone while zone paging sat on Up/Down, so none of the three
    // obvious ways to change zone — arrows, the dots, dragging — did anything.
    auto go_zone = [&](int idx) {
        if (idx < 0 || idx >= (int)zone_battles_.size() || idx == selected_) return;
        selected_ = idx;
        update_selected_battle();
        scroll_x_ = scroll_target_x_ = 0.0f;   // each sheet starts unscrolled
        want_centre_ = true;
    };
    if (key_pressed(input, platform::Key::ArrowRight) || key_pressed(input, platform::Key::D))
        go_zone(selected_ + 1);
    if (key_pressed(input, platform::Key::ArrowLeft) || key_pressed(input, platform::Key::A))
        go_zone(selected_ - 1);
    // Tab cycles the battle nodes inside the current zone.
    if (!nodes_.empty() && key_pressed(input, platform::Key::Tab)) {
        select_node(((size_t)selected_node_ + 1) % nodes_.size());
        want_centre_ = true;
    }

    // Clicking a page dot jumps straight to that zone.
    for (size_t i = 0; i < dot_hit_.size(); ++i) {
        const auto& d = dot_hit_[i];
        if (clicked_in(input, d[0], d[1], d[2], d[3])) { go_zone((int)i); break; }
    }
    // On-screen paging arrows at the edges of the map.
    if (clicked_in(input, L.map_x, L.map_y + L.map_h * 0.40f,
                   L.map_w * 0.06f, L.map_h * 0.20f))
        go_zone(selected_ - 1);
    if (clicked_in(input, L.scroll_x - L.map_w * 0.06f, L.map_y + L.map_h * 0.40f,
                   L.map_w * 0.06f, L.map_h * 0.20f))
        go_zone(selected_ + 1);

    // --- dragging the sheet ------------------------------------------------
    // Press anywhere on the map and drag to pan it. A drag that never moves
    // more than a few pixels is a tap and is left to the node hit test below.
    for (const auto& p : input.pointers) {
        if (p.id < 0) continue;
        const bool on_map = p.x < L.scroll_x && p.y > L.map_y;
        if (p.just_pressed && on_map) {
            drag_active_ = true;
            drag_moved_ = 0.0f;
            drag_last_x_ = p.x;
        } else if (p.pressed && drag_active_) {
            const float dx = p.x - drag_last_x_;
            drag_last_x_ = p.x;
            drag_moved_ += std::fabs(dx);
            scroll_target_x_ = std::max(0.0f, std::min(max_scroll_, scroll_target_x_ - dx));
            scroll_x_ = scroll_target_x_;
        } else if (p.just_released) {
            drag_active_ = false;
        }
    }

    // Panning the sheet to keep the selection visible needs the map transform,
    // which only on_render has. Raise a flag; on_render solves for the scroll.
    scroll_x_ += (scroll_target_x_ - scroll_x_) * std::min(1.0f, ctx.dt_ms * 0.008f);

    // FIGHT
    if (selected_battle_ && !selected_battle_->fights.empty() &&
        clicked_in(input, L.fight_x, L.fight_y, L.fight_w, L.fight_h)) {
        ctx.host.host_set_current_level(selected_zone_name_ + "/" + selected_battle_->name);
        ctx.host.host_set_battle_location(selected_battle_->location);
        ctx.host.host_set_dialogue({{"Sly", selected_battle_->name},
                                    {"Narrator", "Location: " + selected_battle_->location}});
        ctx.host.request_scene_transition(SceneId::Dialogue);
        return;
    }
    // MENU scroll, top left — same box the dojo uses.
    if (clicked_in(input, w * 0.012f, L.panel_h, w * 0.14f, L.panel_h * 0.62f)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }
    // Clicking a node selects it. Hit boxes are computed in on_render and
    // cached here, so a click on the frame after the first render is accurate.
    if (drag_moved_ < 6.0f) {
        for (size_t i = 0; i < node_hit_.size() && i < nodes_.size(); ++i) {
            const auto& hb = node_hit_[i];
            if (clicked_in(input, hb[0], hb[1], hb[2], hb[3])) { select_node(i); break; }
        }
    }
}

void MapScene::on_render(SceneContext& ctx) {
    auto& r = ctx.renderer;
    const float w = (float)ctx.platform.window_width();
    const float h = (float)ctx.platform.window_height();
    const MapLayout L = map_layout(w, h);

    if (zone_battles_.empty()) {
        r.draw_filled_rect_screen(0, 0, w, h, {24, 18, 12, 255});
        ctx.host.host_render_text("no stages.xml", w * 0.4f, h * 0.5f, 0.3f,
                                  200, 200, 200, 255);
        return;
    }
    if (nodes_.empty()) rebuild_nodes(ctx);

    // --- the painted zone sheet -------------------------------------------
    const auto view = ctx.host.host_render_zone_map(zone_sheet_index(), scroll_x_,
                                                    L.map_x, L.map_y, L.map_w, L.map_h);
    max_scroll_ = view.max_scroll;
    if (!view.ok) {
        r.draw_filled_rect_screen(L.map_x, L.map_y, L.map_w, L.map_h, {40, 32, 22, 255});
    }

    // Centre the selection when it changed. The sheet's centre sits at
    // `map_x - scroll + draw_w/2`, so a node at map x lands on screen at
    // `centre_x + x*scale`; solving for the scroll that puts it in the middle
    // of the visible map area (left of the side scroll) gives:
    if (want_centre_ && view.ok && selected_node_ < (int)nodes_.size()) {
        const float sheet_centre_no_scroll = view.centre_x + scroll_x_;
        const float node_no_scroll = sheet_centre_no_scroll +
                                     nodes_[(size_t)selected_node_].x * view.scale;
        scroll_target_x_ = std::max(0.0f, std::min(view.max_scroll,
                                                   node_no_scroll - L.scroll_x * 0.5f));
        want_centre_ = false;
    }

    // --- battle nodes ------------------------------------------------------
    //
    // stages.xml coordinates are measured from the centre of the sheet, with Y
    // pointing UP: in ZONE_1 the tournament (Y=+10) sits above the Lynx node
    // (Y=-45), which is the arrangement on the reference screenshot. Reading Y
    // the other way mirrors the whole map vertically.
    const float icon_size = L.map_h * 0.16f;
    node_hit_.assign(nodes_.size(), {0.0f, 0.0f, 0.0f, 0.0f});
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const auto& n = nodes_[i];
        const float cx = view.ok ? view.centre_x + n.x * view.scale
                                 : L.map_x + L.map_w * 0.5f + n.x;
        const float cy = view.ok ? view.centre_y - n.y * view.scale
                                 : L.map_y + L.map_h * 0.5f - n.y;
        if (cx < L.map_x - icon_size || cx > L.scroll_x + icon_size) continue;

        const bool sel = ((int)i == selected_node_);
        const int state = sel ? 1 : 0;
        if (!ctx.host.host_render_battle_icon(n.icon, state, cx, cy, icon_size) &&
            !ctx.host.host_render_battle_icon(n.icon_fallback, state, cx, cy, icon_size)) {
            // No frame for this kind: a plain marker beats an invisible node.
            r.draw_filled_circle_screen(cx, cy, icon_size * 0.3f,
                                        sel ? resf2::renderer::Color4B{235, 195, 110, 235}
                                            : resf2::renderer::Color4B{150, 120, 80, 210});
        }
        node_hit_[i] = {cx - icon_size * 0.5f, cy - icon_size * 0.5f, icon_size, icon_size};

        // Label under the node, centred, in the map's ink colour.
        const float ls = text_scale(h * 0.030f);
        const auto [tw, th] = ctx.host.host_measure_text(n.label, ls);
        (void)th;
        ctx.host.host_render_text(n.label, cx - tw * 0.5f, cy + icon_size * 0.46f,
                                  ls, 60, 42, 26, 255);
    }

    // --- side scroll -------------------------------------------------------
    if (selected_battle_) {
        ctx.host.host_render_scroll_panel(L.scroll_x, L.scroll_y, L.scroll_w, L.scroll_h);
        const float pad = L.scroll_w * 0.085f;
        const float inner_x = L.scroll_x + pad;
        const float inner_w = L.scroll_w - 2 * pad;
        float y = L.scroll_y + L.scroll_h * 0.045f;

        // Title
        const float title_scale = text_scale(h * 0.040f);
        const std::string title = nodes_.empty() ? selected_battle_->name
                                                 : nodes_[(size_t)selected_node_].label;
        const auto [tw, th] = ctx.host.host_measure_text(title, title_scale);
        ctx.host.host_render_text(title, L.scroll_x + (L.scroll_w - tw) * 0.5f, y,
                                  title_scale, 92, 46, 20, 255);
        y += th * 1.5f;

        // Location photo
        const float shot_h = inner_w * (274.0f / 484.0f);   // the jpgs are 484x274
        // The photo is named after the opponent where there is one
        // (lynx.jpg for the moon fight) and after the location otherwise.
        const std::string& icon_key = nodes_.empty() ? selected_battle_->location
                                                     : nodes_[(size_t)selected_node_].icon;
        if (ctx.host.host_render_battle_preview(icon_key, inner_x, y, inner_w, shot_h) ||
            ctx.host.host_render_battle_preview(selected_battle_->location,
                                                inner_x, y, inner_w, shot_h)) {
            y += shot_h + L.scroll_h * 0.035f;
        }

        const float info_scale = text_scale(h * 0.032f);
        char buf[128];
        auto centred = [&](const std::string& t, float scale, int r8, int g8, int b8) {
            const auto [tw2, th2] = ctx.host.host_measure_text(t, scale);
            ctx.host.host_render_text(t, L.scroll_x + (L.scroll_w - tw2) * 0.5f, y,
                                      scale, (std::uint8_t)r8, (std::uint8_t)g8,
                                      (std::uint8_t)b8, 255);
            y += th2 * 1.35f;
        };

        // [ORIGINAL] "Stage n/N" over a row of round markers, one per fight of
        // this battle. localization key `stage` is "Стадия {0}/{1}".
        {
            const int total = (int)selected_battle_->fights.size();
            const int done = std::min(total, 1);   // 7.3 will drive this from progress
            std::string tpl = ctx.host.host_localized("stage");
            if (tpl.empty()) tpl = "Stage {0}/{1}";
            const std::string a = std::to_string(done), b2 = std::to_string(total);
            for (auto& [tok, val] : {std::pair<std::string, std::string>{"{0}", a},
                                     std::pair<std::string, std::string>{"{1}", b2}}) {
                const auto p2 = tpl.find(tok);
                if (p2 != std::string::npos) tpl.replace(p2, tok.size(), val);
            }
            centred(tpl, info_scale, 92, 46, 20);

            // Round markers. Round_Done / Round_Undone are shipped in
            // textures/misc; a filled square stands in until they are loaded.
            const int per_row = 8;
            const float m = inner_w / (float)per_row * 0.8f;
            const float gap = inner_w / (float)per_row * 0.2f;
            const int rows = (total + per_row - 1) / per_row;
            for (int rI = 0; rI < rows; ++rI) {
                const int in_row = std::min(per_row, total - rI * per_row);
                const float row_w = in_row * m + (in_row - 1) * gap;
                float mx = L.scroll_x + (L.scroll_w - row_w) * 0.5f;
                for (int c = 0; c < in_row; ++c) {
                    const bool filled = (rI * per_row + c) < done;
                    r.draw_filled_rect_screen(mx, y, m, m,
                        filled ? resf2::renderer::Color4B{214, 106, 34, 255}
                               : resf2::renderer::Color4B{74, 54, 36, 255});
                    mx += m + gap;
                }
                y += m + gap;
            }
            y += L.scroll_h * 0.02f;
        }

        // Entry cost / reward.
        if (reward_money_ > 0) {
            std::snprintf(buf, sizeof(buf), "%d", reward_money_);
            centred(buf, info_scale, 92, 46, 20);
        }
        if (fight_power_ > 0) {
            std::snprintf(buf, sizeof(buf), "%d", fight_power_);
            centred(buf, info_scale, 140, 60, 30);
        }

        // FIGHT button, on its own little scroll.
        ctx.host.host_render_scroll_panel(L.fight_x, L.fight_y, L.fight_w, L.fight_h);
        std::string fight = ctx.host.host_localized("startFight");
        if (fight.empty()) fight = "FIGHT!";
        const float fs = text_scale(L.fight_h * 0.42f);
        const auto [fw2, fh2] = ctx.host.host_measure_text(fight, fs);
        ctx.host.host_render_text(fight,
                                  L.fight_x + (L.fight_w - fw2) * 0.5f,
                                  L.fight_y + (L.fight_h - fh2) * 0.5f,
                                  fs, 92, 46, 20, 255);
    }

    // --- top panel and MENU scroll, exactly as in the dojo ------------------
    ctx.host.host_render_top_panel();

    // --- zone caption along the bottom -------------------------------------
    {
        const std::string zone_key = zone_battles_[(size_t)std::max(0, selected_)].zone.name;
        std::string caption = ctx.host.host_localized(zone_key);
        if (caption.empty()) caption = zone_key;
        const float cs = text_scale(h * 0.042f);
        const auto [cw, ch] = ctx.host.host_measure_text(caption, cs);
        const float band_h = h * 0.135f;
        r.draw_filled_rect_screen(0, h - band_h, L.scroll_x, band_h, {26, 16, 8, 225});
        ctx.host.host_render_text(caption, (L.scroll_x - cw) * 0.5f,
                                  h - band_h + band_h * 0.12f, cs,
                                  222, 198, 150, 255);
        // [ORIGINAL] A dot per zone under the title — the map's page indicator,
        // and the only thing on screen that says how many zones there are.
        const int zones = (int)zone_battles_.size();
        const float dot = h * 0.016f;
        const float dgap = dot * 1.4f;
        const float total_w = zones * dot + (zones - 1) * dgap;
        float dx = (L.scroll_x - total_w) * 0.5f;
        const float dy = h - band_h * 0.30f;
        // Hit boxes are padded well past the dot itself — a 12 px disc is not
        // something anyone can hit reliably, and the dots are the map's only
        // visible way to jump between zones.
        dot_hit_.assign((size_t)zones, {0.0f, 0.0f, 0.0f, 0.0f});
        const float pad = dot * 1.6f;
        for (int i = 0; i < zones; ++i) {
            const bool cur = (i == selected_);
            r.draw_filled_circle_screen(dx + dot * 0.5f, dy, dot * 0.5f,
                cur ? resf2::renderer::Color4B{214, 106, 34, 255}
                    : resf2::renderer::Color4B{92, 68, 44, 255});
            dot_hit_[(size_t)i] = {dx - pad * 0.5f, dy - dot * 0.5f - pad * 0.5f,
                                   dot + pad, dot + pad};
            dx += dot + dgap;
        }
        (void)ch;
    }
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
            // The opening tutorial dialogue has no battle behind it — it just
            // hands over to the dojo. Only a dialogue queued from the map does.
            ctx.host.request_scene_transition(
                ctx.host.host_get_battle_location().empty() ? SceneId::MainMenu
                                                            : SceneId::Battle);
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

// [ORIGINAL] A story dialogue is a parchment scroll across the bottom of the
// screen with the speaker's avatar on the left, their name above the text and
// a "ДАЛЕЕ" button on the right — quests.xml describes them as
//   <Dialog Type="Regular" Title="characterSensei" Image="character_sensei">
//     <Line Text="tutorial_begin_1" ButtonText="dlgStoryBtnMore"/>
// so Title and every Line are localization keys, and Image names an avatar in
// image/users/image. This used to be a dark blue rectangle with a hard-coded
// "Click to continue..." in it.
void DialogueScene::on_render(SceneContext& ctx) {
    const auto& lines = ctx.host.host_get_dialogue();
    auto& r = ctx.renderer;
    const float w = (float)ctx.platform.window_width();
    const float h = (float)ctx.platform.window_height();

    r.draw_filled_rect_screen(0, 0, w, h, {0, 0, 0, 150});
    if (current_line_ >= lines.size()) return;

    // The scroll sits across the bottom, as wide as the frame less a margin.
    const float box_w = w * 0.86f;
    const float box_h = h * 0.30f;
    const float box_x = (w - box_w) * 0.5f;
    const float box_y = h - box_h - h * 0.10f;
    ctx.host.host_render_scroll_panel(box_x, box_y, box_w, box_h);

    // Avatar on the left, inset so it clears the sheet's edge strip.
    const float pad = box_h * 0.10f;
    float text_x = box_x + box_w * 0.055f;
    const float avatar = box_h * 0.78f;
    const std::string& speaker_key = lines[current_line_].first;
    if (ctx.host.host_render_ui_texture(speaker_key.empty() ? "character_sensei"
                                                            : speaker_key,
                                        box_x + box_w * 0.03f,
                                        box_y + (box_h - avatar) * 0.5f,
                                        avatar, avatar)) {
        text_x = box_x + box_w * 0.03f + avatar + box_w * 0.025f;
    }

    // Speaker name, then the line. Both arrive as localization keys; the raw
    // key is shown if it is missing so a typo is visible rather than silent.
    const float name_scale = text_scale(h * 0.036f);
    const float body_scale = text_scale(h * 0.040f);
    float ty = box_y + pad;
    {
        std::string title = ctx.host.host_localized(speaker_key);
        if (!title.empty()) {
            const auto [tw, th] = ctx.host.host_measure_text(title, name_scale);
            (void)tw;
            ctx.host.host_render_text(title, text_x, ty, name_scale, 150, 88, 40, 255);
            ty += th * 1.15f;
        }
    }

    // Typewriter reveal over the localized text, wrapped to the sheet.
    std::string full = ctx.host.host_localized(lines[current_line_].second);
    if (full.empty()) full = lines[current_line_].second;
    const size_t chars_visible = text_reveal_ms_ / kCharRevealMs;
    // Count in CODE POINTS, not bytes: a byte-wise substr would cut a Cyrillic
    // letter in half and print a replacement glyph.
    size_t cut = 0, seen = 0;
    while (cut < full.size() && seen < chars_visible) {
        const unsigned char c = (unsigned char)full[cut];
        cut += (c < 0x80) ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        ++seen;
    }
    const std::string visible = full.substr(0, std::min(cut, full.size()));

    const float wrap_w = box_x + box_w - box_w * 0.055f - text_x;
    std::string line_buf;
    size_t pos = 0;
    while (pos <= visible.size()) {
        const size_t sp = visible.find(' ', pos);
        const std::string word = visible.substr(pos, sp == std::string::npos
                                                        ? std::string::npos
                                                        : sp - pos);
        const std::string probe = line_buf.empty() ? word : line_buf + " " + word;
        const auto [pw, ph] = ctx.host.host_measure_text(probe, body_scale);
        if (pw > wrap_w && !line_buf.empty()) {
            ctx.host.host_render_text(line_buf, text_x, ty, body_scale, 62, 42, 24, 255);
            ty += ph * 1.12f;
            line_buf = word;
        } else {
            line_buf = probe;
        }
        if (sp == std::string::npos) break;
        pos = sp + 1;
    }
    if (!line_buf.empty())
        ctx.host.host_render_text(line_buf, text_x, ty, body_scale, 62, 42, 24, 255);

    // The advance button, on its own little scroll like every button in the
    // original. Only once the line has finished revealing.
    if (cut >= full.size()) {
        std::string more = ctx.host.host_localized("dlgStoryBtnMore");
        if (more.empty()) more = "MORE";
        const float bw = box_w * 0.20f, bh = box_h * 0.26f;
        const float bx = box_x + box_w - bw - box_w * 0.05f;
        const float by = box_y + box_h - bh * 0.75f;
        ctx.host.host_render_scroll_panel(bx, by, bw, bh);
        const float bs = text_scale(bh * 0.44f);
        const auto [tw, th] = ctx.host.host_measure_text(more, bs);
        ctx.host.host_render_text(more, bx + (bw - tw) * 0.5f,
                                  by + (bh - th) * 0.5f, bs, 92, 46, 20, 255);
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
    sy += line_h + 10.0f;

    // Equipped items section
    r.draw_filled_rect_screen(panel_x, sy, panel_w, 2, {200, 170, 100, 150});
    sy += 8.0f;
    ctx.host.host_render_text("Equipped", panel_x + panel_w * 0.5f - 50, sy, 0.28f, 200, 170, 100, 255);
    sy += 30.0f;

    struct EquipSlot { const char* label; const char* slot; };
    EquipSlot slots[] = {
        {"Weapon:", "weapon"},
        {"Armor:", "armor"},
        {"Helmet:", "helmet"},
        {"Ranged:", "ranged"},
        {"Magic:", "magic"},
    };
    for (const auto& es : slots) {
        std::string eq = ctx.host.host_get_equipped(es.slot);
        if (eq.empty()) eq = "(none)";
        ctx.host.host_render_text(es.label, panel_x + 40, sy, 0.24f, 200, 200, 220, 255);
        ctx.host.host_render_text(eq, panel_x + 120, sy, 0.24f, 100, 200, 255, 255);
        sy += 28.0f;
    }
}

// ============================================================
// ShopScene helpers
// ============================================================

// Forward declarations for helper functions used by ShopScene.
// (Defined after ShopScene to keep category logic close to rendering.)

// Get items for a category from list_data.
static std::vector<resf2::format::ListItem> get_items_for_category(SceneContext& ctx, const std::string& category);

// Map item type (from list.xml) to equipment slot name.
static std::string slot_for_category(const std::string& type);

// ============================================================
// ShopScene
// ============================================================

void ShopScene::on_enter(SceneContext& ctx) {
    std::printf("[shop] enter\n");
    ctx.renderer.set_clear_color(0.04f, 0.04f, 0.07f, 1.0f);
    scroll_y_ = 0.0f;
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

    float w = (float)ctx.platform.window_width();
    float bar_h = 60.0f;

    // Click on items
    float cy = bar_h + 20.0f - scroll_y_;
    std::string categories[] = {"Weapon", "Armor", "Helm", "Ranged", "Magic"};

    for (auto& cat : categories) {
        auto items = get_items_for_category(ctx, cat);
        if (items.empty()) continue;

        // Category header (clickable for scroll-to)
        // Skip header rendering as it's just decoration
        cy += 35.0f;

        for (size_t idx = 0; idx < items.size(); ++idx) {
            const auto& entry = items[idx];
            float iy = cy + idx * 44.0f;

            // Skip if off-screen
            if (iy + 44 < 0 || iy > ctx.platform.window_height()) continue;

            // Buy button
            float bx = w - 165.0f;
            bool owned = ctx.host.host_has_item(entry.name);
            bool equipped = ctx.host.host_get_equipped(
                slot_for_category(entry.type)) == entry.name;

            if (!owned && !entry.is_paid) {
                // BUY button
                if (clicked_in(input, bx, iy + 4, 70, 34)) {
                    if (ctx.host.host_buy_item(entry.name)) {
                        ctx.host.host_play_ui_click();
                    }
                }
            } else if (owned && !equipped) {
                // EQUIP button
                if (clicked_in(input, bx, iy + 4, 70, 34)) {
                    if (ctx.host.host_equip_item(entry.name)) {
                        ctx.host.host_play_ui_click();
                    }
                }
            } else if (equipped) {
                // UNEQUIP button
                if (clicked_in(input, bx, iy + 4, 70, 34)) {
                    std::string slot = slot_for_category(entry.type);
                    if (ctx.host.host_unequip_item(slot)) {
                        ctx.host.host_play_ui_click();
                    }
                }
            }

            // Sell button (only for owned, non-equipped items)
            if (owned && !equipped) {
                float sell_bx = w - 90.0f;
                if (clicked_in(input, sell_bx, iy + 4, 70, 34)) {
                    if (ctx.host.host_sell_item(entry.name)) {
                        ctx.host.host_play_ui_click();
                    }
                }
            }
        }
        cy += items.size() * 44.0f + 10.0f;
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
    ctx.host.host_render_text(gold_str, w - 160, 18, 0.28f, 255, 220, 100, 255);

    // Check if we have any catalog data
    auto* list_data = ctx.host.host_get_list_data();
    if (!list_data || list_data->items.empty()) {
        ctx.host.host_render_text("No items available", w * 0.5f - 80, h * 0.5f, 0.30f, 150, 150, 170, 200);
        return;
    }

    int player_level = ctx.host.host_get_player_level();
    float cy = bar_h + 20.0f - scroll_y_;
    std::string categories[] = {"Weapon", "Armor", "Helm", "Ranged", "Magic"};

    for (auto& cat : categories) {
        auto items = get_items_for_category(ctx, cat);
        if (items.empty()) continue;

        // Category header
        r.draw_filled_rect_screen(20, cy, w - 40, 30, {40, 40, 55, 220});
        ctx.host.host_render_text(cat, 30, cy + 5, 0.28f, 200, 170, 100, 255);
        cy += 35.0f;

        for (size_t idx = 0; idx < items.size(); ++idx) {
            const auto& entry = items[idx];
            float iy = cy + idx * 44.0f;

            // Skip if off-screen
            if (iy + 44 < 0 || iy > h) continue;

            // Item row background (alternating)
            r.draw_filled_rect_screen(25, iy, w - 50, 40, (idx % 2 == 0) ?
                ren::Color4B{30, 30, 45, 200} : ren::Color4B{25, 25, 40, 200});

            // Item name
            ctx.host.host_render_text(entry.name, 35, iy + 10, 0.24f, 200, 200, 220, 255);

            // Stats
            std::string stats;
            if (entry.weapon_damage > 0)
                stats += "DMG:" + std::to_string((int)entry.weapon_damage) + " ";
            if (entry.body_defense > 0)
                stats += "DEF:" + std::to_string((int)entry.body_defense) + " ";
            if (entry.head_defense > 0)
                stats += "HDEF:" + std::to_string((int)entry.head_defense) + " ";
            if (entry.ranged_damage > 0)
                stats += "RDMG:" + std::to_string((int)entry.ranged_damage) + " ";
            if (entry.magic_damage > 0)
                stats += "MDMG:" + std::to_string((int)entry.magic_damage) + " ";
            ctx.host.host_render_text(stats, 210, iy + 2, 0.20f, 150, 150, 170, 200);

            // Level requirement
            if (entry.level > 1) {
                ctx.host.host_render_text("Lv." + std::to_string(entry.level),
                    210, iy + 22, 0.20f, 150, 150, 170, 200);
            }

            // IAP label
            if (entry.is_paid) {
                ctx.host.host_render_text("REAL $", w - 220, iy + 10, 0.22f, 255, 220, 100, 200);
                continue;
            }

            bool owned = ctx.host.host_has_item(entry.name);
            bool equipped = ctx.host.host_get_equipped(
                slot_for_category(entry.type)) == entry.name;

            // Price / Owned label
            if (owned) {
                ctx.host.host_render_text("OWNED", w - 230, iy + 2, 0.20f, 100, 200, 100, 200);
                if (equipped) {
                    ctx.host.host_render_text("EQUIPPED", w - 230, iy + 22, 0.20f, 100, 200, 255, 200);
                }
            } else {
                std::string price_str = std::to_string(entry.price) + "g";
                ctx.host.host_render_text(price_str, w - 230, iy + 2, 0.22f, 255, 220, 100, 255);
                if (entry.level > player_level) {
                    ctx.host.host_render_text("LVL REQ", w - 230, iy + 22, 0.18f, 255, 100, 100, 200);
                }
            }

            // Action buttons: BUY / EQUIP / UNEQUIP / SELL
            float buy_bx = w - 165.0f;
            float sell_bx = w - 90.0f;

            if (!owned && !entry.is_paid) {
                // BUY button
                bool can_afford = currency >= entry.price && player_level >= entry.level;
                bool can_buy = can_afford && entry.price > 0;
                r.draw_filled_rect_screen(buy_bx, iy + 4, 70, 32, can_buy ?
                    ren::Color4B{50, 120, 50, 220} : ren::Color4B{60, 60, 60, 200});
                ctx.host.host_render_text("BUY", buy_bx + 12, iy + 9, 0.22f, 255, 255, 255, 255);
            } else if (owned && !equipped) {
                // EQUIP button
                r.draw_filled_rect_screen(buy_bx, iy + 4, 70, 32, {50, 80, 140, 220});
                ctx.host.host_render_text("EQUIP", buy_bx + 6, iy + 9, 0.20f, 255, 255, 255, 255);
                // SELL button
                r.draw_filled_rect_screen(sell_bx, iy + 4, 70, 32, {140, 80, 40, 220});
                ctx.host.host_render_text("SELL", sell_bx + 10, iy + 9, 0.22f, 255, 255, 255, 255);
            } else if (equipped) {
                // UNEQUIP button
                r.draw_filled_rect_screen(buy_bx, iy + 4, 70, 32, {100, 60, 60, 220});
                ctx.host.host_render_text("UNEQUIP", buy_bx + 1, iy + 9, 0.18f, 255, 255, 255, 255);
            }
        }
        cy += items.size() * 44.0f + 10.0f;
    }
}

// Helper: get items for a category from ShopManager via list_data
static std::vector<resf2::format::ListItem> get_items_for_category(SceneContext& ctx, const std::string& category) {
    std::vector<resf2::format::ListItem> result;
    auto* list_data = ctx.host.host_get_list_data();
    if (!list_data) return result;
    for (const auto& item : list_data->items) {
        if (item.type == category && !item.shop_hide && !item.hidden && item.price > 0) {
            result.push_back(item);
        }
    }
    return result;
}

// Helper: map item type (from list.xml) to equipment slot name
static std::string slot_for_category(const std::string& type) {
    if (type == "Weapon") return "weapon";
    if (type == "Armor")  return "armor";
    if (type == "Helm")   return "helmet";
    if (type == "Ranged") return "ranged";
    if (type == "Magic")  return "magic";
    return {};
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

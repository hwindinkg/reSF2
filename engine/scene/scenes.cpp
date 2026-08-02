// engine/scene/scenes.cpp
//
// Concrete Scene implementations.

#include "scenes.hpp"
#include "scene_system.hpp"

#include "../renderer/renderer.hpp"
#include "../platform/platform.hpp"
#include "../format/stage_parser.hpp"
#include "../format/list_parser.hpp"
#include "../game/ui_scale.hpp"
#include "../game/shop.hpp"

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

    // Menu items — positioned to match the scroll menu (render_menu_expanded)
    // which draws icons using the same ui::atlas_scale layout law.
    // Rendered order: Dojo, Map, Shop, Profile, Settings (5 items).
    const float win_h = (float)ctx.platform.window_height();
    const float s = resf2::ui::atlas_scale(win_h);
    const float roll_y = resf2::ui::top_panel_h(win_h);
    const float roll_h = 114.0f * s;
    const float paper_y = roll_y + roll_h - 3.0f;
    const float paper_padding = 44.0f * s;
    const float icon_size = 176.0f * s;
    const float icon_spacing = 25.0f * s;
    const float ix = 32.0f * s + paper_padding + 31.0f * s;
    const float iy = paper_y + paper_padding;

    struct MenuItem { const char* name; SceneId target; };
    MenuItem items[] = {
        {"Dojo",     SceneId::MainMenu},
        {"Map",      SceneId::Map},
        {"Shop",     SceneId::Shop},
        {"Profile",  SceneId::Profile},
        {"Settings", SceneId::Settings},
    };
    for (int idx = 0; idx < 5; ++idx) {
        const float icon_y = iy + idx * (icon_size + icon_spacing);
        if (clicked_in(input, ix, icon_y, icon_size, icon_size)) {
            ctx.host.host_play_ui_click();
            std::printf("[mainmenu] clicked '%s' -> %s\n",
                        items[idx].name, scene_name(items[idx].target));
            ctx.host.request_scene_transition(items[idx].target);
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
        // [ORIGINAL] Show ALL zones from stages.xml. Zone lock state comes
        // from the save data (usersDefault.xml <Battles>), not sequential
        // progression. Zone 1 is unlocked, zones 2-6 are locked on first start.
        for (const auto& zone : stages->zones) {
            // [ORIGINAL] Zones flagged Start="1" (the Punchbag training zone)
            // never appear on the map: the zone-list builder @ 0x100c17d0
            // skips any zone whose Start byte is set (Zone ctor 0x102996d0
            // stores the flag at +0x118, FUN_10299cd0 reads it back).
            if (zone.start > 0) continue;
            std::vector<size_t> indices;
            for (size_t bi = 0; bi < zone.battles.size(); ++bi) {
                const auto& battle = zone.battles[bi];
                // [ORIGINAL] Type="HIDDEN" (enum 13 in the type table @
                // FUN_1012eb30) is skipped outright when DisplayZone builds
                // its nodes (0x100a2910). The name-based filters approximate
                // the quest-driven visibility of the variant nodes.
                if (battle.type == "HIDDEN" || battle.type == "FAKE" || battle.name.find("LOCKED") != std::string::npos || battle.name.find("ECLIPSE") != std::string::npos || battle.name.find("INTERMISSION") != std::string::npos) continue;
                indices.push_back(bi);
            }
            if (indices.empty()) continue;
            zone_battles_.push_back({zone, indices});
        }
        std::printf("[map] loaded %zu zones\n", zone_battles_.size());
        for (const auto& zb : zone_battles_) {
            bool unlocked = ctx.host.host_is_zone_unlocked(zb.zone.name);
            std::printf("[MAP] zone='%s' battles=%zu bg='%s' scroll_x=%.1f selected=%d %s\n",
                        zb.zone.name.c_str(), zb.battle_indices.size(),
                        zb.zone.filename.c_str(), scroll_x_, selected_,
                        unlocked ? "UNLOCKED" : "LOCKED");
        }
        // [ORIGINAL] The map opens on the player's current zone — the zone
        // builder @ 0x100c17d0 selects the page whose name matches the
        // profile's current zone, falling back to the first.
        const std::string level = ctx.host.host_get_current_level();
        const auto slash = level.find('/');
        if (slash != std::string::npos) {
            const std::string zone_name = level.substr(0, slash);
            for (size_t i = 0; i < zone_battles_.size(); ++i)
                if (zone_battles_[i].zone.name == zone_name) { selected_ = (int)i; break; }
        }
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

// [ORIGINAL] Zone background textures. The original loads per-zone images
// from image/locations/ based on the zone's thematic name (FUN_100c17d0
// passes the zone name to the texture loader). The shipped zone sheets are
// numeric (1.jpg..7.jpg), but the original also has themed map backgrounds.
// This table maps zone names to fallback tint colours when neither the
// numeric sheet nor a themed texture is available.
struct ZoneBackground {
    const char* zone_name;
    const char* texture_path;  // image/locations/<name>, may not exist
    ren::Color4B fallback_tint;
};
static const ZoneBackground kZoneBackgrounds[] = {
    {"ZONE_1", "image/locations/mapTutorial",  {180, 160, 120, 255}},  // parchment
    {"ZONE_2", "image/locations/mapEgypt",     {210, 185, 120, 255}},  // sandy
    {"ZONE_3", "image/locations/mapJapan",     {160, 180, 160, 255}},  // green-grey
    {"ZONE_4", "image/locations/mapMedieval",  {170, 150, 130, 255}},  // stone
    {"ZONE_5", "image/locations/mapFantasy",   {150, 140, 180, 255}},  // purple
    {"ZONE_6", "image/locations/mapShadow",    {100,  90, 100, 255}},  // dark
    {"ZONE_7", "image/locations/mapFinal",     {180, 140, 100, 255}},  // golden
    {"Punchbag", "image/locations/mapTutorial", {180, 160, 120, 255}}, // tutorial
};
static ren::Color4B zone_fallback_tint(const std::string& zone_name) {
    for (const auto& zb : kZoneBackgrounds)
        if (zone_name == zb.zone_name) return zb.fallback_tint;
    return {40, 32, 22, 255};  // default parchment brown
}

// [ORIGINAL] Text on this screen is sized the same way the HUD numerals are
// (PORT_PLAN 6.1): from the viewport height, not from constants. The bitmap
// font's line box is ~115 px at scale 1, so `text_scale(px)` asks for a height
// in pixels and gets the scale back. Sizing by hand is how the FIGHT caption
// first came out four times too big and ran off the panel.
constexpr float kFontLineBoxPx = 115.0f;
inline float text_scale(float wanted_px) { return wanted_px / kFontLineBoxPx; }

MapLayout map_layout(float w, float h) {
    MapLayout L;
    // [ORIGINAL] Same law as the in-game HUD: the panel is its atlas height
    // (192 px) through the 768-point space, i.e. 12.5% of the viewport
    // (ui_scale.hpp has the derivation and binary addresses).
    L.panel_h = ui::top_panel_h(h);
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

    // [FIX] Menu scroll toggle. The original map has a MENU button in the top
    // panel (same roll button the dojo uses). Pressing M opens the same
    // vertical scroll menu with Dojo/Map/Shop/Profile/Settings entries.
    // Previously this scene had no M handler, so the menu was unreachable
    // from the map even though the scroll button was rendered on the HUD.
    if (key_pressed(input, platform::Key::M)) {
        ctx.host.host_toggle_menu_overlay();
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
        // [ORIGINAL] The fight parameters travel with the fight: rounds and
        // the round clock from <Fight>, the opponent from its first warrior.
        // [HEURISTIC-TODO] Which of the battle's fights is up should come
        // from progression; the first one stands in until 7.3 lands.
        const auto& fight = selected_battle_->fights.front();
        SceneHost::BattleInfo info;
        info.rounds = fight.rounds;
        info.round_time_s = fight.round_time;
        info.reward_gold = fight.reward.money;
        info.reward_xp = fight.reward.exp;
        if (!fight.warriors.empty()) {
            const auto& w0 = fight.warriors.front();
            info.enemy_name = !w0.first_name.empty() ? w0.first_name
                                                     : w0.template_name;
        }
        ctx.host.host_set_battle_info(info);
        ctx.host.host_set_dialogue({{"Sly", selected_battle_->name},
                                    {"Narrator", "Location: " + selected_battle_->location}});
        ctx.host.request_scene_transition(SceneId::Dialogue);
        return;
    }
    // MENU scroll, top left — same box the dojo uses.
    // [FIX] Toggling the menu overlay instead of navigating to MainMenu.
    // The dojo click handler (game.cpp ~1814) toggles the overlay, so the
    // map should do the same for consistent behaviour.
    if (clicked_in(input, w * 0.012f, L.panel_h, w * 0.14f, L.panel_h * 0.62f)) {
        ctx.host.host_toggle_menu_overlay();
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
        // [ORIGINAL] Fall back to a zone-themed tint when the sheet texture
        // is missing. The original uses the same image/locations/ textures
        // as the fight backgrounds; those are not yet extracted.
        const std::string& zn = zone_battles_[(size_t)std::max(0, selected_)].zone.name;
        const auto tint = zone_fallback_tint(zn);
        r.draw_filled_rect_screen(L.map_x, L.map_y, L.map_w, L.map_h, tint);
        std::printf("[MAP] zone_bg missing for '%s', using fallback tint\n", zn.c_str());
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
        // [ORIGINAL] The original uses 5 textures per node (DisplayZone @
        // 0x100a1c00): active_batch (bright, selected), base_batch (normal),
        // locked_batch (grey), locked_active_batch (grey+selected),
        // pressed_batch (clicked). We map these to states 0/1/2:
        //   1 = active  — selected or just completed (bright)
        //   0 = base    — unlocked, not selected
        //   2 = locked  — not yet reachable (grey)
        // A node is reachable if it or any prior node in this zone has been
        // completed, matching the sequential progression the quest engine
        // will eventually drive via ShowBattle/HideBattle actions.
        int state = 0;  // base — unlocked
        if (sel) {
            state = 1;  // active — selected
        } else if (!n.completed) {
            bool reachable = false;
            for (size_t j = 0; j <= i; ++j) {
                if (nodes_[j].completed) { reachable = true; break; }
            }
            // First node is always reachable (zone gate is in on_enter).
            if (i == 0) reachable = true;
            if (!reachable) state = 2;  // locked
        }
        if (!ctx.host.host_render_battle_icon(n.icon, state, cx, cy, icon_size) &&
            !ctx.host.host_render_battle_icon(n.icon_fallback, state, cx, cy, icon_size)) {
            // No frame for this kind: a plain marker beats an invisible node.
            // Colour matches the state: gold=active, brown=base, grey=locked.
            ren::Color4B marker_col = n.completed ? ren::Color4B{140, 180, 100, 210}
                                      : (state == 2) ? ren::Color4B{90, 80, 70, 180}
                                      : sel ? ren::Color4B{235, 195, 110, 235}
                                            : ren::Color4B{150, 120, 80, 210};
            r.draw_filled_circle_screen(cx, cy, icon_size * 0.3f, marker_col);
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
            // [ORIGINAL] Each fight in a battle has its own level path
            // "zone/battle/fight" — the original checks completion per fight
            // to fill the round markers (FUN_100a3e50 iterates fights and
            // queries the profile's completed set via FUN_10138130).
            int done = 0;
            for (size_t fi = 0; fi < selected_battle_->fights.size(); ++fi) {
                const auto& fight = selected_battle_->fights[fi];
                std::string fight_level = selected_zone_name_ + "/" +
                                          selected_battle_->name + "/" +
                                          fight.name;
                if (ctx.host.host_is_level_completed(fight_level)) ++done;
            }
            // [L1] Log the round progress ONCE per state change, not every
            // frame: the soak log showed ~150 identical lines per second.
            char progress_buf[160];
            std::snprintf(progress_buf, sizeof(progress_buf),
                          "[MAP] round_progress: zone='%s' battle='%s' done=%d total=%d",
                          selected_zone_name_.c_str(), selected_battle_->name.c_str(),
                          done, total);
            if (progress_buf != last_round_progress_log_) {
                std::printf("%s\n", progress_buf);
                last_round_progress_log_ = progress_buf;
            }
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

    // [FIX] Render the menu overlay (scroll with Dojo/Map/Shop/Profile/Settings)
    // on top of the map. Previously host_render_menu_overlay was only reachable
    // from host_render_scene (dojo path), so pressing M on the map produced no
    // visible result even though the overlay state was set correctly.
    ctx.host.host_render_menu_overlay();
}

// ============================================================
// DialogueScene
// ============================================================

void DialogueScene::on_enter(SceneContext& ctx) {
    std::printf("[dialogue] enter\n");
    ctx.host.host_reset_menu_state();
    current_line_ = 0;
    text_reveal_ms_ = 0;
    const auto& lines = ctx.host.host_get_dialogue();
    std::printf("[DIALOG] lines=%zu speaker='%s'\n",
                lines.size(), lines.empty() ? "" : lines[0].first.c_str());
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

    // Esc: skip dialogue -> back to MainMenu
    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }

    // [D4] Advance on ANY input — a key press or a click — not just
    // Space/Enter. The original is a mobile game where tapping anywhere
    // advances the dialogue; on the PC port the player's own action keys
    // (P punch / O kick) must do the same. The soak showed the dialogue
    // "stuck" on line 1 because only Space/Enter/click were listened for
    // while the player pressed P/O — the only keys in the whole session.
    bool any_input = false;
    for (const bool just : input.keys_just_pressed) {
        if (just) { any_input = true; break; }
    }
    if (!any_input) {
        for (const auto& p : input.pointers) {
            if (p.just_pressed) { any_input = true; break; }
        }
    }
    if (any_input) advance();
}

// [ORIGINAL] A story dialogue is a parchment scroll in the center of the
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

    // [D1] No full-screen dim behind the dialogue: the soak showed the whole
    // background blackened while the original keeps the location visible.
    // The parchment panel below is the only thing the dialogue draws.
    if (current_line_ >= lines.size()) return;

    // ---- Scroll panel geometry ----
    // [ORIGINAL] The Regular dialog (QuestActionDialog @ FUN_101c7d20,
    // render vtable[4] = FUN_101ca500) positions the parchment background at
    // x = -450 with scale 1.8 (JS: Od.ala → b.C(-450), b.la(1.8*i)), giving
    // a visible parchment span of -450 … +450 in centered coordinates.
    // Button layout in JS: moreBtn.C(850 - w/2) → right edge at 850+w/2.
    // Screen edges in the JS coordinate system: ±850 (total 1700 units).
    // Proportional fractions of screen width/height:
    //   parchment width  = 900 / 1700 ≈ 0.529   (was 0.86 — far too wide)
    //   left edge        = 400 / 1700 ≈ 0.235
    // Text area (from JS qbb): Fa(900,800) → 900 pt wide × 800 pt tall,
    //   left padding inside parchment ≈ 50/900 ≈ 0.056 of parchment width.
    // The design space is 768-pt tall, width floats with aspect ratio;
    // 1365.25×768 for 16:9 (ui_scale.hpp).  The JS 1700×960 is a separate
    // dialog-local coordinate frame; the proportions below are screen-relative.
    // [D1] Vertically CENTERED: the soak showed the panel stuck to the bottom
    // of the screen; the original centers the parchment on the display.
    const float box_w = w * 0.53f;                       // 900/1700
    const float box_h = h * 0.20f;                       // text area fills ~80% of scroll height
    const float box_x = w * 0.235f;                      // (1700-900)/2/1700
    const float box_y = (h - box_h) * 0.5f;              // centered vertically
    ctx.host.host_render_scroll_panel(box_x, box_y, box_w, box_h);

    // ---- Avatar ----
    // [ORIGINAL] In the JS (Od.$A → Zg.C(-450+OB)) the avatar sits at the
    // left edge of the parchment inset by ~15 pt.  Avatar size is derived
    // from text area height (700 pt from JS kb.Fa(900,800) → kb.rd(true) →
    // kb.Kc(.9) → scaled by 700 pt).  Proportional to parchment height:
    //   avatar = 700/800 ≈ 0.875 of parchment height  (was 0.78)
    //   left inset = 15/900 ≈ 0.017 of parchment width
    const float pad = box_h * 0.08f;
    float text_x = box_x + box_w * 0.055f;
    const float avatar = box_h * 0.875f;
    const std::string& speaker_key = lines[current_line_].first;
    std::string avatar_name = "character_sensei";  // default fallback
    if (!speaker_key.empty()) {
        avatar_name = "character_";
        for (char c : speaker_key)
            avatar_name += (char)std::tolower((unsigned char)c);
    }
    if (ctx.host.host_render_ui_texture(avatar_name,
                                         box_x + box_w * 0.017f,
                                         box_y + (box_h - avatar) * 0.5f,
                                         avatar, avatar)) {
        // [ORIGINAL] Text starts right after the avatar + small gap.
        // avatar occupies `avatar` px; gap ≈ 2% of parchment width.
        text_x = box_x + box_w * 0.017f + avatar + box_w * 0.02f;
    }

    // Speaker name, then the line. The speaker key stored in dialogue lines is
    // a Latin identifier (e.g. "Sensei"); the localization key is formed by
    // prepending "character" (matching quests.xml Title="characterSensei").
    // The raw key is shown if localization is missing so a typo is visible.
    const float name_scale = text_scale(h * 0.036f);
    const float body_scale = text_scale(h * 0.040f);
    float ty = box_y + pad;
    {
        std::string title = ctx.host.host_localized("character" + speaker_key);
        if (title.empty()) title = ctx.host.host_localized(speaker_key);
        if (title.empty()) title = speaker_key;
        if (!title.empty()) {
            const auto [tw, th] = ctx.host.host_measure_text(title, name_scale);
            (void)tw;
            ctx.host.host_render_text(title, text_x, ty, name_scale, 150, 88, 40, 255);
            ty += th * 1.15f;
        }
    }

    // Wrap the localized text to the sheet. The whole line is revealed at
    // once — [D3] the soak showed a letter-by-letter typewriter (30 ms/char)
    // that left long lines half-read; the original shows the full line
    // immediately. chars_visible is capped at the full length so the reveal
    // loop below always completes on the first frame.
    std::string full = ctx.host.host_localized(lines[current_line_].second);
    if (full.empty()) full = lines[current_line_].second;
    const size_t chars_visible = full.size();
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
        // [DIALOG] Log typewriter completion
        std::printf("[DIALOG] typewriter=100%% line=%zu/%zu\n",
                    current_line_ + 1, lines.size());

        // [ORIGINAL] QuestActionDialog @ FUN_101c7d20 carries a choice vector
        // at +0xa4..+0xb0 (param_1+0x29 in assembly, confirmed at 0x101c7e89).
        // When choices are present, render them as clickable buttons below
        // the text instead of the generic MORE button.
        // Choice layout: centered horizontally within the parchment, sized
        // proportionally to the scroll dimensions.
        const auto& choices = ctx.host.host_get_dialogue_choices();
        if (!choices.empty()) {
            const float cbw = box_w * 0.30f, cbh = box_h * 0.25f;
            const float cbgap = box_w * 0.04f;
            const float total_cw = choices.size() * cbw + (choices.size() - 1) * cbgap;
            float cbx = box_x + (box_w - total_cw) * 0.5f;
            const float cby = box_y + box_h - cbh * 0.75f;
            for (size_t ci = 0; ci < choices.size(); ++ci) {
                ctx.host.host_render_scroll_panel(cbx, cby, cbw, cbh);
                const float cs = text_scale(cbh * 0.40f);
                const std::string label = ctx.host.host_localized(choices[ci]);
                const std::string display = label.empty() ? choices[ci] : label;
                const auto [ctw, cth] = ctx.host.host_measure_text(display, cs);
                ctx.host.host_render_text(display, cbx + (cbw - ctw) * 0.5f,
                                          cby + (cbh - cth) * 0.5f, cs,
                                          92, 46, 20, 255);
                // Choice click → advance dialogue
                if (clicked_in(ctx.platform.input(), cbx, cby, cbw, cbh)) {
                    std::printf("[DIALOG] choice=%zu '%s'\n", ci, choices[ci].c_str());
                    current_line_++;
                    text_reveal_ms_ = 0;
                    if (current_line_ >= lines.size()) {
                        ctx.host.request_scene_transition(
                            ctx.host.host_get_battle_location().empty()
                                ? SceneId::MainMenu : SceneId::Battle);
                    }
                    return;
                }
                cbx += cbw + cbgap;
            }
            return;  // choices replace the MORE button
        }

        std::string more = ctx.host.host_localized("dlgStoryBtnMore");
        if (more.empty()) more = "MORE";
        // [ORIGINAL] JS: moreBtn.C(850 - w/2), moreBtn.xc(600), moreBtn.Pb(125).
        // Button is 125 units wide, centered at x=850 in a ±850 frame.
        // Proportional: width = 125/1700 ≈ 0.074, right edge at (850+62.5)/1700 ≈ 0.537.
        // Height proportional to scroll height: 125/800 ≈ 0.156.
        const float bw = w * 0.074f;
        const float bh = box_h * 0.50f;
        const float bx = box_x + box_w - bw - box_w * 0.03f;
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
    guard_timer_ms_ = 0;
    between_rounds_ms_ = 0;
    wins_player_ = 0;
    wins_enemy_ = 0;
    ctx.host.host_reset_menu_state();
    ctx.host.host_set_battle_mode(true);
    ctx.host.host_set_show_enemy(true);
    // [ORIGINAL] Rounds and the per-round clock come from stages.xml
    // (<Fight Rounds= RoundTime=>), handed over by the map.
    const auto& info = ctx.host.host_get_battle_info();
    rounds_total_ = std::max(1, info.rounds);
    round_time_ms_ = std::max(1, info.round_time_s) * 1000;
    round_left_ms_ = round_time_ms_;
    ctx.host.host_set_round_wins(0, 0);
    // Load the actual battle location (from Map selection) instead of dojo
    std::string loc = ctx.host.host_get_battle_location();
    if (!loc.empty() && loc != "dojo") {
        std::printf("[battle] loading battle location: %s\n", loc.c_str());
        ctx.host.host_load_battle_location(loc);
    } else if (!ctx.host.host_location_loaded()) {
        ctx.host.host_load_location();
    }
    ctx.host.host_reset_round();
    // Start battle music
    ctx.host.host_start_battle_music();
}

void BattleScene::finish_round(SceneContext& ctx, bool player_won) {
    if (player_won) ++wins_player_; else ++wins_enemy_;
    ctx.host.host_set_round_wins(wins_player_, wins_enemy_);
    const int need = rounds_total_ / 2 + 1;
    if (wins_player_ >= need || wins_enemy_ >= need) {
        ctx.host.host_set_battle_result(wins_player_ >= need ? "victory"
                                                             : "defeat");
        ctx.host.request_scene_transition(SceneId::Results);
        return;
    }
    // Next round after a short pause; the original shows a round banner here
    // — that widget is not reversed yet.
    between_rounds_ms_ = kBetweenRoundsMs;
}

void BattleScene::on_update(SceneContext& ctx) {
    guard_timer_ms_ += ctx.dt_ms;

    if (between_rounds_ms_ > 0) {
        // Freeze the fight during the inter-round pause, then reset.
        if (between_rounds_ms_ <= ctx.dt_ms) {
            between_rounds_ms_ = 0;
            round_left_ms_ = round_time_ms_;
            ctx.host.host_reset_round();
        } else {
            between_rounds_ms_ -= ctx.dt_ms;
        }
        return;
    }

    ctx.host.host_update_gameplay(ctx.dt_ms);

    // Guard: prevent immediate transitions for the first 500ms.
    // This prevents accidental key carryover from the dialogue scene
    // (e.g., Space/Enter key being detected as a new press in Battle).
    if (guard_timer_ms_ < kGuardMs) return;

    // A round ends when a fighter dies...
    const std::string outcome = ctx.host.host_round_outcome();
    if (!outcome.empty()) {
        std::printf("[battle] round over: %s (%d:%d)\n", outcome.c_str(),
                    wins_player_, wins_enemy_);
        finish_round(ctx, outcome == "victory");
        return;
    }
    // ...or when the round clock runs out. On timeout the healthier fighter
    // takes the round. [HEURISTIC-TODO] The original's timeout/tie rule has
    // not been reversed; equal health goes to the enemy here.
    round_left_ms_ -= (int)ctx.dt_ms;
    if (round_left_ms_ <= 0) {
        const bool player_won = ctx.host.host_player_health_frac() >
                                ctx.host.host_enemy_health_frac();
        std::printf("[battle] round timeout (%d:%d)\n", wins_player_,
                    wins_enemy_);
        finish_round(ctx, player_won);
    }
}

void BattleScene::on_render(SceneContext& ctx) {
    // Host renders the location, the fighters and the fight HUD
    // (ScreenModel: bars, names, round dots — see render_fight_hud).
    ctx.host.host_render_scene();
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

    // [ORIGINAL] Rewards come from stages.xml <Fight Reward=><Reward Money= Exp=>,
    // handed over by the map via BattleInfo. Victory awards the full amount;
    // defeat awards nothing (the original does not give consolation rewards).
    const auto& info = ctx.host.host_get_battle_info();
    reward_gold_ = is_victory_ ? info.reward_gold : 0;
    reward_xp_ = is_victory_ ? info.reward_xp : 0;

    if (is_victory_) {
        std::printf("[results] victory! rewards: %d gold, %d XP\n", reward_gold_, reward_xp_);
        // Add currency reward
        if (reward_gold_ > 0) ctx.host.host_add_currency(reward_gold_);
        // Mark level as completed
        std::string level = ctx.host.host_get_current_level();
        if (!level.empty()) {
            ctx.host.host_add_completed_level(level);
            // [ORIGINAL] QuestManager processes FightEnd event to unlock zones/battles.
            ctx.host.host_trigger_quest_event("FightEnd", level);
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

    // Menu scroll toggle — consistent with Map, Shop, and dojo.
    if (key_pressed(input, platform::Key::M)) {
        ctx.host.host_toggle_menu_overlay();
    }

    // Continue button area — same proportions as on_render.
    const float w = (float)ctx.platform.window_width();
    const float h = (float)ctx.platform.window_height();
    const float btn_w = w * 0.22f;
    const float btn_h = h * 0.072f;
    const float btn_x = (w - btn_w) * 0.5f;
    const float btn_y = h * 0.74f;

    if (clicked_in(input, btn_x, btn_y, btn_w, btn_h) ||
        key_pressed(input, platform::Key::Space) ||
        key_pressed(input, platform::Key::Enter)) {
        ctx.host.host_play_ui_click();
        // Victory: go to Map to continue, defeat: back to MainMenu.
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
    const float w = (float)ctx.platform.window_width();
    const float h = (float)ctx.platform.window_height();

    // Background tint — victory = dark green, defeat = dark red.
    // Same approach as the dojo's clear-colour pattern: a subtle tinted wash
    // over the renderer's base clear, not a flat opaque fill.
    if (is_victory_) {
        r.draw_filled_rect_screen(0, 0, w, h, {12, 32, 12, 255});
    } else {
        r.draw_filled_rect_screen(0, 0, w, h, {36, 12, 10, 255});
    }

    // Text sizing: same law as MapScene (bitmap font line box ~115 px at 1.0).
    // Scale = desired_pixel_height / 115.0f.
    auto centre_text = [&](const std::string& text, float y, float scale,
                           uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
        const auto [tw, th] = ctx.host.host_measure_text(text, scale);
        ctx.host.host_render_text(text, (w - tw) * 0.5f, y, scale, cr, cg, cb, ca);
        return th;
    };

    // ---- Victory / Defeat banner ----
    // Large centred title. Colour matches the background tint: gold for
    // victory, crimson for defeat.
    const std::string title_key = is_victory_ ? "victory" : "defeat";
    std::string title = ctx.host.host_localized(title_key);
    if (title.empty()) title = is_victory_ ? "VICTORY" : "DEFEAT";
    const float title_scale = h * 0.085f / 115.0f;
    const uint8_t title_r = is_victory_ ? 255 : 255;
    const uint8_t title_g = is_victory_ ? 215 : 60;
    const uint8_t title_b = is_victory_ ? 60 : 40;
    centre_text(title, h * 0.10f, title_scale, title_r, title_g, title_b, 255);

    // ---- Rewards scroll panel ----
    // Parchment scroll panel (same asset as Dialogue and Shop) centred on screen.
    const float panel_w = w * 0.42f;
    const float panel_h = h * 0.38f;
    const float px = (w - panel_w) * 0.5f;
    const float py = h * 0.26f;
    ctx.host.host_render_scroll_panel(px, py, panel_w, panel_h);

    const float pad = panel_w * 0.08f;
    float y = py + panel_h * 0.08f;

    // Panel header: "Rewards" centred
    std::string rewards_label = ctx.host.host_localized("rewards");
    if (rewards_label.empty()) rewards_label = "Rewards";
    const float hdr_scale = h * 0.036f / 115.0f;
    const auto [hdr_tw, hdr_th] = ctx.host.host_measure_text(rewards_label, hdr_scale);
    ctx.host.host_render_text(rewards_label, px + (panel_w - hdr_tw) * 0.5f, y,
                              hdr_scale, 92, 46, 20, 255);
    y += hdr_th * 1.5f;

    // Reward rows — only shown on victory (defeat has no rewards).
    const float info_scale = h * 0.032f / 115.0f;
    const float row_h = panel_h * 0.18f;
    if (is_victory_) {
        // Gold row
        {
            const float lx = px + pad;
            std::string gold_label = ctx.host.host_localized("gold");
            if (gold_label.empty()) gold_label = "Gold";
            ctx.host.host_render_text(gold_label, lx, y, info_scale, 92, 62, 30, 255);
            char val_buf[32];
            std::snprintf(val_buf, sizeof(val_buf), "+%d", reward_gold_);
            const auto [vw, vh] = ctx.host.host_measure_text(val_buf, info_scale);
            ctx.host.host_render_text(val_buf, px + panel_w - pad - vw, y,
                                      info_scale, 220, 180, 40, 255);
            (void)vh;
            y += row_h;
        }
        // XP row
        {
            const float lx = px + pad;
            std::string xp_label = ctx.host.host_localized("experience");
            if (xp_label.empty()) xp_label = "XP";
            ctx.host.host_render_text(xp_label, lx, y, info_scale, 92, 62, 30, 255);
            char val_buf[32];
            std::snprintf(val_buf, sizeof(val_buf), "+%d", reward_xp_);
            const auto [vw, vh] = ctx.host.host_measure_text(val_buf, info_scale);
            ctx.host.host_render_text(val_buf, px + panel_w - pad - vw, y,
                                      info_scale, 80, 160, 255, 255);
            (void)vh;
            y += row_h;
        }
    } else {
        // Defeat message
        std::string no_reward = ctx.host.host_localized("noRewards");
        if (no_reward.empty()) no_reward = "No rewards";
        centre_text(no_reward, y + row_h * 0.3f, info_scale, 140, 60, 50, 255);
    }

    // ---- Continue button ----
    // Scroll panel button, matching the FIGHT button style in MapScene.
    const float btn_w = w * 0.22f;
    const float btn_h = h * 0.072f;
    const float btn_x = (w - btn_w) * 0.5f;
    const float btn_y = h * 0.74f;
    ctx.host.host_render_scroll_panel(btn_x, btn_y, btn_w, btn_h);

    std::string btn_label = ctx.host.host_localized(is_victory_ ? "continue" : "backToMenu");
    if (btn_label.empty()) btn_label = is_victory_ ? "CONTINUE" : "BACK TO MENU";
    const float btn_scale = h * 0.034f / 115.0f;
    const auto [btw, bth] = ctx.host.host_measure_text(btn_label, btn_scale);
    ctx.host.host_render_text(btn_label, btn_x + (btn_w - btw) * 0.5f,
                              btn_y + (btn_h - bth) * 0.5f, btn_scale,
                              92, 46, 20, 255);

    // ---- Top HUD panel (matches dojo/map/shop) ----
    ctx.host.host_render_top_panel();

    // Menu overlay (toggled by M key)
    ctx.host.host_render_menu_overlay();
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

// [ORIGINAL] ShopScreen @ 0x1021f170 — 8 category tabs with stride 0x10 at
// this+0x124..0x194. We expose the 5 primary categories that match the
// original's first five tabs (Weapon=1, Armor=2, Helm=3, Ranged=4, Magic=5).
// Categories 6-8 in the original (enchantments/specials/upgrades) are not yet
// reversed and are omitted.

// Shop-local text scale: wanted-px → font-scale, same law as MapScene.
// The bitmap font's line box is ~115 px at scale 1.
static float shop_text_scale(float wanted_px) { return wanted_px / 115.0f; }

// Get items for a category from list_data.
static std::vector<resf2::format::ListItem> shop_items_for_category(
    SceneContext& ctx, const std::string& category) {
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

// [ORIGINAL] Look up a single item by its list.xml Name.
// Used for the equipped-item rows, which need the item's own data (notably
// UpgradeLevel) and not just its id.
static const resf2::format::ListItem* shop_find_item(
    SceneContext& ctx, const std::string& name) {
    auto* list_data = ctx.host.host_get_list_data();
    if (!list_data || name.empty()) return nullptr;
    for (const auto& item : list_data->items) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

// Map item type (from list.xml) to equipment slot name.
static std::string shop_slot_for_type(const std::string& type) {
    if (type == "Weapon") return "weapon";
    if (type == "Armor")  return "armor";
    if (type == "Helm")   return "helmet";
    if (type == "Ranged") return "ranged";
    if (type == "Magic")  return "magic";
    return {};
}

// [REWORK] Shop layout matching the reference screenshot proportions.
// [ORIGINAL] Derived from ShopScreen @ 0x1021f170. The reference shows:
//   - MENU scroll roll at the very top (same as dojo)
//   - Three vertical columns below: fighter silhouette (left), equipped-items
//     scroll (centre), item detail/buy panel (right)
//   - Bottom bar with currency readouts on the left and category icons on the
//     right.
namespace {
struct ShopLayout {
    float s;                // points -> screen pixels
    float win_w, win_h;     // cached window size in screen pixels
    float logical_w;        // logical width in points (win_w / s)

    // --- vertical bands ---
    float menu_roll_y;      // top of the MENU scroll roll bar
    float menu_roll_h;      // height of the roll bar
    float body_y;           // top of the body (below roll bar)
    float body_h;           // total body height
    float bottom_y;         // top of the bottom category/currency bar
    float bottom_h;         // bottom bar height

    // --- body columns (horizontal thirds) ---
    float fighter_x, fighter_w;   // left column: fighter silhouette + TRY ON
    float scroll_x,  scroll_w;    // centre column: equipped-items parchment
    float detail_x,  detail_w;    // right column: item name + stats + BUY

    // --- fighter column internals ---
    float try_on_y, try_on_h;     // TRY ON button inside fighter area
    float arrow_l_x, arrow_r_x;   // left/right navigation arrows
    float arrow_y, arrow_w, arrow_h;

    // --- bottom bar internals ---
    float currency_x;             // left edge of currency display
    float cat_icon_y, cat_icon_w; // category icon row
    float cat_icons_start_x;      // first icon x

    // --- detail panel internals ---
    float detail_pad;             // padding inside detail panel
    float stat_bar_x, stat_bar_w, stat_bar_h;  // damage bar position
    float buy_btn_x, buy_btn_y, buy_btn_w, buy_btn_h;  // BUY button
};

ShopLayout shop_layout(float w, float h) {
    ShopLayout L;
    L.s = resf2::ui::points_scale(h);
    L.win_w = w;
    L.win_h = h;
    L.logical_w = w / L.s;

    // Vertical bands — reference uses the dojo's top HUD + a MENU scroll roll
    // above the body, and a category/currency bar at the bottom.
    L.menu_roll_h = 56.0f * L.s;
    L.menu_roll_y = 192.0f * L.s;   // below the top panel (same as dojo)
    L.bottom_h    = 80.0f * L.s;
    L.bottom_y    = h - L.bottom_h;
    L.body_y      = L.menu_roll_y + L.menu_roll_h;
    L.body_h      = L.bottom_y - L.body_y;

    // Three body columns: 28% / 40% / 32% of logical width, in screen pixels.
    L.fighter_w = L.logical_w * 0.28f * L.s;
    L.scroll_w  = L.logical_w * 0.40f * L.s;
    L.detail_w  = L.logical_w * 0.32f * L.s;
    L.fighter_x = 0.0f;
    L.scroll_x  = L.fighter_w;
    L.detail_x  = L.fighter_w + L.scroll_w;

    // Fighter column: silhouette in the upper 60%, TRY ON button + arrows
    // below. Arrows are small chevrons at the left/right edges.
    L.try_on_h = 36.0f * L.s;
    L.try_on_y = L.body_y + L.body_h * 0.62f;
    L.arrow_w  = 28.0f * L.s;
    L.arrow_h  = 40.0f * L.s;
    L.arrow_y  = L.try_on_y + L.try_on_h + 12.0f * L.s;
    L.arrow_l_x = L.fighter_x + 6.0f * L.s;
    L.arrow_r_x = L.fighter_x + L.fighter_w - L.arrow_w - 6.0f * L.s;

    // Bottom bar: currency on the left, category icons centred.
    L.currency_x = 16.0f * L.s;
    L.cat_icon_w = 52.0f * L.s;
    L.cat_icon_y = L.bottom_y + (L.bottom_h - L.cat_icon_w) * 0.5f;
    const int n_cats = 5;
    const float total_cat_w = n_cats * L.cat_icon_w + (n_cats - 1) * 12.0f * L.s;
    L.cat_icons_start_x = (w - total_cat_w) * 0.5f;

    // Detail panel: padding, stat bar, buy button near the bottom.
    L.detail_pad = L.detail_w * 0.07f;
    L.stat_bar_x = L.detail_x + L.detail_pad;
    L.stat_bar_w = L.detail_w - 2.0f * L.detail_pad;
    L.stat_bar_h = 24.0f * L.s;
    L.buy_btn_w  = L.detail_w * 0.72f;
    L.buy_btn_h  = 44.0f * L.s;
    L.buy_btn_x  = L.detail_x + (L.detail_w - L.buy_btn_w) * 0.5f;
    L.buy_btn_y  = L.bottom_y - L.buy_btn_h - 14.0f * L.s;

    return L;
}
}  // namespace

// ============================================================
// ShopScene
// ============================================================

void ShopScene::on_enter(SceneContext& ctx) {
    std::printf("[shop] enter\n");
    ctx.renderer.set_clear_color(0.05f, 0.03f, 0.01f, 1.0f);

    // Reset state
    selected_category_ = 0;   // 0 = Weapon
    selected_item_idx_ = 0;
    scroll_offset_ = 0.0f;

    // [SHOP] Log initial state
    auto items = shop_items_for_category(ctx, categories_[0]);
    int gold = ctx.host.host_get_currency();
    int level = ctx.host.host_get_player_level();
    std::printf("[SHOP] category='%s' items=%zu selected=%d gold=%d level=%d\n",
                categories_[0].c_str(), items.size(), selected_item_idx_,
                gold, level);
}

void ShopScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();
    const float w = (float)ctx.platform.window_width();
    const float h = (float)ctx.platform.window_height();
    const ShopLayout L = shop_layout(w, h);

    // --- Back / Esc ---
    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }
    // [FIX] M key toggles the menu overlay instead of going back to main menu.
    // The shop has its own MENU scroll roll now; pressing M opens the same
    // vertical scroll menu as on the dojo/map.
    if (key_pressed(input, platform::Key::M)) {
        ctx.host.host_toggle_menu_overlay();
    }

    // Back click on the MENU roll bar (leftmost 20%) — goes back to main menu
    if (clicked_in(input, 0, L.menu_roll_y, 80.0f * L.s, L.menu_roll_h)) {
        ctx.host.host_play_ui_click();
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }
    // Back button click (top-left corner placeholder for older flows)
    if (clicked_in(input, 10, 10, 80, 40)) {
        ctx.host.host_play_ui_click();
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }

    // --- Category icon clicks (bottom bar) ---------------------------------
    const float icon_gap = 12.0f * L.s;
    for (size_t i = 0; i < categories_.size(); ++i) {
        float ix = L.cat_icons_start_x + static_cast<float>(i) * (L.cat_icon_w + icon_gap);
        if (clicked_in(input, ix, L.cat_icon_y, L.cat_icon_w, L.cat_icon_w)) {
            if (static_cast<int>(i) != selected_category_) {
                selected_category_ = static_cast<int>(i);
                selected_item_idx_ = 0;
                scroll_offset_ = 0.0f;
                ctx.host.host_play_ui_click();
                std::printf("[SHOP] category switch -> '%s'\n",
                            categories_[selected_category_].c_str());
            }
            break;
        }
    }

    // --- Left/right navigation arrows in fighter column --------------------
    if (clicked_in(input, L.arrow_l_x, L.arrow_y, L.arrow_w, L.arrow_h)) {
        int nc = static_cast<int>(categories_.size());
        selected_category_ = (selected_category_ - 1 + nc) % nc;
        selected_item_idx_ = 0;
        scroll_offset_ = 0.0f;
        ctx.host.host_play_ui_click();
    }
    if (clicked_in(input, L.arrow_r_x, L.arrow_y, L.arrow_w, L.arrow_h)) {
        int nc = static_cast<int>(categories_.size());
        selected_category_ = (selected_category_ + 1) % nc;
        selected_item_idx_ = 0;
        scroll_offset_ = 0.0f;
        ctx.host.host_play_ui_click();
    }

    // --- Equipped-item slot clicks (centre scroll) --------------------------
    auto items = shop_items_for_category(ctx, categories_[selected_category_]);
    int item_count = static_cast<int>(items.size());
    {
        const float pad = L.scroll_w * 0.08f;
        const float inner_x = L.scroll_x + pad;
        const float inner_w = L.scroll_w - 2.0f * pad;
        float y = L.body_y + L.body_h * 0.06f;
        // Skip header (approx 26pt * scale * 1.4)
        y += 26.0f * L.s * 1.4f;
        const float row_h = L.body_h * 0.22f;
        // Up to kVisibleRows rows
        for (int i = 0; i < kVisibleRows; ++i) {
            if (clicked_in(input, inner_x, y, inner_w, row_h - 6.0f * L.s)) {
                int new_sel = static_cast<int>(scroll_offset_) + i;
                if (new_sel >= 0 && new_sel < item_count && new_sel != selected_item_idx_) {
                    selected_item_idx_ = new_sel;
                    ctx.host.host_play_ui_click();
                }
                break;
            }
            y += row_h;
        }
    }

    // --- BUY / EQUIP / UNEQUIP buttons --------------------------------------
    if (selected_item_idx_ >= 0 && selected_item_idx_ < item_count) {
        const auto& item = items[selected_item_idx_];
        bool owned = ctx.host.host_has_item(item.name);
        bool equipped = ctx.host.host_get_equipped(
            shop_slot_for_type(item.type)) == item.name;
        int gold = ctx.host.host_get_currency();
        int level = ctx.host.host_get_player_level();

        if (!owned && !item.is_paid) {
            if (clicked_in(input, L.buy_btn_x, L.buy_btn_y, L.buy_btn_w, L.buy_btn_h)) {
                bool can_buy = (gold >= item.price && level >= item.level && item.price > 0);
                if (can_buy) {
                    if (ctx.host.host_buy_item(item.name)) {
                        ctx.host.host_play_ui_click();
                        std::printf("[SHOP] buy item='%s' price=%d -> success\n",
                                    item.name.c_str(), item.price);
                    }
                }
            }
        } else if (owned && !equipped) {
            if (clicked_in(input, L.buy_btn_x, L.buy_btn_y, L.buy_btn_w, L.buy_btn_h)) {
                if (ctx.host.host_equip_item(item.name)) {
                    ctx.host.host_play_ui_click();
                }
            }
        } else if (equipped) {
            if (clicked_in(input, L.buy_btn_x, L.buy_btn_y, L.buy_btn_w, L.buy_btn_h)) {
                std::string slot = shop_slot_for_type(item.type);
                if (ctx.host.host_unequip_item(slot)) {
                    ctx.host.host_play_ui_click();
                }
            }
        }
    }

    // --- Scroll with W/S keys in the scroll area ----------------------------
    if (key_pressed(input, platform::Key::W) && scroll_offset_ > 0.0f) {
        scroll_offset_ -= 1.0f;
    }
    if (key_pressed(input, platform::Key::S)) {
        float max_scroll = std::max(0.0f,
            static_cast<float>(item_count) - static_cast<float>(kVisibleRows));
        if (scroll_offset_ < max_scroll) {
            scroll_offset_ += 1.0f;
        }
    }
}

void ShopScene::on_render(SceneContext& ctx) {
    auto& r = ctx.renderer;
    const float w = (float)ctx.platform.window_width();
    const float h = (float)ctx.platform.window_height();
    const ShopLayout L = shop_layout(w, h);

    // --- Background: dark brown base (matches reference screenshot base) ----
    r.draw_filled_rect_screen(0, 0, w, h, {18, 12, 6, 255});

    // --- Top HUD panel (gold/level) — same as dojo/menu --------------------
    ctx.host.host_render_top_panel();

    // --- MENU scroll roll at the top of the body ----------------------------
    // [ORIGINAL] Same roll texture the dojo uses; its label is the localized
    // "MENU" string. The click target toggles the menu overlay (see on_update).
    {
        const float ry = L.menu_roll_y, rh = L.menu_roll_h;
        r.draw_filled_rect_screen(0, ry, w, rh, {42, 28, 14, 230});
        // Left and right caps (slightly darker to fake a rolled edge)
        r.draw_filled_rect_screen(0, ry, 20.0f * L.s, rh, {60, 40, 20, 255});
        r.draw_filled_rect_screen(w - 20.0f * L.s, ry, 20.0f * L.s, rh,
                                  {60, 40, 20, 255});
        std::string menu_label = ctx.host.host_localized("menu");
        if (menu_label.empty()) menu_label = "MENU";
        const float ts = shop_text_scale(rh * 0.55f);
        const auto [tw, th] = ctx.host.host_measure_text(menu_label, ts);
        ctx.host.host_render_text(menu_label, (w - tw) * 0.5f, ry + (rh - th) * 0.5f,
                                  ts, 255, 230, 170, 255);
    }

    // --- Left column: fighter silhouette + TRY ON + arrows ------------------
    {
        const float fx = L.fighter_x, fw = L.fighter_w, fy = L.body_y, fh = L.body_h;
        // Dark silhouette backdrop — a placeholder for the real 3D model. The
        // original renders the fighter using the same body_model as gameplay,
        // but that needs the full model pipeline (not available in the shop
        // scene yet). A dark gradient stands in so the proportions are visible.
        ren::Color4B sil_bg{12, 8, 4, 255};
        r.draw_filled_rect_screen(fx, fy, fw, fh, sil_bg);
        // Silhouette "fighter" rectangle (dark brown) with a subtle highlight
        const float sil_pad = fw * 0.10f;
        const float sil_x = fx + sil_pad;
        const float sil_w = fw - 2.0f * sil_pad;
        const float sil_h = fh * 0.55f;
        const float sil_y = fy + fh * 0.04f;
        r.draw_filled_rect_screen(sil_x, sil_y, sil_w, sil_h, {26, 18, 10, 240});
        // Simple body shape inside the silhouette (two triangles for torso+legs)
        const float cx = sil_x + sil_w * 0.5f;
        const float top_y = sil_y + sil_h * 0.05f;
        const float bot_y = sil_y + sil_h * 0.95f;
        const float shoulder_w = sil_w * 0.38f;
        const float hip_w = sil_w * 0.22f;
        // Torso triangle (pointing up)
        r.draw_filled_rect_screen(cx - shoulder_w, top_y, shoulder_w * 2.0f,
                                  sil_h * 0.45f, {52, 36, 20, 200});
        // Legs rectangle
        r.draw_filled_rect_screen(cx - hip_w, top_y + sil_h * 0.45f,
                                  hip_w * 2.0f, sil_h * 0.50f, {44, 30, 16, 200});
        // Label
        ctx.host.host_render_text("FIGHTER", cx - 36.0f * L.s,
                                  sil_y + sil_h + 6.0f * L.s,
                                  shop_text_scale(18.0f * L.s),
                                  120, 90, 60, 200);

        // TRY ON button below the silhouette
        const float try_x = fx + (fw - L.try_on_h * 3.5f) * 0.5f;
        const float try_w = L.try_on_h * 3.5f;
        r.draw_filled_rect_screen(try_x, L.try_on_y, try_w, L.try_on_h,
                                  {90, 70, 40, 230});
        std::string try_label = ctx.host.host_localized("tryOn");
        if (try_label.empty()) try_label = "TRY ON";
        const float try_ts = shop_text_scale(L.try_on_h * 0.55f);
        const auto [ttw, tth] = ctx.host.host_measure_text(try_label, try_ts);
        ctx.host.host_render_text(try_label, try_x + (try_w - ttw) * 0.5f,
                                  L.try_on_y + (L.try_on_h - tth) * 0.5f,
                                  try_ts, 255, 230, 170, 255);

        // Navigation arrows (< >) at the edges of the fighter area
        const float arr_ts = shop_text_scale(L.arrow_h * 0.9f);
        ctx.host.host_render_text("<", L.arrow_l_x, L.arrow_y, arr_ts,
                                  220, 180, 120, 255);
        ctx.host.host_render_text(">", L.arrow_r_x, L.arrow_y, arr_ts,
                                  220, 180, 120, 255);
    }

    // --- Centre column: equipped-items scroll (parchment) -------------------
    {
        // Parchment background (same tint as the dojo's scroll panels).
        const float sx = L.scroll_x, sw = L.scroll_w, sy = L.body_y, sh = L.body_h;
        ctx.host.host_render_scroll_panel(sx, sy, sw, sh);

        const float pad = sw * 0.08f;
        const float inner_x = sx + pad;
        const float inner_w = sw - 2.0f * pad;
        float y = sy + sh * 0.06f;

        // Header: "EQUIPPED" centred
        std::string header = ctx.host.host_localized("equipped");
        if (header.empty()) header = "EQUIPPED";
        const float hdr_scale = shop_text_scale(26.0f * L.s);
        const auto [htw, hth] = ctx.host.host_measure_text(header, hdr_scale);
        ctx.host.host_render_text(header, sx + (sw - htw) * 0.5f, y,
                                  hdr_scale, 92, 46, 20, 255);
        y += hth * 1.4f;

        // Three equipped-item slots: weapon (top), consumable (middle),
        // secondary weapon (bottom). Each row shows the item's image, name,
        // and a star rating. Missing slots render an empty placeholder.
        struct Slot { const char* label; std::string slot_key; };
        Slot slots[] = {{"Weapon",    "weapon"},
                        {"Consumable","consumable"},
                        {"Ranged",    "ranged"}};
        const float row_h = sh * 0.22f;
        const float icon_sz = row_h * 0.75f;
        for (const auto& slot : slots) {
            std::string equipped = ctx.host.host_get_equipped(slot.slot_key);
            // Row background (subtle lighter stripe for readability)
            r.draw_filled_rect_screen(inner_x, y, inner_w, row_h - 6.0f * L.s,
                                      {180, 150, 100, 60});
            const float icon_x = inner_x + 6.0f * L.s;
            const float icon_y = y + (row_h - 6.0f * L.s - icon_sz) * 0.5f;
            if (!equipped.empty()) {
                // Try to render the equipped item's texture.
                if (!ctx.host.host_render_ui_texture(equipped,
                        icon_x, icon_y, icon_sz, icon_sz)) {
                    // Fallback tinted square
                    r.draw_filled_rect_screen(icon_x, icon_y, icon_sz, icon_sz,
                                              {110, 80, 40, 200});
                }
                // Item name
                std::string name = ctx.host.host_localized(equipped);
                if (name.empty()) name = equipped;
                const float name_ts = shop_text_scale(22.0f * L.s);
                ctx.host.host_render_text(name, icon_x + icon_sz + 10.0f * L.s,
                                          y + 8.0f * L.s, name_ts,
                                          70, 40, 20, 255);
                // [ORIGINAL] The stars are the item's UpgradeLevel from
                // list.xml, not a rarity: the binary has no "Rarity" or "Rank"
                // string anywhere, only UpgradeLevel / UpgradeNumber /
                // UpgradeList. This was hardcoded to "4" for every item.
                if (const auto* def = shop_find_item(ctx, equipped)) {
                    const int stars = def->upgrade_level;
                    std::string rating;
                    for (int i = 0; i < stars; ++i) rating += "\xe2\x98\x85";
                    if (rating.empty()) rating = "\xe2\x98\x86";  // unupgraded
                    ctx.host.host_render_text(rating,
                                              icon_x + icon_sz + 10.0f * L.s,
                                              y + 28.0f * L.s,
                                              shop_text_scale(18.0f * L.s),
                                              200, 170, 60, 255);
                }
            } else {
                // Empty slot placeholder
                r.draw_filled_rect_screen(icon_x, icon_y, icon_sz, icon_sz,
                                          {80, 60, 40, 140});
                const float lbl_ts = shop_text_scale(18.0f * L.s);
                ctx.host.host_render_text(slot.label,
                                          icon_x + icon_sz + 10.0f * L.s,
                                          y + (row_h - 6.0f * L.s) * 0.5f - 8.0f * L.s,
                                          lbl_ts, 130, 100, 70, 200);
                ctx.host.host_render_text("(empty)",
                                          icon_x + icon_sz + 10.0f * L.s,
                                          y + (row_h - 6.0f * L.s) * 0.5f + 10.0f * L.s,
                                          shop_text_scale(14.0f * L.s),
                                          130, 100, 70, 160);
            }
            y += row_h;
        }
    }

    // --- Right column: item detail panel ------------------------------------
    {
        const float dx = L.detail_x, dw = L.detail_w, dy = L.body_y, dh = L.body_h;
        ctx.host.host_render_scroll_panel(dx, dy, dw, dh);
        const float pad = L.detail_pad;
        const float stat_x = dx + pad;
        float info_y = dy + 14.0f * L.s;

        auto items = shop_items_for_category(ctx, categories_[selected_category_]);
        int item_count = static_cast<int>(items.size());
        if (selected_item_idx_ < 0 || selected_item_idx_ >= item_count) {
            ctx.host.host_render_text("No item selected",
                                      stat_x, info_y, shop_text_scale(20.0f * L.s),
                                      150, 120, 80, 200);
        } else {
            const auto& item = items[selected_item_idx_];

            // Large item icon preview (centre of the top of the panel)
            const float big_icon = dw * 0.20f;
            const float big_icon_x = dx + (dw - big_icon) * 0.5f;
            if (!ctx.host.host_render_ui_texture(item.image,
                    big_icon_x, info_y, big_icon, big_icon)) {
                r.draw_filled_rect_screen(big_icon_x, info_y, big_icon, big_icon,
                                          {80, 60, 30, 200});
            }
            info_y += big_icon + 10.0f * L.s;

            // Item name (centred)
            std::string display_name = ctx.host.host_localized(item.name);
            if (display_name.empty()) display_name = item.name;
            const float title_scale = shop_text_scale(28.0f * L.s);
            const auto [tw, th] = ctx.host.host_measure_text(display_name, title_scale);
            ctx.host.host_render_text(display_name,
                                      dx + (dw - tw) * 0.5f, info_y,
                                      title_scale, 255, 230, 180, 255);
            info_y += th * 1.4f;

            // Damage bar (sword icon + orange bar)
            const float bar_y = info_y;
            // Sword icon placeholder (text "â")
            // [ORIGINAL] textures/misc/Damage.png. This drew a Unicode sword
            // glyph as a stand-in even though the real art ships in the dump;
            // the glyph is now only a fallback if the texture is absent.
            {
                const float icon = 22.0f * L.s;
                if (!ctx.host.host_render_ui_texture("Damage", stat_x, bar_y,
                                                     icon, icon)) {
                    ctx.host.host_render_text("\xe2\x9a\x94", stat_x, bar_y,
                                              shop_text_scale(22.0f * L.s),
                                              230, 77, 77, 255);
                }
            }
            const float bar_x = stat_x + 28.0f * L.s;
            const float bar_w = L.stat_bar_w - 28.0f * L.s;
            const float bar_h = L.stat_bar_h;
            // Bar background
            r.draw_filled_rect_screen(bar_x, bar_y, bar_w, bar_h, {40, 28, 16, 200});
            // Bar fill — width proportional to damage (max reference 100)
            const float dmg_frac = std::min(1.0f, item.weapon_damage / 100.0f);
            r.draw_filled_rect_screen(bar_x, bar_y, bar_w * dmg_frac, bar_h,
                                      {220, 120, 40, 240});
            // Damage number
            char dmg_buf[32];
            std::snprintf(dmg_buf, sizeof(dmg_buf), "%d",
                          static_cast<int>(item.weapon_damage));
            ctx.host.host_render_text(dmg_buf, bar_x + bar_w + 6.0f * L.s, bar_y,
                                      shop_text_scale(20.0f * L.s),
                                      230, 200, 140, 255);
            info_y += bar_h + 10.0f * L.s;

            // Other stats (defense, ranged, magic) — compact text list
            auto render_stat = [&](const std::string& label, float value,
                                   uint8_t sr, uint8_t sg, uint8_t sb) {
                if (value <= 0) return;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s: +%d", label.c_str(),
                              static_cast<int>(value));
                ctx.host.host_render_text(buf, stat_x, info_y,
                                          shop_text_scale(18.0f * L.s),
                                          sr, sg, sb, 255);
                info_y += 20.0f * L.s;
            };
            render_stat("Defense",  item.body_defense,   77, 128, 230);
            render_stat("Head Def", item.head_defense,   77, 128, 230);
            render_stat("Ranged",   item.ranged_damage, 180, 130,  60);
            render_stat("Magic",    item.magic_damage,  180,  80, 220);
            info_y += 4.0f * L.s;

            // Level requirement
            ctx.host.host_render_text(
                "Lv. " + std::to_string(item.level),
                stat_x, info_y, shop_text_scale(18.0f * L.s),
                180, 180, 180, 255);
            info_y += 22.0f * L.s;

            // Owned / equipped status
            bool owned = ctx.host.host_has_item(item.name);
            bool equipped = ctx.host.host_get_equipped(
                shop_slot_for_type(item.type)) == item.name;
            if (equipped) {
                ctx.host.host_render_text("EQUIPPED", stat_x, info_y,
                    shop_text_scale(18.0f * L.s), 100, 200, 255, 255);
            } else if (owned) {
                ctx.host.host_render_text("OWNED", stat_x, info_y,
                    shop_text_scale(18.0f * L.s), 100, 200, 100, 255);
            }
        }
    }

    // --- BUY button (inside right column, above the bottom bar) -------------
    {
        auto items = shop_items_for_category(ctx, categories_[selected_category_]);
        int item_count = static_cast<int>(items.size());
        if (selected_item_idx_ >= 0 && selected_item_idx_ < item_count) {
            const auto& item = items[selected_item_idx_];
            int gold = ctx.host.host_get_currency();
            int level = ctx.host.host_get_player_level();
            bool owned = ctx.host.host_has_item(item.name);
            bool equipped = ctx.host.host_get_equipped(
                shop_slot_for_type(item.type)) == item.name;
            bool can_buy = (gold >= item.price && level >= item.level && item.price > 0);

            if (!owned && !item.is_paid) {
                // Green BUY button with price and gem icon
                r.draw_filled_rect_screen(L.buy_btn_x, L.buy_btn_y,
                    L.buy_btn_w, L.buy_btn_h,
                    can_buy ? ren::Color4B{51, 120, 26, 255}
                            : ren::Color4B{36, 36, 36, 220});
                const float btn_ts = shop_text_scale(L.buy_btn_h * 0.55f);
                std::string buy_label = ctx.host.host_localized("buy");
                if (buy_label.empty()) buy_label = "BUY";
                std::string price_str = std::to_string(item.price);
                // gem icon (placeholder)
                // [ORIGINAL] textures/misc/ruby.png is the gem currency icon;
                // the Unicode diamond is now only a fallback.
                {
                    const float gy =
                        L.buy_btn_y + (L.buy_btn_h - L.buy_btn_h * 0.55f) * 0.5f;
                    const float gsz = L.buy_btn_h * 0.55f;
                    if (!ctx.host.host_render_ui_texture(
                            "ruby", L.buy_btn_x + 14.0f * L.s, gy, gsz, gsz)) {
                        ctx.host.host_render_text("\xe2\x97\x86",
                            L.buy_btn_x + 14.0f * L.s, gy,
                            btn_ts, 200, 230, 255, 255);
                    }
                }
                // "BUY <price>" text
                ctx.host.host_render_text(buy_label + " " + price_str,
                    L.buy_btn_x + L.buy_btn_w * 0.5f - 20.0f * L.s,
                    L.buy_btn_y + (L.buy_btn_h - L.buy_btn_h * 0.55f) * 0.5f,
                    btn_ts, can_buy ? (uint8_t)255 : (uint8_t)102, 255, 180, 255);
            } else if (owned && !equipped) {
                // EQUIP button
                r.draw_filled_rect_screen(L.buy_btn_x, L.buy_btn_y,
                    L.buy_btn_w, L.buy_btn_h, {51, 80, 140, 255});
                const float btn_ts = shop_text_scale(L.buy_btn_h * 0.55f);
                std::string equip_label = ctx.host.host_localized("equip");
                if (equip_label.empty()) equip_label = "EQUIP";
                const auto [ewt, eht] = ctx.host.host_measure_text(equip_label, btn_ts);
                ctx.host.host_render_text(equip_label,
                    L.buy_btn_x + (L.buy_btn_w - ewt) * 0.5f,
                    L.buy_btn_y + (L.buy_btn_h - eht) * 0.5f,
                    btn_ts, 255, 255, 255, 255);
            } else if (equipped) {
                // UNEQUIP button
                r.draw_filled_rect_screen(L.buy_btn_x, L.buy_btn_y,
                    L.buy_btn_w, L.buy_btn_h, {102, 60, 60, 255});
                const float btn_ts = shop_text_scale(L.buy_btn_h * 0.55f);
                std::string unequip_label = ctx.host.host_localized("unequip");
                if (unequip_label.empty()) unequip_label = "UNEQUIP";
                const auto [uwt, uht] = ctx.host.host_measure_text(unequip_label, btn_ts);
                ctx.host.host_render_text(unequip_label,
                    L.buy_btn_x + (L.buy_btn_w - uwt) * 0.5f,
                    L.buy_btn_y + (L.buy_btn_h - uht) * 0.5f,
                    btn_ts, 255, 255, 255, 255);
            }
        }
    }

    // --- Bottom bar: currency (left) + category icons (centred) ------------
    {
        r.draw_filled_rect_screen(0, L.bottom_y, w, L.bottom_h, {20, 13, 5, 240});
        r.draw_filled_rect_screen(0, L.bottom_y, w, 2.0f * L.s, {200, 170, 100, 150});

        int gold = ctx.host.host_get_currency();
        int level = ctx.host.host_get_player_level();
        const float curr_ts = shop_text_scale(22.0f * L.s);
        // Diamond icon (gem) placeholder + gold amount
        ctx.host.host_render_text("\xe2\x97\x86", L.currency_x,
            L.bottom_y + 10.0f * L.s, curr_ts, 200, 230, 255, 255);
        ctx.host.host_render_text(std::to_string(gold),
            L.currency_x + 28.0f * L.s, L.bottom_y + 10.0f * L.s,
            curr_ts, 255, 217, 0, 255);
        // Bag icon + level
        ctx.host.host_render_text("\xe2\x96\xb2", L.currency_x,
            L.bottom_y + 38.0f * L.s, curr_ts * 0.9f, 180, 140, 80, 255);
        ctx.host.host_render_text("Lv." + std::to_string(level),
            L.currency_x + 28.0f * L.s, L.bottom_y + 38.0f * L.s,
            curr_ts * 0.9f, 180, 230, 180, 255);

        // Category icons (one per category, centred). Selected = bright.
        // [U2] Real atlas frames from shopButtons.plist (Weapon/Armor/
        // Helmet/Ranged_weapon/Magic, <name>_active when selected); the
        // Unicode glyphs were the no-asset fallback.
        const float icon_gap = 12.0f * L.s;
        float ix = L.cat_icons_start_x;
        static const char* kCatFrames[] = {"Weapon", "Armor", "Helmet",
                                           "Ranged_weapon", "Magic"};
        for (size_t i = 0; i < categories_.size(); ++i) {
            bool sel = (static_cast<int>(i) == selected_category_);
            // Icon background
            r.draw_filled_rect_screen(ix, L.cat_icon_y, L.cat_icon_w, L.cat_icon_w,
                sel ? ren::Color4B{90, 60, 25, 240}
                    : ren::Color4B{40, 28, 14, 200});
            // Border on selected
            if (sel) {
                r.draw_filled_rect_screen(ix, L.cat_icon_y, L.cat_icon_w, 3.0f * L.s,
                                          {220, 180, 100, 255});
                r.draw_filled_rect_screen(ix, L.cat_icon_y + L.cat_icon_w - 3.0f * L.s,
                                          L.cat_icon_w, 3.0f * L.s,
                                          {220, 180, 100, 255});
            }
            // Category icon from the shop atlas
            const std::string frame = std::string(kCatFrames[i]) +
                                      (sel ? "_active" : "");
            const float pad = L.cat_icon_w * 0.10f;
            if (!ctx.host.host_render_ui_texture(frame,
                    ix + pad, L.cat_icon_y + pad,
                    L.cat_icon_w - 2.0f * pad, L.cat_icon_w - 2.0f * pad)) {
                // No atlas: keep the old glyph fallback.
                static const char* cat_glyphs[] = {"\xe2\x9a\x94", "\xe2\x9b\xa8",
                                                   "\xe2\x9b\x91", "\xe2\x9e\xb6",
                                                   "\xe2\x9c\xa8"};
                const float glyph_ts = shop_text_scale(L.cat_icon_w * 0.55f);
                ctx.host.host_render_text(cat_glyphs[i],
                    ix + (L.cat_icon_w - glyph_ts * 30.0f) * 0.5f,
                    L.cat_icon_y + (L.cat_icon_w - glyph_ts * 30.0f) * 0.5f,
                    glyph_ts,
                    sel ? (uint8_t)255 : (uint8_t)140,
                    sel ? (uint8_t)220 : (uint8_t)110,
                    sel ? (uint8_t)120 : (uint8_t)80,
                    255);
            }
            ix += L.cat_icon_w + icon_gap;
        }
    }

    // --- Menu overlay (toggled by M key) rendered on top --------------------
    ctx.host.host_render_menu_overlay();
}

// [ORIGINAL] ShopScreen @ 0x1021f170 textures:
//   "textures/screens/shop/buttons/shopButtons" — button atlas (this+0x29c)
//   "image/enchantments/batchEnchantments" — enchantment atlas (this+0x2a0)
//   Base shop texture DAT_10658f70 (this+0x298)
//   Background: "textures/fullscreen/dojo_full_bg_light" (this+0x70) and
//               "textures/fullscreen/dojo_full_bg" (this+0x71)
// These are loaded by the constructor; the UI above uses scroll panels +
// bitmap font as fallbacks until the shop-specific atlases are wired.

// ============================================================
// SettingsScene
// ============================================================

void SettingsScene::on_enter(SceneContext& ctx) {
    std::printf("[settings] enter\n");
    ctx.renderer.set_clear_color(0.03f, 0.03f, 0.06f, 1.0f);
}

void SettingsScene::on_update(SceneContext& ctx) {
    const auto& input = ctx.platform.input();

    // --- Back / Esc ---
    if (key_pressed(input, platform::Key::Escape)) {
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }
    // [FIX] M key toggles the menu overlay instead of going back to main menu.
    // Consistent with Shop, Map, and Results scenes.
    if (key_pressed(input, platform::Key::M)) {
        ctx.host.host_toggle_menu_overlay();
    }

    // Back button click (top-left corner)
    if (clicked_in(input, 10, 10, 80, 40)) {
        ctx.host.host_play_ui_click();
        ctx.host.request_scene_transition(SceneId::MainMenu);
        return;
    }

    // Volume sliders (click-drag simulation)
    // In a real implementation, these would adjust audio engine volume.
    // For now, just detect clicks on the slider track.
    float w = (float)ctx.platform.window_width();
    float h = (float)ctx.platform.window_height();
    float panel_x = w * 0.15f, panel_w = w * 0.7f;
    float sy = h * 0.15f;

    // Master volume slider track
    float slider_x = panel_x + 120.0f, slider_w = panel_w - 140.0f;
    float slider_y = sy + 20.0f, slider_h = 30.0f;
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

    // --- Background ---
    r.draw_filled_rect_screen(0, 0, w, h, {8, 8, 16, 255});

    // --- Top bar ---
    r.draw_filled_rect_screen(0, 0, w, bar_h, {20, 20, 40, 230});
    r.draw_filled_rect_screen(10, 10, 80, bar_h - 20, {50, 50, 70, 200});
    ctx.host.host_render_text("< BACK", 20, 18, 0.32f, 220, 220, 240, 255);
    r.draw_filled_rect_screen(100, 10, w - 160, bar_h - 20, {30, 30, 50, 180});
    ctx.host.host_render_text("SETTINGS", w * 0.5f - 50, 15, 0.40f, 200, 200, 220, 255);

    // --- Top HUD panel (gold/level) — same as dojo/menu ---
    ctx.host.host_render_top_panel();

    float panel_x = w * 0.15f, panel_w = w * 0.7f;
    float panel_h = h * 0.7f, panel_y = bar_h + 30.0f;

    // Settings panel background
    r.draw_filled_rect_screen(panel_x, panel_y, panel_w, panel_h, {25, 25, 40, 220});
    // Gold border top and bottom
    r.draw_filled_rect_screen(panel_x, panel_y, panel_w, 2, {200, 170, 100, 200});
    r.draw_filled_rect_screen(panel_x, panel_y + panel_h - 2, panel_w, 2, {200, 170, 100, 200});

    // Section header
    ctx.host.host_render_text("Audio", panel_x + panel_w * 0.5f - 30, panel_y + 15, 0.35f, 220, 220, 240, 255);

    float sy = panel_y + 60.0f;
    float line_h = 35.0f;
    float slider_x = panel_x + 120.0f, slider_w = panel_w - 140.0f;

    // Master volume
    ctx.host.host_render_text("Master Volume", panel_x + 40, sy, 0.28f, 200, 200, 220, 255);
    // Slider track
    r.draw_filled_rect_screen(slider_x, sy + 20.0f, slider_w, 30.0f, {40, 40, 60, 200});
    // Slider fill (stub: 70%)
    r.draw_filled_rect_screen(slider_x, sy + 20.0f, slider_w * 0.7f, 30.0f, {100, 180, 255, 200});
    // Slider knob
    r.draw_filled_rect_screen(slider_x + slider_w * 0.7f - 4.0f, sy + 16.0f, 8.0f, 38.0f, {220, 220, 240, 255});
    sy += line_h + 20.0f;

    // Music volume
    ctx.host.host_render_text("Music Volume", panel_x + 40, sy, 0.28f, 200, 200, 220, 255);
    r.draw_filled_rect_screen(slider_x, sy + 20.0f, slider_w, 30.0f, {40, 40, 60, 200});
    r.draw_filled_rect_screen(slider_x, sy + 20.0f, slider_w * 0.5f, 30.0f, {100, 255, 100, 200});
    r.draw_filled_rect_screen(slider_x + slider_w * 0.5f - 4.0f, sy + 16.0f, 8.0f, 38.0f, {220, 220, 240, 255});
    sy += line_h + 30.0f;

    // Language section
    r.draw_filled_rect_screen(panel_x, sy, panel_w, 2, {200, 170, 100, 150});
    sy += 12.0f;
    ctx.host.host_render_text("Language", panel_x + panel_w * 0.5f - 45, sy, 0.30f, 200, 170, 100, 255);
    sy += 35.0f;

    float lang_btn_w = 100.0f, lang_btn_h = 36.0f;
    float lang_x = panel_x + 40.0f;
    // English button
    r.draw_filled_rect_screen(lang_x, sy, lang_btn_w, lang_btn_h, {60, 60, 80, 200});
    ctx.host.host_render_text("English", lang_x + 15, sy + 8, 0.24f, 220, 220, 240, 255);
    // Russian button
    r.draw_filled_rect_screen(lang_x + 120, sy, lang_btn_w, lang_btn_h, {60, 60, 80, 200});
    ctx.host.host_render_text("Russian", lang_x + 135, sy + 8, 0.24f, 220, 220, 240, 255);

    // --- Menu overlay (toggled by M key) rendered on top ---
    ctx.host.host_render_menu_overlay();
}

}  // namespace resf2::scene

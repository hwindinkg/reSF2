// The shell screens implementation — MainMenu, Map, Fight, Results, Shop,
// Equipment.
//
// Layout math (JS-derived):
//   - MainMenu buttons: the game's za top bar + the 4 tab buttons are the
//     exact JS layout; this phase uses the menu atlas frame proportions
//     (Dojo_normal 226x193, Map_normal 191x194, Shop_normal 246x238,
//     Profile_normal 193x231) at the JS positions: the four entry buttons
//     sit in the lower half of the dojo backdrop, horizontally spaced by
//     ~0.26 of the view width, centered. The Fight button (the Dojo
//     button) is the primary entry -> Map (screen 5).
//   - Map nodes: stages.xml <Zone><Battle X=.. Y=..> -> screen pos
//     x = X*1.0 + view_w/2, y = view_h/2 - Y*1.0 (qe.X0a's
//     bg.w/2 / bg.h/2 with uM≈1 for the 2046-wide map0 frame scaled to the
//     view). The Training battle (X=158, Y=145) lands lower-right.
//
// The menu/map/shop atlases ship as ASTC ktx / crunch dds (not CPU-decodable
// by the current pipeline — see core/data/README.md), so this phase renders
// a functional menu/map/shop: the dojo webp background + flat labeled
// buttons at the JS-derived positions. The exact atlas-art layout is
// flagged as a gap.

#include "app/screens.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "anim_archive.hpp"
#include "app/app.hpp"
#include "app/save_system.hpp"
#include "atlas.hpp"
#include "scene/fight.hpp"
#include "scene/location_scene.hpp"
#include "scene/model.hpp"
#include "scene/renderer.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

namespace {

constexpr float kViewW = 1280.0f;
constexpr float kViewH = 720.0f;

// Draws a flat (untextured) button + its label as a solid quad. The exact
// menu atlas art (ASTC) is unavailable to the CPU pipeline this phase —
// flagged as the exact-layout gap.
void draw_flat_button(App& app, const std::string& label, float cx, float cy, float w, float h,
                      float r, float g, float b, bool hovered) {
    sf2::render::Renderer& ren = app.renderer();
    const float x0 = cx - w / 2.0f;
    const float y0 = cy - h / 2.0f;
    const float x1 = cx + w / 2.0f;
    const float y1 = cy + h / 2.0f;
    const float border = hovered ? 3.0f : 2.0f;
    const float verts_border[] = {
        x0 - border, y0 - border, x1 + border, y0 - border, x1 + border, y1 + border,
        x0 - border, y0 - border, x1 + border, y1 + border, x0 - border, y1 + border,
    };
    ren.draw_triangles(verts_border, 6, 0.1f, 0.1f, 0.12f, 0.9f);
    const float verts_fill[] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
    ren.draw_triangles(verts_fill, 6, r, g, b, 0.92f);
    (void)label;
}

// Loads the map battle nodes from stages.xml (the JS `Ch` parser L1224).
std::vector<MapScreen::Node> load_battle_nodes(float view_w, float view_h) {
    std::vector<MapScreen::Node> out;
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/stages.xml";
        std::ifstream in(path, std::ios::binary);
        if (!in) return out;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root || std::string(root.name()) != "Stages") return out;
        const pugi::xml_node zones = root.child("Zones");
        if (!zones) return out;
        for (const pugi::xml_node zone : zones.children("Zone")) {
            for (const pugi::xml_node battle : zone.children("Battle")) {
                MapScreen::Node n;
                n.name = battle.attribute("Name").value();
                n.type = battle.attribute("Type").value();
                const float x = sf2::data::xml_attr_float(battle, "X", 0.0f);
                const float y = sf2::data::xml_attr_float(battle, "Y", 0.0f);
                n.x = x * 1.0f + view_w / 2.0f;
                n.y = view_h / 2.0f - y * 1.0f;
                n.active = !battle.attribute("Type").empty() ||
                           n.type == "DUMMY" || n.type == "TUTORIAL";
                out.push_back(std::move(n));
            }
            break;  // the tutorial zone (the playable start)
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "map: stages.xml load failed: %s\n", e.what());
    }
    return out;
}

// The reward of a battle's first non-zero <Reward> (JS `tt.bm` L116924).
void battle_rewards(const std::string& battle_name, int& out_money, int& out_exp) {
    out_money = 0;
    out_exp = 0;
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/stages.xml";
        std::ifstream in(path, std::ios::binary);
        if (!in) return;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root) return;
        for (const pugi::xml_node zone : root.child("Zones").children("Zone")) {
            for (const pugi::xml_node battle : zone.children("Battle")) {
                if (std::string(battle.attribute("Name").value()) != battle_name) continue;
                const pugi::xml_node fight = battle.child("Fight");
                if (!fight) return;
                const pugi::xml_node rewards = fight.child("Rewards");
                if (!rewards) return;
                for (const pugi::xml_node reward : rewards.children("Reward")) {
                    const int m = sf2::data::xml_attr_int(reward, "Money", 0);
                    const int e = sf2::data::xml_attr_int(reward, "Exp", 0);
                    if (m > 0 || e > 0) {
                        out_money = m;
                        out_exp = e;
                        return;
                    }
                }
                return;
            }
        }
    } catch (const std::exception&) {
    }
}

// The player's (type, subtype) items for the Locks move list: the equipped
// slots (JS `xc.hk`) + the owned inventory (JS `p.o.xa`).
std::vector<std::pair<std::string, std::string>> owned_items(App& app) {
    std::vector<std::pair<std::string, std::string>> out;
    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return out;
    }
    const std::vector<CatalogItem> catalog = load_full_catalog(app);
    const auto subtype_of = [&catalog](const std::string& name) {
        for (const CatalogItem& ci : catalog) {
            if (ci.name == name) return ci.subtype;
        }
        return std::string();
    };
    for (const std::string& slot : {w.weapon, w.armor, w.helm}) {
        const std::string st = subtype_of(slot);
        if (!st.empty()) {
            for (const CatalogItem& ci : catalog) {
                if (ci.name == slot) {
                    out.emplace_back(ci.type, st);
                    break;
                }
            }
        }
    }
    for (const auto& oi : w.items) {
        if (oi.count <= 0) continue;
        for (const CatalogItem& ci : catalog) {
            if (ci.name == oi.name && !ci.subtype.empty()) {
                out.emplace_back(ci.type, ci.subtype);
                break;
            }
        }
    }
    // The fighter's Skeleton (the Skeleton lock passes for every move — the
    // JS fighter always owns the Skeleton item, `users_default` has
    // Skeleton="Skeleton").
    out.emplace_back("Skeleton", "Skeleton");
    // The default Fists (the unarmed weapon subtype).
    out.emplace_back("Weapon", "Fists");
    return out;
}

} // namespace

// The shared catalog (loaded once, cached).
std::vector<CatalogItem> load_catalog(App& app) {
    static std::vector<CatalogItem> cached;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            const std::string path = "reference/extracted/xml/res/list.xml";
            std::ifstream in(path, std::ios::binary);
            if (in) {
                std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
                const std::vector<CatalogItem> all =
                    parse_item_catalog(std::string(data.begin(), data.end()));
                cached = shop_items(all);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "item catalog load failed: %s\n", e.what());
        }
    }
    (void)app;
    return cached;
}

// The FULL catalog (all items incl. the ShopHide/Hidden base items).
std::vector<CatalogItem> load_full_catalog(App& app) {
    static std::vector<CatalogItem> cached;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            const std::string path = "reference/extracted/xml/res/list.xml";
            std::ifstream in(path, std::ios::binary);
            if (in) {
                std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
                cached = parse_item_catalog(std::string(data.begin(), data.end()));
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "full item catalog load failed: %s\n", e.what());
        }
    }
    (void)app;
    return cached;
}

// ---------------------------------------------------------------------------
// MainMenuScreen
// ---------------------------------------------------------------------------

MainMenuScreen::MainMenuScreen(ScreenManager& mgr) : Screen(mgr, "GeneralMenu") {
    const float bw = 240.0f;
    const float bh = 120.0f;
    const float y = kViewH * 0.72f;
    const float xs[] = {kViewW * 0.28f, kViewW * 0.46f, kViewW * 0.64f, kViewW * 0.82f};
    struct Def {
        const char* label;
        int target;
    };
    const Def defs[] = {
        {"FIGHT", kScreenMap}, {"MAP", kScreenMap}, {"SHOP", kScreenShop}, {"PROFILE", kScreenProfile},
    };
    for (int i = 0; i < 4; ++i) {
        Button b;
        b.label = defs[i].label;
        b.x = xs[i];
        b.y = y;
        b.w = bw;
        b.h = bh;
        b.target = defs[i].target;
        buttons_.push_back(b);
    }
}

void MainMenuScreen::update_impl(float dt) {
    (void)dt;
    if (!money_logged_) {
        money_logged_ = true;
        try {
            const WarriorSave w = app().save().load();
            std::fprintf(stdout, "[menu] MONEY %d   LV %d   POWER %d   WEAPON %s   ARMOR %s   HELM %s\n",
                         w.money, w.level, w.power, w.weapon.c_str(), w.armor.c_str(),
                         w.helm.c_str());
            std::fflush(stdout);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[menu] save read failed: %s\n", e.what());
        }
    }
    const App::PointerState& p = app().pointer();
    hover_ = -1;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const Button& b = buttons_[i];
        if (p.x >= b.x - b.w / 2 && p.x <= b.x + b.w / 2 && p.y >= b.y - b.h / 2 &&
            p.y <= b.y + b.h / 2) {
            hover_ = static_cast<int>(i);
            if (p.pressed) {
                std::fprintf(stdout, "[menu] click %s -> screen %d\n", b.label.c_str(), b.target);
                std::fflush(stdout);
                push(static_cast<ScreenId>(b.target));
            }
        }
    }
    if (hover_ != last_hover_) {
        last_hover_ = hover_;
        if (hover_ >= 0) {
            std::fprintf(stdout, "[menu] hover %s\n", buttons_[hover_].label.c_str());
            std::fflush(stdout);
        }
    }
}

void MainMenuScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        ren.draw_sprite(*dojo, ren.current_camera());
    } else {
        const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(verts, 6, 0.12f, 0.12f, 0.16f, 1.0f);
    }
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const Button& b = buttons_[i];
        const bool hovered = static_cast<int>(i) == hover_;
        const float r = hovered ? 0.85f : (b.target == kScreenMap ? 0.72f : 0.45f);
        const float g = hovered ? 0.72f : (b.target == kScreenMap ? 0.62f : 0.48f);
        const float bl = hovered ? 0.35f : (b.target == kScreenMap ? 0.2f : 0.42f);
        draw_flat_button(app, b.label, b.x, b.y, b.w, b.h, r, g, bl, hovered);
        (void)app.draw_text(b.x, b.y, b.label, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// MapScreen
// ---------------------------------------------------------------------------

MapScreen::MapScreen(ScreenManager& mgr) : Screen(mgr, "Map") {
    nodes_ = load_battle_nodes(kViewW, kViewH);
    std::fprintf(stdout, "[map] %zu battle nodes loaded\n", nodes_.size());
    for (const auto& n : nodes_) {
        std::fprintf(stdout, "[map] node %s (type=%s) at (%.0f, %.0f)%s\n", n.name.c_str(),
                     n.type.c_str(), n.x, n.y, n.active ? "" : " [locked]");
    }
}

void MapScreen::update_impl(float dt) {
    (void)dt;
    const App::PointerState& p = app().pointer();
    // BACK (top-left) -> the main menu (the loop's map -> shop / map ->
    // equipment legs; the JS map has a back/exit control in the top bar).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[map] BACK -> GeneralMenu\n");
            std::fflush(stdout);
            manager().pop();
            return;
        }
    }
    hover_ = -1;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        if (p.x >= n.x - 60 && p.x <= n.x + 60 && p.y >= n.y - 60 && p.y <= n.y + 60) {
            hover_ = static_cast<int>(i);
            if (p.pressed && n.active) {
                // JS `Ya` battle-start (L2131-2132): `wa.F().mp(6, battle)`.
                // Carry the battle into the pending flow: name/location +
                // the reward (the first non-zero <Reward>).
                PendingBattle& pb = app().pending_battle();
                pb.battle_name = n.name;
                pb.location = "dojo";
                pb.has_result = false;
                // The node's fight -> reward. The Training fight has
                // Money=0; use the battle's own reward lookup.
                battle_rewards(n.name, pb.reward_money, pb.reward_exp);
                // The owned items (JS `ra.Hza` move list input).
                pb.owned = owned_items(app());
                std::fprintf(stdout, "[map] click %s -> Fight (reward money=%d exp=%d)\n",
                             n.name.c_str(), pb.reward_money, pb.reward_exp);
                std::fflush(stdout);
                push(kScreenFight);
            }
        }
    }
}

void MapScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        ren.draw_sprite(*dojo, ren.current_camera());
    } else {
        const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(verts, 6, 0.08f, 0.1f, 0.14f, 1.0f);
    }
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        const bool hovered = static_cast<int>(i) == hover_;
        const float r = hovered ? 0.9f : (n.active ? 0.6f : 0.3f);
        const float g = hovered ? 0.5f : (n.active ? 0.4f : 0.3f);
        const float b = hovered ? 0.3f : (n.active ? 0.25f : 0.3f);
        const float d = hovered ? 66.0f : 60.0f;
        const float x0 = n.x - d / 2, y0 = n.y - d / 2;
        const float verts[] = {x0, y0, x0 + d, y0, x0 + d, y0 + d, x0, y0, x0 + d, y0 + d, x0, y0 + d};
        ren.draw_triangles(verts, 6, r, g, b, n.active ? 0.95f : 0.5f);
    }
    // The BACK button (top-left).
    draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
}

// ---------------------------------------------------------------------------
// FightScreen
// ---------------------------------------------------------------------------

FightScreen::FightScreen(ScreenManager& mgr, const std::string& battle_name,
                         const std::string& location, int reward_money, int reward_exp,
                         const std::vector<std::pair<std::string, std::string>>& owned)
    : Screen(mgr, "Fight"),
      battle_name_(battle_name),
      location_(location),
      reward_money_(reward_money),
      reward_exp_(reward_exp) {
    FightAssets& assets = app().fight_assets();
    std::fprintf(stdout, "[fight] battle=%s location=%s reward money=%d exp=%d\n",
                 battle_name_.c_str(), location_.c_str(), reward_money_, reward_exp_);
    std::fflush(stdout);

    // The dojo location (the tutorial-zone backdrop). Load once; the
    // FightAssets keeps it.
    if (assets.dojo.layers().empty()) {
        const std::string loc_dir = app().res_root() + "/locations/" + location_;
        std::string params_xml, atlas_json;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(loc_dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(location_ + "_params.", 0) == 0 &&
                    name.size() > 4 && name.substr(name.size() - 4) == ".xml") {
                    params_xml = entry.path().string();
                } else if (name.rfind(location_ + ".", 0) == 0 &&
                           name.size() > 5 && name.substr(name.size() - 5) == ".json") {
                    atlas_json = entry.path().string();
                }
            }
            assets.dojo.load(params_xml, {atlas_json}, app().res_root());
            // [FIX Phase 4a/4b - dojo texture resolve] The atlas texture for
            // the location is the IMAGE beside the JSON (`dojo.b920e18e.webp`),
            // NOT the JSON path - the JSON's hash stem (`dojo.d31b1e71`)
            // does not match the image's (`dojo.b920e18e`). The old code
            // derived the image path from the JSON stem
            // (`dojo.d31b1e71.webp`, does not exist) -> no texture uploaded
            // -> every location sprite rendered as a black solid -> the whole
            // dojo black + the black fighters invisible. Resolve the image by
            // the LOCATION prefix (`dojo.*`) like the scene_probe.
            const std::string loc_prefix = location_ + ".";
            for (const auto& entry : std::filesystem::directory_iterator(loc_dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(loc_prefix, 0) != 0) continue;
                const std::string ext = entry.path().extension().string();
                if (ext != ".webp" && ext != ".png" && ext != ".jpg") continue;
                sf2::data::Texture tex;
                if (!sf2::data::decode_texture(entry.path().string(), tex)) continue;
                const GLuint gl = app().renderer().texture_for("dojo_atlas_" + name, tex);
                if (gl != 0) {
                    std::ifstream in(atlas_json, std::ios::binary);
                    std::vector<std::uint8_t> jb(
                        (std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
                    const sf2::data::atlas a =
                        sf2::data::atlas_parse(jb.data(), jb.size());
                    for (const auto& fr : a.frames) {
                        app().renderer().texture_alias(fr.name, gl);
                    }
                    std::fprintf(stdout, "[fight] dojo atlas texture: %s (%dx%d, %zu frames)\n",
                                 entry.path().filename().string().c_str(), tex.w, tex.h,
                                 a.frames.size());
                }
                break;  // one atlas image per location
            }
            std::fprintf(stdout, "[fight] dojo scene: %zu layers, arena %.0fx%.0f\n",
                         assets.dojo.layers().size(), assets.dojo.arena_width(),
                         assets.dojo.arena_height());
            // [FIX Phase 4b — the floor the fighters stand on] The dojo's
            // `dojo_floor_*` atlas sprites are white frames tinted black by
            // the params `Color="0x000000"` — they render as a pure-black
            // strip where the fighters stand, making the black silhouettes
            // invisible ("no body"). The oracle's fighter-zone floor is a
            // warm wooden floor (~0xC77946); tint the floor sprites warm so
            // the black fighters are visible on it.
            for (const auto& layer : assets.dojo.layers()) {
                for (const auto& s : layer->sprites) {
                    if (s->texture_name.rfind("dojo_floor_", 0) == 0) {
                        s->color_r = 0xC7 / 255.0f;
                        s->color_g = 0x79 / 255.0f;
                        s->color_b = 0x46 / 255.0f;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[fight] dojo scene load failed: %s\n", e.what());
        }
    }

    const float arena_w = assets.dojo.arena_width() > 0.0f ? assets.dojo.arena_width() : 1960.0f;
    const float wall = 80.0f;
    // [FIX Phase 4b — fighters stay in the arena] The arena is centered on
    // the location origin (the params Width/2); the walls sit at
    // ±(arena_w/2 - wall). The old `wall_max = arena_w - wall` (1880) let
    // the fighters walk far past the right wall (world 900) into the void —
    // the enemy AI wandered to x=1244 and stood on black. Bound the fighters
    // to the actual arena walls (±900).
    const float wall_min = -(arena_w * 0.5f - wall);
    const float wall_max = arena_w * 0.5f - wall;

    sf2::scene::BattleParams battle;
    battle.name = battle_name_;
    battle.type = "FightNone";
    battle.location = location_;
    battle.rounds = 2;
    battle.round_time = 99;
    battle.health_recovery = 1.0f;
    // [FIX Phase 4a — spawn sides] The dojo_params ModelsViewer places
    // PlayerPositionX=690 (left) / EnemyPositionX=973 (right) — but the
    // ORACLE trace (reference/traces/console.log) shows the PLAYER at the
    // RIGHT (P:972.954) and the ENEMY (Punchbag) at the LEFT (E:690.000):
    //   F0|1|0|0|P:972.954,-108.114,1,,1,NAME_SHADOW|E:690.000,-93.000,1,,1,Punchbag
    // The ModelsViewer "Player"/"Enemy" labels are reversed relative to
    // the fight roles (the viewer's "Player" = the left Punchbag = the
    // fight ENEMY). The old code spawned the player LEFT — the fighters
    // appeared on the wrong sides ("in nowhere" + mirrored).
    // [FIX Phase 4a — fighters on the floor] The spawn Y is the ModelsViewer
    // COM y; the native anchors the fighter by its clip ground-contact (see
    // Fighter::sample) so the feet land at the given world y. The dojo's
    // VISIBLE floor is the dojo_floor sprite line (world Y=223.5) — the
    // fighters stand on it (feet at 223.5 -> the floor sprite row). The
    // params Floor attr (80) is the arena's physics line, NOT the visible
    // wooden floor.
    const float floor_y = 223.5f;
    battle.player_spawn_x = 973.0f;
    battle.player_spawn_y = floor_y;   // the ground line (visible dojo floor)
    battle.enemy_spawn_x = 690.0f;
    battle.enemy_spawn_y = floor_y;
    battle.max_hp = 1;  // the game's HP fallback (Zn = aB>0 ? aB : 1)
    battle.player_unarmed_damage = 80.0f;

    const sf2::scene::TacticDef* tactic = nullptr;
    const auto it = assets.tactic_defs.find("Standard");
    if (it != assets.tactic_defs.end()) tactic = &it->second;

    static std::mt19937 s_rng(0x5F2);
    std::mt19937& rng = s_rng;
    auto roll01 = [&rng]() {
        return static_cast<float>(rng()) / static_cast<float>(rng.max());
    };

    fight_ = std::make_unique<sf2::scene::FightController>();
    fight_->init_locks(battle, assets.merged, assets.moves, assets.clips,
                       assets.tactics_sets, tactic, "Player", "Enemy",
                       battle.player_spawn_x, battle.player_spawn_y,
                       battle.enemy_spawn_x, battle.enemy_spawn_y,
                       battle.max_hp, battle.max_hp, roll01, owned);
    fight_->set_bounds(wall_min, wall_max, floor_y);  // the visible dojo floor (the camera anchor)
    // [FIX Phase 4b — black silhouettes] The fighters' mesh color is the
    // location's Root Color (the dojo_params `<Root Color="0x000000">`,
    // JS `Na.cd` fills the fighter Path2D with it). The oracle's fighters
    // are black silhouettes — no red/blue team colors.
    fight_->set_fighter_color(assets.dojo.root_color());
    std::fprintf(stdout, "[fight] fighter color 0x%06X (location %s Root Color)\n",
                 assets.dojo.root_color(), location_.c_str());

    // Log the player's move list (the equipment-change evidence).
    std::fprintf(stdout, "[fight] player move list (%zu moves):\n",
                 fight_->player().fighter.hb().size());
    for (const auto* m : fight_->player().fighter.hb()) {
        std::fprintf(stdout, "  %s\n", m->name.c_str());
    }
    std::fflush(stdout);
}

void FightScreen::on_key(int glfw_key, bool down) {
    // GLFW key codes -> the game's key_type (JS `Ik` keyboard events ->
    // the fight input; the move Keys conditions read Punch/Forward/Back).
    // Bindings (reasonable desktop keys):
    //   A/Left = Back, D/Right = Forward, W/Up = Jump(up),
    //   Space/J = Punch, L = Kick, S/Down = Crouch(down).
    // K/B = Super (the Fists moveset has no Super-key moves; bound for the
    // weapon movesets that do). Blocking is NOT a raw key in this game: the
    // fighter blocks while any move's `Block` interval is active (e.g. the
    // HighPunch recovery), so attacking/stepping with the keys above also
    // provides the block window.
    sf2::scene::key_type kt = static_cast<sf2::scene::key_type>(0);
    switch (glfw_key) {
        case 65: case 263: kt = sf2::scene::key_type::back; break;     // A / Left
        case 68: case 262: kt = sf2::scene::key_type::forward; break;  // D / Right
        case 87: case 265: kt = sf2::scene::key_type::up; break;       // W / Up (Jump)
        case 83: case 264: kt = sf2::scene::key_type::down; break;     // S / Down (Crouch)
        case 32: case 74: kt = sf2::scene::key_type::punch; break;     // Space / J
        case 76: kt = sf2::scene::key_type::kick; break;               // L
        case 75: case 66: kt = sf2::scene::key_type::super; break;     // K / B (special)
        default: return;
    }
    const int idx = static_cast<int>(kt);
    if (idx < 0 || idx >= 16) return;
    key_state_[idx] = down;
    if (fight_) {
        fight_->player_input(kt, down ? sf2::scene::press_type::tap
                                      : sf2::scene::press_type::release);
    }
}

std::size_t FightScreen::move_list_size() const {
    return fight_ != nullptr ? fight_->player().fighter.hb().size() : 0;
}

// [trace, Phase 0] Arms the FightController's per-frame pose dump (the
// pose/camera trace the JS-side oracle dump is diffed against).
void FightScreen::enable_pose_dump(const std::string& path, int frames) {
    if (fight_ != nullptr) {
        fight_->set_pose_dump(path, frames);
    }
}

// [FIX Phase 4a verification] Bone-sample check: the sampled bone positions
// (the Fighter::positions() after sample) for a few skeleton bones vs the
// clip data, plus the triangle bbox (humanoid shape + on-screen check).
void FightScreen::verify_fight() const {
    if (fight_ == nullptr) return;
    const sf2::scene::FightCamera& cam = fight_->camera();
    std::fprintf(stdout, "[verify] camera center=(%.1f, %.1f) zoom=%.3f\n",
                 cam.center_x, cam.center_y, cam.zoom);
    std::fprintf(stdout, "[verify] player world x=%.1f enemy world x=%.1f\n",
                 fight_->player().fighter.world_x(), fight_->enemy().fighter.world_x());
    // [FIX Phase 4b — screen positions] The fighters' feet (the spawn floor
    // line) project with the SAME camera the render used.
    {
        const sf2::app::FightAssets& assets = app().fight_assets();
        sf2::render::Camera c;
        c.center_x = cam.center_x;
        c.center_y = cam.center_y;
        c.zoom = cam.zoom;
        c.view_w = 1280.0f;
        c.view_h = 720.0f;
        c.arena_h = assets.dojo.arena_height() > 0.0f ? assets.dojo.arena_height() : 560.0f;
        c.arena_floor = assets.dojo.arena_floor();
        c.arena_center_x = assets.dojo.arena_width() * 0.5f;
        const float feet_y = fight_->player().fighter.world_y();
        const float psx = c.world_to_screen_x(fight_->player().fighter.world_x(), 0.0f);
        const float psy = c.world_to_screen_y(feet_y);
        const float esx = c.world_to_screen_x(fight_->enemy().fighter.world_x(), 0.0f);
        const float esy = c.world_to_screen_y(feet_y);
        std::fprintf(stdout, "[verify] screen: player feet=(%.0f, %.0f) enemy feet=(%.0f, %.0f)\n",
                     psx, psy, esx, esy);
    }
    auto report = [](const char* who, const sf2::scene::Fighter& f) {
        const sf2::scene::Model& m = f.model();
        const std::vector<float>& pos = f.positions();
        const char* bones[4] = {"COM", "NTop", "NAnkle_2", "NHeadF"};
        std::fprintf(stdout, "[verify] %s bone sample (world):\n", who);
        for (int b = 0; b < 4; ++b) {
            const int idx = m.bone_by_name(bones[b]);
            if (idx < 0 || static_cast<std::size_t>(idx) * 2 + 1 >= pos.size()) continue;
            std::fprintf(stdout, "  %s = (%.1f, %.1f)\n", bones[b],
                         pos[static_cast<std::size_t>(idx) * 2],
                         pos[static_cast<std::size_t>(idx) * 2 + 1]);
        }
        // [Phase 4d debug] The key skeleton bones for the capsule strip.
        static const char* kCaps[14] = {"NHead", "NTop", "NNeck", "NChest", "NStomach",
                                        "NHip_1", "NHip_2", "NKnee_1", "NKnee_2",
                                        "NAnkle_1", "NAnkle_2", "NToe_1", "NToe_2", "NHeel_1"};
        std::fprintf(stdout, "[verify] %s capsule bones (world):\n", who);
        for (int b = 0; b < 14; ++b) {
            const int idx = m.bone_by_name(kCaps[b]);
            if (idx < 0 || static_cast<std::size_t>(idx) * 2 + 1 >= pos.size()) continue;
            std::fprintf(stdout, "  %-10s = (%.1f, %.1f)\n", kCaps[b],
                         pos[static_cast<std::size_t>(idx) * 2],
                         pos[static_cast<std::size_t>(idx) * 2 + 1]);
        }
        // [Phase 4d debug] The BODY-Node* cloth nodes' world positions (the
        // body-mesh leg coverage check).
        static const char* kCloth[8] = {"BODY-Node16", "BODY-Node11", "BODY-Node15",
                                        "BODY-Node12", "BODY-Node17", "BODY-Node18",
                                        "BODY-Node20", "BODY-Node19"};
        std::fprintf(stdout, "[verify] %s BODY-Node cloth (world):\n", who);
        for (int b = 0; b < 8; ++b) {
            const int idx = m.bone_by_name(kCloth[b]);
            if (idx < 0 || static_cast<std::size_t>(idx) * 2 + 1 >= pos.size()) continue;
            std::fprintf(stdout, "  %s = (%.1f, %.1f)\n", kCloth[b],
                         pos[static_cast<std::size_t>(idx) * 2],
                         pos[static_cast<std::size_t>(idx) * 2 + 1]);
        }
        float min_x, min_y, max_x, max_y;
        f.triangle_bbox(min_x, min_y, max_x, max_y);
        const float bw = max_x - min_x, bh = max_y - min_y;
        std::fprintf(stdout, "[verify] %s tri-bbox: (%.1f, %.1f)-(%.1f, %.1f) "
                             "w=%.1f h=%.1f ratio=%.2f\n",
                     who, min_x, min_y, max_x, max_y, bw, bh,
                     bh > 0.0f ? bw / bh : 0.0f);
        // The widest triangle span (the stretched-mesh check).
        float widest = 0.0f;
        for (const sf2::scene::TriResolved& tri : m.resolved_tris) {
            const float x1 = pos[static_cast<std::size_t>(tri.i1) * 2];
            const float y1 = pos[static_cast<std::size_t>(tri.i1) * 2 + 1];
            const float x2 = pos[static_cast<std::size_t>(tri.i2) * 2];
            const float y2 = pos[static_cast<std::size_t>(tri.i2) * 2 + 1];
            const float x3 = pos[static_cast<std::size_t>(tri.i3) * 2];
            const float y3 = pos[static_cast<std::size_t>(tri.i3) * 2 + 1];
            float sx = std::fabs(x1 - x2);
            if (std::fabs(x2 - x3) > sx) sx = std::fabs(x2 - x3);
            if (std::fabs(x3 - x1) > sx) sx = std::fabs(x3 - x1);
            float sy = std::fabs(y1 - y2);
            if (std::fabs(y2 - y3) > sy) sy = std::fabs(y2 - y3);
            if (std::fabs(y3 - y1) > sy) sy = std::fabs(y3 - y1);
            if (sx + sy > widest) widest = sx + sy;
        }
        std::fprintf(stdout, "[verify] %s widest-tri-span=%.1f\n", who, widest);
        // On-screen check: the bbox center within the 1280x720 view.
        const float cx = (min_x + max_x) * 0.5f, cy = (min_y + max_y) * 0.5f;
        std::fprintf(stdout, "[verify] %s on-screen: center=(%.0f, %.0f) %s\n", who, cx, cy,
                     (cx >= 0 && cx <= 1280 && cy >= 0 && cy <= 720) ? "OK" : "OFF-SCREEN");
    };
    report("player", fight_->player().fighter);
    report("enemy", fight_->enemy().fighter);
    std::fflush(stdout);
}
void FightScreen::update_impl(float dt) {
    if (fight_ == nullptr) return;
    if (!auto_attack_wired_) {
        auto_attack_wired_ = true;
        if (app().auto_attack()) {
            fight_->set_auto_attack(true);
            std::fprintf(stdout, "[fight] auto-attack ON\n");
        }
    }
    fight_->update(dt);

    // Per-second log.
    if (fight_->frame() / 60 != last_log_frame_) {
        last_log_frame_ = fight_->frame() / 60;
        const int timer = fight_->round().gma -
                          static_cast<int>(std::ceil(fight_->round().time));
        std::fprintf(stdout, "[fight] F%d phase=%d round=%d timer=%d P:%.0f (%s) E:%.0f (%s)\n",
                     fight_->frame(), fight_->phase(), fight_->round().number,
                     std::max(0, timer), fight_->player().hp,
                     fight_->player().last_move.empty() ? "idle" : fight_->player().last_move.c_str(),
                     fight_->enemy().hp,
                     fight_->enemy().last_move.empty() ? "idle" : fight_->enemy().last_move.c_str());
        std::fflush(stdout);
    }

    // Battle end -> Results (JS `bea` L413 -> `v.kD` L622187 -> the
    // results; `qxa` L1213 pops back to the map).
    if (fight_->battle_over() && !results_pushed_) {
        results_pushed_ = true;
        const bool player_won = fight_->winner() != nullptr && fight_->winner()->is_player;
        PendingBattle& pb = app().pending_battle();
        pb.has_result = true;
        pb.player_won = player_won;
        std::fprintf(stdout, "[fight] BATTLE END winner=%s player_won=%d\n",
                     fight_->winner() ? fight_->winner()->name.c_str() : "(none)", player_won);
        std::fprintf(stdout,
                     "[fight] summary: P hp=%.0f rounds=%d hits=%d | E hp=%.0f rounds=%d hits=%d\n",
                     fight_->player().hp, fight_->player().rounds_won,
                     fight_->player().hits_landed, fight_->enemy().hp,
                     fight_->enemy().rounds_won, fight_->enemy().hits_landed);
        std::fflush(stdout);
        push(kScreenResults);
    }
}

void FightScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    if (fight_ == nullptr) return;
    FightAssets& assets = app.fight_assets();

    const sf2::scene::FightCamera& cam = fight_->camera();
    sf2::render::Camera camera;
    camera.center_x = cam.center_x;
    camera.center_y = cam.center_y;
    camera.zoom = cam.zoom;
    camera.view_w = kViewW;
    camera.view_h = kViewH;
    camera.arena_h = assets.dojo.arena_height() > 0.0f ? assets.dojo.arena_height() : 560.0f;
    camera.arena_floor = assets.dojo.arena_floor();
    // [FIX Phase 4a — dojo behind the fighters] The arena center (the
    // parallax Io reference) is the LOCATION's center = Width/2 (the
    // dojo_params Width=1960 -> 980), NOT 0. With arena_center_x=0 the
    // camera at the fighters (center 831) gave Io = -831 and every
    // background layer shifted 831*Factor px LEFT — the mountains/temple/
    // bridge/tree layers landed entirely off-screen and only the sky
    // (factor 0.4) + the walls (factor 1) were visible. With the arena
    // center at 980, Io = +149 -> the parallax is a small, correct shift.
    camera.arena_center_x = assets.dojo.arena_width() * 0.5f;
    ren.begin_frame(camera);
    for (const auto& layer : assets.dojo.layers()) {
        assets.dojo.render_layer(ren, *layer, camera);
    }

    auto project = [&camera](const std::vector<float>& v) {
        std::vector<float> out(v.size());
        for (std::size_t i = 0; i < v.size(); i += 2) {
            out[i] = camera.world_to_screen_x(v[i], 0.0f);
            out[i + 1] = camera.world_to_screen_y(v[i + 1]);
        }
        return out;
    };
    std::vector<float> verts, pv, ev;
    fight_->player().fighter.build_vertices(verts);
    pv = project(verts);
    fight_->enemy().fighter.build_vertices(verts);
    ev = project(verts);
    // [Phase 4d] The oracle renders the fighter as the ragdoll capsule
    // STRIP: every collidable edge is a stroked line (JS `Dk` node:
    // `add(b,e,c,a,stroke/2)` with `stroke = Radius1*2`, drawn by the
    // `zu` class — see sf2.502f0946.js `class zu` + `class Dk`). The
    // triangle mesh alone (mdl_body = legs/feet only) leaves the torso
    // (EChest/EStomach) EMPTY — the user's "no armor/torso" report. Draw
    // the collidable capsule edges as thick quads over the mesh so the
    // fighter is a solid humanoid silhouette (head/neck/chest/stomach/
    // arms/legs) matching the oracle.
    auto draw_capsules = [&camera, &ren](const sf2::scene::FightFighter& f) {
        const float r = f.fighter.color_r(), g = f.fighter.color_g(), b = f.fighter.color_b();
        // [Phase 4d — capsule-figure render] The oracle draws the fighter's
        // body from the merged model's CAPSULE FIGURES (JS `Yc.Tib`: every
        // `<Capsule_* Type="Capsule" Radius1=".." Edge="..">` becomes a `zu`
        // visual node -> a `Dk` stroked line, stroke = Radius1*2). The
        // capsule figures live in the armor/body/helm models (mdl_body has
        // EChest/EStomach/EThigh/ECalf/EArm..., mdl_head has EHead/ENeck).
        // Each capsule's Edge resolves to the edge's two endpoint bones;
        // the stroke width comes from Radius1 (NOT the edge's collidable
        // radius, which is the physics radius).
        const sf2::scene::Model& model = f.fighter.model();
        for (const sf2::scene::Capsule& cap : model.capsules) {
            // Find the edge by name.
            const sf2::scene::EdgeDef* edge = nullptr;
            for (const sf2::scene::EdgeDef& ed : model.edges) {
                if (ed.name == cap.edge) { edge = &ed; break; }
            }
            if (edge == nullptr) continue;
            const int i1 = model.bone_by_name(edge->end1);
            const int i2 = model.bone_by_name(edge->end2);
            if (i1 < 0 || i2 < 0) continue;
            const std::vector<float>& pos = f.fighter.positions();
            const std::size_t u1 = static_cast<std::size_t>(i1) * 2;
            const std::size_t u2 = static_cast<std::size_t>(i2) * 2;
            if (u1 + 1 >= pos.size() || u2 + 1 >= pos.size()) continue;
            const float stroke = cap.radius1 * 2.0f * camera.zoom;
            if (stroke <= 0.0f) continue;
            const float sx1 = camera.world_to_screen_x(pos[u1], 0.0f);
            const float sy1 = camera.world_to_screen_y(pos[u1 + 1]);
            const float sx2 = camera.world_to_screen_x(pos[u2], 0.0f);
            const float sy2 = camera.world_to_screen_y(pos[u2 + 1]);
            float dx = sx2 - sx1, dy = sy2 - sy1;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-4f) continue;
            dx /= len; dy /= len;
            const float px = -dy * stroke * 0.5f;
            const float py = dx * stroke * 0.5f;
            float quad[12] = {
                sx1 + px, sy1 + py, sx2 + px, sy2 + py,
                sx1 - px, sy1 - py, sx2 + px, sy2 + py,
                sx2 - px, sy2 - py, sx1 - px, sy1 - py,
            };
            ren.draw_triangles(quad, 6, r, g, b, 1.0f);
        }
    };
    draw_capsules(fight_->player());
    draw_capsules(fight_->enemy());
    ren.draw_triangles(pv.data(), pv.size() / 2, fight_->player().fighter.color_r(),
                       fight_->player().fighter.color_g(), fight_->player().fighter.color_b());
    ren.draw_triangles(ev.data(), ev.size() / 2, fight_->enemy().fighter.color_r(),
                       fight_->enemy().fighter.color_g(), fight_->enemy().fighter.color_b());

    // The fight HUD (flat bars).
    auto draw_bar = [&](float x, float y, float w, float h, float ratio,
                        std::uint32_t color) {
        float verts[12] = {x, y, x + w * ratio, y, x, y + h,
                           x + w * ratio, y, x + w * ratio, y + h, x, y + h};
        ren.draw_triangles(verts, 6, ((color >> 16) & 0xFF) / 255.0f,
                           ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f, 0.95f);
    };
    const float bar_w = 440.0f, bar_h = 16.0f, bar_y = 60.0f;
    const float p_ratio =
        fight_->player().max_hp > 0.0f ? fight_->player().hp / fight_->player().max_hp : 0.0f;
    const float e_ratio =
        fight_->enemy().max_hp > 0.0f ? fight_->enemy().hp / fight_->enemy().max_hp : 0.0f;
    draw_bar(60.0f, bar_y, bar_w, bar_h, p_ratio, 0x20D020);
    draw_bar(kViewW - 60.0f - bar_w, bar_y, bar_w, bar_h, e_ratio, 0x4020E0);

    const int timer =
        fight_->round().gma - static_cast<int>(std::ceil(fight_->round().time));
    const std::string tstr = std::to_string(std::max(0, timer));
    float tx = kViewW * 0.5f - tstr.size() * 20.0f;
    for (char ch : tstr) {
        float verts[12] = {tx, 30.0f, tx + 18.0f, 30.0f, tx, 58.0f,
                           tx + 18.0f, 30.0f, tx + 18.0f, 58.0f, tx, 58.0f};
        ren.draw_triangles(verts, 6, 1.0f, 1.0f, 1.0f, 0.95f);
        tx += 22.0f;
        (void)ch;
    }

    const int rounds_total = fight_->round().length;
    for (int i = 0; i < rounds_total; ++i) {
        const bool p_done = i < fight_->player().rounds_won;
        const float px = 90.0f + static_cast<float>(i) * 26.0f;
        float dv[12] = {px, 86.0f, px + 16.0f, 86.0f, px, 102.0f,
                        px + 16.0f, 86.0f, px + 16.0f, 102.0f, px, 102.0f};
        ren.draw_triangles(dv, 6, p_done ? 0.2f : 0.35f, p_done ? 0.8f : 0.35f,
                           p_done ? 0.2f : 0.35f, 1.0f);
        const float ex = kViewW - 90.0f - static_cast<float>(rounds_total - 1 - i) * 26.0f;
        float ev2[12] = {ex, 86.0f, ex + 16.0f, 86.0f, ex, 102.0f,
                         ex + 16.0f, 86.0f, ex + 16.0f, 102.0f, ex, 102.0f};
        ren.draw_triangles(ev2, 6, 0.35f, 0.8f, 0.35f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// ResultsScreen
// ---------------------------------------------------------------------------

ResultsScreen::ResultsScreen(ScreenManager& mgr, bool player_won, int money_reward,
                             int exp_reward)
    : Screen(mgr, "Results"), player_won_(player_won), money_reward_(money_reward),
      exp_reward_(exp_reward) {}

void ResultsScreen::update_impl(float dt) {
    (void)dt;
    if (!applied_) {
        applied_ = true;
        WarriorSave w;
        try {
            w = app().save().load();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[result] save load failed: %s\n", e.what());
            return;
        }
        if (player_won_) {
            // JS `dmb` -> `emb` (L93552): Money -> `Pa.Fwa` (Tb += money),
            // Exp -> `Pa.Iab` -> `p.o.Jab` (XP).
            const int before = w.money;
            w.money += money_reward_;
            w.experience += exp_reward_;
            std::fprintf(stdout, "[result] WIN reward money=%d exp=%d (money %d -> %d)\n",
                         money_reward_, exp_reward_, before, w.money);
            while (w.experience >= 100 && w.level < 50) {
                w.experience -= 100;
                w.level++;
                w.power += 2;
                std::fprintf(stdout, "[result] LEVEL UP -> %d (power %d)\n", w.level, w.power);
            }
        } else {
            std::fprintf(stdout, "[result] LOSS (no reward)\n");
        }
        try {
            app().save().save(w);
            std::fprintf(stdout, "[result] save: money=%d exp=%d level=%d weapon=%s\n", w.money,
                         w.experience, w.level, w.weapon.c_str());
            std::fflush(stdout);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[result] save failed: %s\n", e.what());
        }
    }
    const App::PointerState& p = app().pointer();
    if (p.pressed) {
        std::fprintf(stdout, "[result] click -> back to Map\n");
        std::fflush(stdout);
        // JS `qxa` (L1213) pops back to the map: the Results screen sits on
        // top of the Fight screen it replaced, so both pop (the fight is
        // done; the map is the caller the flow returns to).
        manager().pop();
        manager().pop();
    }
}

void ResultsScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(verts, 6, 0.0f, 0.0f, 0.0f, 0.6f);
    std::fprintf(stdout, "[result] %s\n", player_won_ ? "WIN" : "LOSS");
}

// ---------------------------------------------------------------------------
// ShopScreen
// ---------------------------------------------------------------------------

ShopScreen::ShopScreen(ScreenManager& mgr) : Screen(mgr, "Shop") {
    items_ = load_catalog(app());
    std::fprintf(stdout, "[shop] %zu shop items\n", items_.size());
    for (const auto& it : items_) {
        std::fprintf(stdout, "[shop] item %s (%s) price=%d model=%s\n", it.name.c_str(),
                     it.subtype.empty() ? it.type.c_str() : it.subtype.c_str(), it.price,
                     it.model.c_str());
    }
    std::fflush(stdout);
}

void ShopScreen::update_impl(float dt) {
    (void)dt;
    const App::PointerState& p = app().pointer();
    try {
        const WarriorSave w = app().save().load();
        if (w.money != money_logged_) {
            money_logged_ = w.money;
            std::fprintf(stdout, "[shop] MONEY %d\n", w.money);
            std::fflush(stdout);
        }
    } catch (const std::exception&) {
    }
    hover_ = -1;
    // BACK (top-left) -> the main menu (the loop's shop -> menu leg).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[shop] BACK -> GeneralMenu\n");
            std::fflush(stdout);
            manager().pop();
            return;
        }
    }
    const float card_w = 300.0f, card_h = 150.0f;
    const float x0 = kViewW * 0.25f, y0 = 200.0f;
    const float dx = 330.0f, dy = 170.0f;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const int col = static_cast<int>(i % 2);
        const int row = static_cast<int>(i / 2);
        const float cx = x0 + col * dx;
        const float cy = y0 + row * dy;
        if (p.x >= cx - card_w / 2 && p.x <= cx + card_w / 2 && p.y >= cy - card_h / 2 &&
            p.y <= cy + card_h / 2) {
            hover_ = static_cast<int>(i);
            if (p.pressed) {
                const CatalogItem& it = items_[i];
                WarriorSave w;
                try {
                    w = app().save().load();
                } catch (const std::exception&) {
                    break;
                }
                // JS `Pa.iwa` (L629626): money check `p.o.Tb >= a.jp()`,
                // deduct `p.o.Fr(b)`, add `Pa.gI` (L628934) -> inventory.
                if (w.has_item(it.name)) {
                    std::fprintf(stdout, "[shop] %s already owned\n", it.name.c_str());
                    std::fflush(stdout);
                } else if (w.money >= it.price) {
                    w.money -= it.price;
                    WarriorSave::OwnedItem oi;
                    oi.name = it.name;
                    oi.count = 1;
                    w.items.push_back(oi);
                    app().save().save(w);
                    std::fprintf(stdout,
                                 "[shop] BOUGHT %s (%s) price=%d -> money %d, item added\n",
                                 it.name.c_str(), it.subtype.c_str(), it.price, w.money);
                    std::fflush(stdout);
                } else {
                    std::fprintf(stdout, "[shop] NOT ENOUGH MONEY for %s (need %d, have %d)\n",
                                 it.name.c_str(), it.price, w.money);
                    std::fflush(stdout);
                }
            }
        }
    }
}

void ShopScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        ren.draw_sprite(*dojo, ren.current_camera());
    }
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.35f);

    const float card_w = 300.0f, card_h = 150.0f;
    const float x0 = kViewW * 0.25f, y0 = 200.0f;
    const float dx = 330.0f, dy = 170.0f;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const int col = static_cast<int>(i % 2);
        const int row = static_cast<int>(i / 2);
        const float cx = x0 + col * dx;
        const float cy = y0 + row * dy;
        const bool hovered = static_cast<int>(i) == hover_;
        const float r = hovered ? 0.75f : 0.45f;
        const float g = hovered ? 0.6f : 0.35f;
        const float b = hovered ? 0.3f : 0.2f;
        draw_flat_button(app, items_[i].name, cx, cy, card_w, card_h, r, g, b, hovered);
    }
    // The BACK button (top-left).
    draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
}

// ---------------------------------------------------------------------------
// EquipmentScreen
// ---------------------------------------------------------------------------

EquipmentScreen::EquipmentScreen(ScreenManager& mgr) : Screen(mgr, "Equipment") {
    // The FULL catalog — the owned base items (Body/Head/Fists) are
    // ShopHide/Hidden and absent from the shop-visible list; the grid
    // must resolve their type/subtype to place the cards.
    catalog_ = load_full_catalog(app());
}

void EquipmentScreen::update_impl(float dt) {
    (void)dt;
    const App::PointerState& p = app().pointer();
    WarriorSave w;
    try {
        w = app().save().load();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[equip] save load failed: %s\n", e.what());
        return;
    }
    hover_ = -1;
    // BACK (top-left) -> the main menu (the loop's equipment -> menu leg).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[equip] BACK -> GeneralMenu\n");
            std::fflush(stdout);
            manager().pop();
            return;
        }
    }
    // The owned items grid: click to equip into its type's slot. `card`
    // counts only the Weapon/Armor/Helm cards (NoRanged/NoMagic etc. are
    // filtered out and must NOT consume a grid slot — the JS profile grid
    // shows only the equippable item types).
    const float grid_x = kViewW * 0.55f, grid_y0 = 220.0f, grid_dx = 240.0f, grid_dy = 110.0f;
    int idx = 0;
    int card = 0;
    for (const auto& oi : w.items) {
        std::string type, subtype;
        for (const CatalogItem& ci : catalog_) {
            if (ci.name == oi.name) {
                type = ci.type;
                subtype = ci.subtype;
                break;
            }
        }
        if (type != "Weapon" && type != "Armor" && type != "Helm") {
            ++idx;
            continue;
        }
        const int col = card % 2;
        const int row = card / 2;
        const float cx = grid_x + col * grid_dx;
        const float cy = grid_y0 + row * grid_dy;
        if (p.x >= cx - 110 && p.x <= cx + 110 && p.y >= cy - 40 && p.y <= cy + 40) {
            hover_ = idx;
            if (p.pressed) {
                // JS `$g.$o` (L152184): `p.o.Ca.hk(a.type, a)` sets the
                // slot, `setItem`, `save()`.
                WarriorSave w2 = app().save().load();
                std::string* slot_val = nullptr;
                if (type == "Weapon") slot_val = &w2.weapon;
                else if (type == "Armor") slot_val = &w2.armor;
                else slot_val = &w2.helm;
                *slot_val = oi.name;
                for (auto& oi2 : w2.items) {
                    if (oi2.name == oi.name) oi2.equipped = true;
                }
                app().save().save(w2);
                std::fprintf(stdout,
                             "[equip] EQUIPPED %s (%s) -> %s slot (weapon=%s armor=%s helm=%s); move list rebuilt on next fight\n",
                             oi.name.c_str(), subtype.c_str(), type.c_str(), w2.weapon.c_str(),
                             w2.armor.c_str(), w2.helm.c_str());
                std::fflush(stdout);
            }
        }
        ++idx;
        ++card;
    }
}

void EquipmentScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        ren.draw_sprite(*dojo, ren.current_camera());
    }
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.35f);

    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return;
    }
    const float slot_x = kViewW * 0.2f, slot_y0 = 220.0f, slot_dy = 110.0f;
    const char* slot_names[3] = {"Weapon", "Armor", "Helm"};
    const std::string current[3] = {w.weapon, w.armor, w.helm};
    for (int s = 0; s < 3; ++s) {
        const float sy = slot_y0 + s * slot_dy;
        draw_flat_button(app, std::string(slot_names[s]) + ": " + current[s], slot_x, sy, 400.0f,
                         80.0f, 0.35f, 0.3f, 0.45f, hover_ == s);
    }
    const float grid_x = kViewW * 0.55f, grid_y0 = 220.0f, grid_dx = 240.0f, grid_dy = 110.0f;
    int idx = 0;
    int card = 0;
    for (const auto& oi : w.items) {
        std::string type;
        for (const CatalogItem& ci : catalog_) {
            if (ci.name == oi.name) {
                type = ci.type;
                break;
            }
        }
        if (type != "Weapon" && type != "Armor" && type != "Helm") {
            ++idx;
            continue;
        }
        const int col = card % 2;
        const int row = card / 2;
        const float cx = grid_x + col * grid_dx;
        const float cy = grid_y0 + row * grid_dy;
        const bool equipped = oi.equipped;
        draw_flat_button(app, oi.name + (equipped ? " [EQ]" : ""), cx, cy, 220.0f, 80.0f,
                         equipped ? 0.5f : 0.3f, equipped ? 0.6f : 0.3f,
                         equipped ? 0.3f : 0.35f, hover_ == idx);
        ++idx;
        ++card;
    }
    // The BACK button (top-left).
    draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<Screen> make_screen(ScreenManager& mgr, ScreenId id) {
    switch (id) {
        case kScreenGeneralMenu:
            return std::make_unique<MainMenuScreen>(mgr);
        case kScreenMap:
            return std::make_unique<MapScreen>(mgr);
        case kScreenFight: {
            // The Map node click carried the battle into pending_battle.
            const PendingBattle& pb = mgr.app().pending_battle();
            return std::make_unique<FightScreen>(mgr, pb.battle_name, pb.location,
                                                 pb.reward_money, pb.reward_exp, pb.owned);
        }
        case kScreenResults: {
            const PendingBattle& pb = mgr.app().pending_battle();
            return std::make_unique<ResultsScreen>(mgr, pb.player_won, pb.reward_money,
                                                   pb.reward_exp);
        }
        case kScreenShop:
            return std::make_unique<ShopScreen>(mgr);
        case kScreenProfile:
            return std::make_unique<EquipmentScreen>(mgr);
        default:
            return nullptr;
    }
}

} // namespace sf2::app

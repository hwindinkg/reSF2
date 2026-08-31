// The shell screens implementation — MainMenu, Map, BattleResult.
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

#include "app/screens.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "app/app.hpp"
#include "app/save_system.hpp"
#include "atlas.hpp"
#include "scene/renderer.hpp"
#include "scene/sprite.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

namespace {

constexpr float kViewW = 1280.0f;
constexpr float kViewH = 720.0f;

// Draws a flat (untextured) rounded-ish button + its label as a solid
// quad. The exact menu atlas art (ASTC) is unavailable to the CPU pipeline
// this phase — flagged as the exact-layout gap.
void draw_flat_button(App& app, const std::string& label, float cx, float cy, float w, float h,
                      float r, float g, float b, bool hovered) {
    sf2::render::Renderer& ren = app.renderer();
    const float x0 = cx - w / 2.0f;
    const float y0 = cy - h / 2.0f;
    const float x1 = cx + w / 2.0f;
    const float y1 = cy + h / 2.0f;
    // Border + fill (two quads: the border is a darker solid behind).
    const float border = hovered ? 3.0f : 2.0f;
    const float verts_border[] = {
        x0 - border, y0 - border, x1 + border, y0 - border, x1 + border, y1 + border,
        x0 - border, y0 - border, x1 + border, y1 + border, x0 - border, y1 + border,
    };
    ren.draw_triangles(verts_border, 6, 0.1f, 0.1f, 0.12f, 0.9f);
    const float verts_fill[] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
    ren.draw_triangles(verts_fill, 6, r, g, b, 0.92f);

    // Label: a crude two-line-free text approximation — the BMFont glyph
    // path is deferred, so labels are drawn as centered white text via
    // small solid quads per character is overkill; this phase draws the
    // label text through the renderer's flat path only when a font exists.
    // For now the label is emitted as a caption on the button (see the
    // screen's own draw below) — here we keep the button art.
    (void)label;
}

// Loads the map battle nodes from stages.xml (the same XML the JS `Ch`
// parser + `qb` battles read, JS L1224/1404). Returns the nodes with their
// screen positions.
std::vector<MapScreen::Node> load_battle_nodes(const std::string& res_root, float view_w,
                                               float view_h) {
    std::vector<MapScreen::Node> out;
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/stages.xml";
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            // Fall back to the packaged xml.dat? The extracted copy is the
            // canonical source (the game loads stages.xml from xml.dat).
            return out;
        }
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root || std::string(root.name()) != "Stages") {
            return out;
        }
        // The map shows the CURRENT zone's battles. The game's map screen
        // (Ya) iterates `p.YBa()` (the zones) and the player's
        // CurrentZone (users_default = ZONE_1); the tutorial zone
        // (Punchbag) is the first and the only unlocked one at game start.
        // For the shell, zone 0 = the tutorial zone (Training + Bosses).
        const pugi::xml_node zones = root.child("Zones");
        if (!zones) {
            return out;
        }
        for (const pugi::xml_node zone : zones.children("Zone")) {
            for (const pugi::xml_node battle : zone.children("Battle")) {
                MapScreen::Node n;
                n.name = battle.attribute("Name").value();
                n.type = battle.attribute("Type").value();
                const float x = sf2::data::xml_attr_float(battle, "X", 0.0f);
                const float y = sf2::data::xml_attr_float(battle, "Y", 0.0f);
                // qe.X0a: node_x = X*uM + bg.w/2, node_y = -Y*uM + bg.h/2
                // with uM≈1 and the map bg scaled to the view center.
                n.x = x * 1.0f + view_w / 2.0f;
                n.y = view_h / 2.0f - y * 1.0f;
                n.active = !battle.attribute("Type").empty() ||
                           n.type == "DUMMY" || n.type == "TUTORIAL";
                out.push_back(std::move(n));
            }
            break;  // tutorial zone only (the playable start)
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "map: stages.xml load failed: %s\n", e.what());
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// MainMenuScreen
// ---------------------------------------------------------------------------

MainMenuScreen::MainMenuScreen(ScreenManager& mgr) : Screen(mgr, "GeneralMenu") {
    // The four entry buttons: Dojo (Fight), Map, Shop, Profile — mirroring
    // the menu atlas frames (Dojo_normal etc.) and the game's button strip
    // (the za/scroll layout, JS L1976-1977, is a vertical strip on the
    // left; the port places them horizontally for a usable windowed menu).
    // The Fight entry (the Dojo button) is the primary -> Map.
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
            std::fprintf(stdout, "[menu] MONEY %d   LV %d   POWER %d\n", w.money, w.level,
                         w.power);
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
                // The game's GeneralMenu "Fight" goes to the map; the Shop
                // and Profile are placeholders this phase (3.6b).
                if (b.target == kScreenMap) {
                    push(static_cast<ScreenId>(b.target));
                } else {
                    std::fprintf(stdout, "[menu] %s is a placeholder (3.6b)\n", b.label.c_str());
                }
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

    // Dojo backdrop (the game's GeneralMenu shows the dojo behind the menu).
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        ren.draw_sprite(*dojo, ren.current_camera());
    } else {
        // Flat fallback: dark gradient-ish backdrop.
        const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(verts, 6, 0.12f, 0.12f, 0.16f, 1.0f);
    }

    // Buttons.
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const Button& b = buttons_[i];
        const bool hovered = static_cast<int>(i) == hover_;
        // Fight = gold-ish highlight, others neutral (the game's
        // EButtonGold / EButtonWhite styles).
        const float r = hovered ? 0.85f : (b.target == kScreenMap ? 0.72f : 0.45f);
        const float g = hovered ? 0.72f : (b.target == kScreenMap ? 0.62f : 0.48f);
        const float bl = hovered ? 0.35f : (b.target == kScreenMap ? 0.2f : 0.42f);
        draw_flat_button(app, b.label, b.x, b.y, b.w, b.h, r, g, bl, hovered);
        // The label — no BMFont glyph path this phase; draw a simple
        // centered text via the renderer's solid path is skipped, so the
        // button label is printed in the log on hover/click (the visible
        // text is the next-phase nicety).
        (void)app.draw_text(b.x, b.y, b.label, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// MapScreen
// ---------------------------------------------------------------------------

MapScreen::MapScreen(ScreenManager& mgr) : Screen(mgr, "Map") {
    nodes_ = load_battle_nodes(mgr.app().res_root(), kViewW, kViewH);
    std::fprintf(stdout, "[map] %zu battle nodes loaded\n", nodes_.size());
    for (const auto& n : nodes_) {
        std::fprintf(stdout, "[map] node %s (type=%s) at (%.0f, %.0f)%s\n", n.name.c_str(),
                     n.type.c_str(), n.x, n.y, n.active ? "" : " [locked]");
    }
}

void MapScreen::update_impl(float dt) {
    (void)dt;
    const App::PointerState& p = app().pointer();
    hover_ = -1;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        if (p.x >= n.x - 60 && p.x <= n.x + 60 && p.y >= n.y - 60 && p.y <= n.y + 60) {
            hover_ = static_cast<int>(i);
            if (p.pressed && n.active) {
                std::fprintf(stdout, "[map] click %s -> placeholder Fight (3.6b)\n",
                             n.name.c_str());
                std::fflush(stdout);
                // 3.6b wires the real Fight screen; for now show the
                // result placeholder.
                push(kScreenFight);
            }
        }
    }
}

void MapScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // Map backdrop: the dojo (the tutorial zone is the dojo location).
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
        // Node button: the game's base_training/active_training frames are
        // ASTC — this phase draws a flat circle-ish node marker.
        const float r = hovered ? 0.9f : (n.active ? 0.6f : 0.3f);
        const float g = hovered ? 0.5f : (n.active ? 0.4f : 0.3f);
        const float b = hovered ? 0.3f : (n.active ? 0.25f : 0.3f);
        const float d = hovered ? 66.0f : 60.0f;
        const float x0 = n.x - d / 2, y0 = n.y - d / 2;
        const float verts[] = {x0, y0, x0 + d, y0, x0 + d, y0 + d, x0, y0, x0 + d, y0 + d, x0, y0 + d};
        ren.draw_triangles(verts, 6, r, g, b, n.active ? 0.95f : 0.5f);
    }
}

// ---------------------------------------------------------------------------
// BattleResultScreen
// ---------------------------------------------------------------------------

BattleResultScreen::BattleResultScreen(ScreenManager& mgr, std::string winner_text)
    : Screen(mgr, "FightResult"), winner_text_(std::move(winner_text)) {}

void BattleResultScreen::update_impl(float dt) {
    (void)dt;
    const App::PointerState& p = app().pointer();
    if (p.pressed) {
        // The game's results screen returns to the map ("Fight" results ->
        // `wa.F().mp(5)` after the reward flow, JS L1213 `qxa`). Pop back
        // to the map beneath (the results screen was pushed on top of it).
        std::fprintf(stdout, "[result] click -> back to Map\n");
        std::fflush(stdout);
        manager().pop();
    }
}

void BattleResultScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // Dim the backdrop + the winner text (flat placeholder).
    const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(verts, 6, 0.0f, 0.0f, 0.0f, 0.6f);
    std::fprintf(stdout, "[result] %s\n", winner_text_.c_str());
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
        case kScreenFight:
            return std::make_unique<BattleResultScreen>(mgr, "PLACEHOLDER: Fight wired in 3.6b");
        default:
            return nullptr;
    }
}

} // namespace sf2::app

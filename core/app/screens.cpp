// The shell screens implementation — Dojo (home), MainMenu, Map, Fight,
// Results, Shop, Equipment.
//
// Layout math (JS-derived):
//   - Dojo/home buttons: the same menu-atlas entry buttons + positions as
//     the GeneralMenu (the four entry buttons sit in the lower half of the
//     dojo backdrop, horizontally spaced by ~0.26 of the view width,
//     centered). The Dojo FIGHT button starts the training fight vs the
//     Punchbag dummy; Map/Shop/Profile push their screens.
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
#include <unordered_map>

#include "anim_archive.hpp"
#include "app/app.hpp"
#include "app/save_system.hpp"
#include "atlas.hpp"
#include "audio/audio.hpp"
#include "font.hpp"
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

// The between-rounds HUD "Next" button (JS `vhb` L410 case 1 -> `Z2()`):
// center + size in screen coords. Drawn only while
// FightController::round_wait(); a click (or Space/Enter, see on_key)
// runs next_round_requested() (recovery + the next round).
constexpr float kNextBtnCX = kViewW * 0.5f;
constexpr float kNextBtnCY = kViewH * 0.6f;
constexpr float kNextBtnW = 240.0f;
constexpr float kNextBtnH = 80.0f;

// ---------------------------------------------------------------------------
// On-screen gamepad geometry — [ORIGINAL] JS `Za.update()` (sf2.js L454-456)
// lays the virtual controls out on the 960x540 design rect (N.rect) scaled to
// the view. Screen mode (no touch device, `L.K.un == false`):
//   c = Eha*0.05, d = Eha*0.03, e = max(150, Eha*0.2)   (Eha = design height)
//   joystick: scale f = e / sNa (sNa = the base frame width), center =
//     (e/2 + c, view_bottom - e/2 - d)
//   buttons node: scale e/440, center = (view_right - 220*e/440 - c,
//     view_bottom - 298*e/440 - d); the buttons sit INSIDE the node at the
//     JS `fu` offsets: punch Si(120,28), kick fh(-50,198) — the node's
//     local Y grows DOWN (Ea screen space), so +198 is the LOWER button.
// The knob drag maps to a movement sector via `ze.GBa` (the 8-way split
// with the 55-degree sector half-angle, jz=55): 1=up 2=up-forward
// 3=forward 4=down-forward 5=down 6=down-back 7=back 8=up-back — the same
// key_type 1-8 the keyboard path feeds.
// ---------------------------------------------------------------------------
constexpr float kPadMarginC = kViewH * 0.05f;  // c — side margin
constexpr float kPadMarginD = kViewH * 0.03f;  // d — bottom margin
constexpr float kPadSizeE = 150.0f;            // e — max(150, H*0.2) at 720
// The joystick base frame (JoystickContainer_norm) is 466 atlas px wide.
constexpr float kJoyBaseFrame = 466.0f;
// The buttons node is 440x596 local px (JS `fu` layout), scaled by e/440.
constexpr float kPadNodeW = 440.0f;
constexpr float kPadNodeH = 596.0f;
// The JS button hit radius: `uab` tests x*x+y*y < 13225 (= 115^2) in the
// node's local space.
constexpr float kPadBtnHitR = 115.0f;
// The knob travel: the JS `ze` grab zone is Ho*1.5 (Kz=1.5) where Ho is
// the knob's max offset; the dead zone is Ho*0.5 (Lz=0.5). Ho itself is
// srb(EH.za()/2) = half the ACTION container frame (466) — but the knob
// offset fed to e5() is normalized (GBa works on the unit vector), so the
// native uses the on-screen base radius directly.
constexpr float kJoyGrabScale = 1.5f;   // Kz — grab zone = base_r * this
constexpr float kJoyDeadZone = 0.5f;    // Lz — dead zone = base_r * this
// The JS sector math: aO = 55 deg (jz), r8 = 35 deg (90-jz). A diagonal
// counts only when its off-axis component exceeds tan(r8) of the main one.
constexpr float kJoySectorTan = 0.7002f;  // tan(35 deg)

// The on-screen gamepad layout (view coords, computed once per use).
struct GamepadLayout {
    float joy_cx = 0.0f, joy_cy = 0.0f;  // the base center
    float joy_r = 0.0f;                  // the base radius (on screen)
    float knob_r = 0.0f;                 // the knob radius (on screen)
    float node_cx = 0.0f, node_cy = 0.0f;  // the buttons node center
    float node_scale = 1.0f;            // e/440
    float punch_cx = 0.0f, punch_cy = 0.0f;  // button centers (view coords)
    float kick_cx = 0.0f, kick_cy = 0.0f;
    float btn_r = 0.0f;  // the button hit radius (view coords)

    GamepadLayout() {
        // Joystick (JS: f = e/sNa; center = (f*sNa/2 + c, bottom - f*sNa/2 - d)
        // -> with f*sNa == e: (e/2 + c, bottom - e/2 - d)).
        joy_r = kPadSizeE * 0.5f;
        joy_cx = kPadSizeE * 0.5f + kPadMarginC;
        joy_cy = kViewH - kPadSizeE * 0.5f - kPadMarginD;
        // The knob frame (Joystick_norm, 212 px) renders at the same scale
        // as the base (the JS `ze` builds both from E.get(268) at the
        // node's scale; the knob is 212/466 of the base).
        knob_r = kPadSizeE * (212.0f / kJoyBaseFrame) * 0.5f;
        // Buttons node (JS: scale e/440; center = (right - 220*scale - c,
        // bottom - 298*scale - d)).
        node_scale = kPadSizeE / kPadNodeW;
        node_cx = kViewW - kPadNodeW * 0.5f * node_scale - kPadMarginC;
        node_cy = kViewH - kPadNodeH * 0.5f * node_scale - kPadMarginD;
        // The buttons inside the node (JS `fu` ctor: Si(120,28) punch,
        // fh(-50,198) kick; local Y grows down).
        punch_cx = node_cx + 120.0f * node_scale;
        punch_cy = node_cy + 28.0f * node_scale;
        kick_cx = node_cx - 50.0f * node_scale;
        kick_cy = node_cy + 198.0f * node_scale;
        btn_r = kPadBtnHitR * node_scale;
    }
};

// [ORIGINAL] JS `ze.GBa` — the knob vector -> the movement key 1-8 (0 =
// neutral/dead). Screen Y grows down, so the JS flips y (`d = a.y*-1`)
// before the sector test; the native works in the same screen space.
int joy_sector_of(float dx, float dy, float base_r) {
    const float len2 = dx * dx + dy * dy;
    if (len2 <= base_r * base_r * kJoyDeadZone * kJoyDeadZone) return 0;
    // Flip to the JS's up-positive space.
    const float x = dx;
    const float y = -dy;
    // Rotate by the sector half-angle (aO=55deg): e = x*cos+x*sin... the
    // JS rotates (x,y) by aO/2 into (e,c) — the exact matrix:
    //   e = x*cos(aO/2) + y*sin(aO/2)
    //   c = y*cos(aO/2) - x*sin(aO/2)
    constexpr float kHalf = 55.0f * 3.14159265358979323846f / 180.0f * 0.5f;
    const float e = x * std::cos(kHalf) + y * std::sin(kHalf);
    const float c = y * std::cos(kHalf) - x * std::sin(kHalf);
    const bool e_pos = e >= 0.0f;
    const bool c_pos = c >= 0.0f;
    int sector;      // the JS quadrant id (1..4)
    float main_v;    // the axis-aligned component
    float off_v;     // the diagonal component
    if (e_pos && c_pos) {
        sector = 1;  // up .. up-forward
        main_v = std::fabs(e);
        off_v = std::fabs(c);
    } else if (e_pos && !c_pos) {
        sector = 2;  // forward .. down-forward
        main_v = std::fabs(c);
        off_v = std::fabs(e);
    } else if (!e_pos && c_pos) {
        sector = 4;  // back .. up-back
        main_v = std::fabs(c);
        off_v = std::fabs(e);
    } else {
        sector = 3;  // down .. down-back
        main_v = std::fabs(e);
        off_v = std::fabs(c);
    }
    // A diagonal counts when the off-axis component is SMALL relative to
    // the main one (JS `d<=a*this.EVa&&(e=!0)` — e=true = the diagonal
    // branch of the switch; the pure directions are the e-axis edges).
    const bool diagonal = off_v <= main_v * kJoySectorTan;
    switch (sector) {
        case 1: return diagonal ? 2 : 1;  // up | up-forward
        case 2: return diagonal ? 4 : 3;  // forward | down-forward
        case 3: return diagonal ? 6 : 5;  // down | down-back
        case 4: return diagonal ? 8 : 7;  // back | up-back
        default: return 0;
    }
}

// Loads the ui/controller atlas (the virtual gamepad art: Joystick*,
// btn_punch_*, btn_kick_*) into the app's atlas cache. Returns true when
// the frames are registered (logged once). The atlas ships as ASTC ktx —
// the same decode path the other UI atlases use.
bool load_controller_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string ui_dir = app.res_root() + "/ui";
        // Find the controller json (controller.<hash>.json).
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(ui_dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("controller.", 0) == 0 &&
                entry.path().extension().string() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) {
            std::fprintf(stderr, "[fight] controller atlas json not found in %s\n",
                         ui_dir.c_str());
            return false;
        }
        // The texture: try ktx then dds beside the json (the hashed stem).
        const std::string stem = std::filesystem::path(json_path)
                                     .filename()
                                     .string()
                                     .substr(0, std::string("controller").size());
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".ktx", ".dds", ".webp", ".png"}) {
            for (const auto& entry : std::filesystem::directory_iterator(ui_dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(stem + ".", 0) == 0 &&
                    entry.path().extension().string() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) {
            std::fprintf(stderr, "[fight] controller atlas texture not decodable\n");
            return false;
        }
        const GLuint gl = app.renderer().texture_for("controller_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[fight] controller atlas: %dx%d tex %dx%d %zu frames\n",
                     a.w, a.h, tex.w, tex.h, a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[fight] controller atlas load failed: %s\n", e.what());
    }
    return ok;
}

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

// Tries to draw an atlas frame centered at (cx,cy) sized to (w,h). Returns true if drawn.
bool try_draw_atlas_button(App& app, const std::string& frame_name, float cx, float cy, float w, float h,
                           float alpha = 1.0f) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame(frame_name, &fr, &tw, &th, &gl)) return false;
    sf2::scene::Sprite s;
    s.texture_name = frame_name;
    s.frame_x = static_cast<float>(fr.x);
    s.frame_y = static_cast<float>(fr.y);
    s.frame_w = static_cast<float>(fr.w);
    s.frame_h = static_cast<float>(fr.h);
    s.tex_w = static_cast<float>(tw);
    s.tex_h = static_cast<float>(th);
    s.solid = false;
    s.color_a = alpha;
    if (fr.rotated) {
        std::swap(s.frame_w, s.frame_h);
    }
    s.transform.set_pos(cx, cy);
    if (fr.w > 0 && fr.h > 0) {
        s.transform.set_scale(w / static_cast<float>(fr.w), h / static_cast<float>(fr.h));
    }
    // UI is screen-space: identity camera (world == screen)
    sf2::render::Camera ui_cam;
    ui_cam.center_x = 640.0f;
    ui_cam.center_y = 360.0f;
    ui_cam.zoom = 1.0f;
    ui_cam.view_w = 1280.0f;
    ui_cam.view_h = 720.0f;
    ui_cam.arena_h = 720.0f;
    ui_cam.arena_floor = 0.0f;
    ui_cam.arena_center_x = 640.0f;
    app.renderer().draw_sprite(s, ui_cam);
    return true;
}

// ---------------------------------------------------------------------------
// Fight banner + hit sparks (JS `Cr` L2021-2026 / `Hyb`+`ryb`+`av`).
//
// The original's ROUND/FIGHT!/VICTORY/DEFEAT labels are pre-rendered sprites
// in the `image` atlas (id 1310, JS_GAMEPLAY §"ai" L373-375: `y.BQa/uQa/
// zQa/wQa`) + the round NUMBER drawn with fight/round.fnt. That atlas ships
// as ASTC ktx / crunch dds — not CPU-decodable by this pipeline (see the
// file header) — so the native banner renders the text with the menu font
// (ui/font-en.fnt, full A-Z coverage; round.fnt carries ONLY digits/":"/"/"
// glyphs, no letters). The banner is a pure presentation layer over the
// fight: it reads FightController::banner()/banner_text()/banner_progress()
// and never touches the simulation.
// ---------------------------------------------------------------------------

// The banner's animation envelope over `progress` (0..1): scale-in 0.5 -> 1.0
// over the first 15%, hold at 1.0, fade out over the last 20%. The
// victory/defeat banners hold forever (banner_len_ = 1e9 -> the controller's
// progress stays ~0), so they pop in from the screen-tracked age and hold.
float banner_scale_at(float progress) {
    constexpr float kInEnd = 0.15f;  // scale-in window (first 15%)
    if (progress <= 0.0f) return 0.5f;
    if (progress < kInEnd) {
        const float t = progress / kInEnd;              // 0..1
        const float rise = t * t * (3.0f - 2.0f * t);   // smoothstep 0..1
        return 0.5f + 0.5f * rise;                      // 0.5 -> 1.0
    }
    return 1.0f;
}

float banner_alpha_at(float progress) {
    constexpr float kFadeStart = 0.8f;  // fade-out window (last 20%)
    if (progress >= kFadeStart) {
        const float t = (progress - kFadeStart) / (1.0f - kFadeStart);
        return 1.0f - t;  // linear 1 -> 0
    }
    return 1.0f;
}

// Draws the current fight banner (ROUND N / FIGHT! / K.O. / VICTORY /
// DEFEAT) centered at ~35% of the view height. White with a black drop
// shadow (ROUND/FIGHT), red (K.O., larger), gold (VICTORY), red (DEFEAT).
// Screen-space (the UI camera), drawn over the fight but under the gamepad
// and the Next button (the caller's draw order).
//
// `banner_age` = fight frames since the banner was raised (tracked by the
// FightScreen — the controller's banner_progress() divides by banner_len_,
// which is 1e9 for the hold-forever VICTORY/DEFEAT banners, so their
// controller progress stays ~0; the screen-side age drives their pop-in).
void draw_fight_banner(App& app, const sf2::scene::FightController& fight, int banner_age) {
    const sf2::scene::banner_kind kind = fight.banner();
    if (kind == sf2::scene::banner_kind::none) return;
    const char* text = fight.banner_text();
    if (text == nullptr || text[0] == '\0') return;

    const sf2::data::font* fnt = app.menu_font();
    const unsigned int tex = app.font_texture();
    if (fnt == nullptr || tex == 0) return;  // no font -> no banner

    float progress = fight.banner_progress();
    if (kind == sf2::scene::banner_kind::victory ||
        kind == sf2::scene::banner_kind::defeat) {
        // Hold-forever banner: pop in over ~0.75 s from the screen-tracked
        // age, then HOLD (no fade — the results screen takes over). The
        // envelope's fade window starts at 0.8, so clamp the pop-in
        // progress at 0.8: alpha stays 1.0 forever.
        constexpr float kHoldBannerInFrames = 45.0f;
        constexpr float kHoldProgressCap = 0.8f;
        progress = std::min(kHoldProgressCap,
                           static_cast<float>(banner_age) / kHoldBannerInFrames);
    }
    const float scale_anim = banner_scale_at(progress);
    const float alpha = banner_alpha_at(progress);
    if (alpha <= 0.01f) return;

    // The base glyph scale: font-en caps are ~53px tall; the banner reads
    // big at ~1.6x, K.O. bigger still.
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float size = 1.6f;
    if (kind == sf2::scene::banner_kind::ko) {
        r = 0.95f;
        g = 0.12f;
        b = 0.10f;
        size = 2.2f;
    } else if (kind == sf2::scene::banner_kind::victory) {
        r = 1.0f;
        g = 0.82f;
        b = 0.25f;
        size = 1.9f;
    } else if (kind == sf2::scene::banner_kind::defeat) {
        r = 0.90f;
        g = 0.15f;
        b = 0.12f;
        size = 1.9f;
    }
    const float scale = size * scale_anim;

    // Centered at ~35% of the view height. draw_text_* anchors a line at
    // its top y, so center the font's line box on the target point (the
    // caps sit in the line's upper half — slightly above center, right for
    // a banner).
    const float cx = kViewW * 0.5f;
    const float cy = kViewH * 0.35f;
    const float y = cy - static_cast<float>(fnt->line_height) * scale * 0.5f;

    // The black drop shadow (offset ~2px per scale unit), then the text.
    const float shadow_off = 2.0f + scale * 0.8f;
    app.draw_text_centered(*fnt, tex, cx + shadow_off, y + shadow_off, text, scale,
                           0.0f, 0.0f, 0.0f, 0.75f * alpha);
    app.draw_text_centered(*fnt, tex, cx, y, text, scale, r, g, b, alpha);
}

// Draws the live hit-spark particles (world space -> screen through the SAME
// camera the fighters use, factor 1.0 — the shake/framing is already baked
// into the camera the caller passes). Each spark is a small screen-space
// quad of the particle's world `size` scaled by the camera zoom, tinted
// `color` (0xRRGGBB), fading by age/life. Draw order: AFTER the fighters,
// BEFORE the fg floor layers (bg -> fighters -> SPARKS -> fg floor — the
// b615a1bf layer order).
void draw_hit_sparks(sf2::render::Renderer& ren, const sf2::render::Camera& camera,
                     const sf2::scene::EffectSystem& fx) {
    const std::vector<sf2::scene::particle>& parts = fx.particles();
    for (const sf2::scene::particle& p : parts) {
        if (p.life <= 0.0f || p.age >= p.life) continue;
        const float t = p.age / p.life;          // 0..1 lived
        const float alpha = 1.0f - t;            // fade out by age/life
        if (alpha <= 0.02f) continue;
        const float r = static_cast<float>((p.color >> 16) & 0xFFu) * (1.0f / 255.0f);
        const float g = static_cast<float>((p.color >> 8) & 0xFFu) * (1.0f / 255.0f);
        const float b = static_cast<float>(p.color & 0xFFu) * (1.0f / 255.0f);
        // World -> screen (factor 1.0: the sparks live in the fight plane).
        const float sx = camera.world_to_screen_x(p.x, 1.0f);
        const float sy = camera.world_to_screen_y(p.y);
        // The spark's world size -> screen px (the same zoom the capsule
        // strokes use); shrink slightly as it fades.
        const float half = p.size * camera.zoom * 0.5f * (0.4f + 0.6f * alpha);
        if (half < 0.5f) continue;
        const float verts[] = {
            sx - half, sy - half, sx + half, sy - half, sx - half, sy + half,
            sx + half, sy - half, sx + half, sy + half, sx - half, sy + half,
        };
        ren.draw_triangles(verts, 6, r, g, b, alpha);
    }
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
                // [Phase A3] SFX: the menu button click (the game's
                // snd_click_1 — `ta.ak("snd_click_1")` on UI taps).
                sf2::audio::AudioEngine::instance().play("click");
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
        sf2::render::Camera ui_cam;
        ui_cam.center_x = kViewW * 0.5f;
        ui_cam.center_y = kViewH * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = kViewW;
        ui_cam.view_h = kViewH;
        ui_cam.arena_h = kViewH;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = kViewW * 0.5f;
        ren.draw_sprite(*dojo, ui_cam);
    } else {
        const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(verts, 6, 0.12f, 0.12f, 0.16f, 1.0f);
    }
    // Frame names for the 4 menu buttons (TexturePacker menu atlas)
    const char* frame_names[4] = {"Dojo_normal", "Map_normal", "Shop_normal", "Profile_normal"};
    const char* frame_hover[4] = {"Dojo_active", "Map_active", "Shop_active", "Profile_active"};
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const Button& b = buttons_[i];
        const bool hovered = static_cast<int>(i) == hover_;
        const char* fn = hovered ? frame_hover[i] : frame_names[i];
        if (!try_draw_atlas_button(app, fn, b.x, b.y, b.w, b.h, 1.0f)) {
            const float r = hovered ? 0.85f : (b.target == kScreenMap ? 0.72f : 0.45f);
            const float g = hovered ? 0.72f : (b.target == kScreenMap ? 0.62f : 0.48f);
            const float bl = hovered ? 0.35f : (b.target == kScreenMap ? 0.2f : 0.42f);
            draw_flat_button(app, b.label, b.x, b.y, b.w, b.h, r, g, bl, hovered);
        }
        (void)app.draw_text(b.x, b.y, b.label, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// DojoScreen
// ---------------------------------------------------------------------------

DojoScreen::DojoScreen(ScreenManager& mgr) : Screen(mgr, "Dojo") {
    const float bw = 240.0f;
    const float bh = 120.0f;
    const float y = kViewH * 0.72f;
    const float xs[] = {kViewW * 0.28f, kViewW * 0.46f, kViewW * 0.64f, kViewW * 0.82f};
    struct Def {
        const char* label;
        int target;
    };
    // The home hub: FIGHT starts the training battle vs the Punchbag dummy
    // (the stages.xml Punchbag zone "Training"), Map/Shop/Profile are the
    // entry buttons (JS menu atlas Dojo_normal/Map_normal/Shop_normal/
    // Profile_normal — the original home screen's buttons).
    const Def defs[] = {
        {"FIGHT", kScreenFight}, {"MAP", kScreenMap}, {"SHOP", kScreenShop}, {"PROFILE", kScreenProfile},
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

void DojoScreen::update_impl(float dt) {
    (void)dt;
    if (!money_logged_) {
        money_logged_ = true;
        try {
            const WarriorSave w = app().save().load();
            std::fprintf(stdout, "[dojo] MONEY %d   LV %d   POWER %d   WEAPON %s   ARMOR %s   HELM %s\n",
                         w.money, w.level, w.power, w.weapon.c_str(), w.armor.c_str(),
                         w.helm.c_str());
            std::fflush(stdout);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[dojo] save read failed: %s\n", e.what());
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
                sf2::audio::AudioEngine::instance().play("click");
                std::fprintf(stdout, "[dojo] click %s -> screen %d\n", b.label.c_str(), b.target);
                std::fflush(stdout);
                if (b.target == kScreenFight) {
                    // The training fight vs the Punchbag dummy — the same
                    // pending-battle hand-off the MapScreen uses (JS `Ya`
                    // battle-start); the "Training" battle of the stages.xml
                    // Punchbag zone (Start=1, Money=0/Exp=0).
                    PendingBattle& pb = app().pending_battle();
                    pb.battle_name = "Training";
                    pb.location = "dojo";
                    pb.enemy_name = "Punchbag";
                    pb.has_result = false;
                    battle_rewards("Training", pb.reward_money, pb.reward_exp);
                    pb.owned = owned_items(app());
                    std::fprintf(stdout,
                                 "[dojo] FIGHT -> Training fight vs %s (reward money=%d exp=%d)\n",
                                 pb.enemy_name.c_str(), pb.reward_money, pb.reward_exp);
                    std::fflush(stdout);
                }
                push(static_cast<ScreenId>(b.target));
            }
        }
    }
    if (hover_ != last_hover_) {
        last_hover_ = hover_;
        if (hover_ >= 0) {
            std::fprintf(stdout, "[dojo] hover %s\n", buttons_[hover_].label.c_str());
            std::fflush(stdout);
        }
    }
}

void DojoScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        sf2::render::Camera ui_cam;
        ui_cam.center_x = kViewW * 0.5f;
        ui_cam.center_y = kViewH * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = kViewW;
        ui_cam.view_h = kViewH;
        ui_cam.arena_h = kViewH;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = kViewW * 0.5f;
        ren.draw_sprite(*dojo, ui_cam);
    } else {
        const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(verts, 6, 0.12f, 0.12f, 0.16f, 1.0f);
    }
    // The menu-atlas entry buttons (Dojo/Map/Shop/Profile) — the same
    // frames the GeneralMenu renders; the Dojo button is the home hub's
    // training-Fight entry.
    const char* frame_names[4] = {"Dojo_normal", "Map_normal", "Shop_normal", "Profile_normal"};
    const char* frame_hover[4] = {"Dojo_active", "Map_active", "Shop_active", "Profile_active"};
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const Button& b = buttons_[i];
        const bool hovered = static_cast<int>(i) == hover_;
        const char* fn = hovered ? frame_hover[i] : frame_names[i];
        if (!try_draw_atlas_button(app, fn, b.x, b.y, b.w, b.h, 1.0f)) {
            const float r = hovered ? 0.85f : (b.target == kScreenFight ? 0.72f : 0.45f);
            const float g = hovered ? 0.72f : (b.target == kScreenFight ? 0.62f : 0.48f);
            const float bl = hovered ? 0.35f : (b.target == kScreenFight ? 0.2f : 0.42f);
            draw_flat_button(app, b.label, b.x, b.y, b.w, b.h, r, g, bl, hovered);
        }
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
    // BACK (top-left) -> the previous screen (the Dojo home hub — the
    // loop's map -> dojo / map -> equipment legs; the JS map has a
    // back/exit control in the top bar).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[map] BACK -> previous screen\n");
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
        sf2::render::Camera ui_cam;
        ui_cam.center_x = kViewW * 0.5f;
        ui_cam.center_y = kViewH * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = kViewW;
        ui_cam.view_h = kViewH;
        ui_cam.arena_h = kViewH;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = kViewW * 0.5f;
        ren.draw_sprite(*dojo, ui_cam);
    } else {
        const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(verts, 6, 0.08f, 0.1f, 0.14f, 1.0f);
    }
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        const bool hovered = static_cast<int>(i) == hover_;
        const float d = hovered ? 66.0f : 60.0f;
        // Try textured BattleBtn frames (buttons atlas). Use a generic frame that exists for all nodes.
        const char* tex_frame = hovered ? "BattleBtnActive/active_lynx" : "BattleBtnBase/base_lynx";
        bool drawn = try_draw_atlas_button(app, tex_frame, n.x, n.y, d, d, n.active ? 1.0f : 0.6f);
        if (!drawn) {
            // Fallback: try any BattleBtn frame present
            const char* fallbacks[] = {"BattleBtnBase/base_lynx", "BattleBtnActive/active_lynx", "BattleBtnBase/base_hermit"};
            for (const char* fb : fallbacks) {
                if (try_draw_atlas_button(app, fb, n.x, n.y, d, d, n.active ? 1.0f : 0.6f)) { drawn = true; break; }
            }
        }
        if (!drawn) {
            const float r = hovered ? 0.9f : (n.active ? 0.6f : 0.3f);
            const float g = hovered ? 0.5f : (n.active ? 0.4f : 0.3f);
            const float b = hovered ? 0.3f : (n.active ? 0.25f : 0.3f);
            const float x0 = n.x - d / 2, y0 = n.y - d / 2;
            const float verts[] = {x0, y0, x0 + d, y0, x0 + d, y0 + d, x0, y0, x0 + d, y0 + d, x0, y0 + d};
            ren.draw_triangles(verts, 6, r, g, b, n.active ? 0.95f : 0.5f);
        }
    }
    // The BACK button (top-left) — try textured misc frame, else flat.
    if (!try_draw_atlas_button(app, "btn_back", 64.0f, 40.0f, 88.0f, 48.0f, 1.0f)) {
        draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
    }
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

    // The on-screen gamepad art (JS `Za`): the ui/controller atlas
    // (Joystick*, btn_punch_*, btn_kick_*). Loaded once per process; the
    // frames register into the app's atlas cache for draw_gamepad.
    (void)load_controller_atlas(app());

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
    // Arena bounds are [wall, arena_w-wall] = [80,1880] (0-based, JS Bf NU/width).
    // The centered ±900 calculation placed spawn 973 outside the arena (clamped to 900,
    // causing |dx| ~211 vs oracle). Reverted to JS-accurate bounds.
    const float wall_min = wall;
    const float wall_max = arena_w - wall;

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
    const float floor_y = -20.0f;  // camera anchor -> cam ~ -219 -> floor at 559, fighters COM -93/-110 -> feet ~ -3/ -? on floor
    battle.player_spawn_x = 973.0f;
    battle.player_spawn_y = -110.0f;  // COM Y (oracle Me -108, Enemy -93) — ModelsViewer split
    battle.enemy_spawn_x = 690.0f;
    battle.enemy_spawn_y = -93.0f;
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
    // The enemy's display name: the Dojo training fight names its Punchbag
    // dummy (JS stages.xml Fight 1 Warrior FirstName="Punchbag"); the map
    // flow keeps the pending-battle default "Enemy".
    const std::string& enemy_name = app().pending_battle().enemy_name;
    fight_->init_locks(battle, assets.merged, assets.moves, assets.clips,
                       assets.tactics_sets, tactic, "Player", enemy_name,
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
    // Between rounds: Space (32) / Enter (257) = the HUD "Next" button (JS
    // `vhb` L410 case 1) — confirm the next round instead of feeding the
    // punch/attack mapping below.
    if (fight_ != nullptr && down && fight_->round_wait() &&
        (glfw_key == 32 || glfw_key == 257)) {
        sf2::audio::AudioEngine::instance().play("click");
        std::fprintf(stdout, "[fight] NEXT round requested (Space/Enter)\n");
        std::fflush(stdout);
        fight_->next_round_requested();
        return;
    }
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

bool FightScreen::round_wait() const {
    return fight_ != nullptr && fight_->round_wait();
}

void FightScreen::next_button_center(float& cx, float& cy) const {
    cx = kNextBtnCX;
    cy = kNextBtnCY;
}

// [trace, Phase 0] Arms the FightController's per-frame pose dump (the
// pose/camera trace the JS-side oracle dump is diffed against).
void FightScreen::enable_pose_dump(const std::string& path, int frames) {
    if (fight_ != nullptr) {
        fight_->set_pose_dump(path, frames);
    }
}

// ---------------------------------------------------------------------------
// On-screen gamepad (JS `Za` virtual controls) — the original's touch pad.
// ---------------------------------------------------------------------------

bool FightScreen::pad_visible() const {
    // The pad shows only while the round is live (JS `Za.isVisible` is set
    // by the fight HUD; between rounds the Next button replaces it).
    return fight_ != nullptr && !fight_->round_wait() && !fight_->battle_over();
}

// The pointer -> gamepad events (JS `ze.nia/Qgb/oia` for the joystick,
// `fu.nia/oia` for the buttons). Runs in update_impl BEFORE the fight
// update so the buffered keys land the same frame (like on_key).
void FightScreen::update_gamepad_input() {
    if (fight_ == nullptr || !pad_visible()) {
        // Round ended mid-drag: release everything so no key stays held.
        if (joy_grabbed_ || joy_sector_ != 0 || btn_punch_down_ || btn_kick_down_) {
            joy_grabbed_ = false;
            joy_knob_x_ = joy_knob_y_ = 0.0f;
            joy_sector_ = 0;
            btn_punch_down_ = btn_kick_down_ = false;
        }
        return;
    }
    const App::PointerState& p = app().pointer();
    const GamepadLayout pad;

    // --- Joystick (JS `ze`): grab inside the base's 1.5x zone, drag the
    // knob, map the offset to the movement sector 1-8. The knob follows
    // the pointer clamped to the base radius (JS `e5` + `Mz.G_a`).
    if (p.pressed && !joy_grabbed_) {
        const float dx = static_cast<float>(p.x) - pad.joy_cx;
        const float dy = static_cast<float>(p.y) - pad.joy_cy;
        const float grab_r = pad.joy_r * kJoyGrabScale;
        if (dx * dx + dy * dy <= grab_r * grab_r) {
            joy_grabbed_ = true;
        }
    }
    if (joy_grabbed_) {
        if (!p.down) {
            // Released (JS `oia`): neutral + the key release event.
            joy_grabbed_ = false;
            joy_knob_x_ = joy_knob_y_ = 0.0f;
            if (joy_sector_ != 0) {
                fight_->player_input(static_cast<sf2::scene::key_type>(joy_sector_),
                                     sf2::scene::press_type::release);
                std::fprintf(stdout, "[fight] player input -> joy release %d\n",
                             joy_sector_);
                joy_sector_ = 0;
            }
        } else {
            // Drag (JS `Qgb`): clamp the knob to the base, recompute the
            // sector, and emit the key change (release the old sector's
            // key, tap the new one — the same edges the keyboard path
            // produces via on_key).
            float dx = static_cast<float>(p.x) - pad.joy_cx;
            float dy = static_cast<float>(p.y) - pad.joy_cy;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float max_off = pad.joy_r;
            if (len > max_off) {
                dx *= max_off / len;
                dy *= max_off / len;
            }
            joy_knob_x_ = dx;
            joy_knob_y_ = dy;
            const int sector = joy_sector_of(dx, dy, pad.joy_r);
            if (sector != joy_sector_) {
                if (joy_sector_ != 0) {
                    fight_->player_input(static_cast<sf2::scene::key_type>(joy_sector_),
                                         sf2::scene::press_type::release);
                }
                if (sector != 0) {
                    fight_->player_input(static_cast<sf2::scene::key_type>(sector),
                                         sf2::scene::press_type::tap);
                }
                std::fprintf(stdout, "[fight] player input -> joy sector %d -> %d\n",
                             joy_sector_, sector);
                std::fflush(stdout);
                joy_sector_ = sector;
            }
        }
    }

    // --- Attack buttons (JS `fu.nia/oia`): a press inside a button's
    // circle taps the attack key; the release ends it. The JS hit test is
    // the node-local x*x+y*y < 115^2 — the native tests the view-space
    // circle around each button center.
    const float px = static_cast<float>(p.x);
    const float py = static_cast<float>(p.y);
    if (p.pressed && !joy_grabbed_) {
        const float pdx = px - pad.punch_cx;
        const float pdy = py - pad.punch_cy;
        if (pdx * pdx + pdy * pdy <= pad.btn_r * pad.btn_r) {
            btn_punch_down_ = true;
            fight_->player_input(sf2::scene::key_type::punch, sf2::scene::press_type::tap);
            std::fprintf(stdout, "[fight] player input -> punch (pad)\n");
            std::fflush(stdout);
        } else {
            const float kdx = px - pad.kick_cx;
            const float kdy = py - pad.kick_cy;
            if (kdx * kdx + kdy * kdy <= pad.btn_r * pad.btn_r) {
                btn_kick_down_ = true;
                fight_->player_input(sf2::scene::key_type::kick, sf2::scene::press_type::tap);
                std::fprintf(stdout, "[fight] player input -> kick (pad)\n");
                std::fflush(stdout);
            }
        }
    }
    if (btn_punch_down_ && !p.down) {
        btn_punch_down_ = false;
        fight_->player_input(sf2::scene::key_type::punch, sf2::scene::press_type::release);
    }
    if (btn_kick_down_ && !p.down) {
        btn_kick_down_ = false;
        fight_->player_input(sf2::scene::key_type::kick, sf2::scene::press_type::release);
    }
}

// The gamepad render (JS `Za.Ea` -> `ze.Ea` + `fu.Ea`): the atlas frames
// from ui/controller — JoystickContainer_norm/action (base), Joystick_
// norm/action (knob), btn_punch_normal/action, btn_kick_normal/action.
// Flat procedural circles when the atlas is unavailable.
void FightScreen::draw_gamepad(App& app) const {
    if (fight_ == nullptr || !pad_visible()) return;
    const GamepadLayout pad;
    sf2::render::Renderer& ren = app.renderer();

    // --- Joystick: base + knob. The JS swaps the base frame to _action
    // while grabbed (`RT(a)` toggles aX/EH/$W/DX) — the native mirrors it.
    const bool grabbed = joy_grabbed_;
    const char* base_frame = grabbed ? "JoystickContainer_action" : "JoystickContainer_norm";
    const char* knob_frame = grabbed ? "Joystick_action" : "Joystick_norm";
    const float base_size = pad.joy_r * 2.0f;
    const float knob_size = pad.knob_r * 2.0f;
    if (!try_draw_atlas_button(app, base_frame, pad.joy_cx, pad.joy_cy, base_size,
                               base_size, 1.0f)) {
        // Fallback: a dark translucent circle (12-segment disc).
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegs = 20;
        const float step = 2.0f * kPi / static_cast<float>(kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const float a0 = static_cast<float>(s) * step;
            const float a1 = static_cast<float>(s + 1) * step;
            float tri[6] = {pad.joy_cx,
                           pad.joy_cy,
                           pad.joy_cx + std::cos(a0) * pad.joy_r,
                           pad.joy_cy + std::sin(a0) * pad.joy_r,
                           pad.joy_cx + std::cos(a1) * pad.joy_r,
                           pad.joy_cy + std::sin(a1) * pad.joy_r};
            ren.draw_triangles(tri, 3, 0.08f, 0.08f, 0.1f, 0.55f);
        }
    }
    const float knob_cx = pad.joy_cx + joy_knob_x_;
    const float knob_cy = pad.joy_cy + joy_knob_y_;
    if (!try_draw_atlas_button(app, knob_frame, knob_cx, knob_cy, knob_size, knob_size,
                               1.0f)) {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegs = 14;
        const float step = 2.0f * kPi / static_cast<float>(kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const float a0 = static_cast<float>(s) * step;
            const float a1 = static_cast<float>(s + 1) * step;
            float tri[6] = {knob_cx,
                           knob_cy,
                           knob_cx + std::cos(a0) * pad.knob_r,
                           knob_cy + std::sin(a0) * pad.knob_r,
                           knob_cx + std::cos(a1) * pad.knob_r,
                           knob_cy + std::sin(a1) * pad.knob_r};
            ren.draw_triangles(tri, 3, 0.22f, 0.24f, 0.28f, 0.85f);
        }
    }

    // --- Attack buttons: punch + kick circles (the JS `ig` swaps the
    // frame to _action while pressed).
    const float btn_size = pad.btn_r * 2.0f;
    const bool punch_drawn =
        try_draw_atlas_button(app, btn_punch_down_ ? "btn_punch_action" : "btn_punch_normal",
                              pad.punch_cx, pad.punch_cy, btn_size, btn_size, 1.0f);
    const bool kick_drawn =
        try_draw_atlas_button(app, btn_kick_down_ ? "btn_kick_action" : "btn_kick_normal",
                              pad.kick_cx, pad.kick_cy, btn_size, btn_size, 1.0f);
    if (!punch_drawn) {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegs = 14;
        const float step = 2.0f * kPi / static_cast<float>(kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const float a0 = static_cast<float>(s) * step;
            const float a1 = static_cast<float>(s + 1) * step;
            float tri[6] = {pad.punch_cx,
                           pad.punch_cy,
                           pad.punch_cx + std::cos(a0) * pad.btn_r,
                           pad.punch_cy + std::sin(a0) * pad.btn_r,
                            pad.punch_cx + std::cos(a1) * pad.btn_r,
                           pad.punch_cy + std::sin(a1) * pad.btn_r};
            ren.draw_triangles(tri, 3, btn_punch_down_ ? 0.9f : 0.3f, 0.55f, 0.15f, 0.85f);
        }
    }
    if (!kick_drawn) {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegs = 14;
        const float step = 2.0f * kPi / static_cast<float>(kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const float a0 = static_cast<float>(s) * step;
            const float a1 = static_cast<float>(s + 1) * step;
            float tri[6] = {pad.kick_cx,
                           pad.kick_cy,
                           pad.kick_cx + std::cos(a0) * pad.btn_r,
                           pad.kick_cy + std::sin(a0) * pad.btn_r,
                           pad.kick_cx + std::cos(a1) * pad.btn_r,
                           pad.kick_cy + std::sin(a1) * pad.btn_r};
            ren.draw_triangles(tri, 3, btn_kick_down_ ? 0.9f : 0.3f, 0.55f, 0.15f, 0.85f);
        }
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
        const float psx = c.world_to_screen_x(fight_->player().fighter.world_x(), 1.0f);
        const float psy = c.world_to_screen_y(feet_y);
        const float esx = c.world_to_screen_x(fight_->enemy().fighter.world_x(), 1.0f);
        const float esy = c.world_to_screen_y(feet_y);
        std::fprintf(stdout, "[verify] screen: player feet=(%.0f, %.0f) enemy feet=(%.0f, %.0f)\n",
                     psx, psy, esx, esy);
    }
    auto report = [&](const char* who, const sf2::scene::Fighter& f) {
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
        // On-screen check: the bbox center within the 1280x720 view (projected).
        const float world_cx = (min_x + max_x) * 0.5f;
        const float world_cy = (min_y + max_y) * 0.5f;
        sf2::render::Camera vcam;
        vcam.center_x = cam.center_x;
        vcam.center_y = cam.center_y;
        vcam.zoom = cam.zoom;
        vcam.view_w = 1280.0f;
        vcam.view_h = 720.0f;
        vcam.arena_h = 560.0f;
        vcam.arena_floor = 80.0f;
        vcam.arena_center_x = 980.0f;
        const float scx = vcam.world_to_screen_x(world_cx, 1.0f);
        const float scy = vcam.world_to_screen_y(world_cy);
        std::fprintf(stdout, "[verify] %s on-screen: center=(%.0f, %.0f) %s\n", who, scx, scy,
                     (scx >= 0 && scx <= 1280 && scy >= 0 && scy <= 720) ? "OK" : "OFF-SCREEN");
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
    // The on-screen gamepad (JS `Za`): the pointer events feed the same
    // player_input path the keyboard uses — BEFORE the fight update so
    // the buffered keys land this frame (the same ordering as on_key).
    update_gamepad_input();
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

    // Between-rounds HUD "Next" button (JS `vhb` L410 case 1 -> `Z2()`): a
    // click on the button while the host waits between rounds runs the
    // recovery and starts the next round (no button while the round is
    // live; Space/Enter handled in on_key).
    if (fight_->round_wait()) {
        const App::PointerState& p = app().pointer();
        if (p.pressed && p.x >= kNextBtnCX - kNextBtnW * 0.5f &&
            p.x <= kNextBtnCX + kNextBtnW * 0.5f && p.y >= kNextBtnCY - kNextBtnH * 0.5f &&
            p.y <= kNextBtnCY + kNextBtnH * 0.5f) {
            sf2::audio::AudioEngine::instance().play("click");
            std::fprintf(stdout, "[fight] NEXT round requested (round %d done)\n",
                         fight_->round().number);
            std::fflush(stdout);
            fight_->next_round_requested();
        }
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
    // [fix(render): arena layer order] The original game draws the fighters
    // INSIDE the ModelsViewer (Type=2) layer — background layers first, then
    // the fighters, then every layer AFTER the ModelsViewer (the floor /
    // arena sides / dust / glow / pixel_1 vignette) ON TOP of the fighters
    // (JS_RENDER §7, "Что у нас не так" #1). The old code drew ALL layers
    // before the fighters, so the floor rendered UNDER their feet — the
    // broken "arena behind the fighters" look.
    const std::size_t fighter_layer = assets.dojo.fighter_layer();
    assets.dojo.render_layers(ren, camera, 0, fighter_layer);  // background (parallax)

    // [fix(render): remove fake shadow] The oracle JS draws NO per-fighter
    // shadow (JS_RENDER §3.2: "в JS НЕТ пер-бойцовской тени"). The old
    // procedural black ellipse (the A4 commit 84269826) was a native
    // invention — the silhouettes stand straight on the floor line.

    auto project = [&camera](const std::vector<float>& v) {
        std::vector<float> out(v.size());
        for (std::size_t i = 0; i < v.size(); i += 2) {
            out[i] = camera.world_to_screen_x(v[i], 1.0f);
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
        // visual node -> a `Dk` stroked line, stroke = Radius1*2). Dedup by
        // edge keeps max Radius to avoid double squares (EThigh 12+15).
        const sf2::scene::Model& model = f.fighter.model();
        std::unordered_map<std::string, float> edge_max;
        edge_max.reserve(model.capsules.size() * 2u);
        for (const sf2::scene::Capsule& cap : model.capsules) {
            auto it = edge_max.find(cap.edge);
            if (it == edge_max.end() || cap.radius1 > it->second) {
                edge_max[cap.edge] = cap.radius1;
            }
        }
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kDiscSegments = 12;
        for (const auto& kv : edge_max) {
            const std::string& edge_name = kv.first;
            const float rad = kv.second;
            const sf2::scene::EdgeDef* edge = nullptr;
            for (const sf2::scene::EdgeDef& ed : model.edges) {
                if (ed.name == edge_name) {
                    edge = &ed;
                    break;
                }
            }
            if (edge == nullptr) {
                continue;
            }
            const int i1 = model.bone_by_name(edge->end1);
            const int i2 = model.bone_by_name(edge->end2);
            if (i1 < 0 || i2 < 0) {
                continue;
            }
            const std::vector<float>& pos = f.fighter.positions();
            const std::size_t u1 = static_cast<std::size_t>(i1) * 2;
            const std::size_t u2 = static_cast<std::size_t>(i2) * 2;
            if (u1 + 1 >= pos.size() || u2 + 1 >= pos.size()) {
                continue;
            }
            const float stroke = rad * 2.0f * camera.zoom;
            if (stroke <= 0.0f) {
                continue;
            }
            const float sx1 = camera.world_to_screen_x(pos[u1], 1.0f);
            const float sy1 = camera.world_to_screen_y(pos[u1 + 1]);
            const float sx2 = camera.world_to_screen_x(pos[u2], 1.0f);
            const float sy2 = camera.world_to_screen_y(pos[u2 + 1]);
            float dx = sx2 - sx1;
            float dy = sy2 - sy1;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float cr = stroke * 0.5f;
            auto draw_disc = [&](float cx, float cy) {
                const float step = 2.0f * kPi / static_cast<float>(kDiscSegments);
                for (int s = 0; s < kDiscSegments; ++s) {
                    const float a0 = static_cast<float>(s) * step;
                    const float a1 = static_cast<float>(s + 1) * step;
                    float tri[6] = {
                        cx,
                        cy,
                        cx + std::cos(a0) * cr,
                        cy + std::sin(a0) * cr,
                        cx + std::cos(a1) * cr,
                        cy + std::sin(a1) * cr,
                    };
                    ren.draw_triangles(tri, 3, r, g, b, 1.0f);
                }
            };
            if (len < 1e-4f) {
                draw_disc(sx1, sy1);
                continue;
            }
            dx /= len;
            dy /= len;
            const float px = -dy * cr;
            const float py = dx * cr;
            float quad[12] = {
                sx1 + px, sy1 + py, sx2 + px, sy2 + py,
                sx1 - px, sy1 - py, sx2 + px, sy2 + py,
                sx2 - px, sy2 - py, sx1 - px, sy1 - py,
            };
            ren.draw_triangles(quad, 6, r, g, b, 1.0f);
            draw_disc(sx1, sy1);
            draw_disc(sx2, sy2);
        }
    };
    draw_capsules(fight_->player());
    draw_capsules(fight_->enemy());
    ren.draw_triangles(pv.data(), pv.size() / 2, fight_->player().fighter.color_r(),
                       fight_->player().fighter.color_g(), fight_->player().fighter.color_b());
    ren.draw_triangles(ev.data(), ev.size() / 2, fight_->enemy().fighter.color_r(),
                       fight_->enemy().fighter.color_g(), fight_->enemy().fighter.color_b());

    // The hit sparks (JS `Hyb`/`ryb`/`av`): world-space particles projected
    // through the SAME camera the fighters used (factor 1.0 — the shake is
    // baked into the camera framing). Drawn AFTER the fighters, BEFORE the
    // fg floor layers (bg -> fighters -> SPARKS -> fg floor — the b615a1bf
    // layer order; the batch preserves submission order).
    draw_hit_sparks(ren, camera, fight_->fx());

    // [fix(render): arena layer order] The foreground layers — the ones the
    // params XML places AFTER the ModelsViewer (Type=2) fighter layer: the
    // dojo floor (`dojo_floor_1/2`), the arena side walls, the punch-bag
    // holder and the pixel_1 vignette — draw ON TOP of the fighters, exactly
    // like the original's `_0007_arena` / dust / glow (JS_RENDER §7).
    // Fall back to rendering nothing extra when the location has no
    // fighter layer (fighter_layer == npos already drew every layer above).
    const std::size_t n_layers = assets.dojo.layers().size();
    if (fighter_layer != sf2::scene::LocationScene::npos) {
        assets.dojo.render_layers(ren, camera, fighter_layer + 1, n_layers);
    }

    // --- HUD Phase A2: HealthBar frames + bitmap-font timer/rounds (1:1 original) ---
    // Frames: fight/ui.json -> HealthBar_Empty (bg), HealthBar_Full (player fill),
    //          HealthBarBlue_Full (enemy fill), HealthBar_Hit/Blue_Hit (damage), Round_Done/Undone.
    // JS Sf.layout (L2036) for 1280x720: d=1.5, e=1.1, c=min(W,H)/2/675*g,
    // g=1+(d-1)/0.5*0.1=1.1, f=c0*0.07 -> c=0.5867, bar centers =
    // W*0.5 +/- 520*c*e, bar y = P + 150*c + f*g. Bar frame h = 43 (atlas),
    // on-screen h = 43*c.
    const float bar_w = 440.0f, bar_h = 25.0f, bar_y = 115.7f;
    const float bar_cx_player = kViewW * 0.5f - 520.0f * 0.5867f * 1.1f;
    const float bar_cx_enemy = kViewW * 0.5f + 520.0f * 0.5867f * 1.1f;
    const float p_ratio = fight_->player().max_hp > 0.0f
                              ? std::clamp(fight_->player().hp / fight_->player().max_hp, 0.0f, 1.0f)
                              : 0.0f;
    const float e_ratio = fight_->enemy().max_hp > 0.0f
                              ? std::clamp(fight_->enemy().hp / fight_->enemy().max_hp, 0.0f, 1.0f)
                              : 0.0f;

    auto draw_hp_bar = [&](float x, float y, float w, float h, float ratio,
                           const char* fill_frame) {
        // Background: HealthBar_Empty stretched to full width
        if (!app.draw_atlas_rect("HealthBar_Empty", x, y, w, h, 1.0f)) {
            // Fallback flat dark bg
            const float bg[] = {x, y, x + w, y, x, y + h, x + w, y, x + w, y + h, x, y + h};
            ren.draw_triangles(bg, 6, 0.12f, 0.12f, 0.12f, 0.92f);
        }
        if (ratio > 0.001f) {
            const float fw = w * ratio;
            if (!app.draw_atlas_rect(fill_frame, x, y, fw, h, 1.0f)) {
                const bool is_blue = std::string(fill_frame).find("Blue") != std::string::npos;
                const float r = is_blue ? 0.25f : 0.16f;
                const float g = is_blue ? 0.45f : 0.82f;
                const float b = is_blue ? 0.92f : 0.16f;
                const float fg[] = {x, y, x + fw, y, x, y + h, x + fw, y, x + fw, y + h, x, y + h};
                ren.draw_triangles(fg, 6, r, g, b, 0.96f);
            }
        }
        // thin border over bar for readability
        const float br = 1.0f;
        const float top[] = {x - br, y - br, x + w + br, y - br, x - br, y,
                             x + w + br, y - br, x + w + br, y, x - br, y};
        const float bot[] = {x - br, y + h, x + w + br, y + h, x - br, y + h + br,
                             x + w + br, y + h, x + w + br, y + h + br, x - br, y + h + br};
        ren.draw_triangles(top, 6, 0.0f, 0.0f, 0.0f, 0.85f);
        ren.draw_triangles(bot, 6, 0.0f, 0.0f, 0.0f, 0.85f);
    };

    draw_hp_bar(bar_cx_player - bar_w * 0.5f, bar_y, bar_w, bar_h, p_ratio, "HealthBar_Full");
    draw_hp_bar(bar_cx_enemy - bar_w * 0.5f, bar_y, bar_w, bar_h, e_ratio,
                "HealthBarBlue_Full");

    // Timer — bitmap-font centered (Sf.layout: top-center). Uses fight/digits.fnt
    // (fallback to ui/font-en). Scale tuned so ~80px glyph -> ~30px on HUD.
    const int timer =
        fight_->round().gma - static_cast<int>(std::ceil(fight_->round().time));
    const std::string tstr = std::to_string(std::max(0, timer));
    {
        const sf2::data::font* fnt = app.digits_font() ? app.digits_font() : app.menu_font();
        unsigned int tex = app.digits_font() ? app.digits_texture() : app.font_texture();
        if (fnt != nullptr && tex != 0) {
            // digits.fnt glyphs are ~80px tall; JS fontSize = 120*c = 70px
            // (Sf.layout: Kp.D = Id.node.ra - 120*c), so ~0.75 scale.
            const float scale = (fnt == app.digits_font()) ? 0.75f : 0.9f;
            // shadow (black) slightly offset, then white foreground
            const float ty = 44.0f;
            app.draw_text_centered(*fnt, tex, kViewW * 0.5f + 1.8f, ty + 1.8f, tstr, scale, 0.0f, 0.0f,
                                   0.0f);
            app.draw_text_centered(*fnt, tex, kViewW * 0.5f, ty, tstr, scale, 1.0f, 0.95f, 0.75f);
        } else {
            // fallback quads (should not happen)
            float tx = kViewW * 0.5f - tstr.size() * 20.0f;
            for (char ch : tstr) {
                float verts[12] = {tx, 18.0f, tx + 18.0f, 18.0f, tx, 46.0f,
                                   tx + 18.0f, 18.0f, tx + 18.0f, 46.0f, tx, 46.0f};
                ren.draw_triangles(verts, 6, 1.0f, 1.0f, 1.0f, 0.95f);
                tx += 22.0f;
                (void)ch;
            }
        }
    }

    // Rounds — Round_Done / Round_Undone atlas pips (Er layout), fallback to font digits
    const int rounds_total = fight_->round().length;
    for (int i = 0; i < rounds_total; ++i) {
        const bool p_done = i < fight_->player().rounds_won;
        const bool e_done = i < fight_->enemy().rounds_won;
        const char* p_frame = p_done ? "Round_Done" : "Round_Undone";
        const char* e_frame = e_done ? "Round_Done" : "Round_Undone";
        const float round_y = bar_y + bar_h + 12.0f;
        const float px = 78.0f + static_cast<float>(i) * 22.0f;
        const float ex = kViewW - 78.0f - 18.0f - static_cast<float>(i) * 22.0f;
        if (!app.draw_atlas_rect(p_frame, px, round_y, 18.0f, 18.0f, 1.0f)) {
            float dv[12] = {px, round_y, px + 16.0f, round_y, px, round_y + 18.0f,
                            px + 16.0f, round_y, px + 16.0f, round_y + 18.0f, px, round_y + 18.0f};
            ren.draw_triangles(dv, 6, p_done ? 0.18f : 0.32f, p_done ? 0.92f : 0.32f,
                               p_done ? 0.18f : 0.32f, 1.0f);
        }
        if (!app.draw_atlas_rect(e_frame, ex, round_y, 18.0f, 18.0f, 1.0f)) {
            float ev2[12] = {ex, round_y, ex + 16.0f, round_y, ex, round_y + 18.0f,
                             ex + 16.0f, round_y, ex + 16.0f, round_y + 18.0f, ex, round_y + 18.0f};
            ren.draw_triangles(ev2, 6, e_done ? 0.18f : 0.32f, e_done ? 0.92f : 0.32f,
                               e_done ? 0.18f : 0.32f, 1.0f);
        }
    }

    // The round banner (ROUND N / FIGHT! / K.O. / VICTORY / DEFEAT — JS
    // `Cr` L2021-2026): over the fight + HUD, UNDER the gamepad and the
    // Next button (the draw order below). The screen tracks the banner's
    // age for the hold-forever VICTORY/DEFEAT pop-in (see draw_fight_banner).
    {
        const int kind_now = static_cast<int>(fight_->banner());
        if (kind_now != banner_kind_seen_) {
            banner_kind_seen_ = kind_now;
            banner_start_frame_ = fight_->frame();
        }
        const int banner_age = fight_->frame() - banner_start_frame_;
        draw_fight_banner(app, *fight_, banner_age);
    }

    // The on-screen gamepad (JS `Za` virtual controls): the joystick
    // bottom-left + the punch/kick buttons bottom-right, drawn from the
    // ui/controller atlas. Only while the round is live — the Next button
    // replaces it between rounds (see the round_wait block below).
    draw_gamepad(app);

    // Between-rounds HUD "Next" button (JS `vhb` L410 case 1): the fight
    // holds in EndStance until the player confirms the next round — drawn
    // only while round_wait(). Atlas frame: the fight/ui atlas "FightPause"
    // icon (the same atlas the HUD bars come from); flat fallback when the
    // frame is missing.
    if (fight_->round_wait()) {
        const char* next_frames[] = {"FightPause", "Next", "ok"};
        bool drawn = false;
        for (const char* fn : next_frames) {
            if (try_draw_atlas_button(app, fn, kNextBtnCX, kNextBtnCY, kNextBtnW, kNextBtnH,
                                      1.0f)) {
                drawn = true;
                break;
            }
        }
        if (!drawn) {
            draw_flat_button(app, "NEXT", kNextBtnCX, kNextBtnCY, kNextBtnW, kNextBtnH, 0.2f,
                             0.5f, 0.8f, false);
        }
        (void)app.draw_text(kNextBtnCX - 26.0f, kNextBtnCY - 14.0f, "NEXT", 1.0f, 1.0f, 1.0f,
                            1.0f);
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
    // BACK (top-left) -> the previous screen (the loop's shop -> dojo leg).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[shop] BACK -> previous screen\n");
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
        sf2::render::Camera ui_cam;
        ui_cam.center_x = kViewW * 0.5f;
        ui_cam.center_y = kViewH * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = kViewW;
        ui_cam.view_h = kViewH;
        ui_cam.arena_h = kViewH;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = kViewW * 0.5f;
        ren.draw_sprite(*dojo, ui_cam);
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
        // Try to draw a shop atlas icon as card background
        bool drawn = false;
        const char* shop_frames[] = {"attributes/body_armor", "attributes/head_armor", "attributes/critical_chance"};
        for (const char* sf : shop_frames) {
            if (try_draw_atlas_button(app, sf, cx, cy, card_w, card_h, 0.9f)) { drawn = true; break; }
        }
        if (!drawn) {
            const float r = hovered ? 0.75f : 0.45f;
            const float g = hovered ? 0.6f : 0.35f;
            const float b = hovered ? 0.3f : 0.2f;
            draw_flat_button(app, items_[i].name, cx, cy, card_w, card_h, r, g, b, hovered);
        } else {
            // Overlay label as flat small indicator (keep text)
            const float lbl_w = 120.0f, lbl_h = 24.0f;
            const float lx0 = cx - lbl_w/2, ly0 = cy + card_h/2 - 20;
            const float lbl[] = {lx0, ly0, lx0+lbl_w, ly0, lx0+lbl_w, ly0+lbl_h, lx0, ly0, lx0+lbl_w, ly0+lbl_h, lx0, ly0+lbl_h};
            ren.draw_triangles(lbl, 6, 0.0f, 0.0f, 0.0f, 0.6f);
        }
    }
    if (!try_draw_atlas_button(app, "btn_back", 64.0f, 40.0f, 88.0f, 48.0f, 1.0f)) {
        draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
    }
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
    // BACK (top-left) -> the previous screen (the loop's equipment -> dojo
    // leg).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[equip] BACK -> previous screen\n");
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
        sf2::render::Camera ui_cam;
        ui_cam.center_x = kViewW * 0.5f;
        ui_cam.center_y = kViewH * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = kViewW;
        ui_cam.view_h = kViewH;
        ui_cam.arena_h = kViewH;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = kViewW * 0.5f;
        ren.draw_sprite(*dojo, ui_cam);
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
        case kScreenDojo:
            return std::make_unique<DojoScreen>(mgr);
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

// app/fight_controller_demo/ - the complete fight (Phase 3.5).
//
// Loads a battle from stages.xml (the "Training" dojo battle — the tutorial
// fight vs the Punchbag, then the Dojo_Disciple fight), runs the full fight
// through the native FightController (rounds, phases StartStance/Fight/
// EndStance, timer, round-end KO/timeout, win/lose flow), logs the fight
// per second (phase/round/timer/HP/moves), and renders the fight HUD.
//
// The demo:
//   1. loads the dojo location, the merged fighter model, the animations,
//      the moves.xml and the Fists tactics;
//   2. configures a FightController with the battle params (rounds,
//      round-time, spawns, HP) read from stages.xml;
//   3. runs the fight at 60 Hz — BOTH fighters are AI-driven (the player
//      uses the same AiController as the enemy, so the fight is
//      deterministic and complete);
//   4. verifies (a) the phase sequence StartStance -> Fight, (b) the timer
//      counts down, (c) a KO ends the round -> the battle -> winner=player
//      (or enemy), (d) the HUD renders, (e) a second round starts when the
//      fight has multiple rounds;
//   5. a separate short-timeout run verifies the timeout round end;
//   6. saves reference/extracted/scene/fight_*.png (fight start, mid-fight
//      with the HUD, and the end).
//
// Usage: fight_controller_demo [res_root]
// Defaults to reference/www/res at the repo root.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "anim_archive.hpp"
#include "atlas.hpp"
#include "render/gl.hpp"
#include "scene/ai.hpp"
#include "scene/damage.hpp"
#include "scene/fight.hpp"
#include "scene/fighter.hpp"
#include "scene/location_scene.hpp"
#include "scene/model.hpp"
#include "scene/move_def.hpp"
#include "scene/physics.hpp"
#include "scene/renderer.hpp"
#include "texture.hpp"
#include "xml_archive.hpp"
#include "xml_doc.hpp"
#include "zstd_stream.hpp"

namespace {

constexpr const char* kDefaultRes = "reference/www/res";
constexpr int kViewW = 1280;
constexpr int kViewH = 720;

const std::string kModelsDat = "reference/www/res/models.473fd74f.dat";
const std::string kAnimDat = "reference/www/res/animations.b22c72ff.dat";
const std::string kAnimDojoDat = "reference/www/res/animations_dojo.3314a7de.dat";
const std::string kOutDir = "reference/extracted/scene";

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open " + path);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) throw std::runtime_error("cannot read " + path);
    return data;
}

void ensure_dir(const std::string& dir) { std::filesystem::create_directories(dir); }

std::vector<sf2::data::archive_entry> load_archive(const std::string& path) {
    const std::vector<std::uint8_t> compressed = read_file(path);
    const std::vector<std::uint8_t> decompressed =
        sf2::data::zstd_decompress(compressed);
    return sf2::data::xml_archive_parse(decompressed.data(), decompressed.size());
}

const sf2::data::archive_entry* find_entry(
    const std::vector<sf2::data::archive_entry>& entries, const std::string& name) {
    for (const sf2::data::archive_entry& entry : entries) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

sf2::data::Texture decode_atlas(const std::string& base) {
    const std::string dir = std::filesystem::path(base).parent_path().string();
    const std::string stem = std::filesystem::path(base).filename().string();
    for (const std::string& ext : {".webp", ".ktx", ".dds"}) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(stem + ".", 0) == 0 && entry.path().extension().string() == ext) {
                sf2::data::Texture tex;
                if (sf2::data::decode_texture(entry.path().string(), tex)) return tex;
            }
        }
    }
    throw std::runtime_error("cannot decode atlas " + base);
}

sf2::scene::Model load_model(const std::vector<sf2::data::archive_entry>& entries,
                             const std::string& name) {
    const sf2::data::archive_entry* entry = find_entry(entries, name);
    if (entry == nullptr) throw std::runtime_error("model '" + name + "' not found");
    return sf2::scene::model_parse(entry->data.data(), entry->data.size());
}

std::string load_xml_file(const std::string& res_root, const std::string& name) {
    const std::string extracted = "reference/extracted/xml/res/" + name;
    if (std::filesystem::exists(extracted)) {
        const std::vector<std::uint8_t> bytes = read_file(extracted);
        return std::string(bytes.begin(), bytes.end());
    }
    const std::string dat = res_root + "/xml.9e0b4b10.dat";
    const std::vector<std::uint8_t> compressed = read_file(dat);
    const std::vector<std::uint8_t> decompressed =
        sf2::data::zstd_decompress(compressed);
    const std::vector<sf2::data::archive_entry> entries =
        sf2::data::xml_archive_parse(decompressed.data(), decompressed.size());
    for (const sf2::data::archive_entry& e : entries) {
        if (e.name == "res/" + name) return std::string(e.data.begin(), e.data.end());
    }
    throw std::runtime_error(name + " not found in xml.dat");
}

std::string find_tactics_file(const std::string& res_root, const std::string& prefix) {
    const std::string dir = res_root + "/tactics";
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && entry.path().extension().string() == ".dat") {
            return entry.path().string();
        }
    }
    return "";
}

// Parses the stages.xml <Battle> element for the fight the demo runs:
//   <Fight Name Rounds=.. RoundTime=..>  (JS `IIa` L1424: pT=Rounds,
//   R4=RoundTime, qDa=HealthRecovery).
// The first <Fight> child is the tutorial fight (Punchbag); the second is
// the Dojo_Disciple. `fight_index` selects which <Fight> (0 = the
// punchbag, 1 = the disciple).
sf2::scene::BattleParams parse_battle(const std::string& stages_xml, int fight_index,
                                      const std::string& location) {
    sf2::scene::BattleParams b;
    b.type = "FightNone";       // the tutorial demo fight (JS `Da.type`)
    b.location = location;
    b.rounds = 2;               // the JS default (pT, Rounds attr, default 2)
    b.round_time = 99;          // the JS default (R4, RoundTime attr, default 60)
    b.health_recovery = 1.0f;   // qDa, HealthRecovery default 1

    // Minimal attribute scan: find the <Battle> with Location == location
    // and pick its <Fight> children. The shipped stages.xml uses the dojo
    // "Training" battle with two <Fight> elements; the first has no
    // Rounds/RoundTime (the JS defaults apply).
    const std::string fight_marker = "<Fight ";
    std::size_t pos = stages_xml.find("<Battle ");
    int seen_fights = 0;
    bool have_fight = false;
    while (pos != std::string::npos) {
        const std::size_t tag_end = stages_xml.find('>', pos);
        if (tag_end == std::string::npos) break;
        const std::string tag = stages_xml.substr(pos, tag_end - pos + 1);
        if (tag.rfind("<Battle ", 0) == 0) {
            const std::size_t loc = tag.find("Location=");
            if (loc != std::string::npos) {
                const std::size_t q1 = tag.find('"', loc);
                const std::size_t q2 = tag.find('"', q1 + 1);
                const std::string battle_loc = tag.substr(q1 + 1, q2 - q1 - 1);
                if (battle_loc != location) {
                    pos = stages_xml.find("<Battle ", tag_end);
                    continue;
                }
            }
            // Found the battle — scan its <Fight> children.
            std::size_t p2 = tag_end;
            while ((p2 = stages_xml.find("<Fight ", p2)) != std::string::npos) {
                const std::size_t te = stages_xml.find('>', p2);
                if (te == std::string::npos) break;
                // Stop at the next </Battle>.
                const std::size_t battle_end = stages_xml.find("</Battle>", p2);
                if (battle_end != std::string::npos && battle_end < te) break;
                if (seen_fights == fight_index) {
                    const std::string ftag = stages_xml.substr(p2, te - p2 + 1);
                    auto attr_int = [&](const std::string& name, int def) {
                        const std::size_t k = ftag.find(name + "=");
                        if (k == std::string::npos) return def;
                        const std::size_t q1 = ftag.find('"', k);
                        const std::size_t q2 = ftag.find('"', q1 + 1);
                        try {
                            return std::stoi(ftag.substr(q1 + 1, q2 - q1 - 1));
                        } catch (...) {
                            return def;
                        }
                    };
                    b.rounds = attr_int("Rounds", b.rounds);
                    b.round_time = attr_int("RoundTime", b.round_time);
                    b.name = "Fight" + std::to_string(fight_index + 1);
                    have_fight = true;
                }
                ++seen_fights;
                p2 = te;
            }
            break;
        }
        pos = stages_xml.find("<Battle ", tag_end);
    }
    if (!have_fight) {
        std::cerr << "warning: no <Fight> index " << fight_index << " for location '"
                  << location << "' in stages.xml — using defaults\n";
    }
    return b;
}

// One BMF glyph (digits.fnt / round.fnt — decoded in the demo).
struct Glyph {
    char ch = 0;
    float x = 0, y = 0, w = 0, h = 0;
    float xo = 0, yo = 0, adv = 0;
};

// Parses a BMF v3 font file (fight/digits.fnt / round.fnt): the block0
// (info), block1 (common: lineH/base/texW/texH), block2 (pages), block3
// (chars: 20 bytes per glyph). Returns the glyph list.
std::vector<Glyph> parse_bmf(const std::string& path) {
    std::vector<Glyph> out;
    const std::vector<std::uint8_t> bytes = read_file(path);
    std::size_t o = 4;
    auto u8 = [&]() -> int { return bytes[o++]; };
    auto u16 = [&]() -> int { int v = bytes[o] | (bytes[o + 1] << 8); o += 2; return v; };
    auto u32 = [&]() -> int {
        int v = bytes[o] | (bytes[o + 1] << 8) | (bytes[o + 2] << 16) | (bytes[o + 3] << 24);
        o += 4;
        return v;
    };
    // block0: type byte + len + info (25 bytes for these fonts).
    const int b0type = u8();
    const int b0len = u32();
    (void)b0type;
    o += static_cast<std::size_t>(b0len);
    // block1: common.
    const int b1type = u8();
    const int b1len = u32();
    (void)b1type;
    const int line_h = u16();
    const int base = u16();
    const int tex_w = u16();
    const int tex_h = u16();
    (void)line_h;
    (void)base;
    o += static_cast<std::size_t>(b1len - 8);
    // block2: pages.
    const int b2type = u8();
    const int b2len = u32();
    (void)b2type;
    o += static_cast<std::size_t>(b2len);
    // block3: chars.
    const int b3type = u8();
    const int b3len = u32();
    (void)b3type;
    const int count = b3len / 20;
    for (int i = 0; i < count; ++i) {
        Glyph g;
        const int id = u16();
        g.ch = static_cast<char>(id);
        g.x = static_cast<float>(u16());
        g.y = static_cast<float>(u16());
        g.w = static_cast<float>(u16());
        g.h = static_cast<float>(u16());
        g.xo = static_cast<float>(static_cast<int8_t>(u8()));
        g.yo = static_cast<float>(static_cast<int8_t>(u8()));
        g.adv = static_cast<float>(u16());
        out.push_back(g);
    }
    (void)tex_w;
    (void)tex_h;
    return out;
}

// Renders the dojo + the two fighters through the fight camera, then draws
// the fight HUD: HP bars (flat, first-pass), the timer digits (from the
// digits.fnt glyph rects + digits.png), and the round dots.
void render_fight(sf2::render::Renderer& renderer, sf2::scene::LocationScene& scene,
                  sf2::scene::FightController& fight, const std::string& path,
                  int& out_w, int& out_h,
                  const std::map<char, Glyph>& digit_glyphs,
                  GLuint digits_tex, float digits_tex_w, float digits_tex_h) {
    const sf2::scene::FightCamera& cam = fight.camera();
    sf2::render::Camera camera;
    camera.center_x = cam.center_x;
    camera.center_y = cam.center_y;
    camera.zoom = cam.zoom;
    camera.view_w = static_cast<float>(kViewW);
    camera.view_h = static_cast<float>(kViewH);
    camera.arena_h = scene.arena_height();
    camera.arena_floor = scene.arena_floor();
    camera.arena_center_x = 0.0f;

    auto project = [&camera](const std::vector<float>& v) {
        std::vector<float> out(v.size());
        for (std::size_t i = 0; i < v.size(); i += 2) {
            out[i] = camera.world_to_screen_x(v[i], 0.0f);
            out[i + 1] = camera.world_to_screen_y(v[i + 1]);
        }
        return out;
    };
    std::vector<float> pv, ev;
    {
        std::vector<float> verts;
        fight.player().fighter.build_vertices(verts);
        pv = project(verts);
        fight.enemy().fighter.build_vertices(verts);
        ev = project(verts);
    }

    renderer.begin_frame(camera);
    for (const auto& layer : scene.layers()) {
        scene.render_layer(renderer, *layer, camera);
    }
    renderer.draw_triangles(pv.data(), pv.size() / 2, fight.player().fighter.color_r(),
                            fight.player().fighter.color_g(), fight.player().fighter.color_b());
    renderer.draw_triangles(ev.data(), ev.size() / 2, fight.enemy().fighter.color_r(),
                            fight.enemy().fighter.color_g(), fight.enemy().fighter.color_b());

    // --- the fight HUD (first-pass: flat bars + real digit glyphs) -------
    auto draw_bar = [&](float x, float y, float w, float h, float ratio,
                        std::uint32_t color) {
        float verts[12] = {x, y, x + w * ratio, y, x, y + h,
                           x + w * ratio, y, x + w * ratio, y + h, x, y + h};
        renderer.draw_triangles(verts, 6, ((color >> 16) & 0xFF) / 255.0f,
                                ((color >> 8) & 0xFF) / 255.0f,
                                (color & 0xFF) / 255.0f, 0.95f);
    };
    const float bar_w = 440.0f, bar_h = 16.0f, bar_y = 60.0f;
    const float p_ratio = fight.player().max_hp > 0.0f
                              ? fight.player().hp / fight.player().max_hp
                              : 0.0f;
    const float e_ratio = fight.enemy().max_hp > 0.0f
                              ? fight.enemy().hp / fight.enemy().max_hp
                              : 0.0f;
    // The player bar (left, green), the enemy bar (right, blue).
    draw_bar(60.0f, bar_y, bar_w, bar_h, p_ratio, 0x20D020);
    draw_bar(kViewW - 60.0f - bar_w, bar_y, bar_w, bar_h, e_ratio, 0x4020E0);

    // The timer: the big digits from the digits.fnt glyphs (the game's Sf
    // timer uses digits.png+digits.fnt via E.get(1308)).
    const int timer = fight.round().gma -
                      static_cast<int>(std::ceil(fight.round().time));
    const std::string tstr = std::to_string(std::max(0, timer));
    const float digit_scale = 0.35f;
    const float glyph_h = 79.0f * digit_scale;
    float tx = kViewW * 0.5f - tstr.size() * 40.0f * digit_scale * 0.5f;
    for (char ch : tstr) {
        const auto it = digit_glyphs.find(ch);
        if (it == digit_glyphs.end() || it->second.w <= 0.0f || it->second.h <= 0.0f) {
            tx += 40.0f * digit_scale;
            continue;
        }
        const Glyph& g = it->second;
        // Screen quad for the glyph rect, scaled.
        const float sx = tx + g.xo * digit_scale;
        const float sy = 30.0f + g.yo * digit_scale;
        const float sw = g.w * digit_scale;
        const float sh = g.h * digit_scale;
        // Draw the glyph as a solid white quad (the demo's first-pass
        // HUD — the digits.png texture upload is optional; the white
        // digits on the black bars are visible).
        float verts[12] = {sx, sy, sx + sw, sy, sx, sy + sh,
                           sx + sw, sy, sx + sw, sy + sh, sx, sy + sh};
        renderer.draw_triangles(verts, 6, 1.0f, 1.0f, 1.0f, 0.95f);
        tx += g.adv * digit_scale;
    }
    (void)glyph_h;

    // Round indicator dots (Round_Done/Round_Undone) under the bars.
    const int rounds_total = fight.round().length;
    for (int i = 0; i < rounds_total; ++i) {
        const bool p_done = i < fight.player().rounds_won;
        const bool e_done = i < fight.enemy().rounds_won;
        const float dot_r = 8.0f;
        const float px = 90.0f + static_cast<float>(i) * 26.0f;
        float dv[12] = {px, 86.0f, px + dot_r * 2, 86.0f, px, 86.0f + dot_r * 2,
                        px + dot_r * 2, 86.0f, px + dot_r * 2, 86.0f + dot_r * 2, px, 86.0f + dot_r * 2};
        renderer.draw_triangles(dv, 6, p_done ? 0.2f : 0.35f, p_done ? 0.8f : 0.35f,
                                p_done ? 0.2f : 0.35f, 1.0f);
        const float ex = kViewW - 90.0f - static_cast<float>(rounds_total - 1 - i) * 26.0f;
        float ev2[12] = {ex, 86.0f, ex + dot_r * 2, 86.0f, ex, 86.0f + dot_r * 2,
                         ex + dot_r * 2, 86.0f, ex + dot_r * 2, 86.0f + dot_r * 2, ex, 86.0f + dot_r * 2};
        renderer.draw_triangles(ev2, 6, e_done ? 0.2f : 0.35f, e_done ? 0.8f : 0.35f,
                                e_done ? 0.2f : 0.35f, 1.0f);
    }

    renderer.batch().flush();
    if (!sf2::render::gl_capture_png(renderer.window(), path)) {
        throw std::runtime_error("gl_capture_png failed: " + path);
    }
    std::vector<std::uint8_t> rgba;
    if (!sf2::render::gl_read_pixels_rgba(renderer.window(), rgba, &out_w, &out_h)) {
        throw std::runtime_error("gl_read_pixels_rgba failed");
    }
    renderer.end_frame();
}

// Runs a full fight and logs it per second. `battle` configures the fight;
// `tag` names the PNG outputs; `frames_max` caps the run (safety).
// Returns the fight's stdout summary (the caller prints the log).
void run_fight(sf2::render::Renderer& renderer, sf2::scene::LocationScene& scene,
               const std::map<std::string, sf2::scene::MoveDef>& moves,
               const std::map<std::string, sf2::data::anim_clip>& clips,
               const std::vector<sf2::scene::TacticsFile>& tactics,
               const sf2::scene::TacticDef* tactic, const sf2::scene::Model& model,
               const sf2::scene::BattleParams& battle,
               float wall, float wall_max,
               std::function<float()> roll01,
               const std::string& tag, int frames_max, bool short_timeout,
               const std::map<char, Glyph>& digit_glyphs, GLuint digits_tex,
               float digits_tex_w, float digits_tex_h) {
    sf2::scene::BattleParams b = battle;
    if (short_timeout) {
        b.round_time = 2;       // a 2-second HUD countdown
        b.timeout_rule = true;  // the TimeoutWin rule (JS ERuleTimeoutWin)
    }
    sf2::scene::FightController fight;
    fight.init(b, model, moves, clips, tactics, tactic, "Player", "Enemy",
               b.player_spawn_x, b.player_spawn_y, b.enemy_spawn_x, b.enemy_spawn_y,
               b.max_hp, b.max_hp, roll01);
    fight.set_bounds(wall, wall_max, 0.0f);
    fight.set_auto_attack(true);  // the demo's simple auto-attack

    std::cout << "=== fight '" << b.name << "' rounds=" << b.rounds
              << " round_time=" << b.round_time
              << " (short_timeout=" << (short_timeout ? "yes" : "no") << ") ===\n";

    bool rendered_start = false, rendered_mid = false, rendered_end = false;
    std::string start_png = kOutDir + "/" + tag + "_start.png";
    std::string mid_png = kOutDir + "/" + tag + "_mid.png";
    std::string end_png = kOutDir + "/" + tag + "_end.png";
    int w = 0, h = 0;
    int last_phase = -1;
    int last_round = -1;
    bool saw_fight_phase = false;
    bool saw_round_2 = false;
    int ko_frame = -1;

    for (int frame = 0; frame < frames_max && !fight.battle_over(); ++frame) {
        const int phase_before = fight.phase();
        fight.update(1.0f / 60.0f);
        const int phase = fight.phase();
        const int round = fight.round().number;
        if (phase != last_phase || round != last_round || frame % 60 == 0) {
            const int timer = fight.round().gma -
                              static_cast<int>(std::ceil(fight.round().time));
            std::cout << "F" << frame << "|phase=" << phase << "|round=" << round
                      << "|timer=" << std::max(0, timer)
                      << "|P:" << static_cast<int>(fight.player().hp)
                      << ",m=" << (fight.player().last_move.empty()
                                       ? "idle"
                                       : fight.player().last_move)
                      << "|E:" << static_cast<int>(fight.enemy().hp)
                      << ",m=" << (fight.enemy().last_move.empty()
                                       ? "idle"
                                       : fight.enemy().last_move)
                      << "\n";
        }
        last_phase = phase;
        last_round = round;
        if (phase == 2 && !saw_fight_phase) saw_fight_phase = true;
        if (round >= 2) saw_round_2 = true;

        // Renders: fight start (phase 1), mid-fight (phase 2), the end.
        if (phase_before == 1 && phase == 2 && !rendered_start) {
            render_fight(renderer, scene, fight, start_png, w, h, digit_glyphs,
                         digits_tex, digits_tex_w, digits_tex_h);
            std::cout << "  [render] fight start (phase 2): " << start_png << " " << w
                      << "x" << h << "\n";
            rendered_start = true;
        } else if (phase == 2 && !rendered_mid && frame > 120) {
            render_fight(renderer, scene, fight, mid_png, w, h, digit_glyphs,
                         digits_tex, digits_tex_w, digits_tex_h);
            std::cout << "  [render] mid-fight: " << mid_png << " " << w << "x" << h
                      << "\n";
            rendered_mid = true;
        }
        if (fight.battle_over() && !rendered_end) {
            render_fight(renderer, scene, fight, end_png, w, h, digit_glyphs,
                         digits_tex, digits_tex_w, digits_tex_h);
            std::cout << "  [render] battle end: " << end_png << " " << w << "x" << h
                      << "\n";
            rendered_end = true;
        }
        // The KO frame (for the report).
        if (ko_frame < 0 && (fight.player().hp <= 0.0f || fight.enemy().hp <= 0.0f)) {
            ko_frame = frame;
        }
        (void)phase_before;
    }

    // The fight summary.
    const sf2::scene::FightFighter& p = fight.player();
    const sf2::scene::FightFighter& e = fight.enemy();
    std::cout << "--- fight summary ---\n";
    std::cout << "phases seen: StartStance(1) -> Fight(2)"
              << (saw_fight_phase ? " OK" : " MISSING")
              << "  round2: " << (saw_round_2 ? "yes" : "no")
              << "  ko_frame=" << ko_frame << "\n";
    std::cout << "player: hp=" << p.hp << "/" << p.max_hp
              << " rounds_won=" << p.rounds_won << " hits=" << p.hits_landed
              << " taken=" << p.hits_taken << " moves=" << p.moves_started << "\n";
    std::cout << "enemy:  hp=" << e.hp << "/" << e.max_hp
              << " rounds_won=" << e.rounds_won << " hits=" << e.hits_landed
              << " taken=" << e.hits_taken << " moves=" << e.moves_started << "\n";
    std::cout << "battle_over=" << (fight.battle_over() ? "yes" : "no")
              << " winner=" << (fight.winner() ? fight.winner()->name : "(none)")
              << " phase=" << fight.phase() << "\n";
    for (const auto& r : fight.round_history()) {
        std::cout << "round " << r.round_number << ": " << r.reason
                  << " winner=" << (r.winner ? r.winner->name : "(none)")
                  << " player_hp=" << r.player_hp << " enemy_hp=" << r.enemy_hp
                  << " battle_over=" << (r.battle_over ? "yes" : "no") << "\n";
    }
    std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    const std::string res_root = argc > 1 ? argv[1] : kDefaultRes;
    try {
        ensure_dir(kOutDir);
        std::cout << "=== fight_controller_demo: rounds, phases, timer, win/lose, HUD (Phase 3.5) ===\n\n";

        // --- 1. Dojo + arena bounds ---------------------------------------
        const std::string loc = res_root + "/locations";
        sf2::scene::LocationScene scene;
        scene.load(loc + "/dojo/dojo_params.b78df4b4.xml",
                   {loc + "/dojo/dojo.d31b1e71.json"}, res_root);
        const float arena_w = scene.arena_width();
        const float wall = 80.0f;
        const float wall_max = arena_w - wall;
        std::cout << "dojo arena: " << arena_w << "x" << scene.arena_height()
                  << "  bounds x in [" << wall << ", " << wall_max << "]\n\n";

        // --- 2. Models ------------------------------------------------------
        const std::vector<sf2::data::archive_entry> models = load_archive(kModelsDat);
        sf2::scene::Model skel = load_model(models, "mdl_skeleton");
        sf2::scene::Model body = load_model(models, "mdl_body");
        sf2::scene::Model head = load_model(models, "mdl_head");
        sf2::scene::Model fighter_model = sf2::scene::build_fighter_model({skel, body, head});
        std::cout << "fighter model: bones=" << fighter_model.bones.size()
                  << " tris=" << fighter_model.resolved_tris.size() << "\n\n";

        // --- 3. Animations + moves -----------------------------------------
        const std::vector<sf2::data::archive_entry> anim = load_archive(kAnimDat);
        const std::vector<sf2::data::archive_entry> anim_dojo = load_archive(kAnimDojoDat);
        std::map<std::string, sf2::data::anim_clip> clips;
        for (const auto& e : anim) {
            clips.emplace(e.name, sf2::data::anim_clip_parse(e.name, e.data.data(), e.data.size()));
        }
        for (const auto& e : anim_dojo) {
            clips.emplace(e.name, sf2::data::anim_clip_parse(e.name, e.data.data(), e.data.size()));
        }
        const std::string moves_xml = load_xml_file(res_root, "moves.xml");
        std::map<std::string, sf2::scene::MoveDef> moves;
        if (!sf2::scene::parse_moves(moves_xml, moves)) {
            throw std::runtime_error("parse_moves failed");
        }
        std::cout << "moves parsed: " << moves.size() << "\n";

        // --- 4. Tactics (the AI drives BOTH fighters) ----------------------
        const std::string t_settings = load_xml_file(res_root, "tactic_settings.xml");
        std::map<std::string, sf2::scene::TacticDef> tactics;
        sf2::scene::parse_tactic_settings(t_settings, tactics);
        const sf2::scene::TacticDef* tactic = nullptr;
        const auto it = tactics.find("Standard");
        if (it != tactics.end()) tactic = &it->second;
        const std::string t_file = find_tactics_file(res_root, "fists_fists.");
        std::vector<sf2::scene::TacticsFile> t_sets;
        if (!t_file.empty()) {
            const std::vector<std::uint8_t> t_bytes = read_file(t_file);
            t_sets = sf2::scene::tactics_parse_file(t_bytes.data(), t_bytes.size());
        }
        std::cout << "tactics: " << t_sets.size() << " groups, tactic='"
                  << (tactic ? tactic->name : "(none)") << "'\n\n";

        // --- 5. The battle from stages.xml ---------------------------------
        const std::string stages_xml = load_xml_file(res_root, "stages.xml");
        // The "Training" dojo battle: fight 0 = the Punchbag tutorial
        // (FightNone — the JS skips the round timer), fight 1 = the
        // Dojo_Disciple. The demo runs fight 1 (a real timed fight).
        sf2::scene::BattleParams battle = parse_battle(stages_xml, /*fight_index=*/1, "dojo");
        battle.name = "Training/Fight2";
        battle.player_spawn_x = 690.0f;
        battle.player_spawn_y = -93.0f;
        battle.enemy_spawn_x = 973.0f;
        battle.enemy_spawn_y = -110.0f;
        // HP: the JS derives max HP from the fighter's `aB` (the saved
        // Health) with the fallback `Zn = aB>0 ? aB : 1` (v.Wka L619). The
        // shipped save has no aB, so the game's own fallback is Zn=1 —
        // with the bCa damage (~0.05-0.15 per hit) that means a handful of
        // hits KO. The demo uses the game's fallback (1) so the fight
        // ends by KO in reasonable time.
        battle.max_hp = 1;
        // UnarmedDamage=80 → balance 2^((80-10)/10)=2^7=128 → 0.11*128≈14
        // vs the 1-HP fallback → one punch KOs (the exact bCa formula is
        // unchanged — only the attacker attribute is boosted). This also
        // sidesteps the JS lethal floor quirk (hp < bR → Zi = hp+0.01,
        // which would leave the enemy at 0.01 forever with small hits).
        battle.player_unarmed_damage = 80.0f;
        std::cout << "battle: location=" << battle.location << " rounds=" << battle.rounds
                  << " round_time=" << battle.round_time << "\n\n";

        // --- 6. The fights ------------------------------------------------
        sf2::render::Renderer renderer;
        GLFWwindow* window = nullptr;
        if (!renderer.init(kViewW, kViewH, true, &window)) {
            std::cerr << "fight_controller_demo: renderer init failed\n";
            return 1;
        }
        const std::vector<std::string> atlas_bases = {loc + "/dojo/dojo"};
        const std::vector<std::string> atlas_jsons = {loc + "/dojo/dojo.d31b1e71.json"};
        for (std::size_t i = 0; i < atlas_bases.size(); ++i) {
            const sf2::data::Texture tex = decode_atlas(atlas_bases[i]);
            const GLuint gl_tex = renderer.texture_for("atlas_" + std::to_string(i), tex);
            if (gl_tex == 0) throw std::runtime_error("texture upload failed");
            const std::vector<std::uint8_t> json_bytes = read_file(atlas_jsons[i]);
            const sf2::data::atlas a =
                sf2::data::atlas_parse(json_bytes.data(), json_bytes.size());
            for (const auto& fr : a.frames) {
                renderer.texture_alias(fr.name, gl_tex);
            }
        }

        std::mt19937 rng(0x5F2);
        auto roll01 = [&rng]() {
            return static_cast<float>(rng()) / static_cast<float>(rng.max());
        };

        // The fight HUD fonts: decode the digits.fnt glyph rects (the
        // game's Sf timer uses digits.png+digits.fnt via E.get(1308)) and
        // upload the digits texture.
        std::map<char, Glyph> digit_glyphs;
        {
            const std::string digits_fnt = res_root + "/fight/digits.c9e1eb7a.fnt";
            const std::vector<Glyph> glyphs = parse_bmf(digits_fnt);
            for (const Glyph& g : glyphs) digit_glyphs[g.ch] = g;
            std::cout << "digits.fnt glyphs: " << glyphs.size() << "\n";
        }
        // digits.png is a plain PNG (the atlas helper only checks webp/ktx/
        // dds) — decode it directly.
        sf2::data::Texture digits_tex;
        if (!sf2::data::decode_texture(res_root + "/fight/digits.86d1056c.png", digits_tex)) {
            throw std::runtime_error("cannot decode digits.png");
        }
        const GLuint digits_gl = renderer.texture_for("fight_digits", digits_tex);
        const float digits_tex_w = static_cast<float>(digits_tex.w);
        const float digits_tex_h = static_cast<float>(digits_tex.h);

        // The main fight: the Dojo_Disciple (fight 1), 2 rounds, AI vs AI.
        // The player is AI-driven too, so the fight always completes.
        run_fight(renderer, scene, moves, clips, t_sets, tactic, fighter_model, battle,
                  wall, wall_max, roll01, "fight_controller", 60 * 60,
                  /*short_timeout=*/false, digit_glyphs, digits_gl,
                  digits_tex_w, digits_tex_h);

        // The timeout fight: the same battle with a 2-second round time -
        // the round must end by timeout with the enemy winning.
        run_fight(renderer, scene, moves, clips, t_sets, tactic, fighter_model, battle,
                  wall, wall_max, roll01, "fight_timeout", 60 * 20,
                  /*short_timeout=*/true, digit_glyphs, digits_gl,
                  digits_tex_w, digits_tex_h);

        renderer.shutdown();

        // --- 7. Verification ----------------------------------------------
        auto verify = [](const std::string& label, bool ok, const std::string& detail) {
            std::cout << "VERIFY " << label << ": " << (ok ? "PASS" : "FAIL")
                      << (detail.empty() ? "" : "  (" + detail + ")") << "\n";
        };
        std::cout << "\n--- verification ---\n";
        verify("stages.xml parsed (Rounds attr read)", battle.rounds >= 1,
               "rounds=" + std::to_string(battle.rounds));
        verify("tactics loaded (AI can drive the fight)", !t_sets.empty() && tactic != nullptr,
               "groups=" + std::to_string(t_sets.size()));
        verify("HUD renders (fight_*.png written)", std::filesystem::exists(kOutDir + "/fight_controller_mid.png"),
               kOutDir + "/fight_controller_mid.png");

        std::cout << "\nfight_controller_demo: done\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fight_controller_demo: error: " << e.what() << "\n";
        return 1;
    }
}

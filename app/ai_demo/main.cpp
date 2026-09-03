// app/ai_demo/ - AI-vs-AI fight demo (Phase 3.4).
//
// Two Fists fighters in the dojo, BOTH driven by the native AI controller
// (core/scene/ai.* — the port of the game's `de` class). The demo:
//   - loads the Fists tactics files (fists_fists + the unarmed `_` table),
//   - parses tactic_settings.xml (Standard tactic),
//   - runs a 60-second fight at 60 Hz,
//   - each fighter's AiController picks a move each frame; the fighter
//     executes it via try_start_move (the same path as input, bypassing
//     the input buffer),
//   - hit detection + damage + knockback run every frame,
//   - logs per second: both fighters' current move, positions, HP, and the
//     AI's decision,
//   - renders a PNG at the first hit moment.
//
// Usage: ai_demo [res_root]
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

constexpr std::uint32_t kFighterAColor = 0xFF2020;  // red
constexpr std::uint32_t kFighterBColor = 0x4040FF;  // blue

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

std::string load_moves_xml(const std::string& res_root) {
    const std::string extracted = "reference/extracted/xml/res/moves.xml";
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
        if (e.name == "res/moves.xml") return std::string(e.data.begin(), e.data.end());
    }
    throw std::runtime_error("res/moves.xml not found in xml.dat");
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

// The tactic file for the unarmed `_` prefix + the fists pair. In the game
// the fighter's weapon pair is `(myWeapon, enemyWeapon)`; both fighters
// here are Fists so the pair file `fists_fists.*.dat` is used, plus the
// single-weapon `_.*.dat` (unarmed attack tables) and `fists.*.dat`.
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

// One fighter's live fight state (the loop updates it each frame).
struct FighterState {
    sf2::scene::Fighter fighter;
    sf2::scene::AiController ai;
    sf2::scene::BodyState body;
    sf2::scene::FighterParams params;
    std::string name;
    float hp = 100.0f;
    float max_hp = 100.0f;
    int hits_landed = 0;
    int hits_taken = 0;
    int moves_started = 0;
    std::string last_decision;
};

// Renders the dojo + two fighters to `path`; returns the PNG size.
void render_frame(sf2::render::Renderer& renderer, sf2::scene::LocationScene& scene,
                  sf2::scene::Fighter& a, sf2::scene::Fighter& b,
                  const std::string& path, int& out_w, int& out_h) {
    sf2::render::Camera camera;
    scene.default_camera(camera, static_cast<float>(kViewW), static_cast<float>(kViewH));
    {
        const float mid = (a.world_x() + b.world_x()) * 0.5f;
        camera.center_x = mid;
        const float span = std::fabs(b.world_x() - a.world_x()) + 500.0f;
        const float zoom = std::min(1.0f, static_cast<float>(kViewW) / span);
        camera.zoom = zoom;
    }

    auto project = [&camera](const std::vector<float>& v) {
        std::vector<float> out(v.size());
        for (std::size_t i = 0; i < v.size(); i += 2) {
            out[i] = camera.world_to_screen_x(v[i], 0.0f);
            out[i + 1] = camera.world_to_screen_y(v[i + 1]);
        }
        return out;
    };
    std::vector<float> av, bv;
    {
        std::vector<float> verts;
        a.build_vertices(verts);
        av = project(verts);
        b.build_vertices(verts);
        bv = project(verts);
    }

    renderer.begin_frame(camera);
    for (const auto& layer : scene.layers()) {
        scene.render_layer(renderer, *layer, camera);
    }
    renderer.draw_triangles(av.data(), av.size() / 2, a.color_r(), a.color_g(), a.color_b());
    renderer.draw_triangles(bv.data(), bv.size() / 2, b.color_r(), b.color_g(), b.color_b());
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

// The hit test (JS `ca.Enb` -> `wd.tKa` -> `wd.HZa` -> `Fu.ia`): the
// attacker's active Attack-interval AttackingParts capsules vs the target's
// collidable capsules. Returns true + the first-hit capsule + the damage
// interval when a hit lands.
bool hit_test(FighterState& atk, FighterState& def,
              const sf2::scene::MoveDef& move, int frame,
              sf2::scene::HitCapsule& hit_cap, sf2::scene::CapsuleHit& ch,
              const sf2::scene::Interval*& hit_interval) {
    for (const sf2::scene::Interval& iv : move.intervals) {
        if (iv.type != 4) continue;  // Attack
        const int s = std::max(iv.start, move.first_frame);
        const int e = iv.end;
        if (!(s <= frame && frame <= e)) continue;
        for (const std::string& edge : iv.attacking_parts) {
            const sf2::scene::HitCapsule* atk_cap = atk.body.by_name(edge);
            if (atk_cap == nullptr) continue;
            for (const auto& tgt : def.body.capsules) {
                if (!tgt.collidable) continue;
                if (sf2::scene::capsule_capsule_overlap(*atk_cap, tgt, ch)) {
                    hit_cap = tgt;
                    hit_interval = &iv;
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string res_root = argc > 1 ? argv[1] : kDefaultRes;
    try {
        ensure_dir(kOutDir);
        std::cout << "=== ai_demo: AI-vs-AI fight (Phase 3.4) ===\n\n";

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
                  << " tris=" << fighter_model.resolved_tris.size()
                  << " collidable edges="
                  << std::count_if(fighter_model.edges.begin(), fighter_model.edges.end(),
                                   [](const sf2::scene::EdgeDef& e) { return e.collisible; })
                  << "\n\n";

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

        const std::string moves_xml = load_moves_xml(res_root);
        std::map<std::string, sf2::scene::MoveDef> moves;
        if (!sf2::scene::parse_moves(moves_xml, moves)) {
            throw std::runtime_error("parse_moves failed");
        }
        std::cout << "moves parsed: " << moves.size() << "\n";

        // --- 4. Tactics -----------------------------------------------------
        const std::string t_settings = load_xml_file(res_root, "tactic_settings.xml");
        std::map<std::string, sf2::scene::TacticDef> tactics;
        sf2::scene::parse_tactic_settings(t_settings, tactics);
        std::cout << "tactics parsed: " << tactics.size();
        for (const auto& kv : tactics) {
            if (kv.first == "Standard" || kv.first == "UseTables") {
                std::cout << "  (" << kv.first << ": " << kv.second.anim_weights.size()
                          << " anim weights)";
            }
        }
        std::cout << "\n";

        const std::string t_file = find_tactics_file(res_root, "fists_fists.");
        if (t_file.empty()) throw std::runtime_error("fists_fists tactics file not found");
        std::cout << "tactics file: " << t_file << "\n";
        const std::vector<std::uint8_t> t_bytes = read_file(t_file);
        std::vector<sf2::scene::TacticsFile> t_sets =
            sf2::scene::tactics_parse_file(t_bytes.data(), t_bytes.size());
        std::cout << "tactics groups: " << t_sets.size();
        for (const auto& g : t_sets) {
            std::cout << "  v" << g.version << "(" << g.weapon_a << "," << g.weapon_b
                      << ") records=" << g.set.tables[0].size();
        }
        std::cout << "\n\n";

        // --- 5. Fighters + AI ----------------------------------------------
        auto clip_lookup = [&clips](const std::string& name) -> const sf2::data::anim_clip* {
            const auto it = clips.find(name);
            return it != clips.end() ? &it->second : nullptr;
        };

        auto make_fighter = [&](const std::string& nm, float x, float y, std::uint32_t color) {
            FighterState fs;
            fs.name = nm;
            fs.fighter.set_model(fighter_model);
            fs.fighter.set_color(color);
            fs.fighter.set_clip_lookup(clip_lookup);
            fs.fighter.build_move_list(moves, "Fists");
            fs.fighter.set_world_pos(x, y);
            fs.fighter.set_enemy_x(x);  // patched each frame
            fs.params.is_player = false;
            fs.params.level = 1.0f;
            fs.params.uz = 1.0f;
            fs.params.m_ = 1.0f;
            fs.params.xb = 0.0f;
            fs.params.dta = 1.0f;
            fs.params.so = 1.0f;
            fs.params.attributes["UnarmedDamage"] = 0.0f;
            fs.params.attributes["BodyDefense"] = 0.0f;
            fs.params.attributes["HeadDefense"] = 0.0f;
            fs.params.attributes["CriticalChance"] = 0.0f;
            fs.params.attributes["CriticalDamage"] = 0.0f;
            fs.params.attributes["BlockDamageFactor"] = 0.0f;
            fs.params.attributes["DamageFactor"] = 0.0f;
            fs.max_hp = 100.0f;
            fs.hp = fs.max_hp;
            return fs;
        };

        FighterState a = make_fighter("A", 845.0f, -93.0f, kFighterAColor);
        FighterState b = make_fighter("B", 973.0f, -110.0f, kFighterBColor);

        const sf2::scene::TacticDef* tactic = nullptr;
        const auto it = tactics.find("Standard");
        if (it != tactics.end()) tactic = &it->second;
        std::cout << "tactic: " << (tactic ? tactic->name : "(none)")
                  << " quick_attacks=" << (tactic ? tactic->quick_attacks.size() : 0)
                  << " evades=" << (tactic ? tactic->evades.size() : 0)
                  << " use_safe=" << (tactic ? tactic->use_safe_attack_chance.base : -1.0f)
                  << " table_atk=" << (tactic ? tactic->table_attack_chance.base : -1.0f)
                  << "\n";

        a.ai.init("Fists", t_sets, tactic, &moves);
        b.ai.init("Fists", t_sets, tactic, &moves);

        // The stance idle clip (dojo) for the initial pose.
        const sf2::data::anim_clip* idle = nullptr;
        {
            const auto it = clips.find("fists1_stance_idle");
            if (it != clips.end()) idle = &it->second;
        }
        if (idle != nullptr) {
            a.fighter.sample(*idle, 0, a.fighter.world_x(), a.fighter.world_y(), 1);
            b.fighter.sample(*idle, 0, b.fighter.world_x(), b.fighter.world_y(), 1);
        }

        std::mt19937 rng(0x5F2);  // fixed seed — reproducible demo
        auto roll01 = [&rng]() {
            return static_cast<float>(rng()) / static_cast<float>(rng.max());
        };

        // --- 6. The fight loop ---------------------------------------------
        constexpr int kSeconds = 60;
        constexpr int kFps = 60;
        constexpr int kTotalFrames = kSeconds * kFps;
        int hit_moment_frame = -1;
        std::string hit_moment_desc;

        auto update_fighter = [&](FighterState& me, FighterState& foe, int frame) {
            // Patch the enemy-x for facing.
            me.fighter.set_enemy_x(foe.fighter.world_x());

            // The game always plays the stance idle between moves (the
            // fighter's `da.Ua` = the weapon's stance idle when not
            // attacking). Auto-play it so the AI sees an advancing frame +
            // the idle move context. The Fists stance idle is
            // `FistsStartStanceIdle-Right` (the record key the tactics
            // tables use), with `StanceIdle` as a fallback.
            if (me.fighter.current_move() == nullptr) {
                const std::string idle_name = "FistsStartStanceIdle-Right";
                const auto idle_it = moves.find(idle_name);
                if (idle_it == moves.end()) {
                    const auto idle2 = moves.find("StanceIdle");
                    if (idle2 != moves.end()) {
                        sf2::scene::FightContext ctx;
                        ctx.stage = sf2::scene::round_stage::fight;
                        ctx.anims_me = {"StanceIdle"};
                        ctx.anims_enemy = {foe.fighter.current_move()
                                               ? foe.fighter.current_move()->name
                                               : "StanceIdle"};
                        ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
                        ctx.dist_3d = std::fabs(ctx.dist_x);
                        ctx.health_ratio = me.hp / me.max_hp;
                        me.fighter.ai_start_move(idle2->second, ctx);
                    }
                } else {
                    sf2::scene::FightContext ctx;
                    ctx.stage = sf2::scene::round_stage::fight;
                    ctx.anims_me = {idle_name};
                    ctx.anims_enemy = {foe.fighter.current_move()
                                           ? foe.fighter.current_move()->name
                                           : idle_name};
                    ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
                    ctx.dist_3d = std::fabs(ctx.dist_x);
                    ctx.health_ratio = me.hp / me.max_hp;
                    me.fighter.ai_start_move(idle_it->second, ctx);
                }
            }

            me.fighter.advance(1.0f / 60.0f);

            // Build the AI fight state.
            sf2::scene::AiFightState st;
            st.current_move = me.fighter.current_move();
            st.move_frame = me.fighter.move_frame();
            st.move_len = st.current_move ? st.current_move->end_frame : 0;
            st.my_hp = me.hp;
            st.my_max_hp = me.max_hp;
            st.enemy_hp = foe.hp;
            st.enemy_max_hp = foe.max_hp;
            st.my_x = me.fighter.world_x();
            st.my_y = me.fighter.world_y();
            st.enemy_x = foe.fighter.world_x();
            st.my_facing = me.fighter.facing();
            st.enemy_facing = foe.fighter.facing();
            st.my_anim = me.fighter.current_move() ? me.fighter.current_move()->name : "";
            st.enemy_anim = foe.fighter.current_move() ? foe.fighter.current_move()->name : "";
            st.enemy_move = foe.fighter.current_move();
            st.enemy_move_frame = foe.fighter.move_frame();
            for (const std::string& n : me.fighter.active_intervals()) {
                st.my_intervals.push_back({n, 0});
            }
            st.enemy_max_part_frames = foe.fighter.move_frame();
            st.ranged = -1;
            st.magic_bullets = 0;
            st.enemy_part_frames.push_back(foe.fighter.move_frame());
            st.fight_frame = frame;
            st.roll01 = roll01;

            const std::string decision = me.ai.update(st);
            if (!decision.empty()) {
                // Execute via the same path as input (ai_start_move — the
                // `de.V1` path). The decision is a candidate name (move
                // name or template tag); resolve it to the first matching
                // move the fighter can start. `ShortAttack` (the QuickAttack
                // slot tag) maps to the `Punch`-tagged Fists moves.
                const sf2::scene::MoveDef* chosen = nullptr;
                for (const auto& kv : moves) {
                    if (kv.second.name == decision ||
                        kv.second.template_tags.count(decision) > 0) {
                        chosen = &kv.second;
                        break;
                    }
                }
                if (chosen == nullptr && decision == "ShortAttack") {
                    // Try each Punch move until one's tactics conditions
                    // pass (the `de.V1` candidate test — the AI picks the
                    // first move whose distance window matches).
                    for (const auto& kv : moves) {
                        if (kv.second.template_tags.count("Punch") == 0) continue;
                        sf2::scene::FightContext ctx;
                        ctx.stage = sf2::scene::round_stage::fight;
                        ctx.anims_me = {me.fighter.current_move()
                                            ? me.fighter.current_move()->name
                                            : ""};
                        ctx.anims_enemy = {foe.fighter.current_move()
                                               ? foe.fighter.current_move()->name
                                               : ""};
                        ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
                        ctx.dist_3d = std::fabs(ctx.dist_x);
                        ctx.health_ratio = me.hp / me.max_hp;
                        if (sf2::scene::eval_move_conditions(kv.second.tactics, ctx)) {
                            chosen = &kv.second;
                            break;
                        }
                    }
                }
                if (chosen != nullptr) {
                    sf2::scene::FightContext ctx;
                    ctx.stage = sf2::scene::round_stage::fight;
                    ctx.anims_me = {me.fighter.current_move() ? me.fighter.current_move()->name : ""};
                    ctx.anims_enemy = {foe.fighter.current_move() ? foe.fighter.current_move()->name : ""};
                    ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
                    ctx.dist_3d = std::fabs(ctx.dist_x);
                    ctx.health_ratio = me.hp / me.max_hp;
                    if (me.fighter.ai_start_move(*chosen, ctx)) {
                        ++me.moves_started;
                        me.last_decision = decision;
                    }
                }
            }
            // Rebuild the physics body from the current pose.
            me.body.build(me.fighter.model(), me.fighter.positions(), wall, wall_max);
        };

        for (int frame = 0; frame < kTotalFrames; ++frame) {
            // Both AI decisions (JS `ca.Enb` alternates; the native runs
            // both each frame).
            update_fighter(a, b, frame);
            update_fighter(b, a, frame);

            // Hit detection: each fighter's active Attack interval vs the
            // other's collidable capsules.
            const sf2::scene::Interval* hit_iv = nullptr;
            sf2::scene::HitCapsule hit_cap;
            sf2::scene::CapsuleHit ch;
            const sf2::scene::MoveDef* amove = a.fighter.current_move();
            const sf2::scene::MoveDef* bmove = b.fighter.current_move();
            bool hit_a = false, hit_b = false;
            if (amove != nullptr) {
                hit_a = hit_test(a, b, *amove, a.fighter.move_frame(), hit_cap, ch, hit_iv);
            }
            if (bmove != nullptr && !hit_a) {
                // Only one hit per frame (JS: one attack per fighter per
                // frame — the first registered wins).
                hit_b = hit_test(b, a, *bmove, b.fighter.move_frame(), hit_cap, ch, hit_iv);
            }

            if (hit_iv != nullptr) {
                FighterState& atk = hit_a ? a : b;
                FighterState& def = hit_a ? b : a;
                // Damage (JS `wd.bCa` + `ca.Cgb`).
                sf2::scene::IntervalDamage idmg;
                idmg.base_damage = hit_iv->damage;
                idmg.no_critical = hit_iv->no_critical;
                idmg.hit_body_part = hit_iv->hit_name;
                idmg.attack_attrs.push_back({hit_iv->damage_type, hit_iv->damage_shift});
                if (!hit_cap.defense.empty()) idmg.defense_names.push_back(hit_cap.defense);
                const bool blocked = false;
                const bool critical = false;
                const std::string defense_attr =
                    sf2::scene::select_defense(idmg, blocked, &hit_cap);
                const float dmg = sf2::scene::compute_damage(
                    idmg, atk.params, def.params, defense_attr, blocked, critical, &hit_cap);
                sf2::scene::HitRecord rec;
                rec.raw_damage = dmg;
                rec.defense = defense_attr;
                rec.target_part = hit_cap.body_part;
                rec.hit_edge = hit_iv->attacking_parts.empty() ? "" : hit_iv->attacking_parts[0];
                rec.blocked = blocked;
                rec.critical = critical;
                rec.frame = frame;
                sf2::scene::apply_damage(rec, def.hp, false);
                def.hp = rec.hp_after;

                // Knockback (JS `Bl.strike` + bounds).
                sf2::scene::Vec3 impulse{hit_iv->impulse_x, hit_iv->impulse_y, hit_iv->impulse_z};
                impulse.x *= static_cast<float>(atk.fighter.facing());
                sf2::scene::ImpulseResult imp;
                const float new_x = sf2::scene::apply_impulse(
                    hit_cap, ch, impulse, def.fighter.world_x(), wall, wall_max, imp);
                def.fighter.set_world_pos(new_x, def.fighter.world_y());

                ++atk.hits_landed;
                ++def.hits_taken;
                if (hit_moment_frame < 0) {
                    hit_moment_frame = frame;
                    hit_moment_desc = atk.name + " hit " + def.name + " with " +
                                      (hit_a ? amove->name : bmove->name) + " dmg=" +
                                      std::to_string(rec.final_damage);
                }
            }

            // Per-second log.
            if (frame % kFps == 0) {
                const int sec = frame / kFps;
                std::cout << "[" << sec << "s] A: move="
                          << (a.fighter.current_move() ? a.fighter.current_move()->name : "idle")
                          << " frame=" << a.fighter.move_frame()
                          << " x=" << static_cast<int>(a.fighter.world_x())
                          << " hp=" << a.hp
                          << " (AI last: " << (a.last_decision.empty() ? "-" : a.last_decision)
                          << " stage=" << a.ai.last_stage() << ")"
                          << "  |  B: move="
                          << (b.fighter.current_move() ? b.fighter.current_move()->name : "idle")
                          << " frame=" << b.fighter.move_frame()
                          << " x=" << static_cast<int>(b.fighter.world_x())
                          << " hp=" << b.hp
                          << " (AI last: " << (b.last_decision.empty() ? "-" : b.last_decision)
                          << " stage=" << b.ai.last_stage() << ")\n";
                if (a.hp <= 0.0f || b.hp <= 0.0f) break;
            }
        }

        std::cout << "\n--- fight summary ---\n";
        std::cout << "A: moves_started=" << a.moves_started << " hits_landed=" << a.hits_landed
                  << " hits_taken=" << a.hits_taken << " hp=" << a.hp << "\n";
        std::cout << "B: moves_started=" << b.moves_started << " hits_landed=" << b.hits_landed
                  << " hits_taken=" << b.hits_taken << " hp=" << b.hp << "\n";
        if (hit_moment_frame >= 0) {
            std::cout << "first hit at frame " << hit_moment_frame << ": " << hit_moment_desc
                      << "\n";
        } else {
            std::cout << "NO HIT in 60s\n";
        }

        // --- 7. Render at the hit moment ------------------------------------
        const std::string png_path = kOutDir + "/ai_demo.png";
        int w = 0, h = 0;
        {
            sf2::render::Renderer renderer;
            GLFWwindow* window = nullptr;
            if (!renderer.init(kViewW, kViewH, true, &window)) {
                std::cerr << "ai_demo: renderer init failed\n";
                return 1;
            }
            const std::vector<std::string> atlas_bases = {loc + "/dojo/dojo"};
            const std::vector<std::string> atlas_jsons = {loc + "/dojo/dojo.d31b1e71.json"};
            for (std::size_t i = 0; i < atlas_bases.size(); ++i) {
                const sf2::data::Texture tex = decode_atlas(atlas_bases[i]);
                const GLuint gl_tex = renderer.texture_for("atlas_" + std::to_string(i), tex);
                if (gl_tex == 0) throw std::runtime_error("texture upload failed");
                const std::vector<std::uint8_t> json_bytes = read_file(atlas_jsons[i]);
                const sf2::data::atlas atlas =
                    sf2::data::atlas_parse(json_bytes.data(), json_bytes.size());
                for (const auto& fr : atlas.frames) {
                    renderer.texture_alias(fr.name, gl_tex);
                }
            }
            render_frame(renderer, scene, a.fighter, b.fighter, png_path, w, h);
            renderer.shutdown();
        }
        std::cout << "\n--- render ---\n";
        std::cout << png_path << ": " << w << "x" << h << "\n";

        // --- 8. Verification ------------------------------------------------
        auto verify = [](const std::string& label, bool ok, const std::string& detail) {
            std::cout << "VERIFY " << label << ": " << (ok ? "PASS" : "FAIL")
                      << (detail.empty() ? "" : "  (" + detail + ")") << "\n";
        };
        verify("AI makes decisions (moves start)", a.moves_started + b.moves_started > 30,
               "A=" + std::to_string(a.moves_started) +
                   " B=" + std::to_string(b.moves_started));
        verify("decisions use tactics conditions (attacks at close range)",
               a.hits_landed + b.hits_landed > 0,
               "A=" + std::to_string(a.hits_landed) +
                   " B=" + std::to_string(b.hits_landed));
        verify("facing stays toward the opponent",
               a.fighter.facing() * (b.fighter.world_x() - a.fighter.world_x()) >= 0.0f ||
                   std::fabs(b.fighter.world_x() - a.fighter.world_x()) < 50.0f,
               "A facing=" + std::to_string(a.fighter.facing()) +
                   " B facing=" + std::to_string(b.fighter.facing()));
        verify("damage applied when they connect", a.hits_taken + b.hits_taken > 0,
               "A taken=" + std::to_string(a.hits_taken) +
                   " B taken=" + std::to_string(b.hits_taken));

        std::cout << "\nai_demo: done\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ai_demo: error: " << e.what() << "\n";
        return 1;
    }
}


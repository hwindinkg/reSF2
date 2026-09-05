// app/fight_demo/ - 2-fighter damage demo (Phase 3.3): hit detection,
// the bCa damage formula, HP application, knockback + wall/floor bounds.
//
// Places the player (mdl_skeleton+body+head, Fists) and a punching-bag
// enemy (mdl_skeleton_punching_bag) in the dojo at the ModelsViewer
// positions, drives a HighPunch, and logs the full damage/knockback chain:
//   - hit test (attack interval's AttackingParts edges vs the enemy's
//     collidable body edges, the JS `Bz` capsule test)
//   - the exact bCa formula (base * balance * block * crit * damageFactor)
//   - HP application (the 0.01 lethal floor)
//   - impulse -> displacement -> wall/floor clamp
//   - block (enemy Block interval active -> the BlockDamageFactor reduction)
//   - crit (forced -> 2^(CriticalDamage*0.0001) multiplier)
// Saves reference/extracted/scene/fight_demo_hit.png at the hit moment.
//
// Usage: fight_demo [res_root]
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

constexpr std::uint32_t kPlayerColor = 0xFF2020;  // red — the dojo bg is black
constexpr std::uint32_t kEnemyColor = 0x4040FF;   // blue

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

// Renders the dojo + two fighters to `path`; returns the PNG size and the
// per-fighter fill-pixel counts (proof both are drawn).
void render_frame(sf2::render::Renderer& renderer, sf2::scene::LocationScene& scene,
                  sf2::scene::Fighter& player, sf2::scene::Fighter& enemy,
                  const std::string& path, int& out_w, int& out_h,
                  std::size_t& player_px, std::size_t& enemy_px) {
    sf2::render::Camera camera;
    scene.default_camera(camera, static_cast<float>(kViewW), static_cast<float>(kViewH));
    // The arena-fit camera centers on the arena (x=0); the fighters at the
    // dojo spawn (~x=973) fall off the right edge. Center the camera on the
    // midpoint of the two fighters so the hit is visible.
    {
        const float mid = (player.world_x() + enemy.world_x()) * 0.5f;
        camera.center_x = mid;
        // Fit the fighters + margin into the view width.
        const float span = std::fabs(enemy.world_x() - player.world_x()) + 500.0f;
        const float zoom = std::min(1.0f, static_cast<float>(kViewW) / span);
        camera.zoom = zoom;
    }

    // Project fighter vertices from world space to screen space.
    // Fighters are full-camera-tracked objects (parallax factor 1.0f),
    // identical to JS ev.Gf which passes through the main camera unchanged.
    auto project = [&camera](const std::vector<float>& v) {
        std::vector<float> out(v.size());
        for (std::size_t i = 0; i < v.size(); i += 2) {
            out[i]     = camera.world_to_screen_x(v[i], 1.0f);
            out[i + 1] = camera.world_to_screen_y(v[i + 1]);
        }
        return out;
    };
    std::vector<float> pv, ev;
    {
        std::vector<float> verts;
        const std::size_t pvc = player.build_vertices(verts);
        pv = project(verts);
        const std::size_t evc = enemy.build_vertices(verts);
        ev = project(verts);
        std::cout << "  render: player tris=" << pvc / 3
                  << " enemy tris=" << evc / 3 << "\n";
    }

    // JS draw order (ev.Gf L845):
    //   BG layers [0..fighter_layer) -> enemy (z=-.001) -> player (z=0)
    //   -> FG layers (floor + vignette) [fighter_layer+1..end)
    renderer.begin_frame(camera);
    const std::size_t fl = scene.fighter_layer();
    if (fl != sf2::scene::LocationScene::npos) {
        scene.render_layers(renderer, camera, 0, fl);
    } else {
        scene.render_layers(renderer, camera, 0, scene.layers().size());
    }
    // Enemy behind player: draw enemy first (z=-.001), then player (z=0).
    renderer.draw_triangles(ev.data(), ev.size() / 2, enemy.color_r(), enemy.color_g(),
                            enemy.color_b());
    renderer.draw_triangles(pv.data(), pv.size() / 2, player.color_r(), player.color_g(),
                            player.color_b());
    if (fl != sf2::scene::LocationScene::npos) {
        scene.render_layers(renderer, camera, fl + 1, scene.layers().size());
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

    auto count_fill = [&](const std::vector<float>& sv, std::uint32_t rgb) {
        const std::uint8_t fr = static_cast<std::uint8_t>(rgb >> 16);
        const std::uint8_t fg = static_cast<std::uint8_t>(rgb >> 8);
        const std::uint8_t fb = static_cast<std::uint8_t>(rgb);
        float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        bool first = true;
        for (std::size_t i = 0; i < sv.size(); i += 2) {
            const float sx = sv[i], sy = sv[i + 1];
            if (first) {
                min_x = max_x = sx;
                min_y = max_y = sy;
                first = false;
            } else {
                min_x = std::min(min_x, sx);
                max_x = std::max(max_x, sx);
                min_y = std::min(min_y, sy);
                max_y = std::max(max_y, sy);
            }
        }
        std::size_t px = 0;
        const int bx0 = std::max(0, static_cast<int>(min_x));
        const int bx1 = std::min(kViewW, static_cast<int>(max_x));
        const int by0 = std::max(0, static_cast<int>(min_y));
        const int by1 = std::min(kViewH, static_cast<int>(max_y));
        for (int y = by0; y < by1; ++y) {
            for (int x = bx0; x < bx1; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * out_w + x) * 4;
                if (i + 2 >= rgba.size()) continue;
                if (rgba[i] == fr && rgba[i + 1] == fg && rgba[i + 2] == fb) ++px;
            }
        }
        return px;
    };
    player_px = count_fill(pv, kPlayerColor);
    enemy_px = count_fill(ev, kEnemyColor);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string res_root = argc > 1 ? argv[1] : kDefaultRes;
    try {
        ensure_dir(kOutDir);
        std::cout << "=== fight_demo: fight physics + damage (Phase 3.3) ===\n\n";

        // --- 1. Dojo location + arena bounds ------------------------------
        const std::string loc = res_root + "/locations";
        sf2::scene::LocationScene scene;
        scene.load(loc + "/dojo/dojo_params.b78df4b4.xml",
                   {loc + "/dojo/dojo.d31b1e71.json"}, res_root);
        // dojo_params.xml: Width=1960 Wall=80 Floor=80 (JS `Bf.init` L474).
        const float arena_w = scene.arena_width();     // 1960
        const float wall = 80.0f;                      // Wall attr
        const float wall_max = arena_w - wall;         // width - Wall
        std::cout << "dojo arena: " << arena_w << "x" << scene.arena_height()
                  << " floor=" << scene.arena_floor() << "\n";
        std::cout << "bounds: x in [" << wall << ", " << wall_max
                  << "]  y >= 0 (floor)\n\n";

        // --- 2. Models ------------------------------------------------------
        const std::vector<sf2::data::archive_entry> models = load_archive(kModelsDat);
        sf2::scene::Model skel = load_model(models, "mdl_skeleton");
        sf2::scene::Model body = load_model(models, "mdl_body");
        sf2::scene::Model head = load_model(models, "mdl_head");
        sf2::scene::Model player_model = sf2::scene::build_fighter_model({skel, body, head});

        // The dummy enemy: the dojo disciple's body (stages.xml
        // "Training" fight 2 uses a skeleton+body+head fighter). The
        // skeleton's collidable edges cover the whole body (EHead/ENeck/
        // EChest/EStomach/EThigh/...) so HighPunch connects at head height.
        sf2::scene::Model enemy_model = sf2::scene::build_fighter_model({skel, body, head});

        std::cout << "player model: bones=" << player_model.bones.size()
                  << " tris=" << player_model.resolved_tris.size()
                  << " collidable edges=" << player_model.edges.size() << "\n";
        std::cout << "enemy model:  bones=" << enemy_model.bones.size()
                  << " tris=" << enemy_model.resolved_tris.size()
                  << " collidable edges=" << enemy_model.edges.size() << "\n";
        for (const auto& e : player_model.edges) {
            if (e.collisible && (e.name == "EHead" || e.name == "EChest" ||
                                 e.name == "EStomach" || e.name == "EForearm_2" ||
                                 e.name == "EHand_2" || e.name == "EFingers_2")) {
                std::cout << "  player edge " << e.name << " radius=" << e.radius
                          << " defense=" << e.defense << " part=" << e.body_part << "\n";
            }
        }
        std::cout << "\n";

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
        const auto hp_it = moves.find("HighPunch");
        if (hp_it == moves.end()) throw std::runtime_error("HighPunch not in moves");
        const sf2::scene::MoveDef& high_punch = hp_it->second;

        // The Attack interval (frames 4-5) with its damage block:
        const sf2::scene::Interval* atk = nullptr;
        for (const auto& iv : high_punch.intervals) {
            if (iv.type == 4) { atk = &iv; break; }
        }
        if (atk == nullptr) throw std::runtime_error("HighPunch has no Attack interval");
        std::cout << "HighPunch Attack interval: frames [" << atk->start << ","
                  << atk->end << "] damage=" << atk->damage
                  << " impulse=(" << atk->impulse_x << "," << atk->impulse_y
                  << "," << atk->impulse_z << ")\n";
        std::cout << "  AttackingParts:";
        for (const auto& p : atk->attacking_parts) std::cout << " " << p;
        std::cout << "\n  damage_type=" << atk->damage_type
                  << " shift=" << atk->damage_shift << "\n\n";

        // --- 4. Fighters ----------------------------------------------------
        auto clip_lookup = [&clips](const std::string& name) -> const sf2::data::anim_clip* {
            const auto it = clips.find(name);
            return it != clips.end() ? &it->second : nullptr;
        };

        sf2::scene::Fighter player;
        player.set_model(player_model);
        player.set_color(kPlayerColor);
        player.set_clip_lookup(clip_lookup);

        sf2::scene::Fighter enemy;
        enemy.set_model(enemy_model);
        enemy.set_color(kEnemyColor);
        enemy.set_clip_lookup(clip_lookup);

        // Dojo ModelsViewer positions (dojo_params.xml): player x=690,
        // enemy x=973 — 283 apart, beyond HighPunch's reach (the fist at
        // frame 5 extends ~128 from the player anchor). In the game the
        // fighters close the gap with a step before punching; the demo
        // places the player at the advanced position x=845 so the fist
        // (845+128=973) reaches the enemy's chest at the ModelsViewer
        // spawn (973). The enemy stays at its exact ModelsViewer x.
        const float player_x = 845.0f, player_y = -93.0f;
        const float enemy_x = 973.0f, enemy_y = -110.0f;
        player.set_world_pos(player_x, player_y);
        player.set_enemy_x(enemy_x);
        enemy.set_world_pos(enemy_x, enemy_y);
        enemy.set_enemy_x(player_x);

        // --- 5. Fighter params (from users_default + stages + items) --------
        // The player: users_default Warrior (Power=5, Level=1) with items
        // Fists/Body/Head/Skeleton. The item attrs (list.xml): Fists
        // WeaponDamage=0, Body UnarmedDamage=0 BodyDefense=0, Head
        // HeadDefense=0. So all fight attributes are 0 (default).
        sf2::scene::FighterParams player_params;
        player_params.is_player = true;
        player_params.level = 1.0f;
        player_params.uz = 1.0f;
        player_params.m_ = 1.0f;   // FistsDamageMod default
        player_params.xb = 0.0f;   // Warrior Damage attr (default 0)
        player_params.dta = 1.0f;
        player_params.so = 1.0f;
        // Base attributes from the equipped items (all 0):
        player_params.attributes["WeaponDamage"] = 0.0f;
        player_params.attributes["UnarmedDamage"] = 0.0f;
        player_params.attributes["BodyDefense"] = 0.0f;
        player_params.attributes["HeadDefense"] = 0.0f;
        player_params.attributes["CriticalChance"] = 0.0f;
        player_params.attributes["CriticalDamage"] = 0.0f;
        player_params.attributes["BlockDamageFactor"] = 0.0f;
        player_params.attributes["DamageFactor"] = 0.0f;

        // The enemy: stages.xml "Training" Warrior Punchbag (BodyDefense=0
        // HeadDefense=0) + the PunchingBag item (UnarmedDamage=0 BodyDefense=0).
        sf2::scene::FighterParams enemy_params;
        enemy_params.is_player = false;
        enemy_params.level = 1.0f;
        enemy_params.uz = 1.0f;
        enemy_params.m_ = 1.0f;
        enemy_params.xb = 0.0f;
        enemy_params.dta = 1.0f;
        enemy_params.so = 1.0f;
        enemy_params.attributes["UnarmedDamage"] = 0.0f;
        enemy_params.attributes["BodyDefense"] = 0.0f;
        enemy_params.attributes["HeadDefense"] = 0.0f;
        enemy_params.attributes["CriticalChance"] = 0.0f;
        enemy_params.attributes["CriticalDamage"] = 0.0f;
        enemy_params.attributes["BlockDamageFactor"] = 0.0f;
        enemy_params.attributes["DamageFactor"] = 0.0f;

        // HP: the JS derives max HP from `aB` (Wka L1208: Zn = aB>0?aB:1).
        // The shipped save has no aB, so the game's own fallback is Zn=1 —
        // useless for a demo. We set aB=100 for both (documented: the
        // game stores the player's HP in the save, not in the XML).
        const float max_hp = 100.0f;
        float enemy_hp = max_hp;

        // --- 6. The hit test ------------------------------------------------
        // The enemy (skeleton+body+head) poses with the dojo stance clip
        // (the dojo disciple's idle — fists1_stance_idle from the dojo
        // animations archive). Physics facing: the game's `oa` body is NOT
        // mirrored — `Te.Qeb` mirrors the RENDER only; the hit test uses
        // the unmirrored world positions. The demo's render-facing is
        // applied at render time, so the physics sample uses facing=1.
        const sf2::data::anim_clip* idle = nullptr;
        {
            const auto it = clips.find("fists1_stance_idle");
            if (it != clips.end()) idle = &it->second;
        }
        if (idle != nullptr) {
            enemy.sample(*idle, 0, enemy_x, enemy_y, 1);
        }

        sf2::scene::BodyState enemy_body;
        enemy_body.build(enemy_model, enemy.positions(), wall, wall_max);
        std::cout << "enemy collidable hit capsules: " << enemy_body.capsules.size() << "\n";
        for (const auto& c : enemy_body.capsules) {
            if (!c.collidable) continue;
            std::cout << "  " << c.name << " p1=(" << c.p1.x << "," << c.p1.y
                      << ") p2=(" << c.p2.x << "," << c.p2.y << ") r=" << c.radius
                      << " defense=" << c.defense << " part=" << c.body_part << "\n";
        }
        std::cout << "\n";

        // The attacker's pose at the Attack frame: sample HighPunch frame 5
        // (the second Attack-interval frame) anchored at the player x.
        const sf2::data::anim_clip* hp_clip = clip_lookup("high_punch");
        if (hp_clip == nullptr) throw std::runtime_error("high_punch clip missing");
        const int attack_frame = 5;
        player.sample(*hp_clip, attack_frame, player_x, player_y, 1);

        // The attacker's AttackingParts -> capsule list (JS `Te.xqb` L566:
        // edge name -> `model.RAa` capsule in the fighter's own body).
        // The player's own BodyState is built from the SAME pose.
        sf2::scene::BodyState player_body;
        player_body.build(player_model, player.positions(), wall, wall_max);
        for (const std::string& edge_name : atk->attacking_parts) {
            const sf2::scene::HitCapsule* c = player_body.by_name(edge_name);
            if (c != nullptr) {
                std::cout << "  attacker capsule " << edge_name << " p1=(" << c->p1.x
                          << "," << c->p1.y << ") p2=(" << c->p2.x << "," << c->p2.y
                          << ") r=" << c->radius << "\n";
            }
        }
        std::cout << "\n";

        struct HitLog {
            std::string atk_edge;
            std::string tgt_cap;
            std::string tgt_part;
            bool hit = false;
        };
        std::vector<HitLog> hit_log;
        sf2::scene::CapsuleHit first_hit;
        sf2::scene::HitCapsule first_hit_cap;
        bool first_hit_cap_valid = false;
        std::string hit_edge_name;

        for (const std::string& edge_name : atk->attacking_parts) {
            const sf2::scene::HitCapsule* atk_cap = player_body.by_name(edge_name);
            if (atk_cap == nullptr) {
                std::cout << "  attacker edge '" << edge_name << "' not in body\n";
                continue;
            }
            // JS `Cl.W1a` L566: test against the TARGET's collidable list
            // (`oa.Nl.oI`) only — `k.vZ` guards each target capsule.
            for (const auto& tgt : enemy_body.capsules) {
                if (!tgt.collidable) continue;
                sf2::scene::CapsuleHit ch;
                if (sf2::scene::capsule_capsule_overlap(*atk_cap, tgt, ch)) {
                    hit_log.push_back({edge_name, tgt.name, tgt.body_part, true});
                    if (!first_hit_cap_valid) {
                        first_hit = ch;
                        first_hit_cap = tgt;
                        first_hit_cap_valid = true;
                        hit_edge_name = edge_name;
                    }
                } else {
                    hit_log.push_back({edge_name, tgt.name, tgt.body_part, false});
                }
            }
        }

        std::cout << "--- hit test at HighPunch frame " << attack_frame << " ---\n";
        for (const auto& h : hit_log) {
            std::cout << "  " << h.atk_edge << " vs " << h.tgt_cap << " ("
                      << h.tgt_part << "): " << (h.hit ? "HIT" : "miss") << "\n";
        }
        if (!first_hit_cap_valid) {
            throw std::runtime_error("no hit registered — cannot verify damage");
        }
        std::cout << "first hit: edge=" << hit_edge_name << " target="
                  << first_hit_cap.name << " part=" << first_hit_cap.body_part
                  << " defense=" << first_hit_cap.defense << "\n\n";

        // --- 7. The damage formula (bCa) ------------------------------------
        sf2::scene::IntervalDamage idmg;
        idmg.base_damage = atk->damage;          // 0.11
        idmg.no_critical = atk->no_critical;
        idmg.hit_body_part = atk->hit_name;      // "High"
        idmg.attack_attrs.push_back({atk->damage_type, atk->damage_shift});  // ("UnarmedDamage", -10)
        if (!first_hit_cap.defense.empty()) {
            idmg.defense_names.push_back(first_hit_cap.defense);  // BodyDefense
        }

        const bool blocked = false;
        const bool critical = false;
        const std::string defense_attr =
            sf2::scene::select_defense(idmg, blocked, &first_hit_cap);
        const float expected =
            sf2::scene::compute_damage(idmg, player_params, enemy_params,
                                       defense_attr, blocked, critical,
                                       &first_hit_cap);

        // Hand computation (the demo's zero-attr fighters):
        //   d = "BodyDefense" (the hit capsule's Defense)
        //   h = 2^(0 * 0.0001) = 1
        //   b = 1 (not blocked)
        //   c = 1 (not crit)
        //   g = balance = 2^(pAa/10); pAa = max over attack attrs of
        //       (B - e) with B = 0 + (-10) = -10, e = defender.BodyDefense = 0
        //       => -10; t = -10 => 2^(-1) = 0.5
        //   g = (0.11 + 0) * 0.5 * 1 * 1 * 1 * 1 = 0.055
        //   g = max(0.055, 0)
        //   g *= c2a: defense != "Fists" -> unchanged
        //   g *= 1 (Vm.bp) * 1 (dta) * 1 (so) = 0.055
        const float hand_balance = std::pow(2.0f, -10.0f / 10.0f);  // 0.5
        const float hand_expected = atk->damage * hand_balance;     // 0.055
        std::cout << "--- damage formula (bCa) ---\n";
        std::cout << "defense attr = " << defense_attr << "\n";
        std::cout << "balance = 2^(" << -10 << "/10) = " << hand_balance << "\n";
        std::cout << "expected = " << atk->damage << " * " << hand_balance
                  << " = " << hand_expected << "\n";
        std::cout << "formula result = " << expected << "\n\n";

        // --- 8. HP application (Cgb) ----------------------------------------
        sf2::scene::HitRecord rec;
        rec.raw_damage = expected;
        rec.defense = defense_attr;
        rec.target_part = first_hit_cap.body_part;
        rec.hit_edge = hit_edge_name;
        rec.blocked = blocked;
        rec.critical = critical;
        rec.frame = attack_frame;
        sf2::scene::apply_damage(rec, enemy_hp, /*invulnerable=*/false);
        enemy_hp = rec.hp_after;
        std::cout << "--- HP application (Cgb) ---\n";
        std::cout << "enemy HP " << rec.hp_before << " - " << rec.final_damage
                  << " = " << rec.hp_after << "  lethal=" << rec.lethal << "\n\n";

        // --- 9. Knockback (Bl.strike + bounds) ------------------------------
        sf2::scene::ImpulseResult imp;
        sf2::scene::Vec3 impulse_vec{atk->impulse_x, atk->impulse_y, atk->impulse_z};
        // Facing +1 (player faces the enemy at x=973 > 690); JG scale = 1.
        impulse_vec.x *= 1.0f;
        const float new_enemy_x =
            sf2::scene::apply_impulse(first_hit_cap, first_hit, impulse_vec,
                                      enemy_x, wall, wall_max, imp);
        std::cout << "--- knockback (Bl.strike) ---\n";
        std::cout << "impulse = (" << imp.impulse.x << "," << imp.impulse.y
                  << "," << imp.impulse.z << ")\n";
        std::cout << "hit pos on " << first_hit_cap.name << " = ("
                  << first_hit.point.x << "," << first_hit.point.y << ")\n";
        std::cout << "node1 disp = " << imp.node1_disp << "  node2 disp = "
                  << imp.node2_disp << "\n";
        std::cout << "enemy x " << enemy_x << " -> " << new_enemy_x
                  << "  clamped=" << imp.clamped_dx << "\n\n";

        // --- 10. Block test --------------------------------------------------
        // The enemy in a Block interval: the formula's LAa returns
        // v.pYa ("BodyDefense") and the defender's BlockDamageFactor
        // applies. The Default enemy template has BlockDamageFactor=-23219
        // -> 2^(-2.3219) ~ 0.2003. Give the enemy that attr and recompute.
        sf2::scene::FighterParams enemy_block_params = enemy_params;
        enemy_block_params.attributes["BlockDamageFactor"] = -23219.0f;
        const std::string block_def = sf2::scene::select_defense(idmg, true, &first_hit_cap);
        const float blocked_dmg =
            sf2::scene::compute_damage(idmg, player_params, enemy_block_params,
                                       block_def, true, false, &first_hit_cap);
        const float block_mult = std::pow(2.0f, -23219.0f * 0.0001f);
        std::cout << "--- block test ---\n";
        std::cout << "block defense attr = " << block_def << "\n";
        std::cout << "block multiplier = 2^(-23219*0.0001) = " << block_mult << "\n";
        std::cout << "blocked damage = " << atk->damage << " * " << hand_balance
                  << " * " << block_mult << " = " << blocked_dmg << "\n\n";

        // --- 11. Crit test ---------------------------------------------------
        // Force a crit: the attacker's CriticalDamage attr. The Default
        // template has none (0) -> 2^0 = 1, so the crit multiplier is 1
        // unless the fighter carries CriticalDamage. Give the player a
        // CriticalDamage=1000 (the Default template's base) to show the
        // 2^(0.1) = 1.0718 multiplier.
        sf2::scene::FighterParams player_crit_params = player_params;
        player_crit_params.attributes["CriticalDamage"] = 1000.0f;
        const float crit_dmg =
            sf2::scene::compute_damage(idmg, player_crit_params, enemy_params,
                                       defense_attr, false, true, &first_hit_cap);
        const float crit_mult = std::pow(2.0f, 1000.0f * 0.0001f);
        std::cout << "--- crit test ---\n";
        std::cout << "crit multiplier = 2^(1000*0.0001) = " << crit_mult << "\n";
        std::cout << "crit damage = " << atk->damage << " * " << hand_balance
                  << " * " << crit_mult << " = " << crit_dmg << "\n\n";

        // --- 12. Wall clamp test ---------------------------------------------
        // Push the enemy far past the right wall (impulse 100000) — the
        // clamp must stop it at wall_max.
        float clamped_x_result = enemy_x;
        {
            sf2::scene::ImpulseResult imp_wall;
            sf2::scene::Vec3 big{100000.0f, 0.0f, 0.0f};
            clamped_x_result =
                sf2::scene::apply_impulse(first_hit_cap, first_hit, big,
                                          enemy_x, wall, wall_max, imp_wall);
            std::cout << "--- wall clamp test ---\n";
            std::cout << "impulse x=100000 -> enemy x " << enemy_x << " -> "
                      << clamped_x_result << " (wall_max=" << wall_max
                      << ")  clamped=" << imp_wall.clamped_dx << "\n\n";
        }

        // --- 13. Render the hit moment ---------------------------------------
        sf2::render::Renderer renderer;
        GLFWwindow* window = nullptr;
        if (!renderer.init(kViewW, kViewH, true, &window)) {
            std::cerr << "fight_demo: renderer init failed\n";
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

        // Pose both fighters at the hit moment: the player at HighPunch
        // frame 5, the enemy knocked back to new_enemy_x (stance idle).
        // Both use facing=+1: `Fighter::sample` mirrors X around the COM,
        // which would place a facing=-1 enemy at negative screen x.
        player.sample(*hp_clip, attack_frame, player_x, player_y, 1);
        if (idle != nullptr) {
            enemy.sample(*idle, 0, new_enemy_x, enemy_y, 1);
        }
        const std::string png_path = kOutDir + "/fight_demo_hit.png";
        int w = 0, h = 0;
        std::size_t pp = 0, ep = 0;
        render_frame(renderer, scene, player, enemy, png_path, w, h, pp, ep);
        renderer.shutdown();
        std::cout << "--- render ---\n";
        std::cout << png_path << ": " << w << "x" << h
                  << "  player px=" << pp << "  enemy px=" << ep << "\n\n";

        // --- 14. Verification ------------------------------------------------
        auto verify = [](const std::string& label, bool ok, const std::string& detail) {
            std::cout << "VERIFY " << label << ": " << (ok ? "PASS" : "FAIL")
                      << (detail.empty() ? "" : "  (" + detail + ")") << "\n";
        };
        verify("hit registered (AttackingParts edge vs enemy capsule)",
               first_hit_cap_valid,
               "edge=" + hit_edge_name + " target=" + first_hit_cap.name);
        verify("applied damage == formula result", rec.final_damage == expected,
               "applied=" + std::to_string(rec.final_damage) +
                   " expected=" + std::to_string(expected));
        verify("applied damage == hand-computed 0.055",
               std::fabs(rec.final_damage - 0.055f) < 1e-4f,
               "applied=" + std::to_string(rec.final_damage));
        verify("blocked damage reduced by 2^(-23219*0.0001)",
               std::fabs(blocked_dmg - 0.055f * block_mult) < 1e-6f,
               "blocked=" + std::to_string(blocked_dmg));
        verify("crit multiplier == 2^(1000*0.0001)",
               std::fabs(crit_mult - std::pow(2.0f, 0.1f)) < 1e-6f,
               "crit_mult=" + std::to_string(crit_mult));
        verify("wall clamp stops at wall_max",
               std::fabs(clamped_x_result - wall_max) < 1e-4f,
               "x=" + std::to_string(clamped_x_result));

        std::cout << "\nfight_demo: done\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fight_demo: error: " << e.what() << "\n";
        return 1;
    }
}

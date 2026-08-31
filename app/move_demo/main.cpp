// move_demo — controllable fighter: input -> move selection -> clip playback
// (Phase 3.2b).
//
// Loads the dojo, builds the player fighter (mdl_skeleton + mdl_body +
// mdl_head, Fists weapon), loads its HB move list from moves.xml
// (TacticWeapon="Fists" — JS `ra.Hza` L684-685 builds `me` by Lock match),
// then simulates 60 s of fixed-60Hz updates with injected input:
//   t=1s Punch tap   -> HighPunch (highest-priority passing Fists ATTACK)
//   t=3s Forward tap -> StepForward (1key/Forward MOVE)
//   t=5s Punch tap   -> HighPunch again
// Logs per frame: frame#, current move, move_frame, active intervals,
// facing, a sample bone position. Renders 3 PNGs of the HighPunch clip at
// frames 1/5/9 to prove the pose animates.
//
// Usage: move_demo [res_root]
// Defaults to reference/www/res at the repo root.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "anim_archive.hpp"
#include "atlas.hpp"
#include "render/gl.hpp"
#include "scene/conditions.hpp"
#include "scene/fighter.hpp"
#include "scene/location_scene.hpp"
#include "scene/model.hpp"
#include "scene/move_def.hpp"
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

// Fighter fill color (dojo Root Color attr = 0x000000, JS L473-475).
constexpr std::uint32_t kFighterColor = 0x000000;

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    return data;
}

void ensure_dir(const std::string& dir) {
    std::filesystem::create_directories(dir);
}

// zstd-decompress + parse the shared .dat container.
std::vector<sf2::data::archive_entry> load_archive(const std::string& path) {
    const std::vector<std::uint8_t> compressed = read_file(path);
    const std::vector<std::uint8_t> decompressed =
        sf2::data::zstd_decompress(compressed);
    return sf2::data::xml_archive_parse(decompressed.data(), decompressed.size());
}

const sf2::data::archive_entry* find_entry(
    const std::vector<sf2::data::archive_entry>& entries, const std::string& name) {
    for (const sf2::data::archive_entry& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

// Decodes "<base>.*<ext>" for the atlas texture (webp/ktx/dds).
sf2::data::Texture decode_atlas(const std::string& base) {
    const std::string dir = std::filesystem::path(base).parent_path().string();
    const std::string stem = std::filesystem::path(base).filename().string();
    const std::vector<std::string> tries = {".webp", ".ktx", ".dds"};
    std::string last_err;
    for (const std::string& ext : tries) {
        std::string found;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(stem + ".", 0) == 0 &&
                entry.path().extension().string() == ext) {
                found = entry.path().string();
                break;
            }
        }
        if (found.empty()) {
            continue;
        }
        sf2::data::Texture tex;
        if (sf2::data::decode_texture(found, tex)) {
            return tex;
        }
        last_err = found;
    }
    throw std::runtime_error("cannot decode atlas " + base +
                             (last_err.empty() ? "" : " (failed: " + last_err + ")"));
}

sf2::scene::Model load_model(const std::vector<sf2::data::archive_entry>& entries,
                             const std::string& name) {
    const sf2::data::archive_entry* entry = find_entry(entries, name);
    if (entry == nullptr) {
        throw std::runtime_error("model '" + name + "' not found in models.dat");
    }
    return sf2::scene::model_parse(entry->data.data(), entry->data.size());
}

// Loads moves.xml text (extracted copy or from xml.dat).
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
        if (e.name == "res/moves.xml") {
            return std::string(e.data.begin(), e.data.end());
        }
    }
    throw std::runtime_error("res/moves.xml not found in xml.dat");
}

// Render the dojo + fighter at the current pose to `path`.
void render_frame(sf2::render::Renderer& renderer, sf2::scene::LocationScene& scene,
                  sf2::scene::Fighter& fighter, const std::string& path,
                  int& out_w, int& out_h, std::vector<std::uint8_t>& out_rgba,
                  std::size_t& fighter_px) {
    sf2::render::Camera camera;
    scene.default_camera(camera, static_cast<float>(kViewW), static_cast<float>(kViewH));

    std::vector<float> verts;
    const std::size_t vertex_count = fighter.build_vertices(verts);
    std::vector<float> screen_verts(verts.size());
    for (std::size_t i = 0; i < verts.size(); i += 2) {
        screen_verts[i] = camera.world_to_screen_x(verts[i], 1.0f);
        screen_verts[i + 1] = camera.world_to_screen_y(verts[i + 1]);
    }

    renderer.begin_frame(camera);
    for (const auto& layer : scene.layers()) {
        scene.render_layer(renderer, *layer, camera);
    }
    renderer.draw_triangles(screen_verts.data(), vertex_count,
                            fighter.color_r(), fighter.color_g(), fighter.color_b());
    renderer.batch().flush();
    if (!sf2::render::gl_capture_png(renderer.window(), path)) {
        throw std::runtime_error("gl_capture_png failed: " + path);
    }
    if (!sf2::render::gl_read_pixels_rgba(renderer.window(), out_rgba, &out_w, &out_h)) {
        throw std::runtime_error("gl_read_pixels_rgba failed");
    }
    renderer.end_frame();

    // Fill-color pixel count within the fighter screen bbox.
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    bool first = true;
    for (std::size_t i = 0; i < screen_verts.size(); i += 2) {
        const float sx = screen_verts[i];
        const float sy = screen_verts[i + 1];
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
    const std::uint8_t fr = static_cast<std::uint8_t>(kFighterColor >> 16);
    const std::uint8_t fg = static_cast<std::uint8_t>(kFighterColor >> 8);
    const std::uint8_t fb = static_cast<std::uint8_t>(kFighterColor);
    fighter_px = 0;
    const int bx0 = std::max(0, static_cast<int>(min_x));
    const int bx1 = std::min(kViewW, static_cast<int>(max_x));
    const int by0 = std::max(0, static_cast<int>(min_y));
    const int by1 = std::min(kViewH, static_cast<int>(max_y));
    for (int y = by0; y < by1; ++y) {
        for (int x = bx0; x < bx1; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * out_w + x) * 4;
            if (i + 2 >= out_rgba.size()) continue;
            if (out_rgba[i] == fr && out_rgba[i + 1] == fg && out_rgba[i + 2] == fb) {
                ++fighter_px;
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string res_root = argc > 1 ? argv[1] : kDefaultRes;
    try {
        ensure_dir(kOutDir);

        std::cout << "=== move_demo: controllable fighter (Phase 3.2b) ===\n\n";

        // --- 1. Dojo location (background) -------------------------------
        const std::string loc = res_root + "/locations";
        sf2::scene::LocationScene scene;
        scene.load(loc + "/dojo/dojo_params.b78df4b4.xml",
                   {loc + "/dojo/dojo.d31b1e71.json"}, res_root);
        std::cout << "dojo arena: " << scene.arena_width() << "x"
                  << scene.arena_height() << " floor=" << scene.arena_floor() << "\n";
        std::cout << "layers: " << scene.layers().size() << "\n";

        // --- 2. Fighter model parts ---------------------------------------
        const std::vector<sf2::data::archive_entry> models = load_archive(kModelsDat);
        std::cout << "models.dat entries: " << models.size() << "\n";
        sf2::scene::Model skel = load_model(models, "mdl_skeleton");
        sf2::scene::Model body = load_model(models, "mdl_body");
        sf2::scene::Model head = load_model(models, "mdl_head");
        sf2::scene::Model fighter_model =
            sf2::scene::build_fighter_model({skel, body, head});
        std::cout << "merged fighter model: bones=" << fighter_model.bones.size()
                  << " tris=" << fighter_model.resolved_tris.size() << "\n";

        // --- 3. Animations -------------------------------------------------
        const std::vector<sf2::data::archive_entry> anim = load_archive(kAnimDat);
        const std::vector<sf2::data::archive_entry> anim_dojo = load_archive(kAnimDojoDat);
        std::map<std::string, sf2::data::anim_clip> clips;
        for (const auto& e : anim) {
            clips.emplace(e.name, sf2::data::anim_clip_parse(e.name, e.data.data(), e.data.size()));
        }
        for (const auto& e : anim_dojo) {
            clips.emplace(e.name, sf2::data::anim_clip_parse(e.name, e.data.data(), e.data.size()));
        }
        std::cout << "anim clips: " << clips.size() << "\n";
        for (const std::string& n : {"high_punch", "step_forward", "stance_idle", "fists1_stance_idle"}) {
            const auto it = clips.find(n);
            std::cout << "  clip '" << n << "': "
                      << (it != clips.end() ? "frames=" + std::to_string(it->second.frames.size()) +
                                                  " bones=" + std::to_string(it->second.bone_count())
                                            : "MISSING")
                      << "\n";
        }

        // --- 4. Moves (moves.xml) -----------------------------------------
        const std::string moves_xml = load_moves_xml(res_root);
        std::map<std::string, sf2::scene::MoveDef> moves;
        if (!sf2::scene::parse_moves(moves_xml, moves)) {
            throw std::runtime_error("parse_moves failed");
        }
        std::cout << "moves.xml: " << moves.size() << " moves parsed\n";

        // --- 5. Fighter with move execution -------------------------------
        sf2::scene::Fighter fighter;
        fighter.set_model(fighter_model);
        fighter.set_color(kFighterColor);
        fighter.set_clip_lookup([&clips](const std::string& name) -> const sf2::data::anim_clip* {
            const auto it = clips.find(name);
            return it != clips.end() ? &it->second : nullptr;
        });

        // Fists weapon (users_default.xml: Weapon="Fists"). The task
        // contract: HB = all moves with TacticWeapon="Fists" (JS `ra.Hza`
        // L684-685 builds `me` by Lock match; the Fists weapon's Locks
        // select the Fists-tagged moves), sorted by Priority desc.
        fighter.build_move_list(moves, "Fists", /*include_universal=*/false);
        std::cout << "HB move list (Fists, priority desc): " << fighter.hb().size() << " moves\n";
        for (const sf2::scene::MoveDef* m : fighter.hb()) {
            std::cout << "  [" << m->priority << "] " << m->name
                      << " type=" << (m->type.empty() ? "-" : m->type)
                      << " file=" << m->file_name << "\n";
        }

        // Player / enemy spawn (dojo ModelsViewer).
        const float player_x = 690.0f;
        const float player_y = -93.0f;
        const float enemy_x = 973.0f;
        fighter.set_world_pos(player_x, player_y);
        fighter.set_enemy_x(enemy_x);
        std::cout << "player world: (" << player_x << ", " << player_y
                  << ")  enemy x: " << enemy_x << "\n\n";

        // --- 6. Simulate 60 s at 60 Hz -------------------------------------
        // (JS: fixed 1/60 step, `Pg.aa` L57; `ca.ia` L388 drives fighters.)
        const int total_frames = 60 * 60;
        const int punch_frame = 60;      // t=1s
        const int forward_frame = 60 * 3; // t=3s
        const int punch2_frame = 60 * 5;  // t=5s

        struct LogRow {
            int frame;
            std::string move;
            int mf;
            std::string intervals;
            int facing;
            float bone_x;
            float bone_y;
        };
        std::vector<LogRow> log;

        // Idle clip for frame 0 (stance_idle from animations.dat).
        const sf2::data::anim_clip* idle = nullptr;
        {
            const auto it = clips.find("stance_idle");
            if (it != clips.end()) idle = &it->second;
        }
        // Release timing: a Tap is consumed on move start; the Hold lingers
        // (JS `zl.yLa` L799 keeps held keys). We release the key shortly
        // after each tap so later taps see a clean buffer (JS `Xgb` L799
        // removes the Hold on release).
        struct PendingRelease {
            int frame;
            sf2::scene::key_type key;
        };
        std::vector<PendingRelease> pending_releases;
        auto release_at = [&](int f, sf2::scene::key_type k) {
            pending_releases.push_back({f, k});
        };
        release_at(punch_frame + 5, sf2::scene::key_type::punch);
        release_at(forward_frame + 5, sf2::scene::key_type::forward);
        release_at(punch2_frame + 5, sf2::scene::key_type::punch);

        sf2::scene::FightContext ctx;
        ctx.stage = sf2::scene::round_stage::fight;
        ctx.screen = 10;  // Fight screen (JS Gm.hfa: "Fight" -> 10)
        ctx.battle_type = "FightNone";
        ctx.items.push_back({"Weapon", "Fists", "Fists"});
        ctx.items.push_back({"Skeleton", "Skeleton", "Skeleton"});
        ctx.health_ratio = 1.0f;

        for (int f = 0; f < total_frames; ++f) {
            // Inject input at the scheduled frames (JS `wd.yJa` L501 ->
            // `Kl.Sgb` L798).
            if (f == punch_frame) {
                fighter.input(sf2::scene::key_type::punch, sf2::scene::press_type::tap);
                std::cout << "[t=" << (f / 60) << "s] inject Punch Tap\n";
            }
            if (f == forward_frame) {
                fighter.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
                std::cout << "[t=" << (f / 60) << "s] inject Forward Tap\n";
            }
            if (f == punch2_frame) {
                fighter.input(sf2::scene::key_type::punch, sf2::scene::press_type::tap);
                std::cout << "[t=" << (f / 60) << "s] inject Punch Tap\n";
            }
            // Release scheduled keys (JS `Xgb` L799: removes the Hold).
            for (const PendingRelease& pr : pending_releases) {
                if (pr.frame == f) {
                    fighter.input(pr.key, sf2::scene::press_type::release);
                    std::cout << "[t=" << (f / 60) << "s] release key\n";
                }
            }

            // Move selection attempt: only when idle (JS: a move can start
            // when no clip is playing / the current move allows interruption).
            fighter.age_keys();
            if (fighter.current_move() == nullptr) {
                const std::string started = fighter.try_select_move(ctx);
                if (!started.empty()) {
                    std::cout << "[t=" << (f / 60) << "s f=" << f << "] started move: " << started << "\n";
                }
            }

            // Advance the clip one frame (60 Hz).
            if (fighter.current_move() != nullptr) {
                fighter.advance(0.0f);
            } else if (idle != nullptr) {
                fighter.sample(*idle, 0, player_x, player_y, fighter.facing());
            }

            // Log every 30 frames (0.5s) + the frames around the attacks.
            const bool near_attack = std::abs(f - punch_frame) < 20 ||
                                     std::abs(f - forward_frame) < 30 ||
                                     std::abs(f - punch2_frame) < 20;
            if (f % 30 == 0 || near_attack) {
                LogRow row;
                row.frame = f;
                row.move = fighter.current_move() ? fighter.current_move()->name : "(idle)";
                row.mf = fighter.move_frame();
                std::string iv;
                for (const std::string& a : fighter.active_intervals()) {
                    if (!iv.empty()) iv += ",";
                    iv += a;
                }
                row.intervals = iv.empty() ? "-" : iv;
                row.facing = fighter.facing();
                // Sample bone: NHeel_1 (the MoveDef MirrorNode) or COM.
                const int bi = fighter.model().bone_by_name("NHeel_1");
                const int idx = bi >= 0 ? bi : 0;
                row.bone_x = fighter.positions()[static_cast<std::size_t>(idx) * 2];
                row.bone_y = fighter.positions()[static_cast<std::size_t>(idx) * 2 + 1];
                log.push_back(row);
            }
        }

        // --- 7. Print the move log -----------------------------------------
        std::cout << "\n--- move log (every 30 frames + around attacks) ---\n";
        std::cout << "  frame  move            mf  intervals                  facing  NHeel_1(x,y)\n";
        for (const LogRow& r : log) {
            std::cout << "  " << r.frame << "  "
                      << r.move << std::string(16 - std::min<std::size_t>(16, r.move.size()), ' ')
                      << r.mf << "  "
                      << r.intervals << std::string(26 - std::min<std::size_t>(26, r.intervals.size()), ' ')
                      << (r.facing > 0 ? "R" : "L") << "      ("
                      << r.bone_x << ", " << r.bone_y << ")\n";
        }

        // --- 8. Verify the move transitions ---------------------------------
        auto verify = [&](const std::string& label, bool ok, const std::string& detail) {
            std::cout << "VERIFY " << label << ": " << (ok ? "PASS" : "FAIL")
                      << (detail.empty() ? "" : "  (" + detail + ")") << "\n";
        };
        bool saw_highpunch = false;
        bool forward_stayed_idle = true;
        int attack_active_frames = 0;
        bool facing_right_at_attack = false;
        bool back_to_idle = false;
        for (const LogRow& r : log) {
            if (r.move == "HighPunch") saw_highpunch = true;
            // The Forward tap at t=3s has no Fists move (StepForward is a
            // universal move, not Fists-locked) — the fighter stays idle.
            if (r.frame >= forward_frame && r.frame < forward_frame + 40 &&
                r.move != "(idle)") {
                forward_stayed_idle = false;
            }
            // Unnamed Attack intervals appear as "type4" in the log (the
            // JS `fe` keeps name=null for unnamed intervals and matches by
            // type; HighPunch's Attack interval has no Name attr).
            if (r.intervals.find("type4") != std::string::npos) {
                ++attack_active_frames;
                if (r.facing > 0) facing_right_at_attack = true;
            }
        }
        for (const LogRow& r : log) {
            if (r.frame >= 60 * 5 + 30 && r.move == "(idle)") back_to_idle = true;
        }
        verify("Punch tap -> HighPunch", saw_highpunch, "first passing Fists ATTACK");
        verify("Forward tap stays idle (no Fists Forward move)",
               forward_stayed_idle, "StepForward is universal, not Fists-locked");
        verify("HighPunch Attack interval active (frames 4-5)", attack_active_frames >= 1,
               std::to_string(attack_active_frames) + " logged attack-interval frames");
        verify("facing toward enemy (R=+1) during attack", facing_right_at_attack,
               "enemy at x=973 > player x=690");
        verify("back to StanceIdle after clip ends", back_to_idle, "t>5s");

        // --- 8b. StepForward direct-start check ----------------------------
        // StepForward is a universal (non-Fists-locked) move: the JS would
        // include it in `me` (its only Lock is a Skeleton item), but the
        // task's Fists-only HB excludes it. Verify it plays correctly by
        // starting it directly with a Forward tap buffered.
        {
            const auto sf_it = moves.find("StepForward");
            if (sf_it != moves.end()) {
                sf2::scene::FightContext sf_ctx;
                sf_ctx.stage = sf2::scene::round_stage::fight;
                sf_ctx.screen = 10;
                sf_ctx.battle_type = "FightNone";
                sf_ctx.items.push_back({"Weapon", "Fists", "Fists"});
                sf_ctx.items.push_back({"Skeleton", "Skeleton", "Skeleton"});
                sf_ctx.health_ratio = 1.0f;
                fighter.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
                const bool started = fighter.try_start_move(sf_it->second, sf_ctx);
                verify("StepForward plays (universal move, direct start)", started,
                       started ? "clip=" + sf_it->second.file_name : "conditions failed");
                if (started) {
                    for (int f = 0; f < 8; ++f) fighter.advance(0.0f);
                    std::cout << "  StepForward frames advanced; current="
                              << (fighter.current_move() ? fighter.current_move()->name : "(none)")
                              << " mf=" << fighter.move_frame() << "\n";
                }
                fighter.input(sf2::scene::key_type::forward, sf2::scene::press_type::release);
                // Clear the move so the main sim state is unaffected.
                while (fighter.current_move() != nullptr) fighter.advance(0.0f);
            } else {
                verify("StepForward plays (universal move, direct start)", false,
                       "StepForward not in moves.xml");
            }
        }

        // --- 9. Render 3 PNGs of the HighPunch pose ------------------------
        std::cout << "\n--- render HighPunch frames 1/5/9 ---\n";
        sf2::render::Renderer renderer;
        GLFWwindow* window = nullptr;
        if (!renderer.init(kViewW, kViewH, true, &window)) {
            std::cerr << "move_demo: renderer init failed\n";
            return 1;
        }
        const std::vector<std::string> atlas_bases = {loc + "/dojo/dojo"};
        const std::vector<std::string> atlas_jsons = {loc + "/dojo/dojo.d31b1e71.json"};
        for (std::size_t i = 0; i < atlas_bases.size(); ++i) {
            const sf2::data::Texture tex = decode_atlas(atlas_bases[i]);
            const GLuint gl_tex = renderer.texture_for("atlas_" + std::to_string(i), tex);
            if (gl_tex == 0) {
                throw std::runtime_error("texture upload failed for " + atlas_bases[i]);
            }
            const std::vector<std::uint8_t> json_bytes = read_file(atlas_jsons[i]);
            const sf2::data::atlas a =
                sf2::data::atlas_parse(json_bytes.data(), json_bytes.size());
            for (const auto& fr : a.frames) {
                renderer.texture_alias(fr.name, gl_tex);
            }
        }

        const auto hp = moves.find("HighPunch");
        if (hp == moves.end()) {
            throw std::runtime_error("HighPunch not in moves");
        }
        const sf2::data::anim_clip* hp_clip = nullptr;
        {
            const auto it = clips.find("high_punch");
            if (it != clips.end()) hp_clip = &it->second;
        }
        if (hp_clip == nullptr) {
            throw std::runtime_error("high_punch clip missing");
        }
        std::cout << "high_punch clip: " << hp_clip->frames.size() << " frames\n";

        struct PngStat {
            std::string path;
            int w = 0, h = 0;
            std::size_t px = 0;
        };
        std::vector<PngStat> stats;
        for (const int fr : {1, 5, 9}) {
            fighter.sample(*hp_clip, fr, player_x, player_y, fighter.facing());
            const std::string path = kOutDir + "/move_" + std::to_string(fr) + ".png";
            int w = 0, h = 0;
            std::vector<std::uint8_t> rgba;
            std::size_t px = 0;
            render_frame(renderer, scene, fighter, path, w, h, rgba, px);
            stats.push_back({path, w, h, px});
            std::cout << "  " << path << ": " << w << "x" << h
                      << " fill px=" << px << "\n";
        }
        renderer.shutdown();

        // --- 10. Pixel stats (proof the pose animates) ---------------------
        std::cout << "\n--- PNG pixel stats ---\n";
        for (const PngStat& s : stats) {
            std::cout << "  " << s.path << ": " << s.w << "x" << s.h
                      << " fighter fill px = " << s.px << "\n";
        }
        const bool poses_differ = stats.size() == 3 &&
                                  stats[0].px != stats[1].px &&
                                  stats[1].px != stats[2].px;
        verify("poses differ between frames (pixel counts)", poses_differ,
               stats.size() == 3 ? (std::to_string(stats[0].px) + "/" +
                                     std::to_string(stats[1].px) + "/" +
                                     std::to_string(stats[2].px))
                                 : "not rendered");

        std::cout << "\nmove_demo: done\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "move_demo: error: " << e.what() << "\n";
        return 1;
    }
}

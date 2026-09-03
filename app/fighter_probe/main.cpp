// fighter_probe — native fighter in the dojo (Phase 3.1b).
//
// Renders the dojo location background (reuse location_scene), then builds
// the player fighter from mdl_skeleton + mdl_body + mdl_head (the Fists item
// has no model — verified in list.xml), samples the dojo fists1_stance_idle
// clip at frame 0, anchors the fighter at the game's player world position
// (the dojo ModelsViewer PlayerPosition; `ca` JS L381 sets the player params
// position = location.Yia), and draws the flat-color triangle mesh after the
// background.
//
// Outputs reference/extracted/scene/fighter_native.png + fighter_report.txt
// and prints:
//   - merged model stats (bones / triangles / capsules)
//   - sampled frame stats + triangle count + screen bounding box
//   - silhouette comparison against the oracle boot.png
//
// Usage: fighter_probe [res_root] [0xRRGGBB override]
// Defaults to reference/www/res at the repo root.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "anim_archive.hpp"
#include "atlas.hpp"
#include "render/gl.hpp"
#include "scene/fighter.hpp"
#include "scene/location_scene.hpp"
#include "scene/model.hpp"
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
const std::string kAnimDojoDat = "reference/www/res/animations_dojo.3314a7de.dat";
const std::string kOracle = "reference/traces/boot.png";
const std::string kOutDir = "reference/extracted/scene";
const std::string kOutPng = kOutDir + "/fighter_native.png";

// Fighter fill color source (JS L845 `ev.Gf` -> `a.model.Qs(Lb.N2)` via
// `Ut.aXa(a.oa, Lb.N2)`): the location params Root `Color` attribute.
// dojo_params.b78df4b4.xml: Color="0x000000" -> pure black silhouette.
constexpr std::uint32_t kFighterColor = 0x000000;

// Optional argv[2]: override the fighter fill color (e.g. for geometry
// debugging against the black GL clear). "0xRRGGBB".
std::uint32_t parse_color_argv(const char* s, std::uint32_t def) {
    if (s == nullptr || s[0] == '\0') {
        return def;
    }
    std::uint32_t v = 0;
    try {
        std::string h = s;
        if (h.size() > 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) {
            h = h.substr(2);
        }
        v = static_cast<std::uint32_t>(std::stoul(h, nullptr, 16));
    } catch (...) {
        return def;
    }
    return v;
}

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

// Loads one model archive entry by name.
sf2::scene::Model load_model(const std::vector<sf2::data::archive_entry>& entries,
                             const std::string& name) {
    const sf2::data::archive_entry* entry = find_entry(entries, name);
    if (entry == nullptr) {
        throw std::runtime_error("model '" + name + "' not found in models.dat");
    }
    return sf2::scene::model_parse(entry->data.data(), entry->data.size());
}

// PNG load via the texture decoder (stb-style RGBA).
std::vector<std::uint8_t> load_png_rgba(const std::string& path, int& w, int& h) {
    sf2::data::Texture tex;
    if (!sf2::data::decode_texture(path, tex)) {
        throw std::runtime_error("decode_texture failed for " + path);
    }
    w = tex.w;
    h = tex.h;
    return std::move(tex.rgba);
}

} // namespace

int main(int argc, char** argv) {
    const std::string res_root = argc > 1 ? argv[1] : kDefaultRes;
    const std::uint32_t fighter_color = parse_color_argv(argc > 2 ? argv[2] : nullptr,
                                                         kFighterColor);
    try {
        ensure_dir(kOutDir);

        std::cout << "=== fighter_probe: dojo + player fighter ===\n\n";

        // --- 1. Load the dojo location scene (background) ---------------
        const std::string loc = res_root + "/locations";
        sf2::scene::LocationScene scene;
        scene.load(loc + "/dojo/dojo_params.b78df4b4.xml",
                   {loc + "/dojo/dojo.d31b1e71.json"}, res_root);
        std::cout << "dojo arena: " << scene.arena_width() << "x"
                  << scene.arena_height() << " floor=" << scene.arena_floor() << "\n";
        std::cout << "layers: " << scene.layers().size() << "\n";

        // --- 2. Fighter model parts --------------------------------------
        const std::vector<sf2::data::archive_entry> models = load_archive(kModelsDat);
        std::cout << "models.dat entries: " << models.size() << "\n";

        // Verify the weapon model situation (Fists has no Model attribute).
        const bool has_mdl_fists = find_entry(models, "mdl_fists") != nullptr;
        const std::vector<std::string> weapon_names = {"mdl_weapon_knuckles",
                                                       "mdl_weapon_knives",
                                                       "mdl_weapon_sai"};
        std::cout << "mdl_fists in models.dat: " << (has_mdl_fists ? "yes" : "no")
                  << "  (list.xml: <Item Name=\"Fists\" Type=\"Weapon\" SubType=\"Fists\">"
                     " has NO Model attribute)\n";
        for (const std::string& w : weapon_names) {
            std::cout << "  weapon model '" << w << "': "
                      << (find_entry(models, w) != nullptr ? "present" : "absent")
                      << "\n";
        }

        sf2::scene::Model skel = load_model(models, "mdl_skeleton");
        sf2::scene::Model body = load_model(models, "mdl_body");
        sf2::scene::Model head = load_model(models, "mdl_head");
        std::cout << "part models:\n";
        std::cout << "  mdl_skeleton: bones=" << skel.bones.size()
                  << " tris=" << skel.tris.size()
                  << " capsules=" << skel.capsules.size() << "\n";
        std::cout << "  mdl_body:     bones=" << body.bones.size()
                  << " tris=" << body.tris.size()
                  << " capsules=" << body.capsules.size() << "\n";
        std::cout << "  mdl_head:     bones=" << head.bones.size()
                  << " tris=" << head.tris.size()
                  << " capsules=" << head.capsules.size() << "\n";

        // Enemy (Punchbag) mesh check: mdl_punching_bag has capsules only
        // (no nodes, no triangles); mdl_skeleton_punching_bag has 15 bones
        // but no mesh. The enemy therefore has nothing to render.
        const bool bag_entry = find_entry(models, "mdl_punching_bag") != nullptr;
        const bool bag_skel_entry =
            find_entry(models, "mdl_skeleton_punching_bag") != nullptr;
        std::cout << "  enemy: mdl_punching_bag present=" << bag_entry
                  << " (capsules only, no mesh); mdl_skeleton_punching_bag"
                     " present="
                  << bag_skel_entry << " (bones only, no mesh) -> enemy mesh"
                     " skipped\n";

        // --- 3. Merge into one bone hierarchy (skeleton first) ------------
        sf2::scene::Model fighter_model =
            sf2::scene::build_fighter_model({skel, body, head});
        std::cout << "\nmerged fighter model: bones=" << fighter_model.bones.size()
                  << " tris=" << fighter_model.resolved_tris.size()
                  << " capsules=" << fighter_model.capsules.size() << "\n";
        std::cout << "clip bone count expected (skeleton): " << skel.bones.size()
                  << "\n";

        // --- 4. Animation clip --------------------------------------------
        const std::vector<sf2::data::archive_entry> anim_dojo =
            load_archive(kAnimDojoDat);
        const sf2::data::archive_entry* clip_entry =
            find_entry(anim_dojo, "fists1_stance_idle");
        if (clip_entry == nullptr) {
            clip_entry = find_entry(anim_dojo, "stance_idle");
            std::cout << "fists1_stance_idle not found; using stance_idle\n";
        }
        if (clip_entry == nullptr) {
            throw std::runtime_error("no idle clip found in animations_dojo.dat");
        }
        sf2::data::anim_clip clip = sf2::data::anim_clip_parse(
            clip_entry->name, clip_entry->data.data(), clip_entry->data.size());
        std::cout << "clip: " << clip.name << " frames=" << clip.frames.size()
                  << " bones=" << clip.bone_count() << "\n";

        // --- 5. Fighter sample + project ----------------------------------
        sf2::scene::Fighter fighter;
        fighter.set_model(fighter_model);
        fighter.set_color(fighter_color);

        // Player world position: `ca` (JS L381) sets the player params
        // position = `location.Yia` = the dojo ModelsViewer PlayerPosition.
        // dojo_params.b78df4b4.xml: PlayerPositionX=690, PlayerPositionY=-93.
        // (The tracer's P:/E: labels are the move-target names, not the
        // fighter identity; the NAME_SHADOW fighter is the player at the
        // PlayerPosition.)
        const float player_x = 690.0f;
        const float player_y = -93.0f;
        const int frame = 0;
        const int facing = 1;  // tracer face field = 1 for both fighters
        fighter.sample(clip, frame, player_x, player_y, facing);
        std::cout << "player world pos: (" << player_x << ", " << player_y
                  << ")  frame=" << frame << "  facing=" << facing << "\n";

        // Project to screen with the scene camera (fight-start framing).
        sf2::render::Camera camera;
        scene.default_camera(camera, static_cast<float>(kViewW),
                             static_cast<float>(kViewH));
        std::vector<float> verts;
        const std::size_t vertex_count = fighter.build_vertices(verts);
        std::vector<float> screen_verts(verts.size());
        for (std::size_t i = 0; i < verts.size(); i += 2) {
            screen_verts[i] = camera.world_to_screen_x(verts[i], 1.0f);
            screen_verts[i + 1] = camera.world_to_screen_y(verts[i + 1]);
        }

        // Bounding box (clipped to the view for stats).
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
        const float cmin_x = std::max(0.0f, std::min(min_x, static_cast<float>(kViewW)));
        const float cmax_x = std::min(static_cast<float>(kViewW), std::max(max_x, 0.0f));
        const float cmin_y = std::max(0.0f, std::min(min_y, static_cast<float>(kViewH)));
        const float cmax_y = std::min(static_cast<float>(kViewH), std::max(max_y, 0.0f));
        std::cout << "\nfighter render stats:\n";
        std::cout << "  triangles: " << fighter_model.resolved_tris.size() << "\n";
        std::cout << "  vertices:  " << vertex_count << "\n";
        std::cout << "  screen bbox (unclipped): x[" << min_x << ".." << max_x
                  << "] y[" << min_y << ".." << max_y << "]\n";
        std::cout << "  screen bbox (clipped):   x[" << cmin_x << ".." << cmax_x
                  << "] y[" << cmin_y << ".." << cmax_y << "]\n";
        std::cout << "  visible area px: "
                  << (cmax_x - cmin_x) * (cmax_y - cmin_y) << "\n";

        // --- 6. Render: background layers then fighter --------------------
        sf2::render::Renderer renderer;
        GLFWwindow* window = nullptr;
        if (!renderer.init(kViewW, kViewH, true, &window)) {
            std::cerr << "fighter_probe: renderer init failed\n";
            return 1;
        }

        const std::vector<std::string> atlas_bases = {loc + "/dojo/dojo"};
        const std::vector<std::string> atlas_jsons = {loc + "/dojo/dojo.d31b1e71.json"};
        for (std::size_t i = 0; i < atlas_bases.size(); ++i) {
            const sf2::data::Texture tex = decode_atlas(atlas_bases[i]);
            std::cout << "atlas " << i << ": " << tex.w << "x" << tex.h << "\n";
            const GLuint gl_tex = renderer.texture_for("atlas_" + std::to_string(i), tex);
            if (gl_tex == 0) {
                throw std::runtime_error("texture upload failed for " + atlas_bases[i]);
            }
            const std::vector<std::uint8_t> json_bytes = read_file(atlas_jsons[i]);
            const sf2::data::atlas a =
                sf2::data::atlas_parse(json_bytes.data(), json_bytes.size());
            for (const auto& f : a.frames) {
                renderer.texture_alias(f.name, gl_tex);
            }
        }

        renderer.begin_frame(camera);
        for (const auto& layer : scene.layers()) {
            scene.render_layer(renderer, *layer, camera);
        }
        // Draw the fighter AFTER the background (in front).
        renderer.draw_triangles(screen_verts.data(), vertex_count,
                                fighter.color_r(), fighter.color_g(),
                                fighter.color_b());
        renderer.batch().flush();
        if (!sf2::render::gl_capture_png(renderer.window(), kOutPng)) {
            throw std::runtime_error("gl_capture_png failed");
        }
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
        if (!sf2::render::gl_read_pixels_rgba(renderer.window(), rgba, &w, &h)) {
            throw std::runtime_error("gl_read_pixels_rgba failed");
        }
        renderer.end_frame();

        // --- 7. Pixel stats + oracle comparison ---------------------------
        // Count fill-color pixels WITHIN the fighter bbox (the black
        // background elsewhere would dominate a whole-image count).
        std::size_t fighter_px = 0;
        const std::uint8_t fr = static_cast<std::uint8_t>(fighter_color >> 16);
        const std::uint8_t fg = static_cast<std::uint8_t>(fighter_color >> 8);
        const std::uint8_t fb = static_cast<std::uint8_t>(fighter_color);
        const int bx0 = std::max(0, static_cast<int>(cmin_x));
        const int bx1 = std::min(kViewW, static_cast<int>(cmax_x));
        const int by0 = std::max(0, static_cast<int>(cmin_y));
        const int by1 = std::min(kViewH, static_cast<int>(cmax_y));
        for (int y = by0; y < by1; ++y) {
            for (int x = bx0; x < bx1; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                if (i + 2 >= rgba.size()) {
                    continue;
                }
                if (rgba[i] == fr && rgba[i + 1] == fg && rgba[i + 2] == fb) {
                    ++fighter_px;
                }
            }
        }
        std::cout << "\nfighter_native.png: " << w << "x" << h
                  << "  fill-color px in fighter bbox (0x" << std::hex
                  << fighter_color << std::dec << "): " << fighter_px << "\n";

        int ow = 0, oh = 0;
        const std::vector<std::uint8_t> oracle = load_png_rgba(kOracle, ow, oh);
        std::cout << "oracle: " << ow << "x" << oh << " from " << kOracle << "\n";

        // Oracle region around the player spawn (screen position of the
        // player world point under the fight-start camera).
        const float px_screen = camera.world_to_screen_x(player_x, 1.0f);
        const float py_screen = camera.world_to_screen_y(player_y);
        std::cout << "player COM screen: (" << px_screen << ", " << py_screen << ")\n";

        // Sample the oracle around the player COM for dark silhouette pixels.
        // The oracle's fighter fill is the ~(47,37,27) family (the game's
        // black fill over the dark dojo bg lands near that after JPEG-ish
        // capture); count "fighter-family" pixels (dark, low-saturation)
        // within the oracle region.
        const int rx0 = static_cast<int>(px_screen) - 160;
        const int rx1 = static_cast<int>(px_screen) + 160;
        const int ry0 = static_cast<int>(py_screen) - 200;
        const int ry1 = static_cast<int>(py_screen) + 80;
        std::size_t dark_px = 0;
        std::size_t region_px = 0;
        for (int y = std::max(0, ry0); y < std::min(oh, ry1); ++y) {
            for (int x = std::max(0, rx0); x < std::min(ow, rx1); ++x) {
                const std::size_t j = (static_cast<std::size_t>(y) * ow + x) * 4;
                if (j + 2 >= oracle.size()) {
                    continue;
                }
                ++region_px;
                // Dark silhouette: all channels below 100, low max-min spread
                // (the fighter fill is a near-neutral dark brown/black).
                const int r = oracle[j], g = oracle[j + 1], b = oracle[j + 2];
                const int mx = std::max(r, std::max(g, b));
                const int mn = std::min(r, std::min(g, b));
                if (mx < 100 && (mx - mn) < 45) {
                    ++dark_px;
                }
            }
        }
        std::cout << "oracle player region (" << rx0 << "," << ry0 << ")-(" << rx1
                  << "," << ry1 << "): " << region_px << " px, " << dark_px
                  << " dark silhouette px\n";
        if (region_px > 0) {
            std::cout << "  silhouette presence: "
                      << (100.0 * static_cast<double>(dark_px) /
                          static_cast<double>(region_px))
                      << "%\n";
        }

        // --- 8. Comparison report -----------------------------------------
        const std::string report_path = kOutDir + "/fighter_report.txt";
        {
            std::ofstream rep(report_path);
            if (!rep) {
                throw std::runtime_error("cannot write " + report_path);
            }
            rep << "fighter_probe report: dojo + native fighter (Phase 3.1b)\n";
            rep << "----------------------------------------------------------\n";
            rep << "model parts: mdl_skeleton (bones=" << skel.bones.size()
                << " tris=" << skel.tris.size() << "), mdl_body (bones="
                << body.bones.size() << " tris=" << body.tris.size()
                << "), mdl_head (bones=" << head.bones.size()
                << " tris=" << head.tris.size() << ")\n";
            rep << "merged: bones=" << fighter_model.bones.size()
                << " triangles=" << fighter_model.resolved_tris.size()
                << " capsules=" << fighter_model.capsules.size() << "\n";
            rep << "weapon: mdl_fists absent from models.dat; the Fists item "
                   "has no Model attribute -> weapon mesh skipped "
                   "(skeleton carries Weapon-Node bones)\n";
            rep << "enemy: mdl_punching_bag = capsules only, "
                   "mdl_skeleton_punching_bag = bones only -> no enemy mesh\n";
            rep << "clip: " << clip.name << " frames=" << clip.frames.size()
                << " bones=" << clip.bone_count() << "\n";
            rep << "player world pos: (" << player_x << ", " << player_y
                << ")  frame=" << frame << "  facing=" << facing << "\n";
            rep << "fighter color: 0x" << std::hex << fighter_color << std::dec
                << " (location Root Color attr, JS ev.Gf/Ut.aXa -> model.Qs)\n";
            rep << "render: " << kOutPng << " " << w << "x" << h << "\n";
            rep << "  triangles drawn: " << fighter_model.resolved_tris.size()
                << "\n";
            rep << "  screen bbox: x[" << min_x << ".." << max_x << "] y["
                << min_y << ".." << max_y << "]\n";
            rep << "  clipped bbox: x[" << cmin_x << ".." << cmax_x << "] y["
                << cmin_y << ".." << cmax_y << "]\n";
            rep << "  fill-color px: " << fighter_px << "\n";
            rep << "oracle: " << kOracle << " " << ow << "x" << oh << "\n";
            rep << "  player COM screen: (" << px_screen << ", " << py_screen
                << ")\n";
            rep << "  region (" << rx0 << "," << ry0 << ")-(" << rx1 << ","
                << ry1 << "): " << region_px << " px, " << dark_px
                << " dark silhouette px (presence "
                << (region_px > 0
                        ? std::to_string(100.0 * static_cast<double>(dark_px) /
                                         static_cast<double>(region_px))
                        : "n/a")
                << "%)\n";
            rep << "honest assessment: the fighter renders at the dojo player "
                   "spawn with the documented camera; the oracle boot.png was "
                   "captured during the pre-fight camera intro (phase 1) which "
                   "frames the arena differently (fighters ~90px higher), so "
                   "the silhouette overlap is indicative, not pixel-exact.\n";
            std::cout << "report written to " << report_path << "\n";
        }

        renderer.shutdown();
        std::cout << "\nfighter_probe: done — " << kOutPng << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fighter_probe: error: " << e.what() << "\n";
        return 1;
    }
}

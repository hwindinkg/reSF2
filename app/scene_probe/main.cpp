// scene_probe — native location renderer vs the oracle screenshot.
//
// Renders the DOJO and ARENA location scenes (background layers only,
// fight-start camera framing) and compares the dojo render against the
// oracle capture reference/traces/boot.png.
//
// The camera + parallax model is the one extracted from the game JS and
// implemented in core/scene/renderer.hpp (Camera):
//   F9  = (arena_h/2 - floor)/2
//   Io  = arena_center_x - camera_center_x
//   x'  = (world_x + Io*factor - center_x)*zoom + view_w/2
//   y'  = (world_y + F9*(1-zoom) - center_y)*zoom + view_h/2
// At fight start the camera is locked to the arena center (Io = 0) and the
// zoom fits the arena width into the view: zoom = view_w / arena_w.
//
// Usage: scene_probe [res_root]
// Defaults to reference/www/res at the repo root.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "atlas.hpp"
#include "render/gl.hpp"
#include "scene/location_scene.hpp"
#include "scene/renderer.hpp"
#include "texture.hpp"

namespace {

constexpr const char* kDefaultRes = "reference/www/res";
constexpr int kViewW = 1280;
constexpr int kViewH = 720;

const std::string kOracle = "reference/traces/boot.png";
const std::string kOutDir = "reference/extracted/scene";

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

// Decodes "<base>.*<ext>" for ext in {.webp, .ktx, .dds} — the game's file
// names carry hash suffixes (dojo.b920e18e.webp), so candidates are resolved
// by directory scan.
sf2::data::Texture decode_atlas(const std::string& base) {
    const std::string dir = std::filesystem::path(base).parent_path().string();
    const std::string stem = std::filesystem::path(base).filename().string();
    const std::vector<std::string> tries = {".webp", ".ktx", ".dds"};
    std::vector<std::string> errors;
    for (const std::string& ext : tries) {
        std::string found;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(stem + ".", 0) == 0 && entry.path().extension().string() == ext) {
                found = entry.path().string();
                break;
            }
        }
        if (found.empty()) {
            errors.push_back(stem + ".*" + ext);
            continue;
        }
        sf2::data::Texture tex;
        if (sf2::data::decode_texture(found, tex)) {
            std::cout << "  decoded from: " << found << "\n";
            return tex;
        }
        errors.push_back(found);
    }
    std::string msg = "cannot decode atlas " + base + " (tried";
    for (const std::string& e : errors) {
        msg += " " + e;
    }
    msg += ")";
    throw std::runtime_error(msg);
}

// --- render one location ----------------------------------------------------

struct RenderResult {
    std::string png_path;
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgba;
    std::size_t unique_colors = 0;
    std::size_t nontransparent = 0;
};

RenderResult render_location(sf2::render::Renderer& renderer,
                             const sf2::scene::LocationScene& scene,
                             const std::string& out_path) {
    sf2::render::Camera camera;
    scene.default_camera(camera, static_cast<float>(kViewW), static_cast<float>(kViewH));

    renderer.begin_frame(camera);
    for (const auto& layer : scene.layers()) {
        scene.render_layer(renderer, *layer, camera);
    }
    // Flush the batched quads to GL, then capture the back buffer BEFORE
    // swapping (glReadPixels(GL_BACK) after glfwSwapBuffers reads undefined
    // memory — the buffers have been exchanged).
    renderer.batch().flush();
    RenderResult r;
    r.png_path = out_path;
    r.w = kViewW;
    r.h = kViewH;
    if (!sf2::render::gl_capture_png(renderer.window(), out_path)) {
        throw std::runtime_error("gl_capture_png failed for " + out_path);
    }
    if (!sf2::render::gl_read_pixels_rgba(renderer.window(), r.rgba, &r.w, &r.h)) {
        throw std::runtime_error("gl_read_pixels_rgba failed");
    }
    renderer.end_frame();
    // Pixel stats.
    std::vector<std::uint8_t> seen(256 * 256 * 256, 0);
    std::size_t nontransparent = 0;
    std::size_t unique = 0;
    for (std::size_t i = 0; i < r.rgba.size(); i += 4) {
        if (r.rgba[i + 3] > 0) {
            ++nontransparent;
        }
        const std::uint32_t key = (std::uint32_t(r.rgba[i]) << 16) |
                                  (std::uint32_t(r.rgba[i + 1]) << 8) | r.rgba[i + 2];
        if (seen[key] == 0) {
            seen[key] = 1;
            ++unique;
        }
    }
    r.unique_colors = unique;
    r.nontransparent = nontransparent;
    return r;
}

// --- PNG loading (sf2::data::decode_texture) --------------------------------

std::vector<std::uint8_t> load_png_rgba(const std::string& path, int& w, int& h) {
    sf2::data::Texture tex;
    if (!sf2::data::decode_texture(path, tex)) {
        throw std::runtime_error("sf2::data::decode_texture failed for " + path);
    }
    w = tex.w;
    h = tex.h;
    return std::move(tex.rgba);
}

// --- structural comparison --------------------------------------------------

// 16 bins/channel RGB histogram overlap (0..100%).
double histogram_overlap(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    constexpr int kBins = 16;
    std::vector<std::uint64_t> ha(3 * kBins, 0), hb(3 * kBins, 0);
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i + 3 < n; i += 4) {
        const int ar = a[i] >> 4, ag = a[i + 1] >> 4, ab = a[i + 2] >> 4;
        const int br = b[i] >> 4, bg = b[i + 1] >> 4, bb = b[i + 2] >> 4;
        ha[0 * kBins + ar]++;
        ha[1 * kBins + ag]++;
        ha[2 * kBins + ab]++;
        hb[0 * kBins + br]++;
        hb[1 * kBins + bg]++;
        hb[2 * kBins + bb]++;
    }
    std::uint64_t total = 0, common = 0;
    for (int c = 0; c < 3; ++c) {
        for (int b = 0; b < kBins; ++b) {
            const std::uint64_t na = ha[static_cast<std::size_t>(c) * kBins + b];
            const std::uint64_t nb = hb[static_cast<std::size_t>(c) * kBins + b];
            total += std::min(na, nb);
            common += std::min(na, nb);
        }
        total += 0;  // per-channel share
    }
    // Overlap = (sum of min counts across all channels) / (sum of all counts).
    std::uint64_t all_a = 0, all_b = 0;
    for (std::size_t v : ha) all_a += v;
    for (std::size_t v : hb) all_b += v;
    const std::uint64_t denom = std::max<std::uint64_t>(all_a, all_b);
    if (denom == 0) {
        return 0.0;
    }
    return 100.0 * static_cast<double>(common) / static_cast<double>(denom);
}

// Whether a color region sampled from `img` (a rectangle in the atlas frame's
// on-screen placement) is "plausibly present" in `oracle`. Samples the
// screen rect the frame was drawn to and checks the oracle has similar
// colors there (binned match).
bool region_present_in_oracle(const std::vector<std::uint8_t>& img, int img_w, int img_h,
                              const std::vector<std::uint8_t>& oracle, int oracle_w, int oracle_h,
                              float x, float y, float w, float h) {
    const int x0 = std::max(0, static_cast<int>(x));
    const int y0 = std::max(0, static_cast<int>(y));
    const int x1 = std::min(img_w - 1, static_cast<int>(x + w));
    const int y1 = std::min(img_h - 1, static_cast<int>(y + h));
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    // Sample a few points in the region from both images.
    int matches = 0, total = 0;
    for (int sy = y0; sy <= y1; sy += std::max(1, (y1 - y0) / 4)) {
        for (int sx = x0; sx <= x1; sx += std::max(1, (x1 - x0) / 4)) {
            const std::size_t i = (static_cast<std::size_t>(sy) * img_w + sx) * 4;
            const std::size_t j = (static_cast<std::size_t>(sy) * oracle_w + sx) * 4;
            if (j + 3 >= oracle.size()) {
                continue;
            }
            ++total;
            // Binned color match (4-bit per channel tolerance).
            if ((img[i] >> 4) == (oracle[j] >> 4) && (img[i + 1] >> 4) == (oracle[j + 1] >> 4) &&
                (img[i + 2] >> 4) == (oracle[j + 2] >> 4)) {
                ++matches;
            }
        }
    }
    return total > 0 && static_cast<double>(matches) / total >= 0.25;
}

// --- main probe -------------------------------------------------------------

void probe_location(const std::string& name, const std::string& params_xml,
                    const std::vector<std::string>& atlas_jsons,
                    const std::vector<std::string>& atlas_bases, const std::string& res_root,
                    sf2::render::Renderer& renderer, std::vector<RenderResult>& out) {
    std::cout << "=== " << name << " ===\n";

    sf2::scene::LocationScene scene;
    scene.load(params_xml, atlas_jsons, res_root);

    // Print scene info.
    sf2::render::Camera camera;
    scene.default_camera(camera, static_cast<float>(kViewW), static_cast<float>(kViewH));
    std::cout << "  arena: " << scene.arena_width() << "x" << scene.arena_height()
              << " floor=" << scene.arena_floor() << "\n";
    std::cout << "  camera: center=(" << camera.center_x << "," << camera.center_y
              << ") zoom=" << camera.zoom << "  view=" << camera.view_w << "x" << camera.view_h
              << "\n";
    std::cout << "  layers: " << scene.layers().size() << "\n";
    for (std::size_t i = 0; i < scene.layers().size(); ++i) {
        const auto& layer = scene.layers()[i];
        std::cout << "    [" << i << "] factor=" << layer->factor
                  << " type=" << layer->type << " sprites=" << layer->sprites.size() << "\n";
    }

    // Upload the atlas textures and alias every ClassName to its GL texture.
    for (std::size_t i = 0; i < atlas_bases.size(); ++i) {
        const std::string key = "atlas_" + std::to_string(i);
        const sf2::data::Texture tex = decode_atlas(atlas_bases[i]);
        std::cout << "  atlas " << i << ": " << tex.w << "x" << tex.h << "\n";
        const GLuint gl_tex = renderer.texture_for(key, tex);
        if (gl_tex == 0) {
            throw std::runtime_error("texture upload failed for " + atlas_bases[i]);
        }
        // Alias every frame name (ClassName) in this atlas to its GL texture.
        std::vector<std::uint8_t> json_bytes = read_file(atlas_jsons[i]);
        const sf2::data::atlas a = sf2::data::atlas_parse(json_bytes.data(), json_bytes.size());
        for (const auto& f : a.frames) {
            renderer.texture_alias(f.name, gl_tex);
        }
        std::cout << "  aliased " << a.frames.size() << " ClassNames from " << atlas_jsons[i]
                  << "\n";
    }

    const std::string out_path = kOutDir + "/" + name + "_native.png";
    RenderResult r = render_location(renderer, scene, out_path);
    std::cout << "  saved " << out_path << "\n";
    std::cout << "  pixels: " << r.w << "x" << r.h << "  unique_colors=" << r.unique_colors
              << "  nontransparent=" << r.nontransparent << "\n";
    out.push_back(std::move(r));
}

void compare_with_oracle(const RenderResult& dojo, const std::string& report_path) {
    std::cout << "\n=== structural comparison: dojo_native vs oracle boot.png ===\n";

    int ow = 0, oh = 0;
    const std::vector<std::uint8_t> oracle = load_png_rgba(kOracle, ow, oh);
    std::cout << "  oracle: " << ow << "x" << oh << " from " << kOracle << "\n";
    if (ow != dojo.w || oh != dojo.h) {
        std::cout << "  WARNING: oracle " << ow << "x" << oh << " != native " << dojo.w << "x"
                  << dojo.h << " — comparison is size-mismatched\n";
    }

    std::ofstream rep(report_path);
    if (!rep) {
        throw std::runtime_error("cannot write " + report_path);
    }
    rep << "scene_probe structural comparison: dojo_native vs oracle boot.png\n";
    rep << "oracle: " << ow << "x" << oh << "  native: " << dojo.w << "x" << dojo.h << "\n\n";

    // (a) histogram overlap.
    const double overlap = histogram_overlap(dojo.rgba, oracle);
    std::cout << "  (a) histogram overlap (16 bins/channel RGB): " << overlap << "%\n";
    rep << "(a) histogram overlap (16 bins/channel RGB): " << overlap << "%\n\n";

    // (b) ClassName presence: every dojo atlas frame was it drawn (it's in a
    // layer) and is its screen region plausibly present in the oracle.
    // Reconstruct the dojo scene for frame rects.
    const std::string res_root = kDefaultRes;
    sf2::scene::LocationScene scene;
    scene.load(res_root + "/locations/dojo/dojo_params.b78df4b4.xml",
               {res_root + "/locations/dojo/dojo.d31b1e71.json"}, res_root);
    sf2::render::Camera cam;
    scene.default_camera(cam, static_cast<float>(kViewW), static_cast<float>(kViewH));

    // Collect the set of ClassNames actually drawn, with their on-screen rect.
    std::map<std::string, std::string> drawn;  // ClassName -> "x,y,w,h"
    for (const auto& layer : scene.layers()) {
        for (const auto& sp : layer->sprites) {
            if (sp->solid) {
                continue;
            }
            const float half_w = sp->transform.scale_x * sp->frame_w / 2.0f;
            const float half_h = sp->transform.scale_y * sp->frame_h / 2.0f;
            const float sx = cam.world_to_screen_x(sp->transform.x, layer->factor);
            const float sy = cam.world_to_screen_y(sp->transform.y);
            drawn[sp->texture_name] = std::to_string(sx - half_w) + "," + std::to_string(sy - half_h) +
                                      "," + std::to_string(2 * half_w) + "," +
                                      std::to_string(2 * half_h);
        }
    }

    std::cout << "  (b) dojo atlas frames (" << 23 << " expected):\n";
    rep << "(b) dojo atlas ClassName presence\n";
    rep << "    ClassName                 drawn  in_oracle\n";
    int drawn_count = 0, in_oracle_count = 0;
    // Expected dojo frame names (from dojo.d31b1e71.json).
    const std::vector<std::string> expected = {
        "_0000_arena_floor", "_0001_go_table",       "_0002_hermit_swords",
        "_0003_shogun_daishou", "_0004_wasp_naginata", "_0005_widow_fans",
        "_0006_butcher_cleavers", "_0007_lynx_claws", "_0008_lamp_right",
        "_0009_lamp_left",     "_0010_Wall",          "_0011_tree_and_light",
        "_0012_bridge",        "_0013_temple",        "_0014_mountains",
        "_0015_bg",            "dojo_floor_1",        "dojo_floor_2",
        "dojo_punch_bag_holder", "layer_4",           "left_wall",
        "pixel_1",             "right_wall",
    };
    for (const auto& cls : expected) {
        const auto it = drawn.find(cls);
        const bool is_drawn = it != drawn.end();
        bool in_oracle = false;
        if (is_drawn && it->second != "pixel_1") {
            // Parse the rect and probe the oracle.
            float x = 0, y = 0, w = 0, h = 0;
            std::sscanf(it->second.c_str(), "%f,%f,%f,%f", &x, &y, &w, &h);
            in_oracle = region_present_in_oracle(dojo.rgba, dojo.w, dojo.h, oracle, ow, oh, x, y, w,
                                                 h);
        }
        if (is_drawn) ++drawn_count;
        if (in_oracle) ++in_oracle_count;
        char line[256];
        std::snprintf(line, sizeof(line), "    %-22s %s  %s\n", cls.c_str(),
                      is_drawn ? "yes" : "no", in_oracle ? "yes" : "no");
        std::cout << line;
        rep << line;
    }
    std::cout << "    drawn=" << drawn_count << "/23  in_oracle=" << in_oracle_count << "/23\n";
    rep << "    drawn=" << drawn_count << "/23  in_oracle=" << in_oracle_count << "/23\n\n";

    // (c) layer order sanity.
    std::cout << "  (c) layer order (back -> front):\n";
    rep << "(c) layer order sanity (back -> front)\n";
    for (std::size_t i = 0; i < scene.layers().size(); ++i) {
        const auto& layer = scene.layers()[i];
        std::string desc = layer->type == 2 ? "ModelsViewer(fighters, skipped)"
                                            : "factor=" + std::to_string(layer->factor) + " sprites=" +
                                                  std::to_string(layer->sprites.size());
        std::string line = "    [" + std::to_string(i) + "] " + desc + "\n";
        std::cout << line;
        rep << line;
    }
    rep << "\n";

    std::cout << "  report written to " << report_path << "\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::string res_root = argc > 1 ? argv[1] : kDefaultRes;
    try {
        ensure_dir(kOutDir);

        sf2::render::Renderer renderer;
        GLFWwindow* window = nullptr;
        if (!renderer.init(kViewW, kViewH, true, &window)) {
            std::cerr << "scene_probe: renderer init failed\n";
            return 1;
        }

        const std::string loc = res_root + "/locations";
        std::vector<RenderResult> results;

        // DOJO: one atlas.
        probe_location("dojo", loc + "/dojo/dojo_params.b78df4b4.xml",
                       {loc + "/dojo/dojo.d31b1e71.json"}, {loc + "/dojo/dojo"}, res_root, renderer,
                       results);

        // ARENA: two atlases (arena + arena-2).
        probe_location("arena", loc + "/arena/arena_params.16ca56d9.xml",
                       {loc + "/arena/arena.ca2949ef.json", loc + "/arena/arena-2.586e4f15.json"},
                       {loc + "/arena/arena", loc + "/arena/arena-2"}, res_root, renderer, results);

        // Compare dojo against the oracle.
        const RenderResult& dojo = results[0];
        compare_with_oracle(dojo, kOutDir + "/compare_report.txt");

        renderer.shutdown();
        std::cout << "\nscene_probe: done\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "scene_probe: error: " << e.what() << "\n";
        return 1;
    }
}

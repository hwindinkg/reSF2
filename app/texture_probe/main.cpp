// texture_probe — decode the game's textures + atlas + font metadata.
//
// 1. res/locations/arena/: decode the atlas texture (try .webp, then .ktx,
//    then .dds) -> dimensions + pixel stats; save RGBA as PNG to
//    reference/extracted/textures/arena_atlas.png.
// 2. res/fight/: decode ui.png + parse ui.json -> frame count + first 10.
// 3. res/ui/: parse font-en.fnt -> char count + first 10 chars.
// 4. res/locations/dojo/: decode the dojo atlas texture -> report + save PNG.
//
// Usage: texture_probe [res_root]
// Defaults to reference/www/res at the repo root.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "atlas.hpp"
#include "font.hpp"
#include "texture.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

namespace {

constexpr const char* kDefaultRes = "reference/www/res";

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

struct pixel_stats {
    double avg_r = 0, avg_g = 0, avg_b = 0, avg_a = 0;
    std::size_t unique_colors = 0;
    bool has_alpha = false;        // any pixel with a < 255
    std::size_t transparent = 0;   // count of a == 0
    std::size_t nontrivial = 0;    // pixels that are not pure black or pure white
};

pixel_stats compute_stats(const sf2::data::Texture& tex) {
    pixel_stats s;
    const std::size_t n = static_cast<std::size_t>(tex.w) * tex.h;
    std::vector<std::uint8_t> seen(256 * 256 * 256, 0);
    std::uint64_t rsum = 0, gsum = 0, bsum = 0, asum = 0;
    bool any_alpha = false;
    std::size_t transparent = 0;
    std::size_t nontrivial = 0;
    std::size_t unique = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* p = &tex.rgba[i * 4];
        rsum += p[0];
        gsum += p[1];
        bsum += p[2];
        asum += p[3];
        if (p[3] < 255) {
            any_alpha = true;
        }
        if (p[3] == 0) {
            ++transparent;
        }
        if (!(p[0] == 0 && p[1] == 0 && p[2] == 0) &&
            !(p[0] == 255 && p[1] == 255 && p[2] == 255)) {
            ++nontrivial;
        }
        const std::uint32_t key = (std::uint32_t(p[0]) << 16) | (std::uint32_t(p[1]) << 8) | p[2];
        if (seen[key] == 0) {
            seen[key] = 1;
            ++unique;
        }
    }
    s.avg_r = static_cast<double>(rsum) / n;
    s.avg_g = static_cast<double>(gsum) / n;
    s.avg_b = static_cast<double>(bsum) / n;
    s.avg_a = static_cast<double>(asum) / n;
    s.unique_colors = unique;
    s.has_alpha = any_alpha;
    s.transparent = transparent;
    s.nontrivial = nontrivial;
    return s;
}

void print_stats(const std::string& label, const sf2::data::Texture& tex) {
    const pixel_stats s = compute_stats(tex);
    std::cout << "  " << label << ": " << tex.w << "x" << tex.h << "\n";
    std::cout << "    avg RGBA: (" << s.avg_r << ", " << s.avg_g << ", " << s.avg_b << ", "
              << s.avg_a << ")\n";
    std::cout << "    unique colors: " << s.unique_colors
              << "  alpha present: " << (s.has_alpha ? "yes" : "no")
              << "  transparent px: " << s.transparent
              << "  non-black/white px: " << s.nontrivial << "\n";
}

void save_png(const std::string& path, const sf2::data::Texture& tex) {
    ensure_dir(std::filesystem::path(path).parent_path().string());
    const int ok = stbi_write_png(path.c_str(), tex.w, tex.h, 4, tex.rgba.data(),
                                  tex.w * 4);
    if (!ok) {
        throw std::runtime_error("stbi_write_png failed for " + path);
    }
    std::cout << "  saved " << path << "\n";
}

// Tries .webp, then .ktx, then .dds for a given base path (no extension).
// The game's file names carry hash suffixes (e.g. arena.7995a5ab.webp), so
// each candidate is resolved by directory scan for "<base>.*<ext>".
sf2::data::Texture decode_atlas(const std::string& base) {
    const std::string dir = std::filesystem::path(base).parent_path().string();
    const std::string stem = std::filesystem::path(base).filename().string();
    const std::vector<std::string> tries = {".webp", ".ktx", ".dds"};
    std::vector<std::string> errors;
    for (const std::string& ext : tries) {
        // Find "<stem>.*<ext>" in the directory.
        std::string found;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            const std::string e = entry.path().extension().string();
            if (name.rfind(stem + ".", 0) == 0 && e == ext) {
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

// --- task 1: arena atlas -----------------------------------------------------

void probe_arena(const std::string& res_root, const std::string& out_dir) {
    const std::string arena_dir = res_root + "/locations/arena";
    std::cout << "=== 1. arena atlas ===\n";
    const std::string base = arena_dir + "/arena";
    sf2::data::Texture tex = decode_atlas(base);
    print_stats("arena atlas", tex);

    // Sanity: dims must match the atlas JSON meta.size.
    const std::vector<std::uint8_t> json = read_file(base + ".ca2949ef.json");
    const sf2::data::atlas a = sf2::data::atlas_parse(json.data(), json.size());
    std::cout << "  atlas json meta.size: " << a.w << "x" << a.h
              << "  frames: " << a.frames.size() << "\n";
    if (a.w != tex.w || a.h != tex.h) {
        throw std::runtime_error("SANITY FAIL: decoded dims " + std::to_string(tex.w) + "x" +
                                 std::to_string(tex.h) + " != atlas meta " + std::to_string(a.w) +
                                 "x" + std::to_string(a.h));
    }
    std::cout << "  sanity: dims match atlas meta.size\n";

    save_png(out_dir + "/arena_atlas.png", tex);
    std::cout << "\n";
}

// --- task 2: fight ui --------------------------------------------------------

void probe_fight_ui(const std::string& res_root) {
    const std::string fight_dir = res_root + "/fight";
    std::cout << "=== 2. fight ui ===\n";

    const sf2::data::Texture ui = [&] {
        sf2::data::Texture t;
        if (!sf2::data::decode_texture(fight_dir + "/ui.62bee150.png", t)) {
            throw std::runtime_error("cannot decode ui.62bee150.png");
        }
        return t;
    }();
    print_stats("ui.png", ui);

    const std::vector<std::uint8_t> json = read_file(fight_dir + "/ui.4c9e126b.json");
    const sf2::data::atlas a = sf2::data::atlas_parse(json.data(), json.size());
    std::cout << "  frame count: " << a.frames.size() << "\n";
    const int shown = a.frames.size() < 10 ? static_cast<int>(a.frames.size()) : 10;
    for (int i = 0; i < shown; ++i) {
        const sf2::data::atlas_frame& f = a.frames[static_cast<std::size_t>(i)];
        std::cout << "    " << i + 1 << ". " << f.name << "  rect=(" << f.x << "," << f.y << ","
                  << f.w << "x" << f.h << ")  rotated=" << (f.rotated ? "yes" : "no")
                  << "  trimmed=" << (f.trimmed ? "yes" : "no") << "\n";
    }
    // Sanity: ui.png dims must match the atlas meta.size.
    if (a.w != ui.w || a.h != ui.h) {
        throw std::runtime_error("SANITY FAIL: ui.png " + std::to_string(ui.w) + "x" +
                                 std::to_string(ui.h) + " != ui.json meta " + std::to_string(a.w) +
                                 "x" + std::to_string(a.h));
    }
    std::cout << "  sanity: ui.png dims match ui.json meta.size\n\n";
}

// --- task 3: font ------------------------------------------------------------

void probe_font(const std::string& res_root) {
    const std::string ui_dir = res_root + "/ui";
    std::cout << "=== 3. font-en ===\n";
    const std::vector<std::uint8_t> fnt = read_file(ui_dir + "/font-en.7043b83b.fnt");
    const sf2::data::font f = sf2::data::font_parse(fnt.data(), fnt.size());
    std::cout << "  lineHeight=" << f.line_height << "  base=" << f.base
              << "  page=\"" << f.page << "\"  scale=" << f.scale_w << "x" << f.scale_h << "\n";
    std::cout << "  char count: " << f.chars.size() << "\n";
    const int shown = f.chars.size() < 10 ? static_cast<int>(f.chars.size()) : 10;
    for (int i = 0; i < shown; ++i) {
        const sf2::data::font_char& c = f.chars[static_cast<std::size_t>(i)];
        const std::string printable = c.id >= 32 && c.id < 127
                                          ? std::string(1, static_cast<char>(c.id))
                                          : "?";
        std::cout << "    id=" << c.id << " ('" << printable << "')  rect=(" << c.x << "," << c.y
                  << "," << c.w << "x" << c.h << ")  xoff=" << c.xoffset
                  << "  yoff=" << c.yoffset << "  xadv=" << c.xadvance << "\n";
    }
    std::cout << "\n";
}

// --- task 4: dojo atlas ------------------------------------------------------

void probe_dojo(const std::string& res_root, const std::string& out_dir) {
    const std::string dojo_dir = res_root + "/locations/dojo";
    std::cout << "=== 4. dojo atlas ===\n";
    const std::string base = dojo_dir + "/dojo";
    sf2::data::Texture tex = decode_atlas(base);
    print_stats("dojo atlas", tex);

    const std::vector<std::uint8_t> json = read_file(base + ".d31b1e71.json");
    const sf2::data::atlas a = sf2::data::atlas_parse(json.data(), json.size());
    std::cout << "  atlas json meta.size: " << a.w << "x" << a.h
              << "  frames: " << a.frames.size() << "\n";
    if (a.w != tex.w || a.h != tex.h) {
        throw std::runtime_error("SANITY FAIL: decoded dims " + std::to_string(tex.w) + "x" +
                                 std::to_string(tex.h) + " != atlas meta " + std::to_string(a.w) +
                                 "x" + std::to_string(a.h));
    }
    std::cout << "  sanity: dims match atlas meta.size\n";

    save_png(out_dir + "/dojo_atlas.png", tex);
    std::cout << "\n";
}

// --- task 5: controller atlas (ui/controller) --------------------------------
// Decodes the ui/controller atlas (ktx ASTC / dds BCn) and saves it as PNG
// so the pad art (JoystickContainer_norm etc.) can be inspected offline.
void probe_controller(const std::string& res_root, const std::string& out_dir) {
    const std::string ctrl_dir = res_root + "/ui";
    std::cout << "=== 5. controller atlas ===\n";
    const std::string base = ctrl_dir + "/controller";
    sf2::data::Texture tex = decode_atlas(base);
    print_stats("controller atlas", tex);

    // Locate the matching json (controller.<hash>.json) for the sanity check.
    std::string json_path;
    for (const auto& entry : std::filesystem::directory_iterator(ctrl_dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("controller.", 0) == 0 && entry.path().extension().string() == ".json") {
            json_path = entry.path().string();
            break;
        }
    }
    if (!json_path.empty()) {
        const std::vector<std::uint8_t> json = read_file(json_path);
        const sf2::data::atlas a = sf2::data::atlas_parse(json.data(), json.size());
        std::cout << "  atlas json meta.size: " << a.w << "x" << a.h
                  << "  frames: " << a.frames.size() << "\n";
        if (a.w != tex.w || a.h != tex.h) {
            throw std::runtime_error("SANITY FAIL: decoded dims " + std::to_string(tex.w) + "x" +
                                     std::to_string(tex.h) + " != atlas meta " +
                                     std::to_string(a.w) + "x" + std::to_string(a.h));
        }
        std::cout << "  sanity: dims match atlas meta.size\n";
    }
    save_png(out_dir + "/controller_atlas.png", tex);
    std::cout << "\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::string res_root = argc > 1 ? argv[1] : kDefaultRes;
    const std::string out_dir = "reference/extracted/textures";

    try {
        probe_arena(res_root, out_dir);
        probe_fight_ui(res_root);
        probe_font(res_root);
        probe_dojo(res_root, out_dir);
        probe_controller(res_root, out_dir);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "texture_probe: error: " << e.what() << "\n";
        return 1;
    }
}

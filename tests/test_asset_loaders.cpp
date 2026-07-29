// tests/test_asset_loaders.cpp
//
// Unit tests for the Stage 5 asset loaders:
//   - plist_atlas (Cocos2d-x TexturePacker v2)
//   - atf_tactics (zlib-compressed tactics blob)
//   - bitmap_font (AngelCode BMFont)
//
// Build:
//   g++ -std=c++23 -Wall -Wextra -Iengine -I/usr/include \
//       tests/test_asset_loaders.cpp \
//       engine/reverse/plist_atlas.cpp \
//       engine/reverse/atf_tactics.cpp \
//       engine/reverse/bitmap_font.cpp \
//       -lz -o build/test_asset_loaders
//   ./build/test_asset_loaders

#include "../engine/reverse/plist_atlas.hpp"
#include "../engine/reverse/atf_tactics.hpp"
#include "../engine/reverse/bitmap_font.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace resf2::reverse;

static int g_failures = 0;
static int g_tests    = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_tests;                                                      \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK(%s)\n",             \
                         __FILE__, __LINE__, #cond);                    \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        ++g_tests;                                                      \
        if (!((a) == (b))) {                                            \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK_EQ(%s, %s)\n",      \
                         __FILE__, __LINE__, #a, #b);                   \
        }                                                               \
    } while (0)

// ---------- plist_atlas tests ----------

static const char* kSamplePlist = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
    <dict>
        <key>frames</key>
        <dict>
            <key>background_1.png</key>
            <dict>
                <key>frame</key>
                <string>{{0,512},{512,512}}</string>
                <key>offset</key>
                <string>{0,0}</string>
                <key>rotated</key>
                <false/>
                <key>sourceColorRect</key>
                <string>{{0,0},{512,512}}</string>
                <key>sourceSize</key>
                <string>{512,512}</string>
            </dict>
            <key>background_2.png</key>
            <dict>
                <key>frame</key>
                <string>{{0,0},{256,128}}</string>
                <key>offset</key>
                <string>{5,-3}</string>
                <key>rotated</key>
                <true/>
                <key>sourceColorRect</key>
                <string>{{10,20},{256,128}}</string>
                <key>sourceSize</key>
                <string>{300,150}</string>
            </dict>
        </dict>
        <key>metadata</key>
        <dict>
            <key>format</key>
            <integer>2</integer>
            <key>realTextureFileName</key>
            <string>bg.png</string>
            <key>size</key>
            <string>{512,1024}</string>
            <key>smartupdate</key>
            <string>$TexturePacker:SmartUpdate:abc:def:ghi$</string>
            <key>textureFileName</key>
            <string>bg.png</string>
        </dict>
    </dict>
</plist>
)";

static void test_plist_synthetic() {
    auto r = plist::parse(kSamplePlist);
    CHECK(r.has_value());
    if (!r) return;
    auto& a = *r;

    CHECK_EQ(a.frames.size(), 2u);
    CHECK_EQ(a.metadata.format, 2);
    CHECK_EQ(a.metadata.real_texture_filename, "bg.png");
    CHECK_EQ(a.metadata.texture_filename, "bg.png");
    CHECK_EQ(a.metadata.texture_w, 512);
    CHECK_EQ(a.metadata.texture_h, 1024);

    // Frame 0: background_1.png
    CHECK(a.name_index.count("background_1.png") > 0);
    auto& f0 = a.frames[a.name_index["background_1.png"]];
    CHECK_EQ(f0.name, "background_1.png");
    CHECK_EQ(f0.atlas_x, 0);
    CHECK_EQ(f0.atlas_y, 512);
    CHECK_EQ(f0.atlas_w, 512);
    CHECK_EQ(f0.atlas_h, 512);
    CHECK_EQ(f0.offset_x, 0);
    CHECK_EQ(f0.offset_y, 0);
    CHECK_EQ(f0.rotated, false);
    CHECK_EQ(f0.source_x, 0);
    CHECK_EQ(f0.source_y, 0);
    CHECK_EQ(f0.source_w, 512);
    CHECK_EQ(f0.source_h, 512);
    CHECK_EQ(f0.source_size_w, 512);
    CHECK_EQ(f0.source_size_h, 512);

    // Frame 1: background_2.png (rotated, with offset)
    CHECK(a.name_index.count("background_2.png") > 0);
    auto& f1 = a.frames[a.name_index["background_2.png"]];
    CHECK_EQ(f1.name, "background_2.png");
    CHECK_EQ(f1.atlas_x, 0);
    CHECK_EQ(f1.atlas_y, 0);
    CHECK_EQ(f1.atlas_w, 256);
    CHECK_EQ(f1.atlas_h, 128);
    CHECK_EQ(f1.offset_x, 5);
    CHECK_EQ(f1.offset_y, -3);
    CHECK_EQ(f1.rotated, true);
    CHECK_EQ(f1.source_x, 10);
    CHECK_EQ(f1.source_y, 20);
    CHECK_EQ(f1.source_w, 256);
    CHECK_EQ(f1.source_h, 128);
    CHECK_EQ(f1.source_size_w, 300);
    CHECK_EQ(f1.source_size_h, 150);
}

static void test_plist_empty_rejected() {
    auto r = plist::parse("");
    CHECK(!r.has_value());
    CHECK_EQ(r.error(), plist::ParseError::kInputEmpty);
}

static void test_plist_real_file() {
    // Try to find a real .plist file in the extracted APK
    fs::path candidates[] = {
        "assets/assets/1536/locations/new_year_dojo/bg.plist",
        "assets/assets/1536/locations/dojo/bg.plist",
        "assets/assets/1536/textures/buttons/back.plist",
    };
    for (const auto& path : candidates) {
        if (!fs::exists(path)) continue;
        auto r = plist::parse_file(path.string());
        CHECK(r.has_value());
        if (!r) continue;
        auto& [buf, a] = *r;
        CHECK(a.frames.size() > 0);
        CHECK_EQ(a.metadata.format, 2);
        std::printf("  [plist] %s: %zu frames, atlas %dx%d\n",
                    path.string().c_str(), a.frames.size(),
                    a.metadata.texture_w, a.metadata.texture_h);
        return;
    }
    std::printf("SKIP test_plist_real_file (no .plist fixtures found)\n");
}

// ---------- atf_tactics tests ----------

static void test_atf_synthetic_too_small() {
    std::vector<std::byte> empty;
    auto r = atf::parse(empty);
    CHECK(!r.has_value());
    CHECK_EQ(r.error(), atf::ParseError::kInputEmpty);
}

static void test_atf_real_file() {
    // .atf files live in assets/tactics/.  Two naming patterns:
    //   single-weapon:  fists.atf, axes.atf  (version 2)
    //   weapon-pair:    axes_katars.atf       (version 1)
    fs::path candidates[] = {
        "assets/tactics/fists.atf",
        "assets/tactics/axes.atf",
        "assets/tactics/knobsticks.atf",
        "assets/tactics/axes_katars.atf",
        "assets/tactics/knobsticks_machete.atf",
    };
    bool any_found = false;
    for (const auto& path : candidates) {
        if (!fs::exists(path)) continue;
        any_found = true;
        auto r = atf::parse_file(path.string());
        CHECK(r.has_value());
        if (!r) continue;
        auto& t = *r;
        // Accept both version 1 (pair) and version 2 (single weapon).
        CHECK(t.header.version == 1 || t.header.version == 2);
        CHECK(!t.header.weapon_a_name.empty());
        // v=1 pairs have a weapon_b; v=2 singles leave it empty.
        if (t.header.version == 1) {
            // weapon_b may be empty for same-weapon pairs (e.g. fist_fist).
        }
        CHECK_EQ(t.binary_prefix.stride, static_cast<std::uint16_t>(858));
        // The 858-byte record should have non-zero indices.
        bool any_nonzero = false;
        for (auto idx : t.animation_indices) {
            if (idx != 0) { any_nonzero = true; break; }
        }
        CHECK(any_nonzero);
        // String pool should be non-empty and yield parsed names.
        CHECK(!t.string_pool.empty());
        CHECK(t.animation_names.size() > 0);
        // Every index in the record should be within the parsed names
        // range (or we tolerate out-of-range as unreversed data).
        std::printf("  [atf] %s: v=%u A='%s' B='%s' stride=%u "
                    "pool=%zu names=%zu unique_indices=%zu\n",
                    path.string().c_str(), t.header.version,
                    t.header.weapon_a_name.c_str(),
                    t.header.weapon_b_name.c_str(),
                    t.binary_prefix.stride,
                    t.string_pool.size(),
                    t.animation_names.size(),
                    [&]{
                        std::set<uint8_t> u;
                        for (auto v : t.animation_indices) u.insert(v);
                        return u.size();
                    }());
    }
    if (!any_found) {
        std::printf("SKIP test_atf_real_file (no .atf fixtures found)\n");
    }
}

// ---------- bitmap_font tests ----------

static const char* kSampleFont = R"(info face="Carter One" size=220 bold=0 italic=0 charset="" unicode=0 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=0,0
common lineHeight=339 base=244 scaleW=640 scaleH=640 pages=1 packed=0
page id=0 file="CarterOne_numbers_220.png"
chars count=3
char id=32 x=158 y=390 width=0 height=0 xoffset=0 yoffset=244 xadvance=48 page=0 chnl=0
char id=47 x=326 y=386 width=120 height=190 xoffset=7 yoffset=79 xadvance=94 page=0 chnl=0
char id=48 x=2 y=2 width=182 height=190 xoffset=15 yoffset=81 xadvance=175 page=0 chnl=0
kernings count=1
kerning first=47 second=48 amount=-2
)";

static void test_font_synthetic() {
    auto r = font::parse(kSampleFont);
    CHECK(r.has_value());
    if (!r) return;
    auto& f = *r;

    CHECK_EQ(f.info.face, "Carter One");
    CHECK_EQ(f.info.size, 220);
    CHECK_EQ(f.info.bold, false);
    CHECK_EQ(f.info.italic, false);
    CHECK_EQ(f.info.unicode, false);
    CHECK_EQ(f.info.stretch_h, 100);
    CHECK_EQ(f.info.smooth, true);
    CHECK_EQ(f.info.aa, 1);

    CHECK_EQ(f.common.line_height, 339);
    CHECK_EQ(f.common.base, 244);
    CHECK_EQ(f.common.scale_w, 640);
    CHECK_EQ(f.common.scale_h, 640);
    CHECK_EQ(f.common.pages, 1);
    CHECK_EQ(f.common.packed, false);

    CHECK_EQ(f.pages.size(), 1u);
    CHECK_EQ(f.pages[0].id, 0);
    CHECK_EQ(f.pages[0].file, "CarterOne_numbers_220.png");

    CHECK_EQ(f.chars.size(), 3u);
    CHECK(f.char_index.count(32) > 0);
    CHECK(f.char_index.count(47) > 0);
    CHECK(f.char_index.count(48) > 0);

    auto& c32 = f.chars[f.char_index[32]];
    CHECK_EQ(c32.id, 32);
    CHECK_EQ(c32.x, 158);
    CHECK_EQ(c32.y, 390);
    CHECK_EQ(c32.width, 0);
    CHECK_EQ(c32.height, 0);
    CHECK_EQ(c32.xadvance, 48);

    auto& c48 = f.chars[f.char_index[48]];
    CHECK_EQ(c48.id, 48);
    CHECK_EQ(c48.x, 2);
    CHECK_EQ(c48.y, 2);
    CHECK_EQ(c48.width, 182);
    CHECK_EQ(c48.height, 190);
    CHECK_EQ(c48.xoffset, 15);
    CHECK_EQ(c48.yoffset, 81);
    CHECK_EQ(c48.xadvance, 175);

    CHECK_EQ(f.kernings.size(), 1u);
    CHECK_EQ(font::kerning_amount(f, 47, 48), -2);
    CHECK_EQ(font::kerning_amount(f, 48, 47), 0);  // no kerning
}

static void test_font_empty_rejected() {
    auto r = font::parse("");
    CHECK(!r.has_value());
    CHECK_EQ(r.error(), font::ParseError::kInputEmpty);
}

static void test_font_real_file() {
    fs::path candidates[] = {
        "assets/assets/1536/fonts/CarterOne_numbers_220.fnt",
        "assets/assets/1536/fonts/CarterOne_num_240.fnt",
        "assets/assets/1536/fonts/obelix.fnt",
    };
    for (const auto& path : candidates) {
        if (!fs::exists(path)) continue;
        auto r = font::parse_file(path.string());
        CHECK(r.has_value());
        if (!r) continue;
        auto& [buf, f] = *r;
        CHECK(!f.info.face.empty());
        CHECK(f.common.scale_w > 0);
        CHECK(f.chars.size() > 0);
        std::printf("  [font] %s: face='%s' %d chars, scale %dx%d\n",
                    path.string().c_str(), f.info.face.c_str(),
                    (int)f.chars.size(), f.common.scale_w, f.common.scale_h);
        return;
    }
    std::printf("SKIP test_font_real_file (no .fnt fixtures found)\n");
}

int main() {
    test_plist_synthetic();
    test_plist_empty_rejected();
    test_plist_real_file();

    test_atf_synthetic_too_small();
    test_atf_real_file();

    test_font_synthetic();
    test_font_empty_rejected();
    test_font_real_file();

    std::printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}

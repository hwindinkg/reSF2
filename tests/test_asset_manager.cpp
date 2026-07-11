// tests/test_asset_manager.cpp
//
// Unit tests for the Stage 6 AssetManager.

#include "../engine/runtime/asset_manager.hpp"
#include "../engine/reverse/plist_atlas.hpp"
#include "../engine/reverse/atf_tactics.hpp"
#include "../engine/reverse/bitmap_font.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

using namespace resf2::runtime::assets;
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

// ---------- Test fixtures ----------

namespace {

// Write a minimal .plist to a temp dir
fs::path write_test_plist(const fs::path& dir) {
    fs::path path = dir / "test.plist";
    const char* content = R"(<?xml version="1.0"?>
<plist version="1.0"><dict>
  <key>frames</key><dict>
    <key>spr1.png</key><dict>
      <key>frame</key><string>{{0,0},{32,32}}</string>
      <key>offset</key><string>{0,0}</string>
      <key>rotated</key><false/>
      <key>sourceColorRect</key><string>{{0,0},{32,32}}</string>
      <key>sourceSize</key><string>{32,32}</string>
    </dict>
  </dict>
  <key>metadata</key><dict>
    <key>format</key><integer>2</integer>
    <key>realTextureFileName</key><string>test.png</string>
    <key>size</key><string>{64,64}</string>
    <key>textureFileName</key><string>test.png</string>
  </dict>
</dict></plist>)";
    std::ofstream f(path);
    f << content;
    return path;
}

// Write a minimal .fnt to a temp dir
fs::path write_test_font(const fs::path& dir) {
    fs::path path = dir / "test.fnt";
    const char* content = R"(info face="Test" size=32 bold=0 italic=0 charset="" unicode=1 stretchH=100 smooth=0 aa=1 padding=0,0,0,0 spacing=1,1
common lineHeight=40 base=30 scaleW=128 scaleH=128 pages=1 packed=0
page id=0 file="test.png"
chars count=2
char id=65 x=0 y=0 width=20 height=30 xoffset=0 yoffset=0 xadvance=22 page=0 chnl=15
char id=66 x=22 y=0 width=20 height=30 xoffset=0 yoffset=0 xadvance=22 page=0 chnl=15
kernings count=0)";
    std::ofstream f(path);
    f << content;
    return path;
}

// Write some raw bytes
fs::path write_test_raw(const fs::path& dir, const std::string& name, const std::vector<std::byte>& data) {
    fs::path path = dir / name;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    return path;
}

}  // namespace

// ---------- Tests ----------

static void test_init_shutdown() {
    AssetManager am;
    Config cfg;
    cfg.search_roots = {fs::temp_directory_path()};
    cfg.worker_threads = 0;  // synchronous
    CHECK(am.init(cfg));
    am.shutdown();
}

static void test_load_not_found() {
    AssetManager am;
    Config cfg;
    cfg.search_roots = {fs::temp_directory_path()};
    cfg.worker_threads = 0;
    am.init(cfg);
    auto r = am.load("nonexistent_file_xyz.plist");
    CHECK(!r.has_value());
    CHECK_EQ(r.error(), LoadError::kNotFound);
    am.shutdown();
}

static void test_load_no_loader() {
    auto tmp = fs::temp_directory_path() / "resf2_test_am";
    fs::create_directories(tmp);
    auto path = write_test_raw(tmp, "test.unknownext", {std::byte{0x42}});
    
    AssetManager am;
    Config cfg;
    cfg.search_roots = {tmp};
    cfg.worker_threads = 0;
    am.init(cfg);
    auto r = am.load("test.unknownext");
    CHECK(!r.has_value());
    CHECK_EQ(r.error(), LoadError::kNoLoaderRegistered);
    am.shutdown();
    fs::remove_all(tmp);
}

static void test_load_plist() {
    auto tmp = fs::temp_directory_path() / "resf2_test_am";
    fs::create_directories(tmp);
    auto path = write_test_plist(tmp);
    
    AssetManager am;
    Config cfg;
    cfg.search_roots = {tmp};
    cfg.worker_threads = 0;
    am.init(cfg);
    am.register_loader(std::make_unique<PlistAtlasLoader>());
    
    auto r = am.load("test.plist");
    CHECK(r.has_value());
    if (r) {
        auto& asset = *r;
        CHECK_EQ(asset->path, "test.plist");
        CHECK(asset->raw_bytes.size() > 0);
        CHECK(asset->parsed.has_value());
        CHECK(asset->load_time_ms < 1000);  // should be fast
        
        // Try to cast to ParsedAtlas
        auto* atlas_ptr = std::any_cast<std::shared_ptr<plist::ParsedAtlas>>(&asset->parsed);
        CHECK(atlas_ptr != nullptr);
        if (atlas_ptr) {
            auto& atlas = *atlas_ptr;
            CHECK_EQ(atlas->frames.size(), 1u);
            CHECK_EQ(atlas->metadata.format, 2);
            CHECK_EQ(atlas->metadata.texture_filename, "test.png");
        }
    }
    am.shutdown();
    fs::remove_all(tmp);
}

static void test_load_font() {
    auto tmp = fs::temp_directory_path() / "resf2_test_am";
    fs::create_directories(tmp);
    auto path = write_test_font(tmp);
    
    AssetManager am;
    Config cfg;
    cfg.search_roots = {tmp};
    cfg.worker_threads = 0;
    am.init(cfg);
    am.register_loader(std::make_unique<BitmapFontLoader>());
    
    auto r = am.load("test.fnt");
    CHECK(r.has_value());
    if (r) {
        auto* font_ptr = std::any_cast<std::shared_ptr<font::ParsedFont>>(&(*r)->parsed);
        CHECK(font_ptr != nullptr);
        if (font_ptr) {
            CHECK_EQ((*font_ptr)->info.face, "Test");
            CHECK_EQ((*font_ptr)->chars.size(), 2u);
        }
    }
    am.shutdown();
    fs::remove_all(tmp);
}

static void test_cache_hits() {
    auto tmp = fs::temp_directory_path() / "resf2_test_am";
    fs::create_directories(tmp);
    auto path = write_test_plist(tmp);
    
    AssetManager am;
    Config cfg;
    cfg.search_roots = {tmp};
    cfg.worker_threads = 0;
    am.init(cfg);
    am.register_loader(std::make_unique<PlistAtlasLoader>());
    
    // First load: cache miss
    CHECK(!am.is_cached("test.plist"));
    auto r1 = am.load("test.plist");
    CHECK(r1.has_value());
    CHECK(am.is_cached("test.plist"));
    CHECK_EQ(am.cache_count(), 1u);
    
    // Second load: cache hit (same pointer)
    auto r2 = am.load("test.plist");
    CHECK(r2.has_value());
    CHECK_EQ(r1.value().get(), r2.value().get());  // same shared_ptr target
    
    am.shutdown();
    fs::remove_all(tmp);
}

static void test_evict() {
    auto tmp = fs::temp_directory_path() / "resf2_test_am";
    fs::create_directories(tmp);
    auto path = write_test_plist(tmp);
    
    AssetManager am;
    Config cfg;
    cfg.search_roots = {tmp};
    cfg.worker_threads = 0;
    cfg.max_cache_bytes = 1;  // tiny cache, will evict immediately
    am.init(cfg);
    am.register_loader(std::make_unique<PlistAtlasLoader>());
    
    auto r = am.load("test.plist");
    CHECK(r.has_value());
    // With 1-byte cache limit, the asset should be evicted after loading
    // (but the load itself succeeds and returns the asset)
    
    am.evict_all();
    CHECK_EQ(am.cache_count(), 0u);
    CHECK_EQ(am.cache_bytes(), 0u);
    
    am.shutdown();
    fs::remove_all(tmp);
}

static void test_async_load() {
    auto tmp = fs::temp_directory_path() / "resf2_test_am";
    fs::create_directories(tmp);
    auto path = write_test_plist(tmp);
    
    AssetManager am;
    Config cfg;
    cfg.search_roots = {tmp};
    cfg.worker_threads = 2;
    am.init(cfg);
    am.register_loader(std::make_unique<PlistAtlasLoader>());
    
    auto future = am.load_async("test.plist");
    auto status = future.wait_for(std::chrono::seconds(5));
    CHECK_EQ(status, std::future_status::ready);
    auto r = future.get();
    CHECK(r.has_value());
    if (r) {
        CHECK_EQ((*r)->path, "test.plist");
    }
    
    am.shutdown();
    fs::remove_all(tmp);
}

static void test_invalidate() {
    auto tmp = fs::temp_directory_path() / "resf2_test_am";
    fs::create_directories(tmp);
    auto path = write_test_plist(tmp);
    
    AssetManager am;
    Config cfg;
    cfg.search_roots = {tmp};
    cfg.worker_threads = 0;
    am.init(cfg);
    am.register_loader(std::make_unique<PlistAtlasLoader>());
    
    auto r1 = am.load("test.plist");
    CHECK(r1.has_value());
    am.invalidate("test.plist");
    
    // Next load should re-read from disk
    auto r2 = am.load("test.plist");
    CHECK(r2.has_value());
    // Different pointer (re-loaded)
    CHECK(r1.value().get() != r2.value().get());
    
    am.shutdown();
    fs::remove_all(tmp);
}

static void test_registered_extensions() {
    AssetManager am;
    Config cfg;
    cfg.search_roots = {fs::temp_directory_path()};
    cfg.worker_threads = 0;
    am.init(cfg);
    am.register_loader(std::make_unique<PlistAtlasLoader>());
    am.register_loader(std::make_unique<BitmapFontLoader>());
    am.register_loader(std::make_unique<AtfTacticsLoader>());
    
    auto exts = am.registered_extensions();
    CHECK(exts.size() >= 3);
    
    am.shutdown();
}

int main() {
    test_init_shutdown();
    test_load_not_found();
    test_load_no_loader();
    test_load_plist();
    test_load_font();
    test_cache_hits();
    test_evict();
    test_async_load();
    test_invalidate();
    test_registered_extensions();

    std::printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}

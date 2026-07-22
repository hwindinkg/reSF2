#include "../engine/format/location_parser.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using resf2::format::LocationParser;
using resf2::format::LocationData;

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

#define CHECK_MSG(cond, msg)                                            \
    do {                                                                \
        ++g_tests;                                                      \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s (%s)\n",               \
                         __FILE__, __LINE__, msg, #cond);               \
        }                                                               \
    } while (0)

// Count how many location NAME entries exist in assets/locations/
// The game discovers location names by directory scan.
static void test_location_directory_exists() {
    auto loc_dir = fs::path("assets") / "locations";
    CHECK_MSG(fs::exists(loc_dir), "assets/locations directory exists");
    int count = 0;
    for (auto& entry : fs::directory_iterator(loc_dir)) {
        if (entry.is_directory()) {
            ++count;
        }
    }
    std::printf("  Discovered %d location directories\n", count);
    // The original game has 56 locations
    CHECK_MSG(count == 56, "Expected 56 location directories");
}

// Parse ALL 56 location params.xml files using LocationParser
static void test_all_locations_parse() {
    auto loc_dir = fs::path("assets") / "locations";
    int parsed_ok = 0;
    int with_layers = 0;
    int with_floor = 0;
    int with_wall = 0;
    int parse_failed = 0;

    for (auto& entry : fs::directory_iterator(loc_dir)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        auto params_path = entry.path() / "params.xml";
        if (!fs::exists(params_path)) {
            std::fprintf(stderr, "  %s: NO params.xml!\n", name.c_str());
            ++g_failures;
            ++g_tests;
            continue;
        }

        LocationParser parser;
        LocationData loc;
        if (!parser.load_file(params_path.string(), loc)) {
            std::fprintf(stderr, "  %s: PARSE ERROR: %s\n",
                         name.c_str(), parser.error().c_str());
            ++parse_failed;
            ++g_failures;
            ++g_tests;
            continue;
        }
        ++parsed_ok;
        ++g_tests;  // at least one CHECK passes

        loc.name = name;

        // Each location should have at least 1 layer
        if (loc.layers.size() > 0) ++with_layers;
        else {
            std::fprintf(stderr, "  %s: 0 layers!\n", name.c_str());
            ++g_failures;
        }

        // Most locations have floor > 0
        if (loc.floor > 0) ++with_floor;
        if (loc.wall > 0) ++with_wall;

        // Location must have a Color attribute
        if (loc.color.empty()) {
            std::fprintf(stderr, "  %s: no Color attribute\n", name.c_str());
            ++g_failures;
        }

        // Parallax factors: check all layers have factor >= 0 (0 is valid,
        // means "use default 1.0" in the renderer). Max is 1.0.
        // Raid locations (fatum_raid, fungus_raid, etc.) use factor=0.
        for (size_t li = 0; li < loc.layers.size(); ++li) {
            auto& layer = loc.layers[li];
            if (layer.factor < 0.0f || layer.factor > 1.0f) {
                std::fprintf(stderr, "  %s layer %zu: invalid factor=%.2f\n",
                             name.c_str(), li, layer.factor);
                ++g_failures;
            }
            // Each image should have valid dimensions
            for (auto& img : layer.images) {
                if (img.w <= 0 || img.h <= 0) {
                    std::fprintf(stderr, "  %s layer %zu image '%s': invalid dims %.0fx%.0f\n",
                                 name.c_str(), li, img.class_name.c_str(), img.w, img.h);
                    ++g_failures;
                }
            }
        }
    }

    std::printf("  Parsed OK: %d / 56\n", parsed_ok);
    std::printf("  With layers: %d  With floor: %d  With wall: %d\n",
                with_layers, with_floor, with_wall);
    std::printf("  Parse failures: %d\n", parse_failed);

    CHECK_MSG(parsed_ok == 56, "All 56 locations must parse successfully");
    CHECK_MSG(with_layers == 56, "All 56 locations have layers");
}

// Verify specific well-known locations have expected properties
static void test_specific_locations() {
    // arena
    {
        LocationParser parser;
        LocationData loc;
        CHECK_MSG(parser.load_file("assets/locations/arena/params.xml", loc),
                  "arena params.xml loads");
        CHECK_MSG(loc.layers.size() > 0, "arena has layers");
        CHECK_MSG(loc.floor > 0, "arena has floor");
        CHECK_MSG(loc.wall > 0, "arena has wall");
        CHECK_MSG(!loc.color.empty(), "arena has color");
        std::printf("  arena: %zu layers, floor=%.0f, wall=%.0f, color=%s\n",
                    loc.layers.size(), loc.floor, loc.wall, loc.color.c_str());
    }
    // dojo
    {
        LocationParser parser;
        LocationData loc;
        CHECK_MSG(parser.load_file("assets/locations/dojo/params.xml", loc),
                  "dojo params.xml loads");
        CHECK_MSG(loc.layers.size() > 0, "dojo has layers");
        CHECK_MSG(loc.floor > 0, "dojo has floor");
        CHECK_MSG(loc.wall > 0, "dojo has wall");
        CHECK_MSG(!loc.color.empty(), "dojo has color");
        // Dojo should have player/enemy positions from ModelsViewer
        std::printf("  dojo: %zu layers, player=(%.0f,%.0f) enemy=(%.0f,%.0f)\n",
                    loc.layers.size(), loc.player_x, loc.player_y,
                    loc.enemy_x, loc.enemy_y);
    }
    // cave (no atlas, color-only fallback expected)
    {
        LocationParser parser;
        LocationData loc;
        CHECK_MSG(parser.load_file("assets/locations/cave/params.xml", loc),
                  "cave params.xml loads");
        std::printf("  cave: %zu layers, color=%s\n",
                    loc.layers.size(), loc.color.c_str());
    }
    // bridge (complex location with many atlases)
    {
        LocationParser parser;
        LocationData loc;
        CHECK_MSG(parser.load_file("assets/locations/bridge/params.xml", loc),
                  "bridge params.xml loads");
        CHECK_MSG(loc.layers.size() > 0, "bridge has layers");
        std::printf("  bridge: %zu layers\n", loc.layers.size());
        for (auto& l : loc.layers) {
            std::printf("    type=%d factor=%.2f atlas='%s' images=%zu\n",
                        l.type, l.factor, l.atlas_name.c_str(), l.images.size());
        }
    }
}

// Test discover_locations directory enumeration
static void test_discover_locations() {
    auto loc_dir = fs::path("assets") / "locations";
    std::vector<std::string> names;
    for (auto& entry : fs::directory_iterator(loc_dir)) {
        if (entry.is_directory()) {
            names.push_back(entry.path().filename().string());
        }
    }
    CHECK_MSG(names.size() == 56, "discovered 56 location directories");

    // Verify dojo is present
    bool found_dojo = false;
    bool found_arena = false;
    for (auto& n : names) {
        if (n == "dojo") found_dojo = true;
        if (n == "arena") found_arena = true;
    }
    CHECK_MSG(found_dojo, "dojo directory found");
    CHECK_MSG(found_arena, "arena directory found");

    std::printf("  Location names: %zu discovered\n", names.size());
}

int main() {
    std::printf("=== Location Parser Test ===\n\n");

    test_location_directory_exists();
    std::printf("\n");
    test_discover_locations();
    std::printf("\n");
    test_specific_locations();
    std::printf("\n");
    test_all_locations_parse();

    std::printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures > 0 ? 1 : 0;
}

// tests/test_location_layer_path.cpp
//
// Regression test for <Layer Path="..."> in params.xml.
//
// [ORIGINAL] The layer parser at game+0x3E40D0 reads three attributes off
// <Layer>: Atlas, Path and Scaling. `Path` redirects the atlas lookup to
// ANOTHER location's directory, e.g.
//
//     <Layer Type="1" Factor="0.01" Atlas="bg" Path="locations/waterfall/">
//
// inside locations/waterfall_small/params.xml. 26 layers across the shipped
// files use it. reSF2 ignored the attribute, so those atlases were searched for
// under the *current* location, were never found, and the layers rendered
// nothing at all — a correctness bug, not a cosmetic one.
//
// `Scaling` (62 layers) was likewise dropped.
//
// This test asserts the parser keeps both, and that every shipped params.xml
// that uses Path names a location directory that actually exists.

#include "../engine/format/location_parser.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace resf2::format;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++failed; } \
    else { std::printf("  PASS: %s\n", msg); ++passed; } \
} while (0)

// Strip "locations/waterfall/" down to "waterfall", the way the loader does.
static std::string owner_of(const std::string& path) {
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    const auto slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

static void test_parses_path_and_scaling() {
    std::printf("\n-- Path / Scaling are parsed --\n");

    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Root Music=\"6|7\" Color=\"0x281409\" Wall=\"305\" Floor=\"80\""
        " Width=\"1960\" Height=\"560\">\n"
        "  <Layer Type=\"1\" Factor=\"0.01\" Atlas=\"bg\""
        " Path=\"locations/waterfall/\">\n"
        "    <Image X=\"-255\" Y=\"0\" ClassName=\"background_1\""
        " Width=\"512\" Height=\"512\"/>\n"
        "  </Layer>\n"
        "  <Layer Type=\"1\" Factor=\"1\" Atlas=\"atlas_layer3\" Scaling=\"1\">\n"
        "    <Image X=\"0\" Y=\"0\" ClassName=\"layer_3\" Width=\"256\" Height=\"64\"/>\n"
        "  </Layer>\n"
        "  <Layer Type=\"1\" Factor=\"1\" Atlas=\"atlas_layer2\">\n"
        "    <Image X=\"0\" Y=\"20\" ClassName=\"layer_2\" Width=\"512\" Height=\"512\"/>\n"
        "  </Layer>\n"
        "</Root>\n";

    LocationParser p;
    LocationData d;
    CHECK(p.parse(xml, d), "params.xml parses");
    CHECK(d.layers.size() == 3, "three layers parsed");
    if (d.layers.size() < 3) return;

    // Root box, for good measure — these feed the camera framing.
    CHECK(d.width == 1960.0f && d.height == 560.0f, "Root Width/Height parsed");
    CHECK(d.wall == 305.0f && d.floor == 80.0f, "Root Wall/Floor parsed");

    CHECK(d.layers[0].path == "locations/waterfall/",
          "Layer Path is preserved verbatim");
    CHECK(owner_of(d.layers[0].path) == "waterfall",
          "Path resolves to the owning location name");
    CHECK(!d.layers[0].scaling, "Scaling absent -> false");

    CHECK(d.layers[1].scaling, "Scaling=\"1\" -> true");
    CHECK(d.layers[1].path.empty(), "no Path -> empty (use own location)");

    CHECK(!d.layers[2].scaling && d.layers[2].path.empty(),
          "plain layer has neither");
}

static void test_shipped_paths_resolve() {
    std::printf("\n-- every shipped Path names a real location --\n");

    const std::filesystem::path roots[] = {
        "assets/locations",
        "assets/assets/locations",
        "assets/files/assets/locations",
    };
    std::filesystem::path root;
    for (const auto& r : roots) {
        if (std::filesystem::is_directory(r)) { root = r; break; }
    }
    if (root.empty()) {
        std::printf("  SKIP: no locations directory found (run from repo root)\n");
        return;
    }

    int with_path = 0, resolved = 0, scaling_layers = 0;
    std::vector<std::string> unresolved;

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        const auto params = entry.path() / "params.xml";
        if (!std::filesystem::exists(params)) continue;

        LocationParser p;
        LocationData d;
        if (!p.load_file(params.string(), d)) continue;

        for (const auto& layer : d.layers) {
            if (layer.scaling) ++scaling_layers;
            if (layer.path.empty()) continue;
            ++with_path;
            const auto owner = owner_of(layer.path);
            if (std::filesystem::is_directory(root / owner)) {
                ++resolved;
            } else {
                unresolved.push_back(entry.path().filename().string() +
                                     " -> " + layer.path);
            }
        }
    }

    std::printf("  %d layers use Path, %d resolve; %d layers set Scaling\n",
                with_path, resolved, scaling_layers);
    for (const auto& u : unresolved)
        std::fprintf(stderr, "    unresolved: %s\n", u.c_str());

    CHECK(with_path > 0, "the shipped data really does use Path");
    CHECK(scaling_layers > 0, "the shipped data really does use Scaling");
    CHECK(unresolved.empty(), "every Path names an existing location directory");
}

int main() {
    std::printf("=== location <Layer Path/Scaling> test ===\n");
    test_parses_path_and_scaling();
    test_shipped_paths_resolve();
    std::printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

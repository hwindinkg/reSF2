// tests/test_world_geometry.cpp
//
// Behavioural checks on the world/model placement invariants, run against the
// real shipped assets. These are the checks that were missing while the engine
// rendered the fighter 138 world units below the floor and still reported
// "26/26 tests passed".
//
// Everything here is pure data + arithmetic, so it needs no GL context. The
// arithmetic mirrors what Game::resolve_body_node / Game::update_camera do; if
// those diverge from this file, one of the two is wrong and the test says so.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../engine/format/location_parser.hpp"
#include "../engine/format/xml_doc.hpp"
#include "../engine/game/types.hpp"

namespace {

int g_fail = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++g_fail;
    }
}

void check_near(float got, float want, float tol, const std::string& what) {
    ++g_checks;
    if (!(std::fabs(got - want) <= tol)) {
        std::fprintf(stderr, "FAIL: %s — got %.2f, want %.2f +-%.2f (off by %.2f)\n",
                     what.c_str(), got, want, tol, got - want);
        ++g_fail;
    }
}

std::string read_text(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// Node names in file order — this is the order AssetManager::load_skeleton
// builds, and it is what indexes the per-frame arrays in the .bin animations.
struct Skeleton {
    std::vector<std::string> order;
    std::vector<float> rest_y;

    bool load(const std::filesystem::path& p) {
        const std::string xml = read_text(p);
        if (xml.empty()) return false;
        resf2::format::XmlDocument doc;
        if (!doc.parse(xml)) return false;
        auto* scene = doc.root()->first_child("Scene");
        if (!scene) return false;
        auto* nodes = scene->first_child("Nodes");
        if (!nodes) return false;
        for (const auto& child : nodes->children) {
            if (child.attr("X").empty()) continue;
            order.push_back(child.name);
            rest_y.push_back(std::strtof(child.attr("Y").c_str(), nullptr));
        }
        return !order.empty();
    }

    int index_of(const std::string& n) const {
        auto it = std::find(order.begin(), order.end(), n);
        return it == order.end() ? -1 : static_cast<int>(it - order.begin());
    }
};

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path root = (argc > 1) ? argv[1] : ".";
    const auto assets = root / "assets";

    // ---------------------------------------------------------------- location
    resf2::format::LocationParser lp;
    resf2::format::LocationData loc;
    const std::string params = read_text(assets / "locations" / "dojo" / "params.xml");
    check(!params.empty(), "dojo/params.xml is readable");
    if (params.empty()) return 1;
    check(lp.parse(params, loc), "dojo/params.xml parses");

    check_near(loc.width, 1960.0f, 0.5f, "dojo Width");
    check_near(loc.height, 560.0f, 0.5f, "dojo Height");
    check_near(loc.wall, 305.0f, 0.5f, "dojo Wall");
    check_near(loc.floor, 80.0f, 0.5f, "dojo Floor");

    // The physics floor declared by params, and the top edge of the floor strip
    // the artist actually drew (layer_3, Y is inverted for images). Two
    // independent derivations of the same plane — if they drift apart the
    // coordinate model is wrong.
    const float floor_declared = -loc.height * 0.5f + loc.floor;
    float floor_drawn = floor_declared;
    bool have_drawn = false;
    for (const auto& layer : loc.layers) {
        for (const auto& img : layer.images) {
            if (img.class_name.rfind("layer_3", 0) != 0) continue;
            const float top = -img.y + img.h * 0.5f;
            if (!have_drawn || top > floor_drawn) { floor_drawn = top; have_drawn = true; }
        }
    }
    check(have_drawn, "dojo has a layer_3 floor strip");
    check_near(floor_declared, -200.0f, 0.5f, "declared floor = -Height/2 + Floor");
    check_near(floor_drawn, floor_declared, 10.0f,
               "drawn floor agrees with declared floor");

    // <ModelsViewer> positions are measured from the LEFT edge of the world.
    // The enemy stands under the ceiling hook the punching bag hangs from,
    // which is drawn at layer_5 X (an <Image>, so centred on the origin).
    const float enemy_world_x = loc.enemy_x - loc.width * 0.5f;
    float hook_x = 0.0f;
    bool have_hook = false;
    for (const auto& layer : loc.layers) {
        for (const auto& img : layer.images) {
            if (img.class_name != "layer_5") continue;
            hook_x = img.x;
            have_hook = true;
        }
    }
    check(have_hook, "dojo has the layer_5 ceiling hook");
    if (have_hook)
        check_near(enemy_world_x, hook_x, 5.0f,
                   "enemy_x - Width/2 lands on the ceiling hook");

    // ---------------------------------------------------------------- skeleton
    Skeleton skel;
    check(skel.load(assets / "models" / "skeleton_full.xml"), "skeleton_full.xml loads");
    const int i_pivot = skel.index_of("NPivot");
    check(i_pivot >= 0, "skeleton has NPivot");

    std::vector<int> i_feet;
    for (const char* n : {"NToe_1", "NToe_2", "NHeel_1", "NHeel_2",
                          "NAnkle_1", "NAnkle_2"}) {
        const int i = skel.index_of(n);
        if (i >= 0) i_feet.push_back(i);
    }
    check(!i_feet.empty(), "skeleton has foot nodes");
    if (i_pivot < 0 || i_feet.empty()) {
        std::fprintf(stderr, "\n%d/%d checks failed\n", g_fail, g_checks);
        return 1;
    }

    // ------------------------------------------------------------- animations
    //
    // .bin animations are authored with the location floor at y = 0, so a
    // grounded animation must keep its lowest foot node on the floor plane for
    // EVERY frame. This is the invariant the engine was violating.
    auto lowest_foot_world = [&](const resf2::game::AnimationData& a, int frame,
                                 float& out) -> bool {
        float best = 0.0f;
        bool any = false;
        for (int idx : i_feet) {
            float x, y, z;
            if (!a.get_node_pos(frame, idx, x, y, z)) continue;
            if (!any || y < best) { best = y; any = true; }
        }
        if (!any) return false;
        out = floor_declared + best;
        return true;
    };

    struct Case {
        const char* name;
        bool grounded;   // feet must stay on the floor for every frame
    };
    const Case cases[] = {
        {"fists1_stance_idle", true},
        {"stance_idle", true},
        {"stance_2", true},
        {"jump", false},
    };

    const auto anim_dir = assets / "animations" / "binary";
    int grounded_seen = 0;
    for (const auto& c : cases) {
        const auto path = anim_dir / (std::string(c.name) + ".bin");
        if (!std::filesystem::exists(path)) {
            std::printf("SKIP %s (not present)\n", c.name);
            continue;
        }
        resf2::game::AnimationData a;
        check(a.load(path.string()), std::string(c.name) + ": .bin loads");
        check(a.frame_count > 0, std::string(c.name) + ": has frames");
        if (a.frame_count <= 0) continue;

        float lo = 0.0f, hi = 0.0f;
        bool any = false;
        for (int f = 0; f < a.frame_count; ++f) {
            float y;
            if (!lowest_foot_world(a, f, y)) continue;
            if (!any) { lo = hi = y; any = true; }
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
        check(any, std::string(c.name) + ": foot nodes resolve");
        if (!any) continue;

        std::printf("%-22s frames=%3d  foot world Y %.1f .. %.1f  (floor %.1f)\n",
                    c.name, a.frame_count, lo, hi, floor_declared);

        if (c.grounded) {
            ++grounded_seen;
            // A grounded animation must (a) make contact with the floor and
            // never sink through it, and (b) never float away from it. A foot
            // may legitimately lift during a step or a lunge — stance_2 raises
            // one by 6 units — so only the *lowest* frame is pinned tightly.
            check_near(lo, floor_declared, 3.0f,
                       std::string(c.name) + ": makes floor contact without sinking");
            check(hi <= floor_declared + 60.0f,
                  std::string(c.name) + ": never floats away from the floor");
        } else {
            // A jump has to actually leave the ground and come back to it.
            check(hi - floor_declared > 100.0f,
                  std::string(c.name) + ": leaves the ground by more than 100 units");
            check_near(lo, floor_declared, 5.0f,
                       std::string(c.name) + ": returns to the floor");
        }
    }
    check(grounded_seen >= 2, "at least two grounded animations were checked");

    // ------------------------------------------------------------------ camera
    // Game::update_camera frames the whole world height into the viewport.
    for (const auto& [vw, vh] : std::vector<std::pair<float, float>>{
             {1280.0f, 720.0f}, {1024.0f, 768.0f}, {1920.0f, 1080.0f}}) {
        const float zoom = vh / loc.height;
        check_near(vh / zoom, loc.height, 0.01f,
                   "viewport " + std::to_string(static_cast<int>(vw)) + "x" +
                       std::to_string(static_cast<int>(vh)) + " shows Height world units");
        const float half_view_w = vw / (2.0f * zoom);
        check(half_view_w < loc.width * 0.5f,
              "viewport " + std::to_string(static_cast<int>(vw)) +
                  " is narrower than the world");
    }

    if (g_fail) {
        std::fprintf(stderr, "\n%d of %d checks failed\n", g_fail, g_checks);
        return 1;
    }
    std::printf("\nall %d checks passed\n", g_checks);
    return 0;
}

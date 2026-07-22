#include "../engine/format/xml_doc.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using resf2::format::XmlDocument;

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

#define CHECK_MSG(cond, msg)                                            \
    do {                                                                \
        ++g_tests;                                                      \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s (%s)\n",               \
                         __FILE__, __LINE__, msg, #cond);               \
        }                                                               \
    } while (0)

static std::string read_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static void test_body_xml() {
    auto xml = read_text("assets/models/body.xml");
    XmlDocument doc;
    CHECK_MSG(doc.parse(xml), "body.xml parse failed");
    auto* scene = doc.root()->first_child("Scene");
    CHECK_MSG(scene != nullptr, "body.xml: no <Scene>");

    auto* ns = scene->first_child("Nodes");
    CHECK(ns != nullptr);
    int body_nodes = 0, macro_nodes = 0;
    for (const auto& c : ns->children) {
        auto t = c.attr("Type");
        if (t == "Node") ++body_nodes;
        else if (t == "MacroNode") ++macro_nodes;
    }
    std::printf("  body.xml: %d nodes, %d macronodes\n", body_nodes, macro_nodes);
    CHECK(body_nodes > 0);
    CHECK(macro_nodes > 0);

    auto* es = scene->first_child("Edges");
    CHECK(es != nullptr);
    int edge_count = 0;
    for (const auto& c : es->children) {
        if (c.attr("Type") == "Edge") ++edge_count;
    }
    std::printf("  body.xml: %d edges\n", edge_count);
    CHECK(edge_count > 0);

    auto* fs = scene->first_child("Figures");
    CHECK(fs != nullptr);
    int capsule_count = 0, triangle_count = 0;
    for (const auto& c : fs->children) {
        auto t = c.attr("Type");
        if (t == "Capsule") ++capsule_count;
        else if (t == "Triangle") ++triangle_count;
    }
    std::printf("  body.xml: %d capsules, %d triangles\n", capsule_count, triangle_count);
    CHECK(capsule_count > 0);
    CHECK(triangle_count > 0);
}

static void test_head_xml() {
    auto xml = read_text("assets/models/head.xml");
    std::printf("  head.xml: %zu bytes\n", xml.size());
    XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "  head.xml parse error: %s\n", doc.error().c_str());
        CHECK(false);
        return;
    }
    auto* scene = doc.root()->first_child("Scene");
    CHECK_MSG(scene != nullptr, "head.xml: no <Scene>");

    auto* ns = scene->first_child("Nodes");
    CHECK(ns != nullptr);
    int head_nodes = 0, macro_nodes = 0;
    for (const auto& c : ns->children) {
        auto t = c.attr("Type");
        if (t == "Node") ++head_nodes;
        else if (t == "MacroNode") ++macro_nodes;
    }
    std::printf("  head.xml: %d nodes, %d macronodes\n", head_nodes, macro_nodes);
    CHECK(head_nodes > 0);
    CHECK(macro_nodes > 0);

    auto* es = scene->first_child("Edges");
    CHECK(es != nullptr);
    int edge_count = 0;
    for (const auto& c : es->children) {
        if (c.attr("Type") == "Edge") ++edge_count;
    }
    CHECK(edge_count > 0);

    auto* fs = scene->first_child("Figures");
    CHECK(fs != nullptr);
    int triangle_count = 0;
    for (const auto& c : fs->children) {
        if (c.attr("Type") == "Triangle") ++triangle_count;
    }
    std::printf("  head.xml: %d triangles\n", triangle_count);
    CHECK(triangle_count > 0);
}

static void test_skeleton_xml() {
    auto xml = read_text("assets/models/skeleton.xml");
    std::printf("  skeleton.xml: %zu bytes\n", xml.size());
    XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "  skeleton.xml parse error: %s\n", doc.error().c_str());
        CHECK(false);
        return;
    }
    auto* scene = doc.root()->first_child("Scene");
    CHECK_MSG(scene != nullptr, "skeleton.xml: no <Scene>");

    auto* ns = scene->first_child("Nodes");
    CHECK(ns != nullptr);
    int skel_count = 0, macro_count = 0;
    for (const auto& c : ns->children) {
        if (!c.attr("X").empty()) {
            ++skel_count;
            if (c.attr("Type") == "MacroNode") ++macro_count;
        }
    }
    std::printf("  skeleton.xml: %d nodes (%d macronodes)\n", skel_count, macro_count);
    CHECK(skel_count > 0);

    // Verify NPivot Y value (used as stance_npivot_y_ baseline)
    {
        bool found_npivot = false;
        for (const auto& c : ns->children) {
            if (c.name == "NPivot") {
                float y = std::stof(c.attr("Y"));
                std::printf("  NPivot Y = %.6f\n", y);
                CHECK_EQ((int)(y * 100 + 0.5f), 16948);  // 169.4843...
                found_npivot = true;
                break;
            }
        }
        CHECK_MSG(found_npivot, "NPivot node not found in skeleton.xml");
    }

    auto* es = scene->first_child("Edges");
    CHECK(es != nullptr);
    int edge_count = 0;
    for (const auto& c : es->children) {
        auto t = c.attr("Type");
        if (t == "Edge" || t == "Muscle") ++edge_count;
    }
    std::printf("  skeleton.xml: %d edges\n", edge_count);
    CHECK(edge_count > 0);
}

static void test_punching_bag_xml() {
    auto xml = read_text("assets/models/skeleton_punching_bag.xml");
    std::printf("  skeleton_punching_bag.xml: %zu bytes\n", xml.size());
    XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "  skeleton_punching_bag.xml parse error: %s\n", doc.error().c_str());
        CHECK(false);
        return;
    }
    auto* scene = doc.root()->first_child("Scene");
    CHECK_MSG(scene != nullptr, "skeleton_punching_bag.xml: no <Scene>");

    auto* ns = scene->first_child("Nodes");
    CHECK(ns != nullptr);
    int node_count = 0, com_count = 0;
    for (const auto& c : ns->children) {
        auto t = c.attr("Type");
        if (t == "Node") ++node_count;
        else if (t == "CenterOfMass") ++com_count;
    }
    std::printf("  skeleton_punching_bag.xml: %d nodes, %d COM\n", node_count, com_count);
    CHECK(node_count > 0);
    CHECK_EQ(com_count, 1);

    auto* es = scene->first_child("Edges");
    CHECK(es != nullptr);
    int edge_count = 0;
    for (const auto& c : es->children) {
        if (c.attr("Type") == "Edge") ++edge_count;
    }
    std::printf("  skeleton_punching_bag.xml: %d edges\n", edge_count);
    CHECK(edge_count > 0);
}

static void test_loading_xml() {
    auto xml = read_text("assets/1536/textures/fullscreen/startLoading.xml");
    std::printf("  startLoading.xml: %zu bytes\n", xml.size());
    XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "  startLoading.xml parse error: %s\n", doc.error().c_str());
        CHECK(false);
        return;
    }
    auto* images = doc.root()->first_child("Images");
    CHECK_MSG(images != nullptr, "startLoading.xml: no <Images>");
    if (!images) return;
    CHECK(!images->attr("Scale").empty());

    int image_count = 0;
    for (const auto& c : images->children) {
        if (c.name == "Image") {
            ++image_count;
            CHECK(!c.attr("File").empty());
            CHECK(!c.attr("X").empty());
        }
    }
    std::printf("  startLoading.xml: %d images\n", image_count);
    CHECK(image_count > 0);
}

// Test MidFrames → fps formula: fps = 60 / (1 + mid_frames)
static void test_mid_frames_formula() {
    // MidFrames=2 (default) → fps=20
    { float fps = 60.0f / (1.0f + 2); CHECK_EQ((int)fps, 20); }
    // MidFrames=1 → fps=30
    { float fps = 60.0f / (1.0f + 1); CHECK_EQ((int)fps, 30); }
    // MidFrames=0 → fps=60
    { float fps = 60.0f / (1.0f + 0); CHECK_EQ((int)fps, 60); }
    // MidFrames=3 → fps=15
    { float fps = 60.0f / (1.0f + 3); CHECK_EQ((int)fps, 15); }
    // FirstFrame at fps=20: anim_time = first_frame / fps
    { int ff = 5; float fps = 20.0f; float t = (float)ff / fps; CHECK_EQ((int)(t * 1000), 250); }
    // FirstFrame=-1 → no override
    { int ff = -1; float fps = 20.0f; float t = (ff >= 0) ? (float)ff / fps : 0; CHECK_EQ((int)(t * 1000), 0); }
    std::printf("  MidFrames/FirstFrame formula: OK\n");
}

int main() {
    test_body_xml();
    test_head_xml();
    test_skeleton_xml();
    test_punching_bag_xml();
    test_loading_xml();
    test_mid_frames_formula();

    std::printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures > 0 ? 1 : 0;
}

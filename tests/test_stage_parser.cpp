// Tests the stages.xml parser — campaign structure validation.

#include "../engine/format/stage_parser.hpp"
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    std::printf("=== Stage Parser Test ===\n\n");

    auto path = fs::path("assets") / "stages.xml";
    if (!fs::exists(path)) {
        std::printf("NOT FOUND: %s\n", path.string().c_str());
        return 1;
    }
    std::printf("File: %s\n", path.string().c_str());

    resf2::format::StageParser parser;
    resf2::format::StageData data;
    if (!parser.load_file(path.string(), data)) {
        std::printf("PARSE ERROR: %s\n", parser.error().c_str());
        return 1;
    }

    std::printf("\nZones: %zu\n", data.zones.size());
    int total_battles = 0, total_fights = 0;
    for (auto& z : data.zones) {
        std::printf("  Zone '%s' start=%d battles=%zu\n",
                    z.name.c_str(), z.start, z.battles.size());
        for (auto& b : z.battles) {
            std::printf("    Battle '%s' type=%s (%s) fights=%zu\n",
                        b.name.c_str(), b.type.c_str(), b.location.c_str(),
                        b.fights.size());
            total_battles++;
            for (auto& f : b.fights) {
                std::printf("      Fight '%s' power=%d rounds=%d warriors=%zu\n",
                            f.name.c_str(), f.power, f.rounds, f.warriors.size());
                total_fights++;
                for (auto& w : f.warriors) {
                    std::printf("        Warrior '%s' tactic=%s\n",
                                w.template_name.c_str(), w.tactic.c_str());
                }
            }
        }
    }
    std::printf("\nTotals: %zu zones, %d battles, %d fights\n",
                data.zones.size(), total_battles, total_fights);

    // Verify Zone 1 (Dojo)
    if (!data.zones.empty()) {
        auto& z = data.zones[0];
        std::printf("\nZone 1: '%s' start=%d battles=%zu\n",
                    z.name.c_str(), z.start, z.battles.size());
        if (!z.battles.empty()) {
            auto& b = z.battles[0];
            std::printf("  First battle: '%s' type=%s loc=%s\n",
                        b.name.c_str(), b.type.c_str(), b.location.c_str());
            if (!b.fights.empty()) {
                auto& f = b.fights[0];
                std::printf("   First fight: '%s' power=%d warriors=%zu\n",
                            f.name.c_str(), f.power, f.warriors.size());
            }
        }
    }

    std::printf("\n=== Done ===\n");
    return 0;
}

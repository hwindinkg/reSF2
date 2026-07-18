// Phase 2 test: validates the full asset pipeline
//   - DZ archive opening
//   - XML document parsing
//   - Moves database loading
//   - Location params parsing

#include "../engine/fight/moves.hpp"
#include "../engine/format/location_parser.hpp"
#include "../engine/reverse/dz_reader.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    std::printf("=== Asset Pipeline Test ===\n\n");

    // 1. DZ Archives
    std::printf("--- DZ Archives ---\n");
    auto& dz = resf2::dz::DzRegistry::instance();
    auto root = fs::path("assets");

    if (fs::exists(root / "files.dz")) {
        if (dz.open_archive((root / "files.dz").string())) {
            std::printf("  files.dz: OK\n");
        }
    }
    if (fs::exists(root / "animations.dz")) {
        if (dz.open_archive((root / "animations.dz").string())) {
            std::printf("  animations.dz: OK\n");
        }
    }

    // 2. Moves XML parser
    std::printf("\n--- Moves.xml Parser ---\n");
    resf2::fight::MoveDatabase moves;
    auto moves_path = root / "animations" / "moves.xml";
    if (fs::exists(moves_path)) {
        if (moves.load_from_file(moves_path.string())) {
            std::printf("  Moves loaded: %zu\n", moves.size());

            int with_intervals = 0, with_uninterrupt = 0;
            for (auto& [n, m] : moves.all_moves()) {
                if (!m.attack_intervals.empty()) with_intervals++;
                if (!m.uninterrupt_intervals.empty()) with_uninterrupt++;
            }
            std::printf("  With attack intervals: %d\n", with_intervals);
            std::printf("  With uninterrupt: %d\n", with_uninterrupt);

            // Verify specific moves
            auto* lp = moves.find("LowPunch");
            if (lp) {
                std::printf("  LowPunch: dir=%s type=%s kc=%d\n",
                            lp->direction.c_str(), lp->move_type.c_str(), lp->key_count);
                for (auto& iv : lp->attack_intervals) {
                    std::printf("    Attack [%.0f-%.0f] dmg=%d impulse=(%.0f,%.0f)\n",
                                iv.start, iv.end, iv.damage, iv.impulse.x, iv.impulse.y);
                }
            }

            auto* stance = moves.find("StanceIdle");
            if (stance) {
                std::printf("  StanceIdle: file=%s\n", stance->filename.c_str());
            }

            // Query: 1key Central Punches
            resf2::fight::MoveDatabase::MoveQuery q;
            q.direction = "Central";
            q.move_type = "Punch";
            q.key_count = 1;
            auto results = moves.query(q);
            std::printf("  Central 1key Punches: %zu\n", results.size());
        } else {
            std::printf("  FAILED: %s\n", moves_path.string().c_str());
        }
    } else {
        std::printf("  NOT FOUND: %s\n", moves_path.string().c_str());
    }

    // 3. Location XML parser
    std::printf("\n--- Location Parser ---\n");
    auto loc_path = root / "locations" / "dojo" / "params.xml";
    if (fs::exists(loc_path)) {
        resf2::format::LocationParser parser;
        resf2::format::LocationData loc;
        if (parser.load_file(loc_path.string(), loc)) {
            std::printf("  Color: %s\n", loc.color.c_str());
            std::printf("  Size: %.0f x %.0f\n", loc.width, loc.height);
            std::printf("  Player: (%.0f, %.0f)\n", loc.player_x, loc.player_y);
            std::printf("  Enemy: (%.0f, %.0f)\n", loc.enemy_x, loc.enemy_y);
            std::printf("  Layers: %zu\n", loc.layers.size());
            for (size_t i = 0; i < loc.layers.size(); ++i) {
                auto& l = loc.layers[i];
                std::printf("    Layer %zu: type=%d atlas='%s' images=%zu\n",
                            i, l.type, l.atlas_name.c_str(), l.images.size());
            }
        } else {
            std::printf("  FAILED: %s\n", parser.error().c_str());
        }
    } else {
        std::printf("  NOT FOUND: %s\n", loc_path.string().c_str());
    }

    std::printf("\n=== Done ===\n");
    return 0;
}

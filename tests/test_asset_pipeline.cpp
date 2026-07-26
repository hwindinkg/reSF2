// tests/test_asset_pipeline.cpp
//
// The asset pipeline end to end: derbh archives, moves.xml, location params.
//
// This used to print what it found and `return 0` unconditionally, so it could
// not fail no matter how broken the pipeline was. Every observation is now an
// assertion, and the assets it needs are the ones committed to the repository.

#include <cstdio>
#include <filesystem>

#include "../engine/fight/moves.hpp"
#include "../engine/format/location_parser.hpp"
#include "../engine/reverse/dz_reader.hpp"
#include "check.hpp"

namespace fs = std::filesystem;
using resf2::test::check;
using resf2::test::check_eq;
using resf2::test::check_ge;
using resf2::test::check_near;

int main(int argc, char** argv) {
    const fs::path root = (argc > 1) ? fs::path(argv[1]) / "assets" : fs::path("assets");

    // --- derbh archives ------------------------------------------------------
    auto& dz = resf2::dz::DzRegistry::instance();
    check(fs::exists(root / "files.dz"), "files.dz is present");
    check(dz.open_archive((root / "files.dz").string()), "files.dz opens");
    check(fs::exists(root / "animations.dz"), "animations.dz is present");
    check(dz.open_archive((root / "animations.dz").string()), "animations.dz opens");

    // Reading through the registry must produce real, decompressed content —
    // this is what was broken while the DZ coder was unimplemented.
    const auto forge = dz.read_file("forge.xml");
    check_ge(static_cast<double>(forge.size()), 100000.0,
             "forge.xml decompresses to its full size");
    const auto moves_blob = dz.read_file("moves.xml");
    check_ge(static_cast<double>(moves_blob.size()), 1000000.0,
             "moves.xml decompresses to its full size");

    // --- moves.xml -----------------------------------------------------------
    resf2::fight::MoveDatabase moves;
    const auto moves_path = root / "animations" / "moves.xml";
    check(fs::exists(moves_path), "animations/moves.xml is present");
    check(moves.load_from_file(moves_path.string()), "moves.xml parses");
    check_ge(static_cast<double>(moves.size()), 100.0, "moves.xml defines many moves");

    int with_intervals = 0, with_uninterrupt = 0;
    for (const auto& [n, m] : moves.all_moves()) {
        if (!m.attack_intervals.empty()) ++with_intervals;
        if (!m.uninterrupt_intervals.empty()) ++with_uninterrupt;
    }
    check_ge(with_intervals, 20.0, "some moves carry attack intervals");
    check_ge(with_uninterrupt, 1.0, "some moves carry uninterrupt intervals");

    const auto* lp = moves.find("LowPunch");
    check(lp != nullptr, "LowPunch exists");
    if (lp) {
        check(!lp->direction.empty(), "LowPunch has a direction");
        check_eq(lp->move_type, std::string("Punch"), "LowPunch is a Punch");
        check_ge(static_cast<double>(lp->attack_intervals.size()), 1.0,
                 "LowPunch has an attack interval");
    }

    resf2::fight::MoveDatabase::MoveQuery q;
    q.direction = "Central";
    q.move_type = "Punch";
    q.key_count = 1;
    check_ge(static_cast<double>(moves.query(q).size()), 1.0,
             "a one-key central punch is reachable");

    // --- location params -----------------------------------------------------
    const auto loc_path = root / "locations" / "dojo" / "params.xml";
    check(fs::exists(loc_path), "dojo/params.xml is present");
    resf2::format::LocationParser parser;
    resf2::format::LocationData loc;
    check(parser.load_file(loc_path.string(), loc), "dojo/params.xml parses");
    check_near(loc.width, 1960.0, 0.5, "dojo Width");
    check_near(loc.height, 560.0, 0.5, "dojo Height");
    check_ge(static_cast<double>(loc.layers.size()), 4.0, "dojo has its layers");
    int images = 0;
    for (const auto& l : loc.layers) images += static_cast<int>(l.images.size());
    check_ge(images, 20.0, "dojo layers carry their images");

    return resf2::test::summary();
}

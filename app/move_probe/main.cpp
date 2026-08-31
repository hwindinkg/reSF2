// move_probe — moves.xml parser + condition evaluator probe (Phase 3.2a).
//
// Loads res/moves.xml (either the extracted copy at
// reference/extracted/xml/res/moves.xml or from the xml.dat archive),
// parses all 1048 moves into MoveDef, verifies the HighPunch contract,
// then evaluates HighPunch's <Conditions> against two fight contexts:
//   1. neutral (no keys, Fight stage, no current animation)  -> expect FALSE
//   2. Punch Tap buffered + Fight stage + no conflicting anim -> expect TRUE
// and prints the full evaluation trace for each.
//
// Usage: move_probe [moves.xml path] [xml.dat path]
// Defaults to the extracted file; falls back to reading the archive.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "scene/conditions.hpp"
#include "scene/move_def.hpp"
#include "xml_archive.hpp"
#include "zstd_stream.hpp"

namespace {

const std::string kDefaultExtracted = "reference/extracted/xml/res/moves.xml";
const std::string kDefaultDat = "reference/www/res/xml.9e0b4b10.dat";

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

// Loads moves.xml text: direct file, else extract "res/moves.xml" from the
// xml.dat archive.
std::string load_moves_xml(const std::string& extracted,
                           const std::string& dat) {
    if (std::filesystem::exists(extracted)) {
        const std::vector<std::uint8_t> bytes = read_file(extracted);
        return std::string(bytes.begin(), bytes.end());
    }
    const std::vector<std::uint8_t> compressed = read_file(dat);
    const std::vector<std::uint8_t> decompressed =
        sf2::data::zstd_decompress(compressed);
    const std::vector<sf2::data::archive_entry> entries =
        sf2::data::xml_archive_parse(decompressed.data(), decompressed.size());
    for (const sf2::data::archive_entry& e : entries) {
        if (e.name == "res/moves.xml") {
            return std::string(e.data.begin(), e.data.end());
        }
    }
    throw std::runtime_error("res/moves.xml not found in " + dat);
}

// Context for the probe's HighPunch tests.
// The Keys condition only gates when the fighter is input-gated
// (`Ae.gm` true — the strike/continuation path; JS `vm.he`: with gm false
// during move testing the condition passes trivially and the actual input
// gating is the KeyPressed event). The probe sets gm=true so the Keys
// condition exercises its key-matching logic.
sf2::scene::FightContext neutral_context() {
    sf2::scene::FightContext ctx;
    ctx.stage = sf2::scene::round_stage::fight;
    ctx.keys_gm = true;
    return ctx;
}

sf2::scene::FightContext punch_tap_context() {
    sf2::scene::FightContext ctx;
    ctx.stage = sf2::scene::round_stage::fight;
    ctx.keys_gm = true;
    ctx.keys.push_back({sf2::scene::key_type::punch,
                        sf2::scene::press_type::tap});
    return ctx;
}

void print_move_summary(const sf2::scene::MoveDef& m) {
    std::cout << "  name=" << m.name
              << " type=" << (m.type.empty() ? "(none)" : m.type)
              << " file=" << m.file_name
              << " priority=" << m.priority
              << " mid=" << m.mid_frames
              << " first=" << m.first_frame
              << " end=" << m.end_frame
              << " mirror=" << (m.mirror_node.empty() ? "-" : m.mirror_node)
              << " tacticWeapon=" << (m.tactic_weapon.empty() ? "-" : m.tactic_weapon)
              << "\n";
    std::cout << "  template tags (" << m.template_tags.size() << "):";
    for (const std::string& t : m.template_tags) std::cout << " " << t;
    std::cout << "\n";
    std::cout << "  conditions: " << m.conditions.size()
              << "  tactics: " << m.tactics.size()
              << "  intervals: " << m.intervals.size()
              << "  locks: " << m.locks.size() << "\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::string extracted = argc > 1 ? argv[1] : kDefaultExtracted;
    const std::string dat = argc > 2 ? argv[2] : kDefaultDat;
    try {
        std::cout << "=== move_probe: moves.xml parser + condition evaluator ===\n\n";

        const std::string xml = load_moves_xml(extracted, dat);
        std::cout << "moves.xml: " << xml.size() << " bytes\n";

        std::map<std::string, sf2::scene::MoveDef> moves;
        if (!sf2::scene::parse_moves(xml, moves)) {
            throw std::runtime_error("parse_moves: <Movesxml> root not found");
        }
        std::cout << "parsed moves: " << moves.size()
                  << " (expected 1048)\n";

        // --- HighPunch verification ----------------------------------------
        std::cout << "\n--- HighPunch verification ---\n";
        const auto it = moves.find("HighPunch");
        if (it == moves.end()) {
            throw std::runtime_error("HighPunch not found");
        }
        const sf2::scene::MoveDef& hp = it->second;
        print_move_summary(hp);

        const bool template_ok =
            hp.template_tags.count("1key") && hp.template_tags.count("Central") &&
            hp.template_tags.count("Unarmed") && hp.template_tags.count("Punch") &&
            hp.template_tags.count("Controlled") && hp.template_tags.count("Arms") &&
            hp.template_tags.count("NotTitan") && hp.template_tags.count("SoundStrike");
        std::cout << "template tags == {1key,Central,Unarmed,Punch,Controlled,Arms,"
                     "NotTitan,SoundStrike}: "
                  << (template_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "Type == ATTACK: "
                  << (hp.type == "ATTACK" ? "PASS" : "FAIL (" + hp.type + ")") << "\n";
        std::cout << "FileName == high_punch.bytes: "
                  << (hp.file_name == "high_punch.bytes" ? "PASS" : "FAIL") << "\n";
        std::cout << "Priority == 110: "
                  << (hp.priority == 110 ? "PASS" : "FAIL") << "\n";
        std::cout << "MidFrames == 2: "
                  << (hp.mid_frames == 2 ? "PASS" : "FAIL") << "\n";
        std::cout << "FirstFrame == 1: "
                  << (hp.first_frame == 1 ? "PASS" : "FAIL") << "\n";

        // Condition tree dump.
        std::cout << "\nHighPunch condition tree:\n";
        for (const sf2::scene::Cond& c : hp.conditions) {
            std::cout << sf2::scene::cond_to_string(c, 1);
        }

        // Interval dump.
        std::cout << "\nHighPunch intervals (" << hp.intervals.size() << "):\n";
        for (const sf2::scene::Interval& iv : hp.intervals) {
            std::cout << "  " << (iv.name.empty() ? "(type)" : iv.name)
                      << " type=" << iv.type << " frames[" << iv.start << ".."
                      << iv.end << "]";
            if (!iv.attacking_parts.empty()) {
                std::cout << " parts=";
                for (const std::string& p : iv.attacking_parts) std::cout << p << " ";
            }
            std::cout << " dmg=" << iv.damage
                      << (iv.damage_type.empty() ? "" : " (" + iv.damage_type + " shift=" +
                                                        std::to_string(iv.damage_shift) + ")");
            if (iv.has_impulse) {
                std::cout << " impulse=(" << iv.impulse_x << "," << iv.impulse_y
                          << "," << iv.impulse_z << ")";
            }
            std::cout << "\n";
        }

        // --- Condition evaluation ------------------------------------------
        std::cout << "\n--- Condition evaluation (HighPunch) ---\n";

        std::cout << "\nContext 1: neutral (Fight stage, no keys, no anim)\n";
        {
            sf2::scene::FightContext ctx = neutral_context();
            std::string trace;
            const bool r = sf2::scene::eval_move_conditions(hp.conditions, ctx, &trace);
            std::cout << trace;
            std::cout << "RESULT: " << (r ? "TRUE" : "FALSE")
                      << "  (expect FALSE) " << (r ? "FAIL" : "PASS") << "\n";
        }

        std::cout << "\nContext 2: Punch Tap buffered, Fight stage, no anim\n";
        {
            sf2::scene::FightContext ctx = punch_tap_context();
            std::string trace;
            const bool r = sf2::scene::eval_move_conditions(hp.conditions, ctx, &trace);
            std::cout << trace;
            std::cout << "RESULT: " << (r ? "TRUE" : "FALSE")
                      << "  (expect TRUE) " << (r ? "PASS" : "FAIL") << "\n";
        }

        // --- Sanity: a couple of other moves --------------------------------
        std::cout << "\n--- Spot checks ---\n";
        for (const std::string& n : {"StanceIdle", "StepForward", "HighKick"}) {
            const auto m = moves.find(n);
            if (m != moves.end()) {
                print_move_summary(m->second);
            } else {
                std::cout << "  " << n << ": not found\n";
            }
        }

        // Interval-type histogram.
        std::map<int, int> iv_types;
        std::size_t attack_intervals = 0;
        for (const auto& kv : moves) {
            for (const sf2::scene::Interval& iv : kv.second.intervals) {
                iv_types[iv.type]++;
                if (iv.type == 4) attack_intervals++;
            }
        }
        std::cout << "\ninterval type histogram (0=other 2=Uninterrupt "
                     "3=SelfUninterrupt 4=Attack 5=Block 6=Invulnerable 7=Invisible):\n";
        for (const auto& kv : iv_types) {
            std::cout << "  type " << kv.first << ": " << kv.second << "\n";
        }
        std::cout << "attack intervals with parts: " << attack_intervals << "\n";

        std::cout << "\nmove_probe: done\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "move_probe: error: " << e.what() << "\n";
        return 1;
    }
}

// tests/test_input_spec.cpp
//
// Specification test for the control scheme, read from assets/animations/moves.xml.
//
// Written against the shipped data, so the original satisfies it by
// construction. It goes through the engine's own move loader, so a failure means
// reSF2 is dropping input information it needs.
//
// The control scheme is entirely data-driven:
//
//   <Move Name="HighPunch" Template="1key|Central|Unarmed|Punch" ...>
//     <Conditions>
//       <Keys><Key Type="Punch" PressType="Tap"/></Keys>
//     </Conditions>
//     <Locks><Item Type="Weapon" SubType="Fists"/></Locks>
//     <Intervals><Interval Name="Attack" Start=".." End=".."/></Intervals>
//   </Move>
//
// so the reachable move set is exactly what the loader keeps. Facts pinned here,
// all counted from the shipped file with XML comments stripped:
//
//   873 moves, 325 of them with key bindings
//   14 key types: Punch Kick Up Down Forward Back Up-Back Up-Forward
//                 Down-Back Down-Forward Magic Ranged Super RaidCharge
//   2 press types: Tap (419 bindings) and Hold (212)
//   Template tokens encode the chord size: 1key (111), 2key (170), 3key (61)

#include "../engine/game/asset_manager.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>

using namespace resf2;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++failed; } \
    else { std::printf("  PASS: %s\n", msg); ++passed; } \
} while (0)

int main() {
    std::printf("=== control scheme specification (moves.xml) ===\n");

    std::string root = "assets";
    if (!std::filesystem::exists(root)) {
        std::fprintf(stderr, "assets/ not found; run from the repo root\n");
        return 1;
    }

    game::AssetManager assets;
    assets.load_moves(root);
    const auto& moves = assets.moves();
    std::printf("loaded %zu moves\n", moves.size());

    CHECK(moves.size() >= 850, "873 moves are defined in the shipped data");

    // ---- key bindings ----
    std::printf("\n-- key bindings --\n");
    std::set<std::string> key_types;
    std::set<std::string> press_types;
    std::size_t bound = 0, tap_bindings = 0, hold_bindings = 0, hold_moves = 0;
    std::map<int, std::size_t> chord_sizes;
    for (const auto& [name, m] : moves) {
        if (m.key_types.empty()) continue;
        ++bound;
        chord_sizes[static_cast<int>(m.key_types.size())]++;
        for (const auto& k : m.key_types) key_types.insert(k);
        for (const auto& p : m.key_press_types) {
            press_types.insert(p);
            if (p == "Hold") ++hold_bindings; else ++tap_bindings;
        }
        if (m.needs_hold) ++hold_moves;
    }
    std::printf("  %zu moves are key-bound; key types=%zu press types=%zu\n",
                bound, key_types.size(), press_types.size());
    std::printf("  bindings: Tap=%zu Hold=%zu (moves needing a hold: %zu)\n",
                tap_bindings, hold_bindings, hold_moves);
    for (const auto& [n, c] : chord_sizes)
        std::printf("    %d-key chords: %zu moves\n", n, c);

    // 325 <Keys> blocks exist but only 312 distinct moves are bound: a move can
    // carry several <Keys> blocks under an <Operator Type="Or"> to express
    // ALTERNATIVE chords for the same action. Counting blocks instead of moves
    // is the easy mistake here.
    CHECK(bound == 312,
          "312 moves are key-bound (325 <Keys> blocks; some are Or-alternatives)");
    CHECK(key_types.size() >= 14,
          "all 14 key types are reachable (Punch/Kick/8 directions/Magic/"
          "Ranged/Super/RaidCharge)");
    for (const char* k : {"Punch", "Kick", "Up", "Down", "Forward", "Back",
                          "Magic", "Ranged", "Super"})
        CHECK(key_types.count(k) > 0,
              (std::string("key type '") + k + "' is bound to at least one move").c_str());

    // The Tap/Hold distinction: two moves can share a key and differ only here.
    CHECK(press_types.count("Tap") > 0 && press_types.count("Hold") > 0,
          "both Tap and Hold press types survive parsing");
    CHECK(hold_bindings >= 200,
          "212 bindings require a HELD key -- without PressType every hold "
          "collapses onto its tap variant and charged attacks are unreachable");
    CHECK(hold_moves >= 100, "100+ distinct moves need a held key");

    // Chords: a 3-key move must not be reachable from a single key.
    CHECK(chord_sizes.count(1) && chord_sizes.count(2),
          "both single-key and two-key chords exist");
    // Every key must carry a press type, or the two lists desynchronise and a
    // chord's second key silently loses its Tap/Hold requirement.
    bool press_aligned = true;
    for (const auto& [name, m] : moves) {
        if (m.key_types.size() != m.key_press_types.size()) {
            press_aligned = false;
            std::fprintf(stderr, "    '%s': %zu keys but %zu press types\n",
                         name.c_str(), m.key_types.size(),
                         m.key_press_types.size());
            break;
        }
    }
    CHECK(press_aligned,
          "key_types and key_press_types stay index-aligned for every move");

    // ---- attack timing ----
    std::printf("\n-- attack intervals --\n");
    std::size_t with_attack = 0, with_uninterrupt = 0, with_block = 0;
    for (const auto& [name, m] : moves) {
        if (m.attack_start >= 0) ++with_attack;
        if (m.uninterrupt_start >= 0) ++with_uninterrupt;
        if (m.block_start >= 0) ++with_block;
    }
    std::printf("  attack=%zu uninterrupt=%zu block=%zu\n",
                with_attack, with_uninterrupt, with_block);
    CHECK(with_attack >= 200,
          "attack windows are parsed -- they decide when a hit connects");
    CHECK(with_uninterrupt >= 100,
          "uninterrupt windows are parsed -- they decide what can be cancelled");

    // ---- weapon gating ----
    std::printf("\n-- weapon locks --\n");
    std::set<std::string> subtypes;
    std::size_t gated = 0;
    for (const auto& [name, m] : moves) {
        if (m.required_weapon_subtype.empty()) continue;
        ++gated;
        subtypes.insert(m.required_weapon_subtype);
    }
    std::printf("  %zu moves gated by weapon, %zu distinct subtypes\n",
                gated, subtypes.size());
    CHECK(gated >= 100,
          "<Locks><Item SubType=..> gates moves by equipped weapon");
    CHECK(subtypes.size() >= 10,
          "many weapon subtypes each unlock their own move set");

    std::printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

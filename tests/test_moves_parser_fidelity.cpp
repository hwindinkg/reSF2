// tests/test_moves_parser_fidelity.cpp
//
// Soak-fix Wave 7a (SOAK_TRIAGE.md P10): the moves.xml parser must apply
// each move's authored attributes, not just an animation. The reference is
// the REAL device file reverse/data/animations/moves.xml — captured from the
// game (Redmi 6A, commit 379c387, reverse/analysis/LIVE_GAME_EVIDENCE.md Q3)
// — parsed through the ENGINE'S OWN loader (AssetManager::parse_moves_xml),
// then compared field-by-field against the verbatim device values:
//
//   HighPunch:    window Start=4/End=5, MidFrames=2, FirstFrame=1, key
//                 Punch/Tap, Damage 0.11, UnarmedDamage Shift=-10,
//                 Impulse X=245, Hit High, edges EForearm_2/EHand_2/EFingers_2
//   LowPunch:     2key|Down|Unarmed|Punch, Start=6/End=8, FirstFrame=3,
//                 Damage 0.16, UnarmedDamage Shift=-10, Impulse X=245,
//                 keys Punch/Tap + Down/Hold
//   ForwardRoll:  1key|DownForward|NotTitan, Type MOVE, MidFrames=2,
//                 FirstFrame=2, key Down-Forward/Tap
//
// RED on HEAD (2026-08-02): the nested <Damage Type="UnarmedDamage"
// Shift="-10"/> is dropped (no damage_attr/damage_attr_shift on MoveDef), so
// the unarmed-damage attribute shift never reaches the damage formula.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

#include "../engine/game/asset_manager.hpp"
#include "../engine/format/xml_doc.hpp"

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %lld, expected %lld\n", \
                     __LINE__, msg, (long long)(a), (long long)(b)); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while (0)

#define CHECK_STREQ(a, b, msg) do { \
    std::string _sa((a)), _sb((b)); \
    if (_sa != _sb) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got '%s', expected '%s'\n", \
                     __LINE__, msg, _sa.c_str(), _sb.c_str()); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while (0)

static std::string load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return data;
}

static bool has_edge(const resf2::game::MoveDef& m, const std::string& edge) {
    for (const auto& e : m.attack_edges)
        if (e == edge) return true;
    for (const auto& e : m.attacking_parts)
        if (e == edge) return true;
    return false;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "reverse/data/animations/moves.xml";
    const std::string xml = load_file(path);
    if (xml.empty()) {
        std::fprintf(stderr, "FAILED to read %s\n", path);
        return 1;
    }

    resf2::game::AssetManager assets;
    assets.parse_moves_xml(xml);  // the engine's own moves.xml loader
    const auto& moves = assets.moves();

    std::fprintf(stderr, "  [P10] parsed %zu moves from %s\n", moves.size(), path);
    CHECK(moves.size() > 800, "P10: the real device moves.xml yields the full move table");

    // ---- HighPunch (LIVE_GAME_EVIDENCE.md Q3-B, verbatim) ----
    {
        auto it = moves.find("HighPunch");
        CHECK(it != moves.end(), "P10: HighPunch found");
        if (it == moves.end()) return tests_failed ? 1 : 0;
        const auto& m = it->second;
        CHECK_EQ(m.mid_frames, 2, "P10: HighPunch MidFrames=2");
        CHECK_EQ(m.first_frame, 1, "P10: HighPunch FirstFrame=1");
        CHECK_EQ(m.attack_start, 4, "P10: HighPunch attack Start=4");
        CHECK_EQ(m.attack_end, 5, "P10: HighPunch attack End=5");
        CHECK(m.damage == 0.11f, "P10: HighPunch Damage=0.11");
        CHECK(m.impulse_x == 245.0f, "P10: HighPunch Impulse X=245");
        CHECK_STREQ(m.damage_attr, "UnarmedDamage", "P10: HighPunch Damage Type=UnarmedDamage");
        CHECK_EQ(m.damage_attr_shift, -10, "P10: HighPunch UnarmedDamage Shift=-10");
        CHECK(has_edge(m, "EForearm_2") && has_edge(m, "EHand_2") && has_edge(m, "EFingers_2"),
              "P10: HighPunch AttackingParts = EForearm_2/EHand_2/EFingers_2");
        CHECK(!m.key_types.empty() && m.key_types[0] == "Punch" &&
              !m.key_press_types.empty() && m.key_press_types[0] == "Tap",
              "P10: HighPunch key Punch/Tap");
        CHECK(m.is_attack && m.is_unarmed, "P10: HighPunch Type=ATTACK, template Unarmed");
    }

    // ---- LowPunch (device file, verbatim excerpt) ----
    {
        auto it = moves.find("LowPunch");
        CHECK(it != moves.end(), "P10: LowPunch found");
        if (it == moves.end()) return tests_failed ? 1 : 0;
        const auto& m = it->second;
        CHECK_EQ(m.mid_frames, 2, "P10: LowPunch MidFrames=2");
        CHECK_EQ(m.first_frame, 3, "P10: LowPunch FirstFrame=3");
        CHECK_EQ(m.attack_start, 6, "P10: LowPunch attack Start=6");
        CHECK_EQ(m.attack_end, 8, "P10: LowPunch attack End=8");
        CHECK(m.damage == 0.16f, "P10: LowPunch Damage=0.16");
        CHECK(m.impulse_x == 245.0f, "P10: LowPunch Impulse X=245");
        CHECK_STREQ(m.damage_attr, "UnarmedDamage", "P10: LowPunch Damage Type=UnarmedDamage");
        CHECK_EQ(m.damage_attr_shift, -10, "P10: LowPunch UnarmedDamage Shift=-10");
        CHECK(has_edge(m, "EForearm_1") && has_edge(m, "EHand_1") && has_edge(m, "EFingers_1"),
              "P10: LowPunch AttackingParts = EForearm_1/EHand_1/EFingers_1");
        CHECK(m.key_types.size() >= 2 &&
              (m.key_types[0] == "Punch" || m.key_types[1] == "Punch"),
              "P10: LowPunch binds Punch + Down");
        CHECK(m.key_press_types.size() >= 2 &&
              std::find(m.key_press_types.begin(), m.key_press_types.end(), "Hold") != m.key_press_types.end(),
              "P10: LowPunch Down key PressType=Hold");
        CHECK_EQ(m.key_count, 2, "P10: LowPunch template is 2key");
    }

    // ---- ForwardRoll (Type MOVE, no damage attribute) ----
    {
        auto it = moves.find("ForwardRoll");
        CHECK(it != moves.end(), "P10: ForwardRoll found");
        if (it == moves.end()) return tests_failed ? 1 : 0;
        const auto& m = it->second;
        CHECK_EQ(m.mid_frames, 2, "P10: ForwardRoll MidFrames=2");
        CHECK_EQ(m.first_frame, 2, "P10: ForwardRoll FirstFrame=2");
        CHECK_STREQ(m.direction, "DownForward", "P10: ForwardRoll direction DownForward");
        CHECK(!m.is_attack, "P10: ForwardRoll Type=MOVE (not an attack)");
        CHECK(m.is_not_titan, "P10: ForwardRoll template carries NotTitan");
        CHECK(!m.key_types.empty() && m.key_types[0] == "Down-Forward",
              "P10: ForwardRoll key Down-Forward");
    }

    std::printf("\n=== P10 moves.xml parser fidelity: %d passed, %d failed ===\n",
                tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}

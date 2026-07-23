#include "../engine/fight/moves.hpp"
#include "../engine/format/xml_doc.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>

// Load entire file into string
static std::string load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return data;
}

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while(0)

// CHECK_EQ for integers only
#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %lld, expected %lld\n", \
                     __LINE__, msg, (long long)(a), (long long)(b)); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while(0)

// CHECK_EQ_F for float
#define CHECK_EQ_F(a, b, msg) do { \
    float _va = (float)(a); float _vb = (float)(b); \
    if (_va != _vb) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %f, expected %f\n", \
                     __LINE__, msg, _va, _vb); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while(0)

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
} while(0)

int main(int argc, char* argv[]) {
    const char* path = argc > 1 ? argv[1] : "assets/animations/moves.xml";
    std::string xml_content = load_file(path);
    
    if (xml_content.empty()) {
        std::fprintf(stderr, "FAILED to read %s\n", path);
        return 1;
    }
    
    // ===== Test 1: Parse via XmlDocument directly =====
    std::printf("\n=== Test 1: XmlDocument direct parse ===\n");
    {
        resf2::format::XmlDocument doc;
        CHECK(doc.parse(xml_content), "Parse moves.xml with XmlDocument");
        
        const auto* root = doc.root();
        CHECK(root != nullptr, "Root node exists");
        
        const auto* movesxml = root->first_child("Movesxml");
        CHECK(movesxml != nullptr, "Movesxml node exists");
        
        const auto* moves_node = movesxml->first_child("Moves");
        CHECK(moves_node != nullptr, "Moves node exists");
        
        auto move_children = moves_node->find_all("Move");
        CHECK(!move_children.empty(), "At least one Move element found");
        std::printf("  Found %zu Move elements\n", move_children.size());
    }
    
    // ===== Test 2: Load via MoveDatabase =====
    std::printf("\n=== Test 2: MoveDatabase load ===\n");
    resf2::fight::MoveDatabase db;
    CHECK(db.load_from_xml(xml_content), "MoveDatabase loads from XML");
    CHECK(db.size() > 800, "More than 800 moves loaded");
    std::printf("  Loaded %zu moves\n", db.size());
    
    // ===== Test 3: StanceIdle (basic move) =====
    std::printf("\n=== Test 3: StanceIdle verification ===\n");
    {
        const auto* m = db.find("StanceIdle");
        CHECK(m != nullptr, "StanceIdle found");
        if (m) {
            CHECK_STREQ(m->filename, "stance_idle.bin", "StanceIdle filename");
            CHECK_STREQ(m->template_name, "IdleStance", "StanceIdle template");
            CHECK_EQ(m->mid_frames, 2, "StanceIdle mid_frames == 2");
        }
    }
    
    // ===== Test 4: LowPunch (attack move with sounds) =====
    std::printf("\n=== Test 4: LowPunch verification ===\n");
    {
        const auto* m = db.find("LowPunch");
        CHECK(m != nullptr, "LowPunch found");
        if (m) {
            CHECK_STREQ(m->filename, "low_punch.bin", "LowPunch filename");
            CHECK_STREQ(m->direction, "Down", "LowPunch direction == Down");
            CHECK_STREQ(m->move_type, "Punch", "LowPunch type == Punch");
            CHECK_EQ(m->key_count, 2, "LowPunch key_count == 2");
            CHECK_STREQ(m->tactic_weapon, "Fists", "LowPunch tactic_weapon");
            CHECK_EQ(m->mid_frames, 2, "LowPunch mid_frames == 2");
            
            // Attack intervals
            CHECK(!m->attack_intervals.empty(), "LowPunch has attack intervals");
            if (!m->attack_intervals.empty()) {
                const auto& ai = m->attack_intervals[0];
                CHECK_EQ_F(ai.start, 6.0f, "LowPunch attack start == 6");
                CHECK_EQ_F(ai.end, 8.0f, "LowPunch attack end == 8");
                CHECK_STREQ(ai.hit_type, "Low", "LowPunch hit type == Low");
                CHECK_EQ_F(ai.impulse.x, 245.0f, "LowPunch impulse X == 245");
            }
            
            // Sound events -- LowPunch has 3 <Sound> in <Actions>
            CHECK_EQ((int)m->sound_events.size(), 3, "LowPunch has 3 sound events");
            if (m->sound_events.size() >= 3) {
                CHECK_STREQ(m->sound_events[0].sound, "m_pl_attack1", "Sound 1 name");
                CHECK_EQ_F(m->sound_events[0].time, 3.0f, "Sound 1 frame == 3");
                CHECK_STREQ(m->sound_events[1].sound, "f_pl_attack1", "Sound 2 name");
                CHECK_EQ_F(m->sound_events[1].time, 3.0f, "Sound 2 frame == 3");
                CHECK_STREQ(m->sound_events[2].sound, "swish3", "Sound 3 name");
                CHECK_EQ_F(m->sound_events[2].time, 5.0f, "Sound 3 frame == 5");
            }
        }
    }
    
    // ===== Test 5: ElbowStrike (has Perk lock) =====
    std::printf("\n=== Test 5: ElbowStrike verification ===\n");
    {
        const auto* m = db.find("ElbowStrike");
        CHECK(m != nullptr, "ElbowStrike found");
        if (m) {
            CHECK_EQ(m->mid_frames, 2, "ElbowStrike mid_frames == 2");
            CHECK_STREQ(m->lock_perk, "PERK_ELBOW_STRIKE", "ElbowStrike requires PERK_ELBOW_STRIKE");
            CHECK_STREQ(m->direction, "DownBack", "ElbowStrike direction == DownBack");
        }
    }
    
    // ===== Test 6: Query Central 1key Punch =====
    std::printf("\n=== Test 6: Query Central 1key Punches ===\n");
    {
        resf2::fight::MoveDatabase::MoveQuery q;
        q.direction = "Central";
        q.move_type = "Punch";
        q.key_count = 1;
        auto results = db.query(q);
        CHECK(!results.empty(), "At least one Central 1key Punch found");
        std::printf("  Found %zu results\n", results.size());
    }
    
    // ===== Test 7: Invulnerable intervals =====
    std::printf("\n=== Test 7: Invulnerable intervals ===\n");
    {
        // FistsHit (or similar hit animation) should have Invulnerable intervals
        // Search for a move with invulnerable_intervals
        bool found_invuln = false;
        for (auto& [name, move] : db.all_moves()) {
            if (!move.invulnerable_intervals.empty()) {
                found_invuln = true;
                std::printf("  Move '%s' has %zu Invulnerable interval(s):\n",
                            name.c_str(), move.invulnerable_intervals.size());
                for (auto& invi : move.invulnerable_intervals) {
                    std::printf("    Name='%s' Start=%.0f End=%.0f\n",
                                invi.name.c_str(), invi.start, invi.end);
                }
                CHECK(!move.invulnerable_intervals.empty(),
                      "Found move with Invulnerable intervals");
                // Check structure
                const auto& first = move.invulnerable_intervals[0];
                CHECK(first.end > 0 || first.name == "Boss",
                      "Invulnerable interval has valid End or name 'Boss'");
                break;
            }
        }
        CHECK(found_invuln, "At least one move has Invulnerable intervals");
    }
    
    // ===== Test 8: Distance conditions =====
    std::printf("\n=== Test 8: Distance conditions ===\n");
    {
        bool found_distance = false;
        for (auto& [name, move] : db.all_moves()) {
            if (move.distance.active) {
                found_distance = true;
                std::printf("  Move '%s' has Distance: min=%.0f max=%.0f\n",
                            name.c_str(), move.distance.min_dist, move.distance.max_dist);
                CHECK(move.distance.active, "Distance condition is active");
                CHECK(move.distance.min_dist >= 0, "Distance min >= 0");
                CHECK(move.distance.max_dist > 0, "Distance max > 0");
                break;
            }
        }
        CHECK(found_distance, "At least one move has Distance conditions");
    }
    
    // ===== Test 9: Uninterrupt intervals (existing) =====
    std::printf("\n=== Test 9: Uninterrupt intervals ===\n");
    {
        bool found_uninterrupt = false;
        for (auto& [name, move] : db.all_moves()) {
            if (!move.uninterrupt_intervals.empty()) {
                found_uninterrupt = true;
                std::printf("  Move '%s' has %zu Uninterrupt interval(s)\n",
                            name.c_str(), move.uninterrupt_intervals.size());
                break;
            }
        }
        CHECK(found_uninterrupt, "At least one move has Uninterrupt intervals");
    }
    
    // ===== Test 10: TacticWeapon field =====
    std::printf("\n=== Test 10: TacticWeapon field ===\n");
    {
        bool found_weapon = false;
        for (auto& [name, move] : db.all_moves()) {
            if (!move.tactic_weapon.empty()) {
                found_weapon = true;
                std::printf("  Move '%s' has tactic_weapon='%s'\n",
                            name.c_str(), move.tactic_weapon.c_str());
                break;
            }
        }
        CHECK(found_weapon, "At least one move has tactic_weapon set");
    }
    
    // ===== Summary =====
    std::printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

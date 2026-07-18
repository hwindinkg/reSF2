#include "../engine/fight/moves.hpp"
#include <cstdio>

int main(int argc, char* argv[]) {
    const char* path = argc > 1 ? argv[1] : "assets/animations/moves.xml";

    resf2::fight::MoveDatabase db;
    if (!db.load_from_file(path)) {
        std::fprintf(stderr, "FAILED to load moves from %s\n", path);
        return 1;
    }

    std::printf("[moves] Loaded %zu moves\n", db.size());

    // Count by type
    int punch_count = 0, kick_count = 0, block_count = 0;
    int jump_count = 0, step_count = 0, stance_count = 0;
    int combo_count = 0;
    std::string prev_move;

    for (auto& [n, m] : db.all_moves()) {
        if (m.move_type == "Punch") punch_count++;
        else if (m.move_type == "Kick") kick_count++;
        else if (m.move_type == "Block") block_count++;
        else if (m.move_type == "Jump") jump_count++;
        else if (m.move_type == "Step" || m.move_type == "DoubleStep") step_count++;
        else if (m.move_type.find("Stance") != std::string::npos) stance_count++;
        if (m.key_count == 3) combo_count++;
    }

    std::printf("  Punches: %d, Kicks: %d, Blocks: %d\n", punch_count, kick_count, block_count);
    std::printf("  Jumps: %d, Steps: %d, Stances: %d\n", jump_count, step_count, stance_count);
    std::printf("  3-key combos: %d\n", combo_count);

    // List some specific moves
    auto* stance = db.find("StanceIdle");
    if (stance) {
        std::printf("\nStanceIdle: file=%s, templ=%s, dir=%s, type=%s, weapon=%s\n",
                    stance->filename.c_str(), stance->template_name.c_str(),
                    stance->direction.c_str(), stance->move_type.c_str(),
                    stance->lock_weapon.c_str());
    }

    auto* punch = db.find("LowPunch");
    if (punch) {
        std::printf("LowPunch: file=%s, templ=%s, dir=%s, type=%s, kc=%d\n",
                    punch->filename.c_str(), punch->template_name.c_str(),
                    punch->direction.c_str(), punch->move_type.c_str(),
                    punch->key_count);
        std::printf("  Intervals: %zu, Uninterrupts: %zu\n",
                    punch->attack_intervals.size(), punch->uninterrupt_intervals.size());
        for (auto& iv : punch->attack_intervals) {
            std::printf("  Attack [%.0f-%.0f] dmg=%d impulse=(%.0f,%.0f) hit='%s'\n",
                        iv.start, iv.end, iv.damage, iv.impulse.x, iv.impulse.y,
                        iv.hit_type.c_str());
        }
    }

    // Sample a combo
    auto* combo = db.find("DoublePunch");
    if (combo) {
        std::printf("\nDoublePunch: file=%s, dir=%s, kc=%d, req_anim=%s\n",
                    combo->filename.c_str(), combo->direction.c_str(),
                    combo->key_count, combo->required_current_animation.c_str());
    }

    // Query: find all 1key Punch moves with direction="Central"
    resf2::fight::MoveDatabase::MoveQuery q;
    q.direction = "Central";
    q.move_type = "Punch";
    q.key_count = 1;
    auto results = db.query(q);
    std::printf("\nCentral 1key Punches: %zu\n", results.size());
    for (auto* m : results) {
        std::printf("  %s (file=%s, weapon=%s)\n",
                    m->name.c_str(), m->filename.c_str(), m->lock_weapon.c_str());
    }

    return 0;
}

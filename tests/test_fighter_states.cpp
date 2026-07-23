// tests/test_fighter_states.cpp
//
// Tests for the Fighter state machine and damage/invulnerability system.
// Covers: damage application, Invulnerable state, AI decisions, bag_hit_ removal.

#include "../engine/fight/fighter.hpp"
#include "../engine/fight/ai.hpp"
#include "../engine/fight/moves.hpp"
#include "../engine/core/math.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>

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

#define CHECK_EQ(a, b, msg) do { \
    auto _va = (a); auto _vb = (b); \
    if (_va != _vb) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %lld, expected %lld\n", \
                     __LINE__, msg, (long long)_va, (long long)_vb); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while(0)

#define CHECK_NEAR(a, b, eps, msg) do { \
    float _va = (float)(a); float _vb = (float)(b); float _eps = (float)(eps); \
    if (std::abs(_va - _vb) > _eps) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %f, expected ~%f (eps=%f)\n", \
                     __LINE__, msg, _va, _vb, _eps); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while(0)

int main() {
    using namespace resf2::fight;
    using resf2::core::Vec2;

    // ===== Test 1: Fighter default state =====
    std::printf("\n=== Test 1: Fighter default state ===\n");
    {
        Fighter f;
        CHECK(f.state() == Fighter::State::Idle, "Fighter starts in Idle state");
        CHECK_EQ(f.health(), 100.0f, "Fighter starts with 100 health");
        CHECK_EQ(f.energy(), 0.0f, "Fighter starts with 0 energy");
        CHECK_EQ(f.combo_count(), 0, "Fighter starts with combo count 0");
    }

    // ===== Test 2: Fighter takes damage =====
    std::printf("\n=== Test 2: Fighter takes damage ===\n");
    {
        Fighter::Config cfg;
        cfg.health = 100.0f;
        cfg.max_health = 100.0f;
        cfg.name = "TestFighter";
        Fighter f(cfg);
        
        f.take_damage(25.0f, Vec2{100, 0});
        CHECK_EQ(f.health(), 75.0f, "Health decreases by 25 after take_damage(25)");
        CHECK(f.state() == Fighter::State::Hit, "Fighter enters Hit state after taking damage");
        CHECK_EQ(f.energy(), 12.5f, "Energy gained: damage * 0.5 = 12.5");
        CHECK_EQ(f.combo_count(), 1, "Combo count increments after hit");
    }

    // ===== Test 3: Fighter dies from lethal damage =====
    std::printf("\n=== Test 3: Fighter dies from lethal damage ===\n");
    {
        Fighter::Config cfg;
        cfg.health = 50.0f;
        Fighter f(cfg);
        
        f.take_damage(100.0f, Vec2{});
        CHECK_EQ(f.health(), 0.0f, "Health clamped to 0 on lethal damage");
        CHECK(f.state() == Fighter::State::Dead, "Fighter enters Dead state on lethal damage");
    }

    // ===== Test 4: Fighter exits Hit state after stun timer expires =====
    std::printf("\n=== Test 4: Fighter exits Hit state ===\n");
    {
        Fighter::Config cfg;
        cfg.health = 100.0f;
        Fighter f(cfg);
        
        f.take_damage(10.0f, Vec2{});
        CHECK(f.state() == Fighter::State::Hit, "Fighter in Hit state after damage");
        
        // Update past the hit stun timer (300ms)
        f.update(0.35f);
        CHECK(f.state() == Fighter::State::Idle, "Fighter returns to Idle after hit stun expires");
    }

    // ===== Test 5: Fighter invulnerability (is_invulnerable) =====
    std::printf("\n=== Test 5: Fighter invulnerability ===\n");
    {
        // Without a MoveDef with invulnerable intervals, is_invulnerable should return false
        Fighter f;
        CHECK(!f.is_invulnerable(), "Fighter is not invulnerable by default (no active animation)");
    }

    // ===== Test 6: AIController exists and can be configured =====
    std::printf("\n=== Test 6: AIController configuration ===\n");
    {
        AIController ai;
        CHECK(!ai.has_tactic_data(), "AI starts without tactic data");
        
        AIController::Settings s;
        s.aggression = 0.8f;
        s.attack_chance = 0.5f;
        s.block_chance = 0.2f;
        ai.set_settings(s);
        
        CHECK_EQ(ai.settings.aggression, 0.8f, "AI aggression set correctly");
        CHECK(ai.last_decision() == 8, "AI last_decision starts as Central (8)");
    }

    // ===== Test 7: Fighter config is mutable =====
    std::printf("\n=== Test 7: Fighter config ===\n");
    {
        Fighter::Config cfg;
        cfg.position = Vec2{100, 200};
        cfg.facing_right = true;
        cfg.speed = 150.0f;
        Fighter f(cfg);
        
        CHECK_NEAR(f.position().x, 100.0f, 0.01f, "Fighter position.x from config");
        CHECK_NEAR(f.position().y, 200.0f, 0.01f, "Fighter position.y from config");
        CHECK(f.facing_right(), "Fighter facing_right from config");
        
        f.set_position(Vec2{300, 400});
        CHECK_NEAR(f.position().x, 300.0f, 0.01f, "Fighter position updated via set_position");
        
        f.set_facing(false);
        CHECK(!f.facing_right(), "Fighter facing updated via set_facing");
    }

    // ===== Test 8: Fighter reset restores state =====
    std::printf("\n=== Test 8: Fighter reset ===\n");
    {
        Fighter::Config cfg;
        cfg.health = 100.0f;
        Fighter f(cfg);
        
        f.take_damage(50.0f, Vec2{});
        CHECK_EQ(f.health(), 50.0f, "Health reduced after damage");
        
        f.reset();
        CHECK_EQ(f.health(), 100.0f, "Health restored to max_health after reset");
        CHECK_EQ(f.combo_count(), 0, "Combo count reset to 0");
        CHECK(f.state() == Fighter::State::Idle, "State reset to Idle after reset");
    }

    // ===== Test 9: MoveDatabase loads with Invulnerable intervals =====
    std::printf("\n=== Test 9: MoveDatabase Invulnerable intervals ===\n");
    {
        // This test requires moves.xml. If unavailable, skip.
        MoveDatabase db;
        bool loaded = db.load_from_file("assets/animations/moves.xml");
        if (loaded) {
            bool found_invuln = false;
            for (auto& [name, move] : db.all_moves()) {
                if (!move.invulnerable_intervals.empty()) {
                    found_invuln = true;
                    std::printf("  Move '%s' has %zu Invulnerable interval(s)\n",
                                name.c_str(), move.invulnerable_intervals.size());
                    break;
                }
            }
            CHECK(found_invuln, "At least one move has Invulnerable intervals");
        } else {
            std::printf("  SKIP: moves.xml not available\n");
        }
    }

    // ===== Summary =====
    std::printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

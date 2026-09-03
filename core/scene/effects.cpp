// Hit-spark particles — see effects.hpp for the JS refs and the RNG note.

#include "scene/effects.hpp"

#include <cmath>

namespace sf2::scene {

// Spark tuning (world units / 60 Hz frames). The specs give the JS names
// (`Hyb` sprite 1306, `av` texture 260) but not the numeric configs; these
// constants approximate the original's look: a short-lived fan of sparks.
namespace {

constexpr int kSparksMin = 8;        // sparks per burst (spec: 8-14)
constexpr int kSparksMax = 14;
constexpr float kSpeedMin = 2.0f;     // initial speed (world units/frame)
constexpr float kSpeedMax = 6.0f;
constexpr float kSpreadRad = 0.9f;   // fan half-angle (~52°) around the
                                      // blow-away direction
constexpr float kGravity = 0.35f;     // world units/frame² (down = +y)
constexpr float kLifeMin = 20.0f;     // frames (spec: ~20-30)
constexpr float kLifeMax = 30.0f;
constexpr float kSizeMin = 3.0f;      // spark size (world units)
constexpr float kSizeMax = 7.0f;

}  // namespace

float EffectSystem::next01() {
    // A 32-bit LCG (Numerical Recipes) — deterministic, private to the
    // effect layer; never touches the fight's shared roll01.
    lcg_ = 1664525u * lcg_ + 1013904223u;
    return static_cast<float>(lcg_ >> 8) * (1.0f / 16777216.0f);
}

void EffectSystem::spawn_hit_sparks(float x, float y, int facing) {
    // The fan blows AWAY from the attacker: facing +1 (attacker looks
    // right) -> sparks fly right (+x); facing -1 -> sparks fly left.
    const float dir = static_cast<float>(facing >= 0 ? 1 : -1);
    const int count = kSparksMin +
                     static_cast<int>(next01() * static_cast<float>(kSparksMax - kSparksMin + 1));
    live_.reserve(live_.size() + static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float speed = kSpeedMin + next01() * (kSpeedMax - kSpeedMin);
        const float angle = (next01() * 2.0f - 1.0f) * kSpreadRad;
        particle p;
        p.x = x;
        p.y = y;
        p.vx = dir * speed * std::cos(angle);
        // The vertical spread is symmetric around the hit point (sparks
        // fly up and down from the contact); gravity pulls them down.
        p.vy = speed * std::sin(angle) * 0.6f;
        p.life = kLifeMin + next01() * (kLifeMax - kLifeMin);
        p.age = 0.0f;
        p.size = kSizeMin + next01() * (kSizeMax - kSizeMin);
        // Warm spark palette (the original's sparks are white-yellow).
        const float t = next01();
        p.color = t < 0.5f   ? 0xFFFFEE66u   // warm yellow
                : t < 0.8f ? 0xFFFFFFCCu   // white-hot
                           : 0xFFAA6622u;  // ember orange
        live_.push_back(p);
    }
}

void EffectSystem::update() {
    // Advance + compact in place (dead particles drop out; the order of
    // the survivors is preserved).
    std::size_t w = 0;
    for (std::size_t i = 0; i < live_.size(); ++i) {
        particle& p = live_[i];
        p.age += 1.0f;
        if (p.age >= p.life) continue;  // dead — dropped
        p.vy += kGravity;               // gravity (down = world +y)
        p.x += p.vx;
        p.y += p.vy;
        live_[w++] = p;
    }
    live_.resize(w);
}

}  // namespace sf2::scene

#pragma once

// Hit-spark particles (the fight's VISUAL effects layer — no simulation
// impact). Ported conceptually from the game's spark effects:
//   - JS_GAMEPLAY.md §7 «Звук/эффекты»: the hit sparks `Hyb` (sprite 1306)
//     + the particle systems `ryb`/`av`;
//   - JS_RENDER.md §5 «Эффекты»: the sparks `av` (texture 260).
// The exact JS particle configs are not in the specs; this is the demo's
// deterministic approximation: a fan of 8-14 sparks from the hit point,
// blown away from the attacker, gravity down, ~20-30 frame fade.
//
// IMPORTANT: the RNG here is a PRIVATE deterministic LCG — it must never
// consume the fight's shared roll01 (that would perturb the AI decisions
// and diverge the pose dump from the oracle).

#include <cstdint>
#include <vector>

namespace sf2::scene {

// One spark particle (world space; y is up-negative, same as the pose
// bones — gravity pulls toward +y).
struct particle {
    float x = 0.0f;             // world position
    float y = 0.0f;
    float vx = 0.0f;            // world units per 60 Hz frame
    float vy = 0.0f;
    float life = 0.0f;          // total frames alive
    float age = 0.0f;           // frames lived so far (age >= life = dead)
    float size = 0.0f;          // world units (the renderer fades by age/life)
    std::uint32_t color = 0xFFFFFFFFu;  // 0xRRGGBB
};

// The particle pool: a compacting vector (dead particles are dropped each
// update; the capacity is retained across bursts, so a burst never
// reallocates after the first few hits).
class EffectSystem {
public:
    // Spawns 8-14 hit sparks fanned AWAY from the attacker (the direction
    // `facing`, ±1) from the hit point (x, y) — the spark burst at the
    // contact point (JS `Hyb`/`ryb`).
    void spawn_hit_sparks(float x, float y, int facing);

    // Advances every particle one 60 Hz frame: gravity pulls down (world
    // +y), age increments; dead particles (age >= life) are removed.
    void update();

    // The live particles (for rendering; fade each by age/life).
    const std::vector<particle>& particles() const { return live_; }

    // True while no particle is alive.
    bool empty() const { return live_.empty(); }

    // Resets the pool (between battles).
    void clear() { live_.clear(); }

private:
    // A private deterministic LCG in [0,1) — NOT the fight's shared roll01
    // (see the file comment).
    float next01();

    std::vector<particle> live_;
    std::uint32_t lcg_ = 0x853C49E7u;  // fixed seed — reproducible bursts
};

}  // namespace sf2::scene

#pragma once

// Hit-spark particles (the fight's VISUAL effects layer — no simulation
// impact). Faithful port of the JS spark subsystem `av` (L833-834) spawned by
// `Ut.ryb` (L824) and triggered through `ql.Rub`/`Ut.Bob` (L369/L368).
//
// JS cites (sf2.502f0946.js, verified this session):
//   - `av` ctor (L833): `fg.x = a.x/200 + oa.eT(-40,40)/10`,
//     `fg.y = a.y/200 + oa.eT(-60,20)/10`; sprite = `E.get(260)` (the
//     `ui/misc` atlas — manifest L2490 token 260) frame `y.xRa` =
//     `"drop_blood"` (L2465; sourceSize 26x26, ui/misc.json).
//   - `av.ia` (L833): `Y.x += fg.x; Y.y += fg.y; fg.y += .2;`
//     `Y.Wg(atan2(fg.y,fg.x)*57.29577951308232*(fg.x<0?-1:1))`.
//   - `Ut.ryb` (L824): clears the previous burst (`dKa()`), spawns `count`
//     particles with `la(.3)`, then resets the pool clock (`uba=0`).
//   - `Ut.Cnb` (L824): steps every particle; clears the WHOLE pool once
//     `uba>90` (so a burst lives ~90 frames, not a per-particle lifetime).
//   - `ql.Rub` (L369): `count` defaults to `4`.
//
// IMPORTANT: the RNG here is a PRIVATE deterministic LCG — it must never
// consume the fight's shared roll01 (that would perturb the AI decisions
// and diverge the pose dump from the oracle). JS uses the wall-clock RNG
// (`oa.eT`); the native keeps determinism deliberately.

#include <cstdint>
#include <vector>

namespace sf2::scene {

// One spark particle (world space; y is up-negative, same as the pose
// bones — gravity pulls toward +y).
struct particle {
    float x = 0.0f;             // world position (JS sprite node C/D)
    float y = 0.0f;
    float vx = 0.0f;            // JS `fg.x` — per-frame x displacement
    float vy = 0.0f;            // JS `fg.y` — per-frame y displacement (+gravity)
    float rotation_deg = 0.0f;  // JS `Wg(...)` — sprite rotation in degrees
    float life = 0.0f;          // JS pool lifetime (`uba>90` -> 90 frames)
    float age = 0.0f;           // frames lived so far (age >= life = dead)
    float size = 0.0f;          // world units (JS `la(.3)` * sprite sourceSize)
    std::uint32_t color = 0xFFFFFFFFu;  // 0xRRGGBB
};

// The particle pool: JS keeps ONE burst (`WY`) + a pool clock (`uba`); a new
// hit clears and replaces the burst. Particles are compacted each update.
class EffectSystem {
public:
    // Spawns the JS `ryb` burst: clears any previous burst, then emits
    // `kSparkCount` (=4, JS L369 default) sparks from the hit point (x, y)
    // travelling along `facing` (±1 -> the hit direction x). `color` is the
    // location root colour (JS `Na.cd(Lb.N2)`); the caller may pass it.
    void spawn_hit_sparks(float x, float y, int facing,
                          std::uint32_t color = 0xFFFFFFFFu);

    // Advances every particle one 60 Hz frame (JS `av.ia` + `Ut.Cnb`):
    // position += fg, fg.y += 0.2, rotation recomputed; clears the pool when
    // the burst clock passes 90.
    void update();

    // The live particles (for rendering; fade each by age/life).
    const std::vector<particle>& particles() const { return live_; }

    // True while no particle is alive.
    bool empty() const { return live_.empty(); }

    // Resets the pool (between battles).
    void clear() {
        live_.clear();
        pool_age_ = 0.0f;
    }

private:
    // A private deterministic LCG in [0,1) — NOT the fight's shared roll01
    // (see the file comment).
    float next01();

    std::vector<particle> live_;
    float pool_age_ = 0.0f;            // JS `uba` — burst clock (frames)
    std::uint32_t lcg_ = 0x853C49E7u;  // fixed seed — reproducible bursts
};

}  // namespace sf2::scene

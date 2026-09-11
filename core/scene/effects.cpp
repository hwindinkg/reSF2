// Hit-spark particles — see effects.hpp for the JS refs and the RNG note.

#include "scene/effects.hpp"

#include <cmath>

namespace sf2::scene {

// Spark tuning — every value is the JS `av`/`ryb` number, not a guess.
namespace {

// JS `ql.Rub` (L369): `c==null&&(c=4)` — the burst count.
constexpr int kSparkCount = 4;
// JS `Ut.ryb` (L824): `e.Y.la(.3)` — uniform sprite scale.
constexpr float kSparkScale = 0.3f;
// `y.xRa` -> "drop_blood" (L2465) in the ui/misc atlas (asset 260, manifest
// L2490): sourceSize 26x26. World base = sourceSize * scale = 26 * 0.3.
constexpr float kSparkSpriteWorld = 26.0f;
constexpr float kSparkSize = kSparkScale * kSparkSpriteWorld;  // 7.8 units
// JS `av` ctor (L833): fg = (dir/200) + rand ranges.
constexpr float kSparkDirDiv = 200.0f;
constexpr float kJitterXMin = -40.0f;
constexpr float kJitterXMax = 40.0f;
constexpr float kJitterYMin = -60.0f;
constexpr float kJitterYMax = 20.0f;
constexpr float kJitterDiv = 10.0f;
// JS `av.ia` (L833): `this.fg.y += .2` every tick.
constexpr float kSparkGravity = 0.2f;
// JS `Ut.Cnb` (L824): clear the pool once `uba > 90`.
constexpr float kSparkPoolLife = 90.0f;

constexpr float kPi = 3.14159265358979323846f;

// JS `Y.Wg(atan2(fg.y,fg.x)*57.29577951308232*(fg.x<0?-1:1))`.
float spark_rotation_deg(float vx, float vy) {
    const float deg = std::atan2(vy, vx) * (180.0f / kPi);
    return vx < 0.0f ? -deg : deg;
}

}  // namespace

float EffectSystem::next01() {
    // A 32-bit LCG (Numerical Recipes) — deterministic, private to the
    // effect layer; never touches the fight's shared roll01.
    lcg_ = 1664525u * lcg_ + 1013904223u;
    return static_cast<float>(lcg_ >> 8) * (1.0f / 16777216.0f);
}

void EffectSystem::spawn_hit_sparks(float x, float y, int facing,
                                    std::uint32_t color) {
    // JS `Ut.ryb` (L824) clears the previous burst before spawning the new
    // one (`this.dKa()`), then resets the pool clock (`this.uba=0`).
    live_.clear();
    pool_age_ = 0.0f;

    // The native hit is directed along ±x by the attacker's facing — the JS
    // `IDa` direction vector reduced to its x component.
    const float dir_x = facing >= 0 ? 1.0f : -1.0f;
    const float dir_y = 0.0f;

    live_.reserve(static_cast<std::size_t>(kSparkCount));
    for (int i = 0; i < kSparkCount; ++i) {
        particle p;
        p.x = x;
        p.y = y;
        // JS `av` ctor (L833):
        //   fg.x = dir.x/200 + rand(-40,40)/10
        //   fg.y = dir.y/200 + rand(-60,20)/10
        p.vx = dir_x / kSparkDirDiv +
               (kJitterXMin + next01() * (kJitterXMax - kJitterXMin)) / kJitterDiv;
        p.vy = dir_y / kSparkDirDiv +
               (kJitterYMin + next01() * (kJitterYMax - kJitterYMin)) / kJitterDiv;
        p.rotation_deg = spark_rotation_deg(p.vx, p.vy);
        p.life = kSparkPoolLife;  // JS `uba>90` (whole burst)
        p.age = 0.0f;
        p.size = kSparkSize;      // JS `la(.3)`
        p.color = color;          // JS `Na.cd(Lb.N2)` (location root colour)
        live_.push_back(p);
    }
}

void EffectSystem::update() {
    // JS `Ut.Cnb` (L824): for each particle `ia()`, then `uba++` and
    // `dKa()` (clear) when `uba > 90`.
    for (particle& p : live_) {
        // JS `av.ia` (L833).
        p.x += p.vx;
        p.y += p.vy;
        p.vy += kSparkGravity;
        p.rotation_deg = spark_rotation_deg(p.vx, p.vy);
        p.age += 1.0f;
    }
    pool_age_ += 1.0f;
    if (pool_age_ > kSparkPoolLife) {
        live_.clear();
        pool_age_ = 0.0f;
    }
}

}  // namespace sf2::scene

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

// JS `Az(a,b)` (L12): signed angle from `a` to `b` in degrees
// (`atan2(cross, dot) * 180/pi`). `Az(dir,(1,0))` = atan2(-dir_y, dir_x).
float sign_angle_deg(float ax, float ay, float bx, float by) {
    const float cross = ax * by - ay * bx;
    const float dot = ax * bx + ay * by;
    return std::atan2(cross, dot) * (180.0f / kPi);
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
        // JS `Na.cd(Lb.N2)` — location root colour; the explicit argument (or
        // `set_color`) overrides the white default.
        p.color = (color == 0xFFFFFFFFu) ? color_ : color;
        live_.push_back(p);
    }
}

void EffectSystem::spawn_hit_flash(float x, float y, float dir_x, float dir_y,
                                   float angle_offset_deg, float scale, float speed,
                                   const std::string& frame_prefix, int frame_count) {
    // JS `Hyb` (L825): `this.lo.Fwb()` stops the previous run first, then a
    // fresh `gJa` run is started (`play`) and configured.
    flash_ = hit_flash{};
    flash_.active = frame_count > 0;
    flash_.x = x;
    flash_.y = y;
    // JS: `a=Az(dir,(1,0))+f; this.lo.Wg(isNaN(a)?0:-a)`.
    const float az = sign_angle_deg(dir_x, dir_y, 1.0f, 0.0f) + angle_offset_deg;
    flash_.angle_deg = std::isnan(az) ? 0.0f : -az;
    flash_.scale = scale * 0.7f;  // JS `this.lo.la(e*.7)`
    flash_.frame_prefix = frame_prefix;
    flash_.frame_count = flash_.active ? frame_count : 0;
    // JS `Hyb` (L825): `d.uub(1/c/60)` sets the run time-scale
    // `NL = 1/(c*60)`; the `kg.Yda(run,60)` run frame is `1/60` animation-s
    // (L732) and `Vj.update` advances `CA += dt*NL` (L1451), so the real
    // per-frame time is exactly `c` (`Vu.time`, `lrb` L395) -- not
    // `1/(c*60)` (that is `NL`, a time-scale, not a duration).
    flash_.frame_time_sec = speed > 0.0f ? speed : (1.0f / 60.0f);
    flash_.frame = 0;
    flash_.accum = 0.0f;
    flash_.age = 0.0f;
}

std::string EffectSystem::hit_flash_frame() const {
    if (!flash_.active || flash_.frame_count <= 0) return std::string();
    int f = flash_.frame;
    if (f < 0) f = 0;
    if (f >= flash_.frame_count) f = flash_.frame_count - 1;
    const std::string& p = flash_.frame_prefix;
    return p + "/" + p + "_" + std::to_string(f + 1);
}

void EffectSystem::show_offscreen_markers(float left_off, float right_off, float speed,
                                          float view_w, float view_h, float floor_y) {
    // JS `sXa` (L827-828): `this.FN==null&&(...)`, `this.hO==null&&(...)` —
    // the arrows are created once; later calls update nothing. Re-running
    // here would reset the frame cursor, so bail out while already shown.
    if (markers_.active) return;
    // JS `sXa` (L827-828): `c=1/(c/60)|0` then `kg.Yda(frames, c)`. `Yda`
    // sets each frame duration to `1/fps` (L1620, `b=1/b`), so with
    // `fps=60/speed` one frame lasts `speed/60` s.
    const float fps = speed > 0.0f ? (60.0f / speed) : 60.0f;
    const float frame_time = 1.0f / fps;
    const float h = 7.0f + 2.0f * floor_y;               // JS `h`
    const float y = view_h * 0.5f - floor_y - 7.0f;      // JS `-k`, k=7+ct-H/2
    markers_ = offscreen_markers{};
    markers_.active = true;
    markers_.frame_count = 20;                           // frames "0".."19"
    markers_.left.x = -view_w * 0.25f + left_off * 0.5f;  // JS `-W/4+a/2`
    markers_.left.width = view_w * 0.5f + left_off;       // JS `W/2+a`
    markers_.left.y = y;
    markers_.left.height = h;
    markers_.left.frame_time_sec = frame_time;
    markers_.right.x = view_w * 0.25f + right_off * 0.5f; // JS `W/4+b/2`
    markers_.right.width = view_w * 0.5f - right_off;     // JS `W/2-b`
    markers_.right.y = y;
    markers_.right.height = h;
    markers_.right.frame_time_sec = frame_time;
}

std::string EffectSystem::marker_frame_name(const fight_marker& m) {
    int f = m.frame % 20;
    if (f < 0) f += 20;
    return std::to_string(f);
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

    const float dt = 1.0f / 60.0f;  // one fixed 60 Hz tick
    // JS `Hyb` one-shot completion (`gJa(..., function(){g.lo.R(!1)})`): step
    // the flash frames; deactivate when the run is exhausted.
    if (flash_.active) {
        flash_.accum += dt;
        flash_.age += 1.0f;
        const float ft = flash_.frame_time_sec > 0.0f ? flash_.frame_time_sec : dt;
        while (flash_.active && flash_.accum >= ft) {
            flash_.accum -= ft;
            ++flash_.frame;
            if (flash_.frame >= flash_.frame_count) flash_.active = false;
        }
    }

    // JS `beb` (L828): the ringout arrows loop their 20 frames forever until
    // `pnb` removes them.
    if (markers_.active) {
        const int n = markers_.frame_count > 0 ? markers_.frame_count : 20;
        fight_marker* both[2] = {&markers_.left, &markers_.right};
        for (fight_marker* m : both) {
            m->accum += dt;
            const float ft = m->frame_time_sec > 0.0f ? m->frame_time_sec : dt;
            while (m->accum >= ft) {
                m->accum -= ft;
                m->frame = (m->frame + 1) % n;
            }
        }
    }
}

}  // namespace sf2::scene

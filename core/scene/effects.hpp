#pragma once

// Fight VISUAL effects layer — no simulation impact. Faithful port of:
//   - the spark subsystem `av` (L833-834) spawned by `Ut.ryb` (L824) and
//     triggered through `ql.Rub`/`Ut.Bob` (L369/L368);
//   - the hit-flash overlay `Hyb` (L825) — the timed `fight/fx` overlay;
//   - the off-screen ringout markers `sXa` (L827-828) — the two `fight/
//     ringout` arrows on the camera-glued UI container `Cu`.
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
//   - `Na.cd(Lb.N2)` (L824/L833): every effect sprite is tinted with the
//     location root colour (`set_color`).
//
// IMPORTANT: the RNG here is a PRIVATE deterministic LCG — it must never
// consume the fight's shared roll01 (that would perturb the AI decisions
// and diverge the pose dump from the oracle). JS uses the wall-clock RNG
// (`oa.eT`); the native keeps determinism deliberately.

#include <cstdint>
#include <string>
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

// The hit-flash overlay — JS `Hyb` (L825). One `fight/fx` (asset 1306,
// manifest L2490) frame run (`jg.Rza.v[vT]`: "hit_blade"/"critical" = 29
// frames, L731) played ONCE at the hit point, rotated to the hit direction.
// JS cites:
//   - `this.lo` = `R.$(E.get(1306))` (L832) — the `fight/fx` atlas sprite.
//   - `this.lo.C(a.x); this.lo.D(a.y)` — position = the hit point `a`.
//   - `this.lo.la(e*.7)` — uniform scale (`la`).
//   - `a=Az(dir,(1,0))+f; this.lo.Wg(isNaN(a)?0:-a)` — rotation in degrees.
//   - `d.uub(1/c/60)` (L1621) — frame timing. The overlay is TIMED
//     (one-shot): when the run ends the `gJa` completion callback hides it
//     (`g.lo.R(!1)`).
struct hit_flash {
    bool active = false;
    float x = 0.0f;               // world/container x (JS `C(a.x)`)
    float y = 0.0f;               // world/container y (JS `D(a.y)`)
    float angle_deg = 0.0f;       // JS `Wg(-(Az(dir)+f))`
    float scale = 1.0f;           // JS `la(e*.7)`
    std::string frame_prefix;     // JS `d` run key, e.g. "hit_blade"
    int frame_count = 0;          // run length (29 for hit_blade/critical)
    int frame = 0;                // cursor; name = prefix + "_" + (frame+1)
    float frame_time_sec = 1.0f;  // JS `uub(1/c/60)` — seconds per frame
    float accum = 0.0f;           // frame-time accumulator
    float age = 0.0f;             // ticks lived
};

// One off-screen ringout arrow — JS `sXa` (L827-828). `fight/ringout`
// (asset 1300) frames "0".."19" drawn on the camera-glued UI container `Cu`
// (`FOa` L831 copies the fighter-node scale/translate onto it).
// JS cites (L827-828): `h=7+2*ct` (`Pb`, display height);
// `k=7+ct-H/2` with y=`-k` (`D`); the left arrow x = `-W/4+a/2` and width
// `W/2+a` (`C`/`xc`), the right arrow x = `W/4+b/2` and width `W/2-b`;
// frame run `kg.Yda(e,c)` (L1620) with `c=floor(60/speed)` (`Yda` frame =
// 1/fps, L1620 `b=1/b`).
struct fight_marker {
    float x = 0.0f;               // screen x (JS `r.C(q)`)
    float y = 0.0f;               // screen y (JS `r.D(-k)`)
    float width = 0.0f;           // JS `r.xc(n)` — display width
    float height = 0.0f;          // JS `r.Pb(h)` = 7 + 2*floor
    int frame = 0;                // cursor over frames "0".."19"
    float frame_time_sec = 1.0f;  // JS `kg.Yda`: 1/fps = speed/60
    float accum = 0.0f;
};

// The two arrows of `sXa`: `FN` (left) + `hO` (right), shown by `sXa` and
// removed by `pnb` (L828). `active` mirrors `this.cga`.
struct offscreen_markers {
    bool active = false;
    fight_marker left;            // JS `this.FN`
    fight_marker right;           // JS `this.hO`
    int frame_count = 20;         // frames "0".."19"
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

    // JS `Na.cd(Lb.N2)` (L824/L833): the location root colour tints every
    // spawned sprite. The fight screen sets this once from the location root
    // colour instead of the hardcoded white default.
    void set_color(std::uint32_t color) { color_ = color; }
    std::uint32_t color() const { return color_; }

    // JS `Hyb` (L825): starts the one-shot hit-flash overlay. `dir_x/dir_y`
    // is the hit direction `b`, `angle_offset_deg` is `f`, `scale` is `e`
    // (stored as `e*.7`), `speed` is `c` (frame time = 1/(speed*60)).
    void spawn_hit_flash(float x, float y, float dir_x, float dir_y,
                         float angle_offset_deg, float scale, float speed,
                         const std::string& frame_prefix, int frame_count);
    const hit_flash& hit_flash_state() const { return flash_; }
    void clear_hit_flash() { flash_ = hit_flash{}; }

    // Atlas frame name for the current flash frame ("prefix/prefix_N"), or
    // "" while inactive. Matches the `jg.Rza` runs (L373) / `fx_frames`.
    std::string hit_flash_frame() const;

    // JS `sXa(a,b,c)` (L827-828): shows the two ringout arrows.
    // `left_off`=a, `right_off`=b, `speed`=c; `view_w/view_h/floor_y` are
    // `Lb.width/Lb.height/Lb.ct`.
    void show_offscreen_markers(float left_off, float right_off, float speed,
                                float view_w, float view_h, float floor_y);
    // JS `pnb` (L828): removes both arrows.
    void hide_offscreen_markers() { markers_ = offscreen_markers{}; }
    const offscreen_markers& markers_state() const { return markers_; }
    // Atlas frame name for a marker's current frame ("0".."19").
    static std::string marker_frame_name(const fight_marker& m);

    // True while nothing at all is live (sparks, flash, markers).
    bool empty() const { return live_.empty() && !flash_.active && !markers_.active; }

    // Resets the pool (between battles).
    void clear() {
        live_.clear();
        pool_age_ = 0.0f;
        flash_ = hit_flash{};
        markers_ = offscreen_markers{};
    }

private:
    // A private deterministic LCG in [0,1) — NOT the fight's shared roll01
    // (see the file comment).
    float next01();

    std::vector<particle> live_;
    float pool_age_ = 0.0f;            // JS `uba` — burst clock (frames)
    std::uint32_t lcg_ = 0x853C49E7u;  // fixed seed — reproducible bursts
    std::uint32_t color_ = 0xFFFFFFFFu;  // JS `Na.cd(Lb.N2)` location tint
    hit_flash flash_;                  // JS `this.lo` (`Hyb` L825)
    offscreen_markers markers_;        // JS `this.FN`/`this.hO` (`sXa` L827)
};

}  // namespace sf2::scene

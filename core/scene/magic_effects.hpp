#pragma once

// Magic/effect containers (Phase 7.2) — the native port of the JS effect
// containers `Xm` (L836-837) / `cv` "EffectsRunning" (L837-839) driven by the
// frame animator `ni` (L1141-1144).
//
// JS cites (sf2.502f0946.js, verified this session):
//   - `tl` (L842-844): `qh` = fighters, `Gq` = `Xm` ground effects (z=+.01),
//     `Hq` = `Xm` air effects (z=+.01). `tl.Nt(a)` (L842) routes a started
//     effect by `a.Gfb` (OnBackground): `Gfb ? Gq.Nt(a) : Hq.Nt(a)`.
//   - `cv.lwb(a, b)` (L838-839) attaches an effect to a bone and builds the
//     `ni` frame animation: `c = a.NL/60` seconds per frame, `f.mP = c`,
//     `f.iterations = a.wcb ? -1 : 1`, `a.lYa ? f.wrb() : f.RLa()`.
//   - `ni` (L1141-1144): `mP` = seconds per frame, `Qe` accumulator, `hc`
//     cursor, `K9` step (±1), `Qu`=last / `mv`=first, `iterations` (>0
//     finite, -1 loop), `LJ` playing. `ia(a)` loops
//     `for(Qe+=a; Qe>=mP;) { JXa(); Qe-=mP }`; `JXa` steps `hc`, wraps to
//     `mv` and decrements `iterations` (clearing `LJ` at 0).
//   - `a.NL` = the `TimeScale` attribute (L729, default 1); so one `ni`
//     frame lasts `NL` ticks at 60 Hz (native `ticks_per_frame`).
//   - `cv.WL()` (L839): ticks with `a = 1/v.on()` (global timescale) via
//     `d.animate.ia(L.K.sk.Bm*a)`; a finished one-shot (`!LJ`) is removed.
//
// Asset facts (manifest L2490): hit flash `E.get(1306)` = `fight/fx` (frames
// `hit_blade/*`, `critical/*`, `block/*`, `effect_shield_hex_hit/*` — the
// real `ni` frame source); markers `E.get(1300)` = `fight/ringout`; sparks
// `E.get(260)` = `ui/misc`. There are NO `magic/*.json` in this snapshot; the
// `fight/fx` atlas frame runs are the available `ni` frame sets.
//
// NO gameplay impact: instances carry only presentation state (position,
// facing, frame cursor). Spawning/updating/destroying never touches the
// fight simulation (same layering rule as the banner machine and the hit
// sparks — the pose dump is byte-identical with or without effects).

#include <cstdint>
#include <string>
#include <vector>

namespace sf2::scene {

// One data-loaded effect kind (one `magic/<fileName>` in JS terms).
struct MagicEffectDesc {
    std::string name;                  // spawn("name")
    std::vector<std::string> frames;   // atlas frame names, play order
    bool loop = false;                 // JS `wcb` / iterations -1
    bool reverse = false;              // JS `lYa` -> `wrb()` (vs `RLa()`)
    bool on_background = false;        // JS `Gfb` (L729) -> `tl.Nt` ground layer
    float ticks_per_frame = 1.0f;      // JS `NL` (TimeScale, L729); mP = NL/60 s
    float scale = 1.0f;                // JS `Wl * scale` (with facing)
    float size = 24.0f;                // world-unit quad size
    std::uint32_t color = 0xFFFFFFFFu;  // 0xRRGGBB tint
    float gravity = 0.0f;              // world units/frame^2 (+y = down)
    float vx = 0.0f;                   // drift, world units/frame
    float vy = 0.0f;
};

// One live effect (a `bv`-wrapped `dd` in JS terms).
struct MagicInstance {
    std::size_t desc = 0;     // index into the loaded descs
    float x = 0.0f;           // world position
    float y = 0.0f;
    float vx = 0.0f;          // per-instance drift (copied at spawn)
    float vy = 0.0f;
    int facing = 1;           // JS `Fc.Wl = da.hd()` (+1 / -1 mirror)
    int frame = 0;            // JS `ni.hc` — current frame cursor
    int frame_step = 1;       // JS `ni.K9` (+1 forward / -1 backward)
    int iterations_left = 1;  // JS `ni.iterations` (>0 finite; -1 = loop)
    bool playing = true;      // JS `ni.LJ`
    float accum = 0.0f;       // JS `ni.Qe` — frame-time accumulator (seconds)
    float age = 0.0f;         // ticks lived (for the end-fade)
    // JS `bv.model` — the emitting model, the second half of the effect
    // identity used by `LNa`/`Gwb` (`b == f.model`). 0/1 = the native fight
    // sides (player/enemy); -1 = unbound (legacy spawn).
    int owner = -1;
    // JS `bv.effect.P1` — a follow/attach effect (set by `<Attach>` or
    // `<Position Follow="true">`, `Yl.parse` L730). Repositioned every tick
    // from the owner's live transform by `bv.update` (L834).
    bool follow = false;
    // JS `bv.Yla` — latched by `StopFollowEffect` (`cv.Gwb` L838). While set,
    // `cv.WL` (L839) skips the `d.update()` follow step but keeps advancing
    // the frame animation (`d.animate.ia`) to completion.
    bool detached = false;
};

// One owner transform for the follow update (JS `bv.update` reads
// `model.Fc` + `model.da.hd()`): world anchor + facing sign.
struct EffectAnchor {
    float x = 0.0f;
    float y = 0.0f;
    int facing = 1;
};

// The two render containers (JS `Xm` + `cv`, L836-839): descriptors + live
// instances, split by `OnBackground` (`Gfb`, L729) the way `tl.Nt` routes
// into `Gq`/`Hq` (L842) — see `background()`/`foreground()`.
class MagicEffects {
public:
    // JS `tl.Zab` (L843-844): the two `Xm` containers are glued to the scene
    // with `translate.z = .01` (`Gq` first, `Hq` second) -> the effects layer
    // sits above the fighters (z=0) and the background. The renderer draws
    // `background()` at `kBackgroundZ` and `foreground()` at `kForegroundZ`.
    static constexpr float kBackgroundZ = 0.01f;  // JS `Gq` z=+.01 (L843)
    static constexpr float kForegroundZ = 0.01f;  // JS `Hq` z=+.01 (L844)

    // Replaces the descriptor set (JS: the `magic/*.json` registry).
    // Returns false when `descs` is empty (keeps the old set).
    bool load(const std::vector<MagicEffectDesc>& descs);

    // Seeds the two built-in descriptors from the real `fight/fx` atlas
    // frame runs (the available `ni` frames): "hit_flash" (hit_blade),
    // "magic_trail" (effect_shield_hex_hit, looping).
    void add_default_descs();

    // Spawns a live instance (JS `Nt`/`lwb`). Returns false for unknown
    // names (never throws, never touches the sim). `owner` is the emitting
    // model (JS `bv.model`); `follow` marks a follow/attach effect (JS `P1`).
    bool spawn(const std::string& name, float x, float y, int facing,
               int owner = -1, bool follow = false);

    // JS `cv.Dwb`/`LNa` (L838): StopEffect. Destroys the FIRST live instance
    // whose `(effect.name == name && model == owner)` matches (the loop
    // `break`s on the first hit). `owner < 0` keeps the legacy remove-all of
    // every instance with that name.
    void stop(const std::string& name, int owner);
    // Legacy name-only stop (remove every instance with that name).
    void stop(const std::string& name) { stop(name, -1); }
    void stop_all();

    // JS `cv.Hwb`/`Gwb` (L838): StopFollowEffect. Latches `Yla` on the first
    // live instance whose `(effect.name == name && model == owner)` matches,
    // which stops the follow update while the animation plays out.
    void stop_follow(const std::string& name, int owner);

    // Advances one 60 Hz tick (JS `ni.ia`: `animate.ia(Bm * (1 / v.on()))`).
    // `timescale` is JS `v.on()` (1.0 = real time). Finished one-shots
    // (`!LJ`) are destroyed (JS `LNa`); loopers wrap. `owners`/`owner_count`
    // are the live model transforms for the follow update (`bv.update` L834):
    // a live `follow && !detached` instance is repositioned from
    // `owners[in.owner]` before the frame step.
    void update(float timescale, const EffectAnchor* owners = nullptr,
                int owner_count = 0);

    // The two render containers (JS `tl.Gq`/`tl.Hq`, L842-844). `background`
    // holds the `OnBackground` (`Gfb`) instances (JS `Gq`, z=+.01) that draw
    // BEFORE the fighters; `foreground` holds the rest (JS `Hq`, z=+.01)
    // that draw AFTER — the two render passes (PORT_AUDIT_SCENE W4).
    const std::vector<MagicInstance>& background() const { return background_; }
    const std::vector<MagicInstance>& foreground() const { return foreground_; }

    // Combined view (background then foreground) for callers that do not
    // split the passes. Returns a copy.
    std::vector<MagicInstance> live() const;

    bool empty() const { return background_.empty() && foreground_.empty(); }
    void clear() {
        background_.clear();
        foreground_.clear();
    }

    // Frame fade for one instance: 1.0 through most of life, ramping out
    // over the last 8 ticks for one-shots (the `uub` fade in JS terms).
    float alpha_for(const MagicInstance& in) const;

    // Current atlas frame name for one instance ("" when the desc has no
    // frames — the renderer then draws the flat tinted quad).
    std::string frame_for(const MagicInstance& in) const;

    // Total one-shot lifetime in ticks (loopers report -1.0f).
    float life_for(const MagicInstance& in) const;

    // Presentation fields of the instance's descriptor (for the renderer).
    float size_for(const MagicInstance& in) const;
    std::uint32_t color_for(const MagicInstance& in) const;

    // JS `Gfb` (L729) -> JS `tl.Nt` (L842) picks `Gq` (ground) vs `Hq` (air).
    bool background_for(const MagicInstance& in) const;

private:
    const MagicEffectDesc* find(const std::string& name) const;

    std::vector<MagicEffectDesc> descs_;
    std::vector<MagicInstance> background_;  // JS `Gq` — before the fighters
    std::vector<MagicInstance> foreground_;  // JS `Hq` — after the fighters
};

}  // namespace sf2::scene

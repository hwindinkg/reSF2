#pragma once

// Magic/effect containers (Phase 7.2) — the native port of the JS effect
// containers `Xm` (L836-837) / `cv` "EffectsRunning" (L837-839).
//
// JS cites (sf2.502f0946.js, verified 2026-09-04):
//   - `tl` (L842-844): `qh` = fighters, `Gq` = `Xm` ground effects (z=+.01),
//     `Hq` = `Xm` air effects (z=+.01) — effects layer just above everything.
//     `tl.ZP` subscribes fighter `Nt`/`Ot`/`Pt` events to effect start/stop.
//   - `cv.lwb(a, b)` (L838-839): builds a `dd` effect from
//     `magic/<fileName>.json` + `magic/<fileName>.png`
//     (`E.get(G.qf("magic/" + a.fileName + ".json"))`), wraps it in a `bv`
//     (facing mirror via `hd()`, position from the bone, scale), with
//     `iterations` (`wcb` -> -1 = loop), direction `RLa`/`wrb`
//     (forward/backward), speed `mP = NL / 60`.
//   - `cv.WL()` (L839): ticks with `a = 1 / v.on()` (global timescale):
//     `d.animate.ia(L.K.sk.Bm * a)` — and destroys finished non-loop effects
//     (`LJ` false -> `LNa` remove). `P1 && !Yla` effects also `update()`.
//   - `Xm`: `Nt` (add `lwb`), `Ot` (remove `Dwb`), `Pt` (stop `Hwb`).
//
// Data note: this www snapshot ships NO `magic/*.json` (only the fight fx
// atlas `fight/fx.*.json` + `particles.*.json`, both TexturePacker frame
// lists — verified on disk). So descriptors are fed as DATA
// (`MagicEffectDesc`, e.g. frame runs collected from the fx atlas by the
// caller); `add_default_descs()` seeds three built-in descriptors so the
// pipeline works headless. When real `magic/*.json` assets land, parse them
// into `MagicEffectDesc` and `load()` them — no code change here.
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
    bool loop = false;                 // JS `LJ` / iterations `wcb` -> -1
    bool reverse = false;              // JS `wrb()` (vs `RLa()` forward)
    float ticks_per_frame = 4.0f;      // JS `mP = NL / 60` frame pacing
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
    float frame_pos = 0.0f;   // JS `animate` cursor, in frames
    float age = 0.0f;         // ticks lived (for the end-fade)
};

// The container: data-loaded descriptors + live instances (JS `Xm` + `cv`).
class MagicEffects {
public:
    // Replaces the descriptor set (JS: the `magic/*.json` registry).
    // Returns false when `descs` is empty (keeps the old set).
    bool load(const std::vector<MagicEffectDesc>& descs);

    // Seeds the three built-in descriptors (data-or-default so the pipeline
    // runs before real magic JSON lands): "hit_flash" (one-shot impact
    // flash), "round_intro" (one-shot ring at round start), "magic_trail"
    // (looping missile trail).
    void add_default_descs();

    // Spawns a live instance (JS `Nt`/`lwb`). Returns false for unknown
    // names (never throws, never touches the sim).
    bool spawn(const std::string& name, float x, float y, int facing);

    // Stops live instances of `name` (JS `Pt`/`Hwb`); `stop_all` clears.
    void stop(const std::string& name);
    void stop_all();

    // Advances one 60 Hz tick (JS `WL`: `animate.ia(Bm * (1 / v.on()))`).
    // `timescale` is JS `v.on()` (1.0 = real time). Finished non-loop
    // instances are destroyed (JS `LNa`); loopers wrap.
    void update(float timescale);

    // The live instances (for rendering).
    const std::vector<MagicInstance>& live() const { return live_; }
    bool empty() const { return live_.empty(); }
    void clear() { live_.clear(); }

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

private:
    const MagicEffectDesc* find(const std::string& name) const;

    std::vector<MagicEffectDesc> descs_;
    std::vector<MagicInstance> live_;
};

}  // namespace sf2::scene

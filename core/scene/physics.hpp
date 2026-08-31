#pragma once

// Fight physics: hit detection (capsule-vs-capsule), impulse/knockback and
// arena bounds. Ported term-for-term from sf2.502f0946.js — see
// core/scene/README.md for the full JS study with line refs.
//
// JS study summary (details in README.md):
//   - Attack check: `ca.Enb` (L390) alternates fighters, `wd.tKa` (L499)
//     -> `wd.HZa` (L499) -> `Fu.ia` (Cl, L566). `Cl.ia` tests the
//     attacker's active Attack interval's AttackingParts capsules
//     (`Te.GY`, filled by `Te.xqb` L566 from the interval's edge names via
//     `model.RAa`) against the target's collidable capsules (`oa.Nl.oI`).
//   - The geometric test is `Bz` (L12): 2D swept-capsule vs capsule on the
//     XZ plane (y is the up axis, dropped for the hit test — SF2 fighters
//     fight on the ground plane). Two moving capsules (each defined by its
//     two endpoint nodes) with radii: the distance between the segment
//     mid-lines must drop below the sum of radii during the frame.
//   - Impulse: `wd.Kwb` (L509) builds the impulse vector from the interval
//     (Impulse X/Y/Z), scaled by facing and fighter scale (`JG`), then
//     `wd.strike` (L509) -> `Bva` -> `IH.strike` (Bl, L582) splits it onto
//     the two endpoint nodes of the hit capsule by the hit position
//     (`(1-b)/weight` and `b/weight`).
//   - Bounds: `Al.ia` (L582) -> `fha` clamps every body node to the arena
//     x-range [wall, width-wall] and y >= 0 (floor). The wall/floor come
//     from the location params (`Bf.init` L474: `NU=Wall`, `ct=Floor`).
//     Fight setup (`ca.ggb` L383) sets `v.tFa=location.NU` and
//     `v.NKa=location.width-NU`.

#include <cstdint>
#include <string>
#include <vector>

#include "scene/model.hpp"

namespace sf2::scene {

// A 3D point/vector in world units (x = lateral, y = up, z = depth).
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
};

// One live collision capsule: an edge's two endpoint bones' CURRENT world
// positions + radius + defense/body-part tags (JS `yu` L803-807).
struct HitCapsule {
    std::string name;         // edge name (`yu.name`)
    Vec3 p1;                  // endpoint 1 world pos (`yu.sx.ma`)
    Vec3 p2;                  // endpoint 2 world pos (`yu.Zs.ma`)
    float radius = 0.0f;      // `yu.gb`
    float margin1 = 0.0f;     // `yu.$Fa`
    float margin2 = 0.0f;     // `yu.aGa`
    bool collidable = false;  // `yu.vZ` — in the TARGET's hit list (`Nl.oI`)
    std::string body_part;    // `yu.HC` ("Head"/"Body"/"Legs")
    std::string defense;      // `yu.Xi` ("BodyDefense"/"HeadDefense")
    float weight1 = 1.0f;     // endpoint-1 node mass (`Vc.weight`)
    float weight2 = 1.0f;     // endpoint-2 node mass
};

// The result of one capsule-vs-capsule hit test (JS `Cl.W1a` L566).
struct CapsuleHit {
    bool hit = false;
    // The closest-point on the attacker capsule (JS `Bz` fills the
    // hit position into `strike.n$`/`strike.o$` — the two capsules'
    // closest endpoints; the demo uses the attacker endpoint).
    Vec3 point;
};

// --- geometric primitives (JS Bz L12, Uy L12, Vy/Wy L12, Ls L12) -------
// Segment-segment closest-point/collision on the XZ plane. The fighters'
// bodies are swept capsules (endpoint positions from the current pose);
// two capsules overlap when the distance between their segment mid-lines
// drops below the sum of their radii.
// `ic` helpers (L653): $z = |a-b|<1e-10, f2 = |a|<1e-10, wJ = a>=b&&a<=c,
// c4 = a*a.
bool capsule_capsule_overlap(const HitCapsule& a, const HitCapsule& b,
                             CapsuleHit& out);

// --- fighter body state (JS `Dl` + `Vc` nodes) -------------------------
// The physics body a fight needs: per-edge hit capsules resolved to the
// current pose, plus the body node masses for impulse weighting.
//
// NOTE: the JS keeps ALL edges in `Nl.all` (the attacker's AttackingParts
// resolve against it) and only the `Collisible="1"` edges in `Nl.oI`
// (the target's hit list). `build()` includes every edge; the hit test
// filters `collidable` on the TARGET side.
struct BodyState {
    // Edge name -> live capsule (only collidable edges; JS `Nl.oI`).
    std::vector<HitCapsule> capsules;
    // Bone index -> node mass (JS `Vc.weight`, from the XML Mass attr).
    std::vector<float> node_mass;

    // Builds the hit-capsule list + masses from a merged model and a pose
    // (bone world positions, x/y pairs — `Fighter::positions()`). Edges
    // reference bone names; unknown bones produce no capsule.
    // `wall`/`width_minus_wall` are the arena x-bounds (JS `v.tFa`/`v.NKa`).
    void build(const Model& model, const std::vector<float>& pose_xy,
               float wall, float width_minus_wall);

    // Clamps every capsule endpoint to the arena bounds (JS `Al.fha` L582 +
    // `P6a` L582): x in [wall, width-wall], y >= 0 (floor). Returns the
    // max |dx| displacement applied (for the knockback log).
    float clamp_to_bounds();

    // Accessor for a single capsule by edge name (JS `Dl.RAa` L573).
    const HitCapsule* by_name(const std::string& name) const;

    // Arena bounds stored by build() (JS `v.tFa`/`v.NKa`).
    float wall_min = 0.0f;
    float wall_max = 0.0f;
};

// --- impulse / knockback (JS `Bl.strike` L582 + `wd.Kwb` L509) ---------
// Applies the move interval's Impulse (X/Y/Z) to the target's hit capsule
// endpoint nodes, split by the hit position along the capsule, then clamps
// to the arena bounds. `facing` is the attacker's facing (±1), `scale` is
// the fighter scale (`JG`, default 1). Mirrors:
//   Kwb: d = (b.kw, b.gR, b.hR) * facing * JG
//   Bl.strike: node1 += d * (1-b)/w1; node2 += d * b/w2
// where b = how far along the capsule the hit landed (0..1), w = node mass.
// The displacement is applied to the DEMO's fighter world-x directly (the
// full ragdoll integration — Vc.sk L796 gravity/friction — is out of scope
// for this milestone; the knockback FEEL — direction, weight split,
// bounds clamp — is exact).
struct ImpulseResult {
    Vec3 impulse;       // the scaled impulse vector
    float node1_disp = 0.0f;  // displacement applied to endpoint 1
    float node2_disp = 0.0f;  // displacement applied to endpoint 2
    float clamped_dx = 0.0f;  // how much the bounds clamp ate
};

// `hit_pos` is the hit point on the target capsule (from the collision
// test). `fighter_x` is the target fighter's world x (for the clamp);
// returns the target's new world x after the impulse + clamp.
float apply_impulse(const HitCapsule& hit_cap, const CapsuleHit& hit,
                    Vec3 impulse, float fighter_x, float wall,
                    float width_minus_wall, ImpulseResult& out);

} // namespace sf2::scene

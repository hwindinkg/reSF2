// Fight physics: hit detection (Bz), impulse (Bl.strike), bounds (Al.fha).
// Ported term-for-term from sf2.502f0946.js — line refs in README.md.

#include "scene/physics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf2::scene {

namespace {

constexpr float kEps = 1e-10f;  // JS `ic.f2`/`$z` epsilon

// JS `ic.f2(a)`: |a| < 1e-10.
bool f2(float a) { return std::fabs(a) < kEps; }
// JS `ic.$z(a,b)`: |a-b| < 1e-10.
bool nearly(float a, float b) { return std::fabs(a - b) < kEps; }
// JS `ic.wJ(a,b,c)`: b <= a <= c.
bool in_range(float a, float b, float c) { return a >= b && a <= c; }
// JS `ic.c4(a)`: a*a.
float sq(float a) { return a * a; }

// JS `Uy(a,b,c,d)`: d = a + (b-a)*c (lerp a point along a segment).
Vec3 lerp(const Vec3& a, const Vec3& b, float c) {
    return {a.x + (b.x - a.x) * c, a.y + (b.y - a.y) * c,
            a.z + (b.z - a.z) * c};
}

// JS `Vy(a,b,c)`: the 2D line equation for segment a-b, on the X/Y plane
// (the JS uses `a.y` — the fighter's height axis — as the second axis of
// the hit test; z is the depth and is ignored by `Bz`).
struct Line2 {
    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
};

Line2 line_of(const Vec3& a, const Vec3& b) {
    Line2 l;
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float d = std::sqrt(dx * dx + dy * dy);
    l.a = (a.y - b.y) / d;
    l.b = (b.x - a.x) / d;
    l.c = -(l.a * a.x + l.b * a.y);
    return l;
}

// JS `Uy(a,b,c,d)` (L11): lerp point d = a+(b-a)*c.
Vec3 lerp_pt(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// JS `Cz`: segment-segment crossing (2D). Collinear overlap writes the
// SECOND segment's start (`e=a` with Cz(a,b)=target pair at the Bz call
// site — REVIEW A LOW fix; verified against L14-15, not the review text).
bool seg_seg_hit(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                 Vec3& out) {
    if ((nearly(a.x, b.x) && nearly(a.y, b.y)) ||
        (nearly(c.x, d.x) && nearly(c.y, d.y))) {
        return false;
    }
    const float f = b.x - a.x;
    const float g = b.y - a.y;
    const float h = d.x - c.x;
    const float k = d.y - c.y;
    const float l = a.x - c.x;
    const float n = a.y - c.y;
    const float q = k * f - h * g;
    const float hh = h * n - k * l;
    const float ff = f * n - g * l;
    if (f2(q)) {
        if (f2(hh) || f2(ff)) {
            float q2 = 0, g2 = 0, l2 = 0, n2 = 0;
            if (a.x < b.x) { q2 = a.x; g2 = b.x; } else { q2 = b.x; g2 = a.x; }
            if (c.x < d.x) { l2 = c.x; n2 = d.x; } else { l2 = d.x; n2 = c.x; }
            if (q2 > n2 || l2 > g2) return false;
            if (a.y < b.y) { q2 = a.y; g2 = b.y; } else { q2 = b.y; g2 = a.y; }
            if (c.y < d.y) { l2 = c.y; n2 = d.y; } else { l2 = d.y; n2 = c.y; }
            if (q2 > n2 || l2 > g2) return false;
            out = c;  // collinear overlap: e = second-segment start
            return true;
        }
        return false;
    }
    const float hq = hh / q;
    if (!in_range(hq, 0.0f, 1.0f) || !in_range(ff / q, 0.0f, 1.0f)) return false;
    out.x = a.x + hq * (b.x - a.x);
    out.y = a.y + hq * (b.y - a.y);
    return true;
}

// JS `Ls(a,b,c,d,e,f,g)`: |a|<=b → project (e = d - a*line), in-range vs
// (f,g) or near an endpoint. Writes e whenever |a|<=b, even on range
// failure (X8 dirt — harmless: X8/o$ is only read on success, and every
// success path below writes it, so per-call zeroing == per-W1a zeroing).
bool in_capsule(float a, float b, const Line2& l, const Vec3& d, Vec3& e,
                const Vec3& f, const Vec3& g) {
    if (std::fabs(a) <= b) {
        e.x = d.x - a * l.a;
        e.y = d.y - a * l.b;
        e.z = 0.0f;
        if ((in_range(e.x, f.x, g.x) || in_range(e.x, g.x, f.x)) &&
            (in_range(e.y, f.y, g.y) || in_range(e.y, f.y, g.y))) {
            return true;
        }
        return sq(d.x - f.x) + sq(d.y - f.y) <= b * b ||
               sq(d.x - g.x) + sq(d.y - g.y) <= b * b;
    }
    return false;
}

}  // namespace

// JS `Bz(a,b,c,d,e,f,g,h,k,l)` verbatim roles (L12):
// (a,b)=target insets (Ula/Pda), c=target gb, (d,e)=attacker insets,
// f=attacker gb, g=W8 (n$), h=X8 (o$), k=attacker Eda, l=target Eda.
// `c+=f` is the RADII SUM (margins are build-time insets, never radius).
// Lines come from the RAW spans (`Vy(dw,Zs.ma,Eda)`); the test points
// come from the insets.
bool capsule_capsule_overlap(const HitCapsule& atk, const HitCapsule& tgt,
                             CapsuleHit& out) {
    Vec3 a = tgt.p1, b = tgt.p2;
    Vec3 d = atk.p1, e = atk.p2;
    float c = tgt.radius + atk.radius;
    Vec3 w8{}, x8{};  // W8/X8 zeroed per call (W1a-equivalent, see above)
    out.hit = false;
    out.kd_null = false;
    if (f2(c)) {
        if (seg_seg_hit(d, e, a, b, w8)) {
            x8 = w8;
            out.n = w8;
            out.o = x8;
            out.point = w8;
            out.hit = true;
            return true;
        }
        return false;
    }
    Line2 l = line_of(atk.r1, atk.r2);  // attacker Eda (raw span)
    Line2 k = line_of(tgt.r1, tgt.r2);  // target Eda (raw span)
    const float n = l.a * a.x + l.b * a.y + l.c;
    const float f2v = l.a * b.x + l.b * b.y + l.c;
    // Both target endpoints on the same side beyond the radii -> no hit.
    if (n * f2v >= 0 && c < std::fabs(n) && c < std::fabs(f2v)) return false;

    const float q = k.a * d.x + k.b * d.y + k.c;
    const float r = k.a * e.x + k.b * e.y + k.c;
    if (q * r >= 0 && c < std::fabs(q) && c < std::fabs(r)) return false;

    if (q * r < 0 && n * f2v < 0) {
        // Segments cross: contact on the attacker segment.
        const float t = q / (q - r);
        w8 = d + (e - d) * t;
        x8 = w8;
        out.n = w8;
        out.o = x8;
        out.point = w8;
        out.hit = true;
        return true;
    }

    // Endpoint-vs-capsule checks. n$ = the tested endpoint, o$ = its
    // projection (Ls writes o1..o4; locals keep X8 dirt contained).
    Vec3 o1 = x8, o2 = x8, o3 = x8, o4 = x8;
    if (in_capsule(n, c, l, a, o1, d, e)) {
        out.n = a; out.o = o1; out.point = a; out.hit = true; return true;
    }
    if (in_capsule(f2v, c, l, b, o2, d, e)) {
        out.n = b; out.o = o2; out.point = b; out.hit = true; return true;
    }
    if (in_capsule(q, c, k, d, o3, a, b)) {
        out.n = d; out.o = o3; out.point = d; out.hit = true; return true;
    }
    if (in_capsule(r, c, k, e, o4, a, b)) {
        out.n = e; out.o = o4; out.point = e; out.hit = true; return true;
    }
    return false;
}

void BodyState::build(const Model& model, const std::vector<float>& pose_xy,
                       float wall, float width_minus_wall,
                       const Model* foe_model,
                       const std::vector<float>* foe_pose_xy) {
    capsules.clear();
    // Node masses from the XML Mass attr (JS `Vc.weight` = Mass).
    node_mass.assign(model.bones.size(), 1.0f);
    for (std::size_t i = 0; i < model.bones.size(); ++i) {
        node_mass[i] = model.bones[i].mass;
    }
    for (const EdgeDef& edge : model.edges) {
        // Endpoint resolution per-label via `wBa` (WEA_STATIC §3): own
        // model first, foe model fallback (enemy-bone feed). A foe-side
        // endpoint samples the FOE pose; masses come from the foe model.
        const int i1 = model.bone_by_name(edge.end1);
        const int i2 = model.bone_by_name(edge.end2);
        const bool foe1 = i1 < 0 && foe_model != nullptr;
        const bool foe2 = i2 < 0 && foe_model != nullptr;
        const int r1 = foe1 ? foe_model->bone_by_name(edge.end1) : i1;
        const int r2 = foe2 ? foe_model->bone_by_name(edge.end2) : i2;
        if (r1 < 0 || r2 < 0) continue;
        const std::vector<float>& pose1 = (foe1 && foe_pose_xy != nullptr) ? *foe_pose_xy : pose_xy;
        const std::vector<float>& pose2 = (foe2 && foe_pose_xy != nullptr) ? *foe_pose_xy : pose_xy;
        const std::size_t u1 = static_cast<std::size_t>(r1);
        const std::size_t u2 = static_cast<std::size_t>(r2);
        if (pose1.size() < u1 * 2 + 2 || pose2.size() < u2 * 2 + 2) continue;
        HitCapsule cap;
        cap.name = edge.name;
        cap.end1 = edge.end1;
        cap.end2 = edge.end2;
        cap.rest_length = edge.length;
        cap.r1 = {pose1[u1 * 2], pose1[u1 * 2 + 1], 0.0f};
        cap.r2 = {pose2[u2 * 2], pose2[u2 * 2 + 1], 0.0f};
        // Inset endpoints (Lnb L791-793): Ula=lerp($Fa), Pda=lerp(1-aGa).
        // Radius is gb ONLY (margins never add — REVIEW A).
        cap.p1 = cap.r1 + (cap.r2 - cap.r1) * edge.margin1;
        cap.p2 = cap.r1 + (cap.r2 - cap.r1) * (1.0f - edge.margin2);
        cap.radius = edge.radius;
        // Margins scan telemetry (REVIEW A: shipped margins expected 0 —
        // nonzero would change hit shapes vs the old inflated radii).
        static bool margins_logged = false;
        if (!margins_logged &&
            (edge.margin1 != 0.0f || edge.margin2 != 0.0f)) {
            margins_logged = true;
            std::fprintf(stderr, "[phys] nonzero capsule margins: %s (%g,%g)\n",
                         edge.name.c_str(), (double)edge.margin1,
                         (double)edge.margin2);
        }
        cap.margin1 = edge.margin1;
        cap.margin2 = edge.margin2;
        cap.collidable = edge.collisible;  // JS `yu.vZ`
        cap.body_part = edge.body_part;
        cap.defense = edge.defense;
        cap.weight1 = (foe1 ? foe_model->bones[u1].mass : node_mass[u1]);
        cap.weight2 = (foe2 ? foe_model->bones[u2].mass : node_mass[u2]);
        capsules.push_back(std::move(cap));
    }
    wall_min = wall;
    wall_max = width_minus_wall;
}

float BodyState::clamp_to_bounds() {
    float max_dx = 0.0f;
    for (HitCapsule& cap : capsules) {
        for (Vec3* p : {&cap.p1, &cap.p2}) {
            if (p->x < wall_min) {
                const float d = wall_min - p->x;
                p->x = wall_min;
                max_dx = std::max(max_dx, d);
            } else if (p->x > wall_max) {
                const float d = p->x - wall_max;
                p->x = wall_max;
                max_dx = std::max(max_dx, d);
            }
            if (p->y < 0.0f) p->y = 0.0f;  // floor (JS `fha`: y >= 0)
        }
    }
    return max_dx;
}

const HitCapsule* BodyState::by_name(const std::string& name) const {
    for (const HitCapsule& c : capsules) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

float apply_impulse(const HitCapsule& hit_cap, const CapsuleHit& hit,
                    Vec3 impulse, float fighter_x, float wall,
                    float width_minus_wall, ImpulseResult& out) {
    out.impulse = impulse;
    out.node1_vec = Vec3{};
    out.node2_vec = Vec3{};
    out.node1_disp = 0.0f;
    out.node2_disp = 0.0f;
    out.clamped_dx = 0.0f;

    // JS `Bl.strike` (L582): b = min(1, |nJa - sx.ma| / rest_length) —
    // the REST length (`|pQ-iU|` = `yu.length`), not the current span.
    // `nJa` is `o$` (NOT `n$` — REVIEW A MED fix).
    const Vec3 to_hit = hit.o - hit_cap.p1;
    const float dist = std::sqrt(to_hit.dot(to_hit));
    const float rest = hit_cap.rest_length;
    float b = rest < 1e-6f ? 1.0f : dist / rest;
    b = std::min(1.0f, b);

    // Node displacement, full vector: d * (1-b)/w1 on node1, d * b/w2 on
    // node2 (`a.sx.XA(l)` / `a.Zs.XA(c)` — x, y AND z move).
    const float w1 = hit_cap.weight1 > 0.0f ? hit_cap.weight1 : 1.0f;
    const float w2 = hit_cap.weight2 > 0.0f ? hit_cap.weight2 : 1.0f;
    out.node1_vec = impulse * ((1.0f - b) / w1);
    out.node2_vec = impulse * (b / w2);
    out.node1_disp = out.node1_vec.x;
    out.node2_disp = out.node2_vec.x;

    // The demo tracks the fighter's COM x; the capsule midpoint shift is
    // the average of the two node displacements (the ragdoll COM follows
    // the weighted node average — `Dl.v6` L573).
    const float mid_disp = (out.node1_disp + out.node2_disp) * 0.5f;
    float new_x = fighter_x + mid_disp;

    // Bounds clamp (JS `Al.fha` L582): x in [wall, width-wall].
    if (new_x < wall) {
        out.clamped_dx = wall - new_x;
        new_x = wall;
    } else if (new_x > width_minus_wall) {
        out.clamped_dx = new_x - width_minus_wall;
        new_x = width_minus_wall;
    }
    return new_x;
}

}  // namespace sf2::scene

// Fight physics: hit detection (Bz), impulse (Bl.strike), bounds (Al.fha).
// Ported term-for-term from sf2.502f0946.js — line refs in README.md.

#include "scene/physics.hpp"

#include <algorithm>
#include <cmath>

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

// JS `Cz`: segment-segment intersection (2D), returns the crossing point.
// (Used by Bz only when one capsule radius is ~0.)
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
            return true;  // collinear overlap
        }
        return false;
    }
    const float hq = hh / q;
    if (!in_range(hq, 0.0f, 1.0f) || !in_range(ff / q, 0.0f, 1.0f)) return false;
    out.x = a.x + hq * (b.x - a.x);
    out.y = a.y + hq * (b.y - a.y);
    return true;
}

// JS `Ls(a,b,c,d,e,f,g)`: point-in-capsule (2D) test — whether the point
// d ± a*normal lies within the capsule defined by segment (e,f) with
// radius b. Returns true when the closest approach is within the radius.
bool in_capsule(float a, float b, const Line2& l, const Vec3& d,
                const Vec3& e, const Vec3& f) {
    if (std::fabs(a) <= b) {
        const float px = d.x - a * l.a;
        const float py = d.y - a * l.b;
        if ((in_range(px, e.x, f.x) || in_range(px, f.x, e.x)) &&
            (in_range(py, e.y, f.y) || in_range(py, f.y, e.y))) {
            return true;
        }
        return sq(d.x - e.x) + sq(d.y - e.y) <= b * b ||
               sq(d.x - f.x) + sq(d.y - f.y) <= b * b;
    }
    return false;
}

}  // namespace

// JS `Bz(a,b,c,d,e,f,g,h,k,l)` — the capsule-vs-capsule sweep test on the
// X/Y plane (the JS `Bz` uses the x and y axes; z is depth and ignored).
// `a,b` = attacker capsule endpoints (positions), `c` = attacker radius
// (+ margin), `d,e` = target capsule endpoints, `f` = target radius.
// The test resolves the closest approach of the two segment mid-lines and
// reports a hit when the distance drops below c+f. On hit, the hit point is
// written to `out.point` (the closest point on the attacker capsule).
bool capsule_capsule_overlap(const HitCapsule& atk, const HitCapsule& tgt,
                             CapsuleHit& out) {
    Vec3 a = atk.p1, b = atk.p2;
    Vec3 d = tgt.p1, e = tgt.p2;
    float c = atk.radius + atk.margin1 + atk.margin2;
    float f = tgt.radius + tgt.margin1 + tgt.margin2;
    c += f;  // JS: `c+=f` — sum of radii

    // Degenerate: attacker is a point (radius ~ 0) — fall back to the
    // segment-segment crossing test.
    if (f2(c)) {
        Vec3 p;
        if (seg_seg_hit(d, e, a, b, p)) {
            out.point = p;
            out.hit = true;
            return true;
        }
        return false;
    }

    // Line equations of both segments (on the X/Y plane).
    Line2 l_tgt = line_of(d, e);  // target line
    const float n = l_tgt.a * a.x + l_tgt.b * a.y + l_tgt.c;
    const float f2v = l_tgt.a * b.x + l_tgt.b * b.y + l_tgt.c;
    // Both attacker endpoints on the same side beyond the radius -> no hit.
    if (n * f2v >= 0 && c < std::fabs(n) && c < std::fabs(f2v)) return false;

    Line2 l_atk = line_of(a, b);
    const float q = l_atk.a * d.x + l_atk.b * d.y + l_atk.c;
    const float r = l_atk.a * e.x + l_atk.b * e.y + l_atk.c;
    if (q * r >= 0 && c < std::fabs(q) && c < std::fabs(r)) return false;

    if (q * r < 0 && n * f2v < 0) {
        // Segments cross: the hit point is the intersection.
        const float t = q / (q - r);
        out.point.x = e.x - d.x;
        out.point.y = e.y - d.y;
        out.point.z = e.z - d.z;
        out.point = out.point * t + d;
        out.hit = true;
        return true;
    }

    // Endpoint-vs-capsule checks (JS `Ls` for each endpoint against the
    // other segment, plus the line-distance tests).
    if (in_capsule(n, c, l_tgt, a, d, e)) { out.point = a; out.hit = true; return true; }
    if (in_capsule(f2v, c, l_tgt, b, d, e)) { out.point = b; out.hit = true; return true; }
    if (in_capsule(q, c, l_atk, d, a, b)) { out.point = d; out.hit = true; return true; }
    if (in_capsule(r, c, l_atk, e, a, b)) { out.point = e; out.hit = true; return true; }
    return false;
}

void BodyState::build(const Model& model, const std::vector<float>& pose_xy,
                      float wall, float width_minus_wall) {
    capsules.clear();
    // Node masses from the XML Mass attr (JS `Vc.weight` = Mass).
    node_mass.assign(model.bones.size(), 1.0f);
    for (std::size_t i = 0; i < model.bones.size(); ++i) {
        node_mass[i] = model.bones[i].mass;
    }
    for (const EdgeDef& edge : model.edges) {
        const int i1 = model.bone_by_name(edge.end1);
        const int i2 = model.bone_by_name(edge.end2);
        if (i1 < 0 || i2 < 0) continue;
        const std::size_t u1 = static_cast<std::size_t>(i1);
        const std::size_t u2 = static_cast<std::size_t>(i2);
        HitCapsule cap;
        cap.name = edge.name;
        cap.p1 = {pose_xy[u1 * 2], pose_xy[u1 * 2 + 1], 0.0f};
        cap.p2 = {pose_xy[u2 * 2], pose_xy[u2 * 2 + 1], 0.0f};
        cap.radius = edge.radius;
        cap.margin1 = edge.margin1;
        cap.margin2 = edge.margin2;
        cap.collidable = edge.collisible;  // JS `yu.vZ`
        cap.body_part = edge.body_part;
        cap.defense = edge.defense;
        cap.weight1 = node_mass[u1];
        cap.weight2 = node_mass[u2];
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
    out.node1_disp = 0.0f;
    out.node2_disp = 0.0f;
    out.clamped_dx = 0.0f;

    // JS `Bl.strike` (L582): b = how far along the capsule the hit landed
    // (0 = at endpoint 1, 1 = at endpoint 2 or beyond).
    const Vec3 seg = hit_cap.p2 - hit_cap.p1;
    const float len = std::sqrt(seg.dot(seg));
    const Vec3 to_hit = hit.point - hit_cap.p1;
    float b = len < 1e-6f ? 1.0f : std::sqrt(to_hit.dot(to_hit)) / len;
    b = std::min(1.0f, b);

    // Node displacement: d * (1-b)/w1 on node1, d * b/w2 on node2.
    const float w1 = hit_cap.weight1 > 0.0f ? hit_cap.weight1 : 1.0f;
    const float w2 = hit_cap.weight2 > 0.0f ? hit_cap.weight2 : 1.0f;
    out.node1_disp = impulse.x * (1.0f - b) / w1;
    out.node2_disp = impulse.x * b / w2;

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

// tests/test_effect_curve.cpp
//
// Conformance test for the <SimpleEffect> animation curve. These are
// properties the ORIGINAL engine satisfies by construction (see
// engine/format/effect_curve.hpp for the addresses), so any reimplementation
// has to satisfy them too:
//
//   * a segment starts at its own Value and ends at the NEXT point's Value,
//     for both the linear (Ease=0) and the parabolic (Ease!=0) branch — this
//     is exactly what Curve::recompute @ 0x1007f0c0 solves the coefficients
//     for, and it is what makes the curve continuous;
//   * the last segment closes the loop back onto the first point's Value;
//   * advancing by the total period returns to the start;
//   * Transparency values are a percentage, converted to a byte by *2.55.
//
// The real dojo effect is checked too, so the test fails if params.xml stops
// parsing or the light patch loses its curve.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "../engine/format/effect_curve.hpp"
#include "../engine/format/location_parser.hpp"
#include "check.hpp"

using resf2::format::EffectCurve;
using resf2::test::check;
using resf2::test::check_ge;
using resf2::test::check_near;

namespace {

std::string read_text(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// Walk one full loop of a curve and assert continuity at every segment
// boundary: the value just before a boundary must agree with the value just
// after it. A curve that jumps here would flicker on screen.
void check_curve_continuity(const EffectCurve& c, const std::string& what) {
    const std::size_t n = c.size();
    check_ge(static_cast<double>(n), 2.0, what + ": has at least two segments");
    if (n < 2) return;

    for (std::size_t i = 0; i < n; ++i) {
        const auto& p = c.points()[i];
        const float v_start = c.value_at(i, 0.0f);
        const float v_end = c.value_at(i, p.period);
        const float want_end = c.points()[(i + 1) % n].value;

        check_near(v_start, p.value, 0.01,
                   what + ": segment " + std::to_string(i) + " starts at its own Value");
        check_near(v_end, want_end, 0.01,
                   what + ": segment " + std::to_string(i) +
                       " ends at the next point's Value (loops on the last)");
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path root = (argc > 1) ? argv[1] : ".";

    // ------------------------------------------------------- linear segments
    {
        EffectCurve c;
        c.add_point(2.0f, 0.0f, 0.0f);
        c.add_point(2.0f, 100.0f, 0.0f);
        check_curve_continuity(c, "linear pair");
        // Ease=0 is a straight line, so the midpoint is the average.
        check_near(c.value_at(0, 1.0f), 50.0, 0.01, "linear segment interpolates linearly");
        check_near(c.value_at(1, 1.0f), 50.0, 0.01, "linear segment interpolates back down");
    }

    // ---------------------------------------------------- parabolic segments
    {
        EffectCurve c;
        c.add_point(2.0f, 0.0f, 1.0f);    // ease in
        c.add_point(2.0f, 100.0f, -1.0f); // ease out
        check_curve_continuity(c, "eased pair");
        // Curvature has to actually bend the segment away from the straight
        // line, otherwise Ease is silently ignored — which is what a naive
        // implementation does.
        //
        // Substituting the coefficients from Curve::recompute into the
        // parabola gives a midpoint of  V + (V'-V)/2 - Ease*P^2/4,  i.e. the
        // deviation from the straight line is exactly -Ease*P^2/4 and does NOT
        // depend on the values. So with P=2 and Ease=+-1 the bend is only one
        // unit out of a hundred — small, but pinned. (My first version of this
        // test demanded a larger deviation and failed against correct code.)
        const float mid_in = c.value_at(0, 1.0f);
        const float mid_out = c.value_at(1, 1.0f);
        check_near(mid_in, 50.0 - 1.0 * 4.0 / 4.0, 0.01,
                   "Ease=+1 bends the segment below the straight line by Ease*P^2/4");
        check_near(mid_out, 50.0 + 1.0 * 4.0 / 4.0, 0.01,
                   "Ease=-1 bends it above by the same amount");
        std::printf("eased midpoints: in=%.2f out=%.2f (linear would be 50)\n",
                    mid_in, mid_out);

        // Same law at a different period, to show the P^2 really is there and
        // the coefficients are not fitted to one case.
        EffectCurve d;
        d.add_point(4.0f, 0.0f, 1.0f);
        d.add_point(4.0f, 100.0f, 0.0f);
        check_curve_continuity(d, "eased long segment");
        check_near(d.value_at(0, 2.0f), 50.0 - 1.0 * 16.0 / 4.0, 0.01,
                   "the bend scales with the square of the period");
    }

    // ------------------------------------------------------------- the clock
    {
        EffectCurve c;
        c.add_point(1.0f, 10.0f, 0.0f);
        c.add_point(3.0f, 20.0f, 0.0f);
        c.add_point(2.0f, 30.0f, 0.0f);
        check_near(c.total_period(), 6.0, 0.001, "the loop is the sum of the periods");

        c.advance(0.5f);
        check(c.index() == 0, "half a second stays in the first segment");
        c.advance(1.0f);
        check(c.index() == 1, "crossing the first period moves to the second segment");
        // Back to the very start after one whole loop.
        c.advance(6.0f);
        check(c.index() == 1, "a full loop returns to the same segment");
        check_near(c.time(), 0.5, 0.001, "a full loop returns to the same phase");

        // A negative step is ignored by the original (0x1007f680 returns early).
        const float before = c.value();
        c.advance(-5.0f);
        check_near(c.value(), before, 0.001, "a negative dt does not move the curve");
    }

    // ------------------------------------------------------- Offset as phase
    {
        EffectCurve a, b;
        for (auto* c : {&a, &b}) {
            c->add_point(1.0f, 0.0f, 0.0f);
            c->add_point(1.0f, 100.0f, 0.0f);
        }
        b.advance(0.5f);   // what <Transparency Offset="0.5"> does
        a.advance(1.5f);
        b.advance(1.0f);
        check_near(a.value(), b.value(), 0.001,
                   "Offset is just an initial advance, not a separate mechanism");
    }

    // ------------------------------------------------------ percent -> alpha
    check(resf2::format::transparency_to_alpha(0.0f) == 0, "0% is fully transparent");
    check(resf2::format::transparency_to_alpha(100.0f) == 255, "100% is fully opaque");
    check(resf2::format::transparency_to_alpha(50.0f) == 127, "50% is half alpha (127, truncated)");

    // Values are clamped as they are added, not when they are used.
    {
        EffectCurve c;
        c.add_point(1.0f, -20.0f, 0.0f, /*clamp_percent=*/true);
        c.add_point(1.0f, 480.0f, 0.0f, /*clamp_percent=*/true);
        check_near(c.points()[0].value, 0.0, 0.001, "a negative Transparency clamps to 0");
        check_near(c.points()[1].value, 100.0, 0.001, "an over-range Transparency clamps to 100");
    }

    // ---------------------------------------------------------- the real dojo
    const auto params = root / "assets" / "locations" / "dojo" / "params.xml";
    const std::string xml = read_text(params);
    check(!xml.empty(), "dojo/params.xml is readable");
    if (!xml.empty()) {
        resf2::format::LocationParser lp;
        resf2::format::LocationData loc;
        check(lp.parse(xml, loc), "dojo/params.xml parses");

        const resf2::format::LayerImage* effect = nullptr;
        int effects = 0;
        for (const auto& layer : loc.layers)
            for (const auto& img : layer.images)
                if (!img.transparency.empty()) { effect = &img; ++effects; }

        check(effects == 1, "dojo declares exactly one animated <SimpleEffect>");
        if (effect) {
            check(effect->class_name == "layer_4",
                  "the animated effect is the light patch (layer_4)");
            check(effect->transparency.size() == 4,
                  "its Transparency curve has four points");
            check_near(effect->transparency.total_period(), 9.2, 0.01,
                       "the light breathes on a 9.2 s loop");
            check_curve_continuity(effect->transparency, "dojo layer_4");

            // Sweep the whole loop: the alpha must stay inside the range the
            // artist asked for. An eased segment can overshoot its endpoints
            // if the coefficients are wrong, which would show up as the patch
            // blowing out to white or vanishing.
            EffectCurve c = effect->transparency;
            float lo = c.value(), hi = c.value();
            for (int i = 0; i < 920; ++i) {
                c.advance(0.01f);
                lo = std::min(lo, c.value());
                hi = std::max(hi, c.value());
            }
            std::printf("dojo layer_4 alpha over one loop: %.1f%% .. %.1f%% "
                        "(points 45/75/55/75)\n", lo, hi);
            check(lo >= 40.0f, "the light never fades further than the artist asked");
            check(hi <= 80.0f, "the light never blows out past the artist's range");
        }
    }

    return resf2::test::summary();
}

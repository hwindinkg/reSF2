#pragma once

#include <cstddef>
#include <vector>

namespace resf2::format {

struct XmlNode;

// ---------------------------------------------------------------------------
// [ORIGINAL] The animation curve behind <SimpleEffect> in a location's
// params.xml. The original drives Transparency, OscillationX/Y and Rotation
// through one and the same curve type.
//
//   <SimpleEffect X="480" Y="0" Type="Picture" ClassName="layer_4" ...>
//     <Transparency Offset="0">
//       <Point Period="1.1" Value="45" Ease="-1"/>
//       <Point Period="3.5" Value="75" Ease="1"/>
//       ...
//     </Transparency>
//   </SimpleEffect>
//
// Reversed from ShadowFight2.s86:
//   Location::parseSimpleEffect  0x10145a90  reads Offset and the Point list
//   Curve::addPoint              0x1007f020  stores a 20-byte point, then
//   Curve::recompute             0x1007f0c0  derives the two coefficients
//   Curve::advance               0x1007f680  walks time to the current segment
//   SimpleEffect::update         0x1007f1f0  evaluates and applies
//
// A point is a SEGMENT, not a keyframe: `value` is the value at the segment's
// start and `period` is how long the segment lasts. The value at the end of a
// segment is the NEXT point's value, and the last segment wraps back to the
// first point's — so the whole thing is one closed loop whose length is the
// sum of the periods (9.2 s for dojo's light patch).
//
// recompute() precomputes, per segment, the coefficients of
//
//     Ease == 0 :  value(t) = c + b*t                    (linear)
//     Ease != 0 :  value(t) = c + Ease * (t + b)^2       (parabola)
//
// so that value(0) == this point's Value and value(period) == the next
// point's Value in both cases. Ease is the parabola's curvature: negative
// eases out of the segment, positive eases into it.
//
// Time is in SECONDS. The original ticks at 60 Hz and scales by 1/60 at
// 0x1007f1f0 before touching the curve, so a Period of 1.1 is 1.1 seconds.
// ---------------------------------------------------------------------------

struct EffectPoint {
    float period = 0.0f;   // <Point Period="...">, seconds
    float value = 0.0f;    // <Point Value="...">, value at the segment start
    float ease = 0.0f;     // <Point Ease="...">, parabola curvature
    // Derived by recompute(); never read from the XML.
    float b = 0.0f;
    float c = 0.0f;
};

class EffectCurve {
public:
    // clamp_percent mirrors 0x1007e6c0, which pins Transparency values into
    // [0, 100] as they are added. Oscillation and Rotation are not clamped.
    void add_point(float period, float value, float ease, bool clamp_percent = false);

    // 0x1007f680. Walks `dt_seconds` forward, crossing into later segments and
    // wrapping past the last one. A negative dt is ignored, as in the original.
    void advance(float dt_seconds);

    // The curve's value at the current time. 0 when there are no points.
    float value() const;

    bool empty() const { return points_.empty(); }
    std::size_t size() const { return points_.size(); }
    std::size_t index() const { return index_; }
    float time() const { return time_; }
    float total_period() const;
    const std::vector<EffectPoint>& points() const { return points_; }

    // The value of segment `i` at local time `t`, without touching the
    // curve's own clock. Exposed so conformance tests can walk a segment.
    float value_at(std::size_t i, float t) const;

private:
    void recompute();

    std::vector<EffectPoint> points_;
    float time_ = 0.0f;
    std::size_t index_ = 0;
};

// Fills `out` from a <Transparency> element: the Offset attribute seeds the
// phase (the original just calls advance(Offset) once) and every <Point> child
// becomes a segment. Shared by both params.xml parsers so they cannot drift.
void parse_transparency(const XmlNode& node, EffectCurve& out);

// [ORIGINAL] 0x1007f1f0 applies a Transparency value as
//     node->setOpacity((int)(value * 2.55) & 0xff)
// i.e. the curve carries a percentage and 255/100 converts it to a byte.
unsigned char transparency_to_alpha(float percent_value);

}  // namespace resf2::format

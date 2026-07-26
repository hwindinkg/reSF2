// engine/format/effect_curve.cpp
//
// See effect_curve.hpp for the derivation and the binary addresses.

#include "effect_curve.hpp"

#include <cstdlib>

#include "xml_doc.hpp"

namespace resf2::format {

namespace {
float to_float(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    return std::strtof(s.c_str(), nullptr);
}
}  // namespace

void EffectCurve::add_point(float period, float value, float ease, bool clamp_percent) {
    // [ORIGINAL] 0x1007f020 drops points with a negative period outright.
    if (period < 0.0f) return;
    if (clamp_percent) {
        // [ORIGINAL] 0x1007e6c0 clamps to [0, 100] BEFORE storing, so the
        // coefficients are derived from the clamped values.
        if (value <= 0.0f) value = 0.0f;
        else if (value >= 100.0f) value = 100.0f;
    }
    EffectPoint p;
    p.period = period;
    p.value = value;
    p.ease = ease;
    points_.push_back(p);
    recompute();
}

// [ORIGINAL] Curve::recompute @ 0x1007f0c0. Runs after EVERY added point, so
// the coefficients are always consistent with the current point list.
void EffectCurve::recompute() {
    const std::size_t n = points_.size();
    for (std::size_t i = 0; i < n; ++i) {
        EffectPoint& p = points_[i];
        const float v = p.value;
        // The segment after the last one is the first one: the curve loops.
        const float v_next = (i == n - 1) ? points_[0].value : points_[i + 1].value;
        const float period = p.period;
        if (period == 0.0f) {
            // A zero-length segment has no slope to speak of. The original
            // zeroes both coefficients and stops walking the list here.
            p.b = 0.0f;
            p.c = 0.0f;
            continue;
        }
        if (p.ease == 0.0f) {
            p.b = (v_next - v) / period;
            p.c = v;
        } else {
            p.b = ((v_next - v) - period * period * p.ease) / (p.ease * 2.0f * period);
            p.c = v - p.b * p.b * p.ease;
        }
    }
}

// [ORIGINAL] Curve::advance @ 0x1007f680.
void EffectCurve::advance(float dt_seconds) {
    if (dt_seconds < 0.0f) return;
    time_ += dt_seconds;
    if (points_.empty()) return;
    // The original's loop condition is a strict "current period < time", so a
    // time exactly equal to the segment length stays in that segment.
    while (points_[index_].period < time_) {
        time_ -= points_[index_].period;
        ++index_;
        if (index_ >= points_.size()) index_ = 0;
        // A list of zero-length segments would spin forever; the original
        // relies on the data never being like that, but a guard costs nothing.
        if (points_[index_].period == 0.0f) break;
    }
}

float EffectCurve::value_at(std::size_t i, float t) const {
    if (i >= points_.size()) return 0.0f;
    const EffectPoint& p = points_[i];
    // [ORIGINAL] the two branches inside SimpleEffect::update @ 0x1007f1f0.
    if (p.ease == 0.0f) return p.c + p.b * t;
    const float u = t + p.b;
    return p.c + u * u * p.ease;
}

float EffectCurve::value() const {
    if (points_.empty()) return 0.0f;
    return value_at(index_, time_);
}

float EffectCurve::total_period() const {
    float sum = 0.0f;
    for (const auto& p : points_) sum += p.period;
    return sum;
}

void parse_transparency(const XmlNode& node, EffectCurve& out) {
    for (const auto& child : node.children) {
        if (child.name != "Point") continue;
        out.add_point(to_float(child.attr("Period")),
                      to_float(child.attr("Value")),
                      to_float(child.attr("Ease")),
                      /*clamp_percent=*/true);
    }
    // [ORIGINAL] Offset is applied by advancing the finished curve once
    // (0x10145a90 calls the offset setter, which is Curve::advance). It must
    // therefore run AFTER the points exist, not before.
    const float offset = to_float(node.attr("Offset"));
    if (offset > 0.0f) out.advance(offset);
}

unsigned char transparency_to_alpha(float percent_value) {
    // [ORIGINAL] (int)(value * 2.55) & 0xff — a truncating cast, then masked.
    const int v = static_cast<int>(percent_value * 2.55f);
    return static_cast<unsigned char>(v & 0xff);
}

}  // namespace resf2::format

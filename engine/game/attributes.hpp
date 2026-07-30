#pragma once

// engine/game/attributes.hpp
//
// [ORIGINAL] Character attributes — `Model::getParameter` @ game+0x6275F4.
//
// This is the state the recovered damage formula needs
// (engine/game/damage_formula.hpp / Model::getTotalDamage @ game+0x4527B4).
// Without it every factor in that formula evaluates to 1.0 and the exponential
// attribute curve is indistinguishable from the old linear one.
//
// Verified shape of the original:
//
//   float getParameter(Model* self, const std::string* name) {
//       int value = 0;
//       if (map_lookup(self + 0x1C4, name, &value, 1, 0))
//           return (float)value;              // vcvt.f32.s32
//       warn("Parameter \"%s\" not found!");  // game+0x1CFA58
//       return -1e35f;                        // sentinel, NOT 0
//   }
//
// Three details that the implementation below reproduces deliberately:
//
//  1. Attributes are a NAME-KEYED MAP at `model+0x1C4`, not a fixed struct, so
//     unknown names are legal at runtime.
//  2. Values are stored as INTEGERS and converted to float on read. The
//     `%3.3f` in the game's own tracer is formatting, not precision.
//  3. A miss returns the sentinel `-1e35f` and logs. This is NOT the same as
//     the alignment-table lookup at game+0x60DF98, which defaults to 0.0.
//     Keeping the two distinct matters: feeding -1e35 into
//     `powf(2.0, delta/10)` underflows to 0 and zeroes the damage, which is
//     exactly the kind of divergence that would be hard to trace later.
//
// The attribute set is fixed by the tracer at game+0x628788, which reads
// exactly seven of them, and by <AlignTargetAttributes> in
// internalSettings.xml.

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace resf2::game {

// [ORIGINAL] Sentinel returned by getParameter for an absent attribute.
// Read from the literal at 0x8F67E658.
inline constexpr float kAttributeMissing = -1e35f;

// True when a value came back as "absent" rather than as a real number.
// The comparison is loose because the stored literal is -1.0000000409e+35.
inline bool attribute_is_missing(float v) { return v <= -1e34f; }

// The names the original's own tracer reads, in its order
// (game+0x628788 -> the "- WeaponDamage: %3.3f" block at 0x8F7A63A8).
inline const std::vector<std::string>& attribute_names() {
    static const std::vector<std::string> names = {
        "WeaponDamage",
        "UnarmedDamage",
        "BodyDefense",
        "HeadDefense",
        "RangedDamage",
        "MagicDamage",
        "RangedQuantity",
    };
    return names;
}

// [ORIGINAL] <AlignTargetAttributes> in internalSettings.xml, held by the
// original as a std::vector of 16-byte records at globals+0x534..0x538
// (112 bytes = 7 records; see PORT_GAPS.md). These are the ALIGNMENT TARGETS
// used to normalise an opponent's attributes, not a character's own stats.
//
// Note this list includes EnchantmentResistance and omits RangedQuantity, i.e.
// it is not the same set as attribute_names() above.
struct AlignTarget {
    const char* name;
    int value;
};
inline const std::vector<AlignTarget>& align_target_attributes() {
    static const std::vector<AlignTarget> targets = {
        {"WeaponDamage", 12},
        {"UnarmedDamage", 0},
        {"BodyDefense", 12},
        {"HeadDefense", 5},
        {"RangedDamage", 12},
        {"MagicDamage", 12},
        {"EnchantmentResistance", 12},
    };
    return targets;
}

// A character's attribute map — the `model+0x1C4` container.
class AttributeSet {
public:
    // [ORIGINAL] Values are stored as integers.
    void set(const std::string& name, int value) { values_[name] = value; }

    void add(const std::string& name, int delta) {
        auto it = values_.find(name);
        if (it == values_.end()) values_[name] = delta;
        else it->second += delta;
    }

    void clear() { values_.clear(); }

    [[nodiscard]] bool has(const std::string& name) const {
        return values_.find(name) != values_.end();
    }

    // [ORIGINAL] Model::getParameter @ game+0x6275F4. Returns the sentinel and
    // warns on a miss, exactly as the original does.
    [[nodiscard]] float get(const std::string& name) const {
        auto it = values_.find(name);
        if (it == values_.end()) {
            if (warn_on_missing_) {
                std::fprintf(stderr, "Parameter \"%s\" not found!\n", name.c_str());
            }
            return kAttributeMissing;
        }
        return static_cast<float>(it->second);
    }

    // Convenience for call sites that want a usable number: the sentinel is
    // folded to `fallback`. Use this ONLY where the original would have had a
    // real value; never paper over a genuinely missing attribute in the damage
    // path, because that changes behaviour rather than matching it.
    [[nodiscard]] float get_or(const std::string& name, float fallback) const {
        auto it = values_.find(name);
        return it == values_.end() ? fallback : static_cast<float>(it->second);
    }

    [[nodiscard]] int raw(const std::string& name, int fallback = 0) const {
        auto it = values_.find(name);
        return it == values_.end() ? fallback : it->second;
    }

    void set_warn_on_missing(bool on) { warn_on_missing_ = on; }

    [[nodiscard]] std::size_t size() const { return values_.size(); }

    [[nodiscard]] const std::unordered_map<std::string, int>& values() const {
        return values_;
    }

    // Seed every attribute the tracer reads to zero, so a character always has
    // the full set present and the damage path never sees the sentinel.
    void reset_to_zero() {
        values_.clear();
        for (const auto& n : attribute_names()) values_[n] = 0;
    }

    // [ORIGINAL] Equipment contributes additively: each item's WeaponDamage /
    // BodyDefense / ... are summed into the character's attributes. This is the
    // aggregation `list.xml` implies (items carry the same attribute names) and
    // what the damage formula then reads through getParameter.
    void add_item_contribution(float weapon_damage, float body_defense,
                               float head_defense, float ranged_damage,
                               float magic_damage) {
        add("WeaponDamage", static_cast<int>(weapon_damage));
        add("BodyDefense", static_cast<int>(body_defense));
        add("HeadDefense", static_cast<int>(head_defense));
        add("RangedDamage", static_cast<int>(ranged_damage));
        add("MagicDamage", static_cast<int>(magic_damage));
    }

    // Dump in the original tracer's format (game+0x628788), for comparison
    // against a capture from the real game.
    void dump(const char* label = "attributes") const {
        std::printf("%s:\n", label);
        for (const auto& n : attribute_names()) {
            const float v = get_or(n, 0.0f);
            std::printf("- %-15s %3.3f\n", (n + ":").c_str(),
                        static_cast<double>(v));
        }
    }

private:
    std::unordered_map<std::string, int> values_;
    // Off by default: the original's warning goes through a sink that checks a
    // global log flag first, so a release build is silent.
    bool warn_on_missing_ = false;
};

// [ORIGINAL] The attribute-difference term used by game+0x60E794's inner helper
// (game+0x60DF98): the attacker's damage attribute minus the defender's defense
// attribute. Note the alignment-table lookup defaults to 0.0 on a miss, which
// is why `get_or(..., 0.0f)` is correct HERE but not in getParameter.
inline float attribute_difference(const AttributeSet& attacker,
                                  const std::string& damage_attribute,
                                  const AttributeSet& defender,
                                  const std::string& defense_attribute) {
    const float atk = attacker.get_or(damage_attribute, 0.0f);
    const float def = defender.get_or(defense_attribute, 0.0f);
    return atk - def;
}

}  // namespace resf2::game

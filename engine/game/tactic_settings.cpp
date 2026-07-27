// engine/game/tactic_settings.cpp
//
// [ORIGINAL] tacticSettings.xml loader + the `cc` weight/curve model and the
// `jL` roulette-wheel pick. See tactic_settings.hpp for the algorithm and the
// PC line references it mirrors.

#include "tactic_settings.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>

#include <string>

#include "engine/format/xml_doc.hpp"

// The text/number helpers live at GLOBAL scope (defined in helpers.cpp,
// declared in game_clean.hpp:68-70) — not in resf2::game. Forward-declare the
// two we use so unqualified lookup inside the namespace falls through to them.
std::string read_text(const std::string& path);
float tof(const std::string& s, float def = 0.0f);

namespace resf2::game {

namespace fmt = resf2::format;

// ---------- TacticWeight ----------

// Gb(), the pre-curve score. [ORIGINAL] sf2_beautified.js:20096
float TacticWeight::score(const TacticContext& ctx) const {
    float a = 0;
    a += ctx.counter * counter_factor;                 // a.counter*b8
    a += ctx.damage * damage_factor;                   // a.Xb*l8
    a += (1.0f - ctx.health) * health_factor;          // (1-a.o1)*Uqa
    a += (1.0f - ctx.enemy_health) * enemy_health_factor;  // (1-a.q1)*wqa
    a += ctx.anim_frames * animation_frames_factor;    // a.xY*Toa
    a += ctx.magic_bullets * magic_bullet_factor;      // a.cl*Mra
    a += ctx.missile_bullets * missile_bullet_factor;  // a.K2*Zra
    a += ctx.hits * hit_factor;                        // a.tf*V8
    a += ctx.child_frames * child_frames_factor;       // a.pZ*Epa
    a += ctx.distance * distance_factor;               // a.Lya*kqa
    a += shift;                                         // + Fk
    // [HEURISTIC-TODO] The per-target AnimationFactors probe (a.a6.S5a) and
    // the ConditionalDesigionFactor term depend on the .atf tactic tables,
    // whose record layout is still unreversed (stride 858, deferred to Stage
    // 4.x). They are omitted here; a first pass runs on the base terms only.
    return a;
}

// QYa (Linear) / NYa (Exponential). [ORIGINAL] sf2_beautified.js:20113,:20117
float TacticWeight::apply_curve(float a) const {
    if (curve == Curve::kExponential) {
        // NYa
        if (a >= 0) return limit + (base - limit) * std::pow(2.0f, -a);
        return anti_limit + (base - anti_limit) * std::pow(2.0f, a);
    }
    // QYa (default)
    if (a >= 0) return base + (limit - base) * std::min(1.0f, a);
    return base + (anti_limit - base) * std::min(1.0f, -a);
}

float TacticWeight::evaluate(const TacticContext& ctx) const {
    return apply_curve(score(ctx));
}

// ---------- TacticDef ----------

// iCa(): first entry whose name matches, unnamed = catch-all default.
// [ORIGINAL] sf2_beautified.js:19930
const TacticWeight* TacticDef::weight_for(const std::string& animation) const {
    const TacticWeight* fallback = nullptr;
    for (const auto& [anim_name, w] : animation_weights) {
        if (anim_name.empty()) { fallback = &w; continue; }
        if (anim_name == animation) return &w;
    }
    return fallback;
}

// ---------- parsing ----------

namespace {

TacticWeight::Curve parse_curve(const std::string& s) {
    // arb(): "Exponential" -> exp; anything else (incl. "Linear", "") -> linear.
    // [ORIGINAL] sf2_beautified.js:arb
    return s == "Exponential" ? TacticWeight::Curve::kExponential
                              : TacticWeight::Curve::kLinear;
}

TacticWeight parse_weight(const fmt::XmlNode& n) {
    TacticWeight w;
    w.base = tof(n.attr("Base"));
    w.limit = tof(n.attr("Limit"));
    // XML uses both "AntiLimit" and "Antilimit" — accept either.
    {
        std::string al = n.attr("AntiLimit");
        if (al.empty()) al = n.attr("Antilimit");
        w.anti_limit = tof(al);
    }
    w.counter_factor = tof(n.attr("CounterFactor"));
    w.damage_factor = tof(n.attr("DamageFactor"));
    w.health_factor = tof(n.attr("HealthFactor"));
    w.enemy_health_factor = tof(n.attr("EnemyHealthFactor"));
    w.animation_frames_factor = tof(n.attr("AnimationFramesFactor"));
    w.child_frames_factor = tof(n.attr("ChildFramesFactor"));
    w.magic_bullet_factor = tof(n.attr("MagicBulletFactor"));
    w.missile_bullet_factor = tof(n.attr("MissileBulletFactor"));
    w.hit_factor = tof(n.attr("HitFactor"));
    w.distance_factor = tof(n.attr("DistanceFactor"));
    w.shift = tof(n.attr("Shift"));
    w.curve = parse_curve(n.attr("FactorType"));
    return w;
}

}  // namespace

bool TacticSettings::load(const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    // tacticSettings.xml lives directly under the asset root (unlike moves.xml,
    // which is in animations/), so `root` itself is searched first. The other
    // dirs mirror load_moves for layouts that keep it beside the animations.
    std::vector<std::filesystem::path> search_dirs = {
        root,
        root/"assets"/"animations",
        root/"animations",
        root/"assets",
    };

    std::string path;
    for (auto& dir : search_dirs) {
        auto p = dir / "tacticSettings.xml";
        if (std::filesystem::exists(p)) { path = p.string(); break; }
    }
    if (path.empty()) {
        std::printf("  tacticSettings.xml NOT FOUND!\n");
        return false;
    }

    auto xml = read_text(path);
    fmt::XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "[tactics] xml_doc parse error: %s\n",
                     doc.error().c_str());
        return false;
    }

    const auto* root_node = doc.root();  // synthetic "#document"
    if (!root_node) return false;

    // #document -> <TacticsSettings> -> <Tactics> -> <Tactic>*
    const auto* settings = root_node->first_child("TacticsSettings");
    if (!settings) settings = root_node;  // tolerate a missing wrapper
    const auto* tactics = settings->first_child("Tactics");
    if (!tactics) {
        std::fprintf(stderr, "[tactics] No <Tactics> section found\n");
        return false;
    }

    for (const auto* t : tactics->find_all("Tactic")) {
        TacticDef def;
        def.name = t->attr("Name");
        def.template_name = t->attr("Template");
        def.type = t->attr("Type");
        if (def.name.empty()) continue;

        if (const auto* weights = t->first_child("AnimationWeights")) {
            for (const auto& child : weights->children) {
                if (child.name != "Animation") continue;
                def.animation_weights.emplace_back(child.attr("Name"),
                                                   parse_weight(child));
            }
        }
        tactics_[def.name] = std::move(def);
    }

    resolve_templates();
    loaded_ = !tactics_.empty();
    std::printf("  tacticSettings.xml: %zu tactics\n", tactics_.size());
    return loaded_;
}

// Template="X" pulls in X's animation weights beneath the local ones. Local
// entries take precedence: they are matched first by weight_for()'s ordering,
// so we append the inherited entries after the local list.
void TacticSettings::resolve_templates() {
    // Bounded passes so a Template cycle can't loop forever.
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (auto& [name, def] : tactics_) {
            if (def.template_name.empty()) continue;
            auto it = tactics_.find(def.template_name);
            if (it == tactics_.end()) continue;
            const TacticDef& base = it->second;
            // Skip if the base itself still needs resolving this pass.
            if (!base.template_name.empty() &&
                tactics_.count(base.template_name)) {
                changed = true;
                continue;
            }
            for (const auto& entry : base.animation_weights) {
                // Don't shadow a locally-declared animation of the same name.
                bool have = false;
                for (const auto& mine : def.animation_weights) {
                    if (mine.first == entry.first) { have = true; break; }
                }
                if (!have) def.animation_weights.push_back(entry);
            }
            def.template_name.clear();  // resolved
            changed = true;
        }
        if (!changed) break;
    }
}

const TacticDef* TacticSettings::tactic(const std::string& name) const {
    auto it = tactics_.find(name);
    return it == tactics_.end() ? nullptr : &it->second;
}

// ---------- selection ----------

int TacticSettings::choose_debug(const TacticDef& tactic,
                                 const std::vector<std::string>& candidates,
                                 const TacticContext& ctx,
                                 std::vector<float>& out_weights) const {
    // jL(): accumulate weights, draw in [0,sum), walk until negative.
    // [ORIGINAL] sf2_beautified.js:19910
    out_weights.clear();
    out_weights.reserve(candidates.size());
    float sum = 0;
    for (const auto& c : candidates) {
        const TacticWeight* w = tactic.weight_for(c);
        float value = w ? w->evaluate(ctx) : 0.0f;
        if (value < 0) value = 0;  // negative weights can't win the draw
        out_weights.push_back(value);
        sum += value;
    }
    if (sum <= 0) return -1;

    // Da.pg.s4(d): uniform in [0, sum).
    float g = (float)std::rand() / (float)RAND_MAX * sum;
    for (size_t i = 0; i < candidates.size(); ++i) {
        g -= out_weights[i];
        if (g < 0) return (int)i;
    }
    return (int)candidates.size() - 1;  // guard against fp rounding
}

int TacticSettings::choose(const TacticDef& tactic,
                           const std::vector<std::string>& candidates,
                           const TacticContext& ctx) const {
    std::vector<float> scratch;
    return choose_debug(tactic, candidates, ctx, scratch);
}

}  // namespace resf2::game

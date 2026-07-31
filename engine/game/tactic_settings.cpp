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

namespace {

// D(A)/C(A)/H(A) lookup for the probe: an animation absent from the memory
// sums reads as 0.0f — neutral-by-zero (ADR-005 R3), matching the binary's
// zero-initialized records (ATF_RECORD_858.md §3).
float memory_sum(const std::unordered_map<std::string, float>& sums,
                 const std::string& animation) {
    const auto it = sums.find(animation);
    return it == sums.end() ? 0.0f : it->second;
}

}  // namespace

// Gb(), the pre-curve score. [ORIGINAL] sf2_beautified.js:20096; term order
// verified against FUN_8f44ac78 @ 0x8F44AC78 (R2 GREEN,
// reverse/analysis/VERIFY_FUN_8f44ac78.md §3): damage*DamageFactor FIRST,
// then counter*CounterFactor — the JS port lists counter first.
float TacticWeight::score(const TacticContext& ctx) const {
    float a = 0;
    a += ctx.damage * damage_factor;                   // [ORIGINAL] 0x8F44AC78: damage first
    a += ctx.counter * counter_factor;                 // ... then counter
    a += (1.0f - ctx.health) * health_factor;          // (1-a.o1)*Uqa
    a += (1.0f - ctx.enemy_health) * enemy_health_factor;  // (1-a.q1)*wqa
    a += ctx.anim_frames * animation_frames_factor;    // a.xY*Toa
    a += ctx.magic_bullets * magic_bullet_factor;      // a.cl*Mra
    a += ctx.missile_bullets * missile_bullet_factor;  // a.K2*Zra
    a += ctx.hits * hit_factor;                        // a.tf*V8
    a += ctx.child_frames * child_frames_factor;       // a.pZ*Epa
    a += ctx.distance * distance_factor;               // a.Lya*kqa
    a += shift;                                         // + Fk
    // [ORIGINAL] FUN_8f44ac78 @ 0x8F44AC78 (R2 GREEN,
    // reverse/analysis/VERIFY_FUN_8f44ac78.md §3): the a.a6.S5a probe is
    // INLINE, one term per <AnimationFactors> child:
    //   a += child.DamageFactor*D(A) + child.CounterFactor*C(A)
    //      + child.HitFactor*H(A)                          (damage first)
    // D(A)/C(A)/H(A) are the decayed per-animation memory sums (TacticMemory,
    // Phase C); absent sums read as 0.0f -> neutral-by-zero (ADR-005 R3).
    // There is NO scalar AnimationFactors coefficient in the native parse.
    for (const AnimationFactorEntry& child : animation_factor_entries) {
        const float d = memory_sum(ctx.anim_memory.damage, child.animation);
        const float c = memory_sum(ctx.anim_memory.counter, child.animation);
        const float h = memory_sum(ctx.anim_memory.hits, child.animation);
        a += child.factors.damage_factor * d
           + child.factors.counter_factor * c
           + child.factors.hit_factor * h;
    }
    // [EXTENSION POINT] ConditionalDesigionFactor — BLOCKED pending binary
    // evidence (0 matches in ARM string table, PORT_GAPS.md:145-148). If a
    // future @reverser pass finds the real key name, add its term HERE, after
    // `shift`, and document the string-table address. Do NOT add it from the
    // JS-port name alone.
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
    // R2 (GAP-4 B4): the native parser reads 15 scalar attributes — there is
    // NO scalar "AnimationFactors" attribute (FUN_8f44c474); the name exists
    // only as a child *element*. Those children are the per-target probe
    // entries consumed inline by score().
    for (const auto& child : n.children) {
        if (child.name != "AnimationFactors") continue;
        TacticWeight::AnimationFactorEntry entry;
        entry.animation = child.attr("Animation");
        entry.factors = parse_weight(child);
        w.animation_factor_entries.push_back(std::move(entry));
    }
    w.curve = parse_curve(n.attr("FactorType"));
    return w;
}

// <DistanceError>/<FrameError>/<ResponseDelay>/<EnemyResponseDelay> are
// <Min Base/><Max Base/> ranges in this dump.
TacticDef::MinMax parse_minmax(const fmt::XmlNode& n) {
    TacticDef::MinMax r;
    // [HEURISTIC-TODO] Min/Max could in principle be full curves; this dump
    // only ever uses a bare Base — read that until evidence differs.
    if (const auto* mn = n.first_child("Min")) r.min = tof(mn->attr("Base"));
    if (const auto* mx = n.first_child("Max")) r.max = tof(mx->attr("Base"));
    return r;
}

// The 20 decision-level keys (ADR-005 D2): sibling elements of
// <AnimationWeights> per the confirmed assets/tacticSettings.xml schema.
void parse_decision_keys(const fmt::XmlNode& t, TacticDef& def) {
    // <UseDefense> — presence = stage-1 gate, 3 sub-chance curves.
    if (const auto* ud = t.first_child("UseDefense")) {
        def.use_defense = true;
        if (const auto* c = ud->first_child("CounterAttackChance"))
            def.counter_attack_chance = parse_weight(*c);
        if (const auto* c = ud->first_child("DodgeChance"))
            def.dodge_chance = parse_weight(*c);
        if (const auto* c = ud->first_child("BlockChance"))
            def.block_chance = parse_weight(*c);
        def.declared_keys |= TacticDef::kUseDefenseKey;
    }

    // Standalone chance curves.
    const struct { const char* name; TacticWeight TacticDef::* slot; unsigned key; } kChances[] = {
        {"UseSafeAttackChance",     &TacticDef::use_safe_attack_chance,     TacticDef::kUseSafeAttackKey},
        {"TableAttackChance",       &TacticDef::table_attack_chance,        TacticDef::kTableAttackKey},
        {"CautiousMovementsChance", &TacticDef::cautious_movements_chance,  TacticDef::kCautiousMovementsKey},
        {"DodgeMissilesChance",     &TacticDef::dodge_missiles_chance,      TacticDef::kDodgeMissilesKey},
        {"DodgeMagicChance",        &TacticDef::dodge_magic_chance,         TacticDef::kDodgeMagicKey},
    };
    for (const auto& ch : kChances) {
        if (const auto* c = t.first_child(ch.name)) {
            def.*(ch.slot) = parse_weight(*c);
            def.declared_keys |= ch.key;
        }
    }

    // <QuickAttacks>/<Evades> — per-animation chance entries, order kept.
    if (const auto* qa = t.first_child("QuickAttacks")) {
        for (const auto& child : qa->children) {
            if (child.name != "QuickAttackChance") continue;
            def.quick_attack_chances.emplace_back(child.attr("Animation"),
                                                  parse_weight(child));
        }
        def.declared_keys |= TacticDef::kQuickAttacksKey;
    }
    if (const auto* ev = t.first_child("Evades")) {
        for (const auto& child : ev->children) {
            if (child.name != "EvadeChance") continue;
            def.evade_chances.emplace_back(child.attr("Animation"),
                                           parse_weight(child));
        }
        def.declared_keys |= TacticDef::kEvadesKey;
    }

    // Min/Max ranges.
    const struct { const char* name; TacticDef::MinMax TacticDef::* slot; unsigned key; } kRanges[] = {
        {"DistanceError",      &TacticDef::distance_error,       TacticDef::kDistanceErrorKey},
        {"FrameError",         &TacticDef::frame_error,          TacticDef::kFrameErrorKey},
        {"ResponseDelay",      &TacticDef::response_delay,       TacticDef::kResponseDelayKey},
        {"EnemyResponseDelay", &TacticDef::enemy_response_delay, TacticDef::kEnemyResponseDelayKey},
    };
    for (const auto& rg : kRanges) {
        if (const auto* c = t.first_child(rg.name)) {
            def.*(rg.slot) = parse_minmax(*c);
            def.declared_keys |= rg.key;
        }
    }

    // <ExpectedWait> — animation-weight list, same shape as AnimationWeights.
    if (const auto* ew = t.first_child("ExpectedWait")) {
        for (const auto& child : ew->children) {
            if (child.name != "Animation") continue;
            def.expected_wait.emplace_back(child.attr("Name"),
                                           parse_weight(child));
        }
        def.declared_keys |= TacticDef::kExpectedWaitKey;
    }

    // <Memory Strikes=".." RoundFactor=".."/>; a `Memory` depth attribute is
    // absent in this dump -> 0 (ring-depth source flagged R5).
    if (const auto* mem = t.first_child("Memory")) {
        def.strikes = std::atoi(mem->attr("Strikes").c_str());
        def.round_factor = tof(mem->attr("RoundFactor"));
        def.memory = std::atoi(mem->attr("Memory").c_str());
        def.declared_keys |= TacticDef::kMemoryKey;
    }
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

        // [ORIGINAL] decision types: Tabular (default, incl. absent Type)
        // and ExpectedWait; the binary rejects everything else with
        // "Strange tactic type: %s" (PORT_GAPS.md:168-169). Real-data
        // consequence (grep-verified 2026-07-31): Beginner (Type="Random")
        // is skipped — 13 <Tactic> elements -> 12 unique names -> 11 loaded.
        if (!def.type.empty() && def.type != "Tabular" &&
            def.type != "ExpectedWait") {
            std::printf("Strange tactic type: %s\n", def.type.c_str());
            continue;
        }

        if (const auto* weights = t->first_child("AnimationWeights")) {
            for (const auto& child : weights->children) {
                if (child.name != "Animation") continue;
                def.animation_weights.emplace_back(child.attr("Name"),
                                                   parse_weight(child));
            }
        }
        parse_decision_keys(*t, def);
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
            // Decision-level keys inherit by the same rule: a key declared
            // locally wins; otherwise the base's value (if it declared the
            // key) is copied. Presence-based keys (UseDefense) inherit only
            // when not locally present.
            const auto inherit = [&](unsigned key, const auto& copy) {
                if ((def.declared_keys & key) == 0 &&
                    (base.declared_keys & key) != 0) {
                    copy();
                    def.declared_keys |= key;
                }
            };
            inherit(TacticDef::kUseDefenseKey, [&] {
                def.use_defense = base.use_defense;
                def.counter_attack_chance = base.counter_attack_chance;
                def.dodge_chance = base.dodge_chance;
                def.block_chance = base.block_chance;
            });
            inherit(TacticDef::kUseSafeAttackKey, [&] {
                def.use_safe_attack_chance = base.use_safe_attack_chance;
            });
            inherit(TacticDef::kTableAttackKey, [&] {
                def.table_attack_chance = base.table_attack_chance;
            });
            inherit(TacticDef::kCautiousMovementsKey, [&] {
                def.cautious_movements_chance = base.cautious_movements_chance;
            });
            inherit(TacticDef::kDodgeMissilesKey, [&] {
                def.dodge_missiles_chance = base.dodge_missiles_chance;
            });
            inherit(TacticDef::kDodgeMagicKey, [&] {
                def.dodge_magic_chance = base.dodge_magic_chance;
            });
            inherit(TacticDef::kQuickAttacksKey, [&] {
                def.quick_attack_chances = base.quick_attack_chances;
            });
            inherit(TacticDef::kEvadesKey, [&] {
                def.evade_chances = base.evade_chances;
            });
            inherit(TacticDef::kDistanceErrorKey, [&] {
                def.distance_error = base.distance_error;
            });
            inherit(TacticDef::kFrameErrorKey, [&] {
                def.frame_error = base.frame_error;
            });
            inherit(TacticDef::kResponseDelayKey, [&] {
                def.response_delay = base.response_delay;
            });
            inherit(TacticDef::kEnemyResponseDelayKey, [&] {
                def.enemy_response_delay = base.enemy_response_delay;
            });
            inherit(TacticDef::kExpectedWaitKey, [&] {
                def.expected_wait = base.expected_wait;
            });
            inherit(TacticDef::kMemoryKey, [&] {
                def.strikes = base.strikes;
                def.round_factor = base.round_factor;
                def.memory = base.memory;
            });
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
                                 std::vector<float>& out_weights,
                                 RngSource rng) const {
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

    // Da.pg.s4(d): uniform in [0, sum). rng() honors the std::rand range
    // contract ([0, RAND_MAX]) so the draw formula is unchanged (ADR-005 D4).
    float g = (float)rng() / (float)RAND_MAX * sum;
    for (size_t i = 0; i < candidates.size(); ++i) {
        g -= out_weights[i];
        if (g < 0) return (int)i;
    }
    return (int)candidates.size() - 1;  // guard against fp rounding
}

int TacticSettings::choose_debug(const TacticDef& tactic,
                                 const std::vector<std::string>& candidates,
                                 const TacticContext& ctx,
                                 std::vector<float>& out_weights) const {
    // Production binding: std::rand preserves the baseline RNG sequence.
    return choose_debug(tactic, candidates, ctx, out_weights,
                        RngSource(std::rand));
}

int TacticSettings::choose(const TacticDef& tactic,
                           const std::vector<std::string>& candidates,
                           const TacticContext& ctx,
                           RngSource rng) const {
    std::vector<float> scratch;
    return choose_debug(tactic, candidates, ctx, scratch, std::move(rng));
}

int TacticSettings::choose(const TacticDef& tactic,
                           const std::vector<std::string>& candidates,
                           const TacticContext& ctx) const {
    std::vector<float> scratch;
    return choose_debug(tactic, candidates, ctx, scratch,
                        RngSource(std::rand));
}

}  // namespace resf2::game

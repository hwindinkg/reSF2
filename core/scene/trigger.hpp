#pragma once

// Trigger bus (PERKS_STATIC §5.3/§5.6, WEA_STATIC): the `bc` bus + `Kw`
// per-model entries + `Fw`/`jp` action queue. Verbatim flow:
//   `Gj(model, slot, withInfo)` (L1364): per `ej` entry → `h8a(slot)` list
//     → scratch `xG{type,info,BI,qk,Jd}` → `v_a` per trigger → `Qh()`.
//   `v_a` (L1364): `c8a(Wa)` must exist+enabled → `bBa` Fw entry →
//     `(info==null || t0a(info)) && Axa(model, UO)` → `UKa` (queue `lY`).
//   `UKa` (L1366-1367): `jp{model=e6a, qk, action, qw=false, Iv=0, Uf=0,
//     Uf=frames}`; immediate `lF` only via `Pob` (Provoke path).
//   `Qh` drains `lY`; `ia` ticks `Uf/Iv` + `JNa` revert on expiry (L1290/
//     L1298-1299) + `ModExpires` publish `Gj(…,14)` (L1299); `Z_a`/`pP`
//     cleanup (L1300-1301); `FE` re-fire (L1366) is OPEN (no call site
//     wired — see below).
// Slots ARE event type ids (`Ej` matches `Hc.type==slot`, L1367):
//   1 RoundStageStart 2 EveryFrame 3 Style 4 Combo 5 HitPreCrit
//   6 HitPostCrit 7 PostHit 8 MagicCharged 9 AnimStart 10 AnimEnd
//   11 AnimInterrupted 12 IntervalStart 13 IntervalEnd 14 ModExpires
//   15 AreaEnter 16 AreaExit.
// Publishers wired: hit `Sba` slots 6 (post-crit, Damage=0 — `Bb.Zi`
// reset at strike start) + 7 (post-hit, Damage=base); EveryFrame slot 2
// per fighter tick; RoundStageStart slot 1 on phase change; Interval
// 12/13 on edge detect; ModExpires 14 on mod expiry. NOT wired (OPEN,
// parsed but never fired): 3 Style, 4 Combo (battle-end `TYa→Gj(a,4)`
// noted), 5 HitPreCrit (`Egb` site unknown), 8 MagicCharged (no magic),
// 9/10/11 anim (plumbing cost), 15/16 area (`rR` bounds OPEN).
// Log-only exec types (JS applies; needs magic/presentation/timing
// systems — REVIEW B LOW): StealMagicMod(1 shipped use), SlowModel(3),
// ChangeModelColor(3), SetCooldown(4), SetDarkness(3), MoveModel(1),
// AddBullets(2), AddMagicCharge(1); JNa revert log-only for 27/28/29;
// Bullets/MagicCharge conditions read 0 (no bh/dO, 2 perks). (`rR` bounds OPEN).

#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <pugixml.hpp>

#include "scene/perks.hpp"
#include "scene/physics.hpp"

namespace sf2::scene {

// --- event type ids (`Ac.parse` L2C8) -----------------------------------
inline constexpr int kEvRoundStage = 1;
inline constexpr int kEvEveryFrame = 2;
inline constexpr int kEvStyle = 3;
inline constexpr int kEvCombo = 4;
inline constexpr int kEvHitPreCrit = 5;
inline constexpr int kEvHitPostCrit = 6;
inline constexpr int kEvPostHit = 7;
inline constexpr int kEvMagicCharged = 8;
inline constexpr int kEvAnimStart = 9;
inline constexpr int kEvAnimEnd = 10;
inline constexpr int kEvAnimIntr = 11;
inline constexpr int kEvIntervalStart = 12;
inline constexpr int kEvIntervalEnd = 13;
inline constexpr int kEvModExpires = 14;
inline constexpr int kEvAreaEnter = 15;
inline constexpr int kEvAreaExit = 16;

inline int event_type_by_name(const std::string& n) {
    if (n == "RoundStageStart") return kEvRoundStage;
    if (n == "EveryFrame") return kEvEveryFrame;
    if (n == "Style") return kEvStyle;
    if (n == "Combo") return kEvCombo;
    if (n == "HitPreCrit") return kEvHitPreCrit;
    if (n == "HitPostCrit") return kEvHitPostCrit;
    if (n == "PostHit") return kEvPostHit;
    if (n == "MagicCharged" || n == "PerkEventMagicCharged") return kEvMagicCharged;
    if (n == "AnimationStart") return kEvAnimStart;
    if (n == "AnimationEnd") return kEvAnimEnd;
    if (n == "AnimationInterrupted") return kEvAnimIntr;
    if (n == "IntervalStart") return kEvIntervalStart;
    if (n == "IntervalEnd") return kEvIntervalEnd;
    if (n == "ModExpires") return kEvModExpires;
    if (n == "AreaEnter") return kEvAreaEnter;
    if (n == "AreaExit") return kEvAreaExit;
    return 0;
}

// `Jf.$ea`: null/""/Me→1, Enemy→2, else 0.
inline int ob_by_player(const std::string& p) {
    if (p.empty() || p == "Me") return 1;
    if (p == "Enemy") return 2;
    return 0;
}

// `Jf.OBa`: StartStance→1, Fight→2, EndStance→3.
inline int stage_by_name(const std::string& n) {
    if (n == "StartStance") return 1;
    if (n == "Fight") return 2;
    if (n == "EndStance") return 3;
    return 0;
}

// Style level names (`ZBa`, L1306-1307): index = Gr level 0..5.
inline const char* style_name_by_level(int level) {
    switch (level) {
        case 0: return "Turtle";
        case 1: return "Hard";
        case 2: return "Brutal";
        case 3: return "Aggressive";
        case 4: return "Crazy";
        default: return "Fantastic";
    }
}

// --- trigger model (Iw/Hc/ec/Ma, L1359/L2C8/L1302/L1374) -----------------
struct TrigEvent {
    int type = 0;  // 1..16 (0 = unknown/dropped at load)
    int ob = 1;    // `Jf.Ob` (default 1 = Me)
    bool negate = false;  // `Jf.cb` (`Not="1"`)
    // `Hh` fields (parse defaults: Xi/anim "", mR/ow/vc -1, DP/CP -1):
    std::string defense, animation;
    int block = -1, critical = -1, shock = -1;
    double dmg_min = -1.0, dmg_max = -1.0;
    int step = 0;  // `Cp.Step` (`u.I`, default 0 = every frame)
    int stage = 0;  // `Ep.Je` (`OBa`, 0 = any)
    std::string interval;  // `Lj` Name (""/absent = wildcard)
    int interval_type = 0;  // `Lj` Type via `fe.G0` (0 = wildcard)
    std::string mod_name, mod_ns;  // `Dp` Name/Namespace
};

struct TrigCond {
    std::string kind;  // ec tag: Random/Style/Combo/RoundStage/...
    int ob = 1;  // `ec.Lh` side select (default 1 = own)
    bool negate = false;
    std::map<std::string, std::string> s;  // Name/Type/Subtype/Min/Max/...
    double chance = 0.0;  // Random (0..1 fraction; `cT(ou*100)`)
    std::string op;  // Operator Or/And
    std::vector<TrigCond> nested;
};

// One trigger (`Iw`): the perk that owns it (`Wa`), gate list, action defs.
struct PerkTrigger {
    std::string name;  // `<Trigger Name>` ("" when absent; Provoke matches it)
    std::string perk;  // owning perk name (`Wa`)
    bool enabled = true;  // `Kw.c8a` gate (`Srb` disables)
    std::vector<TrigEvent> events;  // `Hc`
    std::vector<TrigCond> conds;  // `rb`
    std::vector<PerkAction> actions;
};

// One perk def (`Be`): Set-var map + triggers (+ Template for §5.1 merge).
struct PerkDef {
    std::string name;
    std::string templ;  // `Template=` (triggers merged from the named perk)
    std::map<std::string, double> set_num;
    std::map<std::string, std::string> set_str;
    std::vector<PerkTrigger> triggers;
};

// `mg` event vars (`Sba` stamps Defense/Animation/Critical/Shock/Block/
// Damage; `Cp` StepFrame; `Lj` Interval; `JNa` ModExpires/Namespace/
// ParentPerk).
struct TrigVars {
    std::map<std::string, double> num;
    std::map<std::string, std::string> str;
};

// Condition context for ONE side (`Ae` analog). Magic/bullets stay 0
// (no magic system — OPEN). `hp` is ABSOLUTE (`gd`, L1310-1311 — not a
// ratio); `hit_dmg` is the in-flight `mg.Damage` for `?Hit[].Damage`;
// `q3` is the side's `Fc.Q3` store (`SetModVariable`, `dka`).
struct CondCtx {
    int style_level = 0;
    int combo = 0;
    int stage = 0;
    std::string anim;
    std::vector<std::pair<std::string, int>> intervals;  // (name, G0 type)
    double hp = 1.0;
    double hit_dmg = 0.0;
    int bullets = 0;  // wd.bh (MagicBullet count)
    int raid = 0;     // wd.dO (RaidChargeBullet count)
    double charge = 0.0;  // wd.my [0,1] (MagicCharge)
    std::map<std::string, double> q3;
    std::vector<std::string> items;  // equipped item names
    int round = 1;
    double pain = 0.0;
    bool in_area = false;
    std::set<std::string> mods;  // live mod names (`ModExists`)
    std::map<std::string, std::string> mod_ns;  // mod name -> namespace (`YZa`)
    std::function<double()> draw01;
};

// `Oc.Ag` Min/Max range (`Gw`, L1301); `xE(a)` = within, open ends pass.
inline bool range_has_min(const TrigCond& c) {
    return c.s.find("Min") != c.s.end();
}
inline bool range_has_max(const TrigCond& c) {
    return c.s.find("Max") != c.s.end();
}
inline bool range_check(const TrigCond& c, double v) {
    if (range_has_min(c)) {
        try {
            if (v < std::stod(c.s.at("Min"))) return false;
        } catch (...) {
            return false;
        }
    }
    if (range_has_max(c)) {
        try {
            if (v > std::stod(c.s.at("Max"))) return false;
        } catch (...) {
            return false;
        }
    }
    return true;
}

// --- `kp` comparison conditions (REVIEW B HIGH fix) --------------------
// `ec.create` routes expression tags to `kp` (L1302): the tag IS the
// comparison (`?Compare[Value1,Value2,Tag]`), evaluated by the `Qa`
// engine (`Uha`, L2362). Shipped tags: Less/LessEqual/Greater/
// GreaterEqual/Equal (86 uses). Operands: numeric literals, `_Var`
// (Set-substituted at load), `?PlayerParameter[Me|Enemy].Field`,
// `?Hit[].Damage`, `?Variable[name]` (side Q3), `?Abs[x]`, and
// +,-,*,/,(,) combinations. Unknown fields/functions fail closed
// (same as the old always-false, but numerics now evaluate).
// Supported `?PlayerParameter` fields: Health (absolute `gd`),
// MagicBullet (0 — no magic system, OPEN); DamageConverter and anything
// else fail closed (OPEN).

struct ExprParse {
    const std::string& s;
    std::size_t pos = 0;
    const CondCtx& owner;
    const CondCtx& foe;
    bool fail = false;

    void skip() {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    bool eat(char c) {
        skip();
        if (pos < s.size() && s[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    std::optional<double> parse_expr() {
        auto v = parse_term();
        if (!v) return std::nullopt;
        for (;;) {
            if (eat('+')) {
                auto r = parse_term();
                if (!r) return std::nullopt;
                *v += *r;
            } else if (eat('-')) {
                auto r = parse_term();
                if (!r) return std::nullopt;
                *v -= *r;
            } else {
                return v;
            }
        }
    }
    std::optional<double> parse_term() {
        auto v = parse_factor();
        if (!v) return std::nullopt;
        for (;;) {
            if (eat('*')) {
                auto r = parse_factor();
                if (!r) return std::nullopt;
                *v *= *r;
            } else if (eat('/')) {
                auto r = parse_factor();
                if (!r || *r == 0.0) return std::nullopt;
                *v /= *r;
            } else {
                return v;
            }
        }
    }
    std::optional<double> parse_factor() {
        skip();
        if (eat('-')) {
            auto v = parse_factor();
            if (!v) return std::nullopt;
            return -*v;
        }
        if (eat('(')) {
            auto v = parse_expr();
            if (!v || !eat(')')) return std::nullopt;
            return v;
        }
        if (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) {
            std::size_t len = 0;
            try {
                const double d = std::stod(s.substr(pos), &len);
                if (len == 0) return std::nullopt;
                pos += len;
                return d;
            } catch (...) {
                return std::nullopt;
            }
        }
        if (pos < s.size() && s[pos] == '?') {
            return parse_qref();
        }
        if (pos < s.size() && (s[pos] == '_' || std::isalpha(static_cast<unsigned char>(s[pos])))) {
            // Bare `_Var` (unresolved at load — missing Set) fails closed.
            return std::nullopt;
        }
        return std::nullopt;
    }
    std::optional<double> parse_qref() {
        // `?PlayerParameter[Me|Enemy].Field`, `?Hit[].Damage`,
        // `?Variable[name]`, `?Abs[expr]`.
        ++pos;  // '?'
        std::string name;
        while (pos < s.size() &&
               (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
            name += s[pos++];
        }
        skip();
        std::string arg;
        if (eat('[')) {
            std::size_t depth = 1;
            const std::size_t start = pos;
            while (pos < s.size() && depth > 0) {
                if (s[pos] == '[') ++depth;
                if (s[pos] == ']') --depth;
                ++pos;
            }
            if (depth != 0) return std::nullopt;
            arg = s.substr(start, pos - start - 1);
        }
        std::string field;
        if (eat('.')) {
            while (pos < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
                field += s[pos++];
            }
        }
        if (name == "Abs") {
            ExprParse inner{arg, 0, owner, foe};
            auto v = inner.parse_expr();
            inner.skip();
            if (!v || inner.pos != arg.size()) return std::nullopt;
            return std::fabs(*v);
        }
        if (name == "Hit") {
            if (field == "Damage") return owner.hit_dmg;
            return std::nullopt;
        }
        if (name == "Variable") {
            const auto it = owner.q3.find(arg);
            return it != owner.q3.end() ? std::optional<double>(it->second) : 0.0;
        }
        if (name == "PlayerParameter") {
            const CondCtx& m = (arg == "Enemy") ? foe : (arg == "Me" ? owner : owner);
            if (arg != "Me" && arg != "Enemy") return std::nullopt;
            if (field == "Health") return m.hp;
            if (field == "MagicBullet") return 0.0;  // no magic (OPEN)
            return std::nullopt;  // DamageConverter etc. OPEN
        }
        return std::nullopt;
    }
};

inline std::optional<double> eval_operand(const std::string& text, const CondCtx& owner,
                                          const CondCtx& foe) {
    ExprParse p{text, 0, owner, foe};
    auto v = p.parse_expr();
    p.skip();
    if (!v || p.pos != text.size()) return std::nullopt;
    return v;
}

// `kp.isEqual`: `ih.Wb().Wn()>0` over `?Compare[V1,V2,Tag]`.
inline bool eval_compare(const TrigCond& c, const CondCtx& owner, const CondCtx& foe) {
    const auto it1 = c.s.find("Value1");
    const auto it2 = c.s.find("Value2");
    if (it1 == c.s.end() || it2 == c.s.end()) return false;
    const auto v1 = eval_operand(it1->second, owner, foe);
    const auto v2 = eval_operand(it2->second, owner, foe);
    if (!v1 || !v2) return false;
    const std::string& k = c.kind;
    if (k == "Less") return *v1 < *v2;
    if (k == "LessEqual") return *v1 <= *v2;
    if (k == "Greater") return *v1 > *v2;
    if (k == "GreaterEqual") return *v1 >= *v2;
    if (k == "Equal") return *v1 == *v2;
    return false;
}

// `Axa` per-condition (`ec.isEqual`, L1302-1316 + §5.5 table).
// `owner`/`foe` selected by `Lh(ob)`; expression/kp → false (OPEN).
inline bool eval_cond(const TrigCond& c, const CondCtx& owner, const CondCtx& foe) {
    const CondCtx& m = c.ob == 2 ? foe : owner;
    bool r = false;
    const std::string& k = c.kind;
    if (k == "PerkStart") {
        r = true;
    } else if (k == "Random") {
        // `Da.cT(ou*100)`: `a>b(100)` true else `draw*100 < a*100`.
        const double ch = c.chance;
        double draw = 0.5;
        if (m.draw01) draw = m.draw01();
        r = ch >= 1.0 || draw < ch;
    } else if (k == "Style") {
        // `xE(dz)`: style NAME range Min..Max (`ZBa` 0..5).
        int lo = 0, hi = 5;
        auto it = c.s.find("Min");
        if (it != c.s.end()) {
            if (it->second == "Turtle") lo = 0;
            else if (it->second == "Hard") lo = 1;
            else if (it->second == "Brutal") lo = 2;
            else if (it->second == "Aggressive") lo = 3;
            else if (it->second == "Crazy") lo = 4;
            else if (it->second == "Fantastic") lo = 5;
        }
        it = c.s.find("Max");
        if (it != c.s.end()) {
            if (it->second == "Turtle") hi = 0;
            else if (it->second == "Hard") hi = 1;
            else if (it->second == "Brutal") hi = 2;
            else if (it->second == "Aggressive") hi = 3;
            else if (it->second == "Crazy") hi = 4;
            else if (it->second == "Fantastic") hi = 5;
        }
        r = m.style_level >= lo && m.style_level <= hi;
    } else if (k == "Combo") {
        r = range_check(c, static_cast<double>(m.combo));
    } else if (k == "RoundStage") {
        const auto it = c.s.find("Name");
        const int want = it != c.s.end() ? stage_by_name(it->second) : 0;
        r = want == 0 || want == m.stage;
    } else if (k == "CurrentAnimation") {
        bool ok = true;
        const auto it = c.s.find("Name");
        if (it != c.s.end() && !it->second.empty() && m.anim != it->second) ok = false;
        if (ok) {
            // `xE(ip())`: frame-in-animation — OPEN (no clip clock here);
            // a bare Name match passes, a Min/Max range fails closed.
            ok = !range_has_min(c) && !range_has_max(c);
        }
        r = ok;
    } else if (k == "CurrentInterval") {
        // name-present AND type-present (either empty = wildcard).
        bool name_ok = true, type_ok = true;
        const auto ni = c.s.find("Name");
        const auto ti = c.s.find("Type");
        const bool want_name = ni != c.s.end() && !ni->second.empty();
        const bool want_type = ti != c.s.end() && !ti->second.empty();
        if (want_name || want_type) {
            name_ok = !want_name;
            type_ok = !want_type;
            int want_t = 0;
            if (want_type) {
                try {
                    want_t = std::stoi(ti->second);
                } catch (...) {
                    want_t = 0;
                }
            }
            for (const auto& iv : m.intervals) {
                if (want_name && iv.first == ni->second) name_ok = true;
                if (want_type && iv.second == want_t) type_ok = true;
            }
        }
        r = name_ok && type_ok;
    } else if (k == "Health") {
        r = range_check(c, m.hp);
    } else if (k == "Item") {
        // any `Kea()` entry matches all non-empty Name/Type/Subtype —
        // we only track names, so a non-empty Type/Subtype fails closed.
        r = false;
        const auto nn = c.s.find("Name");
        const auto tn = c.s.find("Type");
        const auto sn = c.s.find("Subtype");
        const bool want_t = tn != c.s.end() && !tn->second.empty();
        const bool want_s = sn != c.s.end() && !sn->second.empty();
        if (!want_t && !want_s) {
            if (nn == c.s.end() || nn->second.empty()) {
                r = !m.items.empty();
            } else {
                for (const auto& it : m.items) {
                    if (it == nn->second) {
                        r = true;
                        break;
                    }
                }
            }
        }
    } else if (k == "Round") {
        // Round gate VERIFIED (REVIEW B LOW): JS round.round++ per Z2
        // (1-based in-fight; writers only reset to 0 at battle init),
        // ours matches (round_.number++ at round_start).
        int want = 0;
        const auto it = c.s.find("Number");
        if (it != c.s.end()) {
            try {
                want = std::stoi(it->second);
            } catch (...) {
                want = 0;
            }
        }
        r = want == m.round;
    } else if (k == "Bullets") {
        // `lp`: Type MagicBullet->bh, RaidChargeBullet->dO, else false.
        const auto ti = c.s.find("Type");
        const std::string t = ti != c.s.end() ? ti->second : "";
        if (t == "MagicBullet") {
            r = range_check(c, static_cast<double>(m.bullets));
        } else if (t == "RaidChargeBullet") {
            r = range_check(c, static_cast<double>(m.raid));
        } else {
            r = false;
        }
    } else if (k == "MagicCharge") {
        r = range_check(c, m.charge);  // `sp`: xE(my)
    } else if (k == "Less" || k == "LessEqual" || k == "Greater" || k == "GreaterEqual" || k == "Equal") {
        // `kp` comparisons (see above); Lh-selected pair (m = cond model).
        r = eval_compare(c, m, c.ob == 2 ? owner : foe);
    } else if (k == "ModExists") {
        // `bc.YZa(name, ns)`: entries under ns with action-name match;
        // else name in the live list.
        bool hit = false;
        const auto ni = c.s.find("Name");
        const auto gi = c.s.find("Namespace");
        const std::string wname = ni != c.s.end() ? ni->second : "";
        const std::string wns = gi != c.s.end() ? gi->second : "";
        if (!wns.empty()) {
            if (wname.empty()) {
                for (const auto& kv : m.mod_ns) {
                    if (kv.second == wns) {
                        hit = true;
                        break;
                    }
                }
            } else {
                const auto it = m.mod_ns.find(wname);
                hit = it != m.mod_ns.end() && it->second == wns;
            }
        } else if (!wname.empty()) {
            hit = m.mods.find(wname) != m.mods.end();
        }
        r = hit;
    } else if (k == "Pain") {
        r = range_check(c, m.pain);
    } else if (k == "Operator") {
        if (c.op == "Or") {
            r = false;
            for (const auto& n : c.nested) {
                if (eval_cond(n, owner, foe)) {
                    r = true;
                    break;
                }
            }
        } else {  // And (default); empty And = true
            r = true;
            for (const auto& n : c.nested) {
                if (!eval_cond(n, owner, foe)) {
                    r = false;
                    break;
                }
            }
        }
    } else if (k == "InTheArea") {
        r = m.in_area;  // `rR` flag (area bounds OPEN)
    } else {
        r = false;  // `expression`/unknown → false (OPEN evaluators)
    }
    return c.negate ? !r : r;
}

// `Hh.isEqual` (HitPreCrit/HitPostCrit/PostHit): super (Ob scope + type)
// + info gates. `-1` = any; Damage missing = 0.
inline bool match_hit_event(const TrigEvent& e, const TrigVars& v, int entry_side,
                            int fired_side) {
    if (e.ob == 1 && entry_side != fired_side) return false;
    if (e.ob == 2 && entry_side == fired_side) return false;
    const auto num = [&v](const char* key, double def) {
        const auto it = v.num.find(key);
        return it != v.num.end() ? it->second : def;
    };
    const auto str = [&v](const char* key) {
        const auto it = v.str.find(key);
        return it != v.str.end() ? it->second : std::string();
    };
    if (!e.defense.empty() && e.defense != str("Defense")) return false;
    if (!e.animation.empty()) {
        const std::string a = str("Animation");
        if (a.empty() || a != e.animation) return false;
    }
    if (e.critical > -1 && e.critical != (num("Critical", 0.0) != 0.0 ? 1 : 0)) return false;
    if (e.shock > -1 && e.shock != (num("Shock", 0.0) != 0.0 ? 1 : 0)) return false;
    if (e.block > -1 && e.block != (num("Block", 0.0) != 0.0 ? 1 : 0)) return false;
    const double dmg = num("Damage", 0.0);
    if (e.dmg_min > -1.0 && dmg < e.dmg_min) return false;
    if (e.dmg_max > -1.0 && dmg > e.dmg_max) return false;
    return true;
}

// `t0a(info)`: ANY Hc event matches (cb-aware). Only hit events carry
// field gates here; the rest match on type+scope (the bus pre-filters by
// slot, and `Ep`/`Cp`/`Lj`/`Dp`/`Gh` compare against ctx/vars below).
inline bool match_event(const TrigEvent& e, const TrigVars& v, int entry_side,
                        int fired_side, int stage, int step_frame) {
    bool r = false;
    if (e.type == kEvHitPreCrit || e.type == kEvHitPostCrit || e.type == kEvPostHit) {
        r = match_hit_event(e, v, entry_side, fired_side);
    } else if (e.type == kEvEveryFrame) {
        // `Cp.isEqual`: step==0 always; else StepFrame%step==0.
        if (e.ob == 1 && entry_side != fired_side) r = false;
        else if (e.ob == 2 && entry_side == fired_side) r = false;
        else r = e.step == 0 || (step_frame % e.step == 0);
    } else if (e.type == kEvRoundStage) {
        // `Ep.isEqual`: Je==0 any else qk.Je match.
        if (e.ob == 1 && entry_side != fired_side) r = false;
        else if (e.ob == 2 && entry_side == fired_side) r = false;
        else r = e.stage == 0 || e.stage == stage;
    } else if (e.type == kEvIntervalStart || e.type == kEvIntervalEnd) {
        // `Lj.isEqual`: Name/Type wildcards (vars carry the edge).
        if (e.ob == 1 && entry_side != fired_side) r = false;
        else if (e.ob == 2 && entry_side == fired_side) r = false;
        else {
            const auto ni = v.str.find("Interval");
            const auto ti = v.num.find("IntervalType");
            const std::string iname = ni != v.str.end() ? ni->second : "";
            const int itype = ti != v.num.end() ? static_cast<int>(ti->second) : 0;
            const bool nok = e.interval.empty() || e.interval == iname;
            const bool tok = e.interval_type == 0 || e.interval_type == itype;
            r = nok && tok;
        }
    } else if (e.type == kEvModExpires) {
        // `Dp.isEqual`: empty Jd → name compare; else namespace compare.
        // Vars carry ModExpires/Namespace (JNa stamps, L1299).
        if (e.ob == 1 && entry_side != fired_side) r = false;
        else if (e.ob == 2 && entry_side == fired_side) r = false;
        else {
            const auto ni = v.str.find("ModExpires");
            const auto gi = v.str.find("Namespace");
            const std::string iname = ni != v.str.end() ? ni->second : "";
            const std::string ins = gi != v.str.end() ? gi->second : "";
            if (e.mod_ns.empty()) {
                r = !e.mod_name.empty() && e.mod_name == iname;
            } else {
                r = e.mod_ns == ins;
            }
        }
    } else if (e.type != 0) {
        // `Gh` (Area/MagicCharged): super only — scope gate.
        if (e.ob == 1 && entry_side != fired_side) r = false;
        else if (e.ob == 2 && entry_side == fired_side) r = false;
        else r = true;
    }
    return e.negate ? !r : r;
}

// One live mod (`jp` entry with a Name — `ModIcon`/`ModAttributes`/
// `ModHealthChange`/timed `ChangeImpulse`/`ChangeAdditionalDamageValue`/…).
// `Uf` counts down in `ia`; at 0 the `JNa` revert runs + `ModExpires`
// publishes slot 14 (`JNa→Gj(d,14)`, L1299). `qw`-armed (ClearMods `Vob`)
// entries flush at `Qh`.
struct ModState {
    std::string name;  // mod name (`Name=` or the action type)
    std::string namespc;  // `Namespace=` (`Jd`)
    std::string parent;  // `ParentPerk` (`Wa`, stamped at install)
    std::string kind;  // action type that installed it
    int uf = 0;  // remaining frames (`Uf`; 0 = untimed/persistent)
    int iv = 0;  // interval (`Iv`)
    double per_frame = 0.0;  // `Tp` heal/damage per frame
    std::map<std::string, std::string> vars;  // `SetModVariable` Q3 store
    std::vector<std::pair<std::string, double>> attr_adds;  // `Rp` (JNa)
    int attr_side = 0;  // side whose attrs got the adds (JNa target)
    int col_side = -1;  // TurnOffCollision target side (JNa restores vZ)
    bool qw = false;  // ClearMods-armed (flush at Qh)
};

// One bus side (`Kw` entry): its model's triggers (`Oa`), live mods,
// deferred `lY` queue, Q3 vars, equipped item names (Item conditions).
struct BusSide {
    int side = 0;
    std::vector<PerkTrigger> triggers;
    std::map<std::string, ModState> mods;
    std::vector<std::pair<PerkTrigger, PerkAction>> deferred;  // `lY`
    std::map<std::string, double> q3;
    std::vector<std::string> items;
};

// The trigger bus (`bc` + per-model `Kw`). Sides are 0 (player) / 1
// (enemy); `fired_side` is the `qk` model of the event. Synchronous:
// `fire()` queues to `lY`, `qh()` executes. `log` sinks no-op lines.
class TrigBus {
   public:
    std::function<void(const std::string&)> log;

    void register_side(int side, std::vector<PerkTrigger> triggers,
                       std::vector<std::string> items) {
        BusSide& s = sides_[side & 1];
        s.side = side & 1;
        s.triggers = std::move(triggers);
        s.items = std::move(items);
    }

    void clear() {
        sides_[0] = BusSide();
        sides_[1] = BusSide();
        sides_[0].side = 0;
        sides_[1].side = 1;
    }

    const BusSide& side(int s) const { return sides_[s & 1]; }
    BusSide& mutable_side(int s) { return sides_[s & 1]; }

    // `Gj(model, slot, withInfo)`: per side → slot triggers → `v_a`.
    // `has_info=false` skips `t0a` (`c==null`); `Qh()` always follows.
    // Matched (trigger, action) pairs land in the side's `lY`; the
    // caller drains them via `qh_*` (hit scope) or plain `qh()`.
    void fire(int slot, const TrigVars& vars, bool has_info, int fired_side,
              const CondCtx& ctx0, const CondCtx& ctx1, int stage, int step_frame) {
        const CondCtx* ctxs[2] = {&ctx0, &ctx1};
        for (int s = 0; s < 2; ++s) {
            BusSide& side = sides_[s];
            for (const PerkTrigger& t : side.triggers) {
                bool in_slot = false;
                for (const TrigEvent& e : t.events) {
                    if (e.type == slot) {
                        in_slot = true;
                        break;
                    }
                }
                if (!in_slot || !t.enabled) continue;
                // `bBa`: the model's Fw entry always exists post-register.
                bool gate = true;
                if (has_info) {
                    gate = false;
                    for (const TrigEvent& e : t.events) {
                        if (e.type != slot) continue;
                        if (match_event(e, vars, s, fired_side, stage, step_frame)) {
                            gate = true;
                            break;
                        }
                    }
                }
                if (!gate) continue;
                const CondCtx& owner = *ctxs[s];
                const CondCtx& foe = *ctxs[1 - s];
                bool ok = true;
                for (const TrigCond& c : t.conds) {
                    if (!eval_cond(c, owner, foe)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;
                for (const PerkAction& a : t.actions) {
                    side.deferred.emplace_back(t, a);
                }
            }
        }
    }

    // Drain one side's `lY` into `out` (hit scope collects combat actions;
    // the caller executes the rest via `apply_side_action`).
    void drain(int side, std::vector<std::pair<PerkTrigger, PerkAction>>& out) {
        BusSide& s = sides_[side & 1];
        out.insert(out.end(), s.deferred.begin(), s.deferred.end());
        s.deferred.clear();
    }

    // `Qh`: flush `qw`-armed mods (ClearMods path) on both sides.
    // Returns (side, mod) pairs flushed for slot-14 publish by the caller.
    void flush_qw(std::vector<std::pair<int, ModState>>& out) {
        for (int s = 0; s < 2; ++s) {
            BusSide& side = sides_[s];
            for (auto it = side.mods.begin(); it != side.mods.end();) {
                if (it->second.qw) {
                    out.emplace_back(s, it->second);
                    it = side.mods.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // `Provoke` (`jpb` → `Pob`): fire the named trigger set immediately
    // (`lF`, not `lY`) on the model's side. Returns fired actions.
    void provoke(int side, const std::string& sl,
                 std::vector<std::pair<PerkTrigger, PerkAction>>& out) {
        BusSide& s = sides_[side & 1];
        for (const PerkTrigger& t : s.triggers) {
            if (t.enabled && !t.name.empty() && t.name == sl) {
                for (const PerkAction& a : t.actions) out.emplace_back(t, a);
            }
        }
    }

    // Install/update a live mod (mod actions). `uf` from `Frames=` (0 =
    // persistent). Re-install refreshes `uf` (verbatim `UKa` always wraps
    // a fresh `jp`; the map keeps one entry per name).
    void install_mod(int side, ModState m) {
        BusSide& s = sides_[side & 1];
        s.mods[m.name] = std::move(m);
    }

    void clear_mods(int side, const std::string& name, const std::string& ns) {
        // `Vob`: arm `qw` on same-named mods (+`bc.iAa` namespace).
        BusSide& s = sides_[side & 1];
        for (auto& kv : s.mods) {
            if (name.empty() || kv.first == name) kv.second.qw = true;
            if (!ns.empty() && kv.second.namespc == ns) kv.second.qw = true;
        }
    }

    void retime_mod(int side, const std::string& name, int uf, int iv,
                    const std::string& ns = "") {
        // `dpb`: retime named mod `Uf/Iv` (-1 = keep); namespace loop
        // retimes same-named entries under `bc.iAa(ns)`.
        BusSide& s = sides_[side & 1];
        for (auto& kv : s.mods) {
            if (kv.first != name) continue;
            if (!ns.empty() && kv.second.namespc != ns) continue;
            if (uf >= 0) kv.second.uf = uf;
            if (iv >= 0) kv.second.iv = iv;
        }
    }

   private:
    BusSide sides_[2];
};

// Per-side mutable state for the `ia` mod tick (kept outside the bus so
// the bus stays UI/model-free; fight.cpp binds these to FightFighters).
struct ModTickCtx {
    std::map<std::string, float>* attrs_by_side[2] = {nullptr, nullptr};
    Vec3* jg_by_side[2] = {nullptr, nullptr};
    bool* col_by_side[2] = {nullptr, nullptr};  // hq.S vZ restore
    std::function<void(int, float)> set_timescale;  // fq.Kvb revert
    std::function<void(int)> reset_color;  // Mp.Qs revert (location color)
    float* ly_by_side[2] = {nullptr, nullptr};
    float* qz_by_side[2] = {nullptr, nullptr};
    // Called per expiry (natural or `qw` flush) for the slot-14 publish.
    std::function<void(int, const ModState&)> on_expire;
};

// `JNa` revert (L1298-1299) for types 1,3,15,20,21,22,27,28,29,30:
//   ModIcon(1): icon gone with the entry; ModAttributes(3): subtract the
//   adds from the recorded side's map; ModInvisibility(15): no state;
//   ChangeImpulse(20): `gob()` → (1,1,1); ChangeHitEffectScale(21):
//   `fob()` → Qz=1; ChangeAdditionalDamageValue(22): `Ynb()` → Ly=0;
//   27/28/29: log-only states; TurnOffCollision(30): log (body flags OPEN).
inline void revert_mod(const ModState& m, int side, ModTickCtx& ctx,
                       std::function<void(const std::string&)> log) {
    if (m.kind == "ModAttributes" && m.attr_side >= 0 && m.attr_side < 2 &&
        ctx.attrs_by_side[m.attr_side] != nullptr) {
        for (const auto& kv : m.attr_adds) {
            (*ctx.attrs_by_side[m.attr_side])[kv.first] -= static_cast<float>(kv.second);
        }
    } else if (m.kind == "ChangeImpulse" && side >= 0 && side < 2 &&
               ctx.jg_by_side[side] != nullptr) {
        *ctx.jg_by_side[side] = Vec3{1.0f, 1.0f, 1.0f};  // `gob()`
    } else if (m.kind == "ChangeAdditionalDamageValue" && side >= 0 && side < 2 &&
               ctx.ly_by_side[side] != nullptr) {
        *ctx.ly_by_side[side] = 0.0f;  // `Ynb()`
    } else if (m.kind == "ChangeHitEffectScale" && side >= 0 && side < 2 &&
               ctx.qz_by_side[side] != nullptr) {
        *ctx.qz_by_side[side] = 1.0f;  // `fob()`
    } else if (m.kind == "SlowModel" && ctx.set_timescale) {
        ctx.set_timescale(side, 1.0f);  // `Kvb` revert: KT(hU, v.dB=1)
    }
    else if (m.kind == "TurnOffCollision" && m.col_side >= 0 && m.col_side < 2 &&
               ctx.col_by_side[m.col_side] != nullptr) {
        *ctx.col_by_side[m.col_side] = true;  // `hq.S(a,true)`
    } else if (m.kind == "ChangeModelColor" && ctx.reset_color) {
        ctx.reset_color(side);  // `Mp.Qs(a,true)` -> location color
    }
    if (log) {
        if (m.kind == "StealMagicMod" || m.kind == "SlowModel" ||
            m.kind == "ChangeModelColor" || m.kind == "TurnOffCollision" ||
            m.kind == "ModInvisibility") {
            log("perkrevert " + m.kind + " " + m.name);
        }
    }
}

// `ia` tick for one side (L1290/L1298-1299): countdown `Uf`, `JNa` revert
// + erase at 0, `ModExpires` slot-14 publish via `on_expire`. Entries with
// `uf<=0` are persistent (no countdown). `ly/qz/jg` resets for the
// matching kinds happen here (owner side).
inline void tick_side_mods(TrigBus& bus, int side, ModTickCtx& ctx) {
    BusSide& s = bus.mutable_side(side);
    for (auto it = s.mods.begin(); it != s.mods.end();) {
        ModState& m = it->second;
        if (m.uf > 0) {
            if (--m.uf == 0) {
                if (bus.log) revert_mod(m, side & 1, ctx, bus.log);
                if (ctx.on_expire) ctx.on_expire(side & 1, m);
                it = s.mods.erase(it);
                continue;
            }
        }
        ++it;
    }
}

// --- perks.xml loader + equip mapping (PERKS_STATIC §5.4/§5.7) ---------
// `_Var` refs (`Be.Ava/okb/yqb`) resolve against the merged Set map
// (def Set, item-ref Set wins — `clone(set,rating)` rebase). Non-numeric
// expressions (`Qa.oh`) are OPEN: constants load, the rest fall back to
// the action default + note. `Template=` merges the named perk's triggers.

inline std::string subst_var(const std::string& v,
                             const std::map<std::string, double>& num,
                             const std::map<std::string, std::string>& str) {
    if (v.empty() || v[0] != '_') return v;
    const std::string key = v.substr(1);
    const auto ns = str.find(key);
    if (ns != str.end()) return ns->second;
    const auto nn = num.find(key);
    if (nn != num.end()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", nn->second);
        return std::string(buf);
    }
    return v;  // unresolved — loaders fall back (OPEN evaluators)
}

inline double load_num_attr(const pugi::xml_node& node, const char* attr,
                            double def,
                            const std::map<std::string, double>& num,
                            const std::map<std::string, std::string>& str) {
    const pugi::xml_attribute a = node.attribute(attr);
    if (!a) return def;
    const std::string v = subst_var(a.value(), num, str);
    try {
        std::size_t pos = 0;
        const double d = std::stod(v, &pos);
        if (pos == v.size()) return d;
    } catch (...) {
    }
    return def;
}

inline int load_int_attr(const pugi::xml_node& node, const char* attr,
                         int def,
                         const std::map<std::string, double>& num,
                         const std::map<std::string, std::string>& str) {
    return static_cast<int>(load_num_attr(node, attr, static_cast<double>(def), num, str));
}

inline std::string load_str_attr(const pugi::xml_node& node, const char* attr,
                                 const std::map<std::string, double>& num,
                                 const std::map<std::string, std::string>& str) {
    const pugi::xml_attribute a = node.attribute(attr);
    if (!a) return "";
    return subst_var(a.value(), num, str);
}

inline TrigEvent load_trig_event(const pugi::xml_node& e,
                                 const std::map<std::string, double>& num,
                                 const std::map<std::string, std::string>& str) {
    TrigEvent ev;
    ev.type = event_type_by_name(e.name());
    ev.ob = ob_by_player(load_str_attr(e, "Player", num, str));
    if (ev.ob == 0) ev.ob = 1;  // `Jf` ctor default is Me(1)
    ev.negate = load_str_attr(e, "Not", num, str) == "1";
    ev.defense = load_str_attr(e, "Defense", num, str);
    ev.animation = load_str_attr(e, "Animation", num, str);
    ev.block = load_int_attr(e, "Block", -1, num, str);
    ev.critical = load_int_attr(e, "Critical", -1, num, str);
    ev.shock = load_int_attr(e, "Shock", -1, num, str);
    ev.dmg_min = load_num_attr(e, "DamageMin", -1.0, num, str);
    ev.dmg_max = load_num_attr(e, "DamageMax", -1.0, num, str);
    ev.step = load_int_attr(e, "Step", 0, num, str);
    ev.stage = stage_by_name(load_str_attr(e, "Name", num, str));
    ev.interval = load_str_attr(e, "Name", num, str);
    {
        // `Lj` Type via `fe.G0` — numeric here; named types are OPEN.
        const std::string t = load_str_attr(e, "Type", num, str);
        if (!t.empty()) {
            try {
                ev.interval_type = std::stoi(t);
            } catch (...) {
                ev.interval_type = 0;
            }
        }
    }
    ev.mod_name = load_str_attr(e, "Name", num, str);
    ev.mod_ns = load_str_attr(e, "Namespace", num, str);
    return ev;
}

inline TrigCond load_trig_cond(const pugi::xml_node& e,
                               const std::map<std::string, double>& num,
                               const std::map<std::string, std::string>& str) {
    TrigCond c;
    c.kind = e.name();
    c.ob = ob_by_player(load_str_attr(e, "Player", num, str));
    if (c.ob == 0) c.ob = 1;
    c.negate = load_str_attr(e, "Not", num, str) == "1";
    for (const pugi::xml_attribute a : e.attributes()) {
        c.s[a.name()] = subst_var(a.value(), num, str);
    }
    c.chance = load_num_attr(e, "Chance", 0.0, num, str);
    c.op = load_str_attr(e, "Type", num, str);
    if (c.kind == "Operator") {
        for (const pugi::xml_node n : e.children()) {
            if (std::string(n.name()) == "Conditions") {
                for (const pugi::xml_node cc : n.children()) {
                    c.nested.push_back(load_trig_cond(cc, num, str));
                }
            } else {
                c.nested.push_back(load_trig_cond(n, num, str));
            }
        }
    }
    return c;
}

inline PerkAction load_perk_action(const pugi::xml_node& e,
                                   const std::map<std::string, double>& num,
                                   const std::map<std::string, std::string>& str) {
    PerkAction a;
    a.type = e.name();
    a.ob = ob_by_player(load_str_attr(e, "Player", num, str));
    if (a.ob == 0) a.ob = 1;
    for (const pugi::xml_attribute at : e.attributes()) {
        const std::string key = at.name();
        const std::string v = subst_var(at.value(), num, str);
        try {
            std::size_t pos = 0;
            const double d = std::stod(v, &pos);
            if (pos == v.size()) {
                a.num[key] = d;
                continue;
            }
        } catch (...) {
        }
        a.str[key] = v;
    }
    return a;
}

inline PerkTrigger load_trigger(const pugi::xml_node& t, const std::string& perk,
                                const std::map<std::string, double>& num,
                                const std::map<std::string, std::string>& str) {
    PerkTrigger out;
    out.name = t.attribute("Name") ? t.attribute("Name").value() : "";
    out.perk = perk;
    const pugi::xml_node evs = t.child("Events");
    if (evs) {
        for (const pugi::xml_node e : evs.children()) {
            const TrigEvent ev = load_trig_event(e, num, str);
            if (ev.type != 0) out.events.push_back(ev);
        }
    }
    const pugi::xml_node cds = t.child("Conditions");
    if (cds) {
        for (const pugi::xml_node e : cds.children()) {
            out.conds.push_back(load_trig_cond(e, num, str));
        }
    }
    const pugi::xml_node acs = t.child("Actions");
    if (acs) {
        for (const pugi::xml_node e : acs.children()) {
            out.actions.push_back(load_perk_action(e, num, str));
        }
    }
    return out;
}

// Parses res/perks.xml into defs (`Be` rows + `<Set>` vars; triggers load
// with the DEF's Set for `_Var` substitution — item-ref overrides apply at
// equip time in `build_side_triggers`). Template triggers merge after all
// rows parse (`Hf.y0a` stamp).
inline std::map<std::string, PerkDef> parse_perks_xml(const std::string& xml_text) {
    std::map<std::string, PerkDef> out;
    pugi::xml_document doc;
    const pugi::xml_parse_result ok = doc.load_string(xml_text.c_str());
    if (!ok) return out;
    const pugi::xml_node root = doc.root().first_child();
    for (const pugi::xml_node p : root.children("Perk")) {
        PerkDef def;
        def.name = p.attribute("Name") ? p.attribute("Name").value() : "";
        if (def.name.empty()) continue;
        def.templ = p.attribute("Template") ? p.attribute("Template").value() : "";
        const pugi::xml_node set = p.child("Set");
        if (set) {
            for (const pugi::xml_attribute a : set.attributes()) {
                try {
                    std::size_t pos = 0;
                    const double d = std::stod(a.value(), &pos);
                    if (pos == std::string(a.value()).size()) {
                        def.set_num[a.name()] = d;
                        continue;
                    }
                } catch (...) {
                }
                def.set_str[a.name()] = a.value();
            }
        }
        for (const pugi::xml_node t : p.children("Trigger")) {
            def.triggers.push_back(load_trigger(t, def.name, def.set_num, def.set_str));
        }
        out[def.name] = std::move(def);
    }
    // Template merge: append the named perk's triggers (own Set applies —
    // substitution already ran at the template's load; re-substitution
    // with the outer Set is OPEN).
    for (auto& kv : out) {
        if (!kv.second.templ.empty()) {
            const auto it = out.find(kv.second.templ);
            if (it != out.end()) {
                kv.second.triggers.insert(kv.second.triggers.end(),
                                          it->second.triggers.begin(),
                                          it->second.triggers.end());
            }
        }
    }
    return out;
}

// One item→perk binding (list.xml `<Perks>`/`<Enchantments><Perk Name>` +
// its `<Set>` overrides; enchant flag kept for the budget log).
struct ItemPerkRef {
    std::string name;
    std::map<std::string, double> set_num;
    std::map<std::string, std::string> set_str;
    bool enchant = false;
};

// Equip mapping (`ZOa`/`Wk` analog, §5.4/§5.7): perk refs → live triggers
// with merged Set vars (item-ref Set wins). Unknown perk names are
// dropped (log line when `log` is set).
inline std::vector<PerkTrigger> build_side_triggers(
    const std::vector<ItemPerkRef>& refs,
    const std::map<std::string, PerkDef>& catalog,
    std::function<void(const std::string&)> log = {}) {
    std::vector<PerkTrigger> out;
    for (const ItemPerkRef& ref : refs) {
        const auto it = catalog.find(ref.name);
        if (it == catalog.end()) {
            if (log) log("perkdrop unknown " + ref.name);
            continue;
        }
        const PerkDef& def = it->second;
        if (ref.set_num.empty() && ref.set_str.empty()) {
            out.insert(out.end(), def.triggers.begin(), def.triggers.end());
            continue;
        }
        // Re-substitute with merged vars (item wins).
        std::map<std::string, double> num = def.set_num;
        for (const auto& kv : ref.set_num) num[kv.first] = kv.second;
        std::map<std::string, std::string> str = def.set_str;
        for (const auto& kv : ref.set_str) str[kv.first] = kv.second;
        // NOTE: triggers were substituted at catalog load; a merged
        // re-substitution needs the raw XML (OPEN) — refs WITH overrides
        // re-resolve only still-literal `_Var` tokens below.
        for (PerkTrigger t : def.triggers) {
            t.perk = def.name;
            for (PerkAction& a : t.actions) {
                for (auto& kv : a.num) {
                    (void)kv;
                }
                for (auto& kv : a.str) {
                    if (!kv.second.empty() && kv.second[0] == '_') {
                        kv.second = subst_var(kv.second, num, str);
                        try {
                            std::size_t pos = 0;
                            const double d = std::stod(kv.second, &pos);
                            if (pos == kv.second.size()) {
                                a.num[kv.first] = d;
                                kv.second = "";
                            }
                        } catch (...) {
                        }
                    }
                }
                for (auto itn = a.str.begin(); itn != a.str.end();) {
                    if (itn->second.empty()) {
                        itn = a.str.erase(itn);
                    } else {
                        ++itn;
                    }
                }
            }
            out.push_back(std::move(t));
        }
    }
    return out;
}

}  // namespace sf2::scene

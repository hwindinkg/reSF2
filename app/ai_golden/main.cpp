// app/ai_golden/ - Phase 6 Node-harness golden, C++ mirror (static-first).
//
// Prints the same decision-math vectors as reference/tools/ai_golden.js
// (DaPrng streams, weight-curve Gb, dqb zones) as JSON on stdout, plus a
// scripted AiController decision trace (regression artifact — the Node side
// has no full-Pqb transcription, so the trace is self-referential for now;
// Phase 8's runtime trace is the full-tree check).
//
//   ai_golden > cpp_vectors.json
//   node reference/tools/ai_golden.js > node_vectors.json
//   python compare (prng bit-exact; gb within 1e-5 — C++ float vs JS double)
//
// NOTE (2026-09-04): written without a toolchain on this machine (no
// msbuild/cl) — NOT compiled. Build + run + diff when a compiler exists;
// the Phase 6 gate stays OPEN until then.
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "scene/ai.hpp"
#include "scene/combat_decide.hpp"
#include "scene/damage.hpp"
#include "scene/fight.hpp"
#include "scene/move_def.hpp"
#include "scene/perks.hpp"
#include "codec.hpp"
#include "zstd_stream.hpp"

namespace {

using sf2::scene::AiController;
using sf2::scene::AiFeatureState;
using sf2::scene::AiFightState;
using sf2::scene::DaPrng;
using sf2::scene::MoveDef;
using sf2::scene::TacticDef;
using sf2::scene::WeightCurve;
using sf2::scene::weight_curve_eval;

WeightCurve constant_curve(float v) {
    WeightCurve c;
    c.base = v;
    c.limit = v;
    c.anti_limit = v;
    c.linear = true;
    return c;
}

WeightCurve lin_curve() {
    // Mirrors ai_golden.js `small`: eq .2 b8 .5 l8 .1 Uqa .05 wqa .02
    // Toa .01 V8 .03 kqa .05 Fk .01 BW .9 dV .05 linear.
    WeightCurve c;
    c.base = 0.2f;
    c.counter_factor = 0.5f;
    c.damage_factor = 0.1f;
    c.health_factor = 0.05f;
    c.enemy_health_factor = 0.02f;
    c.anim_frames_factor = 0.01f;
    c.hit_factor = 0.03f;
    c.distance_factor = 0.05f;
    c.shift = 0.01f;
    c.limit = 0.9f;
    c.anti_limit = 0.05f;
    c.linear = true;
    return c;
}

AiFeatureState small_feat() {
    // Raw field values IDENTICAL to ai_golden.js featS (formula-level
    // golden — no mq() semantics here): o1=0.9, q1=0.7 feed (1-o1)/(1-q1).
    AiFeatureState f;
    f.counter = 0.2f;
    f.xb = 0.5f;
    f.o1 = 0.9f;
    f.q1 = 0.7f;
    f.xY = 2.0f;
    f.cl = 0.0f;
    f.k2 = 0.0f;
    f.tf = 1.0f;
    f.pz = 0.0f;
    f.lya = 1.0f;
    return f;
}

MoveDef named_move(const std::string& name) {
    MoveDef m;
    m.name = name;
    return m;
}

}  // namespace

int main() {
    std::printf("{\n");

    // ---- prng vectors (seeds 0, 1, 12345, 2147483646) ----
    std::printf("  \"prng\": [");
    const std::uint32_t seeds[] = {0u, 1u, 12345u, 2147483646u};
    for (int s = 0; s < 4; ++s) {
        DaPrng pg(seeds[s]);
        const std::uint32_t b0 = pg.b0(), b1 = pg.b0(), b2 = pg.b0();
        DaPrng pg2(seeds[s]);
        const double j0 = pg2.jf(), j1 = pg2.jf();
        DaPrng pg3(seeds[s]);
        const double s4 = pg3.s4(10.0);
        DaPrng pg4(seeds[s]);
        const double dt = pg4.dT(2.0, 8.0);
        DaPrng pg5(seeds[s]);
        const bool ct = pg5.cT(50.0);
        std::printf("%s{\"seed\": %u, \"B0\": [%u, %u, %u], "
                    "\"jf\": [%.17g, %.17g], \"s4_10\": %.17g, "
                    "\"dT_2_8\": %.17g, \"cT_50\": %s}",
                    s ? ", " : "", seeds[s], b0, b1, b2, j0, j1, s4, dt,
                    ct ? "true" : "false");
    }
    std::printf("],\n");

    // ---- gb vectors (linear + exponential, small feat + negated) ----
    WeightCurve lin = lin_curve();
    WeightCurve exp = lin;
    exp.linear = false;
    AiFeatureState fs = small_feat();
    AiFeatureState fsn = fs;
    fsn.counter = -0.6f;
    fsn.xb = -0.4f;
    std::printf("  \"gb\": ["
                "{\"name\": \"linS+\", \"v\": %.9g}, "
                "{\"name\": \"linS-\", \"v\": %.9g}, "
                "{\"name\": \"expS+\", \"v\": %.9g}, "
                "{\"name\": \"expS-\", \"v\": %.9g}],\n",
                static_cast<double>(weight_curve_eval(lin, fs)),
                static_cast<double>(weight_curve_eval(lin, fsn)),
                static_cast<double>(weight_curve_eval(exp, fs)),
                static_cast<double>(weight_curve_eval(exp, fsn)));

    // ---- dqb zones via update() (constant curves 0.2/0.3/0.25) ----
    // Hand-built tactic: constant UseDefense curves; update() needs a
    // non-null moves map, so register one idle move.
    TacticDef tactic;
    tactic.name = "Golden";
    tactic.counter_attack_chance = constant_curve(0.2f);
    tactic.dodge_chance = constant_curve(0.3f);
    tactic.block_chance = constant_curve(0.25f);
    tactic.use_safe_attack_chance = constant_curve(0.0f);
    tactic.table_attack_chance = constant_curve(0.0f);
    tactic.cautious_movements_chance = constant_curve(0.0f);
    tactic.dodge_missiles_chance = constant_curve(0.0f);
    tactic.dodge_magic_chance = constant_curve(0.0f);
    std::map<std::string, MoveDef> moves;
    moves["Idle"] = named_move("Idle");
    moves["Jab"] = named_move("Jab");
    // One quick-attack slot so the scripted trace yields real decisions
    // (JS `bqb` L642 picks the best passing slot; gate `roll < score`).
    sf2::scene::AiAnimSlot slot;
    slot.names.push_back("Jab");
    slot.priority = 1;
    slot.chance = constant_curve(0.9f);
    tactic.quick_attacks.push_back(slot);
    // Roulette weights + ExpectedWait for the idle move: without weights
    // `pick` returns -1; without ExpectedWait the XW branch (JS L607-608)
    // discards slot candidates every pass (expected_wait defaults to 1.0
    // when no current move matches).
    tactic.anim_weights.emplace_back("Jab", constant_curve(1.0f));
    tactic.expected_wait.emplace_back("Idle", constant_curve(30.0f));
    std::vector<sf2::scene::TacticsFile> tactics;
    AiController ai;
    ai.init("", tactics, &tactic, &moves);
    ai.set_seed(7u);
    const MoveDef* idle = &moves["Idle"];
    std::printf("  \"updates\": [");    for (int i = 0; i < 12; ++i) {
        AiFightState st;
        st.current_move = idle;
        st.move_frame = 10;
        st.my_hp = 100.0f;
        st.my_max_hp = 100.0f;
        st.enemy_hp = 100.0f;
        st.enemy_max_hp = 100.0f;
        st.my_x = 0.0f;
        st.enemy_x = 200.0f;
        st.enemy_facing = -1;  // facing me: past the Pqb facing lock
        st.enemy_move_frame = 30;
        st.my_anim = "Idle";
        st.enemy_anim = "Idle";
        st.roll01 = nullptr;  // owned DaPrng stream
        const std::string mv = ai.update(st);
        std::printf("%s{\"i\": %d, \"move\": \"%s\", \"stage\": %d}",
                    i ? ", " : "", i, mv.c_str(), ai.last_stage());
    }
    std::printf("],\n");

    // ---- envelope codec vectors (FLOW_STATIC 3.1: base64 + zstd) ----
    {
        const std::string man_b64 =
            sf2::data::base64_encode(reinterpret_cast<const std::uint8_t*>("Man"), 3);
        const std::vector<std::uint8_t> man_back =
            sf2::data::base64_decode(man_b64);
        const std::string man_str(man_back.begin(), man_back.end());
        std::printf("  \"b64_man\": \"%s\",\n", man_b64.c_str());
        std::printf("  \"b64_roundtrip\": \"%s\",\n", man_str.c_str());
        const std::string xml = "<Root><Warrior ID=\"1\"/></Root>";
        const std::vector<std::uint8_t> xml_bytes(xml.begin(), xml.end());
        const std::vector<std::uint8_t> zc = sf2::data::zstd_compress(
            xml_bytes.data(), xml_bytes.size(), 3);
        const std::string env = sf2::data::base64_encode(zc);
        const std::vector<std::uint8_t> zc_back = sf2::data::base64_decode(env);
        const std::vector<std::uint8_t> xml_back = sf2::data::zstd_decompress(
            zc_back.data(), zc_back.size());
        const std::string xml_rt(xml_back.begin(), xml_back.end());
        std::printf("  \"envelope_roundtrip\": %s,\n",
                    xml_rt == xml ? "true" : "false");
        std::printf("  \"sf2_prefix\": \"%.3s\",\n",
                    ("SF2" + env).c_str());
    }

    // ---- combat vectors (mirror combat_golden.js S5/S6 via the SAME free
    // functions the fight uses: orb_hit / shock_tick / r8a_decide) ----
    {
        // S5: pain seq 40/40/40 @ threshold 100 -> [f,f,t], sr=120.
        // NOTE: sequenced statements (printf arg order is unspecified).
        sf2::scene::ShockState st;
        const bool q0 = sf2::scene::orb_hit(st, 40.0f, 100.0f);
        const bool q1 = sf2::scene::orb_hit(st, 40.0f, 100.0f);
        const bool q2 = sf2::scene::orb_hit(st, 40.0f, 100.0f);
        std::printf("  \"pain_seq\": [%s, %s, %s],\n",
                    q0 ? "true" : "false", q1 ? "true" : "false",
                    q2 ? "true" : "false");
        std::printf("  \"pain_sr\": %.1f,\n", static_cast<double>(st.pain_sr));
        // S5 weapon strike adds 0.
        sf2::scene::ShockState stw;
        sf2::scene::orb_hit(stw, 0.0f, 100.0f);
        std::printf("  \"pain_weapon_adds\": %.1f,\n",
                    static_cast<double>(stw.pain_sr));
        // S5 decay: sr=120 Xza=30 -> 90.
        sf2::scene::ShockState std_;
        std_.pain_sr = 120.0f;
        sf2::scene::shock_tick(std_, 30.0f);
        std::printf("  \"pain_decay\": %.1f,\n",
                    static_cast<double>(std_.pain_sr));
        // S5 vc veto: shocked target cannot re-shock.
        const bool veto = sf2::scene::r8a_decide(
            false, true, 50.0f, false, 81.0f, true, 0.0f, 81.0f, true, false, 0.0f).raw;
        std::printf("  \"r8a_veto\": %s,\n", veto ? "true" : "false");
        // S8 head-hit path (Zi=30, atkSo=3 -> b=10; sr=0/threshold=100;
        // crit_term=0*0, se=false; head_term=0.2*2=0.4, Uq, rHead=0.5).
        sf2::scene::ShockState s8;
        const float b8 = 30.0f / 3.0f;
        const bool c8 = sf2::scene::orb_hit(s8, b8, 100.0f);
        const sf2::scene::R8aOut r8 = sf2::scene::r8a_decide(
            false, false, b8, c8, 0.0f, false, 0.9, 0.4, true, false, 0.5);
        sf2::scene::ShockState s8b;
        const bool c8b = sf2::scene::orb_hit(s8b, b8, 100.0f);
        const sf2::scene::R8aOut r8b = sf2::scene::r8a_decide(
            false, false, b8, c8b, 0.0f, false, 0.9, 0.4, true, true, 0.5);
        std::printf("  \"r8a\": {\"raw\": %s, \"headF\": %s, \"critE\": %s, \"painShock\": %s},\n",
                    r8.raw ? "true" : "false", r8.head_f ? "true" : "false",
                    r8.crit_e ? "true" : "false", r8.pain_c ? "true" : "false");
        std::printf("  \"r8a_blocked\": {\"raw\": %s},\n",
                    r8b.raw ? "true" : "false");
        // S10 DK partition (L673-674 fixture: a/b/c/d, sja=0).
        sf2::scene::DkCandidate ca, cb, cc, cd;
        ca.id = "a";
        cb.id = "b";
        cc.id = "c";
        cd.id = "d";
        ca.anim_id = "A";
        cb.anim_id = "B";
        cc.anim_id = "C";
        cd.anim_id = "D";
        ca.priority = 1;
        cb.priority = 5;
        cc.priority = 3;
        cd.priority = 5;
        cb.eb = cc.eb = cd.eb = true;
        cc.rha = true;
        const std::vector<sf2::scene::DkCandidate> cands10 = {ca, cb, cc, cd};
        const sf2::scene::DkPartition p10 =
            sf2::scene::dk_partition(cands10, false, 0);
        const sf2::scene::DkPartition p10c =
            sf2::scene::dk_partition(cands10, true, 0);
        std::printf("  \"dk10\": {\"e\": \"%s\", \"ukb\": \"%s\"},\n",
                    cands10[static_cast<std::size_t>(p10.e)].id.c_str(),
                    [&]() -> const char* {
                        const auto& u = cands10[static_cast<std::size_t>(p10.ukb)];
                        return u.anim_id.empty() ? u.id.c_str() : u.anim_id.c_str();
                    }());
        std::printf("  \"dk10c_d\": %d,\n",
                    static_cast<int>(p10c.d.size()));
        // S12 DK tail matrix (L674): shared recorder across a-f.
        sf2::scene::DkCandidate cx, cy, cm, cn, cg;
        cx.id = "x";
        cy.id = "y";
        cm.id = "m";
        cm.ms = true;
        cm.r1 = 7;
        cm.anim_type = "Atk";
        cm.anim_e = 3;
        cn.id = "n";
        cn.index = 5;
        cn.anim_type = "Blk";
        cn.anim_e = 9;
        cg.id = "Ganim";
        cg.anim_id = "Ganim";
        const std::vector<sf2::scene::DkCandidate> cands12 = {cx, cy, cm, cn, cg};
        sf2::scene::DkTailCall rec12;
        sf2::scene::dk_tail(rec12, cands12, {0}, -1, -1);  // a
        sf2::scene::dk_tail(rec12, cands12, {0}, 1, -1);   // b
        sf2::scene::dk_tail(rec12, cands12, {}, -1, -1);   // c
        sf2::scene::dk_tail(rec12, cands12, {}, 2, -1);    // d
        sf2::scene::dk_tail(rec12, cands12, {}, 3, -1);    // e
        sf2::scene::dk_tail(rec12, cands12, {}, -1, 4);    // f
        std::printf("  \"dk12_pkb\": %d,\n",
                    static_cast<int>(rec12.pkb.size()));
        std::printf("  \"dk12_nsb\": [[\"%s\", %d]],\n",
                    cands12[rec12.nsb[0].first].id.c_str(), rec12.nsb[0].second);
        std::printf("  \"dk12_jja\": [[\"%s\", %d]],\n",
                    cands12[rec12.jja[0].first].id.c_str(), rec12.jja[0].second);
        std::printf("  \"dk12_ukb\": [[\"%s\"]],\n", rec12.ukb[0].c_str());
        std::printf("  \"dk12_zy\": [\"%s\", %d],\n",
                    rec12.zy.c_str(), rec12.jza);
        // S16 perks (mirrors combat_golden.js S16).
        {
            using sf2::scene::PerkAction;
            std::vector<PerkAction> perks;
            PerkAction s;
            s.type = "SetHit";
            s.num["Critical"] = 1.0;
            s.num["Damage"] = 25.0;
            perks.push_back(s);
            PerkAction l;
            l.type = "Lifesteal";
            l.num["DamagePart"] = 0.5;
            perks.push_back(l);
            PerkAction v;
            v.type = "ChangeAdditionalDamageValue";
            v.num["Value"] = 3.0;
            perks.push_back(v);
            PerkAction im;
            im.type = "ChangeImpulse";
            im.num["MultiplierX"] = 2.0;
            im.num["MultiplierZ"] = 0.5;
            perks.push_back(im);
            PerkAction ma;
            ma.type = "ModAttributes";
            ma.num["ShockCriticalHitChance"] = 0.1;
            perks.push_back(ma);
            PerkAction di;
            di.type = "DisableInterval";
            di.str["IntervalType"] = "Block";
            perks.push_back(di);
            PerkAction mh;
            mh.type = "ModHealthChange";
            mh.num["Frames"] = 3.0;
            mh.num["PerFrameValue"] = -2.0;
            mh.str["Name"] = "burn";
            perks.push_back(mh);
            PerkAction sw;
            sw.type = "Switch";
            perks.push_back(sw);
            PerkAction ab;
            ab.type = "AddBullets";
            perks.push_back(ab);
            sf2::scene::HitRecord rec;
            rec.final_damage = 10.0f;
            const sf2::scene::PerkHitOutcome o =
                sf2::scene::decide_hit_perks(perks, rec);
            std::vector<sf2::scene::ActiveMod> dots = o.install_dots;
            float hp = 50.0f;
            sf2::scene::tick_active_mods(dots, hp, 100.0f);
            std::vector<sf2::scene::ActiveMod> dots2 = o.install_dots;
            float h2 = 50.0f;
            sf2::scene::tick_active_mods(dots2, h2, 100.0f);
            sf2::scene::tick_active_mods(dots2, h2, 100.0f);
            sf2::scene::tick_active_mods(dots2, h2, 100.0f);
            std::printf("  \"perks\": {\"sethit\": [%d, %d, %.17g, %d], "
                        "\"heal\": %.17g, \"add\": %.17g, "
                        "\"impulse\": [%.17g, %.17g, %.17g], "
                        "\"attrs\": [[\"%s\", %.17g]], \"clears\": [[%d, \"%s\"]], "
                        "\"dot\": [\"%s\", %d, %.17g], "
                        "\"noop\": [\"%s\", \"%s\"], "
                        "\"tick\": [%.17g, %d, %d], \"expiry\": [%.17g, %d]},\n",
                        (int)o.f_critical, (int)o.has_critical, (double)o.f_damage,
                        (int)o.has_damage, (double)o.heal, (double)o.dmg_add,
                        o.imp_x, o.imp_y, o.imp_z,
                        o.attr_adds[0].first.c_str(), o.attr_adds[0].second,
                        o.clears[0].first, o.clears[0].second.c_str(),
                        o.install_dots[0].name.c_str(), o.install_dots[0].frames_left,
                        o.install_dots[0].per_frame,
                        o.log[0].c_str(), o.log[1].c_str(),
                        (double)hp, (int)dots.size(), dots[0].frames_left,
                        (double)h2, (int)dots2.size());
        }
        // S17 Bl.strike split (mirrors combat_golden.js S17).
        // b uses o$ (nJa), NOT the contact point (REVIEW A MED fix).
        {
            sf2::scene::HitCapsule cap;
            cap.rest_length = 100.0f;
            cap.p1 = {0.0f, 0.0f, 0.0f};
            cap.p2 = {100.0f, 0.0f, 0.0f};
            cap.weight1 = 2.0f;
            cap.weight2 = 1.0f;
            sf2::scene::CapsuleHit ch;
            ch.hit = true;
            ch.n = {30.0f, 0.0f, 0.0f};
            ch.o = {30.0f, 0.0f, 0.0f};
            ch.point = ch.n;
            sf2::scene::ImpulseResult imp;
            sf2::scene::apply_impulse(cap, ch, {10.0f, 4.0f, 0.0f}, 500.0f, 80.0f,
                                      1880.0f, imp);
            ch.n = {500.0f, 0.0f, 0.0f};
            ch.o = {500.0f, 0.0f, 0.0f};
            ch.point = ch.n;
            sf2::scene::ImpulseResult imp2;
            sf2::scene::apply_impulse(cap, ch, {10.0f, 4.0f, 0.0f}, 500.0f, 80.0f,
                                      1880.0f, imp2);
            std::vector<sf2::scene::Vec3> offs = {{10.0f, 0.0f, 0.0f},
                                                  {0.0f, 5.0f, 0.0f}};
            sf2::scene::decay_knockback(offs);
            std::vector<sf2::scene::Vec3> tiny = {{0.0001f, 0.0f, 0.0f}};
            sf2::scene::decay_knockback(tiny);
            std::printf("  \"wea\": {\"n1\": [%.17g, %.17g], \"n2\": [%.17g, %.17g], "
                        "\"clamp\": [%.17g, %.17g, %.17g], "
                        "\"decay\": [%.17g, %.17g, %.17g, %.17g], "
                        "\"decayzero\": [%.17g, %.17g]},\n",
                        (double)imp.node1_vec.x, (double)imp.node1_vec.y,
                        (double)imp.node2_vec.x, (double)imp.node2_vec.y,
                        (double)(500.0f / 100.0f > 1.0f ? 1.0 : 0.0),
                        (double)imp2.node1_vec.x, (double)imp2.node2_vec.x,
                        (double)offs[0].x, (double)offs[0].y, (double)offs[1].x,
                        (double)offs[1].y, (double)tiny[0].x, (double)tiny[0].y);
        }
        // S17b verbatim Bz (mirrors combat_golden.js S17b).
        {
            auto mkcap = [](float x1, float y1, float x2, float y2, float r) {
                sf2::scene::HitCapsule c;
                c.p1 = {x1, y1, 0.0f};
                c.p2 = {x2, y2, 0.0f};
                c.r1 = c.p1;
                c.r2 = c.p2;
                c.radius = r;
                c.rest_length = 100.0f;
                c.weight1 = 1.0f;
                c.weight2 = 1.0f;
                return c;
            };
            const sf2::scene::HitCapsule capA = mkcap(0, 0, 10, 0, 1);
            const sf2::scene::HitCapsule capX = mkcap(5, -5, 5, 5, 1);
            const sf2::scene::HitCapsule capN = mkcap(9, 0.5f, 20, 0.5f, 1);
            const sf2::scene::HitCapsule capM = mkcap(50, 0, 60, 0, 1);
            const sf2::scene::HitCapsule capZ = mkcap(0, 0, 10, 0, 0);
            const sf2::scene::HitCapsule capC = mkcap(3, 0, 20, 0, 0);
            sf2::scene::CapsuleHit hx, hn, hm, hc;
            const bool bx = sf2::scene::capsule_capsule_overlap(capA, capX, hx);
            const bool bn = sf2::scene::capsule_capsule_overlap(capA, capN, hn);
            const bool bm = sf2::scene::capsule_capsule_overlap(capA, capM, hm);
            const bool bc = sf2::scene::capsule_capsule_overlap(capZ, capC, hc);
            std::printf("  \"bz\": {\"cross\": [%d, %.17g, %.17g, %.17g, %.17g], "
                        "\"near\": [%d, %.17g, %.17g, %.17g, %.17g], "
                        "\"miss\": [%d], "
                        "\"collinear\": [%d, %.17g, %.17g, %.17g, %.17g]},\n",
                        (int)bx, (double)hx.n.x, (double)hx.n.y, (double)hx.o.x,
                        (double)hx.o.y, (int)bn, (double)hn.n.x, (double)hn.n.y,
                        (double)hn.o.x, (double)hn.o.y, (int)bm, (int)bc,
                        (double)hc.n.x, (double)hc.n.y, (double)hc.o.x,
                        (double)hc.o.y);
        }
        // S18 trigger match + conditions (mirrors combat_golden.js S18).
        {
            using sf2::scene::TrigCond;
            using sf2::scene::TrigEvent;
            using sf2::scene::TrigVars;
            using sf2::scene::CondCtx;
            TrigEvent ev;
            ev.type = sf2::scene::kEvHitPostCrit;
            ev.ob = 1;
            ev.critical = 1;
            ev.dmg_min = 5.0;
            TrigVars v;
            v.num["Critical"] = 1.0;
            v.num["Block"] = 0.0;
            v.num["Damage"] = 10.0;
            const bool m18a = sf2::scene::match_hit_event(ev, v, 0, 0);
            TrigVars v2;
            v2.num["Critical"] = 0.0;
            v2.num["Damage"] = 10.0;
            const bool m18b = sf2::scene::match_hit_event(ev, v2, 0, 0);
            TrigVars v3;
            v3.num["Critical"] = 1.0;
            v3.num["Damage"] = 3.0;
            const bool m18c = sf2::scene::match_hit_event(ev, v3, 0, 0);
            const bool m18d = sf2::scene::match_hit_event(ev, v, 0, 1);
            CondCtx own, foe;
            own.style_level = 2;
            own.combo = 3;
            own.hp = 50.0;
            own.round = 2;
            own.hit_dmg = 15.0;
            own.q3["X"] = 1.0;
            own.mods.insert("Icon");
            own.mod_ns["Icon"] = "";
            own.draw01 = []() { return 0.2; };
            foe.hp = 100.0;
            foe.round = 2;
            foe.hit_dmg = 15.0;
            foe.draw01 = []() { return 0.9; };
            TrigCond r1;
            r1.kind = "Random";
            r1.chance = 0.3;
            TrigCond st;
            st.kind = "Style";
            st.s["Min"] = "Brutal";
            st.s["Max"] = "Crazy";
            TrigCond co;
            co.kind = "Combo";
            co.s["Min"] = "1";
            co.s["Max"] = "5";
            TrigCond he;
            he.kind = "Health";
            he.s["Max"] = "40";
            TrigCond me;
            me.kind = "ModExists";
            me.s["Name"] = "Icon";
            TrigCond men = me;
            men.negate = true;
            TrigCond ro;
            ro.kind = "Round";
            ro.s["Number"] = "2";
            TrigCond orc;
            orc.kind = "Operator";
            orc.op = "Or";
            TrigCond or1;
            or1.kind = "Round";
            or1.s["Number"] = "9";
            TrigCond or2;
            or2.kind = "PerkStart";
            orc.nested.push_back(or1);
            orc.nested.push_back(or2);
            TrigCond andc;
            andc.kind = "Operator";
            andc.op = "And";
            andc.nested.push_back(ro);
            andc.nested.push_back(or1);
            TrigCond k1;
            k1.kind = "LessEqual";
            k1.s["Value1"] = "?PlayerParameter[Enemy].Health";
            k1.s["Value2"] = "60";
            TrigCond k2;
            k2.kind = "GreaterEqual";
            k2.s["Value1"] = "?Hit[].Damage";
            k2.s["Value2"] = "10";
            TrigCond k3;
            k3.kind = "Equal";
            k3.s["Value1"] = "?Variable[Y]";
            k3.s["Value2"] = "0";
            TrigCond k4;
            k4.kind = "Less";
            k4.s["Value1"] = "?Abs[0-5]+10";
            k4.s["Value2"] = "20";
            TrigCond k5;
            k5.kind = "Greater";
            k5.s["Value1"] = "?Variable[X]+1";
            k5.s["Value2"] = "1";
            CondCtx nsa, foens;
            nsa.mods.insert("Act");
            nsa.mod_ns["Act"] = "NS";
            CondCtx nsb, foensb;
            nsb.mods.insert("Act");
            nsb.mod_ns["Act"] = "other";
            TrigCond k6;
            k6.kind = "ModExists";
            k6.s["Name"] = "Act";
            k6.s["Namespace"] = "NS";
            std::printf("  \"trigger\": {\"match\": [%d, %d, %d, %d], \"conds\": [%d, %d, "
                        "%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d]},\n",
                        (int)m18a, (int)m18b, (int)m18c, (int)m18d,
                        (int)sf2::scene::eval_cond(r1, own, foe),
                        (int)sf2::scene::eval_cond(r1, foe, own),
                        (int)sf2::scene::eval_cond(st, own, foe),
                        (int)sf2::scene::eval_cond(co, own, foe),
                        (int)sf2::scene::eval_cond(he, own, foe),
                        (int)sf2::scene::eval_cond(me, own, foe),
                        (int)sf2::scene::eval_cond(men, own, foe),
                        (int)sf2::scene::eval_cond(ro, own, foe),
                        (int)sf2::scene::eval_cond(orc, own, foe),
                        (int)sf2::scene::eval_cond(andc, own, foe),
                        (int)sf2::scene::eval_cond(k1, own, foe),
                        (int)sf2::scene::eval_cond(k2, own, foe),
                        (int)sf2::scene::eval_cond(k3, own, foe),
                        (int)sf2::scene::eval_cond(k4, own, foe),
                        (int)sf2::scene::eval_cond(k5, own, foe),
                        (int)sf2::scene::eval_cond(k6, nsa, foens),
                        (int)sf2::scene::eval_cond(k6, nsb, foensb));
        }
        // S18c bus routing (mirrors combat_golden.js S18c; real TrigBus).
        {
            using sf2::scene::PerkTrigger;
            PerkTrigger t;
            t.perk = "T-AV";
            t.enabled = true;
            sf2::scene::TrigEvent e;
            e.type = sf2::scene::kEvHitPostCrit;
            e.ob = 1;
            e.critical = 1;
            t.events.push_back(e);
            sf2::scene::TrigCond mx;
            mx.kind = "ModExists";
            mx.s["Name"] = "Icon";
            mx.negate = true;
            t.conds.push_back(mx);
            sf2::scene::TrigCond ra;
            ra.kind = "Random";
            ra.chance = 0.3;
            t.conds.push_back(ra);
            sf2::scene::PerkAction mi;
            mi.type = "ModIcon";
            t.actions.push_back(mi);
            sf2::scene::TrigBus bus;
            bus.register_side(0, {t}, {});
            bus.register_side(1, {}, {});
            sf2::scene::CondCtx a, b;
            a.draw01 = []() { return 0.2; };
            b.draw01 = []() { return 0.2; };
            sf2::scene::TrigVars vc;
            vc.num["Critical"] = 1.0;
            vc.num["Block"] = 0.0;
            vc.num["Damage"] = 4.0;
            bus.fire(6, vc, true, 0, a, b, 2, 100);
            std::vector<std::pair<PerkTrigger, sf2::scene::PerkAction>> d0, d1;
            bus.drain(0, d0);
            bus.drain(1, d1);
            (void)d1;
            sf2::scene::TrigVars vc0;
            vc0.num["Critical"] = 0.0;
            vc0.num["Damage"] = 4.0;
            bus.fire(6, vc0, true, 0, a, b, 2, 100);
            std::vector<std::pair<PerkTrigger, sf2::scene::PerkAction>> e0;
            bus.drain(0, e0);
            sf2::scene::CondCtx c;
            c.draw01 = []() { return 0.2; };
            c.mods.insert("Icon");
            bus.fire(6, vc, true, 0, c, b, 2, 100);
            std::vector<std::pair<PerkTrigger, sf2::scene::PerkAction>> f0;
            bus.drain(0, f0);
            std::printf("  \"busroute\": {\"q\": [[\"%s\"], [], %d, %d]},\n",
                        d0.empty() ? "-" : d0[0].second.type.c_str(), (int)e0.size(),
                        (int)f0.size());
        }
        // S18b perk loader (verified against perks.xml by combat_diff.py
        // via ElementTree — independent implementation, not a twin).
        {
            std::string xml;
            if (FILE* f = std::fopen("reference/extracted/xml/res/perks.xml", "rb")) {
                char buf[65536];
                std::size_t n = 0;
                while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
                    xml.append(buf, n);
                }
                std::fclose(f);
            }
            const std::map<std::string, sf2::scene::PerkDef> defs =
                sf2::scene::parse_perks_xml(xml);
            const auto av = defs.find("PERK_AVENGER");
            const std::size_t av_trigs = av != defs.end() ? av->second.triggers.size() : 0;
            std::printf("  \"perkload\": {\"count\": %d, \"avenger_trigs\": %d",
                        (int)defs.size(), (int)av_trigs);
            if (av != defs.end()) {
                const sf2::scene::PerkDef& d = av->second;
                const auto sc = d.set_num.find("Chance");
                std::printf(", \"chance\": %.17g, \"trig\": [",
                            sc != d.set_num.end() ? sc->second : -1.0);
                bool first = true;
                for (const sf2::scene::PerkTrigger& t : d.triggers) {
                    if (!first) std::printf(", ");
                    first = false;
                    std::printf("[%d, %d, %d]", (int)t.events.size(),
                                (int)t.conds.size(), (int)t.actions.size());
                }
                std::printf("]");
                if (!d.triggers.empty() && !d.triggers[0].actions.empty()) {
                    std::printf(", \"act0\": \"%s\"",
                                d.triggers[0].actions[0].type.c_str());
                }
            }
            std::printf("},\n");
        }
        // S15 style meter (mirrors combat_golden.js S15).
        {
            const sf2::scene::StyleTable st;
            sf2::scene::StyleMeter m;
            const double c0 = sf2::scene::style_credit(st, m, "HighPunch", 1.1);
            sf2::scene::style_vma(m, c0, 6);
            const double c1 = sf2::scene::style_credit(st, m, "HighPunch", 1.1);
            sf2::scene::style_vma(m, c1, 6);
            sf2::scene::style_decay(m, 0.08);
            sf2::scene::style_vma(m, 2.5, 6);
            std::printf("  \"style\": {\"credit\": [%.17g, %.17g], \"level\": %d, \"best\": %d, \"frac\": %.17g},\n",
                        c0, c1, m.level, m.best, m.frac);
        }
        // Ju frame pick + horizon filter (mirrors ai_golden.js juFrame).
        std::printf("  \"ju\": [{\"k\": %d}, {\"k\": %d}, {\"k\": %d}, {\"k\": %d}, {\"k\": %d},\n",
                    sf2::scene::ju_frame_index(7, 4, 5),
                    sf2::scene::ju_frame_index(4, 4, 5),
                    sf2::scene::ju_frame_index(3, 4, 5),
                    sf2::scene::ju_frame_index(9, 4, 5),
                    sf2::scene::ju_frame_index(4, 4, 0));
        std::printf("   {\"pass\": %s}, {\"pass\": %s}, {\"pass\": %s}],\n",
                    (12 <= 15) ? "true" : "false",
                    (16 <= 15) ? "true" : "false",
                    (15 <= 15) ? "true" : "false");
        // S14 exact Fh.lXa (mirrors combat_golden.js fhLxa; pk EAa order).
        {
            const double pk[6] = {0.0, 3.0, 6.0, 9.0, 12.0, 15.0};
            sf2::scene::PrizeKx k0;
            sf2::scene::PrizeFh f0;
            sf2::scene::fh_lxa(k0, f0, 70.0, 70.0, 0.0, 5.0, 2.0, 1.0, 3.0, pk, 0);
            sf2::scene::PrizeKx k1;
            sf2::scene::PrizeFh f1;
            f1.c6 = 2;
            sf2::scene::fh_lxa(k1, f1, 70.0, 70.0, 0.0, 5.0, 2.0, 1.0, 3.0, pk, 0);
            sf2::scene::PrizeKx k2;
            sf2::scene::PrizeFh f2;
            f2.e6 = 1;
            sf2::scene::fh_lxa(k2, f2, 70.0, 70.0, 0.0, 5.0, 2.0, 1.0, 3.0, pk, 0);
            sf2::scene::PrizeKx k3;
            sf2::scene::PrizeFh f3;
            sf2::scene::fh_lxa(k3, f3, 70.0, 70.0, 5.0, 5.0, 2.0, 1.0, 3.0, pk, 1);
            std::printf("  \"lxa\": [%.1f, %.1f, %.1f, %.1f],\n",
                        k0.m6, k1.m6, k2.m6, k3.m6);
        }
        // S6: Wx=5 fires wqb on the 6th tick, Wx ends -1.
        sf2::scene::ShockState st6;
        st6.weapon_wx = 5;
        std::printf("  \"wqb_fires\": [");
        for (int i = 0; i < 7; ++i) {
            const bool fire = sf2::scene::shock_tick(st6, 0.0f);
            std::printf("%s%s", i ? ", " : "", fire ? "\"wqb\"" : "null");
        }
        std::printf("],\n  \"wqb_wx_end\": %d\n}\n", st6.weapon_wx);
    }
    return 0;
}

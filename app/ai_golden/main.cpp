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
#include "scene/damage.hpp"
#include "scene/move_def.hpp"

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
    std::printf("  \"updates\": [");
    for (int i = 0; i < 12; ++i) {
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

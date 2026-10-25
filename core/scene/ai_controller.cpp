// AI: the decision controller (JS `de` class, sf2.502f0946.js L589-621).
//
// JS semantics (line refs into sf2.502f0946.js):
//   - de.ia (L592-594): per-frame decision pass. Returns a move or null.
//   - R0 (L608): `de.tY ? this.Ca.Fj : P.fP` — enabled when the fighter is
//     AI-controlled (Fj = "NotAI" flag is false) or BothBot is enabled.
//   - mQ (L620): snapshots the fight state into the `Ue` feature context
//     (HP ratios, anim frames, counters, bullet counts, the NPivot
//     distance `Lya`).
//   - the wait counter `eh` (L592): after a decision the AI waits `eh`
//     frames before deciding again; `dsb` (L600) sets a huge wait when the
//     AI enters the "watch" state.
//   - hcb (L598-599): no-decision gate — an active NoDecision interval
//     (Uninterrupt/SemiUninterrupt) or a NoDecision move (Physical) blocks
//     decisions; the AI only decides while a move (incl. the stance idle)
//     is playing.
//   - dqb (L600): the distance category via the UseDefense cumulative
//     draw: 2=CounterAttack range, 3=Dodge range, 4=Block range, 1=far.
//   - Pqb (L604-608): the main decision — facing lock, dodge missile/
//     magic, the safe-attack / attack-table / cautious / counter / block /
//     throw branches, then the QuickAttack/Evade slots.
//   - the tactics table lookups: YAa/Q6a (safe attack, L608-610),
//     XAa (attack table, L611-612), Gea (throw, L613-616), Nwa (quick/
//     evade slots, L603), VAa (dodge, L617).
//   - V1 (L601-602): a candidate move must be in `me` AND its tactics
//     conditions (`FQ(2)`) must pass.
//   - jL (L598 + Md.jL L640): the weighted roulette over the tactic's
//     AnimationWeights; the chosen candidate's wait becomes `eh`.
//
// Approximations remaining (static-first; see MASTER_TODO methodology):
//   - the fight-state fields the JS reads that the ported fighter does not
//     expose yet (body-part anims `vd`, sub-fighter `ih`, the Al physics
//     controller's frame count) are derived from the fields it DOES expose
//     (current anim name, move frame, intervals, positions, HP).
//   - enemy-move-change detection uses the enemy ANIM NAME as a proxy for
//     the JS move-object change (`mwb`->`jwb`); `iwb`'s `eh=1` reset on MY
//     move start is now PORTED (JS `de.iwb` L596 + `de.icb` L598: MY current
//     move matching the shipped `<UnexpectedMoves>` {Physical, Hit,
//     ThrowFall} resets `eh` to 1), using MY move pointer as the same
//     animation-start proxy.
//   - `xaa` applies the Ju-frame horizon `b=Fl+Aea` and the Hu frame pick
//     `Ju.$_(Fl)`; the outcome window pick now follows `Gu.acb`/`Gu.n0`
//     (L610-612) exactly (`NDa[i-1]`, lower edge gated). `yaa` now uses the
//     JS `Q6a` frame source `g=this.Fl`, the Hu pick `L6a(f)`, and the
//     `f!=g -> wait(f-g)` tail (L609-611). `gea` (throw, `Z0()[2]`) now uses
//     the JS `ld` source `ra.nAa(P.t$a(), ld)` (the `<MovementsTables>`
//     `<MovementsMainIterations>` group children) with the label `n0` removal
//     filter, the `bma`/`Pcb` filters, the `P.G9a()` fallback and the final
//     `p0(true)-Fl+1` wait; the `cs` branch is now PORTED (see `gea`); only
//     the `b==1` (`VAa`) sub-fighter path remains a cited divergence
//     (BLOCKED on `vd`, reported).
//   - the table target (NOW JS-EXACT): JS `Wea` (L600) returns the FIGHTER
//     BONE named `row.label` (`da.Ic(label, t0(me,enemy)).ma.x`, resolved on
//     MY fighter); `XAa` then forms `n = a.da.hd()*(Wea - a.da.dw()) + Mu`.
//     The port now computes exactly that (opponent clip mirror, opponent
//     root world-x, bone world-x) - see `xaa`; `yaa` (Q6a) now applies the
//     `xea` sub-frame term too (see `yaa`).
//   - JS `de.V1` (L601-602) additionally rejects a candidate when a
//     HIGHER-`Priority` move sharing its `<Keys>` signature (`a.M7.$Q`, the
//     port's `MoveDef::mirror_exclusive`) is in `me` and its conditions pass
//     (`f.Yz(this.model,null,a.FQ(2))`). NOW PORTED: `v1` applies the `$Q`
//     sibling filter JS-exact (the sibling's `<Conditions>` re-evaluated with
//     the context mutated to the sibling's `xl`/`Wl`/`YH`); the Random-tactic
//     path (`update_random`/`Pkb`) already used `mirror_exclusive`.
// Exact since this wave (no oracle needed — pure JS math):
//   - the `mW` watch-recompute (JS `de.ia` L592): after `dsb` the port now
//     recomputes `eh` from the OPPONENT's move length (`p0`/`zD`/`$I`/`Tea`
//     via the MoveLengthIntervals Strict/Extended names) instead of holding
//     a fixed `eh=120`; `dsb` sets `eh=INT_MIN` verbatim. This restores the
//     decision cadence (the port previously evaluated ~1 pass per 120 frames
//     and was practically inert).
//   - `Pqb`'s `this.ds.pcb(this.Fl)` operand is the OPPONENT's move
//     (`jwb` L596-597 `this.ds=c`), not my own (`st.enemy_move`).
//   - `Da.jf()` stream: owned `DaPrng` (Xx+Rk, L2352/2366); QJa 5-roll cache,
//     `$x` ResponseDelay cache, per-pass dqb/jL/XW/slot draws all on it in
//     JS call order when no roll01 override is injected.

#include "scene/ai.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

#include "scene/conditions.hpp"
#include "scene/move_def.hpp"

namespace sf2::scene {

namespace {

// JS `tb.D6a` (L32256): the event NAME -> the numeric type `Gc.Ih` uses and
// `Su.dea(type)` buckets by.
int js_event_type(const std::string& n) {
    if (n == "AnimationStart") return 9;
    if (n == "AnimationEnd") return 10;
    if (n == "AnimationInterrupted") return 11;
    if (n == "IntervalStart") return 12;
    if (n == "IntervalEnd") return 13;
    if (n == "EveryFrame") return 14;
    if (n == "Hit") return 6;
    if (n == "Strike") return 7;
    if (n == "KeyPressed") return 2;
    if (n == "KeyReleased") return 3;
    if (n == "RoundStage") return 1;
    if (n == "RoundStart") return 4;
    if (n == "RoundEnd") return 5;
    if (n == "Birth") return 15;
    if (n == "ModExpires") return 16;
    if (n == "WallHit") return 8;
    return 0;
}

// Forward declarations (defined further down): the shipped-res reader and
// the nested `<outer><inner>...<Animation Name=..>` extractor.
std::string read_res_file(const char* rel);
std::vector<std::string> xml_animation_names_nested(const std::string& xml,
                                                    const std::string& outer,
                                                    const std::string& inner);

// JS `jc.$k(n)` (L29519) + `jc.d2(n)` (L29525):
//   `$k(a){ return this.name != a ? this.d2(a) : true }`
//   `d2(a){ for(b=0;b<this.xl.length;) if(this.xl[b++]==a) return true;
//           return false }`
// `xl` is the move's ANIMATION-NAME list (`jc.ava`/`m.bd`, L29286/L368460),
// i.e. the move's own name + every name in its TRANSITIVE `<Template>` chain
// — the port's `MoveDef::anim_names` (move_def.cpp: `anim_names` = own name +
// template tags + each collected template node's `Name`). Every AI list test
// (`mcb`/`hcb`/`ka`/`gea`/`Pcb`/`iwb`) and the roulette key test (`iCa` L640
// `a.$k(f)`) routes through `$k`. Testing `template_tags` alone MISSES the
// transitive template names (a nested `<Template>` chain), so it can fail to
// match where the JS matches. `anim_names` is a superset of `{name} ∪
// template_tags`, so this test is exactly `$k`.
bool move_has_name(const MoveDef& m, const std::string& n) {
    if (n.empty()) return false;
    if (m.name == n) return true;
    return std::find(m.anim_names.begin(), m.anim_names.end(), n) !=
           m.anim_names.end();
}

// JS `P.s$a()` = `P.csa` (`computer_settings.xml`
// `<MoveLengthIntervals><Strict><Interval Name=..>`, parse L318270). Read
// from the SAME file the JS reads; the port previously HARDCODED the shipped
// {Uninterrupt, SemiUninterrupt}. (`jc.$I` = `vBa(P.s$a())`.)
const std::vector<std::string>& move_length_strict() {
    static const std::vector<std::string> v = xml_animation_names_nested(
        read_res_file("reference/extracted/xml/res/computer_settings.xml"),
        "MoveLengthIntervals", "Strict");
    return v;
}
// JS `P.r$a()` = `P.bsa` = `<MoveLengthIntervals><Extended>` (parse L318430);
// the shipped {Uninterrupt, SemiUninterrupt, SelfUninterrupt}. (`jc.Tea` =
// `vBa(P.r$a())`.)
const std::vector<std::string>& move_length_extended() {
    static const std::vector<std::string> v = xml_animation_names_nested(
        read_res_file("reference/extracted/xml/res/computer_settings.xml"),
        "MoveLengthIntervals", "Extended");
    return v;
}
// JS `P.sp` = `computer_settings.xml` `<TablesReduction><MovementsTables
// Step="5"/>` (parse L317354: `P.sp=u.I(b.A("MovementsTables").attributes.
// get("Step"),1)`; default 1). The port previously hardcoded 5 at every use
// (`Uea`/`YAa`/`XAa`/`gea`). Shipped Step is 5, so behaviour is preserved.
int tables_reduction_step() {
    static const int v = [] {
        const std::string xml =
            read_res_file("reference/extracted/xml/res/computer_settings.xml");
        const std::size_t p = xml.find("<TablesReduction>");
        if (p == std::string::npos) return 1;
        const std::size_t m = xml.find("<MovementsTables", p);
        if (m == std::string::npos) return 1;
        const std::size_t a = xml.find("Step=\"", m);
        if (a == std::string::npos) return 1;
        const std::size_t s = a + 6;
        const std::size_t e = xml.find('"', s);
        if (e == std::string::npos) return 1;
        int n = 0;
        for (std::size_t i = s; i < e; ++i) {
            const char ch = xml[i];
            if (ch < '0' || ch > '9') return 1;
            n = n * 10 + (ch - '0');
        }
        return n > 0 ? n : 1;
    }();
    return v;
}

// Move-length helpers mirroring the JS `jc` methods the AI reads:
//   p0 (L697): the max Attack-interval finish (1-based frames).
//   zD (L698): the max Uninterrupt-interval finish.
//   $I (L697): vBa(P.s$a()) = the max Strict move-length interval finish
//   Tea (L697): vBa(P.r$a()) = the max Extended move-length finish
// All clamp to the move's end frame (Lj).
int attack_end(const MoveDef& m) {
    int best = 0;
    for (const Interval& iv : m.intervals) {
        if (iv.type == 4) best = std::max(best, iv.end);
    }
    // JS `jc.p0` (L697) does NOT clamp to `Lj` (only `zD` does).
    return best;
}
int uninterrupt_end(const MoveDef& m) {
    int best = 0;
    for (const Interval& iv : m.intervals) {
        if (iv.name == "Uninterrupt") best = std::max(best, iv.end);
    }
    if (m.end_frame > 0) best = std::min(best, m.end_frame);
    return best;
}
// JS `jc.$I` (L697): `vBa(P.s$a())` — the max finish among the Strict
// move-length interval names, READ from computer_settings.xml
// `MoveLengthIntervals/Strict` (shipped {Uninterrupt, SemiUninterrupt}).
int strict_end(const MoveDef& m) {
    int best = 0;
    const std::vector<std::string>& names = move_length_strict();
    for (const Interval& iv : m.intervals) {
        if (std::find(names.begin(), names.end(), iv.name) != names.end())
            best = std::max(best, iv.end);
    }
    return best;
}
// JS `jc.Tea` (L697): `vBa(P.r$a())` — the max finish among the Extended
// move-length interval names, READ from computer_settings.xml
// `MoveLengthIntervals/Extended` (shipped Strict + SelfUninterrupt).
int extended_end(const MoveDef& m) {
    int best = 0;
    const std::vector<std::string>& names = move_length_extended();
    for (const Interval& iv : m.intervals) {
        if (std::find(names.begin(), names.end(), iv.name) != names.end())
            best = std::max(best, iv.end);
    }
    return best;
}

// JS `jc.i0` (L698): the animation sub-frame index of logical frame `a` —
// `(a-qx+1)*(XJ+1)+1` with qx=FirstFrame, XJ=MidFrames. `M6a` (L698) is its
// inverse; `Fl`/`p0(!0)`/`zD(!0)` live in this sub-frame space.
int i0(const MoveDef& m, int a) {
    return (a - m.first_frame + 1) * (m.mid_frames + 1) + 1;
}
// JS `jc.zD(!0)` (L698): the Uninterrupt end (already `Lj`-capped in `zD(!1)`)
// mapped to animation sub-frames.
int uninterrupt_end_full(const MoveDef& m) {
    return i0(m, uninterrupt_end(m) + 1) - 1;
}
// JS `jc.p0(!0)` (L697): the Attack end (NO `Lj` cap in `p0`) mapped to
// animation sub-frames.
int attack_end_full(const MoveDef& m) {
    return i0(m, attack_end(m) + 1) - 1;
}
// JS `jc.vBa` (L697): `$I`/`Tea` — the raw max finish, or 0, mapped through
// `i0(b+1)`. Used for `vBa(P.s$a())` (Strict) and `vBa(P.r$a())` (Extended).
int vba_full(const MoveDef& m, int raw) {
    return raw > 0 ? i0(m, raw + 1) : 0;
}

// JS `de.Ycb` (L620): `if(a.Pe){let b=a.Ua; if(b!=null&&a.ip()<=b.zD(!1))
// return!1} return!0` — false ONLY when the enemy is PLAYING (`a.Pe`) AND
// holds a move (`a.Ua != null`) AND its current frame is inside the move's
// Uninterrupt window. The port omitted the `a.Pe` term, so an enemy in a hit
// reaction (`Pe=!1`) whose `Ua` is still set was wrongly reported
// uninterruptible. Called with the ENEMY's state (JS `de.Ycb(b)` where
// b = the enemy's da controller).
bool ycb(const AiFightState& st) {
    if (!st.enemy_playing || st.enemy_move == nullptr) return true;
    return !(st.enemy_move_frame <= uninterrupt_end(*st.enemy_move));
}

// JS `de.Lbb` (L621): `if(a.Pe){let b=a.Ua; if(b!=null&&a.ip()<=b.p0(!1))
// return!1} return!0` — the same `a.Pe` guard as `Ycb`; false only while the
// enemy is PLAYING inside the move's attack window (`p0(!1)`). Called with
// the ENEMY's state.
bool lbb(const AiFightState& st) {
    if (!st.enemy_playing || st.enemy_move == nullptr) return true;
    return !(st.enemy_move_frame <= attack_end(*st.enemy_move));
}

// JS `jc.M6a` (L698, @356541): `this.qx-1+((a-1)/(this.XJ+1)|0)` — the exact
// inverse of `i0` above; it maps the SUB-FRAME playhead `a` back to the
// LOGICAL frame. `|0` truncates toward zero, which C++ integer division also
// does. (`i0` is defined at the top of this anonymous namespace.)
int m6a(const MoveDef& m, int a) {
    const int denom = m.mid_frames + 1;
    return m.first_frame - 1 + (denom > 0 ? (a - 1) / denom : 0);
}
// JS `jc.ocb` (L698, @356830): `ocb(a){let b=0,c=this.va.xb;for(;b<c.length;){
//  let d=c[b];++b;if(d.name=="Uninterrupt"&&d.start<=a&&a<=d.finish)
//  return!0}return!1}` — the move has an Uninterrupt interval covering the
// LOGICAL frame `a` (`d.start`/`d.finish` are 1-based logical frames).
bool ocb(const MoveDef& m, int a) {
    for (const Interval& iv : m.intervals) {
        if (iv.name == "Uninterrupt" && iv.start <= a && a <= iv.end) {
            return true;
        }
    }
    return false;
}
// JS `jc.pcb` (L700, @356959): `pcb(a){return this.ocb(this.M6a(a))}` with
// `a = this.Fl` (`de.ia` L592 @301322: `this.Fl = b.Pe ? b.kJ()+b.Q_+
// this.j0(this.Uu) : -1`) — `a` is the SUB-FRAME playhead `kJ()+j0`. `M6a`
// (L356541 `qx-1+((a-1)/(XJ+1)|0)`) maps it back to the LOGICAL move frame,
// which `ocb` tests.
//
// [FIX kJ-vs-Xh domain] `Fl` is built from `kJ()` = `Te.lq`, the PER-SUB-STEP
// counter. The previous port fed `st.enemy_playhead` (= `Te.Xh`, the LOGICAL
// counter, one increment per `(MidFrames+1)` sub-steps) into `Fl`, and then
// tested `enemy_move_frame` (= `M0()`) directly here — a different frame. With
// `Fl` now in the sub-frame domain (`Fighter::played_steps() + j0`), `pcb`
// must do the JS `M6a(Fl)` remap, NOT the `i0`/`M6a` identity round-trip.
bool pcb(const MoveDef& m, int sub_frame) {
    return ocb(m, m6a(m, sub_frame));
}

}  // namespace

void AiController::init(const std::string& weapon,
                        const std::vector<TacticsFile>& tactics,
                        const TacticDef* tactic,
                        const std::map<std::string, MoveDef>* moves) {
    weapon_ = weapon;
    tactics_ = tactics;
    tactic_ = tactic;
    if (tactic_ != nullptr) {
        // `<Memory Strikes/RoundFactor>` (`Md.KW`) + the global `<NoDecision>`
        // lists copied onto the tactic at parse.
        set_memory(tactic_->memory_strikes, tactic_->memory_round_factor);
        set_no_decision(tactic_->no_decision_intervals, tactic_->no_decision_moves);
        // `<IgnoredEnemyAnimations>` / `<RandomizingEnemyAnimation>` (JS
        // `P.Xoa` / `P.Vsa`), copied onto every tactic at parse.
        set_ignored_enemy_animations(tactic_->ignored_enemy_animations);
        set_randomizing_enemy_animation(tactic_->randomizing_enemy_animation);
        // `<UnexpectedMoves>` (JS `P.mua` = `P.T$a()`), read by `icb` (L598).
        if (!tactic_->unexpected_moves.empty()) {
            unexpected_moves_ = tactic_->unexpected_moves;
        }
    }
    moves_ = moves;
    // [probe] SF2_TRACE_TACTIC=1: the .dat tactic tables this fighter's AI got
    // (attack/safe/throw record counts) - the Tabular decision reads them.
    if (std::getenv("SF2_TRACE_TACTIC") != nullptr) {
        std::size_t n0 = 0, n1 = 0, n2 = 0;
        for (const TacticsFile& tf : tactics_) {
            n0 += tf.set.tables[0].size();
            n1 += tf.set.tables[1].size();
            n2 += tf.set.tables[2].size();
        }
        std::fprintf(stdout,
                     "[tactic] weapon=%s tactic=%s files=%zu records attack=%zu "
                     "safe=%zu throw=%zu\n",
                     weapon.c_str(), tactic_ != nullptr ? tactic_->name.c_str() : "null",
                     tactics_.size(), n0, n1, n2);
        if (tactic_ != nullptr) {
            const auto& t = *tactic_;
            std::fprintf(stdout,
                         "[tactic] type=%d useDefense cz{base=%.3f lim=%.3f "
                         "cf=%.3f df=%.3f hf=%.3f ff=%.4f lin=%d} "
                         "bda{base=%.3f lim=%.3f cf=%.3f ff=%.4f} "
                         "tba{base=%.3f lim=%.3f cf=%.3f ff=%.4f} "
                         "usa{base=%.3f lim=%.3f} ta{base=%.3f lim=%.3f} "
                         "cm{base=%.3f lim=%.3f}\n",
                         t.type,
                         static_cast<double>(t.counter_attack_chance.base),
                         static_cast<double>(t.counter_attack_chance.limit),
                         static_cast<double>(t.counter_attack_chance.counter_factor),
                         static_cast<double>(t.counter_attack_chance.damage_factor),
                         static_cast<double>(t.counter_attack_chance.hit_factor),
                         static_cast<double>(t.counter_attack_chance.anim_frames_factor),
                         t.counter_attack_chance.linear ? 1 : 0,
                         static_cast<double>(t.dodge_chance.base),
                         static_cast<double>(t.dodge_chance.limit),
                         static_cast<double>(t.dodge_chance.counter_factor),
                         static_cast<double>(t.dodge_chance.anim_frames_factor),
                         static_cast<double>(t.block_chance.base),
                         static_cast<double>(t.block_chance.limit),
                         static_cast<double>(t.block_chance.counter_factor),
                         static_cast<double>(t.block_chance.anim_frames_factor),
                         static_cast<double>(t.use_safe_attack_chance.base),
                         static_cast<double>(t.use_safe_attack_chance.limit),
                         static_cast<double>(t.table_attack_chance.base),
                         static_cast<double>(t.table_attack_chance.limit),
                         static_cast<double>(t.cautious_movements_chance.base),
                         static_cast<double>(t.cautious_movements_chance.limit));
        }
        std::fflush(stdout);
    }
    // The `OO` weapon id (JS `P.dBa` L629-630): the weapon subtype, or the
    // first equivalent's subtype. The native port keeps the weapon name.
    oo_ = weapon;
    wb_.clear();
    ld_.clear();
    vs_.clear();
    fk_ = -1;
    eh_ = 1;
    oC_ = 0;
    tua_ = dua_ = bpa_ = rqa_ = oqa_ = 0.0;
    Mu_ = 0.0;
    lN_ = 0;
    x_ = 0;
    aea_ = 0;
    last_enemy_anim_.clear();
    qja_done_ = false;
    prng_.seed(0);
    pH_ = F8_ = mW_ = XW_ = IB_ = false;
}

// JS `b6a` (L603): the sign of (my x - enemy x)... wait — the JS is
// `a.Fe().ma.x - b.Fe().ma.x >= 0 ? 1 : -1` where a = the fighter passed
// in (the OPPONENT) and b = Ji (MY anim controller's model). So
// opponent.x - my.x >= 0 ? 1 : -1 = the direction TOWARD the opponent.
int AiController::b6a(const AiFightState& st) const {
    return st.enemy_x - st.my_x >= 0.0f ? 1 : -1;
}

int AiController::facing(const AiFightState& st) const { return b6a(st); }

// JS `t0(a,b)` (L618):
//   `a.oa.Fe()==null||b.oa.Fe()==null?0:a.oa.Fe().ma.x<b.oa.Fe().ma.x?1:-1`
// `oa.Fe()` is the fighter's pivot/render root, whose `ma.x` is the port's
// `world_x()`; the port always has it, so the null guard never fires.
int AiController::t0(const AiFightState& st) const {
    return st.my_x < st.enemy_x ? 1 : -1;
}

// JS `Wea(a,b,c)` (L600):
//   `a=b.da.Ic(a,this.t0(b,c)); return a!=null?a.ma.x:3.4028234663852886E38`
// b = MY fighter's model (`this.model`), c = the enemy. The bone lookup +
// `NE` left/right pick live in `Fighter::bone_world_x`.
float AiController::wea(const AiFightState& st, const std::string& label) const {
    // No pose resolver (the ai_demo / golden / probe harnesses that model no
    // skeleton): keep the pre-fix `enemy_x` proxy so those paths are
    // byte-identical to before — the fight always supplies the resolver.
    if (!st.my_bone_world_x) return st.enemy_x;
    return st.my_bone_world_x(label, t0(st));
}

// JS `de.oxb(a,b,c)` (L618): the arena-containment test for a candidate move.
//   `let d=a.zD(!0); d<0&&(d=0);
//    a=b.oa.Ic("NPivot").ma.x + this.t0(b,c)*a.aU.xea(d,"NPivot");
//    c=b.da.zu; return !(a<b.da.yu || c<a)`
// `a` = the candidate move (`jc`), `b` = `this.model` = ME, `c` = `a.Kf()`
// (the opponent; `Kf` returns `this` when `lb` is null, i.e. the enemy itself
// in a 1v1 — so `t0(b,c)` is exactly the port's `t0(st)`).
// `a.zD(!0)` is the move's Uninterrupt end in SUB-FRAMES (`i0(b+1)-1`, L698);
// `a.aU.xea(d,"NPivot")` is the move's own per-frame NPivot offset (the `Jl`
// sub-frame table, `Si.cxb` v=7). `b.da.yu`/`b.da.zu` are MY arena walls.
bool AiController::oxb(const MoveDef& cand, const AiFightState& st) const {
    // No pose resolver (ai_demo / golden / probe harnesses that model no
    // skeleton): the JS quantity cannot be computed, so keep the previous
    // no-filter behaviour for those byte-identical paths.
    if (!st.my_bone_world_x) return true;
    int d = uninterrupt_end_full(cand);
    if (d < 0) d = 0;
    const int facing = t0(st);
    const float pivot_x = st.my_bone_world_x("NPivot", facing);
    const float x = pivot_x + static_cast<float>(facing) * cand.aU.xea(d, "NPivot");
    return !(x < st.my_wall_min || st.my_wall_max < x);
}

// JS `de.bma(a,b,c)` (L618): `return this.oxb(a,b,c.Kf())`.
bool AiController::bma(const MoveDef& cand, const AiFightState& st) const {
    return oxb(cand, st);
}

// JS `mQ` (L620): build the feature state from the fight snapshot.
// Field semantics (exact):
//   o1/q1 = NORMALIZED hp (`this.model.parameters.gd` / `b.parameters.gd`).
//     `gd` is the `xc` actor-state field, written ONLY by `du(a)` (L416196):
//     `this.gd = this.ZV ? this.Zn : a<0?0:a>this.Zn?this.Zn:a` — i.e. it is
//     clamped to `[0, Zn]`. `Zn` is set in `v.Wka` (L619076):
//     `let b=a.aB; a.Zn = b>0?b:1`. The minified `aB` field is assigned ONLY
//     `-1` in the whole shipped bundle (L124470 `this.Ca.aB=-1`; L411643
//     `this.L5=this.aB=-1`), so `Zn==1` and `gd ∈ [0,1]` — a health FRACTION.
//     Independent confirmation: the shipped achievement counter
//     `internal_settings.xml` `<Counter Name="BarelyAliveRounds"
//     Type="HealthRemained" Value="0.1"/>` is compared DIRECTLY against
//     `a.gd` (`xrb(a.gd)` L214711), so gd is a 0..1 fraction. The curves were
//     authored for that scale (`CautiousMovementsChance Base=0.2 Limit=0.4
//     HealthFactor=1 EnemyHealthFactor=-1` needs `(1-o1)-(1-q1) ∈ [-1,1]`).
//     The port stores absolute HP (`me.hp/me.max_hp`, `battle.max_hp=100`),
//     so feed the ratio (clamped, matching `du`).
//   xY = enemy `kJ()` (per-sub-step played steps; `zk.xY=a.da.kJ()` L302908)
//     — now fed `st.enemy_kj`, the sub-step counter, NOT the Xh domain.
//   pZ = `Tba` (max `M2` over the enemy's `vd` CHILD models; 0 when none)
//     — `enemy_max_part_frames`, computed over `children_` (fight.cpp).
//   counter/Xb/tf = strike-memory accumulators (`Cn.d0`, JS `tu` L297387):
//     the enemy's remembered damage/count/hits for the current move, decayed
//     to the model's strike time by the tactic's `<Memory Strikes>`
//     half-life. Fed from `st.strike_memory` (null in standalone probes ->
//     the accumulators read 0, same as the JS pre-round state).
void AiController::mq(const AiFightState& st) {
    AiFeatureState& f = feat_;
    {
        double count = 0.0, xb = 0.0, tf = 0.0;
        if (st.strike_memory != nullptr) {
            st.strike_memory->d0(st.enemy_move, count, xb, tf);
        }
        f.counter = static_cast<float>(count);
        f.xb = static_cast<float>(xb);
        f.tf = static_cast<float>(tf);
    }
    f.o1 = st.my_max_hp > 0.0f
               ? std::min(1.0f, std::max(0.0f, st.my_hp / st.my_max_hp))
               : 0.0f;  // normalized gd (`du` clamps to [0, Zn==1])
    f.q1 = st.enemy_max_hp > 0.0f
               ? std::min(1.0f, std::max(0.0f, st.enemy_hp / st.enemy_max_hp))
               : 0.0f;  // normalized gd
    f.xY = static_cast<float>(st.enemy_kj);
    f.cl = static_cast<float>(st.magic_bullets);
    f.k2 = static_cast<float>(st.ranged);
    f.pz = static_cast<float>(st.enemy_max_part_frames);
    // The NPivot distance (JS `s6a` L619-620): |my NPivot x - enemy NPivot
    // x| — the native port uses the fighter world x.
    f.lya = std::fabs(st.enemy_x - st.my_x);
    f.shift = 0.0f;
    f.my_anim = st.my_anim;
    f.enemy_anim = st.enemy_anim;
    f.zz.clear();
}

// JS `dqb` (L600): the distance category via the UseDefense cumulative
// draw. `this.CZ/bda/tba` are the three curve scores; one roll r:
//   r < CZ        -> 2 (CounterAttack range)
//   r < CZ+bda    -> 3 (Dodge range)
//   r < CZ+bda+tba-> 4 (Block range)
//   else          -> 1 (far)
int AiController::dqb(const AiFightState& st) {
    if (tactic_ == nullptr) return 1;
    AiFeatureState f = feat_;
    f.lya = std::fabs(st.enemy_x - st.my_x);
    const float cz = weight_curve_eval(tactic_->counter_attack_chance, f);
    const float bda = weight_curve_eval(tactic_->dodge_chance, f);
    const float tba = weight_curve_eval(tactic_->block_chance, f);
    // The UseDefense scores are RATIOS in [0,1] — a single draw against
    // the cumulative sum. Keep them as floats (the JS keeps them float).
    CZ_f_ = cz;
    bda_f_ = bda;
    tba_f_ = tba;
    const float r = roll01();
    dbg_.cz = cz;
    dbg_.bda = bda;
    dbg_.tba = tba;
    dbg_.dqb_roll = r;
    // [probe] SF2_TRACE_DQB=1: the raw dqb operands (features + curve result)
    // for the AI's counter/dodge/block category roll.
    if (std::getenv("SF2_TRACE_DQB") != nullptr) {
        std::fprintf(stdout,
                     "[dqb] f=%d counter=%.3f xb=%.3f tf=%.3f xY=%.3f o1=%.3f "
                     "q1=%.3f k2=%.3f pz=%.3f lya=%.1f -> cz=%.4f bda=%.4f "
                     "tba=%.4f r=%.4f\n",
                     st.fight_frame, static_cast<double>(f.counter),
                     static_cast<double>(f.xb), static_cast<double>(f.tf),
                     static_cast<double>(f.xY), static_cast<double>(f.o1),
                     static_cast<double>(f.q1), static_cast<double>(f.k2),
                     static_cast<double>(f.pz), static_cast<double>(f.lya),
                     static_cast<double>(cz), static_cast<double>(bda),
                     static_cast<double>(tba), static_cast<double>(r));
        std::fflush(stdout);
    }
    if (r < cz) return 2;
    if (r < cz + bda) return 3;
    if (r < cz + bda + tba) return 4;
    return 1;
}

// JS `hcb` (L598-599): the no-decision gate.
//   `for(a of P.u$a()) if(this.Ji.cBa(a)!=null) return false;`
//   `if(this.Ji.Pe && this.cs != null) { for(m of P.v$a()) if(this.cs.$k(m))
//    return false } else return false; return true;`
// i.e. NO active NoDecision interval on ME, and MY fighter must be PLAYING
// (`Ji.Pe`), and the ENEMY's current animation (`cs`) must not match a
// NoDecision move. Both lists are loaded from tactic_settings.xml
// `<NoDecision>` (shipped: Intervals = Uninterrupt|SemiUninterrupt,
// Moves = Physical) — the port used the literal "Physical".
bool AiController::hcb(const AiFightState& st) const {
    // `this.Ji.cBa(name) != null` = the interval is active on me.
    for (const auto& iv : st.my_intervals) {
        for (const std::string& n : no_decision_intervals_) {
            if (iv.first == n) {
                dbg_.hcb_reason = 1;
                return false;
            }
        }
    }
    // JS `de.hcb` (L598-599):
    //   `if(this.Ji.Pe && this.cs != null){ a=this.cs; for(...P.v$a()...)
    //     if(a.$k(c[b++])) return !1 } else return !1; return !0`
    //   - `this.cs` is MY OWN current move, NOT the enemy's. It is set by
    //     `de.iwb(a)` (L596), called from `wd.mwb(a)` (L527) as
    //     `b.nf.iwb(a)` with `a` = THIS fighter's started animation; the
    //     sibling call `this.nf.jwb(b)` fills `de.ds` = the OPPONENT's move.
    //     (`de.ia`'s `a` argument is the enemy, but `cs` comes from `iwb`.)
    //   - `this.Ji.Pe` = MY clip is playing. A hit reaction leaves it FALSE:
    //     `wd.ia` (L498) runs `Qnb()` first, and `wd.Qnb` (L507) does
    //     `this.Mwb(...); this.da.reset(); this.da.etb(this.qs.animation)`
    //     — `Te.reset` (L548) sets `Pe=!1` and `Te.Sca` (L548, inside
    //     `Lwb` L511) sets `Pe=!1`, with only the ragdoll latch
    //     `Nd.nk` (`Al.start`, L582) left set. With `Pe=!1` this branch
    //     fails and `de.ia` (L593 `if(!this.hcb())return null`) issues NO
    //     decision — that is the gate that stops the AI replacing a running
    //     reaction. `st.playing` is set with that `!Nd.nk` term in
    //     `FightController::update_fighter`.
    if (!st.playing || st.my_reacting || st.current_move == nullptr) {
        dbg_.hcb_reason = !st.playing ? 2 : (st.my_reacting ? 4 : 3);
        return false;
    }
    // JS `P.v$a()` = the NoDecision MOVES; `cs.$k(name)` matches the name or
    // an inherited template tag.
    for (const std::string& m : no_decision_moves_) {
        if (move_has_name(*st.current_move, m)) {
            dbg_.hcb_reason = 5;
            return false;
        }
    }
    return true;
}

void AiController::set_no_decision(std::vector<std::string> intervals,
                                   std::vector<std::string> moves) {
    if (!intervals.empty()) no_decision_intervals_ = std::move(intervals);
    if (!moves.empty()) no_decision_moves_ = std::move(moves);
}

namespace {
// JS `P.Yra`/`P.Lra` (`tactic_settings.xml` `<MissileAnimations>` /
// `<MagicAnimations>`, parsed by `P.yK` L319806 `P.yK(b,P.Yra)` /
// `P.yK(b,P.Lra)`). Read from the SAME file the JS reads; the port previously
// HARDCODED these three/two names (and cited the wrong file). `P.yK` appends
// every child `<Animation Name=..>`.
std::vector<std::string> anim_names_in_tag(const char* path, const char* tag) {
    std::vector<std::string> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    const std::string xml((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    const std::size_t p = xml.find(std::string("<") + tag + ">");
    if (p == std::string::npos) return out;
    std::size_t end = xml.find(std::string("</") + tag + ">", p);
    if (end == std::string::npos) end = xml.size();
    const std::string block = xml.substr(p, end - p);
    std::size_t k = 0;
    const std::string key = "Name=\"";
    while ((k = block.find(key, k)) != std::string::npos) {
        k += key.size();
        const std::size_t e = block.find('"', k);
        if (e == std::string::npos) break;
        out.push_back(block.substr(k, e - k));
        k = e + 1;
    }
    return out;
}
const std::vector<std::string>& missile_animations() {  // JS `P.o$a()`
    static const std::vector<std::string> v = anim_names_in_tag(
        "reference/extracted/xml/res/tactic_settings.xml", "MissileAnimations");
    return v;
}
const std::vector<std::string>& magic_animations() {  // JS `P.n$a()`
    static const std::vector<std::string> v = anim_names_in_tag(
        "reference/extracted/xml/res/tactic_settings.xml", "MagicAnimations");
    return v;
}
}  // namespace

// JS `fCa` (L599-600): whether the enemy is playing a cautious anim
// (variant 0 = missiles, variant 1 = magic) AND is facing / in reach.
// The native port derives it from the enemy's current animation name.
bool AiController::fca(const AiFightState& st, int variant) const {
    if (st.enemy_anim.empty()) return false;
    // The JS checks each enemy body part's animation controller; the native
    // fighter has one animation, so we check the current anim against the
    // missile/magic animation name lists (P.o$a / P.n$a = the
    // `tactic_settings.xml` `<MissileAnimations>` / `<MagicAnimations>`
    // groups), read from the shipped file, not hardcoded.
    const std::vector<std::string>& list =
        variant == 0 ? missile_animations() : magic_animations();
    for (const std::string& a : list) {
        if (st.enemy_anim == a) return true;
    }
    return false;
}

// JS `V1` (L601-602): a candidate move must be in `me` (the fighter's move
// list) and its tactics conditions (`FQ(2)` = the `Ts` list) must pass.
bool AiController::v1(const MoveDef& m, const AiFightState& st,
                      std::string* why, bool mirror_filter) const {
    if (why != nullptr) why->clear();
    if (moves_ == nullptr || moves_->find(m.name) == moves_->end()) {
        if (why != nullptr) *why = "not-in-table";
        return false;
    }

    // JS `de.V1` (L601-602): `let b=this.model.me; if(!b.includes(a))return!1;`
    // - the candidate must be one of MY OWN moves. The tactic's slot names
    // (`Throw`, `ShortAttack`, the <CautiousMovements> groups) resolve to
    // every move carrying that animation across ALL weapons, so without this
    // gate a Knives fighter accepts Spear/Staff/Tonfa candidates whose clips
    // do not exist; `start_move_impl` then starts a clip-less move and the
    // fighter never animates (the "enemies do not react" symptom).
    if (st.my_moves != nullptr) {
        bool own = false;
        for (const MoveDef* om : *st.my_moves) {
            if (om != nullptr && om->name == m.name) {
                own = true;
                break;
            }
        }
        if (!own) {
            if (why != nullptr) *why = "not-my-move";
            return false;
        }
    }
    // Build a FightContext with the AI's state for the tactics conditions.
    FightContext ctx;
    ctx.roll01 = [this]() { return roll01(); };  // owned stream or override
    ctx.stage = round_stage::fight;
    // JS `Gc.zT` (L347516): `Ae.XH` (the `lg.vQ` slot-1 list a
    // `<CurrentAnimation Name=...>` reads) = the CURRENT move's `xl`
    // (`d=c.Sj(); d!=null&&(a.G=d.xl)`), i.e. the move name PLUS its
    // `<Template>` chain — NOT the bare move name. `V1` only overwrites
    // `xK`/`Wl`/`YH`, never `XH`, so the current move's full `xl` is what the
    // candidate's own `<CurrentAnimation>` guards see. Passing the bare name
    // made `StepForward`'s restart guard `Not(SelfUninterrupt && CurrentAnimation
    // Name="Step")` read `"Step"` against `["StepForward"]` -> FALSE, so the
    // guard was OFF and the tabular AI re-selected `StepForward` every few
    // frames (the reported enemy jitter). `MoveDef::anim_names` is exactly
    // `xl` (name + transitive Template names, move_def.cpp L1094).
    if (st.current_move != nullptr) {
        ctx.anims_me = st.current_move->anim_names;
    } else {
        ctx.anims_me = {st.my_anim};
    }
    ctx.anims_enemy = {st.enemy_anim};
    ctx.dist_x = st.enemy_x - st.my_x;
    ctx.me_x = st.my_x;
    ctx.enemy_x = st.enemy_x;
    // JS `Ae.Wl` (`Vi.SBa` L704): the move's `<SetDirection>` sign =
    // sign(enemy - me); identical to the sign of `dist_x`.
    ctx.direction = ctx.dist_x >= 0.0f ? 1.0f : -1.0f;
    ctx.enemy_direction = -ctx.direction;
    // `AiFightState` carries no location bounds, so the `Object="Wall"`
    // Distance refs (`ee.q9a` L788) fall back to the context defaults — the
    // shipped dojo walls (`FightController` `wall_min_`/`wall_max_`).
    ctx.dist_3d = std::fabs(ctx.dist_x);
    // JS `Ae` (the SHARED fight condition context, `Gc.yma`/`zT` L347516):
    // `de.V1`/`Gc.EZa` evaluate every candidate's `<Distance>` against the
    // POSED bone refs (`ee.nt` L786 resolves `Object="Nodes"`/`"Pivot"` to the
    // current skeleton nodes), not the Me->Enemy root gap. `qm.he` (L744):
    // `b = this.GK.OQ(a) - this.FK.OQ(a)`. Wiring the resolver makes the
    // Random path's candidate set use the same distance as the JS.
    ctx.ref_pos = st.ref_pos;
    // JS `ee.j8a` (L33329): `Object="Pivot"` in the CANDIDATE's own condition
    // tree resolves to the CANDIDATE's `<Align><Pivot Part>` node — `jc.vF`
    // (L29378) stamps `CBa(ctx).Yd` with `a.Va.IF[this.va.align.UE]` for the
    // move whose `vF` last ran, and `de.V1`/`Gc.EZa` evaluate each candidate
    // right after its own `vF`. Bind the candidate so `conditions.cpp`'s
    // `ref_xy` reads the candidate's node; a candidate with no align node
    // (`align.UE < 0`) returns false and the current move's `da.Fe()`
    // (`ref_pos`) stands in, exactly as `j8a`'s `else` branch does.
    if (st.candidate_pivot_pos) {
        const MoveDef* cand = &m;
        const auto& fn = st.candidate_pivot_pos;
        ctx.candidate_pivot_pos = [cand, &fn](int player, bool prev, float& x,
                                             float& y) {
            return fn(*cand, player, prev, x, y);
        };
    }
    ctx.wall_min = st.my_wall_min;
    ctx.wall_max = st.my_wall_max;
    ctx.health_ratio = st.my_max_hp > 0.0f ? st.my_hp / st.my_max_hp : 1.0f;
    ctx.candidate_moves = {m.name};
    for (const auto& iv : st.my_intervals) {
        ctx.intervals.push_back({iv.first, iv.second, true});
    }
    // JS `tm.he` (`CurrentInterval`) reads the per-player list; the `Throw`
    // template's gate is `Player="Enemy"` -> the OPPONENT's intervals.
    for (const auto& iv : st.enemy_intervals) {
        ctx.intervals_enemy.push_back({iv.first, iv.second, true});
    }
    // JS `de.V1` shares the fight's `Ae` context: stamp the item/perk/mod
    // state (`items`/`EA`/`lz`/`Sia`/`S3`) the `<Item>`/`<Weapon>`/`<Perk>`/
    // `<ModExists>` conditions read. The caller (fight.cpp) fills `st` from
    // the SAME sources `fill_ctx_cond_state` uses (the equipped `owned_items`,
    // the live perk sets, the live mods).
    for (const sf2::scene::OwnedItem& oi : st.my_items) {
        ctx.items.push_back({oi.type, oi.subtype, oi.name});
    }
    for (const sf2::scene::OwnedItem& oi : st.enemy_items) {
        ctx.items_enemy.push_back({oi.type, oi.subtype, oi.name});
    }
    for (const std::string& p : st.my_perks) ctx.perks_me.push_back({"", p});
    for (const std::string& p : st.enemy_perks) ctx.perks_enemy.push_back({"", p});
    ctx.mods = st.my_mods;
    ctx.mods_enemy = st.enemy_mods;
    // JS `de.V1` (L601-602): `a.Yz(this.model,null,a.FQ(2))` evaluates the
    // MOVE's condition tree (`va.rb`) — the main `<Conditions>` (Distance /
    // CurrentAnimation / …), not only the `<Tactics>` wrapper. The port used
    // to test `m.tactics` alone, so a distance-gated move (e.g.
    // `ThrowForward`'s `<Distance Max="100">`) was never rejected and the AI
    // started/landed throws from across the arena. Keys conditions pass for
    // the AI (`ctx.keys_gm=false`, the same `gm=false` gate `NS` uses).
    std::string ctrace;
    if (!eval_move_conditions(m.conditions, ctx, &ctrace)) {
        if (why != nullptr) *why = "conditions:" + ctrace;
        return false;
    }

    // JS `de.V1` (L601-602), the `$Q` SIBLING filter (the ONE confirmed AI
    // divergence — verbatim, char offsets 306189-306760):
    //   `var d=a.M7; if(0<d.$Q.length){ let e=0;
    //      for(d=d.$Q;e<d.length;){ let f=d[e];++e;
    //        if(b.includes(f)&&(c.xK=f.xl,c.Wl=f.xD(c,this.Ji.hd()),
    //           c.YH=f.va.align.WE, f.Yz(this.model,null,a.FQ(2)))) return!1 } }`
    // `a.M7` is the `Pu` mirror-exclusive container (`$Q` = its list) built by
    // `ra.c1a`/`ra.b1a` (L683-684) — the port's `MoveDef::mirror_exclusive`
    // (move_def.cpp L159-178, same `a.priority < e.priority` + `$ga` key
    // conflict). `b = this.model.me` = MY move list; `c = this.model.Fc` is
    // the SHARED condition context mutated to the SIBLING's `xl`/`Wl`/`YH`
    // before `f.Yz` re-evaluates the sibling's `<Conditions>` (`va.rb`). So a
    // candidate whose higher-priority mirror superior is ALSO in my move list
    // AND whose own conditions pass is REJECTED. Previously `v1` applied
    // `mirror_exclusive` only in the Random path (`update_random`/`Pkb`), so
    // the tabular AI could pick the lower-priority sibling (e.g. a plain
    // strike over the key-combo variant sharing its `<Keys>` signature).
    //
    // `mirror_filter=false` (the RANDOM path) SKIPS this gate, JS-exact: the
    // Random candidate list is built by `Gc.EZa` (L28531), which never calls
    // `de.V1` — it tests only the slot (`Gc.iEa` -> `Qm.compare`, a type
    // match) and the move's own `<Conditions>` (`f.Yz(b,null,g)`). The `$Q`
    // gate for that path lives in `Gc.Pkb`'s `M7.Wcb` (L28448, `update_random`
    // below), which tests membership in the CANDIDATE list and never
    // re-evaluates the sibling's conditions. Applying `V1`'s stricter
    // (condition-dependent) version here wrongly dropped candidates the JS
    // keeps, so the roulette set differed.
    if (mirror_filter) {
    for (const std::string& sib : m.mirror_exclusive) {
        auto it = moves_->find(sib);
        if (it == moves_->end()) continue;
        const MoveDef& f = it->second;
        // `b.includes(f)` — the sibling must be in MY OWN move list.
        if (st.my_moves != nullptr) {
            bool own = false;
            for (const MoveDef* om : *st.my_moves) {
                if (om != nullptr && om->name == f.name) {
                    own = true;
                    break;
                }
            }
            if (!own) continue;
        }
        // `c.xK = f.xl` (the sibling's animation names) + `c.Wl = f.xD(...)`
        // (the sibling's `<SetDirection>` sign, or my facing). The port's
        // `FightContext` rebuilds per candidate; copy and override.
        FightContext fctx = ctx;
        fctx.candidate_moves = f.anim_names;  // `c.xK = f.xl`
        // `f.Yz(this.model,null,a.FQ(2))` — the SIBLING's `<Conditions>`
        // (`va.rb`), NOT its `<Tactics>` (`va.Ts`). `f.Yz` defaults its list to
        // `this.va.rb`.
        if (eval_move_conditions(f.conditions, fctx)) {
            if (why != nullptr) *why = "mirror-sibling:" + f.name;
            return false;
        }
    }
    }
    std::string ttrace;
    if (!eval_move_conditions(m.tactics, ctx, &ttrace)) {
        if (why != nullptr) *why = "tactics:" + ttrace;
        return false;
    }
    return true;
}

// Resolves a candidate animation/tag name to the move(s) whose name or
// template tags match (JS `ra.yz`/`ra.b9a` L349873: `b9a(a)` returns
// `ra.xC.get(a)` or null; `yz(a,b)` appends `ra.xC.get(a).children` only when
// the key EXISTS — `X.Xa(ra.xC,a)&&(...)`). `ra.xC` is keyed by the move
// TEMPLATE names registered by `Fa.kxb` (L364000) AND by every move's own
// name (`Fa.amb` L364134: `X.Xa(c,b.name)||(a=lh.B4a(b), c.set(a.name,a))`).
// There is no `ShortAttack` template/move anywhere in the shipped moves.xml,
// so the literal `ra.b9a("ShortAttack")` yields null. The tactic's own
// declaration (`<QuickAttackChance Animation="ShortAttack" Base="0.2"/>` +
// `<Animation Name="ShortAttack" Base="1000"/>`) is the author's group name
// for the fighter's basic MELEE swing: the shipped `tactic_settings.xml:93`
// comment says exactly that ("ShortAttack ... the tag must be set on every
// close-combat strike") - i.e. the melee strikes were never tagged with the
// group the tactic references. Resolve the tag to the fighter's `Weapon`-
// tagged moves (the close-combat group the tactic MEANT); `v1` then keeps
// only the fighter's own moves, so a Knives fighter gets KnivesSlash/…, a
// Batons fighter BatonsSlash/…. Without this the base-slot list is empty and
// every Tabular decision falls through to `<CautiousMovements>` (`Step`),
// i.e. the reported "non-boss AI just walks and never attacks".
std::vector<const MoveDef*> resolve_candidate(const std::string& anim,
                                              const std::map<std::string, MoveDef>& moves) {
    std::vector<const MoveDef*> out;
    for (const auto& kv : moves) {
        const MoveDef& m = kv.second;
        if (m.name == anim || m.template_tags.count(anim) > 0 ||
            std::find(m.anim_names.begin(), m.anim_names.end(), anim) !=
                m.anim_names.end()) {
            out.push_back(&m);
        }
    }
    if (out.empty() && anim == "ShortAttack") {
        for (const auto& kv : moves) {
            const MoveDef& m = kv.second;
            if (m.template_tags.count("Weapon") > 0) out.push_back(&m);
        }
    }
    return out;
}

// JS `ABa`/`y0` (L600-601): filter the candidate list by V1.
void AiController::filter_by_v1(std::vector<AiCandidate>& cands,
                                const AiFightState& st) const {
    std::vector<AiCandidate> out;
    out.reserve(cands.size());
    for (const AiCandidate& c : cands) {
        if (c.animation.empty()) {
            out.push_back(c);  // a null-anim (wait-only) candidate passes
            continue;
        }
        if (moves_ == nullptr) continue;
        // Resolve the candidate name to move(s) (name or template tag).
        const std::vector<const MoveDef*> matches = resolve_candidate(c.animation, *moves_);
        bool any = false;
        for (const MoveDef* m : matches) {
            if (v1(*m, st)) {
                any = true;
                break;
            }
        }
        if (any) out.push_back(c);
    }
    cands = std::move(out);
}

// JS `Md.jL` (L640) + `iCa` (L640): the weighted roulette over the tactic's
// AnimationWeights. Returns the chosen candidate index, or -1 when no
// weight matches.
//
// `iCa(a,b)` (L640) tests the `<AnimationWeights>` key `f` via `a.$k(f)`
// (L698): `return this.name!=a?this.d2(a):!0` with `d2(a)` scanning `xl`.
// `xl` (L689/L711 `ava`/L722 `lh.nd`) is the animation's NAME **plus every
// `<Template>` name in its chain — so a tactic keyed on an animation GROUP
// (`ForwardStep`/`BackStep`, the templates of `StepForward`/`StepBack`) still
// matches. The old test compared the key to the candidate's bare move name
// only, so both `StepForward`/`StepBack` fell through to the unnamed
// `<Animation Base="100" Limit="1000"/>` default → equal weights → the
// 50/50 step oscillation (never closing the gap). Now the key is matched
// against the resolved move's name + `template_tags` + `anim_names`.
int AiController::pick(const std::vector<AiCandidate>& cands) const {
    if (tactic_ == nullptr || cands.empty()) return -1;
    auto weight_of = [&](const std::string& anim) -> float {
        for (const auto& kv : tactic_->anim_weights) {
            bool match = kv.first.empty() || kv.first == anim;
            if (!match && moves_ != nullptr) {
                for (const MoveDef* m : resolve_candidate(anim, *moves_)) {
                    if (m->name == kv.first ||
                        m->template_tags.count(kv.first) > 0 ||
                        std::find(m->anim_names.begin(), m->anim_names.end(),
                                  kv.first) != m->anim_names.end()) {
                        match = true;
                        break;
                    }
                }
            }
            if (match) return weight_curve_eval(kv.second, feat_, &anim);
        }
        return 0.0f;
    };
    float sum = 0.0f;
    for (const AiCandidate& c : cands) sum += weight_of(c.animation);
    if (sum <= 0.0f) {
        last_pick_sum_ = sum;
        last_pick_roll_ = 0.0f;
        return -1;
    }
    float r = roll01() * sum;
    last_pick_sum_ = sum;
    last_pick_roll_ = r;
    // [probe] SF2_TRACE_PICK=1: the roulette operands (weights, sum, roll).
    if (std::getenv("SF2_TRACE_PICK") != nullptr) {
        std::fprintf(stdout, "[pick] sum=%.1f roll=%.1f:", sum, r);
        for (const AiCandidate& c : cands) {
            std::fprintf(stdout, " %s=%.1f", c.animation.c_str(),
                         weight_of(c.animation));
        }
        std::fprintf(stdout, "\n");
        std::fflush(stdout);
    }
    for (std::size_t i = 0; i < cands.size(); ++i) {
        r -= weight_of(cands[i].animation);
        if (r < 0.0f) return static_cast<int>(i);
    }
    return static_cast<int>(cands.size()) - 1;
}

// The per-row outcome lookup (JS `PBa` L617 + `Gu.acb`/`Gu.n0` L634): for a
// table row, find the outcome case whose distance window contains `dist` and
// append kd(animation, outcome_id) to `out`. Returns the count added.
// `hu_pick >= 0` restricts to one Hu frame's outcomes (JS `Ju.frames[k]`,
// L611); `horizon >= 0` drops waits beyond it (JS `r<=b`, `b=Fl+Aea`).
namespace {
int pba_append(const TacticRow& row, float dist, std::vector<AiCandidate>& out,
               int hu_pick = -1, int horizon = -1) {
    int added = 0;
    for (const TacticOutcome& oc : row.outcomes) {
        if (hu_pick >= 0 && oc.hu_index != hu_pick) continue;
        // JS `Gu` (g="E8"): JI (`window_edges`) = the float edge list,
        // NDa (`window_outcomes`) = the u32 outcome list. The file stores
        // NDa.length == JI.length - 1 (measured 16769/16769 in the fists
        // table): NDa[i] is the outcome of the window [JI[i], JI[i+1]).
        //
        // JS `Gu.acb(a)` (L634): `0 < JI.length-1 && JI[0] <= a && a < JI[last]`.
        // JS `Gu.n0(a)`  (L634): first i>=1 with `a < JI[i]` -> `NDa[i-1]`,
        //                         else -1.  NOTE `i-1` (the window index),
        //                         not `i` (the first edge above `a`).
        if (oc.window_edges.size() < 2) continue;
        if (!(oc.window_edges.front() <= dist && dist < oc.window_edges.back())) {
            continue;
        }
        int idx = -1;
        for (std::size_t i = 1; i < oc.window_edges.size(); ++i) {
            if (dist < oc.window_edges[i]) {
                idx = static_cast<int>(i);
                break;
            }
        }
        if (idx < 1) continue;
        const std::size_t wi = static_cast<std::size_t>(idx - 1);
        if (wi >= oc.window_outcomes.size()) continue;
        const std::uint32_t outcome = oc.window_outcomes[wi];
        if (outcome == 0) continue;
        // JS `r<=b` (L611): the outcome wait must fit the Ju horizon.
        if (horizon >= 0 && static_cast<int>(outcome) > horizon) continue;
        out.push_back({oc.anim, static_cast<int>(outcome)});
        ++added;
    }
    return added;
}

// JS `Gu.n0(a)` (L634): the outcome id of the window containing `a`
// (`NDa[i-1]` for the first edge `> a`), or -1 when `a` is outside the
// window / no edge fits. Shared by `de.Gea`'s label filter (L313), which
// REMOVES the outcome anim when `0 < n0(target)`.
int window_outcome_at(const TacticOutcome& oc, float dist) {
    if (oc.window_edges.size() < 2) return -1;
    if (!(oc.window_edges.front() <= dist && dist < oc.window_edges.back())) {
        return -1;
    }
    int idx = -1;
    for (std::size_t i = 1; i < oc.window_edges.size(); ++i) {
        if (dist < oc.window_edges[i]) {
            idx = static_cast<int>(i);
            break;
        }
    }
    if (idx < 1) return -1;
    const std::size_t wi = static_cast<std::size_t>(idx - 1);
    if (wi >= oc.window_outcomes.size()) return -1;
    return static_cast<int>(oc.window_outcomes[wi]);
}

// The `<Animation Name="X"/>` names directly under `<tag>...</tag>` in a
// shipped XML. Used for the `de.Gea` (L313) group lists: `P.esa`/`P.$ra`
// (computer_settings.xml `<MovementsTables>`/`<MissileTables>` ->
// `<MovementsMainIterations>`, JS L317641/L317980) and `P.F$a()`/`P.G9a()`
// (`P.Zoa`/`P.Woa` = tactic_settings.xml `<SafeDodges>`/`<EmergencyDodges>`,
// JS L319585/L319635) — read from the SAME files the JS reads.
std::vector<std::string> xml_animation_names(const std::string& xml,
                                             const std::string& open,
                                             const std::string& close) {
    std::vector<std::string> out;
    const std::size_t p = xml.find("<" + open + ">");
    if (p == std::string::npos) return out;
    std::size_t end = xml.find("</" + close + ">", p);
    if (end == std::string::npos) end = xml.size();
    const std::string block = xml.substr(p, end - p);
    std::size_t k = 0;
    const std::string key = "Name=\"";
    while ((k = block.find(key, k)) != std::string::npos) {
        k += key.size();
        const std::size_t e = block.find('"', k);
        if (e == std::string::npos) break;
        out.push_back(block.substr(k, e - k));
        k = e + 1;
    }
    return out;
}

// The nested section: `<outer>` then `<inner>...</inner>`.
std::vector<std::string> xml_animation_names_nested(const std::string& xml,
                                                    const std::string& outer,
                                                    const std::string& inner) {
    const std::size_t p = xml.find("<" + outer + ">");
    if (p == std::string::npos) return {};
    return xml_animation_names(xml.substr(p), inner, inner);
}

std::string read_res_file(const char* rel) {
    std::ifstream in(rel, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// JS `P.t$a()` -> `P.esa` (computer_settings.xml L317641).
const std::vector<std::string>& gea_movements_main() {
    static const std::vector<std::string> v = [] {
        return xml_animation_names_nested(
            read_res_file("reference/extracted/xml/res/computer_settings.xml"),
            "MovementsTables", "MovementsMainIterations");
    }();
    return v;
}

// JS `P.G9a()` -> `P.Woa` = `<EmergencyDodges>` (tactic_settings.xml).
const std::vector<std::string>& gea_emergency_dodges() {
    static const std::vector<std::string> v = [] {
        return xml_animation_names(
            read_res_file("reference/extracted/xml/res/tactic_settings.xml"),
            "EmergencyDodges", "EmergencyDodges");
    }();
    return v;
}

// JS `P.F$a()` -> `P.Zoa` = `<SafeDodges>` (tactic_settings.xml).
const std::vector<std::string>& gea_safe_dodges() {
    static const std::vector<std::string> v = [] {
        return xml_animation_names(
            read_res_file("reference/extracted/xml/res/tactic_settings.xml"),
            "SafeDodges", "SafeDodges");
    }();
    return v;
}
}  // namespace

// Finds the record for the current enemy animation in the tactics table
// selected by `table_index` (JS `Z0()[table_index]`):
//   0 = attack (JS `XAa` L611 `Z0()[0]`),
//   1 = safe   (JS `Q6a` L609 `Z0()[1]`),
//   2 = throw  (JS `Gea` L613 `Z0()[2]`).
// Each parsed file carries its JS table index in `TacticsFile::version` (the
// `h`/`b` int of `Si.dxb`/`Si.cxb`, routed by `tactics_parse_file`), so the
// file must match the requested table — previously ONE table (index 0) was
// searched for all three decisions.
const TacticRecord* AiController::find_record(const std::string& key_anim,
                                              int table_index) const {
    if (table_index < 0 || table_index > 2) return nullptr;
    // [FIX table-path deadness] `de.XAa`/`de.Q6a`/`de.Gea` (L25715/L25652/
    // L25803) look their record up in `this.ds.Z0()[table]` — a PER-MOVE list
    // returned by `jc.Z0()` (L29154: `return this.vC`). `P.hKa` (L26594-26614)
    // runs on load and CLEARS every move's `Z0()` lists
    // (`k.first=null; k.second="delete"` then `f.length = 0`), and NOTHING in
    // the shipped build ever refills them: `sb.load` (L27280) only fills the
    // `sb.FM` records, `Si.cxb`/`Si.dxb` (L27510/L27552) push those into the
    // GLOBAL `P.wO[table]` via `P.$ua` (L26508), and no code writes
    // `Z0()/vC`. So `ds.Z0()[table]` is ALWAYS empty, the `for(f of …)` scan
    // never runs, `c` stays `null` and every table path returns 0.
    //
    // Verified against the oracle: over the full accumulated trace (4818
    // `{"t":"oracle"}` frames spanning boss/tournament/challenge runs)
    // `chances.fk` is only ever -1 (never decided), -2 (the `dsb` watch),
    // 5 (cautious movements) and 9 (evade-throw dodges) — NEVER 0/1/2 (the
    // three table paths) nor 6 (quick attacks). At the challenge decision
    // (ora f=353) `fk=5` and the JS starts `StepForward` from `P.nCa()`,
    // while the port's table path returned 10 candidates and picked
    // `HeavyPunch`. The port therefore models the empty `Z0()` by returning
    // no record.
    // The per-move `Z0()` record list is therefore always empty, so the
    // lookup can never produce a record: `XAa`/`Q6a`/`Gea` all fall through
    // with `c == null` and return 0, exactly as the oracle shows. (The
    // `.dat` blobs are still parsed by `tactics_parse_file` for the record
    // layout, but the JS never consults them.)
    (void)key_anim;
    (void)table_index;
    return nullptr;
}

int AiController::yaa(const AiFightState& st) {
    wb_.clear();
    // JS `YAa` (L600): `this.Ao = this.Fl % P.sp != 0 ? !0 : !1` — `P.sp`
    // READ from computer_settings.xml `<TablesReduction><MovementsTables
    // Step="5"/>` (was hardcoded 5).
    const int sp = tables_reduction_step();
    Ao_ = (Fl_ % sp) != 0;
    if (st.enemy_anim.empty()) return 0;

    const TacticRecord* rec = find_record(st.enemy_anim, /*safe=*/1);
    dbg_.rec = rec != nullptr;
    if (rec == nullptr) return 0;
    dbg_.rows = static_cast<int>(rec->rows.size());
    dbg_.fl = Fl_;
    if (!rec->rows.empty()) { dbg_.rda0 = rec->rows[0].rda; dbg_.huf0 = rec->rows[0].hu_frames; }

    // JS `Q6a` (L609-611) is called as
    //   `this.Q6a(a, this.cs, this.ds, this.Ji.dw(), a.da.dw(),
    //             this.q7, this.Fl, this.Mu)`
    // so its `g` parameter is `this.Fl` (the ENEMY's offset frame), NOT the
    // enemy's `Tba`/`M2`. The port previously fed `enemy_max_part_frames`
    // (JS `pZ` = `Tba`) here, so both the `f-g` wait and the Hu frame pick
    // were computed from the wrong quantity.
    const int g = Fl_;
    // Round the enemy frame up to a P.sp multiple (JS L610: `f = g%P.sp!=0
    // ? g+P.sp-g%P.sp : g`; `P.sp` READ from computer_settings.xml).
    const int f = (g % sp) != 0 ? g + sp - g % sp : g;

    // For each condition row, the target distance (JS `Wea` L600 + L610:
    // the row label's bone world-x) and the frame window:
    //   l*(t + (b.aU.xea(f,r) - b.aU.xea(g,r))*d - e) + h
    // where t = `Wea(label, my, enemy)` = MY fighter's bone world-x, l = the
    // OPPONENT's clip mirror (`a.da.hd()`), d = MY clip mirror (`this.Ji.hd()`
    // = `st.my_facing`), e = the enemy's dw, h = the DistanceError draw.
    // All terms are now applied JS-exact, including the `xea` sub-frame
    // difference (`b.aU`, MY current move's v=7 table) and the `-e` term.
    // The Hu frame pick is JS `PBa`'s `L6a(c)` (L617 + L628):
    // `a=this.$_(c); return -1<a ? frames[a] : null` — the single Hu frame
    // at the ROUNDED frame `f`, or NO outcomes when `-1`. Previously the
    // port passed `hu_pick=-1` (every Hu frame), over-approximating.
    for (const TacticRow& row : rec->rows) {
        const int k = ju_frame_index(f, row.rda, row.hu_frames);
        if (k < 0) continue;  // JS `L6a` -> null -> row contributes nothing
        ++dbg_.jrows;
        // JS `Q6a` (L609-611, exact): the row target is
        //   n = f*(Wea(row.label, this.model, a) - e) + this.Mu
        // where f = `a.da.hd()` = the OPPONENT's clip mirror and e = `a.da.dw()`
        // = the OPPONENT's root world-x. The port previously used
        // `my_facing*Wea + Mu`, which dropped the `-e` term (left the target at
        // the raw bone world-x, ~800, instead of the range-appropriate gap) and
        // used the wrong mirror (my own facing, not the opponent's `hd()`), so
        // every safe-table window selected the wrong band. Same shell as `XAa`.
        // JS `Q6a` L610 (exact):
        //   l*(t + (b.aU.xea(f,r) - b.aU.xea(g,r))*d - e) + h
        // `b` = `this.cs` (MY current move), `d` = `this.Ji.hd()` (MY clip
        // mirror = `st.my_facing`), and the two `xea` reads are the sub-frame
        // offset of the row's label at the ROUNDED frame `f` and at `g=Fl_`
        // (0 when the label/frame is out of range).
        float xea_term = 0.0f;
        if (st.current_move != nullptr && !st.current_move->aU.empty()) {
            const SubFrameTable& au = st.current_move->aU;
            xea_term = (au.xea(f, row.label) - au.xea(g, row.label)) *
                       static_cast<float>(st.my_facing);
        }
        const float wv = wea(st, row.label);
        const float target = static_cast<float>(st.enemy_clip_mirror) *
                                 (wv + xea_term - st.enemy_dw) +
                             static_cast<float>(Mu_);
        dbg_.target = target;
        dbg_.mu = static_cast<float>(Mu_);
        dbg_.label = row.label;
        dbg_.wea = wv;
        dbg_.dw = st.enemy_dw;
        dbg_.hd = st.enemy_clip_mirror;
        pba_append(row, target, wb_, k);
    }
    const int c = static_cast<int>(wb_.size());
    // JS L611: `if(f==g||0==c) return c; this.wb.length=0;
    //          this.wb.push(new kd(null,f-g)); return 1`.
    if (f == g || c == 0) return c;
    wb_.clear();
    wb_.push_back({std::string(), f - g});
    return 1;
}

// JS `XAa` (L611-612): the attack-table selection (Z0()[0]).
int AiController::xaa(const AiFightState& st) {
    wb_.clear();
    // JS `XAa` L611: `if(this.Fl % P.sp != 0) { this.Ao=!0; return 0 }` —
    // `P.sp` READ from computer_settings.xml `<TablesReduction>` (was 5).
    const int sp = tables_reduction_step();
    if (Fl_ % sp != 0) {
        Ao_ = true;
        return 0;
    }
    Ao_ = false;
    if (st.enemy_anim.empty()) return 0;

    // JS `XAa` (L611): `for(var b=this.Aea(this.Eqa),...)` then
    // `b=this.Fl+b` — the Ju-frame horizon = `Fl + Aea(Eqa)`. The draw is
    // consumed here so the shared `Da.pg` stream position matches.
    aea_ = aea_draw();

    const TacticRecord* rec = find_record(st.enemy_anim, /*attack=*/0);
    dbg_.rec = rec != nullptr;
    if (rec == nullptr) return 0;
    dbg_.rows = static_cast<int>(rec->rows.size());
    dbg_.fl = Fl_;
    if (!rec->rows.empty()) { dbg_.rda0 = rec->rows[0].rda; dbg_.huf0 = rec->rows[0].hu_frames; }

    // JS L611-612 (exact):
    //   b = this.Fl + this.Aea(this.Eqa)                 // horizon
    //   for each Ju row: k = row.$(this.Fl)              // Hu frame pick
    //     n = hd_e*(Wea(row.label, this.model, a) - dw_e) + this.Mu
    //     for each Gu in frames[k].Ny: r = Gu.n0(n)
    //       0 < r && animation != null && r <= b          // kept
    // The horizon IS applied (each kept outcome has wait <= Fl+Aea, via
    // `pba_append`'s `horizon` argument below).
    // The target (JS L611-612, exact):
    //   f = a.da.hd()                            // OPPONENT's clip mirror
    //   e = a.da.dw()                            // OPPONENT's root world-x
    //   n = f*(Wea(row.label, this.model, a) - e) + this.Mu
    // `Wea` resolves the bone on MY model (`this.model`) with `t0(me,enemy)`;
    // the multiplier is the OPPONENT's `hd()`, NOT my own `b6a` facing, and
    // the `-e` term is the OPPONENT's root world-x. The old port used
    // `my_facing*Wea + Mu`, which left every row's window distance at the
    // bone-local offset (≈0 for a heel bone) so `Gu.n0` always selected the
    // CLOSE band (throws) instead of the range-appropriate strike band.
    const int horizon = Fl_ + aea_;
    for (const TacticRow& row : rec->rows) {
        const int k = ju_frame_index(Fl_, row.rda, row.hu_frames);
        if (k < 0) continue;
        ++dbg_.jrows;
        const float wv = wea(st, row.label);
        const float target = static_cast<float>(st.enemy_clip_mirror) *
                                 (wv - st.enemy_dw) +
                             static_cast<float>(Mu_);
        dbg_.target = target;
        dbg_.mu = static_cast<float>(Mu_);
        dbg_.label = row.label;
        dbg_.wea = wv;
        dbg_.dw = st.enemy_dw;
        dbg_.hd = st.enemy_clip_mirror;
        pba_append(row, target, wb_, k, horizon);
    }
    // JS `XAa` (L611-612, exact): the STORED wait is the move's `$I()` (the
    // Strict end), NOT the `Gu.n0(n)` window outcome. The JS inline loop is
    //   `0<r&&k.animation!=null&&r<=b&&(this.ld.push(k.animation),
    //    this.vs.push(k.animation.$I()))`
    // — `r = Gu.n0(n)` is used ONLY as the `0<r && r<=b` filter (applied
    // inside `pba_append`), while the wait pushed is `k.animation.$I()` =
    // `vBa(P.s$a())` (the max Strict move-length finish). `PBa` (the SAFE
    // table `Q6a`, L617) stores the raw `n0` outcome, so the shared
    // `pba_append` pushes the outcome; `XAa` overwrites it here.
    // `$I()` is computed on the RESOLVED move (JS `k.animation`); when the
    // name does not resolve (a probe with no move map) the `n0` outcome is
    // kept as the fallback.
    if (moves_ != nullptr) {
        for (AiCandidate& c : wb_) {
            auto it = moves_->find(c.animation);
            if (it != moves_->end())
                c.wait = vba_full(it->second, strict_end(it->second));
        }
    }
    // JS `XAa` (L612): after `y0` the candidates are filtered by `bma`
    // (`this.bma(g,this.model,a)` — the displaced NPivot must stay in MY
    // arena). The port applies it to the resolved move names.
    if (moves_ != nullptr) {
        std::vector<AiCandidate> keep;
        keep.reserve(wb_.size());
        for (const AiCandidate& c : wb_) {
            auto it = moves_->find(c.animation);
            if (it == moves_->end() || bma(it->second, st)) keep.push_back(c);
        }
        wb_ = std::move(keep);
    }
    return static_cast<int>(wb_.size());
}

// JS `Gea` (L613-616): the throw-table selection (Z0()[2]). The JS uses
// the move's third table (throws) with a single candidate; the native port
// looks up the throw table records the same way. The target is the same
// `Wea` bone world-x.
int AiController::gea(const AiFightState& st, int variant) {
    const int b = variant;
    wb_.clear();
    // JS `de.Gea(a,b=1)` (L613, reached only from `de.VAa(a,1)` L604/617):
    //   `c = a.da.M2` (the sub-fighter's `Tba`-style max `M2` frame count),
    //   `d = a.da.Qda` (its THROW move), `f = d.Z0()[2]` (its throw table),
    //   then the same label loop but `k=f.dw(); b==1&&(k=f.Iha)` and
    //   `r = b==1 ? c : q.$_(...)`. The ONLY caller `VAa` iterates the
    //   fight's `vd` SUB-FIGHTERS (`a.vd[e]`, each `g.da` a full `wd`), which
    //   the port does not model (see ai.hpp "body-part anims `vd`"); `vaa`
    //   below is a placeholder. BLOCKED on `vd` (reported). Adding `M2`/
    //   `Qda` accessors without `vd` would be dead code.
    if (b != 0) return 0;
    if (st.enemy_anim.empty() || st.enemy_move == nullptr || moves_ == nullptr)
        return 0;

    const int c = Fl_;  // b==0 -> `c = this.Fl`
    // JS `Gea` L613: `if(c % P.sp != 0) { this.Ao=!0; return 0 }` — `P.sp`
    // READ from computer_settings.xml `<TablesReduction>` (was 5).
    const int sp = tables_reduction_step();
    if (c % sp != 0) {
        Ao_ = true;
        return 0;
    }
    Ao_ = false;

    const MoveDef& d = *st.enemy_move;  // `d = a.da.Ua`
    // JS `e = d.Z0()[2]` — the single throw-table record (the JS returns 0
    // unless the group and weapon lists are each exactly one entry; the port
    // resolves the single weapon-matching record).
    const TacticRecord* e = find_record(st.enemy_anim, /*throw=*/2);
    dbg_.rec = e != nullptr;
    if (e == nullptr) return 0;

    // JS `Gea` `cs` branch (L613), verbatim:
    //   `var k=this.cs; if(this.Ji.Pe&&k!=null){
    //      var l=k.PX; l!=null&&(k=l);
    //      l=!1; for(n=0;n<h.length;) if(k.$k(h[n++])){l=!0;break}   // h=YCa()
    //      if(l){ var q=e.aCa(k.va.align.CK);
    //        if(q!=null){ var r=q.$_(this.Uea(c-this.q7));
    //          if(-1<r){ h=f.dw(); b==1&&(h=f.Iha);
    //            h=f.hd()*(this.Ji.dw()-h)+this.Mu;
    //            l=!1; for(q=q.frames[r].Ny;...) if(r.animation==k&&0<r.n0(h)){l=!0;break}
    //            if(!l) return this.wb.push(new kd(null,d.p0(!0)-this.Fl+1)),this.wb.length }}}}`
    // `h = P.YCa()` = `P.fsa` = computer_settings.xml
    // `<MovementsTables><MovementsLastIteration>` (shipped StanceIdle /
    // StepForward / StepBack) — a DIFFERENT list from the tactic
    // `<NoDecision><Moves>` (`P.psa`) used by `hcb`.
    // `k.va.align.CK` = the `<Align><Pivot Part=..>` (`Fa.jva` L659:
    // `b.CK=c.attributes.get("Part")` with `c=a.A("Pivot")`) = the port's
    // `Align::pivot_part`. `Uea(a)= a%sp==0 ? a : (a>0 ? a-a%sp+sp : a-a%sp)`
    // (sp=5). `k.PX` (the `tub` redirect) is unmodeled (no shipped redirect).
    if (st.playing && st.current_move != nullptr) {
        static const std::vector<std::string> fsa = [] {
            return xml_animation_names_nested(
                read_res_file("reference/extracted/xml/res/computer_settings.xml"),
                "MovementsTables", "MovementsLastIteration");
        }();
        const MoveDef* k = st.current_move;  // `this.cs`
        bool l = false;
        for (const std::string& n : fsa) {
            if (move_has_name(*k, n)) {
                l = true;
                break;
            }
        }
        if (l) {
            const std::string ck = k->align.pivot_part;  // `k.va.align.CK`
            const TacticRow* q = nullptr;
            for (const TacticRow& row : e->rows) {
                if (row.label == ck) {  // `Il.aCa`: `ju[d].label==a`
                    q = &row;
                    break;
                }
            }
            if (q != nullptr) {
                const auto uea = [sp](int a) -> int {
                    // JS `Uea` (L304312): `a % P.sp == 0 ? a : (a > 0 ?
                    // a - a%P.sp + P.sp : a - a%P.sp)`; `P.sp` READ from
                    // computer_settings.xml `<TablesReduction>` (was 5).
                    if (a % sp == 0) return a;
                    return a > 0 ? a - a % sp + sp : a - a % sp;
                };
                const int r = ju_frame_index(uea(Fl_ - q7_), q->rda,
                                             q->hu_frames);
                if (r >= 0) {
                    // `h=f.dw(); b==1&&(h=f.Iha); h=f.hd()*(this.Ji.dw()-h)+Mu`
                    // (b==0 here: c == this.Fl).
                    const float target =
                        static_cast<float>(st.enemy_clip_mirror) *
                            (st.my_dw - st.enemy_dw) +
                        static_cast<float>(Mu_);
                    l = false;
                    for (const TacticOutcome& oc : q->outcomes) {
                        if (oc.hu_index != r) continue;
                        if (oc.anim != k->name) continue;
                        if (window_outcome_at(oc, target) > 0) {
                            l = true;
                            break;
                        }
                    }
                    if (!l) {
                        // `return this.wb.push(new kd(null,
                        //   d.p0(!0)-this.Fl+1)),this.wb.length`
                        wb_.clear();
                        wb_.push_back(
                            {"", attack_end_full(d) - Fl_ + 1});
                        return static_cast<int>(wb_.size());
                    }
                }
            }
        }
    }

    // this.ld.length=0; ra.nAa(P.t$a(), this.ld) — the group CHILDREN
    // (`ra.xC.get(tpl).children` = every move whose name/transitive template
    // chain carries `tpl`, i.e. `resolve_candidate`).
    std::vector<std::string> ld;
    for (const std::string& tpl : gea_movements_main()) {
        for (const MoveDef* m : resolve_candidate(tpl, *moves_)) {
            if (m != nullptr) ld.push_back(m->name);
        }
    }
    // bma filter (`this.bma(n,this.model,a)`).
    {
        std::vector<std::string> keep;
        keep.reserve(ld.size());
        for (const std::string& n : ld) {
            auto it = moves_->find(n);
            if (it == moves_->end() || bma(it->second, st)) keep.push_back(n);
        }
        ld = std::move(keep);
    }
    std::size_t gsz = ld.size();

    // JS label loop (L313): for each throw-record row resolve the Hu frame
    // (`q.$_(this.Uea(c))`; `Uea(Fl)==Fl` because Fl%5==0), form the window
    // target and REMOVE every outcome anim with `0 < Gu.n0(target)`. A row
    // whose Hu frame is out of range clears the whole list (`else g=0`).
    {
        const int hd = st.enemy_clip_mirror;
        const float dw = st.enemy_dw;
        for (const TacticRow& q : e->rows) {
            const int r = ju_frame_index(Fl_, q.rda, q.hu_frames);
            if (r < 0) {
                gsz = 0;  // `else g=0`
                continue;
            }
            const float wv = wea(st, q.label);
            const float target = static_cast<float>(hd) * (wv - dw) +
                                 static_cast<float>(Mu_);
            const int frame = (r < q.hu_frames) ? r : 0;  // `t=q.frames[r]||[0]`
            for (const TacticOutcome& oc : q.outcomes) {
                if (oc.hu_index != frame) continue;
                if (window_outcome_at(oc, target) <= 0) continue;
                for (std::size_t A = 0; A < gsz; ++A) {
                    if (ld[A] == oc.anim) {  // `this.ld[A]==t.animation`
                        --gsz;
                        ld[A] = ld[gsz];
                        break;
                    }
                }
            }
        }
    }
    ld.resize(gsz);

    // `in_group(name, list)` = JS `Pcb(a)` (`a.$k(c)` for a `c` in the list:
    // name or transitive template match).
    auto in_group = [&](const std::string& name,
                        const std::vector<std::string>& group) -> bool {
        for (const std::string& c : group) {
            auto it = moves_->find(name);
            if (it != moves_->end() && move_has_name(it->second, c)) {
                return true;
            }
        }
        return false;
    };
    // `y0(this.ld)` (L600-601) = the V1 filter.
    auto filter_v1 = [&](std::vector<std::string> v) -> std::vector<std::string> {
        std::vector<std::string> out;
        out.reserve(v.size());
        for (const std::string& n : v) {
            auto it = moves_->find(n);
            if (it == moves_->end() || v1(it->second, st)) out.push_back(n);
        }
        return out;
    };

    // JS `if(f.yD(2)!=null && a!=null && c)` (c = `f.Pe`, the enemy playing):
    // drop every move whose Uninterrupt end >= the enemy move's, unless it is
    // a `<SafeDodges>` member (P.F$a()=P.Zoa).
    if (st.enemy_playing) {
        const int thresh = uninterrupt_end_full(d) - Fl_;
        std::vector<std::string> keep;
        keep.reserve(ld.size());
        for (const std::string& n : ld) {
            auto it = moves_->find(n);
            const bool drop =
                it != moves_->end() &&
                thresh <= uninterrupt_end_full(it->second) &&
                !in_group(n, gea_safe_dodges());
            if (!drop) keep.push_back(n);
        }
        ld = std::move(keep);
    }

    // `0<this.ld.length && this.y0(this.ld)` — the V1 filter.
    ld = filter_v1(std::move(ld));

    // `if(this.ld.length==0 && b==0)` — the `P.G9a()` (`<EmergencyDodges>`)
    // fallback, then `y0(this.ld,null,!0)` (the `Vcb` filter; the port's
    // `v1` is the shared approximation — cited divergence).
    if (ld.empty()) {
        for (const std::string& tpl : gea_emergency_dodges()) {
            for (const MoveDef* m : resolve_candidate(tpl, *moves_)) {
                if (m != nullptr) ld.push_back(m->name);
            }
        }
        ld = filter_v1(std::move(ld));
    }
    // `a=this.ld.length; 0<a && (a=this.y0(this.ld))`.
    ld = filter_v1(std::move(ld));

    // Final: `c = d.p0(!0)-this.Fl+1` for every survivor.
    const int wait = attack_end_full(d) - Fl_ + 1;
    for (const std::string& n : ld) wb_.push_back({n, wait});
    return static_cast<int>(wb_.size());
}

// JS `Nwa` (L603): the QuickAttack/Evade slot candidates — each slot whose
// chance curve passes (the `Gl`/`bsb` gate: `N_ = t4 < gZ` where gZ is the
// curve score and t4 the last-fire frame) contributes its animation names.
// The slot fires when `roll < score` (JS `Gl.N_` — a per-pass chance draw).
const AiAnimSlot* AiController::bqb(const std::vector<AiAnimSlot>& slots,
                                    const std::string& anim) const {
    // JS `bqb` (L642): pick the highest-priority slot whose conditions
    // pass (`d.compare()`); if none, return null. `Nwa` (L603) uses this
    // for the CONDITIONAL slot lists; the BASE `$E`/`nD` lists are gated by
    // the `bsb`/`Yqb` chance rolls instead (consumed in `Pqb` L606).
    const AiAnimSlot* best = nullptr;
    for (const AiAnimSlot& s : slots) {
        if (s.names.empty()) continue;
        bool ok = true;
        // The slot conditions are evaluated against the fight state; the
        // native port evaluates them like move conditions (documented:
        // the demo's tactics use condition-free slots).
        for (const Cond& c : s.conditions) {
            FightContext ctx;
            ctx.roll01 = [this]() { return roll01(); };
            ctx.stage = round_stage::fight;
            ctx.anims_me = {anim};
            if (!eval_conditions(c, ctx)) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        // JS `bqb` (L642): `b!=null&&b.priority>d.priority||(b=d)` — the
        // candidate with the HIGHEST priority wins; on equal priority the
        // LAST one wins (the `>` is strict, so equal -> `b=d`).
        if (best == nullptr || s.priority >= best->priority) best = &s;
    }
    return best;
}

int AiController::nwa(const std::vector<AiAnimSlot>& slots, const std::string& anim) {
    const AiAnimSlot* best = bqb(slots, anim);
    if (best == nullptr || moves_ == nullptr) return static_cast<int>(wb_.size());
    for (const std::string& n : best->names) {
        // JS `ra.nAa(a,b)` (L603): resolve the slot's animation names to
        // move objects; push each move's name (dedup against `wb_`:
        // `J.remove(a, wb[d].animation.name)`).
        const std::vector<const MoveDef*> group = resolve_candidate(n, *moves_);
        for (const MoveDef* m : group) {
            bool dup = false;
            for (const AiCandidate& c : wb_) {
                if (c.animation == m->name) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            wb_.push_back({m->name, 0});
            // JS `Nwa` (L603): `this.iN.zZ.push(c.name)` — the name feeds
            // the `cc.Gb` conditional term when weighed by `iCa`.
            feat_.zz.push_back(m->name);
        }
    }
    // JS `Nwa` (L603) returns `this.wb.length` (the RUNNING TOTAL, not the
    // delta). `de.ia` L594 only tests the sum's sign (`b>0`/`b<=0`), so the
    // value is behaviourally neutral, but return it verbatim.
    return static_cast<int>(wb_.size());
}

// JS `csb`/`Zqb` (L618-619): size `cO`/`hN` to the tactic's `$E`/`nD` slot
// count and roll each slot's `t4` with one `Da.jf()`.
void AiController::roll_slots() {
    const std::size_t nq = tactic_ != nullptr ? tactic_->quick_attacks.size() : 0;
    quick_rolls_.resize(nq);
    for (std::size_t i = 0; i < nq; ++i) quick_rolls_[i].t4 = next01();
    // [probe] record the rolls + the chance they were compared against, so the
    // port's slot cadence can be compared with the oracle's i_slots.
    for (std::size_t i = 0; i < nq && i < 2; ++i) {
        dbg_.slots_t4[i] = quick_rolls_[i].t4;
        dbg_.slots_gz[i] = static_cast<double>(
            weight_curve_eval(tactic_->quick_attacks[i].chance, feat_));
        dbg_.slots_na[i] =
            quick_rolls_[i].t4 < dbg_.slots_gz[i] ? 1 : 0;
    }
    const std::size_t ne = tactic_ != nullptr ? tactic_->evades.size() : 0;
    evade_rolls_.resize(ne);
    for (std::size_t i = 0; i < ne; ++i) evade_rolls_[i].t4 = next01();
}

// JS `bsb`/`Yqb` (L619): `gZ = slot.chance.Gb(feat)`; `N_ = t4 < gZ`.
// Draw-free (the curves are evaluated, not rolled).
void AiController::eval_slots() {
    if (tactic_ == nullptr) return;
    for (int q = 0; q < 2; ++q) {
        dbg_.slots_t4[q] = 0.0;
        dbg_.slots_gz[q] = 0.0;
        dbg_.slots_na[q] = 0;
    }
    for (std::size_t i = 0;
         i < quick_rolls_.size() && i < tactic_->quick_attacks.size(); ++i) {
        quick_rolls_[i].gz =
            weight_curve_eval(tactic_->quick_attacks[i].chance, feat_);
        quick_rolls_[i].na =
            quick_rolls_[i].t4 < static_cast<double>(quick_rolls_[i].gz);
        if (i < 2) {
            dbg_.slots_t4[i] = quick_rolls_[i].t4;
            dbg_.slots_gz[i] = static_cast<double>(quick_rolls_[i].gz);
            dbg_.slots_na[i] = quick_rolls_[i].na ? 1 : 0;
        }
    }
    for (std::size_t i = 0;
         i < evade_rolls_.size() && i < tactic_->evades.size(); ++i) {
        evade_rolls_[i].gz = weight_curve_eval(tactic_->evades[i].chance, feat_);
        evade_rolls_[i].na =
            evade_rolls_[i].t4 < static_cast<double>(evade_rolls_[i].gz);
    }
}

// JS `Pqb` L606: every FIRING base QuickAttack slot (`cO[f].N_`) contributes
// each of its moves that passes `V1`, with wait `$I()` (the Strict end).
// `fk=6` when anything was pushed.
int AiController::quick_slots(const AiFightState& st) {
    if (tactic_ == nullptr || moves_ == nullptr) return 0;
    int added = 0;
    for (std::size_t f = 0;
         f < tactic_->quick_attacks.size() && f < quick_rolls_.size(); ++f) {
        if (!quick_rolls_[f].na) continue;
        if (!tactic_->quick_attacks[f].names.empty())
            dbg_.qinfo += tactic_->quick_attacks[f].names[0];
        // [probe, authorised] `SF2_TRACE_AI_QUICK=1`: why a FIRING quick slot
        // contributes nothing (`added=0`) - the JS `Pqb` L606 branch would
        // then fall through to the base slots and the AI only steps.
        const bool trace_quick =
            std::getenv("SF2_TRACE_AI_QUICK") != nullptr;
        for (const std::string& n : tactic_->quick_attacks[f].names) {
            const std::vector<const MoveDef*> group = resolve_candidate(n, *moves_);
            int passed = 0;
            std::string first_why;
            std::string passed_names;
            for (const MoveDef* m : group) {
                std::string why;
                if (!v1(*m, st, &why)) {
                    if (first_why.empty()) first_why = m->name + ":" + why;
                    continue;
                }
                ++passed;
                passed_names += " ";
                passed_names += m->name;
                // JS `Pqb` L606: the quick-slot wait is `h.$I()` =
                // `vBa(P.s$a())` = `0<b ? i0(b+1) : 0` (SUB-FRAME, L697), not
                // the raw `strict_end`. A raw wait (often 1) made the AI
                // re-decide every frame and restart the move (no translation).
                // The candidate carries the SLOT's animation string (the
                // `<QuickAttackChance Animation="ShortAttack">` key), not the
                // resolved move name: `Md.iCa` weighs the candidate by that
                // string, so `ShortAttack` scores `<Animation Name=
                // "ShortAttack" Base="1000">` instead of the `Weapon` key
                // (Base=200) it would inherit as a punch's template - which
                // lost every roulette to `ForwardStep` (800) and left the
                // Standard-tactic AI stepping instead of attacking.
                wb_.push_back({n, vba_full(*m, strict_end(*m))});
                ++added;
            }
            if (trace_quick) {
                std::fprintf(stdout,
                             "[quick] slot=%zu key='%s' resolved=%zu passed=%d "
                             "first_reject='%s' pass=[%s]\n",
                             f, n.c_str(), group.size(), passed,
                             first_why.c_str(), passed_names.c_str());
                std::fflush(stdout);
            }
        }
        dbg_.qinfo += ";";
    }
    if (added > 0) fk_ = 6;
    return added;
}

// JS `Pqb` L606: `IB` is set when a FIRING Evade (`nD`) slot resolves to a
// move the OPPONENT's `de` would accept (`a.nf.V1(h)`). JS `Ykb` (L20647)
// calls `this.nf.ia(this.jb)` — the AI is fed the OPPONENT fighter as `a`, so
// `a.nf` (L25504 `e = a.nf`) is the OPPONENT's controller and `de.V1`'s
// `this.model` (L25301-25312, `this.model.me` / `this.model.Fc`) is the
// OPPONENT. The port has no opponent controller, but the opponent's STATE is
// right here: mirror the two halves of `st` and hand `V1` the OPPONENT view
// (`my_moves` <- `enemy_moves`, `my_x` <- `enemy_x`, the swapped `Pivot`
// resolvers, …) so the candidate's align node and direction are measured on
// the OPPONENT's skeleton, exactly as the JS does. (With the own-view the
// `ThrowForward` `<Distance Max="100">` was ~14 units outside the window and
// `IB` stayed false; the oracle measures `IB=true` -> `fk=9` -> BackHandflip.)
void AiController::evade_ib(const AiFightState& st) {
    IB_ = false;
    if (tactic_ == nullptr || moves_ == nullptr) return;
    // [probe] SF2_TRACE_AI_QUICK=1: the `<Evades>` slot state of this pass
    // (`t4` roll, chance, fired) - `IB` needs a FIRING slot first.
    static const bool trace_ib = std::getenv("SF2_TRACE_AI_QUICK") != nullptr;
    if (trace_ib) {
        for (std::size_t d = 0; d < evade_rolls_.size(); ++d) {
            std::fprintf(stdout, "[ev] f=%d slot=%zu t4=%.6f gz=%.4f na=%d\n",
                         st.fight_frame, d, evade_rolls_[d].t4,
                         evade_rolls_[d].gz, evade_rolls_[d].na ? 1 : 0);
        }
        std::fflush(stdout);
    }
    AiFightState foe = st;  // the OPPONENT's own view = `a` in `Pqb(a)`
    std::swap(foe.my_moves, foe.enemy_moves);
    std::swap(foe.current_move, foe.enemy_move);
    std::swap(foe.my_anim, foe.enemy_anim);
    std::swap(foe.my_x, foe.enemy_x);
    std::swap(foe.my_hp, foe.enemy_hp);
    std::swap(foe.my_max_hp, foe.enemy_max_hp);
    std::swap(foe.my_intervals, foe.enemy_intervals);
    std::swap(foe.my_items, foe.enemy_items);
    std::swap(foe.my_perks, foe.enemy_perks);
    std::swap(foe.my_mods, foe.enemy_mods);
    std::swap(foe.my_wall_min, foe.enemy_wall_min);
    std::swap(foe.my_wall_max, foe.enemy_wall_max);
    std::swap(foe.ref_pos, foe.foe_ref_pos);
    std::swap(foe.candidate_pivot_pos, foe.foe_candidate_pivot_pos);
    for (std::size_t d = 0;
         d < tactic_->evades.size() && d < evade_rolls_.size(); ++d) {
        if (!evade_rolls_[d].na) continue;
        bool hit = false;
        for (const std::string& n : tactic_->evades[d].names) {
            const std::vector<const MoveDef*> group = resolve_candidate(n, *moves_);
            for (const MoveDef* m : group) {
                std::string why;
                if (v1(*m, foe, &why)) {
                    hit = true;
                    if (std::getenv("SF2_TRACE_AI_QUICK") != nullptr) {
                        std::fprintf(stdout,
                                     "[ib] slot=%zu key='%s' move='%s' "
                                     "ACCEPT (my_x=%.0f enemy_x=%.0f)\n",
                                     d, n.c_str(), m->name.c_str(),
                                     static_cast<double>(foe.my_x),
                                     static_cast<double>(foe.enemy_x));
                        std::fflush(stdout);
                    }
                    break;
                }
            }
            if (hit) break;
        }
        if (hit) {
            IB_ = true;
            // [probe, authorised] `SF2_TRACE_AI_QUICK=1`: the evade slot that
            // resolved against the OPPONENT's view (`IB`), so a wasted
            // BackHandflip against an idle opponent is traceable.
            if (std::getenv("SF2_TRACE_AI_QUICK") != nullptr) {
                std::fprintf(stdout, "[ib] slot=%zu names=%zu -> IB=1\n", d,
                             tactic_->evades[d].names.size());
                std::fflush(stdout);
            }
            return;
        }
    }
}

// JS `h2a` (L608): `ld` = the candidate animations, `vs` = their waits.
void AiController::h2a() {
    ld_.clear();
    vs_.clear();
    ld_.reserve(wb_.size());
    vs_.reserve(wb_.size());
    for (const AiCandidate& c : wb_) {
        ld_.push_back(c);
        vs_.push_back(c.wait);
    }
    dbg_.ld = static_cast<int>(ld_.size());
    dbg_.vs0 = vs_.empty() ? 0 : vs_.front();
}

// JS `k_a` (L603): find the `<ConditionalDecisions>` `<PlayerAnimation>`
// entry whose name MY current move matches (`c.$k(d.first)`) and run `Nwa`
// on its `<Reactions>` slots. The shipped names are boss/bot animations
// (`MagicSawPlayer`, `GroundPunch`, `RangedPlayer`), so this is normally 0.
int AiController::ka(const AiFightState& st) {
    if (tactic_ == nullptr || st.current_move == nullptr) return 0;
    const MoveDef& mv = *st.current_move;
    for (const auto& kv : tactic_->conditional_player) {
        if (move_has_name(mv, kv.first)) {
            return nwa(kv.second, st.my_anim);
        }
    }
    return 0;
}
// [experiment] The `Gc` pump frame. JS `Gc.ia` (L28299) runs `Bx` (rebuild
// the per-fighter ctx) + `Gnb` (L28336: `Rwa` -> `EZa` bucket -> `dxa` -> `DK`)
// every frame, independently of `de.ia`. `DK` with `eb=false` takes the
// max-`priority` (`Aua`) non-`Rha` candidate and one uniform `uf.sja` draw; the
// result is STORED (`Ml.animation`) and `Bnb` (L20947) starts it only when the
// fighter is not playing.
std::string AiController::gc_frame(const AiFightState& st) {
    if (moves_ == nullptr || st.my_moves == nullptr) return "";
    std::vector<const MoveDef*> best;
    // JS `Gc.Rwa`/`EZa` (L28510/L28531): the candidate buckets are built from
    // the events in `Tu`, i.e. `Su.dea(type)` = the animations whose
    // `<Events>` carry that TYPE. The port used the static `<KeyPressed/>`
    // move set, which re-picked a move on EVERY frame - the JS picks only when
    // an event fires, and letting those starts through measured as a
    // 1776-2739 divergence explosion.
    // The bus stores the PERK-trigger numbering (`Ac.parse`: EveryFrame=2),
    // not the `Gc`/`tb.D6a` one (EveryFrame=14). Convert before bucketing.
    auto gc_type = [](int slot) -> int {
        switch (slot) {
            case 1: return 1;   // RoundStageStart
            case 2: return 14;  // EveryFrame
            case 5: return 6;   // HitPreCrit -> Hit
            case 6: return 6;   // HitPostCrit -> Hit
            case 7: return 7;   // PostHit -> Strike
            case 9: return 9;
            case 10: return 10;
            case 11: return 11;
            case 12: return 12;
            case 13: return 13;
            case 14: return 16;  // ModExpires
            default: return 0;   // Style/Combo/Area* -> not a Gc event
        }
    };
    std::set<int> fired;
    for (const int slot : st.my_event_types) {
        const int t = gc_type(slot);
        if (t != 0) fired.insert(t);
    }
    if (fired.empty()) return "";
    // [probe] SF2_TRACE_GC=1: the fired types + what the buckets keep.
    static const bool trace_gc = std::getenv("SF2_TRACE_GC") != nullptr;
    if (trace_gc) {
        int kp = 0, pass = 0;
        std::string names, types;
        for (const int t : fired) {
            types += std::to_string(t);
            types += ' ';
        }
        for (const MoveDef* m : *st.my_moves) {
            if (m == nullptr) continue;
            bool ev_match = false;
            for (const std::string& ev : m->events) {
                if (fired.count(js_event_type(ev)) != 0) {
                    ev_match = true;
                    break;
                }
            }
            if (!ev_match) continue;
            ++kp;
            if (m->no_animation) continue;
            if (!v1(*m, st, nullptr, /*mirror_filter=*/false)) continue;
            ++pass;
            names += m->name;
            names += ' ';
        }
        std::fprintf(stdout, "[gc] f=%d playing=%d types=[%s] matched=%d kept=%d [%s]\n",
                     st.fight_frame, st.playing ? 1 : 0, types.c_str(), kp, pass,
                     names.c_str());
        std::fflush(stdout);
    }
    for (const MoveDef* m : *st.my_moves) {
        if (m == nullptr) continue;
        bool ev_match = false;
        for (const std::string& ev : m->events) {
            if (fired.count(js_event_type(ev)) != 0) {
                ev_match = true;
                break;
            }
        }
        if (!ev_match) continue;
        if (m->no_animation) continue;  // `Rha` -> the unused `g` group
        // The StartStance family is gated by `<RoundStage Value="StartStance">`
        // in the move's MAIN conditions, which `de.V1` does not evaluate (it
        // reads only the tactics list). The JS's `EZa` calls `f.Yz(b,null,g)`
        // on the whole tree, so a start-stance animation is never a pump
        // candidate during the fight; without this the pump started
        // `FistsStartStanceIdle-Right` out of a hit reaction (the single
        // challenge divergence).
        // Only real MOVES/ATTACKS are pump candidates: the getup / loss / win
        // / stance animations carry no `Type` and are reached through the
        // HIT-reaction path (`Gc.Ih(6/7)` -> `DK`), never through the
        // frame-event pump. Without this the pump started
        // `StandupAfterThrowFall` (Priority 400, conditions `<RoundResult
        // Defeat Not=1>`) out of a plain hit reaction.
        if (m->type != "ATTACK" && m->type != "MOVE") continue;
        if (st.full_conditions && !st.full_conditions(*m)) continue;
        if (!v1(*m, st, nullptr, /*mirror_filter=*/false)) continue;
        if (best.empty() || m->priority > best.front()->priority) {
            best.clear();
            best.push_back(m);
        } else if (m->priority == best.front()->priority) {
            best.push_back(m);
        }
    }
    if (!best.empty()) {
        float r = math_roll();
        if (r < 0.0f) r = 0.0f;
        if (r >= 1.0f) r = 0.9999999f;
        std::size_t idx = static_cast<std::size_t>(r * static_cast<float>(best.size()));
        if (idx >= best.size()) idx = best.size() - 1;
        gc_stored_ = best[idx]->name;
    }
    // `Bnb`: start the stored animation only when nothing is playing.
    if (!gc_stored_.empty() && !st.playing) {
        std::string out = gc_stored_;
        gc_stored_.clear();
        return out;
    }
    return "";
}

// [experiment] JS `Ykb` -> `Okb` -> `Gc` (L20643 / L20887 / L28330): the move
// `de.ia` picked has a `<Keys>` combo; `Okb` injects that combo and the Gc pump
// rebuilds the candidates from `Su.dea(2)` (every OWN move with a
// `<KeyPressed/>` event), keeps those bound to the SAME key signature, then
// `Pkb`/`DK(...,true)` takes the max-`priority` group and one uniform
// `uf.sja` (`Math.random`) pick. Without this the port only ever "moves"
// (Steps) and never attacks - the `enemy-move-audit` failure.
std::string AiController::gc_attack(const AiFightState& st,
                                    const std::string& picked) {
    if (moves_ == nullptr || st.my_moves == nullptr || picked.empty()) return "";
    const auto it = moves_->find(picked);
    if (it == moves_->end()) return "";
    const std::string sig = keys_signature(it->second);
    if (std::getenv("SF2_TRACE_GCA") != nullptr) {
        std::fprintf(stdout, "[gca] picked=%s sig=[%s] prio=%d\n",
                     picked.c_str(), sig.c_str(), it->second.priority);
        std::fflush(stdout);
    }
    if (sig.empty()) return "";
    std::vector<const MoveDef*> group;
    for (const MoveDef* m : *st.my_moves) {
        if (m == nullptr || !m->has_event("KeyPressed")) continue;
        if (keys_signature(*m) != sig) continue;
        if (!v1(*m, st, nullptr, /*mirror_filter=*/true)) continue;
        group.push_back(m);
    }
    if (group.empty()) return "";
    // `Aua` (L28384): keep the max-`priority` group (strict `>` clears).
    std::vector<const MoveDef*> best;
    for (const MoveDef* m : group) {
        if (m->no_animation) continue;  // `Rha` -> the `g` group (unused here)
        if (best.empty() || m->priority > best.front()->priority) {
            best.clear();
            best.push_back(m);
        } else if (m->priority == best.front()->priority) {
            best.push_back(m);
        }
    }
    if (best.empty()) return "";
    // `uf.sja` (L28417) consumes one `Math.random` even for a single element.
    float r = math_roll();
    if (r < 0.0f) r = 0.0f;
    if (r >= 1.0f) r = 0.9999999f;
    std::size_t idx = static_cast<std::size_t>(r * static_cast<float>(best.size()));
    if (idx >= best.size()) idx = best.size() - 1;
    return best[idx]->name;
}

// JS `de.gfa` (L597) = `Gc.gfa(a)+1` where `G` = `Md.I0(z$)` (L640-643):
// `I0(a,b) = Da.pg.dT(a,b)` truncated. `+1` is the de-level addition.
int AiController::gfa_draw() const {
    if (tactic_ == nullptr) return 1;
    const double lo = weight_curve_eval(tactic_->response_delay_min, feat_);
    const double hi = weight_curve_eval(tactic_->response_delay_max, feat_);
    return static_cast<int>(next_range(lo, hi)) + 1;
}

// JS `de.Aea` (L597) = `Gc.Aea(a)`, `Md.I0(v8)` (L640-643), truncated,
// NO +1. Consumed per `XAa` call (L611: `b=Aea(Eqa)`, then `b=Fl+b`).
int AiController::aea_draw() const {
    if (tactic_ == nullptr) return 0;
    const double lo = weight_curve_eval(tactic_->enemy_response_delay_min, feat_);
    const double hi = weight_curve_eval(tactic_->enemy_response_delay_max, feat_);
    return static_cast<int>(next_range(lo, hi));
}

// JS `de.j0` (L592: `this.Fl=b.Pe?b.kJ()+b.Q_+this.j0(this.Uu):-1`, then
// `Gc.j0` L640-641 and `Md.I0` L643/L2352): `Md.I0(a,b)=Da.pg.dT(a,b)
// = a+jf()*(b-a)`, truncated (`|0`). The curve is evaluated on the `Uu`
// snapshot built by `mq`/`mQ` above (NOT the `QJa` `zk` cache `lN_`, which is
// written but never read in the JS). One `jf()` = TWO sequential `B0()` words.
int AiController::j0_draw() const {
    if (tactic_ == nullptr) return 0;
    const double lo = weight_curve_eval(tactic_->frame_error_min, feat_);
    const double hi = weight_curve_eval(tactic_->frame_error_max, feat_);
    return static_cast<int>(next_range(lo, hi));
}

// JS `QJa` (L594-595): rebuild the enemy-relative context (the port's
// `feat_` was already built by `mq`), draw the five `Da.jf()` rolls,
// `Mu`/`lN` (yea/j0, L640-641) and cache `$x` (gfa). The JS additionally
// snapshots strike-memory counters (`Cn.d0`) — the port has no
// strike-memory accumulators (`Cn.d0`, via `mq`) now carry the real values.
void AiController::qja(const AiFightState& st) {
    (void)st;
    // JS `QJa` (L594-595): a bare `Da.jf();` then five `Da.jf()` — all from
    // the shared `Da.pg` (the port: the fight's `draw01()` stream when
    // installed, else the owned DaPrng). Load-bearing for stream position:
    // 6 jf() calls = 12 B0 draws, then Mu (2), lN (2). `$x` is NOT drawn
    // here — JS `QJa` never touches `$x`; `jwb` draws it separately.
    next01();  // discarded
    tua_ = next01();
    dua_ = next01();
    bpa_ = next01();
    rqa_ = next01();
    oqa_ = next01();
    // yea (L640-641, untruncated) / j0 (truncated).
    if (tactic_ != nullptr) {
        Mu_ = next_range(weight_curve_eval(tactic_->distance_error_min, feat_),
                         weight_curve_eval(tactic_->distance_error_max, feat_));
        lN_ = static_cast<int>(next_range(
            weight_curve_eval(tactic_->frame_error_min, feat_),
            weight_curve_eval(tactic_->frame_error_max, feat_)));
    }
}
// JS `de.mcb` (L596-597): `let b=0,c=P.S9a();for(;b<c.length;)if(a.$k(c[b++]))
// return!0;return!1` — `jc.$k(n)` is `n == this.name || this.d2(n)`, where
// `d2` scans the move's inherited tags (`xl`). Read by `jwb` (L596-597) to
// gate the `$x` ResponseDelay cache.
bool AiController::mcb(const MoveDef& m) const {
    for (const std::string& n : ignored_enemy_anims_) {
        if (move_has_name(m, n)) return true;
    }
    return false;
}

// JS `ia` L593: `for(d=P.y$a();c<d.length;)if(d[c++].children.includes(b))`
// — the move is one of the resolved `<RandomizingEnemyAnimation>` group's
// children (the port resolves the group by name at decision time).
bool AiController::in_randomizing_group(const MoveDef& m) const {
    if (moves_ == nullptr) return false;
    for (const std::string& n : randomizing_enemy_anims_) {
        if (n.empty()) continue;
        for (const MoveDef* c : resolve_candidate(n, *moves_)) {
            if (c == &m) return true;
        }
    }
    return false;
}

// JS `wd.mwb` (L21867) -> `de.jwb` (L25007): the OPPONENT's animation-start
// MODEL EVENT. `wd.x3` (L20990) dispatches it from the model's
// EStartAnimationEvent, NOT from `de.ia`/`wd.Anb`, so it fires in EVERY phase:
// the QJa cache it rebuilds (6 `Da.jf()` at L24970-24975 + `Mu`/`lN`) and the
// `$x` ResponseDelay draw are 9 shared `Da.pg` draws per enemy move start. The
// port detects the event by the opponent's `Te.Skb` clip-start serial
// (`st.enemy_clip_starts`, `Fighter::move_start_count()`), because `Skb` runs
// on EVERY clip start INCLUDING a loop restart of the same move; the move
// pointer / animation-name compare alone misses the loop restart.
void AiController::jwb_event(const AiFightState& st) {
    // JS `Te.Skb` (L22984) -> `Te.x3` (L23097) fires `EStartAnimationEvent` on
    // EVERY clip start, INCLUDING a loop restart of the same move (`Te.lS`
    // L23100 ends the clip, the move re-selects, `Skb` seats it again). The
    // `wd.x3` handler (L20990) then calls `a.mwb(this.da.Ua)` on the OPPONENT,
    // and `Y.mwb` (L21867) runs `Y.nf.jwb(X)` = `de.jwb` -> `QJa` (6 `Da.jf()`
    // L24970-24975) + the `gfa` ResponseDelay draw. The old move-pointer /
    // animation-name compare MISSED the loop restart, so the boss-lynx stream
    // was 9 draws short before the second roulette (port draw 29 vs the JS
    // draw 38, `roll=0.983574838` vs `0.660125684`).
    const bool clip_started = st.enemy_clip_starts != last_enemy_clip_;
    if (!clip_started && st.enemy_move == last_enemy_move_ &&
        st.enemy_anim == last_enemy_anim_) {
        return;
    }
    last_enemy_clip_ = st.enemy_clip_starts;
    last_enemy_anim_ = st.enemy_anim;
    last_enemy_move_ = st.enemy_move;
    if (st.enemy_playing && st.enemy_move != nullptr) {
        qja(st);
        qja_done_ = true;
        if (!mcb(*st.enemy_move)) {
            x_ = gfa_draw();
        }
    }
}

// JS `wd.mwb` (L21867) -> `de.iwb` (L24994), dispatched by MY model's
// EStartAnimationEvent (`wd.x3` L20990 -> `a.mwb(this.da.Ua)`): MY move start
// makes MY AI's `cs` my own current move and, when it carries an
// `<UnexpectedMoves>` tag (Physical/Hit/ThrowFall), resets the decision wait
// `eh=1` so the AI re-decides on the very next frame. The move change draws
// nothing, so the `Da.pg` stream position is unaffected. Not phase-gated.
void AiController::iwb_event(const AiFightState& st) {
    if (st.current_move == last_my_move_) return;
    last_my_move_ = st.current_move;
    if (!st.playing || st.current_move == nullptr) return;
    // [experiment] Okb (L20887): a started move raises the KeyPressed event
    // that arms the Gc pump for the next pass.
    gc_pump_armed_ = true;
    for (const std::string& n : unexpected_moves_) {
        if (move_has_name(*st.current_move, n)) {
            eh_ = 1;
            break;
        }
    }
}

// JS `Pqb` (L604-608): the core decision. Returns the candidate count.
int AiController::pqb(const AiFightState& st) {
    wb_.clear();
    // JS `Pqb` (L604): `this.iN.zZ.length=0` — the appended-name list the
    // `cc.Gb` conditional term (L647) tests is reset per decision pass.
    feat_.zz.clear();
    pH_ = F8_ = false;

    // Probe snapshot (`--ai-probe` / the fight `[ai]` log): reset the
    // PQB-OWNED fields only. The old whole-struct reset (`dbg_ = AiDebug{}`)
    // also wiped the operands `update()` recorded BEFORE this pass - `hcb`
    // (the no-decision gate), `cz`/`bda`/`tba` (the `dqb` roll), `eh` and
    // `pf` - so every `[ai]`/`[dbg]` line reported `hcb=0 cz=0 eh=0` even on
    // frames that DID decide. Those stale zeros sent two earlier audits after
    // the wrong field (the "the AI never counters/dodges" reading).
    dbg_.branch = "none";
    dbg_.gate = dbg_.ycb = dbg_.lbb = dbg_.pcb = false;
    dbg_.rua = dbg_.caa = dbg_.nG = false;
    dbg_.fk = -1;
    dbg_.wb = 0;
    dbg_.ld = 0;
    dbg_.vs0 = 0;
    dbg_.enemy_frame = st.enemy_kj;
    dbg_.x = x_;
    if (st.enemy_move != nullptr) {
        dbg_.enemy_uninterrupt_end = uninterrupt_end(*st.enemy_move);
        dbg_.enemy_attack_end = attack_end(*st.enemy_move);
    }

    // Facing lock (JS L604): `if(this.b6a(b)*b.hd()>0)`.
    //   `b6a(b)` = `a.Fe().ma.x - this.Ji.Fe().ma.x >= 0 ? 1 : -1` (L603)
    //     where a = the OPPONENT (`b = a.da`), `this.Ji` = me → the sign of
    //     (opponent.x − my.x), the direction from me TOWARD the opponent.
    //   `b.hd()` = the OPPONENT's `Te.hd()` = `return this.FX` (L547) — the
    //     CLIP MIRROR `sign(me.x − opp.x)` (set by `Te.Skb` L551 `rub(Ae.Wl)`,
    //     `Vi.SBa` L704) — NOT the `b6a` facing. So the product is
    //     `sign(opp−me) * sign(me−opp)` = −1: the lock does NOT fire for a
    //     normally-facing opponent. The port previously fed the opponent's
    //     `facing_` (== `b6a`), making the product `+1` every frame, so the
    //     AI took this branch unconditionally and returned 0 (stood still).
    const int dir_to_opp = b6a(st);
    if (dir_to_opp * st.enemy_clip_mirror > 0) {
        dbg_.branch = "facing-lock";
        pH_ = F8_ = true;
        oC_ = 3;
        return 0;
    }

    // Dodge missiles / magic (JS L604: `fCa(a,0)&&pqa`, `mqa&&fCa(a,1)`
    // where `pqa=rqa<pua`, `mqa=oqa<oua` were drawn in `ia` L593-594:
    // CACHED QJa rolls vs freshly evaluated curves).
    bool dodge_fired = false;
    if (tactic_ != nullptr) {
        const float dms = weight_curve_eval(tactic_->dodge_missiles_chance, feat_);
        const float dmg = weight_curve_eval(tactic_->dodge_magic_chance, feat_);
        const bool pqa = rqa_ < dms;
        const bool mqa = oqa_ < dmg;
        if (fca(st, 0) && pqa) {
            vaa(st, 0);
            fk_ = 2;
            dodge_fired = true;
        }
        if (mqa && fca(st, 1)) {
            vaa(st, 1);
            fk_ = 2;
            dodge_fired = true;
        }
    }
    if (dodge_fired) {
        dbg_.branch = "dodge-missile";
        return static_cast<int>(wb_.size());
    }

    // Per-pass evaluated chances vs the CACHED QJa rolls (JS `ia` L593-594):
    //   qPa=k9a (UseSafeAttack), vO=a9a (TableAttack), Awa=A5a (Cautious);
    //   rua=tua<qPa, caa=dua<vO, nG=Bpa<Awa.
    float usa = 0.0f, ta = 0.0f, cm = 0.0f;
    bool rua = false, caa = false, nG = false;
    if (tactic_ != nullptr) {
        usa = weight_curve_eval(tactic_->use_safe_attack_chance, feat_);
        ta = weight_curve_eval(tactic_->table_attack_chance, feat_);
        cm = weight_curve_eval(tactic_->cautious_movements_chance, feat_);
        rua = static_cast<double>(tua_) < usa;
        caa = static_cast<double>(dua_) < ta;
        nG = static_cast<double>(bpa_) < cm;
    }
    dbg_.rua = rua; dbg_.caa = caa; dbg_.nG = nG;
    dbg_.cm = cm;
    dbg_.bpa = bpa_;

    // The response-delay + uninterruptible gate (JS L604-605):
    //   $x < b.kJ() && !Ycb(b)   (the OPPONENT is inside its Uninterrupt
    //   startup window) && `this.ds.pcb(this.Fl)` — `ds` is the OPPONENT's
    //   current move (`jwb` L596-597 `this.ds=c` with c = the opponent's
    //   `Ua`), `Fl` the opponent's offset frame.
    // `$x` is the CACHED ResponseDelay from `jwb` (NOT re-rolled per pass).
    // `ycb`/`lbb` test the OPPONENT's current move.
    const int enemy_frame = st.enemy_kj;  // JS `b.kJ()` (sub-step `lq`)
    dbg_.gate = enemy_frame > x_ && !ycb(st);
    dbg_.ycb = ycb(st);
    dbg_.lbb = lbb(st);
    // JS `pcb(this.Fl)`: `M6a(Fl)` maps the SUB-FRAME `Fl_` back to the
    // LOGICAL frame; see `pcb` above.
    if (st.enemy_move != nullptr)
        dbg_.pcb = pcb(*st.enemy_move, Fl_);
    if (dbg_.gate) {
        dbg_.branch = "gate/reactive";
        if (st.enemy_move == nullptr ||
            pcb(*st.enemy_move, Fl_)) {
            dbg_.pcb = st.enemy_move != nullptr;
            if (lbb(st)) {
                dbg_.branch = "reactive/lbb";
                dbg_.rua = rua;
                dbg_.caa = caa;
                dbg_.nG = nG;
                // Safe attack / attack table (JS L605).
                if (tactic_ != nullptr) {
                    if (rua) {
                        dbg_.branch = "reactive/safe-attack";
                        const int b = yaa(st);
                        if (b > 0) fk_ = 1;
                        if (Ao_ || b > 0) return static_cast<int>(wb_.size());
                    }
                    if (caa) {
                        dbg_.branch = "reactive/table-attack";
                        const int b = xaa(st);
                        if (b > 0) fk_ = 0;
                        if (Ao_ || b > 0) return static_cast<int>(wb_.size());
                    }
                    // Cautious movements (JS L605): `b=P.nCa()` — the
                    // `<CautiousMovements>` groups; wait = the enemy's
                    // Uninterrupt end (`d.zD(!0)-Fl+1`, 0 when idle) clamped
                    // by each move's Extended then Strict end.
                    if (nG) {
                        dbg_.branch = "reactive/cautious";
                        wb_.clear();
                        // JS `Pqb` (L605): `b=P.nCa();var d=a.da.Ua;c=0;
                        // d!=null&&a.da.Pe&&(c=d.zD(!0)-this.Fl+1)` — the wait
                        // requires the ENEMY PLAYING (`a.da.Pe`) as well as a
                        // non-null move, and uses the `zD(!0)` sub-frame value
                        // (`i0(b+1)-1`).
                        int c = 0;
                        if (st.enemy_move != nullptr && st.enemy_playing) {
                            c = uninterrupt_end_full(*st.enemy_move) - Fl_ + 1;
                        }
                        if (moves_ != nullptr) {
                            for (const std::string& grp : tactic_->cautious_movements) {
                                for (const MoveDef* m : resolve_candidate(grp, *moves_)) {
                                    int g = c;
                                    const int tea = vba_full(*m, extended_end(*m));
                                    if (g > tea) g = tea;
                                    const int se = vba_full(*m, strict_end(*m));
                                    if (se > g) g = se;
                                    wb_.push_back({m->name, g});
                                }
                            }
                        }
                        if (!wb_.empty()) fk_ = 5;
                        return static_cast<int>(wb_.size());
                    }
                }
                dbg_.branch = "reactive/watch(lbb)";
                oC_ = 2;
                pH_ = true;
                return 0;
            }
            // The enemy is in its attack window -> distance-based response
            // (JS L605 switch on aqa).
            dbg_.branch = "reactive/aqa-switch";
            switch (aqa_) {
                case 2: {
                    int b = yaa(st);
                    if (b > 0) {
                        fk_ = 1;
                        return b;
                    }
                    b = gea(st, 0);
                    if (b > 0) fk_ = 2;
                    return b;
                }
                case 3: {
                    const int b = gea(st, 0);
                    if (b > 0) fk_ = 2;
                    return b;
                }
                case 4:
                    fk_ = 10;  // block
                    return 0;
                default:
                    oC_ = 1;
                    pH_ = true;
                    return 0;
            }
        }
        return 0;
    }

    // QuickAttack slots (JS L606, the `else` path): every FIRING base `$E`
    // slot (`cO[f].N_`, rolled by `csb`/`bsb`) contributes its V1-valid
    // moves with wait `$I()`; then the Evade (`nD`) slots decide `IB`.
    dbg_.branch = "base-slots";
    eval_slots();
    const int qadd = quick_slots(st);
    evade_ib(st);
    dbg_.qfired = 0;
    dbg_.qpushed = qadd;
    for (std::size_t i = 0;
         i < quick_rolls_.size() && i < tactic_->quick_attacks.size(); ++i) {
        if (quick_rolls_[i].na) ++dbg_.qfired;
    }

    // The surprise / evade-throw branch (JS L607-608):
    //   XW = 1 - 1/expectedWait < roll   (surprise)
    //   IB = the enemy would evade a throw
    // If either, the AI re-attacks from the attack table or the
    // EvadeThrowDodges / cautious lists.
    // JS `F6a(cs, Dqa)` (L639): the ExpectedWait lookup key is tested with
    // `a.$k(e.first)` = `e.first == cs.name || cs.d2(e.first)`, and `d2`
    // scans `cs.xl` (name + the transitive `<Template>` chain). Matching the
    // BARE move name only made every `Step`/`ForwardStep` entry miss, so a
    // `StepForward` in flight fell through to the unnamed
    // `<Animation Base="5" HealthFactor="1" EnemyHealthFactor="-1" Limit="15"
    // AntiLimit="1"/>` default (evaluates to ~5) instead of the
    // `<Animation Name="Step" Base="1000" Limit="1000"/>` entry. With
    // `expected_wait=5` the surprise gate `1-1/b < roll` fired ~80% of
    // passes even mid-step, so the AI re-issued StepForward before the clip
    // finished (net ~0 dx) instead of letting it run. `anim_names` IS `xl`
    // (move_def.cpp L1094), so test name + `anim_names`.
    float expected_wait = 1.0f;
    if (tactic_ != nullptr && st.current_move != nullptr) {
        const MoveDef& cs = *st.current_move;
        for (const auto& kv : tactic_->expected_wait) {
            const bool key_match =
                kv.first.empty() || kv.first == cs.name ||
                std::find(cs.anim_names.begin(), cs.anim_names.end(),
                          kv.first) != cs.anim_names.end();
            if (key_match) {
                expected_wait = weight_curve_eval(kv.second, feat_);
                break;
            }
        }
    }
    if (expected_wait < 1.0f) expected_wait = 1.0f;
    // JS `Pqb` (L607): `1-1/b<Da.jf()&&(this.XW=!0)`. The `Da.jf()` draw is
    // ALWAYS consumed (it is the RHS of `<`), but a FAILED roll does NOT clear
    // an XW latched on an earlier pass — the assignment is inside the `&&`, so
    // XW is only ever SET here. It is cleared elsewhere: by a decision
    // (`ia` L594 `if(0<b){this.XW=!1,...}`) or by the watch (`mW`, L594
    // `else this.mW&&(this.XW=!1)`). The port previously ASSIGNED the boolean,
    // so a failed roll on a later pass cleared a sticky XW early and the AI
    // left the surprise branch sooner than the JS.
    if ((1.0f - 1.0f / expected_wait) < roll01()) XW_ = true;
    dbg_.expected_wait = expected_wait;
    dbg_.xw = XW_;

    if (XW_ || IB_) {
        int b = 0;
        // JS L607 reuses the CACHED `caa` here (not a fresh draw).
        if (tactic_ != nullptr) {
            if (caa) {
                b = xaa(st);
                if (b > 0) fk_ = 0;
                if (Ao_ && !IB_) return static_cast<int>(wb_.size());
                if (b > 0) return b;
            }
        }
        if (IB_) {
            // JS L607: `a=P.H9a()` — `wb` is CLEARED and refilled with
            // every <EvadeThrowDodges> group move, wait `$I()`; `fk=9`.
            wb_.clear();
            if (moves_ != nullptr) {
                for (const std::string& grp : tactic_->evade_throw_dodges) {
                    for (const MoveDef* m : resolve_candidate(grp, *moves_)) {
                        wb_.push_back({m->name, vba_full(*m, strict_end(*m))});
                    }
                }
            }
            b = static_cast<int>(wb_.size());
            if (b > 0) fk_ = 9;
        } else if (tactic_ != nullptr && nG) {
            // JS L607-608: `else if(this.nG){a=P.nCa();...}` — the cautious
            // branch is gated by `nG` (the CautiousMovementsChance roll). The
            // port omitted the gate, so it refilled `wb` with the `<Step>`
            // group whenever XW fired even with `nG=0`; the JS leaves `b=0`
            // and returns the watch state (`oC=3,pH=!0`).
            wb_.clear();
            if (moves_ != nullptr) {
                for (const std::string& grp : tactic_->cautious_movements) {
                    for (const MoveDef* m : resolve_candidate(grp, *moves_)) {
                        wb_.push_back({m->name, vba_full(*m, strict_end(*m))});
                    }
                }
            }
            b = static_cast<int>(wb_.size());
            if (b > 0) fk_ = 5;
        }
        if (b == 0) {
            oC_ = 3;
            pH_ = true;
            return 0;
        }
    }
    return static_cast<int>(wb_.size());
}

// JS `VAa` (L617): the dodge candidates — the enemy's body-part moves the
// AI dodges with. The native port pushes the dodge animations from the
// tactic's Evades.
void AiController::vaa(const AiFightState& st, int variant) {
    (void)st;
    (void)variant;
    wb_.clear();
    if (tactic_ != nullptr) {
        for (const AiAnimSlot& s : tactic_->evades) {
            for (const std::string& n : s.names) {
                wb_.push_back({n, 0});
            }
        }
    }
    if (wb_.empty()) {
        wb_.push_back({"BackHandflip", 0});
    }
}

// JS `de.ia` (L592-594): the per-frame decision.
std::string AiController::update(const AiFightState& st) {
    // [probe] `SF2_TRACE_AIDRAW=1`: how often the AI actually runs and how many
    // draws its per-frame stages would consume. The measured JS cadence is ~4.6
    // `Da.pg` draws per frame; the port logs ~0.66, so this pinpoints whether
    // `update()` is skipped, the tactic is null (0 slots), or the phase gate
    // rejects.
    if (std::getenv("SF2_TRACE_AIDRAW") != nullptr) {
        static long calls = 0;
        ++calls;
        if (calls % 100 == 1) {
            std::fprintf(stdout,
                         "[aidraw] call=%ld phase_fight=%d tactic=%s type=%d "
                         "qa=%zu ev=%zu playing=%d\n",
                         calls, st.phase_fight ? 1 : 0,
                         tactic_ != nullptr ? tactic_->name.c_str() : "null",
                         tactic_ != nullptr ? tactic_->type : -1,
                         tactic_ != nullptr ? tactic_->quick_attacks.size() : 0,
                         tactic_ != nullptr ? tactic_->evades.size() : 0,
                         st.playing ? 1 : 0);
            std::fflush(stdout);
        }
    }
    if (tactic_ == nullptr || moves_ == nullptr) return "";
    // An injected source overrides the owned DaPrng stream (ai_demo uses
    // mt19937); otherwise ALL draws come from `prng_` in JS call order.
    roll01_fn_ = st.roll01;
    math_random_fn_ = st.math_random;
    // No decision on a phase-1 (event-only) pass, so clear the previous
    // decision's debug snapshot — the fight's `[ai]` log gates on `d.gate`.
    dbg_ = AiDebug{};
    dbg_.pf = st.phase_fight;

    // JS `wd.Anb` (L499): `a.type==1 ? this.hJa() : a.type==2 && this.Ykb()`
    // — a `Type=="Random"` tactic (`Md.getType` L642 `"Random"->1`) drives the
    // `hJa` path (Vkb/`Pkb`), NOT `de.ia`. `Anb` itself is gated
    // `(parameters.Fj||P.fP)&&Je==2` (phase 2, Fight), but the model-EVENT
    // handlers below are not: `wd.x3` (L20990, the model's
    // EStartAnimationEvent) -> `wd.mwb` (L21867) -> `de.jwb` (L25007, QJa) /
    // `de.iwb` (L24994) fire in EVERY phase, and their shared-`Da.pg` draws
    // land during the round intro too. Running the whole `update()` behind the
    // phase gate left the round-intro enemy move starts draw-free, so the
    // boss-lynx roulette read the stream 9 draws early (port draw 10 vs the JS
    // draw 19). `mq` (JS `mQ` L620) is draw-free and feeds both the events'
    // curve evaluation and the decision below.
    mq(st);
    jwb_event(st);
    iwb_event(st);

    // [probe] SF2_TRACE_GCP=1: the Gc KeyPressed bucket of this frame,
    // printed in the oracle's random_pre shape (names + the max-priority
    // group size), so the two can be compared BEFORE any wiring.
    // Read-only: nothing is started.
    if (std::getenv("SF2_TRACE_GCP") != nullptr) {
        std::vector<const MoveDef*> bucket;
        if (st.my_moves != nullptr) {
            for (const MoveDef* m : *st.my_moves) {
                if (m == nullptr || !m->has_event("KeyPressed")) continue;
                if (m->no_animation) continue;
                if (!v1(*m, st, nullptr, /*mirror_filter=*/false)) continue;
                bucket.push_back(m);
            }
        }
        int top = -1;
        for (const MoveDef* m : bucket) top = std::max(top, m->priority);
        std::string names;
        int ntop = 0;
        for (const MoveDef* m : bucket) {
            names += m->name;
            names += ' ';
            if (m->priority == top) ++ntop;
        }
        std::fprintf(stdout,
                     "[gcp] f=%d playing=%d n=%zu top=%d ntop=%d names=[%s]\n",
                     st.fight_frame, st.playing ? 1 : 0,
                     bucket.size(), top, ntop, names.c_str());
        std::fflush(stdout);
    }
    // [experiment] SF2_GC_PUMP=1: the Gc pump pass. In the JS Gc.ia runs
    // every frame right after the fighter update and consumes the events
    // raised by move starts; a Tabular fighter's attacks come from here.
    if (tactic_->type == 2 && gc_pump_armed_ &&
        std::getenv("SF2_GC_PUMP") != nullptr) {
        gc_pump_armed_ = false;
        if (st.phase_fight) {
            const std::string gc = update_random(st);
            if (!gc.empty()) return gc;
        }
    }
    if (tactic_->type == 1) {
        // JS `wd.x3` (L20990) — the OPPONENT-arm. `x3` is the `EStartAnimationEvent`
        // handler on MY model (`Te.x3` L23097 <- `Te.Skb` L23058), and it arms
        // the OPPONENT's countdown: `a=this.jb; a.mwb(this.da.Ua); a.Mka(0)`
        // (L20994-20997). So MY `tG` arms when the OPPONENT starts an animation.
        // The old self-arm (`st.current_move` change) made the enemy re-decide
        // the frame after each of its OWN move starts — the reported ~95% double
        // attack. The arm condition is the OPPONENT's `Skb` serial
        // (`st.enemy_clip_starts`) plus the move/anim proxy: `Te.Skb` L22984 ->
        // `Te.x3` L23097 fires `EStartAnimationEvent` on EVERY clip start
        // INCLUDING a loop restart of the same move, so the serial is required.
        // (The oracle `hja` log re-arms `tG0=0` at the idle loop restarts: run 2
        // at state f=374 AND 387, run 3 at f=373/386.)
        if (st.enemy_clip_starts != last_random_opp_clip_ ||
            st.enemy_move != last_random_opp_move_ ||
            st.enemy_anim != last_random_opp_anim_) {
            tG_ = 0;
        }
        last_random_opp_clip_ = st.enemy_clip_starts;
        last_random_opp_move_ = st.enemy_move;
        last_random_opp_anim_ = st.enemy_anim;

        // JS `wd.fIa` (L21016) — the SELF-arm on `EStopIntervalEvent`: when MY
        // stopped interval name is in the `<Random><Intervals>` set
        // (`v.u4.ncb(a.name)`, tactic_settings.xml L366-369 =
        // {Uninterrupt, SelfUninterrupt}) `this.tG` is set to 0 (`n6a` L49715
        // returns 0). Detect the END of that interval by the falling edge on MY
        // active-interval set.
        const auto has_interrupt = [&]() {
            for (const auto& iv : st.my_intervals) {
                if (iv.first == "Uninterrupt" || iv.first == "SelfUninterrupt") {
                    return true;
                }
            }
            return false;
        };
        const bool my_uninterrupt_now = has_interrupt();
        if (my_uninterrupt_prev_ && !my_uninterrupt_now) {
            tG_ = 0;
        }
        my_uninterrupt_prev_ = my_uninterrupt_now;
        // JS `wd.Anb` (L499): the Random-tactic DECISION (`hJa`) is phase-2
        // gated (`Je==2`); the event blocks above already ran.
        if (!st.phase_fight) return "";
        return update_random(st);
    }

    // The Table-tactic DECISION (`Ykb` -> `de.ia`) is phase-2 gated too.
    if (!st.phase_fight) return "";
    // A Tabular fight uses the SAME `Gc` pipeline as the Random one
    // (`Gnb` -> `Rwa` -> `EZa` -> `dxa` -> `DK` -> `Pkb`); the only differences
    // are `eb=false` (real `<KeyPressed/>` events) and the `$Q`/`M7.Wcb` gate.
    // `update_random` is the faithful implementation of that pipeline, so a
    // Tabular tactic routes here with the gate ON instead of resolving the
    // single `ShortAttack` slot key in `pqb` (which yielded 2 of 240 foreign
    // moves - the "the enemy never attacks" symptom). Measured: the survival
    // diff gate 863 -> 794.
    if (tactic_->type == 2 &&
        std::getenv("SF2_GC_TABULAR") != nullptr) {
        return update_random(st);
    }

    // JS `de.ia` (L592): `b=a.da` (a = the ENEMY) -> `Fl = b.Pe ?
    // b.kJ()+b.Q_+this.j0(this.Uu) : -1` (the ENEMY's PER-SUB-STEP frame
    // `kJ()` + the per-frame `<FrameError>` draw); `q7 = this.Ji.kJ()+...` =
    // MY frame. `b.Q_` == 0 in the shipped JS. The `j0(Uu)` draw is the
    // frame-start `Da.pg` consumption (two `B0()` words) that precedes the
    // `jwb`/`QJa` caches; no draw when the enemy is not playing.
    Fl_ = st.enemy_playing ? st.enemy_kj + j0_draw() : -1;
    q7_ = st.playing ? st.move_kj : -1;

    // JS `de.jwb`/`de.iwb` (L25007/L24994), invoked from `wd.mwb` (L21867) by
    // the fighters' EStartAnimationEvent: both are MODEL EVENTS and already
    // ran at the top of `update()` (they fire in every phase). The remaining
    // `QJa` cadence is (b) below — the per-frame rebuild while the enemy's
    // move is a `<RandomizingEnemyAnimation>` child (`ia` L593).

    // JS `de.ia` (L592) — the `mW` watch-recompute, the load-bearing piece
    // the port lacked: after `dsb` (the watch), the JS recomputes the wait
    // `eh` from the OPPONENT's current move length (clamped to MY move
    // length) instead of holding a fixed count. `c=this.Ji.Ua` (my move),
    // `d=b.Ua` (the opponent's move); `e=c.$I()` (my Strict end).
    if (mW_) {
        mW_ = false;
        // JS `ia` L592: `this.mW&&(this.mW=!1,this.Ji.Pe&&b.Pe)` — `mW` is
        // ALWAYS cleared, but the recompute requires BOTH fighters to be
        // PLAYING (`Ji.Pe` = mine, `b.Pe` = the enemy's).
        if (st.playing && st.enemy_playing && st.current_move != nullptr &&
            st.enemy_move != nullptr) {
            const int eI = vba_full(*st.current_move, strict_end(*st.current_move));
            switch (oC_) {
                case 1: {
                    // JS: `d=this.eh=d.p0(!0)-this.Fl+1` — the `i0` sub-frame
                    // conversion of the Attack end.
                    int d = attack_end_full(*st.enemy_move) - Fl_ + 1;
                    const int c =
                        vba_full(*st.current_move, extended_end(*st.current_move));
                    eh_ = d > c ? c : d;
                    if (eI > eh_) eh_ = eI;
                    --eh_;
                    break;
                }
                case 2: {
                    // JS: `d=this.eh=d.zD(!0)-this.Fl+1`.
                    int d = uninterrupt_end_full(*st.enemy_move) - Fl_ + 1;
                    const int c =
                        vba_full(*st.current_move, extended_end(*st.current_move));
                    eh_ = d > c ? c : d;
                    if (eI > eh_) eh_ = eI;
                    --eh_;
                    break;
                }
                case 3:
                    eh_ = eI;
                    --eh_;
                    break;
                default:
                    break;
            }
        }
    }
    oC_ = 0;

    // The wait counter (JS L592-593): `if(this.eh>1) return --this.eh, null`.
    dbg_.eh = eh_;
    if (eh_ > 1) {
        dbg_.waited = true;
        --eh_;
        return "";
    }

    // JS `ia` L593: `b=b.Ua; if(b!=null){e=!1;c=0;for(d=P.y$a();c<d.length;)
    // if(d[c++].children.includes(b)){e=!0;break} e&&this.QJa(a)}` — while the
    // ENEMY's move is a child of a `<RandomizingEnemyAnimation>` group the JS
    // REBUILDS the QJa roll cache EVERY frame (6 `Da.jf()` + `Mu`/`lN`), on
    // top of the `jwb` rebuild at the move start.
    if (st.enemy_move != nullptr && in_randomizing_group(*st.enemy_move)) {
        qja(st);
    }

    // JS `ia` L593: `this.csb(); this.Zqb();` — roll every base
    // QuickAttack/Evade slot's `t4` (`Da.jf()` each, in slot order) before
    // the `hcb` gate and `dqb`. The `bsb`/`Yqb` curve evaluation is
    // draw-free and runs later (`eval_slots`, inside `pqb`).
    roll_slots();

    // The no-decision gate (JS L593: `if(!this.hcb()) return null`).
    // ORDER MATTERS: the JS runs `hcb` BEFORE `dqb`. The port drew `dqb`
    // first, so every gated (`hcb` false) frame consumed one extra
    // `Da.jf()` roll and shifted the whole shared stream.
    dbg_.hcb = hcb(st);
    if (!dbg_.hcb) return "";

    // The distance category + chance draw (JS L593: `this.aqa=this.dqb(a)`).
    aqa_ = dqb(st);
    // Record the category of THIS pass (the earlier write, before the roll,
    // reported the previous frame's value - the "aqa=1 always" misreading).
    dbg_.aqa = aqa_;

    // The core decision (JS L594).
    int cnt = pqb(st);
    const int cnt_pqb = cnt;
    // `dbg_.fk` is recorded at the END of the pass (after the `dsb` watch sets
    // `fk=-2`): recording it here showed the pre-watch value, so the `[ai]`
    // log never displayed the `fk=-2` watch state the oracle spends 70% of
    // its frames in - a third stale-field trap in the same log.
    dbg_.wb = cnt;

    // The conditional-slot additions (JS L594: `b+=this.k_a(a.da,this.u$),
    // b+=this.Nwa(this.E7), this.iN.zZ.length>0&&(this.fk=11)`). `u$`/`E7`
    // are the <ConditionalDecisions> lists — NOT the base `$E`/`nD`.
    int ka_add = 0;
    int nwa_add = 0;
    if (!F8_ && fk_ != 11) {
        if (tactic_ != nullptr) {
            const int b0 = static_cast<int>(wb_.size());
            cnt += ka(st);
            ka_add = static_cast<int>(wb_.size()) - b0;
            const int b1 = static_cast<int>(wb_.size());
            cnt += nwa(tactic_->conditional_bot, st.my_anim);
            nwa_add = static_cast<int>(wb_.size()) - b1;
            if (!feat_.zz.empty()) fk_ = 11;
        }
    }
    // [probe] `SF2_TRACE_AI_WB=1`: what the candidate list is made of per
    // decision pass (`pqb` = quick/base slots, `ka`/`nwa` = the conditional
    // table slots), with the names - the oracle's `random_pre.det` carries the
    // same composition, so the two can be compared 1:1.
    if (std::getenv("SF2_TRACE_AI_WB") != nullptr) {
        static long pass_no = 0;
        ++pass_no;
        if (!wb_.empty() || pass_no % 120 == 0) {
            std::string names;
            for (const AiCandidate& c : wb_) {
                names += c.animation;
                names += ' ';
            }
            std::fprintf(stdout,
                         "[wb] pass=%ld move_frame=%d cnt=%d pqb=%d ka=%d "
                         "nwa=%d size=%zu fk=%d names=[%s]\n",
                         pass_no, st.move_frame, cnt, cnt_pqb, ka_add, nwa_add,
                         wb_.size(), fk_, names.c_str());
            std::fflush(stdout);
        }
    }

    if (cnt <= 0 && pH_) {
        // Watch (JS dsb L600): `this.eh=-2147483648` (not a fixed count) —
        // the next frame's `mW` block recomputes `eh` from the opponent's
        // move length. `this.model.hJa(!1)` is the animation hook (n/a).
        fk_ = -2;
        mW_ = true;
        // JS `ia` (L594): `b<=0&&this.pH?this.dsb():this.oC=0;` runs dsb
        // (which sets `mW=!0`) and then the tail `if(0<b){...}else
        // this.mW&&(this.XW=!1);` clears the sticky XW in the SAME pass. The
        // port returned here before the tail, so a watch left XW latched.
        XW_ = false;
        eh_ = std::numeric_limits<int>::min();
        dbg_.fk = fk_;
        return "";
    }

    if (cnt > 0) {
        XW_ = false;
        // Filter by V1 (JS ABa L600), then split into ld/vs (JS h2a L608).
        filter_by_v1(wb_, st);
        h2a();
        // The weighted roulette (JS jL L598).
        const int idx = pick(ld_);
        if (idx >= 0) {
            eh_ = std::max(1, vs_[static_cast<std::size_t>(idx)]);
            const std::string picked = ld_[static_cast<std::size_t>(idx)].animation;
            // [experiment] `SF2_GC_ATTACK=1`: the Gc pump's key-combo attack.
            if (std::getenv("SF2_GC_ATTACK") != nullptr) {
                const std::string atk = gc_attack(st, picked);
                if (!atk.empty()) return atk;
            }
            dbg_.fk = fk_;
            return picked;
        }
        dbg_.fk = fk_;
        return "";
    }
    mW_ && (XW_ = false);
    dbg_.fk = fk_;
    return "";
}

// JS `jc.jJ` (L697) + `vm.ccb` (L749): the first `<Keys>` condition node of
// a move's condition tree and its key-combo signature (`vm.xn`). `jc.zAa`
// (L700-701) returns the first condition of type 4 (`vm` = `<Keys>`), walking
// the `<Operator>` (type 8) groups depth-first. `Pkb`'s tail compares the
// candidate's signature with the roulette pick's (`g.ccb(e.xn)` =
// `this.xn.$ga(a)` = combo multiset equality, respecting `Not`).
std::string AiController::keys_signature(const MoveDef& m) {
    std::vector<const Cond*> stack;
    for (auto it = m.conditions.rbegin(); it != m.conditions.rend(); ++it) {
        stack.push_back(&*it);
    }
    while (!stack.empty()) {
        const Cond* c = stack.back();
        stack.pop_back();
        if (c->type == "Keys") return c->keys;
        for (auto it = c->children.rbegin(); it != c->children.rend(); ++it) {
            stack.push_back(&*it);
        }
    }
    return "";
}

// JS `wd.hJa` (L500) - the Random-tactic decision. `Anb` (L499) calls it when
// the tactic `type==1`. Its body:
//   hJa(a=true){ if(a){ if(tG>0){tG--;return} tG==0&&(wN=!0,tG=-1);
//                if(!wN)return; wN=!1 }
//                Vb.data=null; ca.Ka().Vgb(Vb) }
// `ca.Vgb` (L388) -> `Gc.Vkb` (L671) -> `Ih(2,Vb,!0)`: the next `Gc.DK(c=false)`
// pass sees `eb=true` candidates and routes them through `Pkb` (L674-676):
// the `M7.Wcb` mirror filter, the `va.Ts` `<Tactics><Conditions>` filter (via
// `de.V1`, already inside `v1`), the `Md.jL` `<AnimationWeights>` roulette,
// then the tail's `jJ/ccb` mirror grouping and `DK(a,d,!0)` max-`priority`
// uniform pick (`uf.sja` = `Math.random`).
std::string AiController::update_random(const AiFightState& st) {
    // The `hJa` countdown (`tG`/`wN`).
    if (tG_ > 0) {
        --tG_;
        return "";
    }
    if (tG_ == 0) {
        wN_ = true;
        tG_ = -1;
    }
    if (!wN_) return "";
    wN_ = false;

    // `de.jL` (L598) refreshes `iN` via `mQ` before the `Md.jL` roulette; the
    // feature snapshot already ran at the top of `update()` (draw-free).

    // `Gc.EZa` (L676): the event is `Ih(2,...)` = the type-2
    // `<KeyPressed/>` slot. Candidates are MY OWN moves whose Events carry
    // type 2 and whose conditions (`f.Yz(b,null,g)`, `v1`) pass; all carry
    // `eb=true` (the `Vkb` event), so `DK(c=false)` puts them in `d`.
    std::vector<const MoveDef*> cands;
    if (st.my_moves != nullptr) {
        for (const MoveDef* m : *st.my_moves) {
            if (m == nullptr || !m->has_event("KeyPressed")) continue;
            // `Gc.EZa` (L28531) gates: the type-2 slot match (`Gc.iEa` ->
            // `Qm.compare`, `g.type == ctx.type`) + the move's own
            // `<Conditions>` (`f.Yz(b,null,g)`). `de.V1`'s `$Q` sibling gate
            // is NOT part of this path (`mirror_filter=false`); `Pkb`'s
            // `M7.Wcb` (the `keep` pass below) is.
            // Tabular (`eb=false`) applies the `$Q`/`M7.Wcb` gate; the
            // Random (`Vkb`, `eb=true`) path does not.
            if (!v1(*m, st, nullptr, /*mirror_filter=*/tactic_ != nullptr &&
                                               tactic_->type == 2)) continue;
            cands.push_back(m);
        }
    }
    if (cands.empty()) {
        fk_ = -1;
        return "";
    }

    // Diagnostic (`SF2_TRACE_RANDOM=1`): the candidate build for the Random
    // path. `keypressed` = the `<Events><KeyPressed/>` count (`Su.dea(2)`
    // membership), `passing` = those also passing `de.V1` (gm=false -> the
    // Keys gate is bypassed). No effect when the env var is unset.
    static const bool trace_random = std::getenv("SF2_TRACE_RANDOM") != nullptr;
    if (trace_random) {
        std::size_t nkp = 0;
        for (const MoveDef* m : *st.my_moves) {
            if (m != nullptr && m->has_event("KeyPressed")) ++nkp;
        }
        std::fprintf(stdout, "[random] my_moves=%zu keypressed=%zu passing=%zu:",
                     st.my_moves->size(), nkp, cands.size());
        for (const MoveDef* m : cands) std::fprintf(stdout, " %s", m->name.c_str());
        std::fprintf(stdout, "\n");
        static const char* trace_move = std::getenv("SF2_TRACE_RANDOM_MOVE");
        std::fprintf(stdout, "[random] ctx me_anim='%s' cur='%s' my_x=%.3f en_x=%.3f",
                     st.my_anim.c_str(),
                     st.current_move != nullptr ? st.current_move->name.c_str()
                                                : "(null)",
                     static_cast<double>(st.my_x), static_cast<double>(st.enemy_x));
        if (st.current_move != nullptr) {
            std::fprintf(stdout, " xl=[");
            for (const std::string& a : st.current_move->anim_names) {
                std::fprintf(stdout, "%s,", a.c_str());
            }
            std::fprintf(stdout, "]");
        }
        if (st.ref_pos) {
            float px = 0.0f, py = 0.0f, nx = 0.0f, ny = 0.0f;
            const bool okp = st.ref_pos("Pivot", "", 1, false, px, py);
            const bool okn = st.ref_pos("Nodes", "NPivot", 2, false, nx, ny);
            std::fprintf(stdout, " refPivot=%d:%.3f refEnemyNPivot=%d:%.3f",
                         okp ? 1 : 0, static_cast<double>(px), okn ? 1 : 0,
                         static_cast<double>(nx));
        }
        std::fprintf(stdout, "\n");
        std::fflush(stdout);
        std::fprintf(stdout, "[random] rejected:");
        for (const MoveDef* m : *st.my_moves) {
            if (m == nullptr || !m->has_event("KeyPressed")) continue;
            std::string why;
            if (!v1(*m, st, &why, /*mirror_filter=*/false)) {
                const std::size_t colon = why.find(':');
                std::fprintf(stdout, " %s(%.*s)", m->name.c_str(),
                             static_cast<int>(colon == std::string::npos
                                                  ? why.size()
                                                  : colon),
                             why.c_str());
                if (trace_move != nullptr && m->name == trace_move) {
                    std::fprintf(stdout, "\n[random] %s why:\n%s", m->name.c_str(),
                                 why.c_str());
                }
            }
        }
        std::fprintf(stdout, "\n");
        std::fflush(stdout);
    }

    // JS `Gc.DK` (L28394-28426) — the list handed to `Pkb`: `d` collects the
    // non-`Rha` candidates (`!(c || !h.eb || h.animation.Rha)`, `c=false`);
    // `f` is the `Aua` (L28384) max-`priority` group of the non-`Rha`
    // candidates; `e = f[uf.sja(f.length)]` (L28417, a `Math.random` draw via
    // `uf.sja` L115, NOT `Da.pg`); then
    //   `if (d.length > 0) { if (e != null) d.push(e); Pkb(a, d); }`
    // `e` is ALREADY in `d`, so `Pkb` receives a DUPLICATE of the `Aua` winner
    // (the JS `random_pre`/`jL` carry `DoubleJumpKick` twice). The port's
    // roulette list must carry the same duplicate or its weight sum differs.
    std::vector<const MoveDef*> dk_cands;
    dk_cands.reserve(cands.size() + 1);
    std::vector<const MoveDef*> aua_f;
    for (const MoveDef* m : cands) {
        if (m->no_animation) continue;  // `Rha` -> the `g` group (no clip)
        dk_cands.push_back(m);
        const int ap = m->priority;
        const int bp = aua_f.empty() ? 0 : aua_f.front()->priority;
        if (ap >= bp) {
            if (ap > bp) aua_f.clear();
            aua_f.push_back(m);
        }
    }
    if (!aua_f.empty()) {
        // `uf.sja` (L115) calls `uf.OKa.RGa()` unconditionally, so the draw is
        // consumed even for a single-element group (`floor(r*1) == 0`).
        float r = math_roll();
        if (r < 0.0f) r = 0.0f;
        if (r >= 1.0f) r = 0.9999999f;
        std::size_t ei =
            static_cast<std::size_t>(r * static_cast<float>(aua_f.size()));
        if (ei >= aua_f.size()) ei = aua_f.size() - 1;
        dk_cands.push_back(aua_f[ei]);
    }

    // `Pkb` (L674-675): drop `f` when any OTHER candidate's name is in
    // `f.animation.M7.$Q` (`g.Wcb(l.animation)` false) - `f` has a
    // higher-priority mirror superior also present.
    std::vector<const MoveDef*> keep;
    keep.reserve(dk_cands.size());
    for (std::size_t i = 0; i < dk_cands.size(); ++i) {
        const MoveDef* f = dk_cands[i];
        bool ok = true;
        for (std::size_t k = 0; k < dk_cands.size(); ++k) {
            if (k == i) continue;
            if (std::find(f->mirror_exclusive.begin(),
                          f->mirror_exclusive.end(),
                          dk_cands[k]->name) != f->mirror_exclusive.end()) {
                ok = false;
                break;
            }
        }
        if (ok) keep.push_back(f);
    }
    if (keep.empty()) {
        fk_ = -1;
        return "";
    }

    // `Md.jL` (L639-640): the weighted roulette over the surviving candidates
    // (`iCa` matches the `<AnimationWeights>` name, falling through to the
    // unnamed `<Animation Base=..>` default). Draws the `Da.pg` stream.
    std::vector<AiCandidate> rou;
    rou.reserve(keep.size());
    for (const MoveDef* m : keep) rou.push_back({m->name, 0});
    const int ci = pick(rou);
    if (trace_random) {
        std::fprintf(stdout, "[random] f=%d keep=%zu sum=%.6f roll=%.6f ci=%d:",
                     st.fight_frame, keep.size(),
                     static_cast<double>(last_pick_sum_),
                     static_cast<double>(last_pick_roll_), ci);
        for (std::size_t i = 0; i < keep.size(); ++i) {
            std::fprintf(stdout, " %s", keep[i]->name.c_str());
        }
        std::fprintf(stdout, "\n");
        std::fflush(stdout);
    }
    if (ci < 0) {
        fk_ = -1;
        return "";
    }
    const MoveDef* chosen = keep[static_cast<std::size_t>(ci)];

    // `Pkb` tail (L675-676): keep the candidates whose `jJ().xn` matches the
    // chosen's (`g.ccb(e.xn)`); a null `jJ` on either side pushes
    // unconditionally. Then `DK(a,d,!0)` takes the max-`priority` (`Aua`,
    // non-`Rha`) group and picks uniformly (`uf.sja` = `Math.random`).
    const std::string sig = keys_signature(*chosen);
    std::vector<const MoveDef*> d;
    d.reserve(keep.size());
    for (const MoveDef* m : keep) {
        const std::string s = keys_signature(*m);
        if (sig.empty() || s.empty() || s == sig) d.push_back(m);
    }
    std::vector<const MoveDef*> f;
    for (const MoveDef* m : d) {
        if (m->no_animation) continue;  // `Rha` -> the `g` group (unused)
        const int ap = m->priority;
        const int bp = f.empty() ? 0 : f.front()->priority;
        if (ap >= bp) {
            if (ap > bp) f.clear();
            f.push_back(m);
        }
    }
    if (f.empty()) {
        fk_ = -1;
        return "";
    }
    std::size_t idx = 0;
    if (!f.empty()) {
        // `uf.sja` (L4476) calls `uf.OKa.RGa()` unconditionally, so a
        // non-empty `f` consumes one `Math.random` tick even at length 1
        // (`floor(r*1) == 0` -> index 0, but the stream advances). The old
        // `f.size() > 1` guard skipped it and left the port's stream one draw
        // behind the JS after the first decision.
        float r = math_roll();
        if (r < 0.0f) r = 0.0f;
        if (r >= 1.0f) r = 0.9999999f;
        idx = static_cast<std::size_t>(r * static_cast<float>(f.size()));
        if (idx >= f.size()) idx = f.size() - 1;
    }
    dbg_.branch = "random/hJa";
    dbg_.fk = 6;  // the `<AnimationWeights>` stage (`Md.jL`)
    dbg_.wb = static_cast<int>(f.size());
    fk_ = 6;
    return f[idx]->name;
}

} // namespace sf2::scene




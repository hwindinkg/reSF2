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
//     the JS move-object change (`mwb`->`jwb`); `iwb`'s `eh=1` reset on my
//     move change is not ported (OPEN — needs a live trace to confirm the
//     `mwb` call context).
//   - `xaa` consumes the `Aea` draw for stream position; the Ju-frame-horizon
//     application (`b=Fl+b` windowing over `Ju.frames`, L611) is OPEN — the
//     port keys outcomes off distance windows only.
// Exact since this wave (no oracle needed — pure JS math):
//   - `Da.jf()` stream: owned `DaPrng` (Xx+Rk, L2352/2366); QJa 5-roll cache,
//     `$x` ResponseDelay cache, per-pass dqb/jL/XW/slot draws all on it in
//     JS call order when no roll01 override is injected.

#include "scene/ai.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "scene/conditions.hpp"
#include "scene/move_def.hpp"

namespace sf2::scene {

namespace {

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
    if (m.end_frame > 0) best = std::min(best, m.end_frame);
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

// JS `de.Ycb` (L620): false when the fighter is playing and its current
// frame is inside its Uninterrupt window (`ip() <= zD(!1)`). Called with
// the ENEMY's state (JS `de.Ycb(b)` where b = the enemy's da controller).
bool ycb(const AiFightState& st) {
    if (st.enemy_move == nullptr) return true;
    return !(st.enemy_move_frame <= uninterrupt_end(*st.enemy_move));
}

// JS `de.Lbb` (L621): false when the fighter is playing and its current
// frame is inside its attack window (`ip() <= p0(!1)`). Called with the
// ENEMY's state.
bool lbb(const AiFightState& st) {
    if (st.enemy_move == nullptr) return true;
    return !(st.enemy_move_frame <= attack_end(*st.enemy_move));
}

// JS `jc.pcb` (L700): `ocb(M6a(a))` — the move has an Uninterrupt interval
// covering the frame derived from `a` (M6a: qx-1+((a-1)/(XJ+1))). The
// native port simplifies to: the move has an Uninterrupt interval whose
// range contains `a`.
bool pcb(const MoveDef& m, int frame) {
    for (const Interval& iv : m.intervals) {
        if (iv.name == "Uninterrupt" && iv.start <= frame && frame <= iv.end) {
            return true;
        }
    }
    return false;
}

}  // namespace

void AiController::init(const std::string& weapon,
                        const std::vector<TacticsFile>& tactics,
                        const TacticDef* tactic,
                        const std::map<std::string, MoveDef>* moves) {
    weapon_ = weapon;
    tactics_ = tactics;
    tactic_ = tactic;
    moves_ = moves;
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

// JS `mQ` (L620): build the feature state from the fight snapshot.
// Field semantics (exact):
//   o1/q1 = ABSOLUTE hp (`this.model.parameters.gd` / `b.parameters.gd`;
//     gd is absolute — the ratio form `gd/Zn` exists separately, e.g. the
//     low-HP check `l.parameters.gd/l.parameters.Zn<=v.u4.Uva`). Curves
//     carrying HealthFactor (1, 3) / EnemyHealthFactor (-1, -3) were authored
//     for absolute inputs (e.g. Cautious = gd_en - gd_me); feeding ratios
//     would graduate a nearly-binary signal — verified against
//     res/tactic_settings.xml scales 2026-09-04.
//   xY = enemy `kJ()` (played steps); the port feeds `enemy_move_frame`
//     (Xh-based, same quantity as `Fl_`) — the kJ-vs-Xh residual is OPEN.
//   pZ = `Tba` (max M2 part frames) — `enemy_max_part_frames` ✓.
//   counter/Xb/tf = strike-memory accumulators (`Cn.d0`) — the port has no
//     strike memory yet, stays 0 (documented divergence, not silent).
void AiController::mq(const AiFightState& st) {
    AiFeatureState& f = feat_;
    f.counter = 0.0f;              // no strike-memory accumulators yet
    f.xb = 0.0f;
    f.tf = 0.0f;
    f.o1 = st.my_hp;               // absolute gd (NOT a ratio — see above)
    f.q1 = st.enemy_hp;            // absolute gd
    f.xY = static_cast<float>(st.enemy_move_frame);
    f.cl = static_cast<float>(st.magic_bullets);
    f.k2 = static_cast<float>(st.ranged);
    f.pz = static_cast<float>(st.enemy_max_part_frames);
    // The NPivot distance (JS `s6a` L619-620): |my NPivot x - enemy NPivot
    // x| — the native port uses the fighter world x.
    f.lya = std::fabs(st.enemy_x - st.my_x);
    f.shift = 0.0f;
    f.my_anim = st.my_anim;
    f.enemy_anim = st.enemy_anim;
    f.conditional = false;
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
    if (r < cz) return 2;
    if (r < cz + bda) return 3;
    if (r < cz + bda + tba) return 4;
    return 1;
}

// JS `hcb` (L598-599): the no-decision gate. The JS requires the fighter
// to be PLAYING an animation (`Ji.Pe && cs != null`) — the native fighter
// plays the stance idle when idle, so the demo passes a non-null
// current_move_ for the idle case via the caller; here we only gate on the
// NoDecision lists.
bool AiController::hcb(const AiFightState& st) const {
    // NoDecision intervals (P.osa = ["Uninterrupt","SemiUninterrupt"]).
    for (const auto& iv : st.my_intervals) {
        if (iv.first == "Uninterrupt" || iv.first == "SemiUninterrupt") {
            return false;
        }
    }
    // NoDecision moves (P.psa = ["Physical"]).
    if (st.current_move != nullptr && st.current_move->name == "Physical") {
        return false;
    }
    return true;
}

// JS `fCa` (L599-600): whether the enemy is playing a cautious anim
// (variant 0 = missiles, variant 1 = magic) AND is facing / in reach.
// The native port derives it from the enemy's current animation name.
bool AiController::fca(const AiFightState& st, int variant) const {
    if (st.enemy_anim.empty()) return false;
    // The JS checks each enemy body part's animation controller; the
    // native fighter has one animation, so we check the current anim
    // against the missile/magic animation name lists (P.Yra / P.Lra):
    //   missiles: RangedMissile, MagicMissile, MagicMissileStart
    //   magic:    MagicMissile, MagicStart
    // (these match computer_settings.xml's MissileAnimations /
    // MagicAnimations lists).
    const bool is_missile =
        st.enemy_anim == "RangedMissile" || st.enemy_anim == "MagicMissile" ||
        st.enemy_anim == "MagicMissileStart";
    const bool is_magic =
        st.enemy_anim == "MagicMissile" || st.enemy_anim == "MagicStart";
    if (variant == 0) return is_missile;
    return is_magic;
}

// JS `V1` (L601-602): a candidate move must be in `me` (the fighter's move
// list) and its tactics conditions (`FQ(2)` = the `Ts` list) must pass.
bool AiController::v1(const MoveDef& m, const AiFightState& st) const {
    if (moves_ == nullptr || moves_->find(m.name) == moves_->end()) return false;

    // Build a FightContext with the AI's state for the tactics conditions.
    FightContext ctx;
    ctx.roll01 = [this]() { return roll01(); };  // owned stream or override
    ctx.stage = round_stage::fight;
    ctx.anims_me = {st.my_anim};
    ctx.anims_enemy = {st.enemy_anim};
    ctx.dist_x = st.enemy_x - st.my_x;
    ctx.dist_3d = std::fabs(ctx.dist_x);
    ctx.health_ratio = st.my_max_hp > 0.0f ? st.my_hp / st.my_max_hp : 1.0f;
    ctx.candidate_moves = {m.name};
    for (const auto& iv : st.my_intervals) {
        ctx.intervals.push_back({iv.first, iv.second, true});
    }
    return eval_move_conditions(m.tactics, ctx);
}

// Resolves a candidate animation/tag name to the move(s) whose name or
// template tags match (JS `jc.$k`/`d2` L698: `name==a || xl.includes(a)`
// where `xl` = the move's own name + inherited template tags). The
// QuickAttack slot tags (`ShortAttack`, `Throw`) are the OLD game's move
// tags; `ShortAttack` maps to the `Punch`-tagged Fists moves in this
// build (documented — the shipped tactic_settings.xml references it).
std::vector<const MoveDef*> resolve_candidate(const std::string& anim,
                                              const std::map<std::string, MoveDef>& moves) {
    std::vector<const MoveDef*> out;
    for (const auto& kv : moves) {
        const MoveDef& m = kv.second;
        if (m.name == anim || m.template_tags.count(anim) > 0) {
            out.push_back(&m);
        }
    }
    if (out.empty() && anim == "ShortAttack") {
        for (const auto& kv : moves) {
            const MoveDef& m = kv.second;
            if (m.template_tags.count("Punch") > 0) {
                out.push_back(&m);
            }
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
int AiController::pick(const std::vector<AiCandidate>& cands) const {
    if (tactic_ == nullptr || cands.empty()) return -1;
    float sum = 0.0f;
    for (const AiCandidate& c : cands) {
        const std::string& anim = c.animation;
        float w = 0.0f;
        for (const auto& kv : tactic_->anim_weights) {
            if (kv.first.empty() || kv.first == anim) {
                w = weight_curve_eval(kv.second, feat_);
                break;
            }
        }
        sum += w;
    }
    if (sum <= 0.0f) return -1;
    float r = roll01() * sum;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        const std::string& anim = cands[i].animation;
        float w = 0.0f;
        for (const auto& kv : tactic_->anim_weights) {
            if (kv.first.empty() || kv.first == anim) {
                w = weight_curve_eval(kv.second, feat_);
                break;
            }
        }
        r -= w;
        if (r < 0.0f) return static_cast<int>(i);
    }
    return static_cast<int>(cands.size()) - 1;
}

// The per-row outcome lookup (JS `PBa` L617 + `Gu.n0` L634): for a table
// row, find the outcome case whose distance window contains `dist` and
// append kd(animation, outcome_id) to `out`. Returns the count added.
// `hu_pick >= 0` restricts to one Hu frame's outcomes (JS `Ju.frames[k]`,
// L611); `horizon >= 0` drops waits beyond it (JS `r<=b`, `b=Fl+Aea`).
namespace {
int pba_append(const TacticRow& row, float dist, std::vector<AiCandidate>& out,
               int hu_pick = -1, int horizon = -1) {
    int added = 0;
    for (const TacticOutcome& oc : row.outcomes) {
        if (hu_pick >= 0 && oc.hu_index != hu_pick) continue;
        // Gu.n0 (L634): JI = the sorted float edges, NDa = the u32
        // outcomes. `n0(d)` returns the u32 whose window contains d:
        //   JI[0] <= d < JI[last] -> NDa[first index with JI[i] > d]
        // The native port uses oc.window_outcomes paired with
        // oc.window_edges (same layout).
        if (oc.window_edges.empty()) continue;
        int idx = -1;
        for (std::size_t i = 0; i < oc.window_edges.size(); ++i) {
            if (dist < oc.window_edges[i]) {
                idx = static_cast<int>(i);
                break;
            }
        }
        if (idx < 0 || static_cast<std::size_t>(idx) >= oc.window_outcomes.size()) {
            continue;
        }
        const std::uint32_t outcome = oc.window_outcomes[static_cast<std::size_t>(idx)];
        if (outcome == 0) continue;
        // JS `r<=b` (L611): the outcome wait must fit the Ju horizon.
        if (horizon >= 0 && static_cast<int>(outcome) > horizon) continue;
        out.push_back({oc.anim, static_cast<int>(outcome)});
        ++added;
    }
    return added;
}
}  // namespace

// JS `YAa` (L608-609) + `Q6a` (L609-610): the safe-attack table selection.
// Finds the record for my weapon pair in the safe-attack table (Z0()[1])
// and adds the distance-windowed outcome candidates.
const TacticRecord* AiController::find_record(const std::string& enemy_anim) const {
    for (const TacticsFile& tf : tactics_) {
        if (tf.version == 2 || tf.version == 7) continue;  // single-weapon tables
        for (const TacticRecord& r : tf.set.tables[0]) {
            if ((r.weapon.empty() || r.weapon == oo_ || weapon_.empty()) &&
                r.anim == enemy_anim) {
                return &r;
            }
        }
    }
    return nullptr;
}

int AiController::yaa(const AiFightState& st) {
    wb_.clear();
    Ao_ = (Fl_ % 5) != 0;  // P.sp (TablesReduction Step) = 5
    if (st.enemy_anim.empty()) return 0;

    const TacticRecord* rec = find_record(st.enemy_anim);
    if (rec == nullptr) return 0;

    // Round the enemy frame up to a P.sp multiple (JS L610: `f = g%P.sp!=0
    // ? g+P.sp-g%P.sp : g`).
    const int g = st.enemy_max_part_frames;
    const int f = (g % 5) != 0 ? g + 5 - g % 5 : g;

    // For each condition row, the target x (JS `Wea` L600: the row label's
    // NPivot x — the native port uses the enemy x) and the frame window:
    //   l*(t + (b.aU.xea(f,r) - b.aU.xea(g,r))*d - e) + h
    // where l = my facing, t = the enemy bone x, d = my facing, e = the
    // enemy's dw (body width), h = the DistanceError draw. The native
    // fighter has one body; the xea displacement is 0, so the target =
    // l*(t - e) + h.
    const float target = st.my_facing * (st.enemy_x - 0.0f) + static_cast<float>(Mu_);
    for (const TacticRow& row : rec->rows) {
        pba_append(row, target, wb_);
    }
    if (f == g || wb_.empty()) {
        return static_cast<int>(wb_.size());
    }
    // JS: if no outcomes matched, push a wait-only candidate (null anim +
    // the frames until the rounded frame).
    if (wb_.empty()) {
        wb_.push_back({std::string(), f - g});
        return 1;
    }
    return static_cast<int>(wb_.size());
}

// JS `XAa` (L611-612): the attack-table selection (Z0()[0]).
int AiController::xaa(const AiFightState& st) {
    wb_.clear();
    if (Fl_ % 5 != 0) {
        Ao_ = true;
        return 0;
    }
    Ao_ = false;
    if (st.enemy_anim.empty()) return 0;

    // JS `XAa` (L611): `b=this.Aea(this.Eqa)` — the EnemyResponseDelay draw,
    // then `b=this.Fl+b` (the Ju-frame horizon). The draw is consumed here
    // so the stream position matches; the horizon windowing over Ju.frames
    // is OPEN (the port keys outcomes off distance windows only).
    aea_ = aea_draw();

    const TacticRecord* rec = find_record(st.enemy_anim);
    if (rec == nullptr) return 0;

    // JS L611-612: per Ju row, the Hu frame `k = $_(Fl)` (row frame at
    // the enemy frame) selects that frame's outcomes; each outcome's
    // `n0(n)` (distance window) picks the anim, and waits beyond the
    // horizon `b = Fl + Aea` are dropped (`r<=b`). The Wea-per-label
    // target needs enemy bone data (OPEN) — the port keeps enemy x.
    (void)Fl_; (void)feat_.pz;
    const float target = st.my_facing * st.enemy_x + static_cast<float>(Mu_);
    const int horizon = Fl_ + aea_;
    for (const TacticRow& row : rec->rows) {
        const int k = ju_frame_index(Fl_, row.rda, row.hu_frames);
        if (k < 0) continue;
        pba_append(row, target, wb_, k, horizon);
    }
    return static_cast<int>(wb_.size());
}

// JS `Gea` (L613-616): the throw-table selection (Z0()[2]). The JS uses
// the move's third table (throws) with a single candidate; the native port
// looks up the throw table records the same way.
int AiController::gea(const AiFightState& st, int variant) {
    (void)variant;
    wb_.clear();
    if (st.enemy_anim.empty()) return 0;
    const TacticRecord* rec = find_record(st.enemy_anim);
    if (rec == nullptr) return 0;
    const float target = st.my_facing * st.enemy_x + static_cast<float>(Mu_);
    for (const TacticRow& row : rec->rows) {
        pba_append(row, target, wb_);
    }
    return static_cast<int>(wb_.size());
}

// JS `Nwa` (L603): the QuickAttack/Evade slot candidates — each slot whose
// chance curve passes (the `Gl`/`bsb` gate: `N_ = t4 < gZ` where gZ is the
// curve score and t4 the last-fire frame) contributes its animation names.
// The slot fires when `roll < score` (JS `Gl.N_` — a per-pass chance draw).
int AiController::nwa(const std::vector<AiAnimSlot>& slots, const std::string& anim) {
    // JS `bqb` (L642): pick the highest-priority slot whose conditions
    // pass (`d.compare()`); if none, return null.
    const AiAnimSlot* best = nullptr;
    for (const AiAnimSlot& s : slots) {
        if (s.names.empty()) continue;
        // The slot's chance gate: fires when `roll < score` (JS `Gl.N_` =
        // the per-slot curve evaluate vs the rolled threshold). The native
        // port rolls fresh each pass (the JS re-rolls when the enemy plays
        // a RandomizingEnemyAnimation — documented in README).
        const float score = weight_curve_eval(s.chance, feat_);
        if (!(roll01() < score)) continue;
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
    if (best == nullptr) return 0;

    int added = 0;
    for (const std::string& n : best->names) {
        // Skip names already in wb_ (JS L603: `J.remove(a, wb[d].animation
        // .name)`).
        bool dup = false;
        for (const AiCandidate& c : wb_) {
            if (c.animation == n) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        wb_.push_back({n, 0});
        ++added;
    }
    return added;
}
// JS `de.gfa` (L597) = `Gc.gfa(a)+1` where `G` = `Md.I0(z$)` (L640-643):
// `I0(a,b) = Da.pg.dT(a,b)` truncated. `+1` is the de-level addition.
int AiController::gfa_draw() const {
    if (tactic_ == nullptr) return 1;
    const double lo = weight_curve_eval(tactic_->response_delay_min, feat_);
    const double hi = weight_curve_eval(tactic_->response_delay_max, feat_);
    return static_cast<int>(prng_.dT(lo, hi)) + 1;
}

// JS `de.Aea` (L597) = `Gc.Aea(a)`, `Md.I0(v8)` (L640-643), truncated,
// NO +1. Consumed per `XAa` call (L611: `b=Aea(Eqa)`, then `b=Fl+b`).
int AiController::aea_draw() const {
    if (tactic_ == nullptr) return 0;
    const double lo = weight_curve_eval(tactic_->enemy_response_delay_min, feat_);
    const double hi = weight_curve_eval(tactic_->enemy_response_delay_max, feat_);
    return static_cast<int>(prng_.dT(lo, hi));
}

// JS `QJa` (L594-595): rebuild the enemy-relative context (the port's
// `feat_` was already built by `mq`), draw the five `Da.jf()` rolls,
// `Mu`/`lN` (yea/j0, L640-641) and cache `$x` (gfa). The JS additionally
// snapshots strike-memory counters (`Cn.d0`) — the port has no
// strike-memory yet, accumulators stay 0 (same as `mq`).
void AiController::qja(const AiFightState& st) {
    (void)st;
    // NOTE the leading discarded draw (JS `QJa` L594-595 opens with a bare
    // `Da.jf();` before the five assignments) — load-bearing for stream
    // position: 6 jf() calls = 12 B0 draws, then Mu (2), lN (2), $x (2).
    prng_.jf();  // discarded
    tua_ = prng_.jf();
    dua_ = prng_.jf();
    bpa_ = prng_.jf();
    rqa_ = prng_.jf();
    oqa_ = prng_.jf();
    // yea (L640-641, untruncated) / j0 (truncated).
    if (tactic_ != nullptr) {
        Mu_ = prng_.dT(weight_curve_eval(tactic_->distance_error_min, feat_),
                       weight_curve_eval(tactic_->distance_error_max, feat_));
        lN_ = static_cast<int>(prng_.dT(weight_curve_eval(tactic_->frame_error_min, feat_),
                                       weight_curve_eval(tactic_->frame_error_max, feat_)));
    }
    x_ = gfa_draw();  // JS `jwb` (L596-597): `this.$x=this.gfa(this.Ol)`
}

// JS `Pqb` (L604-608): the core decision. Returns the candidate count.
int AiController::pqb(const AiFightState& st) {
    wb_.clear();
    pH_ = F8_ = false;

    // Facing lock (JS L604): `b6a(b)*b.hd()>0` — when the direction toward
    // the enemy matches my facing... wait — the JS is `b6a(b)` where b =
    // the OPPONENT's anim controller; `b6a` returns sign(opponent.x -
    // my.x) — the direction toward me FROM the opponent... no. Let me
    // re-derive: `b6a(a){a=a.Fe();let b=this.Ji.Fe();return a.ma.x-b.ma.x
    // >=0?1:-1}` — a = opponent body, b = my body → sign(opponent.x -
    // my.x) = the direction from me TOWARD the opponent. Then `b6a(b) *
    // b.hd()` — b.hd() = the OPPONENT's facing. If the direction toward
    // the opponent has the same sign as the opponent's facing... that
    // means the opponent faces AWAY from me. So: when the opponent faces
    // away, the AI watches (doesn't attack).
    const int dir_to_opp = b6a(st);
    if (dir_to_opp * st.enemy_facing > 0) {
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
    if (dodge_fired) return static_cast<int>(wb_.size());

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

    // The response-delay + uninterruptible gate (JS L604-605):
    //   $x < enemy_frame && !Ycb(enemy)   (enemy in its move start)
    //   && my move is uninterruptible at Fl (ds.pcb(Fl))
    // `$x` is the CACHED ResponseDelay from `jwb` (NOT re-rolled per pass).
    // `ycb`/`lbb` test the ENEMY's current move (JS `de.Ycb(b)` where b =
    // the passed-in enemy's da controller).
    const int enemy_frame = st.enemy_move_frame;
    if (enemy_frame > x_ && !ycb(st)) {
        if (st.current_move == nullptr || pcb(*st.current_move, Fl_)) {
            if (lbb(st)) {
                // Safe attack / attack table (JS L605).
                if (tactic_ != nullptr) {
                    if (rua) {
                        const int b = yaa(st);
                        if (b > 0) fk_ = 1;
                        if (Ao_ || b > 0) return static_cast<int>(wb_.size());
                    }
                    if (caa) {
                        const int b = xaa(st);
                        if (b > 0) fk_ = 0;
                        if (Ao_ || b > 0) return static_cast<int>(wb_.size());
                    }
                    // Cautious movements (JS L605): the P.nCa list — the
                    // native port pushes the CautiousMovements animations.
                    if (nG) {
                        wb_.push_back({"StepForward", 0});
                        wb_.push_back({"StepBack", 0});
                        fk_ = 5;
                        return static_cast<int>(wb_.size());
                    }
                }
                oC_ = 2;
                pH_ = true;
                return 0;
            }
            // The enemy is in its attack window -> distance-based response
            // (JS L605 switch on aqa).
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

    // QuickAttack / Evade slots (JS L606-608).
    bool quick_fired = false;
    if (tactic_ != nullptr) {
        for (const auto& s : tactic_->quick_attacks) { (void)s;
            // The slot fires with probability = its score (the JS uses the
            // per-slot curve evaluated against the feature state; the
            // native port rolls uniformly for the demo's Base-only slots).
            const int before = static_cast<int>(wb_.size());
            nwa(tactic_->quick_attacks, st.my_anim);
            if (static_cast<int>(wb_.size()) > before) quick_fired = true;
        }
        if (quick_fired) fk_ = 6;
    }

    // The surprise / evade-throw branch (JS L607-608):
    //   XW = 1 - 1/expectedWait < roll   (surprise)
    //   IB = the enemy would evade a throw
    // If either, the AI re-attacks from the attack table or the
    // EvadeThrowDodges / cautious lists.
    float expected_wait = 1.0f;
    if (tactic_ != nullptr && st.current_move != nullptr) {
        for (const auto& kv : tactic_->expected_wait) {
            if (kv.first.empty() || kv.first == st.current_move->name) {
                expected_wait = weight_curve_eval(kv.second, feat_);
                break;
            }
        }
    }
    if (expected_wait < 1.0f) expected_wait = 1.0f;
    XW_ = (1.0f - 1.0f / expected_wait) < roll01();

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
            // EvadeThrowDodges (P.H9a = Bqa): BackHandflip.
            wb_.push_back({"BackHandflip", 0});
            b = static_cast<int>(wb_.size());
            if (b > 0) fk_ = 9;
        } else if (tactic_ != nullptr) {
            // JS L607-608 reuses the CACHED `nG` here.
            if (nG) {
                wb_.push_back({"StepForward", 0});
                wb_.push_back({"StepBack", 0});
                b = static_cast<int>(wb_.size());
                if (b > 0) fk_ = 5;
            }
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
    if (tactic_ == nullptr || moves_ == nullptr) return "";
    // An injected source overrides the owned DaPrng stream (ai_demo uses
    // mt19937); otherwise ALL draws come from `prng_` in JS call order.
    roll01_fn_ = st.roll01;

    // Snapshot the features (JS mQ L620).
    mq(st);
    // JS `de.ia` (L592): `b=a.da` (a = the ENEMY) -> `Fl = b.kJ()+b.Q_+
    // j0(Uu)` = the ENEMY's animation frame; `q7 = this.Ji.kJ()+...` = MY
    // animation frame.
    Fl_ = st.enemy_move_frame;
    q7_ = st.move_frame;

    // Enemy move change -> refresh the QJa roll cache (JS `mwb`->`jwb`
    // L596-597; the port detects the change by enemy anim name — the
    // observable proxy for the move-object swap).
    if (!qja_done_ || st.enemy_anim != last_enemy_anim_) {
        qja(st);
        last_enemy_anim_ = st.enemy_anim;
        qja_done_ = true;
    }

    // The wait counter (JS L592): `if(this.eh>1) return --this.eh, null`.
    if (eh_ > 1) {
        --eh_;
        return "";
    }
    eh_ = 1;

    // The distance category + chance draws (JS L593-594).
    aqa_ = dqb(st);

    // The no-decision gate (JS L593: `if(!this.hcb()) return null`).
    if (!hcb(st)) return "";

    // The core decision (JS L594).
    int cnt = pqb(st);

    // The QuickAttack/Evade additions (JS L594: `b+=this.k_a(...),
    // b+=this.Nwa(...)`).
    if (!F8_ && fk_ != 11) {
        if (tactic_ != nullptr) {
            cnt += static_cast<int>(nwa(tactic_->quick_attacks, st.my_anim));
            cnt += static_cast<int>(nwa(tactic_->evades, st.my_anim));
        }
    }

    if (cnt <= 0 && pH_) {
        // Watch (JS dsb L600): wait a long time; the mW re-evaluation at
        // the top of the next update clears it when the state changes.
        fk_ = -2;
        mW_ = true;
        eh_ = 120;
        return "";
    }

    if (cnt > 0) {
        XW_ = false;
        // Filter by V1 (JS ABa L600).
        filter_by_v1(wb_, st);
        // Copy to ld/vs (JS h2a L608).
        ld_ = wb_;
        vs_.resize(ld_.size(), 0);
        for (std::size_t i = 0; i < ld_.size(); ++i) vs_[i] = ld_[i].wait;
        // The weighted roulette (JS jL L598).
        const int idx = pick(ld_);
        if (idx >= 0) {
            eh_ = std::max(1, vs_[static_cast<std::size_t>(idx)]);
            return ld_[static_cast<std::size_t>(idx)].animation;
        }
        return "";
    }
    mW_ && (XW_ = false);
    return "";
}

} // namespace sf2::scene




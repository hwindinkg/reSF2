#pragma once

// AI controller: port of the game's `de` class (sf2.502f0946.js L589-621,
// g="E2") — the per-frame enemy/player decision routine, plus the tactics
// file parser (`sb` L648-653, g="F0") and the tactics settings config
// (`P` g="E5" + `Md` g="EB").
//
// JS execution-path study (see core/scene/README.md "AI" section):
//   - fight tick:  ca.Ea (L385) -> nzb() (facing lock, L390) -> ia()
//     (L388) -> Hnb -> wd.ia (L498) -> Anb (L499: `(Fj||P.fP)&&Je==2`)
//     -> Ykb -> `this.nf.ia(a, this.Iqa)` = de.ia (L500).
//   - de.ia (L592-594) is the decision pass; it returns a move (`jc`) or
//     null; wd.Ykb then does `a.jJ()` (the anim name) -> `da.rva=b` ->
//     `Okb(a)` -> `NS` (L505) -> `da.Skb` = the native Fighter::try_start_move.
//   - the decision uses the tactics tables registered by weapon pair
//     (`Si.cxb/dxb` L653-655 -> P.$ua -> P.wO[version][(a,b)]) and the
//     tactic settings (`P.zmb` L623 reads tactic_settings.xml + the
//     tables-reduction params from computer_settings.xml asset 1314).
//
// This port implements the FULL decision structure faithfully:
//   - tactics_parse: the `.dat` container (u32 version, cstr weapon pair,
//     blob) -> sb blob (anim-id pools + per-anim condition rows + the
//     distance-window outcome tables).
//   - TacticDef: the tactic_settings.xml <Tactic> (AnimationWeights +
//     UseDefense/TableAttack/... chance curves + QuickAttack/Evade slots +
//     ExpectedWait).
//   - AiController::update (de.ia semantics): facing, interval wait,
//     move-length / uninterrupt gates, UseSafeAttack/TableAttack draws,
//     per-slot QuickAttack/Evade scoring, the weighted roulette pick
//     (`jL`), and the returned move.

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "scene/conditions.hpp"
#include "scene/fighter.hpp"
#include "scene/move_def.hpp"

namespace sf2::scene {

// ---------------------------------------------------------------------------
// Exact port of the JS PRNG: `Da` + `Rk` (sf2.502f0946.js L2352) on top of
// the `Xx` LCG (L2366).
//
//   Xx.Olb(a) = (a*1103515245 + 12345) mod 2^32, result mod 2^31
//     (dz.Nr = u32 mul, dz.dlb = u32 add, then %2147483648; glibc rand(3)
//     constants — portable).
//   Rk.jf()  = B0()/2^31 + (B0()/2^31)/2^31  (TWO sequential draws,
//     double arithmetic, left-assoc: b1 + b2/2^31).
//   Rk.s4(a) = jf()*a;  Rk.dT(a,b) = a+s4(b-a)  (== Md.I0 core, L643);
//     Rk.cT(a,b=100) = s4(b)<a.
// NOTE the Da.cT shortcut lives at the CALLER (L2352:
// `a>b ? true : pg.cT(a,b)` — no stream draw when a>b); this class is the
// Rk level. All methods use double arithmetic to match the JS number
// semantics bit-for-bit (verified against Node in reference/tools/ai_golden).
class DaPrng {
public:
    explicit DaPrng(std::uint32_t seed = 0) : mf_(seed) {}
    void seed(std::uint32_t s) { mf_ = s; }
    // Xx.Olb (L2366): advance the LCG, return mf mod 2^31.
    std::uint32_t b0() {
        mf_ = mf_ * 1103515245u + 12345u;  // u32 wrap = mod 2^32
        return mf_ & 0x7FFFFFFFu;          // %2147483648
    }
    // Rk.jf (L2352).
    double jf() {
        const double b1 = static_cast<double>(b0()) / 2147483648.0;
        const double b2 = static_cast<double>(b0()) / 2147483648.0;
        return b1 + b2 / 2147483648.0;
    }
    // Rk.s4 / Rk.dT / Rk.cT (L2352).
    double s4(double a) { return jf() * a; }
    double dT(double a, double b) { return a + s4(b - a); }
    bool cT(double a, double b = 100.0) { return s4(b) < a; }

private:
    std::uint32_t mf_;  // Xx.mf
};

// ---------------------------------------------------------------------------
// Tactics file parsing (JS `sb` L648-653 + `Si` L653-655)
// ---------------------------------------------------------------------------

// One distance-window outcome: `{anim, float window-edges[], u32
// window-outcomes[]}` (JS pool0 elem). At decision time the AI compares
// the (adjusted) distance against `edges`, picks the window index
// (first edge > distance), and takes `window_outcomes[window-1]`; >0 ->
// the outcome animation id joins the candidate list (JS Q6a/XAa).
struct TacticOutcome {
    std::string anim;                // pool0 anim id (record/outcome animation)
    std::vector<float> window_edges; // distance-window boundaries (float pool)
    std::vector<std::uint32_t> window_outcomes;  // u32 pool (outcome ids)
    int hu_index = 0;  // which Hu frame of the Ju row this outcome came from
                       // (JS `Ju.frames[k]`; needed for the `$_` frame pick)
};

// One condition row inside a table record (JS vec28B + vec12 groups).
struct TacticRow {
    std::string label;     // vec28B cstr label (JS `Ju.label`, g="EF")
    int rda = 0;           // JS `Ju.Rda`: the row's base frame for `$_`
    int hu_frames = 0;     // JS `Ju.frames.length` (Hu frame count)
    // The outcome cases (JS `Ju.frames` -> Hu -> Gu outcome rows).
    std::vector<TacticOutcome> outcomes;
};

// JS `Ju.$_` frame pick (L648-649): `$_ (a){a-=Rda; return a<frames.length
// ? a : -1}` — the Hu frame active at enemy frame Fl, or -1. (Negative
// Fl-Rda would index undefined in JS — crash, so Fl>=Rda is assumed and
// the port guards k>=0 all the same.)
inline int ju_frame_index(int fl, int rda, int hu_frames) {
    const int k = fl - rda;
    if (k < 0 || k >= hu_frames) return -1;
    return k;
}

// One table record (JS Il = "TacticRecord"): the animation context +
// per-weapon-type branch + rows.
struct TacticRecord {
    std::string anim;                    // record animation id (pool A)
    std::string weapon;                  // pool B weapon-type branch
    std::vector<TacticRow> rows;         // condition rows (vec28B)
};
// The parsed tactics structure for ONE weapon pair / version (JS `sb`).
// `tables[0]` = safe-attack table (Q6a/`Z0()[1]`), `tables[1]` =
// attack table (XAa/`Z0()[0]`), `tables[2]` = throw table (Gea/`Z0()[2]`).
struct TacticsSet {
    bool empty() const {
        return tables[0].empty() && tables[1].empty() && tables[2].empty();
    }
    std::vector<TacticRecord> tables[3];
};

// Decompress + parse ONE tactics .dat file (a single-weapon or weapon-pair
// archive; JS `Si.cxb` L653-654). `data`/`size` = the RAW COMPRESSED file
// bytes. Returns the parsed set keyed by weapon pair "(a,b)" with the
// version, or throws std::runtime_error on malformed input.
struct TacticsFile {
    std::string weapon_a;   // first weapon (may be "" = unarmed)
    std::string weapon_b;   // second weapon
    int version = 0;        // table version (0/1/2/7; 2=single, 7=per-anim)
    TacticsSet set;
};
std::vector<TacticsFile> tactics_parse_file(const std::uint8_t* data,
                                            std::size_t size);

// ---------------------------------------------------------------------------
// Tactic settings (tactic_settings.xml) — JS `P` + `Md`
// ---------------------------------------------------------------------------

// One weight curve (JS `cc` g="EE"): the weighted-sum score + the
// Linear/Exponential normalization. Fields map to the XML attributes
// Base/CounterFactor/DamageFactor/.../Limit/AntiLimit/Shift/FactorType.
struct WeightCurve {
    float base = 0.0f;
    float counter_factor = 0.0f;
    float damage_factor = 0.0f;
    float health_factor = 0.0f;
    float enemy_health_factor = 0.0f;
    float anim_frames_factor = 0.0f;
    float child_frames_factor = 0.0f;
    float magic_bullet_factor = 0.0f;
    float missile_bullet_factor = 0.0f;
    float hit_factor = 0.0f;
    float distance_factor = 0.0f;
    float limit = 0.0f;      // Limit (upper bound)
    float anti_limit = 0.0f; // AntiLimit (lower bound)
    float shift = 0.0f;      // Shift
    float conditional_factor = 0.0f; // ConditionalDesigionFactor
    bool linear = true;      // FactorType: Linear=1 (default), Exponential=0

    // Per-animation-factor children: (anim name, weight) pairs.
    // JS `cc.BM` -> `AnimationFactors Animation=.. DamageFactor=..`.
    struct AnimFactor {
        std::string anim;
        float counter = 0.0f;
        float damage = 0.0f;
        float hit = 0.0f;
    };
    std::vector<AnimFactor> anim_factors;
};

// The fighter-state vector fed to the weight curves (JS `Ue` g="ED" +
// `mQ` L620): counters, HP ratios, animation frames, bullet counts, ...
// The native AI fills it from the fight state each frame.
struct AiFeatureState {
    float counter = 0.0f;      // `counter`
    float xb = 0.0f;           // `Xb` (damage counter)
    float tf = 0.0f;           // `tf` (hits counter)
    float o1 = 0.0f;           // `o1` — my ABSOLUTE hp (`parameters.gd`, not a ratio)
    float q1 = 0.0f;           // `q1` — enemy ABSOLUTE hp
    float xY = 0.0f;           // `xY` — enemy animation frame (`kJ()`; port: move frame)
    float cl = 0.0f;           // `cl` — my magic bullets
    float k2 = 0.0f;           // `K2` — ranged flag (±1)
    float pz = 0.0f;           // `pZ` — enemy's highest body-part frames
    float lya = 0.0f;          // `Lya` — distance between NPivot bones
    float shift = 0.0f;        // `Fk` — the curve's Shift term
    // My/enemy current animation names (for the per-anim weight lookups).
    std::string my_anim;
    std::string enemy_anim;
    // Conditional decision flag (`zZ` — whether a ConditionalDecision fired).
    bool conditional = false;
};

// Evaluates one weight curve against the feature state (JS `cc.Gb` L647 +
// `NYa`/`QYa` L648). `curve` is the parsed curve, `f` the feature state.
float weight_curve_eval(const WeightCurve& curve, const AiFeatureState& f);

// One QuickAttack / Evade slot (JS `Hl` g="E6" + the `$E`/`nD` lists):
// animation names + priority + conditions (all must pass) + the chance
// curve (JS `cc` — the slot fires when `roll < score`).
struct AiAnimSlot {
    std::vector<std::string> names;  // `names` ("|" separated)
    int priority = 0;                // `priority`
    WeightCurve chance;              // the slot's chance curve (`cc`)
    std::vector<Cond> conditions;    // `$c` (Conditions tree)
};

// One tactic definition (JS `Md` g="EB") parsed from a <Tactic> element.
struct TacticDef {
    std::string name;
    int type = 0;  // Md.getType: 0=Normal, 1=Random, 2=Tabular

    // AnimationWeights (JS `$oa`) — the roulette pick weights.
    std::vector<std::pair<std::string, WeightCurve>> anim_weights;

    // UseDefense (JS `Tpa/lqa/spa`): CounterAttack/Dodge/Block chance curves.
    WeightCurve counter_attack_chance;
    WeightCurve dodge_chance;
    WeightCurve block_chance;
    // UseSafeAttackChance / TableAttackChance (JS `sua`/`vO`).
    WeightCurve use_safe_attack_chance;
    WeightCurve table_attack_chance;
    // DodgeMissilesChance / DodgeMagicChance (JS `qqa`/`nqa`).
    WeightCurve dodge_missiles_chance;
    WeightCurve dodge_magic_chance;
    // CautiousMovementsChance (JS `Apa`).
    WeightCurve cautious_movements_chance;

    // QuickAttacks (JS `$E`) + Evades (JS `nD`) slots.
    std::vector<AiAnimSlot> quick_attacks;
    std::vector<AiAnimSlot> evades;

    // ExpectedWait (JS `x8`) — the per-anim "wait after this move" curves.
    std::vector<std::pair<std::string, WeightCurve>> expected_wait;

    // DistanceError / FrameError / ResponseDelay / EnemyResponseDelay
    // (JS `Mu`/`lN`/`z$`/`v8`).
    WeightCurve distance_error_min, distance_error_max;
    WeightCurve frame_error_min, frame_error_max;
    WeightCurve response_delay_min, response_delay_max;
    WeightCurve enemy_response_delay_min, enemy_response_delay_max;
};

// Parses tactic_settings.xml into the named TacticDefs (JS `P.hkb` L629 +
// `Md` L636-643). Throws std::runtime_error on malformed XML.
void parse_tactic_settings(const std::string& xml_text,
                           std::map<std::string, TacticDef>& out);

// ---------------------------------------------------------------------------
// The AI controller (JS `de` L589-621)
// ---------------------------------------------------------------------------

// One candidate move the AI is considering (JS `kd` g="E3"):
// `animation` + the wait frames before it starts.
struct AiCandidate {
    std::string animation;
    int wait = 0;
};

// The per-frame fight snapshot the AI reads (JS `Ue` + `mQ` L620).
struct AiFightState {
    // My fighter (the one being controlled).
    const MoveDef* current_move = nullptr;   // `da.Ua`
    int move_frame = 0;                      // `Te.Xh` (playback frame)
    int move_len = 0;                        // `jc.Lj` (end frame)
    float my_hp = 0.0f, my_max_hp = 1.0f;    // `parameters.gd` / `Zn`
    float enemy_hp = 0.0f, enemy_max_hp = 1.0f;
    float my_x = 0.0f;                       // `oa.Fe().ma.x`
    float enemy_x = 0.0f;
    float my_y = 0.0f;
    int my_facing = 1;                       // `da.hd()`
    int enemy_facing = 1;
    // My / enemy current animation names (`da.Ua.name`).
    std::string my_anim;
    std::string enemy_anim;
    // The ENEMY's current move (JS `de.ia(a)` receives the opponent; the
    // AI reads `a.da.Ua` = the enemy's current animation).
    const MoveDef* enemy_move = nullptr;
    int enemy_move_frame = 0;
    // Active intervals on my fighter (name -> type) (`da.xj` / `P0()`).
    std::vector<std::pair<std::string, int>> my_intervals;
    // Enemy's highest body-part animation frame (`Tba` L595: max over
    // `vd` parts of `da.M2`).
    int enemy_max_part_frames = 0;
    // Ranged flag (`K0`): +1 if I have ranged, -1 otherwise.
    int ranged = -1;
    // Magic bullets (`bh`).
    int magic_bullets = 0;
    // My / enemy body-part frames (JS `vd` bone anim frames) — used for
    // the fCa/V1 body checks.
    std::vector<int> my_part_frames;
    std::vector<int> enemy_part_frames;
    // Frame counter (`ca.frame` — the fight's frame).
    int fight_frame = 0;
    // Random source for the chance draws (injected; the native demo
    // supplies a std::mt19937). `roll01()` returns a value in [0,1).
    std::function<float()> roll01;
    // Cautious-movements condition: whether the enemy is playing a
    // cautious animation (JS `fCa` — body-part anim in `P.nG`).
    bool enemy_cautious = false;
};

// The AI controller (JS `de`).
class AiController {
public:
    // Builds the controller. `weapon` = my weapon subtype ("" = unarmed);
    // `tactics` = the parsed tactics sets for the fight (all versions +
    // weapon pairs); `tactic` = the chosen tactic settings (by name).
    // `moves` = the full move map (for the move lookup + condition eval).
    void init(const std::string& weapon,
              const std::vector<TacticsFile>& tactics,
              const TacticDef* tactic,
              const std::map<std::string, MoveDef>* moves);
    // The per-frame decision (JS `de.ia` L592-594). Returns the chosen
    // move name, or "" when no move should start this frame.
    std::string update(const AiFightState& st);
    // Seeds the owned DaPrng (JS `Da.IT`/`L.web` L67-68: `pg=new Rk(seed)`).
    // Without an explicit roll01 override, ALL draws (QJa, dqb, jL, slots)
    // come from this stream in JS call order.
    void set_seed(std::uint32_t seed) { prng_.seed(seed); }

    // The candidate list from the last decision (for logging; JS `wb`).
    const std::vector<AiCandidate>& candidates() const { return wb_; }
    // The decision stage id from the last pass (JS `fk`): -1 none,
    // 0 TableAttack, 1 SafeAttack, 2 QuickAttack/Evade, 5 Cautious,
    // 6 AnimationWeights, 9 EvadeThrow, 10 TableAttack-Default, 11 = wait.
    int last_stage() const { return fk_; }
    // The facing decision (JS `b6a` L603 + `nzb` L390): the sign of
    // (enemyX - myX), or 0 when unknown.
    int facing(const AiFightState& st) const;

    // The fighter's feature state from the last decision (for logging).
    const AiFeatureState& features() const { return feat_; }

private:
    std::string weapon_;
    const TacticDef* tactic_ = nullptr;
    const std::map<std::string, MoveDef>* moves_ = nullptr;
    // Registered tactics (JS `P.wO[version][(a,b)]`).
    std::vector<TacticsFile> tactics_;
    // The `OO` tactics id for my weapon pair (JS `P.dBa` L629-630).
    std::string oo_;

    // Per-frame state (JS `de` fields).
    std::vector<AiCandidate> wb_;   // the working candidate list (`wb`)
    std::vector<AiCandidate> ld_;   // the final move list (`ld`)
    std::vector<int> vs_;           // per-candidate waits (`vs`)
    int fk_ = -1;                   // decision stage (`fk`)
    int eh_ = 1;                    // the wait counter (`eh`)
    int oC_ = 0;                    // the interval-wait state (`oC`)
    bool pH_ = false;               // `pH` — the "watching" state
    bool F8_ = false;               // `F8` — the interval-wait state
    bool mW_ = false;               // `mW` — the "re-evaluate after wait" flag
    bool XW_ = false;               // `XW` — the surprise/random flag
    bool IB_ = false;               // `IB` — the "enemy throws" flag
    bool Ao_ = false;               // `Ao` — the frame-step flag
    int Fl_ = 0;                    // `Fl` — my animation frame (physics)
    int q7_ = 0;                    // `q7` — enemy animation frame
    double Mu_ = 0.0;               // `Mu` — DistanceError draw (yea, L640-641, untruncated)
    int lN_ = 0;                    // `lN` — FrameError draw (j0|0, L640-641)
    // QJa roll cache (JS `QJa` L594-595, refreshed on enemy move change by
    // `jwb` L596-597): a discarded `Da.jf()` + five cached rolls, compared
    // against per-pass evaluated curves in `ia` L593-594 (`rua=tua<qPa` etc.).
    double tua_ = 0.0, dua_ = 0.0, bpa_ = 0.0, rqa_ = 0.0, oqa_ = 0.0;
    int x_ = 0;                     // `$x` — ResponseDelay cache (`gfa+1`, set in jwb)
    int aea_ = 0;                   // last EnemyResponseDelay draw (`Aea`, per XAa call)
    std::string last_enemy_anim_;   // enemy-move-change detector (anim-name proxy)
    bool qja_done_ = false;
    mutable DaPrng prng_;           // owned stream (JS `Da.pg`)
    int aqa_ = 1;                   // `aqa` — the distance category (dqb)
    // The chance curves' evaluated scores (JS `CZ/bda/tba` from dqb).
    int CZ_ = 0, bda_ = 0, tba_ = 0;
    float CZ_f_ = 0.0f, bda_f_ = 0.0f, tba_f_ = 0.0f;
    // The per-frame curve scores + decided flags (JS `csb`/`bsb`/`Yqb`).
    std::vector<float> quick_scores_, evade_scores_;
    std::vector<bool> quick_decided_, evade_decided_;
    std::vector<float> stage_scores_;  // UseSafeAttack/TableAttack/Cautious/...
    std::vector<bool> stage_decided_;

    // The per-frame feature state (JS `Ue` + `mQ`).
    AiFeatureState feat_;

    // --- helpers (JS de methods) ---
    // Default source: the owned DaPrng (`Da.pg.s4(1)`); tests/demos may
    // inject an override (ai_demo uses mt19937 — then only the QJa/gfa/aea
    // draws stay on the exact stream; full JS-stream parity needs
    // set_seed() + no override).
    float roll01() const {
        if (roll01_fn_) return roll01_fn_();
        return static_cast<float>(prng_.s4(1.0));
    }
    std::function<float()> roll01_fn_;

    // Builds `feat_` from the fight state (JS `mQ` L620).
    void mq(const AiFightState& st);
    // The distance category (JS `dqb` L600): 1 = far, 2 = CounterAttack
    // range, 3 = Dodge range, 4 = Block range.
    int dqb(const AiFightState& st);
    // Finds the tactics table record whose weapon matches mine AND whose
    // anim matches the enemy's current animation (JS: the Il record whose
    // `Tfa` == `OO` inside `ds.Z0()[n]`).
    const TacticRecord* find_record(const std::string& enemy_anim) const;
    // The facing (JS `b6a` L603).
    int b6a(const AiFightState& st) const;
    // Whether the fighter is "watching" (JS `hcb` L598-599): no active
    // NoDecision intervals/moves and not in a NoDecision enemy anim.
    bool hcb(const AiFightState& st) const;
    // The unconditional-move check (JS `Pqb` L604): returns >0 when a
    // move must start this frame.
    int pqb(const AiFightState& st);
    // Fills `wb_` from the safe-attack table (JS `YAa` L608 + `Q6a`
    // L609-610).
    int yaa(const AiFightState& st);
    // Fills `wb_` from the attack table (JS `XAa` L611-612).
    int xaa(const AiFightState& st);
    // Fills `wb_` from the throw table (JS `Gea` L613-616).
    int gea(const AiFightState& st, int variant);
    // The per-slot QuickAttack/Evade evaluate (JS `Nwa` L603 + `bqb`).
    int nwa(const std::vector<AiAnimSlot>& slots, const std::string& anim);
    // The dodge candidate list (JS `VAa` L617).
    void vaa(const AiFightState& st, int variant);
    // The weighted roulette pick (JS `Md.jL` L640).
    int pick(const std::vector<AiCandidate>& cands) const;
    // Filter the candidate list by the V1 move test (JS `ABa` L600).
    void filter_by_v1(std::vector<AiCandidate>& cands,
                      const AiFightState& st) const;
    // Tests one candidate move (JS `V1` L601-602).
    bool v1(const MoveDef& m, const AiFightState& st) const;
    // The `fCa` body-part cautious check (JS `fCa` L599-600).
    bool fca(const AiFightState& st, int variant) const;
    // Rebuilds the QJa roll cache (JS `QJa` L594-595): five `Da.jf()`
    // draws + `Mu`/`lN` (yea/j0) + `$x` (gfa). Called on enemy move
    // change (JS `jwb` L596-597, routed by `mwb`; the port detects the
    // change by enemy anim name — documented proxy).
    void qja(const AiFightState& st);
    // JS `de.gfa` (L597: `Gc.gfa(a)+1`, `G` = `Md.I0(z$)` L640-643).
    int gfa_draw() const;
    // JS `de.Aea` (L597: `Gc.Aea(a)`, `Md.I0(v8)` L640-643, truncated).
    int aea_draw() const;
};

} // namespace sf2::scene

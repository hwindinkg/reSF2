#pragma once

// Condition evaluator for the move system (JS classes Ha..ym, g="125".."134").
//
// The game evaluates a move's <Conditions> tree against the fighter's live
// state (the `Ae` class, g="13F"): current animations, active intervals,
// round stage, keys pressed, mods, items, distance to the enemy, etc.
// Every condition type in the JS switch `ec.create` / `Tl.create` is
// implemented below with EXACT JS semantics; see the per-type comments.
//
// The native port models the state it reads as `FightContext`, and evaluates
// the tree with `eval_conditions` (JS `Ha.he` + `Ha.Nba` Not handling +
// Operator And/Or from `wm.gEa`).

#include <map>
#include <set>
#include <string>
#include <vector>

#include "scene/move_def.hpp"

namespace sf2::scene {

// Key types (JS `sa.$h`): Punch=9, Kick=10, Ranged=11, Magic=12, ...
enum class key_type : int {
    up = 1, up_forward = 2, forward = 3, down_forward = 4, down = 5,
    down_back = 6, back = 7, up_back = 8,
    punch = 9, kick = 10, ranged = 11, magic = 12, raid_charge = 13,
    super = 14,
};

// Press type of a buffered key (JS `vm` Keys condition: Tap/Hold/Release).
enum class press_type : int {
    tap = 1, hold = 2, release = 3,
};

// One buffered input.
struct key_input {
    key_type key;
    press_type press;
};

// Round stages (JS `iz.XBa`): 1=StartStance, 2=Fight, 3=EndStance, 4=PeacefulStart,
// 5=ShopPurchase, 6=PeacefulRestore, 7=TryOn, 0=unknown.
enum class round_stage : int {
    unknown = 0, start_stance = 1, fight = 2, end_stance = 3,
    peaceful_start = 4, shop_purchase = 5, peaceful_restore = 6, try_on = 7,
};

// Fighter state the conditions read (JS `Ae` fields):
//   XH/z_/G3/oZ/A_ = animation name lists per player slot (lg.vQ)
//   xb = active intervals (tm.he)
//   Je = round stage (Em.he)
//   keys = buffered inputs (vm.he)
//   Eib/F3a = "items" lists for Item/Weapon conditions (um/Hm.he)
//   aK = fighter name (Am.he), rr = model ref for Perk/ModelExists
//   IL = last hit info (sm.he Hit condition)
//   EA/lz = perk lists (Bm.he Perk condition)
//   Xda = physics frame number (Cm.he)
//   qb = "is mirrored" flag (Dm.he ModelMirrored)
//   a.zda = round timer (Fm.he RoundResult)
//   ul = screen (Gm.he Screen), cl/Glb = bullets (om.he Bullets)
//   To = battle type (lm.he BattleType), state = boss ability state (nm.he)
//   yDa/zDa = current/param health (rm.he Health)
//   Oga/BEa/CEa = physics flags per player (lg.d7a)
//   Mla/Nla = enemy facing/direction (pm.he Direction)
//   sign = player facing (pm.he)
//   Wl = player scale (qm.he Distance)
struct FightContext {
    // --- animation names currently active on the fighter/player slots ----
    std::vector<std::string> anims_me;     // XH (slot 1) — "my" animations
    std::vector<std::string> anims_enemy;  // z_ (slot 2) — enemy animations
    std::vector<std::string> anims_other;  // G3 (slot 3) — third party
    std::vector<std::string> anims_fourth; // oZ (slot 4)
    std::vector<std::string> anims_sixth;  // A_ (slot 6)
    // The candidate move being evaluated ($Move in CurrentAnimation). The
    // game sets `Ae.xK` to the candidate's animation-name list while testing
    // (wd.V1: `c.xK=a.xl`).
    std::vector<std::string> candidate_moves;

    // --- active intervals on this fighter (JS `Ae.xb`) -------------------
    // name -> (type, active).  Keyed by interval Name where present, else
    // by type string.  JS tm.he: `this.uc==0||this.uc==d.type` and
    // `this.Ba==""||d.name==this.Ba` — either Name or Type can match.
    struct interval_state {
        std::string name;  // Interval Name attr (may be "")
        int type = 0;      // fe.G0 type
        bool active = false;
    };
    std::vector<interval_state> intervals;

    // --- round stage (JS `Ae.Je`, iz.XBa) --------------------------------
    round_stage stage = round_stage::unknown;

    // --- buffered keys (JS `Ae.keys` = zd) -------------------------------
    std::vector<key_input> keys;
    bool keys_gm = false;  // `Ae.gm`: "hold mode" — if set, only Held keys match

    // --- items (Weapon/Item/Player conditions) ---------------------------
    // Each item: (type, subtype, name).
    struct item_info {
        std::string type;
        std::string subtype;
        std::string name;
    };
    std::vector<item_info> items;            // my items (um.he/Hm.he)
    std::vector<item_info> items_enemy;      // enemy items (Hm.he Enemy slot)
    std::vector<std::string> fighter_names;  // "me" fighter names (Am.he)

    // --- mods present (ModExists, tp) ------------------------------------
    std::set<std::string> mods;

    // --- perks (Bm.he Perk condition) ------------------------------------
    // The game keeps EA (my perks) and lz (enemy perks); each perk has an
    // action name + owning perk name.
    struct perk_info {
        std::string action_name;
        std::string perk_name;
    };
    std::vector<perk_info> perks_me;
    std::vector<perk_info> perks_enemy;

    // --- distance to enemy (qm.he Distance) ------------------------------
    // Precomputed distance between the From/To object refs. JS computes it
    // per axis from the two `ee` object refs; the native context fills the
    // 3 values (dx, dy, dist3d) and the evaluator picks by `axis`.
    float dist_x = 0.0f;   // signed X delta (enemy - me, in scale units)
    float dist_y = 0.0f;   // Y delta
    float dist_3d = 0.0f;  // Euclidean 2D distance (JS qm case 2)
    float scale = 1.0f;    // `Ae.Wl` (fighter scale, multiplies X distance)

    // --- health (rm.he) ---------------------------------------------------
    float health_ratio = 1.0f;  // yDa / zDa (current/max)

    // --- physics frame number (Cm.he) -------------------------------------
    int physics_frame = 0;

    // --- misc (not used by moves.xml but present in the JS) --------------
    bool model_mirrored = false;  // Dm.he ModelMirrored
    int round_timer = 0;          // Fm.he RoundResult (zd/Jq = Victory/Defeat)
    bool round_victory = false;   // Fm.zd — "victory round" flag
    int screen = 0;               // Gm.he Screen (0=Fight,10=...)
    int bullets_me = 0;           // om.he Bullets (cl)
    int bullets_enemy = 0;        // om.he Bullets (Glb)
    std::string battle_type;      // lm.he BattleType ("FightNone" default)
    bool boss_ability_state = false;  // nm.he BossAbilityState (Value)
    std::string fighter_name;     // Am.he Name — the fighter's model name
    // Hit condition (sm.he): type + hit animation name of last hit.
    std::string last_hit_type;      // "Critical"/"Shock"/"" — matches by name
    std::string last_hit_animation; // hit animation name
    bool has_last_hit = false;

    // Helpers.
    bool interval_active(const std::string& name, int type = 0) const;
    bool key_pressed(key_type k, press_type p) const;
    bool has_mod(const std::string& name) const;
    bool has_item(const std::string& type, const std::string& subtype,
                  const std::string& name, bool enemy = false) const;
    bool has_perk(const std::string& action_name, const std::string& perk_name,
                  bool enemy = false) const;
    bool has_animation(const std::string& anim, int slot = 1) const;
};

// Evaluate a condition tree against a context. Returns true iff the move
// passes. Mirrors JS: `Ha.Nba` (Not flip) + `wm.gEa` (And/Or short-circuit).
bool eval_conditions(const Cond& cond, const FightContext& ctx,
                     std::string* trace = nullptr, int depth = 0);

// Convenience: evaluate a whole move's <Conditions> list (JS `jc.nw`).
bool eval_move_conditions(const std::vector<Cond>& conds,
                          const FightContext& ctx, std::string* trace = nullptr);

// Debug: one-line human description of a condition node (for the probe trace).
std::string cond_desc(const Cond& c);

} // namespace sf2::scene

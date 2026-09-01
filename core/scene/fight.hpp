#pragma once

// Fight controller: rounds, phases, timer, round-end and the fight HUD.
// Ported from the game's `ca` class (sf2.502f0946.js L379-433) — see
// core/scene/README.md "Fight controller (Phase 3.5)" for the full JS
// study with line refs. The controller turns the pieces (fighter/moves/
// physics/AI) into a complete fight:
//
//   - round flow:  round init (tx L407: timer = round length, Vt=false)
//                  -> phase 1 StartStance (FNa L409 -> xF(1))
//                  -> phase 2 Fight  (Rkb L410 -> xF(2) + HUD play())
//                  -> phase 3 EndStance (E3a L412 -> i4a L409 -> xF(3))
//   - phase machine: `eu` 0=idle 1=StartStance 2=Fight 3=EndStance; each
//     fighter's `Je` stance is synced by xF (L388) so the move conditions'
//     RoundStage gate matches the fight phase.
//   - round end (Onb L411): KO / timeout / (ringout via fight rules).
//     KO  -> the HIGHER-HP fighter wins the round (vfa L413);
//     timeout (round.time >= round.eL) -> the ENEMY wins (E3a c==3
//     branch: `a.ng++` on Zb). The winner's `ng` (rounds won) increments;
//     the round ends with a 3-phase result (round_result + winner + loser).
//   - battle end: when the winner's `ng >= round.eL` (Rounds), or after a
//     KO in the final round, the battle ends (`bea` L413); `winner()` is
//     exposed for the results screen.
//   - HP recovery: `NA` (L414) heals BOTH fighters by `Da.qDa`
//     (HealthRecovery, stages.xml default 1) between rounds — NOT a full
//     reset (the round-2 fighters keep their damaged HP + the recovery).
//   - the timer: HUD display decrements 1/sec from gma (RoundTime) —
//     `Sf.iPa` (L2035): `NF = xU/60` where xU = gma*60+1. The fight
//     round.time advances during phase 2 (this port), matching the HUD
//     countdown; the round ends at round.time >= round.eL (Rounds).
//   - the HUD: `Ar`/`Sf` (L2016-2040) — HealthBar_Full/Empty/Hit bars,
//     the digits.fnt timer, Round_Done/Undone indicators, the FIGHT!/
//     Round labels (fight/ui atlas).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "anim_archive.hpp"
#include "atlas.hpp"
#include "render/gl_types.hpp"
#include "scene/ai.hpp"
#include "scene/damage.hpp"
#include "scene/fighter.hpp"
#include "scene/move_def.hpp"
#include "scene/physics.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"

namespace sf2::scene {

// The battle parameters (JS `Da`, L1418-1424): the fight's config read from
// the stages.xml <Fight> element.
struct BattleParams {
    std::string name;         // "Training" / "BOSS_LYNX"
    std::string type;         // "FightNone" / "FightTutorial" / "FightBosses"...
    std::string location;     // "dojo" / "moon" / ...
    int rounds = 2;           // `pT`  <Fight Rounds="2"> — the ROUNDS-TO-WIN
                              // (the fight ends when a fighter wins this many)
    int round_time = 99;      // `R4`  <Fight RoundTime="99"> (seconds) — the
                              // HUD countdown (JS `$t.gma` / `Sf.iPa`)
    float health_recovery = 1.0f;  // `qDa` <Battle HealthRecovery=".."> (default 1)
    bool timeout_rule = false;     // `ERuleTimeoutWin` — when set, a round ends
                              // when the HUD timer reaches 0 (JS `BT` sets
                              // `ey=2` for the TimeoutWin rule). The shipped
                              // stages use NO timeout rule — rounds end on KO.
    // Fight-level spawn positions (the game reads location.Yia/B_; the
    // demo supplies the dojo ModelsViewer positions).
    float player_spawn_x = 690.0f, player_spawn_y = -93.0f;
    float enemy_spawn_x = 973.0f, enemy_spawn_y = -110.0f;
    int max_hp = 100;         // demo HP cap (the game stores HP in the save)
    // Demo damage tuning: the player's UnarmedDamage attr (the JS balance
    // formula: 2^((attr+shift-defense)/10) × base). The shipped warriors
    // carry UnarmedDamage=2 → ~0.055/hit vs the 1-HP fallback, so a KO
    // would take ~18 hits; the demo raises it so the fight completes fast
    // while keeping the exact bCa formula.
    float player_unarmed_damage = 0.0f;
};

// The live round state (JS `$t` L1239).
struct RoundState {
    int number = 0;       // `round` — the current round number (0 before the
                          // first Z2; increments to 1..Rounds)
    float time = 0.0f;    // `time` — the fight timer (seconds elapsed; the
                          // HUD countdown = gma - time)
    int length = 0;       // `eL` — Da.pT = Rounds: the ROUNDS-TO-WIN
                          // threshold (JS `Onb` L411: `nB.ng >= round.eL`
                          // ends the battle)
    int gma = 60;         // `gma` — the HUD countdown seconds (Da.R4)
    bool running = false; // `Vt` — the fight timer is running (phase 2)
};

// The fight phases (JS `ca.eu` 0-3).
enum class fight_phase : int {
    idle = 0,          // `eu` 0 — no fight (bob's xF(0) before tx)
    start_stance = 1,  // `eu` 1 — StartStance (fighters at spawn, no input)
    fight = 2,         // `eu` 2 — the round is live
    end_stance = 3,    // `eu` 3 — EndStance (results)
};

// How a round ended (JS `ey` 0-6; the demo fight uses KO=0 and
// TimeoutWin=2).
enum class round_result : int {
    ko = 0,          // `ey` 0 — a fighter was KO'd (hp <= 0)
    points = 1,      // `ey` 1 — points rule (unused here)
    timeout_win = 2, // `ey` 2 — timeout/ringout rule
    defeat = 3,      // `ey` 3 — the player lost (PVP/rule); the ENEMY wins
    ringout = 4,     // `ey` 4 — ringout rule
    survival = 5,    // `ey` 5 — survival rule
    pvp = 6,         // `ey` 6 — PVP HP comparison
};

// The per-fighter live state the fight controller owns (JS: the `wd`
// fighter + its `parameters` + the AiController).
struct FightFighter {
    std::string name;         // fighter display name ("Player"/"Enemy")
    bool is_player = false;   // `parameters.qb`
    sf2::scene::Fighter fighter;
    sf2::scene::BodyState body;
    sf2::scene::FighterParams params;
    std::unique_ptr<sf2::scene::AiController> ai;  // null for a manual fighter
    float hp = 0.0f;          // `parameters.gd`
    float max_hp = 0.0f;      // `parameters.Zn`
    int rounds_won = 0;       // `parameters.ng` — rounds won
    bool is_winner = false;   // `zd` — the round/battle winner flag
    int hits_landed = 0;
    int hits_taken = 0;
    int moves_started = 0;
    std::string last_move;    // current move name (for the log/HUD)
    std::string last_decision;  // the AI's last decision (log)
    int last_ai_stage = -1;
    // The fighter's move list is built from the shared move map + weapon.
    std::vector<const sf2::scene::MoveDef*> hb;
};

// One finished round's result (for the demo log + the next-round flow).
struct RoundOutcome {
    round_result result = round_result::ko;
    int round_number = 0;     // the round that just ended
    const FightFighter* winner = nullptr;   // the round winner
    const FightFighter* loser = nullptr;    // the round loser (or KO'd)
    std::string reason;       // "KO" / "TIMEOUT" / "RINGOUT"
    float player_hp = 0.0f;   // end-of-round HP (pre-recovery)
    float enemy_hp = 0.0f;
    bool battle_over = false; // true when the battle ended with this round
};

// The one-line-per-second fight log entry (the demo prints these).
struct FightLogLine {
    int frame = 0;
    int phase = 0;
    int round = 0;
    int timer = 0;            // the HUD countdown (gma - round.time)
    std::string p_move, e_move;
    float p_hp = 0.0f, e_hp = 0.0f;
};

// The fight camera controller (JS `ma.Sya` L1833 + `ql.Ta`): centers the
// camera on the midpoint of the two fighters and zoom-fits the arena. The
// JS intro curve is a smoothed zoom/pan from the arena overview; the port
// keeps the fight-start framing (both fighters + a margin in view) and a
// simple linear intro. `framing()` returns the camera center/zoom for the
// current fighter positions.
struct FightCamera {
    float center_x = 0.0f;
    float center_y = 0.0f;
    float zoom = 1.0f;

    // Arena geometry (from the location params).
    float arena_w = 1960.0f;
    float arena_h = 560.0f;
    float wall = 80.0f;
    float floor = 80.0f;  // the visible arena floor line (world y)

    // Recomputes center/zoom from the two fighters' world x. Mirrors the
    // fight-start view (JS Sya: `f = min(viewW / (span + 100), 1)` — the
    // whole fight in view, zoomed out enough that the 100-px margin fits).
    // [FIX Phase 4b — dojo visible] center_y is chosen so the arena FLOOR
    // lands at ~0.61 of the view height, matching the oracle's fight view.
    // The old center_y=0 put the bright bg layers below the floor line and
    // the black mattes over the visible band ("dojo is black").
    void framing(float ax, float bx, float view_w, float view_h) {
        const float mid = (ax + bx) * 0.5f;
        const float span = std::fabs(bx - ax) + 500.0f;  // the demo margin
        center_x = mid;
        zoom = std::min(1.0f, view_w / span);
        // [FIX Phase 4b — fighters on the floor like the oracle] The
        // vertical center places the fighters' feet (the spawn floor line)
        // at ~0.78 of the view height — the oracle's boot2 shows the
        // fighters' feet at screen y≈559 (of 720). The old anchor (0.61)
        // put the feet too high and the fighters floated over the floor.
        const float floor_screen_y = view_h * 0.78f;
        const float vshift = ((arena_h / 2.0f - floor) / 2.0f) * (1.0f - zoom);
        center_y = floor + vshift - (floor_screen_y - view_h / 2.0f) / zoom;
    }
};

// The fight HUD (JS `Ar` L2016-2019 + `Sf` L2032-2040): HP bars, the round
// timer, the round indicators and the FIGHT!/Round labels. The port renders
// a FUNCTIONAL HUD — HP bars (HealthBar_Full/Empty/Hit), the timer digits
// and the round dots at the JS layout positions — with the fight/ui atlas.
// The exact per-pixel layout of the JS `layout()` (L2036-2037) is
// approximated: the player bar at the left, the enemy bar at the right,
// the timer centered, the round dots between the bars.
class FightHud {
public:
    // Builds the HUD. `atlas_frames` maps an atlas frame NAME to its rect
    // in texture pixels (from the fight/ui.json "filename" fields, e.g.
    // "HealthBar_Full"); `tex_w`/`tex_h` are the atlas texture size;
    // `digits_frames` maps a digit character ('0'..'9') to its frame rect
    // (from digits.fnt); `digits_tex` the digits atlas texture.
    void init(const std::map<std::string, sf2::data::Texture>& /*unused*/,
              const std::map<std::string, sf2::data::atlas_frame>& atlas_frames,
              float tex_w, float tex_h,
              const std::map<char, sf2::data::atlas_frame>& digits_frames,
              float digits_tex_w, float digits_tex_h,
              std::function<GLuint(const std::string&)> texture_lookup);

    // Per-frame HUD update: the timer countdown + the bar damage tween.
    // `running` is the round timer running flag (round.Vt); the countdown
    // decrements 1/sec (JS Sf.iPa L2035).
    void update(float dt, bool running);

    // Recomputes the layout from the view size (JS Sf.layout L2036-2037).
    void layout(float view_w, float view_h);

    // Renders the HUD through the renderer (screen-space).
    void render(sf2::render::Renderer& r);

    // The per-fighter HP + the fight state the HUD reads each frame.
    void set_hp(float player_hp, float player_max, float enemy_hp, float enemy_max);
    void set_round(int round_number, int rounds_total);
    void set_timer(int seconds);          // the displayed countdown
    void set_labels(const std::string& player_name, const std::string& enemy_name);
    void set_phase(fight_phase phase);    // shows FIGHT! / Round N labels

private:
    // One HP bar: the empty (bg), full (current) and hit (trailing) bars.
    struct HpBar {
        bool is_player = true;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float ratio = 1.0f;        // current HP / max
        float hit_ratio = 1.0f;    // trailing (damage) bar
        sf2::scene::Sprite empty, full, hit;
    };
    HpBar player_bar_, enemy_bar_;

    // Round indicators (Round_Done/Round_Undone dots).
    std::vector<sf2::scene::Sprite> round_dots_;

    // Timer digits + the FIGHT!/Round label.
    sf2::scene::Sprite timer_sprite_;      // the timer text (rendered to a
                                           // glyph strip by the demo)
    std::vector<sf2::scene::Sprite> timer_glyphs_;
    sf2::scene::Sprite label_sprite_;

    bool ready_ = false;
    std::function<GLuint(const std::string&)> texture_lookup_;
    std::map<std::string, sf2::data::atlas_frame> frames_;
    std::map<char, sf2::data::atlas_frame> digit_frames_;
    float tex_w_ = 0.0f, tex_h_ = 0.0f;
    float digits_tex_w_ = 0.0f, digits_tex_h_ = 0.0f;
    int timer_seconds_ = 0;
    int round_number_ = 0, rounds_total_ = 2;
    std::string player_name_ = "PLAYER", enemy_name_ = "ENEMY";
    fight_phase phase_ = fight_phase::idle;
    float view_w_ = 1280.0f, view_h_ = 720.0f;
};

// The fight controller (JS `ca` L379-433).
class FightController {
public:
    // Loads the fight: builds the two fighters, their move lists, the AI
    // controllers, and sets the spawn positions (JS `o1a` L403 + `ggb`
    // L383).
    void init(const BattleParams& battle,
              const sf2::scene::Model& model,
              const std::map<std::string, sf2::scene::MoveDef>& moves,
              const std::map<std::string, sf2::data::anim_clip>& clips,
              const std::vector<sf2::scene::TacticsFile>& tactics,
              const sf2::scene::TacticDef* tactic,
              const std::string& player_name,
              const std::string& enemy_name,
              float player_x, float player_y,
              float enemy_x, float enemy_y,
              int player_max_hp, int enemy_max_hp,
              std::function<float()> roll01);

    // Variant that builds the PLAYER's move list from its OWNED items via
    // the Locks test (JS `ra.Hza` L684-685 — `f.nw(d,b)` against the
    // fighter's items) instead of the TacticWeapon string. `owned` is the
    // player's (type, subtype) item list. The enemy stays TacticWeapon-
    // based ("Fists"). Used by the shop/equipment flow: equipping a weapon
    // adds the weapon's Locks-matching moves to the player's move list.
    void init_locks(const BattleParams& battle,
                    const sf2::scene::Model& model,
                    const std::map<std::string, sf2::scene::MoveDef>& moves,
                    const std::map<std::string, sf2::data::anim_clip>& clips,
                    const std::vector<sf2::scene::TacticsFile>& tactics,
                    const sf2::scene::TacticDef* tactic,
                    const std::string& player_name,
                    const std::string& enemy_name,
                    float player_x, float player_y,
                    float enemy_x, float enemy_y,
                    int player_max_hp, int enemy_max_hp,
                    std::function<float()> roll01,
                    const std::vector<std::pair<std::string, std::string>>& player_owned);

    // The arena bounds (wall / width-wall / floor) — set by the caller
    // (JS `ca.ggb` L383: v.tFa = location.NU, v.NKa = location.width - NU).
    void set_bounds(float wall, float wall_max, float floor_y);

    // Per-frame fight update (JS `ca.Ea` L385 -> `ia` L388). `dt` is the
    // fixed 60 Hz step (1/60). Runs the phase machine, the per-fighter
    // update (AI or input + move execution + physics), hit detection +
    // damage, the round-end checks and the round/battle transitions.
    void update(float dt);

    // The player's input (buffered into the player fighter; JS HUD buttons
    // -> `ca.cka`/`ca.U4`). The demo auto-attacks, so this is optional.
    void player_input(sf2::scene::key_type key, sf2::scene::press_type press);

    // Simple auto-attack driver (the game's `FightAuto` / the demo's
    // "simple auto-attack"): in phase 2, when no move is playing, the
    // player steps toward the enemy when far and punches when in reach.
    // Uses the same move-start path as the AI (bypasses the key buffer).
    void set_auto_attack(bool on) { auto_attack_ = on; }

    // [FIX Phase 4b — fighter color from the location] Sets the fill color
    // BOTH fighters' meshes are drawn with. The game's fighters are
    // silhouettes filled with the LOCATION's Root Color (dojo_params
    // `<Root Color="0x000000">`, JS `Na.cd`) — not a per-fighter team
    // color. The fight screen calls this with the loaded location's
    // root_color() after init_locks; the default is black.
    void set_fighter_color(std::uint32_t rgb) {
        fighter_color_ = rgb;
        player_.fighter.set_color(rgb);
        enemy_.fighter.set_color(rgb);
    }

    // --- fight state accessors -------------------------------------------
    const FightFighter& player() const { return player_; }
    const FightFighter& enemy() const { return enemy_; }
    int phase() const { return static_cast<int>(phase_); }
    const RoundState& round() const { return round_; }
    bool battle_over() const { return battle_over_; }
    // The battle winner (null until the battle ends).
    const FightFighter* winner() const { return winner_; }
    const std::vector<RoundOutcome>& round_history() const { return history_; }
    // The arena bounds.
    float wall_min() const { return wall_min_; }
    float wall_max() const { return wall_max_; }
    float floor_y() const { return floor_y_; }
    // The current camera framing (JS ma.Sya).
    const FightCamera& camera() const { return camera_; }
    // The fight HUD.
    FightHud& hud() { return hud_; }
    // The fight frame counter (JS `ca.frame`).
    int frame() const { return frame_; }
    // The last frame's per-second log line (frame % 60 == 0).
    const FightLogLine& last_log_line() const { return last_log_; }

private:
    BattleParams battle_;
    sf2::scene::Model model_;
    const std::map<std::string, sf2::scene::MoveDef>* moves_ = nullptr;
    const std::map<std::string, sf2::data::anim_clip>* clips_ = nullptr;
    std::vector<sf2::scene::TacticsFile> tactics_;
    const sf2::scene::TacticDef* tactic_ = nullptr;
    std::function<float()> roll01_;

    FightFighter player_;          // JS `kc` (params) + `yb` (fighter)
    FightFighter enemy_;           // JS `Zb` (params) + `pb` (fighter)
    // The fighter mesh fill color (the location Root Color; default black).
    std::uint32_t fighter_color_ = 0x000000u;
    RoundState round_;             // JS `round` ($t L1239)
    fight_phase phase_ = fight_phase::idle;  // JS `eu`
    int frame_ = 0;                // JS `ca.frame`
    bool battle_over_ = false;     // JS `xJ` (battle finished)
    const FightFighter* winner_ = nullptr;
    bool auto_attack_ = false;     // the demo's simple auto-attack driver
    bool round_live_ = false;      // JS `h9` — a round is in progress
    bool start_stance_done_ = false;  // phase 1 -> 2 gate
    int start_stance_frames_ = 0;  // phase 1 hold counter
    int end_stance_frames_ = 0;    // phase 3 hold (the FIGHT!/KO banner)
    std::vector<RoundOutcome> history_;
    FightLogLine last_log_;
    float wall_min_ = 80.0f, wall_max_ = 1880.0f, floor_y_ = 0.0f;
    FightCamera camera_;
    FightHud hud_;
    float time_since_log_ = 0.0f;

    // --- fight helpers (JS ca methods) ------------------------------------
    // JS `xF` (L388): set the fight phase + sync the fighters' stance.
    void set_phase(fight_phase p);
    // JS `tx` (L407): round init (timer = round length, Vt = false).
    void round_init();
    // JS `Z2` (L408-409): round start — increments the round counter,
    // re-syncs the fighters, enters phase 1.
    void round_start();
    // JS `FNa` (L409) / `Rkb` (L410) / `i4a` (L409): the phase transitions.
    void enter_start_stance();
    void enter_fight();
    void enter_end_stance();
    // JS `Onb` (L411): the round-end check (KO / timeout).
    void check_round_end();
    // JS `E3a` (L412): apply a round result (rounds-won, winner flags).
    void apply_round_result(round_result result, const FightFighter& winner,
                            const FightFighter& loser);
    // JS `bea` (L413): the battle end.
    void end_battle(const FightFighter& winner);
    // JS `NA` (L414): the between-round recovery (heal qDa, clear flags).
    void between_rounds_recover();
    // JS `vfa` (L413): the round winner by HP.
    const FightFighter& round_winner_by_hp() const;
    // The per-fighter update (AI / input + move execution + physics).
    void update_fighter(FightFighter& me, FightFighter& foe, float dt);
    // Builds one fighter (shared init helper). `weapon_subtype` selects the
    // TacticWeapon-based move list; `owned` (when non-empty) selects the
    // Locks-based list (JS `ra.Hza`).
    FightFighter make_fighter(const std::string& nm, bool is_player, float x, float y,
                              int max_hp, const std::string& weapon_subtype,
                              const std::vector<std::pair<std::string, std::string>>& owned);
    // The hit test (JS `ca.Enb` -> `wd.tKa` -> `Fu.ia`).
    bool hit_test(FightFighter& atk, FightFighter& def, const sf2::scene::MoveDef& move,
                  int frame, sf2::scene::HitCapsule& hit_cap,
                  sf2::scene::CapsuleHit& ch,
                  const sf2::scene::Interval*& hit_interval);
    // Applies a landed hit (damage + knockback; JS `ca.Cgb` L394-397).
    void apply_hit(FightFighter& atk, FightFighter& def,
                   const sf2::scene::MoveDef& move, const sf2::scene::Interval& iv,
                   const sf2::scene::HitCapsule& hit_cap, const sf2::scene::CapsuleHit& ch,
                   int frame);
    // Rebuilds a fighter's physics body from its current pose.
    void rebuild_body(FightFighter& f);
    // Samples the fighter's idle pose (JS: the weapon stance idle).
    void sample_idle(FightFighter& f);
    // The HUD countdown seconds (JS Sf.iPa: gma - round.time).
    int hud_timer() const;
};

} // namespace sf2::scene

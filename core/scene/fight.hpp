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
//   - round end (Onb L411): KO / timeout (TimeoutWin rule only).
//     KO -> the HIGHER-HP fighter wins the round (vfa L413);
//     `Ar.PEa` (L2020): `NF<=0` — the ENEMY wins (E3a c==3, L412-413).
//     The winner's `ng` (rounds won) increments.
//   - the timer: integer `xU` ticks (JS `Sf.iPa` L2036: `--xU`,
//     `NF = xU/60|0`, init `xU = gma*60+1` on reset, text `max(0,NF)`).
//     The fight decrements xU once per phase-2 tick while `Vt` (running);
//     the HUD and the timeout gate read NF.
//   - battle end: when the winner's `ng >= round.eL` (Rounds), or after a
//     KO in the final round, the battle ends (`bea` L413); `winner()` is
//     exposed for the results screen.
//   - HP recovery: `NA` (L414) heals BOTH fighters by `Da.qDa`
//     (HealthRecovery, stages.xml default 1) between rounds — NOT a full
//     reset (the round-2 fighters keep their damaged HP + the recovery).
//   - the timer: integer `xU` ticks (JS `Sf.iPa` L2036: `--xU`,
//     `NF = xU/60|0`, init `xU = gma*60+1` on reset, text `max(0,NF)`).
//     The fight decrements xU once per phase-2 tick while `Vt` (running);
//     the HUD and the timeout gate (`Ar.PEa`: `NF<=0`) read NF.
//   - the HUD: `Ar`/`Sf` (L2016-2040) — HealthBar_Full/Empty/Hit bars,
//     the digits.fnt timer, Round_Done/Undone indicators, the FIGHT!/
//     Round labels (fight/ui atlas).

#include <cstdint>
#include <cstdio>
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
#include "scene/effects.hpp"
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
    float player_spawn_x = 973.0f, player_spawn_y = -110.0f;
    float enemy_spawn_x = 690.0f, enemy_spawn_y = -93.0f;
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
    // The integer round timer (JS `Sf.iPa` L2036: `--xU`, `NF = xU/60|0`;
    // init `xU = gma*60+1` on reset; the text shows `max(0,NF)`). The HUD
    // and the timeout gate both read NF — the port's old float elapsed
    // timer had NO JS counterpart (`$t.time` is set once in `tx()` and
    // never incremented anywhere in the file).
    int time_xu = 0;      // `xU` — countdown ticks (60/sec)
    int time_nf = 0;      // `NF` — countdown seconds shown + gated
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

// The big center-screen banner (the game's FIGHT!/ROUND N/K.O. overlays —
// JS `Sf`'s round.png labels + the KO slow-mo). A pure PRESENTATION state:
// the banner machine never touches the fight simulation (the pose dump is
// byte-identical with or without it).
enum class banner_kind : int {
    none = 0,   // no banner
    round,      // "ROUND N" — the round-start intro
    fight,      // "FIGHT!" — the round goes live
    ko,         // "K.O." — a fighter was KO'd (with the slow-mo)
    victory,    // "VICTORY" — the player won the battle
    defeat,     // "DEFEAT" — the player lost the battle
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
    int timer = 0;            // the HUD countdown seconds (NF)
    std::string p_move, e_move;
    float p_hp = 0.0f, e_hp = 0.0f;
};

// The fight camera controller — an exact port of the JS camera chain
// `ql` (L362-371) + `Ut.Al` (L826-827) + `ma.Sya` (L1833-1835):
//
//   - focus: `tyb` (L363) sets the target to the midpoint of the two
//     fighters' Center-Of-Mass nodes (`wd.mea` -> `Dl.mea(a.Eu,b.Eu)`,
//     the native equivalent is the fighter world_x/world_y anchor);
//     `dZa` (L363-365) smooths the focus with the exact JS deltas:
//       $X = target - prevTarget (prediction),
//       bY = prevFocus + $X, aY = bY - focus, IO = (target - focus)*0.15,
//       LO = aY + IO; |LO| > 200 -> clamped to 200 (velocity clamp);
//       focus += LO; |focus - prevFocus| > 50 -> clamped to 50
//       (per-frame delta clamp).
//   - the camera starts at the fight spawn midpoint (`Lb.z9a` L475) and
//     chases the target — the intro ramp the oracle shows
//     (cy -101.5 -> -222 over the first ~60 frames). The VERTICAL target
//     is the arena-floor anchor (the oracle's CoM-mid for its dummy fight
//     settles on the dojo floor line; the native keeps the floor line at
//     0.78 of the view height — the value the pixel-diff verified).
//   - the panorama (`Ut.Al`): Io = arenaWidth/2 - focus is clamped to
//     +/-((arenaWidth - MaxWidthDelta)*Bj*0.5 - nC*0.5), applied to the
//     camera x (the native Io = arena_center_x - center_x).
//   - the zoom (`ma.Sya`): f = viewH / (arenaH*Bj), aspect clamp 0.45..1,
//     the narrow-screen extra clamp, the width-fit min(viewW/(span*f+100),1)
//     (NO +300 — xCa's +300 is the LAYER zoom), the min-zoom
//     0.6..1.3 `0.6+((clamp(c,0.5,1)-0.5)/0.5)*0.7` (BJ=1 at 16:9: -> 1.3),
//     and the portrait vertical shift round((viewH - e*f)/2)/f*0.5.
//
// `framing()` rebuilds center/zoom each fight frame. State lives in the
// struct so the smoothing runs continuously across frames (JS ql state).
struct FightCamera {
    // The smoothed focus (JS `Go.ma`) and the Sya zoom (JS `f`).
    float center_x = 0.0f;
    float center_y = 0.0f;
    float zoom = 1.0f;         // the RENDER zoom (JS `ma.Sya` f — the
                               // camera's tMa): 1.3 at 16:9 with a tight
                               // fighter span
    float zoom_layer = 1.0f;   // the LAYER zoom Bj (JS `Ut.Bj` = `xCa()`:
                               // min(nC/(span+300),1) — 1.0 at the fight
                               // start). This is what the oracle trace
                               // records as "zoom" (the trace hooks
                               // `Ut.Al`, which receives this.Bj), and what
                               // the panorama clamp (Ut.Al `d`) uses.

    // Arena geometry (from the location params).
    float arena_w = 1960.0f;    // the RAW location width (JS `Lb.width`)
    float arena_h = 560.0f;     // JS `Lb.height`
    float wall = 80.0f;
    float floor = 80.0f;        // the visible arena floor line (world y)
                                // (set_bounds feeds the dojo floor anchor)

    // --- JS `ql` dZa/tyb smoothing state (L362-365) ----------------------
    // Du/By = the current target (fighter midpoint), DO = the previous
    // target; Go/Jl = the smoothed focus, iq = the previous focus.
    bool initialized_ = false;
    float go_x_ = 0.0f, go_y_ = 0.0f;            // Go.ma (current focus)
    float go_prev_x_ = 0.0f, go_prev_y_ = 0.0f;  // iq/Go.mf (previous focus)
    float du_x_ = 0.0f, du_y_ = 0.0f;            // By/Du.ma (current target)
    float du_prev_x_ = 0.0f, du_prev_y_ = 0.0f;  // DO/Du.mf (previous target)
    float start_x_ = 0.0f, start_y_ = 0.0f;      // Lb.z9a (spawn midpoint)

    // --- the hit shake (JS `d3a` + `DL`) ----------------------------------
    // The camera kick on a landed hit. The JS shake needs the per-effect
    // `em` trajectory configs (not in the specs); the port uses a decaying
    // random offset: shake() rolls ±intensity on both axes, update()
    // multiplies it by 0.85 per frame (→ ~0 after ~25 frames). The RNG is
    // a PRIVATE LCG (see shake's impl) — never the fight's shared roll01
    // (that would perturb the AI decisions and diverge the pose dump).
    float shake_x_ = 0.0f;
    float shake_y_ = 0.0f;
    // Rolls a ±intensity kick on both axes (called on a landed hit).
    void shake(float intensity);

    // Recomputes center/zoom from the two fighters' world COM positions
    // (JS `Eu.ma` — the native fighter world_x/world_y anchors) and the
    // view size. An exact port of the JS camera chain (see the struct
    // comment); the `ay`/`by` are the CoM y's (the oracle's CoM-mid
    // vertical target — the native maps it to the floor anchor so the
    // dojo's verified composition holds).
    void framing(float ax, float ay, float bx, float by, float view_w, float view_h);
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
    ~FightController();  // closes the pose dump file if the dump is cut short

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

    // [trace, Phase 0] Arms the per-frame pose dump: for the first `frames`
    // fight frames, update() appends one JSONL line to `path` (reference/
    // traces/native_pose.jsonl). Pure trace — the simulation is untouched.
    void set_pose_dump(const std::string& path, int frames);

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
    // JS `vhb` (L410) case 1 -> `Z2()`: the HUD "Next" button. When a round
    // has ended and the host is waiting between rounds (round_wait_), runs
    // the recovery and starts the next round. No-op while a round is live.
    void next_round_requested();
    // JS: the between-round wait gate — true after a round ends until the
    // player requests the next round (the HUD Next button / `vhb` L410).
    bool round_wait() const { return round_wait_; }
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
    // The visual effects layer (hit sparks) — presentation only, never
    // touches the simulation (its RNG is a private LCG, not roll01).
    const EffectSystem& fx() const { return fx_; }
    // The current center-screen banner (ROUND N / FIGHT! / K.O. /
    // VICTORY / DEFEAT) — presentation only.
    banner_kind banner() const { return cur_banner_; }
    // The banner's display text ("" for none).
    const char* banner_text() const;
    // The banner's progress through its hold, 0..1 (for the fade/scale).
    float banner_progress() const;

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
    bool round_wait_ = false;      // JS: the host waits for the player's
                                   // Next between rounds (vhb L410 -> Z2)
    // The StartStance input buffer (JS `wd.WC` L426 + `llb` L429): ONE slot
    // — the LAST press during phase 1 (StartStance) wins; it is replayed
    // when the fight starts (enter_fight) as if the player pressed now.
    sf2::scene::key_type start_buffer_key_ = sf2::scene::key_type::up;
    bool start_buffer_filled_ = false;
    bool start_stance_done_ = false;  // phase 1 -> 2 gate
    int start_stance_frames_ = 0;  // phase 1 hold counter
    int end_stance_frames_ = 0;    // phase 3 hold (the FIGHT!/KO banner)
    // --- the banner machine (presentation only — see banner_kind) ---------
    banner_kind cur_banner_ = banner_kind::none;
    int banner_start_ = 0;        // frame_ when the banner was raised
    int banner_len_ = 0;          // hold length in frames (0 = not timed)
    int banner_round_ = 0;       // the ROUND N number (banner_round_+1 shown)
    // The visual effects layer (hit sparks) — presentation only.
    EffectSystem fx_;
    std::vector<RoundOutcome> history_;
    FightLogLine last_log_;
    float wall_min_ = 80.0f, wall_max_ = 1880.0f, floor_y_ = 0.0f;
    FightCamera camera_;
    FightHud hud_;
    float time_since_log_ = 0.0f;

    // --- pose dump (trace infrastructure, no behavior change) --------------
    std::string pose_dump_path_;
    int pose_dump_frames_ = 0;      // frames left to dump (0 = off)
    int pose_dump_written_ = 0;     // lines written so far
    std::FILE* pose_dump_file_ = nullptr;

    // Appends one JSONL line for the current frame to the dump file
    // (lazily opened on the first dumped frame; closed after the last).
    void dump_pose_frame();

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

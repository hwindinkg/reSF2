// Fight controller implementation (Phase 3.5).
// Ported from the game's `ca` class (sf2.502f0946.js L379-433). See
// core/scene/README.md "Fight controller (Phase 3.5)" for the JS study.

#include "scene/fight.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>

#include "audio/audio.hpp"
#include "audio/sfx_table.hpp"  // `sfx_stem_for_js` = JS `ta.WBa` (L1265-1274)
#include "scene/fight_camera_sya.hpp"

namespace sf2::scene {

namespace {
// Forward: Jf.OBa stage ids (defined with the bus block below).
int oba_phase(fight_phase p);

// JS `lg.vQ` slot 1/2 (`XH`/`z_`; `lg.he` L749 `lg.xEa(this.Ba, c)`): a
// fighter's animation-NAME list is the current animation's `xl` = the move's
// own name plus its TRANSITIVE `<Template>` chain (`MoveDef::anim_names`).
// The old fill used the move NAME alone, so a template-tag guard such as the
// `Step` template's `<CurrentAnimation Name="Step"/>` (inherited by
// StepForward) never matched and the restart gate read false.
std::vector<std::string> anim_names_of(const Fighter& f) {
    if (f.current_move() == nullptr) return {};
    return f.current_move()->anim_names;
}

// The fight viewport (JS `Lb.width`/`Lb.height` for the fight screen) - the
// same 1280x720 the camera framing hardcodes (framing calls below). The
// `sXa` ringout arrows are screen-space and need it.
constexpr float kFightViewW = 1280.0f;
constexpr float kFightViewH = 720.0f;

// JS `jg.parse` (L731): the `fight/fx` run lengths keyed by the HitEffect
// `FileName` (`vT`). The `jg` action is dispatched from the root `<Triggers>`
// HitEvent set (see `dispatch_move_actions`), so the FileName the shipped
// CriticalEffect/BlockEffect/HitEffect trigger conditions select picks the
// run (moves.xml: `critical` / `block` / `hit_blade`).
constexpr int kFlashFramesCritical = 29;  // L731 "critical"/"hit_blade" = 29
constexpr int kFlashFramesBlock = 24;     // L731 "block" = 24
constexpr int kFlashFramesShieldHex = 16; // L731 "effect_shield_hex_hit" = 16

// JS `jg.parse` (L731) switch on `this.vT`: the `<vT>/<vT>_N` sprite-run
// length. "effect_tornado_hit"/"mgc_effect_gust_up"/unknown = 0 (no run ->
// `Hyb` starts no flash, `EffectSystem::spawn_hit_flash` sets `active=false`).
int hit_effect_run_frames(const std::string& file) {
    if (file == "block") return kFlashFramesBlock;
    if (file == "effect_shield_hex_hit") return kFlashFramesShieldHex;
    if (file == "critical" || file == "hit_blade") return kFlashFramesCritical;
    return 0;
}

// JS `lrb` (L395): the flash time `c` (`Vu.time`) = 1/60 on a critical hit,
// else 1/120.
constexpr float kFlashTimeCrit = 1.0f / 60.0f;
constexpr float kFlashTimeNormal = 1.0f / 120.0f;

// `<CounterPunches Value="50"/>` (reference/extracted/xml/res/internal_settings.xml
// L557; JS `v.Qxa` L1157
// `v.Qxa=u.I(a.A("CounterPunches").attributes.get("Value"),2)`).
// `ca.Cgb` L396 forces the Punchbag's hit reaction when the defender's hit
// counter `sI` reaches this cadence. DATA-DRIVEN (JS-STRICT): parsed into
// `FightParams::counter_punches` by `load_fight_params_from_settings` — the
// shipped XML resolves to 50; the `u.I(...,2)` fallback is 2.

// The banner machine timings (JS class `Cr` L2022-2027 — all in SECONDS:
// `Cr.fu(a){this.Sc=a;...}` L2026). The old port used invented frame counts
// (60 / 40 / 90).
constexpr float kJsBannerRoundBreakSeconds = 1.666f;  // `Cr.tca` L2023 fu(1.666)
constexpr float kJsBannerArmDelaySeconds = 0.5f;      // `Cr.tca` L2023
                                                      // `wh.delay(...,500)`
constexpr float kJsBannerHoldSeconds = 1.166f;        // `Cr.Zy`/`Cr.GZ` L2024
// The phase-1 StartStance hold. The oracle trace (`oracle_pose.jsonl`)
// shows `phase` 1 -> 2 at f=134, so phase 1 is 133 port frames (phase 2
// starts on frame 133 of the port's 1-based counter, matching the oracle's
// 0-based 134).
constexpr int kStartStanceFrames = 133;

// [fx] Latch a fighter's current strike-capsule endpoints (`HitCapsule.r1/r2`
// = JS `sx.ma`/`Zs.ma`) into `prev_cap_ends` (= JS `sx.mf`/`Zs.mf`). Called
// once per tick AFTER the hit pass, so the next frame's `apply_hit` reads the
// true previous-frame positions for the `Hyb` hit direction (JS L395).
void snapshot_capsule_ends(FightFighter& f) {
    f.prev_cap_ends.clear();
    for (const sf2::scene::HitCapsule& c : f.body.capsules) {
        f.prev_cap_ends[c.name] = std::make_pair(c.r1, c.r2);
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// FightCamera — the exact JS camera port (`ma.Sya` L1833 + `Ut.Al` L826 +
// `ql` L362-371). See the FightCamera struct comment in fight.hpp for the
// full JS chain. The key numbers:
//   - tyb (L363): target = midpoint of the fighters' CoM nodes (`Eu.ma` —
//     the native world_x/world_y anchors); the VERTICAL target is the
//     arena-floor anchor (the oracle's CoM-mid for its dummy fight settles
//     on the dojo floor line; the native keeps the verified floor@0.78
//     composition).
//   - dZa (L363-365): $X = target-prevTarget; bY = prevFocus+$X;
//     LO = (bY-focus) + (target-focus)*0.15; |LO|>200 -> 200;
//     focus += LO; |focus-prevFocus|>50 -> 50.
//   - init: the camera starts at the spawn midpoint (Lb.z9a L475); the
//     first target step gives the oracle's intro jump (cy -50/frame, the
//     -101.5 -> -151.5 -> ... -> -222 ramp over ~60 frames).
//   - Al (L826): Io = arenaWidth/2 - focus clamped to
//     +/-((arenaWidth-oGa)*Bj*0.5 - nC*0.5) (panorama limit) — applied to
//     the native center_x (Io = arena_center - center_x).
//   - Sya (L1833): zoom = viewH/(arenaH*Bj), aspect clamp 0.45..1, the
//     extra narrow-screen clamp, width-fit min(viewW/(span*f+100),1), the
//     min-zoom 0.6+((clamp(c,0.5,1)-0.5)/0.5)*0.7 (-> 1.3 at 16:9), the
//     portrait shift round((viewH-e*f)/2)/f*0.5.
//
// NOT ported (flagged in earlier phases): the Bf intro-lens (f3a + Dvb)
// and the shake (d3a + DL) — the oracle capture never triggers the lens
// (trace zoom=1 throughout) and the shake needs the per-effect
// trajectory (`em`) configs.
// JS `ql.Dvb(a)` (L370): `IJ=!0; Bf=a; a.nM<1&&(a.nM=1); a.DS=ia.xCa();
// a.currentScale=a.DS; a.currentFrame=0; R5=a.jz`. `ia.xCa()` is the layer
// zoom `Ut.Bj` the port already computes into `zoom_layer`.
void FightCamera::apply_zoom_effect(int effect_time, float scale) {
    zoom_effect_active_ = true;                            // `IJ=!0`
    zoom_effect_time_ = static_cast<float>(effect_time);   // `Bf.jz`
    zoom_effect_scale_ = scale < 1.0f ? 1.0f : scale;      // `nM` clamp
    zoom_effect_base_ = zoom_layer;                        // `DS = ia.xCa()`
    zoom_effect_current_ = zoom_effect_base_;              // `currentScale = DS`
    zoom_effect_frame_ = 0;                                // `currentFrame = 0`
    std::fprintf(stdout, "[fx] ZoomEffect latch jz=%d zoomScale=%.3f base=%.3f\n",
                 effect_time, static_cast<double>(scale),
                 static_cast<double>(zoom_effect_base_));
    std::fflush(stdout);
}

// JS `ql.f3a()` (L367):
//   a = |Bf.nM - Bf.DS| / (Bf.jz*.5);
//   Bf.currentFrame <= Bf.jz/2 ? Bf.currentScale -= a (floor Bf.nM)
//                              : Bf.currentScale += a (ceil Bf.DS);
//   Bf.currentFrame++.
void FightCamera::tick_zoom_effect() {
    if (!zoom_effect_active_) return;
    const float half = zoom_effect_time_ * 0.5f;
    const float a = std::fabs(zoom_effect_scale_ - zoom_effect_base_) / half;
    if (static_cast<float>(zoom_effect_frame_) <= half) {
        zoom_effect_current_ -= a;
        if (zoom_effect_current_ < zoom_effect_scale_) {
            zoom_effect_current_ = zoom_effect_scale_;
        }
    } else {
        zoom_effect_current_ += a;
        if (zoom_effect_current_ > zoom_effect_base_) {
            zoom_effect_current_ = zoom_effect_base_;
        }
    }
    ++zoom_effect_frame_;
}

void FightCamera::framing(float ax, float ay, float bx, float by, float view_w,
                          float view_h) {
    // Wired: the exact JS camera chain lives in fight_camera_sya.hpp
    // (ql.tyb L363 + ql.dZa L363-365 + Ut.Al L826 + ma.Sya L1833) —
    // framing_sya_impl runs it against this camera's live state.
    framing_sya_impl(*this, ax, ay, bx, by, view_w, view_h);
}

// duplicate of framing() body kept for reference — REMOVED (dead code guard):
// (the pre-wire body moved verbatim into framing_sya_impl; see
// core/scene/fight_camera_sya.hpp)

// The hit-judder + hit-stop (JS `ql.DL` L370, `ql.Fnb` L364, `ql.d3a` L363).
// `apply_hit_effect` = `DL(a)`: latch the `<HitEffect>` row and arm the
// pause (`U1/N3 = PauseTime`) and the judder (`wR/cU/N5 = EffectTime`).
// `tick_hit_effect` = `Fnb()` then `d3a()`: count the two down and, while
// the judder is live, write the JS camera-node offset
//   x = mva*b*sin($za*a*h)*(g-h)/g
//   y = nva*b*sin(aAa*a*h)*(g-h)/g
// with `h = N5-cU` (elapsed) and `a=0.75`/`b=0.3` — the constant resolution
// of the shipped `ce.Bub` trajectory config (see fight.hpp).
void FightCamera::apply_hit_effect(const sf2::scene::HitEffect& e) {
    hw_ = e;
    hit_effect_valid_ = true;
    pause_active_ = true;      // `U1=!0`
    pause_frames_ = e.pause_time;    // `N3=a.YIa`
    shake_active_ = true;      // `wR=!0`
    effect_total_ = e.effect_time;   // `N5=a.jz`
    effect_frames_ = e.effect_time;  // `cU=a.jz`
    shake_peak_x_ = 0.0f;
    shake_peak_y_ = 0.0f;
    // JS `this.gh(0,null)` fires camera event 0 (the audio/state hook — the
    // port has no camera event bus; the audio dispatch is out of scope).
}

void FightCamera::tick_hit_effect() {
    // JS `Fnb()`: `U1&&(N3<=0&&(U1=!1,Bob()),N3--);
    //              wR&&(cU<=0&&(wR=!1,Cwb()),cU--)`.
    if (pause_active_) {
        if (pause_frames_ <= 0) pause_active_ = false;  // `U1=!1, Bob()`
        --pause_frames_;
    }
    if (shake_active_) {
        if (effect_frames_ <= 0) {
            shake_active_ = false;  // `wR=!1, Cwb()`
            // Report: the actual judder envelope reached (JS `Byb` peak).
            std::fprintf(stdout,
                         "[fx] judder done type=%s peakX=%.3f peakY=%.3f\n",
                         hw_.type.c_str(), static_cast<double>(shake_peak_x_),
                         static_cast<double>(shake_peak_y_));
            std::fflush(stdout);
        }
        --effect_frames_;
    }
    // JS `d3a()`: `if(this.wR&&this.hw!=null){ g=N5, h=N5-cU; ... }`.
    if (shake_active_ && hit_effect_valid_) {
        const float g = static_cast<float>(effect_total_);
        const float h = static_cast<float>(effect_total_ - effect_frames_);
        if (g > 0.0f) {
            const float kAmp = 0.3f;   // `ce.Bub.lva.y` (50>=j_.y)
            const float kFreq = 0.75f; // `ce.Bub.Zza.y` (50>=j_.x)
            const float env = (g - h) / g;
            shake_x_ = hw_.amplitude_x * kAmp *
                       std::sin(hw_.frequency_x * kFreq * h) * env;
            shake_y_ = hw_.amplitude_y * kAmp *
                       std::sin(hw_.frequency_y * kFreq * h) * env;
            if (std::fabs(shake_x_) > shake_peak_x_) shake_peak_x_ = std::fabs(shake_x_);
            if (std::fabs(shake_y_) > shake_peak_y_) shake_peak_y_ = std::fabs(shake_y_);
        }
    }
}

// ---------------------------------------------------------------------------
// FightHud
// ---------------------------------------------------------------------------

void FightHud::init(const std::map<std::string, sf2::data::Texture>&,
                    const std::map<std::string, sf2::data::atlas_frame>& atlas_frames,
                    float tex_w, float tex_h,
                    const std::map<char, sf2::data::atlas_frame>& digits_frames,
                    float digits_tex_w, float digits_tex_h,
                    std::function<GLuint(const std::string&)> texture_lookup) {
    frames_ = atlas_frames;
    tex_w_ = tex_w;
    tex_h_ = tex_h;
    digit_frames_ = digits_frames;
    digits_tex_w_ = digits_tex_w;
    digits_tex_h_ = digits_tex_h;
    texture_lookup_ = std::move(texture_lookup);
    ready_ = true;
    layout(view_w_, view_h_);
}

void FightHud::set_hp(float player_hp, float player_max, float enemy_hp, float enemy_max) {
    if (player_max > 0.0f) player_bar_.ratio = std::max(0.0f, std::min(1.0f, player_hp / player_max));
    if (enemy_max > 0.0f) enemy_bar_.ratio = std::max(0.0f, std::min(1.0f, enemy_hp / enemy_max));
    // The "hit" (trailing) bar eases toward the current ratio.
    player_bar_.hit_ratio += (player_bar_.ratio - player_bar_.hit_ratio) * 0.2f;
    enemy_bar_.hit_ratio += (enemy_bar_.ratio - enemy_bar_.hit_ratio) * 0.2f;
}

void FightHud::set_round(int round_number, int rounds_total) {
    round_number_ = round_number;
    rounds_total_ = rounds_total;
}

void FightHud::set_timer(int seconds) { timer_seconds_ = seconds; }

void FightHud::set_labels(const std::string& player_name, const std::string& enemy_name) {
    player_name_ = player_name;
    enemy_name_ = enemy_name;
}

void FightHud::set_phase(fight_phase phase) { phase_ = phase; }

void FightHud::update(float dt, bool running) {
    (void)dt;
    (void)running;
    // The JS Sf.iPa (L2036) decrements the HUD countdown while `round.Vt`
    // (running); the demo reads NF directly from the fight state, so no
    // per-frame HUD state is needed.
}

void FightHud::layout(float view_w, float view_h) {
    view_w_ = view_w;
    view_h_ = view_h;
    // JS Sf.layout (L2036-2037): the bars sit at (viewW*.5 ± 520*c) with
    // a scale `c` that shrinks with the view. The port uses the same
    // structure at a fixed scale (the demo renders 1280x720):
    //   bar center-x: player 320, enemy 960; y = 150; w = 440 (each bar
    //   covers half the width minus the timer), h = 43 (the atlas frame).
    const float bar_w = 440.0f, bar_h = 43.0f, bar_y = 150.0f;
    player_bar_.is_player = true;
    player_bar_.x = view_w_ * 0.5f - 520.0f;
    player_bar_.y = bar_y;
    player_bar_.w = bar_w;
    player_bar_.h = bar_h;
    enemy_bar_.is_player = false;
    enemy_bar_.x = view_w_ * 0.5f + 520.0f - bar_w;
    enemy_bar_.y = bar_y;
    enemy_bar_.w = bar_w;
    enemy_bar_.h = bar_h;
}

void FightHud::render(sf2::render::Renderer& r) {
    (void)r;
    // The HUD is rendered by the demo (fight_controller_demo/main.cpp
    // render_fight): the HP bars are flat quads, the timer digits use the
    // digits.fnt glyph rects, and the round dots are filled squares. The
    // FightHud class keeps the JS layout data (Sf.layout L2036-2037) so a
    // later phase can render the full atlas-backed HUD (HealthBar_* frames,
    // Round_Done/Undone dots, the round/FIGHT labels from round.png).
    if (!ready_) return;
    (void)texture_lookup_;
}

// ---------------------------------------------------------------------------
// FightController
// ---------------------------------------------------------------------------

void FightController::init(const BattleParams& battle,
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
                           const PerkSetup& perks) {
    init_locks(battle, model, moves, clips, tactics, tactic, player_name, enemy_name,
               player_x, player_y, enemy_x, enemy_y, player_max_hp, enemy_max_hp,
               std::move(roll01), {}, perks);
}

void FightController::init_locks(
    const BattleParams& battle, const sf2::scene::Model& model,
    const std::map<std::string, sf2::scene::MoveDef>& moves,
    const std::map<std::string, sf2::data::anim_clip>& clips,
    const std::vector<sf2::scene::TacticsFile>& tactics,
    const sf2::scene::TacticDef* tactic, const std::string& player_name,
    const std::string& enemy_name, float player_x, float player_y,
    float enemy_x, float enemy_y, int player_max_hp, int enemy_max_hp,
    std::function<float()> roll01,
    const std::vector<sf2::scene::OwnedItem>& player_owned,
    const PerkSetup& perks,
    std::function<void(int)> reseed01,
    const sf2::scene::Model* player_model,
    const sf2::scene::Model* enemy_model,
    const sf2::scene::TacticDef* player_tactic) {
    battle_ = battle;
    prize_fh_ = PrizeFh();  // fresh Fh per battle (JS `v.kD(new Fh, ...)`)
    player_.style = StyleMeter();  // style meters reset per battle
    enemy_.style = StyleMeter();
    model_ = model;
    moves_ = &moves;
    clips_ = &clips;
    tactics_ = tactics;
    tactic_ = tactic;
    // P4b: the player's tactic resolution (JS `IKa` L672) — the player's own
    // `<Tactic>` when it resolves, else "Standard". LOGGED ONLY: the player's
    // move start is the JS `Gc.DK` `c == false` branch (L673-674) and never
    // consults a tactic — the `Md`/`iCa` weighted roulette lives inside
    // `Gc.Pkb`, which the human path cannot reach (`Fighter::try_select_move`).
    // Kept because the AI-demo callers still pass their battle tactic here and
    // the `[fight] ... player tactic:` line is part of the boot log.
    player_tactic_ = (player_tactic != nullptr) ? player_tactic : tactic;
    std::fprintf(stdout, "[fight] enemy tactic: %s ; player tactic: %s\n",
                 tactic_ != nullptr ? tactic_->name.c_str() : "<none>",
                 player_tactic_ != nullptr ? player_tactic_->name.c_str() : "<none>");
    std::fflush(stdout);
    roll01_ = std::move(roll01);
    reseed01_ = std::move(reseed01);
    // JS `cl.pmb`: each battle re-picks the `ERuleRandom` children (`pn.M4`).
    random_pick_.clear();
    random_pick_done_ = false;

    // NOTE (W2 blocker): the move-LIST weapon subtype stays "Fists". The JS
    // derives it from the equipped weapon (`xc.cM` L809-810 -> `ra.Hza`
    // L684-685), so a knives fighter should select the Knives moves
    // (`KnivesStartStanceIdle`, `KnivesSlash`, ...). Deriving it here makes
    // that JS-correct change but breaks the `--verify-input` probe
    // expectations, which are built on the shipped Fists loadout
    // (reference/tools/input_phase1.txt): 7/7 -> 5/7. The gate fix belongs in
    // the probe harness (app/game/main.cpp), outside the owned file set.
    player_ = make_fighter(player_name, true, player_x, player_y, player_max_hp,
                           "Fists", player_owned, false, false, player_model);
    // JS `ur` L194-195 gates the enemy: NotAI -> no AiController, and
    // NotAnimation -> no animation attach (bind pose). Each side also renders
    // its OWN equipment model (JS `xc.cM` L809-810; the Punchbag dummy's
    // `merged_bag`, the boss's gear model).
    // JS `ra.Hza` L684-685 builds the move list from the fighter's OWN items
    // (`d.items = a.parameters.jt()`) and `Fd` L808 takes the move-LIST
    // subtype from the equipped Weapon slot's `SubType`. The enemy's
    // items/subtype are resolved from his stage `<Warrior>/<Template>`
    // (screens.cpp `battle_warrior` -> `BattleParams::enemy_owned` /
    // `enemy_weapon_subtype`), so a boss fights with his real kit (Shin:
    // WEAPON_KUNAI -> Knives) instead of the implicit Fists default.
    const std::string enemy_subtype =
        battle_.enemy_weapon_subtype.empty() ? std::string("Fists")
                                             : battle_.enemy_weapon_subtype;
    enemy_ = make_fighter(enemy_name, false, enemy_x, enemy_y, enemy_max_hp,
                          enemy_subtype, battle_.enemy_owned,
                          battle.enemy_not_ai, battle.enemy_not_animation,
                          enemy_model);
    // [FIX Phase 4b — manual control] The player is MANUAL: no AiController,
    // no auto-attack. The input path (player_input -> Fighter::input ->
    // try_select_move) drives the player's moves; the enemy keeps the AI.
    // The demo callers that want an AI player (app/ai_demo) attach their own
    // controller. (The old code attached the player AI unconditionally, which
    // overrode the user's key input — "no input" + the player
    // animating randomly.)

    // Sample the initial stance idle (JS: the weapon's stance idle clip).
    // NotAnimation enemies hold their bind pose instead (JS `QD` L195).
    sample_idle(player_);
    sample_enemy_idle();
    rebuild_body(player_, enemy_);
    rebuild_body(enemy_, player_);
    // Perk bus register (ZOa analog).
    perk_setup_ = perks;
    setup_bus(perks);
    // Magic/effect containers (JS `tl.Rf` L842-844): the descriptors come
    // from the `<Effect>` rows (JS `Yl` L728-730) joined to the REAL
    // `res/magic/mgc_*.json` frame runs (app.cpp `load_magic_atlas_bundle`
    // publishes them via `magic_atlas_frames()`); `Yl` "Effect" actions route
    // here via `tl.Nt`. The `fight/fx` built-ins are only the fallback when
    // no magic atlas resolved.
    {
        const std::size_t n = magic_fx_.load_descriptors(
            sf2::scene::magic_atlas_frames(), *moves_, global_triggers_);
        if (n == 0) magic_fx_.add_default_descs();
    }
    // Magic init (`Ka`: `zL(0)`, `yL($6a)` = InitialCharge table, `LA`).
    init_magic();

    // Camera framing (JS ma.Sya L1833 + the ql dZa intro): the fighters'
    // world anchors are the JS CoM nodes (Eu.ma); the camera starts at the
    // spawn midpoint (Lb.z9a) and chases the smoothed midpoint. `arena_w`
    // is the RAW location width (JS Lb.width = wall + wall_max — the
    // renderer's arena_center_x = arena_width*0.5 depends on it).
    camera_.arena_w = wall_min_ + wall_max_;
    camera_.start_x_ = (player_.fighter.world_x() + enemy_.fighter.world_x()) * 0.5f;
    camera_.start_y_ = (player_.fighter.world_y() + enemy_.fighter.world_y()) * 0.5f;
    camera_.framing(player_.fighter.world_x(), player_.fighter.world_y(),
                    enemy_.fighter.world_x(), enemy_.fighter.world_y(), 1280.0f, 720.0f);

    // The fight start (JS ggb L383): round 0 -> the first round init.
    round_.number = 0;
    round_init();
    // [ROUND-plate lead-in] The controller idles at frame 0 until the `ik` VS
    // overlay ends (`release_intro`, called by the fight screen once
    // `!vs_active_`): the JS creates the fight only after `ik.kg` (L2071), so
    // the port must not burn the phase-1 clock under the overlay.
    // `release_intro` then enters the stance (phase 1) at frame 0 — exactly
    // the oracle's first recorded frame (reference/traces/oracle_pose.jsonl:
    // f=0 phase 1, clip null; NO idle lead-in).
    frame_ = 0;
    phase_ = fight_phase::idle;
    // The plate clock is HELD until the `ik` VS overlay ends (`release_intro`):
    // the plate must not be consumed while the overlay covers the scene.
    intro_hold_ = true;
    // The FIRST round's ROUND 1 plate (`Cr.tca` L2023: type 2, `fu(1.666)`,
    // armed after a 500 ms `wh.delay`). `round_start()` — which raises it
    // for rounds 2+ through the `Z2` path — is NOT called for the first
    // round. It is DISPLAY ONLY (`banner_action::none`): the JS init already
    // entered the start stance (the oracle's f=0 IS phase 1), so the plate
    // must not re-run `FNa` when it expires (a re-entry would restart the
    // 133-frame stance mid-way).
    cur_banner_ = banner_kind::round;
    banner_time_ = kJsBannerRoundBreakSeconds;
    banner_total_ = kJsBannerRoundBreakSeconds;
    banner_armed_ = false;                       // `tca` clears `wU` ...
    banner_arm_delay_ = kJsBannerArmDelaySeconds;  // ... until the 500 ms delay
    banner_action_ = banner_action::none;
    banner_start_ = frame_;
    banner_round_ = round_.number;   // 0 -> "ROUND 1"
    std::fprintf(stdout, "[fight] banner: ROUND %d (F%d)\n", banner_round_ + 1, frame_);
    std::fflush(stdout);

    // JS `rb.Wkb()` (L1277: `ta.ak("snd_gong")`), called from the
    // fight-registration success branch (L1216: `p.o.save(), d && rb.Wkb()`)
    // — ONCE per fight entry, not per round. `init_locks` is that entry
    // (the ctor path the fight screen / `--fight` / the drivers all take).
    // `snd_gong` = 65591 -> `gong.wav` (the `ta.WBa` row is L1266).
    // The Dojo hub's `FightNone` viewer is built through the `m1a` factory
    // (`Tf.init` L1971), NOT the registration path, so it suppresses this
    // (`set_silent_entry`, called before `init_locks`).
    if (!silent_entry_) {
        sf2::audio::AudioEngine::instance().play("snd_gong");
    }
}

// The shared fight draw (JS `Da.pg.jf()`, L2352). An injected override (the
// demo/probe path) wins; otherwise the OWNED `DaPrng` stream is used - the
// same `Xx`+`Rk` LCG the game's global `Da.pg` uses (L2352/2366). `Rk.s4(1)`
// is exactly `Rk.jf()` (L2352: `s4(a){return this.jf()*a}`).
float FightController::draw01() {
    if (roll01_) return roll01_();
    return static_cast<float>(prng_.s4(1.0));
}

// JS `Da.IT(a)` (L2353: `Da.pg.sL(a)`): reseed the shared fight stream in
// place - the `cl.pmb` reseed (L1413). An injected override's reseed hook
// reseeds the external stream; otherwise the owned `DaPrng`.
void FightController::reseed_stream(int seed) {
    if (reseed01_) {
        reseed01_(seed);
        return;
    }
    prng_.seed(static_cast<std::uint32_t>(seed));
}

// JS `uf.sja(a)` (L115: `Math.floor(uf.OKa.RGa() * (a - 0)) + 0`, and
// `at.Nlb` L115 is `Math.random()`): the RandomSound name pick. The stream is
// the UNSHARED `Math.random` — `ta.ak`'s RandomSound path must never eat the
// fight's `Da.pg` draws. Native: the pinned `math_random01()`.
int FightController::random_sound_index(int n) {
    if (n <= 0) return -1;
    return static_cast<int>(math_random01() * static_cast<float>(n));
}

// JS `wd.BNa(a)` (L523): `let b = 0; for (; b < a.length;) a[b++].Uh(this);`
// — every action's `Uh` reaches the per-kind `wd` handler (L518-520). The
// ported kinds:
//   Sound (fm, L735):       `wd.dwb(a)` L519 -> `a.fka(this.parameters.voice)
//                           && ta.ak(a.name, a.ceb, a.volume)`.
//   RandomSound (am, L733): `wd.fwb(a)` L519 -> `a.fka(...) && ta.ak(a.ab())`.
//   StopSound (im, L736):   `wd.ewb(a)` L519 -> `ta.Jwb(a.name)` L1264
//                           (`a=ta.WBa(name); a!=null && L.K.$f.stop(a)`).
//                           NOTE: `ewb` has NO `fka` voice gate (only the two
//                           play kinds check `Voice`), so StopSound fires for
//                           every matching trigger.
//   ShakeScreen (dm, L734): `wd.Wvb(a)` L519 -> `this.uS.Z(a)` -> the fight
//                           screen's `Pi.uS(a){this.Ta.DL(a.hw)}` (L424) ->
//                           `ql.DL(a)` L370 (`hw=a, U1=!0, N3=a.YIa, wR=!0,
//                           N5=cU=a.jz`). `Ta` IS the `ql` camera, so this is
//                           the same latch `HitEffect` uses; the port's
//                           `FightCamera::apply_hit_effect` (L116 above) is
//                           that exact function.
//   CameraWeight (Wl, L726): `wd.ANa(a)` L520 -> `this.fS.Z(a)`; the only
//                           subscriber is `Pi.fS(){debugger}` (L424) — an
//                           EMPTY body. Dispatched as a logged no-op.
//   EnableBossAbility (Zl, L730): `wd.$vb(a)` L520 -> `this.dS.Z(a)`; the
//                           only subscriber is `Pi.dS(){debugger}` (L397) —
//                           EMPTY. Dispatched as a logged no-op.
//   AddBullets (Vl, L725):  `wd.Tvb(a)` L519 — `s6==0` (MagicBullet):
//                           `hZ(value)` (= `zL(bh+value)` L505) then `LA()`
//                           L505; `s6==1` (RaidChargeBullet): `vZa(value)`
//                           (= `dO+=value` L524) then `Amb()` L524.
// The remaining kinds (CreatePlayer/Delete/PlayAnimation/Effect/StopEffect/
// StopFollowEffect/TryOnEnd/HitEffect/SetCooldown/ZoomEffect) need the child
// models / magic-effect containers / perk cooldown timers / intro lens — each
// is listed with its exact missing subsystem in the follow-up report.
// JS `sa.HQ(0, name)` (L707) over the `sa.$h` map (L706): the ability-button
// name -> slot. Absent/unknown -> 0 (`HQ` returns 0).
static int button_slot(const std::string& name) {
    if (name == "Punch") return 9;
    if (name == "Kick") return 10;
    if (name == "Ranged") return 11;
    if (name == "Magic") return 12;
    if (name == "RaidCharge") return 13;
    if (name == "Super") return 14;
    return 0;
}

void FightController::dispatch_move_actions(
    const std::vector<const sf2::scene::MoveAction*>& acts, FightFighter& owner,
    const char* why, const sf2::scene::FightContext& conds) {
    for (const sf2::scene::MoveAction* act : acts) {
        if (act == nullptr) continue;
        const bool sound_kind = act->js_type == 2 || act->js_type == 3 || act->js_type == 4;
        // The four non-audio kinds this wave wired. Keyed on `kind`, not
        // `js_type`: `Xl` (Delete) and `jm` (TryOnEnd) BOTH declare
        // `super(1)` (L728/L737), so `js_type == 1` is ambiguous.
        const bool fx_kind = act->kind == "ShakeScreen" || act->kind == "CameraWeight" ||
                             act->kind == "EnableBossAbility" ||
                             act->kind == "AddBullets" || act->kind == "HitEffect" ||
                             // Magic-effect kinds (JS `Yl`/`gm`/`hm`
                             // L728/L735/L736): routed to the `tl` containers
                             // (`Xm`/`cv`) below.
                             act->kind == "Effect" || act->kind == "StopEffect" ||
                             act->kind == "StopFollowEffect" ||
                             // Child-model kinds (JS `mh`/`Xl`/`$l`): none of
                             // them has an `fka` voice gate.
                             act->kind == "CreatePlayer" || act->kind == "Delete" ||
                             act->kind == "PlayAnimation" ||
                             act->kind == "SetCooldown" || act->kind == "ZoomEffect" ||
                             act->kind == "TryOnEnd";
        if (!sound_kind && !fx_kind) continue;
        // JS `cb.Ti(a,b)` (L724): `if (Fd(this.$c)) return true;` then the
        // `<Conditions>` tree. `$c` empty -> always true.
        if (!act->conditions.empty() &&
            !sf2::scene::eval_move_conditions(act->conditions, conds)) {
            continue;
        }
        // --- ShakeScreen (`dm` L734 -> `wd.Wvb` L519 -> `Pi.uS` L424) -----
        if (act->kind == "ShakeScreen") {
            sf2::scene::HitEffect e;
            e.type = act->shake_type;
            e.pause_time = act->pause_time;      // `YIa`
            e.effect_time = act->effect_time;    // `jz`
            e.amplitude_x = act->amplitude_x;    // `mva`
            e.amplitude_y = act->amplitude_y;    // `nva`
            e.frequency_x = act->frequency_x;    // `$za`
            e.frequency_y = act->frequency_y;    // `aAa`
            std::fprintf(stdout,
                         "[fx] F%d %s %s ShakeScreen type=%s pause=%d eff=%d "
                         "ampX=%.1f freqX=%.2f ampY=%.1f freqY=%.2f\n",
                         frame_, owner.name.c_str(), why, act->shake_type.c_str(),
                         act->pause_time, act->effect_time,
                         static_cast<double>(act->amplitude_x),
                         static_cast<double>(act->frequency_x),
                         static_cast<double>(act->amplitude_y),
                         static_cast<double>(act->frequency_y));
            std::fflush(stdout);
            camera_.apply_hit_effect(e);
            continue;
        }
        // --- the two kinds whose JS consumer is a `debugger` no-op --------
        if (act->kind == "CameraWeight") {
            // `wd.ANa` L520 -> `Pi.fS` L424 `{debugger}`. `Wl.Uh` L726 delays
            // the call by `$x` seconds when `$x >= .01` (a `Re` timer); the
            // callback is the same no-op, so the port logs and does nothing.
            std::fprintf(stdout,
                         "[fx] F%d %s %s CameraWeight time=%.2f delay=%.2f "
                         "(JS Pi.fS no-op)\n",
                         frame_, owner.name.c_str(), why,
                         static_cast<double>(act->weight_time),
                         static_cast<double>(act->weight_delay));
            std::fflush(stdout);
            continue;
        }
        if (act->kind == "EnableBossAbility") {
            // `wd.$vb` L520 -> `Pi.dS` L397 `{debugger}`.
            std::fprintf(stdout, "[fx] F%d %s %s EnableBossAbility value=%d (JS Pi.dS no-op)\n",
                         frame_, owner.name.c_str(), why, act->bool_value ? 1 : 0);
            std::fflush(stdout);
            continue;
        }
        // --- SetCooldown (`bm` L733 -> `wd.Zvb` L520) ----------------------
        // `slot = sa.HQ(0, Button)` (`sa.$h` L706: Punch 9 / Kick 10 /
        // Ranged 11 / Magic 12 / RaidCharge 13 / Super 14; unknown -> 0, and
        // every `wKa`/`b5` case is 9/10/11/14 so 0 is a silent no-op), then
        // `wKa(slot)` (reset + `yd(slot,0,0)`) and `b5(slot, duration)`
        // (arm + `yd`). `wd.yJa` (L501) is the availability gate that reads
        // the armed timers.
        if (act->kind == "SetCooldown") {
            const int slot = button_slot(act->button);
            owner.fighter.ability_cooldown_reset(slot);
            owner.fighter.ability_cooldown_start(slot, static_cast<float>(act->duration));
            std::fprintf(stdout,
                         "[fx] F%d %s %s SetCooldown button='%s' slot=%d duration=%d\n",
                         frame_, owner.name.c_str(), why, act->button.c_str(), slot,
                         act->duration);
            std::fflush(stdout);
            continue;
        }
        // --- ZoomEffect (`km` L737 -> `wd.Yvb` L520 -> `Pi.AS` L424) --------
        // `Pi.AS(a){this.Ta.Dvb(a.Bf)}` (L424) -> `ql.Dvb` (L370).
        if (act->kind == "ZoomEffect") {
            camera_.apply_zoom_effect(act->effect_time, act->zoom_scale);
            continue;
        }
        // --- TryOnEnd (`jm` L737 -> `wd.Vvb` L519 -> `wd.qr`) --------------
        // `wd.Vvb(){this.qr.Z()}` fires the model's `qr` bus; `Pi.wia`
        // (L446) relays it to the screen listener `Oa.yS` (L2301:
        // `Ad.$Ma(); fU(); Oya=!0`) which restores `PeacefulRestore`. The
        // port's TryOn preview lives in the shop screen (screens.cpp), not a
        // FightController, so this only records the JS dispatch.
        if (act->kind == "TryOnEnd") {
            std::fprintf(stdout, "[fx] F%d %s %s TryOnEnd (wd.Vvb -> qr)\n",
                         frame_, owner.name.c_str(), why);
            std::fflush(stdout);
            continue;
        }
        // --- AddBullets (`Vl` L725 -> `wd.Tvb` L519) ----------------------
        if (act->kind == "AddBullets") {
            if (act->bullet_kind == 0) {
                // `hZ(value)` L505 = `zL(this.bh + value)`; `LA()` L505 then
                // normalizes (`my>=1` converts to a bullet + resets,
                // `bh` capped at 1) — the port's `la_normalize` (L2451).
                owner.bullets =
                    sf2::scene::bullets_add(owner.bullets, act->bullet_value);
                la_normalize(owner);
                std::fprintf(stdout,
                             "[fx] F%d %s %s AddBullets MagicBullet value=%d -> bh=%d "
                             "my=%.3f\n",
                             frame_, owner.name.c_str(), why, act->bullet_value,
                             owner.bullets, static_cast<double>(owner.charge));
                std::fflush(stdout);
            } else if (act->bullet_kind == 1) {
                // `vZa(value)` L524 = `this.dO += value`; `Amb()` L524 then
                // publishes `yd(13, -1, -1, dO)` on the `lHa` bus (the raid
                // charge animation channel, not ported) and, when `dO==0`, a
                // `yd(13, 0, 0)` on the `yp` bus.
                owner.raid_bullets += act->bullet_value;
                std::fprintf(stdout,
                             "[fx] F%d %s %s AddBullets RaidChargeBullet value=%d -> dO=%d "
                             "(Amb yd(13) not ported)\n",
                             frame_, owner.name.c_str(), why, act->bullet_value,
                             owner.raid_bullets);
                std::fflush(stdout);
            }
            continue;
        }
        // --- HitEffect (`jg` L731 -> `wd.Xvb` L519) ------------------------
        // JS `wd.Xvb(a)` (L519):
        //   `Xvb(a){this.Vu.Ica && ca.Ka()!=null &&
        //           ca.Ka().Kla(this.Vu.bk, this.Vu.fg, this.Vu.time,
        //                       a.vT, a.aza>0?a.aza:this.Qz, a.ywb)}`
        // `ca.Kla(a,b,c,d,e,f)` (L397) -> `Ta.Kla(a,b,c,!1,d,e,f)` ->
        // `ql.Kla(a,b,c,d,e,f,g){this.ia.Hyb(a,b,c,e,f,g)}` (L370) -> the
        // `Ut.Hyb` flash spawn (L825) = the port's `spawn_hit_flash`.
        // `this.Vu` is the OWNER's `lrb` latch (`reaction()`); the action's
        // `vT`/`aza`/`ywb` are FileName / ChangeHitEffectScale / StartingRotation.
        // The `ca.Ka()!=null` camera test has no port equivalent (the fight
        // always owns a camera), so it is not reproduced.
        if (act->kind == "HitEffect") {
            if (!owner.fighter.has_reaction()) continue;  // `this.Vu.Ica`
            const sf2::scene::Fighter::Reaction& r = owner.fighter.reaction();
            // `a.aza>0?a.aza:this.Qz` — a positive authored scale, else the
            // fighter's live `Qz` (`ChangeHitEffectScale` perk mods write it).
            const float scale =
                act->hit_effect_scale > 0.0f ? act->hit_effect_scale : owner.qz;
            const int frames = hit_effect_run_frames(act->hit_effect_file);
            fx_.spawn_hit_flash(r.pos.x, r.pos.y, r.dir.x, r.dir.y,
                                act->hit_effect_rotation, scale, r.time,
                                act->hit_effect_file, frames);
            std::fprintf(stdout,
                         "[fx] F%d %s %s HitEffect file=%s scale=%.2f rot=%.2f "
                         "pos=%.0f,%.0f frames=%d speed=%.5f\n",
                         frame_, owner.name.c_str(), why,
                         act->hit_effect_file.c_str(), static_cast<double>(scale),
                         static_cast<double>(act->hit_effect_rotation),
                         static_cast<double>(r.pos.x), static_cast<double>(r.pos.y),
                         frames, static_cast<double>(r.time));
            std::fflush(stdout);
            continue;
        }
        // --- Effect (`Yl` L728-730 -> `wd.gwb` L519 -> `tl.Nt` L842) -------
        // `cv.lwb` (L838): the spawn anchor is `a.position.nt(model.Fc)` —
        // the named `<Position>` Part's world position plus the facing-scaled
        // ShiftX (`c.x += ix*a.Wl`, L786) and `-ShiftY` (`c.y -= jx`), NOT the
        // owner CoM. `<Attach>` (`Vu` L781-783) anchors on its RootPoint.
        // `bv.model` is stamped here so StopEffect/StopFollowEffect resolve
        // the same `(Name, model)` identity (L838 `LNa`/`Gwb`).
        if (act->kind == "Effect") {
            if (!act->name.empty()) {
                const int side = (&owner == &player_) ? 0 : 1;
                const int facing = owner.fighter.facing() >= 0 ? 1 : -1;
                float ax = owner.fighter.world_x();
                float ay = owner.fighter.world_y();
                const std::string& part =
                    act->has_attach ? act->attach_root_point : act->effect_pos_part;
                bool part_ok = part.empty();
                if (!part.empty()) {
                    const int bi = owner.fighter.model().bone_by_name(part);
                    const std::vector<float>& pos = owner.fighter.positions();
                    if (bi >= 0 &&
                        static_cast<std::size_t>(bi) * 2 + 1 < pos.size()) {
                        ax = pos[static_cast<std::size_t>(bi) * 2];
                        ay = pos[static_cast<std::size_t>(bi) * 2 + 1];
                        part_ok = true;
                    }
                }
                // `ee.nt` L786: `c.x += this.ix*a.Wl; c.y -= this.jx`.
                ax += act->effect_shift_x * static_cast<float>(facing);
                ay -= act->effect_shift_y;
                const bool ok = magic_fx_.spawn(
                    act->name, ax, ay, owner.fighter.facing(), side,
                    act->effect_follow, ax - owner.fighter.world_x(),
                    ay - owner.fighter.world_y());
                std::fprintf(stdout,
                             "[fx] F%d %s %s Effect name=%s seq=%s anchor=%.0f,%.0f "
                             "part=%s follow=%d scale=%.2f,%.2f rot=%.1f -> %s\n",
                             frame_, owner.name.c_str(), why, act->name.c_str(),
                             act->sequence.c_str(), static_cast<double>(ax),
                             static_cast<double>(ay),
                             part.empty() ? "<com>"
                                          : (part_ok ? part.c_str() : "<unresolved>"),
                             act->effect_follow ? 1 : 0,
                             static_cast<double>(act->effect_scale_x),
                             static_cast<double>(act->effect_scale_y),
                             static_cast<double>(act->start_rotation),
                             ok ? "spawned" : "no descriptor");
                std::fflush(stdout);
            }
            continue;
        }
        // --- StopEffect (`gm` L735 -> `wd.Svb` L519 -> `tl.Ot` L843) -------
        if (act->kind == "StopEffect") {
            if (!act->name.empty()) {
                magic_fx_.stop(act->name, (&owner == &player_) ? 0 : 1);
                std::fprintf(stdout, "[fx] F%d %s %s StopEffect name=%s\n", frame_,
                             owner.name.c_str(), why, act->name.c_str());
                std::fflush(stdout);
            }
            continue;
        }
        // --- StopFollowEffect (`hm` L736 -> `wd.Uvb` L519 -> `tl.Pt` L843) -
        if (act->kind == "StopFollowEffect") {
            if (!act->name.empty()) {
                magic_fx_.stop_follow(act->name, (&owner == &player_) ? 0 : 1);
                std::fprintf(stdout, "[fx] F%d %s %s StopFollowEffect name=%s\n",
                             frame_, owner.name.c_str(), why, act->name.c_str());
                std::fflush(stdout);
            }
            continue;
        }
        // --- child models (JS `mh`/`Xl`/`$l`) ----------------------------
        // `mh.Uh(a){a.bwb(this)}` L727 -> `wd.bwb` L518.
        if (act->kind == "CreatePlayer") {
            spawn_child(owner, *act);
            continue;
        }
        // `Xl.Uh(a){a.cwb(this)}` L728 -> `wd.cwb` L519.
        if (act->kind == "Delete") {
            delete_child_target(owner, *act);
            continue;
        }
        // `$l.Uh(a){a.awb(this)}` L732 -> `wd.awb` L518.
        if (act->kind == "PlayAnimation") {
            play_child_animation(owner, *act);
            continue;
        }
        if (act->js_type == 3) {  // StopSound — no voice gate (JS `wd.ewb`)
            const char* s_stem = sf2::audio::sfx_stem_for_js(act->name.c_str());
            std::fprintf(stdout, "[sfx] F%d %s %s %s name=%s stem=%s\n", frame_,
                         owner.name.c_str(), why, act->kind.c_str(), act->name.c_str(),
                         s_stem != nullptr ? s_stem : "<none>");
            std::fflush(stdout);
            if (s_stem != nullptr) sf2::audio::AudioEngine::instance().stop(act->name);
            continue;
        }
        // JS `fm.fka(voice)` L735 / `am.fka(voice)` L733:
        // `return this.t7 ? true : a == this.J8` — a `<Sound Voice="X">` only
        // fires when X equals the fighter's own `xc.voice` (empty voice ->
        // every Voice-gated action is silent, JS-exact).
        if (act->has_voice && act->voice != owner.fighter.voice()) continue;
        if (act->js_type == 2) {  // Sound
            // JS `ta.ak(a.name, a.ceb, a.volume)` L1264: the name resolves
            // through `ta.WBa`; a miss plays NOTHING (logged as `<none>`).
            const char* stem = sf2::audio::sfx_stem_for_js(act->name.c_str());
            std::fprintf(stdout, "[sfx] F%d %s %s %s name=%s stem=%s\n", frame_,
                         owner.name.c_str(), why, act->kind.c_str(), act->name.c_str(),
                         stem != nullptr ? stem : "<none>");
            std::fflush(stdout);
            if (stem != nullptr) sf2::audio::AudioEngine::instance().play(act->name);
            continue;
        }
        // RandomSound — JS `am.ab()` L733: `b == 0 ? null : a[uf.sja(b)]`,
        // one uniform pick over the action's `<Name>` children.
        const int idx = random_sound_index(static_cast<int>(act->names.size()));
        const std::string pick =
            idx >= 0 ? act->names[static_cast<std::size_t>(idx)] : std::string();
        const char* stem =
            idx >= 0 ? sf2::audio::sfx_stem_for_js(pick.c_str()) : nullptr;
        std::fprintf(stdout, "[sfx] F%d %s %s %s (%d names) name=%s stem=%s\n", frame_,
                     owner.name.c_str(), why, act->kind.c_str(),
                     static_cast<int>(act->names.size()), pick.c_str(),
                     stem != nullptr ? stem : "<none>");
        std::fflush(stdout);
        if (stem != nullptr) sf2::audio::AudioEngine::instance().play(pick);
    }
}

// --- child models (JS `ih` / `wd.vd` / the `su` spawn cache) -------------
// `wd.fya(a,b,c,d)` (L535-536) builds or recycles one child:
//   `e = d!="" && a.cache.pull(d)`                        (recycled `ih`)
//   else `b = a.h7a(b); e = new ih(b)`                    (fresh from items)
//   `e.cacheName=d; e.Kd(c); e.ola(a.ws); e.parameters.ul=a.parameters.ul;
//    e.oa==null ? e.wI(a) : e.Rlb(); e.Naa(a.jb); e.prb(a.JG); e.TT(a.so);
//    a.zWa(e)`
// then `wd.bwb` (L518) plays `a.nx` on it. The port keeps one `ChildModel`
// per spawner side (the `vd` list) plus the `su` cache keyed by `cacheName`.
void FightController::spawn_child(FightFighter& owner,
                                  const sf2::scene::MoveAction& act) {
    if (clips_ == nullptr) return;
    // `d!="" && e = a.cache.pull(d)` — recycle a retired child.
    auto& pool = child_cache_[act.create_cache_key];
    sf2::scene::ChildModel* c = nullptr;
    if (!pool.empty()) {
        const std::size_t idx = pool.back();
        pool.pop_back();
        c = &children_[idx];
        c->active = true;
    } else {
        children_.emplace_back();
        c = &children_.back();
        // `b = a.h7a(items)` -> the child's parameters (its item set is
        // COPIED from the spawner, so `ra.Hza` gives it the spawner's move
        // list); `new ih(b)` -> the `wd` ctor + `Yc.load` of its model.
        // `wd.fya` L535-536 builds a FRESH `ih` from `a.h7a(a.items)`:
        // `wd.ylb` (L268939) resolves each `<Item>` to a list.xml item (its
        // own `Name`, else the spawner's `CopyParentType`/`Subtype`), and
        // `Yc.load` (L289330) merges their `<Item Model>` parts — so the
        // child wears its OWN skeleton (SkeletonMagic/SkeletonMissile/...)
        // plus the spawner's copied magic weapon, NOT the spawner's merged
        // body. The app owns the models.dat cache, so it supplies the model
        // here; without a provider the spawner's `model_` is kept.
        const sf2::scene::Model* child_model = nullptr;
        if (child_model_provider_) {
            child_model = child_model_provider_(act, owner.is_player);
        }
        c->fighter.set_model(child_model != nullptr ? *child_model : model_);
        std::fprintf(stdout,
                     "[child] model %s bones=%zu tris=%zu capsules=%zu "
                     "(spawner bones=%zu)\n",
                     child_model != nullptr ? "OWN" : "spawner",
                     c->fighter.model().bones.size(),
                     c->fighter.model().resolved_tris.size(),
                     c->fighter.model().capsules.size(), model_.bones.size());
        std::fflush(stdout);
        c->fighter.set_math_random([]() { return FightController::math_random01(); });
        c->fighter.set_color(fighter_color_);
        // `me` — the child's move list. JS `ra.Hza` rebuilds it from the
        // child's parameters (the COPIED items), so it is the spawner's own
        // list: `Fighter::hb()` (the `ra.Lk` list), NOT the `FightFighter`
        // scratch member.
        c->hb = owner.fighter.hb();
    }
    // `e.cacheName=d; e.Kd(c)` — the child's identity.
    c->name = act.create_name;
    c->cache_key = act.create_cache_key;
    c->is_player = owner.is_player;
    // `e.Naa(a.jb); e.prb(a.JG); e.TT(a.so)` — parent to the spawner and
    // inherit its live position/facing (`ih.NS` seeds from `lb.sxb()`).
    c->x = owner.fighter.world_x();
    c->y = owner.fighter.world_y();
    c->facing = owner.fighter.facing();
    c->clip = nullptr;
    c->clip_name.clear();
    c->clip_frame = 0;
    // `a.zWa(e)` — `vd.push(e)`.
    std::fprintf(stdout,
                 "[child] F%d %s CreatePlayer name=%s key=%s at=%.0f,%.0f\n",
                 frame_, owner.name.c_str(), act.create_name.c_str(),
                 act.create_cache_key.c_str(), static_cast<double>(c->x),
                 static_cast<double>(c->y));
    std::fflush(stdout);
    // `var c=a.nx; c!=null&&c!="" && (c=m.find(b.me, d=>d.name==a.nx), ...)`
    if (!act.start_animation.empty()) {
        play_child_clip(*c, act.start_animation, owner);
    }
}

// `m.find(c.me, d => d.name == anim)` (L518), then `c.NS(clip,
// clip.xD(c.Fc, c.da.hd()))` (L505) starts it. The port resolves the clip
// from the shared archive by the move's `FileName` stem — exactly what
// `Fighter::start_move_impl` does (fighter.cpp:675-684) — and drives it from
// `advance_child`.
void FightController::play_child_clip(sf2::scene::ChildModel& c,
                                      const std::string& anim,
                                      FightFighter& owner) {
    if (clips_ == nullptr) return;
    const sf2::scene::MoveDef* mov = nullptr;
    for (const sf2::scene::MoveDef* m : c.hb) {
        if (m != nullptr && m->name == anim) {
            mov = m;
            break;
        }
    }
    if (mov == nullptr) {
        std::fprintf(stdout, "[child] F%d %s child '%s' no move '%s' in list\n",
                     frame_, owner.name.c_str(), c.name.c_str(), anim.c_str());
        std::fflush(stdout);
        return;
    }
    // `jc.uja` L693: the clip key is `FileName` minus ".bytes" (`Eza`).
    std::string key = mov->file_name;
    const std::string suffix = ".bytes";
    if (key.size() > suffix.size() &&
        key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
        key = key.substr(0, key.size() - suffix.size());
    }
    const sf2::data::anim_clip* clip = nullptr;
    const auto it = clips_->find(key);
    if (it != clips_->end()) clip = &it->second;
    if (clip == nullptr) {
        const auto it2 = clips_->find(mov->name);
        if (it2 != clips_->end()) clip = &it2->second;
    }
    if (clip == nullptr) {
        std::fprintf(stdout,
                     "[child] F%d %s child '%s' clip '%s' (stem=%s) not in archive\n",
                     frame_, owner.name.c_str(), c.name.c_str(), anim.c_str(),
                     key.c_str());
        std::fflush(stdout);
        return;
    }
    c.clip = clip;
    c.clip_name = anim;
    c.clip_frame = 0;
    std::fprintf(stdout,
                 "[child] F%d %s child '%s' PlayAnimation '%s' frames=%zu\n",
                 frame_, owner.name.c_str(), c.name.c_str(), anim.c_str(),
                 clip->frames.size());
    std::fflush(stdout);
}

// `Vv(a)` (L516): the child whose `ab()==a`, else `vd[0]`.
sf2::scene::ChildModel* FightController::find_child(const std::string& name,
                                                    bool is_player) {
    sf2::scene::ChildModel* first = nullptr;
    for (sf2::scene::ChildModel& c : children_) {
        if (!c.active || c.is_player != is_player) continue;
        if (first == nullptr) first = &c;
        if (!name.empty() && c.name == name) return &c;
    }
    return first;
}

// `wd.awb(a)` (L518).
void FightController::play_child_animation(FightFighter& owner,
                                           const sf2::scene::MoveAction& act) {
    if (act.animation.empty()) return;  // `b==null || b==""` -> no-op
    sf2::scene::ChildModel* target = nullptr;
    switch (act.player) {
        case 4:  // `Child` -> `c = this.Vv(a.cxa)`
            target = find_child(act.child_name, owner.is_player);
            break;
        case 6:  // `EnemyChild` -> `c = this.jb.Vv(a.cxa)`
            target = find_child(act.child_name, !owner.is_player);
            break;
        default: {
            // `c = this.ef(a.pe)` (L262140) for the live-FIGHTER targets:
            //   1 (Me)     -> `this`     -> the owning fighter
            //   2 (Enemy)  -> `this.jb`  -> the opponent
            //   3 (Parent) -> `this.lb`  -> the spawner (null on a top-level
            //                               fighter, which has no `lb`)
            // `c.NS(b, b.xD(c.Fc, c.da.hd()))` (L505) then STARTS move `b` on
            // that fighter's animation controller (`da.Skb`), gated by
            // `(a.r4a || b.Yz(c))` — `ForcePlay="1"` bypasses the condition
            // test (`ai_start_move`'s `gm = !1`), else the move's own
            // `<Conditions>` run (`try_start_move`).
            FightFighter* target = nullptr;
            if (act.player == 1) {
                target = &owner;
            } else if (act.player == 2) {
                target = owner.is_player ? &enemy_ : &player_;
            }
            if (target == nullptr) {  // `lb` — no spawner on a top fighter
                std::fprintf(stdout,
                             "[child] F%d %s PlayAnimation '%s' player=%d -> "
                             "no model (this.ef)\n",
                             frame_, owner.name.c_str(), act.animation.c_str(),
                             act.player);
                std::fflush(stdout);
                return;
            }
            const sf2::scene::MoveDef* mov = nullptr;
            for (const sf2::scene::MoveDef* m : target->fighter.hb()) {
                if (m != nullptr && m->name == act.animation) {
                    mov = m;
                    break;
                }
            }
            if (mov == nullptr) {
                std::fprintf(stdout,
                             "[child] F%d %s PlayAnimation '%s' player=%d -> "
                             "no move in %s's list\n",
                             frame_, owner.name.c_str(), act.animation.c_str(),
                             act.player, target->name.c_str());
                std::fflush(stdout);
                return;
            }
            FightFighter& foe = (target == &player_) ? enemy_ : player_;
            sf2::scene::FightContext ctx;
            fill_ctx_geometry(ctx, *target, foe);
            const sf2::scene::MoveDef* was = target->fighter.current_move();
            const bool started = act.force_play
                                     ? target->fighter.ai_start_move(*mov, ctx)
                                     : target->fighter.try_start_move(*mov, ctx);
            const sf2::scene::MoveDef* now = target->fighter.current_move();
            std::fprintf(stdout,
                         "[child] F%d %s PlayAnimation '%s' player=%d -> FIGHTER "
                         "'%s' %s force=%d was='%s' now='%s'\n",
                         frame_, owner.name.c_str(), act.animation.c_str(),
                         act.player, target->name.c_str(),
                         started ? "STARTED" : "conditions failed",
                         act.force_play ? 1 : 0,
                         was != nullptr ? was->name.c_str() : "<none>",
                         now != nullptr ? now->name.c_str() : "<none>");
            std::fflush(stdout);
            return;
        }
    }
    if (target == nullptr) {
        std::fprintf(stdout,
                     "[child] F%d %s PlayAnimation '%s' child '%s' not found\n",
                     frame_, owner.name.c_str(), act.animation.c_str(),
                     act.child_name.c_str());
        std::fflush(stdout);
        return;
    }
    // `a.r4a || b.Yz(c)` gates the play; `ForcePlay="1"` bypasses the move's
    // own conditions (the port has no per-child move gate yet, so both paths
    // resolve to the same clip start).
    play_child_clip(*target, act.animation, owner);
}

// `wd.cwb(a)` (L519): `a=this.ef(a.pe); this.Uza(); this.tK.Z(a)`. `ef`:
// Me(1)=this, Enemy(2)=jb, Parent(3)=lb, Child(4)=the last `vd` entry,
// EnemyChild(6)=the enemy's child. `Pi.Kja` (L405) queues the signalled
// model for removal (`m.bd(this.gv, a)` -> `Nw`).
void FightController::delete_child_target(FightFighter& owner,
                                          const sf2::scene::MoveAction& act) {
    if (act.player == 4 || act.player == 6) {
        const bool side = (act.player == 4) ? owner.is_player : !owner.is_player;
        sf2::scene::ChildModel* c = find_child(std::string(), side);
        if (c == nullptr) return;
        const std::size_t idx = static_cast<std::size_t>(c - children_.data());
        std::fprintf(stdout, "[child] F%d %s Delete(Child) removes '%s'\n",
                     frame_, owner.name.c_str(), c->name.c_str());
        std::fflush(stdout);
        remove_child(idx);
        return;
    }
    // Me(1)/Enemy(2)/Parent(3) resolve to a live fighter model. JS
    // `wd.cwb` (L519) -> `this.tK.Z(a)` -> `Pi.Kja` (L405):
    //   `Kja(a){ a.Dfa() || this.Zw(!1); m.bd(this.gv, a) }`
    // and `wd.Dfa` (L251005) is `this.lb != null ? this.lb.Dfa() : this.xpa`
    // with `this.xpa = !0` set by the `wd` ctor (L249463). A live top-level
    // fighter therefore has `Dfa() == true`, so `Zw(!1)` (the defeat/teardown
    // signal) is SKIPPED and only `m.bd(this.gv, a)` runs — removing the
    // model from the FIGHTER-CHILD container `gv`, where a top-level fighter
    // is not a member. The JS-exact outcome for a live fighter is hence a
    // no-op on the fight state (which is what this branch reports);
    // `Cwb`-side `Uza()` (L258171) is the actor's `Cn.v_` animation-cancel
    // bus, which the port does not have.
    std::fprintf(stdout,
                 "[child] F%d %s Delete player=%d targets a FIGHTER "
                 "(JS-exact no-op: Dfa()=1 skips Zw; m.bd(gv) is child-scoped)\n",
                 frame_, owner.name.c_str(), act.player);
    std::fflush(stdout);
}

// `wd.pKa` (L517) / `Pi.Kja` (L405): an `ih` with a non-empty `cacheName`
// and a live `lb` is cleared and pushed back into the spawner's `su` cache
// (`this.lb.cache.push(this.cacheName, this)`); the slot stays in `children_`
// so its index remains stable for the recycle pool.
void FightController::remove_child(std::size_t i) {
    if (i >= children_.size()) return;
    sf2::scene::ChildModel& c = children_[i];
    if (!c.active) return;
    c.active = false;
    c.clip = nullptr;
    c.clip_frame = 0;
    if (!c.cache_key.empty()) child_cache_[c.cache_key].push_back(i);
}

void FightController::update_children() {
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (children_[i].active && children_[i].clip != nullptr) advance_child(i);
    }
}

// JS `da.ia` (L547): advance the clip one 60 Hz frame and re-sample the pose.
// When the clip ends, the child's own `Event="AnimationEnd"` actions run —
// the shipped child clips (`ShopMagicMassBomb`, moves.xml:46230) carry
// `<Delete Player="Me" Event="AnimationEnd"/>`, which removes the child.
void FightController::advance_child(std::size_t i) {
    sf2::scene::ChildModel& c = children_[i];
    const std::size_t n = c.clip->frames.size();
    if (n == 0) {
        c.clip = nullptr;
        return;
    }
    c.fighter.sample(*c.clip, c.clip_frame, c.x, c.y, c.facing);
    if (static_cast<std::size_t>(c.clip_frame) + 1 < n) {
        ++c.clip_frame;
        return;
    }
    const std::string anim = c.clip_name;
    c.clip = nullptr;
    c.clip_frame = 0;
    const sf2::scene::MoveDef* mov = nullptr;
    for (const sf2::scene::MoveDef* m : c.hb) {
        if (m != nullptr && m->name == anim) {
            mov = m;
            break;
        }
    }
    if (mov == nullptr) return;
    for (const sf2::scene::MoveAction& a : mov->actions) {
        if (a.frame_trigger || a.event != "AnimationEnd") continue;
        // Delete(Me) on a child removes the child (the shipped self-delete);
        // the other kinds are not child-scoped yet and are reported.
        if (a.kind == "Delete" && a.player == 1) {
            std::fprintf(stdout,
                         "[child] F%d child '%s' Delete(Me) at AnimationEnd -> removed\n",
                         frame_, c.name.c_str());
            std::fflush(stdout);
            remove_child(i);
            return;
        }
        std::fprintf(stdout,
                     "[child] F%d child '%s' AnimationEnd action %s (not child-scoped)\n",
                     frame_, c.name.c_str(), a.kind.c_str());
        std::fflush(stdout);
    }
}

// Probe (env `SF2_CHILD_PROBE`). The shipped `res/moves.xml` reaches 0
// `<CreatePlayer>` rows in the fight's own move lists (the census), so this
// drives one synthetic `mh`/`$l`/`Xl` triple through the exact dispatch path
// (`dispatch_move_actions` -> `spawn_child` -> `play_child_clip` ->
// `update_children` -> `Fighter::sample`/`build_vertices` -> `remove_child`)
// to prove the create -> render -> delete cycle. Pure probe: no sim state
// other than `children_`/`child_cache_` is touched.
void FightController::probe_child_cycle(bool for_player, int ticks,
                                        int* spawned, int* live_after_spawn,
                                        int* live_after_delete) {
    if (spawned != nullptr) *spawned = 0;
    if (live_after_spawn != nullptr) *live_after_spawn = 0;
    if (live_after_delete != nullptr) *live_after_delete = 0;
    if (clips_ == nullptr) return;
    FightFighter& owner = for_player ? player_ : enemy_;
    // The synthetic `mh.nx`: the spawner's first move with a FileName (its
    // clip resolves through the shared archive).
    const sf2::scene::MoveDef* pick = nullptr;
    for (const sf2::scene::MoveDef* m : owner.fighter.hb()) {
        if (m == nullptr || m->file_name.empty()) continue;
        pick = m;
        break;
    }
    sf2::scene::MoveAction create;
    create.kind = "CreatePlayer";
    create.js_type = 0;
    create.frame_trigger = true;
    create.create_name = "ProbeChild";
    create.create_cache_key = "probe_child_key";
    if (pick != nullptr) create.start_animation = pick->name;
    // The shipped `<CreatePlayer Name="Fireball">` item pair (moves.xml:
    // `<Item Type="Skeleton" Name="SkeletonMagic"/>` +
    // `<Item CopyParentType="Magic" Type="Weapon"/>`) so the app provider
    // resolves the child's OWN skeleton + the spawner's magic part
    // (`wd.ylb` L268939) instead of the spawner's merged body.
    {
        sf2::scene::MoveAction::ChildItem sk;
        sk.type = "Skeleton";
        sk.name = "SkeletonMagic";
        create.child_items.push_back(sk);
        sf2::scene::MoveAction::ChildItem mg;
        mg.type = "Weapon";
        mg.copy_type = "Magic";
        create.child_items.push_back(mg);
    }
    std::fprintf(stdout,
                 "[child-probe] spawner=%s hb=%zu clips=%zu pick=%s\n",
                 owner.name.c_str(), owner.fighter.hb().size(), clips_->size(),
                 pick != nullptr ? pick->name.c_str() : "<none>");
    std::fflush(stdout);
    const std::vector<const sf2::scene::MoveAction*> acts{&create};
    // The synthetic action carries no `<Conditions>`, so the dispatch's
    // condition gate never runs and a default context is enough.
    sf2::scene::FightContext child_ctx;
    child_ctx.qb = owner.is_player;
    dispatch_move_actions(acts, owner, "child-probe", child_ctx);
    int live = 0;
    for (const sf2::scene::ChildModel& c : children_) {
        if (c.active) ++live;
    }
    if (spawned != nullptr) *spawned = live;
    if (live_after_spawn != nullptr) *live_after_spawn = live;
    // `da.ia` ticks so the child is posed for the render path.
    for (int i = 0; i < ticks; ++i) update_children();
    for (const sf2::scene::ChildModel& c : children_) {
        if (!c.active) continue;
        std::vector<float> verts;
        c.fighter.build_vertices(verts);
        std::fprintf(stdout,
                     "[child-probe] render: child '%s' clip=%s frame=%d verts=%zu "
                     "bones=%zu\n",
                     c.name.c_str(), c.clip_name.c_str(), c.clip_frame,
                     verts.size() / 2, c.fighter.model().bones.size());
        std::fflush(stdout);
        break;
    }
    // The `Delete` half of the cycle.
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (children_[i].active) {
            remove_child(i);
            break;
        }
    }
    live = 0;
    for (const sf2::scene::ChildModel& c : children_) {
        if (c.active) ++live;
    }
    if (live_after_delete != nullptr) *live_after_delete = live;

    // --- (b) `<PlayAnimation>` aimed at a LIVE FIGHTER -------------------
    // JS `wd.awb` (L518) `pe==1|2|3` (`Me`/`Enemy`/`Parent`) resolves
    // `c = this.ef(pe)` — a live fighter — and `c.NS(b, ...)` (L505) starts
    // the move `b` on its animation controller (`da.Skb`). Drive the exact
    // path from the spawner at the OPPONENT fighter (`pe==2` -> `this.jb`),
    // which is the strongest form (a cross-fighter start).
    FightFighter& foe = for_player ? enemy_ : player_;
    const sf2::scene::MoveDef* foe_move = nullptr;
    for (const sf2::scene::MoveDef* m : foe.fighter.hb()) {
        if (m == nullptr || m->file_name.empty()) continue;
        foe_move = m;
        break;
    }
    sf2::scene::MoveAction play;
    play.kind = "PlayAnimation";
    play.js_type = 17;
    play.frame_trigger = true;
    play.player = for_player ? 2 : 1;  // `Enemy` relative to the spawner
    play.force_play = true;            // `$l.r4a` -> bypass `b.Yz(c)`
    if (foe_move != nullptr) play.animation = foe_move->name;
    const sf2::scene::MoveDef* foe_before = foe.fighter.current_move();
    std::fprintf(stdout,
                 "[child-probe] PlayAnimation fighter probe: actor=%s player=%d "
                 "anim=%s foe_was='%s'\n",
                 owner.name.c_str(), play.player, play.animation.c_str(),
                 foe_before != nullptr ? foe_before->name.c_str() : "<none>");
    std::fflush(stdout);
    if (foe_move != nullptr) {
        const std::vector<const sf2::scene::MoveAction*> play_acts{&play};
        sf2::scene::FightContext play_ctx;
        play_ctx.qb = owner.is_player;
        dispatch_move_actions(play_acts, owner, "child-probe-play", play_ctx);
        const sf2::scene::MoveDef* foe_now = foe.fighter.current_move();
        std::fprintf(stdout,
                     "[child-probe] PlayAnimation fighter probe result: %s "
                     "now='%s' frame=%d\n",
                     (foe_now != nullptr && foe_now == foe_move) ? "STARTED"
                                                                 : "not-started",
                     foe_now != nullptr ? foe_now->name.c_str() : "<none>",
                     foe.fighter.move_frame());
        std::fflush(stdout);
    }
}

// --- root `<Triggers>` (JS `Fa.Exb` L708 -> `ra.Dm`) ---------------------
// The 18 move-action kinds (`lz.create` L737-739). The port DISPATCHES the
// three audio kinds (Sound `wd.dwb` L519, RandomSound `wd.fwb` L519,
// StopSound `wd.ewb` L519 -> `ta.Jwb` L1264), `SetEndStage` (`cm`, whose JS
// `Uh()` L738 is an EMPTY no-op), ShakeScreen (`wd.Wvb` L519 -> `ql.DL`
// L370), CameraWeight (`wd.ANa` L520 -> `Pi.fS` L424 `{debugger}`),
// EnableBossAbility (`wd.$vb` L520 -> `Pi.dS` L397 `{debugger}`) and
// AddBullets (`wd.Tvb` L519 -> `hZ`/`LA`/`vZa`/`Amb`). The rest have no
// consumer subsystem in the port yet and are reported, never faked.
bool FightController::global_kind_dispatched(const std::string& kind) {
    return kind == "Sound" || kind == "RandomSound" || kind == "StopSound" ||
           kind == "SetEndStage" || kind == "ShakeScreen" ||
           kind == "CameraWeight" || kind == "EnableBossAbility" ||
           kind == "AddBullets" || kind == "HitEffect" ||
           kind == "CreatePlayer" || kind == "Delete" || kind == "PlayAnimation" ||
           // JS `Yl`/`gm`/`hm` (L728/L735/L736) -> `exec_action`'s Effect /
           // StopEffect / StopFollowEffect branches (the global `<Triggers>`
           // ships Effect 39 / StopEffect 39).
           kind == "Effect" || kind == "StopEffect" ||
           kind == "StopFollowEffect" ||
           // JS `bm`/`km`/`jm` (L733/L737): SetCooldown / ZoomEffect /
           // TryOnEnd — dispatched to the `ju` cooldown state, the `ql` lens
           // latch and the `wd.qr` TryOn-end record respectively.
           kind == "SetCooldown" || kind == "ZoomEffect" || kind == "TryOnEnd";
}

std::size_t FightController::global_action_kinds() const {
    std::size_t n = 0;
    if (global_triggers_ == nullptr) return 0;
    for (const sf2::scene::GlobalTrigger& t : *global_triggers_) n += t.actions.size();
    return n;
}

// `ra.Z6a`/`ra.yz` + `Su.nw` (L?): a global trigger joins a model's set when
// its `<Locks>` pass against that model's context. The port evaluates the
// locks twice — once per side — mirroring the per-model registration.
void FightController::register_global_triggers(const sf2::scene::FightContext& me_ctx,
                                               const sf2::scene::FightContext& enemy_ctx) {
    global_me_.clear();
    global_enemy_.clear();
    if (global_triggers_ == nullptr) return;
    for (const sf2::scene::GlobalTrigger& t : *global_triggers_) {
        if (t.locks.empty() || sf2::scene::eval_move_conditions(t.locks, me_ctx)) {
            global_me_.push_back(&t);
        }
        if (t.locks.empty() || sf2::scene::eval_move_conditions(t.locks, enemy_ctx)) {
            global_enemy_.push_back(&t);
        }
    }
    std::size_t dispatched = 0;
    for (const sf2::scene::GlobalTrigger& t : *global_triggers_) {
        for (const sf2::scene::MoveAction& a : t.actions) {
            if (global_kind_dispatched(a.kind)) ++dispatched;
        }
    }
    std::fprintf(stdout,
                 "[triggers] global <Triggers>: %zu triggers / %zu actions "
                 "(locks-pass: me %zu, enemy %zu); %zu actions dispatched\n",
                 global_triggers_->size(), global_action_kinds(), global_me_.size(),
                 global_enemy_.size(), dispatched);
    std::fflush(stdout);

    // Per-kind census of the REACHABLE data (the player's + enemy's
    // `ra.Hza` move lists and the registered global set). This is the
    // "which action kinds matter in the shipped fights" table: it is read
    // from the live move tables at fight setup, so a kind that shows 0
    // here cannot fire in this fight no matter how the fighters move.
    {
        static const char* const kKinds[18] = {
            "AddBullets",     "CameraWeight",   "CreatePlayer", "Delete",
            "Effect",         "EnableBossAbility", "HitEffect", "PlayAnimation",
            "RandomSound",    "SetCooldown",    "SetEndStage",  "ShakeScreen",
            "Sound",          "StopEffect",     "StopFollowEffect", "StopSound",
            "TryOnEnd",       "ZoomEffect"};
        const std::vector<const sf2::scene::MoveDef*>& pm = player_.fighter.hb();
        const std::vector<const sf2::scene::MoveDef*>& em = enemy_.fighter.hb();
        std::fprintf(stdout, "[triggers] action-kind census (player-move/enemy-move/"
                             "global):\n");
        for (const char* k : kKinds) {
            std::size_t cp = 0, ce = 0, cg = 0;
            for (const sf2::scene::MoveDef* m : pm) {
                for (const sf2::scene::MoveAction& a : m->actions) {
                    if (a.kind == k) ++cp;
                }
            }
            for (const sf2::scene::MoveDef* m : em) {
                for (const sf2::scene::MoveAction& a : m->actions) {
                    if (a.kind == k) ++ce;
                }
            }
            if (global_triggers_ != nullptr) {
                for (const sf2::scene::GlobalTrigger& t : *global_triggers_) {
                    for (const sf2::scene::MoveAction& a : t.actions) {
                        if (a.kind == k) ++cg;
                    }
                }
            }
            std::fprintf(stdout, "[triggers]   %-17s %4zu/%4zu/%4zu  %s\n", k, cp, ce,
                         cg, global_kind_dispatched(k) ? "DISPATCHED" : "record-only");
        }
        std::fflush(stdout);
    }
}

// The event sites the port publishes for the global set. `event_name` is the
// MOVE event name (`kz`/`tb.D6a` L763): Hit (6), Strike (7), EveryFrame
// (14), RoundStageStart (1), AnimationStart (9) and ModExpires (16) are all
// wired (see the call sites). `value` carries the event payload for the
// subclasses whose `compare` filters on it; `side` -1 = both sides.
void FightController::dispatch_global_triggers(const char* event_name, const char* why,
                                               const char* value, int side,
                                               const sf2::scene::FightContext* hit) {
    if (global_triggers_ == nullptr || (global_me_.empty() && global_enemy_.empty())) {
        return;
    }
    if (side >= 0) side &= 1;
    FightFighter* side_f[2] = {&player_, &enemy_};
    const std::vector<const sf2::scene::GlobalTrigger*>* lists[2] = {&global_me_,
                                                                    &global_enemy_};
    for (int s = 0; s < 2; ++s) {
        if (side >= 0 && s != side) continue;
        const FightFighter& owner = *side_f[s];
        const FightFighter& other = *side_f[1 - s];
        sf2::scene::FightContext ctx;
        // NOT the fight's `Da.pg` stream: the global triggers are an
        // additive evaluation the pre-existing captures never had, so a
        // `<Random>` condition here would shift every later draw. Same
        // unshared-stream rule as the lock scan in `setup_bus`.
        ctx.roll01 = [this]() { return math_random01(); };
        ctx.stage = sf2::scene::round_stage::fight;
        ctx.anims_me = anim_names_of(owner.fighter);
        ctx.anims_enemy = anim_names_of(other.fighter);
        fill_ctx_geometry(ctx, owner, other);
        ctx.health_ratio = owner.max_hp > 0.0f ? owner.hp / owner.max_hp : 0.0f;
        // The owner's live intervals (JS `Ae.xb`): the BlockEffect / HitEffect
        // global triggers gate on `<CurrentInterval Type="Block">` and
        // `<CurrentInterval Not="1" Type="Block">`. Entries carry
        // `active=true` — `FightContext::interval_active` skips inactive ones.
        for (const std::string& n : owner.fighter.active_intervals()) {
            ctx.intervals.push_back({n, owner.fighter.interval_type(n), true});
        }
        // `<CurrentInterval Player="Enemy">` reads the OTHER fighter's live
        // intervals (JS `tm.he` + `Nd.ol`).
        for (const std::string& n : other.fighter.active_intervals()) {
            ctx.intervals_enemy.push_back({n, other.fighter.interval_type(n), true});
        }
        // The landed-hit payload (JS `Bg.Ih(6,a)` passes the SAME `a` to every
        // subscriber; `sm.he` reads `a.IL`): copied so the global `<Hit>`
        // trigger conditions (CriticalEffect/BlockEffect/HitEffect) evaluate.
        if (hit != nullptr) {
            ctx.has_last_hit = hit->has_last_hit;
            ctx.last_hit_type = hit->last_hit_type;
            ctx.last_hit_animation = hit->last_hit_animation;
        }
        for (const sf2::scene::GlobalTrigger* t : *lists[s]) {
            bool event_ok = false;
            for (const sf2::scene::Cond& e : t->events) {
                if (e.type != event_name) continue;
                // The per-subclass payload filter (JS `compare`):
                //   `Sm` (ModExpires, L770): `typeof a.data!="string"&&(a="");
                //      a=this.Ki==a` — EXACT `Name` equality.
                //   `Tm` (RoundStageStart, L772): `a=a.data==this.Nta` with
                //      `Nta=iz.XBa(Ki)` — equal stage codes; the port passes
                //      the JS stage NAME (the enum is the same 1..7 map).
                //   `Km` (AnimationStart, L766): `Ki==""` passes, else `Ki`
                //      is looked up in the owner's animation-name list
                //      (`vQ(a.rb, this.Ob)`) — the port passes the started
                //      move's name and the list holds exactly that name.
                // A non-empty `Name` that does not match excludes the node.
                if (value != nullptr && !e.name.empty() && e.name != value) continue;
                event_ok = true;
                break;
            }
            if (!event_ok) continue;
            if (!t->conditions.empty() &&
                !sf2::scene::eval_move_conditions(t->conditions, ctx)) {
                continue;
            }
            std::vector<const sf2::scene::MoveAction*> acts;
            for (const sf2::scene::MoveAction& a : t->actions) {
                if (global_kind_dispatched(a.kind)) acts.push_back(&a);
            }
            if (acts.empty()) continue;
            // The dispatch runs every time the event fires; the INFO line is
            // printed once per side+trigger+event (an EveryFrame trigger
            // would otherwise print 60x/s and swamp the capture logs).
            const std::string key = owner.name + "|" + t->name + "|" + event_name;
            if (global_logged_.insert(key).second) {
                std::fprintf(stdout, "[triggers] %s %s (%s)\n", owner.name.c_str(),
                             t->name.c_str(), why);
                std::fflush(stdout);
            }
            dispatch_move_actions(acts, *side_f[s], why, ctx);
        }
    }
}

// JS `ca.i6a(a)` (L430): the `SZ` attribute name with the largest `Shift`
// (strict `>` — a tie keeps the FIRST entry). `Cgb` labels `$db` with it.
std::string i6a_attr(const std::vector<std::pair<std::string, float>>& sz) {
    if (sz.empty()) return std::string();
    std::size_t best = 0;
    for (std::size_t i = 1; i < sz.size(); ++i) {
        if (sz[i].second > sz[best].second) best = i;
    }
    return sz[best].first;
}

// JS `uf.RJa()` (L531 via `R8a`) = `Math.random()` (`at.Nlb` L114-115) — a
// stream of its OWN, NOT `Da.pg` (routing the two shock rolls through the
// shared stream desynced crit/AI). The oracle harness pins `Math.random` to
// `mulberry32(0xC0FFEE)` (reference/traces/README.md `harness.mathRandom`), so
// the native mirrors that pin — same spirit as the `Da.pg` replay seed
// (0x5F2). Process-global state: the sequence is fixed per run.
float FightController::math_random01() {
    static std::uint32_t a = 0xC0FFEEu;
    a += 0x6D2B79F5u;
    std::uint32_t t = a;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return static_cast<float>((t ^ (t >> 14))) / 4294967296.0f;
}

// JS `o1a` (L403) + `Gf` (L403-404): build one fighter. The move list is
// the TacticWeapon-based list (`weapon_subtype`) or, when `owned` is
// non-empty, the Locks-based list against the fighter's items (JS `ra.Hza`
// L684-685 — the equipment flow adds the weapon's moves).
FightFighter FightController::make_fighter(
    const std::string& nm, bool is_player, float x, float y, int max_hp,
    const std::string& weapon_subtype,
    const std::vector<sf2::scene::OwnedItem>& owned,
    bool not_ai, bool not_animation, const sf2::scene::Model* model) {
    FightFighter f;
    f.name = nm;
    f.is_player = is_player;
    // JS `xc.cM`: the fighter is built from its OWN equipment model. The
    // caller passes the enemy's model (the Punchbag) or nullptr for the
    // shared fight model.
    f.fighter.set_model(model != nullptr ? *model : model_);
    // JS `uf.sja` (L115: `floor(uf.OKa.RGa()*n) + 0`, `uf.OKa.RGa()` =
    // `Math.random` at L114, `uf.OKa=new at` L2471) is the UNSHARED stream
    // the `Gc.DK` pick draws from (`e = f[uf.sja(f.length)]` L674). Install
    // the pinned `math_random01()` (never `Da.pg`) so a multi-element `Aua`
    // max-`<Priority>` group really draws instead of always taking index 0.
    f.fighter.set_math_random([]() { return FightController::math_random01(); });
    // [FIX Phase 4b — black silhouettes] The fighters' fill color is the
    // LOCATION's Root Color (the dojo_params `<Root Color="0x000000">`),
    // not a hardcoded team color — the oracle's fighters are black
    // silhouettes (JS `Na.cd` fills the Path2D mesh with the location
    // color). set_fighter_color (the location-scene root color) is applied
    // by the fight screen after init_locks; the default here is black so
    // the standalone demos (which have no location) also draw black.
    f.fighter.set_color(fighter_color_);
    // JS `ur` L195 `QD` gates the animation attach (`Te.NS`/`da.ia` L499/
    // L505): a NotAnimation warrior never plays a clip. Skipping the lookup
    // leaves `current_clip_` null, so every later start/sample is a no-op
    // and the fighter keeps the bind pose sampled by `sample_enemy_idle`.
    if (!not_animation) {
        f.fighter.set_clip_lookup(
            [this](const std::string& name) -> const sf2::data::anim_clip* {
                const auto it = clips_->find(name);
                return it != clips_->end() ? &it->second : nullptr;
            });
    }
    // JS `Fd` (L808) + the slot enum (L2473 `I.vg="Weapon"`): the move-LIST
    // weapon subtype is the EQUIPPED Weapon slot item's `SubType` — the `Hd`
    // slot whose model `cM` (L809-810) pushes. `ra.Hza` (L684-685) then
    // admits every move whose TacticWeapon list (`Fa.Ueb` L711 -> `qx`)
    // contains it. Hardcoding "Fists" kept a knives fighter on the Fists move
    // set (and, with the hardcoded idle name, the Fists stance clip) however
    // the save was equipped. The owned list starts with the equipped slots
    // (screens.cpp `owned_items`, JS `xc.hk`), so the FIRST Weapon row IS the
    // equipped slot; an empty owned list keeps the caller's `weapon_subtype`
    // (the implicit shipped loadout: Skeleton + Weapon/<subtype> + Body/Head).
    std::string subtype = weapon_subtype;
    for (const auto& o : owned) {
        if (o.type != "Weapon") continue;
        if (!o.subtype.empty()) subtype = o.subtype;
        else if (!o.name.empty()) subtype = o.name;
        break;
    }
    if (subtype.empty()) subtype = "Fists";
    if (owned.empty()) {
        // JS `ra.Hza` (L684-685) ALWAYS tests every move's `<Locks>` against
        // the fighter's items - there is NO lock-free candidate path. The
        // direct-boot fighter (`--fight` / `--input-tape` / `--verify-input`)
        // owns the shipped default loadout: the Skeleton every fighter has
        // (JS_FLOW "users_default Skeleton=Skeleton"), the FORCED weapon
        // subtype (`weapon_subtype`, always "Fists" here), and the default
        // Body/Head armor (`reference/save.xml` items Body/Head/Fists/
        // NoRanged/NoMagic).
        //
        // Skipping the lock test here (`build_move_list`) put ALL 688
        // `TacticWeapon`-less `<Move>`s of the 1048 in moves.xml into the
        // player's list - every other weapon's step and every boss ability.
        // The tape then resolved `Up` -> `HermitStormPlayer`
        // (FileName hermit_super_attack, Priority 110) and `Back` ->
        // `GiantSwordStepBack` (giant_sword_step_back, Priority 11, whose
        // `<Locks>` require `Weapon SubType="GiantSword"`) - the reported
        // "plays the WRONG animation". The lock test excludes both.
        // The item NAMES matter: `Hm.he` (L758) compares `this.Ba == b.name`,
        // so a named lock (`<Item Type="Armor" Name="BODY_GATEKEEPER"/>`,
        // 64 occurrences in res/moves.xml) only passes for an item carrying
        // that name. The shipped default loadout's item ids are exactly
        // Body/Head/Fists/NoRanged/NoMagic (`reference/save.xml`), so the
        // name equals the subtype here.
        const std::vector<Fighter::OwnedItem> implicit = {
            {"Skeleton", "Skeleton", "Skeleton"},
            {"Weapon", subtype, subtype},
            {"Armor", "Body", "Body"},
            {"Helm", "Head", "Head"},
        };
        f.fighter.build_move_list_locks(*moves_, implicit, /*include_universal=*/true,
                                       subtype);
    } else {
        // The app layer's `owned_items` list carries the NAME of every owned
        // item (JS `Hm.he` L758 `this.Ba == b.name`), so both the direct boot
        // (`--fight`/`--verify-input`/`--input-tape`) and the Map/Dojo launch
        // build the IDENTICAL move list from the same save.
        f.fighter.build_move_list_locks(*moves_, owned, /*include_universal=*/true,
                                       subtype);
    }
    f.fighter.set_world_pos(x, y);
    f.fighter.set_enemy_x(x);  // patched each frame
    f.params.is_player = is_player;
    f.params.level = 1.0f;
    f.params.uz = 1.0f;
    f.params.m_ = 1.0f;
    f.params.xb = 0.0f;
    f.params.dta = 1.0f;
    f.params.so = 1.0f;
    // JS `xc.IY` (L191 via the `<AttributesAlign>` chain, `pGa` L198): the
    // align-armor rows `pAa` blends with. Empty -> the min/max blend
    // saturates, so every shipped fight resolves at least the `Default`
    // template's rows.
    f.params.iy = is_player ? battle_.player_align : battle_.enemy_align;
    f.params.attributes["UnarmedDamage"] =
        is_player ? battle_.player_unarmed_damage : 0.0f;
    f.params.attributes["BodyDefense"] = 0.0f;
    f.params.attributes["HeadDefense"] = 0.0f;
    f.params.attributes["CriticalChance"] = 0.0f;
    f.params.attributes["CriticalDamage"] = 0.0f;
    f.params.attributes["BlockDamageFactor"] = 0.0f;
    f.params.attributes["DamageFactor"] = 0.0f;
    // JS `wd.Fm` (L811): the player's FULL attribute map overrides the zeros
    // above (item `m7a` + group `g8a` + StartingAttributes + level ×
    // LevelAttributeGain, for every `v.eo.attributes` name). The enemy keeps
    // its warrior/rule-resolved attrs (stages.xml `<Attributes>`).
    if (is_player) {
        for (const auto& kv : battle_.player_attrs) {
            f.params.attributes[kv.first] = kv.second;
        }
    }
    // JS `xc.voice` (L807 default "", filled by `ur` L186 from the Warrior's
    // `<Voice>` attr): the `<Sound Voice="..">` gate (`fm.fka` L735 via
    // `wd.dwb` L519). The app layer resolves the two sides' Voice
    // (users_default.xml player / stages.xml stage-Warrior template).
    f.fighter.set_voice(is_player ? battle_.player_voice : battle_.enemy_voice);
    f.max_hp = static_cast<float>(max_hp);
    f.hp = f.max_hp;
    // JS `ur` L194: `Fj = NotAI==null`; the AI is created ONLY when the
    // warrior is not an AI-less dummy (and the tactic resolved).
    if (!is_player && !not_ai && tactic_ != nullptr) {
        f.ai = std::make_unique<sf2::scene::AiController>();
        // JS `de` L589: the AI's weapon pair is the fighter's OWN move-list
        // subtype (`Fd` L808) — not a hardcoded "Fists" — so a knives boss
        // resolves the Knives tactics tables.
        f.ai->init(subtype, tactics_, tactic_, moves_);
        // Diagnostic (boot, once): the JS `Da.pg` split — which AI draws
        // come from the single shared fight stream (`Da.jf`/`s4`/`dT`) and
        // which from the `Math.random` analog (`uf.sja`/`uf.RJa`/`oa.eT`).
        std::fprintf(stdout,
                     "[ai] streams: Da.pg(draw01) <- QJa/gfa/aea/dqb/jL/"
                     "slots/XW/crit; Math.random(pinned) <- R8a shock "
                     "(uf.RJa), Gc.DK reaction pick (uf.sja)\n");
        std::fflush(stdout);
    }
    // The strike-memory half-life is the tactic's `<Memory Strikes>` for the
    // AI side; a fighter with no AI has `model.nf == null` -> `kfa()` = 0
    // (full decay, JS `decay` with half_life 0 -> 2^-inf = 0).
    f.fighter.strike_memory().set_half_life(
        tactic_ != nullptr ? tactic_->memory_strikes : 0.0);
    return f;
}

void FightController::set_bounds(float wall, float wall_max, float floor_y) {
    wall_min_ = wall;
    wall_max_ = wall_max;
    floor_y_ = floor_y;
    camera_.wall = wall;
    camera_.floor = floor_y;  // the visible floor line (the camera anchor)
    camera_.arena_w = wall_min_ + wall_max_;  // the RAW location width (JS Lb.width)
}

// JS `nj` (L885, ERuleRingout): the off-screen-marker rule. The native sim
// has no ERuleRingout engine (stages.xml `<Ringout .../>` is not parsed), so
// the host supplies the bounds (`min_x`/`max_x` = `ZG`/`BH`) and speed
// (`speed` = `tta`, SequentionSpeed). The arrows are shown while a round is
// live (JS `f_a` L897) and hidden at the round-end cleanup (JS `$_a` L427 ->
// `onb`/`pnb` L828). Presentation only - no RNG, no sim effect.
void FightController::set_ringout_rule(bool enabled, float min_x, float max_x,
                                       float speed) {
    ringout_rule_ = enabled;
    ringout_min_ = min_x;
    ringout_max_ = max_x;
    ringout_speed_ = speed;
    // JS `f_a` (L897) runs every frame while the rule is active: enabling
    // mid-round shows the arrows now; disabling hides them immediately.
    if (enabled && phase_ == fight_phase::fight) {
        fx_.show_offscreen_markers(ringout_min_, ringout_max_, ringout_speed_,
                                   kFightViewW, kFightViewH, camera_.floor);
    } else if (!enabled) {
        fx_.hide_offscreen_markers();
    }
}

// --- stage <Rules> engine (JS `du` L894-910) ----------------------------
// JS `Ga.wfa` (L848): the fired rule's winner. Li==1 -> (Yu?2:1); Li==2 ->
// (Yu?1:2); else 3. `E3a` (L412) then does `a=false; case 1: a=true` (wfa==1
// = player/kc wins; any other = enemy/Zb). `Yu` (Ga ctor L846) is true for
// the field rules and false only for the rules whose ctors set `Yu=!1`:
// TimeOutWin `qj` (L912), WinCombo `rj` (L912), WinShock `sj` (L913),
// WinStyle `tj` (L913), Points `gj` (L872), Combo `$m` (L852), Crazy `an`
// (L854). LoseFall `jn` (L866) does NOT clear `Yu` -> stays true (the native
// previously grouped it with the Win*/TimeOutWin rules — fixed).
bool FightController::rule_winner_is_player(const FightRule& r) const {
    bool yu = true;
    switch (r.kind) {
        case FightRuleKind::timeout_win:
        case FightRuleKind::win_combo:
        case FightRuleKind::win_shock:
        case FightRuleKind::win_style:
            yu = false;
            break;
        default:
            break;
    }
    int wfa = 3;
    if (r.apply_to == 1) {
        wfa = yu ? 2 : 1;
    } else if (r.apply_to == 2) {
        wfa = yu ? 1 : 2;
    }
    return wfa == 1;
}

// JS `nj.Zk` (L885) + `nj.hh` (L885-886): the tracked fighter node leaves
// [ZG,BH]x[dN,HO] -> `setActive(false)` + fire. `oy=-location.width/2`,
// `eC=-location.ct`; the node is `Jc.oa.Ic(ON)` (player) / `QI...` (bot).
// The native reduces the node lookup to the fighter's world anchor (the
// model pivot NPivot, which is now the port's anchor). The rule fires
// only with a Node name (JS `ga==null` -> false) and only for the axis set
// by Axis= (the other axis keeps the +/-1E5 defaults, so it never triggers).
bool FightController::rules_ringout_detect(FightRule& r) {
    if (r.node.empty()) return false;  // JS `this.ga == null` (L885)
    const float oy = -camera_.arena_w * 0.5f;   // JS `this.oy` (L885)
    const float eC = -floor_y_;                 // JS `this.eC` (= -location.ct)
    const FightFighter* f = (r.apply_to == 2) ? &enemy_ : &player_;
    // JS `nj.Zk` (L885): `this.ga = a.Jc.oa.Ic(this.ON)` — resolve the
    // named node in the fighter's merged model; `nj.hh` reads `this.ga.ma`
    // (the node's WORLD position). The old code always used the COM anchor;
    // now a non-pivot `Node` name (e.g. "NToeTip_1"/"NKnee_1"/"COM") is
    // resolved by name. "NPivot" (the shipped Ringout node) resolves to the
    // world anchor (positions() already places the pivot there).
    float nx = f->fighter.world_x();
    float ny = f->fighter.world_y();
    const int bi = f->fighter.model().bone_by_name(r.node);
    if (bi >= 0) {
        const std::vector<float>& pos = f->fighter.positions();
        const std::size_t o = static_cast<std::size_t>(bi) * 2;
        if (o + 1 < pos.size()) {
            nx = pos[o];
            ny = pos[o + 1];
        }
    }
    const float x = nx + oy;   // JS `a.x + this.oy`
    const float y = -ny + eC;  // JS `-a.y + this.eC`
    const bool outside =
        x > r.max_x || x < r.min_x || y > r.max_y || y < r.min_y;
    if (outside) {
        r.active = false;  // JS `setActive(false)` (L886)
        return true;
    }
    return false;
}

// JS `en.Zk` (L859) + `ZZa` (L860-861): the HotGround `<Node>` zone test.
// `Zk` resolves each zone's node on the tracked fighter (`a.oa.Ic(name)`)
// and sets `oy=-location.width/2`. `ZZa` returns TRUE iff EVERY zone has the
// node OUTSIDE it: `d=(node.x+oy, -node.y, node.z, 1)` and, per zone,
// `!(x>=N||x<=J||y>=W||y<=P)` -> inside -> false. The native resolves the
// bone by name (world coords, like Ringout); an unknown name falls back to
// the fighter anchor (defensive; shipped nodes exist).
bool FightController::rules_hot_zones_out(const FightRule& r,
                                          const FightFighter& f) {
    const float oy = -camera_.arena_w * 0.5f;  // JS `this.oy` (L859)
    for (const FightRule::HotZone& z : r.hot_zones) {
        float nx = f.fighter.world_x();
        float ny = f.fighter.world_y();
        const int bi = f.fighter.model().bone_by_name(z.name);
        if (bi >= 0) {
            const std::vector<float>& pos = f.fighter.positions();
            const std::size_t o = static_cast<std::size_t>(bi) * 2;
            if (o + 1 < pos.size()) {
                nx = pos[o];
                ny = pos[o + 1];
            }
        }
        const float x = nx + oy;  // JS `d.x + this.oy`
        const float y = -ny;      // JS `-d.y` (no `eC` for HotGround)
        const bool outside =
            x >= z.max_x || x <= z.min_x || y >= z.max_y || y <= z.min_y;
        if (!outside) return false;  // JS `return!1` (node inside a zone)
    }
    return true;  // JS `return!0` (every node outside every zone)
}

// JS `jn.hh` (L867) + `Rba` (L866-867): LoseFall. The JS rule registers the
// per-frame (1) and animation-start (4) passes, plus the fall-reaction pass
// (7) only when `Mwa("Physical")` (L866, the ctor's conditional `Zf(7)`).
//   cp==4 (`Pf` L387 -> `PC(4,side)`): `tN = this.Lba(a.AI)` (L867; `Lba`
//     L848 compares the current animation name against `EM`). The native
//     maps `a.AI` to `current_move()->name` (the established anim proxy) and
//     re-derives `tN` every frame — JS only re-arms on an animation START;
//     equivalent while the animation persists (documented drift).
//   cp==7 (`Lgb` L387: `a.model.lb==null` -> `PC(7,side)`; from
//     `wd.Lwb`/`Qnb` L507/L511): `tN=!0`. The native uses the per-fighter
//     `reaction_fall` pulse (a Fall reaction started by `try_react` in
//     `apply_hit`); the JS `lb==null`/ragdoll `Qnb` gate has no native
//     counterpart (see drift in `rules_lose_fall`).
//   cp==1: `Rba` — the tracked node leaves [ZG,BH]x[dN,HO] -> `setActive(false)`.
bool FightController::rules_lose_fall(FightRule& r) {
    FightFighter& f = (r.apply_to == 2) ? enemy_ : player_;
    // cp==4: `tN = this.Lba(a.AI)` (L867) — the current animation match.
    const std::string anim = f.fighter.current_move() != nullptr
                                 ? f.fighter.current_move()->name
                                 : std::string();
    bool match = false;
    for (const std::string& n : r.animations) {
        if (n == anim) { match = true; break; }
    }
    r.armed = match;
    // cp==7 (registered only when the rule owns a "Physical" animation,
    // `jn` ctor L866): `tN=!0` on the physical fall reaction (L867). The JS
    // event is `ca.Lgb` (L387: `a.model.lb==null -> PC(7,side)`), fired from
    // `wd.Lwb`/`Qnb` (L507/L511) — `Gc.DK` (L673-674) sets the knockdown
    // `qs`/`jJa`. The native has no ragdoll `Qnb`; the per-fighter
    // `reaction_fall` pulse (a Fall reaction started by `try_react` in
    // `apply_hit`, the `DK` analog) stands in. Replaces the old KO-state
    // approximation (`weapon=="Fists" && hp<=0`).
    if (r.physical && f.reaction_fall) r.armed = true;
    if (!r.armed) return false;
    if (r.node.empty()) return false;  // JS `this.ga == null` (L866)
    float nx = f.fighter.world_x();
    float ny = f.fighter.world_y();
    const int bi = f.fighter.model().bone_by_name(r.node);
    if (bi >= 0) {
        const std::vector<float>& pos = f.fighter.positions();
        const std::size_t o = static_cast<std::size_t>(bi) * 2;
        if (o + 1 < pos.size()) {
            nx = pos[o];
            ny = pos[o + 1];
        }
    }
    const float oy = -camera_.arena_w * 0.5f;  // `oy = -location.width/2`
    // JS `eC = -location.Tza` (L866-867); the native has no `Tza` field —
    // the floor anchor (`location.ct`, used by Ringout) stands in. No effect
    // on shipped data: LoseFall's zone is a tautology once armed (see below).
    const float eC = -floor_y_;
    const float x = nx + oy;
    const float y = -ny + eC;
    // `jn` ctor (L866) + `of(a,1E5,-1E5)` (L867) make the shipped Player
    // rules (`Min` absent -> 1E5, `Max=30`) an always-true zone, so the rule
    // fires as soon as the fall animation arms `tN`.
    const bool outside =
        x > r.max_x || x < r.min_x || y > r.max_y || y < r.min_y;
    if (outside) r.active = false;  // `setActive(false)` (L867)
    return outside;
}

// JS `du.Oob` (L901) + `ca.BT` (L392-393): record the fired rule's round
// result. Ringout -> `ey=4` (Death attr kills the tracked side first:
// `a.gra && this.Oe.jT(a.mc())`); TimeOutWin -> `ey=2`. The winner is the
// LAST fired rule (`Pu` overwritten per `Oob`, `BT` sets `ey`).
void FightController::rules_fire(FightRule& r) {
    switch (r.kind) {
        case FightRuleKind::ringout: {
            if (r.death) {
                FightFighter* t = (r.apply_to == 2) ? &enemy_ : &player_;
                t->hp = 0.0f;  // JS `jT(mc)`: `b.du(0)` -> hp 0
            }
            rule_result_ = round_result::ringout;  // JS `BT` L392
            break;
        }
        case FightRuleKind::timeout_win:
            rule_result_ = round_result::timeout_win;  // JS `BT` L393
            break;
        case FightRuleKind::hot_ground: {
            // `Oob` (L901): `a.gra && this.Oe.jT(a.mc())` (death attr ->
            // that side hp 0), then `BT(a)` -> `ey=2` (`qj`/`en` path).
            if (r.death) {
                FightFighter& t = (r.apply_to == 2) ? enemy_ : player_;
                t.hp = 0.0f;
            }
            rule_result_ = round_result::timeout_win;  // JS `BT` -> ey=2
            break;
        }
        case FightRuleKind::regeneration: {
            // `Oob` (L901): `c = kVa; c /= on(); VOa(mc,c)` — heal the
            // rule's side. `VOa && BT` only when the healed fighter is
            // already DEAD (`aM` returns `Jfa()`, L199-199): unreachable
            // while the round is live, so Regeneration never ends a round.
            FightFighter& f = (r.apply_to == 2) ? enemy_ : player_;
            f.hp = std::min(f.max_hp, f.hp + r.regen_rate);
            return;
        }
        case FightRuleKind::life_steal:
            // Heal is applied in `rules_on_hit` (it needs the hit damage
            // `TZ`); `Oob`'s `BT` is dead-gated like Regeneration.
            return;
        case FightRuleKind::points: {
            // `Oob` (L901): `HU(qH,gN)` (HUD), and `a.EV && BT(a)` -> the
            // Score Max was reached (ey=2). The winner is `gj.wfa` (L872):
            // the firing copy's own counter reached Max -> that side wins.
            rule_winner_player_ = (r.apply_to == 1);
            rule_result_ = round_result::timeout_win;  // JS `BT` -> ey=2
            rule_pending_ = true;  // JS `Pu = a`
            return;
        }
        case FightRuleKind::win_combo:
        case FightRuleKind::win_shock:
        case FightRuleKind::lose_fall:
            // `Oob` (L901-902): `case "ERuleLoseFall": ... this.Oe.BT(a)` ->
            // `BT` (L392-393) sets `ey=2`. The winner is resolved by
            // `wfa()` (LoseFall `Yu=true` -> the enemy wins).
            rule_result_ = round_result::timeout_win;  // JS `BT` -> ey=2
            break;
        default:
            return;  // effect OPEN (Darkness/RandomArea/... not modelled)
    }
    rule_winner_player_ = rule_winner_is_player(r);
    rule_pending_ = true;  // JS `Pu = a`
}

// JS `ca.ia` L389 `PC(1,3)` -> `du.Ih(1,3,ze)` (L896) + `ca.Onb` L412
// (`Ema(9); ud.Ih(9,3,ze)` at the HUD timer 0). Detection runs while the
// round is live.
void FightController::rules_frame() {
    if (!round_live_) return;
    // `du.Ih(1,3,ze)` (L896): the per-frame pass over `tX` (the `Zf(1)`
    // rules: Darkness, HotGround, LoseFall, Regeneration, RandomArea).
    // Ringout (`nj`) is the shipped one; HotGround/Regeneration now fire:
    for (FightRule& r : rules_) {
        if (!r.active) continue;
        bool fire = false;
        switch (r.kind) {
            case FightRuleKind::ringout:
                fire = rules_ringout_detect(r);
                break;
            case FightRuleKind::hot_ground: {
                // `en.hh` (L859-860) cp==1. `Voa` = the tracked fighter's
                // current animation ∈ the rule's `EM` list (case 4
                // `Voa=this.Lba(a.AI)`, L860). The native re-derives `Voa`
                // each frame and treats an animation CHANGE as the cp==4
                // edge (`haa=!1`). If (`Voa && ZZa()`) the timer resets to
                // full once (`haa||(Qe=Tra,jc=0,haa=cK=!0)`); otherwise the
                // countdown runs: `jc += 1/rO` (`rO=a.hNa=on()=1`), and every
                // 60 ticks `Qe>0 && (Qe--, cK=true)`. Fires on `Qe<=0`.
                FightFighter& f = (r.apply_to == 2) ? enemy_ : player_;
                const std::string anim =
                    f.fighter.current_move() != nullptr
                        ? f.fighter.current_move()->name
                        : std::string();
                if (anim != r.hot_anim) {  // cp==4: `haa=!1`
                    r.hot_anim = anim;
                    r.hot_haa = false;
                }
                bool voa = false;
                for (const std::string& n : r.animations) {
                    if (n == anim) { voa = true; break; }
                }
                if (voa && rules_hot_zones_out(r, f)) {
                    if (!r.hot_haa) {
                        r.hot_time = r.frames / 60;  // `Tra=Frames/60|0`
                        r.hot_frac = 0.0f;
                        r.hot_haa = true;
                        r.hot_changed = true;
                    }
                } else {
                    r.hot_frac += 1.0f;
                    if (r.hot_frac >= 60.0f) {
                        r.hot_frac = 0.0f;  // JS `jc=0` (not `-=60`)
                        if (r.hot_time > 0) {
                            --r.hot_time;
                            r.hot_changed = true;  // spawn effect (`o_a`)
                        }
                    }
                }
                fire = r.hot_time <= 0;
                break;
            }
            case FightRuleKind::regeneration:
                // `kj.hh` (L882) cp==1: `jc++`; `if (MUa && a.pw) break`;
                // `jc>=DUa -> true` (fires EVERY frame once reached — only
                // `Zk`/the hit reset clears `jc`). `a.pw` (`U0`) is false
                // with shipped data (no WeaponStrike attr).
                ++r.regen_counter;
                fire = r.regen_counter >= r.regen_frames_after_hit;
                break;
            case FightRuleKind::lose_fall:
                // `jn.hh` cp==1 (L867): `return this.Rba()` — armed by the
                // animation (cp==4) / Physical fall (cp==7) passes.
                fire = rules_lose_fall(r);
                break;
            default:
                break;
        }
        if (fire) rules_fire(r);
    }
    // JS `ca.ia` L412: `this.ha.PEa()` (NF<=0) -> `Ema(9); ud.Ih(9,3,ze)`;
    // `qj.hh` (L912) is `return true`, so an active TimeOutWin fires.
    if (round_.time_nf <= 0) {
        for (FightRule& r : rules_) {
            if (r.active && r.kind == FightRuleKind::timeout_win) {
                rules_fire(r);
                break;
            }
        }
    }
}

// JS `du.f_a` L896-897 (`ERuleRingout -> this.Oe.H1a(ZG,BH,tta)`) ->
// `ca.H1a` L390 -> `sXa` L827-828. The FIRST active Ringout rule wins
// (`a||(a=g,...)`); none -> hide the markers.
void FightController::rules_show_markers() {
    for (const FightRule& r : rules_) {
        if (r.active && r.kind == FightRuleKind::ringout) {
            set_ringout_rule(true, r.min_x, r.max_x,
                             static_cast<float>(r.sequention_speed));
            return;
        }
    }
    set_ringout_rule(false, ringout_min_, ringout_max_, ringout_speed_);
}

// JS `ca.F1` L428 -> `du.rob(round>0?round:1)` L900 (mxa/cz/osb/qob/rmb) +
// `du.f_a` L896-897. Per-round: reload the rule set, apply the `kI`/`Ti`
// gates (`du.osb` L898), then the marker pass.
void FightController::rules_begin_round(int round) {
    rule_round_ = round > 0 ? round : 1;  // JS `rob` L900
    rule_pending_ = false;
    rules_ = battle_.rules;
    // JS `dl.jh()` (L1421): the ACTIVE rule list is `p.o.Yh ? this.CV
    // : this.Ae`, and `dl.OK` (L1423-1424) routes each parsed rule by its
    // `Lb.mode` (`Lb.MIa` L847: `Eclipse` absent -> 2; `Eclipse="1"` -> 0;
    // `Eclipse="0"` -> 1):
    //   mode 0 -> the `CV` (eclipse-only) list; mode 1 -> `Ae` (normal
    //   list); mode 2 -> both lists.
    // So an `Eclipse`-tagged rule runs ONLY in its matching eclipse state.
    // The port parsed `eclipse_mode` but never enforced it, so the BOSS_LYNX
    // bot's `<Attributes Eclipse="1" WarriorPower="32" ApplyTo="Bot"/>` ran
    // every round outside the eclipse, adding +32 `UnarmedDamage`/`BodyDefense`
    // (and every other `v.wv` name) each round. That inflated the defender's
    // `pAa` `e = defender.attr(defense_attr)` and collapsed the player's
    // balance term toward 0 (dmg 0.11 -> 0.019 -> 0.0024 -> 0.00).
    const bool eclipse = fight_params().eclipse;
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
                       [eclipse](const FightRule& r) {
                           return !(r.eclipse_mode == 2 ||
                                    (eclipse ? r.eclipse_mode == 0
                                             : r.eclipse_mode == 1));
                       }),
        rules_.end());
    // JS `du.osb` (L898): `active = kI(cz) && Ti()`. `cz` = the round
    // (`rob(round>0?round:1)` L900, called by `ca.F1` L428 — for round 1 in
    // the ctor and for every `round.round>=2` in `IKa` L417, so every round
    // gates). `Ti` = the power range `[xFa,wFa]` vs `p.o.bb()` = the save's
    // current warrior level (`xf.bb` L129409 = `Ca.level`); the port's
    // analog is the per-fighter `FighterParams.level` (make_fighter = 1.0).
    const long power = static_cast<long>(player_.params.level);
    for (FightRule& r : rules_) {
        r.active = fight_rule_gate(r, rule_round_, power);
    }
    // JS `cl.pmb`: each `ERuleRandom` (`pn`) picks ONE child via `pn.M4` —
    // `b = eligible.length; b = b*Da.pg.jf()|0; CB = eligible[b]` — and only
    // that child's rules stay active (`pn.setActive`/`A$`). `M4`'s filter is
    // `Ti()` (power range) only; the Round `kI` gate is the `osb` pass above
    // (`fight_rule_gate`). `Refresh="EachRound"` (`pn.Zsa==2`) re-draws every
    // round; otherwise the choice is cached for the fight. `draw01()` is the
    // shared fight stream (JS `Da.pg.jf()`, L2352) - the OWNED `DaPrng`
    // unless a caller injected an override (the demo/probe path).
    {
        std::map<int, std::vector<int>> groups;  // group -> distinct choices
        for (const FightRule& r : rules_) {
            if (r.random_group < 0) continue;
            std::vector<int>& ch = groups[r.random_group];
            bool seen = false;
            for (int c : ch) {
                if (c == r.random_choice) { seen = true; break; }
            }
            if (!seen) ch.push_back(r.random_choice);
        }
        // JS `cl.pmb` (L1413): `Da.IT(this.ob!=null?this.ob.Qm:
        // 2147483647*Da.pg.jf()|0)` - `$Ja` runs this for EVERY fight, so the
        // port draws once and reseeds ONCE per fight (before any `pn.M4`
        // pick), even when the battle carries no ERuleRandom group. The
        // shipped single fights have no persisted replay record
        // (`this.ob == null`), so the seed is the random path: ONE shared
        // draw (`Da.pg.jf()`) scaled to 31 bits, then `Da.pg.sL(seed)`.
        if (!random_pick_done_) {
            const int seed = static_cast<int>(
                2147483647.0 * static_cast<double>(draw01()));
            reseed_stream(seed);
        }
        for (const auto& kv : groups) {
            const int gid = kv.first;
            const std::vector<int>& choices = kv.second;
            bool each_round = false;
            for (const FightRule& r : rules_) {
                if (r.random_group == gid && r.random_each_round) each_round = true;
            }
            if (!each_round && random_pick_done_ &&
                random_pick_.find(gid) != random_pick_.end())
                continue;  // `EachFight`: keep this fight's pick
            // `pn.M4` eligible = choices whose `Ti()` (power range) holds.
            std::vector<int> eligible;
            for (int c : choices) {
                for (const FightRule& r : rules_) {
                    if (r.random_group == gid && r.random_choice == c &&
                        r.power_min <= power && power <= r.power_max) {
                        eligible.push_back(c);
                        break;
                    }
                }
            }
            int pick = -1;
            if (!eligible.empty()) {
                const float roll = draw01();  // `Da.pg.jf()`
                int idx = static_cast<int>(
                    static_cast<float>(eligible.size()) * roll);  // `*|0`
                if (idx < 0) idx = 0;
                if (idx >= static_cast<int>(eligible.size()))
                    idx = static_cast<int>(eligible.size()) - 1;
                pick = eligible[idx];
            } else if (!choices.empty()) {
                pick = choices[0];  // JS `a$.length>0` reset-retry (no re-roll)
            }
            random_pick_[gid] = pick;
        }
        random_pick_done_ = true;
        for (FightRule& r : rules_) {
            if (r.random_group < 0) continue;
            const auto it = random_pick_.find(r.random_group);
            if (it != random_pick_.end() && r.random_choice != it->second)
                r.active = false;  // `pn.CB`: only the chosen child is active
        }
    }
    rules_apply_round_effects();  // JS `du.F1(a)` L897 (Zk) + `kZ` + `m_a`
    rules_show_markers();
}

// JS `ca.$_a` L427 -> `du.Iwb` L898 (`c.stop()` on every active rule) +
// `ca.onb` L390 (`pnb` L828: remove both arrows).
void FightController::rules_end_round() {
    for (FightRule& r : rules_) r.active = false;
    rule_pending_ = false;
    set_ringout_rule(false, ringout_min_, ringout_max_, ringout_speed_);
}

// JS `du.F1(a)` (L897) + `du.kZ` (L902) + `du.m_a` (L902-903): the per-round
// apply pass. `a` = the `eu` context built in `ca.cYa` (L428: `Jc=yb`,
// `QI=pb`, `DA=kc`, `Bda=Zb`), so `Jc`/`QI` are the two fighters and
// `DA`/`Bda` their parameter maps.
void FightController::rules_apply_round_effects() {
    // JS `I0a` (L409) resets `Iga=!1` at round start; the loop below
    // re-sets it when an active InvertJoystick rule runs (`F1` L897).
    invert_joystick_ = false;
    for (FightRule& r : rules_) {
        if (!r.active) continue;
        switch (r.kind) {
            case FightRuleKind::attributes: {
                // `Zi.Zk` (L849-850): `DA`(player)/`Bda`(bot) attr += (d|0).
                std::map<std::string, float>& attrs =
                    (r.apply_to == 2) ? enemy_.params.attributes
                                      : player_.params.attributes;
                for (const auto& kv : r.attr_adds) {
                    attrs[kv.first] += static_cast<float>(kv.second);
                }
                break;
            }
            case FightRuleKind::remove_interval: {
                // `lj.Zk` (L883): `a.Jc.oY(a9)` (player) / `a.QI.oY(a9)`.
                FightFighter& f = (r.apply_to == 2) ? enemy_ : player_;
                f.fighter.clear_intervals(r.remove_interval_type, "");
                break;
            }
            case FightRuleKind::recharge_magic_each_round:
                // `F1` -> `Oe.fmb(d.mc())` (L397) -> `wd.yKa` (L504).
                reset_magic_fighter((r.apply_to == 2) ? enemy_ : player_);
                break;
            case FightRuleKind::tactic:
                // `F1` -> `Oe.Gqb(d.CVa)` (L397) -> `this.pb.s5(a)` (L399):
                // always the ENEMY (`pb`).
                if (enemy_.ai != nullptr && tactic_defs_ != nullptr) {
                    const auto it = tactic_defs_->find(r.tactic_name);
                    if (it != tactic_defs_->end()) {
                        enemy_.ai->set_tactic(&it->second);
                    }
                }
                break;
            case FightRuleKind::hot_ground:
                // `en.Zk` (L859) -> `reset()` (L859): `Qe=Tra=Frames/60|0`,
                // `jc=0`, `cK=true`. (`Zk` also resolves each `<Node>` zone
                // on the tracked fighter and sets `oy`; the native resolves
                // nodes per-frame in `rules_hot_zones_out` instead.)
                r.hot_time = r.frames / 60;
                r.hot_frac = 0.0f;
                r.hot_changed = true;
                break;
            case FightRuleKind::regeneration:
                r.regen_counter = 0;  // `kj.Zk` (L882)
                break;
            case FightRuleKind::points:
                r.points_self = 0;  // `gj.reset` (L872)
                break;
            case FightRuleKind::invert_joystick:
                // `F1` (L897): `ERuleInvertJoystick -> this.Oe.Iga=!0`.
                invert_joystick_ = true;
                break;
            case FightRuleKind::invulnerability:
                // `gn.Zk` (L863): `this.ws=!0`.
                r.ws = true;
                break;
            case FightRuleKind::combo:
                // `$m.Zk` (L852) -> `De.Zk` -> `compare(ze)` -> `hh`.
                // `ca.cob` (L417) resets `ze` just before `cYa`/`F1(a)`, so
                // `NZ=0` at this point -> `ws = (0 < pV)`.
                r.ws = (0.0f < r.combo_value);
                break;
            case FightRuleKind::crazy:
                // `an.Zk` (L854) -> `hh`: `ze` reset -> `xP=0` ->
                // `ws = (0 < Upa)`.
                r.ws = (0.0f < static_cast<float>(r.crazy_style));
                break;
            default:
                break;  // Darkness/RandomArea resets are presentation-only
        }
    }
    // `du.kZ(3)` (L902): the `wV` (bit 10) AND per side -> the opposite
    // fighter's `ola(!b)`.
    rules_kz(3);
    // `du.m_a` (L902-903): Resistance -> per-fighter `dta`. The JS `g` is
    // the SAVE's resistance count (`p.o.Pw.c0(eta)`, a `Dt` over the save
    // `<Resistances>`); the port has no save-resistance table -> 0. With
    // `g=0` every shipped `<Resistance>` (none in stages.xml) would scale.
    float c = 1.0f, d = 1.0f;
    const float lT = 500.0f;  // JS `v.lT` ResistanceDoublingRange (A2)
    for (const FightRule& r : rules_) {
        if (!r.active || r.kind != FightRuleKind::resistance) continue;
        const float h = r.resist_value;
        const float g = 0.0f;  // p.o.Pw.c0(r.resist_name) — no native source
        if (g < h) {
            c *= std::pow(2.0f, (g - h) / lT);
            d *= std::pow(2.0f, (h - g) / lT);
        }
    }
    player_.params.dta = c;  // `a.zla(c)` (L903)
    enemy_.params.dta = d;   // `b.zla(d)` (L903)
}

// JS `du.kZ` (L902): for one side, `b = AND(!e.ws)` over the active `wV`
// (bit 10) rules of that side, then the OPPOSITE fighter's `ola(!b)` (which
// sets `wd.ws` — the shock/pain immunity flag, read by `wd.R8a`
// `Orb(this.ws?0:b)` L521). `wV` is populated by `De` (L852), the base of
// Invulnerability `gn` (L863), Combo `$m` (L852) and Crazy `an` (L854).
// `side` 3 -> `kZ(1); kZ(2)`; `Oob` also calls `kZ(mc)` when a Combo/Crazy
// `ws` flips (L901-902).
void FightController::rules_kz(int side) {
    if (side == 3) {  // `kZ(3)` -> both (L902)
        rules_kz(1);
        rules_kz(2);
        return;
    }
    bool b = true;
    for (const FightRule& r : rules_) {
        if (!r.active || r.apply_to != side) continue;
        if (r.kind == FightRuleKind::invulnerability ||
            r.kind == FightRuleKind::combo ||
            r.kind == FightRuleKind::crazy) {
            b = b && !r.ws;  // `b = b && !e.ws` (L902)
        }
    }
    FightFighter& other = (side == 1) ? enemy_ : player_;  // `Rea(a==1?2:1)`
    other.shock.weapon_ws = !b;                            // `ola(!b)`
}

// JS `ca.Cgb`'s `PC(5/6,...)` (L396) -> `du.Ih(5/6, side, ze)` (L896) and
// `ca.Ihb`'s `PC(11,...)` (L423): the landed-hit rule pass. `atk_side` is
// the `Ih` `b` argument, so only rules whose `mc()` matches (or is All)
// run — mirroring `f.mc()!=b && f.mc()!=3 && b!=3`.
void FightController::rules_on_hit(FightFighter& atk, FightFighter& def,
                                   const sf2::scene::HitRecord& rec) {
    if (!round_live_) return;
    // JS `Cgb` (L396): `a.model` is the target, so `PC(5, targetSide)` resets
    // the target's Regeneration counter (`kj.hh` cp==5, L882), while
    // `PC(6, attackerSide)` runs LifeSteal/Points/WinShock and `PC(11,
    // attackerSide)` (`Ihb` L423) runs WinCombo — all on the attacker side.
    const int atk_side = atk.is_player ? 1 : 2;
    const int def_side = def.is_player ? 1 : 2;
    for (FightRule& r : rules_) {
        if (!r.active) continue;
        if (r.kind == FightRuleKind::regeneration) {
            if (r.apply_to == def_side || r.apply_to == 3) {
                r.regen_counter = 0;  // `Ih(5, targetSide)`
            }
            continue;
        }
        if (r.apply_to != atk_side && r.apply_to != 3) continue;
        switch (r.kind) {
            case FightRuleKind::life_steal: {
                // `bj.hh` (L865): `cp==6 -> T8 = TZ*nUa; T8!=0`; `Oob`
                // (L901) heals `mc` by `T8/on()`. `TZ` = the dealt damage.
                const float t8 = rec.final_damage * r.lifesteal_part;
                if (t8 != 0.0f) {
                    atk.hp = std::min(atk.max_hp, atk.hp + t8);
                }
                break;
            }
            case FightRuleKind::points: {
                // `gj.compare` (L872) -> `Uwa` (L873): on the side that dealt
                // damage (`a.cp==6 && a.t1`), require the Block/Critical/
                // Shock/Defense filters, then ++own counter. Score fires at
                // `Max` (`EV`), Contest only decides at the timer (`cp==9`).
                bool ok = true;
                if (r.points_has_block && rec.blocked != r.points_block) ok = false;
                if (r.points_has_crit && rec.critical != r.points_crit) ok = false;
                if (r.points_has_shock &&
                    def.shock.shocked_vc != r.points_shock) ok = false;
                if (r.points_defense == 0) ok = ok && rec.head_hit;
                else if (r.points_defense == 1) ok = ok && !rec.head_hit;
                if (ok) {
                    ++r.points_self;
                    if (r.points_type == 1 &&
                        static_cast<float>(r.points_self) >= r.points_max) {
                        rules_fire(r);  // Sets `Pu`/`ey=2` via BT
                    }
                }
                break;
            }
            case FightRuleKind::combo: {
                // `$m.hh` (L852) via `PC(11, side)` (`ca.Ihb` L423): the
                // attacker's `ws = (NZ < pV)`; a flip re-runs `kZ(mc)`
                // (`De.Vwa` returns changed -> `Oob` -> `kZ`, L852/L901-902).
                const bool nw =
                    (static_cast<float>(atk.combo_run) < r.combo_value);
                if (r.ws != nw) {
                    r.ws = nw;
                    rules_kz(r.apply_to);
                }
                break;
            }
            case FightRuleKind::win_combo:
                // `rj.hh` (L912): `NZ` = the combo run -> `BT` (`ey=2`).
                if (static_cast<float>(atk.combo_run) >= r.win_combo_value) {
                    rules_fire(r);
                }
                break;
            case FightRuleKind::win_shock:
                // `sj.hh` (L913): `a.C3` = the OPPONENT's shock -> `BT`.
                if (def.shock.shocked_vc) {
                    rules_fire(r);
                }
                break;
            default:
                break;
        }
    }
    // `ca.Ihb` (L423) fires for BOTH fighters' combo trackers: the target's
    // run resets to 0 on every landed hit, so its Combo `ws` is re-derived
    // (`PC(11, targetSide)`) and the `kZ` mutuality re-runs on a flip.
    for (FightRule& r : rules_) {
        if (!r.active || r.kind != FightRuleKind::combo) continue;
        if (r.apply_to != def_side) continue;
        const bool nw = (static_cast<float>(def.combo_run) < r.combo_value);
        if (r.ws != nw) {
            r.ws = nw;
            rules_kz(r.apply_to);
        }
    }
}

// Modes setup path (tournament/survival): rounds/time/recovery, per-side
// DamageFactor rules, NoBullets flag, enemy rebuild. Runs post-init,
// pre-first-update (round_start consumes battle_).
void FightController::apply_mode_setup(const ModeSetup& setup) {
    battle_.rounds = setup.rounds;
    battle_.round_time = setup.round_time;
    battle_.health_recovery = static_cast<float>(setup.health_recovery);
    player_.params.attributes["DamageFactor"] +=
        static_cast<float>(setup.player_damage_factor);
    set_no_bullets_replenish(setup.no_bullets);
    // Enemy rebuild from the resolved warrior (items/tactic/attrs/perks).
    const float ex = enemy_.fighter.world_x();
    const float ey = enemy_.fighter.world_y();
    const int emax =
        enemy_.max_hp > 0.0f ? static_cast<int>(enemy_.max_hp) : 100;
    enemy_ = make_fighter(enemy_.name, false, ex, ey, emax, "Fists",
                          setup.enemy.owned);
    for (const auto& kv : setup.enemy.attrs) {
        enemy_.params.attributes[kv.first] += static_cast<float>(kv.second);
    }
    enemy_.params.attributes["DamageFactor"] +=
        static_cast<float>(setup.enemy_damage_factor);
    if (!setup.enemy.tactic.empty() && enemy_.ai != nullptr &&
        tactic_defs_ != nullptr) {
        const auto it = tactic_defs_->find(setup.enemy.tactic);
        if (it != tactic_defs_->end()) enemy_.ai->set_tactic(&it->second);
    }
    perk_setup_.enemy_refs.clear();
    for (const std::string& pn : setup.enemy.perk_names) {
        sf2::scene::ItemPerkRef ref;
        ref.name = pn;
        perk_setup_.enemy_refs.push_back(std::move(ref));
    }
    sample_enemy_idle();
    rebuild_body(player_, enemy_);
    rebuild_body(enemy_, player_);
    setup_bus(perk_setup_);
    init_magic();
    // JS `wd.K0` (L505): the enemy's equipped `NoRanged` item (`parameters.ig`,
    // type `I.Vh`; `vzb` L108540 maps the type to the name "NoRanged"). The
    // app resolves the stage warrior's items to `OwnedItem` rows
    // (`ModeEnemy.owned`); a `NoRanged` row (name or subtype) means `K2 = +1`.
    // With an empty/unresolved list this leaves `ranged_available = true`
    // (K2 = -1) — reported as the exact missing input rather than guessed.
    for (const auto& ow : setup.enemy.owned) {
        if (ow.name == "NoRanged" || ow.subtype == "NoRanged" || ow.type == "NoRanged") {
            enemy_.ranged_available = false;
        }
    }
    std::fprintf(stdout,
                 "[ai] enemy ranged_available=%d -> K2=%d (owned items=%zu)\n",
                 enemy_.ranged_available ? 1 : 0,
                 enemy_.ranged_available ? -1 : 1, setup.enemy.owned.size());
    std::fflush(stdout);
}

// JS `xF` (L388): set the fight phase and sync the fighters' `Je` stance
// (the move conditions' RoundStage reads it).
void FightController::set_phase(fight_phase p) {
    phase_ = p;
    // RoundStageStart publish (slot 1, Ep matches qk.Je per side).
    for (int q = 0; q < 2; ++q) {
        bus_.fire(1, sf2::scene::TrigVars(), true, q, cond_ctx(0), cond_ctx(1), oba_phase(phase_), frame_);
        std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> pairs;
        bus_.drain(0, pairs); bus_.drain(1, pairs);
        for (const auto& pr : pairs) exec_action(pr.first, pr.second, q);
    }
    player_.fighter.set_enemy_x(enemy_.fighter.world_x());
    enemy_.fighter.set_enemy_x(player_.fighter.world_x());
}

// JS `tx` (L407): round init — the timer is the round length, Vt=false.
// The HUD timer (`Sf`) resets `xU = gma*60+1` (L2036); the first phase-2
// tick decrements it to gma*60 (`NF = gma`).
void FightController::round_init() {
    round_.running = false;
    round_.length = battle_.rounds;     // Da.pT (Rounds)
    round_.gma = battle_.round_time;    // Da.R4 (RoundTime)
    round_.time_xu = round_.gma * 60 + 1;
    round_.time_nf = round_.gma;
    round_live_ = false;
    start_stance_done_ = false;
    end_stance_frames_ = 0;
}

// JS `Z2` (L408-409): the AUTOMATIC round advance. Reached from the
// round-end epilogue (JS `Onb` L411 else branch: `ZK(); NA(); Z2()`) and
// from the round plate chain `ca.tx` (L407) -> `Ar.wca` (L2019) ->
// `Cr.wca` (L2023, `type=1; ONa()`) -> `ca.vhb` (L410) case 1 -> `Z2()`.
// The JS increments the round, re-syncs everything, raises the ROUND N
// break plate (`this.ha.tca(this.round.round,!1)`) whose expiry calls
// `FNa()` (phase 1, L409). No host click exists anywhere in this path.
void FightController::round_start() {
    // Snapshot the pre-round HP (JS Pm/vo) — the recovery is applied in
    // between_rounds_recover() when the previous round ends.
    round_.number++;   // JS `this.round.round++` (L409)
    round_init();
    // JS `IKa` L417 / ctor `F1` (L381): the rule pass runs at round SETUP,
    // before the StartStance, so `Iga` (InvertJoystick) is already live for
    // the phase-1 buffered press (`N0a` L426 -> `LBa` L399). `du.osb` (L898)
    // gate = `kI(round) && Ti()` (InvertJoystick ships with no Round/Level
    // attrs, so it always passes).
    invert_joystick_ = false;
    {
        const int rr = round_.number > 0 ? round_.number : 1;
        const long power = static_cast<long>(player_.params.level);
        for (const FightRule& r : battle_.rules) {
            if (r.kind == FightRuleKind::invert_joystick &&
                fight_rule_gate(r, rr, power)) {
                invert_joystick_ = true;
                break;
            }
        }
    }
    // Per-round magic reset (`yKa`: bh=0 + InitialCharge; raid persists).
    init_magic();
    // Per-round bus re-register (JS tb.Yka L401/402/405: cKa + Gf both
    // sides — live timed mods do NOT persist across rounds).
    setup_bus(perk_setup_);
    dga_ = false;  // JS `Dga` reset per round (L409)
    // JS `Z2` (L409): `this.Iga=this.kh=this.Dga=this.JJ=!1; this.ey=0;
    // this.Pu=null; this.m$=!1;` — the per-fighter round-over latch `kh`
    // (latched by `E3a` L413) is cleared for the new round.
    player_.kh = false;
    enemy_.kh = false;
    // JS `wd.wI` per-round re-init: `Wx=-1`, `sr=0`; PLUS the shock/disarm
    // latches `vc`/`sn`, which the JS clears at every round boundary
    // (`NA` L414 `c.sn=!1;c.vc=!1` and the per-fighter `Z2` ->
    // `Mtb(){this.vc=!1}` via `MHa` L409). The old port comment claiming
    // `vc`/`sn` persist across rounds was WRONG.
    player_.shock.pain_sr = 0.0f;
    player_.shock.weapon_wx = -1;
    player_.shock.shocked_vc = false;
    player_.fighter.set_shock_latch(false);  // `oa.vc` reset
    player_.shock.disarm_sn = false;
    enemy_.shock.pain_sr = 0.0f;
    enemy_.shock.weapon_wx = -1;
    enemy_.shock.shocked_vc = false;
    enemy_.shock.disarm_sn = false;
    // Reset the round flags on the fighters (JS `c.parameters.nob()`).
    player_.fighter.set_enemy_x(enemy_.fighter.world_x());
    enemy_.fighter.set_enemy_x(player_.fighter.world_x());
    // JS `ha.tca(this.round.round, !1)` (L409) -> `Cr.tca` (L2023): type 2,
    // `fu(1.666)`, `wU` cleared and re-armed by the 500 ms `wh.delay`. The
    // expiry dispatches through `ca.vhb` (L410) case 2 -> `FNa` (phase 1).
    cur_banner_ = banner_kind::round;
    banner_time_ = kJsBannerRoundBreakSeconds;      // JS `fu(1.666)`
    banner_total_ = kJsBannerRoundBreakSeconds;
    banner_armed_ = false;                          // `tca` clears `wU` ...
    banner_arm_delay_ = kJsBannerArmDelaySeconds;   // ... for 500 ms
    banner_action_ = banner_action::begin_round;    // vhb case 2 -> `FNa`
    banner_start_ = frame_;
    banner_round_ = round_.number;
    std::fprintf(stdout, "[fight] banner: ROUND %d (F%d)\n", banner_round_ + 1, frame_);
    std::fflush(stdout);
    // NOTE: phase 1 does NOT start here — the JS `FNa` (L409) runs when the
    // break plate expires (`banner_expire`), exactly as `vhb` case 2 does.
}

// JS `FNa` (L409): phase 1 — fighters at their spawn, no input yet.
void FightController::enter_start_stance() {
    // JS `FNa` (L409) starts the phase-1 clock: `frame_ == 0` coincides with
    // phase 1 so the frame-indexed fight timelines (drivers, oracle mapping)
    // stay phase-local (the plate lead-in ran at frame 0 in the idle phase).
    frame_ = 0;
    // Respawn the fighters at their spawn positions (JS `tja`/`Qlb`).
    // [FIX Phase 4a — fighters on the floor; Wave U pivot anchor] The dojo
    // spawn Y (-110/-93, the ModelsViewer Y) is the PivotNode world y:
    // `Fighter::sample` anchors the model's PivotNode bone (`Dl.Ic(v.wya)`,
    // `internal_settings.xml` `<PivotNode Name="NPivot"/>`) at the spawn, so
    // the clip's ground-contact bones rest on the dojo floor line
    // (dojo_params Floor="80"). The old COM anchor sat the pivot ~17 units
    // high (bag: 226).
    // [FIX round-start positions] `set_world_pos` alone is UNDONE by the very
    // next `sample()`: fighter.hpp L443-447 documents that with an active move
    // the anchor is rebuilt as `world_x_ = px[anchor] + render_offset_ +
    // j8_x_`, so the spawn was silently discarded and the fighters kept the
    // previous round's x after the ROUND plate. The JS writes an ABSOLUTE
    // anchor — the ctor `this.kc.position = location.Yia` / `this.Zb.position
    // = location.B_` (L381) and the boss advance `this.Zb.position =
    // location.B_` (`mfb`, L405) — which is exactly what `teleport` does (it
    // absorbs the delta into `render_offset_` so the anchor STICKS).
    player_.fighter.teleport(battle_.player_spawn_x, battle_.player_spawn_y);
    enemy_.fighter.teleport(battle_.enemy_spawn_x, battle_.enemy_spawn_y);
    sample_idle(player_);
    sample_enemy_idle();
    rebuild_body(player_, enemy_);
    rebuild_body(enemy_, player_);
    set_phase(fight_phase::start_stance);
    round_live_ = false;
    start_stance_done_ = false;
    start_stance_frames_ = 0;   // reset so every round re-plays the intro
    start_buffer_filled_ = false;  // fresh round, empty round-start buffer
    round_wait_ = false;   // the break plate expired -> the round is running
    // JS `FNa` (L409): `this.Ta.XF(!0); this.xF(1)`. The JS re-shows the
    // scene at the START of `FNa` because its round-reset reposition (`Z2`)
    // already ran while hidden; the port performs that reposition HERE (the
    // `teleport` above), so the re-show runs AFTER it — the teleport still
    // executes inside the hidden window (the whole function is one frame).
    set_scene_visible(true);
    // Root `<Triggers>` `RoundStageStart` (`kz.create` `Tm` L772, whose
    // `parse` maps `Name` through `iz.XBa` L447: StartStance=1, Fight=2,
    // EndStance=3, ...). The port publishes the JS stage NAME, which is the
    // same 1..7 map as `sf2::scene::round_stage`.
    dispatch_global_triggers("RoundStageStart", "RoundStageStart", "StartStance");
}

// JS `Rkb` (L410): phase 2 — the round goes live (HUD play() sets
// round.Vt = true, the timer starts).
void FightController::enter_fight() {
    set_phase(fight_phase::fight);
    round_live_ = true;
    round_.running = true;   // the timer counts down (JS Sf.play L2037)
    start_stance_done_ = true;
    // JS `f_a` (L896-897): with the round live, the FIRST active
    // `ERuleRingout` rule feeds the two `sXa` arrows
    // (`this.Oe.H1a(a.ZG, a.BH, a.tta)`). The rules come from the stage's
    // `<Rules>` parsed into battle_.rules (apply_stage_ringout_rule); the
    // per-round `kI`/`Ti` gates run in `rob` (L900) before `f_a`. Presentation
    // only - set_ringout_rule never touches the simulation / RNG.
    rules_begin_round(round_.number);
    // [FIX idle-slide] Cut the intro stance clip (stance_1/stance_2,
    // root-moving) so the fighters don't keep sliding 853 units into the
    // idle phase. The intro clip was re-triggered at f130 because its
    // 129-frame duration is 4 short of the 133-frame StartStance, so a
    // right-facing fighter entered Fight still on stance_2 (delta -148.6).
    // Clearing forces the next update_fighter to pick the static
    // FistsStartStanceIdle-Left (fists1_stance_idle, 38f, delta 0).
    player_.fighter.clear_move();
    enemy_.fighter.clear_move();
    // [FIX transition continuity — Root 2] Do NOT pre-pose the fighters with
    // `sample_idle` here. `Fighter::sample` also advances the ragdoll solver
    // state (`sol_ma_`/`sol_mf_`/`sol_prev_com_`), so pre-posing the idle
    // clip's frame 0 leaves `sol_ma_`/`sol_mf_` straddling a full intro→idle
    // pose jump; the idle move's prepend is then built as
    // `ma ± 1.5·(ma-mf)` from that jump and the first sampled idle frame
    // overshoots (idle cf=2 weapon bones +267 world units). The JS has no
    // such cut: the oracle trace (reference/traces/console.run4.log) keeps
    // the intro clip through the transition frame and steps into the idle
    // smoothly (root-relative 19.6 units), while the port's pre-pose broke
    // the pose by 353 units. Leaving the pose at the intro's end keeps the
    // solver space continuous; the idle move starts on the next update.
    // The NotAnimation dummy still needs its bind pose.
    if (battle_.enemy_not_animation) sample_enemy_idle();
    rebuild_body(player_, enemy_);
    rebuild_body(enemy_, player_);

    // JS `llb` (L429): replay the StartStance input buffer as if the player
    // pressed NOW — `WC != -1 && (c.yJa(c.WC), c.WC = -1)`. The buffered tap
    // joins the player's key buffer; the next update_fighter picks it up via
    // try_select_move (the manual input path), so the press made before the
    // round is not lost.
    if (start_buffer_filled_) {
        player_.fighter.input(start_buffer_key_, press_type::tap);
        start_buffer_filled_ = false;
    }
    // Root `<Triggers>` `RoundStageStart Name="Fight"` (`Tm` L772; the
    // stage code 2). Published at the phase-2 transition, the only site the
    // round-stage machine raises (`kg` handler L412/L387 -> `Rkb`).
    dispatch_global_triggers("RoundStageStart", "RoundStageStart", "Fight");
}

// JS `ca.o1a` L403 (`Da.type=="FightNone" ? a() : ...`) + `ca.kg` L387
// (`this.eu==1&&a&&(this.fxa(), this.Da.type!="FightNone" ? this.Am() :
// this.xF(2))`): the Dojo hub's `FightNone` viewer goes straight to phase 2.
// No StartStance wait, no FIGHT!/ROUND plate (`Am` is not taken), no round
// timer (`Rkb`/`Sf.play` is not reached) and no round flow (`Onb`/`E3a` are
// gated off in `update`). `xF(2)` also arms the virtual gamepad
// (`Za.F().nla(!0)` -> `Za.F().isVisible=true`).
void FightController::enter_fight_none() {
    fight_none_ = true;
    set_scene_visible(true);  // JS `xF(2)` -> `Za.F().nla(!0)`: isVisible=true
    set_phase(fight_phase::fight);
    round_live_ = true;      // `xF(2)` makes the fight phase live for hits/fx
    round_.running = false;  // NO `Sf.play()`: the round timer never ticks
    start_stance_done_ = true;
    // Same idle hand-off as `enter_fight` (drop the intro stance clip so the
    // static idle plays; keep the NotAnimation bag at bind pose).
    player_.fighter.clear_move();
    enemy_.fighter.clear_move();
    if (battle_.enemy_not_animation) sample_enemy_idle();
    rebuild_body(player_, enemy_);
    rebuild_body(enemy_, player_);
    // No plate: `xF(2)` skips `Am()`; the ROUND 1 plate raised by `init_locks`
    // belongs to the battle flow (JS `ggb` -> `swb` -> `FNa`).
    cur_banner_ = banner_kind::none;
    banner_time_ = 0.0f;
    banner_total_ = 0.0f;
    banner_armed_ = false;
    banner_arm_delay_ = 0.0f;
    banner_action_ = banner_action::none;
    round_wait_ = false;
    // `xF` dispatches the phase to the fighters (RoundStageStart slot 1 via
    // `set_phase`) and the root `<Triggers>` like `enter_fight` does.
    dispatch_global_triggers("RoundStageStart", "RoundStageStart", "Fight");
}

// JS `i4a` (L409): phase 3 — the round's EndStance (results shown).
void FightController::enter_end_stance() {
    set_phase(fight_phase::end_stance);
    round_.running = false;
    round_live_ = false;
    end_stance_frames_ = 0;
    // JS `$_a` (L427) -> `onb()`/`pnb` (L828) -> `du.Iwb` (L898): the
    // ringout arrows are removed at the round-end cleanup and every active
    // rule is stopped. `rules_end_round` clears the marker + rule set.
    rules_end_round();
    // Root `<Triggers>` `RoundStageStart Name="EndStance"` (`Tm` L772,
    // stage code 3) — 3 of the 4 shipped RoundStageStart triggers use it.
    dispatch_global_triggers("RoundStageStart", "RoundStageStart", "EndStance");
}

    // JS `Onb` (L411): the round-end check. KO when a fighter's hp <= 0;
// timeout ONLY when the fight has the TimeoutWin rule (JS `BT` L392 sets
// `ey=2` for ERuleTimeoutWin; the shipped stages use no timeout rule).
// The timeout winner is the ENEMY (JS E3a c==3 branch: `a.ng++` on Zb).
// JS `Onb` (L411): the round-end check. A fired stage rule (`Pu != null`)
// ends the round first (JS `ca.ia` L412: `... || this.Pu != null ...` ->
// `E3a(vfa(!0), vfa(!1), this.ey)`), with its `ey`/winner from `BT`/`wfa`.
// Otherwise KO when a fighter's hp <= 0; timer 0 only with a TimeOutWin rule
// (handled by `rules_frame`; the fallback below keeps the old behaviour).
void FightController::check_round_end() {
    if (!round_live_) return;
    // JS `ca.ia` L412: `this.Pu != null` -> the round ends with `this.ey`;
    // `E3a` L412-413 picks the winner from `Pu.wfa()` (any of c==2/3/4).
    if (rule_pending_) {
        rule_pending_ = false;
        const FightFighter& w = rule_winner_player_ ? player_ : enemy_;
        const FightFighter& l = rule_winner_player_ ? enemy_ : player_;
        apply_round_result(rule_result_, w, l);
        return;
    }
    // JS `Ar.PEa` (L2020): `mb.NF<=0` — the HUD counter hit zero. JS
    // `ca.ia` L412 ends the round at `ha.PEa()` for ANY fight (`Da.type !=
    // "FightNone"`), so the timeout does NOT require a TimeOutWin rule (the
    // native port used to gate on `battle_.timeout_rule`). The winner then
    // comes from `E3a` L412-413 below (a Points `Pu.wfa()`, a TimeOutWin rule
    // forcing `wfa()=1` -> player, else `Zb` = the ENEMY).
    bool has_points_rule = false;
    for (const FightRule& r : rules_) {
        if (r.active && r.kind == FightRuleKind::points) {
            has_points_rule = true;
            break;
        }
    }
    const bool timeout = round_.time_nf <= 0;
    const bool player_ko = player_.hp <= 0.0f;
    const bool enemy_ko = enemy_.hp <= 0.0f;

    if (!player_ko && !enemy_ko && !timeout) return;

    if (timeout) {
        if (has_points_rule) {
            // `gj.wfa` (L872): Contest `qH>gN` (the player copy vs the enemy
            // copy). `E3a` L412-413 c==2/3/4 uses `Pu.wfa()`.
            const FightRule* p1 = nullptr;
            const FightRule* p2 = nullptr;
            for (const FightRule& r : rules_) {
                if (!r.active || r.kind != FightRuleKind::points) continue;
                if (r.apply_to == 1) p1 = &r;
                else if (r.apply_to == 2) p2 = &r;
            }
            const int qH = p1 != nullptr ? p1->points_self : 0;
            const int gN = p2 != nullptr ? p2->points_self : 0;
            const bool player_wins = qH > gN;
            apply_round_result(round_result::timeout_win,
                               player_wins ? player_ : enemy_,
                               player_wins ? enemy_ : player_);
        } else if (battle_.timeout_rule) {
            // JS `qj` (L912): TimeOutWin forces `Li=1`, `Yu=false` ->
            // `wfa()`=1 -> `E3a` `a=true` -> the PLAYER (kc) wins.
            apply_round_result(round_result::timeout_win, player_, enemy_);
        } else {
            // JS `E3a` (L413) with `ey==3` and no fired rule (`Pu==null`):
            // `a=false` -> the winner is `Zb` (the ENEMY). The oracle's
            // BOSS_LYNX timeout is a player loss (`kk` "You lose!").
            apply_round_result(round_result::timeout_win, enemy_, player_);
        }
    } else if (player_ko && enemy_ko) {
        // Both KO'd the same frame: higher HP wins (JS vfa L413).
        const FightFighter& w = round_winner_by_hp();
        apply_round_result(round_result::ko, w, w.is_player ? enemy_ : player_);
    } else if (player_ko) {
        apply_round_result(round_result::ko, enemy_, player_);
    } else {
        apply_round_result(round_result::ko, player_, enemy_);
    }
}

// JS `E3a` (L412) + `vfa` (L413): apply the round result — the winner's
// rounds-won (`ng`) increments, both fighters get the winner flags, and
// the round enters phase 3. When the winner has won `round.eL` rounds
// (Rounds), the battle ends (JS Onb `a` -> bea).
void FightController::apply_round_result(round_result result, const FightFighter& winner,
                                         const FightFighter& loser) {
    RoundOutcome oc;
    oc.result = result;
    oc.round_number = round_.number;
    oc.winner = &winner;
    oc.loser = &loser;
    oc.player_hp = player_.hp;
    oc.enemy_hp = enemy_.hp;
    switch (result) {
        case round_result::ko:
            oc.reason = "KO";
            break;
        case round_result::timeout_win:
            oc.reason = "TIMEOUT";
            break;
        case round_result::ringout:
            oc.reason = "RINGOUT";
            break;
        default:
            oc.reason = "ROUND";
            break;
    }

    // The winner's rounds-won (JS `a.ng++`).
    FightFighter& w = winner.is_player ? player_ : enemy_;
    FightFighter& l = winner.is_player ? enemy_ : player_;
    w.rounds_won++;
    w.is_winner = true;
    l.is_winner = false;

    // JS `E3a` (L413): `a.kh=!0; b.kh=!0;` — both fighters latch the
    // round-over flag, which gates the attack pass in `ca.Hnb` (L389).
    player_.kh = true;
    enemy_.kh = true;

    // JS `Onb` (L411) FIRST statement: `this.Ta.XF(!1)` — hide the whole
    // 3-D view (the fighters + the location draw) for the round transition.
    // It stays hidden through `ZK()`/`NA()`/`Z2()` (the round-reset
    // reposition) until `FNa` (L409) re-shows it, so the reset never draws a
    // visible teleport. The HUD (the ROUND/K.O. plate + bars) keeps drawing.
    set_scene_visible(false);

    // The K.O. finish plate (JS `Cr.GZ` L2024, type 6/7, `fu(1.166)`).
    // `GZ` has no `ca.vhb` (L410) case, so its expiry does NOT dispatch —
    // but the port uses it as the end-stance hold: JS advances the round
    // when the end-stance animation finishes (`kg` L387 `h4a` -> `Ewb`
    // L404 -> `h9` -> `Onb` L411 `ZK(); NA(); Z2()`), and the port has no
    // end-stance clip, so the plate's `fu(1.166)` hold stands in for it.
    if (result == round_result::ko) {
        banner_show(banner_kind::ko, kJsBannerHoldSeconds,
                    banner_action::next_round, false);
        std::fprintf(stdout, "[fight] banner: K.O. (F%d)\n", frame_);
        std::fflush(stdout);
    }

    enter_end_stance();
    history_.push_back(oc);

    // Battle end: the winner reached `round.eL` (Rounds) — JS Onb
    // `a = wo.nB.ng >= round.eL` -> `a ? bea(nB)`.
    const bool battle_end = w.rounds_won >= round_.length;
    if (battle_end) {
        end_battle(w);
    } else if (cur_banner_ == banner_kind::ko) {
        // The K.O. plate holds the break (see above); its expiry runs
        // `NA()` + `Z2()` through `banner_expire`.
        round_wait_ = true;
    } else {
        // JS `Onb` (L411) else branch: `this.Ta.XF(!1), this.ZK(),
        // this.NA(), this.Z2()` — the next round starts AUTOMATICALLY.
        // There is no host "Next" button in the JS; the round-break plate
        // raised by `Z2` (`Cr.tca` L2023) holds the round until `FNa`.
        round_wait_ = true;
        between_rounds_recover();   // JS `NA` (L414)
        round_start();              // JS `Z2` (L408) via `tx`/`wca`/`vhb`
    }
}

// JS `bea` (L413): the battle end — the winner is fixed, the fight stops.
void FightController::end_battle(const FightFighter& winner) {
    battle_over_ = true;
    winner_ = &winner;
    round_.running = false;
    round_live_ = false;
    round_wait_ = false;
    // The final banner: VICTORY for the player's win, DEFEAT for the loss
    // (presentation only; no `fu` timer — the results screen takes over).
    banner_show(winner.is_player ? banner_kind::victory : banner_kind::defeat,
                0.0f, banner_action::none, false);
    // JS `tl.fB` (L844) / `ca.kD`: the effect containers drain at the battle
    // end (`fB()` -> `Gq.fB()`/`Hq.fB()`).
    magic_fx_.clear();
    std::fprintf(stdout, "[fight] banner: %s (F%d)\n",
                 winner.is_player ? "VICTORY" : "DEFEAT", frame_);
    std::fflush(stdout);
}

// JS `NA` (L414): the between-round recovery. The game heals BOTH fighters
// by `Da.qDa` (HealthRecovery, default 1) — `c.jT(this.Da.qDa)` — so the
// fighters keep their damaged HP between rounds (NOT a full reset).
// It also clears the per-fighter round latches: `c.sn=!1; c.vc=!1; ...
// c.kh=!1` — the disarm/shock latches do NOT survive a round boundary.
void FightController::between_rounds_recover() {
    const float recover = battle_.health_recovery;
    player_.hp = std::min(player_.max_hp, player_.hp + recover);
    enemy_.hp = std::min(enemy_.max_hp, enemy_.hp + recover);
    // Reset the round flags (JS NA: zd/br/cE/kh/sn/sJ/pw/Iq).
    player_.is_winner = false;
    enemy_.is_winner = false;
    for (FightFighter* f : {&player_, &enemy_}) {
        f->shock.disarm_sn = false;    // `sn`
        f->shock.shocked_vc = false;   // `vc`
        f->fighter.set_shock_latch(false);  // `oa.vc` reset
        f->kh = false;                 // `parameters.kh`
    }
    // JS `Cn.$K()` (L297964): at the round boundary every strike-memory
    // accumulator is scaled by the tactic's `<Memory RoundFactor>` (`mt()`
    // = `KW.Q4`), then the model's strike clock `lU` resets (`reset()`
    // L253395: `this.dz=this.lU=this.sI=this.sr=0`). Commit the pending
    // buffer (`v_`) first so `$K` scales settled values.
    for (FightFighter* f : {&player_, &enemy_}) {
        double q4 = 10.0;  // `Iu` ctor default; overridden by the tactic
        if (f->ai != nullptr) q4 = f->ai->memory_round_factor();
        f->fighter.strike_memory().round_factor(q4);
        f->fighter.strike_memory().set_time(0.0);
    }
}

// JS `vfa` (L413): the round winner by HP.
const FightFighter& FightController::round_winner_by_hp() const {
    if (player_.hp >= enemy_.hp) return player_;
    return enemy_;
}

void FightController::sample_idle(FightFighter& f) {
    // [FIX pre-stance pose — JS `Te.NS`/`da.Ua == null`] A fighter with no
    // clip playing holds the BIND pose: the JS fighter's nodes are never posed
    // before the first `Skb` (the oracle's first fight frames — clip `null` —
    // are exactly the model's bind pose, e.g. yb bones[18]=690.000,
    // bones[30]=672.318, bones[31]=710.761, i.e. the XML bind deltas
    // -17.682/+20.761 about NPivot). The old body POSED the fighter with
    // `fists1_stance_idle` frame 0, which is not in the JS and left
    // `sol_ma_` (the JS `currentNode.ma` analog that `Te.Gub` L557-559 reads
    // as the align reference `e`) 23.8 units off the bind pose — the residual
    // intro-stance offset. `sample` with a 1-frame, bone-less clip leaves
    // every bone at its bind position (the same hub path the NotAnimation
    // dummy uses).
    sf2::data::anim_clip bind_clip;
    bind_clip.frames.resize(1);
    f.fighter.sample(bind_clip, 0, f.fighter.world_x(), f.fighter.world_y(),
                     f.fighter.facing());
}

void FightController::sample_enemy_idle() {
    // JS `QD` (NotAnimation, L195/L499): the dummy holds its BIND pose.
    // `Fighter::sample` with a 1-frame clip whose frame has no bones leaves
    // every bone at its bind position — the exact hub path
    // (screens.cpp:3510-3512). Otherwise the enemy uses the stance idle.
    if (battle_.enemy_not_animation) {
        sf2::data::anim_clip bind_clip;
        bind_clip.frames.resize(1);
        enemy_.fighter.sample(bind_clip, 0, enemy_.fighter.world_x(),
                              enemy_.fighter.world_y(), enemy_.fighter.facing());
        return;
    }
    sample_idle(enemy_);
}

void FightController::rebuild_body(FightFighter& f, const FightFighter& foe) {
    // The foe model/pose enables the `wBa` enemy-bone fallback (WEA_STATIC
    // §3): edge endpoints missing locally resolve against the foe skeleton.
    f.body.build(f.fighter.model(), f.fighter.positions(), wall_min_, wall_max_,
                 &foe.fighter.model(), &foe.fighter.positions());
}

// --- perk trigger bus (`tb`) -------------------------------------------
// `ZOa` (L398-399): rebuild live trigger sets from the PerkSetup and
// register both sides (`Gf`). Enemy refs are usually empty (enemy gear
// is not modeled — OPEN).
void FightController::setup_bus(const PerkSetup& perks) {
    // Full reset first (JS `reset()`+`pP(true)`+`JNa` path + `Yka` re-gf):
    // live state reverts (JG/Ly/Qz/collision/timescale/color/dots), then
    // the bus clears and both sides re-register fresh.
    for (FightFighter* f : {&player_, &enemy_}) {
        f->jg = sf2::scene::Vec3{1.0f, 1.0f, 1.0f};
        f->params.ly = 0.0f;
        f->qz = 1.0f;
        f->collidable = true;
        f->fighter.set_time_scale(1.0f);
        f->fighter.set_color(fighter_color_);
        f->dots.clear();
    }
    bus_.clear();
    bus_.log = [](const std::string& line) {
        std::fprintf(stdout, "[perk] %s\n", line.c_str());
        std::fflush(stdout);
    };
    tactic_defs_ = perks.tactics;
    player_items_ = perks.player_items;
    enemy_items_ = perks.enemy_items;
    // JS `wd.K0` (L505): `parameters.ig != null && parameters.ig.Yb ==
    // "NoRanged" ? 1 : -1`. `parameters.ig` is the equipped item of the
    // NoRanged type (`vzb` L108540 maps type `I.Vh` -> name "NoRanged").
    // The shipped equipment carries an equipped `NoRanged` item, so
    // `ranged_available == false` -> `K2 = +1` (the old hardcoded -1 was
    // inverted). Derived from the equipped item names the caller supplies.
    auto has_noranged = [](const std::vector<std::string>& items) {
        for (const std::string& n : items) {
            if (n == "NoRanged") return true;
        }
        return false;
    };
    player_.ranged_available = !has_noranged(player_items_);
    enemy_.ranged_available = !has_noranged(enemy_items_);
    if (perks.catalog == nullptr) return;
    bus_.register_side(
        0, sf2::scene::build_side_triggers(perks.player_refs, *perks.catalog, bus_.log),
        player_items_);
    bus_.register_side(
        1, sf2::scene::build_side_triggers(perks.enemy_refs, *perks.catalog, bus_.log),
        enemy_items_);

    // Root `<Triggers>` (JS `Fa.Exb` L708 -> `ra.Dm`): lock-filter the global
    // set per side against the same items/perks the perk bus uses. The lock
    // evaluation reuses the MOVE condition evaluator (`ra.yz` -> `Su.nw` ->
    // `Ha.he`, i.e. `Tl` conditions: `<Perk>`/`<Item>`/`<ModExists>`/...).
    {
        using FC = sf2::scene::FightContext;
        auto fill = [](FC& ctx, const std::vector<std::string>& own_items,
                       const std::vector<std::string>& foe_items,
                       const std::vector<sf2::scene::ItemPerkRef>& own_refs,
                       const std::vector<sf2::scene::ItemPerkRef>& foe_refs) {
            for (const std::string& n : own_items) ctx.items.push_back(FC::item_info{"", "", n});
            for (const std::string& n : foe_items) {
                ctx.items_enemy.push_back(FC::item_info{"", "", n});
            }
            for (const sf2::scene::ItemPerkRef& r : own_refs) {
                ctx.perks_me.push_back(FC::perk_info{"", r.name});
            }
            for (const sf2::scene::ItemPerkRef& r : foe_refs) {
                ctx.perks_enemy.push_back(FC::perk_info{"", r.name});
            }
            ctx.health_ratio = 1.0f;  // fresh fight: both sides at full HP
        };
        FC me_ctx, foe_ctx;
        fill(me_ctx, perks.player_items, perks.enemy_items, perks.player_refs,
             perks.enemy_refs);
        fill(foe_ctx, perks.enemy_items, perks.player_items, perks.enemy_refs,
             perks.player_refs);
        // The global set's condition rolls must NOT touch a shared stream
        // (the fight's `Da.pg` replay stream, or conditions.cpp's static
        // fallback): a build-time lock scan that consumed them desynced the
        // deterministic captures (the tutorial fight shifted 2 frames).
        // Route `<Random>` through the port's already-unshared pinned stream
        // (`math_random01` — the RandomSound convention).
        auto unshared = [this]() { return math_random01(); };
        me_ctx.roll01 = unshared;
        foe_ctx.roll01 = unshared;
        register_global_triggers(me_ctx, foe_ctx);
    }
}

namespace {
int oba_phase(fight_phase p) {
    // `Jf.OBa` stage ids for `Ep` matching.
    switch (p) {
        case sf2::scene::fight_phase::start_stance: return 1;
        case sf2::scene::fight_phase::fight: return 2;
        case sf2::scene::fight_phase::end_stance: return 3;
        default: return 0;
    }
}
}  // namespace

sf2::scene::CondCtx FightController::cond_ctx(int side, double hit_dmg) {
    const FightFighter& me = side == 0 ? player_ : enemy_;
    sf2::scene::CondCtx ctx;
    ctx.style_level = me.style.level;
    ctx.combo = me.combo_run;
    ctx.stage = oba_phase(phase_);
    ctx.anim = me.fighter.current_move() != nullptr ? me.fighter.current_move()->name : "";
    for (const std::string& n : me.fighter.active_intervals()) {
        ctx.intervals.emplace_back(n, me.fighter.interval_type(n));
    }
    ctx.hp = me.hp;  // absolute gd (L1310-1311)
    ctx.bullets = me.bullets;
    ctx.raid = me.raid_bullets;
    ctx.charge = me.charge;
    ctx.hit_dmg = hit_dmg;
    ctx.items = side == 0 ? player_items_ : enemy_items_;
    ctx.round = round_.number;
    ctx.pain = me.shock.pain_sr;
    ctx.in_area = false;  // `rR` area bounds are OPEN
    for (const auto& kv : bus_.side(side).mods) {
        ctx.mods.insert(kv.first);
        ctx.mod_ns[kv.first] = kv.second.namespc;
    }
    for (const auto& kv : bus_.side(side).q3) ctx.q3[kv.first] = kv.second;
    ctx.draw01 = [this]() { return draw01(); };
    return ctx;
}

static bool is_combat_action(const std::string& t) {
    // Actions that fold into the CURRENT hit record (`ppb`/`apb`/`Yob`
    // run inside the Cgb fire, pre-`LWa`). TurnOffCollision is state
    // (`hq.S` on the qk model — handled in exec, not the record).
    // Everything else is bus state (JG/Ly/Qz/mods/vars) for future hits.
    return t == "SetHit" || t == "Lifesteal" || t == "DisableInterval";
}

// Target side for an action (`Ma.e6a`): Ob==1 → owner, Ob==2 → foe.
static int action_side(int owner_side, const sf2::scene::PerkAction& a) {
    return a.ob == 2 ? 1 - (owner_side & 1) : (owner_side & 1);
}

void FightController::exec_action(const sf2::scene::PerkTrigger& t,
                                  const sf2::scene::PerkAction& a, int owner_side,
                                  int depth) {
    if (depth > 8) return;  // Provoke re-entrancy guard (nesting is static)
    owner_side &= 1;
    const int tgt = action_side(owner_side, a);
    FightFighter& target = tgt == 0 ? player_ : enemy_;
    const std::string& type = a.type;
    const auto num = [&a](const char* key, double def) {
        const auto it = a.num.find(key);
        return it != a.num.end() ? it->second : def;
    };
    const auto str = [&a](const char* key) {
        const auto it = a.str.find(key);
        return it != a.str.end() ? it->second : std::string();
    };
    const int uf = static_cast<int>(num("Frames", 0.0));
    if (type == "ModAttributes") {
        // `VKa`: instant ±attr adds on the target + mod entry for `JNa`.
        sf2::scene::ModState m;
        m.name = str("Name").empty() ? type : str("Name");
        m.namespc = str("Namespace");
        m.parent = t.perk;
        m.kind = type;
        m.uf = uf;
        m.attr_side = tgt;
        for (const auto& kv : a.num) {
            if (kv.first == "Frames") continue;
            target.params.attributes[kv.first] += static_cast<float>(kv.second);
            m.attr_adds.emplace_back(kv.first, kv.second);
        }
        // `Rp.aP`: non-numeric attrs are per-fire expressions — evaluate
        // now (owner/foe contexts) and apply like constants.
        {
            const sf2::scene::CondCtx oc = cond_ctx(tgt);
            const sf2::scene::CondCtx fc = cond_ctx(1 - tgt);
            for (const auto& kv : a.str) {
                if (kv.first == "Name" || kv.first == "Namespace") continue;
                const auto v = sf2::scene::eval_operand(kv.second, oc, fc);
                if (v) {
                    target.params.attributes[kv.first] += static_cast<float>(*v);
                    m.attr_adds.emplace_back(kv.first, (double)*v);
                }
            }
        }
        bus_.install_mod(owner_side, std::move(m));
    } else if (type == "ChangeImpulse") {
        // `YLa`: SET the target's JG (missing multiplier = 0, `u.H`).
        // Current hit already consumed JG (`Kwb` runs pre-`Cgb`) — this
        // shapes FUTURE hits. Timed entries revert via `gob()`.
        target.jg = sf2::scene::Vec3{static_cast<float>(num("MultiplierX", 0.0)),
                                     static_cast<float>(num("MultiplierY", 0.0)),
                                     static_cast<float>(num("MultiplierZ", 0.0))};
        if (uf > 0) {
            sf2::scene::ModState m;
            m.name = str("Name").empty() ? type : str("Name");
            m.parent = t.perk;
            m.kind = type;
            m.uf = uf;
            bus_.install_mod(owner_side, std::move(m));
        }
    } else if (type == "ChangeAdditionalDamageValue") {
        // `WKa`: Ly SET (+`Tua` record skipped — OPEN).
        target.params.ly = static_cast<float>(num("Value", 0.0));
        if (uf > 0) {
            sf2::scene::ModState m;
            m.name = str("Name").empty() ? type : str("Name");
            m.parent = t.perk;
            m.kind = type;
            m.uf = uf;
            bus_.install_mod(owner_side, std::move(m));
        }
    } else if (type == "ChangeHitEffectScale") {
        target.qz = static_cast<float>(num("Scale", 1.0));
        if (uf > 0) {
            sf2::scene::ModState m;
            m.name = str("Name").empty() ? type : str("Name");
            m.parent = t.perk;
            m.kind = type;
            m.uf = uf;
            bus_.install_mod(owner_side, std::move(m));
        }
    } else if (type == "ModHealthChange") {
        // `Inb` via `znb`: per-frame DoT/HoT on the target + mod entry
        // (uf from Frames; 0 = persistent — `ia` only counts uf>0).
        sf2::scene::ActiveMod dot;
        dot.name = str("Name").empty() ? "dot" : str("Name");
        const auto fi = a.num.find("Frames");
        dot.frames_left = fi != a.num.end() ? static_cast<int>(fi->second) : 0;
        dot.per_frame = num("PerFrameValue", 0.0);
        if (tgt == 0) {
            player_.dots.push_back(dot);
        } else {
            enemy_.dots.push_back(dot);
        }
        sf2::scene::ModState m;
        m.name = dot.name;
        m.parent = t.perk;
        m.kind = type;
        m.uf = dot.frames_left;
        m.per_frame = dot.per_frame;
        bus_.install_mod(owner_side, std::move(m));
    } else if (type == "ModIcon" || type == "ModInvisibility" || type == "ModFlag") {
        // ModFlag (Sp, 178 shipped uses): named persistent mod (mirrors
        // ModIcon) so Blocker/RockOn/Icon-family ModExists chains work.
        sf2::scene::ModState m;
        m.name = str("Name").empty() ? type : str("Name");
        m.namespc = str("Namespace");
        m.parent = t.perk;
        m.kind = type;
        m.uf = uf;
        bus_.install_mod(owner_side, std::move(m));
    } else if (type == "SetModFrames") {
        // `dpb`: retime the named mod (-1 = keep), incl. the namespace loop.
        bus_.retime_mod(owner_side, str("Name"), static_cast<int>(num("Frames", -1.0)),
                        static_cast<int>(num("Interval", -1.0)), str("Namespace"));
    } else if (type == "ApplyModEffect") {
        bus_.log("perknoop ApplyModEffect " + str("Name") + " (U4 OPEN)");
    } else if (type == "ClearMods") {
        bus_.clear_mods(owner_side, str("Name"), str("Namespace"));
    } else if (type == "SetModVariable") {
        bus_.mutable_side(owner_side).q3[str("Name")] = num("Value", 0.0);
    } else if (type == "SetRangeVariable") {
        double v = num("Value", 0.0);
        const auto mn = a.num.find("Min");
        const auto mx = a.num.find("Max");
        if (mn != a.num.end() && v < mn->second) v = mn->second;
        if (mx != a.num.end() && v > mx->second) v = mx->second;
        bus_.mutable_side(owner_side).q3[str("Name")] = v;
    } else if (type == "SetTactic") {
        // `qpb`: `model.yZa(LL)` — real tactic switch on the target side.
        FightFighter& f = tgt == 0 ? player_ : enemy_;
        const std::string name = str("Name");
        const sf2::scene::TacticDef* def = nullptr;
        if (tactic_defs_ != nullptr) {
            const auto it = tactic_defs_->find(name);
            if (it != tactic_defs_->end()) def = &it->second;
        }
        if (f.ai && def != nullptr) {
            f.ai->set_tactic(def);
            std::fprintf(stdout, "[perk] SetTactic %s -> %s\n", f.name.c_str(), name.c_str());
            std::fflush(stdout);
        } else {
            bus_.log("perknoop SetTactic " + name + " (no ai/defs)");
        }
    } else if (type == "Provoke") {
        // `jpb`: re-fire the SL trigger set via `Pob` (immediate `lF`).
        std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> fired;
        bus_.provoke(tgt, str("Trigger"), fired);
        for (const auto& pr : fired) {
            if (is_combat_action(pr.second.type)) {
                bus_.log("perknoop Provoke-combat " + pr.second.type +
                         " (outside hit scope, OPEN)");
                continue;
            }
            exec_action(pr.first, pr.second, tgt, depth + 1);
        }
    } else if (type == "AddBullets") {
        // `Rob`: value.Wn() int; ONLY MagicBullet acts (`hZ`+`LA`),
        // other types are a verbatim no-op.
        const std::string cz = str("BulletType");
        const int v = static_cast<int>(num("Value", 0.0));
        if (cz == "MagicBullet") {
            target.bullets = sf2::scene::bullets_add(target.bullets, v);
            la_normalize(target);
            fire_slot8(tgt);
        } else {
            bus_.log("perknoop AddBullets " + cz + " (verbatim Rob no-op)");
        }
    } else if (type == "AddMagicCharge") {
        // `Sob`: `Hwa` (only when bh==0) + `LA` + slot-8 publish.
        const double v = num("Value", 0.0);
        target.charge = sf2::scene::charge_add(target.bullets, target.charge, v);
        la_normalize(target);
        fire_slot8(tgt);
    } else if (type == "SlowModel") {
        // `Kvb`: b = Speed (default `v.on()` = 1 assumed); b<1 is a
        // verbatim no-op; else KT(hU, b) — single channel here.
        const double b = num("Speed", 1.0);
        if (b >= 1.0) {
            if (tgt == 0) {
                player_.fighter.set_time_scale(static_cast<float>(b));
            } else {
                enemy_.fighter.set_time_scale(static_cast<float>(b));
            }
            if (uf > 0) {
                sf2::scene::ModState m;
                m.name = str("Name").empty() ? type : str("Name");
                m.parent = t.perk;
                m.kind = type;
                m.uf = uf;
                bus_.install_mod(owner_side, std::move(m));
            }
        }
    } else if (type == "ChangeModelColor") {
        // `Mp.Qs`: flat tint ($J color expr — hex/dec ints supported,
        // complex Qa.oh text fails closed); revert restores location color.
        const std::string col = str("Color");
        std::uint32_t rgb = 0;
        bool ok = false;
        if (!col.empty()) {
            try {
                std::size_t pos = 0;
                const unsigned long v = std::stoul(col, &pos, 0);
                if (pos == col.size()) {
                    rgb = static_cast<std::uint32_t>(v) & 0xFFFFFFu;
                    ok = true;
                }
            } catch (...) {
            }
        }
        if (ok) {
            if (tgt == 0) {
                player_.fighter.set_color(rgb);
            } else {
                enemy_.fighter.set_color(rgb);
            }
            sf2::scene::ModState m;
            m.name = str("Name").empty() ? type : str("Name");
            m.parent = t.perk;
            m.kind = type;
            m.uf = uf;
            bus_.install_mod(owner_side, std::move(m));
        } else {
            bus_.log("perknoop ChangeModelColor " + col + " (unparsed)");
        }
    } else if (type == "TurnOffCollision") {
        // `hq.S`: qk capsules vZ=false (false = apply path); revert
        // restores true. Fighter-level gate, equivalent outcome.
        target.collidable = false;
        sf2::scene::ModState m;
        m.name = str("Name").empty() ? type : str("Name");
        m.parent = t.perk;
        m.kind = type;
        m.uf = uf;
        m.col_side = tgt;
        bus_.install_mod(owner_side, std::move(m));
    } else if (type == "Effect") {
        // JS `Yl` (L728) -> `wd.gwb` (L519: `this.Nt.Z(a)`) -> `tl.Nt`
        // (L842): route the started effect by `Gfb` (OnBackground) into
        // `Gq`/`Hq` (modelled by `magic_fx_`). The identity is `(Name, model)`
        // — `bv.model` is stamped here so `StopEffect`/`StopFollowEffect` can
        // resolve it (L838 `LNa`/`Gwb`). The `<Attach>`/`<Position Follow>`
        // follow (`P1`, L730) is not parsed yet, so `follow` stays false; an
        // unknown Name is a no-op.
        const std::string nm = str("Name");
        if (!nm.empty()) {
            FightFighter& owner = (owner_side == 0) ? player_ : enemy_;
            const int side = (owner_side == 0) ? 0 : 1;
            const bool ok = magic_fx_.spawn(nm, owner.fighter.world_x(),
                                            owner.fighter.world_y(),
                                            owner.fighter.facing(), side);
            bus_.log("effect " + nm +
                     (ok ? std::string() : std::string(" (no descriptor)")));
        }
    } else if (type == "StopEffect") {
        // JS `gm` (L735) -> `wd.Svb` (L519: `a.model=…; this.Ot.Z(a)`) ->
        // `tl.Ot` (L843) -> `cv.Dwb` -> `LNa(name, model)` (L838): destroy
        // the first live effect matching `(name, owner model)`.
        const std::string nm = str("Name");
        if (!nm.empty()) {
            magic_fx_.stop(nm, (owner_side == 0) ? 0 : 1);
            bus_.log("stopeffect " + nm);
        }
    } else if (type == "StopFollowEffect") {
        // JS `hm` (L736) -> `wd.Uvb` (L519: `a.model=this; this.Pt.Z(a)`) ->
        // `tl.Pt` (L843) -> `cv.Hwb` -> `Gwb` (L838): latch `Yla` on the
        // matching `(name, owner model)` so `cv.WL` stops the follow update
        // but keeps the animation running to its end.
        const std::string nm = str("Name");
        if (!nm.empty()) {
            magic_fx_.stop_follow(nm, (owner_side == 0) ? 0 : 1);
            bus_.log("stopfolloweffect " + nm);
        }
    } else if (is_combat_action(type)) {
        bus_.log("perknoop " + type + " (outside hit scope, OPEN)");
    } else {
        bus_.log("perknoop " + type);
    }
}

// Fire a hit-scope slot (6 = HitPostCrit with Damage=0 — `Bb.Zi` reset at
// strike start; 7 = PostHit with Damage=base) and execute. Combat actions
// fold into the shared record (ej order: player side first); state
// actions apply immediately (post-`Kwb`, shaping future hits).
void FightController::run_bus_hit(int slot, const sf2::scene::TrigVars& vars,
                                  int fired_side, sf2::scene::HitRecord& rec,
                                  FightFighter& atk, FightFighter& def, int depth,
                                  bool* out_has_damage, float* out_damage) {
    (void)depth;
    double hit_dmg = 0.0;
    {
        const auto it = vars.num.find("Damage");
        if (it != vars.num.end()) hit_dmg = it->second;
    }
    bus_.fire(slot, vars, true, fired_side & 1, cond_ctx(0, hit_dmg), cond_ctx(1, hit_dmg),
              oba_phase(phase_), frame_);
    for (int s = 0; s < 2; ++s) {
        std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> pairs;
        bus_.drain(s, pairs);
        std::vector<sf2::scene::PerkAction> combat;
        for (const auto& pr : pairs) {
            if (is_combat_action(pr.second.type)) {
                combat.push_back(pr.second);
            } else {
                exec_action(pr.first, pr.second, s);
            }
        }
        if (combat.empty()) continue;
        // Lifesteal heals per-action (target side varies by Ob); the rest
        // shares the record — split Lifesteal out first.
        std::vector<sf2::scene::PerkAction> rest, life;
        for (const auto& a : combat) {
            if (a.type == "Lifesteal") {
                life.push_back(a);
            } else {
                rest.push_back(a);
            }
        }
        if (!rest.empty()) {
            const sf2::scene::PerkHitOutcome po =
                sf2::scene::decide_hit_perks(rest, rec, atk.params.so, def.params.so);
            if (po.has_critical) rec.critical = po.f_critical;
            if (po.has_block) rec.blocked = po.f_block;
            if (po.has_shock) rec.shock = po.f_shock;
            if (po.has_disarm) rec.disarm = po.f_disarm;
            if (po.has_damage) {
                rec.raw_damage = po.f_damage;
                if (out_has_damage != nullptr) *out_has_damage = true;
                if (out_damage != nullptr) *out_damage = po.f_damage;
            }
            for (const auto& cl : po.clears) {
                def.fighter.clear_intervals(cl.first, cl.second);
            }
        }
        for (const auto& a : life) {
            const sf2::scene::PerkHitOutcome po = sf2::scene::decide_hit_perks(
                std::vector<sf2::scene::PerkAction>{a}, rec, atk.params.so, def.params.so);
            if (po.heal != 0.0f) {
                const int t = action_side(s, a);
                FightFighter& dst = t == 0 ? player_ : enemy_;
                dst.hp += po.heal;
                if (dst.hp > dst.max_hp) dst.hp = dst.max_hp;
                if (dst.hp < 0.0f) dst.hp = 0.0f;
            }
        }
    }
}

// Shared JNa state bindings (attrs/jg/ly/qz/collision per side).
sf2::scene::ModTickCtx FightController::mod_tick_ctx() {
    sf2::scene::ModTickCtx ctx;
    ctx.attrs_by_side[0] = &player_.params.attributes;
    ctx.attrs_by_side[1] = &enemy_.params.attributes;
    ctx.jg_by_side[0] = &player_.jg;
    ctx.jg_by_side[1] = &enemy_.jg;
    ctx.ly_by_side[0] = &player_.params.ly;
    ctx.ly_by_side[1] = &enemy_.params.ly;
    ctx.qz_by_side[0] = &player_.qz;
    ctx.qz_by_side[1] = &enemy_.qz;
    ctx.col_by_side[0] = &player_.collidable;
    ctx.col_by_side[1] = &enemy_.collidable;
    ctx.set_timescale = [this](int s, float v) {
        if (s == 0) {
            player_.fighter.set_time_scale(v);
        } else {
            enemy_.fighter.set_time_scale(v);
        }
    };
    ctx.reset_color = [this](int s) {
        if (s == 0) {
            player_.fighter.set_color(fighter_color_);
        } else {
            enemy_.fighter.set_color(fighter_color_);
        }
    };
    return ctx;
}

// Magic/bullet normalize (`LA` without the link branch — versus has no
// link model; the `lb!=null` delegation is OPEN): my>=1 converts to a
// bullet + reset, bullets cap at 1. `NoBulletsReplenishment` skips the
// conversion (cj ERule marker).
// Magic per-fighter init + per-round reset (`Ka`/`yKa`: `zL(0)`,
// `yL(InitialCharge)`, `LA`; raid bullets persist — no round reset and
// no consume site in the static text).
void FightController::init_magic() {
    for (FightFighter* f : {&player_, &enemy_}) reset_magic_fighter(*f);
}

void FightController::reset_magic_fighter(FightFighter& f) {
    const sf2::scene::FightParams& gfp = sf2::scene::FightParams::defaults();
    f.bullets = 0;
    f.charge = sf2::scene::charge_add(
        0, 0.0, static_cast<double>(sf2::scene::magic_aq(
                         gfp.magic_initial_base, gfp.magic_initial_attr, f.params)));
    la_normalize(f);
}

void FightController::la_normalize(FightFighter& f) {
    const sf2::scene::LaNorm r =
        sf2::scene::la_normalize(f.bullets, f.charge, no_bullets_replenish_);
    f.bullets = r.bh;
    f.charge = r.my;
}

// Slot-8 publish after bullet/charge adds (mirrors the only shipped
// publisher pattern, debug case 18: `hZ+LA` then `Gj(yb,8)`).
void FightController::fire_slot8(int side) {
    side &= 1;
    bus_.fire(sf2::scene::kEvMagicCharged, sf2::scene::TrigVars(), true, side,
              cond_ctx(0), cond_ctx(1), oba_phase(phase_), frame_);
    for (int q = 0; q < 2; ++q) {
        std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> pairs;
        bus_.drain(q, pairs);
        for (const auto& pr : pairs) exec_action(pr.first, pr.second, q);
    }
}

// `ia` mod tick for one side + slot-14 publish on expiry.
void FightController::tick_mods(int side) {
    side &= 1;
    sf2::scene::ModTickCtx ctx = mod_tick_ctx();
    ctx.on_expire = [this](int s, const sf2::scene::ModState& m) {
        // `JNa→Gj(d,14)`: vars carry ModExpires/Namespace/ParentPerk.
        sf2::scene::TrigVars v;
        v.str["ModExpires"] = m.name;
        v.str["Namespace"] = m.namespc;
        v.str["ParentPerk"] = m.parent;
        bus_.fire(sf2::scene::kEvModExpires, v, true, s, cond_ctx(0), cond_ctx(1),
                  oba_phase(phase_), frame_);
        for (int q = 0; q < 2; ++q) {
            std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> pairs;
            bus_.drain(q, pairs);
            for (const auto& pr : pairs) exec_action(pr.first, pr.second, q);
        }
        // Root `<Triggers>` `ModExpires` (`kz.create` `Sm` L770): the event's
        // `Name` must equal the expired mod's name (`Sm.compare` L770:
        // unchanged `Ki==data` string compare). 63 of the shipped global
        // triggers sit here, so this is by far the busiest event site.
        if (s >= 0) {
            dispatch_global_triggers("ModExpires", "ModExpires", m.name.c_str(), s & 1);
        }
    };
    sf2::scene::tick_side_mods(bus_, side, ctx);
}

// Per-side per-frame bus work: EveryFrame publish (slot 2, `Cp.Step`
// against StepFrame), mod `ia` tick, `qw` flush, interval edge detect
// (slots 12/13 with `Lj` Name/Type vars).
void FightController::tick_bus_side(int side) {
    side &= 1;
    FightFighter& me = side == 0 ? player_ : enemy_;
    FightFighter& foe = side == 0 ? enemy_ : player_;
    sf2::scene::TrigVars v;
    v.num["StepFrame"] = static_cast<double>(frame_);
    char stepbuf[32];
    std::snprintf(stepbuf, sizeof(stepbuf), "%d", frame_);
    v.str["StepFrame"] = stepbuf;
    bus_.fire(sf2::scene::kEvEveryFrame, v, true, side, cond_ctx(0), cond_ctx(1),
              oba_phase(phase_), frame_);
    for (int q = 0; q < 2; ++q) {
        std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> pairs;
        bus_.drain(q, pairs);
        for (const auto& pr : pairs) exec_action(pr.first, pr.second, q);
    }
    tick_mods(side);
    std::vector<std::pair<int, sf2::scene::ModState>> flushed;
    bus_.flush_qw(flushed);
    if (!flushed.empty()) {
        sf2::scene::ModTickCtx ctx = mod_tick_ctx();
        for (const auto& fm : flushed) {
            sf2::scene::revert_mod(fm.second, fm.first, ctx, bus_.log);
            sf2::scene::TrigVars ev;
            ev.str["ModExpires"] = fm.second.name;
            ev.str["Namespace"] = fm.second.namespc;
            ev.str["ParentPerk"] = fm.second.parent;
            bus_.fire(sf2::scene::kEvModExpires, ev, true, fm.first, cond_ctx(0),
                      cond_ctx(1), oba_phase(phase_), frame_);
            for (int q = 0; q < 2; ++q) {
                std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> p2;
                bus_.drain(q, p2);
                for (const auto& pr : p2) exec_action(pr.first, pr.second, q);
            }
            // Root `<Triggers>` ModExpires for the flushed (`qw`) mods.
            dispatch_global_triggers("ModExpires", "ModExpires",
                                     fm.second.name.c_str(), fm.first & 1);
        }
    }
    // Interval edges (`Lj`): added → Start(12), removed → End(13).
    std::set<std::string> cur(me.fighter.active_intervals().begin(),
                              me.fighter.active_intervals().end());
    for (const std::string& n : cur) {
        if (me.prev_intervals.find(n) == me.prev_intervals.end()) {
            // JS `fIa` (L258784): on the `Uninterrupt` interval start,
            // `let a=this.da.Ua, b=this.jb; b!=null&&b.Kf().Cn.rY(!0,a);
            // this.Kf().Cn.rY(!1,a)` — count the strike in BOTH memories.
            if (n == "Uninterrupt" && me.fighter.current_move() != nullptr) {
                const sf2::scene::MoveDef* mv = me.fighter.current_move();
                foe.fighter.strike_memory().rY(true, mv);
                me.fighter.strike_memory().rY(false, mv);
            }
            sf2::scene::TrigVars ev;
            ev.str["Interval"] = n;
            ev.num["IntervalType"] = static_cast<double>(me.fighter.interval_type(n));
            bus_.fire(sf2::scene::kEvIntervalStart, ev, true, side, cond_ctx(0),
                      cond_ctx(1), oba_phase(phase_), frame_);
            std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> p3;
            bus_.drain(0, p3);
            bus_.drain(1, p3);
            for (const auto& pr : p3) exec_action(pr.first, pr.second, side);
        }
    }
    for (const std::string& n : me.prev_intervals) {
        if (cur.find(n) == cur.end()) {
            sf2::scene::TrigVars ev;
            ev.str["Interval"] = n;
            bus_.fire(sf2::scene::kEvIntervalEnd, ev, true, side, cond_ctx(0),
                      cond_ctx(1), oba_phase(phase_), frame_);
            std::vector<std::pair<sf2::scene::PerkTrigger, sf2::scene::PerkAction>> p3;
            bus_.drain(0, p3);
            bus_.drain(1, p3);
            for (const auto& pr : p3) exec_action(pr.first, pr.second, side);
        }
    }
    me.prev_intervals = std::move(cur);
}


void FightController::fill_ctx_geometry(FightContext& ctx, const FightFighter& me,
                                       const FightFighter& foe) const {
    // JS `qm.he` L744 reads `To.OQ(a) - From.OQ(a)` per ref; the native
    // context carries the two roots (`Enemy - Me`) plus the wall bounds so the
    // `Object="Wall"` refs can be resolved (`ee.q9a` L788).
    ctx.me_x = me.fighter.world_x();
    ctx.enemy_x = foe.fighter.world_x();
    ctx.dist_x = ctx.enemy_x - ctx.me_x;
    ctx.dist_3d = std::fabs(ctx.dist_x);
    // JS `Ae.Wl` (`Vi.SBa` L704, from the move's `<SetDirection>`).
    ctx.direction = ctx.dist_x >= 0.0f ? 1.0f : -1.0f;
    ctx.enemy_direction = -ctx.direction;
    ctx.wall_min = wall_min_;
    ctx.wall_max = wall_max_;
    // JS `Ae.To` (the event-context factory: `Z6a`/`Hza`/`e4a` L685 all stamp
    // `d.To = ca.Ka()!=null ? ca.Ka().Da.type : "FightNone"`), consumed by the
    // `lm` BattleType condition (`lm.he`: `ctx.To == Value`). `battle_.type` is
    // the `Da.type` derived from the stage KIND (`p.Wab` -> `b0`, fight.hpp
    // `battle_type_for_kind`). Every fight.cpp `FightContext` site routes
    // through this fill, so the rule evaluates instead of reading "".
    ctx.battle_type = battle_.type;
}

void FightController::player_input(sf2::scene::key_type key, sf2::scene::press_type press) {
    // JS `ca.LBa` (L399) via `N0a`/`O0a` (L426): when `Iga` (the
    // InvertJoystick flag, set by `F1` L897) is live, a directional control
    // 1..8 is remapped (up<->down / forward<->back): 1<->5, 2<->6, 3<->7,
    // 4<->8. Keys 9+ (Punch/Kick/...) are returned unchanged (`default:a`).
    if (invert_joystick_) {
        switch (static_cast<int>(key)) {
            case 1: key = static_cast<sf2::scene::key_type>(5); break;
            case 2: key = static_cast<sf2::scene::key_type>(6); break;
            case 3: key = static_cast<sf2::scene::key_type>(7); break;
            case 4: key = static_cast<sf2::scene::key_type>(8); break;
            case 5: key = static_cast<sf2::scene::key_type>(1); break;
            case 6: key = static_cast<sf2::scene::key_type>(2); break;
            case 7: key = static_cast<sf2::scene::key_type>(3); break;
            case 8: key = static_cast<sf2::scene::key_type>(4); break;
            default: break;
        }
    }
    // JS `ca.N0a` (L426): in phase 1 (StartStance) a PRESS goes into the
    // round's single-slot input buffer `WC` — `this.eu==1 ?
    // b.WC==-1&&(b.WC=a) : ...` — so the FIRST press of the phase wins and
    // later presses are ignored. It is replayed by `llb` when the fight
    // starts. In phase 2 the press is buffered into the fighter directly
    // (`eu==2 && b.yJa(a)`).
    if (phase_ == fight_phase::start_stance) {
        if (press == press_type::tap && !start_buffer_filled_) {
            start_buffer_key_ = key;
            start_buffer_filled_ = true;
        }
        return;  // holds/releases during the intro are not moves — ignore
    }
    if (phase_ != fight_phase::fight) return;  // JS: input only in phases 1/2
    // JS `wd.yJa(a)` (L501) — the ability PRESS gate. The method is an
    // OR-chain whose SHORT-CIRCUIT is the gate: it reaches
    // `this.Kl.Sgb(a)` (the key-buffer forward) only when every cooldown
    // term is false. `ca.N0a` (L426) calls it as `eu==2 && b.yJa(a)` and
    // discards the return value, so the observable effect is: an ability key
    // (slot 9..14) is forwarded ONLY while its cooldown is not running.
    //   `a==12 && bh==0 && !$aa` / `a==11 && SR && mA<TR` /
    //   `a==10 && i2 && eA<DR`   / `a==9 && m4 && JA<aT` /
    //   `a==14 && iu<pU`         / `!sN` / `Kl.Sgb(a)`
    const int slot = static_cast<int>(key);
    if (press == press_type::tap && slot >= 9 && slot <= 14) {
        if (player_.fighter.ability_cooldown_running(slot)) {
            std::fprintf(stdout,
                         "[cd] yJa F%d key=%d slot=%d BLOCKED (cooldown "
                         "running)\n",
                         frame_, slot, slot);
            std::fflush(stdout);
            return;
        }
        std::fprintf(stdout, "[cd] yJa F%d key=%d slot=%d ready -> Kl.Sgb\n",
                     frame_, slot, slot);
        std::fflush(stdout);
    }
    player_.fighter.input(key, press);
}

// JS `ca.Enb` (L390) + `wd.tKa` -> `Fu.ia`: the attack check — the
// attacker's active Attack-interval AttackingParts capsules vs the target's
// collidable capsules.
bool FightController::hit_test(FightFighter& atk, FightFighter& def,
                               const sf2::scene::MoveDef& move, int frame,
                               sf2::scene::HitCapsule& hit_cap,
                               sf2::scene::CapsuleHit& ch,
                               const sf2::scene::Interval*& hit_interval,
                               const sf2::scene::HitCapsule*& atk_cap) {
    atk_cap = nullptr;  // JS `b.Py` = the attacker strike capsule (L395)
    // JS `Cl.ia` one-shot (`dW`, L566-567): the same attack object never
    // tests twice in a row — without this every overlapped frame re-hits.
    // HZa position (hzaGate L500-501): the caller runs the yD(4)+invuln
    // gate FIRST; geometry here tests ONLY the first active Attack
    // interval (`da.yD(4)` — single-d semantics), not every interval.
    auto& last = cl_last_[atk.name];
    const sf2::scene::Interval* d = nullptr;
    for (const sf2::scene::Interval& iv : move.intervals) {
        if (iv.type != 4) continue;  // Attack
        const int s = std::max(iv.start, move.first_frame);
        const int e = iv.end;
        if (s <= frame && frame <= e) {
            d = &iv;
            break;
        }
    }
    if (d == nullptr) return false;
    // TurnOffCollision (`hq.S` zeroes all vZ): no hittable capsules.
    if (!def.collidable) return false;
    const auto key = std::make_pair(static_cast<const void*>(&move),
                                    static_cast<const void*>(d));
    if (last == key) return false;  // dW == c: already CONNECTED
    // JS `Cl.ia` (L566-567): `dW` latches ONLY on a successful test --
    // `if(this.W1a(...)) return this.dW=c,!0` (and the `!c.aEa` early
    // return). Latching before the geometry (as this port did) gives the
    // swing exactly ONE geometry sample per interval activation: if the
    // first active frame does not overlap, the whole attack whiffs even as
    // the fist crosses the target on a later frame -- the reported "most
    // attacks pass through". Latch at each success below.
    {
        // JS `!c.aEa` (L566-567): no AttackingParts = always connects
        // (11/618 shipped attack intervals); n$=o$=(0,0,0), KD=null.
        // KD=null skips `Bl.strike` (no knockback); contact stays the
        // midpoint for presentation (sparks).
        if (d->attacking_parts.empty()) {
            for (const auto& tgt : def.body.capsules) {
                if (!tgt.collidable) continue;
                hit_cap = tgt;
                ch.hit = true;
                ch.kd_null = true;
                ch.n = {0.0f, 0.0f, 0.0f};
                ch.o = {0.0f, 0.0f, 0.0f};
                ch.point.x = (tgt.p1.x + tgt.p2.x) * 0.5f;
                ch.point.y = (tgt.p1.y + tgt.p2.y) * 0.5f;
                ch.point.z = (tgt.p1.z + tgt.p2.z) * 0.5f;
                hit_interval = d;
                last = key;  // `dW = c`
                return true;
            }
            return false;
        }
        for (const std::string& edge : d->attacking_parts) {
            const sf2::scene::HitCapsule* ac = atk.body.by_name(edge);
            if (ac == nullptr) continue;
            for (const auto& tgt : def.body.capsules) {
                if (!tgt.collidable) continue;
                if (sf2::scene::capsule_capsule_overlap(*ac, tgt, ch)) {
                    hit_cap = tgt;
                    atk_cap = ac;  // JS `b.Py` (L395)
                    hit_interval = d;
                    last = key;  // `dW = c`
                    return true;
                }
            }
        }
    }
    return false;
}

// JS `wd.HZa` gate position (hzaGate L500-501): the yD(4) pick + the
// invuln/bypass check run BEFORE geometry (a blocked chain must not
// consume dW). Returns the attack interval to test, or null.
const sf2::scene::Interval* FightController::hza_pick(const FightFighter& target,
                                                      const sf2::scene::MoveDef& move,
                                                      int frame) {
    const sf2::scene::Interval* d = nullptr;
    for (const auto& iv : move.intervals) {
        if (iv.type != 4) continue;
        const int s = std::max(iv.start, move.first_frame);
        if (s <= frame && frame <= iv.end) {
            d = &iv;
            break;
        }
    }
    if (d == nullptr) return nullptr;
    // No chain while the TARGET holds Invulnerable (yD(6)) unless the
    // attack bypasses (`jga` && (`iga` empty || `SZa(iga)`)).
    if (target.fighter.has_invuln()) {
        const bool bypass =
            d->ignores_invuln &&
            (d->invuln_bypass_names.empty() ||
             std::any_of(d->invuln_bypass_names.begin(),
                         d->invuln_bypass_names.end(), [&](const std::string& n) {
                             return target.fighter.active_intervals().count(n) > 0;
                         }));
        if (!bypass) return nullptr;
    }
    return d;
}

// JS `ca.Cgb` (L394-397): apply a landed hit — the bCa damage, the lethal
// floor, HP -= Zi, and the knockback impulse.
void FightController::apply_hit(FightFighter& atk, FightFighter& def,
                                const sf2::scene::MoveDef& move,
                                const sf2::scene::Interval& iv,
                                const sf2::scene::HitCapsule& hit_cap,
                                const sf2::scene::CapsuleHit& ch, int frame,
                                const sf2::scene::HitCapsule* atk_cap) {
    sf2::scene::IntervalDamage idmg;
    idmg.base_damage = iv.damage;
    idmg.no_critical = iv.no_critical;
    idmg.hit_body_part = iv.hit_name_at(frame);  // JS `Ul.B8a(e.M0())`
    // JS `wd.bCa(a,...)` receives `a.SZ` (EVERY sub-`<Damage>`) and `a.KP`
    // (EVERY `<Defense>`), plus `e.da.Ua.QX` for `c2a`. The old code pushed
    // only the FIRST sub-`<Damage>` (`iv.damage_type`) and stuffed the
    // defender capsule's `Xi` into `defense_names[0]`, which killed both the
    // `KP[0]` branch and the `blocked` branch of `LAa` (L536).
    idmg.attack_attrs = iv.attack_attrs;
    idmg.defense_names = iv.defense_names;
    idmg.qx = move.qx;
    // JS `wd.strike` (L509-510) order on the TARGET: block-break FIRST
    // (`g.DDa` -> `hT(5)`; shipped moves never set IgnoresBlock, so this
    // is dead with shipped data but faithful), then `Bb.block = Nbb()`
    // (the defender's Block interval AFTER the break), then LAa/crit.
    if (iv.ignores_block) {
        def.fighter.clear_block();
    }
    bool blocked = def.fighter.has_block();
    // JS `wd.strike` crit (L510): `se = !block && !g.a3 && Lcb(A9a())`;
    // JS `Lcb(a)` = `Da.cT(a*100)` (L1204): `a>1 -> true` (the `a>b` shortcut)
    // else a fresh draw `< a`. `A9a = pga?100:gya.p8a` (L529; `pga` setter
    // OPEN -> false path). Draws come from the shared fight stream
    // (`Da.pg.dT` analog; `draw01()` = the owned `DaPrng` or an override).
    const float a9 = sf2::scene::crit_chance(atk.params);
    bool critical =
        !blocked && !iv.no_critical && (a9 > 1.0f || draw01() < a9);
    const std::string defense_attr = sf2::scene::select_defense(idmg, blocked, &hit_cap);
    const float dmg = sf2::scene::compute_damage(idmg, atk.params, def.params, defense_attr,
                                                 blocked, critical, &hit_cap);
    sf2::scene::HitRecord rec;
    rec.raw_damage = dmg;
    rec.defense = defense_attr;
    rec.target_part = hit_cap.body_part;
    rec.hit_edge = iv.attacking_parts.empty() ? "" : iv.attacking_parts[0];
    bool sethit_damage = false;
    float sethit_value = 0.0f;
    rec.blocked = blocked;
    rec.critical = critical;
    // Slot 6 = HitPostCrit at the Dgb point (post-se, pre-Ca;
    // mg.Damage = 0 — Bb.Zi was reset at strike start). Overrides
    // here feed compute_damage (exact: Dgb precedes Ca).
    {
        sf2::scene::TrigVars hv6;
        hv6.str["Defense"] = defense_attr;
        hv6.str["Animation"] = move.name;
        hv6.num["Critical"] = critical ? 1.0 : 0.0;
        hv6.num["Shock"] = 0.0;
        hv6.num["Block"] = blocked ? 1.0 : 0.0;
        hv6.num["Damage"] = 0.0;
        run_bus_hit(sf2::scene::kEvHitPostCrit, hv6, def.is_player ? 0 : 1, rec, atk, def,
                    0, &sethit_damage, &sethit_value);
    }
    // Slot-6 SetHit overrides feed bCa (exact: Dgb precedes bCa).
    blocked = rec.blocked;
    critical = rec.critical;
    // JS `wd.R8a` shock decider on the target (L531-532 + L511):
    // `Uq` = head-zone hit; `b = Zi/atk.so` (`so` OPEN -> 1.0); `ws`
    // (weapon strike) is the TARGET's flag — `strike` runs on the target and
    // `R8a(e)` receives the attacker only for its `Shock*Chance` attrs, so
    // `this.Orb(this.ws?0:b)` gates the TARGET's pain (`this.ws`, L521). Set
    // by the Invulnerability/Combo/Crazy `kZ` pass (`ola(!b)`, L902).
    // crit/head terms = Base + attr (`p8a` pattern, OPEN exact formula).
    rec.head_hit = hit_cap.body_part == "Head";
    {
        const sf2::scene::FightParams& gfp = sf2::scene::FightParams::defaults();
        const float b = dmg;  // Zi/so with so=1.0 (OPEN)
        const bool pain_c = sf2::scene::orb_hit(
            def.shock, def.shock.weapon_ws ? 0.0f : b, gfp.shock_threshold);
        // JS `R8a` (L531): `a=v.Ub.iya; d.attributes.get(v.Ub.hya,e); a*=e.G`
        // — MULTIPLY (an absent attr makes the chance 0), not the old `+`.
        const float crit_term =
            gfp.shock_crit_base * atk.params.attr("ShockCriticalHitChance");
        const float head_term =
            gfp.shock_head_base * atk.params.attr("ShockHeadHitChance");
        // The two `r8a` draws come from `uf.RJa()` = `Math.random`, NOT the
        // shared `Da.pg` crit/AI stream (`draw01()`).
        const bool ub = sf2::scene::r8a_decide(
            false, def.shock.shocked_vc, b, pain_c, crit_term, critical,
            math_random01(), head_term, rec.head_hit, blocked,
            math_random01()).raw;
        rec.shock = ub;
        // JS `Cgb` shock apply (L394): `Ub&&(vc?Ub=false:vc=true)`.
        if (ub) {
            if (def.shock.shocked_vc) {
                rec.shock = false;
            } else {
                def.shock.shocked_vc = true;
                def.fighter.set_shock_latch(true);  // `oa.vc` (Al.sk/jE gate)
            }
        }
        // JS `Cgb` disarm (L394): `Yi&&(d=$b(Au); sn||own?Yi=false:...)`
        // with `Au` = Shock.Weapon (`Fists`, internal_settings, verified).
        // Unarmed-on-Fists takes the `ownHd` path -> Yi=false; a
        // knife-wielder proceeds (sn latch + `kwb()` arms `Wx=MFa`).
        // `Wqb` item-swap/fling/`Wsb`/drop-event bodies are presentation
        // (OPEN); the swap + attr set + vc latch are live below.
        rec.disarm = ub;
        if (rec.disarm) {
            if (def.shock.disarm_sn || def.weapon == "Fists") {
                rec.disarm = false;
            } else {
                def.shock.disarm_sn = true;
                // JS `kwb()` (L522): `Wx<0 && (Wx=MFa)`.
                if (def.shock.weapon_wx < 0) {
                    def.shock.weapon_wx = gfp.shock_loosening_delay;
                }
            }
        }
    }
    rec.frame = frame;
    // JS `ep` (L394): `Bb.ep = !Dga`, and `Dga` latches ONLY inside the
    // unblocked branch (`b.block || (hT(5), Dga=!0, ...)` — blocked hits
    // neither break block nor consume first-hit status).
    rec.first_hit = !dga_;
    if (!blocked) {
        dga_ = true;
    }
    // JS `Jma` L511: `Bb.ep = (sI==0); ...; this.sI++` - the defender's
    // landed-hit counter advances on every resolved hit (blocked or not);
    // `ca.Cgb` L396 reads it back for the Punchbag reaction cadence.
    def.fighter.note_hit_taken();
    // Perk trigger bus, hit scope (replaces the direct hook):
    // slot 7 = PostHit at the `Cgb` point (`Sba(a.model,b,7)` — after the
    // R8a/disarm rolls, before `LWa`/damage; SetHit overrides land on the
    // shared record pre-`apply_damage`, `ChangeImpulse`/`Ly` shape FUTURE
    // hits since `Kwb` already consumed JG). `mg` = the Sba stamps.
    bool hit_blocked = blocked;
    bool hit_critical = critical;
    {
        sf2::scene::TrigVars hv;
        hv.str["Defense"] = defense_attr;
        hv.str["Animation"] = move.name;
        hv.num["Critical"] = critical ? 1.0 : 0.0;
        hv.num["Shock"] = rec.shock ? 1.0 : 0.0;
        hv.num["Block"] = blocked ? 1.0 : 0.0;
        hv.num["Damage"] = static_cast<double>(dmg);
        run_bus_hit(sf2::scene::kEvPostHit, hv, def.is_player ? 0 : 1, rec, atk, def, 0, &sethit_damage, &sethit_value);
        hit_blocked = rec.blocked;
        hit_critical = rec.critical;
    }
    sf2::scene::apply_damage(rec, def.hp, false);    if (sethit_damage) {
        // `ppb` sets `bR` AND `Zi` directly (no lethal clamp at set time).
        rec.raw_damage = sethit_value;
        rec.final_damage = sethit_value;
        rec.hp_after = def.hp > sethit_value ? def.hp - sethit_value : 0.0f;
        rec.lethal = sethit_value >= def.hp;
    }
    // JS `ca.Cgb` (L395) order after the lethal latch and the SetHit override:
    //   `a.model.ws && (b.Zi = 0)`   -> the weapon/regen veto
    //   `a.model.$db(b.Zi, ca.i6a(c), b.JP)`
    //   `this.aM(a.model, -b.Zi)`    -> the HP spend
    //   `this.udb(a.model.jb, b.Zi)` -> gear lifesteal heals the attacker
    // `ws` is set only by the Invulnerability rule pass (L902 `ola(!b)`), so
    // this is a no-op for rule-less fights, but the ORDER must match 1:1.
    if (def.shock.weapon_ws) {
        rec.final_damage = 0.0f;  // `Zi = 0`
        rec.hp_after = def.hp;    // `aM(model, -0)` leaves HP untouched
    }
    def.hp = rec.hp_after;
    // JS `strike` (L259058): `++this.lU` on the ATTACKER, and
    // `LWa(a,b,c)` (L519, called at L260105 `this.LWa(e, this.Bb.aI,
    // this.Bb.Zi)`): `this.Kf().Cn.nY(!0,b,c)` (my attack map) +
    // `a.Cn.nY(!1,b,c)` (the target's `bqa` map) with `b = the move`,
    // `c = the dealt damage`. Feeds the AI's `Cn.d0` strike-memory features.
    atk.fighter.bump_strike_time();
    atk.fighter.strike_memory().nY(true, &move, rec.final_damage);
    def.fighter.strike_memory().nY(false, &move, rec.final_damage);
    // JS `$db(a,b,c){this.i_.add(a,b,c)}` (L523): `(Zi, i6a(SZ), JP)`.
    i_.push_back({rec.final_damage, i6a_attr(idmg.attack_attrs), defense_attr});
    // JS `this.udb(a.model.jb, b.Zi)` (L395 -> L403):
    //   `c.G * v.kha.Bc * Zi * (a.jb.so / a.so)` on the ATTACKER, healed via
    //   `aM` (clamped to [0, Zn] = max HP). `a` inside `udb` is the
    //   attacker, so `a.jb.so/a.so` = defender.so / attacker.so.
    {
        const sf2::scene::FightParams& gfp = sf2::scene::FightParams::defaults();
        const float ls = atk.params.attr(gfp.lifesteal_attr);
        if (ls != 0.0f) {
            const float heal =
                ls * gfp.lifesteal_base * rec.final_damage *
                (atk.params.so != 0.0f ? def.params.so / atk.params.so : 0.0f);
            if (heal != 0.0f) {
                atk.hp = std::min(atk.max_hp, atk.hp + heal);
            }
        }
    }

    // Magic recharge (`Jma`, lb==null branch — versus has no link model;
    // the lb!=null delegation is OPEN): attacker charges from dealt
    // damage while bh==0: `Hwa(2^e*c*b*Zi)` with e = DamageRecharge on
    // crit else PainRecharge, then `LA()` normalize.
    {
        const sf2::scene::FightParams& gfp = sf2::scene::FightParams::defaults();
        if (atk.bullets == 0 && rec.final_damage > 0.0f) {
            const float e = hit_critical
                ? sf2::scene::magic_aq(gfp.magic_damage_base, gfp.magic_damage_attr,
                                       atk.params)
                : sf2::scene::magic_aq(gfp.magic_pain_base, gfp.magic_pain_attr,
                                       atk.params);
            const float b = sf2::scene::block_mult(def.params, hit_blocked, gfp);
            const float c = sf2::scene::crit_mult(atk.params, hit_critical, gfp);
            const double add = sf2::scene::magic_recharge(static_cast<double>(e),
                                                          static_cast<double>(b),
                                                          static_cast<double>(c),
                                                          static_cast<double>(rec.final_damage));
            atk.charge = sf2::scene::charge_add(atk.bullets, atk.charge, add);
            la_normalize(atk);
        }
    }

    // HUD style credit (JS `Sh.Vma` <- `Sf.strike`, L2040; `Jh.Gua` feeds
    // prize b6): every landed hit credits the ATTACKER's meter with the
    // attack move's RNa (blocked or not — `ha.Gzb` runs unconditionally).
    {
        static const StyleTable kStyle;
        const double credit =
            style_credit(kStyle, atk.style, move.name, move.style_factor);
        style_vma(atk.style, credit, 6);
        if (atk.style.best > prize_fh_.b6) prize_fh_.b6 = atk.style.best;
    }

    // Event flags for the golden trace (JS `Sba` L393: Defense/Animation/
    // Critical/Shock/Block/Damage) — one line per landed hit.
    if (hit_critical || rec.shock || hit_blocked || rec.first_hit) {
        std::fprintf(stdout, "[hit] F%d %s->%s dmg=%.2f%s%s%s%s\n", frame,
                     atk.name.c_str(), def.name.c_str(), rec.final_damage,
                     hit_critical ? " CRIT" : "", rec.shock ? " SHOCK" : "",
                     hit_blocked ? " BLOCK" : "", rec.first_hit ? " FIRST" : "");
        std::fflush(stdout);
    }

    // JS `ca.Cgb` (L394-397): `b.block || (model.hT(5), Dga, ...)` — a
    // landed UNBLOCKED hit destroys the target's Block intervals and picks
    // a new reaction move via `Gc.DK` (d-set first-match:
    // `Fighter::try_react`; shock prefers *Fall* reactions).
    // Strike-flag counters for the prize Fh (JS `Sf.strike` flags d/e/h:
    // first-hit -> `cvb`/`p1a` (c6++), shock -> `yvb`/`P1a` (e6++); blocked
    // hits take the `UYa` path and count nothing).
    if (!hit_blocked) {
        def.fighter.clear_block();
        if (rec.first_hit) ++prize_fh_.c6;
        if (rec.shock) ++prize_fh_.e6;
        sf2::scene::FightContext rctx;
        rctx.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
        rctx.stage = sf2::scene::round_stage::fight;
        rctx.anims_me = anim_names_of(def.fighter);
        rctx.anims_enemy = anim_names_of(atk.fighter);
        fill_ctx_geometry(rctx, def, atk);
        rctx.health_ratio = def.max_hp > 0.0f ? def.hp / def.max_hp : 0.0f;
        rctx.last_hit_type = hit_critical ? "Critical" : (rec.shock ? "Shock" : "");
        rctx.candidate_moves = {};
        // JS `Gc.DK` (L343452): `f[uf.sja(f.length)]` — the reaction pick is
        // a UNIFORM `Math.random` draw (`uf.sja` L57426), NOT `Da.pg`. The
        // port routes it through the pinned `math_random01()` so it never
        // perturbs the shared fight/AI stream.
        const std::string reaction = def.fighter.try_react(rctx, rec.shock);
        if (!reaction.empty()) {
            // JS `Gc.DK` (L673-674) -> `jJa`/`Qnb` -> `wd.Lwb` -> `ca.Lgb`
            // (L387) -> `PC(7,side)`: the knockdown reaction start is the
            // LoseFall cp==7 event. A *Fall*-named reaction is the native
            // proxy for the JS `Qnb`/`qs.animation` knockdown.
            if (reaction.find("Fall") != std::string::npos) {
                def.reaction_fall = true;
            }
            // JS `Gc.DK` (L673) -> `jJa`/`Qnb` (L507) -> `wd.Mwb`/`Lwb`
            // (L507/L511) -> `ca.Lwb` (L387) -> `Nd.start(a)` (L582): the
            // landed reaction STARTS the ragdoll (`nk=true; frameCount=0;
            // names={reaction}`). `Te.Skb` (`Fighter::start_move_impl`)
            // calls `Al.stop` when the next clip starts.
            def.fighter.ragdoll_start(reaction, wall_min_, wall_max_, floor_y_);
            std::fprintf(stdout, "[ragdoll] F%d %s START '%s' (nk=1)\n", frame,
                         def.name.c_str(), reaction.c_str());
            std::fprintf(stdout, "[react] F%d %s -> %s\n", frame,
                         def.name.c_str(), reaction.c_str());
            std::fflush(stdout);
        }
    }

    // Knockback (JS Kwb + bounds): interval impulse mirrored by facing,
    // scaled by the attacker JG (ChangeImpulse shapes FUTURE hits).
    sf2::scene::Vec3 impulse{iv.impulse_x, iv.impulse_y, iv.impulse_z};
    impulse.x *= static_cast<float>(atk.fighter.facing()) * atk.jg.x;
    impulse.y *= atk.jg.y;
    impulse.z *= atk.jg.z;
    sf2::scene::ImpulseResult imp;
    // KD=null (no AttackingParts) skips Bl.strike: no knockback.
    if (!ch.kd_null) {
    // JS `Bl.strike` (L588) top: `this.s2a()` — midpoint-smooth every body
    // (`mf = (mf+ma)*0.5`) on the struck model before splitting the impulse.
    def.fighter.strike_midpoint_smooth();
    const float new_x =
        sf2::scene::apply_impulse(hit_cap, ch, impulse, def.fighter.world_x(),
                                  wall_min_, wall_max_, imp);
    (void)new_x;  // the displacement now lives on the endpoint NODES (below)
    // JS `Bl.strike` (L587-588): `a.sx.XA(l)` / `a.Zs.XA(c)` add the
    // impulse-split vectors to the endpoint nodes' WORLD `ma` directly. While
    // the ragdoll is active the clip apply never overwrites those nodes, so
    // the reaction PERSISTS across frames (no decaying offset, no snap-back).
    {
        const int b1 = def.fighter.model().bone_by_name(hit_cap.end1);
        const int b2 = def.fighter.model().bone_by_name(hit_cap.end2);
        // [probe, authorised] Per-hit impulse evidence for the NotAnimation
        // dojo bag: the struck endpoint nodes' solver `ma` before/after the
        // `Bl.strike` write, plus the DRAWN-pose delta armed for the next
        // `sample()` (`[bagmove]`).
        const bool bag_probe = battle_.enemy_not_animation && !def.is_player;
        const bool has2 = b2 >= 0 && b2 != b1;
        const float p1x = bag_probe && b1 >= 0 ? def.fighter.solver_ma_x(b1) : 0.0f;
        const float p1y = bag_probe && b1 >= 0 ? def.fighter.solver_ma_y(b1) : 0.0f;
        const float p2x = bag_probe && has2 ? def.fighter.solver_ma_x(b2) : 0.0f;
        const float p2y = bag_probe && has2 ? def.fighter.solver_ma_y(b2) : 0.0f;
        if (b1 >= 0) def.fighter.strike_node(b1, imp.node1_vec);
        if (has2) def.fighter.strike_node(b2, imp.node2_vec);
        std::fprintf(stdout,
                     "[strike] F%d %s x1=%.2f x2=%.2f nk=%d frame=%d\n", frame,
                     def.name.c_str(), imp.node1_vec.x, imp.node2_vec.x,
                     def.fighter.ragdoll_active() ? 1 : 0,
                     def.fighter.ragdoll_frame_count());
        if (bag_probe) {
            const float q1x = b1 >= 0 ? def.fighter.solver_ma_x(b1) : 0.0f;
            const float q1y = b1 >= 0 ? def.fighter.solver_ma_y(b1) : 0.0f;
            const float q2x = has2 ? def.fighter.solver_ma_x(b2) : 0.0f;
            const float q2y = has2 ? def.fighter.solver_ma_y(b2) : 0.0f;
            std::fprintf(stdout,
                         "[bagimp] F%d n1=%s ma(%.3f,%.3f)->(%.3f,%.3f) "
                         "d=(%.3f,%.3f) n2=%s d=(%.3f,%.3f) imp1=(%.3f,%.3f)\n",
                         frame, hit_cap.end1.c_str(), p1x, p1y, q1x, q1y,
                         q1x - p1x, q1y - p1y, hit_cap.end2.c_str(), q2x - p2x,
                         q2y - p2y, imp.node1_vec.x, imp.node1_vec.y);
            def.fighter.arm_strike_move_probe();
        }
        std::fflush(stdout);
    }
    }

    // `<Actions>` on the landed hit (JS `ca.Cgb` L396:
    //   `this.Bg.Ih(6, a); this.Bg.Ih(7, a);`
    // `Gc.Ih(a,b,c)` L671 stores `d.model = a==7 ? b.Pd : b.model`, and
    // `Gnb` L672 dispatches `c.model.da.CZa(c.type)`). With `strike`'s
    // payload (`Vb.Pd` = the ATTACKER, `Vb.model` = the DEFENDER):
    //   Strike (7) -> the ATTACKER's current move -> the authored
    //                 `<RandomSound Event="Strike">` (snd_hit1..6 — 481 of
    //                 them in moves.xml) plus the per-move Strike `<Sound>`s.
    //   Hit (6)    -> the DEFENDER's current move -> its `Event="Hit"`
    //                 actions (the 24 StopSound/StopEffect/Delete entries; the
    //                 StopSound half needs `AudioEngine::stop` — follow-up).
    // [fx] The `Hyb` hit direction (JS L395): the strike capsule's per-frame
    // motion delta `b.Py.sx/Zs .ma-.mf` -- the endpoints' current minus
    // previous-frame world positions. `r1/r2` are `sx.ma`/`Zs.ma`; the
    // snapshot map holds the previous frame (`sx.mf`/`Zs.mf`).
    sf2::scene::Vec3 hdir{0.0f, 0.0f, 0.0f};
    if (atk_cap != nullptr) {
        const auto pit = atk.prev_cap_ends.find(atk_cap->name);
        if (pit != atk.prev_cap_ends.end()) {
            hdir.x = (atk_cap->r1.x - pit->second.first.x) +
                     (atk_cap->r2.x - pit->second.second.x);
            hdir.y = (atk_cap->r1.y - pit->second.first.y) +
                     (atk_cap->r2.y - pit->second.second.y);
        }
    }

    // JS `a.Pd.da.yD(4).DL && a.model.lrb(b.bk, d, b.se?.0166:.00833)` (L395):
    // the `Vu` latch is armed BEFORE `Bg.Ih(6,a)` fires the `<Hit/>` event
    // (the JS call order: offset 200831 `lrb`, 201585 `Bg.Ih(6,a)`), so the
    // global HitEffect trigger's `Xvb` (L519) consumer sees `Vu.Ica`. `bk` =
    // contact point, `fg` = the strike capsule's motion direction, `time` =
    // the flash speed (1/60 crit else 1/120). Presentation only: no RNG/sim.
    const float flash_time = hit_critical ? kFlashTimeCrit : kFlashTimeNormal;
    if (!iv.no_effect) {
        def.fighter.latch_reaction(ch.point, hdir, flash_time);  // `lrb`
    } else {
        def.fighter.clear_reaction();  // no `lrb` on a NoEffect interval
    }

    // NOTE: there is NO `ta.ak` in `ca.Cgb` itself (L394-397) — the previous
    // `play("hit")` here was invented and is removed.
    {
        sf2::scene::FightContext ev;
        ev.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
        ev.stage = sf2::scene::round_stage::fight;
        ev.anims_me = anim_names_of(atk.fighter);
        ev.anims_enemy = anim_names_of(def.fighter);
        fill_ctx_geometry(ev, atk, def);
        ev.health_ratio = atk.max_hp > 0.0f ? atk.hp / atk.max_hp : 0.0f;
        // JS `sm.he` reads `a.IL` (the hit event data): `se` -> "Critical",
        // `Ub` -> "Shock". `has_last_hit` arms the `<Hit>` conditions.
        ev.last_hit_type = hit_critical ? "Critical" : (rec.shock ? "Shock" : "");
        ev.has_last_hit = true;
        // `CZa(7)` reads the attacker's move; `move` IS the attacker's move.
        std::vector<const sf2::scene::MoveAction*> strike_acts;
        for (const sf2::scene::MoveAction& a : move.actions) {
            if (!a.frame_trigger && a.event == "Strike") strike_acts.push_back(&a);
        }
        dispatch_move_actions(strike_acts, atk, "Strike", ev);
        // `CZa(6)` reads the defender's CURRENT move (`Vb.model`).
        dispatch_move_actions(def.fighter.move_actions_for_event("Hit"), def, "Hit", ev);
        // Root `<Triggers>` (JS `ra.Dm`, registered per model by `ra.yz`):
        // the global set's own `<Hit>`/`<Strike>` events (`kz` Nm/Um). The
        // Hit event dispatches on the DEFENDER's set (`d.model = b.model`)
        // and carries the landed-hit context so the `<Hit>` conditions of the
        // CriticalEffect/BlockEffect/HitEffect rows evaluate (`sm.he`).
        dispatch_global_triggers("Strike", "Strike");
        dispatch_global_triggers("Hit", "Hit", nullptr, def.is_player ? 0 : 1, &ev);
    }

    // JS `ca.Cgb` L396 tail (the Punchbag's forced reaction):
    //   `wa.F().gE() || this.Da.type!="FightNone" || this.Zb.$s!="Punchbag" ||
    //    a.model.sI != v.Qxa || (this.rwb(), a.model.oa.vc=!0, a.model.V_a());`
    // `rwb()` (L431) = `this.Ta.DL(this.ZAa(false,false,true))` - force the
    // `<HitEffects>` "Shock" row (`select_hit_effect(false,false,true)` = the
    // `ZAa` port, L422) through the camera hit-effect latch. `v.Qxa` =
    // `kCounterPunches`. `FightNone` is uniquely the dojo Punchbag
    // (`stages.xml` zone Punchbag node `Training` Type="DUMMY"; JS `p.Wab`
    // L181 maps DUMMY -> "FightNone"; JS also checks `Zb.$s=="Punchbag"`, a
    // fighter label the controller does not carry).
    // `oa.vc=!0` (`a.model.oa.vc=!0`) is the model's shock latch — the same
    // flag `wd.vc` the port keeps as `FightFighter::shock.shocked_vc`
    // (JS `Cgb` L394 `Ub&&(a.model.vc?...:a.model.vc=!0)`). `V_a()` (L517:
    // `let a=0,b=this.oa.Va.all; ... c.UEa&&c.kla(!1)`) releases the model's
    // `Weak="1"` figures - now implemented as `Fighter::release_weak()`, fed
    // by the `Vc.UEa` parse (`Bone::weak`, JS `Yc.Ijb` L572
    // `d.UEa=u.ka(b.attributes.get("Weak"))`). The NotAnimation dummy DOES
    // wear `SkeletonPunchingBag` (`stages.xml` L15; the FightScreen-ctor merge
    // keeps `assets.merged_bag`), whose Node12 carries `Weak="1"`
    // (`mdl_skeleton_punching_bag.xml`), so the release has a real target.
    // OBSERVABILITY (still OPEN, JS-STRICT): clearing the node's `MG`
    // (`Bone::fixed`) is read by the JS `strike` edge gate (L588
    // `if(!d.MG||!e.MG)`) and the `jE` `cA` gate (L583 `d.nh&&!d.NG&&...`);
    // the port's Verlet gates on `cloth` only (fighter.cpp `Al.sk`,
    // `nk=false`) and its capsule hit test has no endpoint-immovability gate,
    // so the value flips exactly but has no consumer yet.
    if (battle_.type == "FightNone" && !def.is_player &&
        def.fighter.hits_taken() ==
            sf2::scene::FightParams::defaults().counter_punches) {
        if (const sf2::scene::HitEffect* forced =
                sf2::scene::select_hit_effect(false, false, true)) {
            camera_.apply_hit_effect(*forced);
        }
        def.shock.shocked_vc = true;  // `a.model.oa.vc=!0` (L396)
        def.fighter.set_shock_latch(true);  // `oa.vc` (Al.sk/jE gate)
        def.fighter.release_weak();   // `a.model.V_a()` (L396 -> V_a L517)
    }

    // [fx] Hit sparks `ql.Rub`/`Ut.ryb` (JS L369/L824): the burst is spawned
    // ONLY on a critical strike -- JS L395 `b.se && this.Ta.Rub(b.bk,b.fg)`.
    // The burst origin is the contact point (`strike.n$` = `ch.point`). The
    // presentation RNG is EffectSystem's private LCG (never roll01). JS fans
    // by the impulse direction `b.fg`; the native keeps the facing x
    // reduction (`spawn_hit_sparks`) - the impulse-vector fan is OPEN.
    if (hit_critical) {
        fx_.spawn_hit_sparks(ch.point.x, ch.point.y, atk.fighter.facing());
    }

    // [fx] The hit flash `Hyb` (JS L825) is now driven by the parsed
    // `HitEffect` (`jg`) action: the root `<Triggers>` CriticalEffect /
    // BlockEffect / HitEffect rows dispatch through `dispatch_move_actions`
    // on the `<Hit/>` event above, and each reads the `lrb` latch armed
    // earlier to `spawn_hit_flash` (`ca.Kla` -> `ql.Kla` -> `Ut.Hyb`). No
    // flash is spawned here — the action dispatch is the single source.

    // [fx] The camera hit-judder + hit-stop (JS `ca.Cgb` L396:
    // `if(b.se||b.Uq&&!b.block||b.Ub) c=this.ZAa(b.se,b.Uq&&!b.block,b.Ub),
    // c!=null&&this.Ta.DL(c)`). `ZAa` scans the shipped `<HitEffects>` rows
    // in document order and returns the first matching the active flags; the
    // old port used an invented constant `shake(6.0)`. `DL` latches the row
    // and arms the per-type PauseTime (hit-stop) + EffectTime judder.
    if (hit_critical || (hit_cap.body_part == "Head" && !hit_blocked) ||
        rec.shock) {
        const sf2::scene::HitEffect* he =
            sf2::scene::select_hit_effect(hit_critical,
                                          hit_cap.body_part == "Head" && !hit_blocked,
                                          rec.shock);
        if (he != nullptr) {
            camera_.apply_hit_effect(*he);
            std::fprintf(stdout,
                         "[fx] hitstop type=%s pause=%d effect=%d "
                         "ampX=%.1f freqX=%.2f ampY=%.1f freqY=%.2f\n",
                         he->type.c_str(), he->pause_time, he->effect_time,
                         he->amplitude_x, he->frequency_x, he->amplitude_y,
                         he->frequency_y);
            std::fflush(stdout);
        }
    }
    if (hit_critical) {
        std::fprintf(stdout, "[fx] sparks at %.0f,%.0f\n", ch.point.x, ch.point.y);
        std::fflush(stdout);
    }

    ++atk.hits_landed;
    ++def.hits_taken;
    // Prize stats (JS `v.kD`/`bzb` factors): shocks dealt accumulate; the
    // battle's first striker is latched once.
    // JS `wd.strike` (L510): `this.Bb.block||(e.JCa()||this.Era++,e.dca())`
    // — the whole combo statement sits behind `!block`, so a BLOCKED hit
    // neither raises the attacker's ladder (`e.dca()` -> `Vx.wgb`) nor
    // resets the target's. The old port bumped it on blocked hits too.
    if (!hit_blocked) {
        ++atk.combo_run;
        if (atk.combo_run > atk.max_combo) atk.max_combo = atk.combo_run;
        // JS `wd.strike` (L510): `this.Bb.block||(e.JCa()||this.Era++,
        // e.dca())`. `e.JCa()` (= the attacker's `Vx.v1` latch, read BEFORE
        // `e.dca()` arms it) — only the FIRST hit of a combo increments the
        // victim's `Era`. The victim's own ladder (`combo_run`=`Vx.tf`) is
        // NOT reset here (the JS reset is the `Vx.wyb` window decay only).
        if (!atk.combo_active) ++def.era;
        // JS `Vx.wgb` (L510 `e.dca()`): `this.v1=!0; this.OV=0; ++this.tf`.
        // The latch + window counter arm the time decay in the per-frame
        // `Vx.wyb` tick below.
        atk.combo_active = true;
        atk.combo_frames = 0;
    }
    if (rec.shock) ++atk.shocks_dealt;
    if (!battle_first_hit_) {
        battle_first_hit_ = true;
        battle_first_by_player_ = atk.is_player;
    }
    // JS `ca.Cgb` (L396) `PC(5/6,...)` + `ca.Ihb` (L423): the landed-hit
    // rule pass (LifeSteal heal, Regeneration reset, Points, WinCombo/
    // WinShock). Runs after the combo counters so `NZ` includes this hit.
    rules_on_hit(atk, def, rec);
    (void)move;
}

// JS `v.kD`/`bzb` prize factors (FLOW_STATIC section 4.3;
// internal_settings `<RewardsPrize>` values verified 2026-09-04).
FightController::BattlePrize FightController::prize(int base_coins) const {
    BattlePrize p;
    p.perfect = player_.hits_taken == 0;
    p.first_strike = battle_first_hit_ && battle_first_by_player_;
    p.max_combo = player_.max_combo;
    p.shocks = player_.shocks_dealt;
    p.style_value = 0;  // style untracked -> Turtle 0 (OPEN)
    // Exact `Fh.lXa` (L2054-2056): prize-base `a` = the head prize `ph`
    // (OPEN D0-table value -> base coins used); coins `b` = base;
    // gems 0; Ia/epF/UiF/UbF = 5/2/1/3; pk EAa order; kq = 0
    // (DenominationDigits absent in seed).
    static const double kPk[6] = {0.0, 3.0, 6.0, 9.0, 12.0, 15.0};
    PrizeKx kx;
    fh_lxa(kx, prize_fh_, static_cast<double>(base_coins),
           static_cast<double>(base_coins), 0.0, 5.0, 2.0, 1.0, 3.0, kPk, 0);
    p.coins_total = static_cast<int>(kx.m6);
    p.coins_bonus = p.coins_total - static_cast<int>(prize_vk(base_coins, 0));
    p.gems_bonus = static_cast<int>(kx.mOa);
    // The per-category `Fh.Kx` rows (JS `oc.P3/ep/Ui/DZ/Ub`): the Results
    // breakdown shows each bonus, not the 0/1 flag or the combo count.
    p.coins_perfect = static_cast<int>(kx.p3);
    p.coins_first = static_cast<int>(kx.ep);
    p.coins_combo = static_cast<int>(kx.ui);
    p.coins_style = static_cast<int>(kx.dz);
    p.coins_shock = static_cast<int>(kx.ub);
    return p;
}

void FightController::update_fighter(FightFighter& me, FightFighter& foe, float dt) {
    // The per-fighter update: the AI (or input) picks a move, the fighter
    // executes it, the physics body is rebuilt. Mirrors the JS `wd.ia` +
    // `de.ia` path (see core/scene/README.md).
    // Perk bus per-frame work (EveryFrame slot 2 + mod ia + 12/13 edges).
    tick_bus_side(&me == &player_ ? 0 : 1);
    // Perk DoTs/HoTs (JS `znb`/`Inb`, L1290/L1298): tick installed mods
    // once per frame (signed per-frame value, clamped, expired dropped).
    if (!me.dots.empty()) sf2::scene::tick_active_mods(me.dots, me.hp, me.max_hp);
    me.fighter.set_enemy_x(foe.fighter.world_x());
    // JS `wd.x3` -> `Fu.hob()` (dW=null): `x3` runs from `Te.Skb` at EVERY
    // clip start, so a repeat swing of the SAME move must clear the one-shot
    // too. The move POINTER is unchanged on a self-repeat, hence the serial.
    {
        const int cur = me.fighter.move_start_count();
        auto it = cl_move_.find(me.name);
        if (it == cl_move_.end() || it->second != cur) {
            cl_move_[me.name] = cur;
            cl_last_.erase(me.name);
            // Root `<Triggers>` `AnimationStart` (`kz.create` `Km` L766): the
            // JS animator raises `EStartAnimationEvent` at the end of
            // `Te.Skb` (`this.x3(this.Ua)` L551 -> `gh`, L553), and the
            // listener chain lands on `CZa(9)`. The move-change edge above IS
            // that start; the event's `Name` filter is the started move name
            // (`Km.compare` L766 checks `Ki` against the owner's animation
            // list, which holds exactly this name).
            if (me.fighter.current_move() != nullptr) {
                dispatch_global_triggers("AnimationStart", "AnimationStart",
                                         me.fighter.current_move()->name.c_str(),
                                         &me == &player_ ? 0 : 1);
            }
        }
    }
    // [FIX Phase 4b — fighters stay in the arena] The root-motion walk
    // (Fighter::advance) moves world_x freely; clamp it to the arena walls
    // (the params walls at ±(Width/2 - Wall)) so a fighter can't walk out of
    // the dojo into the void (the enemy AI previously wandered to x=1244,
    // past the right wall at 900, and stood on the black background).
    me.fighter.clamp_x(wall_min_, wall_max_);

    // [ragdoll probe] Per-frame world position while the `Al` ragdoll latch
    // is active — the reproduction for "the hit reaction must not snap back".
    if (me.fighter.ragdoll_active()) {
        std::fprintf(stdout, "[rdx] F%d %s x=%.2f y=%.2f fc=%d\n", frame(),
                     me.name.c_str(), me.fighter.world_x(),
                     me.fighter.world_y(), me.fighter.ragdoll_frame_count());
        std::fflush(stdout);
    }

    // [FIX Phase 4b — manual control] The PLAYER's key input FIRST: when
    // the fighter is a manual (non-AI, non-auto-attack) fighter, the
    // buffered keys (Fighter::input via the fight screen's on_key) are
    // consumed here by the move selection. This must run BEFORE the
    // stance-idle auto-play below, so a key press interrupts the idle
    // (otherwise the idle would re-start a clip every frame and the input
    // could never win — "no input").
    if (me.ai == nullptr && !auto_attack_ && phase_ == fight_phase::fight) {
        // JS `zl.ia` (L798): one input-age tick per fight frame — drop the
        // Tap sequence at `dX>=15`, run the 30-frame hold/release cycle,
        // rebuild the held set from the down keys. Must run BEFORE the move
        // selection so the selection sees the same aged buffer the JS
        // `wd.BHa` handler would.
        me.fighter.age_keys();
        sf2::scene::FightContext ctx;
        ctx.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
        ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
        // [FIX input anim list — Root 3] JS `lg.he` CurrentAnimation reads the
        // fighter's OWN animation-name list (`a.rr.ef(player)`, L749). The
        // port passed `{}`, so every non-negated CurrentAnimation gate on the
        // input path read false (and `$NoAnimation$` read true). The list
        // carries the current move plus the logical stance state
        // (`StanceLeft`/`StanceRight` — the names used by moves.xml
        // `<CurrentAnimation Name="StanceLeft"/>` in the stance-idle set).
        // [TASK A] JS `lg.vQ` slot 1 (`XH`) = the current animation's `xl`:
        // the move's own name + its transitive `<Template>` chain. The old
        // fill carried only the move NAME + the logical stance state, so the
        // `Step` template's `<CurrentAnimation Name="Step"/>` restart guard
        // (inherited by StepForward) read false and the move restarted on a
        // re-press inside `SelfUninterrupt`.
        ctx.anims_me = anim_names_of(me.fighter);
        ctx.anims_me.push_back(me.is_player ? "StanceLeft" : "StanceRight");
        ctx.anims_enemy = anim_names_of(foe.fighter);
        // `<CurrentInterval Player="Enemy">` reads the OPPONENT's live
        // intervals (JS `tm.he` + `Nd.ol`); the `Throw` template's Throwable
        // gate (moves.xml:553/565) depends on it.
        ctx.intervals_enemy.clear();
        for (const std::string& n : foe.fighter.active_intervals()) {
            ctx.intervals_enemy.push_back({n, foe.fighter.interval_type(n), true});
        }
        // JS `Dm.he` Player condition source (`a.qb=b.parameters.qb` L680).
        ctx.qb = me.is_player;
        fill_ctx_geometry(ctx, me, foe);
        ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
        // JS-exact PLAYER path — the `Gc.DK` `c == false` branch (L673-674):
        // candidates by <KeyPressed> + own <Conditions>, then the `Aua`
        // max-<Priority> group picked UNIFORMLY (`f[uf.sja(f.length)]`).
        // The `Pkb` tail — `M7.Wcb` mirror filter, `va.Ts`
        // <Tactics><Conditions> filter and the `Md.jL`/`iCa` weighted
        // roulette — is on the AI's `eb=true` (`Gc.Vkb`) path only and is
        // deliberately NOT applied here (see `Fighter::try_select_move`).
        const std::string chosen = me.fighter.try_select_move(ctx);
        if (!chosen.empty()) {
            ++me.moves_started;
            me.last_decision = "input:" + chosen;
            std::fprintf(stdout, "[fight] player input -> %s (F%d)\n", chosen.c_str(),
                         frame_);
            std::fflush(stdout);
        }
    }

    // The stance idle auto-play (the game plays the weapon stance idle
    // between moves — the AI's record key). [FIX Phase 4a] The idle has
    // -Left/-Right mirror variants; the fighter plays the one matching its
    // facing (JS `Te.rub` sets `FX` from the facing; the move's MirrorNode
    // picks the mirror). The oracle trace (reference/traces/console.log)
    // shows the LEFT-facing enemy on `FistsStartStanceIdle-Left` (clip
    // fists1_stance_idle, 38 frames) — the old hardcoded `-Right` played
    // fists2_stance_idle (101 frames), the wrong mirror + a different clip.
    // During phase 1 (StartStance) the fighter plays the intro stance clip
    // (`FistsStartStance-Left/-Right`, stance_1/stance_2 — the trace F2..
    // F134 = `FistsStartStance-Left`); the idle variant is the phase-2
    // loop.
    // The NotAnimation dummy never auto-plays the stance idle: JS `QD`
    // (L499) gates `da.ia`, and the idle move would otherwise register as a
    // started move. It stays exactly where `sample_enemy_idle` left it.
    const bool stance_locked = battle_.enemy_not_animation && &me == &enemy_;
    if (me.fighter.current_move() == nullptr && !stance_locked) {
        const bool intro = phase_ == fight_phase::start_stance;
        // The mirror variant is picked from the direction to the enemy
        // (JS `wd.NS` L506: facing = sign(enemyX - myX); the move's
        // MirrorNode maps it to the -Left/-Right variant). The FIRST
        // auto-play runs before any move has set facing_, so derive it
        // from the raw positions instead of the (defaulted) facing_.
        // [FIX stance side — JS `<Player Number>` gate] The variant is chosen
        // by the move's own `<Player Number=..>` gate, NOT by facing:
        // moves.xml gives `…-Left` `<Player Number="1"/>` (JS `Dm.he` L755:
        // `Number==1 == qb` -> the CONTROLLED fighter) and `…-Right` to the
        // other side. The oracle trace (reference/traces/console.run4.log)
        // shows the controlled fighter on `FistsStartStance-Left` with fx=+1
        // (cf=4, sub=2), so Left is independent of facing. The old
        // facing-derived `face_left` rule was invented: it gave a right-facing
        // player the enemy clip `…-Right` (clip stance_2 / fists2_stance_idle,
        // the wrong mirror), and hard-coded `Idle-Left` for BOTH sides — the
        // reported wrong models/animations.
        // [W2] Resolve the stance from THIS fighter's OWN unlocked list
        // (`hb_`), not a hardcoded Fists name. `hb_` already excludes every
        // other weapon's TacticWeapon moves, so the `Template` tag isolates
        // the equipped weapon's stance family: the phase-1 intro uses
        // `StanceLeft`/`StanceRight` (`FistsStartStance-Left`,
        // `KnivesStartStance-Left`, ...), the phase-2 loop `StartIdleStance`
        // (`FistsStartStanceIdle-Left`, `KnivesStartStanceIdle`, ...). JS
        // `Aua` (L673) keeps the max-`<Priority>` group — `KnivesStartStanceIdle`
        // (11) beats the universal `FistsStartStanceIdle-Left` (10) exactly as
        // in the JS — and the `-Left`/`-Right` variant follows the controlled
        // side within a tie.
        // [N2] The LIVE combat idle is the `IdleStance` family, not
        // `StartIdleStance`. `StanceIdle` (moves.xml L4,
        // `Template="IdleStance|Stance"`, FileName `stance_idle.bytes`,
        // Priority 0) matches once the intro `Stance*` clip is over and after
        // ANY move (`<Conditions>`: `CurrentAnimation Name="Stance" Not="1"`
        // OR `Transition` OR `$Move`). `computer_settings.xml` L24
        // (`<OutcomeTables><StartAnimation Name="StanceIdle"/>`) starts the
        // stance into it, and every attack gates on
        // `<CurrentAnimation Name="IdleStance"/>` plus
        // `<CurrentAnimation Name="StartIdleStance" Not="1"/>`
        // (moves.xml L6916/L6919): a fighter can only attack FROM the regular
        // idle. `StartIdleStance` (`FistsStartStanceIdle-Left`, moves.xml
        // L3843, FileName `fists1_stance_idle.bytes`, Priority 10,
        // `EndsStage="1"`) is the ONE-SHOT transition out of the intro stance.
        // The old rule played `StartIdleStance` after every move as well, so
        // the fighter snapped back to the INITIAL/stance-start idle pose the
        // moment an attack ended — the reported symptom.
        const bool post_intro_transition = !intro && me.moves_started == 0;
        const std::vector<std::string> stance_templates =
            intro ? std::vector<std::string>{"StanceLeft", "StanceRight"}
                  : (post_intro_transition
                         ? std::vector<std::string>{"StartIdleStance"}
                         : std::vector<std::string>{"IdleStance"});
        const sf2::scene::MoveDef* idle_move =
            me.fighter.stance_move(stance_templates, me.is_player);
        if (idle_move != nullptr) {
            const std::string& idle_name = idle_move->name;
            sf2::scene::FightContext ctx;
        ctx.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
            ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
            // JS `Dm.he` Player source + the fighter's animation-name list
            // (current move + the logical stance state; see the input path).
            ctx.qb = me.is_player;
            ctx.anims_me = idle_move->anim_names;
            ctx.anims_me.push_back(me.is_player ? "StanceLeft" : "StanceRight");
            ctx.anims_enemy = anim_names_of(foe.fighter);
            if (ctx.anims_enemy.empty()) ctx.anims_enemy.push_back(idle_name);
            fill_ctx_geometry(ctx, me, foe);
            ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
            me.fighter.ai_start_move(*idle_move, ctx);
        }
    }

    // JS `wd.Pnb` (L528): pain decay + weapon-pickup timer, every fighter
    // tick. `Wqb` fires the pickup (unarmed fighters never arm it — Yi is
    // always false for them — so this is a no-op with shipped data, but
    // the decay is live and the stream position of future draws is kept).
    {
        const sf2::scene::FightParams& gfp = sf2::scene::FightParams::defaults();
        if (sf2::scene::shock_tick(me.shock, gfp.shock_frame_reduction)) {
            // JS `Wqb` (L527-528): swap to the `Au` item, `EPa`/`FPa` attr
            // set (`WeaponDamage=0`, internal_settings, verified), `vc`
            // latch. Fling/`Wsb`/drop-event bodies are presentation (OPEN).
            me.weapon = "Fists";
            me.shock.shocked_vc = true;
            me.fighter.set_shock_latch(true);  // `oa.vc` (Al.sk/jE gate)
            me.params.attributes["WeaponDamage"] = 0.0f;
            std::fprintf(stdout, "[fight] F%d %s WQB pickup -> Fists\n",
                         frame_, me.name.c_str());
            std::fflush(stdout);
        }
        // HUD style decay `ia()` (L2092): bar-only drain, levels never drop.
        static const StyleTable kStyleDecay;
        style_decay(me.style, kStyleDecay.tya);
    }

    me.fighter.advance(dt);
    me.last_move = me.fighter.current_move() ? me.fighter.current_move()->name : "";

    // The move's authored `<Actions>` (JS `Te.Lwa` L563-564 -> the
    // `EActionStart` event L530 -> `wd.BNa` L523). The frame-triggered
    // actions whose `Frame` matched the clip frame just displayed play here;
    // the JP/EN whooshes (`snd_swishN` on the authored frames) and the
    // attack grunts (`snd_m_pl_attackN` gated on `xc.voice`) come from this
    // path. The old invented name-substring jump/step triggers are REMOVED
    // (no JS counterpart: `Cgb` L394-397 has no `ta.ak`, and moves.xml has
    // no `snd_jump`/`snd_step` ids).
    {
        sf2::scene::FightContext actx;
        actx.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
        actx.stage = static_cast<sf2::scene::round_stage>(phase_);
        actx.anims_me = {me.last_move};
        actx.anims_enemy = {foe.fighter.current_move() ? foe.fighter.current_move()->name
                                                      : ""};
        actx.qb = me.is_player;
        fill_ctx_geometry(actx, me, foe);
        actx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
        dispatch_move_actions(me.fighter.take_frame_actions(), me, "frame", actx);
        // `AnimationEnd` (10) actions of the move whose clip just ended
        // (JS `Te.lS` L553 -> `wd.kg` -> `Gc.Ih(10,..)` L671 -> `Gnb` L672
        // -> `CZa(10)`; `KNa` leaves `Ua` set, which is why the ended move is
        // captured in `Fighter::take_ended_move`).
        if (const sf2::scene::MoveDef* ended = me.fighter.take_ended_move()) {
            // JS `Uza` (L258171): `let a=this.Sj(), b=this.jb; b!=null &&
            // b.Kf().Cn.v_(!0,a); this.Kf().Cn.v_(!1,a)` — commit the
            // buffered strike memory for the move that just ended, on BOTH
            // sides (`v_` moves `Yo -> Xb` and `gy -> tf`).
            foe.fighter.strike_memory().v_(true, ended);
            me.fighter.strike_memory().v_(false, ended);
            std::vector<const sf2::scene::MoveAction*> end_acts;
            for (const sf2::scene::MoveAction& a : ended->actions) {
                if (!a.frame_trigger && a.event == "AnimationEnd") end_acts.push_back(&a);
            }
            dispatch_move_actions(end_acts, me, "AnimationEnd", actx);
        }
    }

    // The demo's simple auto-attack (the game's FightAuto `P.fP` = BothBot):
    // when idle, step toward the enemy when beyond reach, punch when in
    // reach. It is a DEMO-ONLY harness (`set_auto_attack`, default off) with
    // NO JS counterpart in the fight, so its condition contexts must NOT draw
    // from the fight's shared `Da.pg` stream — that would shift every later
    // AI/crit draw (and the pose dump) away from the oracle. `roll01` is
    // deliberately left UNSET here: `eval_random` then uses the pinned
    // independent stream (`conditions.cpp`'s private `DaPrng`, the documented
    // probe/demo fallback), so the demo's rolls stay off `draw01()`.
    if (auto_attack_ && me.is_player && me.fighter.current_move() == nullptr) {
        const float dist = std::fabs(foe.fighter.world_x() - me.fighter.world_x());
        const std::string move_name =
            dist > 160.0f ? "StepForward" : "HighPunch";
        const auto it = moves_->find(move_name);
        if (it != moves_->end()) {
            sf2::scene::FightContext ctx;
            ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
            ctx.anims_me = anim_names_of(me.fighter);
            ctx.anims_enemy = anim_names_of(foe.fighter);
            fill_ctx_geometry(ctx, me, foe);
            ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
            if (me.fighter.ai_start_move(it->second, ctx)) {
                ++me.moves_started;
                me.last_decision = "auto:" + move_name;
            }
        }
        me.fighter.advance(0.0f);
    }

    // The AI decision (JS `de.ia` via `wd.Anb`/`Ykb`, L499-500:
    // `(parameters.Fj||P.fP)&&Je==2` — the AI ONLY decides in phase 2
    // (Fight). Gating the whole block (not just the move start) also
    // stops the phase-1 QJa roll consumption, keeping the Da stream
    // aligned with the game.
    if (me.ai != nullptr && phase_ == fight_phase::fight) {
        sf2::scene::AiFightState st;        st.current_move = me.fighter.current_move();
        st.my_moves = &me.fighter.hb();    // JS `this.model.me` (`de.V1` L601)
        st.move_frame = me.fighter.move_frame();
        st.move_len = st.current_move ? st.current_move->end_frame : 0;
        st.my_hp = me.hp;
        st.my_max_hp = me.max_hp;
        st.enemy_hp = foe.hp;
        st.enemy_max_hp = foe.max_hp;
        st.my_x = me.fighter.world_x();
        st.my_y = me.fighter.world_y();
        st.enemy_x = foe.fighter.world_x();
        st.my_facing = me.fighter.facing();
        st.enemy_facing = foe.fighter.facing();
        st.my_anim = me.fighter.current_move() ? me.fighter.current_move()->name : "";
        st.enemy_anim = foe.fighter.current_move() ? foe.fighter.current_move()->name : "";
        st.enemy_move = foe.fighter.current_move();
        st.enemy_move_frame = foe.fighter.move_frame();
        for (const std::string& n : me.fighter.active_intervals()) {
            st.my_intervals.push_back({n, 0});
        }
        st.enemy_max_part_frames = foe.fighter.m2();  // JS `Tba` (max `M2`)
        // JS `wd.K0` (L505): NoRanged item equipped -> +1, else -1.
        st.ranged = me.ranged_available ? -1 : 1;
        // JS `Ji.Pe` (`Te.Pe`): a hit reaction forces it FALSE. `wd.Qnb`
        // (L507) starts the reaction via `Mwb`->`Lwb` (`Te.Sca` L548 sets
        // `Pe=!1`) then `da.reset()` (L548 again `Pe=!1`) + `da.etb`, while
        // the ragdoll latch `Nd.nk` (`Al.start`, L582) is what drives the
        // pose. With `Pe=!1` `de.hcb` (L598) returns false and `de.ia`
        // (L593) issues NO move — the reaction runs to completion instead of
        // being replaced by the AI the very next frame.
        st.playing = me.fighter.current_move() != nullptr &&
                     !me.fighter.ragdoll_active();  // JS `Ji.Pe`
        // JS `a.Pe` — the ENEMY's clip must be playing for `de.Ycb`/`de.Lbb`
        // (L620-621) and the `Pqb` nG wait (L605). A reaction forces it false
        // (`Te.Sca`/`da.reset`) while `Ua` can still be set.
        st.enemy_playing = foe.fighter.current_move() != nullptr &&
                           !foe.fighter.ragdoll_active();
        st.my_reacting = me.fighter.ragdoll_active();  // JS `Nd.nk`
        st.magic_bullets = 0;
        st.enemy_part_frames.push_back(foe.fighter.m2());
        st.fight_frame = frame_;
        st.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
        // JS `Da.pg` — the ONE shared stream; hand it to the AI so QJa/gfa/
        // aea/dqb/jL/slots all draw from it (the port previously kept QJa on
        // a private DaPrng, splitting the stream). With a test override
        // installed (`roll01_`) fall back to it so tests stay seeded.
        st.da_pg = roll01_ ? nullptr : &prng_;
        st.strike_memory = &me.fighter.strike_memory();

        const std::string decision = me.ai->update(st);
        me.last_decision = decision;
        me.last_ai_stage = me.ai->last_stage();
        // Wave log: the JS-exact values the audit asked to surface —
        // `K2` (with/without the NoRanged item), `pZ` (Tba = max `M2`)
        // vs the raw move frame, the strike-memory counters, the stream.
        if (decision != last_ai_log_) {
            const sf2::scene::AiFeatureState& ff = me.ai->features();
            const sf2::scene::AiController::AiDebug& d = me.ai->last_debug();
            std::fprintf(stdout,
                         "[ai] F%d %s K2=%d (ranged_available=%d:"
                         " true->-1, false(NoRanged)->+1; alt=%d)"
                         " pZ(Tba/M2)=%d raw_move_frame=%d"
                         " strike{counter=%.3f xb=%.3f tf=%.3f} stream=%s"
                         " | branch=%s fk=%d aqa=%d gate=%d ycb=%d lbb=%d"
                         " pcb=%d rua=%d caa=%d nG=%d hcb=%d ef=%d x=%d"
                         " ue=%d ae=%d wb=%d dec='%s'\n",
                         frame_, me.name.c_str(), st.ranged,
                         me.ranged_available ? 1 : 0,
                         me.ranged_available ? 1 : -1,
                         st.enemy_max_part_frames, foe.fighter.move_frame(),
                         ff.counter, ff.xb, ff.tf,
                         st.da_pg != nullptr ? "Da.pg(draw01)" : "override",
                         d.branch, d.fk, d.aqa, d.gate ? 1 : 0, d.ycb ? 1 : 0,
                         d.lbb ? 1 : 0, d.pcb ? 1 : 0, d.rua ? 1 : 0,
                         d.caa ? 1 : 0, d.nG ? 1 : 0, d.hcb ? 1 : 0,
                         d.enemy_frame, d.x, d.enemy_uninterrupt_end,
                         d.enemy_attack_end, d.wb, decision.c_str());
            std::fflush(stdout);
            last_ai_log_ = decision;
        }
        if (!decision.empty()) {
            const sf2::scene::MoveDef* chosen = nullptr;
            auto pick_ctx = [&]() {
                sf2::scene::FightContext c;
                c.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
                c.stage = static_cast<sf2::scene::round_stage>(phase_);
                c.anims_me = anim_names_of(me.fighter);
                c.anims_enemy = anim_names_of(foe.fighter);
                fill_ctx_geometry(c, me, foe);
                c.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
                return c;
            };
            // JS `de.V1` (L601-602) resolves the candidate to the MOVE whose
            // condition tree passes. A tag can match several moves, so take
            // the first whose `<Conditions>` hold — `Throw` must resolve to
            // the distance-gated `ThrowForward`, never a far throw.
            for (const auto& kv : *moves_) {
                if (kv.second.name != decision &&
                    kv.second.template_tags.count(decision) == 0) {
                    continue;
                }
                sf2::scene::FightContext c = pick_ctx();
                if (!sf2::scene::eval_move_conditions(kv.second.conditions, c)) continue;
                chosen = &kv.second;
                break;
            }
            if (chosen == nullptr && decision == "ShortAttack") {
                for (const auto& kv : *moves_) {
                    if (kv.second.template_tags.count("Punch") == 0) continue;
                    sf2::scene::FightContext ctx;
        ctx.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
                    ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
                    ctx.anims_me = anim_names_of(me.fighter);
                    ctx.anims_enemy = anim_names_of(foe.fighter);
                    fill_ctx_geometry(ctx, me, foe);
                    ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
                    if (sf2::scene::eval_move_conditions(kv.second.tactics, ctx)) {
                        chosen = &kv.second;
                        break;
                    }
                }
            }
            if (chosen != nullptr) {
                sf2::scene::FightContext ctx;
        ctx.roll01 = [this]() { return draw01(); };  // shared fight stream (`Da.pg`)
                ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
                ctx.anims_me = anim_names_of(me.fighter);
                ctx.anims_enemy = anim_names_of(foe.fighter);
                fill_ctx_geometry(ctx, me, foe);
                ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
                if (me.fighter.ai_start_move(*chosen, ctx)) {
                    ++me.moves_started;
                }
            }
        }
    }

    rebuild_body(me, foe);
}

int FightController::hud_timer() const {
    // JS `Sf.iPa` (L2036): the HUD text is `max(0,NF)`.
    return std::max(0, round_.time_nf);
}

// The banner's display text ("" when no banner). banner_round_ is the
// 0-based round number (round_.number), so the label is +1. The ROUND
// text is formatted into a function-local static buffer (single-threaded
// game loop; the caller reads it before the next call).
const char* FightController::banner_text() const {
    static char round_buf[32];
    switch (cur_banner_) {
        case banner_kind::round:
            std::snprintf(round_buf, sizeof(round_buf), "ROUND %d",
                         banner_round_ + 1);
            return round_buf;
        case banner_kind::fight:  return "FIGHT!";
        // The JS draws NO "K.O."/"KO" text anywhere — the literal "K.O."
        // appears 0 times in sf2.502f0946.js. The round-end plate is `Cr.GZ`
        // (L2024, type 6/7) with the callouts-atlas frames `y.zQa="perfect"` /
        // `y.wQa="great"`; the full `Cr` plate set is `uQa="fight"`,
        // `BQa="round"`, `zQa="perfect"`, `wQa="great"`, `DQa="timesup"`,
        // `AQa="ringout"`, `Kna="label_lose"`, `Lna="label_win"`. The "K.O."
        // label was a port invention; the ko banner is a HOLD only and draws
        // no flat text (the atlas path in screens.cpp `banner_atlas_frame`
        // already returns nullptr for it — no invented frame is added here).
        case banner_kind::ko:     return "";
        case banner_kind::victory: return "VICTORY";
        case banner_kind::defeat: return "DEFEAT";
        default:                  return "";
    }
}

// The banner's progress through its hold, clamped to 0..1 (for the
// fade/scale-in; the victory/defeat banner has no timer and holds at 1.0).
// JS `Cr.Sc` counts DOWN from the `fu` value, so the progress is the
// elapsed fraction of that value (`banner_total_`).
float FightController::banner_progress() const {
    if (banner_total_ <= 0.0f) return 1.0f;
    const float p = (banner_total_ - banner_time_) / banner_total_;
    return std::max(0.0f, std::min(1.0f, p));
}

// JS `Cr.fu` (L2026): `this.Sc=a; this.X(!0); this.wU=!0; this.thb.Z(type)`.
// `Cr.tca` (L2023) then clears `wU` and schedules a 500 ms `wh.delay` that
// re-arms it, so the round-break plate passes `arm_after_delay = true`.
void FightController::banner_show(banner_kind kind, float seconds,
                                  banner_action action, bool arm_after_delay) {
    cur_banner_ = kind;       // JS `Cr.type`
    banner_time_ = seconds;   // JS `Cr.Sc`
    banner_total_ = seconds;
    banner_armed_ = !arm_after_delay;  // `fu` arms; `tca` clears it again
    banner_arm_delay_ = arm_after_delay ? kJsBannerArmDelaySeconds : 0.0f;
    banner_action_ = action;
    banner_start_ = frame_;
}

// JS `Cr.aa` (L2027): `!this.pause && this.wU && (this.Sc -= a,
// this.Sc <= 0 && this.ONa())`. `Sc` is a SECONDS countdown (the old port
// used invented frame counts). `Cr.pause` is the HUD pause flag; the whole
// fight update is frozen while paused, so it is always false here.
void FightController::banner_tick(float dt) {
    // [ROUND-plate lead-in] The intro's plate clock is held until the `ik` VS
    // overlay ends (`release_intro`); nothing ticks before then.
    if (intro_hold_) return;
    if (banner_arm_delay_ > 0.0f) {
        banner_arm_delay_ -= dt;
        if (banner_arm_delay_ > 0.0f) return;
        banner_arm_delay_ = 0.0f;
        banner_armed_ = true;   // the `wh.delay(...,500)` callback fired
    }
    if (!banner_armed_) return;
    banner_time_ -= dt;
    if (banner_time_ <= 0.0f) banner_expire();
}

// The `ik` VS overlay (`screens.cpp`) is gone: start the intro's plate clock
// AND enter the start stance (phase 1) immediately. The JS creates the fight
// only after `ik.kg` (L2071), so its first recorded frame is phase 1 at f=0
// with no idle lead-in (reference/traces/oracle_pose.jsonl). The ROUND plate
// raised by `init_locks` keeps ticking as a display-only banner over the
// stance. Idempotent: `intro_hold_` stays false after the first call, so the
// per-frame `!vs_active_` guard cannot restart the stance.
void FightController::release_intro() {
    if (!intro_hold_) return;
    intro_hold_ = false;
    enter_start_stance();
}

// JS `Cr.ONa` (L2026): `this.X(!1); this.wU=!1; this.yA.Z(this.type)`.
// `Ar.E1` (L2017) wires that vector to `Ar.ZHa` (L2020), which clears the
// plate and re-fires `Ar.yA` — where `ca.ggb` (L383) registered `ca.vhb`
// (L410): `switch(this.ha.lp()){case 1:this.Z2(); case 2:case 3:this.FNa();
// case 5:this.Rkb()}` (`Ar.lp` L2017 = `this.Se.type`). The port carries
// the same dispatch in `banner_action_` because its `banner_kind` values are
// NOT the JS type numbers.
void FightController::banner_expire() {
    const banner_kind kind = cur_banner_;
    const banner_action action = banner_action_;
    cur_banner_ = banner_kind::none;
    banner_time_ = 0.0f;
    banner_armed_ = false;
    banner_arm_delay_ = 0.0f;
    banner_action_ = banner_action::none;
    const char* what = kind == banner_kind::round ? "ROUND"
                       : kind == banner_kind::fight ? "FIGHT"
                       : kind == banner_kind::ko ? "K.O."
                       : kind == banner_kind::victory ? "VICTORY"
                       : kind == banner_kind::defeat ? "DEFEAT" : "none";
    std::fprintf(stdout, "[fight] banner expiry: %s (F%d)\n", what, frame_);
    std::fflush(stdout);
    switch (action) {
        case banner_action::begin_round:
            // JS `vhb` (L410) case 2/3 -> `FNa` (L409): phase 1.
            enter_start_stance();
            break;
        case banner_action::next_round:
            // The port's stand-in for the JS end-stance gate
            // (`kg` L387 -> `h4a` L413 -> `Ewb` L404 -> `h9` -> `Onb`
            // L411): `ZK(); NA(); Z2()` — the round AUTO-advances.
            between_rounds_recover();
            round_start();
            break;
        case banner_action::none:
        default:
            break;
    }
}

// JS `ca.Ea` (L385) + `ia` (L388): the per-frame fight update.
void FightController::update(float dt) {
    // The fight frame counter (JS `ca.frame`): counts the LIVE phases only.
    // The idle lead-in (the ROUND plate, before phase 1) keeps it at 0 so
    // `frame_ == 0` coincides with phase 1 (`enter_start_stance` resets it).
    if (phase_ != fight_phase::idle) ++frame_;
    // [fx] The particle pool + the hit judder/hit-stop tick (presentation
    // only — runs even after the battle ends so the KO burst finishes and
    // the camera kick settles back to 0; neither touches the simulation).
    // JS `ql.Fnb` + `ql.d3a` run in the camera's `Ea()` (L364/L363).
    fx_.update();
    // Magic/effect containers (JS `tl.WL` L837 -> `Gq.WL`/`Hq.WL`; the
    // timescale `1/v.on()` = 1.0 here). Presentation only. The two owner
    // anchors feed the follow update (`bv.update` L834) for `follow` effects.
    const sf2::scene::EffectAnchor fx_anchors[2] = {
        {player_.fighter.world_x(), player_.fighter.world_y(),
         player_.fighter.facing()},
        {enemy_.fighter.world_x(), enemy_.fighter.world_y(),
         enemy_.fighter.facing()},
    };
    magic_fx_.update(1.0f, fx_anchors, 2);
    // Child models (JS `wd.vd` — the `<CreatePlayer>` spawns): advance their
    // clips + fire their own `AnimationEnd` actions. Presentation only.
    update_children();
    camera_.tick_hit_effect();
    // JS `ql.f3a()` (L367) — the intro-lens ease (part of the camera update
    // chain `ql.dZa`->`tyb`->`dZa`->`c3a`->`d3a`->`f3a`, L369).
    camera_.tick_zoom_effect();
    // JS `wd.MOa()` (L532): `ca.Ka()` (the player) advances its ability
    // cooldowns when `ca.Ka().eu == 2`. `v.on()` = 1 here.
    player_.fighter.tick_ability_cooldowns(1.0f);
    if (battle_over_) return;

    // The K.O. slow-mo beat (JS: the KO freeze): the first 30 frames of
    // the K.O. banner run the simulation at half speed. Presentation-
    // driven, but it only scales `dt` — by the time the ko banner is up
    // the fight is already in end_stance, so the pose stream is
    // unaffected.
    if (cur_banner_ == banner_kind::ko && frame_ - banner_start_ < 30) {
        dt *= 0.5f;
    }

    // The cp==7 LoseFall edge (JS `ca.Lgb` L387) is a one-frame pulse: it is
    // set by `apply_hit` and consumed by `rules_frame()` later this same
    // update, so clear it here at the frame boundary.
    player_.reaction_fall = false;
    enemy_.reaction_fall = false;

    // The banner countdown — JS `Cr.aa` (`sf2.502f0946.js` L2027):
    // `!this.pause && this.wU && (this.Sc -= a, this.Sc <= 0 && this.ONa())`.
    // `Sc` is SECONDS and the machine is NOT phase-gated. In the JS it runs
    // at the END of the fight update (`ca.ia` L389 ends with
    // `this.Onb(); a=this.ha; a!=null&&a.ia()`), so the tick sits after the
    // phase switch below: an expiry that dispatches `FNa`/`Rkb`/`Z2` takes
    // effect on the next frame, exactly as in the JS.
    // (The old port used invented 60/40-frame holds and chained ROUND N ->
    // FIGHT! here; both are gone — the plates are raised by their JS
    // callers: the round-break plate by `Z2`/init, the FIGHT! plate when
    // the stance ends.)

    // The phase machine.
    switch (phase_) {
        case fight_phase::idle:
            // Not started (bob's xF(0) state) — the demo starts at
            // start_stance, so nothing to do.
            break;
        case fight_phase::start_stance: {
            // JS: the StartStance animation plays once; when it finishes
            // the fight enters phase 2 (`kg` handler: eu==1 && anim.OCa()
            // -> Am()/xF(2)).
            // [FIX Phase 4a — intro plays the stance clip] The oracle trace
            // (reference/traces/console.log) shows phase 1 running the
            // ENEMY's `FistsStartStance-Left` animation: F2..F134 =
            // 133 frames = (stance_1 46 frames - FirstFrame 2)*3 + 1
            // (XJ=2 subframe pacing). The old code held phase 1 for a
            // hardcoded 134 frames WITHOUT animating the fighters (the
            // intro was frozen) — the "too fast" + frozen intro symptom.
            // Now the fighters play the stance clip during phase 1.
            ++start_stance_frames_;
            update_fighter(player_, enemy_, dt);
            update_fighter(enemy_, player_, dt);
            if (start_stance_frames_ >= kStartStanceFrames) {
                // JS `kg` (L387): `this.eu==1 && a` (the stance clip
                // finished) -> `this.Da.type!="FightNone" ? this.Am() :
                // this.xF(2)`. `Am()` raises the FIGHT! plate (`Cr.Zy`
                // L2024, `fu(1.166)`) and only its expiry (`vhb` L410 case
                // 5) calls `Rkb`. The traced configuration takes the
                // `xF(2)` branch — `oracle_pose.jsonl` shows `phase` 1 -> 2
                // at f=134 — so the port enters phase 2 here and shows the
                // plate for the same JS `fu(1.166)` hold (display only).
                banner_show(banner_kind::fight, kJsBannerHoldSeconds,
                            banner_action::none, false);
                std::fprintf(stdout, "[fight] banner: FIGHT! (F%d)\n", frame_);
                std::fflush(stdout);
                enter_fight();
            }
            break;
        }
        case fight_phase::fight: {
            // The round timer (JS `Sf.iPa` L2036 — `--xU`, `NF = xU/60|0`;
            // C++ `/` truncates toward zero = JS `|0`). Ticks while `Vt`.
            if (round_.running) {
                --round_.time_xu;
                round_.time_nf = round_.time_xu / 60;
            }
            // Both fighters act (JS `ca.Hnb` -> each `wd.ia`).
            update_fighter(player_, enemy_, dt);
            update_fighter(enemy_, player_, dt);

            // Hit detection + damage (JS `ca.Enb` + `Cgb`). Gated on the
            // player's round-over latch `kh` exactly as `ca.Hnb` (L389):
            // `if(!this.kc.kh) for(this.Enb(), this.Bg.Bx(), ...)` — once
            // `E3a` (L413) latches the round over, no further hit test runs
            // for the rest of the round.
            if (!player_.kh) {
                const sf2::scene::MoveDef* p_move = player_.fighter.current_move();
                const sf2::scene::MoveDef* e_move = enemy_.fighter.current_move();
                const sf2::scene::Interval* hit_iv = nullptr;
                sf2::scene::HitCapsule hit_cap;
                sf2::scene::CapsuleHit ch;
                const sf2::scene::HitCapsule* atk_cap = nullptr;
                bool hit_player = false, hit_enemy = false;
                if (p_move != nullptr &&
                    hza_pick(enemy_, *p_move, player_.fighter.move_frame()) != nullptr) {
                    hit_enemy = hit_test(player_, enemy_, *p_move, player_.fighter.move_frame(),
                                         hit_cap, ch, hit_iv, atk_cap);
                }
                if (e_move != nullptr && !hit_enemy &&
                    hza_pick(player_, *e_move, enemy_.fighter.move_frame()) != nullptr) {
                    hit_player = hit_test(enemy_, player_, *e_move, enemy_.fighter.move_frame(),
                                          hit_cap, ch, hit_iv, atk_cap);
                }
                if (hit_iv != nullptr) {
                    if (hit_enemy) {
                        apply_hit(player_, enemy_, *p_move, *hit_iv, hit_cap, ch, frame_,
                                  atk_cap);
                    } else {
                        apply_hit(enemy_, player_, *e_move, *hit_iv, hit_cap, ch, frame_,
                                  atk_cap);
                    }
                }
            }

            // JS `wd.Ax` -> `Vx.wyb` (`iu`, g="C5"), once per frame per
            // fighter: `this.v1&&(++this.OV, this.OV>v.pCa()&&(this.j2=
            // this.Ui, this.reset()))` where `reset(){v1=!1; Ui=tf=OV=0}`.
            // `pCa()` = `v.Lpa` = internal_settings `<Combo Time="90"/>`.
            // Ordered AFTER the hit pass so a landed hit's `wgb` (`OV=0`)
            // precedes the tick — exactly `Te.ia`'s `da.ia()` (strike) then
            // `this.Ax()`. This is the combo time decay the port lacked.
            {
                const int combo_window = fight_params().combo_time;
                for (FightFighter* fr : {&player_, &enemy_}) {
                    if (!fr->combo_active) continue;
                    if (++fr->combo_frames > combo_window) {
                        fr->combo_active = false;
                        fr->combo_frames = 0;
                        fr->combo_run = 0;
                    }
                }
            }

            // JS `ca.o1a` L403 / `kg` L387: the `FightNone` viewer has NO
            // round flow — the round-end checks (`Onb` -> `E3a`) and the
            // stage-rule pass (`du.Ih`) never run for it. Everything else in
            // the phase-2 body (fighter step, hit detection, sparks) is the
            // same live path the fight uses.
            if (!fight_none_) {
                // JS `ca.ia` L389 `PC(1,3)` -> `du.Ih(1,3,ze)` (rule detection:
                // the Ringout field exit + the TimeOutWin timer end). Runs
                // before the round-end check so the fired `Pu` ends the round.
                rules_frame();

                // The round-end check (JS `Onb`).
                check_round_end();
            }

            // [fx] Latch this frame's strike-capsule endpoints as the next
            // frame's `sx.mf`/`Zs.mf` (JS `Vc.f4`/`sk` L794-796). The hit
            // pass above read the PREVIOUS frame's values, so snapshot after.
            snapshot_capsule_ends(player_);
            snapshot_capsule_ends(enemy_);
            break;
        }
        case fight_phase::end_stance: {
            // The EndStance hold: the KO/timeout banner shows for a moment
            // (JS `Pf` plays the end animation, then the HUD advances).
            ++end_stance_frames_;
            // The next round (or the battle end) is handled by
            // apply_round_result — nothing to do here.
            break;
        }
    }

    // The global `<Triggers>` EveryFrame publish (JS `kz.create` `Mm`,
    // L771): the trigger bus fires type 14 once per frame while the model's
    // animator runs. The port publishes it for BOTH sides once per frame in
    // the two live phases — exactly where `update_fighter` runs (the
    // `tick_bus_side` perk-bus EveryFrame is the per-side analogue).
    // `Mm.compare` (L767) filters on `data % Step` when `<EveryFrame Step>`
    // is set; `Step` is not part of the move `Cond` record, so the port
    // publishes unconditionally (Step is absent from all 4 shipped
    // EveryFrame triggers).
    if (phase_ == fight_phase::fight || phase_ == fight_phase::start_stance) {
        dispatch_global_triggers("EveryFrame", "EveryFrame");
    }

    // The banner countdown (JS `ca.ia` L389: `this.Onb(); a=this.ha;
    // a!=null&&a.ia()` — the HUD tick that runs `Cr.aa`). It sits AFTER the
    // phase machine, so a dispatch (`FNa`/`Rkb`/`Z2`) is applied from the
    // next frame on — the JS ordering.
    banner_tick(dt);

    // The camera follows the fight (JS ql.Ea -> tyb/dZa/c3a + ma.Sya).
    // JS `ql.tyb` (L363 + the L535/L581 `Dl.mea(a.Eu,b.Eu)`) targets the
    // midpoint of the two fighters' Center-Of-Mass BODY (`Eu.ma`, the
    // mass-weighted centroid computed by `Dl.v6` L577), NOT the render root
    // (`Fe().ma` = the NPivot anchor fed to `dv.ia`). Feed the COM.
    camera_.framing(player_.fighter.com_x(), player_.fighter.com_y(),
                    enemy_.fighter.com_x(), enemy_.fighter.com_y(), 1280.0f, 720.0f);

    // The HUD state.
    hud_.set_hp(player_.hp, player_.max_hp, enemy_.hp, enemy_.max_hp);
    hud_.set_timer(hud_timer());
    hud_.set_round(round_.number, battle_.rounds);
    hud_.set_phase(phase_);

    // The per-second log line.
    time_since_log_ += dt;
    if (time_since_log_ >= 1.0f) {
        time_since_log_ = 0.0f;
        FightLogLine& l = last_log_;
        l.frame = frame_;
        l.phase = static_cast<int>(phase_);
        l.round = round_.number;
        l.timer = hud_timer();
        l.p_move = player_.last_move;
        l.e_move = enemy_.last_move;
        l.p_hp = player_.hp;
        l.e_hp = enemy_.hp;
    }

    // [trace] The per-frame pose dump (a no-op unless --dump-pose armed it).
    dump_pose_frame();
}

// --- pose dump (trace infrastructure, Phase 0) -------------------------------

FightController::~FightController() {
    if (pose_dump_file_ != nullptr) {
        std::fclose(pose_dump_file_);
        pose_dump_file_ = nullptr;
    }
}

void FightController::set_pose_dump(const std::string& path, int frames) {
    pose_dump_path_ = path;
    pose_dump_frames_ = frames;
}

// One JSONL line per frame (reference/traces/native_pose.jsonl contract):
// {"t":"frame","f":..,"phase":..,"round":..,"timer":..,"cam":{..},
//  "fighters":[Me, Enemy]} - Me first, then the enemy. `timer` = the HUD
// countdown seconds (`NF` — was elapsed-up `int(time)`, which had no JS
// counterpart: `$t.time` is write-once). Read-only over the simulation:
// nothing here mutates the fight state.
void FightController::dump_pose_frame() {
    if (pose_dump_frames_ <= 0) return;
    if (pose_dump_file_ == nullptr) {
        if (fopen_s(&pose_dump_file_, pose_dump_path_.c_str(), "wb") != 0 ||
            pose_dump_file_ == nullptr) {
            std::fprintf(stderr, "[dump] cannot open %s — pose dump disabled\n",
                         pose_dump_path_.c_str());
            pose_dump_frames_ = 0;
            return;
        }
    }

    const FightFighter* fighters[2] = {&player_, &enemy_};
    // The dumped zoom = the LAYER zoom Bj (Ut.xCa) — what the oracle trace
    // records (its hook reads Ut.Al's this.Bj = 1.0 at the fight start),
    // NOT the camera zoom (Sya f = 1.3 at 16:9).
    std::fprintf(pose_dump_file_,
                 "{\"t\":\"frame\",\"f\":%d,\"phase\":%d,\"round\":%d,\"timer\":%d,"
                 "\"cam\":{\"cx\":%.6f,\"cy\":%.6f,\"zoom\":%.6f},\"fighters\":[",
                 frame_, phase(), round_.number, round_.time_nf,
                 camera_.center_x, camera_.center_y, camera_.zoom_layer);
    for (int i = 0; i < 2; ++i) {
        const Fighter& f = fighters[i]->fighter;
        const std::vector<float>& pos = f.positions();
        // The clip identifier (JS `da.Ua.name`): the ARCHIVE clip name —
        // MoveDef::file_name (the XML FileName) minus any ".bytes" suffix
        // (fighter.cpp strips the same suffix before the archive lookup),
        // falling back to the move label when no file is attached. The
        // mirror variant (if any) is reflected in `fx`; the clip is the
        // archive name of the clip actually playing.
        std::string clip_name;
        if (const sf2::scene::MoveDef* m = f.current_move()) {
            clip_name = m->file_name;
            const std::string suffix = ".bytes";
            if (clip_name.size() > suffix.size() &&
                clip_name.compare(clip_name.size() - suffix.size(), suffix.size(),
                                  suffix) == 0) {
                clip_name = clip_name.substr(0, clip_name.size() - suffix.size());
            }
            if (clip_name.empty()) clip_name = m->name;
        }
        std::fprintf(pose_dump_file_,
                     "%s{\"id\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"fx\":%d,\"clip\":\"%s\","
                     "\"cf\":%d,\"sub\":%d,\"subn\":%d,\"bones\":[",
                     i == 0 ? "" : ",", i == 0 ? "Me" : "Enemy",
                     f.world_x(), f.world_y(), f.facing(), clip_name.c_str(),
                     f.move_frame(), f.subframe(), f.sub());
        for (std::size_t b = 0; b + 1 < pos.size(); b += 2) {
            std::fprintf(pose_dump_file_, "%s[%.3f,%.3f]", b == 0 ? "" : ",", pos[b],
                         pos[b + 1]);
        }
        std::fprintf(pose_dump_file_, "]}");
    }
    std::fprintf(pose_dump_file_, "]}\n");

    if (++pose_dump_written_ >= pose_dump_frames_) {
        std::fclose(pose_dump_file_);
        pose_dump_file_ = nullptr;
        pose_dump_frames_ = 0;
        std::fprintf(stdout, "[dump] pose trace complete: %d frames -> %s\n",
                     pose_dump_written_, pose_dump_path_.c_str());
        std::fflush(stdout);
    }
}

} // namespace sf2::scene

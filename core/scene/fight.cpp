// Fight controller implementation (Phase 3.5).
// Ported from the game's `ca` class (sf2.502f0946.js L379-433). See
// core/scene/README.md "Fight controller (Phase 3.5)" for the JS study.

#include "scene/fight.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "audio/audio.hpp"

namespace sf2::scene {

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
void FightCamera::framing(float ax, float ay, float bx, float by, float view_w,
                          float view_h) {
    // The CoM y's (ay/by) are the JS `Eu.ma` vertical target — the native
    // keeps the verified floor@0.78 composition instead of the oracle's
    // dummy-CoM (see the struct comment); the horizontal target uses ax/bx.
    (void)ay;
    (void)by;
    // --- the Sya zoom (exact JS L1833) ----------------------------------
    const float aspect = view_w / view_h;
    const float span = std::fabs(bx - ax);             // d = qh.ECa() = |x1-x2|
    const float e = arena_h;                           // m$a() = Lb.height*Bj, Bj = 1
    float f = view_h / e;
    f *= (aspect < 0.45f ? 0.45f : aspect > 1.0f ? 1.0f : aspect);  // c<.45?.45:c>1?1:c
    if (aspect < 0.8f) {
        f *= 0.8f + ((std::max(0.5f, std::min(0.8f, aspect)) - 0.5f) / 0.3f) * 0.2f;
    }
    f *= std::min(1.0f, view_w / (span * f + 100.0f));  // min(viewW/(d*f+100),1)
    const float dmin = 0.6f +
                       ((std::max(0.5f, std::min(1.0f, aspect)) - 0.5f) / 0.5f) *
                           0.7f;                        // the min zoom 0.6..1.3
    if (f < dmin) f = dmin;
    // The layer zoom Bj (Ut.xCa L831): min(nC/(span+300),1) — nC = the
    // half-view world width (mwa: b/Ira, Ira = viewH/arenaH). BJ drives
    // the layer scaling AND the panorama clamp (Ut.Al); the oracle trace
    // records Bj as its "zoom" (trace.js hooks Ut.Al and reads this.Bj —
    // 1.0 at the fight-start span: 995.6/583 < 1). The RENDER zoom stays
    // the Sya f (1.3 at 16:9) — both numbers come from the spec: the
    // camera zoom is Sya's f (L1833), the layer/pano zoom is Ut.Bj (L826).
    const float n_c = view_w / (view_h / e);           // mwa: nC = b/Ira
    zoom_layer = std::min(1.0f, n_c / (span + 300.0f));  // Ut.xCa() -> Bj
    zoom = f;

    // --- the target (ql.tyb + the vertical floor anchor) ----------------
    // First call: start at the spawn midpoint (Lb.z9a) — the f=0 oracle
    // view (831.5, -101.5) — without advancing the chase; the first real
    // frame then produces the oracle's -50 intro jump.
    if (!initialized_) {
        du_prev_x_ = go_x_ = go_prev_x_ = start_x_;
        du_prev_y_ = go_y_ = go_prev_y_ = start_y_;
        du_x_ = start_x_;
        du_y_ = start_y_;
        initialized_ = true;
        center_x = start_x_;
        center_y = start_y_;
        return;
    }
    du_x_ = (ax + bx) * 0.5f;                          // By = mid of the CoM's x
    // The vertical target: the arena FLOOR line at 0.78 of the view height
    // (F9*(1-zoom) keeps the line anchored at any zoom — JS Ut.init
    // F9 = (Lb.height/2 - ct)/2).
    const float floor_screen_y = view_h * 0.78f;
    const float vshift = ((arena_h / 2.0f - floor) / 2.0f) * (1.0f - zoom);
    du_y_ = floor + vshift - (floor_screen_y - view_h / 2.0f) / zoom;

    // --- the smoothing (ql.dZa, exact) ----------------------------------
    const float d_x = du_x_ - du_prev_x_;              // $X = By - DO
    const float d_y = du_y_ - du_prev_y_;
    const float b_y_x = go_prev_x_ + d_x;              // bY = iq + $X
    const float b_y_y = go_prev_y_ + d_y;
    float lo_x = (b_y_x - go_x_) + (du_x_ - go_x_) * 0.15f;  // LO = aY + IO
    float lo_y = (b_y_y - go_y_) + (du_y_ - go_y_) * 0.15f;
    const float lo_len = std::sqrt(lo_x * lo_x + lo_y * lo_y);
    if (lo_len > 200.0f) {                             // |LO|>200 -> 200
        lo_x *= 200.0f / lo_len;
        lo_y *= 200.0f / lo_len;
    }
    go_x_ += lo_x;                                     // Jl += LO
    go_y_ += lo_y;
    float vg_x = go_x_ - go_prev_x_;                   // VG = Jl - iq
    float vg_y = go_y_ - go_prev_y_;
    const float vg_len = std::sqrt(vg_x * vg_x + vg_y * vg_y);
    if (vg_len > 50.0f) {                              // |VG|>50 -> 50
        go_x_ = go_prev_x_ + vg_x * 50.0f / vg_len;
        go_y_ = go_prev_y_ + vg_y * 50.0f / vg_len;
    }
    du_prev_x_ = du_x_;                                // DO = Du.ma
    du_prev_y_ = du_y_;
    go_prev_x_ = go_x_;                                // iq = Go.ma (post-XA)
    go_prev_y_ = go_y_;

    // --- the panorama clamp (Ut.Al: Io = arenaWidth/2 - focus -----------
    // clamped to +/-((arenaWidth-oGa)*Bj*0.5 - nC*0.5)). The native
    // camera's Io = arena_center - center_x, so the clamp bounds the
    // smoothed focus x by the same range (the fight never leaves the
    // arena view — the floor stays fully covered). n_c = the mwa half-view
    // width (computed above in the zoom section); the clamp uses the
    // LAYER zoom Bj (Ut.Al's d formula), not the camera zoom.
    const float d_io = (arena_w - 0.0f) * 0.5f * zoom_layer - n_c * 0.5f;
    const float center = arena_w * 0.5f;
    center_x = go_x_ < center - d_io   ? center - d_io
                : go_x_ > center + d_io ? center + d_io
                                        : go_x_;
    center_y = go_y_;

    // --- the portrait vertical shift (Sya: c<1 && b.D(...)) -------------
    if (aspect < 1.0f) {
        center_y += std::round((view_h - e * zoom) / 2.0f) / zoom * 0.5f;
    }
}

// The hit-shake kick (JS `d3a` + `DL` — see the FightCamera comment). The
// RNG is a PRIVATE LCG (same shape as EffectSystem's): std::rand would be
// fine for a visual-only roll, but a private LCG keeps the shake fully
// deterministic AND independent of the fight's shared roll01 (which must
// never be consumed by presentation code — it would perturb the AI
// decisions and diverge the pose dump from the oracle).
namespace {
std::uint32_t g_shake_lcg = 0x9E3779B9u;  // private, fixed seed
float shake_next01() {
    g_shake_lcg = 1664525u * g_shake_lcg + 1013904223u;
    return static_cast<float>(g_shake_lcg >> 8) * (1.0f / 16777216.0f);
}
}  // namespace

void FightCamera::shake(float intensity) {
    shake_x_ = intensity * (shake_next01() - 0.5f) * 2.0f;
    shake_y_ = intensity * (shake_next01() - 0.5f) * 2.0f;
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
                           std::function<float()> roll01) {
    init_locks(battle, model, moves, clips, tactics, tactic, player_name, enemy_name,
               player_x, player_y, enemy_x, enemy_y, player_max_hp, enemy_max_hp,
               std::move(roll01), {});
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
    const std::vector<std::pair<std::string, std::string>>& player_owned) {
    battle_ = battle;
    model_ = model;
    moves_ = &moves;
    clips_ = &clips;
    tactics_ = tactics;
    tactic_ = tactic;
    roll01_ = std::move(roll01);

    player_ = make_fighter(player_name, true, player_x, player_y, player_max_hp,
                           "Fists", player_owned);
    enemy_ = make_fighter(enemy_name, false, enemy_x, enemy_y, enemy_max_hp, "Fists", {});
    // [FIX Phase 4b — manual control] The player is MANUAL: no AiController,
    // no auto-attack. The input path (player_input -> Fighter::input ->
    // try_select_move) drives the player's moves; the enemy keeps the AI.
    // The demo callers that want an AI player (app/ai_demo) attach their own
    // controller. (The old code attached the player AI unconditionally, which
    // overrode the user's key input — "no input" + the player
    // animating randomly.)

    // Sample the initial stance idle (JS: the weapon's stance idle clip).
    sample_idle(player_);
    sample_idle(enemy_);
    rebuild_body(player_);
    rebuild_body(enemy_);

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
    enter_start_stance();
    // The FIRST round's ROUND 1 banner. round_start() — which raises it for
    // rounds 2+ — is NOT called for the first round (init enters
    // start_stance directly with round_.number = 0), so raise it here.
    // Presentation only; round_.number stays 0 (the pose dump's "round"
    // field is byte-identical).
    cur_banner_ = banner_kind::round;
    banner_round_ = round_.number;   // 0 -> "ROUND 1"
    banner_start_ = frame_;
    banner_len_ = 60;
    std::fprintf(stdout, "[fight] banner: ROUND %d\n", banner_round_ + 1);
    std::fflush(stdout);
}

// JS `o1a` (L403) + `Gf` (L403-404): build one fighter. The move list is
// the TacticWeapon-based list (`weapon_subtype`) or, when `owned` is
// non-empty, the Locks-based list against the fighter's items (JS `ra.Hza`
// L684-685 — the equipment flow adds the weapon's moves).
FightFighter FightController::make_fighter(
    const std::string& nm, bool is_player, float x, float y, int max_hp,
    const std::string& weapon_subtype,
    const std::vector<std::pair<std::string, std::string>>& owned) {
    FightFighter f;
    f.name = nm;
    f.is_player = is_player;
    f.fighter.set_model(model_);
    // [FIX Phase 4b — black silhouettes] The fighters' fill color is the
    // LOCATION's Root Color (the dojo_params `<Root Color="0x000000">`),
    // not a hardcoded team color — the oracle's fighters are black
    // silhouettes (JS `Na.cd` fills the Path2D mesh with the location
    // color). set_fighter_color (the location-scene root color) is applied
    // by the fight screen after init_locks; the default here is black so
    // the standalone demos (which have no location) also draw black.
    f.fighter.set_color(fighter_color_);
    f.fighter.set_clip_lookup(
        [this](const std::string& name) -> const sf2::data::anim_clip* {
            const auto it = clips_->find(name);
            return it != clips_->end() ? &it->second : nullptr;
        });
    if (owned.empty()) {
        f.fighter.build_move_list(*moves_, weapon_subtype);
    } else {
        f.fighter.build_move_list_locks(*moves_, owned);
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
    f.params.attributes["UnarmedDamage"] =
        is_player ? battle_.player_unarmed_damage : 0.0f;
    f.params.attributes["BodyDefense"] = 0.0f;
    f.params.attributes["HeadDefense"] = 0.0f;
    f.params.attributes["CriticalChance"] = 0.0f;
    f.params.attributes["CriticalDamage"] = 0.0f;
    f.params.attributes["BlockDamageFactor"] = 0.0f;
    f.params.attributes["DamageFactor"] = 0.0f;
    f.max_hp = static_cast<float>(max_hp);
    f.hp = f.max_hp;
    if (!is_player && tactic_ != nullptr) {
        f.ai = std::make_unique<sf2::scene::AiController>();
        f.ai->init("Fists", tactics_, tactic_, moves_);
    }
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

// JS `xF` (L388): set the fight phase and sync the fighters' `Je` stance
// (the move conditions' RoundStage reads it).
void FightController::set_phase(fight_phase p) {
    phase_ = p;
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

// JS `Z2` (L408-409): round start — increments the round counter, re-syncs
// the fighters and enters phase 1 (StartStance).
void FightController::round_start() {
    // Snapshot the pre-round HP (JS Pm/vo) — the recovery is applied in
    // between_rounds_recover() when the previous round ends.
    round_.number++;
    round_init();
    dga_ = false;  // JS `Dga` reset per round (L409)
    // JS `wd.wI` per-round re-init: `Wx=-1`, `sr=0` (`vc`/`sn` persist).
    player_.shock.pain_sr = 0.0f;
    player_.shock.weapon_wx = -1;
    enemy_.shock.pain_sr = 0.0f;
    enemy_.shock.weapon_wx = -1;
    // The ROUND N banner (presentation only — see banner_kind).
    cur_banner_ = banner_kind::round;
    banner_round_ = round_.number;
    banner_start_ = frame_;
    banner_len_ = 60;
    std::fprintf(stdout, "[fight] banner: ROUND %d\n", banner_round_ + 1);
    std::fflush(stdout);
    // Reset the round flags on the fighters (JS `c.parameters.nob()`).
    player_.fighter.set_enemy_x(enemy_.fighter.world_x());
    enemy_.fighter.set_enemy_x(player_.fighter.world_x());
    enter_start_stance();
}

// JS `FNa` (L409): phase 1 — fighters at their spawn, no input yet.
void FightController::enter_start_stance() {
    // Respawn the fighters at their spawn positions (JS `tja`/`Qlb`).
    // [FIX Phase 4a — fighters on the floor] The dojo spawn Y (-110/-93,
    // the ModelsViewer Y) is the COM's world y, but the clip's COM sits
    // ~125 world units ABOVE the feet (stance_1: COM_y=-140, feet_y=-15;
    // the COM is the body center). Anchoring the COM at the spawn put the
    // feet ~440 px above the visible floor — "fighters in nowhere". The
    // game's physics (Al solver) rests the fighter on the arena floor
    // (dojo_params Floor="80"); the native places the COM so the clip's
    // ground-contact bones land on the floor line. The floor offset is
    // taken from the stance clip the fighter samples at spawn (feet y -
    // COM y of the first clip frame).
    player_.fighter.set_world_pos(battle_.player_spawn_x, battle_.player_spawn_y);
    enemy_.fighter.set_world_pos(battle_.enemy_spawn_x, battle_.enemy_spawn_y);
    sample_idle(player_);
    sample_idle(enemy_);
    rebuild_body(player_);
    rebuild_body(enemy_);
    set_phase(fight_phase::start_stance);
    round_live_ = false;
    start_stance_done_ = false;
    start_stance_frames_ = 0;   // reset so every round re-plays the intro
    start_buffer_filled_ = false;  // fresh round, empty round-start buffer
}

// JS `Rkb` (L410): phase 2 — the round goes live (HUD play() sets
// round.Vt = true, the timer starts).
void FightController::enter_fight() {
    set_phase(fight_phase::fight);
    round_live_ = true;
    round_.running = true;   // the timer counts down (JS Sf.play L2037)
    start_stance_done_ = true;
    // [FIX idle-slide] Cut the intro stance clip (stance_1/stance_2,
    // root-moving) so the fighters don't keep sliding 853 units into the
    // idle phase. The intro clip was re-triggered at f130 because its
    // 129-frame duration is 4 short of the 133-frame StartStance, so a
    // right-facing fighter entered Fight still on stance_2 (delta -148.6).
    // Clearing forces the next update_fighter to pick the static
    // FistsStartStanceIdle-Left (fists1_stance_idle, 38f, delta 0).
    player_.fighter.clear_move();
    enemy_.fighter.clear_move();
    sample_idle(player_);
    sample_idle(enemy_);
    rebuild_body(player_);
    rebuild_body(enemy_);

    // JS `llb` (L429): replay the StartStance input buffer as if the player
    // pressed NOW — `WC != -1 && (c.yJa(c.WC), c.WC = -1)`. The buffered tap
    // joins the player's key buffer; the next update_fighter picks it up via
    // try_select_move (the manual input path), so the press made before the
    // round is not lost.
    if (start_buffer_filled_) {
        player_.fighter.input(start_buffer_key_, press_type::tap);
        start_buffer_filled_ = false;
    }
}

// JS `i4a` (L409): phase 3 — the round's EndStance (results shown).
void FightController::enter_end_stance() {
    set_phase(fight_phase::end_stance);
    round_.running = false;
    round_live_ = false;
    end_stance_frames_ = 0;
}

// JS `Onb` (L411): the round-end check. KO when a fighter's hp <= 0;
// timeout ONLY when the fight has the TimeoutWin rule (JS `BT` L392 sets
// `ey=2` for ERuleTimeoutWin; the shipped stages use no timeout rule).
// The timeout winner is the ENEMY (JS E3a c==3 branch: `a.ng++` on Zb).
void FightController::check_round_end() {
    if (!round_live_) return;
    // JS `Ar.PEa` (L2020): `mb.NF<=0` — the HUD counter hit zero.
    // (TimeoutWin rule only; shipped stages end on KO.)
    const bool timeout = battle_.timeout_rule && round_.time_nf <= 0;
    const bool player_ko = player_.hp <= 0.0f;
    const bool enemy_ko = enemy_.hp <= 0.0f;

    if (!player_ko && !enemy_ko && !timeout) return;

    if (timeout) {
        // The timeout rule: the enemy wins the round (JS E3a `c==3`).
        apply_round_result(round_result::timeout_win, enemy_, player_);
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

    // The K.O. banner on a knockout round end (presentation only).
    if (result == round_result::ko) {
        cur_banner_ = banner_kind::ko;
        banner_start_ = frame_;
        banner_len_ = 90;
        std::fprintf(stdout, "[fight] banner: K.O.\n");
        std::fflush(stdout);
    }

    enter_end_stance();
    history_.push_back(oc);

    // Battle end: the winner reached `round.eL` (Rounds) — JS Onb
    // `a = wo.nB.ng >= round.eL` -> `a ? bea(nB)`.
    const bool battle_end = w.rounds_won >= round_.length;    if (battle_end) {
        end_battle(w);
    } else {
        // JS `Onb` (L411-412): the next round does NOT start automatically —
        // the round ends into EndStance and the HOST waits for the player's
        // "Next" (JS: the HUD button -> `vhb` case 1 -> `Z2()`). The
        // recovery + round start happen in next_round_requested().
        round_wait_ = true;
    }
}

// JS `vhb` (L410) case 1 -> `Z2()` (L408): the HUD "Next" button pressed —
// when the host is waiting between rounds, run the recovery and start the
// next round. No-op while a round is live.
void FightController::next_round_requested() {
    if (!round_wait_) return;
    round_wait_ = false;
    between_rounds_recover();
    round_start();
}

// JS `bea` (L413): the battle end — the winner is fixed, the fight stops.
void FightController::end_battle(const FightFighter& winner) {
    battle_over_ = true;
    winner_ = &winner;
    round_.running = false;
    round_live_ = false;
    // The final banner: VICTORY for the player's win, DEFEAT for the loss
    // (presentation only; effectively forever — the results screen takes
    // over).
    cur_banner_ = winner.is_player ? banner_kind::victory : banner_kind::defeat;
    banner_start_ = frame_;
    banner_len_ = 1000000000;
    std::fprintf(stdout, "[fight] banner: %s\n",
                 winner.is_player ? "VICTORY" : "DEFEAT");
    std::fflush(stdout);
}

// JS `NA` (L414): the between-round recovery. The game heals BOTH fighters
// by `Da.qDa` (HealthRecovery, default 1) — `c.jT(this.Da.qDa)` — so the
// fighters keep their damaged HP between rounds (NOT a full reset).
void FightController::between_rounds_recover() {
    const float recover = battle_.health_recovery;
    player_.hp = std::min(player_.max_hp, player_.hp + recover);
    enemy_.hp = std::min(enemy_.max_hp, enemy_.hp + recover);
    // Reset the round flags (JS NA: zd/br/cE/kh/sn/sJ/pw/Iq).
    player_.is_winner = false;
    enemy_.is_winner = false;
}

// JS `vfa` (L413): the round winner by HP.
const FightFighter& FightController::round_winner_by_hp() const {
    if (player_.hp >= enemy_.hp) return player_;
    return enemy_;
}

void FightController::sample_idle(FightFighter& f) {
    // The Fists stance idle (the dojo archive's fists1_stance_idle — the
    // game's weapon stance idle). Fall back to the first move's clip.
    const std::string idle_name = "fists1_stance_idle";
    const auto it = clips_->find(idle_name);
    if (it != clips_->end()) {
        f.fighter.sample(it->second, 0, f.fighter.world_x(), f.fighter.world_y(),
                         f.fighter.facing());
    }
}

void FightController::rebuild_body(FightFighter& f) {
    f.body.build(f.fighter.model(), f.fighter.positions(), wall_min_, wall_max_);
}

void FightController::player_input(sf2::scene::key_type key, sf2::scene::press_type press) {
    // JS `ca.N0a` (L426): in phase 1 (StartStance) a PRESS goes into the
    // round's single-slot input buffer `WC` (the last press wins; it is
    // replayed by `llb` when the fight starts). In phase 2 the press is
    // buffered into the fighter directly (`eu==2 && b.yJa(a)`).
    if (phase_ == fight_phase::start_stance) {
        if (press == press_type::tap) {
            start_buffer_key_ = key;
            start_buffer_filled_ = true;
        }
        return;  // holds/releases during the intro are not moves — ignore
    }
    if (phase_ != fight_phase::fight) return;  // JS: input only in phases 1/2
    player_.fighter.input(key, press);
}

// JS `ca.Enb` (L390) + `wd.tKa` -> `Fu.ia`: the attack check — the
// attacker's active Attack-interval AttackingParts capsules vs the target's
// collidable capsules.
bool FightController::hit_test(FightFighter& atk, FightFighter& def,
                               const sf2::scene::MoveDef& move, int frame,
                               sf2::scene::HitCapsule& hit_cap,
                               sf2::scene::CapsuleHit& ch,
                               const sf2::scene::Interval*& hit_interval) {
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
    const auto key = std::make_pair(static_cast<const void*>(&move),
                                    static_cast<const void*>(d));
    if (last == key) return false;  // dW: already tested
    last = key;
    {
        // JS `!c.aEa` (L566-567): no AttackingParts = always connects
        // (11/618 shipped attack intervals); contact = target COM capsule.
        if (d->attacking_parts.empty()) {
            for (const auto& tgt : def.body.capsules) {
                if (!tgt.collidable) continue;
                hit_cap = tgt;
                ch.hit = true;
                ch.point.x = (tgt.p1.x + tgt.p2.x) * 0.5f;
                ch.point.y = (tgt.p1.y + tgt.p2.y) * 0.5f;
                ch.point.z = (tgt.p1.z + tgt.p2.z) * 0.5f;
                hit_interval = d;
                return true;
            }
            return false;
        }
        for (const std::string& edge : d->attacking_parts) {
            const sf2::scene::HitCapsule* atk_cap = atk.body.by_name(edge);
            if (atk_cap == nullptr) continue;
            for (const auto& tgt : def.body.capsules) {
                if (!tgt.collidable) continue;
                if (sf2::scene::capsule_capsule_overlap(*atk_cap, tgt, ch)) {
                    hit_cap = tgt;
                    hit_interval = d;
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
                                const sf2::scene::CapsuleHit& ch, int frame) {
    sf2::scene::IntervalDamage idmg;
    idmg.base_damage = iv.damage;
    idmg.no_critical = iv.no_critical;
    idmg.hit_body_part = iv.hit_name;
    idmg.attack_attrs.push_back({iv.damage_type, iv.damage_shift});
    if (!hit_cap.defense.empty()) idmg.defense_names.push_back(hit_cap.defense);
    // JS `wd.strike` (L509-510) order on the TARGET: block-break FIRST
    // (`g.DDa` -> `hT(5)`; shipped moves never set IgnoresBlock, so this
    // is dead with shipped data but faithful), then `Bb.block = Nbb()`
    // (the defender's Block interval AFTER the break), then LAa/crit.
    if (iv.ignores_block) {
        def.fighter.clear_block();
    }
    const bool blocked = def.fighter.has_block();
    // JS `wd.strike` crit (L510): `se = !block && !g.a3 && Lcb(A9a())`;
    // `Lcb(a)` = `Da.cT(a*100)` (L1204): `a>1 -> true` (the `a>b` shortcut)
    // else a fresh draw `< a`. `A9a = pga?100:gya.p8a` (L529; `pga` setter
    // OPEN -> false path). Draws come from the fight stream (`RJa` analog;
    // merged stream documented in MASTER_TODO).
    const float a9 = sf2::scene::crit_chance(atk.params);
    // `roll01_` may be empty in unit contexts — fall back to a fixed draw
    // (deterministic; never consumes the shared stream when unset).
    auto draw01 = [this]() { return roll01_ ? roll01_() : 0.5f; };
    const bool critical =
        !blocked && !iv.no_critical && (a9 > 1.0f || draw01() < a9);
    const std::string defense_attr = sf2::scene::select_defense(idmg, blocked, &hit_cap);
    const float dmg = sf2::scene::compute_damage(idmg, atk.params, def.params, defense_attr,
                                                 blocked, critical, &hit_cap);
    sf2::scene::HitRecord rec;
    rec.raw_damage = dmg;
    rec.defense = defense_attr;
    rec.target_part = hit_cap.body_part;
    rec.hit_edge = iv.attacking_parts.empty() ? "" : iv.attacking_parts[0];
    rec.blocked = blocked;
    rec.critical = critical;
    // JS `wd.R8a` shock decider on the target (L531-532 + L511):
    // `Uq` = head-zone hit; `b = Zi/atk.so` (`so` OPEN -> 1.0); `ws`
    // (weapon strike, `ola` OPEN -> false, so body strikes add pain);
    // crit/head terms = Base + attr (`p8a` pattern, OPEN exact formula).
    rec.head_hit = hit_cap.body_part == "Head";
    {
        const sf2::scene::FightParams& gfp = sf2::scene::FightParams::defaults();
        const float b = dmg;  // Zi/so with so=1.0 (OPEN)
        const bool pain_c = sf2::scene::orb_hit(
            def.shock, atk.shock.weapon_ws ? 0.0f : b, gfp.shock_threshold);
        const float crit_term =
            gfp.shock_crit_base + atk.params.attr("ShockCriticalHitChance");
        const float head_term =
            gfp.shock_head_base + atk.params.attr("ShockHeadHitChance");
        const bool ub = sf2::scene::r8a_decide(
            false, def.shock.shocked_vc, b, pain_c, crit_term, critical, draw01(),
            head_term, rec.head_hit, blocked, draw01()).raw;
        rec.shock = ub;
        // JS `Cgb` shock apply (L394): `Ub&&(vc?Ub=false:vc=true)`.
        if (ub) {
            if (def.shock.shocked_vc) {
                rec.shock = false;
            } else {
                def.shock.shocked_vc = true;
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
    sf2::scene::apply_damage(rec, def.hp, false);
    def.hp = rec.hp_after;

    // Event flags for the golden trace (JS `Sba` L393: Defense/Animation/
    // Critical/Shock/Block/Damage) — one line per landed hit.
    if (critical || rec.shock || blocked || rec.first_hit) {
        std::fprintf(stdout, "[hit] F%d %s->%s dmg=%.2f%s%s%s%s\n", frame,
                     atk.name.c_str(), def.name.c_str(), rec.final_damage,
                     critical ? " CRIT" : "", rec.shock ? " SHOCK" : "",
                     blocked ? " BLOCK" : "", rec.first_hit ? " FIRST" : "");
        std::fflush(stdout);
    }

    // JS `ca.Cgb` (L394-397): `b.block || (model.hT(5), Dga, ...)` — a
    // landed UNBLOCKED hit destroys the target's Block intervals and picks
    // a new reaction move via `Gc.DK` (d-set first-match:
    // `Fighter::try_react`; shock prefers *Fall* reactions).
    if (!blocked) {
        def.fighter.clear_block();
        sf2::scene::FightContext rctx;
        rctx.roll01 = roll01_;
        rctx.stage = sf2::scene::round_stage::fight;
        rctx.anims_me = {def.fighter.current_move() ? def.fighter.current_move()->name : ""};
        rctx.anims_enemy = {atk.fighter.current_move() ? atk.fighter.current_move()->name : ""};
        rctx.dist_x = atk.fighter.world_x() - def.fighter.world_x();
        rctx.dist_3d = std::fabs(rctx.dist_x);
        rctx.health_ratio = def.max_hp > 0.0f ? def.hp / def.max_hp : 0.0f;
        rctx.last_hit_type = critical ? "Critical" : (rec.shock ? "Shock" : "");
        rctx.candidate_moves = {};
        const std::string reaction = def.fighter.try_react(rctx, rec.shock);
        if (!reaction.empty()) {
            std::fprintf(stdout, "[react] F%d %s -> %s\n", frame,
                         def.name.c_str(), reaction.c_str());
            std::fflush(stdout);
        }
    }

    // Knockback (JS `Bl.strike` + bounds).
    sf2::scene::Vec3 impulse{iv.impulse_x, iv.impulse_y, iv.impulse_z};
    impulse.x *= static_cast<float>(atk.fighter.facing());
    sf2::scene::ImpulseResult imp;
    const float new_x =
        sf2::scene::apply_impulse(hit_cap, ch, impulse, def.fighter.world_x(),
                                  wall_min_, wall_max_, imp);
    def.fighter.set_world_pos(new_x, def.fighter.world_y());

    // [Phase A3] SFX: a landed hit (the game plays the impact sample —
    // JS ca.Cgb's `ta.ak` after the strike lands).
    sf2::audio::AudioEngine::instance().play("hit");

    // [fx] The hit sparks + the camera kick (presentation only — the
    // sparks' RNG is EffectSystem's private LCG, the shake's a private
    // LCG too; neither touches the fight's shared roll01, so the pose
    // dump stays byte-identical). The burst origin is the contact point
    // (CapsuleHit::point — the attacker capsule's closest point, the JS
    // `strike.n$`), fanned AWAY from the attacker's facing.
    fx_.spawn_hit_sparks(ch.point.x, ch.point.y, atk.fighter.facing());
    camera_.shake(6.0f);
    std::fprintf(stdout, "[fx] sparks at %.0f,%.0f\n", ch.point.x, ch.point.y);
    std::fflush(stdout);

    ++atk.hits_landed;
    ++def.hits_taken;
    // Prize stats (JS `v.kD`/`bzb` factors): the attacker's consecutive run
    // grows, the target's resets; shocks dealt accumulate; the battle's
    // first striker is latched once.
    ++atk.combo_run;
    if (atk.combo_run > atk.max_combo) atk.max_combo = atk.combo_run;
    def.combo_run = 0;
    if (rec.shock) ++atk.shocks_dealt;
    if (!battle_first_hit_) {
        battle_first_hit_ = true;
        battle_first_by_player_ = atk.is_player;
    }
    (void)move;
}

// JS `v.kD`/`bzb` prize factors (FLOW_STATIC section 4.3;
// internal_settings `<RewardsPrize>` values verified 2026-09-04).
FightController::BattlePrize FightController::prize() const {
    BattlePrize p;
    p.perfect = player_.hits_taken == 0;
    p.first_strike = battle_first_hit_ && battle_first_by_player_;
    p.max_combo = player_.max_combo;
    p.shocks = player_.shocks_dealt;
    p.style_value = 0;  // style untracked -> Turtle 0 (OPEN)
    p.coins_bonus = prize_coins_bonus(p.perfect, p.first_strike, p.max_combo,
                                      p.shocks, p.style_value);
    p.gems_bonus = 0;  // `hj.Uo`: no evidenced fight source
    return p;
}

// The per-fighter update: the AI (or input) picks a move, the fighter
// executes it, the physics body is rebuilt. Mirrors the JS `wd.ia` +
// `de.ia` path (see core/scene/README.md).
void FightController::update_fighter(FightFighter& me, FightFighter& foe, float dt) {
    me.fighter.set_enemy_x(foe.fighter.world_x());
    // JS `wd.x3` -> `Fu.hob()` (dW=null): every new move start resets the
    // Cl one-shot, so a repeat swing of the same move re-tests instead of
    // being skipped forever by the (move, interval) key.
    {
        const void* cur = static_cast<const void*>(me.fighter.current_move());
        auto it = cl_move_.find(me.name);
        if (it == cl_move_.end() || it->second != cur) {
            cl_move_[me.name] = cur;
            cl_last_.erase(me.name);
        }
    }
    // [FIX Phase 4b — fighters stay in the arena] The root-motion walk
    // (Fighter::advance) moves world_x freely; clamp it to the arena walls
    // (the params walls at ±(Width/2 - Wall)) so a fighter can't walk out of
    // the dojo into the void (the enemy AI previously wandered to x=1244,
    // past the right wall at 900, and stood on the black background).
    me.fighter.clamp_x(wall_min_, wall_max_);

    // [FIX Phase 4b — manual control] The PLAYER's key input FIRST: when
    // the fighter is a manual (non-AI, non-auto-attack) fighter, the
    // buffered keys (Fighter::input via the fight screen's on_key) are
    // consumed here by the move selection (JS: the KeyPressed event handler
    // calls `wd.Lea`/`try_select_move` when a key is pressed). This must
    // run BEFORE the stance-idle auto-play below, so a key press interrupts
    // the idle (otherwise the idle would re-start a clip every frame and
    // the input could never win — "no input").
    if (me.ai == nullptr && !auto_attack_ && phase_ == fight_phase::fight) {
        sf2::scene::FightContext ctx;
        ctx.roll01 = roll01_;  // shared fight stream (`Da.pg` analog)
        ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
        ctx.anims_me = {};
        ctx.anims_enemy = {foe.fighter.current_move() ? foe.fighter.current_move()->name : ""};
        ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
        ctx.dist_3d = std::fabs(ctx.dist_x);
        ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
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
    if (me.fighter.current_move() == nullptr) {
        const bool intro = phase_ == fight_phase::start_stance;
        // The mirror variant is picked from the direction to the enemy
        // (JS `wd.NS` L506: facing = sign(enemyX - myX); the move's
        // MirrorNode maps it to the -Left/-Right variant). The FIRST
        // auto-play runs before any move has set facing_, so derive it
        // from the raw positions instead of the (defaulted) facing_.
        const bool face_left =
            (foe.fighter.world_x() - me.fighter.world_x()) < 0.0f;
        // [FIX idle-clip — surgical] Phase 2 (Fight) idle must use the
        // NON-moving clip (fists1_stance_idle, 38f, delta 0) — the oracle's
        // FistsStartStanceIdle-Left. The old code picked Left/Right by
        // facing, so a right-facing fighter used FistsStartStanceIdle-Right
        // (fists2_stance_idle, 101f) or, during the early re-trigger at the
        // end of phase 1, stance_2 (52f, delta -148.6) which slid 853 units.
        // Mirror is via fx (facing), not a distinct clip.
        const std::string idle_name =
            intro ? std::string("FistsStartStance-") + (face_left ? "Left" : "Right")
                  : std::string("FistsStartStanceIdle-Left");
        const auto idle_it = moves_->find(idle_name);
        if (idle_it != moves_->end()) {
            sf2::scene::FightContext ctx;
        ctx.roll01 = roll01_;  // shared fight stream (`Da.pg` analog)
            ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
            ctx.anims_me = {idle_name};
            ctx.anims_enemy = {foe.fighter.current_move() ? foe.fighter.current_move()->name
                                                          : idle_name};
            ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
            ctx.dist_3d = std::fabs(ctx.dist_x);
            ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
            me.fighter.ai_start_move(idle_it->second, ctx);
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
            // latch. Fling/`Wsb`/drop-event are presentation (OPEN).
            me.weapon = "Fists";
            me.shock.shocked_vc = true;
            me.params.attributes["WeaponDamage"] = 0.0f;
            std::fprintf(stdout, "[fight] F%d %s WQB pickup -> Fists\n",
                         frame_, me.name.c_str());
            std::fflush(stdout);
        }
    }

    me.fighter.advance(dt);
    me.last_move = me.fighter.current_move() ? me.fighter.current_move()->name : "";

    // [Phase A3] SFX: when a movement move STARTS (the fighter transitions
    // into a new move), play the jump/step sample — the game's movement
    // whooshes (f_pl_jump* for jumping moves, swish* for steps/dashes).
    // A move "starts" when its name differs from the fighter's previous
    // move (the idle loops keep the same name, so they don't re-trigger).
    {
        static std::map<std::string, std::string> s_prev_move;
        const std::string& prev = s_prev_move[me.name];
        const std::string& cur = me.last_move;
        if (cur != prev && !cur.empty()) {
            // Jumping: JumpUp / FrontJumpKick / ShortJumpKick /
            // DoubleJumpKick / ReverseJumpKick / WallJump* / BackFlip.
            // Stepping: *StepForward / *StepBack / DoubleStepForward /
            // ForwardRoll / BackRoll / DashBackwards / WallDashForward*.
            if (cur.find("Jump") != std::string::npos || cur == "BackFlip") {
                sf2::audio::AudioEngine::instance().play("jump");
            } else if (cur.find("Step") != std::string::npos ||
                       cur.find("Roll") != std::string::npos ||
                       cur.find("Dash") != std::string::npos) {
                sf2::audio::AudioEngine::instance().play("step");
            }
        }
        s_prev_move[me.name] = cur;
    }

    // The demo's simple auto-attack (the game's FightAuto): when idle,
    // step toward the enemy when beyond reach, punch when in reach.
    if (auto_attack_ && me.is_player && me.fighter.current_move() == nullptr) {
        const float dist = std::fabs(foe.fighter.world_x() - me.fighter.world_x());
        const std::string move_name =
            dist > 160.0f ? "StepForward" : "HighPunch";
        const auto it = moves_->find(move_name);
        if (it != moves_->end()) {
            sf2::scene::FightContext ctx;
        ctx.roll01 = roll01_;  // shared fight stream (`Da.pg` analog)
            ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
            ctx.anims_me = {me.fighter.current_move() ? me.fighter.current_move()->name : ""};
            ctx.anims_enemy = {foe.fighter.current_move() ? foe.fighter.current_move()->name : ""};
            ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
            ctx.dist_3d = std::fabs(ctx.dist_x);
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
        st.enemy_max_part_frames = foe.fighter.move_frame();
        st.ranged = -1;
        st.magic_bullets = 0;
        st.enemy_part_frames.push_back(foe.fighter.move_frame());
        st.fight_frame = frame_;
        st.roll01 = roll01_;

        const std::string decision = me.ai->update(st);
        me.last_decision = decision;
        me.last_ai_stage = me.ai->last_stage();
        if (!decision.empty()) {
            const sf2::scene::MoveDef* chosen = nullptr;
            for (const auto& kv : *moves_) {
                if (kv.second.name == decision ||
                    kv.second.template_tags.count(decision) > 0) {
                    chosen = &kv.second;
                    break;
                }
            }
            if (chosen == nullptr && decision == "ShortAttack") {
                for (const auto& kv : *moves_) {
                    if (kv.second.template_tags.count("Punch") == 0) continue;
                    sf2::scene::FightContext ctx;
        ctx.roll01 = roll01_;  // shared fight stream (`Da.pg` analog)
                    ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
                    ctx.anims_me = {me.fighter.current_move()
                                        ? me.fighter.current_move()->name
                                        : ""};
                    ctx.anims_enemy = {foe.fighter.current_move()
                                           ? foe.fighter.current_move()->name
                                           : ""};
                    ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
                    ctx.dist_3d = std::fabs(ctx.dist_x);
                    ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
                    if (sf2::scene::eval_move_conditions(kv.second.tactics, ctx)) {
                        chosen = &kv.second;
                        break;
                    }
                }
            }
            if (chosen != nullptr) {
                sf2::scene::FightContext ctx;
        ctx.roll01 = roll01_;  // shared fight stream (`Da.pg` analog)
                ctx.stage = static_cast<sf2::scene::round_stage>(phase_);
                ctx.anims_me = {me.fighter.current_move() ? me.fighter.current_move()->name : ""};
                ctx.anims_enemy = {foe.fighter.current_move()
                                       ? foe.fighter.current_move()->name
                                       : ""};
                ctx.dist_x = foe.fighter.world_x() - me.fighter.world_x();
                ctx.dist_3d = std::fabs(ctx.dist_x);
                ctx.health_ratio = me.max_hp > 0.0f ? me.hp / me.max_hp : 0.0f;
                if (me.fighter.ai_start_move(*chosen, ctx)) {
                    ++me.moves_started;
                }
            }
        }
    }

    rebuild_body(me);
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
        case banner_kind::ko:     return "K.O.";
        case banner_kind::victory: return "VICTORY";
        case banner_kind::defeat: return "DEFEAT";
        default:                  return "";
    }
}

// The banner's progress through its hold, clamped to 0..1 (for the
// fade/scale-in; the victory/defeat banner holds at 1.0 forever).
float FightController::banner_progress() const {
    if (banner_len_ <= 0) return 1.0f;
    const float p = static_cast<float>(frame_ - banner_start_) /
                    static_cast<float>(banner_len_);
    return std::max(0.0f, std::min(1.0f, p));
}

// JS `ca.Ea` (L385) + `ia` (L388): the per-frame fight update.
void FightController::update(float dt) {
    ++frame_;
    // [fx] The particle pool + the shake decay (presentation only — runs
    // even after the battle ends so the KO burst finishes and the camera
    // kick settles back to 0; neither touches the simulation).
    fx_.update();
    camera_.shake_x_ *= 0.85f;
    camera_.shake_y_ *= 0.85f;
    if (battle_over_) return;

    // The K.O. slow-mo beat (JS: the KO freeze): the first 30 frames of
    // the K.O. banner run the simulation at half speed. Presentation-
    // driven, but it only scales `dt` — by the time the ko banner is up
    // the fight is already in end_stance, so the pose stream is
    // unaffected.
    if (cur_banner_ == banner_kind::ko && frame_ - banner_start_ < 30) {
        dt *= 0.5f;
    }

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
            if (start_stance_frames_ >= 133) {
                enter_fight();
            }
            break;
        }
        case fight_phase::fight: {
            // The banner machine (presentation only): ROUND N (60f) ->
            // FIGHT! (40f) -> none. The round banner is raised in
            // round_start(); the FIGHT! banner fires when it expires.
            if (cur_banner_ == banner_kind::round &&
                frame_ - banner_start_ >= banner_len_) {
                cur_banner_ = banner_kind::fight;
                banner_start_ = frame_;
                banner_len_ = 40;
                std::fprintf(stdout, "[fight] banner: FIGHT!\n");
                std::fflush(stdout);
            } else if (cur_banner_ == banner_kind::fight &&
                       frame_ - banner_start_ >= banner_len_) {
                cur_banner_ = banner_kind::none;
                banner_len_ = 0;
            }
            // The round timer (JS `Sf.iPa` L2036 — `--xU`, `NF = xU/60|0`;
            // C++ `/` truncates toward zero = JS `|0`). Ticks while `Vt`.
            if (round_.running) {
                --round_.time_xu;
                round_.time_nf = round_.time_xu / 60;
            }
            // Both fighters act (JS `ca.Hnb` -> each `wd.ia`).
            update_fighter(player_, enemy_, dt);
            update_fighter(enemy_, player_, dt);

            // Hit detection + damage (JS `ca.Enb` + `Cgb`).
            const sf2::scene::MoveDef* p_move = player_.fighter.current_move();
            const sf2::scene::MoveDef* e_move = enemy_.fighter.current_move();
            const sf2::scene::Interval* hit_iv = nullptr;
            sf2::scene::HitCapsule hit_cap;
            sf2::scene::CapsuleHit ch;
            bool hit_player = false, hit_enemy = false;
            if (p_move != nullptr &&
                hza_pick(enemy_, *p_move, player_.fighter.move_frame()) != nullptr) {
                hit_enemy = hit_test(player_, enemy_, *p_move, player_.fighter.move_frame(),
                                     hit_cap, ch, hit_iv);
            }
            if (e_move != nullptr && !hit_enemy &&
                hza_pick(player_, *e_move, enemy_.fighter.move_frame()) != nullptr) {
                hit_player = hit_test(enemy_, player_, *e_move, enemy_.fighter.move_frame(),
                                      hit_cap, ch, hit_iv);
            }
            if (hit_iv != nullptr) {
                if (hit_enemy) {
                    apply_hit(player_, enemy_, *p_move, *hit_iv, hit_cap, ch, frame_);
                } else {
                    apply_hit(enemy_, player_, *e_move, *hit_iv, hit_cap, ch, frame_);
                }
            }

            // The round-end check (JS `Onb`).
            check_round_end();
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

    // The camera follows the fight (JS ql.Ea -> tyb/dZa/c3a + ma.Sya).
    camera_.framing(player_.fighter.world_x(), player_.fighter.world_y(),
                    enemy_.fighter.world_x(), enemy_.fighter.world_y(), 1280.0f, 720.0f);

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

// Fight controller implementation (Phase 3.5).
// Ported from the game's `ca` class (sf2.502f0946.js L379-433). See
// core/scene/README.md "Fight controller (Phase 3.5)" for the JS study.

#include "scene/fight.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf2::scene {

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
    // The JS Sf.iPa (L2035) decrements the HUD countdown 1/sec while
    // `round.Vt` (running); the demo reads the countdown directly from the
    // fight state (gma - round.time), so no per-frame HUD state is needed.
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

    // Camera framing (JS ma.Sya L1833): center the fight.
    camera_.arena_w = wall_max_ - wall_min_;
    camera_.framing(player_.fighter.world_x(), enemy_.fighter.world_x(), 1280.0f, 720.0f);

    // The fight start (JS ggb L383): round 0 -> the first round init.
    round_.number = 0;
    round_init();
    enter_start_stance();
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
    camera_.arena_w = wall_max_ - wall_min_;
}

// JS `xF` (L388): set the fight phase and sync the fighters' `Je` stance
// (the move conditions' RoundStage reads it).
void FightController::set_phase(fight_phase p) {
    phase_ = p;
    player_.fighter.set_enemy_x(enemy_.fighter.world_x());
    enemy_.fighter.set_enemy_x(player_.fighter.world_x());
}

// JS `tx` (L407): round init — the timer is the round length, Vt=false.
void FightController::round_init() {
    round_.running = false;
    round_.length = battle_.rounds;     // Da.pT (Rounds)
    round_.time = 0.0f;
    round_.gma = battle_.round_time;    // Da.R4 (RoundTime)
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
    const bool timeout = battle_.timeout_rule &&
                         round_.time >= static_cast<float>(round_.gma);
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

    enter_end_stance();
    history_.push_back(oc);

    // Battle end: the winner reached `round.eL` (Rounds) — JS Onb
    // `a = wo.nB.ng >= round.eL` -> `a ? bea(nB)`.
    const bool battle_end = w.rounds_won >= round_.length;    if (battle_end) {
        end_battle(w);
    } else {
        between_rounds_recover();
        round_start();
    }
}

// JS `bea` (L413): the battle end — the winner is fixed, the fight stops.
void FightController::end_battle(const FightFighter& winner) {
    battle_over_ = true;
    winner_ = &winner;
    round_.running = false;
    round_live_ = false;
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
    if (phase_ != fight_phase::fight) return;  // JS: input only in phase 2
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
    for (const sf2::scene::Interval& iv : move.intervals) {
        if (iv.type != 4) continue;  // Attack
        const int s = std::max(iv.start, move.first_frame);
        const int e = iv.end;
        if (!(s <= frame && frame <= e)) continue;
        for (const std::string& edge : iv.attacking_parts) {
            const sf2::scene::HitCapsule* atk_cap = atk.body.by_name(edge);
            if (atk_cap == nullptr) continue;
            for (const auto& tgt : def.body.capsules) {
                if (!tgt.collidable) continue;
                if (sf2::scene::capsule_capsule_overlap(*atk_cap, tgt, ch)) {
                    hit_cap = tgt;
                    hit_interval = &iv;
                    return true;
                }
            }
        }
    }
    return false;
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
    const bool blocked = false;
    const bool critical = false;
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
    rec.frame = frame;
    sf2::scene::apply_damage(rec, def.hp, false);
    def.hp = rec.hp_after;

    // Knockback (JS `Bl.strike` + bounds).
    sf2::scene::Vec3 impulse{iv.impulse_x, iv.impulse_y, iv.impulse_z};
    impulse.x *= static_cast<float>(atk.fighter.facing());
    sf2::scene::ImpulseResult imp;
    const float new_x =
        sf2::scene::apply_impulse(hit_cap, ch, impulse, def.fighter.world_x(),
                                  wall_min_, wall_max_, imp);
    def.fighter.set_world_pos(new_x, def.fighter.world_y());

    ++atk.hits_landed;
    ++def.hits_taken;
    (void)move;
}

// The per-fighter update: the AI (or input) picks a move, the fighter
// executes it, the physics body is rebuilt. Mirrors the JS `wd.ia` +
// `de.ia` path (see core/scene/README.md).
void FightController::update_fighter(FightFighter& me, FightFighter& foe, float dt) {
    me.fighter.set_enemy_x(foe.fighter.world_x());
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

    me.fighter.advance(dt);
    me.last_move = me.fighter.current_move() ? me.fighter.current_move()->name : "";

    // The demo's simple auto-attack (the game's FightAuto): when idle,
    // step toward the enemy when beyond reach, punch when in reach.
    if (auto_attack_ && me.is_player && me.fighter.current_move() == nullptr) {
        const float dist = std::fabs(foe.fighter.world_x() - me.fighter.world_x());
        const std::string move_name =
            dist > 160.0f ? "StepForward" : "HighPunch";
        const auto it = moves_->find(move_name);
        if (it != moves_->end()) {
            sf2::scene::FightContext ctx;
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

    // The AI decision (JS `de.ia`).
    if (me.ai != nullptr) {
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
    // JS Sf.iPa (L2035): the HUD countdown = round.gma - round.time.
    const int t = round_.gma - static_cast<int>(std::ceil(round_.time));
    return std::max(0, t);
}

// JS `ca.Ea` (L385) + `ia` (L388): the per-frame fight update.
void FightController::update(float dt) {
    ++frame_;
    if (battle_over_) return;

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
            // The round timer (JS `round.time` — the port advances it
            // during phase 2; the HUD counts down).
            if (round_.running) {
                round_.time += dt;
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
            if (p_move != nullptr) {
                hit_enemy = hit_test(player_, enemy_, *p_move, player_.fighter.move_frame(),
                                     hit_cap, ch, hit_iv);
            }
            if (e_move != nullptr && !hit_enemy) {
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

    // The camera follows the fight (JS ma.Sya).
    camera_.framing(player_.fighter.world_x(), enemy_.fighter.world_x(), 1280.0f, 720.0f);

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
//  "fighters":[Me, Enemy]} — Me first, then the enemy. `timer` = the elapsed
// round seconds (int(round_.time)) — the JS `frameJson` basis (round.time),
// NOT the HUD countdown (gma - time). Read-only over the simulation:
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
    std::fprintf(pose_dump_file_,
                 "{\"t\":\"frame\",\"f\":%d,\"phase\":%d,\"round\":%d,\"timer\":%d,"
                 "\"cam\":{\"cx\":%.6f,\"cy\":%.6f,\"zoom\":%.6f},\"fighters\":[",
                 frame_, phase(), round_.number, static_cast<int>(round_.time),
                 camera_.center_x, camera_.center_y, camera_.zoom);
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

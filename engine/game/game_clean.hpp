#pragma once

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>

#include "engine/platform/platform.hpp"
#include "engine/platform/glfw_platform.hpp"
#include "engine/runtime/loop.hpp"
#include "engine/renderer/renderer.hpp"
#include "engine/renderer/irenderer.hpp"
#include "engine/reverse/plist_atlas.hpp"
#include "engine/reverse/bitmap_font.hpp"
#include "engine/reverse/dz_reader.hpp"
#include "engine/scene/scene_system.hpp"
#include "engine/scene/scenes.hpp"
#include "engine/renderer/stb_image.h"
#include "engine/format/xml_doc.hpp"
#include "engine/format/stage_parser.hpp"
#include "engine/format/list_parser.hpp"
#include "engine/audio/audio.hpp"
#include "save.hpp"
#include "boot_configs.hpp"
#include "ui_scale.hpp"
#include "player.hpp"
#include "inventory.hpp"
#include "shop.hpp"
#include "location_manager.hpp"
#include "asset_manager.hpp"
#include "tactic_settings.hpp"
#include "tactic_tables.hpp"
#include "tactic_pipeline.hpp"
#include "animation_player.hpp"
#include "combat.hpp"
#include "condition_system.hpp"
#include "input_handler.hpp"
#include "quest_engine.hpp"
#include "quest_loader.hpp"

// Import commonly-used namespaces at file scope
// (helpers.cpp also uses these at file scope, so they must be here)
namespace plat = resf2::platform;
namespace rt = resf2::runtime;
namespace ren = resf2::renderer;
namespace fmt = resf2::format;
namespace aud = resf2::audio;
namespace plist = resf2::reverse::plist;
namespace font = resf2::reverse::font;
namespace scene = resf2::scene;
namespace save = resf2::save;
namespace player = resf2::player;
namespace inventory = resf2::inventory;
namespace shop = resf2::shop;
namespace quest = resf2::quest;

// ---------- Forward declarations for helper functions ----------
// These are defined in helpers.cpp and used by inline Game methods.
// They live at file scope (not in any namespace) for backward compatibility
// with the monolithic main.cpp they were extracted from.

std::vector<std::byte> read_file(const std::string& path);
std::string read_text(const std::string& path);
float tof(const std::string& s, float def = 0.0f);
int toi(const std::string& s, int def = 0);
std::filesystem::path get_exe_dir();
std::vector<std::filesystem::path> model_paths(const std::string& asset_root, const char* filename);

// Debug log globals — defined in helpers.cpp
extern FILE* g_debug_log;
extern bool g_debug_log_enabled;
void debug_log_init(const std::string& path);
void debug_log(const char* fmt, ...);
void debug_log_close();

// All type definitions moved to types.hpp
#include "types.hpp"

namespace resf2::game {

// ---------- Game ----------
//
// The Game class is the SceneHost — it owns the SceneManager and implements
// the scene::SceneHost interface. Individual scenes (MainMenu, Battle, Map,
// etc.) call back into Game via the host interface to load assets, render
// the dojo, save progress, etc.

class Game final : public rt::IGame, public scene::SceneHost {
public:
    explicit Game(std::string asset_root, bool replay_mode = false, bool dump_state = false);
    ~Game() override;

    // Discover all location names by scanning the assets/locations/ directory.
    void discover_locations() {
        locations_.discover_locations(asset_root_);
    }

    const std::vector<std::string>& location_names() const { return locations_.location_names(); }
    size_t location_count() const { return locations_.location_names().size(); }

    // Start with the world-geometry overlay on (also toggled at runtime with F1).
    void set_debug_world(bool on) { debug_world_ = on; }
    // --scene <name>: open a screen directly instead of walking the Boot ->
    // Loading -> MainMenu flow. Used to capture a screen for comparison
    // against the reference screenshots.
    void set_start_scene(const std::string& s) { start_scene_ = s; }
    // Exposed so a scene can honour the ":N" argument of --scene.
    int start_scene_arg() const override { return start_scene_arg_; }
    // See hermetic_run_ below. main.cpp turns this on for --input-script runs.
    void set_hermetic_run(bool on) { hermetic_run_ = on; }
    // [Wave 10A defect 3] E2E hooks (main.cpp): force a round clock and/or
    // start the Sensei tutorial flow in a scripted run.
    void set_round_time_override(int s) { round_time_override_s_ = s; }
    void set_tutorial_start(bool on) { tutorial_start_flag_ = on; }
    // [Wave 10A defect 6] E2E hook: --equip-magic forces a magic item into
    // the inventory and equips it at boot, so a scripted run can exercise
    // the fight HUD's magic button.
    void set_equip_magic(const std::string& id) { equip_magic_hook_ = id; }
    void set_equip_weapon(const std::string& id) { equip_weapon_hook_ = id; }
    // [Wave 11A M4] E2E hook: --crit-attr <chance> <damage> overrides the
    // player's crit attributes (mirrors --equip-magic).
    void set_crit_attr_hook(int chance, int damage) {
        crit_attr_hook_chance_ = chance;
        crit_attr_hook_damage_ = damage;
    }

    void set_start_location(const std::string& name) {
        if (!name.empty()) {
            current_location_name_ = name;
            locations_.set_current_location_name(name);
            std::printf("[GAME] Start location set to: %s\n", name.c_str());
        }
    }

    // Inject a custom renderer (e.g., software renderer for headless testing).
    // If not called, Game creates a GL renderer in on_init.
    void set_renderer(std::unique_ptr<ren::IRenderer> renderer) {
        renderer_ = std::move(renderer);
    }

    // ---------- Visual audit (tests/tool_visual_audit.cpp) ----------
    // Read-only dumps of the geometric quantities the on-screen composition
    // depends on. These exist because the camera framing and the fighter's
    // model->world mapping were tuned by eye (see the [HEURISTIC-TODO] notes in
    // update_camera); printing the numbers turns "looks wrong" into a
    // measurable divergence from the original's reference screenshot.

    void audit_dump_location() const {
        if (!location_) { std::printf("  (no location loaded)\n"); return; }
        std::printf("  name          %s\n", current_location_name_.c_str());
        std::printf("  Width         %.0f  (half %.0f)\n",
                    location_->width, location_->width * 0.5f);
        std::printf("  Height        %.0f  (half %.0f)\n",
                    location_->height, location_->height * 0.5f);
        std::printf("  Wall          %.0f\n", location_->wall);
        std::printf("  Floor         %.0f\n", location_->floor);
        std::printf("  ModelsViewer  player=(%.0f, %.0f)  enemy=(%.0f, %.0f)\n",
                    location_->player_x, location_->player_y,
                    location_->enemy_x, location_->enemy_y);
        std::printf("  layers        %zu\n", location_->layers.size());
        for (const auto& l : location_->layers) {
            std::printf("    type=%d factor=%.2f atlas=%-14s images=%zu\n",
                        l.type, l.factor, l.atlas_name.c_str(), l.images.size());
            for (const auto& im : l.images) {
                std::printf("        %-16s X=%7.1f Y=%7.1f W=%6.1f H=%6.1f\n",
                            im.class_name.c_str(), im.x, im.y, im.w, im.h);
            }
        }
    }

    void audit_dump_camera() const {
        if (!platform_ || !location_) { std::printf("  (not ready)\n"); return; }
        const float vw = static_cast<float>(platform_->window_width());
        const float vh = static_cast<float>(platform_->window_height());
        std::printf("  viewport      %.0f x %.0f\n", vw, vh);
        std::printf("  zoom          %.4f  (vh / Height)\n", zoom_);
        std::printf("  visible world %.1f x %.1f units\n",
                    zoom_ > 0 ? vw / zoom_ : 0.0f, zoom_ > 0 ? vh / zoom_ : 0.0f);
        std::printf("  camera        x=%.1f y=%.1f\n", cam_x_, cam_y_);
        const float floor_a = -location_->height * 0.5f + location_->floor;
        const float floor_screen = vh * 0.5f - (floor_a - cam_y_) * zoom_;
        std::printf("  floor world y %.1f  (-Height/2 + Floor)\n", floor_a);
        std::printf("  floor screen  y=%.1f  (%.1f%% of frame)\n",
                    floor_screen, vh > 0 ? 100.0f * floor_screen / vh : 0.0f);
        std::printf("  ORIGINAL ref  floor at ~87.5%% of frame height\n");
    }

    void audit_dump_fighter() const {
        if (!platform_) { std::printf("  (not ready)\n"); return; }
        const float vw = static_cast<float>(platform_->window_width());
        std::printf("  player world  x=%.1f y=%.1f  facing=%s\n",
                    player_pos_x_, player_pos_y_, facing_right_ ? "right" : "left");
        std::printf("  enemy world   x=%.1f y=%.1f\n", enemy_pos_x_, enemy_pos_y_);
        std::printf("  y_adjust      %.2f  (animation-driven pivot correction)\n",
                    y_adjust_smoothed_);
        auto pit = assets_->skeleton_nodes().find("NPivot");
        const float pivot_ly = pit != assets_->skeleton_nodes().end()
                                   ? pit->second.y : stance_npivot_y_;
        std::printf("  NPivot local  y=%.2f\n", pivot_ly);
        std::printf("  current anim  '%s'  move='%s'\n",
                    anim_player_.current_anim().c_str(), current_move_.c_str());
        std::printf("  skeleton      %zu nodes, %zu edges\n",
                    assets_->skeleton_nodes().size(),
                    assets_->skeleton_edges().size());
        std::printf("  anim nodes    %zu posed this frame\n", anim_node_pos_.size());
        // If nothing is posed the fighter renders as a static rest-pose blob,
        // which is exactly the visual defect being chased. Walk the same
        // preconditions AnimationPlayer::update() checks, so the break is
        // identified instead of guessed.
        if (anim_node_pos_.empty()) {
            const auto& order = assets_->ordered_node_names();
            const auto& anims = assets_->animations();
            std::printf("  [!] nothing posed - diagnosing:\n");
            std::printf("      ordered_node_names  %zu%s\n", order.size(),
                        order.empty() ? "   <-- EMPTY, update() bails" : "");
            std::printf("      animations loaded   %zu\n", anims.size());
            bool has_npivot = false;
            for (const auto& n : order) if (n == "NPivot") { has_npivot = true; break; }
            std::printf("      NPivot in order     %s%s\n",
                        has_npivot ? "yes" : "NO",
                        has_npivot ? "" : "   <-- update() bails without it");
            auto it = anims.find(anim_player_.current_anim());
            if (it == anims.end()) {
                std::printf("      current anim '%s' NOT in the animation map\n",
                            anim_player_.current_anim().c_str());
            } else {
                std::printf("      current anim frames %d\n", it->second.frame_count);
                if (it->second.frame_count == 0)
                    std::printf("      <-- zero frames, update() bails\n");
            }
        }
        if (zoom_ > 0.0f && vw > 0.0f) {
            const float px = vw * 0.5f + (player_pos_x_ - cam_x_) * zoom_;
            const float ex = vw * 0.5f + (enemy_pos_x_ - cam_x_) * zoom_;
            std::printf("  screen x      player %.1f (%.1f%%)  enemy %.1f (%.1f%%)\n",
                        px, 100.0f * px / vw, ex, 100.0f * ex / vw);
            std::printf("  ORIGINAL ref  player ~36%%  enemy ~64%%\n");
        }
    }

    void audit_dump_bag() const {
        if (!assets_->bag_model()) { std::printf("  (no bag model)\n"); return; }
        const auto& m = *assets_->bag_model();
        std::printf("  model         %zu nodes, %zu edges, %zu capsules\n",
                    m.nodes.size(), m.edges.size(), m.capsules.size());
        std::printf("  verlet        %s (%zu nodes)\n",
                    bag_verlet_init_ ? "active" : "inactive", bag_verlet_.size());
        for (const auto& c : m.capsules) {
            std::printf("      edge=%-12s r1=%.1f r2=%.1f m1=%.1f m2=%.1f\n",
                        c.edge_name.c_str(), c.radius1, c.radius2,
                        c.margin1, c.margin2);
        }
        if (bag_verlet_init_ && !bag_verlet_.empty()) {
            float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
            bool first = true;
            for (const auto& kv : bag_verlet_) {
                const auto& v = kv.second;
                if (first) { x0 = x1 = v.x; y0 = y1 = v.y; first = false; }
                x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
                y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
            }
            std::printf("  world box     x=%.1f..%.1f  y=%.1f..%.1f\n",
                        x0, x1, y0, y1);

            // The bag body is Edge16/Edge17 (radius 25) spanning
            // NBottom -> NPivot -> NNeck. If the simulated node spacing has
            // collapsed, those capsules degenerate and the bag renders as
            // separate blobs instead of one cylinder -- print rest length vs
            // simulated length so that is visible rather than inferred.
            std::printf("  body chain (rest vs simulated):\n");
            const char* chain[] = {"NBottom", "NPivot", "NNeck", "Node12"};
            for (int i = 0; i + 1 < 4; ++i) {
                auto rn1 = m.nodes.find(chain[i]);
                auto rn2 = m.nodes.find(chain[i + 1]);
                auto vn1 = bag_verlet_.find(chain[i]);
                auto vn2 = bag_verlet_.find(chain[i + 1]);
                if (rn1 == m.nodes.end() || rn2 == m.nodes.end()) {
                    std::printf("      %-8s -> %-8s   MODEL NODE MISSING\n",
                                chain[i], chain[i + 1]);
                    continue;
                }
                const float rdx = rn2->second.x - rn1->second.x;
                const float rdy = rn2->second.y - rn1->second.y;
                const float rest = std::sqrt(rdx * rdx + rdy * rdy);
                if (vn1 == bag_verlet_.end() || vn2 == bag_verlet_.end()) {
                    std::printf("      %-8s -> %-8s   rest=%.1f  NOT SIMULATED\n",
                                chain[i], chain[i + 1], rest);
                    continue;
                }
                const float sdx = vn2->second.x - vn1->second.x;
                const float sdy = vn2->second.y - vn1->second.y;
                const float sim = std::sqrt(sdx * sdx + sdy * sdy);
                std::printf("      %-8s -> %-8s   rest=%6.1f  sim=%6.1f%s\n",
                            chain[i], chain[i + 1], rest, sim,
                            (rest > 1.0f && sim < rest * 0.5f) ? "   <-- COLLAPSED" : "");
            }
        }
    }

    void on_init(plat::Platform& platform) override;

void on_update(plat::Platform& platform, uint32_t dt) override;


void on_render(plat::Platform& platform) override;


void on_shutdown(plat::Platform&) override;


    bool quit_requested() const noexcept { return quit_requested_; }

    // ---------- scene::SceneHost implementation ----------
    //
    // These methods are called by the scenes (MainMenu, Battle, etc.) via
    // the SceneHost interface to interact with the game state.

void request_scene_transition(scene::SceneId to) override;


void host_load_location() override;


void host_reset_menu_state() override;


    void host_toggle_menu_overlay() override;
    void host_close_menu_overlay() override;


void host_render_menu_overlay() override;


void host_load_battle_location(const std::string& location) override;


bool host_location_loaded() const noexcept override;


bool host_save_progress() override;


bool host_load_progress() override;


void host_set_dialogue(std::vector<std::pair<std::string, std::string>> lines) override;


const std::vector<std::pair<std::string, std::string>>& host_get_dialogue() const override;


// [ORIGINAL] QuestActionDialog choices at +0xa4..+0xb0 (FUN_101c7d20).
std::vector<std::string> host_get_dialogue_choices() const override;
void host_set_dialogue_choices(std::vector<std::string> choices) override;


// [Wave 9B] Story-dialogue queue (quests.xml <Dialog> sets): queues lines
// plus the scene to return to after the dialogue finishes. The Map shows
// the queued dialogue on its next entry (host_consume_story_dialogue).
void host_queue_story_dialogue(std::vector<std::pair<std::string, std::string>> lines,
                               scene::SceneId return_to) override;
bool host_consume_story_dialogue() override;
scene::SceneId host_get_dialogue_return() const override;

// [Wave 9B] Shop probes (re-soak-5): the centre list's visible rows and the
// selected item, as the scene renders them.
std::vector<std::string> host_shop_visible_rows();
std::string host_shop_selected_item();

// [Wave 9B] Story probe: is a story dialogue queued right now?
bool host_has_pending_story_dialogue() const { return story_dialogue_pending_; }
// [Wave 9B] Quest-engine probe: was a battle shown by a quest action?
bool host_quest_battle_unlocked(const std::string& battle) const {
    return quest_engine_.is_battle_unlocked(battle);
}
// [Wave 9B] S5: FIRST_FIGHT won -> tutorial COMPLETE + Sensei shop dialogue.
void host_finish_tutorial_fight() override;


void host_set_current_level(std::string level_id) override;


void host_add_completed_level(const std::string& level);


    bool host_is_level_completed(const std::string& level) const;

    // [ORIGINAL] Zone/battle lock state from usersDefault.xml
    bool host_is_zone_unlocked(const std::string& zone) const override;
    bool host_is_battle_locked(const std::string& zone, const std::string& battle) const override;
    std::string host_get_tutorial_state() const override;

    // Tutorial: check if the initial Sensei tutorial should fire.
    // Called after loading progress. [ORIGINAL] Driven by Tutorial attribute.
    void check_tutorial();
    // [Q3] Queue the Kenji (Dojo_Disciple) fight after the bag phase so the
    // training dialog hands over to Battle instead of the dojo.
    void queue_tutorial_battle();


std::string host_get_battle_result() const override;


const resf2::format::StageData* host_get_stages() const override;


void host_set_battle_location(std::string loc) override;


std::string host_get_battle_location() const override;


void host_set_battle_result(std::string result) override;

    // Quest event trigger (called from scenes to drive quest progression)
    void host_trigger_quest_event(const std::string& event, const std::string& arg = "") override;


    // ---- Fight parameters and state (D4) ----

void host_set_battle_info(const BattleInfo& info) override;

const BattleInfo& host_get_battle_info() const override;

std::string host_round_outcome() const override;

float host_player_health_frac() const override;

float host_enemy_health_frac() const override;

    // Read-only access to the F1 COMBAT panel's last player->enemy damage
    // breakdown, for the battle-level wiring test (phase 4 step 10).
    float dbg_last_base_damage() const { return dbg_last_base_damage_; }
    float dbg_last_attr_mult() const { return dbg_last_attr_mult_; }
    float dbg_last_block_factor() const { return dbg_last_block_factor_; }
    float dbg_last_final_damage() const { return dbg_last_final_damage_; }
    const std::string& dbg_last_move_name() const { return dbg_last_move_name_; }
    // [P10] The last landed move's authored damage attribute and shift
    // (moves.xml <Damage Type=.. Shift=../>) — the wiring test's prediction
    // must apply DamageAttribute(+Shift) like the game does.
    std::string host_get_last_move_damage_attr() const {
        const auto& mv = assets_->moves();
        auto it = mv.find(dbg_last_move_name_);
        return it == mv.end() ? std::string() : it->second.damage_attr;
    }
    int host_get_last_move_damage_shift() const {
        const auto& mv = assets_->moves();
        auto it = mv.find(dbg_last_move_name_);
        return it == mv.end() ? 0 : it->second.damage_attr_shift;
    }

    // ---- D3 probes: the last enemy-AI pipeline decision ----
    // Read-only view of the F1 overlay stash + the stored decision the
    // executor consumed, for the battle-level wiring test
    // (test_enemy_ai_pipeline). Phase E deleted the legacy enemy_ai_state_
    // int probe with the FSM.
    const std::string& host_get_ai_last_pick() const { return ai_last_pick_; }
    const std::vector<std::string>& host_get_ai_last_candidates() const {
        return ai_last_candidates_;
    }
    const std::vector<float>& host_get_ai_last_weights() const {
        return ai_last_weights_;
    }
    float host_get_ai_last_distance() const { return ai_last_distance_; }

    // ---- E2 probes: direct-consumption executor (ADR-005 Phase B) ----
    // Read-only view of the stored decision the execute block switches on
    // and the enemy motion state it drives — for test_enemy_ai_pipeline's
    // direct-consumption pins (Phase E deletes host_get_enemy_ai_state
    // with the legacy FSM).
    const std::string& host_get_enemy_anim() const { return enemy_anim_; }
    bool host_get_enemy_attacking() const { return enemy_attacking_; }
    bool host_get_enemy_blocking() const { return enemy_fighter_.is_blocking; }
    float host_get_enemy_pos_x() const { return enemy_pos_x_; }
    // [Soak-fix Wave 9A] F1 test seam: the hit_blade effect count (the
    // battle path must spawn the effect at the impact point).
    std::size_t host_get_hit_spark_count() const { return hit_sparks_.size(); }
    // [Soak-fix Wave 9A] F1 test seam: real impact sounds played so far.
    int host_get_hit_sound_count() const { return hit_sound_count_; }
    const std::string& host_get_last_hit_sound() const { return last_hit_sound_; }
    // [Soak-fix Wave 9A] F1: the most recent sound any path played
    // (swish/bodyfall/wall3/armor pins).
    const std::string& host_get_last_sound() const { return last_played_sound_; }
    // [Soak-fix Wave 9A] F1f probe: swish plays counted, so the swing swish
    // stays observable even when the attack voice follows it in the same
    // frame (last-sound probes can only see one of the two).
    int host_get_swish_play_count() const { return swish_play_count_; }
    // ---- Hardcode-fidelity probes (HARDCODE_AUDIT.md HIGH items) ----
    // H07: true when the named animation is in the loaded catalog — the
    // engine must not invent names the original never shipped ("fists_idle"
    // aliased onto fists1_stance_idle).
    bool host_has_animation(const std::string& name) const {
        return assets_ && assets_->animations().count(name) > 0;
    }
    // H07/H05: the enemy idle animation resolves a REAL catalog stance idle
    // (weapon-aware; Fists -> fists1_stance_idle).
    std::string host_get_enemy_idle_anim() const { return enemy_idle_anim(); }
    // H05: the enemy attack animation resolves the weapon family's real
    // 1key attack move (swords_slash for a sword loadout, high_punch for
    // fists).
    std::string host_get_enemy_attack_anim() const { return enemy_attack_anim(); }
    // H02: the tactic->model-file fallback resolver (list.xml Model attr
    // first, static map last).
    std::string host_get_weapon_tactic_model_file(const std::string& subtype) const {
        return weapon_tactic_to_model_file(subtype);
    }
    // H09: the top-panel HUD values the engine actually renders (synced
    // from the loaded save in host_load_progress).
    int host_get_hud_level() const { return hud_level_; }
    int host_get_hud_gold() const { return hud_gold_; }
    int host_get_hud_gems() const { return hud_gems_; }
    // H10: fire a projectile through the real spawn path and read back what
    // the engine resolved from list.xml (MagicDamage / Model).
    struct HostProjectileInfo {
        float damage = 0.0f;
        float radius = 0.0f;
        std::string model_file;
    };
    HostProjectileInfo host_fire_projectile(const std::string& magic_type) {
        spawn_projectile(magic_type, player_pos_x_, player_pos_y_,
                         facing_right_, true);
        HostProjectileInfo out;
        if (!projectiles_.empty()) {
            const Projectile& p = projectiles_.back();
            out.damage = p.damage;
            out.radius = p.radius;
            out.model_file = p.model_file;
        }
        return out;
    }
    // A01: the loaded asset catalog (moves map etc.) for the fidelity tests.
    const AssetManager& host_assets() const { return *assets_; }
    // H08: does the enemy's current swing connect via the model-edge
    // collision path (mirror of the R2 hit test)?
    bool host_enemy_attack_connects() { return enemy_attack_connects(); }
    // H08: place the enemy for the collision probe (read-only getter
    // exists; tests position the pair at controlled distances).
    void host_set_enemy_pos_x(float x) { enemy_pos_x_ = x; }
    void host_set_player_pos_x(float x) { player_pos_x_ = x; }
    // H06: the enemy weapon model file resolved from the stages.xml loadout
    // (list.xml Model + ".xml"); empty = unarmed loadout.
    const std::string& host_get_enemy_weapon_file() const {
        return enemy_weapon_file_;
    }
    const TacticDecision& host_get_enemy_last_decision() const {
        return ai_last_decision_;
    }
    // ---- Soak-fix Wave 1 probes (SOAK_TRIAGE.md §1) ----
    // Read-only player/intro state for the behavioral battle tests
    // (test_soak_ai_defects): the start-stance phase flag, the player's
    // animation name and world X — the A1 intro gate, A2 enemy stance and
    // A6 stance-hold assertions read these.
    bool host_get_start_stance() const { return start_stance_playing_; }
    const std::string& host_get_player_anim() const { return current_anim_; }
    float host_get_player_pos_x() const { return player_pos_x_; }
    // ---- Soak-fix Wave 2 probes (SOAK_TRIAGE.md §2) ----
    // Read-only player movement/facing state for the behavioral movement
    // tests (test_soak_movement_defects): M1..M5 assertions read these.
    bool host_get_player_facing() const { return facing_right_; }
    int host_get_player_move_state() const { return move_state_; }
    float host_get_player_pos_y() const { return player_pos_y_; }
    float host_get_player_turn_blend() const { return player_turn_blend_; }
    // ---- Soak-fix Wave 3 probes (SOAK_TRIAGE.md §3, §7) ----
    // Q1/Q2/Q3 tutorial-state + punching-bag probes and the L1 map-log gate
    // for the dojo/quest defect tests (test_soak_quest_defects). Read-only
    // where possible; host_set_tutorial_state is a test seam that restores
    // a mid-tutorial save (the machine save sits at COMPLETE).
    bool host_get_movement_hint_visible() const {
        return !intro_hint_dismissed_ && overlay_ == Overlay::Dialog;
    }
    void host_set_tutorial_state(std::string s) { tutorial_state_ = std::move(s); }
    int host_get_tutorial_bag_hits() const { return tutorial_bag_hits_; }
    int host_get_player_hits_landed() const { return player_fighter_.hits_landed; }

    // [S1/S2] Voice-gender state for the m_pl_*/f_pl_* sound sets. The player
    // voice comes from <Warrior Voice=> in usersDefault.xml / user.xml
    // (default "Male"); the enemy voice is resolved from the stages.xml
    // warrior template when the map hands a fight over.
    void host_set_player_voice(std::string voice) { player_voice_ = std::move(voice); }
    const std::string& host_get_player_voice() const { return player_voice_; }
    const std::string& host_get_enemy_voice() const { return enemy_voice_; }
    float host_get_bag_displacement() const { return bag_displacement(); }
    float host_get_y_adjust() const { return y_adjust_smoothed_; }
    resf2::scene::SceneId host_get_current_scene() const {
        return scene_manager_.current_id();
    }
    // [E3] Test hook: drop the tactic settings + table families so the
    // battle runs the no-settings path (ADR P4: settings absent -> neutral
    // enemy, traced idle/wait decision — the Phase E pin test).
    void host_unload_tactics() {
        tactics_ = TacticSettings{};
        tactic_tables_ = TacticTableSet{};
    }

    // ---- D4 probes: the ResponseDelay gate + fallback interval ----
    // Read-only view of the loaded-path gate state (the per-AI-frame
    // countdown the live block reads, ADR-005 D8) and the fallback-path
    // interval, plus the interval setter — for test_enemy_ai_pipeline's
    // re-entry-window and strangler checks. Phase E deletes the interval
    // probes with the member.
    int host_get_enemy_decision_countdown() const {
        return combat_.enemy_tactic_memory().frames_until_next_decision;
    }
    int host_get_enemy_reaction_countdown() const {
        return combat_.enemy_tactic_memory().enemy_reaction_frames;
    }
    // [Soak-fix A4] The per-decision Wait countdown (R4 decision+0x12) — the
    // current decision is held while > 0; the pipeline re-enters only when
    // this AND the ResponseDelay countdown are 0.
    int host_get_enemy_wait_countdown() const {
        return combat_.enemy_tactic_memory().wait_frames_remaining;
    }

    // ---- Soak-fix Wave 5 probes (U1-U6): read-only menu/weapon/asset
    // access for the scripted UI tests in test_soak_ui_defects.cpp ----
    bool host_get_menu_open() const { return overlay_ == Overlay::Menu; }
    float host_get_menu_anim_progress() const { return menu_anim_progress_; }
    std::size_t host_get_enemy_weapon_node_count() const {
        return assets_ && assets_->enemy_weapon_model()
            ? assets_->enemy_weapon_model()->nodes.size() : 0;
    }
    std::size_t host_get_enemy_weapon_triangle_count() const {
        return assets_ && assets_->enemy_weapon_model()
            ? assets_->enemy_weapon_model()->triangles.size() : 0;
    }
    std::size_t host_get_player_weapon_node_count() const {
        return assets_ && assets_->weapon_model()
            ? assets_->weapon_model()->nodes.size() : 0;
    }
    // ---- Soak-fix Wave 7a probes (P1-P10): equipment model + duck probes ----
    std::size_t host_get_armor_model_node_count() const {
        return assets_ && assets_->armor_model()
            ? assets_->armor_model()->nodes.size() : 0;
    }
    std::size_t host_get_armor_model_capsule_count() const {
        return assets_ && assets_->armor_model()
            ? assets_->armor_model()->capsules.size() : 0;
    }
    std::size_t host_get_helm_model_node_count() const {
        return assets_ && assets_->helm_model()
            ? assets_->helm_model()->nodes.size() : 0;
    }
    int host_get_armor_capsules_drawn() const { return armor_capsules_drawn_; }
    // [R4] The helm overlay drawn last frame (head.xml re-draw as "helm" is
    // the naked fighter, not armor — see load_equipment_models).
    int host_get_helm_capsules_drawn() const { return helm_capsules_drawn_; }
    // [R4] The J/U weapon cycle (HARDCODE_AUDIT H01: owned weapons only).
    std::vector<std::string> host_get_weapon_cycle() const {
        return weapon_cycle_list_;
    }
    // [R4] The authored tactic reach of a move (moves.xml <Tactics><Conditions>
    // <Distance Max=>) — the R2 battle fallback law. 0 = not parsed.
    float host_get_move_distance_max(const std::string& name) {
        if (!assets_) return 0.0f;
        auto it = assets_->moves().find(name);
        return it != assets_->moves().end() ? it->second.distance_max : 0.0f;
    }
    // [R4] Resolve an attack edge (moves.xml AttackingParts) to its world
    // endpoints + radius with the SAME law the battle hit test uses: the
    // skeleton edge when the name is a body edge, else the equipped WEAPON
    // model's edge (Q2-B: weapon moves reference WEAPON_<NAME>-Edge*_1 /
    // WEAPON_SWORDS-Blade_* on the weapon model — the weapon vertex law,
    // animated). Returns false when neither resolves (unarmed, unknown name).
    bool host_resolve_attack_edge(const std::string& edge_name,
                                  float& x1, float& y1, float& x2, float& y2,
                                  float& radius) {
        const auto pit = assets_->skeleton_nodes().find("NPivot");
        const float pivot_ly = pit != assets_->skeleton_nodes().end()
                                   ? pit->second.y : stance_npivot_y_;
        const float world_cx = player_pos_x_;
        const float world_cy = player_pos_y_ + y_adjust_smoothed_;
        const float dir = facing_right_ ? 1.0f : -1.0f;
        auto skel_edge = assets_->skeleton_edges().find(edge_name);
        if (skel_edge != assets_->skeleton_edges().end()) {
            radius = skel_edge->second.radius;
            auto [ax, ay] = resolve_body_node(skel_edge->second.end1,
                world_cx, world_cy, facing_right_, pivot_ly);
            auto [bx, by] = resolve_body_node(skel_edge->second.end2,
                world_cx, world_cy, facing_right_, pivot_ly);
            x1 = ax; y1 = ay; x2 = bx; y2 = by;
            return true;
        }
        // Weapon-model edge (Q2-B): the endpoints are MacroNodes on the
        // weapon model — resolve via the weapon LCC vertex law (the render
        // law), radius from the weapon edge.
        if (assets_->weapon_model()) {
            auto& wm = *assets_->weapon_model();
            for (const auto& we : wm.edges) {
                if (we.name != edge_name) continue;
                if (!resolve_player_weapon_vertex(wm, we.end1, world_cx,
                        world_cy, dir, pivot_ly, true, x1, y1) ||
                    !resolve_player_weapon_vertex(wm, we.end2, world_cx,
                        world_cy, dir, pivot_ly, true, x2, y2))
                    return false;
                radius = we.radius;
                return true;
            }
        }
        return false;
    }
    bool host_ui_texture_loaded(const std::string& name) const {
        auto it = assets_->hud_textures().find(name);
        return it != assets_->hud_textures().end() && it->second != nullptr;
    }

    // ---- Soak re-soak-3 probes (R1 render, R2 combat) ----
    // Camera framing + player hand/weapon geometry for the R1 probes; the
    // weapon resolver below is the SAME code path the render uses, so the
    // tests measure exactly what the screen draws.
    float host_get_zoom() const { return zoom_; }
    float host_get_camera_x() const { return cam_x_; }
    float host_get_camera_y() const { return cam_y_; }
    float host_get_floor_world_y() const { return floor_world_y_; }
    // [R1] The armor render color the body pass paints with (updated each
    // frame in render_body_model). Must be silhouette-dark — the re-soak-3
    // "тело жёлтого цвета" was the khaki robe fill.
    void host_get_armor_render_color(std::uint8_t& r, std::uint8_t& g,
                                     std::uint8_t& b) const {
        r = armor_render_color_.r;
        g = armor_render_color_.g;
        b = armor_render_color_.b;
    }
    // [R1] World-space bbox the armor capsules painted last frame.
    void host_get_armor_world_extents(float& minx, float& miny,
                                      float& maxx, float& maxy) const {
        minx = armor_world_minx_; miny = armor_world_miny_;
        maxx = armor_world_maxx_; maxy = armor_world_maxy_;
    }
    std::size_t host_get_player_weapon_triangle_count() const {
        return assets_ && assets_->weapon_model()
            ? assets_->weapon_model()->triangles.size() : 0;
    }
    // World position of the hand the weapon attaches to: the skeleton's
    // Weapon-Node2_1 node (pinned to NWrist_1 by the zero-length Edge129 in
    // skeleton.xml — the dojo placement law), resolved like the render.
    void host_get_player_hand_world(float& x, float& y) const {
        const auto pit = assets_->skeleton_nodes().find("NPivot");
        const float pivot_local_y = pit != assets_->skeleton_nodes().end()
                                        ? pit->second.y : stance_npivot_y_;
        const float world_cx = player_pos_x_;
        const float world_cy = player_pos_y_ + y_adjust_smoothed_;
        if (assets_->weapon_model() &&
            resolve_player_weapon_vertex(
                *assets_->weapon_model(), "Weapon-Node2_1", world_cx, world_cy,
                facing_right_ ? 1.0f : -1.0f, pivot_local_y, true, x, y))
            return;
        // Fallback: the wrist node resolved like the render (animated
        // first, then skeleton rest).
        auto ait = anim_node_pos_.find("NWrist_1");
        if (ait != anim_node_pos_.end()) {
            x = world_cx + ait->second.first * (facing_right_ ? 1.0f : -1.0f);
            y = floor_world_y_ + (ait->second.second +
                anim_player_.anim_npivot_bin_y()) + gameplay_y_offset_;
            return;
        }
        auto sit = assets_->skeleton_nodes().find("NWrist_1");
        if (sit != assets_->skeleton_nodes().end()) {
            x = world_cx + (facing_right_ ? sit->second.x : -sit->second.x);
            y = world_cy + (sit->second.y - pivot_local_y);
            return;
        }
        x = world_cx; y = world_cy;
    }
    // World-space centroid of weapon triangle `idx` — exactly how the
    // render draws it.
    bool host_get_player_weapon_triangle_world(int idx, float& cx, float& cy) const {
        if (!assets_ || !assets_->weapon_model() ||
            idx < 0 || idx >= (int)assets_->weapon_model()->triangles.size())
            return false;
        const auto& wm = *assets_->weapon_model();
        const auto& t = wm.triangles[idx];
        const auto pit = assets_->skeleton_nodes().find("NPivot");
        const float pivot_local_y = pit != assets_->skeleton_nodes().end()
                                        ? pit->second.y : stance_npivot_y_;
        const float world_cx = player_pos_x_;
        const float world_cy = player_pos_y_ + y_adjust_smoothed_;
        const float dir = facing_right_ ? 1.0f : -1.0f;
        float ax, ay, bx, by, dx, dy;
        if (!resolve_player_weapon_vertex(wm, t.n1, world_cx, world_cy, dir,
                                          pivot_local_y, true, ax, ay) ||
            !resolve_player_weapon_vertex(wm, t.n2, world_cx, world_cy, dir,
                                          pivot_local_y, true, bx, by) ||
            !resolve_player_weapon_vertex(wm, t.n3, world_cx, world_cy, dir,
                                          pivot_local_y, true, dx, dy))
            return false;
        cx = (ax + bx + dx) / 3.0f;
        cy = (ay + by + dy) / 3.0f;
        return true;
    }
    // R2: the enemy fighter's own model (stages.xml template items ->
    // list.xml Model attrs) — the battle hit-test target.
    std::size_t host_get_enemy_model_edge_count() const {
        return assets_ && assets_->enemy_body_model()
            ? assets_->enemy_body_model()->edges.size() : 0;
    }
    std::size_t host_get_enemy_model_capsule_count() const {
        std::size_t n = 0;
        if (assets_ && assets_->enemy_body_model())
            n += assets_->enemy_body_model()->capsules.size();
        if (assets_ && assets_->enemy_head_model())
            n += assets_->enemy_head_model()->capsules.size();
        return n;
    }

    // ---- Soak-fix Wave 7b probes (P4-P6, P8, P9, P11, P12) ----
    // HUD/quest/dialogue/shop access for test_soak_wave7b_defects.cpp.
    // Layout accessors are the single source the renderers refactor onto,
    // so the tests pin the same numbers the screen draws.

    // [P4] The localized display names the fight HUD draws.
    struct HudFighterNames {
        std::string player;
        std::string enemy;
    };
    HudFighterNames host_get_hud_fighter_names() const {
        HudFighterNames n;
        // Player: the Default warrior template's FirstName is the localization
        // key (NAME_SHADOW -> "SHADOW"); "Shadow" falls back.
        std::string pkey = "NAME_SHADOW";
        if (assets_ && assets_->stages_loaded()) {
            for (const auto& t : assets_->stage_data().templates) {
                if (t.name == "Default" && !t.first_name.empty()) {
                    pkey = t.first_name;
                    break;
                }
            }
        }
        n.player = localized(pkey);
        if (n.player.empty()) n.player = "Shadow";
        // Enemy: battle_info_.enemy_name is the localization key once
        // host_set_battle_info has resolved the stages.xml template.
        const std::string& ekey = battle_info_.enemy_name;
        n.enemy = ekey.empty() ? std::string("???") : localized(ekey);
        if (n.enemy.empty()) n.enemy = ekey;
        return n;
    }

    // [P5] The fight HUD geometry, computed from the atlas frames
    // (batchFightBars.plist: HealthBar_Empty 564x26, HealthBar_Full/Hit 1x43)
    // and the reversed binary constants (inner gap 53 pt, fill 275 pt wide,
    // bar centre 100 - h/2 pt from the top).
    struct FightHudLayout {
        float bar_w = 0, bar_h = 0;        // screen px, from the Empty frame
        float fill_w = 0, fill_h = 0;      // screen px, from the Full strip
        float bar_cy = 0;                  // screen px, bar centre y
        float bar_top_y = 0;
        float player_bar_x = 0;            // screen px, left edge
        float enemy_bar_x = 0;             // screen px, left edge
        float player_name_x = 0;           // screen px, name left edge
        float enemy_name_right = 0;        // screen px, name right edge
        float name_y = 0;                  // screen px, name top
        float dot_y = 0;                   // screen px, dot top
        float player_dot_x = 0;            // screen px, first dot left edge
    };
    FightHudLayout host_get_fight_hud_layout() const {
        FightHudLayout L{};
        if (!platform_) return L;
        const float win_w = static_cast<float>(platform_->window_width());
        const float win_h = static_cast<float>(platform_->window_height());
        const float pts = ui::points_scale(win_h);
        const float cx = win_w * 0.5f;
        auto tex_of = [&](const char* n) -> ren::Texture2D* {
            if (!assets_) return nullptr;
            auto it = assets_->hud_textures().find(n);
            return it == assets_->hud_textures().end() ? nullptr : it->second.get();
        };
        auto* empty = tex_of("HealthBar_Empty");
        auto* full = tex_of("HealthBar_Full");
        auto* undone = tex_of("Round_Undone");
        const float bar_w = (empty ? empty->width() : 564.0f) / ui::kHighTierContentScale;
        const float bar_h = (empty ? empty->height() : 26.0f) / ui::kHighTierContentScale;
        const float fill_h = (full ? full->height() : 43.0f) / ui::kHighTierContentScale;
        constexpr float kInnerGapPts = 53.0f;    // 0x102017c0
        constexpr float kFillWidthPts = 275.0f;  // ProgressBarSkewed +0x150
        L.bar_w = bar_w * pts;
        L.bar_h = bar_h * pts;
        L.fill_w = kFillWidthPts * pts;
        L.fill_h = fill_h * pts;
        L.bar_cy = (100.0f - bar_h * 0.5f) * pts;
        L.bar_top_y = (100.0f - bar_h) * pts;
        L.player_bar_x = cx - (kInnerGapPts + bar_w) * pts;
        L.enemy_bar_x = cx + kInnerGapPts * pts;
        // [P5] Names sit ABOVE the bar's OUTER edge, mirrored with the bars:
        // the player name left-aligned with the bar's left edge, the enemy
        // name right-aligned with the bar's right edge. They used to be
        // anchored at cx±315 pt — 20 pt inside the track, floating over it.
        // [HEURISTIC] Observed original layout: the name labels are set up
        // with a 250 pt width cap (0x10201c30) at the bar corner; the exact
        // anchor was not reversed, the bar-outer-edge read matches the
        // original's screen.
        L.player_name_x = cx - (kInnerGapPts + bar_w) * pts;
        L.enemy_name_right = cx + (kInnerGapPts + bar_w) * pts;
        L.name_y = (65.0f - 20.0f) * pts;
        const float dot_w = (undone ? undone->width() : 66.0f) / ui::kHighTierContentScale;
        const float dot_h = (undone ? undone->height() : 48.0f) / ui::kHighTierContentScale;
        L.dot_y = (116.0f - dot_h * 0.5f) * pts;
        L.player_dot_x = cx - (77.0f + dot_w * 0.5f) * pts;
        return L;
    }

    // [P8] The scroll panel's texture sizes from their SOURCE frames:
    // Roll_* 74 px tall -> 37 pt bar, end caps 156x74, Paper_* 116x1524
    // edge strips at their own aspect.
    struct ScrollPanelLayout {
        float bar_h = 0, end_w = 0, edge_w = 0;
    };
    ScrollPanelLayout host_get_scroll_panel_layout(float w, float h) const {
        ScrollPanelLayout L;
        // [P8] Roll bar: the Roll_left/center/right PNGs are 74 atlas px
        // tall -> 37 pt; the bar is a fixed-height 3-slice whose CENTER
        // tiles horizontally. The old w*0.13 made the bar grow with the
        // panel (~2.5x too tall on the story dialogue) and stretched the
        // 156x74 end caps along with it. Paper edges are the 116x1524 side
        // strips drawn at their own 1:13.1 aspect (the old w*0.055 squashed
        // them ~3.4x and, in the dojo hint overlay, a *6 fudge smeared
        // them). Panel width/height stay inputs for the tiled centre.
        const float pts = ui::points_scale(
            platform_ ? static_cast<float>(platform_->window_height()) : 720.0f);
        L.bar_h = (74.0f / ui::kHighTierContentScale) * pts;
        L.end_w = L.bar_h * (156.0f / 74.0f);
        L.edge_w = h * (116.0f / 1524.0f);
        (void)w;
        return L;
    }

    // [P12] The story-dialogue panel geometry (JS-authored proportions).
    // The shared scene::DialogueLayout is the single source for the
    // DialogueScene render and the placement tests.
    scene::DialogueLayout host_dialogue_layout(float w, float h) const override {
        scene::DialogueLayout D;
        D.box_w = w * 0.53f;                    // 900/1700
        D.box_h = h * 0.20f;
        D.box_x = w * 0.235f;                   // (1700-900)/2/1700
        D.box_y = (h - D.box_h) * 0.5f;         // centred vertically
        D.pad = D.box_h * 0.08f;
        D.portrait_size = D.box_h * 0.875f;     // 700/800
        D.portrait_x = D.box_x + D.box_w * 0.017f;
        D.portrait_y = D.box_y + (D.box_h - D.portrait_size) * 0.5f;
        D.text_x = D.portrait_x + D.portrait_size + D.box_w * 0.02f;
        return D;
    }

    // [P9] The shop preview renderer + what it drew last frame.
    struct ShopPreviewGeometry {
        std::size_t body_capsules = 0;
        std::size_t body_triangles = 0;
        std::size_t weapon_triangles = 0;
    };
    // [P9] The shop's left column shows the REAL fighter: the body model at
    // its rest pose plus the equipped weapon (the P1 weapon model), instead
    // of the flat "FIGHTER" placeholder silhouette. Model frame is Y-up with
    // the NPivot at the origin (feet ~-96, head top ~+110) — the same local
    // frame render_body_model resolves in the dojo — mapped onto the panel
    // rect (x, y, w, h) in screen space.
    void host_render_shop_preview(float x, float y, float w, float h) {
        shop_preview_geom_ = ShopPreviewGeometry{};
        if (!renderer_ || !assets_ || !assets_->body_model()) return;
        auto pivot_it = assets_->skeleton_nodes().find("NPivot");
        const float pivot_local_y = pivot_it != assets_->skeleton_nodes().end()
                                        ? pivot_it->second.y : stance_npivot_y_;
        const float scale = std::min(w, h) / 240.0f;   // fighter ~210 units tall
        const float cxs = x + w * 0.5f;
        const float cys = y + h * 0.55f;               // feet near the bottom
        auto to_s = [&](float wx, float wy) {
            return std::pair<float, float>{cxs + wx * scale, cys - wy * scale};
        };
        const ren::Color4B sil{20, 20, 25, 255};

        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : assets_->body_model()->edges)
            edge_map[e.name] = {e.end1, e.end2};
        for (auto& [ename, e] : assets_->skeleton_edges())
            edge_map[ename] = {e.end1, e.end2};

        // Capsules — same trim/margin math as render_body_model.
        for (auto& c : assets_->body_model()->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto [x1, y1] = resolve_body_node(eit->second.first, 0, 0, true,
                                              pivot_local_y);
            auto [x2, y2] = resolve_body_node(eit->second.second, 0, 0, true,
                                              pivot_local_y);
            const float m1 = c.margin1, m2 = c.margin2;
            const float mx1 = x1 + (x2 - x1) * m1, my1 = y1 + (y2 - y1) * m1;
            const float mx2 = x2 - (x2 - x1) * m2, my2 = y2 - (y2 - y1) * m2;
            const float r = (c.radius1 + c.radius2) * 0.5f;
            const float dx = mx2 - mx1, dy = my2 - my1;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.5f) continue;
            const float ux = dx / len, uy = dy / len;
            const float px = -uy, py = ux;
            const float ht = std::max(r, 1.0f);
            auto [ax, ay] = to_s(mx1 + px * ht, my1 + py * ht);
            auto [bx, by] = to_s(mx2 + px * ht, my2 + py * ht);
            auto [cx, cy_] = to_s(mx2 - px * ht, my2 - py * ht);
            auto [dx_, dy_] = to_s(mx1 - px * ht, my1 - py * ht);
            renderer_->draw_filled_triangle_screen(ax, ay, bx, by, cx, cy_, sil);
            renderer_->draw_filled_triangle_screen(ax, ay, cx, cy_, dx_, dy_, sil);
            auto [c1x, c1y] = to_s(mx1, my1);
            auto [c2x, c2y] = to_s(mx2, my2);
            renderer_->draw_filled_circle_screen(c1x, c1y, ht * scale, sil);
            renderer_->draw_filled_circle_screen(c2x, c2y, ht * scale, sil);
            ++shop_preview_geom_.body_capsules;
        }
        // Skeleton edges with a Radius but no body capsule (EHead/ENeck) —
        // without them the preview has no head.
        for (auto& [ename, sedge] : assets_->skeleton_edges()) {
            if (sedge.radius <= 0) continue;
            bool has_capsule = false;
            for (auto& c : assets_->body_model()->capsules)
                if (c.edge_name == ename) { has_capsule = true; break; }
            if (has_capsule) continue;
            auto [x1, y1] = resolve_body_node(sedge.end1, 0, 0, true, pivot_local_y);
            auto [x2, y2] = resolve_body_node(sedge.end2, 0, 0, true, pivot_local_y);
            const float m1 = sedge.margin1, m2 = sedge.margin2;
            const float mx1 = x1 + (x2 - x1) * m1, my1 = y1 + (y2 - y1) * m1;
            const float mx2 = x2 - (x2 - x1) * m2, my2 = y2 - (y2 - y1) * m2;
            const float dx = mx2 - mx1, dy = my2 - my1;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.5f) continue;
            const float ux = dx / len, uy = dy / len;
            const float px = -uy, py = ux;
            const float ht = std::max(sedge.radius, 1.0f);
            auto [ax, ay] = to_s(mx1 + px * ht, my1 + py * ht);
            auto [bx, by] = to_s(mx2 + px * ht, my2 + py * ht);
            auto [cx, cy_] = to_s(mx2 - px * ht, my2 - py * ht);
            auto [dx_, dy_] = to_s(mx1 - px * ht, my1 - py * ht);
            renderer_->draw_filled_triangle_screen(ax, ay, bx, by, cx, cy_, sil);
            renderer_->draw_filled_triangle_screen(ax, ay, cx, cy_, dx_, dy_, sil);
            auto [c1x, c1y] = to_s(mx1, my1);
            auto [c2x, c2y] = to_s(mx2, my2);
            renderer_->draw_filled_circle_screen(c1x, c1y, ht * scale, sil);
            renderer_->draw_filled_circle_screen(c2x, c2y, ht * scale, sil);
        }
        // Triangles — only pure skeletal ones (same filter as the dojo body
        // render, so cloth nodes do not stretch the rest pose).
        for (auto& t : assets_->body_model()->triangles) {
            auto is_non_skel = [&](const std::string& n) {
                return assets_->body_model()->nodes.count(n) > 0 ||
                       assets_->body_model()->macro_nodes.count(n) > 0;
            };
            if (is_non_skel(t.n1) || is_non_skel(t.n2) || is_non_skel(t.n3)) continue;
            auto can_resolve = [&](const std::string& n) {
                return anim_node_pos_.count(n) || assets_->skeleton_nodes().count(n);
            };
            if (!can_resolve(t.n1) || !can_resolve(t.n2) || !can_resolve(t.n3)) continue;
            auto [tx0, ty0] = resolve_body_node(t.n1, 0, 0, true, pivot_local_y);
            auto [tx1, ty1] = resolve_body_node(t.n2, 0, 0, true, pivot_local_y);
            auto [tx2, ty2] = resolve_body_node(t.n3, 0, 0, true, pivot_local_y);
            auto [sx0, sy0] = to_s(tx0, ty0);
            auto [sx1, sy1] = to_s(tx1, ty1);
            auto [sx2, sy2] = to_s(tx2, ty2);
            renderer_->draw_filled_triangle_screen(sx0, sy0, sx1, sy1, sx2, sy2, sil);
            ++shop_preview_geom_.body_triangles;
        }
        // The equipped weapon (P1 model) at the hand, same placement law as
        // the dojo render: the MacroNode LCC resolver over the skeleton's
        // Weapon-Node* pins. [Wave 9B] The pose source must MATCH the body
        // render above (anim-first): with use_anim=false the knife resolved
        // against the skeleton REST pins while the body used the (stale)
        // dojo pose, so the knife hung above the hands (re-soak-5).
        // [R1] The old draw used the authored rest coords at a fixed
        // +30/+10 offset at 0.3x �?" the knife floated above the head.
        if (assets_->weapon_model() && !assets_->weapon_model()->triangles.empty()) {
            const ren::Color4B wcol{150, 154, 162, 255};
            auto& wm = *assets_->weapon_model();
            for (const auto& t : wm.triangles) {
                float ax, ay, bx, by, cx, cy;
                if (!resolve_player_weapon_vertex(wm, t.n1, 0.0f, 0.0f,
                        1.0f, pivot_local_y, kShopPreviewUseAnim, ax, ay) ||
                    !resolve_player_weapon_vertex(wm, t.n2, 0.0f, 0.0f,
                        1.0f, pivot_local_y, kShopPreviewUseAnim, bx, by) ||
                    !resolve_player_weapon_vertex(wm, t.n3, 0.0f, 0.0f,
                        1.0f, pivot_local_y, kShopPreviewUseAnim, cx, cy))
                    continue;
                auto [sx0, sy0] = to_s(ax, ay);
                auto [sx1, sy1] = to_s(bx, by);
                auto [sx2, sy2] = to_s(cx, cy);
                renderer_->draw_filled_triangle_screen(sx0, sy0, sx1, sy1, sx2, sy2, wcol);
                ++shop_preview_geom_.weapon_triangles;
            }
        }
        // [Wave 9B] Preview-space gap between the body's hand and the weapon
        // anchor, under the SAME resolve law the draw uses (probe for S4).
        {
            auto [hx, hy] = resolve_body_node("NWrist_1", 0, 0, true, pivot_local_y);
            float wx = 0, wy = 0;
            const bool wok = assets_->weapon_model() &&
                resolve_player_weapon_vertex(*assets_->weapon_model(),
                    "Weapon-Node2_1", 0.0f, 0.0f, 1.0f, pivot_local_y,
                    kShopPreviewUseAnim, wx, wy);
            const float dx = hx - wx, dy = hy - wy;
            shop_preview_hand_gap_ = wok ? std::sqrt(dx * dx + dy * dy) : 1e9f;
        }
    }
    ShopPreviewGeometry host_get_shop_preview_geometry() const {
        return shop_preview_geom_;
    }
    // [Wave 9B] Preview-space distance between the body hand (NWrist_1) and
    // the weapon anchor (Weapon-Node2_1), recorded by the preview render
    // under its own resolve law. ~0 when the knife rides the hand.
    float host_get_shop_preview_hand_gap() const { return shop_preview_hand_gap_; }

    // [P6] The Results scene's continue-button label (rematch on a
    // retryable defeat, else continue/back-to-menu).
    std::string host_get_results_button_label() const override {
        const std::string key = (battle_result_ == "victory") ? "continue"
                                : (battle_result_ == "defeat" &&
                                   tutorial_state_ == "FIRST_FIGHT")
                                    ? "dlgStoryBtnRematch"
                                    : "backToMenu";
        std::string label = localized(key);
        if (label.empty())
            label = (battle_result_ == "victory") ? "CONTINUE" : "BACK TO MENU";
        // dlgStoryBtnRematch carries "{image0} {1}" placeholders; strip them.
        const auto brace = label.find(" {");
        if (brace != std::string::npos) label = label.substr(0, brace);
        return label;
    }

    // [P6] Test seams: deterministically kill a fighter (drive the battle to
    // a known outcome) and re-run the tutorial state check.
    void host_damage_player(float amount) {
        auto& f = combat_.player_fighter();
        f.health -= amount;
        if (f.health <= 0.0f) { f.health = 0.0f; f.is_dead = true; }
    }
    void host_damage_enemy(float amount) {
        auto& f = combat_.enemy_fighter();
        f.health -= amount;
        if (f.health <= 0.0f) { f.health = 0.0f; f.is_dead = true; }
    }
    void host_run_tutorial_check() { check_tutorial(); }
    bool host_get_show_enemy() const { return show_enemy_; }
    // [Wave 9B] S4: the shop preview's weapon draw resolves with the SAME
    // pose source as the body pass (anim-first) so the knife rides the hand
    // even with a stale dojo pose in anim_node_pos_ (re-soak-5: the knife
    // hung above the hands — body drew the stale pose, weapon drew
    // skeleton-rest pins). With the anim empty both fall back to the same
    // skeleton rest pose, so a fresh shop is consistent too.
    static constexpr bool kShopPreviewUseAnim = true;
    ShopPreviewGeometry shop_preview_geom_;
    float shop_preview_hand_gap_ = 1e9f;
    // [R1] Last armor render color (probe seam for the body-color test).
    ren::Color4B armor_render_color_{0, 0, 0, 255};
    // [R1] World extents of the armor capsules as rendered last frame (the
    // probe samples exactly the robe's painted area).
    float armor_world_minx_ = 0, armor_world_miny_ = 0;
    float armor_world_maxx_ = 0, armor_world_maxy_ = 0;
    // [R4] Helm capsules drawn last frame (0 for the default save — the
    // base head model is the naked fighter, not a helm overlay).
    int helm_capsules_drawn_ = 0;

void host_reset_round() override;

void host_set_round_wins(int player, int enemy) override;

void host_set_round_left_ms(int ms) override;


int host_get_currency() const override;


bool host_spend_currency(int amount) override;


void host_add_currency(int amount) override;


    // ---- Inventory / Shop ----

bool host_has_item(const std::string& item_id) const override;

// Test seam: put an item into the inventory without shop gates.
void host_add_item(const std::string& item_id);


std::vector<std::string> host_get_owned_items() const override;


std::string host_get_equipped(const std::string& slot) const override;


bool host_buy_item(const std::string& item_id) override;


bool host_sell_item(const std::string& item_id) override;


bool host_equip_item(const std::string& item_id) override;


bool host_unequip_item(const std::string& slot) override;


int host_get_player_level() const override;


int host_get_wins() const override;


int host_get_losses() const override;


const resf2::format::ListData* host_get_list_data() const override;


std::string host_get_current_level() const override;


    // [Wave 8] Boot-order probe: loader events in the order they ran
    // (fidelity test item 3).
    const std::vector<std::string>& boot_events() const { return boot_events_; }

    const resf2::game::BootConfigs& boot_configs() const { return boot_configs_; }


    void host_add_win() override;

    void host_add_loss() override;

    // Sync the combat equipped_weapon_ from the inventory.
    // Called after loading save data and after equipping a weapon.
    void sync_equipped_weapon();

    // Rebuild both fighters' AttributeSets (the model+0x1C4 map the recovered
    // damage formula reads): the player's from equipped items, the enemy's
    // from the <AlignTargetAttributes> baseline. Idempotent; called after
    // save load and on every equip/unequip.
    void rebuild_fighter_attributes();

    // ---------- Audio hooks ----------

    void host_start_menu_music() override;

    void host_start_battle_music() override;

    void host_stop_music() override;

    void host_play_ui_click() override;

    void host_play_result_sound(const std::string& result) override;

    void host_render_text(const std::string& text, float x, float y, float scale, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) const override;

bool host_render_zone_bg(int zone_index, float x, float y, float w, float h) override;
// --- Map screen (see scene_system.hpp for the contract) ---
scene::SceneHost::MapView host_render_zone_map(int zone_index, float scroll_x,
                                               float x, float y, float w, float h) override;
bool host_render_battle_icon(const std::string& icon, int state,
                             float cx, float cy, float size) override;
bool host_render_battle_preview(const std::string& location,
                                float x, float y, float w, float h) override;
void host_render_scroll_panel(float x, float y, float w, float h) override;
bool host_render_ui_texture(const std::string& name,
                            float x, float y, float w, float h) override;
void host_render_top_panel() override;
std::string host_localized(const std::string& key) const override { return localized(key); }
std::pair<float, float> host_measure_text(const std::string& text, float scale) const override {
    return measure_text(text, scale);
}
// Lazily loads the battle-icon atlases and the per-location preview photos.
void load_map_textures();
ren::Texture2D* battle_preview_texture(const std::string& location);


void host_set_show_enemy(bool show) override;


void host_set_battle_mode(bool battle) override;


    // Access the current PlayerProfile (for tests and new code).
    const player::PlayerProfile& player_profile() const noexcept {
        return player_profile_;
    }

    // Called by MainMenuScene and BattleScene to update the dojo gameplay
    // (movement, combat, animation, physics, overlays).
    void host_update_gameplay(uint32_t dt);

    // [STEP 4.7] Trigger knockback/knockdown on the player fighter.
    // Sets vertical velocity (launch upward) and knockdown state.
    // The physics update in host_update_gameplay handles the rest.
    void trigger_knockback(float launch_velocity, bool knockdown);

    // Called by MainMenuScene and BattleScene to render the dojo scene
    // (background, character, bag, HUD, menu/dialog overlays).
void host_render_scene();


    // [ORIGINAL] Render the enemy using the SAME body_model (body.xml + head.xml)
    // as the player, with enemy-specific position/facing/animation state.
    // Full body + head model (capsules + triangles + skeleton edges).
    void render_enemy_fighter() {
        if (enemy_fighter_.is_dead || !assets_->body_model() || assets_->skeleton_nodes().empty()) return;
        auto np_it = assets_->skeleton_nodes().find("NPivot");
        if (np_it == assets_->skeleton_nodes().end()) return;
        float npivot_rest_y = np_it->second.y;
        float world_cx = enemy_pos_x_;
        float world_cy = enemy_pos_y_ + enemy_y_adjust_;
        // Enemy color: dark red (vs player black), white flash on hit, blue block
        ren::Color4B enemy_col = (enemy_hit_flash_ > 0) ?
            ren::Color4B{255, 180, 180, 255} : ren::Color4B{70, 30, 30, 255};
        if (enemy_fighter_.is_blocking) enemy_col = ren::Color4B{40, 40, 80, 255};
        // Get enemy animation frame
        std::string anim_name = enemy_anim_;
        if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
            anim_name = anim_name.substr(0, anim_name.size()-4);
        auto anim_it = assets_->animations().find(anim_name);
        int frame_idx = 0, next_idx = 0;
        float alpha = 0;
        bool has_anim = (anim_it != assets_->animations().end() && anim_it->second.frame_count > 0);
        if (has_anim) {
            auto& anim = anim_it->second;
            float f = enemy_anim_time_ * 20.0f;  // enemy uses fixed 20fps (no MoveDef mid_frames)
            if (f < 0) f = 0;
            int fi = (int)f;
            if (fi < 0) fi = 0;
            frame_idx = anim.frame_count > 0 ? fi % anim.frame_count : 0;
            next_idx = (frame_idx + 1) % anim.frame_count;
            alpha = f - (int)f;
        }
        // Compute animated NPivot (reference for all nodes — prevents stretching)
        float animated_npx = np_it->second.x, animated_npy = npivot_rest_y;
        if (has_anim) {
            for (int i = 0; i < (int)assets_->ordered_node_names().size() && i < 67; ++i) {
                if (assets_->ordered_node_names()[i] == "NPivot") {
                    float x0, y0, z0, x1, y1, z1;
                    if (anim_it->second.get_node_pos(frame_idx, i, x0, y0, z0) &&
                        anim_it->second.get_node_pos(next_idx, i, x1, y1, z1)) {
                        animated_npx = x0 + (x1 - x0) * alpha;
                        animated_npy = y0 + (y1 - y0) * alpha;
                    }
                    break;
                }
            }
        }
        // Build temp anim_node_pos for enemy
        std::unordered_map<std::string, std::pair<float, float>> enemy_node_pos;
        if (has_anim) {
            for (int i = 0; i < (int)assets_->ordered_node_names().size() && i < 67; ++i) {
                const std::string& name = assets_->ordered_node_names()[i];
                float x0, y0, z0, x1, y1, z1;
                if (anim_it->second.get_node_pos(frame_idx, i, x0, y0, z0) &&
                    anim_it->second.get_node_pos(next_idx, i, x1, y1, z1)) {
                    enemy_node_pos[name] = {x0 + (x1 - x0) * alpha, y0 + (y1 - y0) * alpha};
                }
            }
        }
        // Resolve enemy node to world coords (std::function for recursion)
        std::function<bool(const std::string&, float&, float&)> resolve = [&](const std::string& name, float& ox, float& oy) -> bool {
            float lx, ly;
            auto ait = enemy_node_pos.find(name);
            if (ait != enemy_node_pos.end()) {
                lx = ait->second.first; ly = ait->second.second;
            } else {
                auto sit = assets_->skeleton_nodes().find(name);
                if (sit != assets_->skeleton_nodes().end()) {
                    lx = sit->second.x; ly = sit->second.y;
                } else {
                    auto bit = assets_->body_model()->nodes.find(name);
                    if (bit != assets_->body_model()->nodes.end()) {
                        lx = bit->second.x; ly = bit->second.y;
                    } else {
                        auto mit = assets_->body_model()->macro_nodes.find(name);
                        if (mit != assets_->body_model()->macro_nodes.end()) {
                            float sum_lcc = 0, wxx = 0, wyy = 0;
                            for (int i = 0; i < 4; ++i) {
                                if (mit->second.children[i].empty()) continue;
                                float cx, cy;
                                if (!resolve(mit->second.children[i], cx, cy)) continue;
                                wxx += cx * mit->second.lcc[i];
                                wyy += cy * mit->second.lcc[i];
                                sum_lcc += mit->second.lcc[i];
                            }
                            if (std::abs(sum_lcc) > 1e-6f) {
                                ox = wxx / sum_lcc; oy = wyy / sum_lcc;
                                return true;
                            }
                            return false;
                        }
                        return false;
                    }
                }
            }
            float dx = lx - animated_npx;
            float dy = ly - animated_npy;
            ox = world_cx + (enemy_facing_right_ ? dx : -dx);
            oy = world_cy + dy;
            return true;
        };
        // Edge map
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : assets_->body_model()->edges) edge_map[e.name] = {e.end1, e.end2};
        for (auto& [name, e] : assets_->skeleton_edges()) edge_map[name] = {e.end1, e.end2};
        // Render capsules
        for (auto& c : assets_->body_model()->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            float x1, y1, x2, y2;
            if (!resolve(eit->second.first, x1, y1) || !resolve(eit->second.second, x2, y2)) continue;
            float mx1 = x1 + (x2 - x1) * c.margin1, my1 = y1 + (y2 - y1) * c.margin1;
            float mx2 = x2 - (x2 - x1) * c.margin2, my2 = y2 - (y2 - y1) * c.margin2;
            float r = (c.radius1 + c.radius2) * 0.5f;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            renderer_->draw_filled_triangle_world(mx1+px*ht, my1+py*ht, mx2+px*ht, my2+py*ht,
                mx2-px*ht, my2-py*ht, enemy_col);
            renderer_->draw_filled_triangle_world(mx1+px*ht, my1+py*ht, mx2-px*ht, my2-py*ht,
                mx1-px*ht, my1-py*ht, enemy_col);
            renderer_->draw_filled_circle_world(mx1, my1, ht, enemy_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, enemy_col);
        }

        // Render the enemy's weapon at the hand. [U1] The model now carries
        // real geometry (MacroNodes + Triangles); the old code drew a fixed
        // yellow capsule whenever the model pointer existed -- with 0 parsed
        // nodes that was the "yellow placeholder" the soak saw. When the
        // model has geometry, render the mesh; when it has none, draw
        // NOTHING (an empty model must not fake a weapon).
        // [R1] Same vertex law as the player's weapon: the triangles
        // reference the model's own MacroNodes, which compute their
        // position from the skeleton's Weapon-Node* pins at the hand
        // (Edge130 pins Weapon-Node2_2 to NWrist_2). The old code looked
        // the vertices up in the plain node map (empty) and drew the
        // authored rest coords at a hand+30/-10 offset -- the weapon
        // floated far from the fighter. Steel tone, not yellow.
        if (assets_->enemy_weapon_model() &&
            !assets_->enemy_weapon_model()->triangles.empty()) {
            ren::Color4B wcol{150, 154, 162, 255};
            if (enemy_hit_flash_ > 0) wcol = ren::Color4B{255, 255, 220, 255};
            const float dir = enemy_facing_right_ ? 1.0f : -1.0f;
            auto& wm = *assets_->enemy_weapon_model();
            // World-space resolver for the ENEMY's weapon: MacroNode LCC
            // over children, authored node fallback (enemy rest transform),
            // else the enemy node resolver (animated/skeleton) above.
            std::function<bool(const std::string&, float&, float&)> wresolve =
                [&](const std::string& name, float& wx, float& wy) -> bool {
                auto mit = wm.macro_nodes.find(name);
                if (mit != wm.macro_nodes.end()) {
                    float sum = 0.0f, ax = 0.0f, ay = 0.0f;
                    for (int i = 0; i < 4; ++i) {
                        if (mit->second.children[i].empty()) continue;
                        float cx, cy;
                        if (!wresolve(mit->second.children[i], cx, cy)) continue;
                        ax += cx * mit->second.lcc[i];
                        ay += cy * mit->second.lcc[i];
                        sum += mit->second.lcc[i];
                    }
                    if (std::fabs(sum) > 1e-6f) { wx = ax / sum; wy = ay / sum; return true; }
                    return false;
                }
                auto nit = wm.nodes.find(name);
                if (nit != wm.nodes.end()) {
                    wx = enemy_pos_x_ + dir * nit->second.x;
                    wy = (enemy_pos_y_ + enemy_y_adjust_) +
                         (nit->second.y - npivot_rest_y);
                    return true;
                }
                return resolve(name, wx, wy);
            };
            for (const auto& t : wm.triangles) {
                float ax, ay, bx, by, cx, cy;
                if (!wresolve(t.n1, ax, ay) || !wresolve(t.n2, bx, by) ||
                    !wresolve(t.n3, cx, cy))
                    continue;
                renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy, wcol);
            }
        }
        // Render skeleton edges with Radius (EHead, ENeck)
        for (auto& [ename, sedge] : assets_->skeleton_edges()) {
            if (sedge.radius <= 0) continue;
            bool has_capsule = false;
            for (auto& c : assets_->body_model()->capsules) {
                if (c.edge_name == ename) { has_capsule = true; break; }
            }
            if (has_capsule) continue;
            float x1, y1, x2, y2;
            if (!resolve(sedge.end1, x1, y1) || !resolve(sedge.end2, x2, y2)) continue;
            float r = sedge.radius;
            float mx1 = x1 + (x2 - x1) * sedge.margin1, my1 = y1 + (y2 - y1) * sedge.margin1;
            float mx2 = x2 - (x2 - x1) * sedge.margin2, my2 = y2 - (y2 - y1) * sedge.margin2;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) {
                renderer_->draw_filled_circle_world((mx1+mx2)*0.5f, (my1+my2)*0.5f, r, enemy_col);
                continue;
            }
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            renderer_->draw_filled_triangle_world(mx1+px*ht, my1+py*ht, mx2+px*ht, my2+py*ht,
                mx2-px*ht, my2-py*ht, enemy_col);
            renderer_->draw_filled_triangle_world(mx1+px*ht, my1+py*ht, mx2-px*ht, my2-py*ht,
                mx1-px*ht, my1-py*ht, enemy_col);
            renderer_->draw_filled_circle_world(mx1, my1, ht, enemy_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, enemy_col);
        }
        // Render triangles (skip cloth-node AND MacroNode triangles)
        // [ORIGINAL] HEAD-Triangle references HEAD-MacroNode, which uses LCC
        // weights to compute position from skeleton children (NTop, NHeadF, etc.).
        // These LCC weights are calibrated for rest pose — when skeleton animates,
        // the weighted sum produces stretched/wrong positions. Without cloth
        // simulation, these triangles can't render correctly. Skip them.
        for (auto& t : assets_->body_model()->triangles) {
            auto is_non_skel = [&](const std::string& n) {
                return assets_->body_model()->nodes.count(n) > 0 ||
                       assets_->body_model()->macro_nodes.count(n) > 0;
            };
            if (is_non_skel(t.n1) || is_non_skel(t.n2) || is_non_skel(t.n3)) continue;
            float tx0, ty0, tx1, ty1, tx2, ty2;
            if (!resolve(t.n1, tx0, ty0) || !resolve(t.n2, tx1, ty1) ||
                !resolve(t.n3, tx2, ty2)) continue;
            renderer_->draw_filled_triangle_world(tx0, ty0, tx1, ty1, tx2, ty2, enemy_col);
        }
    }

    // Called by LoadingScene to render the loading screen.
    void host_render_loading();

private:
    // ---------- Loading screen ----------
    void load_loading_screen() {
        assets_->load_loading_screen(asset_root_,
            platform_->window_width(), platform_->window_height());
    }

    // Initialize the dojo location: load all assets and set up the scene.
    // Called by host_load_location() (SceneHost interface) when entering
    // MainMenu or Battle scene.
    void init_location();

    void render_loading_screen(plat::Platform& platform) {
        float tw = 1820.0f * load_scale_, th = 1024.0f * load_scale_;
        float ox = (platform.window_width() - tw) / 2.0f;
        float oy = (platform.window_height() - th) / 2.0f;
        for (auto& img : assets_->loading_images()) {
            if (!img.texture) continue;
            float w = img.texture->width() * load_scale_;
            float h = img.texture->height() * load_scale_;
            float x = ox + (img.x + 910.0f) * load_scale_;
            float y = oy + (img.y + 512.0f) * load_scale_;
            renderer_->draw_textured_quad_screen(*img.texture, x, y, w, h);
        }
    }

    // ---------- Location ----------
    void load_location(const std::string& name) {
        locations_.load_location(name, asset_root_, assets_.get());
        location_ = locations_.location();
    }

    void load_atlas(const std::string& name, const std::string& loc) {
        auto root = std::filesystem::path(asset_root_);
        for (const auto& dir : {root/"assets"/"1536"/"locations"/loc,
                                 root/"assets"/"1536"/"textures",
                                 root/"assets"/"1536",
                                 root/"1536"/"locations"/loc,
                                 root/"1536"/"textures",
                                 root/"1536",
                                 root/"assets",
                                 root}) {
            auto pp = dir/(name+".plist"), pn = dir/(name+".png");
            if (std::filesystem::exists(pp) && std::filesystem::exists(pn)) {
                auto result = plist::parse(read_text(pp.string()));
                if (!result) continue;
                auto png_data = read_file(pn.string());
                // Decode atlas PNG for pre-cropping rotated frames
                int aw, ah, ach;
                auto* atlas_px = stbi_load_from_memory(
                    (const stbi_uc*)png_data.data(), (int)png_data.size(),
                    &aw, &ah, &ach, 4);
                auto tex = std::make_unique<ren::Texture2D>();
                if (!tex->init_from_png((const uint8_t*)png_data.data(),
                                         png_data.size())) {
                    if (atlas_px) stbi_image_free(atlas_px);
                    continue;
                }
                AtlasRef a;
                a.texture = std::move(tex);
                a.atlas = std::make_shared<plist::ParsedAtlas>(std::move(*result));
                // Pre-crop rotated frames into individual un-rotated textures
                if (atlas_px) {
                    for (auto& [fname, idx] : a.atlas->name_index) {
                        auto& frame = a.atlas->frames[idx];
                        if (!frame.rotated) continue;
                        // For rotated frames, atlas_w/atlas_h are ATLAS (post-rotation) dimensions.
                        // Original sprite dimensions are swapped.
                        int fw = frame.atlas_h;  // original width (swapped)
                        int fh = frame.atlas_w;  // original height (swapped)
                        auto ctex = std::make_unique<ren::Texture2D>();
                        std::vector<std::uint8_t> px((size_t)fw * fh * 4);
                        for (int y = 0; y < fh; ++y) {
                            for (int x = 0; x < fw; ++x) {
                                // Un-rotate 90° CCW (Cocos2d stores rotated 90° CW)
                                // Formula A (proven correct for location textures):
                                // dest(x,y) ← source(atlas_x + (fh-1-y), atlas_y + x)
                                int sx = frame.atlas_x + (fh - 1 - y);
                                int sy = frame.atlas_y + x;
                                if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                                int src_idx = (sy * aw + sx) * 4;
                                int dst_idx = (y * fw + x) * 4;
                                px[dst_idx+0] = atlas_px[src_idx+0];
                                px[dst_idx+1] = atlas_px[src_idx+1];
                                px[dst_idx+2] = atlas_px[src_idx+2];
                                px[dst_idx+3] = atlas_px[src_idx+3];
                            }
                        }
                        ctex->init_rgba(fw, fh, px.data());
                        std::string n = fname;
                        if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
                        a.cropped[n] = std::move(ctex);
                    }
                    stbi_image_free(atlas_px);
                }
                std::printf("  Atlas '%s': %zu frames, %zu pre-cropped\n",
                            name.c_str(), a.atlas->frames.size(), a.cropped.size());
                assets_->atlases()[name] = std::move(a);
                return;
            }
        }
        std::printf("  Atlas '%s' NOT FOUND\n", name.c_str());
    }

    // Recompute the world→viewport transform for the current location.
    //
    // [ORIGINAL] Location::load (ShadowFight2.s86 FUN_10144420) stores the
    // location box as +0x38 Width ("whole world width") and +0x3c Height
    // ("whole world height"). World units are location-atlas texels: every
    // <Image> in params.xml carries its source size in the same units. The
    // viewport therefore has to show exactly `Height` world units vertically —
    // the location's top layer draws mask rectangles (ClassName="pixel_1")
    // over everything outside that box so wider or taller screens stay clean.
    //
    // The previous code left zoom at 1.0, i.e. one world unit per screen
    // pixel, so on a 712 px tall window it showed 712 world units instead of
    // 560 — everything came out 1.27x too small and vertically off-centre.
    void update_camera() {
        cam_y_ = 0.0f;
        if (location_)
            floor_world_y_ = -location_->height * 0.5f + location_->floor;
        if (!location_ || location_->height <= 0.0f || !platform_) {
            zoom_ = 1.0f;
            cam_x_ = player_pos_x_;
            return;
        }
        const float vw = static_cast<float>(platform_->window_width());
        const float vh = static_cast<float>(platform_->window_height());

        // Vertical framing: show the full world height, centred on the origin.
        //
        // [HEURISTIC-TODO] This is the right order of magnitude but not yet the
        // original's exact frame. Two things are still unresolved and have to
        // be reversed out of Fight (ShadowFight2.s86 FUN_100b3860 — Location*
        // at +0x24c, embedded Camera at +0x250):
        //   1. the visible band may be narrower than Height — the location
        //      draws full-width mask rectangles (ClassName="pixel_1") at world
        //      y >= +226 and y <= -220, leaving 446 of the 560 units. Framing
        //      to that gap alone overshoots, so the rule involves Wall (305)
        //      and/or Floor (80) too.
        //   2. the fighter's model-space -> world-space mapping puts its feet
        //      at world ~-276 while player_pos_y - 96 says -189, so the sprite
        //      transform is off by ~87 units independently of the camera. That
        //      has to be fixed before the frame can be judged.
        zoom_ = vh / location_->height;

        // [HEURISTIC-TODO] Framing rule not yet confirmed against the binary.
        // The original drives its Camera object (ShadowFight2.s86 ctor
        // FUN_10070270, owned by Fight FUN_100b3860) through the animation
        // system — it has "Camera"/"Position" slots — so the exact follow law
        // still has to be reversed. Centring on the midpoint of the two
        // fighters reproduces the reference framing: with dojo's player at
        // world -290 and enemy at -7 it puts them at ~36% / ~64% of the
        // screen width, matching the original's first-launch screenshot.
        const float half_view_w = vw / (2.0f * zoom_);
        const float half_world_w = location_->width * 0.5f;
        float cx = (player_pos_x_ + enemy_pos_x_) * 0.5f;
        // The camera clamps to the WORLD edge, not to `Wall`.
        //
        // `Wall` (305 on dojo, against a half-width of 980) is where the side
        // walls stand — the sprites `left`/`right` are drawn at X=+-680. I
        // briefly clamped the camera to that, because the view was running
        // into an empty strip where the sky showed through. That was treating
        // a symptom of a different defect: `bg` and `atlas_layer1` are stored
        // ROTATED, and the un-rotation was transposing them, so those layers
        // were painted sideways and left the edges bare. With that fixed the
        // strip is covered, and the wall clamp only did harm: it narrowed the
        // camera's travel to +-177 while the midpoint between the fighters
        // ranges over +-228, so the camera sat against its limit and looked
        // like it had stopped following the player.
        //
        // What `Wall` actually bounds is still open (1.2) — most likely the
        // FIGHTERS, not the view.
        const float half_playable_w = half_world_w;
        if (half_view_w >= half_playable_w) {
            cx = 0.0f;
        } else {
            const float lo = -half_playable_w + half_view_w;
            const float hi = half_playable_w - half_view_w;
            cx = (cx < lo) ? lo : ((cx > hi) ? hi : cx);
        }
        cam_x_ = cx;
    }

    void render_location() {
        if (!location_) return;
        // Render ALL layers with parallax support.
        //
        // Coordinate system: params.xml uses the same coordinate system as
        // the player/enemy positions (Y-up, Y=0 near center). We render
        // images directly at their (img.x, img.y) positions.
        //
        // Parallax: layers with factor < 1 scroll slower than the camera.
        // parallax_shift = (1 - factor) * cam_x_ — shifts the layer's X
        // to create the illusion of depth.
        static bool loc_logged = false;
        for (auto& layer : location_->layers) {
            // Parallax: the layer's X position scrolls at `factor` of the camera speed.
            // factor=1.0 → layer moves with camera (foreground).
            // factor=0.5 → layer moves at half speed (appears further away).
            // factor=0.1 → layer barely moves (far background).
            // Implementation: shift the layer's X by -cam_x_ * (1 - factor).
            // When the camera moves right (cam_x_ increases), the layer shifts left
            // by (1-factor)*cam_x_, creating the parallax effect.
            // A layer at depth `factor` must appear to move at `factor` times
            // the camera's rate. Its screen position is
            //     (draw_world_x - cam_x) * zoom
            // and for that to change by -factor * dcam the layer has to be
            // drawn at
            //     draw_world_x = img.x + (1 - factor) * cam_x
            //
            // The sign used to be the other way round, which moved background
            // layers AWAY from the camera at (1-factor) times its speed
            // instead of towards it. On dojo that put the two 512-wide
            // background plates at world 88 and 599 while the camera was
            // looking at [-878, 118] — the background simply was not on
            // screen, which is why the location looked like it had no sky.
            float parallax_factor = layer.factor;
            if (parallax_factor <= 0.0f) parallax_factor = 1.0f;
            float parallax_shift = -(1.0f - parallax_factor) * cam_x_;

                        if (!loc_logged) {
                std::printf("[LOC] layer: type=%d factor=%.2f atlas=%s images=%zu\n",
                            layer.type, layer.factor, layer.atlas_name.c_str(),
                            layer.images.size());
            }

            for (auto& img : layer.images) {
                if (!loc_logged) {
                    std::printf("[LOC]   img: cls='%s' x=%.0f y=%.0f w=%.0f h=%.0f "
                                "parallax=%.1f color='%s'\n",
                                img.class_name.c_str(), img.x, img.y, img.w, img.h,
                                parallax_shift, img.color.c_str());
                }
                // [ORIGINAL] `pixel_1` is a single white texel that params.xml
                // stretches into a solid box and tints with `Color` — the
                // location's masking boxes. dojo has four of them, all in the
                // location colour 0x281409:
                //     X=-890 W=350 H=860   left of the set
                //     X= 890 W=350 H=860   right of it
                //     Y=-426 W=1960 H=400  above it
                //     Y= 470 W=1960 H=500  below it
                // They are what hides everything past the edge of the built
                // scenery, which is why the original never shows sky beyond
                // the walls.
                //
                // This drew them ONLY when the atlas was missing, and then
                // `continue`d unconditionally — so with the atlas present, i.e.
                // whenever the location loaded correctly, all four masks were
                // silently skipped. That is the "you can still see past the
                // edges of the location" on screen.
                if (img.class_name == "pixel_1" && !img.color.empty()) {
                    unsigned long col = std::stoul(img.color, nullptr, 16);
                    ren::Color4B c{
                        (std::uint8_t)((col>>16)&0xFF),
                        (std::uint8_t)((col>>8)&0xFF),
                        (std::uint8_t)(col&0xFF), 255};
                    const float hw = (float)platform_->window_width()  / (2.0f * zoom_);
                    const float hh = (float)platform_->window_height() / (2.0f * zoom_);
                    const float left = cam_x_ - hw, right = cam_x_ + hw;
                    const float bottom = cam_y_ - hh, top = cam_y_ + hh;
                    // params.xml uses Y-DOWN, the world is Y-UP: world_y = -img.y.
                    const float world_x = img.x - parallax_shift;
                    const float world_y = -img.y;
                    const float sx = (world_x - img.w/2.0f - left) / (right - left) * platform_->window_width();
                    const float sy = (1.0f - (world_y - img.h/2.0f - bottom) / (top - bottom)) * platform_->window_height();
                    const float ex = (world_x + img.w/2.0f - left) / (right - left) * platform_->window_width();
                    const float ey = (1.0f - (world_y + img.h/2.0f - bottom) / (top - bottom)) * platform_->window_height();
                    const float x = std::min(sx, ex), y = std::min(sy, ey);
                    const float bw = std::abs(ex - sx), bh = std::abs(ey - sy);
                    renderer_->draw_filled_rect_screen(x, y, bw, bh, c);
                    continue;
                }
                auto it = assets_->atlases().find(img.atlas_name);
                if (it == assets_->atlases().end()) {
                    // Atlas not found � render solid rect from location Color as fallback.
                    // Many locations lack atlas files; this prevents black screens.
                    // Atlas not found: render visible fallback so user can see layer structure.
                    // The location Color is often black, making it look like a parser bug.
                    // Use progressively lighter shades per image to show the layout.
                    {
                        float world_y = -img.y;
                        float world_x = img.x - parallax_shift;
                        float left = world_x - img.w / 2.0f;
                        float bottom = world_y - img.h / 2.0f;
                        float hw2 = (float)platform_->window_width() / (2.0f * zoom_);
                        float hh2 = (float)platform_->window_height() / (2.0f * zoom_);
                        float vis_left2 = cam_x_ - hw2, vis_right2 = cam_x_ + hw2;
                        float vis_bottom2 = cam_y_ - hh2, vis_top2 = cam_y_ + hh2;
                        if (left + img.w < vis_left2 || left > vis_right2 || bottom + img.h < vis_bottom2 || bottom > vis_top2) continue;
                        float sx = (left - vis_left2) / (vis_right2 - vis_left2) * platform_->window_width();
                        float sy = (1.0f - (bottom - vis_bottom2) / (vis_top2 - vis_bottom2)) * platform_->window_height();
                        float sw = img.w / (vis_right2 - vis_left2) * platform_->window_width();
                        float sh = img.h / (vis_top2 - vis_bottom2) * platform_->window_height();
                        // Use per-image Color from params.xml (<Image Color="RRGGBB" />) when available
                        uint8_t r = 100, g = 120, b = 160;
                        if (!img.color.empty()) {
                            unsigned long col = std::stoul(img.color, nullptr, 16);
                            r = (uint8_t)((col>>16)&0xFF);
                            g = (uint8_t)((col>>8)&0xFF);
                            b = (uint8_t)(col&0xFF);
                        } else if (location_ && !location_->color.empty()) {
                            unsigned long col = std::stoul(location_->color, nullptr, 16);
                            r = (uint8_t)((col>>16)&0xFF); if (r < 30) r = 60;
                            g = (uint8_t)((col>>8)&0xFF); if (g < 30) g = 80;
                            b = (uint8_t)(col&0xFF); if (b < 30) b = 100;
                        }
                        ren::Color4B c2{r, g, b, 200};
                        renderer_->draw_filled_rect_screen(sx, sy, sw, sh, c2);
                        // Draw a border to show individual image boundaries
                        ren::Color4B border{255, 255, 255, 60};
                        renderer_->draw_filled_rect_screen(sx, sy, sw, 2, border);
                        renderer_->draw_filled_rect_screen(sx, sy, 2, sh, border);
                        renderer_->draw_filled_rect_screen(sx + sw - 2, sy, 2, sh, border);
                        renderer_->draw_filled_rect_screen(sx, sy + sh - 2, sw, 2, border);
                    }
                    continue;
                }
                auto& atlas = it->second;
                if (!atlas.texture || !atlas.atlas) continue;
                auto fit = atlas.atlas->name_index.find(img.class_name + ".png");
                if (fit == atlas.atlas->name_index.end()) {
                    fit = atlas.atlas->name_index.find(img.class_name);
                    if (fit == atlas.atlas->name_index.end()) continue;
                }
                auto& frame = atlas.atlas->frames[fit->second];
                float img_off_x = (float)frame.offset_x;
                float img_off_y = (float)frame.offset_y;

                // [ORIGINAL] Atlas frames are TRIMMED. The plist records the
                // untrimmed size (`sourceSize`), the trimmed rectangle that is
                // actually stored (`frame`), and `offset` — how far the trimmed
                // rectangle's centre sits from the untrimmed centre, in
                // untrimmed pixels with Y pointing up. params.xml's
                // Width/Height are the size of the UNTRIMMED sprite.
                //
                //   dojo layer_3_1 (the floor):  sourceSize 256x64,
                //                                frame 256x60, offset {0,-2}
                //   dojo left:                   sourceSize  80x512,
                //                                frame  70x512, offset {-5,0}
                //
                // Drawing the trimmed pixels stretched over the untrimmed box
                // — which is what this did — makes every trimmed sprite both
                // the wrong size and in the wrong place: the floor planks came
                // out 4/64 too tall and the side walls 10/80 too wide.
                const float src_w = frame.source_size_w > 0
                                        ? (float)frame.source_size_w : img.w;
                const float src_h = frame.source_size_h > 0
                                        ? (float)frame.source_size_h : img.h;
                const float frame_scale_x = (src_w > 0.0f) ? img.w / src_w : 1.0f;
                const float frame_scale_y = (src_h > 0.0f) ? img.h / src_h : 1.0f;
                // pixel_1 is a 1x1 dot deliberately stretched to a big filler
                // rectangle by params.xml; leave that case alone.
                const bool stretch_dot = (frame.source_size_w <= 1 &&
                                          frame.source_size_h <= 1);
                const float quad_w_trim = stretch_dot
                        ? img.w : (float)frame.atlas_w * frame_scale_x;
                const float quad_h_trim = stretch_dot
                        ? img.h : (float)frame.atlas_h * frame_scale_y;

                // [ORIGINAL] A <SimpleEffect> carries its own alpha curve;
                // SimpleEffect::update @ 0x1007f1f0 applies it as
                // setOpacity((int)(value * 2.55) & 0xff). A plain <Image> has
                // no curve and stays fully opaque. In dojo this is the light
                // patch (layer_4), which breathes between 45% and 75% over a
                // 9.2 s loop — it used to be drawn flat.
                ren::Color4B tint{255, 255, 255, 255};
                if (!img.transparency.empty())
                    tint.a = resf2::format::transparency_to_alpha(img.transparency.value());

                // For rotated frames, use pre-cropped un-rotated texture
                std::string crop_name = img.class_name;
                if (atlas.cropped.count(crop_name)) {
                    // Use pre-cropped texture (already un-rotated)
                    auto& ctex = atlas.cropped[crop_name];
                    // params Y points down and the world is Y-up, hence -img.y;
                    // the plist offset is already Y-up, so it adds directly.
                    float world_y = -img.y + img_off_y * frame_scale_y;
                    float world_x = img.x + img_off_x * frame_scale_x - parallax_shift;
                    float quad_w = quad_w_trim;
                    float quad_h = quad_h_trim;
                    float px = world_x - quad_w / 2.0f;
                    float py = world_y - quad_h / 2.0f;
                    if (!loc_logged)
                        std::printf("[LOC]     -> ROTATED world x %.0f..%.0f y %.0f..%.0f "
                                    "(src %dx%d frame %dx%d tex %dx%d off %d,%d)\n",
                                    px, px + quad_w, py, py + quad_h,
                                    frame.source_size_w, frame.source_size_h,
                                    frame.atlas_w, frame.atlas_h,
                                    ctex->width(), ctex->height(),
                                    frame.offset_x, frame.offset_y);
                    renderer_->draw_textured_quad(*ctex, px, py,
                                                  quad_w, quad_h,
                                                  0.0f, 0.0f, 1.0f, 1.0f, tint);
                    continue;
                }
                
                // Non-rotated frame: use atlas texture with UV mapping
                float tw = (float)atlas.atlas->metadata.texture_w;
                float th = (float)atlas.atlas->metadata.texture_h;
                float u0 = frame.atlas_x / tw;
                float v0 = frame.atlas_y / th;
                float u1 = (frame.atlas_x + frame.atlas_w) / tw;
                float v1 = (frame.atlas_y + frame.atlas_h) / th;
                // Same trimmed-frame placement as the rotated path above. The
                // non-rotated path used to ignore the Y offset entirely, so
                // the two paths disagreed about where a trimmed sprite goes.
                float world_y = -img.y + img_off_y * frame_scale_y;
                float world_x = img.x + img_off_x * frame_scale_x - parallax_shift;
                float quad_w = quad_w_trim;
                float quad_h = quad_h_trim;
                float px = world_x - quad_w / 2.0f;
                float py = world_y - quad_h / 2.0f;
                if (!loc_logged && debug_world_)
                    std::printf("[LOC]     -> world x %.0f..%.0f  y %.0f..%.0f  "
                                "(src %dx%d frame %dx%d off %d,%d)\n",
                                px, px + quad_w, py, py + quad_h,
                                frame.source_size_w, frame.source_size_h,
                                frame.atlas_w, frame.atlas_h,
                                frame.offset_x, frame.offset_y);
                renderer_->draw_textured_quad(*atlas.texture, px, py, quad_w, quad_h,
                                              u0, v0, u1, v1, tint);
            }
        }
        loc_logged = true;
    }

    // [ORIGINAL] Advance every <SimpleEffect> curve in the loaded location.
    // The original runs this from SimpleEffect::update @ 0x1007f1f0 on each
    // effect node; here the effects live on the layer images, so one sweep
    // does them all. Only Transparency is modelled — OscillationX/Y and
    // Rotation use the same curve type but move the node, which needs the
    // effect to be a node in the first place (PORT_PLAN 2.2).
    void update_location_effects(float dt_seconds) {
        if (!location_) return;
        for (auto& layer : location_->layers)
            for (auto& img : layer.images)
                if (!img.transparency.empty()) img.transparency.advance(dt_seconds);
    }

    // Alpha of the first animated <SimpleEffect> in the location, or -1 when
    // the location has none. Reported in the [STATE] dump so a test can see
    // that the curve is actually being ticked by the engine — the unit test
    // proves the maths, this proves the wiring. In dojo it is the light patch,
    // which is off-camera at the start position, so nothing on screen would
    // reveal a curve that never advances.
    float first_effect_alpha() const {
        if (!location_) return -1.0f;
        for (const auto& layer : location_->layers)
            for (const auto& img : layer.images)
                if (!img.transparency.empty()) return img.transparency.value();
        return -1.0f;
    }

    // ---------- Skeleton ----------
    void load_skeleton() {
        assets_->load_skeleton(asset_root_, current_location_name_);
    }

    // ---------- Body model (body.xml) ----------
    void load_body_model() {
        auto candidates = model_paths(asset_root_, "body.xml");
        std::string path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { path = p.string(); break; }
        }
        if (path.empty()) { std::printf("  body.xml NOT FOUND!\n"); return; }
        auto xml = read_text(path);
        assets_->body_model() = std::make_unique<BodyModel>();
        assets_->parse_body_model_xml(xml, assets_->body_model().get(), "BODY-");
        // [ORIGINAL] Also load head.xml as part of the body model.
        // head.xml has HEAD- prefixed nodes/macronodes + BODY- prefixed edges
        // + HEAD-Triangle triangles. It references skeleton nodes NTop, NHeadF,
        // NHeadS_1, NHeadS_2 via MacroNodes. Without this, the character has
        // a capsule instead of a proper head model.
        auto head_candidates = model_paths(asset_root_, "head.xml");
        std::string head_path;
        for (const auto& p : head_candidates) {
            if (std::filesystem::exists(p)) { head_path = p.string(); break; }
        }
        if (!head_path.empty()) {
            auto head_xml = read_text(head_path);
            assets_->parse_body_model_xml(head_xml, assets_->body_model().get(), "HEAD-");
            std::printf("  head.xml loaded (merged into body model)\n");
        }
    }

    // [ORIGINAL] Parse a body/head model XML file into a BodyModel.
    // tag_prefix is "BODY-" for body.xml, "HEAD-" for head.xml.
    // Edges use "BODY-Edge" in both files; capsules use "Capsule_"; triangles
    // use "Type=\"Triangle\"" (prefix-agnostic). Nodes/MacroNodes use the prefix.
    void parse_body_model_xml(const std::string& xml, BodyModel* model, const std::string& tag_prefix) {
        fmt::XmlDocument doc;
        if (!doc.parse(xml)) {
            std::fprintf(stderr, "[body] xml_doc parse error: %s\n", doc.error().c_str());
            return;
        }
        auto* scene = doc.root()->first_child("Scene");
        if (!scene) { std::printf("  body/head.xml: no <Scene>\n"); return; }

        if (auto* ns = scene->first_child("Nodes")) {
            for (const auto& child : ns->children) {
                std::string type = child.attr("Type");
                if (type == "Node") {
                    BodyNode n;
                    n.name = child.name;
                    n.x = tof(child.attr("X"));
                    n.y = tof(child.attr("Y"));
                    n.z = tof(child.attr("Z"));
                    model->nodes[n.name] = n;
                } else if (type == "MacroNode") {
                    BodyMacroNode mn;
                    mn.name = child.name;
                    mn.children[0] = child.attr("ChildNode1");
                    mn.children[1] = child.attr("ChildNode2");
                    mn.children[2] = child.attr("ChildNode3");
                    mn.children[3] = child.attr("ChildNode4");
                    mn.lcc[0] = tof(child.attr("LCC1"));
                    mn.lcc[1] = tof(child.attr("LCC2"));
                    mn.lcc[2] = tof(child.attr("LCC3"));
                    mn.lcc[3] = tof(child.attr("LCC4"));
                    model->macro_nodes[mn.name] = mn;
                }
            }
        }

        if (auto* es = scene->first_child("Edges")) {
            for (const auto& child : es->children) {
                if (child.attr("Type") != "Edge") continue;
                BodyEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                e.collisible = (child.attr("Collisible") == "1");
                model->edges.push_back(e);
            }
        }

        if (auto* fs = scene->first_child("Figures")) {
            for (const auto& child : fs->children) {
                std::string type = child.attr("Type");
                if (type == "Capsule") {
                    BodyCapsule c;
                    c.edge_name = child.attr("Edge");
                    c.radius1 = tof(child.attr("Radius1"));
                    c.radius2 = tof(child.attr("Radius2"));
                    c.margin1 = tof(child.attr("Margin1"));
                    c.margin2 = tof(child.attr("Margin2"));
                    model->capsules.push_back(c);
                } else if (type == "Triangle") {
                    BodyTriangle t;
                    t.n1 = child.attr("Node1");
                    t.n2 = child.attr("Node2");
                    t.n3 = child.attr("Node3");
                    model->triangles.push_back(t);
                }
            }
        }

        std::printf("  [%s] model: %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                    tag_prefix.c_str(), model->nodes.size(), model->edges.size(),
                    model->capsules.size(), model->triangles.size());
    }

    // Resolve a node name to world coordinates (handles BodyNode, SkelNode, MacroNode).
    // Resolve a node name to world coordinates (handles BodyNode, SkelNode, MacroNode).
    // [M5] The X mirror uses player_turn_blend_ (which eases toward the
    // facing sign over ~8 frames) instead of the raw face_right sign, so a
    // turn renders as a short rotation through edge-on instead of a one-frame
    // snap. Outside a turn the blend sits at exactly ±1, so nothing changes.
    std::pair<float, float> resolve_body_node(const std::string& name,
                                              float world_cx, float world_cy,
                                              bool face_right, float pivot_local_y) {
        (void)face_right;
        if (!assets_->body_model()) return {world_cx, world_cy};

        // Check if this node has an animated position (from .bin animation)
        auto ait = anim_node_pos_.find(name);
        if (ait != anim_node_pos_.end()) {
            // [ORIGINAL] .bin animations are authored in the location's own
            // vertical space, with the floor at y = 0. Vertical placement is
            // therefore taken straight from the animation and NOT from
            // player_pos_y / y_adjust:
            //
            //     world_y = floor_world_y + absolute_animation_y
            //
            // AnimationPlayer stores {ix - npivot_x, iy - npivot_y}, so the
            // absolute Y is recovered as ly + anim_npivot_bin_y(). X stays
            // pivot-relative so the fighter follows player_pos_x.
            //
            // Verified over every frame of four animations (floor_world_y_ =
            // -Height/2 + Floor = -200 on dojo), lowest foot node:
            //     fists1_stance_idle  -201.3 .. -201.2   (flat, 38 frames)
            //     stance_idle         -199.2 .. -199.1   (flat, 79 frames)
            //     stance_2            -200.6 .. -194.0   (lunge)
            //     jump                -202.5 ..   -4.0   (leaves the ground)
            // The jump arc falls out for free — it is in the animation data.
            //
            // The previous code did `world_cy + (ly - pivot_local_y)`, i.e. it
            // subtracted the rest pivot (169.48) from an already pivot-relative
            // value, and then tried to patch it with y_adjust (clamped to +-50).
            // That is what put the fighter under the floor. For the same four
            // animations it produced -102.8, -134.3, -208.7 and -218.7 — the
            // idle poses floated 66-100 units above the floor and every
            // animation landed at a different height.
            float lx = ait->second.first, ly = ait->second.second;
            float sx = lx * player_turn_blend_;
            // [STEP 4.7] Apply gameplay Y offset on top of animation data.
            // The animation sets the base pose; gameplay_y_offset_ adds an
            // additional world-Y shift for knockback/knockdown effects.
            float sy = floor_world_y_ + (ly + anim_player_.anim_npivot_bin_y()) + gameplay_y_offset_;
            return {world_cx + sx, sy};
        }

        auto bit = assets_->body_model()->nodes.find(name);
        if (bit != assets_->body_model()->nodes.end()) {
            float lx = bit->second.x, ly = bit->second.y;
            float sx = lx * player_turn_blend_;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }
        auto sit = assets_->skeleton_nodes().find(name);
        if (sit != assets_->skeleton_nodes().end()) {
            float lx = sit->second.x, ly = sit->second.y;
            float sx = lx * player_turn_blend_;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }
        auto mit = assets_->body_model()->macro_nodes.find(name);
        if (mit != assets_->body_model()->macro_nodes.end()) {
            float sum_lcc = 0, wx = 0, wy = 0;
            for (int i = 0; i < 4; ++i) {
                if (mit->second.children[i].empty()) continue;
                auto [cx, cy] = resolve_body_node(mit->second.children[i],
                                                  world_cx, world_cy, face_right, pivot_local_y);
                wx += cx * mit->second.lcc[i];
                wy += cy * mit->second.lcc[i];
                sum_lcc += mit->second.lcc[i];
            }
            if (std::abs(sum_lcc) > 1e-6f)
                return {wx / sum_lcc, wy / sum_lcc};
        }
        return {world_cx, world_cy};
    }

    // [R1] Resolve a weapon-model triangle vertex to world space. Weapon
    // figures reference the model's OWN MacroNodes (weapon_knives.xml ships
    // ZERO plain nodes); a MacroNode's position is the LCC-weighted sum of
    // its Weapon-Node* children, which live in the SKELETON — skeleton.xml
    // pins Weapon-Node2_1 to NWrist_1 with the zero-length Edge129 (the
    // dojo placement law, LIVE_GAME_EVIDENCE Q1/Q2: the weapon rides the
    // skeleton's weapon nodes at the hand). The old render looked the
    // vertices up in the plain node map (always empty for knives) and drew
    // the authored rest coords at a hand-fitted offset — the re-soak-3
    // "оружие не отображается / далеко от персонажа" report.
    // `use_anim` selects the animated world transform (dojo/battle render —
    // the .bin animations animate the Weapon-Node* pins, so the knife
    // tracks the hand); the shop preview (rest pose) passes false.
    bool resolve_player_weapon_vertex(const resf2::game::BodyModel& wm,
                                      const std::string& name,
                                      float world_cx, float world_cy, float dir,
                                      float pivot_local_y, bool use_anim,
                                      float& wx, float& wy) const {
        auto mit = wm.macro_nodes.find(name);
        if (mit != wm.macro_nodes.end()) {
            float sum = 0.0f, ax = 0.0f, ay = 0.0f;
            for (int i = 0; i < 4; ++i) {
                if (mit->second.children[i].empty()) continue;
                float cx, cy;
                if (!resolve_player_weapon_vertex(wm, mit->second.children[i],
                        world_cx, world_cy, dir, pivot_local_y, use_anim,
                        cx, cy))
                    continue;
                ax += cx * mit->second.lcc[i];
                ay += cy * mit->second.lcc[i];
                sum += mit->second.lcc[i];
            }
            if (std::fabs(sum) > 1e-6f) { wx = ax / sum; wy = ay / sum; return true; }
            return false;
        }
        auto nit = wm.nodes.find(name);
        if (nit != wm.nodes.end()) {
            // Authored model-space vertex (plain nodes only — macro names
            // never reach here): weapon-local, rest-pose transform.
            wx = world_cx + dir * nit->second.x;
            wy = world_cy + nit->second.y - pivot_local_y;
            return true;
        }
        if (use_anim) {
            auto ait = anim_node_pos_.find(name);
            if (ait != anim_node_pos_.end()) {
                wx = world_cx + ait->second.first * dir;
                wy = floor_world_y_ + (ait->second.second +
                     anim_player_.anim_npivot_bin_y()) + gameplay_y_offset_;
                return true;
            }
            // [Wave 9B] S4: the walk/stance animations do NOT animate the
            // Weapon-Node* pins (step_forward.bin ships no Weapon-Node
            // entries), so an un-animated pin used to fall back to the
            // skeleton REST pose while the body drew the animated pose —
            // the shop preview showed the knife hanging above the hands on
            // a frozen step pose (re-soak-5). The skeleton PINS the weapon
            // nodes to the wrists (Edge129: Weapon-Node2_1 <-> NWrist_1,
            // Edge130: Weapon-Node2_2 <-> NWrist_2, zero length — the dojo
            // placement law), so an un-animated pin anchors to its ANIMATED
            // body node instead of rest.
            for (const auto& [edge_name, e] : assets_->skeleton_edges()) {
                (void)edge_name;
                const bool pin_is_end1 = (e.end1 == name);
                if (!pin_is_end1 && e.end2 != name) continue;
                const std::string& other = pin_is_end1 ? e.end2 : e.end1;
                if (other.rfind("Weapon-", 0) == 0) continue;  // weapon-to-weapon edges are not pins
                auto oit = anim_node_pos_.find(other);
                if (oit == anim_node_pos_.end()) continue;      // body node not animated either
                wx = world_cx + oit->second.first * dir;
                wy = floor_world_y_ + (oit->second.second +
                     anim_player_.anim_npivot_bin_y()) + gameplay_y_offset_;
                return true;
            }
        }
        auto sit = assets_->skeleton_nodes().find(name);
        if (sit != assets_->skeleton_nodes().end()) {
            wx = world_cx + dir * sit->second.x;
            wy = world_cy + (sit->second.y - pivot_local_y);
            return true;
        }
        return false;
    }

    // ---------- Debug world overlay (F1 / --debug-world) ----------
    //
    // Draws the params.xml-derived geometry in world space so that every
    // world<->screen claim can be read off the screen as a number instead of
    // being eyeballed. Without this, tuning the camera or the fighter's
    // transform is guesswork: a 20-pixel misreading of a screenshot is ~20
    // world units, which is the same order as the discrepancies being chased.
    //
    // Coordinate model being asserted here (see PORT_PLAN.md 1.1):
    //   * world origin = centre of the location box
    //   * <Image> X is centred on the origin, Y is inverted: world_y = -Y
    //   * <ModelsViewer> X is measured from the LEFT edge:
    //         world_x = X - Width/2                      [ORIGINAL, verified]
    //     and Y is used directly (Y-up), NOT inverted
    //   * the box is Width x Height  (Location::load, .s86 FUN_10144420:
    //     +0x38 Width, +0x3c Height, +0x34 Wall, +0x2c Floor)
    void render_debug_world(plat::Platform& platform) {
        if (!debug_world_ || !location_ || !renderer_) return;

        const float vw = static_cast<float>(platform.window_width());
        const float vh = static_cast<float>(platform.window_height());
        const float hw = vw / (2.0f * zoom_);
        const float hh = vh / (2.0f * zoom_);
        const float left = cam_x_ - hw, bottom = cam_y_ - hh;
        auto sx = [&](float wx) { return (wx - left) * zoom_; };
        auto sy = [&](float wy) { return vh - (wy - bottom) * zoom_; };

        const float half_w = location_->width * 0.5f;
        const float half_h = location_->height * 0.5f;
        const ren::Color4B c_box{0, 220, 255, 255};
        const ren::Color4B c_axis{130, 130, 130, 255};
        const ren::Color4B c_mask{255, 0, 220, 255};
        const ren::Color4B c_floor{80, 255, 80, 255};
        const ren::Color4B c_actor{255, 210, 0, 255};

        // World box (Width x Height) and axes.
        renderer_->draw_line_world(-half_w, half_h, half_w, half_h, c_box);
        renderer_->draw_line_world(-half_w, -half_h, half_w, -half_h, c_box);
        renderer_->draw_line_world(-half_w, -half_h, -half_w, half_h, c_box);
        renderer_->draw_line_world(half_w, -half_h, half_w, half_h, c_box);
        renderer_->draw_line_world(-half_w, 0.0f, half_w, 0.0f, c_axis);
        renderer_->draw_line_world(0.0f, -half_h, 0.0f, half_h, c_axis);

        // Horizontal rulers every 50 world units, labelled every 100.
        for (int wy = -static_cast<int>(half_h); wy <= static_cast<int>(half_h); wy += 50) {
            const float y = static_cast<float>(wy);
            const bool major = (wy % 100) == 0;
            renderer_->draw_line_world(left, y, left + (major ? 26.0f : 13.0f), y, c_axis);
            if (major) {
                char b[24];
                std::snprintf(b, sizeof(b), "%d", wy);
                render_text(b, 30.0f, sy(y) - 8.0f, 0.20f, {150, 150, 150, 255});
            }
        }

        // Mask rectangles (the only images carrying a Color) — these are what
        // the location uses to cover everything outside the intended frame.
        float mask_top = half_h, mask_bottom = -half_h;
        for (const auto& layer : location_->layers) {
            for (const auto& img : layer.images) {
                if (img.color.empty()) continue;
                if (img.w < location_->width * 0.9f) continue;
                const float cy = -img.y;
                const float t = cy + img.h * 0.5f, b = cy - img.h * 0.5f;
                if (b > 0.0f && b < mask_top) mask_top = b;
                if (t < 0.0f && t > mask_bottom) mask_bottom = t;
            }
        }
        renderer_->draw_line_world(-half_w, mask_top, half_w, mask_top, c_mask);
        renderer_->draw_line_world(-half_w, mask_bottom, half_w, mask_bottom, c_mask);

        // Floor plane candidates.
        //   A: -Height/2 + Floor          (params Floor read as a bottom margin)
        //   B: top edge of the layer_3 strip, i.e. the drawn floor
        const float floor_a = -half_h + location_->floor;
        renderer_->draw_line_world(-half_w, floor_a, half_w, floor_a, c_floor);
        float floor_b = floor_a;
        bool have_b = false;
        for (const auto& layer : location_->layers) {
            for (const auto& img : layer.images) {
                if (img.class_name.rfind("layer_3", 0) != 0) continue;
                const float t = -img.y + img.h * 0.5f;
                if (!have_b || t > floor_b) { floor_b = t; have_b = true; }
            }
        }
        if (have_b)
            renderer_->draw_line_world(-half_w, floor_b, half_w, floor_b, {255, 140, 0, 255});

        // Fighter markers: declared pivot vs. actually rendered lowest node.
        auto pivot_it = assets_->skeleton_nodes().find("NPivot");
        const float pivot_local_y = pivot_it != assets_->skeleton_nodes().end()
                                        ? pivot_it->second.y : stance_npivot_y_;
        const float world_cx = player_pos_x_;
        const float world_cy = player_pos_y_ + y_adjust_smoothed_;
        auto cross = [&](float x, float y, ren::Color4B col) {
            renderer_->draw_line_world(x - 14.0f, y, x + 14.0f, y, col);
            renderer_->draw_line_world(x, y - 14.0f, x, y + 14.0f, col);
        };
        cross(world_cx, world_cy, c_actor);

        float lowest = world_cy;
        bool have_low = false;
        for (const char* n : {"NToe_1", "NToe_2", "NHeel_1", "NHeel_2",
                              "NAnkle_1", "NAnkle_2"}) {
            auto [nx, ny] = resolve_body_node(n, world_cx, world_cy,
                                              facing_right_, pivot_local_y);
            (void)nx;
            if (!have_low || ny < lowest) { lowest = ny; have_low = true; }
        }
        if (have_low) {
            renderer_->draw_line_world(world_cx - 60.0f, lowest,
                                       world_cx + 60.0f, lowest, {255, 60, 60, 255});
        }
        cross(enemy_pos_x_, enemy_pos_y_, {255, 120, 255, 255});

        // Punching bag: collisible edges plus its overall extent. The bag is
        // what an attack has to reach, so its world box has to be readable
        // next to the fighter's.
        float bag_x0 = 0, bag_x1 = 0, bag_y0 = 0, bag_y1 = 0;
        bool have_bag = false;
        int collisible_edges = 0;
        if (assets_->bag_model()) {
            for (const auto& [n, v] : bag_verlet_) {
                (void)n;
                if (!have_bag) { bag_x0 = bag_x1 = v.x; bag_y0 = bag_y1 = v.y; have_bag = true; }
                bag_x0 = std::min(bag_x0, v.x); bag_x1 = std::max(bag_x1, v.x);
                bag_y0 = std::min(bag_y0, v.y); bag_y1 = std::max(bag_y1, v.y);
            }
            for (const auto& be : assets_->bag_model()->edges) {
                if (!be.collisible || be.radius <= 0) continue;
                auto a = bag_verlet_.find(be.end1);
                auto b = bag_verlet_.find(be.end2);
                if (a == bag_verlet_.end() || b == bag_verlet_.end()) continue;
                ++collisible_edges;
                renderer_->draw_line_world(a->second.x, a->second.y,
                                           b->second.x, b->second.y, {0, 255, 120, 255});
            }
        }

        // The attacking limb of the current move, in the same transform the
        // hit test uses.
        float fist_x = 0, fist_y = 0;
        bool have_fist = false;
        auto move_it = assets_->moves().find(current_move_);
        if (move_it != assets_->moves().end()) {
            for (const auto& edge_name : move_it->second.attack_edges) {
                auto se = assets_->skeleton_edges().find(edge_name);
                if (se == assets_->skeleton_edges().end()) continue;
                for (const std::string& nn : {se->second.end1, se->second.end2}) {
                    if (nn.empty() || !anim_node_pos_.count(nn)) continue;
                    auto [wx, wy] = resolve_body_node(nn, world_cx, world_cy,
                                                      facing_right_, pivot_local_y);
                    cross(wx, wy, {255, 255, 0, 255});
                    fist_x = wx; fist_y = wy; have_fist = true;
                }
            }
        }

        // Numeric readout — the whole point of the overlay.
        char b[256];
        float ty = 90.0f;
        auto line = [&](const char* fmt, auto... a) {
            std::snprintf(b, sizeof(b), fmt, a...);
            render_text(b, 30.0f, ty, 0.22f, {255, 255, 255, 255});
            ty += 22.0f;
        };
        line("params  Width=%.0f Height=%.0f Wall=%.0f Floor=%.0f",
             location_->width, location_->height, location_->wall, location_->floor);
        line("view    zoom=%.4f  visible %.0f x %.0f world units",
             zoom_, vw / zoom_, vh / zoom_);
        line("camera  x=%.1f y=%.1f", cam_x_, cam_y_);
        line("mask band  top=%.0f bottom=%.0f  height=%.0f",
             mask_top, mask_bottom, mask_top - mask_bottom);
        line("floor A (-H/2+Floor)=%.0f   B (layer_3 top)=%.0f  delta=%.0f",
             floor_a, floor_b, floor_b - floor_a);
        line("player  pivot=(%.0f, %.0f) y_adjust=%.1f  lowest node=%.0f",
             world_cx, world_cy, y_adjust_smoothed_, lowest);
        line("player  foot-vs-floorA=%.0f  foot-vs-floorB=%.0f",
             lowest - floor_a, lowest - floor_b);
        line("enemy   pos=(%.0f, %.0f)   params X=%.0f -> X-W/2=%.0f",
             enemy_pos_x_, enemy_pos_y_, location_->enemy_x,
             location_->enemy_x - half_w);
        if (have_bag)
            line("bag     x=%.0f..%.0f y=%.0f..%.0f  collisible edges=%d",
                 bag_x0, bag_x1, bag_y0, bag_y1, collisible_edges);
        else
            line("bag     NOT PLACED (no verlet nodes)");
        if (have_fist)
            line("limb    last attacking node at (%.0f, %.0f)", fist_x, fist_y);
        line("anim    '%s' prio=%d finished=%d   move='%s' state=%d",
             current_anim_.c_str(), anim_player_.anim_priority(),
             anim_player_.anim_finished() ? 1 : 0,
             current_move_.c_str(), move_state_);

        // [ORIGINAL] Enemy AI roulette trace — the tacticSettings.xml weights
        // that drove the last decision (tactic_settings.hpp / jL / iCa). Shows
        // the evaluated weight per candidate and which one the wheel picked, so
        // the pick can be read as numbers instead of guessed from behaviour.
        if (tactics_.loaded()) {
            // [E2] The state name comes from the stored decision (ADR-005
            // Phase B): the legacy enemy_ai_state_ int no longer drives
            // execution or this overlay (deleted in Phase E).
            line("AI      tactic=Standard  dist=%.0f  state=%s  pick=%s",
                 ai_last_distance_, ai_display_state(), ai_last_pick_.c_str());
            for (size_t i = 0; i < ai_last_candidates_.size(); ++i) {
                const bool chosen = (ai_last_candidates_[i] == ai_last_pick_);
                std::snprintf(b, sizeof(b), "  %c %-12s w=%.1f",
                              chosen ? '>' : ' ',
                              ai_last_candidates_[i].c_str(),
                              (i < ai_last_weights_.size()) ? ai_last_weights_[i] : 0.0f);
                render_text(b, 30.0f, ty, 0.20f,
                            chosen ? ren::Color4B{120, 255, 120, 255}
                                   : ren::Color4B{200, 200, 200, 255});
                ty += 20.0f;
            }
        } else {
            line("AI      tacticSettings.xml NOT loaded");
        }
        (void)sx;
    }

    // ---------------------------------------------------------------------------
    // F1 Debug Overlay — comprehensive game state panels rendered on screen.
    // Called from host_render_scene() when debug_world_ is true.
    // ---------------------------------------------------------------------------
    void render_debug_overlay(plat::Platform& platform) {
        if (!debug_world_ || !renderer_) return;

        const float vw = static_cast<float>(platform.window_width());
        const float vh = static_cast<float>(platform.window_height());
        const float col_w = vw * 0.24f;
        const float panel_h = 110.0f;
        const float top_bar_h = panel_h + 28.0f;
        const float bottom_bar_h = 50.0f;
        const float line_h = 15.0f;
        const float pad = 6.0f;
        const float header_scale = 0.24f;
        const float body_scale = 0.20f;

        // Semi-transparent background panels
        const ren::Color4B bg{0, 0, 0, 180};
        const ren::Color4B border{80, 80, 80, 200};
        renderer_->draw_filled_rect_screen(0, 0, vw, top_bar_h, bg);
        renderer_->draw_filled_rect_screen(0, vh - bottom_bar_h, vw, bottom_bar_h, bg);
        // Column dividers
        for (int i = 1; i < 4; ++i) {
            float dx = col_w * i;
            renderer_->draw_line_screen(dx, 0, dx, top_bar_h, border);
        }
        // Separator between top and bottom bars
        renderer_->draw_line_screen(0, top_bar_h, vw, top_bar_h, border);
        renderer_->draw_line_screen(0, vh - bottom_bar_h, vw, vh - bottom_bar_h, border);

        // Title bar
        render_text("F1 DEBUG OVERLAY", pad, pad, header_scale,
                    {255, 220, 100, 255});
        // Version tag (top-right)
        {
            const char* ver = "reSF2 v0.0.3";
            float tw = ver_length(ver, header_scale);
            render_text(ver, vw - tw - pad, pad, header_scale,
                        {180, 180, 180, 255});
        }

        // --- Panel 1: Input (top-left) ---
        {
            float x = pad, y = pad + line_h + 4;
            render_text("INPUT", x, y, header_scale, {100, 200, 255, 255});
            y += line_h + 2;
            char b[128];
            std::snprintf(b, sizeof(b), "fwd:%d back:%d up:%d down:%d",
                          dbg_key_forward_ ? 1 : 0, dbg_key_back_ ? 1 : 0,
                          dbg_key_up_ ? 1 : 0, dbg_key_down_ ? 1 : 0);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "punch:%d kick:%d",
                          dbg_punch_pressed_ ? 1 : 0, dbg_kick_pressed_ ? 1 : 0);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "move_st:%d facing:%s blk:%d",
                           move_state_, facing_right_ ? "R" : "L",
                           player_fighter_.is_blocking ? 1 : 0);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "step_f:%u/%u",
                          input_handler_.step_frames(),
                          InputHandler::kMinStepFrames);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
        }

        // --- Panel 2: Movement (top-center-left) ---
        {
            float x = col_w + pad, y = pad + line_h + 4;
            render_text("MOVEMENT", x, y, header_scale, {100, 255, 150, 255});
            y += line_h + 2;
            char b[128];
            std::snprintf(b, sizeof(b), "pos: %.0f, %.0f",
                          player_pos_x_, player_pos_y_);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "anim: '%s'", current_anim_.c_str());
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "root_dx: %.2f  y_adj: %.1f",
                          anim_root_dx_, y_adjust_smoothed_);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "speed: 150  prio: %d",
                          anim_player_.anim_priority());
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
        }

        // --- Panel 3: Combat (top-center-right) ---
        {
            float x = col_w * 2 + pad, y = pad + line_h + 4;
            render_text("COMBAT", x, y, header_scale, {255, 100, 100, 255});
            y += line_h + 2;
            char b[128];
            // [E2] State name derived from the stored decision (ADR-005
            // Phase B) — the legacy enemy_ai_state_ int is bypassed.
            std::snprintf(b, sizeof(b), "AI: %s (d:%.0f)",
                          ai_display_state(), ai_last_distance_);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "HP: %.0f/%.0f  eHP: %.0f/%.0f",
                          player_fighter_.health, player_fighter_.max_health,
                          enemy_fighter_.health, enemy_fighter_.max_health);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "combo: %d (t:%.2f) pick: %s",
                          player_fighter_.hits_landed, combo_timer_,
                          ai_last_pick_.c_str());
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "weapon: %s", equipped_weapon_.c_str());
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            // Damage formula breakdown (binary: Model::getTotalDamage @ 0x10159a6c)
            // rawDmg = base × attr × block × attack × crit × 2.0
            // finalDmg = factorSet × rawDmg
            std::snprintf(b, sizeof(b), "DMG[%s]: base=%.2f",
                          dbg_last_move_name_.empty() ? "-" : dbg_last_move_name_.c_str(),
                          dbg_last_base_damage_);
            render_text(b, x, y, body_scale, {255, 220, 150, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "  attr=%.2f blk=%.2f atk=%.2f crit=%.2f",
                          dbg_last_attr_mult_, dbg_last_block_factor_,
                          dbg_last_attack_factor_, dbg_last_crit_factor_);
            render_text(b, x, y, body_scale, {255, 220, 150, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "  fset=%.2f => final=%.2f",
                          dbg_last_factor_set_, dbg_last_final_damage_);
            render_text(b, x, y, body_scale, {255, 180, 100, 255});
        }

        // --- Panel 4: Game State (top-right) ---
        {
            float x = col_w * 3 + pad, y = pad + line_h + 4;
            render_text("STATE", x, y, header_scale, {150, 255, 100, 255});
            y += line_h + 2;
            char b[128];
            std::snprintf(b, sizeof(b), "scene: %s",
                          scene::scene_name(scene_manager_.current_id()));
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "level: %s",
                          current_level_.empty() ? "(none)" : current_level_.c_str());
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "gold: %d  lvl: %d  gems: %d",
                          hud_gold_, hud_level_, hud_gems_);
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            std::snprintf(b, sizeof(b), "completed: %zu  battle: %s",
                          completed_levels_.size(),
                          is_battle_mode_ ? "yes" : "no");
            render_text(b, x, y, body_scale, {255, 255, 255, 255});
            y += line_h;
            // Tutorial state (binary: 0x1027d6c0 state machine)
            std::snprintf(b, sizeof(b), "tutorial: %s",
                          tutorial_state_.c_str());
            render_text(b, x, y, body_scale, {255, 200, 100, 255});
            y += line_h;
            // Zone/battle unlock state (binary: DisplayZone 0x100a1c00)
            int zones_unlocked = 0, battles_unlocked = 0;
            for (const auto& [k, v] : zone_unlocked_) if (v) zones_unlocked++;
            for (const auto& [k, v] : battle_unlocked_) if (v) battles_unlocked++;
            std::snprintf(b, sizeof(b), "zones: %zu/%d  battles: %zu/%d",
                          zone_unlocked_.size(), zones_unlocked,
                          battle_unlocked_.size(), battles_unlocked);
            render_text(b, x, y, body_scale, {200, 200, 255, 255});
        }

        // --- Bottom bar: Animation & Move info ---
        {
            float x = pad, y = vh - bottom_bar_h + pad;
            char b[256];
            std::snprintf(b, sizeof(b),
                "ANIM: '%s' finished=%d prio=%d fps=%.0f time=%.2f root_dx=%.2f",
                current_anim_.c_str(),
                anim_player_.anim_finished() ? 1 : 0,
                anim_player_.anim_priority(),
                anim_fps_, anim_time_, anim_root_dx_);
            render_text(b, x, y, body_scale, {200, 200, 255, 200});
            y += line_h;
            std::snprintf(b, sizeof(b),
                "MOVE: '%s' state=%d hit_anim=%u step_cd=%u",
                current_move_.c_str(), move_state_, hit_anim_, step_cooldown_);
            render_text(b, x, y, body_scale, {200, 255, 200, 200});
            y += line_h;
            // Tactic weights summary
            if (!ai_last_candidates_.empty()) {
                std::string ts = "TACTICS: ";
                for (size_t i = 0; i < ai_last_candidates_.size() && i < 5; ++i) {
                    if (i > 0) ts += "  ";
                    const bool chosen = (ai_last_candidates_[i] == ai_last_pick_);
                    char wbuf[64];
                    std::snprintf(wbuf, sizeof(wbuf), "%s%.1f",
                                  chosen ? ">" : "",
                                  (i < ai_last_weights_.size()) ? ai_last_weights_[i] : 0.0f);
                    ts += ai_last_candidates_[i] + "(" + wbuf + ")";
                }
                render_text(ts, x, y, body_scale, {255, 240, 150, 180});
            }
        }
    }

    // Estimate text width for right-aligned text using existing measure_text
    float ver_length(const char* text, float scale) {
        return measure_text(std::string(text), scale).first;
    }

    // Render body model as capsule lines (GL renderer uses thin lines for now).
    void render_body_model() {
        if (!assets_->body_model()) return;
        auto pivot_it = assets_->skeleton_nodes().find("NPivot");
        float pivot_local_y = pivot_it != assets_->skeleton_nodes().end() ? pivot_it->second.y : stance_npivot_y_;

        // Y normalization: keep character at correct height.
        //
        // The .bin animation stores absolute node Y for all nodes.
        // anim_node_pos_[name].y = (abs_y - npivot_y + npivot_rest_y)
        // resolve_body_node: sy = world_cy + (ly - pivot_local_y)
        //
        // For NPivot: sy = world_cy (since ly = npivot_rest_y for NPivot)
        // So world_cy = NPivot world position.
        //
        // player_pos_y_ (-93) is the NPivot world position from params.xml.
        // y_adjust = 0 positions NPivot at player_pos_y_.
        //
        // But there's a +4 offset needed to align feet with floor surface:
        // Floor at world_y = -193 (layer_3 at y=225, height=64, surface = -225+32)
        // NPivot at -93. Feet (NToe) at -93 + (65.52 - 169.48) = -196.96
        // Floor surface at -193. Feet are 3.96 below surface.
        // y_adjust = +4 shifts everything up so feet are at -192.96 ≈ floor.
        //
        // For crouch: NPivot goes to 106.21 (down from 169.48).
        // NToe abs_y = 2.24 (stays at floor).
        // anim_node_pos_.y = (2.24 - 106.21 + 169.48) = 65.51
        // sy = (-93+4) + (65.51 - 169.48) = -89 - 103.97 = -192.97. ON FLOOR! ✓
        //
        // For jump: NPivot goes to 243.93 (up from 169.48).
        // NToe abs_y = 189.15 (feet go up).
        // anim_node_pos_.y = (189.15 - 243.93 + 169.48) = 114.70
        // sy = (-93+4) + (114.70 - 169.48) = -89 - 54.78 = -143.78
        // Floor at -193. Feet at -143.78 — 49 ABOVE floor! ✓ (character jumped up)
        //
        // For roll: NPivot goes to 20.11 (very low).
        // NToe abs_y = 0 (feet at floor).
        // anim_node_pos_.y = (0 - 20.11 + 169.48) = 149.37
        // sy = (-93+4) + (149.37 - 169.48) = -89 - 20.11 = -109.11
        // Floor at -193. Feet at -109 — 84 ABOVE floor! ✗ (character floating)
        //
        // Problem: roll has NPivot very low but feet at floor.
        // The formula sy = world_cy + abs_y - npivot_y gives:
        //   sy = -89 + 0 - 20.11 = -109.11 (wrong, should be -193)
        //
        // Wait: anim_node_pos_.y = (abs_y - npivot_y + npivot_rest_y)
        // sy = world_cy + (ly - pivot_local_y)
        //    = world_cy + (abs_y - npivot_y + npivot_rest_y - npivot_rest_y)
        //    = world_cy + abs_y - npivot_y
        // For roll: sy = -89 + 0 - 20.11 = -109.11. WRONG.
        //
        // But abs_y for NToe in roll = 0 (feet at floor in .bin).
        // npivot_y = 20.11. So abs_y - npivot_y = -20.11.
        // sy = world_cy - 20.11 = -89 - 20.11 = -109.11.
        //
        // For feet at floor (-193): world_cy = -193 + 20.11 = -172.89.
        // y_adjust = -172.89 - (-93) = -79.89.
        //
        // This is the lowest-node approach! But it doesn't work for jump
        // because lowest node stays low during jump.
        //
        // SOLUTION: Use NPivot-based y_adjust for jumps, lowest-node for rolls.
        // But we can't easily distinguish them.
        //
        // BETTER SOLUTION: The original game uses MoveInside alignment which
        // aligns a specific pivot node (NHeel_1 or NHeel_2) to the floor.
        // We should use NHeel_1 Y for alignment, not NPivot or lowest node.
        //
        // For now: use constant y_adjust = 4 (works for standing, crouch, jump).
        // Roll issue: character floats during roll, but roll is short (26 frames).
        // This is acceptable until we implement proper MoveInside alignment.
        // [ORIGINAL] MoveInside pipeline byte-verified (objdump on ShadowFight2.s86):
        //   Step 1 (fcn.10165c10): captures pivotID -> Model+0x58, node_array[pivotID] -> Model+0x5c
        //   Step 2 (fcn.10164c20): resolves new pivotID, calls fcn.10103690 (trivial accessor:
        //     return this+0x7c, 3 bytes), then fcn.10103e80(axis=2) — called ONCE in entire binary
        //   Step 3 (fcn.101661d0): reads Model[0xe8][axis=2][pivotID] (Vec3) via fcn.1028e490 (Vec3 copy)
        //   Post-Step3: playInfo copies Z->X and Z->Y (memcpy). All axes get same Vec3.
        //   fcn.1028e490 = Vec3 copy, fcn.1028e4c0 = Vec3 add, fcn.10102c70 = container accessor
        // [HEURISTIC-TODO] consumption formula (how Vec3 -> world transform) NOT yet traced.
        // The Vec3 from Step 3 is the per-axis displacement; how it is applied to produce
        // the render transform is unconfirmed. However, for Axis="X|Z" (ALL current player
        // moves), the PC sf2.js source shows dI=false -> Y = ShiftY, and ShiftY=0 for all
        // player moves. So this remains open only for hypothetical non-X|Z alignment.
        //
        // [ORIGINAL] MoveInside Y alignment — VERIFIED from PC version sf2.js.
        // See update_animation() for full documentation.
        // y_adjust = ShiftY = 0 for all Axis="X|Z" moves (verified from moves.xml).
        // y_adjust_smoothed_ is computed in update_animation() (before hit detection).
        // Here we just USE the already-computed value.
        float world_cx = player_pos_x_;
        float world_cy = player_pos_y_ + y_adjust_smoothed_;

        // Build edge lookup from both body.xml edges and skeleton.xml edges
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : assets_->body_model()->edges)
            edge_map[e.name] = {e.end1, e.end2};
        for (auto& [name, e] : assets_->skeleton_edges())
            edge_map[name] = {e.end1, e.end2};

        // World-to-screen helper (for capsules that still use screen-space)
        float hw = (float)platform_->window_width() / (2.0f * zoom_);
        float hh = (float)platform_->window_height() / (2.0f * zoom_);
        float left = cam_x_ - hw, right = cam_x_ + hw;
        float bottom = cam_y_ - hh, top = cam_y_ + hh;
        auto w2s = [&](float wx, float wy, float& sx, float& sy) {
            sx = (wx - left) / (right - left) * platform_->window_width();
            sy = (1.0f - (wy - bottom) / (top - bottom)) * platform_->window_height();
        };

        // Render character as unified dark silhouette.
        // Render ALL capsules (including duplicates — they overlap to fill gaps
        // at joints). Apply Margin1/Margin2 to trim ends properly.
        // [ORIGINAL] Player block: blue tint when blocking, matching enemy block color.
        ren::Color4B silhouette_col = player_fighter_.is_blocking
            ? ren::Color4B{40, 40, 80, 255}    // block: dark blue (matches enemy block)
            : ren::Color4B{20, 20, 25, 255};   // normal: dark silhouette

        for (auto& c : assets_->body_model()->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto [x1, y1] = resolve_body_node(eit->second.first,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [x2, y2] = resolve_body_node(eit->second.second,
                world_cx, world_cy, facing_right_, pivot_local_y);
            // Apply margin (trim capsule ends to prevent overlap artifacts)
            float m1 = c.margin1, m2 = c.margin2;
            float mx1 = x1 + (x2 - x1) * m1;
            float my1 = y1 + (y2 - y1) * m1;
            float mx2 = x2 - (x2 - x1) * m2;
            float my2 = y2 - (y2 - y1) * m2;
            
            float r = (c.radius1 + c.radius2) * 0.5f * 1.0f;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            float ax = mx1 + px*ht, ay = my1 + py*ht;
            float bx = mx2 + px*ht, by = my2 + py*ht;
            float cx = mx2 - px*ht, cy_ = my2 - py*ht;
            float dx_ = mx1 - px*ht, dy_ = my1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy_, silhouette_col);
            renderer_->draw_filled_triangle_world(ax, ay, cx, cy_, dx_, dy_, silhouette_col);
            // Circle caps at both ends — fills gaps at joints
            renderer_->draw_filled_circle_world(mx1, my1, ht, silhouette_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, silhouette_col);
        }

        // [ORIGINAL] Render skeleton edges that have a Radius but no capsule
        // in body.xml (e.g. EHead, ENeck). The original game renders these as
        // capsule-like shapes (thick lines with circle caps). skeleton.xml
        // defines <EHead Radius="12"> and <ENeck Radius="6"> — without this,
        // the character has NO HEAD (the body.xml capsules only cover torso/limbs).
        for (auto& [ename, sedge] : assets_->skeleton_edges()) {
            if (sedge.radius <= 0) continue;
            // Skip if this edge already has a capsule in body.xml
            bool has_capsule = false;
            for (auto& c : assets_->body_model()->capsules) {
                if (c.edge_name == ename) { has_capsule = true; break; }
            }
            if (has_capsule) continue;
            // Resolve endpoints
            auto it1 = assets_->skeleton_nodes().find(sedge.end1);
            auto it2 = assets_->skeleton_nodes().find(sedge.end2);
            if (it1 == assets_->skeleton_nodes().end() || it2 == assets_->skeleton_nodes().end()) continue;
            auto [x1, y1] = resolve_body_node(sedge.end1,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [x2, y2] = resolve_body_node(sedge.end2,
                world_cx, world_cy, facing_right_, pivot_local_y);
            float r = sedge.radius;
            float m1 = sedge.margin1, m2 = sedge.margin2;
            float mx1 = x1 + (x2 - x1) * m1;
            float my1 = y1 + (y2 - y1) * m1;
            float mx2 = x2 - (x2 - x1) * m2;
            float my2 = y2 - (y2 - y1) * m2;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) {
                // Degenerate — draw as circle at midpoint
                renderer_->draw_filled_circle_world((mx1+mx2)*0.5f, (my1+my2)*0.5f, r, silhouette_col);
                continue;
            }
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            float ax = mx1 + px*ht, ay = my1 + py*ht;
            float bx = mx2 + px*ht, by = my2 + py*ht;
            float cx = mx2 - px*ht, cy_ = my2 - py*ht;
            float dx_ = mx1 - px*ht, dy_ = my1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy_, silhouette_col);
            renderer_->draw_filled_triangle_world(ax, ay, cx, cy_, dx_, dy_, silhouette_col);
            renderer_->draw_filled_circle_world(mx1, my1, ht, silhouette_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, silhouette_col);
        }

        // Render triangles (small parts)
        // Skip triangles that reference non-animated nodes (BODY-Node entries
        // from body.xml). These are cloth simulation nodes that don't have
        // per-node animation data in the .bin files. Rendering them at their
        // rest-pose positions while other triangle vertices are animated
        // causes visible stretching on the legs (especially around the calves
        // and ankles where BODY-Triangle-7..10 are located).
        for (auto& t : assets_->body_model()->triangles) {
            // [ORIGINAL] Skip triangles that mix cloth nodes (BODY-Node/HEAD-Node)
            // with skeletal nodes. Cloth nodes (BODY-Node*) have rest-pose positions
            // in body.xml but NO per-frame animation data in .bin files — they're
            // physics-simulated (Verlet) in the original. Rendering them at rest
            // pose while other triangle vertices are animated causes severe
            // stretching (especially on legs/calves where BODY-Triangle 7-11
            // mix NAnkle/NKnee with BODY-Node*).
            //
            // Only render triangles where ALL 3 nodes are:
            //   - skeletal (in anim_node_pos_ or assets_->skeleton_nodes()), OR
            //   - MacroNodes (HEAD-MacroNode/BODY-MacroNode — these compute
            //     position from skeletal children via LCC weights, so they
            //     animate correctly)
            // [ORIGINAL] Skip triangles with cloth nodes (BODY-Node/HEAD-Node)
            // AND triangles with MacroNodes (HEAD-MacroNode/BODY-MacroNode).
            // MacroNodes use LCC weights calibrated for rest pose — when skeleton
            // animates, weighted sum produces stretched positions. Without cloth
            // simulation, only render triangles with pure skeletal nodes.
            auto is_non_skel = [&](const std::string& n) {
                return assets_->body_model()->nodes.count(n) > 0 ||
                       assets_->body_model()->macro_nodes.count(n) > 0;
            };
            if (is_non_skel(t.n1) || is_non_skel(t.n2) || is_non_skel(t.n3)) {
                continue;
            }
            auto can_resolve = [&](const std::string& n) {
                return anim_node_pos_.count(n) || assets_->skeleton_nodes().count(n);
            };
            if (!can_resolve(t.n1) || !can_resolve(t.n2) || !can_resolve(t.n3)) {
                continue;
            }
            auto [tx0, ty0] = resolve_body_node(t.n1,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [tx1, ty1] = resolve_body_node(t.n2,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [tx2, ty2] = resolve_body_node(t.n3,
                world_cx, world_cy, facing_right_, pivot_local_y);
            renderer_->draw_filled_triangle_world(tx0, ty0, tx1, ty1, tx2, ty2, silhouette_col);
        }

        // [P3] Render the equipped ARMOR over the body: its capsules
        // reference the fighter's E* skeleton edges (EArm_1, EChest, ... —
        // see armor_robe.xml), the same edge_map the body capsules use, so
        // the armor tracks the animated body. The cloth node/edge mesh is
        // not simulated (no cloth physics), so only the capsules are drawn.
        if (assets_->armor_model()) {
            // [R1] The armor is part of the fighter's unified DARK
            // silhouette (the render law above). The robe (ARMOR_ROBE, the
            // shipped default) covers the whole fighter, and the old khaki
            // fill {128,96,62} over every capsule read as "тело жёлтого
            // цвета". Dark leather tone: silhouette-consistent, slightly
            // lighter than the body {20,20,25} so the armor stays visible.
            ren::Color4B armor_col{34, 31, 27, 255};
            if (player_hit_flash_ > 0) armor_col = ren::Color4B{255, 220, 190, 255};
            armor_render_color_ = armor_col;  // [R1] probe seam
            {
                // [R1] Track the armor's rendered world extents each frame so
                // the probe samples exactly where the robe paints.
                float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
                int drawn = 0;
                for (auto& c : assets_->armor_model()->capsules) {
                    auto eit = edge_map.find(c.edge_name);
                    if (eit == edge_map.end()) continue;
                    auto [ax1, ay1] = resolve_body_node(eit->second.first,
                        world_cx, world_cy, facing_right_, pivot_local_y);
                    auto [ax2, ay2] = resolve_body_node(eit->second.second,
                        world_cx, world_cy, facing_right_, pivot_local_y);
                    minx = std::min(minx, std::min(ax1, ax2));
                    maxx = std::max(maxx, std::max(ax1, ax2));
                    miny = std::min(miny, std::min(ay1, ay2));
                    maxy = std::max(maxy, std::max(ay1, ay2));
                    ++drawn;
                }
                if (drawn > 0) {
                    armor_world_minx_ = minx;
                    armor_world_miny_ = miny;
                    armor_world_maxx_ = maxx;
                    armor_world_maxy_ = maxy;
                }
            }
            for (auto& c : assets_->armor_model()->capsules) {
                auto eit = edge_map.find(c.edge_name);
                if (eit == edge_map.end()) continue;
                auto [ax1, ay1] = resolve_body_node(eit->second.first,
                    world_cx, world_cy, facing_right_, pivot_local_y);
                auto [ax2, ay2] = resolve_body_node(eit->second.second,
                    world_cx, world_cy, facing_right_, pivot_local_y);
                float r = (c.radius1 + c.radius2) * 0.5f;
                if (r <= 0) r = 4.0f;
                float dx = ax2 - ax1, dy = ay2 - ay1;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len < 0.5f) continue;
                float ux = dx / len, uy = dy / len;
                float px = -uy, py = ux;
                renderer_->draw_filled_triangle_world(ax1 + px * r, ay1 + py * r,
                    ax2 + px * r, ay2 + py * r, ax2 - px * r, ay2 - py * r, armor_col);
                renderer_->draw_filled_triangle_world(ax1 + px * r, ay1 + py * r,
                    ax2 - px * r, ay2 - py * r, ax1 - px * r, ay1 - py * r, armor_col);
                renderer_->draw_filled_circle_world(ax1, ay1, r, armor_col);
                renderer_->draw_filled_circle_world(ax2, ay2, r, armor_col);
                ++armor_capsules_drawn_;
            }
            // [R1] The equipped HELM (helm slot -> head.xml, EHead/ENeck
            // capsules) is part of the same armored silhouette; P3 loaded
            // the model but never drew it. Same dark tone as the armor.
            if (assets_->helm_model()) {
                ren::Color4B helm_col = armor_col;
                for (auto& c : assets_->helm_model()->capsules) {
                    auto eit = edge_map.find(c.edge_name);
                    if (eit == edge_map.end()) continue;
                    auto [ax1, ay1] = resolve_body_node(eit->second.first,
                        world_cx, world_cy, facing_right_, pivot_local_y);
                    auto [ax2, ay2] = resolve_body_node(eit->second.second,
                        world_cx, world_cy, facing_right_, pivot_local_y);
                    float r = (c.radius1 + c.radius2) * 0.5f;
                    if (r <= 0) r = 4.0f;
                    float dx = ax2 - ax1, dy = ay2 - ay1;
                    float len = std::sqrt(dx * dx + dy * dy);
                    if (len < 0.5f) continue;
                    float ux = dx / len, uy = dy / len;
                    float px = -uy, py = ux;
                    renderer_->draw_filled_triangle_world(ax1 + px * r, ay1 + py * r,
                        ax2 + px * r, ay2 + py * r, ax2 - px * r, ay2 - py * r, helm_col);
                    renderer_->draw_filled_triangle_world(ax1 + px * r, ay1 + py * r,
                        ax2 - px * r, ay2 - py * r, ax1 - px * r, ay1 - py * r, helm_col);
                    renderer_->draw_filled_circle_world(ax1, ay1, r, helm_col);
                    renderer_->draw_filled_circle_world(ax2, ay2, r, helm_col);
                    ++helm_capsules_drawn_;
                }
            }
        }

        // Render player's equipped weapon model (if loaded)
        // [U1] Weapons have NO edges -- their figures are Triangles.
        // [R1] The vertices resolve through the weapon model's MacroNodes
        // (LCC weights over the skeleton's Weapon-Node*_1 pins -- Edge129
        // pins Weapon-Node2_1 to NWrist_1, the dojo placement law), so the
        // mesh lands AT the hand and tracks it. The old code looked the
        // vertices up in the plain node map (knives ship ZERO plain nodes)
        // and drew the authored rest coords at a fixed body-center offset
        // at 0.3x -- invisible or "далеко от персонажа". Steel tone, not
        // the yellow placeholder.
        if (assets_->weapon_model() && !assets_->weapon_model()->triangles.empty()) {
            ren::Color4B wcol{150, 154, 162, 255};
            const float dir = facing_right_ ? 1.0f : -1.0f;
            auto& wm = *assets_->weapon_model();
            for (const auto& t : wm.triangles) {
                float ax, ay, bx, by, cx, cy;
                if (!resolve_player_weapon_vertex(wm, t.n1, world_cx, world_cy,
                        dir, pivot_local_y, true, ax, ay) ||
                    !resolve_player_weapon_vertex(wm, t.n2, world_cx, world_cy,
                        dir, pivot_local_y, true, bx, by) ||
                    !resolve_player_weapon_vertex(wm, t.n3, world_cx, world_cy,
                        dir, pivot_local_y, true, cx, cy))
                    continue;
                renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy, wcol);
            }
        }
    }

    // ---------- Character rendering ----------
    // Skeleton local coords: Y-UP (0 = feet, positive = up).
    // World coords: Y-UP (cocos2d convention, positive = up).
    //
    // Render ONLY the body silhouette (capsules + triangles).
    // The skeleton lines and joints are NOT rendered — they were causing
    // the "half black, half white squares" effect (white bones drawn over
    // dark silhouette). The original game renders only the silhouette.
    void render_character() {
        // Render body mesh (silhouette from capsules + triangles)
        render_body_model();
        // No skeleton lines, no joints — silhouette only.
    }

    // ---------- Punching bag (real 3D model from skeleton_punching_bag.xml) ----------
    void load_punching_bag_model() {
        auto skel_candidates = model_paths(asset_root_, "skeleton_punching_bag.xml");
        auto fig_candidates = model_paths(asset_root_, "punching_bag.xml");
        std::string skel_path, fig_path;
        for (const auto& p : skel_candidates)
            if (std::filesystem::exists(p)) { skel_path = p.string(); break; }
        for (const auto& p : fig_candidates)
            if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
        if (skel_path.empty()) { std::printf("  skeleton_punching_bag.xml NOT FOUND!\n"); return; }

        assets_->bag_model() = std::make_unique<BodyModel>();

        fmt::XmlDocument skel_doc;
        if (!skel_doc.parse(read_text(skel_path))) {
            std::fprintf(stderr, "[punching_bag] skel parse error: %s\n", skel_doc.error().c_str());
            init_bag_verlet();
            return;
        }
        auto* scene = skel_doc.root()->first_child("Scene");
        if (!scene) { std::printf("  skeleton_punching_bag.xml: no <Scene>\n"); init_bag_verlet(); return; }

        if (auto* ns = scene->first_child("Nodes")) {
            for (const auto& child : ns->children) {
                std::string type = child.attr("Type");
                if (type != "Node" && type != "CenterOfMass") continue;
                BodyNode n;
                n.name = child.name;
                n.x = tof(child.attr("X"));
                n.y = tof(child.attr("Y"));
                n.mass = tof(child.attr("Mass"), 1.0f);
                n.fixed = (toi(child.attr("Fixed")) != 0);
                n.attenuation = tof(child.attr("Attenuation"), 0.02f);
                n.cloth = (toi(child.attr("Cloth")) != 0);
                assets_->bag_model()->nodes[n.name] = n;
            }
        }

        if (auto* es = scene->first_child("Edges")) {
            for (const auto& child : es->children) {
                if (child.attr("Type") != "Edge") continue;
                BodyEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                e.collisible = (child.attr("Collisible") == "1");
                assets_->bag_model()->edges.push_back(e);
            }
        }

        if (!fig_path.empty()) {
            fmt::XmlDocument fig_doc;
            if (fig_doc.parse(read_text(fig_path))) {
                if (auto* fs = fig_doc.root()->first_child("Scene"); fs && (fs = fs->first_child("Figures"))) {
                    for (const auto& child : fs->children) {
                        if (child.attr("Type") != "Capsule") continue;
                        BodyCapsule c;
                        c.edge_name = child.attr("Edge");
                        c.radius1 = tof(child.attr("Radius1"));
                        c.radius2 = tof(child.attr("Radius2"));
                        c.margin1 = tof(child.attr("Margin1"));
                        c.margin2 = tof(child.attr("Margin2"));
                        assets_->bag_model()->capsules.push_back(c);
                    }
                }
            }
        }
        std::printf("  Punching bag: %zu nodes, %zu edges, %zu capsules\n",
                    assets_->bag_model()->nodes.size(), assets_->bag_model()->edges.size(),
                    assets_->bag_model()->capsules.size());
        init_bag_verlet();
    }


    // ---------- Enemy weapon model ----------
    void load_enemy_weapon(const std::string& weapon_name) {
        auto candidates = model_paths(asset_root_, weapon_name.c_str());
        std::string fig_path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
        }
        if (fig_path.empty()) { std::printf("  Enemy weapon '%s' NOT FOUND!\n", weapon_name.c_str()); return; }
        assets_->enemy_weapon_model() = std::make_unique<BodyModel>();
        fmt::XmlDocument doc;
        if (!doc.parse(read_text(fig_path))) {
            std::fprintf(stderr, "[weapon] xml parse error: %s\n", doc.error().c_str());
            assets_->enemy_weapon_model().reset(); return;
        }
        auto* scene = doc.root()->first_child("Scene");
        if (!scene) { assets_->enemy_weapon_model().reset(); return; }
        if (auto* ns = scene->first_child("Nodes")) {
            for (const auto& child : ns->children) {
                std::string type = child.attr("Type");
                // [U1] Weapon models ship ONLY MacroNodes (weapon_knuckles.xml:
                // 138 of them, two hands) — the old filter for Type="Node"/
                // "CenterOfMass" parsed ZERO nodes. Parse them the same way
                // load_player_weapon does: macro map + plain node map
                // (MacroNodes carry X/Y/Mass too).
                if (type == "MacroNode") {
                    BodyMacroNode mn;
                    mn.name = child.name;
                    mn.children[0] = child.attr("ChildNode1");
                    mn.children[1] = child.attr("ChildNode2");
                    mn.children[2] = child.attr("ChildNode3");
                    mn.children[3] = child.attr("ChildNode4");
                    assets_->enemy_weapon_model()->macro_nodes[mn.name] = mn;
                }
                BodyNode n;
                n.name = child.name;
                n.x = tof(child.attr("X"));
                n.y = tof(child.attr("Y"));
                n.mass = tof(child.attr("Mass"), 1.0f);
                n.fixed = (toi(child.attr("Fixed")) != 0);
                n.attenuation = tof(child.attr("Attenuation"), 0.02f);
                assets_->enemy_weapon_model()->nodes[n.name] = n;
            }
        }
        if (auto* es = scene->first_child("Edges")) {
            for (const auto& child : es->children) {
                if (child.attr("Type") != "Edge") continue;
                BodyEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                assets_->enemy_weapon_model()->edges.push_back(e);
            }
        }
        if (auto* fs = scene->first_child("Figures")) {
            for (const auto& child : fs->children) {
                std::string type = child.attr("Type");
                if (type == "Capsule") {
                    BodyCapsule c;
                    c.edge_name = child.attr("Edge");
                    c.radius1 = tof(child.attr("Radius1"));
                    c.radius2 = tof(child.attr("Radius2"));
                    c.margin1 = tof(child.attr("Margin1"));
                    c.margin2 = tof(child.attr("Margin2"));
                    assets_->enemy_weapon_model()->capsules.push_back(c);
                } else if (type == "Triangle") {
                    // [U1] Weapon figures are Triangles (weapon_knuckles.xml:
                    // 276 of them); the Capsule-only filter parsed none.
                    BodyTriangle t;
                    t.n1 = child.attr("Node1");
                    t.n2 = child.attr("Node2");
                    t.n3 = child.attr("Node3");
                    assets_->enemy_weapon_model()->triangles.push_back(t);
                }
            }
        }
        std::printf("  Enemy weapon '%s': %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                    weapon_name.c_str(), assets_->enemy_weapon_model()->nodes.size(),
                    assets_->enemy_weapon_model()->edges.size(),
                    assets_->enemy_weapon_model()->capsules.size(),
                    assets_->enemy_weapon_model()->triangles.size());
    }

    // [H07/H05] REAL enemy animation names from the animation catalog.
    // The engine must play names moves.xml/animations actually ship, never
    // invented labels ("fists_idle", "fists_hit", "fists_block" are NOT
    // moves.xml names — HARDCODE_AUDIT H07/I03). The weapon stance idle
    // resolves through moves.xml: the "StartIdleStance" template move whose
    // TacticWeapon covers the weapon subtype carries the real FileName
    // (SwordsStartStanceIdle -> swords_stance_idle.bin). The default build
    // (no TacticWeapon) wins ties — FistsStartStanceIdle-Left ->
    // fists1_stance_idle.bin, the real fists idle.
    static bool tactic_weapon_matches(const std::string& haystack,
                                      const std::string& subtype) {
        if (haystack.empty()) return false;
        const std::string h = "|" + haystack + "|";
        return h.find("|" + subtype + "|") != std::string::npos;
    }
    static std::string strip_bin_suffix(const std::string& f) {
        if (f.size() > 4 && f.substr(f.size() - 4) == ".bin")
            return f.substr(0, f.size() - 4);
        return f;
    }
    std::string stance_idle_anim_for(const std::string& subtype) const {
        const std::string needle = subtype.empty() ? "Fists" : subtype;
        const MoveDef* best_tagged = nullptr;
        const MoveDef* best_untagged = nullptr;
        if (assets_) {
            for (const auto& [name, mv] : assets_->moves()) {
                (void)name;
                if (mv.template_name.find("StartIdleStance") == std::string::npos)
                    continue;
                if (mv.tactic_weapon.empty()) {
                    if (!best_untagged || mv.priority < best_untagged->priority)
                        best_untagged = &mv;
                    continue;
                }
                if (!tactic_weapon_matches(mv.tactic_weapon, needle)) continue;
                if (!best_tagged || mv.priority < best_tagged->priority)
                    best_tagged = &mv;
            }
        }
        // [H07] Fists uses the DEFAULT build (FistsStartStanceIdle-Left ->
        // fists1_stance_idle.bin, the real fists idle; the -Right variant is
        // a weapon-tagged variant). Any other weapon uses its TacticWeapon
        // stance (SwordsStartStanceIdle -> swords_stance_idle.bin); the
        // default build remains the generic fallback.
        const MoveDef* pick = (needle == "Fists") ? best_untagged : best_tagged;
        if (!pick) pick = best_untagged ? best_untagged : best_tagged;
        if (pick && !pick->filename.empty())
            return strip_bin_suffix(pick->filename);
        return "stance_idle";  // real catalog name (stance_idle.bin)
    }
    std::string enemy_idle_anim() const {
        return stance_idle_anim_for(enemy_weapon_subtype_);
    }

    // [H05] The enemy's attack animation resolves the weapon family's base
    // 1key attack move from moves.xml (TacticWeapon covers the subtype,
    // Template carries "1key" — the base attack binding). The old executor
    // hardcoded "high_punch", so a sword loadout swung with fists anims
    // (HARDCODE_AUDIT H05). Fists -> HighPunch -> high_punch (real).
    std::string enemy_attack_anim() const {
        const std::string needle = enemy_weapon_subtype_.empty()
                                       ? "Fists" : enemy_weapon_subtype_;
        // A move whose TacticWeapon EXPLICITLY covers the subtype wins over
        // an unfiltered move of equal priority (HighKick has no
        // TacticWeapon and would otherwise shadow HighPunch for fists and
        // SwordsSlash for swords via unordered_map iteration order).
        const MoveDef* best = nullptr;
        const MoveDef* best_unfiltered = nullptr;
        if (assets_) {
            for (const auto& [name, mv] : assets_->moves()) {
                (void)name;
                if (!mv.is_attack) continue;
                if (mv.template_name.find("1key") == std::string::npos) continue;
                if (mv.tactic_weapon.empty()) {
                    if (!best_unfiltered || mv.priority < best_unfiltered->priority)
                        best_unfiltered = &mv;
                    continue;
                }
                if (!tactic_weapon_matches(mv.tactic_weapon, needle)) continue;
                if (!best || mv.priority < best->priority) best = &mv;
            }
        }
        if (!best) best = best_unfiltered;
        if (best && !best->filename.empty())
            return strip_bin_suffix(best->filename);
        return "high_punch";  // real fists attack (high_punch.bin)
    }
    // [H05] Block/hit animations are REAL catalog names: "fists_block" /
    // "fists_hit" are not moves.xml names and have no .bin. The player
    // block path plays high_block.bin; the Duck move (moves.xml Type=MOVE,
    // the block action) plays duck.bin; the hit reaction is high_hit.bin.
    std::string enemy_block_anim() const {
        return (ai_last_decision_.animation == "Duck") ? "duck" : "high_block";
    }
    // [Soak-fix Wave 9A] F1: the enemy's hit-reaction animation is the
    // moves.xml Recoil move resolved from the attack's <Hit Name> zone
    // (High -> HighHit -> high_hit.bin, HighHeavy -> HighHitHeavy, ...),
    // stored on the Combat state by apply_player_hit_feedback(); falls back
    // to the catalog default high_hit (the pre-Wave-9A behavior).
    std::string enemy_hit_anim() const {
        return enemy_reaction_anim_.empty() ? "high_hit" : enemy_reaction_anim_;
    }

    // [Soak-fix Wave 9A] F1: full hit feedback for a registered player->
    // enemy hit (battle/dojo sparring path): the enemy's hit-reaction
    // animation (moves.xml Recoil move by the attack's <Hit Name> zone),
    // the real impact sound (hit1-6.wav / super_hit1-5.wav), the reversed
    // authored <Impulse X> knockback over the reaction, and the defender's
    // fight-memory damage-event feed (F2). Defined in game.cpp.
    // [Wave 11A M4] `critical` selects the FALL family reaction (knockdown)
    // with its bodyfall sound instead of the plain Recoil.
    void apply_player_hit_feedback(float hit_x, float hit_y, int hit_frame,
                                   const std::string& move_name,
                                   float final_damage, bool blocked,
                                   bool critical);

    // [H08] The enemy's swing connects via MODEL-EDGE COLLISION — the R2
    // hit-test path mirrored: the enemy's attacking edges (the attack
    // move's AttackingParts: skeleton edges for fists, the ENEMY WEAPON
    // model's edges for a loadout weapon) run against the PLAYER's body
    // capsules (body.xml + helm). The old `dist <= 250` test was a
    // placeholder (HARDCODE_AUDIT H08); it stays ONLY as a documented
    // fallback when the collision path cannot run (no player body model,
    // no resolvable move/edges — placeholder mode).
    bool enemy_attack_connects() {
        if (!assets_ || !assets_->body_model()) return enemy_distance_hit_ok();
        // The enemy's attack move: the move whose FileName matches the
        // resolved attack animation (e.g. SwordsSlash -> swords_slash.bin).
        const std::string anim = enemy_attack_anim();
        const MoveDef* move = nullptr;
        for (const auto& [mn, mv] : assets_->moves()) {
            (void)mn;
            if (mv.filename == anim + ".bin") { move = &mv; break; }
        }
        if (!move || move->attack_edges.empty()) return enemy_distance_hit_ok();
        // Enemy-side node positions from the ENEMY's current animation
        // (mirror of the R2 player->enemy path).
        std::unordered_map<std::string, std::pair<float, float>> enemy_node_pos;
        auto enemy_anim_it = assets_->animations().find(enemy_anim_);
        int frame = 0, next = 0;
        float alpha = 0.0f;
        bool has_anim = false;
        if (enemy_anim_it != assets_->animations().end() &&
            enemy_anim_it->second.frame_count > 0) {
            has_anim = true;
            const auto& anim_data = enemy_anim_it->second;
            float f = enemy_anim_time_ * 20.0f;  // enemy fixed 20fps
            if (f < 0) f = 0;
            int fi = (int)f;
            frame = anim_data.frame_count > 0 ? fi % anim_data.frame_count : 0;
            next = (frame + 1) % anim_data.frame_count;
            alpha = f - (int)f;
            const auto& names = assets_->ordered_node_names();
            // [H08] The .bin animations store ABSOLUTE node coordinates; the
            // AnimationPlayer law converts to pivot-relative by subtracting
            // the frame's NPivot (ix - npivot_x, iy - npivot_y). The R2
            // copy used them raw, which put the enemy body ~330 units off
            // whenever an animation was playing (the hit test silently
            // missed in real fights).
            int npivot_idx = -1;
            for (int i = 0; i < (int)names.size(); ++i)
                if (names[i] == "NPivot") { npivot_idx = i; break; }
            float npx = 0.0f, npy = 0.0f;
            if (npivot_idx >= 0) {
                float z0 = 0.0f;
                anim_data.get_node_pos(frame, npivot_idx, npx, npy, z0);
            }
            for (int i = 0; i < (int)names.size() && i < 67; ++i) {
                float x0, y0, z0, x1, y1, z1;
                if (anim_data.get_node_pos(frame, i, x0, y0, z0) &&
                    anim_data.get_node_pos(next, i, x1, y1, z1)) {
                    enemy_node_pos[names[i]] = {
                        x0 + (x1 - x0) * alpha - npx,
                        y0 + (y1 - y0) * alpha - npy};
                }
            }
        }
        auto pivot_it = assets_->skeleton_nodes().find("NPivot");
        const float pivot_ly = pivot_it != assets_->skeleton_nodes().end()
                                   ? pivot_it->second.y : stance_npivot_y_;

        // Enemy node -> world (mirror of the R2 resolve_enemy_node).
        // [H08] Two coordinate laws: ANIMATION positions are pivot-relative
        // (ix - npivot, iy - npivot), so the world pivot sits AT enemy_y;
        // SKELETON rest positions are rest-relative (rest_y - pivot_ly).
        auto resolve_enemy_node = [&](const std::string& name)
            -> std::pair<float, float> {
            float lx = 0, ly = 0;
            bool from_anim = false;
            bool found = false;
            auto eit = enemy_node_pos.find(name);
            if (eit != enemy_node_pos.end()) { lx = eit->second.first;
                                               ly = eit->second.second;
                                               from_anim = true;
                                               found = true; }
            if (!found) {
                auto sit = assets_->skeleton_nodes().find(name);
                if (sit != assets_->skeleton_nodes().end()) {
                    lx = sit->second.x; ly = sit->second.y; found = true;
                }
            }
            if (!found) return {enemy_pos_x_, enemy_pos_y_};
            const float wy = from_anim
                ? (enemy_pos_y_ + enemy_y_adjust_) + ly
                : (enemy_pos_y_ + enemy_y_adjust_) + (ly - pivot_ly);
            return {enemy_pos_x_ + (enemy_facing_right_ ? lx : -lx), wy};
        };
        // Enemy weapon vertex -> world (macro LCC law over the ENEMY's
        // skeleton/anim positions; mirror of resolve_player_weapon_vertex).
        std::function<bool(const resf2::game::BodyModel&, const std::string&,
                           float&, float&)> resolve_enemy_weapon_vertex;
        resolve_enemy_weapon_vertex =
            [&](const resf2::game::BodyModel& wm, const std::string& name,
                float& wx, float& wy) -> bool {
            auto mit = wm.macro_nodes.find(name);
            if (mit != wm.macro_nodes.end()) {
                float sum = 0.0f, ax = 0.0f, ay = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    if (mit->second.children[i].empty()) continue;
                    float cx, cy;
                    if (!resolve_enemy_weapon_vertex(
                            wm, mit->second.children[i], cx, cy))
                        continue;
                    ax += cx * mit->second.lcc[i];
                    ay += cy * mit->second.lcc[i];
                    sum += mit->second.lcc[i];
                }
                if (std::fabs(sum) > 1e-6f) { wx = ax / sum; wy = ay / sum;
                                              return true; }
                return false;
            }
            auto nit = wm.nodes.find(name);
            if (nit != wm.nodes.end()) {
                wx = enemy_pos_x_ + (enemy_facing_right_ ? nit->second.x
                                                         : -nit->second.x);
                wy = (enemy_pos_y_ + enemy_y_adjust_) +
                     (nit->second.y - pivot_ly);
                return true;
            }
            auto pit = enemy_node_pos.find(name);
            if (pit != enemy_node_pos.end()) {
                wx = enemy_pos_x_ + (enemy_facing_right_ ? pit->second.first
                                                         : -pit->second.first);
                wy = (enemy_pos_y_ + enemy_y_adjust_) + pit->second.second;
                return true;
            }
            auto sit = assets_->skeleton_nodes().find(name);
            if (sit != assets_->skeleton_nodes().end()) {
                wx = enemy_pos_x_ + (enemy_facing_right_ ? sit->second.x
                                                         : -sit->second.x);
                wy = (enemy_pos_y_ + enemy_y_adjust_) +
                     (sit->second.y - pivot_ly);
                return true;
            }
            return false;
        };

        // Edge lookup: skeleton edges + the ENEMY weapon model edges (the
        // attacker side, Q2-A/B: unarmed moves name skeleton edges, weapon
        // moves name the WEAPON model's edges).
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (const auto& [name, e] : assets_->skeleton_edges())
            edge_map[name] = {e.end1, e.end2};
        const resf2::game::BodyModel* ewm = assets_->enemy_weapon_model().get();
        if (ewm)
            for (const auto& e : ewm->edges)
                edge_map[e.name] = {e.end1, e.end2};

        // Player-side capsule edge map: the player's body model edges +
        // skeleton edges (mirror of the R2 defender side).
        std::unordered_map<std::string, std::pair<std::string, std::string>> player_edge_map;
        for (const auto& e : assets_->body_model()->edges)
            player_edge_map[e.name] = {e.end1, e.end2};
        for (const auto& [name, e] : assets_->skeleton_edges())
            player_edge_map[name] = {e.end1, e.end2};

        // Player-side capsules: body model (+ helm/head when present).
        std::vector<const resf2::game::BodyCapsule*> player_capsules;
        for (const auto& c : assets_->body_model()->capsules)
            player_capsules.push_back(&c);
        if (assets_->helm_model())
            for (const auto& c : assets_->helm_model()->capsules)
                player_capsules.push_back(&c);
        if (player_capsules.empty()) return enemy_distance_hit_ok();
        const float pcx = player_pos_x_;
        const float pcy = player_pos_y_ + y_adjust_smoothed_;

        for (const auto& edge_name : move->attack_edges) {
            if (edge_name.empty()) continue;
            const auto eit = edge_map.find(edge_name);
            if (eit == edge_map.end()) continue;
            float atk_radius = 0.0f;
            if (const auto se = assets_->skeleton_edges().find(edge_name);
                se != assets_->skeleton_edges().end())
                atk_radius = se->second.radius;
            else if (ewm)
                for (const auto& we : ewm->edges)
                    if (we.name == edge_name) { atk_radius = we.radius; break; }

            float atk1_wx, atk1_wy, atk2_wx, atk2_wy;
            if (ewm && ewm->macro_nodes.count(eit->second.first)) {
                if (!resolve_enemy_weapon_vertex(
                        *ewm, eit->second.first, atk1_wx, atk1_wy) ||
                    !resolve_enemy_weapon_vertex(
                        *ewm, eit->second.second, atk2_wx, atk2_wy))
                    continue;
            } else {
                auto [ax, ay] = resolve_enemy_node(eit->second.first);
                auto [bx, by] = resolve_enemy_node(eit->second.second);
                atk1_wx = ax; atk1_wy = ay;
                atk2_wx = bx; atk2_wy = by;
            }
            for (const auto* capsule : player_capsules) {
                const auto cap_it = player_edge_map.find(capsule->edge_name);
                if (cap_it == player_edge_map.end()) continue;
                auto [p1_wx, p1_wy] = resolve_body_node(
                    cap_it->second.first, pcx, pcy, facing_right_, pivot_ly);
                auto [p2_wx, p2_wy] = resolve_body_node(
                    cap_it->second.second, pcx, pcy, facing_right_, pivot_ly);
                float body_r = (capsule->radius1 + capsule->radius2) * 0.5f;
                if (body_r <= 0) body_r = 4.0f;
                // Segment-segment closest distance (same law as R2).
                float ex = atk2_wx - atk1_wx, ey = atk2_wy - atk1_wy;
                float fx = p2_wx - p1_wx, fy = p2_wy - p1_wy;
                float gx = atk1_wx - p1_wx, gy = atk1_wy - p1_wy;
                float a = ex * ex + ey * ey;
                float b = ex * fx + ey * fy;
                float c = fx * fx + fy * fy;
                float d = ex * gx + ey * gy;
                float e = fx * gx + fy * gy;
                float det = a * c - b * b;
                float s, t;
                if (det < 1e-12f) {
                    s = 0.0f;
                    t = (b > c) ? d / b : e / c;
                    t = std::max(0.0f, std::min(1.0f, t));
                } else {
                    s = (b * e - c * d) / det;
                    t = (a * e - b * d) / det;
                    if (s < 0) { s = 0; t = e / c; t = std::max(0.0f, std::min(1.0f, t)); }
                    else if (s > 1) { s = 1; t = (b + e) / c; t = std::max(0.0f, std::min(1.0f, t)); }
                    else if (t < 0) { t = 0; s = -d / a; s = std::max(0.0f, std::min(1.0f, s)); }
                    else if (t > 1) { t = 1; s = (b - d) / a; s = std::max(0.0f, std::min(1.0f, s)); }
                }
                float px = atk1_wx + s * ex, py = atk1_wy + s * ey;
                float qx = p1_wx + t * fx, qy = p1_wy + t * fy;
                float rx = px - qx, ry = py - qy;
                float threshold = atk_radius + body_r;
                if (rx * rx + ry * ry < threshold * threshold) return true;
            }
        }
        return false;
    }
    // [H08] The documented distance fallback (the move's authored tactic
    // reach, Max=250 for HighPunch — partially asset-backed per K06; the
    // original is model-edge based, HARDCODE_AUDIT H08).
    bool enemy_distance_hit_ok() const {
        const float dist = std::fabs(enemy_pos_x_ - player_pos_x_);
        return dist <= 250.0f;
    }

    // Map weapon tactic name to model file path.
    // Tactic names like "Swords", "Axes", "Claws" map to "weapon_swords.xml" etc.
    // Returns empty string if no model file exists for this tactic.
    std::string weapon_tactic_to_model_file(const std::string& tactic) const {
        // [H02] The list.xml Model attr is the ONLY legitimate file-name
        // source (LIVE_GAME_EVIDENCE Q1: "Model attribute is lowercase item
        // id, file name matches Model + '.xml'"). The static map below
        // invented 47 names (HARDCODE_AUDIT H02: WandererStaff->
        // weapon_staff.xml, Shuriken->weapon_knives.xml, ...). Resolve the
        // subtype through list.xml FIRST:
        //   pass 1: an OWNED item of this subtype (the J/U cycle is built
        //           from owned items, R4b) — its Model is authoritative;
        //   pass 2: any subtype item whose model file actually ships.
        // The static map stays only for subtypes with no list entry.
        if (list_data_loaded_) {
            const std::vector<std::string> owned = inventory_.all_items();
            auto file_exists = [&](const std::string& f) {
                for (const auto& p : model_paths(asset_root_, f.c_str()))
                    if (std::filesystem::exists(p)) return true;
                return false;
            };
            for (const auto& li : list_data_.items) {
                if (li.subtype != tactic || li.model.empty()) continue;
                if (std::find(owned.begin(), owned.end(), li.name) != owned.end())
                    return li.model + ".xml";
            }
            for (const auto& li : list_data_.items) {
                if (li.subtype != tactic || li.model.empty()) continue;
                if (file_exists(li.model + ".xml"))
                    return li.model + ".xml";
            }
        }
        // Direct file name: tactic name lowercase + "s" for plurals
        std::string lower;
        for (char c : tactic) lower += (char)std::tolower(c);
        // Handle special mappings
        static const std::unordered_map<std::string, std::string> special = {
            {"Fists", ""},         // Unarmed — no weapon model
            {"TwoHanded", "weapon_composite_sword.xml"},
            {"BigSwords", "weapon_big_swords.xml"},
            {"CompositeSword", "weapon_composite_sword.xml"},
            {"CompositeSpear", "weapon_composite_spear.xml"},
            {"CompositeStaff", "weapon_composite_staff.xml"},
            {"CompositeScythe", "weapon_composite_scythe.xml"},
            {"GiantSword", "weapon_giant_sword.xml"},
            {"PowerFists", "weapon_power_fists.xml"},
            {"Glaivebow", "weapon_glaivebow.xml"},
            {"SilverGlaive", "weapon_silver_glaive.xml"},
            {"OneHandedSword", "weapon_one_handed_sword.xml"},
            {"NinjaSword", "weapon_ninja_sword.xml"},
            {"ShogunKatana", "weapon_katana.xml"},
            {"WandererStaff", "weapon_staff.xml"},
            {"TonfaGuns", "weapon_tonfa_guns.xml"},
            {"SharpTonfa", "weapon_sharp_tonfa.xml"},
            {"SteelClaws", "weapon_steel_claws.xml"},
            {"ShockerClaws", "weapon_shocker_claws.xml"},
            {"ButcherKnives", "weapon_butcher_knives.xml"},
            {"CrescentKnives", "weapon_crescent_knives.xml"},
            {"ElectroHammers", "weapon_electro_hammers.xml"},
            {"FireBatons", "weapon_fire_batons.xml"},
            {"BattleHammers", "weapon_battle_hammers.xml"},
            {"TwoHandedBlunt", "weapon_two_handed_cudgel.xml"},
            {"HermitSwords", "weapon_hermit_swords.xml"},
            {"Knobsticks", "weapon_knobsticks.xml"},
            {"MagariYari", "weapon_magari_yari.xml"},
            {"ShuangGou", "weapon_shuang_gou.xml"},
            {"ChineseSabers", "weapon_chinese_sabers.xml"},
            {"IndianKatar", "weapon_indian_katar.xml"},
            {"MonkKatars", "weapon_indian_katar.xml"},
            {"Shuriken", "weapon_knives.xml"},
            {"Kunai", "weapon_kunai.xml"},
            {"FireBall", "magic_fireball.xml"},
            {"Energyball", "magic_energy_ball.xml"},
            {"LightningArrow", "magic_lightning.xml"},
            {"MagicDeathRay", "magic_death_ray.xml"},
            {"MagicAsteroid", "magic_asteroid.xml"},
            {"MassBomb", "magic_mass_bomb.xml"},
            {"MagicBomb", "magic_mass_bomb.xml"},
            {"MagicFireAura", "magic_fire_aura.xml"},
            {"MagicAcidCloud", "magic_fire_aura.xml"},
            {"RootStun", "magic_root_stun.xml"},
            {"FirePillar", "magic_fire_pillar.xml"},
            {"Sawblade", "weapon_sawblade.xml"},
            {"DoubleScythe", "weapon_sectional_scythe.xml"},
        };
        auto it = special.find(tactic);
        if (it != special.end()) return it->second;
        // Generic: "weapon_<lowercased>.xml" — try common patterns
        // Handle s-ending (Swords → sword, Axes → axe, etc.)
        // [P1] The exists-probe used to hardcode asset_root_ + "/assets/models/"
        // (wrong when asset_root_ is the repo root, so the plural file
        // weapon_knives.xml never matched and the guess fell through to the
        // singular "weapon_knive.xml" — the soak's NOT FOUND line). Probe the
        // same search paths the loader itself uses.
        auto exists_anywhere = [&](const std::string& f) {
            for (const auto& p : model_paths(asset_root_, f.c_str()))
                if (std::filesystem::exists(p)) return true;
            return false;
        };
        std::string try_name = "weapon_" + lower + ".xml";
        // Try with and without final 's'
        if (exists_anywhere(try_name)) return try_name;
        if (lower.size() > 1 && lower.back() == 's') {
            try_name = "weapon_" + lower.substr(0, lower.size()-1) + ".xml";
            if (exists_anywhere(try_name)) return try_name;
        }
        return try_name; // return best guess
    }

    // [ORIGINAL] The weapon MODEL FILE is the equipped item's `Model`
    // attribute from list.xml — the device ships
    //   <Item Name="WEAPON_KNIVES" SubType="Knives" Model="weapon_knives" .../>
    // and the file on disk is exactly `Model + ".xml"` (LIVE_GAME_EVIDENCE
    // Q1: weapon_knives.xml exists, weapon_knive.xml does not). The old
    // loader guessed the filename from the SUBTYPE and produced
    // "weapon_knive.xml" — the soak's "Player weapon 'Knives' model NOT
    // FOUND" — so the equipped weapon was invisible. Falls back to the
    // legacy guess for items without a Model attr and for the J/U
    // weapon-cycle path (tactic names that match no inventory item).
    std::string equipped_weapon_model_file(const std::string& tactic) const {
        if (list_data_loaded_) {
            const std::string inv = inventory_.equipped_weapon();
            if (!inv.empty()) {
                for (const auto& item : list_data_.items) {
                    if (item.name == inv && !item.model.empty() &&
                        (tactic.empty() || item.subtype == tactic))
                        return item.model + ".xml";
                }
            }
        }
        return weapon_tactic_to_model_file(tactic);
    }

    // [P3] Equipped armor/helm model files from the list.xml Model attribute
    // (users.xml Armor="ARMOR_ROBE" -> <Item Model="armor_robe"> ->
    // armor_robe.xml; Helm="Head" -> Model="head" -> head.xml; Q4). Empty
    // string = nothing equipped to load.
    std::string equipped_armor_model_file() const {
        if (!list_data_loaded_) return {};
        const std::string id = inventory_.equipped_armor();
        if (id.empty()) return {};
        for (const auto& item : list_data_.items)
            if (item.name == id && !item.model.empty()) return item.model + ".xml";
        return {};
    }
    std::string equipped_helm_model_file() const {
        if (!list_data_loaded_) return {};
        const std::string id = inventory_.equipped_helmet();
        if (id.empty()) return {};
        for (const auto& item : list_data_.items)
            if (item.name == id && !item.model.empty()) return item.model + ".xml";
        return {};
    }
    void load_equipment_models() {
        if (!assets_) return;
        // [R4] The default save equips Armor="Body" Helm="Head" — and those
        // list.xml items (ShopHide=1, Hidden=1) ARE the naked fighter's own
        // models (Model="body" -> body.xml, Model="head" -> head.xml). The
        // armor/helm overlay pass must not re-draw them: the base body/head
        // already render in render_body_model, so overlaying the same
        // geometry in the armor tone drew the whole fighter twice — "тело
        // непойми как, голова вытянута" (re-soak-4) while the enemy (no
        // armor pass) looked right. Only REAL armor/helm items (e.g.
        // ARMOR_ROBE -> armor_robe.xml) draw an overlay.
        const std::string armor = equipped_armor_model_file();
        if (armor.empty() || armor == "body.xml")
            assets_->armor_model().reset();
        else assets_->load_armor_model(armor, asset_root_);
        const std::string helm = equipped_helm_model_file();
        if (helm.empty() || helm == "head.xml")
            assets_->helm_model().reset();
        else assets_->load_helm_model(helm, asset_root_);
    }

    // [R2] Load the ENEMY fighter's own body/head models per the battle
    // setup. battle_info_.enemy_name resolves to a stages.xml <Template>
    // (by Name or FirstName); the template's <Items> are equipment names
    // (BODY_KENJI, HEAD_DISCIPLE, ...); list.xml maps each to a model file
    // (body_kenji.xml, head_disciple.xml — LIVE_GAME_EVIDENCE Q2-C: the
    // defender side is the enemy fighter's own model edges). The battle hit
    // test runs the attacker's attack edges against THIS model's capsules.
    // Idempotent: resets both slots, then loads what the template names.
    // No template match (generic "enemy" in tests, dojo sparring) leaves
    // the slots empty and the hit test falls back to the player's body
    // model — the pre-R2 behavior.
    void load_enemy_fighter_models() {
        if (!assets_) return;
        assets_->enemy_body_model().reset();
        assets_->enemy_head_model().reset();
        // [H06] The enemy WEAPON is part of the same template resolution —
        // the stages.xml <Items> name the loadout (WEAPON_SWORDS, Fists...)
        // and list.xml maps it to a Model file. The old
        // load_enemy_weapon("weapon_knuckles.xml") loaded the SAME weapon
        // for every battle (HARDCODE_AUDIT H06).
        assets_->enemy_weapon_model().reset();
        enemy_weapon_file_.clear();
        enemy_weapon_subtype_ = "Fists";
        enemy_template_resolved_ = false;
        if (!assets_->stages_loaded() || !list_data_loaded_) return;
        const auto& templates = assets_->stage_data().templates;
        const std::string& name = battle_info_.enemy_name;
        const fmt::StageTemplate* tmpl = nullptr;
        // [U1] The dojo's own <Warrior> rows parse as templates with an EMPTY
        // name/first_name; matching an empty battle name against them resolved
        // a phantom template (resolved=1, weapon file "") and starved the
        // weapon_knuckles.xml fallback the dojo render needs (the enemy
        // weapon model never loaded — U1 soak: 0 nodes).
        if (!name.empty()) {
            for (const auto& t : templates) {
                if (t.name == name || t.first_name == name) { tmpl = &t; break; }
            }
        }
        if (!tmpl) return;
        enemy_template_resolved_ = true;
        for (const auto& item : tmpl->items) {
            std::string model_file;
            std::string item_type;
            std::string item_subtype;
            for (const auto& li : list_data_.items) {
                if (li.name == item && !li.model.empty()) {
                    model_file = li.model + ".xml";
                    item_type = li.type;
                    item_subtype = li.subtype;
                    break;
                }
            }
            if (model_file.empty()) continue;
            // Body/head model files by name prefix (reverse/data ships
            // body_*.xml / head_*.xml per fighter; the skeleton item and
            // armor items are not hit-test targets).
            if (model_file.rfind("body_", 0) == 0)
                assets_->load_enemy_body_model(model_file, asset_root_);
            else if (model_file.rfind("head_", 0) == 0)
                assets_->load_enemy_head_model(model_file, asset_root_);
            // [H06] The weapon slot: the template's weapon/magic/ranged
            // item (list.xml Type) maps through its Model attr. "Fists"
            // (Type=Weapon, no Model) resolves to NO weapon — the disciple
            // is unarmed, exactly like the original.
            else if (item_type == "Weapon" || item_type == "Magic" ||
                     item_type == "Ranged") {
                enemy_weapon_file_ = model_file;
                enemy_weapon_subtype_ = item_subtype.empty()
                                           ? "Fists" : item_subtype;
                load_enemy_weapon(enemy_weapon_file_);
                break;
            }
        }
    }
    // Load a weapon model for the player from a tactic name.
    // The weapon model is stored in assets_->weapon_model() for rendering.
    void load_player_weapon(const std::string& tactic) {
        std::string model_file = equipped_weapon_model_file(tactic);
        if (model_file.empty()) {
            assets_->weapon_model().reset();
            return;  // Fists — no weapon model
        }
        auto candidates = model_paths(asset_root_, model_file.c_str());
        std::string fig_path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
        }
        if (fig_path.empty()) {
            std::printf("  Player weapon '%s' model NOT FOUND (tried: %s)!\n",
                       tactic.c_str(), model_file.c_str());
            assets_->weapon_model().reset();
            return;
        }
        assets_->weapon_model() = std::make_unique<BodyModel>();
        fmt::XmlDocument doc;
        if (!doc.parse(read_text(fig_path))) {
            std::fprintf(stderr, "[weapon] parse error for %s: %s\n",
                        model_file.c_str(), doc.error().c_str());
            assets_->weapon_model().reset();
            return;
        }
        auto* scene = doc.root()->first_child("Scene");
        if (!scene) { assets_->weapon_model().reset(); return; }

        // Parse MacroNodes (weapons use MacroNode type, unlike body.xml which uses Node/COM)
        if (auto* ns = scene->first_child("Nodes")) {
            for (const auto& child : ns->children) {
                std::string type = child.attr("Type");
                if (type == "MacroNode") {
                    BodyMacroNode mn;
                    mn.name = child.name;
                    mn.children[0] = child.attr("ChildNode1");
                    mn.children[1] = child.attr("ChildNode2");
                    mn.children[2] = child.attr("ChildNode3");
                    mn.children[3] = child.attr("ChildNode4");
                    // [R1] The LCC weights are what turn the Weapon-Node*
                    // children into the rendered mesh position; without them
                    // every MacroNode resolves to (0,0) and the weapon never
                    // draws (the re-soak-3 "weapon invisible" report).
                    mn.lcc[0] = tof(child.attr("LCC1"));
                    mn.lcc[1] = tof(child.attr("LCC2"));
                    mn.lcc[2] = tof(child.attr("LCC3"));
                    mn.lcc[3] = tof(child.attr("LCC4"));
                    assets_->weapon_model()->macro_nodes[mn.name] = mn;
                }
                // Also store basic position info for rendering
                BodyNode n;
                n.name = child.name;
                n.x = tof(child.attr("X"));
                n.y = tof(child.attr("Y"));
                n.mass = tof(child.attr("Mass"), 1.0f);
                n.fixed = (toi(child.attr("Fixed")) != 0);
                assets_->weapon_model()->nodes[n.name] = n;
            }
        }
        if (auto* es = scene->first_child("Edges")) {
            for (const auto& child : es->children) {
                if (child.attr("Type") != "Edge") continue;
                BodyEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                assets_->weapon_model()->edges.push_back(e);
            }
        }
        if (auto* fs = scene->first_child("Figures")) {
            for (const auto& child : fs->children) {
                if (child.attr("Type") == "Capsule") {
                    BodyCapsule c;
                    c.edge_name = child.attr("Edge");
                    c.radius1 = tof(child.attr("Radius1"));
                    c.radius2 = tof(child.attr("Radius2"));
                    c.margin1 = tof(child.attr("Margin1"));
                    c.margin2 = tof(child.attr("Margin2"));
                    assets_->weapon_model()->capsules.push_back(c);
                } else if (child.attr("Type") == "Triangle") {
                    // [U1] Weapon figures are Triangles (no capsules, no
                    // edges); without them nothing could be rendered.
                    BodyTriangle t;
                    t.n1 = child.attr("Node1");
                    t.n2 = child.attr("Node2");
                    t.n3 = child.attr("Node3");
                    assets_->weapon_model()->triangles.push_back(t);
                }
            }
        }
        std::printf("  Player weapon '%s' (%s): %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                    tactic.c_str(), model_file.c_str(),
                    assets_->weapon_model()->nodes.size(), assets_->weapon_model()->edges.size(),
                    assets_->weapon_model()->capsules.size(),
                    assets_->weapon_model()->triangles.size());
    }

    // Initialize Verlet physics state from the bag's skeleton nodes.
    // Each node gets: position = (x, y), prev_position = (x, y) (at rest).
    // Fixed nodes (Fixed="1") have inv_mass = 0 and don't move.
    // Edges become distance constraints with rest length = edge.length.
    void init_bag_verlet() {
        if (!assets_->bag_model()) return;
        bag_verlet_.clear();
        bag_constraints_.clear();
        // World position of the bag's NPivot (where it hangs in the world)
        // Same coordinate system as player — no Y-invert, use params Y directly
        // with the same -45 offset to align with the floor.
        float bag_cx = location_ ? (location_->enemy_x - 983.0f) : 0.0f;
        float bag_cy = location_ ? (location_->enemy_y + 81.0f) : 0.0f;
        auto pit = assets_->bag_model()->nodes.find("NPivot");
        float pivot_ly = pit != assets_->bag_model()->nodes.end() ? pit->second.y : 109.0f;
        // Initialize nodes: world position = bag_center + (node_local - NPivot_local)
        for (auto& [name, n] : assets_->bag_model()->nodes) {
            VerletNode vn;
            vn.x = bag_cx + n.x * 1.0f;
            vn.y = bag_cy + (n.y - pivot_ly) * 1.0f;
            vn.px = vn.x;  // at rest, prev = current
            vn.py = vn.y;
            vn.mass = n.mass;
            vn.fixed = n.fixed;
            vn.inv_mass = n.fixed ? 0.0f : (n.mass > 0.001f ? 1.0f / n.mass : 1.0f);
            vn.attenuation = n.attenuation;
            bag_verlet_[name] = vn;
        }
        // Initialize constraints from edges
        for (auto& e : assets_->bag_model()->edges) {
            VerletConstraint c;
            c.n1 = e.end1;
            c.n2 = e.end2;
            // Compute rest length from actual node distance (or use edge.length)
            auto n1 = bag_verlet_.find(e.end1);
            auto n2 = bag_verlet_.find(e.end2);
            if (n1 != bag_verlet_.end() && n2 != bag_verlet_.end()) {
                float dx = n1->second.x - n2->second.x;
                float dy = n1->second.y - n2->second.y;
                c.length = std::sqrt(dx*dx + dy*dy);
            } else {
                c.length = e.length;
            }
            c.stiffness = 1.0f;
            bag_constraints_.push_back(c);
        }
        bag_verlet_init_ = true;
        bag_rest_.clear();
        for (const auto& [n, v] : bag_verlet_) bag_rest_[n] = {v.x, v.y};
        // The bag's world box, printed so it can be compared against the
        // fighter's without a screenshot: an attack has to physically reach it.
        float bx0 = 0, bx1 = 0, by0 = 0, by1 = 0;
        bool any = false;
        for (const auto& [n, v] : bag_verlet_) {
            (void)n;
            if (!any) { bx0 = bx1 = v.x; by0 = by1 = v.y; any = true; }
            bx0 = std::min(bx0, v.x); bx1 = std::max(bx1, v.x);
            by0 = std::min(by0, v.y); by1 = std::max(by1, v.y);
        }
        int collisible = 0;
        for (const auto& e : assets_->bag_model()->edges)
            if (e.collisible && e.radius > 0) ++collisible;
        std::printf("  Bag Verlet: %zu nodes, %zu constraints (Node12 fixed)\n",
                    bag_verlet_.size(), bag_constraints_.size());
        int fixed_nodes = 0;
        std::string fixed_names;
        for (const auto& [n, v] : bag_verlet_)
            if (v.fixed) { ++fixed_nodes; fixed_names += n + " "; }
        // A node that no constraint touches is in free fall: gravity acts on it
        // and nothing pulls it back. It never settles, and it drags anything
        // measured from the node set (including the collision segments) with it.
        std::string orphans;
        for (const auto& [n, v] : bag_verlet_) {
            if (v.fixed) continue;
            bool referenced = false;
            for (const auto& c : bag_constraints_)
                if (c.n1 == n || c.n2 == n) { referenced = true; break; }
            if (!referenced) orphans += n + " ";
        }
        // Such a node is not part of the rope: "COM" is the model's centre-of-
        // mass marker. Integrating it makes it fall forever (terminal velocity
        // ~12 world units per frame with Attenuation=0.02), which corrupts
        // anything measured over the bag's node set. Freeze them instead.
        if (!orphans.empty()) {
            std::printf("[BAG] unconstrained nodes frozen (were in free fall): %s\n",
                        orphans.c_str());
            for (auto& [n, v] : bag_verlet_) {
                if (v.fixed) continue;
                bool referenced = false;
                for (const auto& c : bag_constraints_)
                    if (c.n1 == n || c.n2 == n) { referenced = true; break; }
                if (!referenced) { v.fixed = true; v.inv_mass = 0.0f; }
            }
        }
        std::printf("[BAG] world box x=%.0f..%.0f y=%.0f..%.0f  collisible edges=%d"
                    "  floor=%.0f  fixed=%d [%s]\n",
                    bx0, bx1, by0, by1, collisible, floor_world_y_,
                    fixed_nodes, fixed_names.c_str());
    }

    // Apply an impulse to a bag node (called when hit).
    // Impulse = instantaneous velocity change = position offset added to prev pos.
    // In Verlet: vel = (pos - prev), so to add velocity v, set prev -= v * dt.
    void apply_bag_impulse(const std::string& node_name, float vx, float vy) {
        auto it = bag_verlet_.find(node_name);
        if (it == bag_verlet_.end()) return;
        auto& n = it->second;
        if (n.fixed) return;
        // Original Bl.strike: node.ma += impulse / node.weight
        // where ma = current position, weight = XML Mass attribute.
        // Modifying current position (x) directly — NOT prev (px).
        // In Verlet: x += delta => velocity += delta for next frame.
        n.x += vx * n.inv_mass;
        n.y += vy * n.inv_mass;
    }

    // Update bag Verlet physics.
    // 1. Integration: pos_new = 2*pos - prev + acc*dt^2 (gravity + damping)
    // 2. Satisfy constraints (multiple iterations for stiffness)
    // 3. Apply damping (attenuation)
    void update_bag_verlet(float dt) {
        if (!bag_verlet_init_ || !assets_->bag_model()) return;
        const float GRAVITY = -800.0f;  // downward acceleration (heavier bag)
        const int CONSTRAINT_ITERATIONS = 10;
        // 1. Verlet integration
        for (auto& [name, n] : bag_verlet_) {
            if (n.fixed) continue;
            // Verlet: new_pos = pos + (pos - prev) * (1 - attenuation) + acc * dt^2
            float vx = (n.x - n.px) * (1.0f - n.attenuation);
            float vy = (n.y - n.py) * (1.0f - n.attenuation);
            n.px = n.x;
            n.py = n.y;
            n.x += vx;
            n.y += vy + GRAVITY * dt * dt;
        }
        // 2. Satisfy distance constraints
        int skipped_missing = 0, skipped_wsum = 0, applied = 0;
        for (int iter = 0; iter < CONSTRAINT_ITERATIONS; ++iter) {
            for (auto& c : bag_constraints_) {
                auto n1 = bag_verlet_.find(c.n1);
                auto n2 = bag_verlet_.find(c.n2);
                if (n1 == bag_verlet_.end() || n2 == bag_verlet_.end()) { ++skipped_missing; continue; }
                auto& a = n1->second;
                auto& b = n2->second;
                float dx = b.x - a.x;
                float dy = b.y - a.y;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 0.0001f) continue;
                float diff = (dist - c.length) / dist;
                float w1 = a.inv_mass;
                float w2 = b.inv_mass;
                float wsum = w1 + w2;
                if (wsum < 0.0001f) { ++skipped_wsum; continue; }
                ++applied;
                float f = c.stiffness * diff;
                a.x += dx * f * (w1 / wsum);
                a.y += dy * f * (w1 / wsum);
                b.x -= dx * f * (w2 / wsum);
                b.y -= dy * f * (w2 / wsum);
            }
        }
        if (dump_state_ && (bag_diag_ticks_++ % 120) == 0) {
            float worst = 0.0f;
            std::string worst_name;
            for (const auto& c : bag_constraints_) {
                auto n1 = bag_verlet_.find(c.n1);
                auto n2 = bag_verlet_.find(c.n2);
                if (n1 == bag_verlet_.end() || n2 == bag_verlet_.end()) continue;
                const float dx = n2->second.x - n1->second.x;
                const float dy = n2->second.y - n1->second.y;
                const float d = std::sqrt(dx * dx + dy * dy) - c.length;
                if (std::abs(d) > std::abs(worst)) { worst = d; worst_name = c.n1 + "->" + c.n2; }
            }
            std::printf("[BAGSOLVE] constraints=%zu applied=%d skipped_missing=%d "
                        "skipped_wsum=%d worst_violation=%.1f (%s) dt=%.4f\n",
                        bag_constraints_.size(), applied, skipped_missing, skipped_wsum,
                        worst, worst_name.c_str(), dt);
        }
    }
    int bag_diag_ticks_ = 0;
    // Rest positions captured at init, so displacement from rest is measurable.
    // bag_angle_ belongs to an older pendulum model that the Verlet path never
    // writes — it stays 0.0 no matter how hard the bag is hit, which made it
    // look for two sessions as though the bag was never touched.
    std::unordered_map<std::string, std::pair<float, float>> bag_rest_;

    // Largest distance any bag node has moved from its rest position.
    float bag_displacement() const {
        float worst = 0.0f;
        for (const auto& [n, v] : bag_verlet_) {
            auto it = bag_rest_.find(n);
            if (it == bag_rest_.end()) continue;
            const float dx = v.x - it->second.first;
            const float dy = v.y - it->second.second;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > worst) worst = d;
        }
        return worst;
    }

    void render_punching_bag() {
        if (!assets_->bag_model() || !location_) return;
        
        // Bag position: enemy_x from params.xml, adjusted to world space
        float bag_cx = location_->enemy_x - 983.0f;
        
        // Bag NPivot Y in model space = 109.0
        // The bag hangs from Node12 (Y=335) which is fixed at ceiling
        // Node12 world Y should be at ceiling level.
        // Ceiling (layer_5) is at params y=-202 → world_y = +202 (inverted).
        // Node12 local Y = 335, NPivot local Y = 109.
        // Node12 world Y = bag_cy + (335 - 109) = bag_cy + 226
        // We need Node12 at world Y = 202 (ceiling):
        //   bag_cy + 226 = 202 → bag_cy = -24
        // enemy_y = -105. bag_cy = enemy_y + offset = -105 + 81 = -24. ✓
        //
        // BUT: the bag appears too high. The issue is that the player's
        // y_adjust_smoothed_ adds ~+82 units (REF_FEET_LY - ly_lowest),
        // making the player appear higher. The bag doesn't have this
        // adjustment, so it looks relatively higher.
        //
        // FIX: apply the same y_adjust to the bag's rendering Y, so the
        // bag and player are in the same coordinate space.
        auto pit = assets_->bag_model()->nodes.find("NPivot");
        float pivot_ly = pit != assets_->bag_model()->nodes.end() ? pit->second.y : 109.0f;
        float bag_cy = location_->enemy_y + 81.0f + y_adjust_smoothed_;
        
        // === BAG RENDERING (Verlet physics) ===
        // The bag's skeleton nodes are simulated with Verlet integration.
        // Node12 is fixed (ceiling attachment). Other nodes swing freely
        // subject to gravity + distance constraints (edges).
        // We render capsules using the current Verlet node positions.
        
        // Build edge lookup
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : assets_->bag_model()->edges) {
            edge_map[e.name] = {e.end1, e.end2};
        }
        
        // Render bag as unified silhouette (same approach as character)
        ren::Color4B bag_body_col{35, 35, 40, 255};      // dark neutral for bag body
        ren::Color4B bag_chain_col{160, 160, 160, 255};   // gray for chain
        
        for (auto& c : assets_->bag_model()->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto& en1 = eit->second.first;
            auto& en2 = eit->second.second;
            
            // Get node positions from Verlet state (if available) or fall back to rest pose
            float x1, y1, x2, y2;
            if (bag_verlet_init_) {
                auto v1 = bag_verlet_.find(en1);
                auto v2 = bag_verlet_.find(en2);
                if (v1 == bag_verlet_.end() || v2 == bag_verlet_.end()) continue;
                x1 = v1->second.x;
                y1 = v1->second.y;
                x2 = v2->second.x;
                y2 = v2->second.y;
            } else {
                auto nit1 = assets_->bag_model()->nodes.find(en1);
                auto nit2 = assets_->bag_model()->nodes.find(en2);
                if (nit1 == assets_->bag_model()->nodes.end() || nit2 == assets_->bag_model()->nodes.end()) continue;
                x1 = bag_cx + nit1->second.x * 1.0f;
                y1 = bag_cy + (nit1->second.y - pivot_ly) * 1.0f;
                x2 = bag_cx + nit2->second.x * 1.0f;
                y2 = bag_cy + (nit2->second.y - pivot_ly) * 1.0f;
            }
            
            float r = (c.radius1 + c.radius2) * 0.5f * 1.0f;
            bool is_main = (c.radius1 >= 20 || c.radius2 >= 20);
            
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            
            ren::Color4B col = is_main ? bag_body_col : bag_chain_col;
            float ax = x1 + px*ht, ay = y1 + py*ht;
            float bx = x2 + px*ht, by = y2 + py*ht;
            float cx_ = x2 - px*ht, cy_ = y2 - py*ht;
            float dx_ = x1 - px*ht, dy_ = y1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx_, cy_, col);
            renderer_->draw_filled_triangle_world(ax, ay, cx_, cy_, dx_, dy_, col);
            renderer_->draw_filled_circle_world(x1, y1, ht, col);
            renderer_->draw_filled_circle_world(x2, y2, ht, col);
        }
    }

    // ---------- HUD textures (real game textures) ----------
    // ---------- Animation loading (DYNAMIC: scan directory) ----------
    void load_animations() {
        assets_->load_animations(asset_root_);
    }

    // ---------- Move definitions (from moves.xml) ----------
    void load_moves() {
        if (moves_loaded_) return;
        moves_loaded_ = true;
        boot_events_.push_back("moves.xml");
        assets_->load_moves(asset_root_);
    }

    // ---------- Internal settings (internalSettings.xml) ----------
    //
    // [ORIGINAL] Parses damage-related settings from internalSettings.xml.
    // Binary ref: internalSettings parsing at 0x10291370
    void load_internal_settings() {
        assets_->load_internal_settings(asset_root_);
    }

    // ---------- Enemy AI weights (from tacticSettings.xml) ----------
    // [ORIGINAL] The roulette-wheel weight model - see tactic_settings.hpp.
    void load_tactics() {
        tactics_.load(asset_root_);
        // [D3] Table families (assets/tactics/*.atf, ...) for the
        // TacticDecisionPipeline's stage tables + adapter classification.
        // A missing directory is NOT an error — the set stays partial.
        tactic_tables_.load(asset_root_);
        // Wire tactic settings to combat system for AI decision making
        combat_.set_tactic_settings(&tactics_);
    }

    void update_animation(uint32_t dt_ms);
    void play_animation(const std::string& name, bool loop = true, int priority = 0);
    // [ORIGINAL] Model::alignAnimation @ 0x101661d0 — see game.cpp for the
    // full derivation. Places the model so the <Align><Pivot Part> node keeps
    // its world position across an animation change.
    void apply_align(const std::string& anim_name, const MoveDef* move,
                     int first_frame, float prev_anchor_rel_x,
                     bool prev_anchor_known);
    const MoveDef* current_align_move() const;

    // Find the best matching move from moves.xml for the given input context.
    // Returns nullptr if no move matches. Sets candidate_count to the number
    // of valid candidates found.
    const MoveDef* find_best_move(
        const std::string& cur_direction,
        const std::string& cur_move_type,
        bool block_all_combat,
        bool in_attack,
        bool is_uninterrupt,
        bool past_attack_interval,
        int& candidate_count
    );

    void load_hud_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Search both root/assets/1536/ and root/1536/ for textures
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            load_texture_atlas_to_hud(base/"textures"/"panels"/"top",
                                      "batchPanelsTop");
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"dojo",
                                      "batchButtonsDojo");
            // [ORIGINAL] Load fight HUD textures: health bars, energy, timers.
            load_texture_atlas_to_hud(base/"textures"/"fight"/"bars",
                                      "batchFightBars");
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"fight",
                                      "batchButtonsFight");
            // [ORIGINAL] On-screen controls: the virtual stick (a ring plus a
            // knob plus a highlight) and the attack buttons. The original is a
            // touch game — these are its only controls (PORT_PLAN 6.3).
            load_texture_atlas_to_hud(base/"textures"/"joystick",
                                      "batchJoystick");
            // [ORIGINAL] The dialogue scroll is assembled from loose PNGs in
            // textures/scrolls/common: rolled ends plus a tiled centre, with
            // the paper drawn over it. The speaker portrait comes from
            // image/users/image.
            const auto scrolls = base/"textures"/"scrolls"/"common";
            for (const char* n : {"Roll_left", "Roll_center", "Roll_right",
                                  "Paper_left", "Paper_right", "Shadow_roll"})
                load_hud_png(scrolls / (std::string(n) + ".png"), n);
            // [ORIGINAL] Round indicators are loose PNGs in textures/misc,
            // loaded by name in ScreenModel (0x10201d90 / ctor 0x10200c10).
            load_hud_png(base/"textures"/"misc"/"Round_Done.png", "Round_Done");
            load_hud_png(base/"textures"/"misc"/"Round_Undone.png", "Round_Undone");
            // The dojo's bag/disciple toggle (FUN_1014d5c0; small variant —
            // the full-size art is absent from this dump).
            load_hud_png(base/"textures"/"misc"/"btn_disciple_small.png",
                         "btn_disciple_small");
            load_hud_png(base/"image"/"users"/"image"/"character_sensei_small.png",
                         "character_sensei_small");
            // The full-size avatar used by the story dialogues
            // (<Dialog Image="character_sensei"> in quests.xml).
            load_hud_png(base/"image"/"users"/"image"/"character_sensei.png",
                         "character_sensei");
            // [ORIGINAL] Load hit effect textures: hit_blade (18-frame spark
            // animation), hit labels (Aggressive, Brutal, Critical, etc.)
            load_texture_atlas_to_hud(base/"textures"/"effects"/"fight",
                                      "hit_blade");
            load_texture_atlas_to_hud(base/"textures"/"fight"/"hits",
                                      "hitBatch");
            // [ORIGINAL] Loose icons in textures/misc used by the shop and the
            // item panels. These were being drawn as Unicode glyphs (a sword
            // "\u2694", a diamond "\u25c6", a star "\u2605") standing in for the
            // real art, which is present in the dump.
            const auto misc = base/"textures"/"misc";
            for (const char* n : {"Damage", "Shield", "ruby", "credit",
                                  "energy", "Arrow", "VS"})
                load_hud_png(misc / (std::string(n) + ".png"), n);
            // [Wave 9B] Item icons are loose PNGs in image/ut_items/icon,
            // named by list.xml Image= (weapon_knives, armor_robe, ...).
            // Without them the shop rendered names only, with tinted
            // squares standing in for the art (re-soak-5: "отображает
            // только названия предметов, но не их иконки").
            const auto ut_items = base/"image"/"ut_items"/"icon";
            if (std::filesystem::exists(ut_items)) {
                for (auto& entry : std::filesystem::directory_iterator(ut_items)) {
                    if (entry.path().extension() != ".png") continue;
                    load_hud_png(entry.path(), entry.path().stem().string());
                }
            }
            // [U2] The shop's category tabs (Weapon/Armor/Helmet/
            // Ranged_weapon/Magic + _active/_pushed states) and the
            // Wear/Bag buttons. Without this atlas the shop rendered text
            // glyphs for the tabs.
            load_texture_atlas_to_hud(base/"textures"/"screens"/"shop"/"buttons",
                                      "shopButtons");
            // [U3] The settings screen's row icons + language buttons
            // (batchSettings.plist: sound/music/graphics/controller/location,
            // usbr/rus/... language flags) and the slider pieces
            // (batchSlidersSettings.plist: SettingsEmpty/full/slider).
            // Without them the settings scene rendered a flat navy
            // placeholder panel.
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"menu"/"settings",
                                      "batchSettings");
            load_texture_atlas_to_hud(base/"textures"/"sliders"/"settings",
                                      "batchSlidersSettings");
        }
        std::printf("  HUD textures loaded: %zu\n", assets_->hud_textures().size());
    }

    void load_menu_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Which frames were already in the HUD table before this atlas loaded.
        // The menu frames are then exactly the ones that appeared, and they are
        // moved out by PROVENANCE rather than by name.
        //
        // This used to move every key containing "_normal" / "_active" /
        // "_pushed", which also swallowed btn_punch_normal and btn_kick_normal
        // from the fight-button atlas — so the on-screen attack buttons were
        // silently missing from the HUD table while the code drawing them was
        // already correct. A name filter standing in for provenance.
        std::vector<std::string> before;
        before.reserve(assets_->hud_textures().size());
        for (const auto& [k, v] : assets_->hud_textures()) before.push_back(k);
        std::sort(before.begin(), before.end());

        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"menu"/"screens",
                                      "batchButtonsMenuScreens");
        }
        for (auto it = assets_->hud_textures().begin(); it != assets_->hud_textures().end(); ) {
            if (!std::binary_search(before.begin(), before.end(), it->first)) {
                assets_->menu_textures()[it->first] = std::move(it->second);
                it = assets_->hud_textures().erase(it);
            } else {
                ++it;
            }
        }
        // Load scroll/roll textures for parchment menu UI
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            auto scroll_dir = base/"textures"/"scrolls"/"common";
            for (auto& name : {"MenuRoll_left", "MenuRoll_center", "MenuRoll_right",
                               "Roll_left", "Roll_center", "Roll_right",
                               "Paper_left", "Paper_right", "Shadow_roll"}) {
                auto path = scroll_dir / (std::string(name) + ".png");
                if (std::filesystem::exists(path)) {
                    auto data = read_file(path.string());
                    int w, h, ch;
                    auto* px = stbi_load_from_memory(
                        (const stbi_uc*)data.data(), (int)data.size(), &w, &h, &ch, 4);
                    if (px) {
                        auto tex = std::make_unique<ren::Texture2D>();
                        tex->init_rgba(w, h, px);
                        stbi_image_free(px);
                        assets_->scroll_textures()[name] = std::move(tex);
                    }
                }
            }
        }
        std::printf("  Menu textures loaded: %zu, scroll textures: %zu\n",
                    assets_->menu_textures().size(), assets_->scroll_textures().size());
    }

    void load_texture_atlas_to_hud(
        const std::filesystem::path& dir, const std::string& atlas_name)
    {
        // Every failure below used to be a silent `return`, so an atlas that
        // did not load looked exactly like one that was never asked for. That
        // is how the fight buttons stayed invisible while the code that draws
        // them was already correct.
        auto pp = dir / (atlas_name + ".plist");
        auto pn = dir / (atlas_name + ".png");
        if (!std::filesystem::exists(pp) || !std::filesystem::exists(pn)) {
            std::printf("  [atlas] %s: missing %s\n", atlas_name.c_str(),
                        std::filesystem::exists(pp) ? pn.string().c_str()
                                                    : pp.string().c_str());
            return;
        }
        auto result = plist::parse(read_text(pp.string()));
        if (!result) {
            std::printf("  [atlas] %s: plist did not parse\n", atlas_name.c_str());
            return;
        }
        auto png_data = read_file(pn.string());
        // Use stb_image to decode the PNG so we can crop frames on the CPU.
        int aw, ah, ach;
        auto* atlas_px = stbi_load_from_memory(
            (const stbi_uc*)png_data.data(), (int)png_data.size(),
            &aw, &ah, &ach, 4);
        if (!atlas_px) {
            std::printf("  [atlas] %s: png did not decode (%zu bytes)\n",
                        atlas_name.c_str(), png_data.size());
            return;
        }
        for (auto& [name, idx] : result->name_index) {
            auto& frame = result->frames[idx];
            // Rotated frames: the plist's `frame` rect is the UNROTATED size
            // and the atlas holds it transposed. Same un-rotation as
            // AssetManager::load_atlas — see the derivation there. (This is
            // the sixth place in this codebase where the same formula lives in
            // two copies; they had already drifted, the location loader being
            // the wrong one.)
            int fw = frame.atlas_w;
            int fh = frame.atlas_h;
            auto tex = std::make_unique<ren::Texture2D>();
            std::vector<std::uint8_t> px((size_t)fw * fh * 4);
            for (int y = 0; y < fh; ++y) {
                for (int x = 0; x < fw; ++x) {
                    int sx, sy;
                    if (frame.rotated) {
                        // Un-rotate 90° CCW (no swap, formula A):
                        // dest(x,y) ← source(atlas_x + (fh-1-y), atlas_y + x)
                        sx = frame.atlas_x + (fh - 1 - y);
                        sy = frame.atlas_y + x;
                    } else {
                        sx = frame.atlas_x + x;
                        sy = frame.atlas_y + y;
                    }
                    if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                    int src_idx = (sy * aw + sx) * 4;
                    int dst_idx = (y * fw + x) * 4;
                    px[dst_idx+0] = atlas_px[src_idx+0];
                    px[dst_idx+1] = atlas_px[src_idx+1];
                    px[dst_idx+2] = atlas_px[src_idx+2];
                    px[dst_idx+3] = atlas_px[src_idx+3];
                }
            }
            tex->init_rgba(fw, fh, px.data());
            std::string n = name;
            if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
            assets_->hud_textures()[n] = std::move(tex);
        }
        std::printf("  [atlas] %s: %zu frames from %dx%d\n", atlas_name.c_str(),
                    result->name_index.size(), aw, ah);
        stbi_image_free(atlas_px);
    }

    // Load a standalone PNG into the HUD texture table. The scroll pieces and
    // the character portraits are loose files, not atlas frames.
    void load_hud_png(const std::filesystem::path& path, const std::string& name) {
        if (assets_->hud_textures().count(name)) return;
        if (!std::filesystem::exists(path)) return;
        // stbi_load (the file-based entry point) is not compiled into this
        // build; everything goes through stbi_load_from_memory.
        auto bytes = read_file(path.string());
        if (bytes.empty()) return;
        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load_from_memory(
            reinterpret_cast<const unsigned char*>(bytes.data()),
            static_cast<int>(bytes.size()), &w, &h, &comp, 4);
        if (!px) return;
        auto tex = std::make_unique<ren::Texture2D>();
        tex->init_rgba(w, h, px);
        stbi_image_free(px);
        assets_->hud_textures()[name] = std::move(tex);
    }

    // [ORIGINAL] Dialogue text comes from assets/localizations/<lang>.xml as
    // <Word Title="KEY">text</Word>. The dojo intro line the original shows on
    // first launch is tutorial_move.
    std::string localized(const std::string& key) const {
        auto it = localization_.find(key);
        return it == localization_.end() ? std::string() : it->second;
    }

    void load_localization(const std::string& lang = "rus") {
        auto root = std::filesystem::path(asset_root_);
        for (const auto& base : {root/"assets"/"localizations", root/"localizations"}) {
            auto p = base / (lang + ".xml");
            if (!std::filesystem::exists(p)) continue;
            const std::string xml = read_text(p.string());
            size_t pos = 0;
            while ((pos = xml.find("<Word Title=\"", pos)) != std::string::npos) {
                const size_t key_beg = pos + 13;
                const size_t key_end = xml.find('"', key_beg);
                if (key_end == std::string::npos) break;
                const size_t val_beg = xml.find('>', key_end);
                const size_t val_end = xml.find("</Word>", val_beg);
                if (val_beg == std::string::npos || val_end == std::string::npos) break;
                localization_[xml.substr(key_beg, key_end - key_beg)] =
                    xml.substr(val_beg + 1, val_end - val_beg - 1);
                pos = val_end;
            }
            std::printf("  Localization '%s': %zu strings\n", lang.c_str(),
                        localization_.size());
            return;
        }
        std::printf("  Localization '%s' NOT FOUND\n", lang.c_str());
    }

    // ---------- HUD font ----------
    // [D5] The font follows the session language: an English session must
    // render with the English font (eng/sakkal.fnt), not the Russian one —
    // the soak showed Russian glyphs in an English game. The generic fonts
    // and rus/optima.fnt remain as fallbacks so a missing language font
    // never leaves the game without a font.
    void load_hud_font(const std::string& lang = "eng") {
        auto root = std::filesystem::path(asset_root_);
        const std::string font_lang = lang.empty() ? std::string("eng") : lang;
        std::vector<std::filesystem::path> candidates = {
            root/"assets"/"1536"/"fonts"/font_lang/"sakkal.fnt",
            root/"1536"/"fonts"/font_lang/"sakkal.fnt",
            root/"assets"/"1536"/"fonts"/"rus"/"optima.fnt",
            root/"assets"/"1536"/"fonts"/"obelix.fnt",
            root/"1536"/"fonts"/"rus"/"optima.fnt",
            root/"1536"/"fonts"/"obelix.fnt",
        };
        std::string fnt_path, png_path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) {
                fnt_path = p.string();
                auto png = p.parent_path() / (p.stem().string() + ".png");
                if (!std::filesystem::exists(png)) {
                    auto xml = read_text(fnt_path);
                    auto fp = xml.find("file=\"");
                    if (fp != std::string::npos) {
                        fp += 6;
                        auto end = xml.find('"', fp);
                        std::string png_name = xml.substr(fp, end - fp);
                        auto png2 = p.parent_path() / png_name;
                        if (std::filesystem::exists(png2)) png = png2;
                    }
                }
                if (std::filesystem::exists(png)) {
                    png_path = png.string();
                    break;
                }
            }
        }
        if (fnt_path.empty()) return;
        auto result = font::parse(read_text(fnt_path));
        if (!result) return;
        assets_->hud_font() = std::make_shared<font::ParsedFont>(std::move(*result));
        auto png_data = read_file(png_path);
        auto tex = std::make_unique<ren::Texture2D>();
        if (!tex->init_from_png((const uint8_t*)png_data.data(), png_data.size())) return;
        assets_->hud_font_tex() = std::move(tex);
        std::printf("  HUD font loaded: %s (%zu glyphs)\n",
                    fnt_path.c_str(), assets_->hud_font()->chars.size());
    }

    // [ORIGINAL] Load sound effects from the original mobile assets.
    // SF2 sounds are in assets/sounds/*.wav (16-bit PCM, Marmalade s3eAudio).
    // [S3] Load the WHOLE bank, not a hand-picked subset: the old hardcoded
    // 12-name list omitted the hit sets, so every hit request logged
    // "[audio] Sound not found or invalid: f_pl_hit1/2/3" during the soak.
    // assets/sounds/ carries the full original bank (both m_pl_* and f_pl_*
    // voice sets, hit/attack/jump/death/cough included), matching the APK
    // manifest exactly.
    void load_sounds() {
        auto& eng = aud::AudioEngine::instance();
        eng.init();  // defaults to NullAudioBackend (no OpenAL yet)
        auto root = std::filesystem::path(asset_root_);
        // Search paths for sounds (mobile APK layout + extracted layout)
        std::vector<std::filesystem::path> sound_dirs = {
            root/"assets"/"assets"/"sounds",
            root/"assets"/"sounds",
            root/"sounds",
        };
        std::filesystem::path sound_dir;
        for (const auto& d : sound_dirs) {
            if (std::filesystem::exists(d)) { sound_dir = d; break; }
        }
        if (sound_dir.empty()) {
            std::printf("[audio] sounds dir not found\n");
            return;
        }
        size_t present = 0, loaded = 0;
        for (auto& entry : std::filesystem::directory_iterator(sound_dir)) {
            if (entry.path().extension() != ".wav") continue;
            ++present;
            const std::string name = entry.path().stem().string();
            if (eng.load_sound_file(name, entry.path().string())) ++loaded;
        }
        std::printf("[audio] Loaded %zu/%zu sounds from %s\n",
                    loaded, present, sound_dir.string().c_str());
    }

    // Play a sound by name (no-op if not loaded or backend is null)
    void play_sound(const std::string& name, float volume = 1.0f) {
        // [Soak-fix Wave 9A] F1 test seam: count the real impact sounds as
        // they resolve and play — the contact-hit family m_/f_pl_hitN (the
        // LIVE_INTERACTION_TRACE §4.3 pin: hit sound = m_pl_hit2) plus the
        // older hit1-6.wav / super_hit1-5.wav names. The soak showed hits
        // landing with only the swing voice + "armor".
        if (name.rfind("hit", 0) == 0 || name.rfind("super_hit", 0) == 0 ||
            name.find("_pl_hit") != std::string::npos) {
            ++hit_sound_count_;
            last_hit_sound_ = name;
        }
        // [Soak-fix Wave 9A] F1 test seam: the most recent sound ANY path
        // played (swish/bodyfall/wall3/armor pins).
        last_played_sound_ = name;
        aud::AudioEngine::instance().play(name, volume, false);
    }

    // [S1/S2] Voice-gender sound names. The original's voice sets are
    // m_pl_* (male player) and f_pl_* (female player); <Warrior Voice=>
    // in usersDefault.xml / user.xml / stages.xml selects between them.
    // Anything that is not "Female" resolves to the male set — the
    // shipped saves default to Voice="Male".
    static std::string voice_prefix(const std::string& voice) {
        return (voice == "Female" || voice == "female") ? "f" : "m";
    }
    std::string player_attack_sound(int idx) const {
        return voice_prefix(player_voice_) + "_pl_attack" + std::to_string(idx);
    }
    std::string player_hit_sound(int idx) const {
        return voice_prefix(player_voice_) + "_pl_hit" + std::to_string(idx);
    }
    std::string enemy_attack_sound(int idx) const {
        return voice_prefix(enemy_voice_) + "_pl_attack" + std::to_string(idx);
    }
    // [Soak-fix Wave 9A] F1: the enemy's HURT voice — the contact-hit sound
    // pin (LIVE_INTERACTION_TRACE §4.3: hit sound = m_pl_hit2.wav, the
    // gender-appropriate _pl_hit2 of the DEFENDER's voice set).
    std::string enemy_hit_sound(int idx) const {
        return voice_prefix(enemy_voice_) + "_pl_hit" + std::to_string(idx);
    }

    // Decode one UTF-8 code point at `i`, advancing it. Falls back to CP1251
    // for a lead byte with no continuation, matching render_text.
    std::int32_t next_codepoint(const std::string& text, size_t& i) const {
        const auto b0 = static_cast<std::uint8_t>(text[i]);
        auto cont = [&](size_t k) {
            return i + k < text.size() &&
                   (static_cast<std::uint8_t>(text[i + k]) & 0xC0) == 0x80;
        };
        std::int32_t cp = b0;
        size_t adv = 1;
        if (b0 >= 0xF0 && cont(1) && cont(2) && cont(3)) {
            cp = ((b0 & 0x07) << 18) |
                 ((static_cast<std::uint8_t>(text[i + 1]) & 0x3F) << 12) |
                 ((static_cast<std::uint8_t>(text[i + 2]) & 0x3F) << 6) |
                 (static_cast<std::uint8_t>(text[i + 3]) & 0x3F);
            adv = 4;
        } else if (b0 >= 0xE0 && cont(1) && cont(2)) {
            cp = ((b0 & 0x0F) << 12) |
                 ((static_cast<std::uint8_t>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<std::uint8_t>(text[i + 2]) & 0x3F);
            adv = 3;
        } else if (b0 >= 0xC0 && cont(1)) {
            cp = ((b0 & 0x1F) << 6) |
                 (static_cast<std::uint8_t>(text[i + 1]) & 0x3F);
            adv = 2;
        } else if (b0 >= 0xC0) {
            cp = 0x0410 + (b0 - 0xC0);
        }
        i += adv;
        return cp;
    }

    // Advance width and tallest glyph of `text`. Callers used to measure by
    // iterating bytes, which double-counts every Cyrillic letter in UTF-8 and
    // made anything centred on a localized string come out wrong.
    std::pair<float, float> measure_text(const std::string& text, float scale) const {
        if (!assets_->hud_font()) return {0.0f, 0.0f};
        float w = 0.0f, h = 0.0f;
        for (size_t i = 0; i < text.size(); ) {
            const std::int32_t cp = next_codepoint(text, i);
            auto it = assets_->hud_font()->char_index.find(cp);
            if (it == assets_->hud_font()->char_index.end()) continue;
            const auto& ch = assets_->hud_font()->chars[it->second];
            w += ch.xadvance * scale;
            h = std::max(h, static_cast<float>(ch.height) * scale);
        }
        return {w, h};
    }

    void render_text(const std::string& text, float x, float y,
                     float scale, ren::Color4B color) {
        if (!assets_->hud_font() || !assets_->hud_font_tex()) return;
        float cx = x;
        // Decode UTF-8 to code points. The previous loop treated every byte as
        // a character and mapped 0xC0..0xFF as CP1251 Cyrillic, so a UTF-8
        // string — which is what assets/localizations/*.xml contains — came out
        // as two wrong letters per real letter ("Сначала покажи" rendered as
        // "PePPoCPoP..."). CP1251 input is still handled: a lead byte that is
        // not followed by a continuation byte falls back to the old mapping.
        for (size_t i = 0; i < text.size(); ) {
            const auto b0 = static_cast<std::uint8_t>(text[i]);
            std::int32_t cp = b0;
            size_t adv = 1;
            auto cont = [&](size_t k) {
                return i + k < text.size() &&
                       (static_cast<std::uint8_t>(text[i + k]) & 0xC0) == 0x80;
            };
            if (b0 >= 0xF0 && cont(1) && cont(2) && cont(3)) {
                cp = ((b0 & 0x07) << 18) |
                     ((static_cast<std::uint8_t>(text[i + 1]) & 0x3F) << 12) |
                     ((static_cast<std::uint8_t>(text[i + 2]) & 0x3F) << 6) |
                     (static_cast<std::uint8_t>(text[i + 3]) & 0x3F);
                adv = 4;
            } else if (b0 >= 0xE0 && cont(1) && cont(2)) {
                cp = ((b0 & 0x0F) << 12) |
                     ((static_cast<std::uint8_t>(text[i + 1]) & 0x3F) << 6) |
                     (static_cast<std::uint8_t>(text[i + 2]) & 0x3F);
                adv = 3;
            } else if (b0 >= 0xC0 && cont(1)) {
                cp = ((b0 & 0x1F) << 6) |
                     (static_cast<std::uint8_t>(text[i + 1]) & 0x3F);
                adv = 2;
            } else if (b0 >= 0xC0) {
                cp = 0x0410 + (b0 - 0xC0);  // CP1251 fallback
            }
            i += adv;
            auto it = assets_->hud_font()->char_index.find(cp);
            if (it == assets_->hud_font()->char_index.end()) {
                it = assets_->hud_font()->char_index.find(32);
                if (it == assets_->hud_font()->char_index.end()) continue;
            }
            auto& ch = assets_->hud_font()->chars[it->second];
            if (ch.width > 0 && ch.height > 0) {
                float u0 = (float)ch.x / assets_->hud_font()->common.scale_w;
                float v0 = (float)ch.y / assets_->hud_font()->common.scale_h;
                float u1 = (float)(ch.x + ch.width) / assets_->hud_font()->common.scale_w;
                float v1 = (float)(ch.y + ch.height) / assets_->hud_font()->common.scale_h;
                float px = cx + ch.xoffset * scale;
                float py = y + ch.yoffset * scale;
                float pw = ch.width * scale;
                float ph = ch.height * scale;
                renderer_->draw_textured_quad_screen(
                    *assets_->hud_font_tex(), px, py, pw, ph, u0, v0, u1, v1, color);
            }
            cx += ch.xadvance * scale;
        }
    }

    // ---------- On-screen controls ----------
    //
    // [ORIGINAL] Shadow Fight 2 is a touch game: a virtual stick in the
    // bottom-left corner and attack buttons in the bottom-right are its ONLY
    // controls. The art is shipped —
    //   textures/joystick/batchJoystick.plist   JoystickContainer_norm (ring),
    //                                           Joystick_norm (knob),
    //                                           Highlight_Stick, _action variants
    //   textures/buttons/fight/batchButtonsFight.plist
    //                                           btn_punch/kick/magic/throw,
    //                                           each _normal and _action
    // — and this draws it and feeds the same combat code the keyboard does, so
    // there is one input path and not two.
    struct TouchControls {
        // Geometry, in screen pixels, derived from the viewport like the HUD.
        float stick_cx = 0, stick_cy = 0, stick_r = 0;
        float punch_cx = 0, punch_cy = 0, punch_r = 0;
        float kick_cx = 0, kick_cy = 0, kick_r = 0;
        // Ranged and magic sit further along the same arc. They are drawn only
        // when something is equipped in those slots, which is why the tutorial
        // dojo shows two buttons and a later fight shows four.
        float throw_cx = 0, throw_cy = 0;
        float magic_cx = 0, magic_cy = 0;
        // State for this frame.
        float dir_x = 0, dir_y = 0;   // stick deflection, -1..1
        // The deflection quantised to the original's eight directions.
        bool up = false, down = false, left = false, right = false;
        bool punch = false, kick = false;         // held
        bool punch_pressed = false, kick_pressed = false;  // this frame
    };

    TouchControls touch_layout() const {
        TouchControls c;
        if (!platform_) return c;
        const float w = static_cast<float>(platform_->window_width());
        const float h = static_cast<float>(platform_->window_height());
        // [ORIGINAL] ActionButtons::layout @ 0x10046f40 positions the buttons
        // against a parent at the BOTTOM-RIGHT screen corner, in POINTS of the
        // 768-tall layout space (ui_scale.hpp), sprites anchored at center:
        //
        //     kick ->setPosition(-140,             kick.height * 0.5)
        //     punch->setPosition(-punch.width*0.5, 140)
        //     throw->setPosition(kick.x  - 35, kick.y  + 117)
        //     magic->setPosition(punch.x - 35, punch.y + 117)
        //
        // So the kick button RESTS on the bottom edge 140 points in from the
        // right, the punch button is FLUSH against the right edge 140 points
        // up, and throw/magic step up-and-left from each by (-35, +117).
        // Sprite sizes are in points too: a 206x206 frame of the 1536 atlas
        // (batchButtonsFight.plist) is 103 points across, so punch and kick
        // centers end up ~125 points apart with a clean gap — the earlier
        // "offsets put them on top of each other" reading treated the numbers
        // as 1536-atlas pixels, which is the wrong unit, and the placement
        // measured off screenshots that replaced it is gone too.
        //
        // Slot->button mapping in the ctor 0x10046840: +0x118 kick (tag 10),
        // +0x11c punch (tag 9), +0x120 throw, +0x124 magic. Throw and magic
        // are positioned either way but shown only when the matching slot is
        // equipped, which is why a fresh profile sees two buttons, not four.
        const float pts = ui::points_scale(h);   // screen px per point

        // Sprite sizes come from the textures themselves (atlas px / content
        // scale = points), like the original's getContentSize(); the plist
        // sizes are only the fallback for a missing atlas.
        auto side_pts = [&](const char* n, float fallback_px) {
            float px = fallback_px;
            if (assets_) {
                auto it = assets_->hud_textures().find(n);
                if (it != assets_->hud_textures().end() && it->second &&
                    it->second->height() > 0)
                    px = static_cast<float>(it->second->height());
            }
            return px / ui::kHighTierContentScale;
        };
        const float punch_pts = side_pts("btn_punch_normal", 206.0f);
        const float kick_pts = side_pts("btn_kick_normal", 206.0f);

        // Screen Y grows downward, the original's Y up: y_screen = h - y_pts.
        c.punch_r = punch_pts * 0.5f * pts;
        c.kick_r = kick_pts * 0.5f * pts;
        c.punch_cx = w - punch_pts * 0.5f * pts;
        c.punch_cy = h - 140.0f * pts;
        c.kick_cx = w - 140.0f * pts;
        c.kick_cy = h - kick_pts * 0.5f * pts;
        c.throw_cx = c.kick_cx - 35.0f * pts;
        c.throw_cy = c.kick_cy - 117.0f * pts;
        c.magic_cx = c.punch_cx - 35.0f * pts;
        c.magic_cy = c.punch_cy - 117.0f * pts;

        // [ORIGINAL] The stick ring's radius is the texture's own half-width
        // in points: Stick::updateGeometry @ 0x10232910 takes
        // JoystickContainer_norm.getContentSize().width * 0.5 (470 atlas px ->
        // 235 points -> radius 117.5). The knob clamp and grab radii all
        // derive from it there too.
        // [HEURISTIC-TODO] The ring's POSITION is not reversed yet — the Stick
        // node is placed by its parent, which this pass did not chase down.
        // Bottom-left with a small margin matches the reference screenshots.
        c.stick_r = side_pts("JoystickContainer_norm", 470.0f) * 0.5f * pts;
        c.stick_cx = c.stick_r * 1.05f;
        c.stick_cy = h - c.stick_r * 1.05f;
        return c;
    }

    // Reads the pointers and fills touch_. Called once per gameplay frame
    // before the combat code looks at the keyboard.
    void update_touch_controls(const plat::InputState& input) {
        const TouchControls geom = touch_layout();
        TouchControls c = geom;
        const bool was_punch = touch_.punch, was_kick = touch_.kick;

        auto inside = [](const plat::PointerState& p, float cx, float cy, float r) {
            const float dx = p.x - cx, dy = p.y - cy;
            return dx * dx + dy * dy <= r * r;
        };
        for (const auto& p : input.pointers) {
            // An unused pointer slot keeps id = -1 with coordinates (0, 0),
            // which lands inside the stick's quadrant, so skip it. (This is a
            // guard, not a fix for an observed defect: the deflected knob in
            // an early capture was the user actually holding the stick.)
            if (p.id < 0) continue;
            if (!p.pressed) continue;
            // The stick reacts to a pointer anywhere in its quadrant, not just
            // on the ring — that is how the original behaves and it is what
            // makes it usable without looking.
            if (p.x < geom.stick_cx + geom.stick_r * 2.0f &&
                p.y > geom.stick_cy - geom.stick_r * 2.0f) {
                float dx = (p.x - geom.stick_cx) / geom.stick_r;
                float dy = (p.y - geom.stick_cy) / geom.stick_r;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 1.0f) { dx /= len; dy /= len; }
                c.dir_x = dx;
                c.dir_y = dy;
            }
            // [ORIGINAL] The clickable radius is 1.25x the sprite's half-width:
            // every fight button registers width * 0.5 * 1.25 as its touch
            // radius (FUN_1006be90, called from the button ctor 0x10046620).
            constexpr float kHitRadiusFactor = 1.25f;
            if (inside(p, geom.punch_cx, geom.punch_cy, geom.punch_r * kHitRadiusFactor))
                c.punch = true;
            if (inside(p, geom.kick_cx, geom.kick_cy, geom.kick_r * kHitRadiusFactor))
                c.kick = true;
        }
        c.punch_pressed = c.punch && !was_punch;
        c.kick_pressed = c.kick && !was_kick;

        // [ORIGINAL] Quantise the deflection the way the original does. The two
        // numbers come out of assets/internalSettings.xml:
        //
        //     <ControllerGripRelativeRadius Value="0.5" />
        //     <ControllerPrimaryAngle       Value="55"  />
        //
        // The first is the dead zone as a fraction of the ring's radius: the
        // knob has to travel HALF the ring before any direction registers.
        // The second says the eight directions are NOT equal 45-degree
        // sectors — each of the four primary directions owns 55 degrees, and
        // the four diagonals share what is left, 35 degrees each. That is what
        // makes the original feel decisive: it takes a deliberate push to get a
        // diagonal, so "forward" does not turn into "forward+up" on the way.
        //
        // This used to be a per-axis threshold of 0.4, which is a SQUARE dead
        // zone with equal 90-degree quadrants: a diagonal push fired both axes
        // at once and every primary direction was one careless degree away from
        // becoming a diagonal.
        //
        // Binary confirmation: Stick::updateGeometry @ 0x10232910 computes the
        // grab radius as ring_radius * grip (grip read via FUN_1005ae90), and
        // the Stick ctor @ 0x102320e0 clamps the primary angle to [0, 90] and
        // precomputes cos/sin of its half — the same quantisation as below.
        constexpr float kGripRelativeRadius = 0.5f;
        constexpr float kPrimaryAngleDeg = 55.0f;
        const float len = std::sqrt(c.dir_x * c.dir_x + c.dir_y * c.dir_y);
        if (len >= kGripRelativeRadius) {
            // Screen Y grows downwards; atan2 with -dir_y gives a maths-style
            // angle with 0 = right, 90 = up.
            float deg = std::atan2(-c.dir_y, c.dir_x) * 57.2957795f;
            if (deg < 0.0f) deg += 360.0f;
            const float half = kPrimaryAngleDeg * 0.5f;
            auto near_axis = [&](float axis_deg) {
                float d = std::fabs(deg - axis_deg);
                if (d > 180.0f) d = 360.0f - d;
                return d <= half;
            };
            if (near_axis(0.0f))        { c.right = true; }
            else if (near_axis(90.0f))  { c.up = true; }
            else if (near_axis(180.0f)) { c.left = true; }
            else if (near_axis(270.0f)) { c.down = true; }
            else {                        // a diagonal sector
                c.right = (deg < 90.0f || deg > 270.0f);
                c.left = (deg > 90.0f && deg < 270.0f);
                c.up = (deg > 0.0f && deg < 180.0f);
                c.down = (deg > 180.0f && deg < 360.0f);
            }
        }
        touch_ = c;
    }

    void render_touch_controls() {
        if (!platform_ || !renderer_ || !assets_) return;
        const TouchControls c = touch_layout();
        auto tex_of = [&](const char* n) -> ren::Texture2D* {
            auto it = assets_->hud_textures().find(n);
            return it == assets_->hud_textures().end() ? nullptr : it->second.get();
        };
        auto draw_c = [&](const char* n, float cx, float cy, float r) {
            auto* t = tex_of(n);
            if (!t) return false;
            const float aspect = static_cast<float>(t->width()) /
                                 static_cast<float>(std::max(1, t->height()));
            const float dh = r * 2.0f, dw = dh * aspect;
            renderer_->draw_textured_quad_screen(*t, cx - dw * 0.5f, cy - dh * 0.5f,
                                                 dw, dh);
            return true;
        };

        // Report missing art once. A control that silently fails to draw is
        // indistinguishable from one that is not wired up at all.
        static bool reported = false;
        if (!reported) {
            reported = true;
            for (const char* n : {"JoystickContainer_norm", "Joystick_norm",
                                  "btn_punch_normal", "btn_kick_normal"})
                if (!tex_of(n)) std::printf("[TOUCH] missing texture '%s'\n", n);
        }

        const bool active = (touch_.dir_x != 0.0f || touch_.dir_y != 0.0f);
        if (!draw_c(active ? "JoystickContainer_action" : "JoystickContainer_norm",
                    c.stick_cx, c.stick_cy, c.stick_r))
            draw_c("JoystickContainer_norm", c.stick_cx, c.stick_cy, c.stick_r);
        // The knob rides the deflection, clamped inside the ring.
        const float knob_r = c.stick_r * 0.42f;
        const float kx = c.stick_cx + touch_.dir_x * (c.stick_r - knob_r);
        const float ky = c.stick_cy + touch_.dir_y * (c.stick_r - knob_r);
        if (!draw_c(active ? "Joystick_action" : "Joystick_norm", kx, ky, knob_r))
            draw_c("Joystick_norm", kx, ky, knob_r);

        draw_c(touch_.punch ? "btn_punch_action" : "btn_punch_normal",
               c.punch_cx, c.punch_cy, c.punch_r);
        draw_c(touch_.kick ? "btn_kick_action" : "btn_kick_normal",
               c.kick_cx, c.kick_cy, c.kick_r);
        // [ORIGINAL] The ranged and magic buttons exist only while something
        // is equipped in those slots (GameController::layoutButtons @
        // 0x10046f40 positions them either way, but the fight UI hides them) —
        // so a fresh profile with bare fists shows two buttons, not four.
        if (!equipped_ranged_.empty())
            draw_c("btn_throw_normal", c.throw_cx, c.throw_cy, c.punch_r);
        if (!equipped_magic_.empty()) {
            draw_c("btn_magic_normal", c.magic_cx, c.magic_cy, c.punch_r);
            // [Wave 10A defect 6] probe: the magic fight button was never
            // drawn because equipped_magic_ was never synced from the
            // inventory. The probe makes the button's presence observable
            // in a scripted run (same dump-state gate as [STATE]).
            if (dump_state_)
                std::printf("[MAGIC-BTN] equipped='%s' frame='btn_magic_normal'\n",
                            equipped_magic_.c_str());
        }
    }

    // ---------- MENU scroll geometry ----------
    //
    // [ORIGINAL] The MENU scroll hangs off the bottom edge of the top panel and
    // is drawn from `assets/1536/textures/scrolls/common` (`MenuRoll_left`,
    // `_center`, `_right`). It is therefore laid out on the same atlas scale as
    // render_hud(): one atlas pixel of the 1536 tier maps to
    // `ui::atlas_scale(win_h)` screen pixels (see ui_scale.hpp for the law and
    // its binary addresses).
    //
    // This used to be three independent copies of `{btn_x=10, btn_y=58,
    // roll_h=40}` — the collapsed roll, the expanded menu and the click test in
    // Game::on_update — plus a fourth constant for the click box (130 px wide)
    // that did not match the drawn width at all, so part of the label sat
    // outside the clickable area and the rest of the bar was dead. Fixed pixels
    // also meant the scroll drifted away from the panel it hangs from at any
    // viewport other than 720p, while the panel itself scaled.
    struct MenuRollRect {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f, cap_w = 0.0f;
    };

    MenuRollRect menu_roll_rect() {
        MenuRollRect r;
        if (!platform_) return r;
        const float win_h = static_cast<float>(platform_->window_height());
        const float s = ui::atlas_scale(win_h);    // atlas px -> screen px
        r.y = ui::top_panel_h(win_h);              // flush under the top panel
        // [HEURISTIC-TODO] Left inset: 32 atlas units. That reproduces the 10 px
        // gap measured on the reference screenshot at 1280x720; the rule the
        // original uses for screen margins has not been reversed.
        r.x = 32.0f * s;

        // Roll height and cap width come from the atlas' own pixel sizes
        // (MenuRoll_left/right 156x114, MenuRoll_center 338x114), not from
        // eyeballed numbers.
        float roll_src_h = 114.0f, cap_src_w = 156.0f;
        if (assets_) {
            auto lit = assets_->scroll_textures().find("MenuRoll_left");
            if (lit != assets_->scroll_textures().end() && lit->second &&
                lit->second->height() > 0) {
                roll_src_h = static_cast<float>(lit->second->height());
                cap_src_w = static_cast<float>(lit->second->width());
            }
        }
        r.h = roll_src_h * s;
        r.cap_w = cap_src_w * s;

        // The bar is as wide as its label needs: caps + text + one cap of
        // padding, so a longer localization widens the scroll instead of
        // overflowing it.
        const auto [text_w, text_h] = measure_text(menu_label(), menu_label_scale());
        (void)text_h;
        r.w = text_w + 2.0f * r.cap_w + 48.0f * s;
        return r;
    }

    // The label is the localized string, not the Latin literal "MENU":
    // assets/localizations/rus.xml has <Word Title="menu">МЕНЮ</Word>.
    std::string menu_label() const {
        const std::string loc = localized("menu");
        return loc.empty() ? std::string("MENU") : loc;
    }

    // Text scale tied to the roll height so the label keeps its proportion.
    float menu_label_scale() const {
        if (!platform_) return 0.22f;
        const float win_h = static_cast<float>(platform_->window_height());
        // Same rule as the HUD numerals: proportional to the panel height.
        return ui::top_panel_h(win_h) / 280.0f;
    }

    // ---------- Dojo bag/disciple toggle button ----------
    //
    // [ORIGINAL] The dojo screen creates the toggle button in FUN_1014d5c0:
    // position (logical_width - 85, -75) points relative to its parent, art
    // btn_punching_bag / btn_disciple chosen by the CURRENT mode (the button
    // shows what you switch TO), initially hidden until the quest fires
    // ShowDojoDisciple.
    // [HEURISTIC-TODO] Two departures forced by our asset dump: the parent's
    // origin was not chased down, so -75 is read as "below the top panel"
    // (FUN_1014ca50 publishes the panel height for the layout below it); and
    // the full-size btn_disciple/btn_punching_bag PNGs are absent from this
    // dump, so the shipped btn_disciple_small (110 px) stands in for both.
    // The quest gate is not ported either — the button is always shown.
    struct BtnRect { float x = 0, y = 0, w = 0, h = 0; };
    BtnRect disciple_btn_rect() const {
        BtnRect r;
        if (!platform_) return r;
        const float win_w = static_cast<float>(platform_->window_width());
        const float win_h = static_cast<float>(platform_->window_height());
        const float pts = ui::points_scale(win_h);
        float side_pts = 55.0f;   // btn_disciple_small 110 px / content scale
        if (assets_) {
            auto it = assets_->hud_textures().find("btn_disciple_small");
            if (it != assets_->hud_textures().end() && it->second &&
                it->second->height() > 0)
                side_pts = it->second->height() / ui::kHighTierContentScale;
        }
        r.w = side_pts * pts;
        r.h = side_pts * pts;
        r.x = win_w - 85.0f * pts - r.w * 0.5f;
        r.y = ui::top_panel_h(win_h) + 75.0f * pts - r.h * 0.5f;
        return r;
    }

    // ---------- HUD ----------
    void render_hud(plat::Platform& platform) {
        // [ORIGINAL] The top panel is laid out from the atlas' own source sizes
        // (assets/1536/textures/panels/top/batchPanelsTop.plist), not from magic
        // pixel offsets:
        //   Top_Panel   1 x 192   a one-pixel strip tiled across the screen
        //   gold       95 x 95    ruby       88 x 87    energy   103 x 103
        //   Energy_Bar 230 x 32   Level_bar 380 x 38    AddMoney 116 x 116
        //
        // [ORIGINAL] The panel's height is its atlas height (192 px) mapped
        // through the 768-point layout law: FUN_1014ca50 takes the sprite's
        // getContentSize().height verbatim (192 atlas px / content scale 2 =
        // 96 points) and only stretches X across the screen. So the panel is
        // 96/768 = 12.5% of the viewport height at every window size. The
        // 8.5% this used to pin was an eyeballed read of a screenshot and made
        // the whole HUD ~1.5x smaller than the original. Law + addresses in
        // ui_scale.hpp.
        const float win_w = static_cast<float>(platform.window_width());
        const float win_h = static_cast<float>(platform.window_height());
        const float s = ui::atlas_scale(win_h);       // atlas px -> screen px
        const float panel_h = ui::top_panel_h(win_h);
        auto tex_of = [&](const char* n) -> ren::Texture2D* {
            auto it = assets_->hud_textures().find(n);
            return it == assets_->hud_textures().end() ? nullptr : it->second.get();
        };
        auto draw = [&](const char* n, float dx, float dy, float dw, float dh) {
            if (auto* t = tex_of(n))
                renderer_->draw_textured_quad_screen(*t, dx, dy, dw, dh);
        };
        // Vertically centre an element of atlas height `ah` inside the panel.
        auto cy = [&](float ah) { return (panel_h - ah * s) * 0.5f; };

        if (auto* panel = tex_of("Top_Panel")) {
            const float tile_w = std::max(1.0f, panel_h * panel->width() / panel->height());
            for (float px = 0; px < win_w; px += tile_w) {
                const float draw_w = std::min(tile_w, win_w - px);
                renderer_->draw_textured_quad_screen(*panel, px, 0, draw_w, panel_h,
                                                     0, 0, draw_w / tile_w, 1.0f);
            }
        } else {
            renderer_->draw_filled_rect_screen(0, 0, win_w, panel_h, {0, 0, 0, 180});
        }

        // Text sized to the panel rather than to a constant: at 720p the panel
        // is 61 px, and the digits used to be drawn at scale 0.32, which came
        // out ~40 px tall and ran straight over the icons beside them.
        const float text_scale = panel_h / 280.0f;
        const float text_y = panel_h * 0.30f;
        float hx = panel_h * 0.12f;

        draw("level", hx, cy(111.0f), 111.0f * s, 111.0f * s);
        hx += 111.0f * s + panel_h * 0.06f;
        render_text(std::to_string(hud_level_), hx, text_y, text_scale,
                    {255, 255, 255, 255});
        hx += panel_h * 0.55f;

        draw("Level_bar", hx, cy(38.0f), 380.0f * s, 38.0f * s);
        hx += 380.0f * s + panel_h * 0.22f;

        draw("energy", hx, cy(103.0f), 103.0f * s, 103.0f * s);
        hx += 103.0f * s + panel_h * 0.06f;
        draw("Energy_Bar", hx, cy(32.0f), 230.0f * s, 32.0f * s);
        hx += 230.0f * s + panel_h * 0.22f;

        draw("gold", hx, cy(95.0f), 95.0f * s, 95.0f * s);
        hx += 95.0f * s + panel_h * 0.06f;
        render_text(std::to_string(hud_gold_), hx, text_y, text_scale,
                    {255, 240, 200, 255});
        hx += panel_h * 1.7f;

        draw("ruby", hx, cy(87.0f), 88.0f * s, 87.0f * s);
        hx += 88.0f * s + panel_h * 0.06f;
        render_text(std::to_string(hud_gems_), hx, text_y, text_scale,
                    {255, 210, 210, 255});

        // The "+" button sits at the right edge in the original.
        draw("AddMoney", win_w - 116.0f * s - panel_h * 0.12f, cy(116.0f),
             116.0f * s, 116.0f * s);

        // The dojo's bag/disciple toggle (see disciple_btn_rect for the
        // provenance). Slightly dimmed when the click would bring the bag
        // back, so the two states are tellable apart with one art.
        {
            const BtnRect r = disciple_btn_rect();
            auto* t = tex_of("btn_disciple_small");
            if (t) {
                const uint8_t a = show_enemy_ ? 140 : 255;
                renderer_->draw_textured_quad_screen(*t, r.x, r.y, r.w, r.h,
                                                     0, 0, 1, 1,
                                                     {255, 255, 255, a});
            }
        }

        // [ORIGINAL] Dojo is a TRAINING area — NO health bars, NO victory/defeat.
        // Health bars only appear in real fights (map battles). In Dojo, the
        // player practices moves against a training dummy (bag or enemy fighter).
        // The enemy fighter in Dojo is a sparring partner, not a real opponent.
        // B key toggles between punching bag and enemy fighter.
        // Keyboard hints are ours, not the original's — the original has
        // on-screen touch controls there instead (PORT_PLAN 6.3). Keep them
        // behind the debug overlay so the default view matches the reference
        // screenshot.
        if (debug_world_ && total_frame_count_ < 360) {
            uint8_t hint_alpha = (total_frame_count_ < 300) ? 200 :
                (uint8_t)(200 * (360 - total_frame_count_) / 60);
            std::string hint = "WASD move | O punch | P kick | S+D roll | B toggle enemy";
            float hint_w = hint.size() * 8.0f * 0.3f;
            render_text(hint, ((float)platform.window_width() - hint_w) / 2.0f,
                (float)platform.window_height() - 60.0f, 0.3f,
                {220, 220, 220, hint_alpha});
        }
        // Menu button (LEFT side, scroll/roll style) — geometry from
        // menu_roll_rect(), shared with the expanded menu and the click test.
        const MenuRollRect roll = menu_roll_rect();
        // Compute menu animation progress (smoothstep easing)
        float mp = menu_anim_progress_;
        float menu_eased = mp * mp * (3.0f - 2.0f * mp);
        // Show collapsed roll when menu is closed OR animating
        if (menu_eased < 0.99f) {
            auto lit = assets_->scroll_textures().find("MenuRoll_left");
            auto cit = assets_->scroll_textures().find("MenuRoll_center");
            auto rit = assets_->scroll_textures().find("MenuRoll_right");
            const std::string label = menu_label();
            const float label_scale = menu_label_scale();
            const auto [text_w, text_h] = measure_text(label, label_scale);
            // Fade out the collapsed roll as menu expands
            float alpha = 1.0f - menu_eased;
            if (lit != assets_->scroll_textures().end() && cit != assets_->scroll_textures().end() &&
                rit != assets_->scroll_textures().end()) {
                float center_w = roll.w - 2 * roll.cap_w;
                ren::Color4B roll_col{255, 255, 255, (uint8_t)(alpha * 255)};
                renderer_->draw_textured_quad_screen(*lit->second, roll.x, roll.y, roll.cap_w, roll.h, 0,0,1,1, roll_col);
                renderer_->draw_textured_quad_screen(*cit->second, roll.x + roll.cap_w, roll.y, center_w, roll.h, 0,0,1,1, roll_col);
                renderer_->draw_textured_quad_screen(*rit->second, roll.x + roll.cap_w + center_w, roll.y, roll.cap_w, roll.h, 0,0,1,1, roll_col);
            } else {
                ren::Color4B bg{60, 40, 20, (uint8_t)(alpha * 230)};
                renderer_->draw_filled_rect_screen(roll.x, roll.y, roll.w, roll.h, bg);
            }
            // Centre the label on the roll.
            ren::Color4B text_col{255, 240, 200, (uint8_t)(alpha * 255)};
            render_text(label, roll.x + (roll.w - text_w) / 2.0f,
                        roll.y + (roll.h - text_h) / 2.0f, label_scale, text_col);
        }

        // Control hints and the position readout are development scaffolding.
        // They are not in the original — its bottom-left corner holds the
        // virtual joystick — so they live behind the debug overlay (F1).
        if (debug_world_) {
            render_text("A/D - move    Space - hit    M - menu    T - dialog",
                        20, (float)(platform.window_height() - 40), 0.26f,
                        {200, 200, 200, 255});
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Pos: (%.0f, %.0f)",
                          player_pos_x_, player_pos_y_);
            render_text(buf, 20, (float)(platform.window_height() - 65), 0.26f,
                        {180, 180, 180, 255});
        }
    }

    // ---------- Fight HUD (battle mode) ----------
    //
    // [ORIGINAL] The in-fight HUD is ScreenModel (ctor @ 0x10200c10) — one
    // instance per side, both children of a node at the TOP-CENTER of the
    // screen at (0,0) (0x10291370), every X mirrored by the side sign and the
    // enemy side's sprites flipped with setScaleX(-1). Coordinates are points
    // of the 768-tall space (ui_scale.hpp); s = -1 player / +1 enemy:
    //
    //   health bar  @ 0x102017c0  HealthBar_Empty backdrop (564x26 px ->
    //               282x13 pt), node x = s*53 - w/2, y = h/2 - 100; fill is
    //               the 1-px HealthBar_Full strip stretched to 275 pt
    //               (ProgressBarSkewed +0x150), offset -3 (+0x15c), with a
    //               HealthBar_Hit trail draining at 25 units/s (ctor arg).
    //   name label  @ 0x10201c30  x = s*315, y = -65, aligned toward the
    //               center (align 1 left / 5 right), width cap 250.
    //   round dots  @ 0x10201d90  Round_Undone sprites from s*77 stepping
    //               s*(Round_Done.width - 1), y = -116, count = Fight rounds
    //               (fighter+0xe4 <- Fight+0xc @ 0x10291370), mirrored.
    //   avatar      @ 0x10201370  image/users/image/<fighter name>.png at
    //               (s*415, -110), scaled to 200 pt. Not drawn yet: the
    //               fighter -> avatar-file mapping is unreversed.
    //   crazy bar   @ 0x10201f50, combo @ 0x10201690 (ComboModel), perks
    //               @ 0x10201280 (ActivePerkModel) — magic/combo/perk systems
    //               are not ported yet.
    //
    // The mirrored bar's decompiled node x (s*53 - w/2 with scaleX -1) does
    // not read as symmetric on its own — the widget's internal anchor was not
    // chased down — so the enemy bar is placed by symmetry with the player
    // bar, which every other element obeys exactly.
    void render_fight_hud(plat::Platform& platform) {
        if (!renderer_ || !assets_) return;
        const float win_w = static_cast<float>(platform.window_width());
        const float win_h = static_cast<float>(platform.window_height());
        const float pts = ui::points_scale(win_h);
        const float cx = win_w * 0.5f;
        auto tex_of = [&](const char* n) -> ren::Texture2D* {
            auto it = assets_->hud_textures().find(n);
            return it == assets_->hud_textures().end() ? nullptr : it->second.get();
        };

        auto* empty = tex_of("HealthBar_Empty");
        auto* full = tex_of("HealthBar_Full");
        auto* hit = tex_of("HealthBar_Hit");

        // Sizes in points = atlas px / content scale; plist sizes 564x26 and
        // 1x43 are the fallback for a missing atlas.
        const float bar_w = (empty ? empty->width() : 564.0f) /
                            ui::kHighTierContentScale;
        const float bar_h = (empty ? empty->height() : 26.0f) /
                            ui::kHighTierContentScale;
        const float fill_h = (full ? full->height() : 43.0f) /
                             ui::kHighTierContentScale;
        constexpr float kInnerGapPts = 53.0f;   // 0x102017c0
        constexpr float kFillWidthPts = 275.0f; // ProgressBarSkewed +0x150
        constexpr float kFillInsetPts = 3.0f;   // +0x15c (sign folded)
        constexpr float kDrainPerSec = 25.0f;   // ctor arg @ 0x102017c0
        // Node y = empty_h/2 - 100 below the top edge (Y-up), i.e. the bar's
        // center sits at 100 - h/2 points from the top.
        const float bar_cy = (100.0f - bar_h * 0.5f) * pts;

        // The hit trail lags the health value at 25 units/s.
        auto trail = [&](float& t, const FighterState& f) {
            if (t < 0.0f || t < f.health) t = f.health;
            const float dt = last_frame_dt_ms_ * 0.001f;
            t = std::max(f.health, t - kDrainPerSec * dt);
            return t;
        };
        const float trail_p = trail(hp_trail_player_, player_fighter_);
        const float trail_e = trail(hp_trail_enemy_, enemy_fighter_);

        // One side. `dir` is the original's side sign: -1 player, +1 enemy.
        auto draw_bar = [&](float dir, const FighterState& f, float trail_hp) {
            const float outer = cx + dir * (kInnerGapPts + bar_w) * pts;
            const float inner = cx + dir * kInnerGapPts * pts;
            const float x0 = std::min(outer, inner);
            if (empty) {
                // Enemy backdrop mirrored like the original's setScaleX(-1).
                renderer_->draw_textured_quad_screen(
                    *empty, x0, bar_cy - bar_h * 0.5f * pts, bar_w * pts,
                    bar_h * pts, dir > 0 ? 1.0f : 0.0f, 0.0f,
                    dir > 0 ? 0.0f : 1.0f, 1.0f);
            } else {
                renderer_->draw_filled_rect_screen(x0, bar_cy - bar_h * 0.5f * pts,
                                                   bar_w * pts, bar_h * pts,
                                                   {30, 30, 30, 220});
            }
            // Fill grows from the widget's local origin — the OUTER edge —
            // toward the center, so damage recedes from the inner end.
            const float frac = f.max_health > 0 ? std::max(0.0f, f.health / f.max_health) : 0;
            const float tfrac = f.max_health > 0 ? std::max(0.0f, trail_hp / f.max_health) : 0;
            const float fill_x0 = outer - dir * kFillInsetPts * pts;
            const float fy = bar_cy - fill_h * 0.5f * pts;
            auto strip = [&](ren::Texture2D* t, float from_frac, float to_frac,
                             ren::Color4B col) {
                if (!t || to_frac <= from_frac) return;
                const float a = fill_x0 - dir * kFillWidthPts * pts * from_frac;
                const float b = fill_x0 - dir * kFillWidthPts * pts * to_frac;
                renderer_->draw_textured_quad_screen(
                    *t, std::min(a, b), fy, std::fabs(b - a), fill_h * pts,
                    0, 0, 1, 1, col);
            };
            strip(full, 0.0f, frac, {255, 255, 255, 255});
            strip(hit, frac, tfrac, {255, 255, 255, 255});
        };
        draw_bar(-1.0f, player_fighter_, trail_p);
        draw_bar(+1.0f, enemy_fighter_, trail_e);

        // Names: player is the localized "Shadow", the enemy comes from the
        // stages.xml warrior the map handed over.
        // [HEURISTIC-TODO] Text height: the label setup passes 80.0 and a
        // 250-wide cap (0x10201c30); how 80 maps to glyph height in the
        // original's font pipeline is not reversed. 40 pt reads right against
        // the 13-pt bar.
        const FightHudLayout L = host_get_fight_hud_layout();
        const float name_scale = 40.0f / 115.0f;   // kFontLineBoxPx
        const auto names = host_get_hud_fighter_names();
        render_text(names.player, L.player_name_x, L.name_y, name_scale,
                    {255, 255, 255, 255});
        const auto [ew, eh] = measure_text(names.enemy, name_scale);
        (void)eh;
        render_text(names.enemy, L.enemy_name_right - ew, L.name_y, name_scale,
                    {255, 255, 255, 255});

        // Round dots: Fight rounds per side, wins shown as Round_Done.
        auto* done = tex_of("Round_Done");
        auto* undone = tex_of("Round_Undone");
        if (undone) {
            const float dot_w = (done ? done->width() : undone->width()) /
                                ui::kHighTierContentScale;
            const float dot_h = undone->height() / ui::kHighTierContentScale;
            const float dot_y = 116.0f * pts - dot_h * 0.5f * pts;
            auto draw_dots = [&](float dir, int wins) {
                float x = 77.0f * dir;
                for (int i = 0; i < battle_info_.rounds; ++i) {
                    auto* t = (i < wins && done) ? done : undone;
                    renderer_->draw_textured_quad_screen(
                        *t, cx + x * pts - dot_w * 0.5f * pts, dot_y,
                        dot_w * pts, dot_h * pts, dir > 0 ? 1.0f : 0.0f, 0.0f,
                        dir > 0 ? 0.0f : 1.0f, 1.0f);
                    x += dir * (dot_w - 1.0f);
                }
            };
            draw_dots(-1.0f, round_wins_player_);
            draw_dots(+1.0f, round_wins_enemy_);
        }

        // [Wave 10A defect 3] FIGHT TIMER: the round clock (BattleScene's
        // round_left_ms_, pushed via host_set_round_left_ms) was never drawn
        // — the original shows the seconds countdown at the top-center of
        // the fight screen. Rendered as text beside the round dots, same
        // glyph scale as the fighter names.
        const int secs = std::max(0, (round_left_ms_probe_ + 999) / 1000);
        std::string secs_str = std::to_string(secs);
        const float timer_scale = name_scale;
        const auto [tw2, th2] = measure_text(secs_str, timer_scale);
        render_text(secs_str, cx - tw2 * 0.5f,
                    L.name_y - (name_scale > 0 ? 2.0f : 0.0f), timer_scale,
                    {255, 255, 255, 255});
        (void)th2;
        // [Wave 10A defect 3] E2E probe: makes the countdown observable in a
        // scripted run (stdout, same gate as [STATE]).
        if (dump_state_)
            std::printf("[HUD-TIMER] secs=%d left_ms=%d\n", secs,
                        round_left_ms_probe_);
    }

    // ---------- Menu expanded (vertical scroll, matching original game) ----------
    // In the original game, the menu is a VERTICAL scroll on the left side.
    // Icons are stacked top-to-bottom in a single column.
    // The scroll "unrolls" from top to bottom with a smooth animation.
    void render_menu_expanded(plat::Platform& /*platform*/) {
        // Compute eased animation progress
        float mp = menu_anim_progress_;
        float menu_eased = mp * mp * (3.0f - 2.0f * mp);  // smoothstep
        if (menu_eased < 0.01f) return;  // nothing to render

        // Dark backdrop behind expanded scroll menu for contrast
        float ww = (float)platform_->window_width();
        float wh = (float)platform_->window_height();
        ren::Color4B backdrop{0, 0, 0, (uint8_t)(menu_eased * 120)};
        if (backdrop.a > 5)
            renderer_->draw_filled_rect_screen(0, 0, ww, wh, backdrop);

        // Same geometry source as the collapsed roll and the click test.
        const MenuRollRect roll = menu_roll_rect();
        const float btn_x = roll.x, btn_y = roll.y, roll_h = roll.h;
        const float s = ui::atlas_scale(wh);    // atlas px -> screen px

        auto lit = assets_->scroll_textures().find("MenuRoll_left");
        auto cit = assets_->scroll_textures().find("MenuRoll_center");
        auto rit = assets_->scroll_textures().find("MenuRoll_right");

        // Vertical layout: icons stacked top-to-bottom. Sizes are in atlas
        // units of the 1536 tier, scaled by `s` like every other HUD element,
        // instead of the fixed pixels that only happened to look right at 720p.
        const float icon_size = 176.0f * s;
        const float icon_spacing = 25.0f * s;
        const int n_items = 5;
        const float paper_padding = 44.0f * s;
        const float paper_w = icon_size + paper_padding * 2 + 94.0f * s;
        const float full_paper_h = n_items * (icon_size + icon_spacing) + paper_padding * 2;
        // Animate paper height: scroll unrolls from top to bottom
        const float paper_h = full_paper_h * menu_eased;

        if (lit == assets_->scroll_textures().end() || cit == assets_->scroll_textures().end() ||
            rit == assets_->scroll_textures().end()) {
            ren::Color4B bg{60, 40, 20, 230};
            renderer_->draw_filled_rect_screen(btn_x, btn_y, paper_w, paper_h, bg);
            return;
        }

        auto& left_tex = lit->second;
        auto& center_tex = cit->second;
        auto& right_tex = rit->second;
        const float cap_w = roll.cap_w;
        const float center_w = paper_w - 2 * cap_w;

        // Roll bar (top, horizontal) — sized to fit paper width
        float roll_alpha = (menu_eased > 0.05f) ? 1.0f : menu_eased / 0.05f;
        ren::Color4B roll_col{255, 255, 255, (uint8_t)(roll_alpha * 255)};
        renderer_->draw_textured_quad_screen(*left_tex, btn_x, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
        renderer_->draw_textured_quad_screen(*center_tex, btn_x + cap_w, btn_y, center_w, roll_h, 0,0,1,1, roll_col);
        renderer_->draw_textured_quad_screen(*right_tex, btn_x + cap_w + center_w, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);

        // Paper area (below roll, vertical) — clips to animated height
        float paper_y = btn_y + roll_h - 3;
        ren::Color4B paper_bg{200, 170, 120, 245};
        renderer_->draw_filled_rect_screen(btn_x, paper_y, paper_w, paper_h, paper_bg);

        // Paper edges (top and bottom)
        auto pl_it = assets_->scroll_textures().find("Paper_left");
        auto pr_it = assets_->scroll_textures().find("Paper_right");
        if (pl_it != assets_->scroll_textures().end()) {
            float pl_w = paper_w * pl_it->second->width() / pl_it->second->height();
            renderer_->draw_textured_quad_screen(*pl_it->second, btn_x, paper_y, pl_w, paper_w);
        }
        if (pr_it != assets_->scroll_textures().end() && menu_eased > 0.95f) {
            // Only show bottom edge when fully expanded
            float pr_w = paper_w * pr_it->second->width() / pr_it->second->height();
            renderer_->draw_textured_quad_screen(*pr_it->second,
                btn_x, paper_y + paper_h - pr_w, pr_w, paper_w);
        }

        // Shadow below (only when fully expanded)
        auto shadow_it = assets_->scroll_textures().find("Shadow_roll");
        if (shadow_it != assets_->scroll_textures().end() && menu_eased > 0.9f) {
            renderer_->draw_textured_quad_screen(*shadow_it->second,
                btn_x, paper_y + paper_h - 25.0f * s, paper_w, 47.0f * s);
        }

        // Menu icons (vertical stack) — only render icons that fit within the animated height
        // All icons rendered with uniform scaling: scale = icon_size / max_texture_dimension
        // This ensures all icons appear the same size on screen while preserving aspect ratio.
        const char* items[] = {"Dojo", "Map", "Shop", "Profile", "Settings"};
        // Find max texture dimension across all icons for uniform scaling
        int max_tex_dim = 1;
        for (auto& name : items) {
            std::string tex_name = std::string(name) + "_normal";
            auto it = assets_->menu_textures().find(tex_name);
            if (it == assets_->menu_textures().end()) {
                it = assets_->menu_textures().find(std::string(name) + "_Normal");
            }
            if (it != assets_->menu_textures().end()) {
                max_tex_dim = std::max(max_tex_dim, std::max(it->second->width(), it->second->height()));
            }
        }
        float uniform_scale = icon_size / (float)max_tex_dim;
        float ix = btn_x + paper_padding + 31.0f * s;
        float iy = paper_y + paper_padding;
        for (int idx = 0; idx < 5; ++idx) {
            float icon_y = iy + idx * (icon_size + icon_spacing);
            // Skip icons that haven't been revealed yet (below the unrolled height)
            if (icon_y + icon_size > paper_y + paper_h) break;

            auto& name = items[idx];
            // Try different case patterns for the texture name
            std::string tex_name = std::string(name) + "_normal";
            auto it = assets_->menu_textures().find(tex_name);
            if (it == assets_->menu_textures().end()) {
                it = assets_->menu_textures().find(std::string(name) + "_Normal");
            }
            if (it != assets_->menu_textures().end()) {
                // Uniform scale: all icons scaled by same factor, preserving aspect ratio
                float draw_w = it->second->width() * uniform_scale;
                float draw_h = it->second->height() * uniform_scale;
                // Center within the icon_size × icon_size slot
                float draw_x = ix + (icon_size - draw_w) * 0.5f;
                float draw_y = icon_y + (icon_size - draw_h) * 0.5f;
                renderer_->draw_textured_quad_screen(*it->second, draw_x, draw_y,
                                                     draw_w, draw_h);
                if (!loc_icons_logged) {
                    std::printf("[MENU] icon '%s': tex %dx%d → draw %.0fx%.0f (scale=%.2f)\n",
                                name, it->second->width(), it->second->height(),
                                draw_w, draw_h, uniform_scale);
                }
            }
            render_text(name, ix + icon_size + 16.0f * s, icon_y + 31.0f * s,
                        menu_label_scale() * 0.73f, {60, 40, 20, 255});
        }
        loc_icons_logged = true;
    }

    // ---------- Menu overlay ----------
    void render_menu_overlay(plat::Platform& platform) {
        // Dim background
        ren::Color4B dim{0, 0, 0, 160};
        renderer_->draw_filled_rect_screen(
            0, 0, (float)platform.window_width(), (float)platform.window_height(), dim);

        float panel_w = 480, panel_h = 420;
        float px = (platform.window_width() - panel_w) / 2.0f;
        float py = (platform.window_height() - panel_h) / 2.0f;
        ren::Color4B panel_bg{30, 30, 40, 240};
        renderer_->draw_filled_rect_screen(px, py, panel_w, panel_h, panel_bg);
        ren::Color4B border{120, 90, 50, 255};
        renderer_->draw_filled_rect_screen(px, py, panel_w, 3, border);
        renderer_->draw_filled_rect_screen(px, py + panel_h - 3, panel_w, 3, border);
        renderer_->draw_filled_rect_screen(px, py, 3, panel_h, border);
        renderer_->draw_filled_rect_screen(px + panel_w - 3, py, 3, panel_h, border);

        render_text("MENU", px + panel_w/2 - 50, py + 30, 0.5f,
                    {255, 220, 120, 255});

        struct MenuItem { const char* label; const char* sub; };
        MenuItem items[] = {
            {"MAP",      "Travel to other locations"},
            {"SHOP",     "Buy weapons, armour, helmets"},
            {"SETTINGS", "Audio, graphics, controls"},
            {"SAVE",     "Save progress"},
            {"EXIT",     "Return to title screen"}
        };
        float by = py + 100;
        for (auto& it : items) {
            float bx = px + 30, bw = panel_w - 60, bh = 50;
            ren::Color4B btn_bg{60, 60, 80, 220};
            renderer_->draw_filled_rect_screen(bx, by, bw, bh, btn_bg);
            ren::Color4B btn_brd{100, 100, 130, 255};
            renderer_->draw_filled_rect_screen(bx, by, bw, 2, btn_brd);
            render_text(it.label, bx + 20, by + 10, 0.40f,
                        {255, 255, 255, 255});
            render_text(it.sub, bx + 120, by + 15, 0.28f,
                        {180, 180, 200, 255});
            by += 60;
        }
    }

    // ---------- Dialog overlay ----------
    // [ORIGINAL] The dojo dialogue is a paper scroll in the upper right with the
    // speaker's portrait on its left edge. It is built from
    // textures/scrolls/common — Roll_left / Roll_center (tiled) / Roll_right for
    // the rolled bar, Paper_left / Paper_right for the sheet — and the text is
    // the localized string, not English placeholder copy: the original's first
    // launch shows tutorial_move, "Сначала покажи, / как ты двигаешься!".
    //
    // [HEURISTIC-TODO] Placement and proportions are matched to the original's
    // screenshot. The layout rule itself (which anchor, which margins) has not
    // been reversed.
    void render_dialog_overlay(plat::Platform& platform) {
        const float win_w = static_cast<float>(platform.window_width());
        const float win_h = static_cast<float>(platform.window_height());
        auto tex_of = [&](const char* n) -> ren::Texture2D* {
            auto it = assets_->hud_textures().find(n);
            return it == assets_->hud_textures().end() ? nullptr : it->second.get();
        };

        // [ORIGINAL] The hint scroll unrolls right-to-left like a paper scroll.
        // dialog_overlay_anim_ goes 0→1; the visible width grows from the right
        // edge leftward, revealing the content progressively.
        float anim = dialog_overlay_anim_;
        // Smoothstep easing for a natural unroll feel
        float eased = anim * anim * (3.0f - 2.0f * anim);
        if (eased < 0.01f) return;  // nothing visible yet

        // Scroll box: right half of the screen, just under the top panel.
        const float full_box_w = win_w * 0.46f;
        const float box_h = win_h * 0.20f;
        const float box_w = full_box_w * eased;  // animated width (unrolls)
        const float box_x = win_w - full_box_w - win_w * 0.03f + (full_box_w - box_w);  // anchored right
        const float box_y = win_h * 0.11f;

        // Sheet. Paper_left / Paper_right are narrow vertical strips
        // (116 x 1524) — the sheet's side edges, not two halves of it. Drawing
        // them as halves stretched a 1:13 strip into a 4:1 box and produced a
        // dark smear. They are drawn at the edges and the middle is filled with
        // parchment.
        renderer_->draw_filled_rect_screen(box_x, box_y, box_w, box_h,
                                           {226, 205, 163, 250});
        // [P8] Paper edges: the 116x1524 side strips at their own aspect.
        // The old *6 fudge smeared them into a dark band.
        const float pts2 = ui::points_scale(win_h);
        const float edge_w = box_h * (116.0f / 1524.0f);
        if (auto* paper_l = tex_of("Paper_left")) {
            renderer_->draw_textured_quad_screen(*paper_l, box_x, box_y, edge_w, box_h);
        }
        if (auto* paper_r = tex_of("Paper_right")) {
            renderer_->draw_textured_quad_screen(*paper_r, box_x + box_w - edge_w, box_y,
                                                 edge_w, box_h);
        }

        // Rolled bar across the top: a 3-slice with fixed ends (156 x 74) and a
        // tileable centre (688 x 74), drawn at the native 37-pt height.
        auto* roll_l = tex_of("Roll_left");
        auto* roll_c = tex_of("Roll_center");
        auto* roll_r = tex_of("Roll_right");
        if (roll_l && roll_c && roll_r) {
            const float bar_h = (74.0f / ui::kHighTierContentScale) * pts2;
            const float end_w = bar_h * (156.0f / 74.0f);
            const float bar_y = box_y - bar_h * 0.55f;
            renderer_->draw_textured_quad_screen(*roll_l, box_x, bar_y, end_w, bar_h);
            renderer_->draw_textured_quad_screen(*roll_c, box_x + end_w, bar_y,
                                                 box_w - 2.0f * end_w, bar_h);
            renderer_->draw_textured_quad_screen(*roll_r, box_x + box_w - end_w, bar_y,
                                                 end_w, bar_h);
        }

        // Speaker portrait on the left of the sheet.
        // Only render content once the scroll is mostly unrolled.
        if (eased < 0.80f) return;
        float text_x = box_x + box_w * 0.06f;
        if (auto* portrait = tex_of("character_sensei_small")) {
            const float ps = box_h * 0.80f;
            renderer_->draw_textured_quad_screen(*portrait, box_x + box_w * 0.03f,
                                                 box_y + (box_h - ps) * 0.5f, ps, ps);
            text_x = box_x + box_w * 0.03f + ps + box_w * 0.04f;
        }

        std::string line = localized("tutorial_move");
        if (line.empty()) line = "tutorial_move";
        const float text_scale = box_h / 620.0f;
        float ty = box_y + box_h * 0.26f;
        size_t start = 0;
        while (start <= line.size()) {
            size_t nl = line.find('\n', start);
            std::string part = line.substr(start, nl == std::string::npos
                                                      ? std::string::npos
                                                      : nl - start);
            while (!part.empty() && (part.back() == '\r' || part.back() == ' '))
                part.pop_back();
            if (!part.empty()) {
                render_text(part, text_x, ty, text_scale, {60, 40, 20, 255});
                ty += box_h * 0.24f;
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }

private:
    plat::Platform* platform_ = nullptr;
    std::string asset_root_;
    std::unique_ptr<ren::IRenderer> renderer_;

    // --- Module instances ---
    // Combat system: owns all combat/fighter/AI state.
    // Game member variables below reference this via mutable accessors.
    Combat combat_;
    // Animation player: owns animation state (interpolation, node positions, root motion).
    // Game member variables below reference this via mutable accessors.
    AnimationPlayer anim_player_;

    // --- Scene management ---
    scene::SceneManager scene_manager_;
    std::string current_location_name_ = "dojo";
    resf2::game::LocationManager locations_;
    bool location_loaded_ = false;

    // --- Dialogue / story state ---
    std::vector<std::pair<std::string, std::string>> dialogue_lines_;
    size_t dialogue_index_ = 0;
    // [ORIGINAL] QuestActionDialog @ FUN_101c7d20 stores choices at +0xa4..+0xb0
    // (confirmed in assembly at 0x101c7e89: LEA EAX,[ESI+0xa4], vector init).
    // Each choice is a localization key whose text is shown on the button.
    std::vector<std::string> dialogue_choices_;

    // --- Level / progress state ---
    std::string current_level_;
    std::string battle_location_;
    std::string battle_result_;  // "victory" / "defeat" / ""
    // Fight parameters handed over by the map (stages.xml <Fight>), plus the
    // round-win tally shown as Round_Done dots in the fight HUD (D4).
    BattleInfo battle_info_;
    int round_wins_player_ = 0;
    int round_wins_enemy_ = 0;
    // The ProgressBarSkewed "hit" trail: a lagging copy of each health value
    // that drains at 25 units/s (the ctor argument at 0x102017c0), drawn in
    // HealthBar_Hit between the current fill end and itself.
    float hp_trail_player_ = -1.0f;
    float hp_trail_enemy_ = -1.0f;
    // dt of the last gameplay tick, for the trail decay in render_fight_hud.
    uint32_t last_frame_dt_ms_ = 16;
    std::vector<std::string> completed_levels_;
    int currency_ = 1000;  // starting gold
    int player_wins_ = 0;
    int player_losses_ = 0;

    // [ORIGINAL] Tutorial state from usersDefault.xml Tutorial attribute.
    // Values: "MOVE" (initial), "BAG" (punching bag), "FIRST_FIGHT", "COMPLETE".
    std::string tutorial_state_ = "MOVE";
    // [Wave 9B] Story-dialogue queue state (quests.xml <Dialog> sets).
    bool story_dialogue_pending_ = false;
    scene::SceneId dialogue_return_ = scene::SceneId::None;
    // One-shot story beats: the knives-buy prompt and the FirstGuardBeaten
    // (Lynx/May) set. [HEURISTIC-TODO] In-memory only — the original saves
    // quest state in users.xml variables.
    bool tutorial_knives_beat_ = false;
    bool first_guard_beaten_ = false;

    // [ORIGINAL] Zone/battle lock state from <Battles> in usersDefault.xml.
    // Key: "ZONE_N" → true if unlocked. Key: "ZONE_N|BOSS_NAME" → true if unlocked.
    std::map<std::string, bool> zone_unlocked_;
    std::map<std::string, bool> battle_unlocked_;

    // [S1/S2] Voice gender of the player / current enemy ("Male"/"Female").
    // Selects the m_pl_* vs f_pl_* sound set. Player default "Male" matches
    // usersDefault.xml; the enemy voice is resolved in host_set_battle_info.
    std::string player_voice_ = "Male";
    std::string enemy_voice_ = "Male";
    resf2::format::ListData list_data_;
    bool list_data_loaded_ = false;

    // Persistence: SaveManager writes/reads the save file on disk.
    // PlayerProfile holds the authoritative player state (synced on save/load).
    resf2::save::SaveManager save_manager_;
    resf2::player::PlayerProfile player_profile_;

    // Inventory and Shop
    resf2::inventory::Inventory inventory_;
    resf2::shop::ShopManager shop_manager_;

    // Quest engine: drives zone unlocks, battle unlocks, dialogs from quests.xml
    // [ORIGINAL] QuestManager @ 0x101c7d20 processes quest actions on events.
    quest::QuestEngine quest_engine_;

    // [Wave 9B] S5: the 498 quests of quests.xml parsed at boot
    // (quest_loader.cpp), dispatched by host_trigger_quest_event. Empty when
    // the file is absent — the game then runs without quest dispatch.
    std::vector<resf2::game::QuestDef> quest_defs_;
    bool quest_defs_loaded_ = false;
    // [Wave 9B] S5: the knives-bought Lynx challenge fires once per save.
    bool tutorial_lynx_hint_shown_ = false;

    // stage_data_ and stages_loaded_ live in AssetManager (assets_)

    // --- Combat state (owned by combat_ module) ---
    // All combat/fighter/AI state lives in Combat.
    // Reference aliases below make existing code work without changes.
    FighterState& player_fighter_ = combat_.player_fighter();
    FighterState& enemy_fighter_ = combat_.enemy_fighter();
    float& enemy_attack_cooldown_ = combat_.mutable_enemy_attack_cooldown();
    float& player_hit_flash_ = combat_.mutable_player_hit_flash();
    float& enemy_hit_flash_ = combat_.mutable_enemy_hit_flash();
    float& combo_timer_ = combat_.mutable_combo_timer();

    // --- Player block decision state (FUN_10171d80) ---
    // [ORIGINAL] Block is NOT automatic — it's a weighted roulette decision
    // every 0.6-1.0s via TacticSettings::choose() with "Duck" candidate.
    float& block_decision_cooldown_ = combat_.mutable_block_decision_cooldown();
    bool& block_decision_pending_ = combat_.mutable_block_decision_pending();
    float& recent_damage_taken_ = combat_.mutable_recent_damage_taken();
    int& enemy_hits_on_player_ = combat_.mutable_enemy_hits_on_player();

    // [ORIGINAL] Hit effect: uses original hit_blade texture (18-frame spark
    // animation from assets/1536/textures/effects/fight/hit_blade.plist).
    // The original SF2 renders this sprite at the hit point, cycling through
    // frames 1-18 over ~0.3s, then removing it. Falls back to colored circles
    // if the texture is not loaded.
    // HitSpark type lives in types.hpp (namespace resf2::game).
    std::vector<HitSpark> hit_sparks_;
    void spawn_hit_sparks(float x, float y, int /*count*/ = 1) {
        // [ORIGINAL] Spawn a single hit_blade effect at the hit point.
        // count is ignored — the original uses ONE animated sprite, not N circles.
        HitSpark s;
        s.x = x + ((float)(std::rand() % 20) - 10.0f);
        s.y = y + ((float)(std::rand() % 20) - 10.0f);
        s.age = 0;
        s.lifetime = 0.36f;  // 18 frames at 50fps ≈ 0.36s
        s.scale = 0.8f + (float)(std::rand() % 40) / 100.0f;
        hit_sparks_.push_back(s);
    }
    // [Soak-fix Wave 9A] F1 test seam: impact sound counter state (the
    // hit1-6.wav / super_hit1-5.wav plays counted by play_sound).
    int hit_sound_count_ = 0;
    // [Soak-fix Wave 9A] F1f probe state: swing-swish plays (the F1 swish
    // block increments it; see host_get_swish_play_count).
    int swish_play_count_ = 0;
    std::string last_hit_sound_;
    // [Soak-fix Wave 9A] F1: last sound played by ANY path (swish/bodyfall
    // /armor/wall3 pins); see play_sound.
    std::string last_played_sound_;

    // [Soak-fix Wave 9A] F3: stance-idle heel-anchor compensation state —
    // the idle's node map is re-anchored on its planted heel (NHeel_2) so
    // the feet do not slide while the authored pivot lean plays around
    // them (the original's alignAnimation model). See the compensation in
    // game.cpp after the root-motion application.
    float idle_heel_anchor_rel_ = 0.0f;
    std::string prev_update_anim_;

    void update_and_render_hit_sparks(float dt_sec) {
        for (auto& s : hit_sparks_) s.age += dt_sec;
        hit_sparks_.erase(std::remove_if(hit_sparks_.begin(), hit_sparks_.end(),
            [](const HitSpark& s) { return s.age >= s.lifetime; }), hit_sparks_.end());
        for (const auto& s : hit_sparks_) {
            float t = s.age / s.lifetime;
            // hit_blade has 18 frames (hit_blade_1.png .. hit_blade_18.png)
            int frame = (int)(t * 18.0f) + 1;
            if (frame > 18) frame = 18;
            std::string tex_name = "hit_blade_" + std::to_string(frame);
            auto it = assets_->hud_textures().find(tex_name);
            if (it != assets_->hud_textures().end()) {
                // Draw the hit_blade sprite at the hit point (world space)
                float sz = 80.0f * s.scale;
                renderer_->draw_textured_quad(*it->second,
                    s.x - sz/2, s.y - sz/2, sz, sz, 0, 0, 1, 1);
            } else {
                // Fallback: colored circle (only if texture not loaded)
                float radius = (3.0f + t * 8.0f) * s.scale;
                uint8_t alpha = (uint8_t)(255 * (1.0f - t));
                ren::Color4B c{255, (uint8_t)(180 + std::rand() % 75),
                               (uint8_t)(40 + std::rand() % 80), alpha};
                renderer_->draw_filled_circle_world(s.x, s.y, radius, c);
            }
        }
    }

    // --- Enemy skeleton fighter state ---
    // State owned by combat_ module; access via mutable references.
    float& enemy_pos_x_ = combat_.mutable_enemy_pos_x();
    float& enemy_pos_y_ = combat_.mutable_enemy_pos_y();
    float& enemy_anim_time_ = combat_.mutable_enemy_anim_time();
    std::string& enemy_anim_ = combat_.mutable_enemy_anim();
    // [Soak-fix Wave 9A] F1: the resolved enemy hit-reaction anim + the
    // knockback velocity applied over the reaction (see apply_player_hit_feedback).
    std::string& enemy_reaction_anim_ = combat_.mutable_enemy_reaction_anim();
    float& enemy_knockback_vx_ = combat_.mutable_enemy_knockback_vx();
    bool& enemy_facing_right_ = combat_.mutable_enemy_facing_right();
    float& enemy_y_adjust_ = combat_.mutable_enemy_y_adjust();
    bool& enemy_attacking_ = combat_.mutable_enemy_attacking();
    float& enemy_attack_duration_ = combat_.mutable_enemy_attack_duration();
    // Guards the placeholder enemy's one hit per swing in battle mode (D4).
    bool enemy_attack_hit_done_ = false;
    bool& is_battle_mode_ = combat_.mutable_is_battle_mode();
    bool& show_enemy_ = combat_.mutable_show_enemy();
    // [H05/H06] The enemy's weapon subtype from the stages.xml warrior
    // template (<Item Name="WEAPON_SWORDS"> -> list.xml SubType="Swords").
    // Drives the enemy's REAL animation names (stance idle, attack). Fists
    // when no template weapon resolves (dojo disciple, generic test enemy).
    std::string enemy_weapon_subtype_ = "Fists";
    // [H06] The enemy weapon model FILE resolved from the template loadout
    // (list.xml Model + ".xml"); empty = unarmed (Fists loadout) or no
    // template matched.
    std::string enemy_weapon_file_;
    // [H06] True once a stages.xml <Template> matched battle_info_.enemy_name
    // — distinguishes "the loadout says Fists" from "no battle queued".
    bool enemy_template_resolved_ = false;

    // [P3] Armor capsules drawn by the player body render pass (test probe:
    // "the render path consumes the equipped armor model").
    int armor_capsules_drawn_ = 0;

    // ---------- Weapon system ----------
    // Currently equipped weapon type. Used to filter moves by tactic_weapon.
    // Move selection only allows moves whose tactic_weapon matches this value.
    std::string equipped_weapon_ = "Fists";    // [ORIGINAL] Ranged and magic slots. Empty means the corresponding
    // on-screen button is not shown — the original hides them when nothing is
    // equipped, which is what a fresh profile in the dojo looks like. Filled
    // by 5.4 (weapons) / 7.2 (saves).
    std::string equipped_ranged_;
    std::string equipped_magic_;
    // Cycle list for weapon switching (J key cycles, U key to previous).
    // [H01/R4] Built from the SAVE's OWNED weapons — the original has no
    // keyboard weapon cycle (J/U is a dev key; the game equips via the
    // inventory/shop). The old hardcoded tactic list handed the player
    // weapons they don't own (U -> Machete on a knives-only save —
    // HARDCODE_AUDIT H01) and included magic/ranged names that are not
    // melee cycles. Rebuilt by rebuild_weapon_cycle() whenever the
    // inventory/equipment changes (sync_equipped_weapon).
    std::vector<std::string> weapon_cycle_list_ = {"Fists"};
    int weapon_cycle_index_ = 0;

    // [H01/R4] Rebuild the J/U cycle from the owned list.xml Type="Weapon"
    // items (in list.xml order, deduped by SubType; Fists always first).
    void rebuild_weapon_cycle() {
        std::vector<std::string> next = {"Fists"};
        if (list_data_loaded_) {
            // Owned = inventory items + the equipped slots (the equipped
            // weapon leaves items_ — same union host_get_owned_items uses).
            std::vector<std::string> owned = inventory_.all_items();
            for (const auto& slot : inventory::kAllSlots) {
                const std::string eq = inventory_.equipped(slot);
                if (!eq.empty()) owned.push_back(eq);
            }
            for (const auto& li : list_data_.items) {
                if (li.type != "Weapon") continue;
                bool has = false;
                for (const auto& id : owned)
                    if (id == li.name) { has = true; break; }
                if (!has) continue;
                const std::string sub =
                    li.subtype.empty() ? li.name : li.subtype;
                bool dup = false;
                for (const auto& w : next)
                    if (w == sub) { dup = true; break; }
                if (!dup) next.push_back(sub);
            }
        }
        weapon_cycle_list_ = std::move(next);
        // Anchor the index on the currently equipped weapon.
        weapon_cycle_index_ = 0;
        for (std::size_t i = 0; i < weapon_cycle_list_.size(); ++i) {
            if (weapon_cycle_list_[i] == equipped_weapon_) {
                weapon_cycle_index_ = (int)i;
                break;
            }
        }
    }

    // Check if a move is allowed for the currently equipped weapon.
    // Returns true if the move has no tactic_weapon requirement,
    // or if the tactic_weapon matches the equipped weapon (substring match
    // supports pipe-delimited lists like "Swords|ShuangGou|ChineseSabers").
    bool is_weapon_allowed(const MoveDef& move) const {
        if (move.tactic_weapon.empty()) return true;
        std::string haystack = "|" + move.tactic_weapon + "|";
        return haystack.find("|" + equipped_weapon_ + "|") != std::string::npos;
    }

    // Cycle equipped weapon forward or backward.
    void cycle_weapon(int direction) {
        if (weapon_cycle_list_.empty()) return;
        weapon_cycle_index_ = (weapon_cycle_index_ + direction) % (int)weapon_cycle_list_.size();
        if (weapon_cycle_index_ < 0) weapon_cycle_index_ += (int)weapon_cycle_list_.size();
        equipped_weapon_ = weapon_cycle_list_[weapon_cycle_index_];
        // Load the weapon model for the new weapon
        load_player_weapon(equipped_weapon_);
        std::printf("[WEAPON] Switched to: %s (index %d/%zu)\n",
                    equipped_weapon_.c_str(), weapon_cycle_index_, weapon_cycle_list_.size());
    }

    // ---------- Projectile system (magic/ranged) ----------
    struct Projectile {
        float x, y;
        float vx, vy;
        float lifetime = 2.0f;      // seconds remaining
        float age = 0;              // seconds since launch
        float damage = 15.0f;
        float radius = 8.0f;
        std::string type;           // "FireBall", "Energyball", "MagicDeathRay", "Shuriken", etc.
        bool from_player = true;
        bool active = true;
        uint8_t r = 255, g = 100, b = 50;  // color
        // [H10] The magic item's list.xml Model (magic_fireball.xml etc.) —
        // the real visual source the original renders (magic_* effect
        // sequences per moves.xml); colored circles are the fallback.
        std::string model_file;
    };
    std::vector<Projectile> projectiles_;

    // [H10] Resolve the REAL magic data from list.xml: the magic item's
    // MagicDamage attr is the projectile's damage (MAGIC_FIRE_BALL
    // MagicDamage="322", MAGIC_ENERGY_BALL "372", MAGIC_LIGHTNING_ARROW
    // "609" — verified reverse/data/list.xml) and Model attr is the visual
    // model. The old palette {255,100,50} dmg 20 etc. was invented
    // (HARDCODE_AUDIT H10).
    void resolve_magic_item_data(const std::string& magic_type,
                                 float& out_damage,
                                 std::string& out_model) const {
        if (!list_data_loaded_) return;
        for (const auto& li : list_data_.items) {
            if (li.type != "Magic" || li.subtype != magic_type) continue;
            if (li.magic_damage > 0.0f) out_damage = li.magic_damage;
            if (!li.model.empty()) out_model = li.model + ".xml";
            return;  // first match = the canonical item of the subtype
        }
    }

    // Spawn a projectile from the player (or enemy) toward the target.
    void spawn_projectile(const std::string& magic_type, float from_x, float from_y,
                          bool facing_right, bool from_player = true) {
        Projectile p;
        p.x = from_x + (facing_right ? 40.0f : -40.0f);
        p.y = from_y + 10.0f;
        float speed = 400.0f;
        p.vx = facing_right ? speed : -speed;
        p.vy = 0.0f;
        p.type = magic_type;
        p.from_player = from_player;
        p.lifetime = 2.0f;
        p.age = 0;

        // [H10] Damage and visual model come from list.xml (the magic
        // item's MagicDamage / Model attrs); the per-type palette below is
        // the COLOR fallback only — the original renders the magic effect
        // sequences moves.xml names (magic_fireball_start / _middle), which
        // the dump ships but the engine does not render for projectiles yet
        // ([HEURISTIC-TODO]: colors are the engine's stand-in).
        float dmg = 15.0f;
        std::string model;
        resolve_magic_item_data(magic_type, dmg, model);
        p.damage = dmg;
        p.model_file = std::move(model);

        // Color by magic type — FALLBACK palette only (damage comes from
        // list.xml MagicDamage above; the invented per-type damage values
        // are gone, HARDCODE_AUDIT H10). Spellings are the list.xml
        // SubType values the engine spawns with.
        if (magic_type == "FireBall" || magic_type == "Fireball") { p.r = 255; p.g = 100; p.b = 50; p.radius = 10; }
        else if (magic_type == "Energyball" || magic_type == "EnergyBall") { p.r = 100; p.g = 200; p.b = 255; p.radius = 12; }
        else if (magic_type == "LightningArrow") { p.r = 255; p.g = 255; p.b = 0; p.radius = 6; p.vy = -30; }
        else if (magic_type == "MagicDeathRay") { p.r = 200; p.g = 50; p.b = 255; p.radius = 14; }
        else if (magic_type == "MagicAsteroid") { p.r = 150; p.g = 80; p.b = 20; p.radius = 16; p.vy = -100; }
        else if (magic_type == "MassBomb" || magic_type == "MagicBomb") { p.r = 255; p.g = 50; p.b = 50; p.radius = 18; }
        else if (magic_type == "Iceball" || magic_type == "IceBall") { p.r = 150; p.g = 200; p.b = 255; p.radius = 9; }
        else if (magic_type == "MagicFireAura") { p.r = 255; p.g = 150; p.b = 50; p.radius = 20; }
        else if (magic_type == "RootStun") { p.r = 50; p.g = 200; p.b = 50; p.radius = 12; p.vy = -50; }
        else if (magic_type == "Shuriken") { p.r = 200; p.g = 200; p.b = 200; p.radius = 5; }
        else if (magic_type == "Rifle" || magic_type == "Blaster") { p.r = 255; p.g = 255; p.b = 200; p.radius = 4; speed = 600; p.vx = facing_right ? speed : -speed; }
        else { p.r = 200; p.g = 100; p.b = 200; p.radius = 8; }

        projectiles_.push_back(p);
        std::printf("[PROJECTILE] Fired %s (%.0f,%.0f) v=(%.0f,%.0f)\n",
                   magic_type.c_str(), p.x, p.y, p.vx, p.vy);
    }

    // Update all active projectiles (movement, lifetime, hit detection)
    void update_projectiles(float dt_sec) {
        // Heuristic: which projectile types to auto-fire on attack
        static const std::vector<std::string> projectile_types = {
            "FireBall", "Energyball", "LightningArrow", "MagicDeathRay",
            "MagicAsteroid", "MassBomb", "MagicBomb", "Iceball",
            "MagicFireAura", "RootStun", "Shuriken", "Rifle", "Blaster"
        };

        // Auto-spawn: if the current move's tactic_weapon indicates a projectile weapon,
        // and we're at the attack interval start, spawn a projectile.
        // This is a simplified heuristic — the original game uses dedicated
        // magic/ranged move templates with projectile spawning events.
        if (!current_move_.empty() && hit_this_interval_ == false) {
            auto mit = assets_->moves().find(current_move_);
            if (mit != assets_->moves().end() && mit->second.is_attack) {
                // Check if equipped weapon is a projectile type
                bool is_projectile = false;
                for (auto& pt : projectile_types) {
                    if (equipped_weapon_.find(pt) != std::string::npos) {
                        is_projectile = true;
                        break;
                    }
                }
                if (is_projectile) {
                    // Check attack interval timing from the move's Interval list
                    for (auto& iv : mit->second.intervals) {
                        if (iv.type != "Attack" && iv.name != "Attack") continue;
                        float anim_progress = anim_time_ * anim_fps_;
                        if (anim_progress >= iv.start && anim_progress <= iv.start + 1.0f) {
                            spawn_projectile(equipped_weapon_, player_pos_x_, player_pos_y_,
                                            facing_right_, true);
                            hit_this_interval_ = true;  // prevent re-fire same interval
                            break;
                        }
                    }
                }
            }
        }

        // Update existing projectiles
        for (auto& p : projectiles_) {
            if (!p.active) continue;
            p.x += p.vx * dt_sec;
            p.y += p.vy * dt_sec;
            p.age += dt_sec;
            p.lifetime -= dt_sec;
            if (p.lifetime <= 0) { p.active = false; continue; }

            // Hit detection against enemy (player projectiles)
            if (p.from_player) {
                float dist = std::sqrt(std::pow(p.x - enemy_pos_x_, 2) +
                                       std::pow(p.y - (enemy_pos_y_ + enemy_y_adjust_), 2));
                if (dist < 60.0f && !enemy_fighter_.is_dead) {
                    // Hit!
                    enemy_fighter_.health -= p.damage;
                    if (enemy_fighter_.health < 0) enemy_fighter_.health = 0;
                    enemy_hit_flash_ = 0.15f;
                    p.active = false;
                    // Add hit spark
                    HitSpark spark;
                    spark.x = p.x; spark.y = p.y;
                    spark.age = 0; spark.lifetime = 0.3f;
                    spark.scale = 1.0f + p.damage / 30.0f;
                    hit_sparks_.push_back(spark);
                    std::printf("[PROJECTILE] Hit! damage=%.0f enemy_hp=%.0f\n",
                               p.damage, enemy_fighter_.health);
                    if (enemy_fighter_.health <= 0) {
                        enemy_fighter_.is_dead = true;
                        std::printf("[COMBAT] Enemy defeated by %s!\n", equipped_weapon_.c_str());
                    }
                }
            }
        }
        // Clean up inactive projectiles
        projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
            [](const Projectile& p) { return !p.active; }), projectiles_.end());
    }

    // Render active projectiles as colored circles
    void render_projectiles() {
        for (auto& p : projectiles_) {
            if (!p.active) continue;
            float pulse_scale = 1.0f + 0.1f * std::sin(p.age * 15.0f);
            float r = p.radius * pulse_scale;
            renderer_->draw_filled_circle_world(p.x, p.y, r,
                ren::Color4B{p.r, p.g, p.b, 200});
            // Glow effect (larger, transparent)
            if (r > 4.0f) {
                renderer_->draw_filled_circle_world(p.x, p.y, r * 1.5f,
                    ren::Color4B{p.r, p.g, p.b, 80});
            }
        }
    }

    // [ORIGINAL] The very first visit to the dojo opens with the sensei's
    // scroll — "Сначала покажи, как ты двигаешься!" — which is what the
    // reference screenshot of a first launch shows. It stays up until the
    // player moves, and never comes back once they have. Progress-driven
    // tutorial steps beyond this one are 7.3.
    Overlay overlay_ = Overlay::Dialog;
    bool intro_hint_dismissed_ = false;
    // [ORIGINAL] Tutorial flow: the Sensei dialog plays FIRST (full-screen
    // Dialogue scene), then the hint scroll appears in the upper-right.
    // tutorial_dialog_pending_ = true means the dialog should auto-trigger
    // on the first MainMenu update. tutorial_dialog_shown_ = true means the
    // dialog has already played and the hint scroll should now appear.
    bool tutorial_dialog_pending_ = false;
    bool tutorial_dialog_shown_ = false;
    // Scroll unroll animation for the tutorial hint overlay (0..1 progress).
    // The hint unrolls right-to-left like a scroll when it first appears.
    float dialog_overlay_anim_ = 0.0f;
    // Grace period: frames since overlay appeared. Prevents accidental
    // dismissal from keys held during the preceding dialog scene.
    uint32_t overlay_show_frames_ = 0;
    // Tutorial bag hit counter: advances tutorial after N bag hits.
    int tutorial_bag_hits_ = 0;
    // [Q1] Steps taken since the movement hint appeared. The quest movement
    // stage completes after kTutorialMoveSteps step events (a step begins
    // when the movement state enters MOVING), not on the first press.
    static constexpr int kTutorialMoveSteps = 4;
    int tutorial_move_steps_ = 0;
    int last_step_state_ = 0;
    // Player X position when the hint scroll appeared. Used to verify the
    // player ACTUALLY moved (position changed) before dismissing the hint.
    float hint_start_player_x_ = 0.0f;
    float menu_anim_progress_ = 0.0f;  // 0 = collapsed, 1 = fully expanded
    bool loc_icons_logged = false;  // one-shot diagnostic for menu icon sizes
    float load_scale_ = 1.0f, zoom_ = 1.0f;
    resf2::game::GameLocation* location_ = nullptr;

    float player_pos_x_ = 0, player_pos_y_ = 0;
    // [STEP 4.7] Gameplay Y offset layer for knockback/knockdown effects.
    // Applied ON TOP of animation data — shifts the entire fighter vertically
    // in world space. The animation still controls the pose; this adds an
    // additional world-Y shift for launch/knockback physics.
    float gameplay_y_offset_ = 0.0f;
    float y_velocity_ = 0.0f;         // vertical velocity for knockback
    bool is_knocked_down_ = false;    // knockdown state flag
    int knockdown_timer_ = 0;         // recovery timer (frames)
    static constexpr float kKnockbackGravity = 800.0f;  // world units/sec^2
    float cam_x_ = 0, cam_y_ = 0;
    // Debug world overlay, toggled with F1 or started with --debug-world.
    bool debug_world_ = false;
    // [H09] HUD stats come from the LOADED SAVE (users.xml Level= / Money=,
    // synced in host_load_progress); the old constants 7 / 72450 / 9 were
    // invented (HARDCODE_AUDIT H09). Gems: the users.xml schema ships NO
    // ruby attribute (Money/PaidMoney/Bonus only, verified in
    // reverse/data/users.xml) — [HEURISTIC-TODO] stays 0 until a device
    // pull pins the original ruby source.
    int hud_level_ = 1;
    int hud_gold_ = 0;
    int hud_gems_ = 0;
    // <Word Title="KEY">text</Word> pairs from assets/localizations/<lang>.xml.
    std::unordered_map<std::string, std::string> localization_;
    // [ORIGINAL] Floor plane of the current location: -Height/2 + Floor
    // (Location::load, ShadowFight2.s86 FUN_10144420: +0x3c Height, +0x2c Floor).
    // Animations are authored with their floor at y = 0, so this is the datum
    // every animated node's Y is measured from.
    float floor_world_y_ = 0.0f;
    bool facing_right_ = true;
    // [M5] Deferred-turn state: the desired (opponent-facing) direction
    // tracked per frame but applied at the controlled move starts (Wave 11A
    // M2 facing law — SetDirection at move start only, never on input).
    bool desired_facing_right_ = true;
    // [M5] Visual turn blend for the player's render mirror: eases toward
    // the facing sign over a few frames so a turn reads as a short rotation
    // instead of a one-frame snap. ±1 = fully facing right/left.
    float player_turn_blend_ = 1.0f;
    uint32_t& hit_anim_ = combat_.mutable_hit_anim();  // ms remaining
    uint32_t& step_cooldown_ = combat_.mutable_step_cooldown();
    bool& step_active_ = combat_.mutable_step_active();
    uint32_t& step_duration_ = combat_.mutable_step_duration();
    float& step_start_x_ = combat_.mutable_step_start_x();
    float& step_displacement_ = combat_.mutable_step_displacement();
    int& bag_swing_ = combat_.mutable_bag_swing();
    bool& hit_this_interval_ = combat_.mutable_hit_this_interval();
    float& bag_swing_dir_ = combat_.mutable_bag_swing_dir();
    // Physics-based pendulum state for the punching bag.
    // The bag hangs from Node12 (fixed ceiling point) and swings as a pendulum.
    // On hit: an impulse is applied to bag_angle_vel_.
    // Each frame: spring restoring force + damping + integration.
    float& bag_angle_ = combat_.mutable_bag_angle();
    float& bag_angle_vel_ = combat_.mutable_bag_angle_vel();
    // Verlet physics state for the punching bag.
    // The original game uses Verlet integration for the bag's skeleton.
    // Each node has position + previous position. Edges are distance constraints.
    // Fixed nodes (Node12 = ceiling attachment) don't move.
    std::unordered_map<std::string, VerletNode> bag_verlet_;
    std::vector<VerletConstraint> bag_constraints_;
    bool bag_verlet_init_ = false;
    bool quit_requested_ = false;
    
    // Animation state (owned by anim_player_ module)
    // Reference aliases below make existing code work without changes.
    std::string& current_anim_ = anim_player_.mutable_current_anim();
    float& anim_time_ = anim_player_.mutable_anim_time();
    float& anim_speed_ = anim_player_.mutable_anim_speed();
    bool& anim_loop_ = anim_player_.mutable_anim_loop();
    float& anim_fps_ = anim_player_.mutable_anim_fps();
    std::unordered_map<std::string, std::pair<float, float>>& anim_node_pos_ = anim_player_.mutable_anim_node_pos();
    float& anim_root_dx_ = anim_player_.mutable_anim_root_dx();
    float& anim_root_dy_ = anim_player_.mutable_anim_root_dy();
    float& anim_root_anchor_x_ = anim_player_.mutable_anim_root_anchor_x();
    float& anim_root_anchor_y_ = anim_player_.mutable_anim_root_anchor_y();
    bool& anim_anchor_set_ = anim_player_.mutable_anim_anchor_set();
    float& prev_npivot_x_ = anim_player_.mutable_prev_npivot_x();
    bool& prev_npivot_set_ = anim_player_.mutable_prev_npivot_set();
    float& prev_npivot_y_ = anim_player_.mutable_prev_npivot_y();
    bool& prev_npivot_y_set_ = anim_player_.mutable_prev_npivot_y_set();
    int& prev_frame_idx_ = anim_player_.mutable_prev_frame_idx();
    float& jump_y_offset_ = anim_player_.mutable_jump_y_offset();
    float& prev_root_offset_ = anim_player_.mutable_prev_root_offset();
    float& committed_root_x_ = anim_player_.mutable_committed_root_x();
    float& prev_root_offset_x_ = anim_player_.mutable_prev_root_offset_x();
    float& prev_root_offset_y_ = anim_player_.mutable_prev_root_offset_y();
    float& step_start_player_x_ = anim_player_.mutable_step_start_player_x();
    bool& anim_facing_right_ = anim_player_.mutable_anim_facing_right();
    float& y_adjust_smoothed_ = anim_player_.mutable_y_adjust_smoothed();
    uint64_t& total_frame_count_ = anim_player_.mutable_total_frame_count();
    int& priority_ = anim_player_.mutable_priority();
    // Combat state aliases (owned by combat_ member)
    std::string& current_move_ = combat_.mutable_current_move();
    int& no_key_frames_ = combat_.mutable_no_key_frames();
    int& move_state_ = combat_.mutable_move_state();
    bool& start_stance_playing_ = combat_.mutable_start_stance_playing();
    bool& need_switch_to_idle_ = combat_.mutable_need_switch_to_idle();
    uint32_t& step_cooldown_ms_ = combat_.mutable_step_cooldown();
    bool& is_uninterrupt_ = combat_.mutable_is_uninterrupt();
    // Module instances (owned via PImpl, initialized in game.cpp)
    std::unique_ptr<AssetManager> assets_;

    // [ORIGINAL] Enemy AI weight tables (tacticSettings.xml). Drives the
    // roulette-wheel animation pick that replaced the invented state machine.
    TacticSettings tactics_;
    // [D3] Table families (assets/tactics) for the TacticDecisionPipeline
    // (ADR-005 D1/D7): stage tables + the adapter's candidate classification.
    // Loaded beside tactics_ in load_tactics(); empty set = no tables.
    TacticTableSet tactic_tables_;
    // Last roulette decision, kept for the F1 debug overlay.
    std::string ai_last_pick_;                        // chosen category
    std::vector<std::string> ai_last_candidates_;     // parallel to weights
    std::vector<float> ai_last_weights_;              // evaluated weights
    float ai_last_distance_ = 0;                      // ctx.distance used
    // [E2] The stored decision the execute block consumes directly (ADR-005
    // Phase B): the executor switches on this, not the legacy enemy_ai_state_
    // int (bypassed; deleted with the fallback branches in Phase E).
    TacticDecision ai_last_decision_;

    // [E2] Attack/step/block classification of a decision animation — the
    // D7 mapping rows inlined for direct consumption (the adapter is
    // bypassed from Phase B and deleted in Phase E). Table-candidate lookup
    // first, then the [HEURISTIC-TODO] name-list fallback carrying the
    // category names tacticSettings.xml actually ships.
    bool ai_anim_in_candidates(const std::string& anim,
                               TacticTableType type) const {
        if (anim.empty()) return false;
        for (const TacticTable& t : tactic_tables_.tables()) {
            if (t.type != type) continue;
            for (const std::string& c : t.candidates) {
                if (c == anim) return true;
            }
        }
        return false;
    }
    bool ai_anim_is_attack(const std::string& anim) const {
        return ai_anim_in_candidates(anim, TacticTableType::kAttackTable) ||
               anim == "ShortAttack";
    }
    bool ai_anim_is_step(const std::string& anim) const {
        return ai_anim_in_candidates(anim, TacticTableType::kMovementsTable) ||
               anim == "ForwardStep" || anim == "BackStep" || anim == "Retreat";
    }
    bool ai_anim_is_retreat(const std::string& anim) const {
        return anim == "BackStep" || anim == "Retreat";
    }
    bool ai_anim_is_block(const std::string& anim) const {
        return anim == "Duck" || anim == "Block";
    }
    // [E2] F1 overlay state name, derived from the stored decision — the
    // legacy enemy_ai_state_ int is no longer the executor's source of
    // truth (same rows the D7 adapter mapped: attack > step > defense >
    // block > idle).
    const char* ai_display_state() const {
        const TacticDecision& d = ai_last_decision_;
        if (d.stage != DecisionStage::kIdle) {
            if (ai_anim_is_attack(d.animation)) return "attack";
            if (ai_anim_is_step(d.animation)) {
                return ai_anim_is_retreat(d.animation) ? "retreat" : "approach";
            }
            if (d.stage == DecisionStage::kUseDefense ||
                ai_anim_is_block(d.animation)) {
                return "block";
            }
        }
        return "idle";
    }
    InputHandler input_handler_;

    // Debug overlay input cache — updated every frame in host_update_gameplay(),
    // read by render_debug_overlay() to show live key state.
    bool dbg_key_forward_ = false;
    bool dbg_key_back_ = false;
    bool dbg_key_up_ = false;
    bool dbg_key_down_ = false;
    bool dbg_punch_pressed_ = false;
    bool dbg_kick_pressed_ = false;

    // Debug overlay: last damage calculation breakdown (for F1 COMBAT panel)
    float dbg_last_base_damage_ = 0;
    float dbg_last_attr_mult_ = 1.0f;
    float dbg_last_block_factor_ = 1.0f;
    float dbg_last_attack_factor_ = 1.0f;
    float dbg_last_crit_factor_ = 1.0f;
    float dbg_last_factor_set_ = 1.0f;
    float dbg_last_final_damage_ = 0;
    std::string dbg_last_move_name_;

    bool replay_mode_ = false;  // skip menus, go directly to Battle  // true when current frame is in Uninterrupt interval
    bool dump_state_ = false;  // --dump-state: print structured state every frame
    // A hermetic run reads no state from the machine — no saved profile, no
    // saved inventory. Set for scripted runs so a measurement is reproducible
    // on any machine and in any order relative to the tests that write saves.
    bool hermetic_run_ = false;
    // [Wave 8] Boot-order probe: every loader records its event here in load
    // order; the fidelity test compares the sequence to the LIVE_BOOT_TRACE
    // chronology (moves.xml 12.56 -> save 15.82 -> list.xml 15.84 -> stages
    // 16.8 -> quests 17.67 -> packs 18.36 -> config_cdn 18.37).
    std::vector<std::string> boot_events_;
    // Boot-time configs (perks/forge/CharacterProgress/Achievements +
    // quests/config_cdn) parsed before/after the save, per the original.
    resf2::game::BootConfigs boot_configs_;
    // One-shot guards so init_location (runs per location change) records
    // and re-loads each config exactly once.
    bool moves_loaded_ = false;
    bool quests_config_loaded_ = false;
    bool packs_config_loaded_ = false;
    bool cdn_config_loaded_ = false;
    // Does the move currently playing leave the model with a "current node"?
    // Only moves that declare an <Align> pivot do; see apply_align().
    bool prev_move_had_align_ = false;
    TouchControls touch_;       // on-screen controls, updated once per frame
    std::string start_scene_;   // --scene <name>[:<arg>], empty = normal Boot flow
    int start_scene_arg_ = -1;  // the ":N" part, e.g. the map's initial zone
    // [Wave 10A defect 3] E2E hooks: --round-time overrides every battle's
    // round clock (stages.xml RoundTime is 99 s everywhere, too slow to
    // drive a timeout in a scripted run); --tutorial-start runs check_tutorial
    // at boot even in hermetic (--input-script) runs so the script can walk
    // the Sensei -> bag -> Kenji fight flow deterministically.
    int round_time_override_s_ = 0;
    bool tutorial_start_flag_ = false;
    int round_left_ms_probe_ = 0;   // pushed by BattleScene each frame
    std::string equip_magic_hook_;  // --equip-magic <item id>
    std::string equip_weapon_hook_; // --equip-weapon <item id>
    // [Wave 11A M4] --crit-attr <chance> <damage> (-1 = not set).
    int crit_attr_hook_chance_ = -1;
    int crit_attr_hook_damage_ = -1;
    // Animation debug/TODO state
    float stance_npivot_y_ = 106.0f;     // NPivot Y from stance anim (default from params.xml)
    float anim_npivot_bin_y_ = 0.0f;     // NPivot Y from current .bin animation frame
    std::string last_logged_anim_;       // last animation name logged (suppress duplicates)
};

} // namespace resf2::game



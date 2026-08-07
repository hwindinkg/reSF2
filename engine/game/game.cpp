// engine/game/game.cpp
//
// Game class implementation — extracted from game.hpp inline bodies.

#include "game.hpp"
#include "settings_loader.hpp"
#include "attribute_aggregation.hpp"
#include "damage_formula.hpp"
#include "tactic_pipeline.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <filesystem>

namespace resf2::game {

namespace {

// [Wave 11B W2] Arena x-bounds, original semantics (SPEC_WORLD_FEEL 3b,
// VERIFY_W11 3): the fighters stop AT the WALL OBJECTS - the world X of
// the <Image ClassName="left"/"right"> sprites in the location's
// params.xml (dojo +-680) - NOT at +-width/2 (+-980, an engine
// invention: the binary feeds Wall (+0x34) and Width-Wall to the physics
// globals and the wall geometry comes from the left/right anchors; no
// width/2 clamp exists in the image). Falls back to +-width/2 only for
// locations that author no wall sprites.
void location_wall_bounds(const GameLocation* loc, float& lo, float& hi) {
    if (loc && loc->wall_left_x != 0.0f && loc->wall_right_x != 0.0f) {
        lo = loc->wall_left_x;
        hi = loc->wall_right_x;
        return;
    }
    const float half = loc ? loc->width * 0.5f : 0.0f;
    lo = -half;
    hi = half;
}

}  // namespace

// Constructor
Game::Game(std::string asset_root, bool replay_mode, bool dump_state)
    : asset_root_(std::move(asset_root)), replay_mode_(replay_mode), dump_state_(dump_state) {
    save_manager_.set_asset_root(asset_root_);
    discover_locations();
}

// Destructor (required for unique_ptr members with forward-declared types)
Game::~Game() = default;

void Game::play_animation(const std::string& name, bool loop, int priority) {
    auto& animations = assets_->animations();
    auto& moves = assets_->moves();
    if (!animations.count(name)) return;

    // Playback rate and start frame come from the MoveDef whose filename
    // matches this animation. Resolve them first so they can be handed to
    // AnimationPlayer::play() rather than patched in afterwards.
    float fps = 20.0f;  // default: matches MidFrames=2 (60/3=20)
    int first_frame = -1;
    const MoveDef* move_def = nullptr;
    {
        std::string name_no_bin = name;
        if (name_no_bin.size() > 4 &&
            name_no_bin.substr(name_no_bin.size() - 4) == ".bin")
            name_no_bin = name_no_bin.substr(0, name_no_bin.size() - 4);

        for (const auto& [mname, move] : moves) {
            std::string mfile = move.filename;
            if (mfile.size() > 4 &&
                mfile.substr(mfile.size() - 4) == ".bin")
                mfile = mfile.substr(0, mfile.size() - 4);
            if (mfile == name_no_bin) {
                fps = 60.0f / (1.0f + move.mid_frames);
                first_frame = move.first_frame;
                move_def = &move;
                break;
            }
        }
    }

    // The pose we are about to leave, captured before AnimationPlayer::play()
    // resets it. apply_align() needs the anchor node's current world position.
    const float prev_anchor_rel_x =
        (move_def && !move_def->moveinside_pivot_node.empty())
            ? [&] {
                  auto it = anim_node_pos_.find(move_def->moveinside_pivot_node);
                  return it == anim_node_pos_.end() ? 0.0f : it->second.first;
              }()
            : 0.0f;
    const bool prev_anchor_known =
        move_def && !move_def->moveinside_pivot_node.empty() &&
        anim_node_pos_.count(move_def->moveinside_pivot_node) > 0;

    const std::string prev_anim = current_anim_;
    const int prev_priority = priority_;

    // The priority gate and the core playback reset live in AnimationPlayer.
    // This used to be a second, independent copy of that logic here; the two
    // drifted apart and a fix applied to the player alone had no effect,
    // because this copy rejected the animation first.
    if (!anim_player_.play(name, animations, fps, loop, priority)) return;

    if (prev_anim != name) {
        std::printf("[ANIM] play_animation('%s', loop=%d, prio=%d) — switching from '%s' (prio=%d)\n",
                    name.c_str(), loop, priority, prev_anim.c_str(), prev_priority);
    }

    if (first_frame >= 0 && anim_fps_ > 0.0f)
        anim_time_ = static_cast<float>(first_frame) / anim_fps_;

    // [M2] Whole-body-translate moves (rolls, dash, flips) play their
    // authored NPivot root motion RAW: the one-shot align placement would
    // snap the fighter onto the roll's (far-travelling) anchor — measured:
    // forward_roll from idle started 38 px BACKWARD — and the per-frame
    // pinning (guarded in AnimationPlayer::update) would cancel the motion.
    // prev_move_had_align_ is still recorded below so the move AFTER a roll
    // keeps its normal alignment continuity.
    if (!is_root_motion_travel_anim(name))
        apply_align(name, move_def, first_frame, prev_anchor_rel_x, prev_anchor_known);
    // Remember whether the move now playing leaves the model with a current
    // node, i.e. whether it declares an <Align> pivot. See apply_align().
    // Tell the player which node this move pins, so its per-frame root motion
    // holds that node instead of following NPivot. Empty for the steps.
    // [ORIGINAL] setCurrentNode @ 0x10165c10: a move with no <Align> leaves
    // currentNode = null, so the NEXT move has nothing to anchor to. Per-frame
    // pinning must therefore only activate when the previous move had an Align
    // (i.e. currentNode was set). Without this, double_step_forward after a
    // step_forward was pinned in place instead of dashing +220 forward.
    anim_player_.set_align_anchor(
        (move_def && move_def->has_align && move_def->align_x &&
         move_def->align_pivot_object == MoveDef::AlignObject::Nodes &&
         prev_move_had_align_)
            ? move_def->moveinside_pivot_node : std::string());
    prev_move_had_align_ = move_def && move_def->has_align &&
                           move_def->align_pivot_object == MoveDef::AlignObject::Nodes &&
                           !move_def->moveinside_pivot_node.empty();

    // Game-side state that AnimationPlayer does not own.
    anim_root_dx_ = 0.0f;
    anim_root_dy_ = 0.0f;
    prev_root_offset_ = 0.0f;
    prev_npivot_y_set_ = false;
    anim_facing_right_ = facing_right_;
    if (name != "jump" && name != "jump_away" &&
        name != "front_flip" && name != "back_flip" &&
        name != "back_handflip") {
        jump_y_offset_ = 0.0f;
    }
}

// [ORIGINAL] Root placement at animation start.
//
// `ModelAnimation::playInfo` @ 0x10164fa0 runs, in this order:
//     FUN_10104980(move, container, firstFrame)   load the animation frames
//     ModelAnimation::mirrorNodes  @ 0x10164c20   swap L/R node indices when
//                                                 the model faces the other way
//     setCurrentNode(align.pivotID) @ 0x10165c10  model->currentNode = the node
//                                                 named by <Pivot Part>, still
//                                                 holding the position it has
//                                                 in the pose being left
//     Model::alignAnimation @ 0x101661d0          the placement itself
//
// alignAnimation computes, in the model's own space:
//
//     pivot  = <Pivot Object>    Nodes     -> the anchor node in the animation's
//                                            first frame
//                                Animation -> (0,0,0)
//                                Wall      -> +-Location wall X
//                                Pivot     -> the model's current node
//     target = <Position Object> Pivot     -> the model's current node  (658/800)
//                                Nodes     -> a named node on the chosen player
//                                Animation -> the model's previous align offset
//                                Wall      -> +-Location wall X
//     target.x += facing * ShiftX      (0x10166403: MULSS by the +-1 facing byte)
//     target.y += ShiftY               (no facing factor)
//     offset  = target - pivot         (0x1028e890, vec3 subtract)
//     offset.{x,y,z} &= <Align Axis>   (byte flags at move+0x84/85/86)
//     translate every node by offset   (0x10166690)
//
// The net effect for the common case — 613 of the 800 <Align> blocks anchor a
// heel, and 658 target Object="Pivot" — is that the anchor node keeps its world
// position across the animation change and the body is placed around it. Once
// placed, the model just follows the animation data, which is exactly what the
// per-frame NPivot delta in on_update already does.
//
// A move with no <Align> keeps the ctor defaults (Align ctor @ 0x10101c60 sets
// both Object enums to 0 and leaves every axis flag clear), so the offset is
// zero and nothing is repositioned. All 91 such moves are StepForward /
// StepBack / Shop* variants: their locomotion lives in the animation data
// alone. That is why walking must NOT be re-anchored and attacking must be.
//
// In this engine a node's world X is `player_pos_x_ + facing * (node_x -
// npivot_x)`, so pinning the anchor is a one-shot correction of player_pos_x_.
void Game::apply_align(const std::string& anim_name, const MoveDef* move,
                       int first_frame, float prev_anchor_rel_x,
                       bool prev_anchor_known) {
    if (!move || !move->has_align) return;
    // X is the only axis this engine can place: vertical comes straight from
    // the animation (PORT_PLAN 3.1) and Z is depth. 744 of 800 blocks declare
    // exactly "X|Z", the other 56 "X|Y|Z" — X is always controlled.
    if (!move->align_x) return;
    if (move->align_pivot_object != MoveDef::AlignObject::Nodes) return;
    if (move->moveinside_pivot_node.empty()) return;
    // Only the Object="Pivot" target is modelled: it means "the node's own
    // current position", which is the continuity case. Nodes/Wall/Animation
    // targets need the enemy model and the location walls — see PORT_PLAN 4.3.
    if (move->align_position_object != MoveDef::AlignObject::Pivot) return;
    if (!prev_anchor_known) return;   // no previous pose: nothing to anchor to

    // `Position Object="Pivot"` means "where the model's CURRENT NODE is", and
    // the current node is set by setCurrentNode @ 0x10165c10 from the align
    // pivot of the move being started — but only when that move HAS one:
    //
    //     if (moveInfo->align.pivotID >= 0) { currentNode = nodes[pivotID]; }
    //     else { currentNode = 0; warn("align.pivotID == -1"); }
    //
    // A move with no <Align> therefore leaves the model with NO current node,
    // and the next move has nothing to anchor to. All 91 such moves are the
    // steps, so this is exactly the "walk was interrupted" case.
    //
    // Anchoring across it anyway is wrong and measurably so: step_forward
    // leaves its heel 49.8 units behind NPivot while stance_idle wants it 30.3
    // behind, so every interrupted step yanked the body 19.5 units backwards.
    // Tapping forward gained 11.6 from the step and lost 19.5 to the snap —
    // a net 7.9 BACKWARDS per tap, which is why spamming the key walked the
    // fighter back to where he started.
    if (!prev_move_had_align_) return;

    const auto& animations = assets_->animations();
    auto ait = animations.find(anim_name);
    if (ait == animations.end()) return;
    const auto& anim = ait->second;

    const auto& order = assets_->ordered_node_names();
    int anchor_idx = -1, npivot_idx = -1;
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        if (order[i] == move->moveinside_pivot_node) anchor_idx = i;
        else if (order[i] == "NPivot") npivot_idx = i;
    }
    if (anchor_idx < 0 || npivot_idx < 0) return;

    int f0 = (first_frame > 0) ? first_frame : 0;
    if (f0 >= anim.frame_count) f0 = 0;

    float ax, ay, az, px, py, pz;
    if (!anim.get_node_pos(f0, anchor_idx, ax, ay, az)) return;
    if (!anim.get_node_pos(f0, npivot_idx, px, py, pz)) return;

    const float sign = facing_right_ ? 1.0f : -1.0f;
    const float anchor_world_x = player_pos_x_ + sign * prev_anchor_rel_x;
    const float rel_new = ax - px;   // anchor relative to NPivot, model space
    // The per-frame pinning must continue from exactly this pose, or its first
    // delta is measured against nothing and the anchor slips by one frame of
    // animation right after being placed.
    anim_player_.seed_align_rel(rel_new);
    const float placed = anchor_world_x + sign * move->align_shift_x
                       - sign * rel_new;

    if (dump_state_) {
        std::printf("[ALIGN] f=%llu anim='%s' anchor='%s' held_x=%.2f "
                    "shift=%.1f rel_new=%.2f player %.2f -> %.2f (d=%.2f)\n",
                    (unsigned long long)total_frame_count_, anim_name.c_str(),
                    move->moveinside_pivot_node.c_str(), anchor_world_x,
                    move->align_shift_x, rel_new, player_pos_x_, placed,
                    placed - player_pos_x_);
    }
    player_pos_x_ = placed;
}

// The MoveDef driving the animation on screen right now, if that move carries
// an <Align> this engine actually applies. Same predicate as apply_align().
const MoveDef* Game::current_align_move() const {
    std::string name_no_bin = current_anim_;
    if (name_no_bin.size() > 4 &&
        name_no_bin.substr(name_no_bin.size() - 4) == ".bin")
        name_no_bin = name_no_bin.substr(0, name_no_bin.size() - 4);
    for (const auto& [mname, move] : assets_->moves()) {
        std::string mfile = move.filename;
        if (mfile.size() > 4 && mfile.substr(mfile.size() - 4) == ".bin")
            mfile = mfile.substr(0, mfile.size() - 4);
        if (mfile != name_no_bin) continue;
        if (move.has_align && move.align_x &&
            move.align_pivot_object == MoveDef::AlignObject::Nodes &&
            move.align_position_object == MoveDef::AlignObject::Pivot &&
            !move.moveinside_pivot_node.empty())
            return &move;
        return nullptr;
    }
    return nullptr;
}

void Game::update_animation(uint32_t dt_ms) {
    anim_player_.update(dt_ms, assets_->animations(), assets_->ordered_node_names());
}


void Game::on_init(plat::Platform& platform) {
    platform_ = &platform;
            assets_ = std::make_unique<AssetManager>();
            std::printf("reSF2 initialized.\n");
            std::printf("Controls (original SF2 layout):\n");
            std::printf("  W/A/S/D     - Up / Left / Down / Right (movement + attack direction)\n");
            std::printf("  O           - Punch (W=upper, S=low, D=heavy, A=spinning, S+A=elbow)\n");
            std::printf("  P           - Kick (S=sweep, D=front, A=back, S+D=dodge reverse)\n");
            std::printf("  W           - Jump (W+D=front flip, W+A=back flip)\n");
            std::printf("  S+D / S+A   - Forward roll / Back roll\n");
            std::printf("  S (hold)    - Duck (crouch)\n");
            std::printf("  X           - Magic (cast when the magic bar is full)\n");
            std::printf("  Block       - AUTOMATIC (when idle, not attacking)\n");
            std::printf("  M or click menu - Toggle menu\n");
            std::printf("  T           - Toggle dialog\n");
            std::printf("  N           - New game (go to Map)\n");
            std::printf("  1/2/3       - Zoom presets\n");
            std::printf("  Esc         - Quit / close overlay / back\n\n");

            // Only create a GL renderer if no custom renderer was injected
            // via set_renderer() (e.g., software renderer for headless testing).
            if (!renderer_) {
                renderer_ = std::make_unique<ren::Renderer>();
                if (!renderer_->init(platform.window_width(), platform.window_height())) {
                    renderer_.reset(); return;
                }
            }
            renderer_->set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

            // Load loading screen textures (used by LoadingScene via render_loading_screen)
            if (!asset_root_.empty()) load_loading_screen();

            // [Wave 11C P1] PreFight (VS) screen assets (VS_Fon halves, VS
            // label, stripes) — shown by BattleScene before the fight.
            if (!asset_root_.empty()) assets_->load_vs_screen(asset_root_);

            // Initialize audio engine
            {
                auto& audio = aud::AudioEngine::instance();
    #if RESF2_ENABLE_AUDIO
                auto backend = std::make_unique<aud::AlAudioBackend>();
    #else
                auto backend = std::make_unique<aud::NullAudioBackend>();
    #endif
                audio.init(std::move(backend));
            }

            // Register all scenes with the SceneManager
            scene_manager_.register_scene(scene::SceneId::Boot,
                [] { return std::make_unique<scene::BootScene>(); });
            scene_manager_.register_scene(scene::SceneId::Loading,
                [] { return std::make_unique<scene::LoadingScene>(); });
            scene_manager_.register_scene(scene::SceneId::MainMenu,
                [] { return std::make_unique<scene::MainMenuScene>(); });
            scene_manager_.register_scene(scene::SceneId::Map,
                [] { return std::make_unique<scene::MapScene>(); });
            scene_manager_.register_scene(scene::SceneId::Shop,
                [] { return std::make_unique<scene::ShopScene>(); });
            scene_manager_.register_scene(scene::SceneId::Settings,
                [] { return std::make_unique<scene::SettingsScene>(); });
            scene_manager_.register_scene(scene::SceneId::Dialogue,
                [] { return std::make_unique<scene::DialogueScene>(); });
            scene_manager_.register_scene(scene::SceneId::Battle,
                [] { return std::make_unique<scene::BattleScene>(); });
            scene_manager_.register_scene(scene::SceneId::Results,
                [] { return std::make_unique<scene::ResultsScene>(); });
            scene_manager_.register_scene(scene::SceneId::Profile,
                [] { return std::make_unique<scene::ProfileScene>(); });

            // Initialize quest engine callbacks
            // [ORIGINAL] QuestManager @ 0x101c7d20 wires actions to game systems.
            quest_engine_.set_dialog_callback([this](const std::string& title,
                    const std::vector<std::pair<std::string, std::string>>& lines) {
                // [Wave 9B] S5: quest <Dialog> actions are STORY dialogues —
                // quests.xml places them on the Map (Place="Map"), so they
                // play over the Map on its next entry and return to it. The
                // line text is a localization KEY (tutorial_girl_hello,
                // tutorial_bodyguard_win_1, ...); the Dialogue scene
                // localizes it on render.
                std::printf("[QUEST] dialog: title='%s' lines=%zu\n", title.c_str(), lines.size());
                if (!lines.empty())
                    host_queue_story_dialogue(lines, scene::SceneId::Map);
            });
            quest_engine_.set_zone_callback([this](const std::string& zone) {
                std::printf("[QUEST] zone open: '%s'\n", zone.c_str());
                zone_unlocked_[zone] = true;
            });
            quest_engine_.set_battle_callback([this](const std::string& battle, bool locked) {
                std::printf("[QUEST] battle '%s' locked=%d\n", battle.c_str(), locked);
                battle_unlocked_[battle] = !locked;
            });
            quest_engine_.set_currency_callback([this](int amount) {
                std::printf("[QUEST] currency +%d\n", amount);
                host_add_currency(amount);
            });
            quest_engine_.set_item_callback([this](const std::string& item) {
                std::printf("[QUEST] item '%s'\n", item.c_str());
                inventory_.add_item(item);
                player_profile_.add_item(item);
            });
            quest_engine_.set_variable_callback([this](const std::string& name, const std::string& value) {
                std::printf("[QUEST] var '%s' = '%s'\n", name.c_str(), value.c_str());
            });
            std::printf("[QUEST] engine initialized\n");

            // [Wave 8] Boot config set — perks/forge/CharacterProgress/
            // Achievements parse BEFORE the save, matching the original
            // (LIVE_BOOT_TRACE 11.26-12.55 s). They are not consumed by the
            // game logic yet, but must parse without error and be available.
            if (resf2::game::load_boot_configs(asset_root_, boot_configs_)) {
                std::printf("[boot] configs loaded: %zu files\n", boot_configs_.events.size());
            }
            for (const auto& ev : boot_configs_.events) boot_events_.push_back(ev);

            // [Wave 8] moves.xml parses BEFORE the save load, matching the
            // original (12.56 s vs 15.82 s). load_moves() is one-shot; the
            // init_location() call later becomes a no-op.
            load_moves();

            // Load saved progress (gold, wins, levels, inventory).
            //
            // A scripted run is a MEASUREMENT and must not depend on whatever
            // is in the developer's %APPDATA%\reSF2\save.json — same reasoning
            // as the fixed timestep and the ignored pause in PORT_PLAN 9.8.
            // This was not academic: test_save_system and test_inventory write
            // a real save to that user-global path, and it equips
            // WEAPON_KNIVES. Every later scripted run then rejected every
            // Fists move ("cand=0 reject=no_candidate"), so the punch simply
            // never happened. test_input_trace is CTest #1, so on a machine
            // that had never run the suite it passed — and then failed for
            // good afterwards. The suite was order- and history-dependent.
            if (!hermetic_run_) {
                host_load_progress();
            } else {
                std::printf("[save] skipped (scripted run: state comes from the "
                            "script, not from the machine)\n");
            }

            // [Wave 8] list.xml loads AFTER the save, matching the original
            // (15.84 s vs save 15.82 s). The item catalog feeds the Shop.
            {
                // [U2] The fallback path used to sit INSIDE the primary
                // path's existence check, so when "assets/assets/list.xml"
                // was absent (this repo ships only "assets/list.xml") the
                // fallback never ran and the shop got an EMPTY catalog:
                // no items, no BUY button — "shop doesn't work". Try each
                // candidate independently.
                fmt::ListParser lp;
                std::vector<std::filesystem::path> list_candidates = {
                    std::filesystem::path(asset_root_) / "assets" / "list.xml",
                    std::filesystem::path(asset_root_) / "list.xml",
                    std::filesystem::path(asset_root_) / "assets" / "files" / "assets" / "list.xml",
                };
                for (const auto& candidate : list_candidates) {
                    if (list_data_loaded_ || !std::filesystem::exists(candidate)) continue;
                    if (lp.load_file(candidate.string(), list_data_)) {
                        list_data_loaded_ = true;
                        std::printf("[shop] loaded %zu items from %s\n",
                                    list_data_.items.size(), candidate.string().c_str());
                    }
                }
                // Initialize shop manager from loaded catalog
                if (list_data_loaded_) {
                    shop_manager_.load_catalog(list_data_);
                }
                boot_events_.push_back("list.xml");
            }

            // Sync member variables from PlayerProfile (authoritative after load)
            currency_ = player_profile_.currency();
            player_wins_ = player_profile_.wins();
            player_losses_ = player_profile_.losses();

            // [ORIGINAL] Check if the initial Sensei tutorial should fire.
            // Driven by Tutorial="MOVE" in usersDefault.xml on first start.
            if (!hermetic_run_) {
                check_tutorial();
            }
            // [Wave 10A defect 3] E2E hook: --tutorial-start also starts the
            // Sensei flow in a scripted run, so the script can walk the
            // dialog -> bag -> Kenji fight chain deterministically.
            if (tutorial_start_flag_) check_tutorial();

            // [Wave 10A defect 6] E2E hook: --equip-magic forces a magic
            // item into the hermetic inventory and equips it, so the fight
            // HUD's magic button path is exercisable. Runs after the
            // catalog load so the item resolves against list.xml.
            if (!equip_magic_hook_.empty()) {
                inventory_.add_item(equip_magic_hook_);
                inventory_.equip(inventory::kSlotMagic, equip_magic_hook_);
                sync_equipped_weapon();
                std::printf("[equip] E2E hook: magic '%s' equipped\n",
                            equip_magic_hook_.c_str());
            }

            // [Wave 10B soak D7] E2E hook: --equip-weapon forces a weapon
            // into the hermetic inventory and equips it, so the weapon-move
            // mapping is exercisable against the real binary (same pattern
            // as --equip-magic). sync_equipped_weapon resolves the item id
            // to its list.xml SubType (WEAPON_KNIVES -> Knives), which is
            // what the moves.xml TacticWeapon sets are keyed on.
            if (!equip_weapon_hook_.empty()) {
                inventory_.add_item(equip_weapon_hook_);
                inventory_.equip(inventory::kSlotWeapon, equip_weapon_hook_);
                sync_equipped_weapon();
                std::printf("[equip] E2E hook: weapon '%s' equipped\n",
                            equip_weapon_hook_.c_str());
            }

            // [Wave 11C P3] E2E hooks: --tutorial-state <state> parks the
            // tutorial state machine at a given step (SHOP_TRIP etc. - the
            // hermetic profile always saves COMPLETE), and --add-gold <N>
            // tops up the hermetic wallet (the tutorial shop trip must be
            // able to afford the knives; usersDefault.xml ships Money=0).
            if (!tutorial_state_hook_.empty()) {
                tutorial_state_ = tutorial_state_hook_;
                std::printf("[tutorial] E2E hook: state -> %s\n",
                            tutorial_state_.c_str());
            }
            if (add_gold_hook_ > 0) {
                currency_ += add_gold_hook_;
                std::printf("[gold] E2E hook: +%d (total %d)\n",
                            add_gold_hook_, currency_);
            }

            // Start the scene flow at Boot, unless --scene asked for a
            // specific screen. Jumping straight to a screen is what makes it
            // possible to capture and compare it against the reference
            // screenshots without driving the whole menu flow by hand.
            scene::SceneContext ctx{*this, platform, *renderer_, 0};
            scene::SceneId first = scene::SceneId::Boot;
            if (!start_scene_.empty()) {
                static const std::pair<const char*, scene::SceneId> kNames[] = {
                    {"boot", scene::SceneId::Boot},
                    {"loading", scene::SceneId::Loading},
                    {"dojo", scene::SceneId::MainMenu},
                    {"menu", scene::SceneId::MainMenu},
                    {"map", scene::SceneId::Map},
                    {"shop", scene::SceneId::Shop},
                    {"settings", scene::SceneId::Settings},
                    {"dialogue", scene::SceneId::Dialogue},
                    {"battle", scene::SceneId::Battle},
                    {"results", scene::SceneId::Results},
                    {"profile", scene::SceneId::Profile},
                };
                // "map:3" opens the map on zone 3 — capturing a specific
                // zone is how the layout gets compared against the reference.
                std::string want = start_scene_;
                const auto colon = want.find(':');
                if (colon != std::string::npos) {
                    start_scene_arg_ = std::atoi(want.c_str() + colon + 1);
                    want = want.substr(0, colon);
                }
                bool found = false;
                for (const auto& [name, id] : kNames) {
                    if (want == name) { first = id; found = true; break; }
                }
                if (!found)
                    std::fprintf(stderr, "--scene: unknown screen '%s'\n",
                                 start_scene_.c_str());
                else if (first != scene::SceneId::Boot)
                    host_load_location();   // screens past Boot expect assets
            }
            scene_manager_.start(first, ctx);
}

void Game::on_update(plat::Platform& platform, uint32_t dt) {
    if (!renderer_) return;

            // Build the scene context and delegate to the SceneManager.
            // The current scene's on_update handles all input and game logic.
            // For MainMenu/Battle, the scene calls host_update_gameplay() which
            // contains the movement, combat, and animation code.
            scene::SceneContext ctx{*this, platform, *renderer_, dt};
            scene_manager_.update(ctx);
}

void Game::on_render(plat::Platform& platform) {
    if (!renderer_) return;
            renderer_->begin_frame();
            scene::SceneContext ctx{*this, platform, *renderer_, 0};
            scene_manager_.render(ctx);
            // [Wave 10A defect 4] [PIXEL] probe: the top-left corner pixel as
            // actually drawn this frame (screen coords, Y-down). Lets a
            // scripted run assert what a scene really painted — e.g. the
            // dialogue must keep the location visible behind the parchment
            // (corner pixel ≈ the dojo's pre-dialogue corner), instead of
            // regressing to a flat dark fill.
            // [Wave 10A defect 4] [PIXEL] probe: two readbacks in screen
            // coords (Y-down) — p00 = the top-left corner (must stay the
            // location's pixel when a dialogue opens, not regress to the
            // flat clear), p11 = the screen centre (the dialogue parchment
            // paint 226,205,163 identifies dialogue frames).
            if (dump_state_) {
                std::uint8_t rgb[3] = {0, 0, 0};
                if (renderer_->read_pixel(2, 2, rgb)) {
                    std::printf("[PIXEL] f=%llu p00=%d,%d,%d\n",
                                (unsigned long long)total_frame_count_,
                                (int)rgb[0], (int)rgb[1], (int)rgb[2]);
                }
                if (renderer_->read_pixel(640, 360, rgb)) {
                    std::printf("[PIXEL] f=%llu p11=%d,%d,%d\n",
                                (unsigned long long)total_frame_count_,
                                (int)rgb[0], (int)rgb[1], (int)rgb[2]);
                }
            }
            renderer_->end_frame();
}

void Game::on_shutdown(plat::Platform&) {
    aud::AudioEngine::instance().shutdown();
            if (renderer_) renderer_->shutdown();
}

void Game::request_scene_transition(scene::SceneId to) {
    // In replay mode, skip the menu flow: Loading → direct to Battle
            if (replay_mode_ && to == scene::SceneId::MainMenu) {
                to = scene::SceneId::Battle;
                std::printf("[REPLAY] Skipping menus, entering Battle directly\n");
            }
            scene_manager_.transition_to(to);
}

void Game::host_load_location() {
    if (!location_loaded_) {
                init_location();
            }
}

void Game::host_reset_menu_state() {
    // [ORIGINAL] Entering the dojo clears whatever overlay a previous scene
    // left open. The tutorial hint scroll appears when there's an active
    // tutorial action the player needs to perform.
    if (intro_hint_dismissed_ || tutorial_state_ == "COMPLETE") {
        overlay_ = Overlay::None;
    } else if (tutorial_dialog_shown_ || tutorial_state_ != "MOVE") {
        // Show the hint scroll when:
        // - The intro dialog has played (tutorial_dialog_shown_), OR
        // - We're past the MOVE stage (loaded save with BAG/FIRST_FIGHT)
        //   and the intro dialog was already seen in a previous session.
        overlay_ = Overlay::Dialog;
        dialog_overlay_anim_ = 0.0f;  // restart unroll animation
        overlay_show_frames_ = 0;     // reset grace period
        hint_start_player_x_ = player_pos_x_;  // track start position
        tutorial_move_steps_ = 0;     // [Q1] count steps from hint appearance
    } else {
        // Dialog hasn't played yet — no hint scroll.
        overlay_ = Overlay::None;
    }
    // [FIX] Don't reset menu_anim_progress_ if the menu is still open.
    // Resetting it to 0 caused the menu scroll to disappear mid-transition
    // when a scene was entered with the menu already visible.
    if (overlay_ != Overlay::Menu) {
        menu_anim_progress_ = 0.0f;
    }
}

void Game::host_toggle_menu_overlay() {
    // [FIX] Toggling the menu overlay from scenes that do not call
    // host_update_gameplay (Map, Results, Profile, Settings, Shop).
    // Uses the same toggle logic as the M key handler in host_update_gameplay
    // (game.cpp ~1800). The animation progress drives the open/close tween.
    if (overlay_ == Overlay::Menu) {
        overlay_ = Overlay::None;
    } else {
        overlay_ = Overlay::Menu;
        if (menu_anim_progress_ < 0.001f) {
            // Start fresh open animation only if fully closed.
            menu_anim_progress_ = 0.0f;
        }
    }
}

void Game::host_close_menu_overlay() {
    // [U5] Called when the dojo's menu navigates to a submenu: the overlay
    // must close so the menu panel collapses instead of staying on top of
    // Shop/Map/Profile/Settings.
    if (overlay_ == Overlay::Menu) overlay_ = Overlay::None;
}

void Game::host_render_menu_overlay() {
    // [FIX] Advance the menu open/close animation even when host_update_gameplay
    // is not called (Map, Results, etc.). This keeps the scroll unroll/collapse
    // smooth on those scenes too.
    if (menu_anim_progress_ <= 0.001f && overlay_ != Overlay::Menu) return;

    const float dt_ms = static_cast<float>(last_frame_dt_ms_);
    const float target_progress = (overlay_ == Overlay::Menu) ? 1.0f : 0.0f;
    // [U6] The unfold must take kMenuAnimMs of real time. This used to divide
    // by (1000/300) = 3.33 — i.e. 16ms/3.33 = 4.8 progress per frame — so the
    // menu snapped open in ONE frame and the scroll unfold was never seen.
    // Progress advances by dt_ms / 300: 1.0 after 300ms.
    const float anim_speed = 300.0f;  // ms for a full open/close
    if (menu_anim_progress_ < target_progress) {
        menu_anim_progress_ += (float)dt_ms / anim_speed;
        if (menu_anim_progress_ > target_progress) menu_anim_progress_ = target_progress;
    } else if (menu_anim_progress_ > target_progress) {
        menu_anim_progress_ -= (float)dt_ms / anim_speed;
        if (menu_anim_progress_ < target_progress) menu_anim_progress_ = target_progress;
    }

    // Render the scroll menu when there's something visible.
    if (menu_anim_progress_ > 0.01f) {
        render_menu_expanded(*platform_);
    }
}


void Game::host_load_battle_location(const std::string& location) {
    // [D2] Do not reload a location that is already loaded and current: the
    // soak showed the dojo fully reloading after every dialogue exit (the
    // reload even re-printed the whole localization table). Only a location
    // SWAP — a battle swapping the dojo out for a real stage — must force a
    // fresh load. Re-entering the same location keeps its state intact.
    const std::string target = location.empty() ? "dojo" : location;
    if (location_loaded_ && current_location_name_ == target) return;
    // Clear old location atlases so new location's atlases are loaded fresh.
            // Without this, atlases with the same name (e.g. "bg", "atlas_layer1")
            // from the dojo would be reused, showing the wrong background images.
            assets_->atlases().clear();
            current_location_name_ = target;
            location_loaded_ = false;
            init_location();
}

bool Game::host_location_loaded() const noexcept {
    return location_loaded_;
}

bool Game::host_save_progress() {
    // Sync PlayerProfile from current member state
    save::SaveData data;
    data.version = 1;
    data.currency = currency_;
    data.level = 1 + player_wins_ / 5;
    data.wins = player_wins_;
    data.losses = player_losses_;
    data.current_level = current_level_;
    data.completed_levels = completed_levels_;
    // Save inventory state
    inventory_.to_save(data);

    // [ORIGINAL] Save tutorial and zone/battle lock state
    data.tutorial_state = tutorial_state_;
    data.zone_unlocked = zone_unlocked_;
    data.battle_unlocked = battle_unlocked_;

    // Keep the player_profile_ in sync too
    player_profile_ = player::PlayerProfile::from_save_data(data);
    return save_manager_.save(data);
}

bool Game::host_load_progress() {
    boot_events_.push_back("save");  // [Wave 8] boot-order probe
    save::SaveData data;
    if (!save_manager_.load(data)) return false;
    // Populate member variables from loaded data
    currency_ = data.currency;
    player_wins_ = data.wins;
    player_losses_ = data.losses;
    current_level_ = data.current_level;
    completed_levels_ = data.completed_levels;
    // Build PlayerProfile from save data
    player_profile_ = player::PlayerProfile::from_save_data(data);
    // Restore inventory from saved data
    inventory_.from_save(data);
    // Sync combat equipped weapon from inventory
    sync_equipped_weapon();
    // Rebuild fighter attributes from the restored equipment
    rebuild_fighter_attributes();

    // [ORIGINAL] Restore tutorial and zone/battle lock state from XML save
    tutorial_state_ = data.tutorial_state;
    if (tutorial_state_.empty()) tutorial_state_ = "MOVE";
    zone_unlocked_ = data.zone_unlocked;
    battle_unlocked_ = data.battle_unlocked;

    // [S1] <Warrior Voice=> from user.xml / usersDefault.xml selects the
    // player's m_pl_*/f_pl_* sound set ("Male" is the shipped default).
    if (!data.voice.empty()) player_voice_ = data.voice;

    std::printf("[save] loaded %zu completed levels, %d gold, %dw %dl, %zu items, tutorial=%s, %zu zones\n",
                completed_levels_.size(), currency_, player_wins_, player_losses_,
                data.owned_items.size(), tutorial_state_.c_str(), zone_unlocked_.size());

    // [H09] HUD values come from the LOADED SAVE (users.xml Level= / Money=)
    // — the old 1 + wins/5 heuristic and the constants 7/72450/9 were
    // invented (HARDCODE_AUDIT H09). Gems: users.xml ships no ruby
    // attribute (Money/PaidMoney/Bonus only, verified in
    // reverse/data/users.xml) — [HEURISTIC-TODO] stays 0 until a device
    // pull pins the original ruby source.
    hud_level_ = data.level;
    hud_gold_ = currency_;
    hud_gems_ = 0;

    return true;
}

void Game::host_set_dialogue(std::vector<std::pair<std::string, std::string>> lines) {
    dialogue_lines_ = std::move(lines);
            dialogue_index_ = 0;
    dialogue_choices_.clear();
    // [Wave 9B] S5: a plain (non-story) dialogue has no queued return scene —
    // its advance falls back to the battle-location rule. The reset keeps a
    // story dialogue's return (the Map) from leaking into a later pre-battle
    // dialogue (e.g. the Map's Sly intro would return to the Map instead of
    // starting the fight).
    dialogue_return_ = scene::SceneId::None;
}

const std::vector<std::pair<std::string, std::string>>& Game::host_get_dialogue() const {
    return dialogue_lines_;
}

std::vector<std::string> Game::host_get_dialogue_choices() const {
    return dialogue_choices_;
}

void Game::host_set_dialogue_choices(std::vector<std::string> choices) {
    dialogue_choices_ = std::move(choices);
}

// [Wave 9B] Story-dialogue queue (quests.xml <Dialog> sets). Unlike the
// pre-battle dialogue, a queued story dialogue has an explicit return scene
// and is NOT followed by a battle — the stale battle_location_ (set for the
// fight that just ended) must not redirect its last-line transition.
void Game::host_queue_story_dialogue(
    std::vector<std::pair<std::string, std::string>> lines,
    scene::SceneId return_to) {
    dialogue_lines_ = std::move(lines);
    dialogue_index_ = 0;
    dialogue_choices_.clear();
    dialogue_return_ = return_to;
    battle_location_.clear();
    story_dialogue_pending_ = true;
    std::printf("[QUEST] story dialogue queued (%zu lines, return to %s)\n",
                dialogue_lines_.size(), scene::scene_name(return_to));
}

bool Game::host_consume_story_dialogue() {
    const bool p = story_dialogue_pending_;
    story_dialogue_pending_ = false;
    return p;
}

scene::SceneId Game::host_get_dialogue_return() const {
    return dialogue_return_;
}

// [Wave 9B] Shop probes (re-soak-5). Returns what the shop's centre column
// renders right now — the scene owns the list state.
std::vector<std::string> Game::host_shop_visible_rows() {
    auto* shop = dynamic_cast<scene::ShopScene*>(scene_manager_.current());
    if (!shop) return {};
    scene::SceneContext ctx{*this, *platform_, *renderer_};
    return shop->visible_row_names(ctx);
}

std::string Game::host_shop_selected_item() {
    auto* shop = dynamic_cast<scene::ShopScene*>(scene_manager_.current());
    if (!shop) return {};
    scene::SceneContext ctx{*this, *platform_, *renderer_};
    return shop->selected_item_name(ctx);
}

void Game::host_set_current_level(std::string level_id) {
    current_level_ = level_id;
            player_profile_.set_current_level(level_id);
}

void Game::host_add_completed_level(const std::string& level) {
    for (const auto& l : completed_levels_) {
                if (l == level) return;  // already completed
            }
            completed_levels_.push_back(level);
            player_profile_.complete_level(level);
            host_save_progress();
            std::printf("[progress] completed: %s\n", level.c_str());
}

bool Game::host_is_level_completed(const std::string& level) const {
    for (const auto& l : completed_levels_) {
                if (l == level) return true;
            }
            return false;
}

bool Game::host_is_zone_unlocked(const std::string& zone) const {
    // [ORIGINAL] Check zone lock state from usersDefault.xml <Battles>.
    // If we have explicit state, use it; otherwise default to zone 1 unlocked.
    auto it = zone_unlocked_.find(zone);
    if (it != zone_unlocked_.end()) return it->second;
    // Default: only ZONE_1 is unlocked on a fresh start
    return (zone == "ZONE_1");
}

bool Game::host_is_battle_locked(const std::string& zone, const std::string& battle) const {
    // [ORIGINAL] Check battle lock state from usersDefault.xml <Battles>.
    std::string key = zone + "|" + battle;
    auto it = battle_unlocked_.find(key);
    if (it != battle_unlocked_.end()) return !it->second;
    // Default: not locked (if no explicit state)
    return false;
}

std::string Game::host_get_tutorial_state() const {
    return tutorial_state_;
}

void Game::check_tutorial() {
    // [ORIGINAL] The initial Sensei tutorial sequence, driven by the Tutorial
    // attribute on <Warrior> in usersDefault.xml.
    // Sequence: MOVE → BAG → FIRST_FIGHT → COMPLETE
    // Binary ref: tutorial state machine at 0x1027d6c0
    if (tutorial_state_ == "COMPLETE") return;  // tutorial already finished

    // [ORIGINAL] Speaker name from localization key "characterSensei" = "SENSEI"
    // Use the LATIN key "Sensei" as the speaker identifier stored in dialogue
    // lines. The Dialogue scene localizes it for display via host_localized(),
    // and derives the avatar texture name from it ("character_sensei"). Using
    // the localized value (e.g. Cyrillic "Сэнсей") would break avatar lookup.
    const std::string speaker = "Sensei";

    if (tutorial_state_ == "MOVE") {
        // [ORIGINAL] Sensei intro dialog using localization keys from eng.xml:
        // tutorial_begin_1, tutorial_begin_2, tutorial_move
        // Binary ref: 0x1027c910 ("MOVE" string ref), 0x1027d270 (state transition)
        std::string line1 = localized("tutorial_begin_1");
        std::string line2 = localized("tutorial_begin_2");
        std::string line3 = localized("tutorial_move");
        // Fallback to original text if localization not loaded
        if (line1.empty()) line1 = "Well, well... my vain disciple has returned. And without a body it seems. How unfortunate for you.";
        if (line2.empty()) line2 = "You're nothing more than a shadow now. And yet, I sense great power within you. I wonder...";
        if (line3.empty()) line3 = "Let me see you move! Show me what a shadow can do.";
        host_set_dialogue({
            {speaker, line1},
            {speaker, line2},
            {speaker, line3},
        });
        // [ORIGINAL] The dialog auto-triggers when entering the dojo for the
        // first time. Set pending flag so host_update_gameplay transitions to
        // the Dialogue scene on the first MainMenu frame.
        tutorial_dialog_pending_ = true;
        tutorial_state_ = "BAG";
        std::printf("[tutorial] Sensei intro dialog set (localized), state -> BAG, pending=true\n");
    } else if (tutorial_state_ == "BAG") {
        // [ORIGINAL] After player hits the punching bag, Sensei comments.
        // Binary ref: tutorial_punchbag at 0x105f1a98
        // The punchbag dialog is queued here and triggered after the movement
        // hint scroll is dismissed (the player moves, then gets the next hint).
        std::string line = localized("tutorial_punchbag");
        if (line.empty()) line = "Fascinating... Now, see that punching bag? Attack it!";
        host_set_dialogue({{speaker, line}});
        // If the intro dialog was never shown this session (loaded save at BAG
        // stage), show the punchbag dialog now so the player knows what to do.
        if (!tutorial_dialog_shown_) {
            tutorial_dialog_pending_ = true;
            tutorial_dialog_shown_ = true;
        }
        std::printf("[tutorial] Punchbag dialog queued (localized), pending=%d\n",
                    (int)tutorial_dialog_pending_);
        // Transition to FIRST_FIGHT happens when bag is hit (checked in hit detection)
    } else if (tutorial_state_ == "FIRST_FIGHT") {
        // [ORIGINAL] After defeating the bag, Sensei introduces the first real fight.
        // Binary ref: tutorial_training_fight at 0x10386448
        std::string line = localized("tutorial_training_fight");
        if (line.empty()) line = "Impressive... but a bag cannot defend itself. Let's see how you fare against my disciple, Kenji.";
        host_set_dialogue({{speaker, line}});
        tutorial_dialog_pending_ = true;
        tutorial_dialog_shown_ = true;
        // [P6] The state stays FIRST_FIGHT until the training fight is WON:
        // the original only advances the tutorial after the victory. Marking
        // COMPLETE here — before the fight even started — is what broke the
        // story after a loss: the player returned to the dojo with the
        // tutorial finished and nothing left to advance to (the soak log
        // showed the bag/fighter toggle cycling with no progression). The
        // Results scene advances FIRST_FIGHT -> COMPLETE on victory and
        // keeps it on defeat so the fight stays retryable.
        std::printf("[tutorial] Training fight dialog set (localized), state stays FIRST_FIGHT until won\n");
    }
}

std::string Game::host_get_battle_result() const {
    return battle_result_;
}

void Game::host_finish_tutorial_fight() {
    // [ORIGINAL] Winning the Kenji (FIRST_FIGHT) training fight advances the
    // tutorial to the SHOP TRIP (SPEC_PRESENTATION Q3.6: post-boss states
    // 0xA-0xE; state 0xB = shop trip with hints on the shop item cards,
    // key tutorial_buy_knives; tutorial_return_map hint after the trip).
    // The Sensei "find yourself a weapon" dialogue (key tutorial_shop, absent
    // from quests.xml - queued here, not by the quest engine) plays over the
    // shop and returns INTO the shop: the player is sent to the shop to buy
    // the knives. The old code jumped straight to COMPLETE - the "send me to
    // the shop" step never happened.
    tutorial_state_ = "SHOP_TRIP";
    std::string line = localized("tutorial_shop");
    if (line.empty())
        line = "I knew you could do it. Now you need to find yourself a weapon.";
    host_queue_story_dialogue({{"Sensei", line}}, scene::SceneId::Shop);
    std::printf("[tutorial] FIRST_FIGHT won -> SHOP_TRIP, tutorial_shop "
                "dialogue queued (shop trip)\n");
}

namespace {

// [Wave 9B] Evaluate one quest condition with the FightEnd event binding.
// Literal Equal tests (with Not="1") evaluate; anything else — operators
// we do not implement, or operands that are quest-query expressions
// ("?Fight(...).WinCount", "?Item(...).Quantity", ...) — FAILS CLOSED so a
// half-understood quest never fires user-visible actions spuriously.
bool quest_condition_holds(const resf2::game::QuestCondition& c,
                           const std::string& fight,
                           const std::string& fight_result) {
    if (c.op != "Equal") return false;  // unsupported operator -> fail closed
    auto resolve = [&](const std::string& v) -> std::string {
        if (v == "_$Fight") return fight;
        if (v == "_$FightResult") return fight_result;
        return v;
    };
    const std::string a = resolve(c.v1);
    const std::string b = resolve(c.v2);
    if (a.rfind('?', 0) == 0 || b.rfind('?', 0) == 0) return false;  // quest query -> fail closed
    const bool eq = (a == b);
    return c.negate ? !eq : eq;
}

}  // namespace

void Game::queue_tutorial_battle() {
    // [Q3] The training-fight dialog ("...against my disciple, Kenji") must
    // hand over to the disciple fight instead of dropping back to the dojo:
    // stages.xml Zone=Punchbag Battle=Training Fight=2 (warrior
    // Template=Dojo_Disciple, Location=dojo). Setting the battle location
    // makes the Dialogue scene's advance transition to Battle.
    // [HEURISTIC-TODO] The enemy fighter uses the placeholder enemy model —
    // BattleInfo.enemy_name is not yet wired into the enemy fighter setup —
    // and the fight data defaults below stand in when stages.xml is absent.
    scene::SceneHost::BattleInfo info;
    info.rounds = 1;
    info.round_time_s = 99;
    info.enemy_name = "Dojo_Disciple";
    if (assets_->stages_loaded()) {
        const auto& sd = assets_->stage_data();
        for (const auto& z : sd.zones) {
            if (z.name != "Punchbag") continue;
            for (const auto& b : z.battles) {
                if (b.name != "Training") continue;
                if (b.fights.size() >= 2) {
                    const auto& f = b.fights[1];  // Fight 2: the disciple
                    info.rounds = std::max(1, f.rounds);
                    info.round_time_s = std::max(1, f.round_time);
                    info.reward_gold = f.reward.money;
                    info.reward_xp = f.reward.exp;
                    if (!f.warriors.empty()) {
                        const auto& w0 = f.warriors.front();
                        info.enemy_name = !w0.first_name.empty()
                                              ? w0.first_name
                                              : w0.template_name;
                    }
                }
                break;
            }
            break;
        }
    }
    host_set_battle_info(info);
    host_set_battle_location("dojo");
    std::printf("[tutorial] Kenji fight queued (enemy='%s' rounds=%d) "
                "-> Battle after dialog\n",
                info.enemy_name.c_str(), info.rounds);
}

const resf2::format::StageData* Game::host_get_stages() const {
    return assets_->stages_loaded() ? &assets_->stage_data() : nullptr;
}

void Game::host_set_battle_location(std::string loc) {
    battle_location_ = std::move(loc);
}

std::string Game::host_get_battle_location() const {
    return battle_location_;
}

void Game::host_set_battle_result(std::string result) {
    battle_result_ = std::move(result);
}

void Game::host_trigger_quest_event(const std::string& event, const std::string& arg) {
    // [ORIGINAL] QuestManager @ 0x101c7d20 processes quest actions on events.
    // Events: "FightEnd" (arg=level), "SessionStart", "ZoneEnter" (arg=zone).
    // Every quest of quests.xml that lists the event and whose conditions
    // all hold fires its actions (quest_loader.cpp / quest_engine.hpp).
    // [HEURISTIC-TODO] Condition support covers literal Equal tests only;
    // quest-query conditions fail closed (see quest_condition_holds).
    // Lazy load: init_location loads the defs in the normal boot flow, but a
    // scripted session that jumps straight to a mid-game scene (or a fight
    // that ends before the loading screen) must still dispatch.
    if (!quest_defs_loaded_) {
        quest_defs_loaded_ = true;
        resf2::game::load_quest_defs(asset_root_, quest_defs_);
    }
    if (quest_defs_.empty()) return;
    const std::string fight_result = (battle_result_ == "victory") ? "Win" : "Loss";
    std::printf("[QUEST] event '%s' arg '%s' result='%s' (%zu quests loaded)\n",
                event.c_str(), arg.c_str(), fight_result.c_str(), quest_defs_.size());
    for (auto& quest : quest_defs_) {
        if (quest.fired) continue;
        bool has_event = false;
        for (const auto& ev : quest.events)
            if (ev == event) { has_event = true; break; }
        if (!has_event) continue;
        bool hold = true;
        for (const auto& c : quest.conditions)
            if (!quest_condition_holds(c, arg, fight_result)) { hold = false; break; }
        if (!hold) {
            std::printf("[QUEST] quest '%s' conditions not met (skipped)\n",
                        quest.name.c_str());
            continue;
        }
        // [ORIGINAL] Quests fire once per match; the engine re-arms them on
        // the next battle session start.
        quest.fired = true;
        std::printf("[QUEST] quest '%s' FIRES (actions=%zu)\n",
                    quest.name.c_str(), quest.actions.size());
        quest_engine_.execute_actions(quest.actions);
    }
}

void Game::host_set_battle_info(const BattleInfo& info) {
    battle_info_ = info;
    // [Wave 10A defect 3] E2E hook: --round-time forces the round clock
    // (stages.xml ships RoundTime=99 everywhere; a scripted run cannot wait
    // out a 99 s round). Applied here so every entry path (map, tutorial)
    // honours it.
    if (round_time_override_s_ > 0) battle_info_.round_time_s = round_time_override_s_;
    round_wins_player_ = 0;
    round_wins_enemy_ = 0;
    hp_trail_player_ = -1.0f;
    hp_trail_enemy_ = -1.0f;

    // [S2] The enemy's voice comes from stages.xml: the fight's warrior
    // resolves to a <Template> (by Name or FirstName), whose Voice attribute
    // ("Male"/"Female") selects the m_pl_*/f_pl_* sound set the enemy's
    // attack sounds come from. Default "Male" when unknown.
    enemy_voice_ = "Male";
    if (assets_ && assets_->stages_loaded()) {
        const auto& templates = assets_->stage_data().templates;
        const std::string& name = info.enemy_name;
        for (const auto& t : templates) {
            if (t.name == name || t.first_name == name) {
                if (!t.voice.empty()) enemy_voice_ = t.voice;
                break;
            }
        }
        // [P4] The HUD enemy name must come from the language pack: the
        // stages.xml <Template FirstName="NAME_X"> attribute is the
        // LOCALIZATION KEY for the fighter's display name (NAME_KENJI ->
        // "KENJI" in eng.xml / "КЕНДЗИ" in rus.xml). A fight handed over
        // with a raw warrior template ("Dojo_Disciple", "Lynx_Claws") must
        // resolve to that key, or the HUD shows the raw string. A warrior
        // FirstName of its own (e.g. "Punchbag") is kept as-is.
        if (!battle_info_.enemy_name.empty()) {
            const std::string& raw = battle_info_.enemy_name;
            for (const auto& t : templates) {
                if (t.name == raw && !t.first_name.empty()) {
                    battle_info_.enemy_name = t.first_name;
                    break;
                }
            }
        }
    }
    // [R2] Load the enemy fighter's OWN body/head model per the battle
    // setup (stages.xml template items -> list.xml Model attrs) — the
    // battle hit test targets its capsules, not the player's body model.
    load_enemy_fighter_models();
    std::printf("[battle] enemy voice '%s' (enemy='%s')\n",
                enemy_voice_.c_str(), info.enemy_name.c_str());
}

const scene::SceneHost::BattleInfo& Game::host_get_battle_info() const {
    return battle_info_;
}

std::string Game::host_round_outcome() const {
    // The sparring partner shares the enemy fighter state, so this is only
    // meaningful in battle mode — BattleScene is the sole caller.
    if (player_fighter_.is_dead) return "defeat";
    if (enemy_fighter_.is_dead) return "victory";
    return "";
}

float Game::host_player_health_frac() const {
    return player_fighter_.max_health > 0
               ? player_fighter_.health / player_fighter_.max_health
               : 0.0f;
}

float Game::host_enemy_health_frac() const {
    return enemy_fighter_.max_health > 0
               ? enemy_fighter_.health / enemy_fighter_.max_health
               : 0.0f;
}

void Game::host_reset_round() {
    // Same reset the R key performs after a knockout, plus the fighters walk
    // back to their params.xml marks — the original restarts each round from
    // the starting positions.
    player_fighter_ = FighterState{};
    enemy_fighter_ = FighterState{};
    // FighterState{} wipes the AttributeSets — rebuild them (player from
    // equipment, enemy from the AlignTargetAttributes baseline) so every
    // round starts with the model+0x1C4 maps populated. Idempotent.
    rebuild_fighter_attributes();
    // [ORIGINAL] Magic charge round start (Fight::init @ 0x8F41C8E4,
    // SPEC_COMBAT_CORE Q1.5): count=0, charge=clamp(InitialCharge,0,1).
    // InitialCharge is zero-fallback ([HEURISTIC-TODO] the 13-char name
    // lookup FUN_8f65eff8 has no engine attribute source yet), so the bar
    // always opens empty and fills from landed hits.
    player_fighter_.magic_charge = std::clamp(
        player_fighter_.attributes.get_or("InitialCharge", 0.0f), 0.0f, 1.0f);
    player_fighter_.magic_count = 0;
    enemy_fighter_.magic_charge = 0.0f;
    enemy_fighter_.magic_count = 0;
    player_hit_flash_ = 0;
    enemy_hit_flash_ = 0;
    combo_timer_ = 0;
    hit_sparks_.clear();
    enemy_attack_cooldown_ = 0;
    enemy_attacking_ = false;
    // Reset block decision state for new round (FUN_10171d80 cooldown)
    block_decision_cooldown_ = 0.0f;
    block_decision_pending_ = false;
    recent_damage_taken_ = 0.0f;
    enemy_hits_on_player_ = 0;
    hp_trail_player_ = -1.0f;
    hp_trail_enemy_ = -1.0f;
    // [STEP 4.7] Reset knockback/knockdown state
    gameplay_y_offset_ = 0.0f;
    y_velocity_ = 0.0f;
    is_knocked_down_ = false;
    knockdown_timer_ = 0;
    if (location_) {
        const float half_world_w = location_->width * 0.5f;
        player_pos_x_ = location_->player_x - half_world_w;
        player_pos_y_ = location_->player_y;
        enemy_pos_x_ = location_->enemy_x - half_world_w;
        enemy_pos_y_ = location_->enemy_y;
        enemy_facing_right_ = false;
    }
}

void Game::host_set_round_wins(int player, int enemy) {
    round_wins_player_ = player;
    round_wins_enemy_ = enemy;
}

void Game::host_set_round_left_ms(int ms) {
    round_left_ms_probe_ = ms;
}

int Game::host_get_currency() const {
    return currency_;
}

bool Game::host_spend_currency(int amount) {
    if (currency_ < amount) return false;
            currency_ -= amount;
            std::printf("[currency] spent %d, remaining %d\n", amount, currency_);
            return true;
}

void Game::host_add_currency(int amount) {
    currency_ += amount;
            std::printf("[currency] added %d, total %d\n", amount, currency_);
}

bool Game::host_has_item(const std::string& item_id) const {
    return inventory_.has_item(item_id);
}

std::vector<std::string> Game::host_get_owned_items() const {
    std::vector<std::string> result = inventory_.all_items();
            // Also include equipped items
            for (const auto& slot : inventory::kAllSlots) {
                std::string eq = inventory_.equipped(slot);
                if (!eq.empty()) result.push_back(eq);
            }
            return result;
}

std::string Game::host_get_equipped(const std::string& slot) const {
    return inventory_.equipped(slot);
}

void Game::host_add_item(const std::string& item_id) {
    // [P3] Test seam: put an item into the inventory without the shop gates
    // (gold, level, ShopHide). Mirrors host_buy_item's inventory/profile
    // writes. The wave-7a tests equip real items (ARMOR_ROBE, HEAD_KENJI)
    // that a hermetic dojo cannot buy — ARMOR_ROBE is level-gated and every
    // helm in list.xml is ShopHide.
    if (inventory_.has_item(item_id)) return;
    inventory_.add_item(item_id);
    player_profile_.add_item(item_id);
    // [H02] The J/U cycle follows OWNED weapons (R4b) — the seam mirrors
    // host_buy_item's writes, which sync the cycle on every inventory change.
    sync_equipped_weapon();
}

bool Game::host_buy_item(const std::string& item_id) {
    auto* item = shop_manager_.find_item(item_id);
            if (!item) {
                std::printf("[shop] cannot buy %s: not found in catalog\n", item_id.c_str());
                return false;
            }
            if (item->is_paid) {
                std::printf("[shop] cannot buy %s: IAP item\n", item_id.c_str());
                return false;
            }
            int level = host_get_player_level();
            if (!shop_manager_.can_buy(item_id, currency_, level)) {
                std::printf("[shop] cannot buy %s: gold=%d need=%d, level=%d need=%d\n",
                            item_id.c_str(), currency_, item->price, level, item->level_req);
                return false;
            }
            // Deduct gold
            currency_ -= item->price;
            std::printf("[shop] purchased %s for %d gold (remaining: %d)\n",
                        item_id.c_str(), item->price, currency_);
            // Add to inventory
            inventory_.add_item(item_id);
            // Sync PlayerProfile
            player_profile_.add_item(item_id);
            // [Wave 9B] S5: buying the knives during the tutorial queues the
            // Lynx challenge (tutorial_buy_knives + tutorial_map). Those keys
            // are hardcoded tutorial state machine lines (0x1027d6c0),
            // absent from quests.xml; the dialogue plays over the Map on its
            // next entry, once per save.
            if (item_id == "WEAPON_KNIVES" && tutorial_state_ == "COMPLETE" &&
                !tutorial_lynx_hint_shown_) {
                tutorial_lynx_hint_shown_ = true;
                std::string l1 = localized("tutorial_buy_knives");
                std::string l2 = localized("tutorial_map");
                if (l1.empty())
                    l1 = "In this form you can't die. Thus I suggest you challenge one of the demons now: Lynx!";
                if (l2.empty())
                    l2 = "I know where you can find him. Open your map.";
                host_queue_story_dialogue({{"Sensei", l1}, {"Sensei", l2}},
                                          scene::SceneId::Map);
                std::printf("[tutorial] knives bought -> Lynx challenge dialogue queued\n");
            }
            // [Wave 11C P3] Buying the knives during the SHOP TRIP finishes
            // the trip: the tutorial state machine moves past state 0xB and
            // shows the tutorial_return_map hint leading back to the map
            // (SPEC_PRESENTATION Q3.6; the key set @ 0x8F79C468..C750).
            if (item_id == "WEAPON_KNIVES" && tutorial_state_ == "SHOP_TRIP") {
                tutorial_state_ = "RETURN_MAP";
                std::string l1 = localized("tutorial_return_map");
                if (l1.empty())
                    l1 = "Well done. Now return to the map - there is much to do.";
                host_queue_story_dialogue({{"Sensei", l1}},
                                          scene::SceneId::Map);
                std::printf("[tutorial] knives bought during SHOP_TRIP -> "
                            "RETURN_MAP, tutorial_return_map queued\n");
            }
            // Auto-save
            host_save_progress();
            return true;
}

bool Game::host_sell_item(const std::string& item_id) {
    if (!inventory_.has_item(item_id)) {
                std::printf("[shop] cannot sell %s: not owned\n", item_id.c_str());
                return false;
            }
            int price = shop_manager_.sell_price(item_id);
            if (price <= 0) {
                std::printf("[shop] cannot sell %s: no sell value\n", item_id.c_str());
                return false;
            }
            // Remove from inventory
            inventory_.remove_item(item_id);
            player_profile_.remove_item(item_id);
            // Add gold
            currency_ += price;
            std::printf("[shop] sold %s for %d gold (total: %d)\n",
                        item_id.c_str(), price, currency_);
            // Auto-save
            host_save_progress();
            return true;
}

bool Game::host_equip_item(const std::string& item_id) {
    if (!inventory_.has_item(item_id)) {
                std::printf("[equip] cannot equip %s: not owned\n", item_id.c_str());
                return false;
            }
            // Determine slot from item type in catalog
            auto* item = shop_manager_.find_item(item_id);
            if (!item) {
                // Fallback: check list_data_ directly
                for (const auto& li : list_data_.items) {
                    if (li.name == item_id) {
                        const char* slot = shop::item_type_to_slot(li.type);
                        if (!slot) {
                            std::printf("[equip] unknown item type '%s' for %s\n",
                                        li.type.c_str(), item_id.c_str());
                            return false;
                        }
                        inventory_.equip(slot, item_id);
                        player_profile_.equip_item(slot, item_id);
                        // [P3] Armor/helm equip must be visible immediately
                        // (hidden items take this fallback path).
                        if (location_loaded_ &&
                            (std::string(slot) == inventory::kSlotArmor ||
                             std::string(slot) == inventory::kSlotHelmet)) {
                            load_equipment_models();
                        }
                        rebuild_fighter_attributes();
                        std::printf("[equip] equipped %s in %s\n", item_id.c_str(), slot);
                        host_save_progress();
                        return true;
                    }
                }
                std::printf("[equip] cannot find item %s in catalog\n", item_id.c_str());
                return false;
            }
            const char* slot = shop::item_type_to_slot(item->category);
            if (!slot) {
                std::printf("[equip] unknown category '%s' for %s\n",
                            item->category.c_str(), item_id.c_str());
                return false;
            }
            inventory_.equip(slot, item_id);
            player_profile_.equip_item(slot, item_id);
            // Sync combat weapon if equipping a weapon slot
            if (slot == inventory::kSlotWeapon) {
                sync_equipped_weapon();
            }
            // [P3] Armor/helm equip must be visible immediately (the model
            // attach reloads when the location loads; this covers the
            // equip-in-menu case).
            if (location_loaded_ && (slot == inventory::kSlotArmor ||
                                     slot == inventory::kSlotHelmet)) {
                load_equipment_models();
            }
            rebuild_fighter_attributes();
            std::printf("[equip] equipped %s in %s\n", item_id.c_str(), slot);
            host_save_progress();
            return true;
}

bool Game::host_unequip_item(const std::string& slot) {
    if (!inventory_.equipped(slot).empty()) {
                std::string item_id = inventory_.equipped(slot);
                inventory_.unequip(slot);
                player_profile_.equip_item(slot, "");  // clear the slot
                // Sync combat weapon if unequipping weapon slot
                if (slot == inventory::kSlotWeapon) {
                    sync_equipped_weapon();
                }
                rebuild_fighter_attributes();
                std::printf("[equip] unequipped %s from %s\n", item_id.c_str(), slot.c_str());
                host_save_progress();
                return true;
            }
            return false;
}

int Game::host_get_player_level() const {
    return 1 + player_wins_ / 5;
}

int Game::host_get_wins() const {
    return player_wins_;
}

int Game::host_get_losses() const {
    return player_losses_;
}

const resf2::format::ListData* Game::host_get_list_data() const {
    return list_data_loaded_ ? &list_data_ : nullptr;
}

std::string Game::host_get_current_level() const {
    return current_level_;
}

void Game::host_add_win() {
    player_wins_++;
            player_profile_.add_win();
            std::printf("[stats] win recorded, total wins: %d\n", player_wins_);
}

void Game::host_add_loss() {
    player_losses_++;
            player_profile_.add_loss();
            std::printf("[stats] loss recorded, total losses: %d\n", player_losses_);
}

void Game::sync_equipped_weapon() {
    // [ORIGINAL] moves.xml identifies a weapon by its SUBTYPE, and
    // `TacticWeapon` is a '|'-separated set of them:
    //     TacticWeapon="Knives|Keris"
    //     TacticWeapon="Katana|NinjaSword|ShogunKatana"
    // list.xml gives each item both an id and that subtype:
    //     <Item Name="WEAPON_KNIVES" Type="Weapon" SubType="Knives" .../>
    //
    // The inventory stores the ID, and this used to hand the ID straight to
    // the move filter — so with a real weapon equipped, `equipped_weapon_` was
    // "WEAPON_KNIVES", which is in no TacticWeapon set anywhere, and EVERY
    // move was rejected. The player could not attack at all: the log showed
    // "cand=0 reject=no_candidate" on every press while the fighter stood
    // there with his fists, because the weapon MODEL had failed to load too
    // and left him looking unarmed.
    std::string inv_weapon = inventory_.equipped_weapon();
            if (!inv_weapon.empty()) {
                if (const auto* list = host_get_list_data()) {
                    for (const auto& item : list->items) {
                        if (item.name == inv_weapon && !item.subtype.empty()) {
                            inv_weapon = item.subtype;
                            break;
                        }
                    }
                }
            }
            if (!inv_weapon.empty() && inv_weapon != equipped_weapon_) {
                equipped_weapon_ = inv_weapon;
                // Try to load the weapon model if location is loaded
                if (location_loaded_) {
                    load_player_weapon(equipped_weapon_);
                }
                std::printf("[equip] combat weapon synced to: %s\n", equipped_weapon_.c_str());
            } else if (inv_weapon.empty() && equipped_weapon_ != "Fists") {
                // No weapon equipped → use fists
                equipped_weapon_ = "Fists";
                if (location_loaded_) {
                    load_player_weapon(equipped_weapon_);
                }
                std::printf("[equip] combat weapon reset to Fists\n");
            }

            // [Wave 10A defect 6] The ranged/magic fight BUTTONS were never
            // wired: the Game's equipped_ranged_/equipped_magic_ fields were
            // only READ by the touch-controls renderer and nothing ever
            // synced them from the inventory, so the magic button could not
            // appear even with a magic item equipped. Sync both slots here,
            // the same place the weapon comes from.
            equipped_ranged_ = inventory_.equipped_ranged();
            equipped_magic_ = inventory_.equipped_magic();

            // A weapon that matches no move at all leaves the player unable to
            // attack, which is never a legitimate game state — it means the
            // subtype mapping above failed for this item. Say so and fall back
            // to fists rather than handing the player a silent brick.
            if (assets_ && !assets_->moves().empty()) {
                int usable = 0;
                for (const auto& [n, mv] : assets_->moves()) {
                    (void)n;
                    if (mv.is_attack && is_weapon_allowed(mv)) ++usable;
                }
                if (usable == 0) {
                    std::fprintf(stderr,
                        "[equip] '%s' matches no move in moves.xml — falling back to "
                        "Fists. The inventory item's SubType is missing from "
                        "list.xml, or its name is not in any TacticWeapon set.\n",
                        equipped_weapon_.c_str());
                    equipped_weapon_ = "Fists";
                    if (location_loaded_) load_player_weapon(equipped_weapon_);
                }
            }
            // [H01/R4] The J/U cycle follows the save's OWNED weapons
            // (HARDCODE_AUDIT H01: the hardcoded tactic list cycled weapons
            // the player doesn't own — U handed out Machete on a knives
            // save). Runs on every equip/unequip/save-load sync.
            rebuild_weapon_cycle();
}

void Game::rebuild_fighter_attributes() {
    // [ORIGINAL] Every fighter's attributes live in the name-keyed int map at
    // model+0x1C4 that Model::getParameter (game+0x6275F4) reads and that
    // Model::getTotalDamage (game+0x4527B4) consumes. The player's map is
    // aggregated from equipped items; the placeholder enemy gets the
    // <AlignTargetAttributes> baseline ("normalise an opponent's attributes")
    // until stage warriors land (5.3). Perks contribute nothing here — the
    // phase-4 fork verdict is ZERO Case A (PERK_SURVEY.md /
    // PERK_BINARY_SURVEY.md); trigger-system effects are 5.1 scope.
    player_fighter_.attributes = aggregate_equipment_attributes(list_data_, inventory_);
    enemy_fighter_.attributes = seed_enemy_baseline_attributes();
    // [ORIGINAL] The hero's CRIT attributes come from the stages.xml hero
    // template (Template Name="Default" CriticalChance="1000" — the binary's
    // crit_chance getter @ 0x8F4A610C reads them from model+0x1C4, seeded
    // from the template attrs). Equipment contributes nothing for these
    // names, so seed the map here; CriticalDamage absent -> 0 (the crit
    // multiplier 2^(base*attr) is 1.0).
    const auto& sd = assets_->stage_data();
    for (const auto& t : sd.templates) {
        if (t.name != "Default") continue;
        player_fighter_.attributes.set("CriticalChance", t.critical_chance);
        player_fighter_.attributes.set("CriticalDamage", t.critical_damage);
        break;
    }
    // [Wave 11A M4] E2E hook: --crit-attr <chance> <damage> overrides the
    // player's crit attributes (mirrors --equip-magic; lets the real-binary
    // E2E pin the crit roll/multiplier deterministically).
    if (crit_attr_hook_chance_ >= 0) {
        player_fighter_.attributes.set("CriticalChance", crit_attr_hook_chance_);
        std::printf("[equip] E2E hook: player CriticalChance=%d\n",
                    crit_attr_hook_chance_);
    }
    if (crit_attr_hook_damage_ >= 0) {
        player_fighter_.attributes.set("CriticalDamage", crit_attr_hook_damage_);
        std::printf("[equip] E2E hook: player CriticalDamage=%d\n",
                    crit_attr_hook_damage_);
    }
    std::printf("[equip] attributes: player WeaponDamage=%d UnarmedDamage=%d "
                "BodyDefense=%d HeadDefense=%d | enemy baseline WeaponDamage=%d "
                "BodyDefense=%d\n",
                player_fighter_.attributes.raw("WeaponDamage"),
                player_fighter_.attributes.raw("UnarmedDamage"),
                player_fighter_.attributes.raw("BodyDefense"),
                player_fighter_.attributes.raw("HeadDefense"),
                enemy_fighter_.attributes.raw("WeaponDamage"),
                enemy_fighter_.attributes.raw("BodyDefense"));
}

void Game::host_start_menu_music() {
    auto& audio = aud::AudioEngine::instance();
            if (!audio.get_sound("menu_music")) {
                auto root = std::filesystem::path(asset_root_);
                for (const auto& base : {root/"assets"/"assets"/"music",
                                          root/"assets"/"music",
                                          root/"music"}) {
                    auto p = base / "menu.mp3";
                    if (std::filesystem::exists(p)) {
                        audio.load_music_file("menu_music", p.string());
                        break;
                    }
                }
            }
            if (audio.get_sound("menu_music")) {
                audio.play_music("menu_music", 0.5f, true);
            }
}

void Game::host_start_battle_music() {
    auto& audio = aud::AudioEngine::instance();
    auto root = std::filesystem::path(asset_root_);
    // [Wave 11C P2] The battle track is data-driven from the LOCATION's
    // params.xml <Root Music="id|id"> list - NOT a hardcoded track and NOT
    // stages.xml <Battle Music> (that is a separate alternate path with its
    // own caller). The original: ScreenFight ctor 0x8F426524 random-picks
    // one numeric ID from Location+0x18 (FUN_8F43BC98, its only caller) and
    // plays assets/music/<name>.mp3 via the music registry (FUN_8F64B174;
    // the registry files ARE the on-disk assets/music/fight<ID>_*.mp3)
    // - SPEC_PRESENTATION Q2, corrected per VERIFY_W11 (Q2 source
    // attribution: play site passes the Location object).
    const std::string loc = host_get_battle_location().empty()
                                ? current_location_name_
                                : host_get_battle_location();
    const auto ids = locations_.music_list_for(loc, asset_root_);
    std::string track_file;
    std::string picked_id;
    if (ids.empty()) {
        std::printf("[MUSIC] location='%s' has no <Root Music> list - "
                    "no battle track\n", loc.c_str());
    } else {
        // Random pick, like FUN_8F43BC98 (random index into the list).
        picked_id = ids[std::rand() % ids.size()];
        // Numeric ID -> registry: assets/music/fight<ID>_*.mp3.
        for (const auto& base : {root/"assets"/"music",
                                 root/"assets"/"assets"/"music",
                                 root/"music"}) {
            if (!std::filesystem::exists(base)) continue;
            for (auto& entry : std::filesystem::directory_iterator(base)) {
                const std::string fn = entry.path().filename().string();
                if (fn.rfind("fight" + picked_id + "_", 0) == 0 &&
                    fn.ends_with(".mp3")) {
                    track_file = entry.path().string();
                    break;
                }
            }
            if (!track_file.empty()) break;
        }
        if (track_file.empty()) {
            std::printf("[MUSIC] location='%s' music id '%s' has no "
                        "assets/music/fight%s_*.mp3 - no battle track "
                        "(graceful fallback)\n",
                        loc.c_str(), picked_id.c_str(), picked_id.c_str());
        } else {
            // path::c_str() is wchar_t* on Windows - copy to std::string
            // before printf("%s") or the track name truncates at the first
            // wide-character NUL byte.
            const std::string track_name =
                std::filesystem::path(track_file).filename().string();
            std::printf("[MUSIC] location='%s' music ids=%zu picked id='%s' "
                        "track='%s'\n",
                        loc.c_str(), ids.size(), picked_id.c_str(),
                        track_name.c_str());
        }
    }
    if (track_file.empty()) {
        // Graceful fallback: no track, no crash (the original logs
        // "Music: ... doesn't exist" and stays silent).
        audio.stop_music();
        return;
    }
    // Load under the fixed "battle_music" name each battle entry: a different
    // location's track replaces the previous one (load_music_file overwrites
    // the named slot).
    if (audio.load_music_file("battle_music", track_file)) {
        audio.play_music("battle_music", 0.5f, true);
    }
}

void Game::host_stop_music() {
    aud::AudioEngine::instance().stop_music();
}

void Game::host_play_ui_click() {
    aud::AudioEngine::instance().play("buy", 0.5f);
}

void Game::host_play_result_sound(const std::string& result) {
    if (result == "victory") {
                // Play coin sound for victory
                aud::AudioEngine::instance().play("coin_hit1", 0.7f);
            }
}

void Game::host_render_text(const std::string& text, float x, float y, float scale, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) const {
    const_cast<Game*>(this)->render_text(text, x, y, scale, {r, g, b, a});
}

bool Game::host_render_zone_bg(int zone_index, float x, float y, float w, float h) {
    // Lazy-load zone background textures on first use
            auto& zone_tex = assets_->zone_bg_textures();
            if (zone_tex.empty()) {
                auto root = std::filesystem::path(asset_root_);
                for (const auto& base : {root/"assets"/"768"/"image"/"zones", root/"768"/"image"/"zones"}) {
                    bool found_any = false;
                    for (int i = 1; i <= 7; i++) {
                        auto path = base / (std::to_string(i) + ".jpg");
                        if (std::filesystem::exists(path)) {
                            auto data = read_file(path.string());
                            if (!data.empty()) {
                                auto tex = std::make_unique<ren::Texture2D>();
                                if (tex->init_from_memory((const uint8_t*)data.data(), data.size())) {
                                    zone_tex[i] = std::move(tex);
                                    found_any = true;
                                }
                            }
                        }
                    }
                    if (found_any) break;
                }
                std::printf("[ZONE_BG] Loaded %zu zone textures\n", zone_tex.size());
            }
            auto it = zone_tex.find(zone_index);
            if (it == zone_tex.end()) return false;
            renderer_->draw_textured_quad_screen(*it->second, x, y, w, h);
            return true;
}

// ---------------------------------------------------------------------------
// Map screen
//
// [ORIGINAL] Everything here comes out of the shipped assets instead of being
// drawn as coloured rectangles:
//   assets/1536/image/zones/N.jpg                the painted zone sheet
//   assets/1536/image/battles/{base,active,locked}/batchBattles*.plist
//                                                one frame per battle kind
//   assets/1536/image/battles/<location>.jpg     the photo in the side scroll
// The node coordinates are `<Battle X=".." Y="..">` in stages.xml, measured
// from the centre of the sheet.
// ---------------------------------------------------------------------------

void Game::load_map_textures() {
    if (!assets_->map_icon_textures().empty()) return;
    auto root = std::filesystem::path(asset_root_);
    for (const auto& base : {root/"assets"/"1536"/"image"/"battles",
                             root/"1536"/"image"/"battles"}) {
        if (!std::filesystem::exists(base)) continue;
        // The three atlases are named batchBattlesBase / ...Active / ...Locked.
        load_texture_atlas_to_hud(base/"base", "batchBattlesBase");
        load_texture_atlas_to_hud(base/"active", "batchBattlesActive");
        load_texture_atlas_to_hud(base/"locked", "batchBattlesLocked");
        break;
    }
    // load_texture_atlas_to_hud drops frames into hud_textures(); move the
    // battle ones across so they cannot collide with HUD frame names.
    for (auto it = assets_->hud_textures().begin(); it != assets_->hud_textures().end(); ) {
        const bool is_icon = it->first.rfind("base_", 0) == 0 ||
                             it->first.rfind("active_", 0) == 0 ||
                             it->first.rfind("locked_", 0) == 0;
        if (is_icon) {
            assets_->map_icon_textures()[it->first] = std::move(it->second);
            it = assets_->hud_textures().erase(it);
        } else {
            ++it;
        }
    }
    std::printf("[MAP] battle icons loaded: %zu\n", assets_->map_icon_textures().size());
}

ren::Texture2D* Game::battle_preview_texture(const std::string& location) {
    if (location.empty()) return nullptr;
    auto& table = assets_->battle_preview_textures();
    auto it = table.find(location);
    if (it != table.end()) return it->second.get();
    auto root = std::filesystem::path(asset_root_);
    for (const auto& base : {root/"assets"/"1536"/"image"/"battles",
                             root/"1536"/"image"/"battles"}) {
        auto path = base / (location + ".jpg");
        if (!std::filesystem::exists(path)) continue;
        auto data = read_file(path.string());
        if (data.empty()) break;
        auto tex = std::make_unique<ren::Texture2D>();
        if (!tex->init_from_memory((const uint8_t*)data.data(), data.size())) break;
        auto* raw = tex.get();
        table[location] = std::move(tex);
        return raw;
    }
    // Remember the miss so a location without a photo is not re-read every frame.
    table[location] = nullptr;
    return nullptr;
}

scene::SceneHost::MapView Game::host_render_zone_map(
    int zone_index, float scroll_x, float x, float y, float w, float h)
{
    scene::SceneHost::MapView view;
    // host_render_zone_bg already lazy-loads the sheets; reuse that table.
    auto& zone_tex = assets_->zone_bg_textures();
    if (zone_tex.empty()) host_render_zone_bg(zone_index, 0, 0, 0, 0);
    auto it = zone_tex.find(zone_index);
    if (it == zone_tex.end() || !it->second) return view;

    const float sw = static_cast<float>(it->second->width());
    const float sh = static_cast<float>(it->second->height());
    if (sw <= 0.0f || sh <= 0.0f) return view;

    // Cover the viewport height and pan horizontally — the sheet is wider than
    // a 16:9 slice of it, which is what makes the map scrollable at all.
    const float scale = h / sh;
    const float draw_w = sw * scale;
    view.max_scroll = std::max(0.0f, draw_w - w);
    const float sx = std::max(0.0f, std::min(scroll_x, view.max_scroll));
    renderer_->draw_textured_quad_screen(*it->second, x - sx, y, draw_w, h);

    view.ok = true;
    view.scale = scale;
    view.centre_x = x - sx + draw_w * 0.5f;
    view.centre_y = y + h * 0.5f;
    return view;
}

bool Game::host_render_battle_icon(const std::string& icon, int state,
                                   float cx, float cy, float size) {
    load_map_textures();
    static const char* kPrefix[3] = {"base_", "active_", "locked_"};
    if (state < 0 || state > 2) state = 0;
    auto& table = assets_->map_icon_textures();
    // Fall back through the states: not every kind ships all three (the locked
    // atlas has 24 frames against the base atlas' 37).
    for (int s : {state, 0, 1}) {
        auto it = table.find(std::string(kPrefix[s]) + icon);
        if (it == table.end() || !it->second) continue;
        const float aspect = static_cast<float>(it->second->width()) /
                             static_cast<float>(std::max(1, it->second->height()));
        const float dh = size;
        const float dw = size * aspect;
        renderer_->draw_textured_quad_screen(*it->second, cx - dw * 0.5f,
                                             cy - dh * 0.5f, dw, dh);
        return true;
    }
    return false;
}

bool Game::host_render_battle_preview(const std::string& location,
                                      float x, float y, float w, float h) {
    auto* tex = battle_preview_texture(location);
    if (!tex) return false;
    renderer_->draw_textured_quad_screen(*tex, x, y, w, h);
    return true;
}

void Game::host_render_scroll_panel(float x, float y, float w, float h) {
    auto tex_of = [&](const char* n) -> ren::Texture2D* {
        auto it = assets_->scroll_textures().find(n);
        if (it != assets_->scroll_textures().end()) return it->second.get();
        auto it2 = assets_->hud_textures().find(n);
        return it2 == assets_->hud_textures().end() ? nullptr : it2->second.get();
    };
    // Parchment body, then the sheet's side edges, then the rolled bar on top
    // �?" the same three pieces as the dojo dialogue (PORT_PLAN 6.2), which is
    // why Paper_left/right are drawn as narrow strips and not as halves.
    // [P8] Piece sizes come from host_get_scroll_panel_layout: the roll bar
    // is a fixed 37-pt 3-slice and the paper edges keep the 116x1524 aspect.
    renderer_->draw_filled_rect_screen(x, y, w, h, {226, 205, 163, 250});
    const ScrollPanelLayout L = host_get_scroll_panel_layout(w, h);
    const float edge_w = L.edge_w;
    if (auto* pl = tex_of("Paper_left"))
        renderer_->draw_textured_quad_screen(*pl, x, y, edge_w, h);
    if (auto* pr = tex_of("Paper_right"))
        renderer_->draw_textured_quad_screen(*pr, x + w - edge_w, y, edge_w, h);

    auto* roll_l = tex_of("Roll_left");
    auto* roll_c = tex_of("Roll_center");
    auto* roll_r = tex_of("Roll_right");
    if (roll_l && roll_c && roll_r) {
        const float bar_h = L.bar_h;
        const float end_w = L.end_w;
        // [D6] The roll bars sit flush with the panel's top and bottom edges,
        // INSIDE the sheet: the soak showed the bottom texture (подложка)
        // stretched far beyond the parchment. The bars used to overhang the
        // panel by ~0.55 bar heights on each side, inflating the drawn
        // height of the whole dialogue box.
        const float bar_y = y;
        renderer_->draw_textured_quad_screen(*roll_l, x, bar_y, end_w, bar_h);
        renderer_->draw_textured_quad_screen(*roll_c, x + end_w, bar_y,
                                             w - 2.0f * end_w, bar_h);
        renderer_->draw_textured_quad_screen(*roll_r, x + w - end_w, bar_y,
                                             end_w, bar_h);
        // And one at the bottom, so the sheet reads as unrolled rather than cut.
        const float bot_y = y + h - bar_h;
        renderer_->draw_textured_quad_screen(*roll_l, x, bot_y, end_w, bar_h);
        renderer_->draw_textured_quad_screen(*roll_c, x + end_w, bot_y,
                                             w - 2.0f * end_w, bar_h);
        renderer_->draw_textured_quad_screen(*roll_r, x + w - end_w, bot_y,
                                             end_w, bar_h);
    }
}

bool Game::host_render_ui_texture(const std::string& name,
                                  float x, float y, float w, float h) {
    auto it = assets_->hud_textures().find(name);
    if (it == assets_->hud_textures().end() || !it->second) {
        auto it2 = assets_->scroll_textures().find(name);
        if (it2 == assets_->scroll_textures().end() || !it2->second) return false;
        renderer_->draw_textured_quad_screen(*it2->second, x, y, w, h);
        return true;
    }
    renderer_->draw_textured_quad_screen(*it->second, x, y, w, h);
    return true;
}

void Game::host_render_top_panel() {
    if (!platform_) return;
    render_hud(*platform_);
}

void Game::host_set_show_enemy(bool show) {
    show_enemy_ = show;
}

void Game::host_set_battle_mode(bool battle) {
    is_battle_mode_ = battle;
}

void Game::host_render_scene() {
    if (!location_loaded_) return;
            // Restore the location's clear color every frame.
            // This prevents clear_color bleed from other scenes (e.g. Map's dark blue)
            // that would show through gaps in the location background.
            if (location_ && !location_->color.empty()) {
                auto c = std::stoul(location_->color, nullptr, 16);
                renderer_->set_clear_color(
                    ((c>>16)&0xFF)/255.0f,
                    ((c>>8)&0xFF)/255.0f,
                    (c&0xFF)/255.0f, 1.0f);
            }
            render_location();
            // [ORIGINAL] Dojo training mode: show either punching bag OR enemy fighter.
            // Toggle with B key. Both are training dummies — no health, no win/lose.
            if (show_enemy_) {
                render_enemy_fighter();
            } else {
                render_punching_bag();
            }
            render_character();
            render_projectiles();
            update_and_render_hit_sparks(0.016f);
            render_debug_world(*platform_);
            render_debug_overlay(*platform_);
            // [ORIGINAL] The dojo shows the town HUD (top panel); a real fight
            // shows ScreenModel — bars, names, round dots — instead (D4).
            if (!is_battle_mode_) render_hud(*platform_);
            else render_fight_hud(*platform_);
            // [ORIGINAL] The virtual stick and the attack buttons are always on
            // screen while a fight (or the dojo) is up — they are the only
            // controls the original has.
            render_touch_controls();
            if (!is_battle_mode_ && menu_anim_progress_ > 0.01f) render_menu_expanded(*platform_);
            if (!is_battle_mode_ && overlay_ == Overlay::Dialog) render_dialog_overlay(*platform_);
}

void Game::host_render_loading() {
    render_loading_screen(*platform_);
}

void Game::host_render_prefight() {
    render_prefight(*platform_);
}

void Game::init_location() {
    // [ORIGINAL] Mount every derbh archive shipped next to the assets, the way
            // the original does (ShadowFight2.s86 references "assets/files.dz",
            // "ZONE_2.dz" ... at 0x1038ad68). All coders used by the game are
            // implemented natively, so no archive needs pre-extraction.
            // open_archive() is idempotent, so re-entering init_location() is safe.
            auto root = std::filesystem::path(asset_root_);
            auto& dz = resf2::dz::DzRegistry::instance();
            for (const auto& base : {root, root/"assets", root/"assets"/"assets"}) {
                dz.open_archives_in(base.string());
            }
            // Load stage data for the map scene
            if (!assets_->stages_loaded()) {
                // [ORIGINAL] Try multiple paths: root, root/assets, root/assets/files/assets
                std::vector<std::filesystem::path> candidates = {
                    root / "stages.xml",
                    root / "assets/stages.xml",
                    root / "assets/files/assets/stages.xml",
                };
                for (const auto& stages_path : candidates) {
                    if (std::filesystem::exists(stages_path)) {
                        auto stages_text = read_text(stages_path.string());
                        resf2::format::StageParser parser;
                        auto& sd = assets_->stage_data();
                        if (parser.parse(stages_text, sd)) {
                            assets_->set_stages_loaded(true);
                            std::printf("[STAGE] Loaded %zu zones from %s\n", 
                                       sd.zones.size(), stages_path.string().c_str());
                            boot_events_.push_back("stages.xml");  // [Wave 8] probe
                            break;
                        }
                    }
                }
            }
            // [Wave 8] quests.xml (17.67 s), packs.xml (18.36 s) and
            // config_cdn.xml (18.37 s) load AFTER stages, matching the
            // original chronology (LIVE_BOOT_TRACE §2). One-shot: init_location
            // runs per location change.
            if (!quests_config_loaded_) {
                quests_config_loaded_ = true;
                if (resf2::game::load_quests_config(asset_root_, boot_configs_.quests))
                    boot_events_.push_back("quests.xml");
                // [Wave 9B] S5: parse the full quest set for event dispatch
                // (host_trigger_quest_event). One-shot, same load point as
                // the config count above.
                if (!quest_defs_loaded_) {
                    quest_defs_loaded_ = true;
                    resf2::game::load_quest_defs(asset_root_, quest_defs_);
                }
            }
            if (!packs_config_loaded_) {
                packs_config_loaded_ = true;
                if (dz.load_packs_xml(asset_root_)) boot_events_.push_back("packs.xml");
            }
            if (!cdn_config_loaded_) {
                cdn_config_loaded_ = true;
                if (resf2::game::load_cdn_config(asset_root_, boot_configs_.cdn))
                    boot_events_.push_back("config_cdn.xml");
            }
            // Directories searched for loose assets. The original ships part of
            // its asset tree outside the archives (e.g. assets/1536/locations/dojo
            // in the APK), so this is a genuine lookup path, not a workaround.
            for (const auto& base : {root, root/"assets", root/"assets"/"assets"}) {
                dz.add_fallback_dir(base.string());
            }
        
            load_location(current_location_name_);
            location_loaded_ = true;
            if (location_ && !location_->color.empty()) {
                auto c = std::stoul(location_->color, nullptr, 16);
                renderer_->set_clear_color(
                    ((c>>16)&0xFF)/255.0f,
                    ((c>>8)&0xFF)/255.0f,
                    (c&0xFF)/255.0f, 1.0f);
            }
            load_skeleton();
            load_body_model();
            // [P3] Equipped armor/helm models (list.xml Model attr) attach to
            // the fighter — users.xml Armor="ARMOR_ROBE" Helm="Head" (Q4).
            load_equipment_models();
            // [R2] The enemy fighter's own model per the current battle
            // setup (stages.xml template items -> list.xml Model attrs).
            // Idempotent; no-op when no battle is queued (empty enemy name)
            // or the name matches no template. Re-runs here so the Battle
            // scene's location load picks up a battle queued mid-session.
            load_enemy_fighter_models();
            load_punching_bag_model();
            load_animations();
            load_moves();
            load_internal_settings();
            load_tactics();
            // [H06] Load enemy weapon — resolved from the battle setup
            // (stages.xml template <Items> -> list.xml Model) by
            // load_enemy_fighter_models(). The old hardcoded
            // "weapon_knuckles.xml" loaded the SAME weapon for every battle
            // (HARDCODE_AUDIT H06). Fallback: no template matched (dojo
            // sparring, generic test enemy) keeps the real weapon_knuckles.xml
            // as the visual default (U1 render pin); a template that resolved
            // to Fists loads NO weapon (the disciple is unarmed).
            if (enemy_template_resolved_) {
                if (!enemy_weapon_file_.empty())
                    load_enemy_weapon(enemy_weapon_file_);
            } else {
                load_enemy_weapon("weapon_knuckles.xml");
            }
            // Load player's equipped weapon model
            load_player_weapon(equipped_weapon_);
            auto lang = load_user_settings_language(asset_root_);
            std::string loc = lang.value.empty() ? std::string("eng") : normalize_localization_path(lang.value);
            load_localization(loc);
            load_hud_textures();
            load_menu_textures();
            load_hud_font(loc);
            load_sounds();
            if (location_) {
                // Player/enemy positions in params.xml use Y-DOWN, same as image
                // coordinates. Location images are Y-inverted in render_location
                // (world_y = -img.y). But player/enemy Y is used directly (NOT
                // inverted) because the skeleton model space already has Y-UP
                // with NPivot at Y=169 and feet at Y=73 (difference = 96).
                //
                // Floor (layer_3) at params y=225 → world_y = -225 (inverted image).
                // Floor top surface at -225 + 32 = -193.
                // Player NPivot at params y=-93 → world_y = -93 (direct).
                // Player feet at -93 - 96 = -189. Floor at -193. Gap = 4. ✓
                //
                // Bag: enemy_y = -105. Bag NPivot at -105.
                // Node12 (ceiling attachment) at -105 + 226 = 121.
                // Ceiling (layer_5) at params y=-202 → world_y = +202.
                // Need Node12 at ceiling: bag_cy + 226 = 202 → bag_cy = -24.
                // Offset from enemy_y: -24 - (-105) = 81.
                // bag_cy = enemy_y + 81.
                //
                // [ORIGINAL] Location::load (ShadowFight2.s86 FUN_10144420) reads
                //   +0x2c Floor, +0x34 Wall, +0x38 Width, +0x3c Height,
                //   +0x40 MinWidth (defaults to Width), +0x44 MinWidth/Width.
                // Layer <Image> X is centred on the world origin, but ModelsViewer
                // positions are measured from the LEFT edge, so world_x = X - Width/2.
                // Check on dojo: enemy 973 - 1960/2 = -7, and the ceiling hook the
                // bag hangs from is drawn at layer_3 X=-10 — they agree to 3 units.
                // (The previous X_OFFSET = 983 was this same value hand-fitted.)
                const float half_world_w = location_->width * 0.5f;
                player_pos_x_ = location_->player_x - half_world_w;
                player_pos_y_ = location_->player_y;  // no invert (matches location rendering)
                // [ORIGINAL] Enemy fighter position: same as the punching bag/enemy
                // spawn point from params.xml. The enemy skeleton stands here and
                // AI controls its behavior.
                enemy_pos_x_ = location_->enemy_x - half_world_w;
                enemy_pos_y_ = location_->enemy_y;  // use enemy Y from params.xml (not player Y)
                enemy_facing_right_ = false;  // faces left toward player
            }
            update_camera();

            // Play start stance animation (from moves.xml: FistsStartStance-Right)
            // This is the intro animation before the fight begins.
            // stance_2.bin = right-facing start stance.
            // Cannot be interrupted — plays once, then transitions to stance_idle.
            if (assets_->animations().count("stance_2")) {
                play_animation("stance_2", false, 3);  // priority 3: intro stance (non-interruptible)
                current_move_ = "StartStance";
                int fc = assets_->animations()["stance_2"].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                move_state_ = 10;  // special move state (non-interruptible)
                start_stance_playing_ = true;
                std::printf("[STANCE] Playing start stance (stance_2, %d frames)\n", fc);
                // [A2] Enemy start stance (SOAK_TRIAGE.md A2): both fighters
                // play the intro stance — the soak showed only the player in
                // stance_2 while the enemy stood in fists_idle. The enemy
                // renders this animation mirrored (enemy_facing_right_ is
                // false, facing the player), and the AI block is gated during
                // the intro (A1), so nothing overwrites the pose until the
                // fight begins. The anim clock advances in the AI block.
                enemy_anim_ = "stance_2";
                enemy_anim_time_ = 0.0f;
            } else if (assets_->animations().count("stance_idle")) {
                play_animation("stance_idle", true, 0);  // priority 0: idle (always interruptible)
            }
}

// [STEP 4.7] Trigger knockback/knockdown on the player fighter.
// Sets vertical velocity for launch and marks knockdown state.
// Physics integration happens in host_update_gameplay().
void Game::trigger_knockback(float launch_velocity, bool knockdown) {
    if (player_fighter_.is_dead) return;
    y_velocity_ = launch_velocity;
    gameplay_y_offset_ = 0.01f;  // small initial offset to start physics
    is_knocked_down_ = knockdown;
    if (knockdown) {
        knockdown_timer_ = 0;  // timer starts on landing
        player_fighter_.invuln_time = 1.0f;  // brief invulnerability
    }
    std::printf("[KNOCKBACK] launch_vel=%.1f knockdown=%d\n", launch_velocity, knockdown ? 1 : 0);
}

// [ORIGINAL] Enemy -> player melee damage, ported from Model::getTotalDamage
// @ game+0x4527B4 (engine/game/damage_formula.hpp — the multiplication order
// base*f2*f1*f3*add is preserved there; call it, never reimplement it).
//
// Both strike paths in host_update_gameplay (blocked / unblocked) share this
// computation so the two near-duplicate sites cannot diverge. Attribute
// pairing follows the original's helper @ game+0x60DF98: enemy strikes are
// unarmed hits, so attacker UnarmedDamage vs defender BodyDefense
// (f3 = 2^(delta/10)).
//
// Block stays at the CALL SITE as a post-multiplier (base_block_factor = 0.5):
// getTotalDamage has no block term — the original's tracer @ game+0x438530
// prints BlockDamageFactor/Block in the hit processor that CALLS the formula.
//
// [HEURISTIC-TODO] f1/f2 selector terms (game+0x4A94F0 / game+0x4A95A8) are
// disabled-neutral (1.0f) — the factor-set data is not yet ported; crit stays
// 1.0f (CriticalChance/CriticalDamage system not yet ported).
static float enemy_damage_to_player(float base_damage, bool blocked,
                                    const AttributeSet& enemy_attrs,
                                    const AttributeSet& player_attrs,
                                    const DamageSettings& dmg_settings) {
    DamageInputs din;
    // base = 2^(attr*w): DamageFactor is absent in MVP -> get_or 0 -> base 1.0f.
    // get_or, NEVER raw get(): the -1e35f getParameter sentinel must never
    // reach powf (the game+0x60DF98 alignment helper defaults a miss to 0.0).
    din.base_attribute = enemy_attrs.get_or("DamageFactor", 0.0f);
    din.base_weight = dmg_settings.damage_factor_base;
    din.attribute_difference =
        attribute_difference(enemy_attrs, "UnarmedDamage",
                             player_attrs, "BodyDefense");
    din.hit_damage = base_damage;      // original's hit[0x48]
    din.enemy_damage_bonus = 0.0f;     // original's enemy[0x774] — not ported
    const float block_factor = blocked ? dmg_settings.base_block_factor : 1.0f;
    return get_total_damage(din) * block_factor;
}

// [ORIGINAL] MAGIC CHARGE — SPEC_COMBAT_CORE.md Q1 / VERIFY_W11.md Q1 GREEN.
//
// addMagicCharge @ game+0x3F2660 (0x8F4A9660) + the threshold recompute @
// game+0x3F10D8 (0x8F4A80D8). Every landed attack-interval hit charges BOTH
// fighters, in two calls inside Fight::applyHit @ game+0x3E9F9C (0x8F420F9C):
//
//   (attacker, dmg, victim, blocked, critical, role=0)   charges the attacker
//   (victim,   dmg, attacker, blocked, critical, role=1) charges the victim
//
//   charge += pow2Factor(attacker, blocked)      // 0x8F4A94F0 (flag -> 2^attr else 1.0)
//             * powf(2.0f, rechargeAttr(recipient))  // role 0: PainRecharge,
//                                                  //   role 1: DamageRecharge
//             * pow2Factor(victim, critical)      // 0x8F4A95A8
//             * damage,                           // interval Damage Value (+0x48)
//   clamped to [0,1]; skipped when the recipient's count is already 1
//   (full bar never overcharges); suppressed when the ATTACKING move carries
//   NoMagicRecharge (MoveDef+0x148 via 0x8F47D378). Crossing 1.0 sets
//   count=1 (MagicCharged) and resets the bar. Round start (0x8F41C8E4):
//   count=0, charge=clamp(InitialCharge,0,1).
//
// [HEURISTIC-TODO] The pow2 factors and the PainRecharge/DamageRecharge
// attributes are zero-fallback (the spec's [UNCERTAIN] attr source: the
// stages.xml Magic*Recharge names are 17/19 chars, the binary's lookups are
// 12/13-char names, and the factor-set records FUN_8f65fc28/FUN_8f65fcc0 are
// not ported), so f2=f3=1.0 and 2^0=1.0 — a landed punch (HighPunch
// <Damage Value=0.11>) adds 0.11 and the bar fills after ~10 landed hits.
static void add_magic_charge(FighterState& recipient, float damage,
                             bool blocked, bool critical, int role,
                             uint64_t frame, const char* tag) {
    if (recipient.magic_count != 0) return;   // full bar: no overcharge
    const float f2 = 1.0f;                    // pow2Factor(attacker, blocked)
    const float f3 = 1.0f;                    // pow2Factor(victim, critical)
    const float f1 = (role == 0)
        ? recipient.attributes.get_or("PainRecharge", 0.0f)
        : recipient.attributes.get_or("DamageRecharge", 0.0f);
    const float c = recipient.magic_charge +
                    f2 * std::pow(2.0f, f1) * f3 * damage;
    recipient.magic_charge = std::clamp(c, 0.0f, 1.0f);
    // Threshold crossing (0x8F4A80D8): charge >= 1.0 -> count=1, bar reset.
    // count > 1 is impossible here (we skipped when count != 0), so the
    // "Wrong magic count" branch never fires in this port.
    if (recipient.magic_charge >= 1.0f) {
        recipient.magic_count = 1;
        recipient.magic_charge = 0.0f;
    }
    std::printf("[MAGIC-CHG] f=%llu %s role=%d chg=%.3f cnt=%d\n",
                (unsigned long long)frame, tag, role,
                recipient.magic_charge, recipient.magic_count);
}

// Resolve the MoveDef whose animation filename matches the playing anim
// (used for the enemy's current attack move — enemy_anim_ is a stripped
// filename). Returns nullptr when no move owns the anim.
static const MoveDef* move_def_by_anim(
    const std::unordered_map<std::string, MoveDef>& moves,
    const std::string& anim) {
    std::string want = anim;
    if (want.size() > 4 && want.compare(want.size() - 4, 4, ".bin") == 0)
        want = want.substr(0, want.size() - 4);
    for (const auto& [name, m] : moves) {
        (void)name;
        std::string fn = m.filename;
        if (fn.size() > 4 && fn.compare(fn.size() - 4, 4, ".bin") == 0)
            fn = fn.substr(0, fn.size() - 4);
        if (fn == want) return &m;
    }
    return nullptr;
}

void Game::host_update_gameplay(uint32_t dt) {
    // [DIAGNOSTIC] Advance input-script frame counter and apply events
    // scheduled for this frame BEFORE reading input. This keeps script
    // frame N aligned with gameplay frame N (Boot/Loading don't count).
    platform_->tick_input_script();
    last_frame_dt_ms_ = dt;
    const auto& input = platform_->input();
    float dt_sec = (float)dt / 1000.0f;

    // [Q1] Quest movement stage: count step events — a step begins when the
    // movement state ENTERS a moving state (1 = back, 2 = forward). The
    // counter is reset when the hint scroll appears (host_reset_menu_state).
    // [HEURISTIC-TODO] The original requires "a few steps (forward/back)";
    // kTutorialMoveSteps = 4 is the behavioural reading, not binary-verified.
    if (move_state_ == 1 || move_state_ == 2) {
        if (last_step_state_ != 1 && last_step_state_ != 2)
            ++tutorial_move_steps_;
    }
    last_step_state_ = move_state_;

    // [ORIGINAL] Auto-trigger the Sensei tutorial dialog on first dojo entry.
    // The dialog is queued by check_tutorial() during loading; this fires the
    // scene transition on the first gameplay frame so the player sees the dojo
    // for one frame before the dialog covers it (matching the original's flow).
    if (tutorial_dialog_pending_ && !dialogue_lines_.empty()) {
        tutorial_dialog_pending_ = false;
        tutorial_dialog_shown_ = true;
        request_scene_transition(scene::SceneId::Dialogue);
        std::printf("[tutorial] auto-triggering Sensei dialog (Dialogue scene)\n");
        return;  // skip the rest of this frame; scene transition is deferred
    }

    // Advance the tutorial hint scroll unroll animation (0→1 over ~0.5s).
    if (overlay_ == Overlay::Dialog && dialog_overlay_anim_ < 1.0f) {
        dialog_overlay_anim_ = std::min(1.0f, dialog_overlay_anim_ + dt_sec * 2.0f);
    }
    // Count frames the overlay has been visible (grace period before dismissal).
    if (overlay_ == Overlay::Dialog) {
        overlay_show_frames_++;
    }

    // [ORIGINAL] Combat state update: decay hit flash, hit stun, invuln.
    if (player_hit_flash_ > 0) player_hit_flash_ = std::max(0.0f, player_hit_flash_ - dt_sec);
    if (enemy_hit_flash_ > 0) enemy_hit_flash_ = std::max(0.0f, enemy_hit_flash_ - dt_sec);
    if (player_fighter_.hit_stun_time > 0) player_fighter_.hit_stun_time = std::max(0.0f, player_fighter_.hit_stun_time - dt_sec);
    if (enemy_fighter_.hit_stun_time > 0) enemy_fighter_.hit_stun_time = std::max(0.0f, enemy_fighter_.hit_stun_time - dt_sec);
    if (player_fighter_.invuln_time > 0) player_fighter_.invuln_time = std::max(0.0f, player_fighter_.invuln_time - dt_sec);
    if (enemy_fighter_.invuln_time > 0) enemy_fighter_.invuln_time = std::max(0.0f, enemy_fighter_.invuln_time - dt_sec);
    // Combo timer: reset combo if no hit for 2 seconds
    if (combo_timer_ > 0) {
        combo_timer_ -= dt_sec;
        if (combo_timer_ <= 0) {
            player_fighter_.hits_landed = 0;
            enemy_fighter_.hits_landed = 0;
        }
    }

    // [STEP 4.7] Knockback/knockdown physics update.
    // When launched: positive Y velocity lifts fighter, gravity pulls back down.
    // On landing: reset offset, start knockdown recovery timer.
    // During knockdown: wait for timer, then return to stance.
    if (gameplay_y_offset_ > 0.0f || y_velocity_ > 0.0f) {
        gameplay_y_offset_ += y_velocity_ * dt_sec;
        y_velocity_ -= kKnockbackGravity * dt_sec;
        if (gameplay_y_offset_ <= 0.0f) {
            gameplay_y_offset_ = 0.0f;
            y_velocity_ = 0.0f;
            if (is_knocked_down_) {
                knockdown_timer_ = 60;  // ~1 second at 60fps
            }
        }
    }
    if (is_knocked_down_ && gameplay_y_offset_ <= 0.0f) {
        if (knockdown_timer_ > 0) {
            knockdown_timer_--;
        } else {
            is_knocked_down_ = false;
        }
    }

    // Update audio engine (mix + write to backend)
    aud::AudioEngine::instance().update(dt_sec);

    // [ORIGINAL] Player block: triggered by holding BACK direction while in stance.
    // From Model::startAction area in binary — block state is detected when the
    // player holds the back direction during idle/stance. The specific block
    // animation (HighBlock/SweepBlock) is selected based on crouch state.
    // is_blocking_ is updated in the input processing section below.
    if (!player_fighter_.is_dead && hit_anim_ == 0 && move_state_ == 0 &&
        !start_stance_playing_) {
        // Default: not blocking unless back is held (set in input section below)
        // Keep current state until input processing overrides it
    } else if (!player_fighter_.is_dead && move_state_ != 11) {
        // Not in idle/block state — clear block
        player_fighter_.is_blocking = false;
    }

    // [ORIGINAL] Tick block decision cooldown (FUN_10171d80 loop interval).
    // The AI decision loop runs every 0.6-1.0s; when it fires, a block
    // decision becomes pending and is evaluated when an enemy attack lands.
    if (block_decision_cooldown_ > 0) {
        block_decision_cooldown_ -= dt_sec;
        if (block_decision_cooldown_ <= 0) {
            block_decision_pending_ = true;
        }
    }
    // Decay recent damage tracking (used by BlockChance DamageFactor)
    recent_damage_taken_ = std::max(0.0f, recent_damage_taken_ - dt_sec * 10.0f);

    // [ORIGINAL] Enemy AI decision loop, now fully pipeline-driven
    // (ADR-005): the TacticDecisionPipeline decides on the ResponseDelay
    // frame countdown and the executor below consumes the stored decision
    // directly (Phase E deleted the legacy 0..4 state machine, the adapter
    // and the invented decision interval).
    //
    // Only when there IS an enemy. This used to run whenever the fighters were
    // alive, so in the dojo - where the opponent is a punching bag and
    // show_enemy_ is false - an invisible sparring partner kept stepping,
    // blocking and swinging, and its attack sound played out of nowhere. The
    // enemy is not drawn there and must not act there either.
    if (show_enemy_ && !enemy_fighter_.is_dead && !player_fighter_.is_dead) {
        // [D3] One tick per AI frame: advances the TacticMemory interval
        // counters and the decay clock (frames stand in for the binary's
        // fighter+0x71c hits-taken counter, MEMORY_INDEXING_R56.md §2).
        combat_.mutable_enemy_tactic_memory().tick();
        enemy_attack_cooldown_ = std::max(0.0f, enemy_attack_cooldown_ - dt_sec);
        // [A1] Battle-intro gate (SOAK_TRIAGE.md A1): during the
        // start-stance phase the enemy takes no decisions and performs no
        // actions — no attack, step, block or hit before the intro phase
        // completes (the soak showed "[COMBAT] Enemy hit player" twice right
        // after "[scene] enter Battle", before StartStance ended). The gate
        // wraps the WHOLE stun/decide/execute chain — the executor consumes
        // the stored decision every frame and would otherwise force the
        // enemy into fists_idle during the intro. The anim clock below
        // still advances so the enemy's start-stance pose plays (A2), and
        // the facing update keeps the stance mirrored correctly.
        if (start_stance_playing_) {
            // Intro hold: the enemy stays in the start-stance pose set at
            // battle start (enemy_anim_ = "stance_2").
        } else if (enemy_fighter_.hit_stun_time > 0) {
            // Stunned - can't act
            // [H05] Real catalog hit reaction (high_hit.bin); "fists_hit"
            // is not a moves.xml name.
            // [Soak-fix Wave 9A] F1: the zone-resolved reaction anim keeps
            // playing (apply_player_hit_feedback) and the reversed-impulse
            // knockback is spread over the reaction duration.
            enemy_anim_ = enemy_hit_anim();
            if (enemy_knockback_vx_ != 0.0f)
                enemy_pos_x_ += enemy_knockback_vx_ * dt_sec;
        } else {
            const TacticDef* td = tactics_.tactic("Standard");
            if (!td) td = tactics_.tactic("NoTables");
            float dist = std::abs(enemy_pos_x_ - player_pos_x_);
            const bool pipeline_ready = td && tactic_tables_.table_count() > 0;
            // [D4/E3/A4] Re-entry gate (ADR-005 D8 + soak-fix A4): the
            // pipeline re-enters only when the ResponseDelay frame countdown
            // has elapsed (ticked per AI frame above; 0 = unblocked) AND the
            // current decision's R4 Wait countdown has expired
            // (wait_frames_remaining, DECISION_SEMANTICS.md R4 §3.4 — the
            // binary re-decides only when decision+0x12 runs out). Phase E
            // deleted the invented enemy_ai_decision_interval_ gate with the
            // fallback branches.
            const TacticMemory& tmem = combat_.enemy_tactic_memory();
            const bool decision_due =
                tmem.frames_until_next_decision == 0 &&
                tmem.wait_frames_remaining == 0;
            if (decision_due) {
                if (pipeline_ready) {
                    // [D3] LIVE enemy AI runs through the TacticDecisionPipeline
                    // (ADR-005 D1/D7): the seven stages in the tracer's fixed
                    // order (UseDefense .. UseCautiousMovements) evaluate
                    // against the live fight state; the first stage that fires
                    // wins and becomes the stored decision the executor below
                    // consumes directly (E2, ADR-005 Phase B — the adapter is
                    // bypassed, kept one commit for bisectability, deleted in
                    // Phase E). decide() feeds the <AnimationFactors> probe
                    // from enemy_tactic_memory_ itself (D5, per-memory decayed
                    // sums); bullets/anim_frames stay 0 = neutral — the legacy
                    // fight state has no per-frame bullet or enemy
                    // animation-frame source (R3: a miss is never an error).
                    TacticContext ctx;
                    ctx.distance = dist;                       // world points
                    ctx.health = (player_fighter_.max_health > 0)
                        ? enemy_fighter_.health / player_fighter_.max_health : 1.0f;
                    ctx.enemy_health = (player_fighter_.max_health > 0)
                        ? player_fighter_.health / player_fighter_.max_health : 1.0f;
                    // [Soak-fix Wave 9A] F2: the defense draw (and every
                    // curve with Damage/Hit/Counter factors) reads the
                    // DEFENDER's memory of the ATTACKER's actions
                    // (BLOCK_LOGIC.md §1.2: DamageFactor = "damage recently
                    // taken", HitFactor = "when the bot has been hit") —
                    // never the defender's own offense stats. The old feeds
                    // (the enemy's own animation damage + his own hits
                    // landed) plus the standing anim-frames term scored the
                    // UseDefense block ~0.4 on EVERY decision vs a passive
                    // player — the soak's standing block loop. The memory
                    // is fed on player hits (record_hit_taken).
                    float taken_damage = 0.0f, taken_hits = 0.0f,
                          taken_counter = 0.0f;
                    TacticMemory& enemy_mem = combat_.mutable_enemy_tactic_memory();
                    for (const auto& rec : enemy_mem.records) {
                        taken_damage += enemy_mem.decayed_damage(rec.name);
                        taken_hits += enemy_mem.decayed_hits(rec.name);
                        taken_counter += enemy_mem.decayed_counter(rec.name);
                    }
                    ctx.damage = taken_damage;
                    ctx.hits = taken_hits;
                    ctx.counter = taken_counter;
                    ctx.current_animation = enemy_anim_;       // probe target
                    // [Soak-fix Wave 9A] F2: the opponent's attack window —
                    // the stage-1 (UseDefense) gate fires only in reaction
                    // to it (the player's remaining attack frames; 0 while
                    // passive).
                    ctx.threat_frames =
                        (!current_move_.empty() && hit_anim_ > 0)
                            ? hit_anim_ * anim_fps_ / 1000.0f : 0.0f;
                    // [Soak-fix A4] R4 wait-mapping inputs with REAL engine
                    // data (DECISION_SEMANTICS.md R4 §3.1; VERIFY_R34.md
                    // GREEN). ctx.anim_frames = the current animation's
                    // R4-modeled duration (FUN_8f47cbe0:
                    // (frames+1)*(maxAttr - X + 2) + 1) — the frame count is
                    // the animation's real duration from the asset table;
                    // maxAttr/X are [UNCERTAIN] runtime records ->
                    // zero-fallback, leaving the verified +2/+1 constants:
                    // 2*frames+3.
                    //   damage = the matching move's <Damage Value> from
                    //     moves.xml (via FileName == <anim>.bin).
                    //   speed -> WeaponDamage ([HEURISTIC-TODO R4]: the
                    //     binary's attribute-name lists behind
                    //     FUN_8f43f0b8/FUN_8f43f0cc are runtime-populated,
                    //     names [UNCERTAIN]; WeaponDamage is the best
                    //     available fighter attribute).
                    //   animRange/animX stay 0 (no engine source).
                    float anim_frames = 0.0f;
                    if (const auto ait = assets_->animations().find(enemy_anim_);
                        ait != assets_->animations().end()) {
                        anim_frames = static_cast<float>(ait->second.frame_count);
                    }
                    ctx.anim_frames =
                        (anim_frames + 1.0f) * (0.0f - 0.0f + 2.0f) + 1.0f;
                    // [Soak-fix Wave 9A] F2: the old feed put the ENEMY'S
                    // OWN animation's move damage into ctx.damage — the
                    // wrong input for every curve (the defense draw needs
                    // the damage the enemy has TAKEN; see the memory-fed
                    // ctx.damage/hits/counter above). Deleted here so the
                    // memory sums are not overwritten.
                    ctx.anim_range = 0.0f;
                    ctx.speed_attr =
                        enemy_fighter_.attributes.get_or("WeaponDamage", 0.0f);
                    ctx.max_attr = 0.0f;
                    ctx.anim_x = 0.0f;

                    DecisionTrace trace;
                    const TacticDecision decision = decide(
                        *td, ctx, combat_.mutable_enemy_tactic_memory(),
                        tactic_tables_, std::rand, trace);
                    // [E2] Store the decision for the executor: it switches
                    // on the TacticDecision directly (ADR-005 Phase B) — the
                    // adapter is bypassed and enemy_ai_state_ is no longer
                    // written on the loaded path (Phase E deletes it with
                    // the fallback branches).
                    ai_last_decision_ = decision;

                    // Stash for the F1 overlay (ADR C5): the DecisionTrace
                    // stage line-groups as candidate/weight rows (weight = the
                    // stage's first printed score; every stage is evaluated,
                    // the first to fire wins), plus the decision's stage and
                    // animation as the pick.
                    ai_last_candidates_.clear();
                    ai_last_weights_.clear();
                    for (const std::string& l : trace.lines()) {
                        // Stages end where the epilogue begins (DistanceError..).
                        if (l.rfind("DistanceError", 0) == 0) break;
                        const std::size_t colon = l.find(':');
                        ai_last_candidates_.push_back(
                            (colon == std::string::npos) ? l : l.substr(0, colon));
                        float w = 0.0f;
                        const std::size_t sep = l.find(" / ");
                        if (sep != std::string::npos) {
                            char* end = nullptr;
                            w = std::strtof(l.c_str() + sep + 3, &end);
                        }
                        ai_last_weights_.push_back(w);
                    }
                    ai_last_distance_ = dist;
                    ai_last_pick_ = std::string(stage_label(decision.stage));
                    if (!decision.animation.empty())
                        ai_last_pick_ += "/" + decision.animation;

                    // [D4] Each decision opens a fresh ResponseDelay window:
                    // the countdown rolls within the tactic's [Min,Max]
                    // (inclusive) and ticks down one per AI frame, blocking
                    // re-entry until it reaches 0.
                    combat_.mutable_enemy_tactic_memory().start_response_delay(
                        td->response_delay.min, td->response_delay.max, std::rand);

                    // [Soak-fix A4] Hold the decision for its R4 wait: the
                    // per-decision Wait countdown (decision+0x12) now paces
                    // re-entry — the executor below keeps executing the
                    // stored decision (idle anim for a wait) while it runs.
                    // wait <= 0 -> immediate re-entry (binary fallback).
                    combat_.mutable_enemy_tactic_memory().start_decision_wait(
                        decision.wait_frames);
                } else {
                    // [E3] No settings (or no table families): a traced
                    // idle/wait decision - the enemy stays neutral (no
                    // movement/attack/block) until settings load (ADR P4,
                    // GATE GE: the legacy roulette and the random-roll
                    // fallback branches were deleted here). The stash keeps
                    // the F1 overlay truthful: one "Idle" row, no weights.
                    ai_last_decision_ = TacticDecision{};
                    ai_last_candidates_.assign({"Idle"});
                    ai_last_weights_.assign({0.0f});
                    ai_last_distance_ = dist;
                    ai_last_pick_ = "(no tactics)";
                }
            }
        // Execute the stored decision (E2, ADR-005 Phase B): the decision's
        // animation name drives the enemy — attack animations open the
        // attack window (cooldown as before), steps move the enemy,
        // UseDefense/block animations set the block state, and a wait (or
        // unclassified animation) idles for the decision's wait_frames.
        // The legacy enemy_ai_state_ int is bypassed — it stays at its
        // reset value and Phase E deletes it with the fallback branches.
        const TacticDecision& decision = ai_last_decision_;
        const bool decision_is_step = ai_anim_is_step(decision.animation);
        enemy_fighter_.is_blocking =
            (decision.stage == DecisionStage::kUseDefense) ||
            ai_anim_is_block(decision.animation);
        float enemy_speed = 90.0f;
        if (decision_is_step && !ai_anim_is_retreat(decision.animation)) {  // approach
            if (enemy_pos_x_ > player_pos_x_) enemy_pos_x_ -= enemy_speed * dt_sec;
            else enemy_pos_x_ += enemy_speed * dt_sec;
            enemy_anim_ = "step_forward";
            enemy_facing_right_ = (player_pos_x_ > enemy_pos_x_);
        } else if (decision_is_step) {  // retreat
            if (enemy_pos_x_ < player_pos_x_) enemy_pos_x_ -= enemy_speed * dt_sec;
            else enemy_pos_x_ += enemy_speed * dt_sec;
            enemy_anim_ = "step_back";
        } else if (ai_anim_is_attack(decision.animation) &&
                   enemy_attack_cooldown_ <= 0) {  // attack
            // [H05] The attack animation comes from the enemy's WEAPON
            // (moves.xml 1key attack of the loadout's family); the old
            // hardcoded "high_punch" made a sword loadout swing with fists
            // anims (HARDCODE_AUDIT H05).
            enemy_anim_ = enemy_attack_anim();
            enemy_attacking_ = true;
            enemy_attack_hit_done_ = false;
            enemy_attack_duration_ = 0.4f;
            enemy_attack_cooldown_ = 1.5f;
            // [H08] The swing plays from its FIRST frame: the mid-swing hit
            // check (enemy_attack_duration_ <= 0.2 -> 0.208 s, frame ~4 at
            // 20 fps) must sample the move's authored <Attack> interval
            // (HighPunch: Start=4 End=5) — the extended punch pose. Without
            // the reset the accumulated anim clock lands the check on an
            // arbitrary phase (wind-up/recovery ~80% of the time) and the
            // fist edge misses at close range.
            enemy_anim_time_ = 0.0f;
            // [S2] The swing sound comes from the ENEMY's voice set
            // (stages.xml <Template Voice=>), not the player's female set.
            play_sound(enemy_attack_sound(2), 0.4f);
            // [ORIGINAL] Dojo is TRAINING — enemy attacks don't deal damage.
            // In the original, the Dojo sparring partner is a training dummy.
            // Health/damage only applies in real fights (map battles).
        } else if (enemy_fighter_.is_blocking) {  // block
            // [H05] Real block animation (high_block.bin / duck.bin);
            // "fists_block" is not a moves.xml name.
            enemy_anim_ = enemy_block_anim();
        } else if (!enemy_attacking_) {  // idle — the decision's wait
            // [H08] While the swing is in flight (enemy_attack_duration_ >
            // 0) the attack pose must NOT be overwritten: the mid-swing hit
            // check samples enemy_anim_ and would otherwise test the rest
            // pose (hands at the sides) and never connect at close range.
            // [H07] Real catalog stance idle (fists1_stance_idle for fists;
            // the weapon's own stance idle once the loadout resolves it).
            enemy_anim_ = enemy_idle_anim();
        }
        if (enemy_attacking_) {
            enemy_attack_duration_ -= dt_sec;
            // In a real fight the swing connects at its midpoint.
            // [HEURISTIC-TODO] The enemy is a placeholder until warrior
            // templates land (5.3): no skeleton collision on his side, so the
            // hit is a range test at HighPunch's tactic distance (Max=250 in
            // moves.xml), and the damage fraction is HighPunch's
            // <Damage Value>. A blocking player takes nothing — the block
            // interval semantics of the original.
            if (is_battle_mode_ && !enemy_attack_hit_done_ &&
                enemy_attack_duration_ <= 0.2f) {
                enemy_attack_hit_done_ = true;
                // [H08] The swing connects via MODEL-EDGE COLLISION: the
                // enemy's attacking edges (his weapon's/skeleton's) against
                // the player's body capsules — the R2 hit-test path mirrored.
                // The old `dist <= 250` range test stood in for the enemy's
                // skeleton collision (HARDCODE_AUDIT H08) and made every
                // mid-range swing connect regardless of geometry; it remains
                // only as the documented fallback inside
                // enemy_attack_connects() when no collision path is loaded.
                const bool connected = enemy_attack_connects();
                if (connected && player_fighter_.invuln_time <= 0 &&
                    !player_fighter_.is_dead) {
                    // [ORIGINAL] Block is NOT automatic — it's a weighted roulette decision
                    // (FUN_10171d80). When block_decision_pending_ is true, evaluate
                    // TacticContext and call TacticSettings::choose() with candidates
                    // including "Duck" (block). If "Duck" wins, activate block state.
                    // BlockChance factors from tacticSettings.xml:
                    //   Base=0 CounterFactor=0.05 HitFactor=0.15
                    //   DamageFactor=0.5 AnimationFramesFactor=0.005 Limit=1
                    if (block_decision_pending_) {
                        block_decision_pending_ = false;
                        bool can_block = (move_state_ == 0 && hit_anim_ == 0 &&
                                          !start_stance_playing_ && !player_fighter_.is_blocking);
                        if (can_block) {
                            const TacticDef* td = tactics_.tactic("Standard");
                            if (!td) td = tactics_.tactic("NoTables");
                            if (td) {
                                // Build TacticContext from current fight state
                                float dist = std::fabs(enemy_pos_x_ - player_pos_x_);
                                float player_max_hp = player_fighter_.max_health;
                                TacticContext ctx;
                                ctx.distance = dist;
                                ctx.health = (player_max_hp > 0)
                                    ? player_fighter_.health / player_max_hp : 1.0f;
                                ctx.enemy_health = (player_max_hp > 0)
                                    ? enemy_fighter_.health / player_max_hp : 1.0f;
                                ctx.damage = recent_damage_taken_;
                                ctx.hits = (float)enemy_hits_on_player_;
                                // anim_frames: frames remaining in current animation
                                ctx.anim_frames = (anim_fps_ > 0)
                                    ? (hit_anim_ * anim_fps_ / 1000.0f) : 0.0f;

                                // Roulette: include "Duck" (block) as a candidate
                                static const std::vector<std::string> kBlockCandidates = {
                                    "Duck", "ShortAttack", "ForwardStep"
                                };
                                int pick = tactics_.choose(*td, kBlockCandidates, ctx);
                                // pick == 0 means "Duck" won the roulette
                                if (pick == 0) {
                                    // Activate block state
                                    player_fighter_.is_blocking = true;
                                    move_state_ = 11;  // block state
                                    if (assets_->animations().count("high_block")) {
                                        play_animation("high_block", false, 1);
                                        current_move_ = "HighBlock";
                                        int fc = assets_->animations()["high_block"].frame_count;
                                        hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                                    }
                                    std::printf("[BLOCK] Roulette chose Duck — block activated (dist=%.0f dmg=%.2f hits=%d)\n",
                                                dist, recent_damage_taken_, enemy_hits_on_player_);
                                } else {
                                    std::printf("[BLOCK] Roulette did NOT choose Duck (pick=%d)\n", pick);
                                }
                            }
                        }
                        // Reset timer: 0.6-1.0s random cooldown
                        block_decision_cooldown_ = 0.6f + (float)(std::rand() % 400) / 1000.0f;
                    }
                    
                    if (player_fighter_.is_blocking) {
                        play_sound("armor", 0.3f);
                        // [ORIGINAL] Block applies base_block_factor (50% reduction, not 100%)
                        // From binary @ 0x101598c0, parsed from internalSettings.xml
                        const auto& dmg_settings = assets_->damage_settings();
                        float base_damage = dmg_settings.average_base_damage;
                        auto hp_it = assets_->moves().find("HighPunch");
                        if (hp_it != assets_->moves().end() && hp_it->second.damage > 0.0f)
                            base_damage = hp_it->second.damage;
                        
                        // [ORIGINAL] Damage via Model::getTotalDamage @ game+0x4527B4
                        // (shared helper above); block is a call-site post-multiplier
                        // (base_block_factor = 0.5) — the formula has no block term
                        // (tracer @ game+0x438530 prints Block in the hit processor).
                        const float block_factor = dmg_settings.base_block_factor;
                        const float final_damage = enemy_damage_to_player(
                            base_damage, /*blocked=*/true,
                            enemy_fighter_.attributes, player_fighter_.attributes,
                            dmg_settings);
                        
                        std::printf("[COMBAT] Enemy hit player (BLOCKED): base=%.3f block=%.2f final=%.3f\n",
                                    base_damage, block_factor, final_damage);

                        // [ORIGINAL] Magic charge on every landed hit
                        // (Fight::applyHit @ 0x8F420F9C): BOTH fighters
                        // charge; the gate is the ENEMY's attacking move
                        // NoMagicRecharge flag (MoveDef+0x148).
                        {
                            const MoveDef* enemy_atk =
                                move_def_by_anim(assets_->moves(), enemy_anim_);
                            const bool no_magic_recharge =
                                enemy_atk && enemy_atk->no_magic_recharge;
                            if (!no_magic_recharge) {
                                add_magic_charge(enemy_fighter_, base_damage,
                                                 /*blocked=*/true, false,
                                                 0, total_frame_count_, "enemy");
                                add_magic_charge(player_fighter_, base_damage,
                                                 /*blocked=*/true, false,
                                                 1, total_frame_count_, "player");
                            }
                        }
                        
                        player_fighter_.health -= final_damage * player_fighter_.max_health;
                        player_fighter_.invuln_time = 0.4f;
                        player_fighter_.hit_stun_time = 0.15f;  // Reduced stun when blocking
                        player_hit_flash_ = 0.2f;
                        spawn_hit_sparks(player_pos_x_, player_pos_y_ - 40, 4);
                        if (player_fighter_.health <= 0.0f) {
                            player_fighter_.health = 0.0f;
                            player_fighter_.is_dead = true;
                        }
                    } else {
                        // [ORIGINAL] Damage via Model::getTotalDamage @ game+0x4527B4
                        // (shared helper above); not blocking -> no post-multiplier.
                        const auto& dmg_settings = assets_->damage_settings();
                        float base_damage = dmg_settings.average_base_damage;
                        auto hp_it = assets_->moves().find("HighPunch");
                        if (hp_it != assets_->moves().end() && hp_it->second.damage > 0.0f)
                            base_damage = hp_it->second.damage;
                        
                        // [Wave 11A M4] CRIT roll for the ENEMY's strike —
                        // same law as the player side (FUN_8f4aa998): skipped
                        // when blocked or the enemy's move carries
                        // NoCritical. The disciple's CriticalChance=0, so
                        // crits never fire against the player in the MVP.
                        const MoveDef* enemy_atk_move =
                            move_def_by_anim(assets_->moves(), enemy_anim_);
                        bool enemy_crit = false;
                        if (!(enemy_atk_move && enemy_atk_move->no_critical)) {
                            const float chance =
                                enemy_fighter_.attributes.get_or("CriticalChance", 0.0f) *
                                dmg_settings.crit_probability_base;
                            const float roll =
                                (float)(std::rand() % 10000) / 10000.0f;
                            enemy_crit = roll < chance;
                        }
                        const float enemy_crit_mult = enemy_crit
                            ? std::pow(2.0f,
                                       dmg_settings.crit_damage_base *
                                           enemy_fighter_.attributes.get_or(
                                               "CriticalDamage", 0.0f))
                            : 1.0f;
                        
                        const float final_damage = enemy_damage_to_player(
                            base_damage, /*blocked=*/false,
                            enemy_fighter_.attributes, player_fighter_.attributes,
                            dmg_settings) * enemy_crit_mult;
                        
                        std::printf("[COMBAT] Enemy hit player: base=%.3f crit=%d mult=%.4f final=%.3f\n",
                                    base_damage, (int)enemy_crit,
                                    enemy_crit_mult, final_damage);

                        // [ORIGINAL] Magic charge on every landed hit
                        // (Fight::applyHit @ 0x8F420F9C): BOTH fighters
                        // charge; the gate is the ENEMY's attacking move
                        // NoMagicRecharge flag (MoveDef+0x148).
                        {
                            const MoveDef* enemy_atk =
                                move_def_by_anim(assets_->moves(), enemy_anim_);
                            const bool no_magic_recharge =
                                enemy_atk && enemy_atk->no_magic_recharge;
                            if (!no_magic_recharge) {
                                add_magic_charge(enemy_fighter_, base_damage,
                                                 /*blocked=*/false, false,
                                                 0, total_frame_count_, "enemy");
                                add_magic_charge(player_fighter_, base_damage,
                                                 /*blocked=*/false, false,
                                                 1, total_frame_count_, "player");
                            }
                        }
                        
                        player_fighter_.health -= final_damage * player_fighter_.max_health;
                        player_fighter_.invuln_time = 0.4f;
                        player_fighter_.hit_stun_time = 0.25f;
                        player_hit_flash_ = 0.2f;
                        // Track recent damage and hits for block decision context
                        recent_damage_taken_ += final_damage;
                        enemy_hits_on_player_++;
                        enemy_fighter_.hits_landed++;
                        spawn_hit_sparks(player_pos_x_, player_pos_y_ - 40, 8);
                        // [S1] The hurt player's voice set: m_pl_hit* for a
                        // male player (usersDefault.xml default), f_pl_hit*
                        // for a female one.
                        play_sound(player_hit_sound(
                                       enemy_fighter_.hits_landed % 3 + 1),
                                   0.6f);
                        // [STEP 4.7] Trigger knockback on heavy hits (damage > 30% max health)
                        if (final_damage > 0.3f) {
                            trigger_knockback(400.0f, true);  // launch up + knockdown
                        }
                        if (player_fighter_.health <= 0.0f) {
                            player_fighter_.health = 0.0f;
                            player_fighter_.is_dead = true;
                        }
                    }
                }
            }
            if (enemy_attack_duration_ <= 0) enemy_attacking_ = false;
        }
        }  // [A1] end of intro gate — the stun/decide/execute chain
        enemy_anim_time_ += dt_sec;
        // [Wave 10A defect 1] KNOCKBACK OVERFLOW: the enemy's world x is
        // moved by two unbounded paths — the reversed <Impulse X> integrated
        // over the whole reaction duration (knockback) and the AI step
        // executor's approach/retreat — so a chased, repeatedly-hit enemy
        // can fly out of the location's world box (params.xml Width/2; the
        // bag had the same box from the location data). The location wall
        //         is a hard boundary: the enemy may slide INTO it but never past it.
        // Face the player
        enemy_facing_right_ = (player_pos_x_ > enemy_pos_x_);
    }

    // [Wave 10A defect 1 / Wave 10B D2] LOCATION BOUNDS FOR BOTH FIGHTERS:
    // the enemy's world x is moved by two unbounded paths - the reversed
    // <Impulse X> integrated over the whole reaction duration (knockback)
    // and the AI step executor's approach/retreat - so a chased,
    // repeatedly-hit enemy can fly out of the location's world box
    // (params.xml Width/2; the bag had the same box from the location
    // data). The location wall is a hard boundary: the fighter may slide
    // INTO it but never past it. The clamp runs on EVERY gameplay frame
    // for BOTH fighters (the player side sits in its own update below),
    // outside the show_enemy_ gate so the dojo bag anchor and the battle
    // enemy are covered alike ("locations have no bounds you cannot
    // cross").
    // [Wave 11B W2] The bounds are the location's WALL OBJECTS
    // (params.xml <Image ClassName="left"/"right"> X - the original's
    // physics walls, dojo +-680), applied equally to both fighters -
    // not the width/2 clamp the engine invented (VERIFY_W11 3: the
    // binary has no width/2 boundary; the wall geometry comes from the
    // left/right anchors + Wall attribute).
    if (location_) {
        float wall_lo = 0.0f, wall_hi = 0.0f;
        location_wall_bounds(location_, wall_lo, wall_hi);
        enemy_pos_x_ = std::clamp(enemy_pos_x_, wall_lo, wall_hi);
    }

    // R: restart battle (after victory/defeat)
    if (input.keys_just_pressed[(size_t)plat::Key::R]) {
        if (player_fighter_.is_dead || enemy_fighter_.is_dead) {
            player_fighter_ = FighterState{};
            enemy_fighter_ = FighterState{};
            rebuild_fighter_attributes();  // FighterState{} wipes the maps
            battle_result_.clear();
            player_hit_flash_ = 0;
            enemy_hit_flash_ = 0;
            combo_timer_ = 0;
            hit_sparks_.clear();
            enemy_attack_cooldown_ = 0;
            enemy_attacking_ = false;
            // Reset positions
            if (location_) {
                enemy_pos_x_ = location_->enemy_x - 983.0f;
            }
            std::printf("[COMBAT] Battle restarted\n");
        }
    }
    // Toggle between the punching bag and a sparring partner.
    //
    // [ORIGINAL] In the original this is an on-screen button (FUN_1014d5c0):
    // its art flips between btn_disciple and btn_punching_bag to show what
    // you switch TO, at (logical_width - 85, -75) points of its parent. The
    // click below and the B key both land here; B stays as a shortcut.
    bool toggle_partner = input.keys_just_pressed[(size_t)plat::Key::B];
    if (!is_battle_mode_) {
        const auto r = disciple_btn_rect();
        for (const auto& p : input.pointers) {
            if (p.id < 0 || !p.just_pressed) continue;
            if (p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h)
                toggle_partner = true;
        }
    }
    if (toggle_partner) {
        show_enemy_ = !show_enemy_;
        // Put the partner back to a clean stance when he appears, and clear
        // any state he was left in when he goes away. Without this a partner
        // dismissed mid-exchange kept his hit stun, cooldown and attack flag,
        // and resumed from them the next time he was called back.
        enemy_fighter_ = FighterState{};
        rebuild_fighter_attributes();  // FighterState{} wipes the maps
        enemy_attack_cooldown_ = 0.0f;
        enemy_attacking_ = false;
        enemy_attack_duration_ = 0.0f;
        enemy_hit_flash_ = 0.0f;
        // [H07] Real catalog stance idle (not the invented fists_idle).
        enemy_anim_ = enemy_idle_anim();
        if (location_) enemy_pos_x_ = location_->enemy_x - 983.0f;
        std::printf("[DOJO] Switched to %s\n", show_enemy_ ? "enemy fighter" : "punching bag");
        debug_log("[DOJO] Switched to %s\n", show_enemy_ ? "enemy" : "bag");
    }

    // Weapon cycling: J = next weapon, U = previous weapon
    if (input.keys_just_pressed[(size_t)plat::Key::J]) {
        cycle_weapon(1);
        // Log available weapon-specific moves for the new weapon
        int move_count = 0;
        for (auto& [mn, mv] : assets_->moves()) {
            if (is_weapon_allowed(mv)) move_count++;
        }
        std::printf("[WEAPON] %d available moves for %s\n", move_count, equipped_weapon_.c_str());
    }
    if (input.keys_just_pressed[(size_t)plat::Key::U]) {
        cycle_weapon(-1);
    }

    // Esc: close overlay if open, else request quit (handled by scene)
    if (input.keys_just_pressed[(size_t)plat::Key::Escape]) {
        if (overlay_ != Overlay::None) overlay_ = Overlay::None;
        else quit_requested_ = true;
    }
    // M: toggle menu
    if (input.keys_just_pressed[(size_t)plat::Key::M]) {
        overlay_ = (overlay_ == Overlay::Menu) ? Overlay::None : Overlay::Menu;
    }
    // T: toggle dialog
    if (input.keys_just_pressed[(size_t)plat::Key::T]) {
        overlay_ = (overlay_ == Overlay::Dialog) ? Overlay::None : Overlay::Dialog;
    }

    // Click: check menu button (left side). The hit box IS the drawn scroll —
    // menu_roll_rect() is the single source for both. It used to be a fourth
    // independent set of constants (130 px wide against a bar whose width
    // depends on the localized label), so the click target and the graphic
    // only lined up by accident.
    const MenuRollRect roll = menu_roll_rect();
    for (const auto& p : input.pointers) {
        if (p.just_pressed) {
            if (p.x >= roll.x && p.x <= roll.x + roll.w &&
                p.y >= roll.y && p.y <= roll.y + roll.h) {
                overlay_ = (overlay_ == Overlay::Menu) ? Overlay::None : Overlay::Menu;
            }
        }
    }

    // Animate menu expand/collapse (300ms transition)
    float target_progress = (overlay_ == Overlay::Menu) ? 1.0f : 0.0f;
    // [U6] Same law as host_render_menu_overlay: dt_ms / 300 per frame. The
    // old divisor (1000/300 = 3.33) made the progress jump 4.8 per frame and
    // the menu appear without any unfold.
    float anim_speed = 300.0f;
    if (menu_anim_progress_ < target_progress) {
        menu_anim_progress_ += (float)dt / anim_speed;
        if (menu_anim_progress_ > target_progress) menu_anim_progress_ = target_progress;
    } else if (menu_anim_progress_ > target_progress) {
        menu_anim_progress_ -= (float)dt / anim_speed;
        if (menu_anim_progress_ < target_progress) menu_anim_progress_ = target_progress;
    }

    // === INPUT: original SF2 controls ===
    // W=up, A=left, S=down, D=right (absolute directions)
    // O=punch, P=kick
    // Direction keys are interpreted RELATIVE to facing:
    //   If facing right: D=Forward, A=Back
    //   If facing left:  A=Forward, D=Back
    bool key_up = input.keys_down[(size_t)plat::Key::W] ||
                  input.keys_down[(size_t)plat::Key::ArrowUp];
    bool key_down = input.keys_down[(size_t)plat::Key::S] ||
                    input.keys_down[(size_t)plat::Key::ArrowDown];
    bool key_left = input.keys_down[(size_t)plat::Key::A] ||
                    input.keys_down[(size_t)plat::Key::ArrowLeft];
    bool key_right = input.keys_down[(size_t)plat::Key::D] ||
                     input.keys_down[(size_t)plat::Key::ArrowRight];

    // [ORIGINAL] The on-screen stick is the original's only direction input.
    // It is folded into the same four booleans the keyboard produces, so the
    // combat state machine below has ONE input path rather than two — the
    // duplication that has hidden a bug five times in this codebase already.
    // The quantisation (dead zone and sector widths) is the original's, read
    // out of internalSettings.xml — see update_touch_controls.
    update_touch_controls(input);
    if (touch_.left) key_left = true;
    if (touch_.right) key_right = true;
    if (touch_.up) key_up = true;
    if (touch_.down) key_down = true;

    // === DYNAMIC FACING (deferred turn) ===
    // [M5] The original captures facing when a NEW animation starts
    // (ModelAnimation::playInfo sign param, Model+0x54) and holds it for the
    // move; it does not re-evaluate every frame. Per-frame re-evaluation made
    // the fighter snap around the instant a move ended behind the opponent
    // (SOAK_TRIAGE.md M5). The desired (opponent-facing) direction is still
    // tracked every frame, but it is APPLIED only at a fresh movement input,
    // so the turn happens when the player asks for it. Facing stays locked
    // during root-motion moves so their locomotion direction is stable; the
    // render mirror sweeps (player_turn_blend_) instead of snapping.
    // Runs after the touch fold so keyboard and stick count alike.
    bool facing_locked = hit_anim_ > 0 ||
        current_anim_ == "forward_roll" || current_anim_ == "back_roll" ||
        current_anim_ == "jump" || current_anim_ == "jump_away" ||
        current_anim_ == "front_flip" || current_anim_ == "back_flip" ||
        current_anim_ == "back_handflip";
    const bool any_dir_input = key_left || key_right || key_up || key_down;
    if (location_ && !facing_locked) {
        // [Wave 11A M2] FACING TARGET: in dojo mode the target is the
        // (static) bag; in battle mode it is the LIVING enemy. The old code
        // aimed at the constant bag spawn even in battle, so after the enemy
        // was knocked or walked away the fighter turned to face the empty
        // spawn point instead of the enemy ("turns away from the enemy when
        // it should not").
        const float target_x = is_battle_mode_
            ? enemy_pos_x_
            : (location_->enemy_x - 983.0f);
        const bool should_face_right = (target_x >= player_pos_x_);
        const float dist_to_enemy = std::fabs(target_x - player_pos_x_);
        if (dist_to_enemy > 30.0f) {
            desired_facing_right_ = should_face_right;
        }
    }
    // [Wave 11A M2] FACING LAW (SPEC_COMBAT_CORE Q2, VERIFY_W11 Q2 GREEN):
    // the mirror changes ONLY via SetDirection at MOVE START — the
    // Controlled template (all 1key/2key/3key attacks, jumps, rolls, duck,
    // magic cast) turns the fighter to face the enemy; StepForward/StepBack
    // carry NO SetDirection (walking, forward or back, keeps facing); Hit
    // reactions (Impulse Reverse=1) and GetUp never flip the mirror. The
    // desired direction above is APPLIED at the controlled move starts in
    // the selection blocks below, NEVER on direction input — the old
    // apply-on-fresh-input turned the fighter on a back-walk press, which is
    // exactly the reported "turns away from the enemy when it should not".
    // [HEURISTIC-TODO] the idle-stance re-face (SetDirectionIdleStance /
    // SetDirectionTransition templates) is not applied here — the next
    // controlled move start re-faces; a hit reaction never does.
    // [M5] Smooth turn: ease the render mirror toward the facing sign so a
    // turn reads as a short rotation (~8 frames), not a one-frame snap.
    {
        const float blend_target = facing_right_ ? 1.0f : -1.0f;
        player_turn_blend_ += (blend_target - player_turn_blend_) * 0.25f;
        if (std::fabs(player_turn_blend_ - blend_target) < 0.02f)
            player_turn_blend_ = blend_target;
    }

    // [ORIGINAL] The intro scroll asks the player to move; it goes away the
    // moment they have taken a few steps, and does not come back.
    // [Q1] The quest movement stage must NOT complete on a single press:
    // one step is ~50 units, and the old displacement>25 check dismissed
    // the hint mid-stride on the FIRST d press. The original requires a
    // few steps (forward/back); the stage completes after
    // kTutorialMoveSteps step events (each press that starts a step counts,
    // including direction changes mid-walk).
    // [HEURISTIC-TODO] The original's exact step count is not
    // binary-verified; 4 steps is the behavioural reading.
    // Grace period: require 30 frames (~0.5s) before dismissal to prevent
    // accidental trigger from keys held during the preceding dialog scene.
    if (!intro_hint_dismissed_ && overlay_ == Overlay::Dialog &&
        overlay_show_frames_ > 30) {
        if (tutorial_move_steps_ >= kTutorialMoveSteps) {
            intro_hint_dismissed_ = true;
            overlay_ = Overlay::None;
            std::printf("[tutorial] hint scroll dismissed after %d step events\n",
                        tutorial_move_steps_);
            // [ORIGINAL] After the hint scroll is dismissed, trigger the next
            // tutorial step (punchbag dialog). The original quest system chains
            // these automatically via quest events.
            if (tutorial_state_ == "BAG") {
                check_tutorial();  // queues the punchbag dialog line
                if (!dialogue_lines_.empty()) {
                    request_scene_transition(scene::SceneId::Dialogue);
                    std::printf("[tutorial] transitioning to Dialogue for punchbag hint\n");
                }
            }
        }
    }

    // Convert absolute directions to relative (Forward/Back)
    // [ORIGINAL] From Model::step pipeline (0x10161ad0) — walking is animation-driven
    // via movement entries (FUN_1015eeb0). This constant is for AI movement and
    // fallback positioning when animation root motion is not available.
    // [HEURISTIC-TODO] Exact value needs tracing from binary's movement entries.
    // Starting point: 150.0f (faster than enemy's 90.0f since player has anim boost).
    static constexpr float kWalkSpeed = 150.0f;  // units/sec
    (void)kWalkSpeed;  // suppress unused warning until AI uses it

    bool key_forward = facing_right_ ? key_right : key_left;
    bool key_back = facing_right_ ? key_left : key_right;

    // Cache input state for the F1 debug overlay
    dbg_key_forward_ = key_forward;
    dbg_key_back_ = key_back;
    dbg_key_up_ = key_up;
    dbg_key_down_ = key_down;

    // [ORIGINAL] Double-tap detection for DoubleStep/BackHandflip.
    // Direction is RELATIVE to facing: D=Forward when facing right,
    // A=Forward when facing left. This matches original SF2 where
    // double-tap direction depends on character orientation.
    bool fwd_just_pressed = facing_right_ ?
        (input.keys_just_pressed[(size_t)plat::Key::D] ||
         input.keys_just_pressed[(size_t)plat::Key::ArrowRight]) :
        (input.keys_just_pressed[(size_t)plat::Key::A] ||
         input.keys_just_pressed[(size_t)plat::Key::ArrowLeft]);
    bool back_just_pressed = facing_right_ ?
        (input.keys_just_pressed[(size_t)plat::Key::A] ||
         input.keys_just_pressed[(size_t)plat::Key::ArrowLeft]) :
        (input.keys_just_pressed[(size_t)plat::Key::D] ||
         input.keys_just_pressed[(size_t)plat::Key::ArrowRight]);
    uint32_t now_ms = (uint32_t)(total_frame_count_ * dt);
    // [ORIGINAL] Double-tap: use move_state_ instead of current_anim_ name.
    // move_state_ == 2 = MOVING_FORWARD, == 1 = MOVING_BACK. This works
    // regardless of which step animation is playing (step_forward vs
    // weapon-specific variants like composite_sword_step_forward).
    // [ORIGINAL] Window = kDoubleTapWindowMs (300 ms) from Model::step @ 0x10161ad0.
    if (fwd_just_pressed) {
        if (now_ms - input_handler_.last_fwd_tap_ms() < InputHandler::kDoubleTapWindowMs && move_state_ == 2) {
            input_handler_.set_double_step_fwd_requested(true);
        }
        input_handler_.set_last_fwd_tap_ms(now_ms);
    }
    if (back_just_pressed) {
        if (now_ms - input_handler_.last_back_tap_ms() < InputHandler::kDoubleTapWindowMs && move_state_ == 1) {
            input_handler_.set_double_step_back_requested(true);
        }
        input_handler_.set_last_back_tap_ms(now_ms);
    }

    bool punch_pressed = input.keys_just_pressed[(size_t)plat::Key::O] ||
                         input.keys_just_pressed[(size_t)plat::Key::L];  // [ORIGINAL] JS uses L=punch
    bool kick_pressed = input.keys_just_pressed[(size_t)plat::Key::P];
    // Also keep Space/K as fallback for testing
    if (input.keys_just_pressed[(size_t)plat::Key::Space]) punch_pressed = true;
    if (input.keys_just_pressed[(size_t)plat::Key::K]) kick_pressed = true;
    // Cache for F1 debug overlay
    dbg_punch_pressed_ = punch_pressed;
    dbg_kick_pressed_ = kick_pressed;
    // Same folding as the stick above: the on-screen buttons feed the very
    // same booleans, so nothing downstream needs to know where a punch came
    // from.
    if (touch_.punch_pressed) punch_pressed = true;
    if (touch_.kick_pressed) kick_pressed = true;

    // [INPUT] Debug log for input state each frame
    debug_log("[INPUT] fwd=%d back=%d up=%d down=%d punch=%d kick=%d move_state=%d facing=%d\n",
              (int)key_forward, (int)key_back, (int)key_up, (int)key_down,
              (int)punch_pressed, (int)kick_pressed, move_state_,
              facing_right_ ? 1 : 0);

    // Debug: log key state and what blocks input
    if (punch_pressed || kick_pressed) {
        std::printf("[KEY] %s%s pressed — hit_anim=%u is_uninterrupt=%d move_state=%d current_move='%s'\n",
                    punch_pressed ? "O" : "", kick_pressed ? "P" : "",
                    hit_anim_, is_uninterrupt_ ? 1 : 0, move_state_, current_move_.c_str());
        debug_log("[KEY] f=%llu %s%s pressed hit_anim=%u unint=%d ms=%d move='%s' anim='%s' up=%d down=%d fwd=%d back=%d\n",
            (unsigned long long)total_frame_count_,
            punch_pressed ? "O" : "", kick_pressed ? "P" : "",
            hit_anim_, is_uninterrupt_ ? 1 : 0, move_state_, current_move_.c_str(),
            current_anim_.c_str(), (int)key_up, (int)key_down, (int)key_forward, (int)key_back);
    }
    // Note: Removed sticky key buffer — it caused unwanted repeat attacks.
    // GetAsyncKeyState is reliable; the original issue was elsewhere.

    // [ORIGINAL] A step is interruptible the moment the key is released.
    //
    // moves.xml decides this per move, and `StepForward` / `StepBack` declare
    // NO intervals at all — no Uninterrupt, no SemiUninterrupt, nothing. Only
    // `DoubleStepForward` (the dash) locks itself down:
    //     <Interval Name="SemiUninterrupt" End="2"/>
    //     <Interval Name="Uninterrupt" Start="3" End="9"/>
    //     <Interval Name="SelfUninterrupt" Start="10" End="12"/>
    //
    // There used to be an invented 400 ms minimum here, so a plain step kept
    // walking for up to four tenths of a second after the key came up and
    // could not be reversed inside that window. That is the largest single
    // reason the controls felt sticky. The dash is still protected, by
    // move_state_ 10 plus is_uninterrupt_, which read the real intervals.
    // [ORIGINAL] Step frame counting — original uses animation frame count,
    // not wall-clock time, to gate when a step can be interrupted.
    // From Model::step pipeline (0x10161ad0): animation must play N frames
    // before another input is accepted. The old 400ms threshold was invented
    // and caused sticky controls.
    if (move_state_ == 1 || move_state_ == 2) {
        input_handler_.increment_step_frames();
    } else {
        input_handler_.reset_step_frames();
    }
    // [ORIGINAL] Step interruptibility: use actual interval data from the
    // current move when available, fall back to kMinStepFrames otherwise.
    //
    // StepForward / StepBack declare NO intervals — they are interruptible
    // the moment the key is released (subject to kMinStepFrames as a minimum).
    // DoubleStepForward declares SemiUninterrupt End=2, Uninterrupt 3..9,
    // SelfUninterrupt 10..12 — the dash cannot be interrupted until those
    // frames have passed.
    //
    // Binary: Model animation pipeline @ 0x101650FC (playInfo) checks
    // interruptibility each frame against the active interval.
    uint32_t step_min_frames = InputHandler::kMinStepFrames;
    if (!current_move_.empty()) {
        auto mv_it = assets_->moves().find(current_move_);
        if (mv_it != assets_->moves().end()) {
            const auto& mv = mv_it->second;
            int interval_end = 0;
            if (mv.semi_uninterrupt_end > 0)
                interval_end = std::max(interval_end, mv.semi_uninterrupt_end);
            if (mv.uninterrupt_end > 0)
                interval_end = std::max(interval_end, mv.uninterrupt_end);
            if (mv.self_uninterrupt_end > 0)
                interval_end = std::max(interval_end, mv.self_uninterrupt_end);
            if (interval_end > 0)
                step_min_frames = static_cast<uint32_t>(interval_end);
        }
    }
    const bool step_min_played = input_handler_.step_frames() >= step_min_frames;

    // [ORIGINAL] Direction is read per frame, with no latch.
    //
    // The original binary (Model::step 0x10161ad0) has no direction latch;
    // combos are gated by CurrentAnimation conditions from moves.xml, not on
    // key history. The old fwd_held_ms_/back_held_ms_ latch was invented and
    // caused sticky controls — measured on the scripted trace, key up at
    // frame 230 and the walk ending at 243 (13 frames of phantom input).
    // Now fwd_latched/back_latched below just read key_forward/key_back
    // directly — no latch, no history, pure per-frame state.

    // === DYNAMIC MOVE SELECTION (from moves.xml) ===
    // The engine reads ALL moves from moves.xml at load time, including
    // their Template strings (key_count, direction, move_type, etc.).
    // Here we match the current key state against loaded moves and
    // select the one with the highest priority.
    //
    // Key state:
    //   key_forward (D relative to facing) → Forward
    //   key_back (A relative to facing) → Back
    //   key_up (W) → Up
    //   key_down (S) → Down
    //   Combinations: Up+Forward=UpForward, Down+Back=DownBack, etc.
    //   punch_pressed (O) → Punch
    //   kick_pressed (P) → Kick
    //
    // Move type detection (from moves.xml templates):
    //   1key: Just tap Punch/Kick (no direction hold)
    //   2key: Hold direction + tap Punch/Kick (e.g. Sweep = S+P)
    //   3key: Combos — triggered when CURRENT animation is from a 1key
    //         or 2key basic attack. This is the "second tap" combo.
    //         Example: HeavyPunch (1key) → tap Punch again → DoublePunch (3key)
    //
    // Selection: find ALL moves matching (direction, move_type, key_count,
    // is_unarmed, TacticWeapon=Fists or empty). Pick highest priority.
    // For moves with <Distance> condition, only match if enemy is in range.

    // [ORIGINAL] PC source: sf2.js tKa() — player input is NOT gated by
    // Uninterrupt (which is AI-only, ocb/pcb at line 18770).
    // BUT: 1key/2key attacks CANNOT interrupt another 1key/2key attack.
    // Only 3key combos (which require CurrentAnimation=<specific attack>
    // in moves.xml) can interrupt. This is the REAL cancel window:
    //   - During attack animation (hit_anim_ > 0, current_anim_ is attack):
    //     → Only 3key combos allowed (in_basic_attack = true)
    //   - Outside attack (hit_anim_ == 0 or current_anim_ is idle/step):
//     → 1key/2key attacks allowed
    // Previous fix (5f392b0) removed is_uninterrupt_ entirely, allowing
    // ANY attack to interrupt ANY animation - regression.
    // This gate replaces the old is_uninterrupt_ check with the correct
    // logic: in_basic_attack blocks 1key/2key, allows only 3key.
    // [ORIGINAL] PC source: Pqb() (line 18769-18810) - move selection
    // During attack animation (elapsed < total_len):
    //   If in Uninterrupt interval: allow 3key chain attacks (YAa, Gea)
    //   Else: return 0 (NO moves available - not even movement)
    // After attack ends: normal move selection (1key, 2key, movement)
    //
    // In our code: move_state_ == 10 means in attack/special.
    // hit_anim_ > 0 means attack animation playing.
    // current_move_ tracks the move name.
    // is_uninterrupt_ is true only during attack animation's Uninterrupt interval.
    // [ORIGINAL] StartStance is UNINTERRUPTIBLE: the original SF2 uses a
    // battle phase state machine (Je=1 StartStance, Je=2 Fight, Je=3 EndStance).
    // During StartStance phase, player input is BLOCKED — the intro animation
    // must play to completion. See sf2_beautified.js:23843 (Em.he condition).
    // We set start_stance_playing_ = true when the stance starts, and clear it
    // when the animation finishes. While true, ALL combat/movement input is
    // skipped (the player can't punch/kick/move during the intro).
    // [A6] Start-stance hold exit (SOAK_TRIAGE.md A6): once the intro
    // animation has completed (hit_anim_ == 0) the held stance pose ends on
    // the player's first input — any direction key or punch/kick — and the
    // fight begins (round stage Je=1 StartStance -> Je=2 Fight). The
    // original holds the stance until the player interacts; a hit would
    // break it too, but the enemy AI is gated during the stance (A1), so
    // only input is reachable today. Runs before the in_attack gate so the
    // same frame's input is processed normally.
    if (start_stance_playing_ && hit_anim_ == 0 &&
        (key_forward || key_back || key_up || key_down ||
         punch_pressed || kick_pressed)) {
        start_stance_playing_ = false;
    }
    bool in_attack = (move_state_ == 10 && current_move_ != "StartStance" && hit_anim_ > 0);
    if (start_stance_playing_) {
        // Block all input during StartStance intro (original behavior)
        in_attack = true;
        is_uninterrupt_ = false;  // not in uninterrupt — just blocked
    }
    {
    std::string cur_direction;
    std::string cur_move_type;
    float dist_to_enemy;

    if (punch_pressed || kick_pressed) {
        // Determine direction from key state
        cur_direction = "Central";  // default: no direction
        if (key_up && key_forward) cur_direction = "UpForward";
        else if (key_up && key_back) cur_direction = "UpBack";
        else if (key_down && key_forward) cur_direction = "DownForward";
        else if (key_down && key_back) cur_direction = "DownBack";
        else if (key_up) cur_direction = "Up";
        else if (key_down) cur_direction = "Down";
        else if (key_forward) cur_direction = "Forward";
        else if (key_back) cur_direction = "Back";

        // Determine move type
        if (punch_pressed) cur_move_type = "Punch";
        else if (kick_pressed) cur_move_type = "Kick";

        // Compute distance to enemy (bag) for distance-based move selection
        dist_to_enemy = 1000.0f;  // default: far
        if (location_ && assets_->bag_model()) {

        // [ORIGINAL] Combat interrupt logic (from sf2.js Pqb line 18769):
        // - During attack animation (hit_anim_ > 0):
        //   - If in Uninterrupt interval: allow 3key chain combos only
        //   - If PAST attack interval (attack done, in recovery): allow
        //     1key/2key attacks to interrupt (cancel recovery into new attack)
        //   - Otherwise (in attack interval, not uninterrupt): block all
        // - Outside attack: normal 1key/2key selection
        //
        // This fixes the "delay between attacks" — after the attack interval
        // completes (hit registered), the player can immediately start a new
        // attack without waiting for the full animation to finish.
        bool past_attack_interval = false;
        if (in_attack && !current_move_.empty()) {
            auto cur_move_it = assets_->moves().find(current_move_);
            if (cur_move_it != assets_->moves().end() && cur_move_it->second.attack_end > 0) {
                int current_frame = (int)(anim_time_ * anim_fps_);
                if (current_frame >= cur_move_it->second.attack_end) {
                    past_attack_interval = true;
                }
            }
        }
        bool block_all_combat = in_attack && !is_uninterrupt_ && !past_attack_interval;
        const MoveDef* best_move = nullptr;
        int candidate_count = 0;
        // [DIAGNOSTIC] Why a move was rejected, per filter. "cand=0" on its own
        // says nothing: the whole table can be emptied by any one of a dozen
        // conditions and the log looked identical every time. With these
        // counters the reason is on the same line as the symptom.
        struct Reject {
            int no_file = 0, titan = 0, move_type = 0, blocked = 0, key_count = 0;
            int direction = 0, weapon = 0, subtype = 0, cur_anim = 0, self = 0, no_anim = 0;
        } rej;
        for (auto& [name, move] : assets_->moves()) {
            if (move.filename.empty() || move.template_name.empty()) { ++rej.no_file; continue; }
            // Skip Titan moves
            {
                size_t titan_pos = move.template_name.find("Titan");
                if (titan_pos != std::string::npos) {
                    if (titan_pos < 3 || move.template_name.substr(titan_pos - 3, 3) != "Not") {
                        ++rej.titan; continue;
                    }
                }
            }
            // Weapon-specific moves may have empty move_type (template "1key|Central|Weapon").
            // Allow them if they have matching tactic_weapon and no move_type set.
            // [P7] The allowance must ALSO require Type="ATTACK": movement/defensive
            // moves (Duck, ForwardRoll — Type="MOVE", templates like
            // "1key|Down|NotTitan") carry an empty move_type too, and the
            // attack selector was handing a PUNCH press to Duck whenever the
            // real down-punch was unavailable (self-chain or weapon lock) —
            // the soak's "[MOVE] Punch dir=Down -> Duck" input theft.
            // [Wave 10B soak D7] And it must require the press to be a PUNCH:
            // weapon moves are punch-template attacks (template
            // "2key|Central|Weapon"), and moves.xml has no weapon KICK
            // templates at all — a kick press must fall back to the unarmed
            // kicks (HighKick/FrontKick/BackKick per direction, all
            // move_type="Kick", no TacticWeapon). Before this gate, ONE P
            // with knives selected KnivesDoubleSlash (2key|Central|Weapon,
            // prio 115) over HighKick (prio 110): "когда нажал p один раз,
            // ножи атакуют атакой, как на два нажатия o".
            bool move_type_match = (move.move_type == cur_move_type) ||
                (cur_move_type == "Punch" && move.is_attack &&
                 move.move_type.empty() && is_weapon_allowed(move) &&
                 move.key_count <= 2);
            if (!move_type_match) { ++rej.move_type; continue; }

            if (block_all_combat) {
                ++rej.blocked; continue;
            } else if (in_attack && is_uninterrupt_) {
                // In Uninterrupt: only 3key chain combos
                if (move.key_count != 3) { ++rej.key_count; continue; }
                // [ORIGINAL] Binary: ConditionCurrentAnimation::isEqual @ 0x10083bb0
                // Evaluate all conditions via condition_system (centralized API).
                int current_frame = (int)(anim_time_ * anim_fps_);
                {
                    ConditionResult cond = evaluate_conditions(
                        move, current_anim_, current_move_, current_frame);
                    if (!cond.satisfied) { ++rej.cur_anim; continue; }
                }
            } else if (in_attack && past_attack_interval) {
                // Past attack interval: allow 1key/2key to interrupt recovery
                if (move.key_count == 3) { ++rej.key_count; continue; }
            } else {
                if (move.key_count == 3) { ++rej.key_count; continue; }
            }
            // Match direction
            if (move.direction != cur_direction) { ++rej.direction; continue; }
            // Match weapon by tactic_weapon (empty = any weapon)
            if (!is_weapon_allowed(move)) { ++rej.weapon; continue; }
            // Check distance condition (only from main <Conditions>, not <Tactics>)
            // Note: <Tactics><Distance> is for AI move selection, not player.
            // We skip distance check entirely — player can attack at any distance.
            // (The original game uses distance only for AI tactic selection.)
            // Check weapon subtype lock (from <Locks><Item SubType="...">)
            if (!move.required_weapon_subtype.empty() &&
                move.required_weapon_subtype.find(equipped_weapon_) == std::string::npos) { ++rej.subtype; continue; }
            // [ORIGINAL] CurrentAnimation condition check via condition_system.
            // Binary: ConditionCurrentAnimation::isEqual @ 0x10083bb0,
            // findMatchingSlotInList @ 0x10083ca0.
            {
                int current_frame = (int)(anim_time_ * anim_fps_);
                ConditionResult cond = evaluate_conditions(
                    move, current_anim_, current_move_, current_frame);
                if (!cond.satisfied) { ++rej.cur_anim; continue; }
            }
            // Prevent a move from chaining into itself (same move can't restart).
            // Without this, moves with empty required_current_animation can
            // be re-triggered during their own uninterrupt window.
            if (move.name == current_move_) { ++rej.self; continue; }
            // Check if animation exists
            std::string anim_name = move.filename;
            if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                anim_name = anim_name.substr(0, anim_name.size()-4);
            if (!assets_->animations().count(anim_name)) { ++rej.no_anim; continue; }
            // [DIAGNOSTIC] This move passed all filters — count it.
            ++candidate_count;
            // Select by highest priority
            if (!best_move || move.priority > best_move->priority) {
                best_move = &move;
            }
        }

        if (best_move) {
            std::string anim_name = best_move->filename;
            if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                anim_name = anim_name.substr(0, anim_name.size()-4);
            std::printf("[COMBAT] %s%s -> %s (anim '%s', prio=%d, tmpl='%s', dist=%.1f)\n",
                        (in_attack && !is_uninterrupt_) ? "[COMBO] " : "",
                        cur_move_type.c_str(), best_move->name.c_str(),
                        anim_name.c_str(), best_move->priority,
                        best_move->template_name.c_str(), dist_to_enemy);
            debug_log("[MOVE] f=%llu %s dir=%s -> %s (cand=%d prio=%d)\n",
                (unsigned long long)total_frame_count_, cur_move_type.c_str(),
                cur_direction.c_str(), best_move->name.c_str(), candidate_count, best_move->priority);
            // [DIAGNOSTIC] Structured O/P decision log for input diagnosis.
            std::printf("[INPUT_DECISION] f=%llu btn=%s keys_down=%s%s%s%s just=%s%s "
                        "face=%d dir=%s ms=%d anim='%s' move='%s' hit=%u unint=%d "
                        "basic=%d cand=%d sel='%s' reject=none\n",
                        (unsigned long long)total_frame_count_,
                        punch_pressed ? "O" : "P",
                        key_up?"W":"", key_down?"S":"", key_left?"A":"", key_right?"D":"",
                        punch_pressed?"O":"", kick_pressed?"P":"",
                        (int)facing_right_, cur_direction.c_str(), move_state_,
                        current_anim_.c_str(), current_move_.c_str(),
                        hit_anim_, is_uninterrupt_?1:0, (int)(in_attack && !is_uninterrupt_),
                        candidate_count, best_move->name.c_str());
            play_animation(anim_name, false, best_move->priority);
            current_move_ = best_move->name;
            int fc = assets_->animations()[anim_name].frame_count;
            hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
            move_state_ = 10;
            need_switch_to_idle_ = false;
            // [Wave 11A M2] Controlled move start = SetDirection: every
            // attack turns the fighter to face the enemy (moves.xml
            // Controlled template, VERIFY_W11 Q2).
            facing_right_ = desired_facing_right_;
            // [ORIGINAL] Play attack swing sound at attack start.
            // Original SF2 plays f_pl_attack*.wav on the first attack frame;
            // [S1] the m_/f_ prefix follows <Warrior Voice=> in the saves.
            int snd = (best_move->name.length() % 4) + 1;
            // [Soak-fix Wave 9A] F1: the weapon swish — LIVE_INTERACTION_TRACE
            // §4.7 pins melee swings to swish2..swish7 and a sword loadout to
            // swish_sword1 (the soak: swings played no swish at all).
            // [HEURISTIC-TODO] the per-move swish index stays unpinned; the
            // deterministic index mirrors the voice scheme.
            // The swish plays BEFORE the attack voice: the S1 audio soak
            // asserts the swing's LAST sound is the gender voice
            // (m_/f_pl_attack*); the trace's hit chain (§4.3) also orders the
            // swish first.
            play_sound(equipped_weapon_ != "Fists"
                           ? "swish_sword1"
                           : "swish" + std::to_string((best_move->name.length() % 6) + 2),
                       0.4f);
            ++swish_play_count_;  // [Wave 9A] F1f probe: observable even when the voice follows
            play_sound(player_attack_sound(snd), 0.5f);
            goto after_combat;
        } else if (punch_pressed || kick_pressed) {
            // [DIAGNOSTIC] No candidate found — log structured reject.
            std::printf("[INPUT_DECISION] f=%llu btn=%s keys_down=%s%s%s%s just=%s%s "
                        "face=%d dir=%s ms=%d anim='%s' move='%s' hit=%u unint=%d "
                        "basic=%d cand=%d sel='' reject=no_candidate "
                        "| rejected by: file=%d titan=%d type=%d blocked=%d keys=%d "
                        "dir=%d weapon=%d subtype=%d curanim=%d self=%d noanim=%d "
                        "(weapon='%s')\n",
                        (unsigned long long)total_frame_count_,
                        punch_pressed ? "O" : "P",
                        key_up?"W":"", key_down?"S":"", key_left?"A":"", key_right?"D":"",
                        punch_pressed?"O":"", kick_pressed?"P":"",
                        (int)facing_right_, cur_direction.c_str(), move_state_,
                        current_anim_.c_str(), current_move_.c_str(),
                        hit_anim_, is_uninterrupt_?1:0, (int)(in_attack && !is_uninterrupt_),
                        candidate_count,
                        rej.no_file, rej.titan, rej.move_type, rej.blocked,
                        rej.key_count, rej.direction, rej.weapon, rej.subtype,
                        rej.cur_anim, rej.self, rej.no_anim,
                        equipped_weapon_.c_str());
            debug_log("[MOVE] f=%llu %s dir=%s -> NO CANDIDATE (cand=%d ms=%d hit=%u unint=%d)\n",
                (unsigned long long)total_frame_count_, cur_move_type.c_str(),
                cur_direction.c_str(), candidate_count, move_state_, hit_anim_, is_uninterrupt_?1:0);
            // Debug: no move found — log why
            static int no_move_log = 0;
            if (no_move_log < 3) {
                std::printf("[COMBAT] NO MOVE for %s dir='%s' basic=%d — candidates:\n",
                            cur_move_type.c_str(), cur_direction.c_str(), (in_attack && !is_uninterrupt_) ? 1 : 0);
                for (auto& [name, move] : assets_->moves()) {
                    bool mt_match = (move.move_type == cur_move_type) ||
                        (cur_move_type == "Punch" && move.move_type.empty() && is_weapon_allowed(move) && move.key_count <= 2);
                    if (!mt_match) continue;
                    if (move.direction != cur_direction) continue;
                    if (move.key_count == 3 && !(in_attack && !is_uninterrupt_)) continue;
                    std::string anim_name = move.filename;
                    if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                        anim_name = anim_name.substr(0, anim_name.size()-4);
                    bool anim_exists = assets_->animations().count(anim_name) > 0;
                    std::printf("  %s tmpl='%s' kc=%d tw='%s' titan=%d anim=%d\n",
                                name.c_str(), move.template_name.c_str(), move.key_count,
                                move.tactic_weapon.c_str(),
                                move.template_name.find("Titan") != std::string::npos ? 1 : 0,
                                anim_exists ? 1 : 0);
                }
                no_move_log++;
            }
        }
    } else if ((punch_pressed || kick_pressed) && is_uninterrupt_) {
        // [DIAGNOSTIC] O/P pressed but blocked by Uninterrupt interval.
        std::printf("[INPUT_DECISION] f=%llu btn=%s keys_down=%s%s%s%s just=%s%s "
                    "face=%d dir=? ms=%d anim='%s' move='%s' hit=%u unint=%d "
                    "basic=? cand=? sel='' reject=uninterrupt\n",
                    (unsigned long long)total_frame_count_,
                    punch_pressed ? "O" : "P",
                    key_up?"W":"", key_down?"S":"", key_left?"A":"", key_right?"D":"",
                    punch_pressed?"O":"", kick_pressed?"P":"",
                    (int)facing_right_, move_state_,
                    current_anim_.c_str(), current_move_.c_str(),
                    hit_anim_, is_uninterrupt_?1:0);
    }
}
}

// === SPECIAL MOVES (jumps, rolls, duck) — from moves.xml ===
    // Match 1key moves with Jump, Step, or no type (Duck, Roll)
    // [ORIGINAL] PC source: sf2.js Pqb() (line 18769-18810) -- move selector
    // During attack (elapsed < total_len):
    //   If in Uninterrupt: Pqb allows chain attacks (YAa/Gea) -- movement specials NOT included
    //   If NOT in Uninterrupt: Pqb returns 0 -- NO moves available at all
    // After attack ends: normal move selection (1key, 2key, movement specials, steps)
    // movement specials (jump, duck, roll) are NEVER available during attack.
    // Exclude StartStance -- player should act immediately after intro.
    do {
    // [M1] The duck must not gate the movement selector: it plays as a
    // special (move_state 10, hit_anim_ > 0), so S held (duck) then A would
    // otherwise never fire the back roll. The original allows movement
    // specials out of the duck. Combat gating (the in_attack flag below)
    // is untouched — this only opens jump/roll selection while ducking.
    const bool ducking = (current_move_ == "Duck");
    if (!in_attack || ducking) {
        // Trigger when ANY direction key is just pressed; use HELD keys for full direction.
        // This correctly handles W+left (A just pressed while W held) → "UpBack"
        bool any_dir_just_pressed =
            input.keys_just_pressed[(size_t)plat::Key::W] ||
            input.keys_just_pressed[(size_t)plat::Key::ArrowUp] ||
            input.keys_just_pressed[(size_t)plat::Key::A] ||
            input.keys_just_pressed[(size_t)plat::Key::ArrowLeft] ||
            input.keys_just_pressed[(size_t)plat::Key::S] ||
            input.keys_just_pressed[(size_t)plat::Key::ArrowDown] ||
            input.keys_just_pressed[(size_t)plat::Key::D] ||
            input.keys_just_pressed[(size_t)plat::Key::ArrowRight];

        if (!any_dir_just_pressed) {
            // [P7] No direction edge this frame — but the duck block below
            // must still run: the held-key duck continuation ("attack ends
            // while S is held -> duck again") is a held-state transition,
            // and skipping it here was half of the auto-repeat cycle (the
            // state-11 exit re-triggered the animation every hit_anim_
            // expiry). The duck block's own guard prevents re-firing an
            // active duck.
        } else {
            // Determine direction from HELD key state
            std::string cur_direction;
        bool up_held = input.keys_down[(size_t)plat::Key::W] ||
                       input.keys_down[(size_t)plat::Key::ArrowUp];
        bool down_held = input.keys_down[(size_t)plat::Key::S] ||
                         input.keys_down[(size_t)plat::Key::ArrowDown];
        if (up_held && key_forward) cur_direction = "UpForward";
        else if (up_held && key_back) cur_direction = "UpBack";
        else if (up_held) cur_direction = "Up";
        else if (down_held && key_forward) cur_direction = "DownForward";
        else if (down_held && key_back) cur_direction = "DownBack";
        else if (down_held) cur_direction = "Down";

        if (!cur_direction.empty()) {
            // Find best matching move (Jump, or MOVE type with 1key)
            const MoveDef* best_move = nullptr;
            for (auto& [name, move] : assets_->moves()) {
                if (move.filename.empty() || move.template_name.empty()) continue;
                if (move.key_count != 1) continue;
                if (move.direction != cur_direction) continue;
                // [Wave 11A M3] Anti-restart (Controlled template guard):
                // NOT(CurrentAnimation == $Move AND SemiUninterrupt) — a
                // move currently playing inside its OWN SemiUninterrupt
                // window (Duck: <Interval Name="SemiUninterrupt" End="4"/>,
                // frames 0..4) cannot re-select itself, so spamming S cannot
                // restart the duck animation. A re-tap AFTER the window
                // legitimately restarts (original behavior).
                {
                    std::string cand_anim = move.filename;
                    if (cand_anim.size() > 4 &&
                        cand_anim.substr(cand_anim.size() - 4) == ".bin")
                        cand_anim = cand_anim.substr(0, cand_anim.size() - 4);
                    if (cand_anim == current_anim_ &&
                        move.semi_uninterrupt_end >= 0 &&
                        (int)(anim_time_ * anim_fps_) <=
                            move.semi_uninterrupt_end)
                        continue;
                }
                // Skip Titan moves (player is not a Titan)
                if (move.template_name.find("Titan") != std::string::npos &&
                    move.template_name.find("NotTitan") == std::string::npos) continue;
                if (move.template_name.find("Wall") != std::string::npos) continue;
                // Match Jump moves or MOVE type (not Wall, not Punch/Kick).
                // [M1] The old whitelist (move_type in {Jump, MOVE, ""})
                // rejected any move whose 4th template token is a modifier
                // rather than a move type — BackRoll's Template
                // "1key|DownBack|Retreat|NotTitan" parsed move_type="Retreat",
                // so S+A could never fire from this selector: A-then-S (A
                // held, then S) never rolled, and S-then-A only worked when
                // both keys landed in the same frame from idle (the hardcoded
                // roll block below). Type="ATTACK" is the real discriminator
                // that keeps 1key attacks (HighPunch, HighKneeUp, ...) out of
                // the movement selector — Retreat/Arms/etc. modifier moves
                // are locomotion and must pass.
                if (move.is_attack) continue;
                // Weapon filter
                if (!is_weapon_allowed(move)) continue;
                // Check animation
                std::string anim_name = move.filename;
                if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                    anim_name = anim_name.substr(0, anim_name.size()-4);
                if (!assets_->animations().count(anim_name)) continue;
                if (!best_move || move.priority > best_move->priority) {
                    best_move = &move;
                }
            }

            if (best_move) {
                std::string anim_name = best_move->filename;
                if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                    anim_name = anim_name.substr(0, anim_name.size()-4);
                std::printf("[MOVE] %s (anim '%s', prio=%d)\n",
                            best_move->name.c_str(), anim_name.c_str(),
                            best_move->priority);
                // [Wave 11A M3] probe: every Duck move START emits a row —
                // a re-tap that restarts the animation is a second row (the
                // spam-S defect is restarts, and this is the counter).
                if (anim_name == "duck") {
                    std::printf("[DUCK-PLAY] f=%llu anim='%s' move='%s' frame=%.1f\n",
                                (unsigned long long)total_frame_count_,
                                anim_name.c_str(), best_move->name.c_str(),
                                anim_time_ * anim_fps_);
                }
                play_animation(anim_name, false, best_move->priority);
                current_move_ = best_move->name;
                int fc = assets_->animations()[anim_name].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                move_state_ = 10;
                // [Wave 11A M2] Movement specials (jump/roll family) are
                // Controlled moves: SetDirection at move start.
                facing_right_ = desired_facing_right_;
                goto after_combat;
            }
        }
        }  // any_dir_just_pressed [P7]

        // Duck: S just pressed (fresh edge) with no direction.
        // [Wave 11A M3] Duck requires Key Type="Down" PressType="Tap" —
        // a FRESH EDGE only (moves.xml Duck <Key Type="Down"
        // PressType="Tap"/>). The old held-key entry re-triggered the
        // duck every frame the key was down ("при спаме s анимация
        // приседания начинается сначала").
        // [P7] Held-key continuation (the soak's P7 pin): when the one-shot
        // duck special ends while S is STILL held, re-enter the duck so the
        // crouch holds while the key is down — this is a state-0 transition
        // (the fighter is NOT currently ducking), never an auto-repeat of a
        // playing duck.
        const bool duck_edge =
            (input.keys_just_pressed[(size_t)plat::Key::S] ||
             input.keys_just_pressed[(size_t)plat::Key::ArrowDown]) &&
            !key_forward && !key_back;
        const bool duck_held_reenter =
            key_down && !key_forward && !key_back &&
            move_state_ == 0 && current_anim_ != "duck";
        const bool duck_input = duck_edge || duck_held_reenter;
        if (duck_input && (move_state_ == 0 || move_state_ == 11)) {
            // Debug: log duck attempt
            static int duck_log_count = 0;
            if (duck_log_count < 5) {
                std::printf("[DUCK] attempt — move_state=%d key_down=%d\n", move_state_, key_down ? 1 : 0);
                duck_log_count++;
            }
            // Find Duck move (1key|Down|NotTitan, Type=MOVE)
            const MoveDef* duck_move = nullptr;
            for (auto& [name, move] : assets_->moves()) {
                if (move.filename.empty()) continue;
                if (move.key_count != 1) continue;
                if (move.direction != "Down") continue;
                if (move.is_jump) continue;
                if (move.move_type == "Punch" || move.move_type == "Kick") continue;
                // Skip Titan moves (they require TitanGiantSword weapon)
                if (move.template_name.find("Titan") != std::string::npos &&
                    move.template_name.find("NotTitan") == std::string::npos) continue;
                // Weapon filter — match equipped weapon
                if (!is_weapon_allowed(move)) continue;
                // Require NotTitan template (player is not a Titan)
                if (!move.is_not_titan) continue;
                std::string anim_name = move.filename;
                if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                    anim_name = anim_name.substr(0, anim_name.size()-4);
                if (!assets_->animations().count(anim_name)) continue;
                if (!duck_move || move.priority > duck_move->priority) {
                    duck_move = &move;
                }
            }
            if (duck_move) {
                std::string duck_anim_name = duck_move->filename;
                if (duck_anim_name.size() > 4 && duck_anim_name.substr(duck_anim_name.size()-4) == ".bin")
                    duck_anim_name = duck_anim_name.substr(0, duck_anim_name.size()-4);
                // [Wave 11A M3] Anti-restart: while the Duck move is inside
                // its SemiUninterrupt window (frames 0..semi_uninterrupt_end,
                // moves.xml Duck End=4), a re-selection is REJECTED
                // (Controlled guard: NOT(CurrentAnimation == $Move AND
                // SemiUninterrupt)). The selection engine defers the move
                // switch to the next frame (Fighter+0x218), so same-frame
                // re-selections collapse into one switch anyway.
                // [HEURISTIC-TODO] the deferred switch (0x8F4AC4B4) is not
                // ported; the immediate switch + this guard give the same
                // user-visible result.
                const bool in_duck_semi =
                    duck_move->semi_uninterrupt_end >= 0 &&
                    current_anim_ == duck_anim_name &&
                    (int)(anim_time_ * anim_fps_) <= duck_move->semi_uninterrupt_end;
                // Only switch animation if not already ducking
                if (!in_duck_semi &&
                    (move_state_ != 11 || current_anim_ != duck_anim_name)) {
                    std::printf("[DUCK] found: %s (anim '%s')\n", duck_move->name.c_str(), duck_anim_name.c_str());
                    // [Wave 11A M3] probe: every Duck move START (a re-tap
                    // that restarts the animation emits another row; the
                    // spam-S defect is restarts, and this is the counter).
                    std::printf("[DUCK-PLAY] f=%llu anim='%s' move='%s' frame=%.1f\n",
                                (unsigned long long)total_frame_count_,
                                duck_anim_name.c_str(), duck_move->name.c_str(),
                                anim_time_ * anim_fps_);
                    // [Wave 11A M2] Duck is a 1key Controlled move:
                    // SetDirection at move start (face the enemy).
                    facing_right_ = desired_facing_right_;
                    play_animation(duck_anim_name, true, 1);  // priority 1: defensive
                    current_move_ = duck_move->name;
                    move_state_ = 11;
                    input_handler_.set_duck_play_time(0);
                }
            } else {
                static int no_duck_log = 0;
                if (no_duck_log < 2) {
                    std::printf("[DUCK] no duck move found! Searching 1key|Down moves:\n");
                    for (auto& [name, move] : assets_->moves()) {
                        if (move.key_count != 1 || move.direction != "Down") continue;
                        std::printf("  %s tmpl='%s' mt='%s' titan=%d not_titan=%d jump=%d\n",
                                    name.c_str(), move.template_name.c_str(),
                                    move.move_type.c_str(),
                                    move.template_name.find("Titan") != std::string::npos ? 1 : 0,
                                    move.is_not_titan ? 1 : 0,
                                    move.is_jump ? 1 : 0);
                    }
                    no_duck_log++;
                }
            }
        }
    }
    } while(0);

    // === BLOCK MECHANICS (automatic defensive reaction) ===
    // [ORIGINAL] Block is NOT a player action — it's an automatic defensive
    // reaction when the enemy attacks. The block chance comes from AI tactics
    // (BlockChance in tacticSettings.xml). When enemy hits player:
    // 1. Check if player is facing enemy
    // 2. Roll against BlockChance (from AI/tactic settings)
    // 3. If block succeeds: play block animation, apply chip damage
    // 4. If block fails: take full damage
    // 
    // Block is triggered in combat hit detection, NOT in input processing.
    // This section only clears block state when not blocking.
    if (move_state_ == 11) {
        // Currently blocking — check if should continue
        // [P7] Block lasts for the duration of the block animation — but the
        // DUCK (which shares move_state_ 11) must NOT end when its hit_anim_
        // countdown expires while S is still held: that dropped the state to
        // 0 and the held-key duck block re-triggered the animation every
        // cycle (the soak's Duck auto-repeat: "[MOVE] Duck" ×16 with no key
        // events). While the duck key is held the state stays 11 and the
        // looping duck animation plays; release ends it (the exit block
        // below).
        bool still_ducking = key_down && !key_forward && !key_back;
        if (hit_anim_ == 0 && !still_ducking) {
            // Block animation finished — exit block state
            move_state_ = 0;
            player_fighter_.is_blocking = false;
        }
    } else {
        // Not in block state — ensure blocking flag is clear
        player_fighter_.is_blocking = false;
    }

    // === ROLL MOVES (S+D forward roll, S+A back roll) ===
    // Separate from the any_dir_just_pressed-gated block above because
    // rolls should trigger even when the direction keys are already held
    // (e.g. holding D to walk forward + pressing S to roll).
    // These use the raw S+Forward/Back combination directly.
    if (move_state_ == 0 && !start_stance_playing_ && hit_anim_ == 0) {
        // Forward roll: S + forward (D when facing right)
        if (key_down && key_forward && !key_back) {
            if (assets_->animations().count("forward_roll") && current_anim_ != "forward_roll") {
                std::printf("[MOVE] ForwardRoll (S+forward)\n");
                // [Wave 11A M2] Movement specials (jump/roll family) are
                // Controlled moves: SetDirection at move start.
                facing_right_ = desired_facing_right_;
                play_animation("forward_roll", false, 1);  // priority 1: defensive
                current_move_ = "ForwardRoll";
                int fc = assets_->animations()["forward_roll"].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                move_state_ = 10;
                goto after_combat;
            }
        }
        // Back roll: S + back (A when facing right)
        else if (key_down && key_back && !key_forward) {
            if (assets_->animations().count("back_roll") && current_anim_ != "back_roll") {
                std::printf("[MOVE] BackRoll (S+back)\n");
                // [Wave 11A M2] Movement specials (jump/roll family) are
                // Controlled moves: SetDirection at move start.
                facing_right_ = desired_facing_right_;
                play_animation("back_roll", false, 1);  // priority 1: defensive
                current_move_ = "BackRoll";
                int fc = assets_->animations()["back_roll"].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                move_state_ = 10;
                goto after_combat;
            }
        }
    }

    // === MAGIC CAST (SPEC_COMBAT_CORE Q1.6, VERIFY_W11 Q1 GREEN) ===
    // The magic button is a move key of Type="Magic" PressType="Tap" (the
    // MagicXXXPlayer moves: Template="1key|MagicPlayer", Type="ATTACK",
    // Priority=110). Keyboard binding: X. The cast is selectable only with
    // a full bar (count==1) and an equipped magic item; the move is the
    // MagicXXXPlayer of the equipped item's SubType (list.xml MAGIC_FIRE_
    // BALL -> SubType FireBall -> FireballPlayer TacticWeapon="FireBall").
    // Casting consumes the charge (Fighter::applyCommand @ 0x8F4A83B8,
    // command type 0, delta -1) and fires the magic projectile.
    // [HEURISTIC-TODO] The touch magic button only renders today; it does
    // not route into magic_pressed (touch_.magic_pressed does not exist).
    if (input.keys_just_pressed[(size_t)plat::Key::X] &&
        player_fighter_.magic_count == 1 && !equipped_magic_.empty() &&
        !start_stance_playing_ && move_state_ != 10 && hit_anim_ == 0) {
        std::string subtype;
        if (list_data_loaded_) {
            for (const auto& li : list_data_.items) {
                if (li.type == "Magic" && li.name == equipped_magic_) {
                    subtype = li.subtype;
                    break;
                }
            }
        }
        const MoveDef* cast_move = nullptr;
        if (!subtype.empty()) {
            for (auto& [name, m] : assets_->moves()) {
                (void)name;
                if (m.filename.empty()) continue;
                if (m.tactic_weapon != subtype) continue;
                if (m.template_name.find("MagicPlayer") == std::string::npos)
                    continue;
                if (!cast_move || m.priority > cast_move->priority)
                    cast_move = &m;
            }
        }
        if (cast_move) {
            std::string cast_anim = strip_bin_suffix(cast_move->filename);
            if (assets_->animations().count(cast_anim)) {
                play_animation(cast_anim, false, 1);
                current_move_ = cast_move->name;
                int fc = assets_->animations()[cast_anim].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                move_state_ = 10;
                // [Wave 11A M2] Movement specials (jump/roll family) are
                // Controlled moves: SetDirection at move start.
                facing_right_ = desired_facing_right_;
                player_fighter_.magic_count = 0;   // the cast consumes
                // [ORIGINAL] The cast fires the equipped magic: the
                // missile is spawned at the caster's hand travelling in
                // the facing direction (moves.xml <Bullets Type=
                // "MagicBullet">; the missile itself is a MagicMissile
                // family player — NoMagicRecharge, so it never re-charges).
                spawn_projectile(subtype, player_pos_x_, player_pos_y_,
                                 facing_right_, true);
                std::printf("[MAGIC-CAST] f=%llu move='%s' subtype='%s' cnt=%d\n",
                            (unsigned long long)total_frame_count_,
                            cast_move->name.c_str(), subtype.c_str(),
                            player_fighter_.magic_count);
                goto after_combat;
            }
        } else {
            std::printf("[MAGIC-CAST] no cast move for subtype '%s' (item '%s')\n",
                        subtype.c_str(), equipped_magic_.c_str());
        }
    }

    // Decrement step cooldown (set after rolls/specials to prevent
    // immediate step when the held key resumes).
    if (step_cooldown_ms_ > dt) step_cooldown_ms_ -= dt; else step_cooldown_ms_ = 0;

    // === STEP MOVEMENT (from moves.xml: StepForward/StepBack) ===
    // FIX: don't override attack animation with step when in an attack.
    // hit_anim_ > 0 also applies during start_stance (stance_2), which
    // should NOT block steps. Use is_in_attack to distinguish.
    {
    bool is_in_attack = false;
    if (!current_move_.empty()) {
        auto mit = assets_->moves().find(current_move_);
        if (mit != assets_->moves().end() && mit->second.is_attack) is_in_attack = true;
    }
    if (!is_in_attack) {
        // [ORIGINAL] No direction latch — read key state per frame.
        // The original binary (Model::step 0x10161ad0) has no latch;
        // fwd_latched/back_latched are just key_forward/key_back.
        bool fwd_latched = key_forward;
        bool back_latched = key_back;

        // Find step animation names from moves.xml
        std::string step_fwd_anim, step_back_anim;
        int step_fwd_prio = -1, step_back_prio = -1;
        for (auto& [name, move] : assets_->moves()) {
            if (move.filename.empty()) continue;
            // Skip Titan moves (player is not a Titan)
            if (move.template_name.find("Titan") != std::string::npos) continue;
            if (!move.is_step || move.is_double_step) continue;
            // Use steps matching the equipped weapon's tactic_weapon.
            // Weapon-specific steps (composite_sword_step_forward, etc.)
            // are filtered by their tactic_weapon attribute.
            if (!is_weapon_allowed(move)) continue;
            std::string anim_name = move.filename;
            if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                anim_name = anim_name.substr(0, anim_name.size()-4);
            if (!assets_->animations().count(anim_name)) continue;
            if (move.direction == "Forward" && move.priority > step_fwd_prio) {
                step_fwd_anim = anim_name;
                step_fwd_prio = move.priority;
            } else if (move.direction == "Back" && move.priority > step_back_prio) {
                step_back_anim = anim_name;
                step_back_prio = move.priority;
            }
        }
        // Fallback if not found
        if (step_fwd_anim.empty() && assets_->animations().count("step_forward")) step_fwd_anim = "step_forward";
        if (step_back_anim.empty() && assets_->animations().count("step_back")) step_back_anim = "step_back";

        if (move_state_ == 0 && !start_stance_playing_) {  // IDLE (NOT during start stance)
            // [ORIGINAL] During StartStance, player input is blocked — the
            // intro animation must play to completion (sf2.js battle phase
            // Je=1 StartStance). Steps are NOT allowed during start stance.
            // [STEP_DEBUG] Log step conditions around start-stance end
            if (total_frame_count_ >= 245 && total_frame_count_ <= 265) {
                std::printf("[STEPDBG] f=%llu ms=%d ss=%d ha=%u anim='%s' kf=%d kb=%d kd=%d sf='%s'\n",
                            (unsigned long long)total_frame_count_,
                            move_state_, (int)start_stance_playing_, hit_anim_,
                            current_anim_.c_str(),
                            (int)key_forward, (int)key_back, (int)key_down,
                            step_fwd_anim.c_str());
            }
            // [FIX] Only start a new step if we're not already in a step animation
            // that hasn't finished its cycle. This prevents rapid A/D presses from
            // restarting the animation and causing tiny fast steps.
            // The step_cooldown_ms_ check prevents immediate re-entry after a step ends.
            bool can_start_step = (current_anim_ != step_fwd_anim && current_anim_ != step_back_anim)
                                  || anim_player_.anim_finished();
            if (can_start_step && key_forward && !key_back && !key_down && !step_fwd_anim.empty() && step_cooldown_ms_ == 0) {
                move_state_ = 2;
                play_animation(step_fwd_anim, true, 0);  // priority 0: movement (interruptible)
                input_handler_.reset_step_frames();
                // [FIX] Set cooldown after step starts to prevent rapid tap spam.
                // Matches the original's kMinStepFrames=12 gate (200ms at 60fps).
                // Without this, each A/D tap restarts the step animation, causing
                // the character to move much faster than intended.
                step_cooldown_ms_ = 200;
            } else if (can_start_step && key_back && !key_forward && !key_down && !step_back_anim.empty() && step_cooldown_ms_ == 0) {
                move_state_ = 1;
                play_animation(step_back_anim, true, 0);  // priority 0: movement (interruptible)
                input_handler_.reset_step_frames();
                step_cooldown_ms_ = 200;  // [FIX] Same cooldown for backward steps
            }
        } else if (move_state_ == 1) {  // MOVING_BACK
            // [ORIGINAL] Double-tap Back → BackHandflip (handstand flip retreat)
            // Original moves.xml: BackHandflip has Keys=Back Tap + Back Tap,
            // FileName=back_handflip.bin. It's a retreat move (not a dash).
            if (input_handler_.double_step_back_requested()) {
                if (assets_->animations().count("back_handflip")) {
                    play_animation("back_handflip", false, 1);  // priority 1: evasive retreat
                    move_state_ = 10;
                    current_move_ = "BackHandflip";
                    int fc = assets_->animations()["back_handflip"].frame_count;
                    hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                    debug_log("[MOVE] f=%llu BackHandflip (handstand retreat)\n",
                              (unsigned long long)total_frame_count_);
                } else {
                    // [HEURISTIC-TODO] Fallback: manual position jump when
                    // back_handflip animation is not available.
                    player_pos_x_ += (facing_right_ ? -1.0f : 1.0f) * 150.0f;
                    step_cooldown_ms_ = 300;
                    debug_log("[MOVE] f=%llu BackHandflip fallback (no anim)\n",
                              (unsigned long long)total_frame_count_);
                }
                input_handler_.clear_double_step_back();
            } else if (!back_latched && step_min_played) {
                // [ORIGINAL] Key released — step ends only after the animation
                // completes its full cycle. This prevents the visual interruption
                // on a single tap (character must finish its natural step motion).
                auto anim_it = assets_->animations().find(current_anim_);
                // [M4] Cycle completion measured in ANIMATION frames
                // (anim_time_ x anim_fps_), not engine frames: step_frames_
                // counts 16 ms engine frames, so comparing it against the
                // animation's frame_count cut a step at ~5 of its 16 frames
                // — a third of a stride — losing ~20 of the authored 66
                // units and snapping the fighter to idle mid-step (the
                // soak's "small steps without waiting for walk animation
                // end").
                bool cycle_done = (anim_it != assets_->animations().end()) &&
                    (anim_time_ * anim_fps_) >= (float)anim_it->second.frame_count;
                if (cycle_done) {
                    // If the opposite direction is already held, transition directly
                    // to the new step without going through idle.
                    if (fwd_latched && !step_fwd_anim.empty()) {
                        move_state_ = 2;
                        play_animation(step_fwd_anim, true, 0);
                        input_handler_.reset_step_frames();
                    } else {
                        move_state_ = 0; need_switch_to_idle_ = true;
                    }
                }
            } else if (fwd_latched && !back_latched && step_min_played && !step_fwd_anim.empty()) {
                // [FIX] Only change direction if current step animation has
                // completed at least one full cycle. [ORIGINAL] In the binary
                // (Model::step 0x10161ad0), movement entries are processed per
                // complete animation cycle — direction changes mid-cycle are ignored.
                auto anim_it = assets_->animations().find(current_anim_);
                // [M4] Same animation-frame cycle measure as cycle_done above.
                bool cycle_complete = (anim_it != assets_->animations().end()) &&
                    (anim_time_ * anim_fps_) >= (float)anim_it->second.frame_count;
                if (cycle_complete) {
                    move_state_ = 2;
                    play_animation(step_fwd_anim, true, 0);  // priority 0: movement
                    input_handler_.reset_step_frames();  // Reset counter for new direction
                }
            }
        } else if (move_state_ == 2) {  // MOVING_FORWARD
            // [ORIGINAL] Double-tap Forward during ForwardStep  DoubleStepForward (dash)
            if (input_handler_.double_step_fwd_requested()) {
                if (assets_->animations().count("double_step_forward")) {
                    play_animation("double_step_forward", false, 1);  // priority 1: dash (evasive)
                    move_state_ = 10;  // special (non-interruptible during dash)
                    current_move_ = "DoubleStepForward";
                    int fc = assets_->animations()["double_step_forward"].frame_count;
                    hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                    debug_log("[MOVE] f=%llu DoubleStepForward (dash)\n",
                              (unsigned long long)total_frame_count_);
                } else {
                    // [HEURISTIC-TODO] Fallback: manual position jump when
                    // double_step_forward animation is not available.
                    player_pos_x_ += (facing_right_ ? 1.0f : -1.0f) * 150.0f;
                    step_cooldown_ms_ = 300;
                    debug_log("[MOVE] f=%llu DoubleStepForward fallback (no anim)\n",
                              (unsigned long long)total_frame_count_);
                }
                input_handler_.clear_double_step_fwd();
            } else if (!fwd_latched && step_min_played) {
                // [ORIGINAL] Key released — step ends only after the animation
                // completes its full cycle. This prevents the visual interruption
                // on a single tap (character must finish its natural step motion).
                auto anim_it = assets_->animations().find(current_anim_);
                // [M4] Cycle completion measured in ANIMATION frames
                // (anim_time_ x anim_fps_), not engine frames: step_frames_
                // counts 16 ms engine frames, so comparing it against the
                // animation's frame_count cut a step at ~5 of its 16 frames
                // — a third of a stride — losing ~20 of the authored 66
                // units and snapping the fighter to idle mid-step (the
                // soak's "small steps without waiting for walk animation
                // end").
                bool cycle_done = (anim_it != assets_->animations().end()) &&
                    (anim_time_ * anim_fps_) >= (float)anim_it->second.frame_count;
                if (cycle_done) {
                    // If the opposite direction is already held, transition directly
                    // to the new step without going through idle.
                    if (back_latched && !step_back_anim.empty()) {
                        move_state_ = 1;
                        play_animation(step_back_anim, true, 0);
                        input_handler_.reset_step_frames();
                    } else {
                        move_state_ = 0; need_switch_to_idle_ = true;
                    }
                }
            } else if (back_latched && !fwd_latched && step_min_played && !step_back_anim.empty()) {
                // [FIX] Only change direction if current step animation has
                // completed at least one full cycle. [ORIGINAL] In the binary
                // (Model::step 0x10161ad0), movement entries are processed per
                // complete animation cycle — direction changes mid-cycle are ignored.
                auto anim_it = assets_->animations().find(current_anim_);
                // [M4] Same animation-frame cycle measure as cycle_done above.
                bool cycle_complete = (anim_it != assets_->animations().end()) &&
                    (anim_time_ * anim_fps_) >= (float)anim_it->second.frame_count;
                if (cycle_complete) {
                    move_state_ = 1;
                    play_animation(step_back_anim, true, 0);  // priority 0: movement
                    input_handler_.reset_step_frames();  // Reset counter for new direction
                }
            }
        }
    }
    }

    // === HIT ANIM COUNTDOWN ===
    if (hit_anim_ > 0) {
        hit_anim_ -= std::min<uint32_t>(hit_anim_, dt);
        // [FIX] When hit animation expires, reset root motion deltas to prevent
        // residual displacement from the hit animation being applied to the
        // character's position during the transition back to idle/movement.
        if (hit_anim_ == 0) {
            anim_root_dx_ = 0.0f;
            anim_root_dy_ = 0.0f;
        }
    }

    // is_uninterrupt_ will be computed after update_animation()
    // (we need the updated anim_time_ to check the current frame)
    is_uninterrupt_ = false;

    // Exit special move state when animation finishes
    if (move_state_ == 10 && hit_anim_ == 0) {
        std::printf("[STATE] move_state 10->0 (special ended), current_move='%s'\n",
                    current_move_.c_str());
        move_state_ = 0;
        need_switch_to_idle_ = true;
        // Clear current_move_ so 3key combos don't trigger on next key press
        current_move_.clear();
        // Set step cooldown to prevent immediate step after roll/special ends
        // while keys are still held from the roll input.
        step_cooldown_ms_ = 200;
    }
    // Exit duck/block state when input released.
    // move_state_ 11 is shared between duck (key_down) and block (key_back).
    // Exit when neither defensive input is held.
    // [ORIGINAL] No minimum duration for duck; block follows the same pattern.
    if (move_state_ == 11) {
        input_handler_.set_duck_play_time(input_handler_.duck_play_time() + dt);
        bool still_ducking = key_down && !key_forward && !key_back;
        bool still_blocking = key_back && (move_state_ == 0 || move_state_ == 11) &&
                              hit_anim_ == 0 && !start_stance_playing_;
        if (!still_ducking && !still_blocking && input_handler_.duck_play_time() >= 100) {
            move_state_ = 0;
            need_switch_to_idle_ = true;
            player_fighter_.is_blocking = false;
        }
    }

    after_combat:
    // Camera follows player (always update, even after attack)
    // [ORIGINAL] Screen shake on hit: offset camera by a decaying random
    // amount when player_hit_flash_ or enemy_hit_flash_ is active.
    update_camera();
    float shake = 0.0f;
    if (player_hit_flash_ > 0) shake = std::max(shake, player_hit_flash_ * 12.0f);
    if (enemy_hit_flash_ > 0) shake = std::max(shake, enemy_hit_flash_ * 8.0f);
    float shake_x = 0, shake_y = 0;
    if (shake > 0.1f) {
        shake_x = ((float)(std::rand() % 200) - 100.0f) / 100.0f * shake;
        shake_y = ((float)(std::rand() % 200) - 100.0f) / 100.0f * shake;
    }
    renderer_->camera_set_target(cam_x_ + shake_x, cam_y_ + shake_y);
    renderer_->camera_set_zoom(zoom_);

    // === UPDATE ANIMATION ===
    // MUST run BEFORE any play_animation calls so the final frame's
    // root motion is applied before switching to idle.
    update_animation(dt);

    // === ROOT MOTION APPLICATION ===
    // Apply the per-frame NPivot delta to character world position.
    // anim_root_dx_ is the model-space X displacement of NPivot from the
    // previous frame. When facing left, model-forward is -world-x, so the
    // delta is negated.
    // This is what makes step_forward actually move the character forward,
    // and gives forward-lunging attacks their root-motion feel.
    // jump_y_offset_ is accumulated separately in AnimationPlayer::update().
    // [ORIGINAL] The start stance (stance_2) is a cinematic intro — the
    // character holds position. Suppress root motion during it to prevent
    // the visible twitching the NPivot sway in the animation would cause.
    if (!start_stance_playing_) {
        // [M3] "jump" (W alone, JumpUp) is a pure vertical hop: its authored
        // NPivot X wobbles ±6 units (net 0) around the start, which the soak
        // rendered as a visible left/right drift (~0.10 units/frame) whenever
        // the jump followed a non-aligned move (e.g. a step) and the raw
        // deltas were applied. The hop must not travel horizontally at all.
        // [Soak-fix Wave 9A] F3: the stance idle is a PLANTED stance — its
        // authored NPivot/heel-rel wander (measured: stance_idle heel-rel
        // rocks ~±3 over its cycle) fed player_pos_x_ through the M2 anchor
        // pinning, carrying the fighter a few units AFTER the previous
        // animation finished ("некоторые анимации после завершения
        // переносят персонажа чуть вперёд" — measured tail -3.7 after a
        // punch, -3.5 after a step). The idle does not translate the
        // fighter; the heel-continuity snap at the transition (apply_align)
        // is unaffected.
        const bool no_translate =
            (current_anim_ == "jump") || (current_anim_ == "stance_idle");
        const float root_dx = no_translate ? 0.0f : anim_root_dx_;
        player_pos_x_ += root_dx * (facing_right_ ? 1.0f : -1.0f);
    }

    // [Soak-fix Wave 9A] F3: the stance idle holds its planted heel. The
    // root no longer absorbs the idle's authored heel-rel wander (the
    // no-translate above), so the NODE MAP is compensated instead: every
    // node's pivot-relative X is shifted so NHeel_2 stays at the pose it
    // had when the idle started (the pose the transition's apply_align
    // snap placed). The body then leans around the planted feet — exactly
    // the original's alignAnimation model ("the anchor node keeps its
    // world position") — with no arena-position drift and no foot-slide.
    if (current_anim_ == "stance_idle") {
        auto& node_map = anim_player_.mutable_anim_node_pos();
        auto heel_it = node_map.find("NHeel_2");
        if (heel_it != node_map.end()) {
            const float heel_rel = heel_it->second.first;
            if (prev_update_anim_ != "stance_idle")
                idle_heel_anchor_rel_ = heel_rel;  // entry pose (the snapped one)
            const float fix = heel_rel - idle_heel_anchor_rel_;
            if (std::fabs(fix) > 0.0001f) {
                for (auto& [nname, pos] : node_map) {
                    (void)nname;
                    pos.first -= fix;
                }
            }
        }
    }
    prev_update_anim_ = current_anim_;

    // [MOVEMENT] Debug log for movement state
    debug_log("[MOVEMENT] pos_x=%.1f pos_y=%.1f facing=%d state=%d anim='%s' root_dx=%.2f step_frames=%u\n",
              player_pos_x_, player_pos_y_, facing_right_ ? 1 : -1,
              move_state_, current_anim_.c_str(), anim_root_dx_,
              input_handler_.step_frames());
    // Y displacement is handled by y_adjust_smoothed_ (visual Y correction)
    // and jump_y_offset_ (accumulated in AnimationPlayer::update).
    // No world-space Y drift from per-frame root motion.

    // === UNINTERRUPT CHECK (after update_animation) ===
    // [ORIGINAL] PC source: sf2.js ocb() — checks if current animation frame
    // is within any <Interval Name="Uninterrupt"> start..finish range.
    // Intervals are fired as events when the animation passes their start frame.
    // Uninterrupt is NOT a global input lock — it only blocks during specific
    // frames of the ATTACK animation (not stance_idle or other anims).
    // FIX: only check is_uninterrupt_ when current_anim_ IS the attack animation.
    // Previous code checked current_move_ (which stays set after anim ends),
    // using stance_idle's frame count against the attack's Uninterrupt interval.
    is_uninterrupt_ = false;
    if (hit_anim_ > 0 && !current_move_.empty()) {
        auto move_it = assets_->moves().find(current_move_);
        if (move_it != assets_->moves().end()) {
            int un_start = move_it->second.uninterrupt_start;
            int un_end = move_it->second.uninterrupt_end;
            // [ORIGINAL] SemiUninterrupt @ 0x10115910: can be interrupted by
            // attacks but not by movement. 81 moves declare it.
            const int semi_start = 0;  // SemiUninterrupt always starts at frame 0
            const int semi_end = move_it->second.semi_uninterrupt_end;
            // [ORIGINAL] SelfUninterrupt @ 0x10115910: can only be interrupted
            // by itself (combo chains). 4 moves declare it.
            const int self_start = move_it->second.self_uninterrupt_start;
            const int self_end = move_it->second.self_uninterrupt_end;

            // For moves without explicit Uninterrupt data, default the
            // uninterrupt window to the attack interval. This prevents
            // 1key/2key attacks from cancelling the current attack during
            // its active frames while still allowing 3key chain combos.
            if (un_start < 0 && move_it->second.attack_start >= 0 &&
                move_it->second.attack_end > 0) {
                un_start = move_it->second.attack_start;
                un_end = move_it->second.attack_end;
            }

            if (un_start >= 0 || semi_end >= 0 || self_start >= 0) {
                std::string expected_anim = move_it->second.filename;
                if (expected_anim.size() > 4 && expected_anim.substr(expected_anim.size()-4) == ".bin")
                    expected_anim = expected_anim.substr(0, expected_anim.size()-4);
                if (expected_anim == current_anim_) {
                    int current_frame = (int)(anim_time_ * anim_fps_);
                    // Check Uninterrupt interval
                    if (un_start >= 0) {
                        int start = un_start - 1;
                        int end = un_end > 0 ? un_end - 1 : 9999;
                        if (current_frame >= start && current_frame <= end) {
                            is_uninterrupt_ = true;
                        }
                    }
                    // [ORIGINAL] SemiUninterrupt: blocks movement but not attacks.
                    // We treat it as uninterrupt for the movement gate (Step 1.4).
                    if (!is_uninterrupt_ && semi_end >= 0) {
                        int start = semi_start - 1;
                        int end = semi_end - 1;
                        if (current_frame >= start && current_frame <= end) {
                            is_uninterrupt_ = true;
                        }
                    }
                    // [ORIGINAL] SelfUninterrupt: blocks everything except same-move chains.
                    if (!is_uninterrupt_ && self_start >= 0) {
                        int start = self_start - 1;
                        int end = self_end > 0 ? self_end - 1 : 9999;
                        if (current_frame >= start && current_frame <= end) {
                            is_uninterrupt_ = true;
                        }
                    }
                }
            }
        }
    }

    // After update_animation, switch to idle if requested.
    // This ensures the previous animation's final displacement is applied.
    // [FIX] If a direction key is already pressed, skip the one-frame idle
    // and start the new step immediately. This eliminates the visual jerk
    // when rapidly tapping A/D (the original has no idle gap between steps).
    if (need_switch_to_idle_) {
        need_switch_to_idle_ = false;
        if (start_stance_playing_) {
            // [A6] Start-stance hold (SOAK_TRIAGE.md A6): the original
            // holds the stance pose until the player's first input (round
            // stage Je=1 StartStance -> Je=2 Fight — the soak showed the
            // stance dropping to stance_idle right after the animation).
            // The finished non-looping stance clamps to its final frame and
            // the render keeps the pose; the exit check in the input
            // section below breaks the hold on the first input.
        } else {
        // Check if a direction key is held — if so, skip idle and let the
        // step-start logic (above, next frame) fire immediately. We still
        // need one frame in idle state for the step logic to trigger, but
        // we avoid playing the idle ANIMATION (which causes the visual jerk).
        bool dir_held = false;
        {
            bool kf = facing_right_ ? input.keys_down[(size_t)plat::Key::D] || input.keys_down[(size_t)plat::Key::ArrowRight]
                                    : input.keys_down[(size_t)plat::Key::A] || input.keys_down[(size_t)plat::Key::ArrowLeft];
            bool kb = facing_right_ ? input.keys_down[(size_t)plat::Key::A] || input.keys_down[(size_t)plat::Key::ArrowLeft]
                                    : input.keys_down[(size_t)plat::Key::D] || input.keys_down[(size_t)plat::Key::ArrowRight];
            dir_held = (kf || kb) && !input.keys_down[(size_t)plat::Key::S];
        }
        if (!dir_held) {
            play_animation("stance_idle", true, 0);  // priority 0: idle (always interruptible)
        }
        // If dir_held, we stay in move_state_ == 0 but do NOT play idle anim.
        // The step-start logic at the top of the step block will fire next
        // frame (move_state_ == 0 && key_forward/back), starting the new step
        // without any visible idle pose in between.
        }
    }

    // === HIT DETECTION ===
    // hit_anim_ countdown already done above (before special move exit).
    // Only do hit detection here if hit_anim_ is still > 0.
    // Reset hit_this_interval_ at the START of each attack animation (when current_move_
    // changes), not just when hit_anim_ reaches 0.
    // === HIT DETECTION (from moves.xml intervals) ===
    // Original game logic (from moves.xml + s3e disassembly):
    //
    // 1. Each move has an Attack interval: <Interval Type="Attack" Start="4" End="5">
    //    This defines WHICH FRAMES the attack is active.
    //
    // 2. During Attack interval, the game checks collision between
    //    AttackingParts (edges like EForearm_2, EHand_2, EFingers_2)
    //    and the enemy's Collisible edges (edges with Collisible="1").
    //
    // 3. Collision = distance between edge endpoints < threshold.
    //    Each attacking edge has two endpoints (End1, End2 from skeleton).
    //    Both endpoints are checked against ALL enemy collisible edges.
    //
    // 4. On collision: apply Damage, Impulse, and play Hit effect.
    //    The hit is PER-FRAME — each frame in the Attack interval can
    //    register a separate hit. There is NO "hit_this_interval_" flag in the
    //    original. The original checks collision every frame during
    //    the attack interval.
    //
    // 5. To prevent multiple hits per frame, the original uses
    //    Invulnerable interval on the TARGET (not the attacker).
    //    After being hit, the target becomes Invulnerable for N frames.
    //
    // We implement: check collision EVERY frame during Attack interval.
    // Apply impulse only if the bag wasn't already hit THIS FRAME.
    // Reset hit state when leaving the Attack interval (so the next
    // attack interval frame can hit again).

    // IMPORTANT: Clear current_move_ when attack ends, regardless of
    // whether assets_->bag_model() exists. Otherwise current_move_ stays set
    // from a previous attack, causing 3key combos to trigger on the
    // next key press (e.g., DoubleSweep after a single P press).
    if (hit_anim_ > 0 && assets_->bag_model() && location_) {
        auto anim_it = assets_->animations().find(current_anim_);
        if (anim_it != assets_->animations().end()) {
            int fc = anim_it->second.frame_count;
            int current_frame = (int)(anim_time_ * anim_fps_);
            auto move_it = assets_->moves().find(current_move_);
            // [DIAGNOSTIC] Per-frame hit-detection state log for
            // "hit without animation" diagnosis (Task 2).
            {
                std::string expected_anim;
                bool anim_match = false;
                if (move_it != assets_->moves().end()) {
                    expected_anim = move_it->second.filename;
                    if (expected_anim.size() > 4 && expected_anim.substr(expected_anim.size()-4) == ".bin")
                        expected_anim = expected_anim.substr(0, expected_anim.size()-4);
                    anim_match = (expected_anim == current_anim_);
                }
            }
            if (move_it != assets_->moves().end() && move_it->second.attack_start > 0) {
                int attack_start = move_it->second.attack_start;
                int attack_end = move_it->second.attack_end > 0 ?
                               move_it->second.attack_end : attack_start;
                int frame_start = attack_start - 1;
                int frame_end = attack_end - 1;
                // Check if we're in the attack interval
                bool in_attack_interval = (current_frame >= frame_start && current_frame <= frame_end);

                // Reset hit_this_interval_ when NOT in attack interval (allows re-hit
                // when entering the interval again, e.g., for multi-hit moves)
                if (!in_attack_interval) {
                    hit_this_interval_ = false;
                }

                if (in_attack_interval && dump_state_) {
                    // Where the attacking limb actually is, next to where the
                    // bag actually is. Printed rather than drawn so it can be
                    // read out of a scripted trace.
                    float bx0 = 0, bx1 = 0, by0 = 0, by1 = 0;
                    bool any = false;
                    for (const auto& [bn, bv] : bag_verlet_) {
                        (void)bn;
                        if (!any) { bx0 = bx1 = bv.x; by0 = by1 = bv.y; any = true; }
                        bx0 = std::min(bx0, bv.x); bx1 = std::max(bx1, bv.x);
                        by0 = std::min(by0, bv.y); by1 = std::max(by1, bv.y);
                    }
                    auto pv = assets_->skeleton_nodes().find("NPivot");
                    float ply = pv != assets_->skeleton_nodes().end() ? pv->second.y
                                                                     : stance_npivot_y_;
                    // Furthest reach across ALL attacking edges and both of
                    // their endpoints — printing only the first edge's End1
                    // (the elbow) understates the reach by the whole forearm.
                    float best_x = 0.0f, best_y = 0.0f, best_r = 0.0f;
                    std::string best_node;
                    bool have = false;
                    for (const auto& en : move_it->second.attack_edges) {
                        auto se = assets_->skeleton_edges().find(en);
                        if (se == assets_->skeleton_edges().end()) continue;
                        for (const std::string& nn : {se->second.end1, se->second.end2}) {
                            if (nn.empty() || !anim_node_pos_.count(nn)) continue;
                            auto [lx, ly] = resolve_body_node(nn, player_pos_x_,
                                                              player_pos_y_ + y_adjust_smoothed_,
                                                              facing_right_, ply);
                            const bool further = facing_right_ ? (lx > best_x) : (lx < best_x);
                            if (!have || further) {
                                best_x = lx; best_y = ly; best_r = se->second.radius;
                                best_node = nn; have = true;
                            }
                        }
                    }
                    if (have) {
                        const float gap = facing_right_ ? (bx0 - best_x) : (best_x - bx1);
                        std::printf("[ATK] f=%llu frame=%d reach=%s at (%.0f,%.0f) r=%.1f "
                                    "bag=[%.0f..%.0f, %.0f..%.0f] gapX=%.0f px=%.0f\n",
                                    (unsigned long long)total_frame_count_, current_frame,
                                    best_node.c_str(), best_x, best_y, best_r,
                                    bx0, bx1, by0, by1, gap, player_pos_x_);
                    }
                }
                if (in_attack_interval && !hit_this_interval_) {
                    // [REPLACED] Skeleton-based hit detection on enemy fighter.
                    // Uses attacking part nodes (from moves.xml AttackingParts)
                    // vs enemy body collision capsules (from body.xml).
                    // The attacking edge endpoints' world positions are checked
// against the enemy's body capsule segments using segment-
// segment closest distance, same algorithm as bag collision.
// [P2] Battle damage path: a real hit is a collision between the attacker's
// attack edges and the ENEMY FIGHTER's model edges (body.xml + head.xml
// capsules — LIVE_GAME_EVIDENCE Q2-C). The old code applied damage as a
// side-effect of the PUNCHING BAG collision (the bag hangs at the enemy
// spawn, so "hitting the enemy" meant hitting the bag — every battle HIT!
// line logged bag_edge= and battle damage only connected at point-blank).
// The whole computation below moved here from the bag branch; the bag branch
// now runs only in the dojo (!show_enemy_).
auto apply_player_damage_to_enemy = [&](float hit_x, float hit_y,
                                        int hit_frame) -> float {
    player_fighter_.hits_landed++;
    // [ORIGINAL] Combo.Time = 90 frames = 1.5s at 60Hz (from InternalSettings)
    combo_timer_ = 1.5f;
    std::printf("[COMBAT] Combo: hits=%d timer=1.5s\n", player_fighter_.hits_landed);
    // [ORIGINAL] Damage via Model::getTotalDamage @ game+0x4527B4
    // (engine/game/damage_formula.hpp — the multiplication order
    // base*f2*f1*f3*add is preserved there; called, never
    // reimplemented). Caller side of IntervalAttack::getFactors.
    //
    // [ORIGINAL] AverageBaseDamage from internalSettings.xml (parsed at load time)
    // This is the fallback when a move has no explicit Damage value.
    const auto& dmg_settings = assets_->damage_settings();
    float base_damage = move_it->second.damage;
    if (base_damage <= 0.0f) base_damage = dmg_settings.average_base_damage;

    // [Wave 11A M4] CRITICAL-HIT ROLL (FUN_8f4aa998 @ game+0x3F3998):
    // rolled per landed hit when NOT blocked (hit+0x1C2 == 0) and the
    // attacking move carries no NoCritical flag (move+0x4C). chance = attr
    // (CriticalChance) * crit_probability_base (internalSettings.xml
    // <CriticalHit><Probability Base="0.0001">; the stages.xml Default
    // template seeds CriticalChance=1000 -> 10%). [HEURISTIC-TODO] the
    // binary's 5-PRNG-draw distribution (K*(r1/r2 + (r3/r4)/r5) <
    // chance*K @ FUN_8f264674) is approximated with a single uniform draw.
    bool is_critical = false;
    if (!enemy_fighter_.is_blocking &&
        !(move_it != assets_->moves().end() && move_it->second.no_critical)) {
        const float chance =
            player_fighter_.attributes.get_or("CriticalChance", 0.0f) *
            dmg_settings.crit_probability_base;
        const float roll = (float)(std::rand() % 10000) / 10000.0f;
        is_critical = roll < chance;
        std::printf("[CRIT] f=%llu attacker=player chance=%.4f roll=%.4f crit=%d\n",
                    (unsigned long long)total_frame_count_, chance, roll,
                    (int)is_critical);
    }
    // [Wave 11A M4] CRIT DAMAGE MULTIPLIER (FUN_8f4a95a8 @ game+0x3F25A8):
    // 2^(crit_damage_base * attr(CriticalDamage)) — CriticalDamage=0 -> 1.0.
    const float crit_mult = is_critical
        ? std::pow(2.0f,
                   dmg_settings.crit_damage_base *
                       player_fighter_.attributes.get_or("CriticalDamage", 0.0f))
        : 1.0f;
    dbg_last_crit_factor_ = crit_mult;

    // [ORIGINAL] Attribute pairing per the game+0x60DF98 helper:
    // melee damage attribute vs the defender's BodyDefense — weapon
    // hits read WeaponDamage, fists read UnarmedDamage.
    // [P10] The move's own <Damage Type=".."> wins when authored (moves.xml
    // declares UnarmedDamage for the fists moves), and its Shift is added to
    // the attribute before the difference ("DamageAttribute(+Shift) ->
    // DefenseAttribute" — GOLDEN_TESTS.md GAP-3 capture plan).
    // [HEURISTIC-TODO] HeadDefense by hit zone when move data carries
    // it (no hit-zone field on MoveDef today).
    std::string dmg_attr = move_it->second.damage_attr.empty()
        ? ((equipped_weapon_ != "Fists") ? "WeaponDamage" : "UnarmedDamage")
        : move_it->second.damage_attr;

    DamageInputs din;
    // base = 2^(attr*w): DamageFactor absent in MVP -> get_or 0 ->
    // base 1.0f. get_or, NEVER raw get(): the -1e35f getParameter
    // sentinel must never reach powf (game+0x60DF98 defaults 0.0).
    din.base_attribute = player_fighter_.attributes.get_or("DamageFactor", 0.0f);
    din.base_weight = dmg_settings.damage_factor_base;
    // [HEURISTIC-TODO] f1/f2 selector terms (game+0x4A94F0 /
    // game+0x4A95A8) stay disabled-neutral (1.0f) — factor-set data
    // not yet ported. crit stays 1.0f (CriticalChance/CriticalDamage
    // system, internalSettings.xml L560-563, not yet ported).
    din.attribute_difference =
        attribute_difference(player_fighter_.attributes, dmg_attr,
                             enemy_fighter_.attributes, "BodyDefense")
        + static_cast<float>(move_it->second.damage_attr_shift);  // [P10] DamageAttribute(+Shift)
    din.hit_damage = base_damage;      // original's hit[0x48]
    din.enemy_damage_bonus = 0.0f;     // original's enemy[0x774] — not ported

    // [ORIGINAL] Block factor: base_block_factor from binary @ 0x101598c0
    // Binary ref: BlockChance at 0x10242aa2
    // When blocking, damage is reduced by base_block_factor (default 0.5 = 50% reduction).
    // BlockDamageFactor attribute can further reduce chip damage.
    float block_factor = enemy_fighter_.is_blocking ? dmg_settings.base_block_factor : 1.0f;

    // [ORIGINAL] Check if move ignores block (from IntervalAttack +0x75)
    if (enemy_fighter_.is_blocking && move_it->second.ignores_block) {
        block_factor = 1.0f;
        std::printf("[COMBAT] Player hit enemy: IGNORES BLOCK\n");
    }

    // [ORIGINAL] EXPLICIT DELETION (phase 4 step 9): the old
    // raw_damage line ended with a trailing `* 2.0f` — the
    // double-counted power base. 2.0 is the powf base INSIDE
    // get_total_damage, not a trailing multiplier; keeping both
    // doubled all damage at neutral attributes. Do NOT re-add it —
    // the halved damage below is the intended fidelity correction.
    const float final_damage = get_total_damage(din) * block_factor * crit_mult;

    // Store for F1 debug overlay (COMBAT panel damage breakdown)
    dbg_last_base_damage_ = base_damage;
    dbg_last_attr_mult_ = attribute_difference_factor(din.attribute_difference);  // the f3 term (game+0x60E794)
    dbg_last_block_factor_ = block_factor;
    dbg_last_attack_factor_ = 1.0f;   // f1 term disabled-neutral (not ported)
    // dbg_last_crit_factor_ set above (the crit multiplier)
    dbg_last_factor_set_ = 1.0f;      // f2 term disabled-neutral (not ported)
    dbg_last_final_damage_ = final_damage;
    dbg_last_move_name_ = move_it->first;

    std::printf("[COMBAT] Player hit enemy: base=%.3f attrdiff=%.1f f3=%.3f blk=%.2f crit=%d mult=%.4f => final=%.3f\n",
                base_damage, din.attribute_difference,
                dbg_last_attr_mult_, block_factor, (int)is_critical,
                crit_mult, final_damage);

    // [ORIGINAL] Magic charge on every landed hit (Fight::applyHit @
    // 0x8F420F9C): BOTH fighters charge, gated by the ATTACKING move's
    // NoMagicRecharge flag (MoveDef+0x148 via FUN_8f47d378; moves.xml
    // RangedMissile/MagicMissile/RaidMissile/MagicAcidCloud suppress it).
    // [Wave 11A M4] is_critical comes from the crit roll above; the crit
    // pow2 factor (FUN_8f4a95a8) is 2^0=1.0 with zero-fallback attrs.
    {
        const auto atk_it = assets_->moves().find(current_move_);
        const bool no_magic_recharge =
            (atk_it != assets_->moves().end()) &&
            atk_it->second.no_magic_recharge;
        if (!no_magic_recharge) {
            add_magic_charge(player_fighter_, base_damage,
                             enemy_fighter_.is_blocking, is_critical,
                             0, total_frame_count_, "player");
            add_magic_charge(enemy_fighter_, base_damage,
                             enemy_fighter_.is_blocking, is_critical,
                             1, total_frame_count_, "enemy");
        }
    }
    if (is_battle_mode_) {
        enemy_fighter_.health -= final_damage * enemy_fighter_.max_health;
        if (enemy_fighter_.health <= 0.0f) {
            enemy_fighter_.health = 0.0f;
            enemy_fighter_.is_dead = true;
        }
    }

    // [Soak-fix Wave 9A] F1/F2: the full hit feedback — the enemy's
    // hit-reaction animation (moves.xml Recoil by the attack's <Hit Name>
    // zone), the real impact sound (hit1-6/super_hit1-5), the reversed
    // authored <Impulse X> knockback over the reaction, and the defender's
    // fight-memory damage-event feed. The dojo sparring partner reacts too
    // (only health is battle-only).
    apply_player_hit_feedback(hit_x, hit_y, hit_frame, current_move_,
                              final_damage, enemy_fighter_.is_blocking,
                              is_critical);
    return final_damage;
};
if (show_enemy_ && enemy_fighter_.invuln_time <= 0 &&
    (assets_->enemy_body_model() || assets_->body_model())) {
                        auto pivot_it = assets_->skeleton_nodes().find("NPivot");
                        float pivot_ly = pivot_it != assets_->skeleton_nodes().end()
                                             ? pivot_it->second.y : stance_npivot_y_;

                        // Build enemy skeleton node positions from enemy animation
                        // (same approach as render_enemy_fighter)
                        std::string enemy_anim_name = enemy_anim_;
                        if (enemy_anim_name.size() > 4 &&
                            enemy_anim_name.substr(enemy_anim_name.size() - 4) == ".bin")
                            enemy_anim_name = enemy_anim_name.substr(0, enemy_anim_name.size() - 4);
                        auto enemy_anim_it = assets_->animations().find(enemy_anim_name);
                        int enemy_frame = 0, enemy_next = 0;
                        float enemy_alpha = 0;
                        bool has_enemy_anim = (enemy_anim_it != assets_->animations().end() &&
                                               enemy_anim_it->second.frame_count > 0);
                        if (has_enemy_anim) {
                            auto& anim = enemy_anim_it->second;
                            float f = enemy_anim_time_ * 20.0f;
                            if (f < 0) f = 0;
                            int fi = (int)f;
                            if (fi < 0) fi = 0;
                            enemy_frame = anim.frame_count > 0 ? fi % anim.frame_count : 0;
                            enemy_next = (enemy_frame + 1) % anim.frame_count;
                            enemy_alpha = f - (int)f;
                        }

                        // Build enemy node position map (local coords, relative to NPivot)
                        // [H08] The .bin animations store ABSOLUTE coordinates; the
                        // AnimationPlayer law converts to pivot-relative by subtracting
                        // the frame's NPivot (ix - npivot_x, iy - npivot_y). The old
                        // copy used them raw, which put the animated enemy body ~330
                        // units off and silently broke the hit test in real fights.
                        std::unordered_map<std::string, std::pair<float, float>> enemy_node_pos;
                        if (has_enemy_anim) {
                            auto& anim = enemy_anim_it->second;
                            auto& names = assets_->ordered_node_names();
                            int npivot_idx = -1;
                            for (int i = 0; i < (int)names.size(); ++i)
                                if (names[i] == "NPivot") { npivot_idx = i; break; }
                            float npx = 0, npy = 0;
                            if (npivot_idx >= 0) {
                                float z0 = 0;
                                anim.get_node_pos(enemy_frame, npivot_idx, npx, npy, z0);
                            }
                            for (int i = 0; i < (int)names.size() && i < 67; ++i) {
                                float x0, y0, z0, x1, y1, z1;
                                if (anim.get_node_pos(enemy_frame, i, x0, y0, z0) &&
                                    anim.get_node_pos(enemy_next, i, x1, y1, z1)) {
                                    enemy_node_pos[names[i]] = {
                                        x0 + (x1 - x0) * enemy_alpha - npx,
                                        y0 + (y1 - y0) * enemy_alpha - npy};
                                }
                            }
                        }

                        // Resolve enemy node to world coordinates
                        // [H08] Two coordinate laws: ANIMATION positions are
                        // pivot-relative (ix - npivot, iy - npivot) so the
                        // world pivot sits AT enemy_y; SKELETON/body rest
                        // positions are rest-relative (y - pivot_ly).
                        auto resolve_enemy_node = [&](const std::string& name)
                            -> std::pair<float, float> {
                            float lx = 0, ly = 0;
                            bool from_anim = false;
                            bool found = false;

                            // Try animated position from enemy's animation
                            auto eit = enemy_node_pos.find(name);
                            if (eit != enemy_node_pos.end()) {
                                lx = eit->second.first;
                                ly = eit->second.second;
                                from_anim = true;
                                found = true;
                            }

                            // Fallback to skeleton rest position
                            if (!found) {
                                auto skel_it = assets_->skeleton_nodes().find(name);
                                if (skel_it != assets_->skeleton_nodes().end()) {
                                    lx = skel_it->second.x;
                                    ly = skel_it->second.y;
                                    found = true;
                                }
                            }

                            // Fallback to body model rest position
                            if (!found && assets_->body_model()) {
                                auto bit = assets_->body_model()->nodes.find(name);
                                if (bit != assets_->body_model()->nodes.end()) {
                                    lx = bit->second.x;
                                    ly = bit->second.y;
                                    found = true;
                                }
                            }

                            if (!found) return {enemy_pos_x_, enemy_pos_y_};

                            // Apply enemy world transform
                            float wx = enemy_pos_x_ + (enemy_facing_right_ ? lx : -lx);
                            float wy = (enemy_pos_y_ + enemy_y_adjust_) +
                                       (from_anim ? ly : ly - pivot_ly);
                            return {wx, wy};
                        };

                        // Build edge lookup (body_model edges + skeleton edges)
                        // [R2] The defender is the ENEMY fighter's own model
                        // (stages.xml template -> list.xml Model, loaded by
                        // load_enemy_fighter_models): its capsules are the
                        // hit target (Q2-C). Falls back to the player's body
                        // model when the enemy has none (generic test enemy,
                        // dojo sparring) — the pre-R2 behavior.
                        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
                        const resf2::game::BodyModel* enemy_body =
                            assets_->enemy_body_model()
                                ? assets_->enemy_body_model().get()
                                : assets_->body_model().get();
                        for (auto& e : enemy_body->edges)
                            edge_map[e.name] = {e.end1, e.end2};
                        for (auto& [name, e] : assets_->skeleton_edges())
                            edge_map[name] = {e.end1, e.end2};

                        // For each attacking edge, check collision vs enemy body capsules
                        bool enemy_hit = false;
                        for (auto& edge_name : move_it->second.attack_edges) {
                            if (edge_name.empty() || enemy_hit) break;

                            // Get attacking edge endpoints in world space (player)
                            // [R4] The attacker side resolves against the
                            // ATTACKER's model (LIVE_GAME_EVIDENCE Q2-A/B):
                            // unarmed moves name the fighter BODY's skeleton
                            // edges (EForearm_2 ...), weapon moves name the
                            // WEAPON model's edges (WEAPON_SWORDS-Blade_2,
                            // WEAPON_KNIVES-Edge17_1 ...). The old lookup
                            // only checked skeleton_edges, so EVERY weapon
                            // edge fell into the wrist guess with radius 0
                            // — machete/sword attacks could not connect
                            // except at point-blank (re-soak-4: "удары
                            // мачете не наносят урон").
                            auto skel_edge = assets_->skeleton_edges().find(edge_name);
                            std::string atk_n1, atk_n2;
                            bool weapon_edge = false;
                            float weapon_edge_radius = 0.0f;
                            if (skel_edge != assets_->skeleton_edges().end()) {
                                atk_n1 = skel_edge->second.end1;
                                atk_n2 = skel_edge->second.end2;
                            } else if (assets_->weapon_model()) {
                                // Weapon-model edge (Q2-B): endpoints are
                                // the weapon's own MacroNodes — resolved
                                // with the same LCC vertex law the render
                                // uses, radius from the weapon edge.
                                for (const auto& we : assets_->weapon_model()->edges) {
                                    if (we.name != edge_name) continue;
                                    atk_n1 = we.end1;
                                    atk_n2 = we.end2;
                                    weapon_edge = true;
                                    weapon_edge_radius = we.radius;
                                    break;
                                }
                                if (atk_n1.empty()) {
                                    if (edge_name.find("Foot") != std::string::npos ||
                                        edge_name.find("Calf") != std::string::npos ||
                                        edge_name.find("Leg") != std::string::npos) {
                                        atk_n1 = "NToe_1"; atk_n2 = "NAnkle_1";
                                    } else {
                                        atk_n1 = "NWrist_1"; atk_n2 = "NKnuckles_1";
                                    }
                                }
                            } else {
                                if (edge_name.find("Foot") != std::string::npos ||
                                    edge_name.find("Calf") != std::string::npos ||
                                    edge_name.find("Leg") != std::string::npos) {
                                    atk_n1 = "NToe_1"; atk_n2 = "NAnkle_1";
                                } else {
                                    atk_n1 = "NWrist_1"; atk_n2 = "NKnuckles_1";
                                }
                            }

                            float atk1_wx, atk1_wy, atk2_wx, atk2_wy;
                            if (weapon_edge && assets_->weapon_model()) {
                                const float wdir = facing_right_ ? 1.0f : -1.0f;
                                if (!resolve_player_weapon_vertex(
                                        *assets_->weapon_model(), atk_n1,
                                        player_pos_x_,
                                        player_pos_y_ + y_adjust_smoothed_,
                                        wdir, pivot_ly, true, atk1_wx, atk1_wy) ||
                                    !resolve_player_weapon_vertex(
                                        *assets_->weapon_model(), atk_n2,
                                        player_pos_x_,
                                        player_pos_y_ + y_adjust_smoothed_,
                                        wdir, pivot_ly, true, atk2_wx, atk2_wy))
                                    continue;  // unresolvable weapon anchor
                            } else {
                                auto [ax, ay] = resolve_body_node(
                                    atk_n1, player_pos_x_,
                                    player_pos_y_ + y_adjust_smoothed_,
                                    facing_right_, pivot_ly);
                                auto [bx, by] = resolve_body_node(
                                    atk_n2, player_pos_x_,
                                    player_pos_y_ + y_adjust_smoothed_,
                                    facing_right_, pivot_ly);
                                atk1_wx = ax; atk1_wy = ay;
                                atk2_wx = bx; atk2_wy = by;
                            }

                            float atk_radius = 0;
                            if (skel_edge != assets_->skeleton_edges().end())
                                atk_radius = skel_edge->second.radius;
                            else if (weapon_edge)
                                atk_radius = weapon_edge_radius;

                            // Check against each enemy body capsule (the
                            // ENEMY's own model, plus its head model when
                            // the template names one - e.g. HEAD_DISCIPLE).
                            std::vector<const resf2::game::BodyCapsule*> enemy_capsules;
                            for (const auto& c : enemy_body->capsules)
                                enemy_capsules.push_back(&c);
                            if (assets_->enemy_head_model()) {
                                for (const auto& c : assets_->enemy_head_model()->capsules)
                                    enemy_capsules.push_back(&c);
                            }
                            for (auto* capsule : enemy_capsules) {
                                auto cap_edge_it = edge_map.find(capsule->edge_name);
                                if (cap_edge_it == edge_map.end()) continue;

                                auto [en1_wx, en1_wy] = resolve_enemy_node(cap_edge_it->second.first);
                                auto [en2_wx, en2_wy] = resolve_enemy_node(cap_edge_it->second.second);

                                float body_r = (capsule->radius1 + capsule->radius2) * 0.5f;
                                if (body_r <= 0) body_r = 4.0f;  // default capsule radius

                                // Segment-segment closest distance
                                // Attacking segment: (atk1) -> (atk2)
                                // Body capsule segment: (en1) -> (en2)
                                float ex = atk2_wx - atk1_wx, ey = atk2_wy - atk1_wy;
                                float fx = en2_wx - en1_wx, fy = en2_wy - en1_wy;
                                float gx = atk1_wx - en1_wx, gy = atk1_wy - en1_wy;
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
                                float qx = en1_wx + t * fx, qy = en1_wy + t * fy;
                                float rx = px - qx, ry = py - qy;
                                float sq_dist = rx * rx + ry * ry;
                                float threshold = atk_radius + body_r;

                                if (sq_dist < threshold * threshold) {
                                    // [ORIGINAL] Skeleton-based hit confirmed:
                                    // the attacker's attack edge connects with
                                    // the ENEMY FIGHTER's model edge (body.xml /
                                    // head.xml capsule — LIVE_GAME_EVIDENCE Q2-C:
                                    // EChest/EHead/EArm_1/ECalf_1...). This is a
                                    // real fight hit, so the interval is consumed
                                    // here (hit_this_interval_) and the damage is
                                    // applied HERE — not as a side-effect of the
                                    // punching-bag collision (the P2 defect: the
                                    // bag hangs at the enemy spawn, so battle
                                    // damage was gated on hitting the bag).
                                    enemy_fighter_.invuln_time = 0.4f;
                                    enemy_hit_flash_ = 0.25f;
                                    // Spawn hit sparks at collision point
                                    float hit_x = (px + qx) * 0.5f;
                                    float hit_y = (py + qy) * 0.5f;
                                    spawn_hit_sparks(hit_x, hit_y, 10);
                                    debug_log("[HIT] f=%llu move='%s' hit enemy capsule=%s sq_dist=%.1f thresh=%.1f atk_edge=%s\n",
                                        (unsigned long long)total_frame_count_, current_move_.c_str(),
                                        capsule->edge_name.c_str(), sq_dist, threshold * threshold,
                                        edge_name.c_str());
                                    hit_this_interval_ = true;
                                    // [Soak-fix Wave 9A] F1: the old inline
                                    // swing-voice + "armor" plays moved into
                                    // apply_player_damage_to_enemy ->
                                    // apply_player_hit_feedback (the real
                                    // impact sound plays there; the swing
                                    // voice already plays at attack start).
                                    apply_player_damage_to_enemy(hit_x, hit_y, current_frame);
                                    enemy_hit = true;
                                    break;
                                }
                            }
                        }
                    }
                    // [R2] Distance-based fallback for the ENEMY fighter
                    // (mirrors the dojo bag fallback below): the precise
                    // edge test needs the fist nearly touching the enemy
                    // model — the attack interval (HighPunch frames 4-5)
                    // fires early in the swing, so the hand is rarely at
                    // full reach when it runs, and hits only landed at
                    // point-blank (the re-soak-3 "не могу наносить урон
                    // противнику" report — the enemy's own attack range
                    // test uses the same move-tactic law and hit the
                    // player from mid-range). The reach is the move's
                    // authored tactic distance (moves.xml <Distance Max=>,
                    // HighPunch Max=250 — the same range the enemy's
                    // attack applies against the player).
                    if (show_enemy_ && !hit_this_interval_ &&
                        enemy_fighter_.invuln_time <= 0) {
                        const float reach = move_it->second.has_distance_cond
                            ? move_it->second.distance_max : 250.0f;
                        const float dist = std::fabs(player_pos_x_ - enemy_pos_x_);
                        if (dist <= reach) {
                            enemy_fighter_.invuln_time = 0.4f;
                            enemy_hit_flash_ = 0.25f;
                            const float mid_x = (player_pos_x_ + enemy_pos_x_) * 0.5f;
                            spawn_hit_sparks(mid_x, enemy_pos_y_ + 30.0f, 10);
                            debug_log("[HIT] f=%llu move='%s' enemy DISTANCE fallback dist=%.0f reach=%.0f\n",
                                (unsigned long long)total_frame_count_, current_move_.c_str(),
                                dist, reach);
                            hit_this_interval_ = true;
                            // [Soak-fix Wave 9A] F1: impact sound moved into
                            // the feedback path (see the capsule branch).
                            apply_player_damage_to_enemy(mid_x, enemy_pos_y_ + 30.0f, current_frame);
                        }
                    }
                    // Determine attacking limb from AttackingParts in moves.xml
                    // Each AttackingParts Edge has End1 and End2 in skeleton.xml
                    // We check ALL attacking edges, not just one
                    bool hit_registered = false;
                    for (auto& edge_name : move_it->second.attack_edges) {
                        if (edge_name.empty()) continue;
                        // Look up edge in skeleton to get End1/End2
                        auto skel_edge = assets_->skeleton_edges().find(edge_name);
                        std::string node1, node2;
                        if (skel_edge != assets_->skeleton_edges().end()) {
                            node1 = skel_edge->second.end1;
                            node2 = skel_edge->second.end2;
                        } else {
                            // Fallback: guess from edge name
                            if (edge_name.find("Foot") != std::string::npos ||
                                edge_name.find("Calf") != std::string::npos ||
                                edge_name.find("Leg") != std::string::npos) {
                                node1 = "NToe_1"; node2 = "NAnkle_1";
                            } else {
                                node1 = "NWrist_1"; node2 = "NKnuckles_1";
                            }
                        }

                        // Check collision for both endpoints of this edge
                        for (int endpoint = 0; endpoint < 2; endpoint++) {
                            std::string& limb_node = (endpoint == 0) ? node1 : node2;
                            if (limb_node.empty()) continue;
                            auto ait = anim_node_pos_.find(limb_node);
                            if (ait == anim_node_pos_.end()) continue;

                            // Use the SAME transform the renderer uses. This
                            // block used to carry its own copy of it —
                            //   limb_wy = player_pos_y + y_adjust + (ly - pivot_ly)
                            // — which is the double subtraction of the rest
                            // pivot fixed in PORT_PLAN 3.1. The limbs were
                            // therefore tested ~170 world units below where
                            // they were drawn and never reached the bag: the
                            // fighter could stand on top of it, punch, and
                            // bag_hit stayed 0.
                            auto pivot_it = assets_->skeleton_nodes().find("NPivot");
                            float pivot_ly = pivot_it != assets_->skeleton_nodes().end()
                                                 ? pivot_it->second.y : stance_npivot_y_;
                            auto [limb_wx, limb_wy] = resolve_body_node(
                                limb_node, player_pos_x_,
                                player_pos_y_ + y_adjust_smoothed_,
                                facing_right_, pivot_ly);

                            // Get attacking edge radius from skeleton
                            float atk_radius = 0;
                            auto skel_it = assets_->skeleton_edges().find(edge_name);
                            if (skel_it != assets_->skeleton_edges().end()) {
                                atk_radius = skel_it->second.radius;
                            }

                            // Get world-space positions of both attacking edge endpoints
                            auto ait2 = anim_node_pos_.find(node2);
                            if (ait2 == anim_node_pos_.end()) continue;

                            auto [limb2_wx, limb2_wy] = resolve_body_node(
                                node2, player_pos_x_,
                                player_pos_y_ + y_adjust_smoothed_,
                                facing_right_, pivot_ly);

                            // [P2] The punching bag is the DOJO's collision
                            // target only (LIVE_GAME_EVIDENCE Q2-D: the bag
                            // exists solely for training). In a battle vs the
                            // enemy fighter the hit test is the enemy-capsule
                            // block above; this bag loop must not run there —
                            // the soak's battle HIT! lines logged bag_edge=
                            // because the bag (hung at the enemy spawn) was
                            // the collision target.
                            // Check against ALL collisible bag edges
                            bool hit_this_interval_this_frame = false;
                            if (!show_enemy_) {
                            for (auto& be : assets_->bag_model()->edges) {
                                if (!be.collisible) continue;
                                float bag_r = be.radius;
                                if (bag_r <= 0) continue;
                                if (be.end1.empty() || be.end2.empty()) continue;

                                // Get bag edge endpoints from Verlet
                                auto bv1 = bag_verlet_.find(be.end1);
                                auto bv2 = bag_verlet_.find(be.end2);
                                if (bv1 == bag_verlet_.end() || bv2 == bag_verlet_.end()) continue;

                                float be1x = bv1->second.x, be1y = bv1->second.y;
                                float be2x = bv2->second.x, be2y = bv2->second.y;

                                // Segment-segment closest distance, also returns hit ratio t along bag edge
                                float ex = limb2_wx - limb_wx, ey = limb2_wy - limb_wy;
                                float fx = be2x - be1x, fy = be2y - be1y;
                                float gx = limb_wx - be1x, gy = limb_wy - be1y;
                                float a = ex*ex + ey*ey;
                                float b = ex*fx + ey*fy;
                                float c = fx*fx + fy*fy;
                                float d = ex*gx + ey*gy;
                                float e = fx*gx + fy*gy;
                                float det = a*c - b*b;
                                float s, t;
                                if (det < 1e-12f) {
                                    s = 0.0f;
                                    t = (b > c) ? d / b : e / c;
                                    t = std::max(0.0f, std::min(1.0f, t));
                                } else {
                                    s = (b*e - c*d) / det;
                                    t = (a*e - b*d) / det;
                                    if (s < 0) { s = 0; t = e / c; t = std::max(0.0f, std::min(1.0f, t)); }
                                    else if (s > 1) { s = 1; t = (b + e) / c; t = std::max(0.0f, std::min(1.0f, t)); }
                                    else if (t < 0) { t = 0; s = -d / a; s = std::max(0.0f, std::min(1.0f, s)); }
                                    else if (t > 1) { t = 1; s = (b - d) / a; s = std::max(0.0f, std::min(1.0f, s)); }
                                }
                                float px = limb_wx + s*ex, py = limb_wy + s*ey;
                                float qx = be1x + t*fx, qy = be1y + t*fy;
                                float rx = px - qx, ry = py - qy;
                                float sq_dist = rx*rx + ry*ry;
                                float threshold = atk_radius + bag_r;
                                if (sq_dist < threshold * threshold) {
                                    std::printf("[COMBAT] HIT! move=%s frame=%d/%d [%d-%d] atk_edge=%s bag_edge=%s sq_dist=%.1f thresh=%.1f (atk_r=%.1f bag_r=%.1f)\n",
                                                current_move_.c_str(), current_frame, fc,
                                                frame_start, frame_end,
                                                edge_name.c_str(), be.name.c_str(),
                                                sq_dist, threshold*threshold, atk_radius, bag_r);
                                    // [ORIGINAL] JS: Kwb() line 15467 creates impulse H(kw,gR,hR,1)
                                    // from XML <Impulse X/Y/Z>, then strike() applies to defender physics.
                                    // Apply the impulse to the bag edge's Verlet nodes.
                                    float imp_x = move_it->second.impulse_x;
                                    float imp_y = move_it->second.impulse_y;
                                    if (dump_state_)
                                        std::printf("[COMBAT] impulse from move '%s': x=%.1f y=%.1f "
                                                    "(moves.xml <Impulse>)\n",
                                                    current_move_.c_str(), imp_x, imp_y);
                                    if (imp_x != 0 || imp_y != 0) {
                                        float dir = facing_right_ ? 1.0f : -1.0f;
                                        // Distribute impulse by hit position along edge (original Bl.strike)
                                        float hit_ratio = std::max(0.0f, std::min(1.0f, t));
                                        float dist1 = 1.0f - hit_ratio;
                                        float dist2 = hit_ratio;
                                        apply_bag_impulse(be.end1, dir * imp_x * dist1, imp_y * dist1);
                                        apply_bag_impulse(be.end2, dir * imp_x * dist2, imp_y * dist2);
                                    }
                                    hit_this_interval_ = true;
                                    hit_this_interval_this_frame = true;
                                    hit_registered = true;
                                    // The swing itself.
                                    int snd_idx = (current_frame + (int)current_move_[0]) % 4 + 1;
                                    play_sound(player_attack_sound(snd_idx), 0.8f);

                                    // What was hit decides what is heard and what
                                    // reacts. This branch is the PUNCHING BAG's
                                    // collision, and it used to play `armor` — the
                                    // sound of a blade on a fighter's armour — and
                                    // then run the whole enemy-fighter reaction:
                                    // invulnerability, a hit flash and sparks at
                                    // the enemy's position, with no enemy present.
                                    // That is why the dojo sounded like a fight
                                    // against someone while there is only a bag
                                    // hanging there.
                                    if (!show_enemy_) {
                                        // A leather bag takes a body impact, no
                                        // metal. *_pl_hit* is the body-impact
                                        // set; [S1] it follows the player's
                                        // voice. [HEURISTIC-TODO] The bag has
                                        // no Voice of its own; the original's
                                        // exact bag-impact set is unverified.
                                        const int bag_snd = (current_frame + (int)current_move_[0]) % 3 + 1;
                                        play_sound(player_hit_sound(bag_snd), 0.6f);
                                        player_fighter_.hits_landed++;
                                        // [ORIGINAL] Combo.Time = 90 frames = 1.5s at 60Hz (from InternalSettings)
                                        combo_timer_ = 1.5f;
                                        std::printf("[COMBAT] Combo: hits=%d timer=1.5s\n", player_fighter_.hits_landed);
                                        // [ORIGINAL] Tutorial trigger: after 3 bag
                                        // hits, Sensei advances to the first fight.
                                        if (tutorial_state_ == "BAG") {
                                            tutorial_bag_hits_++;
                                            std::printf("[tutorial] bag hits: %d/3\n", tutorial_bag_hits_);
                                            if (tutorial_bag_hits_ >= 3) {
                                                tutorial_state_ = "FIRST_FIGHT";
                                                check_tutorial();
                                                tutorial_dialog_pending_ = true;
                                                // [Q3] Queue the Kenji fight so
                                                // the training dialog hands
                                                // over to Battle.
                                                queue_tutorial_battle();
                                                std::printf("[tutorial] state -> FIRST_FIGHT, dialog pending\n");
                                            }
                                        }
                                    }  // closes the dojo bag-reaction branch
                                    // [P2] The enemy-fighter damage branch used
                                    // to live HERE — battle damage was applied
                                    // as a side-effect of hitting the BAG (the
                                    // bag hangs at the enemy spawn, so the bag
                                    // collision stood in for the fighter). The
                                    // whole computation moved to
                                    // apply_player_damage_to_enemy() in the
                                    // enemy-capsule hit path above; this bag
                                    // branch is dojo-only (show_enemy_ == false)
                                    // and never deals fighter damage.
                                    break;
                                }
                            }
                            }  // !show_enemy_ — bag collision is dojo-only [P2]
                            if (hit_this_interval_this_frame) break;
                        }
                        if (hit_registered) break;
                    }
                    // [Wave 11B W1] DELETED: the distance-based fallback for
                    // the tutorial bag hit counter that used to live here
                    // (game.cpp:5177-5228, mislabeled [ORIGINAL]). VERIFY_W11
                    // 2 adjudicates it an invention: the original binary has
                    // NO branch keyed on |player_x - bag_x| < 200 - no string,
                    // no constant, no distance test anywhere in the image.
                    // The original registers a bag hit ONLY on a real capsule
                    // collision (the generic hit path above) and only then
                    // applies the move's <Impulse X/Y/Z> (moves.xml) over the
                    // hit edge. The fallback counted "hits" the fist never
                    // landed ("Груша слишком сильно отталкивается, даже когда
                    // я её почти и не касаюсь") and advanced the tutorial
                    // without touching the bag. The tutorial counter now
                    // increments exclusively in the real-hit branch above.
                }
            }
        }
        if (hit_anim_ == 0 && move_state_ == 0) {
            need_switch_to_idle_ = true;
            current_move_.clear();
            hit_this_interval_ = false;  // reset for next attack
        }

        // IMPORTANT: DON'T reset move_state_ here — hit_anim_ expiring
        // (start stance countdown) should NOT interrupt step movement.
        // Allow the MOVING_FORWARD/MOVING_BACK state machine to keep
        // stepping. Only clear current_move_ so the next key press can
        // trigger a new move.
        if (hit_anim_ == 0 && move_state_ != 0 && !current_move_.empty()) {
            current_move_.clear();
            hit_this_interval_ = false;
        }
    }

    // IMPORTANT: Also clear current_move_ when hit_anim_ reaches 0
    // even if assets_->bag_model() doesn't exist (no character loaded).
    // This prevents 3key combos from triggering on the next key press
    // when current_move_ is still set from a previous attack.
    // Only force idle switch if not currently stepping.
    if (hit_anim_ == 0 && !current_move_.empty() && move_state_ == 0) {
        need_switch_to_idle_ = true;
        current_move_.clear();
        hit_this_interval_ = false;
    } else if (hit_anim_ == 0 && !current_move_.empty()) {
        // Stepping — just clear the move name, don't interrupt the step
        current_move_.clear();
        hit_this_interval_ = false;
    }

    // [ORIGINAL] SimpleEffect::update @ 0x1007f1f0 advances every effect curve
    // once per tick, scaling the 60 Hz tick count by 1/60 first — so curve
    // Periods are seconds and the animation is independent of frame rate.
    update_location_effects(dt / 1000.0f);

    // Update bag Verlet physics
    update_bag_verlet(dt / 1000.0f);

    // [Wave 10A defect 2] LOCATION BOUNDS: the fighter's world x is driven
    // by unbounded root motion (step_forward NPivot deltas), the step
    // fallbacks and the <Align> pinning — holding Forward walks the player
    // out of the location's world box (params.xml Width/2; probe: px
    // reached -1818/+1691 vs the +-980 dojo box). The wall is the location
    // boundary: the fighter may slide INTO it but never past it. (The
    // enemy side is clamped in its own update above; this is the player
    // half of the same per-frame pair.)
    // [Wave 11B W2] Same bounds source as the enemy side: the location's
    // WALL OBJECTS (params.xml left/right anchors, dojo +-680), NOT
    // +-width/2 (VERIFY_W11 3 - the width/2 clamp is an invention).
    if (location_) {
        float wall_lo = 0.0f, wall_hi = 0.0f;
        location_wall_bounds(location_, wall_lo, wall_hi);
        player_pos_x_ = std::clamp(player_pos_x_, wall_lo, wall_hi);
    }

    // Update projectiles (magic/ranged)
    update_projectiles(dt / 1000.0f);

    // Zoom presets
    if (input.keys_just_pressed[(size_t)plat::Key::F1]) {
        debug_world_ = !debug_world_;
        std::printf("[DEBUG] world overlay %s\n", debug_world_ ? "ON" : "OFF");
    }
    if (input.keys_just_pressed[(size_t)plat::Key::Num1]) zoom_ = 1.0f;
    if (input.keys_just_pressed[(size_t)plat::Key::Num2]) zoom_ = 0.7f;
    if (input.keys_just_pressed[(size_t)plat::Key::Num3]) zoom_ = 1.5f;

    // [DIAGNOSTIC] Structured frame state dump (--dump-state)
    if (dump_state_) {
        // af/fps make the animation clock observable, which is what timing
        // assertions need: moves.xml gives MidFrames (fps = 60/(1+MidFrames)),
        // FirstFrame (where playback starts) and the Attack interval in frame
        // numbers, so a test can check the engine honours all three.
        // al/anchor_x make the <Align> placement observable. anchor_x is the
        // WORLD x of the node the current animation is anchored to; while an
        // aligned animation plays it is the quantity the original holds
        // steady across an animation change (PORT_PLAN 4.3), so a test can
        // assert continuity instead of trusting the diagnostic that computes
        // it. px alone cannot show this: px is NPivot + offset, and NPivot
        // sways within the animation by design.
        int al = 0;
        float anchor_x = player_pos_x_;
        if (const MoveDef* m = current_align_move()) {
            al = 1;
            auto it = anim_node_pos_.find(m->moveinside_pivot_node);
            if (it != anim_node_pos_.end())
                anchor_x = player_pos_x_ +
                           (facing_right_ ? it->second.first : -it->second.first);
        }
        std::printf("[STATE] f=%llu ms=%d ha=%u anim='%s' move='%s' px=%.1f py=%.1f "
                    "ex=%.1f ey=%.1f eanim='%s' "
                    "af=%.2f fps=%.2f bag_hit=%d bag_move=%.2f nv=%zu "
                    "al=%d anchor_x=%.2f fx=%.2f cam=%.1f zoom=%.4f "
                    "fr=%d ef=%d\n",
                    (unsigned long long)total_frame_count_, move_state_, hit_anim_,
                    current_anim_.c_str(), current_move_.c_str(),
                    player_pos_x_, player_pos_y_,
                    enemy_pos_x_, enemy_pos_y_, enemy_anim_.c_str(),
                    anim_time_ * anim_fps_, anim_fps_,
                    (int)hit_this_interval_, bag_displacement(), bag_verlet_.size(),
                    al, anchor_x, first_effect_alpha(), cam_x_, zoom_,
                    (int)facing_right_, (int)enemy_facing_right_);
    }
}

// [Soak-fix Wave 9A] F1: full hit feedback for a registered player->enemy
// hit. The ORIGINAL plays, on impact: (a) the enemy's hit-reaction
// animation — the moves.xml Recoil move selected by the attack's <Hit Name>
// zone (High -> HighHit -> high_hit.bin, HighHeavy -> HighHitHeavy -> ...),
// (b) the pinned contact-hit sound m_/f_pl_hit2 (LIVE_INTERACTION_TRACE
// §4.3 — the soak showed only the swing voice + "armor" in battle), (c) the
// hit_blade effect (spawned by the caller at the impact point), (d) the
// knockback — the attack's authored <Impulse X> applied REVERSED (Hit
// template SetDirection Impulse Reverse=1) spread over the reaction
// duration, (e) the KO fall sound bodyfallN (§4.5) on the enemy's death.
// Also (F2) feeds the defender's fight memory (FUN_8f4b173c /
// FUN_8f4aa998 damage-event model).
void Game::apply_player_hit_feedback(float hit_x, float hit_y, int hit_frame,
                                     const std::string& move_name,
                                     float final_damage, bool blocked,
                                     bool critical) {
    // (a) Resolve the reaction animation from moves.xml: the attack's
    // <Hit Name> zone (parsed into the Attack interval's hit_type) names
    // the Recoil move — "High" -> move "HighHit" (high_hit.bin);
    // modifier zones ("HighHeavy" -> "HighHitHeavy", "MiddleShortPlus" ->
    // "MiddleHitShortPlus", ...) split the suffix. Fallback: the catalog
    // default high_hit (the pre-Wave-9A behavior). [HEURISTIC-TODO] zones
    // without a matching Recoil move (Titan*, boss zones) keep the fallback
    // until their reaction anims are pinned.
    std::string reaction_anim = "high_hit";
    std::string zone;
    const auto move_it = assets_->moves().find(move_name);
    if (move_it != assets_->moves().end()) {
        for (const auto& iv : move_it->second.intervals) {
            if (iv.type == "Attack" && !iv.hit_type.empty()) {
                zone = iv.hit_type;
                break;
            }
        }
        if (!zone.empty()) {
            const std::string base = zone + "Hit";  // "High" -> "HighHit"
            auto hit_it = assets_->moves().find(base);
            if (hit_it != assets_->moves().end() &&
                !hit_it->second.filename.empty()) {
                reaction_anim = strip_bin_suffix(hit_it->second.filename);
            } else {
                // Zone with a modifier suffix: strip it, re-append after
                // "Hit" ("HighHeavy" -> "High" + "Hit" + "Heavy").
                static const char* kMods[] = {"Heavy", "ShortPlus", "Plus",
                                              "Short", "Long", "Pull", "Deflect"};
                for (const char* mod : kMods) {
                    const std::string ms(mod);
                    if (zone.size() > ms.size() &&
                        zone.compare(zone.size() - ms.size(), ms.size(), ms) == 0) {
                        const std::string cand =
                            zone.substr(0, zone.size() - ms.size()) + "Hit" + ms;
                        auto cit = assets_->moves().find(cand);
                        if (cit != assets_->moves().end() &&
                            !cit->second.filename.empty()) {
                            reaction_anim = strip_bin_suffix(cit->second.filename);
                            break;
                        }
                    }
                }
            }
        }
    }

    // [Wave 11A M4] KNOCKDOWN: a received hit of Type=Critical (or Type=
    // Shock — the shock accumulator FUN_8f4a92bc @ game+0x3F22BC is not
    // ported, [HEURISTIC-TODO]) selects the FALL family reaction instead of
    // the plain Recoil: moves.xml HighHitFall @6375 / MiddleHitFall @6425 /
    // SweepHitFall @6470 / SpinningHitFall @6516 / OverheadHitFall @6549
    // (Template Fall = Hit|NotTitan; RockOn blocks them — no mod system in
    // the MVP, [HEURISTIC-TODO]). The fall move plays + its bodyfall sound
    // (High/Middle/Sweep -> bodyfall3, Spinning/Overhead -> bodyfall1; the
    // fall moves carry the CanWallHitFall window for the wall-bounce chain
    // into WallHitFall — wall bounce not ported, [HEURISTIC-TODO]).
    // Recovery is the StandupAfterThrowFall template (AnimationEnd
    // ThrowFall) — the fighter returns to stance when the anim ends.
    std::string fall_sound;
    if (critical && !blocked) {
        const std::string fall_name = zone + "HitFall";
        const auto fit = assets_->moves().find(fall_name);
        if (fit != assets_->moves().end() &&
            !fit->second.filename.empty()) {
            reaction_anim = strip_bin_suffix(fit->second.filename);
        } else {
            reaction_anim = "high_hit_fall";   // catalog default fall
        }
        fall_sound = (zone == "Spinning" || zone == "Overhead")
                         ? "bodyfall1" : "bodyfall3";
        std::printf("[KNOCKDOWN] f=%llu move='%s' zone='%s' fall='%s' "
                    "sound='%s'\n",
                    (unsigned long long)total_frame_count_,
                    move_name.c_str(), zone.c_str(), reaction_anim.c_str(),
                    fall_sound.c_str());
    }

    // The reaction plays for the full animation duration (the enemy renders
    // at a fixed 20fps), during which the enemy takes no decisions.
    float stun_sec = 0.6f;
    if (const auto ait = assets_->animations().find(reaction_anim);
        ait != assets_->animations().end() && ait->second.frame_count > 0) {
        stun_sec = static_cast<float>(ait->second.frame_count) / 20.0f;
    }
    enemy_reaction_anim_ = reaction_anim;
    enemy_fighter_.hit_stun_time = stun_sec;
    enemy_anim_ = reaction_anim;
    enemy_anim_time_ = 0.0f;

    // (d) Knockback: the attack's <Impulse X> (from its Attack interval),
    // reversed — the Hit template's SetDirection Impulse Reverse=1 pushes
    // the defender AWAY from the attacker — spread over the reaction so the
    // displacement reads as a shove, not a teleport.
    float imp_x = 0.0f;
    if (move_it != assets_->moves().end()) imp_x = move_it->second.impulse_x;
    const float away_dir = (player_pos_x_ < enemy_pos_x_) ? 1.0f : -1.0f;
    enemy_knockback_vx_ =
        (stun_sec > 0.0f) ? (away_dir * imp_x / stun_sec) : 0.0f;
    // [Wave 10B D1] KNOCKBACK BOUNDED BY THE ARENA BOUNDS: the wall is a
    // hard boundary - the shove may carry the enemy TO it but never past
    // it. Without this bound the whole authored <Impulse X> integrates
    // over the reaction duration even when the wall is in the way, so an
    // enemy standing near the wall is pushed INTO the clamp every frame
    // for the whole stun ("the enemy is knocked back very far and can fly
    // out of the location"). Same bounds source as the fighter clamps
    // ([Wave 11B W2] the location's WALL OBJECTS - params.xml left/right
    // anchors, dojo +-680, not +-width/2): the total slide is clamped so
    // the enemy's post-hit x stays inside the walls.
    if (location_ && stun_sec > 0.0f) {
        float wall_lo = 0.0f, wall_hi = 0.0f;
        location_wall_bounds(location_, wall_lo, wall_hi);
        const float slide = away_dir * imp_x;             // signed total slide
        const float lo = wall_lo - enemy_pos_x_;          // max leftward slide
        const float hi = wall_hi - enemy_pos_x_;          // max rightward slide
        const float bounded = std::clamp(slide, lo, hi);
        if (std::fabs(bounded - slide) > 1e-3f) {
            std::printf("[HIT-FEEDBACK] knockback bounded by arena: "
                        "slide=%.1f lo=%.1f hi=%.1f vx=%.1f -> %.1f\n",
                        slide, lo, hi, enemy_knockback_vx_,
                        bounded / stun_sec);
            enemy_knockback_vx_ = bounded / stun_sec;
        }
    }
    std::printf("[HIT-FEEDBACK] f=%llu move='%s' zone='%s' reaction='%s' "
                "stun=%.2fs imp=%.1f knockback_vx=%.1f dmg=%.3f blocked=%d\n",
                (unsigned long long)total_frame_count_, move_name.c_str(),
                zone.c_str(), reaction_anim.c_str(), stun_sec, imp_x,
                enemy_knockback_vx_, final_damage, (int)blocked);

    // (b) Impact sound: a blocked hit plays the block sound (armor.wav);
    // an unblocked hit plays the pinned contact-hit sample
    // m_pl_hit2.wav — gender-appropriate via the DEFENDER's voice set
    // (LIVE_INTERACTION_TRACE §4.3: "on a landed hit the original plays
    // the hit sample m_pl_hit2.wav (preloaded)"). The old hit1-6 /
    // super_hit1-5 heuristic was unpinned ([HEURISTIC-TODO] those samples
    // stay catalog-loaded but are not the battle contact sound).
    // [Wave 11A M4] A critical knockdown plays the fall move's OWN sound
    // (bodyfall3/bodyfall1 per the fall family) instead of the hit sound.
    if (critical && !blocked) {
        play_sound(fall_sound, 0.8f);
    } else if (blocked) {
        play_sound("armor", 0.5f);
    } else {
        play_sound(enemy_hit_sound(2), 0.8f);
    }

    // (e) KO fall: a dead enemy plays the fall sound (LIVE_INTERACTION_TRACE
    // §4.5: "enemy KO uses bodyfallN.wav") and drops to the real catalog
    // KO pose. [HEURISTIC-TODO] bodyfall1-vs-bodyfall3 and the exact KO
    // pose stay unpinned — bodyfall1 + lose_fall.bin are the defaults; the
    // stage wall (wall3 pin, §4.4) does not exist in the engine arena.
    if (enemy_fighter_.is_dead) {
        play_sound("bodyfall1", 0.7f);
        enemy_knockback_vx_ = 0.0f;
        enemy_anim_ = "lose_fall";
    }

    // (F2) The defender's fight memory: the damage-event feed keyed by the
    // ATTACKER's animation (the player's current attack anim), in the
    // enemy's (victim) memory — the probe channels the UseDefense draw
    // reads (BLOCK_LOGIC.md §1.2).
    std::string atk_anim = strip_bin_suffix(
        move_it != assets_->moves().end() ? move_it->second.filename
                                          : (move_name + ".bin"));
    combat_.mutable_enemy_tactic_memory().record_hit_taken(atk_anim,
                                                           final_damage);
}

} // namespace resf2::game

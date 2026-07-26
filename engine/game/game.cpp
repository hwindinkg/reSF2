// engine/game/game.cpp
//
// Game class implementation — extracted from game.hpp inline bodies.

#include "game.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <filesystem>

namespace resf2::game {

// Constructor
Game::Game(std::string asset_root, bool replay_mode, bool dump_state)
    : asset_root_(std::move(asset_root)), replay_mode_(replay_mode), dump_state_(dump_state) {
    discover_locations();
}

// Destructor (required for unique_ptr members with forward-declared types)
Game::~Game() = default;

void Game::play_animation(const std::string& name, bool loop, int priority) {
    auto& animations = assets_->animations();
    auto& moves = assets_->moves();
    if (animations.count(name)) {
        // Priority check: if existing animation has strictly higher priority, reject
        if (priority < priority_ && name != current_anim_) {
            std::printf("[ANIM] Rejected '%s' (priority %d) — '%s' has higher priority %d\n",
                        name.c_str(), priority, current_anim_.c_str(), priority_);
            return;
        }
        if (current_anim_ != name) {
            std::printf("[ANIM] play_animation('%s', loop=%d, prio=%d) — switching from '%s' (prio=%d)\n",
                        name.c_str(), loop, priority, current_anim_.c_str(), priority_);
        }
        priority_ = priority;
        current_anim_ = name;
        anim_time_ = 0.0f;
        anim_loop_ = loop;
        anim_fps_ = 20.0f;  // default: matches MidFrames=2 (60/3=20)

        // Look up MoveDef by filename to get mid_frames and first_frame
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
                anim_fps_ = 60.0f / (1.0f + move.mid_frames);
                if (move.first_frame >= 0 && anim_fps_ > 0.0f) {
                    anim_time_ = (float)move.first_frame / anim_fps_;
                }
                break;
            }
        }

        anim_anchor_set_ = false;
        anim_root_dx_ = 0.0f;
        anim_root_dy_ = 0.0f;
        prev_root_offset_ = 0.0f;
        committed_root_x_ = 0.0f;
        prev_root_offset_x_ = 0.0f;
        prev_root_offset_y_ = 0.0f;
        prev_npivot_set_ = false;
        prev_npivot_y_set_ = false;
        prev_frame_idx_ = -1;
        anim_facing_right_ = facing_right_;
        if (name != "jump" && name != "jump_away" &&
            name != "front_flip" && name != "back_flip" &&
            name != "back_handflip") {
            jump_y_offset_ = 0.0f;
        }
    }
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
            std::printf("  Block       - AUTOMATIC (when idle, not attacking)\n");
            std::printf("  M or click menu - Toggle menu\n");
            std::printf("  T           - Toggle dialog\n");
            std::printf("  N           - New game (go to Map)\n");
            std::printf("  Y/L         - Declare victory/defeat (debug, in Battle)\n");
            std::printf("  1/2/3       - Zoom presets\n");
            std::printf("  Esc         - Quit / close overlay / back\n\n");

            renderer_ = std::make_unique<ren::Renderer>();
            if (!renderer_->init(platform.window_width(), platform.window_height())) {
                renderer_.reset(); return;
            }
            renderer_->set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

            // Load loading screen textures (used by LoadingScene via render_loading_screen)
            if (!asset_root_.empty()) load_loading_screen();

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

            // Load item catalog (list.xml) for Shop
            {
                auto list_path = std::filesystem::path(asset_root_) / "assets" / "list.xml";
                if (std::filesystem::exists(list_path)) {
                    fmt::ListParser lp;
                    if (lp.load_file(list_path.string(), list_data_)) {
                        list_data_loaded_ = true;
                        std::printf("[shop] loaded %zu items from %s\n",
                                    list_data_.items.size(), list_path.string().c_str());
                    }
                    auto alt_path = std::filesystem::path(asset_root_) / "list.xml";
                    if (!list_data_loaded_ && std::filesystem::exists(alt_path)) {
                        if (lp.load_file(alt_path.string(), list_data_)) {
                            list_data_loaded_ = true;
                            std::printf("[shop] loaded %zu items from %s\n",
                                        list_data_.items.size(), alt_path.string().c_str());
                        }
                    }
                }
                // Initialize shop manager from loaded catalog
                if (list_data_loaded_) {
                    shop_manager_.load_catalog(list_data_);
                }
            }

            // Load saved progress (gold, wins, levels, inventory)
            host_load_progress();

            // Sync member variables from PlayerProfile (authoritative after load)
            currency_ = player_profile_.currency();
            player_wins_ = player_profile_.wins();
            player_losses_ = player_profile_.losses();

            // Start the scene flow at Boot
            scene::SceneContext ctx{*this, platform, *renderer_, 0};
            scene_manager_.start(scene::SceneId::Boot, ctx);
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
    overlay_ = Overlay::None;
            menu_anim_progress_ = 0.0f;
}

void Game::host_load_battle_location(const std::string& location) {
    // Clear old location atlases so new location's atlases are loaded fresh.
            // Without this, atlases with the same name (e.g. "bg", "atlas_layer1")
            // from the dojo would be reused, showing the wrong background images.
            assets_->atlases().clear();
            current_location_name_ = location.empty() ? "dojo" : location;
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
            // Keep the player_profile_ in sync too
            player_profile_ = player::PlayerProfile::from_save_data(data);
            return save_manager_.save(player_profile_.to_save_data());
}

bool Game::host_load_progress() {
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
            std::printf("[save] loaded %zu completed levels, %d gold, %dw %dl, %zu items\n",
                        completed_levels_.size(), currency_, player_wins_, player_losses_,
                        data.owned_items.size());
            return true;
}

void Game::host_set_dialogue(std::vector<std::pair<std::string, std::string>> lines) {
    dialogue_lines_ = std::move(lines);
            dialogue_index_ = 0;
}

const std::vector<std::pair<std::string, std::string>>& Game::host_get_dialogue() const {
    return dialogue_lines_;
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

std::string Game::host_get_battle_result() const {
    return battle_result_;
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
    std::string inv_weapon = inventory_.equipped_weapon();
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
            // Load a generic battle track — we use the first fight track
            static bool battle_music_loaded = false;
            if (!battle_music_loaded) {
                auto root = std::filesystem::path(asset_root_);
                for (const auto& base : {root/"assets"/"assets"/"music",
                                          root/"assets"/"music",
                                          root/"music"}) {
                    auto p = base / "fight1_samurai_spirit.mp3";
                    if (std::filesystem::exists(p)) {
                        audio.load_music_file("battle_music", p.string());
                        battle_music_loaded = true;
                        break;
                    }
                }
            }
            if (audio.get_sound("battle_music")) {
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
            if (!is_battle_mode_) render_hud(*platform_);
            if (!is_battle_mode_ && menu_anim_progress_ > 0.01f) render_menu_expanded(*platform_);
            if (!is_battle_mode_ && overlay_ == Overlay::Dialog) render_dialog_overlay(*platform_);
}

void Game::host_render_loading() {
    render_loading_screen(*platform_);
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
                // [ORIGINAL] Try correct path first, then fallback
                auto stages_path = root / "assets/stages.xml";
                if (!std::filesystem::exists(stages_path)) {
                    stages_path = root / "assets/files/assets/stages.xml";
                }
                if (std::filesystem::exists(stages_path)) {
                    auto stages_text = read_text(stages_path.string());
                    resf2::format::StageParser parser;
                    auto& sd = assets_->stage_data();
                    if (parser.parse(stages_text, sd)) {
                        assets_->set_stages_loaded(true);
                        std::printf("[STAGE] Loaded %zu zones\n", sd.zones.size());
                    }
                }
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
            load_punching_bag_model();
            load_animations();
            load_moves();
            // Load enemy weapon
            load_enemy_weapon("weapon_knuckles.xml");
            // Load player's equipped weapon model
            load_player_weapon(equipped_weapon_);
            load_hud_textures();
            load_menu_textures();
            load_hud_font();
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
                // X offset: align bag with holder (layer_5 at x=-10).
                // bag_cx = enemy_x - offset = -10 → offset = enemy_x + 10 = 983.
                const float X_OFFSET = 983.0f;  // aligns bag with ceiling holder
                player_pos_x_ = location_->player_x - X_OFFSET;
                player_pos_y_ = location_->player_y;  // no invert (matches location rendering)
                // [ORIGINAL] Enemy fighter position: same as the punching bag/enemy
                // spawn point from params.xml (enemy_x - X_OFFSET). The enemy
                // skeleton stands here and AI controls its behavior.
                enemy_pos_x_ = location_->enemy_x - 983.0f;
                enemy_pos_y_ = location_->enemy_y;  // use enemy Y from params.xml (not player Y)
                enemy_facing_right_ = false;  // faces left toward player
            }
            // Camera: follow player but keep a proper Y that shows the floor.
            // The dojo floor (layer_3) is at world Y ≈ -193. Player at Y ≈ -93.
            // Camera Y should be around -50 to show player + floor + ceiling.
            cam_x_ = player_pos_x_ + 200.0f;
            cam_y_ = -50.0f;  // shows floor and character properly
            zoom_ = 1.0f;

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
            } else if (assets_->animations().count("stance_idle")) {
                play_animation("stance_idle", true, 0);  // priority 0: idle (always interruptible)
            }
}

void Game::host_update_gameplay(uint32_t dt) {
    // [DIAGNOSTIC] Advance input-script frame counter and apply events
    // scheduled for this frame BEFORE reading input. This keeps script
    // frame N aligned with gameplay frame N (Boot/Loading don't count).
    platform_->tick_input_script();
    const auto& input = platform_->input();
    float dt_sec = (float)dt / 1000.0f;

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
    // Update audio engine (mix + write to backend)
    aud::AudioEngine::instance().update(dt_sec);

    // [ORIGINAL] Player block: automatic when idle (not attacking, not moving).
    // Original SF2: block is automatic when standing still and not attacking.
    // key_down (S) = duck (low block); standing = high block.
    if (!player_fighter_.is_dead) {
        bool player_idle = (hit_anim_ == 0 && move_state_ == 0 &&
                            !start_stance_playing_);
        player_fighter_.is_blocking = player_idle;
    }

    // [ORIGINAL] Enemy AI: simple state machine.
    // States: 0=idle, 1=approach, 2=attack, 3=retreat, 4=block
    // Decisions every 0.8s: based on distance to player + randomness.
    if (!enemy_fighter_.is_dead && !player_fighter_.is_dead) {
        enemy_ai_timer_ += dt_sec;
        enemy_attack_cooldown_ = std::max(0.0f, enemy_attack_cooldown_ - dt_sec);
        if (enemy_fighter_.hit_stun_time > 0) {
            // Stunned — can't act
            enemy_anim_ = "fists_hit";
        } else if (enemy_ai_timer_ >= enemy_ai_decision_interval_) {
            enemy_ai_timer_ = 0;
            float dist = std::abs(enemy_pos_x_ - player_pos_x_);
            int r = std::rand() % 100;
            // [ORIGINAL] AI behavior tuned for engaging combat:
            // - Far (>250px): approach
            // - Mid (120-250px): attack range — prefer attack
            // - Close (<120px): mix of attack/retreat/block to avoid clumping
            if (dist > 250) {
                enemy_ai_state_ = 1;  // approach
            } else if (dist > 120) {
                // Mid range: attack often, sometimes approach
                if (r < 50) enemy_ai_state_ = 2;  // attack
                else if (r < 70) enemy_ai_state_ = 1;  // approach
                else if (r < 80) enemy_ai_state_ = 4;  // block
                else enemy_ai_state_ = 0;  // idle
            } else {
                // Close range: mix it up
                if (r < 35) enemy_ai_state_ = 2;  // attack
                else if (r < 55) enemy_ai_state_ = 3;  // retreat
                else if (r < 75) enemy_ai_state_ = 4;  // block
                else enemy_ai_state_ = 0;  // idle
            }
            // Aggression: if player is low health, attack more
            if (player_fighter_.health < 30 && r < 50) {
                enemy_ai_state_ = 2;  // press the advantage
            }
            // Self-preservation: if enemy low health, retreat/block more
            if (enemy_fighter_.health < 30 && r < 60) {
                enemy_ai_state_ = (r < 30) ? 4 : 3;  // block or retreat
            }
        }
        // Execute current AI state
        enemy_fighter_.is_blocking = (enemy_ai_state_ == 4);
        float enemy_speed = 90.0f;
        if (enemy_ai_state_ == 1) {  // approach
            if (enemy_pos_x_ > player_pos_x_) enemy_pos_x_ -= enemy_speed * dt_sec;
            else enemy_pos_x_ += enemy_speed * dt_sec;
            enemy_anim_ = "step_forward";
            enemy_facing_right_ = (player_pos_x_ > enemy_pos_x_);
        } else if (enemy_ai_state_ == 3) {  // retreat
            if (enemy_pos_x_ < player_pos_x_) enemy_pos_x_ -= enemy_speed * dt_sec;
            else enemy_pos_x_ += enemy_speed * dt_sec;
            enemy_anim_ = "step_back";
        } else if (enemy_ai_state_ == 2 && enemy_attack_cooldown_ <= 0) {  // attack
            enemy_anim_ = "high_punch";
            enemy_attacking_ = true;
            enemy_attack_duration_ = 0.4f;
            enemy_attack_cooldown_ = 1.5f;
            play_sound("f_pl_attack2", 0.4f);
            // [ORIGINAL] Dojo is TRAINING — enemy attacks don't deal damage.
            // In the original, the Dojo sparring partner is a training dummy.
            // Health/damage only applies in real fights (map battles).
            // Enemy still plays attack animation + hit spark for visual feedback.
        } else if (enemy_ai_state_ == 4) {  // block
            enemy_anim_ = "fists_block";
        } else {  // idle
            enemy_anim_ = "fists_idle";
        }
        if (enemy_attacking_) {
            enemy_attack_duration_ -= dt_sec;
            if (enemy_attack_duration_ <= 0) enemy_attacking_ = false;
        }
        enemy_anim_time_ += dt_sec;
        // Face the player
        enemy_facing_right_ = (player_pos_x_ > enemy_pos_x_);
    }

    // R: restart battle (after victory/defeat)
    if (input.keys_just_pressed[(size_t)plat::Key::R]) {
        if (player_fighter_.is_dead || enemy_fighter_.is_dead) {
            player_fighter_ = FighterState{};
            enemy_fighter_ = FighterState{};
            battle_result_.clear();
            player_hit_flash_ = 0;
            enemy_hit_flash_ = 0;
            combo_timer_ = 0;
            hit_sparks_.clear();
            enemy_ai_timer_ = 0;
            enemy_ai_state_ = 0;
            enemy_attack_cooldown_ = 0;
            enemy_attacking_ = false;
            // Reset positions
            if (location_) {
                enemy_pos_x_ = location_->enemy_x - 983.0f;
            }
            std::printf("[COMBAT] Battle restarted\n");
        }
    }
    // [ORIGINAL] B: toggle between punching bag and enemy fighter (Dojo training)
    if (input.keys_just_pressed[(size_t)plat::Key::B]) {
        show_enemy_ = !show_enemy_;
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

    // Click: check menu button (left side)
    for (const auto& p : input.pointers) {
        if (p.just_pressed) {
            float btn_x = 10.0f, btn_y = 58.0f, btn_w = 130.0f, btn_h = 40.0f;
            if (p.x >= btn_x && p.x <= btn_x + btn_w &&
                p.y >= btn_y && p.y <= btn_y + btn_h) {
                overlay_ = (overlay_ == Overlay::Menu) ? Overlay::None : Overlay::Menu;
            }
        }
    }

    // Animate menu expand/collapse (300ms transition)
    float target_progress = (overlay_ == Overlay::Menu) ? 1.0f : 0.0f;
    float anim_speed = 1000.0f / 300.0f;
    if (menu_anim_progress_ < target_progress) {
        menu_anim_progress_ += (float)dt / anim_speed;
        if (menu_anim_progress_ > target_progress) menu_anim_progress_ = target_progress;
    } else if (menu_anim_progress_ > target_progress) {
        menu_anim_progress_ -= (float)dt / anim_speed;
        if (menu_anim_progress_ < target_progress) menu_anim_progress_ = target_progress;
    }

    // === DYNAMIC FACING ===
    // Character faces the enemy. Update facing DYNAMICALLY during step
    // [ORIGINAL] PC source: sf2.js — facing is locked during root-motion
    // moves (roll, jump, flip, attack). The original game only updates
    // facing during idle/step states, not during special moves.
    // Our root-motion whitelist (is_root_motion_anim) determines which
    // animations lock facing. This prevents instant flip during roll.
    bool facing_locked = hit_anim_ > 0 ||
        current_anim_ == "forward_roll" || current_anim_ == "back_roll" ||
        current_anim_ == "jump" || current_anim_ == "jump_away" ||
        current_anim_ == "front_flip" || current_anim_ == "back_flip" ||
        current_anim_ == "back_handflip";
    if (location_ && !facing_locked) {
        float bag_x = location_->enemy_x - 983.0f;
        bool should_face_right = (bag_x >= player_pos_x_);
        float dist_to_enemy = std::abs(bag_x - player_pos_x_);
        if (dist_to_enemy > 30.0f) {
            facing_right_ = should_face_right;
        }
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

    // Convert absolute directions to relative (Forward/Back)
    bool key_forward = facing_right_ ? key_right : key_left;
    bool key_back = facing_right_ ? key_left : key_right;

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
    if (fwd_just_pressed) {
        if (now_ms - input_handler_.last_fwd_tap_ms() < 300 && move_state_ == 2) {
            input_handler_.set_double_step_fwd_requested(true);
        }
        input_handler_.set_last_fwd_tap_ms(now_ms);
    }
    if (back_just_pressed) {
        if (now_ms - input_handler_.last_back_tap_ms() < 300 && move_state_ == 1) {
            input_handler_.set_double_step_back_requested(true);
        }
        input_handler_.set_last_back_tap_ms(now_ms);
    }

    bool punch_pressed = input.keys_just_pressed[(size_t)plat::Key::O];
    bool kick_pressed = input.keys_just_pressed[(size_t)plat::Key::P];
    // Also keep Space/K as fallback for testing
    if (input.keys_just_pressed[(size_t)plat::Key::Space]) punch_pressed = true;
    if (input.keys_just_pressed[(size_t)plat::Key::K]) kick_pressed = true;

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

    // [HEURISTIC-TODO] step_min_played: invented 400ms threshold to prevent
    // tap-to-cancel of step animations. The original engine gates move
    // transitions via the Uninterrupt interval in moves.xml (each Move's
    // <Interval Name="Uninterrupt" Start=".." End=".."/>). Once combat
    // logic migrates to use MoveDef::intervals (the full interval vector
    // populated by the xml_doc pass), this 400ms heuristic should be
    // replaced by: `is_in_uninterrupt(current_move_, anim_time_)`.
    if (move_state_ == 1 || move_state_ == 2) {
        input_handler_.set_step_play_time(input_handler_.step_play_time() + dt);
    } else {
        input_handler_.set_step_play_time(0);
    }
    bool step_min_played = input_handler_.step_play_time() >= 400;

    // [HEURISTIC-TODO] fwd_held_ms_/back_held_ms_: invented 200ms latch
    // for direction keys. The original engine reads key state per-frame
    // via the Marmalade keypad (dz_keypad_update_decompiled.c) with no
    // latch — combos are gated by CurrentAnimation conditions, not key
    // history. Remove this latch once combo logic uses MoveQuery with
    // required_current_animation from moves.xml <Conditions>.
    if (key_forward) input_handler_.set_fwd_held_ms(200);
    else if (input_handler_.fwd_held_ms() > 0) input_handler_.set_fwd_held_ms(input_handler_.fwd_held_ms() - (int)dt);
    if (key_back) input_handler_.set_back_held_ms(200);
    else if (input_handler_.back_held_ms() > 0) input_handler_.set_back_held_ms(input_handler_.back_held_ms() - (int)dt);

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
        for (auto& [name, move] : assets_->moves()) {
            if (move.filename.empty() || move.template_name.empty()) continue;
            // Skip Titan moves
            {
                size_t titan_pos = move.template_name.find("Titan");
                if (titan_pos != std::string::npos) {
                    if (titan_pos < 3 || move.template_name.substr(titan_pos - 3, 3) != "Not") {
                        continue;
                    }
                }
            }
            // Weapon-specific moves may have empty move_type (template "1key|Central|Weapon").
            // Allow them if they have matching tactic_weapon and no move_type set.
            bool move_type_match = (move.move_type == cur_move_type) ||
                (move.move_type.empty() && is_weapon_allowed(move) && move.key_count <= 2);
            if (!move_type_match) continue;

            if (block_all_combat) {
                continue;
            } else if (in_attack && is_uninterrupt_) {
                // In Uninterrupt: only 3key chain combos
                if (move.key_count != 3) continue;
                if (!move.required_current_animation.empty()) {
                    if (current_move_ != move.required_current_animation) continue;
                }
            } else if (in_attack && past_attack_interval) {
                // Past attack interval: allow 1key/2key to interrupt recovery
                if (move.key_count == 3) continue;
            } else {
                if (move.key_count == 3) continue;
            }
            // Match direction
            if (move.direction != cur_direction) continue;
            // Match weapon by tactic_weapon (empty = any weapon)
            if (!is_weapon_allowed(move)) continue;
            // Check distance condition (only from main <Conditions>, not <Tactics>)
            // Note: <Tactics><Distance> is for AI move selection, not player.
            // We skip distance check entirely — player can attack at any distance.
            // (The original game uses distance only for AI tactic selection.)
            // Check weapon subtype lock (from <Locks><Item SubType="...">)
            if (!move.required_weapon_subtype.empty() &&
                move.required_weapon_subtype.find(equipped_weapon_) == std::string::npos) continue;
            // [ORIGINAL] CurrentAnimation condition check.
            // PC source: sf2.js np.isEqual() (line 42544) - 3key combos
            // require the current animation to match a specific name.
            // e.g., DoublePunch requires CurrentAnimation="HeavyPunch".
            // The Name in moves.xml matches the Move Name (not filename).
            if (!move.required_current_animation.empty()) {
                if (current_move_ != move.required_current_animation) continue;
            }
            // Prevent a move from chaining into itself (same move can't restart).
            // Without this, moves with empty required_current_animation can
            // be re-triggered during their own uninterrupt window.
            if (move.name == current_move_) continue;
            // Check if animation exists
            std::string anim_name = move.filename;
            if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                anim_name = anim_name.substr(0, anim_name.size()-4);
            if (!assets_->animations().count(anim_name)) continue;
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
            // [ORIGINAL] Play attack swing sound at attack start.
            // Original SF2 plays f_pl_attack*.wav on the first attack frame.
            int snd = (best_move->name.length() % 4) + 1;
            play_sound("f_pl_attack" + std::to_string(snd), 0.5f);
            goto after_combat;
        } else if (punch_pressed || kick_pressed) {
            // [DIAGNOSTIC] No candidate found — log structured reject.
            std::printf("[INPUT_DECISION] f=%llu btn=%s keys_down=%s%s%s%s just=%s%s "
                        "face=%d dir=%s ms=%d anim='%s' move='%s' hit=%u unint=%d "
                        "basic=%d cand=%d sel='' reject=no_candidate\n",
                        (unsigned long long)total_frame_count_,
                        punch_pressed ? "O" : "P",
                        key_up?"W":"", key_down?"S":"", key_left?"A":"", key_right?"D":"",
                        punch_pressed?"O":"", kick_pressed?"P":"",
                        (int)facing_right_, cur_direction.c_str(), move_state_,
                        current_anim_.c_str(), current_move_.c_str(),
                        hit_anim_, is_uninterrupt_?1:0, (int)(in_attack && !is_uninterrupt_),
                        candidate_count);
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
                        (move.move_type.empty() && is_weapon_allowed(move) && move.key_count <= 2);
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
    if (!in_attack) {
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

        if (!any_dir_just_pressed) break;

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
                // Skip Titan moves (player is not a Titan)
                if (move.template_name.find("Titan") != std::string::npos &&
                    move.template_name.find("NotTitan") == std::string::npos) continue;
                // Match Jump moves or MOVE type (not Wall, not Punch/Kick)
                if (move.template_name.find("Wall") != std::string::npos) continue;
                if (!move.is_jump && move.move_type != "Jump" &&
                    move.move_type != "MOVE" && !move.move_type.empty()) continue;
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
                play_animation(anim_name, false, best_move->priority);
                current_move_ = best_move->name;
                int fc = assets_->animations()[anim_name].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                move_state_ = 10;
                goto after_combat;
            }
        }

        // Duck: S held (or just pressed) with no direction
        // Original game: holding S keeps you ducking. If you were attacking
        // and the attack ends while S is held, you immediately duck again.
        bool duck_input = key_down && !key_forward && !key_back;
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
                // Only switch animation if not already ducking
                if (move_state_ != 11 || current_anim_ != duck_anim_name) {
                    std::printf("[DUCK] found: %s (anim '%s')\n", duck_move->name.c_str(), duck_anim_name.c_str());
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
                play_animation("back_roll", false, 1);  // priority 1: defensive
                current_move_ = "BackRoll";
                int fc = assets_->animations()["back_roll"].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                move_state_ = 10;
                goto after_combat;
            }
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
        bool fwd_latched = input_handler_.fwd_held_ms() > 0;
        bool back_latched = input_handler_.back_held_ms() > 0;

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
            // step_cooldown_ms_ prevents immediate step after a roll/special ends
            if (key_forward && !key_back && !key_down && !step_fwd_anim.empty() && step_cooldown_ms_ == 0) {
                move_state_ = 2;
                play_animation(step_fwd_anim, true, 0);  // priority 0: movement (interruptible)
            } else if (key_back && !key_forward && !key_down && !step_back_anim.empty() && step_cooldown_ms_ == 0) {
                move_state_ = 1;
                play_animation(step_back_anim, true, 0);  // priority 0: movement (interruptible)
            }
        } else if (move_state_ == 1) {  // MOVING_BACK
            // [ORIGINAL] Double-tap Back → BackHandflip (handstand flip retreat)
            // Original moves.xml: BackHandflip has Keys=Back Tap + Back Tap,
            // FileName=back_handflip.bin. It's a retreat move (not a dash).
            if (input_handler_.double_step_back_requested() && assets_->animations().count("back_handflip")) {
                play_animation("back_handflip", false, 1);  // priority 1: evasive retreat
                move_state_ = 10;
                current_move_ = "BackHandflip";
                int fc = assets_->animations()["back_handflip"].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                input_handler_.clear_double_step_back();
                debug_log("[MOVE] f=%llu BackHandflip (handstand retreat)\n",
                          (unsigned long long)total_frame_count_);
            } else if (!back_latched && step_min_played) {
                move_state_ = 0; need_switch_to_idle_ = true;
            } else if (fwd_latched && !back_latched && step_min_played && !step_fwd_anim.empty()) {
                move_state_ = 2;
                play_animation(step_fwd_anim, true, 0);  // priority 0: movement
            }
        } else if (move_state_ == 2) {  // MOVING_FORWARD
            // [ORIGINAL] Double-tap Forward during ForwardStep  DoubleStepForward (dash)
            if (input_handler_.double_step_fwd_requested() && assets_->animations().count("double_step_forward")) {
                play_animation("double_step_forward", false, 1);  // priority 1: dash (evasive)
                move_state_ = 10;  // special (non-interruptible during dash)
                current_move_ = "DoubleStepForward";
                int fc = assets_->animations()["double_step_forward"].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / anim_fps_);
                input_handler_.clear_double_step_fwd();
                debug_log("[MOVE] f=%llu DoubleStepForward (dash)\n",
                          (unsigned long long)total_frame_count_);
            } else if (!fwd_latched && step_min_played) {
                move_state_ = 0; need_switch_to_idle_ = true;
            } else if (back_latched && !fwd_latched && step_min_played && !step_back_anim.empty()) {
                move_state_ = 1;
                play_animation(step_back_anim, true, 0);  // priority 0: movement
            }
        }
    }
    }

    // === HIT ANIM COUNTDOWN ===
    if (hit_anim_ > 0) {
        hit_anim_ -= std::min<uint32_t>(hit_anim_, dt);
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
    // Exit duck state when Down released
    // No minimum duration — original game allows immediate release
    if (move_state_ == 11) {
        input_handler_.set_duck_play_time(input_handler_.duck_play_time() + dt);
        if (!key_down && input_handler_.duck_play_time() >= 100) {
            move_state_ = 0;
            need_switch_to_idle_ = true;
        }
    }

    after_combat:
    // Camera follows player (always update, even after attack)
    // [ORIGINAL] Screen shake on hit: offset camera by a decaying random
    // amount when player_hit_flash_ or enemy_hit_flash_ is active.
    cam_x_ = player_pos_x_ + 200.0f;
    float shake = 0.0f;
    if (player_hit_flash_ > 0) shake = std::max(shake, player_hit_flash_ * 12.0f);
    if (enemy_hit_flash_ > 0) shake = std::max(shake, enemy_hit_flash_ * 8.0f);
    float shake_x = 0, shake_y = 0;
    if (shake > 0.1f) {
        shake_x = ((float)(std::rand() % 200) - 100.0f) / 100.0f * shake;
        shake_y = ((float)(std::rand() % 200) - 100.0f) / 100.0f * shake;
    }
    renderer_->camera().set_target(cam_x_ + shake_x, cam_y_ + shake_y);
    renderer_->camera().set_zoom(zoom_);

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
    player_pos_x_ += anim_root_dx_ * (facing_right_ ? 1.0f : -1.0f);
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

            // For moves without explicit Uninterrupt data, default the
            // uninterrupt window to the attack interval. This prevents
            // 1key/2key attacks from cancelling the current attack during
            // its active frames while still allowing 3key chain combos.
            if (un_start < 0 && move_it->second.attack_start >= 0 &&
                move_it->second.attack_end > 0) {
                un_start = move_it->second.attack_start;
                un_end = move_it->second.attack_end;
            }

            if (un_start >= 0) {
                std::string expected_anim = move_it->second.filename;
                if (expected_anim.size() > 4 && expected_anim.substr(expected_anim.size()-4) == ".bin")
                    expected_anim = expected_anim.substr(0, expected_anim.size()-4);
                if (expected_anim == current_anim_) {
                    int current_frame = (int)(anim_time_ * anim_fps_);
                    int start = un_start - 1;
                    int end = un_end > 0 ? un_end - 1 : 9999;
                    if (current_frame >= start && current_frame <= end) {
                        is_uninterrupt_ = true;
                    }
                }
            }
        }
    }

    // After update_animation, switch to idle if requested.
    // This ensures the previous animation's final displacement is applied.
    if (need_switch_to_idle_) {
        need_switch_to_idle_ = false;
        if (start_stance_playing_) {
            start_stance_playing_ = false;
        }
        play_animation("stance_idle", true, 0);  // priority 0: idle (always interruptible)
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

                if (in_attack_interval && !hit_this_interval_) {
                    // [ORIGINAL] Distance-based hit detection on enemy fighter.
                    // If the player's attack limb is within hit range of the
                    // enemy fighter (enemy_pos_x_), register a hit. This works
                    // alongside the bag-collision detection (bag stays at the
                    // original spawn point as a visual punching bag; the enemy
                    // fighter moves via AI and is hit by distance check).
                    if (show_enemy_ && enemy_fighter_.invuln_time <= 0) {
                        float dist_to_enemy = std::abs(enemy_pos_x_ - player_pos_x_);
                        // Hit range: 180px (covers punch/kick reach)
                        if (dist_to_enemy < 180.0f) {
                            // [ORIGINAL] Dojo training — no health damage, just
                            // visual feedback (hit flash, sparks, sound, knockback).
                            enemy_fighter_.invuln_time = 0.4f;
                            enemy_hit_flash_ = 0.25f;
                            int snd_idx = (current_frame + (int)current_move_[0]) % 4 + 1;
                            play_sound("f_pl_attack" + std::to_string(snd_idx), 0.7f);
                            play_sound("armor", 0.5f);
                            hit_this_interval_ = true;  // prevent multi-hit per frame
                            // Spawn hit sparks at enemy position
                            spawn_hit_sparks(enemy_pos_x_, enemy_pos_y_ - 40, 10);
                            debug_log("[HIT] f=%llu move='%s' hit enemy dist=%.1f\n",
                                (unsigned long long)total_frame_count_, current_move_.c_str(), dist_to_enemy);
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

                            float limb_lx = ait->second.first;
                            float limb_ly = ait->second.second;
                            auto pivot_it = assets_->skeleton_nodes().find("NPivot");
                            float pivot_ly = pivot_it != assets_->skeleton_nodes().end() ? pivot_it->second.y : stance_npivot_y_;
                            float limb_wx = player_pos_x_ + (facing_right_ ? limb_lx : -limb_lx);
                            float limb_wy = player_pos_y_ + y_adjust_smoothed_ + (limb_ly - pivot_ly);

                            // Get attacking edge radius from skeleton
                            float atk_radius = 0;
                            auto skel_it = assets_->skeleton_edges().find(edge_name);
                            if (skel_it != assets_->skeleton_edges().end()) {
                                atk_radius = skel_it->second.radius;
                            }

                            // Get world-space positions of both attacking edge endpoints
                            auto ait2 = anim_node_pos_.find(node2);
                            if (ait2 == anim_node_pos_.end()) continue;

                            float limb2_lx = ait2->second.first;
                            float limb2_ly = ait2->second.second;
                            float limb2_wx = player_pos_x_ + (facing_right_ ? limb2_lx : -limb2_lx);
                            float limb2_wy = player_pos_y_ + y_adjust_smoothed_ + (limb2_ly - pivot_ly);

                            // Check against ALL collisible bag edges
                            bool hit_this_interval_this_frame = false;
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
                                    // [ORIGINAL] Play hit sound + apply damage to enemy fighter.
                                    // Original SF2: on hit, plays armor/body sound + damage from
                                    // MoveDef::damage (parsed from <Damage Value=".."/>).
                                    // Pick a random attack sound for variety.
                                    int snd_idx = (current_frame + (int)current_move_[0]) % 4 + 1;
                                    play_sound("f_pl_attack" + std::to_string(snd_idx), 0.8f);
                                    play_sound("armor", 0.6f);
                                    // Apply damage to enemy (punching bag = enemy proxy)
                                    // [ORIGINAL] Dojo training — no health damage.
                                    // Just visual feedback (hit flash, sparks, sound).
                                    if (enemy_fighter_.invuln_time <= 0) {
                                        enemy_fighter_.invuln_time = 0.2f;
                                        enemy_hit_flash_ = 0.2f;
                                        player_fighter_.hits_landed++; combo_timer_ = 2.0f;
                                        int hit_snd_idx = (current_frame + (int)current_move_[0]) % 4 + 1;
                                        play_sound("f_pl_attack" + std::to_string(hit_snd_idx), 0.7f);
                                        play_sound("armor", 0.5f);
                                        spawn_hit_sparks(enemy_pos_x_, enemy_pos_y_ - 40, 10);
                                    }
                                    break;
                                }
                            }
                            if (hit_this_interval_this_frame) break;
                        }
                        if (hit_registered) break;
                    }
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

    // Update bag Verlet physics
    update_bag_verlet(dt / 1000.0f);

    // Update projectiles (magic/ranged)
    update_projectiles(dt / 1000.0f);

    // Zoom presets
    if (input.keys_just_pressed[(size_t)plat::Key::Num1]) zoom_ = 1.0f;
    if (input.keys_just_pressed[(size_t)plat::Key::Num2]) zoom_ = 0.7f;
    if (input.keys_just_pressed[(size_t)plat::Key::Num3]) zoom_ = 1.5f;

    // [DIAGNOSTIC] Structured frame state dump (--dump-state)
    if (dump_state_) {
        std::printf("[STATE] f=%llu ms=%d ha=%u anim='%s' move='%s' px=%.1f py=%.1f "
                    "bag_hit=%d bag_angle=%.3f nv=%zu\n",
                    (unsigned long long)total_frame_count_, move_state_, hit_anim_,
                    current_anim_.c_str(), current_move_.c_str(),
                    player_pos_x_, player_pos_y_,
                    (int)hit_this_interval_, bag_angle_, bag_verlet_.size());
    }
}

} // namespace resf2::game
// engine/scene/scene_system.hpp
//
// Scene / State Manager for reSF2.
//
// Provides a clean finite-state machine for the application-level game flow:
//   Boot -> Loading -> MainMenu -> Map -> Dialogue -> Battle -> Results -> MainMenu
//
// Each Scene has on_enter / on_update / on_render / on_exit hooks. The
// SceneManager owns the current scene and handles transitions. Scenes are
// lightweight — they do not own game assets (the Game class does). Instead,
// scenes receive a SceneContext reference on each hook, giving them access
// to the platform, renderer, input, and a small set of game-state hooks.
//
// This is a Stage 9 addition. The existing Game class in main.cpp is the
// scene host: it constructs the SceneManager, registers scene factories,
// and delegates on_update / on_render to the manager. The heavy game logic
// (rendering, physics, animation, combat) stays in Game; scenes orchestrate
// state transitions and call into Game's public methods.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace resf2::platform { class Platform; }
namespace resf2::renderer { class IRenderer; }
namespace resf2::format { struct StageData; struct ListData; }

namespace resf2::scene {

// [P12] Story-dialogue panel geometry — the JS-authored proportions
// (parchment 900/1700 wide at 400/1700, avatar 0.875 of the sheet height
// at the left 0.017 inset, text after the avatar). One source for the
// DialogueScene render and the placement tests.
struct DialogueLayout {
    float box_x = 0, box_y = 0, box_w = 0, box_h = 0;
    float portrait_x = 0, portrait_y = 0, portrait_size = 0;
    float text_x = 0, pad = 0;
};

// ---------- Scene identifiers ----------

enum class SceneId : std::uint8_t {
    Boot,       // splash / engine init (before assets load)
    Loading,    // progress bar while assets warm up
    MainMenu,   // dojo with punching bag + scroll menu (current "Location")
    Map,        // act/episode/level selection
    Shop,       // equipment store
    Settings,   // options
    Dialogue,   // pre-battle dialogue overlay
    Battle,     // combat scene (proper fight, not just bag)
    Results,    // post-battle victory/defeat screen
    Profile,    // player stats and inventory summary
    None,       // sentinel: no scene / unset return target
};

// Human-readable name for debugging / logging.
[[nodiscard]] const char* scene_name(SceneId id) noexcept;

// ---------- Scene context ----------

// Forward declaration — the full Game class is defined in main.cpp.
class SceneHost;

// Context passed to every scene hook. Gives scenes access to everything
// they need without exposing the full Game class.
struct SceneContext {
    SceneHost& host;
    platform::Platform& platform;
    renderer::IRenderer& renderer;
    std::uint32_t dt_ms = 0;  // delta time for the current frame (update only)
};

// ---------- Scene interface ----------

class Scene {
public:
    virtual ~Scene() = default;

    // Called once when this scene becomes the active scene.
    virtual void on_enter(SceneContext& ctx) { (void)ctx; }

    // Called once per frame with the frame delta in ms.
    virtual void on_update(SceneContext& ctx) { (void)ctx; }

    // Called once per frame to render. The GL context is already current.
    virtual void on_render(SceneContext& ctx) { (void)ctx; }

    // Called once when this scene is being replaced by another.
    virtual void on_exit(SceneContext& ctx) { (void)ctx; }

    // Called when the platform requests quit (window close, Esc at root).
    // Return true to allow quit, false to intercept (e.g. "are you sure?").
    virtual bool on_quit_request(SceneContext& ctx) { (void)ctx; return true; }

    [[nodiscard]] virtual SceneId id() const noexcept = 0;
};

// ---------- Scene host interface ----------

// Implemented by the Game class. Scenes call these methods to interact with
// the game state (load assets, play animations, transition scenes, etc.).
class SceneHost {
public:
    virtual ~SceneHost() = default;

    // Request a scene transition. The manager will call on_exit on the
    // current scene and on_enter on the new scene at the end of the
    // current frame (deferred transition — safe to call from within
    // on_update / on_render).
    virtual void request_scene_transition(SceneId to) = 0;

    // --- Asset / rendering hooks (delegate to Game's existing methods) ---

    // Load the dojo location (background, character, bag, HUD). Called
    // when entering MainMenu or Battle.
    virtual void host_load_location() = 0;

    // Whether the location assets are currently loaded.
    [[nodiscard]] virtual bool host_location_loaded() const noexcept = 0;

    // Reset menu/overlay state (called when entering MainMenu to clear
    // any stale state left by Battle or other scenes).
    virtual void host_reset_menu_state() = 0;

    // Toggle the menu overlay (scroll panel with Dojo/Map/Shop/Profile/Settings
    // icons). Used by scenes that do NOT delegate to host_update_gameplay (Map,
    // Results) so they can open the menu the same way the dojo does.
    virtual void host_toggle_menu_overlay() {}

    // [U5] Close the menu overlay deterministically (no toggle). The expanded
    // menu must not stay visible over a submenu after the player navigates
    // away from the dojo — the soak showed Shop/Map opening with the menu
    // panel still on top.
    virtual void host_close_menu_overlay() {}

    // Render the menu overlay on top of the current scene content, advancing
    // the open/close animation. Safe to call every frame from any scene.
    virtual void host_render_menu_overlay() {}

    // Update the dojo gameplay (movement, combat, animation, physics,
    // overlays). Called by MainMenu and Battle scenes from their on_update.
    virtual void host_update_gameplay(std::uint32_t dt_ms) = 0;

    // Render the dojo scene (background, character, bag, HUD, menu/dialog
    // overlays). Called by MainMenu and Battle scenes from their on_render.
    virtual void host_render_scene() = 0;

    // Render the loading screen. Called by LoadingScene from its on_render.
    virtual void host_render_loading() = 0;

    // --- Persistence ---

    // Save game progress to disk. Returns true on success.
    virtual bool host_save_progress() = 0;

    // Load game progress from disk. Returns true on success.
    virtual bool host_load_progress() = 0;

    // --- Dialogue data ---

    // Set the current dialogue lines (for the Dialogue scene).
    // Each entry is a (speaker_name, text) pair.
    virtual void host_set_dialogue(std::vector<std::pair<std::string, std::string>> lines) = 0;

    // Get the current dialogue lines (for the Dialogue scene to render).
    [[nodiscard]] virtual const std::vector<std::pair<std::string, std::string>>& host_get_dialogue() const = 0;

    // Get the current dialogue choices (for choice-based dialogs like
    // QuestActionDialog). Returns empty vector when there are no choices.
    // [ORIGINAL] QuestActionDialog @ FUN_101c7d20 stores choices at +0xa4..+0xb0.
    [[nodiscard]] virtual std::vector<std::string> host_get_dialogue_choices() const { return {}; }

    // Set dialogue choices (empty to clear).
    virtual void host_set_dialogue_choices(std::vector<std::string>) {}

    // --- [Wave 9B] Story-dialogue queue (quest flow) ---

    // Queue a story dialogue (quests.xml <Dialog> sets) to play over the Map
    // on its next entry, returning to `return_to` when finished. Queued lines
    // are NOT a pre-battle dialogue: battle_location is irrelevant for them.
    virtual void host_queue_story_dialogue(
        std::vector<std::pair<std::string, std::string>> lines, SceneId return_to) {
        (void)lines; (void)return_to;
    }

    // True if a story dialogue is waiting to play; consumes the flag so each
    // queued dialogue plays exactly once.
    virtual bool host_consume_story_dialogue() { return false; }

    // The scene a non-battle dialogue returns to after its last line.
    [[nodiscard]] virtual SceneId host_get_dialogue_return() const {
        return SceneId::MainMenu;
    }

    // [Wave 9B] S5: the tutorial's training fight (FIRST_FIGHT) was WON —
    // the tutorial completes (Tutorial attribute on <Warrior> in
    // usersDefault.xml) and the Sensei "find yourself a weapon" story
    // dialogue is queued for the Map. Only the Results scene calls this on
    // a FIRST_FIGHT victory.
    virtual void host_finish_tutorial_fight() {}

    // --- Battle data ---

    // Set the level/act to battle. Called from Map scene before transitioning
    // to Battle.
    virtual void host_set_current_level(std::string level_id) = 0;

    // Get the result of the last battle ("victory" / "defeat" / "").
    [[nodiscard]] virtual std::string host_get_battle_result() const = 0;

    // --- Stage/Map data ---

    // Get the parsed stage data (zones, battles, fights, rewards).
    // Used by MapScene to render the zone map.
    [[nodiscard]] virtual const format::StageData* host_get_stages() const = 0;

    // --- Battle location ---

    // Set the location name for the next battle (from stages.xml Battle.Location).
    virtual void host_set_battle_location(std::string loc) = 0;

    // Get the current battle location name.
    [[nodiscard]] virtual std::string host_get_battle_location() const = 0;

    // --- Progress ---

    // Mark a level as completed and save progress.
    virtual void host_add_completed_level(const std::string& level) = 0;

    // Get the current/active level string.
    [[nodiscard]] virtual std::string host_get_current_level() const = 0;

    // Track battle outcomes (for Profile stats).
    virtual void host_add_win() = 0;
    virtual void host_add_loss() = 0;

    // Check if a level has been completed.
    [[nodiscard]] virtual bool host_is_level_completed(const std::string& level) const = 0;

    // --- Zone/Battle lock state [ORIGINAL] from usersDefault.xml ---

    // Check if a zone is unlocked. A zone is unlocked if the corresponding
    // <Battle> entry in the save does NOT have a _LOCKED suffix.
    [[nodiscard]] virtual bool host_is_zone_unlocked(const std::string& zone) const { (void)zone; return true; }

    // Check if a specific battle within a zone is locked.
    [[nodiscard]] virtual bool host_is_battle_locked(const std::string& zone, const std::string& battle) const { (void)zone; (void)battle; return false; }

    // Get the current tutorial state. Values: "MOVE", "BAG", "FIRST_FIGHT", "COMPLETE".
    // [ORIGINAL] The Tutorial attribute on <Warrior> in usersDefault.xml.
    [[nodiscard]] virtual std::string host_get_tutorial_state() const { return "COMPLETE"; }

    // Set the tutorial state (ResultsScene advances it on victory, keeps it
    // on defeat so the fight stays retryable).
    virtual void host_set_tutorial_state(std::string s) { (void)s; }

    // The label of the Results scene's continue button. On a DEFEAT of a
    // retryable fight it is the rematch prompt, not "BACK TO MENU".
    [[nodiscard]] virtual std::string host_get_results_button_label() const { return {}; }

    // [P12] The story-dialogue panel geometry at the given viewport.
    [[nodiscard]] virtual DialogueLayout host_dialogue_layout(float w, float h) const {
        DialogueLayout D;
        D.box_w = w * 0.53f;
        D.box_h = h * 0.20f;
        D.box_x = w * 0.235f;
        D.box_y = (h - D.box_h) * 0.5f;
        D.pad = D.box_h * 0.08f;
        D.portrait_size = D.box_h * 0.875f;
        D.portrait_x = D.box_x + D.box_w * 0.017f;
        D.portrait_y = D.box_y + (D.box_h - D.portrait_size) * 0.5f;
        D.text_x = D.portrait_x + D.portrait_size + D.box_w * 0.02f;
        return D;
    }

    // [P9] Render the shop's fighter preview (body + equipped weapon) into
    // the given screen rect. How much was drawn is exposed via the game
    // host's shop-preview geometry accessor.
    virtual void host_render_shop_preview(float x, float y, float w, float h) {
        (void)x; (void)y; (void)w; (void)h;
    }

    // --- Text rendering ---

    // Draw text using the HUD font system.
    // x,y = screen position, scale = font size multiplier, color = RGBA byte color.
    virtual void host_render_text(const std::string& text, float x, float y, float scale, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) const = 0;

    // --- Zone background textures ---

    // Render a zone background texture (1-7) as a full-screen background.
    // Returns true if the texture was found and rendered.
    virtual bool host_render_zone_bg(int zone_index, float x, float y, float w, float h) = 0;

    // --- Map screen ---
    //
    // [ORIGINAL] The world map is one painted sheet per zone
    // (assets/1536/image/zones/N.jpg, 1365x649). It is taller than it is wide
    // relative to a 16:9 viewport, so the original scales it to cover the
    // height and pans horizontally. Battle nodes carry map coordinates in
    // stages.xml (`<Battle X=".." Y="..">`, range -440..158 by -220..145),
    // measured from the CENTRE of that sheet.
    //
    // MapView is the transform the map was drawn with, so the caller can put
    // the nodes on it without duplicating the fitting arithmetic.
    struct MapView {
        bool ok = false;
        float scale = 1.0f;      // sheet pixels -> screen pixels
        float centre_x = 0.0f;   // screen position of the sheet's centre
        float centre_y = 0.0f;
        float max_scroll = 0.0f; // how far the sheet can pan, in screen pixels
    };

    // Draws the zone sheet, panned by `scroll_x` screen pixels, into the given
    // screen rect. Returns the transform used.
    virtual MapView host_render_zone_map(int zone_index, float scroll_x,
                                         float x, float y, float w, float h) = 0;

    // One battle node. `icon` is the kind ("tournament", "lynx", "training"),
    // `state` is 0 = base, 1 = active (selected), 2 = locked — the three
    // atlases the original ships. Returns false if that frame is missing.
    virtual bool host_render_battle_icon(const std::string& icon, int state,
                                         float cx, float cy, float size) = 0;

    // The per-location photo shown in the side scroll (image/battles/<loc>.jpg).
    virtual bool host_render_battle_preview(const std::string& location,
                                            float x, float y, float w, float h) = 0;

    // A vertical parchment scroll: rolled bar on top, sheet below, side edges.
    // Same assets as the dojo dialogue, so every screen that needs a panel
    // gets the same one instead of its own coloured rectangle.
    virtual void host_render_scroll_panel(float x, float y, float w, float h) = 0;

    // The top HUD strip (level, energy, money, gems) and the MENU scroll, as
    // drawn in the dojo. Every screen in the original carries them.
    virtual void host_render_top_panel() = 0;

    // The ":N" argument of --scene, or -1. Lets a screen open on a specific
    // page (the map's zone) for capture and comparison.
    [[nodiscard]] virtual int start_scene_arg() const = 0;

    // Draw a loaded UI texture by its atlas frame name (or loose-PNG name):
    // avatars, buttons, scroll pieces. Returns false when it is not loaded.
    virtual bool host_render_ui_texture(const std::string& name,
                                        float x, float y, float w, float h) = 0;

    // Localized string for a key, or "" when the key is absent.
    [[nodiscard]] virtual std::string host_localized(const std::string& key) const = 0;

    // Width and height of `text` at `scale`, for centring.
    [[nodiscard]] virtual std::pair<float, float> host_measure_text(
        const std::string& text, float scale) const = 0;

    // Toggle between punching bag and enemy fighter.
    virtual void host_set_battle_mode(bool battle) = 0;
    virtual void host_set_show_enemy(bool show) = 0;

    // Load a specific battle location (not just the dojo).
    virtual void host_load_battle_location(const std::string& location) = 0;

    // --- Battle result (set by BattleScene before transitioning to Results) ---

    // Set the battle result before transitioning to Results scene.
    virtual void host_set_battle_result(std::string result) = 0;

    // --- Quest events ---
    // [ORIGINAL] QuestManager @ 0x101c7d20 processes quest actions on events.
    // Trigger a quest event (e.g. "FightEnd", "SessionStart", "ZoneEnter").
    // The quest engine processes matching quest actions (OpenZone, UnlockBattle, Dialog, etc.).
    virtual void host_trigger_quest_event(const std::string& event, const std::string& arg = "") {}

    // --- Fight parameters and state ---
    //
    // [ORIGINAL] The original's Fight carries the stages.xml fight data:
    // rounds (Fight+0xc, becomes the round-dot count fighter+0xe4 in
    // ScreenModel @ 0x10291370) and the enemy warriors. The map hands this
    // over when the FIGHT button is pressed.
    struct BattleInfo {
        std::string enemy_name;   // FirstName / Template of warriors[0]
        int rounds = 1;           // <Fight Rounds="">
        int round_time_s = 99;    // <Fight RoundTime="">
        int reward_gold = 0;      // <Reward Money=""> from stages.xml
        int reward_xp = 0;        // <Reward Exp=""> from stages.xml
    };
    virtual void host_set_battle_info(const BattleInfo& info) = 0;
    [[nodiscard]] virtual const BattleInfo& host_get_battle_info() const = 0;

    // "" while the round is running, "victory"/"defeat" when one fighter died.
    [[nodiscard]] virtual std::string host_round_outcome() const = 0;

    // Health fractions [0..1] for the fight HUD and the timeout rule.
    [[nodiscard]] virtual float host_player_health_frac() const = 0;
    [[nodiscard]] virtual float host_enemy_health_frac() const = 0;

    // Heal both fighters, reset positions and combat state for the next round.
    virtual void host_reset_round() = 0;

    // Wins so far, for the round dots (Round_Done vs Round_Undone).
    virtual void host_set_round_wins(int player, int enemy) = 0;

    // --- Shop / Item data (for ShopScene) ---

    // Get the parsed item catalog (list.xml data). May be null if not loaded.
    [[nodiscard]] virtual const format::ListData* host_get_list_data() const = 0;

    // --- Currency (for Shop and Results) ---

    // Get the player's current gold/currency.
    [[nodiscard]] virtual int host_get_currency() const = 0;

    // Spend gold. Returns true if the player had enough.
    virtual bool host_spend_currency(int amount) = 0;

    // Add gold (rewards from battle, etc.).
    virtual void host_add_currency(int amount) = 0;

    // --- Inventory / Shop (for ShopScene and ProfileScene) ---

    // Check if the player owns an item.
    [[nodiscard]] virtual bool host_has_item(const std::string& item_id) const = 0;

    // Get the list of owned item IDs.
    [[nodiscard]] virtual std::vector<std::string> host_get_owned_items() const = 0;

    // Get the item ID equipped in a slot ("weapon", "armor", etc.).
    // Returns empty string if slot is empty.
    [[nodiscard]] virtual std::string host_get_equipped(const std::string& slot) const = 0;

    // Purchase an item from the shop. Deducts gold and adds to inventory.
    // Returns false if the item cannot be bought (not enough gold, level req, etc.)
    virtual bool host_buy_item(const std::string& item_id) = 0;

    // Sell an item. Removes from inventory and adds gold (half price).
    // Returns false if the item is not owned.
    virtual bool host_sell_item(const std::string& item_id) = 0;

    // Equip an item. Auto-detects slot from its type in list.xml.
    // Returns false if the item is not owned.
    virtual bool host_equip_item(const std::string& item_id) = 0;

    // Unequip the item in the given slot, returning it to inventory.
    // Returns false if the slot was already empty.
    virtual bool host_unequip_item(const std::string& slot) = 0;



    // --- Player stats (for Profile scene) ---
    
    // Get the player's current level (computed from wins or saved).
    [[nodiscard]] virtual int host_get_player_level() const = 0;

    // Get total battle wins.
    [[nodiscard]] virtual int host_get_wins() const = 0;

    // Get total battle losses.
    [[nodiscard]]     virtual int host_get_losses() const = 0;

    // --- Audio hooks ---

    // Start playing menu music (looping).
    virtual void host_start_menu_music() = 0;

    // Start playing battle music (looping).
    virtual void host_start_battle_music() = 0;

    // Stop the currently playing music.
    virtual void host_stop_music() = 0;

    // Play a UI click sound.
    virtual void host_play_ui_click() = 0;

    // Play a result sound (victory/defeat).
    virtual void host_play_result_sound(const std::string& result) = 0;
};

// ---------- Scene factory ----------

// A factory function that creates a Scene instance. Registered with the
// SceneManager at startup.
using SceneFactory = std::function<std::unique_ptr<Scene>()>;

// ---------- Scene manager ----------

class SceneManager {
public:
    SceneManager();

    // Register a scene factory. Called once per scene at startup.
    void register_scene(SceneId id, SceneFactory factory);

    // Start the initial scene. Must be called after all scenes are
    // registered and before the first update/render.
    void start(SceneId initial, SceneContext& ctx);

    // Request a transition to a new scene. The transition is deferred —
    // it happens at the end of the current frame's update.
    void transition_to(SceneId to);

    // Per-frame update. Calls on_update on the current scene, then
    // applies any pending transition.
    void update(SceneContext& ctx);

    // Per-frame render. Calls on_render on the current scene.
    void render(SceneContext& ctx);

    // Handle a quit request. Returns true if the scene allows quit.
    bool handle_quit_request(SceneContext& ctx);

    // Clear all just_pressed input flags. Called after a scene transition
    // to prevent input carryover from the previous scene (e.g. a click that
    // triggered the transition should not also trigger actions in the new
    // scene on its first frame).
    void clear_input_edges(platform::Platform& platform);

    [[nodiscard]] SceneId current_id() const noexcept { return current_id_; }
    [[nodiscard]] bool has_pending_transition() const noexcept { return pending_.has_value(); }

    // [Wave 9B] The live scene object (for tests / host probes).
    [[nodiscard]] Scene* current() noexcept { return current_.get(); }
    [[nodiscard]] const Scene* current() const noexcept { return current_.get(); }

private:
    void apply_transition(SceneContext& ctx);

    std::unordered_map<SceneId, SceneFactory> factories_;
    std::unique_ptr<Scene> current_;
    SceneId current_id_ = SceneId::Boot;
    std::optional<SceneId> pending_;
};

}  // namespace resf2::scene

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
#include <vector>

namespace resf2::platform { class Platform; }
namespace resf2::renderer { class Renderer; }

namespace resf2::scene {

// ---------- Scene identifiers ----------

enum class SceneId : std::uint8_t {
    Boot,       // splash / engine init (before assets load)
    Loading,    // progress bar while assets warm up
    MainMenu,   // dojo with punching bag + scroll menu (current "Location")
    Map,        // act/episode/level selection
    Shop,       // equipment store (stub)
    Settings,   // options (stub)
    Dialogue,   // pre-battle dialogue overlay
    Battle,     // combat scene (proper fight, not just bag)
    Results,    // post-battle victory/defeat screen
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
    renderer::Renderer& renderer;
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

    // --- Battle data ---

    // Set the level/act to battle. Called from Map scene before transitioning
    // to Battle.
    virtual void host_set_current_level(std::string level_id) = 0;

    // Get the result of the last battle ("victory" / "defeat" / "").
    [[nodiscard]] virtual std::string host_get_battle_result() const = 0;
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

    [[nodiscard]] SceneId current_id() const noexcept { return current_id_; }
    [[nodiscard]] bool has_pending_transition() const noexcept { return pending_.has_value(); }

private:
    void apply_transition(SceneContext& ctx);

    std::unordered_map<SceneId, SceneFactory> factories_;
    std::unique_ptr<Scene> current_;
    SceneId current_id_ = SceneId::Boot;
    std::optional<SceneId> pending_;
};

}  // namespace resf2::scene

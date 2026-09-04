#pragma once

// App — the native app shell (main loop + platform window).
//
// Ports the game's frame driver (JS_MAP §1): `Pg.xeb` (L56) runs the
// fixed 60 Hz update decoupled from the render rate via the `Us`
// accumulator (L135: `Gy`/`Bm` = 1/60), and renders once per present with
// `Pg.Ea` (L57) -> `root.nja`. The native shell:
//
//   per present:
//     poll input (keyboard/pointer)          (JS input screens' `Y3()`)
//     accumulate dt, run fixed steps of 1/60:
//       screen_manager.update(1/60)          (JS `Pg.aa` -> mc.aa -> $d.aa)
//     screen_manager.render()                (JS `Pg.Ea` -> mc.Ea)
//     swap buffers
//
// The window is 1280x720 (the JS virtual resolution `N.rect` is 960x540
// at scale 1; the port uses 1280x720 so a windowed desktop app is usable —
// the layouts are placed in this view space).
//
// This class also owns the shared render resources the screens draw with:
// the GL renderer, the dojo background sprite, the menu font, and the
// user save. `core/app` is the shell layer; the GLFW/GL calls live in
// core/render (gl.hpp / renderer.hpp) — the shell stays platform-free
// except through those.

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "atlas.hpp"

struct GLFWwindow;

namespace sf2::render {
class Renderer;
struct Camera;
} // namespace sf2::render
namespace sf2::data {
struct Texture;
struct font;
} // namespace sf2::data
namespace sf2::scene {
struct Sprite;
} // namespace sf2::scene

namespace sf2::app {

class ScreenManager;
class SaveSystem;
struct WarriorSave;
struct FightAssets;
struct CatalogItem;

// The pending battle the Map -> Fight -> Results flow carries (JS `Da` +
// the `v.kD` results data: the battle, the reward, the winner).
struct PendingBattle {
    std::string battle_name = "Training";
    std::string location = "dojo";
    // The enemy's display name. The Dojo's training fight names its
    // "Punchbag" dummy (JS stages.xml Fight 1 Warrior FirstName="Punchbag");
    // the map flow keeps the default "Enemy".
    std::string enemy_name = "Enemy";
    int reward_money = 0;  // the first non-zero <Reward> of the fight
    int reward_exp = 0;
    // Prize snapshot (JS `v.kD`/`bzb`, FLOW_STATIC §4): filled by the
    // FightScreen at battle end from FightController::prize() BEFORE the
    // bonus is folded into reward_money, so Results can show the breakdown
    // (base + Perfect/FirstStrike/Combo/Shock lines). Gems are untracked by
    // prize() — no field here (see the stream report).
    int prize_base_coins = 0;
    int prize_bonus = 0;
    int prize_combo = 0;
    int prize_shocks = 0;
    bool prize_perfect = false;
    bool prize_first = false;
    // The fight outcome (set by the FightScreen at battle end).
    bool has_result = false;
    bool player_won = false;
    // The owned items the player's move list was built from.
    std::vector<std::pair<std::string, std::string>> owned;
};

// A text glyph run: screen-space position + the glyph quads. The shell
// renders text by cutting glyph rects from the font page texture (the
// game's `ea`/`Zb` text object, JS L1711).
struct TextLayout {
    float x = 0.0f;  // top-left
    float y = 0.0f;
    float scale = 1.0f;
    unsigned int font_tex = 0;
};

class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Creates the window + GL context, loads the shared assets, boots to
    // the main menu. Returns false on failure.
    bool init(const std::string& res_root, const std::string& save_path);

    // The main loop — runs until the window closes. `headless_frames`
    // > 0 runs that many frames then closes (used by the log-only verify
    // path). `auto_click` > 0 auto-clicks a screen coordinate once after
    // that many frames (the verify path clicks the Fight button).
    void run(int headless_frames = 0, bool auto_click = false);

    // Runs exactly one present frame (poll input + fixed-step updates +
    // render). Used by the headless-loop driver in app/game/main.cpp.
    void run_one_frame();

    void shutdown();

    // --- shared resources (screens draw through these) --------------------
    sf2::render::Renderer& renderer() { return *renderer_; }
    ScreenManager& screens() { return *screens_; }
    SaveSystem& save() { return *save_; }

    // The shared fight assets (models/moves/clips/tactics/location) —
    // loaded once at init, used by Fight/Shop/Equipment.
    FightAssets& fight_assets() { return *fight_assets_; }
    // Null-safe probe (asset load may fail while the shell still boots —
    // the Dojo idle figure uses this to skip display-only setup).
    bool has_fight_assets() const { return fight_assets_ != nullptr; }

    // The pending battle flow data (the Map -> Fight -> Results hand-off).
    PendingBattle& pending_battle() { return pending_battle_; }

    // The dojo background sprite (the main menu / map backdrop). Loaded at
    // init from the dojo location webp + atlas. Null when the asset is
    // unavailable.
    sf2::scene::Sprite* dojo_sprite() const { return dojo_sprite_.get(); }

    // The menu font (ui/font-en.fnt + png). Null when unavailable.
    const sf2::data::font* menu_font() const { return menu_font_.get(); }
    unsigned int font_texture() const { return font_tex_; }
    const sf2::data::font* digits_font() const { return digits_font_.get(); }
    unsigned int digits_texture() const { return digits_tex_; }
    const sf2::data::font* round_font() const { return round_font_.get(); }
    unsigned int round_texture() const { return round_tex_; }

    // View size (window pixels).
    int view_w() const { return view_w_; }
    int view_h() const { return view_h_; }

    // Captures the current frame to a PNG (uses core/render's capture).
    bool capture_png(const std::string& path);

    // --- input (polled once per present) -----------------------------------
    struct PointerState {
        double x = 0.0;
        double y = 0.0;
        bool down = false;       // button held this frame
        bool pressed = false;    // pressed this frame (edge)
    };
    const PointerState& pointer() const { return pointer_; }
    bool key_pressed(int glfw_key) const { return keys_pressed_.count(glfw_key) != 0; }

    // Test hook: inject a click at view coordinates (the JS `ma.Bd`/pointer
    // tap path). The click edge stays armed for `steps` fixed-update steps
    // so a fixed-step update reliably sees it (headless runs uncapped).
    void inject_click(double x, double y, int steps = 3) {
        injected_x_ = x;
        injected_y_ = y;
        injected_click_pending_ = true;
        injected_click_steps_ = steps;
    }

    // Test hook: inject a key press/release (GLFW key code). Routes to the
    // top screen's `on_key` (the FightScreen's input path). Defined in
    // app.cpp (needs the full ScreenManager type).
    void inject_key(int glfw_key, bool down);

    // The player's fight auto-attack (drives the FightController; the
    // FightScreen wires it at battle start when set).
    void set_auto_attack(bool on) { auto_attack_ = on; }
    bool auto_attack() const { return auto_attack_; }

    // Forces the headless stepping (one fixed step per frame) even with
    // headless_frames_ == 0 — the headless-loop driver runs uncapped.
    void set_headless_frames(int n) { headless_frames_ = n; }

    // --- text ---------------------------------------------------------------
    // Draws `text` at (x,y) top-left using the menu font. Returns false
    // when no font is loaded.
    bool draw_text(float x, float y, const std::string& text, float scale,
                   float r, float g, float b);
    // Atlas rect (x,y = top-left, w,h = size).
    bool draw_atlas_rect(const std::string& name, float x, float y, float w, float h,
                         float alpha = 1.0f);
    float measure_text(const sf2::data::font& font, const std::string& text,
                       float scale) const;
    bool draw_text_with_font(const sf2::data::font& font, unsigned int tex, float x,
                             float y, const std::string& text, float scale, float r,
                             float g, float b, float a = 1.0f);
    bool draw_text_centered(const sf2::data::font& font, unsigned int tex, float cx,
                            float y, const std::string& text, float scale, float r,
                            float g, float b, float a = 1.0f);

    // The users_default template path (res_root/users_default.xml) — used
    // by the SaveSystem.
    const std::string& res_root() const { return res_root_; }

    // Atlas frame cache for UI (menu/map/shop etc.) — populated at init.
    void register_atlas_frame(const sf2::data::atlas_frame& fr, int tex_w, int tex_h, unsigned int gl_tex);
    bool get_atlas_frame(const std::string& name, sf2::data::atlas_frame* out, int* tex_w, int* tex_h, unsigned int* gl_tex) const;
    // Draws an atlas frame centered at (cx,cy) with optional scale. Returns false if frame missing.
    bool draw_atlas_frame(const std::string& name, float cx, float cy, float scale, float alpha = 1.0f);

private:
    void poll_input();
    void update_fixed(float dt);
    void render_frame();

    // Called by ScreenManager boot; builds the initial MainMenu screen.
    void boot();

    int view_w_ = 1280;
    int view_h_ = 720;

    std::string res_root_;
    std::string save_path_;

    std::unique_ptr<sf2::render::Renderer> renderer_;
    std::unique_ptr<ScreenManager> screens_;
    std::unique_ptr<SaveSystem> save_;
    std::unique_ptr<FightAssets> fight_assets_;
    PendingBattle pending_battle_;

    // Shared assets.
    std::unique_ptr<sf2::scene::Sprite> dojo_sprite_;
    std::unique_ptr<sf2::data::font> menu_font_;
    unsigned int font_tex_ = 0;
    std::unique_ptr<sf2::data::font> digits_font_;
    unsigned int digits_tex_ = 0;
    std::unique_ptr<sf2::data::font> round_font_;
    unsigned int round_tex_ = 0;
    // UI atlas cache: frame name -> frame rect + tex size + GL tex.
    struct AtlasEntry {
        sf2::data::atlas_frame frame;
        int tex_w = 0;
        int tex_h = 0;
        unsigned int gl_tex = 0;
    };
    std::unordered_map<std::string, AtlasEntry> atlas_cache_;

    // Fixed-timestep accumulator (the JS `Us`, L135).
    double acc_ = 0.0;
    double last_time_ = 0.0;
    static constexpr double kFixedDt = 1.0 / 60.0;
    int fixed_steps_ = 0;

    PointerState pointer_;
    std::set<int> keys_pressed_;
    bool keys_held_[5] = {};  // the fight keys' held state (for edges)
    bool injected_click_pending_ = false;
    int injected_click_steps_ = 0;  // remaining fixed steps the click stays pressed
    double injected_x_ = 0.0;
    double injected_y_ = 0.0;

    // Headless/auto-click verify hooks.
    int headless_frames_ = 0;
    bool auto_click_ = false;
    bool auto_attack_ = false;
    int frame_count_ = 0;
    int auto_click_stage_ = 0;
};

} // namespace sf2::app

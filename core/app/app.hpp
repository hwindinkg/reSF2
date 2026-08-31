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
#include <vector>

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

    void shutdown();

    // --- shared resources (screens draw through these) --------------------
    sf2::render::Renderer& renderer() { return *renderer_; }
    ScreenManager& screens() { return *screens_; }
    SaveSystem& save() { return *save_; }

    // The dojo background sprite (the main menu / map backdrop). Loaded at
    // init from the dojo location webp + atlas. Null when the asset is
    // unavailable.
    sf2::scene::Sprite* dojo_sprite() const { return dojo_sprite_.get(); }

    // The menu font (ui/font-en.fnt + png). Null when unavailable.
    const sf2::data::font* menu_font() const { return menu_font_.get(); }
    unsigned int font_texture() const { return font_tex_; }

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

    // --- text ---------------------------------------------------------------
    // Draws `text` at (x,y) top-left using the menu font. Returns false
    // when no font is loaded.
    bool draw_text(float x, float y, const std::string& text, float scale,
                   float r, float g, float b);

    // The users_default template path (res_root/users_default.xml) — used
    // by the SaveSystem.
    const std::string& res_root() const { return res_root_; }

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

    // Shared assets.
    std::unique_ptr<sf2::scene::Sprite> dojo_sprite_;
    std::unique_ptr<sf2::data::font> menu_font_;
    unsigned int font_tex_ = 0;

    // Fixed-timestep accumulator (the JS `Us`, L135).
    double acc_ = 0.0;
    double last_time_ = 0.0;
    static constexpr double kFixedDt = 1.0 / 60.0;
    int fixed_steps_ = 0;

    PointerState pointer_;
    std::set<int> keys_pressed_;
    bool injected_click_pending_ = false;
    int injected_click_steps_ = 0;  // remaining fixed steps the click stays pressed
    double injected_x_ = 0.0;
    double injected_y_ = 0.0;

    // Headless/auto-click verify hooks.
    int headless_frames_ = 0;
    bool auto_click_ = false;
    int frame_count_ = 0;
    int auto_click_stage_ = 0;
};

} // namespace sf2::app

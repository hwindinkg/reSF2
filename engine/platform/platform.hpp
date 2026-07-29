// engine/platform/platform.hpp
//
// Platform abstraction layer.
//
// Stage 7.1 implementation. Provides a platform-neutral interface for:
//   - Window management (create, destroy, resize, fullscreen)
//   - GL context creation (GLES 2.0-compatible)
//   - Input polling (keyboard, mouse, touch, gamepad)
//   - Filesystem access (read-only for assets, read-write for saves)
//   - Time (wall-clock, monotonic)
//   - Threading primitives (yield, sleep)
//   - Pause/resume notifications (Android lifecycle, window minimization)
//
// Each target platform implements this interface:
//   - NullPlatform:     headless, for unit tests
//   - GlfwPlatform:     Windows / Linux / macOS (uses GLFW + glad)
//   - AndroidPlatform:  NativeActivity + EGL (Stage 7.1.x)
//   - SwitchPlatform:   libnx (optional, Stage 8)
//
// The Renderer (Stage 7.2) takes a Platform& and uses its GL callbacks.
// The Runtime (Stage 7.1) takes a Platform& and uses its event/loop callbacks.

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>

namespace resf2::platform {

// ---------- Time ----------

// Monotonic time in milliseconds since the platform was initialized.
// Never goes backwards. Use for frame timing.
[[nodiscard]] std::uint64_t now_ms() noexcept;

// ---------- Input state ----------

// Maximum number of simultaneous touch points / mouse buttons tracked.
inline constexpr std::size_t kMaxPointers = 16;

// Maximum number of keys tracked. Covers USB HID codes 0..255.
inline constexpr std::size_t kMaxKeys = 256;

// A single pointer (touch or mouse) state.
struct PointerState {
    std::int32_t id = -1;       // -1 = not pressed
    float x = 0.0f;             // window coords, top-left origin
    float y = 0.0f;
    bool pressed = false;       // currently down?
    bool just_pressed = false;  // went down this frame
    bool just_released = false; // went up this frame
};

// Keyboard key codes (USB HID, subset).
enum class Key : std::int32_t {
    Unknown = 0,
    Escape = 0x29,
    Enter  = 0x28,
    Space  = 0x2C,
    Tab    = 0x2B,
    Backspace = 0x2A,
    ArrowUp    = 0x52,
    ArrowDown  = 0x51,
    ArrowLeft  = 0x50,
    ArrowRight = 0x4F,
    A = 0x04, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0 = 0x1E, Num1 = 0x1F, Num2 = 0x20, Num3 = 0x21, Num4 = 0x22,
    Num5 = 0x23, Num6 = 0x24, Num7 = 0x25, Num8 = 0x26, Num9 = 0x27,
    F1 = 0x3A, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    ShiftLeft = 0xE1, ShiftRight = 0xE5,
    CtrlLeft  = 0xE0, CtrlRight  = 0xE4,
    AltLeft   = 0xE2, AltRight   = 0xE6,
};

// Snapshot of all input state for the current frame.
struct InputState {
    std::array<PointerState, kMaxPointers> pointers{};
    std::array<bool, kMaxKeys> keys_down{};        // currently down?
    std::array<bool, kMaxKeys> keys_just_pressed{}; // went down this frame?
    std::array<bool, kMaxKeys> keys_just_released{}; // went up this frame?
    std::int32_t mouse_delta_x = 0;  // for FPS-style camera (not used in SF2)
    std::int32_t mouse_delta_y = 0;
    float mouse_wheel = 0.0f;
};

// ---------- Window config ----------

struct WindowConfig {
    std::string title = "reSF2";
    std::int32_t width = 1280;
    std::int32_t height = 720;
    bool fullscreen = false;
    bool resizable = true;
    bool vsync = true;
    // GLES2 is the target. On desktop we use GL 2.1 + extensions to
    // emulate GLES2 semantics. On Android we use real EGL+GLES2.
    std::int32_t gl_major = 2;
    std::int32_t gl_minor = 0;
};

// ---------- Platform interface ----------

class Platform {
public:
    virtual ~Platform() = default;

    // ---- Lifecycle ----

    // Initialize the platform (create window, GL context, input devices).
    // Returns false on failure.
    [[nodiscard]] virtual bool init(const WindowConfig& config) = 0;

    // Shut down (destroy window, GL context, release resources).
    virtual void shutdown() noexcept = 0;

    // ---- Main loop integration ----

    // Poll for OS events (window, input, lifecycle). Call once per frame.
    // Returns false if the OS requested quit (window close, ALT+F4, etc.).
    [[nodiscard]] virtual bool poll_events() = 0;

    // Check if the platform has requested quit (window close button,
    // SIGTERM, Android back button at root screen, etc.).
    [[nodiscard]] virtual bool should_quit() const noexcept = 0;

    // Check if the platform is paused (window minimized, Android
    // Activity paused, etc.). When paused, the runtime should skip
    // updates but still poll events.
    [[nodiscard]] virtual bool is_paused() const noexcept = 0;

    // Yield to the OS for ~`ms` milliseconds. Pass 0 for a cooperative
    // yield without sleeping. Use this in the main loop to let other
    // processes run.
    virtual void sleep_ms(std::uint32_t ms) noexcept = 0;

    // ---- Time ----

    // Monotonic time in ms since init(). Use for frame timing.
    [[nodiscard]] virtual std::uint64_t now_ms() const noexcept = 0;

    // ---- Input ----

    // Get the input state for the current frame. Updated by poll_events().
    [[nodiscard]] virtual const InputState& input() const noexcept = 0;

    // ---- Window ----

    [[nodiscard]] virtual std::int32_t window_width() const noexcept = 0;
    [[nodiscard]] virtual std::int32_t window_height() const noexcept = 0;

    // Request window resize (async; the OS may not honor immediately).
    virtual void resize_window(std::int32_t w, std::int32_t h) noexcept = 0;

    // Set window title.
    virtual void set_title(std::string_view title) noexcept = 0;

    // Toggle fullscreen.
    virtual void set_fullscreen(bool fullscreen) noexcept = 0;

    // ---- GL context ----

    // Make the GL context current on the calling thread.
    // Returns false on failure.
    [[nodiscard]] virtual bool make_gl_current() noexcept = 0;

    // Swap the front/back buffers (present the rendered frame).
    virtual void swap_buffers() noexcept = 0;

    // ---- Filesystem ----

    // Read a file as bytes. Returns empty span on failure.
    // Path is relative to the asset root (set by the runtime).
    [[nodiscard]] virtual std::vector<std::byte> read_file(const std::string& path) const = 0;

    // Write a file (used for save games). Returns false on failure.
    [[nodiscard]] virtual bool write_file(const std::string& path,
                                          std::span<const std::byte> data) noexcept = 0;

    // Check if a file exists.
    [[nodiscard]] virtual bool file_exists(const std::string& path) const noexcept = 0;

    // Get the path to the user-writable save directory.
    [[nodiscard]] virtual std::string save_dir() const = 0;

    // ---- Pause/resume callbacks ----

    // Set callbacks for OS pause/resume events. Called from poll_events().
    using PauseCallback = std::function<void()>;
    virtual void set_pause_callback(PauseCallback cb) noexcept = 0;
    virtual void set_resume_callback(PauseCallback cb) noexcept = 0;

    // ---- Deterministic input replay (DIAGNOSTIC) ----
    //
    // [DIAGNOSTIC] Loads a text script of timed keydown/keyup events that
    // are applied ON TOP of the real platform input during poll_events().
    // This does NOT replace the production input backend (GLFW callbacks on
    // Linux, GetAsyncKeyState on Windows) — it merges scripted key state
    // into the same InputState the game reads, so combat/movement logic
    // exercises the identical code path as a human player.
    //
    // Script format (one event per line, '#' comments allowed):
    //   frame <N> keydown <KEY>
    //   frame <N> keyup <KEY>
    // where <KEY> is a Key enum name (W,A,S,D,O,P,Space,ShiftLeft,...) and
    // <N> is the 1-based frame index since on_init().
    //
    // Returns false on parse error (with a message on stderr). A successful
    // load arms the script; it replays once and then the platform continues
    // with real input only.
    [[nodiscard]] virtual bool load_input_script(const std::string& path) noexcept = 0;

    // [DIAGNOSTIC] Advance the input-script frame counter and apply any
    // events scheduled for the new frame. Called once per gameplay frame
    // (from host_update_gameplay), NOT from poll_events, so script frame N
    // aligns with gameplay frame N (Boot/Loading frames don't count).
    virtual void tick_input_script() noexcept = 0;
};

// ---------- Platform factory ----------

// Create a platform instance for the current OS.
//   - On Windows/Linux/macOS: returns a GlfwPlatform (uses GLFW + glad).
//   - On Android: returns an AndroidPlatform (NativeActivity + EGL).
//   - In tests: pass "null" to get a NullPlatform (headless).
[[nodiscard]] std::unique_ptr<Platform> create_platform(std::string_view backend_name = "");

// ---------- Null platform (for unit tests) ----------

// A platform that does nothing — no window, no GL, no input. Useful for
// testing the runtime / asset manager / scene graph without a display.
class NullPlatform : public Platform {
public:
    NullPlatform();
    ~NullPlatform() override;

    [[nodiscard]] bool init(const WindowConfig& config) override;
    void shutdown() noexcept override;
    [[nodiscard]] bool poll_events() override;
    [[nodiscard]] bool should_quit() const noexcept override;
    [[nodiscard]] bool is_paused() const noexcept override;
    void sleep_ms(std::uint32_t ms) noexcept override;
    [[nodiscard]] std::uint64_t now_ms() const noexcept override;
    [[nodiscard]] const InputState& input() const noexcept override;
    [[nodiscard]] std::int32_t window_width() const noexcept override;
    [[nodiscard]] std::int32_t window_height() const noexcept override;
    void resize_window(std::int32_t w, std::int32_t h) noexcept override;
    void set_title(std::string_view title) noexcept override;
    void set_fullscreen(bool fullscreen) noexcept override;
    [[nodiscard]] bool make_gl_current() noexcept override;
    void swap_buffers() noexcept override;
    [[nodiscard]] std::vector<std::byte> read_file(const std::string& path) const override;
    [[nodiscard]] bool write_file(const std::string& path,
                                  std::span<const std::byte> data) noexcept override;
    [[nodiscard]] bool file_exists(const std::string& path) const noexcept override;
    [[nodiscard]] std::string save_dir() const override;
    void set_pause_callback(PauseCallback cb) noexcept override;
    void set_resume_callback(PauseCallback cb) noexcept override;
    [[nodiscard]] bool load_input_script(const std::string& path) noexcept override;
    void tick_input_script() noexcept override;

    // Test helpers (not in the base interface)
    void inject_quit_request() noexcept { quit_requested_ = true; }
    void inject_pause() noexcept { paused_ = true; if (pause_cb_) pause_cb_(); }
    void inject_resume() noexcept { paused_ = false; if (resume_cb_) resume_cb_(); }
    void inject_key_down(Key k) noexcept;
    void inject_key_up(Key k) noexcept;
    void inject_pointer_down(std::int32_t id, float x, float y) noexcept;
    void inject_pointer_up(std::int32_t id) noexcept;
    void inject_pointer_move(std::int32_t id, float x, float y) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool quit_requested_ = false;
    bool paused_ = false;
    InputState input_{};
    PauseCallback pause_cb_;
    PauseCallback resume_cb_;
    std::uint64_t start_time_ms_ = 0;
    WindowConfig config_{};
};

}  // namespace resf2::platform

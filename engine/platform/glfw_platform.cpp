// engine/platform/glfw_platform.cpp
//
// GLFW-based platform backend implementation.
// Uses GLFW3 for window/input + system OpenGL for GL context.
//
// IMPORTANT (Windows only): GLFW on some Windows 10 builds (notably 19044)
// delivers spurious GLFW_RELEASE events for held keys, which makes the
// GLFW key state flicker true→false→true every frame and breaks any
// state machine that depends on stable key state (movement in reSF2).
// To work around this, on Windows we bypass GLFW's key event system
// entirely and query the OS keyboard directly via GetAsyncKeyState().
// GLFW is still used for window management, GL context, mouse and
// lifecycle events.

#include "glfw_platform.hpp"

#include <GLFW/glfw3.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
// Define WIN32_LEAN_AND_MEAN to keep <windows.h> small; we only need
// GetAsyncKeyState() and the VK_* constants.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX 1
#  endif
#  include <windows.h>
#endif

namespace resf2::platform {

namespace fs = std::filesystem;
namespace chrono = std::chrono;

// Convert reSF2 Key enum to GLFW key code
static int key_to_glfw(Key k) {
    switch (k) {
        case Key::Escape:     return GLFW_KEY_ESCAPE;
        case Key::Enter:      return GLFW_KEY_ENTER;
        case Key::Space:      return GLFW_KEY_SPACE;
        case Key::Tab:        return GLFW_KEY_TAB;
        case Key::Backspace:  return GLFW_KEY_BACKSPACE;
        case Key::ArrowUp:    return GLFW_KEY_UP;
        case Key::ArrowDown:  return GLFW_KEY_DOWN;
        case Key::ArrowLeft:  return GLFW_KEY_LEFT;
        case Key::ArrowRight: return GLFW_KEY_RIGHT;
        case Key::A: return GLFW_KEY_A;
        case Key::B: return GLFW_KEY_B;
        case Key::C: return GLFW_KEY_C;
        case Key::D: return GLFW_KEY_D;
        case Key::E: return GLFW_KEY_E;
        case Key::F: return GLFW_KEY_F;
        case Key::G: return GLFW_KEY_G;
        case Key::H: return GLFW_KEY_H;
        case Key::I: return GLFW_KEY_I;
        case Key::J: return GLFW_KEY_J;
        case Key::K: return GLFW_KEY_K;
        case Key::L: return GLFW_KEY_L;
        case Key::M: return GLFW_KEY_M;
        case Key::N: return GLFW_KEY_N;
        case Key::O: return GLFW_KEY_O;
        case Key::P: return GLFW_KEY_P;
        case Key::Q: return GLFW_KEY_Q;
        case Key::R: return GLFW_KEY_R;
        case Key::S: return GLFW_KEY_S;
        case Key::T: return GLFW_KEY_T;
        case Key::U: return GLFW_KEY_U;
        case Key::V: return GLFW_KEY_V;
        case Key::W: return GLFW_KEY_W;
        case Key::X: return GLFW_KEY_X;
        case Key::Y: return GLFW_KEY_Y;
        case Key::Z: return GLFW_KEY_Z;
        case Key::Num0: return GLFW_KEY_0;
        case Key::Num1: return GLFW_KEY_1;
        case Key::Num2: return GLFW_KEY_2;
        case Key::Num3: return GLFW_KEY_3;
        case Key::Num4: return GLFW_KEY_4;
        case Key::Num5: return GLFW_KEY_5;
        case Key::Num6: return GLFW_KEY_6;
        case Key::Num7: return GLFW_KEY_7;
        case Key::Num8: return GLFW_KEY_8;
        case Key::Num9: return GLFW_KEY_9;
        case Key::F1: return GLFW_KEY_F1;
        case Key::F2: return GLFW_KEY_F2;
        case Key::F3: return GLFW_KEY_F3;
        case Key::F4: return GLFW_KEY_F4;
        case Key::F5: return GLFW_KEY_F5;
        case Key::F6: return GLFW_KEY_F6;
        case Key::F7: return GLFW_KEY_F7;
        case Key::F8: return GLFW_KEY_F8;
        case Key::F9: return GLFW_KEY_F9;
        case Key::F10: return GLFW_KEY_F10;
        case Key::F11: return GLFW_KEY_F11;
        case Key::F12: return GLFW_KEY_F12;
        case Key::ShiftLeft:  return GLFW_KEY_LEFT_SHIFT;
        case Key::ShiftRight: return GLFW_KEY_RIGHT_SHIFT;
        case Key::CtrlLeft:   return GLFW_KEY_LEFT_CONTROL;
        case Key::CtrlRight:  return GLFW_KEY_RIGHT_CONTROL;
        case Key::AltLeft:    return GLFW_KEY_LEFT_ALT;
        case Key::AltRight:   return GLFW_KEY_RIGHT_ALT;
        default: return GLFW_KEY_UNKNOWN;
    }
}

// Convert GLFW key code to reSF2 Key enum index
static int glfw_to_key_index(int glfw_key) {
    // We use a reverse mapping. For simplicity, iterate all keys.
    // In production, use a lookup table.
    for (int i = 0; i < static_cast<int>(Key::AltRight) + 1; ++i) {
        if (key_to_glfw(static_cast<Key>(i)) == glfw_key) return i;
    }
    return -1;
}

// Reverse mapping: key index → GLFW key code
static int key_index_to_glfw(int idx) {
    if (idx < 0 || idx > static_cast<int>(Key::AltRight)) return -1;
    return key_to_glfw(static_cast<Key>(idx));
}

#ifdef _WIN32
// Map a GLFW key code to a Win32 Virtual Key code.
// Returns -1 if no meaningful mapping exists (e.g. GLFW_KEY_UNKNOWN).
//
// We translate the GLFW code (which mirrors USB HID usage codes for most
// keys) into the corresponding Win32 VK code. GetAsyncKeyState() takes
// a VK code and reports the realtime OS-level key state.
static int glfw_key_to_vk(int glfw_key) {
    switch (glfw_key) {
        // Letters
        case GLFW_KEY_A: return 'A';
        case GLFW_KEY_B: return 'B';
        case GLFW_KEY_C: return 'C';
        case GLFW_KEY_D: return 'D';
        case GLFW_KEY_E: return 'E';
        case GLFW_KEY_F: return 'F';
        case GLFW_KEY_G: return 'G';
        case GLFW_KEY_H: return 'H';
        case GLFW_KEY_I: return 'I';
        case GLFW_KEY_J: return 'J';
        case GLFW_KEY_K: return 'K';
        case GLFW_KEY_L: return 'L';
        case GLFW_KEY_M: return 'M';
        case GLFW_KEY_N: return 'N';
        case GLFW_KEY_O: return 'O';
        case GLFW_KEY_P: return 'P';
        case GLFW_KEY_Q: return 'Q';
        case GLFW_KEY_R: return 'R';
        case GLFW_KEY_S: return 'S';
        case GLFW_KEY_T: return 'T';
        case GLFW_KEY_U: return 'U';
        case GLFW_KEY_V: return 'V';
        case GLFW_KEY_W: return 'W';
        case GLFW_KEY_X: return 'X';
        case GLFW_KEY_Y: return 'Y';
        case GLFW_KEY_Z: return 'Z';
        // Digits (top row)
        case GLFW_KEY_0: return '0';
        case GLFW_KEY_1: return '1';
        case GLFW_KEY_2: return '2';
        case GLFW_KEY_3: return '3';
        case GLFW_KEY_4: return '4';
        case GLFW_KEY_5: return '5';
        case GLFW_KEY_6: return '6';
        case GLFW_KEY_7: return '7';
        case GLFW_KEY_8: return '8';
        case GLFW_KEY_9: return '9';
        // Function keys
        case GLFW_KEY_F1:  return VK_F1;
        case GLFW_KEY_F2:  return VK_F2;
        case GLFW_KEY_F3:  return VK_F3;
        case GLFW_KEY_F4:  return VK_F4;
        case GLFW_KEY_F5:  return VK_F5;
        case GLFW_KEY_F6:  return VK_F6;
        case GLFW_KEY_F7:  return VK_F7;
        case GLFW_KEY_F8:  return VK_F8;
        case GLFW_KEY_F9:  return VK_F9;
        case GLFW_KEY_F10: return VK_F10;
        case GLFW_KEY_F11: return VK_F11;
        case GLFW_KEY_F12: return VK_F12;
        // Navigation / editing
        case GLFW_KEY_ESCAPE:    return VK_ESCAPE;
        case GLFW_KEY_ENTER:     return VK_RETURN;
        case GLFW_KEY_SPACE:     return VK_SPACE;
        case GLFW_KEY_TAB:       return VK_TAB;
        case GLFW_KEY_BACKSPACE: return VK_BACK;
        case GLFW_KEY_UP:        return VK_UP;
        case GLFW_KEY_DOWN:      return VK_DOWN;
        case GLFW_KEY_LEFT:      return VK_LEFT;
        case GLFW_KEY_RIGHT:     return VK_RIGHT;
        // Modifiers — distinguish left/right via VK_LSHIFT/VK_RSHIFT etc.
        // GetAsyncKeyState() supports both the generic (VK_SHIFT) and the
        // handed (VK_LSHIFT) codes; we use the handed ones to match the
        // Key enum granularity.
        case GLFW_KEY_LEFT_SHIFT:   return VK_LSHIFT;
        case GLFW_KEY_RIGHT_SHIFT:  return VK_RSHIFT;
        case GLFW_KEY_LEFT_CONTROL: return VK_LCONTROL;
        case GLFW_KEY_RIGHT_CONTROL:return VK_RCONTROL;
        case GLFW_KEY_LEFT_ALT:     return VK_LMENU;
        case GLFW_KEY_RIGHT_ALT:    return VK_RMENU;
        default: return -1;
    }
}
#endif  // _WIN32

struct GlfwPlatform::Impl {
    GLFWwindow* window = nullptr;
    InputState input{};
    bool quit_requested = false;
    bool paused = false;
    std::uint64_t start_time_ms = 0;
    PauseCallback pause_cb;
    PauseCallback resume_cb;
    std::string asset_root;

#ifdef _WIN32
    // On Windows we poll GetAsyncKeyState() every frame instead of trusting
    // GLFW key callbacks. To compute edge transitions (just_pressed /
    // just_released) we need the previous frame's down-state per key.
    std::array<bool, kMaxKeys> prev_keys_down_{};
#endif

    static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
        Impl* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (!self) return;

#ifdef _WIN32
        // On Windows: only use GLFW for just_pressed (fast tap detection).
        // Ignore GLFW_RELEASE entirely (spurious on Win10 19044).
        // Ignore GLFW_REPEAT (causes infinite key repeat).
        if (action != GLFW_PRESS) return;

        int idx = glfw_to_key_index(key);
        if (idx < 0 || idx >= static_cast<int>(kMaxKeys)) return;

        // Only set just_pressed if not already down (prevents repeat)
        if (!self->input.keys_down[idx]) {
            self->input.keys_just_pressed[idx] = true;
        }
#else
        int idx = glfw_to_key_index(key);
        if (idx < 0 || idx >= static_cast<int>(kMaxKeys)) return;

        if (action == GLFW_PRESS) {
            if (!self->input.keys_down[idx]) {
                self->input.keys_just_pressed[idx] = true;
            }
            self->input.keys_down[idx] = true;
        } else if (action == GLFW_RELEASE) {
            if (self->input.keys_down[idx]) {
                self->input.keys_just_released[idx] = true;
            }
            self->input.keys_down[idx] = false;
        }
#endif
    }

    static void mouse_button_callback(GLFWwindow* w, int button, int action, int mods) {
        Impl* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        double x, y;
        glfwGetCursorPos(w, &x, &y);
        int id = button;  // mouse button 0/1/2
        if (action == GLFW_PRESS) {
            for (auto& p : self->input.pointers) {
                if (p.id == -1) {
                    p.id = id;
                    p.x = static_cast<float>(x);
                    p.y = static_cast<float>(y);
                    p.pressed = true;
                    p.just_pressed = true;
                    return;
                }
            }
        } else if (action == GLFW_RELEASE) {
            for (auto& p : self->input.pointers) {
                if (p.id == id) {
                    p.pressed = false;
                    p.just_released = true;
                    p.id = -1;
                    return;
                }
            }
        }
    }

    static void cursor_pos_callback(GLFWwindow* w, double x, double y) {
        Impl* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        // Update the first pressed pointer
        for (auto& p : self->input.pointers) {
            if (p.id == 0 && p.pressed) {
                p.x = static_cast<float>(x);
                p.y = static_cast<float>(y);
                return;
            }
        }
    }

    static void window_close_callback(GLFWwindow* w) {
        Impl* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (self) self->quit_requested = true;
    }

    static void window_focus_callback(GLFWwindow* w, int focused) {
        Impl* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        if (focused) {
            if (self->paused) {
                self->paused = false;
                if (self->resume_cb) self->resume_cb();
            }
        } else {
            if (!self->paused) {
                self->paused = true;
                if (self->pause_cb) self->pause_cb();
            }
        }
    }

    static void framebuffer_size_callback(GLFWwindow* w, int width, int height) {
        // GL viewport will be set by the renderer
    }
};

GlfwPlatform::GlfwPlatform() : impl_(std::make_unique<Impl>()) {
    impl_->start_time_ms = now_ms();
}

GlfwPlatform::~GlfwPlatform() {
    shutdown();
}

bool GlfwPlatform::init(const WindowConfig& config) {
    if (!glfwInit()) {
        std::fprintf(stderr, "GlfwPlatform: glfwInit() failed\n");
        return false;
    }

    // Request OpenGL 2.1 context (GLES2 stand-in)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    GLFWmonitor* monitor = nullptr;
    if (config.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
    }

    impl_->window = glfwCreateWindow(
        config.width, config.height,
        config.title.c_str(),
        monitor, nullptr);

    if (!impl_->window) {
        std::fprintf(stderr, "GlfwPlatform: glfwCreateWindow() failed\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(impl_->window);
    glfwSwapInterval(config.vsync ? 1 : 0);

    // Set callbacks
    glfwSetWindowUserPointer(impl_->window, impl_.get());
    glfwSetKeyCallback(impl_->window, Impl::key_callback);
    glfwSetMouseButtonCallback(impl_->window, Impl::mouse_button_callback);
    glfwSetCursorPosCallback(impl_->window, Impl::cursor_pos_callback);
    glfwSetWindowCloseCallback(impl_->window, Impl::window_close_callback);
    glfwSetWindowFocusCallback(impl_->window, Impl::window_focus_callback);
    glfwSetFramebufferSizeCallback(impl_->window, Impl::framebuffer_size_callback);

    impl_->start_time_ms = now_ms();
    impl_->quit_requested = false;
    impl_->paused = false;

    return true;
}

void GlfwPlatform::shutdown() noexcept {
    if (impl_->window) {
        glfwDestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    glfwTerminate();
}

bool GlfwPlatform::poll_events() {
    // Clear per-frame input flags
    for (auto& p : impl_->input.pointers) {
        p.just_pressed = false;
        p.just_released = false;
    }
    // Don't reset keys_just_pressed/released here — do it AFTER glfwPollEvents()
    // so GLFW callbacks during glfwPollEvents() can set them.
    impl_->input.mouse_delta_x = 0;
    impl_->input.mouse_delta_y = 0;
    impl_->input.mouse_wheel = 0.0f;

    glfwPollEvents();

    // Now reset just_pressed/released — but ONLY for keys that GetAsyncKeyState
    // will handle. GLFW callbacks may have already set some during glfwPollEvents().
    // We save the GLFW-set values and merge them after GetAsyncKeyState.
    auto glfw_just_pressed = impl_->input.keys_just_pressed;
    auto glfw_just_released = impl_->input.keys_just_released;
    impl_->input.keys_just_pressed.fill(false);
    impl_->input.keys_just_released.fill(false);

#ifdef _WIN32
    // Win32 path: bypass GLFW key state entirely.
    //
    // GLFW on Windows 10 (build 19044) emits spurious GLFW_RELEASE events
    // for held keys, which causes keys_down to flicker true→false→true
    // every frame. This breaks the movement state machine in main.cpp
    // (step_forward ↔ fists_idle jitter). All 7 previously attempted
    // GLFW-only workarounds failed because the bad RELEASE events are
    // indistinguishable from real ones at the GLFW layer.
    //
    // GetAsyncKeyState(vk) returns a SHORT where the high bit (0x8000)
    // is set iff the key is currently down at the OS level. This is the
    // same source of truth that DirectInput / RawInput derive from, and
    // is immune to GLFW's event-queue glitches.
    //
    // We iterate over every key index in our Key enum (0..AltRight), map
    // it to a GLFW code, then to a Win32 VK code, and poll the OS. Edge
    // transitions are computed against prev_keys_down_ so that
    // keys_just_pressed / keys_just_released remain accurate.
    const int kMaxKeyIdx = static_cast<int>(Key::AltRight) + 1;
    for (int i = 0; i < kMaxKeyIdx; ++i) {
        int glfw_key = key_index_to_glfw(i);
        if (glfw_key < 0) {
            impl_->input.keys_down[i] = false;
            impl_->prev_keys_down_[i] = false;
            continue;
        }
        int vk = glfw_key_to_vk(glfw_key);
        if (vk < 0) {
            // Key is in our enum but has no Win32 VK mapping — leave
            // its state untouched (treated as not down).
            impl_->input.keys_down[i] = false;
            impl_->prev_keys_down_[i] = false;
            continue;
        }
        SHORT state = GetAsyncKeyState(vk);
        bool down = (state & 0x8000) != 0;
        bool was_down = impl_->prev_keys_down_[i];

        // Only set keys_just_pressed if GLFW callback didn't already set it.
        // GetAsyncKeyState can miss fast taps, but GLFW callback catches them.
        if (down && !was_down && !impl_->input.keys_just_pressed[i]) {
            impl_->input.keys_just_pressed[i] = true;
        } else if (!down && was_down) {
            impl_->input.keys_just_released[i] = true;
        }
        impl_->input.keys_down[i] = down;
        impl_->prev_keys_down_[i] = down;
    }
#else
    // Non-Windows path: rely on the GLFW key callback (registered in
    // init()) for keys_down / keys_just_pressed / keys_just_released.
    // Sticky-keys mode ensures we never miss a PRESS event between polls.
    glfwSetInputMode(impl_->window, GLFW_STICKY_KEYS, GLFW_TRUE);
#endif

    // On Windows: also enable sticky keys for GLFW callbacks to catch
    // fast key presses that GetAsyncKeyState might miss between frames.
#ifdef _WIN32
    glfwSetInputMode(impl_->window, GLFW_STICKY_KEYS, GLFW_TRUE);

    // Merge GLFW callback-set just_pressed/released with GetAsyncKeyState results.
    // GLFW catches fast taps that GetAsyncKeyState might miss.
    for (size_t i = 0; i < kMaxKeys; ++i) {
        if (glfw_just_pressed[i]) {
            impl_->input.keys_just_pressed[i] = true;
        }
        if (glfw_just_released[i]) {
            impl_->input.keys_just_released[i] = true;
        }
    }
#endif

    return !impl_->quit_requested;
}

bool GlfwPlatform::should_quit() const noexcept {
    return impl_->quit_requested;
}

bool GlfwPlatform::is_paused() const noexcept {
    return impl_->paused;
}

void GlfwPlatform::sleep_ms(std::uint32_t ms) noexcept {
    if (ms > 0) {
        std::this_thread::sleep_for(chrono::milliseconds(ms));
    } else {
        std::this_thread::yield();
    }
}

std::uint64_t GlfwPlatform::now_ms() const noexcept {
    return resf2::platform::now_ms() - impl_->start_time_ms;
}

const InputState& GlfwPlatform::input() const noexcept {
    return impl_->input;
}

std::int32_t GlfwPlatform::window_width() const noexcept {
    if (!impl_->window) return 0;
    int w, h;
    glfwGetWindowSize(impl_->window, &w, &h);
    return w;
}

std::int32_t GlfwPlatform::window_height() const noexcept {
    if (!impl_->window) return 0;
    int w, h;
    glfwGetWindowSize(impl_->window, &w, &h);
    return h;
}

void GlfwPlatform::resize_window(std::int32_t w, std::int32_t h) noexcept {
    if (impl_->window) {
        glfwSetWindowSize(impl_->window, w, h);
    }
}

void GlfwPlatform::set_title(std::string_view title) noexcept {
    if (impl_->window) {
        glfwSetWindowTitle(impl_->window, std::string(title).c_str());
    }
}

void GlfwPlatform::set_fullscreen(bool fullscreen) noexcept {
    // TODO: implement fullscreen toggle
    (void)fullscreen;
}

bool GlfwPlatform::make_gl_current() noexcept {
    if (!impl_->window) return false;
    glfwMakeContextCurrent(impl_->window);
    return true;
}

void GlfwPlatform::swap_buffers() noexcept {
    if (impl_->window) {
        glfwSwapBuffers(impl_->window);
    }
}

std::vector<std::byte> GlfwPlatform::read_file(const std::string& path) const {
    // Try asset root first, then absolute path
    std::string full = path;
    if (!impl_->asset_root.empty() && !fs::path(path).is_absolute()) {
        full = impl_->asset_root + "/" + path;
    }
    std::ifstream f(full, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = static_cast<std::size_t>(f.tellg());
    if (size == 0) return {};
    f.seekg(0);
    std::vector<std::byte> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
    if (!f) return {};
    return buf;
}

bool GlfwPlatform::write_file(const std::string& path,
                              std::span<const std::byte> data) noexcept {
    try {
        if (auto parent = fs::path(path).parent_path(); !parent.empty()) {
            fs::create_directories(parent);
        }
    } catch (...) {}
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return f.good();
}

bool GlfwPlatform::file_exists(const std::string& path) const noexcept {
    std::error_code ec;
    return fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

std::string GlfwPlatform::save_dir() const {
    return fs::temp_directory_path().string();
}

void GlfwPlatform::set_pause_callback(PauseCallback cb) noexcept {
    impl_->pause_cb = std::move(cb);
}

void GlfwPlatform::set_resume_callback(PauseCallback cb) noexcept {
    impl_->resume_cb = std::move(cb);
}

}  // namespace resf2::platform

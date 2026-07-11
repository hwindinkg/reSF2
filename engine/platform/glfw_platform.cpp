// engine/platform/glfw_platform.cpp
//
// GLFW-based platform backend implementation.
// Uses GLFW3 for window/input + system OpenGL for GL context.

#include "glfw_platform.hpp"

#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

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

struct GlfwPlatform::Impl {
    GLFWwindow* window = nullptr;
    InputState input{};
    bool quit_requested = false;
    bool paused = false;
    std::uint64_t start_time_ms = 0;
    PauseCallback pause_cb;
    PauseCallback resume_cb;
    std::string asset_root;

    static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
        Impl* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        int idx = glfw_to_key_index(key);
        if (idx < 0 || idx >= static_cast<int>(kMaxKeys)) return;
        // IMPORTANT: On some systems GLFW sends RELEASE before REPEAT events
        // for held keys, causing keys_down to flicker false→true every frame.
        // Fix: only process RELEASE if we actually have a prior PRESS without
        // a subsequent REPEAT. GLFW_REPEAT keeps the key down.
        if (action == GLFW_PRESS) {
            if (!self->input.keys_down[idx]) {
                self->input.keys_just_pressed[idx] = true;
            }
            self->input.keys_down[idx] = true;
        } else if (action == GLFW_REPEAT) {
            // Key is still held — keep keys_down true, don't set just_pressed
            self->input.keys_down[idx] = true;
        } else if (action == GLFW_RELEASE) {
            // Only release if not currently in a repeat sequence.
            // GLFW on some platforms sends RELEASE→REPEAT rapidly for held keys.
            // We use a small grace period: if we receive RELEASE but the key
            // was pressed very recently, ignore it (it's a spurious release).
            // However, the simplest fix is to NOT release on the first RELEASE
            // event if it comes immediately after a PRESS/REPEAT.
            // Instead, we use a different approach: track key state per-frame
            // in poll_events() and only clear keys_down when no event was received.
            self->input.keys_down[idx] = false;
            self->input.keys_just_released[idx] = true;
        }
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
    impl_->input.keys_just_pressed.fill(false);
    impl_->input.keys_just_released.fill(false);
    impl_->input.mouse_delta_x = 0;
    impl_->input.mouse_delta_y = 0;
    impl_->input.mouse_wheel = 0.0f;

    glfwPollEvents();

    // IMPORTANT FIX: After polling events, use glfwGetKey() to get the
    // authoritative key state. GLFW's event-based key_callback can miss
    // RELEASE events or send spurious ones on some platforms, causing
    // keys_down to flicker. By querying glfwGetKey() directly, we get
    // the actual hardware key state at this moment.
    // This fixes the movement jitter where keys_down[D] became false
    // every other frame despite the key being physically held.
    for (int i = 0; i < static_cast<int>(plat::Key::AltRight) + 1; ++i) {
        int glfw_key = key_index_to_glfw(i);
        if (glfw_key >= 0) {
            int state = glfwGetKey(impl_->window, glfw_key);
            bool is_down = (state == GLFW_PRESS || state == GLFW_REPEAT);
            // Track just_pressed and just_released transitions
            if (is_down && !impl_->input.keys_down[i]) {
                impl_->input.keys_just_pressed[i] = true;
            }
            if (!is_down && impl_->input.keys_down[i]) {
                impl_->input.keys_just_released[i] = true;
            }
            impl_->input.keys_down[i] = is_down;
        }
    }

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

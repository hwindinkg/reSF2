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
#include <unordered_map>
#include <vector>

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

    // [DIAGNOSTIC] Deterministic input replay script.
    // Loaded via load_input_script(); applied during poll_events() on top
    // of the real GLFW/GetAsyncKeyState input state.
    struct ScriptEvent {
        std::uint64_t frame = 0;       // 1-based frame index
        int key_idx = -1;              // Key enum index
        bool down = true;              // true=keydown, false=keyup
    };
    struct InputScript {
        std::vector<ScriptEvent> events;
        std::uint64_t cur = 0;         // next event index
        std::uint64_t frame_counter = 0;  // incremented each poll_events()
        bool armed = false;
        // Persistent key state from script events (survives GetAsyncKeyState
        // overwrite in poll_events). Only used when armed=true.
        std::array<bool, kMaxKeys> keys_down{};
    };
    InputScript input_script{};

#ifdef _WIN32
    // On Windows we poll GetAsyncKeyState() every frame instead of trusting
    // GLFW key callbacks. To compute edge transitions (just_pressed /
    // just_released) we need the previous frame's down-state per key.
    std::array<bool, kMaxKeys> prev_keys_down_{};
#endif
    std::array<bool, kMaxKeys> glfw_key_consumed_{};  // prevents spurious GLFW_PRESS re-trigger

    static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
        Impl* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (!self) return;

#ifdef _WIN32
        // On Windows: IGNORE all GLFW key events.
        // GetAsyncKeyState polling in poll_events() handles everything.
        // GLFW on Win10 19044 has spurious events that cause key repeat bugs.
        (void)key; (void)scancode; (void)action; (void)mods;
        return;
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
    // Reset per-frame input state BEFORE glfwPollEvents and GetAsyncKeyState.
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

    // [DIAGNOSTIC] When input script is armed, skip real keyboard polling
    // entirely — input comes ONLY from the script. This ensures deterministic
    // replay without real-keyboard interference.
    if (impl_->input_script.armed) {
        // Advance prev_keys_down_ so edge detection works next frame.
        for (int i = 0; i < kMaxKeys; ++i) {
            impl_->prev_keys_down_[i] = impl_->input.keys_down[i];
        }
        return !impl_->quit_requested;
    }

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
            impl_->glfw_key_consumed_[i] = true;  // prevent GLFW re-trigger
        } else if (!down && was_down) {
            impl_->input.keys_just_released[i] = true;
            impl_->glfw_key_consumed_[i] = false;  // reset for next press
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

    return !impl_->quit_requested;
}

// [DIAGNOSTIC] Apply queued input-script events for the current frame.
// Events are ordered by frame index; we apply all events whose frame ==
// current frame counter (1-based). keydown sets keys_down + keys_just_pressed;
// keyup clears keys_down + sets keys_just_released. This overrides any real
// input state for that key this frame, which is intentional for determinism.
//
// IMPORTANT: frame_counter is incremented HERE (not in poll_events) so that
// script frames align with gameplay frames (host_update_gameplay calls),
// not with raw poll_events calls (which also happen during Boot/Loading
// before gameplay starts). This keeps script frame N == gameplay frame N.
void GlfwPlatform::apply_input_script() noexcept {
    auto& s = impl_->input_script;
    if (!s.armed) return;
    while (s.cur < s.events.size() && s.events[s.cur].frame <= s.frame_counter) {
        const auto& ev = s.events[s.cur];
        if (ev.key_idx >= 0 && ev.key_idx < (int)kMaxKeys) {
            if (ev.down) {
                if (!impl_->input.keys_down[ev.key_idx]) {
                    impl_->input.keys_just_pressed[ev.key_idx] = true;
                }
                impl_->input.keys_down[ev.key_idx] = true;
                impl_->input_script.keys_down[ev.key_idx] = true;
            } else {
                if (impl_->input.keys_down[ev.key_idx]) {
                    impl_->input.keys_just_released[ev.key_idx] = true;
                }
                impl_->input.keys_down[ev.key_idx] = false;
                impl_->input_script.keys_down[ev.key_idx] = false;
            }
        }
        ++s.cur;
    }
    ++s.frame_counter;
}

// Parse a Key enum name (e.g. "W", "O", "ShiftLeft", "Space") into its index.
// Returns -1 on unknown name.
static int parse_key_name(const std::string& name) {
    static const std::unordered_map<std::string, int> map = {
        {"Escape", (int)Key::Escape}, {"Enter", (int)Key::Enter},
        {"Space", (int)Key::Space}, {"Tab", (int)Key::Tab},
        {"Backspace", (int)Key::Backspace},
        {"ArrowUp", (int)Key::ArrowUp}, {"ArrowDown", (int)Key::ArrowDown},
        {"ArrowLeft", (int)Key::ArrowLeft}, {"ArrowRight", (int)Key::ArrowRight},
        {"A", (int)Key::A}, {"B", (int)Key::B}, {"C", (int)Key::C}, {"D", (int)Key::D},
        {"E", (int)Key::E}, {"F", (int)Key::F}, {"G", (int)Key::G}, {"H", (int)Key::H},
        {"I", (int)Key::I}, {"J", (int)Key::J}, {"K", (int)Key::K}, {"L", (int)Key::L},
        {"M", (int)Key::M}, {"N", (int)Key::N}, {"O", (int)Key::O}, {"P", (int)Key::P},
        {"Q", (int)Key::Q}, {"R", (int)Key::R}, {"S", (int)Key::S}, {"T", (int)Key::T},
        {"U", (int)Key::U}, {"V", (int)Key::V}, {"W", (int)Key::W}, {"X", (int)Key::X},
        {"Y", (int)Key::Y}, {"Z", (int)Key::Z},
        {"ShiftLeft", (int)Key::ShiftLeft}, {"ShiftRight", (int)Key::ShiftRight},
        {"CtrlLeft", (int)Key::CtrlLeft}, {"CtrlRight", (int)Key::CtrlRight},
        {"AltLeft", (int)Key::AltLeft}, {"AltRight", (int)Key::AltRight},
    };
    auto it = map.find(name);
    return it != map.end() ? it->second : -1;
}

bool GlfwPlatform::load_input_script(const std::string& path) noexcept {
    auto& s = impl_->input_script;
    s.events.clear();
    s.cur = 0;
    s.frame_counter = 0;
    s.armed = false;
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "[INPUT_SCRIPT] cannot open %s\n", path.c_str());
        return false;
    }
    std::string line;
    int line_no = 0;
    while (std::getline(f, line)) {
        ++line_no;
        // strip comment
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        // tokenize
        std::vector<std::string> tok;
        size_t p = 0;
        while (p < line.size()) {
            while (p < line.size() && std::isspace((unsigned char)line[p])) ++p;
            if (p >= line.size()) break;
            size_t e = line.find_first_of(" \t\r\n", p);
            if (e == std::string::npos) e = line.size();
            tok.push_back(line.substr(p, e - p));
            p = e;
        }
        if (tok.empty()) continue;
        if (tok.size() < 3 || tok[0] != "frame") {
            std::fprintf(stderr, "[INPUT_SCRIPT] line %d: expected 'frame <N> keydown|keyup <KEY>'\n", line_no);
            return false;
        }
        std::uint64_t frame;
        try { frame = std::stoull(tok[1]); }
        catch (...) {
            std::fprintf(stderr, "[INPUT_SCRIPT] line %d: bad frame number '%s'\n", line_no, tok[1].c_str());
            return false;
        }
        bool down;
        if (tok[2] == "keydown") down = true;
        else if (tok[2] == "keyup") down = false;
        else {
            std::fprintf(stderr, "[INPUT_SCRIPT] line %d: expected keydown|keyup, got '%s'\n", line_no, tok[2].c_str());
            return false;
        }
        if (tok.size() < 4) {
            std::fprintf(stderr, "[INPUT_SCRIPT] line %d: missing key name\n", line_no);
            return false;
        }
        int idx = parse_key_name(tok[3]);
        if (idx < 0) {
            std::fprintf(stderr, "[INPUT_SCRIPT] line %d: unknown key '%s'\n", line_no, tok[3].c_str());
            return false;
        }
        s.events.push_back({frame, idx, down});
    }
    // sort by frame (stable for same-frame ordering)
    std::stable_sort(s.events.begin(), s.events.end(),
        [](const Impl::ScriptEvent& a, const Impl::ScriptEvent& b) {
            return a.frame < b.frame;
        });
    s.armed = !s.events.empty();
    std::fprintf(stderr, "[INPUT_SCRIPT] loaded %s: %zu events, armed=%d\n",
                path.c_str(), s.events.size(), (int)s.armed);
    return s.armed;
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

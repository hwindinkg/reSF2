// engine/platform/platform.cpp
//
// Implementation of the Platform interface + NullPlatform.

#include "platform.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace resf2::platform {

namespace fs = std::filesystem;
namespace chrono = std::chrono;

// ---------- now_ms (free function) ----------

std::uint64_t now_ms() noexcept {
    static const auto start = chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - start).count());
}

// ---------- create_platform ----------

std::unique_ptr<Platform> create_platform(std::string_view backend_name) {
    if (backend_name == "null" || backend_name == "headless") {
        return std::make_unique<NullPlatform>();
    }
    // TODO Stage 7.1.x: GLFW backend for Windows/Linux/macOS
    // For now, fall back to NullPlatform so tests can run.
    return std::make_unique<NullPlatform>();
}

// ---------- NullPlatform ----------

struct NullPlatform::Impl {
    std::string title;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool fullscreen = false;
};

NullPlatform::NullPlatform()
    : impl_(std::make_unique<Impl>())
    , start_time_ms_(now_ms()) {
}

NullPlatform::~NullPlatform() = default;

bool NullPlatform::init(const WindowConfig& config) {
    impl_->title = config.title;
    impl_->width = config.width;
    impl_->height = config.height;
    impl_->fullscreen = config.fullscreen;
    config_ = config;
    start_time_ms_ = now_ms();
    return true;
}

void NullPlatform::shutdown() noexcept {
    // Nothing to do
}

bool NullPlatform::poll_events() {
    // Clear per-frame input flags
    for (auto& p : input_.pointers) {
        p.just_pressed = false;
        p.just_released = false;
    }
    input_.keys_just_pressed.fill(false);
    input_.keys_just_released.fill(false);
    input_.mouse_delta_x = 0;
    input_.mouse_delta_y = 0;
    input_.mouse_wheel = 0.0f;
    return !quit_requested_;
}

bool NullPlatform::should_quit() const noexcept {
    return quit_requested_;
}

bool NullPlatform::is_paused() const noexcept {
    return paused_;
}

void NullPlatform::sleep_ms(std::uint32_t ms) noexcept {
    if (ms > 0) {
        std::this_thread::sleep_for(chrono::milliseconds(ms));
    } else {
        std::this_thread::yield();
    }
}

std::uint64_t NullPlatform::now_ms() const noexcept {
    return resf2::platform::now_ms() - start_time_ms_;
}

const InputState& NullPlatform::input() const noexcept {
    return input_;
}

std::int32_t NullPlatform::window_width() const noexcept {
    return impl_->width;
}

std::int32_t NullPlatform::window_height() const noexcept {
    return impl_->height;
}

void NullPlatform::resize_window(std::int32_t w, std::int32_t h) noexcept {
    impl_->width = w;
    impl_->height = h;
}

void NullPlatform::set_title(std::string_view title) noexcept {
    impl_->title = std::string(title);
}

void NullPlatform::set_fullscreen(bool fullscreen) noexcept {
    impl_->fullscreen = fullscreen;
}

bool NullPlatform::make_gl_current() noexcept {
    return true;  // no GL context in null platform
}

void NullPlatform::swap_buffers() noexcept {
    // No-op
}

std::vector<std::byte> NullPlatform::read_file(const std::string& path) const {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = static_cast<std::size_t>(f.tellg());
    if (size == 0) return {};
    f.seekg(0);
    std::vector<std::byte> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
    if (!f) return {};
    return buf;
}

bool NullPlatform::write_file(const std::string& path,
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

bool NullPlatform::file_exists(const std::string& path) const noexcept {
    std::error_code ec;
    return fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

std::string NullPlatform::save_dir() const {
    return fs::temp_directory_path().string();
}

void NullPlatform::set_pause_callback(PauseCallback cb) noexcept {
    pause_cb_ = std::move(cb);
}

void NullPlatform::set_resume_callback(PauseCallback cb) noexcept {
    resume_cb_ = std::move(cb);
}

// ---------- Test helpers ----------

void NullPlatform::inject_key_down(Key k) noexcept {
    auto idx = static_cast<std::size_t>(k);
    if (idx < kMaxKeys) {
        if (!input_.keys_down[idx]) {
            input_.keys_just_pressed[idx] = true;
        }
        input_.keys_down[idx] = true;
    }
}

void NullPlatform::inject_key_up(Key k) noexcept {
    auto idx = static_cast<std::size_t>(k);
    if (idx < kMaxKeys) {
        if (input_.keys_down[idx]) {
            input_.keys_just_released[idx] = true;
        }
        input_.keys_down[idx] = false;
    }
}

void NullPlatform::inject_pointer_down(std::int32_t id, float x, float y) noexcept {
    for (auto& p : input_.pointers) {
        if (p.id == -1) {
            p.id = id;
            p.x = x;
            p.y = y;
            p.pressed = true;
            p.just_pressed = true;
            return;
        }
    }
}

void NullPlatform::inject_pointer_up(std::int32_t id) noexcept {
    for (auto& p : input_.pointers) {
        if (p.id == id) {
            p.pressed = false;
            p.just_released = true;
            p.id = -1;
            return;
        }
    }
}

void NullPlatform::inject_pointer_move(std::int32_t id, float x, float y) noexcept {
    for (auto& p : input_.pointers) {
        if (p.id == id) {
            p.x = x;
            p.y = y;
            return;
        }
    }
}

}  // namespace resf2::platform

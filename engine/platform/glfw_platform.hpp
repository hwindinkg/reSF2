// engine/platform/glfw_platform.hpp
//
// GLFW-based platform backend for Windows / Linux / macOS.
//
// Stage 7.1.x implementation. Uses GLFW3 for window management + input,
// and system OpenGL (GL 2.1 compatibility) as a GLES2 stand-in.
//
// On Windows: links against glfw3.dll + opengl32.dll
// On Linux:   links against libglfw.so + libGL.so
// On macOS:   links against glfw3.framework + OpenGL.framework
//
// Build (standalone):
//   g++ -std=c++23 -Iengine -I/usr/include/GLFW -I/usr/include/GL \
//       engine/platform/glfw_platform.cpp -lglfw -lGL -o resf2_glfw

#pragma once

#include "platform.hpp"

namespace resf2::platform {

// GLFW-based platform backend.
// Creates a real OS window with an OpenGL context.
class GlfwPlatform final : public Platform {
public:
    GlfwPlatform();
    ~GlfwPlatform() override;

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace resf2::platform

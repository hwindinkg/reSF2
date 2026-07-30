// tests/headless_test_runner.cpp
//
// HeadlessTestRunner implementation — drives the real Game class headlessly.

#include "headless_test_runner.hpp"

#include <cstdio>

namespace resf2::test {

HeadlessTestRunner::HeadlessTestRunner(const HeadlessTestConfig& config)
    : config_(config) {
}

HeadlessTestRunner::~HeadlessTestRunner() = default;

bool HeadlessTestRunner::init() {
    // Create platform with deterministic clock
    platform_ = std::make_unique<TestPlatform>();
    platform_->set_fixed_time_ms(0);

    // Initialise the window config so scenes see valid dimensions
    platform::WindowConfig wc{};
    wc.title = "HeadlessTest";
    wc.width = config_.width;
    wc.height = config_.height;
    if (!platform_->init(wc)) {
        std::fprintf(stderr, "[HeadlessTestRunner] platform init failed\n");
        return false;
    }

    // Create software renderer
    renderer_ = std::make_unique<renderer::SoftwareRendererAdapter>();
    if (!renderer_->init(config_.width, config_.height)) {
        std::fprintf(stderr, "[HeadlessTestRunner] software renderer init failed\n");
        return false;
    }

    // Keep a non-owning pointer before moving into game
    renderer_ptr_ = renderer_.get();

    // Create game and inject the software renderer before on_init
    game_ = std::make_unique<game::Game>(config_.asset_root);
    game_->set_renderer(std::move(renderer_));

    // Set start scene if configured (before on_init, which checks start_scene_)
    if (!config_.start_scene.empty()) {
        game_->set_start_scene(config_.start_scene);
    }
    // Same ordering constraint: on_init loads the location, so the override has
    // to be in place first.
    if (!config_.start_location.empty()) {
        game_->set_start_location(config_.start_location);
    }

    // Initialise game (loads assets, registers scenes, etc.)
    game_->on_init(*platform_);

    return true;
}

void HeadlessTestRunner::run_frames(int count) {
    for (int i = 0; i < count; ++i) {
        (void)platform_->poll_events();                // clear per-frame input edges
        game_->on_update(*platform_, config_.fixed_dt_ms);
        game_->on_render(*platform_);
        platform_->advance_time_ms(config_.fixed_dt_ms); // advance deterministic clock
        frame_count_++;
    }
}

// ---------- Input injection ----------

void HeadlessTestRunner::inject_key_down(platform::Key key) {
    platform_->inject_key_down(key);
}

void HeadlessTestRunner::inject_key_up(platform::Key key) {
    platform_->inject_key_up(key);
}

void HeadlessTestRunner::tap_key(platform::Key key, int hold_frames) {
    // poll_events() clears the just-pressed edge, so the injection has to land
    // AFTER it and before on_update() -- which means driving the frame by hand
    // instead of going through run_frames().
    if (hold_frames < 1) hold_frames = 1;
    (void)platform_->poll_events();
    platform_->inject_key_down(key);
    game_->on_update(*platform_, config_.fixed_dt_ms);
    game_->on_render(*platform_);
    platform_->advance_time_ms(config_.fixed_dt_ms);
    frame_count_++;

    for (int i = 1; i < hold_frames; ++i) run_frames(1);

    (void)platform_->poll_events();
    platform_->inject_key_up(key);
    game_->on_update(*platform_, config_.fixed_dt_ms);
    game_->on_render(*platform_);
    platform_->advance_time_ms(config_.fixed_dt_ms);
    frame_count_++;
}

void HeadlessTestRunner::inject_pointer_down(float x, float y, std::int32_t id) {
    platform_->inject_pointer_down(id, x, y);
}

void HeadlessTestRunner::inject_pointer_up(std::int32_t id) {
    platform_->inject_pointer_up(id);
}

// ---------- State accessors ----------

float HeadlessTestRunner::player_health_frac() const {
    return game_->host_player_health_frac();
}

float HeadlessTestRunner::enemy_health_frac() const {
    return game_->host_enemy_health_frac();
}

std::string HeadlessTestRunner::round_outcome() const {
    return game_->host_round_outcome();
}

int HeadlessTestRunner::currency() const {
    return game_->host_get_currency();
}

bool HeadlessTestRunner::has_item(const std::string& id) const {
    return game_->host_has_item(id);
}

}  // namespace resf2::test

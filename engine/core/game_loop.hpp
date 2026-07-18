#pragma once

#include <cstdint>
#include <functional>

namespace resf2::core {

// Fixed-timestep game loop — matches JS `Qg` class.
// Runs update at fixed 60 Hz, render at variable rate with interpolation.

class GameLoop {
public:
    using UpdateFn = std::function<void(float dt)>;
    using RenderFn = std::function<void(float alpha)>;

    GameLoop(float fixed_dt = 1.0f / 60.0f)
        : fixed_dt_(fixed_dt) {}

    void set_update(UpdateFn u) { update_ = std::move(u); }
    void set_render(RenderFn r) { render_ = std::move(r); }

    void tick(float real_dt) {
        accumulator_ += real_dt;
        if (accumulator_ > max_frame_time_) accumulator_ = max_frame_time_;

        while (accumulator_ >= fixed_dt_) {
            if (update_) update_(fixed_dt_);
            accumulator_ -= fixed_dt_;
            frame_count_++;
        }

        float alpha = accumulator_ / fixed_dt_;
        if (render_) render_(alpha);
    }

    void reset() { accumulator_ = 0; frame_count_ = 0; }

    float fixed_dt() const { return fixed_dt_; }
    uint64_t frame_count() const { return frame_count_; }
    float fps() const { return fps_; }
    void set_fps(float v) { fps_ = v; }

private:
    float fixed_dt_;
    float accumulator_ = 0;
    float max_frame_time_ = 0.25f;
    uint64_t frame_count_ = 0;
    float fps_ = 0;
    UpdateFn update_;
    RenderFn render_;
};

} // namespace resf2::core

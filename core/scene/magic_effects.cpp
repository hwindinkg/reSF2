// Magic/effect containers — see magic_effects.hpp for the JS refs.

#include "scene/magic_effects.hpp"

#include <algorithm>
#include <cmath>

namespace sf2::scene {

namespace {

constexpr float kEndFadeTicks = 8.0f;  // one-shot fade-out tail (ticks)

}  // namespace

bool MagicEffects::load(const std::vector<MagicEffectDesc>& descs) {
    if (descs.empty()) return false;
    descs_ = descs;
    // Live instances hold desc INDICES — a reload invalidates them (same as
    // JS re-entering a fight: `fB()` drains every effect first).
    live_.clear();
    return true;
}

void MagicEffects::add_default_descs() {
    MagicEffectDesc flash;
    flash.name = "hit_flash";
    flash.frames = {"hit_flash_0", "hit_flash_1", "hit_flash_2", "hit_flash_3"};
    flash.loop = false;
    flash.ticks_per_frame = 2.0f;  // 8-tick flash (~0.13 s)
    flash.size = 30.0f;
    flash.color = 0xFFFFEE66u;  // warm spark tint (matches hit sparks)

    MagicEffectDesc intro;
    intro.name = "round_intro";
    intro.frames = {"round_intro_0", "round_intro_1", "round_intro_2",
                    "round_intro_3", "round_intro_4", "round_intro_5"};
    intro.loop = false;
    intro.ticks_per_frame = 5.0f;  // 30-tick ring (~0.5 s)
    intro.size = 120.0f;
    intro.color = 0xFFFFFFCCu;  // white-hot ring

    MagicEffectDesc trail;
    trail.name = "magic_trail";
    trail.frames = {"magic_trail_0", "magic_trail_1", "magic_trail_2"};
    trail.loop = true;  // JS `wcb` -> iterations -1
    trail.ticks_per_frame = 3.0f;
    trail.size = 18.0f;
    trail.color = 0x66CCFFu;  // cold magic tint
    trail.vy = -0.4f;         // rises while alive

    descs_.push_back(flash);
    descs_.push_back(intro);
    descs_.push_back(trail);
}

const MagicEffectDesc* MagicEffects::find(const std::string& name) const {
    for (const MagicEffectDesc& d : descs_) {
        if (d.name == name) return &d;
    }
    return nullptr;
}

bool MagicEffects::spawn(const std::string& name, float x, float y, int facing) {
    const MagicEffectDesc* d = find(name);
    if (d == nullptr) return false;
    const std::size_t idx = static_cast<std::size_t>(d - descs_.data());
    MagicInstance in;
    in.desc = idx;
    in.x = x;
    in.y = y;
    in.vx = d->vx;
    in.vy = d->vy;
    in.facing = facing >= 0 ? 1 : -1;
    in.frame_pos = d->reverse && !d->frames.empty()
                       ? static_cast<float>(d->frames.size()) - 1.0f
                       : 0.0f;
    in.age = 0.0f;
    live_.push_back(in);
    return true;
}

void MagicEffects::stop(const std::string& name) {
    const MagicEffectDesc* d = find(name);
    if (d == nullptr) return;
    const std::size_t idx = static_cast<std::size_t>(d - descs_.data());
    live_.erase(std::remove_if(live_.begin(), live_.end(),
                               [idx](const MagicInstance& in) { return in.desc == idx; }),
                live_.end());
}

void MagicEffects::stop_all() { live_.clear(); }

void MagicEffects::update(float timescale) {
    const float ts = timescale > 0.0f ? timescale : 1.0f;
    std::size_t w = 0;
    for (std::size_t i = 0; i < live_.size(); ++i) {
        MagicInstance& in = live_[i];
        const MagicEffectDesc& d = descs_[in.desc];
        const float rate = (d.ticks_per_frame > 0.0f ? 1.0f / d.ticks_per_frame : 1.0f) / ts;
        in.age += 1.0f;
        in.x += in.vx / ts;
        in.y += in.vy / ts;
        if (d.frames.empty()) {
            // Timeless tint pulse without frames: lives off the end-fade.
            if (in.age >= kEndFadeTicks * 2.0f) continue;  // dead — dropped
        } else if (d.loop) {
            const float n = static_cast<float>(d.frames.size());
            in.frame_pos += d.reverse ? -rate : rate;
            // Wrap into [0, n) (JS looped `animate`).
            in.frame_pos = in.frame_pos - std::floor(in.frame_pos / n) * n;
        } else {
            in.frame_pos += d.reverse ? -rate : rate;
            const float n = static_cast<float>(d.frames.size());
            const bool done =
                d.reverse ? (in.frame_pos < 0.0f) : (in.frame_pos >= n);
            if (done) continue;  // JS `LNa` — destroy finished one-shots
        }
        live_[w++] = in;
    }
    live_.resize(w);
}

float MagicEffects::life_for(const MagicInstance& in) const {
    const MagicEffectDesc& d = descs_[in.desc];
    if (d.loop || d.frames.empty()) return -1.0f;
    const float tpf = d.ticks_per_frame > 0.0f ? d.ticks_per_frame : 1.0f;
    return static_cast<float>(d.frames.size()) * tpf;
}

float MagicEffects::size_for(const MagicInstance& in) const {
    const MagicEffectDesc& d = descs_[in.desc];
    return d.size * d.scale;
}

std::uint32_t MagicEffects::color_for(const MagicInstance& in) const {
    return descs_[in.desc].color;
}

float MagicEffects::alpha_for(const MagicInstance& in) const {
    const float life = life_for(in);
    if (life < 0.0f) return 1.0f;  // loopers hold full alpha until stop()
    const float left = life - in.age;
    if (left <= 0.0f) return 0.0f;
    if (left >= kEndFadeTicks) return 1.0f;
    return left / kEndFadeTicks;
}

std::string MagicEffects::frame_for(const MagicInstance& in) const {
    const MagicEffectDesc& d = descs_[in.desc];
    if (d.frames.empty()) return "";
    int f = static_cast<int>(std::floor(in.frame_pos));
    if (f < 0) f = 0;
    if (f >= static_cast<int>(d.frames.size())) f = static_cast<int>(d.frames.size()) - 1;
    return d.frames[static_cast<std::size_t>(f)];
}

}  // namespace sf2::scene

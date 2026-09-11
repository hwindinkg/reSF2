// Magic/effect containers — see magic_effects.hpp for the JS refs.

#include "scene/magic_effects.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sf2::scene {

namespace {

constexpr float kEndFadeTicks = 8.0f;  // one-shot fade-out tail (ticks)

// Builds a `ni` frame run from the `fight/fx` atlas naming convention:
// `<prefix>/<prefix>_<i>` for i in [1, count] (e.g. "hit_blade/hit_blade_7").
std::vector<std::string> fx_frames(const char* prefix, int count) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 1; i <= count; ++i) {
        out.push_back(std::string(prefix) + "/" + prefix + "_" + std::to_string(i));
    }
    return out;
}

}  // namespace

bool MagicEffects::load(const std::vector<MagicEffectDesc>& descs) {
    if (descs.empty()) return false;
    descs_ = descs;
    // Live instances hold desc INDICES — a reload invalidates them (same as
    // JS re-entering a fight: `fB()` drains every effect first).
    background_.clear();
    foreground_.clear();
    return true;
}

void MagicEffects::add_default_descs() {
    // Real `fight/fx` atlas frame runs (asset 1306, manifest L2490) replace
    // the previous placeholder names; every one is an `ni`-playable run.
    MagicEffectDesc flash;
    flash.name = "hit_flash";
    flash.frames = fx_frames("hit_blade", 29);  // fx atlas: hit_blade_1..29
    flash.loop = false;                         // JS `wcb` false -> iterations 1
    flash.on_background = false;                // JS `Gfb` false -> air layer
    flash.ticks_per_frame = 1.0f;               // JS `NL` default = 1 (L729)
    flash.size = 40.0f;                         // fx frame ~34..84 px

    MagicEffectDesc intro;
    intro.name = "round_intro";
    intro.frames = fx_frames("block", 24);      // fx atlas: block_1..24
    intro.loop = false;
    intro.on_background = false;
    intro.ticks_per_frame = 1.0f;
    intro.size = 120.0f;

    MagicEffectDesc trail;
    trail.name = "magic_trail";
    trail.frames = fx_frames("effect_shield_hex_hit", 16);
    trail.loop = true;                          // JS `wcb` true -> iterations -1
    trail.on_background = false;
    trail.ticks_per_frame = 1.0f;
    trail.size = 18.0f;
    trail.color = 0x66CCFFu;  // cold magic tint (native extension)
    trail.vy = -0.4f;         // rises while alive (native drift extension)

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
    const int n = static_cast<int>(d->frames.size());
    // JS `cv.lwb` (L839): `a.lYa ? f.wrb() : f.RLa()` — backward starts at the
    // last frame (`Qu`), forward at the first (`mv` = 0).
    in.frame = (d->reverse && n > 0) ? n - 1 : 0;
    in.frame_step = d->reverse ? -1 : 1;
    in.iterations_left = d->loop ? -1 : 1;  // JS `wcb ? -1 : 1`
    in.playing = true;
    in.accum = 0.0f;
    in.age = 0.0f;
    // JS `tl.Nt` (L842): `a.Gfb ? this.Gq.Nt(a) : this.Hq.Nt(a)`.
    if (d->on_background) {
        background_.push_back(in);
    } else {
        foreground_.push_back(in);
    }
    return true;
}

std::vector<MagicInstance> MagicEffects::live() const {
    std::vector<MagicInstance> all;
    all.reserve(background_.size() + foreground_.size());
    all.insert(all.end(), background_.begin(), background_.end());
    all.insert(all.end(), foreground_.begin(), foreground_.end());
    return all;
}

void MagicEffects::stop(const std::string& name) {
    const MagicEffectDesc* d = find(name);
    if (d == nullptr) return;
    const std::size_t idx = static_cast<std::size_t>(d - descs_.data());
    const auto dead = [idx](const MagicInstance& in) { return in.desc == idx; };
    background_.erase(std::remove_if(background_.begin(), background_.end(), dead),
                      background_.end());
    foreground_.erase(std::remove_if(foreground_.begin(), foreground_.end(), dead),
                      foreground_.end());
}

void MagicEffects::stop_all() {
    background_.clear();
    foreground_.clear();
}

void MagicEffects::update(float timescale) {
    const float ts = timescale > 0.0f ? timescale : 1.0f;
    // JS `cv.WL` (L839): `animate.ia(L.K.sk.Bm * a)` with `a = 1/v.on()`.
    const float dt = (1.0f / 60.0f) / ts;
    // The same stepping runs for both containers (JS `Gq.WL()` + `Hq.WL()`,
    // `tl.WL` L837 calls each `Xm.WL`).
    const auto step = [this, ts, dt](std::vector<MagicInstance>& live) {
        std::size_t w = 0;
        for (std::size_t i = 0; i < live.size(); ++i) {
            MagicInstance& in = live[i];
            const MagicEffectDesc& d = descs_[in.desc];
            in.age += 1.0f;
            in.x += in.vx / ts;
            in.y += in.vy / ts;
            if (d.frames.empty()) {
                // Timeless tint pulse without frames: lives off the end-fade.
                if (in.age >= kEndFadeTicks * 2.0f) continue;  // dead — dropped
            } else if (!in.playing) {
                continue;  // JS `cv.WL`: `!d.animate.LJ` -> remove
            } else {
                const int n = static_cast<int>(d.frames.size());
                const float tpf = d.ticks_per_frame > 0.0f ? d.ticks_per_frame : 1.0f;
                const float mp = tpf / 60.0f;  // JS `ni.mP = NL/60` (s/frame)
                // JS `ni.ia`: for(Qe += a; Qe >= mP;) { JXa(); Qe -= mP }.
                in.accum += dt;
                while (in.playing && in.accum >= mp) {
                    in.accum -= mp;
                    in.frame += in.frame_step;
                    if (in.frame >= n || in.frame < 0) {
                        in.frame = 0;  // JS `JXa`: `this.hc = this.mv`
                        if (in.iterations_left > 0) {
                            --in.iterations_left;
                            if (in.iterations_left <= 0) in.playing = false;
                        }
                    }
                }
                if (!in.playing) continue;  // JS `LNa` — destroy finished
            }
            live[w++] = in;
        }
        live.resize(w);
    };
    step(background_);  // JS `Gq`
    step(foreground_);  // JS `Hq`
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

bool MagicEffects::background_for(const MagicInstance& in) const {
    // JS `tl.Nt` (L842): `a.Gfb ? Gq : Hq`.
    return descs_[in.desc].on_background;
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
    int f = in.frame;
    if (f < 0) f = 0;
    if (f >= static_cast<int>(d.frames.size())) {
        f = static_cast<int>(d.frames.size()) - 1;
    }
    return d.frames[static_cast<std::size_t>(f)];
}

}  // namespace sf2::scene

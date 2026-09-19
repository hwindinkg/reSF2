// Magic/effect containers — see magic_effects.hpp for the JS refs.

#include "scene/magic_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// The loaded magic atlas registry (one entry per `res/magic/mgc_*.json`):
// the Sequence stem -> frame names in atlas order. Set once by the app asset
// load (app.cpp); read by `build_magic_descs`.
static std::map<std::string, std::vector<std::string>>& magic_atlas_registry() {
    static std::map<std::string, std::vector<std::string>> s;
    return s;
}

void set_magic_atlas_frames(std::map<std::string, std::vector<std::string>> frames) {
    magic_atlas_registry() = std::move(frames);
}

const std::map<std::string, std::vector<std::string>>& magic_atlas_frames() {
    return magic_atlas_registry();
}

namespace {

// JS `Xy("x;y")` (the `<Attach OffsetVector>`): the two components as a
// shift; anything unparsable stays (0, 0).
void parse_offset_vector(const std::string& s, float& x, float& y) {
    const std::size_t sep = s.find(';');
    if (sep == std::string::npos) return;
    try {
        x = std::stof(s.substr(0, sep));
        y = std::stof(s.substr(sep + 1));
    } catch (const std::exception&) {
        x = 0.0f;
        y = 0.0f;
    }
}

}  // namespace

std::vector<MagicEffectDesc> build_magic_descs(
    const std::map<std::string, std::vector<std::string>>& atlas_frames,
    const std::map<std::string, MoveDef>& moves,
    const std::vector<GlobalTrigger>* global_triggers) {
    std::vector<MagicEffectDesc> descs;
    std::map<std::string, std::size_t> by_name;  // identity -> desc index
    std::size_t rows = 0, resolved = 0, dupes = 0, unresolved = 0;

    const auto add_row = [&](const MoveAction& a) {
        if (a.kind != "Effect") return;
        ++rows;
        if (a.name.empty() || a.sequence.empty()) return;
        const auto fit = atlas_frames.find(a.sequence);
        if (fit == atlas_frames.end() || fit->second.empty()) {
            ++unresolved;
            return;
        }
        ++resolved;
        // The JS descriptor lives on the ACTION instance (one `Yl` per
        // authored `<Effect>`); the native registry is keyed by the spawn
        // identity (`<Effect Name>`, the `cv.LNa`/`Gwb` match key). The first
        // authored row for a name wins; later duplicates are counted, never
        // silently merged.
        if (!by_name.emplace(a.name, descs.size()).second) {
            ++dupes;
            return;
        }
        MagicEffectDesc d;
        d.name = a.name;
        d.frames = fit->second;  // atlas order (`pi.VJa` -> `ve(...)`, L839)
        d.loop = a.effect_looped;              // `wcb`
        d.reverse = a.effect_backwards;        // `lYa`
        d.on_background = a.effect_on_background;  // `Gfb`
        d.ticks_per_frame = a.time_scale;      // `NL` (mP = NL/60 s)
        d.scale = 1.0f;
        d.scale_x = a.effect_scale_x;          // `scale.x`
        d.scale_y = a.effect_scale_y;          // `scale.y`
        d.start_rotation = a.start_rotation;   // `Vla`
        d.size = 1.0f;
        d.draw_source_size = true;             // sourceSize * scale (L839)
        d.pos_player = a.effect_pos_player;
        if (a.has_attach) {
            // `Vu` (L781-783): the RootPoint bone is the anchor and the
            // OffsetVector is the shift. The AttachPoint/`C7a` solver is a
            // bare `debugger` in the JS, so it is not reproduced.
            d.pos_object = "";
            d.pos_part = a.attach_root_point;
            parse_offset_vector(a.attach_offset, d.shift_x, d.shift_y);
        } else {
            d.pos_object = a.effect_pos_object;
            d.pos_part = a.effect_pos_part;
            d.shift_x = a.effect_shift_x;
            d.shift_y = a.effect_shift_y;
        }
        descs.push_back(std::move(d));
    };

    for (const auto& kv : moves) {
        for (const MoveAction& a : kv.second.actions) add_row(a);
    }
    if (global_triggers != nullptr) {
        for (const GlobalTrigger& t : *global_triggers) {
            for (const MoveAction& a : t.actions) add_row(a);
        }
    }
    std::fprintf(stdout,
                 "[fx] magic descriptors: %zu loaded (rows=%zu resolved=%zu "
                 "unresolved=%zu duplicate-names=%zu atlas-sets=%zu)\n",
                 descs.size(), rows, resolved, unresolved, dupes,
                 atlas_frames.size());
    std::fflush(stdout);
    return descs;
}

std::size_t MagicEffects::load_descriptors(
    const std::map<std::string, std::vector<std::string>>& atlas_frames,
    const std::map<std::string, MoveDef>& moves,
    const std::vector<GlobalTrigger>* global_triggers) {
    std::vector<MagicEffectDesc> descs =
        build_magic_descs(atlas_frames, moves, global_triggers);
    const std::size_t n = descs.size();
    if (n > 0) load(descs);
    return n;
}

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
    descs_.push_back(trail);
}

const MagicEffectDesc* MagicEffects::find(const std::string& name) const {
    for (const MagicEffectDesc& d : descs_) {
        if (d.name == name) return &d;
    }
    return nullptr;
}

bool MagicEffects::spawn(const std::string& name, float x, float y, int facing,
                         int owner, bool follow, float anchor_dx,
                         float anchor_dy) {
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
    in.owner = owner;    // JS `bv.model`
    in.follow = follow;  // JS `bv.effect.P1`
    // JS `cv.lwb` (L838): the spawn position IS `a.position.nt(model.Fc)`
    // (the named Part anchor + shift), so `(x,y)` already carries it; keep
    // the offset from the owner CoM for the follow update.
    in.anchor_dx = anchor_dx;
    in.anchor_dy = anchor_dy;
    // JS `tl.Nt(a)` (L842): `a.Gfb ? this.Gq.Nt(a) : this.Hq.Nt(a)` — the
    // `Yl` (Effect trigger action, L728: `Uh(a){a.gwb(this)}` -> fighter `Nt`
    // bus -> `tl.ZP` listener) routes by the descriptor's `OnBackground`
    // (`Gfb`, L729). OPEN: this snapshot ships no `magic/*.json` descriptor
    // registry, so an unknown `Name` is a no-op (the `Yl` caller itself is in
    // the moves.xml <Triggers>, which the native sim does not load).
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

void MagicEffects::stop(const std::string& name, int owner) {
    const MagicEffectDesc* d = find(name);
    if (d == nullptr) return;
    const std::size_t idx = static_cast<std::size_t>(d - descs_.data());
    // JS `tl.Ot` (L843) calls `Gq.Ot(a)` then `Hq.Ot(a)`; each `cv.Dwb` ->
    // `LNa` (L838) removes the FIRST entry matching `(model, name)` and
    // `break`s. `owner < 0` keeps the legacy remove-all-by-name.
    const auto purge = [idx, owner](std::vector<MagicInstance>& v) {
        if (owner < 0) {
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [idx](const MagicInstance& in) {
                                       return in.desc == idx;
                                   }),
                    v.end());
            return;
        }
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i].desc == idx && v[i].owner == owner) {
                v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
                return;  // JS `break` — first match only
            }
        }
    };
    purge(background_);
    purge(foreground_);
}

void MagicEffects::stop_follow(const std::string& name, int owner) {
    const MagicEffectDesc* d = find(name);
    if (d == nullptr) return;
    const std::size_t idx = static_cast<std::size_t>(d - descs_.data());
    // JS `cv.Hwb` -> `Gwb` (L838): latch `Yla` on the first `(model, name)`
    // match, `break`ing. `tl.Pt` (L843) runs it for both `Gq` and `Hq`.
    const auto latch = [idx, owner](std::vector<MagicInstance>& v) {
        for (MagicInstance& in : v) {
            if (in.desc == idx && in.owner == owner) {
                in.detached = true;  // JS `e.Yla = !0`
                return;
            }
        }
    };
    latch(background_);
    latch(foreground_);
}

void MagicEffects::stop_all() {
    background_.clear();
    foreground_.clear();
}

void MagicEffects::update(float timescale, const EffectAnchor* owners,
                          int owner_count) {
    const float ts = timescale > 0.0f ? timescale : 1.0f;
    // JS `cv.WL` (L839): `animate.ia(L.K.sk.Bm * a)` with `a = 1/v.on()`.
    const float dt = (1.0f / 60.0f) / ts;
    // The same stepping runs for both containers (JS `Gq.WL()` + `Hq.WL()`,
    // `tl.WL` L837 calls each `Xm.WL`).
    const auto step = [this, ts, dt, owners, owner_count](
                          std::vector<MagicInstance>& live) {
        std::size_t w = 0;
        for (std::size_t i = 0; i < live.size(); ++i) {
            MagicInstance& in = live[i];
            const MagicEffectDesc& d = descs_[in.desc];
            // JS `bv.update` (L834, reached from `cv.WL` L839 only while
            // `effect.P1 && !Yla`): reposition a live follow/attach effect
            // onto its model before the frame advance. StopFollowEffect
            // latches `Yla` (`detached`) and suppresses this.
            if (in.follow && !in.detached && owners != nullptr &&
                in.owner >= 0 && in.owner < owner_count) {
                // JS `bv.update` (L834): re-anchor on the model. The native
                // anchor keeps the spawn-time `<Position>` offset from the
                // owner CoM (`cv.lwb` L838), so a named-Part effect stays on
                // that part instead of snapping to the CoM.
                in.x = owners[in.owner].x + in.anchor_dx;
                in.y = owners[in.owner].y + in.anchor_dy;
                in.facing = owners[in.owner].facing >= 0 ? 1 : -1;
            }
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

float MagicEffects::scale_x_for(const MagicInstance& in) const {
    // JS `cv.lwb` (L838): `e.scale.x = c.Wl * a.scale.x` — the facing sign is
    // applied by the caller (kept unsigned here so `size_for` stays valid).
    return descs_[in.desc].scale_x;
}

float MagicEffects::scale_y_for(const MagicInstance& in) const {
    return descs_[in.desc].scale_y;  // JS `e.scale.y = a.scale.y`
}

float MagicEffects::rotation_for(const MagicInstance& in) const {
    return descs_[in.desc].start_rotation;  // JS `Vla` (`a.rotate()`)
}

bool MagicEffects::source_size_for(const MagicInstance& in) const {
    return descs_[in.desc].draw_source_size;
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

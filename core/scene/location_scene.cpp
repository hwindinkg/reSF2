// Location scene loader — params XML + atlas JSON + atlas texture -> sprite
// layer nodes, in the game's draw order (back to front).

#include "scene/location_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "atlas.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"
#include "xml_doc.hpp"

namespace sf2::scene {

namespace {

// JS `jh` constants (L1149-1151): degrees->radians (`d*.0174532925199432`),
// the per-update gravity term `this.bab*9.81*.02` (added per CALL, NOT scaled
// by dt — an oracle quirk), and the alpha fade-in step (`view.alpha += .1`
// per call). A private deterministic LCG seed matches the effects layer so
// the sim never perturbs the fight's shared RNG (JS uses wall-clock `oa.eT`).
constexpr float kParticleDegToRad = 0.0174532925199432f;
constexpr float kParticleGravityScale = 9.81f * 0.02f;
constexpr float kParticleAlphaStep = 0.1f;
constexpr float kParticleFixedStep = 1.0f / 60.0f;  // JS `Prewarm` tick (L1149)
constexpr std::uint32_t kParticleSeed = 0x853C49E7u;

// JS `QIa` L481: `if(!v.AEa)` — the global particle-effect kill switch. In
// the shipped build `v.AEa` is only ever assigned `!1` (the `L` ctor L63 and
// the `v` reset L2481), so the gate is always open (emitters are created).
// Kept as a named constant so the branch mirrors the oracle instead of
// silently dropping the gate.
constexpr bool kParticleEffectsEnabled = true;
// JS `Bf.UIa` L478: `if(v.Qcb)return null` — the global Sequention kill
// switch. `v.Qcb` is likewise only ever `!1` (L478/L2481), so sequentions are
// built; kept named for the same reason.
constexpr bool kSequentionEnabled = true;

// The arena height (`Root` `Height`) of the most recently loaded location —
// the native analogue of the JS `Bf` instance's `this.height` (`Bf.init` L474),
// which `Ut.m$a` L823 reads for the `ma.Sya` render zoom. `load` refreshes it;
// `framing_sya_impl` (fight_camera_sya.hpp) consumes it. 0 = no location yet.
float g_active_arena_height = 0.0f;

// A ClassName resolved against an atlas: the frame rect plus the pixel size
// of the atlas texture it lives in (for UV normalization).
struct FrameRef {
    sf2::data::atlas_frame frame;  // copied — atlases are parsed per-iteration
    int atlas_w = 0;
    int atlas_h = 0;
};

std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    return data;
}

std::string read_file_text(const std::string& path) {
    std::vector<std::uint8_t> bytes = read_file_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

// Game's color parse: "0xRRGGBB" -> (R,G,B,1) in 0..1. Na.cd, JS L1448.
void parse_color(const std::string& hex, float& r, float& g, float& b) {
    unsigned int v = 0;
    try {
        const std::string h = hex.size() > 2 && hex[1] == 'x' ? hex.substr(2) : hex;
        v = static_cast<unsigned int>(std::stoul(h, nullptr, 16));
    } catch (...) {
        v = 0;
    }
    r = static_cast<float>((v >> 16) & 0xFF) / 255.0f;
    g = static_cast<float>((v >> 8) & 0xFF) / 255.0f;
    b = static_cast<float>(v & 0xFF) / 255.0f;
}

// Particle color parse: `Na.Rv(K.parseInt(s))` (JS L1448 + `Zib` L1152).
// `s` is a packed ARGB literal (e.g. "0xff5C3D2A"): R = bits 16-23,
// G = 8-15, B = 0-7, A = 24-31, each /255. Returns false (leaving the
// arguments untouched) on a malformed value, so the caller keeps white.
bool parse_argb(const std::string& s, float& r, float& g, float& b, float& a) {
    if (s.empty()) {
        return false;
    }
    unsigned long v = 0;
    try {
        v = std::stoul(s, nullptr, 16);  // base 16 accepts the "0x" prefix
    } catch (...) {
        return false;
    }
    const std::uint32_t argb = static_cast<std::uint32_t>(v);
    r = static_cast<float>((argb >> 16) & 0xFFu) / 255.0f;
    g = static_cast<float>((argb >> 8) & 0xFFu) / 255.0f;
    b = static_cast<float>(argb & 0xFFu) / 255.0f;
    a = static_cast<float>((argb >> 24) & 0xFFu) / 255.0f;
    return true;
}


// JS `zh.Gb` + `zh.cmb` (L1145-1146): evaluate segment `seg` at local time
// `t`. Ease 0 = line, Ease != 0 = parabola; both hit `A` at t=0 and `B` at
// t=Period (cmb solves `b`/`c` so `e*(P+b)^2+c == B`). This is the D6
// `Point/@Ease` curve the previous port ignored (it always lerped).
float zh_value(const std::vector<Sprite::TransKey>& keys, std::size_t n,
               std::size_t seg, float t) {
    const float a = keys[seg].value;
    const float b = keys[(seg + 1) % n].value;
    const float p = keys[seg].period;
    const float e = keys[seg].ease;
    if (p <= 0.0f) {
        return a;  // JS Gw==0 -> cmb zeroes b/c; degenerate
    }
    if (e == 0.0f) {
        return a + (b - a) * (t / p);
    }
    const float bc = (b - a - e * p * p) / (2.0f * e * p);
    const float cc = a - e * bc * bc;
    return e * (t + bc) * (t + bc) + cc;
}

// JS `zh.update` (L1146): advance the clock by `dt` (seconds) and roll the
// segment index while the accumulated time exceeds the segment's Period (a
// strict `>`, wrapping `wp` to 0 at the end of the list).
void advance_timeline(EffectTimeline& tl, float dt) {
    if (tl.keys.empty()) {
        return;
    }
    tl.t += dt;
    const std::size_t n = tl.keys.size();
    while (true) {
        const float per = tl.keys[tl.seg].period;
        if (per <= 0.0f) {  // degenerate JS Gw==0; do not spin
            tl.t = 0.0f;
            break;
        }
        if (tl.t <= per) {
            break;
        }
        tl.t -= per;
        if (++tl.seg >= n) {
            tl.seg = 0;
        }
    }
}

// JS `irb`/`Mrb`/`Nrb(Offset)` (L480-481): seed the timeline clock. The JS
// call is `zh.update(Offset)`, i.e. the same roll applied to the initial time.
void seed_timeline(EffectTimeline& tl) {
    if (tl.keys.empty()) {
        return;
    }
    tl.t = tl.offset;
    advance_timeline(tl, 0.0f);
}

// Current value of a modifier timeline (JS `zh.Gb`, L1146); 0 when inactive.
float timeline_value(const EffectTimeline& tl) {
    if (tl.keys.empty()) {
        return 0.0f;
    }
    return zh_value(tl.keys, tl.keys.size(), tl.seg, tl.t);
}

// JS `Ie` reader (L1152-1153): "a,b" -> [a,b] range (min != max), a single
// number -> a fixed value (min == max). Returns false when the attr is absent.
bool parse_range_attr(const pugi::xml_node& node, const char* name, float& lo,
                      float& hi) {
    const char* raw = node.attribute(name).value();
    if (raw == nullptr || raw[0] == '\0') {
        lo = hi = 0.0f;
        return false;
    }
    const char* comma = std::strchr(raw, ',');
    if (comma == nullptr) {
        lo = hi = static_cast<float>(std::atof(raw));
    } else {
        lo = static_cast<float>(std::atof(std::string(raw, comma).c_str()));
        hi = static_cast<float>(std::atof(comma + 1));
    }
    return true;
}

// JS `xkb` (L1151): "x,y" -> H(x,y). `parseFloat` stops at the comma, so a
// plain prefix parse is exact; a missing second component yields 0.
void parse_emitter_attr(const pugi::xml_node& node, float& ex, float& ey) {
    ex = ey = 0.0f;
    const char* raw = node.attribute("Emitter").value();
    if (raw == nullptr || raw[0] == '\0') {
        return;
    }
    const char* comma = std::strchr(raw, ',');
    ex = static_cast<float>(std::atof(raw));
    if (comma != nullptr) {
        ey = static_cast<float>(std::atof(comma + 1));
    }
}

// JS `QIa` L481-482 + `jh` ctor L1147-1148: one particle emitter. `a.st()`
// is the node's FIRST child (`st(){return this.children[0]}`), i.e. the
// `<Params>` element carrying every emitter attribute. The SIM runs in
// `LocationScene::update`; the render pass is OPEN (see ParticleLayer).
// `ordinal` seeds the private deterministic LCG so emitters do not phase-lock
// (JS draws from the wall-clock `oa.eT`; the native is deliberately stable).
ParticleLayer parse_particle(const pugi::xml_node& node, std::uint32_t ordinal) {
    ParticleLayer p;
    p.rng = kParticleSeed ^ (ordinal * 2654435761u);
    const char* cls = node.attribute("ClassName").value();
    p.class_name = cls != nullptr ? cls : "";
    p.x = sf2::data::xml_attr_float(node, "X");
    p.y = sf2::data::xml_attr_float(node, "Y");
    pugi::xml_node prm = node.child("Params");
    if (prm == nullptr) {
        prm = node;  // defensive: attrs on the emitter node itself
    }
    const char* frame = prm.attribute("Frame").value();
    p.frame = frame != nullptr ? frame : "";
    parse_range_attr(prm, "Life", p.life_min, p.life_max);
    p.gravity = sf2::data::xml_attr_float(prm, "Gravity");
    parse_range_attr(prm, "ForceX", p.force_x_min, p.force_x_max);
    parse_range_attr(prm, "ForceY", p.force_y_min, p.force_y_max);
    p.rate = sf2::data::xml_attr_float(prm, "Rate");
    p.max_particles = sf2::data::xml_attr_int(prm, "MaxParticles", 500);
    parse_range_attr(prm, "AngVel", p.ang_vel_min, p.ang_vel_max);
    parse_range_attr(prm, "StartSize", p.start_size_min, p.start_size_max);
    parse_range_attr(prm, "StartRotation", p.start_rot_min, p.start_rot_max);
    p.start_speed = sf2::data::xml_attr_float(prm, "StartSpeed");
    parse_range_attr(prm, "VelocityX", p.vel_x_min, p.vel_x_max);
    parse_range_attr(prm, "VelocityY", p.vel_y_min, p.vel_y_max);
    parse_emitter_attr(prm, p.emitter_x, p.emitter_y);
    const char* prewarm = prm.attribute("Prewarm").value();
    p.prewarm = prewarm != nullptr && std::strcmp(prewarm, "1") == 0;
    const char* color = prm.attribute("Color").value();
    p.color = color != nullptr ? color : "";
    // JS `Zib` (L1151-1152): "a,b" -> [Na.Rv(a), Na.Rv(b)]; a single value ->
    // [Na.Rv(a)]. Only `rP[0]` reaches the fragment (the shader's mix
    // parameter `a_t` is the always-zero `view.KXa`), so keep the first.
    if (!p.color.empty()) {
        const std::size_t comma = p.color.find(',');
        const std::string first =
            comma == std::string::npos ? p.color : p.color.substr(0, comma);
        parse_argb(first, p.tint_r, p.tint_g, p.tint_b, p.tint_a);
    }
    return p;
}

// The JS `bkb` modifier block (L479-481): OscillationX/Y (`Mrb`/`bXa`,
// `Nrb`/`cXa`), ReappearX/Y (`gsb`/`hsb` over `Zo`), Speed (`zsb`),
// Rotation (`$sb` StartAngle + `Zsb` Offset + `GXa` keys). `Transparency`
// is handled by `parse_transparency` and nested `SimpleEffect` by the
// recursive `parse_simple_effect_node`; both are skipped here.
void parse_effect_modifier(const pugi::xml_node& child, SpriteAnim& anim) {
    const char* n = child.name();
    if (std::strcmp(n, "OscillationX") == 0 ||
        std::strcmp(n, "OscillationY") == 0) {
        EffectTimeline& tl =
            std::strcmp(n, "OscillationX") == 0 ? anim.osc_x : anim.osc_y;
        tl.offset = sf2::data::xml_attr_float(child, "Offset", 0.0f);
        for (const pugi::xml_node pt : child.children()) {
            if (std::strcmp(pt.name(), "Point") != 0) {
                continue;
            }
            Sprite::TransKey k;
            k.value = sf2::data::xml_attr_float(pt, "Value", 0.0f);
            k.period = sf2::data::xml_attr_float(pt, "Period", 1.0f);
            k.ease = sf2::data::xml_attr_float(pt, "Ease", 0.0f);
            tl.keys.push_back(k);
        }
        seed_timeline(tl);
    } else if (std::strcmp(n, "ReappearX") == 0 ||
               std::strcmp(n, "ReappearY") == 0) {
        const bool is_x = std::strcmp(n, "ReappearX") == 0;
        // JS `of(a)` (`new Ba(u.H(Min), u.H(Max))` -> .first/.second).
        const float mn = sf2::data::xml_attr_float(child, "Min", 0.0f);
        const float mx = sf2::data::xml_attr_float(child, "Max", 0.0f);
        if (is_x) {
            anim.reappear_x = true;
            anim.re_x_min = mn;
            anim.re_x_max = mx;
        } else {
            anim.reappear_y = true;
            anim.re_y_min = mn;
            anim.re_y_max = mx;
        }
    } else if (std::strcmp(n, "Speed") == 0) {
        // JS `zsb(X,Y)` L481 -> `Kta`/`Lta`, added per frame (L1139).
        anim.speed_x = sf2::data::xml_attr_float(child, "X", 0.0f);
        anim.speed_y = sf2::data::xml_attr_float(child, "Y", 0.0f);
    } else if (std::strcmp(n, "Rotation") == 0) {
        // JS `case "Rotation"` L480-481: `$sb(StartAngle)`,
        // `Zsb(Offset)` (seed `lO`), then a `GXa(Period,Value,Ease)` key per
        // Point. `ia` L1139 drives `Wg(lO.Gb()+kta)` every frame.
        anim.rot_start = sf2::data::xml_attr_float(child, "StartAngle", 0.0f);
        anim.rot.offset = sf2::data::xml_attr_float(child, "Offset", 0.0f);
        for (const pugi::xml_node pt : child.children()) {
            if (std::strcmp(pt.name(), "Point") != 0) {
                continue;
            }
            Sprite::TransKey k;
            k.value = sf2::data::xml_attr_float(pt, "Value", 0.0f);
            k.period = sf2::data::xml_attr_float(pt, "Period", 1.0f);
            k.ease = sf2::data::xml_attr_float(pt, "Ease", 0.0f);
            anim.rot.keys.push_back(k);
        }
        seed_timeline(anim.rot);
    }
}

// JS `ky(a,b)` (L17): `a -= floor(a/b)*b; return a<0?0:a>b?b:a`.
float wrap_js(float a, float b) {
    if (b <= 0.0f) {
        return a;
    }
    a -= std::floor(a / b) * b;
    return a < 0.0f ? 0.0f : (a > b ? b : a);
}

// JS `Qi.NWa`/`Dla` for a Sequention frame: swap the sprite's atlas rect,
// source/trim and texture alias to `f` (the `R.Cb` path, L1616). The scale is
// set once from frame 0 (JS `vqb` `Rh`/`mj`, L1137) and is NOT recomputed.
// `f.name` is a ClassName on one specific atlas page and `f.tex_w/h` are that
// page's pixel size, so the caller must have aliased EVERY page's frames
// (`atlas_pages()` -> `Renderer::texture_alias`); `draw_sprite` resolves the
// alias to the page's GL texture. Frames of one sequence may span pages
// (autumn + autumn-2; JS `ni.init` L1142 walks `b.nextPage`).
void apply_sequence_frame(Sprite& sprite, const SequenceFrame& f) {
    sprite.texture_name = f.name;
    sprite.frame_x = f.frame_x;
    sprite.frame_y = f.frame_y;
    sprite.frame_w = f.frame_w;
    sprite.frame_h = f.frame_h;
    sprite.tex_w = f.tex_w;
    sprite.tex_h = f.tex_h;
    sprite.rotated = f.rotated;
    if (f.trimmed) {
        sprite.trim_x = f.trim_x;
        sprite.trim_y = f.trim_y;
        sprite.source_w = f.source_w;
        sprite.source_h = f.source_h;
    }
}

// JS `ni.init` L1142 sort key: `K.parseInt(name.substr(len-2))` (the last two
// characters, parsed as an int; non-numeric -> 0).
long sequence_sort_key(const std::string& name) {
    if (name.size() < 2) {
        return 0;
    }
    const std::string tail = name.substr(name.size() - 2);
    char* end = nullptr;
    const long v = std::strtol(tail.c_str(), &end, 10);
    return end == tail.c_str() ? 0 : v;
}

// JS `ni.MT` (L1143): show frame `idx` (re-aliases the texture), if in range.
void seq_mt(SpriteAnim& a, std::size_t idx) {
    const SequenceAnim& s = a.seq;
    if (a.sprite == nullptr || s.frames.empty() || idx > s.qu || idx < s.mv) {
        return;
    }
    apply_sequence_frame(*a.sprite, s.frames[idx]);
    a.visible = true;  // JS `Y.R(!0)`
}

// JS `ni.JXa` (L1144): show the current frame, then step `hc` and wrap.
void seq_jxa(SpriteAnim& a) {
    SequenceAnim& s = a.seq;
    seq_mt(a, s.hc);
    const long next = static_cast<long>(s.hc) + s.k9;
    s.hc = (next > static_cast<long>(s.qu) || next < static_cast<long>(s.mv))
               ? s.mv
               : static_cast<std::size_t>(next);
}

// JS `ni.ia` (L1142): advance the frame clock by `sec` seconds.
void seq_ia(SpriteAnim& a, float sec) {
    SequenceAnim& s = a.seq;
    if (!s.lj || a.sprite == nullptr || s.frames.empty() || s.mP <= 0.0f) {
        return;
    }
    s.qe += sec;
    while (s.qe >= s.mP) {
        seq_jxa(a);
        s.qe -= s.mP;
    }
}

// JS `ni.nxa` (L1142): reset to the first frame and hide.
void seq_nxa(SpriteAnim& a) {
    SequenceAnim& s = a.seq;
    s.hc = s.mv;
    s.qe = 0.0f;
    seq_mt(a, s.hc);
    a.visible = false;  // JS `Y.R(!1)`
}

// JS `ni.rdb` (L1143): seek by the normalized Offset fraction (`k4a` first
// tick). `e5a() = mP*UX`.
void seq_rdb(SpriteAnim& a, float uoa) {
    SequenceAnim& s = a.seq;
    if (s.frames.empty()) {
        return;
    }
    s.qe = s.mP * static_cast<float>(s.ux) * uoa;
    long hc = static_cast<long>(static_cast<float>(s.ux) * uoa);  // `|0`
    hc += static_cast<long>(s.mv);
    if (hc >= static_cast<long>(s.qu)) {
        hc = static_cast<long>(s.qu);
    }
    if (hc < static_cast<long>(s.mv)) {
        hc = static_cast<long>(s.mv);
    }
    s.hc = static_cast<std::size_t>(hc);
    seq_mt(a, s.hc);
}

// JS `xl.jdb` (L1136): reset and show a child effect (`kdb` -> `jdb`).
void anim_jdb(SpriteAnim& a) {
    a.seq.bs = 0.0f;
    a.seq.gl = 0.0f;
    a.hidden = false;
    a.visible = true;
}

// JS `xl.Wwb` (L1136): hide this effect and reset its sequence.
void anim_wwb(SpriteAnim& a) {
    a.hidden = true;
    if (!a.seq.frames.empty()) {
        seq_nxa(a);
    }
    a.visible = false;
}

// JS `xl.vOa` (L1140): on a sequence end (`end`) or reappear (`reappear`),
// a nested effect hides itself and, if the matching launch flag is set,
// launches its children.
void anim_vOa(SpriteAnim& a, bool reappear, bool end) {
    if (a.has_parent) {
        anim_wwb(a);
    }
    if ((a.end_launch && end) || (a.reappear_launch && reappear)) {
        for (SpriteAnim* c : a.children) {
            if (c != nullptr) {
                anim_jdb(*c);
            }
        }
    }
}

// JS `xl.uma` (L1140): the outer sequence clock, in 1/60 s units. Returns the
// units to feed `ni.ia`; 0 while in the `Pause` tail. Ported verbatim,
// including the `abs(Gl)<1e-6` idle test and the end-crossing `vOa(false,true)`.
float seq_uma(SpriteAnim& a, float au) {
    SequenceAnim& s = a.seq;
    float x = wrap_js(au, s.uB + s.eG);
    if (std::fabs(s.gl) < 1.0e-6f) {
        if (s.bs < s.uB && s.bs + x >= s.uB) {
            anim_vOa(a, false, true);
        }
        s.bs += x;
        if (s.bs < s.uB) {
            return x;
        }
        s.gl += s.bs - s.uB;
        if (s.gl >= s.eG) {
            const float rest = s.gl - s.eG;
            s.bs = 0.0f;
            s.gl = 0.0f;
            return seq_uma(a, rest);
        }
        return s.uB - (s.bs - x + 1.0e-6f);
    }
    s.gl += x;
    if (s.gl < s.eG) {
        return 0.0f;
    }
    const float rest = s.gl - s.eG;
    s.bs = 0.0f;
    s.gl = 0.0f;
    return seq_uma(a, rest);
}

// JS `xl.ia` (L1138-1140): one SimpleEffect tick. Position/oscillation/
// rotation/reappear run for both Picture and Sequention; the Sequention
// clock additionally steps the `ni` frames. `dt` seconds; `au` = the number
// of 1/60 s units (`a` in the oracle, L1139 `var b=.016666*a`).
void update_effect(SpriteAnim& a, float dt) {
    if (a.sprite == nullptr) {
        return;
    }
    // JS `if(this.As==null||!this.OX)`: a nested child that has not been
    // launched (OX) runs no update at all.
    if (a.has_parent && a.hidden) {
        return;
    }
    const float au = dt * 60.0f;
    // JS `Nka(!(this.Gl>0&&this.Wqa))` (L1139): HidePaused hides during the
    // Pause tail.
    a.visible = !(a.seq.gl > 0.0f && a.hide_paused);
    // JS `k4a()` (L1138): first tick seeks a Sequention to `Uoa`.
    if (a.first) {
        if (!a.seq.frames.empty()) {
            seq_rdb(a, a.seq.uoa);
        }
        a.first = false;
    }
    // JS `uc==1 && (a=uma(a), a>0 ? pq.ia(.016666*a) : Gl>0 && pq.nxa())`.
    if (!a.seq.frames.empty()) {
        const float adv = seq_uma(a, au);
        if (adv > 0.0f) {
            seq_ia(a, adv / 60.0f);
        } else if (a.seq.gl > 0.0f) {
            seq_nxa(a);
        }
    }
    // JS `a=this.JM+=this.Kta; this.RW.update(b); a+=this.RW.Gb(); C(a)`.
    a.acc_x += a.speed_x * au;
    a.acc_y += a.speed_y * au;
    advance_timeline(a.osc_x, dt);
    advance_timeline(a.osc_y, dt);
    const float x = a.base_x + a.acc_x + timeline_value(a.osc_x);
    const float y = a.base_y + a.acc_y + timeline_value(a.osc_y);
    a.sprite->transform.x = x;
    a.sprite->transform.y = y;
    // JS `if(this.lO.active()){this.lO.update(b); Wg(this.lO.Gb()+this.kta)}`.
    if (!a.rot.keys.empty()) {
        advance_timeline(a.rot, dt);
        a.sprite->transform.rotation = timeline_value(a.rot) + a.rot_start;
    }
    // JS ReappearX/Y `Zwa` (L1140): wrap and `vOa(!0,!1)` if either fired.
    bool triggered = false;
    if (a.reappear_x && (x > a.re_x_max || x < a.re_x_min)) {
        a.acc_x = x > a.re_x_max ? a.re_x_min - a.re_x_max + x
                                 : a.re_x_max - a.re_x_min + x;
        triggered = true;
    }
    if (a.reappear_y && (y > a.re_y_max || y < a.re_y_min)) {
        a.acc_y = y > a.re_y_max ? a.re_y_min - a.re_y_max + y
                                 : a.re_y_max - a.re_y_min + y;
        triggered = true;
    }
    if (triggered) {
        anim_vOa(a, true, false);
    }
}

// The Transparency timeline on a Picture SimpleEffect (JS bkb L478-481:
// `irb(Offset)` + `KWa(Period,Value,Ease)` keys; xl.ia per-frame
// `Y.wa(EO.Gb()/100)`; zh.Gb initial with ar=0 is the first Point Value).
// The rest alpha is the FIRST Point Value/100 (dojo layer_4: 45 -> 0.45).
// Each key's Value is the alpha reached `Period` seconds after the previous
// key; the list loops (LocationScene::update evaluates it). `Offset` seeds
// the clock (JS `irb` L481), and `KWa` clamps every Value to [0,100]
// (L1138). Returns the rest alpha; `sprite.trans_keys` holds the timeline.
float parse_transparency(const pugi::xml_node& node, Sprite& sprite) {
    for (const pugi::xml_node child : node.children()) {
        if (std::strcmp(child.name(), "Transparency") != 0) {
            continue;
        }
        sprite.trans_t = sf2::data::xml_attr_float(child, "Offset", 0.0f);
        float rest = 1.0f;
        bool first = true;
        for (const pugi::xml_node pt : child.children()) {
            if (std::strcmp(pt.name(), "Point") != 0) {
                continue;
            }
            Sprite::TransKey k;
            const float v = sf2::data::xml_attr_float(pt, "Value", 100.0f);
            k.value = std::max(0.0f, std::min(100.0f, v));
            k.period = sf2::data::xml_attr_float(pt, "Period", 1.0f);
            k.ease = sf2::data::xml_attr_float(pt, "Ease", 0.0f);
            sprite.trans_keys.push_back(k);
            if (first) {
                rest = k.value / 100.0f;  // already clamped to [0,100]
                first = false;
            }
        }
        return rest;
    }
    return 1.0f;
}

// The game's `ujb` (L477): "pixel_1" -> solid fill, else atlas sprite.
// Returns nullptr for Image elements that are not sprite draws.
std::shared_ptr<Sprite> make_image(const pugi::xml_node& node,
                                   const std::unordered_map<std::string, FrameRef>& frames) {
    auto sprite = std::make_shared<Sprite>();
    const char* cls = node.attribute("ClassName").value();
    sprite->texture_name = cls != nullptr ? cls : "";

    const float x = sf2::data::xml_attr_float(node, "X");
    const float y = sf2::data::xml_attr_float(node, "Y");
    sprite->transform.set_pos(x, y);
    // JS `R3a` L486-487: `s.Wg(rot)` with `rot = Rotation` attr (D9). The
    // renderer rotates the quad about its center anchor. `xml_attr_float`
    // reads the leading number, matching the JS numeric reader `u.H`.
    sprite->transform.rotation = sf2::data::xml_attr_float(node, "Rotation", 0.0f);

    const float w = sf2::data::xml_attr_float(node, "Width");
    const float h = sf2::data::xml_attr_float(node, "Height");

    if (sprite->texture_name == "pixel_1") {
        // Solid color fill; the game tints with the Color attr (default 0).
        sprite->solid = true;
        sprite->frame_w = w;
        sprite->frame_h = h;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (node.attribute("Color")) {
            parse_color(node.attribute("Color").value(), r, g, b);
        }
        sprite->color_r = r;
        sprite->color_g = g;
        sprite->color_b = b;
        sprite->color_a = 1.0f;
        return sprite;
    }

    const auto it = frames.find(sprite->texture_name);
    if (it == frames.end()) {
        std::fprintf(stderr, "location_scene: no atlas frame for ClassName=\"%s\"\n",
                     sprite->texture_name.c_str());
        return nullptr;
    }
    const FrameRef& ref = it->second;
    const sf2::data::atlas_frame& fr = ref.frame;
    sprite->frame_x = static_cast<float>(fr.x);
    sprite->frame_y = static_cast<float>(fr.y);
    sprite->frame_w = static_cast<float>(fr.w);
    sprite->frame_h = static_cast<float>(fr.h);
    sprite->tex_w = static_cast<float>(ref.atlas_w);
    sprite->tex_h = static_cast<float>(ref.atlas_h);

    // The game scales the sprite to the XML Width/Height (Rh/mj, L486-487:
    // `b.Rh(f/g); b.mj(a/h)` where g/h = fa.x/fa.y = source size, JS L486-487).
    // For trimmed sprites the XML dimensions refer to the original source art
    // (JS pi.VJa L1703: Iq(filename, frame Ec, spriteSourceSize Ec, sourceSize
    // fc, trimmed) + Vs.Qq L1705: Pj(id, name, fa=sourceSize, frame, yx,
    // qj=wNa offset) — fa carries sourceSize, qj the trim offset), not the
    // packed frame — use source_w/h so the scale maps correctly.
    // (Using the packed frame size inflates the scale by source/frame ratio,
    // rendering the sprite at the wrong physical size. Verified vs
    // fx.925b16c7.json "block/block_1": source 1024x1024, frame 46x82 at
    // offset (454,475) — scale must map XML w/h over 1024, not 46/82.)
    {
        const float scale_w = (fr.trimmed && fr.source_w > 0)
                                  ? static_cast<float>(fr.source_w)
                                  : static_cast<float>(fr.w);
        const float scale_h = (fr.trimmed && fr.source_h > 0)
                                  ? static_cast<float>(fr.source_h)
                                  : static_cast<float>(fr.h);
        if (w > 0.0f && h > 0.0f && scale_w > 0.0f && scale_h > 0.0f) {
            sprite->transform.set_scale(w / scale_w, h / scale_h);
        }
    }
    // Store trim fields (JS Vs.Qq L1705 qj=wNa offset + fa=sourceSize) so the
    // renderer can apply the sub-pixel position compensation that aligns the
    // packed content to the source-frame center. Rotated packing flag
    // (JS Iq.dL L1703 -> Pj.dL L1705 -> le.frame.dL, bk L1765 / Cq L1561).
    sprite->rotated = fr.rotated;
    if (fr.trimmed) {
        sprite->trim_x   = static_cast<float>(fr.offset_x);
        sprite->trim_y   = static_cast<float>(fr.offset_y);
        sprite->source_w = static_cast<float>(fr.source_w);
        sprite->source_h = static_cast<float>(fr.source_h);
    }

    if (node.attribute("Color")) {
        float r = 1.0f, g = 1.0f, b = 1.0f;
        parse_color(node.attribute("Color").value(), r, g, b);
        sprite->color_r = r;
        sprite->color_g = g;
        sprite->color_b = b;
    }
    if (sf2::data::xml_attr_bool(node, "Flip", false)) {
        sprite->transform.scale_x = -sprite->transform.scale_x;
    }
    return sprite;
}

// JS `xl.ala`/`vqb` (L1137) builds a Sequention sprite: collect every atlas
// frame whose name starts with `ClassName` (`ni.init` L1142 `qd` prefix
// match), sort by the last two digits, then size the sprite from `Width`/
// `Height` over frame 0's source size. The frame list is stored in `seq`; the
// driver (`seq_mt`) swaps the frame + texture alias at run time. Returns
// nullptr when no frame matches (the JS would have no `Y`).
std::shared_ptr<Sprite> make_sequention(
    const pugi::xml_node& node,
    const std::unordered_map<std::string, FrameRef>& frames, SequenceAnim& seq) {
    const char* cls = node.attribute("ClassName").value();
    const std::string prefix = cls != nullptr ? cls : "";
    std::vector<std::pair<long, SequenceFrame>> ordered;
    for (const auto& kv : frames) {
        if (kv.first.rfind(prefix, 0) != 0) {  // JS qd: indexOf(name)==0
            continue;
        }
        const sf2::data::atlas_frame& fr = kv.second.frame;
        SequenceFrame f;
        f.name = kv.first;
        f.frame_x = static_cast<float>(fr.x);
        f.frame_y = static_cast<float>(fr.y);
        f.frame_w = static_cast<float>(fr.w);
        f.frame_h = static_cast<float>(fr.h);
        f.tex_w = static_cast<float>(kv.second.atlas_w);
        f.tex_h = static_cast<float>(kv.second.atlas_h);
        f.trimmed = fr.trimmed;
        f.trim_x = static_cast<float>(fr.offset_x);
        f.trim_y = static_cast<float>(fr.offset_y);
        f.source_w = static_cast<float>(fr.source_w);
        f.source_h = static_cast<float>(fr.source_h);
        f.rotated = fr.rotated;
        ordered.emplace_back(sequence_sort_key(kv.first), std::move(f));
    }
    if (ordered.empty()) {
        std::fprintf(stderr,
                     "location_scene: Sequention ClassName=\"%s\" has no frames\n",
                     prefix.c_str());
        return nullptr;
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const std::pair<long, SequenceFrame>& a,
                        const std::pair<long, SequenceFrame>& b) {
                         return a.first < b.first;
                     });
    for (auto& p : ordered) {
        seq.frames.push_back(std::move(p.second));
    }
    // JS `ni.init`/`grb(0,-1)`/`RLa`: full range, play forward.
    seq.mv = 0;
    seq.qu = seq.frames.size() - 1;
    seq.ux = seq.frames.size();
    seq.hc = 0;
    seq.k9 = 1;
    seq.lj = true;
    // JS `vqb`: `mP=Speed/60`, `uB=Speed*frames.length+1`, `eG=Pause`.
    const float speed = sf2::data::xml_attr_float(node, "Speed", 0.0f);
    seq.mP = speed / 60.0f;
    seq.uB = speed * static_cast<float>(seq.frames.size()) + 1.0f;
    seq.eG = sf2::data::xml_attr_float(node, "Pause", 0.0f);
    // JS `XXa(Offset)` (L1136): >0 wraps mod 100, <0 -> 100 - wrap(-offset).
    float off = sf2::data::xml_attr_float(node, "Offset", 0.0f);
    if (off > 0.0f) {
        off = wrap_js(off, 100.0f);
    } else if (off < 0.0f) {
        off = 100.0f - wrap_js(-off, 100.0f);
    }
    seq.uoa = off / 100.0f;
    // JS `vqb`: `Y = R.$(frames[0])`, `Rh(Width/fa.x)`, `mj(Height/fa.y)`,
    // `ik(.5,.5)`. The JS `mj` is negative but is cancelled by the following
    // `C5()` texture flipY (L1610) in the shader's flipY V-swap (L1769), so
    // the net orientation is upright — positive scale here.
    const SequenceFrame& f0 = seq.frames[0];
    auto sprite = std::make_shared<Sprite>();
    apply_sequence_frame(*sprite, f0);
    const float w = sf2::data::xml_attr_float(node, "Width");
    const float h = sf2::data::xml_attr_float(node, "Height");
    const float sw = (f0.trimmed && f0.source_w > 0.0f) ? f0.source_w : f0.frame_w;
    const float sh = (f0.trimmed && f0.source_h > 0.0f) ? f0.source_h : f0.frame_h;
    if (w > 0.0f && h > 0.0f && sw > 0.0f && sh > 0.0f) {
        sprite->transform.set_scale(w / sw, h / sh);
    }
    return sprite;
}

// Recursive `Bf.UIa` (L478-479) + `bkb` (L479-481): build one SimpleEffect
// (Picture or Sequention) and its nested `SimpleEffect` children. Every
// emitted sprite is appended to `out_sprites` in JS `pWa` order — nested
// children first (their `UIa` runs during the parent's `bkb`, before the
// parent's own `c.pWa`), then the parent — and every state object is pushed
// to `anims` in the same order (the layer `P7` list `Qi.ia` walks). Returns
// the new state, or nullptr when the element cannot be built.
SpriteAnim* parse_simple_effect_node(
    const pugi::xml_node& node,
    const std::unordered_map<std::string, FrameRef>& frames,
    std::vector<std::unique_ptr<SpriteAnim>>& anims,
    std::vector<std::shared_ptr<Sprite>>& out_sprites, SpriteAnim* parent) {
    const char* type_raw = node.attribute("Type").value();
    const std::string type = type_raw != nullptr ? type_raw : "";
    if (type != "Picture" && type != "Sequention") {
        // JS `UIa` L478 only builds Picture/Sequention; any other Type would
        // leave `Y` null and fault downstream. Skip safely.
        std::fprintf(stderr, "location_scene: SimpleEffect Type=\"%s\" ignored\n",
                     type.c_str());
        return nullptr;
    }
    if (type == "Sequention" && !kSequentionEnabled) {
        return nullptr;  // JS `if(v.Qcb)return null` (L478)
    }
    auto anim_u = std::make_unique<SpriteAnim>();
    SpriteAnim* anim = anim_u.get();
    anim->has_parent = parent != nullptr;
    anim->hidden = parent != nullptr;  // JS xl.nd: a.OX = !0
    anim->base_x = sf2::data::xml_attr_float(node, "X");
    anim->base_y = sf2::data::xml_attr_float(node, "Y");
    // JS `xl` ctor L1135-1136: child-launch flags off the SimpleEffect node.
    anim->end_launch = sf2::data::xml_attr_bool(node, "EndAnimChildLaunch", false);
    anim->reappear_launch =
        sf2::data::xml_attr_bool(node, "ReappearChildLaunch", false);
    anim->hide_paused = sf2::data::xml_attr_bool(node, "HidePaused", false);

    std::shared_ptr<Sprite> sprite = type == "Sequention"
                                         ? make_sequention(node, frames, anim->seq)
                                         : make_image(node, frames);
    if (sprite == nullptr) {
        return nullptr;
    }
    sprite->transform.set_pos(anim->base_x, anim->base_y);
    anim->sprite = sprite.get();
    // JS `bkb` `case "Transparency"` (L481): rest alpha = first key.
    sprite->color_a = parse_transparency(node, *sprite);

    // `bkb` switch (L479-481) in document order. A nested `SimpleEffect`
    // (`case "SimpleEffect": b.nd(UIa(f,c,d))`) is parsed here; its sprite
    // is appended before ours (nested `UIa` runs `c.pWa` first).
    for (const pugi::xml_node child : node.children()) {
        if (std::strcmp(child.name(), "SimpleEffect") == 0) {
            SpriteAnim* c =
                parse_simple_effect_node(child, frames, anims, out_sprites, anim);
            if (c != nullptr) {
                anim->children.push_back(c);
            }
        } else {
            parse_effect_modifier(child, *anim);
        }
    }
    // JS `a.has("Flip") && d.Y.Hr(!0)` for the Sequention path; the Picture
    // path already consumed `Flip` inside `make_image`.
    if (type == "Sequention" && sf2::data::xml_attr_bool(node, "Flip", false)) {
        sprite->transform.scale_x = -sprite->transform.scale_x;
    }
    out_sprites.push_back(sprite);       // JS `c.pWa(d)` — after the nested
    anims.push_back(std::move(anim_u));  // JS `P7.push(d)` — same order
    return anim;
}

} // namespace

void LocationScene::load(const std::string& params_xml, const std::string& atlas_json,
                         const std::string& atlas_tex_path, const std::string& res_root) {
    (void)atlas_tex_path;  // the atlas texture is uploaded by the caller probe
    std::vector<std::string> jsons = {atlas_json};
    load(params_xml, jsons, res_root);
}

void LocationScene::load(const std::string& params_xml, const std::vector<std::string>& atlas_jsons,
                         const std::string& res_root) {
    res_root_ = res_root;

    sf2::data::xml_doc doc;
    const std::vector<std::uint8_t> params_bytes = read_file_bytes(params_xml);
    doc.parse(params_bytes.data(), params_bytes.size());
    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::strcmp(root.name(), "Root") != 0) {
        throw std::runtime_error("location_scene: params root element missing");
    }
    arena_w_ = sf2::data::xml_attr_float(root, "Width", 0.0f);
    arena_h_ = sf2::data::xml_attr_float(root, "Height", 0.0f);
    // Publish `Lb.height` for the Sya render zoom (`Ut.m$a` L823): the fight
    // camera has no pointer to the scene, and its own `arena_h` defaults to
    // the dojo's 560 (see `active_arena_height`). Refreshed on every load, so
    // the fight location loaded by the fight screen wins over the hub's dojo.
    g_active_arena_height = arena_h_;
    arena_floor_ = sf2::data::xml_attr_float(root, "Floor", 0.0f);
    // JS `Bf.init` L474: `this.NU=u.H(a.attributes.get("Wall"))` and
    // `this.Tza=u.H(a.attributes.get("PositionY"))` — the fighter x-clamp
    // (L383) and camera-bound (L867) sources. Exposed so the caller stops
    // hard-coding dojo's 80 for every location.
    arena_wall_ = sf2::data::xml_attr_float(root, "Wall", 0.0f);
    arena_position_y_ = sf2::data::xml_attr_float(root, "PositionY", 0.0f);
    // The Root Color (the fighters' silhouette fill, JS `Na.cd`). Default
    // black when the attr is absent.
    if (root.attribute("Color")) {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        parse_color(root.attribute("Color").value(), r, g, b);
        root_color_ = (static_cast<std::uint32_t>(r * 255.0f) << 16) |
                      (static_cast<std::uint32_t>(g * 255.0f) << 8) |
                      static_cast<std::uint32_t>(b * 255.0f);
    }

    // Parse all atlases into one ClassName -> frame map (later packs win on
    // collision; each frame remembers its owning atlas pixel size).
    std::unordered_map<std::string, FrameRef> frames;
    atlas_names_.clear();
    atlas_pages_.clear();
    for (const std::string& json_path : atlas_jsons) {
        std::vector<std::uint8_t> json_bytes = read_file_bytes(json_path);
        const sf2::data::atlas a = sf2::data::atlas_parse(json_bytes.data(), json_bytes.size());
        atlas_names_.push_back(json_path);
        // Record this page's frame list so the caller can alias every page
        // (JS `Bf.init` L474 attaches `-2`, `-3`, … via `c.eXa`; `ni.init`
        // L1142 walks `b.nextPage`, so a Sequention spans all pages). The
        // merged `frames` map keeps the multi-page ClassName -> frame lookup.
        AtlasPage page;
        page.json_path = json_path;
        page.width = a.w;
        page.height = a.h;
        page.frame_names.reserve(a.frames.size());
        for (const auto& f : a.frames) {
            FrameRef ref;
            ref.frame = f;
            ref.atlas_w = a.w;
            ref.atlas_h = a.h;
            frames[f.name] = ref;
            page.frame_names.push_back(f.name);
        }
        atlas_pages_.push_back(std::move(page));
    }

    layers_.clear();
    anims_.clear();
    hidden_sprites_.clear();
    fighter_layer_ = npos;
    int layer_index = 0;
    std::uint32_t emitter_ordinal = 0;  // unique LCG seed per emitter
    for (const pugi::xml_node layer_node : root.children()) {
        if (std::strcmp(layer_node.name(), "Layer") != 0) {
            continue;
        }
        auto layer = std::make_shared<Layer>();
        layer->name = "layer_" + std::to_string(layer_index);
        layer->factor = sf2::data::xml_attr_float(layer_node, "Factor", 1.0f);
        layer->type = sf2::data::xml_attr_int(layer_node, "Type", 1);
        // The `Scaling` attr -> `ij` (JS zjb L475-476: `b.ij=c>0`). Dojo:
        // every visual layer carries Scaling="1"; Type=2 has none but takes
        // the setScale branch via lEa() (JS L488 branch).
        layer->scaling = sf2::data::xml_attr_int(layer_node, "Scaling", 0) > 0;
        // JS `Bf.init` L475: `c = 0; ... c += -3` per layer -> z = -3*i.
        layer->z = -3.0f * static_cast<float>(layer_index);
        // The ModelsViewer (Type=2) layer is where the ORIGINAL game draws
        // the fighters (JS_RENDER §2.5 / §7). Its index splits the draw
        // order: layers before it are the background, layers after it
        // (floor / dust / glow / pixel_1 vignette) draw ON TOP of the
        // fighters. Its `<ModelsViewer>` child carries the raw container
        // spawns (JS `Yia`/`B_`, Bf.zjb L476) — parsed here (D8).
        if (layer->type == 2 && fighter_layer_ == npos) {
            fighter_layer_ = static_cast<std::size_t>(layer_index);
        }
        {
            const pugi::xml_node mv = layer_node.child("ModelsViewer");
            if (mv != nullptr) {
                // [spawn-mapping] JS `Bf.zjb` L476 reads `Yia.x = PlayerPositionX`
                // and `B_.x = EnemyPositionX`, but `ca` L381 assigns them the
                // OTHER way round:
                //   `a=this.kc.position; b=this.location.Yia; a.x=b.x; ...`
                //   `a=this.Zb.position; b=this.location.B_;  a.x=b.x; ...`
                // `kc` is the ENEMY warrior (`o1a` L403 `this.yb=this.Gf(this.kc)`
                // -> trace id "Enemy") and `Zb` the PLAYER (`this.pb=this.Gf(this.Zb)`
                // -> "Me"), so the XML's `PlayerPosition*` is the ENEMY's spawn and
                // `EnemyPosition*` the PLAYER's. Confirmed against the oracle: the
                // moon `ModelsViewer` has PlayerPositionX=868/EnemyPositionX=1068
                // and the oracle trace has pb/Me root.x=1068, yb/Enemy root.x=868
                // (reference/traces/_residual_pins.md §2). The old port kept the
                // attribute names, which put the player on the wrong side — and so
                // inverted every fighter's facing/mirror state.
                player_spawn_x_ = sf2::data::xml_attr_float(mv, "EnemyPositionX", 0.0f);
                player_spawn_y_ = sf2::data::xml_attr_float(mv, "EnemyPositionY", 0.0f);
                enemy_spawn_x_ = sf2::data::xml_attr_float(mv, "PlayerPositionX", 0.0f);
                enemy_spawn_y_ = sf2::data::xml_attr_float(mv, "PlayerPositionY", 0.0f);
                has_spawns_ = true;
            }
        }
        ++layer_index;

        int sprite_index = 0;  // JS `Qi.QH` starts at 0 per layer (Dla L1599)
        for (const pugi::xml_node child : layer_node.children()) {
            // Sprites emitted by this XML child, in JS `pWa` order. A
            // SimpleEffect can emit several: a nested `SimpleEffect` is
            // appended before its parent (JS `bkb` L481).
            std::vector<std::shared_ptr<Sprite>> emitted;
            if (std::strcmp(child.name(), "Image") == 0) {
                // Raw R3a placement (JS L486-487): X/Y straight through.
                std::shared_ptr<Sprite> sprite = make_image(child, frames);
                if (sprite != nullptr) {
                    emitted.push_back(std::move(sprite));
                }
            } else if (std::strcmp(child.name(), "SimpleEffect") == 0) {
                // Picture / Sequention + the `bkb` modifier block (JS
                // `UIa` L478-479, `bkb` L479-481). The recursive parse builds
                // the effect, its nested SimpleEffect children and registers
                // every SpriteAnim in `anims_`; sprites come back in `pWa`
                // order.
                parse_simple_effect_node(child, frames, anims_, emitted, nullptr);
            } else if (kParticleEffectsEnabled &&
                       (std::strcmp(child.name(), "ParticleEffect") == 0 ||
                        std::strcmp(child.name(), "NewParticleEffect") == 0)) {
                // JS `Bf.zjb` L476-477: both tags route to `QIa` ->
                // `fXa(new jh)`. Parse the emitter, run the ctor `Prewarm`
                // loop, attach it, then the `Qi.fXa` warm-up; `update` runs
                // the `jh` sim and `particle_draws()` exposes the render data
                // (the effects-atlas draw pass itself is OPEN, D7; it is owned
                // by the renderer). D7 routing is N/A for locations: `QIa`
                // appends the `jh` node to the owning `Qi` layer (`Qi.fXa`
                // L488 `this.go.node.appendChild(a.node)`), so emitters are
                // layer children in document order — the `OnBackground`/`Gfb`
                // bg/fg split (`Yl.parse` L729 / `tl.Nt` L842) belongs to the
                // FIGHT's `Yl` effects only. No sprite is emitted, so
                // `sprite_index` (z) is unaffected.
                ParticleLayer emitter = parse_particle(child, emitter_ordinal++);
                // JS `jh` ctor L1149: `Prewarm=="1"` runs `update(1/60)`
                // until the accumulated time reaches `Life` max.
                if (emitter.prewarm) {
                    for (float t = 0.0f; t < emitter.life_max; t += kParticleFixedStep) {
                        step_particle_(emitter, kParticleFixedStep);
                    }
                }
                layer->particles.push_back(std::move(emitter));
                // JS `Qi.fXa` (L481): on attach the layer runs 150 warm-up
                // `update(1/60)` ticks, so every emitter is already mid-stream
                // on the first rendered frame (independent of `Prewarm`).
                for (int i = 0; i < 150; ++i) {
                    step_particle_(layer->particles.back(), kParticleFixedStep);
                }
                // JS `zjb` L477: `QIa` appends the emitter node to the layer
                // display list in document order; record it for interleaving.
                {
                    Layer::DrawItem item;
                    item.is_particle = true;
                    item.particle_index = layer->particles.size() - 1;
                    layer->draw_order.push_back(item);
                }
            }
            // ModelsViewer children are skipped (fighters are drawn by the
            // fight screen); ParticleEffect/NewParticleEffect are parsed above.
            for (auto& sprite : emitted) {
                // JS `Qi.NWa`/`Dla` L487/L1599: z = -0.01*spriteIndex.
                sprite->z = -0.01f * static_cast<float>(sprite_index);
                // JS `zjb` L477: `ujb`/`UIa` append the sprite in document
                // order. Weak ref so a later erase cannot dangle; index is
                // not used (sprites may be erased post-load).
                {
                    Layer::DrawItem item;
                    item.is_particle = false;
                    item.sprite = sprite;
                    layer->draw_order.push_back(item);
                }
                layer->sprites.push_back(sprite);
                ++sprite_index;
            }
        }
        // Only emitter-carrying layers need the ordered list; keep the common
        // (sprite-only) layer lean.
        if (layer->particles.empty()) {
            layer->draw_order.clear();
        }
        layers_.push_back(std::move(layer));
    }
}

float LocationScene::active_arena_height() { return g_active_arena_height; }

void LocationScene::default_camera(sf2::render::Camera& camera, float view_w,
                                   float view_h, float focus_x, float fighter_span) const {
    camera.view_w = view_w;
    camera.view_h = view_h;
    camera.arena_h = arena_h_;
    camera.arena_floor = arena_floor_;
    // Sya hub framing (ma.Sya, JS L1833). The hub renders through the global
    // camera whose horizontal is 0 (Sya `b.C(0)` + `tMa(f)`), but the hub runs
    // the LIVE fight (`Tf` `YL` L1971-1972): `Ut.Al` receives the live `Go.ma`
    // focus, so `Io = Lb.width/2 - focus` is per-frame (D3/W1). Callers that
    // do not pass a focus get the spawn midpoint (frame 0): dojo
    // (690+973)/2 = 831.5 -> Io = 980 - 831.5 = +148.5.
    const float focus =
        focus_x >= 0.0f
            ? focus_x
            : (has_spawns_ ? (player_spawn_x_ + enemy_spawn_x_) * 0.5f : arena_w_ * 0.5f);
    // Renderer Io = arena_center_x - center_x with center_x = 0 (JS camera
    // position 0) -> arena_center_x carries Io directly.
    camera.arena_center_x = arena_w_ * 0.5f - focus;
    camera.center_x = 0.0f;
    // The Sya zoom (JS L1833): f = viewH/(arenaH*Bj), aspect clamp 0.45..1, the
    // narrow-screen clamp, the width fit min(viewW/(span*f+100),1) with the
    // spawn fighter span |enemyX - playerX| (qh.ECa, JS L845-846), and the
    // min-zoom 0.6..1.3 (-> 1.3 at 16:9).
    const float aspect = view_h > 0.0f ? view_w / view_h : 16.0f / 9.0f;
    const float e = arena_h_ > 0.0f ? arena_h_ : 560.0f;
    const float span = fighter_span >= 0.0f
                           ? fighter_span
                           : (has_spawns_ ? std::fabs(enemy_spawn_x_ - player_spawn_x_) : 283.0f);
    // The hub-statics layer zoom Bj (JS `Ut.xCa` L831): min(nC/(span+300),1),
    // then max(Bj, NW) with NW = nC/width. nC = viewW/(viewH/arenaH).
    float layer_zoom = 1.0f;
    {
        const float ira = e > 0.0f ? view_h / e : 1.0f;
        const float n_c = ira > 0.0f ? view_w / ira : view_w;
        const float xca = n_c / (span + 300.0f) < 1.0f ? n_c / (span + 300.0f) : 1.0f;
        const float nw = arena_w_ > 0.0f ? n_c / arena_w_ : 1.0f;
        layer_zoom = xca > nw ? xca : nw;
        if (layer_zoom < 1.0f) {
            layer_zoom = 1.0f;
        }
    }
    camera.layer_zoom = layer_zoom;
    // JS ma.Sya L1833: `e = m$a() = Lb.height * Bj` is the zoom denominator
    // (W5/D11), not the raw arena height.
    float f = e > 0.0f ? view_h / (e * layer_zoom) : 1.0f;
    f *= (aspect < 0.45f ? 0.45f : aspect > 1.0f ? 1.0f : aspect);
    if (aspect < 0.8f) {
        f *= 0.8f + ((std::max(0.5f, std::min(0.8f, aspect)) - 0.5f) / 0.3f) * 0.2f;
    }
    f *= std::min(1.0f, view_w / (span * f + 100.0f));
    const float dmin =
        0.6f + ((std::max(0.5f, std::min(1.0f, aspect)) - 0.5f) / 0.5f) * 0.7f;
    if (f < dmin) {
        f = dmin;
    }
    camera.zoom = f;
    // JS ma.Sya L1833 leaves the camera y at 0 (only the aspect<1 portrait
    // `b.D(...)` moves it); the per-layer `Xrb(F9*(1-Bj))` translate is folded
    // in by render_layer, NOT the projection (D1/W1).
    camera.center_y = 0.0f;
    if (aspect < 1.0f) {
        camera.center_y += std::round((view_h - e * camera.zoom) / 2.0f) / camera.zoom * 0.5f;
    }
}

// JS `ma.Tya` (L1832): the destination-screen camera. See the header note.
void LocationScene::destination_camera(sf2::render::Camera& camera, float view_w,
                                       float view_h) {
    camera.view_w = view_w;
    camera.view_h = view_h;
    const float lc = view_h > 0.0f ? view_w / view_h : 16.0f / 9.0f;
    float d = view_h / 1152.0f;  // `Tya` L1832: d = N.height/1152
    float center_y = 0.0f;
    if (lc <= 1.0f) {
        // `Tya` L1832 `if(c<=1) a.bla(-100), d*=min(c,1)` — the bla moves the
        // model node (see destination_model_offset_x); only d is the camera.
        d *= std::min(lc, 1.0f);
    } else if (lc > 1.7f) {
        // `Tya` L1832 ultra-wide branch: `d += ((c<1.7?1.7:c>2.2?2.2:c)-1.7)
        // /.5*.2` and `position.y = ((...)-1.7)/.5*200`.
        const float t = (std::max(1.7f, std::min(2.2f, lc)) - 1.7f) / 0.5f;
        d += t * 0.2f;
        center_y = t * 200.0f;
    }
    camera.zoom = d;
    camera.center_x = 0.0f;
    camera.center_y = center_y;
    // `Tya` never runs `Ut.Al`, so there is no per-frame parallax: Io = 0.
    camera.arena_center_x = 0.0f;
    camera.layer_zoom = 1.0f;
}

float LocationScene::destination_model_offset_x(float view_w, float view_h) {
    const float lc = view_h > 0.0f ? view_w / view_h : 16.0f / 9.0f;
    if (lc <= 1.0f) {
        return -300.0f;  // `bla(-100)` -> -200 + (-100)
    }
    return -200.0f + (-300.0f * (std::min(2.0f, std::max(1.0f, lc)) - 1.0f));
}

void LocationScene::update(float dt) {
    // SimpleEffect Transparency loop (JS `bkb` L478-481 + `xl.ia` L1139 +
    // `zh` L1144-1146): each key's Value (percent) is reached `Period`
    // seconds after the previous key; the list loops. Segment i runs
    // value[i] -> value[(i+1) % n] over period[i] using the `Ease` curve
    // (`zh_value`: line for Ease 0, parabola otherwise; dojo layer_4 has
    // Ease +/-1), so the rest value (t=0) is value[0] = the parser's
    // initial color_a.
    for (const auto& layer : layers_) {
        for (const auto& sprite : layer->sprites) {
            const std::size_t nk = sprite->trans_keys.size();
            if (nk == 0) {
                continue;
            }
            float total = 0.0f;
            for (const auto& k : sprite->trans_keys) {
                total += std::max(0.0f, k.period);
            }
            if (total <= 0.0f) {
                continue;
            }
            sprite->trans_t += dt;
            while (sprite->trans_t >= total) {
                sprite->trans_t -= total;
            }
            std::size_t seg = 0;
            float acc = 0.0f;
            while (seg < nk) {
                const float per = std::max(0.0f, sprite->trans_keys[seg].period);
                if (per <= 0.0f || sprite->trans_t < acc + per) {
                    break;
                }
                acc += per;
                ++seg;
            }
            if (seg >= nk) {
                seg = nk - 1;
            }
            const float t = sprite->trans_t - acc;
            const float v = zh_value(sprite->trans_keys, nk, seg, t);
            sprite->color_a = std::max(0.0f, std::min(1.0f, v / 100.0f));
        }
    }

    // SimpleEffect tick (JS `xl.ia` L1138-1140, `Qi.ia` L488): oscillation /
    // reappear / speed / Rotation, the `Sequention` clock, and the nested
    // child launch/hide paths. `update_effect` ports `ia` per effect; the
    // layer `P7` order is the `anims_` order (children before parents).
    for (const auto& anim : anims_) {
        update_effect(*anim, dt);
    }
    // Rebuild the draw-time visibility set (JS `Y.R`/`$m`, L1136): a new frame
    // of a launched nested effect shows it, `Wwb`/`HidePaused` hide it.
    hidden_sprites_.clear();
    for (const auto& anim : anims_) {
        if (anim->sprite != nullptr && !anim->visible) {
            hidden_sprites_.insert(anim->sprite);
        }
    }

    // Particle emitters (JS `jh.update` L1149-1151): emission, force/gravity
    // integration, life/alpha, reaping. `dt` is seconds; the gravity and alpha
    // steps are per-CALL in the oracle and stay per-call here.
    for (const auto& layer : layers_) {
        for (ParticleLayer& emitter : layer->particles) {
            step_particle_(emitter, dt);
        }
    }
}

// JS `Ie.Gb` (L1152): `min==max ? min : oa.eT(min,max)`; `oa.eT(a,b)` (L)
// is `a + Math.random()*(b-a)`. The native uses a private deterministic LCG
// so a pose/asset dump is reproducible (JS uses the wall-clock RNG).
float LocationScene::rand_range_(ParticleLayer& emitter, float lo, float hi) {
    if (lo == hi) {
        return lo;
    }
    emitter.rng = 1664525u * emitter.rng + 1013904223u;
    const float u = static_cast<float>(emitter.rng >> 8) * (1.0f / 16777216.0f);
    return lo + u * (hi - lo);
}

// JS `jh.Nvb` (L1150-1151): spawn one particle at a random emitter offset.
// The RNG draw order mirrors the oracle exactly (emitter offset x/y -> force
// x/y -> life -> start rotation -> velocity x/y -> angular velocity -> size).
void LocationScene::spawn_particle_(ParticleLayer& emitter) {
    Particle p;
    // JS update L1149: `Nvb(oa.eT(-t_.x/2,t_.x/2), oa.eT(-t_.y/2,t_.y/2))`.
    p.x = rand_range_(emitter, -emitter.emitter_x * 0.5f, emitter.emitter_x * 0.5f);
    p.y = rand_range_(emitter, -emitter.emitter_y * 0.5f, emitter.emitter_y * 0.5f);
    // JS `new H(v4a.Gb(), w4a.Gb(), 0, 1)` — the spawn-time force vector.
    p.force_x = rand_range_(emitter, emitter.force_x_min, emitter.force_x_max);
    p.force_y = rand_range_(emitter, emitter.force_y_min, emitter.force_y_max);
    // JS `new Cv(a, b, hA.Gb(), c)` — life.
    p.life = rand_range_(emitter, emitter.life_min, emitter.life_max);
    // JS `c.view.rotation = Vla.Gb()*.0174532925199432`.
    p.rotation_rad =
        rand_range_(emitter, emitter.start_rot_min, emitter.start_rot_max) * kParticleDegToRad;
    p.alpha = 0.0f;  // JS `c.view.alpha=0`
    // JS `d=velocityX.Gb(); e=-velocityY.Gb()` — VelocityY is NEGATED (L1151).
    const float vel_x = rand_range_(emitter, emitter.vel_x_min, emitter.vel_x_max);
    const float vel_y = -rand_range_(emitter, emitter.vel_y_min, emitter.vel_y_max);
    if (emitter.start_speed > 0.0f && vel_x != 0.0f && vel_y != 0.0f) {
        // JS L1151: ub = normalize((vel_x, vel_y)) * StartSpeed ...
        const float len = std::sqrt(vel_x * vel_x + vel_y * vel_y);
        p.vx = vel_x / len * emitter.start_speed;
        p.vy = vel_y / len * emitter.start_speed;
    }
    p.vx += vel_x;  // ... then `ub += (d, e)`.
    p.vy += vel_y;
    // JS `c.wY = wY.Gb()*.0174532925199432` (radians/second).
    p.ang_vel_rad =
        rand_range_(emitter, emitter.ang_vel_min, emitter.ang_vel_max) * kParticleDegToRad;
    // JS `d = vwb.Gb()/BA.qc.re.dt[cOa].fa.x` (L1151): dividing by the frame
    // `sourceSize.x` needs the effects atlas `E.get(1304)` (OPEN), so the raw
    // StartSize is carried and the renderer scales at draw time.
    p.start_size = rand_range_(emitter, emitter.start_size_min, emitter.start_size_max);
    emitter.live.push_back(p);  // JS `this.BA.pl.push(c.view)`
}

// JS `jh.update` (L1149-1151) for one emitter.
void LocationScene::step_particle_(ParticleLayer& emitter, float dt) {
    emitter.spawn_acc += dt;
    if (emitter.rate > 0.0f) {
        // JS `b=1/this.v3a` then a SINGLE `if ($P>=b)` (not a while), so a
        // large dt still emits at most one particle per update.
        const float interval = 1.0f / emitter.rate;
        if (emitter.spawn_acc >= interval) {
            emitter.spawn_acc -= interval;
            if (static_cast<int>(emitter.live.size()) < emitter.max_particles) {
                spawn_particle_(emitter);
            }
        }
    }
    for (Particle& p : emitter.live) {
        // JS: `d.hA-=a`; alpha fades IN at +.1/call and OUT as `alpha = hA`
        // once the remaining life drops below 1 (L1149).
        p.life -= dt;
        if (p.life < 1.0f) {
            p.alpha = p.life;
        } else {
            p.alpha += kParticleAlphaStep;
            if (p.alpha > 1.0f) {
                p.alpha = 1.0f;
            }
        }
        p.vx += p.force_x * dt;
        p.vy += p.force_y * dt;
        // JS quirk (L1149): gravity is added per update, NOT scaled by dt.
        p.vy += emitter.gravity * kParticleGravityScale;
        p.rotation_rad += p.ang_vel_rad * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
    }
    // JS removal loop (L1150): drop every particle whose life ran out. The
    // oracle also `BA.submit()`s the GPU batch here — that flush is the effects
    // renderer's job (OPEN, L1150); the sim only fills `live`.
    emitter.live.erase(
        std::remove_if(emitter.live.begin(), emitter.live.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        emitter.live.end());
}

std::vector<ParticleDraw> LocationScene::particle_draws() const {
    std::vector<ParticleDraw> out;
    for (const auto& layer : layers_) {
        for (const ParticleLayer& emitter : layer->particles) {
            for (const Particle& p : emitter.live) {
                out.push_back(make_particle_draw_(*layer, emitter, p));
            }
        }
    }
    return out;
}

ParticleDraw LocationScene::make_particle_draw_(const Layer& layer,
                                                const ParticleLayer& emitter,
                                                const Particle& p) const {
    ParticleDraw d;
    // JS `jh` ctor (L1148): the emitter node `Hd` carries `translate=(X,Y)`;
    // the batch owner `Xb` child carries `scale=(1,-1)` (L1149). The rendered
    // centre is therefore `(X + ca.x, Y - ca.y)` and the rotation flips sign.
    d.x = emitter.x + p.x;
    d.y = emitter.y - p.y;
    d.rotation_rad = -p.rotation_rad;
    d.alpha = p.alpha;
    d.start_size = p.start_size;
    d.factor = layer.factor;
    d.frame = emitter.frame;
    // JS `BA.rP[0] = Zib(Color)[0]` (L1151-1152); white when absent.
    d.color_r = emitter.tint_r;
    d.color_g = emitter.tint_g;
    d.color_b = emitter.tint_b;
    d.color_a = emitter.tint_a;
    return d;
}

void LocationScene::render_layers(sf2::render::Renderer& renderer,
                                  const sf2::render::Camera& camera, std::size_t begin,
                                  std::size_t end) const {
    const std::size_t stop = std::min(end, layers_.size());
    for (std::size_t i = begin; i < stop; ++i) {
        render_layer(renderer, *layers_[i], camera);
    }
}

void LocationScene::render_layer(sf2::render::Renderer& renderer, const Layer& layer,
                                 const sf2::render::Camera& camera) const {
    // The per-layer node scale (JS L488 `b.lEa()||b.ij?b.setScale(Bj)`):
    // Bj comes from the camera (computed once per frame by the caller —
    // default_camera for the hub statics, fight framing for the fight).
    // At Bj=1 both branches are the identity; the Xrb-vs-setScale split
    // only moves pixels when Bj!=1 (push-in / lens-zoom moments).
    const bool scaled = (layer.type == 2) || layer.scaling;
    const float ls = scaled ? camera.layer_zoom : 1.0f;
    // JS `Ut.Al` L826-827: `L.lEa()||L.ij ? L.setScale(Bj) : L.Xrb(F9*(1-Bj))`.
    // A scaled layer gets `setScale(Bj)` and translate_y stays 0; a non-scaled
    // layer keeps scale 1 and carries translate_y = F9*(1-Bj) (D4/W2). At
    // Bj=1 both branches are identity (only push-in/lens-zoom moves pixels).
    const float layer_y = scaled ? 0.0f : camera.f9() * (1.0f - camera.layer_zoom);

    // Emitter-free layers keep the previous sprite-only path byte-for-byte.
    if (layer.particles.empty()) {
        for (const auto& sprite : layer.sprites) {
            // The game's ujb (JS L477) draws pixel_1 masks opaque - they are
            // part of the arena frame (the side/top/bottom blackout around the
            // 1960x560 arena), so they draw like every other sprite.
            // `Y.R(false)` hides an effect (JS `nxa`/`Wwb` L1136/L1142).
            if (hidden_sprites_.count(sprite.get()) != 0) {
                continue;
            }
            renderer.draw_sprite(*sprite, camera, layer.factor, ls, layer_y);
        }
        return;
    }

    // The location effects atlas `E.get(1304)` = `fight/particles.png`
    // (manifest L2490 token 1304; frames in token 1305). Lazy + cached.
    renderer.ensure_particle_atlas(res_root_);

    const auto draw_emitter = [&](const ParticleLayer& emitter) {
        for (const Particle& p : emitter.live) {
            renderer.draw_particle(make_particle_draw_(layer, emitter, p), camera, ls, layer_y);
        }
    };

    if (layer.draw_order.empty()) {
        // Defensive: a layer built without order info draws sprites then
        // emitters (document order was not recorded).
        for (const auto& sprite : layer.sprites) {
            if (hidden_sprites_.count(sprite.get()) != 0) {
                continue;
            }
            renderer.draw_sprite(*sprite, camera, layer.factor, ls, layer_y);
        }
        for (const ParticleLayer& emitter : layer.particles) {
            draw_emitter(emitter);
        }
        return;
    }

    // JS `nja` L29: draw self, then each display-list child in list order.
    // `zjb` L476-477 appends sprites and emitters in document order, and the
    // WebGL context disables depth (L64), so this is the exact composition.
    for (const Layer::DrawItem& item : layer.draw_order) {
        if (item.is_particle) {
            if (item.particle_index < layer.particles.size()) {
                draw_emitter(layer.particles[item.particle_index]);
            }
        } else if (const std::shared_ptr<Sprite> sprite = item.sprite.lock()) {
            if (hidden_sprites_.count(sprite.get()) != 0) {
                continue;
            }
            renderer.draw_sprite(*sprite, camera, layer.factor, ls, layer_y);
        }
    }
}

} // namespace sf2::scene

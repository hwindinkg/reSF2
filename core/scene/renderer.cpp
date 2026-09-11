// Scene renderer implementation: context lifecycle, texture cache, and the
// sprite draw path (world -> camera -> screen quad -> batch).

#include "scene/renderer.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "atlas.hpp"
#include "render/gl.hpp"
#include "render/texture_gpu.hpp"
#include "scene/location_scene.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"

namespace sf2::render {

namespace {

// Builds the 6 vertices of a quad in screen space for a sprite.
// `factor` is the layer parallax factor: the camera x offset is scaled by
// it (game's `Wrb(Io * bp)`), so background layers shift less than the
// camera when it pans. At fight start (camera centered on the arena,
// Io == 0) the parallax is a no-op.
void sprite_to_quad(const sf2::scene::Sprite& s, const Camera& camera, float factor,
                     float layer_scale, float layer_y, SpriteQuad& quad) {
    const sf2::scene::Transform& t = s.transform;

    const float half_w = t.scale_x * s.frame_w / 2.0f;
    const float half_h = t.scale_y * s.frame_h / 2.0f;

    // Anchor offset in local units (0..1 -> -half..+half).
    const float ax = (t.anchor_x - 0.5f) * 2.0f * half_w;
    const float ay = (t.anchor_y - 0.5f) * 2.0f * half_h;

    // Local corners (in world units, centered on anchor):
    //   0=(-w,-h) 1=(+w,-h) 2=(-w,+h) 3=(+w,+h)
    float lx[4] = {-half_w - ax, half_w - ax, -half_w - ax, half_w - ax};
    float ly[4] = {-half_h - ay, -half_h - ay, half_h - ay, half_h - ay};
    // JS `R3a` L486-487: `s.Wg(rot); s.ik(.5,.5)` — sprite rotation about
    // its center anchor. `Rotation` is the XML attribute in degrees (the raw
    // number `u.H(attr)`; the camera's own rotation converts with *pi/180 at
    // L79, so degrees is the convention). 0 = no-op; dojo has no Rotation,
    // locations like autumn carry one (parsed as its leading number by the
    // same numeric reader the native uses for every XML float).
    if (t.rotation != 0.0f) {
        const float th = t.rotation * 3.14159265358979323846f / 180.0f;
        const float ct = std::cos(th), st = std::sin(th);
        for (int c = 0; c < 4; ++c) {
            const float px = lx[c] * ct - ly[c] * st;
            const float py = lx[c] * st + ly[c] * ct;
            lx[c] = px;
            ly[c] = py;
        }
    }
    // Normalized UV corners [0,1].
    // Y-origin: the atlas frame rect is top-left origin and the upload is
    // top-row-first with an identity sampler (stb top row -> v=0 row), so
    // v=(y)/tex_h samples the intended file row — the same net mapping as
    // JS (dr Rj L1763 `v_tcoord=(x, tex_h-y)/tex_h` over a FLIP_Y upload,
    // pixelStorei(37440,1) L1821). No flip here.
    // Rotated packing (JS le.frame.dL: bk L1765 transposed draw + Cq L1561
    // ctx.rotate(-PI/2)): stored texels are the source rotated 90deg CW, so
    // quad TL/TR/BL/BR sample stored TR/BR/TL/BL (Cq net screen(dx,dy) =
    // (dy,h-dx): stored TR -> screen TL). 0/12823 shipped res frames are
    // rotated, so this branch is neutral today and un-verified on live art.
    const float u_norm = s.tex_w > 0.0f ? 1.0f / s.tex_w : 1.0f;
    const float v_norm = s.tex_h > 0.0f ? 1.0f / s.tex_h : 1.0f;
    const float fx0 = s.frame_x, fy0 = s.frame_y;
    const float fx1 = s.frame_x + s.frame_w, fy1 = s.frame_y + s.frame_h;
    const float u[4] = {!s.rotated ? fx0 * u_norm : fx1 * u_norm,
                        !s.rotated ? fx1 * u_norm : fx1 * u_norm,
                        !s.rotated ? fx0 * u_norm : fx0 * u_norm,
                        !s.rotated ? fx1 * u_norm : fx0 * u_norm};
    const float v[4] = {!s.rotated ? fy0 * v_norm : fy0 * v_norm,
                        !s.rotated ? fy0 * v_norm : fy1 * v_norm,
                        !s.rotated ? fy1 * v_norm : fy0 * v_norm,
                        !s.rotated ? fy1 * v_norm : fy1 * v_norm};

    // Trim compensation (JS pi.VJa L1703 Iq wNa/fa + Vs.Qq L1705 Pj qj/fa +
    // R.Cb L1615 Em=qj/ba(frame) + R.Th L1615 translate b-f+d): the packed
    // frame is the trimmed content at spriteSourceSize (trim_x, trim_y)
    // inside the full sourceSize (source_w, source_h); the sprite's XML
    // position is the center of the FULL source frame. JS lands the packed
    // content's center at x-(source_w/2-trim-frame_w/2) (Th: Tx=x-BS+off,
    // BS=fa/2 at sx=1), so the quad shifts by the NEGATION of
    // (source-center minus content-center):
    //   (trim_x + frame_w/2 - source_w/2) * scale_x (and likewise Y).
    // (The previous sign mirrored trimmed content across the source center:
    // e.g. fx block_1 rendered at x+35 instead of x-35.) Zero when not
    // trimmed (source_w == 0 sentinel). Magnitudes verified vs 5 samples:
    // fx.925b16c7.json block_1 |35|,|4| + block_3 |26|,|4|, dojo floor_1
    // |2|y, left_wall |5|x, right_wall |3|x.
    const float trim_adj_x = s.source_w > 0.0f
        ? (s.trim_x + s.frame_w / 2.0f - s.source_w / 2.0f) * t.scale_x
        : 0.0f;
    const float trim_adj_y = s.source_h > 0.0f
        ? (s.trim_y + s.frame_h / 2.0f - s.source_h / 2.0f) * t.scale_y
        : 0.0f;

    // The layer-node scale (JS L488): scaled layers (lEa||ij) carry
    // go.scale=Bj, so every child world coord is pre-multiplied by Bj
    // BEFORE the shared camera projection runs. Corner pairing preserved:
    // 0=(-w,-h) 1=(+w,-h) 2=(-w,+h) 3=(+w,+h).
    const float sx0 = (t.x + trim_adj_x) * layer_scale;
    const float sy0 = (t.y + trim_adj_y) * layer_scale + layer_y;
    const float lx0 = lx[0] * layer_scale, lx1 = lx[1] * layer_scale;
    const float ly0 = ly[0] * layer_scale, ly1 = ly[2] * layer_scale;
    const float sx[4] = {
        camera.world_to_screen_x(sx0 + lx0, factor), camera.world_to_screen_x(sx0 + lx1, factor),
        camera.world_to_screen_x(sx0 + lx0, factor), camera.world_to_screen_x(sx0 + lx1, factor)};
    const float sy[4] = {
        camera.world_to_screen_y(sy0 + ly0), camera.world_to_screen_y(sy0 + ly0),
        camera.world_to_screen_y(sy0 + ly1), camera.world_to_screen_y(sy0 + ly1)};

    // Two triangles: (0,1,2) (2,1,3).
    quad.v[0] = {sx[0], sy[0], u[0], v[0], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[1] = {sx[1], sy[1], u[1], v[1], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[2] = {sx[2], sy[2], u[2], v[2], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[3] = {sx[2], sy[2], u[2], v[2], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[4] = {sx[1], sy[1], u[1], v[1], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[5] = {sx[3], sy[3], u[3], v[3], s.color_r, s.color_g, s.color_b, s.color_a};
}

} // namespace

bool Renderer::init(int view_w, int view_h, bool hidden, GLFWwindow** out_window) {
    if (!glfw_context_create(view_w, view_h, hidden, &window_)) {
        return false;
    }
    if (out_window != nullptr) {
        *out_window = window_;
    }
    if (!batch_.init(view_w, view_h)) {
        std::fprintf(stderr, "renderer: sprite batch init failed\n");
        shutdown();
        return false;
    }
    return true;
}

void Renderer::shutdown() {
    batch_.shutdown();
    for (auto& kv : textures_) {
        if (kv.second != 0) {
            delete_texture(kv.second);
        }
    }
    textures_.clear();
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

GLuint Renderer::texture_for(const std::string& name, const sf2::data::Texture& tex) {
    const auto it = textures_.find(name);
    if (it != textures_.end()) {
        return it->second;
    }
    const GLuint id = upload_texture_rgba(tex);
    textures_[name] = id;
    return id;
}

void Renderer::texture_alias(const std::string& name, GLuint texture) {
    if (texture != 0) {
        textures_[name] = texture;
    }
}

GLuint Renderer::texture_lookup(const std::string& name) const {
    const auto it = textures_.find(name);
    return it != textures_.end() ? it->second : 0;
}

void Renderer::draw_sprite(const sf2::scene::Sprite& sprite, const Camera& camera,
                            float factor, float layer_scale, float layer_y) {
    SpriteQuad quad;
    sprite_to_quad(sprite, camera, factor, layer_scale, layer_y, quad);
    GLuint texture = 0;
    if (!sprite.solid) {
        texture = textures_.count(sprite.texture_name) ? textures_[sprite.texture_name] : 0;
    }
    batch_.add_quad(quad, texture);
}

bool Renderer::ensure_particle_atlas(const std::string& res_root) {
    if (particle_atlas_attempted_) {
        return particle_texture_ != 0;
    }
    particle_atlas_attempted_ = true;
    if (res_root.empty()) {
        return false;
    }
    namespace fs = std::filesystem;
    const std::string dir = res_root + "/fight";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return false;
    }
    // The effects atlas ships as `fight/particles.<hash>.png` +
    // `fight/particles.<hash>.json` (manifest L2490 tokens 1304/1305). Pick
    // the first JSON and the first decodable image sibling.
    std::string json_path;
    std::string tex_path;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("particles.", 0) != 0) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext == ".json") {
            if (json_path.empty()) {
                json_path = entry.path().string();
            }
        } else if (tex_path.empty() &&
                   (ext == ".png" || ext == ".webp" || ext == ".ktx" || ext == ".dds")) {
            tex_path = entry.path().string();
        }
    }
    if (json_path.empty() || tex_path.empty()) {
        std::fprintf(stderr, "renderer: fight/particles atlas not found under %s\n",
                     dir.c_str());
        return false;
    }
    sf2::data::Texture tex;
    if (!sf2::data::decode_texture(tex_path, tex) || tex.w <= 0 || tex.h <= 0) {
        std::fprintf(stderr, "renderer: fight/particles decode failed: %s\n",
                     tex_path.c_str());
        return false;
    }
    const GLuint gl = texture_for("fight_particles_atlas", tex);
    if (gl == 0) {
        return false;
    }
    std::ifstream in(json_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "renderer: fight/particles json unreadable: %s\n",
                     json_path.c_str());
        return false;
    }
    const std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    sf2::data::atlas parsed;
    try {
        parsed = sf2::data::atlas_parse(jb.data(), jb.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "renderer: fight/particles parse failed: %s\n", e.what());
        return false;
    }
    for (const auto& fr : parsed.frames) {
        ParticleFrame pf;
        pf.fx = static_cast<float>(fr.x);
        pf.fy = static_cast<float>(fr.y);
        pf.fw = static_cast<float>(fr.w);
        pf.fh = static_cast<float>(fr.h);
        pf.src_w = static_cast<float>(fr.source_w);
        pf.src_h = static_cast<float>(fr.source_h);
        pf.tex_w = static_cast<float>(parsed.w);
        pf.tex_h = static_cast<float>(parsed.h);
        pf.trimmed = fr.trimmed;
        particle_frames_[fr.name] = pf;
    }
    particle_texture_ = gl;
    std::fprintf(stdout, "[renderer] effects atlas fight/particles: %dx%d %zu frames (tex %u)\n",
                 parsed.w, parsed.h, parsed.frames.size(), gl);
    return true;
}

void Renderer::draw_particle(const sf2::scene::ParticleDraw& draw, const Camera& camera,
                             float layer_scale, float layer_y) {
    if (particle_texture_ == 0) {
        return;
    }
    const auto it = particle_frames_.find(draw.frame);
    if (it == particle_frames_.end()) {
        return;
    }
    const ParticleFrame& f = it->second;
    // JS L1151: `jka = kka = StartSize / dt[cOa].fa.x` (sourceSize.x).
    const float denom = f.src_w > 0.0f ? f.src_w : (f.fw > 0.0f ? f.fw : 1.0f);
    const float scale = draw.start_size / denom;
    // JS L1554: a trimmed frame draws at its PACKED size (`Nc`), an untrimmed
    // one at `sourceSize` (`fa`). The particles atlas ships no trimmed frames,
    // so scale maps the full source box.
    const float quad_w = scale * (f.trimmed ? f.fw : (f.src_w > 0.0f ? f.src_w : f.fw));
    const float quad_h = scale * (f.trimmed ? f.fh : (f.src_h > 0.0f ? f.src_h : f.fh));
    const float hw = quad_w * 0.5f, hh = quad_h * 0.5f;

    // The batch owner `Xb` carries `scale=(1,-1)` (JS L1149), reflecting every
    // billboard across its emitter node's local x-axis. A centred rect is
    // symmetric, so the geometry is the rect rotated by `-rotation`, and the
    // atlas frame is mirrored vertically. `draw.rotation_rad` is already the
    // negated view rotation (see `make_particle_draw_`).
    const float th = draw.rotation_rad;
    const float ct = std::cos(th), st = std::sin(th);
    float lx[4] = {-hw, hw, -hw, hw};
    float ly[4] = {-hh, -hh, hh, hh};
    for (int c = 0; c < 4; ++c) {
        const float px = lx[c] * ct - ly[c] * st;
        const float py = lx[c] * st + ly[c] * ct;
        lx[c] = px;
        ly[c] = py;
    }

    const float u0 = f.tex_w > 0.0f ? f.fx / f.tex_w : 0.0f;
    const float v0 = f.tex_h > 0.0f ? f.fy / f.tex_h : 0.0f;
    const float u1 = f.tex_w > 0.0f ? (f.fx + f.fw) / f.tex_w : 1.0f;
    const float v1 = f.tex_h > 0.0f ? (f.fy + f.fh) / f.tex_h : 1.0f;
    // Corner order matches the sprite path (0=TL 1=TR 2=BL 3=BR); the V pair
    // is swapped for the batch's vertical mirror.
    const float u[4] = {u0, u1, u0, u1};
    const float v[4] = {v1, v1, v0, v0};

    const float wx = draw.x * layer_scale;
    const float wy = draw.y * layer_scale + layer_y;
    const float sx[4] = {
        camera.world_to_screen_x(wx + lx[0] * layer_scale, draw.factor),
        camera.world_to_screen_x(wx + lx[1] * layer_scale, draw.factor),
        camera.world_to_screen_x(wx + lx[2] * layer_scale, draw.factor),
        camera.world_to_screen_x(wx + lx[3] * layer_scale, draw.factor)};
    const float sy[4] = {
        camera.world_to_screen_y(wy + ly[0] * layer_scale),
        camera.world_to_screen_y(wy + ly[1] * layer_scale),
        camera.world_to_screen_y(wy + ly[2] * layer_scale),
        camera.world_to_screen_y(wy + ly[3] * layer_scale)};

    // JS WebGL shader (L1750): `v_color = mix(u_startColor, u_endColor, a_t)
    // * a_alpha`. `a_t` is the view's `KXa`, initialised 0 (L1649) and never
    // assigned, and the batch fills all four vertices from it (L1755), so the
    // result is the START colour times the particle alpha. `draw.color_*`
    // already carry `Na.Rv(Zib(Color)[0])` (white when Color is absent).
    const float r = draw.color_r * draw.alpha;
    const float g = draw.color_g * draw.alpha;
    const float b = draw.color_b * draw.alpha;
    const float a = draw.color_a * draw.alpha;
    SpriteQuad quad;
    static const int kOrder[6] = {0, 1, 2, 2, 1, 3};
    for (int i = 0; i < 6; ++i) {
        const int k = kOrder[i];
        quad.v[i] = {sx[k], sy[k], u[k], v[k], r, g, b, a};
    }
    batch_.add_quad(quad, particle_texture_);
}

void Renderer::draw_triangles(const float* verts, std::size_t vertex_count,
                              float r, float g, float b, float a) {
    batch_.add_triangles(verts, vertex_count, r, g, b, a);
}

void Renderer::draw_effect_quad(float cx, float cy, float w, float h,
                                float rotation_deg, float r, float g, float b,
                                float a) {
    const float hw = w * 0.5f, hh = h * 0.5f;
    // Corner pairing matches the sprite path: 0=(-w,-h) 1=(+w,-h)
    // 2=(-w,+h) 3=(+w,+h).
    float lx[4] = {-hw, hw, -hw, hw};
    float ly[4] = {-hh, -hh, hh, hh};
    if (rotation_deg != 0.0f) {
        const float th = rotation_deg * 3.14159265358979323846f / 180.0f;
        const float ct = std::cos(th), st = std::sin(th);
        for (int c = 0; c < 4; ++c) {
            const float px = lx[c] * ct - ly[c] * st;
            const float py = lx[c] * st + ly[c] * ct;
            lx[c] = px;
            ly[c] = py;
        }
    }
    // Two triangles: (0,1,2) (2,1,3) — same winding as the sprite batch.
    const float verts[12] = {
        cx + lx[0], cy + ly[0], cx + lx[1], cy + ly[1], cx + lx[2], cy + ly[2],
        cx + lx[1], cy + ly[1], cx + lx[3], cy + ly[3], cx + lx[2], cy + ly[2],
    };
    batch_.add_triangles(verts, 6, r, g, b, a);
}
void Renderer::render_node(sf2::scene::Node& node, const Camera& camera) {
    node.render(*this);
    for (const auto& child : node.children()) {
        render_node(*child, camera);
    }
}

void Renderer::begin_frame(const Camera& camera) {
    camera_ = camera;
    gl::glViewport(0, 0, static_cast<int>(camera.view_w), static_cast<int>(camera.view_h));
    gl::glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl::glClear(GL_COLOR_BUFFER_BIT);
    gl::glEnable(GL_BLEND);
    gl::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    batch_.set_viewport(static_cast<int>(camera.view_w), static_cast<int>(camera.view_h));
}

void Renderer::end_frame() {
    batch_.flush();
    glfwSwapBuffers(window_);
    glfwPollEvents();
}

} // namespace sf2::render

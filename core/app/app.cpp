// App shell implementation — main loop, input, shared asset loading.

#include "app/app.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "app/fight_assets.hpp"
#include "app/quest_engine.hpp"
#include "app/save_system.hpp"
#include "app/screen_manager.hpp"
#include "app/screens.hpp"
#include "atlas.hpp"
#include "audio/audio.hpp"
#include "font.hpp"
#include "render/gl.hpp"
#include "scene/renderer.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"
#include "xml_archive.hpp"
#include "xml_doc.hpp"
#include "zstd_stream.hpp"

namespace sf2::app {

namespace {

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
    const std::vector<std::uint8_t> bytes = read_file_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

// Decompresses + parses a zstd xml archive (models/animations .dat).
std::vector<sf2::data::archive_entry> load_archive(const std::string& path) {
    const std::vector<std::uint8_t> compressed = read_file_bytes(path);
    const std::vector<std::uint8_t> decompressed =
        sf2::data::zstd_decompress(compressed);
    return sf2::data::xml_archive_parse(decompressed.data(), decompressed.size());
}

const sf2::data::archive_entry* find_entry(
    const std::vector<sf2::data::archive_entry>& entries, const std::string& name) {
    for (const sf2::data::archive_entry& entry : entries) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

// The extracted moves.xml / tactic_settings.xml (the game loads them from
// xml.dat; the extracted copies are the canonical source).
std::string extracted_xml(const std::string& name) {
    const std::string path = "reference/extracted/xml/res/" + name;
    if (std::filesystem::exists(path)) {
        return read_file_text(path);
    }
    throw std::runtime_error("extracted xml missing: " + path);
}

// Decodes an atlas texture by trying all decodable formats (ktx ASTC, dds BCn, webp, png).
bool decode_atlas_any(const std::string& base, sf2::data::Texture& out) {
    const std::string dir = std::filesystem::path(base).parent_path().string();
    const std::string stem = std::filesystem::path(base).filename().string();
    for (const std::string& ext : {".ktx", ".dds", ".webp", ".png", ".jpg"}) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(stem + ".", 0) == 0 && entry.path().extension().string() == ext) {
                if (sf2::data::decode_texture(entry.path().string(), out)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Helper to load a TexturePacker atlas (json + sibling texture) under dir/prefix.
static GLuint load_ui_atlas_bundle_impl(sf2::app::App& app, const std::string& dir, const std::string& prefix) {
    std::string json_path;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix + ".", 0) == 0 && entry.path().extension().string() == ".json") {
            json_path = entry.path().string();
            break;
        }
    }
    if (json_path.empty()) return 0;
    sf2::data::Texture tex;
    if (!decode_atlas_any(dir + "/" + prefix, tex)) {
        std::fprintf(stderr, "app: atlas texture missing for %s/%s\n", dir.c_str(), prefix.c_str());
        return 0;
    }
    const GLuint gl = app.renderer().texture_for(prefix + "_atlas", tex);
    if (gl == 0) return 0;
    try {
        const std::vector<std::uint8_t> jb = read_file_bytes(json_path);
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[app] atlas %s/%s: %dx%d tex %dx%d %zu frames (tex %u)\n",
                     dir.c_str(), prefix.c_str(), a.w, a.h, tex.w, tex.h, a.frames.size(), gl);
        return gl;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: atlas parse failed %s: %s\n", json_path.c_str(), e.what());
        return 0;
    }
}

} // namespace

App::App() = default;

App::~App() { shutdown(); }

bool App::init(const std::string& res_root, const std::string& save_path) {
    res_root_ = res_root;
    save_path_ = save_path;

    renderer_ = std::make_unique<sf2::render::Renderer>();
    GLFWwindow* window = nullptr;
    if (!renderer_->init(view_w_, view_h_, /*hidden=*/false, &window)) {
        std::fprintf(stderr, "app: renderer init failed\n");
        return false;
    }

    // Save system — first run uses the users_default template. The shipped
    // res has the hashed name users_default.b7da2019.xml (G.rq[9]); the
    // extracted copy lives at reference/extracted/xml/res/users_default.xml.
    std::string default_save = res_root + "/users_default.xml";
    if (!std::filesystem::exists(default_save)) {
        const std::string hashed = res_root + "/users_default.b7da2019.xml";
        if (std::filesystem::exists(hashed)) {
            default_save = hashed;
        } else {
            const std::string extracted = "reference/extracted/xml/res/users_default.xml";
            if (std::filesystem::exists(extracted)) {
                default_save = extracted;
            }
        }
    }
    try {
        save_ = std::make_unique<SaveSystem>(save_path, default_save);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: save init failed: %s\n", e.what());
        return false;
    }

    // Dojo background (webp + atlas json) — the menu/map backdrop.
    try {
        const std::string loc = res_root + "/locations/dojo";
        sf2::data::Texture tex;
        if (decode_atlas_any(loc + "/dojo", tex)) {
            const GLuint gl_tex = renderer_->texture_for("dojo_bg", tex);
            const std::vector<std::uint8_t> json_bytes =
                read_file_bytes(loc + "/dojo.d31b1e71.json");
            const sf2::data::atlas a = sf2::data::atlas_parse(json_bytes.data(), json_bytes.size());
            for (const auto& fr : a.frames) {
                renderer_->texture_alias(fr.name, gl_tex);
            }
            // The full-screen background frame (_0015_bg: 1936x512 in a
            // 1960-wide arena). Draw it scaled to the view.
            dojo_sprite_ = std::make_unique<sf2::scene::Sprite>();
            dojo_sprite_->texture_name = "_0015_bg";
            dojo_sprite_->solid = false;
            const auto* frame = [&]() -> const sf2::data::atlas_frame* {
                for (const auto& fr : a.frames) {
                    if (fr.name == "_0015_bg") return &fr;
                }
                return nullptr;
            }();
            if (frame != nullptr) {
                dojo_sprite_->frame_x = static_cast<float>(frame->x);
                dojo_sprite_->frame_y = static_cast<float>(frame->y);
                dojo_sprite_->frame_w = static_cast<float>(frame->w);
                dojo_sprite_->frame_h = static_cast<float>(frame->h);
                dojo_sprite_->tex_w = static_cast<float>(a.w);
                dojo_sprite_->tex_h = static_cast<float>(a.h);
                // Center the bg on the view, stretched to cover it.
                dojo_sprite_->transform.set_pos(view_w_ / 2.0f, view_h_ / 2.0f);
                dojo_sprite_->transform.set_scale(static_cast<float>(view_w_) / frame->w,
                                                  static_cast<float>(view_h_) / frame->h);
            }
        } else {
            std::fprintf(stderr, "app: dojo webp unavailable — flat bg\n");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: dojo bg load failed: %s\n", e.what());
    }

    // Menu font (BMFont binary + png page).
    try {
        const std::string ui = res_root + "/ui";
        menu_font_ = std::make_unique<sf2::data::font>();
        {
            const std::vector<std::uint8_t> fnt_bytes = read_file_bytes(ui + "/font-en.7043b83b.fnt");
            *menu_font_ = sf2::data::font_parse(fnt_bytes.data(), fnt_bytes.size());
        }
        // The .fnt page name is "-" (a relative ref); the real page is
        // ui/font-en.<hash>.png (asset id 264 = ui/font{lang}.png).
        sf2::data::Texture font_tex;
        if (decode_atlas_any(ui + "/font-en", font_tex)) {
            font_tex_ = renderer_->texture_for("font-en", font_tex);
        } else {
            std::fprintf(stderr, "app: font page png unavailable\n");
        }
        std::fprintf(stdout, "[app] menu font: %zu chars %dx%d tex %u\n",
                     menu_font_->chars.size(), menu_font_->scale_w, menu_font_->scale_h,
                     font_tex_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: font load failed: %s\n", e.what());
        menu_font_.reset();
    }

    // Fight HUD fonts: digits (timer) + round (round label). The .fnt page
    // name is e.g. "digits_0.png" — the real file is digits.<hash>.png.
    try {
        const std::string fight = res_root + "/fight";
        for (const auto& entry : std::filesystem::directory_iterator(fight)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("digits.", 0) == 0 && entry.path().extension() == ".fnt") {
                const std::vector<std::uint8_t> b = read_file_bytes(entry.path().string());
                digits_font_ = std::make_unique<sf2::data::font>(sf2::data::font_parse(b.data(), b.size()));
                sf2::data::Texture tex;
                if (decode_atlas_any(fight + "/digits", tex)) {
                    digits_tex_ = renderer_->texture_for("digits_font", tex);
                    std::fprintf(stdout, "[app] digits font: %zu chars %dx%d tex %u (%s)\n",
                                 digits_font_->chars.size(), digits_font_->scale_w,
                                 digits_font_->scale_h, digits_tex_, name.c_str());
                }
                break;
            }
        }
        for (const auto& entry : std::filesystem::directory_iterator(fight)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("round.", 0) == 0 && entry.path().extension() == ".fnt") {
                const std::vector<std::uint8_t> b = read_file_bytes(entry.path().string());
                round_font_ = std::make_unique<sf2::data::font>(sf2::data::font_parse(b.data(), b.size()));
                sf2::data::Texture tex;
                if (decode_atlas_any(fight + "/round", tex)) {
                    round_tex_ = renderer_->texture_for("round_font", tex);
                    std::fprintf(stdout, "[app] round font: %zu chars %dx%d tex %u (%s)\n",
                                 round_font_->chars.size(), round_font_->scale_w,
                                 round_font_->scale_h, round_tex_, name.c_str());
                }
                break;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: fight font load failed: %s\n", e.what());
    }

    // UI atlases (menu/map/shop/profile/etc.) — KTX ASTC decoded to RGBA.
    try {
        const std::string ui = res_root + "/ui";
        const std::string mp = res_root + "/map";
        load_ui_atlas_bundle_impl(*this, ui, "menu");
        load_ui_atlas_bundle_impl(*this, ui, "shop");
        load_ui_atlas_bundle_impl(*this, ui, "profile");
        load_ui_atlas_bundle_impl(*this, ui, "misc");
        load_ui_atlas_bundle_impl(*this, ui, "skills");
        load_ui_atlas_bundle_impl(*this, mp, "buttons");
        // Fight HUD atlas (HealthBar_*, Round_*, FightPause) — 1px slices
        // stretched to the bar rects (see screens.cpp FightScreen HUD).
        load_ui_atlas_bundle_impl(*this, res_root + "/fight", "ui");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: ui atlas load failed: %s\n", e.what());
    }

    screens_ = std::make_unique<ScreenManager>(*this);

    // The shared fight assets (models/moves/clips/tactics/location). Built
    // once; the Shop/Equipment screens rebuild the merged model on equip.
    try {
        fight_assets_ = std::make_unique<FightAssets>();
        const std::string res = res_root_;
        const std::vector<sf2::data::archive_entry> models =
            load_archive(res + "/models.473fd74f.dat");
        const auto load_model = [&](const std::string& name) {
            const sf2::data::archive_entry* e = find_entry(models, name);
            if (e == nullptr) throw std::runtime_error("model '" + name + "' not found");
            return sf2::scene::model_parse(e->data.data(), e->data.size());
        };
        fight_assets_->skeleton = load_model("mdl_skeleton");
        fight_assets_->body = load_model("mdl_body");
        fight_assets_->head = load_model("mdl_head");
        // The default Fists = no weapon model (JS: Fists has no Model).
        fight_assets_->weapon = sf2::scene::Model{};
        fight_assets_->armor = sf2::scene::Model{};  // Body default
        fight_assets_->helm = sf2::scene::Model{};   // Head default
        fight_assets_->rebuild_merged();

        const std::vector<sf2::data::archive_entry> anim =
            load_archive(res + "/animations.b22c72ff.dat");
        const std::vector<sf2::data::archive_entry> anim_dojo =
            load_archive(res + "/animations_dojo.3314a7de.dat");
        for (const auto& e : anim) {
            fight_assets_->clips.emplace(
                e.name, sf2::data::anim_clip_parse(e.name, e.data.data(), e.data.size()));
        }
        for (const auto& e : anim_dojo) {
            fight_assets_->clips.emplace(
                e.name, sf2::data::anim_clip_parse(e.name, e.data.data(), e.data.size()));
        }

        const std::string moves_xml = extracted_xml("moves.xml");
        if (!sf2::scene::parse_moves(moves_xml, fight_assets_->moves)) {
            throw std::runtime_error("parse_moves failed");
        }

        const std::string t_settings = extracted_xml("tactic_settings.xml");
        sf2::scene::parse_tactic_settings(t_settings, fight_assets_->tactic_defs);

        // The fists tactics file (the AI's decision tables).
        {
            const std::string dir = res + "/tactics";
            std::string t_file;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("fists_fists.", 0) == 0 && name.substr(name.size() - 4) == ".dat") {
                    t_file = entry.path().string();
                    break;
                }
            }
            if (!t_file.empty()) {
                const std::vector<std::uint8_t> t_bytes = read_file_bytes(t_file);
                fight_assets_->tactics_sets =
                    sf2::scene::tactics_parse_file(t_bytes.data(), t_bytes.size());
            }
        }

        std::fprintf(stdout, "[assets] merged fighter bones=%zu tris=%zu, moves=%zu, clips=%zu, tactics=%zu groups\n",
                     fight_assets_->merged.bones.size(),
                     fight_assets_->merged.resolved_tris.size(),
                     fight_assets_->moves.size(), fight_assets_->clips.size(),
                     fight_assets_->tactics_sets.size());
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: fight assets load failed: %s\n", e.what());
        fight_assets_.reset();
    }

    // [Phase A3] The SFX engine: preloads the game's wav samples and
    // starts the audio device (its own thread — the game loop never
    // blocks). Falls back to a beep generator when no samples resolve.
    {
        sf2::audio::AudioEngine& sfx = sf2::audio::AudioEngine::instance();
        const bool ok = sfx.init(res_root_);
        std::fprintf(stdout, "[audio] engine %s\n", ok ? "OK" : "NO DEVICE (silent)");
        std::fflush(stdout);
    }

    boot();
    return true;
}

void App::boot() {
    // Boot to the Dojo home screen (screen 3) — the ORIGINAL starts in the
    // Dojo (the JS flow: Preloader(0) -> Loader(2) -> Dojo(3)), NOT in the
    // GeneralMenu (screen 8; the native GeneralMenu is kept for the menu
    // flow but the game boots through Preloader -> Loader -> Dojo -> the
    // home hub; the shell skips the loading screens and starts at the dojo).
    std::fprintf(stdout, "[screen] boot: Preloader(0) -> Loader(2) -> Dojo(3)\n");
    std::fflush(stdout);
    screens_->push(make_screen(*screens_, kScreenDojo));
    // Session start for the quest engine (JS `v.uwb` -> QUEST_EVENT_SESSION,
    // fired from the loader; the Dojo push above already fired ChangeTab +
    // SceneLoaded for the boot edge).
    try {
        QuestJournal j;
        try {
            j.player_level = save_->load().level;
        } catch (const std::exception&) {
        }
        quest_engine().fire(*this, "SessionStart", j);
    } catch (const std::exception&) {
    }
}

void App::poll_input() {
    pointer_.pressed = false;
    if (injected_click_pending_) {
        pointer_.x = injected_x_;
        pointer_.y = injected_y_;
        pointer_.down = true;
        // Only the FIRST pending step is a pressed edge (the subsequent
        // steps are the held button). Without this, every pending step
        // re-triggers pressed consumers (a buy/equip click fired 3x).
        pointer_.pressed = injected_click_steps_ == 3;
        std::fprintf(stdout, "[input] injected click at (%.0f, %.0f) steps left=%d\n", pointer_.x,
                     pointer_.y, injected_click_steps_);
        std::fflush(stdout);
        return;
    }
    // The real pointer comes from GLFW callbacks in the platform layer; for
    // this phase the shell polls the mouse button + cursor position.
    int button = glfwGetMouseButton(renderer_->window(), GLFW_MOUSE_BUTTON_LEFT);
    bool down = button == GLFW_PRESS;
    if (down && !pointer_.down) {
        pointer_.pressed = true;
    }
    pointer_.down = down;
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(renderer_->window(), &x, &y);
    pointer_.x = x;
    pointer_.y = y;

    // Keyboard: poll the fight keys and route edge transitions to the top
    // screen (JS `Ik` keydown/keyup -> the fight input path). Only the keys
    // the fight uses (WASD/arrows/space).
    struct KeyMap {
        int glfw;
        int idx;
    };
    static const KeyMap kFightKeys[] = {
        {GLFW_KEY_A, 0}, {GLFW_KEY_LEFT, 0}, {GLFW_KEY_D, 1},
        {GLFW_KEY_RIGHT, 1}, {GLFW_KEY_W, 2}, {GLFW_KEY_UP, 2},
        {GLFW_KEY_S, 3}, {GLFW_KEY_DOWN, 3}, {GLFW_KEY_SPACE, 4},
    };
    for (const KeyMap& km : kFightKeys) {
        const bool now_down = glfwGetKey(renderer_->window(), km.glfw) == GLFW_PRESS;
        if (now_down && !keys_held_[km.idx]) {
            keys_held_[km.idx] = true;
            if (screens_ != nullptr && screens_->top() != nullptr) {
                screens_->top()->on_key(km.glfw, true);
            }
        } else if (!now_down && keys_held_[km.idx]) {
            keys_held_[km.idx] = false;
            if (screens_ != nullptr && screens_->top() != nullptr) {
                screens_->top()->on_key(km.glfw, false);
            }
        }
    }
}

void App::update_fixed(float dt) {
    screens_->update(dt);
}

void App::render_frame() {
    sf2::render::Camera camera;
    camera.view_w = static_cast<float>(view_w_);
    camera.view_h = static_cast<float>(view_h_);
    renderer_->begin_frame(camera);
    screens_->render(*this);
    renderer_->end_frame();
}

void App::run_one_frame() {
    glfwPollEvents();

    // Auto-click BEFORE poll_input: inject_click arms a pending click whose
    // pressed edge fires on the FIRST poll (steps == 3). Injection after
    // poll_input missed that edge — the first poll saw steps already
    // decremented to 2, so `pressed` never became true and the injected
    // click was a silent no-op. Stage 0: click the FIGHT button on the
    // boot screen (the Dojo home — its FIGHT button starts the training
    // fight). Stage 1 (after the map is up): click the Training node.
    if (auto_click_) {
        if (auto_click_stage_ == 0 && frame_count_ == 30) {
            inject_click(view_w_ * 0.28, view_h_ * 0.72);
            auto_click_stage_ = 1;
        } else if (auto_click_stage_ == 1 && frame_count_ == 60 &&
                   screens_->current_id() == kScreenMap) {
            // Training node at (view_w/2 + 158, view_h/2 - 145).
            inject_click(view_w_ / 2.0 + 158.0, view_h_ / 2.0 - 145.0);
            auto_click_stage_ = 2;
        }
    }

    poll_input();

    const double now = glfwGetTime();
    double dt = now - last_time_;
    last_time_ = now;
    if (dt > 0.25) dt = 0.25;
    if (headless_frames_ > 0) {
        // Headless runs uncapped: force one fixed step per frame so the
        // screen flow (menu -> map -> node) advances deterministically.
        acc_ = kFixedDt;
    } else {
        acc_ += dt;
    }

    while (acc_ >= kFixedDt) {
        update_fixed(static_cast<float>(kFixedDt));
        if (injected_click_pending_ && --injected_click_steps_ <= 0) {
            injected_click_pending_ = false;
            std::fprintf(stdout, "[input] click consumed after %d steps\n",
                         injected_click_steps_ + 1);
        }
        acc_ -= kFixedDt;
        ++fixed_steps_;
    }
    render_frame();
    ++frame_count_;
}

void App::run(int headless_frames, bool auto_click) {
    headless_frames_ = headless_frames;
    auto_click_ = auto_click;
    last_time_ = glfwGetTime();

    while (!glfwWindowShouldClose(renderer_->window())) {
        run_one_frame();

        if (headless_frames_ > 0 && frame_count_ >= headless_frames_) {
            break;
        }
    }
}

void App::shutdown() {
    // [Phase A3] Stop the SFX engine (logs the played counters — the
    // headless verification proof).
    sf2::audio::AudioEngine::instance().shutdown();
    screens_.reset();
    fight_assets_.reset();
    save_.reset();
    dojo_sprite_.reset();
    menu_font_.reset();
    digits_font_.reset();
    round_font_.reset();
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
}

bool App::capture_png(const std::string& path) {
    // The render path swaps buffers (end_frame); the just-presented frame
    // lives in GL_FRONT. gl_capture_png reads GL_BACK by default (the
    // previous frame), so flip the read buffer first.
    sf2::render::gl_read_buffer_front(renderer_->window());
    const bool ok = sf2::render::gl_capture_png(renderer_->window(), path);
    sf2::render::gl_read_buffer_back(renderer_->window());
    return ok;
}

void App::inject_key(int glfw_key, bool down) {
    if (screens_ != nullptr && screens_->top() != nullptr) {
        screens_->top()->on_key(glfw_key, down);
    }
}

void App::register_atlas_frame(const sf2::data::atlas_frame& fr, int tex_w, int tex_h, unsigned int gl_tex) {
    AtlasEntry e;
    e.frame = fr;
    e.tex_w = tex_w;
    e.tex_h = tex_h;
    e.gl_tex = gl_tex;
    atlas_cache_[fr.name] = std::move(e);
    renderer_->texture_alias(fr.name, gl_tex);
}

bool App::get_atlas_frame(const std::string& name, sf2::data::atlas_frame* out, int* tex_w, int* tex_h, unsigned int* gl_tex) const {
    auto it = atlas_cache_.find(name);
    if (it == atlas_cache_.end()) return false;
    if (out) *out = it->second.frame;
    if (tex_w) *tex_w = it->second.tex_w;
    if (tex_h) *tex_h = it->second.tex_h;
    if (gl_tex) *gl_tex = it->second.gl_tex;
    return true;
}

bool App::draw_atlas_frame(const std::string& name, float cx, float cy, float scale, float alpha) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!get_atlas_frame(name, &fr, &tw, &th, &gl)) return false;
    sf2::scene::Sprite s;
    s.texture_name = name;
    s.frame_x = static_cast<float>(fr.x);
    s.frame_y = static_cast<float>(fr.y);
    s.frame_w = static_cast<float>(fr.w);
    s.frame_h = static_cast<float>(fr.h);
    s.tex_w = static_cast<float>(tw);
    s.tex_h = static_cast<float>(th);
    s.solid = false;
    s.color_a = alpha;
    if (fr.rotated) {
        std::swap(s.frame_w, s.frame_h);
    }
    s.transform.set_pos(cx, cy);
    s.transform.set_scale(scale, scale);
    // UI is screen-space: use an identity camera (world == screen)
    sf2::render::Camera ui_cam;
    ui_cam.center_x = view_w_ * 0.5f;
    ui_cam.center_y = view_h_ * 0.5f;
    ui_cam.zoom = 1.0f;
    ui_cam.view_w = static_cast<float>(view_w_);
    ui_cam.view_h = static_cast<float>(view_h_);
    ui_cam.arena_h = ui_cam.view_h;
    ui_cam.arena_floor = 0.0f;
    ui_cam.arena_center_x = ui_cam.center_x;
    renderer_->draw_sprite(s, ui_cam);
    return true;
}

bool App::draw_atlas_rect(const std::string& name, float x, float y, float w, float h,
                          float alpha) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!get_atlas_frame(name, &fr, &tw, &th, &gl)) return false;
    sf2::scene::Sprite s;
    s.texture_name = name;
    s.frame_x = static_cast<float>(fr.x);
    s.frame_y = static_cast<float>(fr.y);
    s.frame_w = static_cast<float>(fr.w);
    s.frame_h = static_cast<float>(fr.h);
    s.tex_w = static_cast<float>(tw);
    s.tex_h = static_cast<float>(th);
    s.solid = false;
    s.color_a = alpha;
    if (fr.rotated) std::swap(s.frame_w, s.frame_h);
    // x,y is top-left in view space; Sprite pos is center.
    s.transform.set_pos(x + w * 0.5f, y + h * 0.5f);
    if (fr.w > 0 && fr.h > 0) {
        s.transform.set_scale(w / static_cast<float>(fr.w),
                              h / static_cast<float>(fr.h));
    }
    sf2::render::Camera ui_cam;
    ui_cam.center_x = static_cast<float>(view_w_) * 0.5f;
    ui_cam.center_y = static_cast<float>(view_h_) * 0.5f;
    ui_cam.zoom = 1.0f;
    ui_cam.view_w = static_cast<float>(view_w_);
    ui_cam.view_h = static_cast<float>(view_h_);
    ui_cam.arena_h = ui_cam.view_h;
    ui_cam.arena_floor = 0.0f;
    ui_cam.arena_center_x = ui_cam.center_x;
    renderer_->draw_sprite(s, ui_cam);
    return true;
}

float App::measure_text(const sf2::data::font& font, const std::string& text,
                        float scale) const {
    float w = 0.0f;
    for (unsigned char ch : text) {
        const std::uint32_t id = static_cast<std::uint32_t>(ch);
        const sf2::data::font_char* g = nullptr;
        for (const auto& c : font.chars) {
            if (c.id == id) { g = &c; break; }
        }
        if (g == nullptr) continue;
        w += static_cast<float>(g->xadvance) * scale;
    }
    return w;
}

bool App::draw_text_with_font(const sf2::data::font& font, unsigned int tex, float x,
                              float y, const std::string& text, float scale, float r,
                              float g, float b, float a) {
    if (tex == 0) return false;
    // Map font page -> texture name used for lookup: we uploaded as
    // "font-en"/"digits_font"/"round_font"; the sprite system resolves
    // by texture_name alias. Register the page alias on demand.
    std::string tex_name;
    if (tex == font_tex_) tex_name = "font-en";
    else if (tex == digits_tex_) tex_name = "digits_font";
    else if (tex == round_tex_) tex_name = "round_font";
    else tex_name = "font-en";
    // Ensure renderer knows the alias for the font's page name if it
    // differs (the .fnt page string is e.g. "digits_0.png" vs "digits_font").
    if (!font.page.empty() && font.page != "-") {
        renderer_->texture_alias(font.page, tex);
        // digits_0.png sibling without hash is not in cache; also alias the
        // generic page key.
    }
    // Include hash PNG alias too (digits_0.png etc.)
    float cursor_x = x;
    float cursor_y = y;
    bool any = false;
    for (unsigned char ch : text) {
        const std::uint32_t id = static_cast<std::uint32_t>(ch);
        const sf2::data::font_char* glyph = nullptr;
        for (const auto& c : font.chars) {
            if (c.id == id) { glyph = &c; break; }
        }
        if (glyph == nullptr) continue;
        if (glyph->w == 0 || glyph->h == 0) {
            cursor_x += static_cast<float>(glyph->xadvance) * scale;
            continue;
        }
        sf2::scene::Sprite s;
        s.texture_name = tex_name;
        // Also ensure alias resolution for tex_name
        if (renderer_->texture_lookup(tex_name) == 0) {
            renderer_->texture_alias(tex_name, tex);
        }
        s.frame_x = static_cast<float>(glyph->x);
        s.frame_y = static_cast<float>(glyph->y);
        s.frame_w = static_cast<float>(glyph->w);
        s.frame_h = static_cast<float>(glyph->h);
        s.tex_w = static_cast<float>(font.scale_w);
        s.tex_h = static_cast<float>(font.scale_h);
        s.solid = false;
        s.color_r = r;
        s.color_g = g;
        s.color_b = b;
        s.color_a = a;
        // Glyph quad centered at (cursor + offset + half size)
        s.transform.set_pos(cursor_x + (static_cast<float>(glyph->xoffset) + glyph->w * 0.5f) * scale,
                            cursor_y + (static_cast<float>(glyph->yoffset) + glyph->h * 0.5f) * scale);
        s.transform.set_scale(scale, scale);
        sf2::render::Camera ui_cam;
        ui_cam.center_x = static_cast<float>(view_w_) * 0.5f;
        ui_cam.center_y = static_cast<float>(view_h_) * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = static_cast<float>(view_w_);
        ui_cam.view_h = static_cast<float>(view_h_);
        ui_cam.arena_h = ui_cam.view_h;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = ui_cam.center_x;
        renderer_->draw_sprite(s, ui_cam);
        cursor_x += static_cast<float>(glyph->xadvance) * scale;
        any = true;
    }
    return any;
}

bool App::draw_text_centered(const sf2::data::font& font, unsigned int tex, float cx,
                             float y, const std::string& text, float scale, float r,
                             float g, float b, float a) {
    const float w = measure_text(font, text, scale);
    return draw_text_with_font(font, tex, cx - w * 0.5f, y, text, scale, r, g, b, a);
}

bool App::draw_text(float x, float y, const std::string& text, float scale, float r, float g,
                    float b) {
    if (menu_font_ == nullptr || font_tex_ == 0) {
        return false;
    }
    return draw_text_with_font(*menu_font_, font_tex_, x, y, text, scale, r, g, b);
}

} // namespace sf2::app

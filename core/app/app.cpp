// App shell implementation — main loop, input, shared asset loading.

#include "app/app.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "app/fight_assets.hpp"
#include "app/save_system.hpp"
#include "app/screen_manager.hpp"
#include "app/screens.hpp"
#include "atlas.hpp"
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

// Decodes an atlas texture by trying the decodable formats (webp first).
// The UI atlases ship as ASTC ktx / crunch dds which the CPU pipeline
// cannot decode — the caller falls back to a flat background in that case.
bool decode_atlas_any(const std::string& base, sf2::data::Texture& out) {
    const std::string dir = std::filesystem::path(base).parent_path().string();
    const std::string stem = std::filesystem::path(base).filename().string();
    for (const std::string& ext : {".webp", ".png", ".jpg"}) {
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
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: font load failed: %s\n", e.what());
        menu_font_.reset();
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

    boot();
    return true;
}

void App::boot() {
    // Boot straight to the GeneralMenu (screen 8) — the game boots through
    // Preloader(0) -> Loader(2) -> Dojo(3) -> the menu; the shell skips the
    // loading screens and starts at the playable menu (the fight demo runs
    // separately). Logged as the JS screen flow would trace it.
    std::fprintf(stdout, "[screen] boot: Preloader(0) -> Loader(2) -> Dojo(3) -> GeneralMenu(8)\n");
    std::fflush(stdout);
    screens_->push(make_screen(*screens_, kScreenGeneralMenu));
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
    poll_input();

    // Auto-click before the fixed-step update so the screen sees the
    // pressed edge this frame. Stage 0: click the Fight button (menu).
    // Stage 1 (after the map is up): click the Training node.
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
    screens_.reset();
    fight_assets_.reset();
    save_.reset();
    dojo_sprite_.reset();
    menu_font_.reset();
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

bool App::draw_text(float x, float y, const std::string& text, float scale, float r, float g,
                    float b) {
    if (menu_font_ == nullptr || font_tex_ == 0) {
        return false;
    }
    (void)x;
    (void)y;
    (void)text;
    (void)scale;
    (void)r;
    (void)g;
    (void)b;
    // Text rendering is implemented by the screens through the renderer;
    // this helper is a stub for now (the BMFont glyph-quad path is a
    // later-phase nicety). The shell's screens draw labeled flat buttons.
    return false;
}

} // namespace sf2::app

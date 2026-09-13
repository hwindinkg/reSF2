// App shell implementation — main loop, input, shared asset loading.

#include "app/app.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <GLFW/glfw3.h>

#include <cctype>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <locale>
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

// Localized asset resolution (JS `G.bg`, L2393-2394): a localized file is
// named `<stem>-<lang>.<hash><ext>`; when it is absent the `<lang=en>` file
// is used (the EN fallback). Returns "" when neither exists.
std::string find_localized_file(const std::string& dir, const std::string& stem,
                                const std::string& lang, const std::string& ext) {
    for (const std::string& l : {lang, std::string("en")}) {
        if (l.empty()) continue;
        const std::string prefix = stem + "-" + l + ".";
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(prefix, 0) == 0 && entry.path().extension() == ext) {
                    return entry.path().string();
                }
            }
        } catch (const std::exception&) {
            // missing directory — fall through to the EN attempt
        }
    }
    return {};
}

// JS `G.Ska` (L2392): lowercase the requested language and coerce it to the
// supported set `G.v9` (L2492); anything else (including null) becomes "en".
// Vanilla `assets/localization.xml` (<Languages Default="eng">) agrees.
std::string resolve_language(const std::string& lang) {
    std::string l;
    l.reserve(lang.size());
    for (const char ch : lang) {
        l.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    static const char* const kSupported[] = {"tr", "ru", "pt", "ko", "ja",
                                             "it", "fr", "es", "en", "de"};
    for (const char* const s : kSupported) {
        if (l == s) return l;
    }
    return "en";
}

// The platform UI language's primary subtag (the native analog of the browser
// `navigator.language` the JS platform bridge returns — `Ca.c6a()` =
// `window.GameInterface.getCurrentLanguage()` = `p.get().locale ||
// navigator.language`, microsite-game-interface L61141; `L.web` L33041 reads
// it and validates it against `iv` before `new L(a)`). `std::locale("")`
// reports the user's default locale ("ru-RU"/"Russian_Russia.1251"); take the
// leading 2-letter subtag lowercased. "" when unavailable.
std::string platform_language() {
#ifdef _WIN32
    // The user's default locale (e.g. "ru-RU" -> "ru"). WebView2's
    // `navigator.language` reports the same OS UI language.
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
    const int n = GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH);
    if (n > 1) {
        std::string l;
        for (int i = 0; i < n - 1 && l.size() < 2; ++i) {
            const wchar_t c = buf[i];
            if (c < 0x80 && std::isalpha(static_cast<unsigned char>(c))) {
                l.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            } else {
                break;
            }
        }
        if (!l.empty()) return l;
    }
#endif
    try {
        const std::string name = std::locale("").name();
        std::string l;
        for (const char ch : name) {
            if (!std::isalpha(static_cast<unsigned char>(ch))) break;
            l.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            if (l.size() == 2) break;
        }
        return l;
    } catch (const std::exception&) {
        return {};
    }
}

// JS `Tk.n5` (L88) verbatim: the localized "Loading" word (UTF-8 bytes).
const char* loading_word(const std::string& lang) {
    if (lang == "de") return "Laden";
    if (lang == "es") return "Cargando";
    if (lang == "fr") return "Chargement";
    if (lang == "it") return "Caricamento";
    if (lang == "ja") return "\xE8\xAA\xAD\xE3\x81\xBF\xE8\xBE\xBC\xE3\x81\xBF\xE4\xB8\xAD";
    if (lang == "ko") return "\xEB\xA1\x9C\xEB\x94\xA9 \xEC\xA4\x91";
    if (lang == "pt") return "Carregando";
    if (lang == "ru") return "\xD0\x97\xD0\xB0\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xBA\xD0\xB0";
    if (lang == "tr") return "Y\xC3\xBCkleniyor";
    return "Loading";
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

bool App::init(const std::string& res_root, const std::string& save_path,
               const std::string& lang) {
    res_root_ = res_root;
    save_path_ = save_path;
    // JS `G.Ska` (L2392): lowercase + default/coerce to "en" (the supported
    // set is `G.v9`, L2492). The runtime lang source is the platform locale
    // (JS `Ca.c6a()`, see platform_language) unless the caller passes one.
    const std::string requested = lang.empty() ? platform_language() : lang;
    lang_ = resolve_language(requested);
    std::fprintf(stdout, "[app] language: platform='%s' requested='%s' -> '%s'\n",
                 platform_language().c_str(), lang.c_str(), lang_.c_str());
    std::fflush(stdout);

    renderer_ = std::make_unique<sf2::render::Renderer>();
    GLFWwindow* window = nullptr;
    if (!renderer_->init(view_w_, view_h_, /*hidden=*/false, &window)) {
        std::fprintf(stderr, "app: renderer init failed\n");
        return false;
    }
    // Keyboard is read by polling `glfwGetKey`, which only reflects keys the
    // focused window received (GLFW `Ik` equivalent: the browser canvas holds
    // focus while playing). A window launched under a terminal can open
    // inactive, so request focus explicitly; otherwise no key reaches
    // `poll_input` until the user clicks.
    if (window != nullptr) {
        glfwFocusWindow(window);
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

    // Menu font (BMFont binary + png page): JS asset ids 264/265,
    // `ui/font{lang}.{png,fnt}` (PORT_AUDIT_UI §0.3 — the RU BMF ships).
    // The .fnt page name is "-" (a relative ref); the real page is
    // ui/font-<lang>.<hash>.png. Missing localized files fall back to EN
    // (JS `G.bg` L2394). The texture cache key stays "font-en" because the
    // sprite layer aliases the menu font under that name.
    try {
        const std::string ui = res_root + "/ui";
        menu_font_ = std::make_unique<sf2::data::font>();
        const std::string fnt_path = find_localized_file(ui, "font", lang_, ".fnt");
        if (fnt_path.empty()) {
            throw std::runtime_error("no ui/font-*.fnt");
        }
        {
            const std::vector<std::uint8_t> fnt_bytes = read_file_bytes(fnt_path);
            *menu_font_ = sf2::data::font_parse(fnt_bytes.data(), fnt_bytes.size());
        }
        sf2::data::Texture font_tex;
        if (decode_atlas_any(ui + "/font-" + lang_, font_tex)) {
            font_tex_ = renderer_->texture_for("font-en", font_tex);
        } else if (lang_ != "en" && decode_atlas_any(ui + "/font-en", font_tex)) {
            font_tex_ = renderer_->texture_for("font-en", font_tex);
            std::fprintf(stdout, "[app] font page fallback -> en\n");
        } else {
            std::fprintf(stderr, "app: font page png unavailable\n");
        }
        std::fprintf(stdout, "[app] menu font[%s]: %zu chars %dx%d tex %u (%s)\n",
                     lang_.c_str(), menu_font_->chars.size(), menu_font_->scale_w,
                     menu_font_->scale_h, font_tex_, fnt_path.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: font load failed: %s\n", e.what());
        menu_font_.reset();
    }

    // Splash/Loader art (JS `Rg` L1967 / `ad` L1969; asset ids 274-279):
    // `splash/loading{lang}.{png,fnt}` (276/277), `splash/logo.png` (275),
    // `splash/bg.jpg` (279). Crash-safe: a miss leaves the overlay art
    // null/0 and the boot proceeds (PORT_AUDIT_UI §4.12).
    try {
        const std::string splash = res_root + "/splash";
        const std::string lf = find_localized_file(splash, "loading", lang_, ".fnt");
        if (!lf.empty()) {
            const std::vector<std::uint8_t> b = read_file_bytes(lf);
            splash_loading_font_ =
                std::make_unique<sf2::data::font>(sf2::data::font_parse(b.data(), b.size()));
            sf2::data::Texture ltex;
            if (decode_atlas_any(splash + "/loading-" + lang_, ltex) ||
                (lang_ != "en" && decode_atlas_any(splash + "/loading-en", ltex))) {
                splash_loading_tex_ = renderer_->texture_for("splash_loading", ltex);
            }
        }
        sf2::data::Texture logo;
        if (decode_atlas_any(splash + "/logo", logo)) {
            splash_logo_tex_ = renderer_->texture_for("splash_logo", logo);
            splash_logo_w_ = logo.w;
            splash_logo_h_ = logo.h;
        }
        sf2::data::Texture bg;
        if (decode_atlas_any(splash + "/bg", bg)) {
            splash_bg_tex_ = renderer_->texture_for("splash_bg", bg);
            splash_bg_w_ = bg.w;
            splash_bg_h_ = bg.h;
        }
        // `Tk.mG` = E.get(278) (cast) and `Tk.qe` = E.get(274) (scroll):
        // standalone splash art (cast.* / scroll.*, webp/avif), decoded
        // directly like the sensei portrait below. `E.get(279)` is the
        // full-screen fill (bg.jpg) `gG`, already loaded as "splash_bg".
        const auto load_splash_art = [&](const char* prefix, const char* alias,
                                         unsigned int* out_tex, int* out_w, int* out_h) {
            for (const auto& entry : std::filesystem::directory_iterator(splash)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(prefix, 0) != 0) continue;
                const std::string ext = entry.path().extension().string();
                if (ext != ".webp" && ext != ".png" && ext != ".avif") continue;
                sf2::data::Texture tex;
                if (!sf2::data::decode_texture(entry.path().string(), tex)) continue;
                *out_tex = renderer_->texture_for(alias, tex);
                *out_w = tex.w;
                *out_h = tex.h;
                break;
            }
        };
        load_splash_art("cast.", "splash_cast", &splash_cast_tex_, &splash_cast_w_,
                        &splash_cast_h_);
        load_splash_art("scroll.", "splash_scroll", &splash_scroll_tex_, &splash_scroll_w_,
                        &splash_scroll_h_);
        // Loader screen (`ad` view `tr`, L1867-1868): `loader/logo.png` (816,
        // `tr.pE`) + `loader/bg.jpg` (817, `ef.Qa`). Separate assets from the
        // Preloader `Tk` art above; a miss leaves them 0 and the loader falls
        // back to the text-only overlay.
        {
            const std::string loader_dir = res_root + "/loader";
            sf2::data::Texture llogo;
            if (decode_atlas_any(loader_dir + "/logo", llogo)) {
                loader_logo_tex_ = renderer_->texture_for("loader_logo", llogo);
                loader_logo_w_ = llogo.w;
                loader_logo_h_ = llogo.h;
            }
            sf2::data::Texture lbg;
            if (decode_atlas_any(loader_dir + "/bg", lbg)) {
                loader_bg_tex_ = renderer_->texture_for("loader_bg", lbg);
                loader_bg_w_ = lbg.w;
                loader_bg_h_ = lbg.h;
            }
            std::fprintf(stdout, "[app] loader art: logo %dx%d tex %u, bg %dx%d tex %u\n",
                         loader_logo_w_, loader_logo_h_, loader_logo_tex_, loader_bg_w_,
                         loader_bg_h_, loader_bg_tex_);
        }
        std::fprintf(stdout,
                     "[app] splash: loading font %zu chars tex %u, logo tex %u, bg tex %u, "
                     "cast tex %u, scroll tex %u\n",
                     splash_loading_font_ != nullptr ? splash_loading_font_->chars.size() : 0,
                     splash_loading_tex_, splash_logo_tex_, splash_bg_tex_, splash_cast_tex_,
                     splash_scroll_tex_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: splash load failed: %s\n", e.what());
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
        // Virtual-gamepad art (Joystick*, btn_punch_*, btn_kick_*): loaded
        // at boot (Dojo wave) so the Dojo hub pad resolves on frame 1.
        // Before, only the FightScreen ctor loaded it, so the hub pad was
        // a silent miss until a fight had run ([ui] miss telemetry).
        load_ui_atlas_bundle_impl(*this, ui, "controller");
        // Fight HUD atlas (HealthBar_*, Round_*, FightPause) — 1px slices
        // stretched to the bar rects (see screens.cpp FightScreen HUD).
        load_ui_atlas_bundle_impl(*this, res_root + "/fight", "ui");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: ui atlas load failed: %s\n", e.what());
    }

    // Sensei portrait (Dojo wave): quest dialogs pass
    // Image="character_sensei_small" (JS `He` L1045-1048); the 256px webp
    // ships with transparent corners already (no CPU masking needed).
    // The green ring is dialog chrome drawn by the Dojo screen.
    try {
        const std::string dir = res_root + "/users/images";
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("character_sensei_small.", 0) != 0) continue;
            const std::string ext = entry.path().extension().string();
            if (ext != ".webp" && ext != ".png") continue;
            sf2::data::Texture tex;
            if (!sf2::data::decode_texture(entry.path().string(), tex)) continue;
            const GLuint gl = renderer_->texture_for("sensei_portrait", tex);
            std::fprintf(stdout, "[app] sensei portrait: %s %dx%d (tex %u)\n",
                         name.c_str(), tex.w, tex.h, gl);
            std::fflush(stdout);
            break;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "app: sensei portrait load failed: %s\n", e.what());
    }

    screens_ = std::make_unique<ScreenManager>(*this);

    // The shared fight assets (models/moves/clips/tactics/location). Built
    // once; the Shop/Equipment screens rebuild the merged model on equip.
    try {
        fight_assets_ = std::make_unique<FightAssets>();
        // JS `v.wya` (`internal_settings.xml` `<PivotNode Name>`, parse L1155,
        // default "NPivot"): the bone every fighter is anchored on
        // (`Dl.jX`/`Dl.Trb`/`Dl.oL` L577). Wire the shipped value into the
        // scene anchor before any Fighter samples (dojo figures are created
        // lazily on first render, the fight later).
        try {
            const std::string settings_xml = extracted_xml("internal_settings.xml");
            sf2::data::xml_doc doc;
            doc.parse(reinterpret_cast<const std::uint8_t*>(settings_xml.data()),
                      settings_xml.size());
            const pugi::xml_node root = doc.root().first_child();
            if (root) {
                const pugi::xml_node pivot = root.child("PivotNode");
                if (pivot && pivot.attribute("Name")) {
                    sf2::scene::set_fighter_pivot_bone(pivot.attribute("Name").value());
                }
            }
        } catch (const std::exception&) {
            // Config absent: keep the shipped default "NPivot".
        }
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
        // Retain the models archive + seed the part cache (no re-parse) so a
        // fight can build each warrior's OWN equipment model (JS `xc.cM`
        // L809-810 -> `Yc.load` L568); see FightAssets::merge_names.
        fight_assets_->model_archive = models;
        fight_assets_->model_cache.emplace("mdl_skeleton", fight_assets_->skeleton);
        fight_assets_->model_cache.emplace("mdl_body", fight_assets_->body);
        fight_assets_->model_cache.emplace("mdl_head", fight_assets_->head);
        fight_assets_->rebuild_merged();

        // The Punchbag dummy merge (hub display only — see FightAssets).
        // Inner-guarded so a missing bag entry can never endanger the base
        // fight assets above (the outer catch would reset everything).
        try {
            fight_assets_->bag_skeleton = load_model("mdl_skeleton_punching_bag");
            fight_assets_->bag_body = load_model("mdl_punching_bag");
            fight_assets_->merged_bag = sf2::scene::build_fighter_model(
                {fight_assets_->bag_skeleton, fight_assets_->bag_body});
            fight_assets_->model_cache.emplace("mdl_skeleton_punching_bag",
                                               fight_assets_->bag_skeleton);
            fight_assets_->model_cache.emplace("mdl_punching_bag",
                                               fight_assets_->bag_body);
            // The bag's COM (== the top-mount Node12, bind Y=+335) is 226
            // units above its NPivot (Y=+109). `Fighter::sample` now anchors
            // on the model's PivotNode bone (Wave U general fix), so the bag
            // lands on NPivot directly and NO bag-scoped COM re-point is
            // needed (the old patch was a no-op once the general fix landed).
            std::fprintf(stdout, "[assets] punchbag dummy bones=%zu tris=%zu capsules=%zu\n",
                         fight_assets_->merged_bag.bones.size(),
                         fight_assets_->merged_bag.resolved_tris.size(),
                         fight_assets_->merged_bag.capsules.size());
            std::fflush(stdout);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[assets] punchbag dummy skipped: %s\n", e.what());
        }

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

        // Perk catalog (res/perks.xml `Be` defs) for the fight trigger bus.
        try {
            fight_assets_->perk_catalog =
                sf2::scene::parse_perks_xml(extracted_xml("perks.xml"));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "app: perks.xml load failed: %s\n", e.what());
        }

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

// Boot overlay length: JS `Rg` (Preloader, L1967) runs 0-95%, then `ad`
// (Loader, L1969) holds "Loading 100%" until `aHa>30` frames. The native
// shell shows the same art for a fixed number of 1/60 steps (asset loading
// itself is synchronous in `init`).
constexpr int kBootSplashFrames = 75;
constexpr int kBootLoaderFrames = 30;

void App::boot() {
    // Boot to the Dojo home screen (screen 3) — the ORIGINAL starts in the
    // Dojo (the JS flow: Preloader(0) -> Loader(2) -> Dojo(3)), NOT in the
    // GeneralMenu (screen 8). The shell draws the Preloader/Loader art as a
    // boot overlay (JS `Rg` L1967 / `ad` L1969, PORT_AUDIT_UI §4.12); the
    // Dojo is pushed immediately so screen ids/inputs stay deterministic,
    // and the overlay is draw-only (skipped when headless).
    std::fprintf(stdout, "[screen] boot: Preloader(0) -> Loader(2) -> Dojo(3)\n");
    std::fflush(stdout);
    boot_splash_total_ = kBootSplashFrames;
    boot_splash_frames_ = kBootSplashFrames;
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
    // screen (JS `Ik` keydown/keyup -> the fight input path). Each physical
    // GLFW key keeps its OWN held edge: binding two keys to one action must
    // not make them share a flag (that emitted a phantom release of the
    // partner every frame while either was held — see `fight_keys_down_`).
    //
    // The polled set must deliver EVERY key the JS key map can produce plus
    // the shell's non-fight keys (JS `Gz` L24 builds `Os.v` with Space/
    // Escape/Enter/arrows; `Za.bbb` L456 wires the keydown/keyup). The old
    // set omitted Escape(256)/P(80)/Enter(257)/O(79)/Q(81) and carried a
    // non-JS `B` — a pressed key that is never polled delivers no edge at
    // all. The JS fight controls are `sc.OD` (`Af.oUa` L2472): A/S/D/W +
    // K/L/O/P/J/Q (no B).
    static const int kFightKeys[] = {
        GLFW_KEY_ESCAPE, GLFW_KEY_ENTER,
        GLFW_KEY_A, GLFW_KEY_LEFT, GLFW_KEY_D, GLFW_KEY_RIGHT,
        GLFW_KEY_W, GLFW_KEY_UP, GLFW_KEY_S, GLFW_KEY_DOWN,
        GLFW_KEY_SPACE, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
        GLFW_KEY_O, GLFW_KEY_P, GLFW_KEY_Q,
    };
    for (const int glfw_key : kFightKeys) {
        const bool now_down = glfwGetKey(renderer_->window(), glfw_key) == GLFW_PRESS;
        const bool was_down = fight_keys_down_.count(glfw_key) != 0;
        if (now_down && !was_down) {
            fight_keys_down_.insert(glfw_key);
            if (screens_ != nullptr && screens_->top() != nullptr) {
                screens_->top()->on_key(glfw_key, true);
            }
        } else if (!now_down && was_down) {
            fight_keys_down_.erase(glfw_key);
            if (screens_ != nullptr && screens_->top() != nullptr) {
                screens_->top()->on_key(glfw_key, false);
            }
        }
    }
}

void App::update_fixed(float dt) {
    // Boot overlay countdown (JS `Rg`/`ad`), one step per fixed update.
    if (boot_splash_frames_ > 0) {
        --boot_splash_frames_;
    }
    screens_->update(dt);
}

void App::render_frame() {
    sf2::render::Camera camera;
    camera.view_w = static_cast<float>(view_w_);
    camera.view_h = static_cast<float>(view_h_);
    renderer_->begin_frame(camera);
    screens_->render(*this);
    // Boot overlay (JS `Rg`/`ad`). Headless runs skip the draw so captures
    // and goldens stay byte-stable (the countdown still runs).
    if (boot_splash_frames_ > 0 && headless_frames_ == 0) {
        draw_boot_splash();
    }
    renderer_->end_frame();
}

void App::draw_boot_splash() {
    // JS `Rg`/`Tk` (L1967, L87-90) Preloader -> `ad` (L1969) Loader.
    // `Tk` layout (cast id 278, scroll id 274) + the localized
    // `splash/loading{lang}` BMF (ids 276/277, `ea` L87-88) with the UTF-8
    // glyph path are implemented below. Still OPEN (PORT_AUDIT_UI §4.12):
    //   - `Rg.Ea` state-3 module passes (L1967: `Ev` + `ap`/`cp`/`$o`/`bp`/
    //     `dp`, defs L1160-1164). They are async app-startup side effects
    //     (version, quest login dialogs, reload) whose `Ev.x$a()` progress
    //     (L1163 = round(PZ/N*100)) feeds `gMa(x,95,100)`. Native init is
    //     synchronous, so there is no per-module frame boundary to derive the
    //     95->100 ramp from — the passes are performed inside `init`/`boot`.
    //   - the `ad` view `tr` art (id 816/817, L1867-1868) IS drawn below
    //     (loader branch); the Preloader `Tk` branch is the `!loader` path.
    const bool loader = boot_splash_frames_ <= kBootLoaderFrames;
    sf2::render::Camera ui_cam;
    ui_cam.center_x = static_cast<float>(view_w_) * 0.5f;
    ui_cam.center_y = static_cast<float>(view_h_) * 0.5f;
    ui_cam.zoom = 1.0f;
    ui_cam.view_w = static_cast<float>(view_w_);
    ui_cam.view_h = static_cast<float>(view_h_);
    ui_cam.arena_h = ui_cam.view_h;
    ui_cam.arena_floor = 0.0f;
    ui_cam.arena_center_x = ui_cam.center_x;

    // JS `ad` clears the frame to black (L1969: `Ha.yT(new H(0,0,0,1))`);
    // the Preloader draws its splash over the same black base. This also
    // hides the Dojo that is already pushed underneath the overlay.
    {
        sf2::scene::Sprite cover;
        cover.solid = true;
        cover.color_r = 0.0f;
        cover.color_g = 0.0f;
        cover.color_b = 0.0f;
        cover.color_a = 1.0f;
        cover.frame_w = static_cast<float>(view_w_);
        cover.frame_h = static_cast<float>(view_h_);
        cover.transform.set_pos(ui_cam.center_x, ui_cam.center_y);
        renderer_->draw_sprite(cover, ui_cam);
    }

    // `Tk` (L87-90) splash layout. `gG`=E.get(279) is the full-screen fill
    // (already covered by the black base above); `mG`=E.get(278) cast,
    // `ly`=E.get(275) logo, `qe`=E.get(274) scroll, `Jo`=E.get(276) text.
    // `Jo` position/scale is computed here and used by the text draw below.
    float jo_cx = ui_cam.center_x;
    float jo_cy = ui_cam.center_y + static_cast<float>(view_h_) * 0.25f;
    float jo_scale = 1.0f;
    const float W = static_cast<float>(view_w_);
    const float H = static_cast<float>(view_h_);
    const float lc = W / H;  // N.lc
    // Centred textured-quad helper. `flip_x` mirrors the quad (negative
    // scale on the render quad — the same `scale_x = -scale_x` idiom the
    // location renderer uses for `Hr()`-flipped images).
    const auto draw_tex = [&](const char* alias, unsigned int tex, int tw, int th, float cxp,
                              float cyp, float dw, float dh, bool flip_x = false) {
        if (tex == 0 || tw <= 0 || th <= 0) return;
        sf2::scene::Sprite s;
        s.texture_name = alias;
        s.frame_x = 0.0f;
        s.frame_y = 0.0f;
        s.frame_w = static_cast<float>(tw);
        s.frame_h = static_cast<float>(th);
        s.tex_w = static_cast<float>(tw);
        s.tex_h = static_cast<float>(th);
        s.solid = false;
        s.transform.set_pos(cxp, cyp);
        s.transform.set_scale((flip_x ? -1.0f : 1.0f) * dw / static_cast<float>(tw),
                              dh / static_cast<float>(th));
        renderer_->draw_sprite(s, ui_cam);
    };
    if (!loader) {
        // `gG` (E.get(279)): the full-screen fill, stretched over the view.
        draw_tex("splash_bg", splash_bg_tex_, splash_bg_w_, splash_bg_h_, ui_cam.center_x,
                 ui_cam.center_y, static_cast<float>(view_w_), static_cast<float>(view_h_));
        // `ly` (logo) L89: width `min(W,H)*fac` (`c<1 ? .9+... : .7-.2*(lc1-1)`),
        // top-centre `C(W/2)`, `D(ly.qa()*.25)` (`c<.6` portrait branch).
        const float lc_hi = std::clamp(lc, 1.0f, 2.0f);
        const float ly_fac =
            lc < 1.0f ? 0.9f + (std::clamp(lc, 0.5f, 1.0f) - 0.5f) / 0.5f * -0.2f
                      : 0.7f - 0.2f * (lc_hi - 1.0f);
        if (splash_logo_tex_ != 0 && splash_logo_w_ > 0) {
            const float lw = std::min(W, H) * ly_fac;
            const float lh = lw * static_cast<float>(splash_logo_h_) /
                             static_cast<float>(splash_logo_w_);
            float ly_top = lh * 0.25f;
            if (lc < 0.6f) {
                const float lc_lo = std::clamp(lc, 0.5f, 0.6f);
                ly_top = lh + (lc_lo - 0.5f) / 0.1f * (0.0f - lh);
            }
            draw_tex("splash_logo", splash_logo_tex_, splash_logo_w_, splash_logo_h_,
                     W * 0.5f, ly_top + lh * 0.5f, lw, lh);
        }
        // `mG` (cast) L89: width `W*fac`, `D(H)` bottom, `C(W/2)` (anchor
        // bottom-centre, `ik(.5,1)`/`Rn(.5,1)` L87).
        const float cast_fac =
            lc < 1.0f ? 2.2f + (std::clamp(lc, 0.5f, 1.0f) - 0.5f) / 0.5f * -1.2f
                      : 1.0f - 0.5f * (lc_hi - 1.0f);
        const float cast_w = W * cast_fac;
        const float cast_h =
            splash_cast_w_ > 0
                ? cast_w * static_cast<float>(splash_cast_h_) / static_cast<float>(splash_cast_w_)
                : 0.0f;
        draw_tex("splash_cast", splash_cast_tex_, splash_cast_w_, splash_cast_h_, W * 0.5f,
                 H - cast_h * 0.5f, cast_w, cast_h);
        // `qe` (scroll) L89-90: `C(W/2)`, `D(mG.ra)` (cast bottom), width
        // `c<1 ? W*.5 : H*.4`, then shifted up by `H*.15`.
        const float scroll_w = lc < 1.0f ? W * 0.5f : H * 0.4f;
        const float scroll_h =
            splash_scroll_w_ > 0 ? scroll_w * static_cast<float>(splash_scroll_h_) /
                                       static_cast<float>(splash_scroll_w_)
                                 : 0.0f;
        const float scroll_top = H - H * 0.15f;
        draw_tex("splash_scroll", splash_scroll_tex_, splash_scroll_w_, splash_scroll_h_,
                 W * 0.5f, scroll_top + scroll_h * 0.5f, scroll_w, scroll_h);
        // `Jo` (text) L90: `Fa(qe.za()*.75, qe.qa())`, `ua(a*.4)`, `C(qe.ya)`
        // (scroll left), `D(qe.ra)` (scroll bottom), `Ia(128)` centre.
        if (splash_loading_font_ != nullptr && scroll_h > 0.0f) {
            const int eF = splash_loading_font_->size > 0 ? splash_loading_font_->size : 100;
            // JS `Tk.aa` L90 `Jo.ua(qe.qa()*.4)` -> `ea.ua` (L1711) multiplies
            // by `ea.a1` (ja/ko/ru 0.8, else 1 — L65/L1931/L2484); `Qh.print`
            // L1631 divides by `charset.eF`.
            jo_scale = (scroll_h * 0.4f * ui_text_scale()) / static_cast<float>(eF);
            jo_cx = (W * 0.5f - scroll_w * 0.5f) + scroll_w * 0.75f * 0.5f;
            jo_cy = scroll_top + scroll_h;
        }
    }

    if (loader) {
        // JS `ad` (Loader, L1969) renders through its view class `tr`
        // (L1867-1868) — a DIFFERENT view from the Preloader's `Tk`. `ef`
        // (L1867) draws a black base + `Qa` (two tiled `E.get(817)` =
        // `loader/bg.jpg`, the second `Hr(true)` mirrored); `tr` adds
        // `pE = E.get(816)` = `loader/logo.png` and the `info` text node.
        // `ad.Ea` (L1969) writes literally "Loading 100%".
        //
        // `ef.layout` (L1867): `node.la(min(1024, N.Eha) / (oea()*1.1))`, the
        // node centred on the safe rect (`N.rect` centre = W/2, H/2); `oea()`
        // = `tr.pE.fa.x` (the logo source width), `N.Eha = min(W, H)`.
        // `tr.aa` (L1868): `a = (1.2 + ((clamp(lc,.4,1)-.4)/.6*-1)) * pE.fa.y`
        // shifts BOTH `pE` and `Qa` up by `a` node-local (`pE.D(-a)`,
        // `Qa.D(-a)`).
        const float logo_sw =
            loader_logo_w_ > 0 ? static_cast<float>(loader_logo_w_) : 1024.0f;
        const float logo_sh =
            loader_logo_h_ > 0 ? static_cast<float>(loader_logo_h_) : 300.0f;
        const float node_scale = std::min(1024.0f, std::min(W, H)) / (logo_sw * 1.1f);
        const float a_off =
            (1.2f + ((std::clamp(lc, 0.4f, 1.0f) - 0.4f) / 0.6f) * -1.0f) * logo_sh;
        const float node_cx = W * 0.5f;
        const float node_cy = H * 0.5f;
        const float art_cy = node_cy - a_off * node_scale;
        // `ef.layout`: `Qa.Rh(min(2, W/oYa/node.Eb))` = x scale, and
        // `Qa.mj(b + (1-lc)*.75)` = y scale (b = W/oYa/node.Eb); `oYa` = the
        // pair width = 2 * the bg source width.
        const float oYa =
            (loader_bg_w_ > 0 ? static_cast<float>(loader_bg_w_) : 512.0f) * 2.0f;
        const float b_rep = node_scale > 0.0f ? W / oYa / node_scale : 1.0f;
        const float qa_sx = std::min(2.0f, b_rep);
        const float qa_sy = b_rep + (1.0f - lc) * 0.75f;
        if (loader_bg_tex_ != 0 && loader_bg_w_ > 0 && loader_bg_h_ > 0) {
            const float dw = static_cast<float>(loader_bg_w_) * qa_sx * node_scale;
            const float dh = static_cast<float>(loader_bg_h_) * qa_sy * node_scale;
            // Left half = `a` (normal); right half = `b` (`Hr(true)` mirror).
            draw_tex("loader_bg", loader_bg_tex_, loader_bg_w_, loader_bg_h_,
                     node_cx - dw * 0.5f, art_cy, dw, dh, false);
            draw_tex("loader_bg", loader_bg_tex_, loader_bg_w_, loader_bg_h_,
                     node_cx + dw * 0.5f, art_cy, dw, dh, true);
        }
        if (loader_logo_tex_ != 0 && loader_logo_w_ > 0 && loader_logo_h_ > 0) {
            draw_tex("loader_logo", loader_logo_tex_, loader_logo_w_, loader_logo_h_, node_cx,
                     art_cy, static_cast<float>(loader_logo_w_) * node_scale,
                     static_cast<float>(loader_logo_h_) * node_scale, false);
        }
        // `tr.info` (L1867-1868): font `E.Na()` = `E.get(264,16)` =
        // `ui/font{lang}` (the port's menu font), colour `Na.cd(13743222)` =
        // (209,182,118), centred at `pE.ra + pE.qa()*.75` (node-local, scaled
        // by the parent node). The whole node scale applies.
        if (menu_font_ != nullptr && font_tex_ != 0) {
            const std::string text = "Loading 100%";
            const float info_cy = node_cy + (-a_off + logo_sh * 0.75f) * node_scale;
            const float scale = node_scale;
            const float tw = measure_text(*menu_font_, text, scale);
            const float th = sf2::data::measure_text_height_utf8(*menu_font_, text, scale);
            draw_text_with_font(*menu_font_, font_tex_, node_cx - tw * 0.5f,
                                info_cy - th * 0.5f, text, scale, 209.0f / 255.0f,
                                182.0f / 255.0f, 118.0f / 255.0f);
        }
        return;
    }
    if (splash_loading_font_ == nullptr || splash_loading_tex_ == 0) {
        return;
    }
    char buf[64];
    {
        // JS `Tk.n5` (L88): "<word> <n>%" mapped 0..95.
        const int pre_span = boot_splash_total_ - kBootLoaderFrames;
        const int elapsed = pre_span - boot_splash_frames_;
        const int pct = pre_span > 0 ? (elapsed * 95) / pre_span : 95;
        std::snprintf(buf, sizeof(buf), "%s %d%%", loading_word(lang_), pct);
    }
    // `Jo` (L90): `ua(qe.qa()*.4)` centred on the scroll node.
    const float w = measure_text(*splash_loading_font_, buf, jo_scale);
    const float lh = std::max(
        1.0f, static_cast<float>(splash_loading_font_->line_height) * jo_scale);
    draw_text_with_font(*splash_loading_font_, splash_loading_tex_, jo_cx - w * 0.5f,
                        jo_cy - lh * 0.5f, buf, jo_scale, 1.0f, 1.0f, 1.0f);
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
        // A real pointer press edge is latched for THIS present frame only
        // (`poll_input` clears `pointer_.pressed` on the next frame), while
        // the pointer consumers (the on-screen gamepad `update_gamepad_input`
        // and every screen's `update_impl`) sample it at the fixed 60 Hz step.
        // With vsync off (`glfwSwapInterval(0)`) the present rate is far above
        // 60 Hz, so most fixed ticks have no accumulator credit and the click
        // edge is dropped before any consumer sees it (measured: 5/60 clicks).
        // Run one fixed step on the edge frame so the JS `Hk` mouse-event
        // dispatch is never lost (JS: `Hk` mousedown -> `DGa` -> handlers).
        if (pointer_.pressed && acc_ < kFixedDt) acc_ = kFixedDt;
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
    splash_loading_font_.reset();
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
    // Rotated packing (JS le.frame.dL, bk L1765 / Cq L1561): the renderer
    // un-rotates UVs; quad keeps stored dims (no w/h swap — TLa keeps Nc).
    s.rotated = fr.rotated;
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
    // Rotated packing (JS le.frame.dL, bk L1765 / Cq L1561): the renderer
    // un-rotates UVs; quad keeps stored dims (no w/h swap — TLa keeps Nc).
    s.rotated = fr.rotated;
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
    // UTF-8 aware: .fnt glyph ids are Unicode codepoints (see font.hpp), so
    // decode before lookup. ASCII stays byte-identical.
    return sf2::data::measure_text_utf8(font, text, scale);
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
    else if (tex == splash_loading_tex_) tex_name = "splash_loading";
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
    // UTF-8 aware: decode each codepoint, then look the glyph up by id (the
    // .fnt id is a Unicode codepoint; see font.hpp). ASCII stays identical.
    for (std::size_t i = 0; i < text.size();) {
        const std::uint32_t id = sf2::data::utf8_next(text, i);
        const sf2::data::font_char* glyph = sf2::data::find_glyph(font, id);
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

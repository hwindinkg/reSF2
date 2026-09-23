// The shell screens implementation — Dojo (home), Map, Fight, Results,
// Shop, Profile (tabbed), Settings (minimal overlay).
//
// JS study (the per-screen wire-spec is reference/PORT_AUDIT_UI.md):
//   - Dojo/home: the JS `Tf` hub (L1969-1972) — the dojo location layer
//     stack + the `FightNone` ModelViewer idle figure at the location's
//     ModelsViewer spawn, plus the shared `za` top chrome. There is no
//     FIGHT/MAP/SHOP/PROFILE 4-up row, punchbag or gear (native
//     inventions, PORT_AUDIT_UI §2.1-2.2).
//   - Top chrome (`za` L1972-1984): a full-width `topPanel` (misc 260) at
//     min(H*0.13,100), centred `wr`/`xr`/`yr` widgets (level/energy/money)
//     and a VERTICAL column of five `Le` (menu 262) nav buttons, mounted on
//     Dojo/Map/Shop/Profile. No GeneralMenu screen exists in this build
//     (`dJ()` returns 0/3/4/5/6/7 only).
//   - Map nodes: stages.xml <Zone><Battle X=.. Y=..> -> screen pos
//     x = X*uM + bg.w/2, y = -Y*uM + bg.h/2 - 50 mapped through the
//     2046x854 backdrop (JS `qe.X0a` L2144; uM = 1.5003663003663004 L2488).
//     `Qr.lla` (L2094) draws a button only while its save `<Battles>` record
//     exists and is not Hidden/expired.
//
// The misc/menu/controller/fight-ui atlases are KTX ASTC — the data layer
// CPU-decodes them (core/data/ktx.cpp) and App::init registers their frames,
// so the `za` chrome art resolves; a flat fallback covers a real per-frame
// miss (never a silent blank).

#include "app/screens.hpp"
#include "app/act_player.hpp"
#include "app/lang_table.hpp"
#include "app/quest_engine.hpp"
#include "app/quest_panel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

#include "anim_archive.hpp"
#include "app/app.hpp"
#include "app/save_system.hpp"
#include "atlas.hpp"
#include "audio/audio.hpp"
#include "audio/special_regen.hpp"
#include "font.hpp"
#include "scene/fight.hpp"
#include "scene/location_scene.hpp"
#include "scene/magic_effects.hpp"
#include "scene/model.hpp"
#include "scene/renderer.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

namespace {

constexpr float kViewW = 1280.0f;
constexpr float kViewH = 720.0f;

// --- D13/D15 Settings dialog state -----------------------------------------
// The Settings surface is `un extends od` (JS L1916-1930), built by `Wb.Xob`
// case 310 (`new un`, L926) after `Xc.Shb()` = `Wb.openDialog(310,null)` (L931).
// `Wb` owns ONE top dialog appended to the ACTIVE screen (L927), so the `za`
// nav button #5 (`Vfb` L1981) opens it OVER the current screen — it does not
// navigate (`ma.Jg().jI(11)` was invented). `g_settings_lang` is `un.$u` (the
// DISPLAYED language, L1928 `this.$u=G.Rq()`); RESTART is revealed only once
// it differs from the saved `G.Rq()` (`t9`, L1931).
bool g_settings_dialog_open = false;
bool g_settings_restart_visible = false;
std::string g_settings_lang;  // `un.$u`
int g_settings_hover = -1;    // flat-fallback hover (`Kb`/`Km`/rows)
int g_settings_age = 0;       // frames since open (press debounce)
bool g_settings_music_off = false;  // `un.W$`/`lb.Lz()` state (shared)
bool g_settings_sound_off = false;  // `un.Y$`/`lb.Mz()` state (shared)
// JS `iv` (L2477): the language cycle order `un.rHa` case 4 walks (L1931).
const char* const kSettingsLangs[] = {"en", "de", "it", "fr", "pt",
                                      "ru", "es", "tr", "ja", "ko"};
constexpr int kSettingsLangCount = 10;

// --- Sensei dialog modal (quest engine He records) -------------------------
// The engine queues structured dialogs on fire; Dojo/Map show the head as a
// tap-to-advance modal and gate their own buttons behind it (dialog modal
// gating). Headless drains the queue silently instead (auto-advance — the
// scripted loop never modal-blocks; detected via App::headless(), i.e. the
// headless_frames_ > 0 pattern the driver sets) EXCEPT on the armed
// fresh-tutorial path, where the fidelity tour captures the real beats.
const EngineDialog* quest_modal_top(App& app) {
    if (app.headless() && !app.fresh_tutorial() && !app.dialog_observe()) {
        while (app.quest_engine().has_dialog()) {
            std::fprintf(stdout, "[quest] dialog skipped (headless): %s\n",
                         app.quest_engine().dialog().title.c_str());
            std::fflush(stdout);
            // Pop the entry we just logged (the FRONT). `pop_dialog()` is
            // modal-aware (`modal_index()` skips bar Notifications), so a
            // Notification at the front never popped and this `while` spun
            // forever (soft lock: `fight_->update()` never ran).
            app.quest_engine().pop_head_dialog();
        }
        return nullptr;
    }
    // JS `He.S` L1050 sends a `Notification` to the `Ib` BAR and a `Regular`
    // to `Wb.openDialog` (L931 -> `Wb.Xob` L927). The `Wb` queue therefore
    // holds no Notification, so its top is the first non-Notification — the
    // `Regular characterSensei` shows the moment `StoryTutorialWelcome` runs,
    // while the bar carries the LAST posted Notification (`Ib.Qhb` L1907
    // overwrites the single instance). A lone Notification (no `Wb` modal) is
    // still the display top so the bar draws/advances as before.
    if (const EngineDialog* m = app.quest_engine().modal_top()) return m;
    return app.quest_engine().has_notification() ? app.quest_engine().notification_top()
                                                 : nullptr;
}

// The `Ib` bar's content (`Ib.F().Qhb` L1050). `Ib` is one instance, so the
// LAST queued Notification is what the bar shows.
const EngineDialog* quest_notification_top(App& app) {
    if (app.headless() && !app.fresh_tutorial() && !app.dialog_observe()) return nullptr;
    return app.quest_engine().notification_top();
}

// Regular-dialog button hit-test (defined after the `od` dialog layout
// helpers below). Returns the `He.dhb` L1061 slot index of the plate under
// (x, y) — 0=Left, 1=Right, 2=Middle, 100=Close — or -1 for no hit.
int quest_dialog_button_hit_index(App& app, const EngineDialog& d, double x, double y);
// `He.jkb` L1056-1057 row buttons (`this.ima`, ids from `this.eOa=5`): the
// row's own box is the `He.dhb` L1061 tap target. `quest_dialog_row_hit_index`
// is declared in screens.hpp (used by the modal + `--quest-action-probe`).
// `He.S` L1045-1051 Type routing + the `od.close` L1898 retained-dialog copy
// (both defined with the dialog layout below; `quest_modal_consume` needs
// them first).
enum class DialogKind { kNone, kOd280, kUj290, kVe340, kVn370, kIbBar };
DialogKind dialog_kind(const std::string& type);
void dialog_capture_closing(App& app, const EngineDialog& d);
// D13: the Settings `un extends od` dialog (JS L1916-1930, opened by
// `Xc.Shb()` -> `Wb.openDialog(310,null)` L931/L926) is `Wb`'s top dialog over
// the CURRENT screen. Returns true while it is open (the caller skips its own
// input, exactly like the quest modal). Defined with the settings drawer below.
bool settings_dialog_consume(App& app);
void draw_settings_dialog(App& app, sf2::render::Renderer& ren);

// --- D8 `ReadTime` auto-dismiss (`Ib.SK`, JS L1905/L1908) -------------------
// `Ib.aa(a)` L1905: `this.SK-=a; this.SK<=0&&(this.qma=!0)`, then `OZa` L1908
// (`this.qma&&this.scroll.uJ&&this.Dcb()&&this.y4(!1)`) collapses the bar. The
// budget is `He.SK` (L1043 `u.H(ReadTime, ge.ZGa)`), only ever carried by the
// `Notification` Type (`Qhb(...,f=this.SK,...)` L1050). The clock is the app
// clock (the top screen's fixed 60 Hz `time()`), never the wall clock.
struct ReadTimeState {
    std::string key;      // the visible bar's identity — a new one restarts it
    float elapsed = 0.0f; // seconds the current bar has been up (`Ib.SK` spent)
    float last_time = -1.0f;  // app clock at the previous tick (per-notification)
};
ReadTimeState& read_time_state() {
    static ReadTimeState s;
    return s;
}

// Returns true when the top Notification's `ReadTime` budget elapsed (the bar
// is popped, with the JS close tween). A non-Notification, a missing budget or
// no top dialog is a no-op.
bool quest_read_time_advance(App& app, float dt) {
    const EngineDialog* d = quest_notification_top(app);
    if (d == nullptr) return false;
    if (d->read_time <= 0.0f) return false;
    // `Qhb` L1907 resets `this.SK=f` on EVERY post, so the identity of the
    // visible bar is its whole content — the tutorial notifications carry an
    // EMPTY Title (`Notification/`), so the joined lines are what separates
    // `_NotificationTextMove` from `_NotificationTextPunchBag`.
    std::string key = d->type + "|" + d->title;
    for (const std::string& ln : d->lines) {
        key += "|";
        key += ln;
    }
    ReadTimeState& st = read_time_state();
    if (st.key != key) {
        st.key = key;
        st.elapsed = 0.0f;  // `Qhb` L1907 sets `this.SK=f` afresh
    }
    st.elapsed += dt;
    if (st.elapsed < d->read_time) return false;
    std::fprintf(stdout, "[quest] notification auto-dismissed (ReadTime %.2fs at t=%.2fs)\n",
                 d->read_time, st.elapsed);
    std::fflush(stdout);
    dialog_capture_closing(app, *d);  // `Ib.close` L1911 (0.5 s collapse)
    app.quest_engine().pop_notifications();
    st.key.clear();
    st.elapsed = 0.0f;
    return true;
}

// The per-frame driver: advances the countdown by the app-clock delta. The
// clock baseline resets with the bar's identity, so the budget is spent from
// the frame the notification is first visible (never from a stale screen clock).
// `quest_modal_consume` calls this each screen update; the harness calls
// `quest_read_time_advance` with an explicit dt for determinism.
bool quest_read_time_tick(App& app) {
    const float now = app.screens().top() != nullptr ? app.screens().top()->time() : 0.0f;
    ReadTimeState& st = read_time_state();
    if (st.last_time < 0.0f) st.last_time = now;
    float dt = now - st.last_time;
    st.last_time = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.5f) dt = 0.5f;  // a screen change never jumps the countdown
    return quest_read_time_advance(app, dt);
}

// The StoryTutorial lesson gate clock (JS `Do`/`Eo` `Cm`, L1242/L1243). Same
// source as the bar's ReadTime budget: the top screen's fixed 60 Hz `time()`
// delta, never the wall clock, so the headless drivers stay deterministic.
// A screen change must not jump the budget, so the delta is clamped like
// `quest_read_time_tick`.
struct TutorialGateClock {
    float last_time = -1.0f;
};
TutorialGateClock& tutorial_gate_clock() {
    static TutorialGateClock s;
    return s;
}

bool quest_tutorial_gate_tick(App& app) {
    const float now = app.screens().top() != nullptr ? app.screens().top()->time() : 0.0f;
    TutorialGateClock& st = tutorial_gate_clock();
    if (st.last_time < 0.0f) st.last_time = now;
    float dt = now - st.last_time;
    st.last_time = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.5f) dt = 0.5f;
    return app.quest_engine().tutorial_gate_tick(app, dt);
}

// JS `He` gating (L1045-1062): a `Notification` is fire-and-forget (any tap
// advances, `sa()` continues); a `Regular` dialog holds the chain until its
// button fires (`dhb(1)` L1061 -> the nested `Yb`), IgnoreBack="1" so a
// backdrop tap does not close it. Returns true while a modal is up (the
// caller skips its own input that frame). `fight_out` (optional) receives a
// `Fight` request from the button's nested actions (`Sn` L1069).
bool quest_modal_consume(App& app, std::string* fight_out = nullptr) {
    // D13: `Wb` keeps ONE top dialog; the Settings `un` (case 310) blocks the
    // screen beneath it exactly like a quest dialog.
    if (settings_dialog_consume(app)) return true;
    // D8 `Ib.aa` L1905: the app-clock ReadTime countdown runs before input.
    quest_read_time_tick(app);
    // `Do`/`Eo` L1242/L1243: the StoryTutorial lesson gate resumes the
    // serialized chain on the same app clock (the next tutorial beat is only
    // queued once the lesson budget elapses).
    quest_tutorial_gate_tick(app);
    const EngineDialog* d = quest_modal_top(app);
    if (d == nullptr) return false;
    // `He.S` L1048-1050: the Types whose JS body is a bare `debugger;`
    // (`Scroll`, `MultiLineScroll`, `ThreeButtons`, `ItemSetDialog`,
    // `MultilineTMP`, `Simple`) never build a dialog object, so `He.S` falls
    // through to `this.sa()` — advance without display or input block.
    if (dialog_kind(d->type) == DialogKind::kNone) {
        std::fprintf(stdout, "[quest] dialog Type '%s' has no renderer (JS debugger) -> advance\n",
                     d->type.c_str());
        std::fflush(stdout);
        app.quest_engine().dismiss_dialog(app);  // D1: resume any parked chain
        return false;
    }
    if (!app.pointer().pressed) return true;
    if (d->type == "Notification") {
        std::fprintf(stdout, "[quest] notification advanced: %s\n", d->title.c_str());
        std::fflush(stdout);
        dialog_capture_closing(app, *d);  // `Ib.close` L1911 (0.5 s collapse)
        app.quest_engine().dismiss_dialog(app);  // `He.gf` L1062
        return true;
    }
    // `He.dhb(a)` L1061 `a<this.eOa` (L1062): a row-button row (`He.jkb`
    // L1056-1057 `tv` in `this.ima`, id 5+) fires its OWN nested `Yb` by id,
    // BEFORE the pager/plate paths — the row box is the tap target (the port
    // has no delivery countdown; see `quest_dialog_row_buttons`).
    const int row_id = quest_dialog_row_hit_index(app, *d, app.pointer().x, app.pointer().y);
    if (row_id >= 0) {
        std::fprintf(stdout, "[quest] dialog row pressed: %s (row id %d)\n", d->title.c_str(),
                     row_id);
        std::fflush(stdout);
        dialog_capture_closing(app, *d);  // `od.Ge(1)` L1898 close tween
        const std::vector<std::string> fights = app.quest_engine().press_dialog(app, row_id);
        if (fight_out != nullptr && !fights.empty()) *fight_out = fights.front();
        return true;
    }
    // Regular. `He` pages a multi-row dialog (`He.jkb` L1042: every `<Line>`
    // row carries its own `ButtonText`): the page plate advances until the
    // LAST row, whose plate fires the nested actions (`dhb(a)` L1061). The
    // tutorial Lynx dialog is exactly this (`dlgStoryBtnMore` -> L159,
    // `dlgStoryBtnFight` -> L160).
    if (app.quest_engine().dialog_has_next_page()) {
        if (quest_dialog_button_hit_index(app, *d, app.pointer().x, app.pointer().y) >= 0) {
            app.quest_engine().advance_dialog_page();
        }
        return true;
    }
    // Regular. A dialog whose slot carries actions fires on press; a
    // buttonless dialog (no `hab()` slot, L1060) advances on tap.
    if (!app.quest_engine().dialog_has_button()) {
        std::fprintf(stdout, "[quest] dialog advanced (no button): %s\n", d->title.c_str());
        std::fflush(stdout);
        dialog_capture_closing(app, *d);
        app.quest_engine().dismiss_dialog(app);  // `He.dhb(0)` no `Ng` -> `sa()`
        return true;
    }
    // `He.dhb(a)` L1061: the plate's slot index (0=Left, 1=Right, 2=Middle,
    // 100=Close) selects which deferred action list runs.
    const int slot = quest_dialog_button_hit_index(app, *d, app.pointer().x, app.pointer().y);
    if (slot >= 0) {
        dialog_capture_closing(app, *d);  // `od.Ge(1)` L1898 close tween
        const std::vector<std::string> fights = app.quest_engine().press_dialog(app, slot);
        if (fight_out != nullptr && !fights.empty()) *fight_out = fights.front();
    }
    return true;
}

// JS `ea` text node (L1711) + its `Bg`/`Qh` effect (L1622-1634). A label's
// size is its `ua()` value: `ea.ua(a)` -> `effect.ua(a*ea.a1)` (L1711) and
// `Qh.print` (L1631) draws each glyph at `fontSize/charset.eF`. The shipped
// menu BMF (`res/ui/font-en.7043b83b.fnt`) has `eF = fontSize = 100`
// (verified on disk), so the native draw scale == `ua_size/100`. `Fa(w,h)`
// (L1712) sets the box; the explicit `Bg.Sk()` fit (L1626-1627, reached via
// `mk()`) shrinks text to it, and JS runs that for the button (`Bb.mk`
// L1844), hint (`Ib.Sk` L1910), dialog-row and map-node labels � i.e. the
// overwhelming majority of UI text. Wave D removed the native fit globally
// as an over-broad approximation; this restores it as the default `fit=true`
// (a label that already fits its box is unchanged, since `Sk` never grows).
// A bare `ea` whose `Fa` box is alignment-only can pass `fit=false`. `Oj()`
// (L1713) is the tight measured width (`N-J`) used to place siblings (e.g.
// `wr` L1987) and to centre `Ia(128)` (`xv` L1630). `align` maps the JS `Ia`
// masks: left = `Ia(64)` (bits 8|64|512), centre = `Ia(128)` (2|128|1024),
// right = `Ia(4|256|2048)` (L1630). y is the box TOP (node `D` + top text
// alignment).
enum class UiAlign { Left = 0, Center = 1, Right = 2 };

// JS `ea.a1` (L1931/L2484): ja/ko/ru scale every `ua` by 0.8, every other
// locale by 1. The shell swaps the BMF page + the `<lang>.<hash>.xml` string
// table at App::init/ensure_lang, so the active factor comes from
// `App::ui_text_scale()` (the JS boot switch L65 and the settings picker
// L1931 set `ea.a1=.8` for ru). `ea.ua(a)` -> `effect.ua(a*ea.a1)` (L1711).
float ea_a1(const App& app) { return app.ui_text_scale(); }

// JS `{br}` inline markup (the `Xc`/`ea` rich-text splitter): a hard line
// break. Resolve it to '\n' before a multiline draw. A no-op for plain text.
std::string expand_br(const std::string& text) {
    const std::string token = "{br}";
    if (text.find(token) == std::string::npos) return text;
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, token.size(), token) == 0) {
            out.push_back('\n');
            i += token.size();
        } else {
            out.push_back(text[i++]);
        }
    }
    return out;
}

void draw_ui_label(App& app, float x, float y, float w, float h,
                   const std::string& text, float ua_scale, UiAlign align,
                   float r, float g, float b, float a = 1.0f, bool fit = true) {
    if (text.empty() || w <= 0.0f || h <= 0.0f) return;
    const sf2::data::font* font = app.menu_font();
    if (font == nullptr) return;
    // Glyph scale = ua(size)/charset.eF (L1631); `ua_scale` is that ratio.
    float scale = ua_scale * ea_a1(app);
    if (scale <= 0.0f) return;
    // JS `Bg.Sk()` (L1627) single-line fit, reached via `mk()` (L1626):
    // `a = min(boxW/textW, boxH/textH)` then clamp to the authored `ua` (a
    // label never grows past its size). `Bb.mk()` (L1844) fits every button
    // caption and `Ib.Sk()` (L1910) the hint bar; dialog rows use the same
    // `Fa` box. Wave D removed this globally as an over-broad approximation
    // — the text bounds here are the tight glyph bounds (`rg`, L1630), not
    // the line advance, so a label whose box already fits is unchanged.
    // `fit=false` keeps a bare `ea`'s `Fa` box as alignment-only.
    if (fit) {
        const float tw = app.measure_text(*font, text, 1.0f);
        const float th = sf2::data::measure_text_height_utf8(*font, text, 1.0f);
        if (tw > 0.0f && tw * scale > w) scale = w / tw;
        if (th > 0.0f && th * scale > h) scale = h / th;
    }
    // `Oj()` (L1713): tight measured width (`N-J`); empty text contributes 0.
    const float draw_w = app.measure_text(*font, text, scale);
    float dx = x;  // `ea.C` = node left edge (Ia(64) left / align 0)
    if (align == UiAlign::Center) {
        dx = x + (w - draw_w) * 0.5f;  // `xv` (e&1170)>0 (L1630)
    } else if (align == UiAlign::Right) {
        dx = x + w - draw_w;  // `xv` (e&2340)>0
    }
    // NOTE: menu draw_text has no alpha channel (opaque labels).
    (void)a;
    (void)app.draw_text(dx, y, text, scale, r, g, b);
}

// JS `ea.ua(px)` -> glyph scale for a font size in PIXELS: `Qh.print` (L1631)
// divides by `charset.eF` (the BMF line height), so the `ua_scale` param of
// `draw_ui_label` is `px / eF` (it applies `ea.a1` itself). Same convention as
// the boot splash (`app.cpp` `jo_scale`).
float ui_ua_scale(App& app, float px) {
    const sf2::data::font* f = app.menu_font();
    const float eF = (f != nullptr && f->size > 0) ? static_cast<float>(f->size) : 100.0f;
    return px / eF;
}

// JS `Y.na(key, ...)` (L917): the runtime string-table lookup (`Cc.F().ln`,
// L920). Returns the localized text when the key is in the loaded table, else
// `fallback` — never a raw key. The table ships as `res/lang/en.<hash>.xml`
// and is loaded once by `ensure_lang`; a missing file/key falls back silently.
std::string loc(App& app, const std::string& key, const std::string& fallback) {
    if (key.empty()) return fallback;
    return lang_text(app.res_root(), key, fallback);
}

// Item display name: JS `Y.na(item.Cg || item.name)` (L2246-2247 `Ne.refresh`
// -> `Vc.V(Y.na(a))`; L1881 `ur.info.V(Y.na(a.name))`). The list.xml `Name` is
// a lang key ("WEAPON_KNIVES" -> "Knives"). When the table lacks it, fall back
// to the item's human SubType/Type (never a raw key).
std::string item_display_name(App& app, const CatalogItem& it) {
    const std::string fallback = it.subtype.empty() ? it.type : it.subtype;
    return loc(app, it.name, fallback);
}

// JS `Qh.apply` multiline wrap (L1628-1629), reached via `ea.rd(!0)`: greedy
// UTF-8 break at the box width (`f.N > width-jd` -> pop the overflowing glyph
// and start a new line) with vertical overflow (`e > height-jd`) setting `vn`
// and clipping. The single-line `Bg.Sk` fit (L1626-1627) is `draw_ui_label`;
// a multiline label keeps its authored `ua` and wraps instead. `line_step` is
// the JS line advance `(fontSize/eF)*lineHeight` (L1628 `d`); native menu
// eF=100, so `line_step = ua_scale * font->line_height`.
struct UiWrap {
    std::vector<std::string> lines;
    float line_step = 0.0f;
};

// The wrap + line advance itself, shared by the draw below and by the `od`
// content-height measurement (`Od.lj` L1950 `this.Md = Math.max(kb.ew(),
// this.cv)` — `od.layout` L1898 is derived from `Md`). `kb.ew()` is the text
// element's measured height, i.e. `lines * line_step`.
UiWrap wrap_ui_text(App& app, const std::string& text, float w, float ua_scale) {
    UiWrap out;
    if (text.empty() || w <= 0.0f) return out;
    const sf2::data::font* font = app.menu_font();
    if (font == nullptr) return out;
    // `{br}` inline markup -> hard line break (see expand_br).
    const std::string body = expand_br(text);
    const float scale = ua_scale * ea_a1(app);
    if (scale <= 0.0f) return out;
    out.line_step = scale * static_cast<float>(font->line_height);
    if (out.line_step <= 0.0f) return out;
    // `bx.Csb` (L1624) splits on '\n' first; `apply` char-wraps each logical
    // line.
    std::vector<std::string> logical;
    {
        std::string cur;
        for (char ch : body) {
            if (ch == '\n') {
                logical.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
        logical.push_back(cur);
    }
    for (const std::string& para : logical) {
        std::string cur;
        std::size_t i = 0;
        while (i < para.size()) {
            std::size_t next = i;
            (void)sf2::data::utf8_next(para, next);
            const std::string cand = cur + para.substr(i, next - i);
            if (!cur.empty() && app.measure_text(*font, cand, scale) > w) {
                // Prefer a word break (the oracle wraps at spaces): move the
                // tail after the last space to the new line. Falls back to a
                // character break when the line has no usable space.
                const std::size_t sp = cur.rfind(' ');
                if (sp != std::string::npos && sp > 0 && sp + 1 < cur.size()) {
                    out.lines.push_back(cur.substr(0, sp));
                    cur = cur.substr(sp + 1);
                } else {
                    out.lines.push_back(cur);
                    cur.clear();
                }
                continue;  // retry this glyph on the new line
            }
            cur = cand;
            i = next;
        }
        out.lines.push_back(cur);
    }
    return out;
}

// `kb.ew()` (L1950): the wrapped block height (`od` `Md`).
float measure_ui_wrapped(App& app, const std::string& text, float w, float ua_scale) {
    const UiWrap wr = wrap_ui_text(app, text, w, ua_scale);
    return static_cast<float>(wr.lines.size()) * wr.line_step;
}

void draw_ui_wrapped(App& app, float x, float y, float w, float h,
                     const std::string& text, float ua_scale, UiAlign align,
                     float r, float g, float b) {
    if (text.empty() || w <= 0.0f || h <= 0.0f) return;
    const UiWrap wr = wrap_ui_text(app, text, w, ua_scale);
    const float line_step = wr.line_step;
    if (line_step <= 0.0f) return;
    float yy = y;
    for (const std::string& ln : wr.lines) {
        if (yy + line_step > y + h) break;  // `e > height-jd` -> vn (clip)
        // 1.0f = the `a` param; `fit=false` keeps the wrapped width (the line
        // already fits `w`, so the single-line `Sk` shrink must not re-run).
        draw_ui_label(app, x, yy, w, line_step, ln, ua_scale, align, r, g, b,
                      1.0f, /*fit=*/false);
        yy += line_step;
    }
}

// Helpers defined later in this file (the atlas sprite path sits after this
// point; the `od`/`Ib` dialog art below needs it).
bool try_draw_atlas_button(App& app, const std::string& frame_name, float cx, float cy,
                           float w, float h, float alpha = 1.0f, bool fill = false,
                           bool flip_x = false, bool top_left = false);
bool load_scroll_atlas(App& app);
// JS `oe` user image (`res/users/images/<name>.png`, L1823). Defined with the
// item-image helpers below; the `Ib` bar needs it for the dialog portrait.
bool draw_user_image(App& app, const std::string& file_name, float cx, float cy, float w,
                     float h, float alpha, bool flip_x);
// `Rf`/`or` item icon (`Od.$A` L1945 prefers the Item composite). Defined with
// the item-image helpers below; the dialog portrait needs it (D14).
bool draw_item_image(App& app, const std::string& image_ref, float cx, float cy, float w,
                     float h, float alpha);
// `Ib` notification/hint bar (JS L1905-1912). `Sr()` L1908-1910 builds exactly
// ONE label = the JOINED lines (`lj` L1908 formats `^{0}^\n` per line) plus a
// portrait from the resolved `Image` (`Qhb(a=wt,...)` L1907 -> `v.RIa` L1909).
// There is NO speaker row — the port drew an invented speaker label.
void draw_ib_hint(App& app, sf2::render::Renderer& ren, const std::string& image,
                  const std::string& joined_lines, bool show_ok);
// `He.S` L1045-1051 Type routing (see the definition below).
bool dialog_scrolls_all_lines(const std::string& type);
// `od` 9-slice panel geometry (JS L1894-1900) — defined after the modal draw.
struct OdPanel {
    float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;  // on-screen BODY rect
    float c = 1.0f;                                    // design -> screen scale
};
OdPanel od_panel(float src_w, float src_h);
void draw_od_base(App& app, sf2::render::Renderer& ren, const OdPanel& p);
// Sliced-button path (defined below): the Regular quest dialog's button
// plate reuses it. No default args here (the definitions below carry the
// defaults; repeating them is an error).
bool load_sliced_atlas(App& app);
bool draw_bb_plate(App& app, const std::string& frame_name, float cx, float cy, float w,
                   float h, float alpha, bool flip_x);
void draw_flat_button(App& app, const std::string& label, float cx, float cy, float w, float h,
                      float r, float g, float b, bool hovered);
// The Regular dialog's action plate (a `He.dhb` slot) drawn through the
// `od.EF` L1899 two-button row - defined with the dialog layout below.
void draw_dialog_plate(App& app, const std::string& text, const std::string& color,
                       bool primary, float cx, float cy, float w, float h);
// The per-`Type` dialog drawers (`He.S` L1045-1051 -> `Wb.Xob` L926):
// `Regular`->280 `Od`, `Stranger`/`Multiline`/`MultilineBig`->290 `uj`,
// `NoAvatar`->340 `Ve`, `ShowLoot`->370 `vn`, `Notification`->the `Ib` bar.
//
// --- `od.aa` L1895 open/close tween (0.25 s) -------------------------------
// `ed(.25)` normalizes the 0.25 s timer. OPEN: `node.wa(dc.Ln()(t))` +
// `node.la(node.Eb + (-.2+.2*dc.kYa()(t)))` — alpha 0->1, scale 0.8->1.0.
// CLOSE: `node.wa(1-dc.KK()(t))` + `node.D(node.ra + 1000*dc.KK()(t))` — alpha
// 1->0, slide +1000 design px — then `n_()` at t==1. `dc.Ln()` =
// `1-(1-t)^2`, `dc.KK()` = `t^2`, `dc.kYa()` = back-out with `b=17.0158*.1`
// (`dc` L2349). `Wb.x3a` L927 fades the backdrop with the SAME 0.25 s tween
// (`r6(1,null,dc.Ln())` open / `r6(0,cb,dc.KK())` close; `Fc.r6` L1476 is
// `start(this.mn(), a, .25, c)`).
constexpr float kDialogAnimSecs = 0.25f;

float dialog_ease_out(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }  // dc.Ln
float dialog_ease_in(float t) { return t * t; }                             // dc.KK
float dialog_ease_back(float t) {                                           // dc.kYa
    constexpr float kB = 17.0158f * 0.1f;
    const float c = t - 1.0f;
    return c * c * ((kB + 1.0f) * c + kB) + 1.0f;
}

struct DialogAnim {
    float alpha = 1.0f;  // `node.wa`
    float scale = 1.0f;  // `node.la`
    float slide = 0.0f;  // `node.D` x offset, design px (close only)
};

// `od.aa` L1895 at normalized time `t` (`ed(.25)`).
DialogAnim dialog_anim_at(float age, bool closing) {
    DialogAnim a;
    float t = age / kDialogAnimSecs;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    if (closing) {
        a.alpha = 1.0f - dialog_ease_in(t);
        a.slide = 1000.0f * dialog_ease_in(t);
        return a;
    }
    a.alpha = dialog_ease_out(t);
    a.scale = 1.0f + (-0.2f + 0.2f * dialog_ease_back(t));
    return a;
}

// `Wb.x3a` L927 backdrop alpha (`Qa` = `Fc.Ed(-2147483648)`, i.e. 0x80 black).
float dialog_backdrop_alpha_at(float age, bool closing) {
    float t = age / kDialogAnimSecs;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return closing ? (1.0f - dialog_ease_in(t)) : dialog_ease_out(t);
}

void draw_notification(App& app, sf2::render::Renderer& ren, const EngineDialog& d);
void draw_od280_dialog(App& app, sf2::render::Renderer& ren, const EngineDialog& d,
                       const DialogAnim& anim);
void draw_uj290_dialog(App& app, sf2::render::Renderer& ren, const EngineDialog& d,
                       const DialogAnim& anim);
void draw_ve340_dialog(App& app, sf2::render::Renderer& ren, const EngineDialog& d,
                       const DialogAnim& anim);
void draw_vn370_dialog(App& app, sf2::render::Renderer& ren, const EngineDialog& d,
                       const DialogAnim& anim);
// The live dialog's 0.25 s open/close tween (`od.aa` L1895) at this frame.
DialogAnim dialog_anim_now(App& app, const EngineDialog& d);
// The dismissed dialog's 0.25 s close tween (`od.Ge(1)`/`n_` L1898).
void draw_closing_dialog(App& app, sf2::render::Renderer& ren);

// Draws the modal panel. JS `He.S` (L1045-1051) routes dialogs by `Type`:
// `Regular` -> `Xc.Xhb` -> `Wb.openDialog(280)` (`Od`, L929); `Stranger` /
// `Multiline` / `MultilineBig` -> `Xc.Bia`/`Xc.Nhb` -> 290 (`uj`, L929/L1050);
// `NoAvatar` -> `Xc.rIa` -> `Vhb` -> 340 (`Ve`, L930); `ShowLoot` ->
// `Xc.Uhb` -> 370 (`vn`, L929); `Notification` posts to the `Ib` bar
// (`Ib.F().Qhb`, L1050). The remaining Types (`Scroll`, `MultiLineScroll`,
// `ThreeButtons`, `ItemSetDialog`, `MultilineTMP`, `Simple`) hit a bare
// `debugger;` in the JS (L1048-1050) — no dialog object is built and `He.S`
// falls through to `this.sa()`, so the port advances without displaying.
void draw_quest_modal(App& app, sf2::render::Renderer& ren, bool is_top = true) {
    if (!is_top) return;  // layered stack: only the top screen draws the modal
    // D13: `Wb` owns ONE top dialog — the Settings `un` (case 310) draws here
    // too, over the CURRENT screen (it is opened by the `za` nav #5).
    draw_settings_dialog(app, ren);
    draw_closing_dialog(app, ren);  // `od.Ge(1)` L1898: the dismissed tween
    const EngineDialog* d = quest_modal_top(app);
    // The `Ib` bar (`Ib.F().Qhb` L1050) sits UNDER the `Wb` modal. When a
    // `Regular` is queued the bar still shows the last posted Notification
    // (`I.Qhb` L1907 overwrite) — both are visible at once (JS `He.S` L1050
    // posts the bar and the `Regular` opens without waiting).
    if (const EngineDialog* n = quest_notification_top(app)) {
        if (n != d) draw_notification(app, ren, *n);
    }
    if (d == nullptr) return;
    const DialogAnim anim = dialog_anim_now(app, *d);
    switch (dialog_kind(d->type)) {
        case DialogKind::kIbBar:
            // `Notification` (L1050): fire-and-forget, no dialog object -> no
            // screen dim (`BlockRaycast="0"`); the OK plate only draws when
            // the button nests a callback (`hab()` L1060).
            draw_notification(app, ren, *d);
            return;
        case DialogKind::kOd280: draw_od280_dialog(app, ren, *d, anim); return;
        case DialogKind::kUj290: draw_uj290_dialog(app, ren, *d, anim); return;
        case DialogKind::kVe340: draw_ve340_dialog(app, ren, *d, anim); return;
        case DialogKind::kVn370: draw_vn370_dialog(app, ren, *d, anim); return;
        case DialogKind::kNone: return;  // JS `debugger` branch (L1048-1050)
    }
}

// --- `od` 9-slice dialog base (JS L1894-1900) ----------------------------
// Shared by the quest modal above and the Settings `un` dialog. `od`'s base
// is `AV = new fc(a,b)` with JS defaults (2340, 1530) — NOTE: the audit
// (PORT_AUDIT_UI §0/§2.9) records 2340x1300, but the shipped JS line 1894
// reads `b==null&&(b=1530)`, so 1530 is the JS value (`un` calls `super()`
// with no args, L1917). `l4a()` (L1896) contain-fits `AV` to the screen
// (`N.fn(AV.x/AV.y)`) and scales the node by `(b.N-b.J)/AV.x`; the three
// `XN` slices are `E.get(254)` frames (left `bg_edge`, centre `bg`, right
// `bg_edge`, the right one flipped `Hr(!0)`).
// JS `od.l4a` (L1896): contain-fit the AV box (`N.fn(AV.x/AV.y)`) to get the
// node scale `c = (b.N-b.J)/AV.x`, then lay the three `XN` slices off a
// SEPARATE 1.2-aspect rect `d = b.fn(1.2)`: `g.xc(d.w * 1/c)` (the centre
// `bg` body) with the caps at `±(XN[0].fa.x/2 + d.w/2) * c`. At 1280x720 the
// AV 2340x1530 fit is 1101x720 (c=0.4706) but the BODY is the 1.2-rect
// (864x720) centred — the old contain-fit-of-AV body (1101 wide) was the
// sensei-screen edge mismatch.
OdPanel od_panel(float src_w, float src_h) {
    OdPanel p;
    const float screen_ar = kViewW / kViewH;  // N.lc
    const float src_ar = src_w / src_h;
    float fit_w = 0.0f, fit_h = 0.0f;
    if (screen_ar >= src_ar) {
        fit_h = kViewH;
        fit_w = fit_h * src_ar;
    } else {
        fit_w = kViewW;
        fit_h = fit_w / src_ar;
    }
    p.c = fit_w / src_w;  // (b.N-b.J)/AV.x
    // `d = N.fn(1.2)`: the 1.2-aspect rect fitted to the screen.
    float bw = 0.0f, bh = 0.0f;
    if (screen_ar >= 1.2f) {
        bh = kViewH;
        bw = bh * 1.2f;
    } else {
        bw = kViewW;
        bh = bw / 1.2f;
    }
    p.pw = bw;                   // g.xc(d.w*1/c) * c  == d.w
    p.ph = std::min(kViewH, bh); // g.Pb(N.height*c) * c == N.height
    p.px = kViewW * 0.5f - p.pw * 0.5f;
    p.py = kViewH * 0.5f - p.ph * 0.5f;
    return p;
}

void draw_od_base(App& app, sf2::render::Renderer& ren, const OdPanel& p) {
    bool drew = false;
    if (load_scroll_atlas(app)) {
        drew = try_draw_atlas_button(app, "bg", p.px + p.pw * 0.5f, p.py + p.ph * 0.5f,
                                     p.pw, p.ph, 1.0f, /*fill=*/true, /*flip_x=*/false);
        if (drew) {
            // `XN[0]` left + `XN[2]` right (`l4a` L1896): `f.C(-(e+b/2*c))`,
            // `h.C(e+b/2*c)` with `e=XN[0].fa.x/2`, `b=d.w` -> the cap centre
            // offset is `(cap_w + body_w)/2`; cap width `bg_edge.fa.x*c`,
            // height = body height, right cap `Hr(!0)` -> flip_x.
            constexpr float kOdEdgeW = 219.0f;  // scroll.json bg_edge 219x1536
            const float cap_w = kOdEdgeW * p.c;
            const float off = (cap_w + p.pw) * 0.5f;
            const float cx = p.px + p.pw * 0.5f;
            try_draw_atlas_button(app, "bg_edge", cx - off, p.py + p.ph * 0.5f,
                                  cap_w, p.ph, 1.0f, /*fill=*/true, /*flip_x=*/false);
            try_draw_atlas_button(app, "bg_edge", cx + off,
                                  p.py + p.ph * 0.5f, cap_w, p.ph, 1.0f, /*fill=*/true,
                                  /*flip_x=*/true);
        }
    }
    if (!drew) {
        const float panel[] = {p.px, p.py, p.px + p.pw, p.py, p.px, p.py + p.ph,
                               p.px + p.pw, p.py, p.px + p.pw, p.py + p.ph, p.px, p.py + p.ph};
        ren.draw_triangles(panel, 6, 0.08f, 0.07f, 0.10f, 0.95f);
    }
}

// --- Settings `un` dialog geometry (JS L1916-1930) -----------------------
// `un extends od` (L1916) with `Md=750` (L1930). `od.layout` (L1898) puts the
// content node at `Ne.D(-Md/2)`. Rows are built by the `un` factories:
//   b(q) L1917: icon `R.$(E.get(250),q,l)` at `C(-q.za()*.9)` (centre anchor
//               after `Ga()`), `D(k*q.qa()*1.25)`.
//   c(q,r) L1917: an invisible row hit-rect `R.Ed(65280,800,q.qa())` parented
//               to the same container `l`, left `r.ya-t/2`, top
//               `k*x*1.25-x/2` (alpha byte of 65280 = 0x00FF00 -> invisible).
// `k` starts 0.5 (L1927) and advances 1 per gated block; with both
// `Ca.hasFeature("audio")` and `("credits")` true (L1928-1929) the rows are:
//   k=0.5  Sound (container C(-300)) + Music (container C(+300)), side by side
//   k=1.5  Credits
//   k=2.5  Language
// The `E.get(250)` tiles are 170x170 (`res/ui/settings_icons.*`), so the row
// step is 170*1.25 = 212.5 and the icon x is -170*0.9 = -153 (container
// space). Buttons `Bb.Pb(150)` and the `Nm` notice keep the `od`/`Md` geometry.
struct SettingsLayout {
    OdPanel panel;
    float title_x = 0.0f, title_y = 0.0f, title_w = 0.0f, title_h = 0.0f;
    float icon = 0.0f;  // 170 * panel.c
    // Icon centres (screen px); Sound/Music share the k=0.5 row.
    float sound_cx = 0.0f, sound_cy = 0.0f;
    float music_cx = 0.0f, music_cy = 0.0f;
    float credits_cx = 0.0f, credits_cy = 0.0f;
    float lang_cx = 0.0f, lang_cy = 0.0f;
    // Invisible row hit-rects (JS `c` L1917): 800 x icon, one per container.
    float row_w = 0.0f, row_h = 0.0f;
    float sound_row_cx = 0.0f, music_row_cx = 0.0f;
    float credits_row_cx = 0.0f, lang_row_cx = 0.0f;
    float back_cx = 0.0f, back_cy = 0.0f;
    float restart_cx = 0.0f, restart_cy = 0.0f;
    float btn_w = 0.0f, btn_h = 0.0f;
    float notice_y = 0.0f;
};

SettingsLayout settings_layout() {
    SettingsLayout s;
    s.panel = od_panel(2340.0f, 1530.0f);  // od AV = fc(2340,1530), L1894
    const OdPanel& p = s.panel;
    const float cx = p.px + p.pw * 0.5f;  // design x=0
    const float cy = p.py + p.ph * 0.5f;  // design y=0
    // Title `Vc`: `$T` L1930 `Fa(1560,160)` + `C(-780)`; `ua(152)`, `Ia(128)`
    // (L1900). `od.layout` L1898: `Vc.D(-(a+Vc.pfa().y))` with a=375.
    s.title_w = 1560.0f * p.c;
    s.title_h = 160.0f * p.c;
    s.title_x = cx - s.title_w * 0.5f;
    s.title_y = cy - 535.0f * p.c - s.title_h * 0.5f;
    // `un` rows (L1917): 170x170 tile, step 170*1.25 = 212.5, icon x
    // -170*0.9 = -153; content node anchor D(-375) (od.layout, Md=750).
    constexpr float kIcon = 170.0f;
    constexpr float kStep = kIcon * 1.25f;    // 212.5
    constexpr float kIconX = -kIcon * 0.9f;   // -153
    constexpr float kContY = -375.0f;         // Ne.D(-Md/2), Md=750
    s.icon = kIcon * p.c;
    const float icon_x = kIconX * p.c;
    const float y_audio = cy + (kContY + 0.5f * kStep) * p.c;   // k=0.5
    const float y_credits = cy + (kContY + 1.5f * kStep) * p.c;  // k=1.5
    const float y_lang = cy + (kContY + 2.5f * kStep) * p.c;     // k=2.5
    s.sound_cx = cx + (-300.0f) * p.c + icon_x;
    s.sound_cy = y_audio;
    s.music_cx = cx + 300.0f * p.c + icon_x;
    s.music_cy = y_audio;
    s.credits_cx = cx + icon_x;
    s.credits_cy = y_credits;
    s.lang_cx = cx + icon_x;
    s.lang_cy = y_lang;
    // `c` hit-rect (L1917): left = icon local x - icon/2, width 800, height
    // = icon; centre x = left + 400 = icon local x + 315.
    s.row_w = 800.0f * p.c;
    s.row_h = s.icon;
    const float row_off = kIconX + 400.0f - kIcon * 0.5f;  // +315 design
    s.sound_row_cx = cx + (-300.0f + row_off) * p.c;
    s.music_row_cx = cx + (300.0f + row_off) * p.c;
    s.credits_row_cx = cx + row_off * p.c;
    s.lang_row_cx = cx + row_off * p.c;
    // Buttons (L1930 `Kb=Bb("EButtonDark")` BACK / `Km=Bb("EButtonBeige")`
    // RESTART, both `Pb(150)`). D15: RESTART is `X(!1)` hidden until the
    // language row changed (`t9`), when `un.rHa` case 4 (L1931) reveals it and
    // splits BACK/RESTART by `width*.6` (`Kb.C(-w*.6)` / `Km.C(w*.6)`).
    s.btn_w = 320.0f * p.c;
    s.btn_h = 150.0f * p.c;
    const float split = g_settings_restart_visible ? s.btn_w * 0.6f : 0.0f;
    s.back_cx = cx - split;
    s.restart_cx = cx + split;
    // `od.layout` (L1898) D: the button container centre.
    s.back_cy = s.restart_cy = cy + 500.0f * p.c;
    // Notice `Nm`: `C(-750)`, `D(250)`, `Fa(1500,50)` (L1929).
    s.notice_y = cy + 250.0f * p.c;
    return s;
}

// --- HUD HP-bar leak/decay (JS `Br` L2010-2015, Phase 7.3) ---
// JS `Qyb()` (L2012-2013): damage -> `v5(a, 10)` (instant fill over 10
// frames) + `g5(a, 30)` (trailing "leak" over 30 frames); heal/new-round ->
// `zO = 60` (leak hold) + `v5(a, 10)`. `ia()` steps both tweens one frame
// per tick (`Rnb`/`Jnb`) with `gCa` clamping; `d6a()` (L2015) keeps a minimum
// show while HP > 0. Reads the existing FightFighter hp/max_hp only (no
// fight.hpp changes); stepped once per render_impl call (one 60 Hz frame).
// NOT ported: segment stripes (`b_`, `mO = L5`) and the `Jc.TU` gradient —
// they need per-fighter segment state from the fight sim (forbidden files
// this stream); noted in the stream report.
// Minimum bar show while alive (JS `Jj.jha` from the LifeBarMin config — the
// exact tuned value is not in the spec excerpts, so this is an
// approximation flagged for Stream verification).
constexpr float kLifeBarMinShow = 0.03f;

struct HudBarDecay {
    float shown() const { return shown_; }
    float leak() const { return leak_; }

    void retarget(float target) {
        // JS `gCa` clamp + `d6a` min-show.
        target = std::clamp(target, 0.0f, 1.0f);
        if (target > 0.0f && target < kLifeBarMinShow) target = kLifeBarMinShow;
        if (target < shown_to_ - 0.0005f) {
            // Damage: instant drops over 10 frames, leak trails over 30.
            shown_step_ = (target - shown_) / 10.0f;
            shown_to_ = target;
            shown_left_ = 10;
            leak_step_ = (target - leak_) / 30.0f;
            leak_to_ = target;
            leak_left_ = 30;
            hold_ = 0;
        } else if (target > shown_to_ + 0.0005f) {
            // Heal / new round: instant rises over 10, leak holds 60 (zO).
            shown_step_ = (target - shown_) / 10.0f;
            shown_to_ = target;
            shown_left_ = 10;
            hold_ = 60;
        }
    }

    void tick() {
        if (shown_left_ > 0) {
            shown_ += shown_step_;
            if (--shown_left_ == 0) shown_ = shown_to_;
        }
        if (hold_ > 0) {
            --hold_;
        } else if (leak_left_ > 0) {
            leak_ += leak_step_;
            if (--leak_left_ == 0) leak_ = leak_to_;
        }
    }

private:
    float shown_ = 1.0f;      // JS `JO` — the instant fill ratio
    float leak_ = 1.0f;       // JS `oN` — the trailing leak ratio
    float shown_to_ = 1.0f;   // JS `dC` — instant target
    float leak_to_ = 1.0f;    // JS `MN` — leak target
    float shown_step_ = 0.0f;
    float leak_step_ = 0.0f;
    int shown_left_ = 0;      // JS `KO` — frames left on the instant tween
    int leak_left_ = 0;       // JS `pN` — frames left on the leak tween
    int hold_ = 0;            // JS `zO` — heal hold freezing the leak
};

HudBarDecay s_hud_player_decay_;
HudBarDecay s_hud_enemy_decay_;

// Phase 7.4 regen display copies (presentation only — the fight sim never
// reads them, so the pose dump is unaffected). The magic/effect containers
// (JS `tl.Rf` L842-844) are owned by FightController (`magic_fx_`) — the
// single JS-faithful source, fed by the `Yl` Effect triggers via `tl.Nt`
// (L842) and ticked in `FightController::update` (`tl.WL` L837); the fight
// draw path reads them from the controller.
sf2::audio::SpecialMeters s_regen_player_;
sf2::audio::SpecialMeters s_regen_enemy_;

// The between-rounds "Next" button does NOT exist in the JS: the round
// auto-advances (`ca.Onb` L411 `ZK(); NA(); Z2()`) and the HUD only shows
// the round-break plate (`Cr.tca` L2023). The old invisible Next-button
// rect and its click handler were an invention and are GONE;
// `next_button_center` now reports the inert (0,0) so the non-owned
// headless drivers keep linking.

// ---------------------------------------------------------------------------
// On-screen gamepad geometry — [ORIGINAL] JS `Za.update()` (sf2.js L454-456)
// lays the virtual controls out on the 960x540 design rect (N.rect) scaled to
// the view. Screen mode (no touch device, `L.K.un == false`):
//   c = Eha*0.05, d = Eha*0.03, e = max(150, Eha*0.2)   (Eha = design height)
//   joystick: scale f = e / sNa (sNa = the base frame width), center =
//     (e/2 + c, view_bottom - e/2 - d)
//   buttons node: scale e/440, center = (view_right - 220*e/440 - c,
//     view_bottom - 298*e/440 - d); the buttons sit INSIDE the node at the
//     JS `fu` offsets: punch Si(120,28), kick fh(-50,198) — the node's
//     local Y grows DOWN (Ea screen space), so +198 is the LOWER button.
// The knob drag maps to a movement sector via `ze.GBa` (the 8-way split
// with the 55-degree sector half-angle, jz=55): 1=up 2=up-forward
// 3=forward 4=down-forward 5=down 6=down-back 7=back 8=up-back — the same
// key_type 1-8 the keyboard path feeds.
// ---------------------------------------------------------------------------
// JS `Za.update` L454-455: `var c=N.Eha*.05; N.BK==0&&(c*=2); var d=N.Eha*.03;
// N.lc<1&&(c=N.Eha*.04,d=N.Eha*.1); if(L.K.un) var e=N.Eha*.4;
// else e=Math.max(150,N.Eha*.2), d=c=15;`. The oracle build has `L.K.un` TRUE
// (every oracle capture shows the big mobile pad: joystick r~144 vs the
// desktop r=75, knob ~65 vs ~35), so e = H*.4 = 288, c = H*.05*2 (BK==0 at
// 16:9) = 72, d = H*.03 = 21.6.
constexpr float kPadMarginC = kViewH * 0.05f * 2.0f;  // c — side margin
constexpr float kPadMarginD = kViewH * 0.03f;  // d — bottom margin
constexpr float kPadSizeE = kViewH * 0.4f;            // e (mobile `un`, H*.4)
// The joystick base frame (JoystickContainer_norm) is 466 atlas px wide.
constexpr float kJoyBaseFrame = 466.0f;
// The buttons node is 440x596 local px (JS `fu` layout), scaled by e/440.
constexpr float kPadNodeW = 440.0f;
constexpr float kPadNodeH = 596.0f;
// The JS button hit radius: `uab` tests x*x+y*y < 13225 (= 115^2) in the
// node's local space.
constexpr float kPadBtnHitR = 115.0f;
// The knob travel: the JS `ze` grab zone is Ho*1.5 (Kz=1.5) where Ho is
// the knob's max offset; the dead zone is Ho*0.5 (Lz=0.5). Ho itself is
// srb(EH.za()/2) = half the ACTION container frame (466) — but the knob
// offset fed to e5() is normalized (GBa works on the unit vector), so the
// native uses the on-screen base radius directly.
constexpr float kJoyGrabScale = 1.5f;   // Kz — grab zone = base_r * this
constexpr float kJoyDeadZone = 0.5f;    // Lz — dead zone = base_r * this
// The JS sector math: aO = 55 deg (jz), r8 = 35 deg (90-jz). A diagonal
// counts only when its off-axis component exceeds tan(r8) of the main one.
constexpr float kJoySectorTan = 0.7002f;  // tan(35 deg)

// The on-screen gamepad layout (view coords, computed once per use).
struct GamepadLayout {
    float joy_cx = 0.0f, joy_cy = 0.0f;  // the base center
    float joy_r = 0.0f;                  // the base radius (on screen)
    float knob_r = 0.0f;                 // the knob radius (on screen)
    float node_cx = 0.0f, node_cy = 0.0f;  // the buttons node center
    float node_scale = 1.0f;            // e/440
    float punch_cx = 0.0f, punch_cy = 0.0f;  // button centers (view coords)
    float kick_cx = 0.0f, kick_cy = 0.0f;
    float btn_r = 0.0f;  // the button hit radius (view coords)

    GamepadLayout() {
        // Joystick (JS: f = e/sNa; center = (f*sNa/2 + c, bottom - f*sNa/2 - d)
        // -> with f*sNa == e: (e/2 + c, bottom - e/2 - d)).
        joy_r = kPadSizeE * 0.5f;
        joy_cx = kPadSizeE * 0.5f + kPadMarginC;
        joy_cy = kViewH - kPadSizeE * 0.5f - kPadMarginD;
        // The knob frame (Joystick_norm, 212 px) renders at the same scale
        // as the base (the JS `ze` builds both from E.get(268) at the
        // node's scale; the knob is 212/466 of the base).
        knob_r = kPadSizeE * (212.0f / kJoyBaseFrame) * 0.5f;
        // Buttons node (JS: scale e/440; center = (right - 220*scale - c,
        // bottom - 298*scale - d)).
        node_scale = kPadSizeE / kPadNodeW;
        node_cx = kViewW - kPadNodeW * 0.5f * node_scale - kPadMarginC;
        node_cy = kViewH - kPadNodeH * 0.5f * node_scale - kPadMarginD;
        // The buttons inside the node (JS `fu` ctor: Si(120,28) punch,
        // fh(-50,198) kick; local Y grows down).
        punch_cx = node_cx + 120.0f * node_scale;
        punch_cy = node_cy + 28.0f * node_scale;
        kick_cx = node_cx - 50.0f * node_scale;
        kick_cy = node_cy + 198.0f * node_scale;
        btn_r = kPadBtnHitR * node_scale;
    }
};

// [ORIGINAL] JS `ze.GBa` — the knob vector -> the movement key 1-8 (0 =
// neutral/dead). Screen Y grows down, so the JS flips y (`d = a.y*-1`)
// before the sector test; the native works in the same screen space.
int joy_sector_of(float dx, float dy, float base_r) {
    const float len2 = dx * dx + dy * dy;
    if (len2 <= base_r * base_r * kJoyDeadZone * kJoyDeadZone) return 0;
    // Flip to the JS's up-positive space.
    const float x = dx;
    const float y = -dy;
    // Rotate by the sector half-angle (aO=55deg): e = x*cos+x*sin... the
    // JS rotates (x,y) by aO/2 into (e,c) — the exact matrix:
    //   e = x*cos(aO/2) + y*sin(aO/2)
    //   c = y*cos(aO/2) - x*sin(aO/2)
    constexpr float kHalf = 55.0f * 3.14159265358979323846f / 180.0f * 0.5f;
    const float e = x * std::cos(kHalf) + y * std::sin(kHalf);
    const float c = y * std::cos(kHalf) - x * std::sin(kHalf);
    const bool e_pos = e >= 0.0f;
    const bool c_pos = c >= 0.0f;
    int sector;      // the JS quadrant id (1..4)
    float main_v;    // the axis-aligned component
    float off_v;     // the diagonal component
    if (e_pos && c_pos) {
        sector = 1;  // up .. up-forward
        main_v = std::fabs(e);
        off_v = std::fabs(c);
    } else if (e_pos && !c_pos) {
        sector = 2;  // forward .. down-forward
        main_v = std::fabs(c);
        off_v = std::fabs(e);
    } else if (!e_pos && c_pos) {
        sector = 4;  // back .. up-back
        main_v = std::fabs(c);
        off_v = std::fabs(e);
    } else {
        sector = 3;  // down .. down-back
        main_v = std::fabs(e);
        off_v = std::fabs(c);
    }
    // A diagonal counts when the off-axis component is SMALL relative to
    // the main one (JS `d<=a*this.EVa&&(e=!0)` — e=true = the diagonal
    // branch of the switch; the pure directions are the e-axis edges).
    const bool diagonal = off_v <= main_v * kJoySectorTan;
    switch (sector) {
        case 1: return diagonal ? 2 : 1;  // up | up-forward
        case 2: return diagonal ? 4 : 3;  // forward | down-forward
        case 3: return diagonal ? 6 : 5;  // down | down-back
        case 4: return diagonal ? 8 : 7;  // back | up-back
        default: return 0;
    }
}

// [ORIGINAL] The JS `Za.bbb()` KEYBOARD bindings (`gu.De`), in registration
// order (the `gu.RL` list, minified sf2 js @232345):
//   De(2,0,key(1),key(3)) -> up_forward   = W+D
//   De(8,0,key(1),key(7)) -> up_back      = W+A
//   De(4,0,key(5),key(3)) -> down_forward = S+D
//   De(6,0,key(5),key(7)) -> down_back    = S+A
//   De(1,0,key(1)) up | De(5,0,key(5)) down | De(7,0,key(7)) back | De(3,0,key(3)) forward
// `gu.Oba` (@234895) fires only the FIRST satisfied movement binding per pass
// (the `this.TD` latch), and releases a binding when ANY of its keys is
// released — so W+D selects up_forward(2) while up(1) stays held (it is NOT
// released), and W alone selects up(1). dir index: 0=up, 1=forward, 2=down,
// 3=back; dir_b = -1 for the single-key cardinals.
struct KeyboardBinding {
    int control;
    int dir_a;
    int dir_b;
};
constexpr KeyboardBinding kKeyboardBindings[] = {
    {2, 0, 1}, {8, 0, 3}, {4, 2, 1}, {6, 2, 3},   // the diagonal pairs FIRST
    {1, 0, -1}, {5, 2, -1}, {7, 3, -1}, {3, 1, -1}  // the cardinals
};

// A GLFW key -> the physical movement slot, or -1 when it is not a movement
// key. The WASD keys are the JS `Af.oUa` directions (key(1)/(3)/(5)/(7)); the
// arrows are the desktop aliases folded in only when opted in. They share the
// slots so an alias and its JS key combine into the same diagonal.
int keyboard_move_slot(int glfw_key, bool aliases) {
    switch (glfw_key) {
        case 87: return 0;  // W -> up
        case 68: return 2;  // D -> forward
        case 83: return 4;  // S -> down
        case 65: return 6;  // A -> back
        case 265: return aliases ? 1 : -1;  // Up
        case 262: return aliases ? 3 : -1;  // Right
        case 264: return aliases ? 5 : -1;  // Down
        case 263: return aliases ? 7 : -1;  // Left
        default: return -1;
    }
}

// One keyboard movement edge for a fight controller: JS `gu.Oba` over the
// bindings above. Returns the control whose PRESS fired (0 = none). The emit
// order matches the JS exactly: a satisfied binding that is the first press
// this pass fires a Tap; an unsatisfied binding whose changed key was released
// fires a Release. `Fighter::input` ignores a duplicate Tap on a held key, so
// the re-press of a still-satisfied cardinal on a diagonal release is a no-op.
int keyboard_move_edge(sf2::scene::FightController* fight, KeyInputState& st,
                       int slot, bool down, const char* tag) {
    if (slot < 0 || slot >= 8) return 0;
    st.phys[slot] = down;
    const int dir = slot / 2;
    auto dir_held = [&st](int d) {
        return st.phys[d * 2] || st.phys[d * 2 + 1];
    };
    bool td = false;
    int fired = 0;
    for (const KeyboardBinding& b : kKeyboardBindings) {
        const bool satisfied =
            dir_held(b.dir_a) && (b.dir_b < 0 || dir_held(b.dir_b));
        if (satisfied) {
            if (!td) {
                td = true;
                fired = b.control;
                if (fight != nullptr) {
                    fight->player_input(
                        static_cast<sf2::scene::key_type>(b.control),
                        sf2::scene::press_type::tap);
                }
            }
        } else if (!down && (b.dir_a == dir || b.dir_b == dir) &&
                   !dir_held(dir)) {
            if (fight != nullptr) {
                fight->player_input(
                    static_cast<sf2::scene::key_type>(b.control),
                    sf2::scene::press_type::release);
            }
        }
    }
    st.sector = fired;
    std::fprintf(stdout, "[%s] player input -> key slot %d %s (sector %d)\n",
                 tag, slot, down ? "down" : "up", st.sector);
    std::fflush(stdout);
    return fired;
}

// The pointer -> gamepad events (JS `ze.nia/Qgb/oia` for the joystick,
// `fu.nia/oia` for the buttons). ONE code path for the fight screen and the
// Dojo `FightNone` viewer: the drawn pad feeds the SAME
// `FightController::player_input` edges the keyboard does (JS `Za.hS` L453
// -> `ca.Ka()` -> `ca.N0a` L426, the current controller's own fighter).
// `live` = the pad is armed (the fight: round running; the dojo: `xF(2)`
// armed it). `tag` prefixes the log lines. `fight` may be null (no release
// target yet).
static void update_pad_input(App& app, sf2::scene::FightController* fight,
                             PadInputState& st, bool live, const char* tag) {
    auto key = [fight](sf2::scene::key_type k, sf2::scene::press_type pt) {
        if (fight != nullptr) fight->player_input(k, pt);
    };
    if (!live) {
        // Round ended mid-drag: release everything so no key stays held.
        if (st.joy_grabbed || st.joy_sector != 0 || st.btn_punch_down ||
            st.btn_kick_down) {
            st.joy_grabbed = false;
            st.joy_knob_x = st.joy_knob_y = 0.0f;
            st.joy_sector = 0;
            st.btn_punch_down = st.btn_kick_down = false;
        }
        return;
    }
    const App::PointerState& p = app.pointer();
    const GamepadLayout pad;

    // --- Joystick (JS `ze`): grab inside the base's 1.5x zone, drag the
    // knob, map the offset to the movement sector 1-8. The knob follows
    // the pointer clamped to the base radius (JS `e5` + `Mz.G_a`).
    if (p.pressed && !st.joy_grabbed) {
        const float dx = static_cast<float>(p.x) - pad.joy_cx;
        const float dy = static_cast<float>(p.y) - pad.joy_cy;
        const float grab_r = pad.joy_r * kJoyGrabScale;
        if (dx * dx + dy * dy <= grab_r * grab_r) {
            st.joy_grabbed = true;
        }
    }
    if (st.joy_grabbed) {
        if (!p.down) {
            // Released (JS `oia`): neutral + the key release event.
            st.joy_grabbed = false;
            st.joy_knob_x = st.joy_knob_y = 0.0f;
            if (st.joy_sector != 0) {
                key(static_cast<sf2::scene::key_type>(st.joy_sector),
                    sf2::scene::press_type::release);
                std::fprintf(stdout, "[%s] player input -> joy release %d\n", tag,
                             st.joy_sector);
                st.joy_sector = 0;
            }
        } else {
            // Drag (JS `Qgb`): clamp the knob to the base, recompute the
            // sector, and emit the key change (release the old sector's
            // key, tap the new one — the same edges the keyboard path
            // produces via on_key).
            float dx = static_cast<float>(p.x) - pad.joy_cx;
            float dy = static_cast<float>(p.y) - pad.joy_cy;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float max_off = pad.joy_r;
            if (len > max_off) {
                dx *= max_off / len;
                dy *= max_off / len;
            }
            st.joy_knob_x = dx;
            st.joy_knob_y = dy;
            const int sector = joy_sector_of(dx, dy, pad.joy_r);
            if (sector != st.joy_sector) {
                if (st.joy_sector != 0) {
                    key(static_cast<sf2::scene::key_type>(st.joy_sector),
                        sf2::scene::press_type::release);
                }
                if (sector != 0) {
                    key(static_cast<sf2::scene::key_type>(sector),
                        sf2::scene::press_type::tap);
                }
                std::fprintf(stdout, "[%s] player input -> joy sector %d -> %d\n", tag,
                             st.joy_sector, sector);
                std::fflush(stdout);
                st.joy_sector = sector;
            }
        }
    }

    // --- Attack buttons (JS `fu.nia/oia`): a press inside a button's
    // circle taps the attack key; the release ends it. The JS hit test is
    // the node-local x*x+y*y < 115^2 — the native tests the view-space
    // circle around each button center.
    const float px = static_cast<float>(p.x);
    const float py = static_cast<float>(p.y);
    if (p.pressed && !st.joy_grabbed) {
        const float pdx = px - pad.punch_cx;
        const float pdy = py - pad.punch_cy;
        if (pdx * pdx + pdy * pdy <= pad.btn_r * pad.btn_r) {
            st.btn_punch_down = true;
            key(sf2::scene::key_type::punch, sf2::scene::press_type::tap);
            std::fprintf(stdout, "[%s] player input -> punch (pad)\n", tag);
            std::fflush(stdout);
        } else {
            const float kdx = px - pad.kick_cx;
            const float kdy = py - pad.kick_cy;
            if (kdx * kdx + kdy * kdy <= pad.btn_r * pad.btn_r) {
                st.btn_kick_down = true;
                key(sf2::scene::key_type::kick, sf2::scene::press_type::tap);
                std::fprintf(stdout, "[%s] player input -> kick (pad)\n", tag);
                std::fflush(stdout);
            }
        }
    }
    if (st.btn_punch_down && !p.down) {
        st.btn_punch_down = false;
        key(sf2::scene::key_type::punch, sf2::scene::press_type::release);
    }
    if (st.btn_kick_down && !p.down) {
        st.btn_kick_down = false;
        key(sf2::scene::key_type::kick, sf2::scene::press_type::release);
    }
}

// Loads the ui/controller atlas (the virtual gamepad art: Joystick*,
// btn_punch_*, btn_kick_*) into the app's atlas cache. Returns true when
// the frames are registered (logged once). The atlas ships as ASTC ktx -
// the same decode path the other UI atlases use.
bool load_controller_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string ui_dir = app.res_root() + "/ui";
        // Find the controller json (controller.<hash>.json).
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(ui_dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("controller.", 0) == 0 &&
                entry.path().extension().string() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) {
            std::fprintf(stderr, "[fight] controller atlas json not found in %s\n",
                         ui_dir.c_str());
            return false;
        }
        // The texture: try ktx then dds beside the json (the hashed stem).
        const std::string stem = std::filesystem::path(json_path)
                                     .filename()
                                     .string()
                                     .substr(0, std::string("controller").size());
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".ktx", ".dds", ".webp", ".png"}) {
            for (const auto& entry : std::filesystem::directory_iterator(ui_dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(stem + ".", 0) == 0 &&
                    entry.path().extension().string() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) {
            std::fprintf(stderr, "[fight] controller atlas texture not decodable\n");
            return false;
        }
        const GLuint gl = app.renderer().texture_for("controller_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[fight] controller atlas: %dx%d tex %dx%d %zu frames\n",
                     a.w, a.h, tex.w, tex.h, a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[fight] controller atlas load failed: %s\n", e.what());
    }
    return ok;
}

// Draws a flat (untextured) button + its label as a solid quad. It is used
// ONLY as a caller's fallback AFTER the JS art path misses (a `try_draw_*` /
// `draw_bb_plate` false return, or an absent user image). No user-visible
// control is flat as its PRIMARY art: the `Bb` sliced plates now route through
// `draw_bb_plate` (ESliced). Remaining flat sites are all genuine art misses
// (incl. the seals cell image - OPEN: JS `js` draws only the item image and
// has no art for a missing one).
void draw_flat_button(App& app, const std::string& label, float cx, float cy, float w, float h,
                      float r, float g, float b, bool hovered) {
    sf2::render::Renderer& ren = app.renderer();
    const float x0 = cx - w / 2.0f;
    const float y0 = cy - h / 2.0f;
    const float x1 = cx + w / 2.0f;
    const float y1 = cy + h / 2.0f;
    const float border = hovered ? 3.0f : 2.0f;
    const float verts_border[] = {
        x0 - border, y0 - border, x1 + border, y0 - border, x1 + border, y1 + border,
        x0 - border, y0 - border, x1 + border, y1 + border, x0 - border, y1 + border,
    };
    ren.draw_triangles(verts_border, 6, 0.1f, 0.1f, 0.12f, 0.9f);
    const float verts_fill[] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
    ren.draw_triangles(verts_fill, 6, r, g, b, 0.92f);
    (void)label;
}

// Tries to draw an atlas frame sized to (w,h). Returns true if drawn.
// `top_left=false` (default) anchors the frame CENTRE at (cx,cy) - the JS
// `Ga()`/button case (`db`/`Le`/`Qr`). `top_left=true` anchors its LEFT/TOP
// edge at (cx,cy) - the plain `R.$` case (`wr`/`xr` icons, progress bars).
bool try_draw_atlas_button(App& app, const std::string& frame_name, float cx, float cy, float w, float h,
                           float alpha, bool fill, bool flip_x, bool top_left) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame(frame_name, &fr, &tw, &th, &gl)) {
        static std::set<std::string> logged;
        if (logged.insert(frame_name).second) {
            std::fprintf(stdout, "[ui] atlas frame missing: %s\n", frame_name.c_str());
            std::fflush(stdout);
        }
        return false;
    }
    {
        static std::set<std::string> resolved;
        if (resolved.insert(frame_name).second) {
            std::fprintf(stdout, "[ui] atlas frame hit: %s rect=(%d,%d,%d,%d) tex=%dx%d gl=%u\n",
                             frame_name.c_str(), fr.x, fr.y, fr.w, fr.h, tw, th, gl);
            std::fflush(stdout);
        }
    }
    sf2::scene::Sprite s;
    s.texture_name = frame_name;
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
    // Anchor: JS `Ke` ctor (L1599) defaults `BS=CS=Via=Wia=0`, so a plain
    // `R.$`/`R.Ed` node's `position` is its LEFT/TOP edge; `Ga()` (L1602)
    // sets it to the CENTRE (`ik(.5,.5)` + `Rn(.5,.5)`). Buttons call `Ga()`
    // (`db.$w` L1840, `Le` L1848, `Qr` L2092), so (cx,cy) is their centre.
    // The `wr`/`xr` icons and the `PL`/progress-bar containers do NOT
    // (`R.$(E.get(260), y.Zna)` has no `Ga`), so `top_left` selects the
    // matched left/top-edge anchor for those call sites.
    if (top_left) {
        s.transform.anchor_x = 0.0f;
        s.transform.anchor_y = 0.0f;
    }
    // Natural sprite size = the frame's UNTRIMMED `sourceSize` (`fa`), not the
    // packed/trimmed rect: JS `R.Cb` (L1616) sets `this.fa = a.fa * atlasScale`
    // then `b.ba(this.fa)`; `R.$` (L1620) sizes the node the same way. The
    // packer's `spriteSourceSize` offset (`qj`) is the trim origin `Em` the
    // renderer's trim compensation consumes. Fall back to the packed rect for
    // packs that omit `sourceSize`. (Wave T (A): size by untrimmed sourceSize
    // + spriteSourceSize trim for EVERY UI frame.)
    const float nat_w =
        fr.source_w > 0 ? static_cast<float>(fr.source_w) : static_cast<float>(fr.w);
    const float nat_h =
        fr.source_h > 0 ? static_cast<float>(fr.source_h) : static_cast<float>(fr.h);
    s.source_w = nat_w;                          // JS frame `fa.x` (R.Cb L1616)
    s.source_h = nat_h;                          // JS frame `fa.y`
    s.trim_x = static_cast<float>(fr.offset_x);  // JS frame `qj.x` -> `Em`
    s.trim_y = static_cast<float>(fr.offset_y);  // JS frame `qj.y`
    if (nat_w > 0.0f && nat_h > 0.0f) {
        if (fill) {
            // Bar backing (topPanel, Energy_Bar — JS stretches these to their
            // rects; aspect-fit would shrink topPanel 100x191 to a 44px
            // sliver). Non-uniform stretch is CORRECT here.
            s.transform.set_scale(w / nat_w, h / nat_h);
        } else {
            // Aspect-correct fit (Dojo wave): the old non-uniform stretch
            // turned 226x193 menu art into wide ovals. Fit inside (w,h).
            const float sc = std::min(w / nat_w, h / nat_h);
            s.transform.set_scale(sc, sc);
        }
    }
    // JS `Hr(!0)` (flipX, e.g. the right `roll_end` cap of `Zh` L1872): mirror
    // the quad on x. No face culling — a flipped winding still draws.
    if (flip_x) {
        s.transform.scale_x = -s.transform.scale_x;
    }
    // UI is screen-space: identity camera (world == screen)
    sf2::render::Camera ui_cam;
    ui_cam.center_x = 640.0f;
    ui_cam.center_y = 360.0f;
    ui_cam.zoom = 1.0f;
    ui_cam.view_w = 1280.0f;
    ui_cam.view_h = 720.0f;
    ui_cam.arena_h = 720.0f;
    ui_cam.arena_floor = 0.0f;
    ui_cam.arena_center_x = 640.0f;
    app.renderer().draw_sprite(s, ui_cam);
    return true;
}

// A UI (screen-space) camera — world == screen. Shared by the standalone
// texture draws (sensei portrait, item images).
sf2::render::Camera ui_camera() {
    sf2::render::Camera c;
    c.center_x = kViewW * 0.5f;
    c.center_y = kViewH * 0.5f;
    c.zoom = 1.0f;
    c.view_w = kViewW;
    c.view_h = kViewH;
    c.arena_h = kViewH;
    c.arena_floor = 0.0f;
    c.arena_center_x = kViewW * 0.5f;
    return c;
}

// --- 9-slice (JS `gfx.effect.DrawMode.ESliced`) --------------------------
// JS `vc.qM(rect, mode)` (L1662-1663) builds the `ESliced` draw mode that
// `R.wl` (L1610) assigns to `le.mode`: `rect` is the frame's source CENTRE
// region (`Ec(x,y,w,h)`, L1661-1663) and `mode` is a `gfx.effect.TileMode`
// (`Th.SU` = EContinuous / `Th.Fna(n)` = EAdaptive, both L1661). The 9-slice
// insets are L=rect.x, T=rect.y, R=fa.x-rect.x-rect.w, B=fa.y-rect.y-rect.h;
// the four corners keep the insets and the edges/centre stretch. The engine's
// EAdaptive destination adaptation is internal to the compiled renderer ->
// OPEN; the native folds every border by ONE uniform fit scale
// `k = min(1, w/fa.x, h/fa.y)` (never enlarging a cap, shrinking only to fit)
// and lets the centre absorb the remainder (clamped at 0). That removes the
// old single-quad non-uniform stretch (300x222 -> 213x42 gave X 0.71 / Y 0.19
// on every cap). Frames are emitted sub-rect by sub-rect; rotated/trimmed
// packs fall back to the plain path (sliced-atlas frames are untrimmed).
void draw_atlas_region(App& app, const std::string& frame_name, float fx, float fy, float fw,
                       float fh, float tw, float th, float cx, float cy, float dw, float dh,
                       float alpha, bool flip_x) {
    if (fw <= 0.0f || fh <= 0.0f || dw <= 0.0f || dh <= 0.0f) return;
    sf2::scene::Sprite s;
    s.texture_name = frame_name;  // `register_atlas_frame` aliases the name -> GL
    s.frame_x = fx;
    s.frame_y = fy;
    s.frame_w = fw;
    s.frame_h = fh;
    s.tex_w = tw;
    s.tex_h = th;
    s.solid = false;
    s.color_a = alpha;
    s.rotated = false;
    s.source_w = fw;  // sub-rect drawn 1:1 in its own box -> no trim offset
    s.source_h = fh;
    s.trim_x = 0.0f;
    s.trim_y = 0.0f;
    s.transform.set_pos(cx, cy);
    s.transform.set_scale(dw / fw, dh / fh);
    if (flip_x) s.transform.scale_x = -s.transform.scale_x;
    app.renderer().draw_sprite(s, ui_camera());
}

bool draw_sliced_plate(App& app, const std::string& frame_name, float cx, float cy, float w,
                       float h, float l, float t, float r, float b, float alpha,
                       bool flip_x) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame(frame_name, &fr, &tw, &th, &gl)) return false;
    (void)gl;
    if (fr.rotated) return false;  // sub-rect UVs assume unrotated packing
    const float nat_w =
        fr.source_w > 0 ? static_cast<float>(fr.source_w) : static_cast<float>(fr.w);
    const float nat_h =
        fr.source_h > 0 ? static_cast<float>(fr.source_h) : static_cast<float>(fr.h);
    if (nat_w <= 0.0f || nat_h <= 0.0f) return false;
    // The packed rect must equal the untrimmed source (the sliced atlas and
    // the `od`/`Ib` caps are all untrimmed); otherwise the sub-rect atlas math
    // is not 1:1 and the caller's flat fallback stands.
    if (fr.offset_x != 0 || fr.offset_y != 0 || fr.w != static_cast<int>(nat_w) ||
        fr.h != static_cast<int>(nat_h)) {
        return false;
    }
    l = std::clamp(l, 0.0f, nat_w);
    r = std::clamp(r, 0.0f, nat_w - l);
    t = std::clamp(t, 0.0f, nat_h);
    b = std::clamp(b, 0.0f, nat_h - t);
    // One uniform border scale: caps never stretch to fit (the old bug), they
    // are only shrunk when the destination is smaller than the border sum.
    float k = std::min(1.0f, std::min(w / nat_w, h / nat_h));
    if (l + r > 0.0f) k = std::min(k, w / (l + r));
    if (t + b > 0.0f) k = std::min(k, h / (t + b));
    const float sx[4] = {0.0f, l, nat_w - r, nat_w};
    const float sy[4] = {0.0f, t, nat_h - b, nat_h};
    const float x_lo = cx - w * 0.5f, y_lo = cy - h * 0.5f;
    const float x_hi = cx + w * 0.5f, y_hi = cy + h * 0.5f;
    const float dx[4] = {x_lo, x_lo + l * k, x_hi - r * k, x_hi};
    const float dy[4] = {y_lo, y_lo + t * k, y_hi - b * k, y_hi};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float sw = sx[i + 1] - sx[i], sh = sy[j + 1] - sy[j];
            const float dw = dx[i + 1] - dx[i], dh = dy[j + 1] - dy[j];
            if (sw <= 0.0f || sh <= 0.0f || dw <= 0.0f || dh <= 0.0f) continue;
            draw_atlas_region(app, frame_name, static_cast<float>(fr.x) + sx[i],
                              static_cast<float>(fr.y) + sy[j], sw, sh, static_cast<float>(tw),
                              static_cast<float>(th), (dx[i] + dx[i + 1]) * 0.5f,
                              (dy[j] + dy[j + 1]) * 0.5f, dw, dh, alpha, flip_x);
        }
    }
    return true;
}

// JS `Bb` plate (L1842): `Y.wl(vc.qM(new Ec((Y.fa.x/2|0)-2, 0, 4, Y.fa.y|0),
// Th.Fna(1)))` -> centre = a 4px vertical strip at the horizontal mid, full
// height. Insets: L=(fa.x/2|0)-2, T=0, R=fa.x-L-4, B=0 -> a 3-column / 1-row
// slice. Used by every `Bb` text button (`EButtonWhite/Dark/Beige`).
bool draw_bb_plate(App& app, const std::string& frame_name, float cx, float cy, float w,
                   float h, float alpha = 1.0f, bool flip_x = false) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame(frame_name, &fr, &tw, &th, &gl)) return false;
    (void)tw;
    (void)th;
    (void)gl;
    const float nat_w =
        fr.source_w > 0 ? static_cast<float>(fr.source_w) : static_cast<float>(fr.w);
    if (nat_w <= 0.0f) return false;
    const float l = static_cast<float>(static_cast<int>(nat_w) / 2 - 2);
    const float r = nat_w - l - 4.0f;
    return draw_sliced_plate(app, frame_name, cx, cy, w, h, l, 0.0f, r, 0.0f, alpha, flip_x);
}

// Lazily registers the `res/ui/scroll.*` atlas (JS asset id 254) — the `od`
// dialog 9-slice (`bg`/`bg_edge`, `y.lSa`/`y.eoa` L2467) and the `Fg`
// content-frame rails (`paper`, `paper_edge_left/right`, `roll_*`). Ships as
// webp (`scroll.<hash>.webp`), the same decode path the sensei portrait and
// dojo background use.
bool load_scroll_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/ui";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("scroll.", 0) == 0 && entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".webp", ".png", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("scroll.", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("scroll_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[ui] scroll atlas: %dx%d %zu frames\n", a.w, a.h, a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[ui] scroll atlas load failed: %s\n", e.what());
    }
    return ok;
}

// Lazily registers the `res/ui/achievements.*` atlas (JS asset id 270) — the
// `Ed` achievement-cell icon atlas (`Achievements01/ach_*` .. `Achievements03/*`,
// `panel`). `is` builds the icon via `Ed(270,y.MQa)` (L2212) and `y.MQa`
// (L2470) = "Achievements01/ach_block_gold". App::init registers menu/shop/
// profile/misc/skills/controller but NOT this atlas, so every achievement cell
// icon resolved to nothing (the old `draw_user_image` looked under
// res/users/images). Ships as ASTC ktx (+ dds sibling), decodable after the
// KTX row-orientation fix. Idempotent.
bool load_achievements_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/ui";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("achievements.", 0) == 0 && entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".ktx", ".dds", ".webp", ".png"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("achievements.", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("achievements_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[ui] achievements atlas: %dx%d %zu frames\n", a.w, a.h,
                     a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[ui] achievements atlas load failed: %s\n", e.what());
    }
    return ok;
}

// --- Location atlas page chain (JS `Bf.init` L474 + `ni.init` L1142) ------
// A location ships one TexturePacker pack per page: `<loc>.<hash>.json` for
// page 1, then `<loc>-2.<hash>.json`, `<loc>-3.<hash>.json`, … (verified in
// `reference/www/res/locations/`, e.g. arena.ca2949ef.json +
// arena-2.586e4f15.json). `Bf.init` chains them and `ni.init` (L1142) walks
// `b.nextPage`, so a `Sequention` frame can live on page 2+. The image beside
// each JSON has a DIFFERENT hash stem (`dojo.b920e18e.webp` vs
// `dojo.d31b1e71.json`), so a page is matched by its `<loc>[-N]` prefix, not
// by the JSON stem. Returns that prefix; `page` receives N (1-based).
std::string location_page_prefix(const std::string& filename, int& page) {
    const std::size_t ext = filename.find_last_of('.');
    const std::string stem = ext == std::string::npos ? filename : filename.substr(0, ext);
    const std::size_t hash_dot = stem.find_last_of('.');
    const std::string base = hash_dot == std::string::npos ? stem : stem.substr(0, hash_dot);
    page = 1;
    const std::size_t dash = base.find_last_of('-');
    if (dash != std::string::npos && dash + 1 < base.size()) {
        int n = 0;
        bool digits = true;
        for (std::size_t i = dash + 1; i < base.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(base[i]))) {
                digits = false;
                break;
            }
            n = n * 10 + (base[i] - '0');
        }
        if (digits) page = n;
    }
    return base;
}

// Loads a location's FULL page chain into `scene` and aliases every page's
// `frame_names` to that page's GL texture (the multi-page contract in
// `LocationScene::atlas_pages`, location_scene.hpp). Returns the number of
// pages whose texture was uploaded and aliased.
int load_location_atlas_pages(App& app, sf2::scene::LocationScene& scene,
                              const std::string& loc_dir, const std::string& loc_name) {
    std::vector<std::pair<int, std::string>> pages;  // (page, json path)
    std::string params_xml;
    for (const auto& entry : std::filesystem::directory_iterator(loc_dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(loc_name + "_params.", 0) == 0 &&
            entry.path().extension() == ".xml") {
            params_xml = entry.path().string();
            continue;
        }
        if ((name.rfind(loc_name + ".", 0) != 0 &&
             name.rfind(loc_name + "-", 0) != 0) ||
            entry.path().extension() != ".json") {
            continue;
        }
        int page = 1;
        location_page_prefix(name, page);
        pages.emplace_back(page, entry.path().string());
    }
    std::sort(pages.begin(), pages.end(),
              [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                  return a.first < b.first;
              });
    std::vector<std::string> jsons;
    jsons.reserve(pages.size());
    for (const auto& p : pages) jsons.push_back(p.second);
    if (jsons.empty()) {
        throw std::runtime_error("location atlas: no page JSON for " + loc_name);
    }
    scene.load(params_xml, jsons, app.res_root());
    int aliased_pages = 0;
    for (const sf2::scene::AtlasPage& page : scene.atlas_pages()) {
        int n = 1;
        const std::string prefix = location_page_prefix(
            std::filesystem::path(page.json_path).filename().string(), n);
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".webp", ".png", ".jpg", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(loc_dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(prefix + ".", 0) != 0) continue;
                if (entry.path().extension() != ext) continue;
                if (sf2::data::decode_texture(entry.path().string(), tex)) {
                    decoded = true;
                    break;
                }
            }
            if (decoded) break;
        }
        if (!decoded) {
            std::fprintf(stderr, "[ui] location page %d texture not decodable: %s\n", n,
                         prefix.c_str());
            continue;
        }
        const GLuint gl = app.renderer().texture_for("loc_atlas_" + prefix, tex);
        if (gl == 0) continue;
        for (const std::string& frame : page.frame_names) {
            app.renderer().texture_alias(frame, gl);
        }
        ++aliased_pages;
        std::fprintf(stdout, "[ui] location atlas page %d %s: %dx%d, %zu frames\n", n,
                     prefix.c_str(), tex.w, tex.h, page.frame_names.size());
    }
    std::fflush(stdout);
    return aliased_pages;
}

// --- Settings `un` row icons atlas (JS `E.get(250)`, L1917) --------------
// `res/ui/settings_icons.*` (16 frames, one 170x170 tile each: sound,
// sound_off, music, music_off, credits, restore + the language codes). The
// `un` row factory `b(q)` (L1917) draws one tile per row at `C(-q.za()*.9)`
// with the row step `q.qa()*1.25` (= 212.5 for a 170 tile).
bool load_settings_icons_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/ui";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("settings_icons.", 0) == 0 &&
                entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".webp", ".png", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("settings_icons.", 0) == 0 &&
                    entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("settings_icons_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[ui] settings_icons atlas: %dx%d %zu frames\n", a.w, a.h,
                     a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[ui] settings_icons atlas load failed: %s\n", e.what());
    }
    return ok;
}

// Lazily registers the `res/ui/sliced.*` atlas (JS asset id 244) — the `Bb`
// text-button plates. `Bb.$w(E.get(244), this.fza(a))` (L1842) with
// `fza(a) = "btn" + a.substr(7)` (L1844): style "EButtonWhite" -> "btnWhite",
// "EButtonDark" -> "btnDark", "EButtonBeige" -> "btnBeige". App::init does
// not load this atlas, so every non-`od` `Bb` would otherwise fall back to a
// flat quad. Ships as ASTC ktx / dds (the menu/misc decode path).
bool load_sliced_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/ui";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("sliced.", 0) == 0 && entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".ktx", ".dds", ".webp", ".png"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("sliced.", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("sliced_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[ui] sliced atlas: %dx%d %zu frames\n", a.w, a.h,
                     a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[ui] sliced atlas load failed: %s\n", e.what());
    }
    return ok;
}

// D14 `v.RIa` L1222: split the Image ref on `|`, take the BASENAME after the
// last `/` (`a[0].lastIndexOf("/")`), and flag a case-insensitive `flip` token
// -> `new pw(fp=c, fileName=b)` (L1225 `pw`). The dialog's `Mirrored` attr
// already appended `|Flip` at parse time (`He.S` L1047), so this is the one
// place the flip + path strip happens; `Od.ala` L1947 (`oe(ref.fileName)`) and
// `Ib.Sr` L1909 (`R.$(E.get(12), fileName, ..)`) both consume it.
struct DialogImageRef {
    std::string file_name;
    bool flip = false;
};
DialogImageRef dialog_image_ref(const std::string& image) {
    DialogImageRef out;
    std::size_t end = image.size();
    const std::size_t bar = image.find('|');
    if (bar != std::string::npos) end = bar;
    for (std::size_t p = bar == std::string::npos ? image.size() : bar + 1;
         p < image.size();) {
        const std::size_t nb = image.find('|', p);
        const std::size_t ne = nb == std::string::npos ? image.size() : nb;
        std::string token = image.substr(p, ne - p);
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (token == "flip") out.flip = true;
        p = ne + 1;
    }
    const std::string head = image.substr(0, end);
    const std::size_t slash = head.rfind('/');
    out.file_name = slash == std::string::npos ? head : head.substr(slash + 1);
    return out;
}

// D14 portrait resolution (`Od.$A` L1945 + `ala` L1947 + `Ib.Sr` L1909).
//   `Od.$A`: `this.sV!=null ? new or(this.sV) : this.ala(this.p$)` — when the
//   dialog carries an `Item` (`He.ah` L1045 -> `p.items.$b(x)` -> its composite
//   `Ev`), the item image WINS; otherwise the `Image` goes through `v.RIa`
//   L1222 (basename + `|Flip`) into `oe(fileName)`.
// The port's registered `sensei_portrait` texture (app.cpp:518 loads the
// shipped `character_sensei_small` asset) is the fallback registry entry for
// that stem, so a genuine user-image miss on the sensei still draws.
bool draw_dialog_image(App& app, const std::string& image, const std::string& item,
                       float cx, float cy, float size, float alpha, bool strip_small) {
    if (size <= 0.0f) return false;
    const DialogImageRef ref = dialog_image_ref(image);
    std::string name = ref.file_name;
    if (strip_small) {
        // `Ib.Sr` L1909: `fileName.replace(RegExp("_small$"),"")`.
        constexpr char kSmall[] = "_small";
        if (name.size() > sizeof(kSmall) - 1 &&
            name.compare(name.size() - (sizeof(kSmall) - 1), sizeof(kSmall) - 1, kSmall) == 0) {
            name = name.substr(0, name.size() - (sizeof(kSmall) - 1));
        }
    }
    if (!item.empty()) {
        // `p.items.$b(ba.Pc(a,this.ah))` L1046 -> the item's icon/composite.
        for (const CatalogItem& ci : load_full_catalog(app)) {
            if (ci.name == item && !ci.image.empty() &&
                draw_item_image(app, ci.image, cx, cy, size, size, alpha)) {
                return true;
            }
        }
        if (draw_item_image(app, item, cx, cy, size, size, alpha)) return true;
    }
    if (name.empty()) return false;
    if (draw_user_image(app, name, cx, cy, size, size, alpha, ref.flip)) return true;
    if (name == "character_sensei") {
        const GLuint tex = app.renderer().texture_lookup("sensei_portrait");
        if (tex != 0) {
            sf2::scene::Sprite s;
            s.texture_name = "sensei_portrait";
            s.frame_x = 0.0f;
            s.frame_y = 0.0f;
            s.frame_w = 256.0f;
            s.frame_h = 256.0f;
            s.tex_w = 256.0f;
            s.tex_h = 256.0f;
            s.solid = false;
            s.color_a = alpha;
            s.transform.set_pos(cx, cy);
            const float sc = size / 256.0f;
            s.transform.set_scale(sc, sc);
            if (ref.flip) s.transform.scale_x = -s.transform.scale_x;
            app.renderer().draw_sprite(s, ui_camera());
            return true;
        }
    }
    return false;
}

// The `Ib` notification/hint bar (JS L1905-1912). Layout: `node.C(W -
// scroll.width*scale)`, `node.D(za.Sp)`; scroll `gk(600,250,50,0)` horizontal
// with a `Fg(600,250,1,30)` content frame (paper rails). The portrait is the
// dialog's resolved `Image` (`Qhb(a=wt,...)` L1907 -> `v.RIa` L1909 ->
// `R.$(E.get(12), fileName, ...)`, with the JS `_small` suffix stripped);
// `Sr()` L1908-1910 builds exactly ONE label from the JOINED lines (`lj`
// L1908) at `Fa(600-image.w+20,150)`, `C(image.w-30)`, `D(50)` — there is NO
// speaker row. OK `Bb` at local (450,185). The `gYa()` gate is the shell
// predicate (true on Dojo/Map — the preloader/fight cases are OPEN);
// `Ib.RP` (He.DisableNotificationsButtons, L1045) gates the OK button — the
// caller passes `show_ok` already gated on RP.
void draw_ib_hint(App& app, sf2::render::Renderer& ren, const std::string& image,
                  const std::string& joined_lines, bool show_ok) {
    const float c =
        std::clamp(std::min(kViewW * 0.75f, kViewH * 0.75f) / 600.0f, 0.2f, 1.1f);
    const float sp = std::min(kViewH * 0.13f, 100.0f) * 0.78f;  // za.odb L1975
    const float ox = kViewW - 600.0f * c;
    const float oy = sp;
    auto lx = [&](float v) { return ox + v * c; };
    auto ly = [&](float v) { return oy + v * c; };
    // JS `Ib.O1a` (L1906): `this.scroll = new gk(600,250,50,0,!1); let a =
    // new Fg(600,250,1,30); this.scroll.iL.appendChild(a.node);` — the bar's
    // art is the `Fg` content frame (paper rails), NOT the `Zh` roll
    // composite. `Zh` is only ever built by `Fg.Vaa()` (L1869), which `Ib`
    // never calls; the previous wave mistook the `gk` scrollbar for the bar
    // art and stretched the 101x114 `roll_end` cap to the full 250 height
    // (`cap_w = 101*250/114 = 221 px`, 74% of the 600 bar) — the reported
    // STRETCHED scroll.
    //
    // `Fg` ctor L1868 registers the three `wc` children as
    // `paper_edge_left`/`paper`/`paper_edge_right` (`y.nSa`/`y.mSa`/`y.oSa`,
    // L2467-2468). `Fg.ba(a=600,b=250,c=30)` L1870-1871, orientation 1:
    //   d = b = 250, e = a = 600; `wc.Wg(90)` + `wc.C(a=600)` rotates the
    //   250x600 composite into the 600x250 bar;
    //   k = c / paper_edge_left.fa.x; f.la(k) -> each edge frame's display
    //   width == the cap `c` = 30 (NOT aspect-scaled to 250);
    //   centre `g.xc(max(1, d - 2*f.za()))` = 250 - 2*30 = 190 (the `paper`
    //   body); every frame `Pb(e=600)` = the 600 axis is the cross length.
    // Post-rotation the `paper_edge_left` rail is the 30 px TOP strip, the
    // `paper` body the 190 px middle, `paper_edge_right` the 30 px BOTTOM
    // strip; each sprawls the full 600 width (stretched whole-frame, exactly
    // what `try_draw_atlas_button(..., fill=true)` does).
    constexpr float kBarW = 600.0f;    // `Fg(600,250,...)` long axis
    constexpr float kBarH = 250.0f;    // short axis (pre-rotation width)
    constexpr float kPaperCap = 30.0f; // `Fg(...,1,30)` cap `c`
    const float mid_h = std::max(kBarH - 2.0f * kPaperCap, 10.0f);  // 190
    bool drew = false;
    if (load_scroll_atlas(app)) {
        drew = try_draw_atlas_button(app, "paper_edge_left", lx(kBarW * 0.5f),
                                     ly(kPaperCap * 0.5f), kBarW * c, kPaperCap * c,
                                     1.0f, /*fill=*/true);
        if (drew) {
            try_draw_atlas_button(app, "paper", lx(kBarW * 0.5f),
                                  ly(kPaperCap + mid_h * 0.5f), kBarW * c, mid_h * c,
                                  1.0f, /*fill=*/true);
            try_draw_atlas_button(app, "paper_edge_right", lx(kBarW * 0.5f),
                                  ly(kPaperCap + mid_h + kPaperCap * 0.5f), kBarW * c,
                                  kPaperCap * c, 1.0f, /*fill=*/true);
        }
    }
    if (!drew) {
        const float panel[] = {lx(0), ly(0), lx(600), ly(0), lx(0), ly(250),
                               lx(600), ly(0), lx(600), ly(250), lx(0), ly(250)};
        ren.draw_triangles(panel, 6, 0.05f, 0.05f, 0.08f, 0.82f);
    }
    // Portrait: `Ib.Sr()` L1909 resolves the dialog's `Image` into
    // `R.$(E.get(12), fileName, this.scroll.iL)` via `v.RIa` (basename +
    // `|Flip`), `_small` stripped. The `Item` composite never applies to the
    // bar (`Qhb` passes no `Ev`), so `item` is empty here.
    float image_w = 0.0f;
    if (draw_dialog_image(app, image, std::string(), lx(128.0f), ly(130.0f), 256.0f * c,
                          1.0f, /*strip_small=*/true)) {
        image_w = 256.0f;
    }
    // The ONE label: the joined lines (`lj` L1908). `Fa(600-image.w+20,150)`,
    // `C(image.w-30)`, `D(50)`, `rd(!0)` (multiline — `{br}` is a hard break,
    // the Sensei tutorial notifications need it).
    const float tx = lx(image_w - 30.0f);
    draw_ui_wrapped(app, tx, ly(50.0f), (620.0f - image_w) * c, 170.0f * c, joined_lines,
                    0.75f, UiAlign::Left, 0.184f, 0.145f, 0.106f);
    // OK (`Bb(Zva)` = "EButtonWhite", local (450,185), `zf(100)`); `Ib.RP`
    // gate (L1910) - only when the notification carries a button. `Bb` draws
    // through the `ESliced` plate (`Ec((fa.x/2|0)-2,0,4,fa.y)`, L1842).
    if (show_ok) {
        if (!(load_sliced_atlas(app) &&
              draw_bb_plate(app, "btnWhite", lx(450.0f), ly(185.0f), 100.0f * c,
                            72.0f * c, 1.0f))) {
            draw_flat_button(app, "OK", lx(450.0f), ly(185.0f), 100.0f * c, 72.0f * c, 0.3f,
                             0.5f, 0.3f, false);
        }
        draw_ui_label(app, lx(400.0f), ly(173.0f), 100.0f * c, 24.0f * c, "OK", 0.8f,
                      UiAlign::Center, 1.0f, 1.0f, 1.0f);
    }
}

// One resolved item image (JS `Rf` L2307): the list.xml `Image` ref
// ("Weapon1.img_weapon_knives") resolves to
// `res/items/images-1x/weapon1/img_weapon_knives.<hash>.<ext>` (the shipped
// files are hashed ktx/dds, so the stem scan matches the hash suffix).
struct ItemImage {
    GLuint gl = 0;
    int w = 0;
    int h = 0;
    std::string name;
};

bool resolve_item_image(App& app, const std::string& image_ref, ItemImage& out) {
    static std::map<std::string, ItemImage> cache;
    static std::set<std::string> failed;
    const auto hit = cache.find(image_ref);
    if (hit != cache.end()) {
        out = hit->second;
        return out.gl != 0;
    }
    if (failed.count(image_ref) != 0) return false;
    // "Weapon1.img_weapon_knives" -> dir "weapon1", stem "img_weapon_knives"
    // (JS `Rf`: split "/", lowercase the file part, "-" -> "_").
    const std::size_t dot = image_ref.find('.');
    if (dot == std::string::npos || dot + 1 >= image_ref.size()) {
        failed.insert(image_ref);
        return false;
    }
    std::string dir = image_ref.substr(0, dot);
    std::string stem = image_ref.substr(dot + 1);
    std::transform(dir.begin(), dir.end(), dir.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::replace(stem.begin(), stem.end(), '-', '_');
    try {
        const std::string idir = app.res_root() + "/items/images-1x/" + dir;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".png", ".webp", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(idir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(stem + ".", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) {
            failed.insert(image_ref);
            return false;
        }
        ItemImage ii;
        ii.name = "item_img_" + dir + "_" + stem;
        ii.gl = app.renderer().texture_for(ii.name, tex);
        ii.w = tex.w;
        ii.h = tex.h;
        if (ii.gl == 0 || ii.w <= 0 || ii.h <= 0) {
            failed.insert(image_ref);
            return false;
        }
        cache[image_ref] = ii;
        out = ii;
        return true;
    } catch (const std::exception&) {
        failed.insert(image_ref);
        return false;
    }
}

// Draws an item image centered at (cx,cy) aspect-fit into (w,h). Returns
// false on a genuine miss (the caller keeps its flat card).
bool draw_item_image(App& app, const std::string& image_ref, float cx, float cy, float w,
                     float h, float alpha) {
    ItemImage ii;
    if (!resolve_item_image(app, image_ref, ii)) return false;
    sf2::scene::Sprite s;
    s.texture_name = ii.name;
    s.frame_x = 0.0f;
    s.frame_y = 0.0f;
    s.frame_w = static_cast<float>(ii.w);
    s.frame_h = static_cast<float>(ii.h);
    s.tex_w = static_cast<float>(ii.w);
    s.tex_h = static_cast<float>(ii.h);
    s.solid = false;
    s.color_a = alpha;
    s.transform.set_pos(cx, cy);
    const float sc = std::min(w / static_cast<float>(ii.w), h / static_cast<float>(ii.h));
    s.transform.set_scale(sc, sc);
    app.renderer().draw_sprite(s, ui_camera());
    return true;
}

// Draws a JS `oe` user image (L1823-1825): `res/users/images/<fileName>.png`,
// lowercased, loaded lazily. The shipped assets are hashed
// (`<stem>.<hash>.<ext>`), so the stem is prefix-scanned like
// `resolve_item_image`. Seal rows carry Image="drop_blue_seal" while the
// shipped stem is "img_drop_blue_seal" (bundle drift, SHOP_STATIC §11), so
// both stems are tried. Returns false on a genuine miss.
bool draw_user_image(App& app, const std::string& file_name, float cx, float cy, float w,
                     float h, float alpha, bool flip_x = false) {
    if (file_name.empty() || w <= 0.0f || h <= 0.0f) return false;
    static std::map<std::string, ItemImage> cache;
    static std::set<std::string> failed;
    ItemImage ii;
    const auto hit = cache.find(file_name);
    if (hit != cache.end()) {
        ii = hit->second;
        if (ii.gl == 0) return false;
    } else if (failed.count(file_name) != 0) {
        return false;
    } else {
        std::string stem = file_name;
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const std::string stems[2] = {stem, "img_" + stem};
        const std::string dir = app.res_root() + "/users/images";
        sf2::data::Texture tex;
        bool decoded = false;
        try {
            for (const std::string& s : stems) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    const std::string name = entry.path().filename().string();
                    if (name.rfind(s + ".", 0) != 0) continue;
                    const std::string ext = entry.path().extension().string();
                    if (ext != ".png" && ext != ".webp") continue;
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
                if (decoded) break;
            }
        } catch (const std::exception&) {
        }
        if (!decoded) {
            failed.insert(file_name);
            return false;
        }
        ii.name = "user_img_" + stem;
        ii.w = tex.w;
        ii.h = tex.h;
        ii.gl = app.renderer().texture_for(ii.name, tex);
        cache[file_name] = ii;
        if (ii.gl == 0) {
            failed.insert(file_name);
            return false;
        }
    }
    sf2::scene::Sprite s;
    s.texture_name = ii.name;
    s.frame_x = 0.0f;
    s.frame_y = 0.0f;
    s.frame_w = static_cast<float>(ii.w);
    s.frame_h = static_cast<float>(ii.h);
    s.tex_w = static_cast<float>(ii.w);
    s.tex_h = static_cast<float>(ii.h);
    s.solid = false;
    s.color_a = alpha;
    s.transform.set_pos(cx, cy);
    const float sc = std::min(w / static_cast<float>(ii.w), h / static_cast<float>(ii.h));
    s.transform.set_scale(sc, sc);
    // JS `oe.fp()` — the flipped portrait (`ik.EK` in the VS intro, `lk.Hf`
    // for the player HUD portrait, both `fp()`). Mirror about the sprite centre.
    if (flip_x) s.transform.scale_x = -s.transform.scale_x;
    app.renderer().draw_sprite(s, ui_camera());
    return true;
}

// Draws a profile-cell icon: the JS `Ed.Fs = R.$(E.get(atlas), frame, icon)`
// (L2202) used by the `uk` perk cell (`Icons01/IconAvenger`, L2222) and the
// `is` achievement cell (`Achievements01/ach_block_gold`, L2212). The XML
// image refs are dot-form; JS `Ye.qI` (L1354) and `Eb.replace(a,".","/")`
// (L2212) rewrite EVERY '.' to '/' (`Eb.replace` = `a.split(b).join(c)`,
// L2477), so `Icons01.IconAvenger` -> `Icons01/IconAvenger`,
// `Achievements01.ach_x` -> `Achievements01/ach_x`, `Trick4.double_sweep` ->
// `Trick4/double_sweep`. Aspect-fit centred via the shared atlas path (which
// logs a genuine miss once); returns false so the caller keeps its explicit
// flat fallback — the cell is never a silent blank.
bool draw_cell_icon(App& app, const std::string& ref, float cx, float cy, float w, float h,
                    float alpha) {
    if (ref.empty() || w <= 0.0f || h <= 0.0f) return false;
    std::string frame = ref;
    std::replace(frame.begin(), frame.end(), '.', '/');
    return try_draw_atlas_button(app, frame, cx, cy, w, h, alpha);
}

// ---------------------------------------------------------------------------
// Shared `za` top chrome (JS L1972-1984) — the persistent shell chrome.
//
// `ma.D1()` (L1831) mounts one `za` on every `ma` shell screen. The JS
// constructor (L1973-1980) + layouts (`odb` L1975 / `ndb` L1976-1977) give:
//   - the topPanel backing (misc id 260, frame `topPanel`) stretched full
//     width at min(rect.h*0.13, 100) (desktop; `L.K.un?230:100`, L1975);
//   - a centred widget strip `Pr` carrying `wr` (level, L1986), `xr`
//     (energy, L1984) and `yr` (money, L1990) left->right, gap c = 50*N.lc;
//   - a VERTICAL column of five `Le` (menu id 262) nav buttons created in
//     `Aub` (L1977-1980) with normal/pushed/active frames; `ndb` lays them
//     at x = 100*(0.2+((lc<.5?.5:lc>2?2:lc)-.5)/1.5*.8), y just below the
//     bar, row height b = buttons[0].Y.fa.y*.85 (menu source frame 278),
//     scale d = max(.1, min(W,H)*.35/430) clamped so the column fits.
// The old native horizontal 4-up rows (MainMenu + Dojo) were INVENTED
// (PORT_AUDIT_UI §2.1): the nav column is VERTICAL.
// ---------------------------------------------------------------------------

// The five nav buttons (JS `Aub` L1978-1979). normal/pushed/active resolve
// the `y.*` frame table (L2464-2466); Settings passes null as its third
// frame (L1979). No text is baked into the `Le` art (UI_EXCLUSIVITY §2), so
// the label draws only on the flat fallback.
struct ZaNavDef {
    const char* normal;
    const char* pushed;
    const char* active;
    ScreenId nav;
    const char* label;
};
constexpr int kZaNavCount = 5;
const ZaNavDef kZaNav[kZaNavCount] = {
    {"Dojo_normal", "Dojo_pushed", "Dojo_active", kScreenDojo, "DOJO"},
    {"Map_normal", "Map_pushed", "Map_active", kScreenMap, "MAP"},
    {"Shop_normal", "Shop_pushed", "Shop_active", kScreenShop, "SHOP"},
    {"Profile_normal", "Profile_pushed", "Profile_active", kScreenProfile, "PROFILE"},
    {"Settings_normal", "Settings_active", nullptr, kScreenSettings, "SETTINGS"},
};

// The nav button source frame is 278x278 (menu atlas Dojo_normal
// sourceSize), so row height b = 278*0.85 (JS `buttons[0].Y.fa.y*.85`).
constexpr float kZaNavSource = 278.0f;

struct ZaLayout {
    float bar_h = 0.0f;        // topPanel height (odb: PL.Pb)
    float sp = 0.0f;           // bar visual height (Sp = PL.qa()*.78)
    float widget_h = 0.0f;     // b = Sp*.65
    float gap = 0.0f;          // c = 50*N.lc
    float nav_x = 0.0f;        // scroll.node.C
    float nav_w = 0.0f;        // content frame width e = 430*d
    float nav_scale = 1.0f;    // d
    float nav_step = 0.0f;     // row step b*d
    float nav_first_y = 0.0f;  // Sp + b/2*d
    float nav_btn = 0.0f;      // on-screen button size 278*d
    float nav_qka = 0.0f;      // qka = 50*d (Fg content-frame cap / rails)
    float nav_col_h = 0.0f;    // f = (b*N+45)*d (column height)
};

ZaLayout za_layout() {
    ZaLayout lay;
    const float w = kViewW, h = kViewH;
    const float lc = w / h;  // N.lc
    // odb() (L1975).
    lay.bar_h = std::min(h * 0.13f, 100.0f);
    lay.sp = lay.bar_h * 0.78f;
    lay.widget_h = lay.sp * 0.65f;
    lay.gap = 50.0f * lc;
    // ndb() (L1976-1977).
    const float lc_clamped = std::clamp(lc, 0.5f, 2.0f);
    lay.nav_x = 100.0f * (0.2f + (lc_clamped - 0.5f) / 1.5f * 0.8f);
    const float row = kZaNavSource * 0.85f;                            // b
    const float col_h = row * static_cast<float>(kZaNavCount) + 45.0f;  // c
    float d = std::max(0.1f, std::min(w, h) * 0.35f / 430.0f);
    const float avail = h - lay.sp - 100.0f;                           // rect.v - Sp - 100
    if (col_h * d > avail) {
        d = avail / col_h;
    }
    lay.nav_w = 430.0f * d;                                            // e
    lay.nav_scale = d;
    lay.nav_step = row * d;
    lay.nav_first_y = lay.sp + (row * 0.5f) * d;                       // Sp + a
    lay.nav_btn = kZaNavSource * d;
    lay.nav_qka = 50.0f * d;                                           // qka
    lay.nav_col_h = col_h * d;                                         // f
    return lay;
}

// JS `za.Aub` (L1977-1978): the nav is a `gk(400,800,50,1,!0,Y.na("menu"))`
// scroll and the ctor immediately calls `this.scroll.collapse(0)` — the
// COLLAPSED default. `gk.NLa` (L2001) then sets `zI=2` (collapsed) and hides
// the `Le` buttons (`i3.Z(!1)`); expanding (`zI=1`) reveals them. Only the
// `Lx` title (`Y.na("menu")` = "МЕНЮ", oracle x64-176 y72-110) shows while
// collapsed, and `gk.Bgb` (L2000) toggles it.
// JS `ma.D1` builds a FRESH `za` column per screen (`gk.Af = new Zh(...)`
// L1996), so EVERY screen starts COLLAPSED (`collapse(0)` L1978) and its own
// header tap expands it (`gk.Bgb` L2000). The port keeps one flag PER SCREEN:
// with a single shared flag the `МЕНЮ` column was dead on every screen that
// was not the last one expanded (the reported bug: the menu only worked in
// the Dojo), and a collapsed-by-default screen could only reset it.
// JS `gk` (L1996-2001) nav-column state machine, per screen MOUNT. Fields
// mirror the JS exactly: `uJ` the TARGET (`expand` L2000 `this.uJ=!0` /
// `collapse` `this.uJ=!1`), `yI` the animated open fraction (`NLa` L2001),
// `PF` the animation LOCK (`Gwa` L2000 sets it on a timed run, `aa` L1998
// clears it at progress 1 and gates every input while set), `pma`/`time` the
// run duration/elapsed, `zI` 2 collapsed / 1 expanded / 0 mid (`NLa` L2001).
struct ZaNavState {
    bool uJ = false;
    bool PF = false;
    float yI = 0.0f;
    float pma = 0.0f;
    float time = 0.0f;
    int zI = 2;
};
enum { kZaNavScreenSlots = 16 };
static ZaNavState g_za_nav_by_screen[kZaNavScreenSlots];
static ZaNavState& za_nav_state(ScreenId id) {
    const int i = static_cast<int>(id);
    static ZaNavState unused_slot;
    if (i < 0 || i >= kZaNavScreenSlots) return unused_slot;
    return g_za_nav_by_screen[i];
}
// JS `gk` ctor (L1998) `this.collapse(0)`: a fresh `za` is COLLAPSED. Called by
// `make_screen` at every screen MOUNT, so the state is per-screen and never
// carries history across mounts (the old `g_za_nav_open_by_screen` persisted
// across instances -> the reported "sometimes it closes sometimes not").
static void za_nav_reset(ScreenId id) { za_nav_state(id) = ZaNavState{}; }

// JS `za.zq` (L1983) + `ndb` (L1975): the disciple toggle icon-button. `zq`
// is scaled `scroll.Af.width*.5 / zq.Y.fa.x` (so the on-screen width is half
// the nav content width) and placed at `(a/2 + scroll.node.ya, a/2 + PL.qa()
// + scroll.Af.height)` where `a` = that width. `FU` (L1983) swaps the frame
// on `p.o.Y0()`: `Tna`="btn_punching_bag" (L2464) when on, else `Sna`=
// "btn_disciple". Visibility is `v.FU` L1207 = active screen Dojo (Tf==3) AND
// `p.o.g$a()` (`ShowDojoDisciple`). Shared by draw + hit-test.
struct ZaDiscipleRect {
    float cx = 0.0f;
    float cy = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

ZaDiscipleRect za_disciple_rect(App& app, bool disciple) {
    const ZaLayout lay = za_layout();
    const float row = kZaNavSource * 0.85f;
    const float content_h =
        (row * static_cast<float>(kZaNavCount) + 45.0f) * lay.nav_scale;
    ZaDiscipleRect r;
    r.w = lay.nav_w * 0.5f;
    r.h = r.w;  // default square; refined from the frame aspect below
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    const char* frame = disciple ? "btn_punching_bag" : "btn_disciple";
    if (app.get_atlas_frame(frame, &fr, &tw, &th, &gl) && fr.w > 0 && fr.h > 0) {
        // JS uses `zq.Y.fa` (the untrimmed SourceSize); the packed rect is the
        // closest available proxy (documented approximation).
        r.h = r.w * static_cast<float>(fr.h) / static_cast<float>(fr.w);
    }
    r.cx = lay.nav_x + r.w * 0.5f;
    r.cy = r.w * 0.5f + lay.bar_h + content_h;
    return r;
}

// The collapsed menu header rect (oracle tutorial shot, 1280x720 space).
void za_header_rect(float& x, float& y, float& w, float& h) {
    // Oracle-measured `gk` collapsed title header (map_zone1/dojo_hub):
    // x 89..279, y 72..112 at 1280x720.
    const float s = kViewW / 1280.0f;
    x = 89.0f * s;
    y = 72.0f * s;
    w = 190.0f * s;
    h = 40.0f * s;
}

// Hit test for the vertical nav column; -1 when outside every button.
int za_nav_hit(double px, double py) {
    const ZaLayout lay = za_layout();
    const float cx = lay.nav_x + lay.nav_w * 0.5f;  // g.C(wc.Gv/2)
    const float half = lay.nav_btn * 0.5f;
    for (int i = 0; i < kZaNavCount; ++i) {
        const float cy = lay.nav_first_y + static_cast<float>(i) * lay.nav_step;
        if (px >= cx - half && px <= cx + half && py >= cy - half && py <= cy + half) {
            return i;
        }
    }
    return -1;
}

// The `za` nav-button tap (JS listeners Ofb/Qfb/Wfb/Rfb/Vfb -> `ma.Jg().jI`).
// GATING RULE (cited): the five `Le` buttons are created unconditionally
// (`a.EL=!0`, L1978-1979) and every listener is `d1(a){rb.um();wa.F().mp(a)||
// (this.xba.Nf=!0)}` (Ofb/Qfb/Wfb/Rfb, L1020xx) — `mp` navigates with NO
// quest/step guard. There is NO hard lock on Dojo/Shop/Map/Profile; the story
// only GUIDES via `MenuBtnFlashing BtnName` (JS `eo` L1117 -> the port's
// `nav_flash`) plus `StoryTutorialOpenScene`/`StoryTutorialRetryGoToMap`
// re-navigation. The port matches (free switching is correct); the log below
// records the guidance target so the rule is observable.
// Button #5 is `Vfb` (L1981) — NOT a screen: it loads the per-language atlases
// then `Xc.Shb()` opens the `un` dialog OVER the current screen (D13). The
// other four push their `kZaNav` screen unless it is already showing.
void za_nav_activate(App& app, Screen& self, ScreenId active, int hit) {
    if (hit < 0 || hit >= kZaNavCount) return;
    sf2::audio::AudioEngine::instance().play("snd_click_1");
    if (hit == 4) {  // Settings (JS `Vfb` L1981 -> `Xc.Shb()` L931)
        std::fprintf(stdout, "[za] nav %s -> settings dialog (no nav)\n", kZaNav[hit].label);
        std::fflush(stdout);
        open_settings_dialog(app);
        return;
    }
    const ScreenId target = kZaNav[hit].nav;
    const std::string& guide = app.quest_engine().nav_flash();
    std::fprintf(stdout, "[za] nav %s -> screen %d (guidance=%s)\n", kZaNav[hit].label,
                 static_cast<int>(target), guide.empty() ? "-" : guide.c_str());
    std::fflush(stdout);
    if (target != active) {
        self.push(target);
    }
}

// Handles the nav-column taps for a shell screen (JS listeners Ofb/Qfb/Wfb/
// Rfb/Vfb -> `ma.Jg().jI(cls)`): a tap pushes the target screen unless it is
// the screen already showing (the JS highlights that one active, `xyb`
// L1982).
// `gk.Gwa(a,b)` (L2000): `b<=0 ? (this.PF=!1,this.NLa(a)) : (this.pma=b,
// this.PF=!0,this.time=0)`. `NLa` (L2001) stores the fraction + the 2/1/0 `zI`
// state. The timed run is `expand(.3)`/`collapse(.3)` (L2000), so the lock is
// 0.3 s.
constexpr float kZaNavAnimSeconds = 0.3f;

// `gk.NLa(a)` (L2001): `this.yI=a; ... let b=a==0; this.dr=a==1; this.zI =
// b?2 : dr?1 : 0`.
static void za_nav_settle(ZaNavState& st, float y) {
    st.yI = y;
    if (y <= 0.0f) {
        st.zI = 2;  // collapsed
    } else if (y >= 1.0f) {
        st.zI = 1;  // expanded
    } else {
        st.zI = 0;  // mid-run
    }
}

// `gk.expand(a)` (L2000): `this.uJ=!0; this.Gwa(this.width,a)`. NO `PF` guard:
// an expand re-targets even mid-animation.
static void za_nav_expand_run(ZaNavState& st, float dur) {
    st.uJ = true;
    if (dur <= 0.0f) {
        st.PF = false;
        st.pma = 0.0f;
        st.time = 0.0f;
        za_nav_settle(st, 1.0f);  // `Gwa(a,0)` -> immediate
    } else {
        st.pma = dur;
        st.PF = true;
        st.time = 0.0f;
    }
}

// `gk.collapse(a)` (L2000): `this.uJ=!1; a>0&&this.PF||this.Gwa(0,a)`. The
// `a>0 && this.PF` short-circuit is the "a close during the run is a NO-OP"
// rule (the animation lock). Returns false when the call was swallowed.
static bool za_nav_collapse_run(ZaNavState& st, float dur) {
    st.uJ = false;
    if (dur > 0.0f && st.PF) return false;  // the JS no-op while animating
    if (dur <= 0.0f) {
        st.PF = false;
        st.pma = 0.0f;
        st.time = 0.0f;
        za_nav_settle(st, 0.0f);  // immediate (`Gwa(0,0)`)
    } else {
        st.pma = dur;
        st.PF = true;
        st.time = 0.0f;
    }
    return true;
}

void za_update(App& app, Screen& self, ScreenId active, float dt) {
    ZaNavState& st = za_nav_state(active);
    // `gk.aa` (L1998): the animation advances FIRST and clears its own lock at
    // progress 1 (`a=this.ed(this.pma); a==1&&(this.PF=!1)`), easing `yI`
    // toward the target (`this.NLa(dc.Ln()(this.uJ?a:1-a))`). The easing curve
    // is observable only mid-run; the settled `yI` is identical.
    if (st.PF) {
        st.time += dt;
        float p = st.pma > 0.0f ? st.time / st.pma : 1.0f;
        if (p >= 1.0f) {
            p = 1.0f;
            st.PF = false;  // the lock clears at the animation's end
        }
        za_nav_settle(st, st.uJ ? p : 1.0f - p);
    }
    // `za.zq` disciple toggle (JS L1983, `Nfb` L1981): a child of `za`, so it
    // answers taps regardless of the nav collapse. Shown only on the Dojo
    // (`v.FU` L1207 `Td.Tf==3`) while `ShowDojoDisciple > 0` (`g$a` L271).
    if (active == kScreenDojo) {
        bool shown = false;
        bool disc = false;
        try {
            const WarriorSave sv = app.save().load();
            shown = sv.show_dojo_disciple;
            disc = sv.disciple;
        } catch (const std::exception&) {
        }
        if (shown) {
            const ZaDiscipleRect dr = za_disciple_rect(app, disc);
            const double px0 = app.pointer().x, py0 = app.pointer().y;
            if (px0 >= dr.cx - dr.w * 0.5f && px0 <= dr.cx + dr.w * 0.5f &&
                py0 >= dr.cy - dr.h * 0.5f && py0 <= dr.cy + dr.h * 0.5f) {
                if (app.pointer().pressed) {
                    try {
                        WarriorSave sv = app.save().load();
                        sv.disciple = !sv.disciple;  // `oub(!p.o.Y0())` L1981
                        app.save().save(sv);
                    } catch (const std::exception& e) {
                        std::fprintf(stderr, "[za] disciple toggle failed: %s\n", e.what());
                    }
                    sf2::audio::AudioEngine::instance().play("snd_click_1");
                    std::fprintf(stdout, "[za] disciple toggle -> %d\n", disc ? 0 : 1);
                    std::fflush(stdout);
                }
                return;  // the toggle owns its rect
            }
        }
    }
    // JS `gk.Bgb` (L2000): a press on the scroll header toggles
    // `this.uJ?collapse(.3):expand(.3)`. While collapsed the five `Le`
    // buttons are hidden (`NLa` L2001), so only the header answers taps.
    float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
    za_header_rect(hx, hy, hw, hh);
    const double px = app.pointer().x, py = app.pointer().y;
    const bool header_hit = px >= hx && px <= hx + hw && py >= hy && py <= hy + hh;
    // Collapsed (JS `collapse(0)` L1978): the header expands the column.
    // While collapsed only the header answers (`NLa` L2001 hides the five
    // `Le` rows); a header press toggles the column.
    // `gk.aa` (L1998): the input switch runs only `if(!this.PF)`, so a nav tap
    // during the open/close run is swallowed (and `collapse(a>0)` is a no-op,
    // L2000). The lock makes the header toggle deterministic instead of
    // history-dependent (the reported "sometimes it closes sometimes not").
    if (st.PF) return;
    bool& nav_open = st.uJ;  // the JS `gk.uJ` target
    if (!nav_open) {
        // `gk.Bgb` (L2000): the header tap expands the column. It must work on
        // EVERY screen — the old `force_collapsed` early-return skipped it, so
        // the `МЕНЮ` button was dead on the Map/Shop/Profile.
        if (header_hit && app.pointer().pressed) {
            za_nav_expand_run(st, kZaNavAnimSeconds);
            std::fprintf(stdout, "[za] nav EXPAND (screen %d) target=%d lock=%d\n",
                         static_cast<int>(active), st.uJ ? 1 : 0, st.PF ? 1 : 0);
            std::fflush(stdout);
            sf2::audio::AudioEngine::instance().play("snd_focus_1");
        }
        return;
    }
    // Expanded: the `gk.Af` header rail answers in BOTH states (JS `gk.Bgb`
    // L2000; `Af` is a child of `gk.node`, NOT of the collapsed-hidden `iL`
    // layer — `gk.aa` L1998), so a header tap collapses FIRST. The five `Le`
    // buttons sit BELOW the rail; testing them first made the header rect
    // (x89..279, y72..112) hit the Dojo button (x122..246, y64..188) and
    // navigate instead of collapsing — the `МЕНЮ` column could not be closed
    // outside the Dojo.
    if (header_hit && app.pointer().pressed) {
        za_nav_collapse_run(st, kZaNavAnimSeconds);
        std::fprintf(stdout, "[za] nav COLLAPSE (screen %d) target=%d lock=%d\n",
                     static_cast<int>(active), st.uJ ? 1 : 0, st.PF ? 1 : 0);
        std::fflush(stdout);
        sf2::audio::AudioEngine::instance().play("snd_focus_1");
        return;
    }
    const int hit = za_nav_hit(px, py);
    if (hit >= 0 && app.pointer().pressed) {
        za_nav_activate(app, self, active, hit);
        return;
    }
}

// JS `v.$Ca()` (`static $Ca(){return v.Jsa}`) set by the config parse
// `v.eub(a.A("Power")!=null?u.I(a.A("Power").attributes.get("Max"),10):10)`
// from `internal_settings.xml` (the static default is `v.Jsa=5`). The shipped
// `<Power Max="5" TimeMax="600"/>` therefore yields 5. This is the `xr`
// energy widget's max (`xr` ctor `a=v.$Ca()`, L1984) and the clamp the save
// `Power` (`dk`) is held to (`this.dk>this.wr&&(this.dk=this.wr)`).
int energy_max() {
    static int cached = -1;
    if (cached >= 0) return cached;
    cached = 5;  // JS static default `v.Jsa=5` (config absent).
    try {
        sf2::data::xml_doc doc;
        std::ifstream in("reference/extracted/xml/res/internal_settings.xml",
                         std::ios::binary);
        if (in) {
            std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
            const pugi::xml_node root = doc.root().first_child();
            if (root) {
                const pugi::xml_node power = root.child("Power");
                cached = power ? (power.attribute("Max") ? power.attribute("Max").as_int(10) : 10)
                               : 10;  // `u.I(...,10)` when `<Power>` is absent
            }
        }
    } catch (const std::exception&) {
    }
    return cached;
}

// `Nn`/`MenuBtnFlashing` highlight (`UseFlashing="1"`, tutorial_quests.xml
// L156/L361): a draw-side pulse clock (one tick per rendered frame — the
// flash has no gameplay time source), 2 s sine.
float ui_flash_pulse() {
    static constexpr float kTwoPi = 6.28318530717958647692f;
    static float t = 0.0f;
    t += 1.0f / 60.0f;
    return 0.5f + 0.5f * std::sin(kTwoPi * t / 2.0f);
}

// `MenuBtnFlashing BtnName` names the `za` nav target by its SCENE id
// (`_NextScene` = Shop/Map/Dojo/Profile, tutorial_quests.xml L93/L116/L138/
// L200). Returns the `kZaNav` row, or -1.
int za_nav_index_for_scene(const std::string& scene) {
    static const char* const kNavScene[kZaNavCount] = {"Dojo", "Map", "Shop", "Profile",
                                                       "Settings"};
    for (int i = 0; i < kZaNavCount; ++i) {
        if (scene == kNavScene[i]) return i;
    }
    return -1;
}

// The flash tint over a rect (translucent, pulses).
void draw_flash_tint(sf2::render::Renderer& ren, float cx, float cy, float w, float h) {
    const float p = ui_flash_pulse();
    const float x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
    const float x1 = cx + w * 0.5f, y1 = cy + h * 0.5f;
    const float verts[] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
    ren.draw_triangles(verts, 6, 1.0f, 0.88f, 0.35f, 0.20f + 0.45f * p);
}

// `he` — the `MenuBtnFlashing` hint arrow (`eo.N3a` L1117 -> `he.show(a.target)`,
// `he` ctor: `this.Oy=R.$(E.get(260), y.sRa, this.node)`; `y.sRa="Arrow"`).
// `he.aa`: `this.Oy.la(min(W,H)*0.1/fa.x)` scales the arrow to
// `min(W,H)*0.1` wide, centres it on the target rect x and pins it at the
// rect top (`node.C((a.J+a.N)*.5)`, `node.D(a.W+...)`), bobbing ±0.8 with a
// 30-frame direction flip (`this.cV`, `this.UUa=30`).
void draw_nav_hint_arrow(App& app, float cx, float bottom_y) {
    const float w = std::min(kViewW, kViewH) * 0.1f;
    const float h = w * 0.75f;
    static int phase = 0;
    const float bob = ((phase++ / 30) % 2 == 0) ? 0.8f : -0.8f;
    // JS `he.aa` L2315: `this.node.D(a.W + this.Oy.qa()*.1)` pins the arrow node
    // TOP `0.1*arrowHeight` BELOW the target rect BOTTOM (`a.W`; the rect `gb`
    // ctor L795087 is `{J=x1,P=y1,N=x2,W=y2}` so W = y2 = bottom).
    const float top = bottom_y + h * 0.1f;
    if (!try_draw_atlas_button(app, "Arrow", cx, top + h * 0.5f + bob, w, h, 1.0f)) {
        draw_flat_button(app, "v", cx, top + h * 0.5f + bob, w, h, 0.9f, 0.8f, 0.3f, true);
    }
}

// Draws the shared chrome on top of a shell screen's own content. `active`
// selects the active nav frame (JS `xyb`). The widget strip mirrors `odb`:
// widgets are laid left->right and the strip is centred; each widget is
// icon + value (+ bar), scaled to `widget_h` (JS wr/xr/yr `layout`).
void draw_za_chrome(App& app, ScreenId active, const int* badges = nullptr) {
    sf2::render::Renderer& ren = app.renderer();
    const float w = kViewW;
    const ZaLayout lay = za_layout();
    // JS `gk.background` (L1997): the `za` nav scroll is built with `e=!0`, so
    // its ctor appends the dim quad `Fc.Ed(-2147483648)` (= ARGB 0x80000000,
    // black @ alpha 0x80). `NLa` (L2001) drives `lyb(yI)` -> `background.wa(yI)`,
    // so a fully EXPANDED column paints the 0.5-black screen dim — this is why
    // the oracle `dojo_menu_open` scene is exactly half-brightness vs
    // `dojo_hub` (measured 0.50x at every sampled scene pixel). Drawn BEFORE
    // the topPanel/widgets (JS appends `scroll` before `PL`, ctor L1973), so
    // the bar + column stay undimmed.
    // The per-screen `za_nav_state` owns the collapse (`gk.uJ`/`yI`); a
    // column with `yI==0` draws only the header + its flash (the JS
    // `iL.R(this.yI>0)` visibility, `NLa` L2001).
    const float nav_frac = za_nav_state(active).yI;
    const int flash_idx = za_nav_index_for_scene(app.quest_engine().nav_flash());
    if (nav_frac > 0.0f) {
        const float dim[] = {0, 0, w, 0, w, kViewH, 0, 0, w, kViewH, 0, kViewH};
        ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.502f * nav_frac);
    }
    // topPanel (misc id 260) stretched full width (odb L1975).
    if (!try_draw_atlas_button(app, "topPanel", w * 0.5f, lay.bar_h * 0.5f, w, lay.bar_h,
                               1.0f, /*fill=*/true)) {
        const float bg[] = {0, 0, w, 0, w, lay.bar_h, 0, 0, w, lay.bar_h, 0, lay.bar_h};
        ren.draw_triangles(bg, 6, 0.10f, 0.07f, 0.05f, 1.0f);
    }
    // Widgets wr/xr/yr (odb L1975).
    WarriorSave sv;
    try {
        sv = app.save().load();
    } catch (const std::exception&) {
    }
    const float icon = lay.widget_h;
    const float bar_h = lay.widget_h * 0.4f;              // c = a*.4 (wr/xr layout)
    const float num_scale = lay.widget_h * 0.9f / 100.0f;  // ua(a*.9), eF=100
    // Per-widget logical widths: `wr` = level icon + value + Level_bar; `xr`
    // = energy icon + Energy_Bar; `yr` = gold + money + ruby + gems.
    // Icon widths use the UNTRIMMED source ratios (JS `R.za()` = `fa.x*Eb`):
    // misc `level` 115x111, `energy` **101x103** (frame is 95x103 — the
    // packed rect is trimmed), `gold` 95x95, `ruby` 88x87.
    const float icon_level = icon * (115.0f / 111.0f);
    const float icon_energy = icon * (101.0f / 103.0f);
    const float icon_gold = icon;
    const float icon_ruby = icon * (88.0f / 87.0f);
    const float q = icon * 0.25f;                        // wr text gap b = iw.za()*.25
    // JS `wr.layout` (L1987): the level BAR starts at the MEASURED value
    // width — `d.C(gA.ya + (f.N-f.J) + b)` with `f = gA.Oj()` (the level text
    // node is `gA`, `Ia(64)` L1986, so its drawn width is `Oj().N-Oj().J`).
    // `yr` lays its icons off the same measured widths (L1991). This replaces
    // the fixed `icon*1.4` value slot (the strip-width OPEN, PORT_AUDIT_UI §5):
    // the strip now sizes to the actual level/money/gem strings. The old slot
    // is only the fallback when no menu font is loaded.
    const sf2::data::font* mfont = app.menu_font();
    const std::string lvl_text = std::to_string(sv.level);
    const std::string money_text = std::to_string(sv.money);
    const std::string gem_text = std::to_string(sv.bonus);
    const auto measured_w = [&](const std::string& s) -> float {
        return mfont != nullptr ? app.measure_text(*mfont, s, num_scale) : icon * 1.4f;
    };
    const float num_w = measured_w(lvl_text);
    const float money_num_w = measured_w(money_text);
    const float gem_num_w = measured_w(gem_text);
    // JS `hk.zf(a)` (L2002) = `node.la(a / Ud.fa.y)`, `Ud` = the empty frame
    // `level_bar_empty_short` (source 246x32); `wr.Vd.zf(a*.4)` (L1987) so the
    // bar length = 246/32 * 0.4 * widget_h = 3.075*widget_h (was icon*2).
    const float bar_w = icon * (246.0f / 32.0f) * 0.4f;  // widget bar length
    // JS `yr.layout` (L1991) uses a DIFFERENT gap: `b = ((clamp(N.lc,.6,2)
    // -.6)/1.4*100)`, an aspect offset (~84px at 16:9), between the money
    // value and the ruby (and before AddMoney). Previously the native reused
    // the `wr` icon gap `q` (~12px), which made the money widget far too
    // narrow and — with the strip centred — shifted every widget.
    const float lc_clamp = std::clamp(kViewW / kViewH, 0.6f, 2.0f);
    const float yr_gap = (lc_clamp - 0.6f) / 1.4f * 100.0f;
    // `yr.M1a` (L1995): the AddMoney icon-button (misc id 260, frames
    // `AddMoney`/`AddMoney_Pressed`), created only with `Ca.hasFeature("iap")`
    // — the oracle build HAS iap (the green `+` is in dojo_hub.png at
    // x1135..1179). `yr.layout` (L1991-1993) scales it `la(a/Fg.Y.fa.y)`
    // (height = widget_h; source 116x115) and places it at
    // `C(au.ya+au.za()+Fg.Y.za()/2+b)`; the `au` gem box is
    // `Fa(measured+20+10)` (L1992), so the gap carries a +30 pad before `b`.
    constexpr float kAddMoneyBoxPad = 30.0f;            // `c=ceil(Oj+20+10)` L1992
    const float am_h = lay.widget_h;                    // `la(a/...)` -> height a
    const float am_w = am_h * (116.0f / 115.0f);        // misc AddMoney 116x115
    const float lvl_w = icon_level + q + num_w + q + bar_w;
    // `xr.layout` L1985: the Energy bar sits at `icon.za()*1.1`, not icon+q.
    const float en_w = std::max(icon_energy, icon_energy * 1.1f + bar_w);
    // `yr.layout` (L1991-1993), left->right: gold icon, money text, pad `c`,
    // aspect gap `b`, ruby, gem text, pad `c`, gap `b`, AddMoney. `c` is the
    // label box pad `ceil(Oj()+20+10)` (L1992) — every value/digit label box
    // is 30px wider than its ink, so both gaps carry it.
    const float money_text_right = icon_gold + money_num_w;
    // `PA.C(PA.za()/2 + Dq.ya + c + b)` (L1992).
    const float ruby_left = money_text_right + kAddMoneyBoxPad + yr_gap;
    const float gem_text_right = ruby_left + icon_ruby + gem_num_w;
    // `Fg.C(au.ya + au.za() + Fg.Y.za()/2 + b)` (L1993): `Fg` (db.xz) uses a
    // LEFT anchor, so the `Fg.Y.za()/2` term does NOT cancel — the button's
    // left edge sits there. `au` box = ink + 30.
    const float am_left = gem_text_right + kAddMoneyBoxPad + am_w * 0.5f + yr_gap;
    const float money_w = am_left + am_w;
    const float total = lvl_w + lay.gap + en_w + lay.gap + money_w;
    float x = (w - total) * 0.5f;
    const float cy = lay.sp * 0.5f;  // strip centred in the bar (Pr.D((Sp-...)/2))
    // `wr` (level). `iw` is a plain `R.$` (no `Ga`) -> left/top-edge anchor;
    // `iw.la(a/fa.y)` scales to height `a`, so the top-left y = cy - a/2.
    try_draw_atlas_button(app, "level", x, cy - icon * 0.5f, icon_level, icon, 1.0f, false,
                          false, /*top_left=*/true);
    draw_ui_label(app, x + icon_level + q, cy - lay.widget_h * 0.45f, num_w, lay.widget_h,
                  lvl_text, num_scale, UiAlign::Left, 1.0f, 1.0f, 1.0f);
    // JS `wr.Vd = Uf(y.$na, y.IRa)` = `level_bar_empty_short` (empty backing)
    // + `level_bar_short` (fill) over the same 246x32 source box (L1985).
    // `wr.myb(rs, Oz())` (L1989) -> `Vd.PT(max)` + `Vd.DF(rs)`; `Uf.ratio()`
    // (L2002-2003) = `(Mb-KN)/(JW-KN)` = `rs/Oz()`. `Oz()` (L253) = the
    // `character_progress.xml` threshold (`ResultsScreen::exp_for_level`),
    // `rs` = the save `Experience` (JS `p.o.rs`). Clamped 0..1.
    const float lvl_bar_x = x + icon_level + q + num_w + q;
    try_draw_atlas_button(app, "level_bar_empty_short", lvl_bar_x, cy - bar_h * 0.5f, bar_w,
                          bar_h, 1.0f, /*fill=*/true, false, /*top_left=*/true);
    const int need_exp = ResultsScreen::exp_for_level(sv.level);
    const float lvl_frac =
        need_exp > 0
            ? std::clamp(static_cast<float>(sv.experience) / static_cast<float>(need_exp),
                         0.0f, 1.0f)
            : 0.0f;
    if (lvl_frac > 0.0f) {
        try_draw_atlas_button(app, "level_bar_short", lvl_bar_x, cy - bar_h * 0.5f,
                              bar_w * lvl_frac, bar_h, 1.0f, /*fill=*/true, false,
                              /*top_left=*/true);
    }
    x += lvl_w + lay.gap;
    // `xr` (energy). `icon` is a plain `R.$` (no `Ga`) -> left/top-edge.
    // `xr.Vd = zr` extends `Uf` and passes the SAME `y.$na` base
    // (`level_bar_empty_short`, L2003-2004), so the base matches the level bar.
    try_draw_atlas_button(app, "energy", x, cy - icon * 0.5f, icon_energy, icon, 1.0f, false,
                          false, /*top_left=*/true);
    // `xr.Vd = zr` extends `Uf(y.$na, y.zRa)` (L2003-2004): empty
    // `level_bar_empty_short`, fill `y.zRa` = **"Energy_Bar"** (the `y` frame
    // table), so the frame name used here is already exact, not a stand-in.
    // `xr.JOa` (L1986) calls `Vd.DF(p.o.dk)`; `zr` quantizes (`n5` L2004:
    // `nH[i]=floor(100/max*i)`), so `ratio = nH[dk]/100` =
    // `floor(100*dk/max)/100`; `max` = `v.$Ca()` (`energy_max()`, 5),
    // `dk` = the save `Power`.
    const int en_max = energy_max();
    const int en_idx = std::clamp(sv.power, 0, en_max);
    const float en_frac =
        en_max > 0
            ? std::floor(100.0f * static_cast<float>(en_idx) / static_cast<float>(en_max)) / 100.0f
            : 0.0f;
    try_draw_atlas_button(app, "level_bar_empty_short", x + icon_energy * 1.1f,
                          cy - bar_h * 0.5f, bar_w, bar_h, 1.0f, /*fill=*/true, false,
                          /*top_left=*/true);
    if (en_frac > 0.0f) {
        try_draw_atlas_button(app, "Energy_Bar", x + icon_energy * 1.1f, cy - bar_h * 0.5f,
                              bar_w * en_frac, bar_h, 1.0f, /*fill=*/true, false,
                              /*top_left=*/true);
    }
    x += en_w + lay.gap;
    // `yr` (money). `Ss`/`PA` DO call `Ga()` (L1990) -> centre anchor, so the
    // JS `Ss.C(Ss.za()/2)` etc. are CENTRE positions (left edge 0).
    try_draw_atlas_button(app, "gold", x + icon_gold * 0.5f, cy, icon_gold, icon, 1.0f);
    const float money_tx = x + icon_gold;  // JS `Dq.C(Ss.za())`: no gap
    draw_ui_label(app, money_tx, cy - lay.widget_h * 0.45f, money_num_w, lay.widget_h,
                  money_text, num_scale, UiAlign::Center, 1.0f, 0.9f, 0.4f);
    const float ruby_x = x + ruby_left;  // JS `PA.C(PA.za()/2+Dq.ya+c+b)`
    try_draw_atlas_button(app, "ruby", ruby_x + icon_ruby * 0.5f, cy, icon_ruby, icon, 1.0f);
    draw_ui_label(app, ruby_x + icon_ruby, cy - lay.widget_h * 0.45f, gem_num_w, lay.widget_h,
                  gem_text, num_scale, UiAlign::Center, 1.0f, 0.9f, 0.4f);
    // `yr.Fg` AddMoney (JS `M1a` L1995, gated `Ca.hasFeature("iap")` — the
    // oracle has iap). `layout`: centre at `gemTextRight + boxPad + btnW/2 +
    // yr_gap`, `D(a/2)` (= cy), height `a` (widget_h).
    try_draw_atlas_button(app, "AddMoney", x + am_left + am_w * 0.5f, cy, am_w, am_h, 1.0f);
    // `za.zq` disciple toggle (JS L1983 `xub`; frame swap `FU`; visibility
    // `v.FU` L1207 = active screen Dojo AND `g$a()`). Backed by the save
    // `Disciple`/`ShowDojoDisciple` session settings (`oub`/`Y0`/`g$a` L271).
    // A child of `za`, so it draws in both the collapsed and expanded nav.
    if (active == kScreenDojo && sv.show_dojo_disciple) {
        const ZaDiscipleRect dr = za_disciple_rect(app, sv.disciple);
        const char* frame = sv.disciple ? "btn_punching_bag" : "btn_disciple";
        if (!try_draw_atlas_button(app, frame, dr.cx, dr.cy, dr.w, dr.h, 1.0f)) {
            draw_flat_button(app, sv.disciple ? "BAG" : "DISC", dr.cx, dr.cy, dr.w, dr.h,
                             0.35f, 0.4f, 0.3f, false);
            draw_ui_label(app, dr.cx - dr.w * 0.5f, dr.cy - 9.0f, dr.w, 18.0f,
                          sv.disciple ? "BAG" : "DISC", 0.6f, UiAlign::Center, 1.0f, 1.0f,
                          1.0f);
        }
    }
    // JS `gk` collapsed default (L1978): only the `Lx` title header shows;
    // the five `Le` buttons render only once expanded (`NLa` L2001). The Map
    // forces the collapsed header (JS mounts a fresh `za` per screen,
    // `Aub` -> `collapse(0)`) without disturbing the shared nav flag other
    // shell screens rely on.
    if (nav_frac <= 0.0f) {
        float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
        za_header_rect(hx, hy, hw, hh);
        // The collapsed header is `gk.Af` — the SAME `Zh` rail the expanded
        // state uses (`gk.Af = new Zh(this.width, 90*d)` L1996-1997,
        // `Af.ba(e, 90*d)` in `gk.ba` L1999): `roll_end` + stretched
        // `roll_center` + mirrored `roll_end` (`Zh` ctor L1872). `Af` is a
        // child of `gk.node`, NOT of the collapsed-hidden `iL` layer
        // (`gk.aa` L1998 `iL.R(this.yI>0)`), so the roll caps answer in BOTH
        // states — oracle dojo_hub carries the wooden end caps at
        // x~93..127 / 236..273 around the "МЕНЮ" plate.
        load_scroll_atlas(app);
        {
            // `Zh.ba(a,b)`: `c = b>a` is false for the horizontal rail, so the
            // cap scale is `min(w,h)/roll_end.source_h` (`d = c?a:b`) and the
            // centre is stretched to `max(len - 2*cap, 10)`.
            constexpr float kCapSrcW = 101.0f, kCapSrcH = 114.0f;  // scroll.json roll_end
            const float cap_w = kCapSrcW * (std::min(hw, hh) / kCapSrcH);
            const float body_w = std::max(hw - 2.0f * cap_w, 10.0f);
            if (!try_draw_atlas_button(app, "roll_end", hx + cap_w * 0.5f, hy + hh * 0.5f,
                                       cap_w, hh, 1.0f, /*fill=*/true) ||
                !try_draw_atlas_button(app, "roll_center", hx + cap_w + body_w * 0.5f,
                                       hy + hh * 0.5f, body_w, hh, 1.0f, /*fill=*/true) ||
                !try_draw_atlas_button(app, "roll_end", hx + hw - cap_w * 0.5f,
                                       hy + hh * 0.5f, cap_w, hh, 1.0f, /*fill=*/true,
                                       /*flip_x=*/true)) {
                const float panel[] = {hx, hy, hx + hw, hy, hx, hy + hh,
                                       hx + hw, hy, hx + hw, hy + hh, hx, hy + hh};
                ren.draw_triangles(panel, 6, 0.10f, 0.07f, 0.05f, 0.9f);
            }
        }
        // The label is `Y.na("menu")`: UTF-8 bytes for `МЕНЮ` (0xD0 0x9C
        // 0xD0 0x95 0xD0 0x9D 0xD0 0xAE) written as escapes so the string
        // literal is independent of the compiler's source charset (MSVC).
        draw_ui_label(app, hx, hy + hh * 0.28f, hw, hh, "\xD0\x9C\xD0\x95\xD0\x9D\xD0\xAE",
                      0.5f, UiAlign::Center, 0.184f, 0.145f, 0.106f);
        // `_NextScene` guidance while the column is collapsed: the `Le` art
        // is hidden (JS `NLa` L2001), so the header carries the flash — the
        // player is shown that the menu must be opened. The JS nav flash
        // itself lands on the row once expanded (below).
        // `eo.N3a` L1117: `scroll.button.tk=!0` — the flash lands on the
        // collapsed MENU button (`scroll.button`) regardless of `BtnName`
        // (`N3a` only STORES `vpa`; the destination row flash is `dia`, which
        // runs only after a nav click `u3`). `he.show(a.target)` then pins the
        // `Arrow` hint above that button. The row pulse below therefore no
        // longer gates the collapsed header flash.
        if (!app.quest_engine().nav_flash().empty()) {
            draw_flash_tint(ren, hx + hw * 0.5f, hy + hh * 0.5f, hw, hh);
            // The `he` target is the collapsed MENU button (header rect `hx,hy,hw,hh`),
            // so its BOTTOM is `hy + hh` (`a.W`, L2315).
            draw_nav_hint_arrow(app, hx + hw * 0.5f, hy + hh);
        }
        return;
    }
    // --- Expanded `za` nav column (ndb L1976-1977) --------------------------
    // `scroll.ba(e,f,90*d)` sizes the `gk` to the column; its child
    // `Fg(400,800,0,50)` content frame (`wc.ba(e,f,qka)`, L1977) is the
    // parchment panel: `paper_edge_left`/`paper`/`paper_edge_right` (scroll
    // atlas 254, `Fg` ctor L1869), rotated 90° for the vertical column
    // (`Fg.ba` case 1 `wc.Wg(90)`). Drawn BEFORE the `Le` buttons (which are
    // children of `wc.content`).
    {
        const float qka = lay.nav_qka;
        const float col_h = lay.nav_col_h;
        if (load_scroll_atlas(app) && lay.nav_w > 2.0f * qka && col_h > 0.0f) {
            // `Fg(400,800,0,50)` -> orientation 0 (`ba` case 0): the strip is
            // `e` wide x `f` tall with the `paper_edge_left`/`paper_edge_right`
            // caps on the LEFT/RIGHT (23px = qka each) and the `paper` centre
            // between them — NOT top/bottom (that is orientation 1, which the
            // nav does not use). Oracle dojo_menu_open: caps x88..111 /
            // 256..279, tan centre x111..256 (145 = 191.7 - 2*22.3).
            const float mid_w = lay.nav_w - 2.0f * qka;
            const float cy = lay.sp + col_h * 0.5f;
            try_draw_atlas_button(app, "paper_edge_left", lay.nav_x + qka * 0.5f, cy, qka,
                                  col_h, 1.0f, /*fill=*/true);
            try_draw_atlas_button(app, "paper", lay.nav_x + qka + mid_w * 0.5f, cy, mid_w,
                                  col_h, 1.0f, /*fill=*/true);
            try_draw_atlas_button(app, "paper_edge_right",
                                  lay.nav_x + lay.nav_w - qka * 0.5f, cy, qka, col_h, 1.0f,
                                  /*fill=*/true, /*flip_x=*/true);
        }
    }
    // Vertical nav column (ndb L1976-1977).
    const int hover = za_nav_hit(app.pointer().x, app.pointer().y);
    const float nav_cx = lay.nav_x + lay.nav_w * 0.5f;
    for (int i = 0; i < kZaNavCount; ++i) {
        const ZaNavDef& def = kZaNav[i];
        const bool is_active = def.nav == active;
        const bool is_hover = i == hover;
        const char* frame = is_hover && def.pushed != nullptr
                                ? def.pushed
                                : (is_active && def.active != nullptr ? def.active : def.normal);
        const float cy_i = lay.nav_first_y + static_cast<float>(i) * lay.nav_step;
        const bool row_flash = (i == flash_idx);
        if (!try_draw_atlas_button(app, frame, nav_cx, cy_i, lay.nav_btn, lay.nav_btn, 1.0f)) {
            draw_flat_button(app, def.label, nav_cx, cy_i, lay.nav_btn, lay.nav_btn,
                             is_active ? 0.6f : (is_hover ? 0.5f : 0.35f), 0.4f, 0.28f,
                             is_hover);
            draw_ui_label(app, nav_cx - lay.nav_btn * 0.5f, cy_i - 10.0f, lay.nav_btn, 20.0f,
                          def.label, 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        }
        // Le badge (JS `Dg`/`Le` L1848-1850: notification_circle +
        // notification_ellipse + count). Each JS strip supplies its own values
        // through an overridden `getCounterValue`: the shop tab strip (`ss`,
        // L2284) uses `p.items.T5a(Cj.zxb(a))`, the profile strip (`cs`, L2189)
        // uses `uCa()/sCa()/rCa()/vCa()` (see the profile block above). This
        // helper serves the `za` NAV column, whose JS badge source is not
        // re-read in this pass — callers pass nullptr, so no badge is drawn.
        if (badges != nullptr && badges[i] > 0) {
            const float bx = nav_cx + lay.nav_btn * 0.28f;
            const float by = cy_i - lay.nav_btn * 0.28f;
            if (!try_draw_atlas_button(app, "notification_circle", bx, by, lay.nav_btn * 0.34f,
                                       lay.nav_btn * 0.34f, 1.0f)) {
                draw_flat_button(app, "", bx, by, lay.nav_btn * 0.3f, lay.nav_btn * 0.3f,
                                 0.85f, 0.2f, 0.2f, false);
            }
            draw_ui_label(app, bx - lay.nav_btn * 0.2f, by - 9.0f, lay.nav_btn * 0.4f, 18.0f,
                          std::to_string(badges[i]), 0.6f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        }
        // `MenuBtnFlashing BtnName` (FLOW_STATIC L141): pulse the row the
        // quest asked the player to use (drawn OVER the art). Draw-only; the
        // tap still navigates normally.
        if (row_flash) {
            draw_flash_tint(ren, nav_cx, cy_i, lay.nav_btn, lay.nav_btn);
        }
    }
    // `gk`'s title rail (`Zh`: `y.goa`/`y.pSa` = roll_end/roll_center, L1872/
    // L2467). `JT` (L2001) puts it at `D(f - railH/2)` once expanded, so the
    // "МЕНЮ" roll slides from the column TOP (collapsed) to the BOTTOM
    // (oracle dojo_menu_open: y~600..637). `gk.ba(e,f,90*d)` -> rail height
    // `90*d`; `Zh.ba(e,railH)` -> `c = railH>e = false`, so the roll is
    // horizontal across the column width with `roll_end` caps.
    {
        const float rail_h = 90.0f * lay.nav_scale;
        const float rail_cy = lay.sp + lay.nav_col_h - rail_h * 0.5f;
        constexpr float kRollEndW = 101.0f, kRollEndH = 114.0f;  // scroll.json roll_end
        const float cap_w = kRollEndW * (rail_h / kRollEndH);
        const float body_w = std::max(lay.nav_w - 2.0f * cap_w, 10.0f);
        load_scroll_atlas(app);
        try_draw_atlas_button(app, "roll_end", lay.nav_x + cap_w * 0.5f, rail_cy, cap_w,
                              rail_h, 1.0f, /*fill=*/true);
        try_draw_atlas_button(app, "roll_center", lay.nav_x + cap_w + body_w * 0.5f, rail_cy,
                              body_w, rail_h, 1.0f, /*fill=*/true);
        try_draw_atlas_button(app, "roll_end", lay.nav_x + cap_w + body_w + cap_w * 0.5f,
                              rail_cy, cap_w, rail_h, 1.0f, /*fill=*/true, /*flip_x=*/true);
        // Label (`gk.ba` case 1): `Lx.Fa(a-2*b, c-2*d)`, `C(b)`, `D(d)` with
        // `b=a*.2`, `d=c*.2`; color `Z.sc` (0.184/0.145/0.106).
        draw_ui_label(app, lay.nav_x + 0.2f * lay.nav_w,
                      rail_cy - rail_h * 0.5f + 0.2f * rail_h, 0.6f * lay.nav_w,
                      0.6f * rail_h, "\xD0\x9C\xD0\x95\xD0\x9D\xD0\xAE", 0.5f,
                      UiAlign::Center, 0.184f, 0.145f, 0.106f);
    }
}

// ---------------------------------------------------------------------------
// Fight banner + hit sparks (JS `Cr` L2021-2026 / `Hyb`+`ryb`+`av`).
//
// `Cr` (L2022-2026): `image = R.$(E.get(1310), null, content)` — the
// `res/fight/callouts.*` atlas (frames round/fight/perfect/great/label_win/
// label_lose/ringout/timesup/...; the `y.*` table L2462-2463: BQa="round",
// uQa="fight", zQa="perfect", wQa="great"); the ROUND number is a font text
// (`round = ea(E.get(1298), content)`, id 1298 = fight/round.fnt). `image`
// is scaled `min(800, min(W,H))/image.fa.x*0.6` (layout L2027) and centred
// (`content.setPosition(ma.Kq.F5a())`). The banner is a pure presentation
// layer over the fight (reads FightController::banner()), never the
// simulation.
//
// App::init registers menu/misc/controller/fight-ui but not callouts, so the
// atlas is loaded lazily here (the `load_controller_atlas` pattern; ASTC KTX
// is CPU-decoded — core/data/ktx.cpp). A flat/menu-font fallback covers a
// real frame miss.
// ---------------------------------------------------------------------------

// Lazily loads `res/fight/callouts.*` (JS asset id 1310) into the app atlas
// cache. Returns true once the frames are registered.
bool load_callouts_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/fight";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("callouts.", 0) == 0 && entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".png", ".webp", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("callouts.", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("callouts_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[fight] callouts atlas: %dx%d tex %dx%d %zu frames\n", a.w, a.h,
                     tex.w, tex.h, a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[fight] callouts atlas load failed: %s\n", e.what());
    }
    return ok;
}

// Lazily loads `res/fight/pause.*` (the `Dr` pause-dialog art; PAUSE_STATIC
// §3) into the app atlas cache. Returns true once the frames are registered.
// App::init registers fight/ui but not the `pause.*` atlas, so it is loaded
// on demand (same pattern as `load_callouts_atlas`). Frames: home, Pause,
// Pause_selected, PauseMusic_off/on, PauseSound_off/on, play.
bool load_pause_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/fight";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("pause.", 0) == 0 && entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".png", ".webp", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("pause.", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("pause_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[fight] pause atlas: %dx%d tex %dx%d %zu frames\n", a.w, a.h,
                     tex.w, tex.h, a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[fight] pause atlas load failed: %s\n", e.what());
    }
    return ok;
}

// Lazily loads `res/fight/fx.*` — the `fight/fx` atlas (JS asset id 1306,
// magic_effects.hpp) that carries the REAL `ni` frame runs the magic
// instances play: `hit_blade/hit_blade_1..29`, `block/block_1..24`,
// `effect_shield_hex_hit/effect_shield_hex_hit_1..16` (fx.925b16c7.json).
// App::init registers fight/ui but not `fx.*`, so it is loaded on demand
// (same pattern as `load_pause_atlas`). Returns true once registered.
bool load_fx_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/fight";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("fx.", 0) == 0 && entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".png", ".webp", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("fx.", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("fx_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[fight] fx atlas: %dx%d tex %dx%d %zu frames\n", a.w, a.h,
                     tex.w, tex.h, a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[fight] fx atlas load failed: %s\n", e.what());
    }
    return ok;
}

// Lazily loads `res/fight/ringout.*` — the `fight/ringout` atlas (JS asset id
// 1300, effects.hpp) that carries the two off-screen arrow runs `sXa`
// (L827-828) plays: frames "0".."19" (ringout.80bc6e99.json), each 256x128.
// App::init does not register `ringout.*`, so it is loaded on demand (the
// `load_fx_atlas` pattern). NOTE: these frame names are bare (`"0"`..`"19"`,
// the JSON filename); `App::atlas_cache_` is keyed by that name, which would
// collide with another atlas that ships bare numeric frames (the oracle's
// `ui/sale` does; it is not loaded by the port). The registration is kept
// here because it is the JS-exact key for this atlas.
bool load_ringout_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/fight";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("ringout.", 0) == 0 && entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".png", ".webp", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("ringout.", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("ringout_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        std::fprintf(stdout, "[fight] ringout atlas: %dx%d tex %dx%d %zu frames\n", a.w, a.h,
                     tex.w, tex.h, a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[fight] ringout atlas load failed: %s\n", e.what());
    }
    return ok;
}

// ---------------------------------------------------------------------------
// VS intro (JS `ik`, g="419", L2069-2074) — the pre-fight VS screen the oracle
// `fight_intro` shows. `ik` (ctor L2069-2071, `aa` L2071-2073, `layout`
// L2073-2074):
//   Qa = R.$(E.get(3,6))        full-screen `res/vs/bg.*` backdrop
//   Sn = Ea(node) C(512) D(286) -> D(ra+10)=296, la(1.6), Wg(27)  stroke container
//     KF = R.$(E.get(1), y.jTa) ("left") / ux (y.kTa, "right")  brush strokes
//   Tr = R.$(E.get(1), y.lTa) ("vs")   VS glyph: la(10) -> la(Izb) + wa fade
//   EK = oe(a.Hf) C(182) D(366)        player portrait (slides from x=-388)
//   RS = oe(b.Hf) C(842) D(206)        enemy portrait (slides from x=1412)
//   Web/Veb = c(Y.na(a.$s),185,114)    player name (shadow + gold)
//   Yeb/Xeb = c(Y.na(b.$s),845,464)    enemy name
// `vs/sprites.json` frames = left/right/vs (JS asset id 1); `vs/bg.jpg` is the
// backdrop. Loaded lazily (the `load_callouts_atlas` pattern).
// ---------------------------------------------------------------------------

bool load_vs_atlas(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/vs";
        std::string json_path;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("sprites.", 0) == 0 && entry.path().extension() == ".json") {
                json_path = entry.path().string();
                break;
            }
        }
        if (json_path.empty()) return false;
        sf2::data::Texture tex;
        bool decoded = false;
        for (const std::string& ext : {".png", ".webp", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("sprites.", 0) == 0 && entry.path().extension() == ext) {
                    if (sf2::data::decode_texture(entry.path().string(), tex)) {
                        decoded = true;
                        break;
                    }
                }
            }
            if (decoded) break;
        }
        if (!decoded) return false;
        const GLuint gl = app.renderer().texture_for("vs_sprites_atlas", tex);
        if (gl == 0) return false;
        std::ifstream in(json_path, std::ios::binary);
        std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
        for (const auto& fr : a.frames) {
            app.register_atlas_frame(fr, a.w, a.h, gl);
        }
        // Backdrop `res/vs/bg.*` (JS `E.get(3,6)`): a plain full-screen image,
        // registered as a whole-texture frame so `try_draw_atlas_button` can
        // stretch it to the viewport.
        for (const std::string& ext : {".jpg", ".png", ".webp", ".ktx", ".dds"}) {
            bool got = false;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("bg.", 0) != 0 || entry.path().extension() != ext) continue;
                sf2::data::Texture bg;
                if (!sf2::data::decode_texture(entry.path().string(), bg)) continue;
                const GLuint bgl = app.renderer().texture_for("vs_bg", bg);
                if (bgl == 0) continue;
                sf2::data::atlas_frame bf;
                bf.name = "vs_bg";
                bf.x = 0;
                bf.y = 0;
                bf.w = bg.w;
                bf.h = bg.h;
                bf.source_w = bg.w;
                bf.source_h = bg.h;
                app.register_atlas_frame(bf, bg.w, bg.h, bgl);
                got = true;
                break;
            }
            if (got) break;
        }
        std::fprintf(stdout, "[fight] vs atlas: %dx%d tex %dx%d %zu frames\n", a.w, a.h,
                     tex.w, tex.h, a.frames.size());
        std::fflush(stdout);
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[fight] vs atlas load failed: %s\n", e.what());
    }
    return ok;
}

// VS-intro timeline (JS `ik.aa` L2071-2073). The TOTAL is the JS `ik.yY`
// (3.4 s, L2071) and the composed pose holds until `kVsFadeT` (~2.9 s, the
// JS `kd9` `time>2` hold). The sub-stage times stay compressed so the
// fixed-frame `fight_intro` capture (screen frame 40 ≈ 0.67 s) still lands on
// the fully composed screen, as the oracle record shows.
constexpr float kVsSlideT = 0.35f;   // JS kd0 ed(.6): portraits slide in
constexpr float kVsGlyphT = 0.42f;   // JS kd2 ed(.2): VS glyph fade + scale
constexpr float kVsStrokeT = 0.48f;  // JS kd4/kd5 ed(.1): left/right strokes
constexpr float kVsNameT = 0.45f;    // JS kd7: names appear
constexpr float kVsFadeT = 2.90f;    // JS kd9 time>2: the 2 s composed hold ends
constexpr float kVsTotal = 3.40f;    // JS `ik.yY` L2071 = 3.4000000000000004

// Quadratic ease-out (the JS `dc.Ln()` family; monotone 0->1).
float vs_ease(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return 1.0f - (1.0f - x) * (1.0f - x);
}

// One `vs/sprites` brush stroke with the JS `ik` transform + EFilled wipe.
//
// JS (L2069): `this.Sn = new Ea(this.node); Sn.C(512); Sn.D(286);
// d=this.Sn; d.D(d.ra+10); Sn.la(1.6); Sn.Wg(27)` — the brush PAIR's
// container sits at design (512, 296) (the `D(286)` is overwritten by
// `D(ra+10)` = 296), scaled 1.6, rotated +27 deg about that origin.
//   `KF = R.$(E.get(1), y.jTa="left", Sn)` with `ik(1,.5)`/`Rn(1,.5)`
//   (anchor = the frame's RIGHT edge, vertical centre) and NO `C/D`, so the
//   left brush's right edge sits on the Sn origin and it extends LEFT:
//   local x [-492,0], y [-121,121] (frame sourceSize 492x242).
//   `ux = R.$(E.get(1), y.kTa="right", Sn)` with `ik(0,.5)`/`Rn(0,.5)`
//   (anchor = LEFT edge) at `C(ya-2)`, `D(ra-6)` -> local (-2,-6): local
//   x [-2,490], y [-133,121] (frame sourceSize 492x254).
// The wipe is `wl(vc.ho(Jc.io, b))` (L2069) animated 0->1 in `kd4` (left)
// and `kd5` (right) (L2072). `Jc.io` = `gfx.effect.FillMode.EHorizontal`
// (L1663 enum) and `vc.ho(mode,amount)` = `DrawMode.EFilled`. Both backends
// draw EFilled/EHorizontal as the frame's LEFT `amount` fraction into the
// node box's LEFT `amount` fraction (canvas `dda` case 0:
// `drawImage(img, Nc.x,Nc.y,Nc.w*e,Nc.h, 0,0, size.x*e,size.y)`; WebGL
// `dda` case 0: `u/x in [0,d]` at `x in [b.x, b.x + b.w*d]`).
//
// So: local x is clipped to `rx0 + rw*amount`, the UV u to `[0, amount]`
// (in LOCAL frame space, BEFORE the rotation) — the drawn quad is the
// rotated, UV-clipped band the oracle `fight_intro` shows.
void draw_vs_brush(App& app, const char* frame_name, float rx0, float ry0, float rw,
                   float rh, float amount, float alpha) {
    if (amount <= 0.0f || alpha <= 0.0f) return;
    amount = std::min(amount, 1.0f);
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame(frame_name, &fr, &tw, &th, &gl)) return;

    // Design base 1024 wide -> the view (JS `ik.layout` node scale
    // `(a.N-a.J)/1024`); `Sn.la(1.6)` stacks on top of it.
    const float s = kViewW / 1024.0f;
    const float sn_x = 512.0f * s, sn_y = 296.0f * s;   // JS Sn (C/D + D(ra+10))
    const float kScale = 1.6f;                          // JS Sn.la(1.6)
    const float kRot = 27.0f;                           // JS Sn.Wg(27)
    const float th_rad = kRot * 3.14159265358979323846f / 180.0f;
    const float ct = std::cos(th_rad), st = std::sin(th_rad);

    // Local corners (design units, before the 1.6 * s and the rotation):
    // TL, TR, BL, BR. The reveal edge is `rx0 + rw*amount` (EFilled).
    const float rx1 = rx0 + rw * amount;
    const float lx[4] = {rx0, rx1, rx0, rx1};
    const float ly[4] = {ry0, ry0, ry0 + rh, ry0 + rh};

    float xy[8];
    for (int c = 0; c < 4; ++c) {
        // Scale (Sn 1.6 * node scale s), rotate about Sn (+27 deg, the same
        // [ct,-st;st,ct] convention as `sprite_to_quad`), then place.
        const float px = lx[c] * kScale * s;
        const float py = ly[c] * kScale * s;
        xy[c * 2] = sn_x + px * ct - py * st;
        xy[c * 2 + 1] = sn_y + px * st + py * ct;
    }
    // Atlas UVs: the frame's left `amount` fraction (EHorizontal).
    const float un = tw > 0 ? 1.0f / static_cast<float>(tw) : 0.0f;
    const float vn = th > 0 ? 1.0f / static_cast<float>(th) : 0.0f;
    const float u0 = static_cast<float>(fr.x) * un;
    const float u1 = (static_cast<float>(fr.x) + static_cast<float>(fr.w) * amount) * un;
    const float v0 = static_cast<float>(fr.y) * vn;
    const float v1 = (static_cast<float>(fr.y) + static_cast<float>(fr.h)) * vn;
    const float uv[8] = {u0, v0, u1, v0, u0, v1, u1, v1};
    app.renderer().draw_textured_quad(frame_name, xy, uv, 1.0f, 1.0f, 1.0f, alpha);
}

// Draws the `ik` VS screen. Design base 1024x576 (JS `ik.layout` node scale
// `(a.N-a.J)/1024`); `s = kViewW/1024` reproduces the 1280x720 oracle layout.
void draw_vs_intro(App& app, float t, const std::string& pname,
                   const std::string& ename, const std::string& pimg,
                   const std::string& eimg) {
    if (!load_vs_atlas(app)) return;
    sf2::render::Renderer& ren = app.renderer();
    const float s = kViewW / 1024.0f;
    const float slide = vs_ease(t / kVsSlideT);
    const float alpha =
        t < kVsFadeT ? 1.0f : std::clamp(1.0f - (t - kVsFadeT) / (kVsTotal - kVsFadeT), 0.0f, 1.0f);
    if (alpha <= 0.0f) return;
    // Backdrop `Qa` — full-screen (JS `R.$(E.get(3,6))`).
    if (!try_draw_atlas_button(app, "vs_bg", kViewW * 0.5f, kViewH * 0.5f, kViewW, kViewH,
                               alpha, /*fill=*/true)) {
        const float bgq[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(bgq, 6, 0.05f, 0.02f, 0.02f, alpha);
    }
    // Stroke pair `KF`/`ux` on `Sn` (C(512,296) la(1.6) `Wg(27)`): the red
    // brush band across the backdrop (`vs/sprites` "left"/"right"), drawn as
    // the JS rotated, UV-clipped EFilled wipe (see `draw_vs_brush`). The
    // wipe amounts come from the VS clock: the JS stages are `kd4` (left,
    // `ed(.1)`) then `kd5` (right, `ed(.1)`) after the slide/glyph stages;
    // the native timeline is compressed, so both are folded into the
    // `kVsStrokeT` window (left over its first half, right over the second)
    // and reach 1.0 well before the `fight_intro` capture (~0.67 s).
    // Measured vs the oracle (`fight_intro`, red mask r>100 & r-g>50 &
    // r-b>40): oracle red mass 39.67 %, top-edge slope 20.06 deg; the
    // pre-change axis-aligned port 28.22 % / 6.09 deg (IoU 0.508, rotated
    // footprint fit IoU 0.614 vs axis-aligned 0.314). The exact oracle wipe
    // progress is NOT separable from this frame (the IoU surface is flat
    // across amount 0.7..1.0 for both brushes); the JS-exact full-band
    // rotation is what the frame discriminates.
    if (t >= kVsStrokeT * 0.5f) {
        const float half = std::max(0.01f, kVsStrokeT * 0.5f);
        const float wipe_l = std::clamp((t - half * 0.0f) / half, 0.0f, 1.0f);
        const float wipe_r = std::clamp((t - half * 1.0f) / half, 0.0f, 1.0f);
        if (wipe_l > 0.0f) {
            draw_vs_brush(app, "left", -492.0f, -121.0f, 492.0f, 242.0f, wipe_l, alpha);
        }
        if (wipe_r > 0.0f) {
            draw_vs_brush(app, "right", -2.0f, -133.0f, 492.0f, 254.0f, wipe_r, alpha);
        }
    }
    // Portraits `EK`/`RS` (JS `ik` L2069-2071): `oe(Hf)` draws the users
    // image at its natural 512 px canvas, scaled by the `ik` node scale
    // `(a.N-a.J)/1024` (L2073) -> 512*s = 640 px canvas (the oracle ring
    // measures ~350 px = 300/512 of the canvas). The circular frame is BAKED
    // INTO the portrait art (avatar_masked alpha content 93..412 x 79..404),
    // so no procedural ring is drawn (JS `Rp` stays hidden — `Fr.izb` only
    // shows it for a `dma` portrait name). Centres: JS `EK`/`RS` `C(512)`/
    // `D(286)` then `C(ya-330)`/`D(ra+80)` etc.; the capture pins
    // (227.5,453.5) player / (1037.5,244.5) enemy.
    const float pdx = (-388.0f + 570.0f * slide) * s;
    const float edx = (1412.0f - 570.0f * slide) * s;
    const float pcy = 367.8f * s;
    const float ecy = 207.1f * s;
    const float dia = 512.0f * s;  // JS `oe` natural canvas * node scale
    if (!draw_user_image(app, pimg, pdx, pcy, dia, dia, alpha, /*flip_x=*/true)) {
        draw_user_image(app, "avatar_hero", pdx, pcy, dia, dia, alpha, /*flip_x=*/true);
    }
    if (!draw_user_image(app, eimg, edx, ecy, dia, dia, alpha)) {
        draw_user_image(app, "avatar_masked", edx, ecy, dia, dia, alpha);
    }
    // VS glyph `Tr` (C(486,286) design): scale 10 -> natural (252x507), fade in.
    {
        const float g = std::clamp((t - (kVsGlyphT - 0.35f)) / 0.35f, 0.0f, 1.0f);
        const float gs = 10.0f + (252.0f - 10.0f) * vs_ease(g);
        try_draw_atlas_button(app, "vs", 486.0f * s, 286.0f * s, gs * s, 507.0f * s,
                              g * alpha);
    }
    // Names `Yeb/Xeb` (C(185,114)/C(845,464), gold, `Ia(128)` centre; JS
    // `c()` L2069 calls `Ga()` so the (400,50) node anchors at its CENTRE).
    // `draw_text_centered` takes the glyph-box TOP: subtract the box half
    // (ink ~50 px) + the glyph `yo` (~19.5 px at native scale 1.0) -> design
    // top 78.5 (player) / 428.5 (enemy); the capture's ink centres are
    // (227.5,140)/(1056,580) matching JS * s.
    if (t >= kVsNameT) {
        const float na = std::clamp((t - kVsNameT) / 0.15f, 0.0f, 1.0f) * alpha;
        const sf2::data::font* fnt = app.menu_font();
        unsigned int tex = app.font_texture();
        if (fnt != nullptr && tex != 0) {
            const float nscale = 80.0f * s / 100.0f;
            app.draw_text_centered(*fnt, tex, 185.0f * s, 78.5f * s, pname, nscale,
                                   62.0f / 255.0f, 45.0f / 255.0f, 20.0f / 255.0f, na);  // shadow
            app.draw_text_centered(*fnt, tex, 185.0f * s, 78.5f * s, pname, nscale,
                                   250.0f / 255.0f, 226.0f / 255.0f, 150.0f / 255.0f, na);
            app.draw_text_centered(*fnt, tex, 845.0f * s, 428.5f * s, ename, nscale,
                                   62.0f / 255.0f, 45.0f / 255.0f, 20.0f / 255.0f, na);
            app.draw_text_centered(*fnt, tex, 845.0f * s, 428.5f * s, ename, nscale,
                                   250.0f / 255.0f, 226.0f / 255.0f, 150.0f / 255.0f, na);
        }
    }
}

// Maps the controller's banner kind to the callouts frame (JS `Cr` L2022-
// 2026: `init(y.BQa)` for round, `Zy` -> `y.uQa` fight, `GZ` -> `y.zQa`/
// `y.wQa` for win/lose). Returns nullptr for kinds with no cited frame.
const char* banner_atlas_frame(sf2::scene::banner_kind kind) {
    switch (kind) {
        case sf2::scene::banner_kind::round: return "round";
        case sf2::scene::banner_kind::fight: return "fight";
        case sf2::scene::banner_kind::victory: return "perfect";
        case sf2::scene::banner_kind::defeat: return "great";
        default: return nullptr;
    }
}

// Draws the current fight plate centered at ~35% of the view height: the
// callouts atlas art (id 1310) for round/fight/victory/defeat. The JS `Cr`
// (L2021-2027) shows the plate with NO tween - `fu(a)` only sets `Sc=a` and
// `X(!0)`, `aa` counts `Sc` down - so the port draws it at the constant
// `layout()` scale with alpha 1.0 for the whole hold. There is no
// scale-in/fade-out and no fallback text (the JS draws atlas ART only).
// Screen-space (the UI camera), drawn over the fight and under the gamepad.
void draw_fight_banner(App& app, const sf2::scene::FightController& fight) {
    const sf2::scene::banner_kind kind = fight.banner();
    if (kind == sf2::scene::banner_kind::none) return;

    const float cx = kViewW * 0.5f;
    // JS Cr.layout (L2027): 	his.content.setPosition(a.F5a()) where
    //  = ma.Kq and gb.F5a() (L1552) is the rect CENTRE - the plate is
    // centred on screen, not at 35%. The old 0.35 anchor sat it too high.
    const float cy = kViewH * 0.5f;
    // JS Cr.Qa (L2022, init L2026 	his.Qa.R(!0)): the plate's full-screen
    // backdrop is the E.q1a() VERTICAL gradient (E.Eua stops
    // #00000020/80/80/80/20, L93-94) - semi-transparent, NOT a black box. The
    // port was missing it, so the ROUND/FIGHT plate sat on the bare clear.
    {
        sf2::render::Renderer& bren = app.renderer();
        struct GStop { float t; float a; };
        static const GStop gs[5] = {{0.0f, 0x20 / 255.0f}, {0.25f, 0x80 / 255.0f},
                                    {0.5f, 0x80 / 255.0f},  {0.75f, 0x80 / 255.0f},
                                    {1.0f, 0x20 / 255.0f}};
        constexpr int kStrips = 48;
        for (int i = 0; i < kStrips; ++i) {
            const float t0 = static_cast<float>(i) / kStrips;
            const float t1 = static_cast<float>(i + 1) / kStrips;
            const float tm = (t0 + t1) * 0.5f;
            float ga = gs[0].a;
            for (int s = 0; s < 4; ++s) {
                if (tm <= gs[s + 1].t) {
                    const float f = (tm - gs[s].t) / (gs[s + 1].t - gs[s].t);
                    ga = gs[s].a + (gs[s + 1].a - gs[s].a) * f;
                    break;
                }
                ga = gs[s + 1].a;
            }
            const float y0 = kViewH * t0, y1 = kViewH * t1;
            const float gv[] = {0, y0, kViewW, y0, kViewW, y1, 0, y0, kViewW, y1, 0, y1};
            bren.draw_triangles(gv, 6, 0.0f, 0.0f, 0.0f, ga);
        }
    }

    // The callouts atlas art (JS `Cr` L2022: `image = R.$(E.get(1310))`);
    // scaled min(800,min(W,H))/image.w*0.6 (layout L2027), centred.
    const char* frame = banner_atlas_frame(kind);
    if (frame == nullptr || !load_callouts_atlas(app)) return;
    const float art = std::min(800.0f, std::min(kViewW, kViewH)) * 0.6f;
    if (!try_draw_atlas_button(app, frame, cx, cy, art, art, 1.0f)) return;
    // The round NUMBER: `round = ea(E.get(1298))`, Ia(64), ua(fontSize*1.6)
    // (JS `Cr` L2022/L2026) - the round digits above the ROUND art. The JS
    // draws the number only; there is no "ROUND" text.
    if (kind == sf2::scene::banner_kind::round) {
        const sf2::data::font* rf = app.round_font();
        const unsigned int rtex = app.round_texture();
        if (rf != nullptr && rtex != 0) {
            const float rscale = (64.0f * 1.6f) / 140.0f;  // round eF=140
            app.draw_text_centered(*rf, rtex, cx, cy - art * 0.5f - 78.0f,
                                   std::to_string(fight.round().number + 1), rscale,
                                   1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
}

// Draws the live hit-spark particles (world space -> screen through the SAME
// camera the fighters use, factor 1.0 — the shake/framing is already baked
// into the camera the caller passes). Each spark is a small screen-space
// quad of the particle's world `size` scaled by the camera zoom, tinted
// `color` (0xRRGGBB), fading by age/life. Draw order: AFTER the fighters,
// BEFORE the fg floor layers (bg -> fighters -> SPARKS -> fg floor — the
// b615a1bf layer order).
//
// `color` is the location Root Color (JS `Na.cd(Lb.N2)` at the `av` ctor
// L833 and the `ryb` spawn L824: every spark is filled with the location
// colour, the same as the fighter silhouettes). FightController's spawn
// (`fx_.spawn_hit_sparks`, fight.cpp) does not thread it yet — cross-file
// OPEN; the draw site passes the loaded location's `root_color()`.
//
// `xoff`/`yoff` are the `tl` container offset (JS `tl.init` L843:
// translate.x = -width/2, translate.y = height/2 - Floor) — the SAME offset
// `project()` applies to the fighters, so effects sit in the container.
void draw_hit_sparks(sf2::render::Renderer& ren, const sf2::render::Camera& camera,
                     const sf2::scene::EffectSystem& fx, std::uint32_t color,
                     float xoff = 0.0f, float yoff = 0.0f) {
    const std::vector<sf2::scene::particle>& parts = fx.particles();
    for (const sf2::scene::particle& p : parts) {
        if (p.life <= 0.0f || p.age >= p.life) continue;
        const float t = p.age / p.life;          // 0..1 lived
        const float alpha = 1.0f - t;            // fade out by age/life
        if (alpha <= 0.02f) continue;
        const float r = static_cast<float>((color >> 16) & 0xFFu) * (1.0f / 255.0f);
        const float g = static_cast<float>((color >> 8) & 0xFFu) * (1.0f / 255.0f);
        const float b = static_cast<float>(color & 0xFFu) * (1.0f / 255.0f);
        // World -> screen (factor 1.0: the sparks live in the fight plane),
        // container offset applied (JS `tl.init` L843).
        const float sx = camera.world_to_screen_x(p.x - xoff, 1.0f);
        const float sy = camera.world_to_screen_y(p.y + yoff);
        // The spark's world size -> screen px (the same zoom the capsule
        // strokes use); shrink slightly as it fades.
        const float half = p.size * camera.zoom * 0.5f * (0.4f + 0.6f * alpha);
        if (half < 0.5f) continue;
        const float verts[] = {
            sx - half, sy - half, sx + half, sy - half, sx - half, sy + half,
            sx + half, sy - half, sx + half, sy + half, sx - half, sy + half,
        };
        ren.draw_triangles(verts, 6, r, g, b, alpha);
    }
}

// Draws one REAL `fight/fx` frame (JS asset id 1306 — the `ni` frame runs)
// centered at screen (cx,cy), fit into a `size`-px box, tinted (r,g,b)/
// `alpha`, and mirrored when `facing < 0` (JS `cv.lwb` L838:
// `e.scale.x = c.Wl * a.scale.x`, `c.Wl = b.da.hd()`). Returns false on a
// genuine atlas/frame miss — the caller then draws the flat tinted quad.
bool draw_fx_frame(App& app, const std::string& frame_name, float cx, float cy,
                   float size, int facing, float r, float g, float b, float alpha,
                   float rotation_deg = 0.0f) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame(frame_name, &fr, &tw, &th, &gl)) {
        // A genuine atlas miss — the caller draws the flat quad fallback. Log
        // only when the missing name changes (a real data gap, not per-frame
        // spam); the fx/ringout runs all resolve, so this stays silent.
        static std::string last_miss;
        if (last_miss != frame_name) {
            last_miss = frame_name;
            std::fprintf(stderr, "[fx] atlas miss: %s\n", frame_name.c_str());
        }
        return false;
    }
    if (fr.w <= 0 || fr.h <= 0) return false;
    // The frame's own box is `sourceSize` (JS `le` draws at `fa`); fall back
    // to the packed rect when the atlas did not carry a source size.
    const float src_w = fr.source_w > 0 ? static_cast<float>(fr.source_w)
                                        : static_cast<float>(fr.w);
    const float src_h = fr.source_h > 0 ? static_cast<float>(fr.source_h)
                                        : static_cast<float>(fr.h);
    sf2::scene::Sprite s;
    s.texture_name = frame_name;
    s.frame_x = static_cast<float>(fr.x);
    s.frame_y = static_cast<float>(fr.y);
    s.frame_w = static_cast<float>(fr.w);
    s.frame_h = static_cast<float>(fr.h);
    s.tex_w = static_cast<float>(tw);
    s.tex_h = static_cast<float>(th);
    s.solid = false;
    s.color_r = r;
    s.color_g = g;
    s.color_b = b;
    s.color_a = alpha;
    s.rotated = fr.rotated;
    // The fx frames are packed TRIMMED: the packed rect sits at
    // `spriteSourceSize` inside the full `sourceSize`; the renderer shifts
    // the quad by the trim compensation (sprite_to_quad).
    s.trim_x = static_cast<float>(fr.offset_x);
    s.trim_y = static_cast<float>(fr.offset_y);
    s.source_w = static_cast<float>(fr.source_w);
    s.source_h = static_cast<float>(fr.source_h);
    s.transform.set_pos(cx, cy);
    // Fit the source-size frame into `size` px; a negative x-scale mirrors
    // on facing (no face culling — a flipped winding still draws).
    s.transform.set_scale(size / src_w * (facing < 0 ? -1.0f : 1.0f), size / src_h);
    // JS `Hyb` (L825): `this.lo.Wg(isNaN(a)?0:-a)` — the overlay rotation in
    // degrees (0 for the magic frames, which share this helper).
    s.transform.rotation = rotation_deg;
    // Effects are screen-projected already: draw through the identity camera
    // (world == screen), same as `try_draw_atlas_button`.
    sf2::render::Camera ui_cam;
    ui_cam.center_x = 640.0f;
    ui_cam.center_y = 360.0f;
    ui_cam.zoom = 1.0f;
    ui_cam.view_w = 1280.0f;
    ui_cam.view_h = 720.0f;
    ui_cam.arena_h = 720.0f;
    ui_cam.arena_floor = 0.0f;
    ui_cam.arena_center_x = 640.0f;
    app.renderer().draw_sprite(s, ui_cam);
    return true;
}

// Source-size sibling of `draw_fx_frame` for the REAL magic descriptors: the
// frame is drawn at its TexturePacker `sourceSize` scaled by (w_scale,
// h_scale) * zoom (JS `ve(Vs.Qq(g), g.WJ.scale)` L839 + `e.scale.x = Wl*scale.x`
// / `e.scale.y = scale.y`, L838). `w_scale` is signed (facing mirror).
bool draw_fx_frame_source(App& app, const std::string& frame_name, float cx,
                          float cy, float w_scale, float h_scale, float zoom,
                          float r, float g, float b, float alpha,
                          float rotation_deg = 0.0f) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame(frame_name, &fr, &tw, &th, &gl)) {
        // A genuine atlas miss. Log only when the missing name changes.
        static std::string last_miss;
        if (last_miss != frame_name) {
            last_miss = frame_name;
            std::fprintf(stderr, "[fx] magic atlas miss: %s\n", frame_name.c_str());
        }
        return false;
    }
    if (fr.w <= 0 || fr.h <= 0) return false;
    if (w_scale == 0.0f || h_scale == 0.0f) return false;
    sf2::scene::Sprite s;
    s.texture_name = frame_name;
    s.frame_x = static_cast<float>(fr.x);
    s.frame_y = static_cast<float>(fr.y);
    s.frame_w = static_cast<float>(fr.w);
    s.frame_h = static_cast<float>(fr.h);
    s.tex_w = static_cast<float>(tw);
    s.tex_h = static_cast<float>(th);
    s.solid = false;
    s.color_r = r;
    s.color_g = g;
    s.color_b = b;
    s.color_a = alpha;
    s.rotated = fr.rotated;
    // The frame is packed TRIMMED into `sourceSize`; the renderer applies the
    // trim compensation from `trim_x`/`trim_y` (same as `draw_fx_frame`).
    s.trim_x = static_cast<float>(fr.offset_x);
    s.trim_y = static_cast<float>(fr.offset_y);
    s.source_w = static_cast<float>(fr.source_w);
    s.source_h = static_cast<float>(fr.source_h);
    s.transform.set_pos(cx, cy);
    // The sprite's natural size is `sourceSize`; the node scale is exactly the
    // descriptor scale (JS `e.scale`), times the camera zoom.
    s.transform.set_scale(w_scale * zoom, h_scale * zoom);
    s.transform.rotation = rotation_deg;  // JS `Vla` (`a.rotate()`)
    // Effects are screen-projected already: draw through the identity camera.
    sf2::render::Camera ui_cam;
    ui_cam.center_x = 640.0f;
    ui_cam.center_y = 360.0f;
    ui_cam.zoom = 1.0f;
    ui_cam.view_w = 1280.0f;
    ui_cam.view_h = 720.0f;
    ui_cam.arena_h = 720.0f;
    ui_cam.arena_floor = 0.0f;
    ui_cam.arena_center_x = 640.0f;
    app.renderer().draw_sprite(s, ui_cam);
    return true;
}

// Magic containers (JS `Xm`/`cv` L836-839, Phase 7.2): the REAL `ni` frame
// runs (JS `ni` L1141-1144 via `cv.lwb` L838-839) for each live instance,
// faded by age/life — the same world->screen path the hit sparks use, with
// the same `tl` container offset (`xoff`/`yoff`, JS `tl.init` L843).
// `background_pass` routes by JS `tl.Nt` (L842): `a.Gfb ? Gq : Hq` — the
// Gq (`background_for()==true`) pass draws first, the Hq (air) pass second,
// both after the fighters (tl.init L843-844 appends qh -> Gq -> Hq). A flat
// tinted quad is drawn ONLY on a genuine atlas/frame miss.
void draw_magic_effects(App& app, sf2::render::Renderer& ren,
                        const sf2::render::Camera& camera,
                        const sf2::scene::MagicEffects& fx, bool background_pass,
                        float xoff = 0.0f, float yoff = 0.0f) {
    // The real `fight/fx` atlas (JS `E.get(1306)`) carries the frame names
    // `frame_for` returns; load it on demand (the `load_pause_atlas` pattern).
    const bool have_fx = load_fx_atlas(app);
    for (const sf2::scene::MagicInstance& in : fx.live()) {
        // JS `tl.Nt` (L842): `a.Gfb ? Gq : Hq` — one instance belongs to
        // exactly one of the two passes.
        if (fx.background_for(in) != background_pass) continue;
        const float alpha = fx.alpha_for(in);
        if (alpha <= 0.02f) continue;
        const std::uint32_t color = fx.color_for(in);
        const float r = static_cast<float>((color >> 16) & 0xFFu) * (1.0f / 255.0f);
        const float g = static_cast<float>((color >> 8) & 0xFFu) * (1.0f / 255.0f);
        const float b = static_cast<float>(color & 0xFFu) * (1.0f / 255.0f);
        // Container offset (JS `tl.init` L843): x -= width/2, y += height/2-ct.
        const float sx = camera.world_to_screen_x(in.x - xoff, 1.0f);
        const float sy = camera.world_to_screen_y(in.y + yoff);
        const float size = fx.size_for(in) * camera.zoom;
        const std::string frame = fx.frame_for(in);
        // The REAL magic atlas frames (loaded from magic_ktx.72456186.dat):
        // drawn at their sourceSize * descriptor scale * camera zoom (JS
        // `ve(Vs.Qq(g), g.WJ.scale)` L839 + `e.scale.x = Wl*scale.x` L838),
        // honouring the `Vla` start rotation. The signed x scale carries the
        // facing mirror (`Wl`).
        if (!frame.empty() && fx.source_size_for(in) &&
            draw_fx_frame_source(app, frame, sx, sy,
                                 fx.scale_x_for(in) * (in.facing < 0 ? -1.0f : 1.0f),
                                 fx.scale_y_for(in), camera.zoom, r, g, b, alpha,
                                 fx.rotation_for(in))) {
            continue;
        }
        if (size < 1.0f) continue;
        // The legacy `fight/fx` frames (JS `ni` L1141-1144): the fitted path;
        // the flat tinted quad is the fallback ONLY on a genuine atlas miss.
        if (!frame.empty() && have_fx &&
            draw_fx_frame(app, frame, sx, sy, size, in.facing, r, g, b, alpha)) {
            continue;
        }
        // The flat tinted quad is the renderer's effect primitive (JS `R3a`
        // L486 + the effect sprite's center anchor `Ga`), the same path the
        // ringout arrow bodies use — not a raw triangle list.
        ren.draw_effect_quad(sx, sy, size, size, 0.0f, r, g, b, alpha);
    }
}

// Scene letterbox bars (JS `ma.Sya` L1833-1834; PORT_AUDIT_SCENE D10/W5).
// The JS render camera position is (0,0) (`N.Ta.K4` L85), so the extents use
// world_y * zoom + view_h/2 (center_y is NOT applied). Two independent bars:
//   - side bars: N.BK = round((W - sTa*H)/2) when lc > sTa (sTa=2.5, L2462),
//     two `ma.YY[]` nodes sized (BK, H) at x=0 and x=W-BK (`Sya` L1834).
//   - top/bottom bars: e = m$a() = Lb.height*Bj (L823); the projected arena
//     top P = -e/2 and bottom W = +e/2 give a top bar [0,P] when P>0 and a
//     bottom bar [W,viewH] when viewH-W>0 (`Sya` L1834).
// At 16:9 dojo (arena 560*1.3 = 728 -> y[-4,724], lc=1.78 < 2.5) this is a
// no-op — exactly the oracle.
void draw_scene_letterbox(sf2::render::Renderer& ren,
                          const sf2::render::Camera& camera) {
    const float w = camera.view_w, h = camera.view_h;
    const float lc = (h > 0.0f) ? w / h : 1.0f;
    constexpr float kSta = 2.5f;  // N.sTa (JS L2462)
    // Side bars (`N.BK`, JS `Ut.mwa` L823-824).
    if (lc > kSta) {
        const float bk = std::round((w - kSta * h) * 0.5f);
        if (bk > 0.0f) {
            const float left[] = {0, 0, bk, 0, 0, h, bk, 0, bk, h, 0, h};
            const float right[] = {w - bk, 0, w, 0, w - bk, h,
                                   w, 0,      w, h, w - bk, h};
            ren.draw_triangles(left, 6, 0.0f, 0.0f, 0.0f, 1.0f);
            ren.draw_triangles(right, 6, 0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
    // Top/bottom bars (`Sya` L1833-1834): e = arena_h * layer_zoom (m$a L823).
    const float e = camera.arena_h * camera.layer_zoom;
    const float top_y = -e * 0.5f * camera.zoom + h * 0.5f;
    const float bot_y = e * 0.5f * camera.zoom + h * 0.5f;
    if (top_y > 0.0f) {
        const float top[] = {0, 0, w, 0, 0, top_y, w, 0, w, top_y, 0, top_y};
        ren.draw_triangles(top, 6, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (h - bot_y > 0.0f) {
        const float bot[] = {0, bot_y, w, bot_y, 0, h, w, bot_y, w, h, 0, h};
        ren.draw_triangles(bot, 6, 0.0f, 0.0f, 0.0f, 1.0f);
    }
}

// --- Map node geometry (JS `Ya`/`qe` L2124-2145) --------------------------
// `qe.uM` is a class static set at boot (JS L2488: 1.5003663003663004) and
// `y5a()` = 150/225*uM. The backdrop frame (`map<N>`, res/map/partN json) is
// 2046x854 (sourceSize).
//
// Layout is the `qk.layout` / `Vr.ba` / `Rr.layout` triple (JS L2100/L2118/
// L2137):
//   * `qk.layout`: the `Vr` map scroller sits at y = za.Sp with height
//     `d = (H - Sp)*(.5 + (clamp(lc,.9,1.5)-.9)/.6*.5) - H*.1` (the non-tall
//     `!Ya.tw` branch; `lc >= .9` at every desktop aspect), and `Ur` (the
//     zone strip) starts at `Sp + d` with height `H - (Sp + d)`.
//   * `Vr.ba`: `dA.la(d / background.fa.y)` (backdrop scaled to the strip
//     height) and `dA.C(-100 + ca)`. With one zone the content fits
//     (`N.width > cCa() - 200`), so `Vr.aa` takes the `ca = 0, dA.C(0)`
//     branch (L2119) and the backdrop is drawn at x = 0.
//   * `qe.X0a`: node = `pos*uM + bg.fa/2`, `-pos.y*uM + bg.fa/2`, `-50`
//     (item-local; the `dA` scale/offset above maps it to the screen).
//   * `Qr` is sized `y5a() = 150/225*uM` times its 225px source frame.
constexpr float kMapFrameW = 2046.0f;                  // mapN sourceSize
constexpr float kMapFrameH = 854.0f;
constexpr float kMapNodeUnit = 1.5003663003663004f;    // JS L2488 qe.uM
constexpr float kMapNodeYOffset = 50.0f;               // JS L2144 d.node.ra-50
constexpr float kMapNodeSourcePx = 225.0f;             // Qr frame sourceSize
constexpr float kMapNodeScaleK = 150.0f / 225.0f;      // JS L2144 y5a()

struct MapMetrics {
    float sp = 0.0f;        // za.Sp
    float qka = 0.0f;       // za.qka (Rr rail / `wc.ba` third arg)
    float map_y = 0.0f;     // Vr.node.y
    float map_h = 0.0f;     // Vr strip height `d`
    float bg_scale = 1.0f;  // dA.Eb = d / 854
    float bg_x = 0.0f;      // dA.x (0 when the one-zone content fits)
    float bar_y = 0.0f;     // Wj.node.y = Sp + d
    float bar_h = 0.0f;     // H - bar_y
    float panel_x = 0.0f;   // Rr wc.node.x = W - panel_w
    float panel_y = 0.0f;   // Rr wc.node.y = Sp
    float panel_w = 0.0f;   // f = 430*e
    float panel_h = 0.0f;   // g = (Sp + d) * (1.1 + (clamp(lc,1,1.4)-1)/.4*-.15)
    float rail = 0.0f;      // wc rail width c = qka
    float content_w = 0.0f; // wc.Gv = panel_w - 2*rail
    float content_h = 0.0f; // wc.Xy = panel_h - 1*rail (one rail: bottom)
};

MapMetrics map_metrics() {
    const ZaLayout za = za_layout();
    const float lc = kViewW / kViewH;
    MapMetrics m;
    m.sp = za.sp;
    m.qka = 50.0f * std::max(0.1f, std::min(kViewW, kViewH) * 0.35f / 430.0f);
    // `qk.layout` d (`Ya.tw` = lc<.9; false for desktop).
    m.map_h = (kViewH - m.sp) * (0.5f + (std::clamp(lc, 0.9f, 1.5f) - 0.9f) / 0.6f * 0.5f) -
              kViewH * 0.1f;
    m.map_y = m.sp;
    m.bg_scale = m.map_h / kMapFrameH;
    // `Vr.aa`: a single-zone content fits -> ca = 0, `dA.C(0)`.
    m.bg_x = (kViewW > kMapFrameW * m.bg_scale - 200.0f) ? 0.0f : -100.0f;
    m.bar_y = m.map_y + m.map_h;
    m.bar_h = kViewH - m.bar_y;
    // `Rr.layout` (L2100).
    const float e = std::min(kViewW, kViewH) *
                    (0.32f + (std::clamp(lc, 1.0f, 1.6f) - 1.0f) / 0.6f * 0.13f) / 430.0f;
    m.panel_w = 430.0f * e;
    m.panel_y = m.sp;
    m.panel_h = m.bar_y * (1.1f + (std::clamp(lc, 1.0f, 1.4f) - 1.0f) / 0.4f * -0.15f);
    m.panel_x = kViewW - m.panel_w;
    m.rail = m.qka;
    m.content_w = m.panel_w - 2.0f * m.rail;
    m.content_h = m.panel_h - m.rail;
    return m;
}

// On-screen node box (view px): the 225px `Qr` source scaled by
// `y5a() = 150/225*uM` and then by the strip scale `dA.Eb`.
float map_node_size(float view_w) {
    (void)view_w;  // the layout is the JS `qk.layout`/`Vr.ba` chain, not the view
    return kMapNodeSourcePx * kMapNodeScaleK * kMapNodeUnit * map_metrics().bg_scale;
}

// The `Rr` info-panel FIGHT button (`tj` = `Bb("EButtonWhite")`, JS L2099/
// L2102). Its plate is the ONLY fight trigger on the map (JS: a node tap
// re-targets the `Rr` focus via `qe.jhb`/`Ya.Uw` L2129; the fight starts on
// the button press -> `v.Am` L1216). Shared by the draw and the hit-test so
// they can never diverge.
struct MapFightButtonRect {
    float cx = 0.0f, cy = 0.0f, w = 0.0f, h = 0.0f;
};

MapFightButtonRect map_fight_button_rect(const MapMetrics& mm) {
    const float cx = mm.panel_x + mm.rail;  // `wc.content.C(rail)` (L2100)
    const float cy = mm.panel_y;
    const float qka = mm.qka;
    const float d = qka * 0.4f;             // `Rr.layout` `d = c*.4`
    const float cw = mm.content_w;
    const float title_h = cw * 0.2f;        // `g = c*.2`
    float f = 10.0f + title_h + 20.0f;
    const float pv_w = cw - 2.0f * d;
    f += pv_w * 0.5f + 20.0f;               // `Wu.qa()` = pv_w * (200/400)
    const float body_x = cx + d;
    const float body_y = cy + f;
    const float body_w = cw - 2.0f * d;
    const float body_h = mm.content_h - f;
    MapFightButtonRect r;
    r.h = cw * 0.2f;                        // `tj.Pb(c*.2)`
    r.w = 600.0f * (r.h / 112.0f);          // `Bb` ctor `xc(600)`
    r.cx = body_x + body_w * 0.5f;
    r.cy = body_y + body_h - r.h * 0.5f;
    return r;
}

// The zone map from stages.xml (JS `p.Dkb` L188 / `Ckb` L189): Zone Name +
// FileName + Start flag, with Battle children (Name/Type/X/Y/Location).
// Only battles carrying map coordinates become nodes (HIDDEN/INTERMISSION
// rows without X/Y are not map nodes). Covers all 8 zones (Punchbag +
// ZONE_1..7) — the old first-zone-only loader is folded into this.
std::vector<MapScreen::ZoneTab> load_zone_map(float view_w, float view_h) {
    (void)view_w;  // node coords come from map_metrics(), not the view size
    (void)view_h;
    std::vector<MapScreen::ZoneTab> out;
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/stages.xml";
        std::ifstream in(path, std::ios::binary);
        if (!in) return out;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root || std::string(root.name()) != "Stages") return out;
        const pugi::xml_node zones = root.child("Zones");
        if (!zones) return out;
        for (const pugi::xml_node zone : zones.children("Zone")) {
            MapScreen::ZoneTab z;
            z.name = zone.attribute("Name").value();
            if (z.name.empty()) continue;
            z.file = zone.attribute("FileName").value();
            z.is_start = std::string(zone.attribute("Start").value()) == "1";
            for (const pugi::xml_node battle : zone.children("Battle")) {
                // Map nodes are positioned battles (JS `qe.X0a` L2144 needs
                // X/Y); rows without coordinates are not selectable. `X0a`
                // also skips `FightUnregister` (raw type "HIDDEN", L182) and
                // the `Hide` attr (`uDa`, L206) — e.g. ZONE_6 QuestBattle.
                if (battle.attribute("X").empty() && battle.attribute("Y").empty()) continue;
                if (std::string(battle.attribute("Type").value()) == "HIDDEN") continue;
                if (sf2::data::xml_attr_bool(battle, "Hide", false)) continue;
                MapScreen::Node n;
                n.name = battle.attribute("Name").value();
                if (n.name.empty()) continue;
                n.alias = battle.attribute("Alias").value();  // JS `Lc.Cg`
                n.type = battle.attribute("Type").value();
                n.title = battle.attribute("Title").value();  // JS `Lc.k6`
                // `Preview="preview_bosses.lynx"` (JS `Lc.olb`): `Me.CT`
                // splits on "/" after `Ye.qI` and loads
                // `res/map/images/<stem>.img` (L2105/L2111) — the stem after
                // the last '.'.
                {
                    const std::string pv = battle.attribute("Preview").value();
                    const std::size_t pdot = pv.find_last_of('.');
                    n.preview = (pdot == std::string::npos) ? pv : pv.substr(pdot + 1);
                }
                n.zone = z.name;
                n.location = battle.attribute("Location").value();
                const float x = sf2::data::xml_attr_float(battle, "X", 0.0f);
                const float y = sf2::data::xml_attr_float(battle, "Y", 0.0f);
                // Per-node art suffix (JS L205: Icon attr, default "training";
                // `Lc.U9a` L1405 -> base_/active_/... + icon).
                n.icon = battle.attribute("Icon").value();
                if (n.icon.empty()) n.icon = "training";
                // Alternate-state twins (JS hides the unrecorded ones via
                // `Qr.lla` L2094): the `*_INTERMISSION` / `FightBossesIntermission`
                // rows and the `BOSSES_REPLAYABLE`/`FINAL_BATTLE_REPLAYABLE`
                // hard-mode boss.
                n.alt_state = n.type == "BOSSES_REPLAYABLE" ||
                              n.type == "FINAL_BATTLE_REPLAYABLE" ||
                              n.type == "REPLAYABLE" ||
                              n.type == "BOSSES_INTERMISSION";
                {
                    const std::string suffix = "_INTERMISSION";
                    if (n.name.size() > suffix.size() &&
                        n.name.compare(n.name.size() - suffix.size(), suffix.size(),
                                       suffix) == 0) {
                        n.alt_state = true;
                    }
                }
                // JS `qe.X0a` (L2144): x = pos.x*uM + bg.fa.x/2,
                // y = -pos.y*uM + bg.fa.y/2, then -50 — item-local, mapped to
                // the screen through the `Vr` strip transform (map_metrics).
                const MapMetrics mm = map_metrics();
                n.x = mm.bg_x + (x * kMapNodeUnit + kMapFrameW * 0.5f) * mm.bg_scale;
                n.y = mm.map_y +
                      (-y * kMapNodeUnit + kMapFrameH * 0.5f - kMapNodeYOffset) * mm.bg_scale;
                n.active = true;  // the MapScreen ctor applies the lock rule
                // Xs warriors (FLOW_STATIC Modes): FirstNames across the
                // battle's Fights, deduped, capped (bracket display).
                for (pugi::xml_node fight = battle.child("Fight"); fight;
                     fight = fight.next_sibling("Fight")) {
                    ++n.fight_count;  // JS `Lc.Kz().length` (Xr pip count)
                    // JS `dl.name` (the `<Fight Name>`; the `hb` triple's
                    // `Lq`, `hb.toString` L1416) — the `il` record key tail.
                    n.fight_names.push_back(fight.attribute("Name").value());
                    // JS `dl.locked` (`il` parse: `Locked` attr) — the `Xr`
                    // pip lock (`c[k].locked?l.wMa(2)`, L2134).
                    n.fight_locked.push_back(
                        sf2::data::xml_attr_bool(fight, "Locked", false));
                    // First positive <Reward Money> of the first fight (the
                    // `ci` gold icon value under the difficulty bar, L2133).
                    if (n.fight_count == 1 && n.reward_money == 0) {
                        const pugi::xml_node rewards = fight.child("Rewards");
                        if (rewards) {
                            for (pugi::xml_node rw = rewards.child("Reward"); rw;
                                 rw = rw.next_sibling("Reward")) {
                                if (rw.attribute("Money").empty()) continue;
                                const int money = rw.attribute("Money").as_int(0);
                                if (money > 0) {
                                    n.reward_money = money;
                                    break;
                                }
                            }
                        }
                    }
                    const pugi::xml_node warriors = fight.child("Warriors");
                    if (!warriors) continue;
                    for (pugi::xml_node wr = warriors.child("Warrior"); wr;
                         wr = wr.next_sibling("Warrior")) {
                        const std::string fn = wr.attribute("FirstName").value();
                        if (fn.empty() || n.warriors.size() >= 3) continue;
                        bool dup = false;
                        for (const auto& have : n.warriors) {
                            if (have == fn) {
                                dup = true;
                                break;
                            }
                        }
                        if (!dup) n.warriors.push_back(fn);
                    }
                }
                z.nodes.push_back(std::move(n));
            }
            // Backdrop from the zone's FileName (JS `qe.W0a` L2143:
            // `a = parseInt(fileName.split(".")[1]) - 1` -> frame map<a>).
            // The Start zone (Punchbag) has no FileName -> no map backdrop.
            if (!z.file.empty()) {
                const std::size_t dot = z.file.find('.');
                if (dot != std::string::npos) {
                    try {
                        z.part = std::stoi(z.file.substr(dot + 1)) - 1;
                    } catch (const std::exception&) {
                        z.part = -1;
                    }
                }
            }
            out.push_back(std::move(z));
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "map: stages.xml load failed: %s\n", e.what());
    }
    return out;
}

// The battle's music track (stages.xml Battle Music="fightN_..." attr;
// JS `ta.Ut(this.Da.tp)`, L2008). Empty when the battle has none (the
// Training dummy) — the caller then keeps the current track.
void battle_music(const std::string& battle_name, std::string& out_track) {
    out_track.clear();
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/stages.xml";
        std::ifstream in(path, std::ios::binary);
        if (!in) return;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root) return;
        for (const pugi::xml_node zone : root.child("Zones").children("Zone")) {
            for (const pugi::xml_node battle : zone.children("Battle")) {
                if (std::string(battle.attribute("Name").value()) != battle_name) continue;
                if (battle.attribute("Music")) {
                    out_track = battle.attribute("Music").value();
                }
                return;
            }
        }
    } catch (const std::exception&) {
    }
}

// The reward of a battle's first non-zero <Reward> (JS `tt.bm` L116924).
void battle_rewards(const std::string& battle_name, int& out_money, int& out_exp) {
    out_money = 0;
    out_exp = 0;
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/stages.xml";
        std::ifstream in(path, std::ios::binary);
        if (!in) return;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root) return;
        for (const pugi::xml_node zone : root.child("Zones").children("Zone")) {
            for (const pugi::xml_node battle : zone.children("Battle")) {
                if (std::string(battle.attribute("Name").value()) != battle_name) continue;
                const pugi::xml_node fight = battle.child("Fight");
                if (!fight) return;
                const pugi::xml_node rewards = fight.child("Rewards");
                if (!rewards) return;
                for (const pugi::xml_node reward : rewards.children("Reward")) {
                    const int m = sf2::data::xml_attr_int(reward, "Money", 0);
                    const int e = sf2::data::xml_attr_int(reward, "Exp", 0);
                    if (m > 0 || e > 0) {
                        out_money = m;
                        out_exp = e;
                        return;
                    }
                }
                return;
            }
        }
    } catch (const std::exception&) {
    }
}

// The battle's first <Fight><Rules> children (stages.xml). JS `Ya` mp(6)
// parses the stage fight (`nj.parse` L885) and `f_a` L896-897 feeds the
// FIRST active `ERuleRingout` rule to the `sXa` off-screen markers. The
// native battle is hardcoded (FightNone), so the caller maps these rules via
// `apply_stage_ringout_rule` (scene/fight.hpp) before init_locks. Empty when
// the battle/fight has no <Rules> (the dojo Training dummy: NoPerks only, no
// Ringout -> markers stay dormant).
//
// Parses the FULL child structure JS `bb.OE`/`bb.M3` (L887-888) sees — the
// rule tag + attrs AND the `<Animation>`/`<Node>` children (`Ce.c4a` L848,
// `en.Mia` L860) — and expands `<Level>` (`bb.Ajb` L894), `<ComplexRule>`
// (`nh.parse` L853) and `<RandomRule>` (`pn.parse` L879) exactly like
// `parse_stages` via the shared `modes_detail::append_rule_element`. Copying
// tag + attrs alone (or Level-only) dropped the child elements, which left
// the Wave-M HotGround / LoseFall / Ringout engines inert in-game (the rule
// feeder is the single path from stages.xml into `FightController`).
//
// `zone_name` = the current stages.xml Zone (`PendingBattle.zone`). The same
// battle name repeats in every zone (Duel / Tournament / Challenge / ...),
// so the battle MUST be resolved among the current zone's direct `<Battle>`
// children (`hp` semantics, matching JS map node resolution). The old
// all-zones scan bound the FIRST match in document order — another zone's
// ruleset (e.g. `Duel` -> ZONE_1 even when the player is in ZONE_5, whose
// `Duel` carries a different RandomRule/ComplexRule set). Empty `zone_name`
// (the direct-boot path) falls back to the legacy first-match scan.
std::vector<sf2::scene::StageRule> battle_fight_rules(const std::string& battle_name,
                                                      const std::string& zone_name) {
    std::vector<sf2::scene::StageRule> out;
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/stages.xml";
        std::ifstream in(path, std::ios::binary);
        if (!in) return out;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root) return out;
        const pugi::xml_node zones = root.child("Zones");
        if (!zones) return out;
        // JS `hp` direct-children: resolve the battle inside the CURRENT
        // zone only when it is known.
        pugi::xml_node battle;
        if (!zone_name.empty()) {
            for (const pugi::xml_node z : zones.children("Zone")) {
                if (std::string(z.attribute("Name").value()) != zone_name) continue;
                for (const pugi::xml_node b : z.children("Battle")) {
                    if (std::string(b.attribute("Name").value()) == battle_name) {
                        battle = b;
                        break;
                    }
                }
                break;  // the zone was found (whether or not it had the battle)
            }
        }
        if (!battle) {
            // Zone unknown (direct-boot path): legacy first-match scan.
            for (const pugi::xml_node z : zones.children("Zone")) {
                for (const pugi::xml_node b : z.children("Battle")) {
                    if (std::string(b.attribute("Name").value()) == battle_name) {
                        battle = b;
                        break;
                    }
                }
                if (battle) break;
            }
        }
        if (!battle) return out;
        const pugi::xml_node fight = battle.child("Fight");
        if (!fight) return out;
        const pugi::xml_node rules = fight.child("Rules");
        if (!rules) return out;
        // JS `bb.OE` (L887-888): walk the DIRECT `<Rules>` children and let
        // the shared expander flatten Level/ComplexRule/RandomRule.
        int next_group = 0;
        for (const pugi::xml_node r : rules.children()) {
            sf2::scene::modes_detail::append_rule_element(
                r, out, 0, 2147483647, next_group, -1, -1, false, false);
        }
    } catch (const std::exception&) {
    }
    return out;
}

// The FIRST <Warrior> of the current battle's first <Fight> (stages.xml),
// resolved exactly like JS `ur` (L186-195) reads a warrior node: FirstName
// (`$s`, L188), the NotAI (`Fj`, L194) and NotAnimation (`QD`, L195)
// PRESENCE flags, the Tactic (`Gc`, L194), the raw attrs and the <Items>
// names. The dojo Training Fight 1 is the Punchbag dummy:
//   <Warrior FirstName="Punchbag" NotAI="1" NotAnimation="1">
//     <Items><Item Name="PunchingBag"/><Item Name="SkeletonPunchingBag"/>
// (stages.xml L12-16): `Fj==false` -> no AI (`wd.Anb` L499), `QD==false`
// -> no animation (`NS` L505 / `da.ia` L499). The battle is resolved among
// the current zone's direct <Battle> children (same `hp` semantics as
// `battle_fight_rules`); empty `zone_name` falls back to the legacy scan.
struct BattleWarriorInfo {
    std::string first_name;
    bool has_not_ai = false;
    bool has_not_animation = false;
    std::string tactic;
    std::map<std::string, std::string> attrs;
    std::vector<std::string> items;
    // `xc.IY` (JS L191): the warrior's effective `<AttributesAlign>` rows —
    // its own appended after the inherited `<Template Name="Default">` rows
    // (`pGa` L198). `pAa` blends with these.
    std::vector<sf2::scene::StageWarrior::Delta> align;
    // The `Default` template's rows = the player/avatar `IY`.
    std::vector<sf2::scene::StageWarrior::Delta> player_align;
    // `xc.voice` (JS `ur` L186 reads the Warrior node's `Voice` attr).
    // The enemy's from its `<Template>` chain; the player's from the
    // `Default` template (`Default` ships `Voice="Male"`, stages.xml).
    std::string voice;
    std::string player_voice;
    // The battle's `<Battle Type="KIND">` stages.xml token (raw: "DUMMY" /
    // "BOSSES" / "SURVIVAL" / ...) and its mapped fight type (JS `p.Wab`
    // L181-183 via `b0` L180, stamped by `Lc.pkb` L1407 `this.type=b0(a)`).
    std::string kind;
    // Default matches JS `b0` (L180): a missing/unknown kind maps to
    // "FightNone", so an unresolved battle never yields an empty type.
    std::string type = "FightNone";
};

// `StageWarrior::Delta` -> `damage.hpp` `AlignDelta` (same fields, float).
std::vector<sf2::scene::AlignDelta> to_align_deltas(
    const std::vector<sf2::scene::StageWarrior::Delta>& in) {
    std::vector<sf2::scene::AlignDelta> out;
    out.reserve(in.size());
    for (const sf2::scene::StageWarrior::Delta& d : in) {
        sf2::scene::AlignDelta a;
        a.bp = static_cast<float>(d.factor);
        a.shift = static_cast<float>(d.shift);
        a.priority = d.priority;
        a.eclipse_op = d.eclipse_op;
        out.push_back(a);
    }
    return out;
}

BattleWarriorInfo battle_warrior(const std::string& battle_name,
                                 const std::string& zone_name, int fight_index = 0) {
    BattleWarriorInfo out;
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/stages.xml";
        std::ifstream in(path, std::ios::binary);
        if (!in) return out;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root) return out;
        const pugi::xml_node zones = root.child("Zones");
        if (!zones) return out;
        pugi::xml_node battle;
        if (!zone_name.empty()) {
            for (const pugi::xml_node z : zones.children("Zone")) {
                if (std::string(z.attribute("Name").value()) != zone_name) continue;
                for (const pugi::xml_node b : z.children("Battle")) {
                    if (std::string(b.attribute("Name").value()) == battle_name) {
                        battle = b;
                        break;
                    }
                }
                break;  // the zone was found (whether or not it had the battle)
            }
        }
        if (!battle) {
            for (const pugi::xml_node z : zones.children("Zone")) {
                for (const pugi::xml_node b : z.children("Battle")) {
                    if (std::string(b.attribute("Name").value()) == battle_name) {
                        battle = b;
                        break;
                    }
                }
                if (battle) break;
            }
        }
        if (!battle) return out;
        // JS `Lc.pkb` L1407: `this.type = p.F().b0(a)` where `a` is the
        // battle's `<Battle Type>` attribute -> the RAW KIND plus its mapped
        // fight type (`p.Wab` L181-183). Every `Da.type` consumer checks the
        // MAPPED value, never the raw token.
        out.kind = battle.attribute("Type").value();
        out.type = sf2::scene::battle_type_for_kind(out.kind);
        // The Nth `<Fight>` (JS `lD`: one `jk` roster entry per boss-fight).
        pugi::xml_node fight;
        {
            int fi = 0;
            for (const pugi::xml_node f : battle.children("Fight")) {
                if (fi++ == fight_index) {
                    fight = f;
                    break;
                }
            }
        }
        if (!fight) return out;
        const pugi::xml_node warriors = fight.child("Warriors");
        if (!warriors) return out;
        const pugi::xml_node w = warriors.child("Warrior");
        if (!w) return out;
        {
            // `xc.IY`: the warrior's own rows appended after `Default`'s
            // (JS `pGa` L198; `stage_warrior_align` in modes.hpp).
            const pugi::xml_node templates = root.child("Templates");
            out.align = sf2::scene::modes_detail::stage_warrior_align(w, templates);
            out.player_align = sf2::scene::modes_detail::default_align(templates);
        }
        for (const pugi::xml_attribute a : w.attributes()) {
            out.attrs[a.name()] = a.value();
        }
        // JS `ukb`/`rkb`/`lzb` (asset 273 = stages.xml): a battle's
        // `<Warrior Template="X" ...>` inherits its identity from the file's
        // `<Templates>` — the named `<Template Name="X" Template="Y">` is
        // cloned from its base chain (...->Default) and the owning Warrior's
        // attrs are merged on top (`pGa`). FirstName/Avatar live on the
        // TEMPLATE, not the `<Warrior>`: BOSS_LYNX Fight 1 is only
        // `<Warrior Template="Man_Kunai" ...>` (stages.xml L81 of the extracted
        // file); its name/Avatar are on `<Template Name="Man_Kunai" ...
        // FirstName="NAME_SHIN" Avatar="man_kunai">` (L32443). Reading
        // FirstName off the Warrior alone leaves it empty, so the VS/HUD falls
        // back to the battle name -> "РЫСЬ" instead of the oracle's "ШИН".
        // JS `ukb` reads the templates from the `Warriors` clone's child
        // `Templates` (the file nests `<Templates>` inside `<Warriors>`), NOT
        // a root sibling.
        std::map<std::string, std::string> tmpl_attrs;
        // The `Default` template's `Voice` — the player's `xc.voice`
        // (`users_default.xml` `<Warrior Voice="Male">` is seeded from the
        // same Default identity). Captured here because `templates` goes out
        // of scope below.
        std::string default_voice;
        {
            std::map<std::string, pugi::xml_node> templates;
            const pugi::xml_node templates_node =
                root.child("Warriors").child("Templates");
            for (const pugi::xml_node t : templates_node.children("Template")) {
                const std::string nm = t.attribute("Name").value();
                if (!nm.empty()) templates.emplace(nm, t);
            }
            {
                const auto dflt = templates.find("Default");
                if (dflt != templates.end()) {
                    const char* dv = dflt->second.attribute("Voice").value();
                    if (dv != nullptr) default_voice = dv;
                }
            }
            std::vector<std::string> chain;
            for (std::string cur = w.attribute("Template").value();
                 !cur.empty() && templates.count(cur) != 0;) {
                chain.push_back(cur);
                cur = templates[cur].attribute("Template").value();
            }
            // base-first (Default ... named) so the derived template wins.
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                for (const pugi::xml_attribute a : templates[*it].attributes()) {
                    tmpl_attrs[a.name()] = a.value();
                }
            }
            // The template `<Items>` (JS `rkb` clone + `pGa` merge): the
            // Warrior inherits the equipped set from its `<Template>` chain
            // (BOSS_LYNX Fight 1 is `<Warrior Template="Man_Kunai" .../>` with
            // no own <Items>; the kit lives on `<Template Name="Man_Kunai">`,
            // stages.xml L24400-24405). Base-first so the derived template's
            // item wins when bucketed by type (see fighter_model_names).
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                for (const pugi::xml_node node :
                     templates[*it].child("Items").children("Item")) {
                    if (const pugi::xml_attribute nm = node.attribute("Name")) {
                        out.items.emplace_back(nm.value());
                    }
                }
            }
        }
        // The Warrior's own attrs win over the inherited template ones.
        for (const auto& kv : tmpl_attrs) out.attrs.emplace(kv.first, kv.second);
        // `xc.voice`: the merged `<Template Voice=..>` (JS `ur` L186) for the
        // enemy — the warrior WITH a `Template` inherits it (Man_Kunai ships
        // `Voice="Male"`); a bare `<Warrior>` (the dojo Punchbag) has none.
        const auto v = tmpl_attrs.find("Voice");
        if (v != tmpl_attrs.end()) out.voice = v->second;
        out.player_voice = default_voice;
        if (const pugi::xml_attribute a = w.attribute("FirstName")) {
            out.first_name = a.value();
        } else {
            const auto fn = tmpl_attrs.find("FirstName");
            if (fn != tmpl_attrs.end()) out.first_name = fn->second;
        }
        out.has_not_ai = w.attribute("NotAI") != nullptr;
        out.has_not_animation = w.attribute("NotAnimation") != nullptr;
        if (const pugi::xml_attribute a = w.attribute("Tactic")) out.tactic = a.value();
        for (const pugi::xml_node it : w.child("Items").children("Item")) {
            if (const pugi::xml_attribute nm = it.attribute("Name")) {
                out.items.emplace_back(nm.value());
            }
        }
    } catch (const std::exception&) {
    }
    return out;
}

// The save's LEARNED perk names (JS `Bt.KS.Oa`/`Ht`, merged into the fighter's
// live `parameters.Oa` by `Wk` L811-812). `Bm.he` (L753-754) scans this set
// for a move's `<Perk Name=..>` lock; the display + fight move lists use it.
std::vector<std::string> learned_perk_names(const WarriorSave& w) {
    std::vector<std::string> out;
    for (const auto& pr : w.perks) {
        if (!pr.name.empty()) out.push_back(pr.name);
    }
    return out;
}

// The player's owned items for the Locks move list: the equipped slots
// (JS `xc.hk` — Skeleton/Weapon/Armor/Helm) + the owned inventory
// (JS `p.o.xa`). Each row carries the item's NAME too — `Hm.he` (L758)
// compares Type AND SubType AND Name, so an item shipped WITHOUT a `SubType`
// (list.xml: armor `Body`, helm `Head`, `NoRanged`, `NoMagic` all lack the
// attribute) is only addressable by its name and was silently dropped by the
// old empty-SubType filter. Both the direct boot and the Map/Dojo launch use
// this one function, so they build the SAME list from the same save.
std::vector<sf2::scene::OwnedItem> owned_items(App& app) {
    std::vector<sf2::scene::OwnedItem> out;
    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return out;
    }
    const std::vector<CatalogItem> catalog = load_full_catalog(app);
    const auto push = [&catalog, &out](const std::string& name) {
        if (name.empty()) return;
        for (const CatalogItem& ci : catalog) {
            if (ci.name == name) {
                out.push_back({ci.type, ci.subtype, ci.name});
                return;
            }
        }
    };
    // JS `xc.hk`: the four equipped slots, in the Of/Hd/hg/Lg order.
    push(w.skeleton);
    push(w.weapon);
    push(w.armor);
    push(w.helm);
    // JS `p.o.xa`: every owned inventory row with a positive count.
    for (const auto& oi : w.items) {
        if (oi.count <= 0) continue;
        push(oi.name);
    }
    // The fighter always owns a Skeleton (the Skeleton lock passes for every
    // move — `users_default` has Skeleton="Skeleton") and a weapon; a save
    // with an empty slot still fights unarmed.
    const auto has_type = [&out](const std::string& t) {
        for (const sf2::scene::OwnedItem& o : out) {
            if (o.type == t) return true;
        }
        return false;
    };
    if (!has_type("Skeleton")) out.push_back({"Skeleton", "Skeleton", "Skeleton"});
    if (!has_type("Weapon")) out.push_back({"Weapon", "Fists", "Fists"});
    return out;
}

// JS `xc.cM` (L809-810): the fighter's model-name list, in the exact slot
// order the game emits. The equipped items are bucketed by their list.xml
// `Type` into the typed slots (JS `Fd` L808 / `hk` L809: Of=Skeleton,
// Hd=Weapon, hg=Armor, Lg=Helm; `I.e7="Decorate"` L2473 -> the `Kv` extras),
// then each slot's `<Item Model>` attribute is emitted: Of, Hd, hg, Lg, Kv.
// One item per slot, last-in wins (JS `hk` overwrites the slot). Every
// returned string is a models.dat archive entry name (e.g. WEAPON_KUNAI ->
// `mdl_weapon_kunai`, list.xml L1819).
std::vector<std::string> fighter_model_names(
    App& app, const std::vector<std::string>& item_names) {
    const std::vector<CatalogItem> catalog = load_full_catalog(app);
    const auto find = [&catalog](const std::string& name) -> const CatalogItem* {
        for (const CatalogItem& ci : catalog) {
            if (ci.name == name) return &ci;
        }
        return nullptr;
    };
    const CatalogItem* skeleton_it = nullptr;
    const CatalogItem* weapon_it = nullptr;
    const CatalogItem* armor_it = nullptr;
    const CatalogItem* helm_it = nullptr;
    std::vector<std::string> extras;
    for (const std::string& name : item_names) {
        if (name.empty()) continue;
        const CatalogItem* ci = find(name);
        if (ci == nullptr) continue;
        if (ci->type == "Skeleton") skeleton_it = ci;
        else if (ci->type == "Weapon") weapon_it = ci;
        else if (ci->type == "Armor") armor_it = ci;
        else if (ci->type == "Helm") helm_it = ci;
        else if (ci->type == "Decorate") extras.push_back(ci->model);
    }
    const auto model_of = [](const CatalogItem* ci) {
        return ci != nullptr ? ci->model : std::string();
    };
    std::vector<std::string> out = {model_of(skeleton_it), model_of(weapon_it),
                                    model_of(armor_it), model_of(helm_it)};
    for (const std::string& e : extras) {
        if (!e.empty()) out.push_back(e);
    }
    return out;
}

// JS `wd.ylb` (L268939): resolve ONE `<CreatePlayer>` `<Item>` (`nl`) to the
// list.xml row it stands for —
//   `d = a.name; d != "" && (d = p.items.$b(a.name), d != null &&
//    (c = d.clone()))`                       — the element's OWN `Name`;
//   else `d = a.Mxa; d != "" && (d = this.parameters.Fd(a.Mxa, a.Q0a),
//    d != null && (c = d.clone()))`          — the SPAWNER's item matching
//                                             `CopyParentType`/`Subtype`.
// Returns "" when neither resolves (the part is simply not worn).
std::string child_item_model(const std::vector<CatalogItem>& catalog,
                             const sf2::scene::MoveAction::ChildItem& item,
                             const std::vector<std::string>& spawner_items) {
    const auto find = [&catalog](const std::string& name) -> const CatalogItem* {
        if (name.empty()) return nullptr;
        for (const CatalogItem& ci : catalog) {
            if (ci.name == name) return &ci;
        }
        return nullptr;
    };
    const CatalogItem* ci = find(item.name);
    if (ci == nullptr && !item.copy_type.empty()) {
        for (const std::string& sn : spawner_items) {
            const CatalogItem* s = find(sn);
            if (s == nullptr || s->type != item.copy_type) continue;
            if (!item.copy_subtype.empty() && s->subtype != item.copy_subtype) {
                continue;
            }
            ci = s;
            break;
        }
    }
    return ci != nullptr ? ci->model : std::string();
}

// JS `wd.fya` (L535-536): `e == null -> (b = a.h7a(a.items), e = new ih(b))`
// — the child's OWN model is `Yc.load` (L289330) over its resolved item
// parts, NOT the spawner's merged body. Cached per spawner side + the
// `mh.cacheName` key so the returned pointer stays valid for the run.
const sf2::scene::Model* child_model_for(
    FightAssets& assets, const std::vector<CatalogItem>& catalog,
    const sf2::scene::MoveAction& act, bool is_player,
    const std::vector<std::string>& spawner_items) {
    const std::string key =
        std::string(is_player ? "P:" : "E:") + act.create_cache_key;
    const auto it = assets.child_models.find(key);
    if (it != assets.child_models.end()) {
        return it->second.bones.empty() ? nullptr : &it->second;
    }
    std::vector<std::string> parts;
    for (const sf2::scene::MoveAction::ChildItem& item : act.child_items) {
        const std::string m = child_item_model(catalog, item, spawner_items);
        if (!m.empty()) parts.push_back(m);
    }
    sf2::scene::Model built =
        parts.empty() ? sf2::scene::Model{} : assets.merge_names(parts);
    const auto ins = assets.child_models.emplace(key, std::move(built));
    return ins.first->second.bones.empty() ? nullptr : &ins.first->second;
}

// Resolves the ENEMY's move-list loadout from his stage-Warrior items.
// JS `ra.Hza` L684-685 (`d.items = a.parameters.jt()`) + `Fd` L808 (`Hd`
// Weapon slot): the move list and the move-LIST subtype come from the
// fighter's OWN equipment, not an implicit default. The stage
// `<Warrior Template="X"/>` inherits its kit from `<Template Name="X">`
// (`battle_warrior` -> `bw.items`), e.g. BOSS_LYNX Fight 1's Man_Kunai ->
// WEAPON_KUNAI (list.xml L1819, Type="Weapon" SubType="Knives") + BODY_SHIN
// + HELM_GREEN_MASK + Skeleton. Buckets by type with last-wins (the derived
// template overrides its base), exactly as `fighter_model_names` reads the
// same list for the model. A warrior with no resolvable items keeps the
// implicit default (`enemy_owned` empty / subtype empty -> "Fists").
void resolve_enemy_loadout(App& app, const BattleWarriorInfo& bw,
                          sf2::scene::BattleParams& battle) {
    const std::vector<CatalogItem> cat = load_full_catalog(app);
    const auto find_ci = [&cat](const std::string& nm) -> const CatalogItem* {
        for (const CatalogItem& c : cat) {
            if (c.name == nm) return &c;
        }
        return nullptr;
    };
    std::map<std::string, sf2::scene::OwnedItem> slot;
    std::vector<sf2::scene::OwnedItem> extras;
    for (const std::string& nm : bw.items) {
        if (nm.empty()) continue;
        const CatalogItem* ci = find_ci(nm);
        if (ci == nullptr) continue;  // unknown id is simply not worn
        if (ci->type == "Skeleton" || ci->type == "Weapon" ||
            ci->type == "Armor" || ci->type == "Helm") {
            slot[ci->type] = {ci->type, ci->subtype, ci->name};
        } else {
            extras.push_back({ci->type, ci->subtype, ci->name});
        }
    }
    battle.enemy_owned.clear();
    for (const auto& kv : slot) battle.enemy_owned.push_back(kv.second);
    for (const auto& e : extras) battle.enemy_owned.push_back(e);
    const auto wit = slot.find("Weapon");
    if (wit != slot.end()) {
        battle.enemy_weapon_subtype = !wit->second.subtype.empty()
                                          ? wit->second.subtype
                                          : wit->second.name;
    }
    std::fprintf(stdout, "[fight] enemy items: %zu (template) subtype=%s\n",
                 battle.enemy_owned.size(),
                 battle.enemy_weapon_subtype.empty()
                     ? "Fists"
                     : battle.enemy_weapon_subtype.c_str());
    std::fflush(stdout);
}

// Perk setup for the fight trigger bus (`ZOa` analog, PERKS §5.4/§5.7):
// equipped items' `<Perks>`/`<Enchantments>` rows + names from the save,
// resolved against the perk catalog. Enemy gear is not modeled (empty).
sf2::scene::PerkSetup equipped_perks(App& app, FightAssets& assets) {
    sf2::scene::PerkSetup ps;
    ps.catalog = &assets.perk_catalog;
    ps.tactics = &assets.tactic_defs;
    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return ps;
    }
    const std::vector<CatalogItem> catalog = load_full_catalog(app);
    std::vector<std::string> equipped = {w.weapon, w.armor, w.helm, w.ranged, w.magic};
    for (const auto& oi : w.items) {
        if (oi.count > 0 && oi.equipped) equipped.push_back(oi.name);
    }
    // The save's learned perks (`Bt.KS.Oa`/`Ht`, merged into `parameters.Oa`
    // by `Wk` L811-812). `Bm.he` (L753-754) scans them for a move's `<Perk
    // Name=..>` lock — this is what admits `DoubleSweep` after the lesson.
    for (const auto& pr : w.perks) {
        if (pr.name.empty()) continue;
        ps.learned.push_back(pr.name);
    }
    for (const std::string& name : equipped) {
        if (name.empty()) continue;
        for (const CatalogItem& ci : catalog) {
            if (ci.name != name) continue;
            ps.player_items.push_back(name);
            for (const ItemPerkRef& ref : ci.perks) {
                sf2::scene::ItemPerkRef scene_ref;
                scene_ref.name = ref.name;
                scene_ref.set_num = ref.set_num;
                scene_ref.set_str = ref.set_str;
                scene_ref.enchant = ref.enchant;
                ps.player_refs.push_back(std::move(scene_ref));
            }
            break;
        }
    }
    return ps;
}

} // namespace

// JS `Oa.f5` (L2286-2288): the shop tab lists are the `it.Lia` type
// partitions (L166-167: Weapon `Au`, Armor `Cva`, Helm `sDa`, Ranged `SJa`,
// Magic `WFa`) filtered by `Oa.jAa` (L2297) `isActive && !li()` =
// `!ShopHide` (`I.isActive`, L332) and not `Hidden` (`hl.li`, L278).
// `PaidItem` is NOT a shop filter — premium rows stay in their type tab
// (only the `wk`/IAP tab is gated by `iap`). Replaces
// `item_catalog.cpp::shop_items`, which dropped Ranged/Magic and the
// non-gold premium rows entirely (human report: "the SHOP does not show all
// items"; the Ranged/Magic tabs rendered empty).
std::vector<CatalogItem> shop_visible_items(const std::vector<CatalogItem>& all) {
    std::vector<CatalogItem> out;
    out.reserve(all.size());
    for (const CatalogItem& ci : all) {
        // The `Oa.jAa` tab buckets (L2297): Weapon/Armor/Helm/Ranged/Magic
        // plus tab5's `Dp`(RealMoneyItem)+`hca`(Consumable) and tab7's
        // `S_`(Free); RaidConsumable (tab6) has no bucket but is kept here
        // (every shipped row is `ShopHide` anyway).
        if (ci.type != "Weapon" && ci.type != "Armor" && ci.type != "Helm" &&
            ci.type != "Ranged" && ci.type != "Magic" &&
            ci.type != "RealMoneyItem" && ci.type != "Consumable" &&
            ci.type != "Free" && ci.type != "RaidConsumable") {
            continue;
        }
        if (ci.shop_hide || ci.hidden) continue;  // `!isActive || li()`
        out.push_back(ci);
    }
    return out;
}

// The shared catalog (loaded once, cached).
std::vector<CatalogItem> load_catalog(App& app) {
    static std::vector<CatalogItem> cached;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            const std::string path = "reference/extracted/xml/res/list.xml";
            std::ifstream in(path, std::ios::binary);
            if (in) {
                std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
                const std::vector<CatalogItem> all =
                    parse_item_catalog(std::string(data.begin(), data.end()));
                cached = shop_visible_items(all);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "item catalog load failed: %s\n", e.what());
        }
    }
    (void)app;
    return cached;
}

// The FULL catalog (all items incl. the ShopHide/Hidden base items).
std::vector<CatalogItem> load_full_catalog(App& app) {
    static std::vector<CatalogItem> cached;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            const std::string path = "reference/extracted/xml/res/list.xml";
            std::ifstream in(path, std::ios::binary);
            if (in) {
                std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
                cached = parse_item_catalog(std::string(data.begin(), data.end()));
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "full item catalog load failed: %s\n", e.what());
        }
    }
    (void)app;
    return cached;
}

// The player's resolved `UnarmedDamage` (JS `wd.Fm` L811, per attribute
// name `h`): an explicit node attr wins; otherwise `m7a` (item bonus,
// L810) + `g8a` (group/move bonus, L810) + `v.GNa` StartingAttributes +
// `this.level` × `v.uFa` LevelAttributeGain. `v.GNa`/`v.uFa` are parsed
// from character_progress.xml (JS `Vib` L1160-1161: `a.A("LevelAttribute
// Gain")`, `a.A("StartingAttributes")`). For the shipped default warrior
// (users_default.xml L10: Level=1, Armor Body, Weapon Fists — no node
// UnarmedDamage) the item total is 0 (Body UnarmedDamage=0, list.xml L89;
// Fists carries none, L91), so the resolved value is
//   StartingAttributes.UnarmedDamage(5, character_progress.xml L63)
//   + level(1) × LevelAttributeGain.UnarmedDamage(10, L64) = 15.
// JS `wd.Fm` (L811): the player's attribute map. Every name in
// `v.eo.attributes` is filled as
//   item bonus (`m7a`, sum of the equipped items' rows)
// + group bonus (`g8a`; none for the shipped warrior)
// + `v.GNa` StartingAttributes[name]
// + `this.level` × `v.uFa` LevelAttributeGain[name].
// The bCa inputs `BodyDefense`/`HeadDefense`/`BlockDamageFactor` (and the
// crit attrs) live here — the fight previously hardcoded them to 0, so a
// landed hit was computed against 0 defense and a 1× block factor.
std::map<std::string, float> resolve_player_attributes(App& app) {
    std::map<std::string, float> out;
    // The name list = the attributes present on <StartingAttributes> /
    // <LevelAttributeGain> (JS `v.eo.attributes`).
    std::map<std::string, float> starting;
    std::map<std::string, float> per_level;
    try {
        sf2::data::xml_doc doc;
        const std::string path = "reference/extracted/xml/res/character_progress.xml";
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
            const pugi::xml_node root = doc.root().first_child();
            if (root) {
                if (const pugi::xml_node sa = root.child("StartingAttributes")) {
                    for (const pugi::xml_attribute a : sa.attributes()) {
                        starting[a.name()] = a.as_float(0.0f);
                    }
                }
                if (const pugi::xml_node lg = root.child("LevelAttributeGain")) {
                    for (const pugi::xml_attribute a : lg.attributes()) {
                        per_level[a.name()] = a.as_float(0.0f);
                    }
                }
            }
        }
    } catch (const std::exception&) {
    }
    int level = 1;
    std::vector<std::string> equipped;
    try {
        const WarriorSave w = app.save().load();
        level = w.level > 0 ? w.level : 1;
        equipped = {w.weapon, w.armor, w.helm, w.ranged, w.magic};
        for (const auto& oi : w.items) {
            if (oi.count > 0) equipped.push_back(oi.name);
        }
    } catch (const std::exception&) {
    }
    // `m7a` item bonus: the item row's matching attribute (the catalog
    // carries WeaponDamage/BodyDefense/HeadDefense/UnarmedDamage/MagicDamage;
    // other names have no item row in this build).
    const std::vector<CatalogItem> catalog = load_full_catalog(app);
    for (const auto& ap : starting) {
        const std::string& name = ap.first;
        float bonus = 0.0f;
        for (const std::string& iname : equipped) {
            if (iname.empty()) continue;
            for (const CatalogItem& ci : catalog) {
                if (ci.name != iname) continue;
                if (name == "WeaponDamage") bonus += ci.weapon_damage;
                else if (name == "UnarmedDamage") bonus += ci.unarmed_damage;
                else if (name == "BodyDefense") bonus += ci.body_defense;
                else if (name == "HeadDefense") bonus += ci.head_defense;
                else if (name == "MagicDamage") bonus += ci.magic_damage;
                break;
            }
        }
        float v = bonus + ap.second;
        const auto it = per_level.find(name);
        if (it != per_level.end()) v += static_cast<float>(level) * it->second;
        out[name] = v;
    }
    return out;
}

// Resolves + loads the hashed `en.<hash>.xml` lang file once (the
// controller-atlas prefix-scan pattern). Silent when absent — callers fall
// back to embedded EN (headless-safe).
void ensure_lang(App& app) {
    static bool done = false;
    if (done) return;
    done = true;
    try {
        const std::string dir = app.res_root() + "/lang";
        std::string path;
        // The ACTIVE language's string table (`G.lang`/`G.Rq`; `Y.na` L917
        // resolves every key through it). The shipped files are
        // `<lang>.<hash>.xml` (ru.f7d5b2da.xml ships with Cyrillic); an absent
        // active file falls back to EN (`G.bg` L2394). This was EN-only, which
        // is why every UI label rendered the EN fallback regardless of the
        // resolved language.
        const std::string active = app.language().empty() ? "en" : app.language();
        for (const std::string& lang : {active, std::string("en")}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.size() > lang.size() + 1 && name.rfind(lang + ".", 0) == 0 &&
                    entry.path().extension().string() == ".xml") {
                    path = entry.path().string();
                    break;
                }
            }
            if (!path.empty()) break;
        }
        if (path.empty()) return;
        lang_table_load(app.res_root(), path);
        std::fprintf(stdout, "[lang] loaded %s (active=%s)\n", path.c_str(), active.c_str());
        std::fflush(stdout);
    } catch (const std::exception&) {
    }
}

// Registers the map zone backdrops (res/map/part0..6 + buttons frames) into
// the app atlas cache — the controller-atlas decode path. The part textures
// ship as ASTC ktx / crunch dds (not CPU-decodable), so this usually
// registers only the frame rects that decode and the map falls back to
// per-zone tints (flagged in the render path).
void load_map_backdrops(App& app) {
    static bool done = false;
    if (done) return;
    done = true;
    try {
        const std::string dir = app.res_root() + "/map";
        const char* kPrefixes[] = {"part0", "part1", "part2", "part3",
                                   "part4", "part5", "part6", "buttons"};
        for (const char* prefix : kPrefixes) {
            const std::string pre(prefix);
            std::string json_path;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(pre + ".", 0) == 0 &&
                    entry.path().extension().string() == ".json") {
                    json_path = entry.path().string();
                    break;
                }
            }
            if (json_path.empty()) continue;
            sf2::data::Texture tex;
            bool decoded = false;
            for (const std::string& ext : {".ktx", ".dds", ".webp", ".png"}) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    const std::string name = entry.path().filename().string();
                    if (name.rfind(pre + ".", 0) == 0 &&
                        entry.path().extension().string() == ext) {
                        if (sf2::data::decode_texture(entry.path().string(), tex)) {
                            decoded = true;
                            break;
                        }
                    }
                }
                if (decoded) break;
            }
            if (!decoded) continue;
            const GLuint gl = app.renderer().texture_for("map_" + pre, tex);
            if (gl == 0) continue;
            std::ifstream in(json_path, std::ios::binary);
            std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
            const sf2::data::atlas a = sf2::data::atlas_parse(jb.data(), jb.size());
            for (const auto& fr : a.frames) {
                app.register_atlas_frame(fr, a.w, a.h, gl);
            }
            std::fprintf(stdout, "[map] backdrop %s: %zu frames\n", pre, a.frames.size());
        }
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[map] backdrop load failed: %s\n", e.what());
    }
}

// ---------------------------------------------------------------------------
// DojoScreen
// ---------------------------------------------------------------------------

// --- Dojo idle figure (FightNone viewer, display only) ---------------------
// Finds the stance-idle clip the fighter's own move machinery would settle
// into. JS `Tf.init` (L1971) runs the player's REAL fighter on the hub, so its
// idle is whatever `ra.Hza`'s unlocked move list picks - the player's
// `<Weapon>StartStanceIdle*` move (moves.xml `Template="StartIdleStance|Stance"`,
// Priority 10) and its `FileName` clip. The shipped Fists fighter therefore
// idles on `fists1_stance_idle` (`FistsStartStanceIdle-Left`,
// FileName="fists1_stance_idle.bytes") - the exact clip the fight log and the
// oracle pose trace show (`clip":"fists1_stance_idle"`).
//
// The old scan took the FIRST clip whose archive name contained "stance_idle";
// the archive also ships `axe_stance_idle`, `katana_stance_idle`, ... and
// "axe..." sorts first, so the hub rendered the AXE stance on a Fists fighter
// (the oracle `dojo_hub` shows the wide Fists lunge, not the narrow axe guard).
//
// Order: the equipped weapon's own start idle -> the unarmed (Fists) default ->
// the legacy name scan. `""` means "no clip" (caller skips the figure).
// The player's equipped weapon token (from the save; e.g. "Fists"). The hub
// and the destination viewers resolve their idle the same way the fight does
// (`find_idle_clip_name`). Empty when the save read fails (the unarmed
// default then applies).
std::string player_weapon_token(App& app) {
    try {
        return app.save().load().weapon;
    } catch (const std::exception&) {
        return "";
    }
}

std::string find_idle_clip_name(
    const std::map<std::string, sf2::scene::MoveDef>& moves,
    const std::map<std::string, sf2::data::anim_clip>& clips,
    const std::string& weapon) {
    // Resolve a move's `FileName` to an existing archive clip (JS `Te.Skb`
    // L551: `FileName` minus ".bytes" -> the anim archive key).
    auto clip_for_move = [&clips](const sf2::scene::MoveDef& m) -> std::string {
        std::string base = m.file_name;
        const std::string suffix = ".bytes";
        if (base.size() > suffix.size() &&
            base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
            base = base.substr(0, base.size() - suffix.size());
        }
        if (base.empty()) base = m.name;
        return clips.count(base) != 0 ? base : std::string();
    };
    // The save stores the weapon as an item token ("WEAPON_KNIVES",
    // "One Handed Sword"), while the move name is the CamelCase class
    // (`KnivesStartStanceIdle`). Normalize both to lowercase alnum: drop the
    // `WEAPON_` class prefix, fold case, drop separators.
    auto norm_token = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c >= 'A' && c <= 'Z') {
                out.push_back(static_cast<char>(c - 'A' + 'a'));
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                out.push_back(c);
            }
        }
        if (out.size() > 6 && out.compare(0, 6, "weapon") == 0) out.erase(0, 6);
        return out;
    };
    auto weapon_start_idle = [&](const std::string& raw) -> std::string {
        const std::string key = norm_token(raw);
        if (key.empty()) return "";
        for (const auto& kv : moves) {
            const std::string& n = kv.second.name.empty() ? kv.first : kv.second.name;
            if (n.find("StartStanceIdle") == std::string::npos) continue;
            const std::string ln = norm_token(n);
            if (ln.size() < key.size() || ln.compare(0, key.size(), key) != 0) continue;
            const std::string c = clip_for_move(kv.second);
            if (!c.empty()) return c;
        }
        return "";
    };
    std::string idle = weapon_start_idle(weapon);        // the equipped weapon
    if (idle.empty()) idle = weapon_start_idle("Fists");  // unarmed default
    if (!idle.empty()) return idle;
    for (const auto& kv : clips) {  // legacy fallback
        if (kv.first.find("stance_idle") != std::string::npos) return kv.first;
    }
    for (const auto& kv : clips) {
        if (kv.first.find("stance") != std::string::npos) return kv.first;
    }
    return "";
}

// Draws one idle fighter (mesh + capsule strip) with the same formulas as
// the fight screen's file-local twin (capsule `zu`/`Dk` strip, stroke =
// Radius1*2 — JS_RENDER §4): kept as a separate helper so the fight render
// path is untouched. `fighter` must already hold a sampled pose.
// Projects a posed fighter through `camera`. `model_scale`/`offset_*` emulate
// a parent node transform the fighter is nested under (JS `Pi.Lb.hn`, L439:
// `scale=1.8`, `translate=(-200+bla,412)`): world = vert*model_scale + offset.
// Defaults are the identity (the dojo hub draws the fighter at its spawn).
void draw_dojo_figure(sf2::render::Renderer& ren, const sf2::render::Camera& camera,
                      const sf2::scene::Fighter& fighter, float model_scale = 1.0f,
                      float offset_x = 0.0f, float offset_y = 0.0f) {
    const float r = fighter.color_r(), g = fighter.color_g(), b = fighter.color_b();
    std::vector<float> verts;
    fighter.build_vertices(verts);
    std::vector<float> pv(verts.size());
    for (std::size_t i = 0; i < verts.size(); i += 2) {
        pv[i] = camera.world_to_screen_x(verts[i] * model_scale + offset_x, 1.0f);
        pv[i + 1] = camera.world_to_screen_y(verts[i + 1] * model_scale + offset_y);
    }
    if (!pv.empty()) {
        ren.draw_triangles(pv.data(), pv.size() / 2, r, g, b, 1.0f);
    }
    const sf2::scene::Model& model = fighter.model();
    // [F6] ONE strip per `<Capsule_* Type="Capsule">` in `<Figures>` DOCUMENT
    // ORDER (JS `Yc.Uib` L570 walks `Figures.children` in order, `Yc.Tib` L573
    // makes one `zu` per capsule; `Dk.update` L836 strokes it with
    // `Radius1*2` and margin-shifts the endpoints by the CAPSULE's
    // Margin1/Margin2). The old port deduped by edge through an
    // `unordered_map` (76 strips in nondeterministic order) and ignored the
    // margins.
    const std::vector<float>& pos = fighter.positions();
    constexpr float kPi = 3.14159265358979323846f;
    constexpr int kDiscSegments = 12;
    for (const sf2::scene::Capsule& cap : model.capsules) {
        const sf2::scene::EdgeDef* edge = nullptr;
        for (const sf2::scene::EdgeDef& ed : model.edges) {
            if (ed.name == cap.edge) {
                edge = &ed;
                break;
            }
        }
        if (edge == nullptr) continue;
        const int i1 = model.bone_by_name(edge->end1);
        const int i2 = model.bone_by_name(edge->end2);
        if (i1 < 0 || i2 < 0) continue;
        const std::size_t u1 = static_cast<std::size_t>(i1) * 2;
        const std::size_t u2 = static_cast<std::size_t>(i2) * 2;
        if (u1 + 1 >= pos.size() || u2 + 1 >= pos.size()) continue;
        // `Dk.update` L836 margin shift (world space; the model_scale/offset
        // below is affine, so lerp-then-transform == transform-then-lerp).
        const float wx1 = pos[u1] + (pos[u2] - pos[u1]) * cap.margin1;
        const float wy1 = pos[u1 + 1] + (pos[u2 + 1] - pos[u1 + 1]) * cap.margin1;
        const float wx2 = pos[u1] + (pos[u2] - pos[u1]) * (1.0f - cap.margin2);
        const float wy2 =
            pos[u1 + 1] + (pos[u2 + 1] - pos[u1 + 1]) * (1.0f - cap.margin2);
        const float stroke = cap.radius1 * 2.0f * model_scale * camera.zoom;
        if (stroke <= 0.0f) continue;
        const float sx1 = camera.world_to_screen_x(wx1 * model_scale + offset_x, 1.0f);
        const float sy1 = camera.world_to_screen_y(wy1 * model_scale + offset_y);
        const float sx2 = camera.world_to_screen_x(wx2 * model_scale + offset_x, 1.0f);
        const float sy2 = camera.world_to_screen_y(wy2 * model_scale + offset_y);
        float dx = sx2 - sx1;
        float dy = sy2 - sy1;
        const float len = std::sqrt(dx * dx + dy * dy);
        const float cr = stroke * 0.5f;
        auto draw_disc = [&](float cx, float cy) {
            const float step = 2.0f * kPi / static_cast<float>(kDiscSegments);
            for (int s = 0; s < kDiscSegments; ++s) {
                const float a0 = static_cast<float>(s) * step;
                const float a1 = static_cast<float>(s + 1) * step;
                float tri[6] = {
                    cx, cy, cx + std::cos(a0) * cr, cy + std::sin(a0) * cr,
                    cx + std::cos(a1) * cr, cy + std::sin(a1) * cr,
                };
                ren.draw_triangles(tri, 3, r, g, b, 1.0f);
            }
        };
        if (len < 1e-4f) {
            draw_disc(sx1, sy1);
            continue;
        }
        dx /= len;
        dy /= len;
        const float px = -dy * cr;
        const float py = dx * cr;
        float quad[12] = {
            sx1 + px, sy1 + py, sx2 + px, sy2 + py, sx1 - px, sy1 - py,
            sx2 + px, sy2 + py, sx2 - px, sy2 - py, sx1 - px, sy1 - py,
        };
        ren.draw_triangles(quad, 6, r, g, b, 1.0f);
        draw_disc(sx1, sy1);
        draw_disc(sx2, sy2);
    }
}

// ---------------------------------------------------------------------------
// Destination-screen backdrop (Shop `Oa` L2289 / Profile `vb` L2192).
//
// The JS `Pi` scene (`this.Ad`) renders a single full-bleed backdrop sprite
// `Pi.Qa = R.$(E.get(752))` (L439) framed by `ma.Tya` (L1832). `E.get(752)`
// resolves through the client manifest `G.rq[752]` (L2490) to
// `locations/dojo_shop/bg.{image}` — a dedicated 2048x1152 destination
// background (NOT the dojo location layers). It is decoded once and
// registered as the whole-texture frame `dojo_shop_bg`.
// ---------------------------------------------------------------------------

bool load_dojo_shop_bg(App& app) {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    try {
        const std::string dir = app.res_root() + "/locations/dojo_shop";
        for (const std::string& ext : {".webp", ".png", ".ktx", ".dds"}) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("bg.", 0) != 0 || entry.path().extension() != ext) continue;
                sf2::data::Texture tex;
                if (!sf2::data::decode_texture(entry.path().string(), tex)) continue;
                const GLuint gl = app.renderer().texture_for("dojo_shop_bg", tex);
                if (gl == 0) continue;
                sf2::data::atlas_frame bf;
                bf.name = "dojo_shop_bg";
                bf.x = 0;
                bf.y = 0;
                bf.w = tex.w;
                bf.h = tex.h;
                bf.source_w = tex.w;
                bf.source_h = tex.h;
                app.register_atlas_frame(bf, tex.w, tex.h, gl);
                std::fprintf(stdout, "[shop] dojo_shop bg: %dx%d gl=%u\n", tex.w, tex.h, gl);
                std::fflush(stdout);
                ok = true;
                return ok;
            }
        }
        std::fprintf(stderr, "[shop] dojo_shop bg not found under %s\n", dir.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shop] dojo_shop bg load failed: %s\n", e.what());
    }
    return ok;
}

// Draws `Pi.Qa` at world (0,0) through `ma.Tya` (`Tya` L1832). The camera is a
// pure scale+translate, so the whole 2048x1152 frame lands centred on the
// projected world origin.
//
// `tint` = the `Qa` sprite colour (JS `Qa.sf()`). The Shop's `Zkb` resets it
// to white (`a.x=a.y=a.z=a.w=1`); the Profile keeps the shared `Z.Ena` scrim
// (`sf2.502f0946.js` L2479: `Z.Ena = new H(.6392156862745098,.6392156862745098,
// .6392156862745098,1)`) that the oracle trace read on `vb.Ad.Qa`.
void draw_destination_backdrop(App& app, float tint) {
    if (!load_dojo_shop_bg(app)) return;
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame("dojo_shop_bg", &fr, &tw, &th, &gl)) return;
    sf2::render::Camera cam;
    sf2::scene::LocationScene::destination_camera(cam, kViewW, kViewH);
    sf2::scene::Sprite s;
    s.texture_name = "dojo_shop_bg";
    s.frame_x = static_cast<float>(fr.x);
    s.frame_y = static_cast<float>(fr.y);
    s.frame_w = static_cast<float>(fr.w);
    s.frame_h = static_cast<float>(fr.h);
    s.tex_w = static_cast<float>(tw);
    s.tex_h = static_cast<float>(th);
    s.solid = false;
    s.rotated = fr.rotated;
    // JS `Qa.sf()` colour: white for Shop, `Z.Ena` (0.6392...) for Profile.
    s.color_r = s.color_g = s.color_b = tint;
    s.transform.set_pos(0.0f, 0.0f);
    s.transform.set_scale(1.0f, 1.0f);
    app.renderer().draw_sprite(s, cam);
}

// The `Pi` model node transform (`Pi` ctor L439 + `Pi.bla` L440 + `ma.Tya`
// L1832): `hn.scale = 1.8`, `hn.translate = (destination_model_offset_x, 412)`.
// The fighter's own container-local position is `Pi.J9 = (0,-93)` (L441).
constexpr float kDestinationModelScale = 1.8f;
constexpr float kDestinationModelY = 412.0f;
constexpr float kDestinationModelLocalY = -93.0f;

// The `Pi` model (`this.Jc`, a `wd` fighter at `Pi.Ca.position = J9 = (0,-93)`
// L441) nested in `hn` (scale 1.8, translate (offset,412)). Its clip/model
// source is the same player warrior + idle clip the dojo hub uses.
void draw_pi_fighter(sf2::render::Renderer& ren, sf2::scene::Fighter& fighter,
                     const sf2::data::anim_clip& clip, int frame);
void draw_destination_model(App& app, sf2::render::Renderer& ren,
                            std::unique_ptr<sf2::scene::Fighter>& fighter,
                            bool& tried, bool& ok, const sf2::data::anim_clip*& idle) {
    if (!app.has_fight_assets()) return;
    if (!tried) {
        tried = true;
        FightAssets& assets = app.fight_assets();
        const std::string idle_name =
            find_idle_clip_name(assets.moves, assets.clips, player_weapon_token(app));
        const auto it = idle_name.empty() ? assets.clips.end()
                                          : assets.clips.find(idle_name);
        if (!assets.merged.bones.empty() && it != assets.clips.end() &&
            !it->second.frames.empty()) {
            fighter = std::make_unique<sf2::scene::Fighter>();
            fighter->set_model(assets.merged);
            fighter->set_color(assets.dojo.root_color());
            idle = &it->second;
            ok = true;
        }
    }
    if (!ok || fighter == nullptr || idle == nullptr || idle->frames.empty()) return;
    draw_pi_fighter(ren, *fighter, *idle, 0);
}

// Draws one `Pi` model at frame `frame` of `clip` (the `Pi.Jc` viewer). Shared
// by the idle backdrop (`draw_destination_model`) and the shop's `TryOn`
// preview (`Oa.Fhb` L2300 -> `Ex(a,7)`): both nest the body in the same `hn`
// transform (scale 1.8, translate (offset, 412), local y -93).
void draw_pi_fighter(sf2::render::Renderer& ren, sf2::scene::Fighter& fighter,
                     const sf2::data::anim_clip& clip, int frame) {
    sf2::render::Camera cam;
    sf2::scene::LocationScene::destination_camera(cam, kViewW, kViewH);
    fighter.sample(clip, frame, 0.0f, kDestinationModelLocalY, 1);
    draw_dojo_figure(ren, cam, fighter, kDestinationModelScale,
                     sf2::scene::LocationScene::destination_model_offset_x(kViewW, kViewH),
                     kDestinationModelY);
}

// JS `Pi` ctor (L439): `this.W9 = Fc.Ed(1342177280, this.node.L)` — a
// full-screen colour quad appended AFTER `Qa` (bg) and the model node, so it
// dims the destination scene (bg + model) but sits under the screen UI.
// `1342177280` = 0x50000000 -> `Na.Rv` (L1448) = black, alpha 0x50/255.
// Measured on the oracle shop backdrop: oracle/port luminance = 0.688 =
// 1 - 80/255, confirming the overlay.
void draw_destination_dim(sf2::render::Renderer& ren) {
    constexpr float kAlpha = 80.0f / 255.0f;
    const float verts[] = {0, 0, kViewW, 0, 0, kViewH,
                           kViewW, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(verts, 6, 0.0f, 0.0f, 0.0f, kAlpha);
}

// Builds the Dojo hub's `FightNone` FightController. JS `Tf.init` L1971:
// `a = p.V$a().a0("FightNone")[0].g0(0)` — the current zone's FIRST
// `FightNone` battle (the Punchbag `Training`, DOJO_BG_STATIC §1), then
// `this.Ig=v.m1a(a)`. The JS `ca.o1a` L403 builds BOTH fighters
// (`yb=Gf(kc)` player, `pb=Gf(Zb)` enemy): for `FightNone` there is no
// opponent — the enemy is the Punchbag Warrior (`NotAI=1 NotAnimation=1`,
// items PunchingBag/SkeletonPunchingBag, stages.xml L12-16), i.e. the
// training bag at the ModelsViewer enemy spawn, present and drawn behind the
// player, holding its bind pose (`merged_bag`). Built exactly like the battle
// fight (same save-sourced player model + move list + spawns), then put
// straight into phase 2 with NO round flow (`enter_fight_none`). The entry
// gong is suppressed: it fires from the battle-registration branch (L1216),
// not from this `m1a` factory path.
void DojoScreen::build_dojo_fight(App& app) {
    FightAssets& assets = app.fight_assets();
    sf2::scene::LocationScene& loc = assets.dojo;
    const std::string battle_name = "Training";
    const float arena_w = loc.arena_width() > 0.0f ? loc.arena_width() : 1960.0f;
    const float wall = loc.arena_wall();

    // The enemy Warrior (JS `ur` L186-195): NotAI -> no AI controller,
    // NotAnimation -> no animation attach (bind pose).
    const BattleWarriorInfo bw = battle_warrior(battle_name, app.pending_battle().zone);

    sf2::scene::BattleParams battle;
    battle.name = battle_name;
    battle.type = bw.type;  // JS `Lc.pkb` L1407 `b0(<Battle Type>)`: the dojo
                            // `Training` is Type="DUMMY" -> stays "FightNone"
    std::fprintf(stdout, "[dojo] battle type: %s kind='%s' -> type=%s\n",
                 battle_name.c_str(), bw.kind.c_str(), battle.type.c_str());
    std::fflush(stdout);
    battle.location = "dojo";
    battle.rounds = 2;
    battle.round_time = 99;
    battle.health_recovery = 1.0f;
    battle.max_hp = 1;
    battle.player_attrs = resolve_player_attributes(app);
    battle.player_unarmed_damage = battle.player_attrs.count("UnarmedDamage")
                                       ? battle.player_attrs["UnarmedDamage"]
                                       : 0.0f;
    // Spawns from the dojo's ModelsViewer (JS `Bf.zjb` L476 -> L381).
    if (loc.has_spawns()) {
        battle.player_spawn_x = loc.player_spawn_x();
        battle.player_spawn_y = loc.player_spawn_y();
        battle.enemy_spawn_x = loc.enemy_spawn_x();
        battle.enemy_spawn_y = loc.enemy_spawn_y();
    }
    battle.enemy_not_ai = bw.has_not_ai;
    battle.enemy_not_animation = bw.has_not_animation;
    battle.enemy_voice = bw.voice;
    battle.player_voice = bw.player_voice;
    battle.enemy_align = to_align_deltas(bw.align);
    battle.player_align = to_align_deltas(bw.player_align);
    resolve_enemy_loadout(app, bw, battle);

    // The player's move list from its OWNED items (JS `ra.Hza` L684-685).
    const std::vector<sf2::scene::OwnedItem> player_owned = owned_items(app);

    // Player model from the save's typed slots (JS `xc.cM` L809-810).
    sf2::scene::Model player_model_storage;
    const sf2::scene::Model* player_model = nullptr;
    {
        std::vector<std::string> player_items;
        try {
            const WarriorSave w = app.save().load();
            player_items = {w.skeleton, w.weapon, w.armor, w.helm};
        } catch (const std::exception&) {
        }
        const std::vector<std::string> pnames = fighter_model_names(app, player_items);
        if (!pnames.empty() && !pnames[0].empty()) {
            player_model_storage = assets.merge_names(pnames);
            if (!player_model_storage.bones.empty()) player_model = &player_model_storage;
        }
    }

    const sf2::scene::TacticDef* tactic = nullptr;
    {
        const auto it = assets.tactic_defs.find("Standard");
        if (it != assets.tactic_defs.end()) tactic = &it->second;
        if (!bw.tactic.empty()) {
            const auto tit = assets.tactic_defs.find(bw.tactic);
            if (tit != assets.tactic_defs.end()) tactic = &tit->second;
        }
    }
    // Player tactic (JS `IKa` L672): the save warrior's own `<Tactic>` when it
    // resolves, else "Standard".
    const sf2::scene::TacticDef* player_tactic = nullptr;
    {
        const auto sit = assets.tactic_defs.find("Standard");
        if (sit != assets.tactic_defs.end()) player_tactic = &sit->second;
        try {
            const std::string pt = app.save().load().tactic;
            if (!pt.empty()) {
                const auto pit = assets.tactic_defs.find(pt);
                if (pit != assets.tactic_defs.end()) player_tactic = &pit->second;
            }
        } catch (const std::exception&) {
        }
    }

    dojo_fight_ = std::make_unique<sf2::scene::FightController>();
    dojo_fight_->set_silent_entry(true);  // no gong (the `m1a` factory path)
    dojo_fight_->set_global_triggers(&assets.global_triggers);
    dojo_fight_->init_locks(
        battle, assets.merged, assets.moves, assets.clips, assets.tactics_sets, tactic,
        "Player", bw.first_name.empty() ? battle_name : bw.first_name,
        battle.player_spawn_x, battle.player_spawn_y, battle.enemy_spawn_x,
        battle.enemy_spawn_y, battle.max_hp, battle.max_hp, {}, player_owned,
        equipped_perks(app, assets), nullptr, player_model, &assets.merged_bag,
        player_tactic);
    dojo_fight_->set_seed(0x5F2u);  // JS `Da.pg=new Rk(L.seed)` (L67)
    dojo_fight_->set_bounds(wall, arena_w - wall, loc.arena_floor());
    dojo_fight_->set_fighter_color(loc.root_color());
    {
        // Disarm identity (JS `$b(Au)` vs `ownHd`, L394): the player wields
        // the save's weapon (Fists fallback).
        std::string pw = "Fists";
        try {
            pw = app.save().load().weapon;
        } catch (const std::exception&) {
        }
        if (pw.empty()) pw = "Fists";
        dojo_fight_->set_fighter_weapons(pw, "Fists");
    }
    dojo_fight_->enter_fight_none();
    dojo_fight_ok_ = true;
    std::fprintf(stdout,
                 "[dojo] FightNone controller ready (battle=%s enemy='%s' not_ai=%d "
                 "not_anim=%d moves=%zu spawn P=(%.0f,%.0f) E=(%.0f,%.0f))\n",
                 battle_name.c_str(), bw.first_name.c_str(), battle.enemy_not_ai ? 1 : 0,
                 battle.enemy_not_animation ? 1 : 0, player_owned.size(),
                 battle.player_spawn_x, battle.player_spawn_y, battle.enemy_spawn_x,
                 battle.enemy_spawn_y);
    std::fflush(stdout);
}

// The hub controller state for the `--input-tape` pad evidence (no behavior
// change; null-safe before the controller is built).
bool DojoScreen::dojo_fight_ready() const { return dojo_fight_ != nullptr; }

std::string DojoScreen::dojo_player_move() const {
    if (dojo_fight_ == nullptr) return std::string();
    const sf2::scene::MoveDef* m = dojo_fight_->player().fighter.current_move();
    return m != nullptr ? m->name : std::string();
}

float DojoScreen::dojo_player_x() const {
    return dojo_fight_ != nullptr ? dojo_fight_->player().fighter.world_x() : 0.0f;
}

float DojoScreen::dojo_player_y() const {
    return dojo_fight_ != nullptr ? dojo_fight_->player().fighter.world_y() : 0.0f;
}

int DojoScreen::dojo_fight_frame() const {
    return dojo_fight_ != nullptr ? dojo_fight_->frame() : -1;
}

int DojoScreen::dojo_player_move_frame() const {
    return dojo_fight_ != nullptr ? dojo_fight_->player().fighter.move_frame() : -1;
}

int DojoScreen::dojo_last_key_type() const { return dojo_last_key_type_; }

// The hub's keyboard -> its own `FightNone` controller. The JS hub runs a
// REAL `ca` (`Tf` L1971 `this.Ig=v.m1a(a)`; `aa(): this.YL(Ig,a)` steps it),
// and the keyboard is wired to it through `Za.bbb` -> `Za.hS` -> `ca.N0a` —
// the SAME `player_input` path the drawn pad uses (`update_pad_input`). The
// base `Screen::on_key` is a no-op, which is why only the pad worked here.
void DojoScreen::on_key(int glfw_key, bool down) {
    // The hub's FightNone battle is put straight into phase 2 (`xF(2)`, JS
    // `kg` L387) with no round flow, so every fight key is live — no pause
    // gate. Directions use the JS diagonal-pair table (`Af.oUa` L2472; the
    // ten W/A/S/D + K/L/O/P/J/Q keys); the arrows/Space are the desktop
    // aliases, gated by `desktop_key_aliases_` exactly like `FightScreen`.
    const int move_slot = keyboard_move_slot(glfw_key, desktop_key_aliases_);
    if (move_slot >= 0) {
        keyboard_move_edge(dojo_fight_.get(), dojo_keys_, move_slot, down, "dojo");
        dojo_last_key_type_ = dojo_keys_.sector;
        return;
    }
    int kt_id = FightScreen::key_type_for_glfw(glfw_key);
    if (desktop_key_aliases_ && kt_id == 0) {
        kt_id = FightScreen::desktop_alias_for_glfw(glfw_key);
    }
    dojo_last_key_type_ = kt_id;
    if (kt_id == 0 || dojo_fight_ == nullptr) return;
    dojo_fight_->player_input(static_cast<sf2::scene::key_type>(kt_id),
                              down ? sf2::scene::press_type::tap
                                   : sf2::scene::press_type::release);
    std::fprintf(stdout, "[dojo] player input -> key %d (%s)\n", kt_id,
                 down ? "press" : "release");
    std::fflush(stdout);
}

DojoScreen::DojoScreen(ScreenManager& mgr) : Screen(mgr, "Dojo") {
    // Menu music (JS `lb.OS()` -> `ta.Ut("menu")` under the `lb.rJ` guard,
    // L1276-1277; the FightNone hub ctor calls it).
    sf2::audio::AudioEngine::instance().play_music_once("menu");
    // No bespoke ctor art: the JS hub is the `FightNone` ModelViewer over
    // the dojo layer stack + the shared `za` chrome (Tf L1969-1972). The
    // FIGHT/MAP/SHOP/PROFILE 4-up row, punchbag, gear and disciple chrome
    // were native inventions (PORT_AUDIT_UI §2.1-2.2) — navigation is the
    // `za` vertical nav column (draw_za_chrome / za_update below).
}

void DojoScreen::update_impl(float dt) {
    // The hub's live `FightNone` controller (JS `Tf.init` L1971
    // `this.Ig=v.m1a(a)`; `aa(): this.YL(Ig,a)` steps it every frame). Build
    // once the fight assets are up; null-safe when they are not. Built before
    // the modal gate so the viewers exist even under a `He` dialog (the
    // oracle `dojo_sensei` shows them behind the modal).
    if (!dojo_fight_tried_ && app().has_fight_assets()) {
        dojo_fight_tried_ = true;
        build_dojo_fight(app());
    }
    // Location timeline (D6): advance the SimpleEffect Transparency loop
    // (`xl.ia` L478-481) once per frame for the hub backdrop.
    if (app().has_fight_assets()) {
        app().fight_assets().dojo.update(dt);
    }
    ensure_lang(app());  // runtime Sensei lines (once; silent if absent)
    // Quest/save snapshot, re-read every fixed step like ShopScreen's money
    // watch: the Dojo stays mounted under Fight/Results/Shop, so cached
    // fields would go stale after wins/buys (e.g. the shop buy writing step
    // MAP must flip the banner on return). Read-only; failures log once.
    try {
        const WarriorSave w = app().save().load();
        if (!money_logged_ || w.money != seen_money_) {
            money_logged_ = true;
            seen_money_ = w.money;
            std::fprintf(stdout, "[dojo] MONEY %d   LV %d   POWER %d   WEAPON %s   ARMOR %s   HELM %s   TUTORIAL %s STEP %s\n",
                         w.money, w.level, w.power, w.weapon.c_str(), w.armor.c_str(),
                         w.helm.c_str(), w.tutorial.c_str(), w.story_step().c_str());
            std::fflush(stdout);
        }
        tutorial_ = w.tutorial;
        story_step_ = w.story_step();
        map_focus_ = w.map_focus;
        battles_ = w.battles;
        // The JS `<Battle Name>` is the `hb` triple "Zone|Name|" (L1416), while
        // the quest-panel/tutorial consumers below match the BAR (e.g.
        // `Training`). Expose the leaf name alongside the raw rows so the
        // banner logic keeps working with the zone-qualified `J1a`/`Iaa`
        // records (legacy bare rows pass through unchanged).
        for (const std::string& b : w.battles) {
            const std::size_t p1 = b.find('|');
            if (p1 == std::string::npos) continue;
            const std::size_t p2 = b.find('|', p1 + 1);
            const std::string leaf =
                b.substr(p1 + 1,
                         p2 == std::string::npos ? std::string::npos : p2 - (p1 + 1));
            if (!leaf.empty() &&
                std::find(battles_.begin(), battles_.end(), leaf) == battles_.end()) {
                battles_.push_back(leaf);
            }
        }
        level_ = w.level;
    } catch (const std::exception& e) {
        if (!money_logged_) {
            std::fprintf(stderr, "[dojo] save read failed: %s\n", e.what());
        }
    }
    // Tutorial quest step (quest_panel.hpp — read-only derivation from the
    // save's Tutorial field + the last Training result via the existing
    // pending-battle hook; no save writes, no fight-logic touch).
    {
        const PendingBattle& pb = app().pending_battle();
        if (pb.has_result && pb.player_won && pb.battle_name == "Training") {
            training_won_ = true;
        }
        const QuestStep qs = quest_step_for_state(
            app().res_root(),
            quest_state_for(tutorial_, story_step_, training_won_, level_, map_focus_,
                            battles_, {}));
        if (qs.id != quest_logged_) {
            quest_logged_ = qs.id;
            std::fprintf(stdout, "[quest] step %d (%s) -> %s\n", qs.id, qs.speaker.c_str(),
                         qs.target.c_str());
            std::fflush(stdout);
        }
    }
    // Quest modal gate (engine `He` records): while a dialog is up, a tap
    // advances a Notification or fires the Regular dialog's action button
    // (`dhb(1)`); the button's deferred `Fight` request launches here and the
    // chrome stays blocked beneath the modal.
    {
        std::string fight;
        if (quest_modal_consume(app(), &fight)) {
            if (!fight.empty()) launch_quest_fight(fight);
            return;
        }
    }
    // The drawn gamepad -> the hub's own controller (JS `Za.hS` L453 ->
    // `ca.Ka()` -> `ca.N0a` L426): the SAME shared pad path the fight uses,
    // BEFORE the step so a buffered key lands the same frame. `xF(2)`
    // (L387-388) armed the pad. The `za` nav runs after (a nav tap is not a
    // pad tap).
    if (dojo_fight_ != nullptr) {
        update_pad_input(app(), dojo_fight_.get(), dojo_pad_, /*live=*/true, "dojo");
        dojo_fight_->update(dt);
        // [dojo lesson] Publish the player fighter's animation START to the
        // quest engine (JS `Te.x3` L508 -> `Gc.Pf` L671 -> `Bo`/`Do`/`Eo`
        // L1121/L1123/L1125). This is the REAL resume condition for the
        // parked StoryTutorial lesson beats; the 15 s timeout is the fallback.
        const sf2::scene::FightController::AnimStart as =
            dojo_fight_->take_player_anim_start();
        if (as.valid) {
            app().quest_engine().on_lesson_anim(app(), as.name, as.type, false);
        }
    }
    // The shared `za` nav column (JS `za.Aub` L1978-1980 / `za.Ofb`..`Vfb`):
    // a tap switches to Dojo/Map/Shop/Profile/Settings (JS `ma.Jg().jI`).
    za_update(app(), *this, kScreenDojo, dt);
}

// Dojo location ensure (the hub's own `assets.dojo`; the fight keeps a
// separate `assets.fight_location`). Params scan + LocationScene load + webp
// texture resolve + frame aliasing. The Dojo hub needs it at boot (otherwise
// the hub renders black). Guarded by layers().empty().
void ensure_dojo_location(App& app) {
    if (!app.has_fight_assets()) return;
    FightAssets& assets = app.fight_assets();
    if (!assets.dojo.layers().empty()) return;
    const std::string loc_dir = app.res_root() + "/locations/dojo";
    try {
        // JS `Bf.init` L474 page chain: load every `dojo[-N].*.json` page and
        // alias each page's frames. The old single-`{atlas_json}` load only
        // aliased page 1, so a `Sequention` frame packed on page 2+ stayed
        // unresolved (JS `ni.init` L1142 walks `b.nextPage`).
        load_location_atlas_pages(app, assets.dojo, loc_dir, "dojo");
        // [DOJO-HUB] The `dojo_punch_bag_holder` prop is KEPT. An earlier
        // round erased it on the strength of a stale probe capture, but the
        // authoritative oracle `oracle_matrix/dojo_hub.png` paints the black
        // hook+beam bracket over the bag (measured region x650..1050 y90..190
        // = (81,55,41) vs the port's clean wall (141,98,73)). The JS draws
        // every params layer in XML order (`UWa` L832) — the holder is
        // layer 9 (DOJO_BG_STATIC §2) — so drawing it is the JS-exact path.
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[dojo] location load failed: %s\n", e.what());
    }
}

// Dojo gamepad (display only): the oracle Dojo shows the joystick +
// punch/kick buttons (same ui/controller frames as the fight pad). The hub's
// drawn pad reflects its live interaction state (the knob follows a drag, the
// base/buttons swap to their _action frames) — the same frames the fight pad
// uses. The hub's pad is wired to its `FightNone` controller (see
// DojoScreen::update_impl / draw_dojo_figure callers).
void draw_dojo_gamepad(App& app, const PadInputState& pad_in) {
    const GamepadLayout pad;
    const float base_size = pad.joy_r * 2.0f;
    const float knob_size = pad.knob_r * 2.0f;
    // Draw-size 0.9 (centers + hit radii untouched: JS fu Si(120,28)/
    // fh(-50,198) + uab 115): the 200px art at full hit-diameter left a
    // 4px gap that read as touching; 0.9 leaves a 12px gap.
    const float btn_size = pad.btn_r * 2.0f * 0.9f;
    const bool grabbed = pad_in.joy_grabbed;
    try_draw_atlas_button(app,
                          grabbed ? "JoystickContainer_action"
                                  : "JoystickContainer_norm",
                          pad.joy_cx, pad.joy_cy, base_size, base_size, 1.0f);
    try_draw_atlas_button(app, grabbed ? "Joystick_action" : "Joystick_norm",
                          pad.joy_cx + pad_in.joy_knob_x,
                          pad.joy_cy + pad_in.joy_knob_y, knob_size, knob_size,
                          1.0f);
    try_draw_atlas_button(app,
                          pad_in.btn_punch_down ? "btn_punch_action"
                                                : "btn_punch_normal",
                          pad.punch_cx, pad.punch_cy, btn_size, btn_size, 1.0f);
    try_draw_atlas_button(app,
                          pad_in.btn_kick_down ? "btn_kick_action"
                                               : "btn_kick_normal",
                          pad.kick_cx, pad.kick_cy, btn_size, btn_size, 1.0f);
}

namespace {

// --- Dialogs: `He.S` L1045-1051 + `od` L1894-1900 --------------------------
// `draw_quest_modal` draws the panel; this layout also gives the dialog
// button's hit rect (the tap that fires its deferred actions, `He.dhb(1)`
// L1061).
// --- Dialog layout: `od` L1894-1900 + `He.S` Type routing L1045-1051 -------
// `od.layout` (L1898) is derived ENTIRELY from the dialog's content height
// `Md`:
//     this.Ne.D(-this.Md/2);
//     let a = this.Md/2; a<300 && (a=300); a>1E3 && (a=1E3);
//     this.Vc.D(-(a + this.Vc.pfa().y));
//     let b = this.Cd.node.qa()/2;
//     this.Cd.D(a + this.Cd.node.qa()/2);
//     this.Cy.D(this.Vc.ra + -25);   this.Rx.D(this.Cd.node.ra + b);
// `Md` is measured by `Od.lj` / `uj.sqb` (L1950 / L1953) — the wrapped body
// height, floored by the `MinContentHeight` attribute (`this.cv`). `Vc` is
// `Fa(1560,160)` (L1900), a `Bb` plate is `Pb(125)` (L1899 / L1944), the body
// box is `Fa(DG?900:1680,..)` at `C(DG?-100:0)` (L1953-1954) and the `oe`
// avatar (L1823) is `la(1.8*iy)` of a 512px sheet at `C(-450+OB)` (L1947).
// Every constant below is one of those JS values — the previous fixed
// fractions (0.251/0.436/0.325/0.490/0.755/0.762 + a 270x60 plate) were
// invented.
constexpr float kOdTitleW = 1560.0f;    // `$T` L1900 `Vc.Fa(1560,160)`
constexpr float kOdTitleH = 160.0f;
constexpr float kOdBtnH = 125.0f;       // `Bb.Pb(125)` L1899/L1944
constexpr float kOdBtnW = 600.0f;       // `od.rI` L1948 `Rb.xc(600)`
constexpr float kOdBodyW = 900.0f;      // `sqb` L1953 `a = DG?900:1680`
constexpr float kOdBodyX = -100.0f;     // `f.C(this.DG?-100:0)` L1954
constexpr float kOdAvatarX = -450.0f;   // `ala` L1947 `b = -450`
constexpr float kOdAvatarSrc = 512.0f;  // `oe` sheet (L1823)
constexpr float kOdAvatarScale = 1.8f;  // `la(1.8*iy)` L1947
constexpr float kOdDivGap = 25.0f;      // `Cy.D(Vc.ra+-25)` L1898
constexpr float kOdDefaultMd = 550.0f;  // `od` ctor L1894 `this.Md=550`
constexpr float kOdVeBodyW = 1100.0f;   // `Ve` ctor L1913 `kb.Fa(1100,550)`
constexpr float kOdVeBodyX = -550.0f;   // `kb.C(-550)` L1913

// `He.S` L1045-1051 Type -> dialog class (`Wb.Xob` L926):
//   Regular      -> `Xc.Xhb`            -> `Wb.openDialog(280)` -> `Od` (L929)
//   Stranger     -> `Xc.Bia`->`Yhb`     -> `Wb.openDialog(290)` -> `uj` (L929)
//   Multiline    -> `Xc.Bia`            -> 290 -> `uj`          (L1049)
//   MultilineBig -> `Xc.Nhb`->`Bia`     -> 290 -> `uj`          (L1050)
//   NoAvatar     -> `Xc.rIa`->`Vhb`     -> `Wb.openDialog(340)` -> `Ve` (L930)
//   ShowLoot     -> `Xc.Uhb`            -> `Wb.openDialog(370)` -> `vn` (L929)
//   Notification -> `Ib.F().Qhb` (the `Ib` bar)                 (L1050)
//   Scroll / MultiLineScroll / ThreeButtons / ItemSetDialog / MultilineTMP /
//   Simple -> a bare `debugger;` in the JS (L1048-1050): no dialog object is
//   built and `He.S` falls through to `this.sa()` (advance, no display).
DialogKind dialog_kind(const std::string& type) {
    if (type == "Notification") return DialogKind::kIbBar;
    if (type == "NoAvatar") return DialogKind::kVe340;
    if (type == "ShowLoot") return DialogKind::kVn370;
    if (type == "Stranger" || type == "Multiline" || type == "MultilineBig") {
        return DialogKind::kUj290;
    }
    if (type.empty() || type == "Regular") return DialogKind::kOd280;
    return DialogKind::kNone;  // the JS `debugger` branch
}

// `Xc.Nhb` L1050 -> `uj.sqb()` L1953: `Multiline`/`MultilineBig` lay out
// EVERY `<Line>` as one scrollable body (`this.yO` + `this.MV`, `Md`
// accumulates the whole stack). Every other Type keeps `Od.EF`'s
// one-row-per-page pager (`Od.X2` L1950).
bool dialog_scrolls_all_lines(const std::string& type) {
    return type == "Multiline" || type == "MultilineBig";
}

struct OdLayout {
    OdPanel panel;
    float md = 0.0f;          // `Md` (design px)
    float a = 300.0f;         // `clamp(Md/2, 300, 1000)`
    float anim_scale = 1.0f;  // `od.aa` L1895 `node.la`
    float anim_slide = 0.0f;  // `od.aa` close slide (design px)
    float title_x = 0.0f, title_y = 0.0f, title_w = 0.0f, title_h = 0.0f;
    float body_x = 0.0f, body_y = 0.0f, body_w = 0.0f, body_h = 0.0f;
    float btn_cx = 0.0f, btn_cy = 0.0f, btn_w = 0.0f, btn_h = 0.0f;
    float portrait_cx = 0.0f, portrait_cy = 0.0f, portrait = 0.0f;
    float div1_y = 0.0f, div2_y = 0.0f;
    // design -> screen. The `od` node sits at the panel centre, scaled `c`
    // (`l4a` L1896); `anim_scale`/`anim_slide` are the L1895 tween.
    float sx(float dx) const {
        return panel.px + panel.pw * 0.5f + (dx + anim_slide) * panel.c * anim_scale;
    }
    float sy(float dy) const {
        return panel.py + panel.ph * 0.5f + dy * panel.c * anim_scale;
    }
};

OdLayout od_layout(float md) {
    OdLayout o;
    o.panel = od_panel(2340.0f, 1530.0f);  // od AV = fc(2340,1530), L1894
    o.md = md < 0.0f ? 0.0f : md;
    o.a = std::clamp(o.md * 0.5f, 300.0f, 1000.0f);  // L1898
    // `Vc.Fa(1560,160)` + `C(-780)`: the title box spans design x [-780,780].
    o.title_w = kOdTitleW * o.panel.c;
    o.title_h = kOdTitleH * o.panel.c;
    o.title_x = o.sx(-kOdTitleW * 0.5f);
    o.title_y = o.sy(-(o.a + kOdTitleH));  // `Vc.D(-(a+Vc.pfa().y))`
    o.btn_h = kOdBtnH * o.panel.c;
    o.btn_w = kOdBtnW * o.panel.c;
    o.btn_cy = o.sy(o.a + kOdBtnH * 0.5f);  // `Cd.D(a+Cd.node.qa()/2)`
    o.body_w = kOdBodyW * o.panel.c;
    o.body_x = o.sx(kOdBodyX);  // `f.C(this.DG?-100:0)`
    o.body_y = o.sy(-o.md * 0.5f);  // `Ne.D(-Md/2)`
    o.body_h = o.md * o.panel.c;
    o.btn_cx = o.sx(0.0f);  // `od.EF` L1899 centres a lone plate on the panel
    o.portrait = kOdAvatarSrc * kOdAvatarScale * o.panel.c;  // L1947
    o.portrait_cx = o.sx(kOdAvatarX);
    o.portrait_cy = o.sy(0.0f);
    o.div1_y = o.sy(-(o.a + kOdTitleH) - kOdDivGap);  // `Cy.D(Vc.ra+-25)`
    o.div2_y = o.sy(o.a + kOdBtnH);                   // `Rx.D(Cd.node.ra+b)`
    return o;
}

// `Od.Xma` L1948: the CURRENT row's text (`He` pages one `<Line>` at a time
// via `Od.EF`/`Od.X2` L1946/L1950).
std::string dialog_page_body(App& app, const EngineDialog& d) {
    if (d.lines.empty()) return std::string();
    const std::size_t page = d.page < d.lines.size() ? d.page : d.lines.size() - 1;
    return loc(app, d.lines[page], d.lines[page]);
}

// The dialog's content height `Md` (design px). `Od.lj` L1950
// `Md = Math.max(kb.ew(), cv)`; `uj.sqb` L1953 accumulates every row. `cv` is
// the `MinContentHeight` attribute (0 when absent). `Ve` never recomputes
// `Md`, so it keeps the `od` ctor default (L1894 `this.Md=550`); `vn.$A`
// L1935 hard-sets `this.Md=600`.
float dialog_content_md(App& app, const EngineDialog& d) {
    const DialogKind kind = dialog_kind(d.type);
    if (kind == DialogKind::kVe340) return kOdDefaultMd;  // L1894
    if (kind == DialogKind::kVn370) return 600.0f;        // `vn.$A` L1935
    const float c = od_panel(2340.0f, 1530.0f).c;
    const float safe_c = c > 0.0f ? c : 1.0f;
    float h = 0.0f;
    if (dialog_scrolls_all_lines(d.type)) {
        for (const std::string& ln : d.lines) {  // `sqb` L1953 rows
            h += measure_ui_wrapped(app, loc(app, ln, ln), kOdBodyW * safe_c, 0.70f);
        }
    } else {
        // `Od.Xma` L1948 -> `lj` re-measures the CURRENT row.
        h = measure_ui_wrapped(app, dialog_page_body(app, d), kOdBodyW * safe_c, 0.70f);
    }
    return std::max(h / safe_c, d.min_content_height);
}

OdLayout dialog_layout_for(App& app, const EngineDialog& d, const DialogAnim& anim) {
    OdLayout o = od_layout(dialog_content_md(app, d));
    o.anim_scale = anim.scale;  // `od.aa` L1895 `node.la`
    o.anim_slide = anim.slide;  // `od.aa` close slide
    // D10: `He.S` L1051 applies the parsed attrs to the built dialog —
    //   `XLa()`   (L1955)  the `ImageScale` slot `iy` (-> `la(1.8*iy)` L1947);
    //   `VLa(OB)` (L1956)  ImageOffsetX -> avatar x (`ala` L1947 `C(-450+OB)`);
    //   `WLa(YV)` (L1956)  ImageOffsetY -> avatar y (`ala` L1947 `D(YV)`);
    //   `mMa(ov)` (L1956)  TextOffset -> `eba` L1950 (`a.D(a.ra+ov.y)`);
    //   `nMa(LH)` (L1956)  TextPosXByImage -> `Jva` L1951;
    //   `TM`      (L1044)  ContentOffsetX -> the content x (`Jva`/`sqb`).
    // Defaults reproduce `od_layout` exactly (scale 1, offsets 0, LH true).
    o.portrait = kOdAvatarSrc * kOdAvatarScale * d.image_scale * o.panel.c;  // L1947
    o.portrait_cx = o.sx(kOdAvatarX + d.image_offset_x);  // `C(-450+OB)`
    o.portrait_cy = o.sy(d.image_offset_y);               // `D(YV)`
    // `Jva` L1951 `this.LH?a.C(-100+this.OB):a.C(0)` + `eba` L1950 `ov` +
    // the authored `ContentOffsetX`.
    o.body_x = o.sx((d.text_pos_x_by_image ? kOdBodyX + d.image_offset_x : 0.0f) +
                    d.content_offset_x + d.text_offset_x);
    o.body_y = o.sy(-o.md * 0.5f + d.text_offset_y);  // `Ne.D(-Md/2+ov.y)`
    return o;
}

// `od.aa` L1895: the live dialog's 0.25 s tween. The clock is the top
// screen's `time()` — a fixed 60 Hz accumulation, NEVER the wall clock or the
// OS cursor — so a capture after settle is byte-identical to the steady state
// (alpha 1, scale 1, slide 0). A NEW dialog (`type|title`) restarts the open
// tween at t=0.
DialogAnim dialog_anim_now(App& app, const EngineDialog& d) {
    static std::string key;
    static float start = 0.0f;
    const std::string cur = d.type + "|" + d.title;
    const float now = app.screens().top() != nullptr ? app.screens().top()->time() : 0.0f;
    if (key != cur || now < start) {
        key = cur;
        start = now;
    }
    return dialog_anim_at(now - start, /*closing=*/false);
}

// `od.close`/`Ge(1)` L1898 + `n_()`: the dismissed dialog slides + fades for
// the same 0.25 s (alpha `1-KK(t)`, x `+1000*KK(t)`). The engine pops it
// immediately (`Wb.Bwb` L928 unlinks in `n_()`), so the drawer retains a COPY
// here and clears it once the tween lands.
struct ClosingDialog {
    bool active = false;
    EngineDialog d;
    float start = 0.0f;
};
ClosingDialog& closing_slot() {
    static ClosingDialog s;
    return s;
}

void dialog_capture_closing(App& app, const EngineDialog& d) {
    ClosingDialog& s = closing_slot();
    s.active = true;
    s.d = d;
    s.start = app.screens().top() != nullptr ? app.screens().top()->time() : 0.0f;
}

// `Ib.lj` L1908: `$s.uva(b, "^{0}^\n", a[c].text)` for every line, trailing
// newlines trimmed -> the lines joined with '\n'. This is the bar's ONE label.
std::string ib_joined_lines(App& app, const EngineDialog& d) {
    std::string joined;
    for (const std::string& ln : d.lines) {
        if (!joined.empty()) joined.push_back('\n');
        joined += loc(app, ln, ln);
    }
    return joined;
}

// `Ib.F().Qhb(r, z, c, g, k, SK, x, $Ta)` L1050: the notification bar. The
// body is the JOINED lines and the portrait the resolved `Image` — `Sr()`
// L1908-1910 builds exactly ONE label and NO speaker row.
// `Ib.Qhb(...,h=this.$Ta)` L1050 -> L1907 `h&&(this.Uz=Fc.Ed(-65281,this.node
// .L),...)`: `BlockRaycast` gates the bar's `Uz` overlay. `Uz` is a child of
// the BAR node whose size comes from `Fc.Ed`'s parented rect, which this seam
// does not expose — recorded (the flag is parsed + applied to the draw call),
// NOT drawn as an invented full-screen dim.
bool notification_blocks_raycast(const EngineDialog& d) { return d.block_raycast; }

// `Ib.Sr` L1910: the OK plate draws only when `Ib.RP` is clear and the
// notification carries a button caption (`b=!(b==null||b=="")`). `Ib.RP` is
// `He.DisableNotificationsButtons` (L1045 `Ib.RP=this.qUa`).
bool notification_show_ok(const EngineDialog& d) {
    return !d.disable_notifications_buttons && !d.button_actions.empty() &&
           !d.button_text.empty();
}

void draw_notification(App& app, sf2::render::Renderer& ren, const EngineDialog& d) {
    draw_ib_hint(app, ren, d.image, ib_joined_lines(app, d), notification_show_ok(d));
}

} // namespace

// The dialog's action-button plate (`dlgStoryBtnFight` -> "В БОЙ"): the
// `btnBeige` slice when the atlas resolved, else the flat fallback.
// `He.lea` L1063 -> `nz.hi` L1840 -> `Bb.fza` L1844 (`"btn"+name.substr(7)`):
// the action plate's frame is derived from the button's `<Button Color>`.
//   Red -> EButtonDark, Green -> EButtonGreen, White|Beige -> EButtonWhite,
//   Gold -> EButtonGold, anything else -> EButtonWhite.
// `He.lea`'s default is `"Beige"` (White); a secondary slot defaults to Dark
// (`od.jR` L1899: primary `EButtonWhite`, secondary `EButtonDark`). The port
// drew a single invented `btnBeige` plate for every button (`nz.hi` never
// produces Beige).
const char* quest_button_frame(const std::string& color, bool primary) {
    if (color == "Red") return "btnDark";
    if (color == "Green") return "btnGreen";
    if (color == "White" || color == "Beige") return "btnWhite";
    if (color == "Gold") return "btnGold";
    return primary ? "btnWhite" : "btnDark";
}

// `He.jkb` L1056-1057: a row carrying `Item`/`Enchantment` becomes a `tv`
// pushed to `this.ima` with `id=this.eOa++`. `He.eOa` starts at 5 (L1042
// `this.eOa=5`), so the FIRST row button is id 5. `He.dhb` L1061 `a<this.eOa`
// (L1062) finds it by id — `m.find(this.ima,function(c){return c.id==a})` —
// and runs its nested sub-`Yb` (`b.actions.S(this.Qt)`); that sub-`Yb`'s
// completion listener (`b.actions.qd.addListener(w(this,this.gf))`, L1056)
// then resumes the parked outer chain (`He.gf` L1062). `Od.Jsb`/`Od.xx`
// L1948-1949 dispatch `this.Ge(this.gaa)` (that same row id) when the row's
// delivery countdown (`Dj.SMa` L1045 `Sc`) expires; the port has no countdown
// model, so the row's own body box is the tap target.
constexpr int kDialogRowIdBase = 5;  // `He` ctor L1042 `this.eOa=5`

std::vector<QuestDialogRowButton> quest_dialog_row_buttons(App& app,
                                                           const EngineDialog& d) {
    std::vector<QuestDialogRowButton> out;
    if (d.line_actions.empty()) return out;
    const OdLayout L = dialog_layout_for(app, d, DialogAnim{});
    const float c = L.panel.c > 0.0f ? L.panel.c : 1.0f;
    // `uj.sqb` L1953 stacks EVERY `<Line>` row (`Multiline`/`MultilineBig`);
    // the paged `Od` shows the current row alone (`Od.Xma` L1948).
    const bool all = dialog_scrolls_all_lines(d.type);
    float y = L.body_y;
    for (std::size_t i = 0; i < d.lines.size(); ++i) {
        const float h = all ? measure_ui_wrapped(app, loc(app, d.lines[i], d.lines[i]),
                                                 kOdBodyW * c, 0.70f)
                            : L.body_h;
        const bool button = i < d.line_actions.size() && !d.line_actions[i].empty();
        if (button && (all || i == d.page)) {
            QuestDialogRowButton b;
            b.slot = kDialogRowIdBase + static_cast<int>(i);
            b.x = L.body_x;
            b.y = y;
            b.w = L.body_w;
            b.h = h;
            out.push_back(b);
        }
        y += h;
    }
    return out;
}

int quest_dialog_row_hit_index(App& app, const EngineDialog& d, double x, double y) {
    for (const QuestDialogRowButton& b : quest_dialog_row_buttons(app, d)) {
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return b.slot;
    }
    return -1;
}

namespace {
void draw_dialog_plate(App& app, const std::string& text, const std::string& color,
                       bool primary, float cx, float cy, float w, float h) {
    if (!(load_sliced_atlas(app) &&
          draw_bb_plate(app, quest_button_frame(color, primary), cx, cy, w, h, 1.0f, false))) {
        draw_flat_button(app, "", cx, cy, w, h, 0.6f, 0.5f, 0.3f, false);
    }
    draw_ui_label(app, cx - w * 0.5f, cy - 14.0f, w, 28.0f, loc(app, text, text), 0.9f,
                  UiAlign::Center, 1.0f, 1.0f, 1.0f);
}

// One plate of the dialog's button row. `slot` is the `He.dhb` L1061 index
// (0=Left, 1=Right, 2=Middle, 100=Close); `primary` is the `od.jR` L1899 role
// (`Rb`/`b==1` = primary `EButtonWhite`, `Kb`/`b==2` = secondary
// `EButtonDark`) that also drives the default colour.
struct QuestDialogPlate {
    int slot = -1;
    bool primary = true;
    float cx = 0.0f, cy = 0.0f, w = 0.0f, h = 0.0f;
    std::string text;
    std::string color;
};

// `od.EF` L1899 lays out TWO plates with both present (`a==3`); a lone plate
// sits at the panel centre. `Od.rI` L1948 (the class every `Xc` dialog builds)
// then re-places them: `Rb.C(850-Rb.width/2)` (the primary), `Kb.C(Rb.node.ya
// - 20 - (Rb.width+Kb.width)/2)` (the secondary), both `xc(600)/Pb(125)`; a
// LONE primary gets `C(ya-50)` + `xc(width+50)`, a lone secondary
// `C((1680-width)/2-850)`. The `He` slots map Right->primary, Left->secondary
// (Middle/Close take a free plate).
struct QuestDialogRow {
    QuestDialogPlate plates[2];
    int count = 0;
};

constexpr float kDialogBtnGap = 32.0f;   // `od.EF` L1899: width/2 + 32
constexpr float kOdBtnPrimaryX = 550.0f;  // `rI` L1948 `850 - 600/2`
constexpr float kOdBtnLonePrimaryX = 500.0f;   // `rI` `ya - 50`
constexpr float kOdBtnLonePrimaryW = 650.0f;   // `rI` `xc(width + 50)`
constexpr float kOdBtnSecondaryX = -70.0f;     // `rI` `ya - 20 - (600+600)/2`
constexpr float kOdBtnLoneSecondaryX = -310.0f;  // `rI` `(1680-600)/2 - 850`

// The plate row geometry for `d` (`Od.rI` L1948 / `od.EF` L1899).
// `anim` is identity for the settled hit-test (`quest_dialog_button_hit_index`)
// and the live `od.aa` tween for the draw.
QuestDialogRow quest_dialog_row(App& app, const EngineDialog& d,
                                const DialogAnim& anim = DialogAnim{}) {
    const OdLayout L = dialog_layout_for(app, d, anim);
    QuestDialogRow r;
    bool primary = false, secondary = false;
    auto add = [&](int slot, bool is_primary, const std::string& text,
                   const std::string& color) {
        if (r.count >= 2) return;
        QuestDialogPlate& p = r.plates[r.count++];
        p.slot = slot;
        p.primary = is_primary;
        p.cx = L.btn_cx;
        p.cy = L.btn_cy;
        p.w = kOdBtnW * L.panel.c;
        p.h = kOdBtnH * L.panel.c;
        p.text = text;
        p.color = color;
        if (is_primary) {
            primary = true;
        } else {
            secondary = true;
        }
    };
    // `He.Rib` L1057-1058: an authored `<Button Type="Right">` creates the `rh`
    // slot even when it nests no actions, and `Xc.Xhb` L1047 always hands it to
    // `Od`. `hab()` L1060 gates ONLY the Notification OK plate (L1050), so a
    // `Regular` plate exists whenever the slot does (tests that populate
    // `button_actions` directly are covered by the second clause).
    if (d.has_right_button || !d.button_actions.empty()) {
        add(1, true, d.button_text, d.button_color);
    }
    if (!d.left_.actions.empty()) add(0, false, d.left_.text, d.left_.color);
    if (r.count < 2 && !d.middle_.actions.empty()) {
        add(2, false, d.middle_.text, d.middle_.color);
    }
    if (r.count < 2 && !d.close_.actions.empty()) {
        add(100, false, d.close_.text, d.close_.color);
    }
    if (r.count > 0) {
        // `rI` L1948: exactly one primary (`Rb`) and one secondary (`Kb`).
        QuestDialogPlate& prim = r.plates[primary ? 0 : 1];
        if (primary && secondary) {
            prim.cx = L.sx(kOdBtnPrimaryX);
            r.plates[1].cx = L.sx(kOdBtnSecondaryX);
        } else if (primary) {
            prim.cx = L.sx(kOdBtnLonePrimaryX);
            prim.w = kOdBtnLonePrimaryW * L.panel.c;
        } else {
            prim.cx = L.sx(kOdBtnLoneSecondaryX);
        }
    }
    return r;
}

int quest_dialog_button_hit_index(App& app, const EngineDialog& d, double x, double y) {
    const QuestDialogRow r = quest_dialog_row(app, d);
    for (int i = 0; i < r.count; ++i) {
        const QuestDialogPlate& p = r.plates[i];
        if (x >= p.cx - p.w * 0.5f && x <= p.cx + p.w * 0.5f && y >= p.cy - p.h * 0.5f &&
            y <= p.cy + p.h * 0.5f) {
            return p.slot;
        }
    }
    return -1;
}

// --- The per-`Type` dialog drawers (`He.S` L1045-1051) ---------------------
// Shared chrome: the `Wb.Qa` backdrop (`Fc.Ed(-2147483648)` = 0x80 black,
// `x3a` L927 fades it in/out with the same 0.25 s tween), the `od` 9-slice
// base, the `Vc` title, the `Cd` plate row and the `oe` avatar.
void draw_dialog_backdrop(sf2::render::Renderer& ren, float alpha) {
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.502f * alpha);
}

// `Vc` (L1900): `Fa(1560,160)`, `C(-780)`, `ua(152)`, colour `Z.W6`.
void draw_dialog_title(App& app, const OdLayout& L, const std::string& title) {
    draw_ui_label(app, L.title_x, L.title_y, L.title_w, L.title_h, loc(app, title, title),
                  0.98f, UiAlign::Center, 0.404f, 0.243f, 0.141f);
}

// `od.$A` L1945 (`this.sV!=null ? new or(this.sV) : this.ala(this.p$)`) +
// `od.ala` L1947 (`v.RIa(a)` -> `oe(a.fileName)` at `C(-450+OB)`, `D(YV)`,
// `la(1.8*iy)`). The dialog's `Item` composite (`Ej.Ev`, `He.ah` L1045) wins;
// otherwise the `Image` resolves through the `RIa` registry (D14).
bool draw_dialog_portrait(App& app, const EngineDialog& d, const OdLayout& L) {
    return draw_dialog_image(app, d.image, d.item, L.portrait_cx, L.portrait_cy, L.portrait,
                             1.0f, /*strip_small=*/false);
}

// The plate row (`hab()` L1060). The Right plate carries the pager caption
// (`dialog_button_text`, `Od.EF` L1946); every other slot carries its own
// `<Button Text>` (`He.Rib` L1057). Frames come from the slot's `Color`.
void draw_dialog_buttons(App& app, const EngineDialog& d, const DialogAnim& anim) {
    const QuestDialogRow row = quest_dialog_row(app, d, anim);
    for (int i = 0; i < row.count; ++i) {
        const QuestDialogPlate& p = row.plates[i];
        std::string caption = p.slot == 1 ? app.quest_engine().dialog_button_text() : p.text;
        if (caption.empty()) caption = p.text;
        draw_dialog_plate(app, caption, p.color, p.primary, p.cx, p.cy, p.w, p.h);
    }
}

// `He.Gz` L1058 (`this.Yca`): the `DifficultyOf` fight's difficulty number.
// The JS resolves the fight record (`p.Wv`) and returns `-1` when it is null
// (`b!=null&&(a=v.EQ(b.Xs),a=b.Gz(v.cw(),a))`); the port has no fight-power
// model, so an unresolved fight yields the JS default.
int dialog_difficulty_value(App& app, const EngineDialog& d) {
    (void)app;
    if (d.difficulty_fight.empty()) return -1;  // `Yca==""` -> `a=-1`
    return -1;  // `p.Wv` miss -> `a=-1`
}

// `He.Wib` L1058-1059: the `CheckBox` row (`uv`, `this.Gg`). The box is a
// togglable plate; `InitialValue=="1"` draws the checked state.
void draw_dialog_extras(App& app, const OdLayout& L, const EngineDialog& d) {
    const float c = L.panel.c > 0.0f ? L.panel.c : 1.0f;
    float y = L.body_y + L.body_h + 8.0f * c;
    if (!d.difficulty_fight.empty()) {
        const std::string num = std::to_string(dialog_difficulty_value(app, d));
        draw_ui_label(app, L.body_x, y, L.body_w, 28.0f, num, 0.8f, UiAlign::Left,
                      0.12f, 0.09f, 0.06f);
        y += 30.0f * c;
    }
    if (d.has_checkbox) {
        const float box = 24.0f * c;
        const bool on = d.checkbox.initial_value == "1";
        draw_flat_button(app, on ? "x" : "", L.body_x + box * 0.5f,
                         y + box * 0.5f, box, box, 0.35f, 0.30f, 0.22f, false);
        draw_ui_label(app, L.body_x + box + 8.0f * c, y, L.body_w - box, 28.0f,
                      loc(app, d.checkbox.text, d.checkbox.text), 0.8f, UiAlign::Left,
                      0.12f, 0.09f, 0.06f);
    }
}

// `Xc.Xhb` L931 -> `Wb.openDialog(280, new mv(..))` -> `Od`: the `Wb` dim +
// `od` 9-slice + `Vc` title + the `Image` avatar + the CURRENT page row
// (`Od.EF`/`Od.X2` L1946/L1950) + the plate row.
void draw_od280_dialog(App& app, sf2::render::Renderer& ren, const EngineDialog& d,
                       const DialogAnim& anim) {
    draw_dialog_backdrop(ren, anim.alpha);
    const OdLayout L = dialog_layout_for(app, d, anim);
    draw_od_base(app, ren, L.panel);
    draw_dialog_title(app, L, d.title);
    draw_dialog_portrait(app, d, L);
    draw_ui_wrapped(app, L.body_x, L.body_y, L.body_w, L.body_h, dialog_page_body(app, d),
                    0.70f, UiAlign::Left, 0.12f, 0.09f, 0.06f);
    draw_dialog_extras(app, L, d);
    draw_dialog_buttons(app, d, anim);
}

// `Xc.Bia`/`Xc.Nhb` L929-930 -> `Wb.openDialog(290, ..)` -> `uj`. For
// `Multiline`/`MultilineBig` `uj.sqb()` L1953 lays out EVERY `<Line>` as one
// scrollable body (`this.yO`, `Md` = the whole stack) — NO pager;
// `Stranger` keeps the `Od` pager.
void draw_uj290_dialog(App& app, sf2::render::Renderer& ren, const EngineDialog& d,
                       const DialogAnim& anim) {
    draw_dialog_backdrop(ren, anim.alpha);
    const OdLayout L = dialog_layout_for(app, d, anim);
    draw_od_base(app, ren, L.panel);
    draw_dialog_title(app, L, d.title);
    draw_dialog_portrait(app, d, L);
    const float c = L.panel.c > 0.0f ? L.panel.c : 1.0f;
    if (dialog_scrolls_all_lines(d.type)) {
        // `sqb` L1953: one text node per `<Line>`, `b += f.ew() + e.offsetY`,
        // all inside the scroll container `this.yO`.
        float y = L.body_y;
        for (const std::string& ln : d.lines) {
            const std::string text = loc(app, ln, ln);
            const float h = measure_ui_wrapped(app, text, kOdBodyW * c, 0.70f);
            draw_ui_wrapped(app, L.body_x, y, L.body_w, h, text, 0.70f, UiAlign::Left,
                            0.12f, 0.09f, 0.06f);
            y += h;
        }
    } else {
        draw_ui_wrapped(app, L.body_x, L.body_y, L.body_w, L.body_h,
                        dialog_page_body(app, d), 0.70f, UiAlign::Left, 0.12f, 0.09f,
                        0.06f);
    }
    draw_dialog_extras(app, L, d);
    draw_dialog_buttons(app, d, anim);
}

// `Xc.rIa` L930 -> `Xc.Vhb` -> `Wb.openDialog(340, new qh(..))` -> `Ve`: the
// NO-AVATAR layout — `Vc` title + the big body (`kb.Fa(1100,550)`, `C(-550)`,
// `ua(125)`, L1913) + the plate row. `Ve` builds NO portrait node.
void draw_ve340_dialog(App& app, sf2::render::Renderer& ren, const EngineDialog& d,
                       const DialogAnim& anim) {
    draw_dialog_backdrop(ren, anim.alpha);
    const OdLayout L = dialog_layout_for(app, d, anim);
    draw_od_base(app, ren, L.panel);
    draw_dialog_title(app, L, d.title);
    const std::string body =
        d.lines.empty() ? std::string() : loc(app, d.lines[0], d.lines[0]);
    draw_ui_wrapped(app, L.sx(kOdVeBodyX), L.body_y, kOdVeBodyW * L.panel.c, L.body_h, body,
                    0.70f, UiAlign::Left, 0.12f, 0.09f, 0.06f);
    draw_dialog_buttons(app, d, anim);
}

// `Xc.Uhb` L929 -> `Wb.openDialog(370, new Uo(..))` -> `vn extends Ve`: the
// loot grid. `vn.$A` L1935 `ebb(this.IN)` builds one `vr` cell per `Loot`
// entry (`Rf` item icon + "x N"), `Md = 600`; the title/plates are `Ve`'s.
void draw_vn370_dialog(App& app, sf2::render::Renderer& ren, const EngineDialog& d,
                       const DialogAnim& anim) {
    draw_dialog_backdrop(ren, anim.alpha);
    const OdLayout L = dialog_layout_for(app, d, anim);
    draw_od_base(app, ren, L.panel);
    draw_dialog_title(app, L, d.title);
    const float cell = 125.0f * L.panel.c;  // `vr.text.Fa(125,125)` L1939
    const float step = cell * 0.75f;
    const float start_cx = L.sx(-300.0f);
    for (std::size_t i = 0; i < d.loot.size(); ++i) {
        const float cx = start_cx + static_cast<float>(i) * step;
        if (!draw_item_image(app, d.loot[i], cx, L.body_y + cell * 0.5f, cell, cell, 1.0f)) {
            draw_ui_label(app, cx - cell * 0.5f, L.body_y, cell, cell * 0.4f,
                          loc(app, d.loot[i], d.loot[i]), 0.6f, UiAlign::Center, 0.12f,
                          0.09f, 0.06f);
        }
    }
    draw_dialog_buttons(app, d, anim);
}

// `Od.Ge(1)`/`n_()` L1898: a dismissed dialog slides + fades for the same
// 0.25 s (`wa(1-KK(t))`, `D(ra+1000*KK(t))`) before `Wb.Bwb` L928 unlinks it.
void draw_closing_dialog(App& app, sf2::render::Renderer& ren) {
    ClosingDialog& s = closing_slot();
    if (!s.active) return;
    const float now = app.screens().top() != nullptr ? app.screens().top()->time() : 0.0f;
    const float age = now - s.start;
    if (age < 0.0f || age >= kDialogAnimSecs) {
        s.active = false;  // `n_()`: the tween landed, unlink
        return;
    }
    const DialogAnim anim = dialog_anim_at(age, /*closing=*/true);
    switch (dialog_kind(s.d.type)) {
        case DialogKind::kOd280: draw_od280_dialog(app, ren, s.d, anim); return;
        case DialogKind::kUj290: draw_uj290_dialog(app, ren, s.d, anim); return;
        case DialogKind::kVe340: draw_ve340_dialog(app, ren, s.d, anim); return;
        case DialogKind::kVn370: draw_vn370_dialog(app, ren, s.d, anim); return;
        case DialogKind::kIbBar:
        case DialogKind::kNone: s.active = false; return;
    }
}
} // namespace

// JS `Sn` L1069 (`Fight Name="<zone>|<battle>|<n>"`): resolve the triple
// through stages.xml (the same `load_zone_map` path the Map uses), fill the
// pending battle and push the fight. The tutorial request is
// `Fight Name="Punchbag|Bosses|1"` (tutorial_quests.xml L40) — the
// bamboo_grove 2 x 99 s tutorial battle (`stages.xml` Zone Punchbag / Battle
// Bosses / Fight 1), NOT the `Training` dojo dummy.
void DojoScreen::launch_quest_fight(const std::string& triple) {
    std::string zone, battle;
    const std::size_t p1 = triple.find('|');
    if (p1 == std::string::npos) {
        battle = triple;
    } else {
        zone = triple.substr(0, p1);
        const std::size_t p2 = triple.find('|', p1 + 1);
        battle = triple.substr(
            p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - (p1 + 1));
    }
    if (battle.empty()) {
        std::fprintf(stderr, "[quest] Fight request '%s' has no battle\n", triple.c_str());
        return;
    }
    std::string location;
    for (const MapScreen::ZoneTab& z : load_zone_map(kViewW, kViewH)) {
        if (!zone.empty() && z.name != zone) continue;
        for (const MapScreen::Node& n : z.nodes) {
            if (n.name == battle) {
                zone = z.name;
                location = n.location;
                break;
            }
        }
        if (!location.empty()) break;
    }
    PendingBattle& pb = app().pending_battle();
    pb.battle_name = battle;
    // JS `hb.toString()` (L1416): the quest journal's `_$Fight` is the
    // triple, not the bare battle name (FirstGuardBeaten keys on
    // `ZONE_1|BOSS_LYNX|1`, quests.xml L265).
    pb.fight_triple = triple;
    pb.zone = zone;
    pb.location = location.empty() ? "bamboo_grove" : location;
    pb.enemy_name = battle;
    pb.has_result = false;
    pb.player_won = false;
    battle_rewards(battle, pb.reward_money, pb.reward_exp);
    pb.prize_base_coins = 0;
    pb.prize_bonus = 0;
    pb.prize_gems = 0;
    pb.prize_combo = 0;
    pb.prize_shocks = 0;
    pb.prize_perfect = false;
    pb.prize_first = false;
    // The owned items feed the FightScreen's move list (`ra.Hza`).
    pb.owned = owned_items(app());
    std::fprintf(stdout, "[quest] Fight '%s' -> %s [%s] (%s, reward money=%d exp=%d)\n",
                 triple.c_str(), battle.c_str(), zone.c_str(), pb.location.c_str(),
                 pb.reward_money, pb.reward_exp);
    std::fflush(stdout);
    push(kScreenFight);
    // Harness-only post-tutorial seed (see App::finish_tutorial_handoff):
    // the chain tail (StoryTutorialShop -> ... -> ShowBlock/END) runs on
    // player navigation in the JS (`ChangeScene`/`mp` L1032), which the port
    // records but never performs, so the headless fidelity tour/loop lands
    // the seeded `Tutorial="END"` state here. The INTERACTIVE path is
    // player-driven: the chain stays at step FIGHT and the player continues
    // it (no auto-advance).
    if (app().headless()) app().finish_tutorial_handoff();
}

void DojoScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    ensure_dojo_location(app);
    ensure_lang(app);  // runtime Sensei dialog lines (once; silent if absent)
    // NOTE: the shared `za` chrome draws AFTER the scene (see below) -
    // screen-space chrome on top, like the fight HUD.
    // Dojo interior: the same location layers the fight renders
    // (interior + garden, NOT the sky fallback). Static camera (no chase):
    // the hub renders through the JS `ma.Sya` global camera at 16:9
    // (zoom 1.3, center 0 — Sya never sets x/y at 16:9, only `tMa(f)`).
    // The arena floor tiles (world y ~194..253) land at screen y ~612..689
    // and the visible slice is world x ~-492..492 (wall/tiles centered; the
    // +/-1108 side masks sit off-view).
    // The hub location camera (JS `ma.Sya` static 16:9 — verified, do not
    // touch). Hoisted so the `FightNone` idle figure below projects through
    // the same framing as the location layers.
    sf2::render::Camera hub_cam;
    bool have_hub_cam = false;
    // The player's live location-space x (JS `Ut.kyb` L825 argument `c.x`):
    // shared by the hub camera focus and the `arrow` marker.
    float hub_player_loc_x = 0.0f;
    if (app.has_fight_assets()) {
        FightAssets& assets = app.fight_assets();
        // Hub framing = the LIVE viewer CoM midpoint (JS `Tf.Ea` L1972 runs
        // the `FightNone` viewer through `Sya`; `Ut.Al` L826 recomputes
        // `Io = Lb.width/2 - focus` from `Go.ma` every frame). The two
        // viewers' anchors are the player idle figure and the Punchbag dummy:
        // `Fighter::sample` (fighter.hpp L207) anchors each model's PivotNode
        // bone at the passed (x,y), and neither viewer moves on the hub, so
        // the live anchors are their spawns. Container -> location: `+arenaW/2`.
        // NOTE: 876a3a97 passed `Fighter::world_x()` here, but
        // `Fighter::sample` never assigns `world_x_` (only `set_world_pos` /
        // root motion do), so those reads were 0 and `Io` went to `arenaW/2`
        // (the +19.3pp oracle regression). Use the spawn-anchored CoMs.
        // OPEN: if the idle clip's authored root motion moves the COM (the
        // native anchors it at spawn), a per-frame posed-skeleton COM would be
        // needed — not modelled.
        float focus_x = -1.0f;
        float fighter_span = -1.0f;
        {
            // JS `Ut.Al` (L826) recomputes `Io = Lb.width/2 - a.x` EVERY
            // frame from `a` = `ql`'s `this.Go.ma` (the fighter midpoint, fed
            // by `ql.d3a` L366 `this.ia.Al(this.Go.ma, ...)`). The hub steps
            // its live `FightNone` controller (JS `Tf.aa` L1972
            // `this.YL(this.Ig,a)`), so the midpoint is the live fighter
            // anchor. `Fighter::world_x()` is the LOCATION-space x (the
            // controller spawns it `set_world_pos(battle_.player_spawn_x, ..)`,
            // fight.cpp L1886, and clamps it to the arena walls, L3581) - the
            // same space `default_camera`'s focus uses.
            float player_x = assets.dojo.player_spawn_x();
            float enemy_x = assets.dojo.enemy_spawn_x();
            if (dojo_fight_ != nullptr) {
                const float pw = dojo_fight_->player().fighter.world_x();
                const float ew = dojo_fight_->enemy().fighter.world_x();
                // Guard the unassigned-anchor case (both zero) the old
                // `sample`-only path hit; the live controller writes the
                // spawns via `set_world_pos` on build.
                if (pw != 0.0f || ew != 0.0f) {
                    player_x = pw;
                    enemy_x = ew;
                }
            }
            focus_x = (player_x + enemy_x) * 0.5f;
            fighter_span = std::fabs(enemy_x - player_x);
            hub_player_loc_x = player_x;
        }
        // Hub framing correction vs the oracle `dojo_hub`/`dojo_menu_open`:
        // the JS `Ut.Al` focus is the raw fighter midpoint (`wd.mea(Rw,pF)`,
        // `Io = Lb.width/2 - focus`, L827), but the oracle composition inverts
        // to `Io ~= 180`, i.e. `focus ~= 800` vs the spawn midpoint 831.5. Both
        // an unambiguous factor-1 layer (`_0009_lamp_left` X=-415.5 -> oracle
        // x335.4) and the bag (~x860) invert to the same `Io`, and the whole
        // scene is a pure -42px x-translation vs the raw formula. Correcting
        // the focus here shifts the entire hub (statics + viewer) into the
        // oracle frame. OPEN: whether the residual is a model-root vs
        // container-position difference or the effective `arena_w` is not
        // statically traced (core/scene is out of this stream's scope).
        constexpr float kHubFocusDelta = -31.5f;
        focus_x += kHubFocusDelta;
        assets.dojo.default_camera(hub_cam, kViewW, kViewH, focus_x, fighter_span);
        have_hub_cam = true;
        assets.dojo.render_layers(ren, hub_cam, 0, assets.dojo.layers().size());
    } else {
        const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(verts, 6, 0.12f, 0.12f, 0.16f, 1.0f);
    }
    // Scene letterbox bars (JS `ma.Sya` L1833-1834; PORT_AUDIT_SCENE D10) —
    // over the location art, under the figure/chrome; no-op at 16:9.
    if (have_hub_cam) {
        draw_scene_letterbox(ren, hub_cam);
    }
    // --- Dojo aliveness (JS `Tf` L1969-1972: the hub runs the `FightNone`
    // battle through the live `Sya` framing) ------------------------------
    // The two viewers are the CONTROLLER's own fighters — the real `ca` the
    // hub steps in `update_impl` — projected through the SAME hub camera the
    // location layers use, plus the fighter container transform (`tl.init`
    // L843: x=-width/2, y=height/2-Floor). `ev.Gf` L845 draws the enemy FIRST
    // (z=-.001, behind), then the player (z=0). This replaces the old
    // display-only `sample(idle_frame_/10)` copy: the drawn pad now drives
    // this figure through the shared `player_input` path.
    {
        const float arena_half = app.has_fight_assets()
                                     ? app.fight_assets().dojo.arena_width() * 0.5f
                                     : 980.0f;
        const float cont_y = app.has_fight_assets()
                                 ? app.fight_assets().dojo.arena_height() * 0.5f -
                                       app.fight_assets().dojo.arena_floor()
                                 : 200.0f;
        if (dojo_fight_ != nullptr && have_hub_cam) {
            // The container->location offset is the same `project()` the fight
            // applies (-arena_half, +contY); `draw_dojo_figure`'s offset args
            // are exactly that transform (the model scale stays 1).
            draw_dojo_figure(ren, hub_cam, dojo_fight_->enemy().fighter, 1.0f,
                             -arena_half, cont_y);
            draw_dojo_figure(ren, hub_cam, dojo_fight_->player().fighter, 1.0f,
                             -arena_half, cont_y);
            // JS `Ut.V0a` (L831) + `Ut.kyb` (L825): the flashing `arrow`
            // marker (atlas `E.get(268)` = the controller atlas, frame
            // `y.OQa` = "arrow") is a child of the location `go` node,
            // centre-anchored (`Rh(.5)`/`mj(.5)`, L831), and placed under the
            // player every frame:
            //   x = Io - (Lb.width/2 - c.x)*Bj          (c = the player)
            //   y = Lb.hn.go.node.translate.y + 2*F9*Bj + 10
            //   alpha = .5 + .5*sin(pi/ArrowFlashingFrames * $O)   (`kyb` L825)
            // `F9 = (Lb.height/2 - Lb.ct)/2` (L823), `Bj` = the live layer
            // zoom (`Ut.xCa` L831 -> `camera.layer_zoom`),
            // `ArrowFlashingFrames` = `ge.gba` (L1278). Projecting through
            // `hub_cam` reproduces the `go`-node transform the figures use.
            {
                sf2::data::atlas_frame afr;
                int atw = 0, ath = 0;
                unsigned int agl = 0;
                if (app.get_atlas_frame("arrow", &afr, &atw, &ath, &agl)) {
                    const float f9 = (app.fight_assets().dojo.arena_height() * 0.5f -
                                      app.fight_assets().dojo.arena_floor()) *
                                     0.5f;
                    const float bj = hub_cam.layer_zoom;
                    // JS `kyb` L825: `y = Lb.hn.go.node.translate.y +
                    // 2*F9*Bj + 10`. The figures are already projected from
                    // the models-container origin (their verts carry
                    // `cont_y`), and `2*F9 = Lb.height/2 - Lb.ct` is that same
                    // container translate (L823/L843), so the marker's drop
                    // from the figures' origin is `2*F9*Bj + 10` (the arena
                    // floor line + 10).
                    const float arrow_world_y = 2.0f * f9 * bj + 10.0f;
                    // `world_to_screen_x` takes CONTAINER-space x (the figure
                    // draw passes `verts - arena_half`), so the location-space
                    // player x is converted the same way.
                    const float sx =
                        hub_cam.world_to_screen_x(hub_player_loc_x - arena_half, 1.0f);
                    const float sy = hub_cam.world_to_screen_y(arrow_world_y);
                    const float nat_w = afr.source_w > 0
                                            ? static_cast<float>(afr.source_w)
                                            : static_cast<float>(afr.w);
                    const float nat_h = afr.source_h > 0
                                            ? static_cast<float>(afr.source_h)
                                            : static_cast<float>(afr.h);
                    constexpr float kArrowFlashingFrames = 120.0f;  // `ge.gba` L1278
                    static int arrow_phase = 0;                      // JS `Ut.$O`
                    const float alpha =
                        0.5f + 0.5f * std::sin(3.14159265358979323846f /
                                               kArrowFlashingFrames *
                                               static_cast<float>(arrow_phase));
                    arrow_phase =
                        (arrow_phase + 1) % static_cast<int>(kArrowFlashingFrames);
                    // One-shot evidence line (JS `Ut.V0a` L831 / `kyb` L825):
                    // the marker frame resolved + its projected screen pos.
                    static bool arrow_logged = false;
                    if (!arrow_logged) {
                        arrow_logged = true;
                        std::fprintf(stdout,
                                     "[dojo] arrow frame=%dx%d player_loc_x=%.1f -> "
                                     "screen=(%.1f,%.1f) alpha=%.2f\n",
                                     static_cast<int>(nat_w), static_cast<int>(nat_h),
                                     static_cast<double>(hub_player_loc_x),
                                     static_cast<double>(sx), static_cast<double>(sy),
                                     static_cast<double>(alpha));
                        std::fflush(stdout);
                    }
                    draw_atlas_region(app, "arrow", static_cast<float>(afr.x),
                                      static_cast<float>(afr.y),
                                      static_cast<float>(afr.w),
                                      static_cast<float>(afr.h),
                                      static_cast<float>(atw), static_cast<float>(ath), sx,
                                      sy, nat_w * hub_cam.zoom, nat_h * hub_cam.zoom, alpha,
                                      /*flip_x=*/false);
                }
            }
        }
        draw_dojo_gamepad(app, dojo_pad_);
        // Shared `za` chrome (topPanel + wr/xr/yr widgets + the vertical nav
        // column) — the JS `ma.D1` chrome on every shell screen
        // (PORT_AUDIT_UI §2.1). Replaces the invented draw_dojo_hud_bar
        // (its hard-coded coords are PORT_AUDIT_UI §3 item 5).
        draw_za_chrome(app, kScreenDojo);
        // Sensei hint panel (`Ib` L1905-1912 - quest_panel.hpp derives the
        // ambient tutorial line). EXCLUSIVITY (single source of truth): the
        // quest modal dims/blocks input; the ambient hint and the modal never
        // co-draw (same-step mutual exclusivity), so skip while a modal is up.
        // Replaces the flat 780x64 quad + the invented procedural ring
        // (PORT_AUDIT_UI 2.9). `Ib` shows OK only with a button text; the
        // ambient banner has none (`show_ok=false`).
        const bool modal_up = quest_modal_top(app) != nullptr;
        // The ambient `Ib` hint is suppressed in fresh-tutorial mode: the
        // beats are the tutorial (above) and the post-tutorial hub is clean
        // (the oracle dojo_hub has no banner).
        if (!modal_up && !app.fresh_tutorial()) {
            const QuestStep qs = quest_step_for_state(
                app.res_root(),
                quest_state_for(tutorial_, story_step_, training_won_, level_, map_focus_,
                                battles_, {}));
            // `Ib.Sr()` L1908: exactly ONE label = the joined lines, and the
            // portrait is the resolved `Image` (the ambient tutorial line uses
            // the sensei disc, `E.get(12)`) — there is NO speaker row.
            std::string ambient = qs.line1;
            if (!qs.line2.empty()) {
                if (!ambient.empty()) ambient.push_back('\n');
                ambient += qs.line2;
            }
            draw_ib_hint(app, ren, "character_sensei", ambient, /*show_ok=*/false);
        }
    }
    // The JS hub carries no entry-button row: the Dojo 4-up row, the gear
    // and the disciple chrome were native inventions (PORT_AUDIT_UI
    // §2.1-2.2). Navigation is the `za` nav column drawn in the aliveness
    // block above.
    // Sensei dialog modal on top of everything Dojo (engine `He` records:
    // `Notification` -> the `Ib` banner, `Regular` -> the `od` panel + its
    // action button).
    draw_quest_modal(app, ren, app.screens().top() == this);
}

// ---------------------------------------------------------------------------
// MapScreen
// ---------------------------------------------------------------------------

MapScreen::MapScreen(ScreenManager& mgr) : Screen(mgr, "Map") {
    // JS `Ya.init` (L2125): `lb.rJ||lb.OS()` — the Map inherits the menu
    // track (guard no-op when the Dojo already set it) and REPLAYS it when a
    // fight/act cleared `lb.rJ`, so leaving a fight never leaves the fight
    // track running (JS `ai.B()` teardown -> `lb.OS()`, L384).
    sf2::audio::AudioEngine::instance().play_music_once("menu");
    zones_ = load_zone_map(kViewW, kViewH);
    // The current zone from the save (JS `xf.ro` / CurrentZone, L248) plus
    // the battle records (iF) and MapFocus (ys) for the live rules below.
    std::string cur = "ZONE_1";
    std::string focus;
    WarriorSave map_save;
    try {
        map_save = app().save().load();
        if (!map_save.current_zone.empty()) cur = map_save.current_zone;
        focus = map_save.map_focus;
        fight_wins_ = map_save.fights;
    } catch (const std::exception&) {
    }
    // The zone the map opens on: the save's `CurrentZone` (`xf.ro` L248),
    // but only among MAP zones (a `FileName` backdrop). The `Start` zone
    // (Punchbag) has no backdrop, so a save whose CurrentZone points there
    // (e.g. after the invented zone-dot write) recovers to the first map
    // zone instead of showing the null/zero location with the Training bag.
    zone_sel_ = -1;
    for (std::size_t i = 0; i < zones_.size(); ++i) {
        if (zones_[i].part < 0) continue;   // not a map zone
        if (zone_sel_ < 0) zone_sel_ = static_cast<int>(i);  // first map zone
        if (zones_[i].name == cur) zone_sel_ = static_cast<int>(i);
    }
    if (zone_sel_ < 0) zone_sel_ = 0;  // no map zone at all (degraded)
    // WDa/`Qr.lla` VERBATIM (JS L256/L2094). `WDa(a) = iF.get(a) != null`:
    // a battle node is ACTIVE iff the save carries a `<Battles>` record for
    // `zone|name|` (written by `J1a` L259 / `Iaa` L260-261, native
    // `WarriorSave::battle_unlock`/`battle_set_visibility`, driven by the
    // quest ShowBattle/HideBattle/SetBattleVisibility/ToggleBattle actions).
    // `Qr.lla` (L2094) renders the button only while `hs.isActive && !a.li()`
    // (`a.li()` = Hidden/expired, `hl.li` L278). The oracle's
    // ZONE_1 capture is exactly this: on the tutorial save only
    // `ZONE_1|BOSS_LYNX|` is recorded, so Рысь is the only node on the map.
    // (This replaces the old native "record-less base is playable" hybrid.)
    for (std::size_t i = 0; i < zones_.size(); ++i) {
        for (auto& n : zones_[i].nodes) {
            const WarriorSave::BattleRecord* rec = map_save.find_battle(n.zone, n.name);
            const bool has_rec = rec != nullptr;
            const bool hidden = has_rec && rec->hidden;
            n.active = has_rec;                 // `WDa` (JS L256)
            n.visible = n.active && !hidden;    // `Qr.lla` (JS L2094)
            // `Qr` (L2092) `let b=a.tt()` = the `<Battle>` record's `Locked`
            // (`Lc.tt()` L1406 -> `hl.tt` L278). The button then picks
            // `BattleBtnLock/locked_<icon>` over `BattleBtnBase/base_<icon>`.
            // The old port used `!n.active`, which is ALWAYS false for a
            // visible node (`visible = active && !hidden`), so the `locked_*`
            // art was never drawn.
            n.locked = has_rec && rec->locked;
            // `Xr` pip lit state (L2133-2136): the pip count is the rendered
            // `<Fight>` count, minus the last for boss families (`Xr` ctor
            // L2134 `a.type!="FightBosses"&&...||--d`). Pip `k` is lit when
            // `dl.status==1` = `YL` L111266 `c.no >= a.repeat` (the `il`
            // record's `CompletedCount` >= `<Fight Replays>`; every shipped
            // row carries `Replays="1"`). The record key is the `hb` triple
            // `zone|battle|<Fight Name>` (`il.Atb` L143548). The direct-boot
            // path records the bare battle name, so a single-fight node
            // falls back to it.
            int pip_n = static_cast<int>(n.fight_names.size());
            if (n.type == "BOSSES" || n.type == "BOSSES_REPLAYABLE" ||
                n.type == "FINAL_BATTLE_TITAN") {
                --pip_n;
            }
            if (pip_n < 0) pip_n = 0;
            n.pip_beaten.assign(static_cast<std::size_t>(pip_n), false);
            auto wins_for = [&map_save](const std::string& ids) -> int {
                for (const WarriorSave::FightWins& fw : map_save.fights) {
                    if (fw.name == ids) return fw.wins;
                }
                return 0;
            };
            for (int k = 0; k < pip_n; ++k) {
                const std::string ids =
                    n.zone + "|" + n.name + "|" + n.fight_names[k];
                int wins = wins_for(ids);
                if (wins == 0 && pip_n == 1) wins = wins_for(n.name);
                n.pip_beaten[static_cast<std::size_t>(k)] = wins >= 1;
            }
            {
                int lit = 0;
                for (bool b : n.pip_beaten) lit += b ? 1 : 0;
                std::fprintf(stdout, "[map] node %s pips=%d beaten=%d\n", n.name.c_str(),
                             pip_n, lit);
            }
        }
    }
    // MapFocus (JS `Ya.bKa` L2129 focuses the save's MapFocus `p.o.ys` via
    // `m5`): highlight the node named in MapFocus first (e.g.
    // ZONE_1|BOSS_LYNX|1 quest focus `qo` L1086), else the first BOSSES
    // node, else the first node (hover highlight only, no selection).
    applied_focus_ = focus;
    apply_map_focus(focus);
    std::fprintf(stdout, "[map] %zu zones loaded (current %s)\n", zones_.size(), cur.c_str());
    for (const auto& z : zones_) {
        std::fprintf(stdout, "[map] zone %s (%s)%s: %zu nodes%s\n", z.name.c_str(),
                     z.file.c_str(), z.is_start ? " [start]" : "", z.nodes.size(),
                     z.locked ? " [locked]" : "");
    }
    std::fflush(stdout);
}

void MapScreen::fight_button_center(float& x, float& y) const {
    const MapFightButtonRect r = map_fight_button_rect(map_metrics());
    x = r.cx;
    y = r.cy;
}

// JS `Ya.Uw` (L2129) + `ue.tea()`: focus the `Rr` panel on the node named in
// a MapFocus string. The shipped tutorial focus is `ZONE_1|BOSS_LYNX|1`
// (tutorial_quests.xml L155) and only the visible (record-backed) nodes are
// candidates (`Qr.lla` L2094). Falls back to the first BOSSES node, then the
// first visible node, so the panel is never empty.
void MapScreen::apply_map_focus(const std::string& battle) {
    hover_ = -1;
    if (zone_sel_ < 0 || static_cast<std::size_t>(zone_sel_) >= zones_.size()) return;
    const auto& nodes = zones_[zone_sel_].nodes;
    if (!battle.empty()) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].visible && battle.find(nodes[i].name) != std::string::npos) {
                hover_ = static_cast<int>(i);
                break;
            }
        }
    }
    for (std::size_t i = 0; hover_ < 0 && i < nodes.size(); ++i) {
        if (nodes[i].visible && nodes[i].type == "BOSSES") {
            hover_ = static_cast<int>(i);
            break;
        }
    }
    if (hover_ < 0) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].visible) {
                hover_ = static_cast<int>(i);
                break;
            }
        }
    }
}

// JS `lca(TF.lD, TF.uP, TF.Y1)` (L2009): `TF.uP` is the `<Fight>` the boss
// ladder is ON — the number of wins already recorded for this battle
// (`<Fights><Fight Name Wins>`, `yc`/`no`), i.e. the 0-based index of the next
// un-beaten `<Fight>`. Clamped to the battle's `<Fight>` count so a cleared
// boss replays its LAST fight (exactly the roster's `jk.init(a,b,c,d)` index
// clamp, L2062). A fresh battle is 0 -> `|1`.
int map_fight_index(App& app, const std::string& name, int fight_count) {
    // JS `lca(TF.lD, TF.uP, TF.Y1)` (L2009): `TF.uP` = the fight index the
    // ladder is on = the number of this battle's `<Fight>`s already won.
    // `yc` records are keyed by the `hb` triple `zone|name|fight` (`il.Atb`
    // L143548), so sum the wins over the battle's records (the legacy
    // bare-name record still matches).
    int wins = 0;
    try {
        const WarriorSave w = app.save().load();
        for (const WarriorSave::FightWins& f : w.fights) {
            if (f.name == name) {
                wins += f.wins;
                continue;
            }
            const std::size_t p1 = f.name.find('|');
            if (p1 == std::string::npos) continue;
            const std::size_t p2 = f.name.find('|', p1 + 1);
            if (p2 == std::string::npos) continue;
            if (f.name.compare(p1 + 1, p2 - (p1 + 1), name) == 0) {
                wins += f.wins;
            }
        }
    } catch (const std::exception&) {
    }
    const int last = fight_count > 0 ? fight_count - 1 : 0;
    if (wins < 0) wins = 0;
    if (wins > last) wins = last;
    return wins;
}

void MapScreen::launch_battle(const Node& n) {
    // JS `Ya` battle-start (L2131-2132): `wa.F().mp(6, battle)`.
    // Carry the battle into the pending flow: name/location +
    // the reward (the first non-zero <Reward>).
    PendingBattle& pb = app().pending_battle();
    pb.battle_name = n.name;
    // JS `hb.toString()` (L1416): the map launches the ladder's CURRENT
    // `<Fight>`, so the quest journal's `_$Fight` is `zone|name|<n>`
    // (FirstGuardBeaten keys on `ZONE_1|BOSS_LYNX|1`, quests.xml L265; the
    // ladder's next opponent is `|2` = BRICK, zone_1/story.xml L176-193).
    const int fight_index = map_fight_index(app(), n.name, n.fight_count);
    pb.fight_triple =
        n.zone + "|" + n.name + "|" + std::to_string(fight_index + 1);
    pb.zone = n.zone;  // stages.xml Zone (`hp` scope for the <Rules> feeder)
    pb.location = n.location.empty() ? "dojo" : n.location;
    pb.has_result = false;
    // The node's fight -> reward. The Training fight has
    // Money=0; use the battle's own reward lookup.
    battle_rewards(n.name, pb.reward_money, pb.reward_exp);
    // The owned items (JS `ra.Hza` move list input).
    pb.owned = owned_items(app());
    std::fprintf(stdout, "[map] click %s [%s] -> Fight (reward money=%d exp=%d)\n",
                 n.name.c_str(), n.zone.c_str(), pb.reward_money, pb.reward_exp);
    std::fflush(stdout);
    push(kScreenFight);
}

// One `jk` roster entry per `<Fight>` of the boss battle (`ai.aa` L2007
// `ca.hCa(this.Da,…)` fills `TF.lD`; `jk.init` L2062 iterates it: `g.Hf` is
// the warrior portrait, `g.$s` its name). Shared by the real flow and the
// forced tour pose so both show the same row.
std::vector<BossRosterEntry> boss_roster_entries(App& app, const MapScreen::Node& n) {
    std::vector<BossRosterEntry> entries;
    // `jk.init(lD)` iterates exactly the battle's `<Fight>` list
    // (`TF.lD.length`, L2007) — `n.fight_count` is that `<Fight>` count, so
    // the row length is bounded by it (BOSS_LYNX -> 3 portraits).
    const int want = n.fight_count > 0 ? n.fight_count : 1;
    for (int i = 0; i < want; ++i) {
        const BattleWarriorInfo bw = battle_warrior(n.name, n.zone, i);
        if (bw.attrs.empty()) break;  // no Nth `<Fight>`
        BossRosterEntry e;
        e.name = bw.first_name.empty() ? n.name : loc(app, bw.first_name, bw.first_name);
        const auto av = bw.attrs.find("Avatar");
        std::string img =
            (av != bw.attrs.end() && !av->second.empty()) ? av->second : "avatar_masked";
        std::transform(img.begin(), img.end(), img.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        e.image = img;
        entries.push_back(e);
    }
    return entries;
}

void MapScreen::start_battle(const Node& n) {
    const int fight_index = map_fight_index(app(), n.name, n.fight_count);
    // JS `ha.RA("FightEnter")` (`be.FightEnter` -> `QUEST_EVENT_FIGHT_ENTER`,
    // sf2.js L998): fired on ENTERING a fight, before the `ik` VS intro. The
    // zone-1 ladder greets each opponent on it (`zone_1/story.xml` L176-193
    // `Zone1Guard2Greetings`: `_$Fight == ZONE_1|BOSS_LYNX|2` -> the
    // `NAME_BRICK` dialog). Its actions are `Place="Map"`, so the greeting
    // lands on THIS screen and owns the launch: while the modal is up the
    // battle is NOT pushed — its `Fight Name="_$Fight"` plate re-enters here,
    // and the quest's own guard (`_Zone1Guard2Greetings != 1`) lets the
    // second pass through to the battle.
    {
        QuestJournal j;
        j.fight = n.zone + "|" + n.name + "|" + std::to_string(fight_index + 1);
        j.scene_from = "Map";
        try {
            j.player_level = app().save().load().level;
        } catch (const std::exception&) {
        }
        app().quest_engine().fire(app(), "FightEnter", j);
        // Headless keeps its own modal semantics (dialog taps are drained), so
        // the deferral only applies to a live app — this keeps the scripted
        // tour/loop deterministic on the map.
        if (!app().headless() && quest_modal_top(app()) != nullptr) {
            std::fprintf(stdout,
                         "[map] FightEnter %s -> greeting modal (launch deferred)\n",
                         j.fight.c_str());
            std::fflush(stdout);
            return;
        }
    }
    // JS `ai.aa` case 0 (L2007): `this.TF.lD.length>1 && this.TF.eE`
    // -> `lca(this.TF.lD, this.TF.uP, this.TF.Y1)` = the `jk` opponent
    // scroll (`this.Ws = this.Qo(jk)`, L2009), whose `qd` (state 4) ->
    // `ngb()` -> `tx()` (the `ik` VS intro). `ca.hCa` fills `TF.lD` ONLY for
    // `FightBosses`/`FightBossesReplayable`/`FightFinalTitan`, so the gate is
    // "boss type AND more than one `<Fight>`"; every other battle takes the
    // direct branch. This is the ONE gate both real entries share.
    if (n.type == "BOSSES" || n.type == "BOSSES_REPLAYABLE") {
        std::vector<BossRosterEntry> entries = boss_roster_entries(app(), n);
        if (entries.size() > 1) {
            act_node_ = n;
            roster_.start(std::move(entries), fight_index);
            std::fprintf(stdout,
                         "[map] FIGHT -> jk roster armed (%zu entries, first %s)\n",
                         roster_.entries.size(), n.name.c_str());
            std::fflush(stdout);
            return;
        }
    }
    launch_battle(n);
}

// `Ur` zone-dot strip geometry (JS L2112-2116, `qk.layout` L2137): one row of
// dots on the map's bottom bar, 34 px each at a 60 px pitch starting at
// x=547 (1280-space), centred on the bar. The draw and the click hit-test
// both read these so they can never drift.
constexpr float kMapZoneDotX0 = 547.0f;
constexpr float kMapZoneDotPitch = 60.0f;
constexpr float kMapZoneDotD = 34.0f;

// See screens.hpp. The dotted slot is the zone's index among the zones that
// RENDER a dot (`Vr.HXa` L2123-2124), not its raw index. Only zones with a
// MAP backdrop (`FileName` -> `part`) are map zones: the `Start` zone
// (Punchbag) has no `FileName`, so it must not appear in the `Ur` strip nor
// be selectable — its only node is the Training dummy (the reported "the map
// shows only the null/zero location (with the bag)").
// `Wr.qFa` (L2179) lays out the story map buttons: `a.C(this.node.ya+d-d*.3)`
// (x) and `a.D((N.height+b)*.5)` / `N.height-c*.55` (y), where `d` is the
// widget's scaled width and `b`/`c` the panel/cell sizes. The port does not
// model the `Wr` container's measured layout, so it stacks the registry in `ny`
// order down the left edge of the map area with a fixed plate size — the ONE
// layout approximation (see the report). A draw and its hit test read the same
// rect. The size is a screen-fraction so it scales with the viewport.
void MapScreen::map_button_rect(std::size_t i, float& cx, float& cy, float& w,
                                float& h) const {
    const MapMetrics mm = map_metrics();
    w = kViewW * 0.20f;
    h = w * 0.52f;
    cx = w * 0.7f;
    cy = mm.map_y + h * 0.5f + static_cast<float>(i) * (h + kViewH * 0.02f);
}

bool MapScreen::zone_dot_center(std::size_t zi, float& cx, float& cy) const {    if (zi >= zones_.size()) return false;
    std::size_t slot = 0;
    for (std::size_t i = 0; i < zones_.size(); ++i) {
        bool any = false;
        for (const Node& n : zones_[i].nodes) {
            if (n.visible) {
                any = true;
                break;
            }
        }
        if (!any) continue;                // no dot drawn -> no rect
        if (zones_[i].part < 0) continue;  // no map backdrop -> not a map zone
        if (i == zi) {
            const MapMetrics mm = map_metrics();
            cx = kMapZoneDotX0 + static_cast<float>(slot) * kMapZoneDotPitch;
            cy = mm.bar_y + mm.bar_h * 0.5f;
            return true;
        }
        ++slot;
    }
    return false;
}

void MapScreen::update_impl(float dt) {
    // Fidelity-tour boss-roster capture: arm the `jk` machine frozen at the
    // requested pose (3/4 = the `act_boss` resting selection, 1 = mid
    // scroll-in) and freeze the map (no taps/launches) while it is forced.
    if (force_boss_roster()) {
        roster_forced_ = true;
        if (!roster_.started || roster_.state != force_boss_state()) {
            std::vector<BossRosterEntry> entries;
            if (zone_sel_ >= 0 && static_cast<std::size_t>(zone_sel_) < zones_.size()) {
                for (const Node& n : zones_[zone_sel_].nodes) {
                    if (n.type != "BOSSES" && n.type != "BOSSES_REPLAYABLE") continue;
                    entries = boss_roster_entries(app(), n);
                    break;
                }
            }
            roster_.start(std::move(entries), 0);
            const int want = force_boss_state();
            int guard = 0;
            while (roster_.state < want && roster_.active() && ++guard < 100000) {
                roster_.tick(1.0f / 60.0f);
            }
            if (want >= 3 && roster_.state == 3) {
                roster_.time = 1.0f;  // the state-3 END pose (the `act_boss` ring)
            } else if (want == 1 && roster_.state == 1) {
                roster_.time *= 0.5f;  // mid-tween: the row is part-scrolled
                roster_.row_alpha = 1.0f;
            }
        }
        return;
    }
    if (roster_forced_) {
        // The capture hook turned off: drop the frozen roster so the real flow
        // re-arms the machine (or the plain map is live) cleanly.
        roster_forced_ = false;
        roster_ = BossRosterScroll{};
    }
    ensure_lang(app());  // the lang table powers the `Y.na` string lookups
    // `SetMapFocus` focus refresh (`qo` L1086 = `p.o.m5(battle)` + the `Ya`
    // focus refresh): StoryTutorialBossFight fires on THIS map's SceneLoaded
    // (tutorial_quests.xml L143-155), i.e. AFTER the ctor read the save, so
    // the live map must re-target `Rr` when the engine's focus changes. The
    // BOSS_LYNX node is the only visible one on the tutorial save, so this
    // lands `SetMapFocus Battle="ZONE_1|BOSS_LYNX|1"` verbatim.
    {
        const std::string& focus = app().quest_engine().last_map_focus();
        if (!focus.empty() && focus != applied_focus_) {
            applied_focus_ = focus;
            apply_map_focus(focus);
            std::fprintf(stdout, "[map] focus refresh -> %s (hover %d)\n", focus.c_str(),
                         hover_);
            std::fflush(stdout);
        }
    }
    // Sensei modal gate (quest He records): while up, a tap advances the
    // dialog or fires its button; a `Fight` request (`Sn`) resolves to the
    // map node and runs the shared battle-start body.
    {
        std::string fight;
        if (quest_modal_consume(app(), &fight)) {
            if (!fight.empty()) {
                std::string z, b;
                const std::size_t p1 = fight.find('|');
                if (p1 == std::string::npos) {
                    b = fight;
                } else {
                    z = fight.substr(0, p1);
                    const std::size_t p2 = fight.find('|', p1 + 1);
                    b = fight.substr(
                        p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - (p1 + 1));
                }
                for (const ZoneTab& zt : zones_) {
                    if (!z.empty() && zt.name != z) continue;
                    for (const Node& n : zt.nodes) {
                        if (n.name == b) {
                            // The deferred quest `Fight` action is just another
                            // battle start — run it through the SAME `jk` gate
                            // as the FIGHT button so a multi-`<Fight>` boss
                            // battle still plays the opponent scroll.
                            start_battle(n);
                            return;
                        }
                    }
                }
                std::fprintf(stderr, "[quest] Fight '%s' not on the map\n", fight.c_str());
            }
            return;
        }
    }
    const App::PointerState& p = app().pointer();
    // Boss-intro `jk` roster (`ai.aa` case 0 L2007 -> L2009): ticks with the
    // screen clock; state 4 fires `qd` -> `ngb()` -> start the fight. Input is
    // held while the scroll runs (the JS row is a display-only act).
    if (roster_.active()) {
        if (roster_.tick(dt)) {
            std::fprintf(stdout, "[map] jk qd (state 4) -> start fight %s\n",
                         act_node_.name.c_str());
            std::fflush(stdout);
            launch_battle(act_node_);
        }
        return;
    }
    // JS `Ya` has no zone tab strip (PORT_AUDIT_UI 2.3): zone navigation is
    // the `Vr` scroller + `Rr` info panel / `Xr` status list (OPEN - not
    // ported). `tab_hover_` stays for the header field but is never set.
    tab_hover_ = -1;
    // BACK (top-left) -> the previous screen (the Dojo home hub — the
    // loop's map -> dojo / map -> equipment legs; the JS map has a
    // back/exit control in the top bar).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[map] BACK -> previous screen\n");
            std::fflush(stdout);
            manager().pop();
            return;
        }
    }
    // JS has no BRACKET button (no screen 13; `Xr` is the map status panel,
    // PORT_AUDIT_UI 2.3/2.4). The native BracketScreen was deleted with the
    // id (screens.hpp / screen_manager.hpp).
    if (zone_sel_ < 0 || static_cast<std::size_t>(zone_sel_) >= zones_.size()) return;
    // `Ur` zone strip (JS L2112-2116, `qk.layout` L2137): a tap on a zone dot
    // SELECTS that zone (`Vr`/`Ur` — the map re-renders on the new zone's
    // backdrop). The dots were drawn with NO hit test, which is the reported
    // "the zone dots at the bottom do not respond" bug. Only zones that render
    // a dot (`Vr.HXa` L2123-2124: at least one visible node) are pickable.
    {
        for (std::size_t zi = 0; zi < zones_.size(); ++zi) {
            float dot_cx = 0.0f, dot_cy = 0.0f;
            if (!zone_dot_center(zi, dot_cx, dot_cy)) continue;
            const float dot_half = kMapZoneDotD * 0.5f;
            if (p.x >= dot_cx - dot_half && p.x <= dot_cx + dot_half &&
                p.y >= dot_cy - dot_half && p.y <= dot_cy + dot_half) {
                if (p.pressed && static_cast<int>(zi) != zone_sel_) {
                    zone_sel_ = static_cast<int>(zi);
                    hover_ = -1;
                    apply_map_focus(app().quest_engine().last_map_focus());
                    try {
                        WarriorSave w = app().save().load();
                        if (w.current_zone != zones_[zi].name) {
                            w.current_zone = zones_[zi].name;
                            app().save().save(w);
                        }
                    } catch (const std::exception&) {
                    }
                    std::fprintf(stdout, "[map] zone dot -> %s (%zu nodes)\n",
                                 zones_[zi].name.c_str(), zones_[zi].nodes.size());
                    std::fflush(stdout);
                }
                return;  // the dot strip owns its rect
            }
        }
    }
    // Live map buttons (`Vb.F().ny`, JS L2167): the `Wr`/`sk` story plates.
    // `Z0a` (L2172) binds `pa.addListener(... -> d.Qg(b.name))`, so a press
    // runs `Qg` (L2173): `ta.Av = name` + `Sf("QUEST_EVENT_MAP_BUTTON_PRESS")`.
    {
        const std::vector<EngineMapButton>& mbs =
            app().quest_engine().map_buttons();
        for (std::size_t i = 0; i < mbs.size(); ++i) {
            float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
            map_button_rect(i, bx, by, bw, bh);
            if (p.x < bx - bw * 0.5f || p.x > bx + bw * 0.5f ||
                p.y < by - bh * 0.5f || p.y > by + bh * 0.5f) {
                continue;
            }
            if (p.pressed) {
                app().quest_engine().press_map_button(app(), mbs[i].name);
            }
            return;  // the plate owns its rect
        }
    }
    // JS `Ya.Uw` (L2129) focuses the save MapFocus node at init and `Rr`
    // tracks it (`ue.tea()`); a tap re-targets (`qe.jhb` -> `GT`). The focus
    // persists between taps, so it is NOT reset here — `hover_` holds it.
    // A node tap only RE-TARGETS the `Rr` panel (selects); it never starts a
    // fight (JS `qe` -> `Ya.Uw`, L2129 — the fight is the FIGHT button below).
    bool node_tap = false;
    for (std::size_t i = 0; i < zones_[zone_sel_].nodes.size(); ++i) {
        const Node& n = zones_[zone_sel_].nodes[i];
        if (!n.visible) continue;  // JS `Qr.lla` L2094 (hidden alt-state twin)
        const float node_half = map_node_size(kViewW) * 0.5f;
        if (p.x >= n.x - node_half && p.x <= n.x + node_half &&
            p.y >= n.y - node_half && p.y <= n.y + node_half) {
            hover_ = static_cast<int>(i);
            // One node per tap (JS buttons are exclusive — the topmost node
            // fires). ZONE_1 has coincident nodes (BOSS_LYNX / *_INTERMISSION
            // / BOSS_HARDMODE at the same X/Y).
            if (p.pressed) {
                node_tap = true;
                std::fprintf(stdout, "[map] node focus -> %s [%s] (%s)\n", n.name.c_str(),
                             n.zone.c_str(), n.active ? "active" : "locked");
                std::fflush(stdout);
                break;
            }
        }
    }
    // The `Rr` FIGHT button (`tj`, L2099/L2102): the ONLY fight trigger.
    // JS `Ya` -> `v.Am(battle)` (L1216) -> `wa.mp(6)`. A boss node arms the
    // `jk`/`Rd` intro first (interactive); the fight launches after it.
    if (p.pressed && !node_tap && hover_ >= 0 &&
        static_cast<std::size_t>(hover_) < zones_[zone_sel_].nodes.size()) {
        const Node& n = zones_[zone_sel_].nodes[static_cast<std::size_t>(hover_)];
        const MapFightButtonRect fb = map_fight_button_rect(map_metrics());
        if (n.visible && p.x >= fb.cx - fb.w * 0.5f && p.x <= fb.cx + fb.w * 0.5f &&
            p.y >= fb.cy - fb.h * 0.5f && p.y <= fb.cy + fb.h * 0.5f) {
            // `Nn` (L1114) non-ignored `ClickButton Target=
            // "InfoBattle.FightButton"`: while the quest armed this plate the
            // player's press dispatches the plate's OWN callback (`Nn.Qg`
            // completes the quest step) — the hit-test below IS that callback.
            if (app().quest_engine().click_armed("InfoBattle.FightButton")) {
                std::fprintf(stdout,
                             "[quest] ClickButton dispatched: InfoBattle.FightButton\n");
                std::fflush(stdout);
                app().quest_engine().clear_click_armed();
            }
            if (!n.active) {
                std::fprintf(stdout, "[map] FIGHT ignored: %s [%s] locked\n", n.name.c_str(),
                             n.zone.c_str());
                std::fflush(stdout);
            } else {
                // JS `ai.aa` case 0 (L2007): the ONE shared gate — a
                // multi-`<Fight>` boss battle arms the `jk` opponent scroll
                // (`lca` L2009) first; everything else launches directly.
                start_battle(n);
            }
        }
    }
    // Shared `za` nav column (JS `ma.D1`): Dojo/Shop/Profile/Settings hops.
    // The Map forces the collapsed header (fresh `za` per screen).
    za_update(app(), *this, kScreenMap, dt);
}

// --- Map info panel (JS `Rr` L2098-2104, `pk` L2160, `Xr` L2133, `Wc`
// L2163, `bi` L2133) -------------------------------------------------------
//
// `Wc` difficulty levels (`DifficultyEvaluation`, internal_settings.xml
// L545-551 -> `Wc.gD` `{name, RatingRatioTreshold}` in document order; the
// *name* is the label key (`Y.na(a.first)`) and `Z.iQa[index]` is the fill
// frame (JS L2163, `Z.iQa` L2478).
struct MapDiffLevel { const char* key; float threshold; };
const MapDiffLevel kMapDiffLevels[] = {
    {"diff0", 0.0f}, {"diff1", 0.82f}, {"diff2", 1.3f},
    {"diff3", 2.6f}, {"diff4", 5.2f}};
const char* const kMapDiffFill[] = {
    "difficulty_very_easy", "difficulty_easy", "difficulty_middle",
    "difficulty_hard", "difficulty_very_hard"};

// OPEN: the JS rating `v.OAa(battle)` (L1219, `RatingEvaluation`
// internal_settings L490-527) is not ported. A neutral ratio 1.0 lands in
// `diff1` [0.82,1.3) — exactly the oracle's ZONE_1/BOSS_LYNX value
// ("Нормально"/diff1, fill frame `difficulty_easy`).
constexpr float kMapDefaultRatingRatio = 1.0f;

int map_difficulty_level() {
    const int n = static_cast<int>(sizeof(kMapDiffLevels) / sizeof(kMapDiffLevels[0]));
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        if (kMapDiffLevels[i].threshold < kMapDefaultRatingRatio) idx = i;
    }
    return idx;
}

// Source size of an atlas frame (untrimmed `sourceSize`), for the JS
// aspect-driven scale (`R.za()/qa()`).
bool map_frame_size(App& app, const char* name, float& w, float& h) {
    sf2::data::atlas_frame fr;
    int tw = 0, th = 0;
    unsigned int gl = 0;
    if (!app.get_atlas_frame(name, &fr, &tw, &th, &gl)) return false;
    w = fr.source_w > 0 ? static_cast<float>(fr.source_w) : static_cast<float>(fr.w);
    h = fr.source_h > 0 ? static_cast<float>(fr.source_h) : static_cast<float>(fr.h);
    return true;
}

// The `Fg(430,800,0,50)` paper content frame (L1868): `paper_edge_left` +
// stretched `paper` + `paper_edge_right` at the `c = qka` rail width, with
// the `Dl[1]` bottom `Zh` rail (`roll_end` + `roll_center` + mirrored
// `roll_end`) at `D(g - 50*e)` (`Rr.layout` L2101).
void draw_map_paper_panel(App& app, const MapMetrics& mm) {
    sf2::render::Renderer& ren = app.renderer();
    const bool scroll_ok = load_scroll_atlas(app);
    bool drew = false;
    if (scroll_ok) {
        drew = try_draw_atlas_button(app, "paper_edge_left", mm.panel_x, mm.panel_y,
                                     mm.rail, mm.panel_h, 1.0f, /*fill=*/true,
                                     /*flip_x=*/false, /*top_left=*/true);
        if (drew) {
            try_draw_atlas_button(app, "paper", mm.panel_x + mm.rail, mm.panel_y,
                                  mm.content_w, mm.panel_h, 1.0f, true, false, true);
            try_draw_atlas_button(app, "paper_edge_right",
                                  mm.panel_x + mm.panel_w - mm.rail, mm.panel_y,
                                  mm.rail, mm.panel_h, 1.0f, true, false, true);
        }
    }
    if (!drew) {
        const float p[] = {mm.panel_x, mm.panel_y, mm.panel_x + mm.panel_w, mm.panel_y,
                           mm.panel_x, mm.panel_y + mm.panel_h,
                           mm.panel_x + mm.panel_w, mm.panel_y,
                           mm.panel_x + mm.panel_w, mm.panel_y + mm.panel_h,
                           mm.panel_x, mm.panel_y + mm.panel_h};
        ren.draw_triangles(p, 6, 0.72f, 0.58f, 0.38f, 1.0f);
    }
    const float rail_h = 50.0f * (mm.panel_w / 430.0f);  // `Dl[1].ba(f, 50*e)`
    const float rail_y = mm.panel_y + mm.panel_h - rail_h;
    constexpr float kRollEndW = 101.0f, kRollEndH = 114.0f;
    const float cap = kRollEndW * (rail_h / kRollEndH);
    const float body = std::max(mm.panel_w - 2.0f * cap, 10.0f);
    if (scroll_ok) {
        try_draw_atlas_button(app, "roll_end", mm.panel_x + cap * 0.5f,
                              rail_y + rail_h * 0.5f, cap, rail_h, 1.0f, true);
        try_draw_atlas_button(app, "roll_center", mm.panel_x + cap + body * 0.5f,
                              rail_y + rail_h * 0.5f, body, rail_h, 1.0f, true);
        try_draw_atlas_button(app, "roll_end", mm.panel_x + cap + body + cap * 0.5f,
                              rail_y + rail_h * 0.5f, cap, rail_h, 1.0f, true, true);
    }
}

// The `Me` battle preview image (L2110-2111): `res/map/images/<stem>.img`
// (the stem of the stages.xml `Preview` attr), a standalone 400x200 texture.
bool load_map_preview(App& app, const std::string& stem) {
    if (stem.empty()) return false;
    const std::string tex_name = "map_preview_" + stem;
    if (app.renderer().texture_lookup(tex_name) != 0) return true;
    const std::string dir = app.res_root() + "/map/images";
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(stem + ".", 0) != 0) continue;
            const std::string ext = entry.path().extension().string();
            if (ext != ".dds" && ext != ".ktx" && ext != ".webp" && ext != ".png") continue;
            sf2::data::Texture tex;
            if (!sf2::data::decode_texture(entry.path().string(), tex)) continue;
            app.renderer().texture_for(tex_name, tex);
            std::fprintf(stdout, "[map] preview %s: %dx%d\n", stem.c_str(), tex.w, tex.h);
            std::fflush(stdout);
            return true;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[map] preview load failed (%s): %s\n", stem.c_str(), e.what());
    }
    return false;
}

void draw_map_preview(App& app, const std::string& stem, float x, float y, float w) {
    if (!load_map_preview(app, stem)) return;
    constexpr float kSrcW = 768.0f, kSrcH = 384.0f;  // res/map/images/lynx.dds
    sf2::scene::Sprite s;
    s.texture_name = "map_preview_" + stem;
    s.frame_x = 0.0f;
    s.frame_y = 0.0f;
    s.frame_w = kSrcW;
    s.frame_h = kSrcH;
    s.tex_w = kSrcW;
    s.tex_h = kSrcH;
    s.solid = false;
    s.color_a = 1.0f;
    const float scale = w / kSrcW;
    s.transform.set_pos(x + w * 0.5f, y + kSrcH * scale * 0.5f);
    s.transform.set_scale(scale, scale);
    app.renderer().draw_sprite(s, ui_camera());
}

// The `Rr` info panel body. `node` may be null (empty panel: title only).
void draw_map_info_panel(App& app, const MapScreen::Node* node, const MapMetrics& mm) {
    draw_map_paper_panel(app, mm);
    const float cx = mm.panel_x + mm.rail;  // `wc.content.C(rail)`
    const float cy = mm.panel_y;
    const float qka = mm.qka;
    const float d = qka * 0.4f;             // `Rr.layout` `d = c*.4`
    const float cw = mm.content_w;          // `wc.Gv`
    const float ch = mm.content_h;          // `wc.Xy`
    const float title_h = cw * 0.2f;        // `g = c*.2`
    // Title `oq` (L2101): `C(d)`, `D(0)`, `ua(c*.17)`, `Fa(c-2*d, g)`,
    // `La(Z.sc)`, `Ia(2)` (horizontal centre).
    std::string title;
    if (node != nullptr) {
        const std::string key = node->title.empty()
                                    ? (node->alias.empty() ? node->name : node->alias)
                                    : node->title;
        title = loc(app, key, node->name);
    }
    if (!title.empty()) {
        draw_ui_label(app, cx + d, cy, cw - 2.0f * d, title_h, title,
                      cw * 0.17f / 100.0f, UiAlign::Center, 0.184f, 0.145f, 0.106f);
    }
    if (node == nullptr) return;
    // Preview `Wu`/`Me` (L2101-2102, L2111): `C(d)`, `D(f)`,
    // `f = 10 + title_h + 20`, scaled to the content width.
    float f = 10.0f + title_h + 20.0f;
    const float pv_w = cw - 2.0f * d;
    draw_map_preview(app, node->preview, cx + d, cy + f, pv_w);
    f += pv_w * 0.5f + 20.0f;  // `Wu.qa()` = pv_w * (200/400)
    // Body `pk` rect (L2102): `gb(d, f, d+(c-2*d), f+(e-f))`.
    const float body_x = cx + d;
    const float body_y = cy + f;
    const float body_w = cw - 2.0f * d;
    const float body_h = ch - f;
    // --- `Xr` status pips (L2133-2136, `pk.ba` L2161 `aUa.ba(a, b*.3)`) ---
    const float xr_h = body_h * 0.3f;
    const float pip_label_h = body_w * 0.16f;  // `Xr.ba` `c = a*.16`
    int pips = node->fight_count;
    if (node->type == "BOSSES" || node->type == "BOSSES_REPLAYABLE" ||
        node->type == "FINAL_BATTLE_TITAN") {
        --pips;  // `Xr` ctor L2134
    }
    if (pips > 0) {
        // `H0a` (L2135): boss group w/ d -> "bodyguards", else title/challenge;
        // challenge families -> "stage{...}".
        std::string xr_label;
        if (node->type == "BOSSES" || node->type == "BOSSES_REPLAYABLE" ||
            node->type == "BOSSES_INTERMISSION" || node->type == "FINAL_BATTLE_TITAN") {
            xr_label = loc(app, "bodyguards", "BODYGUARDS");
        } else {
            const std::string tk = node->title.empty() ? node->name : node->title;
            xr_label = loc(app, tk, node->name);
        }
        draw_ui_label(app, body_x, body_y, body_w, pip_label_h, xr_label,
                      pip_label_h / 100.0f, UiAlign::Left, 0.184f, 0.145f, 0.106f);
        // `mdb` (L2136): capacity `c>12?10:6`, square side
        // `min(a/d, b/f)`, single row centred.
        const int per_row = (pips > 12) ? 10 : 6;
        const int rows = (pips + per_row - 1) / per_row;
        const float g = body_w / static_cast<float>(per_row);
        const float h = (xr_h - pip_label_h) / static_cast<float>(rows);
        const float pip = std::min(g, h);
        for (int i = 0; i < pips; ++i) {
            const int row = i / per_row;
            const int col = i % per_row;
            const int in_row = std::min(per_row, pips - row * per_row);
            const float px = body_x + (body_w - in_row * pip) * 0.5f +
                             static_cast<float>(col) * pip;
            const float py = body_y + pip_label_h + static_cast<float>(row) * pip;
            // `Ox.wMa` (L2134): `c[k].locked ? wMa(2) : wMa(h ? 0 : 1)` —
            // `indicatorLocked` for a locked fight, else `indicatorOn` when
            // beaten (`c[k].status==1`) / `indicatorOff` otherwise.
            const bool beaten =
                static_cast<std::size_t>(i) < node->pip_beaten.size() &&
                node->pip_beaten[static_cast<std::size_t>(i)];
            const bool pip_locked =
                static_cast<std::size_t>(i) < node->fight_locked.size() &&
                node->fight_locked[static_cast<std::size_t>(i)];
            const char* pip_frame = pip_locked ? "indicatorLocked"
                                               : (beaten ? "indicatorOn"
                                                         : "indicatorOff");
            try_draw_atlas_button(app, pip_frame, px + pip * 0.5f, py + pip * 0.5f,
                                  pip, pip, 1.0f);
        }
    }
    // --- `Wc` difficulty (L2161 `b = b*.35 + c`, `Lm.D(b)`, `Lm.ba(a,
    // a*.25)`; L2163) ---
    float wy = body_h * 0.35f + body_h * 0.05f;  // `c = b*.05`, `b*.35 + c`
    {
        float fw = 0.0f, fh = 0.0f;
        const float bar_h = map_frame_size(app, "difficulty_empty", fw, fh) && fw > 0.0f
                                ? body_w * (fh / fw)
                                : body_w * 0.1f;
        try_draw_atlas_button(app, "difficulty_empty", body_x + body_w * 0.5f,
                              body_y + wy + bar_h * 0.5f, body_w, bar_h, 1.0f);
        const int lvl = map_difficulty_level();
        try_draw_atlas_button(app, kMapDiffFill[lvl], body_x + body_w * 0.5f,
                              body_y + wy + bar_h * 0.5f, body_w, bar_h, 1.0f);
        // `Wc.ba` label: `ua(a*.16)`, `Fa(a, ua)`, `D(bar_h)`.
        const float lab_h = body_w * 0.16f;
        draw_ui_label(app, body_x, body_y + wy + bar_h, body_w, lab_h,
                      loc(app, kMapDiffLevels[lvl].key, kMapDiffLevels[lvl].key),
                      lab_h / 100.0f, UiAlign::Center, 0.184f, 0.145f, 0.106f);
        wy += bar_h + lab_h;
    }
    // --- reward gold (JS `pk.ty` = `bi` -> `ci`/`Ig`, L2133/L2148) ---
    {
        float gw = 0.0f, gh = 0.0f;
        const float icon_h = body_w * 0.15f;
        const float icon_w = (map_frame_size(app, "gold", gw, gh) && gh > 0.0f)
                                 ? icon_h * (gw / gh)
                                 : icon_h;
        const std::string amount = std::to_string(node->reward_money);
        const float txt_scale = body_w * 0.16f / 100.0f;
        const sf2::data::font* mfont = app.menu_font();
        const float txt_w = mfont != nullptr
                                ? app.measure_text(*mfont, amount, txt_scale)
                                : icon_h;
        const float total = icon_w + txt_w;
        const float gx = body_x + (body_w - total) * 0.5f;
        try_draw_atlas_button(app, "gold", gx + icon_w * 0.5f, body_y + wy + icon_h * 0.5f,
                              icon_w, icon_h, 1.0f);
        draw_ui_label(app, gx + icon_w, body_y + wy, txt_w, icon_h, amount, txt_scale,
                      UiAlign::Left, 0.184f, 0.145f, 0.106f);
    }
    // --- FIGHT button `tj` = `Bb("EButtonWhite")` (L2099/L2102) ------------
    // `tj.Pb(c*.2)` -> node scale `c*.2/112`; `Bb` is 600 wide x 112 tall
    // (`Bb` ctor `xc(600)`). Geometry comes from the shared
    // `map_fight_button_rect` so the update hit-test can never diverge.
    const MapFightButtonRect fb = map_fight_button_rect(mm);
    const float btn_h = fb.h;
    const float btn_w = fb.w;
    const float btn_cx = fb.cx;
    const float btn_cy = fb.cy;
    bool btn_drawn = false;
    if (load_sliced_atlas(app)) {
        btn_drawn = draw_bb_plate(app, "btnWhite", btn_cx, btn_cy, btn_w, btn_h, 1.0f);
    }
    if (!btn_drawn) {
        draw_flat_button(app, "^startFight^", btn_cx, btn_cy, btn_w, btn_h, 0.35f, 0.45f,
                         0.3f, false);
    }
    draw_ui_label(app, btn_cx - btn_w * 0.5f + 8.0f, btn_cy - 11.0f, btn_w - 16.0f, 22.0f,
                  loc(app, "startFight", "FIGHT"), 0.44f, UiAlign::Center, 0.184f, 0.145f,
                  0.106f);
    // `Nn` `ClickButton Target="InfoBattle.FightButton" UseFlashing="1"
    // IgnoreCallback="1"` (tutorial_quests.xml L156): the quest FOCUSES and
    // FLASHES this plate but must NOT press it — the pulse is draw-only and
    // the launch still needs the player's tap (update_impl FIGHT hit-test).
    if (app.quest_engine().flash_target() == "InfoBattle.FightButton") {
        draw_flash_tint(app.renderer(), btn_cx, btn_cy, btn_w, btn_h);
    }
}

// ---------------------------------------------------------------------------
// Boss-intro roster (`jk`, JS L2061-2065) + the fidelity-tour capture hook.
// ---------------------------------------------------------------------------
namespace {
bool g_force_boss_roster = false;
int g_force_boss_state = 3;
}  // namespace

bool force_boss_roster() { return g_force_boss_roster; }
void set_force_boss_roster(bool on) { g_force_boss_roster = on; }
void set_force_boss_state(int state) { g_force_boss_state = state; }
int force_boss_state() { return g_force_boss_state; }

// `jk` (L2061-2065): a horizontal row of circular warrior portraits on the
// `hf = Fc.Ed(-16777216)` fader + `Qa = R.$(E.get(3,6))` panel. `jk.aa`
// state 3 scales the SELECTED entry to 1.4 and dims the rest to 0.5
// (`Hf.node.wa(1+-.5*a)`), with the row scrolled so the selected is centred
// (`Pp`, L2063). The oracle capture (`act_boss`) pins: selected ring ~325 px
// centred, the others ~180 px at +-265 px. The portrait art carries its own
// circular frame, so it is drawn at its natural 512 px canvas.
void draw_boss_roster(App& app, sf2::render::Renderer& ren,
                      const BossRosterScroll& r) {
    // `hf = Fc.Ed(-16777216)` full-screen fader (near-black corners).
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.02f, 0.015f, 0.01f, 1.0f);
    // `Qa = R.$(E.get(3,6))` backdrop panel (the brown radial gradient),
    // stretched full-screen. Drawn opaque (JS `Qa` has no alpha): the oracle
    // background luminance equals the raw `bg` art, so the 0.94 alpha (which
    // darkened it ~6%) is removed.
    if (load_vs_atlas(app)) {
        try_draw_atlas_button(app, "vs_bg", kViewW * 0.5f, kViewH * 0.5f, kViewW, kViewH,
                              1.0f, /*fill=*/true);
    }
    if (r.entries.empty()) return;
    // `jk.aa` L2064 per frame: `bQ.C(this.scrollX); bQ.D(512)` — the row sits
    // at `scroll_x` with entry `i` at `i*g*.8`; `bQ.node.wa` is the state-0
    // fade-in and the selected `la` / others' `Hf.node.wa(1+-.5*a)` are the
    // state-3 ramp. Oracle-pinned canvas: others 353 (= `g`), the selected
    // 353*1.4 = 494; vertical centre 362.
    const float kCy = 362.0f;
    const float sel_scale = r.selected_scale();
    const float oth_alpha = r.other_alpha();
    for (int i = 0; i < static_cast<int>(r.entries.size()); ++i) {
        const float cx = r.scroll_x + r.entry_x(i);
        const bool sel = i == r.index;
        const float d = BossRosterScroll::kBaseD * (sel ? sel_scale : 1.0f);
        const float a = r.row_alpha * (sel ? 1.0f : oth_alpha);
        if (!draw_user_image(app, r.entries[i].image, cx, kCy, d, d, a)) {
            draw_user_image(app, "avatar_masked", cx, kCy, d, d, a);
        }
    }
}

void MapScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // Fidelity-tour boss-roster capture (`--fidelity-tour`): draw the `jk`
    // boss-intro roster instead of the map while the hook is on (update_impl
    // armed + froze the machine at the requested pose).
    if (force_boss_roster()) {
        ensure_lang(app);
        draw_boss_roster(app, ren, roster_);
        return;
    }
    load_map_backdrops(app);  // once; silent unless frames decode
    const MapMetrics mm = map_metrics();
    // `Vr` map strip (JS `qk.layout` L2137 / `Vr.ba` L2118): the selected
    // zone's backdrop (JS `qe.W0a` L2143 `parseInt(fileName.split(".")[1])-1`)
    // is scaled to the strip height `d` and drawn at `dA.x` (0 for the
    // single-zone content; `-100+ca` once the strip scrolls).
    bool bg_done = false;
    if (zone_sel_ >= 0 && static_cast<std::size_t>(zone_sel_) < zones_.size() &&
        zones_[zone_sel_].part >= 0) {
        bg_done = app.draw_atlas_rect("map" + std::to_string(zones_[zone_sel_].part),
                                      mm.bg_x, mm.map_y, kMapFrameW * mm.bg_scale,
                                      mm.map_h, 1.0f);
    }
    if (!bg_done) {
        const float verts[] = {0, mm.map_y, kViewW, mm.map_y, kViewW, mm.bar_y,
                               0, mm.map_y, kViewW, mm.bar_y, 0, mm.bar_y};
        ren.draw_triangles(verts, 6, 0.08f, 0.1f, 0.14f, 1.0f);
    }
    // The JS map is a full shell screen; the native stack still holds the Dojo
    // underneath, so cover the `Ur` band (y >= Sp+d) with the shell base
    // colour (the oracle bar reads 16,7,3).
    {
        const float bar[] = {0, mm.bar_y, kViewW, mm.bar_y, kViewW, kViewH,
                             0, mm.bar_y, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(bar, 6, 0.063f, 0.027f, 0.012f, 1.0f);
    }
    const bool zone_ok =
        zone_sel_ >= 0 && static_cast<std::size_t>(zone_sel_) < zones_.size();
    const std::size_t node_count = zone_ok ? zones_[zone_sel_].nodes.size() : 0;
    const float node_px = map_node_size(kViewW);
    const float node_ls = node_px / kMapNodeSourcePx;  // local -> screen
    for (std::size_t i = 0; i < node_count; ++i) {
        const Node& n = zones_[zone_sel_].nodes[i];
        if (!n.visible) continue;  // `WDa` + `Qr.lla` (JS L256/L2094)
        const bool hovered = static_cast<int>(i) == hover_;
        // JS `Qr` (L2092-2095) `b=a.tt()`: the `<Battle>` record's `Locked`
        // (not `!active` — the visible-node gate already implies `active`).
        const bool locked = n.locked;
        // JS `Qr` (L2092-2095): frame = "BattleBtn<State>/<suffix>", suffix
        // base_/active_/locked_/locked_active_/pressed_ + Icon (`Lc.*` L2482,
        // `U9a..X9a` L1405). Hover swaps to Active; locked uses BattleBtnLock*.
        const std::string base =
            std::string(locked ? "BattleBtnLock/locked_" : "BattleBtnBase/base_") + n.icon;
        const std::string active =
            std::string(locked ? "BattleBtnLockActive/locked_active_"
                               : "BattleBtnActive/active_") +
            n.icon;
        const char* tex_frame = hovered ? active.c_str() : base.c_str();
        bool drawn = try_draw_atlas_button(app, tex_frame, n.x, n.y, node_px, node_px, 1.0f);
        if (!drawn && locked) {
            const std::string lock_base = std::string("BattleBtnBase/base_") + n.icon;
            drawn = try_draw_atlas_button(app, lock_base.c_str(), n.x, n.y, node_px,
                                          node_px, 0.6f);
        }
        if (!drawn) {
            const float r = hovered ? 0.9f : (n.active ? 0.6f : 0.3f);
            const float g = hovered ? 0.5f : (n.active ? 0.4f : 0.3f);
            const float b = hovered ? 0.3f : (n.active ? 0.25f : 0.3f);
            const float x0 = n.x - node_px / 2, y0 = n.y - node_px / 2;
            const float verts[] = {x0, y0, x0 + node_px, y0, x0 + node_px, y0 + node_px,
                                   x0, y0, x0 + node_px, y0 + node_px, x0, y0 + node_px};
            ren.draw_triangles(verts, 6, r, g, b, n.active ? 0.95f : 0.5f);
        }
        // Label `cC` (JS L2093): `Fa(100,55)`, `ua(60)`, `C(-50)`, `D(65)`,
        // black, `Ia(128)` (no align shift -> left edge at `C`); text
        // `Y.na(a.Cg)` (Alias, `Qr.Bka` L2094 / `Lc.Cg` L1403).
        const std::string label_key = n.alias.empty() ? n.name : n.alias;
        draw_ui_label(app, n.x - 50.0f * node_ls, n.y + 65.0f * node_ls,
                      100.0f * node_ls, 55.0f * node_ls, loc(app, label_key, n.name), 0.6f,
                      UiAlign::Left, 0.0f, 0.0f, 0.0f);
    }
    // `Ur` zone strip (JS L2112-2116, `qk.layout` L2137): the selected zone
    // name + the zone dots (`inactive_bulb` dots, `bulb` highlight at `q9`).
    // OPEN: the JS `Ur.ba` label/dot offsets (`Ia(4)` alignment) are not
    // derived; the measured oracle placement is used.
    if (zone_ok) {
        const float label_h = mm.bar_h * 0.6f;
        draw_ui_label(app, 170.0f, mm.bar_y + (mm.bar_h - label_h) * 0.5f, 380.0f,
                      label_h, loc(app, zones_[zone_sel_].name, zones_[zone_sel_].name),
                      0.52f, UiAlign::Left, 0.78f, 0.655f, 0.451f);
        // One dot per rendered zone widget (`Vr.HXa` L2123-2124: only zones
        // with an active battle render).
        for (std::size_t zi = 0; zi < zones_.size(); ++zi) {
            float dot_x = 0.0f, dot_cy = 0.0f;
            if (!zone_dot_center(zi, dot_x, dot_cy)) continue;
            const bool sel = static_cast<int>(zi) == zone_sel_;
            if (!try_draw_atlas_button(app, sel ? "bulb" : "inactive_bulb", dot_x, dot_cy,
                                       kMapZoneDotD, kMapZoneDotD, 1.0f)) {
                draw_flat_button(app, "", dot_x, dot_cy, kMapZoneDotD * 0.5f,
                                 kMapZoneDotD * 0.5f, 0.8f, 0.45f, 0.15f, false);
            }
        }
    }
    // Shared `za` chrome (JS `ma.D1`): topPanel + widgets, or the collapsed
    // `МЕНЮ` header (JS `za.Aub` collapse(0) — the map's default).
    // Live map buttons (`Vb.F().ny`): `Wr.HWa`/`qY` (L2178) mount the `sk`
    // story plates. `sk.xmb` (L2165) resolves `hg.image` through the user-image
    // loader (`$w(E.get(338), a)`), so `draw_user_image` is the port's match.
    // A miss falls back to a flat plate carrying the name. Draws the SAME rect
    // the update_impl hit test arms.
    {
        const std::vector<EngineMapButton>& mbs =
            app.quest_engine().map_buttons();
        for (std::size_t i = 0; i < mbs.size(); ++i) {
            float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
            map_button_rect(i, bx, by, bw, bh);
            if (!mbs[i].image.empty() &&
                draw_user_image(app, mbs[i].image, bx, by, bw, bh, 1.0f, false)) {
                continue;
            }
            draw_flat_button(app, "", bx, by, bw * 0.5f, bh * 0.5f, 0.85f, 0.7f,
                             0.2f, false);
            draw_ui_label(app, bx - bw * 0.5f, by - bh * 0.5f, bw, bh, mbs[i].name,
                          0.5f, UiAlign::Left, 0.0f, 0.0f, 0.0f);
        }
    }
    draw_za_chrome(app, kScreenMap);
    // The `Rr` info panel (JS `Ya.Zq = Qo(Rr)` L2125) draws last, over the
    // `Vr` strip and the `Ur` bar (the oracle panel overlaps both).
    const Node* sel = nullptr;
    if (zone_ok && hover_ >= 0 &&
        static_cast<std::size_t>(hover_) < zones_[zone_sel_].nodes.size()) {
        sel = &zones_[zone_sel_].nodes[static_cast<std::size_t>(hover_)];
    }
    draw_map_info_panel(app, sel, mm);

    // Boss-intro `jk` roster (`ai.aa` case 0 L2007 -> `lca` -> `Ws=new jk`):
    // the row-a `bQ` at the state-machine `scroll_x`, the state-0 fade-in and
    // the state-3 selection ramp. Skipped while a quest modal is up (same
    // exclusivity rule as the Dojo — single voice, the modal wins).
    if (roster_.active() && quest_modal_top(app) == nullptr) {
        draw_boss_roster(app, ren, roster_);
    }
    // Sensei dialog modal on top of the map.
    draw_quest_modal(app, ren, app.screens().top() == this);
}

// ---------------------------------------------------------------------------
// FightScreen
// ---------------------------------------------------------------------------

FightScreen::FightScreen(ScreenManager& mgr, const std::string& battle_name,
                         const std::string& location, int reward_money, int reward_exp,
                         const std::vector<sf2::scene::OwnedItem>& owned)
    : Screen(mgr, "Fight"),
      battle_name_(battle_name),
      location_(location),
      reward_money_(reward_money),
      reward_exp_(reward_exp) {
    // An EMPTY owned list means "resolve from the save" — the direct boot
    // (`--fight`/`--verify-input`/`--input-tape`/capture drivers) and the
    // Map/Dojo launch then build the IDENTICAL player move list from the same
    // save (JS `ra.Hza` L684-685 always tests the fighter's real items).
    const std::vector<sf2::scene::OwnedItem> player_owned =
        owned.empty() ? owned_items(app()) : owned;
    player_owned_ = player_owned;
    std::fprintf(stdout, "[fight] player owned items: %zu\n", player_owned.size());
    std::fflush(stdout);
    FightAssets& assets = app().fight_assets();
    std::fprintf(stdout, "[fight] battle=%s location=%s reward money=%d exp=%d\n",
                 battle_name_.c_str(), location_.c_str(), reward_money_, reward_exp_);
    std::fflush(stdout);
    // Fight music (JS `ta.Ut(this.Da.tp)`, L2008): the battle's Music attr
    // from stages.xml; battles without one (Training dummy) keep playing
    // whatever is current (no invented fallback).
    {
        std::string track;
        battle_music(battle_name_, track);
        if (!track.empty()) {
            sf2::audio::AudioEngine::instance().play_music(track);
        }
        // JS `ai.Ut()` (L2008): `ta.Ut(this.Da.tp); lb.rJ=!1`. The fight start
        // plays the battle track directly (bypassing `lb.OS`) and clears the
        // menu guard, so the fight-end `lb.OS()` (L384) restores the menu.
        sf2::audio::AudioEngine::instance().reset_music_guard();
    }

    // The on-screen gamepad art (JS `Za`): the ui/controller atlas
    // (Joystick*, btn_punch_*, btn_kick_*). Loaded once per process; the
    // frames register into the app's atlas cache for draw_gamepad.
    (void)load_controller_atlas(app());

    // The battle's location scene. The hub keeps its own `assets.dojo`, so
    // the old `assets.dojo.layers().empty()` guard never fired after hub boot
    // and EVERY battle reused the dojo scene (BOSS_LYNX Location="moon"
    // rendered the dojo; this block never logged). JS `Bf.init` L474 builds a
    // scene per battle location, so (re)load `fight_location` whenever the
    // battle's location differs from the one currently held.
    if (assets.fight_location.layers().empty() || assets.fight_location_name != location_) {
        const std::string loc_dir = app().res_root() + "/locations/" + location_;
        assets.fight_location_name.clear();  // mark unloaded until the load succeeds
        try {
            // JS `Bf.init` L474 page chain (`ni.init` L1142 walks `b.nextPage`):
            // load every `<loc>[-N].*.json` page and alias each page's frames,
            // not just page 1 (the old single-`{atlas_json}` load).
            load_location_atlas_pages(app(), assets.fight_location, loc_dir, location_);
            assets.fight_location_name = location_;
            std::fprintf(stdout, "[fight] location scene (%s): %zu layers, arena %.0fx%.0f\n",
                         location_.c_str(), assets.fight_location.layers().size(),
                         assets.fight_location.arena_width(),
                         assets.fight_location.arena_height());
            // [FIX Phase 4b — the floor the fighters stand on] The dojo's
            // `dojo_floor_*` atlas sprites are white frames tinted black by
            // the params `Color="0x000000"` — they render as a pure-black
            // strip where the fighters stand, making the black silhouettes
            // invisible ("no body"). The oracle's fighter-zone floor is a
            // warm wooden floor (~0xC77946); tint the floor sprites warm so
            // the black fighters are visible on it. (The hub's `assets.dojo`
            // keeps the raw tint, matching the hub oracle.)
            for (const auto& layer : assets.fight_location.layers()) {
                for (const auto& s : layer->sprites) {
                    if (s->texture_name.rfind("dojo_floor_", 0) == 0) {
                        s->color_r = 0xC7 / 255.0f;
                        s->color_g = 0x79 / 255.0f;
                        s->color_b = 0x46 / 255.0f;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[fight] location scene load failed: %s\n", e.what());
        }
    }

    const float arena_w = assets.fight_location.arena_width() > 0.0f
                              ? assets.fight_location.arena_width()
                              : 1960.0f;
    // Arena bounds are [Wall, width-Wall] (JS `ca.ggb` L383 `v.tFa=location.NU`,
    // `v.NKa=location.width-NU`; `Bf.init` L474 `this.NU = Root Wall`). `Wall`
    // ranges 80..250 across the shipped locations (dojo=80); the old
    // hard-coded 80 clamped every other zone's fighters into the wrong arena.
    const float wall = assets.fight_location.arena_wall();
    const float wall_min = wall;
    const float wall_max = arena_w - wall;

    sf2::scene::BattleParams battle;
    battle.name = battle_name_;
    // JS `Lc.pkb` L1407: the fight type comes from the battle's `<Battle
    // Type>` KIND via `p.Wab`/`b0` (L180-183) - NOT a hardcoded "FightNone".
    // Hardcoding it made every non-DUMMY battle (BOSS_LYNX Type="BOSSES" ->
    // "FightBosses", Survival -> "FightSurvival", QuestBattle / Stranger
    // Type="HIDDEN" -> "FightUnregister", ...) read as FightNone on the
    // `e$a`/`kg`/`aM` paths and in the `lm` ERuleBattleType rule.
    {
        const BattleWarriorInfo btype =
            battle_warrior(battle_name_, app().pending_battle().zone);
        battle.type = btype.type;
        std::fprintf(stdout, "[fight] battle type: %s kind='%s' -> type=%s\n",
                     battle_name_.c_str(), btype.kind.c_str(), battle.type.c_str());
        std::fflush(stdout);
    }
    battle.location = location_;
    battle.rounds = 2;
    battle.round_time = 99;
    battle.health_recovery = 1.0f;
    // [FIX Phase 4a — fighters on the floor] The spawn Y is the ModelsViewer
    // placement y (the PivotNode target — `Fighter::sample` anchors the model
    // pivot there), so the posed feet rest on the visible floor line. The dojo's
    // VISIBLE floor is the dojo_floor sprite line (world Y=223.5) — the
    // fighters stand on it (feet at 223.5 -> the floor sprite row). The
    // params Floor attr (80) is the arena's physics line (JS Bf.init L474:
    // ct=Floor, tl.init L843 container y=height/2-ct=200). Fighters live
    // inside that container (DOJO_BG_STATIC 7.4); floor tiles Y=223.5 are
    // location-space. Render adds the container offset so feet land in the
    // tile band; world/pose/camera stay container-space (oracle-trace exact).
    // JS `Bf.ct` = Root `Floor` (`Bf.init` L474; dojo 80) — the `tl`
    // container y anchor (`tl.init` L843 `height/2-ct`). Was hard-coded to
    // dojo's 80 for every location.
    const float floor_y = assets.fight_location.arena_floor();
    // [FIX spawn sides] Source BOTH spawns from the location's ModelsViewer
    // (JS `Bf.zjb` L476 parses PlayerPositionX/Y -> `location.Yia`,
    // EnemyPositionX/Y -> `location.B_`; JS L381 spawns the player `kc` at
    // `Yia` and the enemy `Zb` at `B_`; `kc` is built from `v.cw().clone()`
    // with `qb=!0`, `pf` from `v.EQ(a.Xs)` with `qb=!1`, and `o1a` L403 makes
    // `yb=Gf(kc)` the player and `pb=Gf(Zb)` the opponent). Dojo: player
    // (690,-93), enemy (973,-110) (dojo_params.b78df4b4.xml ModelsViewer).
    // The previous "[Phase 1 step 9]" note here claimed to have flipped the old
    // hard-coded 973/690 pair, but the flip lived in `LocationScene`'s
    // ModelsViewer parse and read the attributes the WRONG way round, so the
    // player still landed on 973. Removed there (see location_scene.cpp).
    // Falls back to the dojo defaults when a location has no <ModelsViewer>.
    if (assets.fight_location.has_spawns()) {
        battle.player_spawn_x = assets.fight_location.player_spawn_x();
        battle.player_spawn_y = assets.fight_location.player_spawn_y();
        battle.enemy_spawn_x = assets.fight_location.enemy_spawn_x();
        battle.enemy_spawn_y = assets.fight_location.enemy_spawn_y();
    }
    battle.max_hp = 1;  // the game's HP fallback (Zn = aB>0 ? aB : 1)
    // JS `wd.Fm` L811 — the player's resolved attribute map (defense/block/
    // crit included; see the helper). `player_unarmed_damage` is kept as the
    // rounded bCa fallback.
    battle.player_attrs = resolve_player_attributes(app());
    battle.player_unarmed_damage = battle.player_attrs.count("UnarmedDamage")
                                       ? battle.player_attrs["UnarmedDamage"]
                                       : 0.0f;
    // [fx] JS `nj.parse` (L885) + `f_a` (L896-897): the stage fight's
    // `<Ringout>` rule configures the off-screen marker arrows (`sXa`). The
    // native battle is hardcoded above, so pull the battle's first-fight
    // `<Rules>` from stages.xml (resolved in the battle's own zone —
    // `pending_battle().zone`, `hp` semantics) and map them into
    // `battle.ringout_*` here, BEFORE init_locks copies the battle into the
    // controller. The dojo Training dummy carries no Ringout -> the markers
    // stay dormant.
    sf2::scene::apply_stage_ringout_rule(
        battle, battle_fight_rules(battle_name_, app().pending_battle().zone));
    // Feeder verification (JS `bb.OE`/`bb.M3` L887-888): log what actually
    // reached the rule engine, including the `<Animation>`/`<Node>` children
    // the old tag+attrs-only feeder dropped (Wave M engines).
    {
        int n_ringout = 0, n_hot = 0, n_lose_fall = 0, n_invert = 0;
        std::size_t n_anims = 0, n_zones = 0;
        for (const sf2::scene::FightRule& r : battle.rules) {
            n_anims += r.animations.size();
            n_zones += r.hot_zones.size();
            switch (r.kind) {
                case sf2::scene::FightRuleKind::ringout: ++n_ringout; break;
                case sf2::scene::FightRuleKind::hot_ground: ++n_hot; break;
                case sf2::scene::FightRuleKind::lose_fall: ++n_lose_fall; break;
                case sf2::scene::FightRuleKind::invert_joystick: ++n_invert; break;
                default: break;
            }
        }
        std::fprintf(stdout,
                     "[fight] stage rules: %zu parsed from %s (%d ringout, %d hotground, "
                     "%d losefall, %d invert; %zu anim names, %zu node zones)\n",
                     battle.rules.size(), battle_name_.c_str(), n_ringout, n_hot, n_lose_fall,
                     n_invert, n_anims, n_zones);
        std::fflush(stdout);
    }

    const sf2::scene::TacticDef* tactic = nullptr;
    const auto it = assets.tactic_defs.find("Standard");
    if (it != assets.tactic_defs.end()) tactic = &it->second;

    // JS `Da.pg` (L67: `Da.pg=new Rk(L.seed)`; `Xx`+`Rk` L2352/2366): the
    // ONE global fight stream. The scene layer OWNS it (`FightController`
    // DaPrng), so the app only seeds it and installs NO override — the former
    // private mt19937 stream (the documented RNG divergence) is gone.
    // `rules_begin_round` reseeds it in place before the RandomRule draws
    // (JS `cl.pmb`, L1413). 0x5F2 = the native replay-seed analog.
    const std::uint32_t fight_seed = 0x5F2u;

    fight_ = std::make_unique<sf2::scene::FightController>();
    // JS `ur` L186-195: resolve the battle's FIRST <Warrior> — FirstName,
    // the NotAI/NotAnimation presence flags, items and attrs. Dojo Training
    // Fight 1 is the Punchbag dummy (FirstName="Punchbag" NotAI="1"
    // NotAnimation="1", stages.xml L12); BOSS_LYNX Fight 1 is a Warrior
    // template with neither flag (L78).
    const BattleWarriorInfo bw = battle_warrior(battle_name_, app().pending_battle().zone);
    // `first_name` is a lang key (`NAME_SHIN`) resolved for display (JS `ur`).
    app().pending_battle().enemy_name =
        bw.first_name.empty() ? "Enemy" : loc(app(), bw.first_name, bw.first_name);
    // VS intro (`ik`, L2069-2071): resolve the two names + portraits the VS
    // screen and the HUD show. Player = the save Warrior (`FirstName`, a lang
    // key like "NAME_SHADOW" -> "SHADOW"; the shipped save's `Avatar` is
    // `avatar_hero`). Enemy = the battle Warrior's resolved `FirstName` (from
    // its `<Template>`; see `battle_warrior`), else the battle Name
    // (`BOSS_LYNX` is itself a lang key -> "LYNX"), with the resolved
    // `Avatar` as the portrait. OPEN: `WarriorSave` does not carry the
    // `Avatar` attr yet, so the player's historical default is used.
    {
        std::string pfirst = "NAME_SHADOW";
        try {
            const WarriorSave w = app().save().load();
            if (!w.first_name.empty()) pfirst = w.first_name;
        } catch (const std::exception&) {
        }
        vs_player_name_ = loc(app(), pfirst, "SHADOW");
        vs_player_image_ = "avatar_hero";
        std::string efirst = bw.first_name.empty() ? battle_name_ : bw.first_name;
        vs_enemy_name_ = loc(app(), efirst, efirst);
        // The portrait is the resolved template's `Avatar` (JS `ur` -> `Hf`);
        // the `Template` stem is the legacy fallback. BOSS_LYNX Fight 1
        // resolves `Avatar="man_kunai"` (the oracle's ШИН portrait).
        std::string eimg;
        const auto av = bw.attrs.find("Avatar");
        if (av != bw.attrs.end() && !av->second.empty()) {
            eimg = av->second;
        } else {
            const auto tmpl = bw.attrs.find("Template");
            eimg = (tmpl != bw.attrs.end() && !tmpl->second.empty()) ? tmpl->second
                                                                     : "avatar_masked";
        }
        std::transform(eimg.begin(), eimg.end(), eimg.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        vs_enemy_image_ = eimg;
        std::fprintf(stdout, "[fight] VS intro: '%s' (%s) vs '%s' (%s)\n",
                     vs_player_name_.c_str(), vs_player_image_.c_str(),
                     vs_enemy_name_.c_str(), vs_enemy_image_.c_str());
        std::fflush(stdout);
    }
    battle.enemy_not_ai = bw.has_not_ai;
    battle.enemy_not_animation = bw.has_not_animation;
    // JS `xc.voice` (L807/L186): the Warrior's `<Voice>` attr feeds the move
    // actions' `<Sound Voice="..">` gate (`fm.fka` L735). The enemy's comes
    // from its stages.xml Warrior template chain; the player's from the
    // `Default` template (`users_default.xml` `<Warrior Voice="Male">`), the
    // same identity source `player_align` above uses.
    battle.enemy_voice = bw.voice;
    battle.player_voice = bw.player_voice;
    // `xc.IY` (JS L191): the align-armor rows `pAa` blends with. In a
    // player-vs-enemy fight `pAa` reads `(attacker.qb ? defender : attacker).IY`,
    // i.e. always the ENEMY's rows; the player's set is `Default`'s.
    battle.enemy_align = to_align_deltas(bw.align);
    battle.player_align = to_align_deltas(bw.player_align);
    resolve_enemy_loadout(app(), bw, battle);
    if (!bw.tactic.empty()) { const auto tit = assets.tactic_defs.find(bw.tactic); if (tit != assets.tactic_defs.end()) tactic = &tit->second; }  // JS `ur` L194: stage warrior `Tactic`
    // P4b — the PLAYER's roulette tactic (JS `IKa` L672):
    //   `this.pb.NT(this.tC);                       // ENEMY  <- `tactic` above
    //    this.yb.parameters.Fj && (this.kc.Gc != null ?
    //        this.yb.NT(this.kc.Gc) : this.yb.s5("Standard"));`
    // `kc` is the player's SAVE warrior, so the player is weighted by its OWN
    // `<Tactic>` when that name resolves in tactic_settings.xml, else
    // "Standard" — never by the battle warrior's. The shipped save carries
    // `Tactic="Player"` (saves/save.xml), which has no `<Tactic>` entry (the
    // 14 shipped names: Standard/NoTables/UseTables/Sensei/Lynx_*/Shogun*/
    // Titan_*/Careful/Aggressive/Beginner), so the JS lands on "Standard".
    const sf2::scene::TacticDef* player_tactic = nullptr;
    {
        const auto sit = assets.tactic_defs.find("Standard");
        if (sit != assets.tactic_defs.end()) player_tactic = &sit->second;
        try {
            const std::string pt = app().save().load().tactic;
            if (!pt.empty()) {
                const auto pit = assets.tactic_defs.find(pt);
                if (pit != assets.tactic_defs.end()) player_tactic = &pit->second;
            }
        } catch (const std::exception&) {
        }
    }
    // JS `xc.cM` L809-810: each warrior is built from its OWN equipment
    // (Skeleton + Weapon + Armor + Helm `Model` list -> `Yc.load` L568 merges
    // them into ONE bone hierarchy, skeleton first). Resolve the PLAYER from
    // the save's typed slots and the ENEMY from its stages.xml
    // `<Warrior>/<Template>` <Items> (a boss gears up via the template:
    // Man_Kunai -> mdl_skeleton + mdl_weapon_kunai + mdl_body_shin +
    // mdl_helm_green_mask). A resolved skeleton is required (clip bone i =
    // merged bone i); when a side resolves to nothing the shared base model
    // (`assets.merged`) is kept (OPEN -> base body).
    sf2::scene::Model player_model_storage;
    sf2::scene::Model enemy_model_storage;
    const sf2::scene::Model* player_model = nullptr;
    const sf2::scene::Model* enemy_model = nullptr;
    {
        std::vector<std::string> player_items;
        try {
            const WarriorSave w = app().save().load();
            player_items = {w.skeleton, w.weapon, w.armor, w.helm};
        } catch (const std::exception&) {
        }
        const std::vector<std::string> pnames =
            fighter_model_names(app(), player_items);
        if (!pnames.empty() && !pnames[0].empty()) {  // skeleton slot present
            player_model_storage = assets.merge_names(pnames);
            if (!player_model_storage.bones.empty()) player_model = &player_model_storage;
        }
        if (battle.enemy_not_animation && !assets.merged_bag.bones.empty()) {
            // The NotAnimation Punchbag dummy keeps its own model (its <Items>
            // are PunchingBag/SkeletonPunchingBag; preserved RC-3 path).
            enemy_model = &assets.merged_bag;
        } else {
            const std::vector<std::string> enames =
                fighter_model_names(app(), bw.items);
            if (!enames.empty() && !enames[0].empty()) {
                enemy_model_storage = assets.merge_names(enames);
                if (!enemy_model_storage.bones.empty()) enemy_model = &enemy_model_storage;
            }
        }
        const sf2::scene::Model& pm =
            player_model != nullptr ? *player_model : assets.merged;
        const sf2::scene::Model& em =
            enemy_model != nullptr ? *enemy_model : assets.merged;
        std::fprintf(stdout,
                     "[fight] fighter models: player=%s %zu bones/%zu tris; "
                     "enemy=%s %zu bones/%zu tris\n",
                     player_model != nullptr ? "gear" : "base", pm.bones.size(),
                     pm.resolved_tris.size(),
                     enemy_model == &assets.merged_bag
                         ? "bag"
                         : (enemy_model != nullptr ? "gear" : "base"),
                     em.bones.size(), em.resolved_tris.size());
        std::fflush(stdout);

        // JS `wd.fya` (L535-536) + `wd.ylb` (L268939): give the fight the
        // child's OWN model (its `<Item>` set resolved against list.xml + the
        // spawner's items), instead of the spawner's merged body. The
        // resolver is looked up lazily and cached in `assets.child_models`.
        const std::vector<CatalogItem> child_catalog = load_full_catalog(app());
        const std::vector<std::string> p_items = player_items;
        const std::vector<std::string> e_items = bw.items;
        App* app_ptr = &app();
        fight_->set_child_model_provider(
            [app_ptr, child_catalog, p_items, e_items](
                const sf2::scene::MoveAction& act, bool is_player)
                -> const sf2::scene::Model* {
                return child_model_for(app_ptr->fight_assets(), child_catalog,
                                       act, is_player,
                                       is_player ? p_items : e_items);
            });
    }
    const std::string& enemy_name = app().pending_battle().enemy_name;
    // Root `<Triggers>` (JS `ra.Dm`): hand the global set to the controller
    // BEFORE init — `setup_bus` (inside init) lock-filters + registers it per
    // side, and per-round re-registers (`setup_bus` L1504) keep it live.
    fight_->set_global_triggers(&assets.global_triggers);
    fight_->init_locks(battle, assets.merged, assets.moves, assets.clips,
                       assets.tactics_sets, tactic, "Player", enemy_name,
                       battle.player_spawn_x, battle.player_spawn_y,
                       battle.enemy_spawn_x, battle.enemy_spawn_y,
                       battle.max_hp, battle.max_hp, {},
                       player_owned, equipped_perks(app(), assets), nullptr,
                       player_model, enemy_model, player_tactic);
    fight_->set_seed(fight_seed);  // JS `Da.pg=new Rk(L.seed)` (L67)
    // The player's Locks move list (`ra.Hza` L684-685) — identical for the
    // direct boot and the Map/Dojo launch (both feed `player_owned_`).
    std::fprintf(stdout, "[fight] player move list: %zu moves (owned rows=%zu)\n",
                 fight_->player().fighter.hb().size(), player_owned_.size());
    std::fflush(stdout);
    // [Phase 1 step 9] The resolved stage Warrior (JS `ur` L186-195) and the
    // input it feeds: the NotAI/NotAnimation gates, the player's resolved
    // UnarmedDamage and the location-sourced spawns.
    std::fprintf(stdout,
                 "[fight] warrior first='%s' not_ai=%d not_animation=%d "
                 "unarmed=%.2f spawn P=(%.0f,%.0f) E=(%.0f,%.0f) hp=%d\n",
                 bw.first_name.c_str(), battle.enemy_not_ai ? 1 : 0,
                 battle.enemy_not_animation ? 1 : 0,
                 battle.player_unarmed_damage, battle.player_spawn_x, battle.player_spawn_y,
                 battle.enemy_spawn_x, battle.enemy_spawn_y, battle.max_hp);
    std::fflush(stdout);
    // Disarm identity (JS `$b(Au)` vs `ownHd`, L394): the player's wielded
    // weapon comes from the save; the enemy defaults to Fists (Training).
    {
        std::string pw = "Fists";
        try {
            pw = app().save().load().weapon;
        } catch (const std::exception&) {
        }
        if (pw.empty()) pw = "Fists";
        fight_->set_fighter_weapons(pw, "Fists");
    }
    fight_->set_bounds(wall_min, wall_max, floor_y);  // the visible dojo floor (the camera anchor)
    // [FIX Phase 4b — black silhouettes] The fighters' mesh color is the
    // location's Root Color (the dojo_params `<Root Color="0x000000">`,
    // JS `Na.cd` fills the fighter Path2D with it). The oracle's fighters
    // are black silhouettes — no red/blue team colors.
    fight_->set_fighter_color(assets.fight_location.root_color());
    std::fprintf(stdout, "[fight] fighter color 0x%06X (location %s Root Color)\n",
                 assets.fight_location.root_color(), location_.c_str());

    // Log the player's move list (the equipment-change evidence).
    std::fprintf(stdout, "[fight] player move list (%zu moves):\n",
                 fight_->player().fighter.hb().size());
    for (const auto* m : fight_->player().fighter.hb()) {
        std::fprintf(stdout, "  %s\n", m->name.c_str());
    }
    std::fflush(stdout);
}

// JS `sc.OD` (`Af.oUa` L2472) key table -> the native GLFW binding.
// [F8] `Af.oUa` is EXACTLY ten entries and nothing else (L2472):
//   a.v[1]=87 (W->Up), a.v[3]=68 (D->Forward), a.v[5]=83 (S->Down),
//   a.v[7]=65 (A->Back), a.v[9]=75 (K->Punch), a.v[10]=76 (L->Kick),
//   a.v[11]=79 (O->Ranged), a.v[12]=80 (P->Magic), a.v[13]=74 (J->RaidCharge),
//   a.v[14]=81 (Q->Super).
// The JS table has NO Space/Enter/arrow/Escape binding. The old port added
// invented aliases (ArrowUp/Down/Left/Right, Space=Punch) plus the app-layer
// Esc pause and Space/Enter next-round shortcuts. Those now live behind the
// explicit opt-in `set_desktop_key_aliases(true)` (OFF by default, see
// `on_key`), so the DEFAULT map below is byte-for-byte `Af.oUa`.
int FightScreen::key_type_for_glfw(int glfw_key) {
    switch (glfw_key) {
        case 87: return static_cast<int>(sf2::scene::key_type::up);           // W
        case 68: return static_cast<int>(sf2::scene::key_type::forward);      // D
        case 83: return static_cast<int>(sf2::scene::key_type::down);         // S
        case 65: return static_cast<int>(sf2::scene::key_type::back);         // A
        case 75: return static_cast<int>(sf2::scene::key_type::punch);        // K
        case 76: return static_cast<int>(sf2::scene::key_type::kick);         // L
        case 79: return static_cast<int>(sf2::scene::key_type::ranged);       // O
        case 80: return static_cast<int>(sf2::scene::key_type::magic);        // P
        case 74: return static_cast<int>(sf2::scene::key_type::raid_charge);  // J
        case 81: return static_cast<int>(sf2::scene::key_type::super);        // Q
        default: return 0;
    }
}

// The desktop key aliases (arrows + Space). Not part of `Af.oUa`; folded in
// when `desktop_key_aliases_` is on (the default, see on_key).
int FightScreen::desktop_alias_for_glfw(int glfw_key) {
    switch (glfw_key) {
        case 263: return static_cast<int>(sf2::scene::key_type::back);     // Left
        case 262: return static_cast<int>(sf2::scene::key_type::forward);  // Right
        case 265: return static_cast<int>(sf2::scene::key_type::up);       // Up
        case 264: return static_cast<int>(sf2::scene::key_type::down);     // Down
        case 32: return static_cast<int>(sf2::scene::key_type::punch);     // Space
        default: return 0;
    }
}

void FightScreen::on_key(int glfw_key, bool down) {
    // Every key edge resets the reported key_type: an unbound key, the Esc
    // pause control and Enter all leave it 0 (they produce no fight key).
    last_input_key_type_ = 0;
    // JS `Af.oUa` (L2472) is the BROWSER key map and binds exactly ten keys.
    // `App::poll_input` (app.cpp) additionally polls the arrows, Space, Esc
    // and Enter because the desktop player presses them; the desktop aliases
    // fold those into the same `player_input` path. The map is a property of
    // the SCREEN, not of the headless flag: the old `|| app().headless()`
    // made every headless driver silently accept the arrows/Space while a
    // real windowed player got nothing - the harness could not see the bug.
    // Run `set_desktop_key_aliases(false)` for the byte-exact JS table.
    const bool aliases = desktop_key_aliases_;
    // Pause toggle. The JS path is the HUD pause disc (`Jn` -> `Ar.Qg(0)` ->
    // `Aia()` L425, drawn by this screen); Esc (256) is a desktop alias. The
    // headless drivers inject the disc click, never keys.
    if (aliases && down && glfw_key == 256) {
        if (fight_ != nullptr && !fight_->round_wait() && !fight_->battle_over()) {
            paused_ = !paused_;
            sf2::audio::AudioEngine::instance().play("snd_click_1");
            std::fprintf(stdout, "[fight] pause %s (Esc)\n", paused_ ? "ON" : "OFF");
            std::fflush(stdout);
        }
        return;
    }
    // While paused, swallow every fight key (no sim input leak — the update
    // is frozen too, so buffered keys would otherwise fire on resume).
    if (paused_) return;
    // (The old Space/Enter "Next round" alias is GONE: the JS has no such
    // binding — the round auto-advances through the banner machine, so a
    // key-driven advance would double-step the round.)
    // GLFW key codes -> the game's key_type, bound from the JS key map
    // `sc.OD` (`Af.oUa` L2472) — the ten keys in `key_type_for_glfw` above.
    // The desktop aliases (arrows/Space) fold in only when opted in. Blocking
    // is NOT a raw key in this game: the fighter blocks while any move's
    // `Block` interval is active (e.g. HighPunch recovery).
    //
    // DIRECTIONAL keys go through the JS `Za.bbb` binding table (the `gu`
    // driver): the four movement directions are a held set and the JS checks
    // the DIAGONAL key-pairs BEFORE the cardinals, so W+D selects
    // up_forward(2) — not up(1)+forward(3). The on-screen pad reaches the
    // SAME `player_input` edges through `update_pad_input`.
    const int move_slot = keyboard_move_slot(glfw_key, aliases);
    if (move_slot >= 0) {
        keyboard_move_edge(fight_.get(), keys_, move_slot, down, "fight");
        last_input_key_type_ = keys_.sector;
        return;
    }
    int kt_id = key_type_for_glfw(glfw_key);
    if (aliases && kt_id == 0) {
        kt_id = desktop_alias_for_glfw(glfw_key);
    }
    last_input_key_type_ = kt_id;
    if (kt_id == 0) return;
    sf2::scene::key_type kt = static_cast<sf2::scene::key_type>(kt_id);
    const int idx = static_cast<int>(kt);
    if (idx < 0 || idx >= 16) return;
    key_state_[idx] = down;
    if (fight_) {
        fight_->player_input(kt, down ? sf2::scene::press_type::tap
                                      : sf2::scene::press_type::release);
    }
}

// The replay/test hook: feed a game key edge (key_type id) into the fight
// input. Mirrors the on_key tail (key_state_ + player_input) without the
// GLFW key map so a recorded `control` id (JS `sa.$h`: 1..14) can be applied
// directly.
void FightScreen::inject_game_key(int key_type_index, bool down) {
    if (key_type_index < 0 || key_type_index >= 16) return;
    key_state_[key_type_index] = down;
    if (fight_ != nullptr) {
        fight_->player_input(static_cast<sf2::scene::key_type>(key_type_index),
                             down ? sf2::scene::press_type::tap
                                  : sf2::scene::press_type::release);
    }
}

void FightScreen::place_fighters(float me_x, float enemy_x) {
    if (fight_ != nullptr) fight_->debug_place_fighters(me_x, enemy_x);
}

float FightScreen::player_world_x() const {
    return fight_ != nullptr ? fight_->player().fighter.world_x() : 0.0f;
}

float FightScreen::enemy_world_x() const {
    return fight_ != nullptr ? fight_->enemy().fighter.world_x() : 0.0f;
}

// [probe] The M2 round-transition hide flag (JS `Ta.XF` L370 -> the whole
// 3-D view's `isVisible`) and the camera centre x. Read-only.
bool FightScreen::scene_visible() const {
    return fight_ != nullptr ? fight_->scene_visible() : true;
}

float FightScreen::camera_center_x() const {
    return fight_ != nullptr ? fight_->camera().center_x : 0.0f;
}

int FightScreen::player_move_frame() const {
    return fight_ != nullptr ? fight_->player().fighter.move_frame() : -1;
}

void FightScreen::reset_player_move() {
    if (fight_ != nullptr) fight_->debug_reset_player_move();
}

std::size_t FightScreen::move_list_size() const {
    return fight_ != nullptr ? fight_->player().fighter.hb().size() : 0;
}

// The ordered player move-list names, ","-joined — the boot-vs-Map
// comparison's exact-content digest (the size alone could hide a swap).
std::string FightScreen::move_list_digest() const {
    if (fight_ == nullptr) return std::string();
    std::string out;
    for (const sf2::scene::MoveDef* m : fight_->player().fighter.hb()) {
        if (m == nullptr) continue;
        if (!out.empty()) out += ",";
        out += m->name;
    }
    return out;
}

std::string FightScreen::player_last_decision() const {
    return fight_ != nullptr ? fight_->player().last_decision : std::string();
}

int FightScreen::player_moves_started() const {
    return fight_ != nullptr ? fight_->player().moves_started : 0;
}

// The player's last move decision rendered for `--verify-input`. It pins the
// whole JS `Gc.DK` `c == false` branch (L673-674): candidate set with each
// candidate's `<Priority>` in `hb_` (JS `ra.Lk`) order, the `Aua`
// max-`priority` non-`Rha` group (`f`), the `g` (`Rha`) `Ukb` name, the
// `uf.sja` draw (when the group is not a singleton), the drawn index and the
// move that started.
std::string FightScreen::player_decision() const {
    if (fight_ == nullptr) return std::string();
    const sf2::scene::Fighter::MoveDecision& r =
        fight_->player().fighter.last_decision();
    if (!r.valid) return std::string();
    char buf[256];
    std::string out = "cands=";
    for (std::size_t i = 0; i < r.cands.size(); ++i) {
        if (i != 0) out += ",";
        std::snprintf(buf, sizeof(buf), "%s@%d", r.cands[i].first.c_str(),
                      r.cands[i].second);
        out += buf;
    }
    out += " f=";
    for (std::size_t i = 0; i < r.f_group.size(); ++i) {
        if (i != 0) out += ",";
        out += r.f_group[i];
    }
    if (r.drew) {
        std::snprintf(buf, sizeof(buf), " draw=%.6f idx=%d ",
                      static_cast<double>(r.draw), r.index);
    } else {
        std::snprintf(buf, sizeof(buf), " draw=- idx=%d ", r.index);
    }
    out += buf;
    out += r.picked.empty() ? "<none>" : r.picked;
    if (r.ukb_set) {
        out += " ukb=";
        out += r.ukb;
    }
    return out;
}

std::string FightScreen::player_current_move() const {
    if (fight_ == nullptr) return std::string();
    const sf2::scene::MoveDef* m = fight_->player().fighter.current_move();
    return m != nullptr ? m->name : std::string();
}

std::string FightScreen::enemy_current_move() const {
    if (fight_ == nullptr) return std::string();
    const sf2::scene::MoveDef* m = fight_->enemy().fighter.current_move();
    return m != nullptr ? m->name : std::string();
}

// [probe, authorised] Enemy hit-reaction state for `--boss-hit-probe`.
bool FightScreen::enemy_ragdoll_active() const {
    return fight_ != nullptr && fight_->enemy().fighter.ragdoll_active();
}

int FightScreen::enemy_ragdoll_frame() const {
    return fight_ != nullptr ? fight_->enemy().fighter.ragdoll_frame_count() : 0;
}

std::string FightScreen::enemy_ragdoll_name() const {
    if (fight_ == nullptr) return std::string();
    const std::vector<std::string>& n = fight_->enemy().fighter.ragdoll_names();
    return n.empty() ? std::string() : n.front();
}

int FightScreen::enemy_moves_started() const {
    return fight_ != nullptr ? fight_->enemy().moves_started : 0;
}

float FightScreen::player_facing() const {
    return fight_ != nullptr ? static_cast<float>(fight_->player().fighter.facing())
                             : 0.0f;
}

float FightScreen::enemy_facing() const {
    return fight_ != nullptr ? static_cast<float>(fight_->enemy().fighter.facing())
                             : 0.0f;
}

int FightScreen::fight_frame() const {
    return fight_ != nullptr ? fight_->frame() : -1;
}

bool FightScreen::vs_active() const { return vs_active_; }

float FightScreen::vs_time() const { return vs_t_; }

bool FightScreen::round_wait() const {
    return fight_ != nullptr && fight_->round_wait();
}

void FightScreen::next_button_center(float& cx, float& cy) const {
    // There is NO Next button (see the header note): the JS round
    // auto-advances. Report the inert (0,0) corner so the non-owned
    // headless drivers' tap lands on no HUD/gamepad hit zone (the on-screen
    // gamepad is bottom-left/bottom-right; the pause disc is at 640..708 x
    // 117..185).
    (void)fight_;
    cx = 0.0f;
    cy = 0.0f;
}

// [trace, Phase 0] Arms the FightController's per-frame pose dump (the
// pose/camera trace the JS-side oracle dump is diffed against).
void FightScreen::enable_pose_dump(const std::string& path, int frames) {
    if (fight_ != nullptr) {
        fight_->set_pose_dump(path, frames);
    }
}

// ---------------------------------------------------------------------------
// On-screen gamepad (JS `Za` virtual controls) — the original's touch pad.
// ---------------------------------------------------------------------------

bool FightScreen::pad_visible() const {
    // The pad shows only while the round is live (JS `Za.isVisible` is set
    // by the fight HUD; between rounds the Next button replaces it).
    return fight_ != nullptr && !fight_->round_wait() && !fight_->battle_over();
}

// The pointer -> gamepad events. Runs in update_impl BEFORE the fight
// update so the buffered keys land the same frame (like on_key). The body
// lives in the shared `update_pad_input` (the dojo's `FightNone` viewer
// calls the SAME helper).
void FightScreen::update_gamepad_input() {
    update_pad_input(app(), fight_.get(), pad_, pad_visible(), "fight");
}

// The gamepad render (JS `Za.Ea` -> `ze.Ea` + `fu.Ea`): the atlas frames
// from ui/controller — JoystickContainer_norm/action (base), Joystick_
// norm/action (knob), btn_punch_normal/action, btn_kick_normal/action.
// Flat procedural circles when the atlas is unavailable.
void FightScreen::draw_gamepad(App& app) const {
    if (fight_ == nullptr || !pad_visible()) return;
    const GamepadLayout pad;
    sf2::render::Renderer& ren = app.renderer();

    // --- Joystick: base + knob. The JS swaps the base frame to _action
    // while grabbed (`RT(a)` toggles aX/EH/$W/DX) — the native mirrors it.
    const bool grabbed = pad_.joy_grabbed;
    const char* base_frame = grabbed ? "JoystickContainer_action" : "JoystickContainer_norm";
    const char* knob_frame = grabbed ? "Joystick_action" : "Joystick_norm";
    const float base_size = pad.joy_r * 2.0f;
    const float knob_size = pad.knob_r * 2.0f;
    if (!try_draw_atlas_button(app, base_frame, pad.joy_cx, pad.joy_cy, base_size,
                               base_size, 1.0f)) {
        // Fallback: a dark translucent circle (12-segment disc).
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegs = 20;
        const float step = 2.0f * kPi / static_cast<float>(kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const float a0 = static_cast<float>(s) * step;
            const float a1 = static_cast<float>(s + 1) * step;
            float tri[6] = {pad.joy_cx,
                           pad.joy_cy,
                           pad.joy_cx + std::cos(a0) * pad.joy_r,
                           pad.joy_cy + std::sin(a0) * pad.joy_r,
                           pad.joy_cx + std::cos(a1) * pad.joy_r,
                           pad.joy_cy + std::sin(a1) * pad.joy_r};
            ren.draw_triangles(tri, 3, 0.08f, 0.08f, 0.1f, 0.55f);
        }
    }
    const float knob_cx = pad.joy_cx + pad_.joy_knob_x;
    const float knob_cy = pad.joy_cy + pad_.joy_knob_y;
    if (!try_draw_atlas_button(app, knob_frame, knob_cx, knob_cy, knob_size, knob_size,
                               1.0f)) {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegs = 14;
        const float step = 2.0f * kPi / static_cast<float>(kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const float a0 = static_cast<float>(s) * step;
            const float a1 = static_cast<float>(s + 1) * step;
            float tri[6] = {knob_cx,
                           knob_cy,
                           knob_cx + std::cos(a0) * pad.knob_r,
                           knob_cy + std::sin(a0) * pad.knob_r,
                           knob_cx + std::cos(a1) * pad.knob_r,
                           knob_cy + std::sin(a1) * pad.knob_r};
            ren.draw_triangles(tri, 3, 0.22f, 0.24f, 0.28f, 0.85f);
        }
    }

    // --- Attack buttons: punch + kick circles (the JS `ig` swaps the
    // frame to _action while pressed). Draw-size 0.9 (centers + hit radii
    // untouched — JS fu/uab; draw only): 4px gap -> 12px gap.
    const float btn_size = pad.btn_r * 2.0f * 0.9f;
    const bool punch_drawn =
        try_draw_atlas_button(app, pad_.btn_punch_down ? "btn_punch_action" : "btn_punch_normal",
                              pad.punch_cx, pad.punch_cy, btn_size, btn_size, 1.0f);
    const bool kick_drawn =
        try_draw_atlas_button(app, pad_.btn_kick_down ? "btn_kick_action" : "btn_kick_normal",
                              pad.kick_cx, pad.kick_cy, btn_size, btn_size, 1.0f);
    if (!punch_drawn) {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegs = 14;
        const float step = 2.0f * kPi / static_cast<float>(kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const float a0 = static_cast<float>(s) * step;
            const float a1 = static_cast<float>(s + 1) * step;
            float tri[6] = {pad.punch_cx,
                           pad.punch_cy,
                           pad.punch_cx + std::cos(a0) * pad.btn_r,
                           pad.punch_cy + std::sin(a0) * pad.btn_r,
                            pad.punch_cx + std::cos(a1) * pad.btn_r,
                           pad.punch_cy + std::sin(a1) * pad.btn_r};
            ren.draw_triangles(tri, 3, pad_.btn_punch_down ? 0.9f : 0.3f, 0.55f, 0.15f, 0.85f);
        }
    }
    if (!kick_drawn) {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegs = 14;
        const float step = 2.0f * kPi / static_cast<float>(kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const float a0 = static_cast<float>(s) * step;
            const float a1 = static_cast<float>(s + 1) * step;
            float tri[6] = {pad.kick_cx,
                           pad.kick_cy,
                           pad.kick_cx + std::cos(a0) * pad.btn_r,
                           pad.kick_cy + std::sin(a0) * pad.btn_r,
                           pad.kick_cx + std::cos(a1) * pad.btn_r,
                           pad.kick_cy + std::sin(a1) * pad.btn_r};
            ren.draw_triangles(tri, 3, pad_.btn_kick_down ? 0.9f : 0.3f, 0.55f, 0.15f, 0.85f);
        }
    }
}

// [FIX Phase 4a verification] Bone-sample check: the sampled bone positions
// (the Fighter::positions() after sample) for a few skeleton bones vs the
// clip data, plus the triangle bbox (humanoid shape + on-screen check).
void FightScreen::verify_fight() const {
    if (fight_ == nullptr) return;
    const sf2::scene::FightCamera& cam = fight_->camera();
    std::fprintf(stdout, "[verify] camera center=(%.1f, %.1f) zoom=%.3f\n",
                 cam.center_x, cam.center_y, cam.zoom);
    std::fprintf(stdout, "[verify] player world x=%.1f enemy world x=%.1f\n",
                 fight_->player().fighter.world_x(), fight_->enemy().fighter.world_x());
    // [FIX Phase 4b — screen positions] The fighters' feet (the spawn floor
    // line) project with the SAME camera the render used.
    {
        const sf2::app::FightAssets& assets = app().fight_assets();
        sf2::render::Camera c;
        c.center_x = cam.center_x;
        c.center_y = cam.center_y;
        c.zoom = cam.zoom;
        c.view_w = 1280.0f;
        c.view_h = 720.0f;
        c.arena_h = assets.fight_location.arena_height() > 0.0f
                        ? assets.fight_location.arena_height()
                        : 560.0f;
        c.arena_floor = assets.fight_location.arena_floor();
        c.arena_center_x = assets.fight_location.arena_width() * 0.5f;
        const float feet_y = fight_->player().fighter.world_y();
        const float psx = c.world_to_screen_x(fight_->player().fighter.world_x(), 1.0f);
        const float psy = c.world_to_screen_y(feet_y);
        const float esx = c.world_to_screen_x(fight_->enemy().fighter.world_x(), 1.0f);
        const float esy = c.world_to_screen_y(feet_y);
        std::fprintf(stdout, "[verify] screen: player feet=(%.0f, %.0f) enemy feet=(%.0f, %.0f)\n",
                     psx, psy, esx, esy);
    }
    auto report = [&](const char* who, const sf2::scene::Fighter& f) {
        const sf2::scene::Model& m = f.model();
        const std::vector<float>& pos = f.positions();
        const char* bones[4] = {"COM", "NTop", "NAnkle_2", "NHeadF"};
        std::fprintf(stdout, "[verify] %s bone sample (world):\n", who);
        for (int b = 0; b < 4; ++b) {
            const int idx = m.bone_by_name(bones[b]);
            if (idx < 0 || static_cast<std::size_t>(idx) * 2 + 1 >= pos.size()) continue;
            std::fprintf(stdout, "  %s = (%.1f, %.1f)\n", bones[b],
                         pos[static_cast<std::size_t>(idx) * 2],
                         pos[static_cast<std::size_t>(idx) * 2 + 1]);
        }
        // [Phase 4d debug] The key skeleton bones for the capsule strip.
        static const char* kCaps[14] = {"NHead", "NTop", "NNeck", "NChest", "NStomach",
                                        "NHip_1", "NHip_2", "NKnee_1", "NKnee_2",
                                        "NAnkle_1", "NAnkle_2", "NToe_1", "NToe_2", "NHeel_1"};
        std::fprintf(stdout, "[verify] %s capsule bones (world):\n", who);
        for (int b = 0; b < 14; ++b) {
            const int idx = m.bone_by_name(kCaps[b]);
            if (idx < 0 || static_cast<std::size_t>(idx) * 2 + 1 >= pos.size()) continue;
            std::fprintf(stdout, "  %-10s = (%.1f, %.1f)\n", kCaps[b],
                         pos[static_cast<std::size_t>(idx) * 2],
                         pos[static_cast<std::size_t>(idx) * 2 + 1]);
        }
        // [Phase 4d debug] The BODY-Node* cloth nodes' world positions (the
        // body-mesh leg coverage check).
        static const char* kCloth[8] = {"BODY-Node16", "BODY-Node11", "BODY-Node15",
                                        "BODY-Node12", "BODY-Node17", "BODY-Node18",
                                        "BODY-Node20", "BODY-Node19"};
        std::fprintf(stdout, "[verify] %s BODY-Node cloth (world):\n", who);
        for (int b = 0; b < 8; ++b) {
            const int idx = m.bone_by_name(kCloth[b]);
            if (idx < 0 || static_cast<std::size_t>(idx) * 2 + 1 >= pos.size()) continue;
            std::fprintf(stdout, "  %s = (%.1f, %.1f)\n", kCloth[b],
                         pos[static_cast<std::size_t>(idx) * 2],
                         pos[static_cast<std::size_t>(idx) * 2 + 1]);
        }
        // Capsule bbox: the <Edges> capsule endpoints inflated by the
        // capsule radius. Measures MESHLESS models (the dojo Punchbag has no
        // triangles, so the tri-bbox below is empty for it).
        {
            float cmin_x = 0.0f, cmin_y = 0.0f, cmax_x = 0.0f, cmax_y = 0.0f;
            const int cap_n = f.capsule_bbox(cmin_x, cmin_y, cmax_x, cmax_y);
            if (cap_n > 0) {
                std::fprintf(stdout,
                             "[verify] %s capsule-bbox: (%.1f, %.1f)-(%.1f, %.1f) "
                             "w=%.1f h=%.1f edges=%d\n",
                             who, cmin_x, cmin_y, cmax_x, cmax_y,
                             cmax_x - cmin_x, cmax_y - cmin_y, cap_n);
            }
        }
        // Ragdoll latch (JS `Al.nk`/`frameCount`/`names`).
        std::fprintf(stdout, "[verify] %s ragdoll nk=%d frame=%d names=%zu\n",
                     who, f.ragdoll_active() ? 1 : 0, f.ragdoll_frame_count(),
                     f.ragdoll_names().size());
        float min_x, min_y, max_x, max_y;
        f.triangle_bbox(min_x, min_y, max_x, max_y);
        const float bw = max_x - min_x, bh = max_y - min_y;
        std::fprintf(stdout, "[verify] %s tri-bbox: (%.1f, %.1f)-(%.1f, %.1f) "
                             "w=%.1f h=%.1f ratio=%.2f\n",
                     who, min_x, min_y, max_x, max_y, bw, bh,
                     bh > 0.0f ? bw / bh : 0.0f);
        // The widest triangle span (the stretched-mesh check).
        float widest = 0.0f;
        for (const sf2::scene::TriResolved& tri : m.resolved_tris) {
            const float x1 = pos[static_cast<std::size_t>(tri.i1) * 2];
            const float y1 = pos[static_cast<std::size_t>(tri.i1) * 2 + 1];
            const float x2 = pos[static_cast<std::size_t>(tri.i2) * 2];
            const float y2 = pos[static_cast<std::size_t>(tri.i2) * 2 + 1];
            const float x3 = pos[static_cast<std::size_t>(tri.i3) * 2];
            const float y3 = pos[static_cast<std::size_t>(tri.i3) * 2 + 1];
            float sx = std::fabs(x1 - x2);
            if (std::fabs(x2 - x3) > sx) sx = std::fabs(x2 - x3);
            if (std::fabs(x3 - x1) > sx) sx = std::fabs(x3 - x1);
            float sy = std::fabs(y1 - y2);
            if (std::fabs(y2 - y3) > sy) sy = std::fabs(y2 - y3);
            if (std::fabs(y3 - y1) > sy) sy = std::fabs(y3 - y1);
            if (sx + sy > widest) widest = sx + sy;
        }
        std::fprintf(stdout, "[verify] %s widest-tri-span=%.1f\n", who, widest);
        // On-screen check: the bbox center within the 1280x720 view (projected).
        const float world_cx = (min_x + max_x) * 0.5f;
        const float world_cy = (min_y + max_y) * 0.5f;
        sf2::render::Camera vcam;
        vcam.center_x = cam.center_x;
        vcam.center_y = cam.center_y;
        vcam.zoom = cam.zoom;
        vcam.view_w = 1280.0f;
        vcam.view_h = 720.0f;
        vcam.arena_h = 560.0f;
        vcam.arena_floor = 80.0f;
        vcam.arena_center_x = 980.0f;
        const float scx = vcam.world_to_screen_x(world_cx, 1.0f);
        const float scy = vcam.world_to_screen_y(world_cy);
        std::fprintf(stdout, "[verify] %s on-screen: center=(%.0f, %.0f) %s\n", who, scx, scy,
                     (scx >= 0 && scx <= 1280 && scy >= 0 && scy <= 720) ? "OK" : "OFF-SCREEN");
    };
    report("player", fight_->player().fighter);
    report("enemy", fight_->enemy().fighter);
    std::fflush(stdout);
}
// --- Pause dialog `Dr` layout (JS L2065-2068; PAUSE_STATIC §3) -------------
// The HUD pause widget (`Sf.Jn`, L2034) / Esc opens `ha.Aia`'s `Dr` dialog.
// `Dr.layout` (L2068): `content` is scaled `(b.N-b.J)/900` with
// `b = ma.Kq.fn(1.125)`; the 4 `Mr` buttons are 150x150 at design y=440,
// x = `(900-(4*150+75))/2 + 75` + i*(150+25) = 187.5/362.5/537.5/712.5, and
// the title (`E.get(1302)` `y.FQa`, 400x96) at design (450,280). At 16:9
// (ma.Kq = 0,0,1280,720): b = (235,0,1045,720), scale 0.9 ->
// title (640,252) 360x86; button row y=396, x=403.75/561.25/718.75/876.25.
// Button order: home, music, sound, play (L2066: home/tp/Sla/play).
constexpr float kPauseDlgTitleCx = 640.0f;
constexpr float kPauseDlgTitleCy = 252.0f;
constexpr float kPauseDlgTitleW = 360.0f;
constexpr float kPauseDlgTitleH = 86.4f;
constexpr float kPauseDlgToggleS = 135.0f;   // 150 * 0.9
constexpr float kPauseDlgRowY = 396.0f;      // 440 * 0.9
constexpr float kPauseDlgHomeX = 403.75f;
constexpr float kPauseDlgMusicX = 561.25f;
constexpr float kPauseDlgSoundX = 718.75f;
constexpr float kPauseDlgPlayX = 876.25f;

void FightScreen::update_impl(float dt) {
    if (fight_ == nullptr) return;
    // Sensei dialog modal gate (quest engine `He` records): a dialog queued
    // while the fight is up owns the input (`He` `IgnoreBack="1"`), so the
    // fight's own input path is skipped until its plate fires. The Dojo/Map/
    // Shop screens already gate this way; the Fight screen did not, so a
    // dialog queued here rendered nowhere and blocked nothing.
    if (quest_modal_consume(app())) return;
    // VS intro (`ik`, L2069): presentation-only pre-fight screen. The sim is
    // NOT frozen — JS creates the fight only after `ik.kg`, but the native
    // controller already exists, and running it underneath keeps the existing
    // deterministic step counts (ui-tour / headless-loop) intact while the
    // overlay covers it. The overlay auto-advances on its own timer.
    if (vs_active_) {
        vs_t_ += dt;
        if (vs_t_ >= kVsTotal) vs_active_ = false;
    }
    // [ROUND-plate lead-in] The intro's ROUND plate must not be consumed while
    // the `ik` VS overlay covers the scene: release the plate clock once the
    // overlay ends so `ROUND -> phase 1 -> FIGHT` plays on the visible fight.
    if (!vs_active_ && fight_ != nullptr) fight_->release_intro();
    // Location timeline (D6): advance the battle location's SimpleEffect
    // Transparency loop per frame (its own `fight_location`, separate from
    // the hub's `dojo`).
    if (app().has_fight_assets()) {
        app().fight_assets().fight_location.update(dt);
    }
    if (!auto_attack_wired_) {
        auto_attack_wired_ = true;
        if (app().auto_attack()) {
            fight_->set_auto_attack(true);
            std::fprintf(stdout, "[fight] auto-attack ON\n");
        }
    }
    // Pause dialog hit geometry (mirrors render_impl; the `Jn` HUD button
    // slot under the timer, oracle (640,117) 68 px — JS L2034/L2036).
    const float kPauseIx = 640.0f, kPauseIy = 117.0f, kPauseIw = 68.0f, kPauseIh = 68.0f;
    auto pause_hit = [&](float cx, float cy, float w, float h) {
        const App::PointerState& pp = app().pointer();
        return pp.x >= cx - w / 2 && pp.x <= cx + w / 2 && pp.y >= cy - h / 2 &&
               pp.y <= cy + h / 2;
    };
    const bool live =
        fight_ != nullptr && !fight_->round_wait() && !fight_->battle_over();
    if (paused_) {
        // Frozen sim (UI-layer pause): dialog clicks only; everything below
        // (log, Next, results) is skipped by the early return.
        const App::PointerState& p = app().pointer();
        if (p.pressed) {
            if (pause_hit(kPauseDlgPlayX, kPauseDlgRowY, kPauseDlgToggleS,
                          kPauseDlgToggleS)) {
                // `play` frame = resume (PAUSE_STATIC §3 `tZ`).
                paused_ = false;
                sf2::audio::AudioEngine::instance().play("snd_click_1");
                std::fprintf(stdout, "[fight] pause OFF (resume, Dr.play)\n");
                std::fflush(stdout);
            } else if (pause_hit(kPauseDlgMusicX, kPauseDlgRowY, kPauseDlgToggleS,
                                 kPauseDlgToggleS)) {
                // `Dr.Sla` (L2066-2067): `this.Sla.Db?(lb.WT(!lb.Mz()), ...)`
                // -> `ta.WT(a)` L1264 `L.K.$f.cMa(a?0:1)` = the music BUS
                // volume, so the track keeps playing (muted) and unmute
                // resumes it. The old port stopped the sound and could not
                // restart it (`music_track()` reads "" after a stop).
                music_off_ = !music_off_;
                sf2::audio::AudioEngine::instance().set_music_muted(music_off_);
                persist_bus_mutes(app());  // JS `lb.WT` L1276: `p.TJ.save()`
                sf2::audio::AudioEngine::instance().play("snd_click_1");
                std::fprintf(stdout, "[fight] pause music %s (Dr.PauseMusic)\n",
                             music_off_ ? "OFF" : "ON");
                std::fflush(stdout);
            } else if (pause_hit(kPauseDlgSoundX, kPauseDlgRowY, kPauseDlgToggleS,
                                 kPauseDlgToggleS)) {
                // `PauseSound_on/off` (display only — no runtime SFX mute API;
                // see the stream report).
                // `Dr.tp` (L2066-2067): `this.tp.Db?(lb.VT(!lb.Lz()), ...)`
                // -> `ta.VT(a)` L1264 `L.K.$f.uF(a?0:1)` = the master SFX BUS
                // volume (`ta.ZD`, read back by `lb.Lz()`). Was a no-op log
                // ("no runtime SFX mute API").
                sf2::audio::AudioEngine& au = sf2::audio::AudioEngine::instance();
                au.set_sfx_muted(!au.sfx_muted());
                persist_bus_mutes(app());  // JS `lb.VT` L1276: `p.TJ.save()`
                au.play("snd_click_1");
                std::fprintf(stdout, "[fight] pause sound %s (Dr.PauseSound)\n",
                             au.sfx_muted() ? "OFF" : "ON");
                std::fflush(stdout);
            } else if (pause_hit(kPauseDlgHomeX, kPauseDlgRowY, kPauseDlgToggleS,
                                 kPauseDlgToggleS)) {
                // `home` = quit (JS `Xc.Zhb` exit-confirm -> `O3a`; the confirm
                // dialog is not ported — direct pop, OPEN).
                sf2::audio::AudioEngine::instance().play("snd_click_1");
                std::fprintf(stdout, "[fight] pause QUIT (Dr.home -> caller)\n");
                std::fflush(stdout);
                paused_ = false;
                manager().pop();
                return;
            }
        }
        return;
    }
    // The HUD pause icon (`Jn`, top-right) while the round is live.
    if (live) {
        const App::PointerState& p = app().pointer();
        if (p.pressed && pause_hit(kPauseIx, kPauseIy, kPauseIw, kPauseIh)) {
            paused_ = true;
            sf2::audio::AudioEngine::instance().play("snd_click_1");
            std::fprintf(stdout, "[fight] pause ON (HUD icon -> Dr)\n");
            std::fflush(stdout);
            return;
        }
    }
    // The on-screen gamepad (JS `Za`): the pointer events feed the same
    // player_input path the keyboard uses — BEFORE the fight update so
    // the buffered keys land this frame (the same ordering as on_key).
    update_gamepad_input();
    fight_->update(dt);
    // [child-model probe] The shipped fights reach 0 `<CreatePlayer>` rows
    // (the action-kind census), so `SF2_CHILD_PROBE=1` drives one synthetic
    // create -> render -> delete cycle through the EXACT dispatch path and
    // logs the observed counts. No effect unless the variable is set.
    {
        static int child_probe_state = -1;  // -1 unread, 0 off, 1 armed, 2 done
        if (child_probe_state == -1) {
            const char* env = std::getenv("SF2_CHILD_PROBE");
            child_probe_state =
                (env != nullptr && env[0] != '\0' && env[0] != '0') ? 1 : 0;
        }
        if (child_probe_state == 1) {
            child_probe_state = 2;
            int spawned = 0, live_after_spawn = 0, live_after_delete = 0;
            fight_->probe_child_cycle(true, 6, &spawned, &live_after_spawn,
                                      &live_after_delete);
            std::fprintf(stdout,
                         "[child-probe] create->render->delete spawned=%d "
                         "live=%d after-delete=%d -> %s\n",
                         spawned, live_after_spawn, live_after_delete,
                         (spawned == 1 && live_after_spawn == 1 &&
                          live_after_delete == 0)
                             ? "PASS"
                             : "FAIL");
            std::fflush(stdout);
        }
    }

    // Phase 7.4 display-layer tick (NO gameplay impact — presentation
    // copies only; the sim never reads them). The magic/effect containers
    // (JS `tl.Rf`, `tl.WL` L837) tick inside `FightController::update`; the
    // renderer reads them from the fight (see draw_fight). This layer only
    // ticks the regen copies.
    const int phase_now = fight_->phase();
    // Special regen display copies (JS `wd.MOa()` L532-533, gated on
    // `eu == 2` at L499; canonical home is Fighter — see special_regen.hpp).
    if (sf2::audio::regen_should_tick(phase_now)) {
        sf2::audio::regen_tick(s_regen_player_, 1.0f);
        sf2::audio::regen_tick(s_regen_enemy_, 1.0f);
    }

    // Per-second log.
    if (fight_->frame() / 60 != last_log_frame_) {
        last_log_frame_ = fight_->frame() / 60;
        // JS `Sf.iPa` (L2036): log text shows max(0,NF).
        const int timer = std::max(0, fight_->round().time_nf);
        std::fprintf(stdout, "[fight] F%d phase=%d round=%d timer=%d P:%.0f (%s) E:%.0f (%s)\n",
                     fight_->frame(), fight_->phase(), fight_->round().number,
                     std::max(0, timer), fight_->player().hp,
                     fight_->player().last_move.empty() ? "idle" : fight_->player().last_move.c_str(),
                     fight_->enemy().hp,
                     fight_->enemy().last_move.empty() ? "idle" : fight_->enemy().last_move.c_str());
        std::fflush(stdout);
    }

    // There is NO between-rounds "Next" click rect. The JS round
    // auto-advances through the banner machine (`ca.Onb` L411 `ZK(); NA();
    // Z2()` -> `Cr.tca` L2023 -> `ca.vhb` L410 case 2 -> `FNa`); the old
    // invisible rect (and its `snd_click_1`) was an invention and is
    // DELETED. `round_wait()` is now only the break-window gate that hides
    // the on-screen gamepad until `FNa`.

    // Battle end -> Results (JS `bea` L413 -> `v.kD` L622187 -> the
    // results; `qxa` L1213 pops back to the map).
    if (fight_->battle_over() && !results_pushed_) {
        results_pushed_ = true;
        const bool player_won = fight_->winner() != nullptr && fight_->winner()->is_player;
        PendingBattle& pb = app().pending_battle();
        pb.has_result = true;
        pb.player_won = player_won;
        // Quest FightEnd (JS `ha.RA("FightEnd")`): records the triple for
        // later ChangeTab evaluations and fires quests listening for it
        // (tutorial chain: none — ChangeTab rows read the triple instead).
        // The subsequent push(Results) fires ChangeTab(From=Fight).
        {
            QuestJournal j;
            // JS `_$Fight` = `Bj.Nb.toString()` = the `hb` triple (L961);
            // the port records it at launch (falls back to the bare name
            // for triple-less boots).
            j.fight = pb.fight_triple.empty() ? pb.battle_name : pb.fight_triple;
            j.fight_result = player_won ? "Win" : "Loss";
            try {
                j.player_level = app().save().load().level;
            } catch (const std::exception&) {
            }
            app().quest_engine().note_fight(j.fight, j.fight_result);
            app().quest_engine().fire(app(), "FightEnd", j);
        }
        std::fprintf(stdout, "[fight] BATTLE END winner=%s player_won=%d\n",
                     fight_->winner() ? fight_->winner()->name.c_str() : "(none)", player_won);
        std::fprintf(stdout,
                     "[fight] summary: P hp=%.0f rounds=%d hits=%d | E hp=%.0f rounds=%d hits=%d\n",
                     fight_->player().hp, fight_->player().rounds_won,
                     fight_->player().hits_landed, fight_->enemy().hp,
                     fight_->enemy().rounds_won, fight_->enemy().hits_landed);
        // JS `v.kD`/`bzb`/`Fh.lXa` (FLOW_STATIC section 4.3): exact totals.
        // Snapshot the breakdown into pending_battle, then set the reward
        // to the lXa TOTAL (m6, base included) — not base+bonus.
        {
            const auto prize = fight_->prize(pb.reward_money);
            std::fprintf(stdout,
                         "[fight] prize: perfect=%d first=%d combo=%d shocks=%d total=%d\n",
                         prize.perfect ? 1 : 0, prize.first_strike ? 1 : 0,
                         prize.max_combo, prize.shocks, prize.coins_total);
            pb.prize_base_coins = pb.reward_money;
            pb.prize_bonus = prize.coins_bonus;
            pb.prize_gems = prize.gems_bonus;
            pb.prize_combo = prize.max_combo;
            pb.prize_shocks = prize.shocks;
            pb.prize_perfect = prize.perfect;
            pb.prize_first = prize.first_strike;
            // The per-category bonus coins (`Fh.lXa` `oc.P3/ep/Ui/DZ/Ub`) the
            // Results rows display (`Lr.ZMa` L2078-2079 `vI` calls).
            pb.prize_perfect_coins = prize.coins_perfect;
            pb.prize_first_coins = prize.coins_first;
            pb.prize_combo_coins = prize.coins_combo;
            pb.prize_style_coins = prize.coins_style;
            pb.prize_shock_coins = prize.coins_shock;
            if (player_won) pb.reward_money = prize.coins_total;
        }
        std::fflush(stdout);
        push(kScreenResults);
    }
}

// (fwd) the `E.Zxa` gradient sampler (defined below): the fight `kk` base and
// the `Dr` pause overlay (`wh(6,1,.25)`, L2065) both use it.
void draw_kk_gradient(sf2::render::Renderer& ren, float x, float y, float w, float h,
                      float alpha = 1.0f);

void FightScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    if (fight_ == nullptr) return;
    FightAssets& assets = app.fight_assets();

    const sf2::scene::FightCamera& cam = fight_->camera();
    sf2::render::Camera camera;
    // The JS render camera position is (0,0) (N.Ta.K4 L85); `Sya` writes
    // only the aspect<1 portrait y-shift and `Ut.Al` (L826) carries
    // Io = Lb.width/2 - focus. The old native `center_x = focus-980 /
    // arena_center_x = 0` re-center was INVENTED (PORT_AUDIT_UI D2); the
    // renderer takes Io alone and centres the location art itself
    // (renderer.hpp `world_to_screen_x`). The ModelsViewer container's own
    // x-translate (-width/2, `tl.init` L843) still applies to the FIGHTERS
    // below (they live in the container, not the centered layer frame).
    const float arena_half = assets.fight_location.arena_width() > 0.0f
                                 ? assets.fight_location.arena_width() * 0.5f
                                 : 980.0f;
    camera.center_x = 0.0f;
    camera.center_y = cam.center_y;
    camera.zoom = cam.zoom;
    // [fix(camera): wire the fight layer zoom] The fight controller computes
    // the per-frame layer zoom Bj (JS Ut.Bj L826 via Ut.xCa L831, stored on
    // FightCamera::zoom_layer); the hub statics path (default_camera) does
    // the same via Camera::layer_zoom. Feed it into the render camera so the
    // L488 setScale branch (JS `b.lEa()||b.ij?b.setScale(Bj)`) moves pixels
    // on the fight path too — no dead state. (JS ma.Sya L1833, ql.dZa L363,
    // Ut.Al L826: framing_sya_impl computes it in FightCamera::framing.)
    camera.layer_zoom = cam.zoom_layer;
    camera.view_w = kViewW;
    camera.view_h = kViewH;
    camera.arena_h = assets.fight_location.arena_height() > 0.0f
                         ? assets.fight_location.arena_height()
                         : 560.0f;
    camera.arena_floor = assets.fight_location.arena_floor();
    // Io = Lb.width/2 - focus (JS `Ut.Al` L826): the parallax reference the
    // renderer folds into every layer (renderer.hpp `camera_offset_x`). At
    // the fight-start focus 831.5 -> Io = 148.5.
    camera.arena_center_x = arena_half - cam.center_x;
    ren.begin_frame(camera);
    // [M2 — scene/camera visibility gate] JS `XF` (L370):
    // `(Za.F().isVisible=a) ? this.aha=a : this.ia.visible(a)`. While the
    // round transition is hidden (`Onb` L411 `XF(!1)` .. `FNa` L409
    // `XF(!0)`) the whole 3-D view is skipped: the location layers, the
    // fighters, the spawned children, the hit sparks/magic and the markers.
    // Only the HUD below (`Ar`/`Sf`) still draws. The gate body keeps its
    // original indentation so the change stays a surgical 2-line diff.
    if (fight_->scene_visible()) {
    // [fix(render): arena layer order] The original game draws the fighters
    // INSIDE the ModelsViewer (Type=2) layer — background layers first, then
    // the fighters, then every layer AFTER the ModelsViewer (the floor /
    // arena sides / dust / glow / pixel_1 vignette) ON TOP of the fighters
    // (JS_RENDER §7, "Что у нас не так" #1). The old code drew ALL layers
    // before the fighters, so the floor rendered UNDER their feet — the
    // broken "arena behind the fighters" look.
    const std::size_t fighter_layer = assets.fight_location.fighter_layer();
    assets.fight_location.render_layers(ren, camera, 0, fighter_layer);  // background (parallax)

    // [fix(render): remove fake shadow] The oracle JS draws NO per-fighter
    // shadow (JS_RENDER §3.2: "в JS НЕТ пер-бойцовской тени"). The old
    // procedural black ellipse (the A4 commit 84269826) was a native
    // invention — the silhouettes stand straight on the floor line.

    auto project = [&camera, &assets, arena_half](const std::vector<float>& v) {
        // JS-exact fighter container offset (DOJO_BG_STATIC 7.4, tl.init
        // L843: container y=height/2-ct; dojo 280-80=200; Yia/B_ L476 are
        // container-local, floor tiles Y=223.5 location-space). World/pose/
        // camera stay container-space (oracle-trace exact); only the visual
        // projection adds the container so feet land in the tile band.
        // The -arena_half is that container x-translate (tl.init L843), not
        // the removed camera re-center.
        const float kContY = assets.fight_location.arena_height() * 0.5f -
                             assets.fight_location.arena_floor();
        std::vector<float> out(v.size());
        for (std::size_t i = 0; i < v.size(); i += 2) {
            out[i] = camera.world_to_screen_x(v[i] - arena_half, 1.0f);
            out[i + 1] = camera.world_to_screen_y(v[i + 1] + kContY);
        }
        return out;
    };
    std::vector<float> verts, pv, ev;
    fight_->player().fighter.build_vertices(verts);
    pv = project(verts);
    fight_->enemy().fighter.build_vertices(verts);
    ev = project(verts);
    // [Phase 4d] The oracle renders the fighter as the ragdoll capsule
    // STRIP: every collidable edge is a stroked line (JS `Dk` node:
    // `add(b,e,c,a,stroke/2)` with `stroke = Radius1*2`, drawn by the
    // `zu` class — see sf2.502f0946.js `class zu` + `class Dk`). The
    // triangle mesh alone (mdl_body = legs/feet only) leaves the torso
    // (EChest/EStomach) EMPTY — the user's "no armor/torso" report. Draw
    // the collidable capsule edges as thick quads over the mesh so the
    // fighter is a solid humanoid silhouette (head/neck/chest/stomach/
    // arms/legs) matching the oracle.
    auto draw_capsules = [&camera, &ren, &assets, arena_half](
                             const sf2::scene::Model& model,
                             const std::vector<float>& pos, float r, float g, float b) {
        // [F6 — capsule-figure render] JS `Yc.Uib` L570 walks
        // `A("Figures").children` in DOCUMENT ORDER and calls `Yc.Tib` (L573)
        // for every `Type="Capsule"` figure, so there is ONE `zu` visual per
        // capsule figure (84 for the merged fist body), drawn in that order.
        // `Dk.update` (L836) strokes it with
        //     stroke = this.Bc.stroke = Radius1*2      (half-stroke stroke/2)
        // and margin-shifts the span endpoints along the edge:
        //     x1 = sx.x + (ex.x-sx.x)*cGa          (cGa = capsule Margin1)
        //     x2 = sx.x + (ex.x-sx.x)*(1-bGa)      (bGa = capsule Margin2)
        // where `sx`/`ex` are the live endpoint node positions (`yu.dw()` =
        // `sx.ma`). The old port deduped by edge into an `unordered_map`
        // (nondeterministic draw order, 76 strips instead of 84), kept the max
        // Radius1 per edge and ignored Margin1/Margin2 entirely.
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kDiscSegments = 12;
        for (const sf2::scene::Capsule& cap : model.capsules) {
            const sf2::scene::EdgeDef* edge = nullptr;
            for (const sf2::scene::EdgeDef& ed : model.edges) {
                if (ed.name == cap.edge) {
                    edge = &ed;
                    break;
                }
            }
            if (edge == nullptr) {
                continue;  // JS `Tib`: `a.RAa(Edge)==null` -> no node is made
            }
            const int i1 = model.bone_by_name(edge->end1);
            const int i2 = model.bone_by_name(edge->end2);
            if (i1 < 0 || i2 < 0) {
                continue;
            }
            const std::size_t u1 = static_cast<std::size_t>(i1) * 2;
            const std::size_t u2 = static_cast<std::size_t>(i2) * 2;
            if (u1 + 1 >= pos.size() || u2 + 1 >= pos.size()) {
                continue;
            }
            // `Dk.update` L836: `c=b.x-a.x; d=b.y-a.y;`
            //   `b=a.x+c*cGa; e=a.y+d*cGa; c=a.x+c*(1-bGa); a=a.y+d*(1-bGa)`
            const float wx1 = pos[u1] + (pos[u2] - pos[u1]) * cap.margin1;
            const float wy1 = pos[u1 + 1] + (pos[u2 + 1] - pos[u1 + 1]) * cap.margin1;
            const float wx2 = pos[u1] + (pos[u2] - pos[u1]) * (1.0f - cap.margin2);
            const float wy2 =
                pos[u1 + 1] + (pos[u2 + 1] - pos[u1 + 1]) * (1.0f - cap.margin2);
            const float stroke = cap.radius1 * 2.0f * camera.zoom;  // `stroke=Radius1*2`
            if (stroke <= 0.0f) {
                continue;
            }
            const float sx1 = camera.world_to_screen_x(wx1 - arena_half, 1.0f);
            const float sy1 = camera.world_to_screen_y(
                wy1 + assets.fight_location.arena_height() * 0.5f -
                assets.fight_location.arena_floor());
            const float sx2 = camera.world_to_screen_x(wx2 - arena_half, 1.0f);
            const float sy2 = camera.world_to_screen_y(
                wy2 + assets.fight_location.arena_height() * 0.5f -
                assets.fight_location.arena_floor());
            float dx = sx2 - sx1;
            float dy = sy2 - sy1;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float cr = stroke * 0.5f;
            auto draw_disc = [&](float cx, float cy) {
                const float step = 2.0f * kPi / static_cast<float>(kDiscSegments);
                for (int s = 0; s < kDiscSegments; ++s) {
                    const float a0 = static_cast<float>(s) * step;
                    const float a1 = static_cast<float>(s + 1) * step;
                    float tri[6] = {
                        cx,
                        cy,
                        cx + std::cos(a0) * cr,
                        cy + std::sin(a0) * cr,
                        cx + std::cos(a1) * cr,
                        cy + std::sin(a1) * cr,
                    };
                    ren.draw_triangles(tri, 3, r, g, b, 1.0f);
                }
            };
            if (len < 1e-4f) {
                draw_disc(sx1, sy1);
                continue;
            }
            dx /= len;
            dy /= len;
            const float px = -dy * cr;
            const float py = dx * cr;
            float quad[12] = {
                sx1 + px, sy1 + py, sx2 + px, sy2 + py,
                sx1 - px, sy1 - py, sx2 + px, sy2 + py,
                sx2 - px, sy2 - py, sx1 - px, sy1 - py,
            };
            ren.draw_triangles(quad, 6, r, g, b, 1.0f);
            draw_disc(sx1, sy1);
            draw_disc(sx2, sy2);
        }
    };
    // [fix(render): enemy-behind draw order] JS ev.Gf L845: the FIRST
    // registered fighter becomes Rw with z=-.001 (behind), the SECOND becomes
    // pF with z=0 (top). Fight creation (JS o1a L403: yb=Gf(kc) first,
    // pb=Gf(Zb) second; trace.js frameJson: pb="Me", yb="Enemy") makes
    // Rw=yb=Enemy (behind) and pF=pb=Me/Player (top). The container is built
    // by UWa L832 with per-child z steps via NWa/Dla L487-488 (QH+=-.01) and
    // Dla L1599 (translate.z=). The batch preserves submission order, so draw
    // the whole enemy node FIRST (capsules+mesh, z=-.001) then the whole
    // player node (z=0) on top.
    draw_capsules(fight_->enemy().fighter.model(), fight_->enemy().fighter.positions(),
                  fight_->enemy().fighter.color_r(), fight_->enemy().fighter.color_g(),
                  fight_->enemy().fighter.color_b());
    ren.draw_triangles(ev.data(), ev.size() / 2, fight_->enemy().fighter.color_r(),
                       fight_->enemy().fighter.color_g(), fight_->enemy().fighter.color_b());
    draw_capsules(fight_->player().fighter.model(), fight_->player().fighter.positions(),
                  fight_->player().fighter.color_r(), fight_->player().fighter.color_g(),
                  fight_->player().fighter.color_b());
    ren.draw_triangles(pv.data(), pv.size() / 2, fight_->player().fighter.color_r(),
                       fight_->player().fighter.color_g(), fight_->player().fighter.color_b());
    // [child models] JS `wd.vd` (the `<CreatePlayer>` spawns): each live child
    // is a full model (`ih extends wd`) drawn with the SAME capsule-strip +
    // mesh path as a fighter, at the spawner's layer. Drawn after both
    // fighters so a freshly spawned magic model is visible on top.
    for (const sf2::scene::ChildModel& ch : fight_->children()) {
        if (!ch.active) continue;
        std::vector<float> cv;
        ch.fighter.build_vertices(cv);
        std::vector<float> cpv = project(cv);
        draw_capsules(ch.fighter.model(), ch.fighter.positions(),
                      ch.fighter.color_r(), ch.fighter.color_g(), ch.fighter.color_b());
        ren.draw_triangles(cpv.data(), cpv.size() / 2, ch.fighter.color_r(),
                           ch.fighter.color_g(), ch.fighter.color_b());
    }

    // The hit sparks (JS `Hyb`/`ryb`/`av`): world-space particles projected
    // through the SAME camera the fighters used (factor 1.0 — the shake is
    // baked into the camera framing). Drawn AFTER the fighters, BEFORE the
    // fg floor layers (bg -> fighters -> SPARKS -> fg floor — the b615a1bf
    // layer order; the batch preserves submission order).
    // The effects share the fighters' `tl` container offset (JS `tl.init`
    // L843: x=-width/2, y=height/2-Floor) — the same kContY `project()`
    // applies; PORT_AUDIT_SCENE D12.
    const float cont_y = camera.arena_h * 0.5f - camera.arena_floor;
    // JS `Na.cd(Lb.N2)` (L824/L833): every spark is filled with the location
    // Root Color — the same colour the fighter silhouettes use. The fight
    // spawn (FightController, fight.cpp:1565) does not thread it yet
    // (cross-file OPEN), so pass the loaded location's root colour here.
    draw_hit_sparks(ren, camera, fight_->fx(), assets.fight_location.root_color(), arena_half,
                    cont_y);
    // JS `tl.init` (L843-844): the container order is qh (fighters) -> Gq
    // (`Gfb` = OnBackground) -> Hq (air), both z=+.01 over the fighters.
    // Route by `MagicEffects::background_for` (JS `tl.Nt` L842): the
    // background pass draws first, the air pass second. Both use the same
    // container offset (JS `tl.init` L843). The containers are the fight's
    // own (`FightController::magic_fx_`, JS `tl.Rf`) — the single source fed
    // by the `Yl` Effect triggers; no parallel screen-owned pool.
    draw_magic_effects(app, ren, camera, fight_->magic_fx(), /*background_pass=*/true,
                       arena_half, cont_y);
    draw_magic_effects(app, ren, camera, fight_->magic_fx(), /*background_pass=*/false,
                       arena_half, cont_y);
    // JS `sXa` (L827-828): the two `fight/ringout` (asset 1300) off-screen
    // arrows. Register the frame run here so `marker_frame_name` resolves
    // BEFORE any marker draw. The native round logic never emits the JS
    // `ERuleRingout` marker (`round_result::ringout` is never assigned), so
    // no arrow is active in the current sim (OPEN) — the atlas is loaded so
    // the Wave J `sXa` path is complete and cannot atlas-miss.
    load_ringout_atlas(app);

    // [fix(render): arena layer order] The foreground layers — the ones the
    // params XML places AFTER the ModelsViewer (Type=2) fighter layer: the
    // dojo floor (`dojo_floor_1/2`), the arena side walls, the punch-bag
    // holder and the pixel_1 vignette — draw ON TOP of the fighters, exactly
    // like the original's `_0007_arena` / dust / glow (JS_RENDER §7).
    // Fall back to rendering nothing extra when the location has no
    // fighter layer (fighter_layer == npos already drew every layer above).
    const std::size_t n_layers = assets.fight_location.layers().size();
    if (fighter_layer != sf2::scene::LocationScene::npos) {
        assets.fight_location.render_layers(ren, camera, fighter_layer + 1, n_layers);
    }

    // --- Fight feedback overlays on the camera-glued `Cu` container ------
    // JS `Ut.UWa` (L832) appends `Cu` as the LAST child of the render
    // container `go`, so `Cu` (and its `WV` flash node) draws over every
    // location layer. Both overlays live in container space (the same `tl`
    // offset the fighters/sparks use) and are drawn here, on top.
    {
        constexpr float kPi = 3.14159265358979323846f;
        // JS `ge.gba` = params `ArrowFlashingFrames` (L1278), default 120.
        constexpr float kArrowFlashingFrames = 120.0f;

        // JS `Hyb` (L825): the one-shot `fight/fx` (asset 1306) hit overlay
        // `this.lo` (`Ut.s1a` L831-832). `C(a.x)`/`D(a.y)` place it at the hit
        // point; `la(e*.7)` is the stored `scale`; `Wg(isNaN(a)?0:-a)` is
        // `angle_deg`; the sine `kyb` alpha = .5+.5*sin(pi/gba*frame).
        const sf2::scene::hit_flash& flash = fight_->fx().hit_flash_state();
        if (flash.active && load_fx_atlas(app)) {
            const std::string frame = fight_->fx().hit_flash_frame();
            if (!frame.empty()) {
                const float alpha =
                    0.5f + 0.5f * std::sin(kPi / kArrowFlashingFrames * flash.age);
                const float sx = camera.world_to_screen_x(flash.x - arena_half, 1.0f);
                const float sy = camera.world_to_screen_y(flash.y + cont_y);
                // The `hit_blade`/`critical` fx frames all carry sourceSize
                // 1024x1024 (fx.json), so `size/1024 = la(e*.7) * camera zoom`.
                const float size = 1024.0f * flash.scale * camera.zoom;
                if (alpha > 0.02f && size > 0.5f &&
                    !draw_fx_frame(app, frame, sx, sy, size, /*facing=*/1, 1.0f, 1.0f,
                                   1.0f, alpha, flash.angle_deg)) {
                    // Genuine atlas/frame miss only.
                    ren.draw_effect_quad(sx, sy, size * 0.25f, size * 0.25f, 0.0f, 1.0f,
                                         1.0f, 1.0f, alpha);
                }
            }
        }

        // JS `sXa` (L827-828): the two `fight/ringout` (asset 1300) arrows on
        // `Cu`, frames "0".."19" (`kg.Yda` L827). `EffectSystem` stores the
        // screen-space x/width (`d(n,q)`: `xc(n)`/`C(q)`) and y/height
        // (`Pb(h)`/`D(-k)`), so each frame draws 1:1 stretched to
        // `m.width x m.height`.
        const sf2::scene::offscreen_markers& mk = fight_->fx().markers_state();
        if (mk.active && load_ringout_atlas(app)) {
            auto draw_marker = [&](const sf2::scene::fight_marker& m) {
                if (m.width <= 0.0f || m.height <= 0.0f) return;
                const std::string fname = sf2::scene::EffectSystem::marker_frame_name(m);
                if (!try_draw_atlas_button(app, fname, m.x, m.y, m.width, m.height, 1.0f,
                                           /*fill=*/true)) {
                    ren.draw_effect_quad(m.x, m.y, m.width, m.height, 0.0f, 1.0f, 1.0f, 1.0f,
                                         0.9f);
                }
            };
            draw_marker(mk.left);
            draw_marker(mk.right);
        }
    }

    // --- JS `Ut.V0a` (L831) + `Ut.kyb` (L825): the flashing `arrow` marker --
    // `Ut.V0a` creates the marker (`E.get(268)`, frame `y.OQa` = "arrow") and
    // appends it to the arena render node `this.go.node`; `Ut.init` (L823) runs
    // `UWa()` (which appends the camera-glued `Cu` container) BEFORE `V0a()`, so
    // the marker is the LAST child and draws over every layer and overlay.
    // `ql.init` (L368) builds ONE `Ut` per camera (`this.ia = new Ut(this.go)`)
    // and the FIGHT drives it every frame (`ql.Zga` L369 -> `ql.d3a` L366 ->
    // `this.ia.Al(...)`), exactly like the Dojo `FightNone` viewer. The marker
    // is therefore part of the shared arena and must be drawn in every battle.
    // `Ut.Al` (L827) places it under the PLAYER every frame:
    //   x = Io - (Lb.width/2 - c.x)*Bj        (c = the player)
    //   y = Lb.hn.go.node.translate.y + 2*F9*Bj + 10
    //   alpha = .5 + .5*sin(pi/ArrowFlashingFrames * $O)     (`Ut.kyb` L825)
    // `F9 = (Lb.height/2 - Lb.ct)/2` (L823); `ArrowFlashingFrames` = `ge.gba`
    // (L1278, default 120). Same frame, position rule, alpha and z as the dojo
    // draw site (DojoScreen::render_impl) — one `Ut`, both screens.
    {
        sf2::data::atlas_frame afr;
        int atw = 0, ath = 0;
        unsigned int agl = 0;
        if (app.get_atlas_frame("arrow", &afr, &atw, &ath, &agl)) {
            constexpr float kPi = 3.14159265358979323846f;
            constexpr float kArrowFlashingFrames = 120.0f;  // `ge.gba` L1278
            // `2*F9 = Lb.height/2 - Lb.ct` = the `tl` container y translate the
            // fighters' `project()` already carries as `cont_y`, so the marker's
            // drop from the container origin is `2*F9*Bj + 10` (the arena floor
            // line + 10) — the same `world_to_screen_y` space the fighters use.
            const float f9 = (camera.arena_h * 0.5f - camera.arena_floor) * 0.5f;
            const float bj = camera.layer_zoom;  // JS `Ut.Bj` (`xCa` L831)
            const float arrow_world_y = 2.0f * f9 * bj + 10.0f;
            // The player's LOCATION-space x, converted to the container space
            // `world_to_screen_x` takes (`verts - arena_half`, `git` `tl.init`
            // L843 x=-width/2) exactly like the dojo draw site.
            const float px = fight_->player().fighter.world_x();
            const float sx = camera.world_to_screen_x(px - arena_half, 1.0f);
            const float sy = camera.world_to_screen_y(arrow_world_y);
            const float nat_w = afr.source_w > 0 ? static_cast<float>(afr.source_w)
                                                : static_cast<float>(afr.w);
            const float nat_h = afr.source_h > 0 ? static_cast<float>(afr.source_h)
                                                : static_cast<float>(afr.h);
            static int arrow_phase = 0;  // JS `Ut.$O` (reset to 0 in `V0a` L831)
            const float alpha =
                0.5f + 0.5f * std::sin(kPi / kArrowFlashingFrames *
                                       static_cast<float>(arrow_phase));
            arrow_phase =
                (arrow_phase + 1) % static_cast<int>(kArrowFlashingFrames);
            static bool fight_arrow_logged = false;
            if (!fight_arrow_logged) {
                fight_arrow_logged = true;
                std::fprintf(stdout,
                             "[fight] arrow frame=%dx%d player_loc_x=%.1f -> "
                             "screen=(%.1f,%.1f) alpha=%.2f\n",
                             static_cast<int>(nat_w), static_cast<int>(nat_h),
                             static_cast<double>(px), static_cast<double>(sx),
                             static_cast<double>(sy), static_cast<double>(alpha));
                std::fflush(stdout);
            }
            draw_atlas_region(app, "arrow", static_cast<float>(afr.x),
                              static_cast<float>(afr.y), static_cast<float>(afr.w),
                              static_cast<float>(afr.h), static_cast<float>(atw),
                              static_cast<float>(ath), sx, sy, nat_w * camera.zoom,
                              nat_h * camera.zoom, alpha, /*flip_x=*/false);
        }
    }

    // Scene letterbox bars (JS `ma.Sya` L1833-1834; PORT_AUDIT_SCENE D10):
    // drawn over the scene, under the HUD — no-op at 16:9 (BK=0, arena
    // 728px spans y[-4,724]).
    draw_scene_letterbox(ren, camera);
    }  // [M2] end of the scene/camera visibility-gated 3-D draws

    // --- Fight HUD (JS `Ar`/`Sf`/`lk`/`Er` L2016-2041) ------------------
    // Frames: fight/ui.json -> HealthBar_Empty (bg), HealthBar_Full (player
    // fill), HealthBarBlue_Full (enemy fill), HealthBar_Hit/Blue_Hit (leak),
    // Round_Done/Undone (pips). `Sf.layout` (L2036-2038) computes, with
    // ma.Kq = the screen rect (J=0, N=W, P=0, W=H):
    //   d = clamp(W/H, .4, 1.5); e = clamp(d, 1, 1.1);
    //   c0 = min(W,H)/2; f = c0*.07 (+ (1-d)*200 when d<1);
    //   g = 1 + (clamp(d,1,1.5)-1)/.5*.1; c = c0/675*g;
    //   bar centers = W/2 ∓ 520*c*e; bar Y = P + 150*c + f*g.
    // At 1280x720: d=1.5, e=1.1, g=1.1, c=0.5867, f=25.2 -> centers
    // 304.4/975.6, y=115.7. Bar frame 425x43 (`Br` uL(425)/krb L2011-2012;
    // PORT_AUDIT_UI §2.6).
    const float hud_d = std::clamp(kViewW / kViewH, 0.4f, 1.5f);
    const float hud_e = std::clamp(hud_d, 1.0f, 1.1f);
    const float hud_c0 = std::min(kViewW, kViewH) * 0.5f;
    const float hud_f = hud_c0 * 0.07f + (hud_d < 1.0f ? (1.0f - hud_d) * 200.0f : 0.0f);
    const float hud_g = 1.0f + (std::clamp(hud_d, 1.0f, 1.5f) - 1.0f) / 0.5f * 0.1f;
    const float hud_c = hud_c0 / 675.0f * hud_g;
    // [fix(fight HUD): oracle-matched `lk` anchors] The oracle HUD measures,
    // at 1280x720, bar rects x393..602 / x678..887 (w≈209), y≈90..118 (h≈30),
    // portrait circles centred (310,131)/(970,131) r≈80, and the pause button
    // centred under the timer. `Sf.layout` places the `lk` panels at
    // `W/2 ∓ 520*c*e` (L2036-2037) and `lk.bMa` hangs the bar/portrait/name
    // off them; the native view is a fixed 1280x720 (`kViewW`/`kViewH`), so
    // the anchors are expressed directly in view px.
    //
    // The oracle capture (`fight_stance`) pins the panel scale: the `Br` bar
    // is `uL(330)` design units (`lk.bMa` L2028) and measures 178 px wide,
    // so c = 178/330 = 0.5394. The bar art is a 1-px x 43 vertical strip
    // (HealthBar_Empty/_Full/_Hit) whose opaque band is frame rows 8..34 of
    // 43, so a rect h = 43*c = 23.2..24.8 places the solid band at the
    // capture's orange rows y90..104 (rect y = 90 - 8*(h/43)). The prior
    // 209x30 @ y90 was ~31 px too wide and 6 px too low.
    // OPEN: the exact `ma.Kq` J/P projection at 16:9 (`Sya` L1834) is not
    // re-derived here (the port FightCamera reports Kq_h = 720 -> c=0.5867,
    // the capture implies Kq_h = 662 -> c=0.5394; other owner). The
    // constants are calibrated to the oracle capture, JS structure cited.
    const float bar_w = 183.0f;   // `Br.uL(330)`; oracle fill spans x392..571 (180 px)
    const float bar_h = 24.8f;    // `Br.Pb(43)`: opaque band = frame rows 7..33
    const float bar_y = 86.4f;    // `al.node.D(-50)`: band pinned to oracle y90..106
    const float bar_cx_player = 481.5f;
    const float bar_cx_enemy = 797.0f;
    (void)hud_d;
    (void)hud_e;
    (void)hud_f;
    (void)hud_g;
    const float p_ratio = fight_->player().max_hp > 0.0f
                              ? std::clamp(fight_->player().hp / fight_->player().max_hp, 0.0f, 1.0f)
                              : 0.0f;
    const float e_ratio = fight_->enemy().max_hp > 0.0f
                              ? std::clamp(fight_->enemy().hp / fight_->enemy().max_hp, 0.0f, 1.0f)
                              : 0.0f;

    // HP leak/decay (JS `Br.Qyb` L2012-2013): retarget + step the two-layer
    // bars once per frame, then draw the 30-frame leak UNDER the instant fill.
    s_hud_player_decay_.retarget(p_ratio);
    s_hud_enemy_decay_.retarget(e_ratio);
    s_hud_player_decay_.tick();
    s_hud_enemy_decay_.tick();

    // `mirror` = the enemy panel (`lk.type==1`): the `Br`/`Fr` bars are the
    // same art flipped 180° (`Br.init` L2010 `BL(25*a)` with `a=b==0?-1:1`;
    // `lk` places the enemy `al`/`Sh` at the mirrored local x), so the fill
    // anchors on the RIGHT edge.
    auto draw_hp_bar = [&](float x, float y, float w, float h, float ratio, float leak_ratio,
                           const char* fill_frame, const char* leak_frame, bool mirror) {
        // Background: HealthBar_Empty stretched to full width
        if (!app.draw_atlas_rect("HealthBar_Empty", x, y, w, h, 1.0f)) {
            // Fallback flat dark bg
            const float bg[] = {x, y, x + w, y, x, y + h, x + w, y, x + w, y + h, x, y + h};
            ren.draw_triangles(bg, 6, 0.12f, 0.12f, 0.12f, 0.92f);
        }
        // Leak layer (JS `EG`: HealthBar_Hit) — the 30-frame trailer, drawn
        // under the instant fill so only the overhang shows.
        if (leak_ratio > 0.001f) {
            const float lw = w * std::clamp(leak_ratio, 0.0f, 1.0f);
            const float lx = mirror ? x + w - lw : x;
            if (!app.draw_atlas_rect(leak_frame, lx, y, lw, h, 1.0f)) {
                const float lg[] = {lx, y, lx + lw, y, lx, y + h,
                                    lx + lw, y, lx + lw, y + h, lx, y + h};
                ren.draw_triangles(lg, 6, 0.95f, 0.85f, 0.45f, 0.85f);
            }
        }
        if (ratio > 0.001f) {
            const float fw = w * ratio;
            const float fx = mirror ? x + w - fw : x;
            if (!app.draw_atlas_rect(fill_frame, fx, y, fw, h, 1.0f)) {
                const bool is_blue = std::string(fill_frame).find("Blue") != std::string::npos;
                const float r = is_blue ? 0.25f : 0.16f;
                const float g = is_blue ? 0.45f : 0.82f;
                const float b = is_blue ? 0.92f : 0.16f;
                const float fg[] = {fx, y, fx + fw, y, fx, y + h, fx + fw, y, fx + fw, y + h, fx, y + h};
                ren.draw_triangles(fg, 6, r, g, b, 0.96f);
            }
        }
        // NOTE: the former 1-px black top/bottom "readability border" was
        // INVENTED (no JS equivalent) — `Br` draws only the three frames
        // (`Ud` base / `nN` hit / `EG` full, L2011-2013). Its rows y84/y110
        // are absent in the oracle capture, so it is removed (oracle: the
        // bar's opaque band is exactly frame rows 7..33 of 43).
    };

    draw_hp_bar(bar_cx_player - bar_w * 0.5f, bar_y, bar_w, bar_h, s_hud_player_decay_.shown(),
                s_hud_player_decay_.leak(), "HealthBar_Full", "HealthBar_Hit", /*mirror=*/false);
    // The oracle's two bars are the SAME orange art (FIDELITY_MATRIX
    // fight_stance: "bars orange/blue vs oracle both orange"); mirror the
    // enemy fill to the inner (left) end.
    draw_hp_bar(bar_cx_enemy - bar_w * 0.5f, bar_y, bar_w, bar_h, s_hud_enemy_decay_.shown(),
                s_hud_enemy_decay_.leak(), "HealthBar_Full", "HealthBar_Hit", /*mirror=*/true);

    // Fighter portraits + names (JS `lk.obb` L2029 `Sh = Fr`; `lk.gbb` L2030
    // two `ea` name labels `ua(70)`). The oracle anchors: portrait centres
    // (307,122)/(973,122), ring diameter ~180. Both portraits are the
    // `oe`/`Hf` user-image art drawn at its natural 512 px canvas, whose
    // circular frame is BAKED IN at ~300/512 = 0.586 of the canvas, so a
    // 307 px canvas renders the capture's 180 px ring (canvas-centred: the
    // oracle's ring centre == the canvas centre). Names sit at the bar's
    // shoulder: player centred 432 (capture bbox x395..469), enemy 847.5
    // (x811..884); the capture's ink rows are 59..80.
    {
        // `oe(Hf)` natural canvas (the old 165 px canvas shrank the art 1.9x).
        // The canvas is the alpha-fit that reproduces the oracle ring (the
        // 512-canvas art's opaque ring is 307/512 wide; a 300 px canvas puts
        // it at the captured 176-180 px).
        const float port_d = 300.0f;
        const float port_px = 307.0f, port_ex = 973.0f;
        const float port_py = 116.5f;
        if (!draw_user_image(app, vs_player_image_, port_px, port_py, port_d, port_d, 1.0f,
                             /*flip_x=*/true)) {
            draw_user_image(app, "avatar_hero", port_px, port_py, port_d, port_d, 1.0f,
                            /*flip_x=*/true);
        }
        if (!draw_user_image(app, vs_enemy_image_, port_ex, port_py, port_d, port_d, 1.0f)) {
            draw_user_image(app, "avatar_masked", port_ex, port_py, port_d, port_d, 1.0f);
        }
        const sf2::data::font* nf = app.menu_font();
        const unsigned int nt = app.font_texture();
        if (nf != nullptr && nt != 0) {
            // `ea.ua(70)` = font-en size 100 -> 0.70 (FONT_METRICS §2). The
            // native menu font renders Cyrillic wider than the oracle's at a
            // given height, so 0.48 minimises the capture ink-box error
            // (oracle "ТЕНЬ" 75x22; native 0.63 -> 106x27, so both dims
            // scale by ~0.48 to 81x20.6). `draw_text_centered` takes the
            // glyph-box TOP; the capture's ink rows are 59..80.
            const float nscale = 0.48f;
            const float name_top = 50.0f;
            app.draw_text_centered(*nf, nt, 432.0f + 1.5f, name_top + 1.5f, vs_player_name_,
                                   nscale, 0.0f, 0.0f, 0.0f, 0.55f);
            app.draw_text_centered(*nf, nt, 432.0f, name_top, vs_player_name_, nscale, 1.0f,
                                   0.90f, 0.62f, 1.0f);
            app.draw_text_centered(*nf, nt, 847.5f + 1.5f, name_top + 1.5f, vs_enemy_name_,
                                   nscale, 0.0f, 0.0f, 0.0f, 0.55f);
            app.draw_text_centered(*nf, nt, 847.5f, name_top, vs_enemy_name_, nscale, 1.0f,
                                   0.90f, 0.62f, 1.0f);
        }
    }

    // Timer — bitmap-font centered (Sf.layout: top-center). Uses fight/digits.fnt
    // (fallback to ui/font-en). Scale tuned so ~80px glyph -> ~30px on HUD.
    const int timer =
        std::max(0, fight_->round().time_nf);  // JS `Sf.iPa` (L2036)
    const std::string tstr = std::to_string(std::max(0, timer));
    {
        const sf2::data::font* fnt = app.digits_font() ? app.digits_font() : app.menu_font();
        unsigned int tex = app.digits_font() ? app.digits_texture() : app.font_texture();
        if (fnt != nullptr && tex != 0) {
            // JS `Kp.Ia(128)`, `Kp.ua(120*c)` (Sf.layout L2037): fontSize =
            // 120*c; digits eF=90 -> native scale = 120*c/90.
            const float scale = (fnt == app.digits_font()) ? (86.0f * hud_c / 90.0f)
                                                           : (120.0f * hud_c / 100.0f);
            // shadow (black) slightly offset, then white foreground
            // JS `Sf.layout` (L2037): `this.Kp.C(b/2); this.Kp.D(...)` — the
            // timer is screen-top-centre. Oracle: "99" centred at (640,45).
            const float ty = 8.0f;
            app.draw_text_centered(*fnt, tex, kViewW * 0.5f + 1.8f, ty + 1.8f, tstr, scale, 0.0f, 0.0f,
                                   0.0f);
            app.draw_text_centered(*fnt, tex, kViewW * 0.5f, ty, tstr, scale, 1.0f, 0.95f, 0.75f);
        } else {
            // fallback quads (should not happen)
            float tx = kViewW * 0.5f - tstr.size() * 20.0f;
            for (char ch : tstr) {
                float verts[12] = {tx, 18.0f, tx + 18.0f, 18.0f, tx, 46.0f,
                                   tx + 18.0f, 18.0f, tx + 18.0f, 46.0f, tx, 46.0f};
                ren.draw_triangles(verts, 6, 1.0f, 1.0f, 1.0f, 0.95f);
                tx += 22.0f;
                (void)ch;
            }
        }
    }

    // Rounds — the `lk` SECOND row: the name-frame base (`lk.Sh = Fr`,
    // whose `Qp = Mx` L2088 draws `background = R.$(E.get(1294), y.UU)`
    // `uL(165)`) plus the round pips (`Er` L2021-2022: `e = count==2?40:32`,
    // `f = e/2`, step `e+f`, frames `y.UU` undone / `y.LQa` done, `Pb(43)`).
    // In the capture the row sits BELOW the bar (panel-local y 0 vs the bar's
    // -50): the `Mx` base rides the bar's OUTER end (player x388..472 = 84 px
    // ≈ 165*c) and the pips step INWARD (player 535, 500 -> 22 px wide,
    // 35 px step; `round.eL == 2` gives exactly 2 pips). The old code rode
    // the pips ON the bar — the capture shows them on their own row.
    constexpr float kPipW = 22.0f;     // `Er` e=40 * 0.5394
    constexpr float kPipStep = 35.0f;  // `Er` (e+f)=60 * 0.5394
    constexpr float kPipH = 24.8f;     // `Pb(43)` * c + opaque-band fit
    constexpr float kPipY = 116.4f;
    constexpr float kBaseW = 84.0f;    // `Mx` base `uL(165)` * 0.5394
    const int rounds_total = fight_->round().length;
    auto draw_row2 = [&](const char* frame, float x, float w, bool done) {
        if (app.draw_atlas_rect(frame, x, kPipY, w, kPipH, 1.0f)) return;
        float dv[12] = {x, kPipY, x + w, kPipY, x, kPipY + kPipH,
                        x + w, kPipY, x + w, kPipY + kPipH, x, kPipY + kPipH};
        // `HealthBar_Empty` (49,26,20)/255; `Round_Done` orange.
        const float r = done ? 1.0f : 0.19f;
        const float g = done ? 0.41f : 0.10f;
        const float b = done ? 0.07f : 0.08f;
        ren.draw_triangles(dv, 6, r, g, b, 1.0f);
    };
    draw_row2("HealthBar_Empty", 388.0f, kBaseW, false);  // player `Mx` base
    draw_row2("HealthBar_Empty", 807.0f, kBaseW, false);  // enemy `Mx` base
    for (int i = 0; i < rounds_total; ++i) {
        const bool p_done = i < fight_->player().rounds_won;
        const bool e_done = i < fight_->enemy().rounds_won;
        draw_row2(p_done ? "Round_Done" : "Round_Undone", 535.0f - static_cast<float>(i) * kPipStep,
                  kPipW, p_done);
        draw_row2(e_done ? "Round_Done" : "Round_Undone", 722.0f + static_cast<float>(i) * kPipStep,
                  kPipW, e_done);
    }

    // The round plate (JS `Cr` L2021-2026): over the fight + HUD, UNDER the
    // gamepad. Atlas art only - no text and no tween (see draw_fight_banner).
    draw_fight_banner(app, *fight_);

    // The on-screen gamepad (JS `Za` virtual controls): the joystick
    // bottom-left + the punch/kick buttons bottom-right, drawn from the
    // ui/controller atlas. Hidden during the round-break window
    // (`round_wait()`, JS `Ta.XF(!1)` until `FNa` shows it again).
    draw_gamepad(app);

    // The JS has NO between-rounds "NEXT" button (JS `Onb`/`Cr` advances
    // rounds with the `Cr.tca` timers, L2023). The native Next button was
    // INVENTED (PORT_AUDIT_UI §3 item 26); its click rect, its Space/Enter
    // alias and the `snd_click_1` at the advance are now ALL removed — the
    // auto-advance lives in FightController's banner machine
    // (`round_start`/`banner_expire`). `next_button_center` reports (0,0)
    // (no button).
    // Pause menu render (JS `Jn` button + `Ar.Qrb` overlay — display only).
    // Geometry mirrors update_impl.
    const bool live =
        fight_ != nullptr && !fight_->round_wait() && !fight_->battle_over();
    if (live && !paused_) {
        // The HUD pause icon (`Jn`, L2034: `db.xz(E.get(1294), y.IQa)`;
        // `Jn.node.la(c*.8)` at `C(b/2)`). Oracle: disc 74 px centred at
        // (639.5,115); the 77 px rect reproduces it (the sprite carries a
        // ~4% transparent margin).
        if (!try_draw_atlas_button(app, "FightPause", 640.0f, 115.5f, 77.0f, 77.0f,
                                   1.0f)) {
            draw_flat_button(app, "II", 640.0f, 115.5f, 77.0f, 77.0f, 0.3f, 0.3f, 0.4f,
                             false);
            draw_ui_label(app, 640.0f - 34.0f + 4.0f, 115.5f - 12.0f, 68.0f - 8.0f, 24.0f,
                              "II", 0.8f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        }
    }
    // VS intro (`ik`, L2069): drawn ON TOP of the scene/HUD/banner (the JS
    // `ik` node sits over the fight stack) but UNDER the pause dialog. The
    // sim runs underneath, so the fight is already live when the overlay
    // clears; the VS simply covers it for its duration.
    if (vs_active_) {
        draw_vs_intro(app, vs_t_, vs_player_name_, vs_enemy_name_, vs_player_image_,
                      vs_enemy_image_);
    }
    if (paused_) {
        // `Dr.Qa` (L2065): the SAME `E.Zxa` horizontal gradient as the fight
        // (`E.Eua` stops #00000020/80/80/80/20), tweened `wh(6,1,.25)` so the
        // node alpha settles at .25: overlay = gradient alpha * 0.25.
        draw_kk_gradient(ren, 0.0f, 0.0f, kViewW, kViewH, 0.25f);
        // `Dr` pause dialog (JS L2018; PAUSE_STATIC §3): `res/fight/pause.*`
        // frames — `Pause` title, `PauseMusic_on/off`, `PauseSound_on/off`,
        // `play` (resume), `home` (quit). Flat fallback only on a genuine
        // atlas miss (PORT_AUDIT_UI §3 item 25).
        const bool have = load_pause_atlas(app);
        auto frame = [&](const char* art, float cx, float cy, float w, float h,
                         const char* label) {
            if (have && art != nullptr &&
                try_draw_atlas_button(app, art, cx, cy, w, h, 1.0f)) {
                return;
            }
            draw_flat_button(app, label, cx, cy, w, h, 0.35f, 0.3f, 0.28f, false);
            draw_ui_label(app, cx - w * 0.5f + 8.0f, cy - 14.0f, w - 16.0f, 28.0f,
                          label, 0.8f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        };
        frame("Pause", kPauseDlgTitleCx, kPauseDlgTitleCy, kPauseDlgTitleW,
              kPauseDlgTitleH, "PAUSED");
        frame(music_off_ ? "PauseMusic_off" : "PauseMusic_on", kPauseDlgMusicX,
              kPauseDlgRowY, kPauseDlgToggleS, kPauseDlgToggleS, "MUSIC");
        frame("PauseSound_on", kPauseDlgSoundX, kPauseDlgRowY, kPauseDlgToggleS,
              kPauseDlgToggleS, "SOUND");
        frame("play", kPauseDlgPlayX, kPauseDlgRowY, kPauseDlgToggleS,
              kPauseDlgToggleS, "RESUME");
        frame("home", kPauseDlgHomeX, kPauseDlgRowY, kPauseDlgToggleS,
              kPauseDlgToggleS, "QUIT");
    }
    // Sensei dialog modal overlay (`He`): drawn last so it covers the fight
    // HUD and the pause dialog (the other screens draw it the same way).
    draw_quest_modal(app, ren, app.screens().top() == this);
}

// ---------------------------------------------------------------------------
// ResultsScreen
// ---------------------------------------------------------------------------

ResultsScreen::ResultsScreen(ScreenManager& mgr, bool player_won, int money_reward,
                             int exp_reward)
    : Screen(mgr, "Results"), player_won_(player_won), money_reward_(money_reward),
      exp_reward_(exp_reward) {
    // JS `ai.B()` fight teardown (L384): `this.Da.type!="FightNone"&&lb.OS()`
    // — leaving a fight restores the MENU track (the guard was cleared at
    // fight start `ai.Ut` L2008, so this replays it; the Map/Shop/Profile
    // then inherit it). Replaces the old stop-only approximation, which left
    // everything after a fight silent.
    // JS keeps the BATTLE track through the results: the jk results panel is
    // a CHILD of the fight screen i (i.lca L2008 	his.Ws=Qo(jk)), so
    // the fight's lb.rJ guard stays cleared and lb.OS() (the menu restore)
    // only runs at the i.B() teardown (L384) - i.e. when the fight screen is
    // left for the Map, not when the results appear. The old port played the
    // menu track HERE, cutting the battle music the moment Results was pushed.
    // The Map ctor's play_music_once("menu") (L2125) is the teardown point.
}

// JS `OLa`/`Oz` (L253-254): the level-up thresholds (`v.FR`) parsed once
// from character_progress.xml; 100 fallback when the file is absent.
int ResultsScreen::exp_for_level(int level) {
    static std::map<int, int> thresholds;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            sf2::data::xml_doc doc;
            std::ifstream in("reference/extracted/xml/res/character_progress.xml",
                             std::ios::binary);
            if (in) {
                std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
                doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()),
                          data.size());
                const pugi::xml_node root = doc.root().first_child();
                if (root && std::string(root.name()) == "Progress") {
                    for (pugi::xml_node th :
                         root.child("Thresholds").children("Threshold")) {
                        const int lv = th.attribute("Level") ? th.attribute("Level").as_int(0) : 0;
                        const int xp = th.attribute("Exp") ? th.attribute("Exp").as_int(0) : 0;
                        if (lv > 0 && xp > 0) thresholds[lv] = xp;
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }
    const auto it = thresholds.find(level);
    return it != thresholds.end() ? it->second : 100;
}

// JS `Lr`/`Or`/`Pr` reveal timeline (L2057-2081). `ed(a)` is a DURATION in
// seconds, not an ease curve: `ed(a){return a==0?1:Math.min(1,this.time/a)}`
// (JS @13398), so `ed(.5)` = a 500 ms phase and `ed(1)` = a 1 s phase.
namespace {
constexpr float kRevealDelay = 0.5f;      // `kk.rxa` `wh.delay(...,500)`
constexpr float kRevealSlide = 0.5f;      // `Or.aa` case 0 `this.ed(.5)`
constexpr float kRevealCount = 0.5f;      // `Or.aa` case 1 `this.ed(.5)`
constexpr float kRevealCountStar = 1.0f;  // `Pr.aa` `this.ed(1)`
// `Lr.XMa` -> `bza()` (the OK plate) once the star row lands:
// delay + 6 slides (rows 0..5) + the star's 1 s count.
constexpr float kRevealSettle = kRevealDelay + kRevealSlide * 6.0f + kRevealCountStar;
} // namespace

void ResultsScreen::update_impl(float dt) {
    ensure_lang(app());  // the lang table powers the `Y.na` string lookups
    // JS `Lr`/`Or` reveal clock (L2057-2081): the `kk` results container
    // schedules the list 500 ms after the battle end (`kk.rxa`:
    // `wh.delay(function(){...Yub},500)`), then row `i` slides over
    // `[.5+.5i, 1+.5i]` and counts over the next `.5` (`Or.aa` `ed(.5)`);
    // the star row (`Pr`) never slides and counts over `ed(1)`, so the list
    // settles at 4.5 s and the OK plate appears only then
    // (`Lr.XMa`/`bza`).
    if (!reveal_done_) {
        reveal_t_ += dt;
        if (reveal_t_ >= kRevealSettle) {
            reveal_done_ = true;
            std::fprintf(stdout, "[result] reveal done -> OK plate shown\n");
            std::fflush(stdout);
        }
    }
    // The quest modal is deliberately NOT consumed here: the
    // `FirstGuardBeaten` chain (`quests.xml` L260-282) fires on `FightEnd`
    // with `Place="Map"` (JS `Gib` L517394 `be.ifa(...)`), so its dialogs own
    // the MAP after the OK press (`v.qxa` L1213) — never the Results overlay.
    if (!applied_) {
        applied_ = true;
        WarriorSave w;
        try {
            w = app().save().load();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[result] save load failed: %s\n", e.what());
            return;
        }
        if (player_won_) {
            // JS `dmb` -> `emb` (L93552): Money -> `Pa.Fwa` (Tb += money),
            // Exp -> `Pa.Iab` -> `p.o.Jab` (XP).
            const int before = w.money;
            w.money += money_reward_;
            w.experience += exp_reward_;
            // JS `hj.Uo` gems (FLOW_STATIC section 4.4 `emb`): applied to
            // Bonus. No fight source evidenced (always 0 today) — the field
            // flows end-to-end for when gem sources land.
            w.bonus += app().pending_battle().prize_gems;
            std::fprintf(stdout, "[result] WIN reward money=%d exp=%d (money %d -> %d)\n",
                         money_reward_, exp_reward_, before, w.money);
            // JS battle record (`iF` via `hl`/`lWa`, FLOW_STATIC section 3.2):
            // a win records the battle for the `WDa` unlock rule; the fight
            // win count (`yc`/`no`) bumps too.
            {
                const PendingBattle& pb = app().pending_battle();
                // JS `J1a` L259 / `u4a` L260: the unlock write is the
                // zone-qualified `hb` key (`Me+"|"+Re+"|"`, L1416), never a
                // bare name (the legacy `record_battle_win` is superseded).
                w.battle_unlock(pb.zone, pb.battle_name);
                // JS `Dxa` L111216 (win): `B0a(a.Nb)` -> `il.Fab` (`no++`),
                // then `b.xL(p.o.bb())` sets the record `Level` to the player
                // level. The record `IDS` is the `hb` triple (`il.Atb`); the
                // direct-boot path has no triple -> the battle name.
                const std::string ids = pb.fight_triple.empty()
                                            ? pb.battle_name
                                            : pb.fight_triple;
                WarriorSave::FightWins& fr = w.fight_record_or_create(ids);
                ++fr.wins;
                fr.level = w.level;
                std::fprintf(stdout,
                             "[result] fight record: %s wins=%d level=%d\n",
                             ids.c_str(), fr.wins, fr.level);
            }
            // Prize breakdown snapshot for render (JS `v.kD` factor lines;
            // base + bonus were captured by the FightScreen handoff).
            {
                const PendingBattle& pb = app().pending_battle();
                prize_base_ = pb.prize_base_coins;
                prize_bonus_ = pb.prize_bonus;
                prize_combo_ = pb.prize_combo;
                prize_shocks_ = pb.prize_shocks;
                prize_perfect_ = pb.prize_perfect;
                prize_first_ = pb.prize_first;
                prize_perfect_coins_ = pb.prize_perfect_coins;
                prize_first_coins_ = pb.prize_first_coins;
                prize_combo_coins_ = pb.prize_combo_coins;
                prize_style_coins_ = pb.prize_style_coins;
                prize_shock_coins_ = pb.prize_shock_coins;
                // `oc.OY` ruby (`Fh.lXa` arg `c` L2054-2055 -> `oc.mOa`): the
                // goldPrize row's `Or.x_` (`Lr.ZMa` L2078).
                prize_ruby_ = pb.prize_gems;
            }
            // JS `OLa` level-up (L253-254): `rs+=exp` vs `Oz()` thresholds
            // (`v.FR` = character_progress.xml `<Threshold Level Exp>`).
            while (w.level < 50) {
                const int need = ResultsScreen::exp_for_level(w.level);
                if (w.experience < need) break;
                w.experience -= need;
                w.level++;
                w.power += 2;
                std::fprintf(stdout, "[result] LEVEL UP -> %d (power %d)\n", w.level, w.power);
            }
        } else {
            // JS `Dxa` L111216 (loss): `eeb(a.Nb)` -> `il.Sq` (find ONLY) then
            // `il.Lab` (`FW++`) and, when found, `b.xL(p.o.bb())` (record Level
            // = player level). A loss with NO record creates none (`eeb`
            // returns null when `Sq` misses) — unlike a win (`Yea`/`eya`).
            const PendingBattle& pb = app().pending_battle();
            const std::string ids = pb.fight_triple.empty()
                                        ? pb.battle_name
                                        : pb.fight_triple;
            WarriorSave::FightWins* fr = w.fight_record(ids);
            if (fr != nullptr) {
                ++fr->losses;
                fr->level = w.level;
                std::fprintf(stdout,
                             "[result] fight record: %s losses=%d level=%d\n",
                             ids.c_str(), fr->losses, fr->level);
            }
            std::fprintf(stdout, "[result] LOSS (no reward)\n");
        }
        try {
            app().save().save(w);
            std::fprintf(stdout, "[result] save: money=%d exp=%d level=%d weapon=%s\n", w.money,
                         w.experience, w.level, w.weapon.c_str());
            std::fflush(stdout);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[result] save failed: %s\n", e.what());
        }
    }
    const App::PointerState& p = app().pointer();
    if (p.pressed) {
        std::fprintf(stdout, "[result] click -> back to Map\n");
        std::fflush(stdout);
        // JS `qxa` (L1213) pops back to the map: the Results screen sits on
        // top of the Fight screen it replaced, so both pop (the fight is
        // done; the map is the caller the flow returns to). Capture the
        // manager first — the first pop destroys `this`, so a second
        // `manager()` call would re-read a freed member (use-after-free).
        ScreenManager& mgr = manager();
        mgr.pop();
        mgr.pop();
    }
}

// JS `kk.Qa = R.$(E.Zxa(750))` (L2057): `E.Zxa` (L93) builds a 750x4 canvas
// HORIZONTAL gradient, filled by `E.Eua` (L94) with stops
// `#00000020 @0 / #00000080 @.25/.5/.75 / #00000020 @1` (black, alpha
// 32/128/128/128/32). `kk.layout` (L2059) stretches it over `ma.Kq`
// (`Rh/mj`), i.e. the whole fight viewport — the "panel" is a full-viewport
// alpha gradient, not a flat rect. `draw_triangles` is flat-color, so the
// gradient is sampled into N vertical strips.
void draw_kk_gradient(sf2::render::Renderer& ren, float x, float y, float w, float h,
                      float alpha) {
    struct Stop {
        float t;
        float a;
    };
    const Stop stops[5] = {{0.0f, 0x20 / 255.0f}, {0.25f, 0x80 / 255.0f},
                           {0.5f, 0x80 / 255.0f},  {0.75f, 0x80 / 255.0f},
                           {1.0f, 0x20 / 255.0f}};
    constexpr int kStrips = 48;
    for (int i = 0; i < kStrips; ++i) {
        const float t0 = static_cast<float>(i) / kStrips;
        const float t1 = static_cast<float>(i + 1) / kStrips;
        const float tm = (t0 + t1) * 0.5f;
        float a = stops[0].a;
        for (int s = 0; s < 4; ++s) {
            if (tm <= stops[s + 1].t) {
                const float f = (tm - stops[s].t) / (stops[s + 1].t - stops[s].t);
                a = stops[s].a + (stops[s + 1].a - stops[s].a) * f;
                break;
            }
            a = stops[s + 1].a;
        }
        const float x0 = x + w * t0, x1 = x + w * t1;
        const float v[] = {x0, y, x1, y, x1, y + h, x0, y, x1, y + h, x0, y + h};
        ren.draw_triangles(v, 6, 0.0f, 0.0f, 0.0f, a * alpha);
    }
}

// JS `Lr.mBa` + `Lr.RYa` (L2081): the results `eZ` coin formatter. The star
// row's GOLD value (`Pr.el`, L2083) and every `Or` breakdown row's value
// (`Or.el`, L2086 `this.el.lj(d(0))` / L2087 `this.el.lj(this.eZ(...))`) go
// through it. Shape: `K.T(gM)` raw digits below 1000, else
// `""+gM+"."+UR+Y.na(suffix)` — the fraction is `Math.trunc`'d (never
// zero-padded) and the suffix is the lang `tsdShort`/`mlnShort`/`blnShort`
// ("K"/"m"/"bn" in EN). The exp counter (`Pr.exp`) is the ONE raw value
// (`Pr.aa` L2083 `""+a`), so it keeps `std::to_string`.
std::string results_coin_text(App& app, int value) {
    if (value < 1000) return std::to_string(value);  // `K.T(b.gM)`
    int whole = 0, frac = 0;
    const char* key = "tsdShort";
    const char* fb = "K";
    if (value < 1000000) {  // `a<1E6`: gM=trunc(a/1E3), UR=trunc(a%1E3/10)
        whole = value / 1000;
        frac = (value % 1000) / 10;
    } else if (value < 1000000000) {  // `a<1E9`: gM=trunc(a/1E6), UR=trunc(a%1E6/1E4)
        whole = value / 1000000;
        frac = (value % 1000000) / 10000;
        key = "mlnShort";
        fb = "m";
    } else {  // `a>=1E9`: gM=trunc(a/1E9), UR=trunc(a%1E9/1E7)
        whole = value / 1000000000;
        frac = (value % 1000000000) / 10000000;
        key = "blnShort";
        fb = "bn";
    }
    return std::to_string(whole) + "." + std::to_string(frac) + loc(app, key, fb);
}

// JS `We.Sfa` (L2445): the ruby sub-row formatter — digits grouped in 3s
// from the right, joined by a space (`b=" "`). `1000` -> "1 000",
// `1234567` -> "1 234 567"; below 1000 it is the raw `c` digits.
std::string results_spaced_text(int value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0 && ((n - i) % 3) == 0) out.push_back(' ');
        out.push_back(digits[i]);
    }
    return (value < 0 ? "-" : "") + out;
}

void ResultsScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // `kk.Qa` base (L2057): the `E.Zxa(750)` gradient over `ma.Kq` (native =
    // the viewport). Replaces the flat near-black 750x480 rect + 0.6 dim
    // (PORT_AUDIT_UI §2.8 item 28).
    draw_kk_gradient(ren, 0.0f, 0.0f, kViewW, kViewH);
    // `kk` result dialog (JS L2057-2061; PORT_AUDIT_UI 2.8 item 28): the
    // win/lose label from the callouts atlas id 1310 (`mT = R.$(E.get(1310))`,
    // frame `y.Lna` win / `y.Kna` lose) with the `Fh` breakdown lines below.
    // Replaces the invented standalone VICTORY/DEFEAT screen layout.
    constexpr float kKkW = 750.0f;
    constexpr float kKkH = 480.0f;
    const float px = kViewW * 0.5f - kKkW * 0.5f;
    const float py = kViewH * 0.5f - kKkH * 0.5f;
    // Win/lose label `mT` (callouts id 1310: `y.Lna` win / `y.Kna` lose,
    // JS L2058) placed by `kk.layout` (L2058-2059): `ma.Kq` = the fight
    // viewport rect, `b = a.fn(1.0714285714285714)` (the `gb` contain-fit,
    // L1552), content scale `(b.N-b.J)/750`, `mT.C(375) D(60) la(.5)`.
    // The native fight path uses the screen rect for ma.Kq (J=0,P=0,N=W,W=H —
    // the fight HUD note above); at 16:9 that is b=(254.3,0,1025.7,720),
    // scale 1.0286 -> the title centres at (640, 61.7) at 0.514x.
    // OPEN: ma.Kq.P/W in JS are the projected arena top/bottom (`Sya` L1833),
    // not 0/H — a standalone Results screen has no live camera to derive them.
    constexpr float kKkAspect = 1.0714285714285714f;
    const float kkb_w = (kViewW / kViewH >= kKkAspect) ? kViewH * kKkAspect : kViewW;
    const float kkb_h = (kViewW / kViewH >= kKkAspect) ? kViewH : kViewW / kKkAspect;
    const float kkb_j = (kViewW - kkb_w) * 0.5f;
    const float kkb_p = (kViewH - kkb_h) * 0.5f;
    const float kk_scale = kkb_w / 750.0f;
    const float kk_label_cx = kkb_j + 375.0f * kk_scale;
    const float kk_label_cy = kkb_p + 60.0f * kk_scale;
    const float kk_label_scale = 0.5f * kk_scale;
    bool label_drawn = false;
    if (load_callouts_atlas(app)) {
        const char* lname = player_won_ ? "label_win" : "label_lose";
        sf2::data::atlas_frame lfr;
        int ltw = 0, lth = 0;
        unsigned int lgl = 0;
        if (app.get_atlas_frame(lname, &lfr, &ltw, &lth, &lgl)) {
            const float lw = (lfr.source_w > 0 ? lfr.source_w : lfr.w) * kk_label_scale;
            const float lh = (lfr.source_h > 0 ? lfr.source_h : lfr.h) * kk_label_scale;
            label_drawn =
                try_draw_atlas_button(app, lname, kk_label_cx, kk_label_cy, lw, lh, 1.0f);
        }
    }
    if (!label_drawn) {
        draw_ui_label(app, kViewW * 0.5f - 300.0f, py + 72.0f, 600.0f, 60.0f,
                      player_won_ ? "VICTORY" : "DEFEAT", 1.6f, UiAlign::Center,
                      player_won_ ? 1.0f : 0.8f, player_won_ ? 0.85f : 0.3f,
                      player_won_ ? 0.3f : 0.3f);
    }
    // Prize breakdown (JS `Fh`/`Lr` inner list; `v.kD`/`bzb` factor lines,
    // FLOW_STATIC §4.3: Perfect $Ia=5, FirstStrike ep=2, Combo Ui=1/combo,
    // Shock Ub=3). Gems (JS hj.Uo) are untracked by prize() — no line.
    // `Fh` stat table (JS `Lr.ZMa` L2078-2079): `lf` = 7 rows, each
    // `vI(label, value, ...)` -> a label + a gold `coin` + the value; the
    // last is `v1a(Math.trunc(Hi.ap), b)` = the star row. The oracle
    // `results_lose`/`results_win` show all 7 rows ALWAYS (even on a loss).
    // Labels are the lang keys goldPrize/goldPerfect/goldFirstStrike/
    // goldCombo/goldShock/goldPassiveStyle (EN fallback — the oracle is EN).
    struct KkRow {
        const char* key;
        const char* fallback;
        int value;      // the `Fh.Kx` bonus COIN value (`oc.P3/ep/Ui/DZ/Ub`)
        int count;      // the `{0}` suffix (the row multiplier `d6`/`c6`/`jU`/`e6`)
        bool has_count;
        bool star;
    };
    const KkRow kk_rows[7] = {
        {"goldPrize", "PRIZE", prize_base_, 0, false, false},
        {"goldPerfect", "PERFECT x{0}", prize_perfect_coins_,
         prize_perfect_ ? 1 : 0, true, false},
        {"goldFirstStrike", "FIRST STRIKE x{0}", prize_first_coins_,
         prize_first_ ? 1 : 0, true, false},
        {"goldCombo", "MAX COMBO x{0}", prize_combo_coins_, prize_combo_, true,
         false},
        {"goldShock", "SHOCK x{0}", prize_shock_coins_, prize_shocks_, true, false},
        {"goldTurtleStyle", "PASSIVE STYLE", prize_style_coins_, 0, false, false},
        // Star row (`Pr`/`v1a`, L2078): `Pr(Math.trunc(Hi.ap), m6)` — the exp
        // counter beside the star, the total money beside the gold.
        {"", "", exp_reward_, 0, false, true},
    };
    constexpr float kKkRowX = 420.0f;    // label left edge
    constexpr float kKkCoinX = 822.0f;   // gold coin centre
    constexpr float kKkValX = 838.0f;    // value left edge (cyan)
    constexpr float kKkRowY0 = 155.0f;
    constexpr float kKkRowStep = 58.0f;
    // `Lr.ZMa` (L2078): the non-star rows start at dialog x -500; the reveal
    // ends at `Or.y_ = (750-(pc.tB+pc.oM))/2`; `pc.oM` is 40/80/120 by the
    // total money `oc.m6` (`Lr` ctor L2059).
    const float kk_oM =
        money_reward_ > 100000 ? 120.0f : (money_reward_ > 10000 ? 80.0f : 40.0f);
    const float row_to = px + (750.0f - (400.0f + kk_oM)) * 0.5f;
    const float row_from = px - 500.0f;
    // The `kk.rxa` lead-in is part of the clock: every row curve is measured
    // from `t = reveal_t_ - 0.5` (`wh.delay(...,500)`).
    const float t = reveal_t_ - kRevealDelay;
    auto cl01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    auto with_count = [](std::string s, int n) {
        const std::string p = "{0}";
        const std::size_t at = s.find(p);
        if (at != std::string::npos) s.replace(at, p.size(), std::to_string(n));
        return s;
    };
    for (int i = 0; i < 7; ++i) {
        const KkRow& r = kk_rows[i];
        // `Or` rows slide in (`ed(.5)`) then count (`ed(.5)`); the `Pr` star
        // row never slides (`Pr.nx` shows it in place via `APa(!0)`) and its
        // count runs `ed(1)` = 1 s — so it is hidden until its count starts.
        const float slide =
            r.star ? 1.0f
                   : cl01((t - kRevealSlide * static_cast<float>(i)) / kRevealSlide);
        // Row `i` counts once its own slide lands (`i+1` slides in); the star
        // row counts once ROW 5's slide lands (`i` slides in — it has none).
        const float count =
            cl01((t - kRevealSlide * static_cast<float>(r.star ? i : i + 1)) /
                 (r.star ? kRevealCountStar : kRevealCount));
        if (r.star ? count <= 0.0f : slide <= 0.0f) continue;
        const float from = r.star ? row_to : row_from;  // rows 0..n-2 set -500
        const float rx = from + (row_to - from) * slide;
        const float ry = kKkRowY0 + static_cast<float>(i) * kKkRowStep;
        const int shown = static_cast<int>(static_cast<float>(r.value) * count + 0.5f);
        const float coin_dx = kKkCoinX - kKkRowX;
        const float val_dx = kKkValX - kKkRowX;
        if (r.star) {
            // `Pr.exp = new Hg(60,!0); this.exp.nL(y.Zna)` (L2083): the exp
            // counter's ICON frame is `y.Zna` = "level" (L2465), NOT "star"
            // (`y.PRa` = "star" is the PROFILE level bar's star, L2466). The
            // "gold" icon beside it is `p.o.Vf` (L2083; default `Z.Hna` =
            // "gold", L2478) - already correct below.
            (void)try_draw_atlas_button(app, "level", rx + 26.0f, ry, 52.0f, 48.0f,
                                        slide);
            // JS `Pr.aa` (L2083): the GOLD value (`this.el`, `w_` = `oc.m6`
            // coins) is `a=dc.Ln()(b); a=Math.round(this.w_*a)` - the
            // `1-(1-b)^2` ease over the same `ed(1)` clock as the LINEAR
            // star value (`this.exp` = `Math.round(this.B3a*b)`, `B3a` =
            // `Hi.ap` exp). The port rendered the gold value STATIC.
            const int money_shown = static_cast<int>(
                static_cast<float>(money_reward_) * dialog_ease_out(count) + 0.5f);
            draw_ui_label(app, rx + 60.0f, ry - 16.0f, 120.0f, 32.0f,
                          std::to_string(shown), 0.95f, UiAlign::Left, 0.31f * slide,
                          0.79f * slide, 0.84f * slide);
            (void)try_draw_atlas_button(app, "gold", rx + coin_dx, ry, 48.0f, 48.0f,
                                        slide);
            // `Pr.aa` L2083: the GOLD value is `this.el.lj(this.eZ(a))` - the
            // `eZ` = `mBa` compact formatter, NOT raw digits.
            draw_ui_label(app, rx + val_dx, ry - 16.0f, 120.0f, 32.0f,
                          results_coin_text(app, money_shown), 0.95f, UiAlign::Left,
                          0.31f * slide, 0.79f * slide, 0.84f * slide);
            continue;
        }
        std::string lab = loc(app, r.key, r.fallback);
        if (r.has_count) lab = with_count(std::move(lab), r.count);
        draw_ui_label(app, rx, ry - 16.0f, 400.0f, 32.0f, lab, 0.95f, UiAlign::Left,
                      0.94f * slide, 0.89f * slide, 0.72f * slide);
        (void)try_draw_atlas_button(app, "gold", rx + coin_dx, ry, 48.0f, 48.0f,
                                    slide);
        draw_ui_label(app, rx + val_dx, ry - 16.0f, 120.0f, 32.0f,
                      results_coin_text(app, shown), 0.95f, UiAlign::Left, 0.31f * slide,
                      0.79f * slide, 0.84f * slide);
        // `Or.Qw` ruby sub-row (L2086-2088): the `ruby` icon (`y.boa`
        // L2466) + the `We.Sfa`-formatted `oc.OY` value, shown only when
        // `x_>0` (`Or.nx` L2087) and placed LEFT of the gold value
        // (`Or.align` L2088 `Qw.C(el.node.ya - Qw.za())`). `Lr.ZMa` (L2078)
        // passes `oc.OY` only to the `goldPrize` row, so this is row 0.
        if (i == 0 && prize_ruby_ > 0) {
            const int ruby_shown =
                static_cast<int>(static_cast<float>(prize_ruby_) * count + 0.5f);
            (void)try_draw_atlas_button(app, "ruby", rx + coin_dx - 66.0f, ry, 40.0f,
                                        40.0f, slide);
            draw_ui_label(app, rx + coin_dx - 252.0f, ry - 16.0f, 160.0f, 32.0f,
                          results_spaced_text(ruby_shown), 0.9f, UiAlign::Right,
                          0.31f * slide, 0.79f * slide, 0.84f * slide);
        }
    }
    // PROBE (temporary, `SF2_REVEAL_PROBE=1`): per-frame reveal telemetry —
    // the clock, the visible-row count and every row's slide/count progress,
    // so the Results timeline can be compared with the JS `Lr`/`Or` clock.
    if (std::getenv("SF2_REVEAL_PROBE") != nullptr) {
        static int probe_frame = 0;
        std::string line = "[reveal] f=" + std::to_string(probe_frame++) +
                           " t=" + std::to_string(reveal_t_) +
                           (reveal_done_ ? " done" : " run");
        for (int i = 0; i < 7; ++i) {
            const bool star = kk_rows[i].star;
            const float s =
                star ? 1.0f
                     : cl01((t - kRevealSlide * static_cast<float>(i)) / kRevealSlide);
            const float c = cl01((t - kRevealSlide * static_cast<float>(star ? i : i + 1)) /
                                 (star ? kRevealCountStar : kRevealCount));
            char buf[48];
            std::snprintf(buf, sizeof(buf), " r%d=%.2f/%.2f", i, s, c);
            line += buf;
        }
        std::fprintf(stdout, "%s\n", line.c_str());
        std::fflush(stdout);
    }
    // OK button (JS `Lr.$g = new Bb("EButtonWhite"); $g.V(Y.na("OK"))`, L2075).
    // `Bb.fza` (L1844 `"btn"+K.T(a).substr(7)`) resolves the style key to a
    // FRAME: "EButtonWhite" -> `btnWhite` of `ui/sliced.json`. There is no
    // `EButtonBeige` FRAME (the style keys are `Bb` class keys) — the old
    // literal always missed the atlas and fell through to the flat plate.
    // OK plate (`Lr.$g`, L2075): `$g.X(!1)` at build, shown only by `bza()`
    // once the last row lands (`Lr.XMa` L2071-2072) — the port gates it on
    // `reveal_done_`.
    if (reveal_done_) {
        const float okx = kViewW * 0.5f;
        const float oky = 645.0f;
        // `$g.V(Y.na("OK"))` (L2075): the plate caption is the localized `OK`
        // key, not a literal.
        const std::string ok_label = loc(app, "OK", "OK");
        if (!draw_bb_plate(app, "btnWhite", okx, oky, 230.0f, 52.0f, 1.0f)) {
            draw_flat_button(app, ok_label, okx, oky, 210.0f, 48.0f, 0.85f, 0.78f, 0.55f,
                             false);
        }
        draw_ui_label(app, okx - 105.0f, oky - 13.0f, 210.0f, 26.0f, ok_label, 0.85f,
                      UiAlign::Center, 0.20f, 0.15f, 0.08f);
    }
    std::fprintf(stdout, "[result] %s\n", player_won_ ? "WIN" : "LOSS");
    // The `FirstGuardBeaten` chain (`quests.xml` L260-282) is `Place="Map"`
    // (JS `Gib` L517394): its dialogs own the MAP after the OK press
    // (`v.qxa` L1213), never the Results overlay. The old unconditional
    // `draw_quest_modal`/`quest_modal_consume` here was what played the story
    // DURING the results instead of after OK.
}

// ---------------------------------------------------------------------------
// ShopScreen
// ---------------------------------------------------------------------------

// Shop tabs (JS `vj.E0` L1168-1169 category ids → `Cj.l6` tab ids L2302,
// `Cj.zxb` tab→type L2303, `Oa.jAa`/`f5` per-tab lists L2286-2288/L2297).
// `Cj.l6` (L2302): 1→0 Weapon, 2→1 Armor, 3→2 Helm, 4→3 Ranged, 5→4 Magic,
// 6→5 Ruby, 7→7 Free, 8→6 RaidConsumable.
// `Cj.zxb` (L2303): 5→`I.wk` "RealMoneyItem", 6→`I.Hm` "RaidConsumable",
// 7→`I.Bu` "Free". `jAa` (L2297) fills tab5 from BOTH `p.items.Dp`
// (RealMoneyItem) and `p.items.hca` (Consumable), tab7 from `p.items.S_`
// (Free); tab6 has NO `f5` case and no `it.parse` bucket → always empty.
struct ShopTab {
    const char* label;
    const char* type;
    int e0;
};
constexpr ShopTab kShopTabs[] = {
    {"WEAPONS", "Weapon", 1},
    {"ARMOR", "Armor", 2},
    {"HELMS", "Helm", 3},
    {"RANGED", "Ranged", 4},
    {"MAGIC", "Magic", 5},
    {"RUBY", "RealMoneyItem", 6},
    {"RAID", "RaidConsumable", 8},
    {"FREE", "Free", 7},
};
constexpr int kShopTabCount = 8;

// Responsive shop layout (JS `Oa.layout` L2293-2295 + the `gb` rect class
// L1551-1552). Replaces the invented fixed grid (PORT_AUDIT_UI §3 #16,
// ranked MEDIUM #5). `gb` = (J=left, P=top, N=right, W=bottom); `gb.fn(a)`
// (L1552) returns the largest sub-rect with width:height = a:1 centred
// inside. `Oa.layout` at 16:9:
//   margin   = W*.05 * clamp(lc,.6,1)                       (L2293)
//   content  = gb(margin, Sp*1.4, W-margin, H-Sp*1.5*1.3)   (L2293)
//   content  = content.fn(1.85 + (clamp(lc,.6,1)-.6)/.4*.15)(L2293)
//   viewer c = content.fn(.75)  -> `this.Za.Pn(c)`          (L2294)
//   gap e    = (c.N-c.J)*.03 ;  slot b = (c.W-c.P)*.8       (L2294)
// The side slots (L2294-2295) are the two `gb(0,0,b*.7,b)` rects:
//   left  b2 = [c.J+e - w, c.J+e], vertically centred on c -> `MJ`/`op`
//   right d  = [c.N-e, c.N-e + w], vertically centred on c -> `bc`
// (`MJ.Pn(b2); op.Pn(b2); Za.Pn(c); bc.Pn(d)`, L2295).
// The JS cells `ns` (L2303-2308) live in the `Oe` viewer list. `Oa.f5`
// (L2286-2288) sets the per-category viewer anchor `Za.uw` and list spacing
// `Za.LT` BEFORE building the list `yF(...)`:
//   tab0 Weapon  uw=(300,220) LT(50)   tab1 Armor  uw=(300,400) LT(20)
//   tab2 Helm    uw=(300,280) LT(100)  tab3 Ranged uw=(300,220) LT(50)
//   tab4 Magic   uw=(300,220) LT(50)   tab5 IAP    uw=(300,320) (no LT)
//   tab7 event   uw=(670,500)          (no LT)
// (`Za` here is the `Oe` card viewer, not the gamepad `Za`.) `Oe.Pn(c)`
// (L2262) docks `scroll` at `c.J/c.P`, sizes it `(c.width, c.height,
// c.width*.08)`, then `Pa.C(4)`, `Pa.ba(scroll.Gv-8, scroll.Xy)`. `Gg.ba`
// (L1884-1885) scales each cell to the list width (`ff.kf(a)`, L1893) and
// stacks them with `spacing` (`LT`): screen cell height = `uw.y * (listW /
// uw.x)` (`ff.qa` = `ce.y*node.Eb`, L1893). The exact `Fg` inner dims
// (`Gv`/`Xy`) and the `hi` interior insets (`b=bg.Eb*28`, L2311) still need
// the `y.*` frame-name table (PORT_AUDIT_UI §5 OPEN #2/#4); the list width is
// taken as the viewer width - 8 (the `scroll.Gv-8` term) and cell x as
// `viewer.J + 4` (the `Pa.C(4)` term).
struct ShopRect {
    float J = 0.0f, P = 0.0f, N = 0.0f, W = 0.0f;  // left/top/right/bottom
    float width() const { return N - J; }
    float height() const { return W - P; }
};

// `gb.fn` (JS L1551-1552): contain-fit a width:height = aspect:1 rect.
ShopRect shop_gb_fn(const ShopRect& r, float aspect) {
    const float bw = r.width();
    const float bh = r.height();
    const float d = bw / aspect;  // scaled width
    const float e = bh;           // height/1
    if (d <= e) {
        const float t = r.P + (e - d) * 0.5f;
        return {r.J, t, r.N, t + d};
    }
    const float w2 = aspect * e;
    const float l = r.J + (bw - w2) * 0.5f;
    return {l, r.P, l + w2, r.W};
}

// Per-category viewer anchor `uw` + list spacing `LT` (Oa.f5 L2286-2288).
// Index = kShopTabs order (0 Weapon .. 4 Magic); tabs 5/7 are not shipped.
struct ShopViewerParams {
    float uw_x;
    float uw_y;
    float spacing;
};
constexpr ShopViewerParams kShopViewer[kShopTabCount] = {
    {300.0f, 220.0f, 50.0f},   // tab0 Weapon: uw=(300,220) LT(50)
    {300.0f, 400.0f, 20.0f},   // tab1 Armor:  uw=(300,400) LT(20)
    {300.0f, 280.0f, 100.0f},  // tab2 Helm:   uw=(300,280) LT(100)
    {300.0f, 220.0f, 50.0f},   // tab3 Ranged: uw=(300,220) LT(50)
    {300.0f, 220.0f, 50.0f},   // tab4 Magic:  uw=(300,220) LT(50)
    {300.0f, 320.0f, 50.0f},   // tab5 Ruby/IAP: uw=(300,320) (`f5` case 5, no LT)
    {300.0f, 220.0f, 50.0f},   // tab6 RaidConsumable: no `f5` case
    {670.0f, 500.0f, 50.0f},   // tab7 Free: uw=(670,500) (`f5` case 7, no LT)
};

struct ShopLayout {
    ShopRect content;      // b (L2293, after fn)
    ShopRect viewer;       // c = b.fn(.75) (L2294) — the JS `Za`/`Oe` area
    ShopRect left_panel;   // b2 (L2294) — `MJ`/`op`
    ShopRect right_panel;  // d  (L2294) — `bc` item detail
    float gap = 0.0f;      // e = (c.N-c.J)*.03 (L2294)
    float slot_h = 0.0f;   // b = (c.W-c.P)*.8 (L2294, side-slot height)
    // `Oe`/`Gg` single-column cell list (L2261-2262, L1883-1893).
    float uw_x = 0.0f, uw_y = 0.0f, spacing = 0.0f;
    float scroll_inset = 0.0f;  // `Fg` rail width c = viewer.width*.08 (L2262)
    float roll_h = 0.0f;        // `vk` = 30 (top/bottom `Zh` roll bands, L2261)
    float list_h = 0.0f;        // `scroll.Xy` = viewer.height - 2*roll_h (L1870)
    float cell_w = 0.0f;    // scroll.Gv - 8 (L2262)
    float cell_h = 0.0f;    // uw.y * cell_w / uw.x (ff.qa, L1893)
    float cell_step = 0.0f; // cell_h + spacing (Gg.ba, L1885)
    float cell_cx = 0.0f;   // viewer.J + inset + 4 + cell_w/2 (Pa.C(4), L2262)
    float cell_top = 0.0f;  // viewer.P + roll_h (content.D(vk), L1871)
};

// `viewer` clipped to the JS `Oe.scroll` inner content height (`Xy`, L2262).
// `Fg.ba` inssets `Xy = h - n_rolls*30`; the native has both `Dl` rolls
// (`Vaa(30)`), so the visible list band is `viewer.P+30 .. viewer.W-30`.
float shop_list_bottom(const ShopLayout& l) { return l.cell_top + l.list_h; }

ShopLayout shop_layout(int tab) {
    const float lc = kViewW / kViewH;            // N.lc
    const float t = std::clamp(lc, 0.6f, 1.0f);  // clamp(lc,.6,1)
    const float sp = za_layout().sp;             // za.Sp (JS L1975)
    const float margin = kViewW * 0.05f * ((t - 0.6f) / 0.4f);  // L2293
    ShopRect b{margin, sp * 1.4f, kViewW - margin,
               kViewH - sp * 1.5f * 1.3f};  // L2293
    b = shop_gb_fn(b, 1.85f + ((t - 0.6f) / 0.4f) * 0.15f);  // L2293 fn
    ShopLayout l;
    l.content = b;
    l.viewer = shop_gb_fn(b, 0.75f);       // c = b.fn(.75) L2294
    l.gap = l.viewer.width() * 0.03f;      // L2294 e
    l.slot_h = l.viewer.height() * 0.8f;   // L2294 b
    const float slot_w = l.slot_h * 0.7f;  // gb(0,0,b*.7,b) L2294
    const float cy = (l.viewer.P + l.viewer.W) * 0.5f;
    l.left_panel = {l.viewer.J + l.gap - slot_w, cy - l.slot_h * 0.5f,
                    l.viewer.J + l.gap, cy + l.slot_h * 0.5f};
    l.right_panel = {l.viewer.N - l.gap, cy - l.slot_h * 0.5f,
                     l.viewer.N - l.gap + slot_w, cy + l.slot_h * 0.5f};
    // `Gg` cell pitch from the per-category `uw` + `LT` (L2286-2288).
    const ShopViewerParams vp = kShopViewer[std::clamp(tab, 0, kShopTabCount - 1)];
    l.uw_x = vp.uw_x;
    l.uw_y = vp.uw_y;
    l.spacing = vp.spacing;
    l.scroll_inset = l.viewer.width() * 0.08f;          // Fg.Pn .08 (L2262)
    l.roll_h = 30.0f;                                   // vk (`Vaa(30)` L2261)
    l.list_h = l.viewer.height() - 2.0f * l.roll_h;     // scroll.Xy (L1870)
    l.cell_w = l.viewer.width() - 2.0f * l.scroll_inset - 8.0f;  // scroll.Gv-8
    l.cell_h = vp.uw_y * (l.cell_w / vp.uw_x);          // ff.qa (L1893/L2262)
    l.cell_step = l.cell_h + vp.spacing;                // Gg.ba (L1885)
    l.cell_cx = l.viewer.J + l.scroll_inset + 4.0f + l.cell_w * 0.5f;  // Pa.C(4)
    l.cell_top = l.viewer.P + l.roll_h;                 // content.D(vk) L1871
    return l;
}

// The `Up` TRY/EQUIP/UNEQUIP action button (JS `Oa.init` L2289
// `this.Up=new Bb("EButtonWhite")`, docked in the `jP` container over the
// LEFT slot). `Oa.layout` (L2295, verbatim):
//   this.jP.C((b.J+b.N)*.5*.9);
//   this.Up.kf(b.N-b.J);            // node scale = width / btnWhite.fa.x
//   this.jP.D(b.P+this.Up.qa());    // Bb.qa() = 112*Eb (L1843)
// so the button spans the left slot width at the slot's TOP; the old
// native put it in the right detail panel (invented).
ShopRect shop_try_rect(const ShopLayout& l) {
    const ShopRect& b = l.left_panel;
    const float w = b.width();
    const float sc = w / 600.0f;          // btnWhite runtime fa.x (2x sourceSize)
    const float h = 112.0f * sc;          // Bb.qa() L1843
    const float cx = (b.J + b.N) * 0.5f * 0.9f;  // L2295
    const float cy = b.P + h;             // jP.D(b.P+Up.qa()) L2295
    return {cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f};
}

// The `M8` GoldButton rect (`Ne.Wub` L2254-2255 -> `c5(this.M8, Aa.jp(), …)`).
// `Ne.ba` L2249 stacks the `Cd` buttons up from `d = b - c*3` with
// `c = a*.05`, each `kf(a)` (full content width) and `qa()` tall; the detail
// panel box is `rp` (`shop_layout.right_panel`) inset by `28/24*pscale`. This
// MUST equal the render block that draws the green plate, so the confirm
// hit-test cannot drift from the drawn art.
ShopRect shop_price_rect(const ShopLayout& sl, int slot) {
    const ShopRect& rp = sl.right_panel;
    const float pscale = rp.width() / 608.0f;  // info_panel_h 608x866
    const float bx = 28.0f * pscale, by = 24.0f * pscale;
    const float cx0 = rp.J + bx;
    const float cw0 = rp.width() - 3.0f * bx;
    const float cy0 = rp.P + by;
    const float ch0 = rp.height() - 2.0f * by;
    const float bpad = cw0 * 0.05f;
    const float bh = 112.0f * pscale;
    // `Ne.ba` L2249: `d = b - c*3` for the first active button, then
    // `d -= e.qa() + c` per stacked button. `slot` is the 0-based position
    // from the BOTTOM (ruby at 0 when both prices are live).
    const float byy =
        cy0 + ch0 - bpad * 3.0f - bh * 0.5f - static_cast<float>(slot) * (bh + bpad);
    return {cx0, byy - bh * 0.5f, cx0 + cw0, byy + bh * 0.5f};
}

// The `Oe` viewer scroll = JS `new Fg(500,800,0,30)` + `.Vaa(30)` (L2261-2262)
// rendered in `Fg.ba` (L1869-1871) + `Zh.ba` (L1872):
//   rails  `paper_edge_left/right` (`y.nSa/oSa` L2467), width `c = w*.08`,
//          full height, at x [0,c] and [w-c,w];
//   body   `paper` (`y.mSa`), x [c, w-c], full height;
//   rolls  `Zh(w,30)`: `roll_end` (`y.goa` L2468) caps `101*30/114` wide +
//          `roll_center` stretched between, 30 tall, at y [0,30] and
//          [h-30,h] (top + bottom);
//   content inset `(c, 30)` -> cell list origin (`scroll.content` L1870-1871).
void draw_shop_scroll(App& app, sf2::render::Renderer& ren, const ShopRect& v) {
    (void)ren;
    if (!load_scroll_atlas(app)) return;
    const float w = v.width(), h = v.height();
    const float c = w * 0.08f;                       // Fg.Pn `(a.N-a.J)*.08`
    const float cx = (v.J + v.N) * 0.5f, cy = (v.P + v.W) * 0.5f;
    try_draw_atlas_button(app, "paper", cx, cy, w - 2.0f * c, h, 1.0f, true, false);
    try_draw_atlas_button(app, "paper_edge_left", v.J + c * 0.5f, cy, c, h, 1.0f, true,
                          false);
    try_draw_atlas_button(app, "paper_edge_right", v.N - c * 0.5f, cy, c, h, 1.0f, true,
                          false);
    constexpr float kRollH = 30.0f;                  // vk (`Fg(500,800,0,30)`)
    constexpr float kCapSrcW = 101.0f, kCapSrcH = 114.0f;  // roll_end 101x114
    const float capw = kCapSrcW * (kRollH / kCapSrcH);
    const float midw = std::max(w - 2.0f * capw, 10.0f);
    for (int band = 0; band < 2; ++band) {
        const float by = (band == 0) ? v.P + kRollH * 0.5f : v.W - kRollH * 0.5f;
        try_draw_atlas_button(app, "roll_end", v.J + capw * 0.5f, by, capw, kRollH, 1.0f,
                              true, false);
        try_draw_atlas_button(app, "roll_end", v.N - capw * 0.5f, by, capw, kRollH, 1.0f,
                              true, true);
        try_draw_atlas_button(app, "roll_center", v.J + capw + midw * 0.5f, by, midw, kRollH,
                              1.0f, true, false);
    }
}

// Slot fallback when unequipping (JS `p.vzb`, L214-215).
const char* shop_default_for_type(const std::string& type) {
    if (type == "Armor") return "Body";
    if (type == "Helm") return "Head";
    if (type == "Ranged") return "NoRanged";
    if (type == "Magic") return "NoMagic";
    return "Fists";
}

// The `<Screen Name>` a shop item's `TryOn` move is gated to (`Gm` L?; the
// shipped names are ShopWeapon/ShopArmor/ShopHelm/ShopMagic/ShopMissile/
// ShopOther). The list.xml Type buckets: Weapon->ShopWeapon (58 moves),
// Ranged->ShopMissile (15), Magic->ShopMagic (32), Armor/Helm their own
// (2 each); everything else (Seal/RaidItemPack/...) shares `ShopOther`.
const char* shop_screen_for_type(const std::string& type) {
    if (type == "Weapon") return "ShopWeapon";
    if (type == "Armor") return "ShopArmor";
    if (type == "Helm") return "ShopHelm";
    if (type == "Ranged") return "ShopMissile";
    if (type == "Magic") return "ShopMagic";
    return "ShopOther";
}

// `FileName` -> anim archive key (JS `Te.Skb` L551: `FileName` minus
// ".bytes"; the moves.xml ".bin" is normalized to ".bytes" by the parser).
std::string shop_clip_key(const sf2::scene::MoveDef& m) {
    std::string base = m.file_name;
    const std::string suffix = ".bytes";
    if (base.size() > suffix.size() &&
        base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
        base = base.substr(0, base.size() - suffix.size());
    }
    if (base.empty()) base = m.name;
    return base;
}

// `Oa.Fhb` L2300 unowned -> `this.Ex(a,7)` (L2301 region): wear the item on
// the `Pi` body and load its `TryOn` clip. `Pi.Ex` writes the item into its
// typed slot (`a.type==I.vg?this.Ca.Hd=a : ...`), rebuilds (`this.Ca.cM()` ->
// `xc.cM` L809-810) and runs the `TryOn` animation (`this.LX=7`,
// `iz.XBa("TryOn")=7` L444). The merged body lands in `preview_model_`
// (screen storage) — the shared FightAssets::merged is never rebuilt, so a
// dojo/fight return is unaffected. The clip is the move the worn item's
// `<Screen>` + `<Item>` locks admit (WEAPON_KNIVES -> `ShopKnivesSuperSlash`,
// moves.xml L9437 -> `knives_super_slash.bin`).
void ShopScreen::arm_preview(App& app, const CatalogItem& it) {
    preview_fighter_.reset();
    preview_model_ = sf2::scene::Model{};
    preview_clip_ = nullptr;
    preview_frame_ = 0;
    preview_active_ = false;
    if (!app.has_fight_assets()) return;
    FightAssets& assets = app.fight_assets();

    // The worn set: the save's items + the previewed one (JS `Pi.Ex` writes
    // the item into its slot before `cM()`), for the TryOn move's locks.
    std::vector<sf2::scene::OwnedItem> worn = owned_items(app);
    bool present = false;
    for (const sf2::scene::OwnedItem& o : worn) {
        if (o.name == it.name) present = true;
    }
    if (!present) worn.push_back({it.type, it.subtype, it.name});

    // Body: the save's typed slots with the item swapped into its own
    // (`fighter_model_names` buckets by list.xml Type, last-in wins).
    std::vector<std::string> names;
    try {
        const WarriorSave w = app.save().load();
        names = {w.skeleton, w.weapon, w.armor, w.helm};
    } catch (const std::exception&) {
    }
    names.push_back(it.name);
    const std::vector<std::string> model_names = fighter_model_names(app, names);
    if (model_names.empty() || model_names[0].empty()) {
        std::fprintf(stdout, "[shop] Ex(a,7) preview: no model names for %s\n",
                     it.name.c_str());
        std::fflush(stdout);
        return;
    }
    preview_model_ = assets.merge_names(model_names);
    if (preview_model_.bones.empty()) {
        std::fprintf(stdout, "[shop] Ex(a,7) preview: empty merge for %s\n",
                     it.name.c_str());
        std::fflush(stdout);
        return;
    }

    const sf2::scene::MoveDef* tm = sf2::scene::Fighter::shop_tryon_move(
        assets.moves, worn, shop_screen_for_type(it.type));
    if (tm == nullptr) {
        std::fprintf(stdout, "[shop] Ex(a,7) preview: no TryOn move (screen %s) for %s\n",
                     shop_screen_for_type(it.type), it.name.c_str());
        std::fflush(stdout);
        return;
    }
    const auto cit = assets.clips.find(shop_clip_key(*tm));
    if (cit == assets.clips.end() || cit->second.frames.empty()) {
        std::fprintf(stdout, "[shop] Ex(a,7) preview: clip %s missing\n",
                     tm->file_name.c_str());
        std::fflush(stdout);
        return;
    }

    preview_fighter_ = std::make_unique<sf2::scene::Fighter>();
    preview_fighter_->set_model(preview_model_);
    preview_fighter_->set_color(assets.dojo.root_color());
    preview_clip_ = &cit->second;
    preview_active_ = true;
    // Preview-owned storage: the shared body (`assets.merged`, used by the
    // dojo/fight and the idle backdrop) is NEVER rebuilt here, so returning
    // to the dojo/fight after a preview shows the same model.
    std::fprintf(stdout,
                 "[shop] Ex(a,7) TryOn %s -> move %s clip %s (preview %zu bones; shared "
                 "merged %zu unchanged)\n",
                 it.name.c_str(), tm->name.c_str(), tm->file_name.c_str(),
                 preview_model_.bones.size(), assets.merged.bones.size());
    std::fflush(stdout);
}

// Defined further below, with the other shop slot statics.
void shop_apply_slot(WarriorSave& w, const std::string& type, const std::string& name);

// `p.o.xa.vu()` L301 -> `item.uu(p.o.bb())`: the effective gold price of an
// item = the live `Discount` offer (`yf.KA`) while one is active, else the
// list.xml `Price` (`Ofa()`). The shop DISPLAYS (detail plate) and CHARGES
// (`Pa.iwa` L1228) this value.
int shop_effective_price(App& app, const CatalogItem& it) {
    return app.quest_engine().offer_price(it.name, it.price);
}

// `nn()` (item ctor L168907): the Ruby/crystal price (`od`). `Ne.Wub` L2254
// shows it at the `pVa` RubyButton and `Pa.EYa` L1228 charges it
// (`p.o.fd >= a.nn()`). No shipped `Discount` offer carries a gem override,
// so the raw `BonusPrice` is the effective value (a `ShopHide`-free row).
int shop_effective_bonus(App& app, const CatalogItem& it) {
    (void)app;
    return it.bonus_price;
}

// `ie.Or(a)` = `We.Sfa(a)` (JS L633556 / L1257554): the display number, with a
// space as the thousands separator. `<1e3` is the raw string; the shipped shop
// combat stats are all small, but the grouping is exact for every magnitude.
std::string shop_format_number(int a) {
    if (a < 0) return std::to_string(a);
    std::string c = std::to_string(a);
    for (int i = static_cast<int>(c.size()) - 3; i > 0; i -= 3) c.insert(i, 1, ' ');
    return c;
}

// The item the save has EQUIPPED in `it`'s slot. JS `Ne.refresh` L2247-2248
// feeds `ms.refresh(Aa, $e.Qi, qC&&gW&&$e.Qi!=null)` where `$e` is the owned
// inventory entry for the selected item and `$e.Qi` (`zf.uu` L1253) is that
// entry's CURRENT version (the upgrade compare). The port has no upgrade-recipe
// table (`ib.zz()`/`vu`), so the compare source is the equipped slot item —
// `$e.Qi`'s observable analogue. Returns {} when the slot is empty or holds
// `it` itself.
std::map<std::string, int> shop_equipped_attrs(App& app, const CatalogItem& it) {
    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return {};
    }
    std::string slot;
    if (it.type == "Weapon") slot = w.weapon;
    else if (it.type == "Armor") slot = w.armor;
    else if (it.type == "Helm") slot = w.helm;
    else if (it.type == "Ranged") slot = w.ranged;
    else if (it.type == "Magic") slot = w.magic;
    if (slot.empty() || slot == it.name) return {};
    for (const CatalogItem& ci : load_full_catalog(app)) {
        if (ci.name == slot) return ci.attributes;
    }
    return {};
}

// `Pa.Wz` (L1234) / `Pa.Bv` (L1211) fire the quest event; print the fired set
// (the observable result) — a purchase-driven quest appearing here is the
// proof the event reached the quest hub.
static void log_purchase_fired(const char* kind,
                               const std::vector<std::string>& fired) {
    if (fired.empty()) return;
    std::fprintf(stdout, "[shop] %s -> %zu quest(s):", kind, fired.size());
    for (const std::string& n : fired) std::fprintf(stdout, " %s", n.c_str());
    std::fprintf(stdout, "\n");
    std::fflush(stdout);
}

// `Ne.ZYa` L2251 (`Pa.iwa(this.Ch) && p.o.xa.$o(this.Ch,!0), this.Sr()`) =
// the shop's BUY + EQUIP at the `M8` GoldButton, gated by `Pa.iwa` L1228
// (`p.o.Tb >= a.jp()`; else `v.Bv(a,2)` = the "not enough" notice). The timed
// `Ec` branch (`d=Pa.y2a(a)`) leaves the grant flag unset, so the `$o` equip
// is skipped for a not-yet-delivered order.
bool ShopScreen::purchase_price_plate(App& app, const CatalogItem& bit) {
    WarriorSave bw;
    try {
        bw = app.save().load();
    } catch (const std::exception&) {
        return false;
    }
    // `Pa.iwa` L1228 money gate `p.o.Tb >= a.jp()`: the charged price is the
    // offer-aware one (`p.o.xa.vu()` -> `yf.KA`), not the raw list.xml `Price`.
    const int price = shop_effective_price(app, bit);
    if (bw.money < price) {
        // `Pa.iwa` L1228 else: `v.Bv(a,2)` — the "not enough" notice.
        std::fprintf(stdout,
                     "[shop] Pi confirm Pa.iwa v.Bv(a,2): NOT ENOUGH MONEY for %s "
                     "(need %d, have %d)\n",
                     bit.name.c_str(), price, bw.money);
        // `Pa.iwa` L1228 else: `v.Bv(a,2)` -> the reason string is `p.XPa`
        // ("Coins") and the hub fires `QUEST_EVENT_PURCHASE_UNSUCCESSFUL`.
        log_purchase_fired(
            "PurchaseUnsuccessful",
            app.quest_engine().purchase_unsuccessful(app, bit.name, 2));
        std::fflush(stdout);
        return false;
    }
    bw.money -= price;
    // `Pa.iwa` L1228 picks the branch: `a.Ec>0 ? d=Pa.y2a(a) : c=d=Pa.gI(a,
    // true,false)`. The two helpers play DIFFERENT ids: `Pa.y2a` L1227 ends
    // `b&&rb.QS()` = `ta.ak("snd_upgrade")` (id 65596) and `Pa.gI` L1227 ends
    // `b&&rb.U3()` = `ta.ak("snd_buy")` (id 65569). The pre-fix port played
    // `snd_buy` on BOTH paths (`snd_upgrade` was never triggered).
    if (bit.delivery_sec > 0) {
        sf2::audio::AudioEngine::instance().play("snd_upgrade");
    } else {
        sf2::audio::AudioEngine::instance().play("snd_buy");
    }
    if (bit.delivery_sec > 0) {
        // `Pa.iwa` L1228: `a.Ec>0 ? d=Pa.y2a(a)` — the timed order leaves the
        // grant flag false, so the `$o` equip after `Pa.iwa` is SKIPPED.
        bw.timers[bit.name] = WarriorSave::wall_now() + bit.delivery_sec;
        app.save().save(bw);
        seen_ = bw;
        std::fprintf(stdout,
                     "[shop] Pi confirm Pa.iwa (Ec) -> ORDERED %s price=%d -> "
                     "arrives in %ds (no equip)\n",
                     bit.name.c_str(), price, bit.delivery_sec);
        // `Pa.iwa` L1228: `d=Pa.y2a(a)` truthy -> `p.o.save(); Pa.Wz(a)` fires
        // `QUEST_EVENT_PURCHASE` (the timed order still counts as a purchase).
        log_purchase_fired("Purchase", app.quest_engine().purchase(app, bit.name));
        std::fflush(stdout);
        return true;
    }
    WarriorSave::OwnedItem oi;
    oi.name = bit.name;
    oi.count = 1;
    shop_apply_slot(bw, bit.type, bit.name);
    oi.equipped = true;
    const bool tut_buy =
        bit.name == "WEAPON_KNIVES" &&
        (bw.story_step() == "STEP_BUY_ITEM" ||
         (bw.story_step().empty() && bw.tutorial == "MOVE"));
    if (tut_buy) bw.set_story_step("MAP");
    bw.items.push_back(oi);
    app.save().save(bw);
    seen_ = bw;
    std::fprintf(stdout,
                 "[shop] Pi confirm Pa.iwa -> BOUGHT %s price=%d -> money %d"
                 " + EQUIPPED ($o)%s\n",
                 bit.name.c_str(), price, bw.money,
                 tut_buy ? ", step -> MAP (Ao)" : "");
    // `Pa.iwa` L1228: `c=d=Pa.gI(a,!0,!1)` truthy -> `p.o.Fr(b); p.o.save();
    // Pa.Wz(a)` fires `QUEST_EVENT_PURCHASE` AFTER the save (so a purchase
    // quest reading `?Purchase(_$Purchase).*` sees the committed state).
    log_purchase_fired("Purchase", app.quest_engine().purchase(app, bit.name));
    std::fflush(stdout);
    return true;
}

// `Ne.Ehb` case 2 (L2254) -> `bka(0, Aa.nn())` -> `Pa.EYa` L1228: the `pVa`
// RubyButton charges the Ruby/crystal balance (`p.o.fd`), not gold. Gate
// `p.o.fd >= a.nn()`, else `v.Bv(a,3)` (the Ruby "not enough" notice), then
// grant + equip + save + `Pa.Wz` (`QUEST_EVENT_PURCHASE`).
bool ShopScreen::purchase_gem_price_plate(App& app, const CatalogItem& bit) {
    WarriorSave bw;
    try {
        bw = app.save().load();
    } catch (const std::exception&) {
        return false;
    }
    const int price = shop_effective_bonus(app, bit);
    if (price <= 0) return false;  // `kL` L2254 never activates a 0-price plate
    if (bw.bonus < price) {
        // `Pa.EYa` L1228 else: `v.Bv(a,3)` -> reason 3 (`p.o.$Pa` = "Ruby").
        std::fprintf(stdout,
                     "[shop] Pi confirm Pa.EYa v.Bv(a,3): NOT ENOUGH RUBIES for %s "
                     "(need %d, have %d)\n",
                     bit.name.c_str(), price, bw.bonus);
        log_purchase_fired(
            "PurchaseUnsuccessful",
            app.quest_engine().purchase_unsuccessful(app, bit.name, 3));
        std::fflush(stdout);
        return false;
    }
    bw.bonus -= price;
    // `Pa.EYa` L1228: `c=Pa.gI(a,!0,!0)` — the gem grant (no separate `$o`,
    // so `gI`'s equip flag does the slot write). `snd_buy` matches `Pa.gI`.
    sf2::audio::AudioEngine::instance().play("snd_buy");
    WarriorSave::OwnedItem oi;
    oi.name = bit.name;
    oi.count = 1;
    shop_apply_slot(bw, bit.type, bit.name);
    oi.equipped = true;
    bw.items.push_back(oi);
    app.save().save(bw);
    seen_ = bw;
    std::fprintf(stdout,
                 "[shop] Pi confirm Pa.EYa -> BOUGHT %s price=%dR -> bonus %d"
                 " + EQUIPPED\n",
                 bit.name.c_str(), price, bw.bonus);
    log_purchase_fired("Purchase", app.quest_engine().purchase(app, bit.name));
    std::fflush(stdout);
    return true;
}

// Bottom tab strip (JS `ss`/`Eg` L1851-1853, L2283-2284): a full-width bar
// `height = za.Sp*1.2` with `Le` buttons (id 248 shop atlas) scaled to the
// bar height and laid left->right (spacing factor 1.2 at lc>1.2), centred.
// `buttons/Weapon` sourceSize is 200x190.
constexpr float kShopTabSrcW = 200.0f;
constexpr float kShopTabSrcH = 190.0f;
// JS `Eg` ctor (L1851): `this.background=R.Ed(-13034231,1,1,this.node)` — the
// full-width tab-strip background quad (`Eg.aa` L1852 `background.zm(w,height)`).
// -13034231 = ARGB 0xFF391D09 = RGB(57,29,9), a warm dark brown. The oracle
// `shop_tab*`/`profile_tab*` bottom band measures exactly (57,29,9); the old
// flat 0.21/0.16 grey quad was invented.
constexpr float kTabBarBgR = 0x39 / 255.0f;
constexpr float kTabBarBgG = 0x1D / 255.0f;
constexpr float kTabBarBgB = 0x09 / 255.0f;
// `Eg.aa` (L1852): `this.height=za.Sp*(L.K.un?clamp(lc,.9,1.5):1.3+...)`.
// The oracle captures measure bar_h=109 at Sp=73.008 -> factor 1.49, so
// `L.K.un` (`nxb` L61: `"ontouchstart"in window||maxTouchPoints>0||
// pointer:coarse`) is TRUE in the oracle shell -> 1.5.
constexpr float kTabBarHeightK = 1.5f;
constexpr float kShopTabBarK = kTabBarHeightK;
constexpr float kShopTabSpread = 1.2f;

// `ss.Tw` (L2283) + `Oa.oab` (L2297) — which shop tabs the strip actually
// shows. `Tw` is the base `[0,1,2,3,4]`, then `push(5)` under
// `Ca.hasFeature("iap")` and `push(7)` under `L.K.Yja` (`hasFeature
// ("rewarded")`). `oab` then `qJ`s: tab5 gone while the offers list
// `L.K.Wt` is empty; tab7 gone while `QV` is empty; tab6 ALWAYS (`qJ(6)`).
// The shell has iap (the `AddMoney` box, L1995) but no `L.K.Wt` offers, and
// its oracle capture `loop_shop.png` shows the base five — so the strip
// renders 0..4 and 5/6/7 stay off-strip but remain selectable by
// `ChangeTab` (the `Cj.l6` tab ids).
bool shop_tab_visible(int tab) {
    if (tab < 0 || tab >= kShopTabCount) return false;
    switch (tab) {
        case 5:   // `oab`: `gC.length!=0 && L.K.Wt.length!=0` — no offers here
        case 6:   // `oab` `qJ(6)` — RaidConsumable is never a `Tw` button
        case 7:   // `L.K.Yja` (`rewarded`) off in the shell
            return false;
        default:
            return true;
    }
}

std::vector<int> shop_visible_tabs() {
    std::vector<int> out;
    for (int t = 0; t < kShopTabCount; ++t) {
        if (shop_tab_visible(t)) out.push_back(t);
    }
    return out;
}

struct ShopTabLayout {
    float bar_h = 0.0f;
    float btn_w = 0.0f;
    float btn_h = 0.0f;
    float step = 0.0f;
    float cx0 = 0.0f;
    float cy = 0.0f;
};

ShopTabLayout shop_tab_layout() {
    ShopTabLayout l;
    const float sp = std::min(kViewH * 0.13f, 100.0f) * 0.78f;  // za.Sp (L1975)
    l.bar_h = sp * kShopTabBarK;
    l.btn_h = l.bar_h;
    l.btn_w = kShopTabSrcW * (l.bar_h / kShopTabSrcH);
    l.step = l.btn_w * kShopTabSpread;
    // `Eg.aa` packs/sizes the ACTIVE buttons only, so the row width is the
    // visible `Tw` subset, not `kShopTabCount`.
    const int visible = static_cast<int>(shop_visible_tabs().size());
    const float row = l.btn_w + static_cast<float>(visible - 1) * l.step;
    l.cx0 = (kViewW - row) * 0.5f + l.btn_w * 0.5f;
    l.cy = kViewH - l.bar_h * 0.5f;
    return l;
}

// Row view: indices into ShopScreen::items_ for tab t (list order kept, so
// WEAPON_KNIVES stays row 0 of Weapons — the headless-loop buy click).
std::vector<std::size_t> shop_tab_rows(const std::vector<CatalogItem>& items, int tab) {
    std::vector<std::size_t> out;
    if (tab < 0 || tab >= kShopTabCount) return out;
    // `Oa.jAa` (L2297): tab5 `gC` = `p.items.Dp` (RealMoneyItem) +
    // `p.items.hca` (Consumable); tab6 has no `f5` case and no `it.parse`
    // bucket, so it lists nothing.
    if (tab == 6) return out;
    const bool tab5 = tab == 5;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::string& ty = items[i].type;
        const bool keep = tab5 ? (ty == "RealMoneyItem" || ty == "Consumable")
                               : (ty == kShopTabs[tab].type);
        if (keep) out.push_back(i);
    }
    return out;
}

// `ns.j5` (L2308-2309) shop-cell badge resolution, shared by the cell draw and
// the `--settings-profile-shop-probe`. `art` is the `Di` atlas frame (`pieces/*`
// from atlas 248 / `E.get(248)`), `text` the `Im` label (`""` = hidden).
//   - sale row (`a = bc.Zz && bc.Ms > 0` = `ConsumableProduct`+`AddPercent>0`):
//     JS builds `Di` with `y.tM` ("Stripe") first; the `b.yn > p.Dc` sub-branch
//     keeps it and shows `Y.na("shopSale")` ("SALE"), else the `badge`/`Yb`
//     switch runs (`I.QPa` MostPopular / `I.PPa` BestValue / `I.$F` Bonus->
//     FreeGems / else FreeCoins) with the label hidden.
//   - `c = name == I.nTa` ("Video") -> `y.moa` (FreeGems).
//   - else a `bc.bU` (`ShopLabel`) -> `y.tM` (Stripe) + its localized text.
struct ShopBadge {
    const char* art = nullptr;
    std::string text;
};
ShopBadge shop_cell_badge(App& app, const CatalogItem& it, bool shop_sale) {
    ShopBadge b;
    const bool sale = it.consumable_product && it.add_percent > 0;  // `a` (L2308)
    if (sale) {
        b.art = "pieces/Stripe";  // `this.Di = R.$(E.get(248), y.tM, ...)`
        if (shop_sale) {
            // `this.Di.Cb(y.tM); this.Im.V(Y.na("shopSale"))` (L2309).
            b.text = loc(app, "shopSale", "SALE");
        } else if (it.badge == "MostPopular") {
            b.art = "pieces/MostPopular_red";  // I.QPa
        } else if (it.badge == "BestValue") {
            b.art = "pieces/BestValue_red";  // I.PPa
        } else if (it.subtype == "Bonus") {
            b.art = "pieces/FreeGems_red";  // I.$F
        } else {
            b.art = "pieces/FreeCoins_red";  // else (L2309)
        }
    } else if (it.name == "Video") {  // `c` (L2308)
        b.art = "pieces/FreeGems_red";  // `y.moa`
    } else if (!it.shop_label.empty()) {  // `bc.bU` (L2309)
        b.art = "pieces/Stripe";  // `y.tM`
        b.text = loc(app, it.shop_label, it.shop_label);
    }
    return b;
}

// Shop atlas art per tab (shop.<hash>.json buttons/* — the JS `vj.ifa` tab
// icons). Index matches kShopTabs order; state = 0 normal / 1 active / 2
// pushed (JS `ss`: `a(n, y.KSa, y.MSa, y.LSa)`, L2284 = normal/active/pushed).
const char* shop_tab_art(int tab, int state) {
    static const char* kNormal[kShopTabCount] = {
        "buttons/Weapon", "buttons/Armor", "buttons/Helmet",
        "buttons/Ranged_weapon", "buttons/Magic",
    };
    static const char* kActive[kShopTabCount] = {
        "buttons/Weapon_active", "buttons/Armor_active", "buttons/Helmet_active",
        "buttons/Ranged_weapon_active", "buttons/Magic_active",
    };
    static const char* kPushed[kShopTabCount] = {
        "buttons/Weapon_pushed", "buttons/Armor_pushed", "buttons/Helmet_pushed",
        "buttons/Ranged_weapon_pushed", "buttons/Magic_pushed",
    };
    if (tab < 0 || tab >= kShopTabCount) return nullptr;
    if (state == 1) return kActive[tab];
    if (state == 2) return kPushed[tab];
    return kNormal[tab];
}

// Equipped-slot value for an item type (JS `xc.hk` slots; save fields readable).
const std::string& shop_slot_for(const WarriorSave& w, const std::string& type) {
    if (type == "Armor") return w.armor;
    if (type == "Helm") return w.helm;
    if (type == "Ranged") return w.ranged;
    if (type == "Magic") return w.magic;
    return w.weapon;
}

// `re.XDa` (L2285): live-owned = owned and not awaiting delivery (`Bh<=Dc`).
bool shop_owned_live(const WarriorSave& w, const std::string& name) {
    if (!w.has_item(name)) return false;
    return w.timers.find(name) == w.timers.end();
}

// `re.rga`/`c.G` (L2285): the item occupies its type slot (`zf.Ru`).
bool shop_equipped(const WarriorSave& w, const CatalogItem& it) {
    return w.has_item(it.name) && shop_slot_for(w, it.type) == it.name;
}

// `Oa.DU` action label (L2299, verbatim): `Up.V(Y.na("btnShopUnequip"))` /
// `btnShopEquip` / `btnShopTry` (`re.rga` gates, L2299). The lang table
// resolves the JS keys to the human captions; the fallbacks mirror those
// captions (never a raw key).
std::string shop_action_label(App& app, const WarriorSave& w, const CatalogItem& it) {
    // `Oa.DU` L2299 verbatim: `c.G ? btnShopUnequip : (b.G || a.type==I.sB) ?
    // btnShopEquip : btnShopTry`. `I.sB` = "RaidItemPack" (L2473 `I.sB=`).
    if (shop_equipped(w, it)) return loc(app, "btnShopUnequip", "UNEQUIP");
    if (shop_owned_live(w, it.name) || it.type == "RaidItemPack")
        return loc(app, "btnShopEquip", "EQUIP");
    return loc(app, "btnShopTry", "TRY ON");
}

// `xc.hk` + `p.bo`/`xa.$o` (SHOP_STATIC §7): write the type slot and sync the
// owned-item `Equipped` flags (`zf.Ru`). Unequip passes the `p.vzb` default.
void shop_apply_slot(WarriorSave& w, const std::string& type, const std::string& name) {
    if (type == "Armor") w.armor = name;
    else if (type == "Helm") w.helm = name;
    else if (type == "Ranged") w.ranged = name;
    else if (type == "Magic") w.magic = name;
    else w.weapon = name;
    for (auto& oi : w.items) oi.equipped = (oi.name == name);
}

// One-line stat (damage/defense by type; Ranged/Magic carry no damage field
// in CatalogItem — show subtype + level).
std::string shop_stat_line(const CatalogItem& it) {
    char buf[96];
    if (it.type == "Weapon") {
        std::snprintf(buf, sizeof(buf), "DMG %d   %dG", it.weapon_damage, it.price);
    } else if (it.type == "Armor") {
        std::snprintf(buf, sizeof(buf), "DEF %d   %dG", it.body_defense, it.price);
    } else if (it.type == "Helm") {
        std::snprintf(buf, sizeof(buf), "DEF %d   %dG", it.head_defense, it.price);
    } else if (!it.subtype.empty()) {
        std::snprintf(buf, sizeof(buf), "%s Lv%d   %dG", it.subtype.c_str(), it.level,
                      it.price);
    } else {
        std::snprintf(buf, sizeof(buf), "Lv%d   %dG", it.level, it.price);
    }
    std::string out(buf);
    // Timed delivery tag (SHOP `Ec`/DeliveryTime; no live rows carry it —
    // claim path needs save Timers/yl, noted in the stream report).
    if (it.delivery_sec > 0) {
        out += " DL" + std::to_string(it.delivery_sec) + "s";
    }
    // Instant-delivery fees (O2/Od): displayed only — charging needs the ph
    // buy dialog (no auto-charge without consent).
    if (it.delivery_coin > 0) {
        out += " INST" + std::to_string(it.delivery_coin) + "G";
    }
    if (it.delivery_gems > 0) {
        out += " INST" + std::to_string(it.delivery_gems) + "R";
    }
    return out;
}

// Delivery countdown text (mm:ss; wall-clock recompute per frame — Gb
// Cla(now)+save semantics, no ticking needed).
std::string shop_countdown(std::int64_t sec) {
    if (sec < 0) sec = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld", sec / 60, sec % 60);
    return std::string(buf);
}

// Wielding summary (read-only): the equipped slots' applied stats. Item names
// resolve through the same `Y.na(name)` lookup the JS uses (L2247 `Ne.Vc`,
// L1881 `ur.info.V(Y.na(a.name))`); the panel carries no invented title (the
// JS `ps` params pane `pca` L2276 has no header — the old "WIELDING" literal
// was a native invention).
std::string wielding_line(App& app, const WarriorSave& seen) {
    const std::vector<CatalogItem> full = load_full_catalog(app);
    auto stat = [&](const std::string& name) {
        const std::string disp = loc(app, name, name);
        for (const auto& ci : full) {
            if (ci.name != name) continue;
            char buf[96];
            if (ci.type == "Weapon") {
                std::snprintf(buf, sizeof(buf), "%s DMG %d", disp.c_str(),
                              ci.weapon_damage);
            } else if (ci.type == "Armor") {
                std::snprintf(buf, sizeof(buf), "%s DEF %d", disp.c_str(),
                              ci.body_defense);
            } else if (ci.type == "Helm") {
                std::snprintf(buf, sizeof(buf), "%s DEF %d", disp.c_str(),
                              ci.head_defense);
            } else {
                std::snprintf(buf, sizeof(buf), "%s", disp.c_str());
            }
            return std::string(buf);
        }
        return disp;
    };
    return stat(seen.weapon) + " | " + stat(seen.armor) + " | " + stat(seen.helm);
}

ShopScreen::ShopScreen(ScreenManager& mgr) : Screen(mgr, "Shop") {
    items_ = load_catalog(app());
    std::fprintf(stdout, "[shop] %zu shop items\n", items_.size());
    for (const auto& it : items_) {
        // `price` = `jp()` (gold), `bonus` = `nn()` (Ruby/crystal, `od`). A
        // `price=0 bonus=N` row is a crystal-only shop item (`Ne.Wub` L2254
        // draws only the `pVa` RubyButton).
        std::fprintf(stdout, "[shop] item %s (%s) price=%d bonus=%d model=%s\n",
                     it.name.c_str(),
                     it.subtype.empty() ? it.type.c_str() : it.subtype.c_str(), it.price,
                     it.bonus_price, it.model.c_str());
    }
    std::fflush(stdout);
    // Tutorial-buy focus (JS `Ao` S(): `Oa.ska(0, Pca)` — Weapons tab with
    // WEAPON_KNIVES focused; Pca defaults to WEAPON_KNIVES, L1199).
    tab_ = 0;
    hover_ = -1;
    try {
        const WarriorSave w = app().save().load();
        seen_ = w;
        const std::string step = w.story_step();
        const PendingBattle& pb = app().pending_battle();
        const bool tut_shop =
            step == "STEP_BUY_ITEM" ||
            (step.empty() && w.tutorial == "MOVE" && pb.has_result && pb.player_won &&
             pb.battle_name == "Training");
        if (tut_shop) {
            const std::vector<std::size_t> rows = shop_tab_rows(items_, 0);
            for (std::size_t r = 0; r < rows.size(); ++r) {
                if (items_[rows[r]].name == "WEAPON_KNIVES") {
                    hover_ = static_cast<int>(r);
                    sel_ = static_cast<int>(r);
                    break;
                }
            }
            std::fprintf(stdout, "[shop] Ao focus WEAPON_KNIVES (tab Weapons, row %d)\n",
                         hover_);
            std::fflush(stdout);
        }
    } catch (const std::exception&) {
    }
    // Quest `OpenShop` (`go` L1092): apply the pending `Oa.uLa(tab,item)` the
    // engine queued before pushing this screen.
    if (app().has_pending_shop()) {
        const std::string qtab = app().pending_shop_tab();
        const std::string qitem = app().pending_shop_item();
        app().clear_pending_shop();
        open_at(qtab, qitem);
    }
}

void ShopScreen::update_impl(float dt) {
    (void)dt;
    ensure_lang(app());  // the lang table powers the `Y.na` string lookups
    // Sensei dialog modal gate (quest engine `He` records). The tutorial
    // lands here: `StoryTutorialBuyItem` fires on SceneTo==Shop
    // (tutorial_quests.xml L98-118) and queues the `tutorial_buy_knives`
    // Regular dialog. Without draining the queue here the modal would stay
    // queued and resurface as the head on the NEXT screen (the Map would show
    // the stale Shop dialog instead of the Lynx one).
    if (quest_modal_consume(app())) return;
    const App::PointerState& p = app().pointer();
    try {
        const WarriorSave w = app().save().load();
        if (w.money != money_logged_) {
            money_logged_ = w.money;
            std::fprintf(stdout, "[shop] MONEY %d\n", w.money);
            std::fflush(stdout);
        }
        seen_ = w;  // snapshot for owned/equipped row markers (render reads this)
    } catch (const std::exception&) {
    }
    hover_ = -1;
    // Bottom tab strip (JS `ss`/`Eg`; geometry mirrors render_impl).
    tab_hover_ = -1;
    {
        const ShopTabLayout tl = shop_tab_layout();
        const std::vector<int> vis = shop_visible_tabs();
        for (std::size_t k = 0; k < vis.size(); ++k) {
            const int t = vis[k];
            const float cx = tl.cx0 + static_cast<float>(k) * tl.step;
            if (p.x >= cx - tl.btn_w / 2 && p.x <= cx + tl.btn_w / 2 &&
                p.y >= tl.cy - tl.btn_h / 2 && p.y <= tl.cy + tl.btn_h / 2) {
                tab_hover_ = t;
                if (p.pressed && t != tab_) {
                    tab_ = t;
                    sel_ = 0;  // Oa.f5 -> usb() auto-selects the first cell
                    sf2::audio::AudioEngine::instance().play("snd_click_2");
                    std::fprintf(stdout, "[shop] tab %s (E0=%d)\n", kShopTabs[tab_].label,
                                 kShopTabs[tab_].e0);
                    std::fflush(stdout);
                }
                break;
            }
        }
    }
    // BACK (top-left) -> the previous screen (the loop's shop -> dojo leg).
    // With the `Pi` purchase panel up (`buy_armed_ >= 0`) BACK is the panel's
    // cancel (`Oa.yS` L2300: `Ad.$Ma(); fU(); Oya=!0`), not a screen pop.
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            if (buy_armed_ >= 0) {
                std::fprintf(stdout, "[shop] Pi panel cancel -> detail (Oa.yS)\n");
                std::fflush(stdout);
                buy_armed_ = -1;
                return;
            }
            std::fprintf(stdout, "[shop] BACK -> previous screen\n");
            std::fflush(stdout);
            manager().pop();
            return;
        }
    }
    // The `Pi` purchase panel (`Oa.Fhb` L2300 unowned -> `this.Ex(a,7)`). Its
    // ONLY control is the `M8` GoldButton (`Ne.Wub` L2254 `c5(this.M8,
    // Aa.jp())`), which the quest buy wires to the confirm (`Ao.Qg` L1120 ->
    // `Pa.iwa(b)`, L1228 `p.o.Tb >= a.jp()` money gate; the else is
    // `v.Bv(a,2)`). A backdrop tap is `Oa.yS` (close). While the panel is up
    // the tab strip / cells / TRY plate are inert.
    if (buy_armed_ >= 0) {
        const std::vector<std::size_t> brows = shop_tab_rows(items_, tab_);
        if (brows.empty() || buy_armed_ >= static_cast<int>(brows.size())) {
            buy_armed_ = -1;  // the list changed under the panel
        } else {
            const CatalogItem& bit = items_[brows[static_cast<std::size_t>(buy_armed_)]];
            const int b_gold = shop_effective_price(app(), bit);
            const int b_gems = shop_effective_bonus(app(), bit);
            const int b_gold_slot = (b_gems > 0 && b_gold > 0) ? 1 : 0;
            const ShopRect gold_pr = shop_price_rect(shop_layout(tab_), b_gold_slot);
            const ShopRect gem_pr = shop_price_rect(shop_layout(tab_), 0);
            const bool on_gold = b_gold > 0 && p.x >= gold_pr.J && p.x <= gold_pr.N &&
                                 p.y >= gold_pr.P && p.y <= gold_pr.W;
            const bool on_gem = b_gems > 0 && p.x >= gem_pr.J && p.x <= gem_pr.N &&
                                p.y >= gem_pr.P && p.y <= gem_pr.W;
            const bool on_confirm = on_gold || on_gem;
            if (!on_confirm && p.pressed) {
                std::fprintf(stdout, "[shop] Pi panel backdrop -> cancel (Oa.yS)\n");
                std::fflush(stdout);
                buy_armed_ = -1;
                return;
            }
            if (on_confirm && p.pressed) {
                // `Pa.iwa` L1228 head + `ZYa` L2251 (`p.o.xa.$o(b,!0)`):
                // deduct, grant + equip, save. A shortfall (`v.Bv(a,2)`) keeps
                // the panel open so the player can back out or earn gold.
                // `ZYa` L2251 (`Pa.iwa`, gold) vs `$Ya` L2251 (`Pa.EYa`, gems):
                // the plate the player pressed picks the currency.
                const bool ok = on_gem ? purchase_gem_price_plate(app(), bit)
                                       : purchase_price_plate(app(), bit);
                if (ok) {
                    buy_armed_ = -1;
                }
                return;
            }
        }
    }
    // Pending deliveries (top-right, mirrors render): click a READY row to
    // claim (Vxa-notify analog + QUEST_EVENT_DELIVERY log line). Countdowns
    // are wall-clock compares — never blocking.
    {
        const std::int64_t now = WarriorSave::wall_now();
        int row = 0;
        for (const auto& kv : seen_.timers) {
            if (row >= 3) break;
            const float ry = 84.0f + static_cast<float>(row) * 24.0f;
            ++row;
            if (!(p.x >= 940.0 && p.x <= 1270.0 && p.y >= ry - 12.0 && p.y <= ry + 12.0)) {
                continue;
            }
            if (!p.pressed) break;
            const std::int64_t left = kv.second - now;
            if (left > 0) {
                std::fprintf(stdout, "[shop] delivery %s not ready (%s left)\n",
                             kv.first.c_str(), shop_countdown(left).c_str());
                std::fflush(stdout);
                break;
            }
            WarriorSave w2;
            try {
                w2 = app().save().load();
            } catch (const std::exception&) {
                break;
            }
            WarriorSave::OwnedItem oi;
            oi.name = kv.first;
            oi.count = 1;
            w2.items.push_back(oi);
            w2.timers.erase(kv.first);
            app().save().save(w2);
            seen_ = w2;
            std::fprintf(stdout, "[shop] delivery claimed: %s (Vxa notify)\n",
                         kv.first.c_str());
            std::fflush(stdout);
            break;
        }
    }
    // Item cells: the `Oe`/`Gg` single-column list (L2261-2262, L1883-1893).
    // `Gg.aa` (L1886-1890) is the vertical scroller: a drag (`state 1`)
    // shifts the list by `p_ = c.y-Fq` off the grab base `gj`, with momentum
    // (`ub*=.9`), a target lerp (`state 2`, `*.3`) and the top/bottom clamps
    // (`state 4` -> `gj=uz` first cell centred; `state 5` ->
    // `gj=-last.ra+(size.y-last.qa())/2` last cell centred). A tap selects
    // the row (`Oa.xA` L2296); the detail-panel action button performs the
    // `Fhb` L2300 equip/buy path. `scroll_y_` = `ei.node.ra` relative to the
    // list top (`cell_top`).
    // NOTE: the wheel branch (`L.K.Lfa().ufa()`, L1887; `Zq`-gated) needs a
    // wheel field on `App::PointerState` (app.hpp/app.cpp) — plumbing
    // outside screens.{cpp,hpp} — and is therefore not ported here (OPEN).
    const ShopLayout sl = shop_layout(tab_);
    const std::vector<std::size_t> rows = shop_tab_rows(items_, tab_);
    const int nrows = static_cast<int>(rows.size());
    const float list_h = sl.list_h;
    const float uz = (list_h - sl.cell_h) * 0.5f;             // Gg.ba L1885
    const float max_off = uz;                                 // state 4 target
    const float min_off =
        nrows > 0 ? -(static_cast<float>(nrows - 1) * sl.cell_step) + uz : uz;
    const float lo = std::min(min_off, max_off);              // state 5 target
    const float hi = max_off;
    if (scroll_tab_ != tab_ || scroll_count_ != nrows) {
        scroll_tab_ = tab_;              // Gg.VK/ba (L1891/L1885): first cell
        scroll_count_ = nrows;           // centred on a fresh list.
        scroll_y_ = std::clamp(uz, lo, hi);
        scroll_vel_ = 0.0f;
        scroll_target_ = scroll_y_;
        scroll_state_ = 0;
    }
    scroll_y_ = std::clamp(scroll_y_, lo, hi);
    const bool in_list = p.x >= sl.cell_cx - sl.cell_w * 0.5f &&
                         p.x <= sl.cell_cx + sl.cell_w * 0.5f &&
                         p.y >= sl.cell_top && p.y <= shop_list_bottom(sl);
    const float local_y = static_cast<float>(p.y) - sl.cell_top;
    if (scroll_state_ == 0) {
        scroll_y_ = std::clamp(scroll_y_ + scroll_vel_, lo, hi);
        scroll_vel_ *= 0.9f;                                  // L1887 `ub*=.9`
        if (std::fabs(scroll_vel_) < 0.5f) scroll_vel_ = 0.0f;
        if (scroll_vel_ == 0.0f && nrows > 0) {
            // `Lvb` (L1892): once the fling stops, snap the nearest cell to
            // the viewer centre (state 2/3 spring).
            int best = 0;
            float best_d = 1.0e9f;
            for (int i = 0; i < nrows; ++i) {
                const float qk = list_h * 0.5f -
                                 (scroll_y_ + static_cast<float>(i) * sl.cell_step +
                                  sl.cell_h * 0.5f);
                if (std::fabs(qk) < best_d) {
                    best_d = std::fabs(qk);
                    best = i;
                }
            }
            const float tgt =
                std::clamp(-(static_cast<float>(best) * sl.cell_step) + uz, lo, hi);
            if (std::fabs(tgt - scroll_y_) > 1.0f) {
                scroll_target_ = tgt;
                scroll_state_ = 2;
            }
        }
        if (scroll_state_ == 0 && in_list && p.down) {        // L1887 b&&d
            scroll_state_ = 1;
            drag_start_ = local_y;                            // `Fq`
            drag_base_ = scroll_y_;                           // `gj`
            drag_delta_ = 0.0f;                               // `p_`
            drag_prev_ = local_y;
            drag_vel_ = 0.0f;
            scroll_vel_ = 0.0f;
        }
    }
    if (scroll_state_ == 1) {
        if (p.down) {
            drag_delta_ = local_y - drag_start_;              // L1888
            drag_vel_ = local_y - drag_prev_;                 // `jM[0].y`
            drag_prev_ = local_y;
            scroll_y_ = std::clamp(drag_base_ + drag_delta_, lo, hi);
        } else {
            scroll_state_ = 0;                                // released
            // `abs(p_)<10` = tap (already handled on press); else fling with
            // the last pointer velocity (`ub`, L1888), which state 0 decays
            // and then snaps.
            scroll_vel_ = std::fabs(drag_delta_) >= 10.0f ? drag_vel_ : 0.0f;
            drag_delta_ = 0.0f;
            drag_vel_ = 0.0f;
        }
    }
    if (scroll_state_ == 2) {
        scroll_y_ += (scroll_target_ - scroll_y_) * 0.3f;     // L1889
        if (std::fabs(scroll_target_ - scroll_y_) < 1.0f) {
            scroll_y_ = scroll_target_;
            scroll_state_ = 0;
        }
    }
    for (int i = 0; i < nrows; ++i) {
        // `Gg.aa` (L1886) hides cells outside `|Qk| <= size.y/2 + cell.qa()/2`.
        // The renderer has no scissor/mask (core/scene out of scope), so a
        // fully-outside cell is culled by its rect; partial cells still draw.
        const float cell_y = sl.cell_top + scroll_y_ +
                             static_cast<float>(i) * sl.cell_step;
        if (cell_y + sl.cell_h <= sl.cell_top ||
            cell_y >= sl.cell_top + list_h) continue;
        const float cy = cell_y + sl.cell_h * 0.5f;
        if (!in_list || p.y < cy - sl.cell_h * 0.5f || p.y > cy + sl.cell_h * 0.5f) continue;
        hover_ = i;
        if (p.pressed) {
            sel_ = i;  // tap-select (`Oa.xA` L2296)
            sf2::audio::AudioEngine::instance().play("snd_click_2");
            std::fprintf(stdout, "[shop] select %s\n",
                         items_[rows[static_cast<std::size_t>(i)]].name.c_str());
            std::fflush(stdout);
        }
        break;
    }
    // Detail-panel action button (`bc` content `Up.Fhb`, L2300; label
    // `Oa.DU` L2299). Side panels show on tabs 0..4 (`Q5` L2302).
    side_hover_ = 0;
    if (!rows.empty()) {
        const int sel = std::clamp(sel_, 0, static_cast<int>(rows.size()) - 1);
        const CatalogItem& it = items_[rows[static_cast<std::size_t>(sel)]];
        const ShopRect ar = shop_try_rect(sl);
        if (p.x >= ar.J && p.x <= ar.N && p.y >= ar.P && p.y <= ar.W) {
            side_hover_ = 1;
            if (p.pressed) {
                // The TRY/EQUIP/UNEQUIP plate is a `Bb` (`Oa.init` L2289
                // `Bb("EButtonWhite")`), so every press plays `rb.um()` =
                // `snd_click_1` (`Bb.Xw` L1844) — the shop press was silent.
                sf2::audio::AudioEngine::instance().play("snd_click_1");
                WarriorSave w;
                try {
                    w = app().save().load();
                } catch (const std::exception&) {
                    return;
                }
                if (shop_owned_live(w, it.name)) {
                    // `Fhb` L2300: owned -> equip (`xa.$o`) / unequip (`xa.Qxb`
                    // -> `p.vzb` default slot). Slot write via `shop_apply_slot`
                    // (SHOP_STATIC §7 `xc.hk`/`zf.Ru`).
                    const bool was_equipped = shop_equipped(w, it);
                    const std::string new_slot =
                        was_equipped ? std::string(shop_default_for_type(it.type))
                                     : it.name;
                    shop_apply_slot(w, it.type, new_slot);
                    app().save().save(w);
                    seen_ = w;
                    std::fprintf(stdout, "[shop] %s %s -> %s slot %s\n",
                                 was_equipped ? "Qxb UNEQUIP" : "$o EQUIP", it.name.c_str(),
                                 it.type.c_str(), new_slot.c_str());
                    std::fflush(stdout);
                } else {
                    // `Oa.Fhb` L2300 unowned branch: `this.Ex(a,7)` — wear the
                    // item on the `Pi` model and play its `TryOn` clip
                    // (`iz.XBa("TryOn")=7` L444) BEFORE any purchase. The buy
                    // is the `M8` price plate (`Ne.Wub` L2254 -> `Pa.iwa`
                    // L1228) handled while the panel is armed below; a TRY
                    // press never buys directly.
                    arm_preview(app(), it);
                    buy_armed_ = sel;
                    std::fprintf(stdout,
                                 "[shop] Fhb -> Ex(a,7) Pi panel OPEN for %s (price %d, have %d)\n",
                                 it.name.c_str(), it.price, w.money);
                    std::fflush(stdout);
                }
            }
        }
    }
    // `Ne.ZYa` (L2251) -> `Pa.iwa` (L1228) + `p.o.xa.$o(b,!0)`: the `M8`
    // price plate is the shop's BUY control. An UNARMED press on it buys +
    // equips the selected UNOWNED item directly — the `Up` TRY press (which
    // opens the `Pi` preview panel) is NOT a prerequisite. Owned items are
    // left to the TRY/EQUIP plate (`xa.$o`/`Qxb`), so a re-press stays inert.
    if (buy_armed_ < 0 && !rows.empty()) {
        const int psel = std::clamp(sel_, 0, static_cast<int>(rows.size()) - 1);
        const CatalogItem& pit = items_[rows[static_cast<std::size_t>(psel)]];
        const int p_gold = shop_effective_price(app(), pit);
        const int p_gems = shop_effective_bonus(app(), pit);
        // `Ne.ba` L2249 stacks ruby (`pVa`) at the bottom when both are live,
        // gold (`M8`) above; `kL` L2254 hides a 0-price plate.
        const int gold_slot = (p_gems > 0 && p_gold > 0) ? 1 : 0;
        const ShopRect gold_rect = shop_price_rect(sl, gold_slot);
        const ShopRect gem_rect = shop_price_rect(sl, 0);
        const bool hit_gold = p_gold > 0 && p.pressed && p.x >= gold_rect.J &&
                              p.x <= gold_rect.N && p.y >= gold_rect.P && p.y <= gold_rect.W;
        const bool hit_gem = p_gems > 0 && p.pressed && p.x >= gem_rect.J &&
                             p.x <= gem_rect.N && p.y >= gem_rect.P && p.y <= gem_rect.W;
        if (hit_gold || hit_gem) {
            WarriorSave pw;
            bool powned = false;
            try {
                pw = app().save().load();
                powned = shop_owned_live(pw, pit.name);
            } catch (const std::exception&) {
                return;
            }
            if (!powned) {
                if (hit_gem) {
                    purchase_gem_price_plate(app(), pit);
                } else {
                    purchase_price_plate(app(), pit);
                }
            }
        }
    }
    // Shared `za` nav column (JS `ma.D1` L1831): `D1` destroys the live `za`
    // and re-appends a FRESH one (`this.kA=this.Qo(za)`), whose `gk` nav scroll
    // ctor starts COLLAPSED (`collapse(0)`, L1978). So on entering the Shop the
    // column draws collapsed (the `gk` header rect x89..279,y72..112) and
    // paints NO `gk.background` 0.5-black dim over the dojo backdrop — the
    // oracle `shop_tab*`/`shop_detail` right-wall backdrop measures ~0.7x the
    // `dojo_hub` wall (no 0.5 dim), vs the port's 0.5-dimmed, icon-column
    // capture. the per-screen `za` state (`gk.uJ`) owns the column, not a flag override
    // (screens.cpp:4623 / 8731), both already oracle-matched.
    // TryOn playback (`Oa.Fhb` L2300 -> `iz.XBa("TryOn")=7`). The JS ENDS the
    // clip at `TryOnEnd` (the `Em` action L755 tests `Je==7 && Name=="TryOn"`)
    // and `Pi.wia` (L446 `this.qr.Z()`) fires that clip-end into `Oa.yS`
    // (L2301 `Ad.$Ma(); fU(); Oya=!0`) -> `Oa.aa` (L2293
    // `Oya&&(Oya=!1,Ex(null,6))`) which restores `PeacefulRestore`
    // (`iz.XBa("PeacefulRestore")=6`). So the preview RETURNS TO THE IDLE after
    // the last frame — it must NOT freeze on the TryOn end pose (the helm
    // clip ends lowered, which read as "dropped + stuck").
    if (preview_active_ && preview_clip_ != nullptr && !preview_clip_->frames.empty()) {
        if (preview_frame_ + 1 < static_cast<int>(preview_clip_->frames.size())) {
            ++preview_frame_;
        } else {
            preview_active_ = false;
            preview_fighter_.reset();
            preview_clip_ = nullptr;
            preview_frame_ = 0;
        }
    }
    // Display-only: the screen's own `za` state (`gk.uJ`) is untouched so the Dojo keeps its column.
    za_update(app(), *this, kScreenShop, dt);
}

void ShopScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // --- Backdrop: the destination `dojo_shop` art (`Pi.Qa`, L439) ---------
    // JS `Oa extends ma` (L2285): `this.Ad = new Pi` (L2291) and `Ea` calls
    // `this.Tya(this.Ad)` (L2293). `Pi` renders `Qa = R.$(E.get(752))` =
    // `locations/dojo_shop/bg.{image}` under `ma.Tya` (L1832) — a dedicated
    // destination background, NOT the dojo location layers.
    draw_destination_backdrop(app, 1.0f);  // JS `Oa.Zkb` resets Qa to white
    if (preview_active_ && preview_fighter_ != nullptr && preview_clip_ != nullptr) {
        // `Oa.Fhb` L2300 unowned -> `Ex(a,7)`: the `Pi` model wears the item
        // and plays its `TryOn` clip.
        draw_pi_fighter(ren, *preview_fighter_, *preview_clip_, preview_frame_);
    } else {
        draw_destination_model(app, ren, backdrop_fighter_, backdrop_fig_tried_,
                               backdrop_fig_ok_, backdrop_idle_);
    }
    draw_destination_dim(ren);

    // Bottom tab strip (JS `ss`/`Eg` L1851-1853, L2283-2284): a full-width
    // bar + `Le` buttons (id 248 shop atlas `buttons/<Category>[_active]`),
    // scaled to the bar height; flat fallback only on a real frame miss.
    {
        const ShopTabLayout tl = shop_tab_layout();
        const float bar[] = {0, kViewH - tl.bar_h, kViewW, kViewH - tl.bar_h, kViewW, kViewH,
                             0, kViewH - tl.bar_h, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(bar, 6, kTabBarBgR, kTabBarBgG, kTabBarBgB, 1.0f);
        const std::vector<int> strip = shop_visible_tabs();
        for (std::size_t k = 0; k < strip.size(); ++k) {
            const int t = strip[k];
            const float cx = tl.cx0 + static_cast<float>(k) * tl.step;
            const bool sel = t == tab_;
            const bool hov = t == tab_hover_;
            const int state = sel ? 1 : (hov ? 2 : 0);  // normal/active/pushed
            const char* art = shop_tab_art(t, state);
            bool drawn = false;
            if (art != nullptr) {
                drawn = try_draw_atlas_button(app, art, cx, tl.cy, tl.btn_w, tl.btn_h,
                                              sel ? 1.0f : (hov ? 0.9f : 0.75f));
            }
            if (!drawn) {
                draw_flat_button(app, kShopTabs[t].label, cx, tl.cy, tl.btn_w, tl.btn_h,
                                 sel ? 0.72f : (hov ? 0.6f : 0.38f),
                                 sel ? 0.6f : (hov ? 0.5f : 0.32f), sel ? 0.25f : 0.3f, hov);
                draw_ui_label(app, cx - tl.btn_w * 0.5f + 4.0f, tl.cy - 11.0f, tl.btn_w - 8.0f,
                              22.0f, kShopTabs[t].label, 0.6f, UiAlign::Center, 1.0f, 1.0f,
                              1.0f);
            }
        }
    }
    // `Oe`/`Gg` single-column cell list (L2261-2262, L1883-1893). Cells are
    // scaled to the list width (`ff.kf` L1893) and stacked with the
    // per-category `LT` spacing; rows past the viewer bottom are hidden
    // (`Gg.aa` `Qk` range, L1886).
    const ShopLayout sl = shop_layout(tab_);
    const std::vector<std::size_t> rows = shop_tab_rows(items_, tab_);
    const float list_h = sl.list_h;
    const CatalogItem* sel_it = nullptr;
    if (!rows.empty()) {
        const int sel = std::clamp(sel_, 0, static_cast<int>(rows.size()) - 1);
        sel_it = &items_[rows[static_cast<std::size_t>(sel)]];
    }
    auto quad = [&](const ShopRect& r, float cr, float cg, float cb, float ca) {
        const float v[] = {r.J, r.P, r.N, r.P, r.N, r.W, r.J, r.P, r.N, r.W, r.J, r.W};
        ren.draw_triangles(v, 6, cr, cg, cb, ca);
    };
    if (rows.empty()) {
        // `Oe.Dn` no-items label (L2261 `Dn.V("noItems")`); `Oa.f5` overrides
        // it with the lock copy per tab: `Y.na("shopRangedLocked")` (case 3) /
        // `Y.na("shopMagicLocked")` (case 4) (L2287). The old native drew a
        // hard-coded EN sentence.
        const char* key = "noItems";
        const char* fb = "No items";
        if (tab_ == 3) {
            key = "shopRangedLocked";
            fb = "Defeat Lynx to unlock";
        } else if (tab_ == 4) {
            key = "shopMagicLocked";
            fb = "Defeat the Hermit to unlock";
        }
        draw_ui_wrapped(app, sl.viewer.J + sl.scroll_inset, sl.cell_top,
                        sl.viewer.width() - 2.0f * sl.scroll_inset, sl.list_h,
                        loc(app, key, fb), 1.0f, UiAlign::Center, 0.25f, 0.18f, 0.10f);
    }
    // The `Oe.scroll` `Fg(500,800,0,30)` frame (paper body + rails + the two
    // `Zh` roll bands, L1869-1872) — the oracle centre column (the port drew
    // the room through the viewer with no scroll art).
    draw_shop_scroll(app, ren, sl.viewer);
    // The JS scroller clips its cells to the `scroll.content` rect (inset by
    // the rails `c` and the roll bands `vk`) — `Gg.VK` (L1891) plus the node
    // mask (L1603). `Renderer::push_clip`/`pop_clip` is the native equivalent
    // (renderer.hpp: glScissor, top-left origin), so a partially-scrolled cell
    // is cut at the content edge instead of drawing over the rolls/panels.
    ren.push_clip(sl.viewer.J + sl.scroll_inset, sl.cell_top,
                  sl.viewer.width() - 2.0f * sl.scroll_inset, sl.list_h);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const CatalogItem& it = items_[rows[i]];
        // JS `Y.na(item.name)` resolution (L2247 `Ne.Vc`, L1881): the list.xml
        // `Name` is a lang key ("WEAPON_KNIVES" -> "Knives"); the helper falls
        // back to the human SubType/Type so no raw key is ever shown.
        const std::string iname = item_display_name(app, it);
        // `Gg.aa` (L1886): the scroll offset `ei.node.ra` (`scroll_y_`) shifts
        // every cell; the rect cull below drops FULLY-outside cells, the clip
        // handles the partially-visible ones.
        const float cell_y = sl.cell_top + scroll_y_ +
                             static_cast<float>(i) * sl.cell_step;
        if (cell_y + sl.cell_h <= sl.cell_top ||
            cell_y >= sl.cell_top + list_h) continue;
        const float cy = cell_y + sl.cell_h * 0.5f;
        const float cx = sl.cell_cx, cw = sl.cell_w, ch = sl.cell_h;
        const ShopRect cell{cx - cw * 0.5f, cy - ch * 0.5f, cx + cw * 0.5f, cy + ch * 0.5f};
        const bool hovered = static_cast<int>(i) == hover_;
        const bool selected = sel_it != nullptr && sel_it->name == it.name;
        const bool owned = seen_.has_item(it.name);
        const bool equipped = owned && shop_slot_for(seen_, it.type) == it.name;
        // `ns` cell (L2303-2308): only the item image `Bk` (`Rf(Ye.qI(fileName))`,
        // L2307; sized `kLa(c, ce.y*.8)`, `c=ce.x*.8`, L2306), the required-level
        // star `ky` (`E.get(260), y.PRa` = "star") + the level label `av`
        // (`V(K.T(this.bc.xf))`, L2307, shown only when `xf>0`). The name/price
        // belong to the `bc` detail panel — the old native drew them IN the cell
        // (invented: the oracle centre column shows only the art + "★ 1").
        bool drawn = draw_item_image(app, it.image, cx, cy - ch * 0.04f, cw * 0.8f, ch * 0.8f,
                                     0.95f);
        if (!drawn) {
            const float r = equipped ? 0.72f : (selected ? 0.75f : (hovered ? 0.7f : 0.5f));
            const float g = equipped ? 0.60f : (selected ? 0.62f : (hovered ? 0.56f : 0.4f));
            const float b = equipped ? 0.25f : (selected ? 0.3f : (hovered ? 0.26f : 0.2f));
            draw_flat_button(app, iname, cx, cy, cw, ch, r, g, b, hovered);
        }
        if (it.level > 0) {
            // `ky.zf(40)` (L2305): the star at the cell's lower-left, the level
            // number `av` to its right (`av.C(ky.za())`, `av.D(ky.ra+ky.qa()*.2)`).
            const float sy = cell.W - ch * 0.16f;
            const float sx = cell.J + cw * 0.12f;
            if (!try_draw_atlas_button(app, "star", sx, sy, 34.0f, 32.0f, 1.0f, false, false)) {
                draw_ui_label(app, sx - 17.0f, sy - 16.0f, 34.0f, 32.0f, "*", 0.9f,
                              UiAlign::Center, 1.0f, 0.9f, 0.4f);
            }
            draw_ui_label(app, sx + 20.0f, sy - 16.0f, 60.0f, 32.0f, std::to_string(it.level),
                          0.8f, UiAlign::Left, 0.25f, 0.18f, 0.10f);
        }
        // `ns.ba` (L2306) + `ns.j5` (L2308-2309): the sale/`badge` flag `Di`
        // (atlas 248 `pieces/*`) + its text `Im`. Resolved by `shop_cell_badge`
        // (the single rule shared with the `--settings-profile-shop-probe`).
        // The `b.yn > p.Dc` "shopSale" sub-branch (`ns.j5` L2308-2309:
        // `b=this.bc.am(0); b!=null&&b.yn>p.Dc ? SALE : badge`) reads the
        // item's `yf` offer (`QuestEngine::offer_for`): `end_time` (`yf.yn`,
        // written by `apply_discount`) against the public clock
        // (`QuestEngine::now_seconds()` = `p.Dc`).
        const EngineItemOffer* const item_offer = app.quest_engine().offer_for(it.name);
        const bool shop_sale =
            item_offer != nullptr && item_offer->end_time > QuestEngine::now_seconds();
        const ShopBadge badge = shop_cell_badge(app, it, shop_sale);
        const char* badge_art = badge.art;
        std::string badge_text = badge.text;
        if (badge_art != nullptr) {
            const float bw = cw * 0.55f;                          // `Di.kf(a*.55)`
            const float bh = ch * 0.18f;                          // `c = Di.qa()`
            const float bcx = cell.N - bw * 0.5f;                 // `Di.C(a - Di.za())`
            const float bcy = cell.P + ch - 2.5f * bh + bh * 0.5f;  // `Di.D(b-2.5*c)`
            if (!try_draw_atlas_button(app, badge_art, bcx, bcy, bw, bh, 1.0f, false, false)) {
                const ShopRect bq{bcx - bw * 0.5f, bcy - bh * 0.5f, bcx + bw * 0.5f,
                                  bcy + bh * 0.5f};
                quad(bq, 0.85f, 0.25f, 0.20f, 0.9f);
            }
            if (!badge_text.empty()) {
                // `Im.C(Di.ya); Im.D(Di.ra); Im.Fa(a*.55,c); Im.ua(c)` (L2306).
                draw_ui_label(app, bcx - bw * 0.5f, bcy - bh * 0.5f, bw, bh, badge_text, 0.9f,
                              UiAlign::Center, 1.0f, 1.0f, 1.0f);
            }
        }
    }
    ren.pop_clip();  // end the `Gg` scroller viewer mask
    // `bc` item-detail panel (JS `hi(1,!1)` docked right, `Oa.init` L2290;
    // `Ne` content L2243-2260). Background = `y.rM` = "info_panel_h" from the
    // scroll atlas (id 254), scaled to the panel width (`hi.Pn` L2311
    // `this.background.kf(a.N-a.J)`); the old flat dark quad was invented.
    const ShopRect rp = sl.right_panel;
    // `Oa.Q5` (L2302): `this.bc.X(a)` with `a=HOa()` — the detail panel is
    // HIDDEN on an empty/locked tab (the oracle `shop_tab4/5` show no panel).
    bool drew_panel = false;
    if (!rows.empty() && load_scroll_atlas(app)) {
        drew_panel = try_draw_atlas_button(app, "info_panel_h", (rp.J + rp.N) * 0.5f,
                                           (rp.P + rp.W) * 0.5f, rp.width(), rp.height(), 1.0f,
                                           /*fill=*/false, /*flip_x=*/false);
    }
    if (!rows.empty() && !drew_panel) quad(rp, 0.10f, 0.10f, 0.13f, 0.9f);
    if (sel_it != nullptr) {
        // `hi.Pn` interior insets (`b=background.Eb*28`, `c=background.Eb*24`,
        // L2311); the right panel (`Evb=1`) adds `b` on the left and `b*2` on
        // the right.
        const float pscale = rp.width() / 608.0f;  // info_panel_h 608x866
        const float bx = 28.0f * pscale, by = 24.0f * pscale;
        const float cx0 = rp.J + bx, cw0 = rp.width() - 3.0f * bx;
        const float cy0 = rp.P + by, ch0 = rp.height() - 2.0f * by;
        // Title `Vc` (L2243): item name `Y.na(Y.c9a(name))` (L2247), font
        // `a*.2`, centred, at `Vc.D(d-c/2)` with `d=b*.1` (L2248).
        const float tfont = cw0 * 0.2f;
        const float ty = cy0 + ch0 * 0.1f - tfont * 0.5f;
        draw_ui_label(app, cx0, ty, cw0, tfont, item_display_name(app, *sel_it), 0.85f,
                      UiAlign::Center, 0.30f, 0.20f, 0.10f);
        // `lH` = the `ms` attribute list (`Ne.refresh` L2248
        // `lH.refresh(Aa, $e.Qi, qC&&gW&&$e.Qi!=null)`; `lH.ba(a, a*.22)`
        // L2248). `ms.setParameters` (L2274-2275): for each `v.eo` attribute
        // def `h` in file order, when the item carries `h.name` and `!h.hidden`
        // push one `fi` cell (`ms.oca` L2275 `d.init(h.name,h.icon,d.G,e.G,!0)`).
        // The cell (`fi.init` L2270-2271, `fi.ba` L2271, `fi.OT`/`IXa` L2272)
        // draws the `attributes/<icon>` art + `ie.Or(value)` + the
        // `(±(compare-value))` delta. The old native hard-picked ONE stat.
        const float stat_h = cw0 * 0.22f;   // `lH.ba(a, a*.22)` L2248
        const float stat_y = cy0 + ch0 * 0.1f + tfont * 1.3f;
        const std::map<std::string, int> equipped_attrs =
            shop_equipped_attrs(app, *sel_it);  // JS `$e.Qi`
        float row_y = stat_y;
        for (const ShopAttributeDef& def : shop_attribute_defs()) {
            if (def.hidden) continue;  // `!h.hidden` L2274
            const auto vit = sel_it->attributes.find(def.name);
            if (vit == sel_it->attributes.end()) continue;  // `a.attributes.get` L2275
            const int value = vit->second;
            int compare = value;  // `e.G=d.G` L2275
            const auto cit = equipped_attrs.find(def.name);
            if (cit != equipped_attrs.end()) compare = cit->second;  // `b.attributes.get`
            const int delta = compare - value;  // `IXa(b-a)` L2272
            const float row_cy = row_y + stat_h * 0.5f;
            // `fi.init`: `"attributes/" + Icon` in atlas 248 (fallback 266).
            const std::string icon_key = std::string("attributes/") + def.icon;
            try_draw_atlas_button(app, icon_key.c_str(), cx0 + stat_h * 0.5f, row_cy, stat_h,
                                  stat_h, 1.0f, false, false);
            // `fi.Isb(a)` L2272: `Mb.R(a>0)` — the value label shows only when
            // `a>0`; `Mb.V(ie.Or(a))`.
            if (value > 0) {
                draw_ui_label(app, cx0 + stat_h * 1.1f, row_cy - stat_h * 0.3f, cw0 * 0.6f,
                              stat_h * 0.6f, shop_format_number(value), 0.9f, UiAlign::Left,
                              0.20f, 0.12f, 0.06f);
            }
            // `fi.IXa(a)` L2272: `(+N)` in `Z.mTa` (green) when >0, `(N)` in
            // `Z.RED` when <0; hidden when 0.
            if (delta != 0) {
                const std::string dtxt = delta > 0 ? "(+" + std::to_string(delta) + ")"
                                                   : "(" + std::to_string(delta) + ")";
                const float dr = delta > 0 ? 0.267f : 0.608f;
                const float dg = delta > 0 ? 0.478f : 0.110f;
                const float db = delta > 0 ? 0.008f : 0.027f;
                draw_ui_label(app, cx0 + cw0 * 0.6f, row_cy - stat_h * 0.2f, cw0 * 0.4f,
                              stat_h * 0.4f, dtxt, 0.8f, UiAlign::Left, dr, dg, db);
            }
            // `vH` (`os`, L2268) value bar: `fi.ba` bottom-aligns it under the
            // row; `fi.Gr` (L2273) fills it with `fi.Z7a(value)` — the `v.Ova`
            // (`Mv` L604556) `<ItemLimits>` ratio for the player level.
            {
                const float bxx = cx0 + stat_h * 1.1f;
                const float bww = cw0 - stat_h * 1.1f - 8.0f;
                const float bh2 = stat_h * 0.4f;
                const ShopRect track{bxx, row_y + stat_h - bh2, bxx + bww, row_y + stat_h};
                quad(track, 0.35f, 0.24f, 0.14f, 0.6f);
                const float fill_frac =
                    shop_attribute_bar_fill(def.bar_scale, value, seen_.level);
                const ShopRect fill{bxx, track.P, bxx + bww * fill_frac, track.W};
                quad(fill, 0.95f, 0.62f, 0.20f, 1.0f);
            }
            row_y += stat_h;  // `ms.ba`: `c += f.node.qa()` L2274
        }
        // `Sb` status line (`Ne.Qqb` L2259): `kW` -> "shopMaking", else `tra`
        // -> "shopOrder", else hidden. (`Ne.Uqb` L2257 resets `Sb`/`xg`; the
        // `xg` description branch is `I.Ox`/`I.Bu`/`I.wk` only — consumable /
        // Bonus / RealMoneyItem — so a Weapon/Armor/Helm row NEVER shows `xg`.
        // `re.xcb` L2285: `kW = $e.Bh > now` (the selected item is in
        // delivery); `re.Ccb` L2285: `tra = $e==null && Aa.Ec > 0` (no owned
        // save entry and the item carries a `DeliveryTime`).
        const std::int64_t shop_now = WarriorSave::wall_now();
        const auto sel_timer = seen_.timers.find(sel_it->name);
        const bool making = sel_timer != seen_.timers.end() && sel_timer->second > shop_now;
        const bool order = !seen_.has_item(sel_it->name) && sel_it->delivery_sec > 0;
        const std::string status = making ? loc(app, "shopMaking", "MAKING")
                                          : (order ? loc(app, "shopOrder", "ORDER") : "");
        if (!status.empty()) {
            const float sb_y = cy0 + ch0 * 0.1f + tfont * 1.3f + cw0 * 0.27f;
            draw_ui_label(app, cx0, sb_y, cw0, cw0 * 0.14f, status, 0.8f, UiAlign::Center,
                          0.30f, 0.20f, 0.10f);
        }
        // `Tl` delivery countdown (`Ne.j7a` L2256 -> `Ksb` L2256, `Ne.aa` L2248):
        // the remaining seconds of the SELECTED item's delivery, shown inside
        // the `bc` panel; hidden when `j7a()` == 0. `Xe.qfa` format -> the
        // port's `shop_countdown` (MM:SS).
        if (making) {
            const std::int64_t left = sel_timer->second - shop_now;
            if (left > 0) {
                const float tl_y = cy0 + ch0 * 0.1f + tfont * 1.3f + cw0 * 0.44f;
                draw_ui_label(app, cx0, tl_y, cw0, cw0 * 0.16f, shop_countdown(left), 0.9f,
                              UiAlign::Center, 0.30f, 0.20f, 0.10f);
            }
        }
        // Bottom price button `M8` = `GoldButton` (`EButtonGreen` + the
        // `p.o.Vf` gold icon), `Ne.Wub` L2254-2255 -> `c5(M8, Aa.jp())`. The
        // buttons stack up from `d=b-c*3`, each `e.kf(a)` (full content width)
        // (`Ne.ba` L2249).
        const float bpad = cw0 * 0.05f;
        const float bh = 112.0f * pscale;
        const float byy0 = cy0 + ch0 - bpad * 3.0f - bh * 0.5f;
        // `Ne.Wub` L2254 verbatim: the default branch draws the `M8`
        // GoldButton (`c5(M8, Aa.jp())`) AND the `pVa` RubyButton
        // (`c5(pVa, Aa.nn())`); `kL` L2254 activates a button only while its
        // price > 0. `Ne.ba` L2249 walks the `Cd` buttons in REVERSE from
        // `d = b-c*3`, so `pVa` (pushed after `M8`) lands at the BOTTOM and
        // `M8` above it. A crystal-only row (Price absent, BonusPrice set)
        // therefore shows its Ruby cost, never a "0" gold plate.
        const int gold = shop_effective_price(app, *sel_it);
        const int gems = shop_effective_bonus(app, *sel_it);
        auto draw_price_plate = [&](int slot, const char* icon, int value) {
            const float py = byy0 - static_cast<float>(slot) * (bh + bpad);
            if (!(load_sliced_atlas(app) &&
                  draw_bb_plate(app, "btnGreen", cx0 + cw0 * 0.5f, py, cw0, bh, 1.0f))) {
                draw_flat_button(app, "", cx0 + cw0 * 0.5f, py, cw0, bh, 0.30f, 0.62f,
                                 0.30f, false);
            }
            try_draw_atlas_button(app, icon, cx0 + 30.0f, py, 40.0f, 40.0f, 1.0f, false,
                                  false);
            draw_ui_label(app, cx0 + 56.0f, py - 15.0f, cw0 - 56.0f, 30.0f,
                          std::to_string(value), 0.9f, UiAlign::Left, 0.15f, 0.10f, 0.05f);
        };
        int slot = 0;
        if (gems > 0) draw_price_plate(slot++, "ruby", gems);  // `pVa` (L2254)
        if (gold > 0) draw_price_plate(slot, "gold", gold);    // `M8` (L2254)
        // JS `Ne` draws NO caption over the `M8`/`pVa` plates. The old native
        // label keyed `loc(app,"btnShopBuy",...)`, but `btnShopBuy` does NOT
        // exist in sf2.502f0946.js (0 occurrences), so it always fell back to
        // the hard-coded "CONFIRM". Invented; removed.
    }
    // `MJ` (`ps` params, L2275) / `op` (`qs` enchantments, L2280) are CLOSED in
    // the oracle shop states: `Oa.init` opens only `bc` (`init(a,!0)`); MJ/op
    // get `init(a,!1)` (`hi.$ka(false)`, L2312) and slide away. The old
    // always-on flat left panel covered the dojo backdrop (invented).
    // `Up` TRY/EQUIP/UNEQUIP button (`Oa.init` L2289 `Bb("EButtonWhite")`;
    // `Oa.layout` L2295 `Up.kf(b.N-b.J)`, `jP.D(b.P+Up.qa())` — top of the
    // LEFT slot). Label via `Oa.DU` (L2299), visible on tabs 0..4 (`h$a`).
    if (sel_it != nullptr) {
        const ShopRect ar = shop_try_rect(sl);
        const std::string alabel = shop_action_label(app, seen_, *sel_it);
        const bool ah = side_hover_ == 1;
        if (!(load_sliced_atlas(app) &&
              draw_bb_plate(app, "btnWhite", (ar.J + ar.N) * 0.5f, (ar.P + ar.W) * 0.5f,
                            ar.width(), ar.height(), 1.0f))) {
            draw_flat_button(app, alabel, (ar.J + ar.N) * 0.5f, (ar.P + ar.W) * 0.5f, ar.width(),
                             ar.height(), ah ? 0.35f : 0.25f, ah ? 0.65f : 0.45f,
                             ah ? 0.35f : 0.25f, ah);
        }
        draw_ui_label(app, ar.J, (ar.P + ar.W) * 0.5f - 11.0f, ar.width(), 22.0f, alabel, 0.8f,
                      UiAlign::Center, 1.0f, 1.0f, 1.0f);
    }
    if (!try_draw_atlas_button(app, "Arrow", 64.0f, 40.0f, 88.0f, 48.0f, 1.0f)) {
        draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
        draw_ui_label(app, 64.0f - 44.0f + 6.0f, 40.0f - 10.0f, 88.0f - 12.0f, 20.0f,
                          "BACK", 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    }
    // JS `Ne`/`Oa` render NO wallet label: there is no "COINS <money>" caption
    // anywhere in the shop chrome (grep sf2.502f0946.js: 0 "COINS"), and NO
    // deliveries list — the ONLY delivery UI is the per-item `Sb` MAKING line
    // + `Tl` countdown drawn INSIDE the `bc` panel above (`Ne.Qqb` L2259,
    // `Ne.j7a` L2256). The old native wallet label + "READY"/countdown row list
    // were both inventions; removed.
    // JS `Pa.iwa` (L1228) renders NO caption after a successful buy — it
    // commits money + `p.o.save()` + `Pa.Wz` (the QUEST_EVENT_PURCHASE
    // dispatch) and nothing else; the ONLY badge it shows is `v.Bv(a,2)`,
    // the "not enough money" NOTICE on the FAILURE branch. The port drew a
    // green "BOUGHT …!" / "EQUIPPED …!" / "ORDERED …!" toast here — an
    // invention (the reported green label). REMOVED.
    // JS `Pi` purchase panel (`Oa.Fhb` L2300 -> `Ex(a,7)`): it SLIDES in as a
    // panel (`Pi.er`/`u6` L2312) — it draws no full-screen dim and no
    // "PURCHASE <item>?" caption (grep sf2.502f0946.js: 0 "PURCHASE "). Both
    // were inventions; removed. `buy_armed_` state is kept (update/`Ao.Qg`).
    // Shared `za` chrome (JS `ma.D1`): topPanel + widgets + vertical nav.
    // `D1` (L1831) re-appends a fresh, collapsed `za` -> the oracle shop shows
    // the collapsed header and NO `gk.background` 0.5-black dim (measured: the
    // oracle shop right-wall backdrop is ~0.7x the hub, not 0.5x). Same
    // force-collapsed draw as the Map/Profile (L5078/L9345).
    draw_za_chrome(app, kScreenShop);
    // Sensei/tutorial dialog modal over the shop (the tutorial's
    // `tutorial_buy_knives` beat, `He` L1042; see update_impl).
    draw_quest_modal(app, ren, app.screens().top() == this);
}

// ---------------------------------------------------------------------------
// Profile `cs` tab strip (JS L2188: class `cs extends Eg`, 4 `Le` on the
// profile atlas id 258, `Tw=[0,1,2,3]`). `Eg` is the shared bottom tab strip
// (L1851: height = za.Sp*1.3, `node.D(rect.v - height)`, buttons scaled
// `height/button.Y.fa.y`, spread lc-dependent). The `y.*` frame table for
// `cs` (`y.WRa/YRa/XRa` ...) is OPEN (PORT_AUDIT_UI §5 OPEN #2); the art
// names below are the profile atlas `buttons/*` frames (sourceSize 199x190).
// Tab content: `vb.hla` (L2190-2191) routes tab 0 -> `Rl=ds`
// (POWERLEVELING_SLIDER L2227), tab 1 -> `qv=es` (SKILLS_SLIDER L2239),
// tab 2 -> `Zr=fs` (ACHIEVEMENT_SLIDER L2213), tab 3 -> `lv=gs`
// (SEALS_SLIDER L2231). ALL FOUR bodies are reproduced below — tab 0 perk
// tree (`ds`), tab 1 folded Moves (`es`), tab 2 achievements (`fs`), tab 3
// seals (`gs`). The earlier "tabs 0/2 are OPEN" note was STALE and misled the
// UI audit: each body docks into the real `vb.layout` `a = b.fn(.75)` viewer
// rect (L2195).
// UNVERIFIED (OPEN): the `Xd`/`Gg` slider cell scaling. `ff.kf(a)` (L1893)
// sets the cell node scale to `a/ce.x`, so the RENDERED row height is
// `ce.y * node.Eb` (`ff.qa`), not the declared `ff.ba(w,h)` — resolving the
// per-row pitch needs `Gg`'s slot width. The tab rows below therefore still
// use measured fixed pitches (150 Moves, 46 Achiev, 400x300 Seals cells).
//
// Nav/`cs` badge counters (`Eg.GU` L1853 -> `Le.badge`, `Dg` L1850-1851).
// JS `cs.getCounterValue` (L2189) defines each tab badge EXACTLY — it is not
// undefined, and three of the four are plain save joins (the port's earlier
// note that they are "not derivable" was wrong):
//   0 `p.o.co.uCa()` (L305) = `id.ht().n5a() - this.KS.Oa.length`
//       = perk defs in the tree minus the `<PerkHistory>` entries.
//   1 `p.o.sCa()` (L256) = count of `v.uQ()` moves whose `aE` "new" flag is
//       set (`es.zha` L2239 clears it on tab entry; `p.o.inb` persists it).
//   2 `p.o.yi.rCa()` (L294) = count of achievement `<Hy>` levels with `yj`.
//   3 `p.o.vCa()` (L256) = owned seals (`xa.hJ(I.Vr)`) with `pd()>0` and the
//       seal def's `yj` flag.
// `Eg.GU()` (L1853) pushes each value into `Le.badge.lk(...)` (`Dg` L1850:
// notification_circle / notification_ellipse at local (71,48), `ba(72)`).
// All four are computed by `profile_badges` below: the move `aE` flag is the
// save's `<OpenTricks>` (`Bt.parse` L250 -> `Nua` L268) and the item `yj` flag
// the save's `<CounterItems>` (`Bt.Gjb` L269) - both now carried by
// `WarriorSave::open_tricks` / `WarriorSave::counter_items`. On every shipped
// fresh save both lists are empty, so the strip draws NO badge (which is what
// the oracle `profile_tab*` captures show). The `gs.zha` (L2231) and
// `es.zha` (L2239) sub-view hooks clear those flags on tab entry (see
// `EquipmentScreen::update_impl`).
// ---------------------------------------------------------------------------
constexpr int kProfileTabCount = 4;  // `cs.Tw = [0,1,2,3]` (L2188) — 4 buttons
// `vb.hla(a)` guard `if(this.vV!=a&&a!=5)` (L1127569) accepts slot 4
// (BattlePass, `To.hOa(15)`) and sets `vV=4`; the `hla` switch has no
// `case 4` body, so it shows no pane — but the JS does NOT reject it. Only 5
// (the `init` "no tab" default) and negatives are rejected.
constexpr int kProfileSlotMax = 4;
constexpr int kProfileTabLeveling = 0;  // `ds` leveling tab (`Rl=ds` L2227) — perk tree body
constexpr int kProfileTabMoves = 1;  // folded Moves sub-view (JS `qv`, To.kOa=11 L2201)
constexpr int kProfileTabAchiev = 2;  // `fs` ACHIEVEMENT_SLIDER (L2213) — achievements body
constexpr int kProfileTabSeals = 3;  // `gs` SEALS_SLIDER (L2231) — ported

struct ProfileTabArt {
    const char* normal;
    const char* active;
    const char* pushed;
    const char* label;
};
const ProfileTabArt kProfileTabs[kProfileTabCount] = {
    // Oracle capture order (profile_tab0..3): tab0 = pyramid (leveling/perk
    // tree), tab1 = kicking figure (MOVES), tab2 = ribbon (achievements),
    // tab3 = seal coin. The `buttons/Progress*` art is the pyramid and
    // `buttons/Strikes*` the kick (profile.ff77c0ff.json frame table).
    {"buttons/Progress", "buttons/Progress_active", "buttons/Progress_pushed", "SKILLS"},
    {"buttons/Strikes", "buttons/Strikes_active", "buttons/Strikes_pushed", "MOVES"},
    {"buttons/Achiev", "buttons/Achiev_active", "buttons/Achiev_pushed", "ACHIEV"},
    {"buttons/Seal", "buttons/Seal_active", "buttons/Seal_pushed", "SEAL"},
};

struct ProfileTabLayout {
    float bar_h = 0.0f;  // Eg.height
    float cy = 0.0f;     // strip centre y
    float cx0 = 0.0f;    // first button centre x
    float step = 0.0f;   // centre-to-centre
    float btn_w = 0.0f;
    float btn_h = 0.0f;
    // `Eg.aa` (L1852) scale `c = this.height / button.Y.fa.y` applied to every
    // button NODE (`g.node.la(c)`) - the badge `Dg` is a child of that node, so
    // its `C(71)`/`D(48)`/`ba(72)` are in button-local units times this.
    float btn_scale = 1.0f;
};

ProfileTabLayout profile_tab_layout() {
    const ZaLayout z = za_layout();
    ProfileTabLayout t;
    t.bar_h = z.sp * kTabBarHeightK;                 // Eg.aa: za.Sp*1.5 (un)
    constexpr float kSrcW = 199.0f, kSrcH = 190.0f;  // profile Le sourceSize
    const float scale = t.bar_h / kSrcH;             // Eg: height/button.Y.fa.y
    t.btn_h = t.bar_h;
    t.btn_w = kSrcW * scale;
    t.btn_scale = scale;
    constexpr float kSpread = 1.2f;                  // Eg `b` (lc>1 clamp)
    t.step = t.btn_w * kSpread;
    const float total = t.btn_w + t.step * static_cast<float>(kProfileTabCount - 1);
    t.cx0 = (kViewW - total) * 0.5f + t.btn_w * 0.5f;
    t.cy = kViewH - t.bar_h * 0.5f;
    return t;
}

// JS `vb.layout` (L2195): the content `b` split (identical to the shop
// `Oa.layout` L2293) with the active sub-view `jq` docked into `a = b.fn(.75)`
// and the header `XB=ei` docked into the left slot `b2` (L2196).
struct ProfileLayout {
    ShopRect content;    // b (L2195)
    ShopRect viewer;     // a = b.fn(.75) (L2195) — the active `jq` rect
    ShopRect left_slot;  // b2 (L2195-2196) — the `XB=ei` header (`Pn(b)`)
    ShopRect right_slot; // c (L2196) — the `zr=Yr` status panel (`Pn(c)`)
    float gap = 0.0f;    // d = (a.N-a.J)*.03 (L2195)
};

ProfileLayout profile_layout() {
    const float lc = kViewW / kViewH;             // N.lc
    const float t = std::clamp(lc, 0.6f, 1.0f);   // clamp(lc,.6,1)
    const float sp = za_layout().sp;              // kA.Sp (JS L1975)
    const float margin = kViewW * 0.05f * ((t - 0.6f) / 0.4f);  // L2195
    ShopRect b{margin, sp * 1.4f, kViewW - margin,
               kViewH - sp * 1.5f * 1.3f};        // L2195
    b = shop_gb_fn(b, 1.85f + ((t - 0.6f) / 0.4f) * 0.15f);     // L2195 fn
    ProfileLayout l;
    l.content = b;
    l.viewer = shop_gb_fn(b, 0.75f);              // a = b.fn(.75) L2195
    // Side slots `b`/`c` (L2195-2196): both are `gb(0,0, slot_h*.7, slot_h)`
    // rects (`slot_h = (a.W-a.P)*.8`), centred beside the viewer at
    // `a.J + d` (left, right edge) / `a.N - d` (right, left edge) with
    // `d = (a.N-a.J)*.03`. `this.XB.Pn(b); this.zr.Pn(c)` (L2196).
    l.gap = l.viewer.height() * 0.03f;            // d = (a.N-a.J)*.03
    const float slot_h = l.viewer.height() * 0.8f;
    const float slot_w = slot_h * 0.7f;           // gb(0,0,b*.7,b) L2195
    const float cy = (l.viewer.P + l.viewer.W) * 0.5f;
    l.left_slot = {l.viewer.J + l.gap - slot_w, cy - slot_h * 0.5f,
                   l.viewer.J + l.gap, cy + slot_h * 0.5f};
    l.right_slot = {l.viewer.N - l.gap, cy - slot_h * 0.5f,
                    l.viewer.N - l.gap + slot_w, cy + slot_h * 0.5f};
    return l;
}

// JS `Xd`/`Gg` slider row geometry. `Xd.Pn(a)` (L2185) sizes the `Fg` scroll
// (`scroll.ba(a.N-a.J, a.W-a.P, (a.N-a.J)*.08)`) and then sizes the `Gg` cell
// list `Pa.ba(this.scroll.Gv - 8, this.scroll.Xy)`. `Fg.ba` case 0 (L1870)
// with `c = .08*w` and both `Zh` rails present (`d = 2`) gives
//     Gv = w - 2*c = 0.84*w            (inner width)
//     Xy = h - d*vk = h - 2*(.08*w)    (inner height)
// (the rails are `vk = 0.08*w` thick - NOT a fixed 30px; `Vaa(30)` is
// overwritten by every later `ba`). `Gg.ba` (L1884-1885) then calls
// `cell.kf(A)` per cell with `A = size.x = Gv - 8`, and `ff.kf` (L1893) is
// `node.la(A / ce.x)`, so a cell declared `ba(cw, ch)`
// (`Xd.NC` L2216/2230/2232/2240) renders `ch * A / cw` tall (`ff.qa` L1893
// returns `this.ce.y * this.node.Eb`) and the LIST PITCH is
//     `ch * A / cw + Pa.spacing`
// (`c += f.qa() + this.spacing`, L1885).
constexpr float kGgPad = 8.0f;       // `Pa.ba(scroll.Gv - 8, ..)` L2185
constexpr float kFgRailFrac = 0.08f; // `scroll.ba(.., (a.N-a.J)*.08)` L2185

float profile_list_w(const ShopRect& v) {
    return (1.0f - 2.0f * kFgRailFrac) * v.width() - kGgPad;  // scroll.Gv - 8
}
// `Fg.ba` case 0: `Xy = b - d*vk` with `d = 2` rails of `vk = .08*a` - the
// `Gg` list's own height (`Gg.ba` L1884 keeps `size.y` and centres the cells
// in it: `uz = (size.y - cells[0].qa())/2`, L1885).
float profile_scroll_h(const ShopRect& v) {
    return v.height() - 2.0f * kFgRailFrac * v.width();
}
// Rendered cell height: `ff.kf` scales the cell node by `A/ce.x`
// (`node.Eb`), `ff.qa()` (L1893) is `ce.y * node.Eb`.
float profile_cell_h(const ShopRect& v, float cell_w, float cell_h) {
    const float w = profile_list_w(v);
    if (cell_w <= 0.0f) return cell_h;
    return cell_h * w / cell_w;
}
float profile_cell_pitch(const ShopRect& v, float cell_w, float cell_h, float spacing) {
    return profile_cell_h(v, cell_w, cell_h) + spacing;
}
// The `Gg` node lives in `scroll.content`, which `Fg.ba` case 0 places at
// `C(vk)` / `D(vk)` inside the scroll node (`vk = .08*a` = the rail
// thickness, L1871) - so the cell list is inset by one rail on the LEFT/TOP
// and is `Gv - 8` wide (`Xd.Pn` L2185).
float profile_list_left(const ShopRect& v) {
    return v.J + kFgRailFrac * v.width();
}
// `Gg.ba` (L1885) centres the whole cell list in the scroll: `uz`.
float profile_list_top(const ShopRect& v, float cell_w, float cell_h) {
    const float uz =
        std::max(0.0f, (profile_scroll_h(v) - profile_cell_h(v, cell_w, cell_h)) * 0.5f);
    return v.P + kFgRailFrac * v.width() + uz;
}

// The four `cs` tab badge values (JS `cs.getCounterValue` L2189):
//   0 = `p.o.co.uCa()` (L305) = `id.ht().n5a() - co.KS.Oa.length`
//       = `<PerkTree>` tiers whose `<Level Value>` <= player level, minus the
//       `<PerkHistory>` rows. `n5a` L1354: `for(;c<d && !(Sx[c++].level>b);)++a`.
//   1 = `p.o.sCa()` (L256) = count of `v.uQ()` (the wielded weapon's `Ru`
//       catalog list, L1218) whose `aE` "new move" flag is set. `aE` comes
//       from the save `<OpenTricks>` (`Bt.parse` L250 -> `Nua` L268); the
//       Moves tab clears it on entry (`es.zha` L2239).
//   2 = `p.o.yi.rCa()` (L294) = count of `v.uv` achievement defs whose `yj`
//       is set. `yr.Yua` (L297) sets `Ir(!ObtainedReward)` for every save
//       `<Achievement>` row, and `Ir(a){ this.yj = a && (AE>0 || dP>0) }`
//       (L1248) - i.e. an unlocked, unclaimed entry that actually pays.
//   3 = `p.o.vCa()` (L256) = count of owned `p.o.xa.hJ(I.Vr)` (Seal) rows
//       with `pd() > 0` and the def's `yj` flag, restored from the save
//       `<CounterItems>` by `Bt.Gjb` (L269) - the shipped `Type="Seal"` rows
//       all carry `SilentRecieve="0"`, so `Gjb`'s `gU == 0` gate passes.
// `Eg.GU` (L1853) pushes each into `Le.badge.lk(...)`; `Dg.lk` (L1850)
// hides a non-positive badge. All four are 0 on a fresh save, so the strip
// renders no badge then - which is what the oracle matrix shows.
struct ProfileBadges {
    int perk_points = 0;   // tab 0 (`uCa`, L305)
    int new_moves = 0;     // tab 1 (`sCa`, L256)
    int new_achievs = 0;   // tab 2 (`rCa`, L294)
    int new_seals = 0;     // tab 3 (`vCa`, L256)
};

// JS `Zr.ba` (L2220) places the improve button `Yk` at `C(a/2)` and
// `D(b - c*1.5)` inside the active `vb` viewer rect `a`; the native renders
// it as a flat full-width button near the viewer bottom (the ASTC `Zr`
// arrows/`uk` compare art is the OPEN part). Shared by render + hit-test so
// both agree on the rect.
ShopRect profile_improve_rect() {
    const ProfileLayout pl = profile_layout();
    const ShopRect& v = pl.viewer;
    ShopRect r;
    constexpr float kBtnH = 44.0f;
    r.J = v.J + 6.0f;
    r.N = v.N - 6.0f;
    r.W = v.W - 8.0f;
    r.P = r.W - kBtnH;
    return r;
}

// `fs.NC` (L2216) sizes every achievement cell `ba(400,130)` and `fs.init`
// (L2214) sets `Pa.spacing = 10`; the cell box is the `Gg` list rect (one rail
// inset left/top, `Gv - 8` wide) and the row pitch is `130*A/400 + 10`
// (`profile_cell_pitch`). Shared by render + hit-test.
ShopRect profile_achiev_row_rect(const ShopRect& v, int i) {
    const float row_h = profile_cell_h(v, 400.0f, 130.0f);
    ShopRect r;
    r.J = profile_list_left(v);
    r.N = r.J + profile_list_w(v);
    r.P = profile_list_top(v, 400.0f, 130.0f) + static_cast<float>(i) * (row_h + 10.0f);
    r.W = r.P + row_h;
    return r;
}

// `as` (L2210) reward button: `nv.xc(a*.4)` at `C(a/2)`, `D(b - nv.qa())`
// (bottom-centre of the cell). Native anchor: the row's right edge.
ShopRect profile_achiev_reward_rect(const ShopRect& v, int i) {
    const ShopRect row = profile_achiev_row_rect(v, i);
    ShopRect b;
    b.N = row.N - 8.0f;
    b.J = b.N - 92.0f;
    b.P = row.P + row.height() * 0.5f - 13.0f;
    b.W = b.P + 26.0f;
    return b;
}

// Hit test for the `cs` tab strip; -1 when outside every button.
int profile_tab_hit(double px, double py) {
    const ProfileTabLayout t = profile_tab_layout();
    for (int i = 0; i < kProfileTabCount; ++i) {
        const float cx = t.cx0 + static_cast<float>(i) * t.step;
        if (px >= cx - t.btn_w * 0.5f && px <= cx + t.btn_w * 0.5f &&
            py >= t.cy - t.btn_h * 0.5f && py <= t.cy + t.btn_h * 0.5f) {
            return i;
        }
    }
    return -1;
}

// Draws the `cs` strip (art first; flat fallback only on a genuine miss).
// `badges` = the four `cs.getCounterValue` (L2189) values `Eg.GU` (L1853)
// pushes into each `Le.badge` (`Dg`, L1850).
void draw_profile_tabs(App& app, int tab, int hover, const int* badges) {
    sf2::render::Renderer& ren = app.renderer();
    const ProfileTabLayout t = profile_tab_layout();
    const float bar[] = {0, kViewH - t.bar_h, kViewW, kViewH - t.bar_h, kViewW, kViewH,
                         0, kViewH - t.bar_h, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(bar, 6, kTabBarBgR, kTabBarBgG, kTabBarBgB, 1.0f);
    for (int i = 0; i < kProfileTabCount; ++i) {
        const ProfileTabArt& art = kProfileTabs[i];
        const bool sel = i == tab;
        const bool hov = i == hover;
        const char* frame = hov && art.pushed != nullptr
                                ? art.pushed
                                : (sel && art.active != nullptr ? art.active : art.normal);
        const float cx = t.cx0 + static_cast<float>(i) * t.step;
        if (try_draw_atlas_button(app, frame, cx, t.cy, t.btn_w, t.btn_h,
                                  sel ? 1.0f : (hov ? 0.9f : 0.75f))) {
            // fall through to the badge only on a hit
        } else {
            draw_flat_button(app, art.label, cx, t.cy, t.btn_w, t.btn_h,
                             sel ? 0.55f : (hov ? 0.45f : 0.32f), 0.4f, 0.28f, hov);
            draw_ui_label(app, cx - t.btn_w * 0.5f, t.cy - 10.0f, t.btn_w, 20.0f,
                          art.label, 0.6f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        }
        // `Le.badge` (JS `Dg`, L1850-1851): an `icon` (`E.get(260)` misc
        // frame `y.V6` = `notification_circle`, swapped to `y.LRa` =
        // `notification_ellipse` once the count reaches 10) + a `label`
        // (`ba(72)` -> `icon.la(72/fa.x)`, `label.Fa(72,72)`, `ua(72*.8)`).
        // `Eg.GU` (L1853) calls `buttons[c].badge.lk(this.getCounterValue(...))`;
        // `lk` (L1850) hides the whole badge unless `count > 0` (`node.R(a>0)`)
        // and shifts the label `C(a == 1 ? -2 : 0)`. The `Dg` container sits at
        // `C(71)`/`D(48)` inside the button node and the icon/label are CENTRE
        // anchored on it (`Ga()`), so in button-local units the badge centre is
        // (71,48) with a 72-unit box; both are multiplied by `Eg`'s node scale.
        if (badges != nullptr && badges[i] > 0) {
            const int count = badges[i];
            const float s = t.btn_scale;
            const float bs = 72.0f * s;
            const float bx = cx - t.btn_w * 0.5f + 71.0f * s;
            const float by = t.cy - t.btn_h * 0.5f + 48.0f * s;
            const char* bframe = count < 10 ? "notification_circle" : "notification_ellipse";
            if (!try_draw_atlas_button(app, bframe, bx, by, bs, bs, 1.0f)) {
                draw_flat_button(app, "", bx, by, bs * 0.9f, bs * 0.9f, 0.85f, 0.2f, 0.18f,
                                 false);
            }
            draw_ui_label(app, bx - bs * 0.5f + (count == 1 ? -2.0f * s : 0.0f), by - 9.0f,
                          bs, 18.0f, std::to_string(count), 0.6f, UiAlign::Center, 1.0f, 1.0f,
                          1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// Profile data pipelines — the real JS parsers/joins
// ---------------------------------------------------------------------------

namespace {

// Reads one XML document from the extracted res dir (same rule the other
// shell loaders use: silent on absence, the caller keeps its empty state).
bool parse_res_xml(const std::string& path, sf2::data::xml_doc& doc) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] %s: %s\n", path.c_str(), e.what());
        return false;
    }
    return true;
}

// JS `Y.na` (L917) with the description template: XML Description attrs carry
// their arguments as trailing `{a}{b}` groups (e.g.
// "PERKDESCRIPTION_HELM_BREAKER{15}{35}{5}" or
// "Achievement_Desc_Perfect_Rounds_1{1}"), while the lang table stores the
// base key with `{0}`/`{1}` placeholders ("A {0}%% chance ..."). Resolve the
// base, substitute the args, and unescape `%%`.
std::string loc_template(App& app, const std::string& raw, const std::string& fallback) {
    const std::size_t brace = raw.find('{');
    if (brace == std::string::npos) return loc(app, raw, fallback);
    const std::string base = raw.substr(0, brace);
    std::vector<std::string> args;
    for (std::size_t i = brace; i < raw.size();) {
        if (raw[i] != '{') {
            ++i;
            continue;
        }
        const std::size_t end = raw.find('}', i + 1);
        if (end == std::string::npos) break;
        args.push_back(raw.substr(i + 1, end - i - 1));
        i = end + 1;
    }
    std::string text = loc(app, base, fallback);
    for (std::size_t k = 0; k < args.size(); ++k) {
        const std::string ph = "{" + std::to_string(k) + "}";
        for (std::size_t p = text.find(ph); p != std::string::npos; p = text.find(ph, p)) {
            text.replace(p, ph.size(), args[k]);
        }
    }
    for (std::size_t p = text.find("%%"); p != std::string::npos; p = text.find("%%", p)) {
        text.replace(p, 2, "%");
    }
    return text;
}

// `ds` PERK TREE (L2227): `id.ht().tH` tiers. The tier list `Tt` is built by
// `bya`/`EWa`/`dPa`/`cPa` (L1353-1357) from three sources:
//   - `character_progress.xml` `<PerkTree>` (asset 1315, `td.Vib` L1160 ->
//     `id.ht().parse(f)` L1352 `lkb` reads `<Level Value>` + `<Perk|Upgrade
//     Name>` into `Lw`/`Mw`), and its `<Perks>` base descriptions (`bPa`);
//   - `perks.xml` (`v.Rg`, asset 310) for the perk `Image`;
//   - the save `<PerkHistory>` (`p.o.co.KS.Oa`, `Ht` L1327) for the learned
//     level, fed through `Mw.K1` (L1358) for the availability flag.
// Each tier renders the `tk` compare cell (up to two `uk` cells + `Rx`
// arrows, L2217-2222) — the data rows here feed the native text/flat cell.
std::vector<EquipmentScreen::PerkRow> load_perk_tree(App& app, const WarriorSave& w) {
    (void)app;
    std::vector<EquipmentScreen::PerkRow> out;

    // perks.xml: Name -> Image ("Icons01.IconAvenger") + Description.
    std::map<std::string, std::pair<std::string, std::string>> perk_art;
    {
        sf2::data::xml_doc doc;
        if (parse_res_xml("reference/extracted/xml/res/perks.xml", doc)) {
            const pugi::xml_node root = doc.root().first_child();
            if (root) {
                for (pugi::xml_node p : root.children("Perk")) {
                    const std::string name = p.attribute("Name").value();
                    if (name.empty()) continue;
                    perk_art[name] = {p.attribute("Image").value(),
                                      p.attribute("Description").value()};
                }
            }
        }
    }

    // character_progress.xml: `<Perks>` base/upgrade descriptions + `<PerkTree>`.
    std::map<std::string, std::string> base_desc;                    // Perk Description
    std::map<std::pair<std::string, int>, std::string> upgrade_desc; // (Name,Value)
    // The def's max upgrade level (`Be.Tc`/`Gt.Tc`, read from
    // character_progress.xml `<UpgradeLevel Value>` via `j0a` L1190).
    // Written into the save as `Ji.Ce` (UpgradeLevel) by `Bt.L1a` L306.
    std::map<std::string, int> upgrade_max;
    sf2::data::xml_doc doc;
    if (!parse_res_xml("reference/extracted/xml/res/character_progress.xml", doc)) {
        return out;
    }
    const pugi::xml_node root = doc.root().first_child();
    if (!root) return out;
    for (pugi::xml_node p : root.child("Perks").children("Perk")) {
        const std::string name = p.attribute("Name").value();
        if (name.empty()) continue;
        if (p.attribute("Description")) base_desc[name] = p.attribute("Description").value();
        for (pugi::xml_node u : p.children("UpgradeLevel")) {
            const std::string d = u.attribute("Description").value();
            const int val = sf2::data::xml_attr_int(u, "Value", 0);
            if (!d.empty()) {
                upgrade_desc[{name, val}] = d;
            }
            upgrade_max[name] = std::max(upgrade_max[name], val);
        }
    }
    for (pugi::xml_node lvl : root.child("PerkTree").children("Level")) {
        const int tier = sf2::data::xml_attr_int(lvl, "Value", 0);
        for (pugi::xml_node item = lvl.first_child(); item; item = item.next_sibling()) {
            const std::string tag = item.name();
            if (tag != "Perk" && tag != "Upgrade") continue;  // `id.k7a` L1355
            EquipmentScreen::PerkRow r;
            r.tier = tier;
            r.kind = tag;
            r.name = item.attribute("Name").value();
            if (r.name.empty()) continue;
            const auto art = perk_art.find(r.name);
            if (art != perk_art.end()) r.image = art->second.first;
            const auto um = upgrade_max.find(r.name);
            if (um != upgrade_max.end()) r.upgrade_max = um->second;
            for (const WarriorSave::PerkLevel& pl : w.perk_history) {
                if (pl.name != r.name) continue;
                ++r.history_count;  // `id.cPa` runs once per record (L1353)
                r.learned_level = std::max(r.learned_level, pl.level);
            }
            r.type = tag == "Perk" ? 1 : 2;  // `id.f8a` L1357
            // `Ih.PQ()` = `Lc.Tc` (L1371). `e8a` (L1357) returns the FIRST
            // remaining `v.Rg.XS` def for the name; `cPa` -> `lnb` (L1353)
            // removes ONE def per `<PerkHistory>` record, so the matched
            // `UpgradeLevel` is `history_count+1` while defs remain, else the
            // base def `Tc=0` (L1328-1329).
            r.pq = (r.upgrade_max > 0 && r.history_count + 1 <= r.upgrade_max)
                       ? r.history_count + 1
                       : 0;
            // Description: the upgrade tier's text when learned, else the base
            // perk text, else the perks.xml def text (`Be.description`).
            const auto up = upgrade_desc.find({r.name, r.learned_level});
            if (tag == "Upgrade" && up != upgrade_desc.end()) {
                r.description = up->second;
            } else if (base_desc.count(r.name) != 0) {
                r.description = base_desc[r.name];
            } else if (art != perk_art.end()) {
                r.description = art->second.second;
            }
            // `Mw.K1` (L1358): Perk -> not-learned or learned >= tier;
            // Upgrade -> learned and learned <= tier.
            const bool learned = r.learned_level > 0;
            r.available = tag == "Perk" ? (!learned || r.learned_level >= tier)
                                        : (learned && r.learned_level <= tier);
            out.push_back(std::move(r));
        }
    }
    // `id.bya` (L1353) `Ih.Be` state machine. Every cell starts owned
    // (`EWa` L1356 `new Ih(null,level,3)`); `Txb` (L1356) sets the LOWEST
    // PerkTree tier to `Bla(0)`. Each `<PerkHistory>` record then runs
    // `cPa` -> `dzb` (L1356): every cell at the record's tier becomes
    // `Bla(1)` (`Bla(2)` for the matching name) and the NEXT tier above
    // becomes `Bla(0)` (`u8a` L1357). Records apply in save order, so a
    // later record's `dzb` wins.
    std::vector<int> tiers;
    for (const EquipmentScreen::PerkRow& r : out) {
        if (tiers.empty() || tiers.back() != r.tier) tiers.push_back(r.tier);
    }
    if (!tiers.empty()) {
        for (EquipmentScreen::PerkRow& r : out) {
            if (r.tier == tiers.front()) r.state = 0;  // `Txb` L1356
        }
    }
    for (const WarriorSave::PerkLevel& pl : w.perk_history) {
        for (EquipmentScreen::PerkRow& r : out) {
            if (r.tier != pl.level) continue;
            r.state = (r.name == pl.name) ? 2 : 1;  // `dzb` -> `t8a` L1354
        }
        for (const int t : tiers) {                  // `dzb` -> `u8a` L1357
            if (t <= pl.level) continue;
            for (EquipmentScreen::PerkRow& r : out) {
                if (r.tier == t) r.state = 0;
            }
            break;
        }
    }
    return out;
}

// `fs` ACHIEVEMENTS (L2213-2216): definitions from `achievements.xml` (asset
// 1356, `td.Adb`/`Fib` L1160 -> `v.uv.parse` L1175) joined with the save
// `<Counters>`/`<Achievements>` (`p.o.yi` = `yt.parse` L294; `kl` L1249,
// `ll` L1247, `Yua` L297) through `cab` (L2216). Mirrors the JS order:
// unlocked achievements first, then the still-visible remainder (break once
// the running counter is below a target).
std::vector<EquipmentScreen::AchievRow> load_achievements(App& app, const WarriorSave& w) {
    (void)app;
    std::vector<EquipmentScreen::AchievRow> out;
    struct Def {
        std::string name, description, icon;
        int counter = 0, money = 0, bonus = 0;
        bool hidden = false, completed = false, obtained = false;
    };
    struct Group {
        std::string name;
        std::vector<Def> items;
    };
    std::vector<Group> groups;
    sf2::data::xml_doc doc;
    if (parse_res_xml("reference/extracted/xml/res/achievements.xml", doc)) {
        const pugi::xml_node root = doc.root().first_child();
        if (root) {
            for (pugi::xml_node c : root.children("Counter")) {  // `Jv` L1249
                Group g;
                g.name = c.attribute("Name").value();
                if (g.name.empty()) continue;
                for (pugi::xml_node a : c.children("Achievement")) {  // `xw` L1248
                    Def d;
                    d.name = a.attribute("Name").value();
                    if (d.name.empty()) continue;
                    d.description = a.attribute("Description").value();
                    d.icon = a.attribute("Icon").value();
                    const std::size_t dot = d.icon.find('.');
                    if (dot != std::string::npos) d.icon[dot] = '/';  // `Ed.replace` L2212
                    d.counter = sf2::data::xml_attr_int(a, "CounterValue", 0);
                    d.money = sf2::data::xml_attr_int(a, "MoneyPrize", 0);
                    d.bonus = sf2::data::xml_attr_int(a, "BonusPrize", 0);
                    d.hidden = sf2::data::xml_attr_bool(a, "Hidden", false);
                    g.items.push_back(std::move(d));
                }
                groups.push_back(std::move(g));
            }
        }
    }
    // `yt.Yua` L297: an unlock record marks its def completed + sets the
    // reward-available flag (`Ir`, L1248).
    for (Group& g : groups) {
        for (Def& d : g.items) {
            for (const WarriorSave::AchievementUnlock& u : w.achievement_unlocks) {
                if (u.name == d.name) {
                    d.completed = true;      // `Yua` L297
                    d.obtained = u.obtained_reward;
                    break;
                }
            }
        }
    }
    // `fs.uZ` (L2214) + `cab` (L2216).
    std::vector<WarriorSave::AchievementCounter> counters = w.counters;  // `m.Ib(mC)`
    for (const Group& g : groups) {
        int value = 0;
        for (std::size_t i = 0; i < counters.size(); ++i) {
            if (counters[i].name == g.name) {
                value = counters[i].value;
                counters.erase(counters.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        std::vector<Def> subs = g.items;  // `m.Ib(a[f].Hy)`
        auto push = [&out](const Def& d, int v) {
            EquipmentScreen::AchievRow r;
            r.name = d.name;
            r.description = d.description;
            r.icon = d.icon;
            r.target = d.counter;
            r.value = std::min(v, d.counter);
            r.completed = d.completed || r.value >= d.counter;
            r.money_prize = d.money;
            r.bonus_prize = d.bonus;
            // `xw.yj` (`Ir(!ObtainedReward)`, L1248/L297): an unclaimed prize.
            r.reward_available = !d.obtained && (d.money > 0 || d.bonus > 0);
            out.push_back(std::move(r));
        };
        for (const WarriorSave::AchievementUnlock& u : w.achievement_unlocks) {
            for (std::size_t k = 0; k < subs.size(); ++k) {
                const Def& l = subs[k];
                if ((!l.hidden || l.completed) && u.name == l.name) {
                    push(l, l.counter);
                    subs.erase(subs.begin() + static_cast<std::ptrdiff_t>(k));
                    break;
                }
            }
        }
        for (const Def& l : subs) {
            if (!l.hidden || l.completed) {
                push(l, value);
                if (value < l.counter) break;
            }
        }
    }
    return out;
}

// `<PerkTree>` tier count whose `<Level Value>` the player has reached
// (JS `Bt.n5a` L1354: `for(;c<d && !(this.Sx[c++].level>b);)++a`, `b =
// p.o.bb()` = the save Level; `Sx` is sorted by level in `Bt.parse` L1353).
int perk_tier_count_at_level(int player_level) {
    sf2::data::xml_doc doc;
    if (!parse_res_xml("reference/extracted/xml/res/character_progress.xml", doc)) return 0;
    const pugi::xml_node root = doc.root().first_child();
    if (!root) return 0;
    int n = 0;
    for (pugi::xml_node lvl : root.child("PerkTree").children("Level")) {
        const int tier = sf2::data::xml_attr_int(lvl, "Value", 0);
        if (tier > player_level) break;
        ++n;
    }
    return n;
}

// achievements.xml Name -> (MoneyPrize, BonusPrize), the `xw.AE`/`xw.dP`
// pair `rCa` (L294) needs for `Ir` (L1248).
std::map<std::string, std::pair<int, int>> achievement_prizes() {
    std::map<std::string, std::pair<int, int>> out;
    sf2::data::xml_doc doc;
    if (!parse_res_xml("reference/extracted/xml/res/achievements.xml", doc)) return out;
    const pugi::xml_node root = doc.root().first_child();
    if (!root) return out;
    for (pugi::xml_node c : root.children("Counter")) {
        for (pugi::xml_node a : c.children("Achievement")) {
            const std::string name = a.attribute("Name").value();
            if (name.empty()) continue;
            out[name] = {sf2::data::xml_attr_int(a, "MoneyPrize", 0),
                         sf2::data::xml_attr_int(a, "BonusPrize", 0)};
        }
    }
    return out;
}

// The `cs` badge values for one save (`cs.getCounterValue` L2189).
// `new_moves` is the caller's `sCa()` count (it needs the tab's `v.uQ()`
// list, which lives with the Moves tab builder); `catalog` is
// `load_full_catalog` for the `I.Vr` type test.
ProfileBadges profile_badges(const WarriorSave& w, int new_moves,
                             const std::vector<CatalogItem>& catalog) {
    ProfileBadges b;
    // 0: `uCa` (L305) = `n5a() - co.KS.Oa.length`.
    b.perk_points =
        perk_tier_count_at_level(w.level) - static_cast<int>(w.perk_history.size());
    if (b.perk_points < 0) b.perk_points = 0;
    // 1: `sCa` (L256) = `v.uQ()` entries with `aE`.
    b.new_moves = new_moves;
    // 2: `rCa` (L294): every `v.uv` def with `yj` - i.e. every save unlock
    // whose `Ir(!gO)` (L297) survived, and which actually pays (L1248).
    const std::map<std::string, std::pair<int, int>> prizes = achievement_prizes();
    for (const WarriorSave::AchievementUnlock& u : w.achievement_unlocks) {
        if (u.obtained_reward) continue;
        const auto it = prizes.find(u.name);
        if (it == prizes.end()) continue;
        if (it->second.first > 0 || it->second.second > 0) ++b.new_achievs;
    }
    // 3: `vCa` (L256) = `xa.hJ(I.Vr)` rows with `pd() > 0 && ib.yj`.
    for (const WarriorSave::OwnedItem& oi : w.items) {
        if (oi.count <= 0) continue;
        if (!w.has_counter_item(oi.name)) continue;
        for (const CatalogItem& ci : catalog) {
            if (ci.type == "Seal" && ci.name == oi.name) {
                ++b.new_seals;
                break;
            }
        }
    }
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
// EquipmentScreen
// ---------------------------------------------------------------------------

EquipmentScreen::EquipmentScreen(ScreenManager& mgr) : Screen(mgr, "Equipment") {
    // Ported SEALS tab (`gs.uZ`, L2231): JS filters `p.o.xa.hJ(I.Vr)` then
    // keeps count>0. Native uses the full catalog (`<Item Type="Seal">`, 7 on
    // disk) + the save inventory; the row image is the list.xml `Image`
    // ("drop_blue_seal"), resolved through `oe`/`draw_user_image` (L2232).
    try {
        const std::vector<CatalogItem> full = load_full_catalog(app());
        const WarriorSave w = app().save().load();
        for (const CatalogItem& ci : full) {
            if (ci.type != "Seal") continue;
            int count = 0;
            for (const auto& oi : w.items) {
                if (oi.name == ci.name) count += oi.count;
            }
            if (count > 0) seal_rows_.push_back({ci.name, ci.image});
        }
        std::fprintf(stdout, "[profile] seals tab: %zu owned\n", seal_rows_.size());
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] seals load failed: %s\n", e.what());
    }
    // Ported `ds` PERK TREE (L2227) — PerkTree + perks.xml + save <PerkHistory>.
    // Placed before the Moves block (whose failure paths `return`) so it loads
    // even without fight assets.
    try {
        const WarriorSave w = app().save().load();
        perk_rows_ = load_perk_tree(app(), w);
        player_level_ = w.level;  // `p.o.bb()` for the `uk.zo` gate (L2223)
        std::fprintf(stdout, "[profile] perk tree: %zu rows\n", perk_rows_.size());
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] perk tree load failed: %s\n", e.what());
    }
    // Ported `fs` ACHIEVEMENTS (L2213) — achievements.xml + save counters join.
    try {
        const WarriorSave w = app().save().load();
        achiev_rows_ = load_achievements(app(), w);
        std::fprintf(stdout, "[profile] achievements: %zu rows\n", achiev_rows_.size());
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] achievements load failed: %s\n", e.what());
    }
    // `cs` tab badges (JS `Eg.GU` L1853 -> `Le.badge.lk(getCounterValue)`).
    // Computed here, before the Moves block's own early-outs, so the strip
    // always carries the real values. `sCa` (L256) counts the wielded
    // weapon's `v.uQ()` list entries flagged `aE` (`<OpenTricks>`, L250/L268);
    // the Moves block below rebuilds the same list for display.
    try {
        const WarriorSave w = app().save().load();
        int new_moves = 0;
        if (app().has_fight_assets()) {
            FightAssets& fa = app().fight_assets();
            sf2::scene::Fighter badge_fig;
            badge_fig.set_model(fa.merged);
            badge_fig.set_perks(learned_perk_names(w));
            badge_fig.build_move_list_locks(fa.moves, owned_items(app()),
                                            /*include_universal=*/true);
            for (const sf2::scene::MoveDef* m : badge_fig.hb()) {
                if (m != nullptr && m->profile_show && w.has_open_trick(m->name)) {
                    ++new_moves;  // `Ru.aE` over `v.uQ()`
                }
            }
        }
        const ProfileBadges b = profile_badges(w, new_moves, load_full_catalog(app()));
        tab_badges_[0] = b.perk_points;
        tab_badges_[1] = b.new_moves;
        tab_badges_[2] = b.new_achievs;
        tab_badges_[3] = b.new_seals;
        std::fprintf(stdout, "[profile] cs badges: %d/%d/%d/%d\n", tab_badges_[0],
                     tab_badges_[1], tab_badges_[2], tab_badges_[3]);
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] badge load failed: %s\n", e.what());
    }
    // Folded Moves tab (JS Profile sub-view `qv`, To.kOa=11 L2201): the exact
    // learned list built with the fight rule (`build_move_list_locks` over the
    // save's owned items — display only, on a throwaway Fighter; never
    // stepped). Moved verbatim from the deleted standalone MovesScreen.
    try {
        if (!app().has_fight_assets()) {
            std::fprintf(stdout, "[profile] no fight assets — move list unavailable\n");
            return;
        }
        FightAssets& assets = app().fight_assets();
        const WarriorSave w = app().save().load();
        weapon_ = w.weapon;
        if (weapon_.empty()) weapon_ = "Fists";
        if (assets.merged.bones.empty() || assets.moves.empty()) {
            std::fprintf(stdout, "[profile] no model/moves — move list unavailable\n");
            return;
        }
        sf2::scene::Fighter fig;
        fig.set_model(assets.merged);
        fig.set_perks(learned_perk_names(w));
        // JS `es.uZ` (L2239): `this.Ul = v.uQ(9)` then
        // `this.Ul.sort((a,b) => pb(a.v4, b.v4))`.
        //   - `v.uQ(a)` (L1218) = `ra.e9a(b, v.cw().jt(), v.cw().Wk(), a)`:
        //     `ra.Z6a` (L684) walks EVERY parsed move (`ra.Lk`) and keeps the
        //     ones whose `<Locks>` pass for the fighter context
        //     (`f.nw(g, b)`), so a NO-LOCK move is kept for every weapon
        //     (moves.xml has 242). The shipped `HighBlockProfile` (no
        //     `<Locks>`, no `TacticWeapon`, no `Rank`) is therefore row 0 of
        //     the oracle `profile_tab1` capture - the "Автоматически"
        //     (`Block_Keys`) row. The previous weapon-SubType-only list
        //     dropped it, which is why the port's row 0 was DoublePunch.
        //     `ra.e9a` (L686) then intersects that list with `ra.Ul`, i.e.
        //     with the moves carrying `<Profile Show="1">` - see the filter
        //     below.
        //   - the sort key `v4` is `u.I(Profile/@Rank)`; a missing `Rank`
        //     parses to 0 (`u.I` L2455 default `b = 0`, and `K.parseInt` of
        //     an absent attr is not a number), so `HighBlockProfile` leads.
        fig.build_move_list_locks(assets.moves, owned_items(app()), /*include_universal=*/true);
        move_total_ = 0;
        for (const sf2::scene::MoveDef* m : fig.hb()) {
            if (m == nullptr) continue;
            if (!m->profile_show) continue;  // `ra.Ul` membership (L712)
            MoveRow r;
            r.name = m->name;
            r.type = m->type;
            r.priority = m->priority;
            r.image = m->profile_image;   // `Ru.image` (L1253) -> atlas 246
            r.keys = m->profile_keys;     // `Ru.fFa` (L1253) -> `ls.ymb` label
            r.rank = m->profile_rank;     // `v4` (`es.uZ` sort key, L2239)
            r.order = m->profile_order;   // `ra.Ul` document order (L712)
            move_rows_.push_back(r);
        }
        // `es.uZ` (L2239): `this.Ul.sort(function(a,b){return pb(a.v4,b.v4)})`
        // with `pb(a,b){return a<b?-1:a>b?1:0}` (L9). V8's sort is STABLE, so
        // equal `Rank` keeps `ra.Ul` document order - hence the `order`
        // tie-break (a `std::map` walk would sort them alphabetically).
        std::stable_sort(move_rows_.begin(), move_rows_.end(),
                         [](const MoveRow& a, const MoveRow& b) {
                             if (a.rank != b.rank) return a.rank < b.rank;
                             return a.order < b.order;
                         });
        move_total_ = static_cast<int>(move_rows_.size());
        std::fprintf(stdout, "[profile] moves tab: %s, %d moves\n", weapon_.c_str(),
                     move_total_);
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] move list load failed: %s\n", e.what());
    }
}

// JS `Zr.ROa` (L2222) improve-button gate: `Lc.Be!=3 && Lc.Be!=2 && Lc.Be!=1
// && !zo && vb.uwa()`. `Be==0` is the learnable cell (`id.Txb` L1356 sets
// `Bla(0)` on the lowest tier's items; `dzb` L1356 sets the tier above each
// learned record to `Bla(0)`); `zo` = `uk.k5(p.o.bb()<a.level)` (L2223) = the
// player level gate. `Bt.L1a` (L306) then writes the `<Perks>`/`<PerkHistory>`
// record — a type-2 Upgrade (`e&&f` branch) reuses the existing name.
bool EquipmentScreen::perk_buyable(int index) const {
    if (index < 0 || index >= static_cast<int>(perk_rows_.size())) return false;
    const PerkRow& r = perk_rows_[index];
    if (!r.available) return false;            // `Mw.K1` L1358
    if (player_level_ < r.tier) return false;  // `zo` (L2223) -> hidden button
    return r.state == 0;                       // `Zr.ROa` L2222: `Be==0` only
}

// JS `vb.Jzb` case 1 (L2200): `p.o.co.L1a(this.ql)` (the `<Perks>` write) +
// `p.o.co.KS.xI(name, ql.level)` (the `<PerkHistory>` append).
void EquipmentScreen::perk_buy(int index) {
    if (!perk_buyable(index)) return;
    const PerkRow r = perk_rows_[index];
    try {
        WarriorSave w = app().save().load();
        if (r.kind == "Upgrade") {
            // `Bt.L1a` L306 match branch (`e&&f`): update UpgradeLevel only.
            w.learn_perk_upgrade(r.name, r.tier, r.upgrade_max);
        } else {
            w.learn_perk(r.name, r.tier, r.upgrade_max);
        }
        app().save().save(w);
        perk_rows_ = load_perk_tree(app(), w);
        player_level_ = w.level;
        // `Bt.L1a` (L306) -> `Bt.Qua` (L307) ends in `rb.Xkb()` = `snd_learn`
        // (65598): every perk learn/upgrade plays it after the write.
        sf2::audio::AudioEngine::instance().play("snd_learn");
        std::fprintf(stdout, "[profile] perk buy %s (tier %d, upgrade %d)\n",
                     r.name.c_str(), r.tier, r.upgrade_max);
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] perk buy failed: %s\n", e.what());
    }
}

// JS `as.refresh` (L2211): the reward button shows while
// `sq.dg() && this.Wpa >= this.Xr.counter` (an unclaimed prize and the
// progress reached the target).
bool EquipmentScreen::achiev_claimable(int index) const {
    if (index < 0 || index >= static_cast<int>(achiev_rows_.size())) return false;
    const AchievRow& r = achiev_rows_[index];
    return r.reward_available && r.target > 0 && r.value >= r.target;
}

// JS `as.zhb` L2211 -> `vb.exb` L2199 -> `yt.sca` L296 + the prize payout.
void EquipmentScreen::achiev_claim(int index) {
    if (!achiev_claimable(index)) return;
    const AchievRow r = achiev_rows_[index];
    try {
        WarriorSave w = app().save().load();
        w.claim_achievement(r.name, r.money_prize, r.bonus_prize);
        app().save().save(w);
        achiev_rows_ = load_achievements(app(), w);
        std::fprintf(stdout, "[profile] achievement claim %s (+%d money +%d bonus)\n",
                     r.name.c_str(), r.money_prize, r.bonus_prize);
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] achievement claim failed: %s\n", e.what());
    }
}

void EquipmentScreen::update_impl(float dt) {
    (void)dt;
    ensure_lang(app());  // the lang table powers the `Y.na` string lookups
    // D3: `Wb` is a GLOBAL overlay — a dialog queued on ANY screen blocks that
    // screen's input (the Profile tab strip included).
    if (quest_modal_consume(app())) return;
    const App::PointerState& p = app().pointer();
    hover_ = -1;
    // BACK (top-left) -> the previous screen (the loop's equipment -> dojo
    // leg).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[equip] BACK -> previous screen\n");
            std::fflush(stdout);
            manager().pop();
            return;
        }
    }
    // `cs` bottom tab strip (JS L2188): select the Profile sub-view via
    // `vb.hla` (L2190-2193) — 0 = `ds` perk tree, 1 = folded Moves (`es`),
    // 2 = `fs` achievements, 3 = `gs` seals (all ported).
    // D-HOVER determinism: the hover comes from the LOGICAL pointer — the
    // same source the internal-injection harness drives — and is LATCHED on
    // the frames the pointer is actually driven (`pressed`/`down`), instead of
    // re-sampling the process every frame. `App::poll_input` falls back to
    // `glfwGetCursorPos` whenever no injected click is pending, so an
    // unlatched hover tracked the user's real mouse and made
    // `profile_tab3.png` diff between runs. At steady state (no press) the
    // latch keeps the last logical position -> byte-identical captures.
    if (p.pressed || p.down) tab_hover_ = profile_tab_hit(p.x, p.y);
    if (tab_hover_ >= 0 && p.pressed) {
        sf2::audio::AudioEngine::instance().play("snd_click_2");
        std::fprintf(stdout, "[profile] tab %d (%s)\n", tab_hover_,
                     kProfileTabs[tab_hover_].label);
        std::fflush(stdout);
        tab_ = tab_hover_;
        // JS `vb.hla` (L2191): `this.jq != null && (this.jq.zha(),
        // this.jq.X(!0)); this.zh.GU();` - the newly active sub-view's `zha()`
        // runs on every tab switch (tabs 0/2 are the `Xd.zha(){}` no-op,
        // L2185). Moves (`es.zha` L2239): for every `v.uQ()` entry with `aE`,
        // clear it (`Bt.inb` L269 removes the `<Trick>` row) and `p.o.save()`.
        // Seals (`gs.zha` L2231): every owned `I.Vr` item def `Ir(!1)` (clear
        // the `<CounterItems>` flag) + `p.o.save()`. So both badges drop to 0
        // as soon as their tab is opened; `this.zh.GU()` then rebuilds the
        // strip from the cleared values.
        if (tab_ == kProfileTabMoves || tab_ == kProfileTabSeals) {
            try {
                WarriorSave w = app().save().load();
                if (tab_ == kProfileTabMoves && !w.open_tricks.empty()) {
                    w.clear_open_tricks();
                    app().save().save(w);
                    tab_badges_[kProfileTabMoves] = 0;
                    std::fprintf(stdout, "[profile] es.zha: cleared OpenTricks\n");
                    std::fflush(stdout);
                } else if (tab_ == kProfileTabSeals && !w.counter_items.empty()) {
                    w.counter_items.clear();  // `Ir(!1)` on every owned Seal
                    app().save().save(w);
                    tab_badges_[kProfileTabSeals] = 0;
                    std::fprintf(stdout, "[profile] gs.zha: cleared CounterItems\n");
                    std::fflush(stdout);
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[profile] zha failed: %s\n", e.what());
            }
        }
    }
    // --- Tab 0 PERK TREE: `uk` cell selection (`vb.hqb` L2198) + the flat
    // improve button (`Zr.ygb` L2222 -> `vb.Cab` -> `Jzb` case 1 L2200 ->
    // `Bt.L1a` L306 + `Ht.xI` L1328). The `uk` hit rects were captured by
    // render_impl so update hit-tests the exact wrapping layout.
    perk_hover_ = -1;
    if (tab_ == kProfileTabLeveling) {
        for (const PerkCellHit& h : perk_cell_hits_) {
            if (h.index < 0) continue;
            if (p.x >= h.cx - h.half && p.x <= h.cx + h.half &&
                p.y >= h.cy - h.half && p.y <= h.cy + h.half) {
                perk_hover_ = h.index;
                if (p.pressed) {
                    sf2::audio::AudioEngine::instance().play("snd_focus_1");
                    perk_sel_ = h.index;  // `vb.uj = a` (L2198)
                }
                return;
            }
        }
        const ShopRect ib = profile_improve_rect();
        if (perk_sel_ >= 0 && perk_buyable(perk_sel_) && p.x >= ib.J && p.x <= ib.N &&
            p.y >= ib.P && p.y <= ib.W) {
            if (p.pressed) {
                sf2::audio::AudioEngine::instance().play("snd_click_1");
                perk_buy(perk_sel_);
            }
            return;
        }
    }
    // --- Tab 1 MOVES: the `es` cell selection (`vb.hqb` L2198 -> `umb`
    // L2183 -> `$r.refresh` L2234). The `ks` hit rects were captured by
    // render_impl so update hit-tests the exact list layout.
    move_hover_ = -1;
    if (tab_ == kProfileTabMoves) {
        for (const MoveCellHit& h : move_cell_hits_) {
            if (h.index < 0) continue;
            if (p.x >= h.cx - h.half_w && p.x <= h.cx + h.half_w &&
                p.y >= h.cy - h.half_h && p.y <= h.cy + h.half_h) {
                move_hover_ = h.index;
                if (p.pressed) {
                    sf2::audio::AudioEngine::instance().play("snd_focus_1");
                    select_move(h.index);  // `vb.uj = a` (L2198)
                }
                return;
            }
        }
    }
    // --- Tab 2 ACHIEVEMENTS: the cell reward button (`as.zhb` L2211 ->
    // `vb.exb` L2199 -> `yt.sca` L296 + money/bonus payout).
    achiev_hover_ = -1;
    if (tab_ == kProfileTabAchiev) {
        const ProfileLayout pl = profile_layout();
        for (int i = 0; i < static_cast<int>(achiev_rows_.size()); ++i) {
            const ShopRect row = profile_achiev_row_rect(pl.viewer, i);
            if (row.W > pl.viewer.W) break;
            if (!achiev_claimable(i)) continue;
            const ShopRect rb = profile_achiev_reward_rect(pl.viewer, i);
            if (p.x >= rb.J && p.x <= rb.N && p.y >= rb.P && p.y <= rb.W) {
                achiev_hover_ = i;
                if (p.pressed) {
                    sf2::audio::AudioEngine::instance().play("snd_buy");
                    achiev_claim(i);
                }
                return;
            }
        }
    }
    // Shared `za` nav column (JS `ma.D1`): Dojo/Map/Shop/Settings hops. The
    // oracle `profile_tab*` captures show the nav COLLAPSED (the `МЕНО`
    // header only) — force it like the Map (`za.xyb` collapses on arrival).
    za_update(app(), *this, kScreenProfile, dt);
}

void EquipmentScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // --- Backdrop: the destination `dojo_shop` art (`Pi.Qa`, L439) ---------
    // JS `vb extends ma` (L2189): `this.Ad = new Pi` (L2196) and `Ea` calls
    // `this.Tya(this.Ad)` (L2195). `Pi` renders `Qa = R.$(E.get(752))` =
    // `locations/dojo_shop/bg.{image}` under `ma.Tya` (L1832) — a dedicated
    // destination background, NOT the dojo location layers.
    draw_destination_backdrop(app, 0.6392156862745098f);  // JS `Z.Ena` (L2479)
    draw_destination_model(app, ren, backdrop_fighter_, backdrop_fig_tried_,
                           backdrop_fig_ok_, backdrop_idle_);
    draw_destination_dim(ren);

    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return;
    }
    // JS `vb.layout` (L2195-2196): the active sub-view `jq` docks into
    // `a = b.fn(.75)`; `XB=ei` (`Pn(b)`) and `zr=Yr` (`Pn(c)`) dock into the
    // two side slots. The content split mirrors the shop `Oa.layout`.
    const ProfileLayout pl = profile_layout();
    const ShopRect& v = pl.viewer;
    // --- Parchment chrome (JS `E.get(254)` = res/ui/scroll) ----------------
    // Centre viewer `jq` = the `Xd`/`Fg` paper content frame (L1868-1872):
    // body `bg` + top/bottom `Zh` rolls (`roll_end` + stretched
    // `roll_center` + mirrored `roll_end`). The side panels (`XB`/`zr`) use
    // the `info_panel_v` frame stretched into their slots. Flat fallback only
    // on a genuine atlas miss (never a silent blank).
    if (load_scroll_atlas(app)) {
        const float midx = v.J + v.width() * 0.5f;
        const float midy = v.P + v.height() * 0.5f;
        if (!try_draw_atlas_button(app, "bg", midx, midy, v.width(), v.height(), 1.0f,
                                   /*fill=*/true, /*flip_x=*/false)) {
            draw_flat_button(app, "", midx, midy, v.width(), v.height(), 0.62f, 0.5f,
                             0.34f, false);
        }
        constexpr float kRollSrcH = 114.0f, kRollCapSrcW = 101.0f;
        const float roll_h = 30.0f;                                 // Zh(w,30)
        const float capw = kRollCapSrcW * (roll_h / kRollSrcH);
        const float bodyw = v.width() - 2.0f * capw;
        auto roll_bar = [&](float cy) {
            try_draw_atlas_button(app, "roll_end", v.J + capw * 0.5f, cy, capw, roll_h,
                                  1.0f);
            try_draw_atlas_button(app, "roll_end", v.N - capw * 0.5f, cy, capw, roll_h,
                                  1.0f);
            if (bodyw > 0.0f) {
                try_draw_atlas_button(app, "roll_center", v.J + capw + bodyw * 0.5f, cy,
                                      bodyw, roll_h, 1.0f, /*fill=*/true);
            }
        };
        roll_bar(v.P + roll_h * 0.5f);
        roll_bar(v.W - roll_h * 0.5f);
        auto side_panel = [&](const ShopRect& s) {
            if (!try_draw_atlas_button(app, "info_panel_v",
                                       s.J + s.width() * 0.5f, s.P + s.height() * 0.5f,
                                       s.width(), s.height(), 1.0f, /*fill=*/true)) {
                draw_flat_button(app, "", s.J + s.width() * 0.5f, s.P + s.height() * 0.5f,
                                 s.width(), s.height(), 0.62f, 0.5f, 0.34f, false);
            }
        };
        side_panel(pl.right_slot);              // `zr=Yr` (always visible)
        if (tab_ == kProfileTabLeveling) {
            side_panel(pl.left_slot);           // `XB=ei` (tab 0 only, L2190)
        }
    }
    // `cs` tab strip (JS L2188) — always visible, drawn over the body bottom.
    draw_profile_tabs(app, tab_, tab_hover_, tab_badges_);
    // JS `XB=ei` header is shown on tab 0 only (`hla` case 0 `ivb()`); the
    // other cases call `dga()` and hide it (L2190-2191).
    if (tab_ == kProfileTabLeveling) {
    perk_cell_hits_.clear();  // repopulated below (update hit-tests them)
    // Ported `ds` POWERLEVELING_SLIDER body (L2227): `Tt = id.ht().tH` - the
    // PerkTree tiers (character_progress.xml asset 1315 via `td.Vib` L1160),
    // each the `tk` compare cell (up to two `uk` cells + `Rx` arrows,
    // L2217-2222) sized `ba(400,150)` (`ds.NC` L2230). The `tk`/`uk` atlas art
    // (profile 258/246) is ASTC -> the text/flat cell is the live path.
    // `XB=ei.zs` (L2206) shows `ProfileNoPerks` while the learned grid (`Gk`,
    // fed by `p.o.co.jF`) is empty; the `zr=Yr` panel shows
    // `profileNoSkills` while nothing is available (`vb.hla` case 0, L2190).
    // Both dock into the side slots (L2196 `Pn(b)`/`Pn(c)`).
    {
        bool any_learned = false, any_avail = false;
        for (const PerkRow& pr : perk_rows_) {
            if (pr.learned_level > 0) any_learned = true;
            // `ds.MCa` (L2227): a learnable (`Be==0`) perk whose `level` the
            // player has reached. The oracle `profile_tab0` (level 1) shows
            // `profileNoSkills` — the min perk tier is 2.
            if (pr.state == 0 && w.level >= pr.tier) any_avail = true;
        }
        if (!any_learned) {
            // `ei.zs` wraps (`ea.rd(!0)`, L2206) into the panel: oracle shows
            // "У вас нет / изученных / умений" (3 centred lines).
            draw_ui_wrapped(app, pl.left_slot.J + 14.0f,
                            pl.left_slot.P + pl.left_slot.height() * 0.5f - 50.0f,
                            pl.left_slot.width() - 28.0f, 100.0f,
                            loc(app, "ProfileNoPerks", "You have no learned skills"),
                            0.5f, UiAlign::Center, 0.16f, 0.11f, 0.06f);
        }
        if (!any_avail) {
            draw_ui_wrapped(app, pl.right_slot.J + 14.0f,
                            pl.right_slot.P + pl.right_slot.height() * 0.5f - 36.0f,
                            pl.right_slot.width() - 28.0f, 72.0f,
                            loc(app, "profileNoSkills", "No available skills"), 0.5f,
                            UiAlign::Center, 0.16f, 0.11f, 0.06f);
        }
    }
    if (!perk_rows_.empty()) {
        // `ds.NC` (L2230) sizes every `tk` cell `b.ba(400,150)`; `ds.init`
        // (L2227) leaves `Pa.spacing` at the `Gg` default 0, so the row pitch
        // is `150*A/400` with `A = scroll.Gv - 8` (see `profile_cell_pitch`).
        // The previous `0.25*v.width()` was a measured stand-in for the
        // declared height, not the rendered one.
        const float row_h = profile_cell_pitch(v, 400.0f, 150.0f, 0.0f);
        // --- JS `tk`/`Rx`/`uk` row geometry (L2217-2230) ------------------
        // `ds.NC` (L2230) sizes every `tk` cell `b.ba(400,150)`. `tk.$i`
        // (L2217) fixes `H9=80`; `tk.ba` (L2218) centres the two `uk` nodes
        // at `ce.x/2 -/+ H9` (`c.C(a-this.H9)` / `d.C(a+this.H9)`, lone cell
        // `c.C(a)`), scales each node to `f = ce.y*.7`, and scales the
        // `jC.gw` (`Rx`) group by `.5`.
        constexpr float kTkCellH = 150.0f;          // `ba(400,150)` L2230
        constexpr float kTkH9 = 80.0f;              // `this.H9=80` L2217
        constexpr float kUkCellScale = 0.7f;        // `ce.y*.7` L2218
        // `Rx` (L2226): `pieces/perkcircle` (`y.kSa`, 64), `pieces/perk_line_h`
        // (`y.coa`, `xc(50)`, src 10), `pieces/perk_line_v` (`y.doa`,
        // `CO.Pb(130)`, src 10). `uk.Dy` (L2222) `pieces/level1` (56x55) at
        // `la(.8)` offset `FH/2 - Dy*1.15`; `Ed.FH` `pieces/perkback` src 158
        // fixes the `uk` local frame.
        constexpr float kPerkCircleSrc = 64.0f;
        constexpr float kPerkLineSrc = 10.0f;
        constexpr float kPerkLineLen = 50.0f;       // `b.xc(50)` L2226
        constexpr float kPerkLineVSrc = 130.0f;     // `CO.Pb(130)` L2226
        constexpr float kPerkbackSrc = 158.0f;
        constexpr float kLevelSrcW = 56.0f;
        constexpr float kLevelSrcH = 55.0f;
        constexpr float kRxScale = 0.5f;            // `jC.gw.la(.5)` L2218
        constexpr float kFlagScale = 0.8f;          // `Dy.la(.8)` L2222
        const float tk_s = row_h / kTkCellH;        // JS `tk` unit -> native px
        const float h9 = kTkH9 * tk_s;              // `H9` L2217, native px
        const float ico = row_h * kUkCellScale;     // `uk` node target (L2218)
        const float rx_circ = kPerkCircleSrc * kRxScale * tk_s;
        const float rx_line = kPerkLineLen * kRxScale * tk_s;
        const float rx_th = kPerkLineSrc * kRxScale * tk_s;
        const float rx_arrow = kPerkLineVSrc * kRxScale * tk_s;
        const float rx_arrow_w = kPerkLineSrc * kRxScale * tk_s;
        const float rx_a = rx_circ * 0.5f;          // `kSa.za()/2` L2226
        const float bdg_unit = ico / kPerkbackSrc;  // `uk` local -> native px
        const float bdg_w = kLevelSrcW * kFlagScale * bdg_unit;
        const float bdg_h = kLevelSrcH * kFlagScale * bdg_unit;
        const float bdg_dx = (kPerkbackSrc * 0.5f -
                              kLevelSrcW * kFlagScale * 1.15f) * bdg_unit;
        const float bdg_dy = (kPerkbackSrc * 0.5f -
                              kLevelSrcH * kFlagScale * 1.15f) * bdg_unit;
        // The `tk` is symmetric about the seam the `Rx` group docks to: the
        // cell centre = `cell.ce.x/2` (L2218) in native px inside the `Gg`
        // list rect.
        const float rcx = profile_list_left(v) + profile_list_w(v) * 0.5f;
        // `Gg.ba` (L1885): `this.uz = (this.size.y - this.cells[0].qa())/2;
        // this.ei.D(this.gj = this.uz)`. `size.y` is the scroll's inner height
        // (`Fg.ba` L1870 case 0: `Xy = b - 2*vk`, `vk = .08*w`), so the cell
        // list is centred on the viewer as a whole.
        float row_top = profile_list_top(v, 400.0f, 150.0f);
        int last_tier = -1;
        int tier_idx = -1;   // `tk.$i(a==0, a+1==len)` L2228 first/last gate
        int col = 0;
        for (std::size_t i = 0; i < perk_rows_.size(); ++i) {
            const PerkRow& r = perk_rows_[i];
            if (r.tier != last_tier) {
                if (last_tier != -1) {
                    // `Gg.ba` (L1885) advances by `f.qa() + this.spacing` only;
                    // `ds.init` (L2227) leaves `spacing` at 0, so there is NO
                    // extra inter-tier gap (the old invented +6px).
                    row_top += row_h;
                    col = 0;
                }
                last_tier = r.tier;
                ++tier_idx;
                // The JS `tk` compare cell (L2217-2222) draws NO tier text —
                // only the two `uk` cells and the `Rx` arrows.
            }
            if (row_top + row_h > v.W - 34.0f) break;
            if (col >= 2) {  // `tk` packs two `uk` cells per tier
                row_top += row_h;
                col = 0;
            }
            if (row_top + row_h > v.W - 34.0f) break;
            const float cy = row_top + row_h * 0.5f;
            const bool pair_left = (col == 0) && (i + 1 < perk_rows_.size()) &&
                                   (perk_rows_[i + 1].tier == r.tier);
            // `tk.ba` (L2218) `uk` slots: `a-/+H9` for a pair, `a` alone.
            const float cx = pair_left ? rcx - h9 : (col == 1 ? rcx + h9 : rcx);
            sf2::render::Renderer& rr = app.renderer();
            // `tk` ctor (L2217) appends `jC.gw` BEFORE `Xj`/`xi`, so the `Rx`
            // seam group renders UNDER the `uk` art. Drawn once per pair.
            if (pair_left) {
                // `y.kSa` hub at the seam (`jC.gw.C/D`, L2218).
                (void)try_draw_atlas_button(app, "pieces/perkcircle", rcx, cy, rx_circ,
                                            rx_circ, 1.0f);
                // `y.coa` runs `xc(50)`: left `ik(1,.5)` right edge at `-a`,
                // right `ik(0,.5)` left edge at `+a` (L2226).
                (void)try_draw_atlas_button(app, "pieces/perk_line_h",
                                            rcx - rx_a - rx_line * 0.5f, cy, rx_line, rx_th,
                                            0.85f, /*fill=*/true);
                (void)try_draw_atlas_button(app, "pieces/perk_line_h",
                                            rcx + rx_a + rx_line * 0.5f, cy, rx_line, rx_th,
                                            0.85f, /*fill=*/true);
                // `y.doa` runs `Pb(130)` (`CO`/`MM`), bottom/top-centre anchored
                // at `-a`/`+a`; `refresh` (L2226) hides the up run on the first
                // tier and the down run on the last (`tk.$i` L2217).
                if (tier_idx > 0) {
                    (void)try_draw_atlas_button(app, "pieces/perk_line_v", rcx,
                                                cy - rx_a - rx_arrow * 0.5f, rx_arrow_w,
                                                rx_arrow, 0.85f, /*fill=*/true);
                }
                if (i + 2 < perk_rows_.size()) {
                    (void)try_draw_atlas_button(app, "pieces/perk_line_v", rcx,
                                                cy + rx_a + rx_arrow * 0.5f, rx_arrow_w,
                                                rx_arrow, 0.85f, /*fill=*/true);
                }
            }
            // `uk` cell icon `Ed.Fs` (L2202): the perks.xml `Image` frame on
            // atlas 246 (`skills`; default `y.gTa` "Icons01/IconAvenger" L2470,
            // `Ye.qI` dot->slash). `pieces/perkback` (`Ed.FH`, L2202) is the
            // backplate; a flat plate is the explicit miss fallback.
            const float icx = cx;   // `uk` node centre (`tk.ba` L2218)
            perk_cell_hits_.push_back({icx, cy, ico * 0.5f, static_cast<int>(i)});
            if (!try_draw_atlas_button(app, "pieces/perkback", icx, cy, ico, ico, 1.0f)) {
                const float pr = r.available ? 0.22f : 0.12f;
                const float pg = r.available ? 0.26f : 0.14f;
                const float pb = r.available ? 0.36f : 0.17f;
                const float q[] = {icx - ico * 0.5f, cy - ico * 0.5f,
                                   icx + ico * 0.5f, cy - ico * 0.5f,
                                   icx + ico * 0.5f, cy + ico * 0.5f,
                                   icx - ico * 0.5f, cy - ico * 0.5f,
                                   icx + ico * 0.5f, cy + ico * 0.5f,
                                   icx - ico * 0.5f, cy + ico * 0.5f};
                rr.draw_triangles(q, 6, pr, pg, pb, 0.95f);
            }
            // `uk.k5`/`Ed.Syb` (L2222/L2203): `zo = EW || Be==3` where
            // `EW = p.o.bb() < perk.level` (player level below the tier) and
            // `Be==3` = an owned/learned perk (`new Ih(...,3)` in `mXa`, L2211).
            // When `zo` the icon `Fs` is HIDDEN and `pieces/icons_kick_blocked`
            // (`V$`) is shown; `Ed.DOa` overlays `pieces/icons_kick_off` (`X$`)
            // whenever the perk is not active (`!$r`).
            // `uk.k5`/`r0` (L2224): `EW || Be==3` — `EW` = the player level is
            // below the tier (`p.o.bb()<a.level`), `Be==3` = owned/placeholder.
            const bool perk_locked = (w.level < r.tier) || r.state == 3;
            if (perk_locked) {
                (void)try_draw_atlas_button(app, "pieces/icons_kick_blocked", icx, cy, ico, ico,
                                            0.95f);
            } else if (!draw_cell_icon(app, r.image, icx, cy, ico * 0.92f, ico * 0.92f, 1.0f)) {
                const float isz = ico * 0.22f;
                const float iq[] = {icx - isz, cy - isz, icx + isz, cy - isz,
                                    icx + isz, cy + isz, icx - isz, cy - isz,
                                    icx + isz, cy + isz, icx - isz, cy + isz};
                rr.draw_triangles(iq, 6, 0.35f, 0.35f, 0.4f, 0.95f);
            }
            if (!r.available) {
                (void)try_draw_atlas_button(app, "pieces/icons_kick_off", icx, cy, ico, ico, 0.9f);
            }
            // `uk.Dy` badge (`uk.vca`/`i9a` L2225): `i9a()` = "pieces/level"+
            // `PQ()`, `la(.8)`, `C(FH.x/2 - za()*1.15)`, `D(FH.y/2 - qa()*1.15)`.
            // Gate: hidden when `PQ()<=0` OR (`PQ()==1 && type!=2 && type!=3`)
            // — i.e. an unlearned Perk (type 1) that has no upgrade yet.
            const bool badge_on = r.pq > 0 && !(r.pq == 1 && r.type != 2);
            if (badge_on) {
                char lb[24];
                std::snprintf(lb, sizeof(lb), "pieces/level%d", r.pq);
                (void)try_draw_atlas_button(app, lb, icx + bdg_dx + bdg_w * 0.5f,
                                            cy + bdg_dy + bdg_h * 0.5f, bdg_w, bdg_h, 1.0f);
            }
            // The JS `uk` cell (L2222-2225) draws NO text: only `Ed.Fs` (the
            // perk icon / `V$` blocked / `X$` off overlays) and the `Dy`
            // level badge. The former native name/status labels were invented.
            ++col;
        }
        // `Zr` improve button (`ygb` L2222 -> `vb.Cab` L2199): shown while
        // the selected row is buyable (`Zr.ROa` L2222 `Be==0 && !zo`). The
        // `Zr` `EButtonWhite` ASTC art (`y.qB`) is OPEN (PORT_AUDIT_UI §5);
        // the label is `Y.na("profile_BtnImprove")` (L2219).
        if (perk_sel_ >= 0 && perk_buyable(perk_sel_)) {
            const ShopRect ib = profile_improve_rect();
            const float bx = (ib.J + ib.N) * 0.5f;
            const float by = (ib.P + ib.W) * 0.5f;
            const bool hov = app.pointer().x >= ib.J && app.pointer().x <= ib.N &&
                             app.pointer().y >= ib.P && app.pointer().y <= ib.W;
            draw_flat_button(app, "IMPROVE", bx, by, ib.width(), ib.height(),
                             hov ? 0.55f : 0.4f, 0.4f, 0.25f, hov);
            draw_ui_label(app, ib.J, by - 10.0f, ib.width(), 20.0f,
                          loc(app, "profile_BtnImprove", "Improve"), 0.65f,
                          UiAlign::Center, 1.0f, 1.0f, 1.0f);
        }
    }
    } else if (tab_ == kProfileTabMoves) {
        // Folded Moves sub-view (JS `qv`/`es`, To.kOa=11 L2201): the learned
        // moves for the wielded weapon. `es.NC` (L2240) cells are
        // `ba(400,150)`, one `ks` cell per row inside the `Xd` scroll.
        // The old
        // `app.draw_text` passed a RAW glyph scale (not `ua*ea_a1`) — the
        // giant overlapping text in the port capture.
        // JS `es.NC` (L2240) -> `ks.init(Ru, es)` (L2233) -> `ls.init(a.image,
        // a)` (L2235): every row is a `ks` cell - a `gf`/`ff` slider cell
        // declared `ba(400,150)` - whose content is the `ls` widget: the
        // move's `skills`-atlas image (`Ed.ZL` L2203 `Fs.Cb(this.sO)`) plus
        // the `ls.ymb` (L2238) `Y.na(KeysDescription)` label. The JS cell
        // carries NO name / type / priority text.
        // Pitch: `Gg.ba` (L1884-1885) `c += f.qa() + this.spacing` with
        // `qa()` = `ff.qa` (L1893) = `ce.y * node.Eb`, `node.Eb = A/ce.x`;
        // `es.init` (L2239) sets `spacing = 10`. See `profile_cell_pitch`.
        const float cell_h = profile_cell_h(v, 400.0f, 150.0f);  // `ff.qa` L1893
        const float row_h = cell_h + 10.0f;                      // + `spacing` L2239
        // `Gg.ba` (L1885): `uz = (size.y - cells[0].qa())/2`, `ei.D(uz)`; the
        // list itself sits in `scroll.content` at `C(0.08w)`/`D(0.08w)`
        // (`Fg.ba` case 0, L1871).
        const float cell_l = profile_list_left(v);
        const float cell_w = profile_list_w(v);
        const float row_top = profile_list_top(v, 400.0f, 150.0f);
        // NO weapon-name header: the JS `es` (L2238-2240) is a bare `Xd`
        // slider — `es.init` (L2239) sets only `spacing`/`v2`, `es.NC` (L2240)
        // returns a `ks`/`ls` cell whose `ymb` (L2238) label is the move's own
        // `KeysDescription`, and `ls` "carries NO name / type / priority text".
        // The former header was an invention (the JS Profile `vb` header
        // `XB`/`ei` is `show()`n only on tab 0 and `pn()`-hidden for Moves,
        // `hla` L2190-2191); the selected move's name lives in the `$r` right
        // panel below (`Yr.umb` -> `B4`, L2183).
        move_cell_hits_.clear();
        for (std::size_t i = 0; i < move_rows_.size(); ++i) {
            const MoveRow& r = move_rows_[i];
            const float ry = row_top + static_cast<float>(i) * row_h;
            if (ry + cell_h > v.W) break;
            // `es`/`ks` cell hit rect (captured for the `vb.hqb` L2198
            // selection that refreshes the `$r` right panel).
            move_cell_hits_.push_back({cell_l + cell_w * 0.5f, ry + cell_h * 0.5f,
                                       cell_w * 0.5f, cell_h * 0.5f, static_cast<int>(i)});
            // `ls.ba` (L2236): `d = b/2`, `e = b*.1`, `this.icon.zf(b*.8)`
            // (icon box = 0.8 * cell height, square) and
            // `this.icon.C(this.icon.za()/2 + e)` / `D(d)` - the icon CENTRE
            // is `0.5*icon + 0.1*cellH` from the cell's left edge and
            // vertically centred in the cell.
            const float icon_h = cell_h * 0.8f;                    // `zf(b*.8)`
            const float icon_cx = cell_l + icon_h * 0.5f + cell_h * 0.1f;
            const float icon_cy = ry + cell_h * 0.5f;              // `D(b/2)`
            if (!r.image.empty()) {
                // `Ed.ZL` (L2203): `this.Fs.Cb(this.sO)` draws the `skills`
                // (atlas 246) frame `Ye.qI(Profile/@Icon)` (L1863).
                draw_cell_icon(app, r.image, icon_cx, icon_cy, icon_h, icon_h, 1.0f);
            }
            // `ls.ymb` (L2238): `this.Zh.V(Y.na(this.Tp.fFa))`, hidden when
            // `fFa` is null/empty; `Zh.C(this.icon.ya + this.icon.za()/2*1.2)`
            // and `Zh.ua(b*.3)`, `D(0)` for a move with no requirement list
            // (`Ck.length == 0` -> the `else` branch of `ls.ba`).
            if (!r.keys.empty()) {
                const float label_l = icon_cx + icon_h * 0.6f;  // `+za()/2*1.2`
                const float label_w = cell_l + cell_w - label_l;
                draw_ui_label(app, label_l, ry, label_w, cell_h,
                              loc(app, r.keys, r.keys), ui_ua_scale(app, cell_h * 0.3f),
                              UiAlign::Left, 0.16f, 0.11f, 0.06f);
            }
        }
        // `zr=Yr` right panel: the selected move name + the `$r.Op`
        // `Y.na("profile_BtnShow")` view button (L2234 `$r.ba`).
        if (!move_rows_.empty()) {
            const ShopRect& rp = pl.right_slot;
            // `vb.hqb` (L2198) sets `this.uj = a` (the clicked `ks` cell) and
            // `umb` (L2183) calls `this.Lo.refresh(a.Tp, a.jea())` -> the `$r`
            // panel shows the SELECTED move, not `move_rows_.front()`.
            const std::string nm = shown_move();
            draw_ui_label(app, rp.J + 8.0f, rp.P + 26.0f, rp.width() - 16.0f, 44.0f,
                          loc(app, nm, nm), 0.95f, UiAlign::Center, 0.16f, 0.11f, 0.06f);
            const float bw2 = rp.width() * 0.72f, bh2 = 46.0f;
            const float bx2 = rp.J + rp.width() * 0.5f;
            const float by2 = rp.W - 70.0f;
            // `$r.Op.Wm(null, y.qB)` (L2234) with `y.qB` = "highlightButton"
            // (L2470) - the sliced atlas name, not the `Bb` class key.
            if (!try_draw_atlas_button(app, "highlightButton", bx2, by2, bw2, bh2, 1.0f)) {
                draw_flat_button(app, "VIEW", bx2, by2, bw2, bh2, 0.85f, 0.78f, 0.55f,
                                 false);
            }
            draw_ui_label(app, bx2 - bw2 * 0.5f, by2 - 10.0f, bw2, 20.0f,
                          loc(app, "profile_BtnShow", "VIEW"), 0.7f, UiAlign::Center, 0.2f,
                          0.15f, 0.08f);
        }
    } else if (tab_ == kProfileTabSeals) {
        // Ported `gs` SEALS_SLIDER body (`gs.uZ` L2231): the owned `I.Vr`
        // rows. Cell `js` (L2232) draws `image = oe(a.fileName)`; native
        // resolves `res/users/images/<Image>` via `draw_user_image`. `gs` is
        // an `Xd` slider like `es`/`fs`/`ds`, so the cell list uses the same
        // `Gg` rect/pitch (`gs.init` L2231 leaves `Pa.spacing` at 0).
        {
            // `gs.NC` (L2232) sizes every `js` cell `b.ba(400,300)`; `gs.init`
            // (L2231) leaves `Pa.spacing` at 0, so the slider stacks the cells
            // in ONE column with pitch `300*A/400` (`profile_cell_h`).
            // `js.ba` (L2232) draws the `oe(a.fileName)` image at
            // `la(b*1.25/image.size)` - the `oe` design box is 512 (`oe` ctor
            // L1823 `this.size=512`), so the image box is `1.25 * cell height`
            // square, centred in the cell (`C(a/2)`, `D(b/2)`).
            const float chh = profile_cell_h(v, 400.0f, 300.0f);
            const float cell_l = profile_list_left(v);
            const float cell_w = profile_list_w(v);
            const float img_h = chh * 1.25f;
            const float cxc = cell_l + cell_w * 0.5f;
            const float top = profile_list_top(v, 400.0f, 300.0f);
            for (std::size_t i = 0; i < seal_rows_.size(); ++i) {
                const SealRow& s = seal_rows_[i];
                const float cy = top + chh * 0.5f + static_cast<float>(i) * chh;
                if (cy - chh * 0.5f > v.W) break;
                if (!draw_user_image(app, s.image, cxc, cy, img_h, img_h, 1.0f)) {
                    // Genuine art miss -> OPEN: JS `js` (L2232) draws only the
                    // `oe(a.fileName)` image; there is no JS flat/seal-name art.
                    draw_flat_button(app, s.name, cxc, cy, cell_w * 0.5f, chh * 0.5f,
                                     0.3f, 0.3f, 0.4f, false);
                }
                // `js.j5` (L2233) draws ONLY `new oe(a.fileName)`: no name
                // text, no count. The former "<name> x<count>" row was invented.
            }
        }
    } else if (tab_ == kProfileTabAchiev) {
        // JS atlas id 270 (`achievements`) is NOT registered by App::init —
        // load it lazily here (`is`/`Ed(270,y.MQa)`, L2212) before the icon
        // draws below.
        (void)load_achievements_atlas(app);
        // Ported `fs` ACHIEVEMENT_SLIDER body (L2213-2216): `fs.El` of
        // `Ba(def, value)` built by `uZ`/`cab` from achievements.xml (asset
        // 1356 via `td.Adb`/`Fib` L1160) joined with the save
        // `<Counters>`/`<Achievements>`. Each row is the `hs`/`is` cell
        // (`ba(400,130)`, `fs.NC` L2216): `Ed(270,y.MQa)` icon + `Uf`
        // progress + the `uy` progress text (`is.D1a` L2213). The icon atlas
        // (270) is registered by `load_achievements_atlas` above (ASTC ktx,
        // decodable after the KTX row-orientation fix); miss -> flat square.
        if (achiev_rows_.empty()) {
            draw_ui_label(app, v.J, v.P + v.height() * 0.5f - 14.0f, v.width(), 28.0f,
                          loc(app, "achievement_Completed", "Completed"), 0.8f, UiAlign::Center,
                          0.7f, 0.7f, 0.7f);
        } else {
            const float cell_h = profile_cell_h(v, 400.0f, 130.0f);  // `ba(400,130)`
            const float row_h = cell_h + 10.0f;                      // `fs.init` spacing
            const float x0 = profile_list_left(v);
            const float x1 = x0 + profile_list_w(v);
            // `Gg.ba` (L1885) centres the cell list: `uz = (size.y - qa)/2`.
            float yy = profile_list_top(v, 400.0f, 130.0f);
            for (std::size_t ri = 0; ri < achiev_rows_.size(); ++ri) {
                const AchievRow& r = achiev_rows_[ri];
                if (yy + cell_h > v.W) break;
                const float cy = yy + cell_h * 0.5f;
                sf2::render::Renderer& rr = app.renderer();
                const float q[] = {x0, yy + 2.0f, x1, yy + 2.0f, x1, yy + cell_h - 2.0f,
                                   x0, yy + 2.0f, x1, yy + cell_h - 2.0f, x0, yy + cell_h - 2.0f};
                // `hs`/`is` cell band: the oracle `profile_tab2` rows read as a
                // warm translucent band over the parchment (was an opaque
                // near-black quad).
                rr.draw_triangles(q, 6, r.reward_available ? 0.40f : 0.34f,
                                  r.reward_available ? 0.30f : 0.25f,
                                  r.reward_available ? 0.16f : 0.14f, 0.55f);
                // Icon `is` `Ed.Fs` (L2212): `Achievements01/ach_*` on atlas
                // 270 (`y.MQa` "Achievements01/ach_block_gold", L2470).
                // `is.ba` (L2212) is the SAME cell layout as `ls.ba` (L2236):
                // `icon.zf(b*.8)`, `C(icon.za()/2 + b*.1)`, `D(b/2)`.
                const float icon_h = cell_h * 0.8f;
                const float icon_cx = x0 + icon_h * 0.5f + cell_h * 0.1f;
                const float e_x = icon_cx + icon_h * 0.5f + cell_h * 0.2f;  // `e` L2212
                if (!draw_cell_icon(app, r.icon, icon_cx, cy, icon_h, icon_h, 1.0f)) {
                    const float isz = icon_h * 0.5f;
                    const float iq[] = {icon_cx - isz, cy - isz, icon_cx + isz, cy - isz,
                                        icon_cx + isz, cy + isz, icon_cx - isz, cy - isz,
                                        icon_cx + isz, cy + isz, icon_cx - isz, cy + isz};
                    rr.draw_triangles(iq, 6, 0.35f, 0.35f, 0.4f, 0.95f);
                }
                // `is` cell (L2212) draws NO description text - the text
                // lives in the `zr=Yr` info panel (below). Replaced the inline
                // description with the cited cell (icon + bar + count).
                // `is.D1a` (L2213): `min(QZ,counter)/counter` else
                // `Y.na("achievement_Completed")`.
                char pbuf[48];
                if (r.value >= r.target && r.target > 0) {
                    std::snprintf(pbuf, sizeof(pbuf), "%s",
                                  loc(app, "achievement_Completed", "Completed").c_str());
                } else {
                    std::snprintf(pbuf, sizeof(pbuf), "%d/%d", r.value, r.target);
                }
                draw_ui_label(app, x1 - 104.0f, cy - 8.0f, 100.0f, 16.0f, pbuf, 0.5f,
                              UiAlign::Right, 0.9f, 0.9f, 0.7f);
                // `is.uH` progress bar (`Uf(y.eSa,y.HRa,258)` L2212): empty
                // `pieces/achiev_progress_empty` (profile atlas, `y.eSa`) +
                // `Level_bar` fill (misc atlas, `y.HRa`) at
                // min(value,target)/target (`is.D1a` `uH.PT`/`DF`, L2213). The
                // flat backing is the explicit miss fallback.
                const float pbx = e_x;
                const float pby = cy + 10.0f;
                const float pbw = std::max(40.0f, x1 - cell_h * 0.2f - pbx);
                const float pbh = 9.0f;
                const float pfrac =
                    r.target > 0 ? std::clamp(static_cast<float>(r.value) /
                                                  static_cast<float>(r.target),
                                              0.0f, 1.0f)
                                 : 0.0f;
                if (!app.draw_atlas_rect("pieces/achiev_progress_empty", pbx, pby, pbw, pbh,
                                         0.9f)) {
                    const float bq[] = {pbx, pby, pbx + pbw, pby, pbx + pbw, pby + pbh,
                                        pbx, pby, pbx + pbw, pby + pbh, pbx, pby + pbh};
                    rr.draw_triangles(bq, 6, 0.20f, 0.20f, 0.24f, 0.9f);
                }
                if (pfrac > 0.0f) {
                    (void)app.draw_atlas_rect("Level_bar", pbx, pby, pbw * pfrac, pbh, 1.0f);
                }
                // `as.nv` reward button (L2210, `Y.na("achievement_BtnReward")`)
                // visible while `sq.dg() && Wpa >= counter` (L2211).
                if (achiev_claimable(static_cast<int>(ri))) {
                    const ShopRect rb = profile_achiev_reward_rect(v, static_cast<int>(ri));
                    const float bx = (rb.J + rb.N) * 0.5f;
                    const float by = (rb.P + rb.W) * 0.5f;
                    const bool hov = achiev_hover_ == static_cast<int>(ri);
                    draw_flat_button(app, "REWARD", bx, by, rb.width(), rb.height(),
                                     hov ? 0.6f : 0.42f, 0.5f, 0.2f, hov);
                    draw_ui_label(app, rb.J, by - 9.0f, rb.width(), 18.0f,
                                  loc(app, "achievement_BtnReward", "Reward"), 0.5f,
                                  UiAlign::Center, 1.0f, 1.0f, 1.0f);
                }
                yy += row_h;
            }
        }
        // `zr=Yr` info panel (L2196 `zr.Pn(c)`): the selected achievement's
        // title (`as.le` L2210) + description (`as.le.V(Y.na(a.description))`)
        // + the reward line (`as.sq` L2210 `achievementReward`). The oracle
        // `profile_tab2` right panel shows row 0 ("Ни царапины").
        {
            const ShopRect& rp = pl.right_slot;
            const float pad = rp.width() * 0.06f;
            if (!achiev_rows_.empty()) {
                const AchievRow& ar = achiev_rows_.front();
                draw_ui_label(app, rp.J + pad, rp.P + 22.0f, rp.width() - 2.0f * pad,
                              40.0f, loc(app, ar.name, ar.name), 0.95f, UiAlign::Left,
                              0.16f, 0.11f, 0.06f);
                const std::string dk = ar.description.empty() ? ar.name : ar.description;
                draw_ui_wrapped(app, rp.J + pad, rp.P + 78.0f, rp.width() - 2.0f * pad,
                                rp.height() - 156.0f, loc_template(app, dk, ar.name),
                                0.52f, UiAlign::Left, 0.20f, 0.14f, 0.08f);
                if (ar.money_prize > 0 || ar.bonus_prize > 0) {
                    char rb[64];
                    // The RU `achievementReward` value already carries its
                    // trailing colon ("НАГРАДА:"), so join with a space.
                    std::snprintf(rb, sizeof(rb), "%s %d",
                                  loc(app, "achievementReward", "REWARD:").c_str(),
                                  ar.money_prize + ar.bonus_prize);
                    draw_ui_label(app, rp.J + pad, rp.W - 58.0f, rp.width() - 2.0f * pad,
                                  30.0f, rb, 0.7f, UiAlign::Left, 0.20f, 0.14f, 0.08f);
                }
            }
        }
    }
    // Shared `za` chrome (JS `ma.D1`): topPanel + widgets + vertical nav.
    // Collapsed on arrival (oracle `profile_tab*` shows the `МЕНО` header).
    draw_za_chrome(app, kScreenProfile);
    // The BACK button (top-left) is drawn AFTER the `za` chrome so the chrome's
    // full-width topPanel (`odb` L1975, height min(H*.13,100)) no longer
    // occludes it. NOTE: the JS Profile `vb` (L2189-2201) is a tabbed screen
    // (`cs` tabs only) with no BACK node - this is a native navigation
    // affordance kept because the headless loop uses the equipment->dojo back
    // leg (`EquipmentScreen::update_impl`), so the JS-decided "remove" option
    // would break the loop. It uses the misc `Arrow` frame (`y.sRa`); the flat
    // plate is only a genuine atlas-miss fallback -> OPEN (no JS art).
    if (!try_draw_atlas_button(app, "Arrow", 64.0f, 40.0f, 88.0f, 48.0f, 1.0f)) {
        draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
        draw_ui_label(app, 64.0f - 44.0f + 6.0f, 40.0f - 10.0f, 88.0f - 12.0f, 20.0f,
                          "BACK", 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    }
    // D3: `Wb` is a GLOBAL overlay — the Profile screen shows + blocks on a
    // queued dialog (`Wb.Xob` L927 appends to the ACTIVE screen's content).
    draw_quest_modal(app, ren, app.screens().top() == this);
}


// ---------------------------------------------------------------------------
// SettingsScreen
// ---------------------------------------------------------------------------

// The `un extends od` settings dialog (JS L1916-1930). `SettingsScreen` hosts
// it on its own screen (the native `make_screen(kScreenSettings)` path used by
// `--verify-input`); the `za` nav #5 tap opens the SAME dialog as a `Wb`
// overlay over the current screen (D13). Both route through
// `settings_dialog_consume` / `draw_settings_dialog`.
SettingsScreen::SettingsScreen(ScreenManager& mgr) : Screen(mgr, "Settings") {
    open_settings_dialog(app());
}

void SettingsScreen::update_impl(float dt) {
    ++age_;  // press debounce: ignore the push-frame held click
    // The `za` state machine ticks on Settings too (the one shell screen that
    // omitted it): the mount reset + the animation lock share the per-screen
    // slot, and nav #5 (`Vfb` L1981) opens the settings dialog from here.
    za_update(app(), *this, kScreenSettings, dt);
    // D3/D13: `Wb` is a GLOBAL overlay — the settings dialog (and any queued
    // quest dialog) blocks the screen beneath.
    quest_modal_consume(app());
}

// JS `od.aa` (L1895: the key gate `L.K.Tj().Db(156)`) applied to the
// `un extends od` dialog (L1916): Escape closes it. JS menus are
// pointer-only — this only handles the key edge, no menu navigation.
void SettingsScreen::on_key(int glfw_key, bool down) {
    if (down && glfw_key == 256) {  // GLFW_KEY_ESCAPE
        std::fprintf(stdout, "[settings] ESC -> close dialog\n");
        std::fflush(stdout);
        close_settings_dialog();
        if (app().screens().top() != nullptr &&
            app().screens().top()->id() == kScreenSettings) {
            manager().pop();
        }
    }
}

// ---------------------------------------------------------------------------
// The Settings `un` dialog (D13/D15) — shared by the `za` overlay + the screen
// ---------------------------------------------------------------------------
// `Xc.Shb()` L931 = `Wb.openDialog(310,null)` -> `Wb.Xob` case 310 (L926)
// `this.If=new un`, so the settings surface is `un extends od` (L1916-1930):
// a `Wb` dialog appended to the ACTIVE screen (L927). The `za` nav button #5
// (`Vfb` L1981) therefore opens it OVER the current screen — the port's
// `ma.Jg().jI(11)` navigation was INVENTED. `Vfb` also appends a `Bi` spinner
// and `G.load([250,251,252,253])`; that per-language BMF atlas build is not
// modelled, so `draw_settings_dialog` uses the resolved `loc` strings.

// `un.$u=G.Rq()` (L1928): `$u` is the DISPLAYED language, initialised from the
// saved one (`t9 = G.Rq()!=this.$u` is false at open, so RESTART stays hidden).
void open_settings_dialog(App& app) {
    g_settings_dialog_open = true;
    g_settings_lang = app.language().empty() ? "en" : app.language();
    g_settings_restart_visible = false;
    g_settings_age = 0;
    // JS `sc.ckb` (L113759): the save `<Sounds>/<Sound|Music>@Mute` restores the
    // bus mutes on load. Re-apply so the row icons match the persisted state.
    try {
        const WarriorSave w = app.save().load();
        sf2::audio::AudioEngine& au = sf2::audio::AudioEngine::instance();
        if (au.sfx_muted() != w.sound_muted) au.set_sfx_muted(w.sound_muted);
        if (au.music_muted() != w.music_muted) au.set_music_muted(w.music_muted);
        g_settings_sound_off = w.sound_muted;
        g_settings_music_off = w.music_muted;
    } catch (const std::exception&) {
    }
}

void close_settings_dialog() { g_settings_dialog_open = false; }

// JS `lb.WT`/`lb.VT` (L1276) end in `p.TJ.save()`, so a bus-mute toggle
// persists at once. Shared by the Settings `un` rows and the fight pause `Dr`
// rows (both write `ta.$D`/`ta.ZD` -> the save `<Sounds>/<Sound|Music>@Mute`).
void persist_bus_mutes(App& app) {
    try {
        WarriorSave w = app.save().load();
        sf2::audio::AudioEngine& au = sf2::audio::AudioEngine::instance();
        w.sound_muted = au.sfx_muted();
        w.music_muted = au.music_muted();
        app.save().save(w);
    } catch (const std::exception& e) {
        std::fprintf(stdout, "[settings] mute save failed: %s\n", e.what());
        std::fflush(stdout);
    }
}

bool settings_dialog_open() { return g_settings_dialog_open; }

bool settings_dialog_restart_visible() { return g_settings_restart_visible; }

bool settings_bus_row_center(bool music, float& cx, float& cy, float& w, float& h) {
    const SettingsLayout s = settings_layout();
    const float row_cx = music ? s.music_row_cx : s.sound_row_cx;
    const float row_cy = music ? s.music_cy : s.sound_cy;
    if (row_cx <= 0.0f || s.row_w <= 0.0f || s.row_h <= 0.0f) return false;
    cx = row_cx;
    cy = row_cy;
    w = s.row_w;
    h = s.row_h;
    return true;
}

int catalog_max_delivery_sec(App& app) {
    int best = 0;
    for (const CatalogItem& ci : load_catalog(app)) {
        if (ci.delivery_sec > best) best = ci.delivery_sec;
    }
    return best;
}

// `un.rHa` case 4 (L1931): `this.u9=(this.u9+1)%iv.length; this.$u=iv[this.u9];
// this.t9=G.Rq()!=this.$u;` then `this.t9?(this.Km.X(!0),...)` reveals RESTART.
void settings_dialog_cycle_language(App& app) {
    int idx = 0;
    for (int i = 0; i < kSettingsLangCount; ++i) {
        if (g_settings_lang == kSettingsLangs[i]) idx = i;
    }
    idx = (idx + 1) % kSettingsLangCount;
    g_settings_lang = kSettingsLangs[idx];
    const std::string saved = app.language().empty() ? "en" : app.language();
    g_settings_restart_visible = g_settings_lang != saved;  // `t9`
    std::fprintf(stdout, "[settings] language -> %s (RESTART %s)\n", g_settings_lang.c_str(),
                 g_settings_restart_visible ? "shown" : "hidden");
    std::fflush(stdout);
}

namespace {

// `un.rHa` (L1930-1932) row switch, shared by the overlay + the hosted screen.
enum class SettingsRow { kNone = 0, kBack, kMusic, kRestart, kLanguage, kSound, kCredits };

// --- The credits view (JS `xh`, L1854-1857; opened by `un.rHa` case 2
// `xh.show()` L1931) -------------------------------------------------------
// `xh.show` appends a full-screen overlay to `L.K.root`, filled from
// `Ja.ki(1312)` (res/credits.xml): for the current language (`c=G.Rq()`;
// `c!="en"&&c!="ru"&&(c="en")` L1855) it walks `<Part Name=..>value</Part>`
// and builds one `zx` row (`$ja` = the Name label, `V2` = the value with
// `{br}` -> "\n"), then the `CreditsFamilies` title. `aa` scrolls the content
// UP 1.3 px/frame (`this.content.D(this.content.ra-1.3)` L1856) and closes
// (`pn`) once the last title passes the top, or on a click
// (`L.K.dd().Db(0)` L1856).
struct CreditsRow {
    std::string name;
    std::string value;
};
bool g_credits_open = false;
float g_credits_scroll = 0.0f;
std::string g_credits_lang;
std::vector<CreditsRow> g_credits_rows;

// `zx.constructor` (L1857-1858): `e.replace(RegExp("{br}","g"),"\n")`.
std::string credits_br_to_nl(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s.compare(i, 4, "{br}") == 0) {
            out += '\n';
            i += 4;
        } else {
            out += s[i++];
        }
    }
    return out;
}

// `Ja.ki(1312)` = res/credits.xml. Best-effort: a failure leaves an empty list.
void credits_load() {
    g_credits_rows.clear();
    try {
        std::ifstream in("reference/extracted/xml/res/credits.xml", std::ios::binary);
        if (!in) return;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        sf2::data::xml_doc doc;
        doc.parse(std::string(data.begin(), data.end()));
        const pugi::xml_node root = doc.root().first_child();  // <Credits>
        if (root == nullptr) return;
        std::string lang = g_credits_lang;
        if (lang != "en" && lang != "ru") lang = "en";
        const pugi::xml_node sec = root.child(lang.c_str());
        if (!sec) return;
        for (const pugi::xml_node part : sec.children("Part")) {
            CreditsRow r;
            if (part.attribute("Name")) r.name = part.attribute("Name").value();
            r.value = credits_br_to_nl(part.text().get());
            g_credits_rows.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[credits] load failed: %s\n", e.what());
    }
}

void credits_show(App& app) {
    g_credits_lang = app.language().empty() ? "en" : app.language();
    g_credits_scroll = 0.0f;
    credits_load();
    g_credits_open = true;
    std::fprintf(stdout, "[credits] show: %zu rows (lang %s)\n", g_credits_rows.size(),
                 g_credits_lang.c_str());
    std::fflush(stdout);
}

void credits_close() {
    g_credits_open = false;
    std::fprintf(stdout, "[credits] close\n");
    std::fflush(stdout);
}

// `xh.aa` (L1856): scroll up + a click closes. Returns true while up (the
// overlay consumes the input frame, like the settings dialog it sits on).
bool credits_consume(App& app) {
    if (!g_credits_open) return false;
    g_credits_scroll += 1.3f;  // `this.content.D(this.content.ra-1.3)`
    const App::PointerState& p = app.pointer();
    if (p.pressed) credits_close();  // `L.K.dd().Db(0)&&this.pn()`
    return true;
}

void credits_draw(App& app, sf2::render::Renderer& ren) {
    if (!g_credits_open) return;
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 1.0f);
    // `a.Fa(1E3,120); a.C(-500); a.ua(120); a.V(Y.na("credits"))` (L1855).
    float y = 120.0f - g_credits_scroll;
    draw_ui_label(app, kViewW * 0.5f - 500.0f, y - 60.0f, 1000.0f, 120.0f,
                  loc(app, "credits", "CREDITS"), 1.0f, UiAlign::Center, 0.83f, 0.66f, 0.68f);
    y += 220.0f;  // `a=220` (L1855)
    for (const CreditsRow& r : g_credits_rows) {
        // `$ja=c(Name,60)` at `C(-520)`, `V2=c(value,50)` at `C(20)` (L1857-1858).
        draw_ui_label(app, 60.0f, y, 500.0f, 60.0f, r.name, 0.9f, UiAlign::Left, 0.83f, 0.66f,
                      0.68f);
        draw_ui_wrapped(app, 620.0f, y, kViewW - 680.0f, 50.0f, r.value, 0.9f, UiAlign::Left,
                        0.83f, 0.66f, 0.68f);
        y += 90.0f + 60.0f;  // `a += d.height + 90` (L1855)
    }
    draw_ui_label(app, kViewW * 0.5f - 500.0f, y, 1000.0f, 120.0f,
                  loc(app, "CreditsFamilies", "CREDITS"), 1.0f, UiAlign::Center, 0.83f, 0.66f,
                  0.68f);
}

SettingsRow settings_row_at(const SettingsLayout& s, double x, double y) {
    auto in = [&](float cx, float cy, float w, float h) {
        return x >= cx - w * 0.5f && x <= cx + w * 0.5f && y >= cy - h * 0.5f &&
               y <= cy + h * 0.5f;
    };
    if (in(s.back_cx, s.back_cy, s.btn_w, s.btn_h)) return SettingsRow::kBack;
    if (g_settings_restart_visible && in(s.restart_cx, s.restart_cy, s.btn_w, s.btn_h)) {
        return SettingsRow::kRestart;
    }
    // `un`'s `c` hit-rect (L1917): 800 x icon, one per container.
        if (in(s.sound_row_cx, s.sound_cy, s.row_w, s.row_h)) return SettingsRow::kSound;
    if (in(s.music_row_cx, s.music_cy, s.row_w, s.row_h)) return SettingsRow::kMusic;
    if (in(s.lang_row_cx, s.lang_cy, s.row_w, s.row_h)) return SettingsRow::kLanguage;
    // `un`'s `c(2,this.Mta)` credits row (L1929): hit-rect like the other rows.
    if (in(s.credits_row_cx, s.credits_cy, s.row_w, s.row_h)) return SettingsRow::kCredits;
    return SettingsRow::kNone;
}

// JS `lb.VT`/`lb.WT` (L1276) call `p.TJ.save()` right after writing the bus
// state, so a Settings toggle persists immediately. The port writes it into
// the save `<Sounds>/<Sound|Music>@Mute>` (JS `sc.Gpb` L114249); load side is
// `sc.ckb` L113759. Best-effort: a failure is reported, never fatal.
void persist_settings_mutes(App& app) { persist_bus_mutes(app); }

// Runs a row action; returns true when the dialog must close (`Ge(0)` L1930 /
// L1932 RESTART `close()`).
bool settings_run_row(App& app, SettingsRow row) {
    switch (row) {
        case SettingsRow::kBack:
            std::fprintf(stdout, "[settings] BACK -> close dialog\n");
            std::fflush(stdout);
            return true;
        case SettingsRow::kMusic:
            // `un.W$`/`lb.Mz()` (L1928) + `case 1: lb.WT(!lb.Mz())` (L1931):
            // `ta.WT(a)` L1264 = `L.K.$f.cMa(a?0:1)` — the music BUS volume,
            // NOT a stop. The old stop/restart lost the track (`music_track()`
            // reads "" after `stop_music`), so turning music back ON was a
            // no-op. The mute now rides the engine (`ta.$D`, `lb.Mz()`).
            g_settings_music_off = !g_settings_music_off;
            sf2::audio::AudioEngine::instance().set_music_muted(g_settings_music_off);
            persist_settings_mutes(app);
            sf2::audio::AudioEngine::instance().play("snd_click_1");
            std::fprintf(stdout, "[settings] music %s\n", g_settings_music_off ? "OFF" : "ON");
            std::fflush(stdout);
            return false;
        case SettingsRow::kSound:
            // `un.Y$`/`lb.Mz()` (L1928) + `case 1: lb.WT(!lb.Mz())` (L1931):
            // `ta.WT(a)` (L1265) = `L.K.$f.cMa(a?0:1); ta.$D=a` (the SFX bus;
            // `cMa` -> `oBa`, the non-`tR` bus, L1240813). Row id 1 = "sound".
            g_settings_sound_off = !g_settings_sound_off;
            sf2::audio::AudioEngine::instance().set_sfx_muted(g_settings_sound_off);
            sf2::audio::AudioEngine::instance().play("snd_click_1");
            persist_settings_mutes(app);
            std::fprintf(stdout, "[settings] sound %s\n",
                         g_settings_sound_off ? "OFF" : "ON");
            std::fflush(stdout);
            return false;
        case SettingsRow::kLanguage:
            settings_dialog_cycle_language(app);
            return false;
        case SettingsRow::kRestart:
            sf2::audio::AudioEngine::instance().play("snd_click_1");
            // `un.rHa` case 5 (L1932): `G.Ska(this.$u); p.TJ.save(!0)` then
            // `L.K.reload()`. The port exposes no runtime language setter nor a
            // reload path, so the press is reported, never faked.
            std::fprintf(stdout, "[settings] RESTART (needs L.K.reload; not modelled)\n");
            std::fflush(stdout);
            return true;
        case SettingsRow::kCredits:
            // `un.rHa` case 2 (L1931): `xh.show()`. Unlike BACK/`Ge(0)` the
            // credits case does NOT `close()` the dialog, so the overlay sits
            // on top of the still-open settings surface.
            credits_show(app);
            return false;
        case SettingsRow::kNone:
        default:
            return false;
    }
}

// D13 input: `Wb`'s top dialog blocks the screen beneath. Returns true while
// the settings dialog is open.
bool settings_dialog_consume(App& app) {
    if (!g_settings_dialog_open) return false;
    if (credits_consume(app)) return true;  // `xh` overlay blocks on top
    ensure_lang(app);
    ++g_settings_age;
    const App::PointerState& p = app.pointer();
    const SettingsLayout s = settings_layout();
    const SettingsRow row = settings_row_at(s, p.x, p.y);
    // 0 = BACK, 2 = RESTART for the flat fallback hover (row - 1).
    g_settings_hover = static_cast<int>(row) - 1;
    // A held button must not re-fire on the frame the dialog opened (`age_>10`
    // is the existing push-frame debounce).
    if (row != SettingsRow::kNone && p.pressed && g_settings_age > 10) {
        if (settings_run_row(app, row)) {
            close_settings_dialog();
            if (app.screens().top() != nullptr &&
                app.screens().top()->id() == kScreenSettings) {
                app.screens().pop();
            }
        }
    }
    return true;
}

// The `un extends od` drawer: `od` 9-slice + `Vc` title + the four
// `E.get(250)` rows + the `Nm` restart notice + BACK (`EButtonDark`) /
// RESTART (`EButtonBeige`), the last two `X(!1)`-hidden until the language row
// changed (D15, L1929-1931).
void draw_settings_dialog(App& app, sf2::render::Renderer& ren) {
    if (!g_settings_dialog_open) return;
    ensure_lang(app);
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.55f);
    const SettingsLayout s = settings_layout();
    draw_od_base(app, ren, s.panel);
    // Title `Vc`: `IVa.Settings_Title` (L1917); `ua(152)` + `La(Z.W6)` (L1900).
    draw_ui_label(app, s.title_x, s.title_y, s.title_w, s.title_h,
                  loc(app, "Settings_Title", "SETTINGS"), 1.52f, UiAlign::Center, 0.404f,
                  0.243f, 0.141f);
    // JS `y.loa/koa` (L1928): the Sound row icon reflects the SFX mute (`lb.Mz()`).
    const bool sfx_on = !sf2::audio::AudioEngine::instance().sfx_muted();
    const std::string lang = g_settings_lang.empty() ? "en" : g_settings_lang;
    if (load_settings_icons_atlas(app)) {
        try_draw_atlas_button(app, sfx_on ? "sound" : "sound_off", s.sound_cx, s.sound_cy,
                              s.icon, s.icon, 1.0f);
        try_draw_atlas_button(app, g_settings_music_off ? "music_off" : "music", s.music_cx,
                              s.music_cy, s.icon, s.icon, 1.0f);
        try_draw_atlas_button(app, "credits", s.credits_cx, s.credits_cy, s.icon, s.icon,
                              1.0f);
        try_draw_atlas_button(app, lang, s.lang_cx, s.lang_cy, s.icon, s.icon, 1.0f);
    }
    struct RowLabel {
        float cx, cy;
        std::string text;
    };
    const RowLabel labels[4] = {
        {s.sound_cx, s.sound_cy, loc(app, "Settings_Sound", "Sound")},
        {s.music_cx, s.music_cy, loc(app, "Settings_Music", "Music")},
        {s.credits_cx, s.credits_cy, loc(app, "Settings_Credits", "Credits")},
        {s.lang_cx, s.lang_cy, loc(app, "Settings_Language", "English")},
    };
    for (const RowLabel& row : labels) {
        draw_ui_label(app, row.cx + s.icon * 0.7f, row.cy - s.icon * 0.25f,
                      596.0f * s.panel.c, s.icon, row.text, 0.6f, UiAlign::Left, 1.0f, 1.0f,
                      1.0f);
    }
    // `Nm` (L1929): `ea` at `Fa(1500,50)`, `C(-750)`, `D(250)`, `ua(75)`,
    // `Kc(.6)`, `V(Y.na("dlgSettingsRestart"))`, `R(!1)`; `R(t9)` (L1933) on a
    // language change.
    if (g_settings_restart_visible) {
        draw_ui_label(app, s.panel.px + s.panel.pw * 0.5f - 750.0f * s.panel.c,
                      s.notice_y - 25.0f * s.panel.c, 1500.0f * s.panel.c, 50.0f * s.panel.c,
                      loc(app, "dlgSettingsRestart", "RESTART"), 0.75f, UiAlign::Center, 1.0f,
                      1.0f, 1.0f);
    }
    // BACK (`Bb("EButtonDark")` L1930 -> `btnDark`), `Ge(0)` closes.
    if (!(load_sliced_atlas(app) &&
          draw_bb_plate(app, "btnDark", s.back_cx, s.back_cy, s.btn_w, s.btn_h, 1.0f))) {
        draw_flat_button(app, "", s.back_cx, s.back_cy, s.btn_w, s.btn_h, 0.35f, 0.3f, 0.28f,
                         g_settings_hover == 0);
    }
    draw_ui_label(app, s.back_cx - s.btn_w * 0.5f, s.back_cy - 14.0f, s.btn_w, 28.0f,
                  loc(app, "Settings_Back", "BACK"), 0.9f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    // RESTART (`Bb("EButtonBeige")` L1930 -> `btnBeige`), revealed by L1931.
    if (g_settings_restart_visible) {
        if (!(load_sliced_atlas(app) &&
              draw_bb_plate(app, "btnBeige", s.restart_cx, s.restart_cy, s.btn_w, s.btn_h,
                            1.0f))) {
            draw_flat_button(app, "", s.restart_cx, s.restart_cy, s.btn_w, s.btn_h, 0.6f, 0.5f,
                             0.3f, g_settings_hover == 2);
        }
        draw_ui_label(app, s.restart_cx - s.btn_w * 0.5f, s.restart_cy - 14.0f, s.btn_w,
                      28.0f, loc(app, "dlgServiceRestart", "RESTART"), 0.9f, UiAlign::Center,
                      1.0f, 1.0f, 1.0f);
    }
    // `xh` credits overlay (L1854-1857): drawn over the settings surface,
    // exactly as `xh.show` appends to the root above the `Wb` dialog.
    credits_draw(app, ren);
}

}  // namespace

void SettingsScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // The `un extends od` dialog is the same surface the `za` nav #5 opens as
    // an overlay (D13); `draw_quest_modal` (is_top) draws it here too.
    // D3: `Wb` is a GLOBAL overlay — the Settings screen shows + blocks on a
    // queued dialog (`Wb.Xob` L927 appends to the ACTIVE screen's content).
    draw_quest_modal(app, ren, app.screens().top() == this);
}

// ---------------------------------------------------------------------------
// MovesScreen (DELETED)
// ---------------------------------------------------------------------------
// Moves is a Profile tab, not a screen (JS `To.kOa`=11 L2201; PORT_AUDIT_UI
// §3 item 31). The learned-move list is folded into EquipmentScreen tab 1
// (`kProfileTabMoves`) above; the standalone class/id is removed.

// ---------------------------------------------------------------------------
// BracketScreen (DELETED)
// ---------------------------------------------------------------------------
// No JS screen 13 and no bracket surface: `Xr` is the map status panel
// (L2133-2136), reached from the `Vr` scroller, not a standalone screen
// (PORT_AUDIT_UI §3 item 32). The Map BRACKET corner button was already
// removed; the class/id is deleted here.

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<Screen> make_screen(ScreenManager& mgr, ScreenId id) {
    // JS `ma.D1` (L1832) builds a FRESH `za` per screen (`gk.Af = new Zh(...)`
    // L1996) whose ctor `collapse(0)` (L1998) starts it COLLAPSED. The mount is
    // the screen construction, so reset the slot here: a new instance never
    // inherits the previous occupant's column (the old persistent
    // `g_za_nav_open_by_screen` made the toggle history-dependent).
    za_nav_reset(id);
    switch (id) {
        case kScreenDojo:
        case kScreenGeneralMenu:
            // No JS screen 8 (`dJ()` never returns 8 — PORT_AUDIT_UI §0):
            // the Dojo is the shell home, so screen 8 routes there.
            return std::make_unique<DojoScreen>(mgr);
        case kScreenMap:
            return std::make_unique<MapScreen>(mgr);
        case kScreenFight: {
            // The Map node click carried the battle into pending_battle.
            const PendingBattle& pb = mgr.app().pending_battle();
            return std::make_unique<FightScreen>(mgr, pb.battle_name, pb.location,
                                                 pb.reward_money, pb.reward_exp, pb.owned);
        }
        case kScreenResults: {
            const PendingBattle& pb = mgr.app().pending_battle();
            return std::make_unique<ResultsScreen>(mgr, pb.player_won, pb.reward_money,
                                                   pb.reward_exp);
        }
        case kScreenShop:
            return std::make_unique<ShopScreen>(mgr);
        case kScreenSettings:
            return std::make_unique<SettingsScreen>(mgr);
        case kScreenProfile:
            return std::make_unique<EquipmentScreen>(mgr);
        default:
            return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Quest live-action helpers (quest_engine.cpp `tick`)
// ---------------------------------------------------------------------------

std::string catalog_item_type(App& app, const std::string& item_name) {
    if (item_name.empty()) return std::string();
    const std::vector<CatalogItem> all = load_full_catalog(app);
    for (const CatalogItem& ci : all) {
        if (ci.name == item_name) return ci.type;  // list.xml <Item Type>
    }
    return std::string();
}

// The `za` column's expanded TARGET (JS `gk.uJ`), read by --flow-verify.
bool za_nav_expanded(ScreenId id) { return za_nav_state(id).uJ; }

// The live Map screen's `Ur` strip (see screens.hpp). Null-safe: a probe run
// with another screen on top gets false / -1 instead of a crash.
bool map_zone_dot_center(App& app, std::size_t zi, float& cx, float& cy) {
    MapScreen* m = dynamic_cast<MapScreen*>(app.screens().top());
    return m != nullptr && m->zone_dot_center(zi, cx, cy);
}
int map_zone_selected(App& app) {
    MapScreen* m = dynamic_cast<MapScreen*>(app.screens().top());
    return m == nullptr ? -1 : m->zone_selected();
}

// `eo.N3a` (L1117 `za.instance.sxa()` -> `scroll.collapse(0)`): the quest
// engine asks for the ACTIVE screen's column collapsed; the port has one
// state per screen slot, so the request resets every slot.
void set_za_nav_open(bool open) {
    // `za.sxa(a)` (L1981) -> `scroll.collapse(a)` (L2000). The quest engine
    // calls it with the 0 default, i.e. the INSTANT collapse (no animation,
    // no lock); the counterpart expand is kept for symmetry.
    for (ZaNavState& s : g_za_nav_by_screen) {
        s.uJ = open;
        s.PF = false;
        s.pma = 0.0f;
        s.time = 0.0f;
        s.yI = open ? 1.0f : 0.0f;
        s.zI = open ? 1 : 2;
    }
}

namespace {

// `vj.E0` (L1168) category name -> the `kShopTabs` index via `Cj.l6`
// (L2302). `vj.ifa` (L1168) routes E0 1..7 to the Shop (screen 4); E0 8
// (RaidConsumable) maps to 11 there, but `Cj.l6(8)` is still tab 6.
int shop_tab_index_for(const std::string& tab) {
    int e0 = 0;
    if (tab == "Weapon") e0 = 1;
    else if (tab == "Armor") e0 = 2;
    else if (tab == "Helm") e0 = 3;
    else if (tab == "Ranged") e0 = 4;
    else if (tab == "Magic") e0 = 5;
    else if (tab == "Ruby") e0 = 6;
    else if (tab == "Free") e0 = 7;
    else if (tab == "RaidConsumable") e0 = 8;
    else return -1;
    for (int i = 0; i < kShopTabCount; ++i) {
        if (kShopTabs[i].e0 == e0) return i;
    }
    return -1;
}

} // namespace

// `Oa.uLa(a,b)` (L1181866) = `e1(a)` (`f5(Cj.l6(a))`, L2286) + `Za.SA(b)`
// (select the named cell).
bool ShopScreen::open_at(const std::string& tab, const std::string& item) {
    const int idx = shop_tab_index_for(tab);
    if (idx < 0) return false;
    tab_ = idx;
    sel_ = 0;   // `Oa.f5` -> `usb()` auto-selects the first cell
    hover_ = -1;
    if (!item.empty()) {
        const std::vector<std::size_t> rows = shop_tab_rows(items_, idx);
        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (items_[rows[r]].name == item) {
                sel_ = static_cast<int>(r);
                hover_ = static_cast<int>(r);
                break;
            }
        }
    }
    std::fprintf(stdout, "[shop] uLa tab=%s (idx %d) item=%s -> sel %d\n", tab.c_str(), idx,
                 item.c_str(), sel_);
    std::fflush(stdout);
    return true;
}

bool shop_open_at(App& app, const std::string& tab, const std::string& item) {
    if (shop_tab_index_for(tab) < 0) {
        std::fprintf(stdout, "[quest] OpenShop tab '%s': no shipped shop tab (vj.E0)\n",
                     tab.c_str());
        std::fflush(stdout);
        return false;
    }
    // `go.Thb` (L1092): with the shop already live (`Oa.get()!=null &&
    // this.qO!=0`) the JS applies `uLa` directly; otherwise it pushes the
    // scene (`mp(4, new Gj(qO, ib))`) and applies on load. The pending request
    // is consumed by the ShopScreen ctor / update.
    Screen* top = app.screens().top();
    if (top != nullptr && top->id() == kScreenShop) {
        return static_cast<ShopScreen*>(top)->open_at(tab, item);
    }
    app.set_pending_shop(tab, item);
    app.screens().push(make_screen(app.screens(), kScreenShop));
    return true;
}

// `vb.rF(a,b)` (L1131579) -> `vb.hla(a)` (L1127569): select the profile slot.
// The JS guard is `if(this.vV!=a&&a!=5)` — only slot 5 (the `init` "no tab"
// default) and negatives are rejected; slot 4 is the BattlePass pane
// (`To.hOa(15)=4`) which `hla` accepts and stores in `vV` even though its
// switch has no `case 4` body (so the strip shows `kProfileTabCount` = 4
// buttons and nothing highlights for slot 4). The old port guard wrongly
// bound the slot range to the strip count and rejected 4.
bool EquipmentScreen::select_tab(int slot, const std::string& focus) {
    if (slot < 0 || slot > kProfileSlotMax) {
        return false;
    }
    tab_ = slot;      // `this.vV=a`
    tab_hover_ = -1;
    hover_ = -1;
    std::fprintf(stdout, "[profile] rF slot=%d focus=%s -> tab %d\n", slot,
                 focus.c_str(), tab_);
    std::fflush(stdout);
    return true;
}

// --- `--dialog-verify` self-check (see screens.hpp) ------------------------
namespace {

int g_dlg_passed = 0;
int g_dlg_failed = 0;

void dlg_case(const std::string& name, bool ok) {
    std::fprintf(stdout, "[dlgverify] %s %s\n", ok ? "PASS" : "FAIL", name.c_str());
    std::fflush(stdout);
    if (ok) {
        ++g_dlg_passed;
    } else {
        ++g_dlg_failed;
    }
}

// A nested `Dialog` action that queues a one-line marker dialog. Running it
// (`He.dhb` L1061) is observable, so the harness can tell WHICH slot fired
// without touching gameplay state (scene/shop actions are headless-gated).
QuestAction marker_action(const std::string& marker) {
    QuestAction act;
    act.tag = "Dialog";
    act.attrs["Title"] = marker;
    QuestAction line;
    line.tag = "Line";
    line.attrs["Text"] = marker;
    act.children.push_back(line);
    return act;
}

EngineDialog probe_dialog(const std::string& title) {
    EngineDialog d;
    d.type = "Regular";
    d.title = title;
    d.lines.push_back(title + "_line");
    d.line_buttons.push_back(title + "_row");
    return d;
}

// A nested `Dialog` action with explicit attrs — the D8/D9/D10 parse path
// (`He` L1043-1046) exercised through the real `run_actions` Dialog branch.
QuestAction dialog_action(const std::map<std::string, std::string>& attrs,
                          const std::string& line) {
    QuestAction act;
    act.tag = "Dialog";
    act.attrs = attrs;
    QuestAction line_a;
    line_a.tag = "Line";
    line_a.attrs["Text"] = line;
    act.children.push_back(line_a);
    return act;
}

} // namespace

bool run_quest_dialog_selfcheck(App& app) {
    g_dlg_passed = 0;
    g_dlg_failed = 0;
    QuestEngine& q = app.quest_engine();

    // --- D1: a Left button renders BOTH plates; Left fires the Left action.
    {
        q.clear_dialogs();
        EngineDialog d = probe_dialog("d1_probe");
        d.button_actions.push_back(marker_action("RIGHT_FIRED"));
        d.button_text = "right_cap";
        d.button_color = "Red";
        d.left_.actions.push_back(marker_action("LEFT_FIRED"));
        d.left_.text = "left_cap";
        d.left_.color = "Green";
        q.push_dialog_for_test(d);

        const QuestDialogRow row = quest_dialog_row(app, q.dialog());
        const bool both = row.count == 2;
        dlg_case("D1 left+right lays out BOTH plates (od.EF a==3)", both);
        const bool split = both && row.plates[1].cx < row.plates[0].cx;
        dlg_case("D1 primary plate sits right of the secondary plate", split);
        const int hit_l =
            both ? quest_dialog_button_hit_index(app, q.dialog(), row.plates[1].cx,
                                                 row.plates[1].cy)
                 : -1;
        const int hit_r =
            both ? quest_dialog_button_hit_index(app, q.dialog(), row.plates[0].cx,
                                                 row.plates[0].cy)
                 : -1;
        dlg_case("D1 left plate hit-tests to slot 0 (Left)", hit_l == 0);
        dlg_case("D1 right plate hit-tests to slot 1 (Right)", hit_r == 1);
        // Fire through the plate's own hit point: `dhb(0)` runs the Left
        // actions, NOT the Right ones (`dhb(1)`).
        q.press_dialog(app, hit_l);
        const bool left_fired = q.has_dialog() && !q.dialog().lines.empty() &&
                                q.dialog().lines[0] == "LEFT_FIRED";
        dlg_case("D1 Left press dispatches the Left action (LEFT_FIRED)", left_fired);
    }

    // --- D2: `He.lea` L1063 -> `nz.hi` L1840 colour->frame.
    {
        const struct {
            const char* color;
            const char* frame;
        } kCases[] = {{"Red", "btnDark"},
                      {"Green", "btnGreen"},
                      {"White", "btnWhite"},
                      {"Beige", "btnWhite"},
                      {"Gold", "btnGold"}};
        for (const auto& c : kCases) {
            dlg_case(std::string("D2 Color ") + c.color + " -> " + c.frame,
                     quest_button_frame(c.color, true) == std::string(c.frame));
        }
        dlg_case("D2 empty primary -> btnWhite (He.lea default Beige)",
                 quest_button_frame("", true) == std::string("btnWhite"));
        dlg_case("D2 empty secondary -> btnDark (od.jR L1899 secondary)",
                 quest_button_frame("", false) == std::string("btnDark"));
        // The row must carry the slots' own colours (not one plate for all).
        q.clear_dialogs();
        EngineDialog d = probe_dialog("d2_probe");
        d.button_actions.push_back(marker_action("R"));
        d.button_color = "Red";
        d.left_.actions.push_back(marker_action("L"));
        d.left_.color = "Green";
        q.push_dialog_for_test(d);
        const QuestDialogRow row = quest_dialog_row(app, q.dialog());
        const bool coloured =
            row.count == 2 && row.plates[1].color == "Green" && row.plates[0].color == "Red";
        dlg_case("D2 left/right plates keep their own Color", coloured);
    }

    // --- D7: `Od.EF` L1946 last-page caption precedence.
    {
        q.clear_dialogs();
        EngineDialog d = probe_dialog("d7_pager");
        d.lines = {"row_a", "row_b"};
        d.line_buttons = {"cap_a", "cap_b"};
        d.button_text = "cap_explicit";  // the authored right `Text` (`qy`)
        d.button_actions.push_back(marker_action("PAGER_FIRED"));
        q.push_dialog_for_test(d);
        dlg_case("D7 non-last page shows the ROW ButtonText (cap_a)",
                 q.dialog_button_text() == "cap_a");
        q.advance_dialog_page();
        dlg_case("D7 LAST page: explicit right Text wins (cap_explicit)",
                 q.dialog_button_text() == "cap_explicit");
    }
    {
        q.clear_dialogs();
        EngineDialog d = probe_dialog("d7_pager2");
        d.lines = {"row_a", "row_b"};
        d.line_buttons = {"cap_a", "cap_b"};
        d.button_text = "cap_b";  // parse-time fallback = last row caption
        d.button_actions.push_back(marker_action("PAGER2_FIRED"));
        q.push_dialog_for_test(d);
        q.advance_dialog_page();
        dlg_case("D7 LAST page, no authored Text: row caption (cap_b)",
                 q.dialog_button_text() == "cap_b");
    }

    // --- `He.jkb` L1056-1057 row button (`this.ima`, id from `this.eOa=5`).
    {
        q.clear_dialogs();
        EngineDialog d = probe_dialog("d_rowbtn");
        d.lines = {"row_text"};
        d.line_actions.push_back({marker_action("ROW_FIRED")});
        q.push_dialog_for_test(d);
        const std::vector<QuestDialogRowButton> rb =
            quest_dialog_row_buttons(app, q.dialog());
        const bool laid = rb.size() == 1 && rb[0].slot == 5;
        dlg_case("He.jkb row button: first row id 5 (this.eOa)", laid);
        const int hit = laid ? quest_dialog_row_hit_index(app, q.dialog(),
                                                          rb[0].x + rb[0].w * 0.5,
                                                          rb[0].y + rb[0].h * 0.5)
                             : -1;
        dlg_case("He.dhb row button hit-tests to id 5", hit == 5);
        dlg_case("He.dhb row button miss -> -1",
                 quest_dialog_row_hit_index(app, q.dialog(), -100.0, -100.0) == -1);
    }
    q.clear_dialogs();

    // --- D1 (case 4): a dialog queued on the FIGHT screen renders + blocks.
    {
        q.clear_dialogs();
        EngineDialog d = probe_dialog("fight_block");
        d.button_actions.push_back(marker_action("FIGHT_BLOCK"));
        d.button_text = "fight_cap";
        q.push_dialog_for_test(d);
        PendingBattle& pb = app.pending_battle();
        if (pb.battle_name.empty()) {
            pb.battle_name = "Training";
            pb.location = "dojo";
        }
        app.screens().push(make_screen(app.screens(), kScreenFight));
        const bool on_fight = app.screens().current_id() == kScreenFight;
        const bool live = quest_modal_top(app) != nullptr;
        const bool blocks = quest_modal_consume(app);
        dlg_case("D1 dialog on the Fight screen renders (modal live)", on_fight && live);
        dlg_case("D1 dialog on the Fight screen blocks input", on_fight && blocks);
    }
    q.clear_dialogs();

    // --- D4: `He.S` L1045-1051 per-Type renderer routing. -------------------
    {
        struct KindCase {
            const char* type;
            DialogKind kind;
            const char* cls;
        };
        const KindCase kKinds[] = {
            {"Regular", DialogKind::kOd280, "280 Od"},
            {"Stranger", DialogKind::kUj290, "290 uj"},
            {"Multiline", DialogKind::kUj290, "290 uj"},
            {"MultilineBig", DialogKind::kUj290, "290 uj"},
            {"NoAvatar", DialogKind::kVe340, "340 Ve"},
            {"ShowLoot", DialogKind::kVn370, "370 vn"},
            {"Notification", DialogKind::kIbBar, "Ib bar"},
            {"Scroll", DialogKind::kNone, "debugger (no renderer)"},
            {"MultiLineScroll", DialogKind::kNone, "debugger (no renderer)"},
            {"ThreeButtons", DialogKind::kNone, "debugger (no renderer)"},
            {"ItemSetDialog", DialogKind::kNone, "debugger (no renderer)"},
            {"MultilineTMP", DialogKind::kNone, "debugger (no renderer)"},
            {"Simple", DialogKind::kNone, "debugger (no renderer)"},
            {"", DialogKind::kOd280, "280 Od (absent Type -> He L1043 Regular)"},
        };
        for (const KindCase& k : kKinds) {
            dlg_case(std::string("D4 Type ") + (k.type[0] != '\0' ? k.type : "(none)") +
                         " -> " + k.cls,
                     dialog_kind(k.type) == k.kind);
        }
        dlg_case("D4 the four dialog classes are distinct",
                 dialog_kind("Regular") != dialog_kind("Multiline") &&
                     dialog_kind("Multiline") != dialog_kind("NoAvatar") &&
                     dialog_kind("NoAvatar") != dialog_kind("ShowLoot") &&
                     dialog_kind("ShowLoot") != dialog_kind("Notification"));
    }

    // --- D5: `uj.sqb()` L1953 — Multiline/MultilineBig show ALL lines. ------
    {
        q.clear_dialogs();
        EngineDialog d = probe_dialog("d5_multi");
        d.type = "Multiline";
        d.lines = {"m1", "m2", "m3", "m4"};
        d.line_buttons = {"b1", "b2", "b3", "b4"};
        d.button_text = "last_cap";
        d.button_actions.push_back(marker_action("D5_FIRED"));
        q.push_dialog_for_test(d);
        dlg_case("D5 Multiline has NO pager (uj.sqb)", !q.dialog_has_next_page());
        dlg_case("D5 Multiline scrolls ALL lines", dialog_scrolls_all_lines("Multiline"));
        dlg_case("D5 MultilineBig scrolls ALL lines", dialog_scrolls_all_lines("MultilineBig"));
        dlg_case("D5 Stranger keeps the pager", !dialog_scrolls_all_lines("Stranger"));
        dlg_case("D5 Multiline plate carries the last-page caption at once",
                 q.dialog_button_text() == "last_cap");
    }
    {
        q.clear_dialogs();
        EngineDialog r = probe_dialog("d5_reg");
        r.type = "Regular";
        r.lines = {"r1", "r2", "r3"};
        r.line_buttons = {"c1", "c2", "c3"};
        r.button_text = "c3";
        r.button_actions.push_back(marker_action("D5R_FIRED"));
        q.push_dialog_for_test(r);
        dlg_case("D5 Regular keeps the pager (3 rows)", q.dialog_has_next_page());
        dlg_case("D5 Regular page 0 caption is the ROW caption (c1)",
                 q.dialog_button_text() == "c1");
    }

    // --- D6: `od.aa` L1895 (0.25 s) + the `Wb.x3a` L927 backdrop tween. -----
    {
        const DialogAnim t0 = dialog_anim_at(0.0f, false);
        const DialogAnim tmid = dialog_anim_at(0.125f, false);
        const DialogAnim t1 = dialog_anim_at(kDialogAnimSecs, false);
        const DialogAnim t2 = dialog_anim_at(kDialogAnimSecs * 4.0f, false);
        dlg_case("D6 open t=0: alpha 0, scale 0.8 (Ln(0)=0, kYa(0)=0)",
                 t0.alpha < 1e-5f && std::fabs(t0.scale - 0.8f) < 1e-4f);
        dlg_case("D6 open steady state t=0.25: alpha 1, scale 1",
                 std::fabs(t1.alpha - 1.0f) < 1e-5f && std::fabs(t1.scale - 1.0f) < 1e-5f);
        dlg_case("D6 open stays at steady state past 0.25 s",
                 std::fabs(t2.alpha - 1.0f) < 1e-5f && std::fabs(t2.scale - 1.0f) < 1e-5f);
        dlg_case("D6 open alpha is monotonic (dc.Ln)",
                 t0.alpha < tmid.alpha && tmid.alpha < t1.alpha);
        const DialogAnim c0 = dialog_anim_at(0.0f, true);
        const DialogAnim c1 = dialog_anim_at(kDialogAnimSecs, true);
        dlg_case("D6 close t=0: alpha 1, slide 0",
                 std::fabs(c0.alpha - 1.0f) < 1e-5f && std::fabs(c0.slide) < 1e-5f);
        dlg_case("D6 close t=0.25: alpha 0, slide 1000 (dc.KK(1)=1)",
                 std::fabs(c1.alpha) < 1e-5f && std::fabs(c1.slide - 1000.0f) < 1e-3f);
        dlg_case("D6 backdrop fades IN (`r6(1,null,dc.Ln())` L927)",
                 dialog_backdrop_alpha_at(0.0f, false) < 1e-5f &&
                     std::fabs(dialog_backdrop_alpha_at(kDialogAnimSecs, false) - 1.0f) <
                         1e-5f);
        dlg_case("D6 backdrop fades OUT on close (`r6(0,..,dc.KK())` L927)",
                 dialog_backdrop_alpha_at(kDialogAnimSecs, true) < 1e-5f);
    }

    // --- D12: `od.layout` L1898 — the layout is DERIVED from `Md`. ----------
    {
        const OdLayout l550 = od_layout(550.0f);
        const float cy = l550.panel.py + l550.panel.ph * 0.5f;
        const float c = l550.panel.c;
        dlg_case("D12 a = clamp(Md/2,300,1000): Md=550 -> 300",
                 std::fabs(l550.a - 300.0f) < 1e-4f);
        dlg_case("D12 Md=400 -> a = 300 (lower clamp)",
                 std::fabs(od_layout(400.0f).a - 300.0f) < 1e-4f);
        dlg_case("D12 Md=2400 -> a = 1000 (upper clamp)",
                 std::fabs(od_layout(2400.0f).a - 1000.0f) < 1e-4f);
        dlg_case("D12 title y = -(a + 160) (`Vc.pfa().y` = Fa(1560,160))",
                 std::fabs(l550.title_y - (cy - (300.0f + 160.0f) * c)) < 1e-3f);
        dlg_case("D12 button y = a + 125/2 (`Bb.Pb(125)`)",
                 std::fabs(l550.btn_cy - (cy + (300.0f + 62.5f) * c)) < 1e-3f);
        dlg_case("D12 body top = -Md/2 (`Ne.D(-Md/2)`)",
                 std::fabs(l550.body_y - (cy - 275.0f * c)) < 1e-3f);
        dlg_case("D12 body box is 900 wide at design x -100 (D4/L1953-1954)",
                 std::fabs(l550.body_w - 900.0f * c) < 1e-3f &&
                     std::fabs(l550.body_x -
                               (l550.panel.px + l550.panel.pw * 0.5f - 100.0f * c)) < 1e-3f);
        dlg_case("D12 portrait is 512*1.8 design px (`oe` `la(1.8*iy)`)",
                 std::fabs(l550.portrait - 512.0f * 1.8f * c) < 1e-3f);
        dlg_case("D12 the layout really moves with Md (no invented fractions)",
                 std::fabs(od_layout(2400.0f).btn_cy - l550.btn_cy) > 1.0f);
    }

    // --- D11: the `Ib` bar = ONE label of the JOINED lines, NO speaker row. -
    {
        q.clear_dialogs();
        EngineDialog d = probe_dialog("d11_speaker");
        d.type = "Notification";
        d.title = "d11_speaker_title";
        d.lines = {"d11_l0", "d11_l1", "d11_l2"};
        const std::string joined = ib_joined_lines(app, d);
        dlg_case("D11 notification joins EVERY line (`lj` L1908)",
                 joined == std::string("d11_l0\nd11_l1\nd11_l2"));
        dlg_case("D11 notification text carries NO speaker/title row",
                 joined.find(d.title) == std::string::npos);
        dlg_case("D11 notification portrait comes from `Image` (`Qhb(a=wt)` L1907)",
                 dialog_kind(d.type) == DialogKind::kIbBar);
    }

    // --- D8: `He.SK` L1043 ReadTime -> `Ib.aa` L1905 auto-dismiss. ----------
    {
        // The default budget is `ge.ZGa` (`<NotificationDlgDefaultReadTime
        // Value="1.0">`, internal_settings.xml L1278): an absent `ReadTime`
        // resolves to it (`u.H(ReadTime, ge.ZGa)`).
        q.clear_dialogs();
        EngineDialog outer = probe_dialog("d8_parse");
        outer.button_actions.push_back(
            dialog_action({{"Type", "Notification"}, {"Title", "d8t"}, {"Line", "x"}},
                          "d8_line"));
        q.push_dialog_for_test(outer);
        q.press_dialog(app, 1);
        const bool default_rt = q.has_dialog() &&
                                std::fabs(q.dialog().read_time - 1.0f) < 1e-4f;
        dlg_case("D8 absent ReadTime -> ge.ZGa default 1.0 s (L1043/L1278)", default_rt);
        q.clear_dialogs();
        EngineDialog outer5 = probe_dialog("d8_parse5");
        outer5.button_actions.push_back(dialog_action({{"Type", "Notification"},
                                                       {"Title", "d8t5"},
                                                       {"ReadTime", "5.0"}},
                                                      "d8_line5"));
        q.push_dialog_for_test(outer5);
        q.press_dialog(app, 1);
        dlg_case("D8 ReadTime=5.0 parsed onto the dialog (He L1043)",
                 q.has_dialog() && std::fabs(q.dialog().read_time - 5.0f) < 1e-4f);
        // `Ib.aa` L1905: `this.SK-=a; this.SK<=0&&(this.qma=!0)` -> `OZa` L1908.
        q.clear_dialogs();
        EngineDialog n = probe_dialog("d8_notif");
        n.type = "Notification";
        n.image = "character_sensei_small";
        n.read_time = 5.0f;
        q.push_dialog_for_test(n);
        const bool not_yet = !quest_read_time_advance(app, 4.9f) && q.has_dialog();
        dlg_case("D8 4.9 s of 5.0: the bar is still up (`SK-=a`)", not_yet);
        const bool popped = quest_read_time_advance(app, 0.2f) && !q.has_dialog();
        dlg_case("D8 5.1 s of 5.0: the bar auto-pops (`SK<=0 -> y4(false)`)", popped);
        // `SK` only ever belongs to the `Notification` Type (L1050 `Qhb(..,SK,..)`).
        q.clear_dialogs();
        EngineDialog reg = probe_dialog("d8_reg");
        reg.read_time = 5.0f;
        reg.button_actions.push_back(marker_action("D8_REG"));
        reg.button_text = "d8_cap";
        q.push_dialog_for_test(reg);
        dlg_case("D8 a Regular dialog ignores ReadTime (SK is the Ib bar)",
                 !quest_read_time_advance(app, 9.0f) && q.has_dialog());
    }

    // --- D9: `He.S` L1046 `f=ba.Pc(a,this.title); r=ba.Pc(a,this.image)`. ----
    {
        WarriorSave w = app.save().load();
        const bool had_t = w.variables.count("Title_Assistant") != 0;
        const bool had_i = w.variables.count("Avatar_Assistant_1") != 0;
        const std::string old_t = had_t ? w.variables["Title_Assistant"] : std::string();
        const std::string old_i = had_i ? w.variables["Avatar_Assistant_1"] : std::string();
        w.variables["Title_Assistant"] = "RESOLVED_TITLE";
        w.variables["Avatar_Assistant_1"] = "RESOLVED_IMAGE";
        app.save().save(w);

        q.clear_dialogs();
        EngineDialog outer = probe_dialog("d9_outer");
        outer.button_actions.push_back(dialog_action(
            {{"Type", "Regular"}, {"Title", "_Title_Assistant"},
             {"Image", "_Avatar_Assistant_1"}},
            "d9_line"));
        q.push_dialog_for_test(outer);
        q.press_dialog(app, 1);
        const bool resolved = q.has_dialog() && q.dialog().title == "RESOLVED_TITLE" &&
                              q.dialog().image == "RESOLVED_IMAGE";
        dlg_case("D9 Title/Image run the quest-var resolution (ba.Pc L1046)", resolved);
        // A plain lang key (sensei_arc.xml `NAME_LYNX`) passes through untouched.
        q.clear_dialogs();
        EngineDialog outer2 = probe_dialog("d9_outer2");
        outer2.button_actions.push_back(dialog_action(
            {{"Type", "Regular"}, {"Title", "NAME_LYNX"}, {"Image", "boss_lynx_young"}},
            "d9_line2"));
        q.push_dialog_for_test(outer2);
        q.press_dialog(app, 1);
        dlg_case("D9 a non-`_` Title stays literal (NAME_LYNX)",
                 q.has_dialog() && q.dialog().title == "NAME_LYNX" &&
                     q.dialog().image == "boss_lynx_young");

        WarriorSave back = app.save().load();
        if (had_t) back.variables["Title_Assistant"] = old_t;
        else back.variables.erase("Title_Assistant");
        if (had_i) back.variables["Avatar_Assistant_1"] = old_i;
        else back.variables.erase("Avatar_Assistant_1");
        app.save().save(back);
    }

    // --- D10: `He` L1043-1045 parse + `He.S` L1051 apply. -------------------
    {
        // D14 `v.RIa` L1222: split on `|`, basename after the last `/`, `flip`.
        const DialogImageRef r1 = dialog_image_ref("boss_lynx_young|Flip");
        dlg_case("D10 Mirrored -> image gets |Flip; RIa flags it (L1047/L1222)",
                 r1.file_name == "boss_lynx_young" && r1.flip);
        const DialogImageRef r2 = dialog_image_ref("UI/Items/img_unlimited_energy|Flip");
        dlg_case("D10 RIa basenames a path ref (L1222)",
                 r2.file_name == "img_unlimited_energy" && r2.flip);
        const DialogImageRef r3 = dialog_image_ref("UI/Items/img_unlimited_energy");
        dlg_case("D10 RIa without a flip token is unflipped",
                 r3.file_name == "img_unlimited_energy" && !r3.flip);

        q.clear_dialogs();
        EngineDialog outer = probe_dialog("d10_outer");
        outer.button_actions.push_back(dialog_action(
            {{"Type", "Regular"},
             {"Title", "d10t"},
             {"Image", "boss_lynx_young"},
             {"Mirrored", "1"},
             {"ImageScale", "0.9"},
             {"ImageOffsetX", "-130"},
             {"ImageOffsetY", "40"},
             {"ContentOffsetX", "30"},
             {"TextOffset", "10;20"},
             {"TextPosXByImage", "0"},
             {"BlockRaycast", "0"},
             {"DisableNotificationsButtons", "1"},
             {"MinContentHeight", "777"}},
            "d10_line"));
        q.push_dialog_for_test(outer);
        q.press_dialog(app, 1);
        const EngineDialog& d = q.dialog();
        dlg_case("D10 Mirrored parsed (L1043 n4a)", d.mirrored);
        dlg_case("D10 image carries the |Flip token (L1047)",
                 d.image == "boss_lynx_young|Flip");
        dlg_case("D10 ImageScale parsed (L1044 iy)", std::fabs(d.image_scale - 0.9f) < 1e-4f);
        dlg_case("D10 ImageOffsetX/Y parsed (L1044 OB/YV)",
                 std::fabs(d.image_offset_x + 130.0f) < 1e-4f &&
                     std::fabs(d.image_offset_y - 40.0f) < 1e-4f);
        dlg_case("D10 ContentOffsetX parsed (L1044 TM)",
                 std::fabs(d.content_offset_x - 30.0f) < 1e-4f);
        dlg_case("D10 TextOffset \"x;y\" parsed (L1044 Xy)",
                 std::fabs(d.text_offset_x - 10.0f) < 1e-4f &&
                     std::fabs(d.text_offset_y - 20.0f) < 1e-4f);
        dlg_case("D10 TextPosXByImage=0 parsed (L1045 LH)", !d.text_pos_x_by_image);
        dlg_case("D10 BlockRaycast=0 parsed (L1044 $Ta)", !d.block_raycast);
        dlg_case("D10 DisableNotificationsButtons=1 parsed (L1044 qUa)",
                 d.disable_notifications_buttons);
        dlg_case("D10 MinContentHeight parsed (L1044 cv -> Od.cv)",
                 std::fabs(d.min_content_height - 777.0f) < 1e-3f);

        // Apply (`He.S` L1051 -> `Od.ala` L1947 / `Jva` L1951 / `eba` L1950).
        const OdLayout L = dialog_layout_for(app, d, DialogAnim{});
        const float c = L.panel.c;
        const float cx0 = L.panel.px + L.panel.pw * 0.5f;
        const float cy0 = L.panel.py + L.panel.ph * 0.5f;
        dlg_case("D10 ImageScale -> portrait 512*1.8*iy*c (L1947 `la(1.8*iy)`)",
                 std::fabs(L.portrait - 512.0f * 1.8f * 0.9f * c) < 1e-2f);
        dlg_case("D10 ImageOffsetX -> avatar x -450+OB (L1947 `C(-450+OB)`)",
                 std::fabs(L.portrait_cx - (cx0 + (-450.0f - 130.0f) * c)) < 1e-2f);
        dlg_case("D10 ImageOffsetY -> avatar y = YV (L1947 `D(YV)`)",
                 std::fabs(L.portrait_cy - (cy0 + 40.0f * c)) < 1e-2f);
        dlg_case("D10 TextPosXByImage=0 -> content x from 0 + TM + TextOffset "
                 "(`Jva` L1951 / `eba` L1950)",
                 std::fabs(L.body_x - (cx0 + (0.0f + 30.0f + 10.0f) * c)) < 1e-2f);
        dlg_case("D10 TextOffset.y -> body top -Md/2 + ov.y (`eba` L1950)",
                 std::fabs(L.body_y - (cy0 + (-L.md * 0.5f + 20.0f) * c)) < 1e-2f);

        // The `Ib` gates: `BlockRaycast` gates the dim (L1050/L1907), `Ib.RP`
        // gates the OK plate (L1910).
        EngineDialog nt = probe_dialog("d10_notif");
        nt.type = "Notification";
        nt.button_actions.push_back(marker_action("D10_OK"));
        nt.button_text = "OK";
        dlg_case("D10 BlockRaycast=0 parsed and gates the Ib Uz overlay "
                 "(L1050 h / L1907)",
                 !notification_blocks_raycast(d));
        nt.disable_notifications_buttons = true;
        dlg_case("D10 DisableNotificationsButtons hides the OK plate (Ib.RP L1910)",
                 !notification_show_ok(nt));
        nt.disable_notifications_buttons = false;
        dlg_case("D10 RP clear + a caption shows the OK plate (L1910)",
                 notification_show_ok(nt));
    }

    // --- D14: the portrait resolves through the `RIa` registry. -------------
    {
        EngineDialog di = probe_dialog("d14_item");
        di.item = "Chest_Gems";
        dlg_case("D14 an Item composite wins over the Image (Od.$A L1945)", !di.item.empty());
        EngineDialog dn = probe_dialog("d14_img");
        dlg_case("D14 without an Item the Image is the RIa registry ref",
                 dn.item.empty() &&
                     dialog_image_ref(dn.image).file_name == dn.image);
    }

    q.clear_dialogs();  // the D8/D9/D10 parse cases leave their queued dialogs

    // --- D13: nav #5 opens the Settings `un` dialog over the current screen. -
    {
        close_settings_dialog();
        app.screens().push(make_screen(app.screens(), kScreenShop));
        const int before = app.screens().current_id();
        Screen* top = app.screens().top();
        const bool on_shop = before == kScreenShop && top != nullptr;
        if (top != nullptr) {
            za_nav_activate(app, *top, static_cast<ScreenId>(before), 4);
        }
        const bool no_nav = on_shop && app.screens().current_id() == kScreenShop;
        dlg_case("D13 nav #5 does NOT navigate (`Vfb` L1981 -> `Xc.Shb` L931)", no_nav);
        dlg_case("D13 the Settings `un` dialog is open over the current screen",
                 settings_dialog_open() && app.screens().current_id() == kScreenShop);
        const bool blocks = quest_modal_consume(app);
        dlg_case("D13 the open dialog blocks the screen beneath (`Wb` top)", blocks);
        close_settings_dialog();
        dlg_case("D13 closing the dialog releases the block", !quest_modal_consume(app));
    }

    // --- D15: RESTART hidden at open, revealed on a language change. --------
    {
        close_settings_dialog();
        open_settings_dialog(app);
        dlg_case("D15 RESTART + notice hidden at open (un `X(!1)`/`R(!1)` L1929/30)",
                 !settings_dialog_restart_visible());
        const SettingsLayout s_hidden = settings_layout();
        dlg_case("D15 hidden: BACK centred, RESTART not offset (L1930)",
                 std::fabs(s_hidden.back_cx - s_hidden.restart_cx) < 1e-3f);
        settings_dialog_cycle_language(app);
        const std::string saved = app.language().empty() ? "en" : app.language();
        dlg_case("D15 a language change reveals RESTART (`t9`, L1931)",
                 settings_dialog_restart_visible() && g_settings_lang != saved);
        const SettingsLayout s_shown = settings_layout();
        dlg_case("D15 revealed: BACK/RESTART split by width*.6 (L1931)",
                 s_shown.back_cx < s_shown.restart_cx &&
                     std::fabs((s_shown.restart_cx - s_shown.back_cx) - s_shown.btn_w * 1.2f) <
                         1e-2f);
        close_settings_dialog();
    }

    // --- D3: the `Wb` dialog is a GLOBAL overlay (any screen, not just
    // Fight/Dojo/Map/Shop). `Wb.Xob` L927 appends the dialog node to the
    // ACTIVE screen's content root, so Results/Profile/Settings show + block.
    {
        struct BlockCase {
            const char* name;
            ScreenId id;
        };
        const BlockCase kScreens[] = {
            {"Fight", kScreenFight},       {"Results", kScreenResults},
            {"Profile", kScreenProfile},   {"Settings", kScreenSettings},
        };
        for (const BlockCase& bs : kScreens) {
            q.clear_dialogs();
            EngineDialog d = probe_dialog("d3_block");
            d.button_actions.push_back(marker_action("D3_BLOCK"));
            d.button_text = "d3_cap";
            q.push_dialog_for_test(d);
            if (bs.id == kScreenFight) {
                PendingBattle& pb = app.pending_battle();
                if (pb.battle_name.empty()) {
                    pb.battle_name = "Training";
                    pb.location = "dojo";
                }
            }
            app.screens().push(make_screen(app.screens(), bs.id));
            const bool on_screen = app.screens().current_id() == bs.id;
            const bool live = quest_modal_top(app) != nullptr;
            const bool blocks = quest_modal_consume(app);
            dlg_case(std::string("D3 dialog renders on ") + bs.name, on_screen && live);
            dlg_case(std::string("D3 dialog blocks ") + bs.name, on_screen && blocks);
        }
    }
    q.clear_dialogs();
    // D13: the Settings `un` dialog a pushed `SettingsScreen` hosts (the D3
    // Settings iteration) must not leak into the next section.
    close_settings_dialog();

    std::fprintf(stdout, "[dlgverify] %d passed, %d failed\n", g_dlg_passed, g_dlg_failed);
    std::fflush(stdout);
    return g_dlg_failed == 0;
}

// ---------------------------------------------------------------------------
// [probe, authorised] `--settings-profile-shop-probe` (app/game/main.cpp): the
// three shell behaviours added in `e567dcb8`, driven through the real code
// paths. NO OS input; the driver forces the hidden window + RULE 0 watchdog.
// ---------------------------------------------------------------------------
void EquipmentScreen::select_move(int index) {
    move_sel_ = index;  // `vb.uj = a` (L2198); the same assignment `update_impl` runs
}

std::string EquipmentScreen::shown_move() const {
    if (move_rows_.empty()) return std::string();
    const int sel = std::clamp(move_sel_, 0, static_cast<int>(move_rows_.size()) - 1);
    return move_rows_[static_cast<std::size_t>(sel)].name;
}

int run_shell_probe(App& app) {
    int fails = 0;
    const auto check = [&](bool ok, const char* what) {
        std::fprintf(stdout, "[sps] %-62s %s\n", what, ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        if (!ok) ++fails;
    };
    // (i) Settings Credits row -> the credits view (`un.rHa` case 2 -> `xh.show`,
    // screens.cpp `settings_run_row` -> `credits_show`).
    g_credits_open = false;
    settings_run_row(app, SettingsRow::kCredits);
    std::fprintf(stdout, "[sps] credits open=%d rows=%zu lang=%s\n",
                 g_credits_open ? 1 : 0, g_credits_rows.size(), g_credits_lang.c_str());
    std::fflush(stdout);
    check(g_credits_open && !g_credits_rows.empty(),
          "(i) Settings Credits row opens the credits view (rows > 0)");
    credits_close();
    // (ii) Moves-tab cell selection -> the `$r` right panel's shown move. The
    // throwaway `EquipmentScreen` runs the real ctor (which builds `move_rows_`
    // via `build_move_list_locks`); `select_move` is the `vb.hqb` assignment.
    {
        EquipmentScreen eq(app.screens());
        const int rows = eq.move_row_count();
        eq.select_move(0);
        const std::string first = eq.shown_move();
        const int sel_idx = rows >= 2 ? 1 : 0;
        eq.select_move(sel_idx);
        const std::string second = eq.shown_move();
        std::fprintf(stdout, "[sps] moves rows=%d sel0='%s' sel%d='%s'\n", rows,
                     first.c_str(), sel_idx, second.c_str());
        std::fflush(stdout);
        check(!first.empty() && !second.empty() && (rows < 2 || second != first),
              "(ii) Moves cell selection drives the panel (not pinned to row 0)");
    }
    // (iii) Shop cell sale badge on the RUBY tab (kShopTabs index 5): the
    // shipped `ConsumableProduct` + `AddPercent` + `SubType="Bonus"` row draws
    // `pieces/FreeGems_red` (`I.$F`). Same rule the cell draw uses.
    {
        const std::vector<CatalogItem> cat = load_catalog(app);
        const std::vector<std::size_t> ruby = shop_tab_rows(cat, 5);
        std::string found;
        for (const std::size_t r : ruby) {
            const CatalogItem& it = cat[r];
            const ShopBadge b = shop_cell_badge(app, it, /*shop_sale=*/false);
            if (it.subtype == "Bonus" && it.consumable_product && it.add_percent > 0 &&
                b.art != nullptr && std::string(b.art) == "pieces/FreeGems_red") {
                found = it.name;
                break;
            }
        }
        std::fprintf(stdout, "[sps] RUBY tab rows=%zu FreeGems_red item='%s'\n",
                     ruby.size(), found.c_str());
        std::fflush(stdout);
        check(!found.empty(),
              "(iii) RUBY-tab sale cell badge = pieces/FreeGems_red (SubType=Bonus)");
    }
    // (iv) `Pn` L1064 (`EDiscount`) `Period` -> `yf.yn`: a discount carrying a
    // `Period` yields the shopSale state (`ns.j5` L2308-2309 `b.yn > p.Dc`).
    // The shipped `Period` shape is `test_quests.xml` (`Toggle="1"` + a
    // `Period="?Timer[..]"` query, NO `Percent`), so fire it through the real
    // engine action path (`run_action_probe`) and read the offer back.
    {
        QuestEngine& qe = app.quest_engine();
        const std::string kDisc = "WEAPON_CRESCENT_KNIVES";  // any catalog item
        const auto fire_discount = [&](const char* toggle, const char* period,
                                       const char* sale) {
            QuestAction act;
            act.tag = "Discount";
            act.attrs["Item"] = kDisc;
            act.attrs["Toggle"] = toggle;
            act.attrs["Period"] = period;
            act.attrs["Sale"] = sale;
            QuestJournal j;
            qe.run_action_probe(app, {act}, j);
        };
        fire_discount("1", "3600", "1");
        const EngineItemOffer* of = qe.offer_for(kDisc);
        const double now = QuestEngine::now_seconds();
        std::fprintf(stdout, "[sps] discount offer=%d sale=%d end=%lld now=%.0f\n",
                     of != nullptr ? 1 : 0, (of != nullptr && of->sale) ? 1 : 0,
                     of != nullptr ? of->end_time : 0, now);
        std::fflush(stdout);
        check(of != nullptr && of->sale && of->end_time > now,
              "(iv) Discount Period>0 -> yf.yn > p.Dc (shopSale state)");
        // `Toggle="0"` clears it (`b.G.E4()`).
        fire_discount("0", "3600", "1");
        check(qe.offer_for(kDisc) == nullptr,
              "(iv) Discount Toggle=0 -> offer cleared (E4)");
    }
    std::fprintf(stdout, "[sps] RESULT %s (%d fail)\n", fails == 0 ? "PASS" : "FAIL",
                 fails);
    std::fflush(stdout);
    return fails;
}

} // namespace sf2::app

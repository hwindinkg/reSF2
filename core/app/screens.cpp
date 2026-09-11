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
//     x = X*1.0 + view_w/2, y = view_h/2 - Y*1.0 (qe.X0a's bg.w/2 /
//     bg.h/2 with uM≈1 for the 2046-wide map0 frame scaled to the view).
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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
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

// --- Sensei dialog modal (quest engine He records) -------------------------
// The engine queues structured dialogs on fire; Dojo/Map show the head as a
// tap-to-advance modal and gate their own buttons behind it (dialog modal
// gating). Headless drains the queue silently instead (auto-advance — the
// scripted loop never modal-blocks; detected via App::headless(), i.e. the
// headless_frames_ > 0 pattern the driver sets).
const EngineDialog* quest_modal_top(App& app) {
    if (app.headless()) {
        while (app.quest_engine().has_dialog()) {
            std::fprintf(stdout, "[quest] dialog skipped (headless): %s\n",
                         app.quest_engine().dialog().title.c_str());
            std::fflush(stdout);
            app.quest_engine().pop_dialog();
        }
        return nullptr;
    }
    if (!app.quest_engine().has_dialog()) return nullptr;
    return &app.quest_engine().dialog();
}

// Returns true while a modal is up (caller skips its own buttons/keys that
// frame); advances the queue on press.
bool quest_modal_consume(App& app) {
    const EngineDialog* d = quest_modal_top(app);
    if (d == nullptr) return false;
    if (app.pointer().pressed) {
        std::fprintf(stdout, "[quest] dialog advanced: %s\n", d->title.c_str());
        std::fflush(stdout);
        app.quest_engine().pop_dialog();
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
// locale by 1. `ensure_lang` is EN-only (the app loads `ui/font-en.*`), so
// the active `a1` is 1; the 0.8 branch is unreachable until the app-level
// per-language font/atlas swap exists (OPEN).
constexpr float kEaA1 = 1.0f;

void draw_ui_label(App& app, float x, float y, float w, float h,
                   const std::string& text, float ua_scale, UiAlign align,
                   float r, float g, float b, float a = 1.0f, bool fit = true) {
    if (text.empty() || w <= 0.0f || h <= 0.0f) return;
    const sf2::data::font* font = app.menu_font();
    if (font == nullptr) return;
    // Glyph scale = ua(size)/charset.eF (L1631); `ua_scale` is that ratio.
    float scale = ua_scale * kEaA1;
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

// Helpers defined later in this file (the atlas sprite path sits after this
// point; the `od`/`Ib` dialog art below needs it).
bool try_draw_atlas_button(App& app, const std::string& frame_name, float cx, float cy,
                           float w, float h, float alpha = 1.0f, bool fill = false,
                           bool flip_x = false, bool top_left = false);
bool load_scroll_atlas(App& app);
void draw_ib_hint(App& app, sf2::render::Renderer& ren, const std::string& speaker,
                  const std::string& line1, const std::string& line2, bool show_ok);

// Draws the modal panel. JS `He` (L1042-1063) routes dialogs by Type:
// `Notification` posts to the `Ib` hint bar (`Ib.F().Qhb`, L1050); every
// other type builds an `Xc` dialog over the `od` 9-slice base (L1894-1900).
// The base is `bg`/`bg_edge` (asset id 254 = res/ui/scroll, `y.lSa`/`y.eoa`
// L2467; `XN[0..2]` L1894), title `Vc` (`Fa(1560,160)`, `C(-780)`, `ua(152)`,
// color `Z.W6` L1900) and the body lines. Replaces the invented flat
// 900x220 quad + "TAP TO CONTINUE" (PORT_AUDIT_UI §2.9).
void draw_quest_modal(App& app, sf2::render::Renderer& ren, bool is_top = true) {
    if (!is_top) return;  // layered stack: only the top screen draws the modal
    const EngineDialog* d = quest_modal_top(app);
    if (d == nullptr) return;
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.6f);
    // `Notification` -> the `Ib` hint bar (L1045-1050), no `od` panel.
    if (d->type == "Notification") {
        draw_ib_hint(app, ren, d->title, d->lines.empty() ? "" : d->lines[0],
                     d->lines.size() > 1 ? d->lines[1] : "", /*show_ok=*/true);
        return;
    }
    // `od` 9-slice: fit the 2340x1300 design rect into the view (`l4a`
    // L1895-1896), draw the `bg` body + `bg_edge` caps, then the `Vc` title.
    const bool have = load_scroll_atlas(app);
    constexpr float kOdW = 2340.0f;
    constexpr float kOdH = 1300.0f;
    const float c = std::min(kViewW / kOdW, kViewH / kOdH);
    const float pw = kOdW * c, ph = kOdH * c;
    const float px = kViewW * 0.5f - pw * 0.5f;
    const float py = kViewH * 0.5f - ph * 0.5f;
    bool drew_bg = false;
    if (have) {
        drew_bg = try_draw_atlas_button(app, "bg", px + pw * 0.5f, py + ph * 0.5f, pw, ph,
                                        1.0f, /*fill=*/true, /*flip_x=*/false);
        if (drew_bg) {
            // `XN[0]` (left) + `XN[2]` (right; JS `Hr(!0)` flips it — native
            // flip is OPEN, the cap reads as a symmetric frame edge here).
            try_draw_atlas_button(app, "bg_edge", px, py + ph * 0.5f, pw, ph, 1.0f, false,
                                  /*flip_x=*/false);
            try_draw_atlas_button(app, "bg_edge", px + pw, py + ph * 0.5f, pw, ph, 1.0f, false,
                                  /*flip_x=*/false);
        }
    }
    if (!drew_bg) {
        const float panel[] = {px, py, px + pw, py, px, py + ph,
                               px + pw, py, px + pw, py + ph, px, py + ph};
        ren.draw_triangles(panel, 6, 0.08f, 0.07f, 0.10f, 0.95f);
    }
    // `Vc` title (`Fa(1560,160)`, `ua(152)`, color `Z.W6` = 0.404/0.243/0.141).
    const float title_w = 1560.0f * c, title_h = 160.0f * c;
    draw_ui_label(app, px + pw * 0.5f - title_w * 0.5f, py + 8.0f * c, title_w, title_h,
                  d->title, 1.0f, UiAlign::Center, 0.404f, 0.243f, 0.141f);
    const float body_y = py + title_h + 24.0f * c;
    for (std::size_t i = 0; i < d->lines.size() && i < 4; ++i) {
        draw_ui_label(app, px + 80.0f * c, body_y + static_cast<float>(i) * 44.0f * c,
                      pw - 160.0f * c, 40.0f * c, d->lines[i], 0.8f, UiAlign::Left, 1.0f,
                      1.0f, 1.0f);
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
struct OdPanel {
    float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;  // on-screen panel rect
    float c = 1.0f;                                    // design -> screen scale
};

OdPanel od_panel(float src_w, float src_h) {
    OdPanel p;
    const float screen_ar = kViewW / kViewH;  // N.lc
    const float src_ar = src_w / src_h;
    if (screen_ar >= src_ar) {
        p.ph = kViewH;
        p.pw = p.ph * src_ar;
    } else {
        p.pw = kViewW;
        p.ph = p.pw / src_ar;
    }
    p.c = p.pw / src_w;  // (b.N-b.J)/AV.x
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
            // `XN[0]` left + `XN[2]` right (the right one flipped, L1894).
            try_draw_atlas_button(app, "bg_edge", p.px, p.py + p.ph * 0.5f, p.pw, p.ph,
                                  1.0f, false, /*flip_x=*/false);
            try_draw_atlas_button(app, "bg_edge", p.px + p.pw, p.py + p.ph * 0.5f, p.pw,
                                  p.ph, 1.0f, false, /*flip_x=*/true);
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
// content node at `Ne.D(-Md/2)` and the button zone at `a=clamp(Md/2,300,1000)`
// (=375 for Md=750); the title `Vc` sits at `D(-(a+Vc.pfa().y))`. Rows are
// Sound/Music (gated by `Ca.hasFeature("audio")`, L1928), Credits
// (`hasFeature("credits")`, L1929) and Language. The exact per-row offsets
// (`b`: `k*icon.qa()*1.25`; `c`: `k*icon.qa()*1.25-icon.qa()/2`, L1917) need
// the `E.get(250)` frame sizes, which the native does not decode — rows are
// stacked on the `Md` zone (OPEN, PORT_AUDIT_UI §2.9).
struct SettingsLayout {
    OdPanel panel;
    float title_x = 0.0f, title_y = 0.0f, title_w = 0.0f, title_h = 0.0f;
    float row_x = 0.0f, row_w = 0.0f, row_h = 0.0f;
    float row_y[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // Sound/Music/Credits/Language
    float back_cx = 0.0f, back_cy = 0.0f;
    float restart_cx = 0.0f, restart_cy = 0.0f;
    float btn_w = 0.0f, btn_h = 0.0f;
    float notice_y = 0.0f;
};

SettingsLayout settings_layout() {
    SettingsLayout s;
    s.panel = od_panel(2340.0f, 1530.0f);  // od AV = fc(2340,1530), L1894
    const OdPanel& p = s.panel;
    const float cx = p.px + p.pw * 0.5f;
    const float cy = p.py + p.ph * 0.5f;
    // Title `Vc`: `$T` L1930 `Fa(1560,160)` + `C(-780)`; `ua(152)`, `Ia(128)`
    // (L1900). `od.layout` L1898: `Vc.D(-(a+Vc.pfa().y))` with a=375.
    s.title_w = 1560.0f * p.c;
    s.title_h = 160.0f * p.c;
    s.title_x = cx - s.title_w * 0.5f;
    s.title_y = cy - 535.0f * p.c - s.title_h * 0.5f;
    // Rows span `Md` (750) centred on the panel.
    s.row_w = 860.0f * p.c;
    s.row_x = cx - s.row_w * 0.5f;
    s.row_h = 110.0f * p.c;
    const float step = s.row_h + 16.0f * p.c;
    const float zone_top = cy - 300.0f * p.c;
    s.row_y[0] = zone_top;
    s.row_y[1] = zone_top + step;
    s.row_y[2] = zone_top + step * 2.0f;
    s.row_y[3] = zone_top + step * 3.0f;
    // Buttons `Bb.Pb(150)` (L1930): BACK left, RESTART at `C(500)`.
    s.btn_w = 320.0f * p.c;
    s.btn_h = 150.0f * p.c;
    s.back_cx = cx - 320.0f * p.c;
    s.restart_cx = cx + 500.0f * p.c;
    // `od.layout` (L1898) D: `Cd.D(a + Cd.node.qa()/2)` with a = Md/2 = 375
    // and the button container height = one `Bb.Pb(150)` -> centre 450.
    s.back_cy = s.restart_cy = cy + 450.0f * p.c;
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

// The between-rounds HUD "Next" button (JS `vhb` L410 case 1 -> `Z2()`):
// center + size in screen coords. Drawn only while
// FightController::round_wait(); a click (or Space/Enter, see on_key)
// runs next_round_requested() (recovery + the next round).
constexpr float kNextBtnCX = kViewW * 0.5f;
constexpr float kNextBtnCY = kViewH * 0.6f;
constexpr float kNextBtnW = 240.0f;
constexpr float kNextBtnH = 80.0f;

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
constexpr float kPadMarginC = kViewH * 0.05f;  // c — side margin
constexpr float kPadMarginD = kViewH * 0.03f;  // d — bottom margin
constexpr float kPadSizeE = 150.0f;            // e — max(150, H*0.2) at 720
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

// Loads the ui/controller atlas (the virtual gamepad art: Joystick*,
// btn_punch_*, btn_kick_*) into the app's atlas cache. Returns true when
// the frames are registered (logged once). The atlas ships as ASTC ktx —
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

// Draws a flat (untextured) button + its label as a solid quad. The exact
// menu atlas art (ASTC) is unavailable to the CPU pipeline this phase —
// flagged as the exact-layout gap.
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

// The `Ib` notification/hint bar (JS L1905-1912). Layout: `node.C(W -
// scroll.width*scale)`, `node.D(za.Sp)`; scroll `gk(600,250,50,0)` horizontal
// with a `Fg(600,250,1,30)` content frame (paper rails). Sensei image = JS
// `E.get(12)` (native `sensei_portrait`); label `Fa(600-image.w+20,150)`,
// `C(image.w-30)`, `D(50)`; OK `Bb` at local (450,185). The `gYa()` gate is
// the shell predicate (true on Dojo/Map — the preloader/fight cases are
// OPEN); `Ib.RP` (He.DisableNotificationsButtons, L1045) gates the OK button
// — the native EngineDialog does not carry it, so callers pass `show_ok`.
void draw_ib_hint(App& app, sf2::render::Renderer& ren, const std::string& speaker,
                  const std::string& line1, const std::string& line2, bool show_ok) {
    const float c =
        std::clamp(std::min(kViewW * 0.75f, kViewH * 0.75f) / 600.0f, 0.2f, 1.1f);
    const float sp = std::min(kViewH * 0.13f, 100.0f) * 0.78f;  // za.odb L1975
    const float ox = kViewW - 600.0f * c;
    const float oy = sp;
    auto lx = [&](float v) { return ox + v * c; };
    auto ly = [&](float v) { return oy + v * c; };
    // JS `Ib` (L1906) builds the bar as the `gk(600,250,50,0,!1)` scroll
    // (L1906 `O1a`), whose art is the `Zh` roll composite (L1872-1873),
    // NOT the `paper` sheet: `Zh` ctor adds `roll_end` (child 0),
    // `roll_center` (child 1) and `roll_end` flipped `Hr(!0)` (child 2)
    // (`y.goa`/`y.pSa` L2467-2468). `Zh.ba(600,250)` (horizontal: `c =
    // h>w = false`, `d = min(w,h) = 250`, `h = d/capSrcH`):
    //   capW  = 101 * 250/114      (roll_end sourceSize 101x114)
    //   bodyW = max(600 - 2*capW, 10)
    //   left cap x=0, body x=capW, right cap x=capW+bodyW (all 250 tall).
    constexpr float kRollEndW = 101.0f, kRollEndH = 114.0f;  // scroll.json roll_end
    constexpr float kBarW = 600.0f, kBarH = 250.0f;          // gk(600,250)
    const float cap_w = kRollEndW * (kBarH / kRollEndH);     // 221.49 local
    const float body_w = std::max(kBarW - 2.0f * cap_w, 10.0f);
    const float ph = kBarH * c;
    bool drew = false;
    if (load_scroll_atlas(app)) {
        drew = try_draw_atlas_button(app, "roll_end", lx(cap_w * 0.5f), ly(kBarH * 0.5f),
                                     cap_w * c, ph, 1.0f, /*fill=*/true);
        if (drew) {
            try_draw_atlas_button(app, "roll_center", lx(cap_w + body_w * 0.5f),
                                  ly(kBarH * 0.5f), body_w * c, ph, 1.0f, /*fill=*/true);
            // Right cap: the third `Zh` child is `roll_end` with `Hr(!0)`.
            try_draw_atlas_button(app, "roll_end",
                                  lx(cap_w + body_w + cap_w * 0.5f), ly(kBarH * 0.5f),
                                  cap_w * c, ph, 1.0f, /*fill=*/true, /*flip_x=*/true);
        }
    }
    if (!drew) {
        const float panel[] = {lx(0), ly(0), lx(600), ly(0), lx(0), ly(250),
                               lx(600), ly(0), lx(600), ly(250), lx(0), ly(250)};
        ren.draw_triangles(panel, 6, 0.05f, 0.05f, 0.08f, 0.82f);
    }
    // Sensei (256px, transparent corners). No procedural ring — JS draws none
    // (PORT_AUDIT_UI §2.9).
    if (app.renderer().texture_lookup("sensei_portrait") != 0) {
        sf2::scene::Sprite s;
        s.texture_name = "sensei_portrait";
        s.frame_x = 0.0f;
        s.frame_y = 0.0f;
        s.frame_w = 256.0f;
        s.frame_h = 256.0f;
        s.tex_w = 256.0f;
        s.tex_h = 256.0f;
        s.solid = false;
        s.color_a = 1.0f;
        s.transform.set_pos(lx(128.0f), ly(130.0f));
        s.transform.set_scale(c, c);
        app.renderer().draw_sprite(s, ui_camera());
    }
    // Label (`Z.sc` = 0.184/0.145/0.106).
    const float tx = lx(256.0f - 30.0f);
    draw_ui_label(app, tx, ly(50.0f), 364.0f * c, 44.0f * c, speaker, 0.9f, UiAlign::Left,
                  0.184f, 0.145f, 0.106f);
    draw_ui_label(app, tx, ly(88.0f), 364.0f * c, 34.0f * c, line1, 0.75f, UiAlign::Left,
                  0.184f, 0.145f, 0.106f);
    if (!line2.empty()) {
        draw_ui_label(app, tx, ly(120.0f), 364.0f * c, 30.0f * c, line2, 0.7f, UiAlign::Left,
                      0.184f, 0.145f, 0.106f);
    }
    // OK (`Bb(Zva)` = "EButtonWhite", local (450,185), `zf(100)`); `Ib.RP`
    // gate (L1910) — only when the notification carries a button.
    if (show_ok) {
        if (!try_draw_atlas_button(app, "btnWhite", lx(450.0f), ly(185.0f), 100.0f * c,
                                   72.0f * c, 1.0f, false)) {
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
    return lay;
}

// JS `za.Aub` (L1977-1978): the nav is a `gk(400,800,50,1,!0,Y.na("menu"))`
// scroll and the ctor immediately calls `this.scroll.collapse(0)` — the
// COLLAPSED default. `gk.NLa` (L2001) then sets `zI=2` (collapsed) and hides
// the `Le` buttons (`i3.Z(!1)`); expanding (`zI=1`) reveals them. Only the
// `Lx` title (`Y.na("menu")` = "МЕНЮ", oracle x64-176 y72-110) shows while
// collapsed. The native keeps one shared flag (JS `za.instance` is a
// singleton mounted on every shell screen). `gk.Bgb` (L2000) toggles it.
bool g_za_nav_open = false;  // JS `collapse(0)` (L1978) default = collapsed

// The collapsed menu header rect (oracle tutorial shot, 1280x720 space).
void za_header_rect(float& x, float& y, float& w, float& h) {
    const float s = kViewW / 1280.0f;
    x = 64.0f * s;
    y = 72.0f * s;
    w = 112.0f * s;
    h = 38.0f * s;
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

// Handles the nav-column taps for a shell screen (JS listeners Ofb/Qfb/Wfb/
// Rfb/Vfb -> `ma.Jg().jI(cls)`): a tap pushes the target screen unless it is
// the screen already showing (the JS highlights that one active, `xyb`
// L1982).
void za_update(App& app, Screen& self, ScreenId active) {
    // JS `gk.Bgb` (L2000): a press on the scroll header toggles
    // `this.uJ?collapse(.3):expand(.3)`. While collapsed the five `Le`
    // buttons are hidden (`NLa` L2001), so only the header answers taps.
    float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
    za_header_rect(hx, hy, hw, hh);
    const double px = app.pointer().x, py = app.pointer().y;
    const bool header_hit = px >= hx && px <= hx + hw && py >= hy && py <= hy + hh;
    // Collapsed (JS `collapse(0)` L1978): the header expands the column.
    if (!g_za_nav_open) {
        if (header_hit && app.pointer().pressed) {
            g_za_nav_open = true;
            sf2::audio::AudioEngine::instance().play("click");
        }
        return;
    }
    // Expanded: a nav button wins over the header (the compact top-left
    // column overlaps); a header tap with no button collapses (L2000 toggle).
    const int hit = za_nav_hit(px, py);
    if (hit >= 0 && app.pointer().pressed) {
        const ScreenId target = kZaNav[hit].nav;
        sf2::audio::AudioEngine::instance().play("click");
        std::fprintf(stdout, "[za] nav %s -> screen %d\n", kZaNav[hit].label,
                     static_cast<int>(target));
        std::fflush(stdout);
        if (target != active) {
            self.push(target);
        }
        return;
    }
    if (header_hit && app.pointer().pressed) {
        g_za_nav_open = false;
        sf2::audio::AudioEngine::instance().play("click");
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

// Draws the shared chrome on top of a shell screen's own content. `active`
// selects the active nav frame (JS `xyb`). The widget strip mirrors `odb`:
// widgets are laid left->right and the strip is centred; each widget is
// icon + value (+ bar), scaled to `widget_h` (JS wr/xr/yr `layout`).
void draw_za_chrome(App& app, ScreenId active, const int* badges = nullptr) {
    sf2::render::Renderer& ren = app.renderer();
    const float w = kViewW;
    const ZaLayout lay = za_layout();
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
    const float lvl_w = icon_level + q + num_w + q + bar_w;
    // `xr.layout` L1985: the Energy bar sits at `icon.za()*1.1`, not icon+q.
    const float en_w = std::max(icon_energy, icon_energy * 1.1f + bar_w);
    const float money_w = icon_gold + money_num_w + yr_gap + icon_ruby + gem_num_w;
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
    const float ruby_left = money_tx + money_num_w + yr_gap;  // JS `PA.C(PA.za()/2+Dq.ya+c+b)`
    try_draw_atlas_button(app, "ruby", ruby_left + icon_ruby * 0.5f, cy, icon_ruby, icon, 1.0f);
    draw_ui_label(app, ruby_left + icon_ruby, cy - lay.widget_h * 0.45f, gem_num_w, lay.widget_h,
                  gem_text, num_scale, UiAlign::Center, 1.0f, 0.9f, 0.4f);
    // JS `gk` collapsed default (L1978): only the `Lx` title header shows;
    // the five `Le` buttons render only once expanded (`NLa` L2001).
    if (!g_za_nav_open) {
        float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
        za_header_rect(hx, hy, hw, hh);
        // The collapsed header over the `gk` scroll art (`Zh` roll frames);
        // the label is `Y.na("menu")` ("МЕНЮ" in the oracle locale).
        if (!try_draw_atlas_button(app, "roll_center", hx + hw * 0.5f, hy + hh * 0.5f, hw,
                                   hh, 1.0f, /*fill=*/true)) {
            const float panel[] = {hx, hy, hx + hw, hy, hx, hy + hh,
                                   hx + hw, hy, hx + hw, hy + hh, hx, hy + hh};
            ren.draw_triangles(panel, 6, 0.10f, 0.07f, 0.05f, 0.9f);
        }
        // The label is `Y.na("menu")`: UTF-8 bytes for `МЕНЮ` (0xD0 0x9C
        // 0xD0 0x95 0xD0 0x9D 0xD0 0xAE) written as escapes so the string
        // literal is independent of the compiler's source charset (MSVC).
        draw_ui_label(app, hx, hy + hh * 0.28f, hw, hh, "\xD0\x9C\xD0\x95\xD0\x9D\xD0\xAE",
                      0.6f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        return;
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
        if (!try_draw_atlas_button(app, frame, nav_cx, cy_i, lay.nav_btn, lay.nav_btn, 1.0f)) {
            draw_flat_button(app, def.label, nav_cx, cy_i, lay.nav_btn, lay.nav_btn,
                             is_active ? 0.6f : (is_hover ? 0.5f : 0.35f), 0.4f, 0.28f,
                             is_hover);
            draw_ui_label(app, nav_cx - lay.nav_btn * 0.5f, cy_i - 10.0f, lay.nav_btn, 20.0f,
                          def.label, 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        }
        // Le badge (JS `Dg`/`Le` L1848-1850: notification_circle + count).
        // The counts are screen-specific saves (Shop `tCa`, Profile totals)
        // NOT derived natively — callers pass nullptr until then (OPEN).
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
// layer over the fight (reads FightController::banner()/banner_text()/
// banner_progress()), never the simulation.
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

// The banner's animation envelope over `progress` (0..1): scale-in 0.5 -> 1.0
// over the first 15%, hold at 1.0, fade out over the last 20%. The
// victory/defeat banners hold forever (banner_len_ = 1e9 -> the controller's
// progress stays ~0), so they pop in from the screen-tracked age and hold.
float banner_scale_at(float progress) {
    constexpr float kInEnd = 0.15f;  // scale-in window (first 15%)
    if (progress <= 0.0f) return 0.5f;
    if (progress < kInEnd) {
        const float t = progress / kInEnd;              // 0..1
        const float rise = t * t * (3.0f - 2.0f * t);   // smoothstep 0..1
        return 0.5f + 0.5f * rise;                      // 0.5 -> 1.0
    }
    return 1.0f;
}

float banner_alpha_at(float progress) {
    constexpr float kFadeStart = 0.8f;  // fade-out window (last 20%)
    if (progress >= kFadeStart) {
        const float t = (progress - kFadeStart) / (1.0f - kFadeStart);
        return 1.0f - t;  // linear 1 -> 0
    }
    return 1.0f;
}

// Draws the current fight banner centered at ~35% of the view height: the
// callouts atlas art (id 1310) for round/fight/victory/defeat, with the
// menu-font text as the fallback when the frame is missing. Screen-space
// (the UI camera), drawn over the fight and under the gamepad (the caller's
// draw order).
//
// `banner_age` = fight frames since the banner was raised (tracked by the
// FightScreen — the controller's banner_progress() divides by banner_len_,
// which is 1e9 for the hold-forever VICTORY/DEFEAT banners, so their
// controller progress stays ~0; the screen-side age drives their pop-in).
void draw_fight_banner(App& app, const sf2::scene::FightController& fight, int banner_age) {
    const sf2::scene::banner_kind kind = fight.banner();
    if (kind == sf2::scene::banner_kind::none) return;

    float progress = fight.banner_progress();
    if (kind == sf2::scene::banner_kind::victory ||
        kind == sf2::scene::banner_kind::defeat) {
        // Hold-forever banner: pop in over ~0.75 s from the screen-tracked
        // age, then HOLD (no fade — the results screen takes over). The
        // envelope's fade window starts at 0.8, so clamp the pop-in
        // progress at 0.8: alpha stays 1.0 forever.
        constexpr float kHoldBannerInFrames = 45.0f;
        constexpr float kHoldProgressCap = 0.8f;
        progress = std::min(kHoldProgressCap,
                           static_cast<float>(banner_age) / kHoldBannerInFrames);
    }
    const float scale_anim = banner_scale_at(progress);
    const float alpha = banner_alpha_at(progress);
    if (alpha <= 0.01f) return;

    // Centered at ~35% of the view height (draw_text_* anchors a line at its
    // top y; the pop-in scale animates the art's box too).
    const float cx = kViewW * 0.5f;
    const float cy = kViewH * 0.35f;

    // The callouts atlas art (JS `Cr` L2022: `image = R.$(E.get(1310))`);
    // scaled min(800,min(W,H))/image.w*0.6 (layout L2027), centred.
    const char* frame = banner_atlas_frame(kind);
    if (frame != nullptr && load_callouts_atlas(app)) {
        const float art = std::min(800.0f, std::min(kViewW, kViewH)) * 0.6f;
        if (try_draw_atlas_button(app, frame, cx, cy, art * scale_anim, art * scale_anim,
                                  alpha)) {
            // The ROUND number: `round = ea(E.get(1298))`, Ia(64),
            // ua(fontSize*1.6) (JS `Cr` L2022/L2026) — round digits above
            // the ROUND art.
            if (kind == sf2::scene::banner_kind::round) {
                const sf2::data::font* rf = app.round_font();
                const unsigned int rtex = app.round_texture();
                if (rf != nullptr && rtex != 0) {
                    const float rscale = (64.0f * 1.6f) / 140.0f;  // round eF=140
                    app.draw_text_centered(*rf, rtex, cx,
                                           cy - art * 0.5f * scale_anim - 78.0f * scale_anim,
                                           std::to_string(fight.round().number),
                                           rscale * scale_anim, 1.0f, 1.0f, 1.0f, alpha);
                }
            }
            return;
        }
    }

    // Flat fallback: menu-font text when the callouts frame is missing.
    const char* text = fight.banner_text();
    if (text == nullptr || text[0] == '\0') return;
    const sf2::data::font* fnt = app.menu_font();
    const unsigned int tex = app.font_texture();
    if (fnt == nullptr || tex == 0) return;  // no font -> no banner

    // The base glyph scale: font-en caps are ~53px tall; the banner reads
    // big at ~1.6x, K.O. bigger still.
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float size = 1.6f;
    if (kind == sf2::scene::banner_kind::ko) {
        r = 0.95f;
        g = 0.12f;
        b = 0.10f;
        size = 2.2f;
    } else if (kind == sf2::scene::banner_kind::victory) {
        r = 1.0f;
        g = 0.82f;
        b = 0.25f;
        size = 1.9f;
    } else if (kind == sf2::scene::banner_kind::defeat) {
        r = 0.90f;
        g = 0.15f;
        b = 0.12f;
        size = 1.9f;
    }
    const float scale = size * scale_anim;
    const float y = cy - static_cast<float>(fnt->line_height) * scale * 0.5f;

    // The black drop shadow (offset ~2px per scale unit), then the text.
    const float shadow_off = 2.0f + scale * 0.8f;
    app.draw_text_centered(*fnt, tex, cx + shadow_off, y + shadow_off, text, scale,
                           0.0f, 0.0f, 0.0f, 0.75f * alpha);
    app.draw_text_centered(*fnt, tex, cx, y, text, scale, r, g, b, alpha);
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
        if (size < 1.0f) continue;
        // The REAL frame (JS `ni` L1141-1144): draw the atlas frame; the flat
        // tinted quad is the fallback ONLY on a genuine atlas/frame miss.
        const std::string frame = have_fx ? fx.frame_for(in) : std::string();
        if (!frame.empty() &&
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
// 2046x854 (sourceSize), and the node is `pos*uM + bg.fa/2`, then `-50`
// (`qe.X0a` L2144). The native stretches the backdrop to the full view, so
// the zone-space point is mapped through the view/backdrop scale.
constexpr float kMapFrameW = 2046.0f;                  // mapN sourceSize
constexpr float kMapFrameH = 854.0f;
constexpr float kMapNodeUnit = 1.5003663003663004f;    // JS L2488 qe.uM
constexpr float kMapNodeYOffset = 50.0f;               // JS L2144 d.node.ra-50
constexpr float kMapNodeSourcePx = 225.0f;             // Qr frame sourceSize
constexpr float kMapNodeScaleK = 150.0f / 225.0f;      // JS L2144 y5a()

// On-screen node size (view px): 225 * (150/225*uM) * (view_w/2046).
float map_node_size(float view_w) {
    return kMapNodeSourcePx * kMapNodeScaleK * kMapNodeUnit * (view_w / kMapFrameW);
}

// The zone map from stages.xml (JS `p.Dkb` L188 / `Ckb` L189): Zone Name +
// FileName + Start flag, with Battle children (Name/Type/X/Y/Location).
// Only battles carrying map coordinates become nodes (HIDDEN/INTERMISSION
// rows without X/Y are not map nodes). Covers all 8 zones (Punchbag +
// ZONE_1..7) — the old first-zone-only loader is folded into this.
std::vector<MapScreen::ZoneTab> load_zone_map(float view_w, float view_h) {
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
                // X/Y); rows without coordinates are not selectable.
                if (battle.attribute("X").empty() && battle.attribute("Y").empty()) continue;
                MapScreen::Node n;
                n.name = battle.attribute("Name").value();
                if (n.name.empty()) continue;
                n.type = battle.attribute("Type").value();
                n.zone = z.name;
                n.location = battle.attribute("Location").value();
                const float x = sf2::data::xml_attr_float(battle, "X", 0.0f);
                const float y = sf2::data::xml_attr_float(battle, "Y", 0.0f);
                // Per-node art suffix (JS L205: Icon attr, default "training";
                // `Lc.U9a` L1405 -> base_/active_/... + icon).
                n.icon = battle.attribute("Icon").value();
                if (n.icon.empty()) n.icon = "training";
                // JS `qe.X0a` (L2144): x = pos.x*uM + bg.fa.x/2,
                // y = -pos.y*uM + bg.fa.y/2, then -50. Mapped to the
                // full-view-stretched backdrop.
                const float sx = view_w / kMapFrameW;
                const float sy = view_h / kMapFrameH;
                n.x = (x * kMapNodeUnit + kMapFrameW * 0.5f) * sx;
                n.y = (-y * kMapNodeUnit + kMapFrameH * 0.5f - kMapNodeYOffset) * sy;
                n.active = true;  // the MapScreen ctor applies the lock rule
                // Xs warriors (FLOW_STATIC Modes): FirstNames across the
                // battle's Fights, deduped, capped (bracket display).
                for (pugi::xml_node fight = battle.child("Fight"); fight;
                     fight = fight.next_sibling("Fight")) {
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
std::vector<sf2::scene::StageRule> battle_fight_rules(const std::string& battle_name) {
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
        for (const pugi::xml_node zone : root.child("Zones").children("Zone")) {
            for (const pugi::xml_node battle : zone.children("Battle")) {
                if (std::string(battle.attribute("Name").value()) != battle_name) continue;
                const pugi::xml_node fight = battle.child("Fight");
                if (!fight) return out;
                const pugi::xml_node rules = fight.child("Rules");
                if (!rules) return out;
                for (const pugi::xml_node r : rules.children()) {
                    sf2::scene::StageRule rule;
                    rule.tag = r.name();
                    for (const pugi::xml_attribute a : r.attributes()) {
                        rule.attrs[a.name()] = a.value();
                    }
                    out.push_back(std::move(rule));
                }
                return out;
            }
        }
    } catch (const std::exception&) {
    }
    return out;
}

// The player's (type, subtype) items for the Locks move list: the equipped
// slots (JS `xc.hk`) + the owned inventory (JS `p.o.xa`).
std::vector<std::pair<std::string, std::string>> owned_items(App& app) {
    std::vector<std::pair<std::string, std::string>> out;
    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return out;
    }
    const std::vector<CatalogItem> catalog = load_full_catalog(app);
    const auto subtype_of = [&catalog](const std::string& name) {
        for (const CatalogItem& ci : catalog) {
            if (ci.name == name) return ci.subtype;
        }
        return std::string();
    };
    for (const std::string& slot : {w.weapon, w.armor, w.helm}) {
        const std::string st = subtype_of(slot);
        if (!st.empty()) {
            for (const CatalogItem& ci : catalog) {
                if (ci.name == slot) {
                    out.emplace_back(ci.type, st);
                    break;
                }
            }
        }
    }
    for (const auto& oi : w.items) {
        if (oi.count <= 0) continue;
        for (const CatalogItem& ci : catalog) {
            if (ci.name == oi.name && !ci.subtype.empty()) {
                out.emplace_back(ci.type, ci.subtype);
                break;
            }
        }
    }
    // The fighter's Skeleton (the Skeleton lock passes for every move — the
    // JS fighter always owns the Skeleton item, `users_default` has
    // Skeleton="Skeleton").
    out.emplace_back("Skeleton", "Skeleton");
    // The default Fists (the unarmed weapon subtype).
    out.emplace_back("Weapon", "Fists");
    return out;
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
                cached = shop_items(all);
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
        // EN only for now: the RU string table (ru.<hash>.xml) IS shipped,
        // and so is a Cyrillic BMF font (res/ui/font-ru.32eaddc0.fnt +
        // font-ru.3338e715.png — PORT_AUDIT_UI §0.3 corrects the old
        // "no ru.fnt on disk" claim). The blocker is App::init, which
        // hardcodes font-en.7043b83b.fnt (app.cpp) — outside this file.
        // Selecting RU therefore needs the app-level font swap
        // (`asset id 264 = ui/font{lang}.png`); OPEN until then.
        if (path.empty()) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name.size() > 7 && name.rfind("en.", 0) == 0 &&
                    entry.path().extension().string() == ".xml") {
                    path = entry.path().string();
                    break;
                }
            }
        }
        if (path.empty()) return;
        lang_table_load(app.res_root(), path);
        std::fprintf(stdout, "[lang] loaded %s\n", path.c_str());
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
// Finds the stance-idle clip (moves.xml `StanceIdle` FileName
// "stance_idle.bytes"): first clip whose archive name contains it, else any
// "stance" clip, else "" (caller skips the figure).
std::string find_idle_clip_name(
    const std::map<std::string, sf2::data::anim_clip>& clips) {
    for (const auto& kv : clips) {
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
void draw_dojo_figure(sf2::render::Renderer& ren, const sf2::render::Camera& camera,
                      const sf2::scene::Fighter& fighter) {
    const float r = fighter.color_r(), g = fighter.color_g(), b = fighter.color_b();
    std::vector<float> verts;
    fighter.build_vertices(verts);
    std::vector<float> pv(verts.size());
    for (std::size_t i = 0; i < verts.size(); i += 2) {
        pv[i] = camera.world_to_screen_x(verts[i], 1.0f);
        pv[i + 1] = camera.world_to_screen_y(verts[i + 1]);
    }
    if (!pv.empty()) {
        ren.draw_triangles(pv.data(), pv.size() / 2, r, g, b, 1.0f);
    }
    const sf2::scene::Model& model = fighter.model();
    std::unordered_map<std::string, float> edge_max;
    edge_max.reserve(model.capsules.size() * 2u);
    for (const sf2::scene::Capsule& cap : model.capsules) {
        auto it = edge_max.find(cap.edge);
        if (it == edge_max.end() || cap.radius1 > it->second) {
            edge_max[cap.edge] = cap.radius1;
        }
    }
    constexpr float kPi = 3.14159265358979323846f;
    constexpr int kDiscSegments = 12;
    for (const auto& kv : edge_max) {
        const sf2::scene::EdgeDef* edge = nullptr;
        for (const sf2::scene::EdgeDef& ed : model.edges) {
            if (ed.name == kv.first) {
                edge = &ed;
                break;
            }
        }
        if (edge == nullptr) continue;
        const int i1 = model.bone_by_name(edge->end1);
        const int i2 = model.bone_by_name(edge->end2);
        if (i1 < 0 || i2 < 0) continue;
        const std::vector<float>& pos = fighter.positions();
        const std::size_t u1 = static_cast<std::size_t>(i1) * 2;
        const std::size_t u2 = static_cast<std::size_t>(i2) * 2;
        if (u1 + 1 >= pos.size() || u2 + 1 >= pos.size()) continue;
        const float stroke = kv.second * 2.0f * camera.zoom;
        if (stroke <= 0.0f) continue;
        const float sx1 = camera.world_to_screen_x(pos[u1], 1.0f);
        const float sy1 = camera.world_to_screen_y(pos[u1 + 1]);
        const float sx2 = camera.world_to_screen_x(pos[u2], 1.0f);
        const float sy2 = camera.world_to_screen_y(pos[u2 + 1]);
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

DojoScreen::DojoScreen(ScreenManager& mgr) : Screen(mgr, "Dojo") {
    // Menu music (JS `lb.OS` -> `ta.Ut("menu")`, L1276-1277).
    sf2::audio::AudioEngine::instance().play_music("menu");
    // No bespoke ctor art: the JS hub is the `FightNone` ModelViewer over
    // the dojo layer stack + the shared `za` chrome (Tf L1969-1972). The
    // FIGHT/MAP/SHOP/PROFILE 4-up row, punchbag, gear and disciple chrome
    // were native inventions (PORT_AUDIT_UI §2.1-2.2) — navigation is the
    // `za` vertical nav column (draw_za_chrome / za_update below).
}

void DojoScreen::update_impl(float dt) {
    idle_frame_++;  // drives the idle-stance frame cycle (display only)
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
    // Sensei modal gate (quest He records): while a dialog is up, taps
    // advance it instead of the chrome (headless auto-drains).
    if (quest_modal_consume(app())) return;
    // The shared `za` nav column (JS `za.Aub` L1978-1980 / `za.Ofb`..`Vfb`):
    // a tap switches to Dojo/Map/Shop/Profile/Settings (JS `ma.Jg().jI`).
    za_update(app(), *this, kScreenDojo);
}

// Dojo location ensure (mirrors the FightScreen dojo-location block:
// params scan + LocationScene load + webp texture resolve + frame
// aliasing). The fight loads it in its ctor; the Dojo hub needs it at
// boot (otherwise the hub renders black). Guarded by layers().empty().
void ensure_dojo_location(App& app) {
    if (!app.has_fight_assets()) return;
    FightAssets& assets = app.fight_assets();
    if (!assets.dojo.layers().empty()) return;
    const std::string loc_dir = app.res_root() + "/locations/dojo";
    std::string params_xml, atlas_json;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(loc_dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("dojo_params.", 0) == 0 && name.size() > 4 &&
                name.substr(name.size() - 4) == ".xml") {
                params_xml = entry.path().string();
            } else if (name.rfind("dojo.", 0) == 0 && name.size() > 5 &&
                       name.substr(name.size() - 5) == ".json") {
                atlas_json = entry.path().string();
            }
        }
        assets.dojo.load(params_xml, {atlas_json}, app.res_root());
        // [DOJO-HUB] Hide the punchbag holder prop. The params carry it as
        // a black-tinted Image (ClassName="dojo_punch_bag_holder", hook+beam
        // art at world (-10,-203.5) -> screen x621-1019 y4-190); the loader
        // path is JS-exact (ujb tint via Na.cd with alpha 1 + R3a placement
        // + NWa paint order), but the oracle hub paints clean wall there —
        // oracle_dojo_norm.png y100-140 x660-940 = (160,129,93) with no hook
        // or beam trace, while the port's black-tinted opaque beam would
        // paint a ~390x20 black bar (measured: port sky band y96-168
        // x400-900 = (52,37,33) vs oracle (130,96,70)). The hide is
        // therefore hub-level: drop the sprite here (hub-only; the
        // FightScreen location block keeps it, so fight rendering and all
        // other locations are untouched).
        for (const auto& layer : assets.dojo.layers()) {
            auto& sprites = layer->sprites;
            sprites.erase(std::remove_if(sprites.begin(), sprites.end(),
                                         [](const std::shared_ptr<sf2::scene::Sprite>& s) {
                                             return s != nullptr &&
                                                    s->texture_name == "dojo_punch_bag_holder";
                                         }),
                          sprites.end());
        }
        const std::string loc_prefix = "dojo.";
        for (const auto& entry : std::filesystem::directory_iterator(loc_dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(loc_prefix, 0) != 0) continue;
            const std::string ext = entry.path().extension().string();
            if (ext != ".webp" && ext != ".png" && ext != ".jpg") continue;
            sf2::data::Texture tex;
            if (!sf2::data::decode_texture(entry.path().string(), tex)) continue;
            const GLuint gl = app.renderer().texture_for("dojo_atlas_" + name, tex);
            if (gl != 0) {
                std::ifstream in(atlas_json, std::ios::binary);
                std::vector<std::uint8_t> jb((std::istreambuf_iterator<char>(in)),
                                             std::istreambuf_iterator<char>());
                const sf2::data::atlas a =
                    sf2::data::atlas_parse(jb.data(), jb.size());
                for (const auto& fr : a.frames) {
                    app.renderer().texture_alias(fr.name, gl);
                }
                std::fprintf(stdout, "[dojo] location atlas texture: %s (%dx%d, %zu frames)\n",
                             entry.path().filename().string().c_str(), tex.w, tex.h,
                             a.frames.size());
                std::fflush(stdout);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[dojo] location load failed: %s\n", e.what());
    }
}

// Dojo gamepad (display only): the oracle Dojo shows the joystick +
// punch/kick buttons (same ui/controller frames as the fight pad, norm
// state — no input handling on the hub).
void draw_dojo_gamepad(App& app) {
    const GamepadLayout pad;
    const float base_size = pad.joy_r * 2.0f;
    const float knob_size = pad.knob_r * 2.0f;
    // Draw-size 0.9 (centers + hit radii untouched: JS fu Si(120,28)/
    // fh(-50,198) + uab 115): the 200px art at full hit-diameter left a
    // 4px gap that read as touching; 0.9 leaves a 12px gap.
    const float btn_size = pad.btn_r * 2.0f * 0.9f;
    try_draw_atlas_button(app, "JoystickContainer_norm", pad.joy_cx, pad.joy_cy,
                          base_size, base_size, 1.0f);
    try_draw_atlas_button(app, "Joystick_norm", pad.joy_cx, pad.joy_cy, knob_size,
                          knob_size, 1.0f);
    try_draw_atlas_button(app, "btn_punch_normal", pad.punch_cx, pad.punch_cy,
                          btn_size, btn_size, 1.0f);
    try_draw_atlas_button(app, "btn_kick_normal", pad.kick_cx, pad.kick_cy,
                          btn_size, btn_size, 1.0f);
}

void DojoScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    ensure_dojo_location(app);
    // NOTE: the shared `za` chrome draws AFTER the scene (see below) —
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
    if (app.has_fight_assets()) {
        FightAssets& assets = app.fight_assets();
        // Hub framing = the LIVE viewer CoM midpoint (JS `Tf.Ea` L1972 runs
        // the `FightNone` viewer through `Sya`; `Ut.Al` L826 recomputes
        // `Io = Lb.width/2 - focus` from `Go.ma` every frame). The two
        // viewers' CoMs are the player idle figure and the Punchbag dummy:
        // `Fighter::sample` (fighter.hpp L207) anchors each COM bone at the
        // passed (x,y), and neither viewer moves on the hub, so the live CoMs
        // are their spawns. Container -> location: `+arenaW/2`.
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
            const float half = assets.dojo.arena_width() * 0.5f;
            const float player_x = assets.dojo.player_spawn_x() - half;
            const float enemy_x = assets.dojo.enemy_spawn_x() - half;
            focus_x = (player_x + enemy_x) * 0.5f + half;
            fighter_span = std::fabs(enemy_x - player_x);
        }
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
    // ModelViewer through the live `Sya` framing) -------------------------
    // The idle figure is the player at the location's ModelsViewer spawn
    // (dojo 690,-93 — Bf.zjb L476), projected through the SAME hub camera
    // the location layers use, plus the fighter container transform
    // (tl.init L843: translate.x = -width/2, translate.y = height/2 - floor).
    // NOT the old hand-placed kFigX/kFeetY capsule (PORT_AUDIT_UI §2.2).
    // Display only — no fight logic runs.
    {
        const float arena_half = app.has_fight_assets()
                                     ? app.fight_assets().dojo.arena_width() * 0.5f
                                     : 980.0f;
        const float cont_y = app.has_fight_assets()
                                 ? app.fight_assets().dojo.arena_height() * 0.5f -
                                       app.fight_assets().dojo.arena_floor()
                                 : 200.0f;
        if (!dojo_fig_tried_) {
            dojo_fig_tried_ = true;
            if (app.has_fight_assets()) {
                FightAssets& assets = app.fight_assets();
                const std::string idle_name = find_idle_clip_name(assets.clips);
                const auto it = idle_name.empty() ? assets.clips.end()
                                                  : assets.clips.find(idle_name);
                if (!assets.merged.bones.empty() && it != assets.clips.end() &&
                    !it->second.frames.empty()) {
                    dojo_fighter_ = std::make_unique<sf2::scene::Fighter>();
                    dojo_fighter_->set_model(assets.merged);
                    dojo_fighter_->set_color(assets.dojo.root_color());
                    dojo_idle_ = &it->second;
                    dojo_fig_ok_ = true;
                    std::fprintf(stdout, "[dojo] idle figure ready (clip %s, frames %zu)\n",
                                 idle_name.c_str(), it->second.frames.size());
                    std::fflush(stdout);
                } else {
                    std::fprintf(stdout, "[dojo] idle figure skipped (no stance clip/model)\n");
                    std::fflush(stdout);
                }
            }
        }
        // [OPEN] The hub's enemy (the Punchbag dummy, `merged_bag`) is not
        // drawn. 876a3a97 added a bind-pose draw here; it rendered nothing
        // (isolated: bag ON vs OFF = +0.03pp, oracle punchbag still absent),
        // so it was removed — the pre-regression baseline had no bag either.
        // A correct draw needs the bag's real idle clip / COM anchor verified
        // against the oracle (DOJO_BG_STATIC §1/§6), not a 0-bone synthetic.
        if (dojo_fig_ok_ && dojo_fighter_ != nullptr && dojo_idle_ != nullptr &&
            !dojo_idle_->frames.empty() && have_hub_cam) {
            const int nframes = static_cast<int>(dojo_idle_->frames.size());
            const int fr = (idle_frame_ / 10) % nframes;  // slow idle cycle
            const float spawn_x =
                (app.has_fight_assets() ? app.fight_assets().dojo.player_spawn_x() : 690.0f) -
                arena_half;
            const float spawn_y =
                (app.has_fight_assets() ? app.fight_assets().dojo.player_spawn_y() : -93.0f) +
                cont_y;
            dojo_fighter_->sample(*dojo_idle_, fr, spawn_x, spawn_y, 1);
            draw_dojo_figure(ren, hub_cam, *dojo_fighter_);
        }
        draw_dojo_gamepad(app);
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
        if (!modal_up) {
            const QuestStep qs = quest_step_for_state(
                app.res_root(),
                quest_state_for(tutorial_, story_step_, training_won_, level_, map_focus_,
                                battles_, {}));
            draw_ib_hint(app, ren, qs.speaker, qs.line1, qs.line2, /*show_ok=*/false);
        }
    }
    // The JS hub carries no entry-button row: the Dojo 4-up row, the gear
    // and the disciple chrome were native inventions (PORT_AUDIT_UI
    // §2.1-2.2). Navigation is the `za` nav column drawn in the aliveness
    // block above.
    // Sensei dialog modal on top of everything Dojo.
    draw_quest_modal(app, ren, app.screens().top() == this);
}

// ---------------------------------------------------------------------------
// MapScreen
// ---------------------------------------------------------------------------

MapScreen::MapScreen(ScreenManager& mgr) : Screen(mgr, "Map") {
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
    zone_sel_ = 0;
    for (std::size_t i = 0; i < zones_.size(); ++i) {
        if (zones_[i].name == cur) zone_sel_ = static_cast<int>(i);
    }
    // WDa LIVE (JS L256 `WDa(a) = iF.get(a) != null`, written by `J1a` L259 /
    // `Iaa` L260-261 and recorded by ResultsScreen): a zone is open when it
    // is Start, is at/before the current zone (past zones open, current
    // playable), or holds ANY recorded battle; later unrecorded zones lock.
    // Record keys here are bare battle names matched against node names (JS
    // keys zone|loc — same open/locked verdict at zone granularity).
    for (std::size_t i = 0; i < zones_.size(); ++i) {
        bool touched = zones_[i].is_start;
        if (!touched) {
            for (const auto& n : zones_[i].nodes) {
                if (map_save.has_battle(n.name)) {
                    touched = true;
                    break;
                }
            }
        }
        zones_[i].locked = !touched && !zones_[i].is_start &&
                           static_cast<int>(i) > zone_sel_ && zones_[i].name != cur;
        for (auto& n : zones_[i].nodes) n.active = !zones_[i].locked;
    }
    // MapFocus (JS `Ya.bKa` L2129 focuses the save's MapFocus `p.o.ys` via
    // `m5`): highlight the node named in MapFocus first (e.g.
    // ZONE_1|BOSS_LYNX|1 quest focus `qo` L1086), else the first BOSSES
    // node, else the first node (hover highlight only, no selection).
    hover_ = -1;
    if (zone_sel_ >= 0 && static_cast<std::size_t>(zone_sel_) < zones_.size()) {
        const auto& focus_nodes = zones_[zone_sel_].nodes;
        if (!focus.empty()) {
            for (std::size_t i = 0; i < focus_nodes.size(); ++i) {
                if (focus.find(focus_nodes[i].name) != std::string::npos) {
                    hover_ = static_cast<int>(i);
                    break;
                }
            }
        }
        for (std::size_t i = 0; hover_ < 0 && i < focus_nodes.size(); ++i) {
            if (focus_nodes[i].type == "BOSSES") {
                hover_ = static_cast<int>(i);
                break;
            }
        }
        if (hover_ < 0 && !focus_nodes.empty()) hover_ = 0;
    }
    std::fprintf(stdout, "[map] %zu zones loaded (current %s)\n", zones_.size(), cur.c_str());
    for (const auto& z : zones_) {
        std::fprintf(stdout, "[map] zone %s (%s)%s: %zu nodes%s\n", z.name.c_str(),
                     z.file.c_str(), z.is_start ? " [start]" : "", z.nodes.size(),
                     z.locked ? " [locked]" : "");
    }
    std::fflush(stdout);
}

void MapScreen::launch_battle(const Node& n) {
    // JS `Ya` battle-start (L2131-2132): `wa.F().mp(6, battle)`.
    // Carry the battle into the pending flow: name/location +
    // the reward (the first non-zero <Reward>).
    PendingBattle& pb = app().pending_battle();
    pb.battle_name = n.name;
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

void MapScreen::update_impl(float dt) {
    (void)dt;
    // Sensei modal gate (quest He records): while up, taps advance the
    // dialog instead of tabs/nodes/BACK (headless auto-drains).
    if (quest_modal_consume(app())) return;
    const App::PointerState& p = app().pointer();
    // Boss-intro act (Rd machine): ticks here; taps skip; completion
    // launches the armed battle. Tabs/nodes/BACK wait below.
    if (act_pending_) {
        act_.tick(dt, p.pressed);
        if (act_.done()) {
            act_pending_ = false;
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
    hover_ = -1;
    for (std::size_t i = 0; i < zones_[zone_sel_].nodes.size(); ++i) {
        const Node& n = zones_[zone_sel_].nodes[i];
        const float node_half = map_node_size(kViewW) * 0.5f;
        if (p.x >= n.x - node_half && p.x <= n.x + node_half &&
            p.y >= n.y - node_half && p.y <= n.y + node_half) {
            hover_ = static_cast<int>(i);
            if (p.pressed && n.active) {
                // Boss multi-intro (JS `hCa` L431-432: BOSSES fights carry
                // the lD intro list; played through the Rd act machine): arm
                // the act first, launch on completion. Headless bypasses
                // straight into the fight (loop pins Fight screens).
                if (!app().headless() &&
                    (n.type == "BOSSES" || n.type == "BOSSES_REPLAYABLE")) {
                    std::vector<ActLine> lines;
                    if (zone_sel_ >= 0 &&
                        static_cast<std::size_t>(zone_sel_) < zones_.size()) {
                        for (const Node& b : zones_[zone_sel_].nodes) {
                            if (b.type == "BOSSES" || b.type == "BOSSES_REPLAYABLE") {
                                ActLine ln;
                                ln.text = b.name;
                                ln.seconds = 2.5f;
                                lines.push_back(ln);
                            }
                        }
                    }
                    if (lines.empty()) {
                        ActLine ln;
                        ln.text = n.name;
                        ln.seconds = 2.5f;
                        lines.push_back(ln);
                    }
                    act_node_ = n;
                    act_pending_ = true;
                    act_.start(lines, false);
                    std::fprintf(stdout, "[map] boss intro act armed (%zu intros, first %s)\n",
                                 lines.size(), n.name.c_str());
                    std::fflush(stdout);
                } else {
                    launch_battle(n);
                }
            }
            // One battle per tap (JS buttons are exclusive — the topmost node
            // fires, not every node under the pointer). ZONE_1 has coincident
            // nodes (BOSS_LYNX / *_INTERMISSION / BOSS_HARDMODE at the same
            // X/Y); without this the click stacks several Fight screens and
            // the Results pop lands back on a leftover fight.
            if (p.pressed) break;
        }
    }
    // Shared `za` nav column (JS `ma.D1`): Dojo/Shop/Profile/Settings hops.
    za_update(app(), *this, kScreenMap);
}

void MapScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    load_map_backdrops(app);  // once; silent unless frames decode
    // Per-zone backdrop (res/map/part0..6 — FLOW_STATIC.md §2.1): the
    // selected zone's "map<N>" art full-bleed when its texture decoded,
    // else the dojo sprite, else flat. Zone→part assumes file order
    // (ZONE_1→part0 … ZONE_7→part6); the Start zone keeps the dojo art.
    // NOTE: part textures ship as ASTC ktx / crunch dds (not CPU-decodable),
    // so the fallback path is the live one until the pipeline decodes them.
    bool bg_done = false;
    if (zone_sel_ >= 0 && static_cast<std::size_t>(zone_sel_) < zones_.size() &&
        zones_[zone_sel_].part >= 0) {
        bg_done = app.draw_atlas_rect(
            "map" + std::to_string(zones_[zone_sel_].part), 0.0f, 0.0f, kViewW, kViewH,
            1.0f);
    }
    if (bg_done) {
        // Backdrop art already covers the view (nodes/tabs/BACK below).
    } else {
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        sf2::render::Camera ui_cam;
        ui_cam.center_x = kViewW * 0.5f;
        ui_cam.center_y = kViewH * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = kViewW;
        ui_cam.view_h = kViewH;
        ui_cam.arena_h = kViewH;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = kViewW * 0.5f;
        ren.draw_sprite(*dojo, ui_cam);
    } else {
        const float verts[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(verts, 6, 0.08f, 0.1f, 0.14f, 1.0f);
    }
    }  // else (!bg_done): dojo sprite / flat fallback above
    // JS `Ya` has no zone tab strip (PORT_AUDIT_UI 2.3). Zone nav is the
    // `Vr` scroller + `Rr`/`Xr` panels (OPEN - not ported).
    const bool zone_ok =
        zone_sel_ >= 0 && static_cast<std::size_t>(zone_sel_) < zones_.size();
    const std::size_t node_count = zone_ok ? zones_[zone_sel_].nodes.size() : 0;
    // Series line (FLOW_STATIC Modes): tournament won/total + next name from
    // the zone's TOURNAMENT nodes and save wins; survival best likewise.
    // Xs/EQ kits and Rk wave rotation are fight-setup concepts with no
    // shell-visible state — position served from records (see report).
    if (zone_ok) {
        int tour_total = 0, tour_won = 0;
        std::string tour_next;
        int surv_best = 0;
        bool has_surv = false;
        for (const Node& zn : zones_[zone_sel_].nodes) {
            int wins = 0;
            for (const auto& fw : fight_wins_) {
                if (fw.name == zn.name) {
                    wins = fw.wins;
                    break;
                }
            }
            if (zn.type == "TOURNAMENT") {
                ++tour_total;
                if (wins > 0) ++tour_won;
                else if (tour_next.empty()) tour_next = zn.name;
            } else if (zn.type == "SURVIVAL") {
                has_surv = true;
                if (wins > surv_best) surv_best = wins;
            }
        }
        std::string series = zones_[zone_sel_].name;
        if (tour_total > 0) {
            series += " | TOURNAMENT " + std::to_string(tour_won) + "/" +
                      std::to_string(tour_total);
            if (!tour_next.empty()) series += " NEXT " + tour_next;
        }
        if (has_surv) {
            series += " | SURVIVAL BEST " + std::to_string(surv_best);
        }
        draw_ui_label(app, 130.0f, 76.0f, 1020.0f, 22.0f, series, 0.7f,
                          UiAlign::Left, 0.9f, 0.9f, 0.9f);
    }
    const float node_px = map_node_size(kViewW);
    for (std::size_t i = 0; i < node_count; ++i) {
        const Node& n = zones_[zone_sel_].nodes[i];
        const bool hovered = static_cast<int>(i) == hover_;
        const bool locked = !n.active;
        // JS `Qr` (L2092-2095): frame = "BattleBtn<State>/<suffix>", suffix
        // base_/active_/locked_/locked_active_/pressed_ + Icon (`Lc.*` L2482,
        // `U9a..X9a` L1405). Hover swaps to Active; locked uses BattleBtnLock*.
        const std::string base = std::string(locked ? "BattleBtnLock/locked_" : "BattleBtnBase/base_") + n.icon;
        const std::string active = std::string(locked ? "BattleBtnLockActive/locked_active_" : "BattleBtnActive/active_") + n.icon;
        const char* tex_frame = hovered ? active.c_str() : base.c_str();
        bool drawn = try_draw_atlas_button(app, tex_frame, n.x, n.y, node_px, node_px, 1.0f);
        if (!drawn && locked) {
            // Some icons carry no lock art (buttons.json Lock=21 vs Base=34):
            // fall back to the unlocked base frame, then flat.
            const std::string lock_base = std::string("BattleBtnBase/base_") + n.icon;
            drawn = try_draw_atlas_button(app, lock_base.c_str(), n.x, n.y, node_px, node_px, 0.6f);
        }
        if (!drawn) {
            const float r = hovered ? 0.9f : (n.active ? 0.6f : 0.3f);
            const float g = hovered ? 0.5f : (n.active ? 0.4f : 0.3f);
            const float b = hovered ? 0.3f : (n.active ? 0.25f : 0.3f);
            const float x0 = n.x - node_px / 2, y0 = n.y - node_px / 2;
            const float verts[] = {x0, y0, x0 + node_px, y0, x0 + node_px, y0 + node_px, x0, y0, x0 + node_px, y0 + node_px, x0, y0 + node_px};
            ren.draw_triangles(verts, 6, r, g, b, n.active ? 0.95f : 0.5f);
        }
        // Label `cC` (JS L2093): `Fa(100,55)`, `ua(60)`, `C(-50)`, `D(65)`,
        // black. Node-local -> screen at the node scale (node_px/150).
        const float ls = node_px / 150.0f;
        draw_ui_label(app, n.x - 50.0f * ls - 50.0f * ls, n.y + 65.0f * ls - 27.5f * ls,
                      100.0f * ls, 55.0f * ls, n.name, 0.6f, UiAlign::Center, 0.0f, 0.0f,
                      0.0f);
    }
    // The BACK button (top-left) — JS-exact misc `Arrow` (y.sRa) + flat fallback.
    // OPEN: no dedicated back frame in JS (btn_back 0 hits); Arrow is the
    // misc-atlas nav arrow used for back navigation.
    if (!try_draw_atlas_button(app, "Arrow", 64.0f, 40.0f, 88.0f, 48.0f, 1.0f)) {
        draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
        draw_ui_label(app, 64.0f - 44.0f + 6.0f, 40.0f - 10.0f, 88.0f - 12.0f, 20.0f,
                          "BACK", 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        draw_ui_label(app, 64.0f - 44.0f + 6.0f, 40.0f - 10.0f, 88.0f - 12.0f, 20.0f,
                          "BACK", 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    }
    // Shared `za` chrome (JS `ma.D1`): topPanel + widgets + vertical nav.
    draw_za_chrome(app, kScreenMap);
    // Boss-intro act overlay (Rd machine over the lD multi-intro list;
    // boss names are display strings, shown raw). Skippable by tap.
    // Boss-intro act overlay: skipped while a quest modal is up (same
    // exclusivity rule as Dojo - single voice, modal wins).
    if (act_pending_ && act_.active() && quest_modal_top(app) == nullptr) {
        const float a = act_.fade();
        if (a > 0.01f) {
            const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH,
                                 0, 0, kViewW, kViewH, 0, kViewH};
            ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, a);
        }
        // `label = ea` (L2095): geometry `Fa(N.width/this.node.Eb*
        // (.9+(N.lc-.4)/1.6*-.5), 400)` + `ua(130)` (L2096; menu eF=100 ->
        // native scale 1.3) + color `Na.cd(13743222)` = (0.82,0.71,0.46).
        // Shown from the node fade-in (step 1) through the timed lines
        // (step 3). Replaces the invented flat "BOSS"/"TAP TO SKIP" text
        // (PORT_AUDIT_UI §3 item 15).
        const std::string line = act_.label();
        if (!line.empty()) {
            const float lc = kViewW / kViewH;
            const float label_w = kViewW * (0.9f + (lc - 0.4f) / 1.6f * -0.5f);
            const float label_h = 400.0f;
            const float label_x = (kViewW - label_w) * 0.5f;
            const float label_y = (kViewH - label_h) * 0.5f;  // box height-centred
            draw_ui_label(app, label_x, label_y, label_w, label_h, line, 1.3f,
                          UiAlign::Center, 0.82f, 0.71f, 0.46f);
        }
    }
    // Sensei dialog modal on top of the map.
    draw_quest_modal(app, ren, app.screens().top() == this);
}

// ---------------------------------------------------------------------------
// FightScreen
// ---------------------------------------------------------------------------

FightScreen::FightScreen(ScreenManager& mgr, const std::string& battle_name,
                         const std::string& location, int reward_money, int reward_exp,
                         const std::vector<std::pair<std::string, std::string>>& owned)
    : Screen(mgr, "Fight"),
      battle_name_(battle_name),
      location_(location),
      reward_money_(reward_money),
      reward_exp_(reward_exp) {
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
    }

    // The on-screen gamepad art (JS `Za`): the ui/controller atlas
    // (Joystick*, btn_punch_*, btn_kick_*). Loaded once per process; the
    // frames register into the app's atlas cache for draw_gamepad.
    (void)load_controller_atlas(app());

    // The dojo location (the tutorial-zone backdrop). Load once; the
    // FightAssets keeps it.
    if (assets.dojo.layers().empty()) {
        const std::string loc_dir = app().res_root() + "/locations/" + location_;
        std::string params_xml, atlas_json;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(loc_dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(location_ + "_params.", 0) == 0 &&
                    name.size() > 4 && name.substr(name.size() - 4) == ".xml") {
                    params_xml = entry.path().string();
                } else if (name.rfind(location_ + ".", 0) == 0 &&
                           name.size() > 5 && name.substr(name.size() - 5) == ".json") {
                    atlas_json = entry.path().string();
                }
            }
            assets.dojo.load(params_xml, {atlas_json}, app().res_root());
            // [FIX Phase 4a/4b - dojo texture resolve] The atlas texture for
            // the location is the IMAGE beside the JSON (`dojo.b920e18e.webp`),
            // NOT the JSON path - the JSON's hash stem (`dojo.d31b1e71`)
            // does not match the image's (`dojo.b920e18e`). The old code
            // derived the image path from the JSON stem
            // (`dojo.d31b1e71.webp`, does not exist) -> no texture uploaded
            // -> every location sprite rendered as a black solid -> the whole
            // dojo black + the black fighters invisible. Resolve the image by
            // the LOCATION prefix (`dojo.*`) like the scene_probe.
            const std::string loc_prefix = location_ + ".";
            for (const auto& entry : std::filesystem::directory_iterator(loc_dir)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(loc_prefix, 0) != 0) continue;
                const std::string ext = entry.path().extension().string();
                if (ext != ".webp" && ext != ".png" && ext != ".jpg") continue;
                sf2::data::Texture tex;
                if (!sf2::data::decode_texture(entry.path().string(), tex)) continue;
                const GLuint gl = app().renderer().texture_for("dojo_atlas_" + name, tex);
                if (gl != 0) {
                    std::ifstream in(atlas_json, std::ios::binary);
                    std::vector<std::uint8_t> jb(
                        (std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
                    const sf2::data::atlas a =
                        sf2::data::atlas_parse(jb.data(), jb.size());
                    for (const auto& fr : a.frames) {
                        app().renderer().texture_alias(fr.name, gl);
                    }
                    std::fprintf(stdout, "[fight] dojo atlas texture: %s (%dx%d, %zu frames)\n",
                                 entry.path().filename().string().c_str(), tex.w, tex.h,
                                 a.frames.size());
                }
                break;  // one atlas image per location
            }
            std::fprintf(stdout, "[fight] dojo scene: %zu layers, arena %.0fx%.0f\n",
                         assets.dojo.layers().size(), assets.dojo.arena_width(),
                         assets.dojo.arena_height());
            // [FIX Phase 4b — the floor the fighters stand on] The dojo's
            // `dojo_floor_*` atlas sprites are white frames tinted black by
            // the params `Color="0x000000"` — they render as a pure-black
            // strip where the fighters stand, making the black silhouettes
            // invisible ("no body"). The oracle's fighter-zone floor is a
            // warm wooden floor (~0xC77946); tint the floor sprites warm so
            // the black fighters are visible on it.
            for (const auto& layer : assets.dojo.layers()) {
                for (const auto& s : layer->sprites) {
                    if (s->texture_name.rfind("dojo_floor_", 0) == 0) {
                        s->color_r = 0xC7 / 255.0f;
                        s->color_g = 0x79 / 255.0f;
                        s->color_b = 0x46 / 255.0f;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[fight] dojo scene load failed: %s\n", e.what());
        }
    }

    const float arena_w = assets.dojo.arena_width() > 0.0f ? assets.dojo.arena_width() : 1960.0f;
    const float wall = 80.0f;
    // Arena bounds are [wall, arena_w-wall] = [80,1880] (0-based, JS Bf NU/width).
    // The centered ±900 calculation placed spawn 973 outside the arena (clamped to 900,
    // causing |dx| ~211 vs oracle). Reverted to JS-accurate bounds.
    const float wall_min = wall;
    const float wall_max = arena_w - wall;

    sf2::scene::BattleParams battle;
    battle.name = battle_name_;
    battle.type = "FightNone";
    battle.location = location_;
    battle.rounds = 2;
    battle.round_time = 99;
    battle.health_recovery = 1.0f;
    // [FIX Phase 4a — spawn sides] The dojo_params ModelsViewer places
    // PlayerPositionX=690 (left) / EnemyPositionX=973 (right) — but the
    // ORACLE trace (reference/traces/console.log) shows the PLAYER at the
    // RIGHT (P:972.954) and the ENEMY (Punchbag) at the LEFT (E:690.000):
    //   F0|1|0|0|P:972.954,-108.114,1,,1,NAME_SHADOW|E:690.000,-93.000,1,,1,Punchbag
    // The ModelsViewer "Player"/"Enemy" labels are reversed relative to
    // the fight roles (the viewer's "Player" = the left Punchbag = the
    // fight ENEMY). The old code spawned the player LEFT — the fighters
    // appeared on the wrong sides ("in nowhere" + mirrored).
    // [FIX Phase 4a — fighters on the floor] The spawn Y is the ModelsViewer
    // COM y; the native anchors the fighter by its clip ground-contact (see
    // Fighter::sample) so the feet land at the given world y. The dojo's
    // VISIBLE floor is the dojo_floor sprite line (world Y=223.5) — the
    // fighters stand on it (feet at 223.5 -> the floor sprite row). The
    // params Floor attr (80) is the arena's physics line (JS Bf.init L474:
    // ct=Floor, tl.init L843 container y=height/2-ct=200). Fighters live
    // inside that container (DOJO_BG_STATIC 7.4); floor tiles Y=223.5 are
    // location-space. Render adds the container offset so feet land in the
    // tile band; world/pose/camera stay container-space (oracle-trace exact).
    const float floor_y = 80.0f;  // JS Floor (dojo_params Root Floor=80)
    battle.player_spawn_x = 973.0f;
    battle.player_spawn_y = -110.0f;  // COM Y (oracle Me -108, Enemy -93) — ModelsViewer split
    battle.enemy_spawn_x = 690.0f;
    battle.enemy_spawn_y = -93.0f;
    battle.max_hp = 1;  // the game's HP fallback (Zn = aB>0 ? aB : 1)
    battle.player_unarmed_damage = 80.0f;
    // [fx] JS `nj.parse` (L885) + `f_a` (L896-897): the stage fight's
    // `<Ringout>` rule configures the off-screen marker arrows (`sXa`). The
    // native battle is hardcoded above, so pull the battle's first-fight
    // `<Rules>` from stages.xml and map them into `battle.ringout_*` here,
    // BEFORE init_locks copies the battle into the controller. The dojo
    // Training dummy carries no Ringout -> the markers stay dormant.
    sf2::scene::apply_stage_ringout_rule(battle, battle_fight_rules(battle_name_));

    const sf2::scene::TacticDef* tactic = nullptr;
    const auto it = assets.tactic_defs.find("Standard");
    if (it != assets.tactic_defs.end()) tactic = &it->second;

    static std::mt19937 s_rng(0x5F2);
    std::mt19937& rng = s_rng;
    auto roll01 = [&rng]() {
        return static_cast<float>(rng()) / static_cast<float>(rng.max());
    };

    fight_ = std::make_unique<sf2::scene::FightController>();
    // The enemy's display name: the Dojo training fight names its Punchbag
    // dummy (JS stages.xml Fight 1 Warrior FirstName="Punchbag"); the map
    // flow keeps the pending-battle default "Enemy".
    const std::string& enemy_name = app().pending_battle().enemy_name;
    fight_->init_locks(battle, assets.merged, assets.moves, assets.clips,
                       assets.tactics_sets, tactic, "Player", enemy_name,
                       battle.player_spawn_x, battle.player_spawn_y,
                       battle.enemy_spawn_x, battle.enemy_spawn_y,
                       battle.max_hp, battle.max_hp, roll01, owned,
                       equipped_perks(app(), assets));
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
    fight_->set_fighter_color(assets.dojo.root_color());
    std::fprintf(stdout, "[fight] fighter color 0x%06X (location %s Root Color)\n",
                 assets.dojo.root_color(), location_.c_str());

    // Log the player's move list (the equipment-change evidence).
    std::fprintf(stdout, "[fight] player move list (%zu moves):\n",
                 fight_->player().fighter.hb().size());
    for (const auto* m : fight_->player().fighter.hb()) {
        std::fprintf(stdout, "  %s\n", m->name.c_str());
    }
    std::fflush(stdout);
}

void FightScreen::on_key(int glfw_key, bool down) {
    // Pause toggle (JS `Jn` pause button → `Ar.Qg(0)` → `Aia()` L425; the
    // P/Esc desktop equivalents — app-layer only). Esc (256) / P (80) on the
    // down edge toggle while the round is live. Headless-safe: the headless
    // driver injects pointer clicks, never keys, so this cannot trigger
    // there (no code gate needed — noted for the record).
    if (down && (glfw_key == 256 || glfw_key == 80)) {
        if (fight_ != nullptr && !fight_->round_wait() && !fight_->battle_over()) {
            paused_ = !paused_;
            sf2::audio::AudioEngine::instance().play("click");
            std::fprintf(stdout, "[fight] pause %s (Esc/P)\n", paused_ ? "ON" : "OFF");
            std::fflush(stdout);
        }
        return;
    }
    // While paused, swallow every fight key (no sim input leak — the update
    // is frozen too, so buffered keys would otherwise fire on resume).
    if (paused_) return;
    // Between rounds: Space (32) / Enter (257) = the HUD "Next" button (JS
    // `vhb` L410 case 1) — confirm the next round instead of feeding the
    // punch/attack mapping below.
    if (fight_ != nullptr && down && fight_->round_wait() &&
        (glfw_key == 32 || glfw_key == 257)) {
        sf2::audio::AudioEngine::instance().play("click");
        std::fprintf(stdout, "[fight] NEXT round requested (Space/Enter)\n");
        std::fflush(stdout);
        fight_->next_round_requested();
        return;
    }
    // GLFW key codes -> the game's key_type (JS `Ik` keyboard events ->
    // the fight input; the move Keys conditions read Punch/Forward/Back).
    // Bindings (reasonable desktop keys):
    //   A/Left = Back, D/Right = Forward, W/Up = Jump(up),
    //   Space/J = Punch, L = Kick, S/Down = Crouch(down).
    // K/B = Super (the Fists moveset has no Super-key moves; bound for the
    // weapon movesets that do). Blocking is NOT a raw key in this game: the
    // fighter blocks while any move's `Block` interval is active (e.g. the
    // HighPunch recovery), so attacking/stepping with the keys above also
    // provides the block window.
    sf2::scene::key_type kt = static_cast<sf2::scene::key_type>(0);
    switch (glfw_key) {
        case 65: case 263: kt = sf2::scene::key_type::back; break;     // A / Left
        case 68: case 262: kt = sf2::scene::key_type::forward; break;  // D / Right
        case 87: case 265: kt = sf2::scene::key_type::up; break;       // W / Up (Jump)
        case 83: case 264: kt = sf2::scene::key_type::down; break;     // S / Down (Crouch)
        case 32: case 74: kt = sf2::scene::key_type::punch; break;     // Space / J
        case 76: kt = sf2::scene::key_type::kick; break;               // L
        case 75: case 66: kt = sf2::scene::key_type::super; break;     // K / B (special)
        default: return;
    }
    const int idx = static_cast<int>(kt);
    if (idx < 0 || idx >= 16) return;
    key_state_[idx] = down;
    if (fight_) {
        fight_->player_input(kt, down ? sf2::scene::press_type::tap
                                      : sf2::scene::press_type::release);
    }
}

std::size_t FightScreen::move_list_size() const {
    return fight_ != nullptr ? fight_->player().fighter.hb().size() : 0;
}

bool FightScreen::round_wait() const {
    return fight_ != nullptr && fight_->round_wait();
}

void FightScreen::next_button_center(float& cx, float& cy) const {
    cx = kNextBtnCX;
    cy = kNextBtnCY;
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

// The pointer -> gamepad events (JS `ze.nia/Qgb/oia` for the joystick,
// `fu.nia/oia` for the buttons). Runs in update_impl BEFORE the fight
// update so the buffered keys land the same frame (like on_key).
void FightScreen::update_gamepad_input() {
    if (fight_ == nullptr || !pad_visible()) {
        // Round ended mid-drag: release everything so no key stays held.
        if (joy_grabbed_ || joy_sector_ != 0 || btn_punch_down_ || btn_kick_down_) {
            joy_grabbed_ = false;
            joy_knob_x_ = joy_knob_y_ = 0.0f;
            joy_sector_ = 0;
            btn_punch_down_ = btn_kick_down_ = false;
        }
        return;
    }
    const App::PointerState& p = app().pointer();
    const GamepadLayout pad;

    // --- Joystick (JS `ze`): grab inside the base's 1.5x zone, drag the
    // knob, map the offset to the movement sector 1-8. The knob follows
    // the pointer clamped to the base radius (JS `e5` + `Mz.G_a`).
    if (p.pressed && !joy_grabbed_) {
        const float dx = static_cast<float>(p.x) - pad.joy_cx;
        const float dy = static_cast<float>(p.y) - pad.joy_cy;
        const float grab_r = pad.joy_r * kJoyGrabScale;
        if (dx * dx + dy * dy <= grab_r * grab_r) {
            joy_grabbed_ = true;
        }
    }
    if (joy_grabbed_) {
        if (!p.down) {
            // Released (JS `oia`): neutral + the key release event.
            joy_grabbed_ = false;
            joy_knob_x_ = joy_knob_y_ = 0.0f;
            if (joy_sector_ != 0) {
                fight_->player_input(static_cast<sf2::scene::key_type>(joy_sector_),
                                     sf2::scene::press_type::release);
                std::fprintf(stdout, "[fight] player input -> joy release %d\n",
                             joy_sector_);
                joy_sector_ = 0;
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
            joy_knob_x_ = dx;
            joy_knob_y_ = dy;
            const int sector = joy_sector_of(dx, dy, pad.joy_r);
            if (sector != joy_sector_) {
                if (joy_sector_ != 0) {
                    fight_->player_input(static_cast<sf2::scene::key_type>(joy_sector_),
                                         sf2::scene::press_type::release);
                }
                if (sector != 0) {
                    fight_->player_input(static_cast<sf2::scene::key_type>(sector),
                                         sf2::scene::press_type::tap);
                }
                std::fprintf(stdout, "[fight] player input -> joy sector %d -> %d\n",
                             joy_sector_, sector);
                std::fflush(stdout);
                joy_sector_ = sector;
            }
        }
    }

    // --- Attack buttons (JS `fu.nia/oia`): a press inside a button's
    // circle taps the attack key; the release ends it. The JS hit test is
    // the node-local x*x+y*y < 115^2 — the native tests the view-space
    // circle around each button center.
    const float px = static_cast<float>(p.x);
    const float py = static_cast<float>(p.y);
    if (p.pressed && !joy_grabbed_) {
        const float pdx = px - pad.punch_cx;
        const float pdy = py - pad.punch_cy;
        if (pdx * pdx + pdy * pdy <= pad.btn_r * pad.btn_r) {
            btn_punch_down_ = true;
            fight_->player_input(sf2::scene::key_type::punch, sf2::scene::press_type::tap);
            std::fprintf(stdout, "[fight] player input -> punch (pad)\n");
            std::fflush(stdout);
        } else {
            const float kdx = px - pad.kick_cx;
            const float kdy = py - pad.kick_cy;
            if (kdx * kdx + kdy * kdy <= pad.btn_r * pad.btn_r) {
                btn_kick_down_ = true;
                fight_->player_input(sf2::scene::key_type::kick, sf2::scene::press_type::tap);
                std::fprintf(stdout, "[fight] player input -> kick (pad)\n");
                std::fflush(stdout);
            }
        }
    }
    if (btn_punch_down_ && !p.down) {
        btn_punch_down_ = false;
        fight_->player_input(sf2::scene::key_type::punch, sf2::scene::press_type::release);
    }
    if (btn_kick_down_ && !p.down) {
        btn_kick_down_ = false;
        fight_->player_input(sf2::scene::key_type::kick, sf2::scene::press_type::release);
    }
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
    const bool grabbed = joy_grabbed_;
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
    const float knob_cx = pad.joy_cx + joy_knob_x_;
    const float knob_cy = pad.joy_cy + joy_knob_y_;
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
        try_draw_atlas_button(app, btn_punch_down_ ? "btn_punch_action" : "btn_punch_normal",
                              pad.punch_cx, pad.punch_cy, btn_size, btn_size, 1.0f);
    const bool kick_drawn =
        try_draw_atlas_button(app, btn_kick_down_ ? "btn_kick_action" : "btn_kick_normal",
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
            ren.draw_triangles(tri, 3, btn_punch_down_ ? 0.9f : 0.3f, 0.55f, 0.15f, 0.85f);
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
            ren.draw_triangles(tri, 3, btn_kick_down_ ? 0.9f : 0.3f, 0.55f, 0.15f, 0.85f);
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
        c.arena_h = assets.dojo.arena_height() > 0.0f ? assets.dojo.arena_height() : 560.0f;
        c.arena_floor = assets.dojo.arena_floor();
        c.arena_center_x = assets.dojo.arena_width() * 0.5f;
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
// --- Pause dialog `Dr` layout (JS L2018; PAUSE_STATIC §3) ------------------
// The fight HUD pause widget (`Sf.Jn`, L2034) / Esc/P opens `ha.Aia`'s `Dr`
// dialog. The exact node tree is OPEN (PAUSE_STATIC OPEN #5); the evidenced
// `res/fight/pause.*` frames are `Pause` (400x96 title), `PauseMusic_on/off`,
// `PauseSound_on/off`, `play` (resume) and `home` (quit) 150x150 buttons.
// Replaces the invented flat RESUME/RESTART/QUIT stack (PORT_AUDIT_UI §3 #25).
constexpr float kPauseDlgTitleCx = kViewW * 0.5f;
constexpr float kPauseDlgTitleCy = 150.0f;
constexpr float kPauseDlgTitleW = 400.0f;
constexpr float kPauseDlgTitleH = 96.0f;
constexpr float kPauseDlgMusicX = 560.0f;
constexpr float kPauseDlgSoundX = 720.0f;
constexpr float kPauseDlgToggleY = 300.0f;
constexpr float kPauseDlgToggleS = 150.0f;
constexpr float kPauseDlgPlayX = 560.0f;
constexpr float kPauseDlgHomeX = 720.0f;
constexpr float kPauseDlgActionY = 470.0f;

void FightScreen::update_impl(float dt) {
    if (fight_ == nullptr) return;
    // Location timeline (D6): the fight renders the same location layers as
    // the hub, so advance the SimpleEffect Transparency loop per frame.
    if (app().has_fight_assets()) {
        app().fight_assets().dojo.update(dt);
    }
    if (!auto_attack_wired_) {
        auto_attack_wired_ = true;
        if (app().auto_attack()) {
            fight_->set_auto_attack(true);
            std::fprintf(stdout, "[fight] auto-attack ON\n");
        }
    }
    // Pause dialog hit geometry (mirrors render_impl; the `Jn` HUD button
    // slot + the `Dr` frame rows, JS L2018).
    const float kPauseIx = 1216.0f, kPauseIy = 40.0f, kPauseIw = 64.0f, kPauseIh = 48.0f;
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
            if (pause_hit(kPauseDlgPlayX, kPauseDlgActionY, kPauseDlgToggleS,
                          kPauseDlgToggleS)) {
                // `play` frame = resume (PAUSE_STATIC §3 `tZ`).
                paused_ = false;
                sf2::audio::AudioEngine::instance().play("click");
                std::fprintf(stdout, "[fight] pause OFF (resume, Dr.play)\n");
                std::fflush(stdout);
            } else if (pause_hit(kPauseDlgMusicX, kPauseDlgToggleY, kPauseDlgToggleS,
                                 kPauseDlgToggleS)) {
                // `PauseMusic_on/off` toggle (JS music keeps playing under a
                // pause — PAUSE_STATIC §5; this toggle is UI-layer).
                music_off_ = !music_off_;
                if (music_off_) {
                    sf2::audio::AudioEngine::instance().stop_music();
                } else {
                    sf2::audio::AudioEngine::instance().play_music(
                        sf2::audio::AudioEngine::instance().music_track());
                }
                sf2::audio::AudioEngine::instance().play("click");
                std::fprintf(stdout, "[fight] pause music %s (Dr.PauseMusic)\n",
                             music_off_ ? "OFF" : "ON");
                std::fflush(stdout);
            } else if (pause_hit(kPauseDlgSoundX, kPauseDlgToggleY, kPauseDlgToggleS,
                                 kPauseDlgToggleS)) {
                // `PauseSound_on/off` (display only — no runtime SFX mute API;
                // see the stream report).
                sf2::audio::AudioEngine::instance().play("click");
                std::fprintf(stdout,
                             "[fight] pause sound toggle (Dr.PauseSound, display-only)\n");
                std::fflush(stdout);
            } else if (pause_hit(kPauseDlgHomeX, kPauseDlgActionY, kPauseDlgToggleS,
                                 kPauseDlgToggleS)) {
                // `home` = quit (JS `Xc.Zhb` exit-confirm -> `O3a`; the confirm
                // dialog is not ported — direct pop, OPEN).
                sf2::audio::AudioEngine::instance().play("click");
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
            sf2::audio::AudioEngine::instance().play("click");
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

    // Between-rounds "Next" click rect (headless driver only): the fight
    // holds in EndStance until next_round_requested(). The VISIBLE Next
    // button was an invention and is removed (PORT_AUDIT_UI section 3 item
    // 26); this invisible rect stays because the headless loop/tour clicks
    // `next_button_center` (main.cpp) until the JS auto-advance (`Cr.tca`
    // L2023) is ported into FightController — OPEN.
    if (fight_->round_wait()) {
        const App::PointerState& p = app().pointer();
        if (p.pressed && p.x >= kNextBtnCX - kNextBtnW * 0.5f &&
            p.x <= kNextBtnCX + kNextBtnW * 0.5f && p.y >= kNextBtnCY - kNextBtnH * 0.5f &&
            p.y <= kNextBtnCY + kNextBtnH * 0.5f) {
            sf2::audio::AudioEngine::instance().play("click");
            std::fprintf(stdout, "[fight] NEXT round requested (round %d done)\n",
                         fight_->round().number);
            std::fflush(stdout);
            fight_->next_round_requested();
        }
    }

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
            j.fight = pb.battle_name;
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
            if (player_won) pb.reward_money = prize.coins_total;
        }
        std::fflush(stdout);
        push(kScreenResults);
    }
}

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
    const float arena_half =
        assets.dojo.arena_width() > 0.0f ? assets.dojo.arena_width() * 0.5f : 980.0f;
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
    camera.arena_h = assets.dojo.arena_height() > 0.0f ? assets.dojo.arena_height() : 560.0f;
    camera.arena_floor = assets.dojo.arena_floor();
    // Io = Lb.width/2 - focus (JS `Ut.Al` L826): the parallax reference the
    // renderer folds into every layer (renderer.hpp `camera_offset_x`). At
    // the fight-start focus 831.5 -> Io = 148.5.
    camera.arena_center_x = arena_half - cam.center_x;
    ren.begin_frame(camera);
    // [fix(render): arena layer order] The original game draws the fighters
    // INSIDE the ModelsViewer (Type=2) layer — background layers first, then
    // the fighters, then every layer AFTER the ModelsViewer (the floor /
    // arena sides / dust / glow / pixel_1 vignette) ON TOP of the fighters
    // (JS_RENDER §7, "Что у нас не так" #1). The old code drew ALL layers
    // before the fighters, so the floor rendered UNDER their feet — the
    // broken "arena behind the fighters" look.
    const std::size_t fighter_layer = assets.dojo.fighter_layer();
    assets.dojo.render_layers(ren, camera, 0, fighter_layer);  // background (parallax)

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
        const float kContY = assets.dojo.arena_height() * 0.5f - assets.dojo.arena_floor();
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
    auto draw_capsules = [&camera, &ren, &assets, arena_half](const sf2::scene::FightFighter& f) {
        const float r = f.fighter.color_r(), g = f.fighter.color_g(), b = f.fighter.color_b();
        // [Phase 4d — capsule-figure render] The oracle draws the fighter's
        // body from the merged model's CAPSULE FIGURES (JS `Yc.Tib`: every
        // `<Capsule_* Type="Capsule" Radius1=".." Edge="..">` becomes a `zu`
        // visual node -> a `Dk` stroked line, stroke = Radius1*2). Dedup by
        // edge keeps max Radius to avoid double squares (EThigh 12+15).
        const sf2::scene::Model& model = f.fighter.model();
        std::unordered_map<std::string, float> edge_max;
        edge_max.reserve(model.capsules.size() * 2u);
        for (const sf2::scene::Capsule& cap : model.capsules) {
            auto it = edge_max.find(cap.edge);
            if (it == edge_max.end() || cap.radius1 > it->second) {
                edge_max[cap.edge] = cap.radius1;
            }
        }
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kDiscSegments = 12;
        for (const auto& kv : edge_max) {
            const std::string& edge_name = kv.first;
            const float rad = kv.second;
            const sf2::scene::EdgeDef* edge = nullptr;
            for (const sf2::scene::EdgeDef& ed : model.edges) {
                if (ed.name == edge_name) {
                    edge = &ed;
                    break;
                }
            }
            if (edge == nullptr) {
                continue;
            }
            const int i1 = model.bone_by_name(edge->end1);
            const int i2 = model.bone_by_name(edge->end2);
            if (i1 < 0 || i2 < 0) {
                continue;
            }
            const std::vector<float>& pos = f.fighter.positions();
            const std::size_t u1 = static_cast<std::size_t>(i1) * 2;
            const std::size_t u2 = static_cast<std::size_t>(i2) * 2;
            if (u1 + 1 >= pos.size() || u2 + 1 >= pos.size()) {
                continue;
            }
            const float stroke = rad * 2.0f * camera.zoom;
            if (stroke <= 0.0f) {
                continue;
            }
            const float sx1 = camera.world_to_screen_x(pos[u1] - arena_half, 1.0f);
            const float sy1 = camera.world_to_screen_y(
                pos[u1 + 1] + assets.dojo.arena_height() * 0.5f - assets.dojo.arena_floor());
            const float sx2 = camera.world_to_screen_x(pos[u2] - arena_half, 1.0f);
            const float sy2 = camera.world_to_screen_y(
                pos[u2 + 1] + assets.dojo.arena_height() * 0.5f - assets.dojo.arena_floor());
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
    draw_capsules(fight_->enemy());
    ren.draw_triangles(ev.data(), ev.size() / 2, fight_->enemy().fighter.color_r(),
                       fight_->enemy().fighter.color_g(), fight_->enemy().fighter.color_b());
    draw_capsules(fight_->player());
    ren.draw_triangles(pv.data(), pv.size() / 2, fight_->player().fighter.color_r(),
                       fight_->player().fighter.color_g(), fight_->player().fighter.color_b());

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
    draw_hit_sparks(ren, camera, fight_->fx(), assets.dojo.root_color(), arena_half,
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
    const std::size_t n_layers = assets.dojo.layers().size();
    if (fighter_layer != sf2::scene::LocationScene::npos) {
        assets.dojo.render_layers(ren, camera, fighter_layer + 1, n_layers);
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

    // Scene letterbox bars (JS `ma.Sya` L1833-1834; PORT_AUDIT_SCENE D10):
    // drawn over the scene, under the HUD — no-op at 16:9 (BK=0, arena
    // 728px spans y[-4,724]).
    draw_scene_letterbox(ren, camera);

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
    const float bar_w = 425.0f, bar_h = 43.0f;
    const float bar_y = 150.0f * hud_c + hud_f * hud_g;
    const float bar_cx_player = kViewW * 0.5f - 520.0f * hud_c * hud_e;
    const float bar_cx_enemy = kViewW * 0.5f + 520.0f * hud_c * hud_e;
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

    auto draw_hp_bar = [&](float x, float y, float w, float h, float ratio, float leak_ratio,
                           const char* fill_frame, const char* leak_frame) {
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
            if (!app.draw_atlas_rect(leak_frame, x, y, lw, h, 1.0f)) {
                const float lg[] = {x, y, x + lw, y, x, y + h,
                                    x + lw, y, x + lw, y + h, x, y + h};
                ren.draw_triangles(lg, 6, 0.95f, 0.85f, 0.45f, 0.85f);
            }
        }
        if (ratio > 0.001f) {
            const float fw = w * ratio;
            if (!app.draw_atlas_rect(fill_frame, x, y, fw, h, 1.0f)) {
                const bool is_blue = std::string(fill_frame).find("Blue") != std::string::npos;
                const float r = is_blue ? 0.25f : 0.16f;
                const float g = is_blue ? 0.45f : 0.82f;
                const float b = is_blue ? 0.92f : 0.16f;
                const float fg[] = {x, y, x + fw, y, x, y + h, x + fw, y, x + fw, y + h, x, y + h};
                ren.draw_triangles(fg, 6, r, g, b, 0.96f);
            }
        }
        // thin border over bar for readability
        const float br = 1.0f;
        const float top[] = {x - br, y - br, x + w + br, y - br, x - br, y,
                             x + w + br, y - br, x + w + br, y, x - br, y};
        const float bot[] = {x - br, y + h, x + w + br, y + h, x - br, y + h + br,
                             x + w + br, y + h, x + w + br, y + h + br, x - br, y + h + br};
        ren.draw_triangles(top, 6, 0.0f, 0.0f, 0.0f, 0.85f);
        ren.draw_triangles(bot, 6, 0.0f, 0.0f, 0.0f, 0.85f);
    };

    draw_hp_bar(bar_cx_player - bar_w * 0.5f, bar_y, bar_w, bar_h, s_hud_player_decay_.shown(),
                s_hud_player_decay_.leak(), "HealthBar_Full", "HealthBar_Hit");
    draw_hp_bar(bar_cx_enemy - bar_w * 0.5f, bar_y, bar_w, bar_h, s_hud_enemy_decay_.shown(),
                s_hud_enemy_decay_.leak(), "HealthBarBlue_Full", "HealthBarBlue_Hit");

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
            const float scale = (fnt == app.digits_font()) ? (120.0f * hud_c / 90.0f)
                                                           : (120.0f * hud_c / 100.0f);
            // shadow (black) slightly offset, then white foreground
            const float ty = 44.0f;
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

    // Rounds — Round pips (JS `Er` L2021-2022): e=32, f=e/2, step e+f, height 43, frames y.UU/y.LQa
    const float pip_e = 32.0f;
    const float pip_step = pip_e + pip_e * 0.5f;  // e + f (Er L2021)
    const float pip_h = 43.0f;                    // Pb(43)
    const float pip_y = bar_y + bar_h + 6.0f;
    const int rounds_total = fight_->round().length;
    for (int i = 0; i < rounds_total; ++i) {
        const bool p_done = i < fight_->player().rounds_won;
        const bool e_done = i < fight_->enemy().rounds_won;
        const char* p_frame = p_done ? "Round_Done" : "Round_Undone";
        const char* e_frame = e_done ? "Round_Done" : "Round_Undone";
        // The player's pips step left from the bar's inner (right) end; the
        // enemy's step right from its inner (left) end (`lk.kva` L2028).
        const float px = bar_cx_player + bar_w * 0.5f - pip_e -
                         static_cast<float>(i) * pip_step;
        const float ex = bar_cx_enemy - bar_w * 0.5f + static_cast<float>(i) * pip_step;
        if (!app.draw_atlas_rect(p_frame, px, pip_y, pip_e, pip_h, 1.0f)) {
            float dv[12] = {px, pip_y, px + pip_e, pip_y, px, pip_y + pip_h,
                            px + pip_e, pip_y, px + pip_e, pip_y + pip_h, px, pip_y + pip_h};
            ren.draw_triangles(dv, 6, p_done ? 0.18f : 0.32f, p_done ? 0.92f : 0.32f,
                               p_done ? 0.18f : 0.32f, 1.0f);
        }
        if (!app.draw_atlas_rect(e_frame, ex, pip_y, pip_e, pip_h, 1.0f)) {
            float ev2[12] = {ex, pip_y, ex + pip_e, pip_y, ex, pip_y + pip_h,
                             ex + pip_e, pip_y, ex + pip_e, pip_y + pip_h, ex, pip_y + pip_h};
            ren.draw_triangles(ev2, 6, e_done ? 0.18f : 0.32f, e_done ? 0.92f : 0.32f,
                               e_done ? 0.18f : 0.32f, 1.0f);
        }
    }

    // The round banner (ROUND N / FIGHT! / K.O. / VICTORY / DEFEAT — JS
    // `Cr` L2021-2026): over the fight + HUD, UNDER the gamepad and the
    // Next button (the draw order below). The screen tracks the banner's
    // age for the hold-forever VICTORY/DEFEAT pop-in (see draw_fight_banner).
    {
        const int kind_now = static_cast<int>(fight_->banner());
        if (kind_now != banner_kind_seen_) {
            banner_kind_seen_ = kind_now;
            banner_start_frame_ = fight_->frame();
        }
        const int banner_age = fight_->frame() - banner_start_frame_;
        draw_fight_banner(app, *fight_, banner_age);
    }

    // The on-screen gamepad (JS `Za` virtual controls): the joystick
    // bottom-left + the punch/kick buttons bottom-right, drawn from the
    // ui/controller atlas. Only while the round is live — the Next button
    // replaces it between rounds (see the round_wait block below).
    draw_gamepad(app);

    // NOTE: the JS has NO between-rounds "NEXT" button (JS `ai`/`Cr` advances
    // rounds with `Cr.tca` timers, L2023). The native Next button was
    // INVENTED (PORT_AUDIT_UI §3 item 26) and is removed here. The keyboard
    // path (Space/Enter -> next_round_requested, on_key) and the headless
    // driver's `next_button_center` click rect (update_impl) remain until the
    // JS auto-advance is ported into FightController — OPEN.
    // Pause menu render (JS `Jn` button + `Ar.Qrb` overlay — display only).
    // Geometry mirrors update_impl.
    const bool live =
        fight_ != nullptr && !fight_->round_wait() && !fight_->battle_over();
    if (live && !paused_) {
        // The HUD pause icon (`Jn`, top-right; `E.get(1294)` frame slot).
        if (!try_draw_atlas_button(app, "FightPause", 1216.0f, 40.0f, 64.0f, 48.0f,
                                   1.0f)) {
            draw_flat_button(app, "II", 1216.0f, 40.0f, 64.0f, 48.0f, 0.3f, 0.3f, 0.4f,
                             false);
            draw_ui_label(app, 1216.0f - 32.0f + 4.0f, 40.0f - 12.0f, 64.0f - 8.0f, 24.0f,
                              "II", 0.8f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
        }
    }
    if (paused_) {
        const float dim[] = {0, 0,         kViewW, 0,         kViewW, kViewH,
                             0, 0,         kViewW, kViewH,    0,      kViewH};
        ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.65f);
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
              kPauseDlgToggleY, kPauseDlgToggleS, kPauseDlgToggleS, "MUSIC");
        frame("PauseSound_on", kPauseDlgSoundX, kPauseDlgToggleY, kPauseDlgToggleS,
              kPauseDlgToggleS, "SOUND");
        frame("play", kPauseDlgPlayX, kPauseDlgActionY, kPauseDlgToggleS,
              kPauseDlgToggleS, "RESUME");
        frame("home", kPauseDlgHomeX, kPauseDlgActionY, kPauseDlgToggleS,
              kPauseDlgToggleS, "QUIT");
    }
}

// ---------------------------------------------------------------------------
// ResultsScreen
// ---------------------------------------------------------------------------

ResultsScreen::ResultsScreen(ScreenManager& mgr, bool player_won, int money_reward,
                             int exp_reward)
    : Screen(mgr, "Results"), player_won_(player_won), money_reward_(money_reward),
      exp_reward_(exp_reward) {
    // No win/lose stinger files ship on disk — stop the fight track on
    // Results instead (documented approximation).
    sf2::audio::AudioEngine::instance().stop_music();
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

void ResultsScreen::update_impl(float dt) {
    (void)dt;
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
                w.record_battle_win(pb.battle_name);
                bool found = false;
                for (auto& f : w.fights) {
                    if (f.name == pb.battle_name) {
                        ++f.wins;
                        found = true;
                    }
                }
                if (!found) w.fights.push_back({pb.battle_name, 1});
                std::fprintf(stdout, "[result] battle record: %s\n",
                             pb.battle_name.c_str());
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
        // Tutorial quest nudge (quest_panel.hpp — read-only derivation via
        // the existing pending-battle hook; the Dojo hint panel picks the
        // step up from here, no save writes).
        {
            const PendingBattle& pb = app().pending_battle();
            if (player_won_ && pb.has_result && pb.battle_name == "Training") {
                quest_toast_ = "Quest update: the dummy falls! Sensei awaits in the Dojo.";
                std::fprintf(stdout, "[result] quest: first Training win -> Sensei hint advanced\n");
                std::fflush(stdout);
            }
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

void ResultsScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.6f);
    // `kk` result dialog (JS L2057-2061; PORT_AUDIT_UI §2.8 item 28): a
    // 750-wide base (`Qa = R.$(E.Zxa(750))`, L2058) carrying the win/lose
    // label from the callouts atlas id 1310 (`mT = R.$(E.get(1310))`, frame
    // `y.Lna` win / `y.Kna` lose) with the `Fh` breakdown lines below.
    // Replaces the invented standalone VICTORY/DEFEAT screen layout.
    constexpr float kKkW = 750.0f;
    constexpr float kKkH = 480.0f;
    const float px = kViewW * 0.5f - kKkW * 0.5f;
    const float py = kViewH * 0.5f - kKkH * 0.5f;
    const float panel[] = {px, py, px + kKkW, py, px, py + kKkH,
                           px + kKkW, py, px + kKkW, py + kKkH, px, py + kKkH};
    ren.draw_triangles(panel, 6, 0.08f, 0.07f, 0.10f, 0.92f);
    // Win/lose label art (callouts id 1310: `label_win`/`label_lose` are the
    // `y.Lna`/`y.Kna` frames, JS L2058). Flat text only on a genuine miss.
    bool label_drawn = false;
    if (load_callouts_atlas(app)) {
        label_drawn = try_draw_atlas_button(
            app, player_won_ ? "label_win" : "label_lose", kViewW * 0.5f, py + 92.0f,
            380.0f, 130.0f, 1.0f);
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
    float y = py + 190.0f;
    auto line = [&](const std::string& s) {
        draw_ui_label(app, kViewW * 0.5f - 300.0f, y, 600.0f, 26.0f, s, 0.85f,
                      UiAlign::Center, 1.0f, 1.0f, 1.0f);
        y += 32.0f;
    };
    if (player_won_) {
        line("Coins: " + std::to_string(prize_base_) + " + bonus " +
             std::to_string(prize_bonus_) + " = " + std::to_string(money_reward_));
        if (prize_perfect_) line("PERFECT +5");
        if (prize_first_) line("FIRST STRIKE +2");
        if (prize_combo_ > 0)
            line("COMBO x" + std::to_string(prize_combo_) + " +" +
                 std::to_string(prize_combo_));
        if (prize_shocks_ > 0)
            line("SHOCK x" + std::to_string(prize_shocks_) + " +" +
                 std::to_string(prize_shocks_ * 3));
        line("EXP +" + std::to_string(exp_reward_));
    }
    if (!quest_toast_.empty()) {
        draw_ui_label(app, kViewW * 0.5f - 300.0f, py + kKkH - 62.0f, 600.0f, 28.0f,
                      quest_toast_, 0.9f, UiAlign::Center, 1.0f, 0.9f, 0.4f);
    }
    // Continue affordance (any tap advances; the update pops — JS `kk` routes
    // through its `Lr` buttons / `v.qxa`).
    draw_ui_label(app, kViewW * 0.5f - 300.0f, py + kKkH - 32.0f, 600.0f, 24.0f,
                  "TAP TO CONTINUE", 0.8f, UiAlign::Center, 0.8f, 0.8f, 0.8f);
    std::fprintf(stdout, "[result] %s\n", player_won_ ? "WIN" : "LOSS");
}

// ---------------------------------------------------------------------------
// ShopScreen
// ---------------------------------------------------------------------------

// Shop tabs (JS `vj.E0` L1168-1169 category ids → `vj.ifa` tab lists,
// `Oa.f5`): 1 Weapon, 2 Armor, 3 Helm, 4 Ranged, 5 Magic.
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
};
constexpr int kShopTabCount = 5;

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
// The JS cells `ns` (L2303-2308) are laid out in the `Oe` viewer list. `Oa.f5`
// (L2286-2288) sets the per-category viewer anchor `Za.uw` and list spacing
// `Za.LT` BEFORE building the list `yF(...)`:
//   tab0 Weapon  uw=(300,220) LT(50)   tab1 Armor  uw=(300,400) LT(20)
//   tab2 Helm    uw=(300,280) LT(100)  tab3 Ranged uw=(300,220) LT(50)
//   tab4 Magic   uw=(300,220) LT(50)   tab5 IAP    uw=(300,320) (no LT)
//   tab7 event   uw=(670,500)          (no LT)
// (`Za` here is the `Oe` card viewer, not the gamepad `Za`.) `Oe.Pn(c)` (L2262)
// then docks `scroll` at `c.J/c.P`, sizes it `(c.width, c.height, c.width*.08)`,
// sets the cell list `Pa.C(4)`/`Pa.ba(scroll.Gv-8, scroll.Xy)` and the `Dn`
// "noItems" label. `LT(a)` = `Pa.spacing`. The exact cell rects need `Oe`'s
// `Fg` scroll content dims (`Gv`/`Xy`), the `Gg` list (`Pa`) cell sizing and
// the `y.*` frame-name table — none of which is derivable statically
// (PORT_AUDIT_UI §5 OPEN #4). The landed native grid keeps the exact JS viewer
// rect `c` (`Oa.layout`, L2293-2295) in 2 columns with the JS gap; the `uw`
// anchors and `LT` spacing are recorded above but not applied to the grid
// (OPEN). The `ns` cell internals (L2305: icon `ky.zf(40)`, name
// `av.Fa(ky.za()*2, ky.qa()*.7)` at `C(ky.za())D(ky.ra+ky.qa()*.2)`, price
// `pv.Fa(a,b*.3)` at `C(a*.05)D(b*.8)`) need the `E.get(260)` icon frame
// dims (`y.PRa`), also OPEN.
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

struct ShopLayout {
    ShopRect content;     // b (L2293, after fn)
    ShopRect viewer;      // c = b.fn(.75) (L2294) — the JS `Za` item area
    float gap = 0.0f;     // e = (c.N-c.J)*.03 (L2294)
    float slot_h = 0.0f;  // b = (c.W-c.P)*.8 (L2294, side-slot height)
    // Native item grid inside `viewer` (2 columns; the JS `ns` pitch OPEN).
    float card_w = 0.0f, card_h = 0.0f;
    float dx = 0.0f, dy = 0.0f;
    float x0 = 0.0f, y0 = 0.0f;  // first card centre
};

ShopLayout shop_layout() {
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
    // 2-column grid filling the viewer rect (gap = the JS `.03`).
    l.card_w = l.viewer.width() * 0.5f - l.gap * 0.5f;
    l.card_h = l.viewer.height() * 0.5f - l.gap * 0.5f;
    l.dx = l.card_w + l.gap;
    l.dy = l.card_h + l.gap;
    l.x0 = l.viewer.J + l.card_w * 0.5f;
    l.y0 = l.viewer.P + l.card_h * 0.5f;
    return l;
}

// Bottom tab strip (JS `ss`/`Eg` L1851-1853, L2283-2284): a full-width bar
// `height = za.Sp*1.2` with `Le` buttons (id 248 shop atlas) scaled to the
// bar height and laid left->right (spacing factor 1.2 at lc>1.2), centred.
// `buttons/Weapon` sourceSize is 200x190.
constexpr float kShopTabSrcW = 200.0f;
constexpr float kShopTabSrcH = 190.0f;
constexpr float kShopTabBarK = 1.2f;
constexpr float kShopTabSpread = 1.2f;

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
    const float row = l.btn_w + static_cast<float>(kShopTabCount - 1) * l.step;
    l.cx0 = (kViewW - row) * 0.5f + l.btn_w * 0.5f;
    l.cy = kViewH - l.bar_h * 0.5f;
    return l;
}

// Row view: indices into ShopScreen::items_ for tab t (list order kept, so
// WEAPON_KNIVES stays row 0 of Weapons — the headless-loop buy click).
std::vector<std::size_t> shop_tab_rows(const std::vector<CatalogItem>& items, int tab) {
    std::vector<std::size_t> out;
    if (tab < 0 || tab >= kShopTabCount) return out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].type == kShopTabs[tab].type) out.push_back(i);
    }
    return out;
}

// Shop atlas art per tab (shop.<hash>.json buttons/* — the JS `vj.ifa` tab
// icons). Index matches kShopTabs order.
const char* shop_tab_art(int tab, bool active) {
    static const char* kNormal[kShopTabCount] = {
        "buttons/Weapon", "buttons/Armor", "buttons/Helmet",
        "buttons/Ranged_weapon", "buttons/Magic",
    };
    static const char* kActive[kShopTabCount] = {
        "buttons/Weapon_active", "buttons/Armor_active", "buttons/Helmet_active",
        "buttons/Ranged_weapon_active", "buttons/Magic_active",
    };
    if (tab < 0 || tab >= kShopTabCount) return nullptr;
    return active ? kActive[tab] : kNormal[tab];
}

// Shop atlas attribute icon for an item type (attributes/* — the JS card
// icon per category; _light variants are the lit/hover versions).
const char* shop_item_art(const std::string& type, bool light) {
    if (type == "Weapon") return light ? "attributes/weapon_attack_light" : "attributes/weapon_attack";
    if (type == "Armor") return light ? "attributes/body_armor_light" : "attributes/body_armor";
    if (type == "Helm") return light ? "attributes/head_armor_light" : "attributes/head_armor";
    if (type == "Ranged") return light ? "attributes/ranged_attack_light" : "attributes/ranged_attack";
    if (type == "Magic") return light ? "attributes/magic_attack_light" : "attributes/magic_attack";
    return nullptr;
}

// Equipped-slot value for an item type (JS `xc.hk` slots; save fields readable).
const std::string& shop_slot_for(const WarriorSave& w, const std::string& type) {
    if (type == "Armor") return w.armor;
    if (type == "Helm") return w.helm;
    if (type == "Ranged") return w.ranged;
    if (type == "Magic") return w.magic;
    return w.weapon;
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

// Wielding summary (read-only): the equipped slots' applied stats, resolved
// through the full catalog (base Body/Head/Fists included).
std::string wielding_line(App& app, const WarriorSave& seen) {
    const std::vector<CatalogItem> full = load_full_catalog(app);
    auto stat = [&](const std::string& name) {
        for (const auto& ci : full) {
            if (ci.name != name) continue;
            char buf[96];
            if (ci.type == "Weapon") {
                std::snprintf(buf, sizeof(buf), "%s DMG %d", name.c_str(),
                              ci.weapon_damage);
            } else if (ci.type == "Armor") {
                std::snprintf(buf, sizeof(buf), "%s DEF %d", name.c_str(),
                              ci.body_defense);
            } else if (ci.type == "Helm") {
                std::snprintf(buf, sizeof(buf), "%s DEF %d", name.c_str(),
                              ci.head_defense);
            } else {
                std::snprintf(buf, sizeof(buf), "%s", name.c_str());
            }
            return std::string(buf);
        }
        return name;
    };
    return "WIELDING: " + stat(seen.weapon) + " | " + stat(seen.armor) + " | " +
           stat(seen.helm);
}

ShopScreen::ShopScreen(ScreenManager& mgr) : Screen(mgr, "Shop") {
    items_ = load_catalog(app());
    std::fprintf(stdout, "[shop] %zu shop items\n", items_.size());
    for (const auto& it : items_) {
        std::fprintf(stdout, "[shop] item %s (%s) price=%d model=%s\n", it.name.c_str(),
                     it.subtype.empty() ? it.type.c_str() : it.subtype.c_str(), it.price,
                     it.model.c_str());
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
                    break;
                }
            }
            std::fprintf(stdout, "[shop] Ao focus WEAPON_KNIVES (tab Weapons, row %d)\n",
                         hover_);
            std::fflush(stdout);
        }
    } catch (const std::exception&) {
    }
}

void ShopScreen::update_impl(float dt) {
    (void)dt;
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
        for (int t = 0; t < kShopTabCount; ++t) {
            const float cx = tl.cx0 + static_cast<float>(t) * tl.step;
            if (p.x >= cx - tl.btn_w / 2 && p.x <= cx + tl.btn_w / 2 &&
                p.y >= tl.cy - tl.btn_h / 2 && p.y <= tl.cy + tl.btn_h / 2) {
                tab_hover_ = t;
                if (p.pressed && t != tab_) {
                    tab_ = t;
                    sf2::audio::AudioEngine::instance().play("click");
                    std::fprintf(stdout, "[shop] tab %s (E0=%d)\n", kShopTabs[tab_].label,
                                 kShopTabs[tab_].e0);
                    std::fflush(stdout);
                }
                break;
            }
        }
    }
    // BACK (top-left) -> the previous screen (the loop's shop -> dojo leg).
    if (p.x >= 20 && p.x <= 108 && p.y >= 12 && p.y <= 68) {
        if (p.pressed) {
            std::fprintf(stdout, "[shop] BACK -> previous screen\n");
            std::fflush(stdout);
            manager().pop();
            return;
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
            confirm_ = "CLAIMED " + kv.first + "!";
            confirm_until_ = time() + 2.5f;
            std::fprintf(stdout, "[shop] delivery claimed: %s (Vxa notify)\n",
                         kv.first.c_str());
            std::fflush(stdout);
            break;
        }
    }
    // Item grid for the active tab (geometry = the responsive `Oa.layout`
    // `gb` split, `shop_layout()`; row 0 of Weapons is WEAPON_KNIVES).
    const ShopLayout sl = shop_layout();
    const std::vector<std::size_t> rows = shop_tab_rows(items_, tab_);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int col = static_cast<int>(i % 2);
        const int row = static_cast<int>(i / 2);
        const float cx = sl.x0 + static_cast<float>(col) * sl.dx;
        const float cy = sl.y0 + static_cast<float>(row) * sl.dy;
        if (p.x >= cx - sl.card_w / 2 && p.x <= cx + sl.card_w / 2 &&
            p.y >= cy - sl.card_h / 2 && p.y <= cy + sl.card_h / 2) {
            hover_ = static_cast<int>(i);
            if (p.pressed) {
                const CatalogItem& it = items_[rows[i]];
                WarriorSave w;
                try {
                    w = app().save().load();
                } catch (const std::exception&) {
                    break;
                }
                // JS `Pa.iwa` coin gate (L1228/SHOP_STATIC §9): `Tb >= jp` →
                // deduct `Fr` + grant `gI` + save. Coins are the Warrior
                // Money attr (the seed `<Currencies/>` is empty — no coin
                // key exists to deduct from; see the stream report).
                if (w.has_item(it.name)) {
                    std::fprintf(stdout, "[shop] %s already owned\n", it.name.c_str());
                    std::fflush(stdout);
                } else if (w.money >= it.price) {
                    w.money -= it.price;
                    if (it.delivery_sec > 0) {
                        // Timed delivery (JS Pa z2a-path: Ec>0 → delivery,
                        // else Cba instant grant below): paid upfront, the
                        // item arrives on claim (Gb Cla(now)+save stamped).
                        w.timers[it.name] =
                            WarriorSave::wall_now() + it.delivery_sec;
                        app().save().save(w);
                        seen_ = w;
                        confirm_ = "ORDERED " + it.name + "!";
                        confirm_until_ = time() + 2.5f;
                        std::fprintf(stdout,
                                     "[shop] ORDERED %s price=%d -> arrives in %ds (claim on arrival)\n",
                                     it.name.c_str(), it.price, it.delivery_sec);
                        std::fflush(stdout);
                    } else {
                    WarriorSave::OwnedItem oi;
                    oi.name = it.name;
                    oi.count = 1;
                    // Tutorial-buy force-equip (JS `Ao` Qg: `Pa.iwa(b) &&
                    // xa.$o(b)` — L1120): WEAPON_KNIVES in tutorial context
                    // equips into its slot and advances the step to MAP
                    // (row 4). Other buys keep the no-equip behavior.
                    const bool tut_buy =
                        it.name == "WEAPON_KNIVES" &&
                        (w.story_step() == "STEP_BUY_ITEM" ||
                         (w.story_step().empty() && w.tutorial == "MOVE"));
                    if (tut_buy) {
                        if (it.type == "Armor") w.armor = it.name;
                        else if (it.type == "Helm") w.helm = it.name;
                        else if (it.type == "Ranged") w.ranged = it.name;
                        else if (it.type == "Magic") w.magic = it.name;
                        else w.weapon = it.name;
                        oi.equipped = true;
                        w.set_story_step("MAP");
                    }
                    w.items.push_back(oi);
                    app().save().save(w);
                    seen_ = w;
                    confirm_ = "BOUGHT " + it.name + "!";
                    confirm_until_ = time() + 2.5f;
                    std::fprintf(stdout,
                                 "[shop] BOUGHT %s (%s) price=%d -> money %d, item added%s\n",
                                 it.name.c_str(), it.subtype.c_str(), it.price, w.money,
                                 tut_buy ? " + EQUIPPED, step -> MAP (Ao)" : "");
                    std::fflush(stdout);
                    }  // else: instant grant (Cba path)
                } else {
                    std::fprintf(stdout, "[shop] NOT ENOUGH MONEY for %s (need %d, have %d)\n",
                                 it.name.c_str(), it.price, w.money);
                    std::fflush(stdout);
                }
            }
        }
    }
    // Shared `za` nav column (JS `ma.D1`): Dojo/Map/Profile/Settings hops.
    za_update(app(), *this, kScreenShop);
}

void ShopScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        sf2::render::Camera ui_cam;
        ui_cam.center_x = kViewW * 0.5f;
        ui_cam.center_y = kViewH * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = kViewW;
        ui_cam.view_h = kViewH;
        ui_cam.arena_h = kViewH;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = kViewW * 0.5f;
        ren.draw_sprite(*dojo, ui_cam);
    }
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.35f);

    // Bottom tab strip (JS `ss`/`Eg` L1851-1853, L2283-2284): a full-width
    // bar + `Le` buttons (id 248 shop atlas `buttons/<Category>[_active]`),
    // scaled to the bar height; flat fallback only on a real frame miss.
    {
        const ShopTabLayout tl = shop_tab_layout();
        const float bar[] = {0, kViewH - tl.bar_h, kViewW, kViewH - tl.bar_h, kViewW, kViewH,
                             0, kViewH - tl.bar_h, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(bar, 6, 0.21f, 0.21f, 0.21f, 1.0f);
        for (int t = 0; t < kShopTabCount; ++t) {
            const float cx = tl.cx0 + static_cast<float>(t) * tl.step;
            const bool sel = t == tab_;
            const bool hov = t == tab_hover_;
            const char* art = shop_tab_art(t, sel || hov);
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
    // Item grid inside the responsive `Oa.layout` `gb` viewer rect
    // (`shop_layout()`, JS L2293-2295).
    const ShopLayout sl = shop_layout();
    const std::vector<std::size_t> rows = shop_tab_rows(items_, tab_);
    if (rows.empty()) {
        draw_ui_label(app, sl.viewer.J, sl.viewer.P, sl.viewer.width(), 26.0f,
                          "No items in this category yet.", 0.8f, UiAlign::Left, 0.7f, 0.7f, 0.7f);
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const CatalogItem& it = items_[rows[i]];
        const int col = static_cast<int>(i % 2);
        const int row = static_cast<int>(i / 2);
        const float cx = sl.x0 + static_cast<float>(col) * sl.dx;
        const float cy = sl.y0 + static_cast<float>(row) * sl.dy;
        const float card_w = sl.card_w, card_h = sl.card_h;
        const bool hovered = static_cast<int>(i) == hover_;
        // Item image (JS `ns.j5` L2307: `Rf(Ye.qI(item.fileName))` ->
        // res/items/images-1x/<dir>/<file>; `it.image` is the list.xml Image
        // ref, e.g. "Weapon1.img_weapon_knives"). The shipped item art is a
        // standalone texture, so it is drawn directly; a genuine miss keeps
        // the flat card. Replaces the invented `attributes/*` stand-ins
        // (PORT_AUDIT_UI 3 #18).
        bool drawn = draw_item_image(app, it.image, cx, cy, card_w * 0.7f, card_h * 0.8f, 0.95f);
        if (!drawn) {
            // Equipped cards read gold (distinct from owned/unowned at a
            // glance); hover still brightens.
            const bool card_equipped = seen_.has_item(it.name) &&
                                       shop_slot_for(seen_, it.type) == it.name;
            const float r = card_equipped ? 0.72f : (hovered ? 0.75f : 0.45f);
            const float g = card_equipped ? 0.60f : (hovered ? 0.6f : 0.35f);
            const float b = card_equipped ? 0.25f : (hovered ? 0.3f : 0.2f);
            draw_flat_button(app, it.name, cx, cy, card_w, card_h, r, g, b, hovered);
        }
        // Owned / equipped markers (JS `zf` inventory + `hk` slots — save
        // fields readable; render reads the update snapshot only).
        const bool owned = seen_.has_item(it.name);
        const bool equipped = owned && shop_slot_for(seen_, it.type) == it.name;
        draw_ui_label(app, cx - card_w / 2 + 12.0f, cy - card_h / 2 + 6.0f, card_w - 24.0f, 24.0f,
                          it.name, 0.7f, UiAlign::Left, 1.0f, 1.0f, 1.0f);
        draw_ui_label(app, cx - card_w / 2 + 12.0f, cy + card_h / 2 - 50.0f, card_w - 24.0f, 22.0f,
                          shop_stat_line(it), 0.65f, UiAlign::Left, 0.9f, 0.9f, 0.9f);
        if (equipped) {
            draw_ui_label(app, cx - card_w / 2 + 12.0f, cy + card_h / 2 - 28.0f, card_w - 24.0f, 22.0f,
                              "EQUIPPED", 0.65f, UiAlign::Left, 0.4f, 1.0f, 0.4f);
        } else if (owned) {
            draw_ui_label(app, cx - card_w / 2 + 12.0f, cy + card_h / 2 - 28.0f, card_w - 24.0f, 22.0f,
                              "OWNED", 0.65f, UiAlign::Left, 1.0f, 0.85f, 0.4f);
        }
    }
    if (!try_draw_atlas_button(app, "Arrow", 64.0f, 40.0f, 88.0f, 48.0f, 1.0f)) {
        draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
        draw_ui_label(app, 64.0f - 44.0f + 6.0f, 40.0f - 10.0f, 88.0f - 12.0f, 20.0f,
                          "BACK", 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    }
    // Wielding summary + buy confirmation (display only).
    // Wallet top-right (was invisible — affordability guessing papercut).
    {
        char mbuf[64];
        std::snprintf(mbuf, sizeof(mbuf), "COINS %d", seen_.money);
        draw_ui_label(app, 1060.0f, 32.0f, 180.0f, 24.0f,
                      mbuf, 0.9f, UiAlign::Right, 1.0f, 0.9f, 0.4f);
    }
    // Incoming deliveries (mirrors the update rects above): name + countdown
    // or READY-claim hint, capped at 3 rows.
    {
        const std::int64_t now = WarriorSave::wall_now();
        int row = 0;
        for (const auto& kv : seen_.timers) {
            if (row >= 3) break;
            const float ry = 84.0f + static_cast<float>(row) * 24.0f;
            ++row;
            const std::int64_t left = kv.second - now;
            const std::string text =
                kv.first + (left > 0 ? " " + shop_countdown(left) : " READY");
            draw_ui_label(app, 950.0f, ry - 8.0f, 300.0f, 20.0f,
                      text, 0.7f, UiAlign::Left, 1.0f, 1.0f, 1.0f);
        }
    }
    draw_ui_label(app, 24.0f, 648.0f, 700.0f, 22.0f,
                      wielding_line(app, seen_), 0.7f, UiAlign::Left, 0.9f, 0.9f, 0.9f);
    if (!confirm_.empty() && time() <= confirm_until_) {
        draw_ui_label(app, kViewW * 0.5f - 220.0f, 678.0f, 440.0f, 26.0f,
                          confirm_, 1.0f, UiAlign::Center, 0.4f, 1.0f, 0.4f);
    }
    // Shared `za` chrome (JS `ma.D1`): topPanel + widgets + vertical nav.
    draw_za_chrome(app, kScreenShop);
}

// ---------------------------------------------------------------------------
// Profile `cs` tab strip (JS L2188: class `cs extends Eg`, 4 `Le` on the
// profile atlas id 258, `Tw=[0,1,2,3]`). `Eg` is the shared bottom tab strip
// (L1851: height = za.Sp*1.3, `node.D(rect.v - height)`, buttons scaled
// `height/button.Y.fa.y`, spread lc-dependent). The `y.*` frame table for
// `cs` (`y.WRa/YRa/XRa` ...) is OPEN (PORT_AUDIT_UI §5 OPEN #2); the art
// names below are the profile atlas `buttons/*` frames (sourceSize 199x190).
// Tab content: `vb.hla` (L2190-2191) routes tab 0 -> `Rl=ds`, tab 1 ->
// `qv=es`, tab 2 -> `Zr=fs`, tab 3 -> `lv=gs`. Only `ds`/`es` are modelled
// (equipment interim + folded Moves); tabs 2/3 (`fs` and `gs`, L2193) are
// OPEN — their builders are not in the static extract, so the native keeps
// the flat placeholder for those tabs. Nav/`cs` badges (`Dg`, L1850-1851):
// `cs.getCounterValue` (L2189) reads `p.o.co.uCa()/p.o.sCa()/p.o.yi.rCa()/
// p.o.vCa()` and `ss` (L2284) `p.items.T5a(Cj.zxb(a))` — the badge COUNTS are
// not derivable from the native save (OPEN); the `Dg.ba(65)`/`Ia(128)`
// geometry is ported in `draw_za_chrome`'s badge path.
// ---------------------------------------------------------------------------
constexpr int kProfileTabCount = 4;
constexpr int kProfileTabEquip = 0;  // equipment interim (JS moves equip to shop `$o`, OPEN)
constexpr int kProfileTabMoves = 1;  // folded Moves sub-view (JS `qv`, To.kOa=11 L2201)

struct ProfileTabArt {
    const char* normal;
    const char* active;
    const char* pushed;
    const char* label;
};
const ProfileTabArt kProfileTabs[kProfileTabCount] = {
    {"buttons/Strikes", "buttons/Strikes_active", "buttons/Strikes_pushed", "SKILLS"},
    {"buttons/Progress", "buttons/Progress_active", "buttons/Progress_pushed", "MOVES"},
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
};

ProfileTabLayout profile_tab_layout() {
    const ZaLayout z = za_layout();
    ProfileTabLayout t;
    t.bar_h = z.sp * 1.3f;                           // Eg.aa: za.Sp*1.3
    constexpr float kSrcW = 199.0f, kSrcH = 190.0f;  // profile Le sourceSize
    const float scale = t.bar_h / kSrcH;             // Eg: height/button.Y.fa.y
    t.btn_h = t.bar_h;
    t.btn_w = kSrcW * scale;
    constexpr float kSpread = 1.2f;                  // Eg `b` (lc>1 clamp)
    t.step = t.btn_w * kSpread;
    const float total = t.btn_w + t.step * static_cast<float>(kProfileTabCount - 1);
    t.cx0 = (kViewW - total) * 0.5f + t.btn_w * 0.5f;
    t.cy = kViewH - t.bar_h * 0.5f;
    return t;
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
void draw_profile_tabs(App& app, int tab, int hover) {
    sf2::render::Renderer& ren = app.renderer();
    const ProfileTabLayout t = profile_tab_layout();
    const float bar[] = {0, kViewH - t.bar_h, kViewW, kViewH - t.bar_h, kViewW, kViewH,
                         0, kViewH - t.bar_h, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(bar, 6, 0.16f, 0.16f, 0.18f, 1.0f);
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
            continue;
        }
        draw_flat_button(app, art.label, cx, t.cy, t.btn_w, t.btn_h,
                         sel ? 0.55f : (hov ? 0.45f : 0.32f), 0.4f, 0.28f, hov);
        draw_ui_label(app, cx - t.btn_w * 0.5f, t.cy - 10.0f, t.btn_w, 20.0f,
                      art.label, 0.6f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// EquipmentScreen
// ---------------------------------------------------------------------------

// Equipped-slot stat value/tag (display only; Ranged has no damage attr in
// list.xml — Level stands in, flagged here and in the delta line).
int equip_stat_value(const CatalogItem& ci) {
    if (ci.type == "Weapon") return ci.weapon_damage;
    if (ci.type == "Armor") return ci.body_defense;
    if (ci.type == "Helm") return ci.head_defense;
    if (ci.type == "Magic") return ci.magic_damage;
    return ci.level;
}

const char* equip_stat_tag(const std::string& type) {
    if (type == "Weapon" || type == "Magic") return "DMG";
    if (type == "Armor" || type == "Helm") return "DEF";
    return "Lv";
}

EquipmentScreen::EquipmentScreen(ScreenManager& mgr) : Screen(mgr, "Equipment") {
    // The FULL catalog — the owned base items (Body/Head/Fists) are
    // ShopHide/Hidden and absent from the shop-visible list; the grid
    // must resolve their type/subtype to place the cards.
    catalog_ = load_full_catalog(app());
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
        fig.build_move_list_locks(assets.moves, owned_items(app()), true);
        move_total_ = static_cast<int>(fig.hb().size());
        for (const sf2::scene::MoveDef* m : fig.hb()) {
            if (m == nullptr) continue;
            MoveRow r;
            r.name = m->name;
            r.type = m->type;
            r.priority = m->priority;
            move_rows_.push_back(r);
        }
        std::fprintf(stdout, "[profile] moves tab: %s, %d moves\n", weapon_.c_str(),
                     move_total_);
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[profile] move list load failed: %s\n", e.what());
    }
}

void EquipmentScreen::update_impl(float dt) {
    (void)dt;
    const App::PointerState& p = app().pointer();
    WarriorSave w;
    try {
        w = app().save().load();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[equip] save load failed: %s\n", e.what());
        return;
    }
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
    // `cs` bottom tab strip (JS L2188): select the Profile sub-view
    // (0 = equipment interim, 1 = folded Moves, 2/3 = OPEN stubs).
    tab_hover_ = profile_tab_hit(p.x, p.y);
    if (tab_hover_ >= 0 && p.pressed) {
        sf2::audio::AudioEngine::instance().play("click");
        std::fprintf(stdout, "[profile] tab %d (%s)\n", tab_hover_,
                     kProfileTabs[tab_hover_].label);
        std::fflush(stdout);
        tab_ = tab_hover_;
    }
    // The owned items grid (equipment tab only): click to equip into its
    // type's slot. `card`
    if (tab_ == kProfileTabEquip) {
    // counts equippable-type cards (all five slots: Weapon/Armor/Helm/
    // Ranged/Magic — JS `xc.hk` slots) EXCEPT the NoRanged/NoMagic
    // placeholders: those are the empty-slot markers (never bought, equip
    // is a no-op), and showing them would shift the bought-knives card off
    // the headless-loop click spot. Other owned rows are skipped without
    // consuming a grid slot.
    const float grid_x = kViewW * 0.55f, grid_y0 = 220.0f, grid_dx = 240.0f, grid_dy = 110.0f;
    int idx = 0;
    int card = 0;
    for (const auto& oi : w.items) {
        std::string type, subtype;
        for (const CatalogItem& ci : catalog_) {
            if (ci.name == oi.name) {
                type = ci.type;
                subtype = ci.subtype;
                break;
            }
        }
        if (oi.name == "NoRanged" || oi.name == "NoMagic") {
            ++idx;
            continue;
        }
        if (type != "Weapon" && type != "Armor" && type != "Helm" && type != "Ranged" &&
            type != "Magic") {
            ++idx;
            continue;
        }
        const int col = card % 2;
        const int row = card / 2;
        const float cx = grid_x + col * grid_dx;
        const float cy = grid_y0 + row * grid_dy;
        if (p.x >= cx - 110 && p.x <= cx + 110 && p.y >= cy - 40 && p.y <= cy + 40) {
            hover_ = idx;
            if (p.pressed) {
                // JS `$g.$o` (L152184): `p.o.Ca.hk(a.type, a)` sets the
                // slot, `setItem`, `save()`.
                WarriorSave w2 = app().save().load();
                std::string* slot_val = nullptr;
                if (type == "Weapon") slot_val = &w2.weapon;
                else if (type == "Armor") slot_val = &w2.armor;
                else if (type == "Helm") slot_val = &w2.helm;
                else if (type == "Ranged") slot_val = &w2.ranged;
                else slot_val = &w2.magic;
                *slot_val = oi.name;
                for (auto& oi2 : w2.items) {
                    if (oi2.name == oi.name) oi2.equipped = true;
                }
                app().save().save(w2);
                std::fprintf(stdout,
                             "[equip] EQUIPPED %s (%s) -> %s slot (weapon=%s armor=%s helm=%s ranged=%s magic=%s); move list rebuilt on next fight\n",
                             oi.name.c_str(), subtype.c_str(), type.c_str(), w2.weapon.c_str(),
                             w2.armor.c_str(), w2.helm.c_str(), w2.ranged.c_str(),
                             w2.magic.c_str());
                std::fflush(stdout);
            }
        }
        ++idx;
        ++card;
    }
    }  // end equipment-tab grid
    // Shared `za` nav column (JS `ma.D1`): Dojo/Map/Shop/Settings hops.
    za_update(app(), *this, kScreenProfile);
}

void EquipmentScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    sf2::scene::Sprite* dojo = app.dojo_sprite();
    if (dojo != nullptr) {
        sf2::render::Camera ui_cam;
        ui_cam.center_x = kViewW * 0.5f;
        ui_cam.center_y = kViewH * 0.5f;
        ui_cam.zoom = 1.0f;
        ui_cam.view_w = kViewW;
        ui_cam.view_h = kViewH;
        ui_cam.arena_h = kViewH;
        ui_cam.arena_floor = 0.0f;
        ui_cam.arena_center_x = kViewW * 0.5f;
        ren.draw_sprite(*dojo, ui_cam);
    }
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.35f);
    // `cs` tab strip (JS L2188) — always visible; the body below is per-tab.
    draw_profile_tabs(app, tab_, tab_hover_);

    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return;
    }
    if (tab_ == kProfileTabEquip) {
    // --- Profile header (read-only warrior stats) -------------------------
    // Level + OLa exp bar (character_progress.xml thresholds, 100 fallback),
    // total wins (Fights/yc records), coins (Money/Tb) + gems (Bonus/$F per
    // SHOP_STATIC §1 `I.$F`). Entry: Dojo/Profile buttons (screen 7).
    {
        const int need = ResultsScreen::exp_for_level(w.level);
        int wins = 0;
        for (const auto& f : w.fights) wins += f.wins;
        char hbuf[64];
        std::snprintf(hbuf, sizeof(hbuf), "LV %d", w.level);
        // Profile-atlas level badge (pieces/level1..9 — clamped to the
        // shipped range; skipped if the frame is missing).
        if (w.level >= 1 && w.level <= 9) {
            char lvl_frame[32];
            std::snprintf(lvl_frame, sizeof(lvl_frame), "pieces/level%d", w.level);
            try_draw_atlas_button(app, lvl_frame, 100.0f, 92.0f, 56.0f, 56.0f, 1.0f);
        }
        draw_ui_label(app, 130.0f, 78.0f, 150.0f, 30.0f,
                      hbuf, 1.1f, UiAlign::Left, 1.0f, 0.9f, 0.4f);
        const float bx0 = 130.0f, by0 = 112.0f, bw = 300.0f, bh = 16.0f;
        const float bbg[] = {bx0, by0, bx0 + bw, by0, bx0, by0 + bh,
                             bx0 + bw, by0, bx0 + bw, by0 + bh, bx0, by0 + bh};
        ren.draw_triangles(bbg, 6, 0.15f, 0.15f, 0.18f, 1.0f);
        const float frac = need > 0 ? std::clamp(static_cast<float>(w.experience) /
                                                     static_cast<float>(need),
                                                 0.0f, 1.0f)
                                    : 0.0f;
        if (frac > 0.001f) {
            const float fw = bw * frac;
            const float bfg[] = {bx0, by0, bx0 + fw, by0, bx0, by0 + bh,
                                 bx0 + fw, by0, bx0 + fw, by0 + bh, bx0, by0 + bh};
            ren.draw_triangles(bfg, 6, 0.3f, 0.7f, 1.0f, 1.0f);
        }
        char xbuf[64];
        std::snprintf(xbuf, sizeof(xbuf), "EXP %d/%d", w.experience, need);
        draw_ui_label(app, 440.0f, 104.0f, 300.0f, 24.0f,
                      xbuf, 0.7f, UiAlign::Left, 0.9f, 0.9f, 0.9f);
        char mbuf[128];
        std::snprintf(mbuf, sizeof(mbuf), "WINS %d    COINS %d    GEMS %d", wins, w.money,
                      w.bonus);
        draw_ui_label(app, 130.0f, 134.0f, 600.0f, 24.0f,
                      mbuf, 0.8f, UiAlign::Left, 1.0f, 1.0f, 1.0f);
    }
    // --- 5 equipment slots (JS `xc.hk` slots; Ranged/Magic included) -------
    // Slot glow follows the HOVERED ITEM's type (the old `hover_ == s`
    // compared an item index against a slot index — coincidental flashes).
    std::string hover_type;
    if (hover_ >= 0 && static_cast<std::size_t>(hover_) < w.items.size()) {
        for (const CatalogItem& ci : catalog_) {
            if (ci.name == w.items[static_cast<std::size_t>(hover_)].name) {
                hover_type = ci.type;
                break;
            }
        }
    }
    const float slot_x = kViewW * 0.2f, slot_y0 = 220.0f, slot_dy = 100.0f;
    const char* slot_names[5] = {"Weapon", "Armor", "Helm", "Ranged", "Magic"};
    // Profile-atlas slot backing art (profile.<hash>.json pieces/*): the
    // perkback square behind each slot, perkcircle for the empty marker.
    const std::string current[5] = {w.weapon, w.armor, w.helm, w.ranged, w.magic};
    for (int s = 0; s < 5; ++s) {
        const float sy = slot_y0 + static_cast<float>(s) * slot_dy;
        std::string stat;
        for (const CatalogItem& ci : catalog_) {
            if (ci.name == current[s]) {
                char sbuf[96];
                std::snprintf(sbuf, sizeof(sbuf), "%s %d", equip_stat_tag(ci.type),
                              equip_stat_value(ci));
                stat = sbuf;
                break;
            }
        }
        const std::string label = std::string(slot_names[s]) + ": " + current[s] +
                                  (stat.empty() ? "" : " (" + stat + ")");
        const bool slot_hov = hover_type == slot_names[s];
        // Real art first (perkback square + the type's shop attribute icon
        // centered); flat fallback keeps the slot visible if art is missing.
        bool drawn = false;
        if (try_draw_atlas_button(app, "pieces/perkback", slot_x, sy, 400.0f, 80.0f,
                                  slot_hov ? 0.95f : 0.8f)) {
            const char* icon = shop_item_art(slot_names[s], slot_hov);
            if (icon != nullptr) {
                try_draw_atlas_button(app, icon, slot_x - 170.0f, sy, 64.0f, 64.0f, 1.0f);
            }
            drawn = true;
        }
        if (!drawn) {
            draw_flat_button(app, label, slot_x, sy, 400.0f, 80.0f, 0.35f, 0.3f, 0.45f,
                             slot_hov);
        }
        draw_ui_label(app, slot_x - 200.0f + 8.0f, sy - 14.0f, 400.0f - 16.0f, 28.0f,
                          label, 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    }
    const float grid_x = kViewW * 0.55f, grid_y0 = 220.0f, grid_dx = 240.0f, grid_dy = 110.0f;
    int idx = 0;
    int card = 0;
    for (const auto& oi : w.items) {
        std::string type;
        for (const CatalogItem& ci : catalog_) {
            if (ci.name == oi.name) {
                type = ci.type;
                break;
            }
        }
        if (oi.name == "NoRanged" || oi.name == "NoMagic") {
            ++idx;
            continue;
        }
        if (type != "Weapon" && type != "Armor" && type != "Helm" && type != "Ranged" &&
            type != "Magic") {
            ++idx;
            continue;
        }
        const int col = card % 2;
        const int row = card / 2;
        const float cx = grid_x + col * grid_dx;
        const float cy = grid_y0 + row * grid_dy;
        const bool equipped = oi.equipped;
        // Real art: perkback card + the type's attribute icon (profile +
        // shop atlases); flat fallback keeps the card visible.
        bool drawn = false;
        if (try_draw_atlas_button(app, "pieces/perkback", cx, cy, 220.0f, 80.0f,
                                  equipped ? 1.0f : (hover_ == idx ? 0.95f : 0.8f))) {
            const char* icon = shop_item_art(type, equipped || hover_ == idx);
            if (icon != nullptr) {
                try_draw_atlas_button(app, icon, cx - 80.0f, cy, 56.0f, 56.0f, 1.0f);
            }
            drawn = true;
        }
        if (!drawn) {
            draw_flat_button(app, oi.name + (equipped ? " [EQ]" : ""), cx, cy, 220.0f, 80.0f,
                             equipped ? 0.5f : 0.3f, equipped ? 0.6f : 0.3f,
                             equipped ? 0.3f : 0.35f, hover_ == idx);
        }
        draw_ui_label(app, cx - 110.0f + 6.0f, cy - 12.0f, 220.0f - 12.0f, 24.0f,
                          oi.name + (equipped ? " [EQ]" : ""), 0.7f, UiAlign::Center,
                          1.0f, 1.0f, 1.0f);
        ++idx;
        ++card;
    }
    // Stat delta preview (read-only): the hovered owned card vs the wielded
    // same-type item. hover_ indexes the owned list (update_impl parity).
    {
        std::string dline = "Hover an owned item to preview its stats.";
        if (hover_ >= 0 && static_cast<std::size_t>(hover_) < w.items.size()) {
            const std::string& hov_name = w.items[static_cast<std::size_t>(hover_)].name;
            const CatalogItem* hov_ci = nullptr;
            for (const CatalogItem& ci : catalog_) {
                if (ci.name == hov_name) {
                    hov_ci = &ci;
                    break;
                }
            }
            if (hov_ci != nullptr) {
                std::string cur_name;
                if (hov_ci->type == "Armor") cur_name = w.armor;
                else if (hov_ci->type == "Helm") cur_name = w.helm;
                else if (hov_ci->type == "Ranged") cur_name = w.ranged;
                else if (hov_ci->type == "Magic") cur_name = w.magic;
                else cur_name = w.weapon;
                if (cur_name == hov_name) {
                    dline = hov_name + " (wielded)";
                } else {
                    int cur_stat = 0;
                    for (const CatalogItem& ci : catalog_) {
                        if (ci.name == cur_name) {
                            cur_stat = equip_stat_value(ci);
                            break;
                        }
                    }
                    const int nw = equip_stat_value(*hov_ci);
                    char dbuf[160];
                    std::snprintf(dbuf, sizeof(dbuf), "%s %s %d -> %d (%+d)",
                                  hov_name.c_str(), equip_stat_tag(hov_ci->type), cur_stat,
                                  nw, nw - cur_stat);
                    dline = dbuf;
                }
            }
        }
        draw_ui_label(app, 24.0f, 664.0f, 760.0f, 24.0f,
                      dline, 0.75f, UiAlign::Left, 0.9f, 0.9f, 0.9f);
    }
    } else if (tab_ == kProfileTabMoves) {
        // Folded Moves sub-view (JS `qv`, To.kOa=11 L2201) — the learned
        // moves for the wielded weapon; moved verbatim from the deleted
        // standalone MovesScreen.
        (void)app.draw_text(130.0f, 84.0f, "MOVES - " + weapon_, 1.1f, 1.0f, 0.9f, 0.4f);
        constexpr std::size_t kMaxRows = 16;
        for (std::size_t i = 0; i < move_rows_.size() && i < kMaxRows; ++i) {
            const MoveRow& r = move_rows_[i];
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s  [%s] P%d", r.name.c_str(),
                          r.type.empty() ? "-" : r.type.c_str(), r.priority);
            (void)app.draw_text(150.0f, 140.0f + static_cast<float>(i) * 30.0f, buf, 0.75f,
                                1.0f, 1.0f, 1.0f);
        }
        if (move_total_ > static_cast<int>(kMaxRows)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "+%d more (%d total)",
                          move_total_ - static_cast<int>(kMaxRows), move_total_);
            (void)app.draw_text(150.0f, 140.0f + 16.0f * 30.0f, buf, 0.75f, 0.7f, 0.7f,
                                0.7f);
        }
        if (move_rows_.empty()) {
            (void)app.draw_text(150.0f, 140.0f, "No moves for this weapon.", 0.8f, 0.7f,
                                0.7f, 0.7f);
        }
    } else {
        // Tabs 2/3: the `vb` sub-views (JS `fs`/`gs`, To.kOa 12/13) are not
        // reproduced — no static content rule recovered (OPEN). The `cs`
        // strip still selects them so the surface exists.
        draw_ui_label(app, kViewW * 0.5f - 300.0f, 300.0f, 600.0f, 40.0f,
                      std::string("PROFILE TAB ") + kProfileTabs[tab_].label + " (OPEN)",
                      1.0f, UiAlign::Center, 0.8f, 0.8f, 0.8f);
    }
    // The BACK button (top-left).
    draw_flat_button(app, "BACK", 64.0f, 40.0f, 88.0f, 48.0f, 0.3f, 0.3f, 0.4f, false);
        draw_ui_label(app, 64.0f - 44.0f + 6.0f, 40.0f - 10.0f, 88.0f - 12.0f, 20.0f,
                          "BACK", 0.7f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    // Shared `za` chrome (JS `ma.D1`): topPanel + widgets + vertical nav.
    draw_za_chrome(app, kScreenProfile);
}

// ---------------------------------------------------------------------------
// SettingsScreen
// ---------------------------------------------------------------------------

SettingsScreen::SettingsScreen(ScreenManager& mgr) : Screen(mgr, "Settings") {}

void SettingsScreen::update_impl(float dt) {
    ++age_;  // press debounce: ignore the push-frame held click
    (void)dt;
    const App::PointerState& p = app().pointer();
    hover_ = -1;
    // BACK (`Bb` "BACK", `un.Kb`, L1930 -> `Ge(0)` closes) -> pop.
    const SettingsLayout s = settings_layout();
    if (p.x >= s.back_cx - s.btn_w * 0.5f && p.x <= s.back_cx + s.btn_w * 0.5f &&
        p.y >= s.back_cy - s.btn_h * 0.5f && p.y <= s.back_cy + s.btn_h * 0.5f) {
        hover_ = 0;
        if (p.pressed && age_ > 10) {
            std::fprintf(stdout, "[settings] BACK -> previous screen\n");
            std::fflush(stdout);
            manager().pop();
            return;
        }
    }
    // MUSIC toggle (working): OFF stops the track, ON replays the last
    // track (play_music of the current track; silent no-op when none —
    // AudioEngine semantics, no scene touch).
    if (p.x >= s.row_x && p.x <= s.row_x + s.row_w && p.y >= s.row_y[1] &&
        p.y <= s.row_y[1] + s.row_h) {
        hover_ = 1;
        if (p.pressed) {
            music_off_ = !music_off_;
            if (music_off_) {
                sf2::audio::AudioEngine::instance().stop_music();
            } else {
                sf2::audio::AudioEngine::instance().play_music(
                    sf2::audio::AudioEngine::instance().music_track());
            }
            sf2::audio::AudioEngine::instance().play("click");
            std::fprintf(stdout, "[settings] music %s\n", music_off_ ? "OFF" : "ON");
            std::fflush(stdout);
        }
    }
    // SOUND row is state display only (no runtime SFX mute/set_enabled API
    // exists — see the stream report); intentionally not clickable.
}

void SettingsScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // Minimal options overlay (NOT a standalone screen; PORT_AUDIT_UI §2.1 /
    // §3 item 30). The JS `za` nav button #5 (`y.mRa`/`y.lRa`, L1979) routes
    // to `za.Vfb` (L1981): it appends a `Bi` spinner (frame `y.aoa` =
    // "loading_circle", L1867) and `G.load([250,251,252,253])` (the per-
    // language atlases). On load completion `xvb()` (L1981) tears the spinner
    // down and calls `Xc.Shb()`; `Xc.Shb()` = `Wb.openDialog(310,null)` and
    // `Wb.Xob` case 310 -> `new un`, so the real dialog is `un extends od`
    // (9-slice `E.get(254)`: `y.lSa`="bg", `y.eoa`="bg_edge"; title `y.pB`=
    // "stripe_top"): rows Sound (`y.koa`/`y.loa`), Music (`y.ioa`/`y.joa`),
    // Credits (`y.rSa`), Language + BACK (`EButtonDark`) / RESTART
    // (`EButtonBeige`), gated by `Ca.hasFeature("audio")`/("credits") with
    // labels from the `un` localized `IVa` table.
    // The `od` base is `AV=fc(2340,1530)` per JS L1894 (`b==null&&(b=1530)`);
    // the audit's 2340x1300 is stale. `Md=750` (L1930). The per-language BMF
    // atlas build (`G.Oq(253)` + `un.C8`, L1927) is not modelled, so the row
    // labels use the EN `IVa` strings and the Language row is EN-only.
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.55f);
    // Real `un extends od` dialog (L1916-1930): 9-slice base + title + rows.
    const SettingsLayout s = settings_layout();
    draw_od_base(app, ren, s.panel);
    // Title `Vc`: `IVa.Settings_Title` EN = "SETTINGS" (L1917); `ua(152)` +
    // `La(Z.W6)` (L1900), `Ia(128)` centre.
    draw_ui_label(app, s.title_x, s.title_y, s.title_w, s.title_h, "SETTINGS", 1.52f,
                  UiAlign::Center, 0.404f, 0.243f, 0.141f);
    // Rows from the `un` `IVa` table (L1917-1924). `Ca.hasFeature("audio")`
    // (L1928) gates Sound+Music and `("credits")` (L1929) gates Credits; both
    // features are present, so four rows. Language is EN-only in the native
    // (`G.Rq()` switch `Oyb` L1931 unreachable), so it shows `app.language()`.
    const bool sfx_on = sf2::audio::AudioEngine::instance().enabled();
    const std::string rows[4] = {
        std::string("Sound: ") + (sfx_on ? "ON" : "OFF"),
        std::string("Music: ") + (music_off_ ? "OFF" : "ON"),
        std::string("Credits"),
        std::string("Language: ") + app.language(),
    };
    for (int i = 0; i < 4; ++i) {
        const bool hov = (i == 1 && hover_ == 1);
        draw_flat_button(app, "", s.row_x + s.row_w * 0.5f, s.row_y[i] + s.row_h * 0.5f,
                         s.row_w, s.row_h, hov ? 0.55f : 0.30f, 0.42f, 0.3f, hov);
        draw_ui_label(app, s.row_x + 12.0f, s.row_y[i] + s.row_h * 0.5f - 14.0f,
                      s.row_w - 24.0f, 28.0f, rows[i], 0.9f, UiAlign::Left, 1.0f, 1.0f, 1.0f);
    }
    // Restart notice `Nm` (`dlgSettingsRestart`, `ua(75)`, L1929).
    draw_ui_label(app, kViewW * 0.5f - 500.0f, s.notice_y, 1000.0f, 40.0f,
                  "Attention! Game must be restarted for these settings to apply.",
                  0.75f, UiAlign::Center, 1.0f, 0.8f, 0.5f);
    // BACK (`EButtonDark`) + RESTART (`EButtonBeige`, L1930). `draw_flat_button`
    // draws the plate only; the `Bb` label is a separate text node.
    draw_flat_button(app, "", s.back_cx, s.back_cy, s.btn_w, s.btn_h, 0.35f, 0.3f, 0.28f,
                     hover_ == 0);
    draw_ui_label(app, s.back_cx - s.btn_w * 0.5f, s.back_cy - 14.0f, s.btn_w, 28.0f, "BACK",
                  0.9f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
    draw_flat_button(app, "", s.restart_cx, s.restart_cy, s.btn_w, s.btn_h, 0.6f, 0.5f, 0.3f,
                     false);
    draw_ui_label(app, s.restart_cx - s.btn_w * 0.5f, s.restart_cy - 14.0f, s.btn_w, 28.0f,
                  "RESTART", 0.9f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
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

} // namespace sf2::app

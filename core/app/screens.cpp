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
void draw_ui_wrapped(App& app, float x, float y, float w, float h,
                     const std::string& text, float ua_scale, UiAlign align,
                     float r, float g, float b) {
    if (text.empty() || w <= 0.0f || h <= 0.0f) return;
    const sf2::data::font* font = app.menu_font();
    if (font == nullptr) return;
    // `{br}` inline markup -> hard line break (see expand_br).
    const std::string body = expand_br(text);
    const float scale = ua_scale * ea_a1(app);
    if (scale <= 0.0f) return;
    const float line_step = scale * static_cast<float>(font->line_height);
    if (line_step <= 0.0f) return;
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
    std::vector<std::string> lines;
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
                    lines.push_back(cur.substr(0, sp));
                    cur = cur.substr(sp + 1);
                } else {
                    lines.push_back(cur);
                    cur.clear();
                }
                continue;  // retry this glyph on the new line
            }
            cur = cand;
            i = next;
        }
        lines.push_back(cur);
    }
    float yy = y;
    for (const std::string& ln : lines) {
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
void draw_ib_hint(App& app, sf2::render::Renderer& ren, const std::string& speaker,
                  const std::string& line1, const std::string& line2, bool show_ok);
// `od` 9-slice panel geometry (JS L1894-1900) — defined after the modal draw.
struct OdPanel {
    float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;  // on-screen BODY rect
    float c = 1.0f;                                    // design -> screen scale
};
OdPanel od_panel(float src_w, float src_h);
void draw_od_base(App& app, sf2::render::Renderer& ren, const OdPanel& p);

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
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.502f);  // `Wb.Qa` = 0x80 black
    // `Notification` -> the `Ib` hint bar (L1045-1050), no `od` panel.
    if (d->type == "Notification") {
        draw_ib_hint(app, ren, d->title, d->lines.empty() ? "" : d->lines[0],
                     d->lines.size() > 1 ? d->lines[1] : "", /*show_ok=*/true);
        return;
    }
    // `od` 9-slice: fit the AV design rect into the view (`l4a` L1895-1896),
    // draw the `bg` body + `bg_edge` caps, then the `Vc` title. The AV is
    // `new fc(a,b)` with JS defaults (2340,1530) — `od` ctor L1894
    // `b==null&&(b=1530)`; the quest dialog is `ph extends od` with `super()`
    // (L1963-1964, no explicit size), so 1530 (NOT the audit's stale 1300).
    const OdPanel panel = od_panel(2340.0f, 1530.0f);
    const float c = panel.c;
    const float px = panel.px, py = panel.py, pw = panel.pw, ph = panel.ph;
    draw_od_base(app, ren, panel);
    // `Vc` title (`Fa(1560,160)`, `ua(152)`, color `Z.W6` = 0.404/0.243/0.141).
    // The Title attr is a lang key (`characterSensei` -> "СЭНСЭЙ" RU).
    const float title_w = 1560.0f * c, title_h = 160.0f * c;
    draw_ui_label(app, px + pw * 0.5f - title_w * 0.5f, py + 8.0f * c, title_w, title_h,
                  loc(app, d->title, d->title), 1.0f, UiAlign::Center, 0.404f, 0.243f, 0.141f);
    // Body `Cd`: the JS runs the body text multiline (`ea.rd(!0)`) — wrap each
    // line into the panel width and clip at the panel bottom (`Qh.apply`
    // L1628-1629; the `Sk` single-line fit is L1626-1627). Replaces the old
    // single-line draw with a 44*c step that overlapped/clipped the longer
    // Sensei lines.
    const float body_y = py + title_h + 24.0f * c;
    const float body_h = (py + ph) - body_y - 24.0f * c;
    std::string body;
    for (std::size_t i = 0; i < d->lines.size(); ++i) {
        if (i != 0) body += "\n";
        body += d->lines[i];
    }
    draw_ui_wrapped(app, px + 80.0f * c, body_y, pw - 160.0f * c, body_h, body, 0.8f,
                    UiAlign::Left, 1.0f, 1.0f, 1.0f);
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
    // Buttons `Bb.Pb(150)` (L1930): BACK only — the oracle `un` dialog shows a
    // single centred НАЗАД (no RESTART; the native RESTART was invented,
    // FIDELITY_MATRIX settings row).
    s.btn_w = 320.0f * p.c;
    s.btn_h = 150.0f * p.c;
    s.back_cx = cx;
    s.restart_cx = cx + 500.0f * p.c;  // unused (kept for layout parity)
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
    // The body line wraps (`ea.rd(!0)` multiline; `{br}` is a hard break) —
    // the Sensei tutorial notifications (tutorial_move {br} ...) need it.
    const float l1_h = line2.empty() ? 170.0f * c : 70.0f * c;
    draw_ui_wrapped(app, tx, ly(88.0f), 364.0f * c, l1_h, line1, 0.75f, UiAlign::Left,
                    0.184f, 0.145f, 0.106f);
    if (!line2.empty()) {
        draw_ui_label(app, tx, ly(162.0f), 364.0f * c, 30.0f * c, line2, 0.7f, UiAlign::Left,
                      0.184f, 0.145f, 0.106f);
    }
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
// collapsed. The native keeps one shared flag (JS `za.instance` is a
// singleton mounted on every shell screen). `gk.Bgb` (L2000) toggles it.
bool g_za_nav_open = false;  // JS `collapse(0)` (L1978) default = collapsed

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

// Handles the nav-column taps for a shell screen (JS listeners Ofb/Qfb/Wfb/
// Rfb/Vfb -> `ma.Jg().jI(cls)`): a tap pushes the target screen unless it is
// the screen already showing (the JS highlights that one active, `xyb`
// L1982).
void za_update(App& app, Screen& self, ScreenId active, bool force_collapsed = false) {
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
                    sf2::audio::AudioEngine::instance().play("click");
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
    // `force_collapsed` (the Map) draws/behaves collapsed without touching the
    // shared flag, so returning to the Dojo keeps its expanded column.
    if (force_collapsed || !g_za_nav_open) {
        if (!force_collapsed && header_hit && app.pointer().pressed) {
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
void draw_za_chrome(App& app, ScreenId active, const int* badges = nullptr,
                    bool force_collapsed = false) {
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
    const bool nav_expanded = !force_collapsed && g_za_nav_open;
    if (nav_expanded) {
        const float dim[] = {0, 0, w, 0, w, kViewH, 0, 0, w, kViewH, 0, kViewH};
        ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.502f);
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
    if (force_collapsed || !g_za_nav_open) {
        float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
        za_header_rect(hx, hy, hw, hh);
        // The collapsed header over the `gk` scroll art (`Zh` roll frames);
        // the label is `Y.na("menu")` ("МЕНЮ" in the oracle locale).
        load_scroll_atlas(app);
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
                      0.5f, UiAlign::Center, 0.184f, 0.145f, 0.106f);
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

// ---------------------------------------------------------------------------
// VS intro (JS `ik`, g="419", L2069-2074) — the pre-fight VS screen the oracle
// `fight_intro` shows. `ik` (ctor L2069-2071, `aa` L2071-2073, `layout`
// L2073-2074):
//   Qa = R.$(E.get(3,6))        full-screen `res/vs/bg.*` backdrop
//   Sn = Ea(node) C(512) D(286) la(1.6) Wg(27)   stroke container
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

// VS-intro timeline (JS `ik.aa` L2071-2073, `yY` 3.4). The native timeline is
// COMPRESSED so the fixed-frame fidelity captures land on the composed screen
// (`fight_intro`, screen frame 40 ≈ 0.67 s) and the bare fight scene (`pause`,
// ≈ 2.8 s); see the OPEN note on `FightScreen::vs_t_` in screens.hpp.
constexpr float kVsSlideT = 0.35f;   // JS kd0 ed(.6): portraits slide in
constexpr float kVsGlyphT = 0.42f;   // JS kd2 ed(.2): VS glyph fade + scale
constexpr float kVsStrokeT = 0.48f;  // JS kd4/kd5 ed(.1): left/right strokes
constexpr float kVsNameT = 0.45f;    // JS kd7: names appear
constexpr float kVsFadeT = 1.55f;    // JS kd10 fade-out begins
constexpr float kVsTotal = 1.85f;    // JS yY 3.4 (compressed; see header)

// Quadratic ease-out (the JS `dc.Ln()` family; monotone 0->1).
float vs_ease(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return 1.0f - (1.0f - x) * (1.0f - x);
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
    // Stroke pair `KF`/`ux` on `Sn` (C(512,286) la(1.6) `Wg(27)`): the red
    // brush band across the backdrop (`vs/sprites` "left"/"right"). OPEN: the
    // native atlas path has no node rotation, so the `Wg(27)` tilt is not
    // applied — the band is drawn axis-aligned at its 1.6x `Sn` scale, which
    // keeps the red mass in the capture's central band (the rotated edges
    // remain a gap).
    if (t >= kVsStrokeT - kVsStrokeT * 0.5f) {
        const float sa = std::clamp((t - kVsStrokeT * 0.5f) / std::max(0.01f, kVsStrokeT * 0.5f),
                                    0.0f, 1.0f) * alpha;
        try_draw_atlas_button(app, "left", 512.0f * s - 246.0f * s * 1.6f, 286.0f * s,
                              492.0f * s * 1.6f, 242.0f * s * 1.6f, sa);
        try_draw_atlas_button(app, "right", 512.0f * s + 246.0f * s * 1.6f, 286.0f * s,
                              492.0f * s * 1.6f, 254.0f * s * 1.6f, sa);
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
};

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
        {
            std::map<std::string, pugi::xml_node> templates;
            const pugi::xml_node templates_node =
                root.child("Warriors").child("Templates");
            for (const pugi::xml_node t : templates_node.children("Template")) {
                const std::string nm = t.attribute("Name").value();
                if (!nm.empty()) templates.emplace(nm, t);
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
float resolve_player_unarmed_damage(App& app) {
    float starting = 0.0f;
    float per_level = 0.0f;
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
                    starting = sa.attribute("UnarmedDamage").as_float(0.0f);
                }
                if (const pugi::xml_node lg = root.child("LevelAttributeGain")) {
                    per_level = lg.attribute("UnarmedDamage").as_float(0.0f);
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
    // `m7a` item bonus: sum the equipped/owned items' UnarmedDamage rows.
    float item_bonus = 0.0f;
    const std::vector<CatalogItem> catalog = load_full_catalog(app);
    for (const std::string& name : equipped) {
        if (name.empty()) continue;
        for (const CatalogItem& ci : catalog) {
            if (ci.name == name) {
                item_bonus += static_cast<float>(ci.unarmed_damage);
                break;
            }
        }
    }
    return starting + static_cast<float>(level) * per_level + item_bonus;
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
        const float stroke = kv.second * 2.0f * model_scale * camera.zoom;
        if (stroke <= 0.0f) continue;
        const float sx1 = camera.world_to_screen_x(pos[u1] * model_scale + offset_x, 1.0f);
        const float sy1 = camera.world_to_screen_y(pos[u1 + 1] * model_scale + offset_y);
        const float sx2 = camera.world_to_screen_x(pos[u2] * model_scale + offset_x, 1.0f);
        const float sy2 = camera.world_to_screen_y(pos[u2 + 1] * model_scale + offset_y);
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
void draw_destination_backdrop(App& app) {
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
void draw_destination_model(App& app, sf2::render::Renderer& ren,
                            std::unique_ptr<sf2::scene::Fighter>& fighter,
                            bool& tried, bool& ok, const sf2::data::anim_clip*& idle) {
    if (!app.has_fight_assets()) return;
    if (!tried) {
        tried = true;
        FightAssets& assets = app.fight_assets();
        const std::string idle_name = find_idle_clip_name(assets.clips);
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
    sf2::render::Camera cam;
    sf2::scene::LocationScene::destination_camera(cam, kViewW, kViewH);
    fighter->sample(*idle, 0, 0.0f, kDestinationModelLocalY, 1);
    draw_dojo_figure(ren, cam, *fighter, kDestinationModelScale,
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
    // Fresh-profile tutorial gate (blocking; see the header comment). The
    // training fight's win (Results pops back here) completes it, so the hub
    // is clean afterwards.
    if (app().fresh_tutorial()) {
        if (!tut_done_) {
            const PendingBattle& pb = app().pending_battle();
            if (pb.has_result && pb.player_won && pb.battle_name == "Training") {
                tut_done_ = true;
                std::fprintf(stdout, "[tutorial] training fight won -> hub\n");
                std::fflush(stdout);
            }
        }
        if (!tut_done_) {
            update_tutorial();
            return;
        }
    }
    // Sensei modal gate (quest He records): while a dialog is up, taps
    // advance it instead of the chrome (headless auto-drains).
    if (quest_modal_consume(app())) return;
    // The shared `za` nav column (JS `za.Aub` L1978-1980 / `za.Ofb`..`Vfb`):
    // a tap switches to Dojo/Map/Shop/Profile/Settings (JS `ma.Jg().jI`).
    za_update(app(), *this, kScreenDojo);
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

// --- Fresh-profile tutorial (JS StoryTutorialWelcome) ----------------------
// The `Ib` notification banner hit rect (the tap that advances a beat).
// Recomputes the draw_ib_hint layout: 600x250 scroll, top-right, c scale.
bool tutorial_banner_hit(double x, double y) {
    const float c =
        std::clamp(std::min(kViewW * 0.75f, kViewH * 0.75f) / 600.0f, 0.2f, 1.1f);
    const float sp = std::min(kViewH * 0.13f, 100.0f) * 0.78f;  // za.odb L1975
    const float ox = kViewW - 600.0f * c;
    return x >= ox && x <= ox + 600.0f * c && y >= sp && y <= sp + 250.0f * c;
}

// The tutorial Regular dialog layout (`Xc`/`od`; oracle
// oracle_tutorial_modal.png): СЭНСЭЙ title, left portrait, right wrapped body,
// the FIGHT button bottom-centre-right.
struct TutorialDialogLayout {
    OdPanel panel;
    float title_y = 0.0f, title_h = 0.0f;
    float portrait_cx = 0.0f, portrait_cy = 0.0f, portrait = 0.0f;
    float body_x = 0.0f, body_y = 0.0f, body_w = 0.0f, body_h = 0.0f;
    float btn_cx = 0.0f, btn_cy = 0.0f, btn_w = 0.0f, btn_h = 0.0f;
};

TutorialDialogLayout tutorial_dialog_layout() {
    TutorialDialogLayout t;
    t.panel = od_panel(2340.0f, 1530.0f);  // od AV = fc(2340,1530), L1894
    const OdPanel& p = t.panel;
    t.title_h = 160.0f * p.c;
    t.title_y = p.py + p.ph * 0.20f;
    // Portrait: oracle dojo_sensei green circle bbox x307..543 y253..403
    // (diameter ~236, centre ~425,340). The `sensei_portrait` texture is the
    // 512px `character_sensei` (DOJO_BG_STATIC §5); the olive disc is ~55% of
    // the texture, so the drawn quad is ~430px.
    t.portrait = 430.0f;
    t.portrait_cx = p.px + p.pw * 0.251f;   // 425 at 1280x720 (body 864 @208)
    t.portrait_cy = p.py + p.ph * 0.500f;   // 360
    // Body: oracle lines start x~595, first top y~249, pitch ~50
    // (`draw_ui_wrapped` ua 0.70 -> drawn scale 0.56 at RU ea.a1=0.8).
    t.body_x = p.px + p.pw * 0.436f;
    t.body_y = p.py + p.ph * 0.325f;
    t.body_w = p.pw * 0.490f;
    t.body_h = p.ph * 0.42f;
    t.btn_w = 270.0f;
    t.btn_h = 60.0f;
    t.btn_cx = p.px + p.pw * 0.755f;
    t.btn_cy = p.py + p.ph * 0.762f;
    return t;
}

void DojoScreen::draw_tutorial(App& app, sf2::render::Renderer& ren) {
    ensure_lang(app);
    if (tut_beat_ <= 1) {
        // Notification (`He` -> `Ib`, L1045-1050): the sensei-small portrait
        // banner with the beat body. No speaker label (the oracle notification
        // shows only the body; `characterSensei` is the Regular title).
        const bool move = tut_beat_ == 0;
        const std::string body = loc(
            app, move ? "tutorial_move" : "tutorial_punchbag",
            move ? "Let me see you move! Show me what a shadow can do."
                 : "Fascinating... Now, see that punching bag? Attack it!");
        // The oracle tutorial notification has no OK button (the whole banner
        // is the tap target); `show_ok=false` also avoids the OK plate
        // overlapping the wrapped body.
        draw_ib_hint(app, ren, "", body, "", /*show_ok=*/false);
        return;
    }
    // Regular dialog (`he`/`Xc`): `od` base + title + portrait + wrapped body
    // + the localized FIGHT button (`dlgStoryBtnFight`).
    const float dim[] = {0, 0, kViewW, 0, kViewW, kViewH, 0, 0, kViewW, kViewH, 0, kViewH};
    ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.502f);  // `Wb.Qa` = 0x80 black
    const TutorialDialogLayout t = tutorial_dialog_layout();
    draw_od_base(app, ren, t.panel);
    // Title `Vc` (`characterSensei` -> "СЭНСЭЙ").
    draw_ui_label(app, t.panel.px + t.panel.pw * 0.5f - 780.0f * t.panel.c, t.title_y,
                  1560.0f * t.panel.c, t.title_h, loc(app, "characterSensei", "SENSEI"),
                  0.98f, UiAlign::Center, 0.404f, 0.243f, 0.141f);
    // Portrait (sensei, 256px, transparent corners).
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
        s.transform.set_pos(t.portrait_cx, t.portrait_cy);
        s.transform.set_scale(t.portrait / 256.0f, t.portrait / 256.0f);
        app.renderer().draw_sprite(s, ui_camera());
    }
    draw_ui_wrapped(app, t.body_x, t.body_y, t.body_w, t.body_h,
                    loc(app, "tutorial_training_fight",
                        "Impressive... but a bag cannot defend itself."),
                    0.70f, UiAlign::Left, 0.12f, 0.09f, 0.06f);
    // FIGHT button (`dlgStoryBtnFight` -> "В БОЙ").
    if (!(load_sliced_atlas(app) &&
          draw_bb_plate(app, "btnBeige", t.btn_cx, t.btn_cy, t.btn_w, t.btn_h, 1.0f))) {
        draw_flat_button(app, "", t.btn_cx, t.btn_cy, t.btn_w, t.btn_h, 0.6f, 0.5f, 0.3f, false);
    }
    draw_ui_label(app, t.btn_cx - t.btn_w * 0.5f, t.btn_cy - 14.0f, t.btn_w, 28.0f,
                  loc(app, "dlgStoryBtnFight", "FIGHT"), 0.9f, UiAlign::Center, 1.0f, 1.0f,
                  1.0f);
}

void DojoScreen::update_tutorial() {
    if (!app().pointer().pressed) return;
    const double x = app().pointer().x;
    const double y = app().pointer().y;
    if (tut_beat_ <= 1) {
        if (tutorial_banner_hit(x, y)) {
            ++tut_beat_;
            std::fprintf(stdout, "[tutorial] beat -> %d\n", tut_beat_);
            std::fflush(stdout);
        }
        return;
    }
    const TutorialDialogLayout t = tutorial_dialog_layout();
    if (x >= t.btn_cx - t.btn_w * 0.5f && x <= t.btn_cx + t.btn_w * 0.5f &&
        y >= t.btn_cy - t.btn_h * 0.5f && y <= t.btn_cy + t.btn_h * 0.5f) {
        start_tutorial_fight();
    }
}

void DojoScreen::start_tutorial_fight() {
    // `Fight Name="Punchbag|Bosses|1"` (tutorial_quests.xml L40): the stages.xml
    // `Zone Name="Punchbag"` / `Battle Name="Training"` (X=158 Y=145,
    // Location="dojo") — the dojo training dummy.
    PendingBattle& pb = app().pending_battle();
    pb.battle_name = "Training";
    pb.zone = "Punchbag";
    pb.location = "dojo";
    pb.enemy_name = "Punchbag";
    pb.has_result = false;
    pb.player_won = false;
    pb.reward_money = 0;
    pb.reward_exp = 0;
    pb.prize_base_coins = 0;
    pb.prize_bonus = 0;
    pb.prize_gems = 0;
    pb.prize_combo = 0;
    pb.prize_shocks = 0;
    pb.prize_perfect = false;
    pb.prize_first = false;
    // The owned items feed the FightScreen's move list (`ra.Hza`; the map's
    // `launch_battle` does the same). Without them the auto-attack has no
    // attackable move and the training fight never ends.
    pb.owned = owned_items(app());
    // The beats are done the moment the fight starts: the Dojo reactivates
    // clean when the fight pops (the hub after the tutorial), and the training
    // fight's own round never resolves (the `Training` <Rules> carry no round
    // end; core/scene is out of scope), so completion is not tied to the KO.
    tut_done_ = true;
    std::fprintf(stdout, "[tutorial] FIGHT -> Punchbag|Bosses|1 (Training, dojo)\n");
    std::fflush(stdout);
    push(kScreenFight);
}

void DojoScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    ensure_dojo_location(app);
    // Fresh-profile tutorial is showing (see draw_tutorial): the hub draws
    // underneath, the dialog on top; the ambient hint is suppressed.
    const bool tut = app.fresh_tutorial() && !tut_done_;
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
            const float half = assets.dojo.arena_width() * 0.5f;
            const float player_x = assets.dojo.player_spawn_x() - half;
            const float enemy_x = assets.dojo.enemy_spawn_x() - half;
            focus_x = (player_x + enemy_x) * 0.5f + half;
            fighter_span = std::fabs(enemy_x - player_x);
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
        // The hub's enemy = the Punchbag dummy (`merged_bag`). JS runs the
        // `FightNone` Punchbag Training viewer on the hub (`Tf.init` L1971),
        // so its enemy is the dummy at the ModelsViewer enemy spawn
        // (973,-110; `Bf.zjb` L476 `B_ = EnemyPosition`). The Warrior is
        // `NotAnimation=1` (JS_FLOW.md:66), so the dummy HOLDS its bind pose
        // (no clip needed — a 1-frame empty clip keeps every bone at bind).
        // `ev.Gf` L845 draws the enemy FIRST (z=-.001), behind the player.
        if (!dojo_bag_tried_) {
            dojo_bag_tried_ = true;
            if (app.has_fight_assets()) {
                FightAssets& assets = app.fight_assets();
                if (!assets.merged_bag.bones.empty()) {
                    dojo_bag_ = std::make_unique<sf2::scene::Fighter>();
                    dojo_bag_->set_model(assets.merged_bag);
                    dojo_bag_->set_color(assets.dojo.root_color());
                    dojo_bag_ok_ = true;
                    std::fprintf(stdout,
                                 "[dojo] punchbag dummy ready (bones %zu, bind pose)\n",
                                 assets.merged_bag.bones.size());
                    std::fflush(stdout);
                }
            }
        }
        if (dojo_bag_ok_ && dojo_bag_ != nullptr && have_hub_cam) {
            const float enemy_x =
                (app.has_fight_assets() ? app.fight_assets().dojo.enemy_spawn_x() : 973.0f) -
                arena_half;
            const float enemy_y =
                (app.has_fight_assets() ? app.fight_assets().dojo.enemy_spawn_y() : -110.0f) +
                cont_y;
            sf2::data::anim_clip bind_clip;  // NotAnimation=1: bind pose (empty frame)
            bind_clip.frames.resize(1);
            dojo_bag_->sample(bind_clip, 0, enemy_x, enemy_y, 1);
            draw_dojo_figure(ren, hub_cam, *dojo_bag_);
        }
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
        // The ambient `Ib` hint is suppressed in fresh-tutorial mode: the
        // beats are the tutorial (above) and the post-tutorial hub is clean
        // (the oracle dojo_hub has no banner).
        if (!modal_up && !app.fresh_tutorial()) {
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
    if (tut) {
        // Fresh-profile tutorial (blocking): the Sensei notification beats /
        // the Regular training-fight dialog (draw_tutorial).
        draw_tutorial(app, ren);
        return;
    }
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
        }
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
                if (focus_nodes[i].visible &&
                    focus.find(focus_nodes[i].name) != std::string::npos) {
                    hover_ = static_cast<int>(i);
                    break;
                }
            }
        }
        for (std::size_t i = 0; hover_ < 0 && i < focus_nodes.size(); ++i) {
            if (focus_nodes[i].visible && focus_nodes[i].type == "BOSSES") {
                hover_ = static_cast<int>(i);
                break;
            }
        }
        if (hover_ < 0 && !focus_nodes.empty()) {
            for (std::size_t i = 0; i < focus_nodes.size(); ++i) {
                if (focus_nodes[i].visible) {
                    hover_ = static_cast<int>(i);
                    break;
                }
            }
        }
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

void MapScreen::update_impl(float dt) {
    (void)dt;
    // Fidelity-tour boss-roster capture: freeze the map (no taps/launches)
    // while the roster overlay is forced (render_impl draws it).
    if (force_boss_roster()) return;
    ensure_lang(app());  // the lang table powers the `Y.na` string lookups
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
    // JS `Ya.Uw` (L2129) focuses the save MapFocus node at init and `Rr`
    // tracks it (`ue.tea()`); a tap re-targets (`qe.jhb` -> `GT`). The focus
    // persists between taps, so it is NOT reset here — `hover_` holds it.
    for (std::size_t i = 0; i < zones_[zone_sel_].nodes.size(); ++i) {
        const Node& n = zones_[zone_sel_].nodes[i];
        if (!n.visible) continue;  // JS `Qr.lla` L2094 (hidden alt-state twin)
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
    // The Map forces the collapsed header (fresh `za` per screen).
    za_update(app(), *this, kScreenMap, /*force_collapsed=*/true);
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
            try_draw_atlas_button(app, "indicatorOff", px + pip * 0.5f, py + pip * 0.5f,
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
    // (`Bb` ctor `xc(600)`), so on screen `600*c*.2/112` x `c*.2`; centred on
    // the body, bottom at `a.W`.
    const float btn_h = cw * 0.2f;
    const float btn_w = 600.0f * (btn_h / 112.0f);
    const float btn_cx = body_x + body_w * 0.5f;
    const float btn_cy = body_y + body_h - btn_h * 0.5f;
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
}

// ---------------------------------------------------------------------------
// Boss-intro roster (`jk`, JS L2061-2065) + the fidelity-tour capture hook.
// ---------------------------------------------------------------------------
namespace {
bool g_force_boss_roster = false;
}  // namespace

bool force_boss_roster() { return g_force_boss_roster; }
void set_force_boss_roster(bool on) { g_force_boss_roster = on; }

// `jk` (L2061-2065): a horizontal row of circular warrior portraits on the
// `hf = Fc.Ed(-16777216)` fader + `Qa = R.$(E.get(3,6))` panel. `jk.aa`
// state 3 scales the SELECTED entry to 1.4 and dims the rest to 0.5
// (`Hf.node.wa(1+-.5*a)`), with the row scrolled so the selected is centred
// (`Pp`, L2063). The oracle capture (`act_boss`) pins: selected ring ~325 px
// centred, the others ~180 px at +-265 px. The portrait art carries its own
// circular frame, so it is drawn at its natural 512 px canvas.
void draw_boss_roster(App& app, sf2::render::Renderer& ren,
                      const std::vector<BossRosterEntry>& roster, int index) {
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
    if (roster.empty()) return;
    if (index < 0 || index >= static_cast<int>(roster.size())) index = 0;
    // Geometry pinned to the oracle capture (`act_boss`): selected disc
    // d~271 (canvas 504), others d~213 (canvas 353), row pitch 282.4 =
    // `d += g*.8` with g = the 353 canvas (L2062), vertical centre 362.
    constexpr float kStep = 282.4f;   // `d += g*.8` row pitch (L2062)
    constexpr float kSelD = 504.0f;   // selected canvas (1.4x) -> ~295 px ring
    constexpr float kOtherD = 353.0f; // others canvas -> ~207 px ring
    constexpr float kCy = 362.0f;
    for (int i = 0; i < static_cast<int>(roster.size()); ++i) {
        const float cx = kViewW * 0.5f + static_cast<float>(i - index) * kStep;
        const float d = (i == index) ? kSelD : kOtherD;
        const float a = (i == index) ? 1.0f : 0.5f;
        if (!draw_user_image(app, roster[i].image, cx, kCy, d, d, a)) {
            draw_user_image(app, "avatar_masked", cx, kCy, d, d, a);
        }
    }
}

void MapScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // Fidelity-tour boss-roster capture (`--fidelity-tour`): draw the
    // boss-intro roster (`jk`) instead of the map while the hook is on.
    if (force_boss_roster()) {
        ensure_lang(app);
        std::vector<BossRosterEntry> roster;
        std::string boss_battle, boss_zone;
        if (zone_sel_ >= 0 && static_cast<std::size_t>(zone_sel_) < zones_.size()) {
            for (const Node& n : zones_[zone_sel_].nodes) {
                if (n.type == "BOSSES" || n.type == "BOSSES_REPLAYABLE") {
                    boss_battle = n.name;
                    boss_zone = n.zone;
                    break;
                }
            }
        }
        // `jk.init(lD)` iterates the boss battle's `<Fight>` list (BOSS_LYNX
        // has Fight 1/2/3 -> three roster portraits, the oracle capture).
        for (int i = 0; i < 3 && !boss_battle.empty(); ++i) {
            const BattleWarriorInfo bw = battle_warrior(boss_battle, boss_zone, i);
            if (bw.attrs.empty()) break;  // no Nth <Fight>
            BossRosterEntry e;
            e.name = bw.first_name.empty() ? boss_battle : loc(app, bw.first_name, bw.first_name);
            const auto av = bw.attrs.find("Avatar");
            std::string img = (av != bw.attrs.end() && !av->second.empty()) ? av->second
                                                                            : "avatar_masked";
            std::transform(img.begin(), img.end(), img.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            e.image = img;
            roster.push_back(e);
        }
        draw_boss_roster(app, ren, roster, 0);
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
        const bool locked = !n.active;
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
        float dot_x = 547.0f;
        const float dot_d = 34.0f;
        const float dot_cy = mm.bar_y + mm.bar_h * 0.5f;
        for (std::size_t zi = 0; zi < zones_.size(); ++zi) {
            bool any = false;
            for (const Node& n : zones_[zi].nodes) {
                if (n.visible) {
                    any = true;
                    break;
                }
            }
            if (!any) continue;
            const bool sel = static_cast<int>(zi) == zone_sel_;
            if (!try_draw_atlas_button(app, sel ? "bulb" : "inactive_bulb", dot_x, dot_cy,
                                       dot_d, dot_d, 1.0f)) {
                draw_flat_button(app, "", dot_x, dot_cy, dot_d * 0.5f, dot_d * 0.5f, 0.8f,
                                 0.45f, 0.15f, false);
            }
            dot_x += 60.0f;
        }
    }
    // Shared `za` chrome (JS `ma.D1`): topPanel + widgets, or the collapsed
    // `МЕНЮ` header (JS `za.Aub` collapse(0) — the map's default).
    draw_za_chrome(app, kScreenMap, nullptr, /*force_collapsed=*/true);
    // The `Rr` info panel (JS `Ya.Zq = Qo(Rr)` L2125) draws last, over the
    // `Vr` strip and the `Ur` bar (the oracle panel overlaps both).
    const Node* sel = nullptr;
    if (zone_ok && hover_ >= 0 &&
        static_cast<std::size_t>(hover_) < zones_[zone_sel_].nodes.size()) {
        sel = &zones_[zone_sel_].nodes[static_cast<std::size_t>(hover_)];
    }
    draw_map_info_panel(app, sel, mm);

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
    battle.type = "FightNone";
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
    // [FIX Phase 1 step 9 — spawn sides] Source BOTH spawns from the
    // location's ModelsViewer (JS `Bf.zjb` L476 parses PlayerPositionX/Y ->
    // `location.Yia`, EnemyPositionX/Y -> `location.B_`; JS L381 spawns the
    // player `kc` at `Yia` and the enemy `Zb` at `B_`). Dojo: player
    // (690,-93), enemy (973,-110) (dojo_params.b78df4b4.xml ModelsViewer). The old hard-coded 973/690 pair trusted
    // the oracle dump's swapped `id` labels (the fighter at x=973 is the
    // 15-bone Punchbag = the enemy) and put the player on the enemy's mark.
    // Falls back to the dojo defaults when a location has no <ModelsViewer>.
    if (assets.fight_location.has_spawns()) {
        battle.player_spawn_x = assets.fight_location.player_spawn_x();
        battle.player_spawn_y = assets.fight_location.player_spawn_y();
        battle.enemy_spawn_x = assets.fight_location.enemy_spawn_x();
        battle.enemy_spawn_y = assets.fight_location.enemy_spawn_y();
    }
    battle.max_hp = 1;  // the game's HP fallback (Zn = aB>0 ? aB : 1)
    // JS `wd.Fm` L811 — the player's resolved UnarmedDamage (see the helper).
    battle.player_unarmed_damage = resolve_player_unarmed_damage(app());
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
    }
    const std::string& enemy_name = app().pending_battle().enemy_name;
    fight_->init_locks(battle, assets.merged, assets.moves, assets.clips,
                       assets.tactics_sets, tactic, "Player", enemy_name,
                       battle.player_spawn_x, battle.player_spawn_y,
                       battle.enemy_spawn_x, battle.enemy_spawn_y,
                       battle.max_hp, battle.max_hp, {},
                       owned, equipped_perks(app(), assets), nullptr,
                       player_model, enemy_model);
    fight_->set_seed(fight_seed);  // JS `Da.pg=new Rk(L.seed)` (L67)
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
int FightScreen::key_type_for_glfw(int glfw_key) {
    switch (glfw_key) {
        case 65: case 263: return static_cast<int>(sf2::scene::key_type::back);         // A / Left
        case 68: case 262: return static_cast<int>(sf2::scene::key_type::forward);      // D / Right
        case 87: case 265: return static_cast<int>(sf2::scene::key_type::up);           // W / Up
        case 83: case 264: return static_cast<int>(sf2::scene::key_type::down);         // S / Down
        case 32: case 75: return static_cast<int>(sf2::scene::key_type::punch);         // Space / K
        case 76: return static_cast<int>(sf2::scene::key_type::kick);                   // L
        case 79: return static_cast<int>(sf2::scene::key_type::ranged);                 // O
        case 80: return static_cast<int>(sf2::scene::key_type::magic);                  // P
        case 74: return static_cast<int>(sf2::scene::key_type::raid_charge);            // J
        case 81: return static_cast<int>(sf2::scene::key_type::super);                  // Q
        default: return 0;
    }
}

void FightScreen::on_key(int glfw_key, bool down) {
    // Pause toggle (JS `Jn` pause button → `Ar.Qg(0)` → `Aia()` L425 — the
    // native Escape desktop alias, app-layer only). Esc (256) on the down
    // edge toggles while the round is live. P (80) is NO LONGER a pause key:
    // JS binds P to Magic (`Af.oUa` L2472 `a.v[12]=80`), so P must reach the
    // fight key map below. Headless-safe: the headless driver injects pointer
    // clicks, never keys, so this cannot trigger there (no code gate needed).
    if (down && glfw_key == 256) {
        if (fight_ != nullptr && !fight_->round_wait() && !fight_->battle_over()) {
            paused_ = !paused_;
            sf2::audio::AudioEngine::instance().play("click");
            std::fprintf(stdout, "[fight] pause %s (Esc)\n", paused_ ? "ON" : "OFF");
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
    // GLFW key codes -> the game's key_type, bound from the JS key map
    // `sc.OD` (`Af.oUa` L2472):
    //   a.v[1]=87 (W->Up), a.v[3]=68 (D->Forward), a.v[5]=83 (S->Down),
    //   a.v[7]=65 (A->Back), a.v[9]=75 (K->Punch), a.v[10]=76 (L->Kick),
    //   a.v[11]=79 (O->Ranged), a.v[12]=80 (P->Magic), a.v[13]=74
    //   (J->RaidCharge), a.v[14]=81 (Q->Super).
    // The JS table defines no B key (the old non-JS B->Super alias is
    // dropped). Space and the arrows stay as documented desktop aliases
    // (Left/Right/Up/Down directions; Space = K/Punch). Blocking is NOT a raw
    // key in this game: the fighter blocks while any move's `Block` interval
    // is active (e.g. the HighPunch recovery).
    const int kt_id = key_type_for_glfw(glfw_key);
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

std::size_t FightScreen::move_list_size() const {
    return fight_ != nullptr ? fight_->player().fighter.hb().size() : 0;
}

std::string FightScreen::player_last_decision() const {
    return fight_ != nullptr ? fight_->player().last_decision : std::string();
}

int FightScreen::player_moves_started() const {
    return fight_ != nullptr ? fight_->player().moves_started : 0;
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
    // VS intro (`ik`, L2069): presentation-only pre-fight screen. The sim is
    // NOT frozen — JS creates the fight only after `ik.kg`, but the native
    // controller already exists, and running it underneath keeps the existing
    // deterministic step counts (ui-tour / headless-loop) intact while the
    // overlay covers it. The overlay auto-advances on its own timer.
    if (vs_active_) {
        vs_t_ += dt;
        if (vs_t_ >= kVsTotal) vs_active_ = false;
    }
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
                sf2::audio::AudioEngine::instance().play("click");
                std::fprintf(stdout, "[fight] pause OFF (resume, Dr.play)\n");
                std::fflush(stdout);
            } else if (pause_hit(kPauseDlgMusicX, kPauseDlgRowY, kPauseDlgToggleS,
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
            } else if (pause_hit(kPauseDlgSoundX, kPauseDlgRowY, kPauseDlgToggleS,
                                 kPauseDlgToggleS)) {
                // `PauseSound_on/off` (display only — no runtime SFX mute API;
                // see the stream report).
                sf2::audio::AudioEngine::instance().play("click");
                std::fprintf(stdout,
                             "[fight] pause sound toggle (Dr.PauseSound, display-only)\n");
                std::fflush(stdout);
            } else if (pause_hit(kPauseDlgHomeX, kPauseDlgRowY, kPauseDlgToggleS,
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
                pos[u1 + 1] + assets.fight_location.arena_height() * 0.5f -
                assets.fight_location.arena_floor());
            const float sx2 = camera.world_to_screen_x(pos[u2] - arena_half, 1.0f);
            const float sy2 = camera.world_to_screen_y(
                pos[u2 + 1] + assets.fight_location.arena_height() * 0.5f -
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
        const float dim[] = {0, 0,         kViewW, 0,         kViewW, kViewH,
                             0, 0,         kViewW, kViewH,    0,      kViewH};
        // `Dr.Qa` (L2065): `R.$(E.Zxa(900))` `wa(0)` `wh(6,1,.25)` full-screen
        // -> a ~0.25-alpha dim (was an invented 0.65).
        ren.draw_triangles(dim, 6, 0.0f, 0.0f, 0.0f, 0.35f);
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
    ensure_lang(app());  // the lang table powers the `Y.na` string lookups
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

// JS `kk.Qa = R.$(E.Zxa(750))` (L2057): `E.Zxa` (L93) builds a 750x4 canvas
// HORIZONTAL gradient, filled by `E.Eua` (L94) with stops
// `#00000020 @0 / #00000080 @.25/.5/.75 / #00000020 @1` (black, alpha
// 32/128/128/128/32). `kk.layout` (L2059) stretches it over `ma.Kq`
// (`Rh/mj`), i.e. the whole fight viewport — the "panel" is a full-viewport
// alpha gradient, not a flat rect. `draw_triangles` is flat-color, so the
// gradient is sampled into N vertical strips.
void draw_kk_gradient(sf2::render::Renderer& ren, float x, float y, float w, float h) {
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
        ren.draw_triangles(v, 6, 0.0f, 0.0f, 0.0f, a);
    }
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
        int value;
        bool mult;
        bool star;
    };
    const KkRow kk_rows[7] = {
        {"goldPrize", "PRIZE", money_reward_, false, false},
        {"goldPerfect", "PERFECT", prize_perfect_ ? 1 : 0, true, false},
        {"goldFirstStrike", "FIRST STRIKE", prize_first_ ? 1 : 0, true, false},
        {"goldCombo", "MAX COMBO", prize_combo_, true, false},
        {"goldShock", "SHOCK", prize_shocks_, true, false},
        {"goldPassiveStyle", "PASSIVE STYLE", 0, false, false},
        {"", "", 0, false, true},  // star row (`v1a`, L2078)
    };
    constexpr float kKkRowX = 420.0f;    // label left edge
    constexpr float kKkCoinX = 822.0f;   // gold coin centre
    constexpr float kKkValX = 838.0f;    // value left edge (cyan)
    constexpr float kKkRowY0 = 155.0f;
    constexpr float kKkRowStep = 58.0f;
    for (int i = 0; i < 7; ++i) {
        const KkRow& r = kk_rows[i];
        const float ry = kKkRowY0 + static_cast<float>(i) * kKkRowStep;
        if (r.star) {
            (void)try_draw_atlas_button(app, "star", kKkRowX + 26.0f, ry, 52.0f, 48.0f,
                                        1.0f);
        } else {
            std::string lab = loc(app, r.key, r.fallback);
            if (r.mult) lab += " x" + std::to_string(r.value);
            draw_ui_label(app, kKkRowX, ry - 16.0f, 400.0f, 32.0f, lab, 0.95f,
                          UiAlign::Left, 0.94f, 0.89f, 0.72f);
            (void)try_draw_atlas_button(app, "gold", kKkCoinX, ry, 48.0f, 48.0f, 1.0f);
        }
        draw_ui_label(app, kKkValX, ry - 16.0f, 60.0f, 32.0f, std::to_string(r.value),
                      0.95f, UiAlign::Left, 0.31f, 0.79f, 0.84f);
    }
    if (!quest_toast_.empty()) {
        draw_ui_label(app, kViewW * 0.5f - 300.0f, kViewH - 96.0f, 600.0f, 28.0f,
                      quest_toast_, 0.9f, UiAlign::Center, 1.0f, 0.9f, 0.4f);
    }
    // OK button (JS `Lr.$g = new Bb("EButtonWhite"); $g.V(Y.na("OK"))`, L2075)
    // bottom-centre in the beige hexagon fleet (`EButtonBeige`). Flat is the
    // genuine atlas-miss fallback.
    {
        const float okx = kViewW * 0.5f;
        const float oky = 645.0f;
        if (!try_draw_atlas_button(app, "EButtonBeige", okx, oky, 230.0f, 52.0f, 1.0f)) {
            draw_flat_button(app, "OK", okx, oky, 210.0f, 48.0f, 0.85f, 0.78f, 0.55f,
                             false);
        }
        draw_ui_label(app, okx - 105.0f, oky - 13.0f, 210.0f, 26.0f, "OK", 0.85f,
                      UiAlign::Center, 0.20f, 0.15f, 0.08f);
    }
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
    if (shop_equipped(w, it)) return loc(app, "btnShopUnequip", "UNEQUIP");
    if (shop_owned_live(w, it.name)) return loc(app, "btnShopEquip", "EQUIP");
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
}

void ShopScreen::update_impl(float dt) {
    (void)dt;
    ensure_lang(app());  // the lang table powers the `Y.na` string lookups
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
                    sel_ = 0;  // Oa.f5 -> usb() auto-selects the first cell
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
            confirm_ = "CLAIMED " + loc(app(), kv.first, kv.first) + "!";
            confirm_until_ = time() + 2.5f;
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
            sf2::audio::AudioEngine::instance().play("click");
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
                    confirm_ = (was_equipped ? "UNEQUIPPED " : "EQUIPPED ") +
                               item_display_name(app(), it) + "!";
                    confirm_until_ = time() + 2.5f;
                    std::fprintf(stdout, "[shop] %s %s -> %s slot %s\n",
                                 was_equipped ? "Qxb UNEQUIP" : "$o EQUIP", it.name.c_str(),
                                 it.type.c_str(), new_slot.c_str());
                    std::fflush(stdout);
                } else if (w.money >= it.price) {
                    // `Fhb` else-branch `Ex(a,7)` -> buy dialog -> `Pa.iwa`
                    // (SHOP_STATIC §6): `Tb >= jp` -> deduct + grant + save.
                    // The `ph` dialog is OPEN; native collapses it to the
                    // gate+grant.
                    w.money -= it.price;
                    if (it.delivery_sec > 0) {
                        // Pa z2a timed delivery: paid upfront, arrives on
                        // claim (Gb Cla(now) stamped).
                        w.timers[it.name] = WarriorSave::wall_now() + it.delivery_sec;
                        app().save().save(w);
                        seen_ = w;
                        confirm_ = "ORDERED " + item_display_name(app(), it) + "!";
                        confirm_until_ = time() + 2.5f;
                        std::fprintf(stdout,
                                     "[shop] ORDERED %s price=%d -> arrives in %ds\n",
                                     it.name.c_str(), it.price, it.delivery_sec);
                        std::fflush(stdout);
                    } else {
                        WarriorSave::OwnedItem oi;
                        oi.name = it.name;
                        oi.count = 1;
                        // Tutorial-buy force-equip (JS `Ao` Qg L1120:
                        // `Pa.iwa(b) && xa.$o(b)`); step -> MAP.
                        const bool tut_buy =
                            it.name == "WEAPON_KNIVES" &&
                            (w.story_step() == "STEP_BUY_ITEM" ||
                             (w.story_step().empty() && w.tutorial == "MOVE"));
                        if (tut_buy) {
                            shop_apply_slot(w, it.type, it.name);
                            oi.equipped = true;
                            w.set_story_step("MAP");
                        }
                        w.items.push_back(oi);
                        app().save().save(w);
                        seen_ = w;
                        confirm_ = "BOUGHT " + item_display_name(app(), it) + "!";
                        confirm_until_ = time() + 2.5f;
                        std::fprintf(stdout,
                                     "[shop] BOUGHT %s (%s) price=%d -> money %d%s\n",
                                     it.name.c_str(), it.subtype.c_str(), it.price, w.money,
                                     tut_buy ? " + EQUIPPED, step -> MAP (Ao)" : "");
                        std::fflush(stdout);
                    }
                } else {
                    std::fprintf(stdout, "[shop] NOT ENOUGH MONEY for %s (need %d, have %d)\n",
                                 it.name.c_str(), it.price, w.money);
                    std::fflush(stdout);
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
    // capture. `force_collapsed` = the Map/Profile precedent
    // (screens.cpp:4623 / 8731), both already oracle-matched.
    // Display-only: `g_za_nav_open` is left intact so the Dojo keeps its column.
    za_update(app(), *this, kScreenShop, /*force_collapsed=*/true);
}

void ShopScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // --- Backdrop: the destination `dojo_shop` art (`Pi.Qa`, L439) ---------
    // JS `Oa extends ma` (L2285): `this.Ad = new Pi` (L2291) and `Ea` calls
    // `this.Tya(this.Ad)` (L2293). `Pi` renders `Qa = R.$(E.get(752))` =
    // `locations/dojo_shop/bg.{image}` under `ma.Tya` (L1832) — a dedicated
    // destination background, NOT the dojo location layers.
    draw_destination_backdrop(app);
    draw_destination_model(app, ren, backdrop_fighter_, backdrop_fig_tried_,
                           backdrop_fig_ok_, backdrop_idle_);
    draw_destination_dim(ren);

    // Bottom tab strip (JS `ss`/`Eg` L1851-1853, L2283-2284): a full-width
    // bar + `Le` buttons (id 248 shop atlas `buttons/<Category>[_active]`),
    // scaled to the bar height; flat fallback only on a real frame miss.
    {
        const ShopTabLayout tl = shop_tab_layout();
        const float bar[] = {0, kViewH - tl.bar_h, kViewW, kViewH - tl.bar_h, kViewW, kViewH,
                             0, kViewH - tl.bar_h, kViewW, kViewH, 0, kViewH};
        ren.draw_triangles(bar, 6, kTabBarBgR, kTabBarBgG, kTabBarBgB, 1.0f);
        for (int t = 0; t < kShopTabCount; ++t) {
            const float cx = tl.cx0 + static_cast<float>(t) * tl.step;
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
        // `lH` = the `ms` attribute list (`lH.ba(a, a*.22)`, L2248): the primary
        // combat stat icon + value + a parameter bar (`shop.json attributes/*`).
        const char* sicon = "attributes/weapon_attack";
        int sval = sel_it->weapon_damage;
        if (sel_it->type == "Armor") {
            sicon = "attributes/body_armor";
            sval = sel_it->body_defense;
        } else if (sel_it->type == "Helm") {
            sicon = "attributes/head_armor";
            sval = sel_it->head_defense;
        } else if (sel_it->type == "Ranged") {
            sicon = "attributes/ranged_attack";
        } else if (sel_it->type == "Magic") {
            sicon = "attributes/magic_attack";
        }
        const float stat_h = cw0 * 0.22f;
        const float stat_y = cy0 + ch0 * 0.1f + tfont * 1.3f;
        try_draw_atlas_button(app, sicon, cx0 + 24.0f, stat_y + stat_h * 0.5f, 44.0f, 44.0f,
                              1.0f, false, false);
        draw_ui_label(app, cx0 + 50.0f, stat_y + stat_h * 0.5f - 15.0f, 56.0f, 30.0f,
                      std::to_string(sval), 0.9f, UiAlign::Left, 0.20f, 0.12f, 0.06f);
        // `parametersBar/bar_N` is the value bar behind the number (shop atlas).
        {
            const float bxx = cx0 + 110.0f;
            const float bww = cw0 - 110.0f;
            const float bh2 = 16.0f;
            const ShopRect track{bxx, stat_y + stat_h * 0.5f - bh2 * 0.5f, bxx + bww,
                                 stat_y + stat_h * 0.5f + bh2 * 0.5f};
            quad(track, 0.35f, 0.24f, 0.14f, 0.6f);
            const ShopRect fill{bxx, track.P, bxx + bww * 0.7f, track.W};
            quad(fill, 0.95f, 0.62f, 0.20f, 1.0f);
        }
        // Bottom price button `M8` = `GoldButton` (`EButtonGreen` + the
        // `p.o.Vf` gold icon), `Ne.Wub` L2254-2255 -> `c5(M8, Aa.jp())`. The
        // buttons stack up from `d=b-c*3`, each `e.kf(a)` (full content width)
        // (`Ne.ba` L2249).
        const float bpad = cw0 * 0.05f;
        const float bh = 112.0f * pscale;
        const float byy = cy0 + ch0 - bpad * 3.0f - bh * 0.5f;
        if (!(load_sliced_atlas(app) &&
              draw_bb_plate(app, "btnGreen", cx0 + cw0 * 0.5f, byy, cw0, bh, 1.0f))) {
            draw_flat_button(app, "", cx0 + cw0 * 0.5f, byy, cw0, bh, 0.30f, 0.62f, 0.30f, false);
        }
        try_draw_atlas_button(app, "gold", cx0 + 30.0f, byy, 40.0f, 40.0f, 1.0f, false, false);
        draw_ui_label(app, cx0 + 56.0f, byy - 15.0f, cw0 - 56.0f, 30.0f,
                      std::to_string(sel_it->price), 0.9f, UiAlign::Left, 0.15f, 0.10f, 0.05f);
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
    // Buy confirmation (display only; the wielding summary now lives in the
    // `MJ` left side panel).
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
    if (!confirm_.empty() && time() <= confirm_until_) {
        draw_ui_label(app, kViewW * 0.5f - 220.0f, 678.0f, 440.0f, 26.0f,
                          confirm_, 1.0f, UiAlign::Center, 0.4f, 1.0f, 0.4f);
    }
    // Shared `za` chrome (JS `ma.D1`): topPanel + widgets + vertical nav.
    // `D1` (L1831) re-appends a fresh, collapsed `za` -> the oracle shop shows
    // the collapsed header and NO `gk.background` 0.5-black dim (measured: the
    // oracle shop right-wall backdrop is ~0.7x the hub, not 0.5x). Same
    // force-collapsed draw as the Map/Profile (L5078/L9345).
    draw_za_chrome(app, kScreenShop, nullptr, /*force_collapsed=*/true);
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
// (SEALS_SLIDER L2231). Tabs 1 (folded Moves) and 3 (`gs` SEALS) are
// reproduced; tabs 0 (`ds`, needs `id.ht().Mi/tH`) and 2 (`fs`, needs
// `v.uv.tI`) are OPEN — the native docks the real `vb.layout` `a =
// b.fn(.75)` viewer rect (L2195) and shows a cited placeholder. Nav/`cs` badges
// (`Dg`, L1850-1851):
// `cs.getCounterValue` (L2189) reads `p.o.co.uCa()/p.o.sCa()/p.o.yi.rCa()/
// p.o.vCa()` and `ss` (L2284) `p.items.T5a(Cj.zxb(a))` — the badge COUNTS are
// not derivable from the native save (OPEN); the `Dg.ba(65)`/`Ia(128)`
// geometry is ported in `draw_za_chrome`'s badge path.
// ---------------------------------------------------------------------------
constexpr int kProfileTabCount = 4;
constexpr int kProfileTabLeveling = 0;  // `ds` leveling tab (`Rl=ds` L2227) — body OPEN
constexpr int kProfileTabMoves = 1;  // folded Moves sub-view (JS `qv`, To.kOa=11 L2201)
constexpr int kProfileTabAchiev = 2;  // `fs` ACHIEVEMENT_SLIDER (L2213) — body OPEN
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
};

ProfileTabLayout profile_tab_layout() {
    const ZaLayout z = za_layout();
    ProfileTabLayout t;
    t.bar_h = z.sp * kTabBarHeightK;                 // Eg.aa: za.Sp*1.5 (un)
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

// `fs.NC` (L2216) sizes every achievement cell `ba(400,130)`; the native
// lays one per row (`row_h`). Shared by render + hit-test.
constexpr float kAchievRowH = 46.0f;
ShopRect profile_achiev_row_rect(const ShopRect& v, int i) {
    ShopRect r;
    r.J = v.J + 4.0f;
    r.N = v.J + v.width() - 4.0f;
    r.P = v.P + 6.0f + static_cast<float>(i) * kAchievRowH;
    r.W = r.P + kAchievRowH;
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
void draw_profile_tabs(App& app, int tab, int hover) {
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
            continue;
        }
        draw_flat_button(app, art.label, cx, t.cy, t.btn_w, t.btn_h,
                         sel ? 0.55f : (hov ? 0.45f : 0.32f), 0.4f, 0.28f, hov);
        draw_ui_label(app, cx - t.btn_w * 0.5f, t.cy - 10.0f, t.btn_w, 20.0f,
                      art.label, 0.6f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
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
                if (pl.name == r.name) r.learned_level = std::max(r.learned_level, pl.level);
            }
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
            if (count > 0) seal_rows_.push_back({ci.name, count, ci.image});
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
        // JS `es.uZ` (L2239): `this.Ul=v.uQ(9)` — the MOVES list is the CURRENT
        // WEAPON's move set (`v.cw()` -> `ra.e9a`/`ra.Z6a` L684-686, which test
        // each weapon move-set's Locks against the weapon's items), NOT the
        // fight's item-lock rule. `build_move_list_locks(..., true)` kept every
        // no-lock move (moves.xml has 242 of 873) -> the port listed the whole
        // catalog; the oracle shows the wielded weapon's few. Build the
        // weapon-scoped list (the same primitive the fight uses per weapon,
        // fight.cpp:362) from the equipped weapon's SubType.
        std::string wsub;
        try {
            for (const CatalogItem& ci : load_full_catalog(app())) {
                if (ci.name == weapon_ && ci.type == "Weapon") {
                    wsub = ci.subtype;
                    break;
                }
            }
        } catch (const std::exception&) {
        }
        if (wsub.empty()) wsub = "Fists";
        fig.build_move_list(assets.moves, wsub, /*include_universal=*/false);
        if (fig.hb().empty()) {
            // Defensive guard: never regress to an empty tab if the SubType
            // resolves to no `TacticWeapon` row (the exact `v.uQ(9)` weapon-set
            // join still needs a runtime trace — PORT_AUDIT_UI §5 OPEN).
            fig.build_move_list_locks(assets.moves, owned_items(app()), true);
        }
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

// JS `Zr.ROa` (L2222) improve-button gate: `Lc.Be!=3 && Lc.Be!=2 && Lc.Be!=1
// && !zo && vb.uwa()`. `Be==0` is the learnable cell (`id.Txb` L1356 sets
// `Bla(0)` on the first tier's items; `mXa` L1355 pushes the owned `Be==3`
// cells); `zo` = `uk.k5(p.o.bb()<a.level)` (L2223) = the player level gate.
// Native mapping: an unlearned "Perk" row (type 1) above the player level is
// not buyable; a "Perk" row at/below level is (`Be==0`). An "Upgrade" row
// (type 2) improves an ALREADY-learned perk (`Bt.L1a` L306 `e&&f` branch).
bool EquipmentScreen::perk_buyable(int index) const {
    if (index < 0 || index >= static_cast<int>(perk_rows_.size())) return false;
    const PerkRow& r = perk_rows_[index];
    if (!r.available) return false;          // `Mw.K1` L1358
    if (player_level_ < r.tier) return false;  // `zo` (L2223) -> hidden button
    if (r.kind == "Perk") return r.learned_level == 0;  // `Be==0` learn target
    // Upgrade: `L1a` L306 matches the existing `<Perk>` by name (`e`) and
    // type 2 (`f`) -> `Np(PQ())`.
    return r.learned_level > 0;
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
    tab_hover_ = profile_tab_hit(p.x, p.y);
    if (tab_hover_ >= 0 && p.pressed) {
        sf2::audio::AudioEngine::instance().play("click");
        std::fprintf(stdout, "[profile] tab %d (%s)\n", tab_hover_,
                     kProfileTabs[tab_hover_].label);
        std::fflush(stdout);
        tab_ = tab_hover_;
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
                    sf2::audio::AudioEngine::instance().play("click");
                    perk_sel_ = h.index;  // `vb.uj = a` (L2198)
                }
                return;
            }
        }
        const ShopRect ib = profile_improve_rect();
        if (perk_sel_ >= 0 && perk_buyable(perk_sel_) && p.x >= ib.J && p.x <= ib.N &&
            p.y >= ib.P && p.y <= ib.W) {
            if (p.pressed) {
                sf2::audio::AudioEngine::instance().play("click");
                perk_buy(perk_sel_);
            }
            return;
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
                    sf2::audio::AudioEngine::instance().play("click");
                    achiev_claim(i);
                }
                return;
            }
        }
    }
    // Shared `za` nav column (JS `ma.D1`): Dojo/Map/Shop/Settings hops. The
    // oracle `profile_tab*` captures show the nav COLLAPSED (the `МЕНО`
    // header only) — force it like the Map (`za.xyb` collapses on arrival).
    za_update(app(), *this, kScreenProfile, /*force_collapsed=*/true);
}

void EquipmentScreen::render_impl(App& app) {
    sf2::render::Renderer& ren = app.renderer();
    // --- Backdrop: the destination `dojo_shop` art (`Pi.Qa`, L439) ---------
    // JS `vb extends ma` (L2189): `this.Ad = new Pi` (L2196) and `Ea` calls
    // `this.Tya(this.Ad)` (L2195). `Pi` renders `Qa = R.$(E.get(752))` =
    // `locations/dojo_shop/bg.{image}` under `ma.Tya` (L1832) — a dedicated
    // destination background, NOT the dojo location layers.
    draw_destination_backdrop(app);
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
    draw_profile_tabs(app, tab_, tab_hover_);
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
            if (pr.kind == "Perk" && w.level >= pr.tier) any_avail = true;
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
        // `ds.NC` (L2230) sizes every `tk` cell `b.ba(400,150)` and the `Xd`
        // slider scales the cell to the list width (`ff.kf`): cell height =
        // 150 * (listW/400) = 0.375*listW. The native list width is the `jq`
        // viewer width, so `row_h = 0.375*v.width()` (the old flat 52px was
        // ~0.15x, packing the whole tree into the visible band).
        const float row_h = 0.25f * v.width();
        const float gutter = 78.0f;  // `Rx` + tier-level track width
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
        // The `tk` is symmetric about the seam the `Rx` group docks to.
        const float rcx = v.J + v.width() * 0.5f;  // pair centred on the scroll
        float row_top = v.P + 40.0f;               // below the top `Zh` roll
        int last_tier = -1;
        int tier_idx = -1;   // `tk.$i(a==0, a+1==len)` L2228 first/last gate
        int col = 0;
        for (std::size_t i = 0; i < perk_rows_.size(); ++i) {
            const PerkRow& r = perk_rows_[i];
            if (r.tier != last_tier) {
                if (last_tier != -1) {
                    row_top += row_h + 6.0f;
                    col = 0;
                }
                last_tier = r.tier;
                ++tier_idx;
                char tbuf[32];
                std::snprintf(tbuf, sizeof(tbuf), "LV %d", r.tier);
                draw_ui_label(app, v.J + 16.0f, row_top + 8.0f, gutter - 10.0f, 20.0f, tbuf,
                              0.6f, UiAlign::Left, 1.0f, 0.9f, 0.4f);
            }
            if (row_top + row_h > v.W - 34.0f) break;
            if (col >= 2) {  // `tk` packs two `uk` cells per tier
                row_top += row_h + 6.0f;
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
            const bool perk_owned = r.learned_level > 0;
            const bool perk_locked = (w.level < r.tier) || perk_owned;
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
            // `uk.Dy` perk-level badge (`i9a` "pieces/level<N>", L2222):
            // `la(.8)`, `C(FH.x/2 - za()*1.15)`, `D(FH.y/2 - qa()*1.15)`.
            if (r.learned_level >= 1 && r.learned_level <= 9) {
                char lb[24];
                std::snprintf(lb, sizeof(lb), "pieces/level%d", r.learned_level);
                (void)try_draw_atlas_button(app, lb, icx + bdg_dx + bdg_w * 0.5f,
                                            cy + bdg_dy + bdg_h * 0.5f, bdg_w, bdg_h, 1.0f);
            }
            // In-cell name/status label (native fallback; the JS `uk` cell
            // draws no text). Flanks the `tk` pair — the left cell is right
            // aligned into the gutter side, the right/lone cell left aligned
            // outward — so it never crosses the `Rx` seam.
            const std::string nm = loc(app, r.name, r.name);
            char sbuf[64];
            if (r.kind == "Upgrade") {
                std::snprintf(sbuf, sizeof(sbuf), "UPGRADE %d/%d", r.learned_level, r.tier);
            } else if (r.learned_level > 0) {
                std::snprintf(sbuf, sizeof(sbuf), "LEARNED %d", r.learned_level);
            } else {
                std::snprintf(sbuf, sizeof(sbuf), "LEARN AT LV %d", r.tier);
            }
            float tx = icx + ico * 0.5f + 6.0f;
            float tw = v.N - 2.0f - tx;
            UiAlign al = UiAlign::Left;
            if (pair_left) {
                tx = v.J + gutter;
                tw = (icx - ico * 0.5f - 6.0f) - tx;
                al = UiAlign::Right;
            }
            tw = std::max(24.0f, tw);
            draw_ui_label(app, tx, cy - 17.0f, tw, 18.0f, nm, 0.52f, al, 1.0f, 1.0f, 1.0f);
            draw_ui_label(app, tx, cy + 3.0f, tw, 16.0f, sbuf, 0.44f, al, 0.8f, 0.85f, 0.9f);
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
        // moves for the wielded weapon. `es.NC` (L2239) cells are
        // `ba(400,150)`; the native lays the rows inside the `jq` viewer
        // content band (below the top `Zh` roll) via `draw_ui_label`. The old
        // `app.draw_text` passed a RAW glyph scale (not `ua*ea_a1`) — the
        // giant overlapping text in the port capture.
        const float inner_top = v.P + 34.0f;                 // below the `Zh` roll
        const float inner_h = v.height() - 68.0f;
        // `es.NC` (L2239): `b=new ks; b.init(this.Ul[a],this); b.ba(400,150)` —
        // every skill row is a 400x150 cell, so only the oracle's few fit the
        // viewer band. The old 44px pitch stacked ~8 rows of the full catalog.
        constexpr float kMoveRowH = 150.0f;
        draw_ui_label(app, v.J + 16.0f, inner_top, v.width() - 32.0f, 28.0f,
                      loc(app, weapon_, weapon_), 0.85f, UiAlign::Center, 0.35f, 0.22f,
                      0.10f);
        const int max_rows =
            std::max(0, static_cast<int>((inner_h - 32.0f) / kMoveRowH));
        const int n = std::min(static_cast<int>(move_rows_.size()), max_rows);
        for (int i = 0; i < n; ++i) {
            const MoveRow& r = move_rows_[i];
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s   [%s] P%d", r.name.c_str(),
                          r.type.empty() ? "-" : r.type.c_str(), r.priority);
            draw_ui_label(app, v.J + 20.0f,
                          inner_top + 34.0f + static_cast<float>(i) * kMoveRowH,
                          v.width() - 40.0f, 28.0f, buf, 0.62f, UiAlign::Left, 0.18f,
                          0.13f, 0.08f);
        }
        if (move_rows_.empty()) {
            draw_ui_label(app, v.J, v.P + v.height() * 0.5f - 14.0f, v.width(), 28.0f,
                          "No moves for this weapon.", 0.8f, UiAlign::Center, 0.4f, 0.3f,
                          0.2f);
        }
        // `zr=Yr` right panel: the selected move name + the `$r.Op`
        // `Y.na("profile_BtnShow")` view button (L2234 `$r.ba`).
        if (!move_rows_.empty()) {
            const ShopRect& rp = pl.right_slot;
            const std::string& nm = move_rows_.front().name;
            draw_ui_label(app, rp.J + 8.0f, rp.P + 26.0f, rp.width() - 16.0f, 44.0f,
                          loc(app, nm, nm), 0.95f, UiAlign::Center, 0.16f, 0.11f, 0.06f);
            const float bw2 = rp.width() * 0.72f, bh2 = 46.0f;
            const float bx2 = rp.J + rp.width() * 0.5f;
            const float by2 = rp.W - 70.0f;
            if (!try_draw_atlas_button(app, "EButtonBeige", bx2, by2, bw2, bh2, 1.0f)) {
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
        // resolves `res/users/images/<Image>` via `draw_user_image`. JS
        // `gs.NC` (`js.ba(400,300)`, L2232) is an `Xd` slider; the native
        // lays a wrapped grid inside the `vb` viewer rect — the `Xd`
        // scroll/centring behaviour is OPEN.
        if (seal_rows_.empty()) {
            draw_ui_label(app, v.J, v.P + v.height() * 0.5f - 20.0f, v.width(), 40.0f,
                          "No seals owned yet.", 0.9f, UiAlign::Center, 0.7f, 0.7f, 0.7f);
        } else {
            const float cw = std::min(400.0f, v.width() / 2.0f - 12.0f);
            const float chh = cw * 0.75f;  // `js` cell 400x300 (L2232)
            constexpr int kCols = 2;
            const float x0 =
                v.J + (v.width() - static_cast<float>(kCols) * cw) * 0.5f + cw * 0.5f;
            const float y0 = v.P + 20.0f + chh * 0.5f;
            for (std::size_t i = 0; i < seal_rows_.size(); ++i) {
                const SealRow& s = seal_rows_[i];
                const float cx = x0 + static_cast<float>(i % kCols) * cw;
                const float cy = y0 + static_cast<float>(i / kCols) * (chh + 16.0f);
                if (cy + chh * 0.5f > v.W) break;
                if (!draw_user_image(app, s.image, cx, cy, chh * 0.75f, chh * 0.7f, 1.0f)) {
                    // Genuine art miss -> OPEN: JS `js` (L2232) draws only the
                    // `oe(a.fileName)` image; there is no JS flat/seal-name art.
                    draw_flat_button(app, s.name, cx, cy, cw - 20.0f, chh, 0.3f, 0.3f, 0.4f,
                                     false);
                }
                char buf[64];
                // list.xml Seal `Name` is a lang key ("drop_name_blueseal"
                // -> "BLUE SEAL"); resolve it like every other item name.
                std::snprintf(buf, sizeof(buf), "%s x%d", loc(app, s.name, s.name).c_str(),
                              s.count);
                draw_ui_label(app, cx - cw * 0.5f + 10.0f, cy + chh * 0.5f - 24.0f, cw - 20.0f,
                              20.0f, buf, 0.6f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
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
            const float row_h = kAchievRowH;
            float yy = v.P + 6.0f;
            for (std::size_t ri = 0; ri < achiev_rows_.size(); ++ri) {
                const AchievRow& r = achiev_rows_[ri];
                if (yy + row_h > v.W) break;
                const float cy = yy + row_h * 0.5f;
                sf2::render::Renderer& rr = app.renderer();
                const float x0 = v.J + 4.0f, x1 = v.J + v.width() - 4.0f;
                const float q[] = {x0, yy + 2.0f, x1, yy + 2.0f, x1, yy + row_h - 2.0f,
                                   x0, yy + 2.0f, x1, yy + row_h - 2.0f, x0, yy + row_h - 2.0f};
                // `hs`/`is` cell band: the oracle `profile_tab2` rows read as a
                // warm translucent band over the parchment (was an opaque
                // near-black quad).
                rr.draw_triangles(q, 6, r.reward_available ? 0.40f : 0.34f,
                                  r.reward_available ? 0.30f : 0.25f,
                                  r.reward_available ? 0.16f : 0.14f, 0.55f);
                // Icon `is` `Ed.Fs` (L2212): `Achievements01/ach_*` on atlas
                // 270 (`y.MQa` "Achievements01/ach_block_gold", L2470). The
                // flat square is the explicit miss fallback.
                if (!draw_cell_icon(app, r.icon, x0 + 22.0f, cy, 34.0f, 34.0f, 1.0f)) {
                    const float isz = 16.0f;
                    const float iq[] = {x0 + 22.0f - isz, cy - isz, x0 + 22.0f + isz, cy - isz,
                                        x0 + 22.0f + isz, cy + isz, x0 + 22.0f - isz, cy - isz,
                                        x0 + 22.0f + isz, cy + isz, x0 + 22.0f - isz, cy + isz};
                    rr.draw_triangles(iq, 6, 0.35f, 0.35f, 0.4f, 0.95f);
                }
                // `is` cell (L2212) draws NO description text — the text
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
                const float pbx = x0 + 46.0f;
                const float pby = cy + 10.0f;
                const float pbw = std::max(40.0f, x1 - 8.0f - pbx);
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
    draw_za_chrome(app, kScreenProfile, nullptr, /*force_collapsed=*/true);
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
}


// ---------------------------------------------------------------------------
// SettingsScreen
// ---------------------------------------------------------------------------

SettingsScreen::SettingsScreen(ScreenManager& mgr) : Screen(mgr, "Settings") {}

void SettingsScreen::update_impl(float dt) {
    ++age_;  // press debounce: ignore the push-frame held click
    (void)dt;
    ensure_lang(app());  // the lang table powers the Settings_*/Back labels
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
    // AudioEngine semantics, no scene touch). Hit-rect = the JS `c` row
    // (L1917): 800 x icon, centred on the icon local x + 315 design.
    if (p.x >= s.music_row_cx - s.row_w * 0.5f &&
        p.x <= s.music_row_cx + s.row_w * 0.5f &&
        p.y >= s.music_cy - s.row_h * 0.5f && p.y <= s.music_cy + s.row_h * 0.5f) {
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

// JS `od.aa` (L1895: the key gate `L.K.Tj().Db(156)`) applied to the
// `un extends od` dialog (L1916): Escape closes it. JS menus are
// pointer-only — this only handles the key edge, no menu navigation.
void SettingsScreen::on_key(int glfw_key, bool down) {
    if (down && glfw_key == 256) {  // GLFW_KEY_ESCAPE
        std::fprintf(stdout, "[settings] ESC -> previous screen\n");
        std::fflush(stdout);
        manager().pop();
    }
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
    // Title `Vc`: `IVa.Settings_Title` (L1917; ru -> "НАСТРОЙКИ"); `ua(152)`
    // + `La(Z.W6)` (L1900), `Ia(128)` centre.
    draw_ui_label(app, s.title_x, s.title_y, s.title_w, s.title_h,
                  loc(app, "Settings_Title", "SETTINGS"), 1.52f,
                  UiAlign::Center, 0.404f, 0.243f, 0.141f);
    // Rows from the `un` `IVa` table (L1917-1924). `Ca.hasFeature("audio")`
    // (L1928) gates Sound+Music and `("credits")` (L1929) gates Credits; both
    // features are present, so the k=0.5/1.5/2.5 rows. The 170x170
    // `E.get(250)` tiles carry the on/off state (sound/sound_off,
    // music/music_off); labels are the plain `IVa` captions (`a()` L1917),
    // placed at `icon_ya + icon_w/2 + icon_w*.2` (i.e. icon_cx + .7 icon).
    // The labels come from the active `<lang>.<hash>.xml` table
    // (`un`'s `IVa` keys, L1917): Settings_Sound/Music/Credits/Language. The
    // Language row shows the active language's own name (ru -> "Русский").
    const bool sfx_on = sf2::audio::AudioEngine::instance().enabled();
    const std::string lang = app.language().empty() ? "en" : app.language();
    if (load_settings_icons_atlas(app)) {
        try_draw_atlas_button(app, sfx_on ? "sound" : "sound_off", s.sound_cx, s.sound_cy,
                              s.icon, s.icon, 1.0f);
        try_draw_atlas_button(app, music_off_ ? "music_off" : "music", s.music_cx,
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
    // The `Nm` restart notice (`dlgSettingsRestart`, L1929) is hidden in the
    // oracle capture (no language change happened), so it is not drawn.
    // BACK (`Bb("EButtonDark")`). The oracle shows one centred НАЗАД and no
    // RESTART (the RESTART plate + notice were invented; FIDELITY_MATRIX
    // settings row). `Bb.fza` (L1844) maps the style to the sliced-atlas
    // frame (`btnDark`), drawn through the `ESliced` plate
    // (`Ec((fa.x/2|0)-2,0,4,fa.y)`, L1842). Flat only on a genuine art miss.
    if (!(load_sliced_atlas(app) &&
          draw_bb_plate(app, "btnDark", s.back_cx, s.back_cy, s.btn_w, s.btn_h, 1.0f))) {
        draw_flat_button(app, "", s.back_cx, s.back_cy, s.btn_w, s.btn_h, 0.35f, 0.3f, 0.28f,
                         hover_ == 0);
    }
    draw_ui_label(app, s.back_cx - s.btn_w * 0.5f, s.back_cy - 14.0f, s.btn_w, 28.0f,
                  loc(app, "Settings_Back", "BACK"), 0.9f, UiAlign::Center, 1.0f, 1.0f, 1.0f);
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

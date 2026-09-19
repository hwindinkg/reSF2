#pragma once
// fight_camera_sya.hpp — the fight-camera pipeline as one wired function.
//
// Full port of the web original's fight-camera chain:
//   ql.tyb  L363      — target = fighter midpoint (Du.ma = wd.mea(Rw, pF))
//   ql.dZa  L363-365  — smoothed focus: $X/bY/aY + IO*0.15; |LO|<=200; |VG|<=50
//   Ut.Al   L826-827  — panorama Io clamp
//   ma.Sya  L1833     — aspect clamps, width-fit, min-zoom, portrait shift
//
// Wired: FightCamera::framing() (fight.cpp) delegates here, so this header
// is live code on the fight render path — not a dead drop-in. Field mapping
// (fight.hpp FightCamera):
//   go_x_/go_prev_x_  = JS Jl / iq   (smoothed focus / prev focus)
//   du_x_/du_prev_x_  = JS By / DO   (current target / prev target)
//   zoom              = JS f  / tMa  (render zoom, ma.Sya)
//   zoom_layer        = JS Bj / Ut.Bj (layer zoom, xCa)
//   center_x/y        = JS Go.ma fed into the Camera struct (Io-clamped)

#include <algorithm>
#include <cmath>

#include "scene/fight.hpp"           // FightCamera definition (fields framing wires)
#include "scene/location_scene.hpp"  // LocationScene::active_arena_height()

namespace sf2::scene {

// framing_sya_impl — exact JS camera pipeline, called from
// FightCamera::framing(). ax/ay = player world COM, bx/by = enemy world COM,
// view_w/h = viewport px.
inline void framing_sya_impl(FightCamera& cam, float ax, float ay, float bx, float by,
                             float view_w, float view_h) {
    // The CoM y's (ay/by) are the JS `Eu.ma` vertical target — the native
    // keeps the verified floor@0.78 composition instead of the oracle's
    // dummy-CoM (see the struct comment); the horizontal target uses ax/bx.
    (void)ay;
    (void)by;
    // --- the Sya zoom (exact JS L1833) ----------------------------------
    const float aspect = view_w / view_h;
    const float span = std::fabs(bx - ax);             // d = qh.ECa() = |x1-x2|
    // The layer zoom Bj (Ut.xCa L831) FIRST: min(nC/(span+300),1), where
    // nC = b/Ira = viewW/(viewH/Lb.height) (mwa L823-824, raw arena height).
    // Bj then feeds the Sya denominator m$a() = Lb.height * Bj (W5/D11).
    // JS `m$a()` L823: `e = Lb.height * Bj` — the ACTIVE location's Root
    // `Height` (moon 512, dojo 560, arena 512), NOT a fixed default. The fight
    // screen (re)loads its location before the first framing call, and
    // `LocationScene::load` publishes that height; consume it so non-560
    // locations fit the view exactly (e.g. moon: 720/512 = 1.40625 instead of
    // the 560-default 1.3 that left 27 px bars top and bottom). Standalone
    // callers with no scene fall back to the camera's own `arena_h`.
    const float loc_h = sf2::scene::LocationScene::active_arena_height();
    if (loc_h > 0.0f) {
        cam.arena_h = loc_h;
    }
    const float n_c = view_w / (view_h / cam.arena_h);  // mwa: nC = b/Ira
    cam.zoom_layer = std::min(1.0f, n_c / (span + 300.0f));  // Ut.xCa() -> Bj
    // JS `ql.c3a` (L365): `ia.Al(..., this.IJ ? this.Bf.currentScale : 0)` —
    // while the intro lens is live (`IJ`) it supplies the layer scale in
    // place of `Bj`. `cam.zoom_layer` stays the RAW Bj for the pano clamp.
    const float layer_eff =
        cam.zoom_effect_active_ ? cam.zoom_effect_current_ : cam.zoom_layer;
    const float e = cam.arena_h * layer_eff;            // m$a() = Lb.height*Bj
    float f = view_h / e;
    f *= (aspect < 0.45f ? 0.45f : aspect > 1.0f ? 1.0f : aspect);  // c<.45?.45:c>1?1:c
    if (aspect < 0.8f) {
        f *= 0.8f + ((std::max(0.5f, std::min(0.8f, aspect)) - 0.5f) / 0.3f) * 0.2f;
    }
    f *= std::min(1.0f, view_w / (span * f + 100.0f));  // min(viewW/(d*f+100),1)
    const float dmin = 0.6f +
                       ((std::max(0.5f, std::min(1.0f, aspect)) - 0.5f) / 0.5f) *
                           0.7f;                        // the min zoom 0.6..1.3
    if (f < dmin) f = dmin;
    // The RENDER zoom is the Sya f (1.3 at 16:9); the layer/pano zoom is
    // Ut.Bj (1.0 at the fight-start span) — both come from the spec.
    cam.zoom = f;

    // --- the target (ql.tyb + the vertical floor anchor) ----------------
    // First call: start at the spawn midpoint (Lb.z9a) — the f=0 oracle
    // view (831.5, -101.5) — without advancing the chase; the first real
    // frame then produces the oracle's -50 intro jump.
    if (!cam.initialized_) {
        cam.du_prev_x_ = cam.go_x_ = cam.go_prev_x_ = cam.start_x_;
        cam.du_prev_y_ = cam.go_y_ = cam.go_prev_y_ = cam.start_y_;
        cam.du_x_ = cam.start_x_;
        cam.du_y_ = cam.start_y_;
        cam.initialized_ = true;
        cam.center_x = cam.start_x_;
        // JS `N.Ta.K4` (L85) leaves the camera position y at 0; the vertical
        // focus never feeds the render camera (D1/W1).
        cam.center_y = 0.0f;
        return;
    }
    cam.du_x_ = (ax + bx) * 0.5f;                      // By = mid of the CoM's x
    // The vertical target: the arena FLOOR line at 0.78 of the view height
    // (F9*(1-zoom) keeps the line anchored at any zoom — JS Ut.init
    // F9 = (Lb.height/2 - ct)/2).
    const float floor_screen_y = view_h * 0.78f;
    const float vshift = ((cam.arena_h / 2.0f - cam.floor) / 2.0f) * (1.0f - cam.zoom);
    cam.du_y_ = cam.floor + vshift - (floor_screen_y - view_h / 2.0f) / cam.zoom;

    // --- the smoothing (ql.dZa, exact) ----------------------------------
    const float d_x = cam.du_x_ - cam.du_prev_x_;      // $X = By - DO
    const float d_y = cam.du_y_ - cam.du_prev_y_;
    const float b_y_x = cam.go_prev_x_ + d_x;          // bY = iq + $X
    const float b_y_y = cam.go_prev_y_ + d_y;
    float lo_x = (b_y_x - cam.go_x_) + (cam.du_x_ - cam.go_x_) * 0.15f;  // LO = aY + IO
    float lo_y = (b_y_y - cam.go_y_) + (cam.du_y_ - cam.go_y_) * 0.15f;
    const float lo_len = std::sqrt(lo_x * lo_x + lo_y * lo_y);
    if (lo_len > 200.0f) {                             // |LO|>200 -> 200
        lo_x *= 200.0f / lo_len;
        lo_y *= 200.0f / lo_len;
    }
    cam.go_x_ += lo_x;                                 // Jl += LO
    cam.go_y_ += lo_y;
    float vg_x = cam.go_x_ - cam.go_prev_x_;           // VG = Jl - iq
    float vg_y = cam.go_y_ - cam.go_prev_y_;
    const float vg_len = std::sqrt(vg_x * vg_x + vg_y * vg_y);
    if (vg_len > 50.0f) {                              // |VG|>50 -> 50
        cam.go_x_ = cam.go_prev_x_ + vg_x * 50.0f / vg_len;
        cam.go_y_ = cam.go_prev_y_ + vg_y * 50.0f / vg_len;
    }
    cam.du_prev_x_ = cam.du_x_;                        // DO = Du.ma
    cam.du_prev_y_ = cam.du_y_;
    cam.go_prev_x_ = cam.go_x_;                        // iq = Go.ma (post-XA)
    cam.go_prev_y_ = cam.go_y_;

    // --- the panorama clamp (Ut.Al: Io = arenaWidth/2 - focus -----------
    // clamped to +/-((arenaWidth-oGa)*Bj*0.5 - nC*0.5)). The native
    // camera's Io = arena_center - center_x, so the clamp bounds the
    // smoothed focus x by the same range (the fight never leaves the
    // arena view — the floor stays fully covered). n_c = the mwa half-view
    // width (computed above in the zoom section); the clamp uses the
    // LAYER zoom Bj (Ut.Al's d formula), not the camera zoom.
    const float d_io = (cam.arena_w - 0.0f) * 0.5f * cam.zoom_layer - n_c * 0.5f;
    const float center = cam.arena_w * 0.5f;
    cam.center_x = cam.go_x_ < center - d_io   ? center - d_io
                : cam.go_x_ > center + d_io ? center + d_io
                                        : cam.go_x_;
    // JS `N.Ta.K4` (L85) resets the camera position to (0,0) each frame and
    // `Sya` never writes position.y (only the aspect<1 portrait `b.D`). The
    // smoothed `go_y_` stays as chase state but must NOT feed the render
    // camera (D1/W1): the whole invented vertical camera is removed.
    cam.center_y = 0.0f;

    // --- the portrait vertical shift (Sya: c<1 && b.D(...)) -------------
    if (aspect < 1.0f) {
        cam.center_y += std::round((view_h - e * cam.zoom) / 2.0f) / cam.zoom * 0.5f;
    }
}

} // namespace sf2::scene

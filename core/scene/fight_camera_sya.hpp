#pragma once
// fight_camera_sya.hpp — JS-accurate FightCamera::framing() body
//
// Full port of the web original's fight-camera pipeline:
//   ql.tyb  L363  — target = fighter midpoint
//   ql.dZa  L363-365 — smoothed focus with velocity/delta clamps
//   Ut.Al   L826-827 — panorama Io clamp
//   ma.Sya  L1833-1835 — aspect clamps, width-fit, min-zoom, portrait shift
//
// Integration: in fight.cpp replace the body of FightCamera::framing with
// a call to framing_sya_impl():
//
//   #include "scene/fight_camera_sya.hpp"
//   void FightCamera::framing(float ax, float ay, float bx, float by,
//                             float view_w, float view_h) {
//       sf2::scene::framing_sya_impl(*this, ax, ay, bx, by, view_w, view_h);
//   }
//
// Field mapping (fight.hpp FightCamera):
//   go_x_/go_prev_x_  = JS Jl / iq   (smoothed focus / prev focus)
//   du_x_/du_prev_x_  = JS By / DO   (current target / prev target)
//   zoom              = JS f  / tMa  (render zoom, ma.Sya)
//   zoom_layer        = JS Bj / Ut.Bj (layer zoom, xCa)
//   center_x/y        = JS Go.ma fed into the Camera struct (Io-clamped)

#include <algorithm>
#include <cmath>

namespace sf2::scene {

// Forward-declare the struct so the header is self-contained.
struct FightCamera;

} // namespace sf2::scene

// Include fight.hpp for the full FightCamera definition.
// (fight_camera_sya.hpp is always included FROM fight.cpp or a .cpp
//  that already has fight.hpp in scope, so there is no circular dependency.)

namespace sf2::scene {

/// framing_sya_impl — exact JS camera pipeline, call from FightCamera::framing().
/// ax/ay = player world COM, bx/by = enemy world COM, view_w/h = viewport px.
inline void framing_sya_impl(FightCamera& cam,
                              float ax, float /*ay*/,
                              float bx, float /*by*/,
                              float view_w, float view_h) {
    // -----------------------------------------------------------------------
    // 1. Target = mid-fighters  (JS tyb L363: Du.ma = wd.mea(Rw, pF))
    // -----------------------------------------------------------------------
    const float target_x = (ax + bx) * 0.5f;

    // -----------------------------------------------------------------------
    // 2. Smoothed focus x — ql.dZa L363-365
    //
    //   $X  = By - DO          target-delta (how much the target moved)
    //   bY  = iq + $X          predict: shift prev-focus by that delta
    //   aY  = bY - Jl          correction toward the prediction
    //   IO  = (By - Jl)*0.15   proportional pull toward target
    //   LO  = aY + IO
    //   |LO| > 200 → clamp     velocity clamp (200 world-units/frame)
    //   Jl += LO
    //   |Jl - prevJl| > 50 → clamp   per-frame delta clamp (50 units)
    // -----------------------------------------------------------------------
    if (!cam.initialized_) {
        cam.go_x_      = target_x;
        cam.go_prev_x_ = target_x;
        cam.du_x_      = target_x;
        cam.du_prev_x_ = target_x;
        cam.initialized_ = true;
    }

    const float dX   = target_x - cam.du_prev_x_;        // $X = By - DO
    const float bY_x = cam.go_prev_x_ + dX;              // bY = iq + $X
    const float aY_x = bY_x - cam.go_x_;                 // aY = bY - Jl
    const float IO_x = (target_x - cam.go_x_) * 0.15f;  // IO = (By-Jl)*.15
    float LO_x = aY_x + IO_x;
    if (std::abs(LO_x) > 200.0f)
        LO_x = std::copysign(200.0f, LO_x);              // |LO| ≤ 200

    const float prev_go_x = cam.go_x_;
    cam.go_x_ += LO_x;                                   // Jl += LO

    const float VG_x = cam.go_x_ - prev_go_x;
    if (std::abs(VG_x) > 50.0f)                          // |VG| > 50 → clamp
        cam.go_x_ = prev_go_x + std::copysign(50.0f, VG_x);

    cam.go_prev_x_ = prev_go_x;   // advance iq
    cam.du_prev_x_ = cam.du_x_;   // advance DO
    cam.du_x_      = target_x;    // advance By

    // -----------------------------------------------------------------------
    // 3. Panorama Io clamp — Ut.Al L826-827
    //
    //   Io      = arena_center(0) − focus
    //   nC      = (view_h / arena_h) * arena_w   visible world width
    //   io_max  = (arena_w * Bj * 0.5) − (nC * 0.5)
    //   Io      = clamp(Io, −io_max, +io_max)
    //   center_x = −Io   (camera center tracks focus minus Io offset)
    // -----------------------------------------------------------------------
    const float Bj = cam.zoom_layer;  // Ut.Bj — 1.0 at static fight start
    const float nC = (cam.arena_h > 0.0f)
                     ? (view_h / cam.arena_h) * cam.arena_w : 0.0f;
    const float io_max = (cam.arena_w * Bj * 0.5f) - (nC * 0.5f);
    float Io = -cam.go_x_;            // Io = 0(arena_center) - focus
    if (io_max > 0.0f) {
        if (Io >  io_max) Io =  io_max;
        if (Io < -io_max) Io = -io_max;
    }
    cam.center_x = -Io;              // camera.center_x = 0 - Io

    // -----------------------------------------------------------------------
    // 4. Zoom — ma.Sya L1833-1835
    //
    //   e = arena_h * Bj           visible world height
    //   f = view_h / e             base zoom: arena fills screen vertically
    //   f *= clamp(c, 0.45, 1)     primary aspect clamp
    //   if c < 0.8: f *= narrow    narrow-screen secondary factor
    //   f *= min(vw/(span*f+100),1) width-fit with 100px margin
    //   f = max(f, min_zoom)       minimum zoom floor  0.6..1.3
    // -----------------------------------------------------------------------
    const float visible_h = cam.arena_h * Bj;
    float f = (visible_h > 0.0f) ? view_h / visible_h : 1.0f;

    // aspect ratio c = view_w / view_h
    const float c = (view_h > 0.0f) ? view_w / view_h : 1.777f;

    // primary aspect clamp: f *= clamp(c, 0.45, 1)
    {
        const float ac = c < 0.45f ? 0.45f : (c > 1.0f ? 1.0f : c);
        f *= ac;
    }
    // narrow-screen secondary clamp (only when c < 0.8):
    //   f *= 0.8 + ((clamp(c,0.5,0.8) - 0.5) / 0.3) * 0.2
    if (c < 0.8f) {
        float t = c < 0.5f ? 0.5f : (c > 0.8f ? 0.8f : c);
        t = (t - 0.5f) / 0.3f;
        f *= 0.8f + t * 0.2f;
    }
    // width-fit: f *= min(view_w / (span*f + 100), 1)
    //   span = |ax - bx|   (JS ECa = |Rw.Eu.x - pF.Eu.x|)
    const float span = std::abs(ax - bx);
    if (span * f + 100.0f > 0.0f)
        f *= std::min(view_w / (span * f + 100.0f), 1.0f);

    // minimum zoom floor: 0.6 + ((clamp(c,0.5,1) - 0.5) / 0.5) * 0.7
    //   → 0.6 at c=0.5 (portrait), 1.3 at c=1.0 (square), stable at 16:9
    {
        const float t = c < 0.5f ? 0.5f : (c > 1.0f ? 1.0f : c);
        const float min_z = 0.6f + ((t - 0.5f) / 0.5f) * 0.7f;
        if (f < min_z) {
            f = min_z;
            cam.center_x = 0.0f;  // JS: C(0) — lock camera to center
        }
    }
    cam.zoom       = f;  // the render Camera.zoom (JS tMa)
    cam.zoom_layer = f;  // Bj — static fight has no separate xCa lens zoom

    // -----------------------------------------------------------------------
    // 5. Portrait vertical shift — Sya L1833 (only when c < 1)
    //
    //   JS: b.D( round((N.height - e*f) / 2) / f * 0.5 )
    //   D() moves the camera DOWN; native center_y positive = camera UP,
    //   so negate to match the JS convention.
    // -----------------------------------------------------------------------
    if (c < 1.0f) {
        const float e = visible_h * f;
        cam.center_y = -std::round((view_h - e * f) / 2.0f) / f * 0.5f;
    }

    // -----------------------------------------------------------------------
    // 6. Camera shake — ql.d3a / DL (decaying random offset, 0.85/frame)
    // -----------------------------------------------------------------------
    cam.center_x += cam.shake_x_;
    cam.center_y += cam.shake_y_;
    cam.shake_x_ *= 0.85f;
    cam.shake_y_ *= 0.85f;
}

} // namespace sf2::scene

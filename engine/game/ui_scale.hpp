#pragma once

// UI coordinate law of the original Marmalade build (ShadowFight2.s86).
//
// [ORIGINAL] The original lays every screen element out in a POINT space that
// is always 768 points tall; the width floats with the aspect ratio.
//
//   ui_scale  = screen_h_px / 768.0f          points -> pixels
//     Written at 0x10240df7 (FUN_10240cd0, first init) and 0x10242178
//     (FUN_10242050, on video-mode change) into the global DAT_1064e774,
//     read back through the getter FUN_10240160. The 768.0f is the literal
//     0x44400000 pushed at 0x10240dd6 as the design CCSize {1365.25, 768}
//     - only .height is ever used for this scale.
//   logical_w = screen_w_px / ui_scale        (DAT_10664fe4)
//   logical_h = screen_h_px / ui_scale = 768  (DAT_10664fe8)
//
// Textures do NOT live in point space directly: the asset tier carries a
// content scale. SystemProperties::setPicturePaths (FUN_10241f90) registers
//
//   FUN_101825f0(1, "assets/768/", "assets/1536/", 2.0f)   HIGH tier
//   FUN_101825f0(0, "assets/768/", "assets/1536/", 1.0f)   LOW  tier
//
// and the 2.0f/1.0f lands in DAT_1064cb94, which Picture::init (FUN_101826c0)
// divides out of every sprite's content size and FUN_10181610 multiplies back
// into texture rects. So a sprite from a 1536-tier atlas is atlas_px / 2
// points big, and one atlas pixel covers
//
//   screen_h_px / (768 * 2) = screen_h_px / 1536
//
// screen pixels - at a 1536-tall window the HIGH-tier atlases are exactly 1:1.
// The tier itself is screen_h > 768 ? HIGH : LOW unless devices.xml overrides
// it (parser FUN_10240e30); this engine ships the 1536 tier, so the content
// scale here is a constant 2.

namespace resf2::ui {

inline constexpr float kDesignHeightPts = 768.0f;      // 0x44400000 @ 0x10240dd6
inline constexpr float kHighTierContentScale = 2.0f;   // FUN_10241f90 -> FUN_101825f0 arg 4

// Screen pixels per point (the original's DAT_1064e774).
inline float points_scale(float win_h_px) { return win_h_px / kDesignHeightPts; }

// Screen pixels per pixel of a 1536-tier atlas.
inline float atlas_scale(float win_h_px) {
    return win_h_px / (kDesignHeightPts * kHighTierContentScale);
}

// [ORIGINAL] The top panel's screen height is its own atlas height mapped
// through the law above, not a fraction of the viewport: FUN_1014ca50 takes
// getContentSize().height of Top_Panel (192 px in batchPanelsTop.plist ->
// 96 points) verbatim and stretches only X across the screen, then publishes
// the resulting height for everything below to lay out against.
inline constexpr float kTopPanelAtlasH = 192.0f;
inline float top_panel_h(float win_h_px) {
    return kTopPanelAtlasH * atlas_scale(win_h_px);
}

}  // namespace resf2::ui

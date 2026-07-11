# 06 — Engine: Marmalade SDK findings

> Status: high-level identification complete. Full subsystem inventory
> (which IwXxx modules are actually used by Shadow Fight 2) is a
> Stage 2 task.

## Engine identity

```
Assertion Failure (Marmalade v8.2.1 [465988])
Error (Marmalade v8.2.1 [465988])
```

— strings found in `libs3e_android.so`.

Marmalade SDK **v8.2.1** (build `465988`) was released in late 2014 /
early 2015. The APK itself is dated 2016-06-09, so the game shipped
with a slightly older Marmalade than latest (8.3 was out by then), but
this is normal — Nekki pinned to 8.2.1 for stability across releases.

## What Marmalade is

Marmalade SDK (formerly Airplay SDK) was a cross-platform C++ game
engine from **Ideaworks3D** / **Marmalade Technologies**, active from
2009 to 2017. It abstracted:

- Window / GL context / input on iOS, Android, macOS, Windows, Linux,
  tvOS, Tizen, Roku, PlayStation Mobile, BlackBerry 10.
- OpenGL ES 1.1 / 2.0 / 3.0 via IwGx (Marmalade's GL abstraction).
- Audio via IwSound (OpenSL ES / AudioUnit / ALSA / XAudio2).
- Resource management via IwResManager (binary group files built
  offline by the Marmalade Deployment Tool).
- 2D / UI via IwUI (immediate-mode-style UI).
- 2D rendering helpers via Iw2D (canvas-style API on top of IwGx).
- HTTP / sockets via IwHTTP.
- Multiplayer via s3eSmartFox (a wrapper around SmartFoxServer 2X).

Game code was written in standard C++ against the `s3e*` (low-level)
and `Iw*` (high-level) APIs, then deployed as a `.s3e` binary (an
ELF-like container with ARM code + data) that the platform-specific
loader (`libs3e_android.so` on Android, the iOS launcher on iOS)
executes.

The `.s3e` binary model is what makes Marmalade distinctive: a single
game binary is shipped as a data file, and the platform loader runs it
in-process via dynamic relocation. This is conceptually similar to
how `.dex` files work on Android, except `.s3e` files contain native
ARM machine code.

## Marmalade subsystems likely used by Shadow Fight 2

Based on the assets, the manifest, the Java glue, and the native libs
present, Shadow Fight 2 uses:

| Marmalade module         | Evidence | reSF2 equivalent |
| ------------------------ | -------- | ---------------- |
| `s3e` (core)             | `libs3e_android.so` exists; `JNI_OnLoad` boots the `.s3e` | `engine/runtime/` |
| `IwGx` (GLES2 rendering) | `SysGlesVersion = 2`; `.png`/`.jpg` textures; `.plist` atlases; landscape-only | `engine/renderer/` |
| `IwResManager`           | `.s3e` binary contains resource sections; `files.dz` is a custom resource index | `engine/runtime/asset_manager.*` |
| `IwSound`                | 76 `.wav` files, 5 `.mp3` files; `MODIFY_AUDIO_SETTINGS` permission | `engine/audio/` |
| `IwUI`                   | CocoGUI JSON scenes (raids UI) | `engine/ui/` (new) |
| `IwHTTP`                 | `INTERNET` permission; CDN config (`config_cdn.xml`); version controller | `engine/network/http.*` |
| `IwNotifications`        | `libs3eAndroidNotifications.so`, `com.nekki.androidnotifications.TimeAlarm` | `engine/platform/notifications.*` |
| `s3eSmartFox`            | `libsmartfox.so` + `libs3eSmartFox.so`; raid mode; `versionController.xml` references online content | `engine/network/smartfox.*` |
| `s3eApkExpansionFile`    | `libs3eApkExpansionFile.so` + `app_android_obb.icf` | `engine/platform/obb.*` (Android only) |
| `s3eAndroidGooglePlayBilling` | `libs3eAndroidGooglePlayBilling.so` + `BILLING` permission | out of scope for reSF2 |
| `s3eGCMClient`           | `libs3eGCMClient.so` + GCM receivers | out of scope (GCM is deprecated) |

Modules NOT used (or at least not visible):
- `IwBillboard` — no billboard ad SDK integrated at the Marmalade layer
  (all ad SDKs are at the Java layer via s3e* extensions).
- `IwNuklear` / immediate-mode UI — not visible, the game uses IwUI /
  CocoGUI retained mode.
- `IwGeom` physics — Marmalade ships a small math library but not a
  physics engine. The game implements its own collision / physics in
  the `.s3e` `.text` section.
- `IwModel` / `IwModelBuilder` — no 3D model files in assets; the game
  is 2D only.

## Marmalade main loop

Marmalade's typical app structure (from the SDK docs, which are public):

```cpp
int main() {
    s3e::Initialize();
    IwGxInit();           // or similar per-module init
    // ... user init ...

    while (!s3eDeviceCheckQuitRequest()) {
        s3eDeviceYield(0);          // let OS breathe
        s3eKeyboardUpdate();
        s3ePointerUpdate();

        // user update
        Update(s3eTimerGetMs());

        // user render
        IwGxSetColClear(0, 0, 0, 0xff);
        IwGxClear();
        Render();
        IwGxFlush();
        IwGxSwapBuffers();
    }

    // ... user shutdown ...
    IwGxTerminate();
    s3e::Terminate();
    return 0;
}
```

reSF2 will use the same overall structure but expressed as C++20:
- `engine::Runtime` owns the main loop.
- `engine::platform::Platform` abstracts `s3eDeviceYield`,
  `s3eKeyboardUpdate`, `s3ePointerUpdate`.
- `engine::renderer::Renderer` abstracts `IwGx*`.
- `engine::animation::Manager`, `engine::physics::World`,
  `engine::audio::Mixer` are subsystems that the runtime ticks.

## Frame timing

Marmalade uses `s3eTimerGetMs()` for wall-clock time and the typical
pattern is variable-step updates driven by the platform's vsync. The
game appears to target 60 FPS (typical for 2014-2016 mobile fighters),
but Stage 3 must confirm whether the gameplay update is fixed-step or
variable-step (fixed-step is more common for fighting games to keep
combo timing deterministic).

## Resource loading

Marmalade's IwResManager uses **binary group files** (`.group.bin`)
built offline by the Deployment Tool. Shadow Fight 2 does NOT ship
`.group.bin` files — instead, it ships:

- `.s3e` containing the executable + an embedded resource group
- `files.dz` containing a file manifest
- Standard assets (`.png`, `.jpg`, `.wav`, `.xml`) on disk
- `.dz` archives for animation data
- `.atf` (zlib-compressed) for tactics

This suggests Nekki wrote their own asset system on top of Marmalade's
file I/O rather than using IwResManager's binary groups. reSF2 will
follow the same pattern — a plain `AssetManager` that resolves paths
to on-disk files (and to entries inside `.dz` archives), with per-format
loader plugins.

## Memory model

`app_android.icf` declares:

```ini
MemSize = [s3e] DispAreaQ + 94371840
```

`DispAreaQ` is Marmalade's display-quad size (screen_width *
screen_height * bytes_per_pixel). For a 1080p landscape display:
1920 × 1080 × 4 = 8 294 400 bytes → `MemSize` ≈ 8 294 400 + 94 371 840
≈ 102 666 240 bytes (~98 MB) total heap.

`app_android_obb.icf` raises this to `DispAreaQ + 104 857 600`
(~100 MB + display).

reSF2 will use a dynamic memory model (no fixed heap) — modern
platforms have no need for the fixed-heap constraint that 2014 mobile
imposed.

## GL configuration

```ini
[GL]
EGL_DEPTH_ENCODING_NV = 0x30E3
```

`EGL_DEPTH_ENCODING_NV` (NV_depth_nonlinear) is an NVIDIA extension
that uses a non-linear depth buffer mapping for better Z-precision in
the near field. On non-NVIDIA hardware Marmalade silently falls back
to a linear depth buffer.

reSF2 will use a standard 24-bit depth buffer (no non-linear encoding)
— the visual difference is negligible for a 2D fighter.

## What's NOT in Marmalade (Nekki's custom code)

The `.s3e` binary contains Nekki's game-specific code on top of
Marmalade. This includes:

- **Skeletal animation system** — Nekki's proprietary format (the
  `.dz` animation archive + the `moves.xml` schema).
- **Combat tactics engine** — Nekki's proprietary format (the `.atf`
  zlib blobs).
- **CocoGUI scene loader** — a Cocos2d-x-compatible UI reader that
  Nekki wrote themselves (no Cocos2d-x lib is linked into the game).
- **Raid / multiplayer mode** — Nekki's protocol on top of SmartFox2X.
- **AI state machines** — per-NPC behaviour trees.

All of these are targets for Stages 3..7.

## Why Marmalade was used (historical context)

In 2013-2014, when Shadow Fight 2 was in development:

- Unity was expensive (per-seat licensing, revenue share above $100K).
- Cocos2d-x was free but had rough Android tooling at the time.
- Unreal Engine was overkill for a 2D fighter.
- Marmalade offered: C++ across iOS+Android+more, no per-seat fees
  (only revenue share), IwGx for GLES2, IwResManager for asset
  packing, and a clean s3e ABI.

Nekki had already shipped the original Shadow Fight (1) on Marmalade,
so the choice was path-dependent.

By 2017 Marmalade was EOL (Marmalade Technologies went into
administration). Nekki later moved Shadow Fight 3 to Unity. Shadow
Fight 2 itself never received a non-Marmalade port, which is why it
will stop running on modern Android (64-bit-only devices drop 32-bit
ARM support).

## reSF2 architecture (target, Marmalade-compatible)

```
                     ┌──────────────────────────┐
                     │   engine::Runtime         │ ← main loop, scheduler
                     └──────────┬───────────────┘
                                │
       ┌───────────┬───────────┼───────────┬──────────────┐
       ▼           ▼           ▼           ▼              ▼
 ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐
 │ platform│ │ renderer │ │ physics │ │ animation│ │  audio   │
 │ (s3e*)  │ │ (IwGx)   │ │ (custom)│ │ (custom) │ │ (IwSound)│
 └────┬────┘ └────┬─────┘ └────┬────┘ └────┬─────┘ └────┬─────┘
      │           │            │           │            │
      ▼           ▼            ▼           ▼            ▼
 ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐
 │ Android │ │ GLES2 /  │ │ hitbox/ │ │ .dz +    │ │ WAV/MP3  │
 │ Linux   │ │ Vulkan / │ │ hurtbox │ │ moves.xml│ │ mixer    │
 │ Windows │ │ Metal    │ │ tables  │ │ loader   │ │          │
 │ macOS   │ │ backends │ │         │ │          │ │          │
 │ Switch  │ │          │ │         │ │          │ │          │
 └─────────┘ └──────────┘ └─────────┘ └──────────┘ └──────────┘
```

Subsystem-specific notes:

- `engine::platform` replaces `s3e*` device APIs. One implementation per
  target OS. The Android implementation wraps `NativeActivity` +
  `EGLContext` + `AAssetManager`.
- `engine::renderer` replaces `IwGx`. The first backend will be
  GLES2.0/GL2.1 (for Android / Linux / macOS), with a Vulkan backend
  later (for Switch / Steam Deck / modern Android).
- `engine::physics` is fully custom — Marmalade does not provide one.
  We re-implement only what the game uses (hitbox-vs-hurtbox
  intersection, no rigid-body dynamics).
- `engine::animation` is fully custom — Nekki's format, not Marmalade's
  IwAnim.
- `engine::audio` wraps miniaudio (public domain) instead of IwSound.
- `engine::network` implements the SmartFox2X binary protocol directly,
  no Marmalade dependency.

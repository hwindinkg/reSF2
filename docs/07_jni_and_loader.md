# 07 — JNI bridge and loader

> Status: high-level map only. Full `RegisterNatives()` table recovery
> is Stage 2 task S2.5.

## JNI surface of `libs3e_android.so`

```
$ nm -D --defined-only lib/armeabi-v7a/libs3e_android.so | grep -E 'Java_|JNI_'
0003c770 T JNI_OnLoad
0003c438 T JNI_OnUnLoad
```

That's the entire static JNI surface. **All Java↔native bindings are
registered dynamically** via `RegisterNatives()` inside `JNI_OnLoad`.

## Why this matters

Statically-exported JNI symbols (e.g. `Java_com_foo_bar_baz`) have their
signatures encoded directly in the symbol name. Dynamically-registered
natives have no such encoding — the `RegisterNatives()` call site must
be inspected to recover the Java method name + C++ function pointer +
JVM-internal signature.

This is normal for Marmalade — the SDK uses `RegisterNatives()` so that
the same Java class (`com.ideaworks3d.marmalade.LoaderThread`) can
dispatch to a stable C++ entry-point table even as the underlying
Marmalade version evolves.

## Java classes that receive native methods

From `strings(1)` on `libs3e_android.so`:

```
Lcom/ideaworks3d/marmalade/LoaderThread;
Lcom/ideaworks3d/marmalade/LoaderThread$MediaPlayerManager;
Lcom/ideaworks3d/marmalade/LoaderView;
Lcom/ideaworks3d/marmalade/LoaderKeyboard;
```

Plus, by analogy with the Marmalade SDK's public source (which was open
until 2017), the following LoaderThread fields / methods are likely
registered as native:

| Java class                           | Probable native methods (hypothesised, not verified) |
| ------------------------------------ | ---------------------------------------------------- |
| `LoaderThread`                       | `nativeInit`, `nativeDestroy`, `nativeRunFrame`, `nativePause`, `nativeResume`, `nativeSurfaceCreated`, `nativeSurfaceChanged`, `nativeSurfaceDestroyed`, `nativeTouchDown`, `nativeTouchMove`, `nativeTouchUp`, `nativeKeyDown`, `nativeKeyUp`, `nativeSetAssetPath`, `nativeLoadS3E`, ... |
| `LoaderView`                         | `nativeRendererCreated`, `nativeRendererChanged`, `nativeRendererDestroyed` |
| `LoaderKeyboard`                     | `nativeSoftKeyboardShow`, `nativeSoftKeyboardHide`, `nativeSoftKeyboardInput` |
| `LoaderThread$MediaPlayerManager`    | `nativeMediaPlayerCreate`, `nativeMediaPlayerPrepare`, `nativeMediaPlayerStart`, `nativeMediaPlayerPause`, `nativeMediaPlayerStop`, `nativeMediaPlayerRelease`, `nativeMediaPlayerSetSurface` |

**Confidence: medium.** These names match the public Marmalade SDK
source layout circa 2014, but Nekki may have customized them. Stage 2
task S2.5 will verify by:

1. Disassembling `JNI_OnLoad` at offset `0x0003c770` in
   `libs3e_android.so` with Ghidra or IDA.
2. Finding the `JNINativeMethod[]` array literal that gets passed to
   `RegisterNatives()`.
3. Extracting the (Java name, C++ symbol, JNI signature) triples.

## Other JNI bridges (per-extension)

Each `libs3e<ExtensionName>.so` registers its own natives in its own
`JNI_OnLoad`. For Stage 1 we did not enumerate these — they are
relevant only for the corresponding platform extension
(IAP / ads / analytics) and most are out of scope for reSF2.

| Extension `.so`                       | Java class (probable) |
| ------------------------------------- | --------------------- |
| `libs3eAndroidGooglePlayBilling.so`   | `com.ideaworks3d.marmalade.s3eAndroidGooglePlayBilling.*` |
| `libs3eApkExpansionFile.so`           | `com.ideaworks3d.marmalade.s3eApkExpansionFile.*` |
| `libs3eAndroidNotifications.so`       | `com.nekki.androidnotifications.*` (custom) |
| `libs3eGCMClient.so`                  | `com.marmalade.studio.android.gcm.s3eGCMClient*` |
| `libs3eAdColony.so`, `libs3eChartBoost.so`, `libs3eTapjoy.so`, `libs3eSponsorPay.so`, `libs3eAppsFlyer.so`, `libs3eFacebook.so` | (various, not enumerated) |
| `libs3eGPGS.so`                       | `com.nekki.gpgs.GameHelper` (custom wrapper) |
| `libs3eObbGui.so`                     | `com.nekki.sf2.s3eObbGui` |
| `libs3eDialog.so`                     | `com.ideaworks3d.marmalade.s3eDialog` (probable) |
| `libs3eKeyListener.so`                | `com.ideaworks3d.marmalade.LoaderKeyboard` (probable, may share with LoaderThread) |
| `libs3eDeviceUniqueID.so`             | `com.ideaworks3d.marmalade.s3eDeviceUniqueID` (probable) |
| `libs3eGetSimOperator.so`             | `com.ideaworks3d.marmalade.s3eGetSimOperator` (probable) |
| `libs3ePermissions.so`                | `com.ideaworks3d.marmalade.s3ePermissions` (probable) |
| `libs3eNekkiExtension.so`             | `com.nekki.<something>` (custom, only 5 KB stub) |
| `libAmazonIAP.so`                     | Amazon IAP wrapper (only on Amazon build variant) |
| `libgamepad.so` / `libInputDeviceExtension.so` | `com.ideaworks3d.marmalade.LoaderThread` (input dispatch) |
| `libImmEndpointWarpJ.so`              | `com.ideaworks3d.marmalade.s3eImmersion` (probable) |
| `libs3eSmartFox.so`                   | `com.ideaworks3d.marmalade.s3eSmartFox` (probable) |
| `libs3eFfmpeg.so`                     | `com.ideaworks3d.marmalade.LoaderThread$MediaPlayerManager` (probable) |

Stage 2 task S2.6 will cross-check this against the DEX declarations
of `native` methods.

## Boot sequence (high-level)

Based on the manifest and the Java glue class names, the boot sequence
on Android is approximately:

1. Android launches `com.nekki.shadowfight.Main` (the LAUNCHER activity).
2. `Main.onCreate()`:
   - Reads `app_android.icf` (or `app_android_obb.icf` for the OBB build).
   - Applies `DispFixRot=Landscape` (locks activity to landscape).
   - Creates a `LoaderView` (a `GLSurfaceView` subclass) and sets it
     as the content view.
   - Creates a `LoaderThread` and binds it to the `LoaderView`'s
     `GLSurfaceView.Renderer`.
   - The `LoaderThread` calls `System.loadLibrary("s3e_android")`,
     which triggers `JNI_OnLoad` → `RegisterNatives()`.
3. The `LoaderView.Renderer.onSurfaceCreated()` callback:
   - Calls `LoaderThread.nativeSurfaceCreated()`.
   - Native side initialises EGL, IwGx, IwResManager.
4. The `LoaderView.Renderer.onSurfaceChanged()` callback:
   - Calls `LoaderThread.nativeSurfaceChanged(w, h)`.
   - Native side sets up the viewport and the display-quad size.
5. The `LoaderThread` then loads `assets/ShadowFight2.s3e`:
   - Calls `LoaderThread.nativeLoadS3E("ShadowFight2.s3e")`.
   - Native side reads the file, verifies signature, LZMA-decompresses,
     parses the `XE3U` header, applies relocations, and jumps to the
     entry point defined in the `.s3e` section table.
6. The `.s3e` entry point runs Marmalade's `main()`:
   - `s3e::Initialize()`, `IwGxInit()`, etc.
   - Loads `assets/settings.xml` to enumerate game data XMLs.
   - Loads `assets/assets/files.dz` to enumerate on-disk assets.
   - Builds the resource manager's path → file-handle map.
   - Enters the main loop.

## reSF2 boot sequence (target)

```
main(argc, argv)
  → resf2::platform::Platform::create()
  → resf2::runtime::Runtime::create(platform)
  → resf2::renderer::Renderer::create(platform, GLES2)
  → resf2::audio::Mixer::create(platform)
  → resf2::asset::AssetManager::create(asset_root)
     ├─ parse settings.xml
     ├─ mount files.dz
     ├─ mount animations.dz
     └─ register per-format loaders (.png, .plist, .atf, .xml, .fnt, ...)
  → resf2::game::Game::create(runtime, renderer, mixer, assets)
  → runtime.run(game)
     └─ loop:
          ├─ platform.poll_events()
          ├─ game.update(dt)
          ├─ renderer.begin_frame()
          ├─ game.render(renderer)
          ├─ renderer.end_frame()
          └─ platform.yield()
  → ... shutdown in reverse order ...
```

The key difference from the original is: reSF2 does **not** load or
execute the original `.s3e` binary. Instead, the game logic is
re-implemented in clean-room C++ (Stage 7), and the `.s3e` is used
only as a **read-only data source** for reverse-engineering purposes
(dumping strings, shaders, relocations, etc. — all done at build time
or by RE tools, never at runtime on the user's machine).

This keeps reSF2 clean-room: the only original binary is the user's
APK / `.s3e`, used only as input to RE tools. The reSF2 binary itself
contains zero code from Marmalade or Nekki.

## Stage 2 deliverables for this doc

- `docs/10_jni_registration_map.md` — full `RegisterNatives()` table
  for `libs3e_android.so` and at least `libs3eSmartFox.so` /
  `libs3eAndroidGooglePlayBilling.so`.
- `docs/09_s3e_binary_format.md` — section table layout, relocation
  format, entry-point resolution.

These will be produced by:
- Disassembling `JNI_OnLoad` (offset `0x0003c770` in
  `libs3e_android.so`) with Ghidra.
- Cross-referencing against the DEX-declared `native` method list.

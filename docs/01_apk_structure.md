# 01 — APK structure

> Source: `Shadow Fight 2_1.9.21.apk`
> sha256: `9258146bb87e7d1010ebbd6cc9f7bc9f00f1f2ff61ae4a73cd29003b072f5143`
> md5:    `b81636e3fb3b6de7ea34c57a9673ce00`
> Size:   94 736 412 bytes (90.3 MiB)
> Build:  2016-06-09 18:13 / 18:15 (UTC)

## Top-level layout

| Path                    | Files | Bytes      | Notes |
| ----------------------- | -----:| ----------:| ----- |
| `assets/`               | 1 866 | 86 588 296 | All game data + `ShadowFight2.s3e` binary |
| `res/`                  |   243 |     583 337 | Android resources (icons, strings, layouts) |
| `lib/`                  |    30 |   6 431 992 | Native libs, `armeabi-v7a` only |
| `META-INF/`             |    16 |     494 057 | Manifest, signatures, maven metadata |
| `com/`                  |    14 |      17 292 | Loose `.java` files (banner-mediation SDK sources, not compiled) |
| `banner_mediation_support/` | 6 |       5 470 | Banner-mediation source files |
| `js/`                   |     2 |      55 511 | JavaScript (likely Tapjoy mediation) |
| `classes.dex`           |     1 |   8 607 228 | Main Java DEX |
| `classes2.dex`          |     1 |   2 289 788 | Secondary Java DEX (MultiDex) |
| `resources.arsc`        |     1 |      89 184 | Compiled Android resources |
| `AndroidManifest.xml`   |     1 |      19 908 | Binary Android manifest (decoded by apktool) |
| **Total**               | **2 181** | **105 182 063** | |

The loose `.java` files under `com/` and `banner_mediation_support/` are
**source code** shipped in the APK (Fyber banner-mediation adapters).
They are not compiled into the DEX — they appear to be packaged for
runtime use by reflection / dynamic compilation. reSF2 does not need
them.

## `assets/` layout

```
assets/
├── ShadowFight2.s3e          # 2 858 937 B  LZMA-compressed Marmalade binary
├── app_android.icf           #     376 B    Marmalade config (Android, no OBB)
├── app_android_obb.icf       #     503 B    Marmalade config (Android, OBB build)
├── app_ios_store.icf         #     449 B    Marmalade config (iOS, App Store)
├── app_localbuild.icf        #     357 B    Marmalade config (local dev)
├── settings.xml              #   5 910 B    Master manifest of XML files
├── vg_close.png              #   6 839 B    GDPR / privacy UI icons
├── vg_cta.png                #   6 437 B
├── vg_cta_disabled.png       #   5 734 B
├── vg_mute_off.png           #   7 107 B
├── vg_mute_on.png            #   6 924 B
├── vg_privacy.png            #   1 324 B
├── Fonts/
│   └── Arial.ttf             # 773 236 B    System Arial fallback
└── assets/                   # actual game asset tree
    ├── 1536/                 # scale-1536 resources (high-density, see iOS asset scale conventions)
    ├── 768/                  # scale-768 resources (medium-density)
    ├── animations.dz         # DTRZ archive of animation XML
    ├── files.dz              # DTRZ archive of file manifest
    ├── cocoGUI/              # Cocos2d-x CocoGUI JSON UI scenes (raids)
    ├── credits/              # end-credits material
    ├── devices.xml           # device capability profile
    ├── googleActivity.ini    # Google Activity config
    ├── locations/            # 50+ battle locations (see below)
    ├── music/                # MP3 music tracks
    ├── obbSettings.xml       # OBB expansion-file settings
    ├── sounds/               # WAV SFX
    ├── tactics/              # 110 .atf weapon-pair combat tactics
    ├── versionController.xml # version / content gating
    └── video/                # MP4 cutscenes
```

### Locations (50 directories)

`arena`, `autumn`, `bamboo_grove`, `battlefield`, `bridge`, `burning_town`,
`capsules`, `castle_and_bridge`, `cave`, `chess_yard`, `dark_room`,
`dojo`, `eggs`, `emerald_forest`, `factory`, `fatum_raid`,
`flooded_village`, `flowers_field`, `flying_rocks`, `flying_rocks_small`,
`fuji`, `fungus_raid`, `graveyard_ships`, `heaven`, `ice_cave`,
`lamps_on_water`, `lava`, `magic_rocks`, `megalith_raid`, `moon`,
`mountain`, `neural_network`, `new_year_dojo`, `night_bridge`,
`pink_lake`, `road`, `ruins_village`, `ruins_village_small`, `sakura`,
`shadow_gate`, `ships`, `skyport`, `snowy_peak`, `spaceship`,
`spaceship_thorny`, `statue`, `stone_dragon`, `stone_forest`,
`stone_forest_thorny`, `swamp`, `village`, `volcano`, `vortex_raid`,
`vulcan_raid`, `waterfall`, `waterfall_small`.

Locations with the `_raid` suffix or `_small` suffix are variants
(raids = multiplayer boss fights; `_small` = low-memory variant).

### Scale dirs: `1536/` vs `768/`

Marmalade uses an iOS-derived asset scale system. `1536` corresponds to
the iPad-retina / iPad-3 density tier (`IOSDispScaleFactor = 200` in
the `.icf`, with a baseline of `768` for non-retina). On Android this
maps to roughly `drawable-xhdpi` and `drawable-mdpi` respectively. The
`1536/` tree is the primary one; `768/` is the fallback.

Inside `1536/`:

| Subdir              | Contents                                     |
| ------------------- | -------------------------------------------- |
| `fonts/`            | `.fnt` + `.png` bitmap font pairs (16 pairs) |
| `fonts/eng/`        | English-specific font pairs                  |
| `fonts/rus/`        | Russian-specific font pairs                  |
| `image/`            | UI icons: achievements, attributes, battles, combobuttons, enchantments, sales, skills, ut_items, users, zones |
| `location_effects/` | per-location particle / overlay effect defs  |
| `locations/`        | per-location texture packs (8 dirs)          |
| `textures/`         | shared texture atlas library (15 dirs)       |

`textures/` subdirs: `screens`, `fight`, `sliders`, `fullscreen`,
`panels`, `raids`, `logos`, `hints`, `scrolls`, `effects`, `misc`,
`joystick`, `buttons`.

## `lib/` layout

Single ABI: `armeabi-v7a`. 30 `.so` files. See `02_native_libraries.md`.

## `META-INF/`

```
META-INF/
├── MANIFEST.MF                       # 229 885 B  per-file SHA digests
├── CERT.SF                           # 230 006 B  signature file
├── CERT.RSA                          #   1 323 B  RSA signature
├── services/
│   └── javax.annotation.processing.Processor  # 128 B
└── maven/
    ├── com.google.guava/guava/
    ├── com.nineoldandroids/library/
    ├── com.squareup/javapoet/
    ├── com.squareup.dagger/dagger/
    ├── org.javassist/javassist/
    └── org.reflections/reflections/
```

The Maven metadata reveals the **build-time** Java dependencies (used by
the annotation processor that generates the Fyber mediation layer, not
runtime). For reSF2, only the runtime dependencies matter; these can be
ignored.

## `res/` (Android resources, 243 files)

Standard Android resource tree. Mostly `drawable-*` (icons for different
densities), `layout` ( Tapjoy / Chartboost / AppLovin activities),
`values` (strings for app_name, facebook_app_id, google_play_services_version),
`xml` (file paths, device profiles). reSF2 does not need any of these.

## `AndroidManifest.xml` (decoded)

Highlights — see `03_java_layer.md` for the full package breakdown.

```xml
<manifest package="com.nekki.shadowfight"
          platformBuildVersionCode="23"
          platformBuildVersionName="6.0-2166767">
  <application
      android:name="android.support.multidex.MultiDexApplication"
      android:theme="@android:style/Theme.Holo.NoActionBar.Fullscreen">
    <activity android:name=".Main" android:launchMode="singleTask">
      <intent-filter>
        <action android:name="android.intent.action.MAIN"/>
        <category android:name="android.intent.category.LAUNCHER"/>
        <category android:name="tv.ouya.intent.category.GAME"/>
      </intent-filter>
    </activity>
    <provider
        android:authorities="zzzz768b4dcde01d5dbb117274855b95a3a8.VFSProvider"
        android:name="com.ideaworks3d.marmalade.VFSProvider"
        android:exported="false" android:multiprocess="true"/>
    <!-- ~30 ad SDK activities, GCM receivers, OBB downloader service -->
  </application>
  <uses-permission android:name="android.permission.INTERNET"/>
  <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
  <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE"/>
  <uses-permission android:name="android.permission.WAKE_LOCK"/>
  <uses-permission android:name="android.permission.VIBRATE"/>
  <uses-permission android:name="android.permission.GET_ACCOUNTS"/>
  <uses-permission android:name="android.permission.ACCESS_WIFI_STATE"/>
  <uses-permission android:name="android.permission.SET_ALARM"/>
  <uses-permission android:name="android.permission.MODIFY_AUDIO_SETTINGS"/>
  <uses-permission android:name="android.permission.SET_ORIENTATION"/>
  <uses-permission android:name="com.android.vending.CHECK_LICENSE"/>
  <uses-permission android:name="com.android.vending.BILLING"/>
  <uses-permission android:name="com.google.android.c2dm.permission.RECEIVE"/>
  <!-- ...and a custom signature-level C2D_MESSAGE permission -->
</manifest>
```

Notable:
- `MultiDexApplication` → 2 DEX files (`classes.dex` + `classes2.dex`).
- `VFSProvider` is Marmalade's Virtual File System content provider —
  it exposes the APK's `assets/` directory to native code via a
  file-descriptor bridge. reSF2 must replicate this or use plain file
  I/O on extracted assets.
- `tv.ouya.intent.category.GAME` — the APK was originally targeted at
  the OUYA console too.
- `INSTALL_REFERRER` is claimed by 4 different receivers (AppsFlyer,
  Tapjoy, Flurry, Marmalade's own) — typical of an ad-monetised game
  of that era.

## `apktool.yml` summary

```yaml
version: 2.9.3
apkFileName: sf2.apk
isFrameworkApk: false
sdkInfo:
  minSdkVersion: 11        # Android 3.0 (Honeycomb)
  targetSdkVersion: 23     # Android 6.0 (Marshmallow)
packageInfo:
  forcedPackageId: 127
versionInfo:
  versionCode: 1000086
  versionName: 1.9.21
```

`forcedPackageId: 127` is the conventional value Marmalade uses for its
deployed APKs — non-Marmalade apps normally use `0x7f` (127) too, so
this is not a unique marker, but it confirms the apktool decode is
correct.

## What is **not** in the APK

- **No shaders** in `assets/` or `res/`. Marmalade ships its GLSL shaders
  inside the `.s3e` binary (compiled into the IwGx shader program cache).
  Stage 2 will dump them from the `.s3e` `.rodata` section.
- **No JSON scene files** for gameplay — only for CocoGUI raids UI. The
  gameplay scenes are described by the XMLs listed in `settings.xml`
  (achievements, quests, perks, models, etc.) and the `.atf` tactics
  blobs.
- **No `.obj` / `.glb` / `.fbx`**. All fighter meshes are 2D sprite
  atlases + skeletal animation; no 3D model files exist.
- **No Cocos2d-x runtime** in Java or native form. The CocoGUI JSON
  files in `cocoGUI/raids/` are loaded by a Cocos2d-style UI reader
  that the game implements internally (not the full Cocos2d-x lib).
- **No Kotlin**. The APK predates Kotlin-first Android.

## Implications for reSF2 Stage 2

1. The first C++ target should be a **read-only `.s3e` parser** that
   returns the section table + symbol table. This unlocks everything
   else (code dump, shader dump, string dump, resource dump).
2. The `.dz` (DTRZ) archive format must be reverse-engineered next —
   it contains `files_list.xml` (the canonical asset index used at
   runtime, distinct from `settings.xml` which is the XML-only index)
   and `animations.dz` (the animation data).
3. The `.atf` (zlib-compressed custom) format is needed before Stage 4
   (battle logic), since each `.atf` describes one weapon-pair's
   combat tactics.

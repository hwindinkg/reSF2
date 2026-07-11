# 03 — Java layer

> Source: `classes.dex` (8 607 228 B, 8 340 classes, 55 958 strings) +
> `classes2.dex` (2 289 788 B, 1 960 classes, 16 754 strings) +
> `AndroidManifest.xml`.

## Method

DEX files were inspected by **string extraction only**. No method bodies
were decompiled — this preserves the clean-room property of reSF2. Class
and package **names** are not copyrightable and can be cited.

For each DEX, all strings matching the JNI class-path pattern
`L<pkg>/<pkg>/.../<Name>;` were extracted via `strings(1)` and aggregated.

## DEX statistics

| DEX            | Classes | Types  | Strings | Notes |
| -------------- | ------: | -----: | ------: | ----- |
| `classes.dex`  | 8 340   | 10 627 | 55 958  | Main DEX |
| `classes2.dex` | 1 960   |  2 699 | 16 754  | MultiDex overflow |
| **Total**      | **10 300** | **13 326** | **72 712** | |

## Top-level Java package distribution (`classes.dex`)

| Package                       | Classes | Role |
| ----------------------------- | ------: | ---- |
| `com/flurry/*`                |     343 | Flurry analytics |
| `com/tapjoy/*`                |     316 | Tapjoy offerwall / video ads |
| `com/applovin/*`              |     172 | AppLovin interstitial ads |
| `com/fyber/*`                 |     145 | Fyber mediation (SponsorPay rebrand) |
| `com/vungle/*`                |     124 | Vungle video ads |
| `com/chartboost/*`            |      82 | ChartBoost interstitial ads |
| `com/inmobi/*`                |      73 | InMobi banner ads |
| `com/jirbo/*` (AdColony)      |      34 | AdColony video ads (jirbo is the legacy name) |
| `com/facebook/*`              |       9 | Facebook SDK (login, ads) |
| `com/google/*`                |       5 | Google Play Services (games, billing) |
| `com/ideaworks3d/marmalade/*` |   ~50   | Marmalade Java glue (see below) |
| `com/nekki/*`                 |       7 | Game-specific Java code (see below) |
| `com/marmalade/studio/android/gcm/*` | ~4 | Marmalade GCM helper |
| `com/hullabu/ac3/*`           |       4 | Hullabu OBB downloader |
| `com/tapjoy/*` (duplicates)   |  (see above) | — |
| `rrrrrr/rrrcrr` etc.          |      ~12 | Obfuscated ad-network helper classes |
| `javassist/tools/*`           |       1 (`classes2.dex`) | Javassist runtime (used by Fyber) |

## `com/nekki/*` — the actual game Java code

Only 7 classes. This is **all** of the game-specific Java:

| Class                                       | Purpose |
| ------------------------------------------- | ------- |
| `com.nekki.shadowfight.Main`                | Entry activity (LAUNCHER). Subclass of `android.app.Activity`. Boots Marmalade via `LoaderThread`. |
| `com.nekki.sf2.GoogleActivity`              | Google Play Games Services sign-in flow. |
| `com.nekki.sf2.ObbGui`                      | OBB download UI (only used in `app_android_obb.icf` build). |
| `com.nekki.sf2.s3eObbGui`                   | JNI bridge for `ObbGui` (paired with `libs3eObbGui.so`). |
| `com.nekki.gpgs.GameHelper`                 | Google Play Games connection helper (port of BaseGameUtils). |
| `com.nekki.gpgs.GameHelper$1`               | Anonymous inner class (callback). |
| `com.nekki.ads.s3eSponsorPay`               | SponsorPay / Fyber offerwall bridge (paired with `libs3eSponsorPay.so`). |
| `com.nekki.ads.s3eSponsorPay$1`             | Anonymous inner class. |

**Total game-specific Java footprint: ~7 classes, ~2-4 KB of code.**
Everything else in the DEX is third-party SDK.

## `com/ideaworks3d/marmalade/*` — Marmalade Java glue

The full Marmalade Java layer was not enumerated exhaustively (would
require decompiling), but the following class names are visible in the
`strings` output of `libs3e_android.so` and `classes.dex`:

| Class                                                | Purpose |
| ---------------------------------------------------- | ------- |
| `com.ideaworks3d.marmalade.LoaderThread`             | Main Marmalade loader thread (owns the GL context + native runtime). |
| `com.ideaworks3d.marmalade.LoaderThread$MediaPlayerManager` | Manages `MediaPlayer` instances for video playback. |
| `com.ideaworks3d.marmalade.LoaderView`               | GLSurfaceView subclass hosting the Marmalade renderer. |
| `com.ideaworks3d.marmalade.LoaderKeyboard`           | Soft-keyboard handler (text input). |
| `com.ideaworks3d.marmalade.VFSProvider`              | ContentProvider exposing `assets/` to native code. |
| `com.ideaworks3d.marmalade.s3eAndroidGooglePlayBilling.*` | IAP v3 bridge. |
| `com.ideaworks3d.marmalade.s3eApkExpansionFile.*`    | OBB expansion-file downloader. |
| `com.ideaworks3d.marmalade.s3eGCMClient*`            | GCM push-notification client (deprecated, replaced by FCM in 2018). |

## Other Marmalade-side helpers

| Class                                                | Purpose |
| ---------------------------------------------------- | ------- |
| `com.marmalade.studio.android.gcm.s3eGCMClientLocalReceiver` | Local broadcast receiver (runs in `:remote` process). |
| `com.marmalade.studio.android.gcm.s3eGCMClientBroadcastReceiver` | GCM broadcast receiver (permission-protected). |

## Manifest-declared components (Java side)

### Activities (28 total, including the entry activity)

| Activity                                            | From SDK |
| --------------------------------------------------- | -------- |
| `com.nekki.shadowfight.Main`                        | Game |
| `com.tapjoy.TJAdUnitActivity` (×3)                  | Tapjoy |
| `com.tapjoy.TJContentActivity` (×2)                 | Tapjoy |
| `com.tapjoy.mraid.view.ActionHandler` (×3)          | Tapjoy |
| `com.tapjoy.mraid.view.Browser` (×3)                | Tapjoy |
| `com.chartboost.sdk.CBImpressionActivity` (×3)      | ChartBoost |
| `com.flurry.android.CatalogActivity`                | Flurry |
| `com.flurry.android.FlurryFullscreenTakeoverActivity` | Flurry |
| `com.facebook.LoginActivity`                        | Facebook |
| `com.facebook.ads.InterstitialAdActivity`           | Facebook Audience Network |
| `com.applovin.adview.AppLovinInterstitialActivity`  | AppLovin |
| `com.applovin.adview.AppLovinConfirmationActivity`  | AppLovin |
| `com.fyber.ads.videos.RewardedVideoActivity`        | Fyber |
| `com.fyber.ads.interstitials.InterstitialActivity`  | Fyber |
| `com.fyber.mediation.adcolony.rv.VideoProxyActivity` | Fyber (AdColony mediation) |
| `com.fyber.mediation.adcolony.interstitial.InterstitialProxyActivity` | Fyber |
| `com.jirbo.adcolony.AdColonyOverlay`                | AdColony |
| `com.jirbo.adcolony.AdColonyFullscreen`             | AdColony |
| `com.jirbo.adcolony.AdColonyBrowser`                | AdColony |
| `com.unity3d.ads.android.view.UnityAdsFullscreenActivity` | Unity Ads |
| `com.vungle.publisher.FullScreenAdActivity`         | Vungle |
| `com.inmobi.rendering.InMobiAdActivity`             | InMobi |
| `com.ideaworks3d.marmalade.s3eAndroidGooglePlayBilling.PurchaseProxy` | Marmalade IAP |

### Services (4)

| Service                                                    | From SDK |
| ---------------------------------------------------------- | -------- |
| `com.hullabu.ac3.HullabuDownloaderService`                 | Hullabu (OBB downloader) |
| `com.ideaworks3d.marmalade.s3eApkExpansionFile.MyDownloaderService` | Marmalade OBB downloader |
| `com.fyber.cache.CacheVideoDownloadService`                | Fyber (pre-caches rewarded video) |

### Broadcast receivers (8)

| Receiver                                                    | Purpose |
| ----------------------------------------------------------- | ------- |
| `com.appsflyer.MultipleInstallBroadcastReceiver`            | AppsFlyer install attribution |
| `com.appsflyer.extension.s3eAppsFlyerInstallReceiver.s3eAppsFlyerInstallReceiver` | AppsFlyer Marmalade bridge |
| `com.tapjoy.InstallReferrerReceiver`                        | Tapjoy install referrer |
| `com.tapjoy.GCMReceiver`                                    | Tapjoy GCM |
| `com.tapjoy.TapjoyReceiver`                                 | Tapjoy general |
| `com.hullabu.ac3.HullabuAlarmReceiver`                      | Hullabu scheduled alarms |
| `com.hullabu.ac3.TimeAlarm`                                 | Hullabu timed notifications |
| `com.nekki.androidnotifications.TimeAlarm`                  | Local notifications (Nekki) |
| `com.inmobi.commons.core.utilities.uid.ImIdShareBroadCastReceiver` | InMobi ID sharing |
| `com.marmalade.studio.android.gcm.s3eGCMClientLocalReceiver` | Marmalade GCM (runs in `:remote` process) |
| `com.marmalade.studio.android.gcm.s3eGCMClientBroadcastReceiver` | Marmalade GCM (permission-protected) |

### Content providers (1)

| Provider                                                    | Authority |
| ----------------------------------------------------------- | --------- |
| `com.ideaworks3d.marmalade.VFSProvider`                     | `zzzz768b4dcde01d5dbb117274855b95a3a8.VFSProvider` |

The `zzzz...` prefix is Marmalade's auto-generated authority name
(derived from the APK signing certificate hash). reSF2 does not need
this provider — we use plain file I/O on extracted assets.

## Permissions (deduplicated)

| Permission                                    | Used by |
| --------------------------------------------- | ------- |
| `INTERNET`                                    | All network SDKs + multiplayer |
| `ACCESS_NETWORK_STATE`                        | All SDKs |
| `ACCESS_WIFI_STATE`                           | AppsFlyer, Vungle, InMobi |
| `WRITE_EXTERNAL_STORAGE`                      | Tapjoy, Fyber, ChartBoost, OBB |
| `WAKE_LOCK`                                   | Tapjoy, Fyber, video playback |
| `VIBRATE`                                     | Haptics, notifications |
| `MODIFY_AUDIO_SETTINGS`                       | Marmalade audio |
| `SET_ORIENTATION`                             | Marmalade orientation lock |
| `GET_ACCOUNTS`                                | GCM, Google Play Services |
| `SET_ALARM`                                   | Local notifications |
| `CHECK_LICENSE`                               | Google Play Licensing (anti-piracy) |
| `BILLING`                                     | Google Play IAP |
| `RECEIVE` (C2DM)                              | GCM push |
| `com.nekki.shadowfight.permission.C2D_MESSAGE` | Custom signature-level GCM permission |

## `<meta-data>` keys

| Key                                          | Value |
| -------------------------------------------- | ----- |
| `applovin.sdk.key`                           | SDK key (recorded but redacted here) |
| `com.google.android.gms.games.APP_ID`        | `@string/app_id` |
| `com.google.android.gms.version`             | `@integer/google_play_services_version` |
| `com.google.android.gms.appstate.APP_ID`     | `@string/app_id` |
| `com.facebook.sdk.ApplicationId`             | `@string/facebook_app_id` |

The actual SDK keys / app IDs live in `res/values/strings.xml` and are
not reproduced here.

## Build dependencies (from `META-INF/maven/`)

The Maven `.pom` files reveal that the **build-time** Java dependencies
include:

- `com.google.guava:guava`
- `com.nineoldandroids:library` (animation backport)
- `com.squareup:javapoet` (code generation)
- `com.squareup.dagger:dagger` (DI, pre-Dagger 2)
- `org.javassist:javassist` (bytecode manipulation)
- `org.reflections:reflections` (classpath scanner)

These are **build-time only** — the Fyber mediation layer generates
adapter classes at build time using Javassist + Reflections + JavaPoet.
None of them run inside the APK at runtime (except Javassist, which is
used by Fyber at runtime to dynamically patch ad SDK classes).

reSF2 does not need any of these.

## What this means for reSF2

The Java layer is **a thin shell**. For the reSF2 port we have two
options:

1. **Skip Java entirely** on non-Android platforms. On Linux / Windows /
   macOS / Steam Deck, the engine boots directly from `main()` and loads
   the `.s3e` + assets from a user-supplied directory.
2. **Minimal Android wrapper** for the Android target: a single
   `NativeActivity`-based entry point that owns a GL context and a
   filesystem bridge. No ad SDKs, no IAP (initially), no analytics.

In other words: of the 10 300 Java classes in the original APK, reSF2
needs **zero** of them for gameplay. The Java layer is pure overhead
from the original ad-monetised distribution model.

## What we **did not** recover

- Java method bodies (deliberately not decompiled — clean-room).
- The exact `RegisterNatives()` table from `JNI_OnLoad` — Stage 2 task.
- The set of `native` methods declared on `LoaderThread` / `LoaderView`
  (would require a `baksmali` pass; deferred to Stage 2 when we need to
  write the Android-side wrapper).
- The intent-filter / deep-link set beyond what's in the manifest above.

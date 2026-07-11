# 02 — Native libraries (`lib/armeabi-v7a/`)

> Source: 30 ELF shared objects, all `armeabi-v7a` (ARMv7-A, Thumb-2, EABI5).
> All stripped. Total: 6 431 992 bytes.

## Overview by role

| Role              | Libraries | Bytes     | Notes |
| ----------------- | ---------:| ---------:| ----- |
| Marmalade runtime | 1         |  800 148  | `libs3e_android.so` — the only required native lib at boot |
| Multiplayer       | 2         | 2 621 968 | `libsmartfox.so` + `libs3eSmartFox.so` (SmartFoxServer 2X) |
| Video             | 5         | 1 510 232 | FFmpeg 2.x family + Marmalade wrapper |
| Haptics           | 1         |   236 920 | Immersion haptic feedback (`libImmEndpointWarpJ.so`) |
| Input             | 2         |    84 688 | Gamepad + key-listener extensions |
| Platform / OS     | 9         |   497 548 | OBB, GCM, billing, notifications, dialog, permissions, etc. |
| Ads / Analytics   | 9         |   461 452 | AdColony, AppLovin, ChartBoost, Tapjoy, SponsorPay, AppsFlyer, Facebook, AmazonIAP, Nekki extension |
| Misc              | 1         |    18 844 | `libs3eDeviceUniqueID.so`, `libs3eGetSimOperator.so` |
| **Total**         | **30**    | **6 431 992** | |

## Native library inventory

| Library                          | Size (B) | NEEDED                                                                 | Role |
| -------------------------------- | -------: | ---------------------------------------------------------------------- | ---- |
| `libs3e_android.so`              |  800 148 | `libz.so`, `libdl.so`, `liblog.so`, `libc.so`                          | Marmalade runtime — boots the `.s3e` binary, hosts `JNI_OnLoad` |
| `libsmartfox.so`                 | 2 143 608 | `libz.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so`             | SmartFoxServer 2X C++ API (boost::asio) |
| `libs3eSmartFox.so`              |  480 792 | `libs3e_android.so`, `libsmartfox.so`, `liblog.so`, `libstdc++.so`, ... | Marmalade glue around SmartFox |
| `libavcodec-55.so`               | 1 016 248 | `libdl.so`, `libavutil-52.so`, `libm.so`, `libc.so`                     | FFmpeg libavcodec (major 55 = FFmpeg 1.2 / 2.x) |
| `libavformat-55.so`              |   214 968 | `libdl.so`, `libavcodec-55.so`, `libavutil-52.so`, `libm.so`, `libz.so`, `libc.so` | FFmpeg libavformat |
| `libavutil-52.so`                |   177 700 | `libdl.so`, `libm.so`, `libc.so`                                       | FFmpeg libavutil |
| `libswscale-2.so`                |   275 868 | `libdl.so`, `libavutil-52.so`, `libm.so`, `libc.so`                    | FFmpeg libswscale |
| `libs3eFfmpeg.so`                |    71 604 | `libs3e_android.so`, `libavutil-52.so`, `libswscale-2.so`, `libavcodec-55.so`, `libavformat-55.so`, `libc.so` | Marmalade glue around FFmpeg |
| `libImmEndpointWarpJ.so`         |   236 920 | `libdl.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`          | Immersion haptic feedback (only lib with `interpreter /system/bin/linker`) |
| `libgamepad.so`                  |     9 216 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | Gamepad input extension |
| `libInputDeviceExtension.so`     |    75 472 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | Generic input device extension (OUYA, Moga, etc.) |
| `libs3eAdColony.so`              |   107 928 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | AdColony video ads |
| `libs3eAndroidGooglePlayBilling.so` |  79 152 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | Google Play IAP v3 |
| `libs3eAndroidNotifications.so`  |     5 120 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | Local notifications |
| `libs3eApkExpansionFile.so`      |    75 056 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | OBB expansion file downloader |
| `libs3eAppsFlyer.so`             |     9 460 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | AppsFlyer attribution |
| `libs3eChartBoost.so`            |    83 340 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | ChartBoost interstitial/video ads |
| `libs3eDeviceUniqueID.so`        |     5 204 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | Device-unique ID retrieval |
| `libs3eDialog.so`                |     5 192 | `libs3e_android.so`, `libc.so`                                          | Native dialog wrapper |
| `libs3eFacebook.so`              |    25 740 | `libs3e_android.so`, `libc.so`                                          | Facebook SDK bridge |
| `libs3eGCMClient.so`             |     9 348 | `libs3e_android.so`, `libc.so`                                          | Google Cloud Messaging client |
| `libs3eGetSimOperator.so`        |     5 316 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | SIM operator retrieval |
| `libs3eGPGS.so`                  |    75 060 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | Google Play Games Services |
| `libs3eKeyListener.so`           |     5 260 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | Hardware key listener |
| `libs3eNekkiExtension.so`        |     5 204 | `libs3e_android.so`, `libc.so`                                          | Nekki-specific extension (5 KB stub) |
| `libs3eObbGui.so`                |     5 200 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | OBB download UI |
| `libs3ePermissions.so`           |   136 380 | `libs3e_android.so`, `libc.so`                                          | Android 6.0 runtime permissions |
| `libs3eSponsorPay.so`            |   112 028 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | SponsorPay (Fyber) offerwall |
| `libs3eTapjoy.so`                |   112 180 | `libs3e_android.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so` | Tapjoy offerwall |
| `libAmazonIAP.so`                |    67 280 | `libs3e_android.so`, `libc.so`                                          | Amazon Appstore IAP |

## Library categories — what reSF2 actually needs

For a single-player-only "boot and reach a fight" milestone, reSF2 only
needs to replicate:

- **`libs3e_android.so`** — the entire Marmalade runtime. **Required.**
- **`libavcodec-55.so` + `libavformat-55.so` + `libavutil-52.so` +
  `libswscale-2.so` + `libs3eFfmpeg.so`** — only needed to play the
  intro / cutscene videos. Can be stubbed for the first milestone.
- **`libs3eAndroidNotifications.so`**, **`libs3eDialog.so`**,
  **`libs3ePermissions.so`** — trivial platform wrappers, easy stubs.
- **`libs3eApkExpansionFile.so`** + **`libs3eObbGui.so`** — only needed
  for the OBB build variant. The APK we have is the non-OBB build.

For multiplayer (Stage 7.10):

- **`libsmartfox.so`** + **`libs3eSmartFox.so`** — must be replaced by
  a SmartFox2X-protocol-compatible client. The protocol is documented
  in the SmartFoxServer 2X public specs and observable in the demangled
  symbol table (see below).

For parity with the original monetisation / ads experience — **out of
scope** for reSF2. The ad / IAP / analytics extensions are not needed
for gameplay and will not be implemented.

## SmartFox2X C++ API surface (from demangled `libsmartfox.so`)

The `Sfs2X::*` namespace tree, recovered via `nm -D` + `c++filt`:

```
Sfs2X::Core
├── SFSIOHandler
├── IDispatchable
├── IPacketEncrypter
├── SFSProtocolCodec
├── DefaultPacketEncrypter
├── Sockets
│   ├── ISocketLayer
│   ├── TCPSocketLayer
│   ├── UDPSocketLayer
│   └── TCPClient
Sfs2X::Bitswarm
├── IController
├── IUDPManager
├── UDPManager
├── BaseController
├── BitSwarmClient
└── Message
Sfs2X::Controllers
├── SystemController
└── ExtensionController
Sfs2X::Util
├── LagMonitor
├── XMLNodeList
└── ConfigLoader
Sfs2X::Exceptions
├── SFSCodecError
├── SFSValidationError
└── SFSError
```

Plus `boost::asio` for TCP/UDP I/O, `boost::interprocess` for some
atomic counter, and `boost::shared_ptr` everywhere (this is pre-C++11
code, dated ~2013-2014 based on the API version).

Build path leaked into the binary:
```
/Users/stals/dev/git/SF2/libs/s3eSmartFox/SmartFox/../boost_android_include/boost/exception/detail/exception_ptr.hpp
```
— the developer's username was `stals`, project root was
`/Users/stals/dev/git/SF2/`. reSF2 does not need this; recorded for
completeness.

## `libs3e_android.so` — JNI surface

```
$ nm -D --defined-only libs3e_android.so | grep -E 'Java_|JNI_'
0003c770 T JNI_OnLoad
0003c438 T JNI_OnUnLoad
```

That's the entire JNI surface. **All Java↔native bindings are registered
dynamically via `RegisterNatives()` inside `JNI_OnLoad`.** Static
`Java_com_...` symbols are not used.

Strings of interest in `libs3e_android.so` (Marmalade version banner +
JNI class references):

```
Assertion Failure (Marmalade v8.2.1 [465988])
Error (Marmalade v8.2.1 [465988])
Application built for S3E version %d.%d, which is not compatible with current version %d.%d
Game S3E version %d.%d.%d is incompatible with loader S3E version %d.%d.%d
Game minor version number is greater
Incorrect signature in s3e file
Error loading s3e!
Error reading s3e file %s
Can't open s3e file %s
Invalid S3E file - incorrect data
Invalid S3E file - read failed
Internal out of memory in s3e loader heap [alloc size = %zu]
Insufficient memory to start application. Not enough memory to begin loading s3e file.
Lcom/ideaworks3d/marmalade/LoaderView;
Lcom/ideaworks3d/marmalade/LoaderThread$MediaPlayerManager;
Lcom/ideaworks3d/marmalade/LoaderKeyboard;
```

Implications:

1. The loader performs a **version check** against the S3E binary. reSF2
   will need to either provide a compatible version handshake or skip
   this check (clean-room: just don't implement the check).
2. The `.s3e` file is **signature-checked** at load time. reSF2 will not
   enforce this — we just parse the file.
3. The Java side that `JNI_OnLoad` touches is `LoaderThread`,
   `LoaderView`, `LoaderKeyboard`, `MediaPlayerManager`. This is the
   complete Java↔native boundary for the runtime itself; everything
   else is platform extensions.

## ELF notes

- All libraries are **ELF 32-bit LSB shared object, ARM, EABI5, version 1 (SYSV)**.
- All are **stripped** (no `.symtab`); we have to work from `.dynsym` only.
- Three libraries have `interpreter /system/bin/linker` set in their PT_INTERP
  (`libImmEndpointWarpJ.so`, `libavcodec-55.so`, `libavformat-55.so`,
  `libavutil-52.so`, `libswscale-2.so`, and a few others). This is unusual
  for a `.so` and suggests they were linked as executables by mistake or
  for testing. It does not affect functionality.
- All libraries that link against `libs3e_android.so` use it as their
  Marmalade runtime bridge; they call into the runtime via the s3e
  extension API (`s3eEdk*`).

## ABI compatibility note for reSF2

The original APK ships **only `armeabi-v7a`**. On a 64-bit ARM (arm64-v8a)
device, Android runs this in 32-bit compatibility mode. On x86_64 devices,
it runs under ARM-to-x86 translation (Houdini/libndk_translation).

reSF2's target ABIs:

| Platform        | Target ABI                 | Notes |
| --------------- | -------------------------- | ----- |
| Linux desktop   | `x86_64`, `aarch64`        | Native compile |
| Windows desktop | `x86_64`                   | MSVC + Clang |
| macOS           | `x86_64`, `arm64`          | Universal binary |
| Android         | `arm64-v8a`, `x86_64`      | Modern targets only; we skip 32-bit |
| Steam Deck      | `x86_64` (Linux)           | Same as Linux desktop |
| Switch (optional) | `aarch64`                | Homebrew toolchain |

No 32-bit Android targets. The reSF2 engine is new code, not a port of
the Marmalade binary, so there is no inheritance constraint from the
original ABI.

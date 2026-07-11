# Stage 1 — Executive summary

> Status: ✅ complete on 2026-07-09.
> Author: main agent.
> Source APK: `Shadow Fight 2 1.9.21` (`com.nekki.shadowfight`,
> versionCode `1000086`), build date 2016-06-09.

## TL;DR

Shadow Fight 2 v1.9.21 is built on the **Marmalade SDK v8.2.1** (formerly
Airplay SDK), shipped as a single-ABI `armeabi-v7a` Android package using
**OpenGL ES 2.0**. The Java side is a thin loader; **all game logic lives
in C++** inside the LZMA-compressed `assets/ShadowFight2.s3e` binary
(magic `XE3U`, 2 858 937 → 8 689 357 bytes when decompressed). The Java
layer's only job is to host `com.ideaworks3d.marmalade.LoaderThread` and
the platform-extension glue (ads, IAP, analytics, GCM, OBB).

## Key numbers

| Metric                                   | Value            |
| ---------------------------------------- | ---------------- |
| APK size                                 | 94 736 412 bytes |
| Uncompressed size                        | 105 182 063 bytes|
| Files inside APK                         | 2 181            |
| ABIs                                     | 1 (`armeabi-v7a`)|
| Native libraries                         | 30               |
| DEX files                                | 2                |
| Total Java classes (DEX count)           | ~10 300          |
| Asset files under `assets/`              | 1 866            |
| Asset bytes                              | 86 588 296       |
| OpenGL ES version                        | 2.0              |
| Orientation                              | Landscape (fixed)|
| minSdkVersion / targetSdkVersion         | 11 / 23          |
| Marmalade SDK version                    | 8.2.1 (build 465988) |

## Engine identity

Confirmed by a literal assertion string in `libs3e_android.so`:

> `Assertion Failure (Marmalade v8.2.1 [465988])`

This is the Marmalade **Android runtime** library. The full Marmalade SDK
stack (loader, s3e runtime, IwGeom, IwGx, IwUI, IwSound, IwResManager)
is bundled inside `libs3e_android.so` and the `.s3e` binary.

## Why this matters for reSF2

Because the game is **not** a typical Java/Cocos2d-x Android app, the
reimplementation strategy is:

1. **Write a portable C++20 runtime** that provides the Marmalade SDK
   surface area actually used by Shadow Fight 2 (s3e + IwGx + IwResManager
   + IwSound + IwUI + IwNuklear, etc.) — not the full SDK, just the
   subset the game touches.
2. **Load and execute** the original `ShadowFight2.s3e` binary — either
   by interpreting its section table and re-locating its ARM code into
   our address space (hard, only works on ARM hosts) or by treating the
   `.s3e` as an opaque data source and re-implementing the game logic
   in clean-room C++ that consumes the same asset files (easier, slower,
   the path reSF2 takes).
3. **Re-implement** the SmartFox2X client API (`libsmartfox.so`) for
   multiplayer, the FFmpeg wrapper (`libs3eFfmpeg.so`) for video, and
   the platform extensions only to the extent needed for the game to
   boot and reach a playable fight.

reSF2 will **not** ship any original binary; the user supplies their own
APK / `.s3e`. reSF2 only provides the runtime + asset loaders.

## What Stage 1 actually proved

- The `.s3e` container is **LZMA1 legacy** compressed (props byte `0x5d`,
  dict size 65 536, uncompressed size embedded in the header).
- After decompression, the payload begins with the four-byte magic
  `XE3U` (`58 45 33 55`), followed by a section table. The bytes
  `# This is the global system configuration file for Marmalade
  applications` appear at offset `0x7f`, and the string `.text` appears
  at offset `0x736c3a` — strong evidence the payload contains a
  standard Marmalade S3E binary with code, data and resource sections.
- `libs3e_android.so` exports only `JNI_OnLoad` and `JNI_OnUnLoad`. All
  Java↔native bindings are registered dynamically via `RegisterNatives()`
  in `JNI_OnLoad`. We must trace `JNI_OnLoad` to recover the binding
  table — that is Stage 2.
- Java side has only one entry point: `com.nekki.shadowfight.Main`
  (extends `Activity`). The rest of `com.nekki.*` is 6 small classes
  for OBB GUI, Google Play Games Services helper, and a SponsorPay
  wrapper. Everything else in the ~10 300-class DEX is third-party ad /
  IAP / analytics SDK.
- Asset format inventory is complete (see `04_assets_inventory.md` and
  `05_resource_formats.md`). All non-XML / non-image formats were at
  least partially decoded; full parsers are Stage 4..5.

## What Stage 1 did **not** do

- Did not decompile any Java method body (clean-room). Only class and
  package **names** were extracted from DEX strings.
- Did not disassemble the `.s3e` ARM code. That is Stage 2.
- Did not recover the JNI registration table. That is Stage 2.
- Did not write any engine code. Stage 2 introduces the first C++
  target (`libresf2_reverse` with the S3E container parser).
- Did not push to GitHub. The user-supplied PAT was exposed in chat
  and was not used. The user must revoke it and reconfigure auth.

## Pointer to next stage

See [`TODO.md`](../TODO.md) → **Stage 2 — `.s3e` binary & JNI map**, and
the per-topic Stage 1 docs in this directory (`01` through `08`).

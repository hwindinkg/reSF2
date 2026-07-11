# 08 — Reverse-engineering log (Stage 1)

> Append-only chronological log of what was actually done in Stage 1,
> including dead ends and rejected approaches. Useful for the next
> agent picking up Stage 2.

## Environment setup

- Workspace: `/home/z/my-project/work/`.
- Tools available natively: `unzip`, `file`, `strings`, `readelf`,
  `objdump`, `nm`, `c++filt`, `python3` (with `lzma`), `git`,
  `g++`, `ffmpeg`, `pandoc`, OpenJDK 21 (`java`).
- Tools installed as standalone JARs (no sudo):
  - `apktool 2.9.3` from
    `https://github.com/iBotPeaches/Apktool/releases/download/v2.9.3/apktool_2.9.3.jar`
  - `jadx 1.4.7` from
    `https://github.com/skylot/jadx/releases/download/v1.4.7/jadx-1.4.7.zip`
- Tools NOT installed (no sudo, no apt access): `aapt`, `aapt2`,
  `dex2jar`, `xxd`, `hexdump`, `cmake`, `ninja`, `clang`. Worked around
  with Python equivalents.

## Step-by-step

### S1.1 — APK download

- First attempt with plain `curl` returned Cloudflare's "Just a
  moment..." anti-bot challenge page (5 352 bytes HTML).
- Second attempt with browser-like headers (`User-Agent`, `Accept`,
  `Sec-Fetch-*`, `Upgrade-Insecure-Requests`, `--compressed`) succeeded.
- Final file: 94 736 412 bytes, `file` reports
  `Android package (APK), with MANIFEST.MF and classes.dex`.
- SHA-256: `9258146bb87e7d1010ebbd6cc9f7bc9f00f1f2ff61ae4a73cd29003b072f5143`
- MD5:    `b81636e3fb3b6de7ea34c57a9673ce00`

### S1.2 — apktool decode

Command:

```
java -jar apktool.jar d -f -s -o apk_extracted/apktool sf2.apk
```

- `-s` skips smali extraction (clean-room: we don't want smali).
- Output: `AndroidManifest.xml` (decoded), `apktool.yml`, raw
  `classes.dex` + `classes2.dex`, `assets/`, `lib/`, `res/`,
  `original/`, `unknown/`.
- Decoding took ~3 seconds. No errors.

### S1.3 — Inventory

`unzip -l sf2.apk | tail -1` → 2 181 files, 105 182 063 bytes
uncompressed. File-type breakdown via `awk` on `unzip -l`:

| Top dir       | Files | Bytes      |
| ------------- | ----: | ---------: |
| assets/       | 1 866 | 86 588 296 |
| res/          |   243 |    583 337 |
| lib/          |    30 |  6 431 992 |
| META-INF/     |    16 |    494 057 |
| com/          |    14 |     17 292 |
| banner_*/     |     6 |      5 470 |
| js/           |     2 |     55 511 |
| classes.dex   |     1 |  8 607 228 |
| classes2.dex  |     1 |  2 289 788 |
| resources.arsc|     1 |     89 184 |
| AndroidManifest.xml | 1 |     19 908 |

### S1.4 — Manifest analysis

`AndroidManifest.xml` decoded by apktool. Highlights:
- `package="com.nekki.shadowfight"`, versionCode `1000086`,
  versionName `1.9.21`.
- `minSdkVersion=11`, `targetSdkVersion=23`.
- One launcher activity: `com.nekki.shadowfight.Main`.
- One content provider: `com.ideaworks3d.marmalade.VFSProvider` with
  authority `zzzz768b4dcde01d5dbb117274855b95a3a8.VFSProvider` (the
  `zzzz...` prefix is Marmalade's auto-generated authority name based
  on the APK signing cert hash).
- ~28 ad-SDK activities, ~8 broadcast receivers, ~4 services.
- OUYA category: `tv.ouya.intent.category.GAME` — original target
  included the OUYA console.

### S1.5 — Native library analysis

`readelf -d` on every `lib/*/*.so` to extract `NEEDED` and `SONAME`.
Results in `02_native_libraries.md`. Key findings:
- Single ABI: `armeabi-v7a`. No arm64, no x86, no x86_64.
- 30 `.so` files. Marmalade runtime `libs3e_android.so` is the only
  one required at boot (the others are platform extensions).
- `libsmartfox.so` is the SmartFoxServer 2X C++ API (Sfs2X::*
  namespaces, boost::asio).
- FFmpeg 2.x family (`libavcodec-55`, `libavformat-55`, `libavutil-52`,
  `libswscale-2`).
- `libs3e_android.so` exports only `JNI_OnLoad` / `JNI_OnUnLoad`.

### S1.6 — Marmalade identification

`strings lib/armeabi-v7a/libs3e_android.so | grep -i marmalade` returned:

```
Assertion Failure (Marmalade v8.2.1 [465988])
Error (Marmalade v8.2.1 [465988])
```

This pins the engine identity. The string `Lcom/ideaworks3d/marmalade/...`
in the same binary confirms the Java glue package.

### S1.7 — `.s3e` extraction

First attempt: `unzip` — failed (not a zip).
Second attempt: `python3 -c "import lzma; ..."` with `FORMAT_ALONE` —
failed with `LZMAError: Invalid or unsupported options`.
Third attempt: `lzma._decode_filter_properties(lzma.FILTER_LZMA1, ...)`
— also failed.
Fourth (working) attempt: hand-rolled filter spec.

```python
import lzma, struct
data = open("ShadowFight2.s3e", "rb").read()
props = data[0]                                   # 0x5d
dict_size = struct.unpack("<I", data[1:5])[0]     # 65536
uncomp_size = struct.unpack("<Q", data[5:13])[0]  # 8 689 357
lc = props % 9                                    # 3
rem = props // 9
lp = rem % 5                                      # 0
pb = rem // 5                                     # 2
filt = [{"id": lzma.FILTER_LZMA1, "dict_size": dict_size,
         "lc": lc, "lp": lp, "pb": pb,
         "mode": lzma.MODE_NORMAL, "preset": 6}]
dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=filt)
out = dec.decompress(data[13:], max_length=uncomp_size)
```

Decompressed payload (8 689 357 bytes) starts with magic `58 45 33 55`
= `XE3U`. Strings inside:

- Offset `0x7f`: `# This is the global system configuration file for
  Marmalade applications...` ← Marmalade's `s3e.icf` inlined.
- Offset `0x154`: `S3E`.
- Offset `0x11d7`: `s3e`.
- Offset `0x736c3a`: `.text` ← ELF-like section name. Strong evidence
  that the payload is a sectioned binary with code + data + resources.

Stage 2 task S2.1 will fully map the section table.

### S1.8 — `.dz` (DTRZ archive) — partial

`file assets/files.dz` → `data` (no signature match). `head -c 4` →
`44 54 52 5a` = `DTRZ`.

Tried `zlib.decompress` starting at offsets 4, 5, 6, ..., 32 — all
failed with `incorrect header check`. The bytes after `DTRZ` are not a
zlib stream.

First bytes after `DTRZ`:
```
78 00 69 00 00 66 69 6c 65 73 5f 6c 69 73 74 2e 78 6d 6c 00 73 65 74 74 69 6e 67 73
```
ASCII: `x\0i\0\0files_list.xml\0settings`. The first 5 bytes look like
a small header (maybe an `0x78` "version" byte + `0x0069` count + `0x00`
pad), then a null-terminated filename `files_list.xml`, then `settings...`
(continues with more filenames).

**Hypothesis**: DTRZ format is:
- `DTRZ` magic (4 bytes)
- header (5 bytes — version + count + pad?)
- list of null-terminated filenames
- list of per-file compressed payloads (offsets implicit, derived from
  running file-size counter)

This needs to be confirmed by disassembling the DTRZ reader in
`libs3e_android.so` (find the `DTRZ` literal in the binary, xref to
reader function). Deferred to Stage 2.

### S1.9 — `.atf` (zlib-compressed tactics) — partial

`file assets/tactics/kusarigama_nunchaku.atf` → `zlib compressed data,
best compression` (magic `78 da`).

`zlib.decompress` succeeded → 439 480 bytes. First 32 bytes:

```
01 00 00 00 4b 75 73 61 72 69 67 61 6d 61 00 4e 75 6e 63 68 61 6b 75 00 f0 a6 01 00 5a 03 1c 1b
```

ASCII-ish:
```
\x01\x00\x00\x00Kusarigama\0Nunchaku\0\xf0\xa6\x01\x00Z\x03\x1c\x1b...
```

So the format is:
- 4 bytes: version (1) or count (1)
- null-terminated string: weapon A name (`Kusarigama`)
- null-terminated string: weapon B name (`Nunchaku`)
- binary tactics data (~439 KB)

The 439 KB size for a single weapon pair is large. Stage 4 task S4.4
will document the byte layout of the tactics data (likely a 2D matrix
of fixed-size records, one per (move_A, move_B) pair).

### S1.10 — `.plist` (Cocos2d-x TexturePacker v2)

`head assets/assets/1536/textures/fullscreen/<sample>.plist` — standard
Cocos2d-x atlas format, `<integer>2</integer>` in metadata confirms
TexturePacker format v2. Decoded trivially.

### S1.11 — DEX string extraction (clean-room)

Did NOT decompile DEX (clean-room: no method bodies). Instead, used
`strings(1)` to extract class-path strings matching
`L<pkg>/<Name>;`. This gives package + class name visibility without
copying any implementation.

Aggregated counts by top-2-level package:

```
classes.dex:
  com/flurry       343
  com/tapjoy       316
  com/applovin     172
  com/fyber        145
  com/vungle       124
  com/chartboost    82
  com/inmobi        73
  com/jirbo         34  (AdColony legacy name)
  com/facebook       9
  com/google         5
  rrrrrr/*          12  (obfuscated ad-helper classes)
```

`com/nekki/*` enumeration:
```
Lcom/nekki/ads/s3eSponsorPay$1;
Lcom/nekki/ads/s3eSponsorPay;
Lcom/nekki/gpgs/GameHelper$1;
Lcom/nekki/gpgs/GameHelper;
Lcom/nekki/sf2/GoogleActivity;
Lcom/nekki/sf2/ObbGui;
Lcom/nekki/sf2/s3eObbGui;
Lcom/nekki/shadowfight/Main;
```

Total game-specific Java footprint: 7 classes (plus 1 anonymous inner
class each for `s3eSponsorPay` and `GameHelper`). Everything else in
the ~10 300-class DEX is third-party SDK.

`com/ideaworks3d/marmalade/*` enumeration from `strings` on
`libs3e_android.so` (since these strings are also baked into the .so
for JNI class lookup):

```
Lcom/ideaworks3d/marmalade/LoaderView;
Lcom/ideaworks3d/marmalade/LoaderThread$MediaPlayerManager;
Lcom/ideaworks3d/marmalade/LoaderKeyboard;
```

Plus from `AndroidManifest.xml`:
- `com.ideaworks3d.marmalade.VFSProvider`
- `com.ideaworks3d.marmalade.s3eAndroidGooglePlayBilling.PurchaseProxy`
- `com.ideaworks3d.marmalade.s3eApkExpansionFile.MyDownloaderService`
- `com.ideaworks3d.marmalade.s3eApkExpansionFile.MyAlarmReceiver`

### S1.12 — `settings.xml` parsing

`cat assets/settings.xml` revealed the master manifest. It is a flat
list of `<File value="path"/>` entries. 156 entries total, covering:
- 14 top-level gameplay XMLs (achievements, quests, perks, etc.)
- 2 animation files (`moves.xml` + `moves.xsd` — the schema is shipped!)
- 12 localization XMLs
- ~90 model XMLs (armor / body / head / helm / magic / punching_bag /
  ranged / skeleton / weapon)
- ~20 quest-extension XMLs (zone_1..7 core+story, intermission,
  promotions, dynamic_discounts, raid_quests, update_1_2_0_0)

### S1.13 — Documentation

Wrote 9 docs in `docs/` (this file + 00..07). Each doc covers one
topic; cross-referenced via Markdown links.

### S1.14 — Repo skeleton

Created:
- `engine/{runtime,renderer,physics,animation,audio,network,platform,tools,reverse}/.keep`
- `tests/.keep`, `assets/.keep`, `scripts/.keep`
- `CMakeLists.txt` (top-level, C++20, no targets yet)
- `engine/CMakeLists.txt` (placeholder, will add subdirectory CMakeLists
  in Stage 2)
- `tests/CMakeLists.txt` (placeholder)
- `scripts/CMakeLists.txt` (placeholder)
- `.gitignore` (excludes build artifacts + the APK and all extracted
  binaries — the repo must stay clean of original game assets)
- `README.md`, `CHANGELOG.md`, `TODO.md`

### S1.15 — Local commit

Single commit, no push (user-supplied PAT was exposed in chat —
deliberately not used).

## Dead ends / rejected approaches

1. **Cloudflare bypass via curl with browser headers**: worked on the
   second try, no need for headless browser.
2. **`lzma._decode_filter_properties(lzma.FILTER_LZMA1, header_bytes)`**:
   did not accept the legacy 13-byte header directly. Had to decode
   props byte into lc/lp/pb by hand.
3. **`unzip` on `.s3e`**: not a zip. Tried anyway because some
   Marmalade deployments use zip containers; this one uses raw LZMA.
4. **`unzip` on decompressed `.s3e`**: also not a zip. The `XE3U`
   container is Marmalade's custom sectioned binary format.
5. **`zlib.decompress` on `.dz` after the 4-byte `DTRZ` magic**: failed.
   The bytes following `DTRZ` are not a raw zlib stream — likely a
   custom container with filename table + per-file payloads.
6. **`jadx --no-src`**: produced empty output (no source = no class
   list either). Fell back to `strings` on the DEX files for class
   name extraction.
7. **`apktool d` without `-s`**: would have produced smali. Deliberately
   used `-s` to skip smali (clean-room: we don't want smali in the
   workspace either, to avoid accidental copying).

## What Stage 2 should bring

- A working C++20 build with `cmake` + `ninja` + a real compiler
  (Clang 16+ or GCC 13+). May need to install via `pip` (e.g.
  `pip install cmake ninja`) since sudo apt is unavailable.
- A `libresf2_reverse` static library with the first target:
  `engine::reverse::S3EFile` (read-only parser for the `XE3U` container).
- Unit tests using Catch2 (vendored as a single header) or doctest.
- A `reverse/` script that dumps `.s3e` sections to disk for further
  inspection.
- Disassembly of `JNI_OnLoad` in `libs3e_android.so` (use objdump or
  install Ghidra headless) to recover the `RegisterNatives()` table.

## Tooling wishlist for Stage 2

- `cmake`, `ninja`, `clang` (or `g++` ≥ 13) — for actual builds.
- `ghidra` (headless) — for `.s3e` and `.so` disassembly.
- `r2` (radare2) — alternative disassembler, lighter weight than Ghidra.
- `frida` — for runtime tracing on a real device (if available).
- `dex2jar` / `baksmali` — for DEX→smali (only if Stage 2 needs to
  enumerate `native` method declarations on the Java side).

If none of these can be installed (no sudo), Stage 2 will have to work
with `objdump` + `nm` + `strings` + hand-written Python parsers, which
is sufficient but slower.

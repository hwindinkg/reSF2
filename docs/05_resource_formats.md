# 05 — Resource formats

> Stage 1 findings. Each format is documented to the level needed for
> Stage 1 (identification + high-level structure). Full byte-level specs
> land in Stage 4..5 along with the parsers.

## Format inventory

| Format   | Magic / signature         | Compression    | Status |
| -------- | ------------------------- | -------------- | ------ |
| `.s3e`   | LZMA1 stream → `XE3U`     | LZMA1 legacy   | Magic + first 32 bytes decoded |
| `.dz`    | `DTRZ`                    | Custom?        | Magic known, full unpack pending |
| `.atf`   | `78 da` (zlib best-comp)  | zlib deflate   | Decompresses to custom tactics blob |
| `.plist` | `<?xml ...><plist ...>`   | none (text)    | Fully decoded (Cocos2d-x TexturePacker v2) |
| `.fnt`   | `info face=...` (text XML) | none (text)    | Standard Cocos2d-x bitmap font |
| `.json`  | `{"widgetTree":...}`      | none (text)    | Standard Cocos2d-x CocoGUI scene |
| `.xml`   | `<?xml ...>`              | none (text)    | Per-file schemas (Stage 4) |
| `.icf`   | `[MYSETTINGS]\n...`       | none (text)    | Marmalade INI-style config |
| `.png`/`.PNG` | `89 50 4E 47`         | PNG ( deflate)| Standard |
| `.jpg`   | `FF D8 FF`                | JPEG           | Standard |
| `.wav`   | `RIFF....WAVEfmt `        | PCM            | Standard |
| `.mp3`   | `FF FB` / `ID3`           | MPEG-1 L3      | Standard |
| `.mp4`   | `....ftyp`                | H.264/AAC      | Standard |
| `.ttf`   | `00 01 00 00`             | —              | Standard TrueType |

## `.s3e` — Marmalade S3E binary container

### Outer wrapper: LZMA1 legacy

```
Offset  Size  Field              Value (observed)
0       1     props byte         0x5d  → lc=3, lp=0, pb=2
1       4     dict_size (LE)     0x00010000  = 65536
5       8     uncomp_size (LE)   0x0000000000848c4d = 8 689 357
13      ...   LZMA1 stream       (decompress with FORMAT_RAW + FILTER_LZMA1)
```

Decompression recipe (Python):

```python
import lzma, struct
data = open("ShadowFight2.s3e", "rb").read()
props = data[0]
dict_size = struct.unpack("<I", data[1:5])[0]
uncomp_size = struct.unpack("<Q", data[5:13])[0]
lc = props % 9
rem = props // 9
lp = rem % 5
pb = rem // 5
filt = [{"id": lzma.FILTER_LZMA1, "dict_size": dict_size,
         "lc": lc, "lp": lp, "pb": pb,
         "mode": lzma.MODE_NORMAL, "preset": 6}]
dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=filt)
out = dec.decompress(data[13:], max_length=uncomp_size)
```

### Inner payload: `XE3U` magic

After LZMA decompression (size = 8 689 357 bytes):

```
Offset  Size  Field                Value (observed)
0       4     magic                58 45 33 55  = "XE3U"
4       4     ?                    0x00042800  (272 384 — possibly .text offset?)
8       4     ?                    0x010c000a
0x0c    4     ?                    0x00001521  (5409 — possibly string-count?)
0x10    4     ?                    0x00043d30
0x14    4     ?                    0x00045251
0x18    4     ?                    0x008042c8
0x1c    4     ?                    0x00825d5c
0x20    ...   ...                  (more u32s — section table?)
0x4c    4     ?                    0x0000004c  (76 — count?)
0x50    4     ?                    0x000014d5
0x54    4     ?                    0x00000000
0x58    4     ?                    0x009aa595 (?)
0x5c    4     ?                    0x00000000
0x60    4     ?                    0x00000128
0x64    4     ?                    0x0000000c
0x68    ...   ...                  ...
0x7f    ...   text                 "# This is the global system configuration file
                                      for Marmalade applications..."
0x154   ...   text                 "S3E" (string)
0x11d7  ...   text                 "s3e"
0x736c3a ...  text                 ".text"  ← section name (suggests ELF-like
                                              section table embedded)
```

**Hypothesis** (confidence: medium):
The `XE3U` payload is a Marmalade-specific ELF-like container with a
custom section table at the start, followed by sections including:
- An embedded Marmalade config text (the `.icf` content, inlined)
- A `.text` section containing ARM Thumb-2 code
- `.rodata`, `.data`, `.bss` (not yet confirmed by string search but
  expected)
- A resource section (IwResManager binary group)
- A shader section (IwGx GLSL programs)

Stage 2 will document the exact section table layout by:
1. Disassembling `JNI_OnLoad` in `libs3e_android.so` to find the loader
   function that parses `XE3U`.
2. Single-stepping the loader on a real device under `gdbserver` to
   observe section enumeration.

### Strings of interest in the decompressed payload

```
# This is the global system configuration file for Marmalade applications.
# This file is automatically included in all application deployments and
# used when debugging an x86 bui...
```

This is Marmalade's `s3e.icf` — the global default config. It is
prepended to the per-app `.icf` files at runtime.

## `.dz` — DTRZ archive format

Two `.dz` files exist:
- `assets/assets/files.dz` — file manifest (`files_list.xml` + others)
- `assets/assets/animations.dz` — animation data

### Observed header

```
Offset  Size  Field                Value
0       4     magic                44 54 52 5a  = "DTRZ"
4       ?     ?                    78 00 69 00 00 66 69 6c 65 73 5f 6c 69 73 74 2e 78 6d 6c 00 73 65 74 74 69 6e 67 73 ...
                                   ↑ seems to start with "files_list.xml\0settings..." as text
```

**Hypothesis** (confidence: low):
After the 4-byte `DTRZ` magic, the format may store an inline file
manifest as null-terminated UTF-8 strings, followed by per-file
compressed payloads. The bytes I previously interpreted as `78 00 69
00` may be UTF-16LE ("x" + null + "i" + null) or may just be the start
of a filename table.

**Data needed to confirm:**
- Full hex dump of the first 256 bytes of both `.dz` files.
- A reference implementation: search `libsmartfox.so` or `libs3e_android.so`
  for the string `DTRZ` to find the reader code, then disassemble.
- Alternatively, run the game under `frida` and hook the `.dz` reader.

Stage 2 task S2.x will resolve this.

## `.atf` — zlib-compressed custom blob (NOT Adobe Texture Format)

110 files under `assets/assets/tactics/`. Despite the `.atf` extension,
these are **not** Adobe Texture Format files (which have magic `ATF`).
They are plain zlib streams.

### Outer wrapper: zlib deflate (best compression)

```
$ file kusarigama_nunchaku.atf
kusarigama_nunchaku.atf: zlib compressed data, best compression

$ python3 -c "import zlib; d=open('kusarigama_nunchaku.atf','rb').read(); \
              o=zlib.decompress(d); print(len(o), o[:32].hex())"
439480 010000004b757361726967616d61004e756e6368616b7500f0a601005a031c1b
```

### Decompressed payload

```
Offset  Size  Field                Value
0       4     magic / version?     01 00 00 00  (= 1, little-endian)
4       var   weapon A name        "Kusarigama\0"  (null-terminated ASCII)
15      var   weapon B name        "Nunchaku\0"
24      ...   tactics data         f0 a6 01 00 5a 03 1c 1b ... (binary)
```

Total decompressed size of `kusarigama_nunchaku.atf`: 439 480 bytes.
That's surprisingly large for a single weapon-pair exchange table. The
table likely contains per-frame hit / hurt boxes, move timings, and
combo links for every animation of weapon A vs every animation of
weapon B.

**Stage 4 task**: Reverse the binary layout after the weapon-name
strings. Likely a count-prefixed array of fixed-size records.

## `.plist` — Cocos2d-x TexturePacker atlas descriptor

Standard Cocos2d-x `.plist` atlas format, **TexturePacker format v2**
(confirmed by `<integer>2</integer>` in the metadata block).

### Structure

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
  <dict>
    <key>frames</key>
    <dict>
      <key><frame_name>.png</key>
      <dict>
        <key>frame</key>          <string>{{x,y},{w,h}}</string>     <!-- atlas rect -->
        <key>offset</key>         <string>{dx,dy}</string>            <!-- center offset -->
        <key>rotated</key>        <false/>                           <!-- 90° CW rotation flag -->
        <key>sourceColorRect</key><string>{{sx,sy},{sw,sh}}</string>  <!-- source rect -->
        <key>sourceSize</key>     <string>{srcw,srch}</string>        <!-- original size -->
      </dict>
      ...
    </dict>
    <key>metadata</key>
    <dict>
      <key>format</key>            <integer>2</integer>
      <key>realTextureFileName</key><string>bg.png</string>
      <key>size</key>              <string>{512,1024}</string>
      <key>smartupdate</key>       <string>$TexturePacker:SmartUpdate:...</string>
      <key>textureFileName</key>   <string>bg.png</string>
    </dict>
  </dict>
</plist>
```

**Notes for the reader**:
- `rotated=true` means the sprite is stored rotated 90° clockwise in
  the atlas (so `w` and `h` in `frame` are swapped relative to
  `sourceSize`).
- `offset` is the offset from the center of the source rect to the
  center of the frame rect.
- `sourceColorRect` is the trimmed sub-rect of the source image that
  was actually packed.

The reader is trivial (XML parser + arithmetic on the `{x,y}` strings).
Stage 5 task S5.1.

## `.fnt` — Cocos2d-x bitmap font

Standard AngelCode BMFont text format. Sample header from `CarterOne_num_240.fnt`:

```
info face="Carter One" size=240 bold=0 italic=0 charset="" unicode=0 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=1,1
common lineHeight=240 base=190 scaleW=512 scaleH=512 pages=1 packed=0
page id=0 file="CarterOne_num_240.png"
chars count=11
char id=48  x=0    y=0    width=132  height=189  xoffset=8    yoffset=24   xadvance=149  page=0  chnl=0
...
kernings count=...
```

Standard BMFont spec. Reader is well-known (e.g. existing impls in
Cocos2d-x, raylib, etc.). Stage 5 task S5.4.

## `.json` — Cocos2d-x CocoGUI scene

11 files under `assets/assets/cocoGUI/raids/`. Standard CocoGUI export
format (Cocos GUI Editor v1.x). Top-level structure:

```json
{
  "classname": "Layout",
  "name": "...",
  "version": "1.0",
  "widgetTree": { ... },
  "nodeTree":   { ... },
  "textures":   [ ... ],
  "nodeChildren": [ ... ]
}
```

Reader needs to:
1. Parse JSON.
2. Walk `widgetTree` building a UI node graph with positions, sizes,
   anchors, sprite references, label text, button callbacks.
3. Resolve sprite references against the texture atlas library.

Stage 7.9 task S7.9.

## `.xml` — game data XML

77 XML files across multiple categories (see `04_assets_inventory.md`).
Each category has its own schema; Stage 4 will document each schema,
using the included `moves.xsd` schema for `moves.xml` as a starting
point.

### Sample: `assets/settings.xml` (the master manifest)

```xml
<Filelist />
<File value="settings.xml" />
<File value="assets/Achievements.xml" />
<File value="assets/CharacterProgress.xml" />
<File value="assets/ComputerSettings.xml" />
<File value="assets/config_cdn.xml" />
<File value="assets/forge.xml" />
<File value="assets/internalSettings.xml" />
<File value="assets/list.xml" />
<File value="assets/localization.xml" />
<File value="assets/perks.xml" />
<File value="assets/quests.xml" />
<File value="assets/raid_stages_default.xml" />
<File value="assets/stages.xml" />
<File value="assets/tacticSettings.xml" />
<File value="assets/usersDefault.xml" />
<File value="assets/userSettings.xml" />
<File value="assets/animations/moves.xml" />
<File value="assets/animations/moves.xsd" />
... (continues with localizations, models, quest extensions)
```

This file is the canonical list of every XML the game loads at boot.
Any XML not listed here is loaded on-demand by name.

## `.icf` — Marmalade config

INI-style, with section headers in `[Brackets]` and key=value lines.
Special Marmalade syntax:

- `{OS=ANDROID}` ... `{}` — conditional block (only applies on Android)
- `{ARCH=AARCH64}` ... `{}` — conditional block (only on arm64)
- `{RUNTIME=ART}` ... `{}` — conditional block (only on ART, not Dalvik)
- `[s3e] DispAreaQ + N` — arithmetic on a previously-defined value

### `app_android.icf` (full)

```ini
[MYSETTINGS]
Device=android
Android_obb=0
[S3E]
DispFixRot=Landscape
MemSize = [s3e] DispAreaQ + 94371840
SysGlesVersion = 2
AndroidTryAndroidIdFirst = 0
[s3e]
SysAppVersion=1.0.0
SysAppCaption="Shadow Fight 2"
IOSDispScaleFactor = 200
iPhoneVideoShareAudioSession = 1
[GL]
EGL_DEPTH_ENCODING_NV = 0x30E3
```

Key settings:
- `SysGlesVersion = 2` → OpenGL ES 2.0
- `DispFixRot=Landscape` → landscape orientation lock
- `MemSize = DispAreaQ + 94371840` → memory budget = display-quad
  size + 90 MB (i.e. 90 MB plus framebuffer-derived size)
- `EGL_DEPTH_ENCODING_NV = 0x30E3` → requests EGL_DEPTH_ENCODING_NV
  (non-linear depth buffer, NVIDIA extension) if available

### `app_android_obb.icf` differences

Adds:
```ini
[S3E]
{OS=ANDROID}
{RUNTIME=ART}
SysStackSwitch=0
{}
[APPSFLYER]
devKey="iznFtBfUgefoA2EhJfLMym"
isHTTPS=true
autoStart=true
```

Plus a larger `MemSize` (104 857 600 = 100 MB on top of `DispAreaQ`).
AppsFlyer devKey is included — this is the OBB-build config which is
distributed via Google Play and uses AppsFlyer attribution.

## `res/` (Android resources, decoded by apktool)

243 files: icons, layouts, strings, xml configs. Standard Android
resource tree. reSF2 does not need any of these — they exist purely for
the Android launcher icon and the ad-SDK activities.

## What's **missing** from the APK

- **No GLSL shader files** — shaders are embedded in the `.s3e` binary
  as strings inside the IwGx shader program cache. Stage 2 will dump
  them from `.rodata`.
- **No level / scene script files** beyond the XMLs listed in
  `settings.xml` and the CocoGUI JSONs for raids. Single-player fight
  scene scripting is embedded in the C++ code (`.text` section of the
  `.s3e` binary).
- **No font kerning overrides** beyond what's in `.fnt` files.
- **No physics scene files** — physics is fully defined at runtime by
  the skeletal animation hitbox data and the `.atf` tactics tables.
- **No JSON manifests** other than `cocoGUI/raids/`. Most data is XML.
- **No `.bin` / `.dat` files** at the asset level — the only binary
  asset is the `.s3e` itself, plus the `.atf` zlib blobs.

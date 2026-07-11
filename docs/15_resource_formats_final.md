# 15 — Resource formats inventory (final)

> Status: Stage 4 complete for on-disk formats. The `.dz` DTRZ archive
> and `.bin` animation blob formats are deferred (see `13_animation_format.md`
> for the encryption blocker).

## Summary

| Format | Status | Notes |
| ------ | ------ | ----- |
| `.s3e` (Marmalade binary) | ✅ Stage 2 | `XE3U` container, LZMA1-compressed |
| `.dz` (DTRZ archive) | ⚠️ Stage 4 partial | Header + filename list mapped; payload encrypted |
| `.atf` (tactics blob) | ⚠️ Stage 4 partial | zlib decompression works; binary layout partial |
| `.plist` (texture atlas) | ✅ Stage 4 | Cocos2d-x TexturePacker v2 (plain XML) |
| `.fnt` (bitmap font) | ✅ Stage 4 | AngelCode BMFont text format (plain text) |
| `.json` (CocoGUI UI scene) | ✅ Stage 4 | Cocos2d-x CocoGUI v3.10 (plain JSON) |
| `.xml` (game data) | ✅ Stage 4 | Plain XML, schema varies per file |
| `.icf` (Marmalade config) | ✅ Stage 1 | INI-style text |
| `.png` / `.jpg` | ✅ Stage 1 | Standard image formats |
| `.wav` / `.mp3` | ✅ Stage 1 | Standard audio formats |
| `.mp4` | ✅ Stage 1 | H.264/AAC video |
| `.ttf` | ✅ Stage 1 | TrueType font |

## `.plist` — Cocos2d-x TexturePacker atlas descriptor

Standard Cocos2d-x atlas format, **TexturePacker format v2** (confirmed
by `<integer>2</integer>` in the metadata block). All 148 `.plist`
files in the APK use this format.

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

### Field semantics

| Field | Type | Description |
| ----- | ---- | ----------- |
| `frame` | `{{x,y},{w,h}}` | Rectangle within the atlas PNG where the sprite is packed |
| `offset` | `{dx,dy}` | Offset from the center of the source rect to the center of the frame rect |
| `rotated` | bool | If `true`, the sprite is stored rotated 90° clockwise in the atlas (so `w` and `h` in `frame` are swapped relative to `sourceSize`) |
| `sourceColorRect` | `{{sx,sy},{sw,sh}}` | The trimmed sub-rect of the source image that was actually packed |
| `sourceSize` | `{srcw,srch}` | The original (untrimmed) size of the source image |

### Reader implementation

Trivial: parse XML with pugixml, parse the `{x,y}` / `{{x,y},{w,h}}`
strings with a small hand-written parser (or regex). reSF2 will
implement `engine/renderer/plist_atlas.{hpp,cpp}` in Stage 5.

## `.fnt` — AngelCode BMFont bitmap font

Standard AngelCode BMFont text format. All 16 `.fnt` files in the APK
use this format. Each `.fnt` is paired with a same-name `.png`
containing the glyph atlas.

### Structure

```
info face="Carter One" size=220 bold=0 italic=0 charset="" unicode=0 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=0,0
common lineHeight=339 base=244 scaleW=640 scaleH=640 pages=1 packed=0
page id=0 file="CarterOne_numbers_220.png"
chars count=13
char id=32 x=158 y=390 width=0 height=0 xoffset=0 yoffset=244 xadvance=48 page=0 chnl=0
char id=47 x=326 y=386 width=120 height=190 xoffset=7 yoffset=79 xadvance=94 page=0 chnl=0
char id=48 x=2 y=2 width=182 height=190 xoffset=15 yoffset=81 xadvance=175 page=0 chnl=0
...
kernings count=...
kerning first=65 second=65 amount=-2
```

### Field semantics

**`info` line:**
| Field | Description |
| ----- | ----------- |
| `face` | Font face name |
| `size` | Font size in pixels |
| `bold` / `italic` | Style flags |
| `charset` | Character set (empty = Unicode) |
| `unicode` | 1 if Unicode, 0 if ASCII |
| `stretchH` | Horizontal stretch percentage |
| `smooth` / `aa` | Anti-aliasing flags |
| `padding` | Padding around glyphs (left, top, right, bottom) |
| `spacing` | Spacing between glyphs (horizontal, vertical) |

**`common` line:**
| Field | Description |
| ----- | ----------- |
| `lineHeight` | Line height in pixels |
| `base` | Baseline Y position |
| `scaleW` / `scaleH` | Atlas texture dimensions |
| `pages` | Number of atlas textures |
| `packed` | 1 if glyphs are packed into color channels |

**`char` line:**
| Field | Description |
| ----- | ----------- |
| `id` | Unicode code point |
| `x` / `y` | Position in atlas |
| `width` / `height` | Size in atlas (0 for space character) |
| `xoffset` / `yoffset` | Offset from cursor to top-left of glyph |
| `xadvance` | How far to advance the cursor after this glyph |
| `page` | Which atlas texture page |
| `chnl` | Color channel (15 = all) |

**`kerning` line:**
| Field | Description |
| ----- | ----------- |
| `first` / `second` | The two code points |
| `amount` | Pixel adjustment to add to `xadvance` when `first` is followed by `second` |

### Reader implementation

Well-known format, multiple open-source readers exist (Cocos2d-x
`CCLabelBMFont`, raylib `LoadFontData`, etc.). reSF2 will implement
`engine/renderer/bitmap_font.{hpp,cpp}` in Stage 5.

## `.json` — Cocos2d-x CocoGUI UI scene

11 files under `assets/assets/cocoGUI/raids/`. Standard CocoGUI export
format from Cocos GUI Editor v3.10.0.0 (version string in the JSON
header).

### Structure

```json
{
  "ID": "e5202e35-475f-4157-9c58-fb7693dc430c",
  "Version": "3.10.0.0",
  "Name": "Top100Dialog",
  "Content": {
    "Content": {
      "Animation": { "Duration": 0, "Speed": 1.0, "Timelines": [], "ctype": "TimelineActionData" },
      "AnimationList": [],
      "ObjectData": {
        "Tag": 106,
        "Children": [
          {
            "FileData": { "Type": "Normal", "Path": "data/assets/768/textures/scrolls/common/Roll_MAP.jpg", "Plist": "" },
            "Scale9OriginX": 126, "Scale9OriginY": 251, "Scale9Width": 132, "Scale9Height": 260,
            "AnchorPoint": { "ScaleX": 0.5, "ScaleY": 0.5 },
            "Position": { "X": 512.0, "Y": 384.0 },
            "Scale": { "ScaleX": 1.0, "ScaleY": 1.0 },
            "CColor": {},
            "IconVisible": false,
            ...
          }
        ]
      }
    }
  }
}
```

### Field semantics (top-level)

| Field | Description |
| ----- | ----------- |
| `ID` | UUID of the scene |
| `Version` | CocoGUI editor version (`3.10.0.0`) |
| `Name` | Scene name (matches filename) |
| `Content.Content.Animation` | Timeline animation data (usually empty) |
| `Content.Content.ObjectData` | The widget tree root |

### Widget node fields

Each widget in the `Children` array has:

| Field | Description |
| ----- | ----------- |
| `FileData.Path` | Texture path (relative to a base dir) |
| `FileData.Plist` | Atlas plist path (if sprite comes from an atlas) |
| `FileData.Type` | `"Normal"` (standalone texture) or `"Plist"` (atlas slice) |
| `Scale9Origin*` / `Scale9Width` / `Scale9Height` | 9-slice scaling parameters |
| `AnchorPoint` | Normalized anchor (0.0–1.0) |
| `Position` | Pixel position relative to parent |
| `Scale` | X/Y scale factors |
| `CColor` | Color tint (RGB) |
| `Rotation` | Rotation in degrees |
| `FlipX` / `FlipY` | Mirror flags |
| `Tag` | Integer tag for runtime lookup |
| `ActionTag` | Action identifier |
| `Children` | Recursive child widgets |
| `ctype` | Widget type (`"ButtonObjectData"`, `"ImageViewObjectData"`, `"ListViewObjectData"`, `"TextObjectData"`, etc.) |

### Widget types observed

| `ctype` | Cocos2d-x class | Purpose |
| ------- | ---------------- | ------- |
| `ButtonObjectData` | `ui::Button` | Clickable button |
| `ImageViewObjectData` | `ui::ImageView` | Static image |
| `TextObjectData` | `ui::Text` | Label |
| `TextAtlasObjectData` | `ui::TextAtlas` | Label using bitmap font |
| `ListViewObjectData` | `ui::ListView` | Scrollable list |
| `PanelObjectData` | `ui::Layout` | Container panel |
| `LoadingBarObjectData` | `ui::LoadingBar` | Progress bar |
| `SliderObjectData` | `ui::Slider` | Slider control |
| `TextFieldObjectData` | `ui::TextField` | Text input |

### Reader implementation

Standard JSON, parse with nlohmann/json (vendored) or nlohmann::json
header-only. Walk the `ObjectData` tree recursively, instantiating
the appropriate `ui::Widget` subclass per `ctype`. reSF2 will implement
`engine/ui/cocogui_loader.{hpp,cpp}` in Stage 7.9.

## `.xml` — game data XML

77 XML files. Categories and schemas vary per file. Schemas are
defined inline (no DTD/XSD references except for `moves.xml` which
ships with `moves.xsd` — both inside `files.dz`, currently
inaccessible).

### On-disk XMLs (accessible now)

| File | Schema highlights |
| ---- | ----------------- |
| `assets/settings.xml` | Master manifest: `<Filelist />` + `<File value="path"/>` entries |
| `assets/assets/devices.xml` | Device capability profile: `<Device Name="..." Tablet="..." QualityCondition="..."/>` |
| `assets/assets/obbSettings.xml` | OBB config: `<ObbVersion>`, `<UseObb>` |
| `assets/assets/versionController.xml` | Game version: `<Version Value="1.9.21"/>` |
| `assets/assets/credits/eng.xml` | English credits (text content) |
| `assets/assets/credits/rus.xml` | Russian credits (text content) |

### Inside `files.dz` (inaccessible until .dz decryption)

156 XML files including:
- `Achievements.xml`, `CharacterProgress.xml`, `quests.xml`,
  `perks.xml`, `stages.xml`, `tacticSettings.xml`, `forge.xml`,
  `usersDefault.xml`, `userSettings.xml`, `list.xml`,
  `internalSettings.xml`, `localization.xml`, `config_cdn.xml`,
  `raid_stages_default.xml`, `ComputerSettings.xml`
- `animations/moves.xml` + `animations/moves.xsd` (the schema!)
- 12 localization XMLs (`localizations/eng.xml`, `rus.xml`, etc.)
- ~90 model XMLs (`models/armor_*.xml`, `models/body_*.xml`,
  `models/weapon_*.xml`, `models/head_*.xml`, `models/helm_*.xml`,
  `models/magic_*.xml`, `models/skeleton_*.xml`)
- ~20 quest extension XMLs (`quest_extensions/zone_*/core.xml`,
  `zone_*/story.xml`, etc.)

### `devices.xml` schema (representative example)

```xml
<?xml version="1.0" encoding="utf-8"?>
<Root>
  <Config>
    <DefaultResolution ConditionType="Equal" Value="752" Tablet="1"
                      Resolution="LOW" LocationResolution="HIGH"/>
    ...
  </Config>
  <Devices>
    <Device Name="iPad2,1" Tablet="1" LocationResolution="HIGH" QualityCondition="HIGH"/>
    <Device Name="iPhone4,1" Tablet="0" QualityCondition="HIGH"/>
    ...
  </Devices>
</Root>
```

Each `<Device>` entry maps a device model name (e.g. `iPad2,1`) to
quality settings. The game uses this at boot to scale graphics
quality based on the device.

reSF2 will mostly ignore `devices.xml` — we target modern hardware
where everything runs at `HIGH` quality by default.

## `.icf` — Marmalade config

INI-style text. Already documented in Stage 1's `05_resource_formats.md`.
No changes for Stage 4.

## reSF2 loader implementation plan (Stage 5)

| Loader | Source | Priority |
| ------ | ------ | -------- |
| `plist_atlas` | `.plist` | Stage 5.1 (needed for M2 — main menu) |
| `bitmap_font` | `.fnt` | Stage 5.4 (needed for M2 — main menu) |
| `cocogui_loader` | `.json` | Stage 7.9 (needed for M2 — raid UI) |
| `xml_loader` | `.xml` | Stage 5.x (per-schema, as needed) |
| `atf_tactics` | `.atf` | Stage 5.2 (needed for M3 — battle logic) |
| `dz_archive` | `.dz` | Stage 5.3 (BLOCKED on encryption key) |
| `s3e_container` | `.s3e` | ✅ Stage 2 (done) |

## What's still unknown

1. `.dz` DTRZ archive decryption key (see `13_animation_format.md`).
2. `.atf` binary tactics table byte layout (see `14_tactics_format.md`).
3. `.bin` animation blob format (deferred until `.dz` extraction works).
4. Per-schema XML layouts for the 156 XMLs inside `files.dz`
   (deferred until `.dz` extraction works).

Items 1–4 are all blocked on the same thing: `.dz` decryption. Once
that's solved, the rest cascades quickly because the XMLs and `.bin`s
will be on disk for direct inspection.

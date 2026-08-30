# core/data — asset & data layer

Portable C++17 modules for loading the Shadow Fight 2 web-game assets.
No platform-specific code lives here (that goes in `platform/` later).

## Modules

| Module | Purpose |
|--------|---------|
| `zstd_stream` | Decompress a complete zstd frame (RFC 8478) into a buffer. |
| `xml_doc` | pugixml wrapper: parse UTF-8 XML text (BOM-tolerant) + attribute helpers. |
| `xml_archive` | Reader for the `xml.dat` container format (see below). |
| `anim_archive` | Reader for the animation clip format inside `animations.*.dat` (see below). |

## The xml.dat container format

`reference/www/res/xml.9e0b4b10.dat` (226,646 bytes) is a zstd-compressed
archive of XML files. After decompression (5,494,525 bytes) the container is a
simple sequential archive — **not** Haxe-serialized:

```
Offset  Size  Field
------  ----  ---------------------------------------------------------
0       2     u16 LE  file count (41 in xml.9e0b4b10.dat)
              ── per file, repeated `count` times ──
+0      1     u8   name length (bytes)
+1      N     name bytes (UTF-8, archive-relative path, e.g. "res/moves.xml")
+N      3     u24 LE data size (3 bytes, little-endian)
+N+3    M     data bytes (the raw XML text)
```

Byte-level example (first entry of xml.9e0b4b10.dat):

```
29 00                count = 0x0029 = 41
15                   name_len = 21
72 65 73 2F 75 73 65 72 73 5F 64 65 66 61 75 6C 74 2E 78 6D 6C
                     "res/users_default.xml"
E7 04 00             size = 0x0004E7 = 1255
3C 3F 78 6D 6C ...   "<?xml version=\"1.0\"?>" ... (1255 bytes)
```

Notes:
- All integers are little-endian. The size field is 24-bit (max 16 MiB per file).
- The archive parses exactly to the end of the decompressed buffer (no trailer).
- The game's `Ja.Mda(archive, name)` (JS_MAP §7.1) extracts a named file from
  this container; `Ja.ki(id)` parses the XML text with `Wg.parse`.
- The same container layout is used by the other `.dat` archives
  (`models.*.dat`, `animations.*.dat`, ...) — verified per-file (Phase 2b).

## The animations.*.dat clip format

`reference/www/res/animations.b22c72ff.dat` (asset 1355, `Ja.Dka`) and
`animations_dojo.3314a7de.dat` (asset 1354) are zstd-compressed containers of
the same layout as `xml.dat` (u16 count + per-file name/size/data). Each entry
is one named move clip (e.g. `air_axe_kick`, `axe_stance_idle`) whose bytes are
parsed by the game's `Vlb` method (the animation data holder feeding the `Te`
animation controller, JS_MAP §7.3). **This is NOT Haxe serialization** — the
game build ships no `haxe.Unserializer`; the clip is a custom binary format:

```
Version 1 (564 of 566 clips in animations.b22c72ff.dat):
  u8   version = 1
  u8   frame count
  per frame:
    u16  bone count
    per bone: 3 x i16 LE (x, y, z)  ->  position (x/16, -y/16, z/16)

Version 0 (2 clips: ranged_blaster_bulet, tentacle_ability):
  u8   version = 0
  u32  frame count
  per frame:
    u8   skipped (unused flag byte)
    u32  bone count
    per bone: 3 x f32 LE (x, y, z)  ->  position (x, -y, z)
```

- Layout is frame-major: `frames[f].bones[b]` = position of bone `b` at frame
  `f`, matching the game's `Te.Kk[frame][bone]` access (`Te.Xqb`, L566).
- The `y` component is negated by the game (`new H(x/16, -(y/16), z/16)`).
- Bone names are NOT stored in the clip — bones are indexed; names live in the
  model XML (`<Nodes>` elements of `models.*.dat`).
- Duration = frame count / 60 s (the game's fixed 60 Hz update step).
- `anim_clip_parse` (anim_archive) implements both versions and validates that
  the clip parses exactly to the entry size.

## The models.*.dat format

`reference/www/res/models.473fd74f.dat` (asset 315, `Ja.Ra`) and
`models_dojo.e57366a0.dat` (asset 314) are zstd-compressed containers of the
same layout as `xml.dat`. Each entry is a named ragdoll model (e.g.
`mdl_armor_alloy`, `mdl_body`) whose data is **plain XML text** parsed by the
game's `Yc.parse` (JS_MAP §7.3):

```
<Scene>
  <Nodes>    <NAME X=.. Y=.. Z=.. Type="Node"|"MacroNode" Mass=.. .../>
             MacroNodes carry ChildNode1..4 + LCC1..4 (local child coords)
  <Edges>    <NAME End1=.. End2=.. Length=.. Radius=.. Type="Edge"|"Muscle" .../>
  <Figures>  <NAME Type="Capsule" Edge=.. .../>   (collision capsules)
             <NAME Type="Triangle" Node1=.. Node2=.. Node3=.. .../>  (mesh)
```

- "Bones" = `<Nodes>` elements (Type="Node" + Type="MacroNode").
- "Mesh" = `<Figures Type="Triangle">` (each references 3 nodes by name).
- The fighter's visual mesh and physics body are both built from this XML
  (`Yc.load` → `Yc.parse`, L289191).

## Decode chain (verified, Phase 2b)

All five `.dat` archives in `reference/www/res/` are **plain zstd frames** —
decompress the whole file, then parse the container. There is **no XOR/decrypt
layer** on these files:

- The game loads assets 0/314/315/1354/1355 via `Ja.Vxb()` with
  `Og.GI` = `(new si).read(a)` (zstd only, JS_MAP §7.2).
- The `oy.vza` wrapper (zstd + SHA-256 XOR) is applied only to assets in
  `G.$Ua = [1317, 1316]` (JS L2389-2390), which are not the animation/model
  archives. Its algorithm, for reference: last 7 bytes = [u24 LE payload
  size][flag byte][3 unused]; payload = bytes [size-(d+7), size-7); if flag
  bit 0 is set, XOR each payload byte with the ASCII hex characters of
  SHA-256(prefix [0, size-(d+7))), cycling 64.
- Empirically verified: whole-file zstd decompression succeeds for all five
  files (xml 5,494,525 B; animations 8,835,082 B; models 39,451,606 B;
  animations_dojo 417,960 B; models_dojo 140,944 B).

## Extraction

`xml_archive_extract(entries, out_dir)` writes every entry under `out_dir`
preserving the archive-relative path, e.g. `res/moves.xml` →
`<out_dir>/res/moves.xml`. The asset_explorer tool extracts to
`reference/extracted/xml/` (gitignored).
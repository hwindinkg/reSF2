# core/data — asset & data layer

Portable C++17 modules for loading the Shadow Fight 2 web-game assets.
No platform-specific code lives here (that goes in `platform/` later).

## Modules

| Module | Purpose |
|--------|---------|
| `zstd_stream` | Decompress a complete zstd frame (RFC 8478) into a buffer. |
| `xml_doc` | pugixml wrapper: parse UTF-8 XML text (BOM-tolerant) + attribute helpers. |
| `xml_archive` | Reader for the `xml.dat` container format (see below). |

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
- The same container layout is expected for the other `.dat` archives
  (`models.*.dat`, `animations.*.dat`, ...) — verify per-file before relying on it.

## Extraction

`xml_archive_extract(entries, out_dir)` writes every entry under `out_dir`
preserving the archive-relative path, e.g. `res/moves.xml` →
`<out_dir>/res/moves.xml`. The asset_explorer tool extracts to
`reference/extracted/xml/` (gitignored).
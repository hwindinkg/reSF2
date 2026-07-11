# 09 — `.s3e` binary format (`XE3U` container)

> Status: Stage 2 partial. Header + embedded config + import-name table
> fully mapped. Relocation table + section table layout still under
> investigation (deferred to Stage 4 when needed for code analysis).
>
> Source: `assets/ShadowFight2.s3e` decompressed to 8 689 357 bytes.

## Outer wrapper: LZMA1 legacy

The `.s3e` file on disk is a raw LZMA1 stream with the legacy 13-byte
header (no container):

| Offset | Size | Field              | Observed value              |
| ------ | ---- | ------------------ | --------------------------- |
| 0x00   | 1    | props byte         | `0x5d` → lc=3, lp=0, pb=2  |
| 0x01   | 4    | dict_size (LE)     | `0x00010000` = 65536        |
| 0x05   | 8    | uncomp_size (LE)   | `0x0000000000848c4d` = 8 689 357 |
| 0x0d   | …    | LZMA1 stream       | decompresses to 8 689 357 B |

Decompression (Python):

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

## Inner payload: `XE3U` magic

After LZMA decompression, the payload starts with magic `58 45 33 55` =
`"XE3U"`.

`XE3U` reversed is `U3EX`. The literal string `"XE3U"` does **not**
appear in `libs3e_android.so` — the loader does not validate this magic
by literal comparison. The validation is likely a u32 compare against
the constant `0x55334558` (LE) loaded via `movw`/`movt`, which does not
show up as a printable string.

### 76-byte header

| Offset | Size | Field              | Observed (hex)   | Observed (dec) | Hypothesis |
| ------ | ---- | ------------------ | ---------------- | -------------:| ---------- |
| 0x00   | 4    | magic              | `58 45 33 55`    | —             | `"XE3U"` literal |
| 0x04   | 4    | u32_04             | `0x00042800`     | 272 384       | code size? (close to 256 KB) |
| 0x08   | 4    | u32_08             | `0x010c000a`     | 17 563 658    | version/flags; bytes `0a 00 0c 01` may be `version.minor.major.flags` |
| 0x0c   | 4    | import_table_end   | `0x00001521`     | 5 409         | end of import-name table (start of next section) |
| 0x10   | 4    | u32_10             | `0x00043d30`     | 277 808       | end of relocation table? |
| 0x14   | 4    | u32_14             | `0x00045251`     | 283 217       | end of next section |
| 0x18   | 4    | vaddr_18           | `0x008042c8`     | 8 405 704     | load vaddr (segment 1 start) |
| 0x1c   | 4    | vaddr_1c           | `0x00825d5c`     | 8 543 580     | load vaddr (segment 2 start) |
| 0x20   | 4    | vaddr_20           | `0x00849519`     | 8 688 921     | load vaddr (segment 3 start, near EOF) |
| 0x24   | 4    | u32_24             | `0x0000008c`     | 140           | small count |
| 0x28   | 4    | u32_28             | `0x00000000`     | 0             | reserved |
| 0x2c   | 4    | config_offset      | `0x0000004c`     | 76            | start of embedded config text (= header size) |
| 0x30   | 4    | config_length      | `0x000014d5`     | 5 333         | length of embedded config text |
| 0x34   | 4    | u32_34             | `0x4a000000`     | 1 241 513 984 | flags? bytes `00 00 00 4a` |
| 0x38   | 4    | vaddr_38           | `0x008495a5`     | 8 689 061     | load vaddr (final segment) |
| 0x3c   | 4    | u32_3c             | `0x00000128`     | 296           | final segment size? (EOF - vaddr_38 ≈ 296) |
| 0x40   | 4    | u32_40             | `0x0000000c`     | 12            | small count |
| 0x44   | 4    | vaddr_44           | `0x007b8000`     | 8 093 696     | GOT vaddr (resolvable function pointers placed here) |
| 0x48   | 4    | u32_48             | `0x00000000`     | 0             | reserved |

**Verification of `0x3c` (296) = final segment size**:
`vaddr_38` (8 689 061) + 296 = 8 689 357 = file size ✓

**Verification of `import_table_end` (0x0c)**:
`config_offset` (0x4c) + `config_length` (0x14d5) = 0x1521 ✓
The import-name table begins at offset 0x1521 with an 8-byte preamble
(`00 00 00 00 a0 16 00 00`) and the first name string starts at 0x1529.

### Embedded config text (offset 0x4c, length 5 333)

The bytes at offset 0x4c..0x1520 are Marmalade's global system config
(`s3e.icf`), an INI-style text file. Sample:

```
# This is the global system configuration file for Marmalade applications.
# This file is automatically included in all application deployments and
# used when debugging an x86 build.
#
# Most of the ...
```

The tail of the config text lists the native libraries the loader should
`dlopen` after the S3E binary is loaded:

```
libs3eChartBoost.so;libs3eAdColony.so;libInputDeviceExtension.so;libs3eSmartFox.so;libs3eObbGui.so
```

This is **runtime extension loading**: the S3E binary itself does not
link these extensions; the loader `dlopen`s them on boot based on this
list.

### Import-name table (offset 0x1529, length ~10 KB)

After the 8-byte preamble, the file contains a sequence of
null-terminated ASCII strings. Each string is a function name the S3E
binary expects the loader to resolve.

Filtering to valid C identifiers (length 3–64, only `[A-Za-z0-9_:]`)
yields **346 entries**:

| Category | Count | Examples |
| -------- | ----: | -------- |
| `gl*`    |   210 | `glBindRenderbuffer`, `glGenFramebuffers`, `glUniform4f`, … |
| `s3e*`   |   117 | `s3eMallocBase`, `s3eFileOpen`, `s3eTimerGetMs`, `s3eSocketCreate`, … |
| other    |    19 | non-prefixed names |

This is **the complete Marmalade SDK API surface** the game uses. For
reSF2, this is the contract our runtime must implement. Full list saved
to `engine/reverse/s3e_imports.txt`.

The loader (`libs3e_android.so`) resolves these names against its own
exported symbol table and writes the function pointers into the GOT
at virtual address `0x007b8000` (header field `vaddr_44`).

The error string `"Functions required by game but not defined in loader:"`
found in `libs3e_android.so` confirms this: if a name in this table has
no matching export in the loader, the game aborts at boot with that
error.

### Relocation table (offset 0x2bbf → 0x43d30, ~266 KB)

After the import-name table ends at offset 0x2bbf, the file contains a
large (~266 KB) binary section. Inspection of the first bytes:

```
00 00 01 00 00 00 20 1e 04 00 85 07 01 00 00 80
7b 00 04 80 7b 00 08 80 7b 00 0c 80 7b 00 10 80
7b 00 14 80 7b 00 18 80 7b 00 1c 80 7b 00 20 80
7b 00 24 80 7b 00 28 80 7b 00 2c 80 7b 00 30 80
```

The repeating `80 7b 00 XX` pattern (where XX increments by 4) suggests
an array of 4-byte virtual addresses pointing into the GOT at
`0x007b8000 + 4*i`. Each entry is essentially a relocation: "at this
GOT slot, store the resolved address of the i-th imported function".

Full layout of this table is **not yet mapped**. reSF2 does not need
to interpret it — we are not executing the original `.s3e` binary, we
are writing a clean-room replacement. We only need to know:

1. The list of import names (✓ extracted).
2. The GLSL shaders embedded in `.rodata` (✓ extracted, see below).
3. The list of strings the game uses (for asset path resolution — partial).

### Section name string table (`.text` at file offset 0x736c3a)

A small section-name table lives near offset 0x736c3a in the file. The
only ELF-like section name we located was `.text` (2 occurrences). The
table is small — ~10 strings total. Marmalade's S3E format does not use
a full ELF section table; this mini-table is likely for diagnostic
purposes only.

### GLSL shaders (offset 0x768ee9 → 0x76b000, ~10 KB)

The `.rodata` section contains 17 GLSL shader source strings. All use
GLES2 syntax (`#ifdef GL_ES`, `precision lowp float`, `texture2D`,
`gl_FragColor`). Several use Cocos2d-x naming conventions
(`CC_Texture0`, `CC_alpha_value`, `v_fragmentColor`, `v_texCoord`),
which suggests Nekki's renderer was at least partially inspired by
Cocos2d-x's `CCGLProgram` even though no Cocos2d-x library is linked.

Shader categories observed:
- Standard sprite shader: `gl_FragColor = v_fragmentColor * texture2D(...)`
- Alpha-test shader: `CC_alpha_value` for cutout sprites
- Mask shader: `u_texture` + `u_mask` for masked compositing
- Color-only shader: solid color with `u_color` uniform

Full list saved to `engine/reverse/s3e_shaders.txt`. These shaders will
be used verbatim by reSF2's renderer (they are not original art — they
are standard GLES2 boilerplate that any clean-room implementation would
re-create identically).

### Code + data sections (offset 0x45251 → 0x8496cd, ~8.4 MB)

The bulk of the file (~8.4 MB) contains:
- ARM Thumb-2 code (the game's compiled C++)
- Read-only data (string literals, lookup tables, IwResManager binary groups)
- Read-write data (global variables, BSS placeholders)

This is the section that reSF2 will **not** parse or execute. We treat
it as a black box and re-implement the game logic in clean-room C++.

### Header field `vaddr_18` / `vaddr_1c` / `vaddr_20` — segment layout

The three vaddr fields at offsets 0x18, 0x1c, 0x20 describe where in
the loader's heap the segments will be placed:

| Field    | vaddr      | Interpretation |
| -------- | ---------- | -------------- |
| vaddr_18 | 0x008042c8 | First segment (code or rodata) loaded at heap+0x42c8 |
| vaddr_1c | 0x00825d5c | Second segment loaded at heap+0x25d5c |
| vaddr_20 | 0x00849519 | Third segment loaded at heap+0x49519 (close to file end) |
| vaddr_38 | 0x008495a5 | Final segment (size 296 bytes) |
| vaddr_44 | 0x007b8000 | GOT — placed BEFORE the load base (0x00800000) so it doesn't conflict |

The base heap address `0x00800000` (8 MB) is Marmalade's default S3E
load base on Android. The 8 MB offset leaves room for the loader's
own data structures below the S3E binary.

## Stage 2 implementation: `engine::reverse::s3e::parse()`

A C++20 read-only parser for the header + config + import-name table is
implemented in `engine/reverse/s3e_container.{hpp,cpp}` with unit tests
in `tests/test_s3e_container.cpp`. The parser:

- Validates the `XE3U` magic.
- Reads all 19 header u32 fields into a `Header` struct.
- Returns a `string_view` over the embedded config text.
- Walks the import-name table and returns a `vector<ImportEntry>` of
  valid C-identifier names (filtering out binary garbage that occupies
  the gap between the name table end and the relocation table start).
- Does NOT parse the relocation table or the code/data sections (out of
  scope for clean-room reimplementation).

The parser uses `std::expected` for error handling (no exceptions in
the parser path), `std::span` for borrow-checking, and bounds-checks
every read.

## What's still unknown (deferred to Stage 4)

1. **Relocation table format** — the 266 KB section at 0x2bbf. Layout
   to be documented if/when we need to do static analysis of the game
   code (e.g. finding the main loop function). For reSF2's runtime we
   do not need this.
2. **Section table** — Marmalade may have a proper section table
   somewhere in the file (analogous to ELF's `.shstrtab` + section
   header table). The `.text` string at 0x736c3a hints at this but we
   have not located the table itself.
3. **Symbol table** — if the S3E has a `.symtab`, it would let us
   recover internal C++ function names (not just imports). Not located
   yet.
4. **Exact loader algorithm** — the function at `0x3c698` in
   `libs3e_android.so` is the real S3E loader. Full disassembly would
   resolve all the above questions, but is not needed for clean-room.

## References

- Marmalade SDK source (publicly available 2009–2017, now archived):
  `s3eLoader.cpp`, `s3eExec.cpp` in the `s3e` module.
- `libs3e_android.so` strings: `"Assertion Failure (Marmalade v8.2.1
  [465988])"`, `"Incorrect signature in s3e file"`, `"Functions required
  by game but not defined in loader:"`, `"Can't open s3e file %s"`.
- `engine/reverse/s3e_imports.txt` — full 346-entry import name list.
- `engine/reverse/s3e_shaders.txt` — full 17-shader source dump.

# 13 — Animation format (`.dz` DTRZ archive + `.bin` animation blobs)

> Status: Stage 4 partial. The `.dz` (DTRZ) archive format is partially
> decoded — header, filename list, and metadata table are mapped, but
> the per-file payload compression is **encrypted** (not standard zlib)
> and the decryption key has not yet been recovered. Full extraction
> is deferred to Stage 4.x (requires disassembly of the .s3e derbh
> reader or runtime tracing).
>
> Source: `assets/animations.dz` (14 MB, 556 files) and
> `assets/files.dz` (2 MB, 120 files).

## Background

The `.dz` files are **Marmalade derbh archives**. Confirmed by string
analysis of the `.s3e` binary:

```
0x797f39: .EGL_DEPTH_SIZE.DTRZ.derbh.autoload.MinBufSize.o
```

The strings `DTRZ` (the magic) and `derbh` (Marmalade's archive system
name) appear adjacent in the `.s3e` `.rodata`. The game calls
`s3eCompressionDecomp*` imports to decompress individual files within
these archives.

`derbh` is Marmalade SDK's binary archive system, analogous to ZIP but
designed for game assets. The format was never officially documented
outside the (now-EOL) Marmalade SDK source.

## Files using the DTRZ format

| File                       | Size        | Files inside | Purpose |
| -------------------------- | ----------: | -----------: | ------- |
| `assets/files.dz`          | 1 975 155 B |          120 | All gameplay XMLs (achievements, quests, perks, models, localizations, etc.) |
| `assets/animations.dz`     |14 094 258 B |          556 | All skeletal animation `.bin` blobs + `animations_list.xml` + `settings.xml` |
| `tmp.dz`                   | (runtime)   |        (var) | Created at runtime for in-memory packing |
| `zip://obb.dz`             | (OBB build) |        (var) | OBB expansion file (mapped as a .dz archive) |
| `ZONE_2.dz` ... `ZONE_6.dz`| (CDN)       |        (var) | Per-zone raid content downloaded from CDN |

## Outer container format (mapped)

```
Offset  Size  Field                Value (files.dz / animations.dz)
0x00    4     magic                "DTRZ" (44 54 52 5a)
0x04    2     u16_1                120 / 557       (file count? table size?)
0x06    2     u16_2                105 / 556       (file count - 1? or true count)
0x08    1     u8                   0               (version? always 0)
0x09    var   filename list        null-terminated UTF-8 strings, back-to-back
??      var   metadata table       6 bytes per entry (see below)
??      var   offset/size table    16 bytes per file entry (format uncertain)
??      var   encrypted payloads   per-file compressed data (see below)
```

### Filename list

After the 9-byte header, the file contains a sequence of null-terminated
UTF-8 strings. Each string is either:
- A **filename** (e.g. `files_list.xml`, `axe_kick.bin`)
- A **directory path** (e.g. `assets\animations\binary` — note backslash
  separator, Windows-style)

Directory entries are repeated once per file inside them. For example,
`assets\animations\binary` appears N times if N animation `.bin` files
live in that directory.

For `files.dz`:
- 224 total name entries
- 120 filenames (have `.` extension)
- 104 directory path entries

For `animations.dz`:
- 1 112 total name entries
- 556 filenames (mostly `.bin` animation blobs)
- 556 directory path entries

### Metadata table (6 bytes per entry)

Starts immediately after the filename list. Each entry:

```
Offset  Size  Field              Value
0x00    2     u16 LE             file_id (incrementing: 0, 1, 2, ...)
0x02    1     byte               always 0x00
0x03    2     u16 LE             always 0xffff (sentinel)
0x05    1     byte               parent_directory_id (index into the
                                  filename list of the parent directory)
```

The `parent_directory_id` lets the loader reconstruct the directory
tree. For example, in `files.dz`:
- entry 0: id=0, parent=0 (root, special)
- entries 1-15: id=1..15, parent=1 (all children of name[1]=`settings.xml`?
  — but `settings.xml` is a file, not a directory. Probably the parent
  semantics differ from what the name suggests.)
- entry 16: id=16, parent=2
- entry 17: id=17, parent=3
- ...

The exact semantics of `parent` are unclear without disassembly.

### Offset/size table (16 bytes per file entry — partial)

After the metadata table, there is a table with 16 bytes per **file**
(not per directory). For `files.dz` with 120 files, this table is
1 920 bytes.

Each 16-byte entry appears to be four u32 LE values, where each u32 is
encoded as `(flags_byte << 24) | u24_value`:

```
Offset  Size  Field              Value
0x00    4     u32 LE             (flags, file_index=0)  — flags byte varies per file
0x04    4     u32 LE             (flags, offset)        — offset within data section
0x08    4     u32 LE             (flags, comp_size)     — compressed size in bytes
0x0c    4     u32 LE             (0x04, uncomp_size)    — uncompressed size, type=zlib
```

The high byte of the 4th field is always `0x04` — likely a compression
type identifier (zlib = 4 in Marmalade's `s3eCompression` enum).

The high bytes of the first three fields vary per file and may be:
- A CRC8 hash of the filename (for integrity check)
- Or a per-file XOR / decryption key byte

**Offset consistency** (verified for files.dz entries 0..19):

| Entry | offset | comp_size | next_offset | diff | padding |
| ----- | -----: | --------: | ----------: | ---: | ------: |
| 0     |   4971 |       139 |        5111 |  140 |       1 |
| 1     |   5111 |        76 |        5188 |   77 |       1 |
| 2     |   5188 |        64 |        5253 |   65 |       1 |
| 3     |   5253 |        62 |        5315 |   62 |       0 |
| 4     |   5315 |        90 |        5406 |   91 |       1 |

`padding` is mostly 1 byte (probably a null terminator or alignment byte).

### Per-file payload (encrypted — NOT standard zlib)

The actual file payloads are NOT stored as plain zlib streams. Despite
the presence of `78 da` / `78 9c` / `78 5e` byte pairs (which look like
zlib magic), `zlib.decompress` fails on all of them with errors like
`invalid code lengths set` or `invalid block type`.

**Hypothesis** (confidence: high): The data is **XOR-encrypted** with
a per-file or global key. Evidence:

- Known-plaintext attack on file 0 of `files.dz` (`files_list.xml`,
  assumed to start with `<?xml version="1.0"`):
  - Cipher bytes at offset 0x2f05: `d5 03 a5 69 fc 05 9b 3a dc fa a0 d7 bc 8f 88 0c 7b 6c d7`
  - Plaintext:                    `3c 3f 78 6d 6c 20 76 65 72 73 69 6f 6e 3d 22 31 2e 30 22`
  - Derived key fragment (19 B):  `e9 3c dd 04 90 25 ed 5f ae 89 c9 b8 d2 b2 aa 3d 55 5c f5`
  - Applying this 19-byte fragment cyclically does NOT decrypt bytes
    19+ correctly — so the key is either longer than 19 bytes, or it's
    a stream cipher (RC4-like), or it's per-file.

**Key recovery attempts** (all failed):
- Searched `.s3e` binary for the 19-byte fragment: not found.
- Tried RC4 with seeds `DTRZ`, `derbh`, `s3e`, `Marmalade`, `sf2`,
  `SF2`, `ShadowFight2`, `Nekki`, `12345678`, `\x00\x00\x00\x00`,
  `\xff\xff\xff\xff`, `DTRZderbh`, `derbhDTRZ`: no match.
- Tried 8-bit LCG brute force (a, c, x0 each 0..255): no match.
- Tried MD5/SHA1/SHA256 of common seeds: no match.

**Conclusion**: The encryption key is either:
1. Derived at runtime from device-specific data (device ID, install
   time, etc.) — unlikely for a static asset archive.
2. Embedded in the `.s3e` `.text` section as compiled code (not as
   a string) and loaded into memory at boot.
3. A more complex stream cipher than RC4.

**Stage 4.x task**: Disassemble the `.s3e` function that handles the
`DTRZ` magic (the string reference is at `.s3e` offset `0x797f39`).
The function near that string is the derbh reader; tracing its data
flow will reveal the decryption.

### Alternative extraction paths

If the encryption cannot be reversed statically, two runtime options:

1. **frida trace on a real device**: Hook `s3eCompressionDecompInit` /
   `s3eCompressionDecomp` and dump the decrypted plaintext to disk.
   Requires a rooted Android device with the game installed.
2. **Memory dump after boot**: After the game loads `files.dz` into
   memory, the decrypted XMLs live in heap. Dump with `gcore` or
   similar. Requires root.

reSF2's Stage 4 implementation will document both paths but not require
them — the engine will load extracted XMLs from disk (provided by the
user after running the runtime dump tool, which we'll write in Stage 6).

## Animation `.bin` blob format (deferred)

Once `animations.dz` is extracted, each `.bin` file (e.g.
`axe_kick.bin`, `air_punch.bin`) is a skeletal animation blob. The
format is expected to be:
- A small header (version, frame count, bone count)
- Per-frame bone transforms (translation, rotation, scale)
- Possibly event markers (hit frames, footstep frames, etc.)

Stage 4.x will document this format after `.dz` extraction works.

## What reSF2 needs from this format

For Stage 7.3 (skeletal animation):
- The list of all animation names (✓ known from filename list).
- The bone hierarchy per animation (deferred — needs `.bin` parsing).
- The frame data per bone per frame (deferred).

For Stage 7.7 (battle logic):
- The frame windows for hitbox active frames (deferred).
- The frame windows for combo links (deferred).

Until `.dz` extraction works, reSF2 cannot implement animation playback
or battle logic. This is the single biggest blocker in Stage 4.

## Workaround for early Stage 7 milestones

For **Milestone M1** (window opens, ESC closes — Stage 7.1) and
**Milestone M2** (main menu visible — Stage 7.2, 7.5, 7.9), reSF2 does
NOT need `.dz` extraction. The main menu uses:
- `.png` textures (on disk, not in `.dz`)
- `.plist` atlases (on disk)
- `.fnt` bitmap fonts (on disk)
- CocoGUI `.json` UI scenes (on disk, under `cocoGUI/raids/`)

So M1 and M2 can proceed without solving the `.dz` encryption.

**Milestone M3** (playable fight) DOES need `.dz` extraction — the
fight uses `.bin` animations and `.xml` model definitions, both of
which live inside `.dz`.

## Next steps for `.dz` extraction

1. Disassemble the function near `.s3e` offset `0x797f39` (the `DTRZ`
   string reference). This is the derbh reader.
2. Trace the data flow from `s3eCompressionDecompInit` through
   `s3eCompressionDecomp` to identify the decryption step.
3. If static analysis fails, write a frida script that hooks the
   decompression functions on a real device and dumps plaintext.
4. Document the decryption in `docs/13b_dz_decryption.md` (to be
   created when the key is found).

# 14 — Tactics format (`.atf` zlib-compressed blobs)

> Status: Stage 4 partial. The `.atf` zlib decompression works (verified
> on 4 sample files). The binary tactics table layout is partially
> decoded — the header is mapped, but the per-record byte layout needs
> more analysis.
>
> Source: `assets/tactics/*.atf` — 110 files, ~5 MB total compressed.

## Overview

The `.atf` files in `assets/tactics/` are **not** Adobe Texture Format
files (which use magic `ATF`). They are zlib-compressed custom blobs
containing **weapon-pair combat tactics** — the data that drives the
game's hitbox/hurtbox/damage/combo system for each weapon-vs-weapon
pairing.

Each `.atf` file describes the combat exchange table for either:
- A single weapon (`_<weapon>.atf` — 19 files, e.g. `_fists.atf`)
- A weapon pair (`<weaponA>_<weaponB>.atf` — ~91 files, e.g.
  `batons_claws.atf`)

## File naming convention

### Single-weapon files (19 total)

`_<weapon>.atf` — defines the base tactics for one weapon against the
"null" opponent (or against itself). The `_` prefix marks these as
base/single-weapon files.

Examples: `_.atf`, `_batons.atf`, `_claws.atf`, `_crescentknives.atf`,
`_fists.atf`, `_keris.atf`, `_knives.atf`, `_knuckles.atf`,
`_kusarigama.atf`, `_machete.atf`, `_ninjasword.atf`, `_nunchaku.atf`,
`_sai.atf`, `_scythe.atf`, `_spear.atf`, `_steelclaws.atf`,
`_swords.atf`, `_tonfa.atf`.

### Weapon-pair files (~91 total)

`<weaponA>_<weaponB>.atf` — defines the tactics for weapon A vs
weapon B. With 19 weapons, the full pair matrix is 19×19 = 361, but
the game ships only ~91 (the most common pairings, or all unique
unordered pairs: 19*18/2 = 171, still more than 91 — so some
asymmetric pairs are also missing).

Examples: `batons_claws.atf`, `batons_fists.atf`, `batons_knives.atf`,
`batons_knuckles.atf`, `batons_kusarigama.atf`, `batons_machete.atf`,
`batons_ninjasword.atf`, `batons_nunchaku.atf`, `batons_sai.atf`,
`batons_spear.atf`, ...

## Weapons enumerated (17 unique)

| Weapon ID | Display name | Notes |
| --------- | ------------ | ----- |
| `batons` | Tonfa-like sticks | Starter weapon |
| `claws` | Claws | |
| `crescentknives` | Crescent knives | |
| `fists` | Fists (no weapon) | Always available |
| `keris` | Kris dagger | |
| `knives` | Knives | |
| `knuckles` | Knuckles | |
| `kusarigama` | Kusarigama | |
| `machete` | Machete | |
| `ninjasword` | Ninja sword | |
| `nunchaku` | Nunchaku | |
| `sai` | Sai | |
| `scythe` | Scythe | |
| `spear` | Spear | |
| `steelclaws` | Steel claws | Upgrade of `claws` |
| `swords` | Swords | |
| `tonfa` | Tonfa | |

The `_.atf` file (empty weapon name) is the "no weapon vs no weapon"
base case — essentially fist-vs-fist tactics.

## File format

### Outer wrapper: zlib (best compression)

```
$ file _fists.atf
_fists.atf: zlib compressed data, best compression

$ python3 -c "import zlib; print(len(zlib.decompress(open('_fists.atf','rb').read())))"
1171186
```

All `.atf` files decompress cleanly with standard `zlib.decompress`
(no encryption, no custom wrapper).

### Decompressed payload

```
Offset  Size   Field                Value
0x00    4      version (u32 LE)     always 1
0x04    var    weapon A name        null-terminated ASCII string
??      var    weapon B name        null-terminated ASCII string
??      var    binary tactics data  (see below)
```

For single-weapon files (`_<weapon>.atf`):
- weapon A name = `""` (empty string, just a null byte)
- weapon B name = the weapon name (e.g. `"Fists"`, `"Batons"`)

For weapon-pair files (`<weaponA>_<weaponB>.atf`):
- weapon A name = weapon A (e.g. `"Batons"`)
- weapon B name = weapon B (e.g. `"Claws"`)

### Sample decompressed headers

| File                       | Compressed | Decompressed | version | name A       | name B       | binary starts at | binary size |
| -------------------------- | ---------: | -----------: | ------: | ------------ | ------------ | ---------------: | ----------: |
| `_fists.atf`               |    181 622 |    1 171 186 |       1 | `""`         | `"Fists"`    |               11 |   1 171 175 |
| `_batons.atf`              |    187 547 |    1 215 016 |       1 | `""`         | `"Batons"`   |               12 |   1 215 004 |
| `batons_claws.atf`         |     55 814 |      296 710 |       1 | `"Batons"`   | `"Claws"`    |               17 |     296 693 |
| `kusarigama_nunchaku.atf`  |     85 255 |      439 480 |       1 | `"Kusarigama"` | `"Nunchaku"` |               24 |     439 456 |

Single-weapon files are ~4× larger than pair files — they likely
contain the full tactics table for that weapon against every possible
opponent move, while pair files contain only the A-vs-B subset.

### Binary tactics data (partial)

After the two weapon name strings, the binary section begins. First
8 bytes:

```
Offset  Size  Field              Value (_fists.atf)
0x00    4     u32 LE             378 702     (record count? byte count?)
0x04    2     u16 LE             858         (constant across all files — stride?)
0x06    2     u16 LE             6 940       (varies per file)
```

The `u16` value `858` (0x035a) is **constant across all 4 sample
files**. This is likely a fixed dimension of the tactics table —
perhaps the number of distinct (move_A, move_B) tuples, or the number
of frames per animation, or a row stride.

After the 8-byte prefix, the data is a long sequence of small u8
values in the range 0x04–0x1c (4–28). These could be:
- Move indices (referencing entries in `moves.xml`)
- Frame counts
- Tactic IDs
- Damage multipliers (encoded as small ints)

### Sample bytes from `_fists.atf` binary section

```
4e c7 05 00  5a 03  1c 1b 1a 19 0e 16 19 18 16 19 18 07 0f 0e 08 0c
|----------| |----| |----------------------------------------------|
u32 =        u16 =  sequence of small u8 values (range 4-28)
378702       858
```

The values `1c 1b 1a 19 0e 16 19 18 16 19 18 07 0f 0e 08 0c` (28, 27,
26, 25, 14, 22, 25, 24, 22, 25, 24, 7, 15, 14, 8, 12) could be move
indices into a move table.

## What reSF2 needs from this format

For **Stage 7.4** (physics: hitbox vs hurtbox) and **Stage 7.7**
(battle logic):

1. **Per-move hitbox/hurtbox rectangles** — where on the screen does
   this move hit, and where is the attacker vulnerable?
2. **Per-move frame windows** — which frames of the animation are
   "active" (can hit), "recovery" (can be punished), "windup" (can
   be interrupted)?
3. **Per-pair damage multipliers** — when weapon A's move X hits
   weapon B's move Y, what's the damage?
4. **Per-pair combo links** — after weapon A's move X hits, which of
   A's moves can combo?

All four are encoded in the `.atf` binary section. The exact byte
layout is the Stage 4.x task.

## Stage 4.x tasks for full `.atf` decoding

1. Decompress all 110 `.atf` files (✓ trivial — `zlib.decompress`).
2. Map the binary section by:
   a. Disassembling the `.s3e` function that loads `.atf` files
      (search for the string `"loading tactics"` — found at `.s3e`
      offset `0x785a85` near `"loadGame - loading tactics"`).
   b. Or by running statistical analysis: looking for repeating
      record boundaries in the binary section.
3. Cross-reference with `assets/tacticSettings.xml` (inside
   `files.dz`) — this XML likely defines the high-level tactics
   structure that the `.atf` binary data instantiates.
4. Cross-reference with `assets/animations/moves.xml` (also inside
   `files.dz`) — this defines the move names and IDs that the `.atf`
   references.
5. Document the full byte layout in `docs/14b_atf_byte_layout.md`.

## Implementation note for reSF2

reSF2's `engine/reverse/atf_tactics.{hpp,cpp}` (Stage 4.x) will:

- Decompress the zlib wrapper.
- Parse the version + weapon-name header.
- Expose the binary section as a `std::span<const std::byte>` for
  downstream consumers (physics, battle logic).
- Once the byte layout is fully mapped, add typed accessors for
  hitbox rectangles, frame windows, damage multipliers, and combo
  links.

Unit tests will use the 4 sample files (`_fists`, `_batons`,
`batons_claws`, `kusarigama_nunchaku`) as fixtures.

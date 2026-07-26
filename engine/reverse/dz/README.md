# derbh (.dz) — reverse-engineering notes

**Status: solved.** Container format and both coders used by Shadow Fight 2 are
implemented natively in `engine/reverse/dz_reader.cpp` + `dz_coder.cpp`.
`tools/dzip.exe` is no longer needed at runtime, and neither is pre-extraction.

Verification: `tests/test_dz_archive.cpp` decodes all 120 files of `files.dz`
and all 557 of `animations.dz`, and byte-compares `files_list.xml`,
`forge.xml`, `moves.xml` and `animations_list.xml` against the extracted copies
committed under `assets/`. A Python cross-check decoded all 120 files of
`files.dz` byte-for-byte against `dzip.exe -d` output.

## Sources

| Binary | What it gave us |
|---|---|
| `download/dzip.exe` | Marmalade's own "Derbh commpress tool" — x86, unstripped enough to name every coder and every coder parameter. Primary source. |
| `reverse/binaries/ShadowFight2.s86` | `DzipFile::open` @ `0x102c9778`, `Derbh::open` @ `0x102ca66b` — confirms the game parses the container identically. |
| `reverse/binaries/libs3e_android.so` | `s3eCompression*` — a *different* subsystem, see the note at the bottom. |

Key dzip.exe addresses (image base `0x00400000`):

| Address | Role |
|---|---|
| `0x00417aa0` | coder registration — names, masks, parameter tables |
| `0x004010d0` | `DzipFile::DzipFile` — constructs the 4 coder objects |
| `0x00401d20` | `DzipFile::open` |
| `0x00401430` | `DzipFile::read` — walks a file's block chain |
| `0x00409d90` | `DZCoder::init` — reads the 10-byte coder header |
| `0x00409f50` | `DZCoder::openEntry` — reset models, init range decoder |
| `0x0040a2f0` | `DZCoder` main symbol loop |
| `0x004097e0` | offset symbol decode + LZ77 copy |
| `0x00408810` / `0x00408e70` | range decoder init / decode |
| `0x00408fe0` | model init; `0x004087b0` rescale; `0x004088a0` sum rebuild |

## Container layout

```
'DTRZ'                                  4
u16 num_files
u16 num_dirs                            index 0 is the implicit root
u8  version                             must be 0
num_files      NUL-terminated file names
num_dirs - 1   NUL-terminated directory paths, stored full ("assets\animations")
num_files chains of u16, each terminated by 0xFFFF:
    chain[0]   = directory index
    chain[1..] = block-table indices; the file is the concatenation of its blocks
u16 num_volumes
u16 num_blocks
num_blocks x 16 bytes:  { u32 offset, u32 comp_size, u32 uncomp_size, u32 flags }
num_volumes - 1 NUL-terminated volume names (multi-part archives)
per-coder headers, in coder registration order, for every coder whose mask
appears in the OR of all block flags
```

Notes that cost earlier sessions a lot of time:

* `offset` is an **absolute** file offset, not relative to a data section.
  Blocks are laid out contiguously and are **not** a single shared stream —
  each block is independently decodable.
* `comp_size` is only maintained by the DZ coder. For ZLib-coder blocks it
  mirrors `uncomp_size`; the deflate stream's own end marker is authoritative.
* The per-file table is **not** a fixed 6-byte record. It is the variable-length
  u16 chain above. Assuming 6 bytes/file put the block table one byte out of
  alignment, which is where the "overlapping offsets / streaming compressor"
  theory came from.
* `num_dirs` counts directory *slots*; only `num_dirs - 1` names are stored
  because index 0 is the root.

## Coders

Registered in `dzip.exe` `FUN_00417aa0`; the mask is the value returned by the
coder's `getMask` vtable slot (offset `+0x0c`).

| Mask | Name | Used by |
|---|---|---|
| `0x004` | `DZ Coder` | `files.dz` |
| `0x008` | `ZLib Coder` | `animations.dz`, `ZONE_*.dz` |
| `0x010` | `BZip Coder` | — |
| `0x080` | `Zero replace` | — |
| `0x100` | `Copy Coder (no compression)` | — |
| `0x200` | `LZMA Coder` | — |

The coder object exposes four function pointers inline (no vtable pointer):
`[0]` init, `[1]` readChunk, `[2]` openEntry, `[3]` getMask.

### ZLib Coder (0x008)

A bare 10-byte gzip header followed by a raw deflate stream and **no trailer**.
Inflate with `wbits = -15` after skipping the header.

### DZ Coder (0x004)

The only coder that writes an archive-level header: 10 bytes, which are coder
parameters `0x14..0x1d` verbatim. Every shipped archive uses the defaults:

```
10 01 08 03 03 07 01 07 03 0f
```

| Byte | Parameter | Default |
|---|---|---|
| 0 | `WinSize` | `0x10` |
| 1 | `Flags` | `1` |
| 2 | `OffsetTableSize` | `8` |
| 3 | `OffsetTables` | `3` |
| 4 | `OffsetContexts` | `3` |
| 5 | `RefLengthTableSize` | `7` |
| 6 | `RefLengthTables` | `1` |
| 7 | `RefOffsetTableSize` | `7` |
| 8 | `RefOffsetTables` | `3` |
| 9 | `BigMinMatch` | `0x0f` |

`init` rejects the archive unless `WinSize < 31`, `Flags < 4` and
`OffsetContexts <= 8`.

**Algorithm.** A carry-less (Subbotin) range decoder driving adaptive frequency
models. Each model is a complete binary tree in a flat `uint16` array: node `i`
has children `2i+1` / `2i+2`, internal nodes hold the sum of their left subtree,
leaves hold symbol frequencies. All frequencies start at 1 and are reset at the
start of every block.

Tree layout (`FUN_00408fe0`) for `n_sym` symbols: `n = 2*n_sym - 1`,
`n_internal = n_sym - 1`, `tree_off` = the largest `2^k - 1` that is `< n`,
`split = n_sym - (tree_off - n_internal)`. Symbol `s` lives at node
`tree_off + s` for `s < split`, otherwise `n_internal + (s - split)`.

Range decoder state is `low`, `code`, `range`; init sets `low = 0`,
`range = 0xFFFFFFFF` and reads 4 big-endian bytes into `code`. Per symbol:

```
r        = range / total
target   = (code - low) / r
          descend the tree, subtracting left-subtree sums, adding `inc`
          to every internal node taken
consumed = target - remainder
low     += consumed * r
range    = leaf_freq * r
leaf    += inc ; total += inc ; halve all leaves if total > max_total
renormalise while ((range + low) ^ low) <= 0xFFFFFF, pulling in bytes
```

Models:

* main — `0x202` symbols, `inc = 0x10`, `max_total = 0x10000`
* offset — `[OffsetContexts][OffsetTables]`, `1 << OffsetTableSize` symbols,
  `inc = 0x20 + 4*k` for table index `k`

Main alphabet:

| Symbol | Meaning |
|---|---|
| `0x000..0x0FF` | literal byte |
| `0x100..0x200` | match, `length = sym - 0xFE` (2..258) |
| `0x201` | end of block |
| `>= 0x202` | cross-entry reference (see below) |

Offset decoding picks the model row by `min(length - 2, OffsetContexts - 1)`,
then reads chunks of `OffsetTableSize` bits; the MSB of each chunk is a
"more chunks follow" flag, and successive chunks use successive tables
(clamped to `OffsetTables - 1`). The assembled value is a distance slot:
`raw < 4` selects one of four recent distances (moving it to front),
otherwise the distance is `raw - 3` and the recent list is shifted.

**Not implemented:** symbols `>= 0x202`. They only occur when some block carries
flag bit `0x1`, marking it as a shared-dictionary block that other blocks
reference (`FUN_00409ba0` / `FUN_00409970` / `FUN_00409ff0` / `FUN_0040a160`,
plus the primer model loaded by `FUN_00409630`). No Shadow Fight 2 archive uses
this. `dz_decode_block` returns empty if it ever sees one.

**Windowing.** The original decodes in windows of `max(MinBufSize, 1 << WinSize)`
bytes and resolves back-references reaching into the previous window through
`DZCoder+0x40`. Decoding a whole block into one contiguous buffer yields the
identical byte stream — the windowing is a memory bound only, and that is what
`dz_decode_block` does.

## Correction to earlier notes

`libs3e_android.so` `FUN_000489f8` / `FUN_00047adc` / `FUN_0004751c` are
`LzmaDec_DecodeToDic` / `LzmaDec_DecodeReal2` / `LzmaDec_TryDummy` — plain LZMA
from the LZMA SDK. Identifying markers: probability count
`0x300 << (lc + lp)` plus `0x736`, `tempBuf[20]` (`LZMA_REQUIRED_INPUT_MAX`),
`remainLen == 0x112` (`kMatchSpecLenStart`). `FUN_000620bc` reads a 13-byte
LZMA_ALONE header and calls `LzmaDec_Allocate` with 5 props bytes.

That is Marmalade's **`s3eCompression`** API, whose coder id 4 happens to be
LZMA. It is a different enum from the derbh coder mask, where `0x004` is the DZ
Coder. Chasing the s3e LZMA path for the derbh type-4 blocks was a dead end;
the `RefOffsetTables` / `RefLengthTables` description in the original notes was
the correct lead.

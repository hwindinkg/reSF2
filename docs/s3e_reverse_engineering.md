# reSF2 — S3E Binary Reverse Engineering Notes

## S3E Binary Format (ShadowFight2.s3e)

### Header Structure (from Marmalade SDK + S3ELoader)
```
Offset  Size  Field              Value
0x00    4     magic              "XE3U"
0x04    4     version            0x00042800 (4.40.0)
0x08    2     flags              0x000A (gcc, pie)
0x0A    2     arch               0x000C (x86_64 with VFP)
0x0C    4     fixupOffset        0x00001521
0x10    4     fixupSize          0x00043D30 (277,808 bytes)
0x14    4     codeOffset         0x00045251
0x18    4     codeFileSize       0x008042C8
0x1C    4     codeMemSize        0x00825D5C
0x20    4     sigOffset          0x00849519
0x24    4     sigSize            0x0000008C (140 bytes)
0x28    4     entryOffset        0x00000000
0x2C    4     configOffset       0x0000004C
0x30    4     configSize         0x000014D5
0x34    4     baseAddrOrig       0x4A000000
0x38    4     extraOffset        0x008495A5
0x3C    4     extraSize          0x00000128 (296 bytes)
0x40    4     extHeaderSize      0x0000000C
0x44    4     dataSegmentOffset  0x007B8000
0x48    4     isJuice            0x00000000
```

### Memory Layout
```
Virtual Address      File Offset          Size       Section
0x4A000000           0x45251              0x7B8000   Code (x86_64, PIE)
0x4A7B8000           0x7FD251             0x4C2C8    Data (GOT, globals)
0x4A8042C8           (BSS)                0x21A94    BSS (uninitialized)
```

### Fixup Table (Relocation Table)
Location: file offset 0x1521, size 277,808 bytes

Contains 4 sections:

#### Section 0 (type=0): External Symbol Names
- 347 strings (GL functions, s3e functions)
- Examples: s3eMallocBase, glBindRenderbuffer, glFramebufferTexture2D, etc.

#### Section 1 (type=1): Internal Relocations
- 67,461 entries
- Each entry is a 4-byte address pointing to a GOT entry in the data section
- GOT entries contain pre-patched absolute virtual addresses (0x4A000000 + offset)
- Example: GOT[0] at 0x7FD251 = 0x4A6D8D34 → string at file offset 0x71DF85

#### Section 2 (type=2): Empty (8 bytes of zeros)

#### Section 3 (type=4): Additional Relocations (2,136 bytes)

### String Table
- Located in the code section: file offsets 0x70xxxx - 0x79xxxx
- Strings are referenced via GOT pointers in the data section
- Code accesses strings through: MOV reg, [rip+offset_to_GOT] → GOT contains string pointer

### Key Engine Strings Found

#### Physics
- `12ModelPhysics` (0x781A69) — Main physics class (RTTI)
- `PhysicsFrameNumber` (0x780C59) — Per-frame physics step
- `16ConditionPhysics` (0x78058D) — Physics condition checker
- Node attributes: Mass, Fixed, Attenuation, Cloth, Weak, Collisible, Passive
- `LZF` (0x315121) — LZF compression (used for DZ archives)

#### Animation
- `14ModelAnimation` (0x77FE11) — Animation controller
- `13InfoAnimation` (0x7808A9) — Animation metadata
- `10MoveInside` (0x7808BB) — Pivot alignment system
  - `align.pivotID == -1` — Pivot validation
  - `moveInside is null` — Null check
- `17IntervalAnimation` (0x780A01) — Animation intervals
- `14IntervalAttack` (0x780A59) — Attack interval system
  - `StartFrame (%i) is outside of attack interval (%i-%i)`
  - `EndFrame (%i) is outside of attack interval (%i-%i)`
- Intervals: SelfUninterrupt, SemiUninterrupt, Uninterrupt
- `MidFrames`, `FirstFrame` — Frame offsets from moves.xml

#### Rendering (Cocos2d-x)
- `N7cocos2d8CCSpriteE` (0x76AF35) — CCSprite
- `N7cocos2d17CCSpriteBatchNodeE` — Sprite batch node
- `N7cocos2d13CCSpriteFrameE` — Sprite frame
- `N7cocos2d18CCSpriteFrameCacheE` — Sprite frame cache
- `textureRotated` (0x76B031) — Atlas rotation flag
- Shaders: ShaderPositionTexture_uColor, ShaderPositionTexture, ShaderPosition_uColor

#### Location
- `ImageLayer` (0x74B315) — Layer image class
- `setupBackground - unknownType: %i` (0x77A225) — Background setup
- `_backgroundPicture '%s' not load` (0x770141) — Background loading
- Layer types: type=1 (parallax), type=2 (models viewer)
- `Factor` — Parallax scroll factor

#### DZ Archive
- `dzip is attached successfully.` (0x78A48D)
- `dzip UNsuccessfull.` (0x78A4AD)
- `derbh` (0x797F3E) — Marmalade archive system name
- `decompress chunk` (0x793EB8)
- `inflate 1.2.3 Copyright 1995-2005 Mark Adler` (0x792A90) — Embedded zlib
- `LZF` (0x315121) — LZF compression (possibly used for DZ)

## DZ Archive Format

### Structure
```
Offset  Size  Description
0x00    4     Magic "DTRZ"
0x04    2     File count (120)
0x06    2     Folder count (104)
0x08    1     Root folder marker (0x00)
0x09    var   File names (null-terminated strings)
0x0EED  720   File attributes table (120 × 6 bytes: folder + index + flags)
0x11BD  1920  Location info table (120 × 16 bytes: offset + comp_size + uncomp_size + flag)
0x1955  var   Compressed data blocks
```

### Location Info Entry (16 bytes)
```
Offset  Size  Description
0x00    4     Data offset (absolute, in file)
0x04    4     Compressed size
0x08    4     Uncompressed size
0x0C    4     Flag (always 4 for all blocks)
```

### Compression
- Flag value: 4 (consistent across all 119 blocks)
- First byte of all blocks: 0x1D (29)
- **NOT** standard: zlib, gzip, raw deflate, LZ4, LZMA, bzip2
- **Possibly**: LZF compression (string found in s3e binary)
- **Possibly**: Marmalade custom "Derbh" compression
- Decompression function in s3e binary near "decompress chunk" strings
- s3e binary has embedded zlib 1.2.3 for HTTP/network decompression

### Attempts Tried
1. zlib (wbits=15) — ❌ incorrect header check
2. raw deflate (wbits=-15, -9 to -15) — ❌ invalid code lengths
3. gzip — ❌ not gzipped
4. LZMA (alone, raw) — ❌ corrupt input
5. LZ4 (block, frame) — ❌ corrupt input
6. bzip2 — ❌ invalid data stream
7. LZF (python-lzf, manual) — ❌ error in compressed data
8. Various skip offsets (0-16 bytes) — ❌ all failed

## MoveInside (pivot alignment) — byte-verified this session

`fcn.10165c10` (VA `0x10165c10` in `ShadowFight2.s86`, PE32 i386, ImageBase
`0x10000000`) is the MoveInside alignment entry, called from `playInfo`
(`fcn.10164fa0`) at `0x10165115` during per-frame animation setup.

Disassembly (verified by `objdump -d -M intel`):
```asm
10165c10: push esi
10165c11: mov  esi, ecx              ; esi = this (Model)
10165c14: mov  eax, [esi+0x20]       ; eax = this->animationInfo   (Model+0x20)
10165c17: mov  eax, [eax+0x94]       ; eax = animationInfo->moveInside  (+0x94)
10165c1d: mov  edi, [eax+0x70]       ; edi = moveInside->align.pivotID  (+0x70)
10165c20: cmp  edi, 0xffffffff       ; pivotID == -1 ?
10165c23: jle  0x10165c3e            ; yes -> warn & zero
; pivotID != -1 (valid node index):
10165c25: mov  ecx, [esi+0xdc]       ; ecx = this->[0xdc] (node-array owner)
10165c2b: mov  [esi+0x58], edi       ; this->pivotID_cached = pivotID   (Model+0x58)
10165c2e: call 0x10048b30            ; eax = node_array_base(this->[0xdc])
10165c33: mov  eax, [eax]            ; eax = *node_array_base  (deref)
10165c35: mov  eax, [eax+edi*4]      ; eax = node_array[pivotID]  (4-byte slot)
10165c39: mov  [esi+0x5c], eax       ; this->align_y = node_array[pivotID]  (Model+0x5c)
10165c3d: ret
; pivotID == -1:
10165c3e: push 0x105b21f8            ; "_animationInfo->moveInside->align.pivotID == -1"
10165c43: mov  [esi+0x5c], 0x0       ; this->align_y = 0
10165c4a: call 0x101472f0            ; s3eAssert/warn
```

Struct layout (byte-confirmed):
| Offset | Field | Container |
|--------|-------|-----------|
| Model+0x20 | animationInfo ptr | Model |
| animInfo+0x94 | moveInside ptr | animationInfo |
| moveInside+0x70 | align.pivotID (int32, -1 = none) | moveInside |
| Model+0x58 | pivotID cached | Model |
| Model+0x5c | align_y (float: node_array[pivotID].Y, or 0) | Model |

`Model::step` (`fcn.10161ad0`) reads `Model+0x58` (pivotID) at the call to
`fcn.10243750` (line 899 in `scripts/dz_model_step_decompiled.c`), which
returns a float used in Y-related math (`fstp dword [ebp-0x34]`).

### moves.xml ↔ binary mapping

moves.xml declares the pivot node per `<Template>` inside `<Align>`:
```xml
<Align Axis="X|Z">
  <Pivot Object="Nodes" Part="NHeel_2"/>   <!-- grounded moves: heel contact -->
  <Position Player="Me" Object="Pivot"/>
</Align>
```
or
```xml
<Align Axis="X|Z">
  <Pivot Object="Animation"/>              <!-- stance/anim-driven: pivotID = -1 -->
  <Position Player="Parent" Object="Animation"/>
</Align>
```

Distribution in `assets/animations/moves.xml`:
- 437 `<Pivot Object="Nodes" Part="NHeel_2"/>` (most grounded moves)
- 176 `NHeel_1`
- 61 `Object="Animation"` (stance, magic, missile — pivotID = -1, align_y = 0)
- 59 `Magic-Node2_1`
- 25 `NPivot` (likely air/jump moves)
- 14 `NNeck`, 13 `Ranged-Node2_1`, ...

### What is NOT yet byte-confirmed [HEURISTIC-TODO]

1. The exact formula that consumes `Model+0x5c` (align_y) to produce the
   render Y. `Axis="X|Z"` on most `<Align>` tags suggests MoveInside may
   align only X and Z, with Y driven by the animation NPivot or physics —
   not yet traced.
2. Whether `node_array[pivotID]` is a flat float array of node Y values
   (current assumption) or a struct (the `*eax` deref before indexing
   suggests a container-of-arrays layout).
3. The `Model+0xdc` "node-array owner" — likely the skeleton instance;
   `fcn.10048b30` is the accessor.

### reSF2 implementation (current, [HEURISTIC-TODO])

`render_body_model()` in `main.cpp` now:
- Parses `<Pivot Part="..."/>` into `MoveDef::moveinside_pivot_node`.
- When the active move has a node pivot, computes
  `y_adjust = floor_y - player_pos_y_ - pivot_node_ly + npivot_rest_y`
  so the named pivot node sits at `floor_y` (dojo = -193).
- Falls back to `FEET_FLOOR_OFFSET = 4` for `Object="Animation"` moves.
- Exponential-smooths the result (`y_smooth_alpha = 0.3`).

This grounds the pivot node (fixes the roll-float symptom when the active
move has a heel pivot) but is an approximation of the real consumption
formula, which remains to be byte-confirmed.

## DZ type 4 decoder — partial verification (this session)

The DZ type-4 decoder lives in `libs3e_android.so` (ELF32, ARM, 800 KB).
`objdump` on this host is x86-only, so capstone (pip-installed) was used
to disassemble.

### Verified [ORIGINAL]

Function at VA `0x389f8` exists and is ARM-mode (not Thumb). Prologue +
first ~40 instructions (capstone):
```asm
0x389f8: push {r4,r5,r6,r7,r8,sb,sl,fp,lr}
0x389fc: sub sp, sp, #0x2c
0x38a00: movw ip, #0xf8cb ; ip = 0xfffff8cb (constant, likely a mask/sentinel)
0x38a04: mov sb, r0        ; sb (r9) = arg0 = decoder context pointer
0x38a08: str r1, [sp, #0x1c]
0x38a10: str r3, [sp, #0x18]
0x38a14: ldr r1, [sp, #0x50]   ; arg4
0x38a18: ldr r3, [r2]          ; *arg2
0x38a1c: str r2, [sp, #0x24]
0x38a20: ldr r1, [r1]          ; *arg4
0x38a28: mov r3, #0
0x38a2c: str r3, [r2]          ; *arg2 = 0
0x38a34: str ip, [sp, #0x20]   ; stash sentinel
0x38a3c: str r3, [r2]          ; (redundant store)
0x38a40: ldr r3, [sb, #0x24]   ; ctx->in_pos  (context +0x24)
0x38a44: ldr r4, [sb, #0x28]   ; ctx->in_size (context +0x28)
0x38a48: ldr r2, [sp, #0x10]
0x38a4c: cmp r3, r4
0x38a50: moveq r3, #0
0x38a54: streq r3, [sb, #0x24]
0x38a5c: rsb r3, r3, r4        ; r3 = in_size - in_pos (remaining)
0x38a60: cmp r2, r3
0x38a64: ldrls r3, [sp, #0xc]
0x38a70: addls r8, r3, r2      ; r8 = consumed + something
0x38a7c: ldr r3, [sb, #0x48]   ; ctx->??? (context +0x48)
0x38a80: sub r2, r3, #1
0x38a84: cmp r2, #0x110        ; compare with 272
0x38a88: bhi #0x38b04
0x38a8c: ldr r2, [sp, #0xc]
0x38a90: ldr r5, [sb, #0x14]   ; ctx->??? (context +0x14)
...
```

Decoder context struct fields identified so far (byte-confirmed accesses):
| Offset | Likely field | Evidence |
|--------|--------------|----------|
| ctx+0x14 | window/history base | ldr r5, [sb,#0x14] early in decode loop |
| ctx+0x24 | input cursor (in_pos) | ldr/str, compared with ctx+0x28 |
| ctx+0x28 | input size (in_size) | compared with ctx+0x24; rsb computes remaining |
| ctx+0x48 | a count/limit (compared with 0x110=272) | sub r2,r3,#1; cmp r2,#0x110 |
| ctx+0x4c/0x50/0x54 | output/arg pointers | passed via sp+0x50 etc. |

### NOT verified [HEURISTIC-TODO]

1. The full decode loop (200+ ARM insns at 0x389f8 + the range coder at
   0x37adc + bit-tree at 0x3751c). Capstone can disassemble it, but
   tracing the algorithm end-to-end and proving byte-identical output on
   a real `.dz` block was not completed this session.
2. The current `engine/reverse/dz_decoder.cpp` is the PREVIOUS session's
   speculative LZMA-variant reimplementation. It has NOT been proven
   correct against the binary. Type-4 blocks still fall back to
   pre-extracted files on disk (`dz_reader::read_file` searches the
   filesystem when the decoder cannot produce output).
3. The container offsets in the older notes (0x0EED attribute table,
   0x11BD location table) do NOT match `assets/files.dz` for THIS APK —
   filenames end at 0x7c9, not 0x0EED. The `dz_reader.cpp` parse logic
   handles this correctly (it walks filenames by null-terminator rather
   than trusting fixed offsets), but the docs were stale. The 227-files
   count in the run log vs the u16 file_count=120 at offset 4 suggests
   `entries_` includes folder-prefixed entries; not yet reconciled.

### Next-session entry points

- Disassemble 0x389f8..0x38d00 fully (capstone), trace the decode loop,
  and map each ctx+offset to a named field.
- Disassemble 0x37adc (range coder) and 0x3751c (bit-tree) — these are
  the helpers; their structure confirms whether this is LZMA-like or a
  custom Marmalade derbh coder.
- Extract one type-4 block from `files.dz` (e.g. `settings.xml`,
  small) and diff the decoder's output against the pre-extracted file
  in `assets/`.

## Key Findings Summary

1. **Engine**: Marmalade SDK + Cocos2d-x (confirmed)
2. **Architecture**: x86_64 PIE (NOT ARM as initially assumed)
3. **Physics**: Verlet integration (ModelPhysics with Mass/Fixed/Attenuation attributes)
4. **Animation**: Root motion via MoveInside pivot alignment
5. **Rendering**: Cocos2d-x with TexturePacker atlases (textureRotated flag)
6. **Location**: ImageLayer with parallax Factor, Y-DOWN coordinates
7. **DZ Archive**: Custom compression (flag=4), possibly LZF or Derbh
8. **Binary is pre-patched**: GOT entries contain absolute virtual addresses

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

## Key Findings Summary

1. **Engine**: Marmalade SDK + Cocos2d-x (confirmed)
2. **Architecture**: x86_64 PIE (NOT ARM as initially assumed)
3. **Physics**: Verlet integration (ModelPhysics with Mass/Fixed/Attenuation attributes)
4. **Animation**: Root motion via MoveInside pivot alignment
5. **Rendering**: Cocos2d-x with TexturePacker atlases (textureRotated flag)
6. **Location**: ImageLayer with parallax Factor, Y-DOWN coordinates
7. **DZ Archive**: Custom compression (flag=4), possibly LZF or Derbh
8. **Binary is pre-patched**: GOT entries contain absolute virtual addresses

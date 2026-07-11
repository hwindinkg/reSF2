# .bin Animation Format

## Overview
Binary animation files containing per-frame bone transforms for fighter models.
Stored in `assets/animations/binary/` inside `animations.dz`.

## Filename convention (from .s3e binary)

The .s3e binary contains the format string `%s/%08x.bin`, meaning animation
files are named with **HEX-encoded IDs**, not text names. Example:
`assets/animations/binary/00001234.bin`

The mapping from animation name (like "air_punch") to hex ID is stored in
`assets/animations/moves.xml` (inside animations.dz). The file `air_punch.bin`
in the user's upload directory was likely renamed from its original hex name.

## Endianness

**ALL multi-byte values are LITTLE-ENDIAN** (verified by hex inspection of
`air_punch.bin`). This is consistent with the ARM/Android target platform.
An earlier version of this doc claimed big-endian — that was wrong and caused
the in-engine loader to produce garbage floats.

## File structure

```
u32 frame_count           # LITTLE-ENDIAN u32, number of frames
frame_count * FrameData   # 809 bytes per frame, packed back-to-back
```

**Verified**: air_punch.bin = 30746 bytes = 4 + 38 × 809 (38 frames).
axe_idle.bin = 33982 bytes = 4 + 42 × 809 (42 frames).

## Frame structure (809 bytes)

```
u8  type_flag             # byte 0 — 1 = keyframe, 5 = interframe
202 * float32 values      # bytes 1..808 — 202 LITTLE-ENDIAN floats
```

The type byte MUST be skipped when reading floats. Reading floats starting
at byte 0 corrupts float[0] with the type flag (1 or 5) as its high byte.

### Frame types

Both keyframes (type=1) and interframes (type=5) store **absolute values**,
not deltas. Verified by comparing consecutive frames:
- Frame 1 (keyframe) root_x = 179.83
- Frame 2 (interframe) root_x = 194.72 (absolute, not delta)
- Frame 3 (interframe) root_x = 213.15
- Frame 4 (keyframe) root_x = 236.53

The interpolation between frames is smooth, confirming both types store
full absolute data. The keyframe/interframe distinction likely affects
compression or seek behavior, not the data interpretation.

In air_punch.bin (38 frames): 18 keyframes, 20 interframes.

## Float layout (202 floats per frame)

**Verified** (read LE starting at byte 1 of each frame):

| Index | Meaning | Verified by |
|---|---|---|
| 0 | Always 0.0 (padding/timestamp) | Hex inspection |
| 1 | Root X (absolute world position, horizontal) | Matches doc: frame 0 = 179.83, frame 23 = 730.47 |
| 2 | Root Y (absolute world position, vertical) | Frame 0 = 220.66, frame 23 = 148.73 |
| 3..201 | Per-node transform data (199 floats) | **UNSOLVED — see below** |

## Verified skeleton Y-value matches

The following skeleton.xml rest-pose Y values were found at scattered float
indices in air_punch.bin frame 0 (tolerance 0.3):

| Node | Skeleton Y | .bin Float Value | Float Index |
|------|-----------|-----------------|-------------|
| NPivot | 169.5 | 169.4 | 55 |
| NToe_1 | 112.9 | 113.0 | 57 |
| NKnee_2 | 143.1 | 142.8 | 21, 75, 78, 111, 167, 191 |
| NHip_1 | 171.1 | 170.8 | 118, 145, 154 |
| NHip_2 | 167.9 | 168.1 | 166 |
| NKnee_1 | 187.2 | 186.8 | 160 |
| NAnkle_1 | 130.3 | 129.9 | 140 |
| NHead | 261.5 | 261.7 | 184 |
| NKnucklesS_1 | 219.7 | 219.4 | 16 |
| NKnucklesS_2 | 259.7 | 259.4 | 61 |
| NFingertipsS_1 | 217.9 | 217.8 | 94 |
| NToeS_1 | 111.8 | 111.9 | 93 |
| NToeTip_1 | 107.2 | 107.3 | 152 |
| NHeel_1 | 121.3 | 121.0 | 24, 63, 189 |

The Y values match within 0.5 units, confirming the float layout (LE, type
byte skipped). But the indices do not fit any clean stride.

## Verified absolute X-value matches

61 floats in frame 0 match `(root_x + skeleton_x)` — these are absolute
world X positions of skeleton nodes. Found at indices:
3, 7, 8, 11, 17, 19, 20, 23, 26, 27, 29, 31, 39, 43, 54, 55, 62, 66, 70,
72, 73, 74, 76, 77, 79, 80, 83, 90, 102, 109, 110, 116, 117, 120, 123,
125, 126, 129, 131, 132, 133, 134, 136, 137, 140, 149, 150, 151, 155,
157, 158, 164, 168, 169, 172, 174, 177, 178, 183, 185, 188

No regular stride fits these indices.

## Tested hypotheses (ALL FAILED)

| Hypothesis | Stride | Offset | Permutation | Matches |
|---|---|---|---|---|
| 50 nodes × 4 floats | 4 | 2 | (X, Y, Z, W) | 1/46 |
| 50 nodes × 4 floats | 4 | 2 | (Y, X, Z, W) | 0/46 |
| 100 nodes × 2 floats | 2 | 2 | (X, Y) | 3/46 |
| 100 nodes × 2 floats | 2 | 2 | (Y, X) | 0/46 |
| All (X,Y,Z) perms | 1-8 | 0-7 | all 6 perms | <5 each |

## Conclusion: non-regular layout

The .bin per-node data uses a **non-regular layout** that cannot be decoded
without a **node-mapping table** from the engine. This table is compiled into
the `ShadowFight2.s3e` binary and maps each float index to a specific
(node, component) pair.

## Key strings from .s3e binary analysis

Mining `ShadowFight2.bin` (8.7 MB extracted .s3e) for animation-related
strings revealed the engine's internal structure:

| Offset | String | Significance |
|---|---|---|
| 0x7808ed | `Animation load error` | Error in .bin loading function |
| 0x7809cd | `assets/animations/binary/` | Path to .bin files |
| 0x797e4a | `%s/%08x.bin` | Filename format (HEX ID) |
| 0x77fe11 | `14ModelAnimation` | Class name (length-prefixed) |
| 0x77fe35 | `In %s animation %d nodes, but in model only %u` | .bin contains node count |
| 0x77fe65 | `ModelAnimation::mirrorNodes - nodes not found: "%s"` | Mirror method |
| 0x77fec9 | `ModelAnimation::getPlayerAnimation - unknown type: %i` | Player anim lookup |
| 0x77ff35 | `ModelAnimation::playInfo - empty animation "%s"` | Play info method |
| 0x781589 | `9ModelNode` | Node class |
| 0x781559 | `14ModelMacroNode` | Macro node class |
| 0x7816f5 | `NodesCount` | XML field for node count |
| 0x77f081 | `count % nodeCount != 0` | Data must be multiple of node count |
| 0x77f4f5 | `pInterframes != _interframes.end()` | Separate interframe storage |
| 0x77fd89 | `12SubcontainerI8Vector3DE` | Uses Vector3D (3 floats) |
| 0x77fbcd | `MyNPivot %10.4f %10.4f %10.4f %10.4f` | Debug: 4 floats per node |
| 0x780e19 | `NoInterpolationFrames` | XML field for interpolation control |
| 0x78b3c5 | `assets/animations.dz` | Archive containing all .bin files |
| 0x78b445 | `assets/animations/moves.xml` | Animation index/manifest |

## Animation motion verification

The air_punch animation shows the model root (float[1]) moving:
- Frame 0: X=179.83 (starting position)
- Frame 10: X=396.54 (moving forward for punch)
- Frame 23: X=730.47 (full extension)
- Frame 37: X=613.83 (returning)

This matches a punch motion: move forward, extend, retract.

## What would be needed to solve the node mapping

1. **Disassemble the .s3e binary** (ARMv7 code) using Ghidra or IDA Pro
2. **Find the `ModelAnimation::load` function** — search for cross-references
   to the string `Animation load error` at offset 0x7808ed
3. **Trace the float-reading loop** — the function will iterate over floats
   and assign each to a specific node's transform component. The assignment
   order reveals the mapping table.
4. **Extract the node-name table** — the order in which nodes appear in the
   .bin differs from skeleton.xml order. This order is defined somewhere in
   the .s3e binary, likely in a static initialization function.

## Why root-motion alone doesn't work

Applying only `float[1]` (root X) and `float[2]` (root Y) as a whole-body
offset was attempted but caused problems:
- air_punch root X delta is ~550px — character flies across screen
- Loop boundary causes snapping back to start position
- Body stays in T-pose while moving (no limb animation)
- Result looks worse than a static character

Root motion is therefore **disabled** until per-node animation works.

## File sizes
- air_punch.bin: 30746 bytes = 4 + 38 × 809 (38 frames)
- axe_idle.bin: 33982 bytes = 4 + 42 × 809 (42 frames)

Both use 809 bytes per frame, confirming the format is consistent across
different animations.

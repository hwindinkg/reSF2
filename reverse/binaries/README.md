# Binary Files for Reverse Engineering

This directory contains the original game binaries needed for further
decompilation and reverse engineering of the Shadow Fight 2 engine.

## Files

| File | Size | Format | Architecture | Purpose |
|------|------|--------|-------------|---------|
| `ShadowFight2.s86` | 6.95 MB | PE32 DLL | Intel i386 | **Main game code (Windows)** — easiest to decompile |
| `s3e_native.dll` | 1.13 MB | PE32 DLL | Intel i386 | Marmalade SDK runtime (Windows) — input, audio, GL→D3D11 |
| `ShadowFight2_android.bin` | 8.29 MB | XE3U (S3E) | x86_64 PIE | Main game code (Android, decompressed) |
| `libs3e_android.so` | 800 KB | ELF SO | ARMv7 | Marmalade loader (Android) — contains DZ decompressor |

## How to Decompile

### ShadowFight2.s86 (Windows, recommended)

This is a standard PE32 DLL — no custom container, no decompression needed.

**objdump** (fastest, already works):
```bash
objdump -d -M intel --start-address=0xADDR --stop-address=0xADDR \
  reverse/binaries/ShadowFight2.s86
```

**Ghidra**:
1. File → Import → select `ShadowFight2.s86`
2. Format: PE (auto-detected), Language: x86:LE:32:default
3. Analyze with "Aggressive Instruction Finder" enabled
4. INT3 (0xCC) padding between functions may cause "bad instruction" warnings —
   these are NOT obfuscation, just alignment padding

**IDA Pro**:
- Load as PE32, Hex-Rays decompiler produces cleanest C output

### ShadowFight2_android.bin (Android)

Requires S3ELoader Ghidra plugin: https://github.com/knot126/S3ELoader

1. Clone S3ELoader, build with Ghidra dev kit
2. Copy .zip to `ghidra/Ghidra/Extensions/`
3. Import `ShadowFight2_android.bin` as "S3ELoader" format
4. Plugin applies relocations and creates proper memory blocks

### libs3e_android.so (Android loader)

Contains the DZ archive decompressor. Load in Ghidra as ELF ARM.

Key function: DZ decode at offset 0x389f8 (~250 ARM instructions).
Algorithm: arithmetic/range coding + 5-byte context + CRC32 hash + LZ77.

## Key Function Addresses (in ShadowFight2.s86)

| Function | Address | What it does |
|----------|---------|-------------|
| `Model::setNearestEnemy` | 0x101586F0 | Store enemy ptr at model+0x190 |
| `Model::getModelAlign` | 0x10159780 | Get facing reference (switch type 1-4) |
| `ModelAnimation::getPlayerAnimation` | 0x1016622A | Position update: `pos = mirrored * speed + old` |
| `ModelAnimation::playInfo` | 0x101650FC | Animation update chain (6 sub-calls) |
| `ModelAnimation::mirrorNodes` | 0x10164093 | Skeleton mirroring (XOR 0x80000000) |
| `Model::startAction` | 0x1015C540 | Start model action |
| `Model::setCurrentNode` | 0x1015B530 | Set current animation node |
| `IntervalAttack::getFactors` | 0x10115921 | Attack damage calculation |
| `interpolateNodes` | 0x10163F60 | Lerp: `new = old + alpha * (target - current)` |
| `updateAnimationFrame` | 0x10164F20 | Advance animation frame |
| `updateNodes` | 0x10165C10 | Update skeleton node positions |
| `applyInterpolation` | 0x10164C20 | Apply frame interpolation |
| `finalizePosition` | 0x101661D0 | Root motion / position finalization |

## Model Structure Offsets (from disassembly)

| Offset | Type | Field |
|--------|------|-------|
| +0x20 | ptr | Animation data |
| +0x40 | ptr | Current animation |
| +0x50 | byte | Flag (cleared on anim start) |
| +0x54 | byte | **Mirrored flag** (0xFF=-1 left, 0x01=+1 right) |
| +0x58 | uint | Animation frame index |
| +0x60 | uint | Animation sub-frame |
| +0x68 | uint | Animation type (0-4) |
| +0x7c | byte | Interpolation flag |
| +0x7d | byte | Node update flag |
| +0x7e | byte | Mirrored flag (copy, set by playInfo) |
| +0x7f | byte | Update flag |
| +0x80 | float | Current position X |
| +0x84 | float | Interpolated position X |
| +0x88 | float | Interpolated position X (copy) |
| +0xb4 | float | Speed/multiplier |
| +0xb8 | float | Position offset Y |
| +0xe0 | float | X coordinate (XOR 0x80000000 for mirror) |
| +0xe4 | float | X coordinate (alternate, non-mirrored) |
| +0xe8 | ptr | Animation container |
| +0x120 | ptr | Nearest enemy (copy) |
| +0x124 | uint | Some timer (set to 0x77359400 = 2000000000) |
| +0x190 | ptr | **Nearest enemy** (primary) |
| +0x220 | ptr | Model reference (case 3 in getModelAlign) |
| +0x45c | byte | Flag (checked in setCurrentNode) |
| +0x588 | ptr | Model reference (case 1 in getModelAlign) |
| +0x598 | ptr | Model reference |

## Animation Update Chain (from playInfo at 0x101650FC)

```
playInfo(model, arg):
  1. Get current animation from container (model+0xe8)
  2. Call updateAnimationFrame (0x10164F20) — advance frame
  3. Call updateNodes (0x10165C10) — update skeleton positions
  4. Call applyInterpolation (0x10164C20) — lerp between frames
  5. Call finalizePosition (0x101661D0) — root motion + facing
  6. If mirrored flag (arg): call mirrorNodes (0x10165D80)
```

## Related Documentation

- [S3E Function Analysis](../docs/s3e_function_analysis.md) — detailed disassembly
- [S3E Windows Binary RE](../docs/s3e_windows_binary_re.md) — binary format overview
- [Ghidra Decompilation Guide](../docs/ghidra_decompilation_guide.md) — setup instructions
- [Development Plan](../docs/DEVELOPMENT_PLAN.md) — RE methodology and roadmap

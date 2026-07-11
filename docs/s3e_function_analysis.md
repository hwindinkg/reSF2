# reSF2 — S3E Function Analysis (from disassembly)

## Source Binary: `Shadow Fight 2.s86` (PE32, i386, 6.95 MB)

The s86 binary is NOT packed or encrypted. objdump can disassemble it directly.
The Ghidra "bad instruction" warnings are due to anti-tamper junk bytes between
functions (0xCC INT3 padding), NOT actual obfuscation of the code itself.

## How to Get Readable Disassembly

### Method 1: objdump (works immediately, no setup)
```bash
objdump -d -M intel --start-address=0xADDR --stop-address=0xADDR \
  "Shadow Fight 2.s86"
```

### Method 2: Ghidra with correct settings
1. Import as PE32
2. Analysis options: enable "Aggressive Instruction Finder"
3. The "bad instruction" warnings are INT3 (0xCC) padding between functions —
   Ghidra sometimes misinterprets them. Use objdump for verification.

### Method 3: IDA Pro
Loads PE32 directly. Better at handling INT3 padding than Ghidra.

## Key Functions Disassembled

### Model::setNearestEnemy (0x101586F0 - 0x1015872D)

```asm
; Input: EBX = model, arg = enemy
101586F0: test eax,eax            ; check enemy ptr
101586FA: mov [ebx+0x190],ecx     ; store enemy at model+0x190
10158700: test ecx,ecx            ; if enemy != null
10158704: mov eax,[ecx]           ; vtable
10158709: call eax                ; enemy->isMirrored()
1015870F: push "Model::setNearestEnemy"  ; DEBUG LOG ONLY
1015871C: mov eax,[ebx+0x190]     ; enemy ptr
10158722: mov [ebx+0x120],eax     ; copy to model+0x120
1015872D: ret 4
```

**Finding:** The string push at 0x1015870F is ONLY for debug logging (when
enemy.isMirrored() == true). The actual function logic is the two MOV
instructions storing the enemy pointer.

### Model::getModelAlign (0x10159780 - 0x101597D8) — FACING DIRECTION

```asm
; Input: arg = type (1-4), ECX = model
; Switch on type-1 (0-3):
case 0: return model (self)
case 1: return model+0x588
case 2: return model+0x190       ; NEAREST ENEMY
case 3: return model+0x220
```

**Finding:** The game gets the facing reference via `getModelAlign(type=2)`
which returns the nearest enemy pointer (stored at model+0x190 by
setNearestEnemy). The game then compares X coordinates of the player and
enemy to determine facing direction.

### ModelAnimation::playInfo (0x101650FC area) — ANIMATION UPDATE

```asm
101650FC: mov ecx,[ebx+0x20]       ; animation data
10165100: lea edi,[ebx+0xe8]        ; animation container
10165107: call 0x10104980           ; get current animation
1016510E: call 0x10164f20           ; update animation frame
10165115: call 0x10165c10           ; update nodes
1016511D: call 0x10164c20           ; apply interpolation
10165124: call 0x101661d0           ; finalize position
10165129: mov al,[ebp+0x10]         ; mirrored flag
1016512C: mov [ebx+0x7e],al         ; store mirrored flag
1016512F: test al,al                ; if mirrored
10165135: call 0x10165d80           ; apply mirror to nodes
```

**Finding:** Animation update calls a chain of functions:
1. Get current animation from container (model+0xe8)
2. Update frame (0x10164f20)
3. Update nodes (0x10165c10)
4. Apply interpolation (0x10164c20)
5. Finalize position (0x101661d0)
6. If mirrored flag (from arg, stored at model+0x7e), apply mirror (0x10165d80)

### ModelAnimation::mirrorNodes (0x10164093 area)

The function iterates over nodes and applies mirroring. From disassembly:
```asm
; Loop over nodes (ebx = start, ecx = end)
101640F8: sar eax,0x2              ; count = (end - start) / 4
10164125: push [ebp+0x8]           ; push mirror flag
1016412A: call 0x100462e0          ; shouldMirror(node, flag)
10164132: test al,al               ; if node should be mirrored
10164134: je skip                  ; skip if not
; ... apply X negation (XOR 0x80000000 pattern seen in getPlayerAnimation)
```

### ModelAnimation::getPlayerAnimation (0x1016622A area) — POSITION UPDATE

From Ghidra decompilation + disassembly:
```c
// At case 2 of switch (mirrored case):
uVar12 = *(uint *)(model + 0xe0);    // load X as uint
uVar12 = uVar12 ^ 0x80000000;        // XOR sign bit = negate float
*(uint *)(local - 0x34) = uVar12;    // store negated X

// Position update formula:
pos_x = (float)mirrored_flag * speed + old_pos_x;
model->position = pos_x;
```

**Finding:** Float negation via XOR 0x80000000. This is the standard IEEE 754
trick to negate a float. Our `facing_right ? delta : -delta` is equivalent.

## Model Structure Offsets (from disassembly)

| Offset | Type | Field |
|--------|------|-------|
| +0x20 | ptr | Animation data |
| +0x50 | byte | (cleared to 0 on animation start) |
| +0x54 | byte | Mirrored flag (0=right, 1=left) |
| +0x68 | uint | Animation type (0-4, switch) |
| +0x7e | byte | Mirrored flag (copy, set by playInfo) |
| +0x80 | float | Updated position X (result of pos = dir * speed + old) |
| +0xb4 | float | Speed/multiplier |
| +0xb8 | float | Position offset Y |
| +0xe0 | float | X coordinate (negated via XOR 0x80000000 for mirror) |
| +0xe8 | ptr | Animation container (list of animations) |
| +0x120 | ptr | Nearest enemy pointer (copy) |
| +0x190 | ptr | Nearest enemy pointer (primary) |
| +0x220 | ptr | Some model reference (case 3 in getModelAlign) |
| +0x45c | byte | Flag checked in setCurrentNode (skip if non-zero) |
| +0x588 | ptr | Some model reference (case 1 in getModelAlign) |
| +0x598 | ptr | Some model reference (returned by function at 0x10159770) |

## How Auto-Facing Works (from disassembly)

1. `Model::setNearestEnemy(enemy)` stores enemy at model+0x190 and model+0x120
2. When facing needs to update, game calls `Model::getModelAlign(type=2)`
3. This returns the enemy pointer (model+0x190)
4. Game compares player X with enemy X:
   - If enemy.x > player.x → face right (mirrored = 0)
   - If enemy.x < player.x → face left (mirrored = 1)
5. When mirrored flag changes, `ModelAnimation::mirrorNodes` is called
6. `mirrorNodes` iterates over skeleton nodes and negates X via XOR 0x80000000
7. `ModelAnimation::getPlayerAnimation` applies the position update:
   `pos = (float)mirrored_flag * speed + old_pos`

## Root Motion Source

The position update formula `pos = mirrored_flag * speed + old_pos` is NOT
the root motion system. This is likely a screen-position adjustment based
on facing direction.

The actual root motion comes from the **MoveInside** system, which aligns
the character's NPivot with the animation's NPivot trajectory. The .bin
animation files store absolute NPivot positions per frame, and the game
uses these to displace the character.

## How to Get Better Decompilation

### Option 1: IDA Pro (best results)
- Load s86 as PE32
- IDA handles INT3 padding better than Ghidra
- Hex-Rays decompiler produces cleaner C code

### Option 2: Ghidra with fixes
1. After analysis, run script to clear all INT3 (0xCC) padding
2. Re-analyze from function entry points
3. Use "Aggressive Instruction Finder" option

### Option 3: Binary Ninja
- Good middle ground between Ghidra and IDA
- Handles PE32 well
- Has decompiler addon

### Option 4: Dynamic analysis (x32dbg/Cheat Engine)
- Set breakpoint at function entry (NOT at string push)
- For setNearestEnemy: breakpoint at 0x101586F0 (function start)
- For getModelAlign: breakpoint at 0x10159780
- For playInfo: breakpoint at 0x101650FC
- Step through (F8) to see actual register values and data flow

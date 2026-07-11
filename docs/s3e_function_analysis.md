# reSF2 — S3E Function Analysis (from disassembly)

## setNearestEnemy (0x101586F0 - 0x1015872D)

```asm
; Input: EBX = model pointer, argument = enemy pointer
101586F0: test eax,eax           ; check enemy ptr
101586F2: je  0x101586F8
101586F4: mov ecx,[ecx]          ; vtable dereference
101586F6: jmp 0x101586FA
101586F8: xor ecx,ecx            ; null enemy
101586FA: mov [ebx+0x190],ecx    ; store enemy at model+0x190
10158700: test ecx,ecx           ; if enemy != null
10158702: je  0x1015871C
10158704: mov eax,[ecx]          ; vtable
10158706: mov eax,[eax+0x4]      ; method index 1 (isMirrored)
10158709: call eax               ; call enemy->isMirrored()
1015870B: test al,al             ; if enemy is mirrored
1015870D: je  0x1015871C
1015870F: push "Model::setNearestEnemy"  ; DEBUG LOG ONLY
10158714: call log_function
10158719: add esp,4
1015871C: mov eax,[ebx+0x190]    ; load enemy ptr
10158722: mov [ebx+0x120],eax    ; copy to model+0x120
10158728: pop edi
10158729: pop ebx
1015872D: ret 4
```

**Key findings:**
- Enemy pointer stored at BOTH model+0x190 and model+0x120
- String "Model::setNearestEnemy" is ONLY for debug logging (when enemy.isMirrored() == true)
- This is why breakpoint at 0x1015870F didn't trigger — it only fires when the ENEMY is mirrored, not when the player turns
- The function is called with 1 argument (ret 4), likely the enemy model pointer

## getPlayerAnimation (0x10166220 area)

```asm
; At 0x101662F7: facing/mirroring check
101662F7: mov al, [esi+0x54]         ; read mirrored flag from model+0x54
101662FA: mov [ebp-0xd], al          ; save to local
101662FD: lea eax, [edi+0x88]        ; enemy facing data
10166303: push 0x103877FC            ; some constant
10166308: push eax                   ; enemy facing ptr
10166309: call FUN_1000cc00          ; check enemy facing direction
1016630E: movzx ecx, al              ; result (0 or 1)
10166316: cmp byte [ebp-0xd], 1      ; is player mirrored?
1016631A: sete al                    ; al = (player_mirrored == 1)
1016631D: cmp eax, ecx              ; compare player facing with enemy facing
```

**Key findings:**
- Model+0x54 = byte, mirrored flag (0 = facing right, 1 = facing left)
- The game compares player's mirrored flag with enemy's facing direction
- XOR 0x80000000 is used to negate floats (flip sign bit) — this is how X coordinates are mirrored

## Mirror System (from getPlayerAnimation + Ghidra)

From the Ghidra decompilation of getPlayerAnimation:
```c
// At case 2 of switch:
uVar12 = *(uint *)(unaff_ESI + 0xe0);  // load X coordinate as uint
uVar12 = uVar12 ^ 0x80000000;          // XOR sign bit = negate float
*(uint *)(unaff_EBP - 0x34) = uVar12;  // store negated X

// At case 4:
uVar5 = *(uint *)(unaff_ESI + 0xe0);   // load X
uVar5 = uVar5;                          // (no XOR = keep original)
```

**How mirroring works:**
1. Float X coordinates are stored as IEEE 754
2. XOR with 0x80000000 flips the sign bit → negates the float
3. `node.x = -node.x` for all nodes when character faces left
4. NPivot X is also negated, so root motion direction reverses naturally

**Implication for reSF2:**
Our approach of using `facing_right_ ? delta : -delta` is correct.
The game does the same thing: negate X coordinates when mirrored.

## Float Negation Pattern

The game uses `XOR 0x80000000` to negate floats:
```asm
; Negate float in EAX:
xor eax, 0x80000000    ; flip sign bit of IEEE 754 float
```

This is equivalent to `float f = -f;` in C/C++.

## Model Structure Offsets (from disassembly)

| Offset | Type | Field |
|--------|------|-------|
| +0x54 | byte | Mirrored flag (0=right, 1=left) |
| +0x68 | uint | Animation type (0-4, switch in getPlayerAnimation) |
| +0x88 | ptr | Enemy facing data |
| +0x120 | ptr | Nearest enemy pointer (copy) |
| +0x190 | ptr | Nearest enemy pointer (primary) |
| +0xe0 | float | X coordinate (for mirroring via XOR) |
| +0xe4 | float | X coordinate (alternate, for non-mirrored case) |
| +0x6c | uint | Animation sub-type |
| +0xb0 | uint | Model type |
| +0xb4 | float | Speed/multiplier |
| +0xb8 | float | Position offset |
| +0x80 | float | Updated position (result of pos = dir * speed + old_pos) |

## Position Update Formula (from getPlayerAnimation)

From Ghidra decompilation:
```c
fVar13 = (float)(int)*(char *)(unaff_ESI + 0x54) * *(float *)(unaff_EDI + 0xb4) +
         *(float *)(unaff_EBP + -0x28);
*(float *)(unaff_EBP + -0x28) = fVar13;
fVar2 = *(float *)(unaff_EDI + 0xb8);
fVar3 = *(float *)(unaff_EBP + -0x24);
*(float *)(unaff_ESI + 0x80) = fVar13;
*(float *)(unaff_EBP + -0x24) = fVar2 + fVar3;
```

Translated:
```c
// pos = mirrored_flag * speed + old_pos
pos_x = (float)mirrored_flag * speed_x + old_pos_x;
model->position = pos_x;
// pos_y += offset_y
pos_y = offset_y + old_pos_y;
```

**Key insight:** When `mirrored_flag` is 0 (facing right), `pos = 0 * speed + old_pos = old_pos` (no change).
When `mirrored_flag` is 1 (facing left), `pos = 1 * speed + old_pos` (applies speed in the mirrored direction).

Wait, this doesn't match — `mirrored_flag` is a byte (0 or 1), so `(float)(int)mirrored_flag` is 0.0 or 1.0.
This means: `pos = (0.0 or 1.0) * speed + old_pos`.

Actually, looking more carefully, this is likely:
- When NOT mirrored: `pos = 0.0 * speed + old_pos` → no displacement
- When mirrored: `pos = 1.0 * speed + old_pos` → adds speed

This suggests the position update only applies when the character IS mirrored, which doesn't make sense for normal movement. The actual root motion likely comes from the MoveInside system (NPivot alignment), not from this position update formula.

This formula might be for a different purpose — perhaps adjusting the model's screen position based on facing, not root motion.

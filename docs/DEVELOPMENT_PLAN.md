# reSF2 — Development Plan

## Current State (Stage 9, July 2026)

### Working ✅
- Character rendering (82 capsules + 29 triangles as silhouette)
- Skeletal animation (38 animations from .bin files)
- Animation interpolation (smooth blending between frames)
- Root motion X (absolute positioning, +66/step loop, +404/roll)
- Root motion Y (absolute offset from frame 0, smooth decay on landing)
- Mirror/facing (locked at animation start, XOR 0x80000000 pattern)
- Verlet physics for punching bag
- Hit detection (moves.xml Attack intervals, 70px threshold)
- Combat: Punch (O), Kick (P) with direction modifiers
- Special moves: Jump, FrontFlip, BackFlip, ForwardRoll, BackRoll
- Duck (S hold), Block (automatic)
- Scene/State Manager (9 scenes: Boot→Loading→MainMenu→Map→Dialogue→Battle→Results)
- Dojo location rendering (parallax background)
- HUD (gold, energy, level bar)
- Scroll/parchment menu
- Save system (JSON stub)

### Broken ❌
- **Animation speed**: might be 2x too fast (anim_speed_ = 30, need to verify)
- **Teleport-back**: character sometimes returns to animation start point
- **Bag Y position**: bag hangs slightly too high
- **Camera Y**: shows outside location (cam_y_ = 0, should be -50)
- **DZ archive decompression**: streaming compression not implemented
- **No real enemy**: punching bag only (no AI opponent)
- **No real dialogue/map/shop**: stubs only
- **Rotated textures**: profile icon + some location backgrounds

---

## Phase 1: Fix Core Movement & Camera (CURRENT PRIORITY)

### 1.1 Fix animation speed
- **Problem**: animations appear 2x too fast
- **Root cause**: `anim_time_ += dt * anim_speed_ / 30.0f` with `anim_speed_ = 30`
  gives `anim_time_ += dt`, then `frame_f = anim_time_ * 30` = 30 frames/sec.
  This is correct for 30fps animation at 60fps render.
- **Action**: verify with frame counting. If still too fast, try anim_speed_ = 15
  (half speed) or check if dt is being double-counted.
- **Original reference**: s3e binary uses `s3eTimerGetMs()` for frame timing.
  The game runs at 60fps physics, 30fps animation, with interpolation.

### 1.2 Fix teleport-back
- **Problem**: character sometimes returns to animation start point
- **Root cause**: when switching from root-motion animation to idle,
  `step_start_player_x_` is saved from `player_pos_x_` which was set by
  the previous animation's absolute positioning. If the previous animation
  ended at displacement X, `player_pos_x_ = step_start + X`. When idle
  starts, `step_start_player_x_ = player_pos_x_` (correct). But if idle
  is NOT a root-motion animation, `update_animation()` doesn't update
  `player_pos_x_`, so it stays at the last value. This should be fine.
- **Potential issue**: `play_animation()` is called from `host_update_gameplay()`
  BEFORE `update_animation()`. So the sequence is:
  1. play_animation("fists_idle") → saves step_start_player_x_ = current pos
  2. update_animation() → for idle (non-root-motion), doesn't touch player_pos_x_
  3. Next frame: play_animation("step_forward") → saves step_start = current pos
  4. update_animation() → sets player_pos = step_start + displacement
- **This should work.** The teleport might come from `facing_right_` changing
  between play_animation (saves anim_facing_right_) and the next frame's
  auto-facing update. Need to verify.

### 1.3 Fix camera Y
- **Problem**: camera at Y=0 shows area above the ceiling
- **Fix**: cam_y_ = -50.0f (shows floor and character properly)
- **Already applied** in this session.

### 1.4 Fix bag Y position
- **Problem**: bag hangs slightly too high
- **Analysis**: bag_cy = enemy_y + 81 = -105 + 81 = -24. Node12 (ceiling
  attachment) at world Y = -24 + (335-109) = 202. Ceiling at Y=202. Correct.
- **Possible issue**: y_adjust_smoothed_ for the player might make the
  character appear lower than expected, making the bag look relatively
  higher. Or the bag's visual rendering uses different Y than Verlet physics.
- **Action**: verify render_punching_bag() Y coordinate vs Verlet Y.

---

## Phase 2: Enemy Implementation

### 2.1 Load enemy character model
- Load `skeleton.xml` + `body.xml` for a second character instance
- Position at `location_->enemy_x, location_->enemy_y`
- Render with different color (e.g., red silhouette vs player's dark)

### 2.2 Enemy AI (basic)
- States: idle, approach, attack, retreat, block
- Simple state machine:
  - If player far: approach (step_forward)
  - If player close: attack (random punch/kick)
  - If player attacking: block (30% chance) or retreat (20%)
  - Cooldown between actions (500ms-1500ms)

### 2.3 Enemy hit detection
- Reuse player hit detection logic
- Attack intervals from moves.xml
- Enemy HP bar in HUD

### 2.4 Enemy root motion
- Same absolute positioning approach as player
- Enemy faces player (mirror based on player position)

---

## Phase 3: DZ Archive Decompression

### 3.1 Approach: Ghidra decompilation of DZ decoder
- Load `Shadow Fight 2.s86` into Ghidra/IDA as PE32
- Find DZ decode function via string xrefs:
  - "decompress chunk" string
  - "dzip" string
  - "derbh" string
- Decompile the function at the xref location
- Port the algorithm to C++ (clean-room)

### 3.2 Algorithm (from ARM analysis)
- Arithmetic/range coding with 32-bit range
- 5-byte context window (last 5 decoded bytes)
- CRC32 hash for context table lookup
- LZ77-style match references
- Streaming: entire data section is one continuous stream

### 3.3 Integration
- Add `engine/reverse/dz_decompressor.cpp`
- Load .dz archives at startup
- Extract XML files for dialogue, map, shop data

---

## Phase 4: Game Content

### 4.1 Dialogue system
- Parse dialogue XML from DZ archives
- Render dialogue box with speaker portrait + text
- Advance on click/Space (already implemented in DialogueScene)
- Portraits from `assets/1536/image/characters/`

### 4.2 Map/level selection
- Parse story.xml from DZ archives
- Render map with act/episode/level buttons
- Click to select level → transition to Dialogue → Battle

### 4.3 Shop
- Parse shop XML from DZ archives
- Render shop items (weapons, armor)
- Buy with gold currency

### 4.4 Save/Load
- JSON format (already stubbed)
- Track: completed levels, currency, equipment, player stats
- Auto-save on battle results

---

## Phase 5: Polish

### 5.1 Fix rotated textures
- Profile menu icon: test alternative rotation formulas in Python
- Location backgrounds: verify `textureRotated` flag handling
- Compare with original game screenshots

### 5.2 Audio
- s3eAudio API (from s3e_native.dll)
- Background music (from assets)
- SFX for hits, steps, UI clicks

### 5.3 Particles/effects
- Hit spark effect on successful hit
- Dust particles on step/roll
- Screen shake on heavy hits

### 5.4 Multiplayer (future)
- s3eSocket API (from s3e_native.dll)
- SmartFox2X protocol (from S3E imports)
- Online battles

---

## Reverse Engineering Methodology

### How to find and port original game logic

#### Step 1: Find the function via string xrefs
```bash
# Search for key strings in the binary
grep -aoE 'ClassName::MethodName' "Shadow Fight 2.s86" | sort -u

# Find the string's virtual address
python3 scripts/find_s86_functions.py

# Disassemble around the xref
objdump -d -M intel --start-address=0xADDR --stop-address=0xADDR+0x100 \
  "Shadow Fight 2.s86"
```

#### Step 2: Understand the assembly
- Look for `mov [ebx+OFFSET]` patterns → struct field access
- Look for `xor eax, 0x80000000` → float negation (mirroring)
- Look for `call` chains → function composition
- Look for `switch` via `jmp [eax*4+table]` → enum dispatch
- Look for `imul` with magic constants → division optimization

#### Step 3: Map to C++ structs
```cpp
// From disassembly: mov eax, [ecx+0x190]  ; nearest enemy
struct Model {
    // ...
    void* nearest_enemy;  // +0x190
    void* nearest_enemy_copy;  // +0x120
    uint8_t mirrored;  // +0x54
    // ...
};
```

#### Step 4: Port the algorithm (clean-room)
- Write C++ that produces the same BEHAVIOR
- Do NOT copy assembly 1:1
- Use standard C++ idioms (structs, vectors, etc.)
- Document the original assembly in comments

#### Step 5: Verify
- Compare output with original game (screenshots, behavior)
- Use Cheat Engine breakpoints for runtime verification
- Check edge cases (facing left, at boundaries, etc.)

### Key function addresses (already found)

| Function | Address | Purpose |
|----------|---------|---------|
| setNearestEnemy | 0x101586F0 | Store enemy pointer |
| getModelAlign | 0x10159780 | Get facing reference |
| getPlayerAnimation | 0x1016622A | Animation selection + position |
| playInfo | 0x101650FC | Animation update chain |
| mirrorNodes | 0x10164093 | Skeleton mirroring |
| startAction | 0x1015C540 | Start model action |
| setCurrentNode | 0x1015B530 | Set current animation |
| IntervalAttack::getFactors | 0x10115921 | Attack damage calc |

### Model structure offsets (from disassembly)

| Offset | Type | Field |
|--------|------|-------|
| +0x54 | byte | Mirrored flag (0=right, 1=left) |
| +0x68 | uint | Animation type (0-4) |
| +0x7e | byte | Mirrored flag (copy) |
| +0x80 | float | Updated position X |
| +0xb4 | float | Speed/multiplier |
| +0xe0 | float | X coordinate (XOR for mirror) |
| +0xe8 | ptr | Animation container |
| +0x120 | ptr | Nearest enemy (copy) |
| +0x190 | ptr | Nearest enemy (primary) |
| +0x588 | ptr | Model reference (case 1) |
| +0x598 | ptr | Model reference |

### Animation update chain (from playInfo disassembly)

```
playInfo:
  1. get current animation from container (model+0xe8)
  2. call 0x10164f20 — update animation frame
  3. call 0x10165c10 — update skeleton nodes
  4. call 0x10164c20 — apply interpolation
  5. call 0x101661d0 — finalize position (root motion)
  6. if mirrored: call 0x10165d80 — apply mirror to nodes
```

### Auto-facing logic (from disassembly)

```
1. setNearestEnemy(enemy) → model+0x190 = enemy
2. getModelAlign(type=2) → returns enemy pointer
3. Compare player X with enemy X
4. If enemy.x > player.x → mirrored = 0 (face right)
5. If enemy.x < player.x → mirrored = 1 (face left)
6. mirrorNodes() → negate X of all nodes (XOR 0x80000000)
7. getPlayerAnimation() → pos = mirrored * speed + old_pos
```

### Root motion (from MoveInside system)

The game uses a MoveInside system that aligns the character's NPivot
with the animation's NPivot trajectory. The .bin animation files store
absolute NPivot positions per frame.

```
For each frame:
  1. Read NPivot X,Y from .bin at current frame
  2. Compute displacement = NPivot[frame] - NPivot[frame 0]
  3. Apply displacement to character world position
  4. If mirrored: negate X displacement
  5. For looping animations: commit cycle displacement at wrap
```

This is exactly what our `update_animation()` does with absolute positioning.

---

## Build & Test

### Windows (user's machine)
```bash
build.bat  # cmake + MSVC
resf2_app.exe --assets <path_to_sf2_assets>
```

### Linux compile check
```bash
bash scripts/verify_main_compile.sh
```

### Key test scenarios
1. Hold D → smooth forward movement, +66 per step loop, no teleport
2. Hold A → smooth backward movement, -66 per step loop
3. W → jump (Y rises then returns to 0)
4. W+D → front flip (X displacement + Y arc)
5. S+D → forward roll (large X displacement, stays on ground)
6. O → high punch (hit detection on bag)
7. D+O → heavy punch (different animation)
8. S+P → sweep (low kick)
9. Walk past enemy → character turns to face enemy
10. All scene transitions work (Menu→Map→Dialogue→Battle→Results→Menu)

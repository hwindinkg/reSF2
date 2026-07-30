# Frida Block Logic Reverse Engineering Report

## Date: 2026-07-30
## Device: Redmi 6A (id=684006127d29)
## Game: Shadow Fight 2 v2.46.0 (com.nekki.shadowfight, PID 7821)
## Binary: ARM 32-bit, loaded at 0x8f35f000 (8.18 MB from OBB via S3E engine)

---

## 1. Binary Layout (ARM)

| Region | Offset Range | Description |
|--------|-------------|-------------|
| Code (ARM) | 0x000000 - 0x700000 | ~7MB of ARM-mode code, 144+ function prologues in first 1MB |
| String data | 0x700000 - 0x760000 | Read-only strings (Block, Tactic, Interval, etc.) |
| Pointer tables | 0x7C0000 - 0x7D0000 | Data structures referencing strings |
| Game struct data | 0x7D0000 - 0x830000 | Runtime game state |

**Total dumped:** First 2MB (8 chunks × 256KB) saved to `reverse/binaries/arm_dump/`

---

## 2. Key Function Addresses (ARM)

| Offset | Call Rate | Description |
|--------|-----------|-------------|
| `0x2f0e0` | 921/s (13815/15s) | **Main game loop** — called per entity per frame |
| `0x692464` | 246/s (3684/15s) | **Input dispatcher** — screen coordinate polling |
| `0x691ef0` | 184/s (2763/15s) | **Entity/model processor** — per-entity updates |
| `0x11f0a0` | parent | Parent function calling entity processor |
| `0x69251c` | parent | Parent of input dispatcher |
| `0x68f194` - `0x694f18` | varies | Hot region functions (rendering/event pipeline) |

**Note:** During idle/menu state, only 11 out of 92 hooked functions were called.
AI decision functions activate ONLY during active combat.

---

## 3. Block/AI String Map

All strings confirmed readable in the ARM binary at these offsets from `0x8f35f000`:

### Defense/Block Strings
| Offset | String | Context |
|--------|--------|---------|
| 0x73f8e0 | "Block" | Interval type name |
| 0x740a38 | "UseDefense" | Defense evaluation section |
| 0x740a64 | "BlockChance" | Block probability calculator |
| 0x740a58 | "DodgeChance" | Dodge probability calculator |
| 0x740b8c | "CounterFactor" | Factor: enemy attacking state |
| 0x740b9c | "DamageFactor" | Factor: damage taken |
| 0x740c24 | "HitFactor" | Factor: recently hit |
| 0x740bd0 | "AnimationFramesFactor" | Factor: animation duration |
| 0x743928 | "BodyDefense" | Defense attribute |
| 0x74d728 | "BlockDamage" | Counter-damage on block |
| 0x74d884 | "BlockDefense" | Defense bonus while blocking |
| 0x74d870 | "BlockDamageFactor" | Block damage multiplier |

### Interval System Strings
| Offset | String | Context |
|--------|--------|---------|
| 0x741648 | "Uninterrupt" | Strict uninterruptible state |
| 0x74165c | "Invulnerable" | Damage immunity state |
| 0x74236a | "IntervalAttack" | Attack interval handler |
| 0x741dae | "ConditionInterval" | Interval condition checker |
| 0x743cc4 | "RemoveInterval" | Interval removal command |
| 0x741620 | "CounterAttack" | Counter-attack evaluation |

### AI/Tactic Strings
| Offset | String | Context |
|--------|--------|---------|
| 0x737a24 | "TACTICS" | AI tactic system |
| 0x733118 | "Tactic" | Tactic reference |
| 0x740698 | "MovementsTables" | Movement decision tables |
| 0x7406a8 | "AttackTables" | Attack timing tables |
| 0x740720 | "OutcomeTables" | Outcome mapping tables |
| 0x740688 | "TablesReduction" | Discretization step |
| 0x737eb0 | "Controlled" | Animation allowing movement changes |

### Model System Strings
| Offset | String | Context |
|--------|--------|---------|
| 0x70c860 | "Model" | Model class name |
| 0x741722 | "ModelAnimation" | Animation system |
| 0x70ca90 | "ShadowScale" | Engine identifier |

### Important: "Duck" NOT FOUND
The string "Duck" (used in x86 for block animation) does NOT exist in the ARM binary.
The ARM version likely uses "Block" directly as the animation/action name, or uses
a different naming convention. This is a key difference from the x86 version.

---

## 4. Interval Data Table (Offset 0x7C2240)

This table defines the interval types used by the game engine:

| Index | Offset | String | Meaning |
|-------|--------|--------|---------|
| 0 | 0x73f8e0 | "Block" | Block interval |
| 1 | 0x72ad70 | (unknown) | Possibly function pointer |
| 2 | 0x741638 | (near CounterAttack) | Counter-attack interval |
| 3 | 0x741648 | "Uninterrupt" | Uninterruptible state |
| 4 | 0x741644 | (near Uninterrupt) | SemiUninterrupt? |
| 5 | 0x741654 | (near Invulnerable) | Related invulnerability |
| 6 | 0x73f8e0 | "Block" | Block (duplicate entry) |
| 7 | 0x74165c | "Invulnerable" | Full invulnerability |
| 8 | 0x74166c | (near Invulnerable) | Related state |
| 9 | 0x70b6ac | (unknown) | Possibly function pointer |
| 10 | 0x74f5d0 | (unknown) | Possibly function pointer |
| 11 | 0x741620 | "CounterAttack" | Counter-attack |

The table is referenced from code that processes interval state transitions.
The code section does NOT contain direct literal pool references to these string
addresses — instead, the strings are accessed through this pointer table,
which acts as an indirection layer.

---

## 5. Input Dispatcher Analysis

The input dispatcher at `0x692464` receives screen coordinates in a fixed pattern:

| Pattern | X | Y | Screen Region | Likely Action |
|---------|---|---|---------------|---------------|
| A | 0 | 649 | Left edge, mid-height | Movement joystick? |
| B | 327 | 676 | Lower-left | Movement joystick |
| C | 538 | 676 | Lower-center | **Block button** |
| D | 1392 | 368 | Far right, upper | Attack/special button |

Device screen: 720×1440 pixels

---

## 6. Xref Analysis Results

### Key Finding: No Direct Code Xrefs to Strings

The ARM binary does NOT use literal pool entries (LDR rX, [PC, #offset]) to reference
the block/tactic strings directly from code. Instead:

1. Strings are stored in `.rodata` at offsets 0x730000-0x760000
2. A **pointer table** at offset 0x7C2240 contains addresses of these strings
3. Code references the pointer table entries (not the strings directly)
4. The pointer table itself is NOT referenced from the code section we scanned

This suggests one of:
- The code uses GOT/PLT-style indirection
- The binary has multiple code segments (some in Thumb mode)
- The references use PC-relative ADR instructions (not LDR from literal pool)
- String matching is done via strcmp with the table entries at runtime

### Alternative Reference Mechanisms Found
- "Block" appears 3 times in the pointer table at 0x7C2240 (indices 0, 6, and in row at 0x7C2274)
- "CounterAttack" appears once in the table at index 11
- The table is likely iterated over by interval processing code

---

## 7. x86 vs ARM Address Mapping

| Function | x86 Address | ARM Offset | Status |
|----------|-------------|-----------|--------|
| AI Decision Loop | 0x10171d80 | ??? | NOT FOUND — needs combat trace |
| IntervalAttack::getFactors | 0x10115921 | ??? | NOT FOUND |
| Model::startAction | 0x1015C540 | ??? | NOT FOUND |
| Model::setCurrentNode | 0x1015B530 | ??? | NOT FOUND |
| Model::step | 0x10161ad0 | ??? | NOT FOUND |
| ConditionInterval::virtual_8 | 0x10086b90 | ??? | NOT FOUND |
| Game Loop | (not in x86 analysis) | 0x2f0e0 | CONFIRMED (921/s) |
| Input Dispatcher | (not in x86 analysis) | 0x692464 | CONFIRMED (246/s) |
| Entity Processor | (not in x86 analysis) | 0x691ef0 | CONFIRMED (184/s) |

**Mapping strategy:** The x86 addresses cannot be directly mapped to ARM because:
1. Different compiler optimizations
2. Different calling conventions (ARM uses r0-r3 for args, x86 uses stack)
3. Different code organization (S3E engine may have platform-specific paths)
4. ARM binary is from OBB (full game), x86 is from PC build (may differ)

---

## 8. Profiling Results (Idle/Menu State)

15-second profiling during idle/menu state:

| Function | Calls | Rate | Interpretation |
|----------|-------|------|----------------|
| 0x2f0e0 (GAME_LOOP) | 13815 | 921/s | ~12 entities × 60fps |
| 0x692464 (INPUT) | 3684 | 246/s | ~4 inputs per frame |
| 0x691ef0 (ENTITY) | 2763 | 184/s | ~3 entities × 60fps |
| All other 82 functions | 0 | 0 | Not called in idle |

**Key insight:** Only 11 out of 92 hooked functions were called during idle.
The AI decision, block evaluation, and combat functions are ONLY triggered during
active combat. This means we MUST profile during a real fight.

---

## 9. Recommendations for Next Steps

### Immediate Actions Required

1. **Start a fight and run Stalker trace:**
   ```
   python reverse/frida_hooks/frida_profiler.py
   ```
   Start a fight IMMEDIATELY when the script says "START A FIGHT NOW!"
   The 15-second profiling window will capture combat functions.

2. **Load ARM binary into Ghidra:**
   - File: `reverse/binaries/arm_dump/sf2_game_2mb.bin`
   - Architecture: ARM, 32-bit, little-endian
   - Base address: 0x8f35f000
   - Focus on: code around offset 0x100000-0x500000 (likely game logic)

3. **Search in Ghidra for:**
   - String "Block" at 0x73f8e0 → find xrefs → find calling function
   - String "UseDefense" at 0x740a38 → find the function that processes defense
   - String "TACTICS" at 0x737a24 → find the AI tactic system entry point
   - Pointer table at 0x7c2240 → find what code iterates over it

4. **Hook specific offsets during combat:**
   Once AI decision function is found, create a targeted hook:
   ```javascript
   Interceptor.attach(GAME_BASE.add(AI_DECISION_OFFSET), {
       onEnter: function(args) {
           // Read TacticContext fields from args
       },
       onLeave: function(retval) {
           // Log the chosen action (Block = Duck in x86, Block in ARM?)
       }
   });
   ```

### Binary Dump Status

| Chunk | File | Size |
|-------|------|------|
| 0 | sf2_chunk_0.bin | 262,144 bytes |
| 1 | sf2_chunk_1.bin | 262,144 bytes |
| 2 | sf2_chunk_2.bin | 262,144 bytes |
| 3 | sf2_chunk_3.bin | 262,144 bytes |
| 4 | sf2_chunk_4.bin | 262,144 bytes |
| 5 | sf2_chunk_5.bin | 262,144 bytes |
| 6 | sf2_chunk_6.bin | 262,144 bytes |
| 7 | sf2_chunk_7.bin | 262,144 bytes |
| **Total** | **sf2_game_2mb.bin** | **2,097,152 bytes** |

---

## 10. Scripts Created

| Script | Purpose | Status |
|--------|---------|--------|
| `frida_enumerate_modules.js` | Find game library and modules | ✅ Working |
| `frida_search_block_strings.js` | Scan for block/AI strings | ✅ Working |
| `frida_broad_xref_scan.js` | Find code xrefs to strings | ✅ Working (no direct xrefs found) |
| `frida_data_table_analysis.js` | Analyze interval pointer table | ✅ Working |
| `frida_combat_analysis.js` | Hook known functions + Stalker | ⚠️ Session timeout issues |
| `frida_dump_binary.js` | Dump game binary to /sdcard/ | ✅ Working (2MB dumped) |
| `frida_stalker_python.py` | Python API for Stalker trace | ⚠️ Device discovery issues |
| `frida_profiler.py` | Combat function profiler | ✅ Working (needs active combat) |
| `hook_block.js` | Original x86-targeted hooks | ❌ Wrong addresses for ARM |
| `hook_comprehensive.js` | Previous comprehensive hooks | ✅ Working (basic) |
| `profile_game.js` | Original profiling script | ✅ Working |

---

## 11. Summary

### What We Know
- Game binary: ARM 32-bit at 0x8f35f000, 8.18MB, loaded from OBB via S3E engine
- 3 hot functions confirmed: game loop (921/s), input dispatcher (246/s), entity processor (184/s)
- All block/AI strings found and mapped (Block, BlockChance, UseDefense, etc.)
- Interval pointer table identified at 0x7C2240
- "Duck" string NOT present in ARM binary (uses "Block" directly)
- No direct code→string xrefs found (uses pointer table indirection)

### What We Don't Know Yet
- The ARM equivalent of `FUN_10171d80` (AI decision loop)
- The ARM equivalent of `IntervalAttack::getFactors`
- The model struct layout on ARM (offsets may differ from x86)
- The actual block decision function and its parameters
- Whether "Block" is the action name in the roulette (replacing "Duck" from x86)

### Critical Missing Piece
**Combat-time profiling data.** All functions identified so far are game infrastructure
(loop, input, entities). The AI decision function only fires during active combat
(every 0.6-1.0 seconds). Running the profiler DURING A FIGHT will reveal which
medium-frequency functions are the AI decision/block evaluation functions.

# reSF2 — S3E Windows Binary Reverse Engineering Notes

## Source: Original Windows Files (Windows Phone 8.1 build, Feb 2015)

The Windows version of Shadow Fight 2 uses a different binary format than the
Android version:

### File: `Shadow Fight 2.s86` (6.95 MB)
- **Format**: PE32 executable (DLL), Intel i386 (NOT ARM, NOT LZMA-compressed)
- **Exports**: 3 ordinal exports (DllGetActivationFactory etc. for WinRT)
- **Imports**: Only MSVCR110.dll + KERNEL32.dll (no s3e_native imports!)
- **Sections**:
  - `.text` 0x36d2dc bytes (code) @ 0x10001000
  - `.rdata` 0x2d9138 bytes (read-only data, strings) @ 0x1036f000
  - `.data` 0xd400 bytes (globals) @ 0x10649000
  - `STLPORT_` 0x20 bytes (STLport runtime marker)
  - `.reloc` 0x4e488 bytes (relocations)

### File: `s3e_native.dll` (1.13 MB)
- **Format**: PE32 DLL, Intel i386
- **Exports**: DllCanUnloadNow, DllGetActivationFactory (WinRT activation)
- **Imports**: KERNEL32, XAudio2_8, d3d11, MFPlat, ole32, MSVCR110, etc.
- This is the Marmalade SDK runtime for Windows. It provides:
  - s3eKeyboard API (keyboard input)
  - s3ePointer API (touch/mouse input)
  - s3eAccelerometer API
  - OpenGL ES 2.0 emulation via Direct3D 11
  - Audio via XAudio2

### File: `Marmalade.App.exe` (107 KB)
- .NET executable (imports mscoree.dll — WinRT managed code wrapper)
- Bootstraps the Marmalade runtime, loads s3e_native.dll, which loads s86

## Architecture Comparison

| Aspect              | Android (ARM)              | Windows (x86)               |
|---------------------|----------------------------|-----------------------------|
| Binary format       | XE3U (LZMA-compressed S3E) | PE32 DLL                    |
| Architecture        | ARMv7 (32-bit)             | Intel i386 (32-bit)         |
| Loader              | libs3e_android.so          | s3e_native.dll              |
| Graphics            | OpenGL ES 2.0 (native)     | OpenGL ES 2.0 → D3D11       |
| Audio               | Android OpenSL             | XAudio2                     |
| Input               | Android touch + keyboard   | WinRT keyboard + touch      |
| Analysis tool       | S3ELoader (Ghidra plugin)  | Standard PE loader (Ghidra) |

**Key advantage of Windows version**: The s86 file is a standard PE32 DLL.
No custom S3E container format, no LZMA decompression, no ARM-specific
relocations. Ghidra can load it directly as a PE32 binary and decompile
to C. The ARM Android version requires the S3ELoader plugin and produces
less readable decompilation.

## Key Engine Classes Found in s86

From string analysis of the .rdata section:

### Animation System
- `ModelAnimation::getPlayerAnimation` — Gets the animation for a player type
- `ModelAnimation::mirrorNodes` — **Mirrors skeleton nodes for facing direction**
  This is the KEY function for understanding how the game handles left/right
  facing. When a character turns around, the game calls mirrorNodes to flip
  the skeleton's X coordinates.
- `ModelAnimation::playInfo` — Animation playback info (frame, time, etc.)
- `AnimationPlayer` — Animation playback controller
- `AnimationStart` / `AnimationEnd` — Animation event markers

### Combat System
- `IntervalAttack::getFactors` — Attack interval factors (damage, range)
- `AttackMoves` — Container for attack move definitions
- `Model::startAction` — Starts a model action (attack, move, block)
- `Model::setCurrentNode` — Sets the current animation node
- `Model::setNearestEnemy` — Sets the nearest enemy (for auto-facing)
- `Model::getModelAlign` — Model alignment (facing direction)
- `ConditionModelMirrored` — Condition: is model mirrored?

### Movement
- `StepForward` / `StepBack` — Step animations
- `DoubleStep` / `DoubleStepForward` — Double-step (run)
- `MoveInside` — Pivot alignment system (root motion via NPivot)
- `CautiousMovements` — Cautious movement (blocking while walking back?)

### Input System (Marmalade s3e API)
- `s3eKeyboardGetState` — Get key state (pressed/released)
- `s3eKeyboardAnyKey` — Check if any key is pressed
- `s3eKeyboardUpdate` — Update keyboard state (called per frame)
- `s3eKeyboardClearState` — Clear keyboard state
- `s3eKeyboardRegister` — Register key event callback
- `AVKeyPad` / `AVKeyboard` — Cocos2d-x keypad/keyboard abstractions
- `AVCCKeypadDelegate` / `AVCCKeypadDispatcher` — Keypad event dispatch
- `KeyPressed` — Key press event
- `Block_Keys` / `Throw_Keys` — Key bindings for block/throw

### Model/Fighter System
- `Model::equipRulesItems` — Equipment rules
- `Model::getTotalDamage` — Total damage calculation
- `ModelObject::addComNodes` — Add center-of-mass nodes
- `ModelMacroNode` — Macro node (weighted average of child nodes)
- `ModelNode` — Skeleton node
- `ModelPhysics` — Physics model (Verlet integration)
- `ModelContainer` — Model container
- `ModelController` — Model controller
- `WeaponModel` — Weapon model
- `ScreenModel` — Screen model (UI)
- `ViewerModel` — Model viewer/renderer

### Fight/Battle System
- `Fight::Fight` — Fight constructor
- `Fight::update` — Per-frame fight update
- `Fight::getModelByAppliance` — Get model by application (player/enemy)
- `Fight::getModelByScreenModel` — Get model by screen model
- `Fight::onUserComboIncrease` — User combo increase handler
- `Fight::removeModel` — Remove model from fight
- `Fight::resetLife` — Reset fighter health
- `Fight::setCurrentFight` — Set current fight
- `Fight::updateFightDataDamage` — Update damage data
- `FightList::getReward` — Get fight reward
- `FightList::putRule` — Put fight rule
- `FightScreen::getZoneIndex` — Get zone index
- `DisplayZone::activeBattle` — Active battle in display zone

### Game Flow
- `GameLoader::loadGame` — Game loader
- `GameUtils::startGame` — Start game
- `Battle::update` — Battle update
- `BattleDaily::update` — Daily battle update
- `BattlePeriodic::initBattles` — Init periodic battles

### Moves XML Parsing
- `MovesMaps::getIndex` — Get move index by name
- `MovesParser::eventsParse` — Parse events from moves.xml
- `Movesxml` — Root XML element
- `CounterConditionsParser::parseCondition` — Parse counter conditions

## Mirror/Facing System

From `ModelAnimation::mirrorNodes` and `ConditionModelMirrored`:

The game uses a **mirroring** system for facing direction, NOT a separate
`facing_right_` boolean that inverts X coordinates at render time.

When a character changes facing:
1. `Model::setNearestEnemy` is called to determine the nearest enemy
2. The game checks if the character should face left or right
3. If facing changes, `ModelAnimation::mirrorNodes` is called
4. `mirrorNodes` flips the X coordinate of every skeleton node:
   `node.x = -node.x` (or `node.x = pivot.x - (node.x - pivot.x)`)
5. `ConditionModelMirrored` is set to true/false for combat logic

**Implication for reSF2**: Instead of tracking `facing_right_` and inverting
at render time, we should mirror the skeleton nodes directly when facing
changes. This is what the original game does, and it's why root motion
deltas need to be inverted when facing left.

## Input System

From `s3eKeyboardGetState` and related:

Marmalade's keyboard API is event-based with state polling:
- `s3eKeyboardUpdate()` is called per frame to process the OS key events
- `s3eKeyboardGetState(key)` returns the current state (pressed/released)
- `s3eKeyboardRegister()` registers a callback for key events
- `s3eKeyboardAnyKey()` checks if any key is pressed

The game likely uses `s3eKeyboardGetState` for held-key detection (movement)
and `s3eKeyboardRegister` for one-shot key events (attacks, menu).

**Key insight**: `s3eKeyboardGetState` is the Marmalade equivalent of
`GetAsyncKeyState` — it polls the OS keyboard state. The flickering issue
on Windows 10 (build 19044) is specific to GLFW's Win32 backend, not to
`s3eKeyboardGetState`. Our `GetAsyncKeyState` approach in glfw_platform.cpp
mirrors what the original game does via Marmalade.

## Recommended Next Steps for Reverse Engineering

1. **Load `Shadow Fight 2.s86` into Ghidra** as a PE32 binary (i386).
   No special loader needed. Apply STLport type signatures for better
   decompilation.

2. **Find `ModelAnimation::mirrorNodes`** — search for the string
   "ModelAnimation::mirrorNodes" in .rdata, find xrefs to it, decompile
   the function. This reveals the exact mirroring formula.

3. **Find `ModelAnimation::getPlayerAnimation`** — reveals how the game
   selects animations based on player state (idle, step, attack, etc.)

4. **Find `Model::setNearestEnemy`** — reveals the auto-facing logic
   (when does the character turn to face the enemy?).

5. **Find `IntervalAttack::getFactors`** — reveals attack damage/range
   calculation from moves.xml intervals.

6. **Find `s3eKeyboardGetState` callers** — reveals how the game polls
   keyboard state for movement (step forward/back).

7. **Find `MoveInside`** — reveals root motion implementation. The
   `AUMoveInside` class (Auto-Update MoveInside) handles the pivot
   alignment that drives character displacement.

## Files in this directory

- `Shadow Fight 2.s86` — Main game binary (PE32, i386, 6.95 MB)
- `s3e_native.dll` — Marmalade SDK runtime (PE32, i386, 1.13 MB)
- `Marmalade.App.exe` — .NET bootstrap (107 KB)
- `libGLESv2.dll` — OpenGL ES 2.0 → D3D11 translation (858 KB)
- `libEGL.dll` — EGL → DXGI translation (57 KB)
- `app.icf` / `s3e.icf` — Marmalade config files
- `gamepadExtension.dll` — Gamepad input extension (43 KB)
- Various .xaml / .winmd files for WinRT UI integration

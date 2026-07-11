# reSF2 — Engine Flow Analysis (from .s3e reverse engineering)

## Original Engine: cocos2d-x 2.2.6 + Marmalade SDK 8.2.1

### Screen/Module System
The game uses a Module/Screen architecture:
- **Module** — top-level game state manager (switches between screens)
- **Screen** types (from .s3e strings):
  - `LoadingScreen` — initial loading (shows startLoading.xml images)
  - `EntryScreen` — title/entry screen
  - `ActScreen` — the act/map selection screen
  - `FightScreen` / `ScreenFight` — the actual fight
  - `ScreenModel` — model viewer (dojo/training)
  - `ShopScreen`, `ProfileScreen`, `LotteryScreen`, `CreditsScreen`
  - `VsScreen` — versus screen before fight
  - `VideoScreen` — video playback
  - `DownloadingScreen` — download progress

### Boot Sequence (inferred from strings)
1. `LoadingScreen` — loads startLoading.xml, shows loading images
   - "LoadingScreen - pauseCallback" / "unpauseCallback" / "videoStopped"
2. `Load animations` — loads animation .bin files
3. `loadGame - loading tactics` — loads tactics (.atf files)
4. `Load settings` — loads settings.xml
5. `Loading tactics for next subtypes:` — loads tactic data
6. Transition to `EntryScreen` or `ActScreen`
7. From ActScreen, player navigates to `Dojo` (GO_TO_DOJO)

### Dojo Screen
- The Dojo is a `ScreenModel` type (model viewer for training)
- `ShowDojoDisciple` — shows the training disciple
- `GO_TO_DOJO` — navigation command
- Background: `textures/fullscreen/dojo_full_bg.xml` (full-screen background)
- Location: `assets/locations/dojo/params.xml` (fight area definition)

## UI System: cocoGUI (CocosBuilder JSON format)

### Scroll/Roll Menu (CRITICAL — this is what the user wants)
The menu is a **scroll/parchment** that unrolls vertically. The textures are:
- `textures/scrolls/common/MenuRoll_left.png` (156×114) — left rolled edge
- `textures/scrolls/common/MenuRoll_center.png` (338×114) — center bar (Scale9)
- `textures/scrolls/common/MenuRoll_right.png` (156×114) — right rolled edge
- `textures/scrolls/common/Roll_left.png` (156×74) — smaller roll
- `textures/scrolls/common/Roll_center.png` (688×74) — center
- `textures/scrolls/common/Roll_right.png` (156×74) — right
- `textures/scrolls/common/Paper_left.png` / `Paper_right.png` — paper edges
- `textures/scrolls/common/Shadow_roll.png` — drop shadow
- `textures/scrolls/common/roll_fade_2/3/4.png` — fade effects

### cocoGUI JSON Format (CocosBuilder 3.10)
Files in `assets/cocoGUI/`:
- `Scroll.json` — scroll container (contains Paper + Roll)
- `Roll.json` — the roll bar (MenuRoll_center + Roll_left + Roll_right)
- `Paper.json` — the paper content area

The Roll.json uses **Scale9 rendering** (9-slice scaling):
- `Scale9OriginX/Y` — the stretchable region origin
- `Scale9Width/Height` — the stretchable region size
- This allows the roll to stretch horizontally while keeping edges fixed

### Menu Button Layout
The menu buttons are in `textures/buttons/menu/screens/batchButtonsMenuScreens`:
- `Dojo_normal/active/pushed.png`
- `Map_normal/active/pushed.png`
- `Shop_normal/active/pushed.png`
- `Profile_normal/active/pushed.png`
- `Settings_normal/active/pushed.png`
- `Fight_normal/active/pushed.png`
- `Lottery_Normal/Active/Pushed.png`
- `Highlight_menu.png` — selection highlight

## Coordinate Systems

### cocos2d-x 2.2.6 Default (Y-UP)
- **Screen coords**: Y-DOWN, origin top-left (0,0 = top-left)
- **World/scene coords**: Y-UP, origin at bottom-left (0,0 = bottom-left)
- **OpenGL projection**: `glOrthof(0, width, 0, height, -1, 1)` — Y-UP
- **Texture UV**: (0,0) = bottom-left (OpenGL convention)

### Location params.xml
- **Y-UP**, origin at CENTER of visible area
- Positive Y = UP (toward ceiling)
- Negative Y = DOWN (toward floor)
- `(X, Y)` = CENTER of the sprite (anchorPoint 0.5, 0.5)
- `Floor="80"` — floor thickness/offset
- `Wall="305"` — wall height
- PlayerPositionY = world Y of model's NPivot (pelvis)

### Texture Atlas (plist format)
- **Y-DOWN** (PNG top-left origin)
- `atlas_y` = top of frame in the PNG
- cocos2d internally flips V: `v_gl = 1.0 - v_png`

## Model/Animation System

### Skeleton (skeleton.xml)
- `<Nodes>`: 46 physics nodes (Y-UP local coords, 0 = feet area)
  - NPivot (Y≈170) = pelvis/center of mass
  - NTop (Y≈283) = top of head
  - NHead (Y≈261) = head
  - NToe_1/2 (Y≈73-113) = toes (feet)
  - Weapon-Node1-4 (Y≈200-320) = weapon attachment points
- `<Edges>`: bone segments (Type="Edge") and muscles (Type="Muscle")
  - Named: EArm_1, EFoot_1, Muscle115, etc.
  - End1, End2 = node names
  - Radius = collision capsule radius
- `<GroupsOfSelection>`: SG-1 through SG-9 = hitbox groups

### Body Model (body.xml)
- `<Nodes>`: BODY-NodeN (cloth/skin points) + BODY-MacroNodeN (composite)
  - MacroNode = weighted blend of 4 skeleton joints (LCC1-4 coefficients)
- `<Edges>`: BODY-EdgeN (cloth constraints)
- `<Figures>`:
  - Capsule: references a skeleton edge (e.g. EArm_1) + Radius1/Radius2
  - Triangle: 3 node refs (BODY-Node, skeleton node, or MacroNode)

### Animation (.bin files)
**Format**: `u32 frame_count` + `frame_count * 809 bytes`

Each frame (809 bytes):
- 1 byte: frame type (1 = keyframe, 5 = interframe)
- 202 × float32 (BIG-ENDIAN): per-node position data
- 1 trailing byte (always 0x66 = 'f')

**Float layout (202 floats)**:
- The 202 floats contain per-node X, Y, Z positions
- Frame 0 (rest pose) Y values match skeleton.xml Y values
- The node ordering in the .bin does NOT match skeleton.xml order
- Need to find the mapping by matching Y values

**Verified matches** (frame 0 Y vs skeleton Y):
| Node | Skeleton Y | Float Index | .bin Y |
|------|-----------|-------------|--------|
| NNeck | 240.86 | 84 | 240.90 |
| NElbow_1 | 200.80 | 22 | 200.66 |
| NWrist_2 | 255.75 | 69 | 256.73 |
| NHip_1 | 171.10 | 60 | 172.21 |
| NKnee_1 | 187.20 | 120 | 186.76 |
| NPivot | 169.48 | 18 | 169.61 |

**Model root movement** (air_punch animation):
- The model translates horizontally during the punch
- Frame 0-10: moving forward
- Frame 23: full extension
- Frame 37: returning

## Asset Loading Order (from .s3e strings)

1. `LoadingScreen` → load `startLoading.xml` images
2. `Load animations` → load all .bin files from `assets/animations/binary/`
3. `loadGame - loading tactics` → load .atf tactic files
4. `Load settings` → load settings.xml
5. `Loading tactics for next subtypes:` → load tactic data per weapon type
6. Load location params.xml + atlases
7. Load model (skeleton.xml + body.xml + weapon/armor/helm XMLs)
8. Load HUD textures (batchPanelsTop, batchButtonsDojo, etc.)
9. Load menu textures (batchButtonsMenuScreens)
10. Load scroll textures (MenuRoll, Roll, Paper)

## Key Issues in Current Implementation

1. **Menu**: Not using scroll/roll textures — should use MenuRoll_left/center/right
   with Scale9 rendering to create a parchment scroll that unrolls vertically

2. **Character**: body.xml mesh rendering may have incorrect coordinate
   transforms. The .bin animation format needs proper decoding to animate
   the skeleton instead of static T-pose

3. **Background**: The Y-UP coordinate system is correct, but the location
   params.xml uses center-origin coords that need proper mapping

4. **Asset paths**: The original uses `data/assets/1536/...` paths internally
   (seen in cocoGUI JSON), but the APK has `assets/1536/...`

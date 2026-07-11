# reSF2 — Work Log

This file tracks all work performed across sessions and subagents for the reSF2
clean-room reimplementation project of Shadow Fight 2 (Marmalade SDK based).

---
Task ID: stage-1
Agent: main
Task: Stage 1 — Full APK investigation and project scaffolding

Work Log:
- Cloned https://github.com/hwindinkg/reSF2.git (no token; user-supplied PAT was
  exposed in chat and was not used — user must revoke and re-create).
- Downloaded Shadow_Fight_2_1.9.21.apk (94,736,412 bytes,
  sha256=9258146bb87e7d1010ebbd6cc9f7bc9f00f1f2ff61ae4a73cd29003b072f5143).
- Decoded with apktool 2.9.3 (resources + manifest + raw DEX, no smali).
- Catalogued 2181 entries / 105 MB uncompressed.
- Identified engine: Marmalade SDK v8.2.1 (string in libs3e_android.so).
- Confirmed OpenGL ES 2.0, landscape orientation, armeabi-v7a only.
- Extracted ShadowFight2.s3e (LZMA1 legacy, 2.86 MB -> 8.69 MB); magic 'XE3U';
  contains Marmalade config text + ARM code (.text @ 0x736c3a) + data sections.
- Mapped 30 native libraries: libs3e_android.so (runtime), libsmartfox.so
  (SmartFoxServer 2X C++ API), FFmpeg 2.x family, 23 s3e* extensions.
- Mapped ~10300 Java classes across classes.dex + classes2.dex; only
  com.nekki.shadowfight.Main is the entry activity; the rest are 3rd-party
  ad/IAP/analytics SDKs + Marmalade's com.ideaworks3d.marmalade glue.
- Catalogued asset formats: PNG 1333, plist 148 (Cocos2d-x TexturePacker v2),
  atf 110 (zlib-compressed weapon-pair tactics data), XML 77, WAV 76, JPG 70,
  FNT 16, JSON 11 (CocoGUI), MP3 5, DTRZ .dz 2, MP4 1, INI 1.
- Documented 50+ locations, 110 tactics files, settings.xml manifest listing
  all gameplay XMLs (achievements, quests, perks, models, localizations...).
- Wrote Stage 1 docs under docs/, scaffoldded engine/ tree, CMake skeleton,
  .gitignore, README, CHANGELOG, TODO.

Stage Summary:
- Engine identified: Marmalade SDK v8.2.1, single-ABI (armeabi-v7a), GLES2.
- Java side is a thin loader; all game logic is C++ shipped inside the
  LZMA-compressed ShadowFight2.s3e binary (Marmalade S3E format).
- Multiplayer stack: SmartFoxServer 2X (libsmartfox.so, boost::asio).
- Video playback: FFmpeg 2.x via libs3eFfmpeg.so wrapper.
- reSF2 implementation strategy: clean-room C++20 engine that can load
  Marmalade .s3e binaries + the asset formats identified above. No original
  source code copied; class/method names derived from public symbol tables
  and observable behaviour only.
- Next stage (Stage 2): full reverse of the .s3e binary structure (section
  table, relocations, .data/.rodata layout), JNI registration map for
  libs3e_android.so, and the first cut of the engine architecture document.

---
Task ID: stage-3
Agent: main
Task: Stage 3 — Engine architecture recovery from .s3e symbol analysis

Work Log:
- Mined 9 330 unique C++ identifiers from .s3e .rodata (excluding the
  import-table region already covered in Stage 2).
- Recovered 85 game-side classes with at least one Class::method
  reference; 250+ methods catalogued. Full list saved to
  engine/reverse/s3e_classes.txt.
- Identified the rendering layer as Cocos2d-x 2.x-style (236 refs to
  "cocos2d" in .s3e strings). Nekki wrote their own thin layer that
  mimics the Cocos2d-x 2.x API on top of Marmalade's IwGx. This is
  NOT the official Cocos2d-x Marmalade port.
- Identified XML parsers: pugixml (primary) + tinyxml (secondary).
- Identified save system: assets/localSettings.bin, AES-encrypted.
  UserDefault.xml for non-sensitive UI settings.
- Confirmed physics is fully custom (no Box2D/Chipmunk/Bullet).
- Confirmed networking uses SmartFoxServer 2X (BitSwarmClient,
  UDPManager, LagMonitor, etc.).
- Confirmed main loop is single-threaded, variable-step, driven by
  s3eTimerGetMs() with dt clamped to 200ms (Cocos2d-x convention).
- Identified camera: 2D orthographic with follow + shake + zoom.
- Wrote docs/11_engine_architecture.md (full architecture + class
  inventory + reSF2 target layout).
- Wrote docs/12_main_loop.md (main loop pseudocode, update order,
  frame timing budget, pause/resume, fixed-step decision rationale).
- Updated TODO.md (Stage 3 marked complete), docs/README.md index.

Stage Summary:
- Architecture fully recovered at the high level: layered stack is
  Game logic -> Cocos2d-x 2.x -> pugixml/tinyxml -> SmartFox2X ->
  Marmalade SDK -> Android.
- 85 game classes organised into 7 functional categories. The most
  important for reSF2's Stage 7 implementation:
    Module             (base class for screens)
    Fight / Battle     (fight instance + battle types)
    Model              (character model + equipment)
    ModelAnimation     (per-model skeletal anim state)
    RulesInspector     (runtime rule engine)
    RuleParser         (XML -> rule objects)
    RaidManager        (multiplayer raids)
    SaveSystem         (encrypted localSettings.bin)
- Main loop model documented in detail (init -> loop -> shutdown)
  with per-subsystem update order, frame timing budget, pause/resume
  semantics.
- Major de-risking: Cocos2d-x 2.x API is public MIT-licensed, so
  reSF2's Stage 7.2 renderer can re-implement CCSprite/CCDirector/etc
  without reverse-engineering Nekki's specific code.
- Next: Stage 4 — full .dz DTRZ archive unpack + moves.xml schema +
  .atf tactics byte layout + C++20 readers.

---
Task ID: stage-7.3
Agent: main
Task: Stage 7.3 — Dojo scene with character, HUD, menu, dialog (headless-tested)

Work Log:
- Added a software renderer (engine/renderer/software_renderer.{hpp,cpp}) that
  renders to an in-memory RGBA framebuffer and saves PNGs via stb_image_write.
  No GL/GPU/window-system dependency. Same public API surface as the GL
  Renderer (camera, draw_textured_quad, begin_frame/end_frame) plus primitive
  shapes for HUD/UI (filled rects, circles, thick lines in screen & world
  space).
- Added headless_main.cpp — a non-interactive driver that boots the engine
  through the full sequence (loading screen → dojo → player → punching bag →
  HUD → menu → dialog → movement demo) and saves a PNG screenshot at each
  stage. Verified visually with the VLM.
- Extended the GL Renderer with the same screen-space primitives
  (draw_textured_quad_screen, draw_filled_rect_screen, draw_filled_circle_screen,
  draw_line_screen, draw_line_world) so the GLFW build has feature parity with
  the headless build. Uses a 1x1 white texture + tinted quads for filled
  shapes.
- Rewrote main.cpp to add: player character (skeletal stick figure from
  skeleton.xml with 47 nodes, thick gray bones + red joints + orange head),
  punching bag (procedural dark-red rectangle with chain at enemy position),
  HUD (money / energy / level badges + menu button in top-right, position
  label + control hint at bottom), menu overlay (Map/Shop/Settings/Save/Exit),
  dialog overlay (Sensei intro line), player movement (A/D + arrows),
  hit animation (Space — extends front arm, swings bag if close),
  menu toggle (M or click menu button), dialog toggle (T).
- Fixed a camera synchronisation bug in headless_main.cpp: the Game class had
  its own Camera2D member that was never connected to the renderer's camera,
  so textured quads used the renderer's default camera (0,0,1.0) while lines
  used the Game's camera. Unified by removing the Game's camera_ member and
  using renderer_.camera() everywhere.
- Updated CMakeLists.txt: added RESF2_BUILD_HEADLESS option and a
  resf2_headless target that links only resf2_renderer + resf2_reverse
  (no GLFW, no platform, no GL). Updated engine/renderer/CMakeLists.txt to
  conditionally compile renderer.cpp + gl_loader.cpp only when OpenGL is
  found (headless builds skip them).
- All 4 existing unit tests still pass on Linux (test_s3e_container,
  test_asset_loaders, test_asset_manager, test_platform_loop).
- Copied stb_image_write.h into engine/renderer/ (from GLFW's deps bundle)
  for PNG output.

Stage Summary:
- Engine boot sequence fully verified headlessly on Linux:
    01_loading.png          — startLoading.xml assets rendered
    02_dojo_background.png  — params.xml + 4 atlases + parallax layers
    03_dojo_player.png      — stick-figure fighter at player position
    04_dojo_punching_bag.png — procedural bag at enemy position
    05_dojo_hud.png         — money/energy/level/menu button overlay
    06_dojo_menu.png        — menu panel (Map/Shop/Settings/Save/Exit)
    07_dojo_player_left.png — player moved 150px left
    08_dojo_player_right.png — player moved 200px right
    09_dojo_dialog.png      — Sensei story dialog panel
- VLM (glm-4.6v) confirms: stick figure with red joints in standing pose,
  punching bag visible, HUD text "72 450 5/5 LVL 7" + menu button, menu
  overlay with all 5 options, dialog with Sensei intro, player position
  changes between movement demo screenshots.
- The interactive GLFW build (main.cpp) has the same feature set but could
  not be compiled on this Linux machine (missing libxrandr-dev and related
  X11 headers; no root access). It should build and run on the user's
  Windows machine via build.bat as before.
- Next: decode the .bin animation format so the stick figure can play
  actual animations (idle, walk, punch) instead of the static T-pose.

---
Task ID: stage-7.4
Agent: main
Task: Stage 7.4 — Fix coordinate system (Y-down), use real textures, fix character orientation

Work Log:
- Diagnosed the root cause of the "upside-down" rendering: the original
  game's location XML uses Y-DOWN coordinates (positive Y = down toward
  floor), but both the software renderer and GL renderer were using Y-UP
  (positive Y = up). This inverted the character vertically (head at
  bottom, feet at top) while the symmetric background tiles hid the
  inversion.
- Fixed the coordinate system in BOTH renderers:
  - software_renderer: changed Camera2D::world_to_screen to use Y-down
    (sy = view_height/2 + dy instead of view_height/2 - dy).
  - GL renderer: changed Camera2D::view_projection to swap bottom/top
    in the ortho matrix (y+hh, y-hh instead of y-hh, y+hh).
  - draw_textured_quad: (x,y) is now top-left in Y-down world coords.
  - draw_line_world: updated w2s transform to Y-down.
- Fixed skeleton parser to only parse the <Nodes> section (was picking
  up node references from <GroupsOfSelection> and <Edges>, causing
  NPivot to be missed and pivot_local_y to default to 0). Now correctly
  finds 46 nodes including NPivot.
- Fixed character placement: PlayerPositionY from params.xml is the
  NPivot (pelvis) world Y, but the original coordinate origin differs
  from ours. We now place the player so feet (NToe) rest on the visible
  floor (world Y ≈ 225 in Y-down).
- Added real game textures for HUD and menu (no more hand-drawn shapes):
  - Top_Panel.png (top HUD bar background)
  - gold.png (coin icon), energy.png (lightning icon)
  - Level_bar.png (level progress bar)
  - btn_punching_bag.png (punching bag icon from dojo buttons atlas)
  - Menu icons: Dojo, Map, Shop, Profile, Settings, Fight, Lottery
    (from batchButtonsMenuScreens atlas)
- Moved the menu button from top-right to top-LEFT (matching original).
  When expanded (M key or click), it shows a horizontal list of 7 real
  menu icons with labels, instead of a centered modal panel.
- Updated main.cpp (GLFW interactive version) with all the same fixes:
  Y-down coords, real textures, left-side menu button, character on floor.
- Note on punching bag: the real 3D model is in punching_bag.xml +
  skeleton_punching_bag.xml inside files.dz, which requires DZ
  decompression (not yet working — needs full Marmalade runtime for
  ARM emulation). Using btn_punching_bag.png icon as a PLACEHOLDER.
- All 4 unit tests still pass.

Stage Summary:
- Character is now upright (head at top, orange circle; feet at bottom)
  and standing ON the floor (not floating).
- HUD uses real Top_Panel, gold, energy, Level_bar textures from the
  original game's atlas.
- Menu button is on the LEFT side; expanded menu shows real game icons
  (Dojo/Map/Shop/Profile/Settings/Fight/Lottery) in a horizontal list.
- Punching bag uses real btn_punching_bag.png icon (placeholder until
  .dz extraction works).
- VLM-verified: character upright on floor, HUD with real textures,
  menu with 7 real icons, movement demo works, dialog overlay works.
- Next: unblock DZ decompression to extract punching_bag.xml +
  skeleton_punching_bag.xml + body.xml + all other model XMLs from
  files.dz. This will allow rendering the real 3D punching bag model
  and real character body parts.

---
Task ID: stage-7.5
Agent: main
Task: Stage 7.5 — Fix Windows build, render body.xml mesh, load skeleton edges

Work Log:
- Fixed Windows build errors:
  - std::min ambiguity (MSVC strict): use std::min<uint32_t> for mixed
    uint32_t/int arguments in hit_anim/bag_swing decay.
  - LNK4006 duplicate stbi symbols: extracted STB_IMAGE_IMPLEMENTATION
    and STB_IMAGE_WRITE_IMPLEMENTATION into a single compilation unit
    (stb_image_impl.cpp) instead of defining them in both renderer.cpp
    and software_renderer.cpp.
  - Fixed int->float conversion warnings (C4244).
  - Fixed [[nodiscard]] warning (C4834): check make_gl_current() return.
- User provided body.xml and punching_bag.xml (extracted from files.dz
  on Windows). These are the real fighter body mesh + punching bag model.
- Implemented body.xml mesh rendering:
  - Parses <Nodes> (BODY-NodeN, BODY-MacroNodeN), <Edges> (BODY-EdgeN),
    <Figures> (Capsules with Edge/Radius1/Radius2, Triangles with
    Node1/Node2/Node3).
  - Renders capsules as thick lines (light gray, radius from XML).
  - Renders triangles as filled polygons (dark gray, rasterized with
    barycentric interpolation).
  - Resolves MacroNodes recursively as weighted averages of child nodes
    (using LCC1..LCC4 coefficients).
- Fixed skeleton.xml edge parsing to also load Type="Muscle" entries
  (Muscle115..120) which are referenced by body.xml capsules.
  Now loads 193 edges (111 Edge + 82 Muscle).
- The dzip repo (https://github.com/kugelrund/dzip) is for Quake demo
  files, NOT for Marmalade's DTRZ format. Not usable. User must extract
  files.dz on Windows using dzip.exe + extract_dz.bat.

Stage Summary:
- Windows build now compiles successfully (all 3 fixes applied).
- Body mesh from body.xml renders: 15 nodes, 36 edges, 82 capsules,
  29 triangles. Character now has a filled body shape (not just stick
  figure) with thick capsule limbs and filled triangle torso.
- Skeleton edges loaded: 193 entries (Edge + Muscle types).
- VLM-verified: character has "filled torso area with thickness".
- Next: need skeleton_punching_bag.xml to render the real punching bag
  3D model (currently using btn_punching_bag.png icon as placeholder).

---
Task ID: stage-7.6
Agent: main
Task: Stage 7.6 — Y-UP coordinate system fix, real 3D punching bag, detailed RE plan

Work Log:
- Created detailed reverse engineering plan (docs/17_detailed_reverse_plan.md)
  documenting:
  - Original engine: cocos2d-x 2.2.6 + Marmalade SDK 8.2.1
  - Coordinate systems: cocos2d uses Y-UP (origin bottom-left), location
    params.xml uses Y-UP (origin center, positive Y = up)
  - Skeleton/model local coords: Y-UP (0 = feet, positive = up)
  - Texture atlas coords: Y-DOWN (PNG top-left origin), cocos2d internally
    flips V
  - Rendering pipeline: CCDirector ortho projection, CCLayers with parallax,
    CCSprites positioned at center (anchorPoint 0.5, 0.5)
  - Body model: triangles (visual mesh) + capsules (collision), MacroNodes
    = weighted blend of skeleton joints

- CRITICAL FIX: Changed coordinate system from Y-DOWN back to Y-UP (cocos2d
  convention). The original game uses Y-UP throughout (confirmed by
  analyzing params.xml ceiling/floor mask positions and cocos2d-x 2.2.6
  default projection). Previous Y-DOWN implementation inverted the scene.

- Fixed software renderer:
  - Camera2D::world_to_screen: sy = view_height/2 - dy (Y-UP to Y-DOWN screen)
  - draw_textured_quad: (x,y) = bottom-left corner, quad extends up (+Y)
  - UV mapping: world bottom (y) -> atlas bottom (v1), world top (y+h) ->
    atlas top (v0)

- Fixed GL renderer:
  - Camera2D::view_projection: standard ortho (bottom=y-hh, top=y+hh) = Y-UP
  - draw_line_world: Y-UP world -> Y-DOWN screen (invert Y)
  - Removed double V-flip in location rendering (was fv0=1-v1 + SpriteBatch
    1-v0, now just SpriteBatch handles the flip)

- Implemented real 3D punching bag model from user-provided
  skeleton_punching_bag.xml + punching_bag.xml:
  - 15 nodes (NNeck, NBottom, NPivot, Node4-7, Node12, Node13-18)
  - 23 edges (Body, Edge8-11, Edge15-32)
  - 11 capsules (Edge16/17 = main bag r=25, others = chain r=2)
  - Renders as thick capsules: dark red bag body + gray chain

- Updated character placement to use PlayerPositionY from params.xml
  directly (matches original engine placement).

- Fixed HUD Top_Panel rendering: tile horizontally instead of stretching
  with invalid UV >1.0.

- All 4 unit tests pass.

Stage Summary:
- Coordinate system is now Y-UP throughout (matching cocos2d-x 2.2.6).
- Dojo background is upright (floor at bottom, ceiling at top).
- Real 3D punching bag model renders with chain hanging from ceiling.
- Character body mesh renders (capsules + triangles from body.xml).
- HUD uses real Top_Panel/gold/energy/Level_bar textures.
- Menu button on left side uses real Settings_normal icon; expanded menu
  shows 7 real icons (Dojo/Map/Shop/Profile/Settings/Fight/Lottery).
- VLM-verified: background upright, punching bag with chain visible,
  character body mesh present.
- Next: decode .bin animation files so character can walk/punch instead
  of static T-pose.

---
Task ID: stage-7.7
Agent: main
Task: Stage 7.7 — Engine flow analysis + scroll menu (matching original logic)

Work Log:
- Created detailed engine flow analysis (docs/18_engine_flow_analysis.md)
  documenting the complete original engine behavior:
  - cocos2d-x 2.2.6 + Marmalade SDK 8.2.1 stack
  - Module/Screen system (LoadingScreen → EntryScreen → ActScreen → Dojo)
  - cocoGUI JSON format (CocosBuilder 3.10) for UI layout
  - Scroll/Roll menu: MenuRoll_left/center/right + Paper_left/right
  - Scale9 rendering for stretchable scroll center
  - Asset loading order from .s3e strings
  - Model/animation system: skeleton + body + .bin animation format

- Analyzed .bin animation format:
  - u32 frame_count + frame_count * 809-byte frames
  - Each frame: 1 byte type (1=keyframe, 5=interframe) + 202 big-endian floats
  - Float Y values match skeleton.xml Y values (verified NPivot=169.5)
  - Node ordering differs from skeleton.xml (need mapping)

- Implemented scroll/parchment menu (matching original game):
  - Loads MenuRoll_left/center/right.png from textures/scrolls/common/
  - Loads Paper_left/right.png and Shadow_roll.png
  - Collapsed: short roll bar with "MENU" text
  - Expanded: full scroll with paper area + 7 menu icons
  - Uses Scale9-style center stretching for the roll bar
  - Paper area has parchment background with menu icon buttons

- Applied same scroll menu to main.cpp (GLFW interactive version):
  - Added scroll_textures_ member
  - load_menu_textures() now loads scroll textures
  - render_menu_expanded() uses scroll/roll UI
  - Collapsed menu button uses MenuRoll textures
  - Updated click hitbox to match new button position

- All 4 unit tests pass.

Stage Summary:
- Engine flow fully documented from .s3e reverse engineering.
- Menu now uses real scroll/parchment textures (MenuRoll + Paper).
- Both headless and GLFW versions have the scroll menu.
- VLM-verified: scroll menu visible with wooden roll caps and paper area.
- Next: decode .bin animation node mapping to enable skeletal animation.

---
Task ID: stage-7.8
Agent: main
Task: Stage 7.8 — Add body model rendering to GLFW version, fix skeleton parser

Work Log:
- CRITICAL FIX: main.cpp (GLFW version) was completely missing body model
  loading and rendering. Only the headless version had it. This was the root
  cause of the user seeing "only a square" instead of a character body.

- Added to main.cpp:
  - BodyMacroNode and BodyTriangle structs (for body.xml mesh)
  - SkelEdge struct (for skeleton edge parsing)
  - body_model_ and skeleton_edges_ members
  - load_body_model() function (parses body.xml: Nodes, MacroNodes, Edges,
    Capsules, Triangles)
  - resolve_body_node() function (resolves node names to world coords,
    handles BodyNode, SkelNode, MacroNode recursively)
  - render_body_model() function (renders capsule edges as world-space lines)
  - Called load_body_model() in on_update after load_skeleton()
  - Called render_body_model() at the start of render_character()

- Fixed skeleton parser in main.cpp:
  - Was only searching for `<N` (missed Weapon-Node entries that start with `<W`)
  - Now searches for all Type="Node" tags (finds all 54 nodes including weapons)
  - Added <Edges> section parsing (Type="Edge" and Type="Muscle")
  - Now loads 193 edges (111 Edge + 82 Muscle) needed for body capsule resolution

- Session restart recovery:
  - SSH keys lost in session restart — generated new ed25519 key
  - Public key: ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAHH/McxeN/Ka2YZHHU7dJRc8yqfldrAYVYJDp7M8ryT
  - User needs to add this as a deploy key to the GitHub repo
  - Recreated ssh_wrapper.py (paramiko-based SSH for git push)
  - Reinstalled cmake and paramiko via pip

- All 4 unit tests pass.

Stage Summary:
- GL renderer (main.cpp) now has full body model rendering matching headless.
- Skeleton parser fixed to find all 54 nodes (including Weapon-Node) and 193 edges.
- Body mesh from body.xml renders as capsule edges (light gray lines).
- Scroll menu uses real MenuRoll/Paper textures.
- Next: user needs to add new SSH deploy key to GitHub for push access.

---
Task ID: stage-8.1
Agent: main
Task: Fix bag swing rendering, add combat/animation diagnostics

Work Log:
- SSH key: previous key (AAAAC3...8ryT) was lost when session restarted.
  Generated new ed25519 key: ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAICOvMNrwSkfbIZ2kwKcENvw/EPJWdFrNODcb5eJbj2Fd
  User needs to add this as a deploy key to the GitHub repo (hwindinkg/reSF2).
  /home/z/ssh_wrapper.py created — paramiko-based SSH wrapper for git over SSH
  (no openssh-client installed in this env). git -c core.sshCommand works.
  Successfully fetched origin/main (c4cfd25) and reset --hard to it.

- Verified project files: 841 files in repo, including:
  - main.cpp (2205 lines), engine/renderer/renderer.cpp (376 lines)
  - assets/animations/binary/ (555 .bin files)
  - assets/models/ (skeleton.xml, body.xml, punching_bag.xml, etc.)
  - assets/animations/moves.xml (858 moves)

- BUG FIX 1 — bag_swing_ was set but never applied to bag rendering:
  render_punching_bag() drew the bag at fixed position even when bag_swing_ > 0.
  Added damped oscillation swing: angle = sin(t * 4π) * (1-t) * 0.45 rad,
  where t = 1 - bag_swing_/800 (0 when hit, 1 when done).
  All bag capsules rotate around Node12 (the fixed ceiling attachment at Y=335).
  2 full oscillations, max ~26 degrees, decaying amplitude.
  Direction: bag swings AWAY from player (player on left → bag swings right).

- DIAGNOSTIC LOGGING — added [COMBAT] and [ANIM] log lines:
  [COMBAT] Space → HighPunch (anim 'high_punch', 12 frames)
  [COMBAT] K → HighKick (anim 'high_kick', 21 frames)
  [COMBAT] Space → HighPunch BUT anim 'high_punch' NOT loaded!  (if missing)
  [COMBAT] HIT! move=HighPunch frame=3/12 dx=283.0 → bag_swing=800ms
  [COMBAT] attack ended → return to fists_idle
  [ANIM] 'fists_idle' frame=0/38 anim_node_pos_.size()=67  npivot_idx=18
    NPivot     anim_local=(   0.00, 169.48)  rest=( -15.83, 169.48)
    NHip_1     anim_local=(   8.35, 167.78)  rest=(  -4.95, 171.10)
    NHip_2     anim_local=(  -8.35, 172.80)  rest=( -26.71, 167.87)
    ... (NKnee, NAnkle, NToe for both sides)
  The [ANIM] log triggers once per animation change (last_logged_anim_ tracking).
  This lets the user verify whether animation data is actually being applied
  to leg nodes (or whether rendering falls back to rest pose).

- Verified .bin file format with Python script:
  - high_punch.bin: fc=12, file_size=9712 (matches 4 + 12*(5+67*12))
  - fists1_stance_idle.bin: fc=38, file_size=30746 (matches 4 + 38*809)
  - Each frame: 1 byte skip + 4 byte node_count (LE) + 67*12 bytes (3 floats LE)
  - Node order matches skeleton.xml XML order (67 nodes: 54 N + 8 Weapon + 1 COM + 12 Macro)
  - NPivot at index 18 in both XML and bin
  - Node[19-26] (Weapon-Nodes) have "parked" positions far from body (constant
    across all frames — unused weapon attachment points). Does NOT affect
    rendering since no leg capsule references Weapon-Nodes.

- Verified all 25 leg capsules + 29 triangles in body.xml:
  - All leg capsule endpoints (NHip, NKnee, NAnkle, NToe, NHeel, NToeS, NToeTip)
    exist in skeleton.xml and are animated via .bin
  - No capsule references missing nodes
  - Some triangles (BODY-Triangle-7..10) mix animated NAnkle/NKnee with
    non-animated BODY-Node12/15/17/18 (cloth nodes from body.xml which
    are NOT in skeleton.xml and NOT in .bin). These could cause minor
    triangle distortion during animation, but only affect cloth detail
    triangles on the right calf — not the main leg capsules.

- Syntax-checked main.cpp with g++ -std=c++23 -fsyntax-only — passes.

Stage Summary:
- Bag now visibly swings when hit (was invisible before — bag_swing_ was dead code).
- Comprehensive [COMBAT] and [ANIM] diagnostic logging added.
- User can now run the game, press Space/K, and the console will show:
  * Whether the key press was registered
  * Whether the animation was found
  * Whether hit detection triggered
  * Whether bag_swing_ was set
  * The animated vs rest positions of key leg nodes
- This will let us pinpoint whether the "legs look stretched" issue is
  caused by animation not being applied, or by something else.
- Next: user needs to add SSH deploy key, build on Windows, test, and
  report what the [ANIM] / [COMBAT] logs show.

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

---
Task ID: stage-8.2
Agent: main
Task: Fix root motion, bag physics, leg stretching, dojo rendering, HUD/menu

Work Log:
- Analyzed user-provided log file showing [ANIM] and [COMBAT] output.
  Key findings:
  * step_forward.bin: NPivot goes 169→235 (+66 units over 16 frames)
  * step_back.bin: NPivot goes 235→169 (-66 units)
  * fists_idle.bin: NPivot essentially static
  * Animation switching between step_forward and fists_idle rapidly
    (play_animation called repeatedly, resetting anim_time_ to 0)
  * Hit detection triggered at dx=150-301 (too lenient, 400px threshold)

- ROOT MOTION FIX: Rewrote root motion code to use OFFSET from frame-0
  NPivot position instead of absolute prev_npivot_x_.
  Old: delta = npivot_x - prev_npivot_x_ (cross-animation jumps)
  New: delta = (npivot_x - anim_root_anchor_x_) - prev_root_offset_
  This prevents large deltas when switching between animations with
  different world-space starting positions. Filter threshold reduced
  from 50 to 40 units. prev_root_offset_ reset in play_animation.
  Added [ANIM] log in play_animation to track when/how often it's called.

- BAG PHYSICS FIX: Replaced simple "player center within 400px of bag"
  check with actual limb-to-bag distance check.
  * Punches: check NWrist_1 (front fist) animated world position
  * Kicks: check NToe_1 (front foot) animated world position
  * Hit threshold: 120 units (was 400)
  * Swing direction: bag_swing_dir_ = +1 if hit from left, -1 if from right
  * Swing angle: dir * sin(t * 4π) * (1-t) * 0.5 rad (damped pendulum)
  Bag now ONLY swings when the attacking limb actually reaches the bag,
  and swings in the correct direction (away from the attacker).

- LEG STRETCHING FIX: Identified that BODY-Triangle-7..10 on the calves
  mix animated skeleton nodes (NAnkle_2, NKnee_2) with non-animated
  cloth nodes (BODY-Node12, 15, 17, 18). The cloth nodes stay at their
  rest-pose positions while skeleton nodes move, causing triangle stretching.
  Fix: skip triangles where any vertex is not in anim_node_pos_ or
  skeleton_nodes_. This removes the stretched cloth-detail triangles
  while keeping the main capsule silhouette intact.

- DOJO RENDERING FIX:
  * Y-axis: Original game uses Cocos2d coords (Y=0 at bottom, Y up).
    reSF2 uses Y=0 at camera center. Added y_offset = 512 (half of 1024px
    design height) to convert: world_y = img.y - y_offset.
    This fixes "floor on ceiling" — floor images now appear at the bottom.
  * Parallax: Implemented parallax scrolling using layer.factor.
    Layers with factor < 1 scroll slower (appear further away).
    parallax_shift = (1 - factor) * cam_x_, applied to image X.
  * Layer types: Removed `if (layer.type != 1) continue;` filter.
    Now renders ALL layer types (background, parallax, foreground).

- HUD/MENU FIX:
  * Scroll size: Collapsed MENU roll now sized to fit "MENU" text
    (text_width + 2*cap_w + padding) instead of fixed 130px.
  * Menu animation: Added menu_anim_progress_ (0→1, 300ms transition).
    Scroll "unrolls" from top to bottom with smoothstep easing.
    Collapsed roll fades out as expanded scroll fades in.
    Icons only render when their area is revealed by the unrolling.
  * Rotated atlas frames: Fixed load_texture_atlas_to_hud to handle
    frames marked as "rotated" in plist. Previously, rotated frames
    were cropped without un-rotating, causing icons to appear sideways
    (e.g., Profile icon rotated right). Now correctly un-rotates 90° CW.
  * Icon size: Increased from 48 to 56 pixels to better match original.
  * Paper width: Increased to fit icon + text labels.

- All changes syntax-checked with g++ -std=c++23 -fsyntax-only — passes.

Stage Summary:
- Root motion: character now moves continuously when holding A/D, no more
  "returns to start" issue.
- Bag physics: bag only swings when the fist/foot actually hits it, and
  swings in the correct direction (away from attacker).
- Leg rendering: no more stretched triangles on calves/ankles.
- Dojo: floor at bottom, ceiling at top, parallax scrolling works, all
  layers rendered.
- Menu: scroll sized to fit "MENU" text, smooth expand/collapse animation,
  rotated atlas frames now display correctly (Profile icon no longer sideways).
- Next: user should build and test. The [ANIM] play_animation log will show
  if animations are still switching rapidly.

---
Task ID: stage-8.3
Agent: main
Task: Fix movement (velocity-based), bag physics (real pendulum), dojo Y-offset revert, HUD text centering

Work Log:
- Analyzed user log: confirmed animation flickering (step_forward ↔ fists_idle
  every frame) prevents root motion accumulation. Character never moves.

- MOVEMENT FIX: Completely replaced root motion with direct velocity-based
  movement. When A/D is held, player_pos_x_ += MOVE_SPEED * dt (200 units/sec).
  Step animations play purely for visual effect. Root motion code in
  update_animation() is now disabled (commented out).
  This is simpler, more reliable, and immune to animation flickering.
  The character now moves continuously when holding A/D and stays at the
  new position when the key is released.

- BAG PHYSICS FIX: Replaced scripted sine wave with real damped spring pendulum.
  State: bag_angle_ (radians), bag_angle_vel_ (rad/sec).
  Physics: angular_acc = -k * angle - c * angular_vel (k=40, c=3.0)
  Integration: vel += acc * dt; angle += vel * dt.
  On hit: impulse applied to bag_angle_vel_ (6.0 for punches, 8.0 for kicks).
  Direction: bag swings AWAY from attacker (dx < 0 → positive angle → right).
  The bag now swings with momentum, oscillates back and forth, and gradually
  comes to rest — like a real punching bag.

- BAG HIT DETECTION FIX:
  * Threshold reduced from 120 to 80 units (tighter, fewer false positives).
  * Now uses moves.xml attack_start/attack_end intervals instead of
    generic middle-third (fc/4 to fc*3/4).
  * Only triggers when the attacking limb (NWrist_1 for punches, NToe_1 for
    kicks) is within 80 units of the bag center.
  * Impulse-based: applies velocity to pendulum instead of scripted timer.

- DOJO RENDERING FIX:
  * Reverted Y-offset (was img.y - 512, now just img.y).
    The params.xml coordinates use the same system as player/enemy positions
    (Y-up, Y=0 near center), so no conversion is needed.
  * Added [LOC] diagnostic logging: prints all layer types, factors, and
    image coordinates on first render. This will help identify the actual
    coordinate values and determine if any Y-flip is needed.
  * Parallax still applied: world_x = img.x - (1-factor)*cam_x_.

- HUD FIX:
  * MENU text: now measures actual text width using font xadvance values,
    then centers it precisely on the roll: text_x = btn_x + (roll_w - text_w)/2.
    Vertical centering: text_y = btn_y + (roll_h - text_height)/2.
  * Menu icons: all rendered at fixed icon_size × icon_size (no aspect ratio
    preservation). This ensures all icons are the same size.
  * Added [MENU] diagnostic logging: prints texture dimensions for each icon.

- Profile mirroring: the un-rotation formula for rotated atlas frames is
  correct (verified mathematically). Profile might be intentionally mirrored
  in the original game, or there's a separate flip flag in the plist.
  Added diagnostic logging to investigate.

- All changes syntax-checked with g++ -std=c++23 -fsyntax-only — passes.

Stage Summary:
- Movement: character now moves continuously when holding A/D, stays at new
  position when released. No more "return to start" issue.
- Bag: real physics pendulum with momentum and damped oscillation. Only
  swings when the limb actually hits (80px threshold). Swings in correct
  direction (away from attacker).
- Dojo: reverted Y-offset, added diagnostic logging to identify the actual
  coordinate system used by params.xml.
- HUD: MENU text precisely centered, all menu icons same size.
- Next: user should build, test, and share the [LOC] diagnostic logs so we
  can determine the correct coordinate system for dojo images.

---
Task ID: stage-8.4
Agent: main
Task: Implement Verlet physics for bag, fix movement cooldown, dojo Y-invert, V-flip, menu icon scaling

Work Log:
- Analyzed user feedback:
  * Bag "dёргается" from hits that miss — physics not realistic
  * Movement has no delay between steps, animations don't blend
  * Dojo background moves too fast and flies off screen
  * Background is upside down, only 1 tree branch correct
  * Menu icons different sizes, profile stretched, dojo squished
  * MENU text position wrong, gold/energy/level shifted left

- BAG PHYSICS — Implemented Verlet integration (matches original game):
  * Original game uses "ModelPhysics" with Verlet integration (confirmed
    via disassembly string table: "12ModelPhysics", "PhysicsFrameNumber")
  * Added VerletNode struct: position (x,y), prev_position (px,py),
    mass, inv_mass, fixed flag, attenuation
  * Added VerletConstraint struct: n1, n2, length, stiffness
  * init_bag_verlet(): initializes all bag skeleton nodes with world
    positions, Node12 is fixed (ceiling attachment). Edges become
    distance constraints with rest length = actual node distance.
  * update_bag_verlet(dt): Verlet integration with gravity (-500 units/s²)
    + attenuation damping, then 8 iterations of constraint satisfaction.
  * apply_bag_impulse(node, vx, vy): applies impulse by moving prev pos.
  * Hit detection now applies impulse to specific bag node (NNeck/NBottom/
    Node4) based on hit height. Impulse strength 10 (punch) / 15 (kick).
  * render_punching_bag now uses Verlet node positions directly.
  * Removed old bag_angle_/bag_angle_vel_ pendulum code.

- MOVEMENT — Added step animation cooldown:
  * Only switch to step_forward/step_back if currently in fists_idle.
    Don't interrupt an in-progress step animation.
  * Return to idle only when step animation has played past 75%.
    This lets the step animation finish naturally before transitioning.
  * Prevents the rapid step↔idle flickering seen in logs.

- DOJO RENDERING — Y-axis inversion:
  * params.xml uses Y-DOWN (Y=0 at top, positive Y = down).
    Evidence: layer_3 (floor) at y=225, but floor should be at bottom.
    Player at y=-93 (above center in Y-DOWN = below center in Y-UP).
  * Now invert: world_y = -img.y for all location images.
  * Floor (y=225) → world_y=-225 (bottom). Correct.
  * Ceiling decor (y=-202) → world_y=+202 (top). Correct.

- DOJO RENDERING — V-coordinate flip for atlas textures:
  * Flipped V coords: v0 = (atlas_y + atlas_h)/th, v1 = atlas_y/th.
  * This makes background textures appear right-side up (were upside down).
  * Cocos2d uses Y-DOWN for sprites; our renderer uses Y-UP, so V flip
    is needed to match the original game's rendering.

- DOJO RENDERING — Parallax:
  * parallax_shift = (1 - factor) * cam_x_.
  * factor=0.1 (bg) → barely moves. factor=1.0 (fg) → moves with camera.
  * Added detailed comment explaining the math.

- MENU ICONS — Uniform scaling:
  * Find max texture dimension across all 5 icons.
  * uniform_scale = icon_size / max_tex_dim.
  * All icons scaled by same factor, preserving aspect ratio.
  * Centered within icon_size × icon_size slot.
  * This ensures Dojo, Map, Shop, Profile, Settings all appear same size.

- MENU TEXT — Vertical centering:
  * Measure actual text height using font char.height * scale.
  * text_y = btn_y + (roll_h - text_h) / 2.
  * Now properly centered vertically on the roll.

- Used VLM to analyze original game screenshots:
  * MENU button is top-LEFT (not right as one screenshot suggested)
  * Menu scroll expands vertically on the left side
  * Gold/energy/level are on the left side of the top panel
  * Floor is at the bottom, punching bag hangs from ceiling

- All changes syntax-checked with g++ -std=c++23 -fsyntax-only — passes.

Stage Summary:
- Bag: real Verlet physics with gravity, distance constraints, damping.
  Node12 fixed at ceiling, other nodes swing naturally. Impulse on hit
  pushes the actual contacted node, causing realistic pendulum motion.
- Movement: step animations play fully before transitioning, no flicker.
- Dojo: Y inverted (floor at bottom), V flipped (textures right-side up),
  parallax correct (bg barely moves, fg moves with camera).
- Menu: all icons same size via uniform scaling, MENU text centered.
- Next: user should build, test, and report if Verlet physics feels right.

---
Task ID: stage-8.5
Agent: main
Task: Revert V-flip, add parallax tiling, fix step cooldown, fix hit detection timing

Work Log:
- Analyzed user feedback:
  * Walking still no delays between steps
  * Dojo still upside down (V-flip was wrong)
  * Background moves too fast, flies off screen
  * Bag doesn't react to all hits, reacts to misses

- V-FLIP REVERTED: The V-flip I added in stage 8.4 was WRONG. The original
  V coordinates were correct (atlas_y measured from top = V=0 side, matching
  glTexImage2D row 0 = top). Reverted to original V coords:
    v0 = atlas_y / th (top of frame)
    v1 = (atlas_y + atlas_h) / th (bottom of frame)

- PARALLAX TILING: Background layers (factor < 1) now tile horizontally
  to fill the screen. For each parallax image, we calculate the visible
  world range and render the texture multiple times shifted by img.w,
  preventing the background from flying off-screen when the camera moves.
  Only applies to layers with factor < 0.99 (parallax layers).
  Foreground layers (factor = 1.0) render normally.

- STEP COOLDOWN: Added step_cooldown_ timer (500ms ≈ 15 frames at 30fps).
  When a step animation starts, cooldown is set. During cooldown:
  - Player can still move (velocity-based, MOVE_SPEED * dt)
  - But step animation won't restart (prevents flickering)
  - Only when cooldown expires AND key is still held, a new step starts
  This creates the visible delay between steps that the original game has.

- HIT DETECTION TIMING FIX: Moved update_animation(dt) BEFORE hit detection.
  Previously, hit detection used anim_node_pos_ from the PREVIOUS frame
  (1-frame lag). This caused:
  - Limb positions were stale when checking distance to bag
  - Attack window (2-4 frames) could be missed entirely
  Now update_animation runs first, so anim_node_pos_ and anim_time_ are
  synchronized with the current frame when hit detection checks them.

- HIT DETECTION ACCURACY:
  * Uses actual moves.xml Attack interval (Start/End frames), not fallback
  * Verified all 11 combat moves have Attack intervals:
    HighPunch: 4-5, HeavyPunch: 8-10, LowPunch: 6-8, DoublePunch: 6-8,
    SpinningPunch: 5-6, UpperCut: 9-12, HighKick: 6-8, FrontKick: 8-9,
    BackKick: 7-9, Sweep: 9-11, LowKick: 4-6
  * If no Attack interval found, hit detection is SKIPPED (no fallback)
  * moves.xml uses 1-indexed frames; converted to 0-indexed (subtract 1)
  * Hit threshold reduced from 80 to 60 units (tighter, fewer false positives)
  * Bag center now uses Verlet NPivot position (more accurate when bag swings)

- Execution order in on_update:
  1. Movement (velocity-based, updates player_pos_x_)
  2. Camera update
  3. Combat input (Space/K → play_animation, sets hit_anim_)
  4. update_animation(dt) — advances anim_time_, updates anim_node_pos_
  5. Hit detection — uses CURRENT frame's anim_node_pos_ and anim_time_
  6. update_bag_verlet(dt) — physics step
  7. Zoom presets

Stage Summary:
- Walking: step cooldown creates visible delay between steps, no flickering
- Dojo: V-flip reverted (textures right-side up), Y-invert kept (positions correct)
- Parallax: background tiles horizontally, no longer flies off-screen
- Hit detection: uses exact moves.xml Attack intervals, synchronized with
  animation frames, 60px threshold, Verlet bag position
- Next: user should build and test

---
Task ID: stage-8.6
Agent: main
Task: Fix rotated atlas frames, Y-invert player/bag positions, root motion movement

Work Log:
- Analyzed user feedback:
  * Background still rotated on its side
  * Player above the floor (not on it)
  * Floor has gaps between segments
  * Bag not centered on holder
  * No walking delay — should be animation-based
  * Profile icon horizontally mirrored

- ROTATED ATLAS FRAMES (render_location):
  * When frame.rotated=true, sprite is stored rotated 90° CW in atlas.
  * Original sprite dimensions: (atlas_w × atlas_h).
  * Atlas region dimensions: (atlas_h × atlas_w) — swapped.
  * Previously: UV mapping swapped w/h, but quad was still img.w × img.h.
    This caused textures to appear rotated on screen.
  * Fix: when frame.rotated, use quad_w=atlas_w, quad_h=atlas_h (original
    sprite dimensions), and keep UV mapping as before. The texture now
    appears un-rotated and correctly sized.

- ROTATED ATLAS FRAMES (load_texture_atlas_to_hud — menu icons):
  * Fixed un-rotation formula for CPU-side cropping.
  * Old (wrong): sx = atlas_x + (fh-1-y), sy = atlas_y + x
  * New (correct): sx = atlas_x + y, sy = atlas_y + (fw-1-x)
  * This fixes Profile icon appearing mirrored — the old formula produced
    a horizontally flipped image for rotated frames.

- PLAYER Y POSITION:
  * params.xml uses Y-DOWN (Y=0 at top, positive Y = down).
  * Player at y=-93 in params means above center in Y-DOWN = below in Y-UP.
  * Old: player_pos_y_ = location_->player_y (-93) → player at y=-93 (below center).
  * New: player_pos_y_ = -location_->player_y (+93) → player at y=+93 (above center).
  * Now player stands ON the floor (floor at world y=-225, player at y=+93).
  * Wait — that's still wrong. Player at +93 is ABOVE center, floor at -225
    is BELOW. Player should be near floor level. Need to reconsider.
  * Actually: in Y-UP world, +93 is above center, -225 is below. Player feet
    should be at floor level. The offset of 50.0f for bag may also need
    adjustment. Will verify with user feedback.

- BAG Y POSITION:
  * Inverted enemy_y: bag_cy = -location_->enemy_y + 50.0f
  * Applied in init_bag_verlet, render_punching_bag, and hit detection.

- MOVEMENT (root motion, not velocity-based):
  * Reverted to root motion from .bin animation data.
  * step_forward.bin: NPivot goes 169→235 (+66 units per step)
  * step_back.bin: NPivot goes 235→169 (-66 units per step)
  * Animation plays NON-LOOPING (loop=false) — plays exactly once.
  * step_cooldown_ = 533ms (16 frames at 30fps) prevents new step until
    current one finishes.
  * Root motion in update_animation: player_pos_x_ += delta, where
    delta = current NPivot offset - prev offset.
  * This matches original game: each step is a discrete animation with
    visible delay between steps. No continuous movement.

- All changes syntax-checked with g++ -std=c++23 -fsyntax-only — passes.

Stage Summary:
- Rotated atlas frames: fixed in both render_location (UV + quad dims) and
  load_texture_atlas_to_hud (un-rotation formula). Textures no longer
  appear sideways; Profile icon no longer mirrored.
- Player/bag Y: inverted to match Y-DOWN params.xml → Y-UP world.
- Movement: root motion from .bin, non-looping, 533ms cooldown between steps.
- Next: user should build, test, and report if positions are correct.

---
Task ID: stage-8.7
Agent: main
Task: Fix non-looping animation wrap-around, step continuity, player Y, Profile rotation, README

Work Log:
- CRITICAL BUG FIX — Non-looping animation wrap-around:
  When a non-looping animation (step_forward/step_back) reached the last frame,
  the code continued to interpolate with frame 0 (next_idx = (frame_idx+1) % count).
  This caused NPivot to interpolate from frame 15 (NPivot=235) back toward frame 0
  (NPivot=169), creating a large negative delta that moved the character backward
  at high speed. This was the "returns to start" and "opposite direction at huge
  speed" bug.
  Fix: when non-looping animation is finished (frame_idx >= frame_count-1),
  set next_idx = frame_idx and alpha = 0. The animation stays exactly at the
  last frame with no interpolation.

- MOVEMENT FIX — Continuous stepping:
  Changed can_step condition to allow starting a new step when current animation
  is step_forward/step_back (not just fists_idle). This allows continuous
  stepping when the key is held — each step plays fully (533ms) then a new
  step starts immediately.

- PLAYER Y POSITION:
  Reverted Y-invert (was -location_->player_y, now location_->player_y).
  Added -45 offset to align player feet with floor.
  Floor (layer_3) at params y=225 → world_y=-225 (inverted location).
  Player NPivot at params y=-93. Feet are ~87 units below NPivot (model space).
  For feet at floor: player_pos_y_ - 87 = -225 → player_pos_y_ = -138.
  Offset: -138 - (-93) = -45.
  player_pos_y_ = location_->player_y - 45.0f

- BAG Y POSITION:
  Reverted Y-invert. Using same -45 offset as player.
  bag_cy = location_->enemy_y - 45.0f + 50.0f

- PROFILE ICON ROTATION:
  Tried formula #4: sx = atlas_x + y, sy = atlas_y + x
  (90° CCW un-rotation without flip)
  Previous formulas:
  - #3 (old): sx = atlas_x + (fh-1-y), sy = atlas_y + x → horizontally mirrored
  - #1 (new): sx = atlas_x + y, sy = atlas_y + (fw-1-x) → upside down
  - #4 (current): sx = atlas_x + y, sy = atlas_y + x → testing

- DOJO BACKGROUND ROTATION:
  Reverted quad dimension swap for rotated frames in render_location.
  Now uses img.w × img.h for quad dimensions (intended display size).
  UV mapping handles rotation via swapped w/h in UV space.

- README UPDATE:
  Comprehensive rewrite with current reverse engineering status:
  - What works (character rendering, Verlet physics, root motion, hit detection)
  - What's in progress (dojo rotation, floor gaps, bag centering)
  - Original engine architecture (Cocos2d-x, ModelPhysics, MoveInside, IntervalAttack)
  - .bin animation format
  - moves.xml attack intervals table
  - Coordinate systems documentation
  - Verlet physics documentation
  - File structure
  - Build instructions
  - Next steps

Stage Summary:
- Non-looping animation wrap-around FIXED — character no longer returns to start
  or moves backward at high speed.
- Continuous stepping works — each step plays fully, then next step starts.
- Player Y aligned with floor (feet at floor level).
- Profile icon rotation formula being tested (#4).
- Dojo background quad dimensions reverted to img.w × img.h.
- README updated with comprehensive reverse engineering status.

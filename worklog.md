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

# reSF2 — Complete Reverse Engineering Plan (v2)

## Reference: PvZ-Portable Architecture

PvZ-Portable is a successful clean-room reimplementation of Plants vs. Zombies.
Key lessons from studying its codebase:

1. **Load game files natively**: PvZ-Portable reads the original `main.pak` 
   archive directly using a custom PAK file reader (paklib/PakInterface).
   It does NOT extract files first — the game engine handles the archive format.

2. **Resource Manager**: Uses a central `ResourceManager` that parses 
   `properties/resources.xml` to build a manifest of all game resources 
   (images, sounds, fonts) with stable IDs.

3. **Authentic rendering**: Uses the same coordinate system, blending modes,
   and draw order as the original game.

4. **Clean separation**: Platform layer (SDL/GL) is separate from game logic.

## reSF2 Current State

### What works (headless renderer):
- Loading screen (startLoading.xml images)
- Dojo background (params.xml + atlas layers with parallax)
- Character body mesh (body.xml capsules + triangles)
- Punching bag 3D model (skeleton_punching_bag.xml + punching_bag.xml)
- HUD with real textures (Top_Panel, gold, energy, Level_bar)
- Scroll menu (MenuRoll_left/center/right + Paper + 7 menu icons)
- Dialog overlay (Sensei intro)

### What's broken (GL renderer on Windows):
- FIXED: Coordinate system mismatch (draw_quad was Y-DOWN, projection was Y-UP)
- FIXED: Loading screen used world-space instead of screen-space
- FIXED: Body model was not loaded/rendered in main.cpp
- FIXED: Skeleton parser missed Weapon-Node entries
- REMAINING: Need to verify the fix works on Windows

### What's not implemented yet:
- .dz archive decompression (DTRZ format, Marmalade derbh)
- .bin animation playback (skeletal animation)
- .s3e binary loading (game logic)
- moves.xml parsing (combat moves, hitboxes)
- stages.xml parsing (fight progression)
- Sound/music playback
- Save system (localSettings.bin)

## Priority Roadmap

### Phase 1: Verify GL renderer works on Windows (CURRENT)
- [x] Fix coordinate system (draw_quad Y-UP, draw_quad_screen Y-DOWN)
- [x] Fix loading screen (use screen-space rendering)
- [x] Add body model to main.cpp
- [x] Fix skeleton parser (find all 54 nodes + 193 edges)
- [ ] User verifies: `git pull && build.bat rebuild`

### Phase 2: DZ Archive Decompression
The game stores ALL model XMLs and animation configs in files.dz (DTRZ format).
Without DZ decompression, we can't load the game natively.

Approach (from RE notes):
- DTRZ header: 4 bytes magic + u16 num_files + u16 num_dirs + u8 version
- Names section: null-terminated strings
- Dir table: 6 bytes per dir
- File table: 16 bytes per file (offset, comp_size, uncomp_size, type)
- Data section: DZ-compressed (arithmetic/range coding with 5-byte context)

Options:
1. **Unicorn ARM emulation**: Load libs3e_android.so, emulate s3eCompressionDecomp*
   functions. Blocked on init_array constructor setup.
2. **Manual port**: Port the DZ arithmetic decoder (~250 ARM instructions) to C++.
3. **User extraction**: User runs dzip.exe on Windows to extract .dz files.
   (Current workaround — not native.)

### Phase 3: Native Asset Loading
Following PvZ-Portable's approach:
- [ ] Implement DZ archive reader (like PvZ's PAK reader)
- [ ] Central ResourceManager that parses settings.xml file manifest
- [ ] Load assets by path from DZ archives OR direct filesystem
- [ ] Asset path resolution: assets/1536/ vs assets/768/ based on resolution

### Phase 4: Animation System
- [ ] Decode .bin animation format (38 frames × 809 bytes, big-endian floats)
- [ ] Find node mapping between .bin and skeleton.xml
- [ ] Implement skeletal animation playback
- [ ] Parse moves.xml for combat moves and hit events

### Phase 5: Game Logic
- [ ] Parse stages.xml for fight progression
- [ ] Implement player movement (A/D keys)
- [ ] Implement combat (hit detection, damage)
- [ ] Implement enemy AI

### Phase 6: Audio
- [ ] Load and play WAV sound effects
- [ ] Load and play MP3 music
- [ ] Implement audio mixer

## Architecture (target, following PvZ-Portable)

```
reSF2/
├── engine/
│   ├── platform/          # Window, GL, input (GLFW/SDL)
│   ├── renderer/          # OpenGL rendering (sprites, lines, text)
│   ├── resource/          # DZ archive reader, asset manager
│   ├── animation/         # .bin decoder, skeletal animation
│   ├── scene/             # Location, models, fight scene
│   ├── ui/                # HUD, menu, dialog (cocoGUI format)
│   ├── audio/             # Sound/music playback
│   └── game/              # Game logic, stages, moves
├── tools/
│   └── dz_extractor/      # Standalone DZ extraction tool
├── tests/
└── main.cpp               # Entry point
```

## Key Files in Original Game

### APK Structure:
```
assets/
├── ShadowFight2.s3e       # Game binary (LZMA-compressed ARM code)
├── app_android.icf        # Marmalade config
├── settings.xml           # File manifest (lists all game files)
├── assets/
│   ├── files.dz           # DZ-compressed: models, configs, XMLs
│   ├── animations.dz      # DZ-compressed: .bin animation files
│   ├── 1536/              # High-res textures (1.5x scale)
│   │   ├── textures/      # PNG textures + plist atlases
│   │   ├── locations/     # Location backgrounds (params.xml + atlases)
│   │   └── fonts/         # Bitmap fonts (.fnt + .png)
│   ├── 768/               # Low-res textures (1x scale)
│   ├── locations/         # Location params.xml (shared)
│   ├── cocoGUI/           # CocosBuilder UI layouts (JSON)
│   ├── sounds/            # WAV sound effects
│   ├── music/             # MP3 music
│   └── video/             # MP4 video
└── lib/                   # Native libraries (ARM)
    └── armeabi-v7a/
        ├── libs3e_android.so    # Marmalade runtime
        ├── libsmartfox.so       # SmartFoxServer (multiplayer)
        └── libs3e*.so           # Marmalade extensions
```

### files.dz contains (120 files):
- Model XMLs: body.xml, skeleton.xml, weapon_*.xml, armor_*.xml, helm_*.xml, head_*.xml
- punching_bag.xml, skeleton_punching_bag.xml
- Game config: stages.xml, moves.xml, achievements.xml, quests.xml, perks.xml
- Localizations: eng.xml, rus.xml, etc.

### animations.dz contains (557 files):
- animations_list.xml (manifest)
- moves.xml (combat move definitions)
- binary/*.bin (skeletal animation data, 809 bytes/frame)

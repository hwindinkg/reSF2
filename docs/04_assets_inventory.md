# 04 — Assets inventory

> Source: `assets/` directory of the APK.
> 1 866 files, 86 588 296 bytes total.

## File-type breakdown

| Extension | Count | Total bytes | Format | Status |
| --------- | ----: | ----------: | ------ | ------ |
| `.png`    | 1 333 |   ~50 000 000 | PNG (some with lowercase `.png`, some `.PNG`) | ✅ standard |
| `.plist`  |   148 |    ~1 500 000 | Cocos2d-x TexturePacker atlas descriptor v2 | ✅ decoded |
| `.atf`    |   110 |    ~5 000 000 | zlib-compressed custom blob (weapon-pair tactics) | ⚠️ partially decoded |
| `.xml`    |    77 |    ~5 000 000 | Various: settings, models, animations, localizations | ✅ text |
| `.wav`    |    76 |    ~3 000 000 | RIFF WAVE PCM | ✅ standard |
| `.jpg`    |    70 |    ~8 000 000 | JPEG (full-screen backgrounds, location art) | ✅ standard |
| `.fnt`    |    16 |       ~20 000 | Cocos2d-x bitmap-font descriptor (text XML) | ✅ text |
| `.json`   |    11 |       ~30 000 | CocoGUI UI scene files | ✅ text |
| `.mp3`    |     5 |    ~7 000 000 | MP3 music | ✅ standard |
| `.PNG`    |     3 |       ~15 000 | PNG (uppercase ext) | ✅ standard |
| `.dz`     |     2 |       ~10 000 | DTRZ custom archive | ⚠️ magic known, full unpack pending |
| `.mp4`    |     1 |    ~3 000 000 | MP4 video | ✅ standard |
| `.ini`    |     1 |          ~500 | INI config (`googleActivity.ini`) | ✅ text |
| `.icf`    |     4 |        ~1 700 | Marmalade INI-style config | ✅ text |
| `.s3e`    |     1 |    2 858 937 | LZMA-compressed Marmalade S3E binary | ⚠️ magic known, sections not mapped |
| `.ttf`    |     1 |      773 236 | TrueType font (Arial) | ✅ standard |

## Top-level `assets/` (root)

| Path                      | Bytes   | Role |
| ------------------------- | ------: | ---- |
| `ShadowFight2.s3e`        | 2 858 937 | LZMA-compressed Marmalade binary (game code + data) |
| `app_android.icf`         |     376 | Marmalade config (Android, no-OBB build) |
| `app_android_obb.icf`     |     503 | Marmalade config (Android, OBB build) |
| `app_ios_store.icf`       |     449 | Marmalade config (iOS App Store build) |
| `app_localbuild.icf`      |     357 | Marmalade config (local dev build) |
| `settings.xml`            |   5 910 | Master manifest listing all gameplay XML files |
| `Fonts/Arial.ttf`         | 773 236 | System Arial fallback |
| `vg_close.png`            |   6 839 | GDPR / privacy consent UI button |
| `vg_cta.png`              |   6 437 | GDPR CTA button |
| `vg_cta_disabled.png`     |   5 734 | GDPR CTA disabled |
| `vg_mute_off.png`         |   7 107 | Mute toggle (off) |
| `vg_mute_on.png`          |   6 924 | Mute toggle (on) |
| `vg_privacy.png`          |   1 324 | Privacy info icon |

## `assets/assets/` (game asset tree)

### Root-level files

| Path                        | Bytes   | Role |
| --------------------------- | ------: | ---- |
| `animations.dz`             |   3 088 | DTRZ archive of animation XML |
| `files.dz`                  |   6 740 | DTRZ archive of file manifest (`files_list.xml`) |
| `devices.xml`               |  ~3 000 | Device capability profile |
| `obbSettings.xml`           |  ~1 500 | OBB expansion-file settings |
| `versionController.xml`     |  ~2 000 | Version / content-gating config |
| `googleActivity.ini`        |     500 | Google Activity params |

### Scale directories `1536/` and `768/`

Two parallel asset trees for different screen densities. `1536/` is the
primary (high-density), `768/` is the fallback. Both have the same
subdirectory structure:

```
1536/
├── fonts/           # 16 .fnt + .png pairs + eng/ and rus/ subtrees
├── image/           # UI icons (achievements, attributes, battles, ...)
├── location_effects/ # per-location effect defs
├── locations/       # per-location texture packs
└── textures/        # shared texture atlas library
```

#### `1536/fonts/` (16 pairs of `.fnt` + `.png`)

| Font                       | Purpose |
| -------------------------- | ------- |
| `CarterOne_num_240`        | Numbers (large, achievements) |
| `CarterOne_numbers_220`    | Numbers (medium) |
| `Carter_one_45`            | Carter One variant (small) |
| `eng/sakkal`               | English: Sakkal (decorative) |
| `obelix`                   | Obelix (display) |
| `rus/optima`               | Russian: Optima |
| `sakkalNumbers`            | Numbers (Sakkal style) |
| `sakkalShadowNumbers`      | Numbers (Sakkal w/ shadow) |
| ... (8 more)               | Various weights and sizes |

Cocos2d-x bitmap font format: XML descriptor + PNG texture atlas.

#### `1536/image/` (UI icon library)

10 subcategories:

| Subdir            | Contents |
| ----------------- | -------- |
| `achievements/`   | Achievement badge icons (bronze/silver/gold) |
| `attributes/`     | Player attribute icons (HP, damage, etc.) |
| `battles/`        | Battle mode icons |
| `combobuttons/`   | Combo-sequence button icons |
| `enchantments/`   | Weapon enchantment icons |
| `sales/`          | Sale / promotion banners |
| `skills/`         | Skill tree icons |
| `ut_items/`       | Usable / consumable item icons |
| `users/`          | User avatar icons |
| `zones/`          | Zone / map icons |

#### `1536/textures/` (shared texture atlases)

15 subcategories:

| Subdir            | Contents |
| ----------------- | -------- |
| `buttons/`        | UI button textures (with `.plist` atlases) |
| `effects/`        | Particle / spell effect textures |
| `fight/`          | In-fight HUD and overlay textures |
| `fullscreen/`     | Full-screen background images (loading screens) |
| `hints/`          | Tutorial hint textures |
| `joystick/`       | Virtual joystick textures |
| `logos/`          | Game / brand logos |
| `misc/`           | Misc shared textures |
| `panels/`         | UI panel textures |
| `raids/`          | Raid-mode specific textures |
| `scrolls/`        | Scroll / list backgrounds |
| `sliders/`        | Slider UI textures |
| `screens/`        | Full-screen overlay screens |

Each `.plist` atlas is paired with a same-name `.png`. All `.plist`s are
TexturePacker format v2 (verified by `<integer>2</integer>` in the
metadata).

#### `1536/locations/` (8 location texture packs)

`new_year_dojo`, `dojo`, `moon`, `mountain`, `bamboo_grove`, `arena` —
these are the **single-player** battle locations that ship in the base
APK. The other 50+ locations (see `01_apk_structure.md`) are loaded
from OBB or downloaded post-install (raid locations).

### `assets/assets/locations/` (50+ location directories)

Each location dir contains:
- A `background.jpg` (or several, for parallax layers)
- `*.png` textures for foreground objects, props, characters
- `*.plist` atlases for animated elements (water, fire, falling leaves)
- `*.xml` defining parallax layers, particle effects, lighting

Locations (50 dirs, see `01_apk_structure.md` for the full list):

`arena`, `autumn`, `bamboo_grove`, `battlefield`, `bridge`,
`burning_town`, `capsules`, `castle_and_bridge`, `cave`, `chess_yard`,
`dark_room`, `dojo`, `eggs`, `emerald_forest`, `factory`, `fatum_raid`,
`flooded_village`, `flowers_field`, `flying_rocks`, `flying_rocks_small`,
`fuji`, `fungus_raid`, `graveyard_ships`, `heaven`, `ice_cave`,
`lamps_on_water`, `lava`, `magic_rocks`, `megalith_raid`, `moon`,
`mountain`, `neural_network`, `new_year_dojo`, `night_bridge`,
`pink_lake`, `road`, `ruins_village`, `ruins_village_small`, `sakura`,
`shadow_gate`, `ships`, `skyport`, `snowy_peak`, `spaceship`,
`spaceship_thorny`, `statue`, `stone_dragon`, `stone_forest`,
`stone_forest_thorny`, `swamp`, `village`, `volcano`, `vortex_raid`,
`vulcan_raid`, `waterfall`, `waterfall_small`.

### `assets/assets/tactics/` (110 `.atf` files)

Weapon-vs-weapon combat tactics. Each file describes the exchange table
for one weapon pair. Two naming patterns:

- `<weapon>.atf` (19 files): e.g. `_batons.atf`, `_claws.atf`,
  `_fists.atf`, `_knives.atf`, `_knuckles.atf`, `_kusarigama.atf`,
  `_machete.atf`, `_ninjasword.atf`, `_nunchaku.atf`, `_sai.atf`,
  `_scythe.atf`, `_spear.atf`, `_steelclaws.atf`, `_swords.atf`,
  `_tonfa.atf`, `_.atf` (the "empty hands" / fist base case).
- `<weaponA>_<weaponB>.atf` (~91 files): e.g. `batons_claws.atf`,
  `batons_fists.atf`, `batons_knives.atf`, `batons_knuckles.atf`,
  `batons_kusarigama.atf`, `batons_machete.atf`, `batons_ninjasword.atf`,
  `batons_nunchaku.atf`, `batons_sai.atf`, `batons_spear.atf`, ...

Weapons enumerated:
- `batons` (Tonfa-like sticks)
- `claws` / `steelclaws`
- `crescentknives`
- `fists`
- `keris` (Kris dagger)
- `knives`
- `knuckles`
- `kusarigama`
- `machete`
- `ninjasword`
- `nunchaku`
- `sai`
- `scythe`
- `spear`
- `swords`
- `tonfa`

The zlib-decompressed payload of `kusarigama_nunchaku.atf` starts with:

```
01 00 00 00 Kusarigama\0Nunchaku\0 ...
```

So the format is: a 4-byte version/length prefix + two null-terminated
weapon-name strings + the tactics data (probably a 2D table of move /
response / frame window tuples). Stage 4 will document the exact
byte layout.

### `assets/assets/cocoGUI/` (Cocos2d-x CocoGUI JSON scenes)

11 JSON files for the raids UI:

| Path                                       | Purpose |
| ------------------------------------------ | ------- |
| `raids/Top100Dialog.json`                  | Top-100 leaderboard dialog |
| `raids/league_leaderbord.json`             | League leaderboard (note the typo "leaderbord") |
| `raids/Roll.json`                          | Raid reward roll animation |
| `raids/daily/DailyTaskCell.json`           | Daily-task list cell |
| `raids/playerInfo/general_info.json`       | Player info: general tab |
| (6 more)                                   | Other raid UI scenes |

These are standard Cocos2d-x CocoGUI export JSON files — version 1.x,
with `widgetTree`, `nodeTree`, and `textures` sections. Stage 4 will
implement a reader.

### `assets/assets/music/` (5 MP3 files)

| Track | Likely purpose |
| ----- | -------------- |
| (filenames not enumerated here for legal reasons) | Menu / fight / boss / victory / defeat themes |

Standard MP3, 128–192 kbps CBR (typical for 2014 mobile). Stage 7.5
will use miniaudio or libopenmpt for playback.

### `assets/assets/sounds/` (76 WAV files)

SFX for hits, blocks, footstep, weapon swing, UI clicks, etc. Standard
RIFF WAVE, PCM 16-bit, 22–44.1 kHz mono.

### `assets/assets/video/` (1 MP4 file)

The intro / studio-logo video. H.264 baseline + AAC, standard MP4
container. Stage 7.x will use libavcodec / libavformat to play this
(either the system FFmpeg or a vendored copy).

### `assets/assets/credits/`

End-credits material (images + text).

## Game data XMLs (referenced by `settings.xml`)

`settings.xml` is the master manifest of XML data files. Categories:

### Top-level game data (`assets/assets/`)

| File                     | Purpose |
| ------------------------ | ------- |
| `Achievements.xml`       | Achievement definitions |
| `CharacterProgress.xml`  | XP / level progression table |
| `ComputerSettings.xml`   | AI difficulty tuning |
| `config_cdn.xml`         | CDN URLs for post-install downloads |
| `forge.xml`              | Weapon upgrade / forge recipes |
| `internalSettings.xml`   | Internal engine settings |
| `list.xml`               | Top-level asset list |
| `localization.xml`       | Localization index |
| `perks.xml`              | Perk / skill tree definitions |
| `quests.xml`             | Quest definitions |
| `raid_stages_default.xml`| Raid stage definitions |
| `stages.xml`             | Single-player stage progression |
| `tacticSettings.xml`     | Combat tactics tuning |
| `usersDefault.xml`       | Default user profile |
| `userSettings.xml`       | User settings schema |

### Animations (`assets/assets/animations/`)

| File            | Purpose |
| --------------- | ------- |
| `moves.xml`     | Move / animation definitions |
| `moves.xsd`     | XML schema for `moves.xml` (rare — included!) |

The presence of `moves.xsd` means we have the **authoritative schema**
for the move XML format. Stage 4 will use this directly.

### Localizations (`assets/assets/localizations/`)

12 language files: `chn.xml`, `chn_tr.xml` (traditional Chinese),
`eng.xml`, `fra.xml`, `ger.xml`, `ita.xml`, `jpn.xml`, `kor.xml`,
`por.xml`, `rus.xml`, `spa.xml`, `tur.xml`.

### Models (`assets/assets/models/`)

Character / weapon / armor / helmet definitions. Categories:

| Prefix            | Examples |
| ----------------- | -------- |
| `armor_*`         | `armor_green`, `armor_hw14_wicked_veil`, `armor_hw15_wicked_veil`, `armor_kendo`, `armor_leather`, `armor_quilted`, `armor_robe`, `armor_ronin`, `armor_super_spiked`, `armor_woman_barbarian`, `armor_woman_green`, `armor_woman_ronin`, `armor_xmas14_santa` |
| `body_*`          | `body`, `body_brick`, `body_kenji`, `body_lynx`, `body_monkey`, `body_shin`, `body_woman` |
| `head_*`          | `head`, `head_brick`, `head_disciple`, `head_kenji`, `head_lynx`, `head_monkey`, `head_night` |
| `helm_*`          | `helm_closed`, `helm_conical_hat`, `helm_gabled`, `helm_green_mask`, `helm_hw14_pumpkin`, `helm_hw14_witch`, `helm_kabuto`, `helm_kendo_mask`, `helm_light`, `helm_soldier_kabuto`, `helm_starter_pack`, `helm_super_mask`, `helm_xmas14_horns`, `helm_xmas14_santa` |
| `magic_*`         | `magic_fireball`, `magic_lightning`, `magic_root_stun` |
| `punching_bag`    | training dummy |
| `ranged_*`        | `ranged_hw15_skull`, `ranged_shurikens`, `ranged_throwing_daggers`, `ranged_xmas14_snowballs` |
| `skeleton_*`      | `skeleton`, `skeleton_heavy`, `skeleton_magic`, `skeleton_missile`, `skeleton_punching_bag` |
| `weapon_*`        | `weapon_batons`, `weapon_claws`, `weapon_daggers`, `weapon_hw14_broom`, `weapon_hw14_scythe`, `weapon_knives`, `weapon_knuckles`, `weapon_kunai`, `weapon_machete`, `weapon_ninja_sword`, `weapon_nunchaku`, `weapon_sai`, `weapon_starter_pack_tonfa`, `weapon_super_kusarigama`, `weapon_super_spear`, `weapon_swords`, `weapon_triangle_knives`, `weapon_xmas14_canes` |

The `hw14_` / `hw15_` / `xmas14_` prefixes are Halloween 2014 / Halloween 2015 /
Christmas 2014 seasonal event items.

### Quest extensions (`assets/assets/quest_extensions/`)

| File                                | Purpose |
| ----------------------------------- | ------- |
| `dynamic_discounts.xml`             | Dynamic IAP discounting |
| `promotions.xml`                    | Time-limited promotions |
| `raid_quests.xml`                   | Raid-mode quests |
| `update_1_2_0_0.xml`                | Update 1.2.0.0 content patch |
| `intermission/story.xml`            | Intermission story beats |
| `zone_1/core.xml`, `zone_1/story.xml` | Zone 1 (intro) |
| `zone_2/core.xml`, `zone_2/story.xml` | Zone 2 |
| `zone_3/core.xml`, `zone_3/story.xml` | Zone 3 |
| `zone_4/core.xml`, `zone_4/story.xml` | Zone 4 |
| `zone_5/core.xml`, `zone_5/story.xml` | Zone 5 |
| `zone_6/story.xml`                  | Zone 6 (story only) |
| `zone_7/story.xml`                  | Zone 7 (story only) |

Six core zones + a seventh "intermission" zone. This matches the
publicly known Shadow Fight 2 act structure (6 acts + boss).

## Estimated disk usage at runtime

| Asset category            | Bytes (approx) |
| ------------------------- | --------------: |
| Textures (PNG + JPG)      | ~58 000 000     |
| Audio (WAV + MP3)         | ~10 000 000     |
| Video (MP4)               | ~3 000 000      |
| XML data                  | ~5 000 000      |
| ATF tactics (compressed)  | ~5 000 000      |
| .s3e binary (compressed)  | ~2 860 000      |
| Plist / FNT / JSON / etc. | ~2 000 000      |
| Fonts (TTF)               | ~770 000        |
| **Total**                 | **~86 600 000** |

Plus the OBB expansion file (downloaded post-install for the OBB build)
contains the remaining location content not in the base APK. reSF2 will
work with the base APK assets first, and add OBB support in Stage 8.

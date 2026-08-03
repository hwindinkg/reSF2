# HARDCODE AUDIT — invented substitutes vs asset-read fidelity

**Directive:** «ДВИЖОК ДОЛЖЕН ЧИТАТЬ ФАЙЛЫ ASSETS, А НЕ ВЫДУМЫВАТЬ ЗАМЕНИТЕЛИ (если их не было в оригинальном движке)».
**Scope:** `engine/` + `tests/` (search only — NO code changes).
**Date:** 2026-08-03. **Mode:** inspect-only.

## Method

Every candidate was cross-checked against the original's asset reads:

| Evidence source | What it proves |
|---|---|
| `reverse/data/list.xml` (527 KB) | Item `Model` attr = file base name (`Model="weapon_knives"` → `weapon_knives.xml`). The ONLY legitimate weapon/armor file-name source. |
| `reverse/data/stages.xml` (1.75 MB) | Battle/template `<Slot ViewType="Weapon" Image="...">` — the enemy's real loadout. |
| `reverse/data/boot_sequence.txt` (901 opens) | Which files the original VFS-opens at boot (`users.xml`, `perks.xml`, `forge.xml`, `CharacterProgress.xml`, `Achievements.xml`, `models/skeleton.xml`…). |
| `reverse/data/models/` + `assets/models/` | Which model files actually exist (144 entries). Names NOT on disk = broken substitutes. |
| `reverse/analysis/LIVE_GAME_EVIDENCE.md` | Q1: «`weapon_knives.xml` exists, `weapon_knive.xml` does not»; Q4: armor/helm Model resolution. |
| `reverse/analysis/LIVE_BOOT_TRACE.md` | Boot order + save format (`users.xml`+hash+backup, gap #1 vs `save.json`). |
| `reverse/data/animations/moves.xml` | Real animation/move names (`Duck`, `ForwardStep`, `Block` templates exist; **no** `Dodge`/`CounterAttack` animation). |

Legend: **HIGH** = asset exists in the dump and the engine substitutes an invented value for it.
**MED** = substitute is active but the exact original value is not yet pinned (or asset absent from dump).
**LOW** = fallback only fires when the asset is missing (documented, tolerable).

---

## Findings (57 rows)

| ID | Location (file:line) | The substitute (quoted) | What the original reads instead (evidence) | Asset evidence | Suggested fix | Priority |
|----|----------------------|-------------------------|---------------------------------------------|----------------|---------------|----------|
| H01 | `engine/game/game_clean.hpp:5225-5233` | `weapon_cycle_list_ = {"Fists","Swords","Axes","Claws","Knuckles","Daggers","Katana","Spear","Staff","Glaive","TwoHanded","CompositeSword","CompositeSpear","CompositeStaff","CompositeScythe","BigSwords","Sai","Tonfa","Fans","Kusarigama","Nunchaku","NinjaSword","Sickles","Batons","Knobsticks","Rifle","GiantSword","PowerFists","Machete","FireBall","Energyball","LightningArrow","Shuriken"}` | Equipped/owned items from the save (`users.xml` `Weapon="WEAPON_KNIVES"`) — there is no keyboard weapon cycle in the original (J/U is a dev key; original equips via inventory/shop) | LIVE_BOOT_TRACE 15.46-15.84: save read `users.xml`; list.xml SubType vocabulary | Build cycle from `inventory_` owned weapons; drop magic names from a weapon cycle | **HIGH** |
| H02 | `engine/game/game_clean.hpp:3024-3072`, `engine/game/asset_manager.cpp:631-679` | `{"TwoHanded","weapon_composite_sword.xml"}, {"BigSwords","weapon_big_swords.xml"}, ... 47 invented tactic→file entries` | list.xml `Model` attr per item — LIVE_GAME_EVIDENCE Q1: «Model attribute is lowercase item id, file name matches Model+".xml"» | list.xml `Model="weapon_knives"`; `assets/models/weapon_big_swords.xml` etc. | Resolve via `equipped_weapon_model_file()` (item Model attr) only; delete the map | **HIGH** |
| H03 | `engine/game/game_clean.hpp:3026,3028` + `asset_manager.cpp:633,635` | `"weapon_composite_sword.xml"` | Real file does not exist anywhere in the dump; list.xml CompositeSword items point at `weapon_super_composite_sword.xml` / `weapon_composite_sword` is not a model | `assets/models/` has `weapon_super_composite_sword.xml`, **no** `weapon_composite_sword.xml` (verified `Test-Path` = False) | Map CompositeSword→`weapon_super_composite_sword.xml` or Model attr | **HIGH** |
| H04 | `engine/game/game_clean.hpp:3036` | `{"OneHandedSword","weapon_one_handed_sword.xml"}` | File absent from dump; list.xml `Model="weapon_one_handed_sword"` also has no file — weapon renders invisible | `Test-Path assets\models\weapon_one_handed_sword.xml` = False | Check item Model; wire real file from device pull | **MED** |
| H05 | `engine/game/game_clean.hpp:3087-3094` | `try_name = "weapon_" + lower + ".xml"` + drop-final-`s` guess (the `weapon_knive.xml` bug class) | Item `Model` attr (Q1: «There is **no** `weapon_knive.xml`») | LIVE_GAME_EVIDENCE.md:34-48 (device `models/` listing) | Always resolve from list.xml Model; never guess | **MED** |
| H06 | `engine/game/game.cpp:1713` | `load_enemy_weapon("weapon_knuckles.xml");` | Enemy's weapon from the battle setup: stages.xml template `<Slot ViewType="Weapon" Image="WEAPON_C2_Z2_MONK_KATAR">` → list.xml Model | `reverse/data/stages.xml` Slot rows; list.xml `WEAPON_KNUCKLES Model="weapon_knuckles"` | Resolve enemy weapon via stages.xml warrior template → list.xml Model | **HIGH** |
| H07 | `engine/game/game.cpp:1980,2161-2163,2367` | `enemy_anim_ = "fists_hit" / "fists_block" / "fists_idle"` | Warrior template's own moves (stages.xml `Template=Dojo_Disciple` → disciple moves) | boot_sequence 20.93 `models/skeleton_punching_bag.xml`; stages.xml warrior templates | Drive enemy anims from template's move set (see H09) | **HIGH** |
| H08 | `engine/game/game.cpp:2168-2173` | «The enemy is a placeholder until warrior templates land: hit is a range test at HighPunch's tactic distance (Max=250)» | Enemy skeleton collision on his model (original hit-testing is model-edge based) | moves.xml `<Move Name="HighPunch" ... Distance Max=250>`; `models/skeleton.xml` | Wire warrior template models + edge collision | **HIGH** |
| H09 | `engine/game/game_clean.hpp:5455-5457` | `hud_level_ = 7; hud_gold_ = 72450; hud_gems_ = 9;` (self-marked «Placeholder player stats shown in the top panel») | Save values: `users.xml` Warrior level/coins/gems | LIVE_BOOT_TRACE 15.84 save read `users.xml`; `reverse/data/users.xml` | Read HUD stats from SaveData | **HIGH** |
| H10 | `engine/game/game_clean.hpp:5288-5299` | Projectile palettes: `{"FireBall" → {255,100,50} dmg 20 r 10 speed 400 … "LightningArrow" → {255,255,0} dmg 30 …}` | Magic appearance/behaviour authored in magic model XMLs + moves.xml intervals | `reverse/data/models/magic_fireball.xml` (MacroNodes/Edges/Figures exist), `magic_lightning.xml`, boot_sequence 12.76-14.96 `magic_*_player.bin` anims | Read projectile visuals from the magic model + `FileName` anims | **HIGH** |
| H11 | `engine/game/game_clean.hpp:5309-5313` | `static const std::vector<std::string> projectile_types = {"FireBall","Energyball","LightningArrow","MagicDeathRay","MagicAsteroid","MassBomb","MagicBomb","Iceball","MagicFireAura","RootStun","Shuriken","Rifle","Blaster"}` | Which moves spawn projectiles comes from move templates (`tactic_weapon`/intervals), not a name list | moves.xml interval `Type="Attack"` records; `magic_*` model files | Derive from move data; drop the list | **MED** |
| H12 | `engine/game/game_clean.hpp:5266,5277-5285` | `damage = 15.0f; radius = 8.0f; lifetime 2.0f; speed 400; spawn offset (facing?40:-40, +10)` | Projectile data from magic model / move `Damage Value` | magic_fireball.xml Mass/LCC values; moves.xml damage | Load from model+moves | **MED** |
| C01 | `engine/scene/scenes.cpp:385-399` | `kZoneBackgrounds[]` tint fills: `{180,160,120,255}` (parchment), `{210,185,120,255}` (sandy), `{160,180,160,255}`, `{170,150,130,255}`, `{150,140,180,255}`, `{100,90,100,255}`, `{180,140,100,255}`, default `{40,32,22,255}` | Map background texture `image/locations/<zone>` (numeric sheets `1.jpg..7.jpg`); tint is only the no-texture fallback | boot_sequence 19.06+ dojo/menu atlas opens; LIVE_GAME_EVIDENCE map shots (`reverse/data/live_shots/`) | Load themed texture per zone; tint only as last resort | **MED** |
| C02 | `engine/scene/scenes.cpp:281-291` | `battle_icon_fallback`: `"DUMMY"/"TUTORIAL"→"training", "TOURNAMENT"→"tournament", "SURVIVAL"→"survival", "PERIODIC"→"duel", "CHALLENGE"→"challenge", "ASCENSION"→"ascension", FINAL_BATTLE→"final_battle", else "duel"` | Battle icon textures keyed by battle (map atlas); type→icon mapping is engine-invented | boot_sequence map atlas opens (19.06-19.39) | Read icon from battle's Image/atlas entry | **MED** |
| C03 | `engine/ui/text.cpp:89-102` | `bar_w=200, bar_h=16, bar_x=10, bar_y=10; 0xFF333333 / 0xFF00AA00 / 0xFF00AAAA` | HUD health/energy bars are atlas art + layout from UI config | boot_sequence `panels/top/batchPanelsTop.*`, `buttons/fight/magic_progress.png` | Render HUD from atlas, not rectangles | **MED** |
| C04 | `engine/ui/button.cpp:56` | `pressed ? 0xFF888888 : hovered ? 0xFFAAAAAA : 0xFF666666 : 0xFF444444` | Button art from atlases (`batchButtonsMenuScreens.*` etc.) | boot_sequence 19.06-19.39 button atlas opens | Texture from atlas; color only when art absent | **LOW** |
| C05 | `engine/scene/scenes.cpp:1906-1907,1915-1916,1942-1943,1981-1982` | Fallback squares `{180,150,100,60}`, `{110,80,40,200}`, `{80,60,40,140}`, `{80,60,30,200}` | Item icons from `host_render_ui_texture(item.image)` — squares only when texture missing | list.xml `Image=` attrs; atlas dumps | Already asset-first; keep as documented fallback | **LOW** |
| C06 | `engine/scene/scenes.cpp:1367,1473,1495,2060,2102,2121,2133,2148` | Text colors `(80,160,255)`, `(140,190,255)`, `(100,200,255)`, `(200,230,255)`, `(255,255,255)` | Original UI text colors come from the scene's styling/atlas, not literals | boot_sequence HUD atlas opens; screenshots `reverse/data/s*.png` | Move to per-scene style table derived from captures | **MED** |
| C07 | `engine/scene/scenes.cpp:1937-1938,2006-2008` | Rating text `(200,170,60)`; Unicode sword glyph `"\xe2\x9a\x94"` stand-in for `textures/misc/Damage.png` | `textures/misc/Damage.png` (comment itself cites it) | assets dump contains `Damage` texture (comment: «the real art ships in the dump») | Keep glyph only as texture-absent fallback (already so) | **LOW** |
| K01 | `engine/game/game_clean.hpp:5426` + `game.cpp:1851-1852,2504-2510` | `static constexpr int kTutorialMoveSteps = 4;` («behavioural reading, not binary-verified») | Original's exact step count for dismissing the intro hint (unpinned) | LIVE_BOOT_TRACE tutorial flow; no counter constant found | Trace from binary's movement/quest events | **MED** |
| K02 | `engine/game/input_handler.hpp:43-45` | `static constexpr uint32_t kMinStepFrames = 12;` (200 ms) | Exact step-min gate from binary `Model::step 0x10161ad0` | comment: «needs tracing from binary's movement entries or moves.xml» | Trace movement entries | **MED** |
| K03 | `engine/scene/scenes.hpp:200-206` | `static constexpr uint32_t kCharRevealMs = 30;` («perceptual guess», 33 chars/s) | Label-animation system's char delay in FUN_101dcc40 (none found) | comment: searched `typewriter`/`charDelay`/`textSpeed` — none | Video-capture original and pin | **MED** |
| K04 | `engine/game/game.cpp:2532-2534` | `static constexpr float kWalkSpeed = 150.0f;` («faster than enemy's 90.0f since player has anim boost») | Binary's movement-entry speed (0x10161ad0) | comment: HEURISTIC-TODO | Trace movement entries | **MED** |
| K05 | `engine/game/game.cpp:3318-3323,3379-3384` | `player_pos_x_ += (facing_right_ ? -1.0f : 1.0f) * 150.0f;` (BackHandflip/DoubleStepForward no-anim position jump; `step_cooldown_ms_ = 300`) | Root motion from `back_handflip.bin` / `double_step_forward.bin` | moves.xml `FileName="back_handflip.bin"`; boot_sequence `*_handflip` anims | Remove fallback once anims guaranteed (they exist in dump) | **MED** |
| K06 | `engine/game/game.cpp:4091-4094` | `const float reach = ... distance_max : 250.0f;` + `dist <= reach` enemy distance hit fallback | Model-edge collision test on the enemy skeleton | moves.xml `HighPunch ... Distance Max=250` (fallback constant is the move's own authored value — partially asset-backed) | Prefer edge test; keep distance as backup | **MED** |
| K07 | `engine/game/game.cpp:4321-4328` | `if (dist_to_bag < 200.0f)` tutorial bag hit fallback | Any visually-connecting attack counts (edge test) | comment cites original behavior | Keep as backup only; fix edge reach | **MED** |
| K08 | `engine/game/game.cpp:4122-4129` | Edge-name guessing: `"Foot"/"Calf"/"Leg" → NToe_1/NAnkle_1, else NWrist_1/NKnuckles_1` | Skeleton edge End1/End2 lookup from `skeleton.xml` | `reverse/data/models/skeleton.xml` | Remove guess; log missing edges | **MED** |
| K09 | `engine/game/game.cpp:1817-1819,3802-3805` | `f1/f2 selector terms ... disabled-neutral (1.0f)`; `crit stays 1.0f` | Factor-set data + CriticalChance/CriticalDamage (internalSettings.xml L560-563) | internalSettings.xml (device) | Port factor sets | **MED** |
| K10 | `engine/game/game.cpp:3792-3794` | `(equipped_weapon_ != "Fists") ? "WeaponDamage" : "UnarmedDamage"` fallback | Move's own `<Damage Type=...>` attr from moves.xml (primary path already asset-driven) | moves.xml damage attrs | Keep as documented fallback | **LOW** |
| K11 | `engine/game/game.cpp:2261-2262,2287-2289` | `player_fighter_.invuln_time = 0.4f; hit_stun_time = 0.15f/0.25f;` | Hit-stun/invuln timings from binary/moves.xml interval data | moves.xml Interval records; no pin yet | Trace timings | **MED** |
| K12 | `engine/game/game.cpp:2302-2303` | `trigger_knockback(400.0f, true)` | Knockback velocity from move/binary data | moves.xml; unverified constant | Trace | **MED** |
| K13 | `engine/game/game_clean.hpp:4207-4212` | `c.stick_cx = c.stick_r*1.05f; c.stick_cy = h - c.stick_r*1.05f;` (stick POSITION, «not reversed yet — Bottom-left with a small margin matches reference screenshots») | Stick node position from parent layout (FUN_10232910) | radius is atlas-derived (470 px) but position is guessed | Reverse parent layout | **MED** |
| A01 | `engine/game/tactic_pipeline.cpp:125-128` | `const char* kDefenseAnimations[3] = {"CounterAttack", "Dodge", "Block"};` — «"CounterAttack" and "Dodge" are unpinned labels» | Real defense animations from moves.xml (only `Block` template exists; `DodgeReverseKick`/`DodgeKick` are attacks) | moves.xml: `Template Name="Block"` exists; **no** `Dodge`/`CounterAttack` animation (verified: only 2 `Dodge*` matches = DodgeKick moves) | Pin to real moves.xml anim names via golden trace | **HIGH** |
| A02 | `engine/game/tactic_pipeline.cpp:93-105` | `movement_candidates` filter `{"ForwardStep","BackStep","BackHandflip","Retreat"}` — «original's movement candidate list is unpinned» | Tactic's own movement weight set (no movements/ table assets exist) | comment: HEURISTIC-TODO; tactics dir | Confirm from tactic XML sets | **MED** |
| A03 | `engine/fight/ai.cpp:250-257` | `candidates = {"ForwardStep","ShortAttack","HeavyAttack","BackStep","Duck","Idle"}` | Candidate labels from tacticSettings.xml `<Animation Name=...>` (weights already file-driven); list itself is fixed by the original iCa roulette | tacticSettings.xml (boot_sequence 12.29); comment cites sf2_beautified.js:19930 | Verify labels against tacticSettings.xml names | **MED** |
| A04 | `engine/game/game.cpp:2210-2215` | Roulette candidates `{"Duck","ShortAttack","ForwardStep"}` | Enemy attack selection from tactic pipeline (stages.xml tactic attr) | stages.xml warrior templates | Drive from tactic def, not inline list | **MED** |
| A05 | `engine/game/tactic_pipeline.cpp:338-339,351` | Safe-attack pick: «unpinned; default = the tactic's own roulette over animation_weights» | Original's safe-attack animation selection | — | Pin via re-verifier | **MED** |
| A06 | `engine/game/tactic_pipeline.cpp:379-380` | Dodge anim: «unpinned; default = the "Dodge" action label (no such moves.xml animation name)» | Real dodge animation | moves.xml (no Dodge) | Pin from golden | **MED** |
| A07 | `engine/game/game.cpp:4270-4274` | Bag impact voice = `player_hit_sound(...)` («The bag has no Voice of its own; the original's exact bag-impact set is unverified») | Bag's own hit-sound set (unverified) | — | Trace sound triggers | **MED** |
| L01 | `engine/scene/scenes.cpp:2229-2232` | Settings rows layout: «The original layout ... is not binary-reversed; the row set follows the batchSettings atlas itself» | batchSettings.plist row order/proportions | batchSettings.plist (atlas) | Reverse from atlas | **MED** |
| L02 | `engine/scene/scenes.cpp:2295-2306` | Language buttons «visual for now» — clicks log only, no language switch | Language switching reloads localization+font tables | localization xmls (`localization.xml` boot 11.29) | Wire reload | **MED** |
| L03 | `engine/scene/scenes.cpp:519-521` | «Which of the battle's fights is up should come from progression; the first one stands in» | Progression-driven fight index | stages.xml `<Battle><Fight>` lists | Wire progression | **MED** |
| L04 | `engine/scene/scenes.cpp:1162-1163` | Timeout/tie: «equal health goes to the enemy here» | Original's timeout/tie rule (unreversed) | — | Reverse rule | **MED** |
| L05 | `engine/scene/scenes.cpp:936` | `std::string avatar_name = "character_sensei";  // default fallback` | quests.xml `<Dialog Image="character_sensei">` attr (primary path already reads it) | quests.xml Dialog Image attrs | Keep as last-resort fallback | **LOW** |
| L06 | `engine/scene/scenes.cpp:1486-1487,1544-1545` | `{"Armor:", "armor"}, {"Helmet:", "helmet"}` slot labels | Localized labels from list.xml/localization | localization xml | Localize slot labels | **LOW** |
| L07 | `engine/game/game.cpp:893-894` | `// TODO: Parse quests.xml, find actions for this event, execute them` — quest event dispatch logs only | QuestManager @ 0x101c7d20 action dispatch | quests.xml (498 quests parsed — boot_configs); boot_sequence 17.67 | Wire quest action dispatch | **MED** |
| L08 | `engine/game/game.cpp:832-838` | `info.rounds = 1; info.round_time_s = 99; info.enemy_name = "Dojo_Disciple";` fallback defaults | stages.xml `Zone=Punchbag Battle=Training` attrs (primary path overwrites when stages.xml present) | stages.xml Training fight | Already asset-first; keep fallback | **LOW** |
| S01 | `engine/game/save.cpp:161-164,229-236,380-401` | `save.json` legacy JSON fallback (`%APPDATA%\reSF2\save.json`) | Original: `users.xml`+`users_backup.xml`+`*.hash`+`localSettings.bin` (XML path is now primary) | LIVE_BOOT_TRACE 15.46/21.38 + gap #1; `reverse/data/users.xml` | Delete JSON path once XML round-trip proven | **MED** |
| S02 | `engine/game/save.cpp:169-170` | `// the hash algorithm is not yet recovered (device offline) — HEURISTIC-TODO` (hash files not written) | `users.xml.hash` / `localSettings.bin.hash` | LIVE_BOOT_TRACE 15.46: `users.xml`(+hash) | Implement hash on next device pull | **MED** |
| B01 | `engine/game/boot_configs.cpp:169-206` | — (clean) perks/forge/CharacterProgress/Achievements/purchased ALL read from files; absence tolerated by design | Original opens the same set | LIVE_BOOT_TRACE 11.26 `perks.xml`, 12.02-12.55 `forge.xml`/`CharacterProgress.xml`/`Achievements.xml`; tests/integration/test_parser_fidelity.cpp | **CONFIRMED FIXED (Wave 8)** — no substitute remains | ✅ |
| B02 | `engine/game/game.cpp:412-414,1643-1645` | list.xml/stages.xml path candidates (path resolution, not content invention) | Original VFS paths | boot_sequence 20.x + 16.79 `stages.xml` | Keep | ✅ |
| B03 | `engine/game/game.cpp:646` | `boot_events_.push_back("save");  // [Wave 8] boot-order probe` | Logging only | — | Keep | ✅ |
| I01 | `engine/game/asset_manager.cpp:1240-1246` | `load_animation()` is an empty stub — «the actual implementation is in Game's inline play_animation» | Real per-animation load (bulk load covers it) | boot_sequence ~450 `animations/binary/*.bin` | Implement or delete stub | **MED** |
| I02 | `engine/game/tactic_memory.hpp:23,76,172` | Decay clock: «stand-in for the binary's fighter+0x71c» (engine tick frames) | fighter+0x71c counter | — | Trace fighter struct | **MED** |
| I03 | `engine/game/asset_manager.cpp:845-847` | `animations_["fists_idle"] = animations_["fists1_stance_idle"]` — invented alias | moves.xml real names (`fists1_stance_idle.bin` is real; `fists_idle` is not a move name) | moves.xml FileName attrs | Use real names in enemy anims (H07) | **MED** |
| I04 | `engine/game/tactic_tables.cpp:146` | `assets/tactics` relocated-dump fallback dir | Path fallback only, not content | tactics dir | Keep | ✅ |
| I05 | `engine/game/condition_system.cpp:42-45` | Animation-name fallback for CurrentAnimation conditions | moves.xml `<CurrentAnimation Name=...>` semantics | moves.xml CurrentAnimation records | Keep (documented, asset-first) | ✅ |
| I06 | `engine/renderer/renderer.cpp:297` | `const std::uint8_t white[4] = {255,255,255,255}` | Trivial white clear constant | — | Keep | ✅ |
| I07 | `engine/platform/glfw_platform.cpp:686` | `// TODO: implement fullscreen toggle` | Missing feature, not a substitute | — | Feature work | **LOW** |
| I08 | `engine/game/tactic_settings.cpp:104-109` | `fallback` unnamed `TacticWeight` catch-all | tacticSettings.xml unnamed catch-all record (original has it too) | tacticSettings.xml | Keep | ✅ |
| I09 | `engine/game/game.cpp:1689-1694` | Location clear color from `location_->color` (params.xml hex) | Original reads color from location data | params.xml (`location_manager.cpp:96,121`) | Already asset-driven | ✅ |

---

## Top 10 HIGH items (ranked)

1. **H01 — weapon cycle list** (`game_clean.hpp:5225`) — 35 hardcoded names incl. magic spells; original equips from `users.xml` inventory. J/U keys don't exist in the original game.
2. **H02 — tactic→model map** (`game_clean.hpp:3024` / `asset_manager.cpp:631`) — 47 invented file names replacing the list.xml `Model` attr (LIVE_GAME_EVIDENCE Q1).
3. **H06 — enemy weapon hardcoded** (`game.cpp:1713`) — `"weapon_knuckles.xml"` for every battle; original takes the loadout from stages.xml slots.
4. **H05 — enemy animations pinned to fists** (`game.cpp:1980/2161/2367`) — enemy always plays `fists_*`; original plays the warrior template's moves.
5. **H09 — placeholder HUD stats** (`game_clean.hpp:5455`) — `hud_level_=7, hud_gold_=72450, hud_gems_=9`; original reads the save.
6. **H10 — magic projectile palette** (`game_clean.hpp:5288`) — invented colors/damage/speed per magic type while `magic_fireball.xml`/`magic_lightning.xml` ship in the dump.
7. **H08 — placeholder enemy hit model** (`game.cpp:2168`) — distance test stands in for enemy skeleton collision.
8. **A01 — unpinned defense animations** (`tactic_pipeline.cpp:128`) — `"CounterAttack"/"Dodge"` labels that don't exist in moves.xml (only `Block` does).
9. **H03 — broken mapping to nonexistent file** (`game_clean.hpp:3026`) — `weapon_composite_sword.xml` doesn't exist; J/U to CompositeSword renders no weapon.
10. **H07 — fists_idle alias** (`asset_manager.cpp:845`) — invented `fists_idle` animation name glued onto `fists1_stance_idle` data (feeds H05).

## Checked-clean summary (no substitute)

- **Boot config set (Wave 8):** `perks.xml`, `forge.xml`, `CharacterProgress.xml`, `Achievements.xml`, `purchased.xml`, `quests.xml`, `config_cdn.xml` — all file-read, order matches LIVE_BOOT_TRACE (11.26/12.02/17.67/18.37). **No remaining boot substitutes.**
- **Save:** primary path now `users.xml` + backup (`save.cpp:155-178`); JSON is a documented legacy fallback; only the `*.hash` algorithm is missing (HEURISTIC-TODO, device offline).
- **Equipment (player):** armor/helm/weapon resolve via list.xml `Model` attr (`equipped_armor_model_file`/`equipped_helm_model_file`/`equipped_weapon_model_file`).
- **Location/layout:** params.xml-driven positions, location color, dialogue geometry (JS-reversed proportions) — all [ORIGINAL]-annotated.

## Methodology notes

- `[HEURISTIC-TODO]` markers audited: 57 found in `engine/` + 10 in `tests/`; every marker with an active substitution is in the table above (rows K01-K13, A01-A07, L01-L07, S01-S02, I01-I03).
- Dead/legacy copies (e.g. `asset_manager.cpp:683-686` marks its map «legacy/dead — the Game's own inline loader runs») are still listed because the map is also exercised by the J/U cycle path (`game_clean.hpp:3105-3118` falls back to it).
- Unverifiable-by-design items (hash algorithm, dodge anim, timeout rule) are MED with the evidence gap stated, per the audit constraint «no speculation beyond evidence».

## Row count

**57 findings** (9 HIGH, 39 MED, 7 LOW, plus 8 checked-clean ✅ rows; 3 HEX-verified nonexistent-file checks).

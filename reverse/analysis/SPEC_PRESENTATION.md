# SPEC — Presentation/Story mechanics (VS screen, location music, tutorial hints)

Target binary: `reverse/binaries/game_region_runtime.bin` (relocated ARMv7 image,
base `0x8F057000`, Ghidra `game_region_runtime.bin`, 16422 functions).
All addresses are **runtime (file) addresses** in the relocated dump.
Method: string anchors → `get_xrefs_to` → PC+8 literal-pool arithmetic (verified
against known string addresses; the decompiler's `DAT_x + -0x7xxxxxxx` renders are
the ARM `add rN,pc,rN` pattern and were resolved from the disassembly).

---

## Q1 — VS screen + fighter portraits

### Verified semantics

1. **The VS screen is the `PreFight` class** (RTTI `"8PreFight"`, str @ `0x8F796104`,
   typeinfo @ `0x8F83F36C`, base = `"6Sprite"` @ `0x8F788EB0`).
   Constructor `FUN_8F416444` (PreFight::PreFight) creates the scene and loads,
   via the texture/sprite loader `FUN_8F0775C4` (path→sprite):
   - `textures/fullscreen/VS_Fon.xml`        @ `0x8F79624C` (ref at `0x8F416478`)
   - `textures/fullscreen/Stripe_left.png`   @ `0x8F79627C`
   - `textures/fullscreen/Stripe_right.png`  @ `0x8F7962A0`
   - `textures/misc/VS.png` (the "VS" label) @ `0x8F7962C8` (ref at `0x8F416548`)
   `VS_Fon.xml` (APK `assets/1536/textures/fullscreen/VS_Fon.xml`) is:
   ```xml
   <Images Scale="1.5">
     <Image File="textures/fullscreen/VS_Fon_left.jpg"  X="-910" Y="-512" />
     <Image File="textures/fullscreen/VS_Fon_right.jpg" X="0"    Y="-512" />
   </Images>
   ```
   → the VS backdrop is **two full-screen JPEG halves** (player left / enemy right),
   plus the `VS.png` label, plus two `Stripe_*.png` accents.

2. **Portraits (VS screen + battle HUD) are avatar PNGs**, not atlas frames:
   path prefix `image/users/image/` @ `0x8F78E090`.
   - Player avatar key `"avatar_hero"` @ `0x8F7A4734` (default when profile's
     Avatar attr is empty, profile parser `FUN_8F6567E8` @ `0x8F6569B4`) →
     `image/users/image/avatar_hero.png` (exists in APK).
   - Enemy avatar key comes from battle/enemy data; fallback
     `"UnkownEnemyAvatar"` @ `0x8F7A4740` (used in the same profile parser).
   - Story characters have hard-coded paths: `character_sensei.png`
     @ `0x8F790D60`, `character_sensei_small.png` @ `0x8F79CCD8` (Kenji),
     `character_puppeteer.png` @ `0x8F793CE8`, plus `character_disciple.png`
     etc. in the APK (`assets/1536/image/users/image/`).
   - Battle-scene avatar sprite builder: `FUN_8F411EDC` — concatenates
     `image/users/image/` + avatarKey + `.png`, creates a sprite
     (`FUN_8F1E3A5C(path,0xFFFFFFFF)`), scales by screen factor
     `FUN_8F65C874()`, positions x=−110 (`0xC2DC0000`), z=3. Called from
     `FUN_8F414174` (fight HUD init, same function that loads
     `textures/fight/bars/batchFightBars` @ `0x8F7960B8`).
   - The `_small` variant (`character_sensei_small.png`) is used by the
     **tutorial HUD overlay** `FUN_8F52868C` (sensei portrait + step text).

3. **Fight HUD**: health bars come from the atlas
   `textures/fight/bars/batchFightBars` @ `0x8F7960B8`
   (refs: `FUN_8F40EA74` @ `0x8F40EB18`, `FUN_8F414174` @ `0x8F414210`).
   Atlas frames (APK `batchFightBars.plist`): `HealthBar_Full.png`,
   `HealthBar_Hit.png`, `HealthBar_Empty.png`, `CrazyBar_*`, `Raid_HealthBar_Full.png`.
   **No portrait frames in the bars atlas** — the face icons are the avatar
   sprites above, placed next to the bar.

4. **Round/FIGHT! labels**: atlas `textures/fight/labels/batchFightLabels`
   @ `0x8F796128` (ref `FUN_8F41040C` @ `0x8F4105A0`). Frames (plist):
   `fight.png` ("FIGHT!" @ `0x8F796168`), `round.png` (@ `0x8F7961A4`),
   `great.png`, `perfect.png`, `ringout.png`, `timesup.png`, `youlose.png`,
   `youwin.png`. Round pips: `textures/misc/Round_Undone.png` /
   `Round_Done.png` @ `0x8F7961D8`/`0x8F7961F8`.

5. **Scene flow**: `"startFight"` @ `0x8F792D1C` is the trigger (callers:
   zone-select `FUN_8F3AF148`, tournament `FUN_8F36F818`, story `FUN_8F4E9024`,
   dojo `FUN_8F559EE0`, raid `FUN_8F3BCF94`/`FUN_8F3B3400`). The fight screen =
   `ScreenFight` class (RTTI `"11ScreenFight"` @ `0x8F796118`, typeinfo
   @ `0x8F83F63C`, vtable methods `FUN_8F411D34`…), ctor `FUN_8F426524`.
   PreFight (VS) runs first, then ScreenFight (battle).

### Key addresses
| item | address |
|---|---|
| `8PreFight` RTTI str / typeinfo | `0x8F796104` / `0x8F83F36C` |
| PreFight ctor (VS screen load) | `FUN_8F416444` |
| VS_Fon.xml / VS.png / Stripe_* str | `0x8F79624C` / `0x8F7962C8` / `0x8F79627C`,`0x8F7962A0` |
| ScreenFight typeinfo / ctor | `0x8F83F63C` / `FUN_8F426524` |
| HUD init (bars+avatars) | `FUN_8F414174`, avatar sprite `FUN_8F411EDC` |
| avatar path prefix / keys | `0x8F78E090`, `0x8F7A4734`, `0x8F7A4740` |
| bars / labels atlases | `0x8F7960B8` / `0x8F796128` |

---

## Q2 — Location (battle) music

### Verified semantics

1. **Battle music is data-driven from `stages.xml`**: every `<Battle>` node
   carries `Music="<track-name>"` (e.g. the tutorial battle line 48:
   `Music="fight1_samurai_spirit"`; `BOSS_LYNX` → `fight10_black_warrior`;
   `BOSS_HERMIT` → `fight13_old_sensei`; Duel/Survival/Stranger/… →
   `fight1_samurai_spirit`). **Most battles literally share
   `fight1_samurai_spirit` — this is why the user hears the same track
   everywhere; it is a data property, not a code fallback.**

2. Parsing chain:
   `BattleList::parse` `FUN_8F2C3ABC` (per-battle: `FUN_8F2C2E84` **Battle::parse**,
   reads attr `"Music"` @ `0x8F78F36C` at `0x8F2C31C4`; appends to a
   vector<string> at battle+0x18). Battle object type from
   `FUN_8F2F38D0` (battle+0x13C).

3. Selection: `Battle::getMusic` `FUN_8F43BC98` — picks a **random** element
   of battle+0x18 (count/3 via 0xAAAAAAAB mul → `FUN_8F264564` = random
   index from 3 RNG calls, returns [0,n)). So a battle may define several
   tracks (attr list) and the engine randomizes.

4. Play site: `ScreenFight` ctor `FUN_8F426524` (end of ctor):
   ```c
   if (FUN_8F2F38D0(battle) == 0)  FUN_8F633E78(defaultTrack, 1);   // guarded play
   else { FUN_8F2F5188(battle,1);
          FUN_8F43BC98(&name, battle);      // random music name
          FUN_8F282EF8(name, 1);            // PLAY, loop=1
          ... }
   ```
   `FUN_8F282EF8` = music play: resolves `assets/music/<name>`, checks the
   sound manager (singleton `FUN_8F060288`), on failure logs
   `"Music: \"%s\" doesn't exist"` @ `0x8F78C124` (ref `0x8F2835BC`).

5. Files verified on disk (APK `assets/assets/music/`): `fight1_samurai_spirit.mp3`,
   `fight5_ninja_in_the_night.mp3`, `fight10_black_warrior.mp3`, `menu.mp3`,
   `act.mp3` → the played name is the **base name; the sound system appends
   `.mp3`** (the loader appends `'.'`+3 chars, `.mp3`, in `FUN_8F64B174`).

6. **Location params.xml music (numeric IDs)**: `locations/<name>/params.xml`
   has `<Root Music="6|7" ...>` (verified in APK: bamboo_grove, moon, dojo …).
   Parsed by `Stage::parse` `FUN_8F2C10C4` (attr `"Music"` @ `0x8F2C1280`,
   stored via `FUN_8F2F32DC`). Battle → location path:
   `Battle::getLocationPath` `FUN_8F43BDF8` builds
   `locations/<loc>/params.xml` (`"locations/"` @ `0x8F79724C`,
   `"params.xml"` @ `0x8F797258`).
   The numeric IDs index a **music registry** loaded at boot:
   `FUN_8F64B174` (boot state machine `FUN_8F619944`, case 5) parses
   ID+filename entries, builds `assets/music/<name>.mp3`, logs
   `"music attached : %d"` @ `0x8F7A3D40` (ref `0x8F64B4A4`).
   [UNCERTAIN] which track each ID maps to (registry content is runtime data;
   the APK ships only 5 of the referenced tracks — the rest come from the OBB/CDN).

7. **MusicFader** (`"10MusicFader"` @ `0x8F78C0E8`, typeinfo `0x8F82DD78`):
   embedded in the fight screen at **this+0x134**; `MusicFader::update`
   `FUN_8F28183C` (driven by fight-screen update `FUN_8F361BA0`):
   fade-in phase (t<fadeIn): volume=(1−t/fadeIn)·target; at t==fadeIn:
   `FUN_8F283AEC()` (stop current) + **`FUN_8F282EF8(name, 1)`** (play looped);
   fade-out phase → callback (vtable+8).

8. Other fixed scene tracks: map/zone-select `FUN_8F3A21DC` (stops current,
   plays default map track), zone select `FUN_8F3A3B9C` (switches map→zone
   track when current name matches). Guarded play helper `FUN_8F633E78`
   (plays once per flag; used by story/tutorial scenes `FUN_8F53EA84` etc.).
   Audio settings parse: `Roster::parseSounds` `FUN_8F2D8124` (sets
   `assets/sounds/`+`assets/music/` dirs, music/sound volume & mute);
   `FUN_8F2D6950` reads `Sounds`→`Music`(volume double)+`Mute`(bool).

### Rule to reproduce
`battle_music = random(battle.MusicList from stages.xml Battle@Music)`,
played as `assets/music/<name>.mp3`, looped, through the fader; missing file
→ error string, silent. Location `params.xml Music="id|id"` is a separate
ID registry (used for location/story scenes), **not** the battle track.

### Key addresses
| item | address |
|---|---|
| `"Music"` attr str | `0x8F78F36C` |
| Battle::parse / BattleList::parse | `FUN_8F2C2E84` / `FUN_8F2C3ABC` |
| Battle::getMusic (random pick) | `FUN_8F43BC98` (+`FUN_8F264564` rng) |
| ScreenFight ctor (play site) | `FUN_8F426524` |
| music play / error str | `FUN_8F282EF8` / `0x8F78C124` |
| MusicFader update / typeinfo | `FUN_8F28183C` / `0x8F82DD78`; host `FUN_8F361BA0` (this+0x134) |
| Stage::parse / Battle::getLocationPath | `FUN_8F2C10C4` / `FUN_8F43BDF8` |
| music registry load / boot | `FUN_8F64B174` / `FUN_8F619944` (case 5) |
| map / zone tracks | `FUN_8F3A21DC` / `FUN_8F3A3B9C` |
| APK tracks | `assets/assets/music/fight1_samurai_spirit.mp3` (+fight5, fight10, menu, act) |

---

## Q3 — Tutorial hints / story arrows

### Verified semantics

1. **Hint widget = `HintBox`** (RTTI `"7HintBox"` @ `0x8F7889A0`, typeinfo
   @ `0x8F828294`, base `"6Sprite"`). Ctor `FUN_8F1C0A2C`; hint textures are
   loaded from the atlas **`textures/hints/batchHints`** @ `0x8F7A3E54`
   with frames `hintsCorner.png`, `hintsArrow.png`, `hintsStroke.png`,
   `hintsPixel.png` (@ `0x8F7A3E14/24/34/44`; `"Init hintbox textures"`
   @ `0x8F7A3EE4`) → the box is the corner+stroke+fill pieces and the
   **arrow is `hintsArrow.png`**. Config: `HintWidth` @ `0x8F7A3028`,
   `HintTimeout` @ `0x8F7A3058`.

2. **Hint factory** `FUN_8F640328` = createHint(text1, text2, text3, anchor,
   timeout, flag):
   - up to **3 text lines** (text objects, empty strings skipped —
     `if (*param_1 != '\0')`),
   - container + `HintBox` (`FUN_8F1C0A2C`), text attach `FUN_8F1C2070`,
     target anchor `FUN_8F1C2260` (UI root node), starts offscreen
     (y=0xFFFFF830=−2000), timer `FUN_8F25792C(hint+0x45, 0x66=102ms, …)`,
   - **registers into the global hint list** (singleton @
     `0x8F640710`-computed, push into vector at +8/+0xC/+0x10).
   Wrappers: `FUN_8F640C84` (3-line, align 2), `FUN_8F640CEC` (field-driven
   run() — the quest-action style caller). `HintPool` class
   (`"8HintPool"` @ `0x8F7A3A6C`, typeinfo `0x8F85AD60`, vtable `0x8F85AD70`).

3. **Where hints appear**:
   - **Dojo tutorial step** `FUN_8F4D9230`: hint at the dojo button
     (param_1[0x4F]); text = localized key (19-char key literal, matches
     `tutorial_return_map`); `FUN_8F1C2324(hint,1)` (arrow on),
     scale 0.75, z-order 10000 via scene add (`vtable+0xCC`).
   - **Map zone buttons** `FUN_8F5DEAA4`: switch(0..5) — 6 zone keys,
     target node = map button `vtable+0xD4(index+1000)`, hint at its
     position, `FUN_8F1C2324(hint,0)` (no arrow), z 1000.
   - **Shop item cards** `FUN_8F3D20D0`: switch(tab 1..4) — 4 localized
     keys (weapon/armor/helm/magic), positioned relative to the item card
     (param_1+0x198) and buy button (param_1+0x19C); register via
     `FUN_8F3C8268(container, hint, 1)`.
   - Raid variants: `RaidActionIndicateRaidButton` (arrow on raid toggle,
     errors `0x8F79D184`/`0x8F79D370`), `ArrowFlashingFrames`
     @ `0x8F7A2FEC`.

4. **Tutorial system = Dojo scene state machine**, class `"8Tutorial"`
   (@ `0x8F79CBC0`, typeinfo `0x8F84B5E4`; ctors `FUN_8F52CAB4`,
   `FUN_8F52D4AC`, `FUN_8F52DAA0` — subclasses DojoTutorial etc.):
   - `FUN_8F52C170` = Tutorial::update — states 0..0x17; step bodies
     `FUN_8F52B524`, `FUN_8F52B5D8`, `FUN_8F528EB4`, `FUN_8F528F04`,
     `FUN_8F52957C` (arrows on combat buttons via `FUN_8F527E5C`),
     `FUN_8F529114`, `FUN_8F529290`, `FUN_8F5297FC`, `FUN_8F52B744`…;
   - step HUD `FUN_8F529D4C(stepIdx)` — big switch; every case localizes a
     key (`FUN_8F279150` — localization table; the `tutorial_*` keys
     @ `0x8F79CDAC`…`0x8F79CF04` have **no code xrefs by design** — they are
     looked up by name) and renders via `FUN_8F52868C`: **sensei portrait
     (`character_sensei_small.png`), step text, animated arrow/indicator**
     (indicator object via `FUN_8F63FA50`, rect 0x42FF0000/0x437F0000, arrows
     `FUN_8F1D8964`, timers);
   - config keys: `TutorialWeapon` @ `0x8F7A476C`, `TutorialBoss`
     @ `0x8F7A478C`, `TutorialTournament` @ `0x8F7A47B0` (used via
     `FUN_8F65ABF0`+0x188/0x18C/0x190 — the training weapon model, the Kenji
     boss battle, the tournament intro), `TutorialStepTimeout`
     @ `0x8F7A47D8`.

5. **Quest layer** (drives story flow incl. "send to shop"): quests are
   loaded from **`assets/quests_cdn.xml`** @ `0x8F789890` (`"parse quests.xml"`
   @ `0x8F78F7D4`, loader `FUN_8F2C69C4`). Actions (registry names @
   `0x8F7906A4`…`0x8F7915xx`): `QuestActionFight` (`0x8F790E28`),
   `QuestActionShop` (`0x8F791314`), `QuestActionChangeScene`
   (`0x8F790764`), `QuestActionGotoZone` (`0x8F790FA0`),
   `QuestActionMapFocus`/`MapMask` (`0x8F791010`/`0x8F791028`),
   `QuestActionDialog` (`0x8F7908C8`), `QuestActionGiveCurrency`
   (`0x8F790EEC`), `QuestActionGiveItem` (`0x8F790F24`),
   `QuestActionBuyItem` (`0x8F790724`), `QuestActionChangePlayerAvatar`
   (`0x8F790744`), `QuestActionOpenForge` (`0x8F791054`), …
   Quest text keys: `tutorial_begin_1/2`, `tutorial_training_fight`,
   `tutorial_buy_knives`, `tutorial_boss_hello[2]`, `tutorial_bodyguard_win_1/2`,
   `tutorial_girl_*`, `tutorial_buy_armor_1/2`, `tutorial_buy_helmet_1/2`,
   `tutorial_goodluck` (@ `0x8F79C468`…`0x8F79C750`).

6. **Post-Kenji step (the "send me to the shop" complaint)**: the tutorial
   state machine fights the boss (`TutorialBoss`, states 5-9), then
   post-win states run dialogs + the **shop tutorial step**
   (states 0xA-0xE; `FUN_8F529114` state 0xB = shop trip; state 0xC finish
   via `FUN_8F5260D4`; `"Finished Tutorial"` @ `0x8F79CD08` used in
   `FUN_8F52C170`/`FUN_8F5294D4`). In the shop, the item-card hints of
   `FUN_8F3D20D0` (tab keys → `tutorial_buy_knives` flow: "buy knives")
   point at the cards; the map/dojo hints (`FUN_8F5DEAA4`/`FUN_8F4D9230`)
   point at zone/dojo buttons.
   [UNCERTAIN] The exact key→state mapping (which `FUN_8F529D4C` case = which
   `tutorial_*` key) is only resolvable at runtime (localization table lookups
   by name; the key literals live in the parser data pool, refs were not
   auto-created by Ghidra). The step *sequence* and the shop trip are
   confirmed by the state machine structure + the `tutorial_buy_knives` key
   set + Shop scene hint code.

### Rule to reproduce
HintBox = up to 3 localized text lines + box (corner/stroke/pixel) + arrow
(`hintsArrow.png`, `ArrowFlashingFrames` for flashing), anchored to a target
node/position, registered in the global hint list, timed out after
`HintTimeout`; rendered above everything (z 10000 in dojo, 1000 on map).
Tutorial = dojo state machine showing per-step HUD (sensei avatar + text +
arrow); after the Kenji (TutorialBoss) win the story continues with dialogs
+ **shop trip (hints on item cards)** + `tutorial_return_map` hint.

### Key addresses
| item | address |
|---|---|
| HintBox typeinfo / ctor / textures | `0x8F828294` / `FUN_8F1C0A2C` / `0x8F7A3E14-44`,`0x8F7A3E54` |
| hint factory / wrappers | `FUN_8F640328` / `FUN_8F640C84`,`FUN_8F640CEC` |
| dojo / map / shop hints | `FUN_8F4D9230` / `FUN_8F5DEAA4` / `FUN_8F3D20D0` |
| Tutorial typeinfo / update / step HUD | `0x8F84B5E4` / `FUN_8F52C170` / `FUN_8F529D4C`,`FUN_8F52868C` |
| quests_cdn.xml / parse | `0x8F789890` / `FUN_8F2C69C4` |
| QuestActionFight / Shop / GotoZone | `0x8F790E28` / `0x8F791314` / `0x8F790FA0` |
| tutorial keys (localization) | `0x8F79CDAC`…`0x8F79CF04`, `0x8F79C468`…`0x8F79C750` |
| Tutorial config keys | `0x8F7A476C`/`0x8F7A478C`/`0x8F7A47B0`/`0x8F7A47D8` |

---

## Global notes
- All addresses are in the relocated dump `game_region_runtime.bin`
  (base `0x8F057000`); file-offset mapping per `RUNTIME_MAP.md`
  (`file = runtime − 0x8F057000 + 0x3E6F1`).
- [UNCERTAIN] items: (a) location-music ID→file mapping (registry runtime
  data; APK ships 5 tracks); (b) exact `FUN_8F529D4C` case↔`tutorial_*` key
  pairing; (c) `Roster::parseSounds` third key literal resolves into an
  EXIF-tag string block (`0x8F763044` = inside "ShutterSpeedValue") — the
  key string itself remains unresolved (volume/mute semantics verified by
  the surrounding code); (d) exact per-line HintBox field layout.
- quests.xml first-quest chain: the file is CDN-only (`assets/quests_cdn.xml`,
  not in this APK) — the tutorial sequence above is reconstructed from the
  in-binary state machine + keys, not from the XML itself.

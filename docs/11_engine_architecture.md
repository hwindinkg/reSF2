# 11 — Engine architecture (recovered)

> Status: Stage 3 partial. Architecture inferred from .s3e string
> analysis + manifest + native libs. The exact call graph is not
> recovered (that needs Ghidra) but the high-level architecture is
> well-constrained by the evidence below.
>
> Source: `engine/reverse/s3e_classes.txt` (85 game classes, 250+
> methods), `engine/reverse/s3e_imports.txt` (346 Marmalade imports),
> string scan of `.s3e` `.rodata`.

## TL;DR

Shadow Fight 2 is built on a **Cocos2d-x 2.x-style rendering layer
sitting on top of Marmalade SDK v8.2.1**. This is **not** the official
Cocos2d-x Marmalade port — Nekki wrote their own thin layer that mimics
the Cocos2d-x API surface (`CCNode`, `CCSprite`, `CCDirector`,
`CCScene`, `CCLayer`, `CCAction`, `CCAnimation`, `CCParticle`,
`CCTouch`, `CCEGLView`).

XML parsing is done by **pugixml** (with **tinyxml** as a secondary
parser, probably for legacy paths). Physics is **custom** (no Box2D /
Chipmunk / Bullet). Networking for raids uses **SmartFoxServer 2X**.
Save games use a custom format with **AES encryption**.

## Layered architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Game logic (Nekki proprietary)                              │
│  ┌─────────────┬──────────────┬──────────────┬────────────┐ │
│  │ Fight       │ Quest        │ Roster       │ Raid       │ │
│  │ Battle      │ QuestAction  │ ShopScreen   │ RaidManager│ │
│  │ BattleDaily │ QuestCondition│ ItemDialog  │ DisplayZone│ │
│  │ Model       │ RuleParser   │ ItemMaker    │ ServerModule│ │
│  │ ModelAnimation│RulesInspector│ LedgerManager│ SessionTracker│ │
│  └─────────────┴──────────────┴──────────────┴────────────┘ │
├─────────────────────────────────────────────────────────────┤
│  Cocos2d-x-style 2D layer (Nekki, mimics Cocos2d-x 2.x API)  │
│  CCNode, CCSprite, CCScene, CCLayer, CCDirector, CCAction,   │
│  CCAnimation, CCAnimate, CCSpriteFrame, CCSpriteBatchNode,   │
│  CCTexture2D, CCTextureCache, CCRenderTexture, CCScrollView, │
│  CCParticle, CCLabelTTF, CCLabelBMFont, CCTouch, CCEGLView   │
│  (236 references to "cocos2d" in .s3e strings)               │
├─────────────────────────────────────────────────────────────┤
│  XML parsers                                                 │
│  pugixml (primary, 4 refs) + tinyxml (secondary, 15 refs)    │
├─────────────────────────────────────────────────────────────┤
│  Networking                                                  │
│  SmartFoxServer 2X C++ client (libsmartfox.so, Sfs2X::*)     │
│  + boost::asio (TCP/UDP) + custom HTTP (for CDN downloads)   │
├─────────────────────────────────────────────────────────────┤
│  Marmalade SDK v8.2.1 runtime (libs3e_android.so)            │
│  s3e* (117 imports) — device, surface, timer, file, audio    │
│  gl* (210 imports) — OpenGL ES 2.0 + 1.1 fixed-function      │
│  IwGx (implicit, via s3eGLGetInt)                            │
├─────────────────────────────────────────────────────────────┤
│  Android platform glue                                       │
│  com.ideaworks3d.marmalade.LoaderThread (Java) + 4 native    │
│  extensions (OBB, billing, notifications, SmartFox)          │
├─────────────────────────────────────────────────────────────┤
│  Android OS (Linux kernel + ART)                             │
└─────────────────────────────────────────────────────────────┘
```

## Game-side C++ class inventory

Extracted by string analysis of `.s3e` `.rodata` (excluding the import
table region 0x1521–0x2bbf). 85 game classes with at least one
`Class::method` reference; 250+ methods total. Full list in
`engine/reverse/s3e_classes.txt`.

### Orchestration / app lifecycle

| Class | Methods | Role |
| ----- | ------- | ---- |
| `Module` | `getScreen`, `getScreenNameFromType`, `getScreenTypeFromString` | Base class for all game screens. Has a screen-type enum and string-to-enum mapping. |
| `ServerModule` | `serverRequestTimeoutCallback` | Subclass of `Module` for online-aware screens. |
| `SessionTracker` | `fileExists` | Tracks play session state on disk. |
| `EventLog` | `send` | Analytics event submission. |
| `SystemProperties` | `fileExists`, `getPathType`, `setPicturePaths` | System info + asset path resolution. |
| `DeviceComparison` | `getParameterFromString`, `updateParameter` | Device-capability comparison (for graphics quality scaling). |
| `QualityOption` | `getOptionFromString`, `turnOption` | Graphics quality enum + toggle. |

### Battle / fight

| Class | Methods | Role |
| ----- | ------- | ---- |
| `Fight` | 14 methods including `Fight`, `fillFightListParametersBuffer`, `fillMagicAndMissilesBuffer`, `getModelByAppliance`, `getModelByScreenModel`, `getModelIndexByScreenModel` | A single fight instance. Holds the model list, magic, missiles. |
| `Battle` | `nullDaily`, `update` | Base battle class (daily / periodic specialisation). |
| `BattleDaily` | `nullDaily`, `setRosterFight`, `update` | Daily-battle specialisation. |
| `BattlePeriodic` | `initBattles`, `update` | Periodic (timed) battle specialisation. |
| `Roster` | `addBattle`, `parseSounds`, `setLotteryDecksMax`, `setLotteryFreeDecksMax` | The player's roster of available battles + lottery (card-draw) state. |
| `DisplayZone` | `activeBattle`, `recreateButtonByBattle` | The map screen showing available battles per zone. |
| `FightList` | `getReward`, `putRule` | List of fights in a stage. |
| `InFightRule` | `copyInFightRule`, `getWinnerAppliance` | Per-fight gameplay rules (e.g. "player must use weapon X"). |
| `RandomRule` | `checkReset`, `resetRandom` | RNG-seeded rules (e.g. random opponent selection). |

### Models (3D characters rendered as 2D sprites)

| Class | Methods | Role |
| ----- | ------- | ---- |
| `Model` | `equipRulesItems`, `getModelAlign`, `getModelByType`, `getTotalDamage`, `setCurrentNode`, `setNearestEnemy` | A character model (player or NPC). Holds the skeletal state, equipment, and current animation node. |
| `ModelAnimation` | `getPlayerAnimation`, `mirrorNodes`, `playInfo` | Per-model animation state. `mirrorNodes` suggests left/right mirroring (the player faces right by default; NPCs face left). |

### Quests / progression

| Class | Methods | Role |
| ----- | ------- | ---- |
| `Quest` | `actionQuest` | Quest execution. |
| `QuestAction` | `getClassActionByType` | Quest action factory (one per action type: kill X, reach Y, etc.). |
| `QuestActionSetEnergy` | `parse` | Specialised action: set player energy. |
| `QuestCondition` | 8 methods including `fightCurrencyCostFunction`, `fightFunction`, `fullFunction`, `getDeliveryUpgradeTime`, `getSimOperator`, `itemFunction` | Quest success-condition evaluator. |
| `RulesInspector` | `applyNoAnimationRules`, `applyNoPerksRules`, `checkButtonRules`, `checkEvent`, `putPerkFromRule`, `putRule` | Runtime rule engine. |
| `RuleParser` | `parseInFightRule`, `parseRules`, `parseStyleType` | XML → rule objects. |
| `ConditionExtension` | `MathFunction`, `mathFunction`, `parseFunctions`, `setValue` | Math expression extension for conditions. |
| `ConditionOperator` | `isEqual` | Comparison operators. |
| `ComparisonExpression` | `compare`, `getTypeFromString` | Comparison expression evaluator. |
| `CounterConditionsParser` | `parseCondition` | Counter-based conditions (e.g. "win 5 fights"). |

### Items / shop / inventory

| Class | Methods | Role |
| ----- | ------- | ---- |
| `ItemInfo` | `getUpdateItemByIndex` | Item metadata. |
| `ItemMaker` | `createItem` | Item factory. |
| `ItemDialog` | `getItemPositionX` | Item detail dialog. |
| `ShopScreen` | `getDiffBetweenSameAttr`, `isEmptySlider` | Shop screen (subclass of `Module`). |
| `InfoSprite` | `errorCallItem`, `setDiscountLabel`, `setPercentMark` | Item sprite with badges (discount %, sale mark). |

### UI / rendering helpers

| Class | Methods | Role |
| ----- | ------- | ---- |
| `Picture` | `setPath`, `getFolder` | Image reference + folder resolution. |
| `Font` | `getRealAlignment`, `stringToAlign`, `stringToCCAlign` | Font alignment helper (converts XML strings to Cocos2d-x alignment enum). |
| `TouchController` | `getTouchSize`, `setTouchSize` | Virtual joystick / button sizing. |
| `Localization` | `getString`, `putParametersIntoString` | i18n string lookup with parameter substitution. |

### Multiplayer (raids)

| Class | Methods | Role |
| ----- | ------- | ---- |
| `RaidManager` | (1 method visible) | Top-level raid coordinator. |
| `RaidTopHundredDialog` | `onGetCurSeasonLeaderboardInfo`, `onGetCurSeasonLeaderboardInfoFail`, `onGetPrevSeasonLeaderboardInfo`, `onGetPrevSeasonLeaderboardInfoFail` | Top-100 leaderboard dialog. |
| `RaidLeaguesDialog` | (1 method visible) | League leaderboard dialog. |
| `ListSF` | 28 methods including `androidLicenseCallback`, `androidLicenseResult`, `appendCurrentVersion`, `buyItem`, `checkAndroidLicense`, `checkApplicationDump`, `isConditionFightCount` | SmartFox-aware list coordinator. Despite the name, this is not a list-of-fights; it's a high-level dispatcher for server round-trips. |

### Ads / IAP / analytics

| Class | Methods | Role |
| ----- | ------- | ---- |
| `SponsorPay` | 11 methods | SponsorPay / Fyber offerwall + interstitials. |
| `SponsorPayBanner` | 5 methods | SponsorPay banner ads. |
| `Metaps` | `getSelf`, `init`, `requestBalance`, `showOffer` | Metaps offerwall (Asian market). |
| `Tapjoy` | `onEventCallback`, `requestContent`, `sendEventAndRun`, `showEvent` | Tapjoy offerwall + video. |
| `AndroidMarket` | `requestRestore`, `restorePurchase` | Google Play IAP restore. |
| `WindowsMarket` | `requestRestore` | Windows Phone IAP restore (legacy port). |
| `LedgerManager` | `sendRequest` | Server-side transaction ledger. |
| `PayLog` | `statusToString` | Payment status enum-to-string. |
| `HashChecker` | `FileHash` | File integrity check (probably for downloaded OBB / CDN assets). |
| `CodedInputStream` | `SetTotalBytesLimit` | Google Protobuf stream (used for server comms). |

## Renderer: Cocos2d-x 2.x-style API on top of GLES2

The `.s3e` binary references the following Cocos2d-x 2.x classes:

| Cocos2d-x class | Ref count in .s3e |
| --------------- | ----------------: |
| `cocos2d` (namespace) | 236 |
| `CCAction` | 6 |
| `CCSprite` | 5 |
| `CCLayer` | 5 |
| `CCTouch` | 5 |
| `CCNode` | 3 |
| `CCSpriteFrame` | 3 |
| `CCTextureCache` | 3 |
| `CCAnimation` | 3 |
| `CCLabelTTF` | 2 |
| `CCLabelBMFont` | 2 |
| `CCTexture2D` | 2 |
| `CCScrollView` | 2 |
| `CCParticle` | 2 |
| `CCEGLView` | 2 |
| `CCDirector` | 1 |
| `CCScene` | 1 |
| `CCSpriteBatchNode` | 1 |
| `CCAnimate` | 1 |
| `CCActionManager` | 1 |
| `CCRenderTexture` | 1 |

This matches Cocos2d-x **v2.x** (the v3.x API dropped the `CC` prefix
and uses `cocos2d::Node`, `cocos2d::Sprite`, etc.). The game was
started in 2012–2013 when v2.x was current; Nekki never migrated.

### Implications for reSF2

reSF2's renderer (`engine/renderer/`) should provide a Cocos2d-x
2.x-compatible API surface (`Node`, `Sprite`, `Scene`, `Layer`,
`Director`, `Action`, `Animation`, `SpriteFrame`, `SpriteBatchNode`,
`Texture2D`, `TextureCache`, `LabelTTF`, `LabelBMFont`, `ScrollView`,
`RenderTexture`, `Particle`, `Touch`, `EGLView`). This is a well-known
public API — reSF2 can re-implement it cleanly without copying any
Nekki code.

The Cocos2d-x 2.x MIT-licensed source code is on GitHub
(`cocos2d-x/v2` branch). reSF2 will write its own implementation
against the same API, but can reference the public source for API
semantics (this is normal practice — e.g. many Cocos2d-x forks exist).

## XML parsing: pugixml + tinyxml

| Parser | Ref count | Likely use |
| ------ | --------: | ---------- |
| `pugixml` | 4 | Primary XML parser for game data XMLs (quests, models, etc.). |
| `tinyxml` | 15 | Secondary parser, probably for legacy paths or for XMLs that pugixml doesn't handle well. |

Both are open-source MIT-licensed libraries. reSF2 will vendor both
unchanged — they are not game-specific and re-implementing them would
be a waste of effort.

## Save game system

Strings found in `.s3e`:

- `assets/localSettings.bin` (loaded from `ram://` Marmalade virtual FS)
- `error during encryption` / `error during decryption`
- `AES128-CBC-SHA`, `AES256-CBC-SHA` (TLS cipher suite strings, but the
  presence of `error during encryption` strongly suggests AES is also
  used for save file encryption)
- `UserDefault.xml` — Cocos2d-x's standard user-defaults file (small
  key-value settings, NOT encrypted)
- `save_json_log`, `save_full_json_log`, `save_pay_log` — debug log
  files for save / payment flows

**Hypothesis** (confidence: high):
- Player progress is saved to `assets/localSettings.bin`.
- The file is AES-encrypted (likely AES-128-CBC with a key derived
  from the device ID + a static salt).
- Cocos2d-x `UserDefault.xml` stores only non-sensitive UI settings
  (last selected language, music volume, etc.).

**Data needed to confirm**:
- Trace the `loadGame` / save functions in `.s3e` `.text` (Stage 7.8).
- Or: capture a real `localSettings.bin` from a running install and
  attempt known-plaintext attacks against the AES key.

## Networking

### SmartFoxServer 2X

`libsmartfox.so` exports the full `Sfs2X::*` C++ API (see
`docs/02_native_libraries.md`). The game uses:

| SmartFox2X API | Use |
| -------------- | --- |
| `Sfs2X::Bitswarm::BitSwarmClient` | Main client connection (TCP) |
| `Sfs2X::Bitswarm::UDPManager` | UDP for real-time raid position updates |
| `Sfs2X::Core::Sockets::TCPSocketLayer` | TCP transport (boost::asio) |
| `Sfs2X::Core::Sockets::UDPSocketLayer` | UDP transport |
| `Sfs2X::Core::SFSProtocolCodec` | SmartFox binary protocol codec |
| `Sfs2X::Core::DefaultPacketEncrypter` | Packet encryption (likely the AES above) |
| `Sfs2X::Controllers::SystemController` | System messages (login, join room) |
| `Sfs2X::Controllers::ExtensionController` | Server extension messages (raids) |
| `Sfs2X::Util::LagMonitor` | Ping/latency tracking |
| `Sfs2X::Util::ConfigLoader` | Loads SmartFox config XML |
| `Sfs2X::Requests::LoginRequest` | Login (mentioned in `.s3e` strings) |
| `Sfs2X::Requests::JoinRoomRequest` (inferred) | Join raid room |

reSF2's `engine/network/` will implement a SmartFox2X-protocol-
compatible client. The protocol is publicly documented at
<https://smartfoxserver.com/2X/docs>.

### HTTP (CDN downloads)

The game downloads post-install content from a CDN:

- `assets/config_cdn.xml` — CDN URL config (in `settings.xml` list)
- `assets/quests_cdn.xml` — Quest definitions fetched from CDN
- Custom HTTP client (no libcurl found in `.s3e` — likely uses
  Marmalade's `IwHTTP`)

reSF2 will use libcurl (vendored) or cpp-httplib (header-only) for
HTTP.

## Physics: custom (no third-party engine)

Strings scanned for Box2D / Chipmunk / Bullet / ODE / Tokamak:

| Engine | Ref count |
| ------ | --------: |
| Box2D | 0 |
| Chipmunk | 0 |
| Bullet | 13 (false positives — these are likely "bullet" the projectile, not the physics engine) |
| ODE | 2 (false positives — "code" or similar) |
| Tokamak | 0 |

The game's physics is **fully custom**. The `.atf` tactics files
(see `docs/05_resource_formats.md`) contain per-weapon-pair combat
tables — these define hitboxes, hurtboxes, frame windows, and damage
multipliers. reSF2's `engine/physics/` will be a custom 2D collision
system driven by the `.atf` data.

Strings found:

- `Hitbox`/`hitbox`: 0 (the game uses other terminology)
- `Hurtbox`/`hurtbox`: 0
- `Collision`/`collision`: 1 (the string "collision")
- `AABB`/`aabb`: 0

This means the game does not use AABB tree structures; collision is
likely **direct rectangle-vs-rectangle testing** per active move
(since there are only 2 fighters on screen, no spatial partitioning is
needed).

## Particle / effects system

`CCParticle` (2 refs) is Cocos2d-x's standard particle system. The
game uses it for:

- Magic spells (`magic_fireball`, `magic_lightning`, `magic_root_stun`)
- Hit spark effects
- Environment effects (rain, snow, falling leaves — per-location
  `.plist` atlases under `1536/location_effects/`)

reSF2 will re-implement `CCParticle` cleanly (the API is public).

## Camera

Strings found:

- `Camera` / `camera`: 14
- `Shake` / `shake`: 5 (camera shake on big hits)
- `Zoom` / `zoom`: 9 (zoom in on critical hits)
- `Scroll` / `scroll`: 28 (camera scroll follows player movement)
- `Ortho` / `ortho`: 4 (orthographic projection — the game is 2D)
- `Projection` / `projection`: 2

The camera is a **2D orthographic camera** with:

- Follow-the-player scroll
- Shake on heavy hits
- Zoom on critical moments (zoom-in on the killing blow, zoom-out
  after a knockdown)

reSF2's camera (`engine/runtime/camera.*`) will be a simple 2D
orthographic camera with smooth follow + shake + zoom modifiers.

## Input pipeline

Marmalade provides `s3ePointerUpdate` / `s3ePointerGetState` for
multi-touch and `s3eKeyboardUpdate` / `s3eKeyboardGetState` for keys.
The game wraps these in a `TouchController` class with `getTouchSize`
/ `setTouchSize` (virtual joystick calibration).

The game registers Marmalade callbacks:

- `s3ePointerRegister(S3E_POINTER_TOUCH_EVENT, ...)` — touch down/move/up
- `s3eKeyboardRegister(S3E_KEY_EVENT, ...)` — key down/up
- `s3eDeviceRegister(S3E_DEVICE_PAUSE/UNPAUSE/INTERRUPT, ...)` — lifecycle

Plus `libgamepad.so` + `libInputDeviceExtension.so` for physical
controllers (OUYA, Moga, Xbox).

reSF2's `engine/platform/input.*` will abstract this with a
platform-neutral `InputState` struct polled once per frame.

## Main loop model

Based on Marmalade SDK convention + the strings `s3eDeviceCheckQuitRequest`
(1 ref) and `s3eTimerGetMs` (1 ref), the main loop is the standard
Marmalade pattern:

```cpp
int main() {
    s3e::Initialize();
    // ... game init: parse settings.xml, load resources, build first scene ...

    while (!s3eDeviceCheckQuitRequest()) {
        s3eDeviceYield(0);              // let OS breathe
        s3eKeyboardUpdate();
        s3ePointerUpdate();

        const uint32 now = s3eTimerGetMs();
        const uint32 dt  = now - last_frame_ms;
        last_frame_ms = now;

        // Cocos2d-x-style frame:
        // 1. CCDirector::sharedDirector()->update(dt)
        // 2. CCActionManager::sharedManager()->update(dt)
        // 3. Game-specific scene update (Module::update(dt))
        // 4. CCDirector::drawScene()  -> walks scene graph, calls visit()/draw()

        IwGxClear();
        CCDirector::sharedDirector()->drawScene();
        IwGxFlush();
        IwGxSwapBuffers();
    }

    // ... shutdown ...
    s3e::Terminate();
    return 0;
}
```

This is the Cocos2d-x 2.x `CCDirector::mainLoop()` pattern, hosted
inside Marmalade's main loop. reSF2 will use the same structure
(see `docs/12_main_loop.md`).

## What's still unknown (deferred to Stage 4+)

1. **Exact main-loop function address** in `.s3e` `.text`. Requires
   disassembly + cross-referencing `s3eDeviceCheckQuitRequest` import
   with the callsite. Stage 4 task.
2. **Scene-graph traversal order**. Cocos2d-x 2.x uses a specific
   `visit()` recursion order; reSF2 will follow the public Cocos2d-x
   2.x source for this.
3. **Damage formula**. Encoded in `.atf` tactics + `tacticSettings.xml`.
   Stage 4 task S4.4.
4. **AI state machine**. Per-NPC behaviour is in `.s3e` `.text`; we
   only see the class names (`Model::setNearestEnemy`,
   `RulesInspector::checkEvent`). Stage 7.6 task.
5. **Save file encryption key**. Needs runtime trace. Stage 7.8 task.
6. **Cocos2d-x version pin**. We know it's v2.x but not the exact
   minor version. Not critical — the v2 API was stable across 2.0–2.2.

## reSF2 target architecture (clean-room)

```
                     ┌──────────────────────────┐
                     │   engine::Runtime         │  ← main loop, scheduler
                     │   (replaces Cocos2d-x     │
                     │    CCDirector)            │
                     └──────────┬───────────────┘
                                │
       ┌───────────┬───────────┼───────────┬──────────────┐
       ▼           ▼           ▼           ▼              ▼
 ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐
 │ platform│ │ renderer │ │ physics │ │ animation│ │  audio   │
 │ (s3e*)  │ │ (Cocos2d │ │ (custom)│ │ (skeletal│ │ (miniaud │
 │         │ │  v2 API) │ │         │ │  + state)│ │  io)     │
 └────┬────┘ └────┬─────┘ └────┬────┘ └────┬─────┘ └────┬─────┘
      │           │            │           │            │
      ▼           ▼            ▼           ▼            ▼
 ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐
 │ Android │ │ GLES2 /  │ │ hitbox/ │ │ .dz +    │ │ WAV/MP3  │
 │ Linux   │ │ Vulkan / │ │ hurtbox │ │ moves.xml│ │ mixer    │
 │ Windows │ │ Metal    │ │ tables  │ │ loader   │ │          │
 │ macOS   │ │ backends │ │         │ │          │ │          │
 │ Switch  │ │          │ │         │ │          │ │          │
 └─────────┘ └──────────┘ └─────────┘ └──────────┘ └──────────┘

 Game code (clean-room reimplementation of Nekki's game logic):
   engine::game::Module             (base class for screens)
   engine::game::Fight              (single fight instance)
   engine::game::Battle             (battle types: daily, periodic)
   engine::game::Roster             (player's battle list)
   engine::game::Model              (character model + equipment)
   engine::game::Quest              (quest engine)
   engine::game::RulesInspector     (runtime rule engine)
   engine::game::RaidManager        (multiplayer raids)
   engine::game::SaveSystem         (encrypted localSettings.bin)
```

Subsystems in priority order for Stage 7:

| Stage | Subsystem | Depends on | Estimated effort |
| ----- | --------- | ---------- | ---------------: |
| 7.1 | `engine::platform` (window, GL, input, fs) | nothing | 1–2 sessions |
| 7.2 | `engine::renderer` (Cocos2d-x v2 API) | 7.1 | 2–3 sessions |
| 7.3 | `engine::animation` (skeletal + state) | 7.2 | 2–3 sessions |
| 7.4 | `engine::physics` (hitbox vs hurtbox) | 7.3 | 1–2 sessions |
| 7.5 | `engine::audio` (miniaudio) | 7.1 | 1 session |
| 7.7 | `engine::game::Fight` + battle logic | 7.2, 7.3, 7.4 | 4–6 sessions |
| 7.8 | `engine::game::SaveSystem` | 7.1 | 1–2 sessions |
| 7.9 | UI: CocoGUI loader | 7.2 | 2–3 sessions |
| 7.10 | `engine::network` (SmartFox2X client) | 7.1 | 3–5 sessions |
| 7.6 | `engine::game::AI` (NPC state machines) | 7.7 | 2–3 sessions |

**Milestone M1** (window opens, ESC closes) = after Stage 7.1.
**Milestone M2** (main menu visible) = after Stages 7.2, 7.5, 7.9.
**Milestone M3** (one playable fight) = after Stages 7.3, 7.4, 7.7.

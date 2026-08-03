# LIVE_BOOT_TRACE — Shadow Fight 2 launch→dojo chronological map (Frida USB)

**Date:** 2026-08-03 · **Device:** Redmi 6A (`684006127d29`, MIUI 10, Android 8.1, root, frida-server PID 24787)
**Game:** `com.nekki.shadowfight` v1.9.21 (versionCode 1000086), launcher `.Main` · **Screen:** landscape 1440×720
**Transport:** python frida 16.2.1 → USB adb → frida-server (root). Spawn + **log-only hooks**, no game-state mutation. UI driven via `adb input tap/swipe`, gated (never during load phase).

**Evidence files:** `reverse/frida_hooks/live_v7_boot.txt` (primary boot trace), `live_v7_dojo.txt`, `live_v7_net.txt`, `live_v7_notaps.txt` (phase runs), `live_boot_trace.{js,py}` (instrument), `reverse/data/boot_sequence.txt` (deduped open map, 901 entries), `reverse/data/live_shots/*.png` + `s*.png` (screenshots), `ui_dump.xml`. Fresh tombstone `tombstone_03` (SIGSEGV, see §5).

---

## 1. Instrument (log-only)

| Hook | Captures |
|---|---|
| `libc.so!open/openat/fopen/fopen64` | physical FS opens (unpacked `files/assets/*`, shared_prefs, statistics) |
| `libs3e_android.so!s3eFileOpen` | **S3E VFS opens — logical pack paths** (`assets/...`) |
| `libs3e_android.so!s3eVideoPlay/IsPlaying/Stop` | intro cinematic lifecycle |
| `libs3e_android.so!s3eAudioPlay/Stop` | music/sfx file requests |
| `libs3e_android.so!s3eSoundChannelPlay` | channel plays |
| `libs3e_android.so!s3eDebugPrint` | game trace (none emitted on this build) |
| Java `Activity.onResume`, `Dialog.show`, `AlertDialog` | activity transitions + native dialogs |

Frida quirks solved on this device: hooks attached **before resume never fire** (deferred attach at +3.5 s); `readUtf8String(N)` throws on short strings (`readCString` used).

---

## 2. Chronological boot map (JS-time since script load)

Device ships **unpacked** assets (`files/assets/`); `files.dz` exists only as a descriptor in `packs.xml` (`<Pack Name="files" Url="assets/files.dz" Version="1.9.21"/>`), no `.dz` on device — VFS resolves to the unpacked tree.

| t (s) | Event | Evidence |
|---|---|---|
| 0.65 | `Activity.onResume: com.nekki.shadowfight.Main` | Java hook |
| ~1–4 | *(pre-attach; prev session)* `packs.xml` → `users.xml`(+hash) → `users_backup.xml`(+hash) → `internalSettings.xml` → `localization.xml` | `live_v5_full.txt`, identical ×3 runs |
| 4.34 | `assets/localization.xml` (VFS+FILE); `assets/video/intro.mp4` VFS open | boot run |
| 4.34→11.10 | **intro cinematic plays** (`s3eVideoPlay(intro.mp4)`, poll `IsPlaying`, `Stop` @11.10) — **auto-ends, no tap needed** | VIDEO hooks |
| 11.17 | `fullscreen/startLoading.xml/.jpg` — loading screen | VFS |
| 11.26–11.29 | `internalSettings.xml`, `tacticSettings.xml`, `perks.xml` | VFS+FILE |
| 12.02–12.55 | `forge.xml`, `CharacterProgress.xml`, `Achievements.xml` | VFS+FILE |
| 12.56 | **`assets/animations/moves.xml`** | VFS+FILE |
| 12.67–15.0 | **full animation catalog: ~450 `animations/binary/*.bin`** — every weapon `*_idle/transition/stance`, all `*_slash/*_split`, blocks, hits, steps, rolls, flips, jumps, kicks, punches, magic/ranged (`fireball_*`, `magic_*`, `ranged_*`, `shop_*`), `stance_1/2.bin` | VFS (pre-opened at boot) |
| 15.37–15.45 | scrolls: `Roll_center/left/right.png`, `Roll_MAP.jpg`, `Paper_left/right.png`, `MenuRoll_*` | VFS |
| 15.46–15.84 | **save read**: `statistics/logs_meta.json`, `paylogs_meta.json`, `users_backup.xml`(+hash), `localSettings.bin`(+hash), **`users.xml`**(+hash), `tacticSettings_result.xml`, `ComputerSettings.xml`, **`list.xml`** | VFS+FILE |
| 16.79–17.67 | `users.xml` (re-read), **`stages.xml`**, `raid_stages_default.xml`, `quests.xml` | VFS+FILE |
| 18.36–18.38 | `packs.xml` (re-read), **`config_cdn.xml`**(+hash) — CDN config check | VFS+FILE |
| 18.4–18.6 | `statistics/sessions.json`, `crashLog.json`, `purchased.xml` (VFS ×2), prefs | FILE |
| 18.94–19.0 | `sounds/armor.wav` (first sound), `s3eSoundChannelPlay ch=0`, `localizations/rus.xml`, `databases/notif.db` | VFS+FILE+SOUND |
| 19.06–19.39 | dojo/menu atlases: `buttons/dojo/batchButtonsDojo.*`, `buttons/menu/screens/batchButtonsMenuScreens.*`, `panels/top/batchPanelsTop.*`, `misc/notification_{circle,ellipse}.png`, `buttons/pieces/batchButtonsPieces.*`, `misc/forge_{green,red,purple}.png` | VFS |
| 19.17–19.29 | fonts: `fonts/rus/optima.{fnt,png}`, `fonts/eng/sakkal.{fnt,png}` | VFS |
| 19.40–19.48 | `fullscreen/startLoading.xml`(re), `loading.xml/.jpg`, `logo.png` | VFS |
| 19.49–19.60 | `files/INSTALLATION`, `files/adc/data/{iap_cache,media_info,tracking_info,manifest,zone_state}.txt` — analytics | FILE |
| 19.94–20.16 | `quest_extensions/raid_quests.xml`, `quests_cdn.xml` (VFS ×2 — CDN fetch) | VFS |
| 20.31–20.61 | **dojo location boot**: `locations/dojo/params.xml`, `bg.plist/png`, `atlas_layer1..3.*`, `misc/shapes.*`, `fight/pointers/arrow.png`, `effects/fight/hit_blade.*`, **`joystick/batchJoystick.*`** (move), **`buttons/fight/batchButtonsFight.*`**, `magic_progress/magic_full/ranged_full.png`, `btn_charge_normal.png`, `Charge_Highlight.png` | VFS |
| 20.62–20.63 | **fighter models**: `models/skeleton.xml`, `body.xml`, `head.xml` | VFS+FILE |
| 20.66–20.91 | **sound catalog (~30 wavs)**: `swish*.wav`, `wall3.wav`, `m_/f_cough.wav`, `bodyfall*.wav`, `m_/f_pl_jump*.wav`, `m_/f_pl_attack*.wav`, `m_pl_hit2.wav`, `swish_sword1.wav` | VFS |
| 20.93 | **punching-bag models**: `skeleton_punching_bag.xml`, `punching_bag.xml` | VFS+FILE |
| 20.94–21.07 | tactics binaries: `tactics/fists.atf`, `Fists_Fists.atf` (+`_Fists.atf`, `_.atf` in same buffer) | VFS |
| 21.13–21.14 | `fight/labels/batchFightLabels.*` (HUD) | VFS |
| 21.17–21.19 | **`assets/music/menu.mp3`** → `s3eVideoStop`, cache `s3eaudio.mp3` — **dojo music starts = dojo stable** | VFS+FILE |
| 21.22–21.36 | dojo UI re-load: `batchButtonsDojo.plist`, `batchButtonsPieces.plist`, `fight/popups/result/Background_*.{jpg,png}`, `MW_stripe.png`, `MW_Bottomstripe.png`, `image/users/image/character_sensei.png` | VFS |
| 21.38–21.41 | save flush: `users.xml`(+hash), logs/paylogs_meta, sessions.json, `localSettings.bin`(+hash), `users_backup.xml`(+hash) | VFS+FILE |
| 29.35+ | only `shared_prefs/com.applovin.sdk.1.xml` spam — **logic loop frozen; render loop still 61 fps** | FILE |

**Deviations from the user's described flow:**
1. **Intro cinematic**: confirmed (4.3→11.1 s), **auto-ends**; the skip tap is not required — a tap *during load* is what kills the game.
2. **Loading screen**: `startLoading` → `loading`, as described.
3. **Update dialog**: **never a native Android dialog** (no `Dialog.show`/`AlertDialog` hits; uiautomator empty — UI is in-engine GL). With network OFF the game skips to the dojo and freezes there; with network ON it freezes at ~4.6 s during the CDN check. An in-engine dialog with two buttons (≈(505,505), ≈(942,505)) appeared post-freeze but the logic loop never reads touch → undismissable by injection.
4. **Dojo**: reached (all dojo assets + `menu.mp3`), but no input is processed — logic thread dies ~21–30 s in (known `base+0x61dc` bug, reproduced without hooks). Dojo interaction, menu tabs, battle: **unreachable in this state**.

---

## 3. Per-phase evidence summary

| Phase | Status | Key evidence |
|---|---|---|
| Boot | ✅ full | §2 chronology; identical start across runs; intro auto-ends |
| Dojo (reach) | ✅ | `locations/dojo/*`, `batchJoystick`, `batchButtonsFight`, `skeleton/body/head.xml`, punching bag, `menu.mp3` @20.3–21.4 |
| Dojo (interact) | ❌ game-blocked | logic loop frozen; `s3ePointerGetTouch*`=0 calls while `s3ePointerUpdate` 60/s; render 61 fps; tombstone null-deref `0x258` |
| Update dialog | ⚠️ seen, undismissable | in-engine; buttons located; taps not read |
| Menu tabs | ❌ unreachable | game frozen before input |
| Battle | ❌ unreachable | same |

---

## 4. RE engine vs original — boot order comparison

RE order (`engine/game/game.cpp` `on_init`→`init_location`, `asset_manager.cpp`): AssetManager/renderer → loading screen → audio → scene registration → **`list.xml`** → quest callbacks → **`host_load_progress()` (`save.json`)** → `check_tutorial()` (`usersDefault.xml`) → Boot→Loading→MainMenu → `init_location`: **open_archive(`*.dz` dir scan)** → **`stages.xml`** → fallback dirs → `load_location(dojo)` → `load_skeleton` → `load_body_model` → `load_equipment_models` → `load_enemy_fighter_models` → `load_punching_bag_model` → `load_animations` (dir scan of `binary/*.bin`) → `load_moves` (**`moves.xml`**) → `load_internal_settings` → `load_tactics` (**`tacticSettings.xml`**) → enemy/player weapon.

| # | Aspect | Original (live) | RE engine | Sev |
|---|---|---|---|---|
| 1 | **Save format** | `users.xml`+hash+`users_backup.xml`(+hash)+`localSettings.bin`(+hash); hash-verified, backup fallback | `save.json` JSON in `%APPDATA%`; no hash/backup | **HIGH** |
| 2 | **Pack discovery** | reads **`packs.xml`** (Name/Url/Version) | dir scan for `*.dz`; **never reads packs.xml**; device has no `.dz` (all unpacked) | **HIGH** |
| 3 | **Order** | `moves.xml` 12.56 → save 15.82 → `list.xml` 15.84 → `stages.xml` 16.8 → `quests.xml` 17.67 → `packs.xml` 18.36 → `config_cdn.xml` 18.37 | `list.xml` in on_init (before save); stages/moves/tactics in init_location; quests/packs/config_cdn never read | MEDIUM |
| 4 | **Intro cinematic** | `s3eVideoPlay(intro.mp4)` 4.3 s, auto-stop 11.1 s | none | MEDIUM |
| 5 | **Boot config set** | `forge.xml`, `CharacterProgress.xml`, `Achievements.xml`, `perks.xml`, `tacticSettings_result.xml`, `ComputerSettings.xml`, `raid_stages_default.xml`, `quests.xml`, `quests_cdn.xml`, `config_cdn.xml`, `purchased.xml` | only list/stages/moves/tacticSettings/internalSettings/loading | MEDIUM |
| 6 | **Animation catalog** | pre-opens ~450 `binary/*.bin` at boot | `load_animations` dir-scans same — **matches** ✅ | OK |
| 7 | **Dojo sounds** | pre-loads ~30 wavs at dojo | no boot-time sound catalog | MEDIUM |
| 8 | **Music** | `assets/music/menu.mp3` at dojo | `host_start_menu_music` — same file expected | OK |
| 9 | **Fighter models** | `skeleton.xml`, `body.xml`, `head.xml` | `load_skeleton`/`load_body_model` — same | OK |
| 10 | **Punching bag** | `skeleton_punching_bag.xml`+`punching_bag.xml` | `load_punching_bag_model` — same | OK |
| 11 | **Tactics** | `tactics/*.atf` binaries at dojo | `load_tactics` parses `tacticSettings.xml` XML (no `.atf` loader) | MEDIUM |
| 12 | **Dojo UI atlases** | `batchButtonsDojo/Fight`, `batchJoystick`, `batchPanelsTop`, `batchFightLabels`, `optima`/`sakkal` fonts, `Roll_*`/`MenuRoll_*`, result popup, `character_sensei.png` | RE HUD differs; atlas/plist set per-screen | MEDIUM |
| 13 | **Localization** | `localization.xml` first (4.34), then `localizations/rus.xml` (19.0, device lang=rus) | `load_localization(lang)` in init_location, `eng` default + path normalization | LOW-MED |
| 14 | **Analytics** | `statistics/*.json`, `files/adc/data/*.txt`, `INSTALLATION`, `purchased.xml`, applovin/fiverocks prefs | none | LOW |
| 15 | **Update/CDN check** | `config_cdn.xml`+hash, `quests_cdn.xml`, `packs.xml` re-read; **freezes with network ON (~4.6 s)**, proceeds with OFF | no CDN layer; no stall | LOW (offline RE) |

---

## 5. Crash wall (blocks dojo/menu/battle phases)

- Fresh tombstone `tombstone_03` (pid 28979, `Thread-2`): `signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x258, Cause: null pointer dereference`, `pc 0x8ef041dc` — same signature as previous session's `base+0x61dc`.
- Not our hooks: (a) reproduced without frida previously; (b) freeze = "logic loop stops reading input" (pointer getters 0 calls) while `s3ePointerUpdate`/render loop continue — dead worker thread; (c) identical across 4 spawns with different tap policies.
- Consequence: dojo interaction / menu tabs / battle traces are **not capturable** on this device/firmware until the crash is understood. Next step: hook the crash site with a return-address trace to identify the null object (candidate: update-check callback or ad-SDK init), or run with the update check disabled (network OFF gives longest survival: 60 s+).

---

## 6. Artifacts (this session)

- `reverse/frida_hooks/live_boot_trace.js` — log-only instrument
- `reverse/frida_hooks/live_boot_trace.py` — phase driver + pixel-classified screenshots
- `reverse/frida_hooks/live_v7_boot.txt` — full boot trace (primary)
- `reverse/frida_hooks/live_v7_dojo.txt`, `live_v7_net.txt`, `live_v7_notaps.txt` — phase runs
- `reverse/frida_hooks/probe_exports*.py`, `probe_s3e.py`, `test_pump*.py`, `attach_pointer_*.py`, `diag_block.py` — diagnostics
- `reverse/data/boot_sequence.txt` — deduped chronological open map
- `reverse/data/live_shots/*.png`, `reverse/data/s*.png` — screenshots
- `reverse/data/ui_dump.xml`, `live_t*.png` — UI/state investigation

# LIVE_INTERACTION_TRACE — SF2 dojo/menu/battle live capture vs RE engine (Frida USB)

**Date:** 2026-08-04 · **Device:** Redmi 6A (`684006127d29`, MIUI 10, Android 8.1, root, frida-server)
**Game:** `com.nekki.shadowfight` v1.9.21 (versionCode 1000086) · **Screen:** landscape 1440×720
**Transport:** python frida 16.2.1 → USB adb → frida-server (root). Log-only hooks; **no game-state mutation**.
**Input delivery:** `adb input` **does NOT reach the game window** on this device; kernel-level `sendevent` on `/dev/input/event2` (`mtk-tpd`) DOES. Verified conversion: display(x,y) → raw(y, 1439−x).
**Update dialog:** native `AlertDialog` ("Доступна новая версия. Обновить сейчас?" / "Обновить" / "Отмена") — **dismissed via sendevent** on "Отмена" (adb input could not).

Evidence: `reverse/frida_hooks/live_v8_interact.js` (instrument), `live_v8_interact.py`, `live_v9_dojo.txt`, `live_v10_dojo.txt`, `live_v11_dojo.txt`, `live_v12_dojo.txt`, `live_v12_net.js`, `live_v12_retry.py`, `reverse/data/dojo_boot_v12_reconstructed.txt`, `reverse/data/live_shots_v8/v12_*.png` (menu/dojo pixel-classified screenshots).

---

## 0. Instrument (log-only)

| Hook | Captures |
|---|---|
| `libc.so!open/openat/fopen/fopen64` | physical FS opens |
| `libs3e_android.so!s3eFileOpen` | VFS logical paths (`assets/...`) |
| `s3eVideoPlay/IsPlaying/Stop` | cinematic lifecycle |
| `s3eAudioPlay/PlayFromBuffer/Stop` | music / audio buffer |
| `s3eSoundChannelPlay/Stop/Pause/Resume/GetFreeChannel` + Sound\*AllChannels | SFX lifecycle (channel id) |
| `s3eDebugPrint` | game trace (none emitted on this build) |
| `s3ePointerUpdate` (counter) | render-loop heartbeat (~61/s = alive) |
| `s3ePointerGetTouchState/GetTouchX/GetTouchY/GetState/GetX/GetY` (counter+log) | **input actually read by the game** |
| `s3ePointerRegister/UnRegister` | callback input path |
| `libc.so!connect/getaddrinfo/recv/recvfrom` | network endpoint diagnostics |
| Java `Activity.dispatchTouchEvent`, `Activity.onResume`, `Dialog.show`, `AlertDialog.setMessage` | touch delivery at the window + native dialogs |

Device quirks re-used from the previous session: hooks attach at +3.5 s (pre-resume hooks never fire); `readCString` instead of `readUtf8String`.

---

## 1. Input-read status (measured, not guessed)

**The game never reads touch input on this build/device.** In every run (6 spawns + 2 live attaches, total ~8 process lifespans):

- `touchReads = 0` in **every** 1 Hz heartbeat of **every** process, for its entire lifetime (up to 90 s observed). Not a single call to `s3ePointerGetTouchState/GetTouchX/GetTouchY/GetState/GetX/GetY` — including during the intro, loading, menu and dojo phases.
- `s3ePointerRegister` was never observed after attach (+3.5 s; registration may happen before attach, but then the callback would still be driven by `s3ePointerUpdate`, and the game would react — it does not).
- `s3ePointerUpdate` keeps firing at 57–63/s → the Marmalade **render loop is alive**; the game-logic side that would consume pointer state is not running.
- A kernel-delivered tap (sendevent) reaches the window (`Activity.dispatchTouchEvent` fires when a dialog is up), but produces **zero** VFS/SOUND/VIDEO/pointer-getter reaction.
- The known SIGSEGV wall (tombstone, null-deref @0x258, pc = base+0x61dc, ~21–30 s) is the *crash variant* of the same phenomenon: the logic loop dies/freezes around that time; with the update dialog present it can freeze earlier.

**Consequence:** dojo *actions* (movement/punch/kick/jump/roll/weapon-switch), *menu tabs* and *battle* cannot be exercised live — the game never consumes the injected input. Everything below is what was captured around that wall, with the exact failure mode documented.

---

## 2. Per-phase capture

### 2.1 Boot / dojo reach (captured, 2 full runs)

Run A (spawn 2 of v9, pid 18755) and Run B (attempt 2 of v12, pid 24889 — see `reverse/data/dojo_boot_v12_reconstructed.txt`). Dojo-stable marker: VFS `assets/music/menu.mp3` (Run A @29.27 s, Run B @32.29 s).

Run B chronology (JS-time since script load):

| t (s) | Event |
|---|---|
| 4.38 | `intro.mp4` PLAY/STOP (instant stop — network-degraded boot) |
| 9.94–9.96 | `fullscreen/startLoading.xml/.jpg` |
| 14.55 | `packs.xml` → boot config phase (users/list/stages/quests/config_cdn sequence, same as Run A / v7 boot map) |
| 17.84–17.85 | `sounds/armor.wav` → `s3eSoundChannelPlay ch=0` (first sound) |
| 17.99–18.00 | `buttons/dojo/batchButtonsDojo.{plist,png}` |
| 21.62 | **`AlertDialog.show` — update window** ("Доступна новая версия…") — loading continues behind it |
| 30.98–31.11 | **dojo location**: `locations/dojo/params.xml`, `768/locations/dojo/bg.{plist,png}`, `atlas_layer1..3.{plist,png}` |
| 31.43–31.93 | **dojo sound catalog, exact order (30 files)**: `swish6, swish7, swish5, wall3, m_cough, f_cough, bodyfall3, bodyfall1, m_pl_jump2, f_pl_jump2, m_pl_jump1, f_pl_jump1, m_pl_jump3, f_pl_jump3, m_pl_attack1, f_pl_attack1, m_pl_attack4, f_pl_attack4, m_pl_attack5, f_pl_attack5, m_pl_attack3, f_pl_attack3, swish4, swish3, m_pl_hit2, swish2, m_pl_attack2, f_pl_attack2, swish_sword1` |
| 32.29 | `music/menu.mp3` — **dojo stable** |

Run A additionally captured (before menu.mp3 @29.27): `joystick/batchJoystick.png`, `buttons/fight/batchButtonsFight.{plist,png}`, `magic_progress/magic_full/ranged_full.png`, `btn_charge_normal.png`, `Charge_Highlight.png`, `models/skeleton.xml`, `models/body.xml`, `models/head.xml`, `models/skeleton_punching_bag.xml`, `models/punching_bag.xml`, `tactics/fists.atf`, `Fists_Fists.atf`, `_Fists.atf`, `_.atf`, `fight/labels/batchFightLabels.{plist,png}`, then dojo-UI reload (`batchButtonsDojo.plist`, result popup `Background_*`, `character_sensei.png`, `MW_stripe.png`), then save flush (`users.xml`+hash, `localSettings.bin`+hash, `logs_meta.json`, `paylogs_meta.json`, `sessions.json`, `users_backup.xml`+hash).

**Screen states (pixel-classified screenshots, `live_shots_v8/v12_*.png`):**
- menu screen: `warm/panel`, avg≈(159,142,121), bright≈0.57 — beige main-menu
- dojo: `dark`, avg≈(100,78,52), sat≈0.36 — dark warm interior
- The menu → dojo transition happens **without any input** (auto-transition, ~3–12 s), verified in v7_dojo (transition before first tap) and v12 run B.

**Dojo input attempts (v12 run B, sendevent, 7/7 injected):** `move_LEFT(260,620)`, `move_RIGHT(420,620)`, `PUNCH(1180,540)`, `KICK(1000,540)`, `JUMP(1180,340)`, `ROLL(1000,340)`, `weapon_switch(700,600)` → **zero reaction** (no VFS, no SOUND, touchReads=0). Taps landed on the menu screen (window opened right at menu.mp3); the game then auto-transitioned to the dojo.

### 2.2 Menu tabs (not capturable — input dead)

The menu screen is reachable (beige, avg≈(159,142,121)) and the game auto-advances to the dojo, but no tab (Map/Shop/Settings/Profile) can be opened: every injected tap is ignored. Per-tab file-access map (list.xml/shop atlases/settings atlases/profile data) is **unreachable on this build/device**; the engine's scene model (Boot/Loading/MainMenu/Map/Shop/Settings/Profile — `game.cpp:449-460`) is the reference for what such a capture would show.

### 2.3 Battle (not capturable — same wall)

Entering a fight requires menu → Map → node → FIGHT, all of which need live input. `locations/battle/*`, fighter models for both sides, weapon files, HUD atlases, hit-feedback traces could not be reached. What *is* pinned from reachable phases: the dojo's hit-related assets (`effects/fight/hit_blade.png`, `m_pl_hit2.wav`, `wall3.wav`, `bodyfall*.wav`) are preloaded at dojo entry — i.e. the original preloads exactly the assets the RE engine's combat never loads (see §3).

---

## 3. RE-vs-original discrepancies (this mission's scope)

| # | Aspect | Original (live) | RE engine | Sev |
|---|---|---|---|---|
| 1 | **Input model** | touch joystick + on-screen buttons; SFX layout: move (260/420, 620), punch (1180,540), kick (1000,540), jump (1180,340), roll (1000,340) | keyboard W/A/S/D/arrows + O/P (+Space/K fallback), double-tap 300 ms for DoubleStep/BackHandflip (`input_handler.cpp:49-78`) | LOW (porting target, known) |
| 2 | **SFX catalog at dojo** | preloads 30 wavs incl. **`m_pl_hit2.wav` (hit), `wall3.wav` (wall impact), `bodyfall1/3.wav`, `m_/f_cough.wav`, `swish2..7.wav`, `swish_sword1.wav`, `m_/f_pl_jump1-3`, `m_/f_pl_attack1-5`, `armor.wav`** | `combat.cpp` only ever plays `"f_pl_attack"+N` and `"armor"` — **no m_ variant for the player, no hit/wall/fall/cough/swish sounds at all** | **HIGH** |
| 3 | **Hit feedback chain** | hit = (weapon swish) + `m_pl_hit2.wav` + `hit_blade.png` effect + `armor.wav` (all preloaded at dojo) | hit = `f_pl_attackN` + `armor`, flash + stun timers (`combat.cpp:18-25, 106-107`); no `m_pl_hit2`, no `hit_blade` effect wiring | **HIGH** |
| 4 | **Gender voices** | both `m_` and `f_` voice sets loaded (player is male, enemy female etc.) | only `f_pl_attackN` used everywhere | MED |
| 5 | **Update dialog** | native `AlertDialog` "Обновить/Отмена" on network ON; blocks the flow until dismissed (dismissible via kernel-level tap) | no update layer | LOW (offline RE) |
| 6 | **Menu → dojo** | auto-transition to dojo without input (~3–12 s after menu.mp3) | MainMenu stays until input (`game.cpp` scene flow) | MED (needs verification against a working device) |
| 7 | **Tactics** | `tactics/*.atf` binaries (fists.atf, Fists_Fists.atf, _Fists.atf, _.atf) at dojo | `load_tactics` parses `tacticSettings.xml` XML | MED (already in boot trace) |
| 8 | **Music start** | `assets/music/menu.mp3` played via `s3eAudioPlayFromBuffer` (len 1310720) exactly at dojo stable | `host_start_menu_music()` → `menu.mp3` (`game.cpp:1356-1365`) — same file ✅ | OK |
| 9 | **Fighter/punching-bag models** | `skeleton.xml`/`body.xml`/`head.xml`/`skeleton_punching_bag.xml`/`punching_bag.xml` | `load_skeleton`/`load_body_model`/`load_punching_bag_model` — same set ✅ | OK |

---

## 4. Fidelity pins (test assertions for the engine)

Concrete values/sequences the original produces that the engine tests should pin:

1. **Dojo-ready music gate**: `menu.mp3` is opened/played **only after** the last dojo asset (`batchButtonsDojo.plist` reload); the engine's dojo-load test must assert `host_start_menu_music` is invoked after `init_location` completes, not before.
2. **Dojo sound catalog (exact 30-name order)** — assert `AssetManager`/audio layer registers each of: `swish6, swish7, swish5, wall3, m_cough, f_cough, bodyfall3, bodyfall1, m_pl_jump2, f_pl_jump2, m_pl_jump1, f_pl_jump1, m_pl_jump3, f_pl_jump3, m_pl_attack1, f_pl_attack1, m_pl_attack4, f_pl_attack4, m_pl_attack5, f_pl_attack5, m_pl_attack3, f_pl_attack3, swish4, swish3, m_pl_hit2, swish2, m_pl_attack2, f_pl_attack2, swish_sword1, armor` (order = load order in §2.1 Run B).
3. **Hit sound = `m_pl_hit2`**: on a landed hit the original plays the *hit* sample `m_pl_hit2.wav` (preloaded), i.e. the combat test must assert `play_sound("m_pl_hit2")` on impact — the engine currently does not have this sound.
4. **Wall impact = `wall3`**: hitting the stage wall plays `wall3.wav` — engine pin for knockback/block vs wall.
5. **Fall = `bodyfall1`/`bodyfall3`**: enemy KO uses `bodyfallN.wav`; player KO also.
6. **Gender-correct voices**: male fighter → `m_pl_attack1..5`; female fighter → `f_pl_attack1..5`. Engine currently plays `f_pl_attackN` unconditionally (`combat.cpp:106,235,245`).
7. **Weapon swish set**: melee swings map to `swish2..swish7` + `swish_sword1` (sword-specific); assert a sword attack plays `swish_sword1`.
8. **First UI sound at dojo = `armor.wav` on channel 0** (observed `s3eSoundChannelPlay ch=0` right after armor.wav load @ dojo entry) — pin for the dojo-entry UI sound.
9. **Hit visual = `effects/fight/hit_blade.png`** — preloaded with the dojo; the hit-effect test should reference this atlas name.
10. **Control layout** (physical coordinates on 1440×720 landscape): move-left (260,620), move-right (420,620), punch (1180,540), kick (1000,540), jump (1180,340), roll (1000,340) — pins for the input-mapping test on touch devices.
11. **Double-tap moves** (`moves.xml`): `BackHandflip` Base=500 AntiLimit=500 Limit=1 DistanceFactor=0.0025 Shift=−0.25 (observed in boot dump of moves.xml) — engine's `kDoubleTapWindowMs=300` gate should be tested against these move params.
12. **Update-dialog behavior**: with network ON the original shows a native AlertDialog before/at dojo; decline must not kill the session (engine has no network layer — pin only for the host/emulator harness).

---

## 5. Crash / input wall — root-cause evidence captured today

1. **Boot stall is random** (~50–80% of spawns): the game stops opening files right after the intro (~4.5 s) while the render loop keeps running 61 fps. Not caused by hooks (reproduced without; identical across 8 lifespans with different policies).
2. **Network state**: stalled runs show all game TCP sockets to Nekki's Azure/CDN hosts (`4.115.x`, `4.116.x`, `142.98.x:443`) stuck in **CLOSE_WAIT** — legacy update/CDN endpoints are degraded; the boot config phase (`packs.xml` … `config_cdn.xml`) waits on a network step. `assets.nekkimobile.ru` (Akamai) resolves and pings fine (25 ms); HTTPS/CLOSE_WAIT handling is what stalls.
3. **Update dialog** (network ON, ~50% of runs that pass the intro): native `AlertDialog` "Доступна новая версия. Обновить сейчас?" with "Обновить"/"Отмена" (uiautomator-visible). `adb input tap` never reaches it; **sendevent tap on "Отмена" (941,491) dismisses it** — but the logic loop is already dead by then, so loading does not resume (observed: black screen afterwards, render loop only).
4. **Input path dead end**: kernel-delivered touches reach the window (dispatchTouchEvent fires) but the game never calls any s3e pointer getter and never registered a callback post-attach → the logic thread that consumes input is dead/frozen on this build+firmware combination. This is the documented blocker for menu tabs and battle (same as previous session's `base+0x61dc` wall; that pc was a crash variant).
5. **Legacy-server degradation note**: yesterday 2/4 runs booted fully; today 1/7 — the stall probability is worsening as the servers degrade, which is the practical reason dojo interaction is getting harder to capture.

---

## 6. Artifacts (this session)

- `reverse/frida_hooks/live_v8_interact.js` — v8 instrument (pointer/audio/net hooks)
- `reverse/frida_hooks/live_v12_net.js` — v12 instrument (+libc connect/getaddrinfo/recv)
- `reverse/frida_hooks/live_v8_interact.py` — v9 driver (spawn, gated taps, decline scan)
- `reverse/frida_hooks/live_v10_attach.py` — live-attach driver (sendevent input)
- `reverse/frida_hooks/live_v11_spawn.py` — spawn + sendevent + decline driver
- `reverse/frida_hooks/live_v12_retry.py` — retry loop driver (stall detection, 25 s patience)
- `reverse/frida_hooks/live_v9_dojo.txt`, `live_v10_dojo.txt`, `live_v11_dojo.txt`, `live_v12_dojo.txt` — run traces
- `reverse/data/dojo_boot_v12_reconstructed.txt` — reconstructed successful dojo run (pid 24889)
- `reverse/data/live_shots_v8/v12_*.png` — pixel-classified screenshots (menu vs dojo states)
- **Not captured** (documented blocker): per-action dojo reactions, menu tabs, battle boot/combat.

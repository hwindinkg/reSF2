# LIVE_GAME_EVIDENCE — Shadow Fight 2 runtime capture

**Date:** 2026-08-02
**Device:** Redmi 6A (adb id `684006127d29`, MIUI 10, Android 8.1, root — frida-server PID 24787)
**Game:** `com.nekki.shadowfight` v2.46.0 (Shadow Fight 2), data dir `/data/user/0/com.nekki.shadowfight/files/assets/`
**Transport:** python frida 16.2.1 (host) → USB adb → frida-server (device). Game spawned under frida with log-only hooks.
**Evidence files:** `reverse/data/` (80 files pulled from device: all 71 model XMLs, moves.xml + xsd, users.xml, list.xml, stages.xml, packs.xml), `reverse/frida_hooks/live_capture.{js,py}`.

---

## 0. Route diagnostics (what worked, what didn't)

| Route | Result |
|---|---|
| MCP `frida_get_device("socket")` | OK — device handle resolves, but it is frida's generic localhost:27042 placeholder, NOT a live Android connection |
| MCP `frida_list_processes` / `enumerate_processes` | **Local-only** — returns the Windows process list even after selecting the socket device |
| MCP `frida_get_process_by_name` | Local-only — `zygote`/`system_server`/`com.nekki.shadowfight` all "not found" |
| MCP `frida_attach_to_process` / `create_interactive_session` | Local-only — attach to game PID 17349 → `unable to find process with pid 17349` |
| `netstat` on host | nothing on 27042 until `adb forward tcp:27042 tcp:27042` is run; even then MCP tools stay local |
| **python frida via USB adb** | **WORKS** — `frida.get_device("684006127d29")`, spawn/attach/resume, log-only hooks, full capture |

**MCP conclusion:** the frida MCP server (`frida-mcp`, no device env in `opencode.jsonc`) is hardwired to the LOCAL device for all tools except those taking an explicit `device_id` (spawn/kill). The `socket` device is never used by attach/process tools. All live evidence below was captured via the python frida route.

**Game stability:** the game **crashes ~28–40 s after launch regardless of frida or hooks** (proved by an unhooked run: new tombstones at 19:10/19:12). Crash: SIGSEGV null-deref at `0x258`, `pc = game_base + 0x61dc` (`0x8da061dc`/`0x8db001dc` in two runs). Tombstones: `/data/tombstones/tombstone_05..08`. Flaky (one 60 s run survived). Injected `input tap` events during the load phase reliably trigger it. → **no combat-time capture; navigation avoided to not kill the game.**

**Game base / ASLR:** prior session's `0x8f35f000` is gone (zeros). Live probes: code mapped at `0x8da00000` (`78 37 95 e5 ...` = ARM literal-pool loads) and `0x8db00000`. Crash PC `0x8da061dc` ⇒ base ≈ `0x8da00000`, offset `0x61dc`.

---

## Q1 — Weapon model name: `weapon_knives.xml` (plural) — SETTLED

**A. The file exists on the device** (`models/` listing, 144 entries, includes):
```
weapon_knives.xml        weapon_knives.xml.hash
weapon_daggers.xml       weapon_batons.xml      weapon_knuckles.xml
weapon_kunai.xml         weapon_machete.xml     weapon_ninja_sword.xml
weapon_nunchaku.xml      weapon_sai.xml         weapon_swords.xml
weapon_claws.xml         weapon_triangle_knives.xml  ...
```
There is **no** `weapon_knive.xml`.

**B. Item→model mapping in the real `list.xml`** (device):
```xml
<Item Name="WEAPON_KNIVES" Image="weapon_knives" Model="weapon_knives" Type="Weapon" SubType="Knives" WeaponDamage="5" Level="1" Price="50" UpgradeLevel="100">
  <Upgrades Template="Weapon_Bonus"/>
</Item>
```
Model attribute is **lowercase item id**, and the file name matches `Model + ".xml"`.

**C. Live player save `users.xml`** — the item equipped on this device:
```
Weapon="WEAPON_KNIVES" Armor="ARMOR_ROBE" Helm="Head"
<Item Name="WEAPON_KNIVES" Equipped="1" Count="1" UpgradeLevel="100" .../>
<Item Name="ARMOR_ROBE"    Equipped="1" Count="1" UpgradeLevel="200" .../>
```

**D. Model structure (`weapon_knives.xml`, real file — Scene/Nodes/Edges/Figures):**
- Edges: `WEAPON_KNIVES-Edge17_1/2` (`Length=30`), `WEAPON_KNIVES-Edge18_1/2` (`Length=60`, `Radius=2`, `Margin1=0`, `Margin2=0.1`, **`Collisible="0"`** — attack anchors, not collision bodies)
- Figures: `WEAPON_KNIVES-Capsule-Edge17_1/2` (Capsule, Radius1=5 Radius2=5) + Triangles

---

## Q2 — Hit test: attacker's attack edges vs ENEMY fighter's model edges — SETTLED (data-authoritative)

**A. The attacker side comes from `moves.xml` `<AttackingParts>`, resolved against the ATTACKER's model.** Real `HighPunch` (unarmed):
```xml
<Interval Type="Attack" Start="4" End="5">
  <AttackingParts>
    <Edge Name="EForearm_2"/>
    <Edge Name="EHand_2"/>
    <Edge Name="EFingers_2"/>
  </AttackingParts>
  <Damage Value="0.11"><Damage Type="UnarmedDamage" Shift="-10"/></Damage>
  <Impulse X="245" Y="0" Z="0"/>
  <Hit Name="High"/>
</Interval>
```
→ **unarmed moves reference the fighter BODY's named edges** (`EForearm_2`, `EHand_2`, `EFingers_2`).

**B. Weapon moves reference the WEAPON MODEL's edges** — full distinct set found in `moves.xml` (excerpt):
```
WEAPON_KNIVES-Edge17_1  WEAPON_KNIVES-Edge18_1  WEAPON_BATONS-Edge15_1
WEAPON_CLAWS-Blade_1    WEAPON_SWORDS-Blade_1   KATAREDGE_1  KnobstickEdge1_1
WEAPON_SAI-Edge30_1     WEAPON_SCYTHE-Edge1     ONE_HANDED ... (60+ weapon edges)
```
Every one of these names exists in the corresponding `models/weapon_*.xml` (e.g. `WEAPON_KNIVES-Edge17_1` is in `weapon_knives.xml` — see Q1-D).

**C. The defender side = the ENEMY fighter's own model edges.** The fighter body model `body.xml` defines the target edge set (capsule figures `Capsule_EFootS_1 Edge="EFootS_1"` ...):
```
EChest  EArm_1/2  EForearm_1/2  EHand_1/2  EFingers_1/2
EThigh_1/2  ECalf_1/2  EFoot_1/2  EToe_1/2  EInstep_1/2  EHeel_2 ...
```
(`head.xml` adds `EHead`; enemy-specific bodies `body_lynx.xml`, `head_lynx.xml`, `skeleton_*.xml` exist for bosses.)

**D. The punching bag is only the TRAINING target** — `punching_bag.xml` exists with generic `Edge10/16/17...` capsules (Radius 2–25), and the save shows `Punchbag|Training`/`Punchbag|Bosses` fights; `skeleton_punching_bag.xml` is the training fight's enemy body. → In real fights the hit test runs attacker attack edges against the **enemy fighter's body/head model edges** (the `E*` set above), **not** a bag model.

---

## Q3 — moves.xml parse — SETTLED (real file + live open)

**A. Live runtime evidence:** the game opened `assets/animations/moves.xml` during startup (captured via libc `open` hook; same sequence across 3 runs: `packs.xml → users.xml → localization → tacticSettings/perks/forge → animations/moves.xml → list.xml → stages.xml → quests → config_cdn`). `moves.xsd` is present next to it.

**B. Full real `HighPunch` move (verbatim from device `animations/moves.xml`):**
```xml
<Move Name="HighPunch" Template="1key|Central|Unarmed|Punch" Type="ATTACK" FileName="high_punch.bin" MidFrames="2" FirstFrame="1" Priority="110" TacticWeapon="Fists">
  <Profile Show="1" Rank="1" Icon="high_punch"/>
  <Tactics>
    <Conditions>
      <Distance Min="0" Max="250" Axis="X">
        <From Player="Me" Object="Pivot"/>
        <To Player="Enemy" Object="Nodes" Part="NPivot"/>
      </Distance>
    </Conditions>
  </Tactics>
  <Align Axis="X|Z">
    <Pivot Object="Nodes" Part="NHeel_2"/>
    <Position Player="Me" Object="Pivot"/>
  </Align>
  <Conditions>
    <Keys>
      <Key Type="Punch" PressType="Tap"/>
    </Keys>
  </Conditions>
  <Locks>
    <Item Type="Weapon" SubType="Fists"/>
  </Locks>
  <Intervals>
<!--         <Interval Name="SemiUninterrupt" End="7"/> -->
    <Interval Name="Uninterrupt" End="9"/> <!-- Start="8" -->
    <Interval Type="Block" Start="10"/>
    <Interval Name="Throwable" Start="10"/>
    <Interval Type="Attack" Start="4" End="5">
      <AttackingParts>
        <Edge Name="EForearm_2"/>
        <Edge Name="EHand_2"/>
        <Edge Name="EFingers_2"/>
      </AttackingParts>
      <Damage Value="0.11">
        <Damage Type="UnarmedDamage" Shift="-10"/>
      </Damage>
      <Impulse X="245" Y="0" Z="0"/>
      <Hit Name="High"/>
    </Interval>
  </Intervals>
  <Actions>
    <Sound Name="m_pl_attack5" Frame="2" Voice="Male"/>
    <Sound Name="f_pl_attack5" Frame="2" Voice="Female"/>
    <Sound Name="swish7" Frame="3"/>
  </Actions>
</Move>
```
Parser-relevant facts: window = `Start=4 End=5` (frames), `MidFrames=2`, `FirstFrame=1`, key `Punch/Tap`, damage `0.11` with `UnarmedDamage Shift=-10`, impulse `X=245 Y=0 Z=0`, hit zone `High`.

---

## Q4 — Armor/helm model files — SETTLED

- Device `models/` contains `armor_robe.xml`, `armor_leather.xml`, `armor_green.xml`, `armor_quilted.xml`, `armor_kendo.xml`, ... (all `armor_*.xml`), `helm_kabuto.xml`, `helm_light.xml`, `helm_closed.xml`, `helm_soldier_kabuto.xml`, ... (all `helm_*.xml`), plus body/head: `body.xml`, `head.xml`, `body_woman.xml`, `head_night.xml`, etc.
- `users.xml`: `Armor="ARMOR_ROBE"` → file `armor_robe.xml`; `Helm="Head"` → file `head.xml` (bare item id, no `helm_` prefix when the item IS the head).
- `list.xml` items carry `Model="<lowercase>"` matching the files (same convention as weapons).

---

## What was NOT captured live and why

- **Combat-time hit-test trace / model load during equip/fight:** the game crashes ~30 s after launch (natural bug, `base+0x61dc` null-deref, reproduced without hooks; injected taps during load reliably trigger it), and the game's model files are served from the `files.dz` pack through the S3E virtual FS — no libc `open` for `models/*.xml`, and the model-name strings only materialize in memory when a model is actually requested (fight/equip screens). Blind navigation without screen vision is not viable. The Q2/Q1/Q4 answers above rest on the authoritative device data (real files + real save + real moves.xml), which is stronger than a partial hook trace.
- **In-memory game string-table dump:** delayed-init scans (all range protections, >2 MB) found no `weapon_`/`armor_`/`moves.xml` strings at any point — consistent with strings living inside the compressed pack until on-demand load.

## Artifacts

- `reverse/data/models/*.xml` — all 71 real model files from the device
- `reverse/data/animations/moves.xml` + `moves.xsd` — real moves data + schema
- `reverse/data/users.xml`, `list.xml`, `stages.xml`, `packs.xml` — real save/config (save language: `rus`)
- `reverse/frida_hooks/live_capture.js` / `live_capture.py` — log-only instrumentation used
- `reverse/frida_hooks/live_v6_full.txt` — raw capture log (file-open sequence, probes)

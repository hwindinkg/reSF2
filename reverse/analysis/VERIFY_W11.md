# Wave 11 SPEC verification report

Target: `reverse/binaries/game_region_runtime.bin` (base `0x8F057000`, ARM:LE:32:v7).
Specs: `SPEC_COMBAT_CORE.md`, `SPEC_WORLD_FEEL.md`, `SPEC_PRESENTATION.md`.
Method: batch decompile + disassembly of parse blocks + literal-pool resolution
(ldr [slot] / add pc,rN → target = (add_pc+8) + slot_value, confirmed against
known strings), string xrefs, moves.xml data cross-check, control-flow metrics.

---

## SUBJECT 1 — SPEC_COMBAT_CORE: **GREEN**

| Item | Result | Evidence |
|---|---|---|
| Q1 magic-charge formula `0x8F4A9660` | **PASS** | `charge = +0x6EC + f2·powf(2,f1)·f3·damage`, clamp `>1.0→1.0 / <0.0→const`, full-bar skip (`+0x6F0 != 0 → return`), tail call `FUN_8f4a80d8`. Role split: role 0 → `FUN_8f65f0f0` (PainRecharge), role 1 → `FUN_8f65f1e8` (DamageRecharge); factors `FUN_8f4a94f0` / `FUN_8f4a95a8`. |
| Q1 threshold `0x8F4A80D8` | **PASS** | `vcmpe 1.0` semantics: `+0x6EC >= 1.0` → `count = +0x6F0+1`, `>1` → `FUN_8f226a58` log, bar reset; `count>1 → +0x6F0=1`; `model+0x8D` (`*(fighter+500)+0x8D`) gate; `FUN_8f2578c8(f, 0xC, {0x10, charge|1.0f, -1, -1})`. |
| Q1 call site `0x8F420F9C` | **PASS** | `FUN_8f47d378(move@hit+0x19C)` gate wraps the block; damage read at `interval+0x48`; **two** `FUN_8f4a9660` calls — `(attacker, dmg, victim, +0x1C2, +0x1C3, 0)` then `(victim, dmg, attacker, +0x1C2, +0x1C3, 1)`; `MagicCharged` event `FUN_8f6aa85c(+0x1C0, f, 8, 1, 0)` at both sites on 0→1 transition. **[UNCERTAIN] +0x1C2=blocked/+0x1C3=critical attribution RESOLVED** — identical flags to both calls; +0x1C2 additionally gates the crit roll in `0x8F4AA998`. |
| Q1 gate `0x8F47D378` | **PASS** | `return *(byte*)(param_1+0x148)` — MoveDef+0x148 exactly. |
| Q1 round start `0x8F41C8E4` | **PASS** | `fight+0x6E0` (id 1) / `fight+0x6E4` (id 2); `+0x6F0=0`; `FUN_8f65eff8` (InitialCharge, 13-char name match, attrs map `model+0x1C4`, error-log→0.0) clamped `[0,1]` → `+0x6EC`; `FUN_8f4a80d8`. |
| Q1 getters | **PASS** | `0x8F4A80D0` = `return +0x6F0`; `FUN_8f65f000`/`FUN_8f65f0f8` = 12-char PainRecharge/DamageRecharge lookups at `+0x1C4`. |
| Q1 data | **PASS** | moves.xml: `RangedMissile`/`MagicMissile` `NoMagicRecharge="1"` (lines 703/742); `MagicPlayer` = `<ModExists Name="Concussion" Not="1"/>` (736); `MagicBombPlayer` @29297 / `MagicDarkWavePlayer` @33415 = `Template="1key|MagicPlayer" Type="ATTACK" Priority="110"`, `<Key Type="Magic" PressType="Tap"/>`. |
| Q1 candidate nuance | note | Candidate `addMagicCharge` parameterizes `pow2Factor(self,…)` per call; binary always applies `94f0`→attacker, `95a8`→victim (role-swap inside the function). Call-site semantics identical; spec formula text (Q1.3) is exact. Cosmetic, not a FAIL. |
| Q2 facing | **PASS** | `SetDirection` @`0x8F7994C0` (contents verified); xrefs from `FUN_8f483a3c` (template merge) and `FUN_8f48e258` (move builder) — both exactly as spec. Condition names `Direction` @`0x8F799544`, `ModelMirrored` @`0x8F799598` verified in the factory name table, xrefs from `FUN_8f488c18`. Data: `Controlled` = `Me.NPivot→Enemy.NPivot` SetDirection; `Step`/`ForwardStep`/`BackStep(Step|Back|Retreat)` — **no SetDirection**; `Retreat` = empty template (line 597); `Hit` = `<Impulse Reverse="1"/>` only (line 318-321); `GetUp` = `Me.NNeck→Me.NPivot`; `RangedMissileStart` = `Parent Wall Back→Front`. |
| Q2 deferred switch `0x8F4AC4B4` | **PASS** | `if (+0x218) { FUN_8f4a6398(f, +0x21C, +0x220?0x170:0); ...; +0x218=0; +0x21C=0; }` — move queued at Fighter+0x218, applied next frame, then cleared. |
| Q3 duck guards | **PASS** | Duck @7273 `Template="1key|Down|NotTitan"`; `1key` = `<CurrentInterval Name="SemiUninterrupt" Not="1"/>` (442); `Controlled` anti-restart = `<Operator Type="And" Not="1"><CurrentAnimation Name="$Move"/><CurrentInterval Name="SemiUninterrupt"/></Operator>` (429-432) — exact; `Step` = `Not(SelfUninterrupt AND (Step|DoubleStep))` (601-607) — exact. Deferred switch same as Q2. |

COMBAT_CORE verdict: **GREEN**. Both [UNCERTAIN] items are acknowledged in the spec;
the flag attribution was independently resolved via the applyHit call site.

---

## SUBJECT 2 — SPEC_WORLD_FEEL: **RED**

| Item | Result | Evidence |
|---|---|---|
| 1a crit chance `0x8F4A610C` | **PASS** | Mode `0x15` + `obj[0x8D]` + `FUN_8f4b1c90(hits+0x638)==0` → 0.0; `FUN_8f66011c` settings; `FUN_8f2a5f5c(attrs+0x1C4, name, &out, 1, 0)`; `return v * base`. Exact match incl. +0x1C4. |
| 1a crit flag `0x8F4AA998` | **PASS** | `if (+0x1C2==0 && move+0x4C==0) { FUN_8f4a610c; +0x1C3 = FUN_8f65bd64(); }` — `move+0x4C` NoCritical confirmed; shock `+0x1C4/+0x1C5` from `FUN_8f4a92bc`; damage `FUN_8f4a97b4(f, move, +0x1C2, +0x1C3, …)`. |
| 1a crit multiplier `0x8F4A95A8` | **PASS** | `!crit → 0x3F800000`; else `FUN_8f72ed40(2.0f, base·attr)` with base at `+0xC` of `FUN_8f65fcc0` settings, attr lookup at `+0x1C4`. Exact. |
| 1a roll `0x8F264674` | **PASS** | `K·(r1/r2 + (r3/r4)/r5) < chance·K` with 5 PRNG draws (`FUN_8f264a34`×3, `FUN_8f264a64`×2 — spec said "3 draws"; formula shape exact, count nuance). |
| 1a settings `0x8F6634C0` | **PASS** | `<CriticalHit>`: `Probability.Base→[0]`, `Attribute→[1..3]`, `Damage.Base→[4]`, `Attribute→[5..7]` — parse order Base-then-Attribute per block, exact. |
| 1a super_hit | **PASS** | `super_hit` — 0 string matches in image. |
| 1b knockdown data | **PASS** | Fall family at exact lines 6375/6425/6470/6516/6549/6587/6629; `Fall` = `Hit|NotTitan` (333). |
| 1b shock `0x8F4A92BC` | **PASS** | `+0x670` accumulator `+= scaled impulse` (param_2+0x34 / move+0x6AC); threshold `FUN_8f6601f8()[0]`; head-hit & shock-crit rolls vs settings `+8/+0x18` with attrs map `attacker+0x1C4`. |
| 2 bag negative claim | **PASS** | Only 4 bag strings in image: `assets/models/punching_bag.xml` @0x8F7A5AC4, `skeleton_punching_bag.xml` @0x8F7A5C0C, `btn_punching_bag(_pressed).png` @0x8F79BF08/58. No `BAG`/distance/`super_hit` strings; bag loader `FUN_8f674734` = 0x8f674734..0x8f679ccf ≈ 22 KB (spec "~22KB" ✓); hit path is the generic `FUN_8f4aa998→FUN_8f420f9c` pipeline; capsule parser `FUN_8f4a4418` structure consistent (strcmp name dispatch + figure build). Residual risk (no exhaustive float-constant 200.0 scan) noted — architecture evidence makes a bag-specific fallback branch implausible. |
| 3 spawns | **PASS** | `0x8F426524`: `FUN_8f43c6f8(Location)`; `FUN_8f271ae4(fighter+0xA0, Location+0x48/+0x54)`; placement `FUN_8f4d2e70(f, FUN_8f4d2a10/18)`; `FUN_8f43b0d0` ModelsViewer: 4 float attrs → Vec2 `+0x48`/`+0x54`, `Type==2 → Location+0x68`; path builder `FUN_8f43bdf8` = `assets/` + `locations/` + `<loc>` + `/` + `params.xml`. |
| 3 Wall/Floor offsets | **FAIL** | **Adjudication (disasm + literal-pool resolved):** parse order in `FUN_8f43c6f8` is — Music→`+0x18` list; **FrictionForce→global `FUN_8f633e64`**; **Wall→`+0x34`**; **Floor→`+0x2c`**; **PositionY→`+0x30`**; Color→`+0x60..62`; Width→`+0x38`; Height→`+0x3c`; MinWidth→`+0x40`, `+0x44`=min/width; GridSize→`+0x28` (int). Spec table says FrictionForce→+0x34 / Wall→+0x2c / Floor→+0x30 — **all three wrong** (Wall/Floor swapped AND FrictionForce is not a struct field at all). Candidate `LocationParams` offsets `wall+0x2c`, `floor+0x30`, `friction_force+0x34` must be corrected to `wall+0x34`, `floor+0x2c` (friction → global). The [UNCERTAIN] block `game+0x3E4E80..3E50A0` is actually string-append code of the path builder (inside `FUN_8f43bdf8`, which Ghidra spans 0x8F43BE80..0x8F43C38B); the real parse blocks are at `0x8F43CC1C+` (game+0x3E5C1C+). |
| 3 bounds semantics | **PASS** | No width/2 clamp in the level ctor; `0x8F426524` feeds `Location+0x34` (Wall) to a global and `Width−Wall` (`+0x38−+0x34`) to the playfield global; spawns come from `+0x48/+0x54`. The semantic claim (physics bounds, not width/2) is supported; only the offsets were wrong. |

WORLD_FEEL verdict: **RED** — §1, §2 pass; §3 offsets contract broken
(3 wrong offsets + candidate struct; one-line-per-field fixes).

---

## SUBJECT 3 — SPEC_PRESENTATION: **YELLOW**

| Item | Result | Evidence |
|---|---|---|
| Q1 PreFight ctor `0x8F416444` | **PASS** | `8PreFight` RTTI @0x8F796104, `11ScreenFight` @0x8F796118 (contents verified). Ctor: `FUN_8f1e3a5c` sprites ×2, `FUN_8f0775c4` texture loads ×4, sub-init `FUN_8f415fe4`. `textures/fullscreen/VS_Fon.xml` @0x8F79624C **xref from FUN_8f416444 @ 0x8f416478** — exact; `Stripe_left/right.png`, `VS.png` in the same pool (0x8F79627C/2A0/2C8). |
| Q1 avatars `0x8F411EDC` | **PASS** | Builds `image/users/image/<key>.png` ('.'+3-byte "png" append visible), sprite via `FUN_8f1e3a5c`, scale `FUN_8f65c874`, x=−110 (`0xC2DC0000`), scene z=3 (vtable+0xCC). Exact. |
| Q2 parse chain | **PASS** | `"Music"` @0x8F78F36C xrefs: `FUN_8f2c2e84` @**0x8f2c31c4** (Battle::parse — exact), `FUN_8f2c10c4` @**0x8f2c1280** (Stage::parse — exact), `FUN_8f43c6f8` @0x8f43cb54 (Location music list), `FUN_8f2d8124` (parseSounds), `FUN_8f633ea0`. |
| Q2 random pick `0x8F43BC98` | **PASS** | Vector<string> (12-byte elems) at `+0x18`; count via magic-mul division; `FUN_8f264564` random index; string copy out. Mechanics exact. |
| Q2 play site | **PASS** | `0x8F426524` tail: `battle==0 → FUN_8f633e78(default,1)`; else `FUN_8f2f5188(battle,1)` → `FUN_8f43bc98(&name, Location)` → `FUN_8f282ef8(name,1)`. `FUN_8f282ef8`: singleton `FUN_8f060288`, exists-check `FUN_8f06036c`, error log `FUN_8f226a58`, play `FUN_8f0603e8(name, loop)`. |
| Q2 source attribution | **FAIL** | The play site passes the **Location** object (param_1[0xA2], built by `FUN_8f43d3d4` + parsed by `FUN_8f43c6f8`), so the battle track = random of the **location's params.xml `Music` list** (`+0x18`), not `battle+0x18` from stages.xml. `FUN_8f43bc98` has exactly ONE caller (`0x8F426524`). stages.xml `Battle@Music` is parsed (`FUN_8f2c2e84`) but is not consumed at the fight-screen play site. The spec's "Rule to reproduce" must read: `battle_music = random(Location.MusicList from params.xml <Root Music="id|id">)` → registry → `assets/music/<name>.mp3`, looped, via fader. The spec's Q2.6 already documents the location-music registry path; Q2.6's "not the battle track" clause is wrong. Mechanics (random pick, play, guarded default, fader) all verified. |
| Q3 Tutorial `0x8F52C170` | **PASS** | switch on `param_1[0x4A]`, 25 successors, cases 0..0x17. Step bodies at exact addresses: 1→`FUN_8f52b524`, 2→`FUN_8f52b5d8`, 4→`FUN_8f528eb4`, 6→`FUN_8f528f04`, 9→`FUN_8f52957c`, 0xB→`FUN_8f529114`, 0xD→`FUN_8f529290`, 0xE→`FUN_8f5297fc`, 0x10→`FUN_8f52b744`; config `FUN_8f65abf0`+0x188 (case 8, TutorialWeapon) / +0x18C (case 5, TutorialBoss) / +0x190 (case 0xF, TutorialTournament); 0xC finish via `FUN_8f5260d4`; `tutorial_buy_knives` @0x8F79C4A8 (in the spec's 0x8F79C468..C750 range). |

PRESENTATION verdict: **YELLOW** — Q1, Q3 pass; Q2 one attribution error (fix in one line).

---

## Summary

| Subject | Verdict | FAIL items |
|---|---|---|
| SPEC_COMBAT_CORE (magic charge / facing / duck) | **GREEN** | — |
| SPEC_WORLD_FEEL (crits / knockdown / bag / enemy Y & bounds) | **RED** | params.xml offsets: Wall→+0x34 (not +0x2c), Floor→+0x2c (not +0x30), FrictionForce→global `FUN_8f633e64` (not +0x34); candidate `LocationParams` fields to fix; [UNCERTAIN] block range misidentified (real parse blocks at 0x8F43CC1C+) |
| SPEC_PRESENTATION (VS screen / music / tutorial) | **YELLOW** | Battle-track source: play site uses Location+0x18 (params.xml `Music`), not Battle+0x18 (stages.xml) |

**Overall VERDICT: RED** (one subject RED; all FAIL items are concrete, addressed
offsets/attributions — no engine-code edits required; specs only).

NEEDS_HUMAN: false — every FAIL is pinned to exact offsets/addresses with a
one-line fix; no unresolvable ambiguity (all spec [UNCERTAIN] items are either
resolved here or explicitly data-side and non-blocking).

## Residual notes
- `FUN_8f26e628` (Location+0x18 music-list append) and the exact "6|7"→registry
  split were not fully resolved (delimiter string at 0x8F781D70 is 4×0x00);
  the list-append itself is verified.
- crit-roll draw count is 5 (spec says 3) — formula shape exact; distribution
  claim unchanged.
- Bag negative claim relies on string/architecture evidence (no exhaustive
  float-200 constant scan).
- completeness_score of the anchor functions is 0% (auto-named FUN_* in this
  dump, no annotations) — annotation-only metric, does not affect verdicts.

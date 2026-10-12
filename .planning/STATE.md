---
phase: port-verification
status: in_progress
lastUpdatedAt: 2026-10-11
lastUpdatedBy: agent
summaryVersion: 2
---

# Session compact — Shadow Fight 2 web → C++/OpenGL port (1:1 verification)

## Objective
Clean C++17/OpenGL port of the **web** build of Shadow Fight 2, **1:1 by code,
logic and look**. Single source of truth: the minified `reference/www/sf2.502f0946.js`
(+ shipped XML under `reference/extracted/xml/res/` and `reference/www/res/`).
The user plays via `play.cmd`; report every divergence, find the ROOT (never the
symptom), never invent, never hardcode.

## Tooling (current)
- **Readable JS (Wakaru)** — `reference/www/sf2.readable.js` (106 205 lines,
  original identifiers). Produced by
  `wakaru.exe --unpack -o <dir> reference/www/sf2.502f0946.js` where
  `wakaru.exe` = `C:\Users\user\AppData\Roaming\npm\node_modules\wakaru\node_modules\@wakaru\cli-win32-x64\wakaru.exe`
  (npm wakaru v1.14.0). **Cite `readable.js:LINE`** for verification (the minified
  file stays the byte-truth). Set `REA_WAKARU_COMMAND` to that exe for the REA MCP.
- **Web oracle** — `reference/runner/` served via `python -m http.server 8000`
  (WebGL2 browser). Captures in `reference/traces/oracle_matrix/`.
- **Gates** (all hidden + `--watchdog`, SERIAL foreground): `--verify-input` 7/7,
  `--input-tape` 0 FAIL, `--flow-verify` 20/20, `--ui-tour` 15/15,
  `--fidelity-tour` 40/40, `--headless-loop` 13/13, `--boss-hit-probe` HIT,
  `--quest-verify`/`--quest-verify-buy`/`--dialog-verify`/`--tutorial-real-verify`/
  `--settings-profile-shop-probe`/`--save-fields-probe` PASS.
- **Build**: `cmake --build E:\reSF2\build --config Release` (incremental;
  `--clean-first` fails MSB4018). exe: `build\app\game\Release\game.exe`.
- **Save guard** (`1138c79a`, `640efc4b`): driver/probe runs back up
  `reference/saves/save.xml` once and restore on exit — the player's save is safe.

## RULE 0 (mandatory)
Hidden window + `--watchdog` FIRST-class; never leave a process; NO OS input
(only `App::inject_click`/`inject_key`). Use the ONE-LINE
`-ArgumentList "<flag> --watchdog N reference/www/res reference/saves/save.xml"`
form (a `$args` parameter / `@(...)` array can corrupt argv). Gates serial.

## Session history (JS-exact roots fixed)
Earlier waves closed the user's reported list: idle pose (`$Move`), ground death
for a ragdolled loser, between-round input/style reset, the max-level achievement
gate, the achievement-toast node scale, the tutorial buy park, the shop preview
re-seat + fresh Dojo rebuild, the round result plate timing, the `Bl.strike`
1-frame deform gate, `--verify-place` MIRROR, `$I()`/posed-bone Distance, the
Direction condition, the crash (FightScreen downcast), Great.MaxHealth, the COM
anchor, the Shock-edge solver, the audio bank sub-sounds, the map/tournament
`isRaid`, the disarm weapon name, `<BarScales>`/`<Attributes>` parsing,
`ShockWin`, `AllowDoubles`, the O1 quest gate, `<StepsNames>`, the crash driver.

**Wakaru era (readable-JS verification)** — the last ~20 commits found deep roots
that char-offset grep missed:
- `fad696b7` **`FightContext` items/perks/mods were empty** → typed
  `<Item>`/`<Perk>`/`<ModExists>` conditions never matched (166 `<Item>` etc.).
- `0e5c5e3a` `eval_birth` empty-Name, `<Operator Type>` (only literal "Or"),
  `<Item>` Type/SubType overwrite.
- `8a497955` `<Perk>` locks used the learned subset (not the merged `Wk()`);
  `<Perk>` `Player` selector; action `<Conditions>` ctx.
- `f2a995bb` round-end plates: `ey=2` (fired rule) vs `ey=3` (timeout).
- `a045f233`/`0ad0b085` quest action factory (7 attrs) + `EFight` queue drop +
  `<Operator>` no-Type false + `CaseSensitive`.
- `0d201470` HP-bar decay (damage/heal swap, leak drain, fixed-60Hz step, ROUND
  plate `-a/2`).
- `ece1c4f0` `MoveModel` tween floor + `$ea`; `d5853fdf` `kq` prize fold;
  `6b965e49` `BossAbilityState`; `7b6f1ca3` `Nwa` total.
- `c4681366` map `Xr` caption (`H0a` `stage{N}{M}`) + pip lit state.
- `3dabbcc8` `Bl.strike` split uses the **live span** `|Zs.ma−sx.ma|` (alias on the
  endpoint `ma`), not the XML rest-length.
- `4577c026` `$k`/`anim_names` (own name + transitive `<Template>` chain) at 7 AI
  sites; `Fh.jU` combo bonus was never set; `Vk` wrap order.
- `90b0f3e5` `SetRangeVariable` `MinValue`/`MaxValue`; `SetModVariable` live value;
  vars written to the **target** model (`UKa`); `<Random Chance>` ctx
  (`vars`/`hit_damage`/health); `MoveModel Axis` exact `split("|")` token.
- `87501954` achievements/tutorial/save/profile sweep — all JS-exact (docs).
- `8f24d467` shop discount **liveness** (`I.N1`: `yf.fE`/`yn`) + `nn()` scales the
  Ruby price by the same live discount.
- `ed587c31` re-verify of the pre-Wakaru fixes vs the readable JS — none wrong
  (only a ground-death attribution comment corrected).
- `6951902f` render: real `drop_blood` hit-spark sprite (no invented fade/shrink)
  + hot_ground callout label Y (absolute `D(-0.1h)`).
- `608aa260` font `eF` = **signed** i16 abs (shipped `.fnt` carry negative sizes
  → the old unsigned read gave 65446/65486, breaking every `fontSize/eF` ratio);
  callout scale `1.2·nat_h/eF` (hot_ground) vs `hud_c` (combo); banner Y =
  arena-projected centre `h/2 − 75·d`; spark-pool clear order (`Cnb` tests
  before increment, no reset); magic effects draw at constant alpha.
## User-reported bug list (2026-10-12) — waves A–D
- `52c37d10` slow-mo jitter (fractional re-sample).
- `2bd3c00e` **quests stalled after the Kenji fight** (the deferred Fight-quest
  drain never ran the `Yb` post-run bookkeeping → `zone_1` stayed unloaded until
  a restart) + **stray "outdated equipment" dialog** (a fresh buy had no
  `upgrade_level` → `?Purchase[].UpgradeLevel`=0) → map-unlock restored.
- `848bf24d` **Shin's "Overheat"** (catalog `<Enchantments>` wrongly fed to the
  enemy/player; the JS uses the SAVE enchantments for OWNED items) + shop/profile
  preview no re-seat on loop (the "teleport/подбрасывает") + shop weapon model
  rebuilt on entry + **`Style` achievement counter**.
- `83026a6e` **death animation** (`EndStance` pick one-shot on the first frame
  while ragdolled → `Loss_fall` rejected, `Loss_2` plays) + **throw victim
  1-frame outside** (arena clamp at `ragdoll_start`).
- NOT fixed (needs a follow-up): plate/stats timing (1b), passive Tabular AI
  (3a — no divergence found in the candidate/weight/slot logic), Monkey instant
  getup (3b), shop buy-plate flash highlight (2a), sensei side-dialog modality
  (4 — port matches JS), wrong dialog after reload (5 — no divergence found).
  ⚠ side effect of the death fix: a winner still mid-attack is rejected for
  `Win_Katana` (one-shot pick semantics) — needs a human look.

- `95a08c82` fight rules: `ERulePerk` — **195 shipped** `<Rules><Perk>` were inert
  (the rule perks never joined the live trigger set; `ca.ZOa`/`du.rmb`) +
  `ERuleNoPerks` (2 shipped `EndStanceClear`).
- `c84fb177` quest dialogs: Title/Image/Line text now run the full `ba.Pc`
  (`?`+`_`), not just `_`.
- `927125a9` fight rules: timer **Contest** — `Ema(9)` sets `cp=9` (L17178), the
  Points `compare` (L36937) then fires → `BT` → **`ey=2` (`rule_win`)**, not
  `ey=3` (timeout); winner `gj.wfa` (Contest `qH>gN`, Score `qH>=Max`).
- `153ad295` `?Hit[].BaseDamage` (JS `xgb` L56083 = `Bb.ODa * 2^(block) *
  2^(crit)`) was declared but **never filled** — 0 in every path. Added
  `HitRecord.base_damage` (`ODa`), threaded through `cond_ctx` +
  `run_bus_hit` + the `exec_action` action-value ctx via persistent
  `cur_hit_*` members (`Bb` persists). Shipped `perks.xml` L315/629/926/947
  read it. ⚠ build/gates pending (background wave held `main.obj`).

## Open items (documented, not invented)
- `vd` sub-fighters (`gea b==1`/`VAa`/`Tba`/`fCa`) — shipped fights are single-part;
  needs a new sub-fighter subsystem (`Qda`/`M2`/`Iha`/per-part bones).
- `StealMagicMod`(27) — needs a model-node graph + magic item slot.
- `?Hit[].BaseDamage` in `<Random Chance>` — `CondCtx` has no base-damage field.
- `?PlayerAttribute[Me|Enemy]` in the `<Random Chance>` path uses the setup
  snapshot (`rt.me_attrs`), not live `params.attributes` (the action-value path is
  already live).
- Latent (0 shipped uses): `ChangeScene CallEvents`, `ShowBattle Instant`,
  `ClickButton UseArrow`; `Pa.y2a` `DeliveryTime>0`; `<Attributes>` `Alias`/`Point`/
  `Format` (no consumer); `Bl.strike` inner `Fixed`-node gate.
- HP-bar segment stripes, per-language `eF` font split.

## Process
After each wave: incremental build, gates, commit + push (warm the credential:
`"protocol=https`nhost=github.com`nusername=hwindinkg`n" | git credential fill`,
then `git -c credential.helper=manager -c credential.interactive=false push ...`).
Never force-push. Never leave orphaned `game.exe`.

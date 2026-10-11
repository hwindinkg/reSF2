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

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
- `2881ccbc` plate/stats timing — the plate now rises on the first EndStance
  `AnimationStart` (JS `ca.Pf` L15718), Results decoupled from the `fu(1.166)`.
- `8fe4eeb0` **Monkey instant getup** — `strike_node` dropped the knockdown
  impulse when no live solver existed → the ragdoll started at zero velocity and
  settled in 1 frame → the `PhysicalLying` settle gate passed immediately. Now
  the impulse seeds the ragdoll (getup waits 21 frames) + the global-trigger
  event `Player`/`Ob` selector.
- `8ba3c5b9` **sensei side dialogs** (the `BlockRaycast=0` `Ib` bar swallowed any
  press; `dismiss_dialog` skipped bars; UI gates read the wrong modal slot),
  **shop buy-plate flash** (`highlightButton`), **reload clears the stale quest
  session** (the wrong dialog after a reload).
- `ed54192d` **passive Tabular AI** — the tactic's `ShortAttack` group does not
  exist in `moves.xml` (the data itself documents the hole), so the base slot
  was empty for every `Standard` enemy → the port now resolves the declared
  group to the owner's melee `Weapon` group (proven: a Standard enemy goes from
  ~2 hits/600f to `E hits=87`).
- ⚠ side effect of the death fix (83026a6e): a winner still mid-attack is
  rejected for `Win_Katana` (one-shot pick semantics) — needs a human look.

## User bug list #2 (2026-10-13/14) — waves J–M
- `60fe6da0` **post-Kenji knives quest / post-purchase restart** — `fire_inner`
  deferred EVERY event except SceneLoaded/ChangeTab in the Fight scene; JS
  suppresses only `FightEnd`, so the `StoryTutorialShop` trailing `<Activate
  ActionID="StoryTutorialOpenScene"/>` was deferred forever (the knives quest
  never activated). Gate now restricted to `FightEnd` + new
  `--tutorial-chain-verify` (full chain, no restart).
- `e364a3b7` **slow-mo still jerky** — the JS animator is a densely re-sampled
  span (`Te.Gka` `UM=(UJ+1)*Tx`, `Te.eda` 1 subframe/frame); the port faked the
  rate with `sub_frac_`/mixed spans. Now `UM=(XJ+1)*Tx`, integer `mo`.
- `2b1cc1a7` **knockover 1-frame jerk** — the impulse was written to the drawn
  `pos_` on the hit frame; JS `wd.Bva` puts it on the node `ma` INSIDE the
  ragdoll start. Now `stash_hit_impulse`/`apply_pending_impulses` replay it in
  `ragdoll_start` after the seed (frame-0 velocity = the impulse).
- `a7cb5905` **achievement counters** — the port implemented only 6 types and its
  comment FALSELY claimed the rest have no achievement. Ported the JS
  fight-session tracker (`fe`): parses `Animation`/`Weapon`/`CounterSpan`/the
  `<Battle>` gate; implements `PerfectRound/FirstHits/ComboCount/HeadHitRound/
  Disarm/**RestrictedAnimation (KickFights/PunchFights/Unarmed)**/HeadHit
  (HeadKick)/BlockedRound/HealthRemained/RoundQuicker/Longer/BossNoLose/
  TournamentsBeaten/ChallangesBeaten/SurvivalRounds`.
- `5daf4c19` shop buy highlight spans the full M8 plate (`draw_bb_plate`);
  **AI Random-path** `tG_` opponent-arm + `fIa` self-arm + round-start (the ~95 %
  double-attack / freeze); preview state re-entry re-seat (`Pi.L4`); new
  `--kick-ko` probe proving `KickFights` PASS.
- `4f70866c` **profile avatar idle-loop** (JS `wd.ia` `Gka` loops IN PLACE; only the
  first seat `Pi.job` re-seats) — the "back→forward + jump" is gone
  (`--profile-avatar-probe`: `restart_step 0.155` vs `steady 0.142`) + the hit
  impulse now REPLACES per strike (JS `Bb` is one overwritten descriptor).
## MILESTONE — ALL FIVE DIFFERENTIAL SCENARIOS ARE 0 (2026-10-16)
`reference/tools/compare_state.py` reports **0 divergences / 36 counted fields** for
**dojo-demo, boss-lynx, tournament, survival, challenge** (368 matched frames each,
100% coverage). The path to 0 (each JS-exact, oracle-verified):
- the tabular `Z0()` lookup is always EMPTY (JS `P.hKa` clears per-move) → `fk` 5/9;
- the AI candidate/decision pipeline: `de.Pqb`/`de.ia`, the Random `hJa` arm, the
  `Gc.DK` candidate build + `Aua` duplicate + the `Math.random`/`Da.pg` draw
  counts, the `Da.pg` seed (`Da.rL()` frozen clock), the tabular index resolution;
- the `<Move PackName>` pack filter (registry 1048 → 163 = JS `ra.Lk`; `me` 114);
- the enemy equipped slots (`a.parameters.jt()`/`ur` `<Items>` equip-by-type);
- `Te.vN` post-clip-end `Xh += 3`; the `Te.xj` interval OBJECT list (name+type);
- the phase-1 gate = the intro stance CLIP END (not 134 frames) + the plate tick
  before the phase machine; the post-intro selection via the `AnimationEnd` event
  pass (not a `Template` template pick); the `de.V1` opponent-model pivot
  (`e = a.nf` → `Object="Pivot"` resolves on the OPPONENT's skeleton).
- Commits: `d3ff40c8`, `569bf6ef`, `e2c36907`, `5dadb025`, `ee30d4d5`, `862e99ab`,
  plus the earlier `f0a3cf9f`/`c3f6fb12`/`c6599920`/`18264ec6` etc.

**BUILD NOTE:** `cmake --build` is BROKEN since the user's VS update deregistered
both instances from the VS Installer. Build with:
`& "D:\MicrosoftVisualStudio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" build\sf2_native.sln /p:Configuration=Release /m /nologo`.

## METHOD (root cause + fix) — 2026-10-14
**Why "new divergences keep appearing":** the port is a hand-transcription (read
JS → understand → write C++); verification was manual sampling; the only
mechanical check (the oracle) covered just the POSE. So coverage never
converged — each pass sampled new areas. The JS is 106205 lines / 3394 named
methods; only 1225 are explicitly mapped in the port's comments (36%).

**The fix — a MECHANICAL DIFFERENTIAL STATE HARNESS (built):**
- `reference/runner/trace.js` now emits a `{"t":"state"}` JSONL line per live
  fight frame (hp/mhp/combo/style/unhit/shock/block/invuln/move/af/intervals/
  ragdoll/phase/round/result/counters/rules/callouts) from the LIVE web objects.
- The port: `--dump-state N` (+ `FightController::dump_state_frame`).
- `reference/tools/extract_state.py` + `reference/tools/compare_state.py` diff
  the two, aligned by `(phase, move, af)`, like `compare_pose.py`.
- Run: `game.exe --fight --headless 600 --dump-state 400 --watchdog 150 …`;
  `shell\bin\Release\net9.0-windows\OracleShell.exe --timeout-ms 95000`;
  `python reference/tools/extract_state.py && python reference/tools/compare_state.py`.
- **First real diff** (dojo, 241 aligned frames, 89.9% coverage): 31 fields, **1
  diverging** (`fighters.Enemy.ragf` 108/241 — the ragdoll frame counter);
  everything else 0/241. Found + fixed `counters.lW` (the port stored the
  complement of JS `ca.lW`). Report: `reference/traces/state_gap_report.txt`.
- `14d1864e` harness: `ragf` → **0/241** (the port-only re-pose ticked the JS
  `Al.ia` `frameCount`); `callouts` → 0/241; `nf`/`timer` from the JS HUD
  (`Sf.NF`); `rules` from `dl.jh()`; a scenario selector
  (`OracleShell --query postTutorial=0`); `reference/traces/README.md` documents
  how to run the whole harness. **Remaining:** `rules.total/active` (241/241 — the
  port splits `ApplyTo="All"` into 2 copies where `dl.jh()` is 1); the boss/
  tournament/survival oracle runs need a hub→fight navigation script (the 350-
  frame cap stops at the hub viewer).

## Hunt waves (2026-10-14) — verbatim inaccuracy sweep
- `5bfd7d79` anim: `UM==1` samples the JS `p2` (the 3rd control point), not the
  span's `p0` — 19 reaction/fall moves (MidFrames absent) drew one span behind.
- `6a1a690f` anim: `move_frame_` = `ff + max(0, span-1) + Mq` (JS `Te.M0` reads
  `Xh`, which is `span+1` for the whole span) — every move's interval/action
  windows were opening ~1 frame early. **oracle-pose-validated** (pose diff
  unchanged; the `(xii)` probe re-derived from the JS; hit window F411 → F256).
- `4f70866c` profile avatar idle loop in place (`Gka`), no `Pi.job` re-seat;
  hit impulse = the LAST strike (JS `Bb` is one overwritten descriptor).
- `436ca606` hunt: pose-dump `clip` = the MOVE NAME (not the archive key); the
  dummy stage move (`PhysicalDummy`); `ragdoll_start` seed clamp only the `cA`
  endpoints (JS `fdb`/`fha`); `Te.M2` = a model-lifetime counter. Pose diff
  improved: real-fighter bone mean **0.020** / max **1.360**, **0/68** over tol.
- `21e08f9b` hunt: the `Physics="1"` stage move (the Punchbag) routes through the
  ragdoll (`DK` L674 `jJa`/`Al.start`) — the dummy is no longer frozen
  (over-tol 238/242 → 109/242, mean 264.8 → 7.9); the ragdoll `fx` reset
  (`Qnb`'s `da.reset()`) — dummy fx mismatch 241/242 → **0/242**.
- OPEN (hunt): the dummy's constant ~1.886 world-y bind/pivot offset (a placement
  convention, not ragdoll); the dummy pair's `109/242` over-tol is a comparison
  alignment artifact (a repeating idle cycle, `cf` always 0).
- OPEN: the frame-exact AI cadence vs a JS oracle trace is unreachable (live
  oracle down); `BarelyAliveRounds` HP scale; `EclipseMode="1"` rows; the
  `{Uninterrupt,SelfUninterrupt}` interval set is a hardcoded pair (shipped-
  correct); `--quest-verify-buy` needs `--watchdog 1500` (harness pacing).

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

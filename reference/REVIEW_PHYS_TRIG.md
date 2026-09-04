# REVIEW_PHYS_TRIG — transcription-fidelity review (Stream 1 implementation)

Reviewer role: verify only, report only — no code fixed here. Scope:
Stream 1's uncommitted `core/scene/physics.{hpp,cpp}`,
`core/scene/trigger.hpp`, fight.cpp trigger wiring (bus setup ~L680-700,
`run_bus_hit` ~L895-970, `tick_mods`/`tick_bus_side` ~L971-1070, slot-6/7
wiring ~L1215-1330, interval edges ~L1455), `perks.hpp:81-154` decider,
`item_catalog.cpp:22-103` — checked line-by-line against
`reference/www/sf2.502f0946.js` **plus shipped-data impact for every
finding** (`perks.xml` 202 perks, `list.xml` catalog).

Severity: **HIGH** (wrong behavior on shipped data) / **MED**
(conditional or bounded) / **LOW** (rare/visual/narrow) / **NOTE**
(verified equivalent or judgment call, no action).

---

## A. Physics (`Bl.strike` map: rest-length `b`, full-vector impulse, `wBa` feed)

- **BUG MED-HIGH — margins added to radius** (`physics.cpp:118-119` vs JS
  L572/L791-793/L11). JS `$Fa`/`aGa` (Margin1/2) are endpoint lerp factors
  (`Uy` lerp proven, L11; `Lnb` insets `Ula`/`Pda`, L792-793), never radius
  adds. `HitCapsule` carries the fields (`model.hpp:70-73`,
  `model.cpp:139-142`) but misuses them. Fix: `radius=edge.radius` only;
  inset endpoints by `$Fa/(1-aGa)` at build. Impact: every capsule inflated
  (nil only if shipped margins are all 0 — suggest a scan assert).
- **BUG MED — `o$` staleness in the `b` weight split**
  (`physics.cpp:249-255` vs L567/L588/L510-512). JS zeroes W8/X8 once per
  `W1a` call; Ls branches 1-2 write only W8(`n$`), so `o$`=(0,0,0) and
  `Bl.strike`'s `b=min(1,|o$-sx.ma|/rest)` ≈1 (all weight to node2) in the
  common case. C++ feeds the real point. Fix: model `n$`/`o$` separately
  with X8 staleness.
- **BUG LOW — collinear-overlap UB** (`physics.cpp:76,126-131` vs L14-15).
  JS writes e≈segment-a start then returns true; C++ returns true with
  uninitialized `p`. Fix: `out = a` on that path (rare path, UB today).
- **BUG LOW — missing NG skips + MG gate** (L588 `d.NG||…`, `e.NG||…`,
  `if(!d.MG||!e.MG)`). Also unmodeled: `s2a()` pose smoothing (L588) and
  `bFa` length refit (L583/L792, `cA`-gated) — NOTEs (small/history-
  dependent/gated).
- **BUG LOW — dead `dmg_add`** (`perks.hpp:112`, zero use sites) vs JS
  `WKa` sets live `Ly` (L1294). Fix: route to future-hit Ly (needs Ly
  field + `bCa` hook) or document.
- **NOTEs (verified equivalent, no action)**: X/Y-plane test (the hpp "XZ
  plane" comment is wrong — code matches L12; fix comment); `c+=f` sum;
  `f2`-degenerate; `Ls` arg order; `line_of≡Vy`; crossing parametrization
  (suggest numeric test); rest-length source (`model.cpp:139` = `Length`
  attr = `yu.length`, L572); `wBa` own-first/foe-fallback (L576; `jX`
  short-circuit unmodeled, minor); mass default (JS `u.H`→0, L2455, vs C++
  1.0 guard — check Mass presence); `fha`/`P6a` close (L583-584); pose z=0
  (equivalent given zeros); `b=min(1,dist/rest)` exact; NaN-degenerate
  equivalent.

## B. TriggerBus

- Slot map 1-16 **verified** vs `Ac.parse` (L1317-1319). `v_a` gates,
  match-then-execute + same-tick `Qh` drain, side order, `t0a`/`Axa`,
  Provoke-immediate, `Pob→UKa(d=truthy)` — all equivalent. Re-entrancy: JS
  unbounded vs C++ depth-8 (safer; differs only on cyclic data) — NOTE.
- **BUG HIGH — `ModFlag` (178 uses) installs nothing** (falls to `perknoop`
  log, fight.cpp:889-891) vs JS nm-entry → `UO` name → `ModExists`
  matches (L1291-1292). Breaks Blocker/RockOn/Icon-family chains (287
  `ModExists`; e.g. HELM_BREAKER/EAGLE_EYE/WIDOW_SHIELDING/SHOGUN_FRENZY
  set Blocker). Fix: install named persistent mod (mirror ModIcon path).
- **BUG HIGH — `kp` expression conditions always false** (29 perks incl
  `PERK_BEGINNER`, boss/set perks) vs `Qa` engine (L2362-2363 `Uha`).
  Fix: implement at least numeric-literal comparisons.
- **BUG MED — per-round re-register missing**: JS `tb.Yka` every round
  (L401/402/405) drops live timed mods; C++ `setup_bus` once per fight
  (fight.cpp:313) over-persists (e.g. 300-frame AVENGER Icon). Fix: mirror
  Yka clear at `round_init`.
- **BUG MED — Health absolute vs ratio** (L1310-1311): JS compares `gd`
  absolute; C++ `hp_ratio`. Diverges only for non-inverted fractional
  bounds (DESPERATE `Min=0/Max=_HealthLimit`); 0-bounds
  (TITAN_DEATH/FEAR_RAY) coincide. Fix: compare absolute hp.
- **BUG LOW — `ModExists` namespace wrong field** (7 Titan-namespace
  uses): C++ compares mod NAME to ns; JS `YZa` checks entries under ns
  with action-name match (L1369). Fix: store action name on ModState,
  index by namespace.
- **BUG LOW — `Round` gate** (MINE_PLAYER/SPHERE_COOLDOWN `Number=1`):
  JS `b.round` writer not found (likely always-false); C++ fires round 1.
  Verify writer, then align.
- **BUG LOW — unhandled exec types** (log-only, JS applies):
  StealMagicMod(1)/SlowModel(3)/ChangeModelColor(3)/SetCooldown(4)/
  SetDarkness(3)/MoveModel(1)/AddBullets(2)/AddMagicCharge(1). Plus JNa
  revert log-only for 27/28/29 (JS restores, L1298-1299) and Bullets
  conditions (2 perks) always false (no bh/dO).
- **NOTEs**: Hh `$k` ≡ exact (`xl`=[own name] only, L711/L723; 50
  Animation="Weapon" gates dead both sides); Style/Combo/Pain zero
  shipped uses (latent model differences); Random logic≡ but default draw
  0.5 + no `Da.pg` draw (stream divergence, 74 uses); CurrentAnimation
  name≡ + Min/Max fail-closed (2 InvisibilityCloak uses);
  CurrentInterval ✓ (data Name-only); Item Name-only ✓;
  Operator/RoundStage/PerkStart/InTheArea ✓ (area false + OPEN);
  `t0a`/`Axa`/ob/stage/step/interval/mod gates spot-verified;
  `c8a`/enabled (no mid-fight disable path in JS either); dpb
  namespace/`-1` nuances; `cpb`/U4 OPEN (documented); TurnOffCollision/
  Switch/ShowDebugLine/MarkPerkAsUsed equivalent-inert; DisableInterval
  ✓ in-hit; ModHealthChange ✓ (persistent equivalence holds);
  Lifesteal/SetHit/ChangeImpulse/ModAttributes/ClearMods/SetModFrames/
  SetModVariable/SetRangeVariable/SetTactic/Provoke verified per
  PERKS_STATIC §5.2 semantics.

## C. Loader (`trigger.hpp` + `item_catalog.cpp`)

- `_Var` plain-name subst ✓; expression fallback documented OPEN ✓
  (TIME_SURGE `?Abs[]` hits it).
- Template merge ✓ for var-less (EndStanceClear verified);
  re-substitution OPEN noted ✓.
- Unknown-name drop ✓ both.
- **NOTE**: `Kia` subset/budget not modeled (C++ takes all refs) —
  bounded: ≤2 refs/item, 2 budgeted items in shipped data; `enchant`
  flag unused. `I.JS` null-skip ✓.

## D. Severity rollup

- HIGH: ModFlag-no-install, kp-false (29 perks).
- MED: margins-to-radius, o$-staleness, per-round re-register, Health
  abs-vs-ratio.
- LOW: collinear UB, NG/MG+s2a/bFa, namespace-YZa, Round gate, 8
  unhandled exec types, JNa-27/28/29, Bullets, dmg_add, TurnOffCollision.
- Rest NOTE / OPEN-documented. No drive-by refactors proposed.

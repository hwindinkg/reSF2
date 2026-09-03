> ⚠️ УСТАРЕЛО. Этот документ описывает реверс нативного/Unity билда SF2,
> НЕ веб-версии. Не использовать как источник истины для порта.
> Валидный оракул: reference/www/sf2.502f0946.js + shell/OracleShell.

# Plan: GAP-3 — Wire Original Damage Formula (getTotalDamage) into reSF2 Combat

## Overview

Replace the three linear placeholder damage computations in `engine/game/game.cpp` with the
recovered original model: `get_total_damage(const DamageInputs&)` (Model::getTotalDamage @
game+0x4527B4, exponential `2^(delta/10)`), fed by per-fighter `AttributeSet` state
(Model::getParameter @ game+0x6275F4). Player attributes are aggregated from equipped items;
the placeholder enemy gets the `<AlignTargetAttributes>` baseline. Both ported headers are
golden-test-pinned and are NOT modified — this task is wiring only.

**Amended 2026-07-31:** the "perks skipped in MVP" decision is REVERSED. Perk→attribute
wiring is recovered from the binary via a dedicated workstream (Steps 3-6) gated by a
Case A / Case B classification fork, inserted before the `rebuild_fighter_attributes()` step
so aggregation knows where perk data plugs in.

## Requirements

- R1: Three call sites (game.cpp L1842-43, L1874-75, L3641-3691) compute damage via
  `resf2::game::get_total_damage()` — the linear `1.0f + damage_factor_base * 0.0f` form is gone.
- R2: The stray `* 2.0f` at L3677 (and its twins at the two enemy→player sites) is DELETED —
  2.0 is the power base inside the formula, not a trailing multiplier.
- R3: Each fighter carries an `AttributeSet`; player's is rebuilt from equipped items on
  load/equip/unequip; enemy's is seeded from `align_target_attributes()`.
- R4: The `-1e35f` getParameter sentinel NEVER reaches `powf` — the damage path uses only
  `attribute_difference()` / `get_or(..., 0.0f)` (0.0f default per game+0x60DF98).
- R5: Multiplication order inside `get_total_damage` (base*f2*f1*f3*add) is never reordered;
  `damage_formula.hpp` and `attributes.hpp` are not edited at all.
- R6: Block stays at the call site as a post-multiplier (see "Block decision" below).
- R7: `[ORIGINAL]`/`[HEURISTIC-TODO]` comment style with binary refs is preserved.
- R8: `test_damage_formula_golden`, `test_attributes_golden`, and the full ctest suite stay green.
- R9 (amendment): perks.xml schema and the list.xml→perks.xml reference mechanism are
  documented in `reverse/analysis/PERK_SURVEY.md`, and the perk parse + perk application
  code is located in the relocated dump with addresses and decompiled shapes recorded.
- R10 (amendment): every perk mechanism is explicitly classified — Case A (persistent
  contribution to the model+0x1C4 attribute map) → extracted, re-verified GREEN, wired;
  Case B (triggered/timed effect) → anchored with binary refs as 5.1 scope, single
  `[ORIGINAL]` TODO at the plug point. Mixed outcomes are allowed (per-mechanism fork).

## Architecture Changes

- **New file**: `engine/game/attribute_aggregation.hpp` — pure free functions:
  `aggregate_equipment_attributes(const format::ListData&, const inventory::Inventory&) -> AttributeSet`
  and `seed_enemy_baseline_attributes() -> AttributeSet`. Header-only, no Game dependencies
  (unit-testable without the Game object). Gains the **perk plug point** in Step 5 (Case A:
  a perk-contribution function + call inside aggregation; Case B: a single `[ORIGINAL]` TODO).
- **New file**: `reverse/analysis/PERK_SURVEY.md` — data survey (schema, reference
  mechanism, per-perk hypothesis), binary anchors (parser + apply path), and the fork
  verdict table with re-verifier verdicts. Documentation only.
- **Modified**: `engine/game/types.hpp` (~L384-400) — `FighterState` gains `AttributeSet attributes;`
  + `#include "attributes.hpp"`. NOTE: orchestrator context said game_clean.hpp; the struct
  physically lives in types.hpp, so the member goes here. attributes.hpp is standalone — no
  include cycle.
- **Modified**: `engine/game/game.cpp` — three damage sites rewired; new
  `Game::rebuild_fighter_attributes()`; two new `#include`s; dbg-overlay remap at site 3.
- **Modified**: `engine/game/game_clean.hpp` (~L457) — declaration of
  `rebuild_fighter_attributes()` next to `sync_equipped_weapon()`.
- **Modified** (Case B only): `reverse/analysis/PORT_GAPS.md` — one note appending the
  Case B perk anchors to the 5.1 (Condition/timed-effect system) scope.
- **New file**: `tests/test_damage_wiring.cpp` + registration in `tests/CMakeLists.txt`.
- **NOT modified**: `engine/game/damage_formula.hpp`, `engine/game/attributes.hpp`
  (golden-pinned verified ports).

## Key Decisions (surfaced for CONFIRM)

### Block decision (required by task brief)

**Block stays at the call site; it does NOT move into DamageInputs.** Evidence:
1. The verified disassembly of getTotalDamage (game+0x4527B4, documented in
   damage_formula.hpp) contains no block term — dmg = base*f2*f1*f3*add, then *crit*multA*multB.
2. The original's built-in tracer (game+0x438530, per PORT_GAPS.md GAP-3) prints
   `BlockDamageFactor` / `Block` in the *hit processor that calls* getTotalDamage.
3. DamageInputs therefore correctly has no block field, and damage_formula.hpp must not be
   touched (golden-pinned).

Call-site form: `final = get_total_damage(din) * block_factor;` where
`block_factor = base_block_factor` (0.5) when the defender blocks, else 1.0. The existing
`ignores_block` override at site 3 is preserved verbatim.

### DamageInputs mapping per call site

Common to all three sites:

| DamageInputs field | Value | Why |
|---|---|---|
| `base_weight` | `dmg_settings.damage_factor_base` (0.0001) | `<DamageFactor Base="0.0001" Attribute="DamageFactor"/>` |
| `base_attribute` | attacker attrs `get_or("DamageFactor", 0.0f)` | Absent in MVP → 0 → base = 2^0 = 1.0f. `get_or`, NEVER raw `get()` (R4) |
| `attacker_weight/attribute/enabled` | `0, 0, false` → f1 = 1.0f | f1 selector data (is_ranged factor sets, game+0x4A94F0) not yet ported — `[HEURISTIC-TODO]` |
| `defender_weight/attribute/enabled` | `0, 0, false` → f2 = 1.0f | f2 selector (enemy, weapon, game+0x4A95A8) not yet ported — `[HEURISTIC-TODO]` |
| `hit_damage` | the site's existing `base_damage` (move damage, `average_base_damage` fallback) | Original's `hit[0x48]` |
| `enemy_damage_bonus` | `0.0f` | Original's `enemy[0x774]` — model bonus, not ported |
| `crit_factor` | `1.0f` | Crit system (CriticalChance/CriticalDamage) is a separate follow-up — keep `[HEURISTIC-TODO]` |
| `enemy_multiplier_a/b` | `1.0f` | enemy[0x678]/[0x6AC], unidentified — identity default |

Per-site differences:

| Site | Attacker attrs | dmg attr | Defender attrs | def attr | Block |
|---|---|---|---|---|---|
| 1 (L1842, enemy→player, blocked) | `enemy_fighter_.attributes` | `UnarmedDamage` (enemy strikes = HighPunch) | `player_fighter_.attributes` | `BodyDefense` | `* base_block_factor` post-multiplier |
| 2 (L1874, enemy→player, unblocked) | same as site 1 | `UnarmedDamage` | same as site 1 | `BodyDefense` | none (×1.0) |
| 3 (L3641, player→enemy) | `player_fighter_.attributes` | `WeaponDamage` if `equipped_weapon_ != "Fists"` else `UnarmedDamage` | `enemy_fighter_.attributes` | `BodyDefense` | `* base_block_factor` when `enemy_fighter_.is_blocking && !move.ignores_block` |

Pairing rationale (helper game+0x60DF98): melee damage attributes vs BodyDefense. HeadDefense
is populated in the AttributeSet but NOT selected this pass — grep-verified that neither
`MoveDef` nor the asset move struct has a hit-zone field, and the original's HeadHit is a
HitEffects/chance concept, not per-move zone data. `[HEURISTIC-TODO]` note left for when
hit-zone data lands. Ranged/magic pairing (RangedDamage/MagicDamage) is out of scope — see
Non-Goals.

### Attribute aggregation

- **Player**: `reset_to_zero()`, then for each of `inventory::kAllSlots` (5 slots): resolve
  equipped id → `ListItem` in `list_data_.items` by name; accumulate via
  `add_item_contribution(weapon_damage, body_defense, head_defense, ranged_damage, magic_damage)`
  PLUS `add("UnarmedDamage", (int)unarmed_damage)` separately — `add_item_contribution` has no
  unarmed param and its signature is golden-pinned (test_attributes_golden.cpp L112-124), so
  attributes.hpp is not extended.
- **Enemy**: `reset_to_zero()` then `set(t.name, t.value)` for each `align_target_attributes()`
  entry (WeaponDamage 12, UnarmedDamage 0, BodyDefense 12, HeadDefense 5, RangedDamage 12,
  MagicDamage 12, EnchantmentResistance 12). This is the documented purpose of
  AlignTargetAttributes ("normalise an opponent's attributes"). When stage warriors land
  (game.cpp L1685: "until warrior templates land (5.3)"), per-warrior items replace this seed.
  Enemy-side perks (stage XML `<Perks><Perk Name=.../>`, stage_parser.hpp L58-59/123) follow
  the same fork verdict as item perks — no separate enemy perk path is built in this phase.
- **Perks — DECISION REVERSED 2026-07-31 (user amendment).** The previous MVP decision
  ("skip perk contribution, mapping not recovered") is void. `ListItem.perks`
  (`ListPerk{name, params}`, list_parser.hpp L29-33 — params are alternating key/value
  strings) MUST be traced into the binary and classified per the fork below; the aggregation
  function gains a **perk plug point** whose content depends on the Step 5 verdict:
  Case A → verified contribution code; Case B → one `[ORIGINAL]` TODO with binary refs.
- **Rebuild triggers** (all idempotent): after save load (game.cpp ~L609-611, next to
  `sync_equipped_weapon()`), in `host_equip_item` both success paths (after
  `player_profile_.equip_item(...)`), in `host_unequip_item` success path.

### Perk recovery framework (amendment 2026-07-31 — replaces the perk skip)

Ground truth already established by inspection (do not re-derive):

- `assets/files/assets/perks.xml` (3373 lines; a second copy at `assets/perks.xml`) is NOT a
  flat param list — it is a trigger script system: `<Perk Name Template Image>` with
  `<Set key=value .../>` params (`_Param` substitution into trigger bodies) and
  `<Trigger><Events>/<Conditions>/<Actions>` blocks. Observed vocabulary includes events
  `HitPreCrit` / `HitPostCrit` / `RoundStageStart`, conditions `LessEqual` / `ModExists` /
  `Random`, actions `ClearMods` / `SetHit` / `ModFlag` / `ModIcon` / **`ModAttributes`** /
  `Provoke`. Some perks are bare (`PERK_DOUBLE_SWEEP` = Name+Image only).
- `ModAttributes DamageFactor="_DamageFactor" Frames="1"` (PERK_HELM_BREAKER) proves
  attribute writes exist inside the perk system — but frame-scoped, i.e. likely Case B.
  The binary, not the XML, decides.
- PORT_GAPS.md L466-477: the perk/quest expression evaluator strings (`PlayerAttribute`,
  `EnemyAttribute`, `PlayerParameter`, `PerkAspectParameter`, `DefenseAttribute`, ...) sit
  near the attribute code — the systems are adjacent in the binary.

**Fork decision criteria (the Step 5 gate — per MECHANISM, not per perk file):**

| Signal in the apply path (from Step 4 evidence) | Verdict | Action |
|---|---|---|
| Writes into the same name-keyed int map at `model+0x1C4` that `Model::getParameter` (game+0x6275F4) reads, at equip/apply time, with NO Frames countdown and NO Condition gating → value present whenever getParameter runs | **Case A** | Extract exact perk→attribute mapping (param key → attribute name, scaling, sign, int conversion); produce candidate C++; **@re-verifier loop (mandatory, max 3 rounds, verdict must be GREEN)**; wire verified mapping into `Game::rebuild_fighter_attributes()` via the aggregation plug point |
| Registers/subscribes trigger instances (HitPreCrit / HitPostCrit / RoundStageStart event lists), evaluates Conditions, or applies `Mod*` actions with a Frames countdown (timed, transient — effect lives and dies inside the trigger system, incl. frame-scoped `ModAttributes`) | **Case B** | Do NOT wire into AttributeSet. Record each perk's anchor (binary address + hooked mechanism) in PERK_SURVEY.md and one PORT_GAPS.md note under 5.1 scope; leave exactly ONE `[ORIGINAL]` TODO with the binary ref at the aggregation plug point |
| Both mechanisms present (likely — one perk can carry Set params AND trigger actions) | **Mixed** | Wire the Case A subset, anchor the Case B subset |

Binding constraints for the workstream (carried over, still in force):
- **FIDELITY**: multiplication order base*f2*f1*f3*add untouched; sentinel -1e35f never
  reaches powf; the stray `* 2.0f` deletion stays an explicit step (Step 9).
- **SCOPE**: perk recovery is limited to the attribute path. The full Condition/timed-effect
  system (PORT_PLAN open item 5.1) and GAP-4 remain out of scope.
- **GHIDRA METHOD** (PORT_PLAN.md method notes): use the RELOCATED dump
  `reverse/binaries/game_region_runtime.bin` at base `0x8F057000`, `ARM:LE:32:v7` — never the
  static file; mind the PC+8 trap (the PC belongs to the `ADD`, not the `LDR`); prefer Ghidra
  `get_xrefs_to` over hand-rolled scanning (`find_string_xrefs.py` has produced false
  positives); use `resolve_dats.py` to read `DAT_x + -0yyy` noise as strings.
- **RE VERIFICATION (project rule)**: every Ghidra-reversed function must go through
  @re-verifier with the function address + candidate C code before being called done —
  fix FAIL items and re-verify, max 3 rounds; if still not GREEN or NEEDS_HUMAN, STOP,
  revert that perk to Case B anchoring, and escalate to the human.

### Observable behavior change (intended)

With neutral attributes the old code produced `damage × 2.0` (the double-counted power base);
the wired code produces `damage × 1.0`. Damage numbers HALVE at zero attribute delta, then
scale exponentially: player WeaponDamage 22 vs enemy BodyDefense 12 → ×2.0; unarmed (0) vs
enemy baseline BodyDefense 12 → ×0.435; enemy UnarmedDamage 0 vs armored player BodyDefense 12
→ ×0.435. Fights get longer; armored builds matter. Case A perks (if any are found) shift
these deltas further by their verified amounts — e.g. a perk contributing DamageFactor raises
the attacker's effective attribute before the formula runs. This is the faithful curve — any
test asserting absolute damage values is updated with a justification comment, never "fixed"
by re-adding multipliers.

## Non-Goals (scope guard)

- NO GAP-4 work (AI roulette/tactic tables).
- NO changes to the projectile/ranged damage path (game_clean.hpp L4388-4402 — a separate
  fourth site; flagged here as follow-up, with RangedDamage vs BodyDefense pairing noted).
- NO crit system, NO factor sets (f1/f2 stay disabled-neutral with TODO anchors).
- NO changes to damage_formula.hpp / attributes.hpp.
- NO save-format changes (AttributeSet is runtime-only, rebuilt on load).
- **Perk scope limit (amendment)**: only the perk→attribute contribution path is recovered.
  The full Condition/trigger/timed-effect system (PORT_PLAN open item 5.1 — event
  subscription, Conditions, ModFlag/ModIcon/Provoke, frame-scoped ModAttributes) is NOT
  ported; Case B findings are documented anchors only. NO perk UI/icons, NO perk shop/forge
  behavior, NO enchantment (ListEnchantment) wiring beyond what the same fork classifies.

## Implementation Steps

### Step 1 — Aggregation module + unit test (TDD)
**Files**: `tests/test_damage_wiring.cpp` (new), `engine/game/attribute_aggregation.hpp` (new),
`tests/CMakeLists.txt` (~L466, after the test_attributes_golden block)
**Task**:
1a. Write the test first (RED — will not compile): pins (i) empty inventory → all seven
attributes 0; (ii) weapon (WeaponDamage 22, UnarmedDamage 0) + armor (BodyDefense 8) + helm
(HeadDefense 4) equipped → summed values incl. UnarmedDamage; (iii) two contributors to the
same attribute stack; (iv) enemy baseline equals align_target_attributes() exactly;
(v) `attribute_difference` between aggregated sets feeds `attribute_difference_factor` with
no sentinel ever produced (`!attribute_is_missing(...)` on every `attribute_names()` entry).
Mirror the CHECK/near_eq harness style of test_attributes_golden.cpp.
1b. Implement `attribute_aggregation.hpp` until green (the two free functions above).
1c. Register in CMake mirroring the test_attributes_golden block (add_executable /
resf2_warnings / cxx_std_23 / add_test / TIMEOUT 60). Test also needs resf2_format +
the game inventory headers — match include linkage used by test_inventory.
**Verify**: `cmake --build build --config Release --target test_damage_wiring` &&
`ctest --test-dir build -R "^test_damage_wiring$" --output-on-failure` — green.
Golden gates: `ctest --test-dir build -R "^test_(damage_formula_golden|attributes_golden)$"` — green.

### Step 2 — FighterState gains AttributeSet
**File**: `engine/game/types.hpp` (include block at top; struct at L384-400)
**Task**: `#include "attributes.hpp"`; add `AttributeSet attributes;` to `FighterState` with
an `[ORIGINAL]` comment (model+0x1C4 name-keyed int map). Additive member only.
**Verify**: `cmake --build build --config Release --target test_fighter_states` (consumes
FighterState) && full-game target compiles; `ctest --test-dir build -R "^test_fighter_states$"` — green.

### Step 3 — Perk data survey (assets only, NO binary) [AMENDMENT]
**File**: `reverse/analysis/PERK_SURVEY.md` (new). Reads: `assets/files/assets/perks.xml`
(canonical shipped asset; note the duplicate at `assets/perks.xml` and record which root the
engine's asset conventions load), `engine/format/list_parser.hpp/.cpp` (ListPerk),
`engine/format/stage_parser.hpp/.cpp` (warrior perks), `assets/files/assets/list.xml`
(perk-bearing item examples).
**Task**:
3a. Document the perks.xml schema: `Perk@Name/Template/Image`, `<Set>` params and `_Param`
substitution, Template inheritance, and the full Trigger/Events/Conditions/Actions
vocabulary with counts. Group perks: "has Triggers" vs "bare (Name+Image only)".
3b. Document the reference mechanism: how `ListPerk.name` (+ alternating key/value `params`)
on a list.xml item maps to `Perk@Name`; give 2-3 concrete equipped-perk examples (item name
→ perk name → params). Note stage XML warrior `<Perks>` entries as the enemy-side source.
3c. Add a per-perk **hypothesis** column (attr vocabulary present — `DamageFactor`,
`ModAttributes`, defense names → Case A candidate; pure icon/flag/trigger → Case B
candidate). Hypothesis ONLY — the binary survey (Step 4) decides.
**Verify**: PERK_SURVEY.md committed; the vocabulary table covers 100% of distinct XML
element names in perks.xml (grep-count cross-check); every `ListPerk.name` referenced from
list.xml resolves to a `Perk@Name` or is explicitly listed as unresolved.

### Step 4 — Perk binary survey (Ghidra) [AMENDMENT]
**Tool**: Ghidra MCP, program `game_region_runtime.bin` — RELOCATED dump, base `0x8F057000`,
`ARM:LE:32:v7` (never the static file). Output: append "Binary anchors" to PERK_SURVEY.md.
**Task**:
4a. Locate the perks.xml parser: find the `"perks.xml"` string, `get_xrefs_to` (NOT
find_string_xrefs.py — known false positives) → loader function; record address + decompiled
shape. Element-name strings (`"ModAttributes"`, `"HitPreCrit"`, `"PerkAspectParameter"`,
`"ConditionPerk"`) → the parser's element dispatch.
4b. Locate perk application: xref 2-3 concrete perk-name strings from the Step 3b examples
(e.g. `"PERK_HELM_BREAKER"`, `"PERK_AVENGER"`) → where perk names are matched/activated;
follow into the apply path. PC+8 trap: when resolving globals the PC belongs to the `ADD`,
not the `LDR`.
4c. Answer the fork question with evidence: does the apply path write the `model+0x1C4`
name→int map (the one getParameter @ game+0x6275F4 reads) persistently at apply time — or
register Condition/Trigger instances / timed Mod* actions? Capture the exact write target
container and any Frames/Condition gating.
**Verify**: PERK_SURVEY.md records for (a) parser and (b) apply path: addresses, decompiled
pseudocode, and the xref chain used; every address is reproducible via the stated Ghidra
query. Documentation only — ZERO engine code changes in this step.

### Step 5 — Classification fork (decision gate + implementation) [AMENDMENT]
**Gate**: explicit verdict table in PERK_SURVEY.md — per perk mechanism → Case A / Case B /
mixed → evidence address. Criteria per the "Perk recovery framework" table above.
**Task (Case A branch)**: extract the exact perk→attribute mapping (param key → attribute
name, scaling, sign, int conversion); write candidate C++ (a perk-contribution function in
`attribute_aggregation.hpp`, e.g. `add_perk_contributions(const ListPerk*, AttributeSet&)`,
plus its call at the aggregation plug point); **route through @re-verifier** with the
function address + candidate C code (project RE rule; fix FAIL items, re-verify, max 3
rounds; not GREEN or NEEDS_HUMAN → STOP, demote that mechanism to Case B anchoring,
escalate). Only re-verifier-GREEN mappings are wired into `Game::rebuild_fighter_attributes()`
via the plug point. Paste each verdict into PERK_SURVEY.md.
**Task (Case B branch)**: record anchors (binary address + hooked mechanism) in
PERK_SURVEY.md + one PORT_GAPS.md note under 5.1 scope; add exactly ONE
`[ORIGINAL] perks: trigger-system effects not ported (5.1) — see PERK_SURVEY.md` TODO at the
aggregation plug point.
**Verify**: verdict table covers every perk family found in Step 3; each Case A mapping has a
recorded GREEN verdict; full build compiles; `ctest --test-dir build -R "^test_damage_wiring$"
--output-on-failure` green — CRITICAL: the Step 1 pins (esp. empty inventory → all zero, and
perk-less items → listed sums) must still pass, i.e. items without Case A perks contribute
exactly nothing.

### Step 6 — Perk wiring verification test (Case A) [AMENDMENT]
**File**: `tests/test_damage_wiring.cpp` (extend)
**Task**: add ≥1 case where an equipped perk-bearing item changes the computed damage:
aggregated AttributeSet differs by exactly the Step 5 verified mapping, and
`get_total_damage` output matches the value the ORIGINAL produces (mapping value plugged
into the verified formula by hand; order base*f2*f1*f3*add untouched). If the fork produced
ZERO Case A mechanisms, this step is a documented no-op: the test file gains a comment citing
the Step 5 verdict table, and the perk Success Criterion is marked N/A-with-evidence (never
silently dropped).
**Verify**: `cmake --build build --config Release --target test_damage_wiring` &&
`ctest --test-dir build -R "^test_damage_wiring$" --output-on-failure` — green; golden gates green.

### Step 7 — Game::rebuild_fighter_attributes + call sites (was Step 3)
**Files**: `engine/game/game.cpp` (~L609-611 load path; host_equip_item L949-990;
host_unequip_item ~L992-1005), `engine/game/game_clean.hpp` (declaration ~L457)
**Task**: Implement `rebuild_fighter_attributes()` = player aggregate (via Step 1 function,
`list_data_` + `inventory_`, INCLUDING the Step 5 perk plug-point outcome — Case A
contributions flow through aggregation; Case B is the single anchored TODO) →
`player_fighter_.attributes`; enemy baseline → `enemy_fighter_.attributes`. Call it: after
`sync_equipped_weapon()` in the load path, and in each equip/unequip success path. Log one
`[equip] attributes: ...` line via `dump()`-style output at verbose sites only (no spam per
frame).
**Verify**: full build (`cmake --build build --config Release`) compiles;
`ctest --test-dir build -R "^test_(inventory|shop_integration|save_system)$" --output-on-failure` — green
(equip/save paths exercised).

### Step 8 — Rewire sites 1+2 (enemy→player) (was Step 4)
**File**: `engine/game/game.cpp` (L1832-1858 blocked; L1860-~1905 unblocked)
**Task**: Replace the placeholder block per the mapping table. Both sites are near-duplicates;
extract a file-local `static float enemy_damage_to_player(float base_damage, bool blocked,
const AttributeSet& enemy_attrs, const AttributeSet& player_attrs, const DamageSettings&)`
helper directly above the first site so the two paths cannot diverge. Delete
`attribute_multiplier`/`attack_factor`/`factor_set_multiplier` locals and BOTH `* 2.0f`.
Keep the existing base_damage lookup (HighPunch + average_base_damage fallback), health
application (`final_damage * player_fighter_.max_health`), prints, stun/invuln/flash/sparks
verbatim. `[ORIGINAL]` header comment citing game+0x4527B4 (formula), game+0x60DF98 (pairing),
game+0x438530 (block-at-call-site evidence); `[HEURISTIC-TODO]` on disabled f1/f2/crit.
**Verify**: `cmake --build build --config Release` compiles; golden gates green (formula
untouched); `ctest --test-dir build -R "^test_(full_battle|battle_integration|trace_replay)$"
--output-on-failure` — green OR fails only on absolute damage values (then see Step 10 note).

### Step 9 — Rewire site 3 (player→enemy) + explicit ×2.0f deletion + dbg overlay (was Step 5)
**File**: `engine/game/game.cpp` (L3631-3700)
**Task**: Same mapping per table (weapon-aware dmg-attr selection on `equipped_weapon_`).
**EXPLICIT DELETION**: the `* 2.0f` tail of L3677 `raw_damage = ...` line, together with the
`raw_damage`/`final_damage` two-liner itself — replaced by `float final_damage =
get_total_damage(din) * block_factor;`. Preserve: ignores_block override print, base_damage
fallback, health application, combo/tutorial logic. Remap dbg fields (declared
game_clean.hpp ~L4574): `dbg_last_attr_mult_` := `attribute_difference_factor(din.attribute_difference)`,
`dbg_last_block_factor_` := block_factor, `dbg_last_attack_factor_`/`dbg_last_crit_factor_`/
`dbg_last_factor_set_` := 1.0f with comment (terms disabled); overlay render at
game_clean.hpp L1709-1711 keeps compiling unchanged.
**Verify**: full build compiles; `ctest --test-dir build -R "^test_(full_battle|battle_integration)$"
--output-on-failure` — green; golden gates green.

### Step 10 — Battle-level damage expectation test (was Step 6)
**File**: `tests/integration/test_full_battle.cpp` (extend; HeadlessTestRunner from phase 3)
**Task**: Add a check that a landed player hit reduces enemy health by
`get_total_damage(predicted_inputs) * block * enemy.max_health` within float epsilon, where
predicted_inputs are derived from the same aggregated AttributeSets the battle used (asserts
the WIRING, not the formula — formula is already golden-pinned). Discovery sub-bullet: if the
runner cannot land a deterministic hit, assert against the `dbg_last_*` breakdown values
instead; if an existing assertion in this file hardcodes old (×2.0) damage numbers, update the
expected value with a comment citing this plan ("×2.0 was the double-counted power base").
**Verify**: `cmake --build build --config Release --target test_full_battle` &&
`ctest --test-dir build -R "^test_full_battle$" --output-on-failure` — green.

### Step 11 — Full gate (was Step 7)
**Task**: `cmake --build build --config Release` (all targets) &&
`ctest --test-dir build --output-on-failure` — entire suite green, WILL_FAIL properties
unchanged. Manual smoke note: one headless battle log line showing
`[COMBAT] Player hit enemy: ... final=...` consistent with the attribute dump.
PERK_SURVEY.md (with verdict table + re-verifier verdicts) committed.
**Verify**: suite exit 0.

## Success Criteria

- [ ] No occurrence of `damage_factor_base * 0.0f` or `* 2.0f` remains in game.cpp damage paths (grep gate)
- [ ] All three sites call `get_total_damage`; block applied only as call-site post-multiplier
- [ ] Equipping a weapon with WeaponDamage 22 vs baseline enemy doubles hit damage vs WeaponDamage 12 (test_damage_wiring + battle test)
- [ ] PERK_SURVEY.md committed: schema + reference mechanism + binary anchors (parser & apply path) + fork verdict table
- [ ] Every Case A perk mapping has a recorded @re-verifier GREEN verdict (max-3-round rule honored); Case B mechanisms have anchors + the single `[ORIGINAL]` TODO
- [ ] test_damage_wiring contains a perk-bearing equipment case matching the original's numbers (Case A), or an explicit N/A comment citing the verdict table (pure Case B outcome)
- [ ] No `perk effect mapping not yet recovered` TODO remains — superseded by PERK_SURVEY anchors (grep gate)
- [ ] `test_damage_formula_golden` + `test_attributes_golden` green (ports untouched)
- [ ] Full `ctest` green; build has zero new warnings
- [ ] damage_formula.hpp / attributes.hpp byte-identical to pre-task state (git diff empty)

## Test Plan

| Step | Test type | File/target |
|---|---|---|
| 1 | Unit (new) | test_damage_wiring |
| 2 | Unit (existing) | test_fighter_states |
| 3-4 | RE survey (documentation) | reverse/analysis/PERK_SURVEY.md |
| 5 | RE verification gate + unit | @re-verifier verdicts (recorded); test_damage_wiring stays green |
| 6 | Unit (extended) | test_damage_wiring (perk case) |
| 7 | Integration (existing) | test_inventory, test_shop_integration, test_save_system |
| 8-9 | Integration (existing) | test_full_battle, test_battle_integration, test_trace_replay |
| 10 | Integration (extended) | test_full_battle |
| 11 | Full suite | ctest (all) |
| all | Golden gates | test_damage_formula_golden, test_attributes_golden |

## Rollback Plan

All changes are confined to: game.cpp (3 sites + rebuild fn + includes), types.hpp (1 member +
1 include), game_clean.hpp (1 declaration), 3 new files (attribute_aggregation.hpp,
test_damage_wiring.cpp, PERK_SURVEY.md), one possible PORT_GAPS.md note, tests. No
save-format, schema, or asset changes; AttributeSet is runtime-only.
1. `git revert <wiring-commit>` (single commit for steps 2, 7-10; step 1 may be its own
   commit; perk workstream steps 5-6 should be their own commit) — restores the linear model
   exactly.
2. Verify rollback: golden tests + full ctest green (they never depend on the wiring).
3. If only site-3 behavior is suspect mid-review, the deletion of `* 2.0f` is independently
   revertible (single-line hunk), but MUST NOT be re-added without reinstating the linear
   model — the two double-count.
4. New files (attribute_aggregation.hpp, test_damage_wiring.cpp) are additive — safe to keep
   even after a revert.
5. Perk workstream (amendment): survey docs are pure documentation — always safe to keep.
   Case A wiring is one contribution function + one call site + one test — independently
   revertible; deleting it returns perks to neutral WITHOUT touching the formula or the three
   damage sites. Case B is comment-only. If a re-verifier round fails irreconcilably, the
   fallback IS the Case B anchor (documented, no code), never an unverified wire.

## References

- reverse/analysis/PORT_GAPS.md — GAP-3 (tracer order, AlignTargetAttributes, priority list);
  L417-477 attribute model + perk/expression evaluator string vocabulary
- reverse/analysis/PORT_PLAN.md §3/§8, method notes L301-325, session log 2026-07-30
- reverse/analysis/RUNTIME_MAP.md — relocated dump usage (base 0x8F057000, ARM:LE:32:v7)
- reverse/analysis/BLOCK_LOGIC.md §3.7 — Condition system class table (ConditionPerk et al.)
- assets/files/assets/perks.xml — trigger-script perk definitions (3373 lines)
- engine/format/list_parser.hpp L29-33 — ListPerk{name, params}; stage_parser.hpp L58-59/123 — warrior perks
- engine/game/damage_formula.hpp — verified disassembly structure (game+0x4527B4 header comment)
- engine/game/attributes.hpp — getParameter shape (game+0x6275F4), sentinel contract

# Perk data survey — perks.xml schema & reference mechanism (Step 3, assets only)

Scope: phase-4 PLAN step 3. Data-only survey of the shipped perk system. NO binary
work (that is Step 4) and NO engine changes. Every claim below is backed by a
verbatim XML excerpt or a mechanical count over the shipped files; the only
speculation lives in the clearly-labeled A/B hypothesis column of §5 and in §7.

Method: all counts produced by parsing the XML (`[xml]` DOM walk) or by regex over
the raw text; both roots of the duplicated asset tree were checked where relevant.

---

## 0. TL;DR for the Step 4 binary survey

1. **perks.xml is a trigger-script system, not a param list.** 142 `<Perk>`
   entries; 78 carry their own `<Trigger>` blocks, 58 are template children that
   only override `<Set>` values, 6 are bare Name+Image move-unlock tokens.
2. **The engine never loads perks.xml.** Zero references to `perks.xml` in
   `engine/`. `ListPerk` (list.xml) is parsed into `ListItem.perks/.enchantments`
   and then *never read again*; stage warrior perks are parsed as bare strings.
3. **No perk writes any of the 7 tracer attributes** (WeaponDamage …
   RangedQuantity). Perk attribute writes target `DamageFactor` (30×),
   `RegenerationRate` (12×), `Lifesteal` (11×), `MagicDamageRecharge` (3×),
   `CriticalDamage` (2×), `MagicPainRecharge`, `CriticalChance`,
   `ShockHeadHitChance` (1× each) — always exactly one attribute per
   `<ModAttributes>` action, and **54 of 61 carry a `Frames` countdown** (usually
   `Frames="1"`). Structurally every attribute write is event-gated → the data
   leans heavily Case B; the binary decides (Step 4).

---

## 1. Files, duplicates, and engine load conventions

| file | bytes | role |
|---|---|---|
| `assets/files/assets/perks.xml` | 131 509 (3373 lines) | canonical shipped perk definitions |
| `assets/perks.xml` | 131 509 | **byte-identical duplicate** (MD5 `FEDD559F672713B34ADC274B2AB827CA`, both copies) |

The same duplication exists for `list.xml` and `stages.xml` (both pairs verified
MD5-identical 2026-07-31). Engine conventions resolve either copy depending on
`asset_root`: tests set `config.asset_root = "assets"` and the loaders probe
`asset_root/assets/<name>.xml` first, then `asset_root/<name>.xml`
(`game.cpp` L331-346 for list.xml) or `assets/stages.xml` then
`assets/files/assets/stages.xml` (`asset_manager.cpp` L1352-1355). For
perks.xml this is moot — **no engine code path loads it** (grep over `engine/`
for `perks.xml`: 0 hits).

Files that *reference* perk names (all under the same duplicated root):

| file | reference form | refs | distinct names | all resolve to `Perk@Name`? |
|---|---|---|---|---|
| `list.xml` | `<Item><Enchantments>/<Perks><Perk Name=…><Set …/></Perk>` | 128 nodes | 45 | yes (0 unresolved) |
| `stages.xml` | `<Warrior><Perks><Perk Name=…/>` | 853 nodes | 94 | yes (0 unresolved) |
| `CharacterProgress.xml` | `<PerkTree><Level><Perk/Upgrade Name=…/>`, `<Perks><Perk><UpgradeLevel><Set …/>` | 166 nodes | 24 | yes |
| `animations/moves.xml` | `<Move><Locks><Perk Name=…/>` / `<Item Perk=…/>` | 38 nodes | 21 | yes |
| `forge.xml` | `<Variation><Enchantments><Perk Name=…><Set Aspect=…/>` | 4 active (+1 commented out) | 4 | yes |

39 perk names are not referenced *by Name* from any XML; of these, 26 are
template parents reachable via `Template=` (e.g. `PERK_ITEM_SPECIAL_LIFESTEAL`
is the parent of the referenced `…_WEAPON` child) and 13 are referenced nowhere
in XML at all (boss/rules perks like `PERK_BOSS_HERMIT_MAGIC`,
`PERK_BOSS_SHOGUN_FRENZY`, `PERK_WEAPON_BLOCK_BREAKER`,
`PERK_GREATER_GOOD_SHARPENING`, `PERK_SPHERE_COOLDOWN`,
`PERK_ITEM_SPECIAL_CRITICAL_CHANCE`, `PERK_ITEM_SPECIAL_SHOCK` — presumably
attached or applied directly by binary code; Step 4 xref targets). See §3.7.

---

## 2. perks.xml schema

### 2.1 Root and `<Perk>` element

Root is `<Perks>` (line 1). Each entry is a `<Perk>`; observed attributes with
occurrence counts (142 perks total):

| attribute | count | meaning (from usage) |
|---|---|---|
| `Name` | 142 | unique id, referenced from other XML by this string |
| `Template` | 126 | parent perk name — inherits triggers, `<Set>` is merged/overridden |
| `Image` | 64 | icon asset id |
| `Hidden` | 35 | `1` = not shown in UI bars |
| `Alias` | 27 | localization key (e.g. `ENCHANTMENT_LIFESTEAL`) |
| `Description` | 24 | localization key with `{…}` substitutions, e.g. `Description="DESC_ENCHANTMENT_SHIELDING{75}{_Frames / 60}"` |
| `BarSetAttribute` | 23 | always `Aspect` — which Set param the UI bar displays |
| `BarScale` | 23 | always `Enchantment` |
| `BarShift` | 10 | e.g. `BarShift="45"` |
| `ItemSet` | 4 | mythic set id (`MONK_SET`, `SENTINEL_SET`, `DRAGON_SET`, `HEAVEN_SET`) |
| `PerkType` | 4 | `Combo` on exactly the 4 set perks |

### 2.2 `<Set>` params and `_Param` substitution

A perk-level `<Set key="value" …/>` (127 occurrences; exactly ONE more `<Set>`
appears as an *action*, §2.4) declares named parameters. Bodies reference them
with an underscore prefix: `_Frames` substitutes the `Frames` Set value, etc.
Example — `PERK_BEGINNER` (L13-26):

```xml
<Perk Name="PERK_BEGINNER" Template="EndStanceClear">
  <Set Health="0.4"/>
  <Trigger>
    <Events>
      <HitPreCrit Player="Me" Block="1"/>
    </Events>
    <Conditions>
      <LessEqual Value1="?PlayerParameter[Enemy].Health" Value2="_Health"/>
    </Conditions>
    <Actions>
      <SetHit Block="0" />
    </Actions>
  </Trigger>
</Perk>
```

A perk may carry **multiple `<Set>` blocks** — they merge (PERK_ITEM_SPECIAL_
PRECISION, L1136-1137):

```xml
<Set Aspect="0" Chance="0.2" DamageFactor="10000" Animation="Weapon" Frames="90"/>
<Set Aspect="?RandomAspect[-30,30]" Animation="Weapon" DamageRating="Weapon"/>
```

Full Set-key vocabulary (distinct keys, count in parentheses; 127 perk-level
Sets): Frames(60) Animation(51) Chance(48) MultiplierRating(36) Defense(30)
Aspect(24) DamageRating(23) Base(22) DamageFactor(20) ChanceFactor(19)
ItemAmplification(18) FramesBetweenHits(18) DefenseRating(12) FlagName(12)
AnimationName(10) Health(6) BaseDamage(5) HitDamageIncrease(5) DamageFlagName(4)
Step(4) Button(2) RegenerationRate(2) AverageBaseDamage(2) Lifesteal(2)
InitialFrames(2) AttributeRegeneration(2) AttributeBleeding(2) and one each of:
Limit, Limit1, Limit2, Limit3, Factor, StepForward, StepBack, Distance, Charge,
Cooldown, Damage, DamageReturn, HealthLimit, Frames2, FramesBase, FramesRange,
Frames_quake, Frames_prequake, InitialFrames, LagFrames, NegateFrames,
**NegateFarmes** (sic — shipped typo on PERK_BOSS_SHOGUN_FRENZY; the WIDOW
perk spells it `NegateFrames`), Animation2, DefeatAnimation, BlockBreakChance,
AttributeDamageFactor, MagicDamageRecharge, Chance2.

Note what Set keys are NOT: none of the 7 tracer attribute names appears as a
Set key. `Defense="BodyDefense"` / `"HeadDefense"` (30×) is a *selector*
(which defense attribute a trigger/Rating consults), not a value assignment.

### 2.3 Template inheritance

`Template="<parent>"` gives the child all of the parent's triggers; the child's
own `<Set>` overrides individual parameter values. Prototype example —
`PERK_ITEM_SPECIAL_LIFESTEAL` parent vs its referenced child (L1175-1177 shape):

```xml
<Perk Name="PERK_ITEM_SPECIAL_PRECISION_WEAPON" Template="PERK_ITEM_SPECIAL_PRECISION" >
  <Set Animation="Weapon" DamageRating="Weapon" />
</Perk>
```

`EndStanceClear` (L2-11) is the root template for 126 perks — a single trigger
that clears all mods at the start of the `EndStance` round stage:

```xml
<Perk Name="EndStanceClear">
  <Trigger>
    <Events>
      <RoundStageStart Name="EndStance" />
    </Events>
    <Actions>
      <ClearMods />
    </Actions>
  </Trigger>
</Perk>
```

Template chains can be two levels deep (e.g. `PERK_ITEM_SPECIAL_FRENZY_DEFENSE_
HELM` → `PERK_ITEM_SPECIAL_FRENZY_DEFENSE` → `EndStanceClear`). 58 perks have a
Template and zero triggers of their own — they exist purely to re-parameterize
the parent. Whether the merge is done by the parser or at runtime is a Step 4
question.

### 2.4 Trigger blocks

222 `<Trigger>` elements across 78 perks. Anatomy: optional `Name` (21×),
optional `<Events>` (206 present → 16 triggers are event-less and fire only via
`<Provoke Trigger="…"/>`), optional `<Conditions>` (194 present, 2 of them
empty `<Conditions/>`), then `<Actions>` (222 — every trigger has one).

Canonical suspect, listed in full per the plan — `PERK_HELM_BREAKER` (L30-63):

```xml
<Perk Name="PERK_HELM_BREAKER" Template="EndStanceClear" Image="IconHelmBreaker">
  <Set DamageFactor="5850" Chance="0.2" Frames="300" Defense="HeadDefense" />
  <Trigger>
    <Events>
      <HitPreCrit Player="Enemy" Defense="_Defense" Block="0"/>
    </Events>
    <Conditions>
      <ModExists Name="Blocker" Not="1" />
    </Conditions>
    <Actions>
      <ModFlag Name="Blocker" Frames="_Frames" />
      <Provoke Trigger="Slave" />
    </Actions>
  </Trigger>
  <Trigger Name="Slave">
    <Conditions>
      <Random Chance="_Chance" />
    </Conditions>
    <Actions>
      <ModIcon Name="Icon" Frames="_Frames" ShowExpiration="1" Image="IconHelmBreaker_Red" Player="Enemy"/>
    </Actions>
  </Trigger>
  <Trigger>
    <Events>
      <HitPostCrit Player="Enemy" Defense="_Defense"/>
    </Events>
    <Conditions>
      <ModExists Name="Icon" />
    </Conditions>
    <Actions>
      <ModAttributes DamageFactor="_DamageFactor" Frames="1" />
    </Actions>
  </Trigger>
</Perk>
```

Reading: when the *player* lands an unblocked hit on the enemy's HeadDefense
zone, flag `Blocker` for 300 frames and roll 20%; on success show an icon; while
the icon lives, any follow-up hit gets a `ModAttributes DamageFactor=5850`
mod **with `Frames="1"`** — i.e. frame-scoped, applied during that hit's
evaluation. Per the plan's fork criteria this is **Case B** (Mod* action with a
Frames countdown, Condition-gated, event-subscribed). The binary confirms or
refutes in Step 4.

Named "slave" triggers fired by `<Provoke Trigger="Slave"/>` have no `<Events>`
(example above). Multiple triggers may share one name (two `Slave` triggers in
PERK_ITEM_SPECIAL_PRECISION, L1156-1172 — opposite `ModExists … Not` guards).

The single `<Set>`-as-action occurrence — `PERK_FULL_POWER` (L351-362):

```xml
<Trigger>
  <Events>
    <HitPreCrit Player="Enemy" Animation="_Animation"/>
  </Events>
  <Conditions>
    <ModExists Name="Icon"/>
  </Conditions>
  <Actions>
    <Set DamageFactor="_DamageFactor" Frames="1"/>
    <ClearMods Name="Icon"/>
  </Actions>
</Trigger>
```

(Elsewhere `<Set>` is a perk-level param block; here it takes the same shape as
a `ModAttributes` action. Whether the binary treats action-`<Set>` and
`ModAttributes` identically is a Step 4 question.)

### 2.5 Complete element vocabulary (grep-count cross-check)

54 distinct element names including the root (53 below it) — 100% coverage:

| kind | elements (count) |
|---|---|
| structural | Perks(root) · Perk(142) · Set(128) · Trigger(222) · Events(206) · Conditions(194) · Actions(222) · RatingEvaluation(21) · Rating(39) |
| events (11) | PostHit(65) HitPreCrit(53) HitPostCrit(48) RoundStageStart(21) ModExpires(18) AnimationStart(15) EveryFrame(7) AreaEnter(4) AreaExit(4) MagicCharged(2) AnimationEnd(2) |
| conditions (17) | ModExists(159) Random(70) CurrentAnimation(23) GreaterEqual(16) Operator(16) RoundStage(13) Less(11) Equal(7) Greater(4) Health(3) InTheArea(3) CurrentInterval(2) Round(2) LessEqual(1) Item(1) Bullets(1) PerkStart(20; 19 direct + 1 nested in Operator) |
| actions (18) | ModAttributes(61) ModIcon(58) ModFlag(55) SetVariable(43) ClearMods(37) SetModFrames(32) ModVariable(29) ApplyModEffect(27) SetHit(22) Provoke(10) SetTactic(5) ChangeImpulse(4) SetCooldown(3) Set(1) ChangeAdditionalDamageValue(1) AddMagicCharge(1) DisableInterval(1) ModInvisibility(1) |

Roles are exclusive: no element appears in more than one of Events/Conditions/
Actions (verified by context walk). `Operator Type="Or|And"` wraps other
conditions (PERK_ITEM_SPECIAL_SHIELDING, L911-914):

```xml
<Operator Type="Or">
  <ModExists Name="IconShielding" Not="1"/>
  <ModExists Name="Flag"/>
</Operator>
```

### 2.6 Events / Conditions / Actions — attribute vocabularies

Event filter attributes: `Player="Me|Enemy"`, `Animation="Weapon|Unarmed|
RangedMissile|RangedMissilePerk|MagicMissile|Bomb|RuleEarthquakeHit|_Animation"`,
`Block="0|1"`, `Critical="0|1"`, `Defense="BodyDefense|HeadDefense|_Defense"`,
`Shock`, `Name` (for RoundStageStart/AnimationStart/AnimationEnd/ModExpires),
`Frames` (once, on HitPostCrit), `Step` (EveryFrame), `Number` (Round).

`ModExists`/`ModFlag`/`ModIcon` support `Namespace="Titans_namespace"` (5
occurrences, all in the Titan fight perks).

`ModAttributes` (the attribute-writing action — 61 occurrences): each instance
carries optional `Name` (mod instance id, 28×), optional `Player` (37×:
Me 12 / Enemy 25 / absent 24), `Frames` (54×) and **exactly one** attribute
key. Attribute keys written across the whole file:

| attribute written | occurrences | example source |
|---|---|---|
| `DamageFactor` | 30 | `_DamageFactor` (17), `_Base` (7), `-100000` (4), `_Damage` (1), `_AttributeDamageFactor` (1) |
| `RegenerationRate` | 12 | bleeding/regeneration family |
| `Lifesteal` | 11 | lifesteal/damage-return/bloodrage family |
| `MagicDamageRecharge` | 3 | martial spirit, magic-recharge enchant |
| `CriticalDamage` | 2 | critical-protection enchant |
| `MagicPainRecharge` | 1 | |
| `CriticalChance` | 1 | PERK_ITEM_SPECIAL_CRITICAL_CHANCE |
| `ShockHeadHitChance` | 1 | PERK_ITEM_SPECIAL_SHOCK |

`SetHit` edits the in-flight hit, not attributes: `Damage(9) Block(7)
Critical(7) Disarm(1) Shock(1)`.

### 2.7 `<RatingEvaluation>` / `<Rating>`

21 perk-level blocks, 39 `<Rating>` rows, only on `PERK_ITEM_SPECIAL_*`
enchantment parents. Every Rating has `EnemyAttribute="EnchantmentResistance"`,
plus `Player`, `Defense="BodyDefense|HeadDefense"` (38 of 39), `Multiplier`,
and optionally `Damage="Weapon|Ranged|…"` (24×). Example (L721-724):

```xml
<RatingEvaluation>
  <Rating Player="Me" Damage="_DamageRating" Defense="BodyDefense" Multiplier="_MultiplierRating" EnemyAttribute="EnchantmentResistance"/>
  <Rating Player="Me" Damage="_DamageRating" Defense="HeadDefense" Multiplier="_MultiplierRating" EnemyAttribute="EnchantmentResistance"/>
</RatingEvaluation>
```

This is the enchantment-strength-vs-resistance computation; it *reads* the
enemy's `EnchantmentResistance` attribute (the same name as in
`AlignTargetAttributes`) — how the read resolves is a Step 4 question.

### 2.8 Expression language

Conditions/actions embed an expression syntax beyond plain `_Param`:

| form | example (verbatim) | where |
|---|---|---|
| `?PlayerParameter[X].Y` | `?PlayerParameter[Enemy].Health` | PERK_BEGINNER L20 |
| `?PlayerAttribute[X].Y` | `?PlayerAttribute[Enemy].EnchantmentResistance` | lifesteal Chance expr L730 |
| `?Aspect[expr]` | `?Aspect[_Aspect * ( 1 - ?CurrentFight[].isRaid * ?PlayerParameter[Me].isPlayer ) + ?PlayerParameter[Me].DefaultPerksAspect * ?CurrentFight[].isRaid * ?PlayerParameter[Me].isPlayer - ?PlayerAttribute[Enemy].EnchantmentResistance]` | PERK_ITEM_SPECIAL_CRITICAL_CHANCE L713 |
| `?CurrentFight[].Y` | inside `?Aspect[…]` above | same |
| `?Variable[Name]` | `?Variable[RegularHit]+?Hit[].Damage` | PERK_ITEM_SPECIAL_SHIELDING L917 |
| `?Hit[].Y` | `?Hit[].Damage`, `?Hit[].BaseDamage` | SHIELDING L917/931, SHROUD_FALL L2599 |
| `?RandomAspect[min,max]` | `Aspect="?RandomAspect[-30,30]"` | PRECISION L1137, forge.xml variations |

These names (`PlayerAttribute`, `PlayerParameter`, …) match the expression-
evaluator string cluster already located near the attribute code in the binary
(PORT_GAPS.md L466-477) — the Step 4 anchor region.

`SetVariable` (43×) vs `ModVariable` (29×): both `Name`+`Value`; Set* persists,
Mod* is frame/mod-lifetime scoped (same naming convention as ModFlag/ModIcon/
ModAttributes vs SetHit/SetCooldown). Semantics to be confirmed in the binary.

### 2.9 `Frames` attributes

`Frames` appears on: Set(60, as a param), ModAttributes(54), ModIcon(50),
ModFlag(44), SetModFrames(32), SetCooldown(3), ChangeAdditionalDamageValue(1),
HitPostCrit(1). With the engine's integer 16 ms frame (PORT_GAPS.md GAP-1),
`Frames="300"` ≈ 5 s, `Frames="1"` = exactly the current frame — i.e. scoped to
the hit being evaluated right now.

---

## 3. Reference mechanism — who points at `Perk@Name`

### 3.1 list.xml items (player equipment → enchantments/perks)

Two containers per `<Item>`: `<Enchantments>` (126 refs on 125 items) and
`<Perks>` (2 refs on 2 items). `<Set>` children give per-item parameter
overrides. Verbatim:

```xml
<Item Name="WEAPON_HW15_BROOM" Image="weapon_hw15_broom" Model="weapon_hw14_broom" Type="Weapon" Level="1" UpgradeLevel="100" ShopHide="1" SubType="Staff" PaidItem="Paid"><Upgrades Template="Paid_Weapon_Bonus" /><Enchantments><Perk Name="PERK_ITEM_SPECIAL_BLOODRAGE_WEAPON" /></Enchantments></Item>
```

```xml
…<Perks><Perk Name="PERK_MINE_PLAYER"><Set Frames="480" /></Perk></Perks><Enchantments><Perk Name="PERK_ITEM_SPECIAL_ENFEEBLE_RANGED"><Set Aspect="1692" /></Perk></Enchantments></Item>   (RANGED_SUPER_MINE)
```

```xml
<Item Name="MAGIC_MIND_THROW" … Type="Magic" SubType="MindThrow" …><Perks><Perk Name="MindThrow" /></Perks></Item>
```

Item-slot spread of list.xml references: Weapon / Ranged / Magic / Helm / Armor
per the per-perk table in §5 (e.g. all `…_WEAPON` children on `Type="Weapon"`
items, `…_HELM` on helms, `…_ARMOR` on armor).

### 3.2 stages.xml warriors (enemy side)

853 `<Perk Name=…/>` refs, 94 distinct, on `<Warrior>` entries (verbatim,
stages.xml L54-64):

```xml
<Warrior Template="Man_Kungfu" BeginnerCheat="1" Tactic="Beginner" WeaponDamage="2" UnarmedDamage="2" BodyDefense="2" HeadDefense="-3" CriticalChance="0">
  <AttributesAlign>
    <Delta Factor="0" Shift="0" Priority="1"/>
    <Delta Factor="1" Shift="-10" Priority="1"/>
  </AttributesAlign>
  <Perks>
    <Perk Name="PERK_BEGINNER"/>
  </Perks>
</Warrior>
```

Note warriors carry their own attribute attributes (`WeaponDamage=…
CriticalChance=…`) plus `<AttributesAlign><Delta Factor Shift Priority/>`
blocks adjacent to `<Perks>` — enemy attributes and enemy perks live in the
same XML node. Stage `<Fight><Rules>` can also *remove* a perk:
`<NoPerks Name="EndStanceClear"/>` (2 occurrences).

### 3.3 CharacterProgress.xml (player level-up + perk upgrades)

Root `<Progress>` children: Thresholds, **StartingAttributes**,
LevelAttributeGain, PerkTree, Perks, MoneyBaseValues, CurrencyBaseValues.
The player's base attribute block (verbatim):

```xml
<StartingAttributes WeaponDamage="5" UnarmedDamage="5" BodyDefense="5" HeadDefense="0" RangedDamage="0" MagicDamage="0" MagicPainRecharge="-10000" MagicDamageRecharge="0" CriticalChance="1000" CriticalDamage="5850" BlockDamageFactor="-23219" InitialMagicCharge="10000" ShockCriticalHitChance="0" ShockHeadHitChance="2500" EnchantmentResistance="0" />
<LevelAttributeGain WeaponDamage="10" UnarmedDamage="10" BodyDefense="10" HeadDefense="10" RangedDamage="10" MagicDamage="10"/>
```

`<PerkTree>` offers perks per level (166 `<Perk>`/`<Upgrade>` nodes, 24
distinct — the classic level-up set: HELM_BREAKER, AVENGER, COBRA, …). The
`<Perks>` block carries **per-upgrade-level Set overrides** (verbatim, trimmed):

```xml
<Perk Name="PERK_HELM_BREAKER"><UpgradeLevel Value="1" Description="PERKDESCRIPTION_HELM_BREAKER{15}{35}{5}"><Set DamageFactor="4330" Chance="0.15" /></UpgradeLevel><UpgradeLevel Value="2" …><Set DamageFactor="4542" Chance="0.175" /></UpgradeLevel>…
```

So the same `Perk@Name` is re-parameterized by the player's perk upgrade level
— a second source of `_Param` values after the perk's own `<Set>` defaults.

### 3.4 moves.xml Locks (move gating)

694 `<Locks>` blocks in moves.xml; 38 reference perks (21 distinct names),
gating moves behind perk ownership (verbatim, PERK_DOUBLE_SWEEP lock):

```xml
<Locks>
  <Perk Name="PERK_DOUBLE_SWEEP"/>
  <Item Type="Weapon" SubType="Katana" Not="1"/>
</Locks>
```

This is what the 6 bare perks (PERK_DOUBLE_SWEEP, PERK_DOUBLE_JUMP_KICK,
PERK_ELBOW_STRIKE, PERK_TWO_FOOT_JUMP_KICK, PERK_BACK_FLIP_KICK, PERK_SUPLEX)
exist for — they are capability tokens, not effect scripts.

### 3.5 forge.xml (enchantment reroll pool)

`<Forge>` → 3 `<Recipe>` blocks. Each recipe has `<Items>` (per-type pool
config) and `<Variation>` entries: `<Conditions><Item Type="Weapon"/>`
selects the slot, `<Enchantments><Perk Name=…><Set Aspect="?RandomAspect[-30,30]"/>` assigns a random-aspect enchantment. The 4 mythic set perks
(MONK/SENTINEL/DRAGON/HEAVEN) are forge-only variations; a fifth,
`PERK_REVIVAL_SET_SUPER_REGEN`, is present but **commented out** and has no
perks.xml entry at all.

### 3.6 Engine parser status (what reSF2 reads today)

| consumer | status |
|---|---|
| `engine/format/list_parser.cpp` L67-77 `parse_perk` | reads `Perk@Name` + `<Set>` attrs into `ListPerk{name, params[]}` (alternating key/value strings); containers split into `ListItem.perks` / `ListItem.enchantments` (L104-126) |
| downstream use of `ListItem.perks/.enchantments` | **none** — grep over `engine/`: the fields are written by the parser and never read |
| `engine/format/stage_parser.cpp` L79-83, L240-245 | warrior/tournament `<Perks>` read as bare name strings into `StageWarrior.perks`; `<NoPerks>` noted in stage_parser.hpp L71 |
| moves `<Locks><Perk Name=…>` | parsed into `MoveDef.required_perk` (`asset_manager.cpp` L776-784, types.hpp L139) but **never consulted** in move gating (`game.cpp` L2412-2431 checks direction/weapon/subtype/animation only) |
| perks.xml itself | **no loader exists** in `engine/` |

### 3.7 Reference resolution audit

- list.xml → perks.xml: 128/128 resolve, 0 unresolved.
- stages.xml → perks.xml: 853/853 resolve, 0 unresolved.
- CharacterProgress.xml → perks.xml: all 24 distinct resolve.
- moves.xml Locks → perks.xml: all 21 distinct resolve.
- forge.xml: 4 active set-perk refs resolve; the commented-out REVIVAL perk has
  no definition (dead content).
- 13 perks are referenced from NO XML: `PERK_ITEM_SPECIAL_CRITICAL_CHANCE`,
  `PERK_ITEM_SPECIAL_SHOCK`, `PERK_BOSS_HERMIT_MAGIC`,
  `PERK_BOSS_BUTCHER_BLEEDING`, `PERK_WEAPON_BLOCK_BREAKER`,
  `PERK_GREATER_GOOD_SHARPENING`, `PERK_BOSS_WASP_DAMAGE_RETURN`,
  `PERK_BOSS_WASP_REGENERATION`, `PERK_BOSS_WIDOW_LIFESTEAL`,
  `PERK_BOSS_WIDOW_SHIELDING`, `PERK_BOSS_SHOGUN_FRENZY`, `PERK_SPHERE_COOLDOWN`,
  `EndStanceClear` (root template — reachable only via `Template=`). Boss/rule
  perks are presumably attached by binary code directly; xref candidates for
  Step 4.

---

## 4. Attribute-name cross-reference

Occurrences of the damage-formula attribute vocabulary inside perks.xml
(exact-token, attribute-name or attribute-value position):

| name | hits | where |
|---|---|---|
| `WeaponDamage` | **0** | — |
| `UnarmedDamage` | **0** | — |
| `BodyDefense` | 35 | `Set@Defense` ×18, `Rating@Defense` ×17 (selector, never assigned) |
| `HeadDefense` | 29 | `Rating@Defense` ×16, `Set@Defense` ×11, `PostHit@Defense` ×2 |
| `RangedDamage` | **0** | — |
| `MagicDamage` | **0** exact (4 substring hits are all `MagicDamageRecharge`) | — |
| `RangedQuantity` | **0** | — |
| `DamageFactor` | 51 | `ModAttributes@DamageFactor` ×30, `Set@DamageFactor` ×20 (19 perk-level + the 1 action-level in PERK_FULL_POWER), +1 Set key `AttributeDamageFactor` |
| `CriticalChance` | 2 | `ModAttributes@CriticalChance` ×1 (L713); the 2nd line hit is `Image="EnchantmentCriticalChance"` (substring, not the attribute) |
| `CriticalDamage` | 2 | `ModAttributes@CriticalDamage` ×2 (L1316, L1329) |
| `BlockDamageFactor` | **0** | — |
| `EnchantmentResistance` | 69 | `Rating@EnemyAttribute` ×39 + 30 inside `?Aspect[… ?PlayerAttribute[Enemy].EnchantmentResistance]` expressions (read-only) |

Summary: perks **read** `BodyDefense`/`HeadDefense`/`EnchantmentResistance` as
selectors and **write** `DamageFactor`, `CriticalChance`, `CriticalDamage` (and
the non-formula params `RegenerationRate`, `Lifesteal`, `MagicDamageRecharge`,
`MagicPainRecharge`, `ShockHeadHitChance`). No perk ever writes the seven
tracer attributes directly.

`DamageFactor` magnitudes observed in Set params: `5850` (×6), `300000` (×2,
GREATER_RANGED/MAGIC_DAMAGE), `10000`, `-25850`, `-16000`, `50000`, `3219`,
`3785`, `4854`, `15850`, `-100000`, plus `_`-params. With `<DamageFactor
Base="0.0001">` from internalSettings.xml these map to exponents of ±0.16…±30
in the `2^(w·attr)` base term — add-vs-set semantics and any clamp are for the
binary to say (Step 4).

---

## 5. Per-perk table with A/B hypothesis

Hypothesis column (HYPOTHESIS ONLY — the Step 4 binary survey decides):
**A** = plausibly a persistent attribute contribution (writes character
attributes at equip time, no Frames countdown, no Condition gating).
**B** = plausibly a triggered/timed effect (subscribes triggers, evaluates
Conditions, Mod* action with Frames). `—` = no attribute path at all.

| perk | referenced from | triggers | events | hypothesis |
|---|---|---|---|---|
| EndStanceClear | tmpl-parent | 1 | RoundStageStart | B (icon/flag/hit-edit only, no attr writes) |
| PERK_BEGINNER | stage(x3) | 1 | HitPreCrit | B (icon/flag/hit-edit only, no attr writes) |
| PERK_DOUBLE_SWEEP | stage(x1); progress; moves | 0 | — | — (bare: move-gate, no attr path) |
| **PERK_HELM_BREAKER** | stage(x9); progress | 3 | HitPreCrit, HitPostCrit | **B — ModAttributes[DamageFactor], `Frames="1"`, Condition-gated (full XML in §2.4)** |
| PERK_DOUBLE_JUMP_KICK | stage(x1); progress; moves | 0 | — | — (bare: move-gate) |
| PERK_AVENGER | stage(x22); progress | 3 | HitPostCrit | B (ModAttributes[DamageFactor], all Frames-scoped) |
| PERK_DESPERATE | progress | 3 | EveryFrame, HitPreCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ELBOW_STRIKE | stage(x1); progress; moves | 0 | — | — (bare: move-gate) |
| PERK_MIRROR | stage(x4); progress | 3 | HitPreCrit, PostHit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_COBRA | stage(x6); progress | 3 | RoundStageStart, PostHit, HitPostCrit | B (icon/flag/hit-edit only) |
| PERK_TWO_FOOT_JUMP_KICK | stage(x1); progress; moves | 0 | — | — (bare: move-gate) |
| PERK_BLOCK_BREAKER | stage(x18); progress | 2 | HitPreCrit | B (SetHit Block edit only) |
| PERK_FURIOUS | progress | 3 | HitPostCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_BACK_FLIP_KICK | stage(x1); progress; moves | 0 | — | — (bare: move-gate) |
| PERK_PAIN_RAGE | stage(x8); progress | 2 | PostHit, HitPreCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_FULL_POWER | stage(x4); progress | 2 | MagicCharged, HitPreCrit | B (**Set-action** `DamageFactor Frames="1"` — sole Set-in-Actions; §2.4) |
| PERK_RICOCHET | stage(x18); progress | 2 | HitPostCrit | B (ModAttributes[DamageFactor] Player=Enemy, Frames-scoped) |
| PERK_STEEL_FOOT | stage(x10); progress | 4 | PostHit | B (ModFlag/icon only) |
| PERK_SUPLEX | progress; moves | 0 | — | — (bare: move-gate) |
| PERK_OVERCHARGE | progress | 4 | MagicCharged, HitPreCrit, HitPostCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_EAGLE_EYE | progress | 3 | AnimationStart, RoundStageStart, EveryFrame, HitPreCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_MARTIAL_SPIRIT | stage(x6); progress | 2 | HitPreCrit | B (ModAttributes[MagicDamageRecharge], Frames-scoped) |
| PERK_ROCK | stage(x4); progress | 2 | HitPostCrit | B (ModAttributes[Lifesteal], Frames-scoped) |
| PERK_CONCUSSION | stage(x4); progress | 1 | PostHit | B (ModFlag/icon only) |
| PERK_ENLIGHTENMENT | stage(x11); progress | 1 | PostHit | B (SetHit/ModFlag only) |
| PERK_DISORIENTATION | progress | 3 | HitPreCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_CRITICAL_CHANCE | UNREFERENCED-XML | 1 | HitPreCrit | B (ModAttributes[CriticalChance], `Frames="1"`) |
| PERK_ITEM_SPECIAL_LIFESTEAL | tmpl-parent | 2 | PostHit | B (ModAttributes[Lifesteal], `Frames="1"`) |
| PERK_ITEM_SPECIAL_LIFESTEAL_WEAPON | list:Weapon; stage(x24); forge | 0 | — | — (template child: inherits parent triggers) |
| PERK_ITEM_SPECIAL_LIFESTEAL_RANGED | list:Ranged; stage(x25); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_LIFESTEAL_MAGIC | list:Magic; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_BLEEDING | tmpl-parent | 4 | PostHit, ModExpires | B (ModAttributes[RegenerationRate], Frames-scoped) |
| PERK_ITEM_SPECIAL_BLEEDING_WEAPON | list:Weapon; stage(x11); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_BLEEDING_RANGED | list:Ranged; stage(x14); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_BLEEDING_MAGIC | stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_INTOXICATION | tmpl-parent | 2 | PostHit | B (ModAttributes[RegenerationRate], Frames-scoped) |
| PERK_ITEM_SPECIAL_INTOXICATION_WEAPON | list:Weapon; stage(x16); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_INTOXICATION_RANGED | list:Ranged; stage(x6); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_INTOXICATION_MAGIC | stage(x3); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_SHIELDING | tmpl-parent | 8 | RoundStageStart, PostHit, HitPostCrit, ModExpires | B (ModAttributes[DamageFactor], Frames-scoped; variable accumulation) |
| PERK_ITEM_SPECIAL_SHIELDING_HELM | list:Helm; stage(x9); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_SHIELDING_ARMOR | list:Armor; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_REGENERATION | tmpl-parent | 2 | PostHit | B (ModAttributes[RegenerationRate], Frames-scoped) |
| PERK_ITEM_SPECIAL_REGENERATION_HELM | list:Helm; stage(x11); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_REGENERATION_ARMOR | list:Armor; stage(x21); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_REJUVENATION | tmpl-parent | 2 | PostHit | B (ModAttributes[RegenerationRate], Frames-scoped) |
| PERK_ITEM_SPECIAL_REJUVENATION_HELM | list:Helm; stage(x3); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_REJUVENATION_ARMOR | list:Armor; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_DAMAGE_RETURN | tmpl-parent | 2 | PostHit | B (ModAttributes[Lifesteal], Frames-scoped) |
| PERK_ITEM_SPECIAL_DAMAGE_RETURN_HELM | list:Helm; stage(x10); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_DAMAGE_RETURN_ARMOR | list:Armor; stage(x10); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_PRECISION | tmpl-parent | 3 | HitPostCrit | B (ModAttributes[DamageFactor], `Frames="1"`; SetHit Critical) |
| PERK_ITEM_SPECIAL_PRECISION_WEAPON | list:Weapon; stage(x21); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_PRECISION_RANGED | list:Ranged; stage(x6); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_SHOCK | UNREFERENCED-XML | 3 | RoundStageStart, HitPostCrit, PostHit | B (ModAttributes[ShockHeadHitChance], Frames-scoped) |
| PERK_ITEM_SPECIAL_MAGIC_DAMAGE_RECHARGE | tmpl-parent | 3 | PostHit | B (ModAttributes[MagicDamageRecharge], Frames-scoped) |
| PERK_ITEM_SPECIAL_MAGIC_DAMAGE_RECHARGE_WEAPON | stage(x10); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_MAGIC_PAIN_RECHARGE | tmpl-parent | 3 | PostHit | B (ModAttributes[MagicPainRecharge], Frames-scoped) |
| PERK_ITEM_SPECIAL_MAGIC_PAIN_RECHARGE_HELM | list:Helm; stage(x19); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_MAGIC_PAIN_RECHARGE_ARMOR | list:Armor; stage(x13); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_CRITICAL_PROTECTION | tmpl-parent | 2 | HitPostCrit | B (ModAttributes[CriticalDamage], Frames-scoped) |
| PERK_ITEM_SPECIAL_CRITICAL_PROTECTION_HELM | list:Helm | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_STUN | tmpl-parent | 4 | HitPreCrit, AnimationEnd, HitPostCrit, AnimationStart | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_STUN_WEAPON | list:Weapon; stage(x5); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_STUN_RANGED | list:Ranged; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_STUN_MAGIC | stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_FRENZY | tmpl-parent | 8 | RoundStageStart, PostHit, HitPostCrit, ModExpires | B (ModAttributes[DamageFactor], Frames-scoped; variable accumulation) |
| PERK_ITEM_SPECIAL_FRENZY_WEAPON | list:Weapon; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_FRENZY_RANGED | list:Ranged; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_FRENZY_MAGIC | stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_FRENZY_DEFENSE | tmpl-parent | 8 | RoundStageStart, PostHit, HitPostCrit, ModExpires | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_FRENZY_DEFENSE_HELM | list:Helm; stage(x6); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_FRENZY_DEFENSE_ARMOR | list:Armor; stage(x6); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_OVERHEAT | tmpl-parent | 2 | PostHit, HitPostCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_OVERHEAT_WEAPON | list:Weapon; stage(x21); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_OVERHEAT_RANGED | list:Ranged; stage(x10); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_OVERHEAT_MAGIC | list:Magic; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_OVERHEAT_DEFENSE | tmpl-parent | 2 | PostHit, HitPostCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_OVERHEAT_DEFENSE_HELM | list:Helm; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_OVERHEAT_DEFENSE_ARMOR | list:Armor; stage(x1); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_TIME_BOMB | tmpl-parent | 2 | PostHit | B (ModFlag/SetHit only) |
| PERK_ITEM_SPECIAL_TIME_BOMB_WEAPON | list:Weapon; stage(x11); moves; forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_TIME_BOMB_RANGED | list:Ranged; stage(x10); moves; forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_TIME_BOMB_MAGIC | stage(x1); moves; forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_ENFEEBLE | tmpl-parent | 8 | RoundStageStart, PostHit, HitPostCrit, ModExpires | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_ENFEEBLE_WEAPON | list:Weapon; stage(x7); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_ENFEEBLE_RANGED | list:Ranged; stage(x10); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_ENFEEBLE_MAGIC | stage(x3); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_WEAKNESS | tmpl-parent | 8 | RoundStageStart, PostHit, HitPostCrit, ModExpires | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_WEAKNESS_WEAPON | list:Weapon; stage(x17); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_WEAKNESS_RANGED | list:Ranged; stage(x3); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_WEAKNESS_MAGIC | list:Magic; stage(x5); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_DAMAGE_ABSORPTION_HEAD | tmpl-parent | 3 | HitPreCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_DAMAGE_ABSORPTION_HEAD_HELM | list:Helm; stage(x16); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_DAMAGE_ABSORPTION_BODY | tmpl-parent | 3 | HitPreCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_ITEM_SPECIAL_DAMAGE_ABSORPTION_BODY_ARMOR | list:Armor; stage(x18); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_BLOODRAGE | tmpl-parent | 3 | PostHit | B (ModAttributes[Lifesteal], Frames-scoped) |
| PERK_ITEM_SPECIAL_BLOODRAGE_WEAPON | list:Weapon; stage(x23); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_BLOODRAGE_RANGED | list:Ranged; stage(x5); forge | 0 | — | — (template child) |
| PERK_ITEM_SPECIAL_BLOODRAGE_MAGIC | list:Magic; stage(x1); forge | 0 | — | — (template child) |
| PERK_ANTI_SHOCK | stage(x148) | 1 | PostHit | B (SetHit Shock edit only) |
| PERK_BOSS_HERMIT_MAGIC | UNREFERENCED-XML | 1 | EveryFrame | B (AddMagicCharge only) |
| PERK_BOSS_BUTCHER_BLEEDING | UNREFERENCED-XML | 1 | PostHit | B (ModAttributes[RegenerationRate], Frames-scoped) |
| PERK_WEAPON_BLOCK_BREAKER | UNREFERENCED-XML | 2 | HitPreCrit | B (SetHit Block edit only) |
| PERK_GREATER_GOOD_SHARPENING | UNREFERENCED-XML | 2 | HitPostCrit | B (SetHit Critical edit only) |
| PERK_BOSS_WASP_DAMAGE_RETURN | UNREFERENCED-XML | 2 | PostHit | B (ModAttributes[Lifesteal→DamageReturn], Frames-scoped) |
| PERK_BOSS_WASP_REGENERATION | UNREFERENCED-XML | 2 | PostHit | B (ModAttributes[RegenerationRate], Frames-scoped) |
| PERK_BOSS_WIDOW_LIFESTEAL | UNREFERENCED-XML | 2 | PostHit | B (ModAttributes[Lifesteal], Frames-scoped) |
| PERK_BOSS_WIDOW_SHIELDING | UNREFERENCED-XML | 2 | HitPreCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_BOSS_SHOGUN_FRENZY | UNREFERENCED-XML | 5 | HitPreCrit, HitPostCrit, ModExpires | B (ModAttributes[DamageFactor], Frames-scoped; SetTactic) |
| PERK_INVISIBILITY | stage(x9); moves | 5 | RoundStageStart, ModExpires, EveryFrame, HitPostCrit | B (ModInvisibility/SetTactic only) |
| PERK_BOSS_ABILITY | tmpl-parent | 2 | RoundStageStart, AnimationStart | B (ModFlag only) |
| PERK_HERMITSTORM | stage(x7); moves | 1 | PostHit | B (ModFlag only) |
| PERK_EARTHQUAKE | stage(x7); moves | 0 | — | — (template child) |
| PERK_WASPFLY | stage(x7); moves | 0 | — | — (template child) |
| PERK_TELEPORTATION | stage(x11); moves | 0 | — | — (template child) |
| PERK_ASSISTANTS | stage(x7); moves | 0 | — | — (template child) |
| GREATER_RANGED_DAMAGE | stage(x6) | 1 | HitPreCrit | B (ModAttributes[DamageFactor], `Frames="1"`, **unconditional** `<Conditions/>`) |
| GREATER_MAGIC_DAMAGE | stage(x4) | 1 | HitPreCrit | B (ModAttributes[DamageFactor], `Frames="1"`, unconditional) |
| PERK_MINE | stage(x2); moves | 0 | — | — (template child) |
| PERK_SHROUD_FALL | stage(x1); moves | 4 | PostHit, AnimationStart | B (SetHit/ModFlag only) |
| PERK_MINE_PLAYER | list:Ranged | 2 | RoundStageStart, AnimationStart | B (ModFlag/SetCooldown only) |
| PERK_SPHERE_COOLDOWN | UNREFERENCED-XML | 1 | RoundStageStart | B (ModFlag/SetCooldown only) |
| MindThrow | list:Magic | 3 | AnimationStart | B (ModFlag/ClearMods only) |
| PERK_TITAN | stage(x2) | 1 | RoundStageStart | B (ModFlag only) |
| PERK_TITAN_DEATH | stage(x2) | 2 | RoundStageStart, AnimationEnd | B (ModFlag/ClearMods only) |
| PERK_TITANS_SHIELD | stage(x4); moves | 3 | RoundStageStart, ModExpires, HitPreCrit | B (ModAttributes[DamageFactor], Frames-scoped) |
| PERK_TRANSIENT_BLEEDING | stage(x20) | 3 | PostHit, AnimationStart | B — ModAttributes[RegenerationRate] **w/o Frames** (named mod `PassingEffect`, applied on PostHit, cleared on AnimationStart; still event-gated — closest to A) |
| PERK_TRANSIENT_REGENERATION | stage(x2) | 3 | HitPostCrit, PostHit, AnimationStart | B — ModAttributes[RegenerationRate] w/o Frames (same named-mod pattern) |
| PERK_NO_THROWS | stage(x2) | 1 | RoundStageStart | B (ModFlag only) |
| PERK_AREA_HEALTH_REGENERATION | stage(x15) | 3 | AreaEnter, AreaExit, AnimationStart | B — ModAttributes[RegenerationRate] w/o Frames (area-scoped named mod) |
| PERK_AREA_HEALTH_DEGENERATION | stage(x10) | 3 | AreaEnter, AreaExit, AnimationStart | B — ModAttributes[RegenerationRate] w/o Frames (area-scoped) |
| PERK_AREA_VULNERABILITY | stage(x3) | 3 | RoundStageStart, AreaEnter, AreaExit | B — ModAttributes[DamageFactor=-100000] w/o Frames, Player=Enemy (area-scoped) |
| PERK_AREA_FRAGILITY | stage(x4) | 3 | AreaEnter, AreaExit, AnimationStart | B — ModAttributes[DamageFactor] w/o Frames (area-scoped) |
| PERK_AREA_LOSE_ON_ANIMATION | stage(x4) | 2 | PostHit | B (SetHit only) |
| PERK_RULE_EARTHQUAKE | stage(x5); moves | 4 | ModExpires, RoundStageStart, PostHit | B (ModFlag/SetHit only) |
| PERK_RULE_THORNS | stage(x4) | 2 | EveryFrame | B (ModAttributes[RegenerationRate], Frames-scoped) |
| PERK_RULE_ENERGY_PILLAR | stage(x3); moves | 2 | ModExpires, RoundStageStart | B (ModFlag only) |
| PERK_MONK_SET_WHIRL | stage(x10); moves; forge | 11 | RoundStageStart, HitPreCrit | B (combo stack: ModVariable/ChangeImpulse/ChangeAdditionalDamageValue; no attr writes) |
| PERK_SENTINEL_SET_SUPER_RETURN | forge | 0 | — | — (template child of EndStanceClear; behavior presumably binary-side) |
| PERK_DRAGON_SET_STONE | forge | 0 | — | — (template child; same) |
| PERK_HEAVEN_SET_BURN | forge | 0 | — | — (template child; same) |

---

## 6. Hypothesis summary & fork-relevant observations

| class | count |
|---|---|
| total perks | **142** |
| clean **A**-hypothesis (persistent, no Frames, no Conditions) | **0** |
| **B**-hypothesis total | **136** (78 with own triggers + 58 template children inheriting) |
| · of which attribute-writing (ModAttributes / Set-action) | 52 (51 ModAttributes + PERK_FULL_POWER) |
| · of which non-attribute (icon/flag/hit-edit/variables only) | 26 |
| bare move-gate tokens (no attribute path at all) | 6 |

Observations that shape Step 4 (all factual from the XML; interpretation is
for the binary):

1. **Every attribute write is event-gated.** All 61 `ModAttributes` (and the
   one Set-action) sit inside `<Actions>` of a trigger with an `<Events>`
   subscription or a `Provoke` path. 54/61 carry `Frames`, 44 of those
   `Frames="1"` — scoped to the currently-evaluated hit. The 7 without
   `Frames` (PERK_TRANSIENT_BLEEDING/REGENERATION, PERK_AREA_×4 family) are
   *named* mods applied on one event and explicitly cleared on another — still
   trigger-lifetime, but the closest thing to a persistent contribution the
   data offers.
2. **Perks touch the damage formula only through `DamageFactor`** (the
   `base_attribute` term of `DamageInputs`) and the crit params
   (`CriticalChance`/`CriticalDamage`) — never through the seven tracer
   attributes. If the binary confirms `ModAttributes` writes land in (or are
   consulted alongside) the `model+0x1C4` name→int map that `getParameter`
   (game+0x6275F4) reads, the wiring point is the DamageFactor key, not
   WeaponDamage & co.
3. **Reference data flows are item-side parameter overrides.** list.xml /
   CharacterProgress.xml / forge.xml never define behavior — they only set
   `<Set>` values (`Aspect`, `Frames`, `DamageFactor`, …) on a `Perk@Name`.
   All behavior lives in perks.xml triggers → the binary survey can key off a
   handful of perk-name strings (e.g. `PERK_HELM_BREAKER`, `PERK_AVENGER`,
   `GREATER_RANGED_DAMAGE`) plus the element-name strings (`ModAttributes`,
   `HitPreCrit`, `Trigger`, `Provoke`, …) to find the parser and the apply
   path.

---

## 7. Open questions handed to Step 4 (binary survey)

1. Where does `ModAttributes` write — the same name-keyed int map at
   `model+0x1C4` that `Model::getParameter` reads, or a separate mod store
   consulted during hit evaluation? Add-vs-set semantics? Sign and int
   conversion of values like `5850` / `-100000` / `300000`?
2. Is action-`<Set>` (PERK_FULL_POWER) the same code path as `ModAttributes`?
3. How is `Frames` counted (16 ms frames per GAP-1?) and where do expired
   mods get reaped (`ModExpires` event source, `ClearMods`, `EndStanceClear`)?
4. How does `Template=` merge — at parse time or runtime — and how do
   item-side `<Set>` overrides (list.xml / CharacterProgress `UpgradeLevel` /
   forge `?RandomAspect`) reach the perk instance?
5. Who attaches the 13 XML-unreferenced perks (boss perks,
   `PERK_ITEM_SPECIAL_CRITICAL_CHANCE`, `PERK_ITEM_SPECIAL_SHOCK`,
   `PERK_SPHERE_COOLDOWN`) — hardcoded attach points?
6. How does `RatingEvaluation` consume `EnchantmentResistance`, and does the
   `?Aspect[…]` expression read `PlayerAttribute[Enemy].EnchantmentResistance`
   through `getParameter` (i.e. is `-1e35f` possible here, or defaulted)?
7. `SetVariable` vs `ModVariable` lifetime semantics.

---

## 8. Binary anchors (Step 4 — Ghidra, relocated dump)

Full details, decompiled shapes, and reproduction queries:
**`reverse/analysis/PERK_BINARY_SURVEY.md`**. Program
`game_region_runtime.bin` @ base `0x8F057000` (16420 functions, saved in the
Ghidra project). `game+X = 0x8F057000 + X`.

**Parser chain**: boot asset registrar `FUN_8f653448` (game+0x5FC448,
'assets/perks.xml' @ game+0x5F9A54) → load driver `FUN_8f653fb0`
(game+0x5FCFB0, "ERROR: loadPerks - wrong file") → template/ID pass
`FUN_8f6a434c` (game+0x64D34C; Perks/Perk/Template/ID) → registrar
`FUN_8f6679dc` (game+0x6109DC; registry @ `0x8F868C54`) → `PerkObject::parse`
`FUN_8f68b280` (game+0x634280; Set params → perk+0xBC map) → trigger parse
`FUN_8f6a33f8` (game+0x64C3F8) → factories: events `FUN_8f699be0`
(game+0x642BE0), conditions `FUN_8f695758` (game+0x63E758), actions
`FUN_8f68e9fc` (game+0x6379FC). Attach/instance factory `FUN_8f68ba34`
(game+0x634A34) — Set overrides merge at RUNTIME (re-parse), template
inheritance at LOAD.

**Apply path**: event dispatch `FUN_8f6a9a38` (game+0x642A38, +2 siblings) →
action switch `FUN_8f6a9164` (game+0x642164) case 3 →
`PerkActionSetAttributes` executor `FUN_8f6a6c70` (game+0x64FC70): evaluates
the (name, expression) entries and writes **model+0x1C4's secondary (mod)
key** as `model = modVal*sign + model` (sign +1 apply / −1 unapply);
'DamageFactor' additionally pokes the in-flight hit. Reaper `FUN_8f6aac7c`
(game+0x643C7C) subtracts at expiry (Frames/ClearMods/EndStanceClear).
`getTotalDamage` (FUN_8f4a97b4, game+0x4527B4) consumes DamageFactor via the
mod-aware map read at game+0x452808 → the `2^(DF*w)` base term.

**Fork verdict: ZERO Case A. All mechanisms Case B** (transient, trigger-
gated, subtract-on-reap). Perk-level `<Set>` = instance param storage, not
attribute writes. 13 XML-unreferenced perks have no binary name refs (only
`PERK_DOUBLE_SWEEP` @ 0x8F79CBCC is hardcoded, a move-gate check). No
candidate C++; no re-verifier round; Step 5 = anchor + single `[ORIGINAL]`
TODO; Step 6 = documented N/A.

---

*Survey produced from shipped assets only. Step 4 appends the binary anchors
(parser + apply path addresses, decompiled shapes) to this file.*

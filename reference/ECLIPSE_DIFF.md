# ECLIPSE_DIFF — Eclipse vanillaXml (2.41.9) vs web XML

**Date:** 2026-09-05. **Status:** research doc only — no code, no commit.
**Discipline:** merge-missing-only. Eclipse values NEVER overwrite web values;
they only fill gaps the web build is missing. All gameplay truth stays
web-JS-first (JS-STRICT: this diff informs OPENs, never overrides JS lines).

## Sources

Eclipse repo: `hwindinkg/ProjectEclipse` (Unity 2022.3.62f3 base project).
Vanilla set = `Assets/vanillaXml/` = canonical vanilla 2.41.9
(per `README.md` “`Assets/vanillaXml/` - canonical vanilla 2.41.9
gameplay/configuration XML” and `HANDOFF.md` “The active gameplay/configuration
source is now `Assets/vanillaXml/`, containing the imported vanilla 2.41.9 data”).
Fetched raw-only (`raw.githubusercontent.com`, no clone/LFS):

- `Assets/vanillaXml/internalSettings.xml` (39 827 B)
- `Assets/vanillaXml/locations/dojo/dojo_params.xml` (2 589 B)
- `Assets/vanillaXml/localization.xml` (4 606 B)
- `Assets/vanillaXml/localizations/rus.xml` (709 736 B, 5 140 `Word` entries)
- `Assets/vanillaXml/BuildSettings.json` (3 725 B)
- `Assets/vanillaXml/stages.xml` (1 671 133 B)
- `Assets/vanillaXml/list.xml` (for RangedQuantity context)

Web counterparts:

- `reference/extracted/xml/res/internal_settings.xml` (43 156 B)
- `reference/www/res/locations/dojo/dojo_params.b78df4b4.xml`
- `reference/www/res/lang/ru.f7d5b2da.xml` (637 597 B, 4 888 `Word` entries)
- `reference/extracted/xml/res/stages.xml`, `list.xml`, `users_default.xml`

Eclipse merge logic (informative only):
`Assets/Scripts/Eclipse/Content/InternalSettingsCompatibility.cs`
(`ImportMissingTopLevelSections` — import whole section only if absent;
`MergeMissingSettings` — add missing attrs/children, never overwrite),
`StageCompatibility.cs` (`ClampLegacyRoundTimes`: `RoundTime > 99 → 99`
unless preserved; `EnsureSurvivalRewardRows`: pad reward rows to wave count
with terminal reward; `MergeMissingBattles`).

## Gap table

| # | Item | Eclipse vanilla | Web | Verdict |
|---|---|---|---|---|
| 1 | Shock `Treshold` | `<Treshold Value="999"/>` | `<Treshold Value="999"/>` — identical full `<Shock>` block (FrameReduction 0.001, Weapon Fists, SetAttribute WeaponDamage/0, LooseningDelay 12, Impulse Y=-0.5, crit/head Base 0.0001) | **CONFIRM 999.** No action. Matches `core/scene/damage.hpp` default 999.0 and `reference/COMBAT_STATIC.md` L281 |
| 2 | `StartingBullets` / `RangedQuantity` | `<StartingBullets Attribute="RangedQuantity"/>` in vanilla internalSettings | Same element present in web internal_settings. `RangedQuantity` count in `list.xml` = **0 in both**; `StartingBullets` count in `list.xml` = **0 in both**; web `users_default.xml` uses `Ranged="NoRanged"` | **OPEN stays open.** 0 JS refs (`COMBAT_STATIC.md` L382). Needs live/item trace, not Eclipse fill — nothing to merge |
| 3 | `RoundTime > 99 → 99` | Vanilla has exactly one `RoundTime > 99`: `RoundTime="990"` on `ZONE_T\|TEST\|1` Fight. `ZONE_T` is a **test zone absent from web** (vanilla 9 zones: Punchbag + ZONE_T + ZONE_1..7; web 8 zones, no ZONE_T). Eclipse `ClampLegacyRoundTimes` exists and would clamp 990→99 | Web stages `RoundTime` distribution: 99×523, 60×14, 45×16, others ≤60 — **zero values > 99** | **NO-ACTION for web.** Clamp rule confirmed to exist in Eclipse compat; web needs no clamping. Do NOT import ZONE_T/TEST (test content, not a gap) |
| 4 | Survival rewards | Waves vs rows complete on both sides (10 waves→11 rows incl. row0; 6 waves→7 rows). Eclipse `EnsureSurvivalRewardRows` pads with terminal reward — nothing to pad anywhere | Content differs, web is the **superset**: normal survival rows — vanilla `Exp+Money` only, web adds `Bonus` (2,4,…,20). INTERMISSION rows — vanilla `Exp=2000` + huge Money, web `Exp=2` + Money + `Bonus`. ZONE_7 rows — vanilla no Bonus/PrizeBase, web has `Bonus=81` + `PrizeBase` | **NO MERGE (web wins).** Missing-only discipline: vanilla adds zero missing rows/attrs. Web economy values stay untouched |
| 5 | `rus.xml` strings | `localizations/rus.xml` exists: 5 140 Words / 5 139 titles | `lang/ru.*.xml` exists: 4 888 Words / 4 887 titles | **AVAILABLE.** 256 vanilla-only titles = live-event content (see §Localization), 4 web-only titles, 11 shared titles with text diffs (web wins). Candidate: import missing event strings only (no overwrite) |
| 6 | “7 missing sections” | Eclipse `HANDOFF.md`: “Vanilla 2.41.9 `internalSettings.xml` omits seven top-level sections that the older recovered runtime still expects.” Eclipse `Tools/AnalyzeXmlMigration.py` names them: **AssemblySettings, Internet, Supports, EULA, Log, ForcedLogConditions, StarterPackTimer** | Web `internal_settings.xml` lacks **the same 7** — top-level sections are 70/70 identical vanilla↔web (same names, same counts, same order-insensitive children except §7) | **Clarification, not a web gap.** The “7 missing” is Eclipse-vs-legacy-runtime, NOT vanilla-vs-web. For reSF2: nothing missing; do NOT import legacy-runtime sections into web data |
| 7 | Other internalSettings diffs (full recursive compare) | — | — | Only two: (a) `<Hints>`: 7 `RaidRoundAdvice{1,2,3,4,6,7,8}` Items vanilla-only (raid content; web has no raid hints) — merge candidate, additive only. (b) `<StyleLevels>`: same 6 styles, atlas prefix differs — vanilla `FightUI.*`, web bare names (`CrazyBar_*`, `Hard`, …). Web naming stays (JS-side); no merge |
| 8 | `Camera` | `<CameraSettings CameraNode="COM" BindingNode="NPivot" MaxWidth="1100" BindingLength="100" MaxWidthDelta="50"/>` | Byte-identical | **CONFIRM.** No action |
| 9 | `dojo_params` spawns | `ModelsViewer PlayerPositionX=690 Y=-93 EnemyPositionX=973 EnemyPositionY=-110` | Identical values | **CONFIRM.** No action |
| 10 | `dojo_params` Root/scene | `Music="6\|7" Color="0x281409" Wall="300" Floor="78"`, 5 layers with `dojo_bg`/`dojo_atlas_layer*` atlases, side/top/bottom `pixel_1` masks, `dojo_punch_bag_holder`, L/R walls | `Wall="80" Floor="80" Pages="1" Color="0x000000"`, 11 layers with `_NNNN_*` classes, no Atlas attrs, lamps + go_table, `layer_4` SimpleEffect with identical Transparency points (45/75/55/75) | **NO MERGE.** Different scene builds (mobile-vanilla vs web); web render path is tuned to web file. `layer_4` effect params match — confirms Wave-7 `location_scene.cpp` alpha work, nothing to change |
| 11 | `BuildSettings.json` | Exists (store URLs, timeouts, `TacticsCaching`, `ShakeScreenParams`, `LogRules`, …) | **No web counterpart** (no match in `reference/extracted/xml/res`) | **NO-ACTION.** Eclipse/launcher-only config, no web consumer. Do not invent one |

## Localization detail (item 5)

Vanilla-only titles (256) are almost entirely live-event/seasonal content the
web snapshot predates or never shipped: `ARMOR/HELM/MAGIC/RANGED/WEAPON`
`*_AZTEC_24`, `*_FUTURIST_25`, `*_NY25`/`*_NEW_YEAR_25`, `*_KARCER_*`,
`*_VOLCANO_*`, `*_SUMMER_FEST_25`, `*_HW_24` sets; `BOSS_NZAMBI(_TITLE)`;
`CNY26_*`, `KeyVaisakhi25`, `*HW24`, `*Vaisakhi*`, `*SummerFest*`,
`*NY25*`, `*CHN_NY25*`, `*American_25*`, `dlgShurale_*`, `dlgEvent_HW24_*`
stories; `SFest*`, `summerfest25_story*`, `win_*/loose_*` batches;
`ENCHANTMENT_HUNGER`, `SPHERES_BALANCER`, `LocalDaily_AnyAd`, `PackOffer5`,
`NY25Chest/Key/Coin`, raid set briefs (`SetUi_KARCER_SET_Brief`,
`SetUi_VOLCANO_SET_Brief`). Full list reproducible via Title-key diff
(`Words/Word[@Title]`): vanilla 5 139 vs web 4 887.

Web-only titles (4, keep): `LocalDaily_RewardedMultiplierVideo`,
`Restore_Error`, `Restore_Info`, `Settings_Restore`.

Shared titles with different text (11, web wins, do not overwrite):
`dlgRaidStarterPackOffer{1,2,3}` (whitespace), `dlgServiceTitlePackDownload`,
`dlgServiceDownloadDemand(Forced)`, `dlgServiceDownloadRequest`,
`dlgServiceAgreeBtn`, `PUSH`, `MAGIC_BP_S4_SCRIPTWRITER`.

## Merge-missing-only rules for any follow-up

1. Import only nodes/attrs **absent** from web (Eclipse `ImportMissing*` semantics).
2. Never change a web value, name, or row (style prefixes, Bonus/PrizeBase,
   RoundTime table, dojo geometry).
3. ZONE_T/TEST, legacy-runtime-only sections (§6), and BuildSettings.json are
   explicitly out of scope.
4. Candidates if proposal #2 proceeds: 7 raid-hint Items (§7a) + 256
   vanilla-only rus strings (§5) — additive, JS-gated where surfaced.

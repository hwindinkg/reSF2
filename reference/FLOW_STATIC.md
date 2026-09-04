# FLOW_STATIC — quest machine, map zones, save format, after-battle rewards (web build)

Static-only spec extracted from `reference/www/sf2.502f0946.js` (measured
2533 lines, 1-based) plus on-disk XML (`reference/extracted/xml/res/*`,
`reference/www/res/*`, read-only). Anything not resolvable without a runtime
trace is marked `OPEN (needs runtime trace)`.
Companion: `reference/JS_FLOW.md` (shell/screen flow narrative — this file is
the machine-readable remainder: quest ids, zone tables, save fields, reward
formulas, all with exact JS line numbers).

Minified names: `wa` navigator (L933-934), `ha` quest manager (L1014-1020),
`Bj` quest journal (L1004), `Yb` action sequence (L954), `S` quest-action base
+ `Fe` E-name factory (L948-953), `p` world state (L176-221), `xf` save data
(L244-274), `v` fight-flow static (L1212-1218), `hp` fight result (L1239-1241),
`sw` reward bundle (L1242), `Ya` Map screen (L2125-2132), `Vr` zone scroller
(L2117-2124), `qe` zone widget (L2140-2144), `st` zone record (L1431-1432),
`Lc/Gb/cl` location records (L1403/L1409/L1411), `hb` battle id (L1416),
`xn` screen enum (L1167), `Aa` save storage (L70-73), `Zd/ad` loader
(L1837/L1969), `oo/qo/so/to/Nn/Mn/Gn/Sn/Tn/He/go/Bo/Co/Do/Eo/Fo/Ao`
quest-action impls (L1030-L1126).

---

## 1. Tutorial quest machine

### 1.1 Quest file and registration (static chain)

| Step | Fact | Line / source |
|---|---|---|
| Quest root file id | `Z.Jna="quests.xml"` | L2478 |
| Root parse entry | `p.L3(Z.Jna)` called from world init `p.Edb` | L178 (`this.L3(Z.Jna)`) |
| `p.L3(file)` | `Ja.ki(G.qf(file))` → children: `Quest` → `ha.F().WO(new be(...))`; `Include` → conditional `Sjb` recurse | L184 |
| Conditional include | `Sjb`: `be.GS(Conditions)` must all `compare(ta)` else skip; `File` split on `\|` | L184 |
| Quest registration | `ha.WO(quest)`: index by event type into `lC` map + `OJa` list; `QUEST_EVENT_ACTIVATE` also to `Vqa` | L1017 |
| Quest object | `be` (quest def: `name/fileName/Hc[]` event list, `compare(ta)`, `jLa(ta)`, `lF(ta)`) | L1017 (`WO`), L1018 (`RA: e.compare(this.ta)`, `Qaa: a.jLa(this.ta)`) |
| Queue | `ha.add`: push to `Dh[]`; `EJ` latch starts pump unless already in fight (`Td.Tf==6`) | L1017 |
| Pump | `ha.eLa()`: shift `Dh[0]`, `b.Mbb()` gate → `b.lF(ta,false)` runs `Yb` action chain; on done `Mt` removes + `Rla` re-sort | L1018-1019 |
| Event fire | `ha.RA(name)` → `ha.Sf(name)`: match `lC` list, `compare(ta)` → `Qaa` → `qT()` pump | L1017-1018 |
| Event-name map | XML tag → `QUEST_EVENT_*` (`ChangeTab`→`QUEST_EVENT_CHANGE_TAB`, `SceneLoaded`→`QUEST_EVENT_SCENE_LOADED`, `FightEnd`→`QUEST_EVENT_FIGHT_END`, `SessionStart`→`QUEST_EVENT_SESSION`, …) | L998-1004 (`So` map class) |
| Action factory | XML action tag → `E`-class (`EChangeScene`→`Gn`, `EFight`→`Sn`, `EFightEnd`→`Tn`, `EOpenShop`→`go`, `EDialog`→`He`, `ESetMapFocus`→`qo`, `ESetCurrentZone`→`oo`, `ESetStoryTutorialStep`→`so`, `ESetVariable`→`to`, `EClickButton`→`Nn`, `EClearQuestQueue`→`Mn`, `EStoryTutorialMove`→`Do`, `…DoubleSweep`→`Bo`, `…LearnPerk`→`Co`, `…Punchbag`→`Eo`, `…ShowBlock`→`Fo`, `…BuyItem`→`Ao`, …) | L948-953 (`Fe.Ij` + `Nz` `E`-prefix matcher L953) |
| Action sequence | `Yb`: `Vl` push, `S` run in order via `qd` events, `gf` advance | L954 |
| Journal | `Bj` (`ha.ta`): `Nb` (current battle `hb`), `Qv` (Win/Loss/Surrender), `DI/Xo/nLa/lLa/XNa/YNa` (scene/tab from/to), `J_/t2`, `setItem`, vars | L1004 (ctor fields), L933-934 (`nLa=xn.iOa(a)`, `Xo`), L1214 (`Nb/Qv/t2/J_`) |
| `_$-var` resolver | `_$StoryTutorialStep` → `p.o.zi.HH`; `_$SceneTo` → `ta.nLa`; `_$SceneFrom` → `ta.lLa`; `_$Fight`/`_$FightResult` similar | L964 |
| Scene announce | `wa.ghb()`: after every screen push → `ha.F().Sf("QUEST_EVENT_SCENE_LOADED")` | L934 |
| Session start | `v.uwb()` → `ha.F().Sf("QUEST_EVENT_SESSION")` (fired from loader `dp` module) | L1215 |

### 1.2 Tutorial quest chain (XML: `reference/extracted/xml/res/quest_extensions/tutorial_quests.xml`)

All 13 quests `Unresumable="1"`. Step variable `_$StoryTutorialStep`
(`SetStoryTutorialStep` action = class `so`, L1119).

| # | Quest (`Name`, Priority) | Event | Trigger condition (`Conditions`) | Completion / effect (`Actions`) |
|---|---|---|---|---|
| 1 | `StoryTutorialWelcome` (-10) | `ChangeTab` | `_$StoryTutorialStep == NotStarted` | `ChangeScene Dojo` (`Gn`, L1030); Switch/Steam branch sets `NotificationTextMove/_PunchBag`; `Dialog Notification` ×2; `StoryTutorialMove` (`Do`, L1123); `StoryTutorialPunchbag` (`Eo`, L1124); `Dialog Regular` button → `SetStoryTutorialStep FIGHT` + `Fight Punchbag\|Bosses\|1` (`Sn`, L1069) |
| 2 | `StoryTutorialReturnToFight` (-10) | `ChangeTab` | step==FIGHT AND NOT(SceneFrom==Fight AND `?Fight[_$Fight].Zone`==Punchbag AND FightResult==Win) AND SceneTo!=Fight | `ChangeScene _$SceneTo`; `Dialog` button → `Fight Punchbag\|Bosses\|1` |
| 3 | `StoryTutorialShop` (-100) | `ChangeTab` | OR(AND(step==FIGHT, SceneFrom==Fight, Zone==Punchbag, Win), AND(step==STEP_BUY_ITEM, SceneTo!=Fight, SceneTo!=Shop)) | `ChangeScene _$SceneTo`; step→STEP_BUY_ITEM; globals `SenseiDialogText=tutorial_shop`, `NextScene=Shop`; `Activate StoryTutorialOpenScene` |
| 4 | `StoryTutorialBuyItem` (-100) | `ChangeTab` | step==STEP_BUY_ITEM AND SceneTo==Shop | `OpenShop Tab=?Purchase[WEAPON_KNIVES].Type Item=WEAPON_KNIVES` (`go`, L1088); `StoryTutorialBuyItem` (`Ao`, L1120); step→MAP; `Dialog tutorial_buy_knives`; globals `DelayBeforeOpenScene=30`, `SenseiDialogText=tutorial_map`, `NextScene=Map`; `Activate StoryTutorialOpenScene` |
| 5 | `StoryTutorialRetryGoToMap` (-100) | `SceneLoaded` | step==MAP AND SceneTo!=Map AND SceneTo!=Fight | `ChangeScene _$SceneTo`; `Dialog tutorial_buy_knives`; set delay/text/next=Map; `Activate StoryTutorialOpenScene` |
| 6 | `StoryTutorialBossFight` (-100) | `SceneLoaded` | step==MAP AND SceneTo==Map | `ChangeScene Map`; `Wait Frames=1`; `SetMapFocus Battle=ZONE_1\|BOSS_LYNX\|1` (`qo`, L1086); `ClickButton Target=InfoBattle.FightButton UseFlashing=1 IgnoreCallback=1` (`Nn`, L1115); `ClearQuestQueue StoryTutorialRetryGoToMap` (`Mn`, L1037); `Dialog tutorial_boss_hello(+2)` button → `Fight ZONE_1\|BOSS_LYNX\|1` |
| 7 | `StoryTutorialLearnPerk` (-100) | `ChangeTab` | step==LEARN_PERK AND SceneTo==Profile AND `?Player[].Level >= 2` | `ChangeScene Profile`; `StoryTutorialLearnPerk` (`Co`, L1122); step→SHOW_DOUBLE_SWEEP; delay/text=`tutorial_dojo_new_move`/next=Dojo; `Activate …` |
| 8 | `StoryTutorialGoToDojo` (-100) | `ChangeTab` | step==SHOW_DOUBLE_SWEEP AND SceneTo!=Dojo AND SceneFrom!=None AND SceneFrom!=Loader | set text/next=Dojo; `Activate StoryTutorialOpenScene` (no scene change) |
| 9 | `StoryTutorialGoToDojoFromLoader` (-100) | `ChangeTab` | step==SHOW_DOUBLE_SWEEP AND SceneTo!=Dojo AND OR(SceneFrom==None, SceneFrom==Loader) | `ChangeScene Dojo` |
| 10 | `StoryTutorialDoubleSweep` (-100) | `ChangeTab` | step==SHOW_DOUBLE_SWEEP AND SceneTo==Dojo | `ChangeScene Dojo`; Switch/Steam branch for `NotificationText`; `Dialog Notification`; `StoryTutorialDoubleSweep` (`Bo`, L1121); step→SHOW_BLOCK; delay/text=`tutorial_profile_moves`/next=Profile; `Activate …` |
| 11 | `StoryTutorialGoToProfile` (-100) | `ChangeTab` | step==SHOW_BLOCK AND SceneTo!=Profile | `ChangeScene _$SceneTo`; set text/next=Profile; `Activate …` |
| 12 | `StoryTutorialShowBlock` (-100) | `ChangeTab` | step==SHOW_BLOCK AND SceneTo==Profile | `ChangeScene Profile`; `Dialog Notification tutorial_block`; `StoryTutorialShowBlock` (`Fo`, L1126); step→END; delay/text=`tutorial_return_map`/next=Map; `Activate …` |
| 13 | `StoryTutorialOpenScene` (-1000) | `Activate` | `_$ActionID == StoryTutorialOpenScene` | delayed open of `NextScene` with `SenseiDialogText` after `DelayBeforeOpenScene` (template tail of file) |

Key action implementations:

| Action XML | Class | Line | Verbatim behavior |
|---|---|---|---|
| `ChangeScene` | `Gn` | L1030-1032 | `mp(3)` fallback path; `mp(5)` when `tqa && MapFocus.Me!="ZONE_1"` reset-to-ZONE_1 branch (L1032) |
| `Fight` | `Sn` | L1069-1070 | `Wv(battle)` lookup; found → `v.Am(a)` else `wa.F().mp(3)` fallback |
| `FightEnd` | `Tn` | L1070 | `ca.Ka().kD(false)` (immediate or delayed `Re`) |
| `OpenShop` | `go` | L1088-1090 | resolve tab/item, `Oa.uLa` if Shop open else `wa.F().mp(4, new Gj(tab,item))` |
| `Dialog` | `He` | L1042+ | lines/buttons; button nests `Fight`/`SetStoryTutorialStep`/etc. |
| `SetMapFocus` | `qo` | L1086 | `p.o.m5(battle)` + `Ya` focus refresh |
| `SetCurrentZone` | `oo` | L1096-1097 | `p.o.stb(name)` |
| `SetStoryTutorialStep` | `so` | L1119 | sets `p.o.zi.HH` (read at L964) |
| `SetVariable` | `to` | L1112 | global/local quest vars (`SenseiDialogText`, `NextScene`, …) |
| `ClickButton` | `Nn` | L1115 | `Target/UseArrow/UseFlashing/ArrowOffsetX…` auto-click |
| `ClearQuestQueue` | `Mn` | L1037 | `ha.F().Yba(names)` |
| `ChangeTab` event | `v.qwa` | L1212 | `Sf("QUEST_EVENT_CHANGE_TAB")` |
| Quest file attach | `Bn` | L1025 | `p.F().L3(filename)` at runtime |

`OPEN (needs runtime trace)`: live `Dh[]` ordering under simultaneous
`ChangeTab`+`SceneLoaded` (sort `Wy`/priority ties, L1017); `Yb` async waits
(`Wait Frames`, `Dialog` modal gating, `Activate` delay frames); `be.Mbb/compare`
per-quest gate details beyond the XML above; `Et/SIa` persisted quest params
(L262); exact `StoryTutorialOpenScene` delay-frame → scene-open path
(template tail after L1126-area actions).

---

## 2. Map zones

### 2.1 Zone list (authoritative: `reference/extracted/xml/res/stages.xml`)

`p.Dkb()` loads asset 273 (`Ja.ki(273)`, L203) → `Ckb` per `<Zone>` (L204):
`new st(Name, FileName, Start!=null, 2, 0, RewardDigits, PrizeBaseDigits)`;
children `<Battle>` → `Qib` locations (L205-206).

| Zone | `FileName` (map bg) | `Start` | Battles | Fights | Battle names (`Name [Type] X,Y Loc`) |
|---|---|---|---|---|---|
| `Punchbag` | — (none) | `1` | 2 | 3 | `Training [DUMMY] 158,145 dojo`; `Bosses [TUTORIAL] -100,-40 bamboo_grove` |
| `ZONE_1` | `Map0.1` | — | 12 | 73 | `BOSS_LYNX [BOSSES] -180,-45 moon`; `Tournament [TOURNAMENT] -395,10 arena`; `Survival [SURVIVAL] -310,-165 bamboo_grove`; `Duel [PERIODIC] 45,60 mountain`; `Stranger [HIDDEN] mountain`; `Ambush1400 [HIDDEN] mountain`; `*_INTERMISSION` ×4; `BOSS_HARDMODE [BOSSES_REPLAYABLE]`; `SENSEI_MEMORIES [STORY] 45,-200 statue` |
| `ZONE_2` | `Map1.2` | — | 14 | 70 | `BOSS_HERMIT_LOCKED [FAKE]`; `BOSS_HERMIT [BOSSES] -195,-65 chess_yard`; `Tournament -100,-220 night_bridge`; `Challenge -380,-170 autumn`; `Survival 20,-10 emerald_forest`; `Duel [PERIODIC] -160,100 sakura`; `Stranger [HIDDEN] sakura`; `*_INTERMISSION` ×4; `BOSS_HARDMODE`; `SENSEI_MEMORIES -350,45 stone_dragon` |
| `ZONE_3` | `Map1.3` | — | 14 | 70 | `BOSS_BUTCHER_LOCKED [FAKE]`; `BOSS_BUTCHER [BOSSES] -180,-60 village`; `Tournament -260,100 castle_and_bridge`; `Challenge 20,50 lamps_on_water`; `Survival -380,-170 swamp`; `Duel [PERIODIC] -100,-220 pink_lake`; `Stranger [HIDDEN]`; `*_INTERMISSION` ×4; `BOSS_HARDMODE`; `SENSEI_MEMORIES -400,-20 village` |
| `ZONE_4` | `Map2.4` | — | 14 | 70 | `BOSS_WASP_LOCKED [FAKE]`; `BOSS_WASP [BOSSES] -180,-60 ships`; `Tournament -260,100 flowers_field`; `Challenge 20,50 dark_room`; `Survival -380,-170 cave`; `Duel [PERIODIC] -100,-220 heaven`; `Stranger [HIDDEN]`; `*_INTERMISSION` ×4; `BOSS_HARDMODE`; `SENSEI_MEMORIES -400,-20 ships` |
| `ZONE_5` | `Map2.5` | — | 14 | 70 | `BOSS_HUNTRESS_LOCKED [FAKE]`; `BOSS_HUNTRESS [BOSSES] -180,-60 fuji`; `Tournament 20,0 volcano`; `Challenge -100,-220 ruins_village`; `Survival -390,-140 battlefield`; `Duel [PERIODIC] -260,100 snowy_peak`; `Stranger [HIDDEN]`; `*_INTERMISSION` ×4; `BOSS_HARDMODE`; `SENSEI_MEMORIES -400,20 flooded_village` |
| `ZONE_6` | `Map2.6` | — | 17 | 76 | `BOSS_SAMURAI_LOCKED [FAKE]`; `BOSS_SAMURAI [BOSSES] -200,-90 burning_town`; `FINAL_BATTLE [FINAL_BATTLE] 57,-55 shadow_gate`; `Tournament -50,-220 ice_cave`; `Challenge -100,100 flying_rocks`; `Survival -400,-200 graveyard_ships`; `Duel [PERIODIC] -300,100 waterfall`; `Stranger+QuestBattle [HIDDEN]`; `*_INTERMISSION` ×5; `BOSS_HARDMODE`; `SENSEI_MEMORIES -400,-30 magic_rocks` |
| `ZONE_7` | `Map3.7` | — | 21 | 114 | `BOSS_TITAN_LOCKED [FAKE]`; `BOSS_TITAN [BOSSES] -120,-80 neural_network`; `MINIBOSS_SHROUD [HIDDEN] factory`; `Tournament 60,0 capsules`; `MINIBOSS_CYPHER [BOSSES]`; `Survival/Survival2`; `MINIBOSS_HYPERION [BOSSES_INTERMISSION]`; `CLIFFHANGER_STONE_FOREST/Challenge/Challenge1_1/MINIBOSS_ANCIENT [..] 80,-200 stone_forest`; `Duel/C3_Duel [PERIODIC]`; `MINIBOSS_SHROUD2`; `C3_Challenge`; `C3_BOSS_TITAN [FINAL_BATTLE_TITAN]`; `C3_Tournament`; `C3_FightWithMay [BOSSES]`; `C3_Survival` |

`st` record (L1431-1432): `{name, fileName, yR(Start), status, index,
bL(RewardDigits), LK(PrizeBaseDigits), Dg[]}` + `xQ(name)`, `a0(type)`,
`Gx()` status recompute. Start zone: `p.V$a()` returns first `uC` with
`yR` (L218) = `Punchbag`.
Map art: `res/map/part0..6` zone backgrounds; `qe.uM=1.5003663003663004`
widget scale constant (L2488); single-widget special case paints `Cga` on
`Map1.2` art (L2122).

### 2.2 `CurrentZone`, unlock rule `WDa`, `MapFocus`, scroller

| Item | Verbatim rule | Line |
|---|---|---|
| `CurrentZone` read | `b=a.attributes.get("CurrentZone"); this.ro=…` in `xf` ctor | L248 |
| `CurrentZone` write | `stb(a){this.ro!=a&&(this.ro=a,this.Cr("CurrentZone",this.ro))}` (+ quest action `oo`, `p.o.stb(name)`) | L274, L1096-1097 |
| Unlock test | `WDa(a){return this.iF.get(a)!=null}` — `iF` = `Battles` map (`hl` records) | L256 |
| Unlock write (first entry) | `J1a(hb)`: find-or-append `<Battle Name="Me\|Re\|">` under `<Battles>`, wrap `hl`, `iF.add` | L259 |
| Unlock write (result) | `Iaa(hb,…)`: `J1a` + `uMa/CMa/gx/yla` flags, link `Uk(hb)→ob`, else `Eja` | L260-261 |
| `MapFocus` read | `b=a.attributes.get("MapFocus"); this.m5(…)` in `xf` ctor | L247 |
| `MapFocus` write | `m5(a){this.ys.gza(a)\|\|(this.ys.kj(a),this.Cr("MapFocus",this.ys.jCa()))}`; `Ttb(a)` alias | L255 |
| MapFocus quest var | `case "MapFocus": b.result=c.ys.jCa()` (quest expression resolver) | L975 |
| MapFocus availability expr | `Available`: `p.Uk(d)!=null ? (p.o.WDa(d)?"1":"0") : "0"` | L981 |
| Focus → map | `Ya.bKa()`: `ue.clear(); ue.sY(); …; Uw(p.Uk(p.o.ys),0)` | L2129 |
| Focus zone strip | `Ya.Uw(loc)`: `ue.Wfa(a)` → `c.Uw(a,b)` + `Ama/Hma` | L2129-2130 |
| Zone widgets | `Vr.sY()`: `HXa` per zone of `p.YBa()`; `HXa`: skip `yR` zones; skip zones whose every `Dg.isActive` is false; else append `qe` widget | L2122-2124 |
| Widget navigation | `Vr.qF(item,dur)` scroll-to; `Vr.Uw(loc)` find widget by `zQ`; `Ya.iqb/eqb/mK` index nav | L2118, L2129, L2139 |
| Boss-gate helpers | `Ya.WEa(zone)`: any `FightBosses/FightFinalTitan/FightBossesIntermission` node `isActive`; `Ya.VEa(zone)`: any completed+unlocked active node | L2131-2132 |
| Zone recompute | `p.Gx()` after changes; `st.Gx()`; `Lc.jab/tt/li/rnb` status bits | L220 (`p.Gx`), L1432, L1406 |
| Node tap → fight | `qe` → `Ya.Uw` → Fight button → `v.Am(battle)` → `wa.mp(6,…)` | L2129, L1216, L934 |

`OPEN (needs runtime trace)`: `hl.uMa/CMa/gx/yla` flag semantics per
result; `Lc.hMa` win/loss recording details (L1406) vs `Iaa`; `Gb`
periodic timers (`Tma/sob/cla/reset`, L1409-1411) live countdown behavior;
`qe` widget layout/pointer hit-testing (`Bd/zQ/GT/$Va`, L2140-2141).

---

## 3. Full save format (`SF2User`)

### 3.1 Storage envelope

| Item | Fact | Line |
|---|---|---|
| Keys | `Aa.WU="SF2User"`, `Aa.Y6="SF2Packs"`, `cg.P6="SF2Flags"` | L2462 |
| Exists check | `Aa.Ue()` = `DD(WU).load().value!=null` | L70 |
| First launch | `(BJ=!Aa.Ue())&&Aa.init()`; `Aa.init` clones XML asset 9 (`users_default`), stamps `Versions/HaxeVersion`, `Aa.save` | L65, L70 |
| Load | `Aa.load()`: `DD(WU).load()` → `ri.decode` (base64) → `cd` un-zstd (`kb.f3`) → `Rb.parse` XML | L70-71 |
| Save | `Aa.save(xml)`: `stringify` → `kb.f3` compress → `ri.encode` → `DD(WU).save` | L71 |
| Backend | `Ca.R1a(name)` = `Ck` → `window.GameInterface.storage` (localStorage `famobi:savegame`) | L34-36 (JS_FLOW §4) |
| Export | `Aa.Ddb/Dpb`: `.sf2` file = `"SF2"+base64(users+packs+flags)` | L71-73 |
| Flags | `Aa.flags` (`cg`), `SF2Flags` JSON | L2462, L68-69 (JS_FLOW) |
| Boot parse | `p.Edb→Xjb`: `EB=Aa.load()`, `CurrentUser/Warriors`, `GameLaunchIndex++`, `LastSaveSlot/SaveSlot/ID/IsFake`, `Qjb(warrior)→new xf` | L200, L203, L178 |

### 3.2 `<Warrior>` attributes → `xf` fields (L245-255)

Seed file: `reference/extracted/xml/res/users_default.xml` (= `reference/www/res/users_default.b7da2019.xml`):
`Money=0 Bonus=50 Level=1 Experience=0 Power=5 Tutorial=MOVE CurrentZone=ZONE_1`,
`Battles: ZONE_1|BOSS_LYNX|`, `Resistances Resistance_2=0`, `Versions 1.0.13`.

| XML attr / node | `xf` field | Lines |
|---|---|---|
| `Money` | `Tb` (`Fr` write + `hL("Money")`) | L246, L251-252 |
| `Bonus` | `fd` (`vl` write + `hL("Bonus")`) | L246, L252 |
| `PaidMoney/PaidBonus` | `hC/WN` (`IMa/HMa`) | L246, L252-253 |
| `DenominationDigits` | `kq` | L246, L255 (`xtb`) |
| `CoinIcon` | `Vf` (`mtb`, strips `MiscSprites.`) | L246, L255 |
| `Level/Experience` | `Ca.level/rs` (`OLa/Jab` level-up curve, `Oz` thresholds from `v.FR`) | L247, L253-254 |
| `Power/PowerSyncTime` | `dk/$N` (cap `wr=v.$Ca()`, `F5`) | L247, L254 |
| `FightIDS` | `XG` (`hb`; `Qtb/brb`) | L247, L255 |
| `MapFocus` | `ys` (`hb`; `m5/Ttb` + `Cr("MapFocus")`) | L247, L255 |
| `ShowUpgrades` | `qC` (`qub`) | L247, L255 |
| `CurrentZone` | `ro` (`stb` + `Cr("CurrentZone")`) | L248, L274 |
| `MapMaskColor` | `SUa` | L248 |
| `Tutorial` (+zi block) | `zi` (`zt.parse`; `HH` = `StoryTutorialStep`, L964) | L248 |
| `Items/Item(Name,Equipped,Count)` | `xa` (`$g.parse(h2, level)`) | L248 |
| `Battles/Battle(Name)` | `iF` (`At`; `hl` via `lWa`, L259) | L248-249 |
| `Fights/Fight` | `yc` (`il` via `uq/eya`; `Sq` lookup; `no` win count) | L249, L258-260 |
| `Shop/Lock(Name)` | `vq` set | L249 |
| `Quests/Quests/Quest + Variables` | `kF` (`Et` via `SIa`, L262), `rv` vars (`wkb`) | L249-250 |
| `OpenTricks/Trick(Name)` | `Nua` set | L250 |
| `ActiveLotteries` | `o7` (created if absent) | L250 |
| `Timers` | `yl` (`Ct`) | L250 |
| `Currencies` | `pG` (`Jia`; `Hua` per-currency) | L250-251 |
| `Payments` | `fX` (`Qia.Njb`) | L251 |
| `Advertising` | `Loa` | L251 |
| `Resistances` | `Pw` (`Dt(jy("Resistances"))`) | L251 |
| `Offers/Offer` | `QN` map (`Ldb/Wyb`) | L251, L257-258 |
| `EclipseMode` | `Yh` (On/Off) | L248 |
| `PeriodicPlayTime` | `b$` (`JMa`) | L248, L254 |
| `StarterPackTimerEndTime` | `yVa` | L248 |
| Any write | `Cr/nF/hL/gka` → `ga.set` + `save()` | L157 (JS_FLOW §4), L251-255 |

`OPEN (needs runtime trace)`: multi-slot switch (`zkb/D9a`, L200);
`GameLaunchIndex/LastSaveSlot` increment rules; `Ct` timer live countdown
(`yl.gJ/Uaa/H4`, L185/L1025); offer/lottery/ad state transitions.

---

## 4. After-battle rewards (`v.kD` formula)

### 4.1 Call chain into `kD`

| Step | Fact | Line |
|---|---|---|
| Round end detect | `ca.Onb()` → `E3a` round apply (`ng++` wins, `zd/Iq/kh`, `ze` tracker) | L411-413 (JS_FLOW §5) |
| Battle end | `ca.bea` → `Ca.bea(gameOver)` / `Ca.K4a(gameComplete)` (GameInterface) | L413-414, L34 |
| Final handler | `ca.TYa()`: `fe.complete`, HP snapshots, `v.JFa(...)` + `v.kD(a, Da, …)` | L424 |
| Pre-end quest event | `v.flb(a,b,c)`: `ta.Nb/Qv(Win/Loss/Surrender)/J_` → `Sf("QUEST_EVENT_FIGHT_PRE_END")` | L1213 |
| Lose → map | `v.qxa(f)`: `type!="FightPVP" && wa.F().mp(5)` | L1213 |
| Start fight | `v.Am(a,…)`: `Qtb(XG)`, `QUEST_EVENT_FIGHT_ENTER`, `qZa(-d4)` power gate, `Wc.Cla` timer, `mp(6,a)` | L1216 |

### 4.2 `v.kD(a,b,c,d,e,f,g,h)` verbatim (L1213-1215)

```
h??=0; g??=0
k = (d==null && f==null)          // k: show-next (no explicit next-battle args)
l = (b==null || e==-1)            // l: surrender/no-battle
n = p.o; b==null && (b=p.Wv(n.XG))
f = new hp; f.Da=b; f.type=b.type
q = b.Nb; r = new hb; r.wm(q.Me,q.Re,q.Lq); f.Mf=r
f.nB=c; f.z2=d; f.qD=e; f.$3a=h
c = b.PU; f.zd() && ++c            // zd() := qD==1 (L1239); PU = consecutive-win counter
b.sR() && (c = f.zd()?1:0)
f.pwa(b.D0(c), a, b)               // resolve reward bundle into f.hj (see §4.3)
c = false
if (!l) {
  b.lg.hMa(b, f.zd())              // node progress record (Lc L1406 / Gb L1410)
  b.type=="FightPeriodic" && f.zd() && (lg.update, Wc.cea(), Gb.cla(wH), v.n1a(...))
  b.type in (FightReplayable|FightBossesReplayable|FightFinalReplayable) &&
    lg.eJ()==1 && p.pXa(lg)        // replayable bookkeeping
  c = p.F().dmb(f)                 // §4.4 apply; c = leveled-up flag
}
ta.Nb=b.Nb; ta.Qv = f.zd()?"Win":(l?"Surrender":"Loss"); ta.t2=c?1:0; ta.J_=g
g = any(b.wi[].bm(level).Yj != null)   // unclaimed sub-fight loot?
n_ = (b.type=="FightRaid")
if ((g=g&&f.zd())) ha.F().Flb = ta     // loot screen journal
if (!g && ((!n_ && ha.F().RA("QUEST_EVENT_FIGHT_END")) ||
           (n_ && ha.F().RA("QUEST_EVENT_RAID_FIGHT_END"))) && k) ha.F().qT()
p.o.save()
v.Qbb(Nb.toString()) && f.zd()         // achievement counters
k ? p.$wa()                            // victory w/o explicit next: auto replays ($wa L221)
  : b.type!="FightRaid" && (l ? v.qxa(f)            // surrender → Map
                            : (b=ca.Ka(), b!=null && b.zia(f)))  // next foe/round
v.mgb.Z()
```

### 4.3 Reward resolution `hp.pwa / Sua` (L1240-1241)

```
pwa(rewards, prevEe, charge):
  if rewards==null: Ee = prevEe
  else:
    hj.clear()
    d = p.o.Yh ? a.Yya : a.XGa        // eclipse vs normal reward branch
    e = true
    if (Z_.bm(level).ph>0 || d?.bm(level).ph>0 || charge.ph>0) e = false
    Sua(Z_, charge.ph, prevEe, e); Sua(d, charge.ph, prevEe, e)
Sua(branch, ph, prevEe, d):
  e = branch.bm(level)                // level-scaled reward entry
  ap += trunc(e.exp)
  c = e.Tb                             // coins
  prize = trunc(e.ph>0 ? e.ph : (ph>=0 ? ph : (d ? ceil(c * v.hF.xya) : 0)))
  bzb(prize, c, e.Uo, v.hF.$Ia, v.hF.ep, v.hF.Ui, v.hF.Ub, v.hF.pk)
  hj.Tb = N5a() (= Ee.oc.m6); hj.Uo = M5a() (= Ee.oc.mOa); hj.exp += e.exp
  hj.Saa/Wua/Vua/Xua: merge P2/Ok/yr/Yj/items/rZ sub-rewards
```

`RewardsPrize` factors (`gw`/`v.hF`, L1192-1193; XML `internal_settings.xml`
`<RewardsPrize>`): `DefaultPrizeBaseFactor xya=0.003`, `Perfect $Ia=5`,
`FirstStrike ep=2`, `ComboCount Ui=1`, `HeadShot=0` (parsed, unused),
`Shock Ub=3`, Styles `pk=[Turtle 0, Hard 3, Brutal 6, Agressive 9, Crazy 12,
Fantastic 15]`.

### 4.4 Reward apply `p.emb` (L185-186)

```
emb(hj):
  d = p.Iab(hj.exp)          // → p.o.Jab → OLa level-up curve (L253-254); d = leveled-up?
  hj.Tb>0  && p.Fwa(Tb)      // coins  (L218)
  hj.Uo>0  && p.Ewa(Uo, 3)   // gems   (L218)
  FAa (currencies, LZ==0) → p.o.Hua(currency, count)
  F8a (resistances)       → p.o.Pw.yWa(pw, count)
  items[] → p.Ky(item,1,…) (stack + mY level-scale); ml (perk-part) → Ky(ml,…)
  p.o.save(); return d
```

Level-up `OLa` (L253-254): `rs+=exp` vs `Oz()` thresholds (`v.FR` from
`character_progress.xml`); on cross: `level++`, `xa.vu/Ryb` unlocks,
`Oa.Lma`, `p.Yyb`, `oS` event; `S6a` (L1212) builds the level-hint dialog.

`OPEN (needs runtime trace)`: `D0(c)` per-battle reward-table select;
`bm(level)` level-scaling curve; `Ee.oc.m6/mOa` (N5a/M5a) context values;
`b.wi` sub-fight loot conditions; `Qbb/X4a/Cpb` achievement-counter side
effects (L1202); `zia` next-foe select; `Yxa/S6a/mgb` presentation.

---

## OPEN (needs runtime trace) — consolidated

1. Quest pump dynamics: `Dh[]` priority-tie order, `Yb` modal/`Wait`-frame
   gating, `Activate`-delay path, `be.Mbb/compare` edge cases (L1017-1019).
2. Node progress flags: `hl.uMa/CMa/gx/yla`, `Lc.hMa`, `Iaa` vs `Eja` paths
   (L259-261, L1406).
3. Periodic/replayable live timers: `Gb.Tma/sob/cla/reset/Vab`, `Wc.cea/ptb`
   (L1409-1411).
4. Reward-table select + scaling: `D0`, `bm`, `Ee.oc`, `wi` loot (L1240-1241,
   L1214).
5. Multi-slot/launch-index/timer countdown persistence (L200, L250).
6. `qe` widget pointer/loading behavior; `Vr` fling physics constants
   (L2117-2121, L2140-2144).

---

## RESOLVED (round 5 research sweep)

### R1. `block_lesson` var (Stream 3 quest beat 8) — RESOLVED: no such var

`block_lesson`/`BlockLesson`/`lesson` have **0 hits** in JS and 0 in
`tutorial_quests.xml`/`quests.xml` (only `fight21_lesson_in_the_dark_room`
music ids, L1275/stages.xml). There is no lesson variable. Beat 8 in file
order = **`StoryTutorialGoToDojo`** (`SHOW_DOUBLE_SWEEP`, `SceneTo!=Dojo`,
`SceneFrom∉{None,Loader}`); its only vars are
`SenseiDialogText=tutorial_dojo_new_move` + `NextScene=Dojo` (+ `Activate
StoryTutorialOpenScene`). The block lesson beat = `StoryTutorialShowBlock`
(beat 12: step `SHOW_BLOCK` + `SceneTo==Profile` → step `END`); its action
`Fo` (L1126-1127) shows the profile-moves overlay and sets **no vars**.

### R2. `Yb` modal / `Wait` gating + `Activate` delay — RESOLVED (static)

- `Yb` (L954): strict sequential chain (`Vl` push, `S` run, `gf` advance on
  `qd` event). No branching — order is static.
- `Wait` = `Ro` (L1113-1114): `Frames` + `ControlsLock` attrs; counts `hc`
  per-frame ticks (`L.K.Oh.ci(nr)`), locks touches (`Sb.F().kk(!0)` +
  `za.enabled=!1`) while waiting, unlocks at `hc>=Oqa`.
- `Activate` = `Ge` (L1023-1024): `ActionID→Goa`; `S` fires
  `RA("QUEST_EVENT_ACTIVATE")` synchronously (`Ge.MZ` set/clear around it).
  `EActivateTimer` = `yj` (L1024-1025): named save-timer set/cancel
  (`yl.Uaa/H4`, `RN` EndTimer L1069).
- `DelayBeforeOpenScene` (tutorial `30`): **0 JS hits** — consumed
  quest-side via `_$DelayBeforeOpenScene` expressions in the
  `StoryTutorialOpenScene` template. OPEN-KEPT only for live modal stacking
  (`Sb.F().kk` queue behavior under overlapping dialogs).

### R3. `hl` progress flags — RESOLVED (L276-279)

`hl` ctor: `Ak` battle id (`hb`), `zo` Locked, `d9` Hidden, `UV/VV`+
`fv/Qm` random seeds, `D9` EndTime, `zH` ReplayCount, `Sqa` Fight flag.
Writers (all static): `uMa` rename, `CMa` lock-set, `gx` hide-set,
`yla` replay-set, `wla/vla` seeds, `l$a` EndTime-now (`D9-p.Dc`,
`debugger`-guarded), `P9a` seed query. `Iaa` (L260-261) drives
`uMa/CMa/gx/yla`; `Lc.hMa` records win/loss (L1406). OPEN-KEPT only for
live win/loss sequencing across replayable replays.

### R4. `Gb` periodic timers + `il`/`Ct` persistence — RESOLVED (static)

- `il` fight record (L279-282): `yG` IDS, `no` CompletedCount (`ptb`
  reset), `FW` LossCount, `eN/uqa` eclipse variants (`ytb/Gab/Hab`),
  `Gs` TimeLeft (`Cla` write), `wH` RandomizeTimeLeft (`xla`),
  `ZB` Level (`xL`), seeds.
- `Gb extends Lc` (L1409-1411): `setTime/Tma` (Cla now + save),
  `sob` (sweep all to played), `cla` (static fan-out), `reset/Vab`
  (zero + `JMa(0)`), `update` (Tma tick), `hMa` (win record).
- `Ct` save timers (L291): `Uaa/BXa/bva` set, `gJ` get, `H4` clear,
  persisted under `<Timers>` (L250).
- Launch/slots (L199-200): `GameLaunchIndex` read-increment-writeback;
  slot select by `SaveSlot+ID` skipping `IsFake`; `zkb` stamps
  `SaveSlot=tT`; `Tmb` strips Token/UseNewHash. OPEN-KEPT only for the live
  tick driver (who calls `Gb.update` per frame) and slot-switch UI.

### R5. `D0` / `bm` / `Ee.oc` / `wi` — RESOLVED (exact lines for Stream 1)

- `fight.D0(a)` (L1420): `wi[a]` by PU index (`Clb` pushes sub-fights;
  `wi` = `Lc.wi`).
- `wt.D0()` (L232-233): weighted draw over `U7` weights via `Da.pg.dT`
  (`xt`: Item→`Zg`/Money→`Di`/Currency→`Ei`/Resistance→`Fi`/Lottery→`Gi`).
- `eg.bm(level)` (L230-231): merge `npa` + `Msa` level ranges → `Yg`
  (`Tb/Uo/exp/ph×10^digits`, `P2/Ok/yr` sublists, L240-241);
  `tt.bm` picks Normal/Eclipse branch (L231).
- `hp.Ee` = `Fh` fight context (L1239-1241: `Sua` sets `Ee=c`; `bzb`
  no-ops when null). `bzb(a..h)` → `Ee.lXa` — see COMBAT_STATIC App. B
  `bzb` entry for the full arithmetic.
- `$L` (L1425): last-`wi` margins (`h4/g4` coin/gem).

### R6. `qe` / `Vr` constants — RESOLVED (L2117-2124, L2140-2141, L2488)

`Vr.qF` scroll-to (`ub=0`, ease `sLa=.1/(dur/.6)`); fling friction
`ub*=.9` (L2120); state machine 0 idle / 1 drag (`Kx` 10px threshold) /
2 snap (`GR` lerp .3); clamp `[size-b+100, 0]` + overshoot 100 (L2120-2121);
`cCa` content width (`background.fa.x` sum, L2123); `HXa` skip-`yR` +
skip all-inactive (L2124); `sY` single-widget `Cga` on `Map1.2` (L2122);
`qe.uM=1.5003663003663004` (L2488); `qe.Bd/zQ/GT/$Va` (L2140-2141).
OPEN-KEPT only for live pointer positions (inherent runtime input).

### R7. `.sf2` export join — RESOLVED: no separator (L72-73, L2333)

`Dpb`: `Ug` frames `ke(len)+yna(bytes)` × (users, packs) + `$p` flag bits
(H1/VF), base64'd with `"SF2"` prefix. `ke` = u32 LE/BE (L2333);
`yna` = zstd-compress (`kb.f3`). Import `Ddb` reads `Yt(ti)` chunks
(L71-72). Join = length-prefixed frames, no text separator.

### R8. `Wins` / `Count` attrs — RESOLVED: don't exist

0 hits in JS, `stages.xml`, `quests.xml`, `tutorial_quests.xml`. The
win/loss counters are `il.no` = `CompletedCount` (`ptb` reset) and `il.FW`
= `LossCount`, plus `eN/uqa` eclipse variants (L280-282). If Stream 1 meant
other attrs, cite the exact file — no further static candidate exists.

### OPEN-KEPT (round 5)

- `Bz` per-frame segment values: formulas static (COMBAT_STATIC §A9);
  needs a runtime trace of `Nl.oI` (`Ula/Pda/gb/Eda/vZ`) per frame +
  `zXa` hit points to close.
- `Gb.update` live driver + slot-switch UI (R4); modal-stack dynamics (R2);
  replayable win/loss sequencing (R3).

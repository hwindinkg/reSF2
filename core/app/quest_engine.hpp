#pragma once

// Quest engine core (app scope) — data-driven quest machine.
//
// Spec: the JS quest loader/engine (`Fe.Ij`/`ha`/`Bj`/`Yb`, event/condition/
// action tables) over the real shipped tree. The ROOT is
// `reference/extracted/xml/res/quests.xml` (the same extracted-res source
// `stages.xml`/`list.xml` resolve from): its `<Quest>` children register
// inline, its `<Include File="a.xml|b.xml">` children load further files
// (each `|` alternative that exists, in order, after evaluating the
// Include's own `<Conditions>`), and a running quest's
// `<AttachQuestFile File="x.xml">` action loads a file at that point
// (JS `Bn.S` -> `p.F().L3`).
//
// What this IS: QuestDef parse (Quest/Events/Conditions/Actions incl.
// nested If/Dialog/Button children), session latch for Unresumable="1",
// condition eval (Equal/Greater/GreaterEqual/Less/LessEqual + Not and
// And/Or nesting, `_$` journal vars + the `?`-queries the shell can
// answer; an operand the shell cannot answer makes the condition
// UNKNOWN — the quest does not fire and the query is logged, never
// silently true/false), and runners for the APP-SCOPED actions only —
// save/UI writes (so/to/qo/oo/SetVariable, queue clears) plus RECORDS for
// everything else (scene/fight/click/dialog/minigame requests are logged,
// never executed: no auto-navigation, no auto-fights, no auto-clicks —
// headless-safe by construction).
// Async semantics (Wait Frames, Dialog modal gating, Activate delays,
// `Dh[]` ordering, `be.Mbb` gates) collapse to synchronous runs — noted.
//
// Journal (Bj analog): story_step comes from the LIVE save at fire time;
// scene_to/from from ScreenManager push/pop; fight triple from FightEnd.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "app/item_catalog.hpp"
#include "app/save_system.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

class App;

// Journal for one event firing (JS `ha.ta`/`Bj` readable subset).
struct QuestJournal {
    std::string scene_to;     // JS `Bj.nLa` (`_$SceneTo`; `wa.mp` L933)
    std::string scene_from;   // JS `Bj.lLa` (`_$SceneFrom`; `wa.mp` L933)
    // JS `Bj.XNa`/`Bj.YNa` (ctor L1005; `_$TabFrom`/`_$TabTo` read L964). Set
    // by `v.qwa` (L1212: `c.XNa=uh.getName(a); c.YNa=uh.getName(b)`) on every
    // screen change, from the `vj.E0` tab names. Distinct from the scene pair.
    std::string tab_from;     // JS `Bj.XNa`
    std::string tab_to;       // JS `Bj.YNa`
    std::string fight;        // last fight name ("Punchbag|Bosses|1" or bare)
    std::string fight_result;  // "Win" / "Loss" / ""
    std::string fight_zone;   // resolved zone ("" when unknown)
    int player_level = 1;
    std::string action_id;  // Activate ActionID
    // JS `Bj.Av`/`Bj.yYa` (ctor L1004 `this.yYa=this.Av=...=""`): the map-button
    // press payload. `Vb.Qg` (L2173) writes `ta.Av = name` before it fires
    // `QUEST_EVENT_MAP_BUTTON_PRESS`; the condition resolver maps
    // `_$ButtonName` -> `ta.Av` (L960) and `_$ButtonType` -> `ta.yYa`.
    std::string button_name;  // `_$ButtonName`
    std::string button_type;  // `_$ButtonType`
    // JS `Bj.item`/`Bj.I_` (ctor L995; `_$Purchase` reads `item.name` at L963,
    // `v8a` L989 reads `item.name`+`I_`). `Pa.Wz` (L1234) writes `item` then
    // fires `QUEST_EVENT_PURCHASE`; `Pa.Bv` (L1211) writes `item`+`I_` then
    // fires `QUEST_EVENT_PURCHASE_UNSUCCESSFUL` (`I_` = the failure reason).
    std::string item;              // `_$Purchase`
    std::string purchase_failure;  // `_$PurchaseUnsuccessful` suffix (`I_`)
    // JS `Bj.eHa` (the "received offer item" journal): `_$Offer` (L494107)
    // reads `this.ta.eHa != null ? eHa.ab() : ""` — the offer NAME whose
    // item was just received (`OfferItemRecieved`, `G_` L513220 maps it to
    // `QUEST_EVENT_OFFER_ITEM_RECIEVED`). Set by `offer_purchase`.
    std::string offer;             // `_$Offer`
};

// Condition node (leaf comparison or And/Or operator). Leaf kinds mirror
// the JS `yb.tD` tag table: Equal/Greater/GreaterEqual/Less/LessEqual.
struct QuestCond {
    std::string kind = "Equal";  // leaf kind, or "And" / "Or" operator
    bool invert = false;         // Not="1"
    std::string value1;
    std::string value2;
    std::vector<QuestCond> children;  // Operator branches
};

// Action node (tag + attrs; If/Dialog keep structured children).
struct QuestAction {
    std::string tag;
    std::map<std::string, std::string> attrs;
    QuestCond if_cond;                     // If/Conditions
    std::vector<QuestAction> if_then;      // If/Then
    std::vector<QuestAction> if_else;      // If/Else
    std::vector<QuestAction> children;     // Dialog lines/buttons/nested acts
};

// One quest definition (JS `be`).
struct QuestDef {
    std::string name;
    int priority = 0;
    bool unresumable = false;
    std::vector<std::string> events;  // ChangeTab/SceneLoaded/Activate/…
    // `<Actions Place="…">` (JS `be.Gib` L1007: `this.k7=be.ifa(Place!=null?
    // Place:"Map")`): the scene the action SET belongs to. 0 = no Place
    // authored (ungated — the JS reads `k7` as the checkpoint scene `Faa`,
    // never as a fire gate, so only an EXPLICIT Place creates a gate).
    // Otherwise the `be.ifa`/`xn.jOa` screen id (`scene_id_for_name`): Fight→6,
    // Dojo→3, Map→5 (the port's resolver also answers Shop→4).
    int place = 0;
    QuestCond root;                   // AND of top-level Conditions
    std::vector<QuestAction> actions;
};

// JS `vh` (L1063): one `<Button>` slot of a `He` dialog. `He.Rib` (L1057-1058)
// files each nested `<Button Type="...">` into its own slot — Left→`Ng`,
// Middle→`Nh`, Right→`rh`, Close→`Hj` — each carrying its own caption, colour
// and deferred actions. The port folded every `<Button>` into the single
// Right slot (`button_actions`/`button_text`), stranding the 159 Left / 16
// Middle / 1 Close shipped slots.
struct EngineDialogButton {
    std::string text;                 // `vh.text` (`Button Text`)
    std::string color;                // `vh.color` (`Button Color`)
    std::vector<QuestAction> actions; // `vh.actions` (deferred to the press)
    bool hint = false;                // `vh.aA` (`Button Flashing`)
};

// Structured dialog record for the Sensei modal (He display lives in
// screens.cpp; the engine only queues).
struct EngineDialog {
    std::string type;   // Notification / Regular
    std::string title;  // characterSensei / boss_lynx / ... (lang key)
    std::string image;
    // Resolved Line texts. Each entry is a runtime lang key (`tutorial_move`,
    // `tutorial_training_fight`, ...) — NOT pre-resolved, so the display
    // layer resolves it after the lang table is loaded (JS `ba.cg`/`ba.Fz`
    // resolve at He.S time; the port queues before boot renders).
    std::vector<std::string> lines;
    // Per-row advance captions (`ButtonText`), parallel to `lines`. JS
    // `He.jkb` (L1042) stores the caption on the ROW: a `Regular` dialog with
    // several `<Line>` rows pages through them, and each page's caption
    // labels the advance plate. In the shipped tutorial the two-row Lynx
    // dialog is `dlgStoryBtnMore` (row 1) / `dlgStoryBtnFight` (row 2,
    // tutorial_quests.xml L159-160).
    std::vector<std::string> line_buttons;
    // Current page (JS `He` line cursor). 0 for a single-row dialog.
    std::size_t page = 0;
    // The dialog button's caption (the `Right` `<Button Text>` when authored,
    // e.g. sensei_arc.xml L59, else the LAST row's `ButtonText`: the page
    // that carries the nested actions, `hab()` L1060).
    std::string button_text;
    // The Right button's `<Button Color>` (JS `vh.color` via `He.Rib` L1057).
    // The frame is `nz.hi(color)` (`He.lea` L1063): Red→Dark, Green→Green,
    // White/Beige→White, Gold→Gold.
    std::string button_color;
    // The Right button's deferred nested actions (JS `vh.actions`, run on
    // press via `He.dhb(1)` L1061). Empty = no button (Notification OK with
    // no actions, `hab()` false).
    std::vector<QuestAction> button_actions;
    // `He.Rib` L1057-1058: an authored `<Button Type="Right">` (or a bare
    // `<Button>` with no `Type`) creates the `rh` slot even with NO nested
    // actions. `Xc.Xhb` L1047 always passes that slot to `Od` (`e`/`k`); `hab()`
    // L1060 gates ONLY the Notification OK plate (L1050). tutorial_quests.xml
    // L112 (`<Button Type="Right" Color="White" />`) is exactly this: a
    // `Regular` whose plate the port dropped because `button_actions` was empty.
    bool has_right_button = false;
    // The other `He` slots (`He.Rib` L1057-1058): Left→`Ng`, Middle→`Nh`,
    // Close→`Hj`. `dhb` L1061 dispatches them by index (0/2/100); the Right
    // slot above is index 1.
    EngineDialogButton left_, middle_, close_;
    // `He.L` L1044 `MinContentHeight` -> `Od.cv` L1944: the floor `od.layout`
    // L1898 applies to the measured content height (`Od.lj` L1950
    // `Math.max(kb.ew(), this.cv)`).
    float min_content_height = 0.0f;
    // --- D10 dialog attrs (`He` L1043-1045 parse, `He.S` L1051 apply) --------
    // `Mirrored` -> `n4a` L1043, applied L1047 (`this.n4a&&(r+="|Flip")`, so the
    //   parse appends "|Flip" to `image`; `v.RIa` L1222 detects it).
    // `ImageScale` -> `iy` L1044, applied L1947 (`Zg.la(1.8*this.iy)`).
    // `ContentOffsetX` -> `TM` L1044, applied to the content x (`Jva` L1951).
    // `ImageOffsetX/Y` -> `OB`/`YV` L1044, applied L1051 `VLa`/`WLa`.
    // `TextOffset` -> `ov` L1044 (`Xy`), applied L1051 `mMa` + `eba` L1950.
    // `TextPosXByImage` -> `LH` L1045, applied L1051 `nMa` (`Jva` L1951).
    // `BlockRaycast` -> `$Ta` L1044, the `Ib.Qhb(...,h)` L1050 dim gate.
    // `DisableNotificationsButtons` -> `qUa` L1044 -> `Ib.RP` (L1045/L1050),
    //   gating the notification OK plate (`Ib.Sr` L1910 `Ib.RP?b=!1:...`).
    bool mirrored = false;
    float image_scale = 1.0f;
    float content_offset_x = 0.0f;
    float image_offset_x = 0.0f;
    float image_offset_y = 0.0f;
    float text_offset_x = 0.0f;
    float text_offset_y = 0.0f;
    bool text_pos_x_by_image = true;
    bool block_raycast = true;
    bool disable_notifications_buttons = false;
    // D8 `He.SK` L1043 (`u.H(ReadTime, ge.ZGa)`): the `Ib` bar's auto-dismiss
    // budget in seconds (`Ib.aa` L1905 `this.SK-=a; this.SK<=0&&(this.qma=!0)`
    // then `OZa` L1908 -> `y4(!1)`). Zero disables the countdown.
    float read_time = 0.0f;
    // `He.ah` L1045 `Item` -> L1046 `x=ba.Pc(a,this.ah); x=p.items.$b(x)`, whose
    // `Ev` composite the portrait prefers (`Od.$A` L1945 `new or(this.sV)`).
    std::string item;
    // `He.L` L1044 `Loot` -> the `ShowLoot` type's item list (`Xc.Uhb` L929:
    // `ba.Pc(a,this.IN).split("|")`, offers.xml is the only shipped one).
    std::vector<std::string> loot;
    std::string quest;               // firing quest name
    QuestJournal journal;            // `Qt` (He.S stores the firing journal)
    // D1 `He.S` L1051: a widget-building Type assigns `C != null`, so the
    // final `C!=null?...:(Ib.RP=!1,this.sa())` never runs `this.sa()` — the
    // serialized `Yb` (L954) PARKS its remaining actions here. `He.gf` L1062
    // (`gf(){Ib.RP=!1;this.sa()}`) resumes them on dismissal, AFTER the
    // pressed button's nested `Yb` (`He.dhb` L1061) completed. The tail lives
    // on the dialog (not in `pending_`) because the resume trigger is the
    // dismissal, not a frame count.
    std::vector<QuestAction> continuation;
    std::map<std::string, std::string> continuation_locals;
    // `He.jkb` (L1056): the row content type — `PriceLine`->1, `LineButton`->2,
    // every other row tag (`Line`/`DeliveryDelay`)->0. Parallel to `lines`.
    std::vector<int> line_content_types;
    // `He.jkb` (L1056-1057): a row carrying `Item`/`Enchantment` becomes a row
    // button (`tv`, pushed to `this.ima`) whose nested actions are its own
    // sub-`Yb`; `He.dhb` (L1061 `a<this.eOa`, ids from 5) dispatches it by id.
    // The port keeps one action list per row (parallel to `lines`) and
    // `press_dialog` runs it by row id. Before this the nested `<GiveItem>` of
    // a `DeliveryDelay` row was dropped entirely (a real divergence).
    std::vector<std::vector<QuestAction>> line_actions;
    // `He.gjb` (L1058): `DifficultyOf Fight` -> `this.Yca` (the resolved fight
    // triple). `He.Gz()` resolves it and returns the difficulty number.
    std::string difficulty_fight;
    // `He.Wib` (L1058-1059): the `CheckBox` row (`uv`, `this.Gg`).
    // `InitialValue`/`Text` are parse attrs; `<On>` -> `kY`, `<Off>` -> `jY`
    // (`He.dhb` L1061 `a==3`/`a==4`). `align_middle` mirrors `YO`.
    struct CheckBox {
        std::string text;
        std::string initial_value;
        std::vector<QuestAction> on;
        std::vector<QuestAction> off;
        bool align_middle = false;
    };
    bool has_checkbox = false;
    CheckBox checkbox;
};
// One `<Battles>` write a quest action asks for. JS mapping:
//   ShowBattle            -> `Aj(true)`  L1108 -> `Iaa(hb,true,true,..)` +
//                            `hl.yla` — ensure the record, set flags;
//   HideBattle            -> `Aj(false)` L1108 -> `Iaa` `c=false` -> `Eja`
//                            L261 — drop the record;
//   SetBattleVisibility   -> `no` L1096 -> `Iaa(hb,false,true,false,hidden)`
//                            — ensure + set Hidden (`IsVisible<=0` => hidden);
//   ToggleBattle          -> `Ho` L1106 -> `hl.gx(!li)` — flip Hidden only
//                            when the record already exists.
struct QuestBattleWrite {
    std::string zone;            // resolved `hb.Me`
    std::string name;            // `hb.Re`
    bool remove = false;         // HideBattle -> `Eja`
    bool toggle_hidden = false;  // ToggleBattle -> flip `hl.gx`
    bool locked = false;         // `Kra` (Iaa `d`)
    bool hidden = false;         // Iaa `e`
    int replay_count = 0;        // `yla` (Iaa `f`)
};

// `Gn` `ChangeScene` (L1032 `S` -> `qIa` -> `wa.F().mp`): the destination the
// engine navigates the ScreenManager to. `reopen` mirrors the `ReopenScene`
// attr (`this.bta`, L1031: navigate even when the target IS the current
// scene). The engine skips the push when the target already is on top.
struct QuestSceneRequest {
    std::string destination;  // resolved `xn.jOa` name (Dojo/Map/Shop/Profile)
    bool reopen = false;      // `ReopenScene="1"`
};

// `go` `OpenShop` (L1092): `mp(4, new Gj(tab, item))` then `Oa.uLa(tab,item)`
// (`f5(tab)` + `Za.SA(item)`, L1181866) — open the Shop, set the `vj.E0` tab
// and select the item by name.
struct QuestShopOpen {
    std::string tab;   // `vj.E0` category name (Weapon/Armor/Helm/…)
    std::string item;  // items.xml Name, or "" (tab only)
};

// `Hn` `ChangeTab` (L948 factory `case "EChangeTab"`; `S` L1032-1034): resolve
// the `Tab` attr (`ba.Pc` -> `vj.E0` index L1168 -> `vj.ifa` screen id L1169)
// and select that screen's tab when it is the current screen (`wa.F().Td.Tf
// == this.CX`). Cases (L1033): 4 Shop `Oa.ska(Cj.l6(Ay), jN)`; 5 Map
// `Ya.rF(Ay)` (an EMPTY stub, L1096890); 7 Profile `vb.rF(To.hOa(Ay), jN)`.
// When the screen controller is not live the JS arms the `wa.F().Qf`
// screen-change listener; the port constructs screens synchronously at push,
// so the owner is always live once `screen_id` matches.
struct QuestTabSelect {
    std::string tab;    // resolved `vj.E0` category name (`Hn.cua`)
    std::string focus;  // `Hn.jN` (`Focus` attr)
    int tab_index = 0;  // `Hn.Ay` (`vj.E0`)
    int screen_id = 0;  // `Hn.CX` (`vj.ifa`)
};

// `Yn` `GiveItem` (JS factory `EGiveItem` L485299; `Yn.S` offset 554285 ->
// `Pa.W$a` L631756 -> `bDa`): grant one item. `Name` is `ITEM` or `ITEM|count`
// (`K.parseInt`); `Quantity` (`c`) adds copies, `PutOn` (`d`) equips. The
// shipped promo forms are `Name="ITEM|100*level+230"` with no other attrs.
struct QuestGiveItem {
    std::string name;      // resolved `Name` (the `|`-split left part)
    int count = 0;         // `|count` (`b`); `>0` -> the stack/upgrade count
    int quantity = 0;      // resolved `Quantity` (`c`)
    bool put_on = false;   // `PutOn` (`d`)
};

// Side effects of one run: save writes (applied) + records (logged only).
struct QuestSideEffects {
    bool has_story_step = false;
    std::string story_step;
    bool has_map_focus = false;
    std::string map_focus;
    bool has_current_zone = false;
    std::string current_zone;
    // `to` (SetVariable, `to.g="218"`) -> `Jpb` (L133404): `p.o.WA(Name,
    // resolved Value, this.CH)` runs for EVERY scope, then `this.CH==0 &&
    // p.o.save()`. `parse` maps `Scope`: "Global"->CH1, "Local"->CH2,
    // "Users"/absent->CH0. `WA` (L133478) writes: CH0 -> the save's
    // `<Quests><Variables>` (`rv`) and PERSISTS; CH1 -> the session map
    // `p.o.AG` (`new Map` ctor L124266, never saved); CH2 -> `ha.F().aH`
    // (`Cja`/`q0` L517259). The key is the PUBLIC `Name` (`WA` does
    // `c.set("Name", a)`), no `_`; `wkb` (L132880) adds the `_` for `rv`.
    std::map<std::string, std::string> set_vars;     // Users (CH0) -> save
    std::map<std::string, std::string> global_vars;  // Global (CH1) -> `AG`
    std::vector<QuestBattleWrite> battle_writes;  // Show/Hide/SetVisibility/Toggle
    std::vector<std::string> scene_requests;      // Gn (record only)
    std::vector<std::string> fight_requests;      // Sn (record only)
    std::vector<std::string> dialogs;             // He summaries (record only)
    std::vector<std::string> clicks;              // Nn targets (record only)
    // `Nn` (`ClickButton`) with `UseFlashing="1"`: flash the target plate
    // WITHOUT firing its callback (`IgnoreCallback="1"`). The tutorial asks
    // for `InfoBattle.FightButton` (tutorial_quests.xml L156) — the map
    // FIGHT button is highlighted, never auto-pressed.
    std::vector<std::string> flash_targets;
    // `MenuBtnFlashing BtnName` (L361): highlight a `za` nav button by scene.
    // The desktop guidance for `_NextScene` (FLOW_STATIC L140-142: the web/
    // else branch shows the notification + the flash and does NOT navigate).
    std::vector<std::string> menu_flashes;
    // `Io` L1107 (`EToggleItems`): `p.iMa(ba.Pc(a,Label), ba.Pc(a,Toggle)=="on")`
    // — `p.iMa` L112419 writes the shop lock (`p.o.vq`/`tnb` L267:
    // `<Shop><Lock Name>` + `R$`) then equips/unequips the matching pack
    // (`p.items.Jrb`/`hnb` L167). Record the resolved `label=toggle`; the
    // write itself is applied by `apply_toggle_items`.
    std::vector<std::string> toggle_items;
    // `Pn` L1064 (`EDiscount`): the price override `getParameters` builds and
    // applies to the shop offer (`yf` + `p.o.xa.vu`). No offer model -> record.
    std::vector<std::string> discounts;
    // `wo`/`bo` L1097/L1086 (`EShowMapButton`/`EHideMapButton`): `Vb.F()`
    // map-button manager add (`hg`) / remove (`oKa`). No manager -> record.
    std::vector<std::string> map_button_shows;
    std::vector<std::string> map_button_hides;
    // `ho` L1090 (`EResetDuelTimer`): `Gb.reset(!1)` on the fight controller.
    std::vector<std::string> duel_timer_resets;
    // `yj` L1024 (`ETimer`/`EActivateTimer`): `p.o.yl.Uaa(name, value)`.
    std::vector<std::string> timer_sets;
    // `Rn` L1069 (`EEndTimer`): `p.o.yl.H4(name)`.
    std::vector<std::string> timer_ends;
    // `zj` L1072 (`EForeach`): one `Type/Name:iterator` per executed item.
    std::vector<std::string> foreach_runs;
    std::vector<std::string> clears;              // Mn queue names
    std::vector<std::string> minigames;           // Do/Eo/Ao/Bo/Co/Fo (record)
    // `Bn.S` (L1025): `AttachQuestFile File` loads a quest file at this
    // point in the run (deferred to the end of the fire pass — the loaded
    // quests register for FUTURE events, exactly like the JS `RA` loop
    // which captures `a.length` before iterating).
    std::vector<std::string> attach_files;
    // `Ge.S` (L1023-1024): `Activate ActionID` (re-fires the Activate
    // event with `_$ActionID` bound; the JS `Ge.MZ` handshake).
    std::vector<std::string> activate_requests;
    // --- live action execution (interactive; headless keeps records) -------
    // The JS action classes that ACT (not merely record), collected during the
    // run and performed by `tick` after the firing pass (so a `mp` push never
    // re-enters the ScreenManager's update iteration). See each cite below.
    //
    // `Gn` L1032: `wa.F().mp(xn.jOa(dest), …)` — push the scene.
    std::vector<QuestSceneRequest> navigate;
    // `go` L1092: `wa.F().mp(4, new Gj(tab,item))` + `Oa.uLa(tab,item)`.
    std::vector<QuestShopOpen> shop_opens;
    // `Hn` L1032-1034: `ChangeTab` tab selection requests (applied in `tick`).
    std::vector<QuestTabSelect> tab_selects;
    // `Nn` L1114 WITHOUT `IgnoreCallback`: the target's own click listeners
    // stay live (`xk.pa` is NOT cleared), so the player's press dispatches the
    // target's callback. The engine only ARMS + logs it (the JS never
    // auto-presses: `Nn.S` hooks `xk.pa.addListener(Qg)` and waits).
    std::vector<std::string> click_arm;
    // `eo` L1117 (`Nn`… `sxa()`): `MenuBtnFlashing` collapses the `za` scroll
    // (`za.instance.sxa()` -> `scroll.collapse(0)`, L2001) before it flashes.
    bool collapse_nav = false;
    // `GiveItem` grants (`Pa.W$a` L631756): applied to the save inventory in
    // `apply_effects` (the JS acts immediately; the port batches save writes).
    std::vector<QuestGiveItem> give_items;
    // `sh` `BuyItem` (`EBuyItem` g="1D4" L526589) with `SB!=3` (Coins/Ruby):
    // `S` L526709 -> `v.fZ(item, SB)` (L620099 -> `VYa` -> `Pa.Wz` L1234) —
    // the synchronous purchase (currency deduct + item grant) is committed in
    // `apply_effects`, which then fires `purchase` (the SAME call the Shop
    // uses). One resolved item name per entry, in action order.
    std::vector<std::string> purchases;
    // `Xn`/`rg` (`EGiveCurrency` g="1F1" L554285 / `ETakeCurrency` g="20C"
    // L567355): one resolved currency write. `Xn.S`: `Type`=`Gold` ->
    // `Fr(Tb+Value)`, `Bonus` -> `vl(fd+Value,6)`, else `TH(Type,Value)`
    // (`p.o.Tb`=`money`, `p.o.fd`=`bonus`, `p.o.TH`=`currencies[Type]`).
    // `rg.S` first gates on `p.o.Xfa(Type,Name,Value)` (affordable) and only
    // then `J0a` deducts (same three shapes, negative); when the gate fails the
    // `<Error>` chain runs with NO write (`apply=false`).
    struct QuestCurrencyWrite {
        std::string type;    // resolved `Type` ("Gold"/"Bonus"/<currency name>)
        std::string name;    // resolved `Name` (`rg` only; the "Currency" key)
        int amount = 0;      // resolved `Value` (`Math.trunc`)
        bool take = false;   // `rg` (deduct) vs `Xn` (grant)
        bool apply = true;   // `rg` affordability failed -> false (Error branch)
    };
    std::vector<QuestCurrencyWrite> currency_writes;
    // `po` (`ESetDataVersion` g="1E0", L1039 `S`): the computed version string
    // (Full when it has exactly 3 dots, else Production.Major.Minor.DataVersion).
    // `apply_effects` performs `p.F().Oqb(b)` (L181) -> ROOT
    // `<Versions><DataVersion Value>` via `SaveSystem::set_data_version`.
    std::vector<std::string> data_version_writes;
    // `$n` (`EGivePerk`) `ApplyTo="Player"` (`jXa` -> `C1a` L555926): the
    // `<Perk Name Level UpgradeLevel>` rows granted via `p.o.co.K1a` (L154884
    // -> port `WarriorSave::learn_perk`). The JS `d8a(name)!=null` gate is the
    // catalog-membership check applied in `run_actions`.
    struct PerkGrant {
        std::string name;
        int level = 0;
        int upgrade = 0;
    };
    std::vector<PerkGrant> perk_grants;
    // `FightEnd` (JS `Tn.S`): `ca.Ka().kD(!1)` — end the live fight. The
    // engine records it; the fight scene consumes the request.
    std::vector<std::string> fight_end_requests;
    std::vector<std::string> unknown;             // unhandled tags
    // Shop-offer controller actions (`nt` g="5B", `p.Cw`): `CheckOffersStart`
    // (`Jn` L531140 -> `p.Cw.a_a()`), `ChangeOfferState` (`En` L528761 ->
    // `rc.state` write) and `CheckItemsFromPurchasedOffers` (`In` L530984 ->
    // `TZa()`). One entry per executed action (`tag:detail`).
    std::vector<std::string> offer_actions;
};

// One live map button (`hg`, JS L2176-2177): an entry of the `Vb` manager's
// `ny` list. `Lua` (L2167) appends it on `ShowMapButton` (deduped by `name`);
// `oKa` removes it on `HideMapButton`. `image` is a user-image key resolved by
// `sk.xmb` (L2165 `$w(E.get(338), a)`), `timer` is the `Sc` timer key.
struct EngineMapButton {
    std::string name;
    std::string image;
    std::string timer;      // `Sc`
    std::string show_type;  // `Kr` (L2177 default "Both")
};

// `yf` (L1246): the offer object. `Pn.S` (L1064, `EDiscount`) builds one per
// discounted item and stores it at `p.o.xa.<item>.Gp`; `p.o.xa.vu()` (L301 ->
// `item.uu(p.o.bb())`) then re-derives the item's displayed/charged price.
struct EngineItemOffer {
    std::string item;          // `yf.og` (the list.xml Name)
    int percent = 0;           // `yf.TP` (`K.T(e)`; the `<Offer Percent>`)
    int price = 0;             // `yf.KA` = base * ((100 - percent) / 100)
    bool sale = false;         // `yf.V4` (`Pn.S` L1065 `f.V4 = d.G`)
    // `yf.yn`: the `Pn.S` L1065 end time `a = h.G>0 ? p.Dc + h.G + tz : 0`
    // (0 = no expiry). `tz = trunc(ed.getTimezoneOffset())` — a hardcoded
    // `return 0` (L2204), so the port adds no timezone term.
    long long end_time = 0;
    // `yf.fE`: `Pn.S` writes `h.G>0`, but the shipped bundle never READS it
    // (only the ctor default `!0`, L28C, and the `from` copy) — the port keeps
    // the default.
    bool active = true;
    // `yf.Aw`: `Pn.S` L1065/1066 builds `new yf(og, Q2a, yn, g.G, k.G)` where
    // `g.G` is the resolved `NewAmount` (`Math.trunc(l.Ie)`, getParameters
    // `this.jsa`). Carried verbatim (0 when the attr is absent).
    int new_amount = 0;
    // `yf.KA` BEFORE the `e.G>0` overwrite: the `yf` ctor (L1246) sets
    // `vja = KA = f+e = "" + NewPrice`, so a `Percent="0"` offer keeps the
    // `NewPrice` string. `Pn.S` resolves it from `this.O9` (`NewPrice`), which
    // is `""` when the attr is absent.
    std::string new_price;
};

// `jl` (L180945): the persisted per-offer state object (`p.o.P7a(name)`,
// L130088). The shop-offer controller (`nt` g="5B") wraps every list.xml
// `SubType="Offer"/"DailyOffer"` item in an `hh`/`pl` and reads its `rc`
// (this struct). Loaded from the save's `<Offers><Offer ..>` rows (`Ldb`
// L130280) and written back (`Wyb` L130310).
struct EngineOfferState {
    int ox = 0;                     // `ox` (StartCount; Ldb bumps 0 -> 1)
    bool UH = false;                // `UH` (AllItemsRecieved)
    long long n4 = 0;               // `n4` (PurchaseTime, `p.Dc` seconds)
    std::string state = "NotStarted"; // `state` (NotStarted/Active/JustStarted/
                                      // LastChance/End/Purchased/Unknown)
    std::string name;               // `name`
};

class QuestEngine {
public:
    QuestEngine() = default;

    // Fires an event (ChangeTab/SceneLoaded/Activate/FightEnd/SessionStart):
    // loads the chain on first use, evaluates quests in file order, runs
    // matches, applies save writes dirty-checked, recurses Activate (cap).
    // Returns fired quest names. Never throws, never blocks, never
    // navigates. `journal` carries the event context.
    std::vector<std::string> fire(App& app, const std::string& event,
                                  const QuestJournal& journal);

    // JS `Pa.Wz` (L1234): the successful-buy dispatch. Sets `ta.item` and
    // fires `QUEST_EVENT_PURCHASE`; returns the fired quest names.
    std::vector<std::string> purchase(App& app, const std::string& item);
    // JS `Pa.Bv` (L1211): the failed-buy dispatch. Sets `ta.item` + `ta.I_`
    // (code 2 -> "Coins", 3 -> "Ruby", 4 -> "Connection", 6 -> "RaidCurr",
    // else null/""; `p.XPa`/`$Pa`/`WPa`/`ZPa` L2472), then fires
    // `QUEST_EVENT_PURCHASE_UNSUCCESSFUL`.
    std::vector<std::string> purchase_unsuccessful(App& app, const std::string& item,
                                                   int code);

    // Records the last fight triple (Bj Nb/Qv analog; set on FightEnd).
    // ChangeTab/SceneLoaded journals leave fight empty and inherit this.
    void note_fight(const std::string& name, const std::string& result);

    // Live `Vb.F().ny` (JS L2167): the map buttons currently shown. `Lua`
    // appends on `ShowMapButton`; `oKa` removes on `HideMapButton`.
    const std::vector<EngineMapButton>& map_buttons() const { return map_buttons_; }
    // JS `Vb.Qg` (L2173): `ha.F().ta.Av=name; ha.F().Sf(
    // "QUEST_EVENT_MAP_BUTTON_PRESS")`. Fires the `MapButtonPress` quest event
    // with `button_name` in the journal and returns the fired quest names.
    std::vector<std::string> press_map_button(App& app, const std::string& name);

    // Sensei-modal queue (He records): display + advance live in screens.
    bool has_dialog() const { return !dialogs_.empty(); }
    const EngineDialog& dialog() const { return dialogs_.front(); }
    void pop_dialog() {
        // `Wb`'s top is the first NON-Notification (`modal_index`) — the same
        // entry `press_dialog` erases. Erasing `begin()` dropped a leading bar
        // Notification instead: the `tutorial_shop` bar entry queued by
        // `StoryTutorialOpenScene` (tutorial_quests.xml L350) was still in the
        // queue when the follow-up `Regular` (L327/L110) arrived, so dismissing
        // the `Regular` left IT queued and the modal re-blocked the Shop forever.
        const std::size_t i = modal_index();
        if (i < dialogs_.size()) {
            dialogs_.erase(dialogs_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    // Headless drain pops the entry it LOGGED (`dialog()` = the FRONT), not
    // `modal_index()`. `pop_dialog()` is modal-aware and is a NO-OP when the
    // queue holds only bar Notifications, so a Notification at the front made
    // the headless `while (has_dialog())` loop spin forever (the `Regular`
    // behind it never drained) — a soft lock. This erase always progresses.
    void pop_head_dialog() {
        if (!dialogs_.empty()) dialogs_.erase(dialogs_.begin());
    }

    // --- `Ib` bar vs `Wb` modal (JS `He.S` L1050 / `Wb.Xob` L927) ----------
    // JS `He.S` L1050 routes a `Notification` to `Ib.F().Qhb(r,z,c,g,k,SK,x,
    // $Ta)` — the BAR — and NOT to `Wb.openDialog`; `Wb.Xob` (L927) is the
    // only `this.II.push(this.If)` site and it is reached from `Xc.Xhb` (the
    // `Regular` path, L931). So a Notification never enters the `Wb` queue and
    // the `Regular` is `Wb`'s top dialog the moment `He.S` runs (its chained
    // condition `this.type=="Notification"&&x || … || (Ib.RP=!1, this.sa())`
    // advances the chain when `x=ba.Zv(a,XVa="0")` is false, L1050). The bar is
    // ONE instance (`Ib`), so the LAST posted Notification is what it shows
    // (`Ib.Qhb` L1907 overwrites the visible bar).
    static bool is_bar_notification(const EngineDialog& d) {
        return d.type == "Notification";
    }
    // `Wb`'s top = the first queued dialog that is NOT a bar Notification.
    std::size_t modal_index() const {
        for (std::size_t i = 0; i < dialogs_.size(); ++i) {
            if (!is_bar_notification(dialogs_[i])) return i;
        }
        return dialogs_.size();
    }
    const EngineDialog* modal_top() const {
        const std::size_t i = modal_index();
        return i < dialogs_.size() ? &dialogs_[i] : nullptr;
    }
    // The `Ib` bar's content = the LAST queued Notification (`Ib.Qhb` L1907
    // overwrites the single bar instance, so `_NotificationTextPunchBag` wins
    // over `_NotificationTextMove` in `StoryTutorialWelcome`).
    const EngineDialog* notification_top() const {
        for (std::size_t i = dialogs_.size(); i-- > 0;) {
            if (is_bar_notification(dialogs_[i])) return &dialogs_[i];
        }
        return nullptr;
    }
    bool has_modal() const { return modal_index() < dialogs_.size(); }
    bool has_notification() const { return notification_top() != nullptr; }
    std::size_t dialog_count() const { return dialogs_.size(); }
    // `Ib.close` (the ReadTime budget spent, `Ib.aa` L1905 -> `y4(!1)`)
    // drops the BAR entry only — the `Wb` queue is untouched.
    void pop_notifications() {
        std::size_t w = 0;
        for (std::size_t i = 0; i < dialogs_.size(); ++i) {
            if (!is_bar_notification(dialogs_[i])) {
                if (w != i) dialogs_[w] = std::move(dialogs_[i]);
                ++w;
            }
        }
        dialogs_.resize(w);
    }

    // JS `He.dhb(a)` L1061: pops the head dialog and runs the deferred nested
    // actions of the slot selected by `button_index` — 0=Left(`Ng`),
    // 1=Right(`rh`, the historical default), 2=Middle(`Nh`), 100=Close(`Hj`).
    // Returns the recorded fight-request names (`Sn`) for the caller to
    // launch (the engine never navigates). Save writes are applied.
    std::vector<std::string> press_dialog(App& app, int button_index = 1);

    // D1 `He.gf` L1062 / `He.dhb(0)` with no `Ng` slot (L1061): dismiss the
    // top modal and resume the chain parked at it (the `continuation` tail).
    // Used by the buttonless-Regular, Notification and no-renderer advance
    // paths — the JS always ends them in `(Ib.RP=!1, this.sa())`.
    void dismiss_dialog(App& app);

    // JS `hab()` L1060: any slot carries actions (`Ng`/`rh`/`Nh`/`Hj`). A
    // dialog with no such slot advances on tap instead of firing a plate.
    // Reads the `Wb` top (the first non-Notification), not a bar Notification.
    bool dialog_has_button() const {
        const EngineDialog* d = modal_top();
        return d != nullptr &&
               (d->has_right_button || !d->button_actions.empty() ||
                !d->left_.actions.empty() || !d->middle_.actions.empty() ||
                !d->close_.actions.empty());
    }

    // Drops every queued dialog (tutorial handoff / scene reset).
    void clear_dialogs() { dialogs_.clear(); }

    // --- `Do`/`Eo`: the StoryTutorial lesson GATE (JS L1242/L1243) ----------
    // `StoryTutorialWelcome` (reference/extracted/xml/res/quest_extensions/
    // tutorial_quests.xml L26-42) is ONE serialized action list:
    //   Dialog(Notification Move) -> <StoryTutorialMove/> ->
    //   Dialog(Notification PunchBag) -> <StoryTutorialPunchbag/> ->
    //   Dialog(Regular characterSensei = the В БОЙ training-fight modal).
    // The JS `Do` (move) / `Eo` (punchbag) action classes SUSPEND the chain
    // there: `Do.S` arms `this.Ni = new Re(w(this,this.Cm), v.su.a_)` — a
    // timer of `v.su.a_` = `TutorialStepTimeout` (`internal_settings.xml`
    // L115 `Value="15"`) — and `Cm` calls `this.sa()`, which resumes the tail
    // (`Yb` L954 serializes the list). So the JS only ever has ONE tutorial
    // beat queued at a time: the Move bar, then the PunchBag bar, then the
    // Regular modal. The oracle tutorial captures are exactly those beats
    // (`oracle_matrix/MANIFEST.md`: tut_fight_stance = the move lesson,
    // tut_fight_phase2 = the punchbag lesson, tut_block/dojo_sensei = the
    // modal).
    // The port has no dojo move / punchbag lesson hooks (the actions were
    // "record only"), so it queued the whole list at frame 0 and `Wb`'s top
    // became the Regular from f=0 — which hides the bar beats. This gate
    // restores the JS serialization using the JS timeout on the APP clock
    // (the top screen's fixed 60 Hz `time()`, like the `ReadTime` bar budget),
    // never the wall clock.
    static constexpr float kTutorialStepTimeoutSec = 15.0f;
    // Beat the suspended chain is parked at: 1 = `StoryTutorialMove`,
    // 2 = `StoryTutorialPunchbag`, 3 = `StoryTutorialDoubleSweep`,
    // 4 = `StoryTutorialShowBlock`, 0 = not gated. The fidelity driver waits on
    // it so each tutorial capture lands on the oracle's own beat.
    int tutorial_gate_beat() const { return tutorial_gate_.active ? tutorial_gate_.beat : 0; }
    // Advances the gate clock by `dt` (app-time seconds); when it elapses the
    // stashed tail runs (which may immediately hit the second lesson and
    // re-park). Returns true when the tail resumed this call.
    bool tutorial_gate_tick(App& app, float dt);
    // JS `Bo`/`Do`/`Eo` `Pf` (sf2.502f0946.js L1121/L1123/L1125 <- the model
    // `Pf` L386): the player fighter STARTED the animation `name` (its JS
    // `zY` type `type`, "EAnimationMove"/"EAnimationAttack"). Runs the JS
    // arm/count and resumes the parked lesson on the REAL condition; the 15 s
    // `TutorialStepTimeout` stays the fallback. `end=true` is the `kg` event
    // (`Fo` L1126/L387, the animation END): it resumes the block lesson.
    void on_lesson_anim(App& app, const std::string& name, const std::string& type,
                        bool end = false);
    // JS `zt.VQ()` (`zi`, bundle idx 156971): `return this.HH != "END"` — the
    // tutorial chain is live while the normalized step is not the terminal
    // `END`. Keyed on the SAVE, not a harness flag.
    bool tutorial_live(App& app) const;

    // Test hook (the `--dialog-verify` harness): queue a dialog record built
    // in-process, so the display/dispatch contracts (Left-vs-Right plate,
    // colour→frame, page caption precedence) can be asserted without firing a
    // quest. Goes through the same queue + cap the parse path uses.
    void push_dialog_for_test(EngineDialog d) {
        if (dialogs_.size() >= 8) dialogs_.erase(dialogs_.begin());
        dialogs_.push_back(std::move(d));
    }

    // Test hook (`--changetab-probe`): parse+run ONE in-process action list so
    // a synthetic `Hn` ChangeTab can be asserted without a shipped quest.
    void run_action_probe(App& app, const std::vector<QuestAction>& acts,
                          const QuestJournal& journal);

    // Test hook (`--quest-query-probe`): resolve ONE expression through the
    // engine's own path (`resolve_token`) against the live save/journal.
    // Returns "" when the expression is UNKNOWN (unanswerable).
    std::string resolve_for_test(App& app, const std::string& expr,
                                 const QuestJournal& journal);

    // One fixed step (called by App::update_fixed AFTER the screen update):
    // resumes deferred `Wait` runs (`Ro` L1119) and performs the queued
    // scene/shop navigation (`Gn`/`go`). A no-op while headless (the driver
    // paths keep the record-only behaviour). Never throws.
    void tick(App& app);

    // `Nn` (L1114) non-ignored target currently armed for the player's press.
    // The screen's own hit-test consults this to log the dispatch (the JS
    // `Qg` listener completes the quest step on the click).
    bool click_armed(const std::string& target) const {
        for (const std::string& t : armed_clicks_) {
            if (t == target) return true;
        }
        return false;
    }
    // Consumes the armed target after the player's press dispatched it.
    void clear_click_armed() { armed_clicks_.clear(); }

    // Test hooks for the interactive verification: how many `ChangeScene` /
    // `OpenShop` actions actually EXECUTED (resolved + performed) rather than
    // being recorded. Monotonic; headless runs leave them at 0.
    std::size_t scene_actions() const { return scene_actions_; }
    std::size_t shop_actions() const { return shop_actions_; }
    // `Hn` (L1032-1034) ChangeTab actions actually EXECUTED (target live).
    std::size_t tab_actions() const { return tab_actions_; }
    // `zj.Qh` L1072 (`EForeach`): the number of `Sl.compare` matches — one per
    // executed sub-quest item (the port's `fx.foreach_runs` count). Monotonic;
    // the `--quest-query-probe` uses it to prove a sub-quest whose conditions
    // read the `?`-queries actually MATCHED.
    std::size_t foreach_matches() const { return foreach_matches_; }
    // `Tn` (`EFightEnd` L485079) `FightEnd` actions that produced a
    // fight-scene end request (`ca.Ka().kD(!1)`). Monotonic.
    std::size_t fight_end_actions() const { return fight_end_actions_; }
    // `sh` `BuyItem` `SB!=3`: purchased items whose `QUEST_EVENT_PURCHASE` was
    // fired (the `v.fZ` commit path). Monotonic; used by `--quest-action-probe`.
    std::size_t purchase_actions() const { return purchase_actions_; }
    // The list.xml `BonusPrice` (`catalog_bonus_price`) — the Ruby price
    // `BuyItem Currency="Ruby"` charges. Public for the probe.
    int bonus_price(App& app, const std::string& name) const {
        return catalog_bonus_price(app, name);
    }

    // --- `Ct` timer registry (JS `p.o.yl`, L291-292) ----------------------
    // The shipped `<ActivateTimer Name=... Value=.../>` (quests.xml L2154) sets
    // a named deadline (`bh.Nv`); `?Timer[Name].Value` (L988) reads the
    // REMAINING seconds (`v.ZI` L1218); expiry fires `QUEST_EVENT_TIMER_END`
    // (`Ct.swa` L292). Test hooks for the `--quest-query-probe` timer proof.
    std::size_t timer_count() const { return timers_.size(); }
    bool timer_present(const std::string& name) const {
        return timers_.find(name) != timers_.end();
    }
    // `v.ZI(bh.Nv)` (L1218): remaining seconds (0 when past); -1 when absent.
    double timer_remaining(const std::string& name) const;
    // `Ct.swa` (L292) `Sf("QUEST_EVENT_TIMER_END")` dispatches so far.
    std::size_t timer_end_fires() const { return timer_end_fires_; }
    // `ha.F().ta.dza` (L292): the timer whose `TimerEnd` last fired.
    const std::string& timer_end_name() const { return timer_end_name_; }
    // `Ct.t_a(a)` (L292) against an explicit clock (the JS tick is
    // parameterized by `p.Dc`): expire every `Nv <= now`, fire `TimerEnd`
    // per timer, then remove them. Returns the expired count.
    std::size_t run_timer_tick_for_test(App& app, double now);

    // --- tab tables (JS `vj` L1168-1169 / `uh` L1169) ---------------------
    // `vj.E0`: tab NAME -> index (0 = "Default", the JS default).
    static int tab_index_for_name(const std::string& name);
    // `uh.getName`: index -> tab NAME ("Default" when unknown).
    static std::string tab_name_for_index(int index);
    // `vj.ifa`: tab index -> owning screen id (11 = the JS unknown default).
    static int tab_screen_for_index(int index);
    // `Bj.DI` (ctor L1005): the CURRENT screen's tracked tab NAME. `wa.mp`
    // normalizes it (`e=vj.E0(e.DI)`, L933) before `v.qwa` writes the
    // `_$TabFrom`/`_$TabTo` pair, so the journal carries a valid `vj.E0`
    // name, never a scene name.
    const std::string& tab_owner() const { return tab_owner_; }
    void set_tab_owner(std::string name) { tab_owner_ = std::move(name); }

    // --- live UI-guidance signals (draw-only; no navigation) --------------
    // `Nn` `ClickButton UseFlashing="1"` target — the shell pulses the named
    // plate (the map's `InfoBattle.FightButton`). Cleared on the next
    // SceneLoaded (it belongs to the screen it was requested on).
    const std::string& flash_target() const { return flash_target_; }
    // `MenuBtnFlashing BtnName` — the `za` nav button (by scene name) to
    // highlight until the player navigates there.
    const std::string& nav_flash() const { return nav_flash_; }
    // Last `SetMapFocus Battle=` value applied (`qo` L1086 = `p.o.m5(battle)`
    // + the `Ya` focus refresh). The Map re-targets `Rr` on change, so a
    // focus landing after the map's construction still takes effect.
    const std::string& last_map_focus() const { return last_map_focus_; }

    // --- `He` pager (L1042-1062) ------------------------------------------
    // A `Regular` dialog with several `<Line>` rows shows one row per page;
    // the page's `ButtonText` labels the advance plate and only the LAST
    // page's plate fires the nested actions (`hab()` L1060 / `dhb(1)` L1061).
    std::string dialog_button_text() const;
    bool dialog_has_next_page() const;
    void advance_dialog_page();

    // For logs/tests.
    std::size_t quest_count() const { return quests_.size(); }
    bool loaded() const { return loaded_; }
    // Shipped files the loader actually read (root + every resolvable
    // `<Include>` alternative + every executed `<AttachQuestFile>`).
    std::size_t file_count() const { return loaded_files_.size(); }
    const std::vector<std::string>& loaded_files() const { return loaded_files_; }
    // Queries the shell could not answer (logged; a condition using one is
    // UNKNOWN and never fires the quest).
    std::size_t unanswerable_count() const { return logged_queries_.size(); }
    const std::set<std::string>& unanswerable_queries() const { return logged_queries_; }

    // --- offer/price model (`yf` L1246 + `Pn.S` L1064, `EDiscount`) --------
    // The live `p.o.xa.<item>.Gp` offer, or null when the item carries none.
    // `offer_price` is the `item.uu(p.o.bb())` read (`p.o.xa.vu()` L301): the
    // `yf.KA` override while an active offer exists, else the list.xml base.
    const EngineItemOffer* offer_for(const std::string& item) const;
    int offer_price(const std::string& item, int base) const;
    std::size_t offer_count() const { return offers_.size(); }
    // JS `p.Dc` (`Math.round(Hb.instance.getTime())`, L178): the game clock in
    // SECONDS — the SAME `quest_now()` the offer deadlines use. Public so the
    // shop cell's `b.yn > p.Dc` sale sub-branch (`ns.j5` L2308-2309) reads the
    // clock `apply_discount` writes into `yf.yn`.
    static double now_seconds();

    // --- shop-offer controller (`nt` g="5B", `p.Cw`; `hh`/`pl` model) ------
    // The list.xml offer DEFINITIONS (JS `p.Cw.It`, built by `A1a` L180xxx
    // from `p.items.gHa` — every item whose SubType is Offer/DailyOffer). File
    // order. Distinct from the EDiscount `offers_` above.
    const std::vector<CatalogItem>& offer_defs(App& app);
    // The live `rc` state (`p.o.P7a(name)`, L130088); default NotStarted.
    const EngineOfferState& offer_state(const std::string& name) const;
    // `a_a()` (L180xxx): start eligible NotStarted offers (`Qba` -> `pwb`:
    // state="JustStarted", `ox`++, arm `OfferTimer_<name>`). Run by the
    // `CheckOffersStart` quest command (`Jn` L531140).
    void offer_check_start(App& app);
    // `QEa()` (L180xxx): the offer's `OfferTimer_<name>` deadline is in the
    // future (`p.o.yl.gJ(oJ()) != null && Nv - p.Dc > 0`).
    bool offer_timer_active(const std::string& name) const;
    // `isActive()` (L180xxx): QEa() || state in {JustStarted,Active} ||
    // (state=="LastChance").
    bool offer_is_active(const std::string& name) const;
    // `En` L528761 (`EChangeOfferState`): `<ChangeOfferState Name Value>` sets
    // `rc.state = Value` when the resolved Value != "Unknown" and differs.
    void offer_change_state(App& app, const std::string& name,
                            const std::string& state);
    // `tlb` L180xxx (the purchase-success path): state="Purchased", log `n4`,
    // then grant the not-yet-received offer items (`Wwa`).
    void offer_purchase(App& app, const std::string& name);
    // `tick_timers` hook: an expired `OfferTimer_<name>` -> `$Za` -> `C3a`:
    // state = `dU` (ShowLastChance) ? "LastChance" : "End" (never from
    // Purchased).
    void offer_timer_expired(App& app, const std::string& name);
    // `CheckItemsFromPurchasedOffers` (`In` L530984 -> `TZa` L180xxx).
    void offer_check_purchased(App& app);
    // `In`/`fz.Wn` (L180...): the state -> ordinal used by the `a_a` sort.
    static int offer_state_rank(const std::string& state);

    // `p.iMa` (L112419): the shop lock write (`p.o.vq`/`tnb` L267 —
    // `<Shop><Lock Name>` + `R$`) then equip (`on`) / unequip every owned item
    // whose catalog `lock` (PackLabel) equals `label` (`Jrb`/`hnb` L167,
    // `Ir` L322), persisting the save. `Jrb`/`hnb` run ONLY on a successful
    // lock toggle (JS-exact: `iMa` gates them on the `vq`/`tnb` result).
    void apply_toggle_items(App& app, const std::string& label, bool on);

    // `Pn.S` (L1064): build/replace (`on`) or clear the `yf` offer for `item`
    // with `KA = base * ((100 - percent) / 100)` (only when `percent > 0` —
    // `e.G>0 &&` in L1065); `Toggle="0"` -> `b.G.E4()`. `period` = `Csa`
    // (`h.G`, `Math.trunc`) -> `yf.yn` (`a = h.G>0 ? p.Dc + h.G + tz : 0`);
    // `sale` = `pta` (`d.G`, `l.Ie>0`) -> `yf.V4`. `count` = the `Item|count`
    // right-hand side (`getParameters` L1066 `r[1]`; `-1` when absent) — the
    // UPGRADE branch (`b.G.lB.set(f.G,g)` L1065). `new_amount`/`new_price` =
    // `NewAmount`/`NewPrice` (`yf.Aw`/`yf.KA` initial).
    void apply_discount(App& app, const std::string& item, int percent, bool on,
                        long long period = 0, bool sale = false, int count = -1,
                        int new_amount = 0, const std::string& new_price = {});
    // `b.G.lB.get(level)` (`I` L171xxx `Xv`): the live UPGRADE offer at an
    // `Item|level`, or null. `Pn.S` L1065 writes it (`lB.set`).
    const EngineItemOffer* upgrade_offer_for(const std::string& item,
                                             int level) const;
    std::size_t upgrade_offer_count() const;

private:
    // `p.o.xa.<item>.Gp` (the live per-item offer; `Pn.S` L1064 writes it,
    // `p.o.xa.vu()` L301 + the shop price render read it).
    std::map<std::string, EngineItemOffer> offers_;
    // `item.lB` (`I` ctor L162531 `this.lB = new Map`): the per-item
    // UPGRADE-level offers keyed by the `Item|count` level (`Pn.S` L1065
    // `b.G.lB.set(f.G,g)`; read back by `Xv` L171xxx `b.Gp = this.lB.get(b.Tg)`).
    std::map<std::string, std::map<int, EngineItemOffer>> upgrade_offers_;

    // The list.xml offer-definition cache (`p.Cw.It`; static for the process)
    // and the live per-offer `rc` states (`p.o.QN`, `P7a` L130088).
    mutable std::vector<CatalogItem> offer_defs_;
    mutable bool offer_defs_ready_ = false;
    std::map<std::string, EngineOfferState> offer_states_;

    // `Qba` L180xxx: `!Nga() && (BCa()<=0 || BCa()<p.Dc) && (N0()<=0 ||
    // N0()>p.Dc) && Ti(player)`; on success `pwb` (state=JustStarted, ox++,
    // arm `OfferTimer_<name>`). Returns whether the offer started.
    bool offer_try_start(App& app, const CatalogItem& ci);
    // `Nga()`: `p.o.xa.Jga(name)` (the inventory owns the offer) or, for a
    // `pl` (DailyOffer), `m.any(item.Ht, b => p.o.xa.Jga(b.name))`.
    bool offer_owned(App& app, const CatalogItem& ci) const;
    // `Ti(player)` L180xxx: every `OfferConditions` (`CE`) leaf holds.
    bool offer_conditions_hold(App& app, const CatalogItem& ci);
    // The offer def with that name, or null (`m.dn(p.Cw.It, e.ab()==name)`).
    const CatalogItem* offer_def(App& app, const std::string& name);

    // One condition-evaluation context (the JS `Bj` journal `ta` plus the
    // live save snapshot the `?`-queries read).
    struct EvalCtx {
        QuestJournal journal;
        std::string story_step;
        // `_$Iterator` (JS `Bj` field; `zj` L1072 writes
        // `parameters.iterator` before each `Sl.compare`/`Sl.lF`).
        std::string iterator;
        // The active run's Local/CH2 store (`ha.F().aH`), consulted by a
        // sub-evaluation (`resolve_token`/`quest_var`). Null at fire time —
        // no quest-local scope is pushed yet (JS `p.o.f5a` L133027).
        const std::map<std::string, std::string>* locals = nullptr;
        int level = 1;
        mutable bool save_loaded = false;
        mutable WarriorSave save;
        const WarriorSave& live(App& app) const;
    };

    bool ensure_loaded(App& app);
    // JS `ha.GEa` (L521470): true while a quest instance with this name is
    // still in the ACTIVE queue (`Dh`) — a queued dialog, a deferred `Wait`
    // run, or the parked StoryTutorial gate. `RP.a.RXa` (`AllowDoubles`)
    // bypasses it. This is the ONLY re-entry gate: `Unresumable` (`be.cyb`,
    // L518544) is read solely by the resume path (`p.o.lpb`/`ResumeQuests`
    // `REa()`), never by the fire gate — so a quest whose event+conditions
    // recur re-fires once its run completes. That is what re-opens the Lynx
    // `StoryTutorialBossFight` dialog on the Map after a LOSS
    // (`tutorial_quests.xml` L131-152: `step==MAP && SceneTo==Map`).
    bool quest_active(const std::string& name) const;
    // JS `L3(a,b)` (L184): read one file, walk the root's children —
    // `Quest` -> register, `Include` -> `Sjb`.
    void load_quest_file(App& app, const std::string& rel);
    // JS `Sjb(a,b)` (L184): evaluate the Include's `<Conditions>` against
    // the current journal; if they hold, load every `File` alternative.
    void load_include(App& app, const pugi::xml_node& include_node);
    void parse_quest_node(App& app, const pugi::xml_node& quest_node,
                          const std::string& file);
    // `only` (non-null) restricts the pass to the single named quest — the
    // Place-gate retry path re-runs exactly the parked quest.
    void fire_inner(App& app, const std::string& event, const QuestJournal& journal,
                    std::vector<std::string>& fired, int depth,
                    const std::string* only = nullptr);
    // Place-gated runs parked while their authored scene was not mounted
    // (JS `be.Gib` L1007 `k7`; the `RA` L1018 / `qT` L1019 pump): replay each
    // whose scene is now current, with the journal captured at the original
    // match (the JS `Dh` pump runs `this.ta` as of `RA`).
    void retry_place_pending(App& app, std::vector<std::string>& fired);
    bool place_pending_has(std::size_t quest_index) const;
    // JS `yb.compare` (L959): 3-valued so a condition using a query the
    // shell cannot answer is UNKNOWN (the quest does not fire) instead of
    // silently true/false.
    enum class Tri { False, True, Unknown };
    Tri eval_cond(App& app, const QuestCond& cond, const EvalCtx& ctx);
    bool conditions_hold(App& app, const QuestCond& cond, const EvalCtx& ctx);
    // `_Name` quest-variable ref (JS `p.o.f5a` L133027): Local (`ha.F().q0`)
    // -> Global (`AG`) -> Users (`rv`). Unknown -> the empty string.
    std::string quest_var(App& app, const std::map<std::string, std::string>& locals,
                          const std::string& token);
    // Resolves one Value1/Value2 expression. Returns false when the shell
    // cannot answer it (a `?`-query it does not model) — the caller then
    // treats the comparison as UNKNOWN.
    bool resolve_token(App& app, const std::string& token, const EvalCtx& ctx,
                       std::string& out);
    // `?Method[arg].Field` — the subset of the JS query engine the shipped
    // conditions read. Returns false (UNKNOWN, logged) for the rest.
    bool resolve_query(App& app, const std::string& token, const EvalCtx& ctx,
                       std::string& out);
    void note_unanswerable(const std::string& token);
    // `p.items.$b(name)` — the list.xml catalog (`Item`/`Purchase` queries).
    // Cached: the catalog is static for the process. Null when absent.
    const CatalogItem* catalog_find(App& app, const std::string& name) const;
    // `?Item[x].BonusPrice` = the list.xml `BonusPrice` attr (the JS `od`).
    // `CatalogItem` does not carry it, so it is read direct + cached.
    int catalog_bonus_price(App& app, const std::string& name) const;
    // Remainder of one action list when a `Wait` suspends it: `Yb` (L954)
    // serializes the list and `Ro` (L1119) completes N frames later, so the
    // actions AFTER the Wait run only once the delay elapses. `rest` is the
    // suspended tail (`inner remainder ++ outer remainder`).
    struct ActionRest {
        bool suspended = false;
        int frames = 0;
        std::vector<QuestAction> rest;
        // D1: the suspension is a WIDGET-building `Dialog` (`He.S` L1051), not
        // a `Wait`. `rest` is the parked outer chain and `dialog_index` is the
        // queued `dialogs_` entry that owns the resume (`He.gf` L1062). `tick`
        // must NOT resume it — the dismissal does.
        bool dialog_parked = false;
        std::size_t dialog_index = static_cast<std::size_t>(-1);
    };
    // One deferred action run (a suspended tail + its journal/locals).
    struct PendingRun {
        std::vector<QuestAction> actions;
        QuestJournal journal;
        std::map<std::string, std::string> locals;
        std::string quest;
        int frames = 0;
    };
    // The StoryTutorial lesson gate (see `tutorial_gate_beat`): the tail of the
    // suspended run plus its journal/locals and the remaining app-time.
    struct TutorialGate {
        bool active = false;
        int beat = 0;          // 1 = move, 2 = punchbag, 3 = double sweep, 4 = block
        float remaining = 0.0f;  // app-time seconds until `Cm` (`TutorialStepTimeout`)
        std::vector<QuestAction> rest;
        QuestJournal journal;
        std::map<std::string, std::string> locals;
        std::string quest;
        // Live step at park time: a change is the JS `p.o.zi.LE` event `Cm`
        // listens on (see `fire`), the real resume condition besides timeout.
        std::string step_at_park;
        // JS `Bo`/`Do`/`Eo` `Pf` arm/count (sf2.502f0946.js L1121/L1123/
        // L1125): `anim_armed` = `w9`/`y9`/`x9` (armed by the animation
        // start), `anim_count` = `dsa`/`Ara` (the resumed arm count).
        int anim_count = 0;
        bool anim_armed = false;
    };
    TutorialGate tutorial_gate_;
    // Runs the stashed tail (shared by the resume path); returns true if the
    // tail completed (false when it re-parked on the next lesson).
    bool resume_tutorial_gate(App& app);

    ActionRest run_actions(App& app, const std::vector<QuestAction>& acts,
                           const QuestJournal& journal, QuestSideEffects& fx,
                           std::map<std::string, std::string>& locals,
                           const std::string& quest, int depth,
                           const std::string& iterator = std::string());
    // D1: runs one action list + its side effects (save writes, live actions,
    // guidance signals, chained `Activate` re-fires). `outer` is the
    // continuation of the ENCLOSING chain: a park here stores `rest ++ outer`
    // as the new dialog's continuation; a `Wait` defers `rest ++ outer` to
    // `tick`. This is the `Yb` L954 composition, shared by `fire_inner`,
    // `press_dialog` and the dialog-resume paths.
    ActionRest run_chain_effects(App& app, const std::vector<QuestAction>& acts,
                                 const QuestJournal& journal,
                                 std::map<std::string, std::string>& locals,
                                 const std::string& quest,
                                 const std::vector<QuestAction>& outer,
                                 std::vector<std::string>* fights_out);
    // D1: stores `rest.rest` as the parked dialog's continuation (`He.gf`).
    void attach_dialog_park(const ActionRest& rest,
                            const std::map<std::string, std::string>& locals);
    // D1 `He.gf` L1062: resumes a dismissed dialog's parked tail.
    void resume_dialog_chain(App& app, EngineDialog& dlg,
                             std::vector<std::string>* fights_out);
    // Collects the executable side effects of one run (interactive only).
    void enqueue_effects(App& app, const QuestSideEffects& fx,
                         const QuestJournal& journal,
                         const std::map<std::string, std::string>& locals,
                         const std::string& quest);
    // Resumes a deferred run (its tail) and re-defers when another Wait hits.
    void resume_run(App& app, PendingRun& run);
    // `Gn.qIa` (L1032): navigate to a resolved scene name.
    void do_navigate(App& app, const QuestSceneRequest& req);
    // `go.Thb` (L1092): open/point the Shop at a tab + item.
    void do_open_shop(App& app, const QuestShopOpen& open);
    // `Hn.S` (L1032-1034): select the target screen's tab (case 4/5/7).
    void do_tab_select(App& app, const QuestTabSelect& sel);
    void apply_effects(App& app, const QuestSideEffects& fx);
    std::string battle_zone(const std::string& battle) const;
    bool loaded_ = false;
    std::vector<QuestDef> quests_;
    // Evaluation context of the load pass (the JS `ha.ta` journal an
    // `<Include>`'s conditions read). The root pass uses the boot journal.
    EvalCtx load_ctx_;
    // JS `p.o.AG` (ctor L124266 `this.AG=new Map`): the `Scope="Global"`
    // store. Session-scoped only — never parsed from or written to the save.
    std::map<std::string, std::string> global_vars_;
    std::vector<std::string> loaded_files_;  // shipped files the loader read
    std::vector<std::string> fired_;  // Unresumable session latch
    // Live map-button registry (JS `Vb.F().ny`, L2167).
    std::vector<EngineMapButton> map_buttons_;
    // Place-gated runs parked until their scene is entered (see
    // `retry_place_pending`). `quest_index` indexes `quests_` (stable: the
    // loader only appends).
    struct PlacePending {
        std::size_t quest_index = 0;
        std::string event;   // the event the set matched
        QuestJournal journal;  // match-time context (JS `ta`)
    };
    std::vector<PlacePending> place_pending_;
    std::map<std::string, std::string> battle_zone_;  // battle -> zone index
    // The stages `<Battle Type>` key per battle (`pkb` L719570 reads it;
    // `?Fight.Type`/`?Battle.Type` map it through `b0`/`rAa`).
    std::map<std::string, std::string> battle_type_;
    // `p.items` catalog cache + the list.xml `BonusPrice` map (see
    // `catalog_find`/`catalog_bonus_price`).
    mutable std::vector<CatalogItem> catalog_cache_;
    mutable bool catalog_ready_ = false;
    mutable std::map<std::string, int> bonus_price_cache_;
    mutable bool bonus_price_ready_ = false;
    std::string last_fight_;
    std::string last_result_;
    std::vector<EngineDialog> dialogs_;  // Sensei-modal queue (cap below)
    // Live UI guidance (see flash_target/nav_flash/last_map_focus above).
    std::string flash_target_;
    std::string nav_flash_;
    std::string last_map_focus_;
    // `?`-queries the shell could not answer (JS has a full query engine;
    // the port answers the subset it models). Logged once each; a condition
    // whose operand is unanswerable is UNKNOWN (never fires).
    std::set<std::string> logged_queries_;
    // Deferred `Wait` runs + the queued live actions (see `tick`).
    std::vector<PendingRun> pending_;
    std::vector<QuestSceneRequest> nav_queue_;
    std::vector<QuestShopOpen> shop_queue_;
    std::vector<QuestTabSelect> tab_queue_;
    bool collapse_nav_pending_ = false;
    std::vector<std::string> armed_clicks_;  // `Nn` non-ignored targets
    std::size_t scene_actions_ = 0;          // executed `ChangeScene` count
    std::size_t shop_actions_ = 0;           // executed `OpenShop` count
    std::size_t tab_actions_ = 0;            // executed `ChangeTab` count
    std::size_t foreach_matches_ = 0;        // `zj.Qh` sub-quest match count
    std::size_t fight_end_actions_ = 0;      // `Tn` (`EFightEnd`) action count
    std::size_t purchase_actions_ = 0;       // `sh` BuyItem purchase fires
    std::string tab_owner_;                  // `Bj.DI` (ctor L1005)
    // --- `Ct` (L291) timer registry (`p.o.yl`) ---------------------------
    // `Uaa`/`H4` (L291): name -> absolute deadline `bh.Nv` in `p.Dc` seconds
    // (`p.Dc=Math.round(Hb.instance.getTime())`, L178 — SECONDS, confirmed by
    // the shipped `Value="86400"` = 24 h). `t_a` (L292) expires them; `swa`
    // raises `TimerEnd`. `dza` (L292) is the firing timer's name.
    std::map<std::string, double> timers_;
    std::string timer_end_name_;       // `ha.F().ta.dza`
    std::size_t timer_end_fires_ = 0;  // `Sf("QUEST_EVENT_TIMER_END")` count
    void timer_activate(const std::string& name, double deadline);  // `Uaa`
    void timer_end(const std::string& name);                        // `H4`
    void tick_timers(App& app, double now);                         // `t_a`
};

// The `<Button Type>` slot census of the shipped quest tree (`quests.xml` plus
// every `quest_extensions/**` file — the tree `ensure_loaded` walks). Each
// `<Button>` is one slot: `He.Rib` L1057-1058. Used by the `--dialog-verify`
// parse assertion; expected 680 Right / 159 Left / 16 Middle / 1 Close.
struct QuestButtonCensus {
    std::size_t right = 0;
    std::size_t left = 0;
    std::size_t middle = 0;
    std::size_t close = 0;
    std::size_t dialogs = 0;  // `<Dialog>` elements seen
    std::size_t files = 0;    // quest XML files read
    std::size_t typed() const { return right + left + middle + close; }

    // Raw TEXT census (regex-equivalent scan of the file bytes, INCLUDING
    // commented-out XML). The parser excludes comments, so this is always >=
    // the element counts above. zone_4/zone_5/zone_6/zone_7 each ship three
    // commented-out quests, so the raw inventory is +12 dialogs / +12 Right /
    // +6 Left over the parsed slots.
    std::size_t text_right = 0;
    std::size_t text_left = 0;
    std::size_t text_middle = 0;
    std::size_t text_close = 0;
    std::size_t text_dialogs = 0;
    std::size_t text_typed() const {
        return text_right + text_left + text_middle + text_close;
    }

    // The `<Dialog Type=...>` distribution over the shipped tree — one entry
    // per `He.S` L1045-1051 routing case (every Type the parser meets, missing
    // `Type` counted as "Regular", the JS default at `He` L1043).
    std::map<std::string, std::size_t> types;
};

// Reads and counts the shipped quest tree. Never throws; a missing tree
// yields an all-zero census (the caller reports FAIL).
QuestButtonCensus census_quest_tree();

} // namespace sf2::app

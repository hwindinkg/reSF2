# SHOP_STATIC — items, prices, shop screen, buy flows, inventory, currencies, enchantments (web build)

Static-only spec from `reference/www/sf2.502f0946.js` (2533 lines, 1-based)
plus on-disk `reference/extracted/xml/res/list.xml` (parsed counts below),
`internal_settings.xml` (`<Tutorial>`), `users_default.xml`, and
`quest_extensions/tutorial_quests.xml` (all read-only). Anything not
resolvable without a runtime trace is marked `OPEN (needs runtime trace)`.
Companions: `reference/JS_FLOW.md` §3 (shop narrative), `reference/FLOW_STATIC.md`
(quest engine `ha`, save `xf`, rewards `v.kD`).

Minified names: `I` item model (L320-345), `it` catalog (L163-167),
`zf` inventory entry (L1253-1262), `$g` inventory (L298-301), `Ji` delivery
stub (L284), `Oa` Shop screen (L2285-2302), `ss` shop tabbar (L2284),
`Cj` tab maps (L2302-2303), `Gj` shop-open args (L1245: `T5` tab, `item`),
`Pa` purchase static (L1226-1234), `p` world (`YDa/gI/Ky/bo/fab/mKa/...`
L211-218), `fi/ms/ps/qs/gi` widgets (L2269-2280), `ph` buy dialog
(L1963-1966), `gg/Qv/Rv/Sv/Tv` currency models (L1182-1183/L1236-1238),
`wf` upgrade values (L354), `Lt` offer-enchant bundle (L349-350),
`Ao` tutorial-buy action (L1120-1121), `nw`/`v.su` tutorial config
(L1198-1199), `Hn` ChangeTab action (L1032-1033), `re` gate helpers (L2285).

---

## 1. Item model `I` (L320-345)

### 1.1 Type/subtype constants (L2473)

`I.Px="Skeleton"`, `I.vg="Weapon"`, `I.Ai="Armor"`, `I.Bi="Helm"`,
`I.Vh="Ranged"`, `I.Cf="Magic"`, `I.wk="RealMoneyItem"`, `I.KTa="Energy"`,
`I.e7="Decorate"`, `I.ITa="Cheat"`, `I.Vr="Seal"`, `I.Bu="Free"`,
`I.yoa="Profile"`, `I.Ox="Consumable"`, `I.Hm="RaidConsumable"`,
`I.sB="RaidItemPack"`, `I.JTa="NoMagic"`, `I.voa="Gold"`, `I.$F="Bonus"`,
`I.c7="UnlimitedEnergy"`, `I.ETa="TapJoy"`, `I.b7="PerkReset"`,
`I.a7="Currency"`, `I.woa="Offer"`, `I.uoa="DailyOffer"`, `I.DTa="RaidCurrency"`.

Numeric type ids — `I.N$a` (L331-332): Weapon 1, Armor 2, Helm 3, Ranged 4,
Magic 5, Consumable/RealMoneyItem 6, Free 7, RaidConsumable 8, else 0.
Catalog-category ids — `vj.E0` (L1168-1169): Weapon 1, Armor 2, Helm 3,
Ranged 4, Magic 5, Ruby 6, Free 7, RaidConsumable 8, Cheat 9, Perks 10,
Moves 11, Achievements 12, QuestItems 13, Count 14, BattlePass 15,
StoryMapStage 16, RaidMapStage 17, else 0; `vj.ifa` maps to tabs (L1169).

### 1.2 `I.pL` XML attrs (L322-329)

| Field | XML attr | Notes |
|---|---|---|
| `name/fileName/model` | `Name/Image/Model` | |
| `type/Yb/ku` | `Type/SubType/TacticSubtype` | `Geb` retypes (L334) |
| `lock` | `PackLabel` then `GroupID` | `HJ` lock gate (L256 FLOW_STATIC) |
| `og` | `AndroidID` | IAP sku (`mt.Mga` Offer/DailyOffer, L345) |
| `mi/od` | `Price/BonusPrice` | base coin / gem price (`xb` = -1-if-absent) |
| `gU/S5/M5` | `SilentRecieve/SingleTimeBuy/ShopButtonHide` | `Scb` single-buy-owned test (L332) |
| `xr/Hp/TK` | `RealPrice[/Const/Currency]` | IAP display (`Gyb`, L342-343) |
| `Zz/CJ` | `ConsumableProduct/isPaid` | |
| `Mn/Ip/aF/bF/cF` | `RecieveGold/RecieveBonus/ReceiveForgeMaterial[123]` | grant payload (`Pa.Kua`, L1231-1232; `Gyb` switch, L343) |
| `Kj/an` | `CurrencyName/CurrencyValue` | currency grant |
| `isActive/eW` | `!ShopHide` (unless `ce.Aab`) | `Xnb` re-activates (L332) |
| `hidden` | `Hidden` | `li()` |
| `xf/Tg` | `Level/UpgradeLevel` | req level / upgrade tier |
| `U5/Ec` | `SpendAfterUse/DeliveryTime` | `Owa`: `Ec>0 && I.W0()>0 → Ec=W0()` (L341); `W0()=ce.bc.Tnb` (L343; `Tnb=-1`, L2475) |
| `O2/Od` | `MoneyDeliveryPrice/BonusDeliveryPrice` | instant-delivery fees |
| `Og/Ms` | `Milestone/AddPercent` | |
| `D3` | `PaidItem` (`"None"` default) | free vs `Paid`/`SuperPaid` lines |
| `D6` | `<Upgrades Template>` | upgrade-table key (`S7a`, L165) |
| `Oa/GF/lQ/kz` | `<Perks>/<Enchantments>/<RecipeDelivery>` | `Ojb/akb/rjb/ljb` (L334-335); `kz` enchant defs via `xe.Qd` |
| `attributes` | any `v.eo` attribute present on the node | combat stats |
| `CE/Ht` | `<OfferConditions>/<OfferItems>` (`Lt` bundles) | `d()`/`Ht` parse (L329) |
| `Ev/dj` | `<CompositeImage>/<MapButtonImage>` | |

### 1.3 Price accessors (L332-334)

- `jp()` = coin price: `am()!=null ? trunc(mi*DCa()) : mi`; `nn()` = gem
  price: same over `od`. `am(a)` picks `Gp`/`Fv` discount by `PAa`/`N1`
  date window (L332/L344-345); `DCa()` = `KA/Ofa()` ratio.
- `Hfa()` = `od>0` (has gem price); `QCa()` = `mi>0`; `Ofa()` = Hfa?od:mi.
- `H$a()` = `mi*(v.yya|0)` (`SellItems` factor; `v.yya` L1155 FLOW_STATIC).
- `C0()` = gem sell-back (`Ip`/`Aw`), `aJ()` = coin sell-back (`Mn`/`Aw`)
  (L333-334); `p.kqb` sell flow (L216).
- `hz(a)` inflation (L335): `mi=Vk(mi,a)` + effect values; `g_` re-prices
  RealMoneyItem gold; `D2a/Wca` scale `Gp/Fv.KA`; `v.Vk(a,b)=ceil(a/10^(kq-b))`
  (L1221; `kq` = DenominationDigits).
- `Scb()` = single-time-owned; `LCa()` = visible+active+lock-ok (L332);
  `ICa()` = has real price (`Hp`).

---

## 2. Catalog `it` + `list.xml` counts on disk

`p.items=new it` (L2472); `it.parse()` (L165): `Ja.ki(818)`; `qkb(UpgradeList)`,
`Lia(Items)`, `BR.parse(ItemSets)`; purchase/offer listeners.

Partition — `Lia` (L166-167): `Xm.push` all, plus `Ai→Cva`, `Ox→hca`,
`Bu→S_` (Free tab), `Bi→sDa`, `wk→Dp` (+`gHa` offers, `QXa` skus),
`Vr→aqb`, `vg→Au`, `Cf→WFa`, `Vh→SJa`.

| Type (disk) | Direct `<Item>` | Notes |
|---|---|---|
| Weapon | 141 | +`Fists` inside; SubTypes: Knives 6, Sai 6, Staff 7, ... (full map §2.1) |
| Armor | 127 | incl. `Body`, `PunchingBag` |
| Helm | 134 | incl. `Head` |
| Ranged | 56 | incl. `NoRanged`; Chakram 17 / Shuriken 15 / Kunai 11 SubTypes |
| Magic | 35 | incl. `NoMagic`; ~30 singletons + Earthquake 3 |
| RealMoneyItem | 78 | `PaidItem`: Paid 149 / SuperPaid 49 (whole catalog) |
| Skeleton | 6 | incl. `SkeletonPunchingBag` |
| RaidConsumable | 3 | incl. `NoRaidCharge` |
| Seal | 7 | `I.Vr` |
| Consumable | 2 | `Unlimited_Energy`, `Perk_Reset` |
| Energy | 1 | `Energy_Refill` (`EnergyRefillItem`, internal_settings) |
| Lottery_Reroll | 1 | |
| Decks | 1 | `Deck` |
| (Type=None) | 5 | `WEAPON/ARMOR/HELM/RANGED/MAGIC_C3_z3` |
| **Direct total** | **597** | +52 nested `<Item>` inside `<OfferItems>` (offer bundles) |
| UpgradeList templates | 10 | `Armor/Weapon/Helm/Ranged/Magic × (free + Paid_)` |
| ItemSets | 0 children | `BR.parse` over empty (L165) |

Upgrade rows per template (disk): Armor 150 / Weapon 150 / Helm 150 /
Ranged 141 / Magic 141 (same for Paid_ twins). Row shape e.g.
`<Upgrade WeaponDamage="30" Level="3" Price="770" BonusPrice="24" UpgradeLevel="300" Milestone="1"/>`
(`Weapon_Bonus[0]`); Paid twin differs only in `Price` (867).
`D6` template key → `S7a(D6)` (L165); `zz()` merges `eB` + template rows
with `Tc>Tg` (L337); `JQ(Tc)` exact-tier (L338), `e7a(Tc)` next-tier (L338),
`h9a` level-gated (L339); `vu(level)` picks current/next upgrade
(`xIa.Gb(type)` level cap, L340); `Xv(wf)` clones item at tier (L340-341).

Helpers: `T5a(type)` badge count, `$b(name)` find, `KQ(og)` sku list (L164);
`qkb` upgrade-tag parse (L164-165); `tCa()` catalog stamp for `Oa.Lma`
(L2298); `y7a/l7a` offer timers (L167).

`OPEN (counts)`: JS_FLOW §3 claims Weapon 150 / Helm 141 / Armor 135 /
Ranged 66 / Magic 43 / RealMoneyItem 91 / Gold 33 / Bonus 18 / DailyOffer 30 —
disk says 141/134/127/56/35/78 (+52 nested; Gold 33 / DailyOffer 30 appear as
*SubTypes*, Bonus 7). Discrepancy unresolved: JS_FLOW may count SubType
splits, nested OfferItems, or the www bundle (`xml.9e0b4b10.dat`) rather than
extracted `list.xml`. Needs runtime `Xm.length` / bundle diff.

### 2.1 Verified rows

- `WEAPON_KNIVES` (disk): `Type=Weapon SubType=Knives WeaponDamage=5 Level=1
  Price=50 UpgradeLevel=100`, Image `Weapon1.img_weapon_knives`, Model
  `mdl_weapon_knives`; no BonusPrice/PaidItem/ShopHide ⇒ coin-only, free
  line, visible.
- `users_default.xml` (disk): `Money=0 Bonus=50`, equipped
  `Body/Head/Fists/NoRanged/NoMagic` — matches `vy` defaults
  (`"Body Head Fists NoRanged NoMagic NoRaidCharge"`, L2477) and `Ff.$xa`
  `UseDefaultItem` (L864-865).
- `internal_settings.xml` `<Tutorial>` (disk): `TutorialWeapon=WEAPON_KNIVES`,
  `TutorialBoss=ZONE_1|BOSS_LYNX|1`, `TutorialTournament=ZONE_1|Tournament|1`,
  `TutorialStepTimeout=15`, Steps
  `NotStarted/FIGHT/STEP_BUY_ITEM[/_FINISH]/MAP/LEARN_PERK/SHOW_DOUBLE_SWEEP/SHOW_BLOCK/END`;
  `v.su` parses it with `Pca` default `"WEAPON_KNIVES"` (L1198-1199).
- `tutorial_quests.xml` `StoryTutorialBuyItem`: `OpenShop
  Tab=?Purchase[WEAPON_KNIVES].Type Item=WEAPON_KNIVES` → `?Purchase[…]` and
  `v.su.Pca` both resolve to the verified row (cross-check GREEN).

---

## 3. Price gate `p.YDa` (L212-213, verbatim)

```
YDa(item, currencyType, count=1):
  switch(type): 1/8: f=Tb, g=jp()*count        // coins
                2/9: f=fd, g=nn()*count        // gems
                10/11/18: f=fd, g=Od           // delivery-price gems
                3/14: g=f=0                    // free
                16: h=fT.XZa()                 // barter stock
                19: f=fd, g=nn()*count
  c=f-g; d={type:0,value:c}
  if ((type!=3&&type!=14) || L.K.qEa):
    item.xf>level → {type:1,value:-1}          // level lock
    elif c<0 → {type:2 (coins) / 6 (20) / 3 (else), value:-1}  // poor
    elif g<0 → {type:0,value:-1}
    elif type==16 && !h → {type:5,value:-1}
  else → {type:4,value:-1}
Fba = YDa(...).value!=-1                       // affordable?
```

`p.gI(item,type,price)` (L213-214): 3→`CYa`; 1/8→`gab`+`Fr(price)` (+`Ec`
delivery `e`); 2/9→`Vfa(false)`+`vl(price,10)`; 11/18→`Vfa(true)`+`vl(,11)`;
14→true; 19→`Vfa`+`BYa`; then `fab`+`Qkb` (unless 18).
`gab`/`Vfa` (L221): paid-delta bookkeeping (`IMa(hC-)`, `Cib`).
`fab` (L214): `Ky` grant + `bo` equip (not for `wk/Ox`).

---

## 4. Shop screen `Oa` + tabs (L2284-2302)

- `dJ()=4` (L2286); `init` builds `bB` (lists), `E3/sFa` (`ps`+`qs`
  enchant panes), `bc` (item list), `MJ/op` panes, `Ne` listener (L2289-2290).
- `f5(tab)` (L2286-2288): 0→`xaa` Weapons, 1→`u7` Armor, 2→`U8` Helm,
  3→`s$` Ranged (+`shopRangedLocked`), 4→`G9` Magic (+`shopMagicLocked`),
  5→`gC` enchant (`type==I.wk` filtered), 7→`QV` offers.
- Tabbar `ss` (L2284): `Tw=[0,1,2,3,4]` +5 if `iap` +7 if `L.K.Yja`;
  badges `T5a(Cj.zxb(tab))`.
- `Cj.l6` shop-id→tab (1→0,2→1,3→2,4→3,5→4,6→5,7→7,8→6, else 8);
  `Cj.zxb` tab→`I.*` type (0→vg,1→Ai,2→Bi,3→Vh,4→Cf,5→wk,6→Hm,7→Bu)
  (L2302-2303).
- `Hn` ChangeTab action (L1032-1033): Shop→`Oa.ska(Cj.l6(Ay),Focus)`.
- `jAa()` (L2297): refill lists via `v.Uv(catalog,relevant)` filter
  (`isActive && !li() && !Tga(lock) && !Scb()`); `oab` hides empty tabs.
- `uLa(tab,item)/ska(tab,item)/qF(tab,item)/e1` open variants (L2296-2297);
  `Vpb` deep-link `Gj(T5,item)` (L2296); `Lma` catalog-stamp refresh +
  `save()` (L2298); `xA/Hhb` selection plumbing (L2296/2298).

---

## 5. Item widgets + buy dialog

- `fi.A$a()` (L2270): `xW>=0&&iO>=0 ? iO : bb()*B9+Fk` — level-scaled attr
  display (`B9/Fk` from `Ova` bar-scale tables, `tbb`, L2272-2273).
- `ms` attr list (`oca` per `v.eo` attribute, L2274-2275); `ps` item pane
  (`dfb/Ex`: trial `xc` + `hk(type)` on a clone, L2277); `qs` enchant pane
  (`vgb/tgb/lgb` events, L2280); `gi` reward-item card (L2277).
- `ph` dialog (L1963-1966): `Mb` 0=coins (`p.o.Vf`) / 1=gems (`Z.Ur`,
  L2478); modes `shopBuy/shopUpgrade/dlgOrder*` (+raid fast-buy);
  `dlgCurrencyQuestion{img}{price}` + `dlgTimeDelivery/Upgrade` countdown
  (`ti.eAa`).

---

## 6. Buy flows `Pa` + `v.VYa` (L1209-1234, L211-218)

`v.VYa(item, ownedEntry, currencyType, count)` (L1209-1210): `YDa` gate
(first failure → `SYa` for 16 else `Bv(type)`); owned-entry shortcuts
(`zOa` clear-equip, case 5/6 `bo` on/off, 7 `kqb` sell, 13 `B2a` grant,
18 enchant `wAa/Xz`); else `p.gI(...)`, then `rf()` re-fetch, `Wma`
enchant-merge, `Vxa` (delivery notify) / `Vmb`.

- `Pa.iwa` coins (L1228): `Tb>=jp → Fr(Tb-jp) + gI/y2a + save + Wz`;
  else `Bv(item,2)`. `Pa.EYa` gems (same over `fd/nn`, `vl(,10)`, `Bv(,3)`).
- `Pa.DYa/FYa` (L1228-1229): upgrade owned entry via `Qi` (`mi` coins /
  `od` gems; `Ec>0` → `z2a` delivery else `Cba` instant).
- `Pa.iZa/AYa/gwa/hwa` (L1229-1230): timed-item rebuy (`Bh/Dc/Nz/Od`).
- `Pa.zYa` (L1230-1231): `od`-currency grant items (`TH(Kj,an)` currencies,
  `co.Tja` perks, `XT` energy).
- `Pa.Kua` (L1231-1232): master grant: `Ky` + `aJ→Mua` gold + `C0→Cua` bonus
  + forge mats (`Dtb/Etb/Ftb`) + `TH(Kj,an)` + perk/energy switches.
- `Pa.bDa/W$` (L1232-1233): `e7a(level)` tier pick → `Kua` → `gla/Np`
  (`Upgrade` below cap, else `xf*100+30`) → `Osb` aspect fix → `uu` →
  `Ir` lock refresh; `c==0` equip-branch (`bo`/`mKa`).
- `Pa.Wz/Gbb` (L1234): `QUEST_EVENT_PURCHASE` / `OFFER_ITEM_RECIEVED`.
- `v.Bv(item,failType)` (L1211): maps 2→`XPa="Coins"`, 3→`$Pa="Ruby"`,
  4→`WPa="Connection"`, 6→`ZPa="RaidCurr"` (L2472) → `I_` +
  `QUEST_EVENT_PURCHASE_UNSUCCESSFUL`.
- `p.Ky` grant (L216-217): stack/level-scale (`uu/bb`), `Np(Tg)`,
  `zF/UT` delivery, `BO` auto-equip flag path; `p.mKa` take (L217-218);
  `p.bo` equip+`setItem` (L214); `p.zOa` unequip-others (L215);
  `p.vzb` fall back to Fists/Body/Head/NoRanged/NoMagic (L214-215);
  `p.Ovb` consumable-stack decrement (L215).

---

## 7. Inventory `$g` / `zf` / equip (L284/L298-301/L1253-1262)

- `$g`: `Oo` add (+`d_` timed list), `removeItem`, `te/Qj` find,
  `hJ(type,sub)` filter, `Jga/ucb` owned tests, `Qxb/$o` equip-swap via
  `Ca.hk` + `setItem` + `save` (L299-300), `Myb/vu` level rescale,
  `Bma/Oda` delivery maturation (`Bh<=Dc` → `QUEST_EVENT_DELIVERY`, L301).
- `zf`: `ab/Kd` name, `Jr(ib)` bind (+`Ce==-1→Np(Tg)`), `vL(Ru)` equipped,
  `pd/lk` count, `zF/UT` delivery, `Np/Ce` upgrade tier, `BF/gla`
  Item-vs-Upgrade (`Nz()`), `Q0()` count>0, `oqb/wT/init` XML bind (L1255-1256),
  `uu(Qi/OH)` upgrade resolution, `sAa/MAa` effective item, `VO/mY`
  enchant-merge persist, `Kia/VXa` enchant set/apply, `wAa` first Wh==0 perk.
- `Ji` (L284): save-backed `Level/Name` stub (`ZB/Ba/Ce`).
- Equip slots — `xc.hk(type,item)` (L809): Ai→hg, Bi→Lg, Px→Of, vg→Hd,
  Cf→Mg, Vh→ig; `El.hk` raid-charge extras (L821-822); `Fd` getters (L809).

---

## 8. Currencies (L273/L1182-1183/L1236-1238/L218/L2472)

`gg.Qd` (`Name/Icon/Group`; `LZ`: Forge 1 / Raid 2, L1236-1237);
`Qv/Nq` level-tables (`Rv{Kj, fp(Level…)}`, `wQ(level)`, L1182/L1238);
`Tv/Uv` resist defs (L1183/1237); `xf.Hua/TH` grant (L273),
`j1/kDa/Xfa/Xbb/Y5a` afford tests (Bonus/Gold/Currency, L273);
`Ewa/Fwa` gem/coin add (L218), `Iab` exp (L218); offer price display
`$ca/displayPrice` (L2298).

---

## 9. Enchantments

`I` carries `Oa` (perk defs) + `GF` (enchant defs, `I.JS/vkb` clone,
L343-344) + `kz` (`xe.Qd`, L335); `Lt.mjb/SAa` offer bundles (L349-350);
`zf.Kia` selects active set (Wh==0 first + `cba.sOa` budget, L1257-1258),
`VXa/anb` applies (L1262); `Pa.Osb` stamps `Aspect=ye.F().gea(level)`
(L1233; `ye.v7` AspectScale L914-915); `Pa.dDa` re-apply on level (L1233-1234);
`qs` pane + `gC` tab filter `type==I.wk` (L2280/L2287).

---

## 10. Tutorial-buy `Ao` (L1120-1121, verbatim)

```
S(): Pca=v.su.Pca; owned?xa.Qj(Pca)!=null && sa()
    Sb.F().kk(!0); Oa.ska(0, Pca-item)     // open shop, Weapons tab, focus item
    M8 modal: Vg(!0), pa.clear, pa+=Qg; tk=!0 (+debug Dj→vS)
Qg(): modal off; b=$b(Pca); b!=null && (Pa.iwa(b) && xa.$o(b,!0), content.Sr()); sa()
```

`Pca` defaults to `"WEAPON_KNIVES"` (L1199) and `TutorialWeapon` on disk
agrees (§2.1) → tutorial buys the verified 50-coin row via the normal
coin gate (`Pa.iwa`, L1228) then force-equips (`$o`, L299-300).

---

## OPEN (needs runtime trace)

1. Catalog-count gap vs JS_FLOW §3: RESOLVED → §11 (documented drift;
   bundle authoritative). Runtime `Xm.length` check still welcome.
2. `Pa.bDa` upgrade-price source (`e7a(Tg)` vs `Np` bookkeeping) against a
   live purchase; `gla("Upgrade")` + `xf*100+30` over-cap rule live check.
3. Delivery timers live: `Bh/Dc` countdown, `Oda/Bma` events, `y2a/z2a`
   paths, `W0/Tnb` override (currently -1 ⇒ `Ec` unchanged).
4. Offer/IAP runtime: `og` sku → `Pb/L.K.Wt displayPrice`, `Ht/Lt` bundle
   contents, `CE` conditions, `Gbb` offer events.
5. `re.*` gate dynamics (`XDa/xcb/Zcb/rga/sEa`, L2285) vs live inventory.
6. Enchant budget live: `cba.sOa` limits, `gea(level)` values, `qs` apply
  /remove (`c0a/R_a`) flows.
7. `QV` offers tab + `QJ/qJ/GU` badge plumbing; `IAP` tab 5 gating (`iap`).

---

## 10. `OLa` level curve (L253-254, verbatim — Stream 1 wiring)

```
Oz(){if(this.Ca!=null){let a=this.Ca.level-1;if(a<this.$B.length)return this.$B[a]}return 2147483647}
OLa(a){if(this.Ca==null)return!1;let b=!1;var c=this.Oz();a=this.rs=a;if(c<=a){b=!0;let d=this.Ca.level;
for(;c<=a;)a-=c,++d,this.Ca.level=d,c=this.Oz(),a>v.Bha&&(a=v.Bha),d>this.$B.length&&(d=this.$B.length,a=this.Oz(),b=!1);
this.rs=a;this.Ca.level=d;this.nF("Level",this.Ca.level)}
this.Ca.level==this.$B.length&&this.Ca.level>1&&(this.rs=a=this.$B[this.Ca.level-2]);
b&&(this.xa.vu(),this.xa.Ryb(this.bb()),c=Oa.get(),c!=null&&c.Lma(),p.F().Yyb(this.bb()),this.oS.Z());
this.nF("Experience",this.rs);this.Zha.Z();return b}
Jab(a){return this.OLa(this.rs+a)}
```

- `$B` built by `Bjb` (L274): `Exp` per `v.FR.thresholds` in order;
  `Oz()` = cost to leave current level (`$B[level-1]`, else 2147483647);
  `$0()` = last threshold's `Level` = max level (L1202);
  `Bha` default `3E7` (L2480) = XML `<MaximumExperience Value="30000000"/>`.
- Table (`character_progress.xml` `<Thresholds>`, = www-bundle copy):
  `v.FR.parse` (L1184: `Ba(Level,Exp)`); `td.Vib` asset 1315 (L1160-1161).

```
L:Exp  1:150 2:190 3:350 4:355 5:355 6:790 7:800 8:1300 9:1450 10:1500
11:1700 12:2400 13:1850 14:2450 15:2950 16:3000 17:3000 18:4000 19:3000 20:4000
21:4650 22:5000 23:5000 24:6800 25:3600 26:5600 27:6800 28:6900 29:7000 30:8500
31:4400 32:6700 33:8400 34:8400 35:9000 36:10000 37:10000 38:11500 39:16900 40:16900
41:16900 42:16900 43:16900 44:150000 45:95000 46:155000 47:90000 48:95000 49:227000
50:173000 51:280000 52:300000
```

52 rows, max level 52, lifetime sum 1,813,340. Level-up side effects:
`xa.vu/Ryb` (unlock point + shop-list refresh eligibility, L303),
`Oa.Lma` shop refresh, `p.Yyb`, `oS` event.

## 11. Catalog-count gap vs JS_FLOW §3 — RESOLVED (documented drift)

| Source | Weapon | Helm | Armor | Ranged | Magic | RealMoney | Gold* | Bonus* | DailyOffer* |
|---|---|---|---|---|---|---|---|---|---|
| Extracted `list.xml` (597 direct) | 141 | 134 | 127 | 56 | 35 | 78 | 33 | 7 | 30 |
| www bundle `res/list.xml` (731 tags) | 148 | 139 | 133 | 64 | 41 | 91 | 33 | 18 | 30 |
| JS_FLOW §3 | 150 | 141 | 135 | 66 | 43 | 91 | 33 | 18 | 30 |

*SubTypes. Bundle == JS_FLOW exactly for RealMoney/SubTypes; equipment is a
clean **+2 per category** in JS_FLOW vs the current bundle. Ruled out:
ShopHide splits, no-Price rows, nested OfferItems (all Type=None), `SubType`
grep artifacts (naive-substring recount = exact counts), name-prefix fallback.
Verdict: **version drift** — JS_FLOW §3 was counted from a www build whose
`list.xml` carried exactly 2 more rows per equipment category; the extracted
copy is additionally stale (597 vs 731 tags, upgrade templates 150/141 vs
214/200/201). **Bundle is authoritative for the port**; JS_FLOW §3 item
counts superseded (its flow content unaffected). OPEN-item 1 closed.

## 12. Upgrade-pricing verification (hand-computed vs UpgradeLists)

Rule (`zz`, L337): candidates = inline `eB` + template (`D6`) rows with
`Tc>Tg`, sorted; `Pa.DYa/FYa` charge `Qi.mi/od` (L1228-1229).

1. `WEAPON_KNIVES` (Tg=100, `D6=Weapon_Bonus`): first row Tc>100 → Tc=300
   → **770 coins / 24 gems** (`Weapon_Bonus[0]`).
2. `Body` (Armor, no `Upgrades` child, no `UpgradeLevel`): no `D6`, empty
   `eB` → `zz` empty → **not upgradeable** (also ShopHide/Hidden).
3. `MAGIC_AE21_SPIRIT_PILLAR` (Tg=600, `D6=Paid_Magic_Bonus`): first row
   Tc>600 → Tc=620 → **200 coins / 3 gems**   (`Paid_Magic_Bonus[1]`;
   row 0 Tc=600 excluded by strict `>`). Enchanted
   (`PERK_ITEM_SPECIAL_BLOODRAGE_MAGIC`), Paid/ShopHide/CLANS.

---

## RESOLVED (round 5 research sweep)

- **`re.*` gates (L2285)**: `XDa` (owned && `Ec>0`), `xcb` (`Bh>Dc`
  timed), `Zcb` (`RB && !zN` upgrade available), `rga` (pair gates),
  `Rcb` (pass-through), `sEa` (owned / count>0). All pure predicates.
- **QV tab refs**: `case 7` (L2288) fed by `jAa` from `S_` Free items
  (L2297); `oab` hides it when empty (L2298). Live offer contents stay
  OPEN-KEPT.
- **Delivery formulas**: `Ec/Bh/zF` fields + `Owa/W0` (L326/L341/L343),
  `y2a/z2a/Cba` instant-vs-timed (L1227-1228), `Bma/Oda` maturation +
  `QUEST_EVENT_DELIVERY` (L301), `Vxa` notify (L1219). Live countdown
  stays OPEN-KEPT.
- **Offer/IAP refs**: `og` sku + `Hp/TK` display (L324/L342-343),
  `Ht/Lt` bundles (L329/L349-350), `Pa.Gbb` event (L1234), `Pb/L.K.Wt`
  price lookup (L2298). Live purchase stays OPEN-KEPT.
- **Enchant budget (static part)**: resolved in PERKS_STATIC §4
  (`sOa`/2-row table/`Kia`); live `c0a/R_a` flows stay OPEN there.

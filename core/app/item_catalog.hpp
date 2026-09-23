#pragma once

// Item catalog — the res/list.xml <Items> (JS `p.items`, class `it` g="5A").
//
// JS study (sf2.502f0946.js):
//   - `it.parse` (L86475) reads every <Item> into `Xm` and buckets them by
//     `type` (I enum L1271963: `I.vg="Weapon"`, `I.Ai="Armor"`,
//     `I.Bi="Helm"`, `I.Vh="Ranged"`, `I.Cf="Magic"`):
//       Weapon -> `Au`, Armor -> `Cva`, Helm -> `sDa`, Ranged -> `WFa`,
//       Magic -> `SJa` (the shop-tab lists).
//   - Each Item carries the list.xml attributes: Name, Type, SubType,
//     Price (gold), BonusPrice (premium), Model, Image, Level, WeaponDamage,
//     BodyDefense/HeadDefense, UnarmedDamage, MagicDamage, ShopHide/Hidden,
//     PaidItem.
//   - The shop screen (Oa g="468") shows the priced items; the purchase
//     flow is `Pa.iwa` (L629626) — money check `p.o.Tb >= a.jp()`, deduct
//     `p.o.Fr(b)`, add `Pa.gI` (L628934) -> `p.o.xa.Oo` -> save.
//
// The native port keeps the same fields the shell needs: the shop item
// list (Weapon/Armor/Helm with a gold Price, not ShopHide/Hidden/Paid) and
// the Model names for the fighter rebuild. The ItemCatalog is pure data +
// parse — no platform code (portable C++17).

#include <map>
#include <string>
#include <vector>

namespace sf2::app {

// One list.xml `<Perks>`/`<Enchantments><Perk Name>` binding (JS `xe`
// be-entry, L1257): the perk name + its `<Set>` overrides. `enchant`
// marks `<Enchantments>` rows (budget path `sOa`, PERKS §4).
struct ItemPerkRef {
    std::string name;
    std::map<std::string, double> set_num;
    std::map<std::string, std::string> set_str;
    bool enchant = false;
};

// One `<OfferConditions>` leaf (JS `yb` node kept in the offer's `CE`,
// item ctor L166270 `d(a.A("OfferConditions"),this.CE)`). `Ti(a)`
// (L180xxx) requires EVERY leaf to hold before the offer may start.
struct OfferCondition {
    std::string kind;    // Equal|Greater|GreaterEqual|Less|LessEqual|Contains|Starts|Ends
    std::string value1;  // Value1
    std::string value2;  // Value2
    bool invert = false; // Not="1"
};

// One list.xml <Item> (JS `p.items.Xm` element).
struct CatalogItem {
    std::string name;       // Name ("WEAPON_KNIVES")
    std::string type;       // Type ("Weapon"/"Armor"/"Helm"/"Ranged"/"Magic")
    std::string subtype;    // SubType ("Knives", "" for armor/helm)
    // PackLabel — the JS item `lock` (`pL` L322: `this.lock = PackLabel ??
    // GroupID`). Groups a shop pack / equipment set; the `ToggleItems` action
    // (`Io` L1107 -> `p.items.Jrb`/`hnb` L167) equips/unequips every item whose
    // `lock` matches the resolved Label.
    std::string pack_label;
    std::string model;      // Model ("mdl_weapon_knives", "" when none)
    std::string image;      // Image (the shop card art ref)
    int price = 0;          // Price (gold; the JS `jp()` uses `mi` when no
                            // price attr — the shipped priced items carry Price)
    // BonusPrice (the JS item `od`; `nn()` = `od` with any discount). The
    // Ruby/crystal cost shown at the `pVa` RubyButton (`Ne.Wub` L2254) and
    // charged by `Pa.EYa` L1228 (`p.o.fd >= a.nn()`). Items with no `Price`
    // but a `BonusPrice` are crystal-only shop rows (120 shipped).
    int bonus_price = 0;
    int level = 1;          // Level
    // `Level` attr PRESENT (JS `xf` = `u.I(Level)`; null when absent).
    // `?Item[x].Level` answers "null" for a Level-less row (`cdb` L977).
    bool has_level = false;
    int weapon_damage = 0;  // WeaponDamage
    int body_defense = 0;   // BodyDefense
    int head_defense = 0;   // HeadDefense
    int unarmed_damage = 0; // UnarmedDamage
    int magic_damage = 0;   // MagicDamage (Magic items; Ranged carries none)
    int delivery_sec = 0;   // DeliveryTime/Ec (timed delivery; 0 = instant)
    int delivery_coin = 0;  // MoneyDeliveryPrice (O2 instant-delivery fee)
    int delivery_gems = 0;  // BonusDeliveryPrice (Od instant-delivery fee)
    // JS `Mn`/`Ip` (item ctor L164808/164852:
    // `this.Mn=xb(a.attributes.get("RecieveGold"))`,
    // `this.Ip=xb(a.attributes.get("RecieveBonus"))`). `?Item[x].RecieveGold`
    // -> `K.T(c.Mn)`, `.RecieveBonus` -> `K.T(c.Ip)` (`cdb` L978).
    int recieve_gold = 0;   // RecieveGold
    int recieve_bonus = 0;  // RecieveBonus
    bool shop_hide = false; // ShopHide="1" (not offered in the shop)
    bool hidden = false;    // Hidden="1"
    bool paid = false;      // PaidItem="Paid"/"SuperPaid" (premium-only)
    // Raw `PaidItem` attr (`D3`, ctor default "None") — `?Purchase[x].PaidItem`
    // returns this string (`IJa` L980).
    std::string paid_item = "None";
    // JS item ctor L321-327: `this.badge = Badge ?? ""` (the `I.QPa`
    // "MostPopular" / `I.PPa` "BestValue" sale-flag switch, `ns.j5` L2309) and
    // `this.bU = ShopLabel ?? ""` (the `pieces/Stripe` badge text). Both are
    // ABSENT from the shipped list.xml (0 rows), so the `bU` branch never fires.
    std::string badge;       // Badge
    std::string shop_label;  // ShopLabel (`bU`)
    // JS `this.Ms = u.I(a.attributes.get("AddPercent"))` (L326) and
    // `this.Zz = u.ka(a.attributes.get("ConsumableProduct"))` (L324). The sale
    // gate `a = bc.Zz && bc.Ms > 0` (L2308) drives the `Di` badge on the
    // RealMoneyItem rows (35 `AddPercent` / 46 `ConsumableProduct` shipped).
    int add_percent = 0;             // AddPercent
    bool consumable_product = false; // ConsumableProduct="1"
    std::vector<ItemPerkRef> perks;  // `<Perks>` + `<Enchantments>` rows
    // The item's combat stats (JS `this.attributes`, a `ud` map; item ctor
    // `for(e of v.eo.attributes) node.attributes.get(e.name)!=null &&
    // this.attributes.set(e.name, u.I(...))`). Raw list.xml XML-attribute
    // values keyed by attribute name — `ms.setParameters` (JS L2274) walks
    // `v.eo.attributes` and reads `a.attributes.get(h.name, out)` to build the
    // shop detail's attribute list. Only the names that appear as XML
    // attributes are present.
    std::map<std::string, int> attributes;
    // Owned-equip status comes from the save (users.xml <Items>), not here.

    // --- shop-offer definition (JS `hh`/`pl`, built for every list.xml item
    // whose `SubType` is "Offer"/"DailyOffer" — `mt.Mga` L175260, bucketed
    // into the catalog's `gHa` by `Lia` L86475). The controller (`nt` g="5B",
    // `p.Cw.It`) wraps each into an `hh` (lp 0) or `pl` (lp 1, DailyOffer).
    // These ARE NOT the EDiscount `yf` offer (`EngineItemOffer` in
    // quest_engine.hpp) — that is the `Pn`/`EDiscount` price override.
    bool is_offer = false;             // `mt.Mga`: SubType in {Offer, DailyOffer}
    std::string offer_kind;            // JS `Yb` (SubType): "Offer"|"DailyOffer"
    std::string offer_text;            // `text` <- Text (dialog Title)
    std::string offer_description;     // `description` <- Description
    std::string offer_profit_image;    // `Xt` <- ProfitImage
    std::string offer_button_image;    // `dZ` <- ButtonImage (map-button art)
    std::string offer_real_price;      // `xr` <- RealPrice ("$2.99")
    std::string offer_focus_on_buy;    // `O_` <- FocusOnBuy
    bool offer_show_last_chance = false;  // `dU` <- ShowLastChance (`u.ka`)
    int offer_duration = 0;            // `duration` <- Duration (seconds)
    std::vector<std::string> offer_items;  // `Ht` <- <OfferItems><Item Name>
    std::vector<OfferCondition> offer_conditions;  // `CE` <- <OfferConditions>
};

// Parses list.xml into the item list (JS `it.parse`). `xml_text` is the
// extracted list.xml document. Throws std::runtime_error on malformed XML.
std::vector<CatalogItem> parse_item_catalog(const std::string& xml_text);

// The shop-visible subset (JS `Oa.f5` tab lists): non-hidden, non-paid
// Weapon/Armor/Helm items with a gold Price.
std::vector<CatalogItem> shop_items(const std::vector<CatalogItem>& all);

// One `internal_settings.xml` `<Attributes><Attribute>` definition (JS `gp`,
// built by `ow.parse` L615263 into `v.eo.attributes`). `ms.setParameters`
// (JS L2274) iterates these in file order; `fi.init` (L2270-2271) resolves the
// row icon as `"attributes/" + Icon` in atlas 248 (fallback: `Icon` in 266).
struct ShopAttributeDef {
    const char* name;  // `Name`  ("WeaponDamage")
    const char* icon;  // `Icon`  ("weapon_attack"; "" when the XML has none)
    bool hidden;       // `Hidden="1"` — `ms` skips these (`!h.hidden`)
};

// The shipped `<Attributes>` defs (internal_settings.xml L21744-23450), in
// file order. `ShopHidden`/`ProfileHidden` do NOT gate the shop list — only
// `Hidden` does (JS L2274-2275 checks `!h.hidden`), so `CriticalRating`
// (`ShopHidden="1"`, no `Hidden`) is included.
const std::vector<ShopAttributeDef>& shop_attribute_defs();

} // namespace sf2::app

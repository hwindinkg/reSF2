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

// One list.xml <Item> (JS `p.items.Xm` element).
struct CatalogItem {
    std::string name;       // Name ("WEAPON_KNIVES")
    std::string type;       // Type ("Weapon"/"Armor"/"Helm"/"Ranged"/"Magic")
    std::string subtype;    // SubType ("Knives", "" for armor/helm)
    std::string model;      // Model ("mdl_weapon_knives", "" when none)
    std::string image;      // Image (the shop card art ref)
    int price = 0;          // Price (gold; the JS `jp()` uses `mi` when no
                            // price attr — the shipped priced items carry Price)
    int level = 1;          // Level
    int weapon_damage = 0;  // WeaponDamage
    int body_defense = 0;   // BodyDefense
    int head_defense = 0;   // HeadDefense
    int unarmed_damage = 0; // UnarmedDamage
    int magic_damage = 0;   // MagicDamage (Magic items; Ranged carries none)
    int delivery_sec = 0;   // DeliveryTime/Ec (timed delivery; 0 = instant)
    bool shop_hide = false; // ShopHide="1" (not offered in the shop)
    bool hidden = false;    // Hidden="1"
    bool paid = false;      // PaidItem="Paid"/"SuperPaid" (premium-only)
    std::vector<ItemPerkRef> perks;  // `<Perks>` + `<Enchantments>` rows
    // Owned-equip status comes from the save (users.xml <Items>), not here.
};

// Parses list.xml into the item list (JS `it.parse`). `xml_text` is the
// extracted list.xml document. Throws std::runtime_error on malformed XML.
std::vector<CatalogItem> parse_item_catalog(const std::string& xml_text);

// The shop-visible subset (JS `Oa.f5` tab lists): non-hidden, non-paid
// Weapon/Armor/Helm items with a gold Price.
std::vector<CatalogItem> shop_items(const std::vector<CatalogItem>& all);

} // namespace sf2::app

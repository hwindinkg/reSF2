// Item catalog implementation — parses res/list.xml <Items> (JS `it` g="5A").

#include "app/item_catalog.hpp"

#include <stdexcept>

#include "xml_doc.hpp"

namespace sf2::app {

namespace {

// The JS `I` item-type enum values (L1271963).
constexpr const char* kTypeWeapon = "Weapon";
constexpr const char* kTypeArmor = "Armor";
constexpr const char* kTypeHelm = "Helm";

bool attr_bool_str(const char* v) { return v != nullptr && std::string(v) == "1"; }

} // namespace

// `internal_settings.xml` `<Attributes>` (L21744-23450), file order. The JS
// `ow.parse` (L615263) reads Name/Icon/Hidden; `ms`/`fi` (L2274-2275) consume
// them in this order.
const std::vector<ShopAttributeDef>& shop_attribute_defs() {
    static const std::vector<ShopAttributeDef> kDefs = {
        {"HeadDefense", "head_armor", false},
        {"BodyDefense", "body_armor", false},
        {"UnarmedDamage", "unarmed_attack", false},
        {"WeaponDamage", "weapon_attack", false},
        {"RangedDamage", "ranged_attack", false},
        {"MagicDamage", "magic_attack", false},
        {"CriticalChance", "critical_chance", true},
        {"CriticalRating", "critical_chance", false},  // ShopHidden, not Hidden
        {"BlockDamageFactor", "", true},
        {"DamageFactor", "", true},
        {"RangedQuantity", "ranged_quantity", true},
        {"CriticalDamage", "", true},
        {"MagicInitialCharge", "", true},
        {"MagicPainRecharge", "", true},
        {"MagicDamageRecharge", "", true},
        {"RegenerationRate", "", true},
        {"Lifesteal", "", true},
        {"ShockCriticalHitChance", "", true},
        {"ShockHeadHitChance", "", true},
        {"EnchantmentResistance", "", true},
    };
    return kDefs;
}

std::vector<CatalogItem> parse_item_catalog(const std::string& xml_text) {
    std::vector<CatalogItem> out;
    sf2::data::xml_doc doc;
    doc.parse(xml_text);

    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "List") {
        throw std::runtime_error("item catalog: root <List> missing");
    }
    const pugi::xml_node items = root.child("Items");
    if (!items) {
        return out;
    }
    for (const pugi::xml_node item : items.children("Item")) {
        CatalogItem ci;
        if (item.attribute("Name")) ci.name = item.attribute("Name").value();
        if (item.attribute("Type")) ci.type = item.attribute("Type").value();
        if (item.attribute("SubType")) ci.subtype = item.attribute("SubType").value();
        // JS `pL` L322: `lock` = `PackLabel` (the GroupID fallback never occurs
        // in the shipped list.xml — 378 `PackLabel`, 0 `GroupID`).
        if (item.attribute("PackLabel")) ci.pack_label = item.attribute("PackLabel").value();
        if (item.attribute("Model")) ci.model = item.attribute("Model").value();
        if (item.attribute("Image")) ci.image = item.attribute("Image").value();
        ci.price = sf2::data::xml_attr_int(item, "Price", 0);
        // JS `od` (item ctor): the `BonusPrice` (Ruby/crystal) cost.
        ci.bonus_price = sf2::data::xml_attr_int(item, "BonusPrice", 0);
        ci.level = sf2::data::xml_attr_int(item, "Level", 1);
        ci.has_level = static_cast<bool>(item.attribute("Level"));
        ci.weapon_damage = sf2::data::xml_attr_int(item, "WeaponDamage", 0);
        ci.body_defense = sf2::data::xml_attr_int(item, "BodyDefense", 0);
        ci.head_defense = sf2::data::xml_attr_int(item, "HeadDefense", 0);
        ci.unarmed_damage = sf2::data::xml_attr_int(item, "UnarmedDamage", 0);
        ci.magic_damage = sf2::data::xml_attr_int(item, "MagicDamage", 0);
        ci.delivery_sec = sf2::data::xml_attr_int(item, "DeliveryTime", 0);
        ci.delivery_coin = sf2::data::xml_attr_int(item, "MoneyDeliveryPrice", 0);
        ci.delivery_gems = sf2::data::xml_attr_int(item, "BonusDeliveryPrice", 0);
        // JS `xb(a.attributes.get("RecieveGold"/"RecieveBonus"))` (L164808/164852).
        ci.recieve_gold = sf2::data::xml_attr_int(item, "RecieveGold", 0);
        ci.recieve_bonus = sf2::data::xml_attr_int(item, "RecieveBonus", 0);
        ci.shop_hide = attr_bool_str(item.attribute("ShopHide").value());
        ci.hidden = attr_bool_str(item.attribute("Hidden").value());
        ci.paid_item =
            item.attribute("PaidItem") ? item.attribute("PaidItem").value() : "None";
        if (item.attribute("PaidItem")) ci.paid = true;
        // JS item ctor L321-327: `badge` (Badge), `bU` (ShopLabel), `Ms`
        // (AddPercent) and `Zz` (ConsumableProduct). `ns.j5` (L2308-2309)
        // reads all four for the shop-cell sale badge.
        if (item.attribute("Badge")) ci.badge = item.attribute("Badge").value();
        if (item.attribute("ShopLabel")) ci.shop_label = item.attribute("ShopLabel").value();
        ci.add_percent = sf2::data::xml_attr_int(item, "AddPercent", 0);
        ci.consumable_product =
            attr_bool_str(item.attribute("ConsumableProduct").value());
        // JS item ctor: `for(e of v.eo.attributes) { let f=a.attributes.get(e.name);
        // f!=null && this.attributes.set(e.name, u.I(f)) }` — the item's combat
        // stats, keyed by the `internal_settings.xml` attribute names.
        for (const ShopAttributeDef& def : shop_attribute_defs()) {
            if (item.attribute(def.name)) {
                ci.attributes[def.name] =
                    sf2::data::xml_attr_int(item, def.name, 0);
            }
        }
        // `<Perks>` + `<Enchantments>` rows (JS `xe.Qd` be-entries, L1257):
        // perk name + `<Set>` overrides (numeric vs string by parse).
        for (const char* section : {"Perks", "Enchantments"}) {
            const pugi::xml_node sec = item.child(section);
            if (!sec) continue;
            const bool enchant = std::string(section) == "Enchantments";
            for (const pugi::xml_node perk : sec.children("Perk")) {
                if (!perk.attribute("Name")) continue;
                ItemPerkRef ref;
                ref.name = perk.attribute("Name").value();
                ref.enchant = enchant;
                const pugi::xml_node set = perk.child("Set");
                if (set) {
                    for (const pugi::xml_attribute a : set.attributes()) {
                        try {
                            std::size_t pos = 0;
                            const double d = std::stod(a.value(), &pos);
                            if (pos == std::string(a.value()).size()) {
                                ref.set_num[a.name()] = d;
                                continue;
                            }
                        } catch (...) {
                        }
                        ref.set_str[a.name()] = a.value();
                    }
                }
                ci.perks.push_back(std::move(ref));
            }
        }
        // Shop-offer definition (JS item ctor L162822-16733x). `mt.Mga`
        // L175260: `a.Yb != "Offer" ? a.Yb == "DailyOffer" : true` — i.e. the
        // `SubType` is "Offer" or "DailyOffer" (both carry `Type="RealMoneyItem"`,
        // `I.wk` L1272054).
        ci.is_offer = ci.subtype == "Offer" || ci.subtype == "DailyOffer";
        if (ci.is_offer) {
            ci.offer_kind = ci.subtype;
            if (item.attribute("Text")) ci.offer_text = item.attribute("Text").value();
            if (item.attribute("Description"))
                ci.offer_description = item.attribute("Description").value();
            if (item.attribute("ProfitImage"))
                ci.offer_profit_image = item.attribute("ProfitImage").value();
            if (item.attribute("ButtonImage"))
                ci.offer_button_image = item.attribute("ButtonImage").value();
            if (item.attribute("RealPrice"))
                ci.offer_real_price = item.attribute("RealPrice").value();
            if (item.attribute("FocusOnBuy"))
                ci.offer_focus_on_buy = item.attribute("FocusOnBuy").value();
            ci.offer_show_last_chance =
                attr_bool_str(item.attribute("ShowLastChance").value());
            ci.offer_duration = sf2::data::xml_attr_int(item, "Duration", 0);
            // `<OfferItems><Item Name=..>` -> `Ht` (Lt entries; the shipped
            // `AllItemsRecieved`/`Nga` paths read only the names).
            const pugi::xml_node offer_items = item.child("OfferItems");
            if (offer_items) {
                for (const pugi::xml_node oi : offer_items.children("Item")) {
                    if (oi.attribute("Name"))
                        ci.offer_items.push_back(oi.attribute("Name").value());
                }
            }
            // `<OfferConditions>` -> `CE` (leaf conditions; `And`/`Operator`
            // nested rows are kept as-is and never pass — no shipped use).
            const pugi::xml_node offer_conditions = item.child("OfferConditions");
            if (offer_conditions) {
                for (const pugi::xml_node oc : offer_conditions.children()) {
                    if (oc.type() != pugi::node_element) continue;
                    OfferCondition cond;
                    cond.kind = oc.name();
                    if (oc.attribute("Value1")) cond.value1 = oc.attribute("Value1").value();
                    if (oc.attribute("Value2")) cond.value2 = oc.attribute("Value2").value();
                    cond.invert = attr_bool_str(oc.attribute("Not").value());
                    ci.offer_conditions.push_back(std::move(cond));
                }
            }
        }
        out.push_back(std::move(ci));
    }
    return out;
}

// The shop-visible items (JS `Oa.f5` tab lists): the non-hidden, non-paid,
// gold-priced Weapon/Armor/Helm entries.
std::vector<CatalogItem> shop_items(const std::vector<CatalogItem>& all) {
    std::vector<CatalogItem> out;
    for (const CatalogItem& ci : all) {
        if (ci.type != kTypeWeapon && ci.type != kTypeArmor && ci.type != kTypeHelm) {
            continue;
        }
        if (ci.shop_hide || ci.hidden || ci.paid) {
            continue;
        }
        if (ci.price <= 0) {
            continue;
        }
        out.push_back(ci);
    }
    return out;
}

} // namespace sf2::app

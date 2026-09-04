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
        if (item.attribute("Model")) ci.model = item.attribute("Model").value();
        if (item.attribute("Image")) ci.image = item.attribute("Image").value();
        ci.price = sf2::data::xml_attr_int(item, "Price", 0);
        ci.level = sf2::data::xml_attr_int(item, "Level", 1);
        ci.weapon_damage = sf2::data::xml_attr_int(item, "WeaponDamage", 0);
        ci.body_defense = sf2::data::xml_attr_int(item, "BodyDefense", 0);
        ci.head_defense = sf2::data::xml_attr_int(item, "HeadDefense", 0);
        ci.unarmed_damage = sf2::data::xml_attr_int(item, "UnarmedDamage", 0);
        ci.magic_damage = sf2::data::xml_attr_int(item, "MagicDamage", 0);
        ci.delivery_sec = sf2::data::xml_attr_int(item, "DeliveryTime", 0);
        ci.shop_hide = attr_bool_str(item.attribute("ShopHide").value());
        ci.hidden = attr_bool_str(item.attribute("Hidden").value());
        if (item.attribute("PaidItem")) ci.paid = true;
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

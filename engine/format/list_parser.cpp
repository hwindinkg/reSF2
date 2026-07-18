#include "list_parser.hpp"
#include "xml_doc.hpp"
#include <cstdio>
#include <fstream>
#include <charconv>

namespace resf2::format {

static float to_float(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    float v;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} ? v : def;
}

static int to_int(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    int v;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} ? v : def;
}

static bool to_bool(const std::string& s) {
    return s == "1" || s == "true";
}

static void parse_upgrade_node(const XmlNode& node, ListUpgrade& u) {
    u.level = to_int(node.attr("Level"));
    u.price = to_int(node.attr("Price"));
    u.bonus_price = to_int(node.attr("BonusPrice"));
    u.upgrade_level = to_int(node.attr("UpgradeLevel"));
    u.weapon_damage = to_float(node.attr("WeaponDamage"));
    u.unarmed_damage = to_float(node.attr("UnarmedDamage"));
    u.body_defense = to_float(node.attr("BodyDefense"));
    u.head_defense = to_float(node.attr("HeadDefense"));
    u.ranged_damage = to_float(node.attr("RangedDamage"));
    u.magic_damage = to_float(node.attr("MagicDamage"));
    u.delivery_time = to_int(node.attr("DeliveryTime"));
    u.bonus_delivery_price = to_int(node.attr("BonusDeliveryPrice"));
    u.milestone = to_int(node.attr("Milestone"));
}

static void parse_upgrades_block(const XmlNode& node, ListUpgradeBlock& block) {
    block.template_name = node.attr("Template");
    if (!block.template_name.empty()) {
        // If Template attr is set, check for inline override children
        for (auto& child : node.children) {
            if (child.name == "Upgrade") {
                ListUpgrade u;
                parse_upgrade_node(child, u);
                block.upgrades.push_back(std::move(u));
            }
        }
    } else {
        // Named template in <UpgradeList>
        block.template_name = node.attr("Name");
        for (auto& child : node.children) {
            if (child.name == "Upgrade") {
                ListUpgrade u;
                parse_upgrade_node(child, u);
                block.upgrades.push_back(std::move(u));
            }
        }
    }
}

static void parse_perk(const XmlNode& node, ListPerk& perk) {
    perk.name = node.attr("Name");
    for (auto& child : node.children) {
        if (child.name == "Set") {
            for (auto& attr : child.attributes) {
                perk.params.push_back(attr.name);
                perk.params.push_back(attr.value);
            }
        }
    }
}

static void parse_item(const XmlNode& node, ListItem& item) {
    item.name = node.attr("Name");
    item.type = node.attr("Type");
    item.subtype = node.attr("SubType");
    item.image = node.attr("Image");
    item.model = node.attr("Model");
    item.level = to_int(node.attr("Level"));
    item.weapon_damage = to_float(node.attr("WeaponDamage"));
    item.unarmed_damage = to_float(node.attr("UnarmedDamage"));
    item.body_defense = to_float(node.attr("BodyDefense"));
    item.head_defense = to_float(node.attr("HeadDefense"));
    item.ranged_damage = to_float(node.attr("RangedDamage"));
    item.magic_damage = to_float(node.attr("MagicDamage"));
    item.price = to_int(node.attr("Price"));
    item.bonus_price = to_int(node.attr("BonusPrice"));
    item.upgrade_level = to_int(node.attr("UpgradeLevel"));
    item.paid_item = node.attr("PaidItem");
    item.shop_hide = to_bool(node.attr("ShopHide"));
    item.hidden = to_bool(node.attr("Hidden"));
    item.pack_label = node.attr("PackLabel");

    item.is_paid = to_bool(node.attr("isPaid"));
    item.real_price = node.attr("RealPrice");
    item.receive_bonus = to_int(node.attr("RecieveBonus"));

    for (auto& child : node.children) {
        if (child.name == "Upgrades") {
            ListUpgradeBlock block;
            parse_upgrades_block(child, block);
            item.upgrades.push_back(std::move(block));
        } else if (child.name == "Enchantments") {
            for (auto& pk : child.children) {
                if (pk.name == "Perk") {
                    ListPerk perk;
                    parse_perk(pk, perk);
                    item.enchantments.push_back(std::move(perk));
                }
            }
        } else if (child.name == "Perks") {
            for (auto& pk : child.children) {
                if (pk.name == "Perk") {
                    ListPerk perk;
                    parse_perk(pk, perk);
                    item.perks.push_back(std::move(perk));
                }
            }
        }
    }
}

bool ListParser::parse(const std::string& xml, ListData& out) {
    XmlDocument doc;
    if (!doc.parse(xml)) {
        error_ = doc.error();
        return false;
    }

    auto* root = doc.root()->first_child("List");
    if (!root) {
        error_ = "No <List> root element";
        return false;
    }

    for (auto& section : root->children) {
        if (section.name == "Items") {
            // Parse all <Item> elements
            for (auto& node : section.children) {
                if (node.name == "Item") {
                    ListItem item;
                    parse_item(node, item);
                    out.items.push_back(std::move(item));
                }
            }
        } else if (section.name == "UpgradeList") {
            // Parse named upgrade templates
            for (auto& node : section.children) {
                if (node.name == "Upgrades") {
                    ListUpgradeBlock block;
                    parse_upgrades_block(node, block);
                    out.upgrade_templates.push_back(std::move(block));
                }
            }
        } else if (section.name == "ItemSets") {
            // Parse item set definitions
            for (auto& node : section.children) {
                if (node.name == "ItemSet") {
                    ListItemSet set;
                    set.name = node.attr("Name");
                    set.title = node.attr("Title");
                    set.text = node.attr("Text");
                    set.brief = node.attr("Brief");
                    for (auto& entry : node.children) {
                        if (entry.name == "Item") {
                            ListItemSetEntry e;
                            e.name = entry.attr("Name");
                            e.scale = to_float(entry.attr("Scale"), 1.0f);
                            e.rotate = to_float(entry.attr("Rotate"));
                            e.x = to_float(entry.attr("X"));
                            e.y = to_float(entry.attr("Y"));
                            e.icons_y = to_float(entry.attr("IconsY"));
                            set.items.push_back(std::move(e));
                        }
                    }
                    out.item_sets.push_back(std::move(set));
                }
            }
        }
    }

    return true;
}

bool ListParser::load_file(const std::string& path, ListData& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        error_ = "Cannot open: " + path;
        return false;
    }
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return parse(data, out);
}

} // namespace resf2::format

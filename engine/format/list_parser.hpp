#pragma once

#include <string>
#include <vector>

namespace resf2::format {

struct ListUpgrade {
    int level = 0;
    int price = 0;
    int bonus_price = 0;
    int upgrade_level = 0;
    float weapon_damage = 0;
    float unarmed_damage = 0;
    float body_defense = 0;
    float head_defense = 0;
    float ranged_damage = 0;
    float magic_damage = 0;
    int delivery_time = 0;
    int bonus_delivery_price = 0;
    int milestone = 0;
};

struct ListUpgradeBlock {
    std::string template_name;  // empty for inline
    std::vector<ListUpgrade> upgrades;
};

struct ListPerk {
    std::string name;
    // Key-value params stored as alternating key/value pairs
    std::vector<std::string> params;
};

struct ListEnchantment {
    std::vector<ListPerk> perks;
};

struct ListItem {
    std::string name;
    std::string type;
    std::string subtype;
    std::string image;
    std::string model;
    int level = 0;
    float weapon_damage = 0;
    float unarmed_damage = 0;
    float body_defense = 0;
    float head_defense = 0;
    float ranged_damage = 0;
    float magic_damage = 0;
    int price = 0;
    int bonus_price = 0;
    int upgrade_level = 0;
    std::string paid_item;
    bool shop_hide = false;
    bool hidden = false;
    std::string pack_label;

    // IAP
    bool is_paid = false;
    std::string real_price;
    int receive_bonus = 0;

    // Upgrades (inline or template ref)
    std::vector<ListUpgradeBlock> upgrades;

    // Enchantments / Perks
    std::vector<ListPerk> enchantments;
    std::vector<ListPerk> perks;
};

struct ListItemSetEntry {
    std::string name;
    float scale = 1.0f;
    float rotate = 0;
    float x = 0;
    float y = 0;
    float icons_y = 0;
};

struct ListItemSet {
    std::string name;
    std::string title;
    std::string text;
    std::string brief;
    std::vector<ListItemSetEntry> items;
};

struct ListData {
    std::vector<ListItem> items;
    std::vector<ListUpgradeBlock> upgrade_templates;
    std::vector<ListItemSet> item_sets;
};

class ListParser {
public:
    bool parse(const std::string& xml, ListData& out);
    bool load_file(const std::string& path, ListData& out);
    const std::string& error() const { return error_; }

private:
    std::string error_;
};

} // namespace resf2::format

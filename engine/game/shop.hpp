// engine/game/shop.hpp
//
// Shop manager for reSF2.
// Handles the item catalog (derived from list.xml) and buy/sell transactions.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "../format/list_parser.hpp"

namespace resf2::shop {

// ---------- Shop item ----------
//
// A display-friendly representation of a single purchasable item.
// Derived from format::ListItem during catalog loading.

struct ShopItem {
    std::string id;          // matches ListItem.name (unique identifier)
    std::string name;        // display name
    std::string category;    // "Weapon", "Armor", "Helm", "Ranged", "Magic"
    int price = 0;
    int level_req = 1;
    float weapon_damage = 0;
    float body_defense = 0;
    float head_defense = 0;
    float ranged_damage = 0;
    float magic_damage = 0;
    bool is_paid = false;     // IAP item (not purchasable with gold)
};

// Map item type from list.xml to a display category.
// Returns nullptr if the type is not a valid shop category.
inline const char* item_type_to_slot(const std::string& type) {
    if (type == "Weapon") return "weapon";
    if (type == "Armor")  return "armor";
    if (type == "Helm")   return "helmet";
    if (type == "Ranged") return "ranged";
    if (type == "Magic")  return "magic";
    return nullptr;
}

// ---------- ShopManager ----------
//
// Owns the catalog of buyable items and handles purchase/sale transactions.

class ShopManager {
public:
    ShopManager() = default;

    // Load the catalog from parsed list.xml data.
    // Only items with a valid price and not shop_hidden are included.
    void load_catalog(const format::ListData& list_data);

    // Get all items in the catalog belonging to a given category.
    // category: "Weapon", "Armor", "Helm", "Ranged", "Magic"
    const std::vector<ShopItem>& get_items(const std::string& category) const;

    // Find a shop item by its ID (name). Returns null if not found.
    const ShopItem* find_item(const std::string& item_id) const;

    // Get all items in the catalog (unfiltered).
    const std::vector<ShopItem>& all_items() const noexcept { return catalog_; }

    // Check if an item can be purchased given the player's gold and level.
    bool can_buy(const std::string& item_id, int current_gold, int player_level) const;

    // --- Transaction helpers (caller handles inventory/currency) ---

    // Get the buy price of an item. Returns 0 if not found.
    int buy_price(const std::string& item_id) const;

    // Get the sell price of an item (typically 50% of buy price).
    int sell_price(const std::string& item_id) const;

    // Get the level requirement of an item. Returns 1 if not found.
    int level_requirement(const std::string& item_id) const;

    // Get the category/slot for an item ID.
    std::string item_category(const std::string& item_id) const;

private:
    std::vector<ShopItem> catalog_;

    // Index by item ID for O(1) lookup.
    // Points into catalog_ — valid as long as catalog_ is not modified.
    std::unordered_map<std::string, size_t> index_;

    // Rebuild the index after catalog changes.
    void rebuild_index();
};

} // namespace resf2::shop

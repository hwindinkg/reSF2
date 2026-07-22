// engine/game/shop.cpp
//
// ShopManager implementation.

#include "shop.hpp"

#include <algorithm>
#include <cstdio>

namespace resf2::shop {

// ============================================================
// Catalog loading
// ============================================================

void ShopManager::load_catalog(const format::ListData& list_data) {
    catalog_.clear();
    index_.clear();

    for (const auto& item : list_data.items) {
        // Skip hidden, shop_hide, or no-price items
        if (item.shop_hide || item.hidden || item.price <= 0) continue;
        // Skip IAP-only items (PaidItem set or is_paid)
        if (item.is_paid || !item.paid_item.empty()) {
            // Include for display but mark as paid
        }

        ShopItem si;
        si.id = item.name;
        si.name = item.name;
        si.category = item.type;  // "Weapon", "Armor", "Helm", "Ranged", "Magic"
        si.price = item.price;
        si.level_req = item.level;
        si.weapon_damage = item.weapon_damage;
        si.body_defense = item.body_defense;
        si.head_defense = item.head_defense;
        si.ranged_damage = item.ranged_damage;
        si.magic_damage = item.magic_damage;
        si.is_paid = item.is_paid || !item.paid_item.empty();

        catalog_.push_back(std::move(si));
    }

    rebuild_index();
    std::printf("[shop] loaded catalog: %zu items\n", catalog_.size());
}

void ShopManager::rebuild_index() {
    index_.clear();
    for (size_t i = 0; i < catalog_.size(); ++i) {
        index_[catalog_[i].id] = i;
    }
}

// ============================================================
// Catalog queries
// ============================================================

const std::vector<ShopItem>& ShopManager::get_items(const std::string& category) const {
    // We cache filtered views in a map for efficiency
    static std::unordered_map<std::string, std::vector<ShopItem>> cache;
    auto it = cache.find(category);
    if (it != cache.end()) return it->second;

    std::vector<ShopItem> filtered;
    for (const auto& si : catalog_) {
        if (si.category == category) {
            filtered.push_back(si);
        }
    }
    auto result = cache.emplace(category, std::move(filtered));
    return result.first->second;
}

const ShopItem* ShopManager::find_item(const std::string& item_id) const {
    auto it = index_.find(item_id);
    if (it == index_.end()) return nullptr;
    return &catalog_[it->second];
}

// ============================================================
// Transaction helpers
// ============================================================

bool ShopManager::can_buy(const std::string& item_id, int current_gold, int player_level) const {
    auto* item = find_item(item_id);
    if (!item) return false;
    if (item->is_paid) return false;  // IAP only
    if (current_gold < item->price) return false;
    if (player_level < item->level_req) return false;
    return true;
}

int ShopManager::buy_price(const std::string& item_id) const {
    auto* item = find_item(item_id);
    return item ? item->price : 0;
}

int ShopManager::sell_price(const std::string& item_id) const {
    auto* item = find_item(item_id);
    // Sell at 50% of buy price
    return item ? std::max(1, item->price / 2) : 0;
}

int ShopManager::level_requirement(const std::string& item_id) const {
    auto* item = find_item(item_id);
    return item ? item->level_req : 1;
}

std::string ShopManager::item_category(const std::string& item_id) const {
    auto* item = find_item(item_id);
    return item ? item->category : std::string{};
}

} // namespace resf2::shop

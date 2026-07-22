// engine/game/player.cpp
//
// Player profile implementation.

#include "player.hpp"

#include <algorithm>
#include <cstdio>

namespace resf2::player {

PlayerProfile::PlayerProfile() = default;

// ============================================================
// Currency
// ============================================================

void PlayerProfile::add_currency(int amount) {
    currency_ += amount;
    std::printf("[player] currency +%d = %d\n", amount, currency_);
}

bool PlayerProfile::spend_currency(int amount) {
    if (currency_ < amount) return false;
    currency_ -= amount;
    std::printf("[player] currency -%d = %d\n", amount, currency_);
    return true;
}

// ============================================================
// Battle stats
// ============================================================

void PlayerProfile::add_win() {
    wins_++;
    // Level up every 5 wins (matches original game's rough pacing)
    int new_level = 1 + wins_ / 5;
    if (new_level > level_) {
        level_ = new_level;
        std::printf("[player] LEVEL UP! Now level %d (wins=%d)\n", level_, wins_);
    }
    std::printf("[player] win recorded (%d total)\n", wins_);
}

void PlayerProfile::add_loss() {
    losses_++;
    std::printf("[player] loss recorded (%d total)\n", losses_);
}

// ============================================================
// Level tracking
// ============================================================

void PlayerProfile::complete_level(const std::string& level) {
    for (const auto& l : completed_levels_) {
        if (l == level) return;  // already completed
    }
    completed_levels_.push_back(level);
    std::printf("[player] level completed: %s (%zu total)\n",
                level.c_str(), completed_levels_.size());
}

bool PlayerProfile::is_level_completed(const std::string& level) const {
    for (const auto& l : completed_levels_) {
        if (l == level) return true;
    }
    return false;
}

void PlayerProfile::set_current_level(const std::string& level) {
    current_level_ = level;
}

// ============================================================
// Inventory
// ============================================================

void PlayerProfile::add_item(const std::string& item_id) {
    if (item_id.empty()) return;
    for (const auto& id : owned_items_) {
        if (id == item_id) return;  // already owned
    }
    owned_items_.push_back(item_id);
    std::printf("[player] acquired item: %s\n", item_id.c_str());
}

bool PlayerProfile::has_item(const std::string& item_id) const {
    for (const auto& id : owned_items_) {
        if (id == item_id) return true;
    }
    return false;
}

bool PlayerProfile::remove_item(const std::string& item_id) {
    if (item_id.empty()) return false;
    // If equipped, unequip first
    if (equipped_weapon_ == item_id) equipped_weapon_.clear();
    if (equipped_armor_ == item_id)  equipped_armor_.clear();
    if (equipped_helmet_ == item_id) equipped_helmet_.clear();
    if (equipped_ranged_ == item_id) equipped_ranged_.clear();
    if (equipped_magic_ == item_id)  equipped_magic_.clear();
    // Remove from owned items
    for (auto it = owned_items_.begin(); it != owned_items_.end(); ++it) {
        if (*it == item_id) {
            owned_items_.erase(it);
            std::printf("[player] removed item: %s\n", item_id.c_str());
            return true;
        }
    }
    return false;
}

// ============================================================
// Equipment
// ============================================================

bool PlayerProfile::equip_item(const std::string& slot, const std::string& item_id) {
    if (!item_id.empty() && !has_item(item_id)) {
        std::printf("[player] cannot equip %s in %s: not owned\n",
                    item_id.c_str(), slot.c_str());
        return false;
    }
    if (slot == "weapon") {
        equipped_weapon_ = item_id;
    } else if (slot == "armor") {
        equipped_armor_ = item_id;
    } else if (slot == "helmet") {
        equipped_helmet_ = item_id;
    } else if (slot == "ranged") {
        equipped_ranged_ = item_id;
    } else if (slot == "magic") {
        equipped_magic_ = item_id;
    } else {
        std::printf("[player] unknown equipment slot: %s\n", slot.c_str());
        return false;
    }
    std::printf("[player] equipped %s in slot %s\n",
                item_id.empty() ? "(none)" : item_id.c_str(), slot.c_str());
    return true;
}

// ============================================================
// Serialization
// ============================================================

save::SaveData PlayerProfile::to_save_data() const {
    save::SaveData data;
    data.version        = 1;
    data.currency       = currency_;
    data.level          = level_;
    data.wins           = wins_;
    data.losses         = losses_;
    data.current_level  = current_level_;
    data.completed_levels = completed_levels_;
    data.owned_items    = owned_items_;
    data.equipped_weapon = equipped_weapon_;
    data.equipped_armor  = equipped_armor_;
    data.equipped_helmet = equipped_helmet_;
    data.equipped_ranged = equipped_ranged_;
    data.equipped_magic  = equipped_magic_;
    return data;
}

PlayerProfile PlayerProfile::from_save_data(const save::SaveData& data) {
    PlayerProfile profile;
    profile.currency_       = data.currency;
    profile.level_          = data.level;
    profile.wins_           = data.wins;
    profile.losses_         = data.losses;
    profile.current_level_  = data.current_level;
    profile.completed_levels_ = data.completed_levels;
    profile.owned_items_    = data.owned_items;
    profile.equipped_weapon_ = data.equipped_weapon;
    profile.equipped_armor_  = data.equipped_armor;
    profile.equipped_helmet_ = data.equipped_helmet;
    profile.equipped_ranged_ = data.equipped_ranged;
    profile.equipped_magic_  = data.equipped_magic;
    return profile;
}

}  // namespace resf2::player

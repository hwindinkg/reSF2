// engine/game/inventory.cpp
//
// Inventory implementation.

#include "inventory.hpp"

#include <algorithm>
#include <cstdio>

namespace resf2::inventory {

// ============================================================
// Item management
// ============================================================

bool Inventory::add_item(const std::string& item_id) {
    if (item_id.empty()) return false;
    // Check if already owned (in items_ or equipped)
    if (has_item(item_id)) return false;
    items_.push_back(item_id);
    std::printf("[inventory] added: %s (total %zu)\n", item_id.c_str(), items_.size());
    return true;
}

bool Inventory::remove_item(const std::string& item_id) {
    if (item_id.empty()) return false;
    bool found = false;
    // Check if equipped — clear the slot
    for (auto& [slot, id] : equipment_) {
        if (id == item_id) {
            id.clear();
            std::printf("[inventory] unequipped %s from %s (for removal)\n",
                        item_id.c_str(), slot.c_str());
            found = true;
            break;
        }
    }
    // Check if in items_ vector
    auto it = std::find(items_.begin(), items_.end(), item_id);
    if (it != items_.end()) {
        items_.erase(it);
        found = true;
    }
    if (found) {
        std::printf("[inventory] removed: %s (remaining %zu)\n", item_id.c_str(), items_.size());
        return true;
    }
    return false;
}

bool Inventory::has_item(const std::string& item_id) const {
    if (item_id.empty()) return false;
    // Check items_
    if (std::find(items_.begin(), items_.end(), item_id) != items_.end()) return true;
    // Check equipment slots
    for (const auto& [slot, id] : equipment_) {
        if (id == item_id) return true;
    }
    return false;
}

void Inventory::clear() {
    items_.clear();
    equipment_.clear();
    std::printf("[inventory] cleared\n");
}

// ============================================================
// Equipment
// ============================================================

std::string Inventory::equipped(const std::string& slot) const {
    auto it = equipment_.find(slot);
    if (it != equipment_.end() && !it->second.empty()) return it->second;
    return {};
}

bool Inventory::equip(const std::string& slot, const std::string& item_id) {
    if (slot.empty()) return false;
    if (item_id.empty()) return false;
    // Item must be owned (in items_)
    auto it = std::find(items_.begin(), items_.end(), item_id);
    if (it == items_.end()) {
        std::printf("[inventory] cannot equip %s in %s: not owned\n",
                    item_id.c_str(), slot.c_str());
        return false;
    }
    // Remove from items_
    items_.erase(it);
    // If slot has an old item, move it back to inventory
    auto old_it = equipment_.find(slot);
    if (old_it != equipment_.end() && !old_it->second.empty()) {
        std::string old_item = old_it->second;
        items_.push_back(old_item);
        std::printf("[inventory] returned %s from %s to inventory\n",
                    old_item.c_str(), slot.c_str());
    }
    // Equip the new item
    equipment_[slot] = item_id;
    std::printf("[inventory] equipped %s in slot %s\n", item_id.c_str(), slot.c_str());
    return true;
}

bool Inventory::unequip(const std::string& slot) {
    auto it = equipment_.find(slot);
    if (it == equipment_.end() || it->second.empty()) return false;
    std::string item_id = it->second;
    it->second.clear();
    items_.push_back(item_id);
    std::printf("[inventory] unequipped %s from %s\n", item_id.c_str(), slot.c_str());
    return true;
}

bool Inventory::is_equipped(const std::string& item_id) const {
    for (const auto& [slot, id] : equipment_) {
        if (id == item_id) return true;
    }
    return false;
}

// ============================================================
// Persistence
// ============================================================

void Inventory::to_save(save::SaveData& data) const {
    // Build the full owned-items list (items_ + equipped items)
    data.owned_items = items_;
    for (const auto& [slot, id] : equipment_) {
        if (!id.empty()) {
            data.owned_items.push_back(id);
        }
    }

    data.equipped_weapon = equipped(kSlotWeapon);
    data.equipped_armor  = equipped(kSlotArmor);
    data.equipped_helmet = equipped(kSlotHelmet);
    data.equipped_ranged = equipped(kSlotRanged);
    data.equipped_magic  = equipped(kSlotMagic);
}

void Inventory::from_save(const save::SaveData& data) {
    clear();

    // Restore equipped items (these are removed from the owned_items list)
    std::vector<std::string> equipped_ids;
    auto restore_equipped = [&](const std::string& slot, const std::string& id) {
        if (!id.empty()) {
            equipment_[slot] = id;
            equipped_ids.push_back(id);
        }
    };
    restore_equipped(kSlotWeapon, data.equipped_weapon);
    restore_equipped(kSlotArmor,  data.equipped_armor);
    restore_equipped(kSlotHelmet, data.equipped_helmet);
    restore_equipped(kSlotRanged, data.equipped_ranged);
    restore_equipped(kSlotMagic,  data.equipped_magic);

    // Restore non-equipped items (everything minus equipped)
    for (const auto& id : data.owned_items) {
        if (std::find(equipped_ids.begin(), equipped_ids.end(), id) == equipped_ids.end()) {
            items_.push_back(id);
        }
    }

    std::printf("[inventory] restored %zu items (%zu owned, %zu equipped)\n",
                data.owned_items.size(), items_.size(), equipment_.size());
}

} // namespace resf2::inventory

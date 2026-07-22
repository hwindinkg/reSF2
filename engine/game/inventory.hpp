// engine/game/inventory.hpp
//
// Inventory and equipment management for reSF2.
// Tracks owned items and their equipment slot assignments.
// When equipping an item, the previously equipped item in that slot
// is automatically returned to the inventory (swap semantics).

#pragma once

#include "save.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace resf2::inventory {

// Equipment slot identifiers.
// These string constants are used as keys in the equipment map.
inline constexpr const char* kSlotWeapon = "weapon";
inline constexpr const char* kSlotArmor  = "armor";
inline constexpr const char* kSlotHelmet = "helmet";
inline constexpr const char* kSlotRanged = "ranged";
inline constexpr const char* kSlotMagic  = "magic";

// All slot names in a convenient array (for iteration).
inline constexpr const char* kAllSlots[] = {
    kSlotWeapon, kSlotArmor, kSlotHelmet, kSlotRanged, kSlotMagic
};
inline constexpr size_t kSlotCount = 5;

// ---------- Inventory class ----------
//
// Manages owned items and equipment slots. Items are stored as string IDs
// (matching the "Name" attribute in list.xml). Equipment slots hold at most
// one item each.

class Inventory {
public:
    Inventory() = default;

    // --- Item management ---

    // Add an item to the inventory. Returns false if already owned.
    // Empty item_id is silently ignored (returns false).
    bool add_item(const std::string& item_id);

    // Remove an item from the inventory. If the item is currently equipped,
    // it is unequipped first. Returns false if the item wasn't owned.
    bool remove_item(const std::string& item_id);

    // Check if an item is in the inventory (including equipped items).
    bool has_item(const std::string& item_id) const;

    // Get all owned item IDs (excluding equipped items).
    const std::vector<std::string>& all_items() const noexcept { return items_; }

    // Remove all items and clear all equipment slots.
    void clear();

    // --- Equipment ---

    // Get the item ID equipped in the given slot, or empty string if none.
    std::string equipped(const std::string& slot) const;

    // Convenience accessors for each slot.
    std::string equipped_weapon() const { return equipped(kSlotWeapon); }
    std::string equipped_armor()  const { return equipped(kSlotArmor); }
    std::string equipped_helmet() const { return equipped(kSlotHelmet); }
    std::string equipped_ranged() const { return equipped(kSlotRanged); }
    std::string equipped_magic()  const { return equipped(kSlotMagic); }

    // Equip an item in a slot. The item must be owned.
    // If the slot already has an item, the old item is returned to inventory.
    // Returns false if the item is not owned.
    bool equip(const std::string& slot, const std::string& item_id);

    // Unequip the item in a slot, moving it back to inventory.
    // Returns true if something was unequipped, false if slot was empty.
    bool unequip(const std::string& slot);

    // Check if an item is currently equipped in any slot.
    bool is_equipped(const std::string& item_id) const;

    // --- Persistence ---

    // Serialize inventory state into a SaveData struct.
    void to_save(save::SaveData& data) const;

    // Deserialize inventory state from a SaveData struct.
    void from_save(const save::SaveData& data);

private:
    std::vector<std::string> items_;                           // owned items (not equipped)
    std::unordered_map<std::string, std::string> equipment_;   // slot -> item_id

    // Internal: move item_id from items_ to a slot.
    // Does NOT check ownership — caller must verify.
    void equip_internal(const std::string& slot, const std::string& item_id);
};

} // namespace resf2::inventory

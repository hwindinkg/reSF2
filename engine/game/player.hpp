// engine/game/player.hpp
//
// Player profile management for reSF2.
// Wraps the player's persistent state (currency, level, wins/losses,
// inventory, equipment) and provides safe mutation helpers.

#pragma once

#include "save.hpp"

#include <string>
#include <vector>

namespace resf2::player {

// ---------- Player profile ----------
//
// Manages the player's mutable game state. All mutations are logged.
// Conversion to/from SaveData enables persistence via SaveManager.

class PlayerProfile {
public:
    PlayerProfile();

    // --- Accessors ---

    int currency() const noexcept { return currency_; }
    int level() const noexcept { return level_; }
    int wins() const noexcept { return wins_; }
    int losses() const noexcept { return losses_; }
    const std::string& current_level() const noexcept { return current_level_; }
    const std::vector<std::string>& completed_levels() const noexcept { return completed_levels_; }
    const std::vector<std::string>& owned_items() const noexcept { return owned_items_; }

    std::string equipped_weapon() const noexcept { return equipped_weapon_; }
    std::string equipped_armor() const noexcept { return equipped_armor_; }
    std::string equipped_helmet() const noexcept { return equipped_helmet_; }
    std::string equipped_ranged() const noexcept { return equipped_ranged_; }
    std::string equipped_magic() const noexcept { return equipped_magic_; }

    // --- Mutators ---

    // Add/subtract currency. Logs the change.
    void add_currency(int amount);
    bool spend_currency(int amount);  // returns false if insufficient

    // Track battle outcomes.
    void add_win();
    void add_loss();

    // Mark a level as completed (no-op if already completed).
    void complete_level(const std::string& level);
    bool is_level_completed(const std::string& level) const;

    // Set the most recently played level.
    void set_current_level(const std::string& level);

    // Add an item to the player's inventory (no-op if already owned).
    void add_item(const std::string& item_id);
    bool has_item(const std::string& item_id) const;

    // Remove an item from the player's inventory.
    // Also unequips if the item is currently equipped.
    // Returns false if the item wasn't owned.
    bool remove_item(const std::string& item_id);

    // Equip an item in a slot. The item must be in owned_items_ to equip it.
    // Returns false if the item is not owned.
    bool equip_item(const std::string& slot, const std::string& item_id);

    // --- Serialization ---

    // Convert to SaveData for persistence.
    save::SaveData to_save_data() const;

    // Restore from SaveData. Returns a profile populated from the data.
    static PlayerProfile from_save_data(const save::SaveData& data);

private:
    int currency_ = 1000;
    int level_ = 1;
    int wins_ = 0;
    int losses_ = 0;
    std::string current_level_;
    std::vector<std::string> completed_levels_;
    std::vector<std::string> owned_items_;

    std::string equipped_weapon_;
    std::string equipped_armor_;
    std::string equipped_helmet_;
    std::string equipped_ranged_;
    std::string equipped_magic_;
};

}  // namespace resf2::player

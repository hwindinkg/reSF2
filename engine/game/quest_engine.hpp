// engine/game/quest_engine.hpp
//
// Minimal quest action processor for reSF2.
//
// [ORIGINAL] The original's quest engine (FUN_101c3db0) processes 57 action
// types from quests.xml. This skeleton handles the progression-critical
// subset: OpenZone (9), SetVariable (7), GiveCurrency (21), GiveItem (19),
// Dialog (1), SetCurrentZone (42), ShowBattle (4), HideBattle (5).
// The remaining 50 types are registered but not yet implemented.
//
// Quest actions are keyed by zone/battle/fight path strings, matching the
// original's "ZONE|Battle|fight" convention (FUN_10138130 -> FUN_101ec2a0).

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace resf2::quest {

// [ORIGINAL] Quest action types from FUN_101c3db0. Only the ones used for
// zone/battle progression are implemented; the rest are stubs.
enum class ActionType : int {
    Dialog         = 1,   // FUN_101c7d20 — show a dialogue
    Fight          = 2,   // FUN_101ce2c0 — trigger a fight
    ActScreen      = 3,   // FUN_101c4ea0 — switch act screen
    ShowBattle     = 4,   // FUN_101d5ab0 — make a battle node visible; parses "Locked" attr
    HideBattle     = 5,   // FUN_101c6ab0 — hide a battle node
    Checkpoint     = 6,   // FUN_101d62d0 — set checkpoint
    SetVariable    = 7,   // FUN_101d0370 — store a named variable
    LevelUpDialog  = 8,   // FUN_101cfe20 — show level-up dialogue
    OpenZone       = 9,   // FUN_101d6560 — unlock a zone on the map
    // 10-11 unused
    OpenShop       = 12,  // FUN_101d2af0 — open shop screen
    // 13-18 unused
    GiveItem       = 19,  // FUN_101cf770 — add item to inventory
    // 20 unused
    GiveCurrency   = 21,  // FUN_101cf640 — add gold/gems
    // 22-27 unused
    SetMapFocus    = 28,  // FUN_101d03f0 — pan map to a location
    // 29-41 unused
    SetCurrentZone = 42,  // FUN_101d1e60 — set player's current zone
    // 43 unused
    BuyItem        = 44,  // FUN_101c5af0 — purchase item
    // 45-56 unused / unknown
    Unknown        = 57,  // sentinel
};

// One quest action parsed from quests.xml.
struct QuestAction {
    int type = 0;                                    // ActionType value
    std::string target;                              // zone/battle/fight path or name
    std::map<std::string, std::string> attributes;   // all XML attributes
    // [Wave 9B] <Dialog> nested <Line Text=..> localization keys, in order.
    // Empty for non-dialog actions and for the legacy single-attribute form.
    std::vector<std::string> dialog_lines;
};

// Callback types for actions that need external side effects.
using DialogCallback   = std::function<void(const std::string& title,
                                            const std::vector<std::pair<std::string, std::string>>& lines)>;
using CurrencyCallback = std::function<void(int amount)>;
using ItemCallback     = std::function<void(const std::string& item_id)>;
using ZoneCallback     = std::function<void(const std::string& zone_name)>;
using BattleCallback   = std::function<void(const std::string& battle_id, bool locked)>;
using VariableCallback = std::function<void(const std::string& name, const std::string& value)>;

// Minimal quest engine — processes quest actions and tracks progression state.
//
// The engine maintains three sets of progression state:
//   open_zones_        — zones the player can access on the map
//   unlocked_battles_  — battle nodes that are visible (ShowBattle)
//   hidden_battles_    — battle nodes that are hidden (HideBattle)
//   variables_         — arbitrary key/value pairs from SetVariable
//
// Zone open/close and battle show/hide map directly to the original's
// DisplayZone visibility logic (0x100a1c00, 0x100c17d0).
class QuestEngine {
public:
    QuestEngine() = default;

    // Register callbacks for side-effect actions.
    void set_dialog_callback(DialogCallback cb)    { on_dialog_ = std::move(cb); }
    void set_currency_callback(CurrencyCallback cb) { on_currency_ = std::move(cb); }
    void set_item_callback(ItemCallback cb)         { on_item_ = std::move(cb); }
    void set_zone_callback(ZoneCallback cb)         { on_zone_ = std::move(cb); }
    void set_battle_callback(BattleCallback cb)     { on_battle_ = std::move(cb); }
    void set_variable_callback(VariableCallback cb)  { on_variable_ = std::move(cb); }

    // Execute a single quest action.
    // [ORIGINAL] Dispatch mirrors FUN_101c3db0's type switch; each type's
    // handler address is noted for cross-reference with the binary.
    inline void execute_action(const QuestAction& action) {
        std::printf("[QUEST] action_type=%d target='%s'\n", action.type, action.target.c_str());
        switch (action.type) {
        case static_cast<int>(ActionType::OpenZone):       // FUN_101d6560
            open_zones_.insert(action.target);
            if (on_zone_) on_zone_(action.target);
            break;
        case static_cast<int>(ActionType::ShowBattle):     // FUN_101d5ab0
            unlocked_battles_.insert(action.target);
            hidden_battles_.erase(action.target);
            if (on_battle_) {
                // [ORIGINAL] The "Locked" attribute controls initial state:
                // Locked="1" means the node appears but greyed out.
                bool locked = action.attributes.count("Locked") > 0 &&
                              action.attributes.at("Locked") == "1";
                on_battle_(action.target, locked);
            }
            break;
        case static_cast<int>(ActionType::HideBattle):     // FUN_101c6ab0
            hidden_battles_.insert(action.target);
            unlocked_battles_.erase(action.target);
            if (on_battle_) on_battle_(action.target, true);
            break;
        case static_cast<int>(ActionType::SetVariable):    // FUN_101d0370
        {
            auto vit = action.attributes.find("Value");
            std::string val = (vit != action.attributes.end()) ? vit->second : "1";
            variables_[action.target] = val;
            if (on_variable_) on_variable_(action.target, val);
            break;
        }
        case static_cast<int>(ActionType::GiveCurrency):   // FUN_101cf640
        {
            auto ait = action.attributes.find("Amount");
            int amount = (ait != action.attributes.end()) ? std::atoi(ait->second.c_str()) : 0;
            if (on_currency_) on_currency_(amount);
            break;
        }
        case static_cast<int>(ActionType::GiveItem):       // FUN_101cf770
            if (on_item_) on_item_(action.target);
            break;
        case static_cast<int>(ActionType::SetCurrentZone): // FUN_101d1e60
            if (on_zone_) on_zone_(action.target);
            break;
        case static_cast<int>(ActionType::Dialog):         // FUN_101c7d20
        {
            // [ORIGINAL] Dialog actions carry Title (speaker key) and Line
            // entries; the quest XML loader (quest_loader.cpp) fills
            // dialog_lines from the nested <Line Text=..> keys. The Dialogue
            // scene localizes each key on render.
            if (on_dialog_) {
                std::vector<std::pair<std::string, std::string>> lines;
                if (!action.dialog_lines.empty()) {
                    for (const auto& key : action.dialog_lines)
                        lines.emplace_back(action.target, key);
                } else {
                    auto lit = action.attributes.find("Line");
                    if (lit != action.attributes.end())
                        lines.emplace_back(action.target, lit->second);
                }
                on_dialog_(action.target, lines);
            }
            break;
        }
        default:
            // Types not yet implemented: Fight(2), ActScreen(3), Checkpoint(6),
            // LevelUpDialog(8), OpenShop(12), SetMapFocus(28), BuyItem(44), etc.
            std::printf("[QUEST] unhandled action type %d target='%s'\n",
                        action.type, action.target.c_str());
            break;
        }
    }

    // Execute a batch of actions (e.g. all actions for completing a fight).
    void execute_actions(const std::vector<QuestAction>& actions) {
        for (const auto& a : actions) execute_action(a);
    }

    // --- Progression queries ---

    // Is a zone currently open (accessible on the map)?
    [[nodiscard]] bool is_zone_open(const std::string& zone_name) const {
        return open_zones_.count(zone_name) > 0;
    }

    // Is a battle node unlocked (visible)?
    [[nodiscard]] bool is_battle_unlocked(const std::string& battle_id) const {
        return unlocked_battles_.count(battle_id) > 0;
    }

    // Is a battle node explicitly hidden?
    [[nodiscard]] bool is_battle_hidden(const std::string& battle_id) const {
        return hidden_battles_.count(battle_id) > 0;
    }

    // Get a quest variable value (empty string if not set).
    [[nodiscard]] std::string get_variable(const std::string& name) const {
        auto it = variables_.find(name);
        return it != variables_.end() ? it->second : std::string{};
    }

    // Manually open a zone (e.g. for initial state or cheat).
    void open_zone(const std::string& zone_name) {
        open_zones_.insert(zone_name);
        if (on_zone_) on_zone_(zone_name);
    }

    // Manually unlock a battle node.
    void unlock_battle(const std::string& battle_id) {
        unlocked_battles_.insert(battle_id);
        hidden_battles_.erase(battle_id);
        if (on_battle_) on_battle_(battle_id, false);
    }

    // Set a variable directly.
    void set_variable(const std::string& name, const std::string& value) {
        variables_[name] = value;
        if (on_variable_) on_variable_(name, value);
    }

    // Debug: dump current state.
    void debug_dump() const {
        std::printf("[QUEST] open_zones=%zu unlocked_battles=%zu hidden_battles=%zu variables=%zu\n",
                    open_zones_.size(), unlocked_battles_.size(),
                    hidden_battles_.size(), variables_.size());
        for (const auto& z : open_zones_)
            std::printf("[QUEST]   zone open: '%s'\n", z.c_str());
        for (const auto& v : variables_)
            std::printf("[QUEST]   var '%s' = '%s'\n", v.first.c_str(), v.second.c_str());
    }

private:
    std::set<std::string> open_zones_;
    std::set<std::string> unlocked_battles_;
    std::set<std::string> hidden_battles_;
    std::map<std::string, std::string> variables_;

    DialogCallback   on_dialog_;
    CurrencyCallback on_currency_;
    ItemCallback     on_item_;
    ZoneCallback     on_zone_;
    BattleCallback   on_battle_;
    VariableCallback on_variable_;
};

}  // namespace resf2::quest

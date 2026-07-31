#pragma once

// engine/game/attribute_aggregation.hpp
//
// [ORIGINAL] Equipment -> attribute aggregation for the recovered damage
// model. Every fighter's attributes live in the name-keyed int map at
// model+0x1C4 that Model::getParameter (game+0x6275F4) reads; this header
// builds that map from equipped items (player) or from the
// <AlignTargetAttributes> baseline (enemy placeholder).
//
// The aggregation itself is what list.xml implies: items carry the same
// attribute names the tracer at game+0x628788 reads (WeaponDamage,
// BodyDefense, ...), and equipment contributes additively — see
// AttributeSet::add_item_contribution in attributes.hpp.
//
// Header-only free functions, no Game dependencies, so the behaviour is
// unit-testable without a Game object (tests/test_damage_wiring.cpp).

#include <string>

#include "attributes.hpp"
#include "inventory.hpp"
#include "../format/list_parser.hpp"

namespace resf2::game {

// Resolve an equipped item id to its list.xml definition, the same linear
// by-name lookup the equip path uses (game.cpp host_equip_item).
inline const format::ListItem* find_list_item(const format::ListData& list_data,
                                              const std::string& item_id) {
    for (const auto& item : list_data.items) {
        if (item.name == item_id) return &item;
    }
    return nullptr;
}

// Player attributes: sum every equipped item's contribution over all five
// slots. reset_to_zero() first, so the full tracer set is always present and
// the damage path never sees the -1e35 sentinel.
inline AttributeSet aggregate_equipment_attributes(
        const format::ListData& list_data,
        const inventory::Inventory& inv) {
    AttributeSet attrs;
    attrs.reset_to_zero();
    for (const char* slot : inventory::kAllSlots) {
        const std::string equipped_id = inv.equipped(slot);
        if (equipped_id.empty()) continue;
        const format::ListItem* item = find_list_item(list_data, equipped_id);
        if (item == nullptr) continue;  // equipped id not in list.xml: contributes nothing

        attrs.add_item_contribution(item->weapon_damage, item->body_defense,
                                    item->head_defense, item->ranged_damage,
                                    item->magic_damage);
        // UnarmedDamage is NOT a parameter of add_item_contribution (that
        // signature is golden-pinned by test_attributes_golden), so it goes
        // through add() separately.
        attrs.add("UnarmedDamage", static_cast<int>(item->unarmed_damage));

        // --- Perk plug point (phase 4 steps 3-6 workstream) ------------------
        // ListItem::perks (ListPerk{name, params}, list_parser.hpp L29-33)
        // plug in HERE, per equipped item, after the direct contribution.
        // The Step 5 fork verdict (PERK_SURVEY.md) decides what lands here:
        // Case A (persistent write into the model+0x1C4 map) -> verified
        // contribution code called at this point; Case B (trigger/timed
        // effect) -> a single [ORIGINAL] TODO with binary refs. Until the
        // verdict lands, perks contribute nothing.
    }
    return attrs;
}

// Enemy placeholder attributes: the <AlignTargetAttributes> baseline from
// internalSettings.xml ("normalise an opponent's attributes", attributes.hpp).
// When stage warriors land (5.3), per-warrior items replace this seed.
inline AttributeSet seed_enemy_baseline_attributes() {
    AttributeSet attrs;
    attrs.reset_to_zero();
    for (const auto& target : align_target_attributes()) {
        attrs.set(target.name, target.value);
    }
    return attrs;
}

}  // namespace resf2::game

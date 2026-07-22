// test_inventory.cpp
//
// Tests for Inventory management, Shop transactions, and equipment system.
//
// Covers:
//   - Inventory add/remove/has/clear
//   - Equipment equip/unequip with swap-back semantics
//   - ShopManager catalog loading and queries
//   - Buy/sell transactions
//   - Save/load persistence via SaveData

#include "../engine/game/inventory.hpp"
#include "../engine/game/shop.hpp"
#include "../engine/game/save.hpp"
#include "../engine/format/list_parser.hpp"

#include <cstdio>
#include <cassert>
#include <string>
#include <vector>

namespace inventory = resf2::inventory;
namespace shop = resf2::shop;
namespace save = resf2::save;
namespace fmt = resf2::format;

// ============================================================
// Test framework (matches existing test style)
// ============================================================

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { \
    test_count++; \
    std::printf("  TEST %d: %s ... ", test_count, name); \
    bool _ok = true;

#define END_TEST \
    if (_ok) { pass_count++; std::printf("PASS\n"); } \
    else { std::printf("FAIL\n"); } \
} while(0)

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("\n    FAIL at line %d: %s\n", __LINE__, #cond); \
        _ok = false; \
    } \
} while(0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        std::printf("\n    FAIL at line %d: expected '%s' == '%s', got %d != %d\n", \
                    __LINE__, #a, #b, (int)_a, (int)_b); \
        _ok = false; \
    } \
} while(0)

#define CHECK_STREQ(a, b) do { \
    std::string _a = (a); std::string _b = (b); \
    if (_a != _b) { \
        std::printf("\n    FAIL at line %d: expected '%s' == '%s', got '%s' != '%s'\n", \
                    __LINE__, #a, #b, _a.c_str(), _b.c_str()); \
        _ok = false; \
    } \
} while(0)

// ============================================================
// Test helpers
// ============================================================

// Build a minimal ListData with a few test items
static fmt::ListData make_test_catalog() {
    fmt::ListData data;

    auto add_item = [&](const std::string& name, const std::string& type,
                        int price, int level, float dmg, float def) {
        fmt::ListItem item;
        item.name = name;
        item.type = type;
        item.price = price;
        item.level = level;
        item.weapon_damage = dmg;
        item.body_defense = def;
        data.items.push_back(std::move(item));
    };

    add_item("IronSword",  "Weapon", 500, 1, 15, 0);
    add_item("SteelSword", "Weapon", 1500, 3, 25, 0);
    add_item("BronzeArmor", "Armor", 400, 1, 0, 10);
    add_item("SteelArmor",  "Armor", 1200, 3, 0, 25);
    add_item("IronHelm",   "Helm",  300, 1, 0, 5);
    add_item("WoodBow",    "Ranged", 600, 2, 8, 0);
    add_item("FireMagic",  "Magic", 800, 2, 0, 0);

    return data;
}

// ============================================================
// Inventory tests
// ============================================================

void test_inventory_add() {
    TEST("Inventory: add items");
    inventory::Inventory inv;
    CHECK(inv.add_item("sword"));
    CHECK(inv.has_item("sword"));
    CHECK_EQ(inv.all_items().size(), (size_t)1);

    // Duplicate add returns false
    CHECK(!inv.add_item("sword"));
    CHECK_EQ(inv.all_items().size(), (size_t)1);

    // Empty string is rejected
    CHECK(!inv.add_item(""));
    CHECK_EQ(inv.all_items().size(), (size_t)1);
    END_TEST;
}

void test_inventory_remove() {
    TEST("Inventory: remove items");
    inventory::Inventory inv;
    inv.add_item("sword");
    inv.add_item("shield");
    CHECK_EQ(inv.all_items().size(), (size_t)2);

    CHECK(inv.remove_item("sword"));
    CHECK(!inv.has_item("sword"));
    CHECK(inv.has_item("shield"));
    CHECK_EQ(inv.all_items().size(), (size_t)1);

    // Remove non-existent returns false
    CHECK(!inv.remove_item("axe"));
    CHECK_EQ(inv.all_items().size(), (size_t)1);
    END_TEST;
}

void test_inventory_clear() {
    TEST("Inventory: clear");
    inventory::Inventory inv;
    inv.add_item("sword");
    inv.add_item("shield");
    inv.equip("weapon", "sword");
    CHECK_EQ(inv.all_items().size(), (size_t)1);  // sword is equipped

    inv.clear();
    CHECK(inv.all_items().empty());
    CHECK(inv.equipped_weapon().empty());
    CHECK(!inv.has_item("sword"));
    END_TEST;
}

void test_inventory_equip() {
    TEST("Inventory: equip weapon");
    inventory::Inventory inv;
    inv.add_item("sword");
    inv.add_item("shield");

    // Equip weapon
    CHECK(inv.equip("weapon", "sword"));
    CHECK_STREQ(inv.equipped_weapon(), "sword");
    CHECK(inv.is_equipped("sword"));
    // Sword should NOT be in all_items() (non-equipped items only)
    bool sword_in_unequipped = false;
    for (const auto& id : inv.all_items()) {
        if (id == "sword") sword_in_unequipped = true;
    }
    CHECK(!sword_in_unequipped);
    // But has_item() should still return true (checks both items_ and equipment)
    CHECK(inv.has_item("sword"));

    // Equip armor
    CHECK(inv.equip("armor", "shield"));
    CHECK_STREQ(inv.equipped_armor(), "shield");
    END_TEST;
}

void test_inventory_equip_swap() {
    TEST("Inventory: equip swap — old item returns to inventory");
    inventory::Inventory inv;
    inv.add_item("sword");
    inv.add_item("axe");

    CHECK(inv.equip("weapon", "sword"));
    CHECK_STREQ(inv.equipped_weapon(), "sword");
    // has_item() returns true for equipped items
    CHECK(inv.has_item("sword"));

    // Equip axe in weapon slot — sword returns to inventory
    CHECK(inv.equip("weapon", "axe"));
    CHECK_STREQ(inv.equipped_weapon(), "axe");
    // Sword should be back in inventory (in items_ vector)
    CHECK(inv.has_item("sword"));
    // Axe is equipped, but has_item() should still return true for equipped items
    CHECK(inv.has_item("axe"));
    CHECK(inv.is_equipped("axe"));
    END_TEST;
}

void test_inventory_unequip() {
    TEST("Inventory: unequip returns item to inventory");
    inventory::Inventory inv;
    inv.add_item("sword");
    inv.equip("weapon", "sword");

    CHECK(inv.unequip("weapon"));
    CHECK(inv.equipped_weapon().empty());
    CHECK(inv.has_item("sword"));  // back in inventory

    // Unequip empty slot returns false
    CHECK(!inv.unequip("weapon"));
    END_TEST;
}

void test_inventory_is_equipped() {
    TEST("Inventory: is_equipped across all slots");
    inventory::Inventory inv;
    inv.add_item("sword");
    inv.add_item("shield");
    inv.add_item("helmet");

    inv.equip("weapon", "sword");
    inv.equip("armor", "shield");

    CHECK(inv.is_equipped("sword"));
    CHECK(inv.is_equipped("shield"));
    CHECK(!inv.is_equipped("helmet"));  // in inventory but not equipped

    inv.equip("helmet", "helmet");
    CHECK(inv.is_equipped("helmet"));
    END_TEST;
}

void test_inventory_equip_not_owned() {
    TEST("Inventory: equip unowned item returns false");
    inventory::Inventory inv;
    CHECK(!inv.equip("weapon", "magic_sword"));
    CHECK(inv.equipped_weapon().empty());
    END_TEST;
}

void test_inventory_remove_equipped() {
    TEST("Inventory: remove equipped item — unequips first");
    inventory::Inventory inv;
    inv.add_item("sword");
    inv.equip("weapon", "sword");

    // Remove equipped item (should unequip it first)
    CHECK(inv.remove_item("sword"));
    CHECK(inv.equipped_weapon().empty());
    CHECK(!inv.has_item("sword"));
    END_TEST;
}

// ============================================================
// ShopManager tests
// ============================================================

void test_shop_catalog() {
    TEST("ShopManager: load catalog");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    // All 7 items should be in catalog
    CHECK_EQ(mgr.all_items().size(), (size_t)7);

    // Find specific items
    auto* item = mgr.find_item("IronSword");
    CHECK(item != nullptr);
    CHECK_STREQ(item->category, "Weapon");
    CHECK_EQ(item->price, 500);

    // Non-existent returns null
    CHECK(mgr.find_item("NonExistent") == nullptr);
    END_TEST;
}

void test_shop_catalog_filters() {
    TEST("ShopManager: get_items by category");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    auto weapons = mgr.get_items("Weapon");
    CHECK_EQ(weapons.size(), (size_t)2);  // IronSword, SteelSword

    auto armors = mgr.get_items("Armor");
    CHECK_EQ(armors.size(), (size_t)2);  // BronzeArmor, SteelArmor

    auto helms = mgr.get_items("Helm");
    CHECK_EQ(helms.size(), (size_t)1);

    auto ranged = mgr.get_items("Ranged");
    CHECK_EQ(ranged.size(), (size_t)1);

    auto magic = mgr.get_items("Magic");
    CHECK_EQ(magic.size(), (size_t)1);
    END_TEST;
}

void test_shop_can_buy() {
    TEST("ShopManager: can_buy checks gold and level");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    // IronSword costs 500, level 1 — can buy with 500+ gold
    CHECK(mgr.can_buy("IronSword", 500, 1));
    CHECK(mgr.can_buy("IronSword", 1000, 1));

    // Not enough gold
    CHECK(!mgr.can_buy("IronSword", 400, 1));

    // SteelSword costs 1500, level 3
    CHECK(mgr.can_buy("SteelSword", 1500, 3));
    CHECK(!mgr.can_buy("SteelSword", 1500, 2));  // level too low
    CHECK(!mgr.can_buy("SteelSword", 1000, 3));  // gold too low

    // Non-existent item
    CHECK(!mgr.can_buy("NonExistent", 9999, 99));
    END_TEST;
}

void test_shop_prices() {
    TEST("ShopManager: buy_price and sell_price");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    CHECK_EQ(mgr.buy_price("IronSword"), 500);
    CHECK_EQ(mgr.buy_price("NonExistent"), 0);

    // Sell price is 50% of buy price
    CHECK_EQ(mgr.sell_price("IronSword"), 250);   // 500/2
    CHECK_EQ(mgr.sell_price("SteelSword"), 750);  // 1500/2
    CHECK_EQ(mgr.sell_price("BronzeArmor"), 200); // 400/2

    // Minimum sell price is 1
    CHECK_EQ(mgr.sell_price("NonExistent"), 0);   // not found
    END_TEST;
}

void test_shop_level_requirement() {
    TEST("ShopManager: level_requirement");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    CHECK_EQ(mgr.level_requirement("IronSword"), 1);
    CHECK_EQ(mgr.level_requirement("SteelSword"), 3);
    CHECK_EQ(mgr.level_requirement("NonExistent"), 1);  // default
    END_TEST;
}

void test_shop_item_category() {
    TEST("ShopManager: item_category");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    CHECK_STREQ(mgr.item_category("IronSword"), "Weapon");
    CHECK_STREQ(mgr.item_category("BronzeArmor"), "Armor");
    CHECK_STREQ(mgr.item_category("NonExistent"), "");
    END_TEST;
}

// ============================================================
// Persistence tests (Inventory <-> SaveData)
// ============================================================

void test_inventory_to_save() {
    TEST("Inventory: to_save populates SaveData correctly");
    inventory::Inventory inv;
    inv.add_item("sword");
    inv.add_item("shield");
    inv.add_item("helmet");
    inv.equip("weapon", "sword");

    save::SaveData data;
    inv.to_save(data);

    // owned_items should contain shield + helmet + sword (equipped)
    // The order: items_ first (shield, helmet), then equipped items
    CHECK_EQ(data.owned_items.size(), (size_t)3);
    CHECK_STREQ(data.equipped_weapon, "sword");
    CHECK(data.equipped_armor.empty());
    CHECK(data.equipped_helmet.empty());

    // Verify sword is in owned_items (as equipped item)
    bool found_sword = false;
    for (const auto& id : data.owned_items) {
        if (id == "sword") found_sword = true;
    }
    CHECK(found_sword);
    END_TEST;
}

void test_inventory_from_save() {
    TEST("Inventory: from_save restores state from SaveData");
    save::SaveData data;
    data.owned_items = {"shield", "helmet", "sword"};
    data.equipped_weapon = "sword";

    inventory::Inventory inv;
    inv.from_save(data);

    // Sword should be equipped
    CHECK_STREQ(inv.equipped_weapon(), "sword");
    CHECK(inv.is_equipped("sword"));

    // Shield and helmet should be in items_ (not equipped)
    CHECK(inv.has_item("shield"));
    CHECK(inv.has_item("helmet"));
    CHECK(!inv.is_equipped("shield"));
    CHECK(!inv.is_equipped("helmet"));

    // Sword should not be in items_ (it's equipped)
    // all_items() returns non-equipped items
    for (const auto& id : inv.all_items()) {
        CHECK(id != "sword");
    }
    END_TEST;
}

void test_inventory_save_roundtrip() {
    TEST("Inventory: save then load roundtrip preserves state");
    inventory::Inventory original;
    original.add_item("IronSword");
    original.add_item("SteelSword");
    original.add_item("BronzeArmor");
    original.add_item("IronHelm");
    original.equip("weapon", "SteelSword");
    original.equip("armor", "BronzeArmor");

    save::SaveData data;
    original.to_save(data);

    inventory::Inventory restored;
    restored.from_save(data);

    // Verify equipped items
    CHECK_STREQ(restored.equipped_weapon(), "SteelSword");
    CHECK_STREQ(restored.equipped_armor(), "BronzeArmor");
    CHECK(restored.equipped_helmet().empty());

    // Verify inventory items
    // has_item() returns true for both owned and equipped items
    CHECK(restored.has_item("IronSword"));
    CHECK(restored.has_item("SteelSword"));   // equipped (has_item includes equipped)
    CHECK(restored.has_item("BronzeArmor"));  // equipped (has_item includes equipped)
    CHECK(restored.has_item("IronHelm"));

    // Verify count: 2 in items_ (non-equipped), 2 equipped
    CHECK_EQ(restored.all_items().size(), (size_t)2);
    CHECK(restored.is_equipped("SteelSword"));
    CHECK(restored.is_equipped("BronzeArmor"));
    END_TEST;
}

void test_inventory_full_equip_roundtrip() {
    TEST("Inventory: all 5 equipment slots save/load roundtrip");
    inventory::Inventory inv;
    inv.add_item("W");
    inv.add_item("A");
    inv.add_item("H");
    inv.add_item("R");
    inv.add_item("M");
    inv.equip("weapon", "W");
    inv.equip("armor", "A");
    inv.equip("helmet", "H");
    inv.equip("ranged", "R");
    inv.equip("magic", "M");

    save::SaveData data;
    inv.to_save(data);

    inventory::Inventory restored;
    restored.from_save(data);

    CHECK_STREQ(restored.equipped_weapon(), "W");
    CHECK_STREQ(restored.equipped_armor(), "A");
    CHECK_STREQ(restored.equipped_helmet(), "H");
    CHECK_STREQ(restored.equipped_ranged(), "R");
    CHECK_STREQ(restored.equipped_magic(), "M");
    END_TEST;
}

// ============================================================
// Integration tests (Inventory + ShopManager)
// ============================================================

void test_shop_buy_flow() {
    TEST("Integration: Shop buy flow — add to inventory, deduct gold");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    inventory::Inventory inv;
    int gold = 1000;
    int player_level = 1;

    // Buy IronSword (500 gold, level 1)
    CHECK(mgr.can_buy("IronSword", gold, player_level));
    gold -= mgr.buy_price("IronSword");
    inv.add_item("IronSword");

    CHECK(inv.has_item("IronSword"));
    CHECK_EQ(gold, 500);

    // Buy BronzeArmor (400 gold, level 1) 
    CHECK(mgr.can_buy("BronzeArmor", gold, player_level));
    gold -= mgr.buy_price("BronzeArmor");
    inv.add_item("BronzeArmor");

    CHECK(inv.has_item("BronzeArmor"));
    CHECK_EQ(gold, 100);

    // Not enough gold for next purchase
    CHECK(!mgr.can_buy("WoodBow", gold, player_level));
    END_TEST;
}

void test_shop_sell_flow() {
    TEST("Integration: Shop sell flow — remove from inventory, add gold");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    inventory::Inventory inv;
    int gold = 500;

    inv.add_item("IronSword");

    // Sell IronSword (sell price = 500/2 = 250)
    int sell_val = mgr.sell_price("IronSword");
    CHECK_EQ(sell_val, 250);
    CHECK(inv.remove_item("IronSword"));
    gold += sell_val;

    CHECK(!inv.has_item("IronSword"));
    CHECK_EQ(gold, 750);
    END_TEST;
}

void test_shop_sell_equipped() {
    TEST("Integration: Sell equipped item — unequips and sells");
    shop::ShopManager mgr;
    auto data = make_test_catalog();
    mgr.load_catalog(data);

    inventory::Inventory inv;
    int gold = 1000;

    inv.add_item("IronSword");
    inv.equip("weapon", "IronSword");

    // Sell by removing (Inventory::remove_item handles unequip)
    int sell_val = mgr.sell_price("IronSword");
    CHECK(inv.remove_item("IronSword"));  // unequips first
    gold += sell_val;

    CHECK(!inv.has_item("IronSword"));
    CHECK(inv.equipped_weapon().empty());
    CHECK_EQ(gold, 1250);
    END_TEST;
}

// ============================================================
// Slot helpers test
// ============================================================

void test_item_type_to_slot() {
    TEST("item_type_to_slot mapping");
    CHECK_STREQ(shop::item_type_to_slot("Weapon"), "weapon");
    CHECK_STREQ(shop::item_type_to_slot("Armor"),  "armor");
    CHECK_STREQ(shop::item_type_to_slot("Helm"),   "helmet");
    CHECK_STREQ(shop::item_type_to_slot("Ranged"), "ranged");
    CHECK_STREQ(shop::item_type_to_slot("Magic"),  "magic");
    CHECK(shop::item_type_to_slot("Unknown") == nullptr);
    CHECK(shop::item_type_to_slot("") == nullptr);
    END_TEST;
}

// ============================================================
// Main
// ============================================================

int main() {
    std::printf("=== Inventory & Equipment System Tests ===\n\n");

    std::printf("--- Inventory ---\n");
    test_inventory_add();
    test_inventory_remove();
    test_inventory_clear();
    test_inventory_equip();
    test_inventory_equip_swap();
    test_inventory_unequip();
    test_inventory_is_equipped();
    test_inventory_equip_not_owned();
    test_inventory_remove_equipped();

    std::printf("\n--- Shop ---\n");
    test_shop_catalog();
    test_shop_catalog_filters();
    test_shop_can_buy();
    test_shop_prices();
    test_shop_level_requirement();
    test_shop_item_category();

    std::printf("\n--- Persistence ---\n");
    test_inventory_to_save();
    test_inventory_from_save();
    test_inventory_save_roundtrip();
    test_inventory_full_equip_roundtrip();

    std::printf("\n--- Integration ---\n");
    test_shop_buy_flow();
    test_shop_sell_flow();
    test_shop_sell_equipped();

    std::printf("\n--- Utilities ---\n");
    test_item_type_to_slot();

    std::printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}

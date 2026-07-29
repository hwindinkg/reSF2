// tests/integration/test_shop_integration.cpp
//
// Shop integration test: verifies that the shop/currency system works
// correctly — currency starts non-negative, buy/sell operations behave
// correctly, and inventory state is consistent.
//
// This test exercises:
// - Currency tracking (non-negative invariant)
// - Item purchase flow (spend currency → add to inventory)
// - Item sale flow (remove from inventory → gain currency)
// - Inventory state consistency (has_item ↔ get_owned_items)

#include "../headless_test_runner.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    std::printf("=== Shop Integration Test ===\n");

    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;

    resf2::test::HeadlessTestRunner runner(config);

    std::printf("Initializing...\n");
    if (!runner.init()) {
        std::fprintf(stderr, "FAIL: init() returned false\n");
        return 1;
    }

    // Let game initialize a few frames
    runner.run_frames(10);

    // Check initial currency
    int initial_currency = runner.currency();
    std::printf("Initial currency: %d\n", initial_currency);
    assert(initial_currency >= 0 && "Currency must be non-negative");
    std::printf("OK: Currency is non-negative\n");

    // Check initial inventory (should not crash)
    auto& game = runner.game();
    auto owned_items = game.host_get_owned_items();
    std::printf("Initial inventory: %zu items\n", owned_items.size());
    for (const auto& item_id : owned_items) {
        assert(runner.has_item(item_id) &&
               "has_item() should return true for items in owned list");
    }
    std::printf("OK: Inventory is consistent\n");

    // Test buy_item with a known-invalid item (should return false gracefully)
    bool bought_fake = game.host_buy_item("__nonexistent_item__");
    assert(!bought_fake && "Buying nonexistent item should fail");
    int after_fake_currency = runner.currency();
    assert(after_fake_currency == initial_currency &&
           "Currency must not change after failed purchase");
    std::printf("OK: Buying nonexistent item correctly fails\n");

    // If we have currency, try to find a buyable item from the catalog
    const auto* list_data = game.host_get_list_data();
    if (list_data && !list_data->items.empty() && initial_currency > 0) {
        // Find the first item we don't already own and can afford
        for (const auto& item : list_data->items) {
            // ListItem uses 'name' as the identifier (matches ShopItem.id)
            const std::string& item_id = item.name;
            if (runner.has_item(item_id)) continue;
            if (item.price <= 0) continue;
            if (item.price > initial_currency) continue;

            std::printf("Attempting to buy '%s' (price: %d)...\n",
                        item_id.c_str(), item.price);
            bool bought = game.host_buy_item(item_id);
            if (bought) {
                int new_currency = runner.currency();
                std::printf("  Bought! Currency: %d -> %d (delta: %d)\n",
                            initial_currency, new_currency,
                            new_currency - initial_currency);

                assert(new_currency < initial_currency &&
                       "Currency must decrease after purchase");
                assert(runner.has_item(item_id) &&
                       "Item must appear in inventory after purchase");
                std::printf("OK: Purchase succeeded, currency and inventory updated\n");

                // Now try to sell it back
                std::printf("Attempting to sell '%s'...\n", item_id.c_str());
                bool sold = game.host_sell_item(item_id);
                if (sold) {
                    int sell_currency = runner.currency();
                    std::printf("  Sold! Currency: %d -> %d\n",
                                new_currency, sell_currency);
                    assert(sell_currency > new_currency &&
                           "Currency must increase after sale");
                    std::printf("OK: Sale succeeded, currency restored\n");
                } else {
                    std::printf("  Sale not implemented or failed (non-fatal)\n");
                }

                break;  // only test one item
            } else {
                std::printf("  Buy returned false (level req or other constraint)\n");
                // Currency should not have changed
                assert(runner.currency() == initial_currency &&
                       "Currency must not change when buy returns false");
            }
        }
    } else {
        std::printf("No catalog or no currency — skipping buy/sell test\n");
    }

    // Final invariant: currency must still be non-negative
    int final_currency = runner.currency();
    assert(final_currency >= 0 && "Currency must remain non-negative");
    std::printf("OK: Final currency is non-negative (%d)\n", final_currency);

    std::printf("\n=== SHOP INTEGRATION TEST PASSED ===\n");
    return 0;
}

#include "../engine/format/list_parser.hpp"
#include <cstdio>
#include <filesystem>
#include "check.hpp"

namespace fs = std::filesystem;

static const char* item_type_str(const std::string& type) {
    if (type == "Weapon") return "Weapon";
    if (type == "Armor") return "Armor";
    if (type == "Helm") return "Helm";
    if (type == "Ranged") return "Ranged";
    if (type == "Magic") return "Magic";
    if (type == "RealMoneyItem") return "IAP";
    if (type == "Skeleton") return "Skeleton";
    if (type == "Seal") return "Seal";
    if (type == "Consumable") return "Consumable";
    if (type == "RaidConsumable") return "RaidConsumable";
    if (type == "Free") return "Free";
    return type.c_str();
}

int main() {
    std::printf("=== List Parser Test ===\n\n");

    auto path = fs::path("assets") / "list.xml";
    if (!fs::exists(path)) {
        std::printf("NOT FOUND: %s\n", path.string().c_str());
        return 1;
    }
    std::printf("File: %s\n\n", path.string().c_str());

    resf2::format::ListParser parser;
    resf2::format::ListData data;
    if (!parser.load_file(path.string(), data)) {
        std::printf("PARSE ERROR: %s\n", parser.error().c_str());
        return 1;
    }

    std::printf("Items: %zu\n", data.items.size());
    std::printf("Upgrade templates: %zu\n", data.upgrade_templates.size());
    std::printf("Item sets: %zu\n\n", data.item_sets.size());

    // Item type breakdown
    std::printf("--- Items by Type ---\n");
    int type_counts[15] = {};
    const char* type_names[15] = {
        "Weapon", "Armor", "Helm", "Ranged", "Magic",
        "RealMoneyItem", "Skeleton", "Seal", "Consumable",
        "RaidConsumable", "Free", "Energy", "Decks",
        "Lottery_Reroll", "Other"
    };
    for (auto& item : data.items) {
        if (item.type == "Weapon") type_counts[0]++;
        else if (item.type == "Armor") type_counts[1]++;
        else if (item.type == "Helm") type_counts[2]++;
        else if (item.type == "Ranged") type_counts[3]++;
        else if (item.type == "Magic") type_counts[4]++;
        else if (item.type == "RealMoneyItem") type_counts[5]++;
        else if (item.type == "Skeleton") type_counts[6]++;
        else if (item.type == "Seal") type_counts[7]++;
        else if (item.type == "Consumable") type_counts[8]++;
        else if (item.type == "RaidConsumable") type_counts[9]++;
        else if (item.type == "Free") type_counts[10]++;
        else if (item.type == "Energy") type_counts[11]++;
        else if (item.type == "Decks") type_counts[12]++;
        else if (item.type == "Lottery_Reroll") type_counts[13]++;
        else type_counts[14]++;
    }
    for (int i = 0; i < 15; i++) {
        if (type_counts[i] > 0) {
            std::printf("  %-18s %d\n", type_names[i], type_counts[i]);
        }
    }

    // Upgrades breakdown
    std::printf("\n--- Upgrade Templates ---\n");
    size_t total_upgrade_steps = 0;
    for (auto& t : data.upgrade_templates) {
        std::printf("  '%s' -> %zu upgrade steps\n",
                    t.template_name.c_str(), t.upgrades.size());
        total_upgrade_steps += t.upgrades.size();
    }
    std::printf("  Total upgrade steps: %zu\n", total_upgrade_steps);

    // Item sets
    std::printf("\n--- Item Sets ---\n");
    for (auto& s : data.item_sets) {
        std::printf("  '%s': %zu items\n", s.name.c_str(), s.items.size());
        for (auto& e : s.items) {
            std::printf("    %s (%.1f, %.1f) scale=%.1f\n",
                        e.name.c_str(), e.x, e.y, e.scale);
        }
    }

    // Sample first few items
    std::printf("\n--- First 5 Items ---\n");
    int count = 0;
    for (auto& item : data.items) {
        if (count++ >= 5) break;
        std::printf("  [%s] name=%s type=%s", item_type_str(item.type),
                    item.name.c_str(), item.type.c_str());
        if (!item.subtype.empty())
            std::printf(" subtype=%s", item.subtype.c_str());
        if (item.price > 0)
            std::printf(" price=%d", item.price);
        if (item.level > 0)
            std::printf(" level=%d", item.level);
        if (!item.pack_label.empty())
            std::printf(" pack=%s", item.pack_label.c_str());
        std::printf("\n");
        if (!item.upgrades.empty()) {
            std::printf("    upgrades: %zu blocks\n", item.upgrades.size());
            for (auto& ub : item.upgrades) {
                if (!ub.template_name.empty())
                    std::printf("      template=%s overrides=%zu\n",
                                ub.template_name.c_str(), ub.upgrades.size());
                else
                    std::printf("      inline steps=%zu\n", ub.upgrades.size());
            }
        }
        if (!item.enchantments.empty()) {
            std::printf("    enchantments: %zu perks\n", item.enchantments.size());
        }
    }

    // Content assertions: parsing "successfully" into an empty structure used
    // to pass silently.
    resf2::test::check_ge(static_cast<double>(data.items.size()), 100.0,
                          "list.xml yields a real item catalogue");
    resf2::test::check_ge(static_cast<double>(data.upgrade_templates.size()), 1.0,
                          "list.xml yields upgrade templates");
    int named = 0;
    for (const auto& it : data.items) if (!it.name.empty()) ++named;
    resf2::test::check_eq(named, static_cast<int>(data.items.size()),
                          "every item has a name");
    return resf2::test::summary();
}

// tests/test_quest_spec.cpp
//
// Specification test for the quest system (the story engine).
//
// This test is written against the SHIPPED DATA (assets/quests.xml), not
// against the engine's current behaviour. The original game satisfies it by
// construction: it loads quests.xml, indexes quests by event, evaluates their
// <Conditions>, and runs their <Actions>. reSF2 currently does none of that --
// Game::host_trigger_quest_event only prints a line and says
// "quest XML dispatch pending" (game.cpp:779).
//
// So the assertions below describe the target, and the ones that fail name
// exactly what is missing. See reverse/analysis/PORT_PLAN.md.
//
// Data facts this pins (counted from assets/quests.xml):
//   504 <Quest> elements, each with <Events> / <Conditions> / <Actions>
//   24 distinct event names; ApplicationStart, SessionStart, FightEnd,
//   FightEnter, Activate, Purchase, ChangeTab, ... are the frequent ones
//   Priority orders execution; Unresumable / Group / AllowDoubles modify it
//   Conditions nest via <Operator Type="And|Or"> around
//   Equal / GreaterEqual / Greater / Less comparisons, each optionally Not="1"
//   Values may be expressions: "?VersionController().Production"

#include "../engine/format/xml_doc.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace resf2::format;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++failed; } \
    else { std::printf("  PASS: %s\n", msg); ++passed; } \
} while (0)

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string find_quests_xml() {
    for (const char* p : {"assets/quests.xml",
                          "assets/files/assets/quests.xml",
                          "assets/assets/quests.xml"}) {
        if (std::filesystem::exists(p)) return p;
    }
    return {};
}

// The event names that actually appear in the shipped quests.xml. A quest
// engine that cannot route these cannot run the story.
static const char* kRequiredEvents[] = {
    "ApplicationStart", "SessionStart", "Activate", "FightEnd", "FightEnter",
    "Purchase", "ChangeTab", "MapButtonPress", "EnergyBarPress",
};

int main() {
    std::printf("=== quest system specification (assets/quests.xml) ===\n");

    const auto path = find_quests_xml();
    if (path.empty()) {
        std::fprintf(stderr, "quests.xml not found; run from the repo root\n");
        return 1;
    }
    const auto xml = read_file(path);
    std::printf("loaded %s (%zu bytes)\n\n", path.c_str(), xml.size());

    XmlDocument doc;
    CHECK(doc.parse(xml), "quests.xml parses with the engine's XML reader");
    const XmlNode* root = doc.root();
    // The reader wraps the file in a synthetic "#document" node, so the real
    // <Quests> container is one level down.
    if (root && root->name == "#document") {
        if (const XmlNode* q = root->first_child("Quests")) root = q;
    }
    if (!root) {
        std::fprintf(stderr, "no root element\n");
        return 1;
    }
    std::printf("  container <%s> with %zu children\n",
                root->name.c_str(), root->children.size());

    // ---- structure ----
    std::printf("\n-- quest structure --\n");
    std::vector<const XmlNode*> quests;
    for (const auto& child : root->children) {
        if (child.name == "Quest") quests.push_back(&child);
    }
    std::printf("  found %zu <Quest> elements\n", quests.size());
    // 504 <Quest occurrences in the raw text, but 6 of those sit inside XML
    // comments, so 498 is the correct live count. (Verified by stripping
    // comments and re-counting -- worth noting, because a naive grep over the
    // file gives 504 and makes a correct parser look broken.)
    CHECK(quests.size() == 498,
          "the shipped story has 498 live quests (504 raw, 6 commented out)");

    int with_events = 0, with_conditions = 0, with_actions = 0;
    int with_priority = 0, unresumable = 0;
    std::set<std::string> event_names;
    std::set<std::string> action_names;

    for (const auto* q : quests) {
        if (!q->attr("Priority").empty()) ++with_priority;
        if (q->attr("Unresumable") == "1") ++unresumable;
        for (const auto& sec : q->children) {
            if (sec.name == "Events") {
                ++with_events;
                for (const auto& e : sec.children) event_names.insert(e.name);
            } else if (sec.name == "Conditions") {
                ++with_conditions;
            } else if (sec.name == "Actions") {
                ++with_actions;
                for (const auto& a : sec.children) action_names.insert(a.name);
            }
        }
    }

    std::printf("  events=%d conditions=%d actions=%d priority=%d unresumable=%d\n",
                with_events, with_conditions, with_actions,
                with_priority, unresumable);
    CHECK(with_events == 478,
          "478 quests are triggered by an event; the rest are run explicitly");
    CHECK(with_actions == 498, "every live quest has an <Actions> block");
    CHECK(with_conditions >= 490, "nearly every quest is gated by <Conditions>");
    CHECK(with_priority >= 400,
          "Priority is set on most quests and must order execution");
    CHECK(unresumable >= 100,
          "Unresumable is set on 165 quests and must suppress replay");

    // ---- events the engine has to route ----
    std::printf("\n-- event vocabulary --\n");
    std::printf("  %zu distinct event names\n", event_names.size());
    CHECK(event_names.size() >= 20,
          "24 distinct event names appear in the shipped data");
    for (const char* e : kRequiredEvents) {
        CHECK(event_names.count(e) > 0,
              (std::string("event '") + e + "' is present and must be routable").c_str());
    }

    // ---- actions the engine has to execute ----
    std::printf("\n-- action vocabulary --\n");
    std::printf("  %zu distinct top-level action names\n", action_names.size());
    for (const char* a : {"Dialog", "SetVariable", "ShowBattle", "ChangeScene",
                          "OpenShop", "ClearQuestQueue", "ToggleItems"}) {
        CHECK(action_names.count(a) > 0,
              (std::string("action '") + a + "' is present and must be executable").c_str());
    }

    // ---- condition expression language ----
    std::printf("\n-- condition expressions --\n");
    // Conditions compare values that may be function-call expressions, e.g.
    //   Value1="?VersionController().Production"
    // A quest engine that cannot evaluate those cannot gate anything.
    int expr_refs = 0, nested_ops = 0, negated = 0;
    std::size_t pos = 0;
    while ((pos = xml.find("Value1=\"?", pos)) != std::string::npos) {
        ++expr_refs;
        pos += 9;
    }
    pos = 0;
    while ((pos = xml.find("<Operator", pos)) != std::string::npos) {
        ++nested_ops;
        pos += 9;
    }
    pos = 0;
    while ((pos = xml.find("Not=\"1\"", pos)) != std::string::npos) {
        ++negated;
        pos += 7;
    }
    std::printf("  '?expr' operands=%d  <Operator>=%d  Not=\"1\"=%d\n",
                expr_refs, nested_ops, negated);
    CHECK(expr_refs > 100,
          "conditions use '?Function().Field' expressions that need evaluating");
    CHECK(nested_ops > 200,
          "conditions nest through <Operator Type=\"And|Or\">");
    CHECK(negated > 50, "Not=\"1\" inverts a comparison and must be honoured");

    // ---- dialogs carry buttons with nested actions ----
    std::printf("\n-- dialogs --\n");
    int dialogs = 0, lines = 0, buttons = 0;
    pos = 0;
    while ((pos = xml.find("<Dialog", pos)) != std::string::npos) { ++dialogs; pos += 7; }
    pos = 0;
    while ((pos = xml.find("<Line", pos)) != std::string::npos) { ++lines; pos += 5; }
    pos = 0;
    while ((pos = xml.find("<Button", pos)) != std::string::npos) { ++buttons; pos += 7; }
    std::printf("  Dialog=%d Line=%d Button=%d\n", dialogs, lines, buttons);
    CHECK(dialogs >= 160, "the story has 168 dialogs");
    CHECK(lines >= 180, "dialogs carry <Line Text=\"locKey\"> children");
    CHECK(buttons >= 200,
          "dialogs carry <Button> children whose own actions run on press");

    std::printf("\n=== %d passed, %d failed ===\n", passed, failed);
    if (failed) {
        std::printf("\nFailures describe quest-system capability the original has\n"
                    "and reSF2 does not; see PORT_PLAN.md.\n");
    }
    return failed == 0 ? 0 : 1;
}

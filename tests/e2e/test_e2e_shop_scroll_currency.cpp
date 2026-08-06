// tests/e2e/test_e2e_shop_scroll_currency.cpp
//
// Wave 10A defect 5 — SHOP SCROLL-DOWN + CURRENCY: the category item list
// could not be scrolled down in a driven run (script keys never reached the
// Shop scene — host_update_gameplay, the only tick_input_script caller,
// does not run there), and the BUY price carried the RUBY (gem) icon while
// shop prices are COINS.
//
// E2E on the REAL binary: boot --scene shop, press S six times. Assert:
//   1. the visible window actually moved ([SHOP-SCROLL] offset rows — the
//      list scrolled down by several rows), and
//   2. the price row drew the COIN icon ([SHOP-PRICE] icon='credit'),
//      not the gem.
// RED on HEAD: no [SHOP-SCROLL] rows at all (keys dead in the Shop scene)
// and icon='ruby'.

#include <cstdio>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;
using resf2::test::check_ge;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_shop_scroll_currency <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // The shop opens directly; six S presses, one every 10 frames.
    std::vector<e2e::InputEvent> events;
    for (int f = 10; f <= 60; f += 10) {
        events.push_back({f, true, "S"});
        events.push_back({f + 2, false, "S"});
    }

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_shop_scroll_input.txt";
    spec.out_name = "e2e_shop_scroll";
    spec.max_frames = 120;
    spec.extra_args = {"--scene", "shop"};
    spec.no_log = true;       // stdout [SHOP-*] probes
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    // ------------------------------------------------------------ scrolling
    const auto scrolls = e2e::filter_lines(run.stdout_lines, "[SHOP-SCROLL]");
    std::printf("shop: %zu [SHOP-SCROLL] row(s)\n", scrolls.size());
    float max_offset = -1.0f;
    int max_scroll_seen = -1;
    static const std::regex re(R"(offset=([-\d.]+) max=([-\d.]+))");
    for (const auto& l : scrolls) {
        std::smatch m;
        if (std::regex_search(l, m, re)) {
            max_offset = std::max(max_offset, std::stof(m[1]));
            max_scroll_seen = std::max(max_scroll_seen, (int)std::stof(m[2]));
        }
    }
    std::printf("shop: scrolled to offset %.0f of max %d\n",
                max_offset, max_scroll_seen);
    check(max_scroll_seen > 0,
          "the category list has more rows than the visible window "
          "(scrolling is meaningful)");
    check_ge(static_cast<double>(max_offset), 4.0,
             "six S presses scrolled the visible window down by 4+ rows");

    // ---------------------------------------------------------- price icon
    const auto prices = e2e::filter_lines(run.stdout_lines, "[SHOP-PRICE]");
    std::printf("shop: %zu [SHOP-PRICE] row(s)\n", prices.size());
    check(!prices.empty(), "a price row was rendered and probed");
    bool coin = false, ruby = false;
    for (const auto& l : prices) {
        if (l.find("icon='credit'") != std::string::npos) coin = true;
        if (l.find("icon='ruby'") != std::string::npos) ruby = true;
    }
    std::printf("shop: price icon coin=%d ruby=%d\n", (int)coin, (int)ruby);
    check(coin, "the price row renders the COIN icon");
    check(!ruby, "the price row does not use the RUBY (gem) icon");

    return resf2::test::summary();
}

#include "../engine/reverse/name_utils.hpp"
#include <cstdio>
#include <cstring>

static int failed = 0;

static void test_eq(const char* label, uint32_t result, uint32_t expected_lowbyte) {
    uint32_t lowbyte = result & 0xFF;
    if (lowbyte != expected_lowbyte) {
        std::fprintf(stderr, "FAIL %s: expected_lowbyte=%u got=%u (result=0x%08X)\n",
                     label, expected_lowbyte, lowbyte, result);
        failed = 1;
    } else {
        std::printf("PASS %s: lowbyte=%u\n", label, lowbyte);
    }
}

static void test_string_equal_with_range() {
    const char* hello = "Hello";
    NameRange hr{hello, hello + 5};

    test_eq("swe_str exact", stringEqualWithRange("Hello", hr), 1);
    test_eq("swe_str content mismatch", stringEqualWithRange("World", hr), 0);

    const char* hi = "Hi";
    test_eq("swe_str too short", stringEqualWithRange("Hi", hr), 0);
    test_eq("swe_str too long", stringEqualWithRange("HelloWorld", hr), 0);

    const char* empty = "";
    test_eq("swe_str empty", stringEqualWithRange("", {empty, empty}), 1);

    const char* four = "Four";
    test_eq("swe_str 4-byte", stringEqualWithRange("Four", {four, four + 4}), 1);

    const char* eight = "Eight888";
    test_eq("swe_str 8-byte", stringEqualWithRange("Eight888", {eight, eight + 8}), 1);
}

static void test_name_range_equal() {
    const char* hello = "Hello";
    const char* hello2 = "Hello";
    const char* world = "World";
    const char* helLo = "HelLo";

    test_eq("nre exact", nameRangeEqual({hello, hello + 5}, {hello2, hello2 + 5}), 1);
    test_eq("nre diff content", nameRangeEqual({hello, hello + 5}, {world, world + 5}), 0);
    test_eq("nre len mismatch", nameRangeEqual({hello, hello + 5}, {hello, hello + 3}), 0);
    test_eq("nre empty", nameRangeEqual({hello, hello}, {hello, hello}), 1);
    test_eq("nre last byte diff", nameRangeEqual({hello, hello + 5}, {helLo, helLo + 5}), 0);

    const char* four = "Four";
    const char* four2 = "Four";
    test_eq("nre 4-byte", nameRangeEqual({four, four + 4}, {four2, four2 + 4}), 1);
}

static void test_name_range_equals_str() {
    const char* hello = "Hello";

    test_eq("nres exact", nameRangeEqualsStr({hello, hello + 5}, "Hello"), 1);
    test_eq("nres content mismatch", nameRangeEqualsStr({hello, hello + 5}, "World"), 0);
    test_eq("nres too short", nameRangeEqualsStr({hello, hello + 5}, "Hi"), 0);
    test_eq("nres too long", nameRangeEqualsStr({hello, hello + 5}, "HelloWorld"), 0);

    const char* empty = "";
    test_eq("nres empty", nameRangeEqualsStr({empty, empty}, ""), 1);

    const char* four = "Four";
    test_eq("nres 4-byte", nameRangeEqualsStr({four, four + 4}, "Four"), 1);

    const char* helLo = "HelLo";
    test_eq("nres last byte", nameRangeEqualsStr({helLo, helLo + 5}, "HelLo"), 1);
    test_eq("nres last byte mismatch", nameRangeEqualsStr({hello, hello + 5}, "HelLo"), 0);
}

int main() {
    std::printf("=== stringEqualWithRange ===\n");
    test_string_equal_with_range();

    std::printf("\n=== nameRangeEqual ===\n");
    test_name_range_equal();

    std::printf("\n=== nameRangeEqualsStr ===\n");
    test_name_range_equals_str();

    std::printf("\n%s\n", failed ? "*** FAILED ***" : "*** ALL PASSED ***");
    return failed;
}

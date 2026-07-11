// tests/test_s3e_container.cpp
//
// Unit tests for engine/reverse/s3e_container.
//
// Build (standalone, no test framework dependency for portability):
//   g++ -std=c++23 -Wall -Wextra -Iengine \
//       tests/test_s3e_container.cpp engine/reverse/s3e_container.cpp \
//       -o build/test_s3e_container
//   ./build/test_s3e_container

#include "../engine/reverse/s3e_container.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace resf2::reverse::s3e;

namespace fs = std::filesystem;

// Tiny test framework: each test asserts; on failure prints + aborts.
static int g_failures = 0;
static int g_tests    = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_tests;                                                      \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK(%s)\n",             \
                         __FILE__, __LINE__, #cond);                    \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        ++g_tests;                                                      \
        if (!((a) == (b))) {                                            \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK_EQ(%s, %s)\n",      \
                         __FILE__, __LINE__, #a, #b);                   \
        }                                                               \
    } while (0)

// ---------- synthetic-data tests ----------

static void test_empty_input_rejected() {
    std::vector<std::byte> empty;
    auto r = parse(std::span<const std::byte>(empty));
    CHECK(!r.has_value());
    CHECK_EQ(r.error(), ParseError::kInputEmpty);
}

static void test_too_small_rejected() {
    std::vector<std::byte> small(10);  // less than 76-byte header
    auto r = parse(std::span<const std::byte>(small));
    CHECK(!r.has_value());
    CHECK_EQ(r.error(), ParseError::kInputTooSmall);
}

static void test_bad_magic_rejected() {
    std::vector<std::byte> buf(200);
    std::memset(buf.data(), 0, buf.size());
    // Write wrong magic.
    buf[0] = std::byte{'X'};
    buf[1] = std::byte{'X'};
    buf[2] = std::byte{'X'};
    buf[3] = std::byte{'X'};
    auto r = parse(std::span<const std::byte>(buf));
    CHECK(!r.has_value());
    CHECK_EQ(r.error(), ParseError::kBadMagic);
}

// Build a minimal valid S3E buffer with a tiny embedded config + import
// table, used by subsequent tests.
static std::vector<std::byte> make_minimal_s3e() {
    std::vector<std::byte> buf;
    buf.resize(0x200);

    auto write_u32 = [&](std::size_t off, std::uint32_t v) {
        buf[off + 0] = std::byte(v & 0xff);
        buf[off + 1] = std::byte((v >> 8) & 0xff);
        buf[off + 2] = std::byte((v >> 16) & 0xff);
        buf[off + 3] = std::byte((v >> 24) & 0xff);
    };

    // Magic.
    buf[0] = std::byte{'X'};
    buf[1] = std::byte{'E'};
    buf[2] = std::byte{'3'};
    buf[3] = std::byte{'U'};

    // Embedded config at offset 0x4c, length 0x10 (16 bytes of text).
    write_u32(0x2c, 0x4c);
    write_u32(0x30, 0x10);
    const char config[] = "# test config\nxx";
    std::memcpy(buf.data() + 0x4c, config, sizeof(config) - 1);

    // Import table starts at 0x4c + 0x10 = 0x5c. We add 8 bytes preamble,
    // then two names, then a NUL. Set import_table_start (offset 0x0c) to
    // the start of the table (which equals end of config text), and u32_10
    // (offset 0x10) to the end of the names region as the scan upper bound.
    const std::uint32_t table_start = 0x4c + 0x10;       // 0x5c
    const std::uint32_t names_start = table_start + 8;   // 0x64
    const char name1[] = "s3eMallocBase";
    const char name2[] = "glBindRenderbuffer";
    std::memcpy(buf.data() + names_start,             name1, sizeof(name1));
    std::memcpy(buf.data() + names_start + sizeof(name1), name2, sizeof(name2));
    const std::uint32_t names_end = names_start + sizeof(name1) + sizeof(name2);
    write_u32(0x0c, table_start);   // import_table_start
    write_u32(0x10, names_end);     // u32_10 = scan upper bound

    return buf;
}

static void test_minimal_valid_parses() {
    auto buf = make_minimal_s3e();
    auto r = parse(std::span<const std::byte>(buf));
    CHECK(r.has_value());
    if (!r) return;
    auto& f = *r;

    CHECK_EQ(f.header.config_offset, 0x4cu);
    CHECK_EQ(f.header.config_length, 0x10u);
    CHECK_EQ(f.config_text.size(), 0x10u);
    CHECK_EQ(f.config_text.substr(0, 13), "# test config");

    CHECK_EQ(f.imports.size(), 2u);
    if (f.imports.size() >= 2) {
        CHECK_EQ(f.imports[0].name, "s3eMallocBase");
        CHECK_EQ(f.imports[1].name, "glBindRenderbuffer");
    }
}

// ---------- real-file test (optional; skipped if fixture missing) ----------

static void test_real_shadowfight2_s3e() {
    // This test runs only if the user has placed a decompressed
    // ShadowFight2.bin at tests/fixtures/ShadowFight2.bin.
    // The file is .gitignored (it's the original game binary, not part
    // of reSF2).
    const fs::path fixture = "tests/fixtures/ShadowFight2.bin";
    if (!fs::exists(fixture)) {
        std::printf("SKIP test_real_shadowfight2_s3e (fixture %s missing)\n",
                    fixture.string().c_str());
        return;
    }

    auto r = parse_file(fixture.string());
    CHECK(r.has_value());
    if (!r) return;
    auto& [buf, f] = *r;

    // Sanity: file should be ~8.69 MB.
    CHECK(buf->size() > 8'000'000);
    CHECK(buf->size() < 9'000'000);

    // Header sanity.
    CHECK_EQ(f.header.config_offset, 0x4cu);
    CHECK_EQ(f.header.config_length, 0x14d5u);  // 5333 bytes
    CHECK(f.config_text.starts_with("# This is the global system"));

    // Import table sanity: Shadow Fight 2 has 346 valid names.
    CHECK(f.imports.size() >= 300);
    CHECK(f.imports.size() <= 400);

    // Spot-check a few known names.
    bool found_s3e_malloc = false;
    bool found_gl_bind    = false;
    for (const auto& imp : f.imports) {
        if (imp.name == "s3eMallocBase")    found_s3e_malloc = true;
        if (imp.name == "glBindRenderbuffer") found_gl_bind  = true;
    }
    CHECK(found_s3e_malloc);
    CHECK(found_gl_bind);

    // Dump summary to stdout for visual confirmation.
    std::printf("%s\n", dump_summary(f).c_str());
}

int main() {
    test_empty_input_rejected();
    test_too_small_rejected();
    test_bad_magic_rejected();
    test_minimal_valid_parses();
    test_real_shadowfight2_s3e();

    std::printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}

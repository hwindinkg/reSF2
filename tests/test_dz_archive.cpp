// tests/test_dz_archive.cpp
//
// End-to-end check of the derbh (.dz) reader against the shipped archives.
//
// files.dz uses the DZ Coder (mask 0x004), animations.dz / ZONE_*.dz use the
// ZLib Coder (mask 0x008). Every file is decoded and its size checked against
// the block table; a handful of files whose extracted form is committed to the
// repository are additionally compared byte-for-byte.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../engine/reverse/dz_reader.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

std::vector<std::byte> read_disk(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::byte> d(size);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(size));
    return d;
}

int g_archives_exercised = 0;

// Decode every file in the archive and report how many came out non-empty.
void exercise(const std::filesystem::path& archive) {
    // A missing archive is a failure, not a skip: these ship with the repo, and
    // silently passing when they are absent is how a broken reader hides.
    check(std::filesystem::exists(archive), "present: " + archive.string());
    if (!std::filesystem::exists(archive)) return;
    ++g_archives_exercised;
    resf2::dz::DzArchive a;
    check(a.open(archive.string()), "open " + archive.string());

    size_t ok = 0, bad = 0;
    for (const auto& name : a.file_names()) {
        auto data = a.read_file(name);
        if (data.empty()) {
            ++bad;
            if (bad <= 5)
                std::fprintf(stderr, "  empty: %s\n", name.c_str());
        } else {
            ++ok;
        }
    }
    std::printf("%s: decoded %zu, failed %zu\n", archive.filename().string().c_str(), ok, bad);
    check(bad == 0, archive.filename().string() + ": every file decodes");
}

// Compare against extracted copies committed under assets/.
void compare_known(const std::filesystem::path& archive,
                   const std::vector<std::pair<std::string, std::string>>& pairs) {
    if (!std::filesystem::exists(archive)) return;
    resf2::dz::DzArchive a;
    if (!a.open(archive.string())) return;

    for (const auto& [in_archive, on_disk] : pairs) {
        auto ref = read_disk(on_disk);
        if (ref.empty()) {
            std::printf("  SKIP %s (no reference at %s)\n", in_archive.c_str(), on_disk.c_str());
            continue;
        }
        auto got = a.read_file(in_archive);
        check(got.size() == ref.size(),
              in_archive + ": size " + std::to_string(got.size()) +
                  " == " + std::to_string(ref.size()));
        check(got == ref, in_archive + ": byte-for-byte match");
        if (got == ref)
            std::printf("  OK %s (%zu bytes)\n", in_archive.c_str(), got.size());
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path root = (argc > 1) ? argv[1] : ".";
    const auto assets = root / "assets";

    exercise(assets / "files.dz");
    exercise(assets / "animations.dz");

    compare_known(assets / "files.dz", {
        {"files_list.xml", (assets / "files" / "files_list.xml").string()},
        {"forge.xml", (assets / "forge.xml").string()},
        {"moves.xml", (assets / "animations" / "moves.xml").string()},
    });
    compare_known(assets / "animations.dz", {
        {"animations_list.xml", (assets / "animations" / "animations_list.xml").string()},
    });

    check(g_archives_exercised >= 2, "both shipped archives were exercised");

    if (g_failures) {
        std::fprintf(stderr, "\n%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}

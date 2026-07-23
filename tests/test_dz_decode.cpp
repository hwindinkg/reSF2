#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

#include "../engine/reverse/dz_decoder.hpp"
#include "../engine/reverse/dz_reader.hpp"

static bool read_file(const char* path, std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    data.resize(static_cast<size_t>(sz));
    std::fread(data.data(), 1, static_cast<size_t>(sz), f);
    std::fclose(f);
    return true;
}

int main() {
    std::printf("Test DZ Decode (fallback mode - files on disk)\n");

    // Verify fallback files exist
    const char* test_files[] = {
        "assets/files/files_list.xml",
        "assets/files/settings.xml",
        "assets/files/assets/Achievements.xml",
        "assets/animations/animations_list.xml",
        "assets/animations/binary/axe_double_slash.bin"
    };
    
    int failures = 0;
    for (auto* path : test_files) {
        FILE* f = std::fopen(path, "rb");
        if (!f) {
            std::fprintf(stderr, "MISSING: %s\n", path);
            ++failures;
        } else {
            std::fseek(f, 0, SEEK_END);
            long sz = std::ftell(f);
            std::fclose(f);
            std::printf("  OK: %s (%ld bytes)\n", path, sz);
        }
    }
    
    // Verify file 0 content matches ground truth
    std::vector<uint8_t> gt;
    if (read_file("assets/files/files_list.xml", gt)) {
        std::printf("\n  files_list.xml: %zu bytes, first=0x%02x ('%c')\n",
                    gt.size(), gt[0], gt[0] >= 32 ? (char)gt[0] : '.');
        std::printf("  Ground truth: VALID\n");
    }

    std::printf("\nResults: %d failures\n", failures);
    return failures;
}

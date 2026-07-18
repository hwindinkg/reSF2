#include <cstdio>
#include <cstdint>
#include <vector>
#include <filesystem>
#include "../engine/reverse/dz_reader.hpp"

namespace fs = std::filesystem;
namespace dz = resf2::dz;

int main() {
    auto& reg = dz::DzRegistry::instance();
    if (!reg.open_archive("assets/animations.dz")) {
        std::printf("FAIL: open animations.dz\n");
        return 1;
    }

    // List first 10 files
    auto& names = reg.read_file("__list__"); // won't work, but let's try
    (void)names;

    // Get from DzArchive directly — find first .bin entry
    // Actually let's just try "hero_1/hand_1.bin" or whatever
    // First, dump the file listing
    std::printf("Animations archive opened\n");

    // Try reading a known animation
    auto data = reg.read_file("001_Stand_Idle_01_1.bin");
    if (data.empty()) {
        // Try without .bin
        data = reg.read_file("001_Stand_Idle_01_1");
    }
    if (data.empty()) {
        std::printf("Can't find specific animation, listing files...\n");
        // Can't list files from registry, need to access archive directly
    } else {
        std::printf("Found animation: %zu bytes\n", data.size());
        std::printf("Hex header:\n");
        size_t n = data.size() > 64 ? 64 : data.size();
        for (size_t i = 0; i < n; i++) {
            std::printf("%02x ", (uint8_t)data[i]);
            if ((i + 1) % 16 == 0) std::printf("\n");
        }
        std::printf("\n");
        // First 4 bytes as u32
        if (data.size() >= 4) {
            uint32_t first = *(uint32_t*)data.data();
            std::printf("First u32: %u (0x%x)\n", first, first);
        }
    }
    return 0;
}

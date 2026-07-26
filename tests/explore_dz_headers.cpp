#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

int main() {
    FILE* f = nullptr;
    if (fopen_s(&f, "assets/files.dz", "rb") || !f) { return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> dz(static_cast<size_t>(fsize));
    fread(dz.data(), 1, fsize, f);
    fclose(f);

    // Parse DTRZ header
    size_t pos = 9;
    uint16_t num_files = static_cast<uint16_t>(dz[4]) | (static_cast<uint16_t>(dz[5]) << 8);
    uint16_t num_dirs = static_cast<uint16_t>(dz[6]) | (static_cast<uint16_t>(dz[7]) << 8);
    
    // Skip names
    for (uint16_t i = 0; i < num_files; i++) {
        while (pos < dz.size() && dz[pos] != 0) pos++;
        pos++;
    }
    for (uint16_t i = 0; i < num_dirs; i++) {
        while (pos < dz.size() && dz[pos] != 0) pos++;
        pos++;
    }
    // Skip file attributes
    pos += num_files * 6 + 3;
    
    // Read file entries
    struct Entry { uint32_t off, comp, uncomp, type; };
    std::vector<Entry> entries;
    entries.reserve(num_files);
    for (uint16_t i = 0; i < num_files; i++) {
        uint32_t f0, f1, f2, f3;
        memcpy(&f0, &dz[pos], 4);
        memcpy(&f1, &dz[pos+4], 4);
        memcpy(&f2, &dz[pos+8], 4);
        memcpy(&f3, &dz[pos+12], 4);
        entries.push_back({f0, f1, f2, f3});
        pos += 16;
    }
    
    // Print first 10 entries with headers and compressed first bytes
    printf("=== First 10 DZ files with their 10-byte RangeSettings headers ===\n\n");
    for (int i = 0; i < 10 && i < (int)entries.size(); i++) {
        auto& e = entries[i];
        const uint8_t* hdr = dz.data() + e.off;  // 10-byte header
        const uint8_t* comp = dz.data() + e.off + 10;  // compressed data starts after header
        int comp_data_size = static_cast<int>(e.comp) - 10;
        
        printf("[%2d] off=0x%06X comp=%-5d uncomp=%-5d type=%d\n", i, e.off, e.comp, e.uncomp, e.type);
        printf("     hdr: ");
        for (int j = 0; j < 10; j++) printf("%02X ", hdr[j]);
        printf("\n");
        if (comp_data_size >= 4) {
            printf("     compressed[0..7]: ");
            for (int j = 0; j < (comp_data_size < 8 ? comp_data_size : 8); j++)
                printf("%02X ", comp[j]);
            printf("\n");
        }
        
        // Compare headers
        if (i > 0) {
            auto& prev = entries[0];
            const uint8_t* hdr0 = dz.data() + prev.off;
            bool same = memcmp(hdr, hdr0, 10) == 0;
            printf("     header %s first file\n", same ? "SAME as" : "DIFFERENT from");
        }
        printf("\n");
    }
    
    // Check how many unique headers exist
    printf("\n=== Unique headers across all %d files ===\n", num_files);
    std::vector<std::vector<uint8_t>> seen_headers;
    for (int i = 0; i < (int)entries.size(); i++) {
        auto& e = entries[i];
        if (e.type != 4) continue;  // only DZ type 4
        const uint8_t* hdr = dz.data() + e.off;
        bool found = false;
        for (auto& sh : seen_headers) {
            if (memcmp(sh.data(), hdr, 10) == 0) { found = true; break; }
        }
        if (!found) {
            seen_headers.push_back(std::vector<uint8_t>(hdr, hdr+10));
            printf("  header[%zu]: ", seen_headers.size()-1);
            for (int j = 0; j < 10; j++) printf("%02X ", hdr[j]);
            printf("\n");
        }
    }
    printf("Total unique headers: %zu\n", seen_headers.size());
    
    // Check type 8 files (animations.dz style)
    printf("\n=== Type 8 file headers ===\n");
    for (int i = 0; i < (int)entries.size(); i++) {
        auto& e = entries[i];
        if (e.type == 8) {
            const uint8_t* hdr = dz.data() + e.off;
            printf("[%2d] off=0x%06X comp=%d hdr: ", i, e.off, e.comp);
            for (int j = 0; j < 10; j++) printf("%02X ", hdr[j]);
            printf("\n");
        }
    }

    return 0;
}

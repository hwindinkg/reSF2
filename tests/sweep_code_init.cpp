// Sweep all code-init/byte-skip combinations to find which gives 0x3C
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

int main() {
    FILE* f = nullptr;
    if (fopen_s(&f, "assets/files.dz", "rb") || !f) return 1;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> dz(fsize);
    fread(dz.data(), 1, fsize, f);
    fclose(f);

    const uint8_t* comp = dz.data() + 0x1937;
    size_t comp_size = 883;

    // Sweep: skip=0..20 (bytes to skip from start), ninit=3..5 (bytes to read for code)
    printf("Sweeping code-init positions...\n");
    for (int skip = 0; skip < 20; skip++) {
        for (int nread = 3; nread <= 5; nread++) {
            if (skip + nread > (int)comp_size) continue;
            
            std::vector<uint16_t> prob(8192, 0x400);
            uint32_t range = 0xFFFFFFFF;
            uint32_t code = 0;
            const uint8_t* in_pos = comp + skip;
            const uint8_t* in_end = comp + comp_size;
            
            for (int i = 0; i < nread && in_pos < in_end; i++)
                code = (code << 8) | *in_pos++;
            
            // Try with and without discarding top byte
            for (int discard = -1; discard <= 1; discard++) {
                uint32_t code_try = code;
                const uint8_t* in_try = in_pos;
                if (discard == 1 && nread >= 1) {
                    // Discard top byte (LZMA style: first byte is properties)
                    code_try = 0;
                    in_try = comp + skip + 1;
                    for (int i = 0; i < nread-1 && in_try < in_end; i++)
                        code_try = (code_try << 8) | *in_try++;
                    in_try = comp + skip + nread;
                } else if (discard == -1) {
                    in_try = in_pos;
                } else {
                    in_try = in_pos;
                }
                
                // Decode first byte
                std::vector<uint16_t> p = prob;
                uint32_t r = range;
                uint32_t c = code_try;
                const uint8_t* ip = in_try;
                const uint8_t* ie = in_end;
                
                auto norm = [&]() {
                    while (r < 0x1000000 && ip < ie + 10) {
                        r <<= 8;
                        if (ip < ie) c = (c << 8) | *ip++;
                        else c <<= 8;
                    }
                };
                auto bit = [&](uint16_t& prob) -> int {
                    norm();
                    uint32_t b = (r >> 11) * prob;
                    if (c < b) { r = b; prob += (0x800 - prob) >> 5; return 0; }
                    else { c -= b; r -= b; prob -= prob >> 5; return 1; }
                };
                
                bit(p[0]); // is_match
                uint32_t sym = 1;
                for (int i = 0; i < 8; i++)
                    sym = (sym << 1) | bit(p[0x736 + sym]);
                uint8_t byte = (uint8_t)(sym - 256);
                
                if (byte == 0x3C) {
                    printf("  *** MATCH! skip=%d nread=%d discard=%d code=0x%08X byte=0x%02X\n",
                           skip, nread, discard, code_try, byte);
                }
            }
        }
    }
    
    printf("\nDone.\n");
    return 0;
}

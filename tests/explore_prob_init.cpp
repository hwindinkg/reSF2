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
    std::vector<uint8_t> dz(fsize);
    fread(dz.data(), 1, fsize, f);
    fclose(f);

    const uint8_t* comp = dz.data() + 0x1937;
    size_t comp_size = 883;

    printf("=== Explore DZ first-byte decode ===\n");
    printf("Initial code bytes: %02X %02X %02X %02X\n\n", comp[0], comp[1], comp[2], comp[3]);

    // Try v2.py-style: norm_thresh=0x10000, shift=8, prob=128, context-based
    printf("--- v2.py style (norm<0x10000, shift=8, prob=128) ---\n");
    for (int tries = 0; tries < 3; tries++) {
        std::vector<uint16_t> prob(4096, 128);
        struct DzState { uint16_t* prob; uint8_t* in_pos; uint8_t* in_end; uint32_t range; uint32_t code; };
        DzState st{};
        st.prob = prob.data(); st.range = 0xFFFFFFFF;
        st.code = (comp[0]<<24)|(comp[1]<<16)|(comp[2]<<8)|comp[3];
        st.in_pos = const_cast<uint8_t*>(comp) + 4;
        st.in_end = const_cast<uint8_t*>(comp) + comp_size;
        auto norm = [&]() { int g=0; while(st.range<0x10000&&g<10){g++;st.range<<=8;if(st.in_pos<st.in_end)st.code=(st.code<<8)|*st.in_pos++;else st.code<<=8;} };
        auto bit = [&](uint16_t& p) -> int { 
            norm(); 
            uint32_t b = (st.range>>8)*(uint32_t)p; 
            if(st.code<b){st.range=b; p+=static_cast<uint16_t>((0x8000-p)>>5); return 0;}
            else{st.code-=b;st.range-=b; p-=static_cast<uint16_t>(p>>5); return 1;} 
        };
        if (tries > 0) (void)bit(prob[0]);
        uint32_t sym = 1;
        for(int i=0;i<8;i++) sym=(sym<<1)|bit(prob[0x736+sym]);
        uint8_t byte = static_cast<uint8_t>(sym-256);
        printf("  is_match=%d: byte=0x%02X %s\n", tries>0?1:0, byte, byte==0x3C?"MATCH!":"");
    }

    // Try mix: norm<0x10000, shift=8, prob=0x400
    printf("\n--- norm<0x10000, shift=8, prob=0x400 ---\n");
    for (int init_prob = 0x400; init_prob <= 0x800; init_prob += 0x80) {
        std::vector<uint16_t> prob(4096, static_cast<uint16_t>(init_prob));
        struct DzState { uint16_t* prob; uint8_t* in_pos; uint8_t* in_end; uint32_t range; uint32_t code; };
        DzState st{};
        st.prob = prob.data(); st.range = 0xFFFFFFFF;
        st.code = (comp[0]<<24)|(comp[1]<<16)|(comp[2]<<8)|comp[3];
        st.in_pos = const_cast<uint8_t*>(comp) + 4;
        st.in_end = const_cast<uint8_t*>(comp) + comp_size;
        auto norm = [&]() { int g=0; while(st.range<0x10000&&g<10){g++;st.range<<=8;if(st.in_pos<st.in_end)st.code=(st.code<<8)|*st.in_pos++;else st.code<<=8;} };
        auto bit = [&](uint16_t& p) -> int { norm(); uint32_t b = (st.range>>8)*(uint32_t)p; if(st.code<b){st.range=b;p+=(0x800-p)>>5;return 0;}else{st.code-=b;st.range-=b;p-=p>>5;return 1;} };
        (void)bit(prob[0]);
        uint32_t sym = 1;
        for(int i=0;i<8;i++) sym=(sym<<1)|bit(prob[0x736+sym]);
        uint8_t byte = static_cast<uint8_t>(sym-256);
        printf("  prob=0x%04X: byte=0x%02X %s\n", init_prob, byte, byte==0x3C?"MATCH!":"");
    }

    // Try: LZMA-style with prob update >> 4 (faster adaptation)
    printf("\n--- prob_update >> 4 (LZMA prob_shift=4) ---\n");
    for (int init_prob = 0x200; init_prob <= 0x800; init_prob += 0x40) {
        std::vector<uint16_t> prob(4096, static_cast<uint16_t>(init_prob));
        struct DzState { uint16_t* prob; uint8_t* in_pos; uint8_t* in_end; uint32_t range; uint32_t code; };
        DzState st{};
        st.prob = prob.data(); st.range = 0xFFFFFFFF;
        st.code = (comp[0]<<24)|(comp[1]<<16)|(comp[2]<<8)|comp[3];
        st.in_pos = const_cast<uint8_t*>(comp) + 4;
        st.in_end = const_cast<uint8_t*>(comp) + comp_size;
        auto norm = [&]() { int g=0; while(st.range<0x1000000&&g<10){g++;st.range<<=8;if(st.in_pos<st.in_end)st.code=(st.code<<8)|*st.in_pos++;else st.code<<=8;} };
        auto bit = [&](uint16_t& p) -> int { norm(); uint32_t b = (st.range>>11)*(uint32_t)p; if(st.code<b){st.range=b;p+=static_cast<uint16_t>((0x800-p)>>4);return 0;}else{st.code-=b;st.range-=b;p-=static_cast<uint16_t>(p>>4);return 1;} };
        (void)bit(prob[0]);
        uint32_t sym = 1;
        for(int i=0;i<8;i++) sym=(sym<<1)|bit(prob[0x736+sym]);
        uint8_t byte = static_cast<uint8_t>(sym-256);
        if(byte==0x3B||byte==0x3C) printf("  init=0x%04X: byte=0x%02X %s\n", init_prob, byte, byte==0x3C?"MATCH!":"");
    }

    // Try: Different max prob in update formula 
    printf("\n--- prob_max=0x10000 range (full 16-bit) ---\n");
    for (int init_prob = 0x200; init_prob <= 0x8000; init_prob += init_prob) {
        std::vector<uint16_t> prob(4096, static_cast<uint16_t>(init_prob));
        struct DzState { uint16_t* prob; uint8_t* in_pos; uint8_t* in_end; uint32_t range; uint32_t code; };
        DzState st{};
        st.prob = prob.data(); st.range = 0xFFFFFFFF;
        st.code = (comp[0]<<24)|(comp[1]<<16)|(comp[2]<<8)|comp[3];
        st.in_pos = const_cast<uint8_t*>(comp) + 4;
        st.in_end = const_cast<uint8_t*>(comp) + comp_size;
        auto norm = [&]() { int g=0; while(st.range<0x10000&&g<10){g++;st.range<<=8;if(st.in_pos<st.in_end)st.code=(st.code<<8)|*st.in_pos++;else st.code<<=8;} };
        auto bit = [&](uint16_t& p) -> int { 
            norm(); 
            uint32_t b = (st.range>>11)*(uint32_t)p; 
            if(st.code<b){st.range=b; p+=static_cast<uint16_t>((0x10000-p)>>5); return 0;}
            else{st.code-=b;st.range-=b; p-=static_cast<uint16_t>(p>>5); return 1;} 
        };
        (void)bit(prob[0]);
        uint32_t sym = 1;
        for(int i=0;i<8;i++) sym=(sym<<1)|bit(prob[0x736+sym]);
        uint8_t byte = static_cast<uint8_t>(sym-256);
        printf("  init=0x%04X: byte=0x%02X %s\n", init_prob, byte, byte==0x3C?"MATCH!":"");
    }

    // The KEY experiment: use different init probs for each literal tree node
    printf("\n=== RangeSettings-based init: use header bytes for different table sections ===\n");
    const uint8_t* hdr = dz.data() + 0x192D;
    printf("Header: ");
    for (int i = 0; i < 10; i++) printf("%02X ", hdr[i]);
    printf("\n");
    
    // Last-ditch: try to read the code with is_match having DIFFERENT init
    // What if the first decision is NOT is_match but is_rep or something else?
    printf("\n--- First decision is is_rep (prob[96]) instead of is_match ---\n");
    for (int init = 0x200; init <= 0x800; init += 0x80) {
        std::vector<uint16_t> prob(4096, static_cast<uint16_t>(init));
        struct DzState { uint16_t* prob; uint8_t* in_pos; uint8_t* in_end; uint32_t range; uint32_t code; };
        DzState st{};
        st.prob = prob.data(); st.range = 0xFFFFFFFF;
        st.code = (comp[0]<<24)|(comp[1]<<16)|(comp[2]<<8)|comp[3];
        st.in_pos = const_cast<uint8_t*>(comp) + 4;
        st.in_end = const_cast<uint8_t*>(comp) + comp_size;
        auto norm = [&]() { int g=0; while(st.range<0x1000000&&g<10){g++;st.range<<=8;if(st.in_pos<st.in_end)st.code=(st.code<<8)|*st.in_pos++;else st.code<<=8;} };
        auto bit = [&](uint16_t& p) -> int { norm(); uint32_t b = (st.range>>11)*(uint32_t)p; if(st.code<b){st.range=b;p+=(0x800-p)>>5;return 0;}else{st.code-=b;st.range-=b;p-=p>>5;return 1;} };
        (void)bit(prob[96]); // is_rep at index 96 (just a guess)
        uint32_t sym = 1;
        for(int i=0;i<8;i++) sym=(sym<<1)|bit(prob[0x736+sym]);
        uint8_t byte = static_cast<uint8_t>(sym-256);
        printf("  init=0x%04X is_rep: byte=0x%02X %s\n", init, byte, byte==0x3C?"MATCH!":"");
    }

    // What if the first compressed byte is special?
    printf("\n--- Skip initial code byte 0 and try first decision differently ---\n");
    for (int skip = 0; skip <= 5; skip++) {
        for (int ninit = 4; ninit <= 5; ninit++) {
            std::vector<uint16_t> prob(4096, 0x400);
            struct DzState { uint16_t* prob; uint8_t* in_pos; uint8_t* in_end; uint32_t range; uint32_t code; };
            DzState st{};
            st.prob = prob.data(); st.range = 0xFFFFFFFF;
            st.code = 0; st.in_pos = const_cast<uint8_t*>(comp) + skip;
            st.in_end = const_cast<uint8_t*>(comp) + comp_size;
            int avail = static_cast<int>(st.in_end - st.in_pos);
            for (int i = 0; i < ninit && i < avail; i++)
                st.code = (st.code << 8) | *st.in_pos++;
            auto norm = [&]() { int g=0; while(st.range<0x1000000&&g<10){g++;st.range<<=8;if(st.in_pos<st.in_end)st.code=(st.code<<8)|*st.in_pos++;else st.code<<=8;} };
            auto bit = [&](uint16_t& p) -> int { norm(); uint32_t b = (st.range>>11)*(uint32_t)p; if(st.code<b){st.range=b;p+=(0x800-p)>>5;return 0;}else{st.code-=b;st.range-=b;p-=p>>5;return 1;} };
            (void)bit(prob[0]); // is_match
            uint32_t sym = 1;
            for(int i=0;i<8;i++) sym=(sym<<1)|bit(prob[0x736+sym]);
            uint8_t byte = static_cast<uint8_t>(sym-256);
            if (byte == 0x3C)
                printf("  skip=%d ninit=%d: byte=0x%02X MATCH!\n", skip, ninit, byte);
        }
    }

    return 0;
}

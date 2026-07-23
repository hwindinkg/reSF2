#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

#include "../engine/reverse/dz_decoder.hpp"

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
    std::printf("Test DZ Decode — full stream from byte 0\n");

    std::vector<uint8_t> dz;
    if (!read_file("assets/files.dz", dz)) {
        std::fprintf(stderr, "FAIL: Cannot open assets/files.dz\n");
        return 1;
    }

    // Parse file table
    uint16_t nf = static_cast<uint16_t>(dz[4]) | (static_cast<uint16_t>(dz[5]) << 8);
    uint16_t nd = static_cast<uint16_t>(dz[6]) | (static_cast<uint16_t>(dz[7]) << 8);
    size_t pos = 9;
    for (uint16_t i = 0; i < nf; ++i) {
        while (pos < dz.size() && dz[pos] != 0) ++pos;
        ++pos;
    }
    for (uint16_t i = 0; i < nd; ++i) {
        while (pos < dz.size() && dz[pos] != 0) ++pos;
        ++pos;
    }
    pos += static_cast<size_t>(nf) * 6 + 4;
    uint32_t data_section = static_cast<uint32_t>(pos) + static_cast<uint32_t>(nf) * 16;

    // Get stream reference and read header
    const uint8_t* full_stream = dz.data() + data_section;
    std::printf("Header bytes 0-4: %02x %02x %02x %02x %02x\n",
                full_stream[0], full_stream[1], full_stream[2],
                full_stream[3], full_stream[4]);
    std::printf("Initial code = 0x%02x%02x%02x%02x\n",
                full_stream[1], full_stream[2], full_stream[3], full_stream[4]);

    // Compute total uncompressed and max stream offset
    uint32_t total_uncomp = 0;
    uint32_t max_end = 0;
    uint32_t file0_off = 0, file0_unc = 0, file0_cmp = 0;
    for (uint16_t i = 0; i < nf; ++i) {
        uint32_t f0, f1, f2;
        std::memcpy(&f0, dz.data() + pos + i * 16, 4);
        std::memcpy(&f1, dz.data() + pos + i * 16 + 4, 4);
        std::memcpy(&f2, dz.data() + pos + i * 16 + 8, 4);
        uint32_t off = f1 & 0x00FFFFFF;
        uint32_t cmp = f2 & 0x00FFFFFF;
        uint32_t unc = f0 & 0x00FFFFFF;
        total_uncomp += unc;
        if (off + cmp > max_end) max_end = off + cmp;
        if (i == 0) { file0_off = off; file0_unc = unc; file0_cmp = cmp; }
    }
    std::printf("File 0: off=%u cmp=%u unc=%u\n", file0_off, file0_cmp, file0_unc);
    std::printf("Total uncomp=%u stream_end=%u\n", total_uncomp, max_end);

    // Our decompress() reads code from compressed[1..4] and data from compressed+5
    // So we must pass it the full stream from byte 0
    // It will produce output[0..N]. File 0 is at output[file0_off..file0_off+file0_unc)
    std::printf("\n--- Full stream decompress ---\n");
    auto full_result = resf2::dz::DzDecompressor::decompress(
        full_stream, max_end, total_uncomp);

    if (full_result.empty()) {
        std::printf("Full stream: EMPTY\n");
    } else {
        std::printf("Full stream: %zu bytes\n", full_result.size());
        std::printf("First 64 hex: ");
        for (size_t i = 0; i < std::min(full_result.size(), size_t(64)); ++i)
            std::printf("%02x ", full_result[i]);
        std::printf("\nFirst 80 text: ");
        for (size_t i = 0; i < std::min(full_result.size(), size_t(80)); ++i)
            std::putchar((full_result[i] >= 32 && full_result[i] < 127) ? (char)full_result[i] : '.');
        std::printf("\n");

        // Extract file 0 from offset
        if (full_result.size() >= file0_off + file0_unc) {
            std::printf("\nFile 0 (output[%u..%u]):\n", file0_off, file0_off + file0_unc - 1);
            std::printf("  Hex: ");
            for (uint32_t i = 0; i < file0_unc; ++i)
                std::printf("%02x ", full_result[file0_off + i]);
            std::printf("\n  Text: ");
            for (uint32_t i = 0; i < file0_unc; ++i) {
                uint8_t c = full_result[file0_off + i];
                std::putchar((c >= 32 && c < 127) ? (char)c : '.');
            }
            std::printf("\n");
        }
    }

    // Also try with code as Big Endian (stream[1..4] as BE)
    std::printf("\n--- Trial: code stream[0..3] as u32 LE ---\n");
    {
        // Make a copy of stream but put code in different position
        std::vector<uint8_t> alt(5 + max_end);
        alt[0] = full_stream[0]; // props
        alt[1] = full_stream[1];
        alt[2] = full_stream[2];
        alt[3] = full_stream[3];
        alt[4] = full_stream[4]; // code as LE
        std::memcpy(alt.data() + 5, full_stream + 5, max_end - 5);
        
        auto r = resf2::dz::DzDecompressor::decompress(alt.data(), max_end + 5, total_uncomp);
        if (r.empty()) {
            std::printf("  EMPTY\n");
        } else {
            std::printf("  %zu bytes, first=0x%02x\n", r.size(), r[0]);
            std::printf("  Text: ");
            for (size_t i = 0; i < std::min(r.size(), size_t(60)); ++i)
                std::putchar((r[i] >= 32 && r[i] < 127) ? (char)r[i] : '.');
            std::printf("\n");
        }
    }

    return 0;
}

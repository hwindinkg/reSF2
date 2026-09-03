#include "zstd_stream.hpp"

#include <stdexcept>
#include <string>

#include <zstd.h>

namespace sf2::data {

namespace {
constexpr std::size_t kStreamChunkSize = 64 * 1024;
} // namespace

std::vector<std::uint8_t> zstd_decompress(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        throw std::runtime_error("zstd_decompress: empty input");
    }

    const unsigned long long content_size = ZSTD_getFrameContentSize(data, size);
    if (content_size == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("zstd_decompress: not a valid zstd frame");
    }

    if (content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Frame header carries no content size — fall back to streaming decode.
        ZSTD_DStream* stream = ZSTD_createDStream();
        if (stream == nullptr) {
            throw std::runtime_error("zstd_decompress: ZSTD_createDStream failed");
        }
        ZSTD_initDStream(stream);

        std::vector<std::uint8_t> out;
        std::vector<std::uint8_t> chunk(kStreamChunkSize);
        ZSTD_inBuffer in{data, size, 0};
        while (in.pos < in.size) {
            ZSTD_outBuffer ob{chunk.data(), chunk.size(), 0};
            const std::size_t ret = ZSTD_decompressStream(stream, &ob, &in);
            if (ZSTD_isError(ret)) {
                ZSTD_freeDStream(stream);
                throw std::runtime_error(std::string("zstd_decompress: ") +
                                         ZSTD_getErrorName(ret));
            }
            out.insert(out.end(), chunk.begin(), chunk.begin() + ob.pos);
            if (ret == 0) {
                break; // frame fully decoded
            }
        }
        ZSTD_freeDStream(stream);
        return out;
    }

    std::vector<std::uint8_t> out(static_cast<std::size_t>(content_size));
    const std::size_t ret = ZSTD_decompress(out.data(), out.size(), data, size);
    if (ZSTD_isError(ret)) {
        throw std::runtime_error(std::string("zstd_decompress: ") + ZSTD_getErrorName(ret));
    }
    out.resize(ret);
    return out;
}

} // namespace sf2::data
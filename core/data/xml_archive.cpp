#include "xml_archive.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace sf2::data {
namespace {

// Bounds-checked little-endian reader over a byte buffer.
class reader {
public:
    reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::uint8_t u8() {
        require(1);
        return data_[pos_++];
    }

    std::uint16_t u16le() {
        require(2);
        const std::uint16_t v = static_cast<std::uint16_t>(data_[pos_]) |
                                static_cast<std::uint16_t>(data_[pos_ + 1]) << 8;
        pos_ += 2;
        return v;
    }

    std::uint32_t u24le() {
        require(3);
        const std::uint32_t v = static_cast<std::uint32_t>(data_[pos_]) |
                                static_cast<std::uint32_t>(data_[pos_ + 1]) << 8 |
                                static_cast<std::uint32_t>(data_[pos_ + 2]) << 16;
        pos_ += 3;
        return v;
    }

    std::string bytes(std::size_t n) {
        require(n);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

    const std::uint8_t* take(std::size_t n) {
        require(n);
        const std::uint8_t* p = data_ + pos_;
        pos_ += n;
        return p;
    }

private:
    void require(std::size_t n) {
        if (n > size_ - pos_) {
            throw std::runtime_error("xml_archive: truncated input");
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

} // namespace

std::vector<archive_entry> xml_archive_parse(const std::uint8_t* data, std::size_t size) {
    reader r(data, size);
    const std::uint16_t count = r.u16le();
    std::vector<archive_entry> entries;
    entries.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint8_t name_len = r.u8();
        archive_entry entry;
        entry.name = r.bytes(name_len);
        const std::uint32_t data_size = r.u24le();
        const std::uint8_t* p = r.take(data_size);
        entry.data.assign(p, p + data_size);
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::size_t xml_archive_extract(const std::vector<archive_entry>& entries,
                                const std::string& out_dir) {
    std::size_t written = 0;
    for (const archive_entry& entry : entries) {
        const std::filesystem::path out_path = std::filesystem::path(out_dir) / entry.name;
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("xml_archive_extract: cannot open " + out_path.string());
        }
        out.write(reinterpret_cast<const char*>(entry.data.data()),
                  static_cast<std::streamsize>(entry.data.size()));
        if (!out) {
            throw std::runtime_error("xml_archive_extract: write failed for " +
                                     out_path.string());
        }
        ++written;
    }
    return written;
}

} // namespace sf2::data
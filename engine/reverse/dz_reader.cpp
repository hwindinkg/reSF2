// engine/reverse/dz_reader.cpp
//
// DZ archive reader implementation.

#include "dz_reader.hpp"
#include "dz_decoder.hpp"
#include <cstdio>
#include <algorithm>

namespace resf2::dz {

// ========== DzArchive ==========

bool DzArchive::open(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto size = static_cast<size_t>(f.tellg());
    if (size < 16) return false;
    f.seekg(0);
    raw_data_.resize(size);
    f.read(reinterpret_cast<char*>(raw_data_.data()), static_cast<std::streamsize>(size));
    f.close();
    
    if (!parse()) {
        raw_data_.clear();
        return false;
    }
    
    opened_ = true;
    std::printf("[DZ] Opened %s: %zu files\n", path.c_str(), entries_.size());
    return true;
}

void DzArchive::close() {
    raw_data_.clear();
    entries_.clear();
    file_names_.clear();
    opened_ = false;
}

bool DzArchive::has_file(const std::string& name) const {
    return entries_.find(name) != entries_.end();
}

std::vector<std::byte> DzArchive::read_file(const std::string& name) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return {};

    const auto& entry = it->second;
    const auto* base = raw_data_.data() + entry.offset;
    // [ORIGINAL] comp_size is now stored in the entry (3rd u32 of the file table).
    // Previously it was inferred from the next file's offset, which happened to
    // equal comp_size ? but only because files are laid out contiguously. Using
    // the stored value is correct and avoids O(N) scans.
    uint32_t comp_size = entry.comp_size;
    if (comp_size == 0) {
        // Fallback: infer from next file's offset (legacy behavior)
        uint32_t next_offset = static_cast<uint32_t>(raw_data_.size());
        for (const auto& [n, e] : entries_) {
            if (e.offset > entry.offset && e.offset < next_offset) {
                next_offset = e.offset;
            }
        }
        comp_size = next_offset - entry.offset;
    }

    switch (entry.comp_type) {
        case 1:  // Copy (no compression)
            return std::vector<std::byte>(base, base + entry.uncomp_size);

        case 2:  // zlib
            return decompress_zlib(base, comp_size);

        case 8:  // gzip
            return decompress_gzip(base, comp_size);

        case 4:  // DZ custom — handled via build-time extraction + runtime fallback
            return {};

        default:
            std::fprintf(stderr, "[DZ] Unknown compression type %u for %s\n",
                         entry.comp_type, name.c_str());
            return {};
    }
}

bool DzArchive::parse() {
    const auto* data = raw_data_.data();
    auto size = raw_data_.size();
    
    if (size < 9) return false;
    if (std::memcmp(data, "DTRZ", 4) != 0) return false;
    
    // Read header
    uint16_t num_files = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
    uint16_t num_dirs = static_cast<uint16_t>(data[6]) | (static_cast<uint16_t>(data[7]) << 8);
    // data[8] = version (0)
    
    size_t pos = 9;
    
    // Read file names
    std::vector<std::string> filenames;
    filenames.reserve(num_files);
    for (uint16_t i = 0; i < num_files; ++i) {
        if (pos >= size) return false;
        size_t end = pos;
        while (end < size && data[end] != std::byte{0}) ++end;
        filenames.emplace_back(reinterpret_cast<const char*>(data + pos), end - pos);
        pos = end + 1;
    }
    
    // Read folder names
    uint16_t actual_num_dirs = num_dirs > 0 ? num_dirs : 0;
    std::vector<std::string> folders;
    folders.push_back("");  // root
    for (uint16_t i = 0; i < actual_num_dirs; ++i) {
        if (pos >= size) return false;
        size_t end = pos;
        while (end < size && data[end] != std::byte{0}) ++end;
        folders.emplace_back(reinterpret_cast<const char*>(data + pos), end - pos);
        pos = end + 1;
    }
    
    // Skip file attribute table (num_files * 6 bytes)
    // Each: folder_idx u16 + file_number u16 + flags u16
    std::vector<uint16_t> file_folder_idx(num_files);
    for (uint16_t i = 0; i < num_files; ++i) {
        if (pos + 6 > size) return false;
        file_folder_idx[i] = static_cast<uint16_t>(data[pos]) |
                             (static_cast<uint16_t>(data[pos+1]) << 8);
        pos += 6;
    }
    
    // Skip lengths trailer (3 bytes: count u16 BE + padding)
    pos += 3;
    
    // File table: num_files * 16 bytes, each field is raw u32 LE.
    //   f0: absolute file offset (where compressed data starts)
    //   f1: compressed size
    //   f2: uncompressed size
    //   f3: compression type (4 = DZ custom)

    for (uint16_t i = 0; i < num_files; ++i) {
        if (pos + 16 > size) return false;

        uint32_t f0, f1, f2, f3;
        std::memcpy(&f0, data + pos, 4);
        std::memcpy(&f1, data + pos + 4, 4);
        std::memcpy(&f2, data + pos + 8, 4);
        std::memcpy(&f3, data + pos + 12, 4);
        pos += 16;

        DzFileEntry entry;
        entry.name = filenames[i];
        entry.offset = f0;
        entry.comp_size = f1;
        entry.uncomp_size = f2;
        entry.comp_type = f3;

        // Build folder path
        if (i < file_folder_idx.size() && file_folder_idx[i] < folders.size()) {
            entry.folder = folders[file_folder_idx[i]];
        }

        // Store with both bare name and folder/name
        entries_[entry.name] = entry;
        if (!entry.folder.empty()) {
            entries_[entry.folder + "/" + entry.name] = entry;
        }

        file_names_.push_back(entry.name);
    }

    return true;
}

std::vector<std::byte> DzArchive::decompress_gzip(const std::byte* data, size_t size) {
    // Some .dz archives store gzip with header but NO trailer (CRC + size).
    // Standard gzip decompression (wbits=31) fails because it expects
    // the 8-byte trailer at the end. We use raw deflate (wbits=-15)
    // and skip the 10-byte gzip header.
    //
    // Also handle full gzip (with trailer) if the data has it.
    bool has_gzip_hdr = (size >= 2 && data[0] == std::byte{0x1f} && data[1] == std::byte{0x8b});
    
    z_stream strm = {};
    const Bytef* in;
    uInt avail;
    
    if (has_gzip_hdr && size >= 10) {
        // Skip the 10-byte gzip header, decompress as raw deflate
        in = reinterpret_cast<const Bytef*>(data) + 10;
        avail = static_cast<uInt>(size - 10);
    } else if (has_gzip_hdr) {
        // Header present but too small ? raw deflate from start
        in = reinterpret_cast<const Bytef*>(data);
        avail = static_cast<uInt>(size);
    } else {
        in = reinterpret_cast<const Bytef*>(data);
        avail = static_cast<uInt>(size);
    }
    strm.next_in = const_cast<Bytef*>(in);
    strm.avail_in = avail;
    
    if (inflateInit2(&strm, -15) != Z_OK) return {};
    
    std::vector<std::byte> result;
    result.resize(65536);
    
    int ret;
    do {
        strm.next_out = reinterpret_cast<Bytef*>(result.data() + strm.total_out);
        strm.avail_out = static_cast<uInt>(result.size() - strm.total_out);
        
        ret = inflate(&strm, Z_NO_FLUSH);
        
        if (ret == Z_BUF_ERROR || strm.total_out >= result.size()) {
            result.resize(result.size() * 2);
            continue;
        }
    } while (ret == Z_OK);
    
    inflateEnd(&strm);
    
    if (ret != Z_STREAM_END && ret != Z_OK) return {};
    
    result.resize(strm.total_out);
    return result;
}

std::vector<std::byte> DzArchive::decompress_zlib(const std::byte* data, size_t size) {
    // Try standard zlib first, fall back to raw deflate
    z_stream strm = {};
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    strm.avail_in = static_cast<uInt>(size);
    
    if (inflateInit(&strm) != Z_OK) return {};
    
    std::vector<std::byte> result;
    result.resize(65536);
    
    int ret;
    do {
        strm.next_out = reinterpret_cast<Bytef*>(result.data() + strm.total_out);
        strm.avail_out = static_cast<uInt>(result.size() - strm.total_out);
        
        ret = inflate(&strm, Z_NO_FLUSH);
        
        if (ret == Z_BUF_ERROR || strm.total_out >= result.size()) {
            result.resize(result.size() * 2);
            continue;
        }
    } while (ret == Z_OK);
    
    inflateEnd(&strm);
    
    if (ret == Z_STREAM_END || (ret == Z_OK && strm.total_out > 0)) {
        result.resize(strm.total_out);
        return result;
    }
    
    // Fall back to raw deflate
    inflateEnd(&strm);
    strm = {};
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    strm.avail_in = static_cast<uInt>(size);
    if (inflateInit2(&strm, -15) != Z_OK) return {};
    result.clear();
    result.resize(65536);
    do {
        strm.next_out = reinterpret_cast<Bytef*>(result.data() + strm.total_out);
        strm.avail_out = static_cast<uInt>(result.size() - strm.total_out);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_BUF_ERROR || strm.total_out >= result.size()) {
            result.resize(result.size() * 2);
            continue;
        }
    } while (ret == Z_OK);
    inflateEnd(&strm);
    if (ret != Z_STREAM_END && ret != Z_OK) return {};
    result.resize(strm.total_out);
    return result;
}

// ========== DzRegistry ==========

DzRegistry& DzRegistry::instance() {
    static DzRegistry registry;
    return registry;
}

bool DzRegistry::open_archive(const std::string& path) {
    auto archive = std::make_unique<DzArchive>();
    if (!archive->open(path)) return false;
    archives_.push_back(std::move(archive));
    return true;
}

std::vector<std::byte> DzRegistry::read_file(const std::string& name) {
    // First try to read from archives
    for (auto& archive : archives_) {
        if (archive->has_file(name)) {
            auto data = archive->read_file(name);
            if (!data.empty()) return data;
        }
    }
    // If not found or decompression failed, try fallback directories
    return read_from_fallback(name);
}

bool DzRegistry::has_file(const std::string& name) {
    for (auto& archive : archives_) {
        if (archive->has_file(name)) return true;
    }
    // Also check fallback directories
    for (const auto& dir : fallback_dirs_) {
        std::filesystem::path p = std::filesystem::path(dir) / name;
        if (std::filesystem::exists(p)) return true;
        // Also check with common subpaths
        p = std::filesystem::path(dir) / "files" / name;
        if (std::filesystem::exists(p)) return true;
        p = std::filesystem::path(dir) / "animations" / name;
        if (std::filesystem::exists(p)) return true;
        p = std::filesystem::path(dir) / "animations" / "binary" / name;
        if (std::filesystem::exists(p)) return true;
    }
    return false;
}

std::vector<std::byte> DzRegistry::read_from_fallback(const std::string& name) {
    for (const auto& dir : fallback_dirs_) {
        // Try several path patterns:
        // 1. <dir>/<name>
        // 2. <dir>/files/<name>  (for files.dz extracted contents)
        // 3. <dir>/animations/<name>  (for animations.dz XML files)
        // 4. <dir>/animations/binary/<name>  (for .bin files)
        // 5. <dir>/files/assets/<name>  (deeper nesting)
        std::vector<std::string> subpaths = {
            name,
            "files/" + name,
            "animations/" + name,
            "animations/binary/" + name,
            "files/assets/" + name,
            "assets/files/" + name,
            "assets/animations/" + name,
            "assets/animations/binary/" + name,
        };
        for (const auto& sub : subpaths) {
            std::filesystem::path p = std::filesystem::path(dir) / sub;
            if (std::filesystem::exists(p) && std::filesystem::is_regular_file(p)) {
                std::ifstream f(p, std::ios::binary | std::ios::ate);
                if (!f) continue;
                auto size = static_cast<size_t>(f.tellg());
                f.seekg(0);
                std::vector<std::byte> data(size);
                f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
                if (!data.empty()) {
                    std::printf("[DZ] Fallback: %s -> %s (%zu bytes)\n",
                                name.c_str(), p.string().c_str(), data.size());
                    return data;
                }
            }
        }
    }
    return {};
}

}  // namespace resf2::dz

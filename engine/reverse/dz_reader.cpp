// engine/reverse/dz_reader.cpp
//
// DZ archive reader implementation. See dz_reader.hpp for the container
// layout and where each field was recovered from.

#include "dz_reader.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <zlib.h>

namespace resf2::dz {
namespace {

uint16_t rd_u16(const std::byte* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t rd_u32(const std::byte* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

// Normalise the backslash-separated directories the archive stores.
std::string to_slash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

}  // namespace

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

    path_ = path;
    if (!parse()) {
        close();
        return false;
    }

    opened_ = true;
    std::printf("[DZ] Opened %s: %zu files, %zu blocks\n",
                path.c_str(), entries_.size(), blocks_.size());
    return true;
}

void DzArchive::close() {
    raw_data_.clear();
    blocks_.clear();
    entries_.clear();
    index_.clear();
    file_names_.clear();
    has_dz_params_ = false;
    opened_ = false;
}

bool DzArchive::has_file(const std::string& name) const {
    return index_.find(name) != index_.end();
}

bool DzArchive::parse() {
    const auto* data = raw_data_.data();
    const size_t size = raw_data_.size();

    if (size < 9) return false;
    if (std::memcmp(data, "DTRZ", 4) != 0) return false;
    if (data[8] != std::byte{0}) return false;  // [ORIGINAL] version must be 0

    const uint16_t num_files = rd_u16(data + 4);
    const uint16_t num_dirs = rd_u16(data + 6);
    size_t pos = 9;

    auto read_cstr = [&](std::string& out) -> bool {
        size_t end = pos;
        while (end < size && data[end] != std::byte{0}) ++end;
        if (end >= size) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), end - pos);
        pos = end + 1;
        return true;
    };

    std::vector<std::string> filenames(num_files);
    for (uint16_t i = 0; i < num_files; ++i)
        if (!read_cstr(filenames[i])) return false;

    // [ORIGINAL] index 0 is the implicit root, only num_dirs - 1 names are stored.
    std::vector<std::string> folders;
    folders.emplace_back("");
    for (uint16_t i = 1; i < num_dirs; ++i) {
        std::string d;
        if (!read_cstr(d)) return false;
        folders.push_back(to_slash(std::move(d)));
    }

    // [ORIGINAL] one u16 chain per file, terminated by 0xFFFF.
    std::vector<std::vector<uint16_t>> chains(num_files);
    for (uint16_t i = 0; i < num_files; ++i) {
        for (;;) {
            if (pos + 2 > size) return false;
            const uint16_t v = rd_u16(data + pos);
            pos += 2;
            if (v == 0xFFFF) break;
            chains[i].push_back(v);
        }
        if (chains[i].empty()) return false;  // needs at least a directory index
    }

    // [ORIGINAL] Derbh::open (FUN_102ca66b) reads this 4-byte sub-header.
    if (pos + 4 > size) return false;
    const uint16_t num_volumes = rd_u16(data + pos);
    const uint16_t num_blocks = rd_u16(data + pos + 2);
    pos += 4;
    if (num_blocks == 0) return false;

    if (pos + static_cast<size_t>(num_blocks) * 16 > size) return false;
    blocks_.resize(num_blocks);
    for (uint16_t i = 0; i < num_blocks; ++i) {
        const std::byte* e = data + pos + static_cast<size_t>(i) * 16;
        blocks_[i] = DzBlock{rd_u32(e), rd_u32(e + 4), rd_u32(e + 8), rd_u32(e + 12)};
    }
    pos += static_cast<size_t>(num_blocks) * 16;

    // [ORIGINAL] additional volume file names for multi-part archives.
    for (uint16_t i = 1; i < num_volumes; ++i) {
        std::string vol;
        if (!read_cstr(vol)) return false;
    }
    if (num_volumes > 1) {
        std::fprintf(stderr, "[DZ] %s: multi-volume archive (%u volumes) is not supported\n",
                     path_.c_str(), num_volumes);
        return false;
    }

    // [ORIGINAL] Derbh::open then calls coder->init() for every coder whose
    // mask intersects the OR of all block flags, lowest mask first. Only the
    // DZ coder consumes bytes here (10, FUN_00409d90).
    uint32_t all_flags = 0;
    for (const auto& b : blocks_) all_flags |= b.flags;
    if (all_flags & kCoderDz) {
        if (pos + DzCoderParams::kHeaderSize > size) return false;
        if (!DzCoderParams::parse(reinterpret_cast<const uint8_t*>(data + pos),
                                  size - pos, dz_params_)) {
            std::fprintf(stderr, "[DZ] %s: invalid DZ coder header\n", path_.c_str());
            return false;
        }
        has_dz_params_ = true;
        pos += DzCoderParams::kHeaderSize;
    }

    entries_.reserve(num_files);
    for (uint16_t i = 0; i < num_files; ++i) {
        DzFileEntry entry;
        entry.name = filenames[i];
        const uint16_t dir_idx = chains[i][0];
        if (dir_idx >= folders.size()) return false;
        entry.folder = folders[dir_idx];
        entry.blocks.assign(chains[i].begin() + 1, chains[i].end());
        for (uint16_t b : entry.blocks) {
            if (b >= blocks_.size()) return false;
            entry.uncomp_size += blocks_[b].uncomp_size;
        }

        const size_t idx = entries_.size();
        entries_.push_back(std::move(entry));

        // Both the bare name and the full folder-qualified path resolve.
        index_.emplace(entries_[idx].name, idx);
        if (!entries_[idx].folder.empty())
            index_.emplace(entries_[idx].folder + "/" + entries_[idx].name, idx);
        file_names_.push_back(entries_[idx].name);
    }

    return true;
}

bool DzArchive::decode_block(const DzBlock& b, std::vector<std::byte>& out) const {
    if (static_cast<size_t>(b.offset) > raw_data_.size()) return false;
    const std::byte* base = raw_data_.data() + b.offset;
    const size_t avail = raw_data_.size() - b.offset;

    // [ORIGINAL] Copy Coder — data stored verbatim.
    if (b.flags & kCoderCopy) {
        if (b.uncomp_size > avail) return false;
        out.insert(out.end(), base, base + b.uncomp_size);
        return true;
    }

    // [ORIGINAL] DZ Coder.
    if (b.flags & kCoderDz) {
        if (!has_dz_params_) return false;
        const size_t comp = (b.comp_size != 0 && b.comp_size <= avail) ? b.comp_size : avail;
        auto decoded = dz_decode_block(dz_params_,
                                       reinterpret_cast<const uint8_t*>(base), comp,
                                       b.uncomp_size);
        if (decoded.size() != b.uncomp_size) return false;
        const auto* p = reinterpret_cast<const std::byte*>(decoded.data());
        out.insert(out.end(), p, p + decoded.size());
        return true;
    }

    // [ORIGINAL] ZLib Coder. comp_size is not maintained for this coder (it
    // mirrors uncomp_size in every shipped archive), so the deflate stream is
    // read to its own end marker instead.
    if (b.flags & kCoderZlib) {
        auto decoded = decompress_gzip(base, avail, b.uncomp_size);
        if (decoded.size() != b.uncomp_size) return false;
        out.insert(out.end(), decoded.begin(), decoded.end());
        return true;
    }

    std::fprintf(stderr, "[DZ] unsupported coder mask 0x%x (bzip/lzma/zero-replace)\n",
                 b.flags);
    return false;
}

std::vector<std::byte> DzArchive::read_file(const std::string& name) const {
    auto it = index_.find(name);
    if (it == index_.end()) return {};
    const DzFileEntry& entry = entries_[it->second];

    std::vector<std::byte> out;
    out.reserve(entry.uncomp_size);
    for (uint16_t bi : entry.blocks) {
        if (!decode_block(blocks_[bi], out)) {
            std::fprintf(stderr, "[DZ] failed to decode block %u of %s\n", bi, name.c_str());
            return {};
        }
    }
    return out;
}

std::vector<std::byte> DzArchive::decompress_gzip(const std::byte* data, size_t size,
                                                  size_t uncomp_size) {
    // [ORIGINAL] The ZLib Coder writes a bare 10-byte gzip header followed by
    // a raw deflate stream and no trailer, so inflate with wbits = -15.
    if (size < 10) return {};
    const bool gz = (data[0] == std::byte{0x1f} && data[1] == std::byte{0x8b});
    const size_t skip = gz ? 10 : 0;

    z_stream strm{};
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data + skip));
    strm.avail_in = static_cast<uInt>(size - skip);
    if (inflateInit2(&strm, -15) != Z_OK) return {};

    std::vector<std::byte> result(uncomp_size ? uncomp_size : 65536);
    int ret = Z_OK;
    for (;;) {
        strm.next_out = reinterpret_cast<Bytef*>(result.data() + strm.total_out);
        strm.avail_out = static_cast<uInt>(result.size() - strm.total_out);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK && ret != Z_BUF_ERROR) break;
        if (strm.total_out >= result.size())
            result.resize(result.size() * 2);
        else if (ret == Z_BUF_ERROR)
            break;  // out of input, stream truncated
    }
    inflateEnd(&strm);
    if (ret != Z_STREAM_END) return {};
    result.resize(strm.total_out);
    return result;
}

// ========== DzRegistry ==========

DzRegistry& DzRegistry::instance() {
    static DzRegistry registry;
    return registry;
}

bool DzRegistry::open_archive(const std::string& path) {
    // init_location() runs on every location change; re-opening an archive
    // each time would leak a full copy of the file.
    std::error_code ec;
    const auto canon = std::filesystem::weakly_canonical(path, ec).string();
    const std::string& key = ec ? path : canon;
    if (std::find(archive_paths_.begin(), archive_paths_.end(), key) != archive_paths_.end())
        return true;

    auto archive = std::make_unique<DzArchive>();
    if (!archive->open(path)) return false;
    archives_.push_back(std::move(archive));
    archive_paths_.push_back(key);
    return true;
}

size_t DzRegistry::open_archives_in(const std::string& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return 0;
    // Sorted so the mount order is deterministic across platforms.
    std::vector<std::filesystem::path> found;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".dz") found.push_back(e.path());
    }
    std::sort(found.begin(), found.end());
    size_t n = 0;
    for (const auto& p : found)
        if (open_archive(p.string())) ++n;
    return n;
}

std::vector<std::byte> DzRegistry::read_file(const std::string& name) {
    for (auto& archive : archives_) {
        if (archive->has_file(name)) {
            auto data = archive->read_file(name);
            if (!data.empty()) return data;
        }
    }
    return read_from_fallback(name);
}

bool DzRegistry::has_file(const std::string& name) {
    for (auto& archive : archives_) {
        if (archive->has_file(name)) return true;
    }
    for (const auto& dir : fallback_dirs_) {
        const std::filesystem::path p = std::filesystem::path(dir) / name;
        if (std::filesystem::exists(p)) return true;
    }
    return false;
}

std::vector<std::byte> DzRegistry::read_from_fallback(const std::string& name) {
    // Plain <dir>/<name> only. The previous implementation probed a list of
    // guessed sub-paths ("files/", "animations/binary/", ...) to work around
    // the DZ coder being unimplemented; that is no longer needed and hid which
    // asset tree a file actually came from.
    for (const auto& dir : fallback_dirs_) {
        const std::filesystem::path p = std::filesystem::path(dir) / name;
        if (!std::filesystem::exists(p) || !std::filesystem::is_regular_file(p)) continue;
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f) continue;
        auto size = static_cast<size_t>(f.tellg());
        f.seekg(0);
        std::vector<std::byte> data(size);
        f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        if (!data.empty()) {
            std::printf("[DZ] loose file: %s -> %s (%zu bytes)\n",
                        name.c_str(), p.string().c_str(), data.size());
            return data;
        }
    }
    return {};
}

}  // namespace resf2::dz

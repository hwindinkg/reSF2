// engine/reverse/dz_reader.hpp
//
// DZ (DTRZ) archive reader for the Marmalade SDK "derbh" format.
// Reads .dz archives at runtime without extracting to disk.
//
// [ORIGINAL] Container layout recovered from DzipFile::open
// (ShadowFight2.s86 FUN_102c9778) and Derbh::open (FUN_102ca66b), and
// cross-checked byte-for-byte against dzip.exe (the Marmalade "Derbh
// commpress tool"):
//
//   'DTRZ'                                   4 bytes
//   u16 num_files
//   u16 num_dirs                             (index 0 is the implicit root)
//   u8  version                              (must be 0)
//   num_files     NUL-terminated file names
//   num_dirs - 1  NUL-terminated directory paths (stored full, e.g. "assets\anim")
//   num_files chains of u16 terminated by 0xFFFF:
//       chain[0]   = directory index
//       chain[1..] = indices into the block table; a file is the
//                    concatenation of its blocks
//   u16 num_volumes
//   u16 num_blocks
//   num_blocks x 16-byte block records: {u32 offset, u32 comp_size,
//                                        u32 uncomp_size, u32 flags}
//   num_volumes - 1 NUL-terminated volume file names (multi-part archives)
//   per-coder headers, in coder registration order, for every coder whose
//   mask appears in the OR of all block flags
//
// Coder masks (dzip.exe FUN_00417aa0):
//   0x004 "DZ Coder"    - Marmalade's adaptive range coder  (files.dz)
//   0x008 "ZLib Coder"  - gzip header + raw deflate         (animations.dz, ZONE_*.dz)
//   0x010 "BZip Coder"
//   0x080 "Zero replace"
//   0x100 "Copy Coder (no compression)"
//   0x200 "LZMA Coder"
//
// Only the DZ coder writes a header (10 bytes, see DzCoderParams).

#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dz_coder.hpp"

namespace resf2::dz {

// Coder mask bits as registered by the original (dzip.exe FUN_00417aa0).
enum : uint32_t {
    kCoderDz = 0x004,
    kCoderZlib = 0x008,
    kCoderBzip = 0x010,
    kCoderZeroReplace = 0x080,
    kCoderCopy = 0x100,
    kCoderLzma = 0x200,
};

// One 16-byte record of the block table.
struct DzBlock {
    uint32_t offset = 0;       // absolute offset of the compressed block in the .dz
    uint32_t comp_size = 0;    // compressed size (only meaningful for the DZ coder)
    uint32_t uncomp_size = 0;  // decompressed size
    uint32_t flags = 0;        // coder mask, see kCoder*
};

struct DzFileEntry {
    std::string name;
    std::string folder;               // "" for root, otherwise e.g. "assets/animations"
    std::vector<uint16_t> blocks;     // block-table indices, in order
    uint32_t uncomp_size = 0;         // sum over blocks
};

class DzArchive {
public:
    bool open(const std::string& path);
    void close();

    bool has_file(const std::string& name) const;

    // Read and decompress a file. Returns an empty vector on failure.
    std::vector<std::byte> read_file(const std::string& name) const;

    const std::vector<std::string>& file_names() const { return file_names_; }

private:
    bool parse();

    // Decode one block according to its coder mask.
    bool decode_block(const DzBlock& b, std::vector<std::byte>& out) const;

    // [ORIGINAL] ZLib Coder: gzip header, raw deflate payload, no trailer.
    static std::vector<std::byte> decompress_gzip(const std::byte* data, size_t size,
                                                  size_t uncomp_size);

    std::vector<std::byte> raw_data_;
    std::vector<DzBlock> blocks_;
    std::vector<DzFileEntry> entries_;
    std::unordered_map<std::string, size_t> index_;  // name / folder+name -> entries_ idx
    std::vector<std::string> file_names_;
    DzCoderParams dz_params_{};
    bool has_dz_params_ = false;
    bool opened_ = false;
    std::string path_;
};

// Global registry of all open DZ archives.
class DzRegistry {
public:
    static DzRegistry& instance();

    // Opening the same path twice is a no-op and returns true.
    bool open_archive(const std::string& path);

    // Open every *.dz directly inside `dir`. Returns how many were opened.
    size_t open_archives_in(const std::string& dir);

    // Try to read a file from any open archive; falls back to the registered
    // extracted-asset directories when the archive lookup misses.
    std::vector<std::byte> read_file(const std::string& name);

    bool has_file(const std::string& name);

    // Register a directory searched for loose files. The original ships part
    // of its asset tree outside the archives (e.g. assets/1536/locations/dojo
    // in the APK), so this is a real lookup path, not a workaround — but it is
    // a plain <dir>/<name> join, no path guessing.
    void add_fallback_dir(const std::string& path) { fallback_dirs_.push_back(path); }

private:
    std::vector<std::unique_ptr<DzArchive>> archives_;
    std::vector<std::string> archive_paths_;
    std::vector<std::string> fallback_dirs_;

    std::vector<std::byte> read_from_fallback(const std::string& name);
};

}  // namespace resf2::dz

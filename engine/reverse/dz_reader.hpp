// engine/reverse/dz_reader.hpp
//
// DZ (DTRZ) archive reader for Marmalade SDK derbh format.
// Reads .dz archives at runtime without extracting to disk.
//
// Two compression types found in SF2:
//   type=4: Marmalade DZ custom compression (files.dz — models, XML configs)
//   type=8: GZIP compression (animations.dz — .bin animation files)
//
// Type 8 (GZIP) is fully supported using zlib's gzip decompression.
// Type 4 (DZ custom) is not yet decompressed — we use the file table
// to find files, and if they're not compressed (type=1 copy), we read
// them directly. For type=4, we fall back to searching the filesystem.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <zlib.h>

namespace resf2::dz {

struct DzFileEntry {
    std::string name;
    uint32_t offset;      // absolute offset in .dz file
    uint32_t uncomp_size; // decompressed size
    uint32_t comp_type;   // 1=copy, 2=zlib, 4=DZ custom, 8=gzip
    std::string folder;   // folder path
};

class DzArchive {
public:
    bool open(const std::string& path);
    void close();
    
    // Check if a file exists in this archive
    bool has_file(const std::string& name) const;
    
    // Read a file from the archive. Returns empty vector on failure.
    std::vector<std::byte> read_file(const std::string& name) const;
    
    // Get list of all file names
    const std::vector<std::string>& file_names() const { return file_names_; }
    
private:
    std::vector<std::byte> raw_data_;
    std::unordered_map<std::string, DzFileEntry> entries_;
    std::vector<std::string> file_names_;
    bool opened_ = false;
    
    // Parse the DTRZ container format
    bool parse();
    
    // Decompress GZIP data (type=8)
    static std::vector<std::byte> decompress_gzip(const std::byte* data, size_t size);
    
    // Decompress zlib data (type=2)
    static std::vector<std::byte> decompress_zlib(const std::byte* data, size_t size);
};

// Global registry of all open DZ archives
class DzRegistry {
public:
    static DzRegistry& instance();
    
    // Open a .dz file and register it
    bool open_archive(const std::string& path);
    
    // Try to read a file from any open archive
    // Returns empty vector if not found
    std::vector<std::byte> read_file(const std::string& name);
    
    // Check if any archive has this file
    bool has_file(const std::string& name);
    
private:
    std::vector<std::unique_ptr<DzArchive>> archives_;
};

}  // namespace resf2::dz

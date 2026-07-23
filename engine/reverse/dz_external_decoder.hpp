#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace resf2::dz {

// External decoder that calls dzip.exe as a subprocess.
// Caches extracted files per archive so each archive is only extracted once.
class DzExternalDecoder {
public:
    DzExternalDecoder();

    // Decompress a single file from the archive.
    // dz_path: path to the .dz archive file on disk
    // file_name: name of the file to extract (as stored in the archive)
    // Returns decompressed data, or empty on failure.
    std::vector<std::byte> decompress(const std::string& dz_path, const std::string& file_name);

private:
    // Extract the entire archive to a temp directory and cache all files
    bool extract_archive(const std::string& dz_path);

    std::string dzip_path_;
    // Cache: archive_path -> (filename -> data)
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::byte>>> cache_;
};

}  // namespace resf2::dz

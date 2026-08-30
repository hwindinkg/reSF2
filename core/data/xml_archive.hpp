#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sf2::data {

// One file inside the xml.dat container.
struct archive_entry {
    std::string name;  // archive-relative path, e.g. "res/moves.xml"
    std::vector<std::uint8_t> data;
};

// Parses the xml.dat container format (see core/data/README.md):
//   u16 LE  file count
//   per file:
//     u8   name length
//     ...  name bytes (UTF-8)
//     u24  data size (3 bytes, little-endian)
//     ...  data bytes
// Throws std::runtime_error on malformed/truncated input.
std::vector<archive_entry> xml_archive_parse(const std::uint8_t* data, std::size_t size);

// Writes every entry under `out_dir`, preserving the archive-relative path.
// Creates directories as needed. Returns the number of files written.
std::size_t xml_archive_extract(const std::vector<archive_entry>& entries,
                                const std::string& out_dir);

} // namespace sf2::data
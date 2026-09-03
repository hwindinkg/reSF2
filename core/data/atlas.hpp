#pragma once

// TexturePacker atlas JSON parser.
//
// Format (verified against res/locations/arena/arena.ca2949ef.json and
// res/fight/ui.4c9e126b.json):
//   {"meta":{"scale":"1","size":{"w":2035,"h":2043},"related_multi_packs":[...]},
//    "frames":[
//      {"rotated":false,"sourceSize":{"h":145,"w":1936},
//       "frame":{"h":145,"x":1,"y":1760,"w":1936},
//       "trimmed":false,
//       "spriteSourceSize":{"h":145,"x":0,"y":0,"w":1936},
//       "filename":"_0007_arena"}, ...]}
//
// Only the fields needed to place a frame on the atlas texture are kept.

#include <cstdint>
#include <string>
#include <vector>

namespace sf2::data {

struct atlas_frame {
    std::string name;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool rotated = false;
    // Original (untrimmed) size and the offset into the trimmed rect, used
    // when a frame was trimmed by the packer.
    int source_w = 0;
    int source_h = 0;
    int offset_x = 0;
    int offset_y = 0;
    bool trimmed = false;
};

struct atlas {
    int w = 0;  // meta.size.w — the atlas texture size
    int h = 0;
    std::vector<atlas_frame> frames;
};

// Parses a TexturePacker JSON document (UTF-8 bytes). Throws std::runtime_error
// on malformed input.
atlas atlas_parse(const std::uint8_t* data, std::size_t size);

} // namespace sf2::data

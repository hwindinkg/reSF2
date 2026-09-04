#pragma once

// SF2User envelope framing (FLOW_STATIC section 3.1 + R7, JS L70-73/L2333).
// Export (`Aa.Dpb`): `"SF2" + base64(ke+yna(users) + ke+yna(packs) +
// $p(H1) + $p(VF))` — length-prefixed zstd frames, no separators.
// `ke(v)` = u32 (cP unset in the bundle -> falsy -> LE); `yna(xml)` =
// ke(compressed-len) + zstd bytes (`kb.f3`, level default); `$p` = one
// flag byte each. Import (`Aa.Ddb`): strip `SF2`, base64-decode, then
// `Yt(ti())` per frame + `ea()` per flag.
// NOTE: `Dpb` writes `ke(string.length)` while `Aa.save` writes
// `ke(compressed.length)`; the reader (`Yt(ti())`) consumes the framed
// length, so the round-trippable form uses the COMPRESSED length (what
// `save` writes). The `Dpb` string-length is a latent game inconsistency,
// documented, not mirrored.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "codec.hpp"
#include "zstd_stream.hpp"

namespace sf2::data {

// `ke(v)`: u32 little-endian.
inline void envelope_ke(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

// `yna(xml)`: one length-prefixed zstd frame.
inline void envelope_yna(std::vector<std::uint8_t>& out, const std::string& xml) {
    const std::vector<std::uint8_t> c = zstd_compress(
        reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
    envelope_ke(out, static_cast<std::uint32_t>(c.size()));
    out.insert(out.end(), c.begin(), c.end());
}

inline std::string envelope_export(const std::string& users_xml,
                                   const std::string& packs_xml, bool h1, bool vf) {
    std::vector<std::uint8_t> out;
    envelope_yna(out, users_xml);
    envelope_yna(out, packs_xml);
    out.push_back(h1 ? 1 : 0);
    out.push_back(vf ? 1 : 0);
    return "SF2" + base64_encode(out);
}

// Reads one u32LE at `pos` (advances). Throws on truncation.
inline std::uint32_t envelope_ti(const std::vector<std::uint8_t>& raw, std::size_t& pos) {
    if (pos + 4 > raw.size()) throw std::runtime_error("sf2 frame truncated");
    const std::uint32_t v =
        static_cast<std::uint32_t>(raw[pos]) |
        (static_cast<std::uint32_t>(raw[pos + 1]) << 8) |
        (static_cast<std::uint32_t>(raw[pos + 2]) << 16) |
        (static_cast<std::uint32_t>(raw[pos + 3]) << 24);
    pos += 4;
    return v;
}

// Decodes the users frame (first) from raw envelope bytes (post-base64).
inline std::string envelope_decode_users(const std::vector<std::uint8_t>& raw) {
    std::size_t pos = 0;
    const std::uint32_t len = envelope_ti(raw, pos);
    if (pos + len > raw.size()) throw std::runtime_error("sf2 frame overrun");
    const std::vector<std::uint8_t> xml =
        zstd_decompress(raw.data() + pos, static_cast<std::size_t>(len));
    return std::string(xml.begin(), xml.end());
}

}  // namespace sf2::data

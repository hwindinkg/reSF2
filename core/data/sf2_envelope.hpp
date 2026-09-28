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

// Reads one length-prefixed zstd frame (`Yt(ti())`), advancing `pos`.
inline std::string envelope_decode_frame(const std::vector<std::uint8_t>& raw,
                                         std::size_t& pos) {
    const std::uint32_t len = envelope_ti(raw, pos);
    if (pos + len > raw.size()) throw std::runtime_error("sf2 frame overrun");
    const std::vector<std::uint8_t> xml =
        zstd_decompress(raw.data() + pos, static_cast<std::size_t>(len));
    pos += len;
    return std::string(xml.begin(), xml.end());
}

// Decodes the users frame (first) from raw envelope bytes (post-base64).
inline std::string envelope_decode_users(const std::vector<std::uint8_t>& raw) {
    std::size_t pos = 0;
    return envelope_decode_frame(raw, pos);
}

// The full `.sf2` payload (`Aa.Ddb` L35024): users frame, packs frame, then
// the H1/VF flag bytes (`Aa.flags.H1 = e==1`, `Aa.flags.VF = e==1`).
struct EnvelopePackage {
    std::string users;
    std::string packs;
    bool h1 = false;
    bool vf = false;
};

inline EnvelopePackage envelope_import(const std::vector<std::uint8_t>& raw) {
    EnvelopePackage p;
    std::size_t pos = 0;
    p.users = envelope_decode_frame(raw, pos);
    p.packs = envelope_decode_frame(raw, pos);
    if (pos + 2 > raw.size()) throw std::runtime_error("sf2 flags truncated");
    p.h1 = raw[pos++] == 1;
    p.vf = raw[pos++] == 1;
    return p;
}

// `Aa.save` (L34817) STORAGE payload: `ke(compressed-len) + zstd(xml)` (the
// `.sf2` file adds the `"SF2"` prefix + packs/flags; `SF2User` does not).
inline std::string envelope_encode_storage(const std::string& xml) {
    std::vector<std::uint8_t> out;
    envelope_yna(out, xml);
    return base64_encode(out);
}

}  // namespace sf2::data

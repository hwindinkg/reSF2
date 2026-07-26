#include "name_utils.hpp"

uint32_t stringEqualWithRange(const char* str, const NameRange& range) noexcept
{
    // ... same code as before ...
    const char* s = str;
    while (*s != '\0')
        ++s;
    uint32_t len = static_cast<uint32_t>(s - str);

    uint32_t rangeLen = static_cast<uint32_t>(range.end - range.begin);
    if (len != rangeLen)
        return len & 0xFFFFFF00u;

    const char* p1 = str;
    const char* p2 = range.begin;
    int32_t remaining = static_cast<int32_t>(len) - 4;

    if (remaining >= 0) {
        do {
            if (*reinterpret_cast<const uint32_t*>(p1) !=
                *reinterpret_cast<const uint32_t*>(p2)) {
                break;
            }
            p1 += 4;
            p2 += 4;
            remaining -= 4;
        } while (remaining >= 0);
    }

    if (remaining == -4) goto match;
    if (*p1 != *p2) goto mismatch;
    if (remaining == -3) goto match;
    if (p1[1] != p2[1]) goto mismatch;
    if (remaining == -2) goto match;
    if (p1[2] != p2[2]) goto mismatch;
    if (remaining == -1) goto match;
    if (p1[3] != p2[3]) goto mismatch;

match:
    return (static_cast<uint32_t>(remaining) & 0xFFFFFF00u) | 1u;

mismatch:
    return static_cast<uint32_t>(remaining) & 0xFFFFFF00u;
}

uint32_t nameRangeEqual(const NameRange& lhs, const NameRange& rhs) noexcept
{
    uint32_t len1 = static_cast<uint32_t>(lhs.end - lhs.begin);
    uint32_t len2 = static_cast<uint32_t>(rhs.end - rhs.begin);
    uint32_t result = len2;

    if (len1 != len2)
        return result & 0xFFFFFF00u;

    const char* p1 = lhs.begin;
    const char* p2 = rhs.begin;
    int32_t remaining = static_cast<int32_t>(len1);

    while (remaining > 3) {
        result = *reinterpret_cast<const uint32_t*>(p1);
        if (result != *reinterpret_cast<const uint32_t*>(p2))
            goto byte_compare;
        p1 += 4;
        p2 += 4;
        remaining -= 4;
    }

    if (remaining == 0) goto match;

byte_compare:
    result = (result & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<unsigned char>(*p1));
    if (*p1 != *p2) goto mismatch;
    if (remaining == 1) goto match;

    result = (result & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<unsigned char>(p1[1]));
    if (p1[1] != p2[1]) goto mismatch;
    if (remaining == 2) goto match;

    result = (result & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<unsigned char>(p1[2]));
    if (p1[2] != p2[2]) goto mismatch;
    if (remaining == 3) goto match;

    result = (result & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<unsigned char>(p1[3]));
    if (p1[3] != p2[3]) goto mismatch;

match:
    return (result & 0xFFFFFF00u) | 1u;

mismatch:
    return result & 0xFFFFFF00u;
}

uint32_t nameRangeEqualsStr(const NameRange& range, const char* str) noexcept
{
    const char* p = str;
    while (*p != '\0') ++p;
    uint32_t len = static_cast<uint32_t>(p - str);

    const char* rp = range.begin;
    uint32_t rlen = static_cast<uint32_t>(range.end - range.begin);

    if (rlen != len)
        return len & 0xFFFFFF00u;

    int32_t rem = static_cast<int32_t>(len - 4);
    while (rem >= 0) {
        if (*reinterpret_cast<const uint32_t*>(rp) !=
            *reinterpret_cast<const uint32_t*>(str)) {
            goto tail;
        }
        rp += 4;
        str += 4;
        rem -= 4;
    }

tail:
    if (rem != -4) {
        if (rp[0] != str[0] ||
            (rem != -3 && (rp[1] != str[1] ||
            (rem != -2 && (rp[2] != str[2] ||
            (rem != -1 && rp[3] != str[3])))))) {
            goto mismatch;
        }
    }

    return (static_cast<uint32_t>(rem) & 0xFFFFFF00u) | 1u;

mismatch:
    return static_cast<uint32_t>(rem) & 0xFFFFFF00u;
}

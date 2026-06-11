#include "Hash.h"
#include <cstring>

namespace lockstep::Hash
{
    // xxHash32 — canonical reference algorithm.
    // https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md

    static constexpr std::uint32_t kP1 = 0x9E3779B1u;
    static constexpr std::uint32_t kP2 = 0x85EBCA77u;
    static constexpr std::uint32_t kP3 = 0xC2B2AE3Du;
    static constexpr std::uint32_t kP4 = 0x27D4EB2Fu;
    static constexpr std::uint32_t kP5 = 0x165667B1u;

    static std::uint32_t rotl32(std::uint32_t v, int r)
    {
        return (v << r) | (v >> (32 - r));
    }

    static std::uint32_t round(std::uint32_t acc, std::uint32_t lane)
    {
        return rotl32(acc + (lane * kP2), 13) * kP1;
    }

    static std::uint32_t readU32(const std::uint8_t* p)
    {
        std::uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — canonical xxHash32 signature
    std::uint32_t xx32(const void* data, std::size_t size, std::uint32_t seed)
    {
        const auto* p = static_cast<const std::uint8_t*>(data);
        const auto* end = p + size;
        std::uint32_t h32;

        if (size >= 16)
        {
            std::uint32_t v1 = seed + kP1 + kP2;
            std::uint32_t v2 = seed + kP2;
            std::uint32_t v3 = seed;
            std::uint32_t v4 = seed - kP1;

            const auto* limit = end - 16;
            while (p <= limit)
            {
                v1 = round(v1, readU32(p));
                p += 4;
                v2 = round(v2, readU32(p));
                p += 4;
                v3 = round(v3, readU32(p));
                p += 4;
                v4 = round(v4, readU32(p));
                p += 4;
            }

            h32 = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
        }
        else
        {
            h32 = seed + kP5;
        }

        h32 += static_cast<std::uint32_t>(size);

        while (p + 4 <= end)
        {
            h32 = rotl32(h32 + (readU32(p) * kP3), 17) * kP4;
            p += 4;
        }

        while (p < end)
        {
            h32 = rotl32(h32 + ((*p) * kP5), 11) * kP1;
            ++p;
        }

        h32 ^= h32 >> 15;
        h32 *= kP2;
        h32 ^= h32 >> 13;
        h32 *= kP3;
        h32 ^= h32 >> 16;

        return h32;
    }
}

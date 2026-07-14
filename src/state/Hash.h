#pragma once

#include <cstddef>
#include <cstdint>

namespace lockstep::Hash
{
    // xxHash32 over an arbitrary byte range — the canonical reference algorithm
    // (see Hash.cpp), not a placeholder.
    std::uint32_t xx32(const void* data, std::size_t size, std::uint32_t seed = 0);
}

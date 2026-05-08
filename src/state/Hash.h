#pragma once

#include <cstddef>
#include <cstdint>

namespace lockstep::Hash
{
    // xxHash32 over an arbitrary byte range. Real implementation lands with
    // sample referencing in M7.
    std::uint32_t xx32(const void* data, std::size_t size, std::uint32_t seed = 0);
}

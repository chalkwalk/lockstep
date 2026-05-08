#include "Hash.h"

namespace lockstep::Hash
{
    std::uint32_t xx32(const void* data, std::size_t size, std::uint32_t seed)
    {
        (void) data;
        (void) size;
        return seed;
    }
}

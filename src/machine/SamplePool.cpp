#include "SamplePool.h"

namespace lockstep
{
    SamplePool::SamplePool() = default;
    SamplePool::~SamplePool() = default;

    const SampleRef* SamplePool::get(int index) const
    {
        if (index < 0 || index >= static_cast<int>(refs_.size()))
            return nullptr;
        return &refs_[static_cast<std::size_t>(index)];
    }
}

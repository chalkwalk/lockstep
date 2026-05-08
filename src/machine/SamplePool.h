#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lockstep
{
    struct SampleRef
    {
        std::string path;          // relative or absolute
        std::uint32_t hashXX32 = 0; // xxHash32 of the PCM payload
    };

    class SamplePool
    {
    public:
        SamplePool();
        ~SamplePool();

        int size() const { return static_cast<int>(refs_.size()); }
        const SampleRef* get(int index) const;

    private:
        std::vector<SampleRef> refs_;
    };
}

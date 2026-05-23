#pragma once

#include <array>
#include "Track.h"

namespace lockstep
{
    inline constexpr int kNumTracks = 16;

    struct Sequence
    {
        std::array<Track, kNumTracks> tracks{};
    };
}

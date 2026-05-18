#pragma once

#include <cstdint>

namespace lockstep
{
    // Active trig-grid input mode. Default is sequencer step toggle/P-Lock.
    // Keyboard, Retrig, and SoundPool are reserved for MG (alternate trig modes).
    enum class TrigGridMode : std::uint8_t
    {
        Default,    // step trig toggle + P-Lock hold (current behaviour)
        Keyboard,   // grid plays notes chromatically (MG)
        Retrig,     // grid selects retrig/slice points (MG)
        SoundPool,  // grid selects sound pool entries (MG)
    };
}

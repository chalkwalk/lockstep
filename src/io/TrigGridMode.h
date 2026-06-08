#pragma once

#include <cstdint>

namespace lockstep
{
    // Active trig-grid momentary overlay. Set on modifier+section hold; cleared on release.
    // Default = normal step toggle/P-Lock. Retrig and SoundPool re-skin the grid
    // while the activating chord is held (Fill+TRIG and Fill+SRC respectively).
    enum class TrigGridMode : std::uint8_t
    {
        Default,    // step trig toggle + P-Lock hold (normal behaviour)
        Retrig,     // grid shows retrig/ratchet rates (or slice indices for ISliceable tracks)
        SoundPool,  // grid shows sound-pool entries for the active track
    };
}

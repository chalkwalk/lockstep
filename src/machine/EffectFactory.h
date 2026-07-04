#pragma once

#include "IEffect.h"
#include <string>
#include <vector>

namespace lockstep
{
    struct EffectInfo
    {
        std::string id;
        std::string name;
        std::string badge;
        // 8.26: true = only available in master/send pickers; hidden from track pickers.
        bool masterOnly = false;
        // Item 5: true = offered ONLY in the two master send slots (never a master
        // or track insert). The "External" send has no DSP instance — it routes the
        // send tap to a host output bus instead of processing it.
        bool sendOnly = false;
    };

    // Item 5: sentinel id for the External send. makeEffectForId returns null for
    // it (no DSP); the processor routes the send tap to a host "Send A/B" bus.
    inline constexpr const char* kExternalSendId = "lockstep.send.external.v1";

    // All effects available for factory creation and display in the picker.
    std::vector<EffectInfo> availableEffects();
    int numAvailableEffects();
    EffectInfo availableEffectInfo(int index);
}

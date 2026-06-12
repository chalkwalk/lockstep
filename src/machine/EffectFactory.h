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
    };

    // All effects available for factory creation and display in the picker.
    std::vector<EffectInfo> availableEffects();
    int numAvailableEffects();
    EffectInfo availableEffectInfo(int index);
}

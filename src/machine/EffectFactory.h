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
    };

    // All effects available for factory creation and display in the picker.
    std::vector<EffectInfo> availableEffects();
    int numAvailableEffects();
    EffectInfo availableEffectInfo(int index);
}

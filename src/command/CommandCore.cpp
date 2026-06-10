#include "CommandCore.h"

namespace lockstep
{
    bool CommandCore::handleDown(const ControllerEvent&,
                                 CommandContext&,
                                 CommandEffects&)
    {
        return false;
    }

    bool CommandCore::handleUp(const ControllerEvent&,
                               CommandContext&,
                               CommandEffects&)
    {
        return false;
    }

    bool CommandCore::handleVerb(EditMode::PrimaryScope,
                                 ControllerButton,
                                 CommandContext&,
                                 CommandEffects&)
    {
        return false;
    }
}

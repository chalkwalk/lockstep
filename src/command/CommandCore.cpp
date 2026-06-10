#include "CommandCore.h"
#include "VerbCommands.h"

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

    bool CommandCore::handleVerb(EditMode::PrimaryScope scope,
                                 ControllerButton verb,
                                 CommandContext& ctx,
                                 CommandEffects& fx)
    {
        using PS = EditMode::PrimaryScope;
        switch (scope)
        {
            case PS::Trig:    return verbs::trig(verb, ctx, fx);
            default:          return false;
        }
    }
}

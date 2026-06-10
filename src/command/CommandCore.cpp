#include "CommandCore.h"
#include "VerbCommands.h"

namespace lockstep
{
    bool CommandCore::handleDown(const ControllerEvent& ev,
                                 CommandContext&,
                                 CommandEffects& fx)
    {
        using CB = ControllerButton;
        using TA = CommandEffects::TransportAction;
        switch (ev.button)
        {
            case CB::PlayStop:
                fx.transport(TA::Play);
                return true;
            case CB::MetronomeToggle:
                fx.transport(TA::Metronome);
                return true;
            default:
                return false;
        }
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
            case PS::Trig:    return verbs::trig    (verb, ctx, fx);
            case PS::Track:   return verbs::track   (verb, ctx, fx);
            case PS::Phrase:  return verbs::phrase  (verb, ctx, fx);
            case PS::Scene:   return verbs::scene   (verb, ctx, fx);
            case PS::Song:    return verbs::song    (verb, ctx, fx);
            case PS::None:    return verbs::noScope (verb, ctx, fx);
            case PS::Morph:    return verbs::morph    (verb, ctx, fx);
            case PS::Section:  return verbs::section  (verb, ctx, fx);
            default:           return false;
        }
    }
}

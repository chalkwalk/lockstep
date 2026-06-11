#pragma once

#include "CommandContext.h"
#include "CommandEffects.h"
#include "KeyBindings.h"
#include "../io/ControllerEvent.h"
#include "../io/EditMode.h"

namespace lockstep
{
  // The command core processes fully-resolved ControllerEvents and verb dispatches.
  // All handlers return true if handled (caller stops), false to fall through to
  // legacy editor code. Methods are added scope-by-scope as each slice migrates
  // from PluginEditor — the initial stub returns false for everything.
  class CommandCore
  {
  public:
    // Called from dispatchDown (button press).
    [[nodiscard]] bool handleDown(const ControllerEvent& ev,
                                  CommandContext& ctx,
                                  CommandEffects& fx);

    // Called from dispatchUp (button release).
    [[nodiscard]] bool handleUp(const ControllerEvent& ev,
                                CommandContext& ctx,
                                CommandEffects& fx);

    // Called from dispatchVerb with the resolved primary scope and verb button.
    [[nodiscard]] bool handleVerb(EditMode::PrimaryScope scope,
                                  ControllerButton verb,
                                  CommandContext& ctx,
                                  CommandEffects& fx);

    // Dispatch a resolved ActionId (called by handleDown after resolveBinding).
    // Returns true if handled, false if the ActionId is not (yet) wired.
    [[nodiscard]] bool handleAction(ActionId action,
                                    const ControllerEvent& ev,
                                    CommandContext& ctx,
                                    CommandEffects& fx);
  };
}

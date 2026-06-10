#pragma once

#include "CommandContext.h"
#include "CommandEffects.h"
#include "../io/EditMode.h"
#include "../io/ControllerEvent.h"

namespace lockstep::verbs
{
  // Per-scope verb handler free functions.
  // Each returns true if the verb was handled (caller must stop processing).
  // Add a function here and call it from CommandCore::handleVerb as each
  // scope migrates from PluginEditor::dispatchVerb.

  [[nodiscard]] bool trig(ControllerButton verb,
                          CommandContext& ctx,
                          CommandEffects& fx);
}

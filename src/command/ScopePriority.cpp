#include "ScopePriority.h"
#include "../state/UiState.h"

namespace lockstep
{
    EditMode::PrimaryScope firstHeldSectionSuiteScope(const UiState& ui) noexcept
    {
        using PS = EditMode::PrimaryScope;
        for (auto s : kScopePriority)
        {
            if (s == PS::Track && ui.trackHeld) { return PS::Track; }
            if (s == PS::Phrase && ui.phraseScopeHeld) { return PS::Phrase; }
            if (s == PS::Scene && ui.sceneHeld) { return PS::Scene; }
            if (s == PS::Morph && ui.morphHeld) { return PS::Morph; }
            if (s == PS::Song && ui.songHeld) { return PS::Song; }
        }
        return PS::None;
    }
} // namespace lockstep

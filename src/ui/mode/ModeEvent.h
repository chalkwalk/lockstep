#pragma once

#include "../../io/ControllerEvent.h"

namespace lockstep
{
    // =========================================================================
    // ModeEvent — reducer input alphabet derived from ControllerEvent.
    //
    // The editor adapts raw ControllerEvents into these semantic forms before
    // calling handleOverlayEvent.  The adapter lives in the editor so the
    // reducer stays pure (no JUCE, no editor state).
    // =========================================================================
    enum class ModeEventKind : uint8_t
    {
        SectionPress,   // a section key (index 0-5) was pressed
        ScopePress,     // a scope modifier key was pressed
        DoubleTapFunc,  // Func was double-tapped
    };

    struct ModeEvent
    {
        ModeEventKind kind;
        int index = -1;                                   // SectionPress: section index
        ControllerButton button = ControllerButton::None; // ScopePress: which scope key
    };

    // Context passed alongside the event for queries that need external state.
    struct ScopeCtx
    {
        bool velAnyEnabled = true; // false when all tracks have velMode==Off
                                   // (controls vel sub-page cycle skip-disabled logic)
    };
}

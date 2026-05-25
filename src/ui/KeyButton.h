#pragma once
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    // Visual state for a single key button.
    enum class KeyButtonState : uint8_t
    {
        Normal,        // default resting appearance
        Pressed,       // physically held down right now
        ModeActive,    // a persistent mode is on (e.g. metronome, trig-grid mode)
        FuncHeld,      // Func modifier is held: primary dims, secondary brightens
        Disabled,      // not applicable in current context
    };

    // Colour group for a key — selects the inactive-bg / active-bg / accent triple.
    struct KeyGroup
    {
        uint32_t inactive;  // background when Normal / FuncHeld
        uint32_t active;    // background when Pressed / ModeActive
        uint32_t accent;    // border colour (1 px Normal, 2 px ModeActive)
    };

    // Draws only the QWERTY key hint in the standard style (12 pt, top-left corner).
    // `inner` is the usable interior rectangle (typically `cell.reduced(1,1)`).
    // alpha = 1.0 for normal, 0.45 for disabled.
    void paintCellKeyHint(juce::Graphics&      g,
                          juce::Rectangle<int> inner,
                          const juce::String&  hint,
                          float                alpha = 1.0f);

    // Stateless paint helper: draws one key into `cell`.
    // All geometry is determined by `cell`; caller positions cells in their own paint().
    // showKeyHint: whether to draw the small physical-key letter in the top-left.
    // compoundOverlay: register 4 — amber top strip drawn when this key is part of an
    //   active compound-chord scope (cross-column modifier pair). Orthogonal to `state`.
    void paintKeyButton(juce::Graphics&      g,
                        juce::Rectangle<int> cell,
                        const juce::String&  keyHint,
                        const juce::String&  primary,
                        const juce::String&  secondary,
                        const KeyGroup&      group,
                        KeyButtonState       state,
                        bool                 showKeyHint,
                        bool                 compoundOverlay = false);
}

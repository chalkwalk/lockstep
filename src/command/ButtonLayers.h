#pragma once

// Single source of truth for button-layer resolution (DESIGN §37.1).
// All three input paths (QWERTY, mouse, controller) call resolveLayer()
// before forwarding events to the command core.

#include "../io/ControllerEvent.h"
#include <cstddef>

namespace lockstep
{
    // Modifier state at the moment a button event is processed.
    // Reads the effective (physical OR latched) flags from UiState.
    struct LayerContext
    {
        bool funcHeld = false;
        bool trackHeld = false;
        bool muteHeld = false;
    };

    // One entry in kLayerRemaps: when `layer` flag is set and the incoming
    // event carries `raw`, rewrite the button to `effective`.
    struct LayerRemap
    {
        enum class Layer : std::uint8_t
        {
            Track,
            Mute,
            Func
        };
        ControllerButton raw;
        Layer layer;
        ControllerButton effective;
    };

    // All logical button remaps, in priority order (Track > Mute > Func).
    // Step keys use Track/Mute layers; section/verb keys use the Func layer.
    // This table is the single authoritative encoding of the DESIGN §13
    // layer grammar; every input source uses resolveLayer(), never inline checks.
    //
    // Note: entries where kPrimary and kFunc map the same key to the same
    // button (P→VerbConfirm, U→VerbRecord) are intentionally absent — resolveLayer
    // returns the event unchanged and the no-op is free.
    inline constexpr LayerRemap kLayerRemaps[] = {
        // Track layer (priority 1): step keys → select-track
        { ControllerButton::Step, LayerRemap::Layer::Track, ControllerButton::SelectTrack },
        // Mute layer (priority 2): step keys → toggle-mute
        { ControllerButton::Step, LayerRemap::Layer::Mute, ControllerButton::ToggleMute },
        // Func layer (priority 3):
        { ControllerButton::Section, LayerRemap::Layer::Func, ControllerButton::MetaSection },
        // Func+3 → MetronomeToggle remap removed (9.10): metronome moved to TIME band field 2.
        { ControllerButton::VerbSnapshot, LayerRemap::Layer::Func, ControllerButton::Restore },
        // Func+VerbClear -> VerbDelete retired (9.29). Delete lives on the gesture
        // axis now (scope + HOLD Clear), because Func+Track is the Machine scope and
        // could not also be "delete the track". Func over Clear is a qualifier again,
        // not a replacement -- which is what makes Trig+Func+Clear (clear the P-Locks,
        // keep the trig) reachable: this remap used to rewrite the button before
        // verbs::trig could read the Func flag, so that documented gesture was dead.
        // CB::VerbDelete still exists as a logical button (controllers may send it;
        // the hold row resolves to ActionId::VerbDelete) -- it just has no QWERTY key.
    };

    // Resolve any layer remap for the incoming event.
    // Returns a copy of `raw` with only the `button` field changed if a
    // matching remap is found; otherwise returns `raw` unchanged.
    // The `index`, `velocity`, and `delta` fields always survive.
    [[nodiscard]] ControllerEvent resolveLayer(ControllerEvent raw,
                                               const LayerContext& ctx) noexcept;
}

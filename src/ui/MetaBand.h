#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SurfaceModel.h"  // RingMode

namespace lockstep
{
    class LockstepProcessor;
    class EditContext;
    struct UiState;

    // -------------------------------------------------------------------------
    // MetaBand — which manipulation-zone surface is currently shown.
    // None = normal machine params; the rest are the meta/swing surfaces.
    enum class MetaBand
    {
        None,
        Cond,
        Trig,
        Divider,
        PhraseLen,
        Global,
        Swing,
        Chance,
        MasterFx,
        Euclidean,
        Transport
    };

    // -------------------------------------------------------------------------
    // MetaFieldView — render-agnostic description of one encoder slot.
    // Populated by buildMetaBand; consumed by ManipulationZone (screen) and
    // buildSurfaceModel (controller), keeping both in lockstep.
    struct MetaFieldView
    {
        bool active = false;             // slot is populated
        juce::String label;
        juce::String valueText;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float value = 0.0f;
        bool stepped = false;
        bool writable = false;
        bool hasOverride = false;
        RingMode ringMode = RingMode::UnipolarFill;
        std::array<ReferenceMark, 2> marks{};  // scope-coloured reference ticks
    };

    // -------------------------------------------------------------------------
    // resolveMetaBand — the single modality cascade.
    // Reads ui.masterSection + held-scope flags + ui.swingDismissed.
    // Both ManipulationZone and buildSurfaceModel call this so they never diverge.
    MetaBand resolveMetaBand(const UiState& ui);

    // swingScopeFor — 0=none, 1=song-all, 2=scene-all delta, 3=song-track delta.
    int swingScopeFor(const UiState& ui);

    // -------------------------------------------------------------------------
    // buildMetaBand — pure builder: fills 8 MetaFieldViews from current state.
    // Lifted verbatim from ManipulationZone::refresh*Sliders.
    std::array<MetaFieldView, 8> buildMetaBand(MetaBand band,
                                               int swingScope,
                                               LockstepProcessor& proc,
                                               int track,
                                               const EditContext& ctx,
                                               const UiState& ui);

    // writeMetaField — write-back: apply a slider/encoder delta to the model.
    // Lifted verbatim from ManipulationZone::write*Field.
    void writeMetaField(MetaBand band,
                        int swingScope,
                        int field,
                        float value,
                        LockstepProcessor& proc,
                        int track,
                        EditContext& ctx,
                        UiState& ui);
}

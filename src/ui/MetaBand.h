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
        Density,
        DensityMode,       // Musicality sub-page (Uniform / Mixed / Metric)
        DensitySelection,  // Selection sub-page (Scrub / Re-roll)
        MasterFx,
        Euclidean,
        Transport,
        Vel,        // velocity overlay depth sub-page (per-track, paginated)
        VelCenter,  // velocity overlay center sub-page
        VelMode,    // velocity overlay mode sub-page (Off / Bar)
        VelBlend    // velocity overlay blend sub-page (Replace / Mix)
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

        // Density-cell metadata (only meaningful when band == Density).
        bool densityCell = false;
        float densityMasterOffset = 0.0f;   // master offset in [-1, 1]
        float densityEffective = 1.0f;      // clamp(per+master, 0.01, 1.0) normalised [0,1]
    };

    // -------------------------------------------------------------------------
    // resolveMetaBand — the single modality cascade.
    // Reads ui.masterSection + held-scope flags + ui.swingDismissed.
    // Both ManipulationZone and buildSurfaceModel call this so they never diverge.
    MetaBand resolveMetaBand(const UiState& ui);

    // swingScopeFor — 0=none, 1=song-all, 2=scene-all delta, 3=song-track delta.
    int swingScopeFor(const UiState& ui);

    // densityEditsMaster — true when a density-band edit should target the global
    // master offset rather than the per-track knob.  Single predicate consulted by
    // every write path (mouse, encoder, writeMetaField guard) so they cannot disagree.
    bool densityEditsMaster(const UiState& ui) noexcept;

    // densityWriteTarget — resolve which track index a density-band slot edit lands on.
    // master==true means the edit targets the master offset; trackIdx is -1 in that case.
    // Absorbs the paging formula shared by buildDensityBand and writeMetaField.
    struct DensityWriteTarget { bool master; int trackIdx; };
    DensityWriteTarget densityWriteTarget(const UiState& ui, int field, int focusedTrack) noexcept;

    // sectionSelectClearsDensitySticky — focus-change supersede policy.
    // Returns true when a bare section press (index 0-4) should exit density sticky mode.
    // Section 5 / FX is reserved for subpage cycling inside the mode; nav keys page banks.
    bool sectionSelectClearsDensitySticky(const UiState& ui, int sectionIndex) noexcept;

    // sectionSelectClearsVelSticky — parallel policy for vel sticky mode.
    // Returns true when a section press (anything except AMP = index 3) should exit.
    bool sectionSelectClearsVelSticky(const UiState& ui, int sectionIndex) noexcept;

    // bandTitle — short display name for a MetaBand, used by the MZ header strip (§26.4.1).
    juce::String bandTitle(MetaBand band);

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

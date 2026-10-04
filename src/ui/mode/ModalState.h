#pragma once
#include <cstdint>

namespace lockstep
{
    struct UiState;

    // ── Modal ─────────────────────────────────────────────────────────────────
    // The single, unified set of mutually-exclusive UI modals (PRINCIPLES §18/§20).
    //
    // Historically modal state was spread across the sticky `Overlay` enum (MZ-band
    // overlays), the `FuncReskin` accessor (Func-layer pickers/editors), the
    // transient `euclidHeld`/`generatorHubHeld` chord bools, and the per-mode flags
    // in UiState. `Modal` is the one enum those collapse into: at most one is active.
    //
    // Two lifecycles live here (documented per value):
    //   • entered/sticky — persist after key release until escape/commit
    //     (Time, Density, Vel, Euclid, NoteEdit, PLockClear, MasterFxPicker)
    //   • held-chord — true only while the activating keys are physically down
    //     (GeneratorHub, MachinePicker, TrackFxPicker)
    //
    // activeModal() is the read-only SSOT query during the migration: it derives the
    // active modal from the existing UiState fields with the authoritative priority
    // (mirrors resolveActiveLayer's grid cascade, then the MZ overlay). Once the
    // reducer owns the state, the per-mode bools are removed and this reads one field.
    enum class Modal : uint8_t
    {
        None,
        // Func-layer pickers / editors (step-grid modals), highest priority first.
        //
        // The gestures below were corrected in 6.9 against `kBindings`, which is
        // the authority (9.12). Three of them had been wrong since 9.29 moved
        // the pickers off the Func layer when Func+Track became the Machine
        // scope, and a fourth described the generator hub as a chord it has
        // not been for some time. A comment is not load-bearing until someone
        // believes it, and these were believed.
        MasterFxPicker,   // Song + hold(FX)    (entered)
        TrackFxPicker,    // Track + hold(FX)   (held-chord)
        MachinePicker,    // Track + hold(SRC)  (held-chord)
        GeneratorHub,     // hold(TapTempo)     (held-chord)
        NoteEdit,         // SRC tap on a held step, via the inspector  (entered)
        PLockClear,       // Func + step        (entered)
        // Armed generators (step grid shows trigs; commit/cancel/escape).
        // All three are chosen from the generator hub: cells 0, 3 and 4.
        Euclid,
        Melodic,
        Harmony,
        // Sticky MZ-band overlays (escape to exit):
        Time,             // Song/Scene + TRIG, toggling
        Density,          // generator hub, cell 1
        Vel,              // generator hub, cell 2
        SampleProps,      // pool sample-properties editor (entered from a pool row)
        Cue,              // 6.4 cue console: Cue scope + long-hold(AMP)
        Identity,         // 5.3 generative naming/colour editor for a Song/Scene/Sound
        Browser,          // 5.3 Song->Scene / per-track Phrase browser; Func+Song+MOD
    };

    // Highest-priority active modal derived from current UiState (read-only).
    // Priority mirrors resolveActiveLayer for the grid modals, then Euclid, then the
    // MZ overlay field. Returns Modal::None when nothing modal is active.
    [[nodiscard]] Modal activeModal(const UiState& ui) noexcept;

    // Stable name for tests / inspector / logging. Never nullptr.
    [[nodiscard]] const char* modalName(Modal m) noexcept;
}

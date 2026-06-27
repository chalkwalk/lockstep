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
        // Func-layer pickers / editors (step-grid modals), highest priority first:
        MasterFxPicker,   // Func+Song+FX     (entered)
        TrackFxPicker,    // Func+FX          (held-chord)
        MachinePicker,    // Func+Track       (held-chord)
        GeneratorHub,     // 3-key long-hold  (held-chord)
        NoteEdit,         // Func+Src+step    (entered)
        PLockClear,       // Func+step        (entered)
        // Armed generators (step grid shows trigs; commit/cancel/escape):
        Euclid,
        Melodic,
        // Sticky MZ-band overlays (escape to exit):
        Time,
        Density,
        Vel,
    };

    // Highest-priority active modal derived from current UiState (read-only).
    // Priority mirrors resolveActiveLayer for the grid modals, then Euclid, then the
    // MZ overlay field. Returns Modal::None when nothing modal is active.
    [[nodiscard]] Modal activeModal(const UiState& ui) noexcept;

    // Stable name for tests / inspector / logging. Never nullptr.
    [[nodiscard]] const char* modalName(Modal m) noexcept;
}

#pragma once
#include <cstdint>

namespace lockstep
{
    struct UiState;

    // Closed enum over the five Func-layer picker / modal-editor modes.
    // These are all entered by a Func+something gesture and all exited on
    // Func release. Priority order matches resolveActiveLayer (highest first).
    enum class FuncReskin : uint8_t
    {
        None,
        MasterFxPicker,  // Func+Song+FX — masterFxPickerOpen
        TrackFxPicker,   // Func+FX       — funcFxHeld
        MachinePicker,   // Func+Track    — funcTrackHeld
        NoteEdit,        // Func+Src+step — noteEditMode
        PLockClear,      // Func+step     — pLockClearMode
    };

    // Returns the highest-priority active Func-layer mode (or None).
    // Pure read of UiState; priority mirrors resolveActiveLayer so the
    // displayed SurfaceLayer and the active FuncReskin always agree.
    [[nodiscard]] FuncReskin activeFuncReskin(const UiState& ui) noexcept;

    // Clears all Func-layer mode state: calls resetNoteEdit(), resetPLockClear(),
    // and resetFxPickers() unconditionally. Each reset is safe when its mode is
    // not active, so a single call is correct on any Func-release path.
    //
    // Caller is responsible for committing any staged edits (note removals,
    // P-Lock clears) BEFORE calling exitFuncReskin — those actions need
    // processor access and cannot live in a pure UiState function.
    void exitFuncReskin(UiState& ui) noexcept;

} // namespace lockstep

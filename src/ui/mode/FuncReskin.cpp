#include "FuncReskin.h"
#include "../../state/UiState.h"

namespace lockstep
{
    FuncReskin activeFuncReskin(const UiState& ui) noexcept
    {
        if (ui.masterFxPickerOpen) { return FuncReskin::MasterFxPicker; }
        if (ui.funcFxHeld)         { return FuncReskin::TrackFxPicker; }
        if (ui.funcTrackHeld)      { return FuncReskin::MachinePicker; }
        if (ui.noteEditMode)       { return FuncReskin::NoteEdit; }
        if (ui.pLockClearMode)     { return FuncReskin::PLockClear; }
        return FuncReskin::None;
    }

    void exitFuncReskin(UiState& ui) noexcept
    {
        ui.resetNoteEdit();    // clears noteEditMode + funcSrcHeld + octave + steps + staged
        ui.resetPLockClear();  // clears pLockClearMode + track + step + staged
        ui.resetFxPickers();   // clears funcTrackHeld + funcFxHeld + masterFxPickerOpen
    }
} // namespace lockstep

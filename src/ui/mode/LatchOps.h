#pragma once
#include <cstdint>
#include "../../state/UiState.h"      // LatchState
#include "../../io/ControllerEvent.h" // ControllerButton

namespace lockstep
{
    // Returns 0 (col1: PhraseScope/MorphScope/MuteScope),
    //         1 (col2: TrackScope/SceneScope/SongScope/FillScope),
    //        -1 if cb is not a latchable modifier.
    [[nodiscard]] int latchColumn(ControllerButton cb) noexcept;

    // Returns a pointer to the latch bool inside `state` that corresponds to cb,
    // or nullptr if cb is not latchable.  Modifying through the pointer is the
    // preferred way to set/clear a single latch without a hard-coded switch.
    [[nodiscard]] bool* latchBoolFor(LatchState& state, ControllerButton cb) noexcept;
    [[nodiscard]] const bool* latchBoolFor(const LatchState& state, ControllerButton cb) noexcept;

    // Clears all latches in the same column as cb, EXCEPT cb itself.
    // Pure: only mutates LatchState.  Callers must handle dispatchUp/side effects
    // for each cleared latch (check the field before + after to detect the change).
    void clearLatchColumnExcept(LatchState& state, ControllerButton cb) noexcept;

} // namespace lockstep

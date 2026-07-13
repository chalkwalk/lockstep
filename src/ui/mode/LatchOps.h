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

    // 9.29 — the compound-scope latch rule, as one pure function.
    //
    // Latching Track or Song WITH Func held latches the compound scope it forms
    // (Machine = Func+Track, Set = Func+Song), and the compound then outlives the Func
    // key — the only latch that survives a key that was part of entering it. It exists
    // because Func never latches on its own, and Machine is precisely the scope you
    // want hands-free: sound design is minutes of two-handed work, not a held chord.
    //
    // Returns what LatchState::compound must become. Note it is false for EVERY other
    // latch and for every unlatch: a compound qualifies exactly one col-2 scope, so a
    // second latch anywhere replaces it rather than accumulating. Keeping the rule
    // here (not inline in the editor's setModifierLatch) is what lets it be tested at
    // all — the editor's latch path is still a juce::Component away from a test.
    [[nodiscard]] bool compoundLatchFor(ControllerButton cb, bool funcHeld,
                                        bool set) noexcept;

} // namespace lockstep

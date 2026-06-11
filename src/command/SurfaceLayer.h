#pragma once
#include <cstdint>
#include "../core/TrackInputMode.h"

namespace lockstep
{
  struct UiState;
  struct EditContext;

  // The active step-grid rendering layer — the single SSOT for which overlay
  // is shown on the step grid and drives dispatch. Priority order: first match wins.
  // resolveActiveLayer() is the one place where all layer conditions are encoded.
  enum class SurfaceLayer : uint8_t
  {
    PendingConfirm,   // absolute confirm prompt (Task B) — highest priority
    SoundPool,        // ui.trigGridMode == SoundPool
    RetrigPicker,     // ui.trigGridMode == Retrig
    MasterFxPicker,   // ui.masterFxPickerOpen (Task B wiring)
    TrackFxPicker,    // ui.funcFxHeld
    MachinePicker,    // ui.funcTrackHeld
    NoteEdit,         // ui.noteEditMode && !noteEditSteps.empty()
    PLockClear,       // ui.pLockClearMode && track matches && step >= 0
    ChromaticInput,   // active track input mode == Chromatic
    LevelsInput,      // active track input mode == Levels
    MorphMuteView,    // ui.morphHeld && ui.muteHeld
    MuteView,         // ui.muteHeld (bare, after MorphMuteView)
    LengthEdit,       // (phraseScopeHeld || morphHeld) && funcHeld && !funcTrackHeld
    MorphStepView,    // ui.morphHeld && !ui.funcHeld
    ScopeSelector,    // trackHeld || phraseScopeHeld || sceneHeld || songHeld
    Base,             // normal step grid
  };

  // Caller-supplied facts that resolveActiveLayer cannot derive from UiState alone.
  struct LayerFacts
  {
    TrackInputMode inputMode   = TrackInputMode::Play;  // active track's input mode
    int            activeTrack = 0;                     // index of the focused track
  };

  // The one function that decides which overlay is active.
  // Called by both buildSurfaceModel (rendering) and dispatchDown (input routing)
  // so they cannot diverge.
  SurfaceLayer resolveActiveLayer(const UiState& ui,
                                  const EditContext& ec,
                                  const LayerFacts& f) noexcept;

} // namespace lockstep

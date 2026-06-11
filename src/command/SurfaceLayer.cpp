#include "SurfaceLayer.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include "../io/TrigGridMode.h"

namespace lockstep
{
  SurfaceLayer resolveActiveLayer(const UiState& ui,
                                  const EditContext& /*ec*/,
                                  const LayerFacts& f) noexcept
  {
    // Priority is top-to-bottom — first matching condition wins.
    // Order mirrors the buildSurfaceModel step-grid cascade, which is the
    // user-visible truth. Any intentional divergence must be documented.

    if (ui.confirm.pending()) { return SurfaceLayer::PendingConfirm; }

    if (ui.trigGridMode == TrigGridMode::SoundPool) { return SurfaceLayer::SoundPool;   }
    if (ui.trigGridMode == TrigGridMode::Retrig)    { return SurfaceLayer::RetrigPicker; }

    if (ui.masterFxPickerOpen) { return SurfaceLayer::MasterFxPicker; }
    if (ui.funcFxHeld)         { return SurfaceLayer::TrackFxPicker;  }
    if (ui.funcTrackHeld)      { return SurfaceLayer::MachinePicker;  }

    if (ui.noteEditMode && !ui.noteEditSteps.empty()) { return SurfaceLayer::NoteEdit; }

    if (ui.pLockClearMode
        && ui.pLockClearTrack == f.activeTrack
        && ui.pLockClearStep  >= 0)
    {
      return SurfaceLayer::PLockClear;
    }

    if (f.inputMode == TrackInputMode::Chromatic) { return SurfaceLayer::ChromaticInput; }
    if (f.inputMode == TrackInputMode::Levels)    { return SurfaceLayer::LevelsInput;    }

    if (ui.morphHeld && ui.muteHeld) { return SurfaceLayer::MorphMuteView; }
    if (ui.muteHeld)                 { return SurfaceLayer::MuteView;      }

    if ((ui.phraseScopeHeld || ui.morphHeld) && ui.funcHeld && !ui.funcTrackHeld)
    {
      return SurfaceLayer::LengthEdit;
    }
    if (ui.morphHeld && !ui.funcHeld) { return SurfaceLayer::MorphStepView; }

    if (ui.trackHeld || ui.phraseScopeHeld || ui.sceneHeld || ui.songHeld)
    {
      return SurfaceLayer::ScopeSelector;
    }

    return SurfaceLayer::Base;
  }
} // namespace lockstep

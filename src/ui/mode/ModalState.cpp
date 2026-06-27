#include "ModalState.h"
#include "../../state/UiState.h"

namespace lockstep
{
    Modal activeModal(const UiState& ui) noexcept
    {
        // Grid-modal cascade — order mirrors resolveActiveLayer (the user-visible
        // step-grid truth). Held-chords and entered modes interleave by priority.
        if (ui.masterFxPickerOpen)                    return Modal::MasterFxPicker;
        if (ui.funcFxHeld)                            return Modal::TrackFxPicker;
        if (ui.funcTrackHeld)                         return Modal::MachinePicker;
        if (ui.generatorHubHeld)                      return Modal::GeneratorHub;
        if (ui.noteEditMode)                          return Modal::NoteEdit;
        if (ui.pLockClearMode)                        return Modal::PLockClear;

        // Armed Euclid generator (outranks the MZ overlay field below; the grid
        // shows trigs while the MZ shows the Euclid band).
        if (ui.euclidHeld)                            return Modal::Euclid;
        if (ui.melodicHeld)                           return Modal::Melodic;

        // Sticky MZ-band overlays.
        switch (ui.overlay)
        {
            case Overlay::Time:    return Modal::Time;
            case Overlay::Density: return Modal::Density;
            case Overlay::Vel:     return Modal::Vel;
            case Overlay::Euclid:  return Modal::Euclid;   // defensive; euclidHeld is the real store
            case Overlay::Melodic: return Modal::Melodic;  // defensive; melodicHeld is the real store
            case Overlay::None:    break;
        }
        return Modal::None;
    }

    const char* modalName(Modal m) noexcept
    {
        switch (m)
        {
            case Modal::None:           return "None";
            case Modal::MasterFxPicker: return "MasterFxPicker";
            case Modal::TrackFxPicker:  return "TrackFxPicker";
            case Modal::MachinePicker:  return "MachinePicker";
            case Modal::GeneratorHub:   return "GeneratorHub";
            case Modal::NoteEdit:       return "NoteEdit";
            case Modal::PLockClear:     return "PLockClear";
            case Modal::Euclid:         return "Euclid";
            case Modal::Melodic:        return "Melodic";
            case Modal::Time:           return "Time";
            case Modal::Density:        return "Density";
            case Modal::Vel:            return "Vel";
        }
        return "None";
    }
}

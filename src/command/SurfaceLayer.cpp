#include "SurfaceLayer.h"
#include "ScopePriority.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include "../io/TrigGridMode.h"

namespace lockstep
{
    SurfaceLayer resolveActiveLayer(const UiState& ui,
                                    const EditContext& ec,
                                    const LayerFacts& f) noexcept
    {
    // Priority is top-to-bottom — first matching condition wins.
    // Order mirrors the buildSurfaceModel step-grid cascade, which is the
    // user-visible truth. Any intentional divergence must be documented.

        if (ui.confirm.pending()) { return SurfaceLayer::PendingConfirm; }
        if (ui.deletePicker.active()) { return SurfaceLayer::DeletePicker; }

        if (ui.trigGridMode == TrigGridMode::SoundPool) { return SurfaceLayer::SoundPool; }
        if (ui.trigGridMode == TrigGridMode::Retrig) { return SurfaceLayer::RetrigPicker; }

        if (ui.masterFxPickerOpen) { return SurfaceLayer::MasterFxPicker; }
        if (ui.funcFxHeld) { return SurfaceLayer::TrackFxPicker; }
        if (ui.funcTrackHeld) { return SurfaceLayer::MachinePicker; }
        if (ui.generatorHubHeld) { return SurfaceLayer::GeneratorHub; }

        // KEY page of the signatures band: the grid hosts the modifier/symmetric
        // panel (DESIGN §4.10). The TIME page leaves the grid as the sequencer.
        if (ui.overlay == Overlay::Time && ui.sigPage == UiState::SigPage::Key)
        {
            return SurfaceLayer::KeyPanel;
        }

        if (ui.noteEditMode && !ui.noteEditSteps.empty()) { return SurfaceLayer::NoteEdit; }

        // While moving/micro-nudging a held step, drop back to the sequencer view
        // so the step is visible hopping across the grid (the MZ flips to the
        // Step-Position panel via resolveMetaBand). Outranks the StepInspector
        // re-skin below. stepMoveActive is only set while a step is held and is
        // cleared on release.
        if (ui.stepMoveActive) { return SurfaceLayer::Base; }

        if (ec.heldStepIndex() >= 0) { return SurfaceLayer::StepInspector; }

        if (ui.pLockClearMode && ui.pLockClearTrack == f.activeTrack && ui.pLockClearStep >= 0)
        {
            return SurfaceLayer::PLockClear;
        }

        if (f.inputMode == TrackInputMode::Chromatic) { return SurfaceLayer::ChromaticInput; }
        if (f.inputMode == TrackInputMode::Levels) { return SurfaceLayer::LevelsInput; }

        if (ui.morphHeld && ui.muteHeld) { return SurfaceLayer::MorphMuteView; }
        if (ui.muteHeld) { return SurfaceLayer::MuteView; }

        if ((ui.phraseScopeHeld || ui.morphHeld) && ui.funcHeld && !ui.funcTrackHeld)
        {
            return SurfaceLayer::LengthEdit;
        }
        if (ui.morphHeld && !ui.funcHeld) { return SurfaceLayer::MorphStepView; }

        // Euclidean modal outranks scope-selector: the grid must show trigs, not
        // the phrase-select banner, whether Phrase is held or latched.
        if (ui.euclidHeld) { return SurfaceLayer::Base; }

        if (ui.trackHeld || ui.phraseScopeHeld || ui.sceneHeld || ui.songHeld)
        {
            return SurfaceLayer::ScopeSelector;
        }

        // S3: a focused looper has no trig steps, so the step grid is its always-on
        // transport + performance console — shown whenever nothing higher is active.
        if (f.activeTrackIsLooper) { return SurfaceLayer::LooperConsole; }

        // 7b: a machine may repurpose the grid as its own console. AlwaysOn shows
        // like the looper; OnDemand only while the user has it open. Both sit below
        // every held modal above so a scope/mute chord still wins.
        if (f.activeTrackConsoleMode == ConsoleMode::AlwaysOn) { return SurfaceLayer::MachineConsole; }
        if (f.activeTrackConsoleMode == ConsoleMode::OnDemand && ui.machineConsoleOpen)
        {
            return SurfaceLayer::MachineConsole;
        }

        return SurfaceLayer::Base;
    }

    const char* layerBanner(SurfaceLayer layer, const UiState& ui) noexcept
    {
        using PS = EditMode::PrimaryScope;
        switch (layer)
        {
            case SurfaceLayer::PendingConfirm:
                return "CONFIRM?";

            case SurfaceLayer::DeletePicker:
                switch (ui.deletePicker.scope)
                {
                    case DeleteScope::Track:  return "DELETE WHICH TRACK?";
                    case DeleteScope::Phrase: return "DELETE WHICH PHRASE?";
                    case DeleteScope::Scene:  return "DELETE WHICH SCENE?";
                    case DeleteScope::None:   return nullptr;
                }
                return nullptr;  // unreachable — DeleteScope is exhaustive above

            case SurfaceLayer::MachinePicker:
                return "SELECT MACHINE";

            case SurfaceLayer::GeneratorHub:
                return "SELECT GENERATOR";

            case SurfaceLayer::LooperConsole:
                return "LOOPER";

            case SurfaceLayer::MachineConsole:
                return "CONSOLE";

            case SurfaceLayer::KeyPanel:
                return "KEY MODIFIERS";

            case SurfaceLayer::ScopeSelector: {
                const PS scope = firstHeldSectionSuiteScope(ui);
                if (scope == PS::Track)  { return "SELECT TRACK"; }
                if (scope == PS::Phrase) { return "SELECT PHRASE"; }
                if (scope == PS::Scene)  { return "SELECT SCENE"; }
                return nullptr;
            }

            case SurfaceLayer::SoundPool:
            case SurfaceLayer::RetrigPicker:
            case SurfaceLayer::MasterFxPicker:
            case SurfaceLayer::TrackFxPicker:
            case SurfaceLayer::NoteEdit:
            case SurfaceLayer::StepInspector:
            case SurfaceLayer::PLockClear:
            case SurfaceLayer::ChromaticInput:
            case SurfaceLayer::LevelsInput:
            case SurfaceLayer::MorphMuteView:
            case SurfaceLayer::MuteView:
            case SurfaceLayer::LengthEdit:
            case SurfaceLayer::MorphStepView:
            case SurfaceLayer::Base:
                return nullptr;
        }
        return nullptr;  // unreachable — SurfaceLayer is exhaustive above
    }
} // namespace lockstep

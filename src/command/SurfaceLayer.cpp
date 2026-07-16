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
        if (ui.machinePickerOpen) { return SurfaceLayer::MachinePicker; }
        if (ui.generatorHubHeld) { return SurfaceLayer::GeneratorHub; }

        // 5.3 identity naming/colour overlay: the grid is the name/colour picker.
        // Sticky (entered explicitly), so it sits below the held pickers above but
        // above the sequencer views — a stray section/scope press exits it (its
        // OverlayDescriptor), it does not silently underlay them.
        if (ui.overlay == Overlay::Identity) { return SurfaceLayer::Identity; }
        if (ui.overlay == Overlay::Browser)  { return SurfaceLayer::Browser; }

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
        if (ui.muteHeld && ui.relaunchHeld) { return SurfaceLayer::MuteRelaunchView; }
        if (ui.muteHeld) { return SurfaceLayer::MuteView; }

        if ((ui.phraseScopeHeld || ui.morphHeld) && ui.funcHeld && !ui.machineScopeHeld)
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
                return ui.routeConsoleActive
                           ? "ROUTING  (tap cell = cycle dest · P = commit · Func+P = cancel)"
                           : "CONSOLE";

            case SurfaceLayer::Identity:
                return ui.identityColourPage ? "PICK COLOUR" : "NAME";

            case SurfaceLayer::Browser:
                // Static fallback; buildSurfaceModel sets a dynamic stepBanner with
                // the live song/scene/track context (overrides this in the renderer).
                return ui.browserPage == UiState::BrowserPage::Scenes ? "BROWSE SCENES"
                                                                      : "BROWSE PHRASES";

            case SurfaceLayer::KeyPanel:
                return "KEY MODIFIERS";

            case SurfaceLayer::ScopeSelector: {
                // 9.29: Func+Track is the Machine scope, not a track selector — the
                // grid still selects tracks (Track is held), but the banner must name
                // the operand the verbs and section keys are now aimed at.
                if (ui.machineScopeHeld) { return "MACHINE - the sound"; }
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
                return nullptr;
            case SurfaceLayer::MuteRelaunchView:
                return "RELAUNCH — tap a track to restart it from step 1";
            case SurfaceLayer::LengthEdit:
            case SurfaceLayer::MorphStepView:
            case SurfaceLayer::Base:
                return nullptr;
        }
        return nullptr;  // unreachable — SurfaceLayer is exhaustive above
    }

    StepRenderKind layerStepRender(SurfaceLayer layer) noexcept
    {
        switch (layer)
        {
            // Labeled: a grid of text cells rendered generically from c.primary — no
            // bespoke paint branch. New text overlays belong here.
            case SurfaceLayer::Identity:
            case SurfaceLayer::Browser:
                return StepRenderKind::Labeled;

            // Custom: a dedicated branch in paintStepRows (grep the guard shown).
            case SurfaceLayer::MachinePicker:    // uiState_.machinePickerOpen
            case SurfaceLayer::TrackFxPicker:    // uiState_.funcFxHeld
            case SurfaceLayer::MasterFxPicker:   // uiState_.masterFxPickerOpen
            case SurfaceLayer::GeneratorHub:     // uiState_.generatorHubHeld
            case SurfaceLayer::KeyPanel:         // overlay==Time && sigPage==Key
            case SurfaceLayer::NoteEdit:         // uiState_.noteEditMode
            case SurfaceLayer::PLockClear:       // uiState_.pLockClearMode
            case SurfaceLayer::MorphMuteView:    // uiState_.muteHeld branch
            case SurfaceLayer::MuteRelaunchView: // uiState_.muteHeld branch
            case SurfaceLayer::MuteView:         // uiState_.muteHeld branch
            case SurfaceLayer::MorphStepView:    // uiState_.morphHeld && !funcHeld
            case SurfaceLayer::LooperConsole:    // activeLayer == LooperConsole
            case SurfaceLayer::MachineConsole:   // activeLayer == MachineConsole
                return StepRenderKind::Custom;

            // Sequencer: the trig step grid (the paintStepRows fall-through default).
            // These decorate the grid via model cell state rather than showing text.
            case SurfaceLayer::PendingConfirm:
            case SurfaceLayer::DeletePicker:
            case SurfaceLayer::SoundPool:
            case SurfaceLayer::RetrigPicker:
            case SurfaceLayer::StepInspector:
            case SurfaceLayer::ChromaticInput:
            case SurfaceLayer::LevelsInput:
            case SurfaceLayer::LengthEdit:
            case SurfaceLayer::ScopeSelector:
            case SurfaceLayer::Base:
                return StepRenderKind::Sequencer;
        }
        return StepRenderKind::Sequencer;  // unreachable — SurfaceLayer is exhaustive
    }
} // namespace lockstep

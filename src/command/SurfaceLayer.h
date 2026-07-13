#pragma once
#include <cstdint>
#include "../core/TrackInputMode.h"
#include "../machine/ConsoleMode.h"

namespace lockstep
{
    struct UiState;
    class EditContext;

  // The active step-grid rendering layer — the single SSOT for which overlay
  // is shown on the step grid and drives dispatch. Priority order: first match wins.
  // resolveActiveLayer() is the one place where all layer conditions are encoded.
    enum class SurfaceLayer : uint8_t
    {
        PendingConfirm,   // absolute confirm prompt (Task B) — highest priority
        DeletePicker,     // deletion target selection (scope+Func+Clear armed)
        SoundPool,        // ui.trigGridMode == SoundPool
        RetrigPicker,     // ui.trigGridMode == Retrig
        MasterFxPicker,   // ui.masterFxPickerOpen (Task B wiring)
        TrackFxPicker,    // ui.funcFxHeld
        MachinePicker,    // ui.machinePickerOpen — Track+hold(SRC)
        GeneratorHub,     // ui.generatorHubHeld — momentary Euclid/Density/Vel picker
        KeyPanel,         // overlay==Time && sigPage==Key — modifier/symmetric grid panel
        NoteEdit,         // ui.noteEditMode && !noteEditSteps.empty()
        StepInspector,    // ec.heldStepIndex() >= 0 — held step shows P-lock overview
        PLockClear,       // ui.pLockClearMode && track matches && step >= 0 (retained for compat)
        ChromaticInput,   // active track input mode == Chromatic
        LevelsInput,      // active track input mode == Levels
        MorphMuteView,    // ui.morphHeld && ui.muteHeld
        MuteRelaunchView, // ui.muteHeld && ui.relaunchHeld (Mute+Play: relaunch/retrigger)
        MuteView,         // ui.muteHeld (bare, after MorphMuteView)
        LengthEdit,       // (phraseScopeHeld || morphHeld) && funcHeld && !machineScopeHeld
        MorphStepView,    // ui.morphHeld && !ui.funcHeld
        ScopeSelector,    // trackHeld || phraseScopeHeld || sceneHeld || songHeld
        LooperConsole,    // focused track is a looper, no higher overlay (S3) — always-on
        MachineConsole,   // machine consoleMode() AlwaysOn, or OnDemand && machineConsoleOpen (7b)
        Base,             // normal step grid
    };

  // Caller-supplied facts that resolveActiveLayer cannot derive from UiState alone.
    struct LayerFacts
    {
        TrackInputMode inputMode = TrackInputMode::Play;  // active track's input mode
        int activeTrack = 0;                     // index of the focused track
        bool activeTrackIsLooper = false;        // focused track runs a LoopMachine (S3)
        ConsoleMode activeTrackConsoleMode = ConsoleMode::None;  // focused machine's console (7b)
    };

  // The one function that decides which overlay is active.
  // Called by both buildSurfaceModel (rendering) and dispatchDown (input routing)
  // so they cannot diverge.
    SurfaceLayer resolveActiveLayer(const UiState& ui,
                                    const EditContext& ec,
                                    const LayerFacts& f) noexcept;

  // Returns the banner text for the given active layer, or nullptr when no banner
  // applies. The switch inside is exhaustive (no default:) so adding a new
  // SurfaceLayer without wiring its banner text is a compile error under -Werror.
  // Dynamic banners (DeletePicker scope, ScopeSelector held scope) are resolved
  // from `ui` directly; callers need not inspect ui.deletePicker or heldScopes.
    const char* layerBanner(SurfaceLayer layer, const UiState& ui) noexcept;

} // namespace lockstep

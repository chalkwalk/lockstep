#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include "../io/ControllerEvent.h"
#include "../state/UiState.h"  // ConfirmKind

namespace lockstep
{
  // Pure-virtual intent interface returned to the editor after command dispatch.
  // Implementations: EditorEffects (live editor), RecordingEffects (gesture tests).
  // Keeps CommandCore JUCE-free except for juce::String (same bar EditMode meets).
    class CommandEffects
    {
    public:
        enum class TransportAction : std::uint8_t
        {
            Play,
            Pause,
            StopReset,
            Panic,
            RecArm,
            Metronome,
            TapTempo
        };

        enum class OverlayId : std::uint8_t
        {
            SamplePool,
            SoundBank,
            MachinePicker,  // step-grid re-skin, not a floating overlay
        };

        virtual ~CommandEffects() = default;

        virtual void status(const juce::String& msg) = 0;
        virtual void requestRepaint() = 0;
        virtual void transport(TransportAction action) = 0;
        virtual void machineAssign(int track, const char* id) = 0;
        virtual void openOverlay(OverlayId id, int param = 0) = 0;
        virtual void crossfader(float value) = 0;
    // Auto-release a transient modifier latch after its terminal action.
    // No-op when the modifier is not latched (physically held is unaffected).
        virtual void releaseLatch(ControllerButton cb) = 0;

    // Scene paste operations (stay editor-side due to async confirm dialog).
        virtual void sceneFloorPaste() = 0;  // floor-only (mute+func+paste)
        virtual void sceneFullPaste(int destSlot) = 0;  // full baked paste + conflict check

    // Execute a pending confirmation (kind + target captured at arm time).
    // Called by CommandCore after the user presses P (CONFIRM) in PendingConfirm layer.
        virtual void executeConfirm(ConfirmKind kind, int target) = 0;

    // Morph operations.
        virtual void morphBake(int track) = 0;  // commit A-side to base
        virtual void morphErase(int track) = 0;  // remove all morph overrides

    // Mute / solo operations.
        virtual void globalMuteToggle(int track) = 0;  // APVTS-level immediate mute
        virtual void soloToggle(int track) = 0;  // additive solo
        virtual void sceneMuteToggle(int track) = 0;  // current-scene active-mask
        virtual void fluidMuteToggle(int track) = 0;  // fluid-mute morph on Level slot
        virtual void toggleCapture() = 0;             // arm/disarm WAV capture

    // ── 9.12 Stage 6 ─────────────────────────────────────────────────────────
    // The effects the gesture-axis actions dispatch through. Each corresponds to
    // an imperative branch that stages 7-8 delete: the branch and the effect must
    // share one implementation (the effect calls the same editor helper the branch
    // does), never two copies that can drift.
        virtual void latchModifier(ControllerButton cb) = 0;  // dbl-tap: latch scope on
        virtual void escapeOverlay() = 0;                     // Func dbl-tap: leave overlay
        virtual void restorePop() = 0;                        // Func+Snapshot: pop one
        virtual void restoreFloor() = 0;                      // Func+Snapshot hold: to floor
        virtual void recordArmOverdub() = 0;                  // RecordArm dbl-tap
        // (PlayStopToggle deliberately has no effect of its own: it reuses
        //  transport(Play), which is already the mode-aware toggle. A second path to
        //  the same behaviour is the drift this stage exists to prevent.)
        virtual void stepLatch(int step) = 0;                 // Step dbl-tap: latch the hold
        virtual void navPageUnlock() = 0;                     // NavRight dbl-tap
        virtual void openGeneratorHub() = 0;                  // TapTempo hold
        virtual void setTrigGridMode(TrigGridMode mode) = 0;  // retrig / sound-pool pickers
    };
}

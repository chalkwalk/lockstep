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
    };
}

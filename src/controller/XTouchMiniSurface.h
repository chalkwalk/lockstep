#pragma once

#include <array>
#include "IControllerSurface.h"
#include "../io/DoubleTapDetector.h"

namespace lockstep
{
    // Bespoke controller surface for the Behringer X-Touch Mini in MCU mode.
    //
    // Input (onInput):
    //   CC 16-23 turns    → applyParamDelta to MZ slots 0-7
    //   Note 32-39 push   → double-click = resetSlot; single-click unbound
    //   Notes (step grid) → Step ButtonDown / ButtonUp events
    //   Note 84 (Layer A) → NavUp down/up
    //   Note 85 (Layer B) → NavDown down/up
    //   Pitch bend ch 9   → setCrossfader (0..1)
    //
    // Feedback (render):
    //   Grid buttons  → off / flash (vel=1) / on (vel=127) from CellState
    //   A/B buttons   → lit when mode-active
    //   Encoder rings → mode + position from model.slots (§35.8.5)
    //   Diffs against shadow cache; only changed cells generate MIDI output.
    //
    // Hardware reference: XTOUCHMINI_MCU.md (confirmed 2026-06-02).
    class XTouchMiniSurface : public IControllerSurface
    {
    public:
        XTouchMiniSurface();

        void onInput(const juce::MidiMessage& msg, ControllerEventSink& sink) override;
        void render(const SurfaceModel& model, juce::MidiOutput& out) override;

    private:
        static int decodeDelta(int ccValue) noexcept;
        static uint8_t cellStateToVelocity(CellState state, const CellDecoration& border) noexcept;

        // Per-encoder double-click tracking (Note 32-39 = encoders 0-7).
        // Uses DoubleTapDetector (threshold kThresholdMs = 350 ms).
        std::array<DoubleTapDetector, 8> encoderDoubleTap_{};

        // Shadow caches for render() diff — indexed by kStepNotes/kLayerNotes positions.
        // Avoids hammering the device with unchanged LED state every 30 Hz frame.
        std::array<uint8_t, 18> ledShadow_{};    // 16 grid + A + B; 255 = uninitialised
        std::array<uint8_t, 8> ringShadow_{};   // 8 encoder rings; 255 = uninitialised

        // Step index 0-15 → MIDI note number (confirmed via controller_probe, 2026-06-02).
        static constexpr std::array<int, 16> kStepNotes = {
            89, 90, 40, 41, 42, 43, 44, 45,   // top row, steps 0-7
            87, 88, 91, 92, 86, 93, 94, 95     // bottom row, steps 8-15
        };

        static constexpr int kLayerANote = 84;
        static constexpr int kLayerBNote = 85;

        static constexpr int kEncoderCCBase = 16;  // CC 16-23 = turns for encoders 0-7
        static constexpr int kEncoderPushBase = 32;  // Note 32-39 = pushes for encoders 0-7
        static constexpr int kRingCCBase = 48;  // CC 48-55 = ring LEDs for encoders 0-7
    };
}

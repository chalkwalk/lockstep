#include "XTouchMiniSurface.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace lockstep
{
    XTouchMiniSurface::XTouchMiniSurface()
    {
        ledShadow_.fill(255);   // 255 = uninitialised → forces first-frame emit
        ringShadow_.fill(255);
    }

    // Signed-magnitude relative: CW = 1..63 (delta = +value), CCW = 65..127 (delta = -(value-64)).
    int XTouchMiniSurface::decodeDelta(int ccValue) noexcept
    {
        if (ccValue >= 1 && ccValue <= 63)
            return ccValue;
        if (ccValue >= 65 && ccValue <= 127)
            return -(ccValue - 64);
        return 0;
    }

    // Map CellState + playhead decoration to one of three legal LED velocities.
    uint8_t XTouchMiniSurface::cellStateToVelocity(CellState state,
                                                     const CellDecoration& border) noexcept
    {
        // Playhead cursor: flash, regardless of trig state.
        if (border.present && border.token == CellState::StepPlayhead)
            return 1;   // flash

        // CellState is add-only: list all known tokens so the compiler warns
        // when a new token is added without updating the controller mapping.
        //
        // Three LED states:  0 = off,  1 = flash (cursor/selected),  127 = solid (occupied/active).
        switch (state)
        {
            // Flash: "this is the cursor / currently selected item" — distinguishes
            // selection from mere occupancy in list/picker/selector modes.
            case CellState::StepHeld:           // step being held for P-Lock edit
            case CellState::SelectorCurrent:    // selected phrase / pattern
            case CellState::MachineCurrent:     // active machine in picker
            case CellState::NoteEditActive:     // this note is present and active
            case CellState::NoteEditStaged:     // staged for removal
            case CellState::LengthBoundary:     // exact phrase-length boundary
                return 1;

            // Solid on: "this position is occupied / this mode is on".
            case CellState::StepTrigCertain:
            case CellState::StepTrigProbable:
            case CellState::StepFillAdd:
            case CellState::SelectorOccupied:
            case CellState::SelectorNext:
            case CellState::SelectorChain:
            case CellState::SelectorDeviated:
            case CellState::SelectorHome:
            case CellState::MachineAvailable:
            case CellState::ModeActive:
            case CellState::Pressed:
            case CellState::LengthInRun:
            case CellState::ChromaticWhite:
            case CellState::ChromaticBlack:
            case CellState::LevelsCell:
            case CellState::MuteAudible:
                return 127;

            // Off: empty, suppressed, out-of-range, muted, or background.
            case CellState::Resting:
            case CellState::FuncHeld:
            case CellState::Disabled:
            case CellState::StepEmpty:
            case CellState::StepTrigSuppressed:
            case CellState::StepFillSuppress:
            case CellState::StepOutOfRange:
            case CellState::StepPlayhead:    // covered by border check above
            case CellState::SelectorEmpty:
            case CellState::SelectorOutRange:
            case CellState::MuteMuted:
            case CellState::MachineUnavailable:
            case CellState::NoteEditOther:
            case CellState::NoteEditResting:
            case CellState::LengthOutRun:
                return 0;
        }
        return 0;  // unreachable; satisfies non-void return
    }

    void XTouchMiniSurface::onInput(const juce::MidiMessage& msg, ControllerEventSink& sink)
    {
        using T = ControllerEvent::Type;
        using CB = ControllerButton;

        // --- Encoder turns: CC 16-23 ---
        if (msg.isController())
        {
            const int cc  = msg.getControllerNumber();
            const int val = msg.getControllerValue();

            if (cc >= kEncoderCCBase && cc < kEncoderCCBase + 8)
            {
                const int slot  = cc - kEncoderCCBase;
                const int delta = decodeDelta(val);
                if (delta != 0 && sink.applyParamDelta)
                    sink.applyParamDelta(slot, delta);
                return;
            }
        }

        // --- Note events: pushes, grid buttons, layer A/B ---
        if (msg.isNoteOnOrOff())
        {
            const int  note   = msg.getNoteNumber();
            const bool isDown = (msg.getVelocity() > 0);

            // Encoder pushes: Note 32-39.
            if (note >= kEncoderPushBase && note < kEncoderPushBase + 8)
            {
                if (isDown)
                {
                    const int    enc  = note - kEncoderPushBase;
                    const auto   now  = juce::Time::currentTimeMillis();
                    const bool   dbl  = (now - lastPushMs_[static_cast<std::size_t>(enc)]) < kDoubleClickMs;
                    lastPushMs_[static_cast<std::size_t>(enc)] = now;

                    if (dbl && sink.resetSlot)
                        sink.resetSlot(enc);
                    // Single push: unbound for now.
                }
                return;
            }

            // Layer A/B.
            if (note == kLayerANote)
            {
                if (sink.emitEvent)
                    sink.emitEvent({ isDown ? T::ButtonDown : T::ButtonUp, CB::NavUp, -1, 0 });
                return;
            }
            if (note == kLayerBNote)
            {
                if (sink.emitEvent)
                    sink.emitEvent({ isDown ? T::ButtonDown : T::ButtonUp, CB::NavDown, -1, 0 });
                return;
            }

            // Grid step buttons: linear scan of kStepNotes[].
            for (int step = 0; step < 16; ++step)
            {
                if (kStepNotes[static_cast<std::size_t>(step)] == note)
                {
                    if (sink.emitEvent)
                        sink.emitEvent({ isDown ? T::ButtonDown : T::ButtonUp, CB::Step, step, 0 });
                    return;
                }
            }
        }

        // --- Fader: pitch bend on channel 9 ---
        if (msg.isPitchWheel() && msg.getChannel() == 9)
        {
            constexpr float kFaderTop = 16256.0f;
            const float norm = juce::jlimit(0.0f, 1.0f,
                                             static_cast<float>(msg.getPitchWheelValue()) / kFaderTop);
            if (sink.setCrossfader)
                sink.setCrossfader(norm);
        }
    }

    void XTouchMiniSurface::render(const SurfaceModel& model, juce::MidiOutput& out)
    {
        // --- Grid button LEDs (steps 0-15) ---
        for (int step = 0; step < 16; ++step)
        {
            const auto& cell = model.step[static_cast<std::size_t>(step)];
            const uint8_t vel = cellStateToVelocity(cell.base, cell.border);
            const auto    idx = static_cast<std::size_t>(step);

            if (vel != ledShadow_[idx])
            {
                ledShadow_[idx] = vel;
                out.sendMessageNow(juce::MidiMessage::noteOn(
                    1, kStepNotes[idx], static_cast<juce::uint8>(vel)));
            }
        }

        // --- Layer A / B LEDs (indices 16, 17 in shadow) ---
        auto sendButton = [&](int shadowIdx, int note, CellState state) {
            const uint8_t vel = (state == CellState::ModeActive
                                 || state == CellState::SelectorCurrent
                                 || state == CellState::Pressed) ? 127u : 0u;
            const auto si = static_cast<std::size_t>(shadowIdx);
            if (vel != ledShadow_[si])
            {
                ledShadow_[si] = vel;
                out.sendMessageNow(juce::MidiMessage::noteOn(
                    1, note, static_cast<juce::uint8>(vel)));
            }
        };
        sendButton(16, kLayerANote, model.navUp.base);
        sendButton(17, kLayerBNote, model.functionRow[3].base);

        // --- Encoder ring LEDs (§35.8.5) ---
        // Reads ring position and mode from model.slots[], which buildSurfaceModel
        // fills from the processor via OEB resolution. No direct proc_ access needed.
        for (int enc = 0; enc < 8; ++enc)
        {
            const auto ri   = static_cast<std::size_t>(enc);
            const auto& slot = model.slots[ri];

            if (!slot.inRange)
            {
                if (ringShadow_[ri] != 0)
                {
                    ringShadow_[ri] = 0;
                    out.sendMessageNow(juce::MidiMessage::controllerEvent(
                        1, kRingCCBase + enc, 0));
                }
                continue;
            }

            const int pos = juce::roundToInt(slot.position * 11.0f);

            uint8_t mode;
            switch (slot.ringMode)
            {
                case RingMode::Dot:               mode = 0x00; break;
                case RingMode::BipolarFromCentre: mode = 0x10; break;
                case RingMode::UnipolarFill:
                default:                          mode = 0x20; break;
            }

            const uint8_t ringByte = static_cast<uint8_t>(mode | pos);
            if (ringByte != ringShadow_[ri])
            {
                ringShadow_[ri] = ringByte;
                out.sendMessageNow(juce::MidiMessage::controllerEvent(
                    1, kRingCCBase + enc, static_cast<int>(ringByte)));
            }
        }
    }
}

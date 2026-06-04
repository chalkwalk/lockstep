#pragma once

#include <functional>
#include <span>
#include "../ui/SurfaceModel.h"
#include "../io/ControllerEvent.h"

namespace juce { class MidiOutput; class MidiMessage; }

namespace lockstep
{
    // Global-level targets for the three extra encoders that fall outside the
    // per-track manipulation zone (Push 1: Tempo/Swing/Master volume).
    enum class GlobalTarget : uint8_t { Tempo, Swing, Master };

    // Thin callback bundle the editor supplies so IControllerSurface::onInput()
    // can push decoded events and param writes without coupling to the editor.
    struct ControllerEventSink
    {
        std::function<void(ControllerEvent)>                      emitEvent;        // injects into editor dispatch
        std::function<void(int mzSlot, int rawDelta)>             applyParamDelta;  // encoder delta → MZ slot write
        std::function<void(int mzSlot)>                           resetSlot;        // double-click reset to default
        std::function<void(float normValue)>                      setCrossfader;    // sets crossfader to 0..1
        std::function<void(GlobalTarget target, int rawDelta)>    applyGlobalDelta; // Tempo/Swing/Master encoder
    };

    // Authoring seam for hardware controller surfaces (§35.8.4 / DESIGN §35).
    // Both the screen and every IControllerSurface call buildSurfaceModel(); they
    // cannot diverge because they share the same pure builder function.
    class IControllerSurface
    {
    public:
        virtual ~IControllerSurface() = default;

        // Called once (on the message thread) immediately after the MIDI ports are
        // opened — and again on each hotplug reconnect. Use it to send init SysEx
        // (MODE_CHANGE, strip mode, display clear, etc.) and reset shadow caches.
        // Default no-op; X-Touch Mini needs no one-time init.
        virtual void onConnect(juce::MidiOutput& out) { juce::ignoreUnused(out); }

        // Called on the message thread (messages drained from the MIDI-thread FIFO).
        virtual void onInput(const juce::MidiMessage& msg, ControllerEventSink& sink) = 0;

        // Called at ~30 Hz. Must diff against an internal shadow cache and emit
        // MIDI only for changed cells so the device wire stays quiet.
        virtual void render(const SurfaceModel& model, juce::MidiOutput& out) = 0;

        // Optional hint: CellState tokens this surface cares about. Reserved for a
        // future selective-rebuild optimisation; return {} to receive all cells.
        [[nodiscard]] virtual std::span<const CellState> statesOfInterest() const { return {}; }
    };
}

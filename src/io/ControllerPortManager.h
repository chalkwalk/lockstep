#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <juce_audio_devices/juce_audio_devices.h>

namespace lockstep
{
    class IControllerSurface;
    struct ControllerEventSink;
    struct SurfaceModel;

    // Owns a MidiInput + MidiOutput for an external controller, identified by a
    // case-insensitive display-name substring (e.g. "X-TOUCH MINI").
    //
    // The MIDI callback (MIDI thread) pushes raw messages into a lock-free FIFO.
    // Input and feedback are split across two seams (DESIGN §35.9.3 — input ≠
    // render): drainInput() runs on the editor's ~30 Hz tick (encoders/buttons
    // must be serviced even when nothing is redrawing); renderSurface() runs from
    // the single invalidation channel's onFrame, so feedback LEDs update on
    // events, not on an unconditional poll. A 1 Hz timer handles hotplug: reopens
    // the device if it disappears and reappears.
    class ControllerPortManager : private juce::MidiInputCallback,
                                  private juce::Timer
    {
    public:
        // nameSubstring: primary case-insensitive name match (e.g. "X-TOUCH MINI").
        // fallbackSubstring: tried if the primary matches nothing (e.g. Linux ALSA
        //   names a second port "…MIDI 2" rather than "…User Port"). Empty = unused.
        explicit ControllerPortManager(juce::String nameSubstring,
                                       juce::String fallbackSubstring = {});
        ~ControllerPortManager() override;

        // Message thread, every tick: drain the input FIFO into surface.onInput().
        // No model, no output needed — encoders/buttons are serviced even when the
        // surface is idle. Returns true if any message was processed, so the caller
        // can mark the surface dirty (input may have moved a param → LED rings must
        // re-render through the invalidation channel).
        bool drainInput(IControllerSurface& surface, ControllerEventSink& sink);

        // Message thread, from onFrame: render feedback LEDs for the current model.
        // No-op when output is closed; fires surface.onConnect() once per open
        // before the first render.
        void renderSurface(IControllerSurface& surface, const SurfaceModel& model);

        [[nodiscard]] bool isOpen() const noexcept { return midiIn_ != nullptr; }

        // Identifier of the currently open input device, or empty if not open.
        // The caller (standalone editor) can pass this to AudioDeviceManager::
        // setMidiInputDeviceEnabled(id, false) so the device is not also routed
        // as raw MIDI notes to processBlock.
        [[nodiscard]] juce::String openedInputId() const
        {
            return juce::String(lastInputId_);
        }

        // Called once on open and once on close (message thread).
        std::function<void(bool open)> onStateChange;

    private:
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
        void timerCallback() override;

        void tryOpen();
        void closeAll();

        juce::String nameSubstring_;
        juce::String fallbackSubstring_;

        std::unique_ptr<juce::MidiInput> midiIn_;
        std::unique_ptr<juce::MidiOutput> midiOut_;
        std::string lastInputId_;
        bool justOpened_ = false; // consumed by drain() to call onConnect once

        static constexpr int kFifoSize = 256;
        juce::AbstractFifo fifo_{ kFifoSize };
        std::array<juce::MidiMessage, kFifoSize> msgBuf_;
    };
}

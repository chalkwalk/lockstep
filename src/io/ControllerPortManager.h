#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <juce_audio_devices/juce_audio_devices.h>

namespace lockstep
{
    class  IControllerSurface;
    struct ControllerEventSink;
    struct SurfaceModel;

    // Owns a MidiInput + MidiOutput for an external controller, identified by a
    // case-insensitive display-name substring (e.g. "X-TOUCH MINI").
    //
    // The MIDI callback (MIDI thread) pushes raw messages into a lock-free FIFO.
    // drain() is called on the message thread (~30 Hz timer in the editor) and
    // routes buffered messages to IControllerSurface::onInput, then calls render().
    // A 1 Hz timer handles hotplug: reopens the device if it disappears and
    // reappears.
    class ControllerPortManager : private juce::MidiInputCallback,
                                   private juce::Timer
    {
    public:
        explicit ControllerPortManager(juce::String nameSubstring);
        ~ControllerPortManager() override;

        // Call on the message thread. Drains the FIFO into surface.onInput(),
        // then calls surface.render(model, *midiOut_) if output is open.
        void drain(IControllerSurface& surface,
                   ControllerEventSink& sink,
                   const SurfaceModel& model);

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

        std::unique_ptr<juce::MidiInput>  midiIn_;
        std::unique_ptr<juce::MidiOutput> midiOut_;
        std::string lastInputId_;

        static constexpr int kFifoSize = 256;
        juce::AbstractFifo                       fifo_{ kFifoSize };
        std::array<juce::MidiMessage, kFifoSize> msgBuf_;
    };
}

#include "ControllerPortManager.h"
#include "../controller/IControllerSurface.h"

namespace lockstep
{
    ControllerPortManager::ControllerPortManager(juce::String nameSubstring,
                                                 juce::String fallbackSubstring)
        : nameSubstring_(std::move(nameSubstring)), fallbackSubstring_(std::move(fallbackSubstring))
    {
        tryOpen();
        startTimerHz(1);
    }

    ControllerPortManager::~ControllerPortManager()
    {
        stopTimer();
        closeAll();
    }

    // MIDI thread — push into FIFO only; no UI/param access.
    void ControllerPortManager::handleIncomingMidiMessage(juce::MidiInput*,
                                                          const juce::MidiMessage& msg)
    {
        int s1, n1, s2, n2;
        fifo_.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 > 0)
            msgBuf_[static_cast<std::size_t>(s1)] = msg;
        else if (n2 > 0)
            msgBuf_[static_cast<std::size_t>(s2)] = msg;
        fifo_.finishedWrite((n1 > 0 || n2 > 0) ? 1 : 0);
    }

    bool ControllerPortManager::drainInput(IControllerSurface& surface,
                                           ControllerEventSink& sink)
    {
        int s1, n1, s2, n2;
        fifo_.prepareToRead(fifo_.getNumReady(), s1, n1, s2, n2);

        for (int i = 0; i < n1; ++i)
            surface.onInput(msgBuf_[static_cast<std::size_t>(s1 + i)], sink);
        for (int i = 0; i < n2; ++i)
            surface.onInput(msgBuf_[static_cast<std::size_t>(s2 + i)], sink);

        fifo_.finishedRead(n1 + n2);
        return (n1 + n2) > 0;
    }

    void ControllerPortManager::renderSurface(IControllerSurface& surface,
                                              const SurfaceModel& model)
    {
        if (!midiOut_)
            return;

        // Fire onConnect once per successful open (and on each hotplug reconnect)
        // before the first feedback render.
        if (justOpened_)
        {
            justOpened_ = false;
            surface.onConnect(*midiOut_);
        }

        surface.render(model, *midiOut_);
    }

    // Message thread — hotplug rescan at ~1 Hz.
    void ControllerPortManager::timerCallback()
    {
        if (midiIn_)
        {
            // Verify current device is still present.
            bool stillPresent = false;
            for (const auto& d : juce::MidiInput::getAvailableDevices())
            {
                if (d.identifier.toStdString() == lastInputId_)
                {
                    stillPresent = true;
                    break;
                }
            }
            if (!stillPresent)
                closeAll();
        }

        if (!midiIn_)
            tryOpen();
    }

    void ControllerPortManager::tryOpen()
    {
        // Try primary match, then fallback (e.g. Linux ALSA uses "…MIDI 2" instead
        // of "…User Port" for the Push 1's second port).
        auto tryMatch = [](const juce::String& name,
                           const juce::String& primary,
                           const juce::String& fallback) -> bool {
            if (name.containsIgnoreCase(primary)) return true;
            return !fallback.isEmpty() && name.containsIgnoreCase(fallback);
        };

        for (const auto& d : juce::MidiInput::getAvailableDevices())
        {
            if (tryMatch(d.name, nameSubstring_, fallbackSubstring_))
            {
                midiIn_ = juce::MidiInput::openDevice(d.identifier, this);
                if (midiIn_)
                {
                    midiIn_->start();
                    lastInputId_ = d.identifier.toStdString();
                }
                break;
            }
        }

        // Output scan is separate — on Linux ALSA, in/out have independent identifiers.
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
        {
            if (tryMatch(d.name, nameSubstring_, fallbackSubstring_))
            {
                midiOut_ = juce::MidiOutput::openDevice(d.identifier);
                break;
            }
        }

        if (midiIn_)
        {
            justOpened_ = true;
            if (onStateChange)
                onStateChange(true);
        }
    }

    void ControllerPortManager::closeAll()
    {
        if (midiIn_)
        {
            midiIn_->stop();
            midiIn_.reset();
            if (onStateChange)
                onStateChange(false);
        }
        midiOut_.reset();
        lastInputId_.clear();
    }
}

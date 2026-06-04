#include "ControllerPortManager.h"
#include "../controller/IControllerSurface.h"

namespace lockstep
{
    ControllerPortManager::ControllerPortManager(juce::String nameSubstring)
        : nameSubstring_(std::move(nameSubstring))
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

    void ControllerPortManager::drain(IControllerSurface& surface,
                                       ControllerEventSink& sink,
                                       const SurfaceModel& model)
    {
        // Fire onConnect once per successful open (and on each hotplug reconnect).
        if (justOpened_ && midiOut_)
        {
            justOpened_ = false;
            surface.onConnect(*midiOut_);
        }

        int s1, n1, s2, n2;
        fifo_.prepareToRead(fifo_.getNumReady(), s1, n1, s2, n2);

        for (int i = 0; i < n1; ++i)
            surface.onInput(msgBuf_[static_cast<std::size_t>(s1 + i)], sink);
        for (int i = 0; i < n2; ++i)
            surface.onInput(msgBuf_[static_cast<std::size_t>(s2 + i)], sink);

        fifo_.finishedRead(n1 + n2);

        if (midiOut_)
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
        for (const auto& d : juce::MidiInput::getAvailableDevices())
        {
            if (d.name.containsIgnoreCase(nameSubstring_))
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
            if (d.name.containsIgnoreCase(nameSubstring_))
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

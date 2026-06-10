#include "InPluginTransport.h"

namespace lockstep
{
    InPluginTransport::InPluginTransport(Clock& clock)
        : clock_(clock)
    {
        playBtn_.setClickingTogglesState(false);
        playBtn_.setWantsKeyboardFocus(false);
        playBtn_.onClick = [this] { onPlayClick(); };
        addAndMakeVisible(playBtn_);

        resetBtn_.setWantsKeyboardFocus(false);
        resetBtn_.onClick = [this] { onResetClick(); };
        addAndMakeVisible(resetBtn_);

        recBtn_.setClickingTogglesState(false);
        recBtn_.setWantsKeyboardFocus(false);
        recBtn_.onClick = [this]
        {
            clock_.setRecordArmed(!clock_.isRecordArmed());
            refresh(buildTransportModel(clock_));
        };
        addAndMakeVisible(recBtn_);

        metroBtn_.setClickingTogglesState(false);
        metroBtn_.setWantsKeyboardFocus(false);
        metroBtn_.onClick = [this]
        {
            clock_.setMetronomeEnabled(!clock_.isMetronomeEnabled());
            refresh(buildTransportModel(clock_));
        };
        addAndMakeVisible(metroBtn_);

        startTimerHz(15);
    }

    void InPluginTransport::setGhosted(bool ghosted)
    {
        ghosted_ = ghosted;
        playBtn_.setAlpha(ghosted ? 0.35f : 1.0f);
        resetBtn_.setAlpha(ghosted ? 0.35f : 1.0f);
    }

    void InPluginTransport::refresh(const TransportModel& m)
    {
        if (m.playing != shadow_.playing)
            playBtn_.setButtonText(m.playing ? "Pause" : "Play");

        if (m.recArmed != shadow_.recArmed || m.overdubArmed != shadow_.overdubArmed)
        {
            juce::Colour bg;
            if (m.overdubArmed)        bg = juce::Colour::fromRGB(210, 130, 30);
            else if (m.recArmed)       bg = juce::Colour::fromRGB(200, 50, 50);
            else                       bg = juce::LookAndFeel::getDefaultLookAndFeel()
                                                .findColour(juce::TextButton::buttonColourId);

            recBtn_.setColour(juce::TextButton::buttonColourId, bg);
            recBtn_.setColour(juce::TextButton::textColourOffId,
                              (m.recArmed || m.overdubArmed)
                                  ? juce::Colours::white
                                  : juce::LookAndFeel::getDefaultLookAndFeel()
                                        .findColour(juce::TextButton::textColourOffId));
            recBtn_.setButtonText(m.overdubArmed ? "Overdub" : "Rec");
        }

        if (m.metronomeOn != shadow_.metronomeOn)
        {
            metroBtn_.setColour(juce::TextButton::buttonColourId,
                                m.metronomeOn
                                    ? juce::Colour::fromRGB(60, 140, 200)
                                    : juce::LookAndFeel::getDefaultLookAndFeel()
                                          .findColour(juce::TextButton::buttonColourId));
            metroBtn_.setColour(juce::TextButton::textColourOffId,
                                m.metronomeOn
                                    ? juce::Colours::white
                                    : juce::LookAndFeel::getDefaultLookAndFeel()
                                          .findColour(juce::TextButton::textColourOffId));
        }

        shadow_ = m;
    }

    void InPluginTransport::timerCallback()
    {
        refresh(buildTransportModel(clock_));
    }

    void InPluginTransport::onPlayClick()
    {
        if (ghosted_)
        {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withTitle("DAW transport is in control")
                    .withMessage("In Locked mode the sequencer follows the DAW timeline.\n"
                                 "Use the DAW transport to start/stop, or switch the\n"
                                 "mode selector to Auto for in-plugin playback.")
                    .withButton("OK"),
                nullptr);
            return;
        }
        clock_.setInPluginPlaying(!clock_.inPluginPlaying());
        refresh(buildTransportModel(clock_));
    }

    void InPluginTransport::onResetClick()
    {
        if (ghosted_)
        {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withTitle("DAW transport is in control")
                    .withMessage("In Locked mode the sequencer follows the DAW timeline.\n"
                                 "Use the DAW transport to start/stop, or switch the\n"
                                 "mode selector to Auto for in-plugin playback.")
                    .withButton("OK"),
                nullptr);
            return;
        }
        clock_.setInPluginPlaying(false);
        clock_.resetPhase();
        refresh(buildTransportModel(clock_));
    }

    void InPluginTransport::paint(juce::Graphics& g)
    {
        juce::ignoreUnused(g);
    }

    void InPluginTransport::resized()
    {
        auto b = getLocalBounds();
        playBtn_.setBounds(b.removeFromLeft(54).reduced(1));
        resetBtn_.setBounds(b.removeFromLeft(46).reduced(1));
        recBtn_.setBounds(b.removeFromLeft(54).reduced(1));
        metroBtn_.setBounds(b.removeFromLeft(46).reduced(1));
    }
}

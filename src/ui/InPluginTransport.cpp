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
        recBtn_.onClick = [this] { clock_.setRecordArmed(!clock_.isRecordArmed()); };
        addAndMakeVisible(recBtn_);

        metroBtn_.setClickingTogglesState(false);
        metroBtn_.setWantsKeyboardFocus(false);
        metroBtn_.onClick = [this] { clock_.setMetronomeEnabled(!clock_.isMetronomeEnabled()); };
        addAndMakeVisible(metroBtn_);

        startTimerHz(15);
    }

    void InPluginTransport::setGhosted(bool ghosted)
    {
        ghosted_ = ghosted;
        playBtn_.setAlpha(ghosted ? 0.35f : 1.0f);
        resetBtn_.setAlpha(ghosted ? 0.35f : 1.0f);
    }

    void InPluginTransport::timerCallback()
    {
        syncPlayLabel();
        syncRecColour();
        syncMetroColour();
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
        syncPlayLabel();
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
    }

    void InPluginTransport::syncPlayLabel()
    {
        playBtn_.setButtonText(clock_.inPluginPlaying() ? "Pause" : "Play");
    }

    void InPluginTransport::syncRecColour()
    {
        const bool armed   = clock_.isRecordArmed();
        const bool overdub = clock_.isOverdubArmed();
        juce::Colour bg;
        if (overdub)       bg = juce::Colour::fromRGB(210, 130, 30);  // amber = overdub
        else if (armed)    bg = juce::Colour::fromRGB(200, 50, 50);   // red = plain record
        else               bg = juce::LookAndFeel::getDefaultLookAndFeel()
                                    .findColour(juce::TextButton::buttonColourId);
        recBtn_.setColour(juce::TextButton::buttonColourId, bg);
        recBtn_.setColour(juce::TextButton::textColourOffId,
                          (armed || overdub) ? juce::Colours::white
                                             : juce::LookAndFeel::getDefaultLookAndFeel()
                                                   .findColour(juce::TextButton::textColourOffId));
        recBtn_.setButtonText(overdub ? "Overdub" : "Rec");
    }

    void InPluginTransport::syncMetroColour()
    {
        const bool on = clock_.isMetronomeEnabled();
        metroBtn_.setColour(juce::TextButton::buttonColourId,
                            on ? juce::Colour::fromRGB(60, 140, 200)
                               : juce::LookAndFeel::getDefaultLookAndFeel()
                                     .findColour(juce::TextButton::buttonColourId));
        metroBtn_.setColour(juce::TextButton::textColourOffId,
                            on ? juce::Colours::white
                               : juce::LookAndFeel::getDefaultLookAndFeel()
                                     .findColour(juce::TextButton::textColourOffId));
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

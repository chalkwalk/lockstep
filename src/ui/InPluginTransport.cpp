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
        clock_.resetPhase();
    }

    void InPluginTransport::syncPlayLabel()
    {
        playBtn_.setButtonText(clock_.inPluginPlaying() ? "Pause" : "Play");
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
    }
}

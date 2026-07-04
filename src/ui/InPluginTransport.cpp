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
        recBtn_.onClick = [this] {
            clock_.setRecordArmed(!clock_.isRecordArmed());
            refresh(buildTransportModel(clock_));
        };
        addAndMakeVisible(recBtn_);

        metroBtn_.setClickingTogglesState(false);
        metroBtn_.setWantsKeyboardFocus(false);
        metroBtn_.onClick = [this] {
            clock_.setMetronomeEnabled(!clock_.isMetronomeEnabled());
            refresh(buildTransportModel(clock_));
        };
        addAndMakeVisible(metroBtn_);
        // No own timer (9.15): the editor's always-on tick calls pollState().
    }

    void InPluginTransport::setGhosted(bool ghosted)
    {
        ghosted_ = ghosted;
        // v27: in the hosted-Locked regime the buttons are no longer inert — they
        // park/unpark the plugin (arm gate). Keep them full-alpha and active when
        // the arm handlers are wired; only dim to the legacy ghost when they aren't.
        const bool armControl = ghosted && onPlayVerb != nullptr;
        const float a = (ghosted && !armControl) ? 0.35f : 1.0f;
        playBtn_.setAlpha(a);
        resetBtn_.setAlpha(a);
        refresh(buildTransportModel(clock_));
    }

    void InPluginTransport::refresh(const TransportModel& mIn)
    {
        TransportModel m = mIn;
        // Arm regime: hosted Locked with the arm handlers wired. The Play button
        // reflects pluginArmed (green Armed / dim Park) instead of in-plugin Play.
        m.armRegime = ghosted_ && onPlayVerb != nullptr && isArmed != nullptr;
        m.armed = m.armRegime ? isArmed() : true;

        if (m.armRegime)
        {
            if (m.armed != shadow_.armed || !shadow_.armRegime)
            {
                auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
                playBtn_.setButtonText(m.armed ? "Armed" : "Park");
                playBtn_.setColour(juce::TextButton::buttonColourId,
                                   m.armed ? juce::Colour::fromRGB(40, 150, 70)
                                           : lf.findColour(juce::TextButton::buttonColourId));
                playBtn_.setColour(juce::TextButton::textColourOffId,
                                   m.armed ? juce::Colours::white
                                           : lf.findColour(juce::TextButton::textColourOffId));
            }
            // Note: shadow_ is committed once at the tail so the rec/metro change
            // detection below still compares against the previous frame.
        }
        else if (m.playing != shadow_.playing || shadow_.armRegime)
        {
            // Normal (or just left the arm regime): restore the Play/Pause label
            // and default colour.
            auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
            playBtn_.setButtonText(m.playing ? "Pause" : "Play");
            playBtn_.setColour(juce::TextButton::buttonColourId,
                               lf.findColour(juce::TextButton::buttonColourId));
            playBtn_.setColour(juce::TextButton::textColourOffId,
                               lf.findColour(juce::TextButton::textColourOffId));
        }

        if (m.recArmed != shadow_.recArmed || m.overdubArmed != shadow_.overdubArmed)
        {
            juce::Colour bg;
            if (m.overdubArmed) bg = juce::Colour::fromRGB(210, 130, 30);
            else if (m.recArmed) bg = juce::Colour::fromRGB(200, 50, 50);
            else bg = juce::LookAndFeel::getDefaultLookAndFeel()
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

    void InPluginTransport::onPlayClick()
    {
        if (ghosted_)
        {
            // v27: hosted Locked — park/unpark the plugin via the mode-aware verb
            // instead of the old "DAW in control" dead-end popup.
            if (onPlayVerb)
            {
                onPlayVerb();
                refresh(buildTransportModel(clock_));
                return;
            }
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
            if (onStopVerb)
            {
                onStopVerb();
                refresh(buildTransportModel(clock_));
                return;
            }
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

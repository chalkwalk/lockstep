#include "InPluginTransport.h"
#include "ChromeLookAndFeel.h"

namespace lockstep
{
    // A transport button is either LIT -- it is saying something (armed, recording,
    // clicking), and the colour IS the message -- or it is resting, in which case it
    // must look like every other button on the surface. Resting therefore REMOVES the
    // override rather than re-asserting a colour it fetched from somewhere: an
    // override that names the resting colour is an override that stops inheriting,
    // which is how this button row drifted away from the rest of the chrome in the
    // first place (it was reading the DEFAULT LookAndFeel, which is not the one the
    // editor installs).
    static void setStateColour(juce::TextButton& b, bool lit, juce::Colour litColour)
    {
        if (lit)
        {
            // semantic colour: the transport is SAYING something (armed / recording /
            // overdubbing / clicking) and the colour is the message, not decoration.
            b.setColour(juce::TextButton::buttonColourId, litColour);
            b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
        else
        {
            b.removeColour(juce::TextButton::buttonColourId);
            b.removeColour(juce::TextButton::textColourOffId);
        }
    }

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
        const bool armControl = ghosted && onPlayVerb != nullptr && isArmed != nullptr;
        const float a = (ghosted && !armControl) ? 0.35f : 1.0f;
        playBtn_.setAlpha(a);
        resetBtn_.setAlpha(a);
        // In the arm regime the single Play button is the Armed/Park toggle; Stop
        // (== park) is redundant, so hide it and re-lay-out to one button.
        armRegimeLayout_ = armControl;
        resetBtn_.setVisible(!armRegimeLayout_);
        resized();
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
                playBtn_.setButtonText(m.armed ? "Armed" : "Park");
                setStateColour(playBtn_, m.armed, juce::Colour::fromRGB(40, 150, 70));
            }
            // Note: shadow_ is committed once at the tail so the rec/metro change
            // detection below still compares against the previous frame.
        }
        else if (m.playing != shadow_.playing || shadow_.armRegime)
        {
            // Normal (or just left the arm regime): restore the Play/Pause label and
            // fall back to the chrome look.
            playBtn_.setButtonText(m.playing ? "Pause" : "Play");
            setStateColour(playBtn_, false, {});
        }

        if (m.recArmed != shadow_.recArmed || m.overdubArmed != shadow_.overdubArmed)
        {
            const bool lit = m.recArmed || m.overdubArmed;
            setStateColour(recBtn_, lit,
                           m.overdubArmed ? juce::Colour::fromRGB(210, 130, 30)
                                          : juce::Colour::fromRGB(200, 50, 50));
            recBtn_.setButtonText(m.overdubArmed ? "Overdub" : "Rec");
        }

        if (m.metronomeOn != shadow_.metronomeOn)
            setStateColour(metroBtn_, m.metronomeOn, juce::Colour::fromRGB(60, 140, 200));

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
        // The transport is the HOT band, but it says so by where it sits, not by
        // being built out of bigger buttons than the rest of the chrome: one control
        // height across the surface (ChromeLookAndFeel::kControlH).
        auto b = getLocalBounds().withSizeKeepingCentre(
            getWidth(), juce::jmin(ChromeLookAndFeel::kControlH, getHeight()));
        // Arm regime: one Armed/Park button (Stop hidden), given the pair's width.
        playBtn_.setBounds(b.removeFromLeft(armRegimeLayout_ ? 72 : 54).reduced(2, 0));
        if (!armRegimeLayout_)
        {
            resetBtn_.setBounds(b.removeFromLeft(46).reduced(2, 0));
        }
        recBtn_.setBounds(b.removeFromLeft(54).reduced(2, 0));
        metroBtn_.setBounds(b.removeFromLeft(46).reduced(2, 0));
    }
}
